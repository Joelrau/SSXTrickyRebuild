// FUN_001FC178 / FUN_00183C20 / FUN_00263000 / 0x1D1498
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshComponentLODInfo.h"
#include "StaticMeshResources.h"
#include "World/SSXPropAnimation.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "Sim/Air/TrickName.h"
#include "Effects/TrackEffects.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeDvd.h"
#include "UI/SSXFeOptions.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXMemCard.h"
#include "Race/SSXReplayFile.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ScrTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	const FSSXLocStrings& ScrLoc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	struct FScrAssets
	{
		FSSXFeAssets A;
		FSSXSpriteTable Sprites;
		bool bFonts = false, bSprites = false;
		FScrAssets()
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
		static FScrAssets& Get() { static FScrAssets S; return S; }
		bool Ready() const { return bFonts && bSprites && ScrLoc().IsLoaded(); }
	};

	struct FScrFe
	{
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FSSXFeDrawList D;
		FScrFe(bool bColdBoot = false) : D(&FScrAssets::Get().A)
		{
			SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
			FE.Init(&Profile, &Cheats, &ScrLoc(), bColdBoot);
		}
		void Settle() { for (int32 N = 0; !FE.IsReady() && FE.Boot == ESSXFeBoot::FrontEnd && N < 3000; ++N) FE.Tick(); }
		void Press(ESSXFeKey K) { Settle(); FE.Key(0, K); FE.Tick(); Settle(); }
		void Redraw() { D.Reset(); FE.Draw(D); }
		FString L(int32 Id) const { return ScrLoc().ByIndex(Id); }
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
		bool HasHook(const TCHAR* H) const { return FE.SoundHooks.Contains(FString(H)); }
	};

	void ScrNear(FAutomationTestBase& T, const FString& What, float Got, float Want, float Tol = 0.51f)
	{
		T.AddInfo(FString::Printf(TEXT("%s: port %.2f model %.2f"), *What, Got, Want));
		T.TestTrue(What, FMath::Abs(Got - Want) <= Tol);
	}
	bool ScrSameColor(const FLinearColor& A, const FLinearColor& B) { return A.Equals(B, 1e-3f); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUITrickBookTest, "SSX.UI.TrickBook", ScrTestFlags)
bool FSSXUITrickBookTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	using namespace SSXFeTrickBook;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; TrickBook checks skipped")); return true; }
	FScrFe F;
	F.Profile.Slot[0].Char = 0;
	F.Profile.Career[0].BookDone[0] = 0x5;
	F.Profile.Career[0].BookDone[1] = 0;
	F.FE.GoTo(ESSXFeScreen::TrickBook, 0);
	F.Settle();
	const FSSXFeState* S = F.FE.Top();
	if (!TestTrue(TEXT("TrickBook open"), S && S->Screen == ESSXFeScreen::TrickBook)) return false;
	TestFalse(TEXT("no Square (vfunc_12 +0x18 = 0)"), S->bSquare);
	TestEqual(TEXT("one chapter visible (none complete)"), S->Items.Num(), 1);
	TestEqual(TEXT("chapter 0 loaded at open (FUN_0027A288(0))"), S->Chapter, 0);
	TestEqual(TEXT("legend 0x800A"), S->HelpMask, ChapterLegend);
	TestEqual(TEXT("help 3545"), F.FE.CurrentHelpLine(), F.L(3545));
	F.Redraw();
	// background: untextured 640 x 480 (1, 1.1137, .8627, 0) -> GS x128 = (143, 110, 0)
	const FSSXFeDrawCmd* Bg = nullptr;
	for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.bGsUntextured && C.Size.X == 640.f && C.Size.Y == 480.f && C.Color.R > 1.f) { Bg = &C; break; } }
	if (TestNotNull(TEXT("background quad"), Bg))
	{
		const FColor Sc = FSSXFeDrawList::ScreenColor(*Bg).ToFColor(false);
		AddInfo(FString::Printf(TEXT("background screen RGB %d %d %d"), Sc.R, Sc.G, Sc.B));
		TestTrue(TEXT("background shows (143, 110, 0)"), FMath::Abs(Sc.R - 143) <= 1 && FMath::Abs(Sc.G - 110) <= 1 && Sc.B == 0);
	}
	TestEqual(TEXT("band 6 tiles of sprite 60"), F.SprCount(SprBand), 6);
	if (const FSSXFeDrawCmd* B = F.Spr(SprBand)) { ScrNear(*this, TEXT("band top (bottom edge at 320)"), B->Pos.Y, 240.f); }
	if (const FSSXFeDrawCmd* C = F.Spr(SprCover); TestNotNull(TEXT("cover sprite 0x86"), C))
	{
		ScrNear(*this, TEXT("cover x"), C->Pos.X, 0.f); ScrNear(*this, TEXT("cover y"), C->Pos.Y, 150.f);
		ScrNear(*this, TEXT("cover w"), C->Size.X, 195.f);
	}
	if (const FSSXFeTextRec* N = F.Txt(TEXT("Eddie")); TestNotNull(TEXT("rider name"), N))
	{
		const float W = F.D.TextBoxWidth(EFont::Title, TEXT("Eddie"), 1.f) + ShadowOffset;
		ScrNear(*this, TEXT("name centred over the cover"), N->Pos.X + W * 0.5f, 97.5f);
		ScrNear(*this, TEXT("name y"), N->Pos.Y, 105.f);
		TestTrue(TEXT("name shadow"), N->bShadow);
	}
	if (const FSSXFeDrawCmd* G = F.Spr(SprChapter); TestNotNull(TEXT("chapter icon 43"), G))
	{
		ScrNear(*this, TEXT("chapter icon x"), G->Pos.X, 480.f); ScrNear(*this, TEXT("chapter icon y"), G->Pos.Y, 105.f);
	}
	const FSSXFeDrawCmd* Pic = nullptr;
	for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.SpriteId == 0x7C && C.Size.X == PicW) { Pic = &C; } }
	if (TestNotNull(TEXT("picture with item 0's sprite"), Pic))
	{
		ScrNear(*this, TEXT("picture x"), Pic->Pos.X, 191.5f); ScrNear(*this, TEXT("picture y"), Pic->Pos.Y, 126.f);
		TestTrue(TEXT("picture white (trick 0 done)"), ScrSameColor(Pic->Color, White));
	}
	TestEqual(TEXT("sprite 41: 8 frame pieces + 2 edges"), F.SprCount(SprFrame), 10);
	{
		bool bFound = false;
		for (const FSSXFeDrawCmd& C : F.D.Cmds)
		{
			if (C.SpriteId == SprFrame && C.bRot90 && FMath::IsNearlyEqual(C.Pos.X, 448.5f) && FMath::IsNearlyEqual(C.Pos.Y, 126.f))
			{
				bFound = true;
				ScrNear(*this, TEXT("rotated right piece w = h41 / 2"), C.Size.X, 7.f);
				ScrNear(*this, TEXT("rotated right piece h = 79"), C.Size.Y, 79.f);
				TestTrue(TEXT("frame black"), ScrSameColor(C.Color, Black));
			}
			if (C.SpriteId == SprFrame && C.bRot90 && FMath::IsNearlyEqual(C.Pos.Y, 205.f) && C.Pos.X < 191.f)
			{
				ScrNear(*this, TEXT("left piece 0x19 right edge at 191.5"), C.Pos.X + C.Size.X, 191.5f);
			}
		}
		TestTrue(TEXT("rotated right frame piece"), bFound);
	}
	for (int32 T = 0; T < 5; ++T)
	{
		const FSSXFeDrawCmd* Ic = nullptr;
		for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.SpriteId == 0x7C + T && C.Size.X == IconW) { Ic = &C; } }
		if (!TestNotNull(FString::Printf(TEXT("trick icon %d"), T), Ic)) continue;
		ScrNear(*this, FString::Printf(TEXT("icon %d x"), T), Ic->Pos.X, 72.5f + 99.f * T);
		ScrNear(*this, FString::Printf(TEXT("icon %d y"), T), Ic->Pos.Y, 315.f);
		TestTrue(FString::Printf(TEXT("icon %d colour"), T), ScrSameColor(Ic->Color, (T == 0 || T == 2) ? White : IconDisabled));
	}
	TestNull(TEXT("no left arrow on the first trick"), [&]() -> const FSSXFeDrawCmd* { for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.SpriteId == SprArrowL && C.Size.Y == TrickArrowH) return &C; } return nullptr; }());
	{
		const FSSXFeDrawCmd* R = nullptr;
		for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.SpriteId == SprArrowR && C.Size.Y == TrickArrowH) R = &C; }
		if (TestNotNull(TEXT("right arrow"), R)) { ScrNear(*this, TEXT("right arrow x"), R->Pos.X, 567.5f); ScrNear(*this, TEXT("right arrow y"), R->Pos.Y, 320.f); }
	}
	TestNull(TEXT("no slider while the chapter list has the focus"), F.Spr(SprSlider));
	{
		FString Want = F.L(CounterLoc);
		for (const int32 V : { 2, 5 }) { const int32 At = Want.Find(TEXT("%d")); if (At != INDEX_NONE) Want = Want.Left(At) + FString::FromInt(V) + Want.Mid(At + 2); }
		if (const FSSXFeTextRec* C = F.Txt(Want); TestNotNull(TEXT("counter text"), C))
		{
			const float W = F.D.TextBoxWidth(EFont::Title, Want, CounterScale) + ShadowOffset;
			ScrNear(*this, TEXT("counter centred"), C->Pos.X + W * 0.5f, 320.f);
			ScrNear(*this, TEXT("counter y 430"), C->Pos.Y, 430.f);
			TestTrue(TEXT("counter gold"), ScrSameColor(C->Color, Gold));
		}
	}
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Cross);
	S = F.FE.Top();
	TestTrue(TEXT("X: trick list focused"), S->bBookTricks);
	TestTrue(TEXT("X: accept sound 4"), F.HasHook(TEXT("ui4")));
	TestEqual(TEXT("trick legend 0x800C"), S->HelpMask, TrickLegend);
	TestEqual(TEXT("help 3546"), F.FE.CurrentHelpLine(), F.L(3546));
	TestEqual(TEXT("trick name = trickdef record 0"), S->BookTrickName, FTable::Get().TrickName(0, 0));
	if (FTable::Get().IsLoaded() && SSXTrick::FTrickTables::Get().IsLoaded()) { TestFalse(TEXT("trick name not empty"), S->BookTrickName.IsEmpty()); }
	F.Redraw();
	if (const FSSXFeDrawCmd* Sl = F.Spr(SprSlider); TestNotNull(TEXT("slider on the trick list"), Sl))
	{
		ScrNear(*this, TEXT("slider x"), Sl->Pos.X, 67.5f); ScrNear(*this, TEXT("slider y"), Sl->Pos.Y, 310.f);
		ScrNear(*this, TEXT("slider w"), Sl->Size.X, 100.f);
	}
	if (const FSSXFeDrawCmd* X = F.Spr(IconCross)) { TestTrue(TEXT("play video enabled for a done trick"), ScrSameColor(X->Color, White)); }
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Cross);
	S = F.FE.Top();
	TestEqual(TEXT("X on a done trick: video k = 0"), S->BookVideo, 0);
	TestEqual(TEXT("X on a done trick: picture sprite 0x7B"), S->BookPicSprite, SprVideo);
	TestTrue(TEXT("X on a done trick: accept sound 4"), F.HasHook(TEXT("ui4")));
	F.Press(ESSXFeKey::Cross);
	TestEqual(TEXT("second X: the stream stays"), F.FE.Top()->BookVideo, 0);
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Right);
	S = F.FE.Top();
	TestEqual(TEXT("Right: trick 1"), S->BookTrick, 1);
	TestEqual(TEXT("Right: vfunc_23 released the video"), S->BookVideo, -1);
	TestEqual(TEXT("Right: picture = trick 1's icon"), S->BookPicSprite, 0x7C + 1);
	TestTrue(TEXT("move sound 4"), F.HasHook(TEXT("ui4")));
	TestTrue(TEXT("picture takes the disabled colour"), ScrSameColor(S->BookPicColor, IconDisabled));
	F.Redraw();
	if (const FSSXFeDrawCmd* X = F.Spr(IconCross)) { TestTrue(TEXT("play video greyed for a trick not done"), ScrSameColor(X->Color, Grey)); }
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Cross);
	TestTrue(TEXT("X on a trick not done: refused sound 7"), F.HasHook(TEXT("ui7")));
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Triangle);
	S = F.FE.Top();
	TestTrue(TEXT("Back: still the TrickBook"), S && S->Screen == ESSXFeScreen::TrickBook);
	TestFalse(TEXT("Back: chapter list focused"), S->bBookTricks);
	TestTrue(TEXT("Back: trick name cleared"), S->BookTrickName.IsEmpty());
	TestEqual(TEXT("Back: picture keeps the trick's sprite"), S->BookPicSprite, 0x7C + 1);
	TestEqual(TEXT("Back: help 3545"), F.FE.CurrentHelpLine(), F.L(3545));
	TestTrue(TEXT("Back sound 6"), F.HasHook(TEXT("ui6")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIIconFlagsTest, "SSX.UI.IconFlags", ScrTestFlags)
bool FSSXUIIconFlagsTest::RunTest(const FString& Parameters)
{
	// FUN_001FC178
	auto P = [](float X, float Y, float W, float H, uint32 F) { return FSSXFeDrawList::IconOrigin(X, Y, W, H, F); };
	TestEqual(TEXT("flags 0"), P(10, 20, 30, 40, 0), FVector2f(10, 20));
	TestEqual(TEXT("flags 4 (bottom)"), P(10, 20, 30, 40, 4), FVector2f(10, -20));
	TestEqual(TEXT("flags 8 (right)"), P(10, 20, 30, 40, 8), FVector2f(-20, 20));
	TestEqual(TEXT("flags 0x60 (centre)"), P(320, 205, 257, 158, 0x60), FVector2f(191.5f, 126.f));
	TestEqual(TEXT("flags 0x18 (rotated, right edge = x - h)"), P(191.5f, 126, 79, 7, 0x18), FVector2f(184.5f, 126));
	TestEqual(TEXT("flags 0x14 (rotated, bottom = y - w)"), P(0, 100, 79, 7, 0x14), FVector2f(0, 21));
	if (!FScrAssets::Get().bSprites) { SSXTest::Skip(*this, TEXT("fe_sprites.json missing; draw checks skipped")); return true; }
	FSSXFeDrawList D(&FScrAssets::Get().A);
	D.Icon(41, 448.5f, 126.f, 79.f, 7.f, 0x12, FLinearColor::Black, 0);
	if (TestEqual(TEXT("one command"), D.Cmds.Num(), 1))
	{
		const FSSXFeDrawCmd& C = D.Cmds[0];
		const FSSXSprite* S = FScrAssets::Get().Sprites.Find(41);
		TestTrue(TEXT("rotated"), C.bRot90);
		TestEqual(TEXT("rotated size = (h, w)"), C.Size, FVector2f(7.f, 79.f));
		if (S) { TestTrue(TEXT("flag 2 flips v"), FMath::IsNearlyEqual(C.UV0.Y, S->V1) && FMath::IsNearlyEqual(C.UV1.Y, S->V0)); }
	}
	D.Reset();
	D.Icon(41, 191.5f, 126.f, 128.5f, 7.f, 0x05, FLinearColor::Black, 0);
	if (TestEqual(TEXT("one command (0x05)"), D.Cmds.Num(), 1))
	{
		const FSSXFeDrawCmd& C = D.Cmds[0];
		const FSSXSprite* S = FScrAssets::Get().Sprites.Find(41);
		TestFalse(TEXT("not rotated"), C.bRot90);
		TestEqual(TEXT("bottom edge at 126"), C.Pos, FVector2f(191.5f, 119.f));
		if (S) { TestTrue(TEXT("flag 1 mirrors u"), FMath::IsNearlyEqual(C.UV0.X, S->U1) && FMath::IsNearlyEqual(C.UV1.X, S->U0)); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIStartScreenTest, "SSX.UI.StartScreen", ScrTestFlags)
bool FSSXUIStartScreenTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; StartScreen checks skipped")); return true; }
	FScrFe F(true);
	TestTrue(TEXT("cold boot on the card check"), F.FE.Boot == ESSXFeBoot::StartScreen);
	F.Redraw();
	TArray<FString> Lines;
	F.L(376).ParseIntoArray(Lines, TEXT("\n"), false);
	TestEqual(TEXT("LOC 376 has three lines"), Lines.Num(), 3);
	const float Pitch = F.D.FontHeight(EFont::Menu, 1.f) + 7.f;
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		const FSSXFeTextRec* R = F.Txt(Lines[I]);
		if (!TestNotNull(FString::Printf(TEXT("box line %d"), I), R)) continue;
		const float W = F.D.TextInkWidth(EFont::Menu, Lines[I], 1.f);
		ScrNear(*this, FString::Printf(TEXT("line %d centred on 320"), I), R->Pos.X + W * 0.5f, 320.f);
		ScrNear(*this, FString::Printf(TEXT("line %d y"), I), R->Pos.Y, 96.f + I * Pitch);
		TestFalse(FString::Printf(TEXT("line %d no shadow"), I), R->bShadow);
	}
	TestEqual(TEXT("no buttons while checking"), F.SprCount(IconCross) + F.SprCount(IconTriangle), 0);
	F.D.Reset();
	F.FE.DrawStartBox(F.D, F.L(377), 5, true);
	const float W341 = float(int32(F.D.TextInkWidth(EFont::Menu, F.L(341), 1.f)));
	const float W442 = float(int32(F.D.TextInkWidth(EFont::Menu, F.L(442), 1.f)));
	const float X0 = 320.f - (W341 + W442 + 70.f + 25.f) * 0.5f;
	if (const FSSXFeDrawCmd* A = F.Spr(IconCross); TestNotNull(TEXT("continue icon 0x51"), A))
	{
		ScrNear(*this, TEXT("first button x"), A->Pos.X, X0); ScrNear(*this, TEXT("button row y 340"), A->Pos.Y, 340.f);
	}
	if (const FSSXFeDrawCmd* B = F.Spr(IconTriangle); TestNotNull(TEXT("retry icon 0x50"), B))
	{
		ScrNear(*this, TEXT("second button x = first + ink + 60"), B->Pos.X, X0 + W341 + 60.f);
	}
	if (const FSSXFeTextRec* T = F.Txt(F.L(341)); TestNotNull(TEXT("continue label"), T))
	{
		ScrNear(*this, TEXT("label at icon + 35"), T->Pos.X, X0 + 35.f);
		TestFalse(TEXT("label without shadow"), T->bShadow);
	}
	F.D.Reset();
	F.FE.DrawStartBox(F.D, F.L(358), 0x22, false);
	if (const FSSXFeDrawCmd* A = F.Spr(IconCross); TestNotNull(TEXT("stacked first icon"), A)) { TestEqual(TEXT("stacked first at (160, 340)"), A->Pos, FVector2f(160.f, 340.f)); }
	if (const FSSXFeDrawCmd* B = F.Spr(IconCircle); TestNotNull(TEXT("stacked retry icon 0x4F"), B)) { TestEqual(TEXT("stacked second at (160, 370)"), B->Pos, FVector2f(160.f, 370.f)); }
	TestNotNull(TEXT("LOC 3226 label"), F.Txt(F.L(3226)));
	return true;
}

// FUN_001D2D50 / 0x1CEF78
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIMemCardPortsTest, "SSX.UI.MemCardPorts", ScrTestFlags)
bool FSSXUIMemCardPortsTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; memory-card checks skipped")); return true; }
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardPorts"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	const FSSXLocStrings& L = ScrLoc();
	FSSXProfile P = FSSXProfile::MakeDefault();
	{
		FSSXMemCard W;
		W.Begin(FSSXMemCard::EMode::Game, true, &P);
		for (int32 N = 0; N < 400 && W.State != FSSXMemCard::SaveList; ++N) W.Tick();
		W.Input(FSSXMemCard::InAccept);
		for (int32 N = 0; N < 400 && W.State != FSSXMemCard::SaveDone; ++N) W.Tick();
		TestEqual(TEXT("settings saved"), (int32)W.State, (int32)FSSXMemCard::SaveDone);
	}
	TArray<uint8> Rep;
	Rep.SetNumZeroed(SSXReplayFile::FileBytes);
	{
		SSXReplayFile::FSetup Su;
		Su.Venue = 1;
		Su.GameMode = 2;
		SSXReplayFile::EncodeSetup(Su, Rep.GetData());
		const FString Path = FSSXMemCard::FilePath(FSSXMemCard::ReplayName(0));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Rep, *Path);
	}
	const FString NoCard = L.ByIndex(366);
	const FString Empty = L.ByIndex(351);
	struct FItem { const TCHAR* Name; FSSXMemCard::EMode Mode; bool bSave; uint32 Legend; };
	const FItem Items[] = { { TEXT("save"), FSSXMemCard::EMode::Game, true, 0xB00u }, { TEXT("load"), FSSXMemCard::EMode::Game, false, 0x500u },
		{ TEXT("load replay"), FSSXMemCard::EMode::Replay, false, 0x500u } };
	for (const FItem& It : Items)
	{
		FSSXMemCard M;
		M.Begin(It.Mode, It.bSave, &P);
		TestFalse(FString::Printf(TEXT("%s: no port menu (vfunc_50 before the checking)"), It.Name), M.bPortMenu);
		TestEqual(FString::Printf(TEXT("%s: the list's legend from the start"), It.Name), M.HelpMask(), It.Legend);
		int32 Frames = 0, NoCardFrames = 0, PreList = 0, BadPreList = 0;
		bool bSaw64 = false, bSaw416 = false;
		while (Frames < 600)
		{
			FSSXFeDrawList D(&FScrAssets::Get().A);
			M.DrawFe(D, L, Frames);
			const FString Free = L.ByIndex(312).Replace(TEXT("%d"), *FString::FromInt(M.FreeKb()));
			bool bFree = false, bTitle = false;
			int32 Rows351 = 0;
			for (const FSSXFeTextRec& R : D.TextRecs)
			{
				NoCardFrames += R.Str.Contains(NoCard) ? 1 : 0;
				bFree |= R.Str == Free;
				bTitle |= R.Str == L.ByIndex(3761);
				Rows351 += R.Str == Empty ? 1 : 0;
			}
			const bool bPre = M.State == FSSXMemCard::Checking || M.State == FSSXMemCard::CheckDone || M.State == FSSXMemCard::Reading;
			if (bPre)
			{
				++PreList;
				bSaw64 |= M.MessageLoc() == 64;
				bSaw416 |= M.MessageLoc() == 416;
				BadPreList += (bFree || !bTitle || Rows351 != M.MaxFiles() || M.LegendDisabledMask() != It.Legend) ? 1 : 0;
			}
			if (M.ListFocused())
			{
				TestTrue(FString::Printf(TEXT("%s: free KB detail back with the list"), It.Name), bFree);
				TestTrue(FString::Printf(TEXT("%s: slot 1 named with the list"), It.Name), M.FileTitle(L, 0) != Empty && Rows351 == M.MaxFiles() - 1);
				break;
			}
			M.Tick();
			++Frames;
		}
		TestEqual(FString::Printf(TEXT("%s: list after 180 + 1 + 90 updates"), It.Name), Frames, 271);
		TestTrue(FString::Printf(TEXT("%s: checking 64 and reading 416 shown"), It.Name), bSaw64 && bSaw416);
		TestEqual(FString::Printf(TEXT("%s: pre-list frames"), It.Name), PreList, 271);
		TestEqual(FString::Printf(TEXT("%s: pre-list: rows 351, 0x15 shown, 0x16 hidden, legend greyed"), It.Name), BadPreList, 0);
		TestEqual(FString::Printf(TEXT("%s: LOC 366 never drawn"), It.Name), NoCardFrames, 0);
	}
	FSSXMemCard::RootOverride().Reset();
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIProfilePagesTest, "SSX.UI.ProfilePages", ScrTestFlags)
bool FSSXUIProfilePagesTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	using namespace SSXFeRiderProfile;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; profile page checks skipped")); return true; }
	FScrFe F;
	F.Profile.Slot[0].Char = 0;
	FSSXCareer& C = F.Profile.Career[0];
	C.BestTime[0] = 12345;
	C.Tracks[0].RaceMedal = SSXProg::Gold;
	C.Tracks[1].RaceMedal = SSXProg::Silver;
	C.BestScore[0] = 4321;
	C.Tracks[0].ShowoffMedal = SSXProg::Bronze;
	C.Bests[0].Value = 5; C.Bests[0].Track = 2;
	const float Air = 3.5f; FMemory::Memcpy(&C.Bests[4].Value, &Air, 4); C.Bests[4].Track = 1;
	C.EventCount[3] = 4; C.EventCount[5] = 4;
	F.FE.GoTo(ESSXFeScreen::RiderProfile, 0);
	F.Settle();
	F.Press(ESSXFeKey::Right); F.Press(ESSXFeKey::Right);
	if (!TestEqual(TEXT("WC Status page"), F.FE.Top()->PageSel, 2)) return false;
	F.Redraw();
	const float Pitch = FMath::Max(F.D.FontHeight(EFont::Menu, WcScale), WcMedalH) + WcSpacing;
	if (const FSSXFeTextRec* N = F.Txt(F.L(3613)); TestNotNull(TEXT("garibaldi row"), N))
	{
		ScrNear(*this, TEXT("venue name right edge 320"), N->Pos.X + F.D.TextBoxWidth(EFont::Menu, F.L(3613), WcScale), 320.f);
		ScrNear(*this, TEXT("first row y 150"), N->Pos.Y, 150.f);
		TestTrue(TEXT("venue name black, no shadow"), ScrSameColor(N->Color, Black) && !N->bShadow);
	}
	if (const FSSXFeTextRec* T = F.Txt(TEXT("2:03.45")); TestNotNull(TEXT("best time 2:03.45"), T)) { ScrNear(*this, TEXT("time x 335"), T->Pos.X, 335.f); }
	if (const FSSXFeDrawCmd* G = F.Spr(SprMedalGold); TestNotNull(TEXT("gold medal 0x3A"), G))
	{
		ScrNear(*this, TEXT("medal x 420"), G->Pos.X, 420.f); ScrNear(*this, TEXT("medal y"), G->Pos.Y, 150.f);
		TestEqual(TEXT("medal 20 x 15"), G->Size, FVector2f(20.f, 15.f));
	}
	if (const FSSXFeDrawCmd* Sv = F.Spr(SprMedalSilver); TestNotNull(TEXT("silver medal row 2"), Sv)) { ScrNear(*this, TEXT("silver y"), Sv->Pos.Y, 150.f + Pitch); }
	if (const FSSXFeTextRec* P = F.Txt(TEXT("4321")); TestNotNull(TEXT("showoff points"), P)) { ScrNear(*this, TEXT("points x 470"), P->Pos.X, 470.f); }
	if (const FSSXFeDrawCmd* B = F.Spr(SprMedalBronze); TestNotNull(TEXT("showoff bronze"), B)) { ScrNear(*this, TEXT("showoff medal x 555"), B->Pos.X, 555.f); }
	TestNotNull(TEXT("header RACE"), F.Txt(F.L(3184)));
	TestNotNull(TEXT("header SHOWOFF"), F.Txt(F.L(3185)));
	{
		const int32 Rank = SSXFeTables::RankFromPoints(C.RankPoints());
		if (const FSSXFeTextRec* R = F.Txt(F.L(23 + Rank)); TestNotNull(TEXT("rank item"), R))
		{
			TestTrue(TEXT("current rank black"), ScrSameColor(R->Color, Black));
			ScrNear(*this, TEXT("rank right edge 165"), R->Pos.X + F.D.TextBoxWidth(EFont::Title, F.L(23 + Rank), RankScale), 165.f);
		}
		const FSSXFeDrawCmd* Cur = nullptr;
		for (const FSSXFeDrawCmd& Cm : F.D.Cmds) { if (Cm.bGsUntextured && Cm.Size == FVector2f(RankCursorW, RankCursorH)) Cur = &Cm; }
		if (TestNotNull(TEXT("rank cursor 180 x 30"), Cur)) { ScrNear(*this, TEXT("cursor y"), Cur->Pos.Y, RankY + Rank * (RankCellH + RankSpacing) - 2.f); }
	}
	F.Press(ESSXFeKey::Right);
	if (!TestEqual(TEXT("Rider's Best page"), F.FE.Top()->PageSel, 3)) return false;
	F.Redraw();
	const float Bp = F.D.FontHeight(EFont::Menu, WcScale) + BestSpacing;
	if (const FSSXFeTextRec* L0 = F.Txt(F.L(3168)); TestNotNull(TEXT("knockdowns label"), L0))
	{
		ScrNear(*this, TEXT("label right edge 390"), L0->Pos.X + F.D.TextBoxWidth(EFont::Menu, F.L(3168), WcScale), 390.f);
		ScrNear(*this, TEXT("first row y 135"), L0->Pos.Y, 135.f);
	}
	if (const FSSXFeTextRec* V = F.Txt(TEXT("5")); TestNotNull(TEXT("knockdowns value 5"), V)) { ScrNear(*this, TEXT("value x 405"), V->Pos.X, 405.f); }
	if (!SSXFeTables::TrackTableName(2).IsEmpty())
	{
		if (const FSSXFeTextRec* W = F.Txt(SSXFeTables::TrackTableName(2)); TestNotNull(TEXT("venue column"), W)) { ScrNear(*this, TEXT("venue x 500"), W->Pos.X, 500.f); }
		const FString Fav = FString::Printf(TEXT("%s (%d)"), *SSXFeTables::TrackTableName(3), 4);
		if (const FSSXFeTextRec* Fr = F.Txt(Fav); TestNotNull(TEXT("favourite venue = first maximum"), Fr)) { ScrNear(*this, TEXT("favourite row 13"), Fr->Pos.Y, 135.f + 13 * Bp); }
	}
	if (const FSSXFeTextRec* A = F.Txt(TEXT("0:03.50")); TestNotNull(TEXT("air time 0:03.50"), A)) { ScrNear(*this, TEXT("air time row 10"), A->Pos.Y, 135.f + 10 * Bp); }
	TestNotNull(TEXT("header RESULTS"), F.Txt(F.L(3181)));
	TestNotNull(TEXT("black tricks row (LOC 3506)"), F.Txt(F.L(3506)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIRiderBestAfterRaceTest, "SSX.UI.RiderBestAfterRace", ScrTestFlags)
bool FSSXUIRiderBestAfterRaceTest::RunTest(const FString& Parameters)
{
	// 0x167010 / 0x166CB0 / 0x164FC0 / 0x166FA0
	using namespace SSXFe;
	using namespace SSXFeRiderProfile;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; profile page checks skipped")); return true; }
	constexpr int32 Snow = 1;
	FSSXProfile Played = FSSXProfile::MakeDefault();
	FSSXCareer& C = Played.Career[0];
	SSXProg::CountEvent(C, Snow);
	FSSXRaceStatBlock B;
	B.Tricks = 17; B.AirBest = 3.25f;
	SSXProg::CareerBests(C, B, Snow);
	FSSXProgressSession Sess;
	FSSXEventResult Ev;
	Ev.Mode = SSXProg::WCRace; Ev.Venue = Snow;
	Ev.Humans.Add({ 0, 0, 0, 17000, 0, TEXT("PLAYER1") });
	SSXProg::EndEvent(Played, Sess, Ev, false);
	FScrFe F;
	if (!TestTrue(TEXT("saved profile loads"), F.Profile.LoadBlob(Played.SaveBlob()))) return false;
	F.Profile.Slot[0].Char = 0;
	F.FE.GoTo(ESSXFeScreen::RiderProfile, 0);
	F.Settle();
	F.Press(ESSXFeKey::Right); F.Press(ESSXFeKey::Right);
	if (!TestEqual(TEXT("WC Status page"), F.FE.Top()->PageSel, 2)) return false;
	F.Redraw();
	TestNotNull(TEXT("WC Status: Snowdream best 2:50.00"), F.Txt(TEXT("2:50.00")));
	F.Press(ESSXFeKey::Right);
	if (!TestEqual(TEXT("Rider's Best page"), F.FE.Top()->PageSel, 3)) return false;
	F.Redraw();
	TestNotNull(TEXT("Rider's Best: tricks 17"), F.Txt(TEXT("17")));
	TestNotNull(TEXT("Rider's Best: air 0:03.25"), F.Txt(TEXT("0:03.25")));
	if (!SSXFeTables::TrackTableName(Snow).IsEmpty())
	{
		TestNotNull(TEXT("Rider's Best: venue column Snowdream"), F.Txt(SSXFeTables::TrackTableName(Snow)));
		TestNotNull(TEXT("favourite venue Snowdream (1)"), F.Txt(FString::Printf(TEXT("%s (1)"), *SSXFeTables::TrackTableName(Snow))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIVenueVideoTest, "SSX.UI.VenueVideo", ScrTestFlags)
bool FSSXUIVenueVideoTest::RunTest(const FString& Parameters)
{
	// FUN_00263000 / 0x3471D0
	TestEqual(TEXT("file 0"), SSXFeVenueVideo::FileName(0), FString(TEXT("GARI.SSS")));
	TestEqual(TEXT("file 8"), SSXFeVenueVideo::FileName(8), FString(TEXT("UNTRACK.SSS")));
	TestEqual(TEXT("file 9"), SSXFeVenueVideo::FileName(9), FString(TEXT("PIPE.SSS")));
	TestTrue(TEXT("no file 10"), SSXFeVenueVideo::FileName(10).IsEmpty());
	FScrFe F;
	F.FE.GoTo(ESSXFeScreen::VenueSel);
	F.Settle();
	const FSSXFeState* S = F.FE.Top();
	if (!TestNotNull(TEXT("VenueSel"), S) || !S->Selected()) return false;
	TestEqual(TEXT("open: the selected open venue plays"), F.FE.VenueVideo, S->Selected()->bEnabled ? S->Sel : -1);
	int32 Serial = F.FE.VenueVideoSerial;
	F.Press(ESSXFeKey::Right);
	S = F.FE.Top();
	TestEqual(TEXT("highlight: the new screen (if open)"), F.FE.VenueVideo, S->Selected()->bEnabled ? S->Sel : -1);
	TestTrue(TEXT("highlight restarts the stream"), F.FE.VenueVideoSerial > Serial);
	for (int32 I = 0; I < S->Items.Num(); ++I)
	{
		if (!S->Items[I].bEnabled)
		{
			F.FE.TopMut()->Sel = (I + S->Items.Num() - 1) % S->Items.Num();
			F.Press(ESSXFeKey::Right);
			if (F.FE.Top()->Sel == I) { TestEqual(TEXT("locked venue: no video"), F.FE.VenueVideo, -1); }
			break;
		}
	}
	for (int32 I = 0; I < S->Items.Num(); ++I)
	{
		if (S->Items[I].bEnabled)
		{
			F.FE.TopMut()->Sel = (I + S->Items.Num() - 1) % S->Items.Num();
			F.Press(ESSXFeKey::Right);
			break;
		}
	}
	TestTrue(TEXT("an open venue plays"), F.FE.VenueVideo >= 0);
	F.Press(ESSXFeKey::Triangle);
	for (int32 N = 0; N < 600 && F.FE.Current() == ESSXFeScreen::VenueSel; ++N) F.FE.Tick();
	TestNotEqual(TEXT("Back left VenueSel"), (int32)F.FE.Current(), (int32)ESSXFeScreen::VenueSel);
	TestEqual(TEXT("vfunc_16 on the pop: videos off"), F.FE.VenueVideo, -1);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetTransientPackage());
		C->SetStaticMesh(Cube);
		USSXPropAnimation::SetFlatVertexColour(C, true);
		TestTrue(TEXT("flat vertex colour set"), USSXPropAnimation::HasFlatVertexColour(C));
		if (USSXPropAnimation::HasFlatVertexColour(C))
		{
			TestEqual(TEXT("every vertex 128"), C->LODData[0].OverrideVertexColors->VertexColor(0), FColor(128, 128, 128, 128));
		}
		USSXPropAnimation::SetFlatVertexColour(C, false);
		TestFalse(TEXT("baked colours back"), USSXPropAnimation::HasFlatVertexColour(C));
	}
	if (!FPaths::FileExists(SSXFeVenueVideo::Path(0)))
	{
		SSXTest::Skip(*this, TEXT("extracted/DATA/VIDEO/GARI.SSS missing: stream checks skipped"));
		return true;
	}
	SSXFeVenueVideo::FVideo V;
	int32 Frames = 0;
	for (int32 N = 1; N <= 12; ++N)
	{
		const bool bNew = V.Step(0, 1);
		Frames += bNew;
		TestEqual(*FString::Printf(TEXT("step %d decodes"), N), bNew, N % 4 == 0);
	}
	TestEqual(TEXT("3 frames in 12 steps"), Frames, 3);
	TestEqual(TEXT("128 wide"), V.W, SSXFeVenueVideo::Size);
	TestEqual(TEXT("128 high"), V.H, SSXFeVenueVideo::Size);
	TestEqual(TEXT("frame 2 shown"), V.Stream.Shown, 2);
	V.Step(0, 2);   // FUN_00263000
	for (int32 N = 0; N < 3; ++N) V.Step(0, 2);
	TestEqual(TEXT("restart: frame 0 again"), V.Stream.Shown, 0);
	V.Step(-1, 3);
	TestEqual(TEXT("stopped"), V.Cur, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIRankVideoFeTest, "SSX.UI.RankVideoFe", ScrTestFlags)
bool FSSXUIRankVideoFeTest::RunTest(const FString& Parameters)
{
	// 0x28C128 / 0x28BE20
	for (const bool bMaster : { false, true })
	{
		FScrFe F;
		F.Profile.Slot[0].Char = 3;
		F.Profile.Career[3].Points[0] = bMaster ? 240 : 0;
		F.Profile.Career[3].Points[1] = 0;
		F.FE.GoTo(ESSXFeScreen::RiderProfile, 0);
		F.Settle();
		F.Press(ESSXFeKey::Right); F.Press(ESSXFeKey::Right);
		if (!TestEqual(TEXT("WC Status page"), F.FE.Top()->PageSel, 2)) return false;
		F.FE.Key(0, ESSXFeKey::Cross);
		F.FE.Tick();
		TestEqual(bMaster ? TEXT("Master: rr_mac") : TEXT("not Master: nothing"), F.FE.MovieRequest, FString(bMaster ? TEXT("RR_MAC") : TEXT("")));
		if (bMaster) { TestTrue(TEXT("X / Start skip"), F.FE.bMovieSkippable); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUISceneEventsTest, "SSX.UI.SceneEvents", ScrTestFlags)
bool FSSXUISceneEventsTest::RunTest(const FString& Parameters)
{
	// FUN_0013BAD8 / 0x3A8D00 / FUN_00262960 / 0x3A8CA0
	SSXEffects::FEffectData Fx;
	if (!Fx.Load(SSXEffects::FEffectData::DefaultPath(TEXT("ssxfe"))))
	{
		SSXTest::Skip(*this, TEXT("SimData/ssxfe.effects.bin not exported (tools/export_effects.py): scene function checks skipped"));
		return true;
	}
	for (const TCHAR* N : { TEXT("FEStartScript"), TEXT("onTitleOpen"), TEXT("onTitleStop") })
	{
		TestTrue(*FString::Printf(TEXT("%s in ssxfe.ssf"), N), Fx.FunctionIndex(N) >= 0);
	}
	for (int32 I = 0; I < 10; ++I)
	{
		const FString N = FString::Printf(TEXT("FE%s1"), SSXFeCams::VenueEventName(I));
		TestTrue(*FString::Printf(TEXT("%s in ssxfe.ssf"), *N), Fx.FunctionIndex(N) >= 0);
	}
	FScrFe F;
	F.FE.GoTo(ESSXFeScreen::ModeSelect);
	F.Settle();
	F.Press(ESSXFeKey::Left);
	F.Press(ESSXFeKey::Right);
	F.Press(ESSXFeKey::Right);
	TestTrue(TEXT("ModeSelect fired door events"), F.FE.SceneEvents.Num() >= 4);
	for (const FString& E : F.FE.SceneEvents)
	{
		TestTrue(*FString::Printf(TEXT("fired %s exists"), *E), Fx.FunctionIndex(E) >= 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIDvdContentTest, "SSX.UI.DvdContent", ScrTestFlags)
bool FSSXUIDvdContentTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	using EL = FSSXFeDvd::EList;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; DVD checks skipped")); return true; }
	FScrFe F;
	F.FE.GoTo(ESSXFeScreen::DVDContent, 0);
	auto Dvd = [&F]() -> const FSSXFeDvd& { return F.FE.Top()->Dvd; };
	auto Idle = [&F, &Dvd]() { for (int32 N = 0; N < 1200 && (Dvd().Phase != 0 || Dvd().Focus == FSSXFeDvd::None || Dvd().TitleSlide.bActive || Dvd().VoiceSlide.bActive); ++N) F.FE.Tick(); };
	auto Key = [&F, &Idle](ESSXFeKey K) { F.FE.Key(0, K); F.FE.Tick(); Idle(); };
	TestEqual(TEXT("opening phase 1"), Dvd().Phase, 1);
	TestFalse(TEXT("no Square"), F.FE.Top()->bSquare);
	F.FE.Tick(); F.FE.Tick();
	TestTrue(TEXT("item 0 slides first"), Dvd().Lists[EL::Main].Items[0].Slide.bActive);
	TestFalse(TEXT("item 1 waits for item 0's half"), Dvd().Lists[EL::Main].Items[1].Slide.bActive);
	F.FE.SoundHooks.Reset();
	Idle();
	TestEqual(TEXT("idle after the slide-in"), Dvd().Phase, 0);
	TestEqual(TEXT("main list focused"), (int32)Dvd().Focus, (int32)EL::Main);
	TestTrue(TEXT("focus gain: speech request 0x3A"), F.HasHook(TEXT("speech58")));
	F.Redraw();
	if (const FSSXFeDrawCmd* B = F.Spr(FSSXFeDvd::SprBackground); TestNotNull(TEXT("background sprite 47"), B)) { TestEqual(TEXT("background 640 x 480"), B->Size, FVector2f(640.f, 480.f)); }
	if (const FSSXFeDrawCmd* Lg = F.Spr(FSSXFeDvd::SprLogo); TestNotNull(TEXT("logo sprite 48"), Lg)) { TestEqual(TEXT("logo at (15, 10)"), Lg->Pos, FVector2f(15.f, 10.f)); TestEqual(TEXT("logo 145 x 85"), Lg->Size, FVector2f(145.f, 85.f)); }
	TestEqual(TEXT("four pulse icons"), F.SprCount(49) + F.SprCount(50) + F.SprCount(51) + F.SprCount(52), 4);
	const float Pitch = F.D.FontHeight(EFont::Menu, 0.9f) + ShadowOffset + 10.f;
	for (int32 K = 0; K < 9; ++K)
	{
		const FSSXFeTextRec* R = F.Txt(F.L(SSXFeDvdTables::MainLoc(K)));
		if (!TestNotNull(FString::Printf(TEXT("main item %d"), K), R)) continue;
		ScrNear(*this, FString::Printf(TEXT("item %d x 100"), K), R->Pos.X, 100.f);
		ScrNear(*this, FString::Printf(TEXT("item %d y"), K), R->Pos.Y, 132.f + K * Pitch);
		TestTrue(FString::Printf(TEXT("item %d black"), K), ScrSameColor(R->Color, Black));
	}
	{
		const FString T = F.L(3315);
		const FSSXFeTextRec* A = F.Txt(T, 0);
		const FSSXFeTextRec* B = F.Txt(T, 1);
		if (TestNotNull(TEXT("title"), A) && TestNotNull(TEXT("title copy"), B))
		{
			const float W = F.D.TextBoxWidth(EFont::Title, T, 1.f);
			ScrNear(*this, TEXT("title right edge 595"), A->Pos.X + W, 595.f);
			ScrNear(*this, TEXT("copy 20 px right"), B->Pos.X - A->Pos.X, 20.f);
			TestTrue(TEXT("copy alpha 0.2"), FMath::IsNearlyEqual(B->Color.A, 0.2f));
			TestFalse(TEXT("title without shadow"), A->bShadow);
		}
	}
	{
		int32 Chev = 0;
		for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.SpriteId == FSSXFeDvd::SprChevron) { if (Chev == 0) ScrNear(*this, TEXT("first chevron x = item x - 80"), C.Pos.X, 20.f); ++Chev; } }
		TestEqual(TEXT("five chevrons"), Chev, 5);
	}
	if (const FSSXFeDrawCmd* Tl = F.Spr(0x68); TestNotNull(TEXT("DVD tile 0x68"), Tl)) { TestEqual(TEXT("tile at (355, 124)"), Tl->Pos, FVector2f(355.f, 124.f)); TestEqual(TEXT("tile 262 x 224"), Tl->Size, FVector2f(262.f, 224.f)); }
	if (const FSSXFeTextRec* Sel = F.Txt(F.L(541)); TestNotNull(TEXT("legend select"), Sel))
	{
		TestFalse(TEXT("legend label without shadow"), Sel->bShadow);
		ScrNear(*this, TEXT("legend right edge 595"), Sel->Pos.X + F.D.TextWidth(EFont::Menu, F.L(541), HelpLabelScale), 595.f);
	}
	TestNull(TEXT("no play video entry on the main list"), F.Txt(F.L(3326)));
	F.FE.SoundHooks.Reset();
	Key(ESSXFeKey::Down);
	TestTrue(TEXT("move sound 4"), F.HasHook(TEXT("ui4")));
	TestTrue(TEXT("TRICKS speech 0x3C"), F.HasHook(TEXT("speech60")));
	TestEqual(TEXT("tile 0x6A for TRICKS"), Dvd().TileSprite, 0x6A);
	Key(ESSXFeKey::Cross);
	TestEqual(TEXT("TRICKS plays tricks.mpc"), F.FE.MovieRequest, FString(TEXT("TRICKS")));
	// 0x264FD8
	F.Redraw();
	TestEqual(TEXT("movie: no FE sprites"), F.D.Cmds.Num(), 0);
	TestEqual(TEXT("movie: no FE texts"), F.D.TextRecs.Num(), 0);
	F.FE.MovieFinished();
	Key(ESSXFeKey::Down); Key(ESSXFeKey::Down); Key(ESSXFeKey::Down);
	TestEqual(TEXT("item 4 selected"), Dvd().Lists[EL::Main].Sel, 4);
	F.FE.SoundHooks.Reset();
	F.FE.Key(0, ESSXFeKey::Cross);
	TestEqual(TEXT("X: phase 2"), Dvd().Phase, 2);
	TestTrue(TEXT("X: play(1)"), F.HasHook(TEXT("ui1")));
	TestFalse(TEXT("X: no accept sound byte on the main list"), F.HasHook(TEXT("ui5")));
	Idle();
	TestEqual(TEXT("voices focused"), (int32)Dvd().Focus, (int32)EL::Voices);
	TestTrue(TEXT("voices open: speech 0x3E"), F.HasHook(TEXT("speech62")));
	F.Redraw();
	if (const TArray<FString>& Nm = SSXFeTables::StringTable(TEXT("dvd_char_names")); Nm.Num() > 0)
	{
		if (const FSSXFeTextRec* E = F.Txt(Nm[0]); TestNotNull(TEXT("EDDIE WACHOWSKI first"), E)) { ScrNear(*this, TEXT("voices x 100"), E->Pos.X, 100.f); ScrNear(*this, TEXT("voices y 132"), E->Pos.Y, 132.f); }
		if (const FSSXFeTextRec* V = F.Txt(F.L(3536)); TestNotNull(TEXT("VOICE BY"), V)) { ScrNear(*this, TEXT("VOICE BY x 20"), V->Pos.X, 20.f); }
	}
	TestNotNull(TEXT("title CHARACTER VOICES"), F.Txt(F.L(3320)));
	TestNotNull(TEXT("portrait 0x71 (Eddie)"), F.Spr(0x71));
	TestNull(TEXT("tile hidden off the main list"), F.Spr(0x6C));
	F.FE.SoundHooks.Reset();
	F.FE.Key(0, ESSXFeKey::Cross);
	TestTrue(TEXT("voices accept sound 5"), F.HasHook(TEXT("ui5")));
	Idle();
	TestEqual(TEXT("talent page focused"), (int32)Dvd().Focus, (int32)EL::Talent);
	TestEqual(TEXT("legend 0xC"), Dvd().LegendMask, 0xCu);
	F.Redraw();
	TestNotNull(TEXT("play video legend"), F.Txt(F.L(3326)));
	if (const FSSXFeTextRec* H = F.Txt(TEXT("Profession")); TestNotNull(TEXT("first line 'Profession' (LOC 3379 without '~')"), H))
	{
		ScrNear(*this, TEXT("talent line x 90"), H->Pos.X, 90.f); ScrNear(*this, TEXT("talent line y 130"), H->Pos.Y, 130.f);
		TestTrue(TEXT("heading white with shadow"), ScrSameColor(H->Color, White) && H->bShadow);
	}
	if (!Dvd().TalentName.IsEmpty()) { if (const FSSXFeTextRec* N = F.Txt(Dvd().TalentName); TestNotNull(TEXT("talent name"), N)) { TestEqual(TEXT("talent name at (90, 94)"), N->Pos, FVector2f(90.f, 94.f)); } }
	{
		const FSSXFeDrawCmd* A = nullptr;
		for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.SpriteId == FSSXFeDvd::SprChevron) A = &C; }
		if (TestNotNull(TEXT("talent arrow"), A)) { ScrNear(*this, TEXT("arrow right edge 95"), A->Pos.X + A->Size.X, 95.f); }
	}
	Key(ESSXFeKey::Cross);
	TestEqual(TEXT("talent X: cv_eddi.mpc"), F.FE.MovieRequest, FString(TEXT("CV_EDDI")));
	F.FE.MovieFinished();
	F.FE.SoundHooks.Reset();
	Key(ESSXFeKey::Triangle);
	TestTrue(TEXT("talent back sound 6"), F.HasHook(TEXT("ui6")));
	TestEqual(TEXT("back to the voices"), (int32)Dvd().Focus, (int32)EL::Voices);
	TestEqual(TEXT("legend 10 again"), Dvd().LegendMask, 10u);
	Key(ESSXFeKey::Triangle);
	TestEqual(TEXT("back to the main list"), (int32)Dvd().Focus, (int32)EL::Main);
	Key(ESSXFeKey::Down); Key(ESSXFeKey::Down); Key(ESSXFeKey::Down);
	TestEqual(TEXT("item 7"), Dvd().Lists[EL::Main].Sel, 7);
	F.FE.SoundHooks.Reset();
	Key(ESSXFeKey::Cross);
	TestEqual(TEXT("jukebox focused"), (int32)Dvd().Focus, (int32)EL::Jukebox);
	TestTrue(TEXT("jukebox start + song 0"), F.HasHook(TEXT("jukeboxStart")) && F.HasHook(TEXT("jukebox0")));
	F.Redraw();
	const TArray<FString>& Titles = SSXFeTables::StringTable(TEXT("dvd_jukebox.title"));
	if (Titles.Num() == 21)
	{
		if (const FSSXFeTextRec* S0 = F.Txt(Titles[0], 0); TestNotNull(TEXT("song 0 row"), S0)) { ScrNear(*this, TEXT("column 0 x 40"), S0->Pos.X, 40.f); ScrNear(*this, TEXT("column y 160"), S0->Pos.Y, 160.f); }
		if (const FSSXFeTextRec* S11 = F.Txt(Titles[11]); TestNotNull(TEXT("song 11 row"), S11)) { ScrNear(*this, TEXT("column 1 x 380"), S11->Pos.X, 380.f); }
		TestNotNull(TEXT("CURRENTLY PLAYING:"), F.Txt(F.L(3537)));
		TestNotNull(TEXT("MUSIC BY"), F.Txt(F.L(3540)));
		Key(ESSXFeKey::Right);
		TestEqual(TEXT("Right: song 11"), Dvd().Selected() ? Dvd().Selected()->Id : -1, 11);
		Key(ESSXFeKey::Up);
		TestEqual(TEXT("Up wraps in the column: song 20"), Dvd().Selected() ? Dvd().Selected()->Id : -1, 20);
		Key(ESSXFeKey::Cross);
		TestTrue(TEXT("X plays song 20"), F.HasHook(TEXT("jukebox20")));
		F.FE.SoundHooks.Reset();
		F.FE.bJukeboxSongEnded = false;
		F.FE.Tick();
		TestFalse(TEXT("still playing: no next song"), F.HasHook(TEXT("jukebox0")));
		const FString Artist20 = Dvd().VoiceValue;
		F.FE.bJukeboxSongEnded = true;
		F.FE.Tick();
		F.FE.bJukeboxSongEnded = false;
		TestTrue(TEXT("ended: next song 0 (wraps after 20)"), F.HasHook(TEXT("jukebox0")) && Dvd().Song == 0);
		TestTrue(TEXT("song 0 selected in column 0"), Dvd().Selected() && Dvd().Selected()->Id == 0 && Dvd().Selected()->Column == 0);
		TestEqual(TEXT("no vfunc_23 highlight: the MUSIC BY value unchanged"), Dvd().VoiceValue, Artist20);
		F.FE.SoundHooks.Reset();
		F.FE.Tick();
		TestFalse(TEXT("one song per end"), F.HasHook(TEXT("jukebox1")));
		F.FE.bJukeboxSongEnded = true;
		F.FE.Tick();
		F.FE.bJukeboxSongEnded = false;
		TestTrue(TEXT("song 0 ended: song 1"), F.HasHook(TEXT("jukebox1")) && Dvd().Song == 1);
	}
	F.FE.SoundHooks.Reset();
	Key(ESSXFeKey::Triangle);
	TestTrue(TEXT("jukebox back stops the music"), F.HasHook(TEXT("jukeboxStop")));
	TestEqual(TEXT("main again"), (int32)Dvd().Focus, (int32)EL::Main);
	TestFalse(TEXT("jukebox info hidden"), Dvd().bJukeboxInfo);
	Key(ESSXFeKey::Down);
	Key(ESSXFeKey::Cross);
	TestEqual(TEXT("previews focused"), (int32)Dvd().Focus, (int32)EL::Previews);
	Key(ESSXFeKey::Down);
	Key(ESSXFeKey::Cross);
	TestEqual(TEXT("NBA STREET PREVIEW movie"), F.FE.MovieRequest, FString(TEXT("NBAPROMO")));
	F.FE.MovieFinished();
	Key(ESSXFeKey::Triangle);
	TestEqual(TEXT("main from the previews"), (int32)Dvd().Focus, (int32)EL::Main);
	F.FE.Key(0, ESSXFeKey::Triangle);
	for (int32 N = 0; N < 200 && F.FE.Current() != ESSXFeScreen::Title; ++N) F.FE.Tick();
	TestEqual(TEXT("Back on the main list: Title"), (int32)F.FE.Current(), (int32)ESSXFeScreen::Title);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIBootCardTimingTest, "SSX.UI.BootCardTiming", ScrTestFlags)
bool FSSXUIBootCardTimingTest::RunTest(const FString& Parameters)
{
	// 0x1845E0 / 0x1850E8
	const bool bFile = FPaths::FileExists(FSSXMemCard::FilePath(FSSXMemCard::SettingsName()));
	FScrFe F(true);
	FSSXFrontEnd& FE = F.FE;
	TestTrue(TEXT("cold boot on the card check"), FE.Boot == ESSXFeBoot::StartScreen);
	int32 Ticks = 0;
	int32 FirstLoading = 0;
	while (FE.Boot == ESSXFeBoot::StartScreen && Ticks < 1000)
	{
		FE.Tick();
		++Ticks;
		if (!FirstLoading && FE.BootState >= 6 && FE.BootState <= 8) { FirstLoading = Ticks; }
	}
	TestEqual(TEXT("total frames of the boot check"), Ticks, bFile ? 275 : 152);
	TestEqual(TEXT("auto-loading message only with a settings file"), FirstLoading != 0, bFile);
	if (bFile) { TestEqual(TEXT("state 6 reached on frame 1 + 75 + 2"), FirstLoading, 78); }
	TestTrue(TEXT("then the EA logo"), FE.Boot == ESSXFeBoot::EaLogo);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILoadingScreenTest, "SSX.UI.LoadingScreen", ScrTestFlags)
bool FSSXUILoadingScreenTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; loading checks skipped")); return true; }
	// 0x264FD8
	FScrFe F;
	F.FE.Boot = ESSXFeBoot::Loading;
	F.FE.BootFrame = 0;
	F.FE.Fader.Start(1.f, 10.f, 0.f, SSXFe::Black);
	for (int32 N = 0; N < 60; ++N) F.FE.Fader.Tick();
	F.Redraw();
	const FSSXFeDrawCmd* Fade = nullptr;
	for (const FSSXFeDrawCmd& C : F.D.Cmds) { if (C.bGsUntextured && C.Size == FVector2f(640.f, 480.f)) Fade = &C; }
	if (TestNotNull(TEXT("fader quad over the launch"), Fade))
	{
		TestTrue(TEXT("opaque black after the 1 s fade"), FMath::IsNearlyEqual(Fade->Color.A, 1.f, 1e-3f) && Fade->Color.R == 0.f);
	}
	TestNull(TEXT("no hint title (cFELoadFromQuit only)"), F.Txt(F.L(3327)));
	TestNull(TEXT("no loading text from the FE (the loader draws it)"), F.Txt(F.L(373)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUISoundStartupTest, "SSX.UI.SoundStartup", ScrTestFlags)
bool FSSXUISoundStartupTest::RunTest(const FString& Parameters)
{
	// 0x20DB18 / FUN_0021BCA8
	const FSSXSettings S;
	const float On = 10.f / 11.f;
	TestTrue(TEXT("master 10/11"), FMath::IsNearlyEqual(S.Master, On, 1e-6f));
	TestTrue(TEXT("music 10/11"), FMath::IsNearlyEqual(S.Music, On, 1e-6f));
	TestTrue(TEXT("sfx 10/11"), FMath::IsNearlyEqual(S.Sfx, On, 1e-6f));
	TestTrue(TEXT("PA speech 10/11"), FMath::IsNearlyEqual(S.McSpeech, On, 1e-6f));
	TestTrue(TEXT("character speech 10/11"), FMath::IsNearlyEqual(S.CharSpeech, On, 1e-6f));
	TestTrue(TEXT("+0x383C 10/11"), FMath::IsNearlyEqual(S.Volume3, On, 1e-6f));
	TestEqual(TEXT("preset 0 (Tasty Mix)"), S.Preset, 0);
	TestTrue(TEXT("a new save carries them"), FSSXSaveData::NewSave().Settings == S);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFeMenuSoundsTest, "SSX.UI.FeMenuSounds", ScrTestFlags)
bool FSSXUIFeMenuSoundsTest::RunTest(const FString&)
{
	FScrFe F;
	auto Go = [&F](ESSXFeScreen S, int32 Pads = 1) { F.FE.PadsConnected = Pads; F.FE.GoTo(S); F.Settle(); F.FE.SoundHooks.Reset(); F.FE.SceneEvents.Reset(); };
	auto Hooks = [&F]() { const TArray<FString> H = F.FE.SoundHooks; F.FE.SoundHooks.Reset(); return H; };
	using H = TArray<FString>;

	// ModeSelect (horizontal, no wrap; bytes 3,0,1,2,1,1,3): move 0 + vfunc_23 speech by group, end bump 3, Circle 1.
	Go(ESSXFeScreen::ModeSelect);
	F.Press(ESSXFeKey::Right);
	TestEqual(TEXT("ModeSelect Right: move 0 + speech 5 (WC)"), Hooks(), H({ TEXT("ui0"), TEXT("speech5") }));
	F.Press(ESSXFeKey::Right);
	TestEqual(TEXT("ModeSelect end bump: 3"), Hooks(), H({ TEXT("ui3") }));
	F.Press(ESSXFeKey::Circle);
	TestEqual(TEXT("ModeSelect Circle: +0x114 = 1"), Hooks(), H({ TEXT("ui1") }));
	F.Press(ESSXFeKey::Triangle);
	{
		const H Got = Hooks();
		TestTrue(TEXT("ModeSelect Back: 2, then vfunc_19 entry 13 = speech stop"), Got.Num() >= 2 && Got[0] == TEXT("ui2") && Got[1] == TEXT("stopSpeech"));
		TestEqual(TEXT("ModeSelect Back -> Title"), (int32)F.FE.Current(), (int32)ESSXFeScreen::Title);
	}

	// FUN_00262CD8
	F.FE.PadsConnected = 2;
	F.FE.GoTo(ESSXFeScreen::NumPlayers);
	F.FE.SoundHooks.Reset();
	F.Settle();
	TestEqual(TEXT("NumPlayers vfunc_20"), Hooks(), H({ TEXT("music1"), TEXT("speech3"), TEXT("speech13") }));
	F.Press(ESSXFeKey::Left);
	TestEqual(TEXT("NumPlayers (vertical): Left ignored"), Hooks(), H());
	F.Press(ESSXFeKey::Up);
	TestEqual(TEXT("NumPlayers Up at the top: bump 7"), Hooks(), H({ TEXT("ui7") }));

	Go(ESSXFeScreen::CharacterSelect);
	{
		FSSXFeState* S = F.FE.TopMut();
		int32 Locked = INDEX_NONE;
		for (int32 I = 0; I < S->Items.Num(); ++I) { if (!S->Items[I].bEnabled) { Locked = I; break; } }
		if (Locked != INDEX_NONE)
		{
			S->Sel = Locked;
			F.Press(ESSXFeKey::Cross);
			F.Press(ESSXFeKey::Circle);
			TestEqual(TEXT("CharacterSelect locked: X refused 3, Circle +0x116 = 3"), Hooks(), H({ TEXT("ui3"), TEXT("ui3") }));
		}
		S->Sel = F.FE.DefaultCarouselSlot;
		F.FE.Key(0, ESSXFeKey::Cross);
		const H Got = Hooks();
		TestTrue(TEXT("CharacterSelect X: 1 + speech stop"), Got.Num() >= 2 && Got[0] == TEXT("ui1") && Got[1] == TEXT("stopSpeech"));
	}

	Go(ESSXFeScreen::CharacterAccept);
	F.Press(ESSXFeKey::Right);
	F.Press(ESSXFeKey::Left);
	F.Press(ESSXFeKey::Circle);
	TestEqual(TEXT("CharacterAccept: bump 7 x2, Circle 5"), Hooks(), H({ TEXT("ui7"), TEXT("ui7"), TEXT("ui5") }));
	F.Press(ESSXFeKey::Triangle);
	{
		const H Got = Hooks();
		TestTrue(TEXT("CharacterAccept Back: 6"), Got.Num() > 0 && Got[0] == TEXT("ui6"));
		TestTrue(TEXT("CharacterSelect re-show: speech 0x10, entry 12, music 2"), Got.Contains(TEXT("speech16")) && Got.Contains(TEXT("stopAnimVoice")) && Got.Contains(TEXT("music2")));
	}

	Go(ESSXFeScreen::CharacterModify);
	F.Press(ESSXFeKey::Down);
	F.Press(ESSXFeKey::Down);
	F.Press(ESSXFeKey::Up);
	F.Press(ESSXFeKey::Up);
	F.Press(ESSXFeKey::Up);
	TestEqual(TEXT("CharacterModify moves"), Hooks(), H({ TEXT("ui4"), TEXT("speech30"), TEXT("ui4"), TEXT("speech31"), TEXT("ui4"), TEXT("speech30"),
		TEXT("ui4"), TEXT("stopSpeech"), TEXT("ui7") }));

	Go(ESSXFeScreen::EventSel);
	F.Press(ESSXFeKey::Down);
	F.Press(ESSXFeKey::Down);
	F.Press(ESSXFeKey::Down);
	TestEqual(TEXT("EventSel moves: Showoff 9, TC 0xC, bump"), Hooks(), H({ TEXT("ui4"), TEXT("speech9"), TEXT("ui4"), TEXT("speech12"), TEXT("ui7") }));
	F.Press(ESSXFeKey::Up);
	F.Press(ESSXFeKey::Up);
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Cross);
	{
		const H Got = Hooks();
		TestEqual(TEXT("-> DifficultSel"), (int32)F.FE.Current(), (int32)ESSXFeScreen::DifficultSel);
		TestTrue(TEXT("DifficultSel vfunc_15: speech 0x2B + music 3"), Got.Contains(TEXT("speech43")) && Got.Contains(TEXT("music3")));
	}
	F.Press(ESSXFeKey::Down);
	F.Press(ESSXFeKey::Down);
	TestEqual(TEXT("DifficultSel moves: 0x2D, 0x2E"), Hooks(), H({ TEXT("ui4"), TEXT("speech45"), TEXT("ui4"), TEXT("speech46") }));

	F.FE.GoTo(ESSXFeScreen::VenueSel);
	{
		const H Got = F.FE.SoundHooks;
		TestTrue(TEXT("VenueSel vfunc_15: speech 0x2F + music 3"), Got.Contains(TEXT("speech47")) && Got.Contains(TEXT("music3")));
	}
	F.Settle();
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Right);
	{
		static const int32 Ids[12] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x36, 0x39, 0x38, 0x35, -1, -1, 0x37 };
		const int32 T = F.FE.Top()->SelectedId();
		H Want({ TEXT("ui0") });
		if (T >= 0 && T < 12 && Ids[T] >= 0) { Want.Add(FString::Printf(TEXT("speech%d"), Ids[T])); }
		TestEqual(TEXT("VenueSel Right: move 0 + the venue speech (FUN_00263000)"), Hooks(), Want);
		FSSXFeState* S = F.FE.TopMut();
		for (int32 I = 0; I < S->Items.Num(); ++I)
		{
			if (!S->Items[I].bEnabled)
			{
				S->Sel = I;
				F.Press(ESSXFeKey::Circle);
				TestEqual(TEXT("VenueSel Circle on a locked venue: 3"), Hooks(), H({ TEXT("ui3") }));
				break;
			}
		}
	}

	Go(ESSXFeScreen::RiderProfile);
	F.Press(ESSXFeKey::Square);
	TestEqual(TEXT("RiderProfile Square: 5, no Options"), Hooks(), H({ TEXT("ui5") }));
	TestEqual(TEXT("still RiderProfile"), (int32)F.FE.Current(), (int32)ESSXFeScreen::RiderProfile);

	Go(ESSXFeScreen::TrickBook);
	F.Press(ESSXFeKey::Square);
	TestEqual(TEXT("TrickBook Square: 5"), Hooks(), H({ TEXT("ui5") }));
	F.FE.Key(0, ESSXFeKey::Circle);
	{
		const H Got = Hooks();
		TestTrue(TEXT("TrickBook Circle: 4 + speech stop"), Got.Num() >= 2 && Got[0] == TEXT("ui4") && Got[1] == TEXT("stopSpeech"));
		TestTrue(TEXT("TrickBook Circle: tutorial launch"), F.FE.bLaunchPending && F.FE.GameMode == 6);
	}

	Go(ESSXFeScreen::EventSel);
	F.FE.Key(0, ESSXFeKey::Square);
	TestFalse(TEXT("EventSel Square: no ui sound"), F.FE.SoundHooks.ContainsByPredicate([](const FString& S) { return S.StartsWith(TEXT("ui")); }));
	TestEqual(TEXT("EventSel Square -> Options"), (int32)F.FE.Current(), (int32)ESSXFeScreen::Options);
	F.Settle();
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Circle);
	F.Press(ESSXFeKey::Square);
	TestEqual(TEXT("Options top list: Circle / Square silent"), Hooks(), H());
	F.Press(ESSXFeKey::Cross);
	F.FE.SoundHooks.Reset();
	F.Press(ESSXFeKey::Circle);
	F.Press(ESSXFeKey::Square);
	TestEqual(TEXT("Options page 0: Circle 5, Square silent"), Hooks(), H({ TEXT("ui5") }));
	return true;
}
namespace
{
	// A minimal valid card replay: RS block (venue, mode, CRC by EncodeSetup) and a one-byte stream (checksum 0xDEADBEEF).
	TArray<uint8> MakeCardReplay(int32 Venue, int32 GameMode)
	{
		TArray<uint8> F;
		F.SetNumZeroed(SSXReplayFile::FileBytes);
		SSXReplayFile::FSetup S;
		S.Venue = Venue;
		S.GameMode = GameMode;
		SSXReplayFile::EncodeSetup(S, F.GetData());
		auto Put = [&F](int32 At, uint32 V) { for (int32 I = 0; I < 4; ++I) F[At + I] = uint8(V >> (8 * I)); };
		Put(SSXReplayFile::LengthAt, 1);
		Put(SSXReplayFile::ChecksumAt, 0xDEADBEEFu);
		return F;
	}

	bool WriteCardReplay(int32 Slot, const TArray<uint8>& F)
	{
		const FString Path = FSSXMemCard::FilePath(FSSXMemCard::ReplayName(Slot));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveArrayToFile(F, *Path);
	}

	void OpenReplayLoad(FSSXMemCard& M)
	{
		M.Begin(FSSXMemCard::EMode::Replay, false, nullptr);
		for (int32 N = 0; N < 600 && M.State != FSSXMemCard::LoadList; ++N) { M.Tick(); }
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIReplayLoadListTest, "SSX.UI.ReplayLoad.List", ScrTestFlags)
bool FSSXUIReplayLoadListTest::RunTest(const FString&)
{
	if (!ScrLoc().IsLoaded()) { SSXTest::Skip(*this, TEXT("LOC not converted; replay load checks skipped")); return true; }
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ReplayLoadList"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	TestTrue(TEXT("slot 1 written (Snowdream)"), WriteCardReplay(0, MakeCardReplay(1, 2)));
	TestTrue(TEXT("slot 3 written (Alaska)"), WriteCardReplay(2, MakeCardReplay(11, 2)));
	FSSXMemCard M;
	M.Begin(FSSXMemCard::EMode::Replay, false, nullptr);
	TestEqual(TEXT("tab LOC 3543 LOAD REPLAY"), M.FeTitleLoc(), 3543);
	TestEqual(TEXT("checking LOC 64"), M.MessageLoc(), 64);
	TestEqual(TEXT("4 rows (+0x2C)"), M.MaxFiles(), 4);
	int32 Frames = 0;
	while (M.State != FSSXMemCard::LoadList && Frames < 600) { M.Tick(); ++Frames; }
	TestEqual(TEXT("load list 0x1C after 180 + 1 + 90 updates"), Frames, 271);
	TestFalse(TEXT("port menu hidden (vfunc_50)"), M.bPortMenu);
	TestEqual(TEXT("legend 0x500 (load, cancel)"), M.HelpMask(), 0x500u);
	TestEqual(TEXT("row 1: LOC 460 + slot + track code (0x1CE8F8)"), M.FileTitle(ScrLoc(), 0), FString(TEXT("SSX Tricky Replay1 snow")));
	TestEqual(TEXT("row 2: LOC 351"), M.FileTitle(ScrLoc(), 1), ScrLoc().ByIndex(351));
	TestEqual(TEXT("row 3: Alaska"), M.FileTitle(ScrLoc(), 2), FString(TEXT("SSX Tricky Replay3 alas")));
	TestEqual(TEXT("track codes (first 4 chars of 0x332F28 +0x10)"), FSSXMemCard::TrackCode(2) + FSSXMemCard::TrackCode(4)
		+ FSSXMemCard::TrackCode(8) + FSSXMemCard::TrackCode(9), FString(TEXT("elysmerqmegabiga")));
	TestEqual(TEXT("load enabled on a file"), M.LegendDisabledMask(), 0u);
	M.Input(FSSXMemCard::InUp);
	TestEqual(TEXT("Up at row 1: stays"), M.Selected, 0);
	M.Input(FSSXMemCard::InDown);
	TestEqual(TEXT("Down -> row 2"), M.Selected, 1);
	TestEqual(TEXT("load greyed on an empty row (FUN_00292968)"), M.LegendDisabledMask(), 0x400u);
	M.Input(FSSXMemCard::InDown);
	M.Input(FSSXMemCard::InDown);
	M.Input(FSSXMemCard::InDown);
	TestEqual(TEXT("Down at row 4: stays"), M.Selected, 3);
	M.Input(FSSXMemCard::InAccept);
	M.Tick();
	M.Input(FSSXMemCard::InUp);
	M.Tick();
	TestTrue(TEXT("X on an empty row: still the list, Up during the flag update ignored"), M.State == FSSXMemCard::LoadList && M.Selected == 3);
	M.Input(FSSXMemCard::InUp);
	M.Input(FSSXMemCard::InUp);
	M.Input(FSSXMemCard::InUp);
	TestEqual(TEXT("back to row 1"), M.Selected, 0);
	M.Input(FSSXMemCard::InCircle);
	M.Tick();
	M.Tick();
	TestTrue(TEXT("Circle ignored, file kept"), M.State == FSSXMemCard::LoadList && M.FileExists(0));
	M.Input(FSSXMemCard::InBack);
	M.Tick();
	TestEqual(TEXT("Triangle: the flag waits one update"), int32(M.State), int32(FSSXMemCard::LoadList));
	M.Tick();
	TestTrue(TEXT("Triangle -> idle, nothing loaded"), M.State == FSSXMemCard::Idle && !M.bLoadedReplay);
	FSSXMemCard::RootOverride().Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIReplayLoadFlowTest, "SSX.UI.ReplayLoad.Flow", ScrTestFlags)
bool FSSXUIReplayLoadFlowTest::RunTest(const FString&)
{
	if (!ScrLoc().IsLoaded()) { SSXTest::Skip(*this, TEXT("LOC not converted; replay load checks skipped")); return true; }
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ReplayLoadFlow"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	const TArray<uint8> File = MakeCardReplay(1, 2);
	WriteCardReplay(0, File);
	FSSXMemCard M;
	OpenReplayLoad(M);
	M.Input(FSSXMemCard::InAccept);
	TArray<int32> States;
	for (int32 N = 0; N < 400 && M.IsActive(); ++N)
	{
		M.Tick();
		States.Add(int32(M.State));
		if (M.State == FSSXMemCard::Loading)
		{
			TestEqual(TEXT("0x1D: LOC 380 with the file's title"), M.MessageText(ScrLoc()),
				ScrLoc().ByIndex(380).Replace(TEXT("%s"), TEXT("SSX Tricky Replay1 snow")));
			TestEqual(TEXT("status: no dialog buttons"), M.DialogMask(), 0u);
			TestEqual(TEXT("status: load and cancel greyed (FUN_002924d0(0))"), M.LegendDisabledMask(), 0x500u);
		}
	}
	auto Count = [&States](int32 S) { int32 C = 0; for (int32 V : States) C += V == S; return C; };
	TestEqual(TEXT("press update keeps 0x1C"), States.Num() ? States[0] : -1, 0x1C);
	TestEqual(TEXT("0x1D one update"), Count(0x1D), 1);
	TestEqual(TEXT("0x1E one update (the folder card reads at once)"), Count(0x1E), 1);
	TestEqual(TEXT("0x1F one update"), Count(0x1F), 1);
	TestEqual(TEXT("0x20: 120 decrements + the transition"), Count(0x20), 121);
	TestEqual(TEXT("0x21 one update (mode 0: no message)"), Count(0x21), 1);
	TestEqual(TEXT("idle in the 127th update (126 frames after the press frame)"), States.Num(), 127);
	TestTrue(TEXT("loaded replay handed over (0x166870 / +0x498)"), M.bLoadedReplay && M.LoadedReplay == File);
	FSSXMemCard::RootOverride().Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIReplayLoadErrorsTest, "SSX.UI.ReplayLoad.Errors", ScrTestFlags)
bool FSSXUIReplayLoadErrorsTest::RunTest(const FString&)
{
	if (!ScrLoc().IsLoaded()) { SSXTest::Skip(*this, TEXT("LOC not converted; replay load checks skipped")); return true; }
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ReplayLoadErrors"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	struct FCase { const TCHAR* Name; int32 Byte; };
	const FCase Cases[] = { { TEXT("setup block CRC (vfunc_10)"), 0x31C }, { TEXT("stream checksum (vfunc_11)"), 0x920 } };
	for (const FCase& C : Cases)
	{
		TArray<uint8> F = MakeCardReplay(1, 2);
		F[C.Byte] ^= 1;
		WriteCardReplay(0, F);
		FSSXMemCard M;
		OpenReplayLoad(M);
		M.Input(FSSXMemCard::InAccept);
		int32 N = 0;
		while (M.State != FSSXMemCard::LoadFailed && N < 50) { M.Tick(); ++N; }
		TestEqual(FString::Printf(TEXT("%s: 0x22 from 0x1E (4 updates after the press)"), C.Name), N, 4);
		TestTrue(FString::Printf(TEXT("%s: LOC 372, continue, nothing loaded"), C.Name), M.MessageLoc() == 372 && M.DialogMask() == 0x10u
			&& M.MessageText(ScrLoc()) == ScrLoc().ByIndex(372) && !M.bLoadedReplay);
		TestEqual(TEXT("dialog: the legend enabled again (FUN_002923b8)"), M.LegendDisabledMask(), 0u);
		M.Input(FSSXMemCard::InAccept);
		M.Tick();
		TestEqual(TEXT("0x22 X: the flag waits one update"), int32(M.State), int32(FSSXMemCard::LoadFailed));
		M.Tick();
		TestEqual(TEXT("0x22 X -> 6 (re-read)"), int32(M.State), int32(FSSXMemCard::Reading));
		for (int32 K = 0; K < 200 && M.State != FSSXMemCard::LoadList; ++K) { M.Tick(); }
		TestEqual(TEXT("-> the list again"), int32(M.State), int32(FSSXMemCard::LoadList));
	}
	FSSXMemCard::RootOverride().Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIReplayLoadFrontEndTest, "SSX.UI.ReplayLoad.FrontEnd", ScrTestFlags)
bool FSSXUIReplayLoadFrontEndTest::RunTest(const FString&)
{
	using namespace SSXFe;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; replay load FE checks skipped")); return true; }
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ReplayLoadFe"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	WriteCardReplay(0, MakeCardReplay(1, 2));
	FScrFe F;
	F.FE.GoTo(ESSXFeScreen::Options);
	F.Settle();
	for (int32 I = 0; I < 3; ++I) { F.Press(ESSXFeKey::Down); }
	F.Press(ESSXFeKey::Cross);
	for (int32 I = 0; I < 2; ++I) { F.Press(ESSXFeKey::Down); }
	F.FE.Key(0, ESSXFeKey::Cross);
	TestEqual(TEXT("Options Save/Load item 2 -> the memory-card overlay"), int32(F.FE.Current()), int32(ESSXFeScreen::MemCard));
	TestTrue(TEXT("mode 0 (replay), load"), F.FE.MemCard.Mode == FSSXMemCard::EMode::Replay && !F.FE.MemCard.bSave);
	for (int32 N = 0; N < 600 && F.FE.MemCard.State != FSSXMemCard::LoadList; ++N) { F.FE.Tick(); }
	TestEqual(TEXT("the load list"), int32(F.FE.MemCard.State), int32(FSSXMemCard::LoadList));
	F.Redraw();
	const FString Row1 = TEXT("SSX Tricky Replay1 snow");
	if (const FSSXFeTextRec* R = F.Txt(Row1); TestNotNull(TEXT("row 1 name"), R))
	{
		ScrNear(*this, TEXT("name x 185 + 30"), R->Pos.X, 215.f);
		ScrNear(*this, TEXT("name y 190"), R->Pos.Y, 190.f);
		TestTrue(TEXT("highlighted row gold (focused list)"), ScrSameColor(R->Color, Gold));
	}
	if (const FSSXFeTextRec* R = F.Txt(F.L(351)); TestNotNull(TEXT("row 2 LOC 351"), R))
	{
		TestTrue(TEXT("an empty row is white (entries are never disabled)"), ScrSameColor(R->Color, White));
	}
	if (const FSSXFeTextRec* R = F.Txt(F.L(3761)); TestNotNull(TEXT("detail 0x15 LOC 3761"), R))
	{
		ScrNear(*this, TEXT("detail 0x15 y 60"), R->Pos.Y, 60.f);
	}
	TestNotNull(TEXT("legend: load (LOC 534)"), F.Txt(F.L(534)));
	TestNotNull(TEXT("legend: cancel (LOC 530)"), F.Txt(F.L(530)));
	// the focused list's menu sounds (default bytes): move 4, end bump 7, X 5, Circle 5, Triangle 6
	auto Key = [&F](ESSXFeKey K) { F.FE.SoundHooks.Reset(); F.FE.Key(0, K); F.FE.Tick(); TArray<FString> Hk = F.FE.SoundHooks; return Hk; };
	using H = TArray<FString>;
	TestEqual(TEXT("Up at row 1: bump 7"), Key(ESSXFeKey::Up), H({ TEXT("ui7") }));
	TestEqual(TEXT("Down: move 4"), Key(ESSXFeKey::Down), H({ TEXT("ui4") }));
	TestEqual(TEXT("X on the empty row: accept 5, nothing loads"), Key(ESSXFeKey::Cross), H({ TEXT("ui5") }));
	F.FE.Tick();
	TestEqual(TEXT("still the list"), int32(F.FE.MemCard.State), int32(FSSXMemCard::LoadList));
	TestEqual(TEXT("Circle: 5"), Key(ESSXFeKey::Circle), H({ TEXT("ui5") }));
	F.FE.Tick();
	TestEqual(TEXT("Up: move 4"), Key(ESSXFeKey::Up), H({ TEXT("ui4") }));
	TestEqual(TEXT("X on row 1: accept 5"), Key(ESSXFeKey::Cross), H({ TEXT("ui5") }));
	F.FE.Tick();
	TestEqual(TEXT("0x1D"), int32(F.FE.MemCard.State), int32(FSSXMemCard::Loading));
	for (int32 N = 0; N < 10 && F.FE.MemCard.State != FSSXMemCard::Loading4; ++N) { F.FE.Tick(); }
	F.Redraw();
	TestNotNull(TEXT("loading: LOC 380 status"), F.Txt(ScrLoc().ByIndex(380).Replace(TEXT("%s"), *Row1)));
	TestTrue(TEXT("loading: warning LOC 350"), F.D.TextRecs.ContainsByPredicate([&F](const FSSXFeTextRec& R)
		{ return !R.Str.IsEmpty() && F.L(350).StartsWith(R.Str.Left(12)); }));
	if (const FSSXFeTextRec* R = F.Txt(Row1); TestNotNull(TEXT("the list stays under the status"), R))
	{
		TestTrue(TEXT("unfocused: white"), ScrSameColor(R->Color, White));
	}
	TestEqual(TEXT("loading: Triangle has no menu"), Key(ESSXFeKey::Triangle), H());
	for (int32 N = 0; N < 200 && F.FE.Current() == ESSXFeScreen::MemCard; ++N) { F.FE.Tick(); }
	TestTrue(TEXT("the overlay popped and the recorded race launches (0x167060, cFE3D+6 = 1)"), F.FE.bReplayLaunch
		&& F.FE.ReplayLaunchSetup.Venue == 1 && F.FE.PendingReplay.Num() == SSXReplayFile::FileBytes);
	FSSXMemCard::RootOverride().Reset();
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIMemCardDialogButtonsTest, "SSX.UI.MemCardDialogButtons", ScrTestFlags)
bool FSSXUIMemCardDialogButtonsTest::RunTest(const FString& Parameters)
{
	// 0x291E00 / 0x2AE098
	using namespace SSXFe;
	if (!FScrAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("FE fonts / sprites / LOC not converted; dialog checks skipped")); return true; }
	const FSSXLocStrings& L = ScrLoc();
	FSSXProfile P = FSSXProfile::MakeDefault();
	struct FCase { FSSXMemCard::EState State; int32 Loc1; int32 Icon1; int32 Loc2; int32 Icon2; };
	const FCase Cases[] = {
		{ FSSXMemCard::Unformatted, 354, IconTriangle, 340, IconCross },
		{ FSSXMemCard::NoCard, 442, IconCross, 340, IconTriangle },
		{ FSSXMemCard::Overwrite, 470, IconCross, 392, IconTriangle },
		{ FSSXMemCard::SaveDone, 341, IconCross, -1, -1 },
	};
	for (const FCase& Cs : Cases)
	{
		FSSXMemCard M;
		M.Begin(FSSXMemCard::EMode::Game, true, &P);
		M.State = Cs.State;
		FSSXFeDrawList D(&FScrAssets::Get().A);
		M.DrawFe(D, L, 0);
		auto FindTxt = [&D](const FString& S) -> const FSSXFeTextRec* { for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == S) return &R; } return nullptr; };
		auto FindSpr = [&D](int32 Id, int32 Nth) -> const FSSXFeDrawCmd* { for (const FSSXFeDrawCmd& C : D.Cmds) { if (C.SpriteId == Id && Nth-- == 0) return &C; } return nullptr; };
		const FString Name = FString::Printf(TEXT("state 0x%X"), (int32)Cs.State);
		const FSSXFeTextRec* T1 = FindTxt(L.ByIndex(Cs.Loc1));
		const FSSXFeDrawCmd* S1 = FindSpr(Cs.Icon1, 0);
		if (!TestTrue(Name + TEXT(": button 1 drawn"), T1 && S1)) continue;
		const float W1 = 4.f + D.TextWidth(EFont::Menu, L.ByIndex(Cs.Loc1), 0.7f) + ShadowOffset;
		const float Y = (ScreenH - 225.f) * 0.5f + 165.f;
		TestTrue(Name + TEXT(": icon 17 x 17 at y + 165"), S1->Size == FVector2f(17.f, 17.f) && FMath::IsNearlyEqual(S1->Pos.Y, Y));
		TestTrue(Name + TEXT(": label 4 px right of the icon"), FMath::IsNearlyEqual(T1->Pos.X, S1->Pos.X + 17.f + 4.f, 0.01f));
		ScrNear(*this, Name + TEXT(": label (line height + shadow) centred in the 17 px cell"), T1->Pos.Y, Y + (17.f - (D.FontHeight(EFont::Menu, 0.7f) + ShadowOffset)) * 0.5f, 0.01f);
		if (Cs.Loc2 < 0)
		{
			ScrNear(*this, Name + TEXT(": single button x = 320 - w / 2"), S1->Pos.X + 17.f, 320.f - W1 * 0.5f, 0.01f);
			continue;
		}
		const FSSXFeTextRec* T2 = FindTxt(L.ByIndex(Cs.Loc2));
		const FSSXFeDrawCmd* S2 = FindSpr(Cs.Icon2, Cs.Icon2 == Cs.Icon1 ? 1 : 0);
		if (!TestTrue(Name + TEXT(": button 2 drawn"), T2 && S2)) continue;
		ScrNear(*this, Name + TEXT(": button 1 x = 320 - (w1 + 50)"), S1->Pos.X + 17.f, 320.f - (W1 + 50.f), 0.01f);
		ScrNear(*this, Name + TEXT(": button 2 icon right edge at 370"), S2->Pos.X + 17.f, 370.f, 0.01f);
		ScrNear(*this, Name + TEXT(": button 2 label at 374"), T2->Pos.X, 374.f, 0.01f);
	}
	return true;
}
