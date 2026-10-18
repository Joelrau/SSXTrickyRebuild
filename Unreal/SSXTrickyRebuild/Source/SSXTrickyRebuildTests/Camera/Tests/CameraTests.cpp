// FUN_00174d28, FUN_00174888
#include "Camera/RaceCamera.h"
#include "Particles/SSXLensFX.h"
#include "World/SSXWorldLighting.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CameraTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraBasicsTest, "SSX.Camera.Basics", CameraTestFlags)
bool FSSXCameraBasicsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("wrap 3pi"), SSXCamera::Wrap(3.0 * UE_DOUBLE_PI), UE_DOUBLE_PI, 1e-12);
	TestEqual(TEXT("wrap -pi -> pi"), SSXCamera::Wrap(-UE_DOUBLE_PI), UE_DOUBLE_PI, 1e-12);
	TestEqual(TEXT("wrap 0.5"), SSXCamera::Wrap(0.5 + 4.0 * UE_DOUBLE_PI), 0.5, 1e-12);
	for (int32 K = 0; K < 4; ++K)
	{
		TestEqual(FString::Printf(TEXT("ease %d (0)"), K), SSXCamera::Ease(K, 0.0), 0.0, 1e-12);
		TestEqual(FString::Printf(TEXT("ease %d (1)"), K), SSXCamera::Ease(K, 1.0), 1.0, 1e-12);
	}
	TestEqual(TEXT("smoothstep(0.5)"), SSXCamera::Ease(2, 0.5), 0.5, 1e-12);
	TestEqual(TEXT("mode 4"), FString(SSXCamera::ModeDefinition(4)), FString(TEXT("chase near")));
	TestEqual(TEXT("mode 7 fallback"), FString(SSXCamera::ModeDefinition(7)), FString(TEXT("chase far")));

	FSSXRaceCamera Cam;
	TestEqual(TEXT("default view"), Cam.GetViewIndex(), 1);
	TestEqual(TEXT("default mode"), Cam.GetMode(), 4);
	const int32 Expected[4] = { 2, 4, 5, 6 };
	for (int32 I = 0; I < 4; ++I)
	{
		Cam.CycleView(+1);
		TestEqual(TEXT("cycled mode"), Cam.GetMode(), Expected[(2 + I) % 4]);
	}
	Cam.CycleView(-1);
	TestEqual(TEXT("cycle back wraps"), Cam.GetViewIndex(), 0);

	FVector F, U;
	FSSXRaceCamera::ViewAxes(1.0, -0.4, 0.3, F, U);
	TestEqual(TEXT("forward unit"), F.Size(), 1.0, 1e-9);
	TestEqual(TEXT("up unit"), U.Size(), 1.0, 1e-9);
	TestEqual(TEXT("up perpendicular"), FVector::DotProduct(F, U), 0.0, 1e-9);
	FSSXRaceCamera::ViewAxes(1.0, -0.4, 0.0, F, U);
	TestTrue(TEXT("no roll: up has no sideways part"), FMath::Abs(FVector::DotProduct(FVector::CrossProduct(F, FVector(0, 0, 1)), U)) < 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraReferenceTest, "SSX.Camera.Reference", CameraTestFlags)
bool FSSXCameraReferenceTest::RunTest(const FString& Parameters)
{
	const FString Dir = FSSXCameraDefSet::DefaultDir();
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Dir, TEXT("camera_reference.csv"))))
	{
		AddError(TEXT("SimData/cameras/camera_reference.csv missing: run tools/export_cameras.py and tools/make_camera_reference.py"));
		return false;
	}
	TUniquePtr<FSSXCameraDefSet> Defs;
	TUniquePtr<FSSXRaceCamera> Cam;
	FString Seq;
	double WorldZ = 0.0;
	bool bWorld = false;
	int32 Rows = 0, Bad = 0;
	double MaxPos = 0.0, MaxAng = 0.0;
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty() || Line.StartsWith(TEXT("# ")))
		{
			continue;
		}
		TArray<FString> F;
		Line.ParseIntoArray(F, TEXT(","), false);
		if (Line.StartsWith(TEXT("#seq")))
		{
			Seq = F[1];
			TArray<FString> Files;
			F[2].ParseIntoArray(Files, TEXT("|"));
			Defs = MakeUnique<FSSXCameraDefSet>();
			if (!Defs->LoadFiles(Files, Dir) || !Defs->Find(TEXT("chase near")))
			{
				AddError(FString::Printf(TEXT("camera JSON missing for %s: run tools/export_cameras.py"), *F[2]));
				return false;
			}
			if (F.Num() > 3 && !F[3].IsEmpty() && !Defs->LoadStageAreas(F[3], Dir))
			{
				AddError(FString::Printf(TEXT("stage areas missing for %s: run tools/export_cameras.py"), *F[3]));
				return false;
			}
			Cam = MakeUnique<FSSXRaceCamera>(Defs.Get());
			Cam->LineQuery = [&WorldZ, &bWorld](const FVector& A, const FVector& B, FVector& Pt, FVector& N)
			{
				if (!bWorld)
				{
					return false;
				}
				const double Da = A.Z - WorldZ, Db = B.Z - WorldZ;
				if ((Da > 0) == (Db > 0) || Da == Db)
				{
					return false;
				}
				Pt = A + (B - A) * (Da / (Da - Db));
				N = FVector(0, 0, 1);
				return true;
			};
			continue;
		}
		if (!Cam || F.Num() < 44)
		{
			AddError(TEXT("malformed reference row"));
			return false;
		}
		auto D = [&F](int32 I) { return FCString::Atod(*F[I]); };
		auto Vec = [&D](int32 I) { return FVector(D(I), D(I + 1), D(I + 2)); };
		bWorld = F[5] != TEXT("none");
		WorldZ = bWorld ? D(5) : 0.0;
		const int32 View = FCString::Atoi(*F[3]);
		if (View >= 0) { Cam->SetViewIndex(View); }
		if (FCString::Atoi(*F[2]) != 0) { Cam->MarkCut(); }
		if (F[4] != TEXT("-") && !Cam->PlayScript(F[4]))
		{
			AddError(FString::Printf(TEXT("script %s not found"), *F[4]));
			return false;
		}
		FSSXCameraRiderInput R;
		R.Velocity = Vec(6);
		R.Forward = Vec(9);
		R.Yaw = D(12);
		R.Anchor = Vec(13);
		R.BoostStrength = D(16);
		R.Motion = FCString::Atoi(*F[17]);
		R.Control = FCString::Atoi(*F[18]);
		R.GroundNormal = Vec(19);
		R.Surface = FCString::Atoi(*F[22]);
		R.PredTimeLeft = D(23);
		R.PredLandVelocity = Vec(24);
		R.PredLandNormal = Vec(27);
		Cam->Step(R);
		++Rows;

		const double* C = Cam->GetC();
		const double* V = Cam->GetV();
		double ErrPos = 0.0, ErrAng = 0.0;
		for (int32 I = 0; I < 3; ++I)
		{
			ErrPos = FMath::Max(ErrPos, FMath::Abs(C[I] - D(30 + I)));
			ErrPos = FMath::Max(ErrPos, FMath::Abs(V[I] - D(38 + I)));
		}
		for (int32 I = 3; I <= 5; ++I)
		{
			ErrAng = FMath::Max(ErrAng, FMath::Abs(SSXCamera::Wrap(C[I] - D(30 + I))));
		}
		ErrAng = FMath::Max(ErrAng, FMath::Abs(C[6] - D(36)));
		ErrAng = FMath::Max(ErrAng, FMath::Abs(SSXCamera::Wrap(V[19] - D(41))));
		ErrAng = FMath::Max(ErrAng, FMath::Abs(V[20] - D(42)));
		const double ErrNear = FMath::Abs(C[7] - D(37));
		const double ErrBlend = FMath::Abs(Cam->GetWipeBlend() - D(43));
		MaxPos = FMath::Max(MaxPos, ErrPos);
		MaxAng = FMath::Max(MaxAng, ErrAng);
		if (ErrPos > 1e-3 || ErrAng > 1e-6 || ErrNear > 1e-6 || ErrBlend > 1e-7)
		{
			if (++Bad <= 5)
			{
				AddError(FString::Printf(TEXT("%s frame %s: pos err %.3g, angle err %.3g, near err %.3g, blend err %.3g"),
					*Seq, *F[1], ErrPos, ErrAng, ErrNear, ErrBlend));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d frames, max position error %.3g cm, max angle error %.3g rad, %d bad"), Rows, MaxPos, MaxAng, Bad));
	TestTrue(TEXT("reference rows"), Rows > 1000);
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraBlendStageYawTest, "SSX.Camera.BlendStageYaw", CameraTestFlags)
bool FSSXCameraBlendStageYawTest::RunTest(const FString& Parameters)
{
	FSSXCameraDefSet Defs;
	if (!Defs.LoadForTrack(TEXT("aloha")))
	{
		AddError(TEXT("aloha camera JSON / stage areas missing: run tools/export_cameras.py"));
		return false;
	}
	const FSSXCameraDef* From = Defs.Find(TEXT("VEN_Est1"));
	const FSSXCameraDef* To = Defs.Find(TEXT("VEN_Est1a"));
	if (!From || !To || From->StageArea != 1 || To->StageArea != 1 || From->TgtAttachRider || To->TgtAttachRider)
	{
		AddError(TEXT("VEN_Est1 / VEN_Est1a: expected fixed-angle stage area 1 cameras"));
		return false;
	}
	FVector PF = From->Base, PT = To->Base;
	double YawF = 0.0, YawT = 0.0;
	if (!Defs.StageAreaTransform(From->StageArea, PF, YawF) || !Defs.StageAreaTransform(To->StageArea, PT, YawT))
	{
		AddError(TEXT("aloha start area matrix missing"));
		return false;
	}
	TestEqual(TEXT("Aloha start area turned -90 deg"), YawF, -UE_DOUBLE_HALF_PI, 1e-4);
	Defs.AddJson(TEXT("{\"scripts\":{")
		TEXT("\"T_CutStage\":[{\"opcode\":0,\"name\":\"VEN_Est1\",\"t0\":0.0,\"t1\":1.0}],")
		TEXT("\"T_BlendStage\":[{\"opcode\":0,\"name\":\"VEN_Est1\",\"t0\":0.0,\"t1\":0.0},")
		TEXT("{\"opcode\":1,\"name\":\"VEN_Est1a\",\"a\":0,\"t0\":0.0,\"t1\":2.5}]}}"));
	const FSSXCameraRiderInput R;

	// FUN_00174c98, FUN_00174888
	{
		FSSXRaceCamera Cam(&Defs);
		TestTrue(TEXT("T_CutStage"), Cam.PlayScript(TEXT("T_CutStage")));
		double ErrYaw = 0.0, ErrPos = 0.0;
		for (int32 K = 0; K < 30; ++K)
		{
			Cam.Step(R);
			ErrYaw = FMath::Max(ErrYaw, FMath::Abs(SSXCamera::Wrap(Cam.GetYaw() - From->TgtYaw)));
			ErrPos = FMath::Max(ErrPos, (Cam.GetPosition() - PF).Size());
		}
		AddInfo(FString::Printf(TEXT("CUT_TO VEN_Est1: yaw err %.3g (no stage yaw), position err %.3g"), ErrYaw, ErrPos));
		TestTrue(TEXT("CUT_TO: look yaw = tgt_yaw (no stage yaw), base transformed"), ErrYaw < 1e-9 && ErrPos < 1e-6);
	}

	// 0x174D28
	{
		FSSXRaceCamera Cam(&Defs);
		TestTrue(TEXT("T_BlendStage"), Cam.PlayScript(TEXT("T_BlendStage")));
		const double YF = From->TgtYaw + YawF, YT = To->TgtYaw + YawT;
		double Clock = 0.0, ErrYaw = 0.0, ErrPos = 0.0, ErrFov = 0.0, MaxStep = 0.0, PrevYaw = 0.0, FirstErr = 1.0;
		int32 Frames = 0;
		bool bDone = false;
		for (int32 K = 0; K < 200 && !bDone; ++K)
		{
			Clock += SSXCamera::Dt;
			Cam.Step(R);
			const double U = FMath::Min(Clock / 2.5, 1.0);
			const double WantYaw = YF + SSXCamera::Wrap(YT - YF) * U;
			const FVector WantPos = PF + (PT - PF) * U;
			const double* V = Cam.GetV();
			const double E = FMath::Abs(SSXCamera::Wrap(Cam.GetYaw() - WantYaw));
			if (K == 0)
			{
				FirstErr = E;
			}
			else
			{
				MaxStep = FMath::Max(MaxStep, FMath::Abs(SSXCamera::Wrap(Cam.GetYaw() - PrevYaw)));
			}
			PrevYaw = Cam.GetYaw();
			ErrYaw = FMath::Max(ErrYaw, E);
			ErrPos = FMath::Max(ErrPos, (FVector(V[0], V[1], V[2]) - WantPos).Size());
			ErrFov = FMath::Max(ErrFov, FMath::Abs(V[6] - (From->Fov + (To->Fov - From->Fov) * U)));
			bDone = Cam.GetEvaluatedName() == To->Name;
			++Frames;
		}
		AddInfo(FString::Printf(TEXT("BLEND_TO VEN_Est1 -> VEN_Est1a on Aloha: %d frames, first-frame yaw err %.3g, max yaw err %.3g, ")
			TEXT("max per-frame yaw step %.4f rad, V position err %.3g, fov err %.3g"), Frames, FirstErr, ErrYaw, MaxStep, ErrPos, ErrFov));
		TestTrue(TEXT("first frame already carries the stage yaw (no swing at the blend start)"), FirstErr < 1e-9);
		TestTrue(TEXT("yaw follows the decoded lerp every frame"), ErrYaw < 1e-9);
		TestTrue(TEXT("no swing: per-frame yaw step <= the linear rate"), MaxStep < FMath::Abs(SSXCamera::Wrap(YT - YF)) / 150.0 + 1e-6);
		TestTrue(TEXT("V position / fov = lerp of the transformed bases / fovs"), ErrPos < 1e-6 && ErrFov < 1e-9);
		TestTrue(TEXT("blend ends on the target (name copied at u >= 1)"), bDone && Frames >= 150 && Frames <= 151);
		TestEqual(TEXT("final yaw = target tgt_yaw + stage yaw"), SSXCamera::Wrap(Cam.GetYaw() - YT), 0.0, 1e-9);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraProjectionTest, "SSX.Camera.Projection", CameraTestFlags)
bool FSSXCameraProjectionTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("camera_projection.csv"));
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		AddError(FString::Printf(TEXT("%s missing (run tools/make_camera_projection_reference.py)"), *Path));
		return false;
	}
	TMap<FString, uint32> W;
	for (int32 I = 1; I < Lines.Num(); ++I)
	{
		FString Name, Hex;
		if (Lines[I].Split(TEXT(","), &Name, &Hex))
		{
			W.Add(Name, uint32(FCString::Strtoui64(*Hex, nullptr, 16)));
		}
	}
	auto F = [&W](const TCHAR* Name) { const uint32 U = W.FindRef(Name); float V; FMemory::Memcpy(&V, &U, 4); return V; };
	auto Bits = [](float V) { uint32 U; FMemory::Memcpy(&U, &V, 4); return U; };
	const float Half = F(TEXT("half_fov")), Near = F(TEXT("near"));
	TestEqual(TEXT("RAM slot: chase near half-FOV 0.825, near 15"), Half == 0.825f && Near == 15.f, true);
	const SSXCamera::FProjection P = SSXCamera::Projection(Half, Near, SSXCamera::DefaultFar);
	const struct { const TCHAR* Name; float Port; } Cells[] = {
		{ TEXT("m00"), P.M00 }, { TEXT("m11"), P.M11 }, { TEXT("m22"), P.M22 }, { TEXT("m23"), P.M23 }, { TEXT("m32"), P.M32 } };
	int32 Exact = 0, Zero = 0, OneUlp = 0;
	FString Report;
	for (const auto& C : Cells)
	{
		const uint32 Ram = W.FindRef(C.Name), Port = Bits(C.Port);
		const int64 Ulp = FMath::Abs(int64(Ram) - int64(Port));
		Exact += Ulp == 0 ? 1 : 0;
		OneUlp += Ulp == 1 ? 1 : 0;
		Report += FString::Printf(TEXT(" %s %08X/%08X"), C.Name, Port, Ram);
	}
	for (int32 R = 0; R < 4; ++R)
	{
		for (int32 Col = 0; Col < 4; ++Col)
		{
			const FString N = FString::Printf(TEXT("m%d%d"), R, Col);
			if (N != TEXT("m00") && N != TEXT("m11") && N != TEXT("m22") && N != TEXT("m23") && N != TEXT("m32"))
			{
				Zero += W.FindRef(N) == 0 ? 1 : 0;
			}
		}
	}
	const FString Msg = FString::Printf(TEXT("projection vs RAM 0x75CFC0 (port/RAM words):%s; %d of 5 non-zero words bit-exact, %d one ulp off, %d of 11 zero words zero; horizontal FOV %.2f deg, vertical at 4:3 %.2f deg"),
		*Report, Exact, OneUlp, Zero, FMath::RadiansToDegrees(2.0 * Half), FMath::RadiansToDegrees(SSXCamera::VerticalFov43(Half)));
	AddInfo(Msg);
	UE_LOG(LogTemp, Display, TEXT("[camera] %s"), *Msg);
	// TODO: m11 is 1 ulp below the RAM word (EE / VU0 rounding of the multiply)
	TestTrue(TEXT("m00 (horizontal cot), m22, m23, m32 bit-exact"), Bits(P.M00) == W.FindRef(TEXT("m00")) && Bits(P.M22) == W.FindRef(TEXT("m22"))
		&& Bits(P.M23) == W.FindRef(TEXT("m23")) && Bits(P.M32) == W.FindRef(TEXT("m32")));
	TestTrue(TEXT("m11 = m00 * 4/3 within 1 ulp"), FMath::Abs(int64(Bits(P.M11)) - int64(W.FindRef(TEXT("m11")))) <= 1);
	TestEqual(TEXT("zero words"), Zero, 11);

	return !HasAnyErrors();
}

// FUN_00174B38, FUN_00174C48, 0x1773C0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraFlashTest, "SSX.Camera.Flash", CameraTestFlags)
bool FSSXCameraFlashTest::RunTest(const FString& Parameters)
{
	static const float Curve[] = { 0.699999988f, 0.740419269f, 0.773045301f, 0.798263907f, 0.816460907f, 0.828022122f,
		0.833333373f, 0.832780302f, 0.826748967f, 0.815625072f, 0.799794197f, 0.779642463f, 0.75555563f, 0.727919221f,
		0.697119415f, 0.663541675f, 0.62757206f, 0.589596212f, 0.550000072f, 0.5091694f, 0.467489958f, 0.425347447f,
		0.383127838f, 0.341216862f, 0.30000037f, 0.259864032f, 0.22119388f, 0.184375554f, 0.149794668f, 0.117837369f,
		0.0888894498f, 0.0633363277f, 0.041563917f, 0.0239586011f, 0.010905385f, 0.00279072532f, 0.f };
	constexpr int32 N = UE_ARRAY_COUNT(Curve);
	TestEqual(TEXT("hermite(0.25)"), SSXCamera::Hermite(0.7f, 0.f, 1.6f, 0.f, 0.25f), 0.815624952f, 1e-6f);
	SSXCamera::FFlash F;
	F.Start();
	for (int32 K = 0; K < N + 3; ++K)
	{
		const float V = F.Update();
		TestEqual(FString::Printf(TEXT("curve %d"), K), V, K < N ? Curve[K] : 0.f, 1e-6f);
	}
	TestFalse(TEXT("37 updates then inactive"), F.bActive);
	F.Clear();
	F.Force();
	for (int32 K = 0; K < 12; ++K)
	{
		TestEqual(FString::Printf(TEXT("hold %d"), K), F.Update(), K < 10 ? 1.f : 0.f);
	}
	F.Clear();
	F.Start();
	F.Force();
	for (int32 K = 0; K < 16; ++K)
	{
		TestEqual(FString::Printf(TEXT("hold over curve %d"), K), F.Update(), K < 10 ? 1.f : Curve[K], 1e-6f);
	}

	FSSXCameraDefSet Defs;
	Defs.AddJson(TEXT("{\"cameras\":{\"chase near\":{\"tgt_yaw\":0.5,\"fov\":1.0,\"angle_smooth_div\":0.0,\"near_clip\":15.0},")
		TEXT("\"T_Fix\":{\"base_z\":300.0,\"tgt_yaw\":1.0,\"fov\":0.8,\"angle_smooth_div\":0.0}},")
		TEXT("\"scripts\":{\"S_Flash\":[{\"opcode\":23,\"name\":\"\",\"t0\":0.0,\"t1\":0.0},")
		TEXT("{\"opcode\":0,\"name\":\"T_Fix\",\"t0\":0.0,\"t1\":5.0}]}}"));
	FSSXCameraRiderInput In;
	{
		FSSXRaceCamera Cam(&Defs);
		TestTrue(TEXT("S_Flash"), Cam.PlayScript(TEXT("S_Flash")));
		Cam.Step(In);
		TestEqual(TEXT("op 0x17 -> 0.7"), Cam.GetRenderFlash(), double(Curve[0]), 1e-6);
		Cam.Step(In);
		TestEqual(TEXT("op 0x17 second frame"), Cam.GetRenderFlash(), double(Curve[1]), 1e-6);
	}
	{
		FSSXRaceCamera Cam(&Defs);
		TestEqual(TEXT("no fade at start"), Cam.GetRenderFlash(), 0.0);
		Cam.ForceFlash();
		TestEqual(TEXT("the call writes nothing"), Cam.GetRenderFlash(), 0.0);
		int32 Faded = 0;
		for (int32 K = 0; K < 20; ++K)
		{
			Cam.Step(In);
			Faded += Cam.GetRenderFlash() >= 1.0 ? 1 : 0;
			if (K == 10) { TestEqual(TEXT("11th update clear"), Cam.GetRenderFlash(), 0.0); }
		}
		TestEqual(TEXT("10 faded updates"), Faded, 10);
	}
	{
		FSSXRaceCamera Cam(&Defs);
		Cam.Step(In);
		Cam.Step(In);
		bool bFrozen = true;
		Cam.ReplayFrozen = [&bFrozen]() { return bFrozen; };
		FSSXRaceCamera::StartFlash();
		Cam.Step(In);
		TestEqual(TEXT("frozen: two updates in one frame"), Cam.GetRenderFlash(), double(Curve[1]), 1e-6);
	}
	SSXCamera::SharedFlash().Clear();
	return !HasAnyErrors();
}

// 0x1C72E8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraFarPlaneTest, "SSX.Camera.FarPlane", CameraTestFlags)
bool FSSXCameraFarPlaneTest::RunTest(const FString& Parameters)
{
	struct FRow { const TCHAR* Track; float D1, D2; };
	static const FRow Rows[] = { { TEXT("Aloha"), 17437.76172f, 15000.58496f }, { TEXT("Pipedream"), 49548.51953f, 20001.83789f },
		{ TEXT("Mesablanca"), 26030.57422f, 18043.13477f }, { TEXT("Alaska"), 40000.f, 30000.f } };
	for (const FRow& R : Rows)
	{
		TestEqual(FString::Printf(TEXT("%s race 1P"), R.Track), SSXCamera::ProjectionFar(R.D1, R.D2, 1, 4, 10), R.D1);
		TestEqual(FString::Printf(TEXT("%s race 2P"), R.Track), SSXCamera::ProjectionFar(R.D1, R.D2, 2, 4, 10), R.D2);
		TestEqual(FString::Printf(TEXT("%s pre-race"), R.Track), SSXCamera::ProjectionFar(R.D1, R.D2, 1, 1, 10), FMath::Max(R.D1, 30000.f));
		TestEqual(FString::Printf(TEXT("%s replay 2P"), R.Track), SSXCamera::ProjectionFar(R.D1, R.D2, 2, 4, 3), FMath::Max(R.D1, 30000.f));
		FSSXTrackPresentation T;
		T.DrawDist1P = R.D1;
		T.DrawDist2P = R.D2;
		TestEqual(FString::Printf(TEXT("%s lens far"), R.Track), SSXLensFX::FParams::From(T).Far, double(R.D1), 1e-3);
	}
	TestEqual(TEXT("cell radius Aloha"), SSXCamera::TerrainCellRadius(17437.76172f), 2);
	TestEqual(TEXT("cell radius Pipedream"), SSXCamera::TerrainCellRadius(49548.51953f), 5);
	TestEqual(TEXT("cell radius 30000"), SSXCamera::TerrainCellRadius(30000.f), 3);
	const SSXCamera::FProjection P = SSXCamera::Projection(0.825f, 15.f, 49548.51953f);
	TestEqual(TEXT("m22"), P.M22, 1.0006057f, 1e-7f);
	TestEqual(TEXT("m32"), P.M32, -30.0090847f, 1e-4f);
	return !HasAnyErrors();
}

// 0x16E900
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraBoneAnchorTest, "SSX.Camera.BoneAnchor", CameraTestFlags)
bool FSSXCameraBoneAnchorTest::RunTest(const FString& Parameters)
{
	FSSXCameraDefSet Defs;
	Defs.AddJson(TEXT("{\"cameras\":{")
		TEXT("\"T_Board\":{\"pos_attach_rider\":1,\"anchor_use_bone\":1,\"bone_index\":-1,\"orbit_enable\":1,\"orbit_dist\":200.0,")
		TEXT("\"tgt_yaw\":0.5,\"fov\":0.8,\"angle_smooth_div\":0.0},")
		TEXT("\"T_Contact\":{\"pos_attach_rider\":1,\"bone_index\":-2,\"orbit_enable\":1,\"orbit_dist\":200.0,")
		TEXT("\"tgt_yaw\":0.5,\"fov\":0.8,\"angle_smooth_div\":0.0}},")
		TEXT("\"scripts\":{\"S_Board\":[{\"opcode\":0,\"name\":\"T_Board\",\"t0\":0.0,\"t1\":5.0}],")
		TEXT("\"S_Contact\":[{\"opcode\":0,\"name\":\"T_Contact\",\"t0\":0.0,\"t1\":5.0}]}}"));
	FSSXCameraRiderInput In;
	In.Anchor = FVector(10.0, 0.0, 70.0);
	In.Contact = FVector(12.0, 1.0, 0.0);
	In.BoardPos = FVector(11.0, 2.0, 5.0);
	In.bHasBoard = true;
	const FVector Offset(0.0, -200.0, 0.0);
	for (int32 Case = 0; Case < 2; ++Case)
	{
		FSSXRaceCamera Cam(&Defs);
		FVector FirstA = FVector(-1.0);
		Cam.LineQuery = [&FirstA](const FVector& A, const FVector&, FVector&, FVector&)
		{
			if (FirstA == FVector(-1.0)) { FirstA = A; }
			return false;
		};
		Cam.PlayScript(Case == 0 ? TEXT("S_Board") : TEXT("S_Contact"));
		Cam.Step(In);
		const FVector WantP = (Case == 0 ? In.BoardPos : In.Contact) + Offset;
		const FVector WantA = Case == 0 ? In.Anchor : In.Contact;
		TestTrue(FString::Printf(TEXT("case %d position"), Case), Cam.GetPosition().Equals(WantP, 1e-6));
		TestTrue(FString::Printf(TEXT("case %d collision anchor"), Case), FirstA.Equals(WantA, 1e-9));
	}
	return !HasAnyErrors();
}

// FUN_00173c50, FUN_00174010, FUN_0016e900, FUN_001722e8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraRiderSlotsTest, "SSX.Camera.RiderSlots", CameraTestFlags)
bool FSSXCameraRiderSlotsTest::RunTest(const FString& Parameters)
{
	FSSXCameraDefSet Defs;
	Defs.AddJson(TEXT("{\"cameras\":{")
		TEXT("\"T_Pair\":{\"pos_attach_rider\":1,\"pos_rider_slot\":17,\"anchor_use_bone\":1,\"bone_index\":4,\"orbit_enable\":1,")
		TEXT("\"orbit_dist\":200.0,\"orbit_dist_boost\":30.0,\"yaw_from_heading\":1,\"yaw_offset\":0.5,\"elev_base\":0.1,")
		TEXT("\"tgt_attach_rider\":1,\"tgt_rider_slot\":0,\"tgt_use_bone\":1,\"tgt_bone_index\":4,\"tgt_yaw\":0.02,")
		TEXT("\"tgt_pitch\":-0.01,\"fov\":0.2,\"angle_smooth_div\":0.0,\"roll_div\":4.0,\"near_clip\":80.0,\"static_cut\":1},")
		TEXT("\"T_Stage\":{\"stage_area\":2,\"base_x\":100.0,\"base_y\":50.0,\"base_z\":300.0,\"tgt_yaw\":1.0,\"tgt_pitch\":-0.1,")
		TEXT("\"fov\":0.8,\"angle_smooth_div\":0.0}},")
		TEXT("\"scripts\":{\"S_Pair\":[{\"opcode\":0,\"name\":\"T_Pair\",\"t0\":0.0,\"t1\":5.0}],")
		TEXT("\"S_Stage\":[{\"opcode\":0,\"name\":\"T_Stage\",\"t0\":0.0,\"t1\":5.0}]}}"));
	FMatrix M = FMatrix::Identity;
	M.M[0][0] = 0.0; M.M[0][1] = 1.0; M.M[1][0] = -1.0; M.M[1][1] = 0.0;
	M.M[3][0] = 5000.0; M.M[3][1] = -2000.0; M.M[3][2] = 700.0;
	Defs.SetStageArea(2, M);

	TArray<FSSXCameraRiderInput> In;
	const int32 Chars[3] = { 3, 5, 7 };
	const double Yaws[3] = { 0.3, -1.2, 2.0 };
	for (int32 I = 0; I < 3; ++I)
	{
		FSSXCameraRiderInput& R = In.AddDefaulted_GetRef();
		R.Character = Chars[I];
		R.Yaw = Yaws[I];
		R.Anchor = FVector(1000.0 * I, 300.0, 50.0);
		for (int32 B = 0; B < 5; ++B) { R.Bones.Add(R.Anchor + FVector(0.0, 0.0, 15.0 * B)); }
	}
	auto Wire = [&In](FSSXRaceCamera& Cam)
	{
		Cam.NumRacers = []() { return 3; };
		Cam.OwnRacer = []() { return 1; };
		Cam.RacerInput = [&In](int32 I, FSSXCameraRiderInput& Out) { Out = In[I]; return true; };
	};
	{
		FSSXRaceCamera Cam(&Defs);
		Wire(Cam);
		TestTrue(TEXT("S_Pair"), Cam.PlayScript(TEXT("S_Pair")));
		Cam.Step(In[1]);
		TestEqual(TEXT("slot 0 = own racer"), Cam.GetSlotRacer(0), 1);
		TestEqual(TEXT("slot 1 = first other racer"), Cam.GetSlotRacer(1), 0);
		TestEqual(TEXT("slot 2 = second other racer"), Cam.GetSlotRacer(2), 2);
		TestEqual(TEXT("slot 3 empty"), Cam.GetSlotRacer(3), -1);
		TestEqual(TEXT("slot 10 empty"), Cam.GetSlotRacer(10), -1);
		TestEqual(TEXT("slot 11 = racer before"), Cam.GetSlotRacer(11), 0);
		TestEqual(TEXT("slot 12 = racer after"), Cam.GetSlotRacer(12), 2);
		TestEqual(TEXT("slot 13 empty"), Cam.GetSlotRacer(13), -1);
		TestEqual(TEXT("slot 14 + 3 = Mac"), Cam.GetSlotRacer(17), 0);
		TestEqual(TEXT("slot 14 + 5 = player"), Cam.GetSlotRacer(19), 1);
		TestEqual(TEXT("slot 14 + 7"), Cam.GetSlotRacer(21), 2);
		TestEqual(TEXT("slot 14 + 0 empty"), Cam.GetSlotRacer(14), -1);
		const double Y = 0.5 + 0.3, E = 0.1, Dist = 200.0;
		const FVector P = In[0].Bones[4] + FVector(Dist * FMath::Sin(Y) * FMath::Cos(E), -Dist * FMath::Cos(Y) * FMath::Cos(E), -Dist * FMath::Sin(E));
		const FVector L = In[1].Bones[4];
		const double WantYaw = FMath::Atan2(L.Y - P.Y, L.X - P.X) + 0.02;
		const double WantPitch = FMath::Atan2(L.Z - P.Z, FVector::Dist2D(L, P)) - 0.01;
		TestTrue(TEXT("position at the rival's bone 4 + orbit"), Cam.GetPosition().Equals(P, 1e-6));
		TestEqual(TEXT("look yaw at the player's bone 4"), SSXCamera::Wrap(Cam.GetYaw() - WantYaw), 0.0, 1e-9);
		TestEqual(TEXT("look pitch at the player's bone 4"), Cam.GetPitch(), WantPitch, 1e-9);
		TestEqual(TEXT("definition FOV"), Cam.GetFov(), 0.2, 1e-12);
		for (int32 K = 0; K < 4; ++K) { Cam.Step(In[1]); }
		const FVector Held = Cam.GetPosition();
		In[0].Bones[4].X += 100.0;
		In[0].Anchor.X += 100.0;
		for (int32 K = 0; K < 5; ++K) { Cam.Step(In[1]); }
		TestTrue(TEXT("static cut: view frozen"), Cam.GetPosition().Equals(Held, 1e-9));
		In[0].Bones[4].X -= 100.0;
		In[0].Anchor.X -= 100.0;
	}
	{
		FSSXRaceCamera Cam(&Defs);
		Cam.PlayScript(TEXT("S_Pair"));
		Cam.Step(In[1]);
		TestEqual(TEXT("no list: slot 0 = Step rider"), Cam.GetSlotRacer(0), 0);
		TestEqual(TEXT("no list: slot 17 empty"), Cam.GetSlotRacer(17), -1);
	}
	{
		FSSXRaceCamera Cam(&Defs);
		Wire(Cam);
		Cam.PlayScript(TEXT("S_Stage"));
		Cam.Step(In[1]);
		TestTrue(TEXT("finish stage camera position"), Cam.GetPosition().Equals(FVector(5000.0 - 50.0, -2000.0 + 100.0, 1000.0), 1e-9));
		TestEqual(TEXT("finish stage camera yaw = tgt_yaw"), Cam.GetYaw(), 1.0, 1e-12);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCameraReverseHoldTest, "SSX.Camera.ReverseHold", CameraTestFlags)
bool FSSXCameraReverseHoldTest::RunTest(const FString& Parameters)
{
	// 0x113A88, 0x1776F0
	FSSXCameraDefSet Defs;
	Defs.AddJson(TEXT("{\"cameras\":{\"T\":{\"fov\":0.8}},\"scripts\":{\"S\":[{\"opcode\":0,\"name\":\"T\",\"t0\":0.0,\"t1\":5.0}]}}"));
	{
		FSSXRaceCamera Cam(&Defs);
		FSSXCameraReverseLatch L;
		Cam.SetViewIndex(2);
		TestEqual(TEXT("start mode"), Cam.GetMode(), 5);
		L.Step(Cam, true, true, false);
		TestEqual(TEXT("edge frame keeps the mode"), Cam.GetMode(), 5);
		L.Step(Cam, true, false, false);
		TestEqual(TEXT("held: mode 3"), Cam.GetMode(), 3);
		L.Step(Cam, true, false, false);
		TestEqual(TEXT("still held"), Cam.GetMode(), 3);
		L.Step(Cam, false, false, false);
		TestEqual(TEXT("release: previous mode"), Cam.GetMode(), 5);
		L.Step(Cam, true, false, false);
		TestEqual(TEXT("held without an edge does nothing"), Cam.GetMode(), 5);
	}
	{
		FSSXRaceCamera Cam(&Defs);
		Cam.SetReverseHold(true, true);
		TestEqual(TEXT("replay flag blocks the press"), Cam.GetMode(), 4);
		Cam.SetReverseHold(true, false);
		TestEqual(TEXT("press"), Cam.GetMode(), 3);
		Cam.SetReverseHold(false, true);
		TestEqual(TEXT("replay flag blocks the release"), Cam.GetMode(), 3);
		Cam.SetReverseHold(false, false);
		TestEqual(TEXT("release"), Cam.GetMode(), 4);
		Cam.SetReverseHold(false, false);
		TestEqual(TEXT("release outside mode 3 is a no-op"), Cam.GetMode(), 4);
	}
	{
		FSSXRaceCamera Cam(&Defs);
		TestTrue(TEXT("S"), Cam.PlayScript(TEXT("S")));
		Cam.SetReverseHold(true, false);
		TestEqual(TEXT("a director script blocks the press"), Cam.GetMode(), 4);
	}
	return !HasAnyErrors();
}

#endif
