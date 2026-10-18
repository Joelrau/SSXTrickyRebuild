#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace SSXTerrainDraw
{
	struct FMeshHeights
	{
		TArray<FVector> V;
		TArray<FIntVector> T;
		TMap<FIntPoint, TArray<int32>> Cells;
		static constexpr double Cell = 1000.0;

		bool Load(const FString& Stem)
		{
			FString Folder = Stem;
			Folder[0] = FChar::ToUpper(Folder[0]);
			const FString Path = FString::Printf(TEXT("/Game/Tracks/%s/%s/StaticMeshes/%s_terrain.%s_terrain"), *Folder, *Stem, *Stem, *Stem);
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
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
				const FVector3f P = Pos[Id];
				Index.Add(Id, V.Add(FVector(P.X, -P.Y, P.Z)));
			}
			for (const FTriangleID Tri : MD->Triangles().GetElementIDs())
			{
				TArrayView<const FVertexID> Ids = MD->GetTriangleVertices(Tri);
				const int32 K = T.Add(FIntVector(Index[Ids[0]], Index[Ids[1]], Index[Ids[2]]));
				FBox2D B(ForceInit);
				for (int32 J = 0; J < 3; ++J) { B += FVector2D(V[T[K][J]]); }
				for (int32 X = FMath::FloorToInt(B.Min.X / Cell); X <= FMath::FloorToInt(B.Max.X / Cell); ++X)
				{
					for (int32 Y = FMath::FloorToInt(B.Min.Y / Cell); Y <= FMath::FloorToInt(B.Max.Y / Cell); ++Y)
					{
						Cells.FindOrAdd(FIntPoint(X, Y)).Add(K);
					}
				}
			}
			return T.Num() > 0;
		}

		// Height of the render mesh straight down at (x, y) nearest to RefZ (within 60 cm), or false.
		bool HeightAt(double X, double Y, double RefZ, double& OutZ) const
		{
			const TArray<int32>* List = Cells.Find(FIntPoint(FMath::FloorToInt(X / Cell), FMath::FloorToInt(Y / Cell)));
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
				const double L0 = ((B.Y - C.Y) * (X - C.X) + (C.X - B.X) * (Y - C.Y)) / D;
				const double L1 = ((C.Y - A.Y) * (X - C.X) + (A.X - C.X) * (Y - C.Y)) / D;
				const double L2 = 1.0 - L0 - L1;
				if (L0 < -1e-6 || L1 < -1e-6 || L2 < -1e-6)
				{
					continue;
				}
				const double Z = L0 * A.Z + L1 * B.Z + L2 * C.Z;
				if (FMath::Abs(Z - RefZ) < Best)
				{
					Best = FMath::Abs(Z - RefZ);
					OutZ = Z;
					bFound = true;
				}
			}
			return bFound;
		}
	};

	double Percentile(TArray<double> A, double Q)
	{
		if (A.Num() == 0)
		{
			return 0.0;
		}
		A.Sort();
		return A[FMath::Clamp(int32(Q * (A.Num() - 1) + 0.5), 0, A.Num() - 1)];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTerrainDrawHeightTest, "SSX.World.TerrainDrawHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXTerrainDrawHeightTest::RunTest(const FString& Parameters)
{
	using namespace SSXTerrainDraw;
	TArray<FString> Lines;
	const FString Csv = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData/terrain_draw_reference.csv"));
	if (!FFileHelper::LoadFileToStringArray(Lines, *Csv) || Lines.Num() < 2)
	{
		SSXTest::Skip(*this, TEXT("SimData/terrain_draw_reference.csv missing (tools/make_terrain_draw_reference.py): skipped"));
		return true;
	}
	TMap<FString, FMeshHeights> Meshes;
	TMap<FString, TArray<double>> DiffDraw, DiffK8, DrawVsAnalytic;
	TArray<double> BoardDiff;
	int32 Missing = 0, Rows = 0;
	for (int32 I = 1; I < Lines.Num(); ++I)
	{
		TArray<FString> C;
		Lines[I].ParseIntoArray(C, TEXT(","));
		if (C.Num() < 10)
		{
			continue;
		}
		FMeshHeights* Mesh = Meshes.Find(C[0]);
		if (!Mesh)
		{
			Mesh = &Meshes.Add(C[0]);
			if (!Mesh->Load(C[0]))
			{
				SSXTest::Skip(*this, FString::Printf(TEXT("%s_terrain missing (rebuild the track map): its rows skipped"), *C[0]));
			}
		}
		if (Mesh->T.Num() == 0)
		{
			continue;
		}
		++Rows;
		const double X = FCString::Atod(*C[5]), Y = FCString::Atod(*C[6]), ZA = FCString::Atod(*C[7]);
		const double ZD = FCString::Atod(*C[8]), Z8 = FCString::Atod(*C[9]);
		double Z = 0.0;
		if (!Mesh->HeightAt(X, Y, ZD, Z))
		{
			++Missing;
			continue;
		}
		DiffDraw.FindOrAdd(C[0]).Add(FMath::Abs(Z - ZD));
		DiffK8.FindOrAdd(C[0]).Add(FMath::Abs(Z8 - ZD));
		DrawVsAnalytic.FindOrAdd(C[0]).Add(ZD - ZA);
		if (C[1].StartsWith(TEXT("rep")))
		{
			BoardDiff.Add(FMath::Abs(Z - ZD));
		}
	}
	if (Rows == 0)
	{
		SSXTest::Skip(*this, TEXT("no terrain meshes: skipped"));
		return true;
	}
	for (const TPair<FString, TArray<double>>& It : DiffDraw)
	{
		AddInfo(FString::Printf(TEXT("%s: %d points; |port render - original drawn| p50 %.4f p99 %.4f max %.4f cm (a k/8 mesh: p50 %.2f p99 %.2f max %.2f); original drawn - analytic p5 %.2f p50 %.2f p95 %.2f cm"),
			*It.Key, It.Value.Num(), Percentile(It.Value, 0.5), Percentile(It.Value, 0.99), Percentile(It.Value, 1.0),
			Percentile(DiffK8[It.Key], 0.5), Percentile(DiffK8[It.Key], 0.99), Percentile(DiffK8[It.Key], 1.0),
			Percentile(DrawVsAnalytic[It.Key], 0.05), Percentile(DrawVsAnalytic[It.Key], 0.5), Percentile(DrawVsAnalytic[It.Key], 0.95)));
		// float32 glTF metres -> Unreal centimetres: ~0.01 cm at 1e5 cm; a k/8 mesh misses by up to tens of cm
		TestTrue(FString::Printf(TEXT("%s: rendered snow = the original's drawn snow (p99 < 0.05 cm)"), *It.Key), Percentile(It.Value, 0.99) < 0.05);
		TestTrue(FString::Printf(TEXT("%s: rendered snow = the original's drawn snow (max < 0.5 cm)"), *It.Key), Percentile(It.Value, 1.0) < 0.5);
	}
	AddInfo(FString::Printf(TEXT("REP0001 board points: %d, |port - original drawn| max %.4f cm; %d of %d points without a mesh triangle"),
		BoardDiff.Num(), Percentile(BoardDiff, 1.0), Missing, Rows));
	TestTrue(TEXT("almost every reference point lies on the render mesh"), Missing <= Rows / 100);
	return true;
}

#endif
