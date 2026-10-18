#include "Audio/SSXAudioDiag.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundWave.h"
#include "SSXTrickyRebuild.h"

namespace SSXAudioDiag
{
	namespace
	{
		int32 GSSXAudioLog = 0;
		int32 GCounts[int32(EResult::Num)] = {};
		TMap<FString, TArray<int32>> GBySystem;
		TArray<FRecord> GRecords;
		int32 GRecordHead = 0;
		constexpr int32 MaxRecords = 8192;
		int32 GPeakActive = 0;
		int32 GMaxSources = 0;

#if !UE_BUILD_SHIPPING
		FAutoConsoleVariableRef CVarLog(TEXT("ssx.Audio.Log"), GSSXAudioLog,
			TEXT("Log every SSX sound request (system, id, 2D/3D, volume, radius, result). 0 off, 1 all, 2 failures only."));
		FAutoConsoleCommand CmdStats(TEXT("ssx.Audio.Stats"), TEXT("Print the SSX sound request counts by system and result."),
			FConsoleCommandDelegate::CreateLambda([]()
			{
				for (const FString& L : Summary()) { UE_LOG(LogSSX, Display, TEXT("[audiolog] %s"), *L); }
				UE_LOG(LogSSX, Display, TEXT("[audiolog] peak active sources %d / cap %d"), GPeakActive, GMaxSources);
			}));
#endif
	}

	const TCHAR* ResultName(EResult R)
	{
		switch (R)
		{
		case EResult::Played: return TEXT("played");
		case EResult::VoiceCap: return TEXT("voice-cap");
		case EResult::NotInManifest: return TEXT("not-in-manifest");
		case EResult::AssetMissing: return TEXT("asset-missing");
		case EResult::NoWorld: return TEXT("no-world");
		case EResult::Volume0: return TEXT("volume-0");
		case EResult::Gated: return TEXT("gated");
		case EResult::OutOfRange: return TEXT("out-of-range");
		case EResult::SpawnFailed: return TEXT("spawn-failed");
		default: return TEXT("?");
		}
	}

	int32 LogLevel() { return GSSXAudioLog; }

	void Note(const TCHAR* System, const FString& Id, bool b3D, float Volume, double RadiusM, EResult Result, const FString& Asset, bool bUI)
	{
#if !UE_BUILD_SHIPPING
		++GCounts[int32(Result)];
		TArray<int32>& S = GBySystem.FindOrAdd(System);
		if (S.Num() == 0) { S.SetNumZeroed(int32(EResult::Num)); }
		++S[int32(Result)];
		FRecord R{ System, Id, b3D, bUI, Volume, RadiusM, Result };
		if (GRecords.Num() < MaxRecords) { GRecords.Add(MoveTemp(R)); }
		else { GRecords[GRecordHead] = MoveTemp(R); GRecordHead = (GRecordHead + 1) % MaxRecords; }
		const bool bFail = Result != EResult::Played;
		if (GSSXAudioLog == 1 || (GSSXAudioLog == 2 && bFail))
		{
			UE_LOG(LogSSX, Display, TEXT("[audiolog] %s %s %s%s vol=%.3f r=%.0fm -> %s (%s)"), System, *Id, b3D ? TEXT("3D") : TEXT("2D"),
				bUI ? TEXT(" ui") : TEXT(""), Volume, RadiusM, ResultName(Result), Asset.IsEmpty() ? TEXT("-") : *Asset);
		}
#endif
	}

	void Song(const FString& What, float Gain)
	{
		const int32 Level = GSSXAudioLog;
		GSSXAudioLog = 0;
		Note(TEXT("song"), What, false, Gain, 0.0, EResult::Played);
		GSSXAudioLog = Level;
		if (Level == 1)
		{
			UE_LOG(LogSSX, Display, TEXT("[audiolog] song %s gain=%.3f"), *What, Gain);
		}
	}

	EResult SpawnResult(const UAudioComponent* C, bool b3D)
	{
		if (!C)
		{
			return b3D ? EResult::OutOfRange : EResult::SpawnFailed;
		}
		if (FAudioDevice* D = C->GetAudioDevice())
		{
			const int32 Active = D->GetNumActiveSources();
			GMaxSources = D->GetMaxChannels();
			GPeakActive = FMath::Max(GPeakActive, Active);
			if (GMaxSources > 0 && Active >= GMaxSources)
			{
				return EResult::VoiceCap;
			}
		}
		return EResult::Played;
	}

	int32 Count(EResult R) { return GCounts[int32(R)]; }

	int32 Count(const FString& System, EResult R)
	{
		const TArray<int32>* S = GBySystem.Find(System);
		return S ? (*S)[int32(R)] : 0;
	}

	const TArray<FRecord>& Records()
	{
		if (GRecordHead != 0)
		{
			TArray<FRecord> Sorted;
			Sorted.Reserve(GRecords.Num());
			for (int32 I = 0; I < GRecords.Num(); ++I) { Sorted.Add(GRecords[(GRecordHead + I) % GRecords.Num()]); }
			GRecords = MoveTemp(Sorted);
			GRecordHead = 0;
		}
		return GRecords;
	}

	void Reset()
	{
		FMemory::Memzero(GCounts);
		GBySystem.Reset();
		GRecords.Reset();
		GRecordHead = 0;
		GPeakActive = 0;
	}

	TArray<FString> Summary()
	{
		TArray<FString> Out;
		for (const TPair<FString, TArray<int32>>& P : GBySystem)
		{
			for (int32 R = 0; R < int32(EResult::Num); ++R)
			{
				if (P.Value[R] > 0) { Out.Add(FString::Printf(TEXT("%s %s %d"), *P.Key, ResultName(EResult(R)), P.Value[R])); }
			}
		}
		Out.Sort();
		return Out;
	}

	int32 PeakActiveSources() { return GPeakActive; }
	int32 MaxSources() { return GMaxSources; }

	namespace
	{
		struct FLoopEntry
		{
			TWeakObjectPtr<UAudioComponent> Comp;
			FString System;
			FString Id;
			bool b3D = false;
			double Start = 0.0;
		};
		TArray<FLoopEntry> GLoops;
		FTSTicker::FDelegateHandle GLoopTicker;
		constexpr float LoopSummaryPeriod = 2.f;

		bool IsLooping(const UAudioComponent* C)
		{
			const USoundWave* W = C ? Cast<USoundWave>(C->Sound) : nullptr;
			return W && W->bLooping;
		}

		void PruneLoops()
		{
			GLoops.RemoveAll([](const FLoopEntry& E) { const UAudioComponent* C = E.Comp.Get(); return !C || !C->IsPlaying(); });
		}
	}

	void NoteLoop(const TCHAR* System, const FString& Id, UAudioComponent* C, bool b3D)
	{
#if !UE_BUILD_SHIPPING
		if (!IsLooping(C))
		{
			return;
		}
		GLoops.RemoveAll([C](const FLoopEntry& E) { return E.Comp.Get() == C || !E.Comp.IsValid(); });
		GLoops.Add({ C, System, Id, b3D, FPlatformTime::Seconds() });
		if (!GLoopTicker.IsValid())
		{
			GLoopTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				if (GSSXAudioLog == 1)
				{
					const FString L = LoopsLine();
					if (!L.IsEmpty()) { UE_LOG(LogSSX, Display, TEXT("[audiolog] %s"), *L); }
				}
				return true;
			}), LoopSummaryPeriod);
		}
#endif
	}

	void NoteStop(const UAudioComponent* C, const TCHAR* Why)
	{
		const int32 I = C ? GLoops.IndexOfByPredicate([C](const FLoopEntry& E) { return E.Comp.Get() == C; }) : INDEX_NONE;
		if (I == INDEX_NONE)
		{
			return;
		}
		if (GSSXAudioLog == 1)
		{
			UE_LOG(LogSSX, Display, TEXT("[audiolog] stop %s %s (%s) after %.1fs"), *GLoops[I].System, *GLoops[I].Id, Why,
				FPlatformTime::Seconds() - GLoops[I].Start);
		}
		GLoops.RemoveAt(I);
	}

	TArray<FLiveLoop> LiveLoops()
	{
		PruneLoops();
		const double Now = FPlatformTime::Seconds();
		TArray<FLiveLoop> Out;
		for (const FLoopEntry& E : GLoops)
		{
			Out.Add({ E.System, E.Id, E.b3D, E.Comp->VolumeMultiplier, Now - E.Start });
		}
		Out.Sort([](const FLiveLoop& A, const FLiveLoop& B) { return A.AgeS > B.AgeS; });
		return Out;
	}

	FString LoopsLine()
	{
		const TArray<FLiveLoop> L = LiveLoops();
		if (L.Num() == 0)
		{
			return FString();
		}
		TArray<FString> Parts;
		for (const FLiveLoop& E : L)
		{
			Parts.Add(FString::Printf(TEXT("%s %s %s vol=%.3f age=%.0fs"), *E.System, *E.Id, E.b3D ? TEXT("3D") : TEXT("2D"), E.Volume, E.AgeS));
		}
		return FString::Printf(TEXT("loops: %d | %s"), L.Num(), *FString::Join(Parts, TEXT("; ")));
	}
}
