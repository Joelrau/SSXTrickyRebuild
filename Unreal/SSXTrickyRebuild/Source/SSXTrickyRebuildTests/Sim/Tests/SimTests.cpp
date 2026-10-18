#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/SimCoords.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SimTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Report(FAutomationTestBase* Test, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Sim] %s"), *Msg);
	}

	bool LoadAlaska(FAutomationTestBase& Test, FPatchWorld& World)
	{
		FString Err;
		if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("alaska")), &Err))
		{
			Test.AddError(FString::Printf(TEXT("Alaska patch data not available: %s"), *Err));
			return false;
		}
		if (!World.GetSurfaces().IsLoaded())
		{
			Test.AddError(TEXT("surfaces.bin not loaded (run tools/export_sim_data.py)"));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSimPatchProbeTest, "SSX.Sim.PatchProbe", SimTestFlags)

bool FSSXSimPatchProbeTest::RunTest(const FString& Parameters)
{
	FPatchWorld World;
	if (!LoadAlaska(*this, World))
	{
		return false;
	}
	const FString CsvPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("alaska.probe_cases.csv"));
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *CsvPath) || Lines.Num() < 2)
	{
		AddError(FString::Printf(TEXT("cannot read %s (run tools/make_probe_cases.py)"), *CsvPath));
		return false;
	}
	constexpr double Tol = 1e-3;
	int32 Cases = 0, Hits = 0, Failures = 0;
	double MaxPointErr = 0.0, MaxNormalErr = 0.0, MaxHeightErr = 0.0;
	for (int32 LineIdx = 1; LineIdx < Lines.Num(); ++LineIdx)
	{
		TArray<FString> Col;
		Lines[LineIdx].ParseIntoArray(Col, TEXT(","), false);
		if (Col.Num() < 34)
		{
			continue;
		}
		auto D = [&Col](int32 I) { return FCString::Atod(*Col[I]); };
		auto V = [&D](int32 I) { return FVector(D(I), D(I + 1), D(I + 2)); };
		FGroundProbeInput In;
		In.Position = V(1);
		In.GroundNormal = V(4);
		In.Forward = V(7);
		In.ContactLeft = V(10);
		In.LeanDeg = D(13);
		In.RiderScale = D(14);
		const bool bExpFound = FCString::Atoi(*Col[15]) != 0;
		const int32 ExpSurface = FCString::Atoi(*Col[16]);
		const int32 ExpKind = FCString::Atoi(*Col[17]);
		const FVector ExpPoint = V(18), ExpNormal = V(21);
		const double ExpHeight = D(24);
		const FVector ExpFwd = V(25), ExpLeft = V(28), ExpFall = V(31);

		FGroundProbeResult R;
		const bool bFound = World.ProbeGround(In, R);
		++Cases;
		FString Why;
		if (bFound != bExpFound)
		{
			Why = FString::Printf(TEXT("found %d, expected %d"), bFound, bExpFound);
		}
		else
		{
			const double NErr = (R.GroundNormal - ExpNormal).GetAbsMax();
			const double FErr = FMath::Max3((R.ContactForward - ExpFwd).GetAbsMax(), (R.ContactLeft - ExpLeft).GetAbsMax(),
			                               (R.FallLine - ExpFall).GetAbsMax());
			MaxNormalErr = FMath::Max(MaxNormalErr, NErr);
			if (R.ContactKind != ExpKind) { Why += TEXT(" kind"); }
			if (NErr > Tol) { Why += FString::Printf(TEXT(" normal err %.3g"), NErr); }
			if (FErr > Tol) { Why += FString::Printf(TEXT(" axes err %.3g"), FErr); }
			if (bFound)
			{
				++Hits;
				const double PErr = (R.ContactPoint - ExpPoint).GetAbsMax();
				const double HErr = FMath::Abs(R.Height - ExpHeight);
				MaxPointErr = FMath::Max(MaxPointErr, PErr);
				MaxHeightErr = FMath::Max(MaxHeightErr, HErr);
				if (R.Surface != ExpSurface) { Why += FString::Printf(TEXT(" surface %d vs %d"), R.Surface, ExpSurface); }
				// 0x128AE8, 0x25DF50
				const double Ulp = FMath::Max(1.0, ExpPoint.GetAbsMax()) * 1.1920929e-7;
				if (PErr > Tol + 8.0 * Ulp) { Why += FString::Printf(TEXT(" point err %.3g"), PErr); }
				if (HErr > Tol + 3.0 * Ulp) { Why += FString::Printf(TEXT(" height err %.3g"), HErr); }
			}
		}
		if (!Why.IsEmpty())
		{
			++Failures;
			AddError(FString::Printf(TEXT("case %s:%s"), *Col[0], *Why));
		}
	}
	Report(this, FString::Printf(TEXT("probe cases %d (hits %d), failures %d; max |dpoint| %.3g cm, max |dnormal| %.3g, max |dheight| %.3g cm"),
	                        Cases, Hits, Failures, MaxPointErr, MaxNormalErr, MaxHeightErr));
	TestTrue(TEXT("at least 100 probe cases"), Cases >= 100);
	return Failures == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSimGroundedRunTest, "SSX.Sim.GroundedRun", SimTestFlags)

bool FSSXSimGroundedRunTest::RunTest(const FString& Parameters)
{
	FPatchWorld World;
	if (!LoadAlaska(*this, World))
	{
		return false;
	}
	FVector MarkerPos, MarkerFwd;
	if (!World.GetStartMarker(MarkerPos, MarkerFwd))
	{
		AddError(TEXT("no start marker in alaska.patches.bin"));
		return false;
	}

	struct FCandidate { int32 Patch; double Dist; };
	TArray<FCandidate> Candidates;
	for (int32 K = 0; K < World.NumPatches(); ++K)
	{
		const FSimPatch& P = World.GetPatch(K);
		if (P.Surface != 1 && P.Surface != 5)
		{
			continue;
		}
		double MinNz = 1.0, MaxNz = -1.0;
		for (double U : { 0.1, 0.5, 0.9 })
		{
			for (double Vv : { 0.1, 0.5, 0.9 })
			{
				FVector Pt, Du, Dv;
				P.Evaluate(U, Vv, Pt, Du, Dv);
				const double Nz = FVector::CrossProduct(Du, Dv).GetSafeNormal().Z;
				MinNz = FMath::Min(MinNz, Nz);
				MaxNz = FMath::Max(MaxNz, Nz);
			}
		}
		if (MinNz >= 0.92 && MaxNz <= 0.9995)
		{
			Candidates.Add({ K, FVector::Dist(P.Evaluate(0.5, 0.5), MarkerPos) });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Dist < B.Dist; });

	auto GentleStretch = [&World](const FVector& From, const FVector& Heading) -> bool
	{
		FVector P = From, Dir = Heading, PrevN = FVector::ZeroVector;
		double SumNz = 0.0;
		constexpr int32 Steps = 100;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			FRiderWorldHit Hit;
			if (!World.LineQuery(P + FVector(0, 0, 300), P - FVector(0, 0, 300), ERiderLineQuery::FineRefined, 0.5, Hit))
			{
				return false;
			}
			const FVector N = Hit.Normal;
			if (N.Z < 0.92 || !(Hit.Surface == 1 || Hit.Surface == 2 || Hit.Surface == 5))
			{
				return false;
			}
			if (Step > 0 && ((N - PrevN).Size() > 0.1 || FVector::DotProduct(N - PrevN, Dir) > 0.01))
			{
				return false;
			}
			PrevN = N;
			SumNz += N.Z;
			Dir = FPatchWorld::FallLineFor(N, Dir);
			P = Hit.Point + 100.0 * Dir;
		}
		return SumNz / (Steps + 1) <= 0.99;
	};

	int32 Best = INDEX_NONE, Tried = 0;
	double BestDist = 0.0;
	FVector Centre = FVector::ZeroVector, Downhill = FVector::ZeroVector;
	for (const FCandidate& Cand : Candidates)
	{
		++Tried;
		FVector Du, Dv;
		World.GetPatch(Cand.Patch).Evaluate(0.5, 0.5, Centre, Du, Dv);
		Downhill = FPatchWorld::FallLineFor(FVector::CrossProduct(Du, Dv).GetSafeNormal(), MarkerFwd);
		if (GentleStretch(Centre, Downhill))
		{
			Best = Cand.Patch;
			BestDist = Cand.Dist;
			break;
		}
	}
	if (Best == INDEX_NONE)
	{
		AddError(FString::Printf(TEXT("no gentle 100 m stretch found among %d candidate patches"), Candidates.Num()));
		return false;
	}
	Report(this, FString::Printf(TEXT("gentle stretch: candidate %d of %d"), Tried, Candidates.Num()));

	FRiderSim Sim;
	Sim.SetWorld(&World);
	Sim.Reset();
	Sim.Spawn(Centre, Downhill, FVector::ZeroVector, 0.0, false);
	if (Sim.State.Motion != ERiderMotion::Ground)
	{
		AddError(FString::Printf(TEXT("spawn did not start grounded (motion %s)"), LexToString(Sim.State.Motion)));
		return false;
	}

	const FVector Start = Sim.State.Position;
	double MaxH = -1e9, MinH = 1e9, MaxSpeed = 0.0;
	int32 FirstAirFrame = -1, MinHFrame = 0;
	TSet<int32> Surfaces;
	TArray<FString> Trace;
	const FRiderInputWord NoInput;
	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		Sim.Step(NoInput);
		const FRiderState& S = Sim.State;
		const FGroundProbeResult& Pr = Sim.LastProbe;
		Trace.Add(FString::Printf(TEXT("f%03d h %+.3f surf %d patch %d refined %d uv (%.3f %.3f) n (%.4f %.4f %.4f) v %.0f vn %+.1f pos (%.1f %.1f %.1f)"),
			Frame, S.GroundHeight, S.SurfaceId, Pr.Hit.PatchIndex, Pr.Hit.bRefined, Pr.Hit.U, Pr.Hit.V,
			S.GroundNormal.X, S.GroundNormal.Y, S.GroundNormal.Z, S.Velocity.Size(),
			FVector::DotProduct(S.Velocity, S.GroundNormal), S.Position.X, S.Position.Y, S.Position.Z));
		MaxH = FMath::Max(MaxH, S.GroundHeight);
		if (S.GroundHeight < MinH) { MinH = S.GroundHeight; MinHFrame = Frame; }
		MaxSpeed = FMath::Max(MaxSpeed, S.Velocity.Size());
		Surfaces.Add(S.SurfaceId);
		if (S.Motion != ERiderMotion::Ground && FirstAirFrame < 0)
		{
			FirstAirFrame = Frame;
			for (int32 I = FMath::Max(0, Trace.Num() - 12); I < Trace.Num(); ++I)
			{
				Report(this, Trace[I]);
			}
			AddError(FString::Printf(TEXT("left the ground at frame %d: motion %s, control %s, h %.3f cm, surface %d, speed %.0f cm/s, pos (%.0f %.0f %.0f)"),
			                         Frame, LexToString(S.Motion), LexToString(S.Control), S.GroundHeight, S.SurfaceId,
			                         S.Velocity.Size(), S.Position.X, S.Position.Y, S.Position.Z));
		}
	}
	for (int32 I = FMath::Max(0, MinHFrame - 3); I < FMath::Min(Trace.Num(), MinHFrame + 3); ++I)
	{
		Report(this, TEXT("around min h: ") + Trace[I]);
	}
	const FRiderState& S = Sim.State;
	FString SurfList;
	for (int32 Id : Surfaces) { SurfList += FString::Printf(TEXT("%d "), Id); }
	Report(this, FString::Printf(TEXT("start patch %d (%.0f m from the marker) at (%.0f %.0f %.0f) game space; travelled %.1f m, drop %.1f m, max speed %.0f cm/s, final speed %.0f cm/s"),
	                        World.GetPatch(Best).SourceIndex, BestDist / 100.0, Start.X, Start.Y, Start.Z,
	                        FVector::Dist(Start, S.Position) / 100.0, (Start.Z - S.Position.Z) / 100.0, MaxSpeed, S.Velocity.Size()));
	Report(this, FString::Printf(TEXT("ground height range [%.3f, %.3f] cm (stick height surface 1: %.3f cm); take-offs %d; surfaces visited: %s"),
	                        MinH, MaxH, World.SurfaceParams(1).StickHeight(), Sim.CountTakeOffs(), *SurfList));
	TestTrue(TEXT("rider moved"), FVector::Dist(Start, S.Position) > 100.0);
	TestEqual(TEXT("take-offs in 600 frames"), Sim.CountTakeOffs(), 0);
	return FirstAirFrame < 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSimStateMachineTest, "SSX.Sim.StateMachine", SimTestFlags)

bool FSSXSimStateMachineTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("transition rows"), FRiderSim::GetTransitionTable().Num(), 51);
	const FRiderTransition* T28 = FRiderSim::FindTransition(TEXT("T28"));
	TestTrue(TEXT("T28 exists"), T28 && T28->Motion == ERiderMotion::Air && T28->Control == ERiderControl::NaturalAir);

	FRiderSim Sim;
	Sim.Reset();
	TestTrue(TEXT("reset -> Static/Void"), Sim.State.Motion == ERiderMotion::Static && Sim.State.Control == ERiderControl::Void);
	Sim.ApplyTransition(TEXT("T60"));
	TestTrue(TEXT("T60 -> WipeOut"), Sim.State.Motion == ERiderMotion::WipeOut && Sim.State.Control == ERiderControl::WipeOut);
	const int32 Before = Sim.GetEvents().Num();
	Sim.SetControl(ERiderControl::WipeOut);
	TestEqual(TEXT("same control is a no-op"), Sim.GetEvents().Num(), Before);

	// 0x129008
	Sim.SetOrientationFromAxes(FVector(1, 0, 0), FVector(0, 0, 1));
	TestTrue(TEXT("forward"), Sim.State.BasisForward.Equals(FVector(1, 0, 0), 1e-6));
	TestTrue(TEXT("right = forward x up"), Sim.State.BasisSide.Equals(FVector(0, -1, 0), 1e-6));

	const FQuat G(FVector(0.3, -0.5, 0.8).GetSafeNormal(), 0.7);
	const FVector Vg(10, 20, 30);
	const FVector A = SSXSim::GameToUnreal(G.RotateVector(Vg));
	const FVector B = SSXSim::GameQuatToUnreal(G).RotateVector(SSXSim::GameToUnreal(Vg));
	TestTrue(TEXT("mirrored quaternion"), A.Equals(B, 1e-9));

	FRiderInputWord W;
	W.bJumpPressed = true;
	W.bJumpHeld = true;
	const FRiderInputWord P = W.MaskedFor(ERiderControl::Prewind);
	TestTrue(TEXT("prewind layout"), !P.bJumpPressed && P.bJumpHeld);
	return true;
}

#endif
