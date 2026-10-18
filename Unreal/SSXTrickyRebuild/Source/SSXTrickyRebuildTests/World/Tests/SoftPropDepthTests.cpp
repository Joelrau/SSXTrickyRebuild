#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Particles/SSXGsBlend.h"
#include "Particles/SSXParticleWorld.h"
#include "World/SSXWorldPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace
{
	FString SoftSquash(const FString& S)
	{
		FString O;
		for (TCHAR C : S) { if (!FChar::IsWhitespace(C)) { O.AppendChar(C); } }
		return O;
	}

	const UMaterialExpressionCustom* SoftFindNode(const UMaterial* M, const TCHAR* Desc)
	{
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E.Get());
			if (C && C->Description == Desc) { return C; }
		}
		return nullptr;
	}

	bool ReadsCustomDepth(const UMaterial* M)
	{
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			const UMaterialExpressionSceneTexture* T = Cast<UMaterialExpressionSceneTexture>(E.Get());
			if (T && T->SceneTextureId == PPI_CustomDepth) { return true; }
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldSoftPropDepthTest, "SSX.World.SoftPropDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXWorldSoftPropDepthTest::RunTest(const FString&)
{
	using namespace SSXGs;
	const FString Prop = SoftSquash(SoftDepthHlsl());
	for (const TCHAR* Name : { TEXT("M_SSXProp_Translucent"), TEXT("M_SSXProp_Translucent_TwoSided") })
	{
		const UMaterial* M = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/Game/World/%s.%s"), Name, Name));
		if (!M)
		{
			AddError(FString::Printf(TEXT("/Game/World/%s missing (Scripts/make_world_materials.py)"), Name));
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: translucent (GS mode 3)"), Name), int32(M->GetBlendMode()), int32(BLEND_Translucent));
		TestTrue(FString::Printf(TEXT("%s: writes custom depth (the soft texels' ZMSK 0)"), Name), bool(M->AllowTranslucentCustomDepthWrites));
		TestEqual(FString::Printf(TEXT("%s: custom-depth clip"), Name), M->GetOpacityMaskClipValue(), SoftDepthClip);
		const UMaterialExpressionCustom* N = SoftFindNode(M, TEXT("SoftDepth"));
		TestTrue(FString::Printf(TEXT("%s: SoftDepth node = SSXGs::SoftDepthHlsl"), Name), N && SoftSquash(N->Code) == Prop);
		TestTrue(FString::Printf(TEXT("%s: reads custom depth"), Name), ReadsCustomDepth(M));
	}
	TestTrue(TEXT("custom-depth clip below the smallest passing opacity"), SoftDepthClip > 0.f && SoftDepthClip < float(PropAlphaRef + 1) / 128.f);
	for (const TCHAR* Name : { TEXT("M_SSXProp_TranslucentCore"), TEXT("M_SSXProp"), TEXT("M_SSXProp_Masked") })
	{
		if (const UMaterial* M = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/Game/World/%s.%s"), Name, Name)))
		{
			TestNull(FString::Printf(TEXT("%s: no SoftDepth node (depth-writing section)"), Name), SoftFindNode(M, TEXT("SoftDepth")));
		}
	}

	const FString Sprite = SoftSquash(SoftDepthSpriteHlsl());
	for (const TCHAR* Name : { TEXT("M_SSXParticleAlpha"), TEXT("M_SSXParticleAdd"), TEXT("M_SSXParticleDarken"),
		TEXT("M_SSXParticleBrighten"), TEXT("M_SSXTrailDarken"), TEXT("M_SSXTrailBrighten") })
	{
		const UMaterial* M = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/Game/Particles/%s.%s"), Name, Name));
		if (!M)
		{
			AddError(FString::Printf(TEXT("/Game/Particles/%s missing (Scripts/make_particle_materials.py)"), Name));
			continue;
		}
		const UMaterialExpressionCustom* N = SoftFindNode(M, TEXT("SoftDepth"));
		TestTrue(FString::Printf(TEXT("%s: SoftDepth node = SSXGs::SoftDepthSpriteHlsl"), Name), N && SoftSquash(N->Code) == Sprite);
		TestFalse(FString::Printf(TEXT("%s: depth test on"), Name), M->bDisableDepthTest);
	}
	for (const TCHAR* Name : { TEXT("M_SSXLensSprite"), TEXT("M_SSXLensFan") })
	{
		if (const UMaterial* M = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/Game/Particles/%s.%s"), Name, Name)))
		{
			TestNull(FString::Printf(TEXT("%s: no SoftDepth (ZTST ALWAYS)"), Name), SoftFindNode(M, TEXT("SoftDepth")));
		}
	}

	{
		const float Inf = TNumericLimits<float>::Max();
		TestTrue(TEXT("nothing wrote custom depth: kept"), SoftDepthFront(12000.f, Inf));
		TestTrue(TEXT("the writer's own texel: kept"), SoftDepthFront(8000.f, 8000.f));
		TestTrue(TEXT("in front of the soft texel: kept"), SoftDepthFront(7000.f, 8000.f));
		TestFalse(TEXT("50 cm behind a soft texel at 80 m: dropped"), SoftDepthFront(8050.f, 8000.f));
		const int32 Ice = GsPropPixel(180, 117, 230);
		TestEqual(TEXT("ice texel"), Ice, 184);
		const int32 Behind = SoftDepthFront(9000.f, 8000.f) ? GsPropPixel(111, 81, Ice) : Ice;
		TestEqual(TEXT("fog puff behind the ice: frame unchanged"), Behind, 184);
		const int32 Front = SoftDepthFront(7000.f, 8000.f) ? GsPropPixel(111, 81, Ice) : Ice;
		TestEqual(TEXT("fog puff in front of the ice: GS mode 3"), Front, 137);
	}

	{
		UMaterialInterface* Soft = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXProp_Translucent.M_SSXProp_Translucent"));
		UMaterialInterface* Core = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXProp_TranslucentCore.M_SSXProp_TranslucentCore"));
		UMaterialInterface* Plain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXProp.M_SSXProp"));
		if (Soft && Core && Plain)
		{
			UMaterialInstanceConstant* SoftMic = NewObject<UMaterialInstanceConstant>(GetTransientPackage());
			SoftMic->SetParentEditorOnly(Soft);
			UMaterialInstanceConstant* CoreMic = NewObject<UMaterialInstanceConstant>(GetTransientPackage());
			CoreMic->SetParentEditorOnly(Core);
			TestTrue(TEXT("instance of M_SSXProp_Translucent is a soft blend"), USSXWorldPresentation::IsSoftBlendMaterial(SoftMic));
			TestFalse(TEXT("instance of the core is not"), USSXWorldPresentation::IsSoftBlendMaterial(CoreMic));
			TestFalse(TEXT("M_SSXProp is not"), USSXWorldPresentation::IsSoftBlendMaterial(Plain));
			TestFalse(TEXT("null is not"), USSXWorldPresentation::IsSoftBlendMaterial(nullptr));

			UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXSoftDepthTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			ON_SCOPE_EXIT
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			};
			auto Spawn = [&](std::initializer_list<UMaterialInterface*> Mats)
			{
				AActor* A = World->SpawnActor<AActor>();
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(A);
				C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				A->SetRootComponent(C);
				C->RegisterComponent();
				int32 I = 0;
				for (UMaterialInterface* M : Mats) { C->SetMaterial(I++, M); }
				return C;
			};
			UStaticMeshComponent* SoftComp = Spawn({ SoftMic });
			UStaticMeshComponent* CoreComp = Spawn({ CoreMic });
			UStaticMeshComponent* PlainComp = Spawn({ Plain });
			const int32 N = USSXWorldPresentation::MarkSoftPropDepth(*World);
			TestEqual(TEXT("one soft-prop component marked"), N, 1);
			TestTrue(TEXT("soft blend section renders custom depth"), SoftComp->bRenderCustomDepth);
			TestFalse(TEXT("core-only component does not (scene depth)"), CoreComp->bRenderCustomDepth);
			TestFalse(TEXT("plain prop does not"), PlainComp->bRenderCustomDepth);
		}
		else
		{
			AddError(TEXT("prop parents missing (Scripts/make_world_materials.py)"));
		}
	}

	TestTrue(TEXT("sprite renderer sorts after the props"), USSXParticleWorld::SpriteSortPriority > 0);
	return !HasAnyErrors();
}

#endif
