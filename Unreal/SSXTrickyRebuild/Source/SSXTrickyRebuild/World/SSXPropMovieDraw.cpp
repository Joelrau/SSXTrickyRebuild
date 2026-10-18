// 0x148D28
#include "World/SSXPropAnimation.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshComponentLODInfo.h"
#include "StaticMeshResources.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SSXTrickyRebuild.h"
#include "TextureResource.h"

namespace
{
	const FName MovieBaseTexParam(TEXT("BaseTex"));
}

void USSXPropAnimation::UploadMovieFrame()
{
	using namespace SSXPropMovie;
	const int32 F = MovieStream.Shown;
	if (!MovieTexture || F < 0 || F >= MovieFile.NumFrames() || MovieFile.FrameWidth(F) != Width || MovieFile.FrameHeight(F) != Height)
	{
		return;
	}
	MovieUploaded = F;
	FTextureResource* Res = MovieTexture->GetResource();
	if (!Res || !Res->GetTexture2DResource())
	{
		return;
	}
	TArray<FColor> Px;
	MovieFile.Decode(F, Px);
	uint8* Buf = new uint8[Width * Height * 4];
	FMemory::Memcpy(Buf, Px.GetData(), Width * Height * 4);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
	MovieTexture->UpdateTextureRegions(0, 1, Region, Width * 4, 4, Buf,
		[](uint8* Data, const FUpdateTextureRegion2D* R) { delete[] Data; delete R; });
}

void USSXPropAnimation::ApplyMovies()
{
	using namespace SSXPropMovie;
	if (MovieInsts.Num() && !MovieTexture)
	{
		MovieTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, TEXT("SSXMovieTexture"));
		if (MovieTexture)
		{
			// 0x1FF890
			void* Mip = MovieTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memzero(Mip, Width * Height * 4);
			MovieTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
			MovieTexture->SRGB = true;
			MovieTexture->Filter = TF_Bilinear;
			MovieTexture->UpdateResource();
			for (const TPair<int32, TArray<FFlipSlot>>& P : MovieSlots)
			{
				for (const FFlipSlot& S : P.Value)
				{
					if (UMaterialInstanceDynamic* M = S.Mid.Get()) { M->SetTextureParameterValue(MovieBaseTexParam, MovieTexture); }
				}
			}
		}
	}
	if (bMovieDirty)
	{
		bMovieDirty = false;
		UploadMovieFrame();
	}
	for (auto It = MovieSlots.CreateIterator(); It; ++It)
	{
		if (MovieInsts.Contains(It.Key()))
		{
			continue;
		}
		for (const FFlipSlot& S : It.Value())
		{
			if (UMaterialInstanceDynamic* M = S.Mid.Get()) { M->SetTextureParameterValue(MovieBaseTexParam, S.Original.Get()); }
		}
		It.RemoveCurrent();
	}
	if (!MovieTexture)
	{
		return;
	}
	for (int32 Inst : MovieInsts)
	{
		if (MovieSlots.Contains(Inst))
		{
			continue;
		}
		TArray<FFlipSlot>& Slots = MovieSlots.Add(Inst);
		if (const TArray<FActorRef>* List = Actors.Find(Inst))
		{
			for (const FActorRef& R : *List)
			{
				if (!R.Actor.IsValid())
				{
					continue;
				}
				TArray<UStaticMeshComponent*> Comps;
				R.Actor->GetComponents(Comps);
				for (UStaticMeshComponent* Comp : Comps)
				{
					for (int32 Slot = 0; Slot < Comp->GetNumMaterials(); ++Slot)
					{
						UMaterialInterface* Mat = Comp->GetMaterial(Slot);
						UTexture* Own = nullptr;
						if (!Mat || !Mat->GetTextureParameterValue(FHashedMaterialParameterInfo(MovieBaseTexParam), Own))
						{
							continue;
						}
						// 0x148D28 / 0x1E1F70 / 0x200390
						if (UMaterialInstanceDynamic* Mid = Comp->CreateAndSetMaterialInstanceDynamic(Slot))
						{
							Mid->SetTextureParameterValue(MovieBaseTexParam, MovieTexture);
							Slots.Add({ Mid, INDEX_NONE, Own });
						}
					}
				}
			}
		}
		UE_LOG(LogSSX, Verbose, TEXT("[props] movie bound: instance %d, %d slots"), Inst, Slots.Num());
	}
}

void USSXPropAnimation::SetFlatVertexColour(UStaticMeshComponent* C, bool bFlat)
{
	const UStaticMesh* Mesh = C ? C->GetStaticMesh() : nullptr;
	const FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
	if (!RD || RD->LODResources.Num() == 0)
	{
		return;
	}
	if (!bFlat)
	{
		if (HasFlatVertexColour(C))
		{
#if WITH_EDITOR
			C->RemoveInstanceVertexColors();
#else
			for (FStaticMeshComponentLODInfo& L : C->LODData) { L.ReleaseOverrideVertexColorsAndBlock(); }   // game build: no editor helper
			C->MarkRenderStateDirty();
#endif
		}
		return;
	}
	if (HasFlatVertexColour(C))
	{
		return;
	}
	C->SetLODDataCount(1, C->LODData.Num());
	FStaticMeshComponentLODInfo& L = C->LODData[0];
	L.OverrideVertexColors = new FColorVertexBuffer;
	L.OverrideVertexColors->InitFromSingleColor(FColor(128, 128, 128, 128), RD->LODResources[0].GetNumVertices());
	BeginInitResource(L.OverrideVertexColors);
	C->MarkRenderStateDirty();
}

bool USSXPropAnimation::HasFlatVertexColour(const UStaticMeshComponent* C)
{
	return C && C->LODData.Num() > 0 && C->LODData[0].OverrideVertexColors != nullptr;
}

void USSXPropAnimation::SetTextureOverride(int32 Inst, UTexture* Tex)
{
	if (Tex) { TexOverrides.Add(Inst, Tex); }
	else { TexOverrides.Remove(Inst); }
}

void USSXPropAnimation::ApplyTextureOverrides()
{
	for (auto It = OverrideSlots.CreateIterator(); It; ++It)
	{
		if (TexOverrides.Contains(It.Key()))
		{
			continue;
		}
		for (const FFlipSlot& S : It.Value())
		{
			if (UMaterialInstanceDynamic* M = S.Mid.Get()) { M->SetTextureParameterValue(MovieBaseTexParam, S.Original.Get()); }
		}
		FlipShown.Remove(It.Key());
		It.RemoveCurrent();
	}
	for (const TPair<int32, TWeakObjectPtr<UTexture>>& P : TexOverrides)
	{
		UTexture* Tex = P.Value.Get();
		if (!Tex)
		{
			continue;
		}
		TArray<FFlipSlot>* Slots = OverrideSlots.Find(P.Key);
		if (!Slots)
		{
			TArray<FFlipSlot> New;
				TArray<TWeakObjectPtr<UStaticMeshComponent>> Flat;
			if (const TArray<FActorRef>* List = Actors.Find(P.Key))
			{
				for (const FActorRef& R : *List)
				{
					if (!R.Actor.IsValid())
					{
						continue;
					}
					TArray<UStaticMeshComponent*> Comps;
					R.Actor->GetComponents(Comps);
					for (UStaticMeshComponent* Comp : Comps)
					{
						for (int32 Slot = 0; Slot < Comp->GetNumMaterials(); ++Slot)
						{
							UMaterialInterface* Mat = Comp->GetMaterial(Slot);
							UTexture* Own = nullptr;
							if (!Mat || !Mat->GetTextureParameterValue(FHashedMaterialParameterInfo(MovieBaseTexParam), Own))
							{
								continue;
							}
							if (UMaterialInstanceDynamic* Mid = Comp->CreateAndSetMaterialInstanceDynamic(Slot))
							{
								New.Add({ Mid, INDEX_NONE, Own });
								Flat.AddUnique(Comp);
							}
						}
					}
				}
			}
			if (New.Num() == 0)
			{
				continue;
			}
			// 0x269D10
			for (const TWeakObjectPtr<UStaticMeshComponent>& C : Flat) { SetFlatVertexColour(C.Get(), true); }
			OverrideComps.Add(P.Key, MoveTemp(Flat));
			Slots = &OverrideSlots.Add(P.Key, MoveTemp(New));
		}
		for (const FFlipSlot& S : *Slots)
		{
			if (UMaterialInstanceDynamic* M = S.Mid.Get()) { M->SetTextureParameterValue(MovieBaseTexParam, Tex); }
		}
	}
}
