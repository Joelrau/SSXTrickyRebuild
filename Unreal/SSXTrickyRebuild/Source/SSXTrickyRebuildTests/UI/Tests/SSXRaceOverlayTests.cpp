// 0x1A9BB8 / 0x1AA630 / 0x1A9928 / 0x1AA268
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "Race/SSXReplay.h"
#include "Race/SSXReplayFile.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXHudData.h"
#include "UI/SSXHudOverlay.h"
#include "UI/SSXMemCard.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXFe;
	constexpr EAutomationTestFlags OvTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	const FSSXLocStrings& OvLoc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	struct FOvAssets
	{
		FSSXFeAssets A;
		FSSXHudSheet Sheet;
		bool bFonts = false, bHud = false;
		FOvAssets()
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
			A.Title = Font(TEXT("TITLE"), TitleScaleX, TitleScaleY);
			A.Menu = Font(TEXT("MENU"), MenuScaleX, MenuScaleY);
			bFonts = A.Title.IsValid() && A.Menu.IsValid();
			bHud = Sheet.Load(FPaths::Combine(FSSXHudData::DefaultDir(), TEXT("hud_sprites.json")), false);
			if (bHud)
			{
				A.HudSprites = &Sheet.Sprites;
				for (const TPair<FName, FString>& P : Sheet.PagePaths) A.Pages.Add(P.Key, Dummy);
			}
		}
		static FOvAssets& Get() { static FOvAssets S; return S; }
		bool Ready() const { return bFonts && bHud && OvLoc().IsLoaded(); }
	};

	struct FOv
	{
		FSSXProfile P = FSSXProfile::MakeDefault();
		FSSXCheats C;
		FSSXRaceUIHostStub H;
		FSSXOverlayManager O;
		FSSXFeDrawList D;
		FOv() : D(&FOvAssets::Get().A)
		{
			SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
			P.Slot[0].Char = 0;
			P.Slot[0].UserName = TEXT("JOEL");
			P.Slot[1].Char = 1;
			P.Slot[1].UserName = TEXT("ANNA");
			O.Init(&P, &C, &OvLoc());
			O.SetHost(&H);
		}
		void Redraw() { D.Reset(); O.Draw(D); }
		void Ticks(int32 N) { for (int32 I = 0; I < N; ++I) O.Tick(); }
		FString L(int32 Id) const { return OvLoc().ByIndex(Id); }
		float Ink(EFont F, const FString& S, float Scale) const { return FMath::TruncToFloat(D.TextInkWidth(F, S, Scale)); }
		const FSSXFeTextRec* Txt(const FString& S, int32 Nth = 0) const
		{
			for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == S && Nth-- == 0) return &R; }
			return nullptr;
		}
		const FSSXFeDrawCmd* Spr(int32 Id, int32 Nth = 0) const
		{
			for (const FSSXFeDrawCmd& Cm : D.Cmds) { if (Cm.SpriteId == Id && Nth-- == 0) return &Cm; }
			return nullptr;
		}
		TArray<const FSSXFeDrawCmd*> Rects() const
		{
			TArray<const FSSXFeDrawCmd*> R;
			for (const FSSXFeDrawCmd& Cm : D.Cmds) { if (Cm.bGsUntextured && !Cm.Texture && Cm.SpriteId < 0) R.Add(&Cm); }
			return R;
		}
		static FSSXRacerResult Racer(const TCHAR* Name, int32 Player, int32 Place, double Sec, int32 Aggr = 0, int32 AggrTo = 0)
		{
			FSSXRacerResult R; R.Name = Name; R.Player = Player; R.Place = Place; R.TimeSec = Sec; R.Aggression = Aggr; R.AggressionTo = AggrTo;
			return R;
		}
		void SixRacers()
		{
			H.Results = { Racer(TEXT("JOEL"), 0, 1, 83.45), Racer(TEXT("Kaori"), -1, 2, 84.0, 40, 70), Racer(TEXT("Mac"), -1, 3, 85.5, 90, 50),
				Racer(TEXT("Zoe"), -1, 4, 86.0, 10, 10), Racer(TEXT("Moby"), -1, 5, 87.0, 55, 60), Racer(TEXT("Elise"), -1, 6, 88.0, 70, 75) };
		}
	};

	void Near(FAutomationTestBase& T, const FString& What, float Got, float Want, float Tol = 1.f)
	{
		T.AddInfo(FString::Printf(TEXT("%s: port %.2f model %.2f"), *What, Got, Want));
		T.TestTrue(What, FMath::Abs(Got - Want) <= Tol);
	}
	void NearTxt(FAutomationTestBase& T, const FString& What, const FSSXFeTextRec* R, float X, float Y, float Tol = 1.f)
	{
		if (!T.TestNotNull(What + TEXT(" drawn"), R)) return;
		Near(T, What + TEXT(" x"), R->Pos.X, X, Tol);
		Near(T, What + TEXT(" y"), R->Pos.Y, Y, Tol);
	}
	void NearSpr(FAutomationTestBase& T, const FString& What, const FSSXFeDrawCmd* C, float X, float Y, float W = -1.f, float H = -1.f)
	{
		if (!T.TestNotNull(What + TEXT(" drawn"), C)) return;
		Near(T, What + TEXT(" x"), C->Pos.X, X);
		Near(T, What + TEXT(" y"), C->Pos.Y, Y);
		if (W >= 0.f) Near(T, What + TEXT(" w"), C->Size.X, W);
		if (H >= 0.f) Near(T, What + TEXT(" h"), C->Size.Y, H);
	}
	bool NearColor(const FLinearColor& A, const FLinearColor& B, float Tol = 0.01f)
	{
		return FMath::Abs(A.R - B.R) <= Tol && FMath::Abs(A.G - B.G) <= Tol && FMath::Abs(A.B - B.B) <= Tol && FMath::Abs(A.A - B.A) <= Tol;
	}
	constexpr float H37 = 33.f;
	constexpr float MenuLh = 15.f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayPrimitivesTest, "SSX.UI.Overlays.Primitives", OvTestFlags)
bool FSSXOverlayPrimitivesTest::RunTest(const FString& Parameters)
{
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	// 0x1A9BB8
	FOv F;
	F.O.Activate(OvViewHiScores);
	F.Redraw();
	TArray<const FSSXFeDrawCmd*> R = F.Rects();
	if (TestTrue(TEXT("three box rects"), R.Num() >= 3))
	{
		const float Top = 240.f - 230.f;
		NearSpr(*this, TEXT("box band"), R[0], -20.f, Top + H37 - 0.21f * H37, 680.f, 460.f - 2.f * H37 + 0.42f * H37);
		NearSpr(*this, TEXT("box top inset"), R[1], -20.f + 0.455f * 340.f, Top + 0.26f * H37, 2.f * (340.f - 0.455f * 340.f), 0.53f * H37);
		NearSpr(*this, TEXT("box bottom inset"), R[2], -20.f + 0.455f * 340.f, 240.f + 230.f - H37 + 0.21f * H37, 2.f * (340.f - 0.455f * 340.f), 0.53f * H37);
		TestTrue(TEXT("box fill (0.65, .1, .1, .1): no open animation outside the pause"), NearColor(R[0]->Color, FeColor(0.65f, 0.1f, 0.1f, 0.1f)));
	}
	const FSSXFeDrawCmd* TopL = F.Spr(HudBoxCorner, 0);
	const FSSXFeDrawCmd* BotL = F.Spr(HudBoxCorner, 2);
	NearSpr(*this, TEXT("top-left corner"), TopL, -20.f, 10.f, 340.f, H37);
	NearSpr(*this, TEXT("bottom-left corner"), BotL, -20.f, 470.f - H37, 340.f, H37);
	if (TopL && BotL)
	{
		TestTrue(TEXT("top corners are the v-flipped art (flags 2)"), TopL->UV0.Y > TopL->UV1.Y);
		TestTrue(TEXT("bottom corners unflipped (flags 0)"), BotL->UV0.Y < BotL->UV1.Y);
		TestTrue(TEXT("corner pieces white, alpha 1"), NearColor(TopL->Color, White));
	}
	const FSSXFeDrawCmd* TopR = F.Spr(HudBoxCorner, 1);
	if (TestNotNull(TEXT("top-right corner"), TopR)) { TestTrue(TEXT("right pieces mirrored in u (flags 3)"), TopR->UV0.X > TopR->UV1.X && TopR->UV0.Y > TopR->UV1.Y); Near(*this, TEXT("top-right x"), TopR->Pos.X, 320.f); }
	NearTxt(*this, TEXT("venue name"), F.Txt(F.L(SSXFeTables::TrackNameLoc(F.H.Track))), 116.f, 10.f + H37 + 7.f);
	const FSSXFeTextRec* Back = F.Txt(F.L(471));
	const float Bw = F.Ink(EFont::Menu, F.L(471), 0.73f);
	const float Pitch = MenuLh * 0.73f + 4.f;
	NearTxt(*this, TEXT("back legend"), Back, 600.f - Bw, 430.f - Pitch);
	NearSpr(*this, TEXT("back icon (centred, scale 0.9)"), F.Spr(HudTriangle), 600.f - Bw - 16.f - 9.f, 430.f - Pitch + MenuLh * 0.73f * 0.5f - 9.f, 18.f, 18.f);
	FOv P;
	TArray<float> Seen;
	for (int32 I = 0; I < 120; ++I) { P.D.Reset(); P.O.Activate(OvViewHiScores); P.O.Draw(P.D); Seen.Add(P.O.GetPulse()); }
	float Hi = 0.f, Lo = 2.f;
	for (float V : Seen) { Hi = FMath::Max(Hi, V); Lo = FMath::Min(Lo, V); }
	TestTrue(TEXT("pulse peaks just above 1.1"), Hi > 1.1f && Hi < 1.106f);
	TestTrue(TEXT("pulse dips just below 0.99"), Lo < 0.99f && Lo > 0.984f);
	TestTrue(TEXT("pulse step 0.004"), FMath::IsNearlyEqual(Seen[1] - Seen[0], 0.004f, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayPauseTest, "SSX.UI.Overlays.Pause", OvTestFlags)
bool FSSXOverlayPauseTest::RunTest(const FString& Parameters)
{
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	FOv F;
	F.O.Pad(0, PadStart);
	F.Redraw();
	TArray<const FSSXFeDrawCmd*> R0 = F.Rects();
	if (TestTrue(TEXT("pause box drawn on the first frame"), R0.Num() >= 3))
	{
		TestTrue(TEXT("alpha starts at 0 (flag 2)"), R0[0]->Color.A < 0.001f);
		Near(*this, TEXT("box grows from 60"), R0[0]->Size.Y, 60.f - 2.f * H37 + 0.42f * H37);
	}
	TestNull(TEXT("no back legend while growing"), F.Txt(F.L(471)));
	F.Ticks(60);
	F.Redraw();
	const float Pitch = MenuLh * 0.92f + 4.f;
	const float Hp = 95.f + 5.f * Pitch;
	TArray<const FSSXFeDrawCmd*> R = F.Rects();
	if (TestTrue(TEXT("pause box"), R.Num() >= 3))
	{
		Near(*this, TEXT("Hp = 95 + 5 (15 x 0.92 + 4)"), R[0]->Size.Y, Hp - 2.f * H37 + 0.42f * H37);
		TestTrue(TEXT("alpha cap 0.88"), FMath::IsNearlyEqual(R[0]->Color.A, 0.88f, 1e-3f));
	}
	// 0x1A9928
	const float TabY = (480.f - Hp) * 0.5f - 8.f;
	const float Tw = F.Ink(EFont::Title, F.L(3683), 0.79f);
	NearSpr(*this, TEXT("tab left 0x3E"), F.Spr(HudTabL), 0.f, TabY, 82.f * 0.9f, 54.f * 0.867f);
	NearSpr(*this, TEXT("tab middle 0x3F"), F.Spr(HudTabM), 73.8f, TabY, Tw + 8.f, 54.f * 0.867f);
	NearSpr(*this, TEXT("tab right 0x40"), F.Spr(HudTabR), 73.8f + Tw + 8.f, TabY, 75.f * 0.9f, 54.f * 0.867f);
	NearTxt(*this, TEXT("tab title TITLE 0.79"), F.Txt(F.L(3683)), 71.8f, TabY + 7.f);
	NearTxt(*this, TEXT("item 2 (options)"), F.Txt(F.L(477)), 233.f, 240.f - 2.5f * Pitch + 2.f * Pitch);
	NearSpr(*this, TEXT("X icon centred on row 0"), F.Spr(HudCross), 233.f - 16.f - 9.f, 240.f - 2.5f * Pitch + MenuLh * 0.92f * 0.5f - 9.f, 18.f, 18.f);
	const FSSXFeTextRec* Sel = F.Txt(F.L(475));
	if (TestNotNull(TEXT("selected row"), Sel)) { TestTrue(TEXT("selected gold"), NearColor(Sel->Color, Gold)); }
	const float Bw = F.Ink(EFont::Menu, F.L(471), 0.73f);
	NearTxt(*this, TEXT("back legend"), F.Txt(F.L(471)), 600.f - Bw, (Hp + 480.f) * 0.5f - 40.f - (MenuLh * 0.73f + 4.f));
	F.O.Pad(0, PadDown); F.O.Pad(0, PadCross);
	F.Redraw();
	TestNull(TEXT("message box hides the menu"), F.Txt(F.L(477)));
	TestNull(TEXT("message box hides the tab"), F.Spr(HudTabL));
	const FSSXFeTextRec* Q = F.Txt(F.L(326));
	const float Lw = F.Ink(EFont::Menu, F.L(326), 1.f);
	NearTxt(*this, TEXT("message line centred"), Q, 320.f - Lw * 0.5f, 240.f - 50.f + 20.f);
	const float Wx = F.Ink(EFont::Menu, F.L(470), 1.f), Wt = F.Ink(EFont::Menu, F.L(392), 1.f);
	const float Buttons = Wx + 40.f + 40.f + Wt;
	const float W = FMath::Max(Lw, Buttons);
	const float X0 = 320.f - W * 0.5f + (W - Buttons) * 0.5f;
	NearSpr(*this, TEXT("X button icon"), F.Spr(HudCross), X0, 240.f);
	NearTxt(*this, TEXT("X label (+23, +2)"), F.Txt(F.L(470)), X0 + 23.f, 242.f);
	NearSpr(*this, TEXT("Triangle button after label + 40"), F.Spr(HudTriangle), X0 + Wx + 40.f, 240.f);
	F.O.Pad(0, PadTriangle);
	// 0x1AC7E0
	F.O.Pad(0, PadUp); F.O.Pad(0, PadUp); F.O.Pad(0, PadCross);
	TestTrue(TEXT("trick book open"), F.O.bTrickBook);
	F.Ticks(40);
	F.Redraw();
	const FSSXFeTextRec* Name = F.Txt(SSXFeTables::RiderName(0));
	NearTxt(*this, TEXT("rider name (40, 242)"), Name, 40.f, 242.f);
	if (Name) TestTrue(TEXT("rider name gold"), NearColor(Name->Color, Gold));
	const FString Chap = F.L(3684).Replace(TEXT("%d"), TEXT("1"));
	NearTxt(*this, TEXT("Chapter centred (320, 242) TITLE 0.9"), F.Txt(Chap), 320.f - F.Ink(EFont::Title, Chap, 0.9f) * 0.5f, 242.f);
	const FString Trick = F.L(3751).Replace(TEXT("%d"), *FString::FromInt(F.O.TrickPage + 1));
	NearTxt(*this, TEXT("trick %d centred (550, 245) TITLE 0.76"), F.Txt(Trick), 550.f - F.Ink(EFont::Title, Trick, 0.76f) * 0.5f, 245.f);
	TestNull(TEXT("trick book list has no X icon"), F.Spr(HudCross));
	TestNotNull(TEXT("trick book keeps the list"), F.Txt(F.L(477)));
	NearSpr(*this, TEXT("right arrow (598, 282)"), F.Spr(HudArrowR), 598.f, 282.f);
	F.P.Career[0].BookDone[0] = 1;
	F.O.TrickPage = 0;
	F.Redraw();
	NearTxt(*this, TEXT("completed (320, 330) MENU 0.8"), F.Txt(F.L(3219)), 320.f - F.Ink(EFont::Menu, F.L(3219), 0.8f) * 0.5f, 330.f);
	const FString TName = SSXFeTrickBook::FTable::Get().TrickName(0, 0);
	if (!TName.IsEmpty())
	{
		const bool bLong = TName.Len() >= 50;
		const FSSXFeTextRec* T = F.Txt(TName);
		NearTxt(*this, TEXT("trick name centred (320, 273)"), T, 320.f - F.Ink(EFont::Menu, TName, bLong ? 0.696f : 0.8f) * 0.5f, bLong ? 275.f : 273.f);
		if (T) TestTrue(TEXT("trick name white"), NearColor(T->Color, White));
	}
	return true;
}

// 0x1AC410 / 0x1AC580 / 0x225618 / 0x225660
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayPauseAudioTest, "SSX.UI.Overlays.PauseAudio", OvTestFlags)
bool FSSXOverlayPauseAudioTest::RunTest(const FString& Parameters)
{
	{
		FOv F;
		F.O.Pad(0, PadStart);
		TestEqual(TEXT("first push pauses the game audio (0x2153F8)"), F.H.MenuAudioPauses, 1);
		TestTrue(TEXT("first push plays zBxsfx #126 (0x215458)"), F.O.Sounds.Contains(SSXUISound::ERace::PauseOpen));
		TestEqual(TEXT("#126"), SSXUISound::RaceSound(SSXUISound::ERace::PauseOpen).Sound, 126);
		F.O.Pad(0, PadDown); F.O.Pad(0, PadDown); F.O.Pad(0, PadCross);
		TestEqual(TEXT("options open"), F.O.PauseDepth, 1);
		const TArray<int32> Rows = F.O.PauseOptionRows();
		const int32 Song = Rows.IndexOfByKey(489);
		if (TestTrue(TEXT("song row"), Song >= 0))
		{
			F.O.OptionsSel = Song;
			F.O.Pad(0, PadRight);
			F.O.Pad(0, PadLeft);
			TestTrue(TEXT("Right = next, Left = previous allowed song"), F.H.SongSteps == TArray<int32>({ 1, -1 }));
		}
		const int32 Master = Rows.IndexOfByKey(486);
		if (TestTrue(TEXT("master row"), Master >= 0))
		{
			F.O.OptionsSel = Master;
			F.P.Settings.Master = 0.f;
			F.O.SetPadHeld(0, PadRight);
			F.O.Pad(0, PadRight);
			F.Ticks(1);
			TestTrue(TEXT("press steps once"), FMath::IsNearlyEqual(F.P.Settings.Master, 1.f / 11.f, 1e-4f));
			F.Ticks(11);
			TestTrue(TEXT("no repeat before count 12"), FMath::IsNearlyEqual(F.P.Settings.Master, 1.f / 11.f, 1e-4f));
			F.Ticks(1);
			TestTrue(TEXT("repeat at count 12"), FMath::IsNearlyEqual(F.P.Settings.Master, 2.f / 11.f, 1e-4f));
			F.Ticks(48);
			TestTrue(TEXT("repeats at 24, 36, 48, 60"), FMath::IsNearlyEqual(F.P.Settings.Master, 6.f / 11.f, 1e-4f));
			F.Ticks(4);
			TestTrue(TEXT("then every 2nd frame"), FMath::IsNearlyEqual(F.P.Settings.Master, 8.f / 11.f, 1e-4f));
			F.O.SetPadHeld(0, 0);
			F.Ticks(20);
			TestTrue(TEXT("released: no repeat"), FMath::IsNearlyEqual(F.P.Settings.Master, 8.f / 11.f, 1e-4f));
		}
		F.O.Pad(0, PadTriangle);
		TestEqual(TEXT("options popped"), F.O.PauseDepth, 0);
		TestEqual(TEXT("a lower level's pop pauses the song (0x225240)"), F.H.SongPauses, 1);
		TestEqual(TEXT("not resumed yet"), F.H.MenuAudioResumes, 0);
		F.O.Pad(0, PadTriangle);
		TestEqual(TEXT("last pop resumes (0x215488)"), F.H.MenuAudioResumes, 1);
		TestEqual(TEXT("no song pause on the last pop"), F.H.SongPauses, 1);
	}
	{
		FOv F;
		F.O.Pad(0, PadStart);
		F.O.Pad(0, PadDown); F.O.Pad(0, PadDown); F.O.Pad(0, PadCross);
		F.O.Pad(0, PadSelect);
		TestTrue(TEXT("Select: song paused and audio resumed"), F.H.SongPauses == 1 && F.H.MenuAudioResumes == 1 && F.O.PauseDepth == -1);
	}
	{
		FOv F;
		F.H.Mode = 3;
		TestTrue(TEXT("SE Showoff"), F.O.ResultsItems() == TArray<int32>({ 0, 2, 1, 4, 3 }));
		F.H.Mode = 5;
		F.P.Session.Points[0].Left = 0;
		TestTrue(TEXT("WC Showoff, no points"), F.O.ResultsItems() == TArray<int32>({ 0, 2, 1, 4 }));
		F.P.Session.Points[0].Left = 2;
		TestTrue(TEXT("WC Showoff, points"), F.O.ResultsItems() == TArray<int32>({ 0, 1, 4 }));
		F.H.bCircuitBeaten = true;
		F.H.Mode = 3;
		TestTrue(TEXT("Showoff, circuit beaten"), F.O.ResultsItems() == TArray<int32>({ 0, 1, 4 }));
		F.H.bCircuitBeaten = false;
		F.H.Mode = 4;
		F.P.Session.Heat = 0;
		TestTrue(TEXT("WC Race heat"), F.O.ResultsItems() == TArray<int32>({ 0, 2, 1, 4, 3 }));
		F.H.bEliminated = true;
		TestTrue(TEXT("WC Race heat, eliminated"), F.O.ResultsItems() == TArray<int32>({ 2, 1, 4, 3 }));
		F.H.bEliminated = false;
		F.P.Session.Heat = 2;
		TestTrue(TEXT("WC Race final, points"), F.O.ResultsItems() == TArray<int32>({ 0, 1, 4 }));
		F.P.Session.Points[0].Left = 0;
		TestTrue(TEXT("WC Race final, no points"), F.O.ResultsItems() == TArray<int32>({ 0, 2, 1, 4, 3 }));
	}
	return true;
}

// 0x1B8BB8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlaySaveWarningTest, "SSX.UI.Overlays.SaveWarning", OvTestFlags)
bool FSSXOverlaySaveWarningTest::RunTest(const FString& Parameters)
{
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardSaveWarning"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
		FSSXMemCard::RootOverride().Reset();
	};
	FOv F;
	F.P.bSettingsDirty = true;
	F.O.Pad(0, PadStart); F.O.Pad(0, PadUp); F.O.Pad(0, PadUp); F.O.Pad(0, PadCross);
	F.O.Pad(0, PadCross);
	F.Redraw();
	auto Warn = [&F]() -> const FSSXFeTextRec*
	{
		const FString Full = F.L(350);
		for (const FSSXFeTextRec& R : F.D.TextRecs)
		{
			const FString S = R.Str.TrimStartAndEnd();
			if (!S.IsEmpty() && R.Pos.Y >= 311.f && Full.Contains(S)) return &R;
		}
		return nullptr;
	};
	const FSSXFeTextRec* W = Warn();
	if (!W)
	{
		for (const FSSXFeTextRec& R : F.D.TextRecs) AddInfo(FString::Printf(TEXT("text '%s' at (%.1f, %.1f)"), *R.Str, R.Pos.X, R.Pos.Y));
		AddInfo(FString::Printf(TEXT("LOC 350 '%s'"), *F.L(350)));
	}
	if (TestNotNull(TEXT("warning LOC 350 drawn with the saving box"), W))
	{
		TestTrue(TEXT("alpha 1.0 at the start"), FMath::IsNearlyEqual(W->Color.A, 1.f, 1e-3f));
		TestTrue(TEXT("white"), FMath::IsNearlyEqual(W->Color.R, 1.f, 1e-3f) && FMath::IsNearlyEqual(W->Color.G, 1.f, 1e-3f));
	}
	F.Ticks(30);
	F.Redraw();
	W = Warn();
	if (W) TestTrue(TEXT("alpha 1 - 30/60"), FMath::IsNearlyEqual(W->Color.A, 0.5f, 1e-3f));
	F.Ticks(60);
	F.Redraw();
	W = Warn();
	if (W) TestTrue(TEXT("bounced at 0.2 and rising (0.2 + 41..42 / 60)"), W->Color.A > 0.87f && W->Color.A < 0.91f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayResultsTest, "SSX.UI.Overlays.Results", OvTestFlags)
bool FSSXOverlayResultsTest::RunTest(const FString& Parameters)
{
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	{
		// 0x1B0188
		FOv F;
		F.H.Mode = 2;
		F.SixRacers();
		F.O.Activate(OvRaceResults);
		F.Redraw();
		NearTxt(*this, TEXT("title (116, (480 - 460) / 2 + h37 + 30)"), F.Txt(F.L(3671)), 116.f, 10.f + H37 + 30.f);
		NearTxt(*this, TEXT("header place (81, 104)"), F.Txt(F.L(3701)), 81.f, 112.f - 8.f);
		NearTxt(*this, TEXT("header time right edge 582"), F.Txt(F.L(3681)), 582.f - F.Ink(EFont::Title, F.L(3681), 0.7f), 104.f);
		const float Row0 = 112.f + MenuLh * 0.85f + 12.f, Pitch = MenuLh * 0.85f + 18.f;
		NearTxt(*this, TEXT("row 0 place"), F.Txt(TEXT("1") + OvLoc().ByIndex(3652)), 81.f, Row0);
		NearTxt(*this, TEXT("row 2 name"), F.Txt(TEXT("Mac")), 164.f, Row0 + 2.f * Pitch);
		const FString T0 = TEXT("1:23.45");
		NearTxt(*this, TEXT("row 0 time right edge 582"), F.Txt(T0), 582.f - F.Ink(EFont::Menu, T0, 0.85f), Row0);
		const FSSXFeTextRec* Human = F.Txt(TEXT("JOEL"));
		if (Human) TestTrue(TEXT("human row gold"), NearColor(Human->Color, Gold));
		TestNull(TEXT("aggression is not a number"), F.Txt(TEXT("40")));
		// FUN_001A91C8
		const FSSXFeDrawCmd* Bar = nullptr;
		for (const FSSXFeDrawCmd* Rc : F.Rects()) { if (FMath::IsNearlyEqual(Rc->Pos.X, 292.f) && FMath::IsNearlyEqual(Rc->Size.X, 70.f) && FMath::Abs(Rc->Pos.Y - (Row0 + Pitch + 3.f)) < 0.5f) Bar = Rc; }
		if (TestNotNull(TEXT("aggression bar back (292, row 1 + 3, 70 x 13)"), Bar)) { TestTrue(TEXT("black"), NearColor(Bar->Color, Black)); Near(*this, TEXT("bar h"), Bar->Size.Y, 13.f); }
		int32 Blue = 0, Red = 0, Green = 0;
		for (const FSSXFeDrawCmd* Rc : F.Rects())
		{
			Blue += NearColor(Rc->Color, FeColor(1.f, 0.f, 0.f, 0.8f)) && FMath::IsNearlyEqual(Rc->Pos.X, 292.f);
			Red += NearColor(Rc->Color, FeColor(1.f, 0.7f, 0.1f, 0.1f));
			Green += NearColor(Rc->Color, FeColor(1.f, 0.1f, 0.7f, 0.1f));
		}
		TestEqual(TEXT("five AI bars"), Blue, 5);
		TestEqual(TEXT("rises red (Kaori 40 -> 70, Moby, Elise)"), Red, 3);
		TestEqual(TEXT("falls green (Mac 90 -> 50)"), Green, 1);
		NearTxt(*this, TEXT("Kaori new value \"70%\" at x + 155"), F.Txt(TEXT("70%")), 292.f + 155.f, Row0 + Pitch + 3.f);
		const FString Enemy = F.L(3551);
		NearTxt(*this, TEXT("label centred (x + 35, y - 13)"), F.Txt(Enemy), 292.f + 35.f - F.Ink(EFont::Menu, Enemy, 0.65f) * 0.5f, Row0 + Pitch + 3.f - 13.f);
		NearSpr(*this, TEXT("rise arrow 0x2C at x + 76 + anim"), F.Spr(HudArrowR), 292.f + 76.f + 1.f / 2.5f, Row0 + Pitch + 3.f);
		{
			// FUN_001A91C8
			FOv G;
			G.H.Mode = 2;
			G.H.Results = { FOv::Racer(TEXT("JOEL"), 0, 1, 80.0), FOv::Racer(TEXT("A"), -1, 2, 81.0, 20, 20), FOv::Racer(TEXT("B"), -1, 3, 82.0, 30, 21),
				FOv::Racer(TEXT("C"), -1, 4, 83.0, 61, 60), FOv::Racer(TEXT("D"), -1, 5, 84.0, 50, 80), FOv::Racer(TEXT("E"), -1, 6, 85.0, 81, 81) };
			G.O.Activate(OvRaceResults);
			G.Redraw();
			auto LabelCount = [&G](int32 Loc, const FLinearColor& C)
			{
				int32 Nc = 0;
				for (const FSSXFeTextRec& R : G.D.TextRecs) { Nc += R.Str == G.L(Loc) && NearColor(R.Color, C); }
				return Nc;
			};
			TestEqual(TEXT("20: Friend green"), LabelCount(4, FeColor(1.f, 0.1f, 1.f, 0.1f)), 1);
			TestEqual(TEXT("21 / 60: Neutral white"), LabelCount(3551, FeColor(1.f, 1.f, 1.f, 1.f)), 2);
			TestEqual(TEXT("80: Neutral orange"), LabelCount(3551, FeColor(1.f, 1.f, 0.471f, 0.039f)), 1);
			TestEqual(TEXT("81: Enemy red"), LabelCount(8, FeColor(1.f, 0.9f, 0.1f, 0.1f)), 1);
			TestNotNull(TEXT("D rise text \"+30 =\""), G.Txt(TEXT("+30 =")));
			TestNotNull(TEXT("B fall text \"--9 =\""), G.Txt(TEXT("--9 =")));
			TestNotNull(TEXT("no change \" =\""), G.Txt(TEXT(" =")));
			TestNotNull(TEXT("new value \"81%\""), G.Txt(TEXT("81%")));
			bool bDBlue = false, bDRed = false, bBGreen = false;
			for (const FSSXFeDrawCmd* Rc : G.Rects())
			{
				bDBlue |= NearColor(Rc->Color, FeColor(1.f, 0.f, 0.f, 0.8f)) && FMath::Abs(Rc->Size.X - 35.f) < 0.01f;
				bDRed |= NearColor(Rc->Color, FeColor(1.f, 0.7f, 0.1f, 0.1f)) && FMath::Abs(Rc->Pos.X - 327.f) < 0.01f && FMath::Abs(Rc->Size.X - 21.f) < 0.01f;
				bBGreen |= NearColor(Rc->Color, FeColor(1.f, 0.1f, 0.7f, 0.1f)) && FMath::Abs(Rc->Pos.X - (292.f + 14.7f - 6.3f)) < 0.01f && FMath::Abs(Rc->Size.X - 6.3f) < 0.01f;
			}
			TestTrue(TEXT("D 50 -> 80: blue 35 wide, red from x + 35, 21 wide"), bDBlue && bDRed);
			TestTrue(TEXT("B 30 -> 21: blue to 21 (14.7), green drawn with the negative change width (x + 14.7 - 6.3, 6.3)"), bBGreen);
		}
		int32 Missing = 0;
		for (int32 I = 0; I < 60; ++I) { F.Ticks(1); F.Redraw(); Missing += F.Txt(TEXT("JOEL")) == nullptr; }
		TestEqual(TEXT("human row drawn on every frame (no blink)"), Missing, 0);
		const FString Next = F.L(3509).TrimEnd();
		TestNotNull(TEXT("menu item next event"), F.Txt(Next));
		F.O.ResultsSel = 4;
		F.O.Pad(0, PadCross);
		F.Redraw();
		TestTrue(TEXT("quit prompt"), F.O.Dialog.bActive);
		TestNull(TEXT("message box hides the results"), F.Txt(TEXT("Mac")));
	}
	{
		FOv F;
		F.H.Mode = 4;
		F.P.Session.Heat = 2;
		F.SixRacers();
		F.O.Activate(OvRaceResults);
		F.Ticks(40);
		F.Redraw();
		const float Row0 = 112.f + MenuLh * 0.85f + 12.f;
		NearTxt(*this, TEXT("WC final row 1 37 below"), F.Txt(TEXT("Kaori")), 164.f, Row0 + 37.f);
		NearTxt(*this, TEXT("WC final title 3650"), F.Txt(F.L(3650)), 116.f, 10.f + H37 + 30.f);
		const FSSXFeDrawCmd* M = F.Spr(HudMedalGold);
		NearSpr(*this, TEXT("gold medal right edge at x[0] - 5"), M, 81.f - 5.f - 74.f * 0.55f, 130.f, 74.f * 0.55f, 56.f * 0.55f);
		F.Ticks(5);
		F.Redraw();
	}
	{
		FOv F;
		F.H.Mode = 4;
		F.P.Session.Heat = 0;
		F.SixRacers();
		F.O.Activate(OvRaceResults);
		F.Redraw();
		const FSSXFeTextRec* Msg = nullptr;
		for (const FSSXFeTextRec& Rr : F.D.TextRecs) { if (FMath::IsNearlyEqual(Rr.Pos.Y, 360.f) && Rr.Pos.X >= 69.f) { Msg = &Rr; break; } }
		NearTxt(*this, TEXT("qualified message box line 1"), Msg, 70.f, 360.f);
		TestNotNull(TEXT("WC heat title 3666"), F.Txt(F.L(3666)));
	}
	{
		// 0x1B0BB8
		FOv F;
		F.H.Mode = 3;
		F.H.Track = 0;
		FSSXShowoffStats S; S.Score = 61234; S.SpinDeg = 1440; S.FlipDeg = 720; S.Grabs = 9; S.Rails = 3; S.BiggestAir = 2.5; S.BestTrick = TEXT("Tail Grab");
		F.H.Stats = { S };
		F.O.Activate(OvRaceResults);
		F.Redraw();
		const float Row0 = 140.f + MenuLh * 0.85f + 12.f, Pitch = MenuLh * 0.85f + 3.f;
		NearTxt(*this, TEXT("score label (191, row 0)"), F.Txt(F.L(3524)), 191.f, Row0);
		NearTxt(*this, TEXT("score value right edge 441"), F.Txt(TEXT("61234")), 441.f - F.Ink(EFont::Menu, TEXT("61234"), 0.85f), Row0);
		NearTxt(*this, TEXT("180s = 1440 / 180"), F.Txt(TEXT("8")), 441.f - F.Ink(EFont::Menu, TEXT("8"), 0.85f), Row0 + Pitch);
		const FSSXFeTextRec* Sc = F.Txt(TEXT("61234"));
		if (Sc) TestTrue(TEXT("score row gold"), NearColor(Sc->Color, Gold));
		NearTxt(*this, TEXT("player name TITLE 0.7 (226, 122)"), F.Txt(TEXT("JOEL")), 226.f, 122.f);
		NearTxt(*this, TEXT("best trick label (191 - 25, 293)"), F.Txt(F.L(505)), 166.f, 293.f);
		NearTxt(*this, TEXT("best trick text box (191, 293 + 24 x 0.66 + 6)"), F.Txt(TEXT("Tail Grab")), 191.f, 293.f + 24.f * 0.66f + 6.f);
		NearSpr(*this, TEXT("1P medal (166, 112) 0.67 (61234 at Garibaldi = gold)"), F.Spr(HudMedalGold), 166.f, 112.f, 74.f * 0.67f, 56.f * 0.67f);
		TestNull(TEXT("no invented stats column at x 300"), F.Txt(F.L(3558)));
	}
	{
		// 0x1B16F0
		FOv F;
		F.H.Mode = 1;
		FSSXShowoffStats S; S.StylePoints = 4321; S.AvgSpeed = 50.0;
		F.H.Stats = { S };
		F.P.Settings.Units = 0;
		F.O.Activate(OvRaceResults);
		F.Redraw();
		const float Row0 = 112.f + MenuLh * 0.85f + 12.f, Pitch = MenuLh * 0.85f + 3.f;
		NearTxt(*this, TEXT("style points label (192, row 1)"), F.Txt(F.L(3558)), 192.f, Row0 + Pitch);
		NearTxt(*this, TEXT("style points value right edge 442"), F.Txt(TEXT("4321")), 442.f - F.Ink(EFont::Menu, TEXT("4321"), 0.85f), Row0 + Pitch);
		TestNotNull(TEXT("average speed in km/h"), F.Txt(TEXT("50 km/h")));
		NearTxt(*this, TEXT("Freeride title"), F.Txt(F.L(3654)), 116.f, 10.f + H37 + 30.f);
		NearTxt(*this, TEXT("Freeride name (192, 122)"), F.Txt(TEXT("JOEL")), 192.f, 122.f);
	}
	{
		FOv F;
		F.H.Mode = 7;
		F.P.Records[F.H.Track].Tables[2].Rows[0].Value = 9000;
		F.H.Results = { FOv::Racer(TEXT("JOEL"), 0, 1, 85.0) };
		F.Ticks(1);
		F.O.Activate(OvRaceResults);
		F.Redraw();
		NearTxt(*this, TEXT("TC title (box 380)"), F.Txt(F.L(3740)), 116.f, 50.f + H37 + 30.f);
		NearTxt(*this, TEXT("TC header difference (430, 175)"), F.Txt(F.L(3739)), 430.f, 175.f);
		NearTxt(*this, TEXT("TC user name (40, 200)"), F.Txt(TEXT("JOEL")), 40.f, 200.f);
		const FSSXFeTextRec* Dif = F.Txt(TEXT("-0:05.00"));
		NearTxt(*this, TEXT("TC difference (430, 200)"), Dif, 430.f, 200.f);
		if (Dif) TestTrue(TEXT("faster = green"), NearColor(Dif->Color, FeColor(1.f, 0.f, 1.f, 0.f)));
		const FSSXFeTextRec* Box = nullptr;
		for (const FSSXFeTextRec& Rr : F.D.TextRecs) { if (FMath::IsNearlyEqual(Rr.Pos.Y, 300.f) && FMath::IsNearlyEqual(Rr.Pos.X, 115.f)) Box = &Rr; }
		TestNotNull(TEXT("TC text box (115, 300) with 3190"), Box);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayPostRaceTest, "SSX.UI.Overlays.PostRace", OvTestFlags)
bool FSSXOverlayPostRaceTest::RunTest(const FString& Parameters)
{
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	{
		// 0x1BD898
		FOv F;
		F.H.Mode = 2;
		F.P.Session.PendingRecords.Add({ 0, 0, F.H.Track, 2 });
		F.O.Activate(OvHiScoreRecord);
		F.Redraw();
		using namespace SSXFeUserName;
		const TCHAR C0 = Cell(0, 0);
		const FString S0(1, &C0);
		const FSSXFeTextRec* A = F.Txt(S0);
		NearTxt(*this, TEXT("grid cell (0, 0)"), A, 166.f - F.Ink(EFont::Menu, S0, 1.f) * 0.5f, 316.f);
		if (A) TestFalse(TEXT("grid letters have no shadow"), A->bShadow);
		NearSpr(*this, TEXT("cursor 0x36"), F.Spr(0x36), 167.f - 25.f * 1.12f * 0.5f, 315.f, 25.f * 1.12f, 20.f * 1.1f);
		NearTxt(*this, TEXT("New Record! centred (320, 256)"), F.Txt(F.L(3616)), 320.f - F.Ink(EFont::Title, F.L(3616), 0.7f) * 0.5f, 256.f);
		const FSSXFeTextRec* L2 = F.Txt(F.L(3617));
		NearTxt(*this, TEXT("Enter your name (320, 280)"), L2, 320.f - F.Ink(EFont::Title, F.L(3617), 0.7f) * 0.5f, 280.f);
		if (L2) TestTrue(TEXT("3617 gold"), NearColor(L2->Color, Gold));
		const float Row0 = H37 + 79.f + MenuLh * 0.85f + 12.f, Pitch = MenuLh * 0.85f + 3.f;
		NearSpr(*this, TEXT("caret 0x32 (x[1] - 15, row + 2)"), F.Spr(0x32), 214.f - 15.f, Row0 + 2.f * Pitch + 2.f);
		NearTxt(*this, TEXT("rank header (131, 104)"), F.Txt(F.L(3679)), 131.f, 104.f);
		const FString End = F.L(274);
		const FSSXFeTextRec* E = F.Txt(End);
		NearTxt(*this, TEXT("END MENU 0.55 at (x - w / 2, y + 4)"), E, 166.f + 13.f * 23.f - F.Ink(EFont::Menu, End, 0.55f) * 0.5f, 316.f + 44.f + 4.f);
	}
	{
		// FUN_0019DF90
		FOv F;
		F.H.Mode = 2;
		F.H.Unlocks = { { 0, 4, 3 } };
		F.O.Activate(OvUnlocked);
		const FString Row = F.L(563).Replace(TEXT("%d"), TEXT("1"));
		F.Ticks(1 + 4);
		F.Redraw();
		NearTxt(*this, TEXT("CONGRATULATIONS header"), F.Txt(F.L(3574) + TEXT("JOEL")), 116.f, H37 + 45.f);
		const FSSXFeTextRec* U = F.Txt(F.L(3575));
		NearTxt(*this, TEXT("You have unlocked (116, y + 28) TITLE"), U, 116.f, H37 + 45.f + 28.f);
		if (U) TestTrue(TEXT("3575 gold"), NearColor(U->Color, Gold));
		const float Row0 = H37 + 91.f + MenuLh * 0.85f + 12.f;
		NearTxt(*this, TEXT("typed part of row 0 (156)"), F.Txt(Row.Left(4)), 156.f, Row0);
		const FSSXFeTextRec* Next = F.Txt(Row.Mid(4, 1));
		if (TestNotNull(TEXT("next character"), Next)) TestTrue(TEXT("next character at half alpha"), FMath::IsNearlyEqual(Next->Color.A, 0.5f));
		NearSpr(*this, TEXT("large icon of the typing row (44, 160)"), F.Spr(0x5B), 44.f, 160.f);
		F.Ticks(200);
		F.Redraw();
		NearTxt(*this, TEXT("whole row once typed"), F.Txt(Row), 156.f, Row0);
		TestNotNull(TEXT("continue legend"), F.Txt(F.L(397)));
	}
	{
		// 0x1BCD28
		FOv F;
		F.H.Mode = 2;
		F.O.bNextEventBack = true;
		F.O.Activate(OvSelectNextEvent);
		F.Redraw();
		NearTxt(*this, TEXT("Next Event title"), F.Txt(F.L(3657)), 116.f, H37 + 97.f);
		const FSSXFeTextRec* Help = nullptr;
		for (const FSSXFeTextRec& Rr : F.D.TextRecs) { if (FMath::IsNearlyEqual(Rr.Pos.Y, H37 + 129.f) && FMath::IsNearlyEqual(Rr.Pos.X, 134.f)) Help = &Rr; }
		if (TestNotNull(TEXT("help box (134, h37 + 129)"), Help)) TestTrue(TEXT("help gold"), NearColor(Help->Color, Gold));
		float Mw = 0.f;
		for (int32 Id : { 411, 457, 3194, 456, 399 }) Mw = FMath::Max(Mw, F.Ink(EFont::Menu, F.L(Id), 0.92f));
		NearTxt(*this, TEXT("item 1 (block left, top 240 + pitch)"), F.Txt(F.L(457)), 320.f - Mw * 0.5f, 240.f + MenuLh * 0.92f + 4.f);
		const float Bw = F.Ink(EFont::Menu, F.L(471), 0.73f);
		NearTxt(*this, TEXT("back at (600 - w, 350) bottom-aligned"), F.Txt(F.L(471)), 600.f - Bw, 350.f - (MenuLh * 0.73f + 4.f));
	}
	{
		FOv F;
		F.H.Mode = 4;
		F.O.NextVenueMode = 4;
		F.O.Activate(OvSelectNextVenue);
		F.Redraw();
		NearTxt(*this, TEXT("Next Venue title (116, h37 + 17)"), F.Txt(F.L(3662)), 116.f, H37 + 17.f);
		NearTxt(*this, TEXT("current medal header"), F.Txt(F.L(342)), 450.f, 148.f);
		const FSSXFeTextRec* M1 = F.Txt(F.L(306), 1);
		NearTxt(*this, TEXT("medal row 1"), M1, 450.f, 170.f + (MenuLh * 0.92f + 4.f));
		int32 N = 0;
		for (const FSSXFeTextRec& Rr : F.D.TextRecs) N += Rr.Str == F.L(306);
		TestEqual(TEXT("one medal text per venue (8)"), N, 8);
	}
	{
		FOv F;
		F.H.Mode = 4;
		FSSXPointSession& S = F.P.Session.Points[0];
		S.Char = 0; S.Left = 3; S.Initial = 5; S.Cur[0] = 4; S.Max[0] = 10;
		F.O.Activate(OvPointDistribution);
		F.Redraw();
		NearTxt(*this, TEXT("points title"), F.Txt(F.L(3750)), 115.f, 48.f);
		NearTxt(*this, TEXT("EDGING right edge 177"), F.Txt(F.L(37)), 177.f - F.Ink(EFont::Menu, F.L(37), 0.88f), 265.f);
		NearTxt(*this, TEXT("Points left right edge 328"), F.Txt(F.L(3195)), 328.f - F.Ink(EFont::Menu, F.L(3195), 0.88f), 240.f);
		const FSSXFeTextRec* Rn = F.Txt(SSXFeTables::RiderName(0));
		if (TestNotNull(TEXT("rider name"), Rn)) TestTrue(TEXT("rider name (1, 1, .471, .039)"), NearColor(Rn->Color, FeColor(1.f, 1.f, 0.471f, 0.039f)));
		const FString Must = F.L(3193).Left(12);
		const FSSXFeTextRec* Warn = nullptr;
		for (const FSSXFeTextRec& Rr : F.D.TextRecs) { if (Rr.Str.StartsWith(Must)) Warn = &Rr; }
		NearTxt(*this, TEXT("distribute-all box (115, 370) while points are left"), Warn, 115.f, 370.f);
		TestNotNull(TEXT("progress bar track 0x44"), F.Spr(0x44));
		NearSpr(*this, TEXT("points-left track (335, 239) 30 wide"), F.Spr(0x44), 335.f, 239.f, 30.f, 10.f);
		TestNotNull(TEXT("pips 0x42"), F.Spr(0x42));
		TestEqual(TEXT("no untextured bar rects"), F.Rects().Num(), 3);
	}
	{
		FOv F;
		F.P.Session.RankAfter = 7;
		F.P.Session.bRankUp = true;
		F.O.RankCardFrames = 300;
		F.Redraw();
		NearTxt(*this, TEXT("3365 y 165"), F.Txt(F.L(3365)), 320.f - F.Ink(EFont::Menu, F.L(3365), 0.9f) * 0.5f, 165.f);
		const FSSXFeTextRec* New = F.Txt(F.L(30));
		NearTxt(*this, TEXT("new rank LOC 23 + 7 at y 295"), New, 320.f - F.Ink(EFont::Menu, F.L(30), 1.2f) * 0.5f, 295.f);
		NearTxt(*this, TEXT("old rank = new LOC + 1 at y 230"), F.Txt(F.L(31)), 320.f - F.Ink(EFont::Menu, F.L(31), 1.2f) * 0.5f, 230.f);
		NearTxt(*this, TEXT("loading right edge 600"), F.Txt(F.L(373)), 600.f - F.Ink(EFont::Menu, F.L(373), 0.87f), 323.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayPreRaceTest, "SSX.UI.Overlays.PreRace", OvTestFlags)
bool FSSXOverlayPreRaceTest::RunTest(const FString& Parameters)
{
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	{
		// 0x1B4250
		FOv F;
		F.H.Mode = 2;
		F.SixRacers();
		F.O.Activate(OvNextRace);
		F.Redraw();
		const float Row0 = 137.f + MenuLh * 0.85f + 12.f, Pitch = MenuLh * 0.85f + 23.f;
		NearTxt(*this, TEXT("title 519"), F.Txt(F.L(519)), 116.f, 10.f + H37 + 30.f);
		NearTxt(*this, TEXT("riders header"), F.Txt(F.L(3680)), 131.f, Row0 - 48.f);
		NearTxt(*this, TEXT("aggression header"), F.Txt(F.L(3676)), 259.f, Row0 - 48.f);
		NearTxt(*this, TEXT("first AI row (Kaori)"), F.Txt(TEXT("Kaori")), 131.f, Row0);
		TestNull(TEXT("no human row"), F.Txt(TEXT("JOEL")));
		const FSSXFeTextRec* Enemy = F.Txt(F.L(8));
		NearTxt(*this, TEXT("Mac's label Enemy (259, row 1 + 3)"), Enemy, 259.f, Row0 + Pitch + 3.f);
		if (Enemy) TestTrue(TEXT("Enemy red (1, .9, .1, .1)"), NearColor(Enemy->Color, FeColor(1.f, 0.9f, 0.1f, 0.1f)));
		NearTxt(*this, TEXT("vs. name TITLE 0.47"), F.Txt(F.L(3767).Replace(TEXT("%S"), TEXT("JOEL"))), 259.f, Row0 - 23.f);
		const FSSXFeTextRec* Box = nullptr;
		for (const FSSXFeTextRec& Rr : F.D.TextRecs) { if (FMath::IsNearlyEqual(Rr.Pos.Y, 352.f) && FMath::IsNearlyEqual(Rr.Pos.X, 116.f)) Box = &Rr; }
		if (TestNotNull(TEXT("text box (116, 352)"), Box)) TestTrue(TEXT("text box gold"), NearColor(Box->Color, Gold));
		TArray<const FSSXFeDrawCmd*> R = F.Rects();
		if (R.Num() > 0) TestTrue(TEXT("backdrop alpha forced to 0.8"), FMath::IsNearlyEqual(R[0]->Color.A, 0.8f));
	}
	{
		FOv F;
		F.H.Mode = 3;
		F.H.Track = 0;
		F.O.Activate(OvNextRace);
		F.Redraw();
		NearSpr(*this, TEXT("disc 1"), F.Spr(0x2E, 0), 50.f, 150.f, 50.f, 56.f);
		NearSpr(*this, TEXT("disc 3"), F.Spr(0x2E, 2), 50.f, 280.f);
		NearTxt(*this, TEXT("x2 legend"), F.Txt(F.L(516)), 110.f, 170.f);
		NearTxt(*this, TEXT("gold target \"55000 pts\""), F.Txt(F.L(389).Replace(TEXT("%S"), TEXT("55000"))), 486.f, 194.f);
		NearSpr(*this, TEXT("target medal icon (426, 189) 0.6"), F.Spr(HudMedalGold), 426.f, 189.f, 74.f * 0.6f, 56.f * 0.6f);
		NearTxt(*this, TEXT("best score message (116, 118)"), F.Txt(F.L(3647).Replace(TEXT("%d"), *FString::FromInt(F.P.Records[0].Tables[1].Rows[0].Value))), 116.f, 118.f);
	}
	{
		// 0x1BD3B8
		FOv F;
		F.O.Activate(OvHelpRace);
		F.Redraw();
		const float W3 = F.Ink(EFont::Menu, F.L(3567), 0.9f);
		NearTxt(*this, TEXT("line 1 (160, 170)"), F.Txt(F.L(3565)), 160.f, 170.f);
		NearTxt(*this, TEXT("3568 after w + 44"), F.Txt(F.L(3568)), 160.f + W3 + 44.f, 270.f);
		NearSpr(*this, TEXT("R2 icon (x + 8 + w, 262)"), F.Spr(HudR2), 160.f + 8.f + W3, 262.f);
		const FSSXFeDrawCmd* Bul = nullptr;
		for (const FSSXFeDrawCmd* Rc : F.Rects()) { if (FMath::IsNearlyEqual(Rc->Size.X, 7.f)) { Bul = Rc; break; } }
		NearSpr(*this, TEXT("bullet 1 (142, 175) 7 x 7"), Bul, 142.f, 175.f, 7.f, 7.f);
		NearTxt(*this, TEXT("title TITLE 0.91 centred (320, 105)"), F.Txt(F.L(3773)), 320.f - F.Ink(EFont::Title, F.L(3773), 0.91f) * 0.5f, 105.f);
	}
	{
		FOv F;
		F.H.Mode = 2;
		F.H.Finishes = { 0, 0 };
		F.H.FinishValues = { TEXT("1:23.45") };
		F.Ticks(12);
		F.Redraw();
		NearSpr(*this, TEXT("finish medal"), F.Spr(HudMedalGold), 72.f, 217.f, 74.f * 1.3f, 56.f * 1.3f);
		const FString Place = TEXT("1") + F.L(3652) + TEXT(" ") + F.L(3663);
		NearTxt(*this, TEXT("place (175, 225)"), F.Txt(Place), 175.f, 225.f);
		const FSSXFeTextRec* V = F.Txt(TEXT("1:23.45"));
		NearTxt(*this, TEXT("value TITLE 1.0 centred (320, 185)"), V, 320.f - F.Ink(EFont::Title, TEXT("1:23.45"), 1.f) * 0.5f, 185.f);
		if (V) TestTrue(TEXT("value gold"), FMath::IsNearlyEqual(V->Color.G, Gold.G));
	}
	{
		// FUN_00250D60
		FOv F;
		F.O.Activate(OvPopAsyncLoad);
		F.Redraw();
		const FSSXFeTextRec* Ld = F.Txt(F.L(373));
		NearTxt(*this, TEXT("loading"), Ld, 600.f - F.Ink(EFont::Menu, F.L(373), 1.f), 440.f);
		if (Ld) TestTrue(TEXT("loading has the shadow"), Ld->bShadow);
		int32 Sprites = 0;
		for (const FSSXFeDrawCmd& Cm : F.D.Cmds) Sprites += Cm.SpriteId >= 0;
		TestEqual(TEXT("no spinner sprite"), Sprites, 0);
	}
	{
		FOv F;
		F.H.Phase = 3;
		F.H.Countdown = -100.0;
		F.Ticks(1);
		F.Redraw();
		const FSSXFeTextRec* Dg = F.Txt(TEXT("2"));
		if (TestNotNull(TEXT("countdown digit"), Dg)) TestTrue(TEXT("digit has the shadow"), Dg->bShadow);
		F.H.Phase = 4;
		F.Ticks(2);
		F.Redraw();
		const FSSXFeTextRec* Go = F.Txt(F.L(3759));
		if (TestNotNull(TEXT("GO!"), Go)) TestTrue(TEXT("GO! has the shadow"), Go->bShadow);
	}
	{
		FOv F;
		F.H.bCanReplay = true;
		F.H.Mode = 2;
		F.SixRacers();
		F.O.Activate(OvRaceResults);
		F.O.ResultsSel = 2;
		F.O.Pad(0, PadCross);
		TestTrue(TEXT("replay item starts the replay (state 1, control active, no menu)"), F.O.IsActive(OvReplay) && F.O.ReplayState == 1 && F.O.IsActive(OvReplayControl) && !F.O.IsReplayMenuOpen());
		F.O.Pad(0, PadStart);
		TestTrue(TEXT("Start opens the replay menu on exit (0x1EFDB0)"), F.O.IsReplayMenuOpen() && F.O.ReplaySel == 1 && F.H.ReplayMenuOpens == 1);
		F.Redraw();
		NearSpr(*this, TEXT("replay tab 0x3E at y 155 (after the control's tab at y 18, slot 5 drawn first)"), F.Spr(HudTabL, 1), 0.f, 155.f);
		float Mw = 0.f;
		for (int32 Id : { 391, 353, 475 }) Mw = FMath::Max(Mw, F.Ink(EFont::Menu, F.L(Id), 0.92f));
		const float Pitch = MenuLh * 0.92f + 4.f;
		NearTxt(*this, TEXT("exit item"), F.Txt(F.L(353)), 320.f - Mw * 0.5f, 240.f - 1.5f * Pitch + Pitch);
		F.O.Pad(0, PadCross);
		TestTrue(TEXT("exit returns to the results"), F.O.IsActive(OvRaceResults) && !F.O.IsActive(OvReplay) && !F.O.IsActive(OvReplayControl) && F.H.ReplayStops == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayReplayControlTest, "SSX.UI.Overlays.ReplayControl", OvTestFlags)
bool FSSXOverlayReplayControlTest::RunTest(const FString& Parameters)
{
	// 0x1EF990 / 0x191B60 / 0x1EC548 / 0x1EB6D0
	if (!FOvAssets::Get().Ready())
	{
		SSXTest::Skip(*this, TEXT("FE fonts / HUD sprites / LOC not converted: skipped"));
		return true;
	}
	FOv F;
	FSSXReplay R;
	R.Init(2, 1, false, nullptr);
	R.AttachHuman(0);
	R.OnStartRace();
	for (int32 I = 0; I < 200; ++I)
	{
		R.Update();
		uint32 W = uint32(I / 20);
		R.RecordOrPlayInput(0, W);
	}
	R.EndRecording();
	R.Update();
	F.H.ReplayObj = &R;
	F.H.ReplayTarget = TEXT("JOEL");
	F.H.bCanReplay = true;
	F.H.Mode = 2;
	F.SixRacers();
	F.O.Activate(OvRaceResults);
	F.O.ResultsSel = 2;
	F.O.Pad(0, PadCross);
	TestTrue(TEXT("replay overlay + control (slot 5) active"), F.O.IsActive(OvReplay) && F.O.IsActive(OvReplayControl));
	TestEqual(TEXT("0x191B60: state 7"), R.GetState(), int32(SSXReplay::Started));
	TestEqual(TEXT("0x1EC548: command 1 (to the start)"), R.GetCommand(), int32(SSXReplay::CmdToStart));
	TestEqual(TEXT("Normal: help level 0 of 1, panel at 480"), F.O.GetReplayControl().HelpLevel * 1000 + int32(F.O.GetReplayControl().PanelY), 480);
	R.Update();
	R.Update();
	R.Update();
	TestEqual(TEXT("paused at the start"), R.GetState(), int32(SSXReplay::Paused));
	F.O.Sounds.Reset();
	F.O.SetPadHeld(0, PadCross);
	F.Ticks(1);
	TestEqual(TEXT("X while paused: command 6"), R.GetCommand(), int32(SSXReplay::CmdPlayToggle));
	TestTrue(TEXT("X: navigate sound, audio resumed (0x215488)"), F.O.Sounds.Contains(SSXUISound::ERace::Navigate) && F.H.ReplayAudioResumes > 0);
	F.O.SetPadHeld(0, 0);
	F.Ticks(1);
	F.O.SetPadHeld(0, PadUp);
	F.Ticks(1);
	F.O.SetPadHeld(0, 0);
	F.Ticks(1);
	TestEqual(TEXT("0x1EC728: the panel slides 6 per frame (+0x4A4 set while it moves)"), int32(F.O.GetReplayControl().PanelY) * 10 + (F.O.GetReplayControl().bHelpChanged ? 1 : 0), 4741);
	F.Ticks(19);
	TestEqual(TEXT("Up: help level 1, panel y 360 (+0x354)"), F.O.GetReplayControl().HelpLevel * 1000 + int32(F.O.GetReplayControl().PanelY), 1360);
	TestTrue(TEXT("Up: help sound (0x214428)"), F.O.Sounds.Contains(SSXUISound::ERace::ReplayHelp));
	F.Redraw();
	TestNotNull(TEXT("title tab LOC 0xEB8"), F.Txt(F.L(0xEB8)));
	TestNotNull(TEXT("camera label (mode 0: LOC 0x14D)"), F.Txt(F.L(0x14D)));
	TestNotNull(TEXT("target rider name"), F.Txt(TEXT("JOEL")));
	const float Fh = MenuLh * 0.5f;
	NearTxt(*this, TEXT("legend LOC 425 right edge at 125, y 360 + 35 - fh / 2"), F.Txt(F.L(0x1A9)), 125.f - F.Ink(EFont::Menu, F.L(0x1A9), 0.85f), 360.f + 35.f - Fh);
	NearTxt(*this, TEXT("legend LOC 433 right edge at 568, y 360 + 83 - fh / 2"), F.Txt(F.L(0x1B1)), 568.f - F.Ink(EFont::Menu, F.L(0x1B1), 0.85f), 360.f + 83.f - Fh);
	if (const FSSXFeDrawCmd* X = F.Spr(HudCross); TestNotNull(TEXT("X icon 0x74"), X))
	{
		Near(*this, TEXT("X icon centre x 154"), X->Pos.X + X->Size.X * 0.5f, 154.f);
		Near(*this, TEXT("X icon centre y 360 + 35"), X->Pos.Y + X->Size.Y * 0.5f, 395.f);
	}
	TestNotNull(TEXT("START label"), F.Txt(TEXT("START")));
	TestTrue(TEXT("panel box (0, 360, 640, 120)"), F.Rects().ContainsByPredicate([](const FSSXFeDrawCmd* C)
		{ return FMath::IsNearlyEqual(C->Pos.X, 0.f, 1.f) && FMath::IsNearlyEqual(C->Pos.Y, 360.f, 1.f) && FMath::IsNearlyEqual(C->Size.X, 640.f, 1.f) && FMath::IsNearlyEqual(C->Size.Y, 120.f, 1.f); }));
	// 0x214428 / 0x214650 / 0x213D60
	TestTrue(TEXT("replay help / camera sounds: zBxsfx #125 / #127"), SSXUISound::RaceSound(SSXUISound::ERace::ReplayHelp).Sound == 125
		&& SSXUISound::RaceSound(SSXUISound::ERace::ReplayCamera).Sound == 127);
	// 0x1EBFC0 / 0x22D370 / 0x215370
	F.H.RacerCount = 6;
	F.O.SetPadHeld(0, 0);
	F.Ticks(1);
	F.O.SetPadHeld(0, PadRight);
	F.Ticks(1);
	F.O.SetPadHeld(0, 0);
	F.Ticks(1);
	TestEqual(TEXT("Right: target audio once"), F.H.ReplayTargetAudios, 1);
	// 0x1EF990(2) / (3): Continue with X while the replay plays: the control (slot 5, updated before slot 9) still sees
	// the menu busy that frame, so the X does not pause the replay (command 7).
	R.Update();
	TestEqual(TEXT("playing before the menu"), R.GetState(), int32(SSXReplay::Play));
	F.O.Pad(0, PadStart);
	F.Ticks(1);
	F.O.ReplaySel = 2;
	F.O.SetPadHeld(0, PadCross);
	F.O.Pad(0, PadCross);
	F.Ticks(1);
	TestTrue(TEXT("Continue closes the menu"), !F.O.IsReplayMenuOpen() && F.H.ReplayMenuCloses == 1);
	TestTrue(TEXT("the Continue press does not reach the replay control"), R.GetCommand() != int32(SSXReplay::CmdPause));
	F.O.SetPadHeld(0, 0);
	F.Ticks(1);
	// 0x191BE0
	F.O.Pad(0, PadStart);
	F.Ticks(1);
	TestTrue(TEXT("menu up: control busy (+0x49C)"), F.O.GetReplayControl().bBusy);
	F.O.Pad(0, PadCross);
	TestTrue(TEXT("exit: back to the results, slots 9 / 5 off"), F.O.IsActive(OvRaceResults) && !F.O.IsActive(OvReplayControl) && F.H.ReplayStops == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayReplayEditorTest, "SSX.UI.Overlays.ReplayEditor", OvTestFlags)
bool FSSXOverlayReplayEditorTest::RunTest(const FString& Parameters)
{
	// 0x1EDEF0 / 0x1ED490 / 0x1EEE00
	if (!FOvAssets::Get().Ready())
	{
		SSXTest::Skip(*this, TEXT("FE fonts / HUD sprites / LOC not converted: skipped"));
		return true;
	}
	FOv F;
	FSSXReplay R;
	R.Init(2, 1, true, nullptr);
	R.AttachHuman(0);
	R.OnStartRace();
	for (int32 I = 0; I < 600; ++I) { R.Update(); uint32 W = uint32(I / 20); R.RecordOrPlayInput(0, W); }
	R.EndRecording();
	R.Update();
	F.H.ReplayObj = &R;
	F.H.ReplayTarget = TEXT("JOEL");
	F.H.bCanReplay = true;
	F.H.Mode = 2;
	F.SixRacers();
	F.O.Activate(OvRaceResults);
	F.O.ResultsSel = 2;
	F.O.Pad(0, PadCross);
	TestTrue(TEXT("editor control: help level 1, panel 360"), F.O.GetReplayControl().bEditor && F.O.GetReplayControl().HelpLevel == 1 && F.O.GetReplayControl().PanelY == 360.f);
	for (int32 I = 0; I < 3; ++I) { R.Update(); F.Ticks(1); }
	TestTrue(TEXT("paused at 0"), R.GetState() == SSXReplay::Paused && R.GetPlayFrame() == 0);
	F.Redraw();
	constexpr float SX = 1.25f, SY = 480.f / 448.f;
	const FSSXSprite* S2a = FOvAssets::Get().Sheet.Sprites.Find(0x2A);
	const float H2a = S2a ? S2a->H : 31.f;
	const float Y0 = 360.f * 0.93333334f + 26.04651f;
	if (const FSSXFeDrawCmd* B = F.Spr(0x2A, 0); TestNotNull(TEXT("0x1EDEF0 bar (sprite 0x2A)"), B))
	{
		Near(*this, TEXT("bar x 51.2 x 1.25"), B->Pos.X, 64.f);
		Near(*this, TEXT("bar y (panel x 0.9333 + 26.05) x 480 / 448"), B->Pos.Y, Y0 * SY);
		Near(*this, TEXT("bar width 409.6 x 1.25"), B->Size.X, 512.f);
		Near(*this, TEXT("bar height (9 - 0.5) x 480 / 448"), B->Size.Y, 8.5f * SY, 0.05f);
		TestEqual(TEXT("bar layer 0xD"), B->Layer, 0xD);
	}
	if (const FSSXFeDrawCmd* Cu = F.Spr(0x2A, 1); TestNotNull(TEXT("0x1EDEF0 cursor"), Cu))
	{
		Near(*this, TEXT("cursor x (51.2 - 2) x 1.25 at frame 0"), Cu->Pos.X, 61.5f);
		Near(*this, TEXT("cursor y (y - 4)"), Cu->Pos.Y, (Y0 - 4.f) * SY);
		Near(*this, TEXT("cursor 5 x 0.6 h"), Cu->Size.X + Cu->Size.Y, 5.f * SX + 0.6f * H2a * SY, 0.05f);
		TestTrue(TEXT("cursor red .8 (no clip starts here), layer 0xE (after the frame call)"), Cu->Layer == 0xE && FMath::IsNearlyEqual(Cu->Color.A, 0.8f) && Cu->Color.G == 0.f);
	}
	const float Ht = H2a * 0.85f, Hm1 = Ht * 0.5f - 1.f;
	const float Yz = 360.f * 0.93333334f + 68.92308f;
	if (const FSSXFeDrawCmd* B = F.Spr(0x2A, 2); TestNotNull(TEXT("0x1ED490 bar"), B))
	{
		Near(*this, TEXT("zoom bar x 76.8 x 1.25"), B->Pos.X, 96.f);
		Near(*this, TEXT("zoom bar y (panel x 0.9333 + 68.92 - h / 2)"), B->Pos.Y, (Yz - Ht * 0.5f) * SY);
		Near(*this, TEXT("zoom bar width 358.4 x 1.25"), B->Size.X, 448.f);
		TestEqual(TEXT("zoom bar layer 0xE"), B->Layer, 0xE);
	}
	if (const FSSXFeDrawCmd* Cu = F.Spr(0x2A, 3); TestNotNull(TEXT("0x1ED490 cursor"), Cu))
	{
		Near(*this, TEXT("zoom cursor x (91.8 - 3) x 1.25"), Cu->Pos.X, 111.f);
		Near(*this, TEXT("zoom cursor y (y - (h / 2 + 1) - 4)"), Cu->Pos.Y, (Yz - (Hm1 + 2.f) - 4.f) * SY);
		TestEqual(TEXT("zoom cursor layer 0xF"), Cu->Layer, 0xF);
	}
	const TArray<const FSSXFeDrawCmd*> Rs = F.Rects();
	const bool bCutBox = Rs.ContainsByPredicate([&](const FSSXFeDrawCmd* C)
		{ return FMath::IsNearlyEqual(C->Pos.X, 90.f * SX, 0.05f) && FMath::IsNearlyEqual(C->Size.X, 4.f * SX, 0.05f) && FMath::IsNearlyEqual(C->Color.B, 0.f) && FMath::IsNearlyEqual(C->Color.G, 1.f); });
	TestTrue(TEXT("frame-0 cut box (yellow while paused on it)"), bCutBox);
	const bool bKeyTick = Rs.ContainsByPredicate([&](const FSSXFeDrawCmd* C)
		{ return FMath::IsNearlyEqual(C->Pos.X, 91.8f * SX, 0.05f) && FMath::IsNearlyEqual(C->Color.R, 0.2f) && FMath::IsNearlyEqual(C->Size.Y, 2.f * Hm1 * SY, 0.05f); });
	TestTrue(TEXT("keyframe tick at frame 0 (grey, the bar's inner height)"), bKeyTick);
	TestNull(TEXT("no Normal legend in the editor (LOC 425)"), F.Txt(F.L(0x1A9)));
	TestNull(TEXT("no editor legend at help level 1"), F.Txt(F.L(0x1A5)));
	F.O.SetPadHeld(0, PadUp);
	F.Ticks(1);
	F.O.SetPadHeld(0, 0);
	F.Ticks(50);
	TestEqual(TEXT("panel at 105"), F.O.GetReplayControl().PanelY, 105.f);
	F.Redraw();
	const float Base = 105.f + 70.f;
	NearTxt(*this, TEXT("label 0x1A5 right of 320 - 108, y base + 64"), F.Txt(F.L(0x1A5)), 212.f - F.Ink(EFont::Menu, F.L(0x1A5), 0.9f), Base + 64.f);
	NearTxt(*this, TEXT("label 0x1B0 left at 320 + 118"), F.Txt(F.L(0x1B0)), 438.f, Base + 64.f);
	NearTxt(*this, TEXT("label 0x1B4 centred on 336"), F.Txt(F.L(0x1B4)), 336.f - F.Ink(EFont::Menu, F.L(0x1B4), 0.9f) * 0.5f, Base + 101.f);
	if (const FSSXFeDrawCmd* Ic = F.Spr(0x6B); TestNotNull(TEXT("legend icon 0x6B"), Ic))
	{
		Near(*this, TEXT("icon 0x6B centre (338, base + 155)"), Ic->Pos.X + Ic->Size.X * 0.5f, 338.f);
		Near(*this, TEXT("icon 0x6B centre y"), Ic->Pos.Y + Ic->Size.Y * 0.5f, Base + 155.f);
		TestEqual(TEXT("legend icons layer 0xE"), Ic->Layer, 0xE);
	}
	const bool bLeader = F.Rects().ContainsByPredicate([&](const FSSXFeDrawCmd* C)
		{ return FMath::IsNearlyEqual(C->Pos.X, 336.f, 0.05f) && FMath::IsNearlyEqual(C->Size.X, 3.f, 0.05f) && FMath::IsNearlyEqual(C->Size.Y, 26.f, 0.05f) && C->Layer == 0xD; });
	TestTrue(TEXT("0x1B4 leader 3 x 26 at x 336"), bLeader);
	TestTrue(TEXT("rotated small icon 0x2F (flags 0x10) next to 0x1A6"), F.D.Cmds.ContainsByPredicate([](const FSSXFeDrawCmd& C) { return C.SpriteId == 0x2F && C.bRot90; }));
	// 0x1FC178 / 0x19BDF8
	if (const FSSXFeDrawCmd* Rot = F.D.Cmds.FindByPredicate([](const FSSXFeDrawCmd& C) { return C.SpriteId == 0x2F && C.bRot90; }))
	{
		FVector2f Uv[4];
		FSSXFeDrawList::CornerUVs(Rot->UV0, Rot->UV1, true, Uv);
		TestTrue(TEXT("0x19BDF8 corners: TL (u1, v0), TR (u1, v1), BR (u0, v1), BL (u0, v0)"),
			Uv[0] == FVector2f(Rot->UV1.X, Rot->UV0.Y) && Uv[1] == Rot->UV1 && Uv[2] == FVector2f(Rot->UV0.X, Rot->UV1.Y) && Uv[3] == Rot->UV0);
	}
	{
		// 0x1FC178
		F.D.Reset();
		const bool bIcon = F.D.Icon(41, 300.f, 200.f, 40.f, 10.f, 0x1C, White, 1);
		if (!bIcon) { AddInfo(TEXT("FE sprite table not loaded here: the turned-origin check is covered by SSX.UI.TrickBook")); }
		if (bIcon)
		{
			const FSSXFeDrawCmd& C = F.D.Cmds.Last();
			TestTrue(TEXT("0x1C: turned 10 x 40 quad"), C.bRot90 && FMath::IsNearlyEqual(C.Size.X, 10.f) && FMath::IsNearlyEqual(C.Size.Y, 40.f));
			Near(*this, TEXT("0x1C: x - quad width (h)"), C.Pos.X, 290.f);
			Near(*this, TEXT("0x1C: y - quad height (w)"), C.Pos.Y, 160.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayResetFadeTest, "SSX.UI.Overlays.ResetFade", OvTestFlags)
bool FSSXOverlayResetFadeTest::RunTest(const FString& Parameters)
{
	// 0x1AF320
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	FOv F;
	F.H.RiderFade = 0.f;
	F.Ticks(1);
	F.Redraw();
	auto FadeRect = [&F]() -> const FSSXFeDrawCmd*
	{
		for (const FSSXFeDrawCmd* Rc : F.Rects()) { if (Rc->Layer == 0xB && FMath::IsNearlyEqual(Rc->Size.X, 640.f) && FMath::IsNearlyEqual(Rc->Size.Y, 480.f)) return Rc; }
		return nullptr;
	};
	TestFalse(TEXT("fade 0: overlay off"), F.O.IsActive(OvBlackFade));
	TestNull(TEXT("fade 0: no quad"), FadeRect());
	F.H.RiderFade = 0.5f;
	F.Ticks(1);
	F.Redraw();
	TestTrue(TEXT("fade > 0: overlay on"), F.O.IsActive(OvBlackFade));
	const FSSXFeDrawCmd* Q = FadeRect();
	if (TestNotNull(TEXT("full-screen quad"), Q))
	{
		TestTrue(TEXT("at (0, 0)"), Q->Pos.IsNearlyZero());
		TestTrue(TEXT("white, alpha = +0x44C"), NearColor(Q->Color, FLinearColor(1.f, 1.f, 1.f, 0.5f)));
		const FLinearColor S = FSSXFeDrawList::ScreenColor(*Q);
		TestTrue(TEXT("on screen mid grey 128 / 255"), FMath::IsNearlyEqual(S.R, 128.f / 255.f, 0.002f) && FMath::IsNearlyEqual(S.G, S.R) && FMath::IsNearlyEqual(S.B, S.R));
	}
	F.H.RiderFade = 0.f;
	F.Ticks(1);
	TestFalse(TEXT("exit 0x106A48 (fade 0): overlay off"), F.O.IsActive(OvBlackFade));
	return true;
}

// 0x1A8668 / 0x1A8648 / 0x1A8658 / 0x1A8638
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayMenuSoundsTest, "SSX.UI.Overlays.MenuSounds", OvTestFlags)
bool FSSXOverlayMenuSoundsTest::RunTest(const FString& Parameters)
{
	using ER = SSXUISound::ERace;
	auto Press = [](FOv& F, uint32 Bits) { F.O.Sounds.Reset(); F.O.Pad(0, Bits); return F.O.Sounds; };
	{
		// 0x1ACE00 / 0x1AAC08
		FOv F;
		F.O.Pad(0, PadStart);
		TestTrue(TEXT("pause: Down + X in one frame = navigate only"), Press(F, PadDown | PadCross) == TArray<ER>({ ER::Navigate }) && F.O.PauseSel == 1 && !F.O.Dialog.bActive);
		TestTrue(TEXT("pause: X on restart = accept, box"), Press(F, PadCross) == TArray<ER>({ ER::Accept }) && F.O.Dialog.bActive);
		TestTrue(TEXT("pause box: Triangle = back sound"), Press(F, PadTriangle) == TArray<ER>({ ER::Back }) && !F.O.Dialog.bActive);
	}
	{
		// 0x1B32C0
		FOv F;
		F.H.Mode = 2;
		F.O.Activate(OvRaceResults);
		TestTrue(TEXT("results: Up wraps to the last item, navigate"), Press(F, PadUp) == TArray<ER>({ ER::Navigate }) && F.O.ResultsSel == 4);
		TestTrue(TEXT("results: Down wraps to 0, navigate"), Press(F, PadDown) == TArray<ER>({ ER::Navigate }) && F.O.ResultsSel == 0);
		F.O.ResultsSel = 4;
		TestTrue(TEXT("results: X on quit = accept, prompt"), Press(F, PadCross) == TArray<ER>({ ER::Accept }) && F.O.Dialog.bActive);
		TestTrue(TEXT("quit box: Triangle = back sound, stays"), Press(F, PadTriangle) == TArray<ER>({ ER::Back }) && !F.O.Dialog.bActive && F.H.ExitCode < 0);
		F.P.bSettingsDirty = true;
		F.O.ResultsSel = 4;
		Press(F, PadCross);
		TestTrue(TEXT("save box: Circle + X = Circle first (back sound, quit without saving)"), Press(F, PadCircle | PadCross) == TArray<ER>({ ER::Back }) && F.H.ExitCode == 2);
	}
	{
		// 0x1B3EC8 / 0x1B6AC0
		FOv F;
		F.O.Activate(OvViewHiScores);
		TestTrue(TEXT("records: Select does nothing"), Press(F, PadSelect).Num() == 0 && F.O.IsActive(OvViewHiScores));
		TestTrue(TEXT("records: Triangle = accept sound -> results"), Press(F, PadTriangle) == TArray<ER>({ ER::Accept }) && F.O.IsActive(OvRaceResults));
		F.O.Deactivate(OvRaceResults);
		F.O.Activate(OvUnlocked);
		TestTrue(TEXT("unlocked: X = accept -> results"), Press(F, PadCross) == TArray<ER>({ ER::Accept }) && F.O.IsActive(OvRaceResults));
	}
	{
		// 0x1BC8A0
		FOv F;
		F.O.bNextEventBack = true;
		F.O.Activate(OvSelectNextEvent);
		TestTrue(TEXT("next event: Up wraps to Quit, navigate"), Press(F, PadUp) == TArray<ER>({ ER::Navigate }) && F.O.NextEventSel == 4);
		TestTrue(TEXT("next event: Select does nothing"), Press(F, PadSelect).Num() == 0 && F.O.IsActive(OvSelectNextEvent));
		TestTrue(TEXT("next event: Triangle = back sound -> results"), Press(F, PadTriangle) == TArray<ER>({ ER::Back }) && F.O.IsActive(OvRaceResults));
		F.O.Deactivate(OvRaceResults);
		F.H.Mode = 4;
		F.O.Activate(OvSelectNextEvent);
		F.O.NextEventSel = 2;
		TestTrue(TEXT("next event WC: X on Time Challenge = silent, stays"), Press(F, PadCross).Num() == 0 && F.O.IsActive(OvSelectNextEvent));
		F.O.NextEventSel = 0;
		TestTrue(TEXT("next event: X on Race = accept -> next venue"), Press(F, PadCross) == TArray<ER>({ ER::Accept }) && F.O.IsActive(OvSelectNextVenue));
	}
	{
		// 0x1BBB30
		FOv F;
		F.O.NextVenueMode = 2;
		F.O.Activate(OvSelectNextVenue);
		const int32 N = F.O.NextVenueList().Num();
		TestTrue(TEXT("next venue: Up wraps to Quit, navigate"), Press(F, PadUp) == TArray<ER>({ ER::Navigate }) && F.O.NextVenueSel == N - 1);
		TestTrue(TEXT("next venue: Up skips the blank row"), Press(F, PadUp) == TArray<ER>({ ER::Navigate }) && F.O.NextVenueSel == N - 3);
		F.O.NextVenueSel = N - 1;
		TestTrue(TEXT("next venue: X on Quit = no sound, prompt"), Press(F, PadCross).Num() == 0 && F.O.Dialog.bActive);
		F.O.Dialog.bActive = false;
		TestTrue(TEXT("next venue: Select does nothing"), Press(F, PadSelect).Num() == 0 && F.O.IsActive(OvSelectNextVenue));
		TestTrue(TEXT("next venue: Triangle = no sound -> next event"), Press(F, PadTriangle).Num() == 0 && F.O.IsActive(OvSelectNextEvent));
	}
	{
		// 0x1BA330
		FOv F;
		F.H.Mode = 4;
		FSSXPointSession& S = F.P.Session.Points[0];
		S.Char = 0; S.Left = 3; S.Initial = 5; S.Cur[0] = 4; S.Max[0] = 10;
		F.O.Activate(OvPointDistribution);
		TestTrue(TEXT("points: Up wraps, point-change sound"), Press(F, PadUp) == TArray<ER>({ ER::PointChange }) && F.O.PointSel == 3);
		F.O.PointSel = 0;
		TestTrue(TEXT("points: Right adds, point-change sound"), Press(F, PadRight) == TArray<ER>({ ER::PointChange }) && S.Left == 2);
		S.Left = 0;
		TestTrue(TEXT("points: Right with none left = denied"), Press(F, PadRight) == TArray<ER>({ ER::Denied }));
		TestTrue(TEXT("points: X at 0 left = no sound, save prompt"), Press(F, PadCross).Num() == 0 && F.O.Dialog.bActive);
	}
	{
		// 0x1B7D98
		FOv F;
		F.P.Session.PendingRecords.Add({ 0, 0, F.H.Track, 2 });
		F.O.Activate(OvHiScoreRecord);
		TestTrue(TEXT("name entry: Up + Left = two navigate sounds"), Press(F, PadUp | PadLeft) == TArray<ER>({ ER::Navigate, ER::Navigate }));
		F.O.HiGridX = 0; F.O.HiGridY = 0;
		TestTrue(TEXT("name entry: X = accept, the first letter replaces the name"), Press(F, PadCross) == TArray<ER>({ ER::Accept }) && F.O.HiName.Len() == 1);
		Press(F, PadCross);
		TestEqual(TEXT("second letter appends"), F.O.HiName.Len(), 2);
		F.O.HiGridX = 12; F.O.HiGridY = 2;
		TestTrue(TEXT("name entry: delete = accept + back"), Press(F, PadCross) == TArray<ER>({ ER::Accept, ER::Back }) && F.O.HiName.Len() == 1);
		TestTrue(TEXT("name entry: Triangle does nothing"), Press(F, PadTriangle).Num() == 0 && F.O.IsActive(OvHiScoreRecord));
	}
	{
		// 0x1EFEF0
		FOv F;
		F.O.Activate(OvReplay);
		TestTrue(TEXT("replay: state 1 ignores X (the control's)"), Press(F, PadCross).Num() == 0 && !F.O.IsReplayMenuOpen());
		TestTrue(TEXT("replay: Start = accept, menu"), Press(F, PadStart) == TArray<ER>({ ER::Accept }) && F.O.IsReplayMenuOpen());
		TestTrue(TEXT("replay: Up to save"), Press(F, PadUp) == TArray<ER>({ ER::Navigate }) && F.O.ReplaySel == 0);
		TestTrue(TEXT("replay: Up at the top = silent"), Press(F, PadUp).Num() == 0 && F.O.ReplaySel == 0);
		Press(F, PadDown);
		TestTrue(TEXT("replay: Down to continue, navigate"), Press(F, PadDown) == TArray<ER>({ ER::Navigate }) && F.O.ReplaySel == 2);
		TestTrue(TEXT("replay: Down at the bottom = silent"), Press(F, PadDown).Num() == 0);
		TestTrue(TEXT("replay: X on continue = back sound, menu closes, replay plays on"), Press(F, PadCross) == TArray<ER>({ ER::Back }) && !F.O.IsReplayMenuOpen() && F.O.IsActive(OvReplay) && F.O.ReplayState == 1);
	}
	return true;
}

// 0x1CF6E8 / 0x1B9968 / 0x1B8D08 / 0x1B8718
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayReplayCardTest, "SSX.UI.Overlays.ReplayCard", OvTestFlags)
bool FSSXOverlayReplayCardTest::RunTest(const FString& Parameters)
{
	using ER = SSXUISound::ERace;
	using FView = FSSXMemCard::FRaceView;
	const FString PrevRoot = FSSXMemCard::RootOverride();
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardOvCard"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
		FSSXMemCard::RootOverride() = PrevRoot;
	};
	const bool bDraw = FOvAssets::Get().Ready();
	if (!bDraw) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; the draw checks are skipped")); }
	FOv F;
	FSSXMemCard& Card = F.O.ReplayCard;
	F.H.ReplayFileBytes.SetNumZeroed(SSXReplayFile::FileBytes);
	auto Step = [&F](uint32 Bits) { F.O.Sounds.Reset(); if (Bits) { F.O.Pad(0, Bits); } F.O.Tick(); return F.O.Sounds; };
	auto SprAt = [&F](int32 Id, float X, float Y)
	{
		return F.D.Cmds.ContainsByPredicate([&](const FSSXFeDrawCmd& C)
			{ return C.SpriteId == Id && FMath::Abs(C.Pos.X - X) <= 1.f && FMath::Abs(C.Pos.Y - Y) <= 1.f; });
	};
	auto CentredAt = [&F](float Y)
	{
		return F.D.TextRecs.ContainsByPredicate([&](const FSSXFeTextRec& R)
			{ return FMath::Abs(R.Pos.Y - Y) <= 1.f && FMath::Abs(R.Pos.X + F.Ink(EFont::Menu, R.Str, 1.f) * 0.5f - 320.f) <= 1.f; });
	};
	auto OpenSave = [&F]()
	{
		F.O.Pad(0, PadStart);
		F.O.ReplaySel = 0;
		F.O.Pad(0, PadCross);
	};
	F.O.Activate(OvReplay);
	OpenSave();
	TestTrue(TEXT("save: the card manager in race (state 0)"), F.O.ReplayState == 0 && Card.bInRace && Card.IsActive());
	TestEqual(TEXT("checking: status (vslot 29 -> 16)"), int32(Card.RaceView().Kind), int32(FView::Status));
	const float Fh = MenuLh;
	if (bDraw)
	{
		F.Redraw();
		TestTrue(TEXT("status text centred on 320 at (int)(240 - fh / 2)"), CentredAt(FMath::TruncToFloat(240.f - Fh * 0.5f)));
	}
	for (int32 N = 0; N < 400 && Card.State != FSSXMemCard::SaveList; ++N) { Step(0); }
	TestEqual(TEXT("the save list (vslot 17)"), int32(Card.RaceView().Kind), int32(FView::List));
	if (bDraw)
	{
		F.Redraw();
		const FSSXFeTextRec* Tab = F.Txt(F.L(450));
		TestTrue(TEXT("tab LOC 450 (cSaveOverlay +0x434) at y 25 + 7"), Tab && FMath::IsNearlyEqual(Tab->Pos.Y, 32.f, 1.f));
		NearTxt(*this, TEXT("row 1 number"), F.Txt(TEXT("1")), 204.8f, 168.f);
		NearTxt(*this, TEXT("row 4 name (empty: LOC 351)"), F.Txt(F.L(351), 3), 236.8f, 168.f + 3.f * 36.f);
		NearTxt(*this, TEXT("help 443 at (448, 288 + 2 (fh + 10))"), F.Txt(F.L(443)), 448.f, 288.f + 2.f * (Fh + 10.f));
		NearTxt(*this, TEXT("help 343 at 288 + 4 (fh + 10)"), F.Txt(F.L(343)), 448.f, 288.f + 4.f * (Fh + 10.f));
		TestTrue(TEXT("help icon 0x74 (X) at (420, y - 2)"), SprAt(0x74, 420.f, 288.f + 2.f * (Fh + 10.f) - 2.f));
		NearSpr(*this, TEXT("help icon 0x72 (Circle)"), F.Spr(0x72), 420.f, 288.f + 4.f * (Fh + 10.f) - 2.f);
		if (const FSSXFeTextRec* Del = F.Txt(F.L(343)))
		{
			TestTrue(TEXT("343 disabled on an empty slot (.5 alpha)"), FMath::IsNearlyEqual(Del->Color.A, 0.5f, 0.02f));
		}
		const bool bBar = F.Rects().ContainsByPredicate([](const FSSXFeDrawCmd* C)
			{ return FMath::IsNearlyEqual(C->Pos.X, 192.f, 0.05f) && FMath::IsNearlyEqual(C->Pos.Y, 168.f - 10.56f, 0.05f)
				&& FMath::IsNearlyEqual(C->Size.X, 371.2f, 0.05f) && FMath::IsNearlyEqual(C->Size.Y, 36.f, 0.05f) && C->Layer == 0xA; });
		TestTrue(TEXT("selection bar (192, y - 10.56, 371.2, 36) on layer 0xA"), bBar);
	}
	TestTrue(TEXT("Down: navigate (vfunc_51)"), Step(PadDown) == TArray<ER>({ ER::Navigate }) && Card.Selected == 1);
	TestTrue(TEXT("Up: navigate (vfunc_52)"), Step(PadUp) == TArray<ER>({ ER::Navigate }) && Card.Selected == 0);
	TestTrue(TEXT("Up at the top: no sound, no move (+0x3C = 0: no wrap)"), Step(PadUp).Num() == 0 && Card.Selected == 0);
	TestTrue(TEXT("X on an empty slot: accept, saving (vslot 18 + warning)"), Step(PadCross) == TArray<ER>({ ER::Accept })
		&& Card.RaceView().Kind == FView::Saving && Card.RaceView().bWarning);
	if (bDraw)
	{
		F.Redraw();
		TestNotNull(TEXT("progress bar sprite 0x2A"), F.Spr(0x2A));
		TestTrue(TEXT("warning LOC 350 centred in (128, 312, 384)"), CentredAt(312.f));
	}
	for (int32 N = 0; N < 400 && Card.State != FSSXMemCard::SaveDone; ++N) { Step(0); }
	const FView Done = Card.RaceView();
	TestTrue(TEXT("save complete: message box LOC 447 as it is, one button 341 (vslot 19)"), Done.Kind == FView::Dialog && Done.TextLoc == 447
		&& Done.Button1 == 341 && Done.Button2 < 0 && Done.bXFirst);
	if (bDraw)
	{
		F.Redraw();
		TestTrue(TEXT("box text centred in (0, 204, 640)"), CentredAt(204.f));
		const float X = 320.f - (F.Ink(EFont::Menu, F.L(341), 1.f) + 24.f) * 0.5f;
		TestTrue(TEXT("one button: icon 0x74 at 320 - (w + 24) / 2, y 295"), SprAt(0x74, X, 295.f));
		NearTxt(*this, TEXT("label at icon + 24, y 296"), F.Txt(F.L(341)), X + 24.f, 296.f);
	}
	TestTrue(TEXT("Start (raw 0x80, mask 0x880): accept, idle -> the menu"), Step(PadStart) == TArray<ER>({ ER::Accept }) && F.O.ReplayState == 2);

	F.O.ReplaySel = 0;
	F.O.Pad(0, PadCross);
	for (int32 N = 0; N < 400 && Card.State != FSSXMemCard::SaveList; ++N) { Step(0); }
	TestTrue(TEXT("X on a saved slot: accept, overwrite box 470 / 392 (vslot 31)"), Step(PadCross) == TArray<ER>({ ER::Accept })
		&& Card.State == FSSXMemCard::Overwrite && Card.RaceView().Button1 == 470 && Card.RaceView().Button2 == 392);
	if (bDraw)
	{
		F.Redraw();
		const float C = ((320.f - ((24.f + 403.2f + F.Ink(EFont::Menu, F.L(392), 1.f)) - 147.2f) * 0.5f) - 147.2f) + 320.f;
		TestTrue(TEXT("two buttons: X icon at c - 172.8"), SprAt(0x74, C - 172.8f, 295.f));
		TestTrue(TEXT("two buttons: Triangle icon at c + 83.2"), SprAt(0x73, C + 83.2f, 295.f));
	}
	TestTrue(TEXT("overwrite Triangle: accept sound, back to the list (0x1D1C30)"), Step(PadTriangle) == TArray<ER>({ ER::Accept }) && Card.State == FSSXMemCard::SaveList);
	TestTrue(TEXT("Circle on the file: accept, delete box (vslot 38)"), Step(PadCircle) == TArray<ER>({ ER::Accept }) && Card.State == FSSXMemCard::DeleteAsk
		&& Card.RaceView().Button1 == 470);
	TestTrue(TEXT("delete Triangle: back sound, the list"), Step(PadTriangle) == TArray<ER>({ ER::Back }) && Card.State == FSSXMemCard::SaveList);
	TestTrue(TEXT("list Triangle: back sound, idle -> the menu"), Step(PadTriangle) == TArray<ER>({ ER::Back }) && F.O.ReplayState == 2);
	return true;
}


// 0x1A27D0 / 0x1A2A00 / 0x1EFEF0 / 0x1F01E8
namespace
{
	bool HudHidden(const FSSXOverlayManager& O, int32 Phase)
	{
		FSSXHudInput In;
		In.Phase = Phase;
		In.bModal = O.AnyModal();
		In.bReplayActive = O.IsActive(OvReplay);
		In.bCardSave = O.IsHudCardSave();
		return !O.IsActive(OvHUD) || FSSXHudOverlay::IsHidden(In);
	}
	void RecordReplay(FSSXReplay& R)
	{
		R.Init(2, 1, false, nullptr);
		R.AttachHuman(0);
		R.OnStartRace();
		for (int32 I = 0; I < 200; ++I)
		{
			R.Update();
			uint32 W = uint32(I / 20);
			R.RecordOrPlayInput(0, W);
		}
		R.EndRecording();
		R.Update();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayOverlayVisibilityTest, "SSX.UI.ReplayOverlay.Visibility", OvTestFlags)
bool FSSXReplayOverlayVisibilityTest::RunTest(const FString& Parameters)
{
	const FString PrevRoot = FSSXMemCard::RootOverride();
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardReplayOv"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
		FSSXMemCard::RootOverride() = PrevRoot;
	};
	{
		FOv F;
		FSSXReplay R;
		RecordReplay(R);
		F.H.ReplayObj = &R;
		F.H.bCanReplay = true;
		F.H.Mode = 2;
		F.SixRacers();
		F.H.Phase = 4;
		F.Ticks(2);
		TestTrue(TEXT("live race: HUD slot 0 active and drawn"), F.O.IsActive(OvHUD) && !HudHidden(F.O, 4));
		F.O.Pad(0, PadStart);
		TestTrue(TEXT("pause menu: the HUD stays drawn (dimmed by the modal flag, not hidden)"), F.O.IsActive(OvPauseMenu) && !HudHidden(F.O, 4));
		F.O.Pad(0, PadTriangle);
		F.H.Phase = 5;
		F.Ticks(1);
		TestFalse(TEXT("end-race enter 0x113E10 closes slot 0"), F.O.IsActive(OvHUD));
		TestFalse(TEXT("... and slot 7 (PopFinish)"), F.O.IsActive(OvPopFinish));
		if (F.O.IsActive(OvPauseMenu)) { F.O.Pad(0, PadStart); }
		if (!F.O.IsActive(OvRaceResults)) { F.O.Activate(OvRaceResults); }
		F.O.ResultsSel = 2;
		F.O.Pad(0, PadCross);
		TestTrue(TEXT("results replay: slot 9 + slot 5 on, slot 1 off"), F.O.IsActive(OvReplay) && F.O.IsActive(OvReplayControl) && !F.O.IsActive(OvRaceResults));
		F.H.Phase = 4;
		F.Ticks(3);
		TestFalse(TEXT("playback in phase 4: slot 0 not reopened"), F.O.IsActive(OvHUD));
		TestTrue(TEXT("playback: HUD not drawn"), HudHidden(F.O, 4));
		TestFalse(TEXT("playback: no countdown overlay"), F.O.IsActive(OvCountdown));
		F.O.Pad(0, PadStart);
		TestFalse(TEXT("playback: Start opens no pause menu"), F.O.IsActive(OvPauseMenu));
		TestTrue(TEXT("Start in playback = the replay menu"), F.O.IsReplayMenuOpen());
		TestTrue(TEXT("replay menu: HUD still not drawn"), HudHidden(F.O, 4));
		if (FOvAssets::Get().Ready())
		{
			F.Redraw();
			NearSpr(*this, TEXT("menu up: the control's tab still drawn (busy) at y 18"), F.Spr(HudTabL, 0), 0.f, 18.f);
		}
		F.O.Pad(0, PadCross);
		TestTrue(TEXT("exit: slots 9 / 5 off, results back"), !F.O.IsActive(OvReplay) && !F.O.IsActive(OvReplayControl) && F.O.IsActive(OvRaceResults));
		F.H.Phase = 5;   // 0x191BE0
		F.Ticks(1);
		TestFalse(TEXT("after the replay: slot 0 still closed"), F.O.IsActive(OvHUD));
		F.O.Deactivate(OvRaceResults);
		F.H.Phase = 1;
		F.Ticks(1);
		TestFalse(TEXT("restart PreRace: slot 0 still closed"), F.O.IsActive(OvHUD));
		F.H.Phase = 3;   // 0x11A7D0
		F.Ticks(1);
		TestTrue(TEXT("restart countdown: slot 0 open again"), F.O.IsActive(OvHUD));
	}
	{
		FOv F;
		F.H.Mode = 1;
		F.H.HudOpens = 1;
		F.H.Phase = 3;
		F.Ticks(1);
		F.H.Phase = 4;
		F.Ticks(2);
		TestTrue(TEXT("freeride race: slot 0 open"), F.O.IsActive(OvHUD));
		F.H.Phase = 5;
		F.Ticks(1);
		F.O.Activate(OvRaceResults);
		TestFalse(TEXT("end-race enter: slot 0 closed"), F.O.IsActive(OvHUD));
		F.H.Phase = 3;
		F.Ticks(1);
		F.H.Phase = 4;
		F.Ticks(3);
		TestTrue(TEXT("attract replay behind the results: slot 0 stays closed, HUD not drawn"), !F.O.IsActive(OvHUD) && HudHidden(F.O, 4)
			&& F.O.IsActive(OvRaceResults));
		F.H.Phase = 5;
		F.Ticks(1);
		F.O.Deactivate(OvRaceResults);
		F.H.Phase = 1;
		F.Ticks(1);
		F.H.Phase = 3;
		F.H.HudOpens = 2;
		F.Ticks(1);
		TestTrue(TEXT("restart countdown enter: slot 0 open"), F.O.IsActive(OvHUD));
	}
	{
		FOv F;
		FSSXReplay R;
		RecordReplay(R);
		R.SetLoaded(true);
		F.H.ReplayObj = &R;
		F.H.bCanReplay = true;
		F.H.bLoadedReplayStart = true;
		F.H.Phase = 4;
		F.Ticks(1);
		TestTrue(TEXT("loaded: slot 9 on, slot 0 off, HUD not drawn"), F.O.IsActive(OvReplay) && !F.O.IsActive(OvHUD) && HudHidden(F.O, 4));
	}
	{
		FOv F;
		F.H.Mode = 2;
		F.H.Phase = 4;
		F.O.Activate(OvRaceResults);
		F.P.bSettingsDirty = true;
		F.O.ResultsSel = 4;
		F.O.Pad(0, PadCross);
		F.O.Pad(0, PadCross);
		TestTrue(TEXT("results quit save: HUD hidden while the card is written"), F.O.IsHudCardSave() && HudHidden(F.O, 4));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayOverlayMenuTest, "SSX.UI.ReplayOverlay.Menu", OvTestFlags)
bool FSSXReplayOverlayMenuTest::RunTest(const FString& Parameters)
{
	using ER = SSXUISound::ERace;
	auto Press = [](FOv& F, uint32 Bits) { F.O.Sounds.Reset(); F.O.Pad(0, Bits); return F.O.Sounds; };
	{
		FOv F;
		FSSXReplay R;
		RecordReplay(R);
		F.H.ReplayObj = &R;
		F.H.bCanReplay = true;
		F.H.Mode = 2;
		F.SixRacers();
		F.O.Activate(OvRaceResults);
		F.O.ResultsSel = 2;
		F.O.Pad(0, PadCross);
		TestTrue(TEXT("Start (state 1 only) = accept, state 2, selection 1 (exit)"), Press(F, PadStart) == TArray<ER>({ ER::Accept })
			&& F.O.ReplayState == 2 && F.O.ReplaySel == 1);
		F.Ticks(1);
		TestTrue(TEXT("menu: the control busy (+0x49C)"), F.O.GetReplayControl().bBusy && !F.O.GetReplayControl().bHidden);
		if (FOvAssets::Get().Ready())
		{
			F.Redraw();
			const FSSXFeTextRec* A = F.Txt(F.L(391));
			const FSSXFeTextRec* B = F.Txt(F.L(353));
			const FSSXFeTextRec* C = F.Txt(F.L(475));
			if (TestNotNull(TEXT("save"), A) && TestNotNull(TEXT("exit"), B) && TestNotNull(TEXT("continue"), C))
			{
				TestTrue(TEXT("order 391 save, 353 exit, 475 continue"), A->Pos.Y < B->Pos.Y && B->Pos.Y < C->Pos.Y);
			}
			TestNotNull(TEXT("title tab LOC 3667"), F.Txt(F.L(3667)));
		}
		TestTrue(TEXT("Down: continue, navigate"), Press(F, PadDown) == TArray<ER>({ ER::Navigate }) && F.O.ReplaySel == 2);
		TestTrue(TEXT("Down at the end: no wrap, silent"), Press(F, PadDown).Num() == 0 && F.O.ReplaySel == 2);
		TestTrue(TEXT("Up + Down in one frame: Up wins"), Press(F, PadUp | PadDown) == TArray<ER>({ ER::Navigate }) && F.O.ReplaySel == 1);
		TestTrue(TEXT("Up: save"), Press(F, PadUp) == TArray<ER>({ ER::Navigate }) && F.O.ReplaySel == 0);
		TestTrue(TEXT("Up at the top: no wrap, silent"), Press(F, PadUp).Num() == 0 && F.O.ReplaySel == 0);
		TestTrue(TEXT("Triangle = back sound, continue (state 1)"), Press(F, PadTriangle) == TArray<ER>({ ER::Back }) && F.O.ReplayState == 1);
		TestTrue(TEXT("the menu opens on exit again"), Press(F, PadStart) == TArray<ER>({ ER::Accept }) && F.O.ReplaySel == 1);
		TestTrue(TEXT("Start in the menu = back sound, continue"), Press(F, PadStart) == TArray<ER>({ ER::Back }) && F.O.ReplayState == 1);
		Press(F, PadStart);
		TestTrue(TEXT("X on exit (results replay) = accept, slots 9 / 5 off, return slot 1"), Press(F, PadCross) == TArray<ER>({ ER::Accept })
			&& !F.O.IsActive(OvReplay) && !F.O.IsActive(OvReplayControl) && F.O.IsActive(OvRaceResults) && F.H.ReplayStops == 1 && F.H.ExitCode < 0);
	}
	{
		FOv F;
		FSSXReplay R;
		RecordReplay(R);
		R.SetLoaded(true);
		F.H.ReplayObj = &R;
		F.H.bCanReplay = true;
		F.H.bLoadedReplayStart = true;
		F.Ticks(1);
		Press(F, PadStart);
		TestTrue(TEXT("loaded: X on exit = accept, the confirm box (state 4)"), Press(F, PadCross) == TArray<ER>({ ER::Accept }) && F.O.ReplayState == 4);
		TestTrue(TEXT("box 0x1AA090: LOC 0x161 / 0x146, X 0x1D6, Triangle 0x188, no Circle"), F.O.Dialog.bActive && F.O.Dialog.Line1 == 0x161
			&& F.O.Dialog.Line2 == 0x146 && F.O.Dialog.LocX == 0x1D6 && F.O.Dialog.LocTriangle == 0x188 && F.O.Dialog.LocCircle < 0);
		TestTrue(TEXT("box: Circle does nothing"), Press(F, PadCircle).Num() == 0 && F.O.ReplayState == 4);
		TestTrue(TEXT("box: Triangle = back sound, the menu (state 2)"), Press(F, PadTriangle) == TArray<ER>({ ER::Back }) && F.O.ReplayState == 2);
		Press(F, PadCross);
		TestTrue(TEXT("box: X = accept, the race is left (exit code 2, no return slot)"), Press(F, PadCross) == TArray<ER>({ ER::Accept })
			&& !F.O.IsActive(OvReplay) && F.H.ExitCode == 2);
	}
	return true;
}

// 0x112B88 / 0x1A7758 / 0x1A7E70
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayRestartAfterFinishTest, "SSX.UI.Overlays.RestartAfterFinish", OvTestFlags)
bool FSSXOverlayRestartAfterFinishTest::RunTest(const FString& Parameters)
{
	const auto Finish1st = [](FOv& F)
	{
		F.H.Finishes = { 0, 0 };
		F.Ticks(12);
	};
	const auto CheckRaceState = [this, &Finish1st](FOv& F, const TCHAR* Path)
	{
		F.H.Phase = 1;   // 0x180A10
		F.Ticks(1);
		for (int32 I = 0; I < OvCount; ++I)
		{
			if (I != OvPopAsyncLoad && I != OvBlackFade && I != OvHelpRace)
			{
				TestFalse(FString::Printf(TEXT("%s: PreRace enter closes slot %d"), Path, I), F.O.IsActive(I));
			}
		}
		F.H.Phase = 3;
		F.Ticks(1);
		F.H.Phase = 4;
		F.Ticks(20);
		TestTrue(FString::Printf(TEXT("%s: HUD back in the restarted race"), Path), F.O.IsActive(OvHUD));
		TestFalse(FString::Printf(TEXT("%s: no PopFinish in the restarted race"), Path), F.O.IsActive(OvPopFinish));
		TestFalse(FString::Printf(TEXT("%s: no results in the restarted race"), Path), F.O.IsActive(OvRaceResults));
		Finish1st(F);
		TestTrue(FString::Printf(TEXT("%s: the restarted race's own finish pops up"), Path), F.O.IsActive(OvPopFinish));
	};
	{
		FOv F;
		F.H.Mode = 2;
		F.SixRacers();
		F.H.Phase = 3;
		F.Ticks(1);
		F.H.Phase = 4;
		F.Ticks(2);
		Finish1st(F);
		TestTrue(TEXT("after the line: PopFinish up"), F.O.IsActive(OvPopFinish) && F.O.IsActive(OvHUD));
		F.O.Pad(0, PadStart); F.O.Pad(0, PadDown); F.O.Pad(0, PadCross); F.O.Pad(0, PadCross);
		TestEqual(TEXT("pause Restart requested"), F.H.Restarts, 1);
		TestFalse(TEXT("pause closed"), F.O.IsActive(OvPauseMenu));
		CheckRaceState(F, TEXT("pause restart after the line"));
	}
	{
		FOv F;
		F.H.Mode = 2;
		F.SixRacers();
		F.H.Phase = 3;
		F.Ticks(1);
		F.H.Phase = 4;
		F.Ticks(2);
		Finish1st(F);
		F.H.Phase = 5;
		F.Ticks(1);
		F.H.bPostRaceReady = true;
		F.Ticks(1);
		F.H.bPostRaceReady = false;
		TestTrue(TEXT("results up"), F.O.IsActive(OvRaceResults));
		F.H.Phase = 4;
		F.Ticks(5);
		F.O.ResultsSel = 1;
		F.O.Pad(0, PadCross);
		TestEqual(TEXT("results Restart requested"), F.H.Restarts, 1);
		CheckRaceState(F, TEXT("results restart"));
	}
	{
		FOv F;
		F.H.Mode = 2;
		F.SixRacers();
		F.H.Phase = 3;
		F.Ticks(1);
		F.H.Phase = 4;
		F.Ticks(2);
		Finish1st(F);
		F.H.Phase = 5;
		F.Ticks(3);
		CheckRaceState(F, TEXT("restart in the end race"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayShowoffMedalTextTest, "SSX.UI.Overlays.ShowoffMedalText", OvTestFlags)
bool FSSXOverlayShowoffMedalTextTest::RunTest(const FString& Parameters)
{
	// 0x1B0BB8 / 0x1B1BA8 / 0x163E28 / 0x16BAA8
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; overlay checks skipped")); return true; }
	constexpr int32 Elysium = 2;
	const auto Run = [this](int32 Mode, int32 Score, int32 Stored, int32 Expect)
	{
		FOv F;
		F.H.Mode = Mode;
		F.H.Track = Elysium;
		F.H.Players = 1;
		F.H.Results = { FOv::Racer(TEXT("JOE"), 0, 1, -1.0) };
		FSSXShowoffStats S;
		S.Score = Score;
		F.H.Stats = { S };
		F.P.Slot[0].Char = 0;
		F.P.Career[0].Tracks[Elysium].ShowoffMedal = uint8(Stored);
		F.O.Activate(OvRaceResults);
		F.Redraw();
		FString All;
		for (const FSSXFeTextRec& R : F.D.TextRecs) { All += R.Str; }
		All.ReplaceInline(TEXT(" "), TEXT(""));
		All.ReplaceInline(TEXT("\n"), TEXT(""));
		for (int32 Id = 582; Id <= 585; ++Id)
		{
			FString Want = F.L(Id);
			Want.ReplaceInline(TEXT(" "), TEXT(""));
			Want.ReplaceInline(TEXT("\n"), TEXT(""));
			TestEqual(FString::Printf(TEXT("mode %d score %d stored %d: LOC %d %s"), Mode, Score, Stored, Id, Id == Expect ? TEXT("shown") : TEXT("absent")),
				All.Contains(Want), Id == Expect);
		}
	};
	Run(5, 831790, SSXProg::Gold, 583);
	Run(5, 0, SSXProg::NoMedal, 585);
	Run(5, 160000, SSXProg::Gold, 584);
	Run(5, 160000, SSXProg::Silver, 582);
	Run(3, 831790, SSXProg::Gold, -1);
	Run(3, 0, SSXProg::NoMedal, -1);          // 0x1B1BA8
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXOverlayScreenClipTest, "SSX.UI.Overlays.ScreenClip", OvTestFlags)
bool FSSXOverlayScreenClipTest::RunTest(const FString& Parameters)
{
	// 0x1A9BB8
	{
		FVector2f P0(-20.f, 100.f), P1(660.f, 200.f);
		FVector2f Uv[4] = { FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), FVector2f(0, 1) };
		TestTrue(TEXT("backdrop quad kept"), FSSXFeDrawList::ClipToScreen(P0, P1, Uv));
		TestTrue(TEXT("x clipped to 0..640"), P0.X == 0.f && P1.X == 640.f);
		TestTrue(TEXT("y untouched"), P0.Y == 100.f && P1.Y == 200.f);
		TestTrue(TEXT("left uv re-sampled (20 / 680)"), FMath::IsNearlyEqual(Uv[0].X, 20.f / 680.f, 1e-5f) && FMath::IsNearlyEqual(Uv[3].X, 20.f / 680.f, 1e-5f));
		TestTrue(TEXT("right uv re-sampled (660 / 680)"), FMath::IsNearlyEqual(Uv[1].X, 660.f / 680.f, 1e-5f) && FMath::IsNearlyEqual(Uv[2].X, 660.f / 680.f, 1e-5f));
		TestTrue(TEXT("v kept"), Uv[0].Y == 0.f && Uv[2].Y == 1.f);
	}
	{
		FVector2f P0(-10.f, 0.f), P1(10.f, 20.f);
		FVector2f Uv[4];
		FSSXFeDrawList::CornerUVs(FVector2f(0, 0), FVector2f(1, 1), true, Uv);
		TestTrue(TEXT("turned quad kept"), FSSXFeDrawList::ClipToScreen(P0, P1, Uv));
		TestTrue(TEXT("turned: TL uv = (1, 0.5)"), Uv[0].Equals(FVector2f(1.f, 0.5f), 1e-5f));
		TestTrue(TEXT("turned: BL uv = (0, 0.5)"), Uv[3].Equals(FVector2f(0.f, 0.5f), 1e-5f));
		TestTrue(TEXT("turned: TR uv unchanged"), Uv[1].Equals(FVector2f(1.f, 1.f), 1e-5f));
	}
	{
		FVector2f P0(-50.f, 10.f), P1(-20.f, 30.f);
		FVector2f Uv[4] = {};
		TestFalse(TEXT("a quad wholly off the picture is dropped"), FSSXFeDrawList::ClipToScreen(P0, P1, Uv));
		FVector2f Q0(10.f, 10.f), Q1(30.f, 30.f);
		FVector2f Uq[4] = { FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), FVector2f(0, 1) };
		TestTrue(TEXT("an inside quad is untouched"), FSSXFeDrawList::ClipToScreen(Q0, Q1, Uq) && Q0 == FVector2f(10.f, 10.f) && Uq[2] == FVector2f(1, 1));
	}
	if (!FOvAssets::Get().Ready()) { SSXTest::Skip(*this, TEXT("fonts / HUD sheet / LOC not converted; results backdrop check skipped")); return true; }
	FOv F;
	F.H.Mode = 3;
	F.H.Track = 2;
	F.H.Players = 1;
	F.H.Results = { FOv::Racer(TEXT("JOE"), 0, 1, -1.0) };
	F.H.Stats = { FSSXShowoffStats() };
	F.P.Slot[0].Char = 0;
	F.O.Activate(OvRaceResults);
	F.Redraw();
	bool bWider = false, bInside = true;
	for (const FSSXFeDrawCmd& C : F.D.Cmds)
	{
		bWider |= C.Pos.X < 0.f || C.Pos.X + C.Size.X > 640.f;
		FVector2f P0 = C.Pos, P1 = C.Pos + C.Size;
		FVector2f Uv[4];
		FSSXFeDrawList::CornerUVs(C.UV0, C.UV1, C.bRot90, Uv);
		if (FSSXFeDrawList::ClipToScreen(P0, P1, Uv))
		{
			bInside &= FMath::Min(P0.X, P1.X) >= 0.f && FMath::Max(P0.X, P1.X) <= 640.f && FMath::Min(P0.Y, P1.Y) >= 0.f && FMath::Max(P0.Y, P1.Y) <= 480.f;
		}
	}
	TestTrue(TEXT("the backdrop is placed past the picture (x -20..660, 0x1A9BB8)"), bWider);
	TestTrue(TEXT("painted quads lie inside 0..640 x 0..480"), bInside);
	return true;
}

#endif
