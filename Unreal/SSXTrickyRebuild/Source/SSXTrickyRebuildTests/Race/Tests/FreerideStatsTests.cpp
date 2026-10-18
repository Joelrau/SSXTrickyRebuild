// 0x1B16F0, 0x1B1278, 0x111820, 0x155DC8
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Harness/RiderSetup.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXRaceUIBridge.h"
#include "Sim/Air/AirStates.h"
#include "Sim/EEFloat.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXFreerideStatsTest, "SSX.Results.FreerideStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXFreerideStatsTest::RunTest(const FString& Parameters)
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
	Cfg.Mode = ERaceEventMode::PracticeFreeride;
	if (!TestTrue(TEXT("init"), Session.Init(Data, Cfg))) return false;
	Session.AddRacer(&Sim, true);
	Session.Start();
	SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim);
	if (!TestNotNull(TEXT("air context"), Air)) return false;
	SSXAir::FTrickScore& Sc = Air->Score;

	int32 Samples = 0, Placements = Session.GetPlacements();
	auto Step = [&]()
	{
		Session.Tick();
		if (Session.GetPlacements() != Placements) { Placements = Session.GetPlacements(); Samples = 0; }
		Sim.Step(FRiderInputWord());
		++Samples;
	};
	int32 Guard = 0;
	while (Session.GetPhase() != ERacePhase::Race && Guard++ < 60 * 120) Step();
	if (!TestTrue(TEXT("freeride running"), Session.GetPhase() == ERacePhase::Race)) return false;
	for (int32 F = 0; F < 600; ++F) Step();
	TestEqual(TEXT("+0xE0: one sample per boarder update since the reset"), int32(Sc.SpeedFrames), Samples);
	TestTrue(TEXT("+0xDC: the rider moved (km/h sum > 0)"), Sc.SpeedKmhSum > 0.f);
	if (!TestTrue(TEXT("no tricks / air / rails / wipe-outs yet (no input)"), Sc.ScoredTricks == 0 && Sc.WipeOutCount == 0
		&& Sc.AirSecondsTotal == 0.0 && Sc.HoldSecondsRace == 0.0 && Sc.RailSecondsTotal == 0.0))
	{
		return false;
	}

	// 0x156148, 0x1569B0, 0x156618, 0x156DE0
	Sc.GrabReachesHold(0x27A);
	for (int32 I = 0; I < 90; ++I) { Sc.TickHold(); }
	Sc.FinishTrick(0x27A, 0.0);
	Sc.UpdateAngles(-370.0, 400.0);
	Sc.AirFrames = 42;
	Sc.Land(false, 0);
	Sc.Value = 0.2;
	Sc.UpdateAngles(190.0, -10.0);
	Sc.AirFrames = 300;
	Sc.Land(false, 0);
	Sc.AirFrames = 30;
	Sc.Land(false, 0);
	Sc.Land(false, 1);
	for (int32 I = 0; I < 120; ++I) { Sc.RailFrame(1000.0); }
	Sc.StartSegment(false, 0, false);
	Sc.Value = 0.1;
	Sc.Bail();
	Step();
	const int32 Score = Sc.Score;
	const float SpeedSum = Sc.SpeedKmhSum, SpeedFrames = Sc.SpeedFrames;
	TestTrue(TEXT("scored"), Score > 0);

	const int32 FinishCs = int32(Session.GetClock() * 100.0);
	Sim.State.FinishResult = FinishCs;
	Session.SetPhase(ERacePhase::EndRace);
	Step();
	const TArray<FRaceResultRow>& Table = Session.GetFinalResults();
	if (!TestEqual(TEXT("one row"), Table.Num(), 1)) return false;
	const FRaceResultRow& Row = Table[0];
	auto Sec = [](int32 Frames) { return float(Frames) * 0.016666668f; };
	const float AirSum = SSXEE::FpuAdd(SSXEE::FpuAdd(Sec(42), Sec(300)), Sec(30));
	TestEqual(TEXT("row +0x00 spin degrees"), Row.SpinDeg, 560);
	TestEqual(TEXT("row +0x04 flip degrees"), Row.FlipDeg, 410);
	TestEqual(TEXT("row +0x08 grabs"), Row.Grabs, 1);
	TestEqual(TEXT("row +0x0C rails"), Row.Rails, 1);
	TestEqual(TEXT("row +0x10 tricks landed (scoring landings only)"), Row.Tricks, 2);
	TestEqual(TEXT("row +0x1C wipe-outs"), Row.Wipeouts, 1);
	TestEqual(TEXT("row +0x50 air time (single-precision sum)"), Row.AirTime, AirSum);
	TestEqual(TEXT("row +0x54 grab time"), Row.GrabTime, Sec(90));
	TestEqual(TEXT("row +0x58 rail time"), Row.RailTime, Sec(120));
	TestEqual(TEXT("row +0x5C biggest air"), Row.BiggestAir, Sec(300));
	TestEqual(TEXT("row +0x68 / +0x6C speed sum / frames copied at the end"), Row.SpeedKmhSum == SpeedSum && Row.SpeedFrames == SpeedFrames, true);
	TestEqual(TEXT("row +0x7C style points"), int32(Row.Score), Score);
	TestEqual(TEXT("DAT_0032E60C: the finish time"), Row.TimeCs, FinishCs);

	FSSXShowoffStats St;
	FSSXRaceUIBridge::ResultsStats(Row, St);
	TestTrue(TEXT("bridge showoff stats"), St.SpinDeg == 560 && St.FlipDeg == 410 && St.Grabs == 1 && St.Rails == 1);
	const float Avg = SpeedSum / SpeedFrames;
	FSSXLocStrings Loc;
	Loc.Load(SSXFrontEnd::LangDir());
	SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
	auto Cs = [](int32 C) { return FString::Printf(TEXT("%d:%02d.%02d"), C / 100 / 60, C / 100 % 60, C % 100); };
	for (int32 Units = 0; Units < 2; ++Units)
	{
		FSSXProfile P = FSSXProfile::MakeDefault();
		P.Slot[0].Char = 0;
		P.Slot[0].UserName = TEXT("JOE");
		P.Settings.Units = Units;
		FSSXCheats C;
		FSSXRaceUIHostStub H;
		H.Mode = 1;
		H.Stats = { St };
		FSSXOverlayManager O;
		O.Init(&P, &C, &Loc);
		O.SetHost(&H);
		O.Activate(OvRaceResults);
		FSSXFeAssets A;
		FSSXFeDrawList D(&A);
		O.Draw(D);
		auto Has = [&D](const FString& S) { return D.Texts.Contains(S); };
		const FString Speed = Units ? FString::Printf(TEXT("%d mph"), int32(Avg * 0.621f)) : FString::Printf(TEXT("%d km/h"), int32(Avg));
		TestTrue(FString::Printf(TEXT("[%s] style points %d"), Units ? TEXT("mph") : TEXT("km/h"), Score), Has(FString::FromInt(Score)));
		TestTrue(FString::Printf(TEXT("average speed '%s' (sum %.3f / %.0f frames)"), *Speed, SpeedSum, SpeedFrames), Has(Speed) && Avg > 1.f);
		TestTrue(TEXT("tricks landed 2"), Has(TEXT("2")));
		TestTrue(TEXT("wipeouts 1"), Has(TEXT("1")));
		TestTrue(TEXT("biggest air 0:05.00"), Has(TEXT("0:05.00")) && Cs(int32(Sec(300) * 100.f)) == TEXT("0:05.00"));
		TestTrue(TEXT("air time 0:06.20 (0.70 + 5.00 + 0.50)"), Has(TEXT("0:06.20")) && Cs(int32(AirSum * 100.f)) == TEXT("0:06.20"));
		TestTrue(TEXT("grab time 0:01.50"), Has(TEXT("0:01.50")));
		TestTrue(TEXT("rail time 0:02.00"), Has(TEXT("0:02.00")));
		TestTrue(FString::Printf(TEXT("total time %s"), *Cs(FinishCs)), Has(Cs(FinishCs)));
	}
	Session.DetachRacer(0);
	return true;
}

#endif
