// 0x1DF478, FUN_001C72E8, 0x1DC3E0, 0x129628, 0x12AE18
#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Harness/RiderSetup.h"
#include "StaticMeshAttributes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace SSXBoardClearance
{
	constexpr int32 NumBins = 50;
	constexpr double OriginalRel[3][NumBins] = {
		{ -1.93, -2.47, -2.82, -3.30, -3.70, -4.13, -4.57, -5.02, -5.35, -5.86, -6.25, -6.59, -7.10, -7.53, -7.93, -8.36, -8.75, -9.12, -9.52, -9.87, -10.30, -10.70, -11.09, -11.46, -11.86, -12.25, -12.64, -12.99, -13.37, -13.71, -14.08, -14.41, -14.74, -15.05, -15.50, -15.78, -16.17, -16.42, -16.81, -17.08, -17.39, -17.71, -18.06, -18.33, -18.65, -18.92, -19.15, -19.41, -19.68, -20.05 },
		{ -0.66, -1.17, -1.59, -1.97, -2.44, -2.85, -3.28, -3.71, -4.09, -4.58, -4.99, -5.35, -5.82, -6.27, -6.65, -7.10, -7.47, -7.87, -8.33, -8.70, -9.13, -9.55, -9.91, -10.30, -10.70, -11.10, -11.45, -11.89, -12.24, -12.58, -12.96, -13.32, -13.71, -14.06, -14.36, -14.78, -15.15, -15.45, -15.79, -16.13, -16.50, -16.79, -17.09, -17.40, -17.67, -18.01, -18.31, -18.53, NAN, -19.03 },
		{ -1.86, -2.25, -2.54, -2.90, -3.25, -3.63, -4.03, -4.31, -4.59, -5.14, -5.35, -5.58, -6.11, -6.51, -6.82, -7.16, -7.55, -7.88, -8.24, -8.54, -8.92, -9.20, -9.62, -9.83, -10.18, -10.46, -10.80, -11.12, -11.46, -11.61, -11.95, -12.26, -12.55, -12.83, -13.07, -13.37, -13.64, -13.93, -14.16, -14.43, -14.89, -15.19, -15.34, -15.53, -15.79, -16.16, -16.34, -16.53, -16.77, -17.02 } };
	constexpr double OriginalOrigin[3] = { -1.39, 3.33, 2.98 };

	double Median(TArray<double> A)
	{
		if (A.Num() == 0)
		{
			return 0.0;
		}
		A.Sort();
		return A[A.Num() / 2];
	}

	double Pct(TArray<double> A, double Q)
	{
		if (A.Num() == 0)
		{
			return 0.0;
		}
		A.Sort();
		return A[FMath::Clamp(int32(Q * (A.Num() - 1) + 0.5), 0, A.Num() - 1)];
	}

	struct FTerrain
	{
		TArray<FVector> V;
		TArray<FIntVector> T;
		TMap<FIntPoint, TArray<int32>> Cells;

		bool Load(const FString& Stem)
		{
			FString Folder = Stem;
			Folder[0] = FChar::ToUpper(Folder[0]);
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Tracks/%s/%s/StaticMeshes/%s_terrain.%s_terrain"), *Folder, *Stem, *Stem, *Stem));
			const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
			if (!MD)
			{
				return false;
			}
			FStaticMeshConstAttributes Attr(*MD);
			TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
			TMap<FVertexID, int32> Index;
			for (const FVertexID Id : MD->Vertices().GetElementIDs())
			{
				Index.Add(Id, V.Add(FVector(Pos[Id].X, -Pos[Id].Y, Pos[Id].Z)));
			}
			for (const FTriangleID Tri : MD->Triangles().GetElementIDs())
			{
				TArrayView<const FVertexID> Ids = MD->GetTriangleVertices(Tri);
				const int32 K = T.Add(FIntVector(Index[Ids[0]], Index[Ids[1]], Index[Ids[2]]));
				FBox2D B(ForceInit);
				for (int32 J = 0; J < 3; ++J) { B += FVector2D(V[T[K][J]]); }
				for (int32 X = FMath::FloorToInt(B.Min.X / 1000.0); X <= FMath::FloorToInt(B.Max.X / 1000.0); ++X)
				{
					for (int32 Y = FMath::FloorToInt(B.Min.Y / 1000.0); Y <= FMath::FloorToInt(B.Max.Y / 1000.0); ++Y)
					{
						Cells.FindOrAdd(FIntPoint(X, Y)).Add(K);
					}
				}
			}
			return T.Num() > 0;
		}

		bool HeightAt(const FVector& P, double& OutZ) const
		{
			const TArray<int32>* List = Cells.Find(FIntPoint(FMath::FloorToInt(P.X / 1000.0), FMath::FloorToInt(P.Y / 1000.0)));
			if (!List)
			{
				return false;
			}
			double Best = 60.0;
			bool bFound = false;
			for (const int32 K : *List)
			{
				const FVector& A = V[T[K].X];
				const FVector& B = V[T[K].Y];
				const FVector& C = V[T[K].Z];
				const double D = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
				if (FMath::Abs(D) < 1e-9)
				{
					continue;
				}
				const double L0 = ((B.Y - C.Y) * (P.X - C.X) + (C.X - B.X) * (P.Y - C.Y)) / D;
				const double L1 = ((C.Y - A.Y) * (P.X - C.X) + (A.X - C.X) * (P.Y - C.Y)) / D;
				if (L0 < -1e-6 || L1 < -1e-6 || 1.0 - L0 - L1 < -1e-6)
				{
					continue;
				}
				const double Z = L0 * A.Z + L1 * B.Z + (1.0 - L0 - L1) * C.Z;
				if (FMath::Abs(Z - P.Z) < Best)
				{
					Best = FMath::Abs(Z - P.Z);
					OutZ = Z;
					bFound = true;
				}
			}
			return bFound;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderBoardClearanceTest, "SSX.Render.BoardClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXRenderBoardClearanceTest::RunTest(const FString& Parameters)
{
	using namespace SSXBoardClearance;
	static const TCHAR* Tracks[] = { TEXT("gari"), TEXT("snow"), TEXT("mesa"), TEXT("elysium") };
	// 0x201138
	TArray<FVector> Verts[2][3];
	bool bGoofyMesh[3] = {};
	for (int32 Goofy = 0; Goofy < 2; ++Goofy)
	{
		for (int32 Class = 0; Class < 3; ++Class)
		{
			bool bIsGoofyMesh = false;
			UStaticMesh* Mesh = USSXCharacterAppearance::LoadDrawnBoardMesh(TEXT("brodi"), Class, Goofy != 0, bIsGoofyMesh);
			const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
			if (!MD)
			{
				SSXTest::Skip(*this, TEXT("board meshes not imported: skipped"));
				return true;
			}
			if (Goofy) { bGoofyMesh[Class] = bIsGoofyMesh; }
			FStaticMeshConstAttributes Attr(*MD);
			TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
			for (const FVertexID Id : MD->Vertices().GetElementIDs()) { Verts[Goofy][Class].Add(FVector(Pos[Id])); }
		}
	}
	TArray<double> Pitch[3][5], Rel[3][5], Low[3][4], Origin[3], VsRender[3], Roll[3];
	int32 TracksRun = 0;
	for (const TCHAR* Stem : Tracks)
	{
		FRaceTrackData Data;
		FPatchWorld World;
		FTerrain Render;
		FString Err;
		if (!Data.Load(FRaceTrackData::DefaultPath(Stem), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(Stem), &Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s skipped: %s"), Stem, *Err));
			continue;
		}
		const bool bRender = Render.Load(Stem);
		const bool bGari = FCString::Strcmp(Stem, TEXT("gari")) == 0;
		FRaceSession Session;
		FRaceConfig Cfg;
		Cfg.Mode = ERaceEventMode::PracticeRace;
		Session.Init(Data, Cfg);
		TArray<TUniquePtr<FRiderSim>> Sims;
		TArray<TUniquePtr<SSXAnim::FRiderAnimDriver>> Drivers;
		for (int32 Class = 0; Class < 3; ++Class)
		{
			TUniquePtr<FRiderSim>& Sim = Sims.Add_GetRef(MakeUnique<FRiderSim>());
			SSXRiderSetup::RegisterStates(*Sim, true);
			SSXRider::ApplyNewSavePlayerProfile(Sim->Profile, 10, 0);
			Sim->Profile.BoardClass = Class;
			Sim->SetWorld(&World);
			TUniquePtr<SSXAnim::FRiderAnimDriver>& D = Drivers.Add_GetRef(MakeUnique<SSXAnim::FRiderAnimDriver>());
			if (!D->Init(SSXAnim::FAflLibrary::Shared(), TEXT("brodi"), Class, 10))
			{
				SSXTest::Skip(*this, TEXT("animation library missing (anim_clips.bin): skipped"));
				return true;
			}
			Session.AddRacer(Sim.Get(), false);
		}
		++TracksRun;
		Session.Start();
		Session.Tick();
		Session.SetPhase(ERacePhase::Race);
		Session.Tick();
		{
			// on the ground on the race line, 10 % down the course, side by side (as SSX.World.BoardOnSnow places them)
			const FRacePath& Line = Data.Race.AIPaths[Session.GetRacer(0).Race.Path];
			const double At = Line.Length() * 0.1;
			const FVector Tan = Line.TangentAt(At).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
			const FVector Side = FVector::CrossProduct(Tan, FVector::UpVector);
			for (int32 I = 0; I < 3; ++I)
			{
				FRiderSim& Sim = *Sims[I];
				const FVector Pos = Line.PointAt(At) + Side * ((I - 1) * 150.0) + FVector(0, 0, 40.0);
				Sim.ApplyTransition(TEXT("T03"));
				Sim.State.Position = Pos;
				Sim.State.Velocity = Tan * 1500.0;
				Sim.SetOrientationFromAxes(Tan, FVector::UpVector);
				Sim.RunGroundProbe();
				FRacerRecord& Rec = Session.GetRacerMutable(I);
				Rec.Race.Reproject(Data.Race, Pos);
				Rec.Line.Cache = FRacePathCache();
				Session.TrackScan(I);
			}
		}
		TArray<double> TrackLow[3];
		for (int32 F = 0; F < 60 * 45; ++F)
		{
			Session.Tick();
			for (int32 I = 0; I < 3; ++I)
			{
				FRiderSim& Sim = *Sims[I];
				Sim.Step(FRiderInputWord());
				Drivers[I]->Step(Sim);
				const FRiderState& S = Sim.State;
				if (S.Motion != ERiderMotion::Ground || S.SurfaceId != 1 || S.ContactKind != 0 || (F % 2 != 0 && !bGari))
				{
					continue;
				}
				SSXAnim::FRiderPalette Pal;
				Drivers[I]->ComputePalette(Sim, Pal);
				FMatrix MeshToGame;
				const FVector N = S.GroundNormal.GetSafeNormal();
				if (!Pal.bValid || !Drivers[I]->GetLibrary() || N.IsNearlyZero()
					|| !USSXCharacterAppearance::BoardMeshToGame(*Drivers[I]->GetLibrary(), I, Pal.Board, Sim.Profile.SizeScale, MeshToGame, Sim.Profile.Stance != 0,
						Sim.Profile.Stance != 0 && bGoofyMesh[I]))
				{
					continue;
				}
				const FVector Hit = S.Position - S.GroundHeight * N;
				double Lowest = 1e9, LowestRender = 1e9;
				for (const FVector& V : Verts[Sim.Profile.Stance != 0 ? 1 : 0][I])
				{
					const FVector W = MeshToGame.TransformPosition(V);
					Lowest = FMath::Min(Lowest, FVector::DotProduct(W - Hit, N));
					double Rz;
					if (bRender && Render.HeightAt(W, Rz))
					{
						LowestRender = FMath::Min(LowestRender, W.Z - Rz);
					}
				}
				const FVector Up(Pal.Board.M[1][0], Pal.Board.M[1][1], Pal.Board.M[1][2]);
				const FVector Across(Pal.Board.M[2][0], Pal.Board.M[2][1], Pal.Board.M[2][2]);
				const double R = FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(N, Across), FVector::DotProduct(N, Up))));
				const double O = FVector::DotProduct(FVector(Pal.Board.M[3][0], Pal.Board.M[3][1], Pal.Board.M[3][2]) - Hit, N);
				const int32 Bin = FMath::FloorToInt(R);
				const FVector Long(Pal.Board.M[0][0], Pal.Board.M[0][1], Pal.Board.M[0][2]);
				const double PitchDeg = FMath::Abs(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(N, Long), -1.0, 1.0))));
				if (bGari && Bin < NumBins && !FMath::IsNaN(OriginalRel[I][Bin]) && PitchDeg < 0.3)
				{
					Rel[I][Bin / 10].Add((Lowest - O) - Sim.Profile.SizeScale * OriginalRel[I][Bin]);
				}
				if (bGari && Bin < NumBins)
				{
					Pitch[I][Bin / 10].Add(PitchDeg);
				}
				Low[I][R < 5.0 ? 0 : R < 15.0 ? 1 : R < 30.0 ? 2 : 3].Add(Lowest);
				Origin[I].Add(O);
				Roll[I].Add(R);
				TrackLow[I].Add(Lowest);
				if (LowestRender < 1e8)
				{
					VsRender[I].Add(LowestRender);
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: lowest board vertex above the ground plane p50 Bx %.2f Fr %.2f Al %.2f cm (%d / %d / %d frames)%s"), Stem,
			Median(TrackLow[0]), Median(TrackLow[1]), Median(TrackLow[2]), TrackLow[0].Num(), TrackLow[1].Num(), TrackLow[2].Num(),
			bRender ? TEXT("") : TEXT(", no terrain mesh")));
	}
	if (TracksRun == 0)
	{
		SSXTest::Skip(*this, TEXT("skipped: no track data"));
		return true;
	}
	static const TCHAR* Names[3] = { TEXT("Bx"), TEXT("Fr"), TEXT("Al") };
	static const double OriginalLow[3][4] = { { -4.35, -7.30, -12.10, -17.46 }, { 1.37, -1.41, -5.77, -8.95 }, { 0.46, -2.04, -6.32, -11.69 } };
	for (int32 I = 0; I < 3; ++I)
	{
		AddInfo(FString::Printf(TEXT("%s: |roll| p50 %.1f p90 %.1f deg (original %s); origin above the plane p50 %.2f (original %.2f); lowest vertex p50 by |roll| 0-5 / 5-15 / 15-30 / 30-50: %.2f / %.2f / %.2f / %.2f (original %.2f / %.2f / %.2f / %.2f); vs the render mesh p5 %.2f p50 %.2f cm"),
			Names[I], Pct(Roll[I], 0.5), Pct(Roll[I], 0.9), I == 0 ? TEXT("12.0 / 42.5") : I == 1 ? TEXT("11.3 / 41.1") : TEXT("13.6 / 44.8"),
			Median(Origin[I]), OriginalOrigin[I], Median(Low[I][0]), Median(Low[I][1]), Median(Low[I][2]), Median(Low[I][3]),
			OriginalLow[I][0], OriginalLow[I][1], OriginalLow[I][2], OriginalLow[I][3], Pct(VsRender[I], 0.05), Pct(VsRender[I], 0.5)));
		int32 Checked = 0;
		for (int32 B = 0; B < 5; ++B)
		{
			if (Rel[I][B].Num() < 25)
			{
				continue;
			}
			++Checked;
			const double D = Median(Rel[I][B]);
			AddInfo(FString::Printf(TEXT("%s |roll| %d-%d: |board pitch| against the ground normal p50 %.2f deg (original ~0.22-0.26)"), Names[I], B * 10, B * 10 + 10, Median(Pitch[I][B])));
			TestTrue(FString::Printf(TEXT("%s |roll| %d-%d deg: board clearance below its origin, port - original at the same roll, median %.2f cm (%d frames)"),
				Names[I], B * 10, B * 10 + 10, D, Rel[I][B].Num()), FMath::Abs(D) < 0.5);
		}
		TestTrue(FString::Printf(TEXT("%s: at least 2 roll ranges ridden on Garibaldi (%d)"), Names[I], Checked), Checked >= 2);
	}
	return true;
}

#endif
