// 0x111820, 0x11F2A0
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Harness/RiderSetup.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXReplayKeyframe.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceResultsTableTest, "SSX.Race.ResultsTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXRaceResultsTableTest::RunTest(const FString& Parameters)
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
	Cfg.Mode = ERaceEventMode::CircuitRace;
	if (!TestTrue(TEXT("init"), Session.Init(Data, Cfg))) return false;
	for (int32 I = 0; I < N; ++I) Session.AddRacer(Sims[I].Get(), I == 0);
	Session.Start();
	auto Step = [&]() { Session.Tick(); for (auto& S : Sims) S->Step(FRiderInputWord()); };
	int32 Guard = 0;
	while (Session.GetPhase() != ERacePhase::Race && Guard++ < 60 * 120) Step();
	if (!TestTrue(TEXT("race running"), Session.GetPhase() == ERacePhase::Race)) return false;

	FSSXRaceKeyframe Mid;
	for (int32 F = 0; F < 300; ++F) Step();
	Mid.Save(Session, 0);
	for (int32 F = 0; F < 300; ++F) Step();
	TestTrue(TEXT("nobody finished yet"), !Session.GetRacer(1).IsFinished() && !Session.GetRacer(0).IsFinished());

	TArray<FRaceStanding> Live;
	Session.GetStandings(Live);
	const int32 Elapsed = FMath::Max(int32(Session.GetClock() * 100.0), 0);
	TMap<int32, int32> Expect;
	for (const FRaceStanding& Row : Live)
	{
		if (Row.bHuman) { Expect.Add(Row.Racer, 50000); continue; }
		const float Total = float(Session.GetRacer(Row.Racer).RaceTotal);
		const float Done = Total - float(Row.Remaining);
		Expect.Add(Row.Racer, Done <= 0.0f ? Elapsed : int32((float(Elapsed) * Total) / Done));
	}
	Session.SetPhase(ERacePhase::EndRace);
	Step();
	const TArray<FRaceResultRow> Table = Session.GetFinalResults();
	if (!TestEqual(TEXT("one row per racer"), Table.Num(), N)) return false;
	for (int32 K = 0; K < N; ++K)
	{
		const FRaceResultRow& Row = Table[K];
		TestEqual(FString::Printf(TEXT("row %d racer %d time (0x11F2A0 / unfinished human 50000)"), K, Row.Racer), Row.TimeCs, Expect.FindRef(Row.Racer));
		if (K > 0) TestTrue(FString::Printf(TEXT("row %d ordered by time"), K), Table[K - 1].TimeCs <= Row.TimeCs);
		TestEqual(FString::Printf(TEXT("row %d = results order"), K), Row.Racer, Session.GetResults()[K]);
		TestEqual(FString::Printf(TEXT("row %d rivalry level toward the human"), K), Row.Level, Session.GetRacer(Row.Racer).Rivals[0].Level);
	}
	TestEqual(TEXT("the unfinished human's finish result is set to 0 (0x111820)"), Session.GetRacer(0).FinishCs(), 0);
	TestTrue(TEXT("the human (50000) is last"), Table.Last().bHuman);

	auto Same = [](const TArray<FRaceResultRow>& A, const TArray<FRaceResultRow>& B)
	{
		if (A.Num() != B.Num()) return false;
		for (int32 I = 0; I < A.Num(); ++I)
		{
			if (A[I].Racer != B[I].Racer || A[I].TimeCs != B[I].TimeCs || A[I].Level != B[I].Level || A[I].LevelStart != B[I].LevelStart
				|| A[I].Score != B[I].Score || A[I].bHuman != B[I].bHuman) return false;
		}
		return true;
	};
	int32 Frames = 0, Stable = 0;
	bool bLiveChanged = false;
	for (int32 Loop = 0; Loop < 3; ++Loop)
	{
		Mid.Load(Session);
		for (int32 F = 0; F < 120; ++F)
		{
			Step();
			++Frames;
			Stable += Same(Session.GetFinalResults(), Table) ? 1 : 0;
			TArray<FRaceStanding> Now;
			Session.GetStandings(Now);
			for (const FRaceStanding& Row : Now)
			{
				for (const FRaceStanding& Was : Live)
				{
					bLiveChanged |= Was.Racer == Row.Racer && !Row.bHuman && Was.EstimatedCs != Row.EstimatedCs;
				}
			}
		}
	}
	TestTrue(TEXT("the replay rewinds the live race (live estimates differ)"), bLiveChanged);
	TestEqual(TEXT("results table unchanged on every replay frame"), Stable, Frames);
	AddInfo(FString::Printf(TEXT("table: %d rows, times %d / %d / %d / %d cs; stable over %d replay frames"), Table.Num(),
		Table[0].TimeCs, Table[1].TimeCs, Table[2].TimeCs, Table[3].TimeCs, Frames));

	Session.Start();
	TestEqual(TEXT("Start clears the table"), Session.GetFinalResults().Num(), 0);
	for (int32 I = 0; I < N; ++I) Session.DetachRacer(I);
	return true;
}

// 0x111738, 0x11D998
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceLoadedReplayEndTest, "SSX.Race.LoadedReplayEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXRaceLoadedReplayEndTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("gari race / sim data not available (%s): skipped"), *Err));
		return true;
	}
	for (const bool bLoaded : { false, true })
	{
		FRiderSim Sim;
		SSXRiderSetup::RegisterStates(Sim, true);
		Sim.SetWorld(&World);
		FRaceSession Session;
		FRaceConfig Cfg;
		Cfg.Mode = ERaceEventMode::PracticeShowoff;
		if (!TestTrue(TEXT("init"), Session.Init(Data, Cfg))) return false;
		Session.AddRacer(&Sim, true);
		Session.Start();
		Session.SetLoadedReplay(bLoaded);
		auto Step = [&]() { Session.Tick(); Sim.Step(FRiderInputWord()); };
		int32 Guard = 0;
		while (Session.GetPhase() != ERacePhase::Race && Guard++ < 60 * 120) Step();
		if (!TestTrue(TEXT("race running"), Session.GetPhase() == ERacePhase::Race)) return false;
		for (int32 F = 0; F < 60; ++F) Step();
		FRaceSessionKeyframe K;
		Session.SaveReplay(K);
		K.Clock = 0.5;
		Session.LoadReplay(K);
		Guard = 0;
		while (Session.GetPhase() == ERacePhase::Race && Guard++ < 60 * 30) Step();
		Step();
		const FString Tag = bLoaded ? TEXT("loaded replay") : TEXT("live race");
		TestEqual(Tag + TEXT(": phase after the race end"), FString(LexToString(Session.GetPhase())), FString(LexToString(bLoaded ? ERacePhase::ReplayEndRace : ERacePhase::EndRace)));
		TestEqual(Tag + TEXT(": results table (vfunc_18) only without the loaded flag"), Session.GetFinalResults().Num(), bLoaded ? 0 : 1);
		if (bLoaded)
		{
			const double Clock = Session.GetClock();
			for (int32 F = 0; F < 120; ++F) Step();
			TestTrue(TEXT("phase 0xB stays (empty handler)"), Session.GetPhase() == ERacePhase::ReplayEndRace);
			TestEqual(TEXT("phase 0xB does not advance the clock"), Session.GetClock(), Clock);
		}
		Session.DetachRacer(0);
	}
	return true;
}

#endif
