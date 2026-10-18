// 0x11F050, 0x116AB8, 0x11EA70, 0x11EAF8, 0x11EBF8, 0x111820, 0x16C228, 0x16C328, 0x16C008, 0x167EA0
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Particles/SSXAngerMarker.h"
#include "Harness/RiderSetup.h"
#include "Progression/SSXHeats.h"
#include "Progression/SSXProgression.h"
#include "Race/RaceRules.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AggFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	SSXRace::FRivalry Pair(int32 Start, int32 Level, int32 Bumps, int32 Shoves, int32 Knocks)
	{
		SSXRace::FRivalry R;
		R.StartLevel = Start; R.Level = Level; R.Bumps = Bumps; R.Shoves = Shoves; R.Hits = Knocks;
		return R;
	}
	int32 Blended(int32 Start, int32 Level, int32 Bumps, int32 Shoves, int32 Knocks)
	{
		SSXRace::FRivalry R = Pair(Start, Level, Bumps, Shoves, Knocks);
		SSXRace::BlendLevel(R);
		return R.Level;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAggressionBlendTest, "SSX.Race.Aggression.Blend", AggFlags)
bool FSSXAggressionBlendTest::RunTest(const FString& Parameters)
{
	using namespace SSXRace;
	// 0x11F050
	TestEqual(TEXT("no contact: back to the start level"), Blended(50, 95, 0, 0, 0), 50);
	TestEqual(TEXT("knock-downs only: w = 1, keeps the level"), Blended(50, 95, 0, 0, 2), 95);
	TestEqual(TEXT("bumps only: w = 1/3 (50 + 15.33)"), Blended(50, 96, 3, 0, 0), 65);
	TestEqual(TEXT("shoves only: w = 2/3 (50 + 30.67)"), Blended(50, 96, 0, 4, 0), 80);
	TestEqual(TEXT("1 bump + 1 shove + 1 knock: w = 6/9 (50 + 30.67)"), Blended(50, 96, 1, 1, 1), 80);
	TestEqual(TEXT("2 bumps + 1 knock: w = 5/9 (50 + 25.56)"), Blended(50, 96, 2, 0, 1), 75);
	TestEqual(TEXT("fall: 80 -> 34, 1 bump: 80 - 15.33 truncated"), Blended(80, 34, 1, 0, 0), 64);
	TestEqual(TEXT("fall truncates toward zero: 10 -> 0, 1 bump + 1 shove: 10 - 5"), Blended(10, 0, 1, 1, 0), 5);
	TestEqual(TEXT("1 bump: 50 -> 51: 50.33 -> 50"), Blended(50, 51, 1, 0, 0), 50);

	// 0x116AB8
	FRivalry Setup; Setup.Level = 64; Setup.LevelMax = 90; Setup.AngerLimit = 10; Setup.Kind = 1;
	FRivalry R = Pair(1, 2, 3, 4, 5); R.Anger = 7; R.Score = 9;
	ResetRivalry(R, Setup);
	TestTrue(TEXT("reset: level = start = set-up level, counters / anger / score 0, kind 1 = friend"),
		R.Level == 64 && R.StartLevel == 64 && R.LevelMax == 90 && R.AngerLimit == 10 && R.Kind == 1 && R.bFriend
		&& R.Bumps == 0 && R.Shoves == 0 && R.Hits == 0 && R.Anger == 0 && R.Score == 0);

	// 0x11EA70, 0x11EAF8
	FRivalry Hum[MaxRacers], Ai[MaxRacers];
	FGrudge GH, GA;
	for (FRivalry& P : Ai) { FRivalry S; S.Level = 50; S.AngerLimit = 30; ResetRivalry(P, S); }
	OnBump(GA, Ai, GH, Hum, 1, 0, true, false, 100, false);
	TestTrue(TEXT("human bumps AI: bumps 1, anger 9"), Ai[0].Bumps == 1 && Ai[0].Anger == 9 && !GA.bActive);
	OnBump(GA, Ai, GH, Hum, 1, 0, true, false, 101, true);
	TestTrue(TEXT("human shoves AI: shoves 1, anger 24"), Ai[0].Shoves == 1 && Ai[0].Anger == 24 && !GA.bActive);
	OnBump(GA, Ai, GH, Hum, 1, 0, true, false, 102, false);
	TestTrue(TEXT("anger capped at the limit 30 -> grudge (kind 0 below 60, level + 15, anger 0, score + 1)"),
		GA.bActive && GA.Rival == 0 && GA.Kind == 0 && Ai[0].Level == 65 && Ai[0].Anger == 0 && Ai[0].Score == 1 && Ai[0].Bumps == 2);
	Ai[0].Anger = 20;
	OnBump(GH, Hum, GA, Ai, 0, 1, false, true, 103, true);
	TestTrue(TEXT("AI shoves its grudge human: human's shoves 1, grudge relieved on the anger (0x11EF00 -45, clamp 0), level kept, AI score + 1"),
		Hum[1].Shoves == 1 && Ai[0].Anger == 0 && Ai[0].Level == 65 && Ai[0].Score == 2);
	OnBump(GH, Hum, GA, Ai, 0, 1, false, true, 104, false);
	TestTrue(TEXT("AI bumps the human: bumps only"), Hum[1].Bumps == 1 && Ai[0].Score == 2 && Hum[1].Anger == 0);
	OnKnockdown(GA, Ai, GH, Hum, 1, 0, true, false, 105);
	TestTrue(TEXT("human knocks the AI down: knock-down counter (+0xA) 1, anger 30 -> grudge kind 1 (65 >= 60), level 80, score + 2"),
		Ai[0].Hits == 1 && GA.Kind == 1 && Ai[0].Level == 80 && Ai[0].Score == 4);
	BlendLevel(Ai[0]);
	TestEqual(TEXT("scripted race blend 50 -> 80 (2 / 1 / 1) = 67"), Ai[0].Level, 67);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAggressionTableTest, "SSX.Race.Aggression.Table", AggFlags)
bool FSSXAggressionTableTest::RunTest(const FString& Parameters)
{
	constexpr int32 N = 4;
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("gari race / sim data not available (%s): skipped"), *Err));
		return true;
	}
	for (const ERaceEventMode Mode : { ERaceEventMode::CircuitRace, ERaceEventMode::PracticeShowoff })
	{
		const bool bBlend = Mode == ERaceEventMode::CircuitRace;
		TArray<TUniquePtr<FRiderSim>> Sims;
		for (int32 I = 0; I < N; ++I)
		{
			TUniquePtr<FRiderSim> S = MakeUnique<FRiderSim>();
			SSXRiderSetup::RegisterStates(*S, true);
			S->SetWorld(&World);
			Sims.Add(MoveTemp(S));
		}
		FRaceSession Session;
		FRaceConfig Cfg;
		Cfg.Mode = Mode;
		if (!TestTrue(TEXT("init"), Session.Init(Data, Cfg))) return false;
		for (int32 I = 0; I < N; ++I) Session.AddRacer(Sims[I].Get(), I == 0);
		const int32 StartLv[N] = { 0, 40, 70, 90 };
		for (int32 I = 1; I < N; ++I)
		{
			SSXRace::FRivalry& S = Session.GetRacerMutable(I).RivalSetup[0];
			S.Level = StartLv[I]; S.Kind = I == 1 ? 1 : 0;
		}
		Session.Start();
		TestEqual(TEXT("the start reset loads the set-up level (0x116AB8)"), Session.GetRacer(2).Rivals[0].Level, 70);
		auto Step = [&]() { Session.Tick(); for (auto& S : Sims) S->Step(FRiderInputWord()); };
		int32 Guard = 0;
		while (Session.GetPhase() != ERacePhase::Race && Guard++ < 60 * 120) Step();
		if (!TestTrue(TEXT("race running"), Session.GetPhase() == ERacePhase::Race)) return false;
		for (int32 F = 0; F < 120; ++F) Step();
		struct FScript { int32 Level, Bumps, Shoves, Knocks, Score; } const Sc[N] = { {}, { 85, 0, 0, 1, 3 }, { 39, 2, 0, 0, 1 }, { 75, 0, 0, 0, 0 } };
		for (int32 I = 1; I < N; ++I)
		{
			SSXRace::FRivalry& P = Session.GetRacerMutable(I).Rivals[0];
			P.Level = Sc[I].Level; P.Bumps = Sc[I].Bumps; P.Shoves = Sc[I].Shoves; P.Hits = Sc[I].Knocks; P.Score = Sc[I].Score;
		}
		Session.SetPhase(ERacePhase::EndRace);
		Step();
		const TArray<FRaceResultRow>& Table = Session.GetFinalResults();
		if (!TestEqual(TEXT("one row per racer"), Table.Num(), N)) return false;
		const int32 Expect[N] = { 0, 85, 59, 90 };
		for (const FRaceResultRow& Row : Table)
		{
			const FString M = FString::Printf(TEXT("mode %d racer %d"), int32(Mode), Row.Racer);
			if (Row.bHuman) { TestEqual(M + TEXT(": own row +0x9C = 3"), Row.Kind, 3); continue; }
			TestEqual(M + TEXT(": +0xA4 start level"), Row.LevelStart, StartLv[Row.Racer]);
			TestEqual(M + TEXT(": +0xAC level (blended in modes 2 / 4 only)"), Row.Level, bBlend ? Expect[Row.Racer] : Sc[Row.Racer].Level);
			TestEqual(M + TEXT(": +0x9C kind"), Row.Kind, Row.Racer == 1 ? 1 : 0);
			TestEqual(M + TEXT(": +0xB4 score"), Row.PairScore, Sc[Row.Racer].Score);
			TestEqual(M + TEXT(": live level = table level"), Session.GetRacer(Row.Racer).Rivals[0].Level, Row.Level);
		}
		// the table is taken once: a second race-end pass does not blend again
		Session.SetPhase(ERacePhase::EndRace);
		Step();
		TestEqual(TEXT("no second blend"), Session.GetRacer(2).Rivals[0].Level, bBlend ? 59 : 39);
		for (int32 I = 0; I < N; ++I) Session.DetachRacer(I);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAggressionPersistTest, "SSX.Race.Aggression.Persist", AggFlags)
bool FSSXAggressionPersistTest::RunTest(const FString& Parameters)
{
	const TArray<int32> Riders = { 0, 3, 5, 7, 9, 11 };
	// 0x11F050, 0x16C228
	TArray<int32> Toward = { 0, Blended(50, 95, 0, 0, 1), Blended(70, 39, 2, 0, 0), Blended(30, 30, 0, 0, 0), 12, 99 };
	FSSXProgressSession Sess;
	SSXHeats::CarryOver(Riders, 0, Toward, Sess.Carried);
	TestTrue(TEXT("C+0x1C = blended levels of the opponents only"), Sess.Carried[3] == 95 && Sess.Carried[5] == 59 && Sess.Carried[7] == 30
		&& Sess.Carried[0] == -1 && Sess.Carried[1] == -1);

	FSSXCareer Career;
	for (int32 K = 0; K < SSXProg::NumRiders; ++K) { Career.Opponent[K][1] = 50; }
	const TArray<int32> Finish = { 0, 3, 5, 7, 9, 11 };
	Sess.Heat = 1;
	FSSXCareer C1 = Career;
	SSXProg::CircuitHeatEnd(Sess, Finish, 0, 4, &C1);
	TestEqual(TEXT("heat 1: career untouched"), C1.Opponent[3][1], 50);
	Sess.Heat = 2; Sess.Elim.Reset(); Sess.Elim.Flag[0] = 1;
	FSSXCareer C2 = Career;
	SSXProg::CircuitHeatEnd(Sess, Finish, 0, 4, &C2);
	TestEqual(TEXT("final with the player eliminated: career untouched"), C2.Opponent[3][1], 50);
	// 0x16C328
	Sess.Heat = 2; Sess.Elim.Reset();
	const TArray<int32> Third = { 3, 5, 0, 7, 9, 11 };
	const int32 Code = SSXProg::CircuitHeatEnd(Sess, Third, 0, 4, &Career);
	TestEqual(TEXT("final -> exit 2"), Code, SSXHeats::ExitDone);
	TestTrue(TEXT("career level words = C+0x1C (>= 0 only)"), Career.Opponent[3][1] == 95 && Career.Opponent[5][1] == 59 && Career.Opponent[7][1] == 30
		&& Career.Opponent[9][1] == 12 && Career.Opponent[11][1] == 99 && Career.Opponent[1][1] == 50 && Career.Opponent[0][1] == 50);

	// 0x16C008
	SSXHeats::FData D;
	for (int32 A = 0; A < SSXHeats::NumRiders; ++A) for (int32 B = 0; B < SSXHeats::NumRiders; ++B) { D.Agg[A][B][1] = 20; }
	int32 Carried[SSXHeats::NumRiders], Words[SSXHeats::NumRiders];
	for (int32 K = 0; K < SSXHeats::NumRiders; ++K) { Carried[K] = -1; Words[K] = Career.Opponent[K][1]; }
	TArray<TArray<int32>> L;
	SSXHeats::InitialLevels(D, Riders, 0, 0, Carried, Words, L);
	TestTrue(TEXT("next race: AI level toward the human from the career (95 - 2, 59 - 2, floor 20 for 12)"),
		L[1][0] == 93 && L[2][0] == 57 && L[4][0] == 20 && L[5][0] == 97);
	// 0x166600
	const SSXHeats::FData& Agg = SSXHeats::FData::Get();
	if (Agg.bLoaded)
	{
		FSSXCareer New;
		SSXProg::NewCareer(New, 4);
		bool bOk = true;
		for (int32 K = 0; K < SSXProg::NumRiders; ++K) { bOk &= New.Opponent[K][1] == Agg.Agg[K][4][1]; }
		TestTrue(TEXT("new career words = 0x167DA0(k, char) word 1"), bOk);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAggressionSingleEventSetupTest, "SSX.Race.Aggression.SingleEventSetup", AggFlags)
bool FSSXAggressionSingleEventSetupTest::RunTest(const FString& Parameters)
{
	using namespace SSXRace;
	using namespace SSXParticles;
	// 0x165CF0, 0x167EA0
	const TArray<int32> Riders = { 4, 3, 5 };
	SSXHeats::FData D;
	for (int32 A = 0; A < SSXHeats::NumRiders; ++A)
		for (int32 B = 0; B < SSXHeats::NumRiders; ++B) { D.Agg[A][B][0] = 50; D.Agg[A][B][1] = 20; D.Agg[A][B][2] = 90; D.Agg[A][B][3] = 0; }
	int32 Career[SSXHeats::NumRiders] = {};
	Career[3] = 55; Career[5] = 7;
	TArray<TArray<int32>> L;
	SSXHeats::SetupLevels(D, Riders, 0, Career, L);
	TestTrue(TEXT("0x167EA0: toward the human = the career word as is (no -2, no word-1 floor), toward a computer rider = word 1"),
		L[1][0] == 55 && L[2][0] == 7 && L[1][2] == 20 && L[2][1] == 20 && L[0][1] == 20);

	FRivalry Ai[MaxRacers], Hum[MaxRacers];
	for (int32 J = 0; J < 3; ++J)
	{
		FRivalry S; S.Level = L[1][J]; S.LevelMax = D.Agg[3][Riders[J]][2]; S.AngerLimit = D.Agg[3][Riders[J]][0]; S.Kind = D.Agg[3][Riders[J]][3];
		ResetRivalry(Ai[J], S);
	}
	FGrudge GA, GH;
	FAngerMarker M;
	FAngerSprite Spr;
	int32 Frame = 0;
	auto Step = [&]()
	{
		GrudgeCooldown(GA, Ai, Frame);
		FAngerMarkerInput In;
		In.bShown = true; In.Human = 0; In.Level = Ai[0].Level; In.Anger = Ai[0].Anger;
		In.bGrudge = GA.bActive; In.GrudgeRival = GA.Rival; In.GrudgeKind = GA.Kind;
		M.Frame(In);
		++Frame;
		return M.Sprite(1000.0, Spr);
	};
	// 0x11ECF8, 0x11EA70, 0x11EAF8, 0x11EBF8
	int32 Drawn = 0;
	for (int32 K = 0; K < 1800; ++K) { Drawn += Step() ? 1 : 0; }
	TestTrue(TEXT("no contact for 30 s: no grudge, marker never drawn"), !GA.bActive && Drawn == 0);
	// 0x124920
	for (int32 K = 0; K < 4; ++K) { OnBump(GA, Ai, GH, Hum, 1, 0, true, false, Frame, false); }
	TestTrue(TEXT("4 bumps: anger 36 below the limit 50, no grudge"), Ai[0].Anger == 36 && !GA.bActive);
	OnKnockdown(GA, Ai, GH, Hum, 1, 0, true, false, Frame);
	TestTrue(TEXT("knock-down reaches the limit: kind-0 grudge on the human, level 55 + 15"), GA.bActive && GA.Rival == 0 && GA.Kind == 0
		&& Ai[0].Level == 70 && Ai[0].Anger == 0);
	for (int32 K = 0; K < 1800; ++K) { Step(); }
	TestTrue(TEXT("30 s later: kind 0 never cools, the white \"!\" stays"), Step() && !GA.bCooled
		&& FMath::IsNearlyEqual(Spr.Argb.Y, 1.0) && FMath::IsNearlyEqual(Spr.Argb.Z, 1.0) && FMath::IsNearlyEqual(Spr.Argb.W, 1.0));
	return true;
}

#endif
