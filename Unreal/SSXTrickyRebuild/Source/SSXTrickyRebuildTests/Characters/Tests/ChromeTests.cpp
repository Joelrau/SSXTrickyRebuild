#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Characters/SSXRiderChrome.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SnowboarderPawn.h"
#include "UI/SSXFrontEndData.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace
{
	constexpr EAutomationTestFlags ChromeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FString Squash(const FString& S)
	{
		FString O;
		for (TCHAR C : S) { if (!FChar::IsWhitespace(C)) { O.AppendChar(C); } }
		return O;
	}

	const UMaterialExpressionCustom* FindCustom(const UMaterial* M, const TCHAR* Desc)
	{
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E.Get());
			if (C && C->Description == Desc) { return C; }
		}
		return nullptr;
	}

	FVector Mirror(const FVector& G) { return FVector(G.X, -G.Y, G.Z); }

	bool PngSize(const FString& Path, int32& W, int32& H)
	{
		TArray<uint8> B;
		if (!FFileHelper::LoadFileToArray(B, *Path) || B.Num() < 24) { return false; }
		W = (B[16] << 24) | (B[17] << 16) | (B[18] << 8) | B[19];
		H = (B[20] << 24) | (B[21] << 16) | (B[22] << 8) | B[23];
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCharacterChromeTest, "SSX.Characters.Chrome", ChromeTestFlags)
bool FSSXCharacterChromeTest::RunTest(const FString&)
{
	const FString Root = FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("characters"));
	int32 Riders = 0;
	for (const FString& Id : SSXCharacters::AllIds())
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, Id, Id + TEXT(".json"))))
		{
			continue;
		}
		TSharedPtr<FJsonObject> J;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid())
		{
			AddError(FString::Printf(TEXT("%s.json unreadable"), *Id));
			continue;
		}
		const TSharedPtr<FJsonObject>* Parts = nullptr;
		if (!J->TryGetObjectField(TEXT("env_pass_chunks"), Parts))
		{
			AddError(FString::Printf(TEXT("%s.json has no env_pass_chunks (re-run tools/convert_characters.py)"), *Id));
			continue;
		}
		++Riders;
		int32 FaceParts = 0;
		const bool bMorphFace = J->GetNumberField(TEXT("face_morph_keys")) > 0;
		for (const auto& P : (*Parts)->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>& V = P.Value->AsArray();
			const int32 Env = int32(V[0]->AsNumber()), Total = int32(V[1]->AsNumber());
			const FString Key(P.Key);
			FaceParts += Key.StartsWith(TEXT("Face"));
			const bool bFace = bMorphFace && Key.StartsWith(TEXT("Face"));
			TestTrue(FString::Printf(TEXT("%s %s: chunks"), *Id, *Key), Total > 0);
			TestEqual(FString::Printf(TEXT("%s %s: environment pass on %s chunks"), *Id, *Key, bFace ? TEXT("no (morph)") : TEXT("all")),
				Env, bFace ? 0 : Total);
		}
		TestEqual(FString::Printf(TEXT("%s: one face part"), *Id), FaceParts, 1);
		const TArray<TSharedPtr<FJsonValue>>* EnvMats = nullptr;
		TestTrue(FString::Printf(TEXT("%s: env materials named <id>_env_<slot>"), *Id), J->TryGetArrayField(TEXT("env_pass_materials"), EnvMats)
			&& EnvMats->Num() > 0 && SSXRiderChrome::IsEnvPassName((*EnvMats)[0]->AsString()));
		const TSharedPtr<FJsonObject>* Boards = nullptr;
		if (TestTrue(FString::Printf(TEXT("%s: boards"), *Id), J->TryGetObjectField(TEXT("boards"), Boards)))
		{
			for (const auto& B : (*Boards)->Values)
			{
				TestTrue(FString::Printf(TEXT("%s board %s: environment pass"), *Id, *B.Key), B.Value->AsObject()->GetBoolField(TEXT("env_pass")));
			}
		}
	}
	if (Riders == 0)
	{
		AddError(TEXT("no converted riders (tools/convert_characters.py)"));
	}
	{
		int32 W = 0, H = 0;
		const FString Env = SSXRiderChrome::EnvTexturePath();
		TestTrue(TEXT("envr.png 128 x 128"), PngSize(Env, W, H) && W == 128 && H == 128);
		const FString Src = FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("textures"), TEXT("TEXTURES"), TEXT("PARTICLE"),
			FString::Printf(TEXT("%03d_envr.png"), SSXRiderChrome::EnvTextureIndex));
		TArray<uint8> A, B;
		TestTrue(TEXT("envr.png = PARTICLE.SSH image 36 'envr'"), FFileHelper::LoadFileToArray(A, *Env) && FFileHelper::LoadFileToArray(B, *Src) && A == B);
	}

	const FString UvHlsl = Squash(SSXRiderChrome::EnvUvHlsl()), ShadeHlsl = Squash(SSXRiderChrome::ShadeHlsl());
	const UMaterial* Plain = LoadObject<UMaterial>(nullptr, TEXT("/Game/World/M_SSXRiderLit.M_SSXRiderLit"));
	const UMaterialExpressionCustom* PlainLight = Plain ? FindCustom(Plain, TEXT("RiderVertexLight")) : nullptr;
	for (const TCHAR* Path : { SSXRiderChrome::MaterialPath, SSXRiderChrome::MaskedMaterialPath })
	{
		const UMaterial* M = LoadObject<UMaterial>(nullptr, Path);
		if (!M)
		{
			AddError(FString::Printf(TEXT("%s missing (Scripts/make_world_materials.py)"), Path));
			continue;
		}
		const FString N = M->GetName();
		TestTrue(N + TEXT(": two-sided (VU 0x318080 never culls)"), M->IsTwoSided());
		TestTrue(N + TEXT(": unlit"), M->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
		TestTrue(N + TEXT(": used with skeletal meshes"), M->GetUsageByFlag(MATUSAGE_SkeletalMesh));
		TestTrue(N + TEXT(": used with morph targets"), M->GetUsageByFlag(MATUSAGE_MorphTargets));
		TestTrue(N + TEXT(": masked (the rider clip rule, Characters/SSXRiderDepth.h)"), M->GetBlendMode() == BLEND_Masked);
		{
			const UMaterialExpression* Mask = M->GetEditorOnlyData()->OpacityMask.Expression;
			const UMaterialExpressionCustom* MaskNode = Cast<UMaterialExpressionCustom>(Mask);
			TestEqual(N + TEXT(": texture alpha in the mask only for the masked variant"), !(MaskNode && MaskNode->Description == TEXT("RiderDepthKeep")),
				FString(Path) == SSXRiderChrome::MaskedMaterialPath);
		}
		const UMaterialExpressionCustom* Uv = FindCustom(M, TEXT("RiderChromeUV"));
		const UMaterialExpressionCustom* Shade = FindCustom(M, TEXT("RiderChromeShade"));
		const UMaterialExpressionCustom* Light = FindCustom(M, TEXT("RiderVertexLight"));
		TestTrue(N + TEXT(": RiderChromeUV = SSXRiderChrome::EnvUvHlsl"), Uv && Squash(Uv->Code) == UvHlsl);
		TestTrue(N + TEXT(": RiderChromeShade = SSXRiderChrome::ShadeHlsl"), Shade && Squash(Shade->Code) == ShadeHlsl);
		TestTrue(N + TEXT(": same vertex lighting as M_SSXRiderLit"), Light && PlainLight && Squash(Light->Code) == Squash(PlainLight->Code));
		bool bEnv = false;
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			const UMaterialExpressionTextureSampleParameter2D* T = Cast<UMaterialExpressionTextureSampleParameter2D>(E.Get());
			bEnv |= T && T->ParameterName == TEXT("EnvTex") && T->SamplerType == SAMPLERTYPE_Color;
		}
		TestTrue(N + TEXT(": samples 'EnvTex' (sRGB, re-encoded like BaseTex)"), bEnv);
	}

	{
		FRandomStream Rng(0x0C40);
		double MaxErr = 0.0;
		for (int32 Case = 0; Case < 200; ++Case)
		{
			const FVector Fwd = Rng.GetUnitVector();
			const FVector Up = Rng.GetUnitVector();
			const FVector UpO = (Up - Fwd * FVector::DotProduct(Up, Fwd)).GetSafeNormal();
			const FVector Right = FVector::CrossProduct(Fwd, UpO);
			FMatrix View = FMatrix::Identity;   // world -> camera, row vectors: row i = (right_i, up_i, fwd_i)
			for (int32 I = 0; I < 3; ++I)
			{
				View.M[I][0] = Right[I]; View.M[I][1] = UpO[I]; View.M[I][2] = Fwd[I];
			}
			const FMatrix Bone = FRotationMatrix(FRotator(Rng.FRandRange(-180, 180), Rng.FRandRange(-180, 180), Rng.FRandRange(-180, 180)));
			const FVector LocalN = Rng.GetUnitVector();
			const FVector2D Vu = SSXRiderChrome::EnvStVu(View, Bone, LocalN);
			const FVector Nw = Bone.TransformVector(LocalN);
			const FVector2D Mat = SSXRiderChrome::EnvUv(Mirror(Nw), Mirror(Right), Mirror(UpO), Mirror(Fwd));
			MaxErr = FMath::Max(MaxErr, (Vu - Mat).GetAbsMax());
		}
		TestTrue(FString::Printf(TEXT("material env UV = VU ST over 200 cameras / bones / normals (max err %.2e)"), MaxErr), MaxErr < 1e-9);
		FMatrix View = FMatrix::Identity;
		View.M[0][0] = 1; View.M[0][1] = 0; View.M[0][2] = 0;
		View.M[1][0] = 0; View.M[1][1] = 0; View.M[1][2] = 1;
		View.M[2][0] = 0; View.M[2][1] = 1; View.M[2][2] = 0;
		const FVector2D S = SSXRiderChrome::EnvStVu(View, FMatrix::Identity, FVector(0.6, 0.0, 0.8));
		TestTrue(TEXT("sample ST (0.6, 0.8) for nw (0.6, 0, 0.8), camera +Y"), S.Equals(FVector2D(0.6, 0.8), 1e-12));
	}
	{
		int32 Cases = 0, MaxDiff = 0;
		for (int32 Tb = 0; Tb <= 255; Tb += 15)
		{
			for (int32 Te = 0; Te <= 255; Te += 17)
			{
				for (const int32 Cv : { 16, 64, 100, 128, 180, 255 })
				{
					const int32 Gs = SSXRiderChrome::GsPixel(Tb, Te, Cv);
					const double M = SSXRiderChrome::ShadePixel(Tb / 255.0, Te / 255.0, Cv / 128.0) * 255.0;
					MaxDiff = FMath::Max(MaxDiff, FMath::Abs(FMath::RoundToInt(M) - Gs));
					++Cases;
				}
			}
		}
		TestTrue(FString::Printf(TEXT("shade = GS (MODULATE, ALPHA mode 9 FIX 33) within the floors: %d cases, max %d"), Cases, MaxDiff), MaxDiff <= 2);
		TestEqual(TEXT("GS: white base, black env, Cv 128 -> 255 - (255 x 33 >> 7)"), SSXRiderChrome::GsPixel(255, 0, 128), 255 - 66);
		TestEqual(TEXT("FIX 0x21"), SSXRiderChrome::EnvFix, 0x21);
	}

	TestEqual(TEXT("Chrome uses outfit 5's files"), SSXCharacters::OutfitTextureNumber(SSXRiderChrome::ChromeOutfit), 6);
	FString Id;
	for (const FString& C : SSXCharacters::AllIds())
	{
		if (SSXCharacters::LoadRiderMesh(C)) { Id = C; break; }
	}
	if (Id.IsEmpty())
	{
		AddError(TEXT("no imported riders under /Game/Characters"));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXChromeTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	ASnowboarderPawn* Pawn = World->SpawnActor<ASnowboarderPawn>();
	USSXCharacterAppearance* App = Pawn ? Pawn->GetAppearance() : nullptr;
	if (!TestNotNull(TEXT("pawn appearance"), App))
	{
		return true;
	}
	USceneComponent* RootC = Pawn->GetRootComponent();
	for (const int32 Outfit : { SSXRiderChrome::ChromeOutfit, 0 })
	{
		TestTrue(FString::Printf(TEXT("%s shown (outfit %d)"), *Id, Outfit), App->Apply(RootC, RootC, Id, INDEX_NONE, 2, 0, Outfit));
		int32 Env = 0, Chrome = 0, Other = 0, OtherChrome = 0;
		for (UMeshComponent* Mesh : { static_cast<UMeshComponent*>(App->GetRiderMesh()), static_cast<UMeshComponent*>(App->GetBoardMesh()) })
		{
			for (int32 S = 0; Mesh && S < Mesh->GetNumMaterials(); ++S)
			{
				UMaterialInterface* Mat = Mesh->GetMaterial(S);
				const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mat);
				const bool bChrome = Mid && Mid->Parent && (Mid->Parent->GetPathName() == FSoftObjectPath(SSXRiderChrome::MaskedMaterialPath).ToString()
					|| Mid->Parent->GetPathName() == FSoftObjectPath(SSXRiderChrome::MaterialPath).ToString());
				UTexture* EnvTex = nullptr;
				if (bChrome) { Mid->GetTextureParameterValue(FMaterialParameterInfo(TEXT("EnvTex")), EnvTex); }
				const UMaterialInterface* Asset = nullptr;
				if (const USkinnedMeshComponent* Sk = Cast<USkinnedMeshComponent>(Mesh))
				{
					const TArray<FSkeletalMaterial>& Mats = Sk->GetSkinnedAsset()->GetMaterials();
					Asset = Mats.IsValidIndex(S) ? Mats[S].MaterialInterface.Get() : nullptr;
				}
				else if (const UStaticMeshComponent* St = Cast<UStaticMeshComponent>(Mesh))
				{
					Asset = St->GetStaticMesh() ? St->GetStaticMesh()->GetMaterial(S) : nullptr;
				}
				if (bChrome && Asset)
				{
					TestEqual(FString::Printf(TEXT("%s outfit %d section %d: chrome variant follows the imported material"), *Id, Outfit, S),
						Mid->Parent->GetPathName(), FSoftObjectPath(Asset->GetBlendMode() == BLEND_Masked
							? SSXRiderChrome::MaskedMaterialPath : SSXRiderChrome::MaterialPath).ToString());
				}
				if (SSXRiderChrome::IsEnvPassMaterial(Asset))
				{
					++Env;
					Chrome += bChrome && EnvTex != nullptr;
				}
				else
				{
					++Other;
					OtherChrome += bChrome;
				}
			}
		}
		const bool bChromeOutfit = Outfit == SSXRiderChrome::ChromeOutfit;
		TestTrue(FString::Printf(TEXT("%s outfit %d: env sections"), *Id, Outfit), Env > 0);
		TestEqual(FString::Printf(TEXT("%s outfit %d: env sections with the chrome material + 'envr'"), *Id, Outfit), Chrome, bChromeOutfit ? Env : 0);
		TestEqual(FString::Printf(TEXT("%s outfit %d: face (no second kick) not chrome"), *Id, Outfit), OtherChrome, 0);
		TestTrue(FString::Printf(TEXT("%s: the face section stays on the plain path"), *Id), Other > 0);
	}
	return true;
}

#endif
