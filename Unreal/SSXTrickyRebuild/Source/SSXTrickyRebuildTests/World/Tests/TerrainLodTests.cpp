#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SSXTerrainLod.h"
#include "World/SSXTerrainLodComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITORONLY_DATA
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace SSXTerrainLodTest
{
	TSharedPtr<FJsonObject> LoadReference(FAutomationTestBase& Test)
	{
		FString Text;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData/terrain_lod_reference.json"));
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
		{
			SSXTest::Skip(Test, TEXT("SimData/terrain_lod_reference.json missing (tools/make_terrain_lod_reference.py): skipped"));
			return nullptr;
		}
		return Root;
	}

	FVector3f Vec(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		return FVector3f(float(A[0]->AsNumber()), float(A[1]->AsNumber()), float(A[2]->AsNumber()));
	}

	void Levels(const TArray<TSharedPtr<FJsonValue>>& A, uint8 Out[4])
	{
		for (int32 E = 0; E < 4; ++E) { Out[E] = uint8(A[E]->AsNumber()); }
	}

	int32 Frac105(const FString& S)
	{
		FString N, D;
		S.Split(TEXT("/"), &N, &D);
		return FCString::Atoi(*N) * (SSXTerrainLod::Den / FCString::Atoi(*D));
	}

	const FSSXTerrainLodData* Data(TMap<FString, FSSXTerrainLodData>& Cache, const FString& Stem)
	{
		if (FSSXTerrainLodData* D = Cache.Find(Stem))
		{
			return D->Patches.Num() ? D : nullptr;
		}
		FSSXTerrainLodData& D = Cache.Add(Stem);
		D.Load(FSSXTerrainLodData::DefaultPath(Stem));
		return D.Patches.Num() ? &D : nullptr;
	}

	int32 FindPatch(const FSSXTerrainLodData& D, int32 PbdIndex)
	{
		return D.Patches.IndexOfByPredicate([PbdIndex](const FSSXTerrainLodData::FPatch& P) { return P.PbdIndex == PbdIndex; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTerrainLodLevelsTest, "SSX.World.TerrainLod.Levels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXTerrainLodLevelsTest::RunTest(const FString& Parameters)
{
	using namespace SSXTerrainLodTest;
	const TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	if (!Ref)
	{
		return true;
	}
	int32 Bad = 0, N = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("levels")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		FVector3f Corners[4];
		const TArray<TSharedPtr<FJsonValue>>& Cs = C->GetArrayField(TEXT("corners"));
		for (int32 I = 0; I < 4; ++I) { Corners[I] = Vec(Cs[I]->AsArray()); }
		uint8 Want[4], Got[4];
		Levels(C->GetArrayField(TEXT("levels")), Want);
		SSXTerrainLod::EdgeLevels(Corners, Vec(C->GetArrayField(TEXT("eye"))), Got);
		if (FMemory::Memcmp(Want, Got, 4) != 0)
		{
			if (++Bad <= 5)
			{
				AddError(FString::Printf(TEXT("case %d: levels %d %d %d %d, model %d %d %d %d"), N, Got[0], Got[1], Got[2], Got[3], Want[0], Want[1], Want[2], Want[3]));
			}
		}
		++N;
	}
	TestEqual(FString::Printf(TEXT("%d level cases (random, both radii, .pbd corners) = the model"), N), Bad, 0);

	const TSharedPtr<FJsonObject>* Ram = nullptr;
	if (!Ref->TryGetObjectField(TEXT("ram"), Ram) || !Ram)
	{
		SSXTest::Skip(*this, TEXT("no RAM case in the reference (no traces): skipped"));
		return true;
	}
	TMap<FString, FSSXTerrainLodData> Cache;
	const FSSXTerrainLodData* D = Data(Cache, (*Ram)->GetStringField(TEXT("track")));
	if (!D)
	{
		SSXTest::Skip(*this, TEXT("SimData/pipe.terrainlod.bin missing (tools/export_terrain_lod.py): RAM case skipped"));
		return true;
	}
	const FVector3f Eye = Vec((*Ram)->GetArrayField(TEXT("eye")));
	int32 Match = 0, Total = 0;
	int32 Hist[4] = { 0, 0, 0, 0 };
	for (const TSharedPtr<FJsonValue>& V : (*Ram)->GetArrayField(TEXT("queued")))
	{
		const TSharedPtr<FJsonObject> Q = V->AsObject();
		const int32 P = FindPatch(*D, int32(Q->GetNumberField(TEXT("patch"))));
		++Total;
		if (P == INDEX_NONE)
		{
			continue;
		}
		uint8 Want[4], Got[4];
		Levels(Q->GetArrayField(TEXT("levels")), Want);
		SSXTerrainLod::EdgeLevels(D->Patches[P].Corners, Eye, Got);
		Match += FMemory::Memcmp(Want, Got, 4) == 0;
		const bool bUniform = Got[0] == Got[1] && Got[1] == Got[2] && Got[2] == Got[3];
		++Hist[bUniform ? (Got[0] - 4) / 2 : 3];
	}
	AddInfo(FString::Printf(TEXT("Pipedream RAM frame: %d of %d queued patches; uniform 4 / 6 / 8 / mixed = %d / %d / %d / %d"),
		Match, Total, Hist[0], Hist[1], Hist[2], Hist[3]));
	TestEqual(TEXT("every patch the game queued gets the game's four edge levels"), Match, Total);
	TestTrue(TEXT("the frame's mix 910 / 83 / 9 / 52"), Hist[0] == 910 && Hist[1] == 83 && Hist[2] == 9 && Hist[3] == 52);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTerrainLodTemplatesTest, "SSX.World.TerrainLod.Templates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXTerrainLodTemplatesTest::RunTest(const FString& Parameters)
{
	using namespace SSXTerrainLod;
	int32 Bad = 0;
	for (int32 S = 0; S < NumSignatures; ++S)
	{
		uint8 L[4];
		SignatureLevels(S, L);
		const TArray<uint32>& T = Template(S);
		int64 Area2 = 0;
		bool bWound = true;
		TSet<int32> Used;
		for (int32 I = 0; I < T.Num(); I += 3)
		{
			const FIntPoint A = LatticeUV(T[I]), B = LatticeUV(T[I + 1]), C = LatticeUV(T[I + 2]);
			const int64 Cross = int64(B.X - A.X) * (C.Y - A.Y) - int64(B.Y - A.Y) * (C.X - A.X);
			bWound &= Cross > 0;
			Area2 += Cross;
			Used.Add(T[I]); Used.Add(T[I + 1]); Used.Add(T[I + 2]);
		}
		bool bEdges = true;
		for (int32 E = 0; E < 4; ++E)
		{
			TSet<int32> OnEdge, Want;
			for (const int32 I : Used)
			{
				const FIntPoint P = LatticeUV(I);
				const int32 T105 = E < 2 ? P.X : P.Y;
				const bool bOn = E == 0 ? P.Y == 0 : E == 1 ? P.Y == Den : E == 2 ? P.X == 0 : P.X == Den;
				if (bOn) { OnEdge.Add(T105); }
			}
			for (int32 K = 0; K < L[E]; ++K) { Want.Add(K * (Den / (L[E] - 1))); }
			bEdges &= OnEdge.Num() == Want.Num() && OnEdge.Includes(Want);
		}
		const bool bOk = bWound && Area2 == int64(Den) * Den * 2 && T.Num() <= MaxTris * 3 && bEdges;
		if (!bOk && ++Bad <= 5)
		{
			AddError(FString::Printf(TEXT("signature %d %d %d %d: wound %d, area x2 %lld, %d tris, edges %d"), L[0], L[1], L[2], L[3],
				bWound, Area2, T.Num() / 3, bEdges));
		}
	}
	TestEqual(TEXT("81 templates: one-way wound tiling of the patch, each edge at its own level"), Bad, 0);
	TestEqual(TEXT("uniform k/3 / k/5 / k/7: 18 / 50 / 98 triangles"),
		FString::Printf(TEXT("%d %d %d"), Template(0).Num() / 3, Template(40).Num() / 3, Template(80).Num() / 3), FString(TEXT("18 50 98")));

	const TSharedPtr<FJsonObject> Ref = SSXTerrainLodTest::LoadReference(*this);
	if (!Ref)
	{
		return true;
	}
	int32 Sigs = 0, Mismatch = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("strips")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		uint8 L[4];
		SSXTerrainLodTest::Levels(O->GetArrayField(TEXT("levels")), L);
		TArray<TArray<int32>> Got;
		PatchStrips(L, Got);
		const TArray<TSharedPtr<FJsonValue>>& Want = O->GetArrayField(TEXT("strips"));
		bool bSame = Got.Num() == Want.Num();
		for (int32 S = 0; bSame && S < Got.Num(); ++S)
		{
			const TArray<TSharedPtr<FJsonValue>>& W = Want[S]->AsArray();
			bSame &= W.Num() == Got[S].Num();
			for (int32 K = 0; bSame && K < W.Num(); ++K)
			{
				const TArray<TSharedPtr<FJsonValue>>& UV = W[K]->AsArray();
				bSame &= LatticeIndex(FIntPoint(SSXTerrainLodTest::Frac105(UV[0]->AsString()), SSXTerrainLodTest::Frac105(UV[1]->AsString()))) == Got[S][K];
			}
		}
		++Sigs;
		if (!bSame && ++Mismatch <= 5)
		{
			AddError(FString::Printf(TEXT("signature %d %d %d %d: strips differ from the drawn order"), L[0], L[1], L[2], L[3]));
		}
		TestEqual(TEXT("signature index round trip"), int32(SignatureIndex(L)), Sigs - 1);
	}
	TestEqual(FString::Printf(TEXT("%d signatures: strips = the original's drawn vertex order"), Sigs), Mismatch, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTerrainLodDrawHeightTest, "SSX.World.TerrainLod.DrawHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXTerrainLodDrawHeightTest::RunTest(const FString& Parameters)
{
	using namespace SSXTerrainLodTest;
	const TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	if (!Ref)
	{
		return true;
	}
	TMap<FString, FSSXTerrainLodData> Cache;
	TArray<double> Diff;
	int32 Missing = 0, NoTri = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("draw")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const FSSXTerrainLodData* D = Data(Cache, O->GetStringField(TEXT("track")));
		const int32 P = D ? FindPatch(*D, int32(O->GetNumberField(TEXT("patch")))) : INDEX_NONE;
		if (P == INDEX_NONE)
		{
			++Missing;
			continue;
		}
		uint8 L[4];
		Levels(O->GetArrayField(TEXT("levels")), L);
		const double X = O->GetNumberField(TEXT("x")), Y = O->GetNumberField(TEXT("y")), Z = O->GetNumberField(TEXT("z"));
		const FSSXTerrainLodData::FVertex* B = D->Block(P);
		const TArray<uint32>& T = SSXTerrainLod::Template(SSXTerrainLod::SignatureIndex(L));
		const FVector Q(X, Y, Z);
		double Best = 1e9;
		for (int32 I = 0; I < T.Num(); I += 3)
		{
			const FVector Hit = FMath::ClosestPointOnTriangleToPoint(Q, FVector(B[T[I]].Position), FVector(B[T[I + 1]].Position), FVector(B[T[I + 2]].Position));
			Best = FMath::Min(Best, FVector::Dist(Hit, Q));
		}
		if (Best > 1e8)
		{
			++NoTri;
			continue;
		}
		Diff.Add(Best);
	}
	if (Diff.Num() == 0)
	{
		SSXTest::Skip(*this, TEXT("no terrain LOD data (tools/export_terrain_lod.py): skipped"));
		return true;
	}
	Diff.Sort();
	const double P99 = Diff[int32(0.99 * (Diff.Num() - 1))], Max = Diff.Last();
	AddInfo(FString::Printf(TEXT("%d points on mixed / far signatures: distance to the drawn mesh p50 %.4f p99 %.4f max %.4f cm; %d without data, %d without a triangle"),
		Diff.Num(), Diff[Diff.Num() / 2], P99, Max, Missing, NoTri));
	TestTrue(TEXT("drawn surface = the original's at mixed / far levels (p99 < 0.05 cm)"), P99 < 0.05);
	TestTrue(TEXT("drawn surface = the original's (max < 0.5 cm)"), Max < 0.5);
	TestTrue(TEXT("every point lies on a triangle of its patch's template"), NoTri == 0);
	return true;
}

#if WITH_EDITORONLY_DATA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTerrainLodMatchesMeshTest, "SSX.World.TerrainLod.MatchesTerrainMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXTerrainLodMatchesMeshTest::RunTest(const FString& Parameters)
{
	int32 Tracks = 0;
	for (const TCHAR* StemC : { TEXT("snow"), TEXT("pipe"), TEXT("gari"), TEXT("alaska") })
	{
		const FString Stem = StemC;
		FString Folder = Stem;
		Folder[0] = FChar::ToUpper(Folder[0]);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Tracks/%s/%s/StaticMeshes/%s_terrain.%s_terrain"), *Folder, *Stem, *Stem, *Stem));
		FSSXTerrainLodData Data;
		const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!MD || !Data.Load(FSSXTerrainLodData::DefaultPath(Stem)))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s: terrain mesh or terrainlod.bin missing: skipped"), *Stem));
			continue;
		}
		++Tracks;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetTransientPackage());
		C->SetStaticMesh(Mesh);
		USSXTerrainLodComponent* Lod = USSXTerrainLodComponent::CreateFor(*C, Data, GetTransientPackage());
		if (!TestNotNull(*FString::Printf(TEXT("%s: LOD component built"), *Stem), Lod))
		{
			continue;
		}
		const FSSXTerrainLodRenderData& R = *Lod->GetRenderData();
		TSet<UMaterialInterface*> MeshMats, LodMats;
		for (int32 S = 0; S < C->GetNumMaterials(); ++S) { MeshMats.Add(C->GetMaterial(S)); }
		for (int32 S = 0; S < Lod->GetNumMaterials(); ++S) { LodMats.Add(Lod->GetMaterial(S)); }
		TestTrue(FString::Printf(TEXT("%s: the same material instances (%d)"), *Stem, MeshMats.Num()), MeshMats.Num() == LodMats.Num() && MeshMats.Includes(LodMats));
		int32 MeshPatches = 0;
		for (const FSSXTerrainLodData::FPatch& P : Data.Patches) { MeshPatches += !P.bShowOff; }
		TestEqual(FString::Printf(TEXT("%s: every normal patch"), *Stem), R.NumPatches(), MeshPatches);

		// Game space (x, y, z) -> Unreal mesh space (x, -y, z): glTF Y-up through Interchange.
		FStaticMeshConstAttributes Attr(*MD);
		TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector2f> Uvs = Attr.GetVertexInstanceUVs();
		struct FInst { FVector3f P; FVector2f U0, U1, U2; };
		TMultiMap<FIntVector, FInst> Cells;
		for (const FVertexInstanceID I : MD->VertexInstances().GetElementIDs())
		{
			const FVector3f P = Pos[MD->GetVertexInstanceVertex(I)];
			Cells.Add(FIntVector(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z)),
				{ P, Uvs.Get(I, 0), Uvs.Get(I, 1), Uvs.Get(I, 2) });
		}
		int32 Checked = 0, Matched = 0;
		double WorstP = 0.0;
		for (int32 P = 0; P < R.NumPatches(); ++P)
		{
			for (int32 V = 0; V < 64; ++V)
			{
				const int32 K = P * SSXTerrainLod::VertsPerPatch + V;
				const FVector3f Q = R.Positions[K];
				++Checked;
				bool bHit = false;
				for (int32 X = -1; X <= 1 && !bHit; ++X)
				for (int32 Y = -1; Y <= 1 && !bHit; ++Y)
				for (int32 Z = -1; Z <= 1 && !bHit; ++Z)
				{
					TArray<const FInst*> Found;
					Cells.MultiFindPointer(FIntVector(FMath::FloorToInt(Q.X) + X, FMath::FloorToInt(Q.Y) + Y, FMath::FloorToInt(Q.Z) + Z), Found);
					for (const FInst* F : Found)
					{
						const double D = FVector3f::Dist(F->P, Q);
						if (D < 0.1 && (F->U0 - R.Uv0[K]).GetAbsMax() < 1e-4f && (F->U1 - R.Uv1[K]).GetAbsMax() < 1e-6f
							&& (F->U2 - R.Uv2[K]).GetAbsMax() < 1e-6f)
						{
							bHit = true;
							WorstP = FMath::Max(WorstP, D);
							break;
						}
					}
				}
				Matched += bHit;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d patches, %d sections; %d of %d k/7 vertices = a static mesh vertex (position within %.4f cm, UV0..2)"),
			*Stem, R.NumPatches(), R.Sections.Num(), Matched, Checked, WorstP));
		TestEqual(FString::Printf(TEXT("%s: k/7 vertices = the imported terrain mesh"), *Stem), Matched, Checked);
	}
	if (Tracks == 0)
	{
		SSXTest::Skip(*this, TEXT("no track data: skipped"));
	}
	return true;
}
#endif

#endif
