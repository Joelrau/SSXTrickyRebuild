#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeRiders.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "Sim/Rider/RiderProfiles.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags LayoutTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	const FSSXLocStrings& LayoutLoc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	struct FLayoutAssets
	{
		FSSXFeAssets A;
		FSSXSpriteTable Sprites;
		bool bFonts = false, bSprites = false;
		FLayoutAssets()
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
		static FLayoutAssets& Get() { static FLayoutAssets S; return S; }
		bool Ready() const { return bFonts && bSprites && LayoutLoc().IsLoaded(); }
	};

	struct FLayoutFe
	{
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FSSXFeDrawList D;
		FLayoutFe() : D(&FLayoutAssets::Get().A)
		{
			SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
			FE.Init(&Profile, &Cheats, &LayoutLoc(), false);
		}
		void Ticks(int32 N) { for (int32 I = 0; I < N; ++I) FE.Tick(); }
		void Settle() { for (int32 N = 0; !FE.IsReady() && FE.Boot == ESSXFeBoot::FrontEnd && N < 3000; ++N) FE.Tick(); }
		void Press(ESSXFeKey K) { Settle(); FE.Key(0, K); FE.Tick(); Settle(); }
		int32 TicksUntil(ESSXFeScreen Screen, int32 Max = 3000)
		{
			for (int32 N = 0; N < Max; ++N) { if (FE.Current() == Screen) return N; FE.Tick(); }
			return FE.Current() == Screen ? Max : -1;
		}
		void Redraw() { D.Reset(); FE.Draw(D); }
		const FSSXFeDrawCmd* Spr(int32 Id, int32 Nth = 0) const
		{
			for (const FSSXFeDrawCmd& C : D.Cmds) { if (C.SpriteId == Id && Nth-- == 0) return &C; }
			return nullptr;
		}
		TArray<const FSSXFeDrawCmd*> AllSpr(int32 Id) const
		{
			TArray<const FSSXFeDrawCmd*> R;
			for (const FSSXFeDrawCmd& C : D.Cmds) { if (C.SpriteId == Id) R.Add(&C); }
			return R;
		}
		const FSSXFeTextRec* Txt(const FString& S) const
		{
			for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == S) return &R; }
			auto Squash = [](const FString& In) { FString O; for (TCHAR C : In) { if (!FChar::IsWhitespace(C)) O.AppendChar(C); } return O; };
			const FString Flat = Squash(S);
			for (const FSSXFeTextRec& R : D.TextRecs) { if (!Flat.IsEmpty() && Squash(R.Str) == Flat) return &R; }
			return nullptr;
		}
		int32 TxtCount(const FString& S) const
		{
			int32 N = 0;
			for (const FSSXFeTextRec& R : D.TextRecs) N += R.Str == S;
			return N;
		}
	};

	void Near(FAutomationTestBase& T, const FString& What, float Got, float Want, float Tol = 1.f)
	{
		T.AddInfo(FString::Printf(TEXT("%s: port %.2f model %.2f (err %.2f)"), *What, Got, Want, FMath::Abs(Got - Want)));
		T.TestTrue(What, FMath::Abs(Got - Want) <= Tol);
	}
	void NearSprite(FAutomationTestBase& T, const FString& What, const FSSXFeDrawCmd* C, float X, float Y, float W, float H)
	{
		if (!T.TestNotNull(What + TEXT(" drawn"), C)) return;
		Near(T, What + TEXT(" x"), C->Pos.X, X);
		Near(T, What + TEXT(" y"), C->Pos.Y, Y);
		Near(T, What + TEXT(" w"), C->Size.X, W);
		Near(T, What + TEXT(" h"), C->Size.Y, H);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILayoutTitleTest, "SSX.UI.Layout.Title", LayoutTestFlags)
bool FSSXUILayoutTitleTest::RunTest(const FString& Parameters)
{
	if (!FLayoutAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; layout checks skipped")); return true; }
	FLayoutFe F;
	F.Ticks(40);
	F.Redraw();
	const FSSXLocStrings& L = LayoutLoc();
	const FSSXFeTextRec* Start = F.Txt(L.ByIndex(3696));
	const FSSXFeTextRec* Dvd = F.Txt(L.ByIndex(3315));
	if (TestNotNull(TEXT("Start Game"), Start) && TestNotNull(TEXT("DVD Content"), Dvd))
	{
		Near(*this, TEXT("Title list left"), Start->Pos.X, 20.f);
		Near(*this, TEXT("Title list top"), Start->Pos.Y, 330.f);
		Near(*this, TEXT("DVD Content left"), Dvd->Pos.X, 20.f);
		Near(*this, TEXT("Title list pitch vs orig_title"), Dvd->Pos.Y - Start->Pos.Y, (855.f - 785.f) * 480.f / 1125.f);
	}
	TestTrue(TEXT("items in the title font"), Start && FMath::IsNearlyEqual(Start->Size.Y, F.D.TextHeight(SSXFe::EFont::Title, 1.f)));
	const FSSXFeTextRec* Press = F.Txt(L.ByIndex(107));
	const FSSXFeTextRec* Copy = F.Txt(L.ByIndex(3544));
	if (TestNotNull(TEXT("Press START"), Press)) { Near(*this, TEXT("Press START x"), Press->Pos.X, 38.f); Near(*this, TEXT("Press START y"), Press->Pos.Y, 410.f); }
	if (TestNotNull(TEXT("copyright"), Copy)) { Near(*this, TEXT("copyright x"), Copy->Pos.X, 20.f); Near(*this, TEXT("copyright y"), Copy->Pos.Y, 428.f); }
	const FSSXFeTextRec* Sel = F.Txt(L.ByIndex(541));
	if (TestNotNull(TEXT("select label"), Sel)) Near(*this, TEXT("legend label right edge (+shadow)"), Sel->Pos.X + Sel->Size.X + 2.f, 595.f);
	NearSprite(*this, TEXT("legend X icon"), F.Spr(SSXFe::IconCross), 599.f, 428.5f, 17.f, 17.f);
	TestNull(TEXT("no frame on Title"), F.Spr(SSXFe::SprPanelL));
	TestFalse(TEXT("no static gold second item: only the highlighted one pulses"), F.FE.Top()->Sel == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILayoutFrameTest, "SSX.UI.Layout.Frame", LayoutTestFlags)
bool FSSXUILayoutFrameTest::RunTest(const FString& Parameters)
{
	if (!FLayoutAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; layout checks skipped")); return true; }
	FLayoutFe F;
	F.Press(ESSXFeKey::Cross);
	F.Ticks(40);
	F.Redraw();
	const FSSXLocStrings& L = LayoutLoc();
	using namespace SSXFe;
	const FString TabStr = L.ByIndex(3499);
	const FSSXFeTextRec* Tab = F.Txt(TabStr);
	TestEqual(TEXT("screen name drawn once"), F.TxtCount(TabStr), 1);
	if (TestNotNull(TEXT("tab text"), Tab))
	{
		Near(*this, TEXT("tab text x"), Tab->Pos.X, 20.f);
		Near(*this, TEXT("tab text y"), Tab->Pos.Y, 20.f);
		TestTrue(TEXT("tab text in the menu font"), FMath::IsNearlyEqual(Tab->Size.Y, F.D.TextHeight(EFont::Menu, 1.f)));
		const float Tw = 141.f, Th = 17.f;
		NearSprite(*this, TEXT("tab body 33"), F.Spr(SprTabBody), 0.f, 0.f, Tw + 20.f, Th + 27.f);
		NearSprite(*this, TEXT("tab cap 32"), F.Spr(SprTabCap), Tw + 20.f, 0.f, 29.f, Th + 27.f);
	}
	NearSprite(*this, TEXT("panel 13"), F.Spr(SprPanelL), 0.f, 0.f, 320.f, 39.f);
	NearSprite(*this, TEXT("panel 14"), F.Spr(SprPanelR), 320.f, 0.f, 320.f, 39.f);
	NearSprite(*this, TEXT("strip 5"), F.Spr(SprStripL), 0.f, 39.f, 320.f, 40.f);
	NearSprite(*this, TEXT("strip 6"), F.Spr(SprStripR), 320.f, 39.f, 320.f, 40.f);
	int32 Dark = 0;
	for (const FSSXFeDrawCmd& C : F.D.Cmds)
	{
		if (C.SpriteId < 0 && C.Kind == FSSXFeDrawCmd::EKind::Quad && C.Texture == nullptr && C.Color.Equals(OptionsPanel))
		{
			const bool bA = FMath::IsNearlyEqual(C.Pos.X, 0.f) && FMath::IsNearlyEqual(C.Pos.Y, 0.f) && FMath::IsNearlyEqual(C.Size.X, 640.f) && FMath::IsNearlyEqual(C.Size.Y, 70.f);
			const bool bB = FMath::IsNearlyEqual(C.Pos.X, 115.f) && FMath::IsNearlyEqual(C.Pos.Y, 70.f) && FMath::IsNearlyEqual(C.Size.X, 410.f) && FMath::IsNearlyEqual(C.Size.Y, 14.f);
			Dark += bA || bB;
		}
	}
	TestEqual(TEXT("title bar dark rects (0,0,640,70) + (115,70,410,14)"), Dark, 2);
	NearSprite(*this, TEXT("bar edge 17"), F.Spr(SprBarEdgeL), 0.f, 63.f, 320.f, 42.f);
	NearSprite(*this, TEXT("bar edge 18"), F.Spr(SprBarEdgeR), 320.f, 63.f, 320.f, 42.f);
	const FSSXFeTextRec* Item = F.Txt(L.ByIndex(3595));
	if (TestNotNull(TEXT("item"), Item))
	{
		Near(*this, TEXT("item centre x (extent + shadow)"), Item->Pos.X + (Item->Size.X + 2.f) * 0.5f, 320.f);
		Near(*this, TEXT("item centre y (extent + shadow)"), Item->Pos.Y + (F.D.FontHeight(EFont::Title, 1.f) + 2.f) * 0.5f, 63.f);
		TestTrue(TEXT("item in the title font"), FMath::IsNearlyEqual(Item->Size.Y, F.D.TextHeight(EFont::Title, 1.f)));
	}
	NearSprite(*this, TEXT("left arrow 3"), F.Spr(SprArrowL), 138.f, 56.f, 18.f, 18.f);
	NearSprite(*this, TEXT("right arrow 4"), F.Spr(SprArrowR), 485.f, 56.f, 18.f, 18.f);
	NearSprite(*this, TEXT("bottom strip 7"), F.Spr(SprBStripL), 0.f, 365.f, 320.f, 40.f);
	NearSprite(*this, TEXT("bottom strip 8"), F.Spr(SprBStripR), 320.f, 365.f, 320.f, 40.f);
	const TArray<const FSSXFeDrawCmd*> Fill = F.AllSpr(SprFillL);
	if (TestTrue(TEXT("fill 11 tiled (75 / 39 = 2 cells)"), Fill.Num() == 2))
	{
		Near(*this, TEXT("fill top"), Fill[0]->Pos.Y, 405.f);
		Near(*this, TEXT("fill bottom"), Fill[1]->Pos.Y + Fill[1]->Size.Y, 480.f);
	}
	const int32 Icons[] = { IconCross, IconTriangle, IconSquare };
	const int32 Labels[] = { 541, 538, 537 };
	for (int32 I = 0; I < 3; ++I)
	{
		const FSSXFeDrawCmd* C = F.Spr(Icons[I]);
		if (TestNotNull(TEXT("legend icon"), C))
		{
			Near(*this, FString::Printf(TEXT("legend row %d icon x"), I), C->Pos.X, 599.f);
			Near(*this, FString::Printf(TEXT("legend row %d centre y"), I), C->Pos.Y + C->Size.Y * 0.5f, 411.f + 20.f * I);
		}
		const FSSXFeTextRec* T = F.Txt(L.ByIndex(Labels[I]));
		if (TestNotNull(TEXT("legend label"), T)) Near(*this, FString::Printf(TEXT("legend row %d label right (+shadow)"), I), T->Pos.X + T->Size.X + 2.f, 595.f);
	}
	const FSSXFeTextRec* Help = F.Txt(L.ByIndex(3233));
	if (TestNotNull(TEXT("help line 3233"), Help)) { Near(*this, TEXT("help line x"), Help->Pos.X, 20.f); Near(*this, TEXT("help line y"), Help->Pos.Y, 402.f); }
	F.FE.Key(0, ESSXFeKey::Left);
	F.Redraw();
	TestNull(TEXT("left arrow hidden on the first item"), F.Spr(SprArrowL));
	TestNotNull(TEXT("help line 3235 (Practice)"), F.Txt(L.ByIndex(3235)));
	F.FE.Key(0, ESSXFeKey::Right);
	F.Redraw();
	const FSSXFeDrawCmd* R = F.Spr(SprArrowR);
	TestTrue(TEXT("right arrow starts red (1, .8, .1, .1)"), R && R->Color.Equals(ArrowFlash, 0.01f));
	F.Ticks(15);
	F.Redraw();
	R = F.Spr(SprArrowR);
	TestTrue(TEXT("white after 15 frames"), R && R->Color.Equals(White, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILayoutCharSelectTest, "SSX.UI.Layout.CharSelect", LayoutTestFlags)
bool FSSXUILayoutCharSelectTest::RunTest(const FString& Parameters)
{
	if (!FLayoutAssets::Get().Ready() || !SSXFeTables::BoardsLoaded()) { SSXTest::Skip(*this, TEXT("FE assets / tables not converted; layout checks skipped")); return true; }
	using namespace SSXFe;
	FLayoutFe F;
	F.FE.GoTo(ESSXFeScreen::CharacterSelect);
	F.Ticks(40);
	F.Settle();
	F.FE.TopMut()->bRankingView = false;
	F.Redraw();
	const FSSXLocStrings& L = LayoutLoc();
	const int32 Char = SSXFeTables::CarouselRider(4);
	const FSSXFeTextRec* Name = F.Txt(SSXFeTables::RiderName(Char));
	if (TestNotNull(TEXT("rider name in the title bar"), Name)) Near(*this, TEXT("name centre y"), Name->Pos.Y + (F.D.FontHeight(EFont::Title, 1.f) + 2.f) * 0.5f, 63.f);
	TestNotNull(TEXT("wraps: left arrow"), F.Spr(SprArrowL));
	TestNotNull(TEXT("wraps: right arrow"), F.Spr(SprArrowR));
	const FSSXFeTextRec* Cls = F.Txt(L.ByIndex(SSXFeTables::BoardTypeLoc(SSXFeTables::Board(Char, 0).Type)));
	if (TestNotNull(TEXT("board class"), Cls)) { Near(*this, TEXT("board class x"), Cls->Pos.X, 20.f); Near(*this, TEXT("board class y"), Cls->Pos.Y, 383.f); }
	const int32 Labels[] = { 37, 39, 40, 41 };
	const TArray<const FSSXFeDrawCmd*> Tracks = F.AllSpr(SprBarTrack);
	const TArray<const FSSXFeDrawCmd*> Caps = F.AllSpr(SprBarCap);
	TestEqual(TEXT("4 bar tracks"), Tracks.Num(), 4);
	TestEqual(TEXT("4 bar caps"), Caps.Num(), 4);
	const SSXFeTables::FBoard& B = SSXFeTables::Board(Char, F.Profile.Slot[0].Char == Char ? F.Profile.Slot[0].Board : 0);
	const TArray<const FSSXFeDrawCmd*> Pips = F.AllSpr(SprPip);
	for (int32 I = 0; I < 4 && Tracks.Num() == 4 && Caps.Num() == 4; ++I)
	{
		const float RowY = 410.f + 15.f * I;
		const int32 Bonus = (int32)(B.Stats[I] * 0.1f);
		const int32 Max = SSXProg::StatBarMax(Char, I) + Bonus;
		const int32 Base = F.Profile.Career[Char].StatPoints[I];
		const FSSXFeTextRec* Lab = F.Txt(L.ByIndex(Labels[I]));
		if (TestNotNull(TEXT("stat label"), Lab))
		{
			Near(*this, FString::Printf(TEXT("row %d label right (+shadow)"), I), Lab->Pos.X + Lab->Size.X + 2.f, 170.f);
			Near(*this, FString::Printf(TEXT("row %d label centre y"), I), Lab->Pos.Y + (F.D.FontHeight(EFont::Menu, 0.7f) + 2.f) * 0.5f, RowY);
		}
		NearSprite(*this, FString::Printf(TEXT("row %d track"), I), Tracks[I], 178.f, RowY - 5.f, Max * 5.f, 10.f);
		NearSprite(*this, FString::Printf(TEXT("row %d cap"), I), Caps[I], 178.f + Max * 5.f, RowY - 5.f, 6.f, 10.f);
		int32 Red = 0, Yellow = 0;
		for (const FSSXFeDrawCmd* P : Pips)
		{
			if (!FMath::IsNearlyEqual(P->Pos.Y, RowY - 5.f, 0.5f)) continue;
			Red += P->Color.Equals(FeColor(1.f, 1.f, 0.f, 0.f), 0.01f);
			Yellow += P->Color.Equals(FeColor(1.f, 1.f, 1.f, 0.f), 0.01f);
		}
		AddInfo(FString::Printf(TEXT("row %d: base %d bonus %d max %d -> yellow cells %d, red cells %d"), I, Base, Bonus, Max, Yellow, Red));
		TestEqual(TEXT("yellow pips = base (5 px cells)"), Yellow, Base);
		TestEqual(TEXT("red layer = base + bonus, only with a bonus"), Red, Bonus > 0 ? Base + Bonus : 0);
		const FSSXFeTextRec* Val = F.Txt(FString::Printf(TEXT("%d/%d"), Base + Bonus, Max));
		if (TestNotNull(TEXT("value text"), Val)) Near(*this, FString::Printf(TEXT("row %d value x"), I), Val->Pos.X, 450.f);
	}
	F.FE.Key(0, ESSXFeKey::Left); F.FE.Key(0, ESSXFeKey::Left); F.FE.Key(0, ESSXFeKey::Left);
	F.FE.TopMut()->bRankingView = false;
	F.Redraw();
	TestEqual(TEXT("on Kaori"), F.FE.Top()->Sel, 1);
	TestNull(TEXT("locked: no stat bars"), F.Spr(SprBarTrack));
	const FSSXFeTextRec* Lock = F.Txt(L.ByIndex(3528));
	if (TestNotNull(TEXT("lock help line 3528"), Lock)) { Near(*this, TEXT("lock line x"), Lock->Pos.X, 20.f); Near(*this, TEXT("lock line y"), Lock->Pos.Y, 402.f); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILayoutSlideInTest, "SSX.UI.Layout.SlideIn", LayoutTestFlags)
bool FSSXUILayoutSlideInTest::RunTest(const FString& Parameters)
{
	if (!FLayoutAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; layout checks skipped")); return true; }
	using namespace SSXFe;
	FLayoutFe F;
	F.Settle();
	F.FE.Key(0, ESSXFeKey::Cross);
	TestEqual(TEXT("ModeSelect built after the Title close (2 updates)"), F.TicksUntil(ESSXFeScreen::ModeSelect), 2);
	F.Redraw();
	NearSprite(*this, TEXT("top panel at -79"), F.Spr(SprPanelL), 0.f, -79.f, 320.f, 39.f);
	NearSprite(*this, TEXT("bottom strip at 480"), F.Spr(SprBStripL), 0.f, 480.f, 320.f, 40.f);
	NearSprite(*this, TEXT("bar edge at -105 + 63"), F.Spr(SprBarEdgeL), 0.f, -42.f, 320.f, 42.f);
	F.Ticks(15);
	F.Redraw();
	NearSprite(*this, TEXT("half way (15 / 30)"), F.Spr(SprBStripL), 0.f, 365.f + 57.5f, 320.f, 40.f);
	TestEqual(TEXT("phase 0 while sliding in: no input (vfunc_18 needs phase 2)"), F.FE.Top()->Phase, 0);
	F.FE.Key(0, ESSXFeKey::Right);
	TestEqual(TEXT("a key during the slide-in is ignored"), F.FE.Top()->SelectedId(), 1);
	F.Ticks(15);
	F.Redraw();
	NearSprite(*this, TEXT("in place at 30"), F.Spr(SprBStripL), 0.f, 365.f, 320.f, 40.f);
	F.Ticks(1);
	TestEqual(TEXT("phase 2 after the 31st tween update (vfunc_07)"), F.FE.Top()->Phase, 2);
	TestTrue(TEXT("vfunc_20: onTitleStop"), F.FE.SceneEvents.Num() > 0 && F.FE.SceneEvents.Last() == TEXT("onTitleStop"));
	F.FE.Key(0, ESSXFeKey::Cross);
	TestEqual(TEXT("still ModeSelect, phase 3"), F.FE.Top()->Phase, 3);
	TestTrue(TEXT("black out fade started (kind 1)"), F.FE.Fader.bActive && F.FE.Fader.Up == 0.5f && F.FE.Fader.Down == 0.5f
		&& F.FE.Fader.Color.R == 0.f);
	F.Ticks(15);
	F.Redraw();
	NearSprite(*this, TEXT("slide-out half way (bottom group 365 -> 480)"), F.Spr(SprBStripL), 0.f, 365.f + 57.5f, 320.f, 40.f);
	TestEqual(TEXT("ModeSelect still on top during the slide-out"), FString(LexToString(F.FE.Current())), FString(TEXT("ModeSelect")));
	const FSSXFeDrawCmd* Fade = nullptr;
	for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.Layer == SpriteLayer(FadeLayer)) Fade = &C; }
	if (TestNotNull(TEXT("fade quad drawn"), Fade))
	{
		NearSprite(*this, TEXT("fade quad covers the screen"), Fade, 0.f, 0.f, 640.f, 480.f);
		TestTrue(TEXT("fade alpha 15/30 = 0.5"), FMath::IsNearlyEqual(Fade->Color.A, 0.5f, 0.02f));
	}
	const int32 ToChar = F.TicksUntil(ESSXFeScreen::CharacterSelect);
	TestEqual(TEXT("CharacterSelect built after the 31-frame slide-out (+1 update for vfunc_08)"), 15 + ToChar, 32);
	F.Settle();
	F.Ticks(70);
	F.FE.Key(0, ESSXFeKey::Triangle);
	TestEqual(TEXT("Back: phase 1"), F.FE.Top()->Phase, 1);
	TestTrue(TEXT("white in fade (kind 1)"), F.FE.Fader.bActive && F.FE.Fader.Color.R == 1.f && F.FE.Fader.Up == 0.5f);
	const int32 ToMode = F.TicksUntil(ESSXFeScreen::ModeSelect);
	TestEqual(TEXT("popped after the 31-frame slide-out"), ToMode, 31);
	F.Redraw();
	TestEqual(TEXT("back on ModeSelect"), FString(LexToString(F.FE.Current())), FString(TEXT("ModeSelect")));
	NearSprite(*this, TEXT("re-show starts off screen"), F.Spr(SprBStripL), 0.f, 480.f, 320.f, 40.f);
	F.Ticks(30);
	F.Redraw();
	NearSprite(*this, TEXT("re-show in place after 30 frames"), F.Spr(SprBStripL), 0.f, 365.f, 320.f, 40.f);
	FSSXFeFader Fd;
	Fd.StartKind(2, Black);
	Fd.Tick();
	TestTrue(TEXT("kind 2: alpha 1 on the first update"), Fd.Alpha == 1.f && Fd.Stage == 1);
	Fd.Tick();
	Fd.Tick();
	TestTrue(TEXT("kind 2: then 1 - t / 1.5 (t carried from the stage changes: 3 / 60)"), FMath::IsNearlyEqual(Fd.Alpha, 1.f - (3.f / 60.f) / 1.5f, 1e-3f));
	TestFalse(TEXT("a start while a fade runs is ignored (FUN_001FF648)"), Fd.Start(0.5f, 0.f, 0.5f, White));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILayoutBackCameraTest, "SSX.UI.Layout.BackCamera", LayoutTestFlags)
bool FSSXUILayoutBackCameraTest::RunTest(const FString& Parameters)
{
	struct FCase { ESSXFeKey Move; const TCHAR* Cam; const TCHAR* Door; };
	const FCase Cases[] = {
		{ ESSXFeKey::None, TEXT("modeSelectSE_WC"), TEXT("onSEOpen") },
		{ ESSXFeKey::Right, TEXT("modeSelectWC_SE"), TEXT("onWCOpen") },
	};
	for (const FCase& C : Cases)
	{
		FLayoutFe F;
		F.Settle();
		F.Press(ESSXFeKey::Cross);
		TestTrue(TEXT("intro on first open"), F.FE.Camera.History.Contains(TEXT("modeSelectIntro")));
		if (C.Move != ESSXFeKey::None) F.Press(C.Move);
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("CharacterSelect"), FString(LexToString(F.FE.Current())), FString(TEXT("CharacterSelect")));
		TestEqual(TEXT("CharacterSelect vfunc_15: camera mode 8"), F.FE.CamMode, 8);
		F.Ticks(5);
		F.Press(ESSXFeKey::Triangle);
		TestEqual(TEXT("back on ModeSelect"), FString(LexToString(F.FE.Current())), FString(TEXT("ModeSelect")));
		TestEqual(FString::Printf(TEXT("cut to %s"), C.Cam), F.FE.Camera.Current, FString(C.Cam));
		TestTrue(TEXT("held frame 0 (cut, not a swing)"), F.FE.Camera.Start == 0.f && F.FE.Camera.End == 0.f && F.FE.Camera.Time == 0.f);
		TestTrue(TEXT("door event of the selected item"), F.FE.SceneEvents.Contains(FString(C.Door)));
		TestFalse(TEXT("not in the character room"), F.FE.Camera.Current.StartsWith(TEXT("charSelect")));
	}
	{
		FLayoutFe F;
		F.Settle();
		F.Press(ESSXFeKey::Cross);
		F.Press(ESSXFeKey::Left);
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("EventSel"), FString(LexToString(F.FE.Current())), FString(TEXT("EventSel")));
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("CharacterSelect"), FString(LexToString(F.FE.Current())), FString(TEXT("CharacterSelect")));
		TestTrue(TEXT("EventSel vfunc_21 (Freeride, 1 pad): request 6 = the PR -> charSelect swing"),
			F.FE.Camera.History.Contains(TEXT("modeSelectPR_charSelect")));
		F.Press(ESSXFeKey::Triangle);
		TestEqual(TEXT("back on EventSel"), FString(LexToString(F.FE.Current())), FString(TEXT("EventSel")));
		TestEqual(TEXT("PR group: cut to modeSelectPR_SE"), F.FE.Camera.Current, FString(TEXT("modeSelectPR_SE")));
	}
	{
		FLayoutFe F;
		F.Settle();
		TestEqual(TEXT("Title: camera mode 1"), F.FE.CamMode, 1);
		TestTrue(TEXT("Title view = modeSelectIntro frame 0 held"), F.FE.Camera.Current == TEXT("modeSelectIntro") && F.FE.Camera.End == 0.f);
		F.FE.SceneEvents.Reset();
		F.Press(ESSXFeKey::Cross);
		TestTrue(TEXT("Start Game: the full modeSelectIntro"), F.FE.Camera.History.Num() >= 2 && F.FE.Camera.History.Last() == TEXT("modeSelectIntro")
			&& F.FE.Camera.End > 0.f);
		TestTrue(TEXT("Title vfunc_21: onTitleOpen (the title doors)"), F.FE.SceneEvents.Contains(TEXT("onTitleOpen")));
		TestEqual(TEXT("ModeSelect"), FString(LexToString(F.FE.Current())), FString(TEXT("ModeSelect")));
		TestEqual(TEXT("ModeSelect highlight (Single Event): camera mode 5"), F.FE.CamMode, 5);
		F.Press(ESSXFeKey::Triangle);
		TestEqual(TEXT("Back -> Title"), FString(LexToString(F.FE.Current())), FString(TEXT("Title")));
		TestEqual(TEXT("Title re-show: camera mode 1"), F.FE.CamMode, 1);
		TestTrue(TEXT("Title re-show: cut to modeSelectIntro frame 0"), F.FE.Camera.Current == TEXT("modeSelectIntro")
			&& F.FE.Camera.Start == 0.f && F.FE.Camera.End == 0.f && F.FE.Camera.Time == 0.f);
		TestTrue(TEXT("ModeSelect vfunc_19: the open door closes"), F.FE.SceneEvents.Contains(TEXT("onSEClose")));
		F.Press(ESSXFeKey::Cross);
		TestEqual(TEXT("Start Game again: the fly-in again"), FString(LexToString(F.FE.Current())), FString(TEXT("ModeSelect")));
		TestTrue(TEXT("second Start Game replays the full intro"), F.FE.Camera.Current == TEXT("modeSelectIntro") && F.FE.Camera.End > 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILayoutRiderSizeTest, "SSX.UI.Layout.RiderSize", LayoutTestFlags)
bool FSSXUILayoutRiderSizeTest::RunTest(const FString& Parameters)
{
	if (!SSXRider::FRiderTable::Shared().IsLoaded()) { SSXTest::Skip(*this, TEXT("riders.bin not converted; size checks skipped")); return true; }
	// 0x331EA8
	const double Want[] = { 0.85, 0.70, 1.0, 0.75, 0.87, 0.85, 0.90, 0.95, 0.85, 0.90, 0.98, 0.85 };
	for (int32 C = 0; C < 12; ++C)
	{
		TestTrue(FString::Printf(TEXT("size scale char %d = %.2f"), C, Want[C]), FMath::IsNearlyEqual(SSXFeRiders::SizeScale(C), Want[C], 0.005));
	}
	struct FR { const TCHAR* Name; int32 Char; double Ratio; };
	const FR Rs[] = { { TEXT("Mac"), 3, 0.234 / 0.182 }, { TEXT("Kaori"), 1, 0.36 / 0.25 }, { TEXT("Moby"), 4, 0.42 / 0.37 } };
	for (const FR& R : Rs)
	{
		const double S = SSXFeRiders::SizeScale(R.Char);
		const double After = R.Ratio * S;
		AddInfo(FString::Printf(TEXT("%s: port/orig before %.2f, 1/scale %.2f, after %.2f"), R.Name, R.Ratio, 1.0 / S, After));
		TestTrue(FString::Printf(TEXT("%s height ratio about 1 after the scale"), R.Name), FMath::Abs(After - 1.0) < 0.05);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILayoutDetailsTest, "SSX.UI.Layout.Details", LayoutTestFlags)
bool FSSXUILayoutDetailsTest::RunTest(const FString& Parameters)
{
	if (!FLayoutAssets::Get().Ready() || !SSXFeTables::BoardsLoaded()) { SSXTest::Skip(*this, TEXT("FE assets / tables not converted; layout checks skipped")); return true; }
	using namespace SSXFe;
	const FSSXLocStrings& L = LayoutLoc();
	{
		// FUN_00274108 / FUN_002742E8
		FLayoutFe F;
		F.FE.GoTo(ESSXFeScreen::NumPlayers);
		F.Ticks(40);
		F.Settle();
		F.Redraw();
		const float ItemH = F.D.FontHeight(F.FE.Top()->ItemFont, 1.f) + 2.f;
		const FSSXFeTextRec* A = F.Txt(L.ByIndex(3597));
		const FSSXFeTextRec* B = F.Txt(L.ByIndex(3611));
		if (TestNotNull(TEXT("item 0 (3597)"), A)) Near(*this, TEXT("item 0 centre y"), A->Pos.Y + ItemH * 0.5f, 93.f);
		if (TestNotNull(TEXT("item 1 (3611)"), B) && A) Near(*this, TEXT("item pitch = height + 4"), B->Pos.Y - A->Pos.Y, ItemH + ListSpacing);
	}
	{
		FLayoutFe F;
		F.FE.GoTo(ESSXFeScreen::CharacterSelect);
		F.Ticks(40);
		F.Settle();
		F.FE.TopMut()->bRankingView = true;
		F.Redraw();
		const FSSXFeTextRec* Head = F.Txt(L.ByIndex(221));
		if (TestNotNull(TEXT("heading 221"), Head))
		{
			TestEqual(TEXT("heading layer 9"), Head->Layer, TextLayer(9));
			Near(*this, TEXT("heading right edge (+shadow)"), Head->Pos.X + Head->Size.X + 2.f, 250.f - 19.f - 6.f, 1.5f);
		}
		const int32 Rank = SSXFeTables::RankFromPoints(0);
		const FSSXFeTextRec* Status = F.Txt(L.ByIndex(23 + Rank));
		if (TestNotNull(TEXT("rank status text"), Status)) TestEqual(TEXT("status layer 9"), Status->Layer, TextLayer(9));
		F.FE.TopMut()->bRankingView = false;
		F.Redraw();
		const FSSXFeDrawCmd* Track = F.Spr(SprBarTrack);
		const FSSXFeDrawCmd* Cap = F.Spr(SprBarCap);
		if (TestNotNull(TEXT("track"), Track) && TestNotNull(TEXT("cap"), Cap))
		{
			TestTrue(TEXT("cap colour = track colour"), Cap->Color.Equals(Track->Color, 0.001f));
			TestEqual(TEXT("cap layer = track layer"), Cap->Layer, Track->Layer);
		}
	}
	return true;
}

#endif
