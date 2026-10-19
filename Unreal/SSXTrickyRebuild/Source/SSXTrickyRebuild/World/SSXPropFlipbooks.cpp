// 0x142AF0 / 0x142D40
#include "World/SSXPropAnimation.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "SSXTrickyRebuild.h"
#include "World/SSXRemasterWorld.h"
#include "UI/SSXFrontEndData.h"

namespace
{
	int32 TextureIndexOf(const UTexture* T)
	{
		if (!T)
		{
			return INDEX_NONE;
		}
		const FString N = T->GetName();
		int32 K = 0;
		while (K < N.Len() && FChar::IsDigit(N[K])) { ++K; }
		return K > 0 && K < N.Len() && N[K] == TEXT('_') ? FCString::Atoi(*N.Left(K)) : INDEX_NONE;
	}

	const FName BaseTexParam(TEXT("BaseTex"));
}

UTexture2D* USSXPropAnimation::FlipTexture(int32 Index)
{
	if (Index < 0)
	{
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* T = FlipTextures.Find(Index))
	{
		return *T;
	}
	if (MissingFlipTextures.Contains(Index))
	{
		return nullptr;
	}
	const FString Dir = FPaths::Combine(FPaths::GetPath(SSXFrontEnd::TextureDir()), TEXT("MODELS"), TrackStem.ToUpper(),
		TEXT("data"), TEXT("models"), TrackStem);
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, FString::Printf(TEXT("%03d_*.png"), Index)), true, false);
	Found.Sort();
	UTexture2D* Tex = Found.Num() ? FImageUtils::ImportFileAsTexture2D(FPaths::Combine(Dir, Found[0])) : nullptr;
	if (!Tex)
	{
		MissingFlipTextures.Add(Index);
		UE_LOG(LogSSX, Warning, TEXT("[props] flipbook texture %d not found in %s (tools/sshtex.py)"), Index, *Dir);
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->UpdateResource();
	FlipTextures.Add(Index, Tex);
	return Tex;
}

void USSXPropAnimation::ApplyFlipbooks()
{
	TMap<int32, int32> Want;
	for (int32 I = 0; I < Controllers.Num(); ++I)
	{
		const SSXProps::FPropController& C = Controllers.Get(I);
		if (C.HasFlip() && C.Flip.FrameCount > 0 && C.Instance != INDEX_NONE)
		{
			Want.Add(C.Instance, C.Flip.Frame);
		}
	}
	for (const TPair<int32, int32>& P : CrackFrames)
	{
		Want.FindOrAdd(P.Key) = P.Value;
	}
	for (auto It = FlipShown.CreateIterator(); It; ++It)
	{
		if (Want.Contains(It.Key()))
		{
			continue;
		}
		if (const TArray<FFlipSlot>* Slots = FlipSlots.Find(It.Key()))
		{
			for (const FFlipSlot& S : *Slots)
			{
				if (UMaterialInstanceDynamic* M = S.Mid.Get()) { M->SetTextureParameterValue(BaseTexParam, S.Original.Get()); }
			}
		}
		It.RemoveCurrent();
	}
	for (const TPair<int32, int32>& P : Want)
	{
		const int32* Shown = FlipShown.Find(P.Key);
		if (Shown && *Shown == P.Value)
		{
			continue;
		}
		TArray<FFlipSlot>* Slots = FlipSlots.Find(P.Key);
		if (!Slots)
		{
			Slots = &FlipSlots.Add(P.Key);
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
							UMaterialInterface* Mat = SSXRemaster::ClassicMaterial(*Comp, Slot);
							UTexture* Own = nullptr;
							if (!Mat || !Mat->GetTextureParameterValue(FHashedMaterialParameterInfo(BaseTexParam), Own))
							{
								continue;
							}
							const int32 Base = TextureIndexOf(Own);
							if (AnimData.FlipTextureFor(P.Key, Base, 0) == INDEX_NONE)
							{
								continue;
							}
							if (UMaterialInstanceDynamic* Mid = SSXRemaster::ClassicMid(*Comp, Slot))
							{
								Slots->Add({ Mid, Base, Own });
							}
						}
					}
				}
			}
		}
		int32 Swapped = 0;
		for (const FFlipSlot& S : *Slots)
		{
			UMaterialInstanceDynamic* M = S.Mid.Get();
			UTexture2D* T = M ? FlipTexture(AnimData.FlipTextureFor(P.Key, S.BaseTexture, P.Value)) : nullptr;
			if (M && T) { M->SetTextureParameterValue(BaseTexParam, T); ++Swapped; }
		}
		if (!Shown)
		{
			UE_LOG(LogSSX, Verbose, TEXT("[props] flipbook bound: instance %d, frame %d, %d / %d slots"), P.Key, P.Value, Swapped, Slots->Num());
		}
		FlipShown.Add(P.Key, P.Value);
	}
}
