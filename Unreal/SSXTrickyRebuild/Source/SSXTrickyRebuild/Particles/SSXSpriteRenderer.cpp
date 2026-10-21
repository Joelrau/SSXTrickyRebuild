#include "Particles/SSXSpriteRenderer.h"

#include "DynamicMeshBuilder.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneView.h"

namespace
{
	struct FProxyBatch
	{
		const FMaterialRenderProxy* Material = nullptr;
		TArray<FSSXSpriteVertexData> Sprites;
		TArray<TArray<FSSXStripVertex>> Strips;
		bool bOcclusion = false;
	};

	FDynamicMeshVertex WithOcc(FDynamicMeshVertex V, const FVector4f& Occ, const FVector2f& Block)
	{
		V.TextureCoordinate[1] = FVector2f(Occ.X, Occ.Y);
		V.TextureCoordinate[2] = FVector2f(Occ.Z, Occ.W);
		V.TextureCoordinate[3] = Block;
		return V;
	}

	FVector3f N_Face(const TArray<FSSXStripVertex>& St, int32 I)
	{
		const int32 A = I & ~1;
		const int32 B = FMath::Min(A + 2, St.Num() - 2);
		const FVector3f E0 = St[A + 1].Pos - St[A].Pos;
		const FVector3f E1 = (B != A) ? St[B].Pos - St[A].Pos : FVector3f(0, 0, 1);
		const FVector3f Cr = FVector3f::CrossProduct(E0, E1);
		return Cr.SizeSquared() > 1e-8f ? Cr.GetSafeNormal() : FVector3f(0, 0, 1);
	}

	class FSSXSpriteSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		explicit FSSXSpriteSceneProxy(USSXSpriteRenderComponent* C)
			: FPrimitiveSceneProxy(C)
			, MaterialRelevance(C->GetMaterialRelevance(GetScene().GetShaderPlatform()))
		{
			bVerifyUsedMaterials = false;
		}

		virtual SIZE_T GetTypeHash() const override { static size_t Unique; return reinterpret_cast<size_t>(&Unique); }

		void SetBatches_RenderThread(TArray<FProxyBatch>&& In) { Batches = MoveTemp(In); }

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
			uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1u << ViewIndex)))
				{
					continue;
				}
				const FSceneView* View = Views[ViewIndex];
				const FVector3f R = FVector3f(View->GetViewRight());
				const FVector3f U = FVector3f(View->GetViewUp());
				const FVector3f N = FVector3f(-View->GetViewDirection());
				// One builder per vertex layout (1 or 4 texcoords): its buffers and vertex factory are made once per view and
				// every batch draws a range of them, in batch order (the translucent sprites keep their order).
				FDynamicMeshBuilder Builders[2] = { FDynamicMeshBuilder(View->GetFeatureLevel(), 1), FDynamicMeshBuilder(View->GetFeatureLevel(), 4) };
				int32 Verts[2] = { 0, 0 }, Tris[2] = { 0, 0 };
				TArray<TPair<int32, FDynamicMeshDrawOffset>, TInlineAllocator<32>> Draws;
				for (int32 BI = 0; BI < Batches.Num(); ++BI)
				{
					const FProxyBatch& B = Batches[BI];
					if (!B.Material || (B.Sprites.Num() == 0 && B.Strips.Num() == 0))
					{
						continue;
					}
					const int32 K = B.bOcclusion ? 1 : 0;
					FDynamicMeshBuilder& MB = Builders[K];
					FDynamicMeshDrawOffset Off;
					Off.FirstIndex = uint32(Tris[K] * 3);
					Off.MinVertexIndex = uint32(Verts[K]);
					for (const FSSXSpriteVertexData& S : B.Sprites)
					{
						const FVector3f Rh = R * S.Half, Uh = U * (S.HalfY >= 0.f ? S.HalfY : S.Half);
						const int32 V0 = MB.AddVertex(WithOcc(FDynamicMeshVertex(S.Pos - Rh + Uh, FVector3f(R), N, FVector2f(S.UV.X, S.UV.Y), S.Color), S.Occ, S.Block));
						MB.AddVertex(WithOcc(FDynamicMeshVertex(S.Pos + Rh + Uh, FVector3f(R), N, FVector2f(S.UV.Z, S.UV.Y), S.Color), S.Occ, S.Block));
						MB.AddVertex(WithOcc(FDynamicMeshVertex(S.Pos + Rh - Uh, FVector3f(R), N, FVector2f(S.UV.Z, S.UV.W), S.Color), S.Occ, S.Block));
						MB.AddVertex(WithOcc(FDynamicMeshVertex(S.Pos - Rh - Uh, FVector3f(R), N, FVector2f(S.UV.X, S.UV.W), S.Color), S.Occ, S.Block));
						MB.AddTriangle(V0, V0 + 1, V0 + 2);
						MB.AddTriangle(V0, V0 + 2, V0 + 3);
						Verts[K] += 4;
						Tris[K] += 2;
					}
					for (const TArray<FSSXStripVertex>& St : B.Strips)
					{
						const int32 NV = St.Num() & ~1;
						if (NV < 4)
						{
							continue;
						}
						const int32 First = MB.AddVertex(WithOcc(FDynamicMeshVertex(St[0].Pos, FVector3f(R), N_Face(St, 0), St[0].UV, St[0].Color), St[0].Occ, St[0].Block));
						for (int32 I = 1; I < NV; ++I)
						{
							MB.AddVertex(WithOcc(FDynamicMeshVertex(St[I].Pos, FVector3f(R), N_Face(St, I), St[I].UV, St[I].Color), St[I].Occ, St[I].Block));
						}
						for (int32 I = 0; I + 3 < NV; I += 2)
						{
							MB.AddTriangle(First + I, First + I + 1, First + I + 2);
							MB.AddTriangle(First + I + 1, First + I + 3, First + I + 2);
							Tris[K] += 2;
						}
						Verts[K] += NV;
					}
					Off.NumPrimitives = uint32(Tris[K]) - Off.FirstIndex / 3;
					Off.MaxVertexIndex = uint32(FMath::Max(Verts[K] - 1, 0));
					if (Off.NumPrimitives > 0) { Draws.Add({ BI * 2 + K, Off }); }
				}
				FDynamicMeshBuilderSettings Settings;
				Settings.bDisableBackfaceCulling = true;
				Settings.bReceivesDecals = false;
				for (const TPair<int32, FDynamicMeshDrawOffset>& D : Draws)
				{
					Builders[D.Key & 1].GetMesh(FMatrix::Identity, Batches[D.Key / 2].Material, SDPG_World, Settings, &D.Value, ViewIndex, Collector);
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bRenderInMainPass = true;
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			return Result;
		}

		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

	private:
		FMaterialRelevance MaterialRelevance;
		TArray<FProxyBatch> Batches;
	};
}

USSXSpriteRenderComponent::USSXSpriteRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bReceivesDecals = false;
	bUseAsOccluder = false;
	SetMobility(EComponentMobility::Movable);
}

void USSXSpriteRenderComponent::SetBaseMaterials(const TArray<UMaterialInterface*>& InBases)
{
	Bases.Reset();
	for (UMaterialInterface* M : InBases) { Bases.Add(M); }
	MarkRenderStateDirty();
}

void USSXSpriteRenderComponent::SetBatches(TArray<FSSXSpriteBatch>&& InBatches)
{
	Batches = MoveTemp(InBatches);
	for (const FSSXSpriteBatch& B : Batches)
	{
		if (B.Material) { Used.AddUnique(B.Material); }
	}
	MarkRenderDynamicDataDirty();
}

FPrimitiveSceneProxy* USSXSpriteRenderComponent::CreateSceneProxy()
{
	return new FSSXSpriteSceneProxy(this);
}

FBoxSphereBounds USSXSpriteRenderComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FVector::ZeroVector, FVector(2.0e6), 2.0e6 * 1.7320508);
}

void USSXSpriteRenderComponent::GetUsedMaterials(TArray<UMaterialInterface*>& Out, bool) const
{
	for (UMaterialInterface* M : Bases) { if (M) { Out.Add(M); } }
	for (UMaterialInterface* M : Used) { if (M) { Out.Add(M); } }
}

void USSXSpriteRenderComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();
	if (!SceneProxy)
	{
		return;
	}
	TArray<FProxyBatch> Data;
	Data.Reserve(Batches.Num());
	for (const FSSXSpriteBatch& B : Batches)
	{
		if (!B.Material || (B.Sprites.Num() == 0 && B.Strips.Num() == 0))
		{
			continue;
		}
		FProxyBatch& P = Data.AddDefaulted_GetRef();
		P.Material = B.Material->GetRenderProxy();
		P.Sprites = B.Sprites;
		P.Strips = B.Strips;
		P.bOcclusion = B.bOcclusion;
	}
	FSSXSpriteSceneProxy* Proxy = static_cast<FSSXSpriteSceneProxy*>(SceneProxy);
	ENQUEUE_RENDER_COMMAND(SSXSpriteBatches)([Proxy, Data = MoveTemp(Data)](FRHICommandListImmediate&) mutable
	{
		Proxy->SetBatches_RenderThread(MoveTemp(Data));
	});
}
