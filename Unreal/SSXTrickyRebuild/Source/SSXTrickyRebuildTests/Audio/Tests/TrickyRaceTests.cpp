#include "Audio/SSXAudioDiag.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXSpeechSystem.h"
#include "Audio/SSXTrickyAudio.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Race/RaceSession.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/RiderSim.h"
#include "UI/SSXHudOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TrickyRaceFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
	using SSXAudioDiag::EResult;

	bool Resolved(EResult R) { return R != EResult::NotInManifest && R != EResult::AssetMissing; }

	struct FTrickyRun
	{
		TArray<SSXAudioDiag::FRecord> Records;
		TArray<int32> Frame107;
		int32 FrameWarn = -1, FrameEnd = -1, FirstSlice = -1, McPost = -1, McPlayed = -1;
		TArray<FString> Slices;
		bool bSlicesResolved = true;
		bool bMcSampleDecoded = false;
	};

	FTrickyRun Run(USSXAudioSubsystem& Audio, int32 Adds, int32 Frames)
	{
		const int32 Mode = int32(ERaceEventMode::CircuitRace);
		FRiderSim Sim;
		Sim.State.FinishResult = -1;
		Sim.State.BoostMeter = 0.9;
		SSXAudioDiag::Reset();
		Audio.SpeechLog.Reset();
		FTrickyRun R;
		for (int32 F = 0; F < Frames; ++F)
		{
			Sim.State.TrickyTimer = FRiderSim::TrickyTimerStep(Sim.State.TrickyTimer, false);   // 0x11B1F8
			if (F == 10)
			{
				for (int32 K = 0; K < Adds; ++K)
				{
					SSXAir::BoostAdd(Sim.State, 0.2, 1.0);   // 0x11B018
				}
			}
			const int32 Before = SSXAudioDiag::Records().Num();
			SSXGameAudio::TrickyFrame(Audio, &Sim, Mode);
			Audio.UpdateSpeech();   // 0x228998
			const TArray<SSXAudioDiag::FRecord>& All = SSXAudioDiag::Records();
			for (int32 I = Before; I < All.Num(); ++I)
			{
				const SSXAudioDiag::FRecord& Rec = All[I];
				R.Records.Add(Rec);
				if (Rec.System == TEXT("zbxsfx") && Rec.Id == TEXT("sfx/zBxsfx/107")) R.Frame107.Add(F);
				if (Rec.System == TEXT("tricky") && Rec.Id == TEXT("sfx/tricky/01") && Resolved(Rec.Result)) R.FrameWarn = F;
				if (Rec.System == TEXT("tricky") && Rec.Id == TEXT("sfx/tricky/02") && Resolved(Rec.Result)) R.FrameEnd = F;
				if (Rec.System == TEXT("trickyphrase"))
				{
					R.Slices.Add(Rec.Id);
					R.bSlicesResolved &= Resolved(Rec.Result);
					if (R.FirstSlice < 0) R.FirstSlice = F;
				}
				if (Rec.System == TEXT("mc") && Rec.Id == FString::Printf(TEXT("request 0x%08X"), SSXSpeech::RequestId(SSXSpeech::DbMC, 0x204C))
					&& Rec.Result == EResult::Played && R.McPost < 0) R.McPost = F;
				if (Rec.System == TEXT("speech-mc") && R.McPlayed < 0 && R.McPost >= 0)
				{
					R.McPlayed = F;
					R.bMcSampleDecoded = Resolved(Rec.Result);
				}
			}
		}
		return R;
	}

	USSXAudioSubsystem* RaceAudio(int32 ArcadeSfx)
	{
		USSXAudioSubsystem* Audio = NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
		Audio->ArcadeSfx = ArcadeSfx;
		SSXGameAudio::OnRaceSetup(*Audio, TEXT("gari"), 0, int32(ERaceEventMode::CircuitRace));
		return Audio;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioTrickyRaceTest, "SSX.Audio.TrickyRace", TrickyRaceFlags)
bool FSSXAudioTrickyRaceTest::RunTest(const FString&)
{
	const int32 Mode = int32(ERaceEventMode::CircuitRace);
	{
		USSXAudioSubsystem* Audio = RaceAudio(1);
		const FSSXTrickyPhraseContext Ctx = Audio->TrickyPhraseContext(Mode);
		if (!Ctx.bSongLoaded || Ctx.LastPhraseBase < 0 || !Audio->GetSpeech().IsLoaded())
		{
			AddError(FString::Printf(TEXT("race song / loop bank / speech not available (song %d, last phrase %d, speech %d)"), Ctx.bSongLoaded,
				Ctx.LastPhraseBase, Audio->GetSpeech().IsLoaded()));
			return false;
		}
		TestEqual(TEXT("Tricky phrase = PhrasesPerBank (default 4) - 1: the stereo phrase 48 (FUN_002246A8)"), Ctx.LastPhraseBase, 48);
		const FTrickyRun R = Run(*Audio, 1, 1300);
		AddInfo(FString::Printf(TEXT("on: #107 at %s, MC 0x204C posted %d played %d, %d slices from %d (%s), #1 at %d, #2 at %d"),
			*FString::JoinBy(R.Frame107, TEXT(","), [](int32 V) { return FString::FromInt(V); }), R.McPost, R.McPlayed, R.Slices.Num(), R.FirstSlice,
			R.Slices.Num() ? *R.Slices[0] : TEXT("-"), R.FrameWarn, R.FrameEnd));
		TestTrue(TEXT("zbxsfx #107 once, 500 ms after the fill (frame 10 + 30)"), R.Frame107.Num() == 1 && R.Frame107[0] >= 39 && R.Frame107[0] <= 41);
		TestEqual(TEXT("MC 0x204C (TRICKY! call) posted at the fill"), R.McPost, 10);
		TestTrue(TEXT("MC 0x204C played from the MC database on channel 0 (decoded sample)"), R.McPlayed >= 10 && R.McPlayed <= 12 && R.bMcSampleDecoded);
		TestEqual(TEXT("the song's last loop phrase: 8 slices twice"), R.Slices.Num(), 16);
		TestTrue(TEXT("every slice resolves to an imported asset"), R.Slices.Num() > 0 && R.bSlicesResolved);
		TestTrue(TEXT("first slice = the last phrase base, on the next beat"),
			R.Slices.Num() > 0 && R.Slices[0].EndsWith(FString::Printf(TEXT("/%02d"), Ctx.LastPhraseBase)) && R.FirstSlice >= 10 && R.FirstSlice <= 10 + 60);
		TestTrue(TEXT("tricky.bnk #1 at 2.7 s left (frame 10 + 1037), resolved"), FMath::Abs(R.FrameWarn - (10 + 1037)) <= 1);
		TestTrue(TEXT("tricky.bnk #2 at the end (frame 10 + 1199), resolved"), FMath::Abs(R.FrameEnd - (10 + 1199)) <= 1);
	}
	{
		USSXAudioSubsystem* Audio = RaceAudio(1);
		const FTrickyRun R = Run(*Audio, 2, 120);
		TestEqual(TEXT("two adds reaching 1.0 -> two #107"), R.Frame107.Num(), 2);
		TestEqual(TEXT("two adds: the fresh one still posts MC 0x204C"), R.McPost, 10);
		TestTrue(TEXT("two adds: the fresh one still starts the loop phrase"), R.FirstSlice >= 10);
	}
	{
		USSXAudioSubsystem* Audio = RaceAudio(0);
		const FTrickyRun R = Run(*Audio, 1, 1300);
		int32 Gated = 0, Other = 0;
		for (const SSXAudioDiag::FRecord& Rec : R.Records)
		{
			Gated += Rec.System == TEXT("tricky") && Rec.Result == EResult::Gated ? 1 : 0;
			Other += (Rec.System == TEXT("trickyphrase") || Rec.System == TEXT("speech-mc") || (Rec.System == TEXT("tricky") && Rec.Result != EResult::Gated)) ? 1 : 0;
		}
		TestEqual(TEXT("Arcade SFX off: #107 only"), R.Frame107.Num(), 1);
		TestTrue(TEXT("Arcade SFX off: no phrase, no MC, no tricky.bnk"), Other == 0 && R.McPost < 0);
		TestEqual(TEXT("Arcade SFX off: one gated record says why (ssx.Audio.Log)"), Gated, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioPointsTickTest, "SSX.Audio.PointsTick", TrickyRaceFlags)
bool FSSXAudioPointsTickTest::RunTest(const FString&)
{
	// FUN_0021a210
	FSSXHudOverlay O;
	O.Activate(int32(ERaceEventMode::CircuitRace), 0, 1, 0, nullptr, 0.0);
	FSSXHudInput In;
	In.Phase = 4;
	In.Players.AddDefaulted();
	auto ValueFor = [](int32 Points) { return (Points + 0.5) / (10000.0 * 0.6786545); };
	In.Players[0].TrickValue = ValueFor(1000);
	int32 Roll = 0, Steady = 0;
	for (int32 F = 0; F < 20; ++F) { O.Update(In); Roll += O.ConsumeTicks(); }
	for (int32 F = 0; F < 120; ++F) { O.Update(In); Steady += O.ConsumeTicks(); }
	TestEqual(TEXT("roll 0 -> 1000: 10 ticks"), Roll, 10);
	TestEqual(TEXT("points standing still: no tick (not a loop)"), Steady, 0);
	int32 Spin = 0;
	for (int32 F = 1; F <= 120; ++F) { In.Players[0].TrickValue = ValueFor(1000 + 20 * F); O.Update(In); Spin += O.ConsumeTicks(); }
	TestEqual(TEXT("spin +2400 points: 24 ticks"), Spin, 24);
	In.Players[0].TrickValue = 0.0;
	O.Update(In);
	TestEqual(TEXT("value cleared: no tick"), O.ConsumeTicks(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioHeldVoicesTest, "SSX.Audio.HeldVoices", TrickyRaceFlags)
bool FSSXAudioHeldVoicesTest::RunTest(const FString&)
{
	// 0x20AAB8
	for (const bool bAI : { false, true })
	{
		USSXBoardSoundComponent* B = NewObject<USSXBoardSoundComponent>();
		B->bIsAI = bAI;
		SSXAudioDiag::Reset();
		FRiderState S;
		S.Motion = ERiderMotion::Ground;
		S.SurfaceId = 0;
		S.ContactForward = FVector(0.0, 1.0, 0.0);
		TArray<float> Vols;
		for (int32 F = 0; F < 90; ++F)
		{
			S.Frame = F;
			S.Velocity = FVector(0.0, 100.0 * F, 0.0);
			B->UpdateFromRider(S, 1.f / 60.f);
			Vols.Add(B->LastVolume);
		}
		int32 Made = 0, BadRadius = 0;
		for (const SSXAudioDiag::FRecord& R : SSXAudioDiag::Records())
		{
			if (R.Id.StartsWith(TEXT("sfx/zboard/")) && R.b3D)
			{
				++Made;
				BadRadius += FMath::IsNearlyEqual(R.RadiusM, bAI ? SSXBoardAudio::AIVoiceRadius : SSXBoardAudio::PlayerVoiceRadius) ? 0 : 1;
			}
		}
		const float Min = FMath::Min(Vols), Max = FMath::Max(Vols);
		AddInfo(FString::Printf(TEXT("%s: %d voices made, glide volume %.3f .. %.3f"), bAI ? TEXT("AI") : TEXT("player"), Made, Min, Max));
		TestEqual(bAI ? TEXT("AI: one held voice (glide), made once") : TEXT("player: speed / glide / carve made once"), Made, bAI ? 1 : 3);
		TestEqual(TEXT("held voices carry the game radius (not 0)"), BadRadius, 0);
		TestTrue(TEXT("the glide volume follows the programs every frame (not stuck at its start value)"), Max > Min);
	}
	return true;
}

#endif
