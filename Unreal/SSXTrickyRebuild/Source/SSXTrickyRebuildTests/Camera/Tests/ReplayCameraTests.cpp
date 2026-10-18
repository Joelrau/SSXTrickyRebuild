// 0x3368B0, 0x1785C0, 0x178A28, 0x178BE8, 0x178680, 0x1786D0, FUN_00171610, FUN_00171500
#include "Misc/AutomationTest.h"
#include "Camera/ReplayCamera.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags RcTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FString Chase(double Dist, double Elev, double Yaw, double Fov, int32 StaticCut = 0)
	{
		return FString::Printf(TEXT("{\"pos_attach_rider\":1,\"orbit_enable\":1,\"orbit_dist\":%f,\"yaw_from_heading\":1,"
			"\"elev_from_velocity\":1,\"elev_base\":%f,\"yaw_offset\":%f,\"elev_div_descending\":2,\"elev_div_ascending\":1,"
			"\"orbit_lag_enable\":1,\"orbit_lag_div\":6,\"tgt_attach_rider\":1,\"tgt_off_z\":50,\"fov\":%f,\"angle_smooth_div\":4,"
			"\"roll_div\":4,\"near_clip\":30,\"static_cut\":%d}"), Dist, Elev, Yaw, Fov, StaticCut);
	}

	void MakeDefs(FSSXCameraDefSet& Defs, const FString& Regions = FString())
	{
		const FString J = FString::Printf(TEXT("{\"cameras\":{\"chase reverse\":%s,\"chase board\":%s,\"chase eyes\":%s,\"chase near\":%s,"
			"\"chase far\":%s,\"chase over\":%s,\"replay start\":%s,\"replay cam\":%s,"
			"\"fly1\":{\"base_x\":1200,\"base_y\":-400,\"base_z\":300,\"tgt_attach_rider\":1,\"fov\":1.4,\"angle_smooth_div\":1,\"near_clip\":20}},"
			"\"scripts\":{\"S1\":[{\"opcode\":0,\"name\":\"fly1\",\"t0\":0,\"t1\":20}],\"S2\":[{\"opcode\":0,\"name\":\"fly1\",\"t0\":0,\"t1\":20}]},"
			"\"regions\":{%s}}"),
			*Chase(350, 0.6, 3.14, 1.75), *Chase(150, -0.5, 0, 1.2), *Chase(1000, -0.85, 0, 2.05), *Chase(180, -0.5, 0, 1.65),
			*Chase(500, -0.7, 0, 1.65), *Chase(1000, -0.85, 0, 1.65), *Chase(400, -0.3, 0.5, 1.25, 1), *Chase(500, -0.45, 0, 1.65),
			*Regions);
		Defs.AddJson(J);
	}

	FSSXCameraRiderInput Rider(const FVector& P, const FVector& V = FVector(0, 1500, -200))
	{
		FSSXCameraRiderInput R;
		R.Anchor = P;
		R.Velocity = V;
		R.Forward = V.GetSafeNormal();
		R.Yaw = FMath::Atan2(V.Y, V.X);
		R.Motion = 2;
		R.Control = 3;
		return R;
	}

	FString Sphere(const TCHAR* Name, const FVector& C, double Radius, const TCHAR* Script, int32 InRace, int32 InReplay, const TCHAR* Extra = TEXT(""))
	{
		return FString::Printf(TEXT("\"%s\":{\"centre_x\":%f,\"centre_y\":%f,\"centre_z\":%f,\"radius\":%f,\"rider_slot\":0,\"on_enter\":1,"
			"\"script_on_enter\":\"%s\",\"active_in_race\":%d,\"active_in_replay\":%d%s}"), Name, C.X, C.Y, C.Z, Radius, Script, InRace, InReplay, Extra);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayCameraModesTest, "SSX.Camera.ReplayModes", RcTestFlags)
bool FSSXReplayCameraModesTest::RunTest(const FString& Parameters)
{
	FSSXCameraDefSet Defs;
	MakeDefs(Defs);
	FSSXReplayCamera RC(&Defs);
	int32 PlayerCuts = 0;
	RC.NumRacers = []() { return 3; };
	RC.CutPlayerCamera = [&]() { ++PlayerCuts; };
	float A, B, C, D;
	int32 T;

	// 0x1785C0, 0x178A28, 0x1786D0
	RC.Activate(true, 5);
	RC.Get(A, B, C, D, T);
	TestTrue(TEXT("active, mode 7, target 0"), RC.IsActive() && RC.GetMode() == 7 && T == 0 && RC.GetCam().bReplay && RC.GetCam().GetMode() == 7);
	TestTrue(TEXT("mode 7 copies \"replay start\" into \"replay cam\""), FMath::IsNearlyEqual(A, 0.5f) && FMath::IsNearlyEqual(B, -0.3f) && C == 400.f && FMath::IsNearlyEqual(D, 1.25f));
	const FSSXCameraDef* Rc = Defs.Find(TEXT("replay cam"));
	TestTrue(TEXT("the copy keeps the name and static_cut (+0x170)"), Rc && Rc->Name == TEXT("replay cam") && Rc->StaticCut == 0);
	RC.Activate(true, 2);
	RC.Get(A, B, C, D, T);
	TestEqual(TEXT("rider 2 of 3 kept"), T, 2);

	RC.Set(0.25f, 2.f, 50.f, 1.f, 1, false);
	RC.Get(A, B, C, D, T);
	TestTrue(TEXT("B <= 1, C >= 200, D x C >= 300 (D = 1.5)"), A == 0.25f && B == 1.f && C == 200.f && D == 1.5f && T == 1);
	RC.Set(-7.f, -3.f, 5000.f, 0.1f, 0, false);
	RC.Get(A, B, C, D, T);
	TestTrue(TEXT("B >= -1, C <= 1200, D raised to 0.25 then >= 0.5; A unclamped"), A == -7.f && B == -1.f && C == 1200.f && D == 0.5f);
	RC.Set(0.f, 0.f, 600.f, 3.f, 0, false);
	RC.Get(A, B, C, D, T);
	TestEqual(TEXT("D <= 1.9"), D, 1.9f);

	// 0x178BE8
	RC.Set(0.f, 0.f, 600.f, 1.f, 0, false);
	RC.Nudge(1.f, 1);
	RC.Get(A, B, C, D, T);
	TestEqual(TEXT("op 1: A + 2 deg"), A, 0.034906585f);
	RC.Nudge(1.f, 2);
	RC.Get(A, B, C, D, T);
	TestEqual(TEXT("op 2: B - 2 deg"), B, -0.034906585f);
	RC.Nudge(1.f, 9);
	RC.Get(A, B, C, D, T);
	TestEqual(TEXT("op 9: C + 5"), C, 605.f);
	RC.Nudge(1.f, 6);
	RC.Get(A, B, C, D, T);
	TestEqual(TEXT("op 6: D - 1/180"), D, 1.f - 0.0055555557f);
	PlayerCuts = 0;
	RC.Nudge(1.f, 5);
	RC.Get(A, B, C, D, T);
	TestTrue(TEXT("op 5 from 0: target wraps to 2, player camera 0 cut"), T == 2 && PlayerCuts == 1);
	RC.Nudge(1.f, 4);
	RC.Get(A, B, C, D, T);
	TestEqual(TEXT("op 4 from 2: wraps to 0"), T, 0);

	// 0x178680
	RC.SetMode(7);
	TArray<int32> Seq;
	for (int32 I = 0; I < 7; ++I) { RC.NextMode(); Seq.Add(RC.GetMode()); }
	TestTrue(TEXT("mode cycle 8, 1, 2, 4, 5, 6, 7"), Seq == TArray<int32>({ 8, 1, 2, 4, 5, 6, 7 }));
	RC.SetMode(0);
	TestEqual(TEXT("0x1786D0(0) -> 7"), RC.GetMode(), 7);
	RC.SetMode(9);
	TestEqual(TEXT("0x1786D0(9) -> 7"), RC.GetMode(), 7);
	RC.SetMode(4);
	RC.Get(A, B, C, D, T);
	TestTrue(TEXT("mode 4 copies \"chase near\""), C == 180.f && FMath::IsNearlyEqual(B, -0.5f) && FMath::IsNearlyEqual(D, 1.65f));

	// 0x383040
	bool bTable = true;
	for (int32 M = 0; M <= 11; ++M)
	{
		const FString Want = (M >= 1 && M <= 6) || M == 8 ? TEXT("replay cam") : (M == 10 ? TEXT("manual target") : TEXT("chase far"));
		bTable &= Want == SSXCamera::ReplayModeDefinition(M);
	}
	TestTrue(TEXT("replay definitions: 1..6 / 8 replay cam, 7 / 9 / 0 / 11 chase far, 10 manual target"), bTable);

	for (const int32 M : { 4, 7 })
	{
		FSSXCameraDefSet D2;
		MakeDefs(D2);
		FSSXReplayCamera Rp(&D2);
		Rp.Activate(true, 0);
		Rp.SetMode(M);
		FSSXRaceCamera Race(&D2);
		Race.SetMode(M == 7 ? 5 : 4);
		double MaxErr = 0.0;
		for (int32 F = 0; F < 90; ++F)
		{
			const FSSXCameraRiderInput In = Rider(FVector(F * 3.0, F * 25.0, -F * 4.0), FVector(180, 1500, -240));
			Rp.GetCam().Step(In);
			Race.Step(In);
			MaxErr = FMath::Max(MaxErr, (Rp.GetCam().GetPosition() - Race.GetPosition()).Size());
			MaxErr = FMath::Max(MaxErr, FMath::Abs(Rp.GetCam().GetFov() - Race.GetFov()) * 1000.0);
		}
		AddInfo(FString::Printf(TEXT("replay mode %d vs race mode %d: max difference %.6f"), M, M == 7 ? 5 : 4, MaxErr));
		TestTrue(FString::Printf(TEXT("replay mode %d evaluates like the race chase camera"), M), MaxErr < 1e-6);
	}
	RC.Activate(false, 0);
	TestTrue(TEXT("0x1785C0(0): inactive, target 0"), !RC.IsActive() && RC.GetTarget() == 0 && !RC.GetCam().bReplay);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraRegionsTest, "SSX.Camera.Regions", RcTestFlags)
bool FSSXCameraRegionsTest::RunTest(const FString& Parameters)
{
	// FUN_00171610, FUN_00171500
	{
		FSSXCameraDefSet Defs;
		MakeDefs(Defs);
		FSSXRaceCamera Cam(&Defs);
		FSSXCameraRegion Cap;
		Cap.Centre = FVector(0, 5000, 0);
		Cap.Radius = 200.0;
		Cap.Capsule = 1;
		Cap.CapsuleLen = 2000.0;
		const auto At = [&](const FVector& P, const FSSXCameraRegion& G) { Cam.Step(Rider(P)); return Cam.RegionInside(G, false); };
		TestTrue(TEXT("capsule yaw 0: on the axis 800 off the centre"), At(FVector(800, 5000, 0), Cap));
		TestTrue(TEXT("capsule: 150 off the axis"), At(FVector(0, 5150, 0), Cap));
		TestFalse(TEXT("capsule: past the box (|dx| 1100 > max(r, len / 2))"), At(FVector(1100, 5000, 0), Cap));
		TestFalse(TEXT("capsule: 250 off the axis"), At(FVector(0, 5250, 0), Cap));
		Cap.CapsuleYaw = UE_HALF_PI;
		TestTrue(TEXT("capsule yaw 90: axis (0, -1, 0): 800 along y"), At(FVector(0, 5800, 0), Cap));
		TestFalse(TEXT("capsule yaw 90: 800 along x is off the axis"), At(FVector(800, 5000, 0), Cap));
		Cap.CapsuleYaw = 0.0;
		Cap.CapsulePitch = UE_HALF_PI;
		TestTrue(TEXT("capsule pitch 90: axis (0, 0, 1)"), At(FVector(0, 5000, 900), Cap));
		FSSXCameraRegion Sph;
		Sph.Centre = FVector(1000, 0, 0);
		Sph.Radius = 300.0;
		TestTrue(TEXT("sphere: inside"), At(FVector(1000, 200, 100), Sph));
		TestFalse(TEXT("sphere: in the box, out of the ball"), At(FVector(1250, 250, 0), Sph));
		Sph.NeedWipeout = 1;
		FSSXCameraRiderInput W = Rider(FVector(1000, 0, 0));
		Cam.Step(W);
		TestFalse(TEXT("need_wipeout: not wiping out"), Cam.RegionInside(Sph, false));
		W.Motion = 5;
		Cam.Step(W);
		TestTrue(TEXT("need_wipeout: motion 5"), Cam.RegionInside(Sph, false));
		Sph.NeedWipeout = 0;
		Sph.NeedBigAir = 1;
		Sph.MinAirDist = 1000.0;
		FSSXCameraRiderInput Air = Rider(FVector(1000, 0, 0));
		Air.PredTimeLeft = 0.5;
		Air.PredLandPoint = FVector(1000, 2000, -500);
		Cam.Step(Air);
		TestTrue(TEXT("need_big_air: 0.5 s left, landing 2062 away"), Cam.RegionInside(Sph, false));
		Air.PredTimeLeft = 0.3;
		Cam.Step(Air);
		TestFalse(TEXT("need_big_air: 0.3 s left"), Cam.RegionInside(Sph, false));
		Sph.NeedBigAir = 0;
		Cam.Step(Rider(FVector(0, 0, 0)));
		Cam.Step(Rider(FVector(1000, 0, 0)));
		TestTrue(TEXT("the previous table (+0x828) keeps the last frame's anchor"), Cam.RegionInside(Sph, false) && !Cam.RegionInside(Sph, true));
	}

	const FString Regions = Sphere(TEXT("R1"), FVector(1000, 0, 0), 300.0, TEXT("S1"), 0, 1) + TEXT(",")
		+ Sphere(TEXT("R2"), FVector(-3000, 0, 0), 300.0, TEXT("S2"), 1, 0) + TEXT(",")
		+ Sphere(TEXT("R3"), FVector(0, 4000, 0), 300.0, TEXT("S2"), 0, 1, TEXT(",\"only_unfinished\":1"));
	FSSXCameraDefSet Defs;
	MakeDefs(Defs, Regions);
	TestEqual(TEXT("regions parsed (one file)"), Defs.GetRegionFiles().Num() == 1 ? Defs.GetRegionFiles()[0].Num() : -1, 3);
	FSSXReplayCamera RC(&Defs);
	RC.Activate(true, 0);
	FSSXRaceCamera& Cam = RC.GetCam();
	Cam.Step(Rider(FVector(0, 0, 0)));
	TestFalse(TEXT("outside: no script"), Cam.IsScriptRunning());
	Cam.Step(Rider(FVector(1000, 100, 0)));
	TestTrue(TEXT("mode 7: entering R1 plays S1 (on_enter)"), Cam.IsScriptRunning() && Cam.GetScriptName() == TEXT("S1"));
	TestEqual(TEXT("the script cut to the fly-by camera"), Cam.GetEvaluatedName(), FString(TEXT("fly1")));
	bool bFrozen = true;
	Cam.ReplayFrozen = [&]() { return bFrozen; };
	for (int32 I = 0; I < 3; ++I) Cam.Step(Rider(FVector(1000, 150, 0)));
	const double Yaw0 = Cam.GetYaw();
	for (int32 I = 0; I < 5; ++I) Cam.Step(Rider(FVector(1000, 400 + I * 300, 0)));
	TestEqual(TEXT("frozen replay: the script camera holds"), Cam.GetYaw(), Yaw0);
	bFrozen = false;
	Cam.Step(Rider(FVector(1000, 2000, 0)));
	TestTrue(TEXT("unfrozen: the fly-by camera follows the rider again"), Cam.GetYaw() != Yaw0);
	RC.SetMode(8);
	TestFalse(TEXT("0x1786D0 resets the director"), Cam.IsScriptRunning());
	Cam.Step(Rider(FVector(0, 0, 0)));
	Cam.Step(Rider(FVector(1000, 0, 0)));
	TestFalse(TEXT("mode 8: replay regions do not run"), Cam.IsScriptRunning());
	RC.SetMode(7);
	Cam.Step(Rider(FVector(0, 0, 0)));
	FSSXCameraRiderInput Fin = Rider(FVector(0, 4000, 0));
	Cam.Step(Fin);
	TestFalse(TEXT("only-finished region: unfinished rider skipped"), Cam.IsScriptRunning());
	Cam.Step(Rider(FVector(0, 0, 0)));
	Fin.FinishResult = 2;
	Cam.Step(Fin);
	TestTrue(TEXT("only-finished region: finished rider plays it"), Cam.IsScriptRunning() && Cam.GetScriptName() == TEXT("S2"));
	// the race camera: replay-only regions never run; race regions do
	FSSXRaceCamera Race(&Defs);
	Race.Step(Rider(FVector(0, 0, 0)));
	Race.Step(Rider(FVector(1000, 0, 0)));
	TestFalse(TEXT("race: replay-only R1 skipped"), Race.IsScriptRunning());
	Race.Step(Rider(FVector(-3000, 0, 0)));
	TestTrue(TEXT("race: active_in_race R2 plays S2"), Race.IsScriptRunning() && Race.GetScriptName() == TEXT("S2"));
	return true;
}

// 0x1786D0, 0x175EB8, 0x231318
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayCameraNarrationTest, "SSX.Camera.ReplayNarrationStop", RcTestFlags)
bool FSSXReplayCameraNarrationTest::RunTest(const FString& Parameters)
{
	FSSXCameraDefSet Defs;
	MakeDefs(Defs);
	FSSXReplayCamera RC(&Defs);
	int32 Stops = 0;
	RC.NumRacers = []() { return 2; };
	RC.OnNarrationStop = [&Stops]() { ++Stops; };
	RC.SetMode(4);
	TestEqual(TEXT("no script: no narration stop"), Stops, 0);
	TestTrue(TEXT("a director script runs"), RC.GetCam().PlayScript(TEXT("S1")) && RC.GetCam().IsDirectorOn());
	RC.SetMode(5);
	TestEqual(TEXT("mode change with the script on: the narration stops once"), Stops, 1);
	TestFalse(TEXT("the director is reset"), RC.GetCam().IsDirectorOn());
	RC.NextMode();
	TestEqual(TEXT("no script any more: no further stop"), Stops, 1);
	return true;
}

#endif
