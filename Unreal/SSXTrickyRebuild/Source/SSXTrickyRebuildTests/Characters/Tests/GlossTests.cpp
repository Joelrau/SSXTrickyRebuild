// FUN_00205670, FUN_0025EDF0
#include "Characters/SSXCharacterLibrary.h"
#include "Characters/SSXRiderChrome.h"
#include "Characters/SSXRiderGloss.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SSXFrontEndData.h"
#include "World/SSXWorldLighting.h"
#include "World/SSXWorldPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace
{
	constexpr EAutomationTestFlags GlossTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FString Squash(const FString& S)
	{
		FString O;
		for (TCHAR C : S) { if (!FChar::IsWhitespace(C)) { O.AppendChar(C); } }
		return O;
	}

	template <class T>
	const T* FindExpr(const UMaterial* M, TFunctionRef<bool(const T&)> Pred)
	{
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			const T* C = Cast<T>(E.Get());
			if (C && Pred(*C)) { return C; }
		}
		return nullptr;
	}

	bool PngSize(const FString& Path, int32& W, int32& H)
	{
		TArray<uint8> B;
		if (!FFileHelper::LoadFileToArray(B, *Path) || B.Num() < 24) { return false; }
		W = (B[16] << 24) | (B[17] << 16) | (B[18] << 8) | B[19];
		H = (B[20] << 24) | (B[21] << 16) | (B[22] << 8) | B[23];
		return true;
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> J;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J);
		}
		return J;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCharacterGlossTest, "SSX.Characters.Gloss", GlossTestFlags)
bool FSSXCharacterGlossTest::RunTest(const FString&)
{
	const FString Root = FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("characters"));
	int32 Riders = 0, Groups = 0, Glossed = 0;
	TMap<FString, TSet<int32>> EnvGlossByRider;
	for (const FString& Id : SSXCharacters::AllIds())
	{
		const TSharedPtr<FJsonObject> J = LoadJson(FPaths::Combine(Root, Id, Id + TEXT(".json")));
		if (!J.IsValid())
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Parts = nullptr;
		if (!J->TryGetObjectField(TEXT("gloss_groups"), Parts))
		{
			AddError(FString::Printf(TEXT("%s.json has no gloss_groups (re-run tools/convert_characters.py)"), *Id));
			continue;
		}
		++Riders;
		TSet<FString> EnvMats;
		const TArray<TSharedPtr<FJsonValue>>* EnvArr = nullptr;
		if (J->TryGetArrayField(TEXT("env_pass_materials"), EnvArr))
		{
			for (const TSharedPtr<FJsonValue>& V : *EnvArr) { EnvMats.Add(V->AsString()); }
		}
		TSet<int32>& Values = EnvGlossByRider.Add(Id);
		for (const auto& P : (*Parts)->Values)
		{
			for (const TSharedPtr<FJsonValue>& RowV : P.Value->AsArray())
			{
				const TArray<TSharedPtr<FJsonValue>>& R = RowV->AsArray();
				const float F0 = float(R[1]->AsNumber());
				const int32 Gloss = int32(R[4]->AsNumber()), Env = int32(R[5]->AsNumber());
				const FString Mat = R[7]->AsString();
				++Groups;
				TestEqual(FString::Printf(TEXT("%s %s material %d: gloss = FUN_00205670 (%.4f)"), *Id, *P.Key, int32(R[0]->AsNumber()), F0),
					Gloss, SSXRiderGloss::GlossOfMpfMaterial(F0, R[2]->AsBool(), R[3]->AsBool()));
				const int32 Shown = Env > 0 ? (Gloss < 0 ? -SSXRiderGloss::Fix(Gloss) : SSXRiderGloss::Fix(Gloss)) : 0;
				TestEqual(FString::Printf(TEXT("%s %s: section %s carries the gloss only with the second kick"), *Id, *P.Key, *Mat),
					SSXRiderGloss::GlossFromName(Mat), Shown);
				if (Env > 0)
				{
					TestTrue(FString::Printf(TEXT("%s: %s is an env-pass material"), *Id, *Mat), EnvMats.Contains(Mat));
					if (Shown != 0) { Values.Add(Shown); ++Glossed; }
				}
			}
		}
		const TSharedPtr<FJsonObject>* Boards = nullptr;
		if (J->TryGetObjectField(TEXT("boards"), Boards))
		{
			for (const auto& B : (*Boards)->Values)
			{
				const TSharedPtr<FJsonObject> O = B.Value->AsObject();
				TestEqual(FString::Printf(TEXT("%s board %s: gloss 63 (board.mpf 0.498 x 127)"), *Id, *B.Key), int32(O->GetNumberField(TEXT("gloss"))), 63);
				TestEqual(FString::Printf(TEXT("%s board %s: material"), *Id, *B.Key), O->GetStringField(TEXT("material")),
					Id + TEXT("_") + SSXRiderGloss::MaterialKey(TEXT("board"), 63));
			}
		}
	}
	if (Riders == 0)
	{
		AddError(TEXT("no converted riders (tools/convert_characters.py)"));
	}
	AddInfo(FString::Printf(TEXT("%d riders, %d mesh groups, %d glossed env groups"), Riders, Groups, Glossed));
	if (const TSet<int32>* Mac = EnvGlossByRider.Find(TEXT("mac")))
	{
		TestTrue(TEXT("mac: suit 23, helm 30, boot 95 / 127 (0.188, 0.239, 0.749, 1.0 x 127)"),
			Mac->Num() == 4 && Mac->Contains(23) && Mac->Contains(30) && Mac->Contains(95) && Mac->Contains(127));
	}
	if (const TSet<int32>* Psy = EnvGlossByRider.Find(TEXT("psymon")))
	{
		TestFalse(TEXT("psymon: the hd_g head group (81) has no second kick, so no gloss pass"), Psy->Contains(81));
	}
	TestEqual(TEXT("name round trip"), SSXRiderGloss::GlossFromName(TEXT("mac_env_s95_boot")), 95);
	TestEqual(TEXT("name round trip (e)"), SSXRiderGloss::GlossFromName(TEXT("x_env_e7_suit")), -7);
	TestEqual(TEXT("no gloss token"), SSXRiderGloss::GlossFromName(TEXT("mac_env_suit")), 0);
	TestEqual(TEXT("slot 4 only -> negative"), SSXRiderGloss::GlossOfMpfMaterial(0.5f, false, true), -63);
	TestEqual(TEXT("no slots -> 0"), SSXRiderGloss::GlossOfMpfMaterial(0.9f, false, false), 0);
	for (const auto& R : EnvGlossByRider)
	{
		USkeletalMesh* Mesh = SSXCharacters::LoadRiderMesh(R.Key);
		if (!Mesh)
		{
			continue;
		}
		TSet<int32> Imported;
		for (const FSkeletalMaterial& M : Mesh->GetMaterials())
		{
			if (const int32 G = SSXRiderGloss::GlossOfMaterial(M.MaterialInterface)) { Imported.Add(G); }
		}
		TestTrue(FString::Printf(TEXT("%s: imported section glosses = converted (re-run Scripts/import_characters.py)"), *R.Key),
			Imported.Num() == R.Value.Num() && Imported.Includes(R.Value));
	}

	const FString SrcDir = FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("textures"), TEXT("TEXTURES"), TEXT("SPECMAP"));
	for (int32 K = 0; K < SSXRiderGloss::NumSpecmaps; ++K)
	{
		for (const bool bEnv : { false, true })
		{
			const FString Path = SSXRiderGloss::SpecmapPath(K, bEnv);
			int32 W = 0, H = 0;
			TestTrue(FString::Printf(TEXT("%s present, 64 x 64"), *SSXRiderGloss::SpecmapName(K, bEnv)), PngSize(Path, W, H) && W == 64 && H == 64);
			TArray<uint8> A, B;
			const FString Src = FPaths::Combine(SrcDir, FString::Printf(TEXT("%03d_%s.png"), 2 * K + int32(bEnv), *SSXRiderGloss::SpecmapName(K, bEnv)));
			TestTrue(FString::Printf(TEXT("%s = SPECMAP.SSH image %d"), *SSXRiderGloss::SpecmapName(K, bEnv), 2 * K + int32(bEnv)),
				FFileHelper::LoadFileToArray(A, *Path) && FFileHelper::LoadFileToArray(B, *Src) && A == B);
		}
	}
	for (const bool bEnv : { false, true })
	{
		TestTrue(FString::Printf(TEXT("fallback %s present"), *SSXRiderGloss::FallbackPath(bEnv)), FPaths::FileExists(SSXRiderGloss::FallbackPath(bEnv)));
	}
	TestEqual(TEXT("game track 3 -> mesa"), SSXRiderGloss::SpecmapName(SSXRiderGloss::SpecmapForGameTrack(3, false), false), FString(TEXT("mesas")));
	TestEqual(TEXT("game track 9 -> fend (switch default)"), SSXRiderGloss::SpecmapForGameTrack(9, false), 11);
	TestEqual(TEXT("game track 10 -> tric"), SSXRiderGloss::SpecmapForGameTrack(10, false), 9);
	TestEqual(TEXT("game track 11 -> alas"), SSXRiderGloss::SpecmapForGameTrack(11, false), 10);
	TestEqual(TEXT("front end -> fend"), SSXRiderGloss::SpecmapForGameTrack(0, true), SSXRiderGloss::FrontEndSpecmap);
	const TPair<const TCHAR*, const TCHAR*> ByName[] = { { TEXT("gari"), TEXT("gari") }, { TEXT("elysium"), TEXT("elys") },
		{ TEXT("merquer"), TEXT("merq") }, { TEXT("untrack"), TEXT("untr") }, { TEXT("megaple"), TEXT("toky") }, { TEXT("trick"), TEXT("tric") },
		{ TEXT("alaska"), TEXT("alas") }, { TEXT("frontend"), TEXT("fend") } };
	for (const auto& P : ByName)
	{
		TestEqual(FString::Printf(TEXT("map %s -> specmap %s"), P.Key, P.Value),
			FString(SSXRiderGloss::SpecmapStem(SSXRiderGloss::SpecmapForTrackId(FSSXTrackPresentation::IdForStem(P.Key)))), FString(P.Value));
	}

	const FString UvHlsl = Squash(SSXRiderGloss::UvHlsl()), ShadeHlsl = Squash(SSXRiderGloss::ShadeHlsl());
	for (const TCHAR* Path : { USSXWorldPresentation::RiderMaterialPath, USSXWorldPresentation::RiderMaskedMaterialPath })
	{
		const UMaterial* M = LoadObject<UMaterial>(nullptr, Path);
		if (!M)
		{
			AddError(FString::Printf(TEXT("%s missing (Scripts/make_world_materials.py)"), Path));
			continue;
		}
		const FString N = M->GetName();
		TestTrue(N + TEXT(": two-sided"), M->IsTwoSided());
		TestTrue(N + TEXT(": unlit"), M->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
		TestTrue(N + TEXT(": skeletal + morph usage"), M->GetUsageByFlag(MATUSAGE_SkeletalMesh) && M->GetUsageByFlag(MATUSAGE_MorphTargets));
		const UMaterialExpressionCustom* Uv = FindExpr<UMaterialExpressionCustom>(M, [](const UMaterialExpressionCustom& C) { return C.Description == TEXT("RiderGlossUV"); });
		const UMaterialExpressionCustom* Shade = FindExpr<UMaterialExpressionCustom>(M, [](const UMaterialExpressionCustom& C) { return C.Description == TEXT("RiderGlossShade"); });
		TestTrue(N + TEXT(": RiderGlossUV = SSXRiderGloss::UvHlsl"), Uv && Squash(Uv->Code) == UvHlsl);
		TestTrue(N + TEXT(": RiderGlossShade = SSXRiderGloss::ShadeHlsl"), Shade && Squash(Shade->Code) == ShadeHlsl);
		TestNotNull(*(N + TEXT(": samples 'GlossTex' (sRGB)")), FindExpr<UMaterialExpressionTextureSampleParameter2D>(M,
			[](const UMaterialExpressionTextureSampleParameter2D& T) { return T.ParameterName == TEXT("GlossTex") && T.SamplerType == SAMPLERTYPE_Color; }));
		TestNotNull(*(N + TEXT(": GlossFix defaults to 0 (no pass)")), FindExpr<UMaterialExpressionScalarParameter>(M,
			[](const UMaterialExpressionScalarParameter& S) { return S.ParameterName == TEXT("GlossFix") && S.DefaultValue == 0.0f; }));
	}

	{
		int32 Cases = 0, MaxDiff = 0;
		for (const int32 Fix : { 0, 8, 23, 63, 95, 127 })
		{
			for (int32 Tb = 0; Tb <= 255; Tb += 15)
			{
				for (int32 Tg = 0; Tg <= 255; Tg += 17)
				{
					for (const int32 Cv : { 16, 64, 100, 128, 180, 255 })
					{
						const int32 Gs = SSXRiderGloss::GsPixel(Tb, Tg, Cv, Fix);
						const double M = SSXRiderGloss::ShadePixel(Tb / 255.0, Tg / 255.0, Cv / 128.0, Fix / 128.0) * 255.0;
						MaxDiff = FMath::Max(MaxDiff, FMath::Abs(FMath::RoundToInt(M) - Gs));
						++Cases;
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("shade = GS (MODULATE, ALPHA mode 5 additive) within the floors: %d cases, max %d"), Cases, MaxDiff), MaxDiff <= 3);
		TestEqual(TEXT("GS: base 100, specmap 200, Cv 128, FIX 63 -> 100 + (200 x 63 >> 7)"), SSXRiderGloss::GsPixel(100, 200, 128, 63), 198);
		TestEqual(TEXT("GS: FIX 0 adds nothing"), SSXRiderGloss::GsPixel(77, 255, 100, 0), (77 * 100) >> 7);
		TestEqual(TEXT("GS: COLCLAMP at 255"), SSXRiderGloss::GsPixel(255, 255, 128, 127), 255);
		TestEqual(TEXT("GS: specmap centre (28) on mac's boot (95), base 120, Cv 128"), SSXRiderGloss::GsPixel(120, 28, 128, 95), 120 + ((28 * 95) >> 7));
		const FVector N(0.3, -0.8, 0.52), R(0.0, 1.0, 0.0), U(0.0, 0.0, 1.0), F(1.0, 0.0, 0.0);
		const FVector2D Chrome = SSXRiderChrome::EnvUv(N, R, U, F), Gl = SSXRiderGloss::Uv(N, R, U, F);
		TestTrue(TEXT("chrome ST (0.52, -0.3) -> gloss UV (0.52, 0) (CLAMP)"),
			Chrome.Equals(FVector2D(0.52, -0.3), 1e-12) && Gl.Equals(FVector2D(0.52, 0.0), 1e-12));
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXGlossTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	USSXWorldPresentation* P = World->GetSubsystem<USSXWorldPresentation>();
	if (!TestNotNull(TEXT("world presentation subsystem"), P))
	{
		return true;
	}
	for (int32 TrackId = 0; TrackId < SSXRiderGloss::NumSpecmaps; ++TrackId)
	{
		P->SetGlossSpecmap(SSXRiderGloss::SpecmapForTrackId(TrackId));
		TestTrue(FString::Printf(TEXT("track %d: loads %s / %s"), TrackId, *SSXRiderGloss::SpecmapName(TrackId, false), *SSXRiderGloss::SpecmapName(TrackId, true)),
			P->GetGlossTexturePath(false) == SSXRiderGloss::SpecmapPath(TrackId, false) && P->GetGlossTexturePath(true) == SSXRiderGloss::SpecmapPath(TrackId, true)
			&& P->GetGlossTexture(false) && P->GetGlossTexture(true));
	}
	P->SetGlossSpecmap(INDEX_NONE);
	TestTrue(TEXT("no record: PARTICLE 'spec' / 'envr' fallbacks"), P->GetGlossTexturePath(false) == SSXRiderGloss::FallbackPath(false)
		&& P->GetGlossTexturePath(true) == SSXRiderGloss::FallbackPath(true));

	USkeletalMesh* Mesh = SSXCharacters::LoadRiderMesh(TEXT("mac"));
	UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, USSXWorldPresentation::RiderMaterialPath);
	UMaterialInterface* Chrome = LoadObject<UMaterialInterface>(nullptr, SSXRiderChrome::MaterialPath);
	if (!Mesh || !Lit || !Chrome)
	{
		AddError(TEXT("mac rider mesh / rider materials not imported"));
		return true;
	}
	for (const int32 TrackId : { 2, FSSXTrackPresentation::IdForStem(TEXT("frontend")) })
	{
		P->SetGlossSpecmap(SSXRiderGloss::SpecmapForTrackId(TrackId));
		int32 WithGloss = 0, Without = 0;
		for (const FSkeletalMaterial& SM : Mesh->GetMaterials())
		{
			UMaterialInterface* Src = SM.MaterialInterface;
			if (!Src) { continue; }
			UMaterialInstanceDynamic* Mid = USSXWorldPresentation::MakeRiderMid(*Src, Lit, World);
			P->ApplyGloss(*Mid, *Src);
			const int32 Gloss = SSXRiderGloss::GlossOfMaterial(Src);
			float Fix = -1.0f;
			UTexture* Tex = nullptr;
			Mid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("GlossFix")), Fix);
			Mid->GetTextureParameterValue(FMaterialParameterInfo(TEXT("GlossTex")), Tex);
			if (Gloss != 0)
			{
				++WithGloss;
				TestTrue(FString::Printf(TEXT("track %d %s: GlossFix = %d / 128, the track's s map"), TrackId, *Src->GetName(), SSXRiderGloss::Fix(Gloss)),
					FMath::IsNearlyEqual(Fix, SSXRiderGloss::Fix(Gloss) / 128.0f) && Tex == P->GetGlossTexture(Gloss < 0));
			}
			else
			{
				++Without;
				TestEqual(FString::Printf(TEXT("track %d %s: no gloss -> GlossFix 0"), TrackId, *Src->GetName()), Fix, 0.0f);
			}
			UMaterialInstanceDynamic* ChromeMid = USSXWorldPresentation::MakeRiderMid(*Src, Chrome, World);
			P->ApplyGloss(*ChromeMid, *Src);
			float ChromeFix = 0.0f;
			TestFalse(FString::Printf(TEXT("%s: chrome instance gets no gloss"), *Src->GetName()),
				ChromeMid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("GlossFix")), ChromeFix) && ChromeFix != 0.0f);
		}
		TestTrue(FString::Printf(TEXT("track %d: mac has glossed (%d) and plain (%d) sections"), TrackId, WithGloss, Without), WithGloss >= 4 && Without > 0);
	}
	return true;
}

#endif
