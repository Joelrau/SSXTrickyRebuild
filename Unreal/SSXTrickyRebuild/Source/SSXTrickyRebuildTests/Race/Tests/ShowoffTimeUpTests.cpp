// 0x11DA40, 0x1AE7F0, 0x156E80, 0x117198, 0x111820, 0x16BAA8, 0x164FC0, 0x165400
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Harness/RiderSetup.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXRaceUIBridge.h"
#include "Sim/Air/TrickName.h"
#include "UI/SSXRaceUIHost.h"
#include "Progression/SSXProgression.h"
#include "Sim/Air/AirStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "UI/SSXFeProfile.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceShowoffTimeUpTest, "SSX.Race.ShowoffTimeUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXRaceShowoffTimeUpTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("gari race / sim data not available (%s): skipped"), *Err));
		return true;
	}
	FRiderSim Sim;
	SSXRiderSetup::RegisterStates(Sim, true);
	Sim.SetWorld(&World);
	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::PracticeShowoff;
	if (!TestTrue(TEXT("init"), Session.Init(Data, Cfg))) return false;
	Session.ShowoffMedalOf = [](int32 Score) { return SSXProg::ShowoffMedal(Score, 0); };
	Session.AddRacer(&Sim, true);
	SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim);
	if (!TestNotNull(TEXT("air context"), Air)) return false;
	Session.Start();
	auto Step = [&]() { Session.Tick(); Sim.Step(FRiderInputWord()); };
	for (int32 G = 0; G < 60 * 120 && Session.GetPhase() != ERacePhase::Race; ++G) Step();
	if (!TestTrue(TEXT("race running"), Session.GetPhase() == ERacePhase::Race)) return false;
	for (int32 F = 0; F < 120; ++F) Step();
	TestTrue(TEXT("showoff clock counts down from the track limit"), Session.GetClock() > 100.0 && Session.GetClock() < 120.0);

	constexpr int32 Banked = 60000;
	Air->Score.Score = Banked;
	Air->Score.SpinDegRace = 25 * 180;
	Air->Score.FlipDegRace = 9 * 360;
	Air->Score.GrabsRace = 5;
	Air->Score.RailsRace = 4;
	const SSXTrick::FRecord Best = SSXTrick::RailTrickRecord(1, 360);
	Air->Score.BestTrickPoints = 9000;
	Air->Score.BestTrick.W0 = Best.W0;
	Air->Score.BestTrick.W1 = Best.W1;
	Air->Score.BestTrick.bValid = Best.bValid;
	FRaceSessionKeyframe K;
	Session.SaveReplay(K);
	K.Clock = 0.5;
	Session.LoadReplay(K);
	Session.ClearCues();
	bool bTimedOut = false, bFinishCue = false;
	int32 FinishParam = -1;
	for (int32 F = 0; F < 60 && !Session.GetRacer(0).IsFinished(); ++F)
	{
		Step();
	}
	for (const FRaceCue& C : Session.GetCues())
	{
		bTimedOut |= C.Kind == ERaceCueKind::TimedOut;
		if (C.Kind == ERaceCueKind::Finish) { bFinishCue = true; FinishParam = C.Param; }
	}
	TestTrue(TEXT("time up finishes the racer"), Session.GetRacer(0).IsFinished());
	TestEqual(TEXT("finish result +0x418 = int(clock x 100) = 0 (0x11DA40)"), Session.GetRacer(0).FinishCs(), 0);
	TestTrue(TEXT("timed-out flag +0x582C (modes 3 / 5, result 0)"), bTimedOut);
	TestTrue(TEXT("finish cue with result 0 (PopFinish: TIME UP 3675, no score)"), bFinishCue && FinishParam == 0);

	// 0x113820
	for (int32 F = 0; F < 60 * 20 && Session.GetPhase() != ERacePhase::EndRace; ++F) Step();
	if (!TestTrue(TEXT("race end reached after the time-up"), Session.GetPhase() == ERacePhase::EndRace)) return false;
	Step();
	const TArray<FRaceResultRow>& Table = Session.GetFinalResults();
	if (!TestEqual(TEXT("one results row"), Table.Num(), 1)) return false;
	TestEqual(TEXT("results score 0: the timed-out flag zeroes +0x5910 every frame (0x156E80), the row copies 0 (0x111820)"), int32(Table[0].Score), 0);
	TestEqual(TEXT("live score 0"), int32(Session.GetRacer(0).Score), 0);
	TestEqual(TEXT("stats score 0"), Air->Score.Score, 0);
	TestEqual(TEXT("180s kept"), Table[0].SpinDeg / 180, 25);
	TestEqual(TEXT("flips kept"), Table[0].FlipDeg / 360, 9);
	TestEqual(TEXT("grabs kept"), Table[0].Grabs, 5);
	TestEqual(TEXT("rails kept"), Table[0].Rails, 4);
	TestTrue(TEXT("best trick record kept in the row (+0x74 = stats +0xE8; 0x156E80 zeroes only +0xF0)"),
		Table[0].BestTrickW0 == Best.W0 && Table[0].BestTrickW1 == Best.W1);
	{
		// 0x1B0BB8, 0x1551C0
		FSSXShowoffStats St;
		FSSXRaceUIBridge::ResultsStats(Table[0], St);
		TestEqual(TEXT("results best trick = the row record's name (0x1551C0)"), St.BestTrick, SSXTrick::FTrickTables::Get().Compose(Best.W0, Best.W1));
		if (SSXTrick::FTrickTables::Get().IsLoaded())
		{
			TestFalse(TEXT("results best trick text not empty"), St.BestTrick.IsEmpty());
		}
		TestEqual(TEXT("results score 0 in the stats"), St.Score, 0);
	}
	TestEqual(TEXT("no medal for score 0 (0x16BAA8)"), SSXProg::ShowoffMedal(int32(Table[0].Score), 0), int32(SSXProg::NoMedal));

	FSSXProfile P;
	static_cast<FSSXSaveData&>(P) = FSSXSaveData::NewSave();
	P.Slot[0].Char = 0;
	P.Slot[0].EventTricks = 0x5;
	if (FSSXCareer* C = P.SlotCareer(0)) { C->BookDone[1] |= 0x5; }
	P.ShowoffEndTricks(0, SSXProg::SEShowoff, Session.GetRacer(0).FinishCs());
	TestEqual(TEXT("time-up: this event's trick-book entries dropped (0x165400, +0x418 < 1)"), P.Slot[0].EventTricks, 0u);
	return true;
}

#endif
