#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Sim/Anim/RiderPose.h"
#include "Animation/MorphTarget.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Characters/SSXRiderPartsMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "SnowboarderPawn.h"
#include "Harness/RiderSetup.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Ground/GroundStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CharacterTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	TArray<FString> ExpectedBones(const FString& Id)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("../../converted/characters") / Id / (Id + TEXT("_bones.txt")));
		TArray<FString> Names;
		FFileHelper::LoadFileToStringArray(Names, *Path);
		Names.RemoveAll([](const FString& N) { return N.TrimStartAndEnd().IsEmpty(); });
		return Names;
	}

	FVector BonePosition(const FReferenceSkeleton& Ref, int32 Index)
	{
		FTransform T = FTransform::Identity;
		for (int32 I = Index; I != INDEX_NONE; I = Ref.GetParentIndex(I))
		{
			T = T * Ref.GetRefBonePose()[I];
		}
		return T.GetLocation();
	}

	struct FCVarScope
	{
		IConsoleVariable* Var;
		FString Old;
		explicit FCVarScope(const TCHAR* Name) : Var(IConsoleManager::Get().FindConsoleVariable(Name)) { Old = Var ? Var->GetString() : FString(); }
		void Set(const FString& V) { if (Var) { Var->Set(*V, ECVF_SetByCode); } }
		~FCVarScope() { Set(Old); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCharacterIdsTest, "SSX.Characters.Ids", CharacterTestFlags)

bool FSSXCharacterIdsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("13 riders"), SSXCharacters::AllIds().Num(), 13);
	TestEqual(TEXT("index 0"), SSXCharacters::IdForIndex(0), FString(TEXT("eddie")));
	TestEqual(TEXT("index 10"), SSXCharacters::IdForIndex(10), FString(TEXT("brodi")));
	TestEqual(TEXT("index 11"), SSXCharacters::IdForIndex(11), FString(TEXT("marisol")));
	TestEqual(TEXT("bad index"), SSXCharacters::IdForIndex(13), FString());
	TestEqual(TEXT("name"), SSXCharacters::NormalizeId(TEXT(" Zoe ")), FString(TEXT("zoe")));
	TestEqual(TEXT("mmm"), SSXCharacters::NormalizeId(TEXT("mmm")), FString(TEXT("zz_mmm")));
	TestEqual(TEXT("numeric"), SSXCharacters::NormalizeId(TEXT("6")), FString(TEXT("jp")));
	TestEqual(TEXT("unknown"), SSXCharacters::NormalizeId(TEXT("mallora")), FString());

	FCVarScope CVar(TEXT("ssx.Character"));
	TestNotNull(TEXT("ssx.Character exists"), CVar.Var);
	CVar.Set(TEXT(""));
	TestEqual(TEXT("index fallback"), SSXCharacters::ResolveId(TEXT(""), 3), FString(TEXT("mac")));
	TestEqual(TEXT("pawn id beats index"), SSXCharacters::ResolveId(TEXT("elise"), 3), FString(TEXT("elise")));
	CVar.Set(TEXT("psymon"));
	TestEqual(TEXT("console beats pawn id"), SSXCharacters::ResolveId(TEXT("elise"), 3), FString(TEXT("psymon")));
	TestEqual(TEXT("board class 2 = alpine"), FString(SSXCharacters::BoardSuffix(2)), FString(TEXT("al")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCharacterAssetsTest, "SSX.Characters.Assets", CharacterTestFlags)

bool FSSXCharacterAssetsTest::RunTest(const FString& Parameters)
{
	int32 Loaded = 0;
	for (const FString& Id : SSXCharacters::AllIds())
	{
		USkeletalMesh* Mesh = SSXCharacters::LoadRiderMesh(Id);
		const TArray<FString> Expected = ExpectedBones(Id);
		if (!Mesh)
		{
			if (Expected.Num() > 0)
			{
				AddWarning(FString::Printf(TEXT("%s: converted but not imported (Scripts/import_characters.py)"), *Id));
			}
			continue;
		}
		++Loaded;
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const int32 NumBones = Ref.GetNum();
		if (Expected.Num() > 0)
		{
			TestEqual(*FString::Printf(TEXT("%s bone count"), *Id), NumBones, Expected.Num());
			for (int32 I = 0; I < FMath::Min(NumBones, Expected.Num()); ++I)
			{
				if (Ref.FindBoneIndex(FName(*Expected[I])) == INDEX_NONE)
				{
					AddError(FString::Printf(TEXT("%s: bone %s missing"), *Id, *Expected[I]));
				}
			}
		}
		TestTrue(*FString::Printf(TEXT("%s has >= 20 bones (%d)"), *Id, NumBones), NumBones >= 20);
		TestEqual(*FString::Printf(TEXT("%s root bone"), *Id), Ref.GetBoneName(0), FName(TEXT("root")));

		const FBox Box = Mesh->GetImportedBounds().GetBox();
		const double Height = Box.Max.Z - Box.Min.Z;
		TestTrue(*FString::Printf(TEXT("%s soles at z=0 (%.2f)"), *Id, Box.Min.Z), FMath::Abs(Box.Min.Z) < 1.0);
		TestTrue(*FString::Printf(TEXT("%s height %.1f cm in 150..210"), *Id, Height), Height > 150.0 && Height < 210.0);
		const int32 Head = Ref.FindBoneIndex(TEXT("head"));
		const int32 EyeL = Ref.FindBoneIndex(TEXT("eye_l"));
		const int32 HandL = Ref.FindBoneIndex(TEXT("l_hand"));
		const int32 FootL = Ref.FindBoneIndex(TEXT("l_foot"));
		if (Head != INDEX_NONE && EyeL != INDEX_NONE && HandL != INDEX_NONE && FootL != INDEX_NONE)
		{
			const FVector H = BonePosition(Ref, Head), E = BonePosition(Ref, EyeL), L = BonePosition(Ref, HandL), F = BonePosition(Ref, FootL);
			TestTrue(*FString::Printf(TEXT("%s faces +Y (eye %.1f vs head %.1f)"), *Id, E.Y, H.Y), E.Y > H.Y + 3.0);
			TestTrue(*FString::Printf(TEXT("%s left hand at +X (%.1f)"), *Id, L.X), L.X > 30.0);
			TestTrue(*FString::Printf(TEXT("%s left foot low (%.1f)"), *Id, F.Z), F.Z < 15.0);
			AddInfo(FString::Printf(TEXT("%s: %d bones, height %.1f cm, head (%.1f %.1f %.1f)"), *Id, NumBones, Height, H.X, H.Y, H.Z));
		}
		else
		{
			AddError(FString::Printf(TEXT("%s: head/eye_l/l_hand/l_foot bones missing"), *Id));
		}
		for (int32 Class = 0; Class < 3; ++Class)
		{
			TestNotNull(*FString::Printf(TEXT("%s board %s"), *Id, SSXCharacters::BoardSuffix(Class)), SSXCharacters::LoadBoardMesh(Id, Class));
			// 0x201138
			TestNotNull(*FString::Printf(TEXT("%s board %s goofy"), *Id, SSXCharacters::BoardSuffix(Class)), SSXCharacters::LoadBoardMesh(Id, Class, true));
		}
	}
	if (Loaded == 0)
	{
		AddError(TEXT("no imported riders under /Game/Characters (run tools/convert_characters.py, then Scripts/import_characters.py)"));
	}
	AddInfo(FString::Printf(TEXT("%d riders imported"), Loaded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCharacterPawnHookTest, "SSX.Characters.PawnHook", CharacterTestFlags)

bool FSSXCharacterPawnHookTest::RunTest(const FString& Parameters)
{
	TArray<int32> Imported;
	for (int32 I = 0; I < SSXCharacters::AllIds().Num(); ++I)
	{
		if (SSXCharacters::LoadRiderMesh(SSXCharacters::IdForIndex(I)))
		{
			Imported.Add(I);
		}
	}
	if (Imported.Num() == 0)
	{
		AddError(TEXT("no imported riders under /Game/Characters"));
		return false;
	}
	FCVarScope CVar(TEXT("ssx.Character"));
	CVar.Set(TEXT(""));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXCharacterTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	ASnowboarderPawn* Pawn = World->SpawnActor<ASnowboarderPawn>();
	if (!TestNotNull(TEXT("pawn spawned"), Pawn))
	{
		return false;
	}
	USSXCharacterAppearance* Appearance = Pawn->GetAppearance();
	if (!TestNotNull(TEXT("appearance component"), Appearance))
	{
		return false;
	}
	USceneComponent* RiderRoot = nullptr;
	USceneComponent* BodyRoot = nullptr;
	TArray<USceneComponent*> Scene;
	Pawn->GetComponents(Scene);
	for (USceneComponent* C : Scene)
	{
		if (C->GetFName() == TEXT("RiderRoot")) { RiderRoot = C; }
		if (C->GetFName() == TEXT("BodyRoot")) { BodyRoot = C; }
	}
	if (!TestTrue(TEXT("pawn has RiderRoot/BodyRoot"), RiderRoot && BodyRoot))
	{
		return false;
	}

	auto PlaceholderVisible = [Pawn, Appearance]()
	{
		TArray<UStaticMeshComponent*> Parts;
		Pawn->GetComponents(Parts);
		int32 Visible = 0;
		for (UStaticMeshComponent* P : Parts)
		{
			if (P != Appearance->GetBoardMesh() && P->IsVisible() && P->ComponentTags.Num() > 0)
			{
				++Visible;
			}
		}
		return Visible;
	};

	const int32 First = Imported[0];
	const FString FirstId = SSXCharacters::IdForIndex(First);
	Pawn->Character = First;
	TestTrue(TEXT("apply by index"), Appearance->Apply(RiderRoot, BodyRoot, Pawn->CharacterId, Pawn->Character, Pawn->BoardClass));
	TestEqual(TEXT("shown id"), Appearance->GetShownId(), FirstId);
	UPoseableMeshComponent* Rider = Appearance->GetRiderMesh();
	if (TestNotNull(TEXT("rider component"), Rider))
	{
		TestTrue(TEXT("rider mesh"), Rider->GetSkinnedAsset() == SSXCharacters::LoadRiderMesh(FirstId));
		TestTrue(TEXT("rider visible"), Rider->IsVisible());
		TestTrue(TEXT("rider under BodyRoot"), Rider->GetAttachParent() == BodyRoot);
		TestTrue(TEXT("rider stands on the deck"), Rider->GetRelativeLocation().Z > 2.0 && Rider->GetRelativeLocation().Z < 20.0);
		const int32 Bones = Rider->GetSkinnedAsset() ? Rider->GetSkinnedAsset()->GetRefSkeleton().GetNum() : 0;
		const int32 Expected = ExpectedBones(FirstId).Num();
		if (Expected > 0)
		{
			TestEqual(TEXT("bone count via pawn"), Bones, Expected);
		}
	}
	TestNotNull(TEXT("board component"), Appearance->GetBoardMesh());
	if (Appearance->GetBoardMesh())
	{
		TestTrue(TEXT("board class 2 -> alpine"), Appearance->GetBoardMesh()->GetStaticMesh().Get() == SSXCharacters::LoadBoardMesh(FirstId, 2));
	}
	for (UPrimitiveComponent* Part : { static_cast<UPrimitiveComponent*>(Appearance->GetRiderMesh()), static_cast<UPrimitiveComponent*>(Appearance->GetBoardMesh()) })
	{
		if (Part)
		{
			TestTrue(FString::Printf(TEXT("%s: no Unreal shadow casting"), *Part->GetName()), !Part->CastShadow && !Part->bCastDynamicShadow && !Part->bCastStaticShadow);
			TestTrue(FString::Printf(TEXT("%s: custom depth (excluded from the projected shadow)"), *Part->GetName()), Part->bRenderCustomDepth);
		}
	}
	TestEqual(TEXT("placeholder hidden"), PlaceholderVisible(), 0);

	Appearance->SetGoofy(true);
	if (Rider)
	{
		TestTrue(TEXT("goofy yaw"), FMath::IsNearlyEqual(FMath::Abs(Rider->GetRelativeRotation().Yaw), 180.f, 0.5f));
	}
	Appearance->SetGoofy(false);

	if (SSXAnim::FAflLibrary::Shared() && Rider)
	{
		FPatchWorld Slope;
		SSXRiderSetup::BuildSlopeWorld(Slope, 15.0, 1);
		FRiderSim Sim;
		Sim.SetWorld(&Slope);
		Sim.Profile.BoardClass = Pawn->BoardClass;
		Sim.Profile.Character = First;
		RegisterGroundStates(Sim);
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.BoardClass = Pawn->BoardClass;
		FRiderInputWord In;
		for (int32 F = 0; F < 90; ++F)
		{
			In.Turn = F > 30 ? 0.7 : 0.0;
			Sim.Step(In);
			Appearance->StepAnimation(Sim);
		}
		const FQuat BodyRot = Sim.State.Orientation;
		if (TestTrue(TEXT("animated pose applied"), Appearance->ApplyAnimatedPose(Sim, Sim.State.Position, BodyRot)) && Appearance->GetAnimator())
		{
			TestTrue(TEXT("animated rider under RiderRoot"), Rider->GetAttachParent() == RiderRoot);
			Rider->RefreshBoneTransforms();
			SSXAnim::FRiderPalette Pal;
			Appearance->GetAnimator()->ComputePalette(Sim, Pal);
			const int32 Bones[3] = { 0, 9, 18 };
			for (const int32 J : Bones)
			{
				const FVector3f W = Pal.Body[J].Row(3);
				const FVector L = BodyRot.UnrotateVector(FVector(W) - Sim.State.Position);
				const FVector Expect(L.Y, L.X, L.Z);   // body (right, forward, up) -> actor (forward, right, up)
				const FName Name(*Appearance->GetAnimator()->GetSkeleton()->Bones[J].Name);
				const FVector Got = Rider->GetBoneLocationByName(Name, EBoneSpaces::ComponentSpace);
				TestTrue(*FString::Printf(TEXT("posed %s at the palette (%.3f cm off)"), *Name.ToString(), (Got - Expect).Size()), (Got - Expect).Size() < 0.1);
				AddInfo(FString::Printf(TEXT("posed %s: %.4f cm from the palette"), *Name.ToString(), (Got - Expect).Size()));
			}
		}
		// 0x201138
		UStaticMesh* Regular = SSXCharacters::LoadBoardMesh(FirstId, Pawn->BoardClass);
		UStaticMesh* Goofy = SSXCharacters::LoadBoardMesh(FirstId, Pawn->BoardClass, true);
		if (UStaticMeshComponent* BoardComp = Appearance->GetBoardMesh(); BoardComp && Goofy)
		{
			TestTrue(TEXT("regular profile: regular board model"), BoardComp->GetStaticMesh().Get() == Regular && !Appearance->IsBoardMeshGoofy());
			Sim.Profile.Stance = 1;
			Appearance->ApplyAnimatedPose(Sim, Sim.State.Position, BodyRot);
			TestTrue(TEXT("goofy profile: <class>Goofy board model"), BoardComp->GetStaticMesh().Get() == Goofy && Appearance->IsBoardMeshGoofy());
			Sim.State.bStanceGoofy = !Sim.State.bStanceGoofy;
			Appearance->ApplyAnimatedPose(Sim, Sim.State.Position, BodyRot);
			TestTrue(TEXT("goofy profile riding switch: still <class>Goofy"), BoardComp->GetStaticMesh().Get() == Goofy);
			Sim.Profile.Stance = 0;
			Appearance->ApplyAnimatedPose(Sim, Sim.State.Position, BodyRot);
			TestTrue(TEXT("regular profile riding switch: regular model"), BoardComp->GetStaticMesh().Get() == Regular && !Appearance->IsBoardMeshGoofy());
			Sim.State.bStanceGoofy = !Sim.State.bStanceGoofy;
		}
	}

	const FString SecondId = SSXCharacters::IdForIndex(Imported.Last());
	Pawn->CharacterId = SecondId;
	Appearance->Apply(RiderRoot, BodyRoot, Pawn->CharacterId, Pawn->Character, Pawn->BoardClass);
	TestEqual(TEXT("CharacterId selects"), Appearance->GetShownId(), SecondId);
	CVar.Set(FirstId);
	Appearance->Apply(RiderRoot, BodyRoot, Pawn->CharacterId, Pawn->Character, Pawn->BoardClass);
	TestEqual(TEXT("ssx.Character selects"), Appearance->GetShownId(), FirstId);
	CVar.Set(TEXT(""));

	Pawn->CharacterId = TEXT("");
	Pawn->Character = 99;
	TestFalse(TEXT("unknown rider -> fallback"), Appearance->Apply(RiderRoot, BodyRoot, Pawn->CharacterId, Pawn->Character, Pawn->BoardClass));
	TestTrue(TEXT("placeholder visible again"), PlaceholderVisible() > 0);
	if (Rider)
	{
		TestFalse(TEXT("rider hidden"), Rider->IsVisible());
	}
	return true;
}

#if WITH_EDITOR   // the board mesh description is editor-only data
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCharacterGoofyBoardTest, "SSX.Characters.GoofyBoardModel", CharacterTestFlags)

bool FSSXCharacterGoofyBoardTest::RunTest(const FString& Parameters)
{
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	if (!Lib)
	{
		SSXTest::Skip(*this, TEXT("no exported animation library: skipped"));
		return true;
	}
	auto Verts = [](UStaticMesh* Mesh, TArray<FVector>& Out)
	{
		const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!MD) { return false; }
		FStaticMeshConstAttributes Attr(*MD);
		TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		for (const FVertexID Id : MD->Vertices().GetElementIDs()) { Out.Add(FVector(Pos[Id])); }
		return Out.Num() > 0;
	};
	auto MaxNearest = [](const TArray<FVector>& A, const TArray<FVector>& B)
	{
		double Max = 0.0;
		for (const FVector& P : A)
		{
			double Best = 1e18;
			for (const FVector& Q : B) { Best = FMath::Min(Best, FVector::DistSquared(P, Q)); }
			Max = FMath::Max(Max, FMath::Sqrt(Best));
		}
		return Max;
	};
	// a board matrix with a yaw, a tilt and a translation (the result must not depend on it)
	SSXAnim::FRowMat Board = SSXAnim::FRowMat::Identity();
	{
		const FMatrix R = FRotationMatrix(FRotator(10.0, 35.0, -5.0));
		for (int32 I = 0; I < 3; ++I) { Board.SetRow(I, FVector3f(R.GetUnitAxis(EAxis::Type(I + 1))), 0.f); }
		Board.SetRow(3, FVector3f(120.f, -40.f, 15.f), 1.f);
	}
	int32 Checked = 0;
	for (const TCHAR* Id : { TEXT("brodi"), TEXT("eddie") })
	{
		for (int32 Class = 0; Class < 3; ++Class)
		{
			TArray<FVector> Reg, Goof;
			if (!Verts(SSXCharacters::LoadBoardMesh(Id, Class), Reg) || !Verts(SSXCharacters::LoadBoardMesh(Id, Class, true), Goof))
			{
				continue;
			}
			bool bGoofyMesh = false;
			TestTrue(TEXT("LoadDrawnBoardMesh(goofy) = the Goofy mesh"),
				USSXCharacterAppearance::LoadDrawnBoardMesh(Id, Class, true, bGoofyMesh) == SSXCharacters::LoadBoardMesh(Id, Class, true) && bGoofyMesh);
			FMatrix GoofyToGame, FallbackToGame;
			if (!USSXCharacterAppearance::BoardMeshToGame(*Lib, Class, Board, 0.9, GoofyToGame, true, true)
				|| !USSXCharacterAppearance::BoardMeshToGame(*Lib, Class, Board, 0.9, FallbackToGame, true, false))
			{
				AddError(TEXT("board skeleton / model missing"));
				return false;
			}
			TArray<FVector> WG, WF;
			for (const FVector& V : Goof) { WG.Add(GoofyToGame.TransformPosition(V)); }
			for (const FVector& V : Reg) { WF.Add(FallbackToGame.TransformPosition(V)); }
			const double D = FMath::Max(MaxNearest(WG, WF), MaxNearest(WF, WG));
			const double Tol = Class == 1 ? 2.0 : 0.05;   // x 0.9 scale: Fr 1.7 cm in the model
			TestTrue(*FString::Printf(TEXT("%s %s: Goofy mesh drawn = regular turned pi (%.3f cm)"), Id, SSXCharacters::BoardSuffix(Class), D), D < Tol);
			const double Unturned = MaxNearest(Goof, Reg);
			AddInfo(FString::Printf(TEXT("%s %s: Goofy vs fallback %.3f cm, Goofy vs regular unturned %.2f cm"), Id, SSXCharacters::BoardSuffix(Class), D, Unturned));
			++Checked;
		}
	}
	if (Checked == 0)
	{
		SSXTest::Skip(*this, TEXT("board meshes not imported: skipped"));
	}
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCharacterMaterialsTest, "SSX.Characters.Materials", CharacterTestFlags)

bool FSSXCharacterMaterialsTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : { TEXT("/Game/World/M_SSXRiderLit.M_SSXRiderLit"), TEXT("/Game/World/M_SSXRiderLit_Masked.M_SSXRiderLit_Masked"),
			TEXT("/Game/Particles/M_SSXGhost.M_SSXGhost") })
	{
		const UMaterial* M = LoadObject<UMaterial>(nullptr, Path);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), Path), M))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s used with skeletal meshes"), Path), M->GetUsageByFlag(MATUSAGE_SkeletalMesh));
		TestTrue(*FString::Printf(TEXT("%s used with morph targets"), Path), M->GetUsageByFlag(MATUSAGE_MorphTargets));
	}

	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Meshes;
		Registry.GetAssetsByPath(TEXT("/Game/Characters"), Meshes, true);
		for (const FAssetData& Data : Meshes)
		{
			if (Data.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName())
			{
				continue;
			}
			const UStaticMesh* SM = Cast<UStaticMesh>(Data.GetAsset());
#if WITH_EDITOR
			TestTrue(*FString::Printf(TEXT("%s is not a Nanite mesh"), *Data.GetObjectPathString()), SM && !SM->IsNaniteEnabled());
#endif
		}
	}

	{
		SSXAnim::FRiderPartsPose Pose;
		Pose.FaceWeights.Init(0.0, 23);
		TArray<double> W;
		for (const bool bSampled : { false, true })
		{
			Pose.bFaceSampled = bSampled;
			SSXCharacters::FaceMorphWeights(Pose, 1.1, W);
			double MaxW = 0.0;
			for (const double X : W) { MaxW = FMath::Max(MaxW, FMath::Abs(X)); }
			TestEqual(*FString::Printf(TEXT("rest face (sampled %d): 23 weights"), bSampled), W.Num(), 23);
			TestEqual(*FString::Printf(TEXT("rest face (sampled %d): all weights 0"), bSampled), MaxW, 0.0);
		}
	}

	int32 Loaded = 0;
	for (const FString& Id : SSXCharacters::AllIds())
	{
		USkeletalMesh* Mesh = SSXCharacters::LoadRiderMesh(Id);
		if (!Mesh)
		{
			continue;
		}
		++Loaded;
		const TArray<FSkeletalMaterial>& Mats = Mesh->GetMaterials();
#if WITH_EDITORONLY_DATA
		const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
		if (!Model || Model->LODModels.Num() == 0)
		{
			AddError(FString::Printf(TEXT("%s: no LOD model"), *Id));
			continue;
		}
		const FSkeletalMeshLODModel& LOD = Model->LODModels[0];
		for (int32 S = 0; S < LOD.Sections.Num(); ++S)
		{
			const FSkelMeshSection& Sec = LOD.Sections[S];
			UMaterialInterface* M = Mats.IsValidIndex(Sec.MaterialIndex) ? Mats[Sec.MaterialIndex].MaterialInterface.Get() : nullptr;
			if (!TestNotNull(*FString::Printf(TEXT("%s section %d has a material"), *Id, S), M))
			{
				continue;
			}
			UTexture* Base = nullptr;
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Guids;
			M->GetAllTextureParameterInfo(Infos, Guids);
			for (const FMaterialParameterInfo& P : Infos)
			{
				if (P.Name.ToString().Contains(TEXT("BaseColor")) || P.Name.ToString().Contains(TEXT("Diffuse")))
				{
					M->GetTextureParameterValue(P, Base);
					break;
				}
			}
			const FString MatName = M->GetName();
			const FString Slot = MatName.Mid(MatName.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd));
			TestTrue(*FString::Printf(TEXT("%s section %d (%s) samples its rider texture (%s)"), *Id, S, *MatName, Base ? *Base->GetName() : TEXT("none")),
				Base && !Base->GetName().StartsWith(TEXT("T_")) && Base->GetName().EndsWith(Slot));
			int32 BadNormals = 0;
			for (const FSoftSkinVertex& V : Sec.SoftVertices)
			{
				const float L = FVector3f(V.TangentZ).Size();
				BadNormals += !(L > 0.9f && L < 1.1f);
			}
			TestEqual(*FString::Printf(TEXT("%s section %d: unit normals"), *Id, S), BadNormals, 0);
		}

		double ConvertedMax = -1.0;
		int32 ConvertedKeys = -1;
		FString JsonText;
		const FString JsonPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../../converted/characters") / Id / (Id + TEXT(".json")));
		TSharedPtr<FJsonObject> Info;
		if (FFileHelper::LoadFileToString(JsonText, *JsonPath) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Info) && Info)
		{
			Info->TryGetNumberField(TEXT("face_morph_max_cm"), ConvertedMax);
			Info->TryGetNumberField(TEXT("face_morph_keys"), ConvertedKeys);
		}
		if (ConvertedKeys <= 0)
		{
			continue;
		}
		const int32 NumVerts = LOD.NumVertices;
		double MaxDelta = 0.0;
		int32 Found = 0, BadDeltas = 0;
		for (int32 K = 0; K < ConvertedKeys; ++K)
		{
			const UMorphTarget* T = Mesh->FindMorphTarget(SSXCharacters::FaceMorphName(K));
			if (!T)
			{
				continue;
			}
			++Found;
			for (const FMorphTargetDelta& D : T->GetMorphTargetDeltas(0))
			{
				const double P = D.PositionDelta.GetAbsMax();
				BadDeltas += !FMath::IsFinite(P) || int32(D.SourceIdx) >= NumVerts;
				MaxDelta = FMath::Max(MaxDelta, P);
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: face morph targets ssx_face_00..%02d"), *Id, ConvertedKeys - 1), Found, ConvertedKeys);
		TestEqual(*FString::Printf(TEXT("%s: morph deltas finite, on mesh vertices"), *Id), BadDeltas, 0);
		TestTrue(*FString::Printf(TEXT("%s: largest morph delta %.3f cm in (0, 10)"), *Id, MaxDelta), MaxDelta > 0.0 && MaxDelta < 10.0);
		TestTrue(*FString::Printf(TEXT("%s: largest morph delta %.4f cm = converter's %.4f cm"), *Id, MaxDelta, ConvertedMax),
			FMath::Abs(MaxDelta - ConvertedMax) < 0.01);
		AddInfo(FString::Printf(TEXT("%s: %d sections, %d face keys, largest delta %.3f cm"), *Id, LOD.Sections.Num(), Found, MaxDelta));
#endif
	}
	if (Loaded == 0)
	{
		AddError(TEXT("no imported riders under /Game/Characters (run tools/convert_characters.py, then Scripts/import_characters.py)"));
	}
	return true;
}

#endif
