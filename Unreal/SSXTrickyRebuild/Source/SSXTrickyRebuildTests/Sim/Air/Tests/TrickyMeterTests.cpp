// 0x11B018, 0x11B1F8, 0x11D038, 0x156618, 0x156DE0, 0x155AA8, 0x1284E0
#include "Misc/AutomationTest.h"
#include "Sim/Air/AirMath.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/Ground/GroundControls.h"
#include "Sim/Ground/GroundPhysics.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrickyMeterTest, "SSX.Air.TrickyMeter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXTrickyMeterTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;

	// 0x2191C0
	{
		FRiderState S;
		S.BoostMeter = 0.8;
		BoostAdd(S, 0.3, 1.0);
		TestEqual(TEXT("0.8 + 0.3: near-full check"), S.MeterNearFullCount, 1);
		TestEqual(TEXT("0.8 + 0.3: Tricky opens"), S.TrickyTimer, TrickyWindowSeconds);
		TestEqual(TEXT("fresh activation steps the hint"), S.UberHintIndex, 1);
		S.BoostMeter = 0.9;
		BoostAdd(S, 0.3, 1.0);
		TestEqual(TEXT("0.9 + 0.3: no near-full check"), S.MeterNearFullCount, 1);
		TestEqual(TEXT("re-fill while Tricky runs: hint not stepped"), S.UberHintIndex, 1);
		S.BoostMeter = 0.5;
		BoostAdd(S, 0.3, 1.0);
		TestEqual(TEXT("0.5 + 0.3: no check"), S.MeterNearFullCount, 1);
	}

	{
		FTrickScore T;
		T.PointsMult = 1.3;
		T.Value = 0.5;
		TestEqual(TEXT("bail meter 0xBDCCC9F3"), T.Bail(), double(-0.09999456f));
		TestEqual(TEXT("bail: M_pts = 1"), T.PointsMult, 1.0);
		TestEqual(TEXT("bail without the crash flag counts a wipe-out (+0x90)"), T.WipeOutCount, 1);
		T.PointsMult = 1.3;
		TestEqual(TEXT("respawn meter 0xBDF5B343"), T.Respawn(), double(-0.11997082f));
		TestEqual(TEXT("respawn: M_pts = 1"), T.PointsMult, 1.0);
		TestEqual(TEXT("respawn count +0x8C"), T.RespawnCount, 1);
	}

	{
		FTrickScore T;
		T.StartSegment(false, 0, false, true);
		TestTrue(TEXT("pipe-wall take-off sets +0x14"), T.bAltFlag);
		T.LateTrick(false);
		TestFalse(TEXT("late split clears +0x14"), T.bAltFlag);
	}

	{
		bool bWall = true;
		const FVector N(0.0, -FMath::Sin(FMath::DegreesToRadians(60.0)), FMath::Cos(FMath::DegreesToRadians(60.0)));
		const FVector F = FVector(0.0, FMath::Cos(FMath::DegreesToRadians(60.0)), FMath::Sin(FMath::DegreesToRadians(60.0)));
		SSXGround::LaunchVelocity(FVector(0, 500, 800), N, F, N, 630.88, false, &bWall);
		TestTrue(TEXT("wall launch"), bWall);
		SSXGround::LaunchVelocity(FVector(0, 500, -100), N, F, N, 630.88, false, &bWall);
		TestFalse(TEXT("moving down: normal launch"), bWall);
		SSXGround::LaunchVelocity(FVector(0, 500, 800), N, F, N, 630.88, true, &bWall);
		TestFalse(TEXT("rail launch"), bWall);
	}

	{
		FRiderSim Sim;
		FRiderState& S = Sim.State;
		S.BoostMeter = 0.5;
		SSXGround::BoostHandler(Sim, true, true);
		TestEqual(TEXT("mid tier strength"), S.BoostStrength, double(0.60135353f));
		TestEqual(TEXT("press edge: pose target (mid)"), S.BoostPose.Target, double(0.55031186f));
		TestEqual(TEXT("press edge: pose rate"), S.BoostPose.Rate, double(2.1442268f));
		TestEqual(TEXT("press edge: flash armed"), S.BoostFlash.Cur, 1.0);
		S.BoostMeter = 0.2;
		SSXGround::BoostHandler(Sim, true, false);
		TestEqual(TEXT("low tier strength"), S.BoostStrength, double(0.2500284f));
		S.BoostMeter = 0.0005;
		SSXGround::BoostHandler(Sim, true, false);
		TestTrue(TEXT("drain is not clamped"), FMath::IsNearlyEqual(S.BoostMeter, 0.0005 - 0.0007501507, 1e-12));
		SSXGround::BoostHandler(Sim, true, false);
		TestEqual(TEXT("negative meter: boost off"), S.BoostStrength, 0.0);
		TestEqual(TEXT("boost off: pose back to 0"), S.BoostPose.Target, 0.0);
		TestEqual(TEXT("boost off: pose rate 1.5258708"), S.BoostPose.Rate, double(1.5258708f));
	}
	return true;
}

#endif
