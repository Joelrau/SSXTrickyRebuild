#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXPrompts.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags PromptFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FPromptCvar
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("ssx.UI.Prompts"));
		int32 Old = V ? V->GetInt() : 1;
		void Set(int32 X) { if (V) { V->Set(X, ECVF_SetByCode); } }
		~FPromptCvar() { Set(Old); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIPromptsTest, "SSX.UI.Prompts", PromptFlags)
bool FSSXUIPromptsTest::RunTest(const FString&)
{
	using SSXPrompts::ImageFor;
	const ESSXPromptContext Menu = ESSXPromptContext::Menu, Race = ESSXPromptContext::Race;
	TestEqual(TEXT("HUD cross on Xbox"), ImageFor(0x74, true, Race, ESSXPromptDevice::Xbox), FString(TEXT("xbox/cross")));
	TestEqual(TEXT("FE square on PS5"), ImageFor(82, false, Menu, ESSXPromptDevice::PS5), FString(TEXT("ps5/square")));
	TestEqual(TEXT("cross is Space in the menus"), ImageFor(81, false, Menu, ESSXPromptDevice::Keyboard), FString(TEXT("key/space")));
	TestEqual(TEXT("circle is Left Ctrl in the menus"), ImageFor(0x72, true, Menu, ESSXPromptDevice::Keyboard), FString(TEXT("key/ctrl")));
	TestEqual(TEXT("circle is F while racing"), ImageFor(0x72, true, Race, ESSXPromptDevice::Keyboard), FString(TEXT("key/f")));
	TestEqual(TEXT("L2 is Z while racing"), ImageFor(0x6F, true, Race, ESSXPromptDevice::Keyboard), FString(TEXT("key/z")));
	TestTrue(TEXT("lessons keep the PS2 glyph"), ImageFor(0x74, true, ESSXPromptContext::Original, ESSXPromptDevice::Xbox).IsEmpty());
	TestTrue(TEXT("PS2 device keeps the glyph"), ImageFor(0x74, true, Race, ESSXPromptDevice::PS2).IsEmpty());
	TestTrue(TEXT("the whole d-pad keeps the glyph"), ImageFor(0x61, true, Race, ESSXPromptDevice::Xbox).IsEmpty());
	TestTrue(TEXT("a non-button FE sprite keeps the original"), ImageFor(61, false, Menu, ESSXPromptDevice::Xbox).IsEmpty());

	// Every image a mapping names exists.
	for (const ESSXPromptDevice Dev : { ESSXPromptDevice::Keyboard, ESSXPromptDevice::Xbox, ESSXPromptDevice::PS4, ESSXPromptDevice::PS5 })
	{
		for (const ESSXPromptContext Ctx : { Menu, Race })
		{
			for (int32 Id = 0x61; Id <= 0x75; ++Id)
			{
				const FString Rel = ImageFor(Id, true, Ctx, Dev);
				if (!Rel.IsEmpty())
				{
					TestTrue(FString::Printf(TEXT("%s exists"), *Rel), FPaths::FileExists(FPaths::Combine(SSXPrompts::Dir(), Rel + TEXT(".png"))));
				}
			}
		}
	}

	// Through a draw list: the prompt replaces the texture and is fitted, centred, in the original box.
	FSSXSpriteTable Hud;
	FSSXSprite Rec;
	Rec.Page = TEXT("hud");
	Rec.W = 28.f;
	Rec.H = 15.f;
	Hud.Sprites.Add(0x6E, Rec);
	FSSXFeAssets Assets;
	Assets.HudSprites = &Hud;
	UTexture* Page = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	Assets.Pages.Add(TEXT("hud"), Page);
	FPromptCvar Cvar;
	Cvar.Set(3);
	FSSXFeDrawList D(&Assets);
	{
		const FSSXPromptScope Race_(D, Race);
		TestTrue(TEXT("L1 drawn"), D.Sprite(0x6E, 100.f, 50.f, -1.f, -1.f, FLinearColor::White, 0, true));
	}
	TestTrue(TEXT("scope restores the menu context"), D.PromptContext == Menu);
	{
		const FSSXPromptScope Lesson(D, ESSXPromptContext::Original);
		D.Sprite(0x6E, 100.f, 50.f, -1.f, -1.f, FLinearColor::White, 0, true);
	}
	const TArray<FSSXFeDrawCmd>& Cmds = D.Cmds;
	if (TestEqual(TEXT("two commands"), Cmds.Num(), 2))
	{
		const FSSXFeDrawCmd& P = Cmds[0];
		TestTrue(TEXT("prompt texture"), P.Texture != Page && P.Texture != nullptr);
		TestTrue(TEXT("fitted inside 28 x 15"), P.Size.X <= 28.f + 1e-3f && P.Size.Y <= 15.f + 1e-3f && (FMath::IsNearlyEqual(P.Size.X, 28.f) || FMath::IsNearlyEqual(P.Size.Y, 15.f)));
		TestTrue(TEXT("centred"), FMath::IsNearlyEqual(P.Pos.X + P.Size.X * 0.5f, 114.f) && FMath::IsNearlyEqual(P.Pos.Y + P.Size.Y * 0.5f, 57.5f));
		TestTrue(TEXT("lesson keeps the original page"), Cmds[1].Texture == Page && Cmds[1].Size == FVector2f(28.f, 15.f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIWidescreenTest, "SSX.UI.Widescreen", PromptFlags)
bool FSSXUIWidescreenTest::RunTest(const FString&)
{
	TestEqual(TEXT("left third moves out"), SSXFeWide::AnchorX(20.f, 106.f), -86.f);
	TestEqual(TEXT("right third moves out"), SSXFeWide::AnchorX(600.f, 106.f), 706.f);
	TestEqual(TEXT("centre stays"), SSXFeWide::AnchorX(320.f, 106.f), 320.f);
	TestEqual(TEXT("16:9 image, 16:9 window: 106.67 past each side"), SSXFeWide::VisibleExtra(16.f / 9.f, 16.f / 9.f), 320.f * 4.f / 3.f - 320.f, 1e-3f);
	TestEqual(TEXT("16:9 image in a 21:9 window: limited by the image"), SSXFeWide::VisibleExtra(16.f / 9.f, 21.f / 9.f), 320.f * 4.f / 3.f - 320.f, 1e-3f);
	TestEqual(TEXT("4:3 image: none"), SSXFeWide::VisibleExtra(4.f / 3.f, 16.f / 9.f), 0.f);
	TestFalse(TEXT("Classic look: the original's stretched frame"), SSXFeWide::Anchored());
	SSXFeWide::SetImageAspect(4.f / 3.f);
	TestEqual(TEXT("no stretch with a 4:3 image"), SSXFeWide::StretchX(), 1.f);
	TestEqual(TEXT("4:3 image: no extra width"), SSXFeWide::ExtraX(), 0.f);
	FVector2f P0(-50.f, 10.f), P1(30.f, 20.f);
	FVector2f Uv[4] = { FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), FVector2f(0, 1) };
	TestTrue(TEXT("4:3: clipped at the frame edge"), FSSXFeDrawList::ClipToScreen(P0, P1, Uv) && P0.X == 0.f);
	return true;
}

#endif
