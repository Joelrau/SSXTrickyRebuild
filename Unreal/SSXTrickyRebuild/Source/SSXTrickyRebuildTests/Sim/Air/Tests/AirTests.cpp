#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sim/Air/AirStates.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/SurfaceTable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AirTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void AirReport(FAutomationTestBase* Test, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Air] %s"), *Msg);
	}

	bool AirNear(double A, double B, double RelTol, double AbsTol = 1e-9)
	{
		return FMath::Abs(A - B) <= FMath::Max(AbsTol, RelTol * FMath::Abs(B));
	}

	class FAirFlatWorld final : public IRiderWorld
	{
	public:
		virtual bool ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const override
		{
			const FVector Z(0, 0, 1);
			const FVector P = In.Position + In.ContactLeft * (In.RiderScale * In.LeanDeg);
			const FVector A = P - 100.0 * In.GroundNormal, B = P + 200.0 * In.GroundNormal;
			Out = FGroundProbeResult();
			Out.ProbePoint = P;
			Out.ContactForward = FVector(In.Forward.X, In.Forward.Y, 0.0).GetSafeNormal();
			Out.ContactLeft = FVector::CrossProduct(Z, Out.ContactForward);
			Out.FallLine = In.Forward;
			Out.GroundNormal = Z;
			if ((A.Z > 0.0) == (B.Z > 0.0))
			{
				Out.ContactKind = 3;
				return false;
			}
			const double T = A.Z / (A.Z - B.Z);
			Out.bFound = true;
			Out.Surface = 1;
			Out.ContactPoint = A + T * (B - A);
			Out.Height = P.Z;
			Out.ContactKind = 0;
			return true;
		}
		virtual bool LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef, FRiderWorldHit& Out) const override
		{
			if ((A.Z > 0.0) == (B.Z > 0.0) || A.Z == B.Z)
			{
				return false;
			}
			const double T = A.Z / (A.Z - B.Z);
			Out = FRiderWorldHit();
			Out.Point = A + T * (B - A);
			Out.Normal = FVector(0, 0, 1);
			Out.Surface = 1;
			Out.T = T;
			return true;
		}
		virtual const FSurfaceParams& SurfaceParams(int32 SurfaceId) const override { return Table.Get(SurfaceId); }
		virtual double FloorZ() const override { return -1e6; }

	private:
		FSurfaceTable Table;   // built-in snow defaults (stick height 2.742 cm)
	};

	struct FJumpResult
	{
		int32 Frames = 0;
		double Apex = 0.0;
		double Meter = 0.0;
		SSXAir::FAirLandingRecord Landing;
		ERiderControl Control = ERiderControl::None;
	};

	FJumpResult RunFlatJump(const FVector& Launch, double PrewindSpin, int32 HoldFrames, int32 SpinDir, const FQuat& Tilt)
	{
		FAirFlatWorld World;
		FRiderSim Sim;
		Sim.SetWorld(&World);
		RegisterAirStates(Sim);
		Sim.Reset();
		Sim.State.bRacing = false;
		Sim.State.Position = FVector(0, 0, 0.01);
		Sim.State.Velocity = Launch;
		Sim.State.Orientation = Tilt;
		Sim.UpdateBasis();
		Sim.State.PrewindA.Cur = PrewindSpin;
		Sim.SetControl(ERiderControl::Jump, TEXT("test"));
		Sim.SetMotion(ERiderMotion::Air, TEXT("T25"));
		Sim.SetControl(ERiderControl::Spin, TEXT("T26"));
		FJumpResult R;
		while (Sim.State.Motion == ERiderMotion::Air && R.Frames < 1200)
		{
			FRiderInputWord In;
			In.Spin = In.LateSpin = R.Frames < HoldFrames ? SpinDir : 0;
			Sim.Step(In);
			++R.Frames;
		}
		const SSXAir::FAirContext* Ctx = SSXAir::GetAirContext(Sim);
		R.Apex = Ctx->ApexZ;
		R.Meter = Sim.State.BoostMeter;
		R.Landing = Ctx->LastLanding;
		R.Control = Sim.State.Control;
		return R;
	}

	bool AirLoadAlaska(FAutomationTestBase& Test, FPatchWorld& World)
	{
		FString Err;
		if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("alaska")), &Err))
		{
			Test.AddError(FString::Printf(TEXT("Alaska patch data not available: %s"), *Err));
			return false;
		}
		World.SetInstances(TArray<FSimInstance>(), TArray<FSimInstanceNode>(), TArray<FSimInstanceTri>());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAirDemoFlatTest, "SSX.Air.DemoFlat", AirTestFlags)

bool FSSXAirDemoFlatTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	const FVector Launch(0.0, 1500.0, 900.0);
	constexpr double Rel = 1e-3;
	constexpr double DegTol = 0.5;
	constexpr double SpinTol = 0.05;

	// 0x100908, 0x102800
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("air_demo.csv"))) || Lines.Num() < 2)
	{
		SSXTest::Skip(*this, TEXT("skipped: SimData/air_demo.csv missing (run tools/make_air_reference.py)"));
		return true;
	}
	for (int32 L = 1; L < Lines.Num(); ++L)
	{
		TArray<FString> F;
		Lines[L].ParseIntoArray(F, TEXT(","), false);
		if (F.Num() < 9)
		{
			continue;
		}
		const double Prewind = FCString::Atod(*F[0]);
		const int32 Hold = FCString::Atoi(*F[1]);
		const int32 Frames = FCString::Atoi(*F[2]);
		const double Apex = FCString::Atod(*F[3]), SpinDeg = FCString::Atod(*F[4]);
		const int32 Spins180 = FCString::Atoi(*F[5]);
		const double Reward = FCString::Atod(*F[6]), Meter = FCString::Atod(*F[7]);
		const FString Tag = FString::Printf(TEXT("prewind %.0f hold %d"), Prewind, Hold);
		const FJumpResult R = RunFlatJump(Launch, Prewind, Hold, 1, FQuat::Identity);
		AirReport(this, FString::Printf(TEXT("%s: airtime %d f, apex %.3f cm, total spin %.2f deg (model %.2f), half-turns %d, reward %+.5f, meter %.5f, %s"),
			*Tag, R.Frames, R.Apex, R.Landing.TotalSpinDeg, SpinDeg, R.Landing.Summary.Spins180, R.Landing.Reward, R.Meter,
			LexToString(R.Landing.Verdict)));
		TestEqual(*(Tag + TEXT(" airtime frames")), R.Frames, Frames);
		TestTrue(*(Tag + TEXT(" apex")), AirNear(R.Apex, Apex, Rel));
		TestTrue(*FString::Printf(TEXT("%s spin %.2f vs model %.2f deg"), *Tag, R.Landing.TotalSpinDeg, SpinDeg),
			FMath::Abs(R.Landing.TotalSpinDeg - FMath::TruncToDouble(SpinDeg)) <= 1.0 + SpinTol);
		TestEqual(*(Tag + TEXT(" half-turns")), R.Landing.Summary.Spins180, Spins180);
		TestTrue(*(Tag + TEXT(" reward")), AirNear(R.Landing.Reward, Reward, Rel, 1e-9));
		TestTrue(*(Tag + TEXT(" meter")), AirNear(R.Meter, Meter, Rel, 1e-9));
		TestTrue(*(Tag + TEXT(" LandNormal")), R.Landing.Verdict == ELandingVerdict::LandNormal && R.Control == ERiderControl::LandNormal);
	}

	// plain ollie and auto-align from 30 deg nose-up (phi = -30) to level
	{
		const FQuat Tilt(FVector(1, 0, 0), FMath::DegreesToRadians(30.0));
		double Psi0, Phi0;
		LandingAngles(Tilt, FVector(0, 1049.962291, -1372.108206), FVector(0, 0, 1), Psi0, Phi0);
		const FJumpResult R = RunFlatJump(Launch, 0.0, 0, 0, Tilt);
		double Psi1, Phi1;
		LandingAngles(R.Landing.Result.FoldedQ, R.Landing.VelocityBefore, FVector(0, 0, 1), Psi1, Phi1);
		AirReport(this, FString::Printf(TEXT("auto-align: takeoff pitch %.2f deg -> touchdown pitch %.4f deg (%d f), %s"),
			FMath::RadiansToDegrees(Phi0), FMath::RadiansToDegrees(Phi1), R.Frames, LexToString(R.Landing.Verdict)));
		TestTrue(TEXT("takeoff pitch -30"), FMath::Abs(FMath::RadiansToDegrees(Phi0) + 30.0) <= DegTol);
		TestTrue(TEXT("auto-align to 0 deg"), FMath::Abs(FMath::RadiansToDegrees(Phi1)) <= DegTol);
		TestEqual(TEXT("auto-align airtime"), R.Frames, 107);
		TestTrue(TEXT("touchdown velocity"), R.Landing.VelocityBefore.Equals(FVector(0, 1049.962291, -1372.108206), 1.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAirLandingGridTest, "SSX.Air.LandingGrid", AirTestFlags)

bool FSSXAirLandingGridTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	using V = ELandingVerdict;
	const FVector Vel(0.0, 1049.962291, -1372.108206);
	const FVector N(0, 0, 1);
	FRiderProfile Profile;

	struct FGridCase { const TCHAR* Axis; int32 Deg; V Verdict; double Factor; };
	const FGridCase Grid[] = {
		{ TEXT("pitch"), 0, V::LandNormal, 1.000000 }, { TEXT("pitch"), 30, V::LandNormal, 0.935749 },
		{ TEXT("pitch"), 50, V::LandHard, 0.637652 },  { TEXT("pitch"), 70, V::LandHard, 0.637652 },
		{ TEXT("pitch"), 100, V::LandHard, 0.637652 }, { TEXT("pitch"), 150, V::WipeOut, 0.0 },
		{ TEXT("roll"), 0, V::LandNormal, 1.000000 },  { TEXT("roll"), 30, V::LandNormal, 0.962921 },
		{ TEXT("roll"), 50, V::LandNormal, 0.908989 }, { TEXT("roll"), 70, V::LandNormal, 0.882581 },
		{ TEXT("roll"), 100, V::WipeOut, 0.0 },        { TEXT("roll"), 150, V::WipeOut, 0.0 },
		{ TEXT("yaw"), 0, V::LandNormal, 1.000000 },   { TEXT("yaw"), 30, V::LandNormal, 0.977282 },
		{ TEXT("yaw"), 50, V::LandNormal, 0.886412 },  { TEXT("yaw"), 70, V::LandNormal, 0.795542 },
		{ TEXT("yaw"), 100, V::LandNormal, 0.750106 }, { TEXT("yaw"), 150, V::LandNormal, 0.750106 },
		{ TEXT("pitch"), -30, V::LandNormal, 0.935749 }, { TEXT("pitch"), -50, V::LandNormal, 0.850082 },
		{ TEXT("pitch"), -70, V::LandNormal, 0.850082 }, { TEXT("pitch"), -90, V::LandNormal, 0.850082 },
	};
	int32 Fails = 0;
	for (const FGridCase& C : Grid)
	{
		const FVector Axis = FCString::Strcmp(C.Axis, TEXT("pitch")) == 0 ? FVector(1, 0, 0)
			: (FCString::Strcmp(C.Axis, TEXT("roll")) == 0 ? FVector(0, 1, 0) : FVector(0, 0, 1));
		FLandingInput In;
		In.BasisQ = FQuat(Axis, FMath::DegreesToRadians(double(C.Deg)));
		In.Velocity = Vel;
		In.Normal = N;
		In.KLand = LandScale(Profile);
		const FLandingResult R = EvaluateLanding(In);
		const double F = R.Verdict == V::WipeOut ? 0.0 : R.SpeedFactor;
		const bool bOk = R.Verdict == C.Verdict && FMath::Abs(F - C.Factor) <= 1e-3 * FMath::Max(1.0, C.Factor);
		Fails += bOk ? 0 : 1;
		TestTrue(FString::Printf(TEXT("%s %+d: %s(%.4f) expected %s(%.4f)"), C.Axis, C.Deg, LexToString(R.Verdict), F,
			LexToString(C.Verdict), C.Factor), bOk);
	}
	{
		FLandingInput In;
		In.Velocity = FVector(0, 1500, -3000);
		In.Normal = N;
		In.KLand = LandScale(Profile);
		TestTrue(TEXT("level board, n.v = -3000 -> LandHard"), EvaluateLanding(In).Verdict == V::LandHard);
		In.Velocity = Vel;
		In.Surface = 6;
		TestTrue(TEXT("surface 6 bails"), EvaluateLanding(In).Verdict == V::WipeOut);
		In.Surface = 1;
		In.bGrabActive = true;
		TestTrue(TEXT("grab not at a landable point bails"), EvaluateLanding(In).Verdict == V::WipeOut);
	}
	AirReport(this, FString::Printf(TEXT("air.py grid: %d cases, %d mismatches"), UE_ARRAY_COUNT(Grid), Fails));

	FRiderProfile LiveProfile;
	LiveProfile.StatBytes[0x16 - 0x0E] = 255;
	struct FLiveCase { double PhiBasis; double Flip; V Verdict; };
	const FLiveCase Live[] = {
		{ -58.3, 0.0, V::LandHard }, { -0.5, -65.0, V::LandHard }, { -0.1, -74.0, V::LandHard },
		{ 3.6, -74.0, V::LandHard }, { -7.1, 21.0, V::LandNormal }, { -8.5, 120.0, V::LandNormal },
	};
	int32 QuatOnlyWrong = 0;
	for (const FLiveCase& C : Live)
	{
		FLandingInput In;
		In.BasisQ = FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(-C.PhiBasis));
		In.PendingFlip = FMath::DegreesToRadians(C.Flip);
		In.Velocity = Vel;
		In.Normal = N;
		In.KLand = LandScale(LiveProfile);
		const FLandingResult R = EvaluateLanding(In);
		FLandingInput Q = In;
		Q.PendingFlip = 0.0;
		const V QuatOnly = EvaluateLanding(Q).Verdict;
		QuatOnlyWrong += QuatOnly != C.Verdict ? 1 : 0;
		AirReport(this, FString::Printf(TEXT("live (%.1f, %+.0f): phi_basis %.2f phi_eff %.2f psi %.1f -> %s (quaternion only: %s)"),
			C.PhiBasis, C.Flip, FMath::RadiansToDegrees(R.PhiBasis), FMath::RadiansToDegrees(R.PhiEff),
			FMath::RadiansToDegrees(R.Psi), LexToString(R.Verdict), LexToString(QuatOnly)));
		TestTrue(FString::Printf(TEXT("live (%.1f, %+.0f) -> %s"), C.PhiBasis, C.Flip, LexToString(C.Verdict)), R.Verdict == C.Verdict);
		TestTrue(TEXT("phi basis"), FMath::Abs(FMath::RadiansToDegrees(R.PhiBasis) - C.PhiBasis) < 1e-6);
	}
	AirReport(this, FString::Printf(TEXT("live cases: quaternion-only reading misclassifies %d of 6"), QuatOnlyWrong));
	TestTrue(TEXT("quaternion-only reading misclassifies the 3 flip hard landings"), QuatOnlyWrong >= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAirPredictorTest, "SSX.Air.Predictor", AirTestFlags)

bool FSSXAirPredictorTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	FPatchWorld World;
	if (!AirLoadAlaska(*this, World))
	{
		return false;
	}
	FVector MarkerPos, MarkerFwd;
	World.GetStartMarker(MarkerPos, MarkerFwd);

	struct FCand { int32 Patch; double Dist; };
	TArray<FCand> Cands;
	for (int32 K = 0; K < World.NumPatches(); ++K)
	{
		const FSimPatch& P = World.GetPatch(K);
		if (P.Surface != 1 && P.Surface != 5)
		{
			continue;
		}
		FVector Pt, Du, Dv;
		P.Evaluate(0.5, 0.5, Pt, Du, Dv);
		const double Nz = FVector::CrossProduct(Du, Dv).GetSafeNormal().Z;
		if (Nz >= 0.80 && Nz <= 0.97)
		{
			Cands.Add({ K, FVector::Dist(Pt, MarkerPos) });
		}
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Dist < B.Dist; });

	constexpr int32 WantLaunches = 8;
	int32 Valid = 0, Tried = 0, RefinedOk = 0, CoarseOk = 0, FineOk = 0, ExactOk = 0, MaxFineErr = 0;
	double MaxRefinedErr = 0.0, MaxCoarseErr = 0.0;
	for (const FCand& C : Cands)
	{
		if (Valid >= WantLaunches || Tried >= 400)
		{
			break;
		}
		++Tried;
		FVector Centre, Du, Dv;
		World.GetPatch(C.Patch).Evaluate(0.5, 0.5, Centre, Du, Dv);
		const FVector N0 = FVector::CrossProduct(Du, Dv).GetSafeNormal();
		const FVector Down = FPatchWorld::FallLineFor(N0, MarkerFwd);

		FRiderSim Sim;
		Sim.SetWorld(&World);
		RegisterAirStates(Sim);
		Sim.Reset();
		Sim.Spawn(Centre, Down, FVector::ZeroVector, 0.0, false);
		if (Sim.State.Motion != ERiderMotion::Ground)
		{
			continue;
		}
		const FVector N = Sim.State.GroundNormal;
		const FVector Fl = FPatchWorld::FallLineFor(N, Down);
		Sim.State.Velocity = 1500.0 * Fl + 600.0 * N;
		const FVector LaunchPos = Sim.State.Position, LaunchVel = Sim.State.Velocity;
		Sim.ApplyTransition(TEXT("T28"));
		const FAirContext* Ctx = GetAirContext(Sim);
		const FLandingPredictorResult Pred = Ctx->LastPrediction;
		const int32 S = Pred.Surface;
		if (!Pred.bHit || S == 0 || S == 6 || S == 10 || Pred.HitTime < 0.3 || Pred.HitTime > 5.0)
		{
			continue;
		}
		int32 Frames = 0;
		TArray<FString> Trace;
		while (Sim.State.Motion == ERiderMotion::Air && Frames < 400)
		{
			Sim.Step(FRiderInputWord());
			++Frames;
			FGroundProbeInput PIn;
			PIn.Position = Sim.State.Position;
			PIn.GroundNormal = Sim.State.GroundNormal;
			PIn.Forward = Sim.State.BasisForward;
			PIn.ContactLeft = Sim.State.ContactLeft;
			FGroundProbeResult PR;
			const bool bF = World.ProbeGround(PIn, PR);
			Trace.Add(FString::Printf(TEXT("      f%d motion %s found %d h %.2f v.n_probe %.1f v.n_state %.1f n_state (%.3f %.3f %.3f)"), Frames,
				LexToString(Sim.State.Motion), bF, PR.Height, FVector::DotProduct(PR.GroundNormal, Sim.State.Velocity),
				FVector::DotProduct(Sim.State.GroundNormal, Sim.State.Velocity), Sim.State.GroundNormal.X, Sim.State.GroundNormal.Y, Sim.State.GroundNormal.Z));
		}
		const FAirLandingRecord& L = Ctx->LastLanding;
		if (!L.bValid || L.bBodyContactBail || Sim.State.Motion == ERiderMotion::Air || Sim.State.Motion == ERiderMotion::Static)
		{
			AirReport(this, FString::Printf(TEXT("patch %d skipped: no touchdown (motion %s after %d f)"),
				World.GetPatch(C.Patch).SourceIndex, LexToString(Sim.State.Motion), Frames));
			continue;
		}
		++Valid;
		const FLandingPredictorResult PFine = PredictLanding(&World, LaunchPos, LaunchVel, true, ERiderLineQuery::Fine);
		const FLandingPredictorResult PExact = PredictLanding(&World, LaunchPos, LaunchVel, true, ERiderLineQuery::FineRefined);
		const int32 FineFrame = FMath::CeilToInt32(PFine.HitTime * 60.0 - 1e-6);
		const int32 ExactFrame = FMath::CeilToInt32(PExact.HitTime * 60.0 - 1e-6);
		FineOk += FMath::Abs(FineFrame - Frames) <= 1 ? 1 : 0;
		ExactOk += FMath::Abs(ExactFrame - Frames) <= 1 ? 1 : 0;
		if (FMath::Abs(ExactFrame - Frames) > 1)
		{
			for (int32 I = FMath::Max(0, Trace.Num() - 7); I < Trace.Num(); ++I) { AirReport(this, Trace[I]); }
		}
		AirReport(this, FString::Printf(TEXT("    fine cells: frame %d (%.4f s); type 2 refined: frame %d (%.4f s)"), FineFrame, PFine.HitTime, ExactFrame, PExact.HitTime));
		const int32 PredFrame = FMath::CeilToInt32(Pred.HitTime * 60.0 - 1e-6);
		const int32 CoarseFrame = FMath::CeilToInt32(Pred.CoarseHitTime * 60.0 - 1e-6);
		const int32 Err = PredFrame - Frames, CErr = CoarseFrame - Frames;
		RefinedOk += FMath::Abs(Err) <= 1 ? 1 : 0;
		CoarseOk += FMath::Abs(CErr) <= 1 ? 1 : 0;
		MaxRefinedErr = FMath::Max(MaxRefinedErr, double(FMath::Abs(Err)));
		MaxCoarseErr = FMath::Max(MaxCoarseErr, double(FMath::Abs(CErr)));
		AirReport(this, FString::Printf(TEXT("patch %d (n.z %.3f): predicted %.4f s (frame %d, chord %d; linear-in-chord %.4f s = frame %d), simulated touchdown frame %d, surface %d -> %d, %s, |dpoint| %.1f cm"),
			World.GetPatch(C.Patch).SourceIndex, N.Z, Pred.HitTime, PredFrame, Pred.ChordIndex, Pred.CoarseHitTime, CoarseFrame,
			Frames, S, L.Surface, LexToString(L.Verdict), FVector::Dist(Pred.Point, Sim.State.ContactPoint)));
		MaxFineErr = FMath::Max(MaxFineErr, FMath::Abs(FineFrame - Frames));
		TestTrue(FString::Printf(TEXT("patch %d coarse-cell (notes) prediction frame %d within 4 of touchdown %d"), World.GetPatch(C.Patch).SourceIndex, PredFrame, Frames),
			FMath::Abs(Err) <= 4);
	}
	AirReport(this, FString::Printf(TEXT("predictor: %d launches (%d sites tried); HitTime (default linear in chord; bRefinePredictorHit off) within +-1 frame: %d (max err %.0f), linear-in-chord within +-1: %d (max err %.0f)"),
		Valid, Tried, RefinedOk, MaxRefinedErr, CoarseOk, MaxCoarseErr));
	AirReport(this, FString::Printf(TEXT("predictor diagnostic: fine-cell sweep within +-1: %d / %d, type-2 sweep within +-1: %d / %d"), FineOk, Valid, ExactOk, Valid));
	TestTrue(TEXT("at least 5 launches"), Valid >= 5);
	TestTrue(TEXT("fine-cell sweep: every launch within 2 frames"), MaxFineErr <= 2);
	TestTrue(TEXT("fine-cell sweep: >= 3/4 of launches within 1 frame"), FineOk * 4 >= Valid * 3);
	return true;
}


namespace
{
	SSXAnim::FAnimData GrabAnim(int32 N, int32 M0, int32 M1, int32 M2, int32 M3, int32 Cat)
	{
		SSXAnim::FAnimData D;
		D.NFrames = N;
		D.Markers[0] = M0; D.Markers[1] = M1; D.Markers[2] = M2; D.Markers[3] = M3;
		D.Category = Cat;
		return D;
	}

	int32 FramesToLandable(SSXAnim::FAnimInstance A, int32 Limit = 400)
	{
		for (int32 K = 1; K <= Limit; ++K)
		{
			A.Advance();
			if (A.Flag(4)) { return K; }
		}
		return -1;
	}

	struct FGrabRun
	{
		int32 Frames = 0;
		SSXAir::FAirLandingRecord Landing;
		int32 TweakStarts = 0;   // 0x1272E8, 0x218B78
	};

	FGrabRun RunGrabJump(const FVector& Launch, TFunctionRef<void(int32 Step, FRiderInputWord& In)> Pad, double TrickyTimer = 0.0)
	{
		static const SSXAnim::FAnimTable Defaults;
		FAirFlatWorld World;
		FRiderSim Sim;
		Sim.SetWorld(&World);
		RegisterAirStates(Sim);
		SSXAir::GetAirContext(Sim)->Anim->Table = &Defaults;
		Sim.Reset();
		Sim.State.bRacing = false;
		Sim.State.Position = FVector(0, 0, 0.01);
		Sim.State.Velocity = Launch;
		Sim.UpdateBasis();
		Sim.SetControl(ERiderControl::Jump, TEXT("test"));
		Sim.SetMotion(ERiderMotion::Air, TEXT("T25"));
		Sim.SetControl(ERiderControl::Spin, TEXT("T26"));
		FGrabRun R;
		while (Sim.State.Motion == ERiderMotion::Air && R.Frames < 1200)
		{
			Sim.State.TrickyTimer = TrickyTimer;
			FRiderInputWord In;
			Pad(R.Frames, In);
			Sim.Step(In);
			++R.Frames;
		}
		R.Landing = SSXAir::GetAirContext(Sim)->LastLanding;
		R.TweakStarts = Sim.State.TweakStartCount;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAirGrabLandabilityTest, "SSX.Air.GrabLandability", AirTestFlags)

bool FSSXAirGrabLandabilityTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	using namespace SSXAnim;
	using V = ELandingVerdict;

	const FAnimData ExMute = GrabAnim(30, 10, 14, 20, 25, CategoryGrab);
	const double Stat128 = GrabSpeed(128.0 / 255.0);
	const double Player = 1.376;
	TestTrue(TEXT("grab speed lerp(0.782, 1.588, s/255)"), FMath::Abs(GrabSpeed(0.0) - 0.78212) < 1e-4 && FMath::Abs(GrabSpeed(1.0) - 1.58790) < 1e-4);
	{
		// held: clamped at marker 1, flags 1+2 only, flag 4 never (brod01 frame 498)
		FAnimInstance A;
		A.Start(ExMute, Player, ExMute.Markers[1]);
		for (int32 K = 0; K < 300; ++K) { A.Advance(); }
		AirReport(this, FString::Printf(TEXT("held exT_MUTE after 300 frames: frame %.2f, flags 0x%X"), A.Frame, A.Flags));
		TestTrue(TEXT("held grab sits at the hold frame 14"), A.Frame == 14.0);
		TestTrue(TEXT("held grab: flags 1+2 only"), A.Flags == 0x3);
		FLandingInput In;
		In.Velocity = FVector(0, 1049.962291, -1372.108206);
		In.KLand = LandScale(FRiderProfile());
		In.bGrabActive = true;
		In.bGrabLandable = A.Flag(4);
		In.bGrabCategory = true;
		const FLandingResult R = EvaluateLanding(In);
		TestTrue(TEXT("held grab bails (7.1 step 1)"), R.Verdict == V::WipeOut && R.bBailGrab);
		A.Hold = -1.0;
		const int32 KPlayer = FramesToLandable(A);
		FAnimInstance B;
		B.Start(ExMute, Stat128, ExMute.Markers[1]);
		for (int32 K = 0; K < 60; ++K) { B.Advance(); }
		B.Hold = -1.0;
		const int32 K128 = FramesToLandable(B);
		int32 KDone = 0;
		for (FAnimInstance C = B; !C.IsDone(); ++KDone) { C.Advance(); }
		AirReport(this, FString::Printf(TEXT("released at the hold point: landable after %d frames (speed 1.376), %d (stat 128, speed %.4f); animation over after %d"),
			KPlayer, K128, Stat128, KDone));
		TestEqual(TEXT("player speed: landable 16 frames after release"), KPlayer, 16);
		TestEqual(TEXT("stat 128: landable 19 frames after release"), K128, 19);
		TestEqual(TEXT("stat 128: animation over 26 frames after release"), KDone, 26);
	}
	{
		struct FLive { const TCHAR* Name; double Frame; int32 M3; bool bLandable; };
		const FLive Live[] = { { TEXT("bxUT_OTMETHOD s1 f4160"), 51.0, 65, false }, { TEXT("bxUT_OTMETHOD s1 f7051"), 36.5, 65, false },
			{ TEXT("bxUT_MTMUTE s1 f5563"), 45.5, 57, false }, { TEXT("bxTW_MUTE s2 f6993"), 14.5, 20, false } };
		for (const FLive& L : Live)
		{
			const bool bFlag4 = double(L.M3) < L.Frame;
			TestEqual(FString::Printf(TEXT("live %s: flag 4 at frame %.1f (marker %d)"), L.Name, L.Frame, L.M3), bFlag4, L.bLandable);
		}
		const FAnimData Uber = GrabAnim(61, 10, -1, 50, 50, CategoryUber);
		FAnimInstance U;
		U.Start(Uber, 1.0, -1.0);
		const int32 KU = FramesToLandable(U);
		AirReport(this, FString::Printf(TEXT("uber (61 f, m3 50): landable from game frame %d (2 m3 = %d)"), KU, 2 * Uber.Markers[3]));
		TestEqual(TEXT("uber landable only after 2 m3 frames"), KU, 2 * Uber.Markers[3] + 1);
	}

	const FVector Launch(0.0, 1500.0, 900.0);
	const int32 GrabId = 12;
	const FGrabRun Plain = RunGrabJump(Launch, [](int32, FRiderInputWord&) {});
	const int32 T = Plain.Frames;
	AirReport(this, FString::Printf(TEXT("plain jump: touchdown on step %d, %s"), T, LexToString(Plain.Landing.Verdict)));
	{
		const FGrabRun R = RunGrabJump(Launch, [GrabId](int32 S, FRiderInputWord& In) { if (S >= 5) { In.TrickId = GrabId; } });
		AirReport(this, FString::Printf(TEXT("grab held to touchdown: %s, category %d, frame %.2f"), LexToString(R.Landing.Verdict), R.Landing.AnimCategory, R.Landing.AnimFrame));
		TestTrue(TEXT("held grab bails"), R.Landing.Verdict == V::WipeOut && R.Landing.Result.bBailGrab);
	}
	int32 FirstLandable = -1;
	for (int32 K = 10; K <= 31; ++K)
	{
		const int32 Release = T - 1 - K;
		const FGrabRun R = RunGrabJump(Launch, [GrabId, Release](int32 S, FRiderInputWord& In) { if (S >= 5 && S < Release) { In.TrickId = GrabId; } });
		const bool bLanded = R.Landing.Verdict != V::WipeOut;
		if (bLanded && FirstLandable < 0) { FirstLandable = K; }
		if (K == 18 || K == 19 || K == 26 || K == 27)
		{
			AirReport(this, FString::Printf(TEXT("grab released %d frames before touchdown: %s, category %d, frame %.2f"), K, LexToString(R.Landing.Verdict), R.Landing.AnimCategory, R.Landing.AnimFrame));
		}
		if (K <= 18) { TestTrue(FString::Printf(TEXT("released %d frames before touchdown bails"), K), !bLanded); }
		if (K >= 19 && K <= 26)
		{
			TestTrue(FString::Printf(TEXT("released %d frames before touchdown lands on the category-14 tail -> LandNormal"), K),
				R.Landing.Verdict == V::LandNormal && R.Landing.AnimCategory == CategoryGrab);
		}
		// 0x15A030
		if (K >= 27) { TestTrue(FString::Printf(TEXT("released %d frames before: animation over (27 frames), not gated"), K), bLanded && R.Landing.AnimCategory == 0); }
	}
	TestEqual(TEXT("released grab first landable 19 frames after release (stat 128)"), FirstLandable, 19);
	{
		const FGrabRun R = RunGrabJump(Launch, [GrabId, T](int32 S, FRiderInputWord& In) { if (S >= T - 7 && S < T - 3) { In.TrickId = GrabId; } });
		AirReport(this, FString::Printf(TEXT("tap 4 frames, released 2 frames before touchdown: %s, category %d"), LexToString(R.Landing.Verdict), R.Landing.AnimCategory));
		TestTrue(TEXT("tap is landable immediately"), R.Landing.Verdict == V::LandNormal && R.Landing.AnimCategory == 0);
	}
	{
		const FVector High(0.0, 1500.0, 1700.0);
		const FGrabRun P2 = RunGrabJump(High, [](int32, FRiderInputWord&) {});
		const int32 T2 = P2.Frames;
		auto RunUber = [&](int32 UberStart)
		{
			return RunGrabJump(High, [GrabId, UberStart](int32 S, FRiderInputWord& In)
			{
				if (S >= UberStart - 30 && S <= UberStart) { In.TrickId = GrabId; }
				if (S == UberStart) { In.bTweak = true; }
			}, 20.0);
		};
		const FGrabRun Early = RunUber(T2 - 1 - 100);
		const FGrabRun Late = RunUber(T2 - 1 - 101);
		AirReport(this, FString::Printf(TEXT("uber started 100 frames before touchdown: %s (category %d, frame %.1f); 101 frames: %s (category %d, frame %.1f); flight %d f"),
			LexToString(Early.Landing.Verdict), Early.Landing.AnimCategory, Early.Landing.AnimFrame,
			LexToString(Late.Landing.Verdict), Late.Landing.AnimCategory, Late.Landing.AnimFrame, T2));
		TestTrue(TEXT("uber 100 frames before touchdown bails"), Early.Landing.Verdict == V::WipeOut && Early.Landing.AnimCategory == CategoryUber);
		TestTrue(TEXT("uber 101 frames before touchdown lands"), Late.Landing.Verdict != V::WipeOut && Late.Landing.AnimCategory == CategoryUber);
		TestEqual(TEXT("uber start: no #119 cue (0x1272E8 state 4 path)"), Late.TweakStarts, 0);
		// 0x218B78
		const FGrabRun Tweak = RunGrabJump(High, [GrabId, T2](int32 S, FRiderInputWord& In)
		{
			const int32 Start = T2 - 1 - 101;
			if (S >= Start - 30 && S <= Start) { In.TrickId = GrabId; }
			if (S == Start) { In.bTweak = true; }
		}, 0.0);
		TestEqual(TEXT("tweak with no uber available: #119 cue once"), Tweak.TweakStarts, 1);
	}
	return true;
}

// 0x100908
namespace
{
	void StartFlipJump(FRiderSim& Sim, const FVector& Pos, const FVector& Vel, int32 SpinStat, double P)
	{
		Sim.Reset();
		Sim.State.bRacing = false;
		Sim.Profile.StatBytes[0x1A - 0x0E] = uint8(SpinStat);
		Sim.State.Position = Pos;
		Sim.State.Velocity = Vel;
		Sim.UpdateBasis();
		Sim.State.PrewindB.Cur = P / (SSXAir::SeedPrewind * SSXAir::SpinGain(Sim.Profile) * SSXAir::SeedPrewindFlipScale);
		Sim.SetControl(ERiderControl::Jump, TEXT("test"));
		Sim.SetMotion(ERiderMotion::Air, TEXT("T25"));
		Sim.SetControl(ERiderControl::Spin, TEXT("T26"));
	}

	double HeldFlipAngle(double P, double K, int32 N, double Dt)
	{
		const double Floor = FMath::Min(FMath::Abs(P), SSXAir::RateFloorMax);
		const float DtF = float(Dt / FRiderSim::Dt) * 0.016666668f;
		float A = 0.0f;
		double Bound = FMath::Abs(P);
		for (int32 J = 0; J < N; ++J)
		{
			A = SSXEE::FpuAdd(A, float(FMath::Max(Bound, Floor)) * DtF);
			Bound *= K;
		}
		return FMath::Sign(P) * double(A);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAirHeldFlipTest, "SSX.Air.HeldFlip", AirTestFlags)

bool FSSXAirHeldFlipTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("heldflip_reference.csv"))))
	{
		AddError(TEXT("SimData/heldflip_reference.csv missing: run tools/make_heldflip_reference.py"));
	}
	struct FRefCase
	{
		FString Name;
		int32 Stat = 0, Updates = 0;
		double Seed = 0.0;
		TArray<int32> SIn, FIn;
		TArray<double> Model, Trace;
	};
	TArray<FRefCase> Cases;
	for (const FString& L : Lines)
	{
		TArray<FString> C;
		L.ParseIntoArray(C, TEXT(","), false);
		if (C.Num() == 5 && C[0] == TEXT("case"))
		{
			FRefCase& R = Cases.AddDefaulted_GetRef();
			R.Name = C[1];
			R.Stat = FCString::Atoi(*C[2]);
			R.Seed = FCString::Atod(*C[3]);
			R.Updates = FCString::Atoi(*C[4]);
		}
		else if (C.Num() == 6 && C[0] == TEXT("s") && Cases.Num() > 0)
		{
			FRefCase& R = Cases.Last();
			R.SIn.Add(FCString::Atoi(*C[2]));
			R.FIn.Add(FCString::Atoi(*C[3]));
			R.Model.Add(FCString::Atod(*C[4]));
			R.Trace.Add(C[5].IsEmpty() ? NAN : FCString::Atod(*C[5]));
		}
	}
	if (Lines.Num() > 0 && Cases.Num() < 2)
	{
		AddError(TEXT("heldflip_reference.csv: expected the two ride_session_1 cases"));
	}
	for (const FRefCase& R : Cases)
	{
		FAirFlatWorld World;
		FRiderSim Sim;
		Sim.SetWorld(&World);
		RegisterAirStates(Sim);
		StartFlipJump(Sim, FVector(0, 0, 1e5), FVector(0, 1500, 0), R.Stat, R.Seed);
		const FAirContext* Ctx = GetAirContext(Sim);
		TestTrue(R.Name + TEXT(": seeded as the trace (5.1/5.2)"), AirNear(Ctx->Spin.FlipRate, R.Seed, 1e-9) && Ctx->Spin.Phase == 0);
		TArray<double> Port;
		for (int32 K = 0; K < R.SIn.Num() && Sim.State.Motion == ERiderMotion::Air; ++K)
		{
			FRiderInputWord In;
			In.Spin = R.SIn[K];
			In.Flip = R.FIn[K];
			Sim.Step(In);
			Port.Add(Sim.State.FlipAngle);
		}
		if (!TestEqual(R.Name + TEXT(": airborne for every update"), Port.Num(), R.SIn.Num()))
		{
			continue;
		}
		double ErrModel = 0.0, ErrTrace = 0.0;
		int32 NTrace = 0;
		for (int32 K = 0; K < Port.Num(); ++K)
		{
			ErrModel = FMath::Max(ErrModel, FMath::Abs(Port[K] - R.Model[K]));
			if (!FMath::IsNaN(R.Trace[K]))
			{
				double E = 1e9;
				for (int32 J = FMath::Max(0, K - 1); J <= FMath::Min(Port.Num() - 1, K + 1); ++J)
				{
					E = FMath::Min(E, FMath::Abs(R.Trace[K] - Port[J]));
				}
				ErrTrace = FMath::Max(ErrTrace, E);
				++NTrace;
			}
		}
		AirReport(this, FString::Printf(TEXT("%s: stat %d seed %.1f deg/s, %d updates, touchdown angle %.2f deg; max |port - model| %.2e deg, max |port - trace| %.3f deg over %d samples"),
			*R.Name, R.Stat, FMath::RadiansToDegrees(R.Seed), Port.Num(), FMath::RadiansToDegrees(Port.Last()),
			FMath::RadiansToDegrees(ErrModel), FMath::RadiansToDegrees(ErrTrace), NTrace));
		TestTrue(R.Name + TEXT(": port = models/air.py per update"), FMath::RadiansToDegrees(ErrModel) < 1e-3);
		TestTrue(R.Name + TEXT(": port = live trace (+-1 frame)"), FMath::RadiansToDegrees(ErrTrace) < 0.25);
	}

	int32 PrevFlips = -1, MaxFlips = 0;
	for (const double Vz : { 400.0, 700.0, 1000.0, 1400.0, 1900.0, 2500.0 })
	{
		FAirFlatWorld World;
		FRiderSim Sim;
		Sim.SetWorld(&World);
		RegisterAirStates(Sim);
		constexpr int32 Stat = 128;
		Sim.Profile.StatBytes[0x1A - 0x0E] = Stat;
		const double P = SeedPrewind * SpinGain(Sim.Profile) * SeedPrewindFlipScale;
		StartFlipJump(Sim, FVector(0, 0, 0.01), FVector(0, 1500, Vz), Stat, P);
		const double K = SpinDecay(Sim.Profile), Dt = Sim.MotionDt();
		double Err = 0.0, Last = 0.0;
		int32 N = 0;
		while (Sim.State.Motion == ERiderMotion::Air && N < 2000)
		{
			FRiderInputWord In;
			In.Flip = -1;
			Sim.Step(In);
			if (Sim.State.Motion != ERiderMotion::Air)
			{
				break;
			}
			++N;
			Last = Sim.State.FlipAngle;
			Err = FMath::Max(Err, FMath::Abs(Last - HeldFlipAngle(P, K, N, Dt)));
		}
		const double Deg = FMath::RadiansToDegrees(Last);
		const int32 Best = Round180(Deg);
		const int32 Flips = (Best - Best % 360) / 360;
		AirReport(this, FString::Printf(TEXT("held front flip, launch vz %.0f: %d air updates, seed %.1f deg/s, last rate %.1f deg/s, angle %.1f deg -> %d flip(s); max |port - closed form| %.2e deg"),
			Vz, N, FMath::RadiansToDegrees(P), FMath::RadiansToDegrees(GetAirContext(Sim)->Spin.FlipRate), Deg, Flips,
			FMath::RadiansToDegrees(Err)));
		TestTrue(FString::Printf(TEXT("vz %.0f: held flip = closed form every update"), Vz), FMath::RadiansToDegrees(Err) < 1e-6);
		TestTrue(FString::Printf(TEXT("vz %.0f: flip count grows with air time"), Vz), Flips >= PrevFlips);
		PrevFlips = Flips;
		MaxFlips = FMath::Max(MaxFlips, Flips);
	}
	TestTrue(TEXT("a held flip keeps rotating: two or more flips in the longest air"), MaxFlips >= 2);

	{
		FAirFlatWorld World;
		FRiderSim Sim;
		Sim.SetWorld(&World);
		RegisterAirStates(Sim);
		Sim.Profile.StatBytes[0x1A - 0x0E] = 128;
		const double P = SeedPrewind * SpinGain(Sim.Profile) * SeedPrewindFlipScale;
		StartFlipJump(Sim, FVector(0, 0, 0.01), FVector(0, 1500, 2500.0), 128, P);
		double Last = 0.0, Max = 0.0;
		bool bHeld = true;
		int32 N = 0;
		while (Sim.State.Motion == ERiderMotion::Air && N < 2000)
		{
			FRiderInputWord In;
			bHeld = bHeld && FMath::RadiansToDegrees(Sim.State.FlipAngle) < 250.0;
			In.Flip = bHeld ? -1 : 0;
			Sim.Step(In);
			if (Sim.State.Motion != ERiderMotion::Air)
			{
				break;
			}
			++N;
			Last = Sim.State.FlipAngle;
			Max = FMath::Max(Max, Last);
		}
		const double Deg = FMath::RadiansToDegrees(Last);
		AirReport(this, FString::Printf(TEXT("front flip let go at 250 deg, %d air updates: last angle %.3f deg (max %.3f)"), N, Deg,
			FMath::RadiansToDegrees(Max)));
		TestTrue(TEXT("let-go flip settles onto 360 in a long air"), Deg > 359.0 && FMath::RadiansToDegrees(Max) <= 360.0);
	}
	return !HasAnyErrors();
}

// 0x1000E0, 0x15FDD0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAirNaturalAirChannel0Test, "SSX.Air.NaturalAirChannel0", AirTestFlags)

bool FSSXAirNaturalAirChannel0Test::RunTest(const FString& Parameters)
{
	FAirFlatWorld World;
	auto Run = [&World](int32 Ch0, double Steer, const FRiderInputWord& In, FRiderState& Out)
	{
		FRiderSim Sim;
		Sim.SetWorld(&World);
		RegisterAirStates(Sim);
		Sim.Reset();
		Sim.State.Position = FVector(0, 0, 500);
		Sim.State.Steer.Cur = Steer;   // 0x1000B8
		Sim.State.Steer.Target = Steer;
		Sim.SetMotion(ERiderMotion::Air, TEXT("test"));
		Sim.SetControl(ERiderControl::NaturalAir, TEXT("test"));
		Sim.State.Crouch.Cur = 0.2;
		Sim.State.Ramp1F0.Cur = 0.7;
		Sim.State.Ch0Anim = Ch0;
		Sim.GetControlState(ERiderControl::NaturalAir)->Update(Sim, In);
		Out = Sim.State;
	};
	FRiderInputWord Turn;
	Turn.Turn = 0.5;
	FRiderState S;
	Run(0x224, 0.0, Turn, S);
	TestTrue(TEXT("0x224: crouch held at 1 (cur, target), rate 0"), S.Crouch.Cur == 1.0 && S.Crouch.Target == 1.0 && S.Crouch.Rate == 0.0);
	TestTrue(TEXT("0x224: pose ramp zeroed"), S.Ramp1F0.Cur == 0.0 && S.Ramp1F0.Target == 0.0 && S.Ramp1F0.Rate == 0.0);
	TestEqual(TEXT("0x224: no Spin with a turn input"), int32(S.Control), int32(ERiderControl::NaturalAir));
	Run(0x223, 0.0, Turn, S);
	TestEqual(TEXT("air idle: a turn input starts Spin (T50)"), int32(S.Control), int32(ERiderControl::Spin));
	Run(0x223, 0.3, Turn, S);
	TestTrue(TEXT("steering: steer_follow -> 0 at 2/s"), S.SteerFollow.Target == 0.0 && S.SteerFollow.Rate == 2.0);
	return !HasAnyErrors();
}

#endif
