// 0x218B20 / 0x218700
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXReplayKeyframe.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags LoopAudioFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioLoopVoicesTest, "SSX.Audio.LoopVoices", LoopAudioFlags)
bool FSSXAudioLoopVoicesTest::RunTest(const FString&)
{
	namespace BA = SSXBoardAudio;
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("snow")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("snow race data / patches: %s: skipped"), *Err));
		return true;
	}
	constexpr int32 N = 6, Human = 3;
	const int32 Riders[N] = { 10, 0, 8, 4, 11, 6 };
	TArray<TUniquePtr<FRiderSim>> Sims;
	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::CircuitRace;
	TestTrue(TEXT("session"), Session.Init(Data, Cfg));
	TArray<USSXBoardSoundComponent*> Boards;
	for (int32 I = 0; I < N; ++I)
	{
		TUniquePtr<FRiderSim> S = MakeUnique<FRiderSim>();
		SSXRiderSetup::RegisterStates(*S, true);
		S->SetWorld(&World);
		S->Profile.Character = Riders[I];
		Session.AddRacer(S.Get(), I == Human);
		Sims.Add(MoveTemp(S));
		USSXBoardSoundComponent* B = NewObject<USSXBoardSoundComponent>();
		B->bIsAI = I != Human;
		Boards.Add(B);
	}
	Session.IsSkipPressed = []() { return true; };
	Session.Start();

	struct FWindow { int32 Start, Len; };
	TArray<TArray<FWindow>> Windows;
	Windows.SetNum(N);
	constexpr int32 S8 = 60 * 8, S19 = 60 * 19 + 30, S30 = 60 * 30;
	for (int32 I = 0; I < N; ++I)
	{
		Windows[I].Add({ S8 + I * 30, 180 });
		if (I < 3) { Windows[I].Add({ S19, 240 }); }
		Windows[I].Add({ S30 + I * 20, 150 });
	}
	constexpr int32 SaveAt = 60 * 5, RestoreAt = 60 * 20, EndAt = 60 * 40, PreRaceLimit = 60 * 120;
	TArray<int32> ExtraCrash, ExtraSlide, SavedCrash, SavedSlide;
	ExtraCrash.SetNumZeroed(N); ExtraSlide.SetNumZeroed(N); SavedCrash.SetNumZeroed(N); SavedSlide.SetNumZeroed(N);
	FSSXRaceKeyframe Key;

	int32 GoFrame = -1, Slides = 0, Slides2D = 0, HeldOutside = 0, TooLoud = 0, RestoreSounds = -1, HeldAfterRestore = 0;
	TArray<FString> Bad;
	auto Feed = [&](int32 T, bool bRestoreFrame)
	{
		for (int32 I = 0; I < N; ++I)
		{
			FRiderState V = Sims[I]->State;
			bool bIn = false;
			for (const FWindow& W : Windows[I])
			{
				if (T >= W.Start && T < W.Start + W.Len)
				{
					bIn = true;
					if (T == W.Start) { ++ExtraCrash[I]; }
					if (T == W.Start + 30 || T == W.Start + 60) { ++ExtraSlide[I]; }
				}
			}
			V.CrashSoundCount += ExtraCrash[I];
			V.SlideSoundCount += ExtraSlide[I];
			if (bIn)
			{
				V.Motion = ERiderMotion::WipeOut;
				V.LastImpact.Speed = 1000.0;
			}
			USSXBoardSoundComponent* B = Boards[I];
			if (bRestoreFrame && I % 2 == 0)
			{
				B->OnRiderStateRestored(V);
			}
			const int32 Before = B->OneShotLog.Num();
			B->UpdateFromRider(V, 1.f / 60.f);
			for (int32 K = Before; K < B->OneShotLog.Num(); ++K)
			{
				if (B->OneShotLog[K] == BA::ZbxsfxId(BA::SlideSound))
				{
					++Slides;
					Slides2D += B->OneShot3DLog[K] ? 0 : 1;
				}
			}
			if (bRestoreFrame)
			{
				RestoreSounds = FMath::Max(RestoreSounds, 0) + (B->OneShotLog.Num() - Before);
				HeldAfterRestore += (B->IsSlideHeld() && V.Motion != ERiderMotion::WipeOut) ? 1 : 0;
			}
			if (B->IsSlideHeld() && V.Motion != ERiderMotion::WipeOut)
			{
				++HeldOutside;
				if (Bad.Num() < 8) { Bad.Add(FString::Printf(TEXT("racer %d T %d motion %d"), I, T, int32(V.Motion))); }
			}
			const float Max = bIn ? FMath::Clamp(BA::Curve(7, 1000), 0, 127) / 127.f : 1.f;
			if (B->SlideGain > Max + 1e-4f || B->LastVolume > 1.f)
			{
				++TooLoud;
			}
		}
	};

	for (int32 F = 0; F < PreRaceLimit + EndAt; ++F)
	{
		Session.Tick();
		for (const TUniquePtr<FRiderSim>& S : Sims) { S->Step(FRiderInputWord()); }
		Session.ClearCues();
		if (GoFrame < 0 && Session.GetPhase() == ERacePhase::Race) { GoFrame = F; }
		if (GoFrame < 0)
		{
			Feed(-1, false);
			continue;
		}
		const int32 T = F - GoFrame;
		if (T == SaveAt)
		{
			Key.Save(Session, uint32(Session.GetFrame()));
			SavedCrash = ExtraCrash;
			SavedSlide = ExtraSlide;
		}
		const bool bRestore = T == RestoreAt && Key.bValid;
		if (bRestore)
		{
			Key.Load(Session);
			ExtraCrash = SavedCrash;
			ExtraSlide = SavedSlide;
			for (int32 I = 0; I < N; ++I)
			{
				for (FWindow& W : Windows[I]) { if (W.Start == S19) { W.Len = RestoreAt - S19; } }
			}
		}
		Feed(T, bRestore);
	}
	if (!TestTrue(TEXT("the race started"), GoFrame >= 0))
	{
		return false;
	}
	TestTrue(TEXT("keyframe saved and restored"), Key.bValid);
	TestTrue(*FString::Printf(TEXT("slides started (%d, one per injected wipe-out at least)"), Slides), Slides >= 2 * N + 3);
	TestEqual(TEXT("every slide is positioned at its rider (0x218700 push +0x28 = rider +0x140)"), Slides2D, 0);
	TestEqual(*FString::Printf(TEXT("no slide held outside a wipe-out (%s)"), *FString::Join(Bad, TEXT(", "))), HeldOutside, 0);
	TestEqual(TEXT("the keyframe restore plays nothing (no crash / slide / tweak from the rewound counters)"), RestoreSounds, 0);
	TestEqual(TEXT("the restore releases the slides of riders it puts out of their wipe-out"), HeldAfterRestore, 0);
	TestEqual(TEXT("no slide louder than its curve-7 volume, no board voice above 1"), TooLoud, 0);
	int32 HeldAtEnd = 0;
	for (int32 I = 0; I < N; ++I)
	{
		HeldAtEnd += (Boards[I]->IsSlideHeld() && Sims[I]->State.Motion != ERiderMotion::WipeOut) ? 1 : 0;
	}
	TestEqual(TEXT("no slide held at the end (every injected wipe-out exited)"), HeldAtEnd, 0);
	return true;
}

#endif
