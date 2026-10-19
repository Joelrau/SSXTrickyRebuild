#include "Engine/PostProcessVolume.h"
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
		{ TEXT("World/M_SSXLine.uasset"), TEXT("43974D40DE94AB8ACC0E3590ACAF36F6E090914F") },
		{ TEXT("World/M_SSXProp.uasset"), TEXT("7B145B189BBDA29DF045252E8A603385A89DB215") },
		{ TEXT("World/M_SSXPropCrowd.uasset"), TEXT("035A5FD1F74A41895677ACB6D211B62E878C3FFE") },
		{ TEXT("World/M_SSXPropOpaque.uasset"), TEXT("6A29A666ADD830EAFF5656EA48524B88BDD5671E") },
		{ TEXT("World/M_SSXPropOpaque_TwoSided.uasset"), TEXT("7CA1B1DD22C545D955DDC7F6ECA17221EB7789A9") },
		{ TEXT("World/M_SSXProp_Masked.uasset"), TEXT("FA731956B76949B22AD37A7F1C8635A20842C3D1") },
		{ TEXT("World/M_SSXProp_Masked_TwoSided.uasset"), TEXT("DA7D46AC87CC0578E103FA602EB63BDF32C4078E") },
		{ TEXT("World/M_SSXProp_Translucent.uasset"), TEXT("9E4E91732A5746E4A1786E3D29643A0595F06883") },
		{ TEXT("World/M_SSXProp_TranslucentCore.uasset"), TEXT("07BDDF322A6904B8785ED3E2F79552BC2F408E0A") },
		{ TEXT("World/M_SSXProp_TranslucentCore_TwoSided.uasset"), TEXT("45CA643FE1BCE3346A96034FCCCCB80C2507AC45") },
		{ TEXT("World/M_SSXProp_Translucent_TwoSided.uasset"), TEXT("112A725F28A36F8CB73E94B9813581C408768BB1") },
		{ TEXT("World/M_SSXProp_TwoSided.uasset"), TEXT("7FEA0DF081466B9FC494282CFE12ACE18590E765") },
		{ TEXT("World/M_SSXRiderLit.uasset"), TEXT("239EC9401CA211EEBF04E1D354D0E2CCBBA8EA38") },
		{ TEXT("World/M_SSXRiderLitChrome.uasset"), TEXT("3B4931702470327C1AF2A60E4F7AFC87F0C0695F") },
		{ TEXT("World/M_SSXRiderLitChrome_Masked.uasset"), TEXT("F9395F0AAB6F9351DD7AF687EFAF78CE1AB8D812") },
		{ TEXT("World/M_SSXRiderLit_Masked.uasset"), TEXT("8A004FD461218DB47700082D87D92CE4E68F3F8F") },
		{ TEXT("World/M_SSXSky.uasset"), TEXT("380743DB5DF12902BC0A34836A5878F92F93ED6C") },
		{ TEXT("World/M_SSXTerrain.uasset"), TEXT("75ABF9890D598DDB13C6AA2BADE84BD925429E48") },
		{ TEXT("World/M_SSXTerrainOpaque.uasset"), TEXT("6669222DD461ADAC29C343E8446FAFC2D7F1AD96") },
		{ TEXT("World/M_SSXTerrain_Masked.uasset"), TEXT("65B9338898232EAD0D65044743149240C29C5ECA") },
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
