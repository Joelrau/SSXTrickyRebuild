#include "World/SSXTerrainLodComponent.h"

#include "Components/StaticMeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "LocalVertexFactory.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "World/SSXRenderLook.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "PrimitiveViewRelevance.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "Sim/SimCoords.h"
#include "SSXTrickyRebuild.h"
#include "StaticMeshResources.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SSXTerrainLodComponent)

static TAutoConsoleVariable<int32> CVarTerrainLod(TEXT("ssx.TerrainLod"), 1,
	TEXT("Terrain patch LOD: 1 = the original's per-edge levels k/7 / k/5 / k/3 per view (default), ")
	TEXT("0 = every patch at k/7 (the imported static mesh)."));

namespace
{
	constexpr int32 SlotIndices = SSXTerrainLod::MaxTris * 3;
	constexpr int32 MaxViewSlots = 2;

	class FSSXTerrainLodSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		FSSXTerrainLodSceneProxy(USSXTerrainLodComponent* Component)
			: FPrimitiveSceneProxy(Component)
			, Data(Component->GetRenderData())
			, VertexFactory(GetScene().GetFeatureLevel(), "FSSXTerrainLodSceneProxy")
			, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetShaderPlatform()))
		{
			const FSSXTerrainLodRenderData& D = *Data;
			const int32 NumVerts = D.Positions.Num();
			VertexBuffers.PositionVertexBuffer.Init(D.Positions);
			VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
			VertexBuffers.StaticMeshVertexBuffer.Init(NumVerts, 3);
			for (int32 I = 0; I < NumVerts; ++I)
			{
				FVector3f TX, TY;
				const FVector3f N = D.Normals[I];
				N.FindBestAxisVectors(TX, TY);
				VertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(I, TX, TY, N);
				VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I, 0, D.Uv0[I]);
				VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I, 1, D.Uv1[I]);
				VertexBuffers.StaticMeshVertexBuffer.SetVertexUV(I, 2, D.Uv2[I]);
			}
			VertexBuffers.ColorVertexBuffer.InitFromSingleColor(FColor::White, NumVerts);

			const int32 NumPatches = D.NumPatches();
			const int32 Near = SSXTerrainLod::NumSignatures - 1;
			for (FViewSlot& Slot : Slots)
			{
				Slot.Signature.Init(uint8(Near), NumPatches);
				Slot.IndexBuffer.Indices.SetNumUninitialized(NumPatches * SlotIndices);
				for (int32 P = 0; P < NumPatches; ++P)
				{
					WritePatch(Slot.IndexBuffer.Indices, P, Near);
				}
			}

			for (int32 S = 0; S < D.Sections.Num(); ++S)
			{
				UMaterialInterface* M = Component->GetMaterial(S);
				Materials.Add(M ? M : UMaterial::GetDefaultMaterial(MD_Surface));
			}

			ENQUEUE_RENDER_COMMAND(InitSSXTerrainLod)([this](FRHICommandListImmediate& RHICmdList)
			{
				VertexBuffers.PositionVertexBuffer.InitResource(RHICmdList);
				VertexBuffers.StaticMeshVertexBuffer.InitResource(RHICmdList);
				VertexBuffers.ColorVertexBuffer.InitResource(RHICmdList);
				FLocalVertexFactory::FDataType VfData;
				VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory, VfData);
				VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory, VfData);
				VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&VertexFactory, VfData);
				VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&VertexFactory, VfData, 0);
				VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory, VfData);
				VertexFactory.SetData(RHICmdList, VfData);
				VertexFactory.InitResource(RHICmdList);
				for (FViewSlot& Slot : Slots)
				{
					Slot.IndexBuffer.InitResource(RHICmdList);
				}
			});
		}

		virtual ~FSSXTerrainLodSceneProxy()
		{
			VertexBuffers.PositionVertexBuffer.ReleaseResource();
			VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
			VertexBuffers.ColorVertexBuffer.ReleaseResource();
			VertexFactory.ReleaseResource();
			for (FViewSlot& Slot : Slots)
			{
				Slot.IndexBuffer.ReleaseResource();
			}
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap,
			FMeshElementCollector& Collector) const override
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_SSXTerrainLod_GetDynamicMeshElements);
			const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
			FColoredMaterialRenderProxy* Wire = nullptr;
			if (bWireframe)
			{
				Wire = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
					FLinearColor(0.0f, 0.5f, 1.0f));
				Collector.RegisterOneFrameMaterialProxy(Wire);
			}
			const FSSXTerrainLodRenderData& D = *Data;
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1 << ViewIndex)))
				{
					continue;
				}
				FViewSlot& Slot = Slots[FMath::Min(ViewIndex, MaxViewSlots - 1)];
				const FVector Eye = SSXSim::UnrealToGame(Views[ViewIndex]->ViewMatrices.GetViewOrigin());
				UpdateSlot(Collector.GetRHICommandList(), Slot, FVector3f(Eye));

				FDynamicPrimitiveUniformBuffer& Uniform = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				FPrimitiveUniformShaderParametersBuilder Builder;
				BuildUniformShaderParameters(Builder);
				Uniform.Set(Collector.GetRHICommandList(), Builder);
				for (int32 S = 0; S < D.Sections.Num(); ++S)
				{
					const FSSXTerrainLodRenderData::FSection& Sec = D.Sections[S];
					if (Sec.NumPatches == 0)
					{
						continue;
					}
					FMeshBatch& Mesh = Collector.AllocateMesh();
					FMeshBatchElement& E = Mesh.Elements[0];
					E.IndexBuffer = &Slot.IndexBuffer;
					E.PrimitiveUniformBufferResource = &Uniform.UniformBuffer;
					E.FirstIndex = Sec.FirstPatch * SlotIndices;
					E.NumPrimitives = Sec.NumPatches * SSXTerrainLod::MaxTris;
					E.MinVertexIndex = Sec.FirstPatch * SSXTerrainLod::VertsPerPatch;
					E.MaxVertexIndex = (Sec.FirstPatch + Sec.NumPatches) * SSXTerrainLod::VertsPerPatch - 1;
					Mesh.bWireframe = bWireframe;
					Mesh.VertexFactory = &VertexFactory;
					Mesh.MaterialRenderProxy = bWireframe ? Wire : Materials[S]->GetRenderProxy();
					Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
					Mesh.Type = PT_TriangleList;
					Mesh.DepthPriorityGroup = SDPG_World;
					Mesh.bCanApplyViewModeOverrides = false;
					Collector.AddMesh(ViewIndex, Mesh);
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bDynamicRelevance = true;
			Result.bRenderInMainPass = ShouldRenderInMainPass();
			Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
			Result.bRenderCustomDepth = ShouldRenderCustomDepth();
			Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
			return Result;
		}

		virtual bool CanBeOccluded() const override { return !MaterialRelevance.bDisableDepthTest; }
		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

	private:
		struct FViewSlot
		{
			FDynamicMeshIndexBuffer32 IndexBuffer;
			TArray<uint8> Signature;
		};

		static void WritePatch(TArray<uint32>& Indices, int32 Patch, int32 Signature)
		{
			const TArray<uint32>& T = SSXTerrainLod::Template(Signature);
			const uint32 Base = uint32(Patch * SSXTerrainLod::VertsPerPatch);
			uint32* Dst = Indices.GetData() + Patch * SlotIndices;
			for (int32 I = 0; I < T.Num(); ++I)
			{
				Dst[I] = Base + T[I];
			}
			for (int32 I = T.Num(); I < SlotIndices; ++I)
			{
				Dst[I] = Base;
			}
		}

		void UpdateSlot(FRHICommandListBase& RHICmdList, FViewSlot& Slot, const FVector3f& Eye) const
		{
			const FSSXTerrainLodRenderData& D = *Data;
			int32 First = MAX_int32, Last = -1, Changed = 0;
			for (int32 P = 0; P < Slot.Signature.Num(); ++P)
			{
				uint8 L[4];
				SSXTerrainLod::EdgeLevels(&D.Corners[P * 4], Eye, L);
				const int32 Sig = SSXTerrainLod::SignatureIndex(L);
				if (Sig != Slot.Signature[P])
				{
					Slot.Signature[P] = uint8(Sig);
					WritePatch(Slot.IndexBuffer.Indices, P, Sig);
					First = FMath::Min(First, P);
					Last = P;
					++Changed;
				}
			}
			if (Last < 0 || !Slot.IndexBuffer.IndexBufferRHI.IsValid())
			{
				return;
			}
			UE_LOG(LogSSX, Verbose, TEXT("[terrain lod] eye (%.0f, %.0f, %.0f): %d of %d patches changed level"), Eye.X, Eye.Y, Eye.Z, Changed, Slot.Signature.Num());
			const uint32 Offset = uint32(First) * SlotIndices * sizeof(uint32);
			const uint32 Size = uint32(Last - First + 1) * SlotIndices * sizeof(uint32);
			void* Dst = RHICmdList.LockBuffer(Slot.IndexBuffer.IndexBufferRHI, Offset, Size, RLM_WriteOnly);
			FMemory::Memcpy(Dst, Slot.IndexBuffer.Indices.GetData() + First * SlotIndices, Size);
			RHICmdList.UnlockBuffer(Slot.IndexBuffer.IndexBufferRHI);
		}

		TSharedPtr<const FSSXTerrainLodRenderData> Data;
		FStaticMeshVertexBuffers VertexBuffers;
		FLocalVertexFactory VertexFactory;
		mutable FViewSlot Slots[MaxViewSlots];
		TArray<UMaterialInterface*> Materials;
		FMaterialRelevance MaterialRelevance;
	};

	/** Texture index of a terrain material slot: the glTF material "tex_NNN" names the slot and the material instance ("tex_NNN_errain"). */
	int32 SlotTexture(const UStaticMeshComponent& C, int32 Slot)
	{
		TArray<FString> Names;
		const TArray<FName> SlotNames = C.GetMaterialSlotNames();
		if (SlotNames.IsValidIndex(Slot)) { Names.Add(SlotNames[Slot].ToString()); }
		if (const UMaterialInterface* M = C.GetMaterial(Slot)) { Names.Add(M->GetName()); }
		for (const FString& N : Names)
		{
			if (N.StartsWith(TEXT("tex_")) && N.Len() >= 7 && FChar::IsDigit(N[4]) && FChar::IsDigit(N[5]) && FChar::IsDigit(N[6]))
			{
				return FCString::Atoi(*N.Mid(4, 3));
			}
		}
		return INDEX_NONE;
	}
}

USSXTerrainLodComponent::USSXTerrainLodComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCollisionProfileName(TEXT("NoCollision"));
	SetGenerateOverlapEvents(false);
	CastShadow = false;
}

bool USSXTerrainLodComponent::Init(const FSSXTerrainLodData& Data, bool bShowOff, const TMap<int32, UMaterialInterface*>& MaterialForTexture,
	const FTransform& LocalToWorld)
{
	TMap<int32, TArray<int32>> ByTexture;
	for (int32 P = 0; P < Data.Patches.Num(); ++P)
	{
		if (Data.Patches[P].bShowOff == bShowOff)
		{
			ByTexture.FindOrAdd(Data.Patches[P].Texture).Add(P);
		}
	}
	ByTexture.KeySort(TLess<int32>());
	for (const TPair<int32, TArray<int32>>& It : ByTexture)
	{
		if (!MaterialForTexture.Contains(It.Key))
		{
			UE_LOG(LogSSX, Warning, TEXT("[terrain lod] texture %d has no material on the terrain mesh"), It.Key);
			return false;
		}
	}
	TSharedPtr<FSSXTerrainLodRenderData> R = MakeShared<FSSXTerrainLodRenderData>();
	const FTransform WorldToLocal = LocalToWorld.Inverse();
	int32 Slot = 0;
	for (const TPair<int32, TArray<int32>>& It : ByTexture)
	{
		FSSXTerrainLodRenderData::FSection& Sec = R->Sections.AddDefaulted_GetRef();
		Sec.FirstPatch = R->NumPatches();
		Sec.NumPatches = It.Value.Num();
		for (const int32 P : It.Value)
		{
			const FSSXTerrainLodData::FPatch& Patch = Data.Patches[P];
			for (int32 C = 0; C < 4; ++C) { R->Corners.Add(Patch.Corners[C]); }
			const FSSXTerrainLodData::FVertex* B = Data.Block(P);
			for (int32 V = 0; V < SSXTerrainLod::VertsPerPatch; ++V)
			{
				const FVector World = SSXSim::GameToUnreal(FVector(B[V].Position));
				R->Positions.Add(FVector3f(WorldToLocal.TransformPosition(World)));
				R->Bounds += World;
				const FVector N = SSXSim::GameToUnreal(FVector(B[V].Normal[0], B[V].Normal[1], B[V].Normal[2]) / 127.0);
				R->Normals.Add(FVector3f(WorldToLocal.TransformVectorNoScale(N).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)));
				R->Uv0.Add(B[V].Uv0);
				R->Uv1.Add(B[V].Uv1);
				R->Uv2.Add(Patch.LightmapOrigin);
			}
		}
		SetMaterial(Slot++, MaterialForTexture[It.Key]);
	}
	R->Bounds = R->Bounds.TransformBy(WorldToLocal);
	RenderData = R;
	MarkRenderStateDirty();
	UpdateBounds();
	return R->NumPatches() > 0;
}

FPrimitiveSceneProxy* USSXTerrainLodComponent::CreateSceneProxy()
{
	return RenderData.IsValid() && RenderData->NumPatches() > 0 ? new FSSXTerrainLodSceneProxy(this) : nullptr;
}

FBoxSphereBounds USSXTerrainLodComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!RenderData.IsValid() || !RenderData->Bounds.IsValid)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
	}
	return FBoxSphereBounds(RenderData->Bounds.TransformBy(LocalToWorld));
}

int32 USSXTerrainLodComponent::GetNumMaterials() const
{
	return RenderData.IsValid() ? RenderData->Sections.Num() : 0;
}

USSXTerrainLodComponent* USSXTerrainLodComponent::CreateFor(UStaticMeshComponent& C, const FSSXTerrainLodData& Data, UObject* Outer)
{
	const UStaticMesh* Mesh = C.GetStaticMesh();
	const FString Name = Mesh ? Mesh->GetName() : FString();
	const bool bShowOff = Name.EndsWith(TEXT("_showoff_terrain"), ESearchCase::CaseSensitive);
	TMap<int32, UMaterialInterface*> Mats;
	for (int32 S = 0; S < C.GetNumMaterials(); ++S)
	{
		const int32 Tex = SlotTexture(C, S);
		if (Tex != INDEX_NONE && C.GetMaterial(S))
		{
			Mats.Add(Tex, C.GetMaterial(S));
		}
	}
	USSXTerrainLodComponent* Lod = NewObject<USSXTerrainLodComponent>(Outer, *(Name + TEXT("_Lod")));
	if (!Lod->Init(Data, bShowOff, Mats, C.GetComponentTransform()))
	{
		Lod->MarkAsGarbage();
		return nullptr;
	}
	Lod->Replaced = &C;
	return Lod;
}

int32 USSXTerrainLodComponent::AttachToTerrain(UWorld& World, TArray<TWeakObjectPtr<USSXTerrainLodComponent>>& OutAttached)
{
	TMap<FString, FSSXTerrainLodData> Cache;
	int32 N = 0;
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
		for (UStaticMeshComponent* C : Comps)
		{
			const UStaticMesh* Mesh = C->GetStaticMesh();
			const FString Name = Mesh ? Mesh->GetName() : FString();
			const bool bShowOff = Name.EndsWith(TEXT("_showoff_terrain"), ESearchCase::CaseSensitive);
			if (!bShowOff && !Name.EndsWith(TEXT("_terrain"), ESearchCase::CaseSensitive))
			{
				continue;
			}
			const FString Stem = Name.LeftChop(bShowOff ? 16 : 8);
			FSSXTerrainLodData* Data = Cache.Find(Stem);
			if (!Data)
			{
				Data = &Cache.Add(Stem);
				FString Err;
				if (!Data->Load(FSSXTerrainLodData::DefaultPath(Stem), &Err))
				{
					UE_LOG(LogSSX, Warning, TEXT("[terrain lod] %s: terrain stays at k/7"), *Err);
				}
			}
			if (Data->Patches.Num() == 0)
			{
				continue;
			}
			USSXTerrainLodComponent* Lod = CreateFor(*C, *Data, *It);
			if (!Lod)
			{
				UE_LOG(LogSSX, Warning, TEXT("[terrain lod] %s: no LOD draw (materials / data mismatch); terrain stays at k/7"), *Name);
				continue;
			}
			Lod->SetMobility(C->Mobility);
			Lod->SetupAttachment(C);
			Lod->RegisterComponent();
			It->AddInstanceComponent(Lod);
			OutAttached.Add(Lod);
			++N;
			UE_LOG(LogSSX, Display, TEXT("[terrain lod] %s: %d patches in %d sections drawn at the original's per-edge levels"),
				*Name, Lod->GetRenderData()->NumPatches(), Lod->GetRenderData()->Sections.Num());
		}
	}
	ApplyCVar(OutAttached);
	return N;
}

void USSXTerrainLodComponent::ApplyCVar(TConstArrayView<TWeakObjectPtr<USSXTerrainLodComponent>> Attached)
{
	// The Remastered look draws the full-detail (Nanite) terrain: the mesh that casts its sun shadow, so a coarser LOD
	// surface never sits inside that shadow.
	const bool bOn = CVarTerrainLod.GetValueOnGameThread() != 0 && SSXRenderLook::Get() != ESSXRenderLook::Remastered;
	for (const TWeakObjectPtr<USSXTerrainLodComponent>& W : Attached)
	{
		USSXTerrainLodComponent* Lod = W.Get();
		if (!Lod || (Lod->GetVisibleFlag() == bOn && Lod->Replaced && Lod->Replaced->GetVisibleFlag() == !bOn))
		{
			continue;
		}
		Lod->SetVisibility(bOn, false);
		if (Lod->Replaced)
		{
			Lod->Replaced->SetVisibility(!bOn, false);
		}
	}
}
