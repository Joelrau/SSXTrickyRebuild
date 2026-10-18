// 0x125CB8
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Harness/RiderSetup.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Ground/GroundStates.h"
#include "Sim/Ground/GroundControls.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/RailCore.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags RailTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Report(FAutomationTestBase* Test, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Rail] %s"), *Msg);
	}

	struct FCsv
	{
		TMap<FString, int32> Col;
		TArray<TArray<FString>> Rows;

		bool Load(const FString& Path)
		{
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 2)
			{
				return false;
			}
			TArray<FString> H;
			Lines[0].ParseIntoArray(H, TEXT(","), false);
			for (int32 I = 0; I < H.Num(); ++I)
			{
				Col.Add(H[I].TrimStartAndEnd(), I);
			}
			for (int32 I = 1; I < Lines.Num(); ++I)
			{
				TArray<FString> C;
				Lines[I].ParseIntoArray(C, TEXT(","), false);
				if (C.Num() == H.Num())
				{
					Rows.Add(MoveTemp(C));
				}
			}
			return Rows.Num() > 0;
		}
		double D(int32 Row, const TCHAR* Name) const { return FCString::Atod(*Rows[Row][Col[Name]]); }
		int32 I(int32 Row, const TCHAR* Name) const { return FCString::Atoi(*Rows[Row][Col[Name]]); }
		FVector V(int32 Row, const FString& Prefix, const TCHAR* X = TEXT("x"), const TCHAR* Y = TEXT("y"), const TCHAR* Z = TEXT("z")) const
		{
			return FVector(D(Row, *(Prefix + X)), D(Row, *(Prefix + Y)), D(Row, *(Prefix + Z)));
		}
	};

	FString SimDataPath(const TCHAR* Name) { return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Name); }

	bool LoadGariRails(FAutomationTestBase* Test, FPatchWorld& World)
	{
		FString Err;
		if (!World.LoadRails(FPatchWorld::DefaultRailPath(TEXT("gari")), &Err))
		{
			SSXTest::Skip(*Test, FString::Printf(TEXT("skipped: %s"), *Err));
			return false;
		}
		return true;
	}

	SSXRail::FRailBoard BoardFrom(const FCsv& C, int32 Row, const FString& P)
	{
		SSXRail::FRailBoard B;
		B.Origin = C.V(Row, P + TEXT("o"));
		B.R0 = C.V(Row, P + TEXT("r0"));
		B.R1 = C.V(Row, P + TEXT("r1"));
		B.R2 = C.V(Row, P + TEXT("r2"));
		return B;
	}

	FSimRailSegment StraightSegment(const FVector& A, const FVector& B, int32 Spline)
	{
		FSimRailSegment S;
		S.Coef[0] = S.Coef[1] = FVector::ZeroVector;
		S.Coef[2] = B - A;
		S.Coef[3] = A;
		S.Bounds = FBox(A.ComponentMin(B), A.ComponentMax(B));
		S.Spline = Spline;
		S.Length = (B - A).Size();
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailQueryTest, "SSX.Rail.Query", RailTestFlags)

bool FSSXRailQueryTest::RunTest(const FString& Parameters)
{
	FPatchWorld World;
	FCsv C;
	if (!LoadGariRails(this, World))
	{
		return true;
	}
	if (!C.Load(SimDataPath(TEXT("gari.rail_query.csv"))))
	{
		SSXTest::Skip(*this, TEXT("skipped: SimData/gari.rail_query.csv missing (run tools/make_rail_reference.py)"));
		return true;
	}
	TestEqual(TEXT("Garibaldi splines"), World.NumRailSplines(), 169);
	TestEqual(TEXT("Garibaldi segments"), World.NumRailSegments(), 542);
	TestFalse(TEXT("Garibaldi enable state is the validated one"), World.IsRailEnableApproximate());
	int32 Found = 0, Mismatch = 0;
	double MaxP = 0.0, MaxT = 0.0;
	for (int32 R = 0; R < C.Rows.Num(); ++R)
	{
		FRiderRailQuery Q;
		Q.Origin = C.V(R, TEXT("p"));
		FRiderRailHit H;
		const bool bHit = World.QueryRail(Q, H);
		if (bHit != (C.I(R, TEXT("found")) != 0))
		{
			++Mismatch;
			continue;
		}
		if (!bHit)
		{
			continue;
		}
		++Found;
		MaxP = FMath::Max(MaxP, (H.Point - C.V(R, TEXT("h"))).Size());
		MaxT = FMath::Max(MaxT, (H.Tangent - C.V(R, TEXT("t"))).Size());
		if (H.Surface != C.I(R, TEXT("surface")) || H.Segment != C.I(R, TEXT("segment")))
		{
			++Mismatch;
		}
	}
	Report(this, FString::Printf(TEXT("query: %d points, %d hits, %d mismatches, max point error %.3g cm, max tangent error %.3g"),
		C.Rows.Num(), Found, Mismatch, MaxP, MaxT));
	TestEqual(TEXT("found / surface / segment agree with the grid-walk model"), Mismatch, 0);
	TestTrue(TEXT("nearest point within 1e-4 cm"), MaxP < 1e-4);
	TestTrue(TEXT("tangent within 1e-6"), MaxT < 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailModelSlideTest, "SSX.Rail.ModelSlide", RailTestFlags)

bool FSSXRailModelSlideTest::RunTest(const FString& Parameters)
{
	FPatchWorld World;
	FCsv C;
	if (!LoadGariRails(this, World))
	{
		return true;
	}
	if (!C.Load(SimDataPath(TEXT("gari.rail_slide.csv"))))
	{
		SSXTest::Skip(*this, TEXT("skipped: SimData/gari.rail_slide.csv missing (run tools/make_rail_reference.py)"));
		return true;
	}
	using namespace SSXRail;
	int32 Scenarios = 0, Frames = 0, Bad = 0;
	double MaxP = 0.0, MaxV = 0.0, MaxRoll = 0.0;
	FRailRider R;
	bool bActive = false;
	for (int32 Row = 0; Row < C.Rows.Num(); ++Row)
	{
		const int32 Step = C.I(Row, TEXT("step"));
		const int32 Expect = C.I(Row, TEXT("result"));
		if (Step == -1)
		{
			R = FRailRider();
			R.Pos = C.V(Row, TEXT("p"));
			R.Vel = C.V(Row, TEXT("v"));
			R.Board = BoardFrom(C, Row, TEXT(""));
			R.Up = C.V(Row, TEXT("up"));
			R.Side = C.V(Row, TEXT("s"));
			R.BoxFilter.Cur = C.D(Row, TEXT("filter"));
			R.BoxFilter.Target = C.D(Row, TEXT("filter_target"));
			R.BoxFilter.Rate = FilterRate;
			bActive = false;
			++Scenarios;
			continue;
		}
		ESlideResult Res = ESlideResult::OnRail;
		if (Step == 0)
		{
			const bool bCaught = CatchTest(R, World, false);
			if (bCaught != (Expect == 0))
			{
				AddError(FString::Printf(TEXT("scenario %s: catch %d, model %d"), *C.Rows[Row][0], bCaught, Expect == 0));
				++Bad;
				continue;
			}
			if (!bCaught)
			{
				continue;
			}
			EnterRail(R);
			bActive = true;
			TestEqual(TEXT("catch grind type"), R.GrindType, C.I(Row, TEXT("grind")));
		}
		else if (bActive)
		{
			R.Steer = C.D(Row, TEXT("steer"));
			R.BoostStrength = C.D(Row, TEXT("boost"));
			R.BoxFilter.Tick(1.0 / 60.0);
			R.Board = BoardFrom(C, Row, TEXT(""));
			Res = SlideStep(R, World);
			const int32 Got = Res == ESlideResult::OnRail ? 0 : (Res == ESlideResult::FellOff ? 1 : 2);
			if (Got != Expect)
			{
				AddError(FString::Printf(TEXT("scenario %s step %d: result %d, model %d"), *C.Rows[Row][0], Step, Got, Expect));
				++Bad;
				bActive = false;
				continue;
			}
			if (Res != ESlideResult::OnRail)
			{
				ExitRail(R);
				bActive = false;
			}
			++Frames;
		}
		else
		{
			continue;
		}
		MaxP = FMath::Max(MaxP, (R.Pos - C.V(Row, TEXT("p"))).Size());
		MaxV = FMath::Max(MaxV, (R.Vel - C.V(Row, TEXT("v"))).Size());
		MaxRoll = FMath::Max(MaxRoll, FMath::Abs(R.Roll - C.D(Row, TEXT("roll"))));
		if (FMath::Abs(R.RailTime - C.D(Row, TEXT("rail_time"))) > 1e-9 || FMath::Abs(R.BoxFilter.Cur - C.D(Row, TEXT("filter"))) > 1e-6
			|| (Step > 0 && R.Surface != C.I(Row, TEXT("surface"))))
		{
			++Bad;
		}
	}
	Report(this, FString::Printf(TEXT("model slide: %d scenarios, %d frames, %d bad, max |dp| %.3g cm, max |dv| %.3g cm/s, max |droll| %.3g rad"),
		Scenarios, Frames, Bad, MaxP, MaxV, MaxRoll));
	TestEqual(TEXT("three scenarios"), Scenarios, 3);
	TestEqual(TEXT("timer / filter / surface / result agree"), Bad, 0);
	TestTrue(TEXT("position within 1e-4 cm"), MaxP < 1e-4);
	TestTrue(TEXT("velocity within 1e-4 cm/s"), MaxV < 1e-4);
	TestTrue(TEXT("roll within 1e-8 rad"), MaxRoll < 1e-8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailLiveTest, "SSX.Rail.Live", RailTestFlags)

bool FSSXRailLiveTest::RunTest(const FString& Parameters)
{
	FPatchWorld World;
	FCsv C;
	if (!LoadGariRails(this, World))
	{
		return true;
	}
	if (!C.Load(SimDataPath(TEXT("gari.rail_live.csv"))))
	{
		SSXTest::Skip(*this, TEXT("skipped: SimData/gari.rail_live.csv missing (needs traces/live/ride_session_1; tools/make_rail_reference.py)"));
		return true;
	}
	using namespace SSXRail;
	int32 Steps = 0, QueryExact = 0, SurfaceOk = 0, SlideExact = 0, SlideSteps = 0;
	int32 CatchSample = INDEX_NONE, GameCatch = INDEX_NONE, CatchGrind = -1, GameGrind = -1, CatchBefore = 0;
	double CatchVelErr = 1e9;
	TArray<double> QErr;
	const bool bHaveR22 = C.Col.Contains(TEXT("nclip")) && C.Col.Contains(TEXT("pfx"));
	int32 OrientSteps = 0, OrientExact = 0, RailUpExact = 0;
	TArray<double> OErr;
	FRiderSim CtlSim;
	SSXGround::FRailSlideControl* Ctl = nullptr;
	int32 CtlSteps = 0, ClipOk = 0, YawOk = 0, GrindOk = 0, SwitchSample = -1, ExitGrind = -1, GameExitGrind = -1;
	int32 PrevTraceClip = -1, CurClip = 0;
	int32 FilterAtReset = 0, CatchRows = 0;
	for (int32 Row = 0; Row < C.Rows.Num(); ++Row)
	{
		const int32 Kind = C.I(Row, TEXT("kind"));
		FRailRider R;
		R.Pos = C.V(Row, TEXT("p"));
		R.Vel = C.V(Row, TEXT("v"));
		R.Up = C.V(Row, TEXT("up"));
		R.Side = C.V(Row, TEXT("s"));
		if (Kind == 0)
		{
			++Steps;
			const FVector Rp = C.V(Row, TEXT("rp"));
			double Best = 1e9;
			FRiderRailHit BestHit;
			for (const TCHAR* P : { TEXT("a"), TEXT("b") })
			{
				const FRailBoard B = BoardFrom(C, Row, P);
				FRiderRailQuery Q;
				Q.Origin = B.Origin;
				FRiderRailHit H;
				if (World.QueryRail(Q, H) && (H.Point - Rp).Size() < Best)
				{
					Best = (H.Point - Rp).Size();
					BestHit = H;
					R.Board = B;
				}
			}
			QErr.Add(Best);
			QueryExact += Best < 0.05 ? 1 : 0;
			SurfaceOk += BestHit.Surface == C.I(Row, TEXT("surface")) ? 1 : 0;
			R.Steer = C.D(Row, TEXT("steer"));
			R.BoostStrength = C.D(Row, TEXT("boost"));
			R.TimeScale = C.D(Row, TEXT("time_scale"));
			R.GrindType = C.I(Row, TEXT("grind"));
			R.BoxFilter.Cur = 1.0;
			R.RailTime = 1.0;
			if (Best < 1e8 && SlideStep(R, World) == ESlideResult::OnRail)
			{
				const double Cap = C.D(Row, TEXT("cap"));
				const double Sp = R.Vel.Size();
				if (Sp > Cap)
				{
					R.Vel *= Cap / Sp;
				}
				++SlideSteps;
				if (bHaveR22)
				{
					const FVector Up0 = C.V(Row, TEXT("up")), F0 = C.V(Row, TEXT("pf"));
					const FQuat Q0 = FMatrix(C.V(Row, TEXT("s")), F0, Up0, FVector::ZeroVector).ToQuat();
					FVector RailUp;
					const FQuat Tgt = RailFrame(R.Tangent, Up0, R.Roll, RailUp);
					const FQuat Q1 = GameSlerp(Q0, Tgt, OrientBlend).GetNormalized();
					auto Deg = [](const FVector& A, const FVector& B)
					{
						return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)));
					};
					const double E = FMath::Max(Deg(Q1.RotateVector(FVector(0, 1, 0)), C.V(Row, TEXT("nf"))),
						Deg(Q1.RotateVector(FVector(0, 0, 1)), C.V(Row, TEXT("nu"))));
					++OrientSteps;
					OErr.Add(E);
					OrientExact += E < 0.05 ? 1 : 0;
					RailUpExact += Deg(RailUp, C.V(Row, TEXT("nru"))) < 0.05 ? 1 : 0;
				}
				const double Ep = (R.Pos - C.V(Row, TEXT("np"))).Size();
				const double Ev = (R.Vel - C.V(Row, TEXT("nv"))).Size();
				SlideExact += (Ep < 0.1 && Ev < 0.5) ? 1 : 0;
			}
			if (bHaveR22)
			{
				const int32 TraceClip = C.I(Row, TEXT("nclip"));
				if (!Ctl)
				{
					RegisterGroundStates(CtlSim);
					RegisterAirStates(CtlSim);
					CtlSim.Reset();
					CtlSim.State.GrindType = C.I(Row, TEXT("grind"));
					CtlSim.SetControl(ERiderControl::RailSlide, TEXT("live"));
					Ctl = static_cast<SSXGround::FRailSlideControl*>(CtlSim.GetControlState(ERiderControl::RailSlide));
				}
				else
				{
					FRiderInputWord W;
					if (TraceClip != PrevTraceClip && PrevTraceClip >= 0)
					{
						W.RailSpin = -1.0;
						SwitchSample = C.I(Row, TEXT("sample"));
					}
					Ctl->Update(CtlSim, W);
				}
				if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(CtlSim))
				{
					if (Ch->PlayRequests.Num()) { CurClip = Ch->PlayRequests.Last(); }
					Ch->PlayRequests.Reset();
				}
				++CtlSteps;
				ClipOk += CurClip == TraceClip ? 1 : 0;
				YawOk += FMath::Abs(FMath::UnwindRadians(CtlSim.State.AnimYaw - C.D(Row, TEXT("nayaw")))) < 1e-4 ? 1 : 0;
				GrindOk += CtlSim.State.GrindType == C.I(Row, TEXT("grind")) ? 1 : 0;
				PrevTraceClip = TraceClip;
			}
		}
		else if (Kind == 2)
		{
			GameExitGrind = C.I(Row, TEXT("ngrind"));
			if (Ctl)
			{
				CtlSim.SetControl(ERiderControl::Spin, TEXT("live exit"));
				ExitGrind = CtlSim.State.GrindType;
			}
		}
		else
		{
			R.Board = BoardFrom(C, Row, TEXT("a"));
			R.BoxFilter.Cur = C.D(Row, TEXT("filter"));
			FilterAtReset += FMath::IsNearlyEqual(R.BoxFilter.Cur, FRiderState().RailBox.Cur, 1e-6) ? 1 : 0;
			++CatchRows;
			const int32 Sample = C.I(Row, TEXT("sample"));
			const int32 NGrind = C.I(Row, TEXT("ngrind"));
			if (NGrind != 0)
			{
				GameCatch = Sample;
				GameGrind = NGrind;
			}
			if (CatchSample == INDEX_NONE && CatchTest(R, World, false))
			{
				CatchSample = Sample;
				CatchGrind = R.GrindType;
				CatchVelErr = (R.Vel - C.V(Row, TEXT("nv"))).Size();
			}
		}
	}
	CatchBefore = (CatchSample != INDEX_NONE && GameCatch != INDEX_NONE) ? GameCatch - CatchSample : -1;
	QErr.Sort();
	const double Median = QErr.Num() ? QErr[QErr.Num() / 2] : 0.0;
	Report(this, FString::Printf(TEXT("live: %d rail steps; query < 0.05 cm %d (median %.4f cm), surface %d; slide exact %d/%d; ")
		TEXT("catch at sample %d (grind field %d, %d samples earlier), velocity error %.4f cm/s, grind %d (game %d)"),
		Steps, QueryExact, Median, SurfaceOk, SlideExact, SlideSteps, CatchSample, GameCatch, CatchBefore, CatchVelErr, CatchGrind, GameGrind));
	TestEqual(TEXT("rail steps in the trace"), Steps, 367);
	TestTrue(TEXT("query exact on >= 338 steps"), QueryExact >= 338);
	TestEqual(TEXT("surface on every step"), SurfaceOk, Steps);
	TestTrue(TEXT("slide exact on >= 292 steps"), SlideExact >= 292);
	TestTrue(TEXT("catch at the grind-field sample or the torn sample before it"), CatchBefore == 0 || CatchBefore == 1);
	TestTrue(TEXT("catch velocity identical to the recorded snap (same game step)"), CatchVelErr < 0.01);
	TestEqual(TEXT("catch grind type"), CatchGrind, GameGrind);
	TestTrue(TEXT("catch rows present"), CatchRows > 0);
	TestEqual(TEXT("trace box filter before the first rail = the port's reset value (1)"), FilterAtReset, CatchRows);
	if (!bHaveR22)
	{
		SSXTest::Skip(*this, TEXT("orientation / switch replay skipped: regenerate SimData/gari.rail_live.csv (tools/make_rail_reference.py)"));
		return true;
	}
	OErr.Sort();
	Report(this, FString::Printf(TEXT("live R22: orientation within 0.05 deg %d/%d (median %.5f deg), rail up %d/%d; control replay %d steps: clip %d, +0x46E0 %d, grind type %d; switch at sample %d; exit grind type %d (game %d)"),
		OrientExact, OrientSteps, OErr.Num() ? OErr[OErr.Num() / 2] : 0.0, RailUpExact, OrientSteps, CtlSteps, ClipOk, YawOk, GrindOk,
		SwitchSample, ExitGrind, GameExitGrind));
	TestTrue(TEXT("orientation within 0.05 deg on >= 315 steps"), OrientExact >= 315);
	TestTrue(TEXT("rail up on >= 315 steps"), RailUpExact >= 315);
	TestEqual(TEXT("switch at sample 6588"), SwitchSample, 6588);
	TestEqual(TEXT("clip on every step"), ClipOk, CtlSteps);
	TestEqual(TEXT("+0x46E0 on every step"), YawOk, CtlSteps);
	TestEqual(TEXT("grind type on every step"), GrindOk, CtlSteps);
	TestEqual(TEXT("exit grind type (forced completion)"), ExitGrind, GameExitGrind);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailRulesTest, "SSX.Rail.Rules", RailTestFlags)

bool FSSXRailRulesTest::RunTest(const FString& Parameters)
{
	using namespace SSXRail;
	FPatchWorld World;
	{
		TArray<FSimRailSegment> Segs = { StraightSegment(FVector(0, 0, 100), FVector(0, 2000, 100), 0) };
		TArray<FSimRailSpline> Spl;
		Spl.AddDefaulted();
		Spl[0].Surface = 12;
		Spl[0].SegmentCount = 1;
		World.SetRails(MoveTemp(Segs), MoveTemp(Spl));
	}
	FRiderRailQuery Q;
	FRiderRailHit H;
	Q.Origin = FVector(50, 500, 120);
	TestTrue(TEXT("query hits"), World.QueryRail(Q, H));
	TestTrue(TEXT("nearest point (golden search: t within 0.0005)"), (H.Point - FVector(0, 500, 100)).Size() < 1.5);
	TestEqual(TEXT("surface from the spline"), H.Surface, 12);
	Q.Origin = FVector(0, 500, 100 + 301);
	TestFalse(TEXT("300 cm AABB cull"), World.QueryRail(Q, H));
	World.SetRailEnabled(0, false);
	Q.Origin = FVector(0, 500, 110);
	TestFalse(TEXT("disabled rails are invisible"), World.QueryRail(Q, H));
	World.SetRailEnabled(0, true);

	auto OnRail = [](const FVector& Pos, const FVector& Vel)
	{
		FRailRider R;
		R.Pos = Pos;
		R.Vel = Vel;
		R.Board.Origin = Pos;
		R.BoxFilter.Cur = 1.0;
		R.RailTime = 1.0;
		return R;
	};
	{
		FRailRider R = OnRail(FVector(0, 5000, 100), FVector(0, 800, 0));
		TestTrue(TEXT("no hit"), SlideStep(R, World) == ESlideResult::NoHit);
		TestEqual(TEXT("no push on a no-hit exit"), R.Vel, FVector(0, 800, 0));
		ExitRail(R);
		TestEqual(TEXT("exit filter value"), R.BoxFilter.Cur, FilterExitValue);
	}
	{
		FRailRider R = OnRail(FVector(0, 2155, 70), FVector(0, 800, 0));
		R.Board.Origin = FVector(0, 2155, 70);
		TestTrue(TEXT("off the end"), SlideStep(R, World) == ESlideResult::FellOff);
		TestTrue(TEXT("push along t"), (R.Vel - FVector(0, 800 - FallPush, 0)).Size() < 1e-3);
	}
	{
		FRailRider R = OnRail(FVector(-60, 1000, 70), FVector(0, 800, 0));
		R.Board.R2 = FVector(1, 0, 0);
		TestTrue(TEXT("fell off sideways"), SlideStep(R, World) == ESlideResult::FellOff);
		FRiderRailQuery Q2;
		Q2.Origin = FVector(-60, 1000, 70);
		FRiderRailHit H2;
		World.QueryRail(Q2, H2);
		FVector U = (H2.Point - Q2.Origin).GetSafeNormal();
		if (FVector::DotProduct(FVector(0, 800, 0), U) < 0.0)
		{
			U = (U - U.Y * FVector(0, 1, 0)).GetSafeNormal();
		}
		TestTrue(TEXT("sideways push along d"), (R.Vel - (FVector(0, 800, 0) - FallPush * U)).Size() < 1e-2);
	}
	{
		FRailRider R = OnRail(FVector(0, 1000, 70), FVector(0, 600, 0));
		R.BoostStrength = 1.0;
		TestTrue(TEXT("on rail"), SlideStep(R, World) == ESlideResult::OnRail);
		TestTrue(TEXT("boost along the travel direction"), FMath::IsNearlyEqual(R.Vel.Y, 600.0 + Boost / 60.0, 1e-3));
	}

	// FRiderSim glue: ride down a 15 deg slope into a rail 20 cm above the snow along the fall line
	{
		FPatchWorld Slope;
		SSXRiderSetup::BuildSlopeWorld(Slope, 15.0, 1);
		const double Ta = FMath::Tan(FMath::DegreesToRadians(15.0));
		const FVector N(0.0, FMath::Sin(FMath::DegreesToRadians(15.0)), FMath::Cos(FMath::DegreesToRadians(15.0)));
		const FVector A = FVector(0, 600, -600 * Ta) + 20.0 * N, B = FVector(0, 2600, -2600 * Ta) + 20.0 * N;
		TArray<FSimRailSegment> Segs = { StraightSegment(A, B, 0) };
		TArray<FSimRailSpline> Spl;
		Spl.AddDefaulted();
		Spl[0].Surface = 13;
		Spl[0].SegmentCount = 1;
		Slope.SetRails(MoveTemp(Segs), MoveTemp(Spl));
		FRiderSim Sim;
		Sim.SetWorld(&Slope);
		RegisterGroundStates(Sim);
		RegisterAirStates(Sim);
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector(0, 900, -900 * Ta), 0.0, false);
		int32 CatchFrame = -1, LeaveFrame = -1, Grind = 0, Surface = 0;
		ERiderControl LeaveControl = ERiderControl::None;
		double ExitFilter = 0.0;
		for (int32 F = 0; F < 400 && LeaveFrame < 0; ++F)
		{
			const ERiderMotion Before = Sim.State.Motion;
			Sim.Step(FRiderInputWord());
			if (CatchFrame < 0 && Sim.State.Motion == ERiderMotion::RailSlide)
			{
				CatchFrame = F;
				Grind = Sim.State.GrindType;
				Surface = Sim.State.SurfaceId;
			}
			if (Before == ERiderMotion::RailSlide && Sim.State.Motion != ERiderMotion::RailSlide)
			{
				LeaveFrame = F;
				LeaveControl = Sim.State.Control;
				ExitFilter = Sim.State.RailBox.Cur;
			}
		}
		Report(this, FString::Printf(TEXT("sim glue: catch frame %d (grind %d, surface %d), leave frame %d (control %s, filter %.4f), y %.1f"),
			CatchFrame, Grind, Surface, LeaveFrame, LexToString(LeaveControl), ExitFilter, Sim.State.Position.Y));
		TestTrue(TEXT("caught the rail"), CatchFrame >= 0);
		TestEqual(TEXT("grind type 50/50"), Grind, 1);
		TestEqual(TEXT("rail surface"), Surface, 13);
		TestTrue(TEXT("left the rail"), LeaveFrame > CatchFrame);
		TestTrue(TEXT("leave -> Air"), Sim.State.Motion == ERiderMotion::Air);
		TestTrue(TEXT("leave -> Spin control"), LeaveControl == ERiderControl::Spin);
		TestEqual(TEXT("steer zeroed"), Sim.State.Steer.Target, 0.0);
		TestEqual(TEXT("exit filter value"), ExitFilter, FilterExitValue);
	}
	return true;
}

namespace
{
	SSXGround::FRailSlideControl* EnterRailControl(FRiderSim& Sim, int32 Gt, int32 BoardClass = 0, int32 Stance = 0)
	{
		RegisterGroundStates(Sim);
		RegisterAirStates(Sim);
		Sim.Profile.BoardClass = BoardClass;
		Sim.Profile.Stance = Stance;
		Sim.Reset();
		Sim.SetOrientationFromAxes(FVector(0, 1, 0), FVector::UpVector);
		Sim.State.GrindType = Gt;
		if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim)) { Ch->PlayRequests.Reset(); Ch->LayerYawAdd = 0.0; }
		Sim.SetControl(ERiderControl::RailSlide, TEXT("test"));
		return static_cast<SSXGround::FRailSlideControl*>(Sim.GetControlState(ERiderControl::RailSlide));
	}

	int32 LastClip(const FRiderSim& Sim)
	{
		const SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim);
		return (Ch && Ch->PlayRequests.Num()) ? Ch->PlayRequests.Last() : 0;
	}

	double TakeLayerYaw(const FRiderSim& Sim)
	{
		SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim);
		const double A = Ch ? Ch->LayerYawAdd : 0.0;
		if (Ch) { Ch->LayerYawAdd = 0.0; }
		return A;
	}

	FRiderInputWord RailWord(double RailSpin, double Crouch = 0.0, bool bJump = false)
	{
		FRiderInputWord W;
		W.RailSpin = RailSpin;
		W.Crouch = Crouch;
		W.bJumpPressed = W.bJumpHeld = bJump;
		return W;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailSwitchTest, "SSX.Rail.Switch", RailTestFlags)

bool FSSXRailSwitchTest::RunTest(const FString& Parameters)
{
	using namespace SSXGround;
	{
		FRiderSim Sim;
		FRailSlideControl* Ctl = EnterRailControl(Sim, 3);
		const FVector F = Sim.State.BasisForward;
		Report(this, FString::Printf(TEXT("enter type 3: clip 0x%X, anim yaw %.6f, body forward (%.4f %.4f %.4f), layer yaw %.6f (model 0x256, -1.570796, -90 deg, -1.570796)"),
			LastClip(Sim), Sim.State.AnimYaw, F.X, F.Y, F.Z, TakeLayerYaw(Sim)));
		TestEqual(TEXT("enter clip RS_FSANTIC"), LastClip(Sim), 0x256);
		TestTrue(TEXT("anim yaw -pi/2"), FMath::IsNearlyEqual(Sim.State.AnimYaw, -UE_DOUBLE_HALF_PI, 1e-9));
		TestTrue(TEXT("body yaw -90 deg: nose turns to the right"), F.Equals(FVector(1, 0, 0), 1e-6));   // 0x128E90, 0x129008
		TestFalse(TEXT("stance 0 kept"), Sim.State.bStanceGoofy);
		Ctl->Update(Sim, RailWord(-1.0));
		TestEqual(TEXT("switch clip RSR_FS2REG"), LastClip(Sim), 0x25C);
		TestTrue(TEXT("switch anim yaw 0"), FMath::IsNearlyEqual(Sim.State.AnimYaw, 0.0, 1e-12));
		TestTrue(TEXT("layer yaw +pi/2"), FMath::IsNearlyEqual(TakeLayerYaw(Sim), UE_DOUBLE_HALF_PI, 1e-12));
		TestEqual(TEXT("grind type unchanged until completion"), Sim.State.GrindType, 3);
		for (int32 F2 = 0; F2 < 12; ++F2) { Ctl->Update(Sim, RailWord(0.0)); }
		TestEqual(TEXT("13 frames in: still type 3 (live)"), Sim.State.GrindType, 3);
		Sim.SetControl(ERiderControl::Spin, TEXT("test"));
		TestEqual(TEXT("exit completes the pending switch: type 1"), Sim.State.GrindType, 1);
		TestEqual(TEXT("completion plays the cycle clip 0x254"), LastClip(Sim), 0x254);
	}
	{
		FRiderSim Sim;
		FRailSlideControl* Ctl = EnterRailControl(Sim, 1);
		const int32 Frames = RailSwitchFrames(Sim, SSXAnim::FindChannel(Sim), 0x25A);
		TestEqual(TEXT("bx switch = 28 frames"), Frames, 28);
		const int32 Cycle[5] = { 1, 4, 2, 3, 1 };
		const int32 Clips[4] = { 0x25A, 0x25F, 0x259, 0x25C };
		const double Yaws[4] = { UE_DOUBLE_HALF_PI, -UE_DOUBLE_PI, -UE_DOUBLE_HALF_PI, 0.0 };
		int32 Done = 0;
		for (int32 K = 0; K < 4; ++K)
		{
			TestEqual(*FString::Printf(TEXT("cycle %d grind type"), K), Sim.State.GrindType, Cycle[K]);
			Ctl->Update(Sim, RailWord(-1.0));
			TestEqual(*FString::Printf(TEXT("cycle %d clip"), K), LastClip(Sim), Clips[K]);
			TestTrue(*FString::Printf(TEXT("cycle %d anim yaw"), K), FMath::IsNearlyEqual(Sim.State.AnimYaw, Yaws[K], 1e-12));
			int32 N = 0;
			while (Sim.State.GrindType == Cycle[K] && N < 100) { Ctl->Update(Sim, RailWord(-1.0)); ++N; }
			Done += N == Frames ? 1 : 0;
		}
		TestEqual(TEXT("each switch completes after 28 frames"), Done, 4);
		TestEqual(TEXT("back to 50/50"), Sim.State.GrindType, 1);
		TestTrue(TEXT("type 2 completion: stance 1 (BX), leaving 2: stance 0"), !Sim.State.bStanceGoofy);
	}
	{
		FRiderSim Sim;
		FRailSlideControl* Ctl = EnterRailControl(Sim, 1);
		Sim.State.Crouch.Cur = 0.5;
		Ctl->Update(Sim, RailWord(1.0));
		TestEqual(TEXT("crouch != 0: no switch"), LastClip(Sim), 0x254);
		Sim.State.Crouch.Cur = 0.0;
		Ctl->Update(Sim, RailWord(1.0));
		TestEqual(TEXT("Left: RSR_REG2FS 0x258"), LastClip(Sim), 0x258);
		TestTrue(TEXT("Left: anim yaw -pi/2"), FMath::IsNearlyEqual(Sim.State.AnimYaw, -UE_DOUBLE_HALF_PI, 1e-12));
		TestTrue(TEXT("Left: layer yaw -pi/2"), FMath::IsNearlyEqual(TakeLayerYaw(Sim), -UE_DOUBLE_HALF_PI, 1e-12));
		Ctl->Update(Sim, RailWord(0.0, 0.0, true));
		TestEqual(TEXT("jump during a switch: latched, still on the rail control"), Sim.State.Control, ERiderControl::RailSlide);
		for (int32 F = 0; F < 40 && Sim.State.Control == ERiderControl::RailSlide; ++F) { Ctl->Update(Sim, RailWord(0.0, 0.0, true)); }
		TestEqual(TEXT("completed to type 3"), Sim.State.GrindType, 3);
		TestEqual(TEXT("held jump -> Prewind after completion"), Sim.State.Control, ERiderControl::Prewind);
	}
	{
		FRiderSim Sim;
		Sim.Profile.Stance = 1;
		EnterRailControl(Sim, 4, 0, 1);
		TestFalse(TEXT("BX goofy type 4: stance flipped to 0"), Sim.State.bStanceGoofy);
		TestTrue(TEXT("type 4: anim yaw +pi/2"), FMath::IsNearlyEqual(Sim.State.AnimYaw, UE_DOUBLE_HALF_PI, 1e-12));
		TestEqual(TEXT("type 4 clip RS_BSANTIC"), LastClip(Sim), 0x257);
		TestTrue(TEXT("flip pi then +pi/2: nose (0,1,0) -> (1,0,0)"), Sim.State.BasisForward.Equals(FVector(1, 0, 0), 1e-6));
		FRiderSim Ag;
		EnterRailControl(Ag, 1, 2, 1);
		TestEqual(TEXT("Alpine goofy: type 1 becomes 2"), Ag.State.GrindType, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailScoreTest, "SSX.Rail.Score", RailTestFlags)

bool FSSXRailScoreTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	{
		FTrickScore S;
		S.Land(false, 1);
		S.RailSpin(0);
		int32 Spin = 0;
		for (int32 F = 0; F < 120; ++F)
		{
			if (F == 10 || F == 50) { Spin += 90; S.RailSpin(Spin); }
			S.RailFrame(900.0);
		}
		Report(this, FString::Printf(TEXT("score: V %.7f (model 0.3699543), rail frames %d"), S.Value, S.RailFrames));
		TestTrue(TEXT("V 0.3699543"), FMath::IsNearlyEqual(S.Value, 0.3699543, 1e-6));
		const double Reward = S.StartSegment(true, 2, false);
		Report(this, FString::Printf(TEXT("exit: points %d (model 2510), reward %.7f (model 0.2510712), M %.2f, record w1 0x%08X"),
			S.LastPoints, Reward, S.PointsMult, S.LastRecord.W1));
		TestEqual(TEXT("2510 points"), S.LastPoints, 2510);
		TestEqual(TEXT("score banked"), S.Score, 2510);
		TestTrue(TEXT("reward 0.2510712"), FMath::IsNearlyEqual(Reward, 0.2510712, 1e-6));
		TestEqual(TEXT("M 1.3 after a switch exit"), S.PointsMult, RailStartPointsMult);
		TestEqual(TEXT("record rail kind = grind type at exit (2)"), int32((S.LastRecord.W1 >> 12) & 15), 2);
		TestEqual(TEXT("record spin index 1 (180)"), int32((S.LastRecord.W1 >> 4) & 15), 1);
		TestEqual(TEXT("combo +1 (2 s > 1 s)"), S.Combo, 1);
		TestEqual(TEXT("new segment type = grind type"), S.SegmentType, 2);
		TestEqual(TEXT("rail frames reset"), S.RailFrames, -1);
		TestEqual(TEXT("type 2 message pushed"), S.Messages.Find(SSXMsg::Points) != nullptr, true);
	}
	{
		FTrickScore S;
		S.Land(false, 1);
		S.RailSpin(90);
		TestEqual(TEXT("quarter switch: 0"), S.Value, 0.0);
		S.RailSpin(0); S.RailSpin(-90); S.RailSpin(-180);
		TestTrue(TEXT("net -180: 0.06995432 once"), FMath::IsNearlyEqual(S.Value, 0.06995432, 1e-9));
		S.RailSpin(-90); S.RailSpin(-180);
		TestTrue(TEXT("again -180: nothing more"), FMath::IsNearlyEqual(S.Value, 0.06995432, 1e-9));
	}
	{
		FTrickScore S;
		S.StartSegment(false, 0, false);
		for (int32 F = 1; F <= 36; ++F) { S.UpdateAngles(10.0 * F, 0.0); }
		const double Meter = S.Land(false, 3);
		TestTrue(TEXT("to-rail landing pays the air trick (360 + To Rail)"), Meter > 0.0);
		TestEqual(TEXT("segment type = grind type 3"), S.SegmentType, 3);
		TestEqual(TEXT("rail frames 0"), S.RailFrames, 0);
	}
	// sim glue (slope rail of SSX.Rail.Rules): the rail gives a score and pays the meter at the exit
	{
		FPatchWorld Slope;
		SSXRiderSetup::BuildSlopeWorld(Slope, 15.0, 1);
		const double Ta = FMath::Tan(FMath::DegreesToRadians(15.0));
		const FVector N(0.0, FMath::Sin(FMath::DegreesToRadians(15.0)), FMath::Cos(FMath::DegreesToRadians(15.0)));
		const FVector A = FVector(0, 600, -600 * Ta) + 20.0 * N, B = FVector(0, 2600, -2600 * Ta) + 20.0 * N;
		TArray<FSimRailSegment> Segs = { StraightSegment(A, B, 0) };
		TArray<FSimRailSpline> Spl;
		Spl.AddDefaulted();
		Spl[0].Surface = 13;
		Spl[0].SegmentCount = 1;
		Slope.SetRails(MoveTemp(Segs), MoveTemp(Spl));
		FRiderSim Sim;
		Sim.SetWorld(&Slope);
		RegisterGroundStates(Sim);
		RegisterAirStates(Sim);
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector(0, 900, -900 * Ta), 0.0, false);
		FAirContext* Air = GetAirContext(Sim);
		int32 RailSteps = 0, MaxRailFrames = -1, ExitGrind = -1;
		double MeterBefore = 0.0, MeterAfter = 0.0, ValueAtExit = 0.0;
		bool bLeft = false;
		for (int32 F = 0; F < 400 && !bLeft; ++F)
		{
			const ERiderMotion Before = Sim.State.Motion;
			MeterBefore = Sim.State.BoostMeter;
			ValueAtExit = Air->Score.Value;
			Sim.Step(FRiderInputWord());
			RailSteps += Sim.State.Motion == ERiderMotion::RailSlide ? 1 : 0;
			MaxRailFrames = FMath::Max(MaxRailFrames, Air->Score.RailFrames);
			if (Before == ERiderMotion::RailSlide && Sim.State.Motion != ERiderMotion::RailSlide)
			{
				bLeft = true;
				MeterAfter = Sim.State.BoostMeter;
				ExitGrind = int32((Air->Score.LastRecord.W1 >> 12) & 15);
			}
		}
		const double Gain = BoostGain(Sim.Profile);
		Report(this, FString::Printf(TEXT("sim glue: %d rail steps, rail frames %d, V at exit %.5f, points %d, score %d, meter %.5f -> %.5f (expect +%.5f), record kind %d, grind type in the air %d"),
			RailSteps, MaxRailFrames, ValueAtExit, Air->Score.LastPoints, Air->Score.Score, MeterBefore, MeterAfter,
			MeterScale * ValueAtExit * Gain, ExitGrind, Sim.State.GrindType));
		TestTrue(TEXT("left the rail"), bLeft);
		TestTrue(TEXT("rail frames counted"), MaxRailFrames >= RailSteps - 1 && MaxRailFrames > 30);
		TestTrue(TEXT("the rail scores points"), Air->Score.Score > 0 && Air->Score.LastPoints == FTrickScore::PointsFor(ValueAtExit, 1.0));
		TestTrue(TEXT("exit pays the meter 0.6786545 V x gain"),
			FMath::IsNearlyEqual(MeterAfter - MeterBefore, FMath::Min(1.0, MeterBefore + MeterScale * ValueAtExit * Gain) - MeterBefore, 2e-3));
		TestEqual(TEXT("record kind = the grind type (50/50)"), ExitGrind, 1);
		TestEqual(TEXT("grind type kept into the air (cleared on landing)"), Sim.State.GrindType, 1);
		TestTrue(TEXT("Spin seeds zeroed at the rail exit"), Air->Spin.SpinRate == 0.0 && Air->Spin.FlipRate == 0.0);
		for (int32 F = 0; F < 400 && Sim.State.Motion != ERiderMotion::Ground; ++F) { Sim.Step(FRiderInputWord()); }
		TestEqual(TEXT("landing clears the grind type"), Sim.State.GrindType, 0);
	}
	return true;
}

// 0x125CB8
namespace
{
	void OneRail(FPatchWorld& World, const FVector& A, const FVector& B)
	{
		TArray<FSimRailSegment> Segs = { StraightSegment(A, B, 0) };
		TArray<FSimRailSpline> Spl;
		Spl.AddDefaulted();
		Spl[0].Surface = 13;
		Spl[0].SegmentCount = 1;
		World.SetRails(MoveTemp(Segs), MoveTemp(Spl));
	}

	SSXRail::FRailRider CatchRider(const FVector& Origin, const FVector& Vel, double Filter, const FVector& F = FVector(0, 1, 0))
	{
		SSXRail::FRailRider R;
		R.Pos = Origin;
		R.Vel = Vel;
		R.Board.Origin = Origin;
		R.Board.R0 = F;
		R.Board.R1 = FVector::UpVector;
		R.Board.R2 = FVector::CrossProduct(F, FVector::UpVector);
		R.BoxFilter.Cur = Filter;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailAcquireTest, "SSX.Rail.Acquire", RailTestFlags)

bool FSSXRailAcquireTest::RunTest(const FString& Parameters)
{
	using namespace SSXRail;
	const double E = 0.01;
	// a short rail along +Y (golden-search error along it < 0.1 cm) at z = 100
	FPatchWorld Along;
	OneRail(Along, FVector(0, 900, 100), FVector(0, 1100, 100));
	auto TryAlong = [&](const FVector& Off, double Filter)
	{
		FRailRider R = CatchRider(FVector(0, 1000, 70) + Off, FVector(0, 800, 0), Filter);
		return CatchTest(R, Along, false);
	};
	// height: the rail point may be from 15 cm below the board origin (c.z = -45) to 75 cm above it (c.z = +45)
	TestTrue(TEXT("rail 74.99 cm above the board: catch"), TryAlong(FVector(0, 0, -(45.0 - E)), 1.0));
	TestFalse(TEXT("rail 75.01 cm above the board: no catch"), TryAlong(FVector(0, 0, -(45.0 + E)), 1.0));
	TestTrue(TEXT("rail 14.99 cm below the board: catch"), TryAlong(FVector(0, 0, 45.0 - E), 1.0));
	TestFalse(TEXT("rail 15.01 cm below the board: no catch"), TryAlong(FVector(0, 0, 45.0 + E), 1.0));
	TestTrue(TEXT("height window does not depend on the filter (r = 0)"), TryAlong(FVector(0, 0, 45.0 - E), 0.0));
	// across the board: 45 cm at r = 1, 27 cm at r = 0
	TestTrue(TEXT("across 44.99 cm, r = 1: catch"), TryAlong(FVector(45.0 - E, 0, 0), 1.0));
	TestFalse(TEXT("across 45.01 cm, r = 1: no catch"), TryAlong(FVector(45.0 + E, 0, 0), 1.0));
	TestTrue(TEXT("across 26.99 cm, r = 0: catch"), TryAlong(FVector(-(27.0 - E), 0, 0), 0.0));
	TestFalse(TEXT("across 27.01 cm, r = 0: no catch"), TryAlong(FVector(-(27.0 + E), 0, 0), 0.0));
	TestTrue(TEXT("across 40 cm: caught with the reset filter (1), not with the old port's 0"),
		TryAlong(FVector(40, 0, 0), FRiderState().RailBox.Cur) && !TryAlong(FVector(40, 0, 0), 0.0));
	TestFalse(TEXT("re-catch filter -0.25 right after an exit: across 22.5 cm"), TryAlong(FVector(22.6, 0, 0), FilterExitValue));
	{
		FRailRider R = CatchRider(FVector(0, 1000, 70), FVector(0, 800, 0), 1.0);
		TestFalse(TEXT("a grab / tweak / uber before its landable flag blocks the catch"), CatchTest(R, Along, true));
	}

	FPatchWorld Across;
	OneRail(Across, FVector(-100, 1000, 100), FVector(100, 1000, 100));
	auto TryAhead = [&](double Ahead, double Filter, FRailRider* Out = nullptr)
	{
		FRailRider R = CatchRider(FVector(0, 1000 - Ahead, 70), FVector(0, 800, 0), Filter);
		const bool b = CatchTest(R, Across, false);
		if (Out) { *Out = R; }
		return b;
	};
	TestTrue(TEXT("rail 134.99 cm ahead, r = 1: catch"), TryAhead(135.0 - E, 1.0));
	TestFalse(TEXT("rail 135.01 cm ahead, r = 1: no catch"), TryAhead(135.0 + E, 1.0));
	TestTrue(TEXT("rail 71.99 cm ahead, r = 0: catch"), TryAhead(72.0 - E, 0.0));
	TestFalse(TEXT("rail 72.01 cm ahead, r = 0: no catch"), TryAhead(72.0 + E, 0.0));
	TestTrue(TEXT("rail 0.19 cm behind: catch (c.v_hat >= -0.2)"), TryAhead(-0.19, 1.0));
	TestFalse(TEXT("rail 0.21 cm behind: no catch"), TryAhead(-0.21, 1.0));
	{
		FRailRider R;
		TryAhead(50.0, 1.0, &R);
		TestTrue(TEXT("perpendicular catch: speed floor 555.56 along the rail"), (R.Vel - MinSpeed * FVector(1, 0, 0)).Size() < 1e-3);
		TestEqual(TEXT("perpendicular catch: board across the rail -> grind type 3 (a = +90 deg)"), R.GrindType, 3);
	}

	{
		FRailRider R = CatchRider(FVector(0, 1000, 70), FVector(800, 800, 0), 1.0);
		TestTrue(TEXT("45 deg approach caught"), CatchTest(R, Along, false));
		TestTrue(TEXT("45 deg approach: speed = v.t = 800"), (R.Vel - FVector(0, 800, 0)).Size() < 1e-3);
		FRailRider S = CatchRider(FVector(0, 1000, 70), FVector(0, 300, 0), 1.0);
		TestTrue(TEXT("slow rider caught"), CatchTest(S, Along, false));
		TestTrue(TEXT("slow rider: |v| = 300 kept (the floor never raises the speed)"), (S.Vel - FVector(0, 300, 0)).Size() < 1e-3);
		FPatchWorld Steep;
		OneRail(Steep, FVector(0, 900, 150), FVector(0, 1100, 50));
		FRailRider D = CatchRider(FVector(0, 1000, 70), FVector(0, 900, -300), 1.0);
		TestTrue(TEXT("sloped rail caught"), CatchTest(D, Steep, false));
		const FVector T = FVector(0, 200, -100).GetSafeNormal();
		const double Sp = FMath::Min(FVector(0, 900, -300).Size(), FMath::Max(FVector(0, 900, -300).Dot(T), MinSpeed));
		const FVector Expect(0, Sp * T.Y, Sp * T.Z * CatchVzKeep);
		TestTrue(TEXT("sloped rail: v = speed t with v.z x 0.1"), (D.Vel - Expect).Size() < 1e-2);
	}
	{
		auto Grind = [&](double ThetaDeg)
		{
			const double Th = FMath::DegreesToRadians(ThetaDeg);
			FRailRider R = CatchRider(FVector(0, 1000, 70), FVector(0, 800, 0), 1.0, FVector(FMath::Sin(Th), FMath::Cos(Th), 0));
			return CatchTest(R, Along, false) ? R.GrindType : -1;
		};
		TestEqual(TEXT("a = 29.9 deg: 50/50 (1)"), Grind(-29.9), 1);
		TestEqual(TEXT("a = 30.1 deg: 3"), Grind(-30.1), 3);
		TestEqual(TEXT("a = -30.1 deg: 4"), Grind(30.1), 4);
		TestEqual(TEXT("a = -149.9 deg: 4"), Grind(149.9), 4);
		TestEqual(TEXT("a = -150.1 deg: switch 50/50 (2)"), Grind(150.1), 2);
	}

	{
		FRiderSim Sim;
		RegisterGroundStates(Sim);
		RegisterAirStates(Sim);
		Sim.Reset();
		const FApproachFilter& F = Sim.State.RailBox;
		TestTrue(TEXT("reset: rail box filter (1, 0, 1) as in every live rider before its first rail"), F.Cur == 1.0 && F.Rate == 0.0 && F.Target == 1.0);
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 100.0, false);
		TestTrue(TEXT("spawn keeps the rail box filter (0x11CAE8 zeroes +0x1F0..+0x280 only)"), Sim.State.RailBox.Cur == 1.0 && Sim.State.RailBox.Target == 1.0);
		Sim.State.RailBox.Cur = FilterExitValue;
		Sim.State.RailBox.Target = 1.0;
		Sim.State.RailBox.Rate = FilterRate;
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 100.0, false);
		TestEqual(TEXT("respawn right after an exit keeps the exit value"), Sim.State.RailBox.Cur, FilterExitValue);
		int32 N = 0;
		FApproachFilter G = Sim.State.RailBox;
		while (G.Cur < 1.0 && N < 100) { G.Tick(1.0 / 60.0); ++N; }
		TestTrue(TEXT("exit filter back to 1 after 45 frames (0.75 s)"), N == 45 || N == 46);
	}

	auto SideRide = [&](double Offset, int32& OutFrame, int32& OutGrind)
	{
		FPatchWorld Slope;
		SSXRiderSetup::BuildSlopeWorld(Slope, 15.0, 1);
		const double Ta = FMath::Tan(FMath::DegreesToRadians(15.0));
		const FVector N(0.0, FMath::Sin(FMath::DegreesToRadians(15.0)), FMath::Cos(FMath::DegreesToRadians(15.0)));
		OneRail(Slope, FVector(Offset, 600, -600 * Ta) + 20.0 * N, FVector(Offset, 2600, -2600 * Ta) + 20.0 * N);
		FRiderSim Sim;
		Sim.SetWorld(&Slope);
		RegisterGroundStates(Sim);
		RegisterAirStates(Sim);
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector(0, 900, -900 * Ta), 0.0, false);
		OutFrame = -1;
		OutGrind = 0;
		for (int32 F = 0; F < 200 && OutFrame < 0; ++F)
		{
			Sim.Step(FRiderInputWord());
			if (Sim.State.Motion == ERiderMotion::RailSlide)
			{
				OutFrame = F;
				OutGrind = Sim.State.GrindType;
			}
		}
	};
	int32 F40 = -1, G40 = 0, F50 = -1, G50 = 0, F0 = -1, G0 = 0;
	SideRide(40.0, F40, G40);
	SideRide(50.0, F50, G50);
	SideRide(0.0, F0, G0);
	Report(this, FString::Printf(TEXT("acquire: side ride 0 cm -> catch frame %d (grind %d), 40 cm -> %d (grind %d), 50 cm -> %d"), F0, G0, F40, G40, F50));
	TestTrue(TEXT("rail straight ahead caught"), F0 >= 0);
	TestTrue(TEXT("rail 40 cm to the side caught from the ground"), F40 >= 0);
	TestEqual(TEXT("40 cm side catch: 50/50"), G40, 1);
	TestTrue(TEXT("rail 50 cm to the side not caught"), F50 < 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailTrackRidesTest, "SSX.Rail.TrackRides", RailTestFlags)

bool FSSXRailTrackRidesTest::RunTest(const FString& Parameters)
{
	static const TCHAR* Tracks[] = { TEXT("gari"), TEXT("mesa"), TEXT("alaska"), TEXT("snow"), TEXT("merquer"),
		TEXT("elysium"), TEXT("aloha"), TEXT("pipe"), TEXT("megaple"), TEXT("untrack") };
	constexpr int32 PerTrack = 8;
	int32 TracksRun = 0, Tries = 0, Caught = 0, Wrong = 0;
	for (const TCHAR* Track : Tracks)
	{
		FPatchWorld World;
		FString Err;
		if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(Track), &Err) || World.NumRailSplines() == 0)
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s skipped: %s"), Track, *Err));
			continue;
		}
		++TracksRun;
		TArray<int32> Enabled;
		for (int32 K = 0; K < World.NumRailSplines(); ++K)
		{
			const FSimRailSpline& S = World.GetRailSpline(K);
			if (S.bEnabled && S.SegmentCount > 0)
			{
				Enabled.Add(K);
			}
		}
		int32 TrackTries = 0, TrackCaught = 0;
		FString Misses;
		for (int32 I = 0; I < Enabled.Num() && TrackTries < PerTrack; I += FMath::Max(1, Enabled.Num() / PerTrack))
		{
			const FSimRailSpline& S = World.GetRailSpline(Enabled[I]);
			double Total = 0.0;
			for (int32 K = 0; K < S.SegmentCount; ++K)
			{
				Total += World.GetRailSegment(S.FirstSegment + K).Length;
			}
			int32 Mid = S.FirstSegment;
			double Into = 0.5 * Total;
			while (Mid < S.FirstSegment + S.SegmentCount - 1 && Into > World.GetRailSegment(Mid).Length)
			{
				Into -= World.GetRailSegment(Mid).Length;
				++Mid;
			}
			const FSimRailSegment& Seg = World.GetRailSegment(Mid);
			const double Tm = Seg.Length > 0.0 ? FMath::Clamp(Into / Seg.Length, 0.0, 1.0) : 0.5;
			const FVector P = Seg.Evaluate(Tm);
			const FVector T = Seg.Derivative(Tm).GetSafeNormal();
			if (FMath::Abs(T.Z) > 0.5 || Total < 300.0)
			{
				continue;
			}
			++TrackTries;
			FRiderSim Sim;
			Sim.SetWorld(&World);
			RegisterGroundStates(Sim);
			RegisterAirStates(Sim);
			Sim.Reset();
			const FVector H = FVector(T.X, T.Y, 0.0).GetSafeNormal();
			// 25 cm above: the board origin must come within 15 cm above the rail (c.z > -45); ~4 frames of fall
			Sim.Spawn(P, H, 900.0 * T + FVector(0, 0, -100.0), 25.0, false);
			Sim.State.Position = P + FVector(0, 0, 25.0);
			Sim.SetOrientationFromAxes(T, (FVector::UpVector - T.Z * T).GetSafeNormal());
			int32 Frame = -1;
			for (int32 F = 0; F < 90 && Frame < 0; ++F)
			{
				Sim.Step(FRiderInputWord());
				if (Sim.State.Motion == ERiderMotion::RailSlide)
				{
					Frame = F;
				}
			}
			if (Frame >= 0)
			{
				++TrackCaught;
				Sim.Step(FRiderInputWord());
				FRiderRailQuery Q;
				Q.Origin = Sim.State.ContactPoint;
				FRiderRailHit Hh;
				const bool bOnRail = World.QueryRail(Q, Hh) && Hh.Distance < 0.5;
				Wrong += bOnRail ? 0 : 1;
				if (bOnRail && Hh.Spline != Enabled[I])
				{
					Misses += FString::Printf(TEXT(" [spline %d caught, rail point on the joining spline %d]"), Enabled[I], Hh.Spline);
				}
			}
			else
			{
				Misses += FString::Printf(TEXT(" spline %d (%s, z %.0f)"), Enabled[I], LexToString(Sim.State.Motion), P.Z);
			}
		}
		Tries += TrackTries;
		Caught += TrackCaught;
		Report(this, FString::Printf(TEXT("%s: %d enabled rails, %d drops, %d caught%s%s"), Track, Enabled.Num(), TrackTries, TrackCaught,
			Misses.IsEmpty() ? TEXT("") : TEXT(";"), *Misses));
	}
	if (TracksRun == 0)
	{
		SSXTest::Skip(*this, TEXT("skipped: no track SimData (tools/export_sim_data.py, tools/export_rails.py)"));
		return true;
	}
	Report(this, FString::Printf(TEXT("track rides: %d tracks, %d drops, %d caught, %d with the rail point off every rail"), TracksRun, Tries, Caught, Wrong));
	TestTrue(TEXT("drops attempted"), Tries > 0);
	TestEqual(TEXT("every drop onto a real rail is caught"), Caught, Tries);
	TestEqual(TEXT("each catch leaves the rider on a rail (rail point within 0.5 cm)"), Wrong, 0);
	return true;
}

#endif
