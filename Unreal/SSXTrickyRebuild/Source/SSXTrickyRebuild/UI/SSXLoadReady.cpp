#include "UI/SSXLoadReady.h"

#include "Components/PrimitiveComponent.h"
#include "ContentStreaming.h"
#include "HAL/IConsoleManager.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "PipelineStateCache.h"
#include "ShaderCompiler.h"
#include "ShaderPipelineCache.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UnrealEngine.h"

void FSSXLoadPending::KeepMax(const FSSXLoadPending& O)
{
	Packages = FMath::Max(Packages, O.Packages);
	Sublevels = FMath::Max(Sublevels, O.Sublevels);
	Streaming = FMath::Max(Streaming, O.Streaming);
	Textures = FMath::Max(Textures, O.Textures);
	Shaders = FMath::Max(Shaders, O.Shaders);
	Psos = FMath::Max(Psos, O.Psos);
	PoolLimited = FMath::Max(PoolLimited, O.PoolLimited);
}

FString FSSXLoadPending::Describe() const
{
	return FString::Printf(TEXT("packages %d, sublevels %d, streaming %d, textures %d, shaders %d, PSOs %d (pool-limited %d)"),
		Packages, Sublevels, Streaming, Textures, Shaders, Psos, PoolLimited);
}

namespace
{
	SSXLoadReady::FProbe& ProbeOverride()
	{
		static SSXLoadReady::FProbe P;
		return P;
	}

	bool TextureStreamingOn()
	{
		return FApp::CanEverRender() && IStreamingManager::Get().IsTextureStreamingEnabled();
	}
}

void SSXLoadReady::SetProbeOverride(FProbe Probe)
{
	ProbeOverride() = MoveTemp(Probe);
}

SSXLoadReady::EMipWait SSXLoadReady::ClassifyMips(int32 ResidentMips, int32 FullMips, bool bInFlight, bool bPoolLimited)
{
	if (ResidentMips >= FullMips)
	{
		return EMipWait::Done;
	}
	return (!bInFlight && bPoolLimited) ? EMipWait::PoolLimited : EMipWait::Pending;
}

SSXLoadReady::FTextureList SSXLoadReady::CollectWorldTextures(const UWorld* World)
{
	FTextureList Out;
	if (!World || !TextureStreamingOn())
	{
		return Out;
	}
	const EMaterialQualityLevel::Type Quality = GetCachedScalabilityCVars().MaterialQualityLevel;
	TSet<UTexture2D*> Seen;
	TArray<UTexture*> Used;
	for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
	{
		UPrimitiveComponent* C = *It;
		if (!C || C->IsTemplate() || !C->IsRegistered() || C->GetWorld() != World)
		{
			continue;
		}
		Used.Reset();
		C->GetUsedTextures(Used, Quality);
		for (UTexture* U : Used)
		{
			UTexture2D* T = Cast<UTexture2D>(U);
			if (T && T->IsStreamable() && !Seen.Contains(T))
			{
				Seen.Add(T);
				Out.Add(T);
			}
		}
	}
	return Out;
}

int32 SSXLoadReady::ForceTexturesResident(const FTextureList& Textures, float Seconds)
{
	if (!TextureStreamingOn())
	{
		return 0;
	}
	int32 N = 0;
	for (const TWeakObjectPtr<UTexture2D>& W : Textures)
	{
		if (UTexture2D* T = W.Get())
		{
			T->SetForceMipLevelsToBeResident(Seconds);
			++N;
		}
	}
	return N;
}

FSSXLoadPending SSXLoadReady::Probe(const UWorld* World, const FTextureList& Watched)
{
	if (const FProbe& Override = ProbeOverride())
	{
		return Override(World);
	}
	FSSXLoadPending P;
	P.Packages = IsAsyncLoading() ? FMath::Max(1, GetNumAsyncPackages()) : 0;
	if (World)
	{
		for (const ULevelStreaming* L : World->GetStreamingLevels())
		{
			if (L && ((L->ShouldBeLoaded() && !L->IsLevelLoaded()) || (L->ShouldBeVisible() && !L->IsLevelVisible())))
			{
				++P.Sublevels;
			}
		}
		if (P.Sublevels == 0 && World->IsVisibilityRequestPending())
		{
			P.Sublevels = 1;
		}
	}
	if (!FApp::CanEverRender())
	{
		return P;
	}
	if (TextureStreamingOn() && Watched.Num() > 0)
	{
		IRenderAssetStreamingManager& Mgr = IStreamingManager::Get().GetRenderAssetStreamingManager();
		static IConsoleVariable* MipBiasVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.MipBias"));
		const bool bPoolLimited = Mgr.GetMemoryOverBudget() > 0 || (MipBiasVar && MipBiasVar->GetFloat() > 0.f);
		int32 Listed = 0;
		for (const TWeakObjectPtr<UTexture2D>& W : Watched)
		{
			UTexture2D* T = W.Get();
			if (!T || !T->IsStreamable())
			{
				continue;
			}
			if (!T->RenderResourceSupportsStreaming())
			{
				continue;
			}
			const FStreamableRenderResourceState S = T->GetStreamableResourceState();
			const int32 Resident = S.NumResidentLODs;
			const int32 Requested = S.NumRequestedLODs;
			const int32 Full = Mgr.IsFullyStreamedIn(T) ? Resident
				: FMath::Clamp<int32>(S.MaxNumLODs - T->GetCachedLODBias(), int32(S.NumNonStreamingLODs), int32(S.MaxNumLODs));
			const bool bInFlight = Requested != Resident || T->HasPendingInitOrStreaming();
			switch (ClassifyMips(Resident, Full, bInFlight, bPoolLimited))
			{
			case EMipWait::Done:
				break;
			case EMipWait::PoolLimited:
				++P.PoolLimited;
				break;
			case EMipWait::Pending:
				++P.Streaming;
				if (Listed++ < 6)
				{
					P.StreamingDetail += FString::Printf(TEXT("%s%s %d/%d/%d"), P.StreamingDetail.IsEmpty() ? TEXT("") : TEXT(", "),
						*T->GetName(), Resident, Requested, Full);
				}
				break;
			}
		}
		if (Listed > 6)
		{
			P.StreamingDetail += FString::Printf(TEXT(" (+%d more)"), Listed - 6);
		}
	}
	for (TObjectIterator<UTexture> It; It; ++It)
	{
		const UTexture* T = *It;
		if (T && !T->IsTemplate() && (T->HasPendingRenderResourceInitialization() || T->HasPendingInitOrStreaming()))
		{
			++P.Textures;
		}
	}
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
	{
		P.Shaders = FMath::Max(1, GShaderCompilingManager->GetNumOutstandingJobs() + GShaderCompilingManager->GetNumPendingJobs());
	}
	P.Psos = int32(PipelineStateCache::NumActivePrecacheRequests() + FShaderPipelineCache::NumPrecompilesRemaining());
	return P;
}

bool FSSXLoadGate::Update(const FSSXLoadPending& Pending, double Now)
{
	if (bReleased)
	{
		return true;
	}
	if (StartTime < 0.0)
	{
		StartTime = Now;
	}
	Last = Pending;
	Waited.KeepMax(Pending);
	if (!Pending.Any() || Now - StartTime >= Timeout)
	{
		bTimedOut = Pending.Any();
		bReleased = true;
		ReleaseTime = Now;
	}
	return bReleased;
}
