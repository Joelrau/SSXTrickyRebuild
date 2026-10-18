// 0x2657A8 / 0x16D230 / 0x1BC8A0 / 0x11E660
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Engine/GameInstance.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXGameAudio.h"
#include "Effects/TrackEffects.h"
#include "Progression/SSXSettingsEffects.h"
#include "Sim/Air/RiderMessages.h"
#include "Sim/Air/TrickScore.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ShowOffTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	int32 CountType(const FRiderMessages& Q, int32 Type, int32* OutValue = nullptr)
	{
		int32 N = 0;
		for (const FRiderMessage& M : Q.Slots)
		{
			if (M.Type == Type)
			{
				++N;
				if (OutValue) { *OutValue = M.Value; }
			}
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXShowOffRacerCountTest, "SSX.ShowOff.RacerCount", ShowOffTestFlags)
bool FSSXShowOffRacerCountTest::RunTest(const FString&)
{
	using namespace SSXSettings;
	// 0x2657A8 / 0x163980 / 0x165558 / 0x1664B0
	TestEqual(TEXT("SE showoff from the FE: the human only (max boarders 6)"), RacerCount(3, 1, 6), 1);
	TestEqual(TEXT("SE showoff from the FE, 2 players: 2"), RacerCount(3, 2, 4), 2);
	TestEqual(TEXT("WC showoff: 1"), RacerCount(5, 1, 6), 1);
	TestEqual(TEXT("WC showoff, 2 players: still 1"), RacerCount(5, 2, 6), 1);
	TestEqual(TEXT("SE race: max boarders"), RacerCount(2, 1, 6), 6);
	TestEqual(TEXT("freeride: players"), RacerCount(1, 1, 6), 1);
	TestEqual(TEXT("tutorial: 1"), RacerCount(6, 1, 6), 1);
	// 0x1BC8A0 / 0x164288
	TestEqual(TEXT("next event Showoff: humans"), NextEventRacerCount(3, 1, 6), 1);
	TestEqual(TEXT("next event Race: max boarders"), NextEventRacerCount(2, 1, 6), 6);
	TestEqual(TEXT("launch after next event Showoff"), LaunchRacerCount(3, 1, 6, NextEventRacerCount(3, 1, 6)), 1);
	TestEqual(TEXT("launch of SE showoff from the FE (no next-event count)"), LaunchRacerCount(3, 1, 6, -1), 1);
	TestEqual(TEXT("launch of WC showoff ignores a next-event count"), LaunchRacerCount(5, 1, 6, 6), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXShowOffMultiplierSoundTest, "SSX.ShowOff.MultiplierSound", ShowOffTestFlags)
bool FSSXShowOffMultiplierSoundTest::RunTest(const FString&)
{
	using namespace SSXEffects;
	// 0x219D70
	TestEqual(TEXT("X2 -> #116"), SSXBoardAudio::MultiplierSound(2.0), 116);
	TestEqual(TEXT("X3 -> #117"), SSXBoardAudio::MultiplierSound(3.0), 117);
	TestEqual(TEXT("X5 -> #118"), SSXBoardAudio::MultiplierSound(5.0), 118);
	TestEqual(TEXT("X4 -> #117"), SSXBoardAudio::MultiplierSound(4.0), 117);

	USSXAudioSubsystem* Audio = NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	Audio->UISoundLog.Reset();
	SSXGameAudio::OnTrackMultiplierOp(*Audio, 2.0);
	SSXGameAudio::OnTrackMultiplierOp(*Audio, 3.0);
	SSXGameAudio::OnTrackMultiplierOp(*Audio, 5.0);
	TestEqual(TEXT("op 14 cues play zbxsfx #116 / #117 / #118"), Audio->UISoundLog,
		TArray<FString>({ TEXT("sfx/zBxsfx/116"), TEXT("sfx/zBxsfx/117"), TEXT("sfx/zBxsfx/118") }));

	static const TCHAR* Stems[] = { TEXT("gari"), TEXT("aloha"), TEXT("mesa"), TEXT("merquer"), TEXT("elysium"), TEXT("snow"),
		TEXT("megaple"), TEXT("pipe"), TEXT("untrack"), TEXT("alaska") };
	for (const TCHAR* Stem : Stems)
	{
		FEffectData D;
		if (!D.Load(FEffectData::DefaultPath(Stem))) { continue; }
		int32 Effect = INDEX_NONE;
		float Arg = 0.f;
		for (int32 E = 0; E < D.Effects.Num() && Effect == INDEX_NONE; ++E)
		{
			for (int32 K = 0; K < D.Effects[E].Y; ++K)
			{
				const FRecord& R = D.Records[D.Effects[E].X + K];
				if (R.Type == 14) { Effect = E; Arg = D.F(R, 0); break; }
			}
		}
		if (Effect == INDEX_NONE) { continue; }
		const int32 M = int32(Arg);
		int32 Inst = INDEX_NONE;
		for (int32 I = 0; I < D.NumInstances() && Inst == INDEX_NONE; ++I)
		{
			for (int32 Slot = 0; Slot < 7; ++Slot)
			{
				if (D.EffectFor(I, Slot) == Effect) { Inst = I; break; }
			}
		}
		struct FCase { int32 Mode; bool bHuman; int32 Motion; double Mult; bool bTake; bool bSound; const TCHAR* Name; };
		const FCase Cases[] = {
			{ 3, true, 1, 1.0, true, true, TEXT("SE showoff, human in the air: taken + cue") },
			{ 5, true, 3, 1.0, true, true, TEXT("WC showoff, human on a rail: taken + cue") },
			{ 3, true, 2, 1.0, false, true, TEXT("showoff, human on the ground: not taken, cue still plays") },
			{ 3, true, 1, double(M), false, true, TEXT("showoff, not above M_pts: not taken, cue still plays") },
			{ 3, false, 1, 1.0, true, false, TEXT("showoff, computer rider: taken, no cue") },
			{ 2, true, 1, 1.0, false, true, TEXT("race mode: no multiplier, cue still plays (0x11E660 has no mode test for it)") },
		};
		for (const FCase& C : Cases)
		{
			FEffectWorld W;
			W.Init(&D, C.Mode, TrackIdForStem(Stem));
			FEffectRider Rider;
			Rider.bHuman = C.bHuman;
			Rider.Motion = C.Motion;
			Rider.PointsMult = C.Mult;
			int32 Taken = 0, Sounds = 0;
			float SoundV = 0.f;
			W.OnOutcome = [&](const FOutcome& O)
			{
				if (O.Kind == EOutcome::Multiplier && O.Rider == &Rider) { ++Taken; }
				if (O.Kind == EOutcome::MultiplierSound && O.Rider == &Rider) { ++Sounds; SoundV = float(O.V); }
			};
			W.RunEffect(Effect, Inst, &Rider);
			for (int32 F = 0; F < 600 && Sounds + Taken == 0; ++F) { W.Frame(); }
			TestEqual(FString::Printf(TEXT("%s %s: multiplier outcome"), Stem, C.Name), Taken > 0, C.bTake);
			TestEqual(FString::Printf(TEXT("%s %s: sound outcome"), Stem, C.Name), Sounds > 0, C.bSound);
			if (C.bTake) { TestTrue(TEXT("M_pts = M after taking"), FMath::IsNearlyEqual(Rider.PointsMult, double(Arg))); }
			if (C.bSound) { TestTrue(TEXT("cue carries the script's value"), SoundV == Arg); }
		}
		AddInfo(FString::Printf(TEXT("%s: effect %d has op 14 (X%d)"), Stem, Effect, M));
		return true;
	}
	SSXTest::Skip(*this, TEXT("no exported track effects with op 14 found; script cases skipped"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXShowOffMultiplierLifetimeTest, "SSX.ShowOff.MultiplierLifetime", ShowOffTestFlags)
bool FSSXShowOffMultiplierLifetimeTest::RunTest(const FString&)
{
	using namespace SSXAir;
	FTrickScore S;
	// FUN_00155cd0
	TestTrue(TEXT("X2 taken from M_pts 1"), S.ScriptMultiplier(2.f));
	TestTrue(TEXT("M_pts = 2"), S.PointsMult == 2.0);
	TestFalse(TEXT("X2 again not taken (not above M_pts)"), S.ScriptMultiplier(2.f));
	TestEqual(TEXT("both X2 hits counted (+0xB0)"), S.MultiplierCounts[0], 2);
	int32 V = 0;
	TestEqual(TEXT("one 0xC entry"), CountType(S.Messages, SSXMsg::Multiplier, &V), 1);
	TestEqual(TEXT("showing X2"), V, 2);
	TestTrue(TEXT("X3 taken"), S.ScriptMultiplier(3.f));
	TestEqual(TEXT("still one 0xC entry (slot reused)"), CountType(S.Messages, SSXMsg::Multiplier, &V), 1);
	TestEqual(TEXT("now X3"), V, 3);

	// 0x156E80
	for (int32 F = 0; F < 36000; ++F) { S.Messages.Age(); }
	TestEqual(TEXT("X3 does not age out"), CountType(S.Messages, SSXMsg::Multiplier), 1);

	// 0x157008 / 0x157308
	S.Land(false, 0);
	TestTrue(TEXT("landing resets M_pts to 1"), S.PointsMult == 1.0);
	TestEqual(TEXT("landing clears X3"), CountType(S.Messages, SSXMsg::Multiplier), 0);
	TestTrue(TEXT("X2 after landing taken"), S.ScriptMultiplier(2.f));
	CountType(S.Messages, SSXMsg::Multiplier, &V);
	TestEqual(TEXT("X2 showing"), V, 2);
	S.Bail();
	TestEqual(TEXT("bail clears it"), CountType(S.Messages, SSXMsg::Multiplier), 0);
	TestTrue(TEXT("X5 taken"), S.ScriptMultiplier(5.f));
	S.Respawn();
	TestEqual(TEXT("respawn clears it"), CountType(S.Messages, SSXMsg::Multiplier), 0);
	TestTrue(TEXT("X3 taken"), S.ScriptMultiplier(3.f));
	// 0x116AB8 / 0x155A20
	S.ResetRaceStats();
	TestEqual(TEXT("race_stats reset clears it"), CountType(S.Messages, SSXMsg::Multiplier), 0);
	TestTrue(TEXT("and M_pts = 1"), S.PointsMult == 1.0);
	return true;
}

#endif
