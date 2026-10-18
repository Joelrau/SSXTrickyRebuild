// FUN_002673F8 / FUN_00267508 / FUN_00261B38 / 0x26AC98
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Sim/Anim/AflClips.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeRiders.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "World/SSXWorldPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXFeRiders;

	FLightSet MakeSet(const float Amb[4], const float Az[3], const float El[3], const float Col[3][4])
	{
		FLightSet L;
		for (int32 I = 0; I < 4; ++I) { L.Ambient[I] = Amb[I]; }
		for (int32 I = 0; I < 3; ++I)
		{
			L.Azimuth[I] = Az[I];
			L.Elevation[I] = El[I];
			for (int32 K = 0; K < 4; ++K) { L.Colour[I][K] = Col[I][K]; }
		}
		L.UpdateDirections();
		return L;
	}

	// FUN_002673F8 / FUN_00267508
	const float UAmb[4] = { 1.f, 0.25f, 0.25f, 0.25f }, UAz[3] = { 60.f, 120.f, 0.f }, UEl[3] = { 0.f, 0.f, 0.f };
	const float UCol[3][4] = { { 0.f, 1.25f, 1.25f, 1.25f }, { 0.f, 1.25f, 1.25f, 1.25f }, { 0.f, 0.f, 0.f, 0.f } };
	const float SAmb[4] = { 1.f, 0.1f, 0.1f, 0.1f }, SAz[3] = { 75.f, 345.f, 285.f }, SEl[3] = { 58.f, 20.f, -20.f };
	const float SCol[3][4] = { { 0.f, 10.f, 10.f, 10.f }, { 0.f, 2.1f, 1.9f, 1.7f }, { 0.f, 1.f, 1.f, 2.f } };

	FVector GameToUnreal(double X, double Y, double Z) { return FVector(X, -Y, Z); }

	struct FColourCase { double N[3]; double C[3]; };
	const FColourCase Sel[] = {
		{ { 0.0, 0.0, 1.0 }, { 1.992188, 1.992188, 1.992188 } },
		{ { 0.0, 0.0, -1.0 }, { 0.394429, 0.394429, 0.688857 } },
		{ { -0.5, -0.8660254, 0.0 }, { 1.992188, 1.992188, 1.992188 } },
		{ { 0.5, 0.8660254, 0.0 }, { 0.100000, 0.100000, 0.100000 } },
		{ { -0.8660472, 0.4999623, 0.0 }, { 1.496926, 1.451905, 1.992188 } },
		{ { -0.8571429, 0.2857143, -0.4285714 }, { 1.533177, 1.490808, 1.992188 } },
	};
	const FColourCase Unsel[] = {
		{ { 0.0, 0.0, 1.0 }, { 0.250000, 0.250000, 0.250000 } },
		{ { 0.0, 0.0, -1.0 }, { 0.362419, 0.362419, 0.362419 } },
		{ { -0.5, -0.8660254, 0.0 }, { 0.874932, 0.874932, 0.874932 } },
		{ { -0.8660472, 0.4999623, 0.0 }, { 0.250000, 0.250000, 0.250000 } },
		{ { 0.3005867, -0.5009779, 0.8115841 }, { 1.263157, 1.263157, 1.263157 } },
		{ { 0.8491858, -0.4902283, 0.1963662 }, { 1.992188, 1.992188, 1.992188 } },
	};
	const FColourCase Mid[] = {
		{ { 0.0, 0.0, -1.0 }, { 0.273068, 0.273068, 0.366136 } },
		{ { 0.5, 0.8660254, 0.0 }, { 1.642957, 1.577838, 1.826793 } },
		{ { -0.8660472, 0.4999623, 0.0 }, { 1.313560, 1.249310, 1.185059 } },
		{ { -0.8571429, 0.2857143, -0.4285714 }, { 0.835463, 0.798311, 0.761160 } },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXFeRiderLightingTest, "SSX.UI.FeRiderLighting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXFeRiderLightingTest::RunTest(const FString& Parameters)
{
	const FLightSet U = MakeSet(UAmb, UAz, UEl, UCol);
	const FLightSet S = MakeSet(SAmb, SAz, SEl, SCol);

	if (const FData* D = FData::Shared())
	{
		double MaxD = 0.0;
		for (const auto& P : { TPair<const FLightSet*, const FLightSet*>(&D->Unselected, &U), TPair<const FLightSet*, const FLightSet*>(&D->Selected, &S) })
		{
			for (int32 I = 0; I < 4; ++I) { MaxD = FMath::Max(MaxD, double(FMath::Abs(P.Key->Ambient[I] - P.Value->Ambient[I]))); }
			for (int32 I = 0; I < 3; ++I)
			{
				MaxD = FMath::Max(MaxD, double(FMath::Abs(P.Key->Azimuth[I] - P.Value->Azimuth[I])));
				MaxD = FMath::Max(MaxD, double(FMath::Abs(P.Key->Elevation[I] - P.Value->Elevation[I])));
				for (int32 K = 0; K < 4; ++K) { MaxD = FMath::Max(MaxD, double(FMath::Abs(P.Key->Colour[I][K] - P.Value->Colour[I][K]))); }
			}
		}
		AddInfo(FString::Printf(TEXT("fe_riders.json light sets vs FUN_002673F8 / FUN_00267508: max diff %.2e"), MaxD));
		TestTrue(TEXT("exported light sets = the code constants"), MaxD < 1e-6);
	}
	else
	{
		SSXTest::Skip(*this, TEXT("SimData/fe_riders.json missing: exported light sets not compared"));
	}

	{
		FSession Session;
		FRing Ring;
		int32 Chars[NumRiders];
		for (int32 K = 0; K < NumRiders; ++K) { Chars[K] = K; }
		Ring.Init(Chars, &Session);
		bool bStart = true;
		for (int32 K = 0; K < NumRiders; ++K) { bStart &= Ring.Lights[K].Blend == 1.f && !Ring.Lights[K].bSelected; }
		TestTrue(TEXT("FE load: every blend 1, no flag (riders start lit)"), bStart);
		Ring.Select(2);
		for (int32 F = 0; F < 16; ++F) { Ring.TickLights(); }
		bool bEnds = Ring.Lights[2].Blend == 1.f;
		for (int32 K = 0; K < NumRiders; ++K) { if (K != 2) { bEnds &= Ring.Lights[K].Blend == 0.f; } }
		TestTrue(TEXT("15-16 FE frames: the selected slot at 1, the others at 0"), bEnds);
		Ring.Select(3);
		for (int32 F = 0; F < 7; ++F) { Ring.TickLights(); }
		TestTrue(TEXT("7 frames after the change: slot 2 at 8/15, slot 3 at 7/15"),
			FMath::IsNearlyEqual(Ring.Lights[2].Blend, 8.f / 15.f, 1e-5f) && FMath::IsNearlyEqual(Ring.Lights[3].Blend, 7.f / 15.f, 1e-5f));
		AddInfo(FString::Printf(TEXT("blend: slot 2 %.6f, slot 3 %.6f after 7 frames"), Ring.Lights[2].Blend, Ring.Lights[3].Blend));
	}

	FSSXFeCamPose Pose;
	Pose.Pos = FVector(-649.5, 375.0, 100.0);
	Pose.RotDeg = FVector(87.02362823486328, 0.0, -119.99641418457031);
	FVector CamLoc;
	FRotator CamRot;
	Pose.ToUnreal(CamLoc, CamRot);
	{
		const FVector Fwd = FRotationMatrix(CamRot).GetUnitAxis(EAxis::X);
		const double DF = (Fwd - GameToUnreal(0.8648884, -0.4992714, -0.0519241)).GetAbsMax();
		TestTrue(TEXT("port camera forward = the game's view matrix z axis"), DF < 1e-5);
		const double RefSel[3][3] = { { 0.4122710, -0.3963555, 0.8203261 }, { -0.6487569, -0.6735472, 0.3541873 }, { -0.9220086, 0.2514200, -0.2944286 } };
		const double RefUns[3][3] = { { 0.4990424, -0.8654101, -0.0449676 }, { 0.9989882, 0.0006466, -0.0449676 }, { -0.4999458, -0.8660567, 0.0 } };
		const USSXWorldPresentation::FRiderLightInputs InS = USSXWorldPresentation::FeRiderLightInputs(S, CamRot);
		const USSXWorldPresentation::FRiderLightInputs InU = USSXWorldPresentation::FeRiderLightInputs(U, CamRot);
		double MaxD = 0.0;
		for (int32 I = 0; I < 3; ++I)
		{
			MaxD = FMath::Max(MaxD, (InS.Dir[I] - GameToUnreal(RefSel[I][0], RefSel[I][1], RefSel[I][2])).GetAbsMax());
			MaxD = FMath::Max(MaxD, (InU.Dir[I] - GameToUnreal(RefUns[I][0], RefUns[I][1], RefUns[I][2])).GetAbsMax());
		}
		AddInfo(FString::Printf(TEXT("light directions (Psymon select camera): max diff %.2e vs fe_riders.camera_to_world; selected fill (light 2) "
			"toward the camera %.3f, unselected lights toward the camera %.3f / %.3f (back lights)"), MaxD,
			-FVector::DotProduct(InS.Dir[2], Fwd), -FVector::DotProduct(InU.Dir[0], Fwd), -FVector::DotProduct(InU.Dir[1], Fwd)));
		TestTrue(TEXT("light directions = camera_to_world"), MaxD < 1e-5);
		TestTrue(TEXT("colour channels: floats 1..3 = r, g, b (selected light 2 blue 2.0)"), InS.Colour[2].Equals(FVector(1.0, 1.0, 2.0), 1e-6));
		TestTrue(TEXT("ambient = floats 1..3"), InS.Ambient.Equals(FVector(0.1, 0.1, 0.1), 1e-6) && InU.Ambient.Equals(FVector(0.25, 0.25, 0.25), 1e-6));
	}

	{
		double MaxD = 0.0;
		auto Run = [&](const FLightSet& L, const FColourCase* Cases, int32 N) {
			const USSXWorldPresentation::FRiderLightInputs In = USSXWorldPresentation::FeRiderLightInputs(L, CamRot);
			for (int32 K = 0; K < N; ++K)
			{
				const FVector Nrm = GameToUnreal(Cases[K].N[0], Cases[K].N[1], Cases[K].N[2]).GetSafeNormal();
				const FVector C = USSXWorldPresentation::RiderVertexColour(In, Nrm);
				MaxD = FMath::Max(MaxD, (C - FVector(Cases[K].C[0], Cases[K].C[1], Cases[K].C[2])).GetAbsMax());
			}
		};
		Run(S, Sel, UE_ARRAY_COUNT(Sel));
		Run(U, Unsel, UE_ARRAY_COUNT(Unsel));
		Run(FLightSet::Lerp(U, S, 7.f / 15.f), Mid, UE_ARRAY_COUNT(Mid));
		AddInfo(FString::Printf(TEXT("vertex colours (%d normals, blends 1 / 0 / 7/15): max diff %.2e vs fe_riders.vertex_colour (1.0 = 0x80)"),
			int32(UE_ARRAY_COUNT(Sel) + UE_ARRAY_COUNT(Unsel) + UE_ARRAY_COUNT(Mid)), MaxD));
		TestTrue(TEXT("per-vertex colour = the rider VU program"), MaxD < 2e-5);
	}

	// FUN_00261B38
	static bool bTables = SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
	(void)bTables;
	{
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FE.Init(&Profile, &Cheats, nullptr, false);
		bool bAll = FE.CarouselWindow == INDEX_NONE;
		for (int32 K = 0; K < NumRiders; ++K) { bAll &= FE.IsCarouselRiderDrawn(K) && FE.CarouselPlatformFrame[K] == 0 && FE.CarouselSignFrame[K] == 0; }
		TestTrue(TEXT("FE load: all 12 riders drawn (+0x14 = 1), every sign / platform at frame 0"), bAll);
		FE.CarouselSlot[0] = FE.CarouselSlot[1] = 4;
		FE.SetCarouselWindow(0, 4);
		FE.SetCarouselWindow(0, 5);
		const bool bDrawn[NumRiders] = { false, false, false, true, true, true, true, true, false, false, false, false };
		bool bOk = true;
		for (int32 K = 0; K < NumRiders; ++K)
		{
			bOk &= FE.IsCarouselRiderDrawn(K) == bDrawn[K] && FE.Riders.Lights[K].bSelected == (K == 5);
			bOk &= FE.CarouselSignFrame[K] == (K == 5 ? 1 : 0) && FE.CarouselPlatformFrame[K] == (K == 5 ? 1 : 0);
		}
		TestTrue(TEXT("window 4 -> 5: riders 3..7 drawn, flag on 5, sign 5 / platform 5 lit, sign 4 back to frame 0"), bOk);
		FE.SetCarouselWindow(1, 4);
		TestTrue(TEXT("pad 2 window: signs 4 and 5 lit, only platform 4"), FE.CarouselSignFrame[4] == 1 && FE.CarouselSignFrame[5] == 1
			&& FE.CarouselPlatformFrame[4] == 1 && FE.CarouselPlatformFrame[5] == 0);
		if (SSXFeTables::PlatformInstance(0) != INDEX_NONE)
		{
			TestEqual(TEXT("platform of slot 0 = mdl_Cplatform_01 (instance 268)"), SSXFeTables::PlatformInstance(0), 268);
			TestEqual(TEXT("platform of slot 2 = mdl_Cplatform_03 (instance 269)"), SSXFeTables::PlatformInstance(2), 269);
			TestEqual(TEXT("sign of slot 2 = mdl_csPsymon_0 (instance 257)"), SSXFeTables::RiderSignInstance(2), 257);
		}
		else
		{
			SSXTest::Skip(*this, TEXT("fe_tables.json without rider_signs / platforms (tools/export_fe_tables.py): instance checks skipped"));
		}
	}

	{
		FSSXFeCameraSet Set;
		const bool bCams = Set.Load(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json")));
		if (!bCams)
		{
			SSXTest::Skip(*this, TEXT("fe_cameras.json missing: swing checks skipped"));
			return true;
		}
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FE.Init(&Profile, &Cheats, nullptr, false);
		FE.Camera.CameraSet = &Set;
		FE.CarouselSlot[0] = 7;
		auto Press = [&FE](ESSXFeKey K) { for (int32 N = 0; N < 3000 && !FE.IsReady(); ++N) { FE.Tick(); } FE.Key(0, K); };
		Press(ESSXFeKey::Cross);
		for (int32 N = 0; N < 3000 && (N == 0 || !FE.IsReady()); ++N) { FE.Tick(); }
		Press(ESSXFeKey::Cross);

		const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
		FPoser Poser;
		FRider& R = FE.Riders.Riders[4];
		const bool bPose = Lib && Poser.Init(Lib, Lib->Skeleton(TEXT("eddie")), SSXFeTables::CarouselRider(4));
		FSession Session;
		Session.OnFeLoad();

		int32 SwingFrames = 0, DrawnOk = 0, Advanced = 0;
		double LastFrame = -1.0;
		for (int32 N = 0; N < 3000 && FE.Current() != ESSXFeScreen::CharacterSelect; ++N)
		{
			FE.Tick();
			if (bPose) { Poser.Step(R, Session); }
			FE.Riders.TickLights();
			const bool bSwing = FE.Current() == ESSXFeScreen::ModeSelect && FE.Camera.Current.EndsWith(TEXT("_charSelect")) && FE.Camera.IsRunning();
			if (!bSwing) { continue; }
			++SwingFrames;
			bool bD = FE.CarouselWindow == 4 && FE.Riders.Lights[4].bSelected && FE.CarouselPlatformFrame[4] == 1;
			for (int32 K = 0; K < NumRiders; ++K) { bD &= FE.IsCarouselRiderDrawn(K) == (K >= 2 && K <= 6); }
			DrawnOk += bD ? 1 : 0;
			if (bPose)
			{
				const SSXAnim::FPlayInstance* I = Poser.GetController().Newest(0);
				const double Fr = I ? I->Subs[0].Frame : -1.0;
				Advanced += (I && Fr != LastFrame) ? 1 : 0;
				LastFrame = Fr;
			}
		}
		AddInfo(FString::Printf(TEXT("swing %s: %d frames; window 4 (riders 2..6 drawn, flag 4, platform 4 lit) on %d of them; "
			"selected blend %.3f at the end%s"), FE.Camera.History.Num() ? *FE.Camera.History.Last() : TEXT("-"), SwingFrames, DrawnOk, FE.Riders.Lights[4].Blend,
			bPose ? *FString::Printf(TEXT("; Eddie's clip frame advanced on %d swing frames"), Advanced) : TEXT(" (no anim_clips.bin: clock not checked)")));
		TestTrue(TEXT("the swing runs while ModeSelect waits (phase 4)"), SwingFrames > 30);
		TestEqual(TEXT("riders 2..6 drawn and slot 4 selected on every swing frame (FUN_00261B38 before request 6)"), DrawnOk, SwingFrames);
		TestEqual(TEXT("FUN_00263270: both pads back on slot 4"), FE.CarouselSlot[0], 4);
		TestEqual(TEXT("CharacterSelect opens on slot 4"), FE.ViewSlot(), 4);
		if (bPose)
		{
			TestTrue(TEXT("Eddie animates during the swing (clip frame advances every swing frame)"), Advanced >= SwingFrames - 1);
		}
	}
	return true;
}

#endif
