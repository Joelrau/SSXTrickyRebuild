#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Misc/AutomationTest.h"
#include "Particles/SSXFogVolumes.h"
#include "Particles/SSXGsBlend.h"
#include "Particles/SSXParticleWorld.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace
{
	constexpr EAutomationTestFlags FogBlendTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
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
			if (C && (!Desc || C->Description == Desc)) { return C; }
		}
		return nullptr;
	}

	float OldAlpha(const FLinearColor& Tex, const FColor& Vc, float CdOpaque)
	{
		const float T = FMath::Clamp(Tex.R * Vc.R / 255.f * 255.f / 128.f, 0.f, 1.f);   // stored byte / 255 x Cv / 128
		const float A = FMath::Clamp(Tex.A * Vc.A / 255.f * 255.f / 128.f, 0.f, 1.f);
		return FMath::Clamp(FMath::Lerp(CdOpaque, T, A), 0.f, 1.f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFogBlendTest, "SSX.World.FogBlend", FogBlendTestFlags)
bool FSSXWorldFogBlendTest::RunTest(const FString&)
{
	using namespace SSXGs;
	struct FWant { const TCHAR* Path; EBlendMode Blend; int32 Mode; bool bOpacity; };
	const FWant Sprites[] = {
		{ USSXParticleWorld::AlphaMaterialPath, BLEND_Translucent, 3, true },
		{ USSXParticleWorld::AddMaterialPath, BLEND_Additive, 5, false },
		{ USSXParticleWorld::DarkenMaterialPath, BLEND_Modulate, 4, false },
		{ USSXParticleWorld::BrightenMaterialPath, BLEND_Modulate, 8, false },
	};
	const FString Hlsl = Squash(SpriteHlsl());
	for (const FWant& W : Sprites)
	{
		const UMaterial* M = LoadObject<UMaterial>(nullptr, W.Path);
		if (!M)
		{
			AddError(FString::Printf(TEXT("%s missing (Scripts/make_particle_materials.py)"), W.Path));
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: blend mode for GS ALPHA mode %d"), *M->GetName(), W.Mode), int32(M->GetBlendMode()), int32(W.Blend));
		TestTrue(FString::Printf(TEXT("%s: two-sided (no GS culling)"), *M->GetName()), M->IsTwoSided());
		TestTrue(FString::Printf(TEXT("%s: unlit"), *M->GetName()), M->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
		TestFalse(FString::Printf(TEXT("%s: depth test on (ZTST GREATER)"), *M->GetName()), M->bDisableDepthTest);
		const UMaterialExpressionCustom* Node = FindCustom(M, TEXT("GsSprite"));
		TestTrue(FString::Printf(TEXT("%s: GsSprite node = SSXGs::SpriteHlsl"), *M->GetName()), Node && Squash(Node->Code) == Hlsl);
		bool bLinear = false;
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			if (const UMaterialExpressionTextureSampleParameter2D* T = Cast<UMaterialExpressionTextureSampleParameter2D>(E.Get()))
			{
				bLinear |= T->ParameterName == TEXT("Tex") && T->SamplerType == SAMPLERTYPE_LinearColor;
			}
		}
		TestTrue(FString::Printf(TEXT("%s: 'Tex' sampled linear (stored bytes)"), *M->GetName()), bLinear);
		const UMaterialEditorOnlyData* Ed = M->GetEditorOnlyData();
		TestEqual(FString::Printf(TEXT("%s: opacity = As / 128 only for the alpha blend"), *M->GetName()),
			Ed && Ed->Opacity.Expression != nullptr, W.bOpacity);
	}
	{
		int32 Checked = 0;
		for (const TCHAR* Name : { TEXT("M_SSXTerrain"), TEXT("M_SSXTerrain_Masked"), TEXT("M_SSXTerrainOpaque"), TEXT("M_SSXProp"),
			TEXT("M_SSXProp_Masked"), TEXT("M_SSXPropOpaque"), TEXT("M_SSXProp_TwoSided"), TEXT("M_SSXPropCrowd"), TEXT("M_SSXSky"),
			TEXT("M_SSXClearColour"), TEXT("M_SSXRiderLit"), TEXT("M_SSXRiderLit_Masked"), TEXT("PP_SSXAuthoredColour") })
		{
			const UMaterial* M = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/Game/World/%s.%s"), Name, Name));
			if (!M) { AddError(FString::Printf(TEXT("/Game/World/%s missing (Scripts/make_world_materials.py)"), Name)); continue; }
			for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
			{
				if (const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E.Get()))
				{
					++Checked;
					const FString S = Squash(C->Code);
					TestFalse(FString::Printf(TEXT("%s %s: no sRGB decode of the output"), Name, *C->Description), S.Contains(TEXT("/1.055,2.4)")));
					if (FCString::Strcmp(Name, TEXT("PP_SSXAuthoredColour")) == 0)
					{
						TestFalse(TEXT("PP_SSXAuthoredColour: no sRGB encode (the scene colour is the PS2 value)"), S.Contains(TEXT("1.0/2.4")));
					}
				}
			}
		}
		TestTrue(TEXT("world custom nodes found"), Checked > 10);
	}
	{
		UTexture2D* T = UTexture2D::CreateTransient(4, 4);
		USSXParticleWorld::ConfigureSpriteTexture(T);
		TestFalse(TEXT("PARTICLE.SSH texture linear (SRGB off)"), T->SRGB);
		TestTrue(TEXT("PARTICLE.SSH texture CLAMP (slot 63 = 1)"), T->AddressX == TA_Clamp && T->AddressY == TA_Clamp);
	}
	TestEqual(TEXT("fog texture = PARTICLE.SSH image 34 'fog0'"), SSXFog::Texture, 34);

	struct FTexel { int32 R, G, B, P; };
	const FTexel Texels[] = { { 223, 222, 222, 162 }, { 226, 226, 226, 255 }, { 209, 209, 209, 96 }, { 199, 200, 200, 34 }, { 220, 220, 220, 0 } };
	const FIntVector Rgbs[] = { { 64, 64, 64 }, { 81, 81, 81 }, { 62, 101, 117 } };
	const float Fades[][2] = { { 1.f, 1.f }, { 0.5f, 1.f }, { 1.f, 0.25f }, { 0.1f, 1.f }, { 1.f, 0.01f } };
	const int32 Cds[] = { 0, 17, 64, 128, 200, 255 };
	int32 Cases = 0, MaxSingle = 0;
	for (const FTexel& T : Texels)
	{
		for (const FIntVector& Rgb : Rgbs)
		{
			for (const auto& Fd : Fades)
			{
				const int32 Av = int32(128.0f * Fd[1] * Fd[0]);
				const FColor Vc(uint8(Rgb.X), uint8(Rgb.Y), uint8(Rgb.Z), uint8(Av));
				const FLinearColor Tex(T.R / 255.f, T.G / 255.f, T.B / 255.f, T.P / 255.f);
				const FGsSource S = GsModulate(FIntVector(T.R, T.G, T.B), TexelAlphaFromPng(T.P), Rgb, Av);
				const FSpriteNode N = SpriteNode(Tex, Vc);
				TestTrue(TEXT("GsSprite node = GS MODULATE exactly"), FMath::RoundToInt(N.Cs.X * 255.f) == S.R
					&& FMath::RoundToInt(N.Cs.Y * 255.f) == S.G && FMath::RoundToInt(N.Cs.Z * 255.f) == S.B && FMath::RoundToInt(N.A * 128.f) == S.A);
				for (int32 Cd : Cds)
				{
					const int32 Gs[3] = { GsBlend(3, S.R, S.A, Cd), GsBlend(3, S.G, S.A, Cd), GsBlend(3, S.B, S.A, Cd) };
					const float Cs[3] = { N.Cs.X, N.Cs.Y, N.Cs.Z };
					for (int32 K = 0; K < 3; ++K)
					{
						const int32 Port = FMath::RoundToInt(FMath::Clamp(MaterialBlend(3, Cs[K], N.A, Cd / 255.f), 0.f, 1.f) * 255.f);
						MaxSingle = FMath::Max(MaxSingle, FMath::Abs(Port - Gs[K]));
						++Cases;
					}
					for (int32 Mode : { 5, 4, 8 })
					{
						const int32 G = GsBlend(Mode, S.R, S.A, Cd);
						const int32 P = FMath::RoundToInt(FMath::Clamp(MaterialBlend(Mode, N.Cs.X, N.A, Cd / 255.f), 0.f, 1.f) * 255.f);
						TestTrue(FString::Printf(TEXT("mode %d: GS %d, port %d (Cs %d As %d Cd %d)"), Mode, G, P, S.R, S.A, Cd), FMath::Abs(P - G) <= 1);
					}
				}
			}
		}
	}
	TestTrue(FString::Printf(TEXT("single puff: |port - GS| <= 1 level (max %d over %d channel cases)"), MaxSingle, Cases), MaxSingle <= 1);

	{
		const FGsSource S = GsModulate(FIntVector(223, 222, 222), TexelAlphaFromPng(162), FIntVector(64, 64, 64), 128);
		TestTrue(TEXT("example: Cs 111, As 81"), S.R == 111 && S.A == 81);
		TestEqual(TEXT("example: GS out 117"), GsBlend(3, S.R, S.A, 128), 117);
		const FSpriteNode N = SpriteNode(FLinearColor(223 / 255.f, 222 / 255.f, 222 / 255.f, 162 / 255.f), FColor(64, 64, 64, 128));
		const float P = MaterialBlend(3, N.Cs.X, N.A, 128 / 255.f) * 255.f;
		AddInfo(FString::Printf(TEXT("example: material Cs %.3f, opacity %.4f -> %.3f (GS 117)"), N.Cs.X * 255.f, N.A, P));
		TestTrue(TEXT("example: material within 1 level"), FMath::Abs(P - 117.f) <= 1.f);
	}

	{
		struct FLayer { FTexel T; FIntVector Rgb; int32 Av; };
		const FLayer Stacks[][3] = {
			{ { Texels[0], Rgbs[0], 128 }, { Texels[2], Rgbs[0], 128 }, { Texels[1], Rgbs[0], 64 } },
			{ { Texels[3], Rgbs[1], 128 }, { Texels[0], Rgbs[1], 12 }, { Texels[2], Rgbs[1], 128 } },
			{ { Texels[0], Rgbs[2], 128 }, { Texels[0], Rgbs[2], 128 }, { Texels[0], Rgbs[2], 128 } },
			{ { Texels[2], Rgbs[0], 32 }, { Texels[4], Rgbs[0], 128 }, { Texels[3], Rgbs[0], 100 } },
		};
		int32 MaxStack = 0, MaxOld = 0;
		for (const auto& St : Stacks)
		{
			for (int32 Cd0 : Cds)
			{
				int32 Gs = Cd0;
				float Port = Cd0 / 255.f, Old = Cd0 / 255.f;
				for (const FLayer& L : St)
				{
					const FGsSource S = GsModulate(FIntVector(L.T.R, L.T.G, L.T.B), TexelAlphaFromPng(L.T.P), L.Rgb, L.Av);
					const FLinearColor Tex(L.T.R / 255.f, L.T.G / 255.f, L.T.B / 255.f, L.T.P / 255.f);
					const FColor Vc(uint8(L.Rgb.X), uint8(L.Rgb.Y), uint8(L.Rgb.Z), uint8(L.Av));
					const FSpriteNode N = SpriteNode(Tex, Vc);
					Gs = GsBlend(3, S.R, S.A, Gs);
					Port = MaterialBlend(3, N.Cs.X, N.A, Port);
					Old = OldAlpha(Tex, Vc, Cd0 / 255.f);
				}
				MaxStack = FMath::Max(MaxStack, FMath::Abs(FMath::RoundToInt(FMath::Clamp(Port, 0.f, 1.f) * 255.f) - Gs));
				MaxOld = FMath::Max(MaxOld, FMath::Abs(FMath::RoundToInt(Old * 255.f) - Gs));
			}
		}
		AddInfo(FString::Printf(TEXT("3 stacked puffs: max |port - GS| %d levels (GS truncates each blend); old opaque-scene material: %d"), MaxStack, MaxOld));
		TestTrue(TEXT("3 stacked puffs within the GS truncation (<= 3 levels)"), MaxStack <= 3);
		TestTrue(TEXT("the old opaque-scene evaluation is off by more"), MaxOld > MaxStack);
	}
	return true;
}

#endif
