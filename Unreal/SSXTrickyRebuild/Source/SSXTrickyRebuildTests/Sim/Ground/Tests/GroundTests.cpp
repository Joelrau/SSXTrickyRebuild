#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Harness/RiderSetup.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/GroundControls.h"
#include "Sim/Ground/GroundMotion.h"
#include "Sim/Ground/GroundPhysics.h"
#include "Sim/Ground/GroundStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/EEFloat.h"
#include "Sim/RiderSim.h"
#include "Sim/SurfaceTable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags GroundTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
	constexpr double Dt = 1.0 / 60.0;

	void Report(FAutomationTestBase* Test, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Ground] %s"), *Msg);
	}

	FSurfaceParams ModelSurface(bool bNoDrag = false, bool bNoCruise = false)
	{
		FSurfaceParams S;
		const float V[13] = { 980.f, 0.02f, 0.01f, 0.004f, 8.f, 30.f, 30.f, 2.f, 10.f, 8.f, 0.f, 30.f, 1.f };
		for (int32 I = 0; I < 13; ++I) { S.F[I] = V[I]; }
		if (bNoDrag) { S.F[1] = S.F[2] = S.F[3] = 0.f; }
		if (bNoCruise) { S.F[11] = 0.f; }
		return S;
	}

	class FGroundTestPlaneWorld : public IRiderWorld
	{
	public:
		FGroundTestPlaneWorld(double SlopeDeg, const FSurfaceParams& InSurface, int32 InSurfaceId = 1)
			: Surface(InSurface), SurfaceId(InSurfaceId)
		{
			const double A = FMath::DegreesToRadians(SlopeDeg);
			N0 = FVector(0.0, FMath::Sin(A), FMath::Cos(A));
		}
		FVector N0;
		FSurfaceParams Surface;
		int32 SurfaceId;

		static FVector Unit(const FVector& V, const FVector& Fb) { const double L = V.Size(); return L < 1e-9 ? Fb : V / L; }

		virtual bool ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const override
		{
			Out = FGroundProbeResult();
			const FVector O = In.Position + In.ContactLeft * (In.RiderScale * In.LeanDeg);
			Out.ProbePoint = O;
			const double Dist = FVector::DotProduct(O, N0);
			const double Denom = FVector::DotProduct(In.GroundNormal, N0);
			double T = 0.0;
			bool bHit = FMath::Abs(Denom) >= 1e-6;
			if (bHit)
			{
				T = -Dist / Denom;
				bHit = T >= -100.0 && T <= 200.0;
			}
			if (!bHit)
			{
				Out.GroundNormal = FVector::UpVector;
				Out.ContactForward = Unit(In.Forward - In.Forward.Z * FVector::UpVector, In.Forward);
				Out.ContactLeft = FVector::CrossProduct(FVector::UpVector, Out.ContactForward);
				Out.FallLine = In.Forward;
				Out.ContactKind = 3;
				return false;
			}
			const FVector Hit = O + T * In.GroundNormal;
			Out.bFound = true;
			Out.Surface = SurfaceId;
			Out.GroundNormal = N0;
			Out.ContactPoint = Hit;
			Out.ContactForward = Unit(In.Forward - FVector::DotProduct(In.Forward, N0) * N0, In.Forward);
			Out.ContactLeft = FVector::CrossProduct(N0, Out.ContactForward);
			Out.Height = FVector::DotProduct(O - Hit, N0);
			const FVector Across = FVector::CrossProduct(N0, FVector::UpVector);
			Out.FallLine = Across.Size() > 1e-4 ? FVector::CrossProduct(N0, Across / Across.Size()) : In.Forward;
			Out.ContactKind = 0;
			return true;
		}
		virtual bool LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef, FRiderWorldHit& Out) const override
		{
			const double Da = FVector::DotProduct(A, N0), Db = FVector::DotProduct(B, N0);
			if ((Da > 0.0 && Db > 0.0) || (Da < 0.0 && Db < 0.0) || Da == Db)
			{
				return false;
			}
			const double T = Da / (Da - Db);
			Out = FRiderWorldHit();
			Out.Point = A + T * (B - A);
			Out.Normal = N0;
			Out.Surface = SurfaceId;
			Out.T = T;
			return true;
		}
		virtual const FSurfaceParams& SurfaceParams(int32) const override { return Surface; }
		virtual double FloorZ() const override { return -1e12; }
	};

	struct FTestRider
	{
		TUniquePtr<FRiderSim> Sim;
		TSharedPtr<FGroundContext> Ctx;
	};

	FTestRider SpawnOnPlane(const FGroundTestPlaneWorld& World, double SlopeDeg, double Speed, const FGroundConfig& Config)
	{
		FTestRider R;
		R.Sim = MakeUnique<FRiderSim>();
		R.Sim->Profile.SizeScale = 0.0;
		R.Sim->SetWorld(&World);
		R.Ctx = RegisterGroundStates(*R.Sim, Config);
		R.Sim->Reset();
		const double A = FMath::DegreesToRadians(SlopeDeg);
		const FVector Fwd(0.0, FMath::Cos(A), -FMath::Sin(A));
		R.Sim->Spawn(FVector::ZeroVector, FVector(0, 1, 0), Speed * Fwd, 0.0, false);
		SSXRiderSetup::RestInSnow(*R.Sim);
		R.Sim->State.PathHeading = FMath::Atan2(Fwd.Y, Fwd.X);
		return R;
	}

	bool Near(FAutomationTestBase& T, const TCHAR* What, double Got, double Exp, double RelTol = 1e-3)
	{
		const double Err = FMath::Abs(Got - Exp) / FMath::Max(FMath::Abs(Exp), 1e-9);
		const bool bOk = Err <= RelTol || FMath::Abs(Got - Exp) < 1e-9;
		Report(&T, FString::Printf(TEXT("%-40s got %.6f expected %.6f (rel err %.2e) %s"), What, Got, Exp, Err, bOk ? TEXT("ok") : TEXT("FAIL")));
		if (!bOk)
		{
			T.AddError(FString::Printf(TEXT("%s: got %.9f expected %.9f"), What, Got, Exp));
		}
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXGroundModelDemoTest, "SSX.Ground.ModelDemo", GroundTestFlags)

bool FSSXGroundModelDemoTest::RunTest(const FString& Parameters)
{
	FGroundConfig Cfg;
	Cfg.StatOverride = 0.5;
	Cfg.bUsePathHeading = true;

	{
		FGroundTestPlaneWorld World(20.0, ModelSurface());
		FTestRider R = SpawnOnPlane(World, 20.0, 0.0, Cfg);
		FRiderInputWord Tuck;
		Tuck.Crouch = 1.0;
		double Top = 0.0, T95 = -1.0;
		for (int32 I = 0; I < 3600; ++I)
		{
			R.Sim->Step(Tuck);
			const double Sp = R.Sim->State.Velocity.Size();
			Top = FMath::Max(Top, Sp);
			if (T95 < 0.0 && Sp >= 0.95 * 2788.84)
			{
				T95 = (I + 1) * Dt;
			}
		}
		Near(*this, TEXT("run: top speed (cm/s)"), Top, 2788.84);
		Near(*this, TEXT("run: time to 95% of cap (s)"), T95, 9.35);
		TestTrue(TEXT("run stays grounded"), R.Sim->State.Motion == ERiderMotion::Ground && R.Sim->CountTakeOffs() == 0);
	}

	struct FCarve { double Stick, Steer, Speed, OmegaDeg, RadiusCm; };
	const FCarve Carves[] = {
		{ 0.5, 0.488706678, 1107.404168836, 46.887757432, 1353.222857405 },
		{ 1.0, 0.615045607, 763.417793093, 54.960304424, 795.858356463 },
	};
	for (const FCarve& C : Carves)
	{
		FGroundTestPlaneWorld World(0.0, ModelSurface(true, true));
		FTestRider R = SpawnOnPlane(World, 0.0, 1500.0, Cfg);
		FRiderInputWord In;
		In.Turn = C.Stick;
		In.Crouch = 1.0;
		TArray<double> Head, Speed;
		for (int32 I = 0; I < 150; ++I)
		{
			R.Sim->Step(In);
			const FVector V = R.Sim->State.Velocity;
			Head.Add(FMath::Atan2(V.Y, V.X));
			Speed.Add(V.Size());
		}
		const double Omega = FMath::UnwindRadians(Head[149] - Head[119]) / (30.0 * Dt);
		const FString Tag = FString::Printf(TEXT("carve %.1f"), C.Stick);
		Near(*this, *(Tag + TEXT(": steer")), R.Sim->State.Steer.Cur, C.Steer);
		Near(*this, *(Tag + TEXT(": speed (cm/s)")), Speed[149], C.Speed);
		Near(*this, *(Tag + TEXT(": yaw rate (deg/s)")), FMath::RadiansToDegrees(Omega), C.OmegaDeg);
		Near(*this, *(Tag + TEXT(": radius (cm)")), Speed[149] / FMath::Abs(Omega), C.RadiusCm);
	}

	// [jump] launch from 2500 cm/s on 20 deg (J_min applies)
	{
		const double A = FMath::DegreesToRadians(20.0);
		const FVector N(0.0, FMath::Sin(A), FMath::Cos(A)), F(0.0, FMath::Cos(A), -FMath::Sin(A));
		struct FJump { double Stat, Charge, J, Vy, Vz; };
		const FJump Jumps[] = {
			{ 0.5, 0.0, 630.88, 2677.078369141, -316.047424316 },
			{ 0.5, 0.5, 630.88, 2677.078369141, -316.047424316 },
			{ 0.5, 1.0, 630.88, 2677.078369141, -316.047424316 },
			{ 1.0, 0.0, 630.88, 2677.078369141, -316.047424316 },
			{ 1.0, 0.5, 630.88, 2677.078369141, -316.047424316 },
			{ 1.0, 1.0, 796.849121094, 2763.327880859, -174.247375488 },
		};
		for (const FJump& Jp : Jumps)
		{
			const FVector V0 = 2500.0 * F;
			const double J = SSXGround::LaunchImpulse(V0.Size(), Jp.Charge, Jp.Stat, SSXGround::K::JumpMin);
			const FVector V1 = SSXGround::LaunchVelocity(V0, N, F, N, J, false);
			const FString Tag = FString::Printf(TEXT("jump stat %.1f charge %.1f"), Jp.Stat, Jp.Charge);
			Near(*this, *(Tag + TEXT(": J")), J, Jp.J);
			Near(*this, *(Tag + TEXT(": v.y")), V1.Y, Jp.Vy);
			Near(*this, *(Tag + TEXT(": v.z")), V1.Z, Jp.Vz);
		}
		Near(*this, TEXT("impulse 500 cm/s charge 0.5 stat 1"), SSXGround::LaunchImpulse(500.0, 0.5, 1.0, 0.0), 102.973541260);
		Near(*this, TEXT("impulse 500 cm/s charge 1 stat 1"), SSXGround::LaunchImpulse(500.0, 1.0, 1.0, 0.0), 411.894165039);
		Near(*this, TEXT("impulse 1500 cm/s charge 0.5 stat 1"), SSXGround::LaunchImpulse(1500.0, 0.5, 1.0, 0.0), 199.212280273);
	}

	// [jump] full control path: hold jump 0.5 s at 1500 cm/s, release, unwind, launch
	{
		FGroundTestPlaneWorld World(20.0, ModelSurface());
		FTestRider R = SpawnOnPlane(World, 20.0, 1500.0, Cfg);
		FRiderInputWord Hold;
		Hold.bJumpHeld = true;
		for (int32 I = 0; I < 30; ++I)
		{
			Hold.bJumpPressed = I == 0;
			R.Sim->Step(Hold);
		}
		const double Charge = R.Sim->State.Crouch.Cur;
		int32 Frames = 0;
		while (R.Sim->State.Motion == ERiderMotion::Ground && Frames < 30)
		{
			R.Sim->Step(FRiderInputWord());
			++Frames;
		}
		Near(*this, TEXT("prewind 0.5 s: crouch charge"), Charge, 0.913018584);
		Near(*this, TEXT("prewind: frames to take-off"), Frames, 6.0);
		Near(*this, TEXT("prewind: J"), R.Ctx->LastLaunchImpulse, 630.88);
		TestTrue(TEXT("prewind: Air + Spin after launch (T25/T26)"),
		         R.Sim->State.Motion == ERiderMotion::Air && R.Sim->State.Control == ERiderControl::Spin);
	}

	{
		using SSXGround::ELandingKind;
		struct FLand { double Yaw, Pitch, Vn; ELandingKind Kind; double Factor, Absorb; };
		const FLand Lands[] = {
			{ 0.1, 0.1, -900.0, ELandingKind::Normal, 1.000003167, 0.405 },
			{ 0.6, 0.4, -1500.0, ELandingKind::Normal, 0.924924975, 0.675 },
			{ 0.2, 0.2, -3000.0, ELandingKind::Hard, 1.000003167, 1.0 },
			{ 0.2, -1.0, -900.0, ELandingKind::Hard, 0.850084024, 0.405 },
		};
		for (const FLand& L : Lands)
		{
			const SSXGround::FLandingResult Res = SSXGround::ClassifyLanding(L.Yaw, L.Pitch, L.Vn, 0.5);
			const FString Tag = FString::Printf(TEXT("land yaw %.1f pitch %.1f vn %.0f"), L.Yaw, L.Pitch, L.Vn);
			TestTrue(*(Tag + TEXT(": kind")), Res.Kind == L.Kind);
			Near(*this, *(Tag + TEXT(": speed factor")), Res.SpeedFactor, L.Factor);
			Near(*this, *(Tag + TEXT(": absorb")), Res.Absorb, L.Absorb);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXGroundModelFramesTest, "SSX.Ground.ModelFrames", GroundTestFlags)

bool FSSXGroundModelFramesTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("ground_reference.csv"));
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 2)
	{
		AddError(FString::Printf(TEXT("cannot read %s (run tools/ground_reference.py)"), *Path));
		return false;
	}
	struct FRow { int32 Frame; double Slope; FVector P, V, F, U; double Steer, Crouch, Brake, H; FString Motion, Control; double J; };
	TMap<FString, TArray<FRow>> Scenarios;
	TArray<FString> Order;
	for (int32 I = 1; I < Lines.Num(); ++I)
	{
		TArray<FString> C;
		Lines[I].ParseIntoArray(C, TEXT(","), false);
		if (C.Num() < 22) { continue; }
		auto D = [&C](int32 K) { return FCString::Atod(*C[K]); };
		FRow R{ FCString::Atoi(*C[1]), D(2), FVector(D(3), D(4), D(5)), FVector(D(6), D(7), D(8)), FVector(D(9), D(10), D(11)),
		        FVector(D(12), D(13), D(14)), D(15), D(16), D(17), D(18), C[19], C[20], D(21) };
		if (!Scenarios.Contains(C[0])) { Order.Add(C[0]); }
		Scenarios.FindOrAdd(C[0]).Add(R);
	}

	auto ModelControl = [](ERiderControl Ctl) -> FString
	{
		switch (Ctl)
		{
		case ERiderControl::Cruise: return TEXT("cruise");
		case ERiderControl::Prewind: return TEXT("prewind");
		case ERiderControl::Jump: return TEXT("jump");
		case ERiderControl::Spin: return TEXT("spin");
		case ERiderControl::NaturalAir: return TEXT("natural_air");
		default: return LexToString(Ctl);
		}
	};
	auto InputFor = [](const FString& Name, int32 I, bool bPrevHeld) -> FRiderInputWord
	{
		FRiderInputWord W;
		if (Name == TEXT("run20")) { W.Crouch = 1.0; }
		else if (Name == TEXT("carve05")) { W.Turn = 0.5; W.Crouch = 1.0; }
		else if (Name == TEXT("carve10")) { W.Turn = 1.0; W.Crouch = 1.0; }
		else if (Name == TEXT("sturn15")) { W.Turn = ((I / 90) % 2 == 0) ? 0.8 : -0.8; W.Brake = (I >= 300 && I < 330) ? 0.6 : 0.0; }
		else if (Name == TEXT("prewind20")) { W.bJumpHeld = I < 30; }
		W.bJumpPressed = W.bJumpHeld && !bPrevHeld;
		W.PrewindTurn = W.Turn;
		return W;
	};

	bool bAllOk = true;
	for (const FString& Name : Order)
	{
		const TArray<FRow>& Rows = Scenarios[Name];
		const double Slope = Rows[0].Slope;
		const bool bCarve = Name.StartsWith(TEXT("carve"));
		FGroundTestPlaneWorld World(Slope, ModelSurface(bCarve, bCarve));
		FGroundConfig Cfg;
		Cfg.bUsePathHeading = true;
		const double Speed0 = Name == TEXT("run20") ? 0.0 : (Name == TEXT("sturn15") ? 800.0 : 1500.0);
		FTestRider R = SpawnOnPlane(World, Slope, Speed0, Cfg);
		double MaxP = 0.0, MaxV = 0.0, MaxF = 0.0, MaxS = 0.0;
		int32 FirstBad = -1;
		FString Why;
		bool bPrevHeld = false;
		for (const FRow& Row : Rows)
		{
			const FRiderInputWord W = InputFor(Name, Row.Frame, bPrevHeld);
			bPrevHeld = W.bJumpHeld;
			R.Sim->Step(W);
			const FRiderState& S = R.Sim->State;
			const double EP = (S.Position - Row.P).GetAbsMax();
			const double EV = (S.Velocity - Row.V).GetAbsMax() / FMath::Max(1.0, Row.V.Size());
			const double EF = FMath::Max((S.BasisForward - Row.F).GetAbsMax(), (S.BasisUp - Row.U).GetAbsMax());
			const double ES = FMath::Max3(FMath::Abs(S.Steer.Cur - Row.Steer), FMath::Abs(S.Crouch.Cur - Row.Crouch), FMath::Abs(S.Brake.Cur - Row.Brake));
			MaxP = FMath::Max(MaxP, EP); MaxV = FMath::Max(MaxV, EV); MaxF = FMath::Max(MaxF, EF); MaxS = FMath::Max(MaxS, ES);
			const FString Mot = S.Motion == ERiderMotion::Ground ? TEXT("ground") : (S.Motion == ERiderMotion::Air ? TEXT("air") : LexToString(S.Motion));
			const bool bStateOk = Mot == Row.Motion && ModelControl(S.Control) == Row.Control;
			// 0x129008, 0x128E90, 0x2510F8, 0x10A0D8
			if (FirstBad < 0 && (EP > 2e-2 || EV > 1e-5 || EF > 2e-5 || ES > 1e-6 || !bStateOk))
			{
				FirstBad = Row.Frame;
				Why = FString::Printf(TEXT("dpos %.3g dvel(rel) %.3g daxes %.3g dfilters %.3g state %s/%s vs %s/%s"),
				                      EP, EV, EF, ES, *Mot, *ModelControl(S.Control), *Row.Motion, *Row.Control);
			}
			if (Row.J >= 0.0 && R.Ctx->LastLaunchImpulse >= 0.0 && FMath::Abs(Row.J - R.Ctx->LastLaunchImpulse) > 1e-6)
			{
				AddError(FString::Printf(TEXT("%s: launch J %.6f vs model %.6f"), *Name, R.Ctx->LastLaunchImpulse, Row.J));
			}
		}
		Report(this, FString::Printf(TEXT("%-10s %3d frames: max |dpos| %.2e cm, max |dvel|/|v| %.2e, max |daxes| %.2e, max |dfilter| %.2e%s"),
		                             *Name, Rows.Num(), MaxP, MaxV, MaxF, MaxS,
		                             FirstBad < 0 ? TEXT("") : *FString::Printf(TEXT(" -- first mismatch frame %d: %s"), FirstBad, *Why)));
		if (FirstBad >= 0)
		{
			bAllOk = false;
			AddError(FString::Printf(TEXT("%s diverges from models/ground.py at frame %d: %s"), *Name, FirstBad, *Why));
		}
	}
	return bAllOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXGroundAlaskaRunTest, "SSX.Ground.AlaskaRun", GroundTestFlags)

bool FSSXGroundAlaskaRunTest::RunTest(const FString& Parameters)
{
	FPatchWorld World;
	FString Err;
	if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("alaska")), &Err) || !World.GetSurfaces().IsLoaded())
	{
		AddError(FString::Printf(TEXT("Alaska patch data / surfaces.bin not available: %s"), *Err));
		return false;
	}
	World.SetInstances(TArray<FSimInstance>(), TArray<FSimInstanceNode>(), TArray<FSimInstanceTri>());
	FVector MarkerPos, MarkerFwd;
	if (!World.GetStartMarker(MarkerPos, MarkerFwd))
	{
		AddError(TEXT("no start marker"));
		return false;
	}

	struct FCand { int32 Patch; double Dist; };
	TArray<FCand> Cands;
	for (int32 K = 0; K < World.NumPatches(); ++K)
	{
		const FSimPatch& P = World.GetPatch(K);
		if (P.Surface != 1 && P.Surface != 2 && P.Surface != 5) { continue; }
		FVector Pt, Du, Dv;
		P.Evaluate(0.5, 0.5, Pt, Du, Dv);
		const double Nz = FVector::CrossProduct(Du, Dv).GetSafeNormal().Z;
		if (Nz >= 0.92 && Nz <= 0.9995) { Cands.Add({ K, FVector::Dist(Pt, MarkerPos) }); }
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Dist < B.Dist; });

	int32 Reasons[4] = { 0, 0, 0, 0 };
	TArray<FVector> Path;
	auto Gentle = [&World, &Reasons, &Path](const FVector& From, const FVector& Heading) -> bool
	{
		constexpr int32 NA = 5, ND = 25;
		Path.Reset();
		FVector P = From, Dir = Heading;
		TArray<FVector> Prev;
		double SumNz = 0.0;
		auto Sample = [&World](const FVector& Q, FRiderWorldHit& Hit) -> bool
		{
			if (!World.LineQuery(Q + FVector(0, 0, 3000), Q - FVector(0, 0, 3000), ERiderLineQuery::FineRefined, 0.5, Hit)) { return false; }
			if (Hit.Normal.Z < 0.0) { Hit.Normal = -Hit.Normal; }
			return true;
		};
		for (int32 D = 0; D < ND; ++D)
		{
			FRiderWorldHit Hc;
			if (!Sample(P, Hc)) { ++Reasons[0]; return false; }
			Path.Add(Hc.Point);
			const FVector Across = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
			TArray<FVector> Row;
			for (int32 A = 0; A < NA; ++A)
			{
				FRiderWorldHit Hit;
				const FVector Q = Hc.Point + Across * (500.0 * (A - NA / 2));
				if (!Sample(Q, Hit)) { ++Reasons[0]; return false; }
				const FVector N = Hit.Normal;
				if (N.Z < 0.80 || Hit.Surface == 0 || SSXGround::IsBailSurface(Hit.Surface)) { ++Reasons[1]; return false; }
				if ((D > 0 && (N - Prev[A]).Size() > 0.15) || (A > 0 && (N - Row[A - 1]).Size() > 0.15)) { ++Reasons[2]; return false; }
				Row.Add(N);
				SumNz += N.Z;
			}
			Prev = Row;
			Dir = FPatchWorld::FallLineFor(Hc.Normal, Dir);
			Dir = FVector(Dir.X, Dir.Y, 0.0).GetSafeNormal();
			P = Hc.Point + 500.0 * Dir;
		}
		if (SumNz / (NA * ND) > 0.99) { ++Reasons[3]; return false; }
		return true;
	};

	FVector Centre, Downhill;
	int32 Tried = 0;
	bool bFound = false;
	for (const FCand& C : Cands)
	{
		++Tried;
		FVector Du, Dv;
		World.GetPatch(C.Patch).Evaluate(0.5, 0.5, Centre, Du, Dv);
		const FVector N = FVector::CrossProduct(Du, Dv).GetSafeNormal();
		Downhill = FPatchWorld::FallLineFor(N, MarkerFwd);
		Downhill = FVector(Downhill.X, Downhill.Y, 0.0).GetSafeNormal();
		if (Gentle(Centre, Downhill)) { bFound = true; break; }
	}
	if (!bFound)
	{
		AddError(FString::Printf(TEXT("no gentle 20 x 120 m area among %d candidates (miss %d, steep/not snow %d, rough %d, flat %d)"), Cands.Num(), Reasons[0], Reasons[1], Reasons[2], Reasons[3]));
		return false;
	}

	FRiderSim Sim;
	Sim.SetWorld(&World);
	TSharedRef<FGroundContext> Ctx = RegisterGroundStates(Sim, FGroundConfig());
	Sim.Reset();
	Sim.Spawn(Centre, Downhill, FVector::ZeroVector, 0.0, false);
	Sim.State.Velocity = 600.0 * Sim.State.ContactForward;
	if (Sim.State.Motion != ERiderMotion::Ground)
	{
		AddError(TEXT("spawn did not start grounded"));
		return false;
	}
	const FVector Start = Sim.State.Position;
	ERiderMotion PrevMotion = Sim.State.Motion;
	int32 FramesInArea = 0, TakeOffsInArea = 0;
	int32 OverCap = 0, SteerLeftFrames = 0, SteerRightFrames = 0;
	double MaxSpeed = 0.0, MaxH = -1e9, MinH = 1e9, YawRateLeft = 0.0, YawRateRight = 0.0, LeanLeft = 0.0, LeanRight = 0.0;
	TMap<int32, int32> Surfaces;
	double PrevYaw = Sim.State.Yaw;
	for (int32 Frame = 0; Frame < 30 * 60; ++Frame)
	{
		// 3 s straight down the surveyed fall line, then steady steer switching side every second
		FRiderInputWord In;
		const int32 Phase = (Frame / 60) % 2;
		In.Turn = Frame < 180 ? 0.0 : (Phase == 0 ? 0.7 : -0.7);
		In.Crouch = 0.0;
		Sim.Step(In);
		const FRiderState& S = Sim.State;
		const double Speed = S.Velocity.Size();
		MaxSpeed = FMath::Max(MaxSpeed, Speed);
		// 0x117968
		const float SpeedF = SSXEE::Length(SSXEE::F(S.Velocity));
		if (S.Motion != ERiderMotion::Air && SpeedF > std::nextafter(float(S.SpeedCap), 3.0e38f)) { ++OverCap; }
		MaxH = FMath::Max(MaxH, S.GroundHeight);
		MinH = FMath::Min(MinH, S.GroundHeight);
		Surfaces.FindOrAdd(S.SurfaceId)++;
		const double YawRate = FMath::RadiansToDegrees(FMath::UnwindRadians(S.Yaw - PrevYaw)) / Dt;
		PrevYaw = S.Yaw;
		if (S.Steer.Cur > 0.5) { ++SteerLeftFrames; YawRateLeft += YawRate; LeanLeft += S.Lean.Cur; }
		if (S.Steer.Cur < -0.5) { ++SteerRightFrames; YawRateRight += YawRate; LeanRight += S.Lean.Cur; }
		double Nearest = 1e18;
		for (const FVector& Pp : Path) { Nearest = FMath::Min(Nearest, FVector::Dist2D(Pp, S.Position)); }
		const bool bInArea = Nearest <= 1000.0;
		if (bInArea) { ++FramesInArea; }
		if (S.Motion != ERiderMotion::Ground && PrevMotion == ERiderMotion::Ground)
		{
			const FString Msg = FString::Printf(TEXT("take-off at frame %d (%s): motion %s control %s h %.2f surface %d speed %.0f"),
			                                    Frame, bInArea ? TEXT("inside the surveyed gentle area") : TEXT("outside the surveyed area"),
			                                    LexToString(S.Motion), LexToString(S.Control), S.GroundHeight, S.SurfaceId, Speed);
			if (bInArea) { ++TakeOffsInArea; AddError(Msg); } else { Report(this, Msg); }
		}
		PrevMotion = S.Motion;
	}
	FString SurfList;
	for (const TPair<int32, int32>& P : Surfaces) { SurfList += FString::Printf(TEXT("%d:%d "), P.Key, P.Value); }
	YawRateLeft /= FMath::Max(1, SteerLeftFrames);
	YawRateRight /= FMath::Max(1, SteerRightFrames);
	LeanLeft /= FMath::Max(1, SteerLeftFrames);
	LeanRight /= FMath::Max(1, SteerRightFrames);
	Report(this, FString::Printf(TEXT("start candidate %d at (%.0f %.0f %.0f); travelled %.0f m, drop %.0f m, max speed %.0f cm/s (cap %.0f), take-offs %d"),
	                             Tried, Start.X, Start.Y, Start.Z, FVector::Dist(Start, Sim.State.Position) / 100.0,
	                             (Start.Z - Sim.State.Position.Z) / 100.0, MaxSpeed, FRiderSim::SpeedTiers[0], Sim.CountTakeOffs()));
	Report(this, FString::Printf(TEXT("height range [%.2f, %.2f] cm; surfaces (id:frames) %s"), MinH, MaxH, *SurfList));
	Report(this, FString::Printf(TEXT("steer > 0.5: %d frames, mean yaw rate %+.1f deg/s, mean lean %+.1f deg; steer < -0.5: %d frames, %+.1f deg/s, lean %+.1f (live: +114 / -28.2, -109 / +28.4)"),
	                             SteerLeftFrames, YawRateLeft, LeanLeft, SteerRightFrames, YawRateRight, LeanRight));
	Report(this, FString::Printf(TEXT("frames inside the surveyed area %d, take-offs there %d"), FramesInArea, TakeOffsInArea));
	TestEqual(TEXT("take-offs on the surveyed gentle terrain"), TakeOffsInArea, 0);
	TestTrue(TEXT("rider spent >= 3 s on the surveyed terrain"), FramesInArea >= 180);
	TestEqual(TEXT("frames above the speed cap"), OverCap, 0);
	TestTrue(TEXT("rider moved > 50 m"), FVector::Dist(Start, Sim.State.Position) > 5000.0);
	TestTrue(TEXT("steer > 0 turns left (positive game-space yaw rate)"), SteerLeftFrames > 0 && YawRateLeft > 0.0);
	TestTrue(TEXT("steer < 0 turns right"), SteerRightFrames > 0 && YawRateRight < 0.0);
	TestTrue(TEXT("lean opposite to steer"), LeanLeft < 0.0 && LeanRight > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXGroundSurfaceTableTest, "SSX.Ground.SurfaceTable", GroundTestFlags)

bool FSSXGroundSurfaceTableTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("patch type 5 -> surface 5 (identity, tools/surface_mapping.py)"), SSXGround::SurfaceIdFromPatchType(5), 5);
	TestTrue(TEXT("deep snow = 3, 4"), SSXGround::IsDeepSnow(3) && SSXGround::IsDeepSnow(4) && !SSXGround::IsDeepSnow(5));
	TestTrue(TEXT("bail = 6, 10"), SSXGround::IsBailSurface(6) && SSXGround::IsBailSurface(10) && !SSXGround::IsBailSurface(1));
	FSurfaceTable Table;
	FString Err;
	if (!Table.LoadFromFile(FSurfaceTable::DefaultPath(), &Err))
	{
		AddError(Err);
		return false;
	}
	const FSurfaceParams& S1 = Table.Get(1);
	const FSurfaceParams& S5 = Table.Get(5);
	const FSurfaceParams& S3 = Table.Get(3);
	Report(this, FString::Printf(TEXT("surface 1: grip %.4f edge %.1f sink %.2f/%.2f cruise %.1f km/h; 5: grip %.4f edge %.1f cruise %.1f; 3: sink %.1f/%.1f"),
	                             S1.Grip(), S1.EdgeDeg(), S1.SinkDepth(), S1.SinkMax(), S1.CruiseKmh(), S5.Grip(), S5.EdgeDeg(), S5.CruiseKmh(),
	                             S3.SinkDepth(), S3.SinkMax()));
	TestTrue(TEXT("record 5 is the low-grip 45 deg surface"), S5.Grip() < 0.01 && FMath::Abs(S5.EdgeDeg() - 45.0) < 0.1);
	TestTrue(TEXT("record 3 is deep snow"), S3.SinkDepth() > 10.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXGroundChangeFakieTest, "SSX.Ground.ChangeFakie", GroundTestFlags)

bool FSSXGroundChangeFakieTest::RunTest(const FString& Parameters)
{
	TArray<int32> LiveFull;
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("change_fakie_live.csv"));
		TArray<FString> Lines;
		if (FFileHelper::LoadFileToStringArray(Lines, *Path) && Lines.Num() > 1)
		{
			double MinJump = 1e9, MaxJump = 0.0;
			for (int32 I = 1; I < Lines.Num(); ++I)
			{
				TArray<FString> C;
				Lines[I].ParseIntoArray(C, TEXT(","), false);
				if (C.Num() < 8) { continue; }
				const double Jump = FCString::Atod(*C[4]);
				MinJump = FMath::Min(MinJump, Jump);
				MaxJump = FMath::Max(MaxJump, Jump);
				const int32 After = FCString::Atoi(*C[6]);
				if (After == int32(ERiderControl::Cruise) || After == int32(ERiderControl::Prewind))
				{
					LiveFull.Add(FCString::Atoi(*C[5]));
				}
			}
			Report(this, FString::Printf(TEXT("live: %d segments, entry yaw jump %.1f..%.1f deg, full lengths %s"), Lines.Num() - 1, MinJump, MaxJump,
				*FString::JoinBy(LiveFull, TEXT(" "), [](int32 V) { return FString::FromInt(V); })));
			TestTrue(TEXT("live: every entry turns the rider by 173..180 deg"), MinJump >= 173.0 && MaxJump <= 180.0);
		}
		else
		{
			SSXTest::Skip(*this, TEXT("SimData/change_fakie_live.csv missing (tools/make_change_fakie_reference.py); live comparison skipped"));
		}
	}

	const FSurfaceParams Surf = ModelSurface();
	FGroundTestPlaneWorld World(0.0, Surf);
	TArray<int32> PortLengths;
	for (int32 Class = 0; Class < 2; ++Class)
	{
		FTestRider R;
		R.Sim = MakeUnique<FRiderSim>();
		R.Sim->Profile.SizeScale = 0.0;
		R.Sim->Profile.BoardClass = Class;
		R.Sim->SetWorld(&World);
		FGroundConfig Config;
		Config.StatOverride = 0.5;
		R.Ctx = RegisterGroundStates(*R.Sim, Config);
		R.Sim->Reset();
		// nose +Y, sliding tail first at 300 cm/s
		R.Sim->Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector(0, -300, 0), 0.0, false);
		FRiderState& St = R.Sim->State;
		const int32 RSwitchN = R.Ctx->Anim->Frames(*R.Sim, SSXAnim::SwitchFakie);
		if (RSwitchN <= 0)
		{
			SSXTest::Skip(*this, TEXT("SimData/anim_table.bin missing: R_SWITCH length unknown, ChangeFakie checks skipped"));
			return true;
		}
		const int32 Expect = SSXAnim::ControlFrames(RSwitchN);
		int32 Entry = -1, Exit = -1, Entries = 0;
		double Jump = 0.0, SteerBefore = 0.0, SteerAfter = 0.0;
		bool bStanceBefore = false, bStanceAfter = false;
		ERiderControl After = ERiderControl::None;
		FRiderInputWord In;
		In.Turn = 0.3;
		for (int32 F = 0; F < 60 * 15; ++F)
		{
			const ERiderControl Before = St.Control;
			const FVector NoseBefore = St.BasisForward;
			const double SB = St.Steer.Cur;
			const bool bGB = St.bStanceGoofy;
			R.Sim->Step(In);
			if (St.Control == ERiderControl::ChangeFakie && Before != ERiderControl::ChangeFakie)
			{
				++Entries;
				if (Entry < 0)
				{
					Entry = F;
					Jump = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(NoseBefore.GetSafeNormal2D(), St.BasisForward.GetSafeNormal2D()), -1.0, 1.0)));
					SteerBefore = SB;
					SteerAfter = St.Steer.Cur;
					bStanceBefore = bGB;
					bStanceAfter = St.bStanceGoofy;
				}
			}
			if (Entry >= 0 && Exit < 0 && St.Control != ERiderControl::ChangeFakie)
			{
				Exit = F;
				After = St.Control;
			}
			In.Turn = F < 30 ? 0.3 : 0.0;
		}
		Report(this, FString::Printf(TEXT("class %d: R_SWITCH n %d, entry frame %d, yaw jump %.2f deg, ChangeFakie %d frames (expect %d), exit %s, entries in 15 s %d, steer %.4f -> %.4f"),
			Class, RSwitchN, Entry, Jump, Exit - Entry, Expect, LexToString(After), Entries, SteerBefore, SteerAfter));
		TestTrue(*FString::Printf(TEXT("class %d: ChangeFakie entered"), Class), Entry >= 0);
		TestTrue(*FString::Printf(TEXT("class %d: turned by 173..180 deg on entry"), Class), Jump >= 173.0 && Jump <= 180.0);
		TestTrue(*FString::Printf(TEXT("class %d: stance bit toggled"), Class), bStanceBefore != bStanceAfter);
		TestEqual(*FString::Printf(TEXT("class %d: lasts ControlFrames(R_SWITCH)"), Class), Exit - Entry, Expect);
		TestEqual(*FString::Printf(TEXT("class %d: back to Cruise"), Class), int32(After), int32(ERiderControl::Cruise));
		TestEqual(*FString::Printf(TEXT("class %d: entered once (not stuck re-entering)"), Class), Entries, 1);
		PortLengths.Add(Expect);
	}
	TestEqual(TEXT("bx / fr R_SWITCH control lengths = 41 / 29 (live)"), PortLengths, TArray<int32>({ 41, 29 }));
	for (const int32 L : LiveFull)
	{
		TestTrue(*FString::Printf(TEXT("live full segment %d frames = a port length (+-1)"), L),
			FMath::Abs(L - 41) <= 1 || FMath::Abs(L - 29) <= 1);
	}

	// 0x103EB8
	{
		FTestRider R;
		R.Sim = MakeUnique<FRiderSim>();
		R.Sim->Profile.SizeScale = 0.0;
		R.Sim->SetWorld(&World);
		R.Ctx = RegisterGroundStates(*R.Sim, FGroundConfig());
		R.Sim->Reset();
		R.Sim->Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector(0, 300, 0), 0.0, false);
		FRiderState& St = R.Sim->State;
		St.Steer.Cur = 0.4;
		St.Lean.Cur = -12.0;
		St.SteerFollow.Target = 0.4;
		const FVector Nose = St.BasisForward;
		SSXGround::FChangeFakieControl Ctl(R.Ctx.ToSharedRef());
		Ctl.Enter(*R.Sim, ERiderControl::Cruise);
		TestEqual(TEXT("enter: steer negated"), St.Steer.Cur, -0.4);
		TestEqual(TEXT("enter: lean negated"), St.Lean.Cur, 12.0);
		TestTrue(TEXT("enter: steer_follow target 0 at 2/s"), St.SteerFollow.Target == 0.0 && St.SteerFollow.Rate == 2.0);
		TestTrue(TEXT("enter: nose reversed"), FVector::DotProduct(Nose, St.BasisForward) < -0.9999);
	}
	return !HasAnyErrors();
}

// 0x105F48, 0x105F58, 0x106140, 0x106228, 0x1062B0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXGroundGateStartTest, "SSX.Ground.GateStart", GroundTestFlags)

bool FSSXGroundGateStartTest::RunTest(const FString& Parameters)
{
	using namespace SSXGround;
	FGroundConfig Cfg;
	Cfg.StatOverride = 0.5;
	const float GF = SSXEE::FpuAdd(127.5f * float(K::GateGainStat) * 0.003921569f, float(K::GateGainBase));
	const double G = GF;
	FGroundTestPlaneWorld World(0.0, ModelSurface());

	struct FRun
	{
		double Push = 0.0, CurAtGo = 0.0, Rise = 0.0, Fall = 0.0, Expected = 0.0, LaunchSpeed = 0.0, CurAtLaunch = 0.0;
		bool bMovedLast = false;
		int32 LaunchFrames = -1, BaseFrames = -1;
	};
	double M1 = Cfg.GateRockPhase;
	auto Run = [&](int32 PullFrame, double Back) -> FRun
	{
		FRun Out;
		FTestRider R;
		R.Sim = MakeUnique<FRiderSim>();
		R.Sim->SetWorld(&World);
		R.Ctx = RegisterGroundStates(*R.Sim, Cfg);
		R.Sim->Reset();
		R.Sim->Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, true);
		R.Sim->SetControl(ERiderControl::GateAnticipate, TEXT("countdown enter"));
		FRiderState& St = R.Sim->State;
		double Prev = St.GateLaunch.Cur;
		for (int32 F = 0; F < 150; ++F)
		{
			FRiderInputWord In;
			In.GateRock = F < PullFrame ? 1.0 : Back;
			Prev = St.GateLaunch.Cur;
			R.Sim->Step(In);
			const double Cur = St.GateLaunch.Cur;
			if (Cur > Prev) { Out.Rise = Cur; }
			if (Cur < Prev) { Out.Fall = Cur; }
			if (In.GateRock > 0.0) { M1 = St.GateLaunch.Target; }
		}
		Out.CurAtGo = St.GateLaunch.Cur;
		Out.bMovedLast = St.GateLaunch.Cur != Prev;
		const double Formula = double(SSXEE::FpuSub(float(Out.Rise), float(Out.Fall)) * float(K::GatePushK) * SSXEE::FpuSub(1.0f, float(Out.CurAtGo)) / SSXEE::FpuAdd(0.0f, 1.0f));
		Out.Expected = (St.GateLaunch.Target < 0.5 && Out.bMovedLast) ? FMath::Max(Formula, K::GatePushMin) : K::GatePushMin;
		R.Sim->ApplyTransition(TEXT("T46"));
		Out.Push = St.LaunchCharge;
		for (int32 F = 1; F <= 600 && St.Control == ERiderControl::GateLaunch; ++F)
		{
			R.Sim->Step(FRiderInputWord());
			if (Out.LaunchFrames < 0 && St.Motion == ERiderMotion::Ground)
			{
				Out.LaunchFrames = F;
				Out.LaunchSpeed = St.Velocity.Size();
				Out.CurAtLaunch = St.GateLaunch.Cur;
			}
			if (St.Control == ERiderControl::GateToBase) { Out.BaseFrames = F; }
		}
		return Out;
	};

	Run(150, -1.0);
	const FRun Neutral = Run(0, 0.0);
	Report(this, FString::Printf(TEXT("gate pose markers: M1 phase %.4f; neutral: cur %.4f push %.3f"), M1, Neutral.CurAtGo, Neutral.Push));
	Near(*this, TEXT("neutral: floor push"), Neutral.Push, K::GatePushMin, 1e-9);
	const FRun Fwd = Run(150, -1.0);
	Near(*this, TEXT("forward at GO: push"), Fwd.Push, Fwd.Expected, 1e-9);
	const FRun Back = Run(0, -1.0);
	Near(*this, TEXT("held back: floor push"), Back.Push, K::GatePushMin, 1e-9);

	double Best = 0.0;
	int32 BestPull = -1, First = -1, Last = -1;
	for (int32 Pull = 0; Pull <= 150; ++Pull)
	{
		const FRun Rn = Run(Pull, -1.0);
		if (FMath::Abs(Rn.Push - Rn.Expected) > 1e-6)
		{
			AddError(FString::Printf(TEXT("pull at frame %d: push %.6f, rule %.6f"), Pull, Rn.Push, Rn.Expected));
		}
		if (Rn.Push > K::GatePushMin)
		{
			if (First < 0) { First = Pull; }
			Last = Pull;
		}
		if (Rn.Push > Best) { Best = Rn.Push; BestPull = Pull; }
	}
	Report(this, FString::Printf(TEXT("rock back window: pull %d..%d updates before GO beat the floor; best %.1f cm/s (x gain %.4f = %.1f cm/s) "
		"pulling %d updates before GO"), 150 - First, 150 - Last, Best, G, Best * G, 150 - BestPull));
	TestTrue(TEXT("a timed rock-back beats the floor push"), Best > K::GatePushMin);
	if (First > 0)
	{
		const FRun Early = Run(First - 1, -1.0);
		TestFalse(TEXT("boundary: earlier pull stopped before GO"), Early.bMovedLast && Early.Expected > K::GatePushMin);
		Near(*this, TEXT("boundary: earlier pull gets the floor"), Early.Push, K::GatePushMin, 1e-9);
		const FRun Edge = Run(First, -1.0);
		TestTrue(TEXT("boundary: first scoring pull moved on the last update"), Edge.bMovedLast);
	}

	const FRun Best1 = Run(BestPull, -1.0);
	Near(*this, TEXT("launch speed = gain x launch value"), Best1.LaunchSpeed, double(GF * float(Best1.Push)), 1e-6);
	const float RiseStep = (GF * float(K::GateRateK) * float(K::GateRateMin)) * 0.016666668f;
	const double RiseRate = RiseStep * 60.0;
	if (Best1.LaunchFrames > 0)
	{
		Report(this, FString::Printf(TEXT("best rock: cur at GO %.4f, push after %d GateLaunch updates (gate value %.4f; marker-2 phase in (%.4f, %.4f]), Gate2Base after %d"),
			Best1.CurAtGo, Best1.LaunchFrames, Best1.CurAtLaunch, Best1.CurAtLaunch - RiseRate, Best1.CurAtLaunch, Best1.BaseFrames));
		float Expect = float(Best1.CurAtGo);
		for (int32 F = 0; F < Best1.LaunchFrames; ++F)
		{
			Expect = Expect < SSXEE::FpuSub(1.0f, RiseStep) ? SSXEE::FpuAdd(Expect, RiseStep) : 1.0f;
		}
		TestTrue(TEXT("gate value rises at the GateLaunch rate"), FMath::IsNearlyEqual(Best1.CurAtLaunch, double(Expect), 1e-9));
	}
	Near(*this, TEXT("neutral launch speed = gain x floor"), Neutral.LaunchSpeed, double(GF * float(K::GatePushMin)), 1e-6);
	return !HasAnyErrors();
}

// 0x325FA8, 0x104800
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXGroundFakieChainTest, "SSX.Ground.FakieChain", GroundTestFlags)

bool FSSXGroundFakieChainTest::RunTest(const FString& Parameters)
{
	const FSurfaceParams Surf = ModelSurface();
	FGroundTestPlaneWorld World(0.0, Surf);
	auto Make = [&World](int32 Class)
	{
		FTestRider R;
		R.Sim = MakeUnique<FRiderSim>();
		R.Sim->Profile.SizeScale = 0.0;
		R.Sim->Profile.BoardClass = Class;
		R.Sim->Profile.Stance = 0;
		R.Sim->SetWorld(&World);
		FGroundConfig Config;
		Config.StatOverride = 0.5;
		R.Ctx = RegisterGroundStates(*R.Sim, Config);
		R.Sim->Reset();
		return R;
	};
	{
		FTestRider R = Make(2);
		if (R.Ctx->Anim->Frames(*R.Sim, SSXAnim::SwitchFakie) <= 0)
		{
			SSXTest::Skip(*this, TEXT("SimData/anim_table.bin missing: class-2 R_SWITCH length unknown, chain checks skipped"));
			return true;
		}
		R.Sim->Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector(0, -300, 0), 0.0, false);
		FRiderState& St = R.Sim->State;
		ERiderControl After = ERiderControl::None;
		bool bIn = false;
		for (int32 F = 0; F < 600 && After == ERiderControl::None; ++F)
		{
			R.Sim->Step(FRiderInputWord());
			if (St.Control == ERiderControl::ChangeFakie) { bIn = true; }
			else if (bIn) { After = St.Control; }
		}
		TestTrue(TEXT("class 2: ChangeFakie entered (R_SWITCH: new stance != profile stance)"), bIn);
		TestEqual(TEXT("class 2: R_SWITCH chains to the fakie loop -> FakieCruise (T12)"), int32(After), int32(ERiderControl::FakieCruise));
	}
	for (int32 Class : { 0, 2 })
	{
		for (int32 Clip : { 0x23F, 0x240 })
		{
			if (Class != 2 && Clip == 0x240) { continue; }
			FTestRider R = Make(Class);
			R.Sim->Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector(0, -200, 0), 0.0, false);
			R.Ctx->Anim->LandHardAnim = Clip;
			R.Sim->SetControl(ERiderControl::LandHard, TEXT("T53"));
			FRiderState& St = R.Sim->State;
			ERiderControl After = ERiderControl::None;
			for (int32 F = 0; F < 600 && After == ERiderControl::None; ++F)
			{
				R.Sim->Step(FRiderInputWord());
				if (St.Control != ERiderControl::LandHard) { After = St.Control; }
			}
			const ERiderControl Want = Clip == 0x240 ? ERiderControl::FakieCruise : ERiderControl::Cruise;
			TestEqual(*FString::Printf(TEXT("class %d LandHard 0x%X moving backwards -> %s"), Class, Clip, LexToString(Want)), int32(After), int32(Want));
		}
	}
	return !HasAnyErrors();
}

#endif
