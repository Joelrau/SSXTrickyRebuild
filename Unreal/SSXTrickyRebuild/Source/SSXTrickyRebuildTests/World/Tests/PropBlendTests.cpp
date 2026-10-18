// FUN_00200390 / 0x1E1F70
#include "AssetRegistry/IAssetRegistry.h"
#include "Internationalization/Regex.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Particles/SSXGsBlend.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace
{
	constexpr EAutomationTestFlags PropBlendTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FString SquashWs(const FString& S)
	{
		FString O;
		for (TCHAR C : S) { if (!FChar::IsWhitespace(C)) { O.AppendChar(C); } }
		return O;
	}

	const UMaterialExpressionCustom* FindNode(const UMaterial* M, const TCHAR* Desc)
	{
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E.Get());
			if (C && C->Description == Desc) { return C; }
		}
		return nullptr;
	}

	const UMaterial* RootOf(const UMaterialInterface* M)
	{
		while (const UMaterialInstance* MI = Cast<UMaterialInstance>(M)) { M = MI->Parent; }
		return Cast<UMaterial>(M);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldPropBlendTest, "SSX.World.PropBlend", PropBlendTestFlags)
bool FSSXWorldPropBlendTest::RunTest(const FString&)
{
	using namespace SSXGs;
	struct FWant { const TCHAR* Name; EBlendMode Blend; const TCHAR* Node; const TCHAR* Code; };
	const FString Atest = FString::Printf(TEXT("returnAs>%d.0?1.0:0.0;"), PropAlphaRef);
	const FString Blend = FString::Printf(TEXT("return(As>%d.0&&As<128.0)?As/128.0:0.0;"), PropAlphaRef);
	const FString Core = TEXT("returnAs>=128.0?1.0:0.0;");
	const FWant Parents[] = {
		{ TEXT("M_SSXProp"), BLEND_Masked, nullptr, nullptr },
		{ TEXT("M_SSXProp_TwoSided"), BLEND_Masked, nullptr, nullptr },
		{ TEXT("M_SSXPropOpaque"), BLEND_Opaque, nullptr, nullptr },
		{ TEXT("M_SSXProp_Masked"), BLEND_Masked, TEXT("PropAlphaTest"), *Atest },
		{ TEXT("M_SSXProp_Masked_TwoSided"), BLEND_Masked, TEXT("PropAlphaTest"), *Atest },
		{ TEXT("M_SSXPropCrowd"), BLEND_Masked, TEXT("PropAlphaTest"), *Atest },
		{ TEXT("M_SSXProp_TranslucentCore"), BLEND_Masked, TEXT("PropCore"), *Core },
		{ TEXT("M_SSXProp_TranslucentCore_TwoSided"), BLEND_Masked, TEXT("PropCore"), *Core },
		{ TEXT("M_SSXProp_Translucent"), BLEND_Translucent, TEXT("PropBlend"), *Blend },
		{ TEXT("M_SSXProp_Translucent_TwoSided"), BLEND_Translucent, TEXT("PropBlend"), *Blend },
	};
	const FString AlphaHlsl = SquashWs(PropAlphaHlsl());
	for (const FWant& W : Parents)
	{
		const UMaterial* M = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/Game/World/%s.%s"), W.Name, W.Name));
		if (!M)
		{
			AddError(FString::Printf(TEXT("/Game/World/%s missing (Scripts/make_world_materials.py)"), W.Name));
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: blend mode"), W.Name), int32(M->GetBlendMode()), int32(W.Blend));
		TestTrue(FString::Printf(TEXT("%s: two-sided (no GS culling)"), W.Name), M->IsTwoSided());
		TestTrue(FString::Printf(TEXT("%s: unlit"), W.Name), M->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
		TestFalse(FString::Printf(TEXT("%s: depth test on (ZTST GREATER)"), W.Name), M->bDisableDepthTest);
		if (W.Blend == BLEND_Masked)
		{
			TestEqual(FString::Printf(TEXT("%s: clip value splits the 0 / 1 mask"), W.Name), M->GetOpacityMaskClipValue(), 0.5f);
		}
		if (W.Node)
		{
			const UMaterialExpressionCustom* A = FindNode(M, TEXT("PropGsAlpha"));
			const UMaterialExpressionCustom* N = FindNode(M, W.Node);
			TestTrue(FString::Printf(TEXT("%s: PropGsAlpha = SSXGs::PropAlphaHlsl"), W.Name), A && SquashWs(A->Code) == AlphaHlsl);
			TestTrue(FString::Printf(TEXT("%s: %s node (AREF %d)"), W.Name, W.Node, PropAlphaRef), N && SquashWs(N->Code) == W.Code);
		}
		else
		{
			TestNull(FString::Printf(TEXT("%s: no texture alpha (opaque textures)"), W.Name), FindNode(M, TEXT("PropGsAlpha")));
		}
		const UMaterialEditorOnlyData* Ed = M->GetEditorOnlyData();
		TestEqual(FString::Printf(TEXT("%s: opacity only on the translucent blend"), W.Name), Ed && Ed->Opacity.Expression != nullptr,
			W.Blend == BLEND_Translucent);
	}

	{
		const int32 Css[] = { 0, 30, 77, 128, 200, 255 };
		const int32 Cds[] = { 0, 16, 64, 128, 201, 255 };
		const int32 Avs[] = { 128, 127, 64 };
		int32 Cases = 0, MaxDiff = 0, AsMismatch = 0;
		for (int32 P = 0; P <= 255; ++P)
		{
			for (int32 Av : Avs)
			{
				const int32 At = TexelAlphaFromPng(P);
				const int32 GsAs = FMath::Min((At * Av) >> 7, 255);
				const float As = PropAlphaNode(P / 255.f, Av / 255.f);
				if (int32(As) != GsAs) { ++AsMismatch; }
				for (int32 Cs : Css)
				{
					for (int32 Cd : Cds)
					{
						const int32 Gs = GsPropPixel(Cs, GsAs, Cd);
						const int32 Port = FMath::RoundToInt(FMath::Clamp(PropMaterialPixel(Cs / 255.f, As, Cd / 255.f), 0.f, 1.f) * 255.f);
						MaxDiff = FMath::Max(MaxDiff, FMath::Abs(Port - Gs));
						++Cases;
					}
				}
			}
		}
		TestEqual(TEXT("PropGsAlpha = GS MODULATE As for every PNG alpha byte"), AsMismatch, 0);
		TestTrue(FString::Printf(TEXT("prop pixel: |port - GS| <= 1 level (GS floors the >> 7; max %d over %d cases)"), MaxDiff, Cases), MaxDiff <= 1);
		TestEqual(TEXT("ice rock edge: As 117"), int32(PropAlphaNode(234 / 255.f, 128 / 255.f)), 117);
		TestEqual(TEXT("ice rock edge: GS 184"), GsPropPixel(180, 117, 230), 184);
		TestTrue(TEXT("ice rock edge: port within 1 level"), FMath::Abs(PropMaterialPixel(180 / 255.f, 117.f, 230 / 255.f) * 255.f - 184.f) <= 1.f);
		TestEqual(TEXT("At 1 (PNG 2) fails AREF 1: frame kept"), GsPropPixel(200, int32(PropAlphaNode(2 / 255.f, 128 / 255.f)), 50), 50);
		TestEqual(TEXT("At 2 (PNG 4) passes AREF 1"), int32(PropAlphaNode(4 / 255.f, 128 / 255.f)), 2);
		TestEqual(TEXT("At 128 (PNG 255): core, Cs"), PropMaterialPixel(0.5f, PropAlphaNode(1.f, 128 / 255.f), 0.9f), 0.5f);
	}

	IAssetRegistry* AR = IAssetRegistry::Get();
	if (!AR)
	{
		AddError(TEXT("no asset registry"));
		return false;
	}
	AR->SearchAllAssets(true);
	const FRegexPattern NamePattern(TEXT("^tex_(-?\\d+)(_2s)?_f([0-9a-f]+)(_cut|_soft)?(_core|_blend)?_rop(\\w*)$"));
	const TCHAR* Tracks[] = { TEXT("Alaska"), TEXT("Aloha"), TEXT("Elysium"), TEXT("Gari"), TEXT("Megaple"), TEXT("Merquer"),
		TEXT("Mesa"), TEXT("Pipe"), TEXT("Snow"), TEXT("Trick"), TEXT("Untrack") };
	int32 Total = 0, Bad = 0;
	TSet<int32> AlaskaTranslucentTextures;
	for (const TCHAR* Track : Tracks)
	{
		FARFilter Filter;
		Filter.PackagePaths = { FName(*FString::Printf(TEXT("/Game/Tracks/%s"), Track)) };
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		TArray<FAssetData> Found;
		AR->GetAssets(Filter, Found);
		int32 NTrans = 0, NCore = 0, NMasked = 0, NOpaque = 0, NTransFlag = 0, NFlag2 = 0;
		for (const FAssetData& A : Found)
		{
			const FString Name = A.AssetName.ToString();
			FRegexMatcher Mt(NamePattern, Name);
			if (!Mt.FindNext()) { continue; }
			const int32 Tex = FCString::Atoi(*Mt.GetCaptureGroup(1));
			const bool b2s = !Mt.GetCaptureGroup(2).IsEmpty();
			const uint32 Flags = FCString::Strtoui64(*Mt.GetCaptureGroup(3), nullptr, 16);
			const FString Alpha = Mt.GetCaptureGroup(4), Part = Mt.GetCaptureGroup(5);
			FString Want = Part == TEXT("_core") ? TEXT("M_SSXProp_TranslucentCore") : Part == TEXT("_blend") ? TEXT("M_SSXProp_Translucent")
				: !Alpha.IsEmpty() ? TEXT("M_SSXProp_Masked") : TEXT("M_SSXProp");
			if (b2s) { Want += TEXT("_TwoSided"); }
			const UMaterialInstanceConstant* Mic = Cast<UMaterialInstanceConstant>(A.GetAsset());
			const UMaterial* Root = RootOf(Mic);
			const FString RootName = Root ? Root->GetName() : FString(TEXT("none"));
			if (RootName != Want)
			{
				if (++Bad <= 10) { AddError(FString::Printf(TEXT("%s %s: parent %s, the texture alpha needs %s"), Track, *Name, *RootName, *Want)); }
				continue;
			}
			++Total;
			if (Part == TEXT("_blend"))
			{
				++NTrans;
				NTransFlag += (Flags & 0x40000) ? 1 : 0;
				if (FCString::Strcmp(Track, TEXT("Alaska")) == 0) { AlaskaTranslucentTextures.Add(Tex); }
			}
			else if (Part == TEXT("_core")) { ++NCore; }
			else if (!Alpha.IsEmpty()) { ++NMasked; }
			else { ++NOpaque; }
			NFlag2 += (Flags & 0x20000) ? 1 : 0;
			if (Part == TEXT("_blend") && !(Flags & 0x40000)) { AddWarning(FString::Printf(TEXT("%s %s: soft alpha without flag 0x40000 (drawn in material order in the original)"), Track, *Name)); }
			if ((Flags & 0x20000) && Alpha != TEXT("_cut") && Alpha != TEXT("")) { AddWarning(FString::Printf(TEXT("%s %s: flag 0x20000 with soft alpha"), Track, *Name)); }
		}
		AddInfo(FString::Printf(TEXT("%s: prop material instances: %d translucent blend (+ %d cores; %d with flag 0x40000), %d masked (cutout), %d opaque; %d with flag 0x20000"),
			Track, NTrans, NCore, NTransFlag, NMasked, NOpaque, NFlag2));
		TestEqual(FString::Printf(TEXT("%s: every blend section has its core"), Track), NCore, NTrans);
	}
	if (Total == 0)
	{
		AddError(TEXT("no prop material instances under /Game/Tracks (re-run tools/convert_tracks.py and Scripts/build_track_map.py)"));
	}
	for (const int32 Tex : { 31, 32, 33, 34, 35, 36, 43, 44, 45 })
	{
		TestTrue(FString::Printf(TEXT("Alaska texture %d (ice rock) is translucent"), Tex), AlaskaTranslucentTextures.Contains(Tex));
	}
	AddInfo(FString::Printf(TEXT("prop materials: %d instances with the parent their texture alpha needs, %d wrong"), Total, Bad));
	return !HasAnyErrors();
}

#endif
