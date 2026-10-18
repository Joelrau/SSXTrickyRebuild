// 0x123FC8, 0x124920, 0x156DE0, 0x11B018, 0x11E988
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Harness/RiderSetup.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/Air/AirStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRiderCrashScoringTest, "SSX.Race.RiderCrashScoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXRiderCrashScoringTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("gari race / sim data not available (%s): skipped"), *Err));
		return true;
	}
	struct FOut { int32 WipeA = 0, WipeB = 0, CrashA = 0, CrashB = 0, KdA = 0, KdB = 0; bool bHitA = false, bHitB = false; double MeterA = 0, MeterB = 0, MeterA0 = 0, MeterB0 = 0; };
	auto Run = [&](ERaceEventMode Mode, double SpeedA, double SpeedB, double BoostB, bool bBWipeOut) -> FOut
	{
		FOut O;
		FRiderSim A, B;
		SSXRiderSetup::RegisterStates(A, true);
		SSXRiderSetup::RegisterStates(B, true);
		A.Profile.MassByte = B.Profile.MassByte = 128;   // 0x11D7B0
		A.SetWorld(&World);
		B.SetWorld(&World);
		FRaceSession Session;
		FRaceConfig Cfg;
		Cfg.Mode = Mode;
		if (!Session.Init(Data, Cfg)) { AddError(TEXT("init")); return O; }
		Session.AddRacer(&A, true);
		Session.AddRacer(&B, false);
		Session.Start();
		int32 Guard = 0;
		while (Session.GetPhase() != ERacePhase::Race && Guard++ < 60 * 120)
		{
			Session.Tick();
			A.Step(FRiderInputWord());
			B.Step(FRiderInputWord());
		}
		for (int32 W = 0; W < 60 * 20 && !(A.State.Motion == ERiderMotion::Ground && B.State.Motion == ERiderMotion::Ground); ++W)
		{
			Session.Tick();
			A.Step(FRiderInputWord());
			B.Step(FRiderInputWord());
		}
		SSXAir::FAirContext* Aa = SSXAir::GetAirContext(A);
		SSXAir::FAirContext* Ab = SSXAir::GetAirContext(B);
		if (!Aa || !Ab || Session.GetPhase() != ERacePhase::Race) { AddError(FString::Printf(TEXT("no race: air %d %d phase %d"), Aa != nullptr, Ab != nullptr, int32(Session.GetPhase()))); return O; }
		const FVector Fwd = A.State.BasisForward;
		const FVector P0 = A.State.Position;
		B.State.BoostStrength = BoostB;
		if (bBWipeOut) { B.State.Motion = ERiderMotion::WipeOut; }
		A.State.BoostMeter = B.State.BoostMeter = 0.5;
		O.MeterA0 = A.State.BoostMeter;
		O.MeterB0 = B.State.BoostMeter;
		for (int32 F = 0; F < 12 && Aa->Score.WipeOutCount + Ab->Score.WipeOutCount + Aa->Score.CrashCount + Ab->Score.CrashCount + Aa->Score.KnockdownCount + Ab->Score.KnockdownCount == 0; ++F)
		{
			A.State.Position = P0;
			B.State.Position = P0 + Fwd * 40.0;
			A.State.Velocity = Fwd * SpeedA;
			B.State.Velocity = Fwd * SpeedB;
			Session.Tick();
			A.Step(FRiderInputWord());
			B.Step(FRiderInputWord());
		}
		O.WipeA = Aa->Score.WipeOutCount; O.WipeB = Ab->Score.WipeOutCount;
		O.CrashA = Aa->Score.CrashCount; O.CrashB = Ab->Score.CrashCount;
		O.KdA = Aa->Score.KnockdownCount; O.KdB = Ab->Score.KnockdownCount;
		O.MeterA = A.State.BoostMeter; O.MeterB = B.State.BoostMeter;
		Session.DetachRacer(1);
		Session.DetachRacer(0);
		return O;
	};
	const FOut D = Run(ERaceEventMode::PracticeFreeride, 850.0, -850.0, 0.0, false);
	TestTrue(TEXT("double knock-down: both riders count a wipe-out (0x156DE0 without the crash flag), no crash"), D.WipeA == 1 && D.WipeB == 1 && D.CrashA == 0 && D.CrashB == 0);
	TestTrue(TEXT("double knock-down: no knock-down stat"), D.KdA == 0 && D.KdB == 0);
	TestTrue(TEXT("double knock-down: each pays the bail meter (-0.1) through the boost add"), D.MeterA < D.MeterA0 && D.MeterB < D.MeterB0);
	const FOut S = Run(ERaceEventMode::PracticeFreeride, 1000.0, -1000.0, 1.0, false);
	TestTrue(TEXT("single: the victim's bail 0x11E988 (wipe-out count, DAT_0032DE8C != 0)"), S.WipeA == 1 && S.CrashA == 0 && S.WipeB == 0 && S.CrashB == 0);
	TestTrue(TEXT("single: the attacker gets the knock-down stat +0xBC"), S.KdB == 1 && S.KdA == 0);
	TestTrue(TEXT("single: the victim's bail meter is not paid"), FMath::IsNearlyEqual(S.MeterA, S.MeterA0, 1e-6) || S.MeterA > S.MeterA0 - 0.05);
	const FOut M0 = Run(ERaceEventMode::PracticeKnockdown, 450.0, -450.0, 1.0, false);
	TestTrue(TEXT("single in mode 0: the victim's bail carries the crash flag (crash count +0xC0)"), M0.CrashA == 1 && M0.WipeA == 0 && M0.KdB == 1);
	return true;
}

#endif
