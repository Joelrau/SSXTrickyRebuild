#include "Harness/RiderSetup.h"

#include "Input/RiderInputDriver.h"
#include "Sim/PatchWorld.h"
#include "Sim/SurfaceTable.h"

#include "Sim/Ground/GroundStates.h"
#include "Sim/Air/AirStates.h"

namespace SSXRiderSetup
{
	FString DescribeStates()
	{
		return TEXT("ground states: P2 (Sim/Ground/GroundStates.h), air states: P3 (Sim/Air/AirStates.h)");
	}

	FString RegisterStates(FRiderSim& Sim)
	{
		RegisterGroundStates(Sim);
		RegisterAirStates(Sim);
		return DescribeStates();
	}

	FString RegisterStates(FRiderSim& Sim, bool bUsePathHeading)
	{
		FGroundConfig Cfg;
		Cfg.bUsePathHeading = bUsePathHeading;
		RegisterGroundStates(Sim, Cfg);
		RegisterAirStates(Sim);
		return DescribeStates() + (bUsePathHeading ? TEXT(", path heading on") : TEXT(""));
	}

	FString InstallStates(FRiderSim& Sim, FRiderInputDriver& Driver, bool bUsePathHeading)
	{
		FString Summary;
		Driver.Install(Sim, [&Summary, bUsePathHeading](FRiderSim& Registry) { Summary = RegisterStates(Registry, bUsePathHeading); });
		return Summary;
	}

	FString InstallStates(FRiderSim& Sim, FRiderInputDriver& Driver)
	{
		FString Summary;
		Driver.Install(Sim, [&Summary](FRiderSim& Registry) { Summary = RegisterStates(Registry); });
		return Summary;
	}

	FQuat RenderedOrientation(const FRiderState& State)
	{
		return SSXAir::GetRenderedOrientation(State);
	}

	bool LastLanding(const FRiderSim* Registry, int32& OutFrame, FString& OutText)
	{
		const SSXAir::FAirContext* Ctx = Registry ? SSXAir::GetAirContext(*Registry) : nullptr;
		if (!Ctx || !Ctx->LastLanding.bValid)
		{
			return false;
		}
		const SSXAir::FAirLandingRecord& L = Ctx->LastLanding;
		OutFrame = L.Frame;
		if (L.Verdict == SSXAir::ELandingVerdict::WipeOut)
		{
			OutText = TEXT("Wipeout!");
			return true;
		}
		TArray<FString> Parts;
		const int32 Spin = FMath::Abs(L.Summary.Spins180) * 180;
		const int32 Flips = FMath::Abs(L.Summary.Flips);
		if (Spin >= 180) { Parts.Add(FString::Printf(TEXT("%d"), Spin)); }
		if (Flips > 0) { Parts.Add(FString::Printf(TEXT("%dx %s"), Flips, L.Summary.Flips > 0 ? TEXT("flip") : TEXT("backflip"))); }
		if (Parts.Num() == 0) { Parts.Add(TEXT("Air")); }
		OutText = FString::Join(Parts, TEXT(" + ")) + FString::Printf(TEXT("  %d pts  (%s)"), L.Points, SSXAir::LexToString(L.Verdict));
		return true;
	}

	void BuildSlopeWorld(FPatchWorld& World, double SlopeDeg, int32 Surface)
	{
		constexpr double Size = 10000.0;
		constexpr int32 Cols = 4;
		constexpr int32 Rows = 34;      // y in [-20000.5, 319999.5]; brod01 travels ~30000 cm
		const double T = FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
		TArray<FSimPatch> Patches;
		Patches.Reserve(Cols * Rows);
		for (int32 R = 0; R < Rows; ++R)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				FSimPatch P;
				for (FVector& C : P.C) { C = FVector::ZeroVector; }
				// 0x25C7C8
				const double X0 = -0.5 * Cols * Size + Col * Size - 0.5;
				const double Y0 = -20000.0 + R * Size - 0.5;
				P.C[0] = FVector(X0, Y0, -Y0 * T);
				P.C[4] = FVector(Size, 0.0, 0.0);
				P.C[1] = FVector(0.0, Size, -Size * T);
				P.Surface = Surface;
				P.Flags = 0;
				P.SourceIndex = Patches.Num();
				Patches.Add(P);
			}
		}
		World.SetPatches(MoveTemp(Patches));
		if (!World.GetSurfaces().IsLoaded())
		{
			World.LoadSurfaces(FSurfaceTable::DefaultPath());
		}
	}

	bool ProjectToGround(const IRiderWorld& World, const FVector& GamePos, FRiderWorldHit& OutHit)
	{
		return World.LineQuery(GamePos + FVector(0, 0, 5000.0), GamePos - FVector(0, 0, 50000.0),
		                       ERiderLineQuery::FineRefined, -1.0, OutHit);
	}

	void RestInSnow(FRiderSim& Sim)
	{
		// 0x11CAE8
		FRiderState& St = Sim.State;
		if (St.ContactKind == 3)
		{
			return;
		}
		const FVector N = St.GroundNormal;
		St.SurfaceP7 = Sim.Surface(St.SurfaceId).SinkDepth();
		St.SurfaceP8 = Sim.Surface(St.SurfaceId).SinkMax();
		St.Position = St.ContactPoint - N * (St.SurfaceP7 * N.Z);
		St.SurfaceChanged = 0;
		Sim.RunGroundProbe();
	}

	bool FindGentleStart(const FPatchWorld& World, const FVector& Marker, const FVector& MarkerForward,
	                     FVector& OutPos, FVector& OutHeading, int32* OutTried)
	{
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
				Candidates.Add({ K, FVector::Dist(P.Evaluate(0.5, 0.5), Marker) });
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

		int32 Tried = 0;
		for (const FCandidate& Cand : Candidates)
		{
			++Tried;
			FVector Centre, Du, Dv;
			World.GetPatch(Cand.Patch).Evaluate(0.5, 0.5, Centre, Du, Dv);
			const FVector Downhill = FPatchWorld::FallLineFor(FVector::CrossProduct(Du, Dv).GetSafeNormal(), MarkerForward);
			if (GentleStretch(Centre, Downhill))
			{
				OutPos = Centre;
				OutHeading = Downhill;
				if (OutTried) { *OutTried = Tried; }
				return true;
			}
		}
		if (OutTried) { *OutTried = Tried; }
		return false;
	}
}
