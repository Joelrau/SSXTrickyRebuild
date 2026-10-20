#include "Components/StaticMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"
#include "World/SSXRemasterWorld.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "TestSkip.h"
#include "World/SSXRenderLook.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags RenderLookFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FPinnedFile { const TCHAR* Path; const TCHAR* Sha1; };

	// Every committed Classic asset, byte for byte. Remastered assets live elsewhere; a regenerated Classic asset
	// fails here (revert it, or update the pin when the Classic change is intended).
	const FPinnedFile ClassicAssets[] = {
		{ TEXT("Particles/M_SSXGhost.uasset"), TEXT("042B77992C12E8B9971242376ACD9429EAB76530") },
		{ TEXT("Particles/M_SSXLensFan.uasset"), TEXT("4ED9AF757BF81DC13720A2BC6D0695A27815F54A") },
		{ TEXT("Particles/M_SSXLensSprite.uasset"), TEXT("507ECDA0BA9201C48560C534AF9DFDA27BF9CA5C") },
		{ TEXT("Particles/M_SSXParticleAdd.uasset"), TEXT("E54E65BA88BCAE513629244078FFB43D40C674B5") },
		{ TEXT("Particles/M_SSXParticleAlpha.uasset"), TEXT("CA67DC6B5D9FB36603B9F4D9E6E2DAFD3EACF7C2") },
		{ TEXT("Particles/M_SSXParticleBrighten.uasset"), TEXT("5D900528CC505205131D5CECFB6DD9A8E448488C") },
		{ TEXT("Particles/M_SSXParticleDarken.uasset"), TEXT("BC09DFEAB364AE57A615C6A3BE2FE8043DF0F708") },
		{ TEXT("Particles/M_SSXShadowProject.uasset"), TEXT("936D66DFD94123F82785F01D3946CAF762158BEA") },
		{ TEXT("Particles/M_SSXTrailBrighten.uasset"), TEXT("C4C7EC5FEF6D41EF56834841A32ECB4E8D1294F5") },
		{ TEXT("Particles/M_SSXTrailDarken.uasset"), TEXT("18C0E0E639DB825632E1FA0F49965F4B8A1B831C") },
		{ TEXT("Rider/M_RiderColor.uasset"), TEXT("43177DF749677EF12D905B5FFA30642EE2203F07") },
		{ TEXT("World/MPC_SSXWorld.uasset"), TEXT("839D20017764BB5F9310D806B0D8260770152CFD") },
		{ TEXT("World/M_SSXClearColour.uasset"), TEXT("DD7BF4854C3613A5C2D89E84802AE0DF42874613") },
		{ TEXT("World/M_SSXLine.uasset"), TEXT("A07E89A084A40A50864014027E5FFC1F5AC6D327") },
		{ TEXT("World/M_SSXProp.uasset"), TEXT("2D65513DF77E5770C8331DFF19BC0EB2387E2DFA") },
		{ TEXT("World/M_SSXPropCrowd.uasset"), TEXT("54C763CDC96CB08130867833A7F77C527AF18667") },
		{ TEXT("World/M_SSXPropOpaque.uasset"), TEXT("5027F4A80EE945A4E9005F3B260E4656B6235A48") },
		{ TEXT("World/M_SSXPropOpaque_TwoSided.uasset"), TEXT("60AB4AFE270F24E044B5701734AF81ADEF64BAEF") },
		{ TEXT("World/M_SSXProp_Masked.uasset"), TEXT("DFE10975880A2006A86DD56E2A3970CB749173A8") },
		{ TEXT("World/M_SSXProp_Masked_TwoSided.uasset"), TEXT("9433B051F95C2FC55C448E4C02E458EEE47B4DB4") },
		{ TEXT("World/M_SSXProp_Translucent.uasset"), TEXT("83C258B5F08AE455D1DD4ACDF8CEF9D09D8E1604") },
		{ TEXT("World/M_SSXProp_TranslucentCore.uasset"), TEXT("4C5146D307918820C334152A1E0D9E898EAAF743") },
		{ TEXT("World/M_SSXProp_TranslucentCore_TwoSided.uasset"), TEXT("CB7EB6240BA96E134E29531F7636A99886D03522") },
		{ TEXT("World/M_SSXProp_Translucent_TwoSided.uasset"), TEXT("75D9C48974506FDE67C3AD2B338A4CBF297D8DC8") },
		{ TEXT("World/M_SSXProp_TwoSided.uasset"), TEXT("437633F41AE90A803E2F2EF805475804F4BF4483") },
		{ TEXT("World/M_SSXRiderLit.uasset"), TEXT("239EC9401CA211EEBF04E1D354D0E2CCBBA8EA38") },
		{ TEXT("World/M_SSXRiderLitChrome.uasset"), TEXT("3B4931702470327C1AF2A60E4F7AFC87F0C0695F") },
		{ TEXT("World/M_SSXRiderLitChrome_Masked.uasset"), TEXT("F9395F0AAB6F9351DD7AF687EFAF78CE1AB8D812") },
		{ TEXT("World/M_SSXRiderLit_Masked.uasset"), TEXT("8A004FD461218DB47700082D87D92CE4E68F3F8F") },
		{ TEXT("World/M_SSXSky.uasset"), TEXT("D12E8DA15A7732063E7E2993073D81AF78687206") },
		{ TEXT("World/M_SSXTerrain.uasset"), TEXT("77755F1CF6683AD127260A3A2C767F7A66939B6E") },
		{ TEXT("World/M_SSXTerrainOpaque.uasset"), TEXT("DB05A140776765131E3E2D5261E172F75CFDB1A9") },
		{ TEXT("World/M_SSXTerrain_Masked.uasset"), TEXT("5B8E059A5B35BCCD2013ADEBD468C37A4BF5F07E") },
		{ TEXT("World/PP_SSXAuthoredColour.uasset"), TEXT("53B32EB3C0807FD4C8270E815D078792D664BE1A") },
		{ TEXT("World/T_SSXLightmapNeutral.uasset"), TEXT("B469B5C9044C4686E96B2F9CF226A11C4BA7EB77") },
	};

	// Every entry of [/Script/Engine.RendererSettings] in DefaultEngine.ini, in file order, and the live cvar value.
	struct FPinnedCvar { const TCHAR* Name; const TCHAR* Ini; int32 Value; };
	const FPinnedCvar ClassicRenderer[] = {
		{ TEXT("r.AllowStaticLighting"), TEXT("False"), 0 },
		{ TEXT("r.Shadow.Virtual.Enable"), TEXT("1"), 1 },
		{ TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("0"), 0 },
		{ TEXT("r.ReflectionMethod"), TEXT("0"), 0 },
		{ TEXT("r.DefaultFeature.MotionBlur"), TEXT("False"), 0 },
		{ TEXT("r.DefaultFeature.AutoExposure"), TEXT("False"), 0 },
		{ TEXT("r.DefaultFeature.Bloom"), TEXT("False"), 0 },
		{ TEXT("r.DefaultFeature.LensFlare"), TEXT("False"), 0 },
		{ TEXT("r.TranslucentSortPolicy"), TEXT("1"), 1 },
		{ TEXT("r.GenerateMeshDistanceFields"), TEXT("True"), 1 },
		{ TEXT("r.SkinCache.CompileShaders"), TEXT("1"), 1 },
		{ TEXT("r.RayTracing"), TEXT("True"), 1 },
	};

	TArray<FString> IniSection(const FString& File, const FString& Section)
	{
		TArray<FString> Lines, Out;
		FFileHelper::LoadFileToStringArray(Lines, *File);
		bool bIn = false;
		for (FString L : Lines)
		{
			L.TrimStartAndEndInline();
			if (L.StartsWith(TEXT("["))) { bIn = L == Section; continue; }
			if (bIn && !L.IsEmpty() && !L.StartsWith(TEXT(";"))) { Out.Add(L); }
		}
		return Out;
	}

	struct FLookRestore
	{
		ESSXRenderLook Prev = SSXRenderLook::Get();
		~FLookRestore() { SSXRenderLook::Set(Prev); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderClassicPinnedTest, "SSX.Render.ClassicPinned", RenderLookFlags)
bool FSSXRenderClassicPinnedTest::RunTest(const FString&)
{
	TestEqual(TEXT("default look is Classic"), int32(SSXRenderLook::Default), int32(ESSXRenderLook::Classic));

	for (const FPinnedFile& F : ClassicAssets)
	{
		TArray<uint8> Bytes;
		const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), F.Path);
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			AddError(FString::Printf(TEXT("%s missing"), F.Path));
			continue;
		}
		FSHAHash Hash;
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);
		TestEqual(FString::Printf(TEXT("%s unchanged"), F.Path), Hash.ToString(), FString(F.Sha1));
	}

	const TArray<FString> Ini = IniSection(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini")),
		TEXT("[/Script/Engine.RendererSettings]"));
	TestEqual(TEXT("DefaultEngine.ini RendererSettings entry count"), Ini.Num(), int32(UE_ARRAY_COUNT(ClassicRenderer)));
	FLookRestore Restore;
	SSXRenderLook::Set(ESSXRenderLook::Remastered);
	SSXRenderLook::Set(ESSXRenderLook::Classic);
	for (int32 I = 0; I < UE_ARRAY_COUNT(ClassicRenderer); ++I)
	{
		const FPinnedCvar& C = ClassicRenderer[I];
		const FString Want = FString::Printf(TEXT("%s=%s"), C.Name, C.Ini);
		TestEqual(FString::Printf(TEXT("DefaultEngine.ini RendererSettings entry %d"), I), Ini.IsValidIndex(I) ? Ini[I] : FString(), Want);
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(C.Name);
		if (!Var) { AddError(FString::Printf(TEXT("%s not registered"), C.Name)); continue; }
		TestEqual(FString::Printf(TEXT("Classic %s"), C.Name), Var->GetInt(), C.Value);
	}

	int32 Maps = 0;
	for (const TCHAR* Map : { TEXT("FrontEnd"), TEXT("Snow") })
	{
		if (!FPaths::FileExists(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Maps"), FString(Map) + TEXT(".umap")))) { continue; }
		const UWorld* World = LoadObject<UWorld>(nullptr, *FString::Printf(TEXT("/Game/Maps/%s.%s"), Map, Map));
		if (!World || !World->PersistentLevel) { AddError(FString::Printf(TEXT("Maps/%s does not load"), Map)); continue; }
		++Maps;
		int32 Volumes = 0;
		for (const AActor* A : World->PersistentLevel->Actors)
		{
			const APostProcessVolume* V = Cast<APostProcessVolume>(A);
			if (!V) { continue; }
			++Volumes;
			const FPostProcessSettings& S = V->Settings;
			const FString P = FString::Printf(TEXT("%s %s: "), Map, *V->GetName());
			TestTrue(P + TEXT("unbound"), V->bUnbound);
			TestTrue(P + TEXT("manual exposure"), S.bOverride_AutoExposureMethod && S.AutoExposureMethod == AEM_Manual);
			TestTrue(P + TEXT("exposure bias 0"), S.bOverride_AutoExposureBias && S.AutoExposureBias == 0.f);
			TestTrue(P + TEXT("no physical camera exposure"), S.bOverride_AutoExposureApplyPhysicalCameraExposure && !S.AutoExposureApplyPhysicalCameraExposure);
			TestTrue(P + TEXT("bloom 0"), S.bOverride_BloomIntensity && S.BloomIntensity == 0.f);
			TestTrue(P + TEXT("vignette 0"), S.bOverride_VignetteIntensity && S.VignetteIntensity == 0.f);
			TestTrue(P + TEXT("grain 0"), S.bOverride_FilmGrainIntensity && S.FilmGrainIntensity == 0.f);
			TestTrue(P + TEXT("flares 0"), S.bOverride_LensFlareIntensity && S.LensFlareIntensity == 0.f);
			TestTrue(P + TEXT("motion blur 0"), S.bOverride_MotionBlurAmount && S.MotionBlurAmount == 0.f);
			TestTrue(P + TEXT("fringe 0"), S.bOverride_SceneFringeIntensity && S.SceneFringeIntensity == 0.f);
			TestTrue(P + TEXT("AO 0"), S.bOverride_AmbientOcclusionIntensity && S.AmbientOcclusionIntensity == 0.f);
			const TArray<FWeightedBlendable>& B = S.WeightedBlendables.Array;
			TestTrue(P + TEXT("one blendable, PP_SSXAuthoredColour x 1"), B.Num() == 1 && B[0].Weight == 1.f && B[0].Object
				&& B[0].Object->GetName() == TEXT("PP_SSXAuthoredColour"));
		}
		TestEqual(FString::Printf(TEXT("%s: one presentation volume"), Map), Volumes, 1);
	}
	if (Maps == 0) { SSXTest::Skip(*this, TEXT("SKIPPED map volumes: Maps/FrontEnd and Maps/Snow missing")); }
	return true;
}

// The crowd (USSXPropAnimation::ApplyCrowd) holds its Classic instances weakly and animates them every frame; while the slot
// shows the lit twin the Classic instance must survive garbage collection, whichever of look switch and crowd setup came first.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderFollowerAliveTest, "SSX.Render.FollowerAlive", RenderLookFlags)
bool FSSXRenderFollowerAliveTest::RunTest(const FString&)
{
	UMaterialInterface* Crowd = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXPropCrowd.M_SSXPropCrowd"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Crowd || !Cube)
	{
		AddError(TEXT("M_SSXPropCrowd or the engine cube missing"));
		return false;
	}
	UWorld* W = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXFollowerAlive"));
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(W);
	for (const bool bLookFirst : { true, false })
	{
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>();
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetStaticMesh(Cube);
		TWeakObjectPtr<UMaterialInstanceDynamic> Classic;
		auto SetCrowd = [&]()
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Crowd, A);
			SSXRemaster::SetClassicMaterial(*C, 0, Mid);
			Classic = Mid;
		};
		if (!bLookFirst) { SetCrowd(); }
		USSXRemasterLibrary::SetRemasteredInWorld(W, true);
		if (bLookFirst) { SetCrowd(); }
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
		const FString Case = bLookFirst ? TEXT("look first") : TEXT("crowd first");
		TestTrue(Case + TEXT(": Classic crowd instance survives GC"), Classic.IsValid());
		TestTrue(Case + TEXT(": slot shows the lit twin"), C->GetMaterial(0) != Classic.Get() && C->GetMaterial(0) != nullptr);
		TestTrue(Case + TEXT(": ClassicMaterial is the animated instance"), SSXRemaster::ClassicMaterial(*C, 0) == Classic.Get());
		USSXRemasterLibrary::SetRemasteredInWorld(W, false);
		TestTrue(Case + TEXT(": Classic restores the instance"), C->GetMaterial(0) == Classic.Get());
		A->Destroy();
	}
	GEngine->DestroyWorldContext(W);
	W->DestroyWorld(false);
	return true;
}

// USSXRemasterWorld::SetGlow (the Showoff crystals): a component of its own, on the lit material, glowing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderGlowTest, "SSX.Render.Glow", RenderLookFlags)
bool FSSXRenderGlowTest::RunTest(const FString&)
{
	UMaterialInterface* Prop = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXProp.M_SSXProp"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Prop || !Cube)
	{
		AddError(TEXT("M_SSXProp or the engine cube missing"));
		return false;
	}
	UWorld* W = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXGlow"));
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(W);
	UStaticMeshComponent* C[2] = {};
	for (UStaticMeshComponent*& M : C)
	{
		M = W->SpawnActor<AStaticMeshActor>()->GetStaticMeshComponent();
		M->SetStaticMesh(Cube);
		M->SetMaterial(0, Prop);
	}
	USSXRemasterLibrary::SetRemasteredInWorld(W, true);
	TestTrue(TEXT("both share the lit twin"), C[0]->GetMaterial(0) == C[1]->GetMaterial(0) && C[0]->GetMaterial(0) != Prop);
	SSXRemaster::SetGlow(*C[0], 2.f);
	const UMaterialInstanceDynamic* Own = Cast<UMaterialInstanceDynamic>(C[0]->GetMaterial(0));
	float Glow = 0.f, Other = -1.f;
	TestTrue(TEXT("glowing component has its own instance"), Own && Own != C[1]->GetMaterial(0));
	TestTrue(TEXT("its parent is a material, not an instance"), Own && Own->Parent && !Own->Parent->IsA<UMaterialInstanceDynamic>());
	TestTrue(TEXT("RmGlow set"), Own && Own->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("RmGlow")), Glow) && Glow == 2.f);
	Cast<UMaterialInstanceDynamic>(C[1]->GetMaterial(0))->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("RmGlow")), Other);
	TestEqual(TEXT("the other component does not glow"), Other, 0.f);
	USSXRemasterLibrary::SetRemasteredInWorld(W, false);
	TestTrue(TEXT("Classic restores the material"), C[0]->GetMaterial(0) == Prop);
	GEngine->DestroyWorldContext(W);
	W->DestroyWorld(false);
	return true;
}

// Glass / water props: an animated soft layer (its twin made when game code first animates it, after the look switch)
// takes the glass material; a lit one the mirror class.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderSurfaceLookTest, "SSX.Render.SurfaceLook", RenderLookFlags)
bool FSSXRenderSurfaceLookTest::RunTest(const FString&)
{
	UMaterialInterface* Soft = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXProp_Translucent.M_SSXProp_Translucent"));
	UMaterialInterface* Lit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXProp.M_SSXProp"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Soft || !Lit || !Cube)
	{
		AddError(TEXT("M_SSXProp(_Translucent) or the engine cube missing"));
		return false;
	}
	UWorld* W = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXSurfaceLook"));
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(W);
	UStaticMeshComponent* C = W->SpawnActor<AStaticMeshActor>()->GetStaticMeshComponent();
	C->SetStaticMesh(Cube);
	C->SetMaterial(0, Soft);
	USSXRemasterWorld* R = W->GetSubsystem<USSXRemasterWorld>();
	USSXRemasterLibrary::SetRemasteredInWorld(W, true);
	if (!TestNotNull(TEXT("remaster subsystem"), R)) { return false; }
	R->AddLookComponent(*C);
	UMaterialInstanceDynamic* Classic = SSXRemaster::ClassicMid(*C, 0);   // game code animating the slot (UV scroll)
	const UMaterialInstanceDynamic* Shown = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
	TestTrue(TEXT("game code gets the Classic instance"), Classic && Classic != Shown);
	TestTrue(TEXT("the shown twin is the glass material"), Shown && Shown->Parent && Shown->Parent->GetName() == TEXT("M_RmPropGlass"));
	USSXRemasterLibrary::SetRemasteredInWorld(W, false);
	C->SetMaterial(0, Lit);
	USSXRemasterLibrary::SetRemasteredInWorld(W, true);
	R->AddLookComponent(*C);
	SSXRemaster::ClassicMid(*C, 0);
	float Surface = 0.f;
	const UMaterialInstanceDynamic* LitShown = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
	TestTrue(TEXT("a lit layer gets the mirror class"),
		LitShown && LitShown->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("RmSurface")), Surface) && Surface == 4.f);
	USSXRemasterLibrary::SetRemasteredInWorld(W, false);
	GEngine->DestroyWorldContext(W);
	W->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderLookSwitchTest, "SSX.Render.LookSwitch", RenderLookFlags)
bool FSSXRenderLookSwitchTest::RunTest(const FString&)
{
	FLookRestore Restore;
	SSXRenderLook::Set(ESSXRenderLook::Classic);
	TArray<ESSXRenderLook> Seen;
	const FDelegateHandle H = SSXRenderLook::OnChanged().AddLambda([&Seen](ESSXRenderLook L) { Seen.Add(L); });
	SSXRenderLook::Set(ESSXRenderLook::Remastered);
	SSXRenderLook::Set(ESSXRenderLook::Remastered);
	TestEqual(TEXT("Get after Set(Remastered)"), int32(SSXRenderLook::Get()), 1);
	SSXRenderLook::Set(ESSXRenderLook::Classic);
	SSXRenderLook::OnChanged().Remove(H);
	TestEqual(TEXT("OnChanged fires once per change"), Seen.Num(), 2);
	TestTrue(TEXT("OnChanged order"), Seen.Num() == 2 && Seen[0] == ESSXRenderLook::Remastered && Seen[1] == ESSXRenderLook::Classic);

	IConsoleVariable* Probe = IConsoleManager::Get().RegisterConsoleVariable(TEXT("ssx.Test.RenderLookProbe"), 7, TEXT("test only"));
	const SSXRenderLook::FCvarValue Set[] = { { TEXT("ssx.Test.RenderLookProbe"), TEXT("3") }, { TEXT("ssx.Test.NoSuchCvar"), TEXT("1") } };
	SSXRenderLook::ApplyCvars(Set, ESSXRenderLook::Remastered);
	TestEqual(TEXT("Remastered sets the cvar"), Probe->GetInt(), 3);
	Probe->Set(5, ECVF_SetByCode);
	SSXRenderLook::ApplyCvars(Set, ESSXRenderLook::Remastered);
	SSXRenderLook::ApplyCvars(Set, ESSXRenderLook::Classic);
	TestEqual(TEXT("Classic restores the value from before the first Remastered"), Probe->GetInt(), 7);
	SSXRenderLook::ApplyCvars(Set, ESSXRenderLook::Classic);
	TestEqual(TEXT("Classic twice is a no-op"), Probe->GetInt(), 7);
	IConsoleManager::Get().UnregisterConsoleObject(Probe);

	for (const SSXRenderLook::FCvarValue& C : SSXRenderLook::RemasteredCvars())
	{
		TestNotNull(FString::Printf(TEXT("Remastered cvar %s exists"), C.Name), IConsoleManager::Get().FindConsoleVariable(C.Name));
	}
	return true;
}

#endif
