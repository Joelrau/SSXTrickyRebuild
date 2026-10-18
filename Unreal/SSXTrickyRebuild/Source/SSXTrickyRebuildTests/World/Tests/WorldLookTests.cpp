#include "AssetRegistry/IAssetRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "StaticMeshResources.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeRiders.h"
#include "UI/SSXFrontEndData.h"
#include "World/SSXWorldLighting.h"
#include "World/SSXWorldPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags LookTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	bool ReadCsv(const FString& Name, TArray<TArray<FString>>& Rows, FAutomationTestBase& Test)
	{
		TArray<FString> Lines;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Name);
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 2)
		{
			Test.AddError(FString::Printf(TEXT("cannot read SimData/%s (run tools/make_world_reference.py)"), *Name));
			return false;
		}
		for (int32 I = 1; I < Lines.Num(); ++I)
		{
			TArray<FString>& C = Rows.AddDefaulted_GetRef();
			Lines[I].ParseIntoArray(C, TEXT(","), false);
		}
		return true;
	}

	// Game space (x, y, z) -> Unreal mesh space (x, -y, z): glTF Y-up through Interchange.
	FVector3f GameToMesh(double X, double Y, double Z) { return FVector3f(float(X), float(-Y), float(Z)); }

	const UMaterialInterface* RootMaterial(const UMaterialInterface* M)
	{
		while (const UMaterialInstance* MI = Cast<UMaterialInstance>(M))
		{
			M = MI->Parent;
		}
		return M;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldPropLightTest, "SSX.World.PropLight", LookTestFlags)
bool FSSXWorldPropLightTest::RunTest(const FString&)
{
	TArray<TArray<FString>> Rows;
	if (!ReadCsv(TEXT("prop_light_reference.csv"), Rows, *this))
	{
		return false;
	}
	TMap<FString, int32> PerTrack, PerTrackExact;
	int32 Checked = 0, Exact = 0, Missing = 0, MaxDiff = 0, BadMaterial = 0;
	TSet<FString> MeshesSeen;
	for (const TArray<FString>& C : Rows)
	{
		if (C.Num() < 13) { continue; }
		const FString& Map = C[0];
		const FString& Stem = C[1];
		const FString& Name = C[2];
		const FString Path = FString::Printf(TEXT("/Game/Tracks/%s/%s/StaticMeshes/%s.%s"), *Map, *Stem, *Name, *Name);
		const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		const FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!RD || RD->LODResources.Num() == 0)
		{
			if (++Missing <= 3) { AddError(FString::Printf(TEXT("%s not found (rebuild the track maps)"), *Path)); }
			continue;
		}
		if (!MeshesSeen.Contains(Path))
		{
			MeshesSeen.Add(Path);
			const UMaterialInterface* Root = RootMaterial(Mesh->GetMaterial(0));
			const FString RootName = Root ? Root->GetName() : FString(TEXT("none"));
			if (!RootName.StartsWith(TEXT("M_SSXProp")))
			{
				++BadMaterial;
				AddError(FString::Printf(TEXT("%s uses %s, not M_SSXProp*"), *Name, *RootName));
			}
		}
		const FStaticMeshLODResources& L = RD->LODResources[0];
		const FPositionVertexBuffer& Pos = L.VertexBuffers.PositionVertexBuffer;
		const FColorVertexBuffer& Col = L.VertexBuffers.ColorVertexBuffer;
		if (Col.GetNumVertices() != Pos.GetNumVertices())
		{
			AddError(FString::Printf(TEXT("%s has no vertex colours"), *Name));
			++Missing;
			continue;
		}
		const FVector3f P = GameToMesh(FCString::Atod(*C[3]), FCString::Atod(*C[4]), FCString::Atod(*C[5]));
		const FVector3f N = GameToMesh(FCString::Atod(*C[6]), FCString::Atod(*C[7]), FCString::Atod(*C[8])).GetSafeNormal();
		const FColor Want(FCString::Atoi(*C[9]), FCString::Atoi(*C[10]), FCString::Atoi(*C[11]), FCString::Atoi(*C[12]));
		int32 Best = -1;
		double BestDot = 0.97;
		for (uint32 V = 0; V < Pos.GetNumVertices(); ++V)
		{
			if (FVector3f::DistSquared(Pos.VertexPosition(V), P) > 0.05f * 0.05f) { continue; }
			const double D = FVector3f::DotProduct(FVector3f(L.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(V)), N);
			if (D > BestDot) { BestDot = D; Best = int32(V); }
		}
		if (Best < 0)
		{
			if (++Missing <= 3) { AddError(FString::Printf(TEXT("%s: no vertex at %s / normal %s"), *Name, *P.ToString(), *N.ToString())); }
			continue;
		}
		const FColor Got = Col.VertexColor(Best);
		const int32 Diff = FMath::Max(FMath::Max(FMath::Abs(Got.R - Want.R), FMath::Abs(Got.G - Want.G)),
			FMath::Max(FMath::Abs(Got.B - Want.B), FMath::Abs(Got.A - Want.A)));
		MaxDiff = FMath::Max(MaxDiff, Diff);
		++Checked;
		++PerTrack.FindOrAdd(Stem);
		if (Diff == 0) { ++Exact; ++PerTrackExact.FindOrAdd(Stem); }
		else if (Diff > 1)
		{
			AddError(FString::Printf(TEXT("%s vertex %d: colour %s, prop_light.py %s"), *Name, Best, *Got.ToString(), *Want.ToString()));
		}
	}
	for (const TPair<FString, int32>& T : PerTrack)
	{
		AddInfo(FString::Printf(TEXT("%s: %d / %d vertex colours equal prop_light.py exactly"), *T.Key, PerTrackExact.FindRef(T.Key), T.Value));
	}
	AddInfo(FString::Printf(TEXT("prop vertex colours vs models/prop_light.py: %d checked in %d meshes, %d exact, max diff %d, %d missing"),
		Checked, MeshesSeen.Num(), Exact, MaxDiff, Missing));
	return Missing == 0 && BadMaterial == 0 && MaxDiff <= 1 && Checked > 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldTwoSidedPropsTest, "SSX.World.TwoSidedProps", LookTestFlags)
bool FSSXWorldTwoSidedPropsTest::RunTest(const FString&)
{
	IAssetRegistry* AR = IAssetRegistry::Get();
	if (!AR)
	{
		AddError(TEXT("no asset registry"));
		return false;
	}
	AR->SearchAllAssets(true);
	FARFilter Filter;
	Filter.PackagePaths = { FName(TEXT("/Game/Tracks")), FName(TEXT("/Game/FrontEnd")) };
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	TArray<FAssetData> Meshes;
	AR->GetAssets(Filter, Meshes);
	TMap<FName, int32> MaterialTwoSided;
	TSet<FName> WasOneSided;
	auto Classify = [&](FName Package) -> int32
	{
		if (const int32* C = MaterialTwoSided.Find(Package)) { return *C; }
		int32 R = -1;
		TArray<FAssetData> In;
		AR->GetAssetsByPackageName(Package, In);
		for (const FAssetData& A : In)
		{
			if (A.IsInstanceOf(UMaterialInterface::StaticClass()))
			{
				const UMaterialInterface* M = Cast<UMaterialInterface>(A.GetAsset());
				R = (M && M->IsTwoSided()) ? 1 : 0;
				const FString Root = RootMaterial(M) ? RootMaterial(M)->GetName() : FString();
				if (Root == TEXT("M_SSXProp") || Root == TEXT("M_SSXProp_Masked") || Root == TEXT("M_SSXPropOpaque")) { WasOneSided.Add(Package); }
				break;
			}
		}
		MaterialTwoSided.Add(Package, R);
		return R;
	};
	struct FCount { int32 Meshes = 0, Slots = 0, OneSided = 0, Before = 0; };
	TMap<FString, FCount> PerArea;
	int32 Bad = 0;
	for (const FAssetData& Mesh : Meshes)
	{
		TArray<FString> Parts;
		Mesh.PackagePath.ToString().ParseIntoArray(Parts, TEXT("/"));
		const FString Area = (Parts.Num() >= 2 && Parts[1] == TEXT("Tracks") && Parts.Num() >= 3) ? Parts[2] : FString(TEXT("FrontEnd"));
		FCount& C = PerArea.FindOrAdd(Area);
		++C.Meshes;
		TArray<FName> Deps;
		AR->GetDependencies(Mesh.PackageName, Deps, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);
		bool bOne = false, bBefore = false;
		for (const FName& D : Deps)
		{
			const int32 K = Classify(D);
			if (K < 0) { continue; }
			bBefore |= WasOneSided.Contains(D);
			++C.Slots;
			if (K == 0)
			{
				bOne = true;
				if (++Bad <= 8) { AddError(FString::Printf(TEXT("%s: material %s is one-sided"), *Mesh.AssetName.ToString(), *D.ToString())); }
			}
		}
		C.OneSided += bOne ? 1 : 0;
		C.Before += bBefore ? 1 : 0;
	}
	int32 Total = 0;
	for (const TPair<FString, FCount>& P : PerArea)
	{
		Total += P.Value.Meshes;
		AddInfo(FString::Printf(TEXT("%s: %d meshes, %d material references, %d meshes differing from the original rule (two-sided); %d were one-sided before R26"),
			*P.Key, P.Value.Meshes, P.Value.Slots, P.Value.OneSided, P.Value.Before));
		TestEqual(FString::Printf(TEXT("%s: one-sided meshes"), *P.Key), P.Value.OneSided, 0);
	}
	if (Total == 0)
	{
		AddError(TEXT("no track meshes under /Game/Tracks (rebuild the track maps)"));
	}
	for (const TCHAR* Path : { TEXT("/Game/World/M_SSXRiderLit.M_SSXRiderLit"), TEXT("/Game/World/M_SSXRiderLit_Masked.M_SSXRiderLit_Masked"),
		TEXT("/Game/World/M_SSXPropCrowd.M_SSXPropCrowd"), TEXT("/Game/World/M_SSXProp.M_SSXProp"), TEXT("/Game/World/M_SSXProp_Masked.M_SSXProp_Masked"),
		TEXT("/Game/World/M_SSXPropOpaque.M_SSXPropOpaque"), TEXT("/Game/World/M_SSXTerrain.M_SSXTerrain"), TEXT("/Game/World/M_SSXTerrainOpaque.M_SSXTerrainOpaque"),
		TEXT("/Game/World/M_SSXSky.M_SSXSky"), TEXT("/Game/Particles/M_SSXParticleAlpha.M_SSXParticleAlpha"),
		TEXT("/Game/Particles/M_SSXParticleAdd.M_SSXParticleAdd"), TEXT("/Game/Particles/M_SSXParticleDarken.M_SSXParticleDarken"),
		TEXT("/Game/Particles/M_SSXParticleBrighten.M_SSXParticleBrighten") })
	{
		const UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
		TestTrue(FString::Printf(TEXT("%s two-sided"), Path), M && M->IsTwoSided());
	}
	if (const UMaterial* Sh = LoadObject<UMaterial>(nullptr, TEXT("/Game/Particles/M_SSXShadowProject.M_SSXShadowProject")))
	{
		TestTrue(TEXT("shadow projection box: one-sided, no depth test (port device)"), !Sh->IsTwoSided() && Sh->bDisableDepthTest);
	}
	AddInfo(FString::Printf(TEXT("two-sided rule: %d static meshes, %d materials classified, %d one-sided references"), Total, MaterialTwoSided.Num(), Bad));
	return Total > 0 && Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldClearColourTest, "SSX.World.ClearColour", LookTestFlags)
bool FSSXWorldClearColourTest::RunTest(const FString&)
{
	TArray<FSSXTrackPresentation> Table;
	FString Err;
	if (!FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table, &Err))
	{
		AddError(Err);
		return false;
	}
	TArray<TArray<FString>> Rows;
	if (!ReadCsv(TEXT("world_clear_reference.csv"), Rows, *this))
	{
		return false;
	}
	int32 Pass = 0;
	for (const TArray<FString>& C : Rows)
	{
		if (C.Num() < 6) { continue; }
		const int32 Id = FCString::Atoi(*C[0]);
		if (!Table.IsValidIndex(Id)) { AddError(FString::Printf(TEXT("no table record %d"), Id)); continue; }
		const FColor Want(FCString::Atoi(*C[2]), FCString::Atoi(*C[3]), FCString::Atoi(*C[4]), FCString::Atoi(*C[5]));
		const FColor Got = Table[Id].ClearColour();
		// The MPC value: bytes / 255, which the authored-colour post process shows unchanged (x 255 back).
		const FLinearColor Param = USSXWorldPresentation::ClearColourParam(Table[Id]);
		const bool bParam = FMath::RoundToInt(Param.R * 255.f) == Want.R && FMath::RoundToInt(Param.G * 255.f) == Want.G
			&& FMath::RoundToInt(Param.B * 255.f) == Want.B;
		if (Got == Want && bParam)
		{
			++Pass;
		}
		else
		{
			AddError(FString::Printf(TEXT("track %d %s: clear %s, prop_light.clear_colour %s"), Id, *C[1], *Got.ToString(), *Want.ToString()));
		}
	}
	TestEqual(TEXT("Snowdream (7,0,38,128)"), Table[1].ClearColour(), FColor(7, 0, 38, 0x80));
	TestEqual(TEXT("Alaska (0,0,16,128)"), Table[10].ClearColour(), FColor(0, 0, 16, 0x80));
	if (Table.IsValidIndex(USSXWorldPresentation::FrontEndId))
	{
		const FLinearColor Fe = USSXWorldPresentation::ClearColourFor(USSXWorldPresentation::FrontEndId, Table[USSXWorldPresentation::FrontEndId]);
		TestTrue(TEXT("front end clears (0, 0, 0)"), Fe.R == 0.f && Fe.G == 0.f && Fe.B == 0.f);
	}
	TestEqual(TEXT("race track clears table 0x48"), USSXWorldPresentation::ClearColourFor(1, Table[1]), USSXWorldPresentation::ClearColourParam(Table[1]));
	AddInfo(FString::Printf(TEXT("clear colour vs table 0x48 / prop_light.clear_colour: %d / %d records"), Pass, Rows.Num()));
	return Pass == Rows.Num() && Pass > 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldRiderSlotsTest, "SSX.World.RiderSlots", LookTestFlags)
bool FSSXWorldRiderSlotsTest::RunTest(const FString&)
{
	{
		FSSXLightSlots S;
		TestEqual(TEXT("single light -> last slot"), S.Insert(FVector(1, 1, 1), FVector(0, 0, 1)), 2);
		TestTrue(TEXT("slots 0/1 stay empty"), S.Colour[0].IsZero() && S.Colour[1].IsZero() && S.Direction[0] == FVector(1, 0, 0));
		TestEqual(TEXT("weaker second -> slot 1"), S.Insert(FVector(0.5, 0, 0), FVector(0, 1, 0)), 1);
		TestEqual(TEXT("stronger third -> slot 2"), S.Insert(FVector(2, 2, 2), FVector(1, 0, 0)), 2);
		TestEqual(TEXT("tie with slot 0 is rejected"), S.Insert(FVector(0.25, 0.25, 0), FVector(0, 0, -1)), -1);
		TestEqual(TEXT("darkening light by |sum|, ties with slot 1 stay below it"), S.Insert(FVector(-1, -1, -1), FVector(0, 0, -1)), 0);
		TestTrue(TEXT("weakest dropped"), FMath::IsNearlyEqual(S.Score[0], 3.0) && FMath::IsNearlyEqual(S.Score[1], 3.0) && FMath::IsNearlyEqual(S.Score[2], 6.0));
	}
	TArray<TArray<FString>> Rows;
	if (!ReadCsv(TEXT("world_slot_reference.csv"), Rows, *this))
	{
		return false;
	}
	int32 Pass = 0, Fail = 0, Lights = 0;
	for (const TArray<FString>& C : Rows)
	{
		if (C.Num() < 4) { continue; }
		const int32 N = FCString::Atoi(*C[0]);
		if (C.Num() < 4 + 3 * N) { continue; }
		FSSXLightSlots S;
		for (int32 K = 0; K < N; ++K)
		{
			const FVector Colour(FCString::Atod(*C[4 + 3 * K]), FCString::Atod(*C[5 + 3 * K]), FCString::Atod(*C[6 + 3 * K]));
			S.Insert(Colour, FVector(double(K), 0.0, 0.0));
		}
		Lights += N;
		bool bOk = true;
		FString Got;
		for (int32 Slot = 0; Slot < FSSXRiderLight::Slots; ++Slot)
		{
			const int32 Want = FCString::Atoi(*C[1 + Slot]);
			const int32 Have = S.Score[Slot] > 0.0 ? int32(S.Direction[Slot].X) : -1;
			bOk &= Want == Have;
			Got += FString::Printf(TEXT("%d "), Have);
		}
		if (bOk) { ++Pass; }
		else if (++Fail <= 5) { AddError(FString::Printf(TEXT("case %d: slots %s, rider_light.py %s %s %s"), Pass + Fail, *Got, *C[1], *C[2], *C[3])); }
	}
	AddInfo(FString::Printf(TEXT("slot selection vs models/rider_light.insert_slot: %d / %d lists (%d lights) match"), Pass, Pass + Fail, Lights));
	return Fail == 0 && Pass > 0;
}

namespace
{
	FTransform ComponentWorld(const USceneComponent* C)
	{
		FTransform T = C->GetRelativeTransform();
		for (const USceneComponent* P = C->GetAttachParent(); P; P = P->GetAttachParent())
		{
			T = T * P->GetRelativeTransform();
		}
		return T;
	}

	struct FTri { FVector3f A, B, C; FVector3f Min, Max; int32 Comp; };

	bool SegmentHitsTri(const FVector3f& O, const FVector3f& D, const FTri& T)
	{
		const FVector3f E1 = T.B - T.A, E2 = T.C - T.A;
		const FVector3f Pv = FVector3f::CrossProduct(D, E2);
		const float Det = FVector3f::DotProduct(E1, Pv);
		if (FMath::Abs(Det) < 1e-9f) { return false; }
		const float Inv = 1.f / Det;
		const FVector3f Tv = O - T.A;
		const float U = FVector3f::DotProduct(Tv, Pv) * Inv;
		if (U < 0.f || U > 1.f) { return false; }
		const FVector3f Qv = FVector3f::CrossProduct(Tv, E1);
		const float V = FVector3f::DotProduct(D, Qv) * Inv;
		if (V < 0.f || U + V > 1.f) { return false; }
		const float S = FVector3f::DotProduct(E2, Qv) * Inv;
		return S > 0.f && S < 1.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFeFloorTest, "SSX.UI.FeFloor", LookTestFlags)
bool FSSXUIFeFloorTest::RunTest(const FString&)
{
	FSSXFeCameraSet Cams;
	if (!Cams.Load(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json"))))
	{
		AddError(TEXT("cannot load converted/frontend/fe_cameras.json (tools/export_fe_cameras.py)"));
		return false;
	}
	SSXFeRiders::FData Data;
	FString Err;
	if (!Data.Load(SSXFeRiders::FData::DefaultPath(), &Err))
	{
		AddError(Err);
		return false;
	}
	UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/FrontEnd.FrontEnd"));
	if (!World || !World->PersistentLevel)
	{
		AddError(TEXT("/Game/Maps/FrontEnd missing (Scripts/build_frontend_map.py)"));
		return false;
	}

	TArray<FTri> Tris;
	int32 Comps = 0, OpaqueComps = 0, HiddenComps = 0;
	for (AActor* A : World->PersistentLevel->Actors)
	{
		if (!A) { continue; }
		TArray<UStaticMeshComponent*> Meshes;
		A->GetComponents(Meshes);
		for (const UStaticMeshComponent* M : Meshes)
		{
			const UStaticMesh* SM = M ? M->GetStaticMesh() : nullptr;
			const FStaticMeshRenderData* RD = SM ? SM->GetRenderData() : nullptr;
			if (!RD || RD->LODResources.Num() == 0) { continue; }
			if (M->IsEditorOnly() || A->GetClass()->GetName().Contains(TEXT("CameraActor"))) { continue; }
			++Comps;
			if (!M->GetVisibleFlag() || M->bHiddenInGame || A->IsHidden() || !M->bRenderInMainPass || !M->bRenderInDepthPass)
			{
				++HiddenComps;
				continue;
			}
			const FTransform W = ComponentWorld(M);
			const FStaticMeshLODResources& L = RD->LODResources[0];
			TArray<uint32> Idx;
			L.IndexBuffer.GetCopy(Idx);
			bool bAnyOpaque = false;
			for (const FStaticMeshSection& Sec : L.Sections)
			{
				const UMaterialInterface* Mat = M->GetMaterial(Sec.MaterialIndex);
				if (!Mat || Mat->GetBlendMode() != BLEND_Opaque) { continue; }
				bAnyOpaque = true;
				for (uint32 T = 0; T < Sec.NumTriangles; ++T)
				{
					FTri Tri;
					const uint32 I0 = Sec.FirstIndex + 3 * T;
					Tri.A = FVector3f(W.TransformPosition(FVector(L.VertexBuffers.PositionVertexBuffer.VertexPosition(Idx[I0]))));
					Tri.B = FVector3f(W.TransformPosition(FVector(L.VertexBuffers.PositionVertexBuffer.VertexPosition(Idx[I0 + 1]))));
					Tri.C = FVector3f(W.TransformPosition(FVector(L.VertexBuffers.PositionVertexBuffer.VertexPosition(Idx[I0 + 2]))));
					Tri.Min = Tri.A.ComponentMin(Tri.B).ComponentMin(Tri.C);
					Tri.Max = Tri.A.ComponentMax(Tri.B).ComponentMax(Tri.C);
					Tri.Comp = Comps;
					Tris.Add(Tri);
				}
			}
			OpaqueComps += bAnyOpaque ? 1 : 0;
		}
	}
	if (Tris.Num() == 0)
	{
		AddError(TEXT("FrontEnd map has no opaque stage triangles"));
		return false;
	}

	const FVector2D BoardXY[] = { { 18.8, 70.4 }, { 12.9, 50.0 }, { -211.9, 50.0 } };
	const double BoardZ[] = { -198.0, -221.0, -267.0, -296.0 };
	int32 Segments = 0, Occluded = 0, CamCount = 0;
	for (const FSSXFeCamera& Cam : Cams.Cameras)
	{
		if (!Cam.Name.StartsWith(TEXT("charSelect_"))) { continue; }
		++CamCount;
		for (const float T : { 0.f, Cam.Duration })
		{
			const FVector CamGame = Cam.Sample(T).Pos;
			const double Ang = FMath::RadiansToDegrees(FMath::Atan2(CamGame.Y, CamGame.X));
			const int32 P = ((FMath::RoundToInt((Ang - 90.0) / 30.0) % 12) + 12) % 12;
			const FVector3f O(float(CamGame.X), float(-CamGame.Y), float(CamGame.Z));
			for (int32 K = P - 2; K <= P + 2; ++K)
			{
				const SSXAnim::FRowMat Root = SSXFeRiders::RiderRoot((K + 12) % 12, Data.RingRadius, Data.RingZ);
				for (const FVector2D& XY : BoardXY)
				{
					for (const double Z : BoardZ)
					{
						const FVector3f B = Root.TransformPoint(FVector3f(float(XY.X), float(XY.Y), float(Z)));
						const FVector3f End(B.X, -B.Y, B.Z);
						const FVector3f D = End - O;
						const FVector3f SMin = O.ComponentMin(End), SMax = O.ComponentMax(End);
						bool bHit = false;
						for (const FTri& Tri : Tris)
						{
							if (Tri.Max.X < SMin.X || Tri.Min.X > SMax.X || Tri.Max.Y < SMin.Y || Tri.Min.Y > SMax.Y
								|| Tri.Max.Z < SMin.Z || Tri.Min.Z > SMax.Z) { continue; }
							if (SegmentHitsTri(O, D, Tri)) { bHit = true; break; }
						}
						++Segments;
						if (bHit) { ++Occluded; }
						else if (Segments - Occluded <= 5)
						{
							AddError(FString::Printf(TEXT("%s t=%.2f: board of slot %d at (%.1f, %.1f, %.0f) not occluded"), *Cam.Name, T, (K + 12) % 12, XY.X, XY.Y, Z));
						}
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("FE floor: %d mesh components (%d with opaque sections, %d hidden), %d opaque triangles; %d cameras, %d / %d camera-to-board segments occluded"),
		Comps, OpaqueComps, HiddenComps, Tris.Num(), CamCount, Occluded, Segments));
	TestEqual(TEXT("no hidden stage meshes"), HiddenComps, 0);
	return Segments > 0 && Occluded == Segments;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldLightmapBlendTest, "SSX.World.LightmapBlend", LookTestFlags)

bool FSSXWorldLightmapBlendTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("a 128, rgb 0: unchanged"), SSXTerrainLook::Blend(FVector3d(100, 50, 20), FVector4d(0, 0, 0, 128)).Equals(FVector3d(100, 50, 20)));
	TestTrue(TEXT("a 255: x 255/128, clamped"), SSXTerrainLook::Blend(FVector3d(100, 200, 0), FVector4d(0, 0, 0, 255)).Equals(FVector3d(199.21875, 255, 0)));
	TestTrue(TEXT("rgb tint subtracted first, negative clamps to 0"), SSXTerrainLook::Blend(FVector3d(10, 50, 0), FVector4d(20, 10, 0, 128)).Equals(FVector3d(0, 40, 0)));

	TArray<TArray<FString>> B, C;
	if (ReadCsv(TEXT("lightmap_blend_reference.csv"), B, *this))
	{
		double MaxErr = 0.0;
		int32 N = 0;
		for (const TArray<FString>& R : B)
		{
			if (R.Num() < 10) continue;
			auto I = [&R](int32 K) { return FCString::Atod(*R[K]); };
			const FVector3d Got = SSXTerrainLook::Blend(FVector3d(I(0), I(1), I(2)), FVector4d(I(3), I(4), I(5), I(6)));
			MaxErr = FMath::Max(MaxErr, FMath::Max3(FMath::Abs(FMath::FloorToDouble(Got.X) - I(7)), FMath::Abs(FMath::FloorToDouble(Got.Y) - I(8)),
				FMath::Abs(FMath::FloorToDouble(Got.Z) - I(9))));
			++N;
		}
		AddInfo(FString::Printf(TEXT("blend: %d sampled gari_L texels vs look.terrain_pixel, max error %.3f (after the GS truncation)"), N, MaxErr));
		TestTrue(TEXT("blend = look.terrain_pixel on every sampled texel"), N > 100 && MaxErr == 0.0);
	}
	if (ReadCsv(TEXT("lightmap_clamp_reference.csv"), C, *this))
	{
		int32 Match = 0, Differ = 0, Escaped = 0, Interior = 0;
		double MaxInterior = 0.0;
		for (const TArray<FString>& R : C)
		{
			if (R.Num() < 9) continue;
			auto D = [&R](int32 K) { return FCString::Atod(*R[K]); };
			const FVector4d Rect(D(1), D(2), D(3), D(4));
			const FIntVector4 Packet(FCString::Atoi(*R[5]), FCString::Atoi(*R[6]), FCString::Atoi(*R[7]), FCString::Atoi(*R[8]));
			const FIntVector4 Got = SSXTerrainLook::RegionClamp(Rect);
			(Got == Packet ? Match : Differ)++;
			const FVector2D Origin(Rect.X / SSXTerrainLook::AtlasCols, Rect.Y / SSXTerrainLook::AtlasRows);
			for (int32 Iu = 0; Iu <= 8; ++Iu)
			{
				for (int32 Iv = 0; Iv <= 8; ++Iv)
				{
					const double U = Iu / 8.0, V = Iv / 8.0;
					const FVector2D A = SSXTerrainLook::AtlasUV(FVector2D(U, V), Origin);
					const double S = A.X * SSXTerrainLook::AtlasCols * 128.0, T = A.Y * SSXTerrainLook::AtlasRows * 128.0;
					const int32 S0 = FMath::FloorToInt32(S - 0.5), T0 = FMath::FloorToInt32(T - 0.5);
					const bool bS1 = S - 0.5 > S0 + 1e-9, bT1 = T - 0.5 > T0 + 1e-9;
					if (S0 < Packet.X || S0 + (bS1 ? 1 : 0) > Packet.Y || T0 < Packet.Z || T0 + (bT1 ? 1 : 0) > Packet.W)
					{
						++Escaped;
					}
					// inside the edge texel centres the sample point is the transposed VU coordinate (look.lightmap_uv)
					const double Sx = (Rect.X + V / 16.0) * 128.0, Tx = (Rect.Y + U / 16.0) * 128.0;
					if (Sx >= Packet.X + 0.5 && Sx <= Packet.Y + 0.5 && Tx >= Packet.Z + 0.5 && Tx <= Packet.W + 0.5)
					{
						MaxInterior = FMath::Max(MaxInterior, FMath::Max(FMath::Abs(S - Sx), FMath::Abs(T - Tx)));
						++Interior;
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("clamp: %d RAM CLAMP_2 packets, %d = RegionClamp(rect), %d differ; %d samples leave the tile; %d interior samples, max |uv - look.lightmap_uv| %.2e texels"),
			Match + Differ, Match, Differ, Escaped, Interior, MaxInterior));
		TestTrue(TEXT("every RAM packet = RegionClamp(patch rect)"), Match > 100 && Differ == 0);
		TestEqual(TEXT("no bilinear tap leaves the 8x8 tile"), Escaped, 0);
		TestTrue(TEXT("interior samples follow the transposed tile (s = x + v/16, t = y + u/16)"), Interior > 0 && MaxInterior < 1e-4);
	}
	return !HasAnyErrors();
}

// 0x30F960
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldTerrainTexOrientationTest, "SSX.World.TerrainTexOrientation", LookTestFlags)

bool FSSXWorldTerrainTexOrientationTest::RunTest(const FString&)
{
	TArray<TArray<FString>> Rows;
	if (!ReadCsv(TEXT("terrain_uv_reference.csv"), Rows, *this))
	{
		return false;
	}
	constexpr double IceBlue = 20.0;
	int32 Patches = 0, Verts = 0, BadUv = 0, Edges = 0, EdgeMatch = 0;
	double MaxErr = 0.0;
	for (const TArray<FString>& C : Rows)
	{
		if (C.Num() >= 7 && C[2] == TEXT("total"))
		{
			const int32 M = FCString::Atoi(*C[3]), N = FCString::Atoi(*C[4]), MNeg = FCString::Atoi(*C[5]);
			AddInfo(FString::Printf(TEXT("%s: %d / %d transition edges continue the neighbour with the GS T, %d / %d with T negated"),
				*C[1], M, N, MNeg, N));
			TestTrue(FString::Printf(TEXT("%s: GS orientation fits the authored transitions"), *C[1]), N > 100 && M > 0.9 * N && MNeg < 0.6 * N);
			continue;
		}
		if (C.Num() < 18) { continue; }
		auto D = [&C](int32 K) { return FCString::Atod(*C[K]); };
		const FString Path = FString::Printf(TEXT("/Game/Tracks/%s/%s/StaticMeshes/%s_terrain.%s_terrain"), *C[0], *C[1], *C[1], *C[1]);
		const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		const FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!RD || RD->LODResources.Num() == 0)
		{
			AddError(FString::Printf(TEXT("%s not found (rebuild the track maps)"), *Path));
			return false;
		}
		const FStaticMeshLODResources& L = RD->LODResources[0];
		const FStaticMeshVertexBuffer& VB = L.VertexBuffers.StaticMeshVertexBuffer;
		if (VB.GetNumTexCoords() < 3)
		{
			AddError(FString::Printf(TEXT("%s has %d UV channels, expected 3"), *Path, VB.GetNumTexCoords()));
			return false;
		}
		const FVector2f Origin(float(D(4)), float(D(5)));
		const FVector2d Corner[4] = { {D(6), D(7)}, {D(8), D(9)}, {D(10), D(11)}, {D(12), D(13)} };
		int32 Found = 0;
		for (uint32 V = 0; V < VB.GetNumVertices(); ++V)
		{
			if (!VB.GetVertexUV(V, 2).Equals(Origin, 1e-4f)) { continue; }
			const FVector2f Local = VB.GetVertexUV(V, 1);
			const double U = Local.X, W = Local.Y;
			const FVector2d Want = (1 - U) * (1 - W) * Corner[0] + U * (1 - W) * Corner[1] + (1 - U) * W * Corner[2] + U * W * Corner[3];
			const FVector2f Got = VB.GetVertexUV(V, 0);
			const double Err = FMath::Max(FMath::Abs(Got.X - Want.X), FMath::Abs(Got.Y - Want.Y));
			MaxErr = FMath::Max(MaxErr, Err);
			if (Err > 2e-3 && ++BadUv <= 3)
			{
				AddError(FString::Printf(TEXT("%s patch %s vertex %u: (u, v) %s UV0 %s, GS (S, T) %s"), *C[1], *C[2], V,
					*Local.ToString(), *Got.ToString(), *Want.ToString()));
			}
			if (Found == 0)
			{
				for (const FStaticMeshSection& Sec : L.Sections)
				{
					if (V >= Sec.MinVertexIndex && V <= Sec.MaxVertexIndex)
					{
						const UMaterialInterface* Root = RootMaterial(Mesh->GetMaterial(Sec.MaterialIndex));
						TestTrue(FString::Printf(TEXT("patch %s drawn by M_SSXTerrain (UV0 = base texture)"), *C[2]),
							Root && Root->GetName().StartsWith(TEXT("M_SSXTerrain")));
					}
				}
			}
			++Found;
		}
		TestTrue(FString::Printf(TEXT("%s patch %s: %d vertices found (tess 7 = 64)"), *C[1], *C[2], Found), Found >= 56);
		Verts += Found;
		++Patches;
		for (int32 K : {14, 16})
		{
			if (FMath::Abs(D(K + 1)) < 1e-9) { continue; }
			++Edges;
			const bool bOk = (D(K) > IceBlue) == (D(K + 1) > IceBlue);
			EdgeMatch += bOk ? 1 : 0;
			TestTrue(FString::Printf(TEXT("%s patch %s (texture %s) %s edge band %.1f vs neighbour %.1f"), *C[1], *C[2], *C[3],
				K == 14 ? TEXT("v=0") : TEXT("v=1"), D(K), D(K + 1)), bOk);
		}
	}
	AddInfo(FString::Printf(TEXT("terrain UV0 = GS (S, T): %d patches, %d vertices, max error %.2e, %d wrong; %d / %d edges continue the neighbour"),
		Patches, Verts, MaxErr, BadUv, EdgeMatch, Edges));
	TestTrue(TEXT("checked the PEEL halfpipe patches"), Patches >= 10 && Edges >= 15);
	TestEqual(TEXT("every vertex UV0 = GS (S, T)"), BadUv, 0);
	return !HasAnyErrors();
}

#endif
