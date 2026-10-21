#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeOptions.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "World/SSXRenderLook.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags OptTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	const FSSXLocStrings& OptLoc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	struct FOptAssets
	{
		FSSXFeAssets A;
		FSSXSpriteTable Sprites;
		bool bFonts = false, bSprites = false;
		FOptAssets()
		{
			UTexture2D* Dummy = NewObject<UTexture2D>(GetTransientPackage());
			Dummy->AddToRoot();
			auto Font = [Dummy](const TCHAR* Name, float Sx, float Sy) -> TSharedPtr<FSSXBitmapFont>
			{
				const FString Base = FPaths::Combine(SSXFrontEnd::FontDir(), Name);
				if (!FPaths::FileExists(Base + TEXT(".json"))) return nullptr;
				TSharedPtr<FSSXBitmapFont> F = FSSXBitmapFont::Load(Base + TEXT(".json"), Dummy,
					FPaths::Combine(SSXFrontEnd::SfnDir(), FString(Name) + TEXT(".SFN")));
				if (F.IsValid()) { F->ScaleX = Sx; F->ScaleY = Sy; }
				return F;
			};
			A.Title = Font(TEXT("TITLE"), SSXFe::TitleScaleX, SSXFe::TitleScaleY);
			A.Menu = Font(TEXT("MENU"), SSXFe::MenuScaleX, SSXFe::MenuScaleY);
			bFonts = A.Title.IsValid() && A.Menu.IsValid();
			bSprites = Sprites.LoadJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_sprites.json")));
			if (bSprites)
			{
				A.FeSprites = &Sprites;
				for (const TPair<int32, FSSXSprite>& P : Sprites.Sprites) A.Pages.Add(P.Value.Page, Dummy);
			}
		}
		static FOptAssets& Get() { static FOptAssets S; return S; }
		bool Ready() const { return bFonts && bSprites && OptLoc().IsLoaded(); }
	};

	struct FOptFe
	{
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FSSXFeDrawList D;
		FOptFe() : D(&FOptAssets::Get().A)
		{
			SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
			FE.Init(&Profile, &Cheats, &OptLoc(), false);
		}
		void Settle() { for (int32 N = 0; !FE.IsReady() && FE.Boot == ESSXFeBoot::FrontEnd && N < 3000; ++N) FE.Tick(); }
		void Press(ESSXFeKey K) { Settle(); FE.Key(0, K); FE.Tick(); Settle(); }
		void Redraw() { D.Reset(); FE.Draw(D); }
		FString L(int32 Id) const { return OptLoc().ByIndex(Id); }
		const FSSXFeTextRec* Txt(const FString& S, int32 Nth = 0) const
		{
			for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == S && Nth-- == 0) return &R; }
			return nullptr;
		}
		const FSSXFeDrawCmd* Spr(int32 Id, int32 Nth = 0) const
		{
			for (const FSSXFeDrawCmd& C : D.Cmds) { if (C.SpriteId == Id && Nth-- == 0) return &C; }
			return nullptr;
		}
		int32 SprCount(int32 Id) const { int32 N = 0; for (const FSSXFeDrawCmd& C : D.Cmds) N += C.SpriteId == Id; return N; }
		float W(const FString& S, float Scale = 1.f) const { return D.TextBoxWidth(SSXFe::EFont::Menu, S, Scale) + SSXFe::ShadowOffset; }
		void OpenPage(int32 P)
		{
			Press(ESSXFeKey::Square);
			for (int32 I = 0; I < P; ++I) Press(ESSXFeKey::Down);
			Press(ESSXFeKey::Cross);
		}
		bool HasHook(const TCHAR* H) const { return FE.SoundHooks.Contains(FString(H)); }
	};

	void Near(FAutomationTestBase& T, const FString& What, float Got, float Want, float Tol = 0.51f)
	{
		T.AddInfo(FString::Printf(TEXT("%s: port %.2f model %.2f"), *What, Got, Want));
		T.TestTrue(What, FMath::Abs(Got - Want) <= Tol);
	}
	bool SameColor(const FLinearColor& A, const FLinearColor& B) { return A.Equals(B, 1e-3f); }

	constexpr float CellH = 17.f;
	float RowTop(int32 Rows) { return 18.f + (408.f - Rows * CellH) * 0.5f; }
	constexpr float PagePitch = 23.f;
	constexpr float EntryX = 320.f, ValueX = 335.f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIOptionsPagesTest, "SSX.UI.OptionsPages", OptTestFlags)
bool FSSXUIOptionsPagesTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeOptions;
	using namespace SSXFe;
	if (!FOptAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; Options layout checks skipped")); return true; }
	{
		FOptFe F;
		F.Press(ESSXFeKey::Square);
		F.Redraw();
		TestEqual(TEXT("top list help = 3287"), F.FE.CurrentHelpLine(), F.L(3287));
		const FSSXFeDrawCmd* X = F.Spr(IconCross);
		TestTrue(TEXT("top list: select legend enabled (white)"), X && SameColor(X->Color, White));
		TestNull(TEXT("no page title on the top list"), F.Txt(F.L(3608), 1));
	}
	{
		FOptFe F;
		F.Profile.Settings.Speaker = 2;
		F.OpenPage(0);
		const FSSXFeState* S = F.FE.Top();
		if (!TestTrue(TEXT("Sound page open"), S && S->Page == 0)) return false;
		TestTrue(TEXT("speech request 0x26 on entry"), F.HasHook(TEXT("speech38")));
		F.Redraw();
		const FSSXFeTextRec* T = F.Txt(F.L(3608));
		if (TestNotNull(TEXT("page title 'Sound'"), T))
		{
			const float Tw = F.D.TextBoxWidth(SSXFe::EFont::Title, F.L(3608), 1.f) + SSXFe::ShadowOffset;
			Near(*this, TEXT("Sound title centre x"), T->Pos.X + Tw * 0.5f, 320.f);
			Near(*this, TEXT("Sound title y"), T->Pos.Y, 60.f);
			TestTrue(TEXT("Sound title white"), SameColor(T->Color, White));
		}
		const TArray<FRow>& Rows = PageRows(0);
		const float Top = RowTop(Rows.Num());
		Near(*this, TEXT("Sound first row top (145.5)"), Top, 145.5f, 0.01f);
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			const FSSXFeTextRec* Lb = F.Txt(F.L(Rows[I].Loc));
			if (!TestNotNull(*FString::Printf(TEXT("row %d label"), I), Lb)) continue;
			Near(*this, FString::Printf(TEXT("row %d label right edge"), I), Lb->Pos.X + F.W(Lb->Str), EntryX);
			Near(*this, FString::Printf(TEXT("row %d y"), I), Lb->Pos.Y, Top + I * PagePitch);
			TestTrue(FString::Printf(TEXT("row %d colour"), I), SameColor(Lb->Color, I == 0 ? Gold : White));
		}
		const FSSXFeTextRec* Sur = F.Txt(F.L(229));
		if (TestNotNull(TEXT("speaker value 'Surround'"), Sur))
		{
			Near(*this, TEXT("value x"), Sur->Pos.X, ValueX);
			Near(*this, TEXT("value y"), Sur->Pos.Y, Top);
			TestTrue(TEXT("selected row value gold"), SameColor(Sur->Color, Gold));
			const FSSXFeDrawCmd* AL = F.Spr(SprArrowL);
			const FSSXFeDrawCmd* AR = F.Spr(SprArrowR);
			if (TestNotNull(TEXT("left arrow"), AL) && TestNotNull(TEXT("right arrow"), AR))
			{
				Near(*this, TEXT("left arrow x (335 - 9 - 5)"), AL->Pos.X, 321.f);
				Near(*this, TEXT("arrow y (row centre - 4.5)"), AL->Pos.Y, Top + 8.5f - 4.5f);
				Near(*this, TEXT("arrow size 9"), AL->Size.X, 9.f, 0.01f);
				Near(*this, TEXT("right arrow x (value right + 5)"), AR->Pos.X, ValueX + F.W(Sur->Str) + 5.f);
			}
		}
		TestEqual(TEXT("one arrow pair (highlighted row only)"), F.SprCount(SprArrowL), 1);
		TestNotNull(TEXT("preset value 'Tasty Mix'"), F.Txt(F.L(230)));
		// 0x32CA04
		TestNotNull(TEXT("arcade value 'On' (start-up settings: arcade 1)"), F.Txt(F.L(215)));
		TestEqual(TEXT("five slider tracks"), F.SprCount(SprSliderTrack), 5);
		for (int32 B = 0; B < 5; ++B)
		{
			const FSSXFeDrawCmd* Tr = F.Spr(SprSliderTrack, B);
			if (!Tr) continue;
			Near(*this, FString::Printf(TEXT("slider %d x"), B), Tr->Pos.X, ValueX);
			Near(*this, FString::Printf(TEXT("slider %d y"), B), Tr->Pos.Y, Top + (B + 2) * PagePitch);
			Near(*this, FString::Printf(TEXT("slider %d w"), B), Tr->Size.X, 150.f, 0.01f);
			Near(*this, FString::Printf(TEXT("slider %d h"), B), Tr->Size.Y, 15.f, 0.01f);
		}
		const FSSXFeDrawCmd* Fill = F.Spr(SprSliderFill);
		if (TestNotNull(TEXT("master fill"), Fill))
		{
			Near(*this, TEXT("master fill width = 150 x master"), Fill->Size.X, 150.f * F.Profile.Settings.Master, 0.01f);
		}
		TestEqual(TEXT("help: speaker 3288"), F.FE.CurrentHelpLine(), F.L(3288));
		const FSSXFeDrawCmd* X = F.Spr(IconCross);
		TestTrue(TEXT("select legend greyed on a value row"), X && SameColor(X->Color, Grey));
		TestTrue(TEXT("previous legend white"), F.Spr(IconTriangle) && SameColor(F.Spr(IconTriangle)->Color, White));

		F.FE.SoundHooks.Reset();
		F.Press(ESSXFeKey::Right);
		TestEqual(TEXT("Right: speaker DTS (3)"), F.Profile.Settings.Speaker, 3);
		TestEqual(TEXT("Right: sounds ui4 + ui16"), F.FE.SoundHooks, TArray<FString>({ TEXT("ui4"), TEXT("ui16") }));
		F.Redraw();
		TestTrue(TEXT("right arrow flashes (not white)"), F.Spr(SprArrowR) && !SameColor(F.Spr(SprArrowR)->Color, White));
		F.Press(ESSXFeKey::Right);
		TestEqual(TEXT("Right wraps to Mono (0)"), F.Profile.Settings.Speaker, 0);
		F.Press(ESSXFeKey::Up);
		TestEqual(TEXT("Up wraps to DONE"), F.FE.Top()->Opt.RowSel[0], Rows.Num() - 1);
		F.Redraw();
		TestEqual(TEXT("DONE help 3534"), F.FE.CurrentHelpLine(), F.L(3534));
		TestTrue(TEXT("DONE: select legend white"), F.Spr(IconCross) && SameColor(F.Spr(IconCross)->Color, White));
		const FSSXFeTextRec* Done = F.Txt(F.L(3533));
		if (TestNotNull(TEXT("DONE"), Done))
		{
			Near(*this, TEXT("DONE right edge"), Done->Pos.X + F.W(Done->Str), EntryX);
			TestTrue(TEXT("DONE gold"), SameColor(Done->Color, Gold));
		}
		F.Press(ESSXFeKey::Down); F.Press(ESSXFeKey::Down); F.Press(ESSXFeKey::Down);
		F.FE.SoundHooks.Reset();
		F.Press(ESSXFeKey::Right);
		TestTrue(TEXT("master clamps at 1"), FMath::IsNearlyEqual(F.Profile.Settings.Master, 1.f));
		TestEqual(TEXT("bar change plays 0x12 (no move sound)"), F.FE.SoundHooks, TArray<FString>({ TEXT("ui18") }));
		F.Redraw();
		TestNull(TEXT("no right arrow at the max"), F.Spr(SprArrowR));
		TestNotNull(TEXT("left arrow at the max"), F.Spr(SprArrowL));
		F.Press(ESSXFeKey::Left);
		TestTrue(TEXT("master 10/11"), FMath::IsNearlyEqual(F.Profile.Settings.Master, 10.f / 11.f, 1e-5f));
		F.FE.SoundHooks.Reset();
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("X on a bar row: still on the page, no sound"), F.FE.Top()->Page * 10 + F.FE.SoundHooks.Num(), 0);
		F.Press(ESSXFeKey::Up);
		F.Press(ESSXFeKey::Right); F.Press(ESSXFeKey::Right);
		TestEqual(TEXT("preset No Music (2)"), F.Profile.Settings.Preset, 2);
		TestTrue(TEXT("No Music: music 0"), FMath::IsNearlyZero(F.Profile.Settings.Music));
		TestTrue(TEXT("No Music: sfx 10/11"), FMath::IsNearlyEqual(F.Profile.Settings.Sfx, 10.f / 11.f, 1e-5f));
		TestTrue(TEXT("bars re-read"), FMath::IsNearlyZero(F.FE.Top()->Opt.Bar[0][5]));
		F.Press(ESSXFeKey::Triangle);
		TestEqual(TEXT("Back: top list"), F.FE.Top()->Page, -1);
		TestEqual(TEXT("Back restores speaker"), F.Profile.Settings.Speaker, 2);
		TestTrue(TEXT("Back restores master (start-up 10/11)"), FMath::IsNearlyEqual(F.Profile.Settings.Master, 10.f / 11.f, 1e-5f));
		TestTrue(TEXT("Back: speech request 0x24"), F.HasHook(TEXT("speech36")));
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("page menu keeps its row"), F.FE.Top()->Opt.RowSel[0], 1);
		F.Press(ESSXFeKey::Right);
		for (int32 I = 0; I < 7; ++I) F.Press(ESSXFeKey::Down);
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("DONE: top list"), F.FE.Top()->Page, -1);
		TestEqual(TEXT("DONE keeps preset No Speech"), F.Profile.Settings.Preset, 1);
		TestTrue(TEXT("No Speech: mc 0"), FMath::IsNearlyZero(F.Profile.Settings.McSpeech));
	}
	{
		FOptFe F;
		F.OpenPage(2);
		F.Redraw();
		const TArray<FRow>& Rows = PageRows(2);
		const float Top = RowTop(Rows.Num());
		Near(*this, TEXT("Game first row top (162.5)"), Top, 162.5f, 0.01f);
		TestNotNull(TEXT("title 'Game Options'"), F.Txt(F.L(186)));
		TestNotNull(TEXT("speed units MPH (Units 1)"), F.Txt(F.L(205)));
		TestNotNull(TEXT("max boarders '6'"), F.Txt(TEXT("6")));
		const FSSXFeDrawCmd* Br = F.Spr(SprSliderTrack);
		if (TestNotNull(TEXT("brightness slider"), Br))
		{
			Near(*this, TEXT("brightness x"), Br->Pos.X, ValueX);
			Near(*this, TEXT("brightness y (row 5)"), Br->Pos.Y, Top + 5 * PagePitch);
			Near(*this, TEXT("brightness width 100"), Br->Size.X, 100.f, 0.01f);
		}
		TestEqual(TEXT("help: speed 3303"), F.FE.CurrentHelpLine(), F.L(3303));
		F.Press(ESSXFeKey::Left);   // FUN_002A99A0
		TestEqual(TEXT("Km/h on change writes 1 (original FUN_002A99A0)"), F.Profile.Settings.Units, 1);
		for (int32 I = 0; I < 5; ++I) F.Press(ESSXFeKey::Down);
		const float B0 = F.Profile.Settings.Brightness;
		F.Press(ESSXFeKey::Right);
		TestTrue(TEXT("brightness step 0.02"), FMath::IsNearlyEqual(F.Profile.Settings.Brightness, B0 + 0.02f, 1e-5f));
		F.Press(ESSXFeKey::Down);
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("DONE commits Km/h (Units 0)"), F.Profile.Settings.Units, 0);
		TestTrue(TEXT("DONE keeps brightness"), FMath::IsNearlyEqual(F.Profile.Settings.Brightness, B0 + 0.02f, 1e-5f));
	}
	{
		FOptFe F;
		F.OpenPage(3);
		F.Press(ESSXFeKey::Down);
		F.Redraw();
		const float Top = RowTop(3);
		Near(*this, TEXT("Save/Load first row top (196.5)"), Top, 196.5f, 0.01f);
		const int32 Locs[3] = { 222, 198, 199 };
		for (int32 I = 0; I < 3; ++I)
		{
			const FSSXFeTextRec* R = F.Txt(F.L(Locs[I]));
			if (!TestNotNull(*FString::Printf(TEXT("save/load item %d"), I), R)) continue;
			Near(*this, FString::Printf(TEXT("item %d centred"), I), R->Pos.X + F.W(R->Str) * 0.5f, 320.f);
			Near(*this, FString::Printf(TEXT("item %d y"), I), R->Pos.Y, Top + I * PagePitch);
			TestTrue(FString::Printf(TEXT("item %d colour"), I), SameColor(R->Color, I == 1 ? Gold : White));
		}
		TestNull(TEXT("no DONE on Save/Load"), F.Txt(F.L(3533)));
		TestEqual(TEXT("help: load 3312"), F.FE.CurrentHelpLine(), F.L(3312));
		TestTrue(TEXT("select legend enabled"), F.Spr(IconCross) && SameColor(F.Spr(IconCross)->Color, White));
	}
	{
		FOptFe F;
		F.OpenPage(1);
		F.Redraw();
		const FSSXFeDrawCmd* Body = F.Spr(61);
		if (TestNotNull(TEXT("controller body sprite 61"), Body))
		{
			Near(*this, TEXT("body x (640 - 204.8) / 2"), Body->Pos.X, 217.6f);
			Near(*this, TEXT("body y 115"), Body->Pos.Y, 115.f);
			Near(*this, TEXT("body w 256 x 0.8"), Body->Size.X, 204.8f, 0.01f);
		}
		TestEqual(TEXT("Default config labels shown (no function)"), F.Txt(F.L(3554)) != nullptr, true);
		TestNull(TEXT("Pro labels hidden (combat cam)"), F.Txt(F.L(3214)));
		const FSSXFeTextRec* V1 = F.Txt(F.L(235));
		const float MenuW = 15.f + F.W(F.L(183));
		if (TestNotNull(TEXT("vibration 1P"), V1))
		{
			Near(*this, TEXT("page 1 entry x = 320 - width / 2"), V1->Pos.X + F.W(V1->Str), 320.f - MenuW * 0.5f);
			Near(*this, TEXT("page 1 y 290"), V1->Pos.Y, 290.f);
		}
		const FSSXFeTextRec* V2 = F.Txt(F.L(236));
		if (V2) Near(*this, TEXT("page 1 pitch 19 (spacing 2)"), V2->Pos.Y, 290.f + 19.f);
		F.Press(ESSXFeKey::Down); F.Press(ESSXFeKey::Down);
		F.Press(ESSXFeKey::Right);
		F.Redraw();
		TestNotNull(TEXT("Pro labels shown"), F.Txt(F.L(3214)));
		F.Press(ESSXFeKey::Down); F.Press(ESSXFeKey::Down);
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("DONE: controller 1P Pro"), F.Profile.Settings.Controller[0], 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIOptionsVideoTest, "SSX.UI.OptionsVideo", OptTestFlags)
bool FSSXUIOptionsVideoTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeOptions;
	if (!FOptAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; Video page checks skipped")); return true; }
	const ESSXRenderLook Prev = SSXRenderLook::Get();
	const int32 PrevQuality = SSXRenderLook::GetQuality();
	ON_SCOPE_EXIT { SSXRenderLook::Set(Prev); SSXRenderLook::SetQuality(PrevQuality); };
	SSXRenderLook::Set(ESSXRenderLook::Classic);
	SSXRenderLook::SetQuality(2);
	{
		FOptFe F;
		F.Press(ESSXFeKey::Square);
		const FSSXFeState* S = F.FE.Top();
		if (!TestTrue(TEXT("Options top list: the original five + Video"), S && S->Items.Num() == 6)) return false;
		TestEqual(TEXT("item 5 text"), S->Items[5].Text, FString(TEXT("VIDEO")));
		TestEqual(TEXT("item 5 id"), S->Items[5].Id, VideoPage);
		for (int32 I = 0; I < 5; ++I) { TestEqual(FString::Printf(TEXT("original item %d help"), I), S->Items[I].HelpLoc, HelpLoc(-1, I)); }
	}
	{
		FOptFe F;
		F.OpenPage(VideoPage);
		const FSSXFeState* S = F.FE.Top();
		if (!TestTrue(TEXT("Video page open"), S && S->Page == VideoPage)) return false;
		F.Redraw();
		TestNotNull(TEXT("title VIDEO"), F.Txt(TEXT("VIDEO")));
		TestNotNull(TEXT("row LOOK"), F.Txt(TEXT("LOOK")));
		TestNotNull(TEXT("value CLASSIC"), F.Txt(TEXT("CLASSIC")));
		TestNotNull(TEXT("row RESOLUTION"), F.Txt(TEXT("RESOLUTION")));
		TestNotNull(TEXT("row DISPLAY"), F.Txt(TEXT("DISPLAY")));
		TestEqual(TEXT("FRAME GENERATION row only with the FSR plugin"), F.Txt(TEXT("FRAME GENERATION")) != nullptr,
			SSXRenderLook::FrameGenerationAvailable());
		const TArray<FIntPoint>& Res = Resolutions();
		TestTrue(TEXT("resolutions listed, smallest first"), Res.Num() > 0 && Res[0].X <= Res.Last().X);
		TestEqual(TEXT("the nearest listed resolution"), Res[ResolutionIndex(Res.Last())], Res.Last());
		TestEqual(TEXT("help line"), F.FE.CurrentHelpLine(), FString(HelpText(VideoPage, 0)));
		F.Press(ESSXFeKey::Right);
		TestEqual(TEXT("Right previews Remastered"), int32(SSXRenderLook::Get()), int32(ESSXRenderLook::Remastered));
		F.Redraw();
		TestNotNull(TEXT("value REMASTERED"), F.Txt(TEXT("REMASTERED")));
		F.Press(ESSXFeKey::Triangle);
		TestEqual(TEXT("Back restores Classic"), int32(SSXRenderLook::Get()), int32(ESSXRenderLook::Classic));
		TestEqual(TEXT("Back leaves the page"), F.FE.Top()->Page, -1);
	}
	{
		FOptFe F;
		F.OpenPage(VideoPage);
		F.Press(ESSXFeKey::Right);
		F.Press(ESSXFeKey::Down);
		F.Press(ESSXFeKey::Left);
		TestEqual(TEXT("QUALITY Left previews Medium"), SSXRenderLook::GetQuality(), 1);
		// down from QUALITY to DONE (FRAME GENERATION is there only with the FSR plugin)
		for (int32 I = 1; I < RowIndex(VideoPage, DoneId); ++I) { F.Press(ESSXFeKey::Down); }
		F.FE.SoundHooks.Reset();
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("DONE keeps Remastered"), int32(SSXRenderLook::Get()), int32(ESSXRenderLook::Remastered));
		TestTrue(TEXT("DONE asks to save the Video settings"), F.HasHook(TEXT("saveVideo")));
		if (SSXRenderLook::FrameGenerationAvailable())
		{
			// the setting outranks the off default in DefaultEngine.ini
			SSXRenderLook::SetFrameGeneration(true);
			TestTrue(TEXT("frame generation stays on"), SSXRenderLook::GetFrameGeneration());
			SSXRenderLook::SetFrameGeneration(false);
			TestFalse(TEXT("frame generation turns off"), SSXRenderLook::GetFrameGeneration());
		}
		TestEqual(TEXT("DONE leaves the page"), F.FE.Top()->Page, -1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUICreditsTest, "SSX.UI.Credits", OptTestFlags)
bool FSSXUICreditsTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeOptions;
	using namespace SSXFe;
	{
		FSSXFeScrollText T;
		T.Reset(300.f);
		TestEqual(TEXT("16 slots (300 / 18)"), T.Slots.Num(), 16);
		TestTrue(TEXT("pitch 18"), FMath::IsNearlyEqual(T.Pitch(), 18.f));
		int32 Frames = 0;
		while (T.Slots.Last() == -1 && Frames < 1000) { T.Tick(); ++Frames; }
		AddInfo(FString::Printf(TEXT("first line after %d frames (0.3 px / frame, pitch 18)"), Frames));
		TestTrue(TEXT("first line enters after ~60 frames"), Frames >= 60 && Frames <= 62);
		TestEqual(TEXT("first line = LOC 2143 in the bottom slot"), T.Slots.Last(), 2143);
		TestTrue(TEXT("offset wrapped below 1 px"), T.Offset < 1.f);
		TestTrue(TEXT("bottom slot fades in (offset / 15)"), FMath::IsNearlyEqual(T.SlotAlpha(15), T.Offset / 15.f));
		for (int32 I = 0; I < 15 * 60 + 5; ++I) T.Tick();
		TestEqual(TEXT("after 15 pitches the first line is in slot 0"), T.Slots[0], 2143);
		TestTrue(TEXT("top slot fades out"), FMath::IsNearlyEqual(T.SlotAlpha(0), 1.f - T.Offset / 18.f));
		int32 Guard = 0;
		bool bRestarted = false;
		int32 MaxCur = 0;
		while (Guard++ < 80000)
		{
			T.Tick();
			MaxCur = FMath::Max(MaxCur, T.Cur);
			if (MaxCur > T.Last && T.Cur == T.First) { bRestarted = true; break; }
		}
		TestTrue(TEXT("every line up to 2986 shown, then restart from 2143"), bRestarted && MaxCur == T.Last + 1);
		FSSXFeScrollText K;
		K.Key(true);
		TestTrue(TEXT("Up: 0.4"), FMath::IsNearlyEqual(K.Speed, 0.4f));
		for (int32 I = 0; I < 10; ++I) K.Key(false);
		TestTrue(TEXT("Down clamps at 0.1"), FMath::IsNearlyEqual(K.Speed, 0.1f));
		for (int32 I = 0; I < 100; ++I) K.Key(true);
		TestTrue(TEXT("Up clamps at 15 x 0.35"), FMath::IsNearlyEqual(K.Speed, 5.25f, 1e-4f));
	}
	FOptFe F;
	F.Press(ESSXFeKey::Square);
	for (int32 I = 0; I < 4; ++I) F.Press(ESSXFeKey::Down);
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Cross);
	const FSSXFeState* S = F.FE.Top();
	if (!TestTrue(TEXT("credits page"), S && S->Page == 4)) return false;
	TestTrue(TEXT("credits active"), S->Opt.bCreditsActive);
	TestTrue(TEXT("speech request 0x29, no music change"), F.HasHook(TEXT("speech41")) && !F.FE.SoundHooks.ContainsByPredicate(
		[](const FString& H) { return H.StartsWith(TEXT("music")); }));
	TestEqual(TEXT("credits help line empty"), F.FE.CurrentHelpLine(), FString());
	for (int32 I = 0; I < 100 && S->Opt.Credits.Slots.Last() == -1; ++I) F.FE.Tick();
	TestEqual(TEXT("page ticks the scroller"), S->Opt.Credits.Slots.Last(), 2143);
	if (FOptAssets::Get().Ready())
	{
		F.Redraw();
		TestNotNull(TEXT("page title 'Credits'"), F.Txt(F.L(180)));
		const FString Head = F.L(2143).RightChop(1);
		const FSSXFeTextRec* R = F.Txt(Head);
		if (TestNotNull(TEXT("heading drawn"), R))
		{
			const FSSXFeScrollText& T = S->Opt.Credits;
			Near(*this, TEXT("heading y = 105 - offset + 15 x 18"), R->Pos.Y, 105.f - T.Offset + 15.f * 18.f, 0.01f);
			const float Ink = float(int32(F.D.TextInkWidth(SSXFe::EFont::Menu, Head, 1.f)));
			Near(*this, TEXT("heading centred on 320 (ink width)"), R->Pos.X, 320.f - Ink * 0.5f, 0.01f);
			TestTrue(TEXT("heading gold"), FMath::IsNearlyEqual(R->Color.R, Gold.R) && FMath::IsNearlyEqual(R->Color.G, Gold.G));
			TestTrue(TEXT("heading alpha = bottom-slot fade"), FMath::IsNearlyEqual(R->Color.A, T.SlotAlpha(15)));
			TestTrue(TEXT("heading shadowed"), R->bShadow);
		}
		TestTrue(TEXT("select legend greyed"), F.Spr(IconCross) && SameColor(F.Spr(IconCross)->Color, Grey));
	}
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Cross);
	TestEqual(TEXT("X: no sound, still credits"), F.FE.SoundHooks.Num() + (F.FE.Top()->Page == 4 ? 0 : 100), 0);
	F.Press(ESSXFeKey::Up);
	TestTrue(TEXT("Up: faster"), FMath::IsNearlyEqual(F.FE.Top()->Opt.Credits.Speed, 0.4f));
	F.Press(ESSXFeKey::Triangle);
	TestEqual(TEXT("Triangle: top list"), F.FE.Top()->Page, -1);
	TestFalse(TEXT("scroller stopped"), F.FE.Top()->Opt.bCreditsActive);
	TestTrue(TEXT("back sound + speech 0x24"), F.HasHook(TEXT("ui6")) && F.HasHook(TEXT("speech36")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIRiderProfileTest, "SSX.UI.RiderProfile", OptTestFlags)
bool FSSXUIRiderProfileTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeRiderProfile;
	using namespace SSXFe;
	FOptFe F;
	F.FE.GoTo(ESSXFeScreen::RiderProfile);
	F.Settle();
	const FSSXFeState* S = F.FE.Top();
	if (!TestTrue(TEXT("RiderProfile"), S && S->Screen == ESSXFeScreen::RiderProfile)) return false;
	const int32 Char = FMath::Clamp(F.Profile.Slot[0].Char, 0, 11);
	TestEqual(TEXT("legend: previous only (mask 8)"), (int32)S->HelpMask, 0x8);
	TestEqual(TEXT("help box 420"), S->HelpW, 420.f);
	TestEqual(TEXT("Bio help 3263"), F.FE.CurrentHelpLine(), F.L(3263));
	TestEqual(TEXT("BackStory range (Eddie 602..615)"), S->ProfileText[0].First, Char == 0 ? 602 : S->ProfileText[0].First);
	TestEqual(TEXT("13 text slots (260 / 20)"), S->ProfileText[0].Slots.Num(), 13);
	if (!FOptAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; RiderProfile draw checks skipped")); return true; }
	F.Redraw();
	const FSSXFeDrawCmd* Panel = nullptr;
	for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (!C.Texture && C.SpriteId < 0 && C.Color.Equals(RiderProfilePanel)) { Panel = &C; break; } }
	if (TestNotNull(TEXT("panel"), Panel))
	{
		Near(*this, TEXT("panel x"), Panel->Pos.X, 180.f, 0.01f);
		Near(*this, TEXT("panel w"), Panel->Size.X, 460.f, 0.01f);
		TestTrue(TEXT("panel drawn as a GS untextured primitive (x 128 / 255 = RGB 80, 108, 105)"), Panel->bGsUntextured);
	}
	const FSSXFeDrawCmd* Por = F.Spr(0x7C);
	if (TestNotNull(TEXT("Bio portrait sprite 0x7C (rpb1)"), Por))
	{
		Near(*this, TEXT("portrait x"), Por->Pos.X, 0.f, 0.01f);
		Near(*this, TEXT("portrait y"), Por->Pos.Y, 58.f, 0.01f);
		Near(*this, TEXT("portrait w"), Por->Size.X, 180.f, 0.01f);
		Near(*this, TEXT("portrait h"), Por->Size.Y, 364.f, 0.01f);
	}
	TestTrue(TEXT("jagged edge sprite 40 at x 167"), F.Spr(SprEdge) && FMath::IsNearlyEqual(F.Spr(SprEdge)->Pos.X, 167.f));
	TestNull(TEXT("no play-video legend (X icon)"), F.Spr(IconCross));
	TestNotNull(TEXT("previous legend (Triangle icon)"), F.Spr(IconTriangle));
	for (int32 K = 0; K < 15; K += 14)
	{
		const FSSXFeTextRec* Lb = F.Txt(F.L(586 + K));
		const FSSXFeTextRec* Va = F.Txt(F.L(BioValueLoc(Char, K)));
		if (!TestNotNull(*FString::Printf(TEXT("Bio label %d"), K), Lb) || !TestNotNull(*FString::Printf(TEXT("Bio value %d"), K), Va)) continue;
		Near(*this, FString::Printf(TEXT("Bio label %d right edge 350"), K), Lb->Pos.X + F.D.TextBoxWidth(EFont::Menu, Lb->Str, 0.8f), 350.f);
		Near(*this, FString::Printf(TEXT("Bio row %d y (111 + 16 k)"), K), Lb->Pos.Y, 111.f + 16.f * K);
		Near(*this, FString::Printf(TEXT("Bio value %d x 365"), K), Va->Pos.X, 365.f);
		TestTrue(TEXT("Bio text black, no shadow"), SameColor(Lb->Color, Black) && !Lb->bShadow && SameColor(Va->Color, Black));
	}
	F.Press(ESSXFeKey::Right);
	S = F.FE.Top();
	TestEqual(TEXT("BackStory help 3264"), F.FE.CurrentHelpLine(), F.L(3264));
	F.Redraw();
	TestNotNull(TEXT("BackStory portrait sprite 0x7D (rpb2)"), F.Spr(0x7D));
	TestEqual(TEXT("BackStory: previous only"), (int32)S->HelpMask, 0x8);
	for (int32 I = 0; I < 200 && S->ProfileText[0].Slots.Last() == -1; ++I) F.FE.Tick();
	const FSSXFeScrollText& T = S->ProfileText[0];
	TestEqual(TEXT("first BackStory line enters the bottom slot"), T.Slots.Last(), T.First);
	F.Redraw();
	const FSSXFeTextRec* Line = F.Txt(F.L(T.First));
	if (TestNotNull(TEXT("first BackStory line drawn"), Line))
	{
		Near(*this, TEXT("bottom slot y"), Line->Pos.Y, 120.f + 12.f * 20.f - T.Offset, 0.01f);
		Near(*this, TEXT("left aligned at 205"), Line->Pos.X, 205.f, 0.01f);
		TestTrue(TEXT("black, fading in"), FMath::IsNearlyZero(Line->Color.R) && FMath::IsNearlyEqual(Line->Color.A, T.Offset / 15.f));
	}
	F.Press(ESSXFeKey::Right);
	S = F.FE.Top();
	F.Redraw();
	TestEqual(TEXT("WC Status mask 0xC"), (int32)S->HelpMask, 0xC);
	TestTrue(TEXT("WC Status portrait grey"), F.Spr(0x7D) && SameColor(F.Spr(0x7D)->Color, Grey));
	const bool bMaster = SSXFeTables::RankFromPoints(F.Profile.Career[Char].RankPoints()) == 0;
	TestTrue(TEXT("play video greyed unless MASTER"), F.Spr(IconCross) && SameColor(F.Spr(IconCross)->Color, bMaster ? White : Grey));
	F.Press(ESSXFeKey::Right); F.Press(ESSXFeKey::Right);
	S = F.FE.Top();
	TestEqual(TEXT("Interview help 3267"), F.FE.CurrentHelpLine(), F.L(3267));
	for (int32 I = 0; I < 200 && S->ProfileText[1].Slots.Last() == -1; ++I) F.FE.Tick();
	F.Redraw();
	const FString Head = F.L(S->ProfileText[1].First).RightChop(1);
	const FSSXFeTextRec* H = F.Txt(Head);
	if (TestNotNull(TEXT("Interview heading drawn without '~'"), H))
	{
		TestTrue(TEXT("heading white with shadow"), FMath::IsNearlyEqual(H->Color.R, 1.f) && H->bShadow);
	}
	TestNotNull(TEXT("Interview portrait sprite 0x7F (rpb4)"), F.Spr(0x7F));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFeColorScaleTest, "SSX.UI.FeColorScale", OptTestFlags)
bool FSSXUIFeColorScaleTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	FSSXFeDrawList D;
	D.Rect(180.f, 0.f, 460.f, 480.f, RiderProfilePanel, 0);
	const FColor Panel = FSSXFeDrawList::ScreenColor(D.Cmds.Last()).ToFColor(false);
	AddInfo(FString::Printf(TEXT("RiderProfile panel on screen: %d %d %d (capture 80 108 105)"), Panel.R, Panel.G, Panel.B));
	TestTrue(TEXT("panel = the capture (+-1)"), FMath::Abs(Panel.R - 80) <= 1 && FMath::Abs(Panel.G - 108) <= 1 && FMath::Abs(Panel.B - 105) <= 1);
	D.Rect(0.f, 0.f, 640.f, 450.f, OptionsPanel, 0);
	const FLinearColor Backdrop = FSSXFeDrawList::ScreenColor(D.Cmds.Last());
	TestTrue(TEXT("Options backdrop 0.1 x 128 = 12.8 (alpha kept 0.88)"), FMath::IsNearlyEqual(Backdrop.R * 255.f, 12.8f, 0.05f)
		&& FMath::IsNearlyEqual(Backdrop.A, 0.88f));
	D.Rect(0.f, 0.f, 640.f, 480.f, White, 0);
	TestEqual(TEXT("white untextured quad = 128"), (int32)FSSXFeDrawList::ScreenColor(D.Cmds.Last()).ToFColor(false).R, 128);
	D.RectScreen(0.f, 0.f, 640.f, 480.f, White, 0);
	TestEqual(TEXT("host clear unscaled"), (int32)FSSXFeDrawList::ScreenColor(D.Cmds.Last()).ToFColor(false).R, 255);
	FSSXFeDrawCmd Tex;
	Tex.Kind = FSSXFeDrawCmd::EKind::Quad;
	Tex.Texture = reinterpret_cast<UTexture*>(1);
	Tex.Color = White;
	Tex.bGsUntextured = true;
	TestEqual(TEXT("textured quad unscaled"), (int32)FSSXFeDrawList::ScreenColor(Tex).ToFColor(false).R, 255);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIUserNameTest, "SSX.UI.UserName", OptTestFlags)
bool FSSXUIUserNameTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeUserName;
	using namespace SSXFe;
	// 0x2B0850
	TestEqual(TEXT("Left from END -> delete"), StepLeft(41), 40);
	TestEqual(TEXT("Left from column 0 -> row end"), StepLeft(14), 27);
	TestEqual(TEXT("Right from END -> its row start"), StepRight(41), 28);
	TestEqual(TEXT("Right from column 13 -> row start"), StepRight(13), 0);
	TestEqual(TEXT("Up from A -> '3' (row 2)"), StepUp(0), 28);
	TestEqual(TEXT("Up from N -> END"), StepUp(13), 41);
	TestEqual(TEXT("Down from the last row -> row 0"), StepDown(40), 12);
	TestEqual(TEXT("grid 405 x 75 at (117.5, 152)"), FVector4f(GridW, GridH, GridX0, GridY0), FVector4f(405.f, 75.f, 117.5f, 152.f));
	FOptFe F;
	F.FE.GoTo(ESSXFeScreen::UserName);
	F.Settle();
	FSSXFeState* S = F.FE.TopMut();
	if (!TestTrue(TEXT("UserName"), S && S->Screen == ESSXFeScreen::UserName)) return false;
	TestEqual(TEXT("cursor on END"), S->GridY * Cols + S->GridX, 41);
	TestEqual(TEXT("no help line"), F.FE.CurrentHelpLine(), FString());
	if (FOptAssets::Get().Ready())
	{
		F.Redraw();
		const FSSXFeTextRec* Item = F.Txt(F.L(3612));
		TestTrue(TEXT("'User Name' item white (the grid has the focus)"), Item && SameColor(Item->Color, White));
		const FSSXFeTextRec* Name = F.Txt(S->Name);
		if (TestNotNull(TEXT("name drawn"), Name))
		{
			const float Wt = F.D.TextBoxWidth(EFont::Title, S->Name, NameScale);
			Near(*this, TEXT("name in the TITLE font (width)"), Name->Size.X, F.D.TextWidth(EFont::Title, S->Name, NameScale), 0.01f);
			Near(*this, TEXT("name centred (640 - w) / 2"), Name->Pos.X, (640.f - Wt) * 0.5f, 0.01f);
			Near(*this, TEXT("name y 100"), Name->Pos.Y, 100.f, 0.01f);
			TestTrue(TEXT("name gold, no shadow"), SameColor(Name->Color, Gold) && !Name->bShadow);
		}
		const FSSXFeDrawCmd* Box = nullptr;
		for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.bGsUntextured && FMath::IsNearlyEqual(C.Size.X, 245.f)) { Box = &C; break; } }
		if (TestNotNull(TEXT("name box 245 x 30"), Box))
		{
			Near(*this, TEXT("box x 320 - 122.5"), Box->Pos.X, 197.5f, 0.01f);
			Near(*this, TEXT("box y 94"), Box->Pos.Y, 94.f, 0.01f);
		}
		const FSSXFeDrawCmd* CapL = F.Spr(SprCap, 0);
		const FSSXFeDrawCmd* CapR = F.Spr(SprCap, 1);
		if (TestNotNull(TEXT("caps"), CapL) && TestNotNull(TEXT("right cap"), CapR))
		{
			Near(*this, TEXT("left cap right edge 197.5"), CapL->Pos.X + CapL->Size.X, 197.5f, 0.01f);
			Near(*this, TEXT("right cap x 442.5"), CapR->Pos.X, 442.5f, 0.01f);
			TestTrue(TEXT("right cap mirrored"), CapR->UV0.X > CapR->UV1.X || CapL->UV0.X > CapL->UV1.X);
		}
		const FSSXFeTextRec* A = F.Txt(TEXT("A"));
		if (TestNotNull(TEXT("grid 'A'"), A))
		{
			Near(*this, TEXT("'A' centred on 117.5"), A->Pos.X + float(int32(F.D.TextWidth(EFont::Menu, TEXT("A"), 1.f))) * 0.5f, 117.5f, 0.51f);
			Near(*this, TEXT("'A' y 152"), A->Pos.Y, 152.f, 0.01f);
			TestTrue(TEXT("grid letters white with shadow"), SameColor(A->Color, White) && A->bShadow);
		}
		const FSSXFeTextRec* End = F.Txt(F.L(274));
		if (TestNotNull(TEXT("END"), End)) Near(*this, TEXT("END y = 202 + 1.5"), End->Pos.Y, 203.5f, 0.01f);
		const FSSXFeDrawCmd* Del = F.Spr(SprDelete);
		if (TestNotNull(TEXT("delete arrow sprite 59"), Del))
		{
			Near(*this, TEXT("delete arrow centred on 477.5"), Del->Pos.X + Del->Size.X * 0.5f, 477.5f, 0.01f);
			Near(*this, TEXT("delete arrow bottom at 202 + 15"), Del->Pos.Y + Del->Size.Y, 217.f, 0.01f);
		}
		const FSSXFeDrawCmd* Cur = F.Spr(SprCursor);
		if (TestNotNull(TEXT("cursor sprite 42"), Cur))
		{
			Near(*this, TEXT("cursor centre x (507.5 + 1)"), Cur->Pos.X + Cur->Size.X * 0.5f, 508.5f, 0.01f);
			Near(*this, TEXT("cursor centre y (202 + 7.5 + 2)"), Cur->Pos.Y + Cur->Size.Y * 0.5f, 211.5f, 0.01f);
			Near(*this, TEXT("cursor 25 x 1.1"), Cur->Size.X, 27.5f, 0.01f);
		}
		TestTrue(TEXT("legend select / previous / options"), F.Spr(IconCross) && F.Spr(IconTriangle) && F.Spr(IconSquare));
	}
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Left);
	TestEqual(TEXT("move sound +0x120 = 12"), F.FE.SoundHooks, TArray<FString>({ TEXT("ui12") }));
	F.Press(ESSXFeKey::Down);
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Cross);
	TestEqual(TEXT("first letter replaces PLAYER1"), F.FE.Top()->Name, FString(TEXT("M")));
	TestEqual(TEXT("accept sound +0x121 = 13"), F.FE.SoundHooks, TArray<FString>({ TEXT("ui13") }));
	for (int32 I = 0; I < 12; ++I) F.Press(ESSXFeKey::Cross);
	TestEqual(TEXT("max 9 characters"), F.FE.Top()->Name, FString(TEXT("MMMMMMMMM")));
	F.Press(ESSXFeKey::Triangle);
	TestEqual(TEXT("Back reloads the stored name"), F.Profile.Slot[0].UserName, FString(TEXT("PLAYER1")));
	return true;
}

#endif
