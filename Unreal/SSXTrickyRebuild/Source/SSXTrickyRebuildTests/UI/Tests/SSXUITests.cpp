#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXMemCard.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"
#include "Sim/Rider/RiderProfiles.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags UITestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	const FSSXLocStrings& TestLoc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	bool TestTables()
	{
		static bool bLoaded = SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
		return bLoaded;
	}

	const FSSXFeCameraSet& TestCameras()
	{
		static FSSXFeCameraSet Set;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; Set.Load(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json"))); }
		return Set;
	}

	struct FFe
	{
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		explicit FFe(int32 Pads = 1)
		{
			TestTables();
			FE.PadsConnected = Pads;
			FE.Init(&Profile, &Cheats, &TestLoc(), false);
			FE.Camera.CameraSet = TestCameras().IsLoaded() ? &TestCameras() : nullptr;
		}
		int32 RunToLaunch(int32 MaxFrames = 600)
		{
			int32 N = 0;
			while (!FE.bLaunchRequested && N < MaxFrames) { FE.Tick(); ++N; }
			return N;
		}
		/** One key, then ticks until the front end takes input again (the slide-out, the camera wait, the next screen's
		 *  slide-in: cFE3DState::vfunc_10 / vfunc_18) or the game launches. */
		void Key(ESSXFeKey K, int32 Pad = 0)
		{
			Settle();
			FE.Key(Pad, K);
			FE.Tick();
			Settle();
		}
		void Settle()
		{
			for (int32 N = 0; !FE.IsReady() && FE.Boot == ESSXFeBoot::FrontEnd && FE.MovieRequest.IsEmpty() && N < 3000; ++N) { FE.Tick(); }
		}
		void GoTo(ESSXFeScreen S, int32 Player = 0) { FE.GoTo(S, Player); Settle(); }
		void X(int32 Pad = 0) { Key(ESSXFeKey::Cross, Pad); }
		FString Screen() const { return LexToString(FE.Current()); }
		TArray<int32> Locs() const
		{
			TArray<int32> R;
			for (const FSSXFeItem& I : FE.Top()->Items) R.Add(I.Loc);
			return R;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILocStringsTest, "SSX.UI.LocStrings", UITestFlags)
bool FSSXUILocStringsTest::RunTest(const FString& Parameters)
{
	FSSXLocStrings Empty;
	TestTrue(TEXT("no fallback text"), Empty.ByIndex(107).IsEmpty());
	const FString Dir = SSXFrontEnd::LangDir();
	if (!FPaths::FileExists(FPaths::Combine(Dir, TEXT("AMERICAN.LOC"))))
	{
		AddWarning(FString::Printf(TEXT("no disc LOC files in %s"), *Dir));
		return true;
	}
	const FSSXLocStrings& L = TestLoc();
	TestEqual(TEXT("3774 strings"), L.Num(), 3774);
	TestEqual(TEXT("107 kFE_TitleScreen"), L.ByIndex(107), FString(TEXT("Press START button")));
	TestEqual(TEXT("3696 Start Game"), L.ByIndex(3696), FString(TEXT("Start Game")));
	TestEqual(TEXT("3315 DVD Content"), L.ByIndex(3315), FString(TEXT("DVD Content")));
	TestEqual(TEXT("3595 Single Event"), L.ByIndex(3595), FString(TEXT("Single Event")));
	TestEqual(TEXT("3499 key"), L.KeyOf(3499), FString(TEXT("kFETitleSelectMode")));
	TestEqual(TEXT("key -> index"), L.IndexOf(TEXT("kPauseMainContinue")), 475);
	TestEqual(TEXT("3533 DONE"), L.ByIndex(3533), FString(TEXT("DONE")));
	TestEqual(TEXT("3759 GO!"), L.ByIndex(3759), FString(TEXT("GO!")));
	TestTrue(TEXT("rider bio keys"), L.IndexOf(TEXT("kFEMacShortBio1")) > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUITablesTest, "SSX.UI.Tables", UITestFlags)
bool FSSXUITablesTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeTables;
	// 0x3470B8
	const TCHAR* Carousel[] = { TEXT("Mac"), TEXT("Kaori"), TEXT("Psymon"), TEXT("Brodi"), TEXT("Eddie"), TEXT("Elise"),
		TEXT("Marisol"), TEXT("Seeiah"), TEXT("Luther"), TEXT("JP"), TEXT("Zoe"), TEXT("Moby") };
	for (int32 S = 0; S < 12; ++S) TestEqual(FString::Printf(TEXT("carousel %d"), S), FString(RiderName(CarouselRider(S))), FString(Carousel[S]));
	const FSSXProfile P = FSSXProfile::MakeDefault();
	for (int32 C = 0; C < 12; ++C)
	{
		const bool bOpen = C == 3 || C == 4 || C == 7 || C == 0;
		TestEqual(FString::Printf(TEXT("rider %s open"), RiderName(C)), P.IsRiderUnlocked(C, false), bOpen);
	}
	TestEqual(TEXT("Brodi is unlock 4"), UnlockRank(10), 4);
	TestEqual(TEXT("Luther is unlock 11"), UnlockRank(2), 11);
	// 0x3A8AF0
	TestEqual(TEXT("venue order"), VenueOrder(), TArray<int32>({ 0, 1, 2, 3, 4, 8, 5, 11, 7, 6 }));
	TestEqual(TEXT("track 11 = Alaska map"), SSXFrontEnd::Tracks()[TrackCatalogIndex(11)].MapName, FString(TEXT("Alaska")));
	TestEqual(TEXT("track 10 = trick course"), SSXFrontEnd::Tracks()[TrackCatalogIndex(10)].Id, FString(TEXT("trick")));
	const int32 Costs[] = { 0, 0, 5, 20, 35, 60, 90, 120, 160, 200, 240, 999 };
	for (int32 B = 0; B < 12; ++B) TestEqual(TEXT("board cost"), BoardCost(B), Costs[B]);
	TestEqual(TEXT("240 = Master"), RankFromPoints(240), 0);
	TestEqual(TEXT("5 = Rookie"), RankFromPoints(5), 8);
	TestEqual(TEXT("4 = Newbie"), RankFromPoints(4), 9);
	TestEqual(TEXT("board types BX/Free/Alpine LOC"), BoardTypeLoc(0) * 10000 + BoardTypeLoc(1) * 100 + BoardTypeLoc(2), 19 * 10000 + 20 * 100 + 18);
	// 0x347F20
	const TArray<SSXFe::FHelpEntry>& H = SSXFe::HelpBarTable();
	TestEqual(TEXT("0x2 select"), H[1].Loc, 541);
	TestEqual(TEXT("0x8 previous / Triangle"), H[3].Loc * 100 + H[3].Icon, 538 * 100 + SSXFe::IconTriangle);
	TestEqual(TEXT("0x2000 options / Square"), H[13].Loc * 100 + H[13].Icon, 537 * 100 + SSXFe::IconSquare);
	// 0x3857C0
	TestEqual(TEXT("X = 'd'"), (int32)SSXFe::KeyForPadBit(SSXFe::PadCross), (int32)'d');
	TestEqual(TEXT("Start = Enter"), (int32)SSXFe::KeyForPadBit(SSXFe::PadStart), 0x0D);
	TestEqual(TEXT("Up = 0x4800"), (int32)SSXFe::KeyForPadBit(SSXFe::PadUp), 0x4800);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFeTablesTest, "SSX.UI.FeTables", UITestFlags)
bool FSSXUIFeTablesTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeTables;
	if (!TestTables())
	{
		SSXTest::Skip(*this, TEXT("fe_tables.json not exported (tools/export_fe_tables.py); exe table checks skipped"));
		return true;
	}
	// 0x3B7F58
	TestEqual(TEXT("grid 14 x 3"), NameGrid().Len(), SSXFeUserName::Cols * SSXFeUserName::Rows);
	TestEqual(TEXT("delete cell (12,2)"), (int32)SSXFeUserName::Cell(12, 2), (int32)TEXT('d'));
	TestEqual(TEXT("END cell (13,2)"), (int32)SSXFeUserName::Cell(13, 2), (int32)TEXT('e'));
	TestEqual(TEXT("digit 1 follows Z"), (int32)SSXFeUserName::Cell(12, 1), (int32)TEXT('1'));
	TestEqual(TEXT("digit 0 after 9"), (int32)SSXFeUserName::Cell(7, 2), (int32)TEXT('0'));
	// 0x332C88
	const int32 Values[] = { 0, 0, 50, 120, 200, 250, 25000 };
	for (int32 C = 0; C < NumRiders; ++C)
	{
		for (int32 O = 0; O < OutfitsPerRider; ++O)
		{
			TestFalse(FString::Printf(TEXT("outfit %d/%d named"), C, O), OutfitName(C, O).IsEmpty());
			TestEqual(FString::Printf(TEXT("outfit %d/%d value"), C, O), OutfitValue(C, O), Values[O]);
		}
	}
	TestTrue(TEXT("boards loaded"), BoardsLoaded());
	for (int32 C = 0; C < NumRiders; ++C)
	{
		for (int32 B = 0; B < BoardsPerRider; ++B)
		{
			const FBoard& R = Board(C, B);
			TestEqual(TEXT("board cost = notes"), R.Cost, BoardCost(B));
			TestTrue(TEXT("board type 0..2"), R.Type >= 0 && R.Type <= 2 && !R.Name.IsEmpty());
		}
	}
	// 0x379FD0
	for (int32 M : { 1, 2, 3, 4, 5, 7 }) TestFalse(TEXT("track 10 masked"), (VenueModeMask(M) & (1u << 10)) != 0);
	TestEqual(TEXT("Freeride: all 10 venues"), (int32)(VenueModeMask(1) & 0x9FFu), 0x9FF);
	TestTrue(TEXT("SE Race: no Untracked / Pipedream"), (VenueModeMask(2) & 0xC0u) == 0);
	TestTrue(TEXT("SE Showoff: Pipedream, no Untracked"), (VenueModeMask(3) & 0xC0u) == 0x40u);
	TestTrue(TEXT("WC Showoff: no Pipedream"), (VenueModeMask(5) & 0x40u) == 0);
	{
		FFe F;
		F.GoTo(ESSXFeScreen::VenueSel);
		const FSSXFeState* V = F.FE.Top();
		for (const FSSXFeItem& I : V->Items)
		{
			if (I.Id == 6 || I.Id == 7) TestFalse(TEXT("Pipedream / Untracked disabled in SE race"), I.bEnabled);
		}
	}
	const SSXRider::FRiderTable& Riders = SSXRider::FRiderTable::Shared();
	if (Riders.IsLoaded())
	{
		const FSSXProfile P = FSSXProfile::MakeDefault();
		for (int32 C = 0; C < NumRiders; ++C)
		{
			for (int32 A = 0; A < 4; ++A)
			{
				TestEqual(TEXT("career points = round(0.4 base)"), (int32)P.Career[C].StatPoints[A], SSXProg::Scaled(Riders.AttributeBase(C, A)));
				TestEqual(TEXT("career attr = base"), (int32)P.Career[C].Attr[A], Riders.AttributeBase(C, A));
			}
		}
	}
	{
		FFe F;
		F.GoTo(ESSXFeScreen::CustomBody);
		TestEqual(TEXT("7 outfits"), F.FE.Top()->Items.Num(), 7);
		TestEqual(TEXT("outfit names from the table"), F.FE.Top()->Items[2].Text, OutfitName(F.Profile.Slot[0].Char, 2));
	}
	return true;
}

// 0x2BA810
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIMovieEndTest, "SSX.UI.MovieEnd", UITestFlags)
bool FSSXUIMovieEndTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("skip ends at once"), SSXFrontEnd::MovieDone(true, false, 3.5, 0.0));
	TestFalse(TEXT("video still playing"), SSXFrontEnd::MovieDone(false, false, 0.0, 10.0));
	TestFalse(TEXT("video ended, audio tail still playing"), SSXFrontEnd::MovieDone(false, true, 3.5, 2.6));
	TestTrue(TEXT("video and audio ended"), SSXFrontEnd::MovieDone(false, true, 3.5, 3.5));
	TestTrue(TEXT("no audio stream: the video's end"), SSXFrontEnd::MovieDone(false, true, 0.0, 0.0));
	TestEqual(TEXT("missing WAV = no audio"), SSXFrontEnd::WavSeconds(TEXT("Z:/nope/none.wav")), 0.0);
	const FString Wav = FPaths::Combine(SSXFrontEnd::VideoDir(), TEXT("EABIG.wav"));
	if (!FPaths::FileExists(Wav))
	{
		SSXTest::Skip(*this, TEXT("converted/frontend/video/EABIG.wav missing (tools/mpc.py): data checks skipped"));
		return true;
	}
	const double Audio = SSXFrontEnd::WavSeconds(Wav);
	TestTrue(TEXT("EABIG audio = 168000 / 48000 s"), FMath::IsNearlyEqual(Audio, 3.5, 1e-6));
	// 60 Hz ticks: the video (77 frames at 29.97) ends first, the movie ends with the audio
	const double VideoEnd = 77.0 * 1001.0 / 30000.0;
	int32 DoneTick = -1;
	for (int32 T = 1; T < 600 && DoneTick < 0; ++T)
	{
		const double Now = T / 60.0;
		DoneTick = SSXFrontEnd::MovieDone(false, Now >= VideoEnd, Audio, Now) ? T : -1;
	}
	TestEqual(TEXT("EABIG ends after 3.50 s (210 ticks), not at the video's 2.57 s"), DoneTick, 210);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIBootTest, "SSX.UI.Boot", UITestFlags)
bool FSSXUIBootTest::RunTest(const FString& Parameters)
{
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardBoot"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXCheats Cheats;
	FSSXFrontEnd FE;
	FE.Init(&Profile, &Cheats, &TestLoc(), true);
	TestTrue(TEXT("starts on the card check"), FE.Boot == ESSXFeBoot::StartScreen);
	for (int32 I = 0; I < 200 && FE.MovieRequest.IsEmpty(); ++I) FE.Tick();
	TestEqual(TEXT("then eabig.mpc"), FE.MovieRequest, FString(TEXT("EABIG")));
	FE.Key(0, ESSXFeKey::Cross);
	TestEqual(TEXT("eabig cannot be skipped"), FE.MovieRequest, FString(TEXT("EABIG")));
	FE.MovieFinished();
	TestEqual(TEXT("then ssxintro.mpc"), FE.MovieRequest, FString(TEXT("SSXINTRO")));
	FE.Key(0, ESSXFeKey::Start);
	TestTrue(TEXT("Start skips the intro (sound 6)"), FE.MovieRequest.IsEmpty() && FE.SoundHooks.Contains(TEXT("ui6")));
	TestEqual(TEXT("Title"), FString(LexToString(FE.Current())), FString(TEXT("Title")));
	TestEqual(TEXT("Title type id 0"), SSXFeTypeId(FE.Current()), 0);
	for (int32 I = 0; I < 3601; ++I) FE.Tick();
	TestEqual(TEXT("attract plays the intro"), FE.MovieRequest, FString(TEXT("SSXINTRO")));
	FE.MovieFinished();
	TestEqual(TEXT("back on Title"), FString(LexToString(FE.Current())), FString(TEXT("Title")));
	FSSXMemCard::RootOverride().Reset();
	return true;
}

// 0x2637A0 / 0x2AB918 / FUN_001FF648
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIBootIntroTest, "SSX.UI.BootIntro", UITestFlags)
bool FSSXUIBootIntroTest::RunTest(const FString& Parameters)
{
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardBootIntro"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXCheats Cheats;
	FSSXFrontEnd FE;
	FE.Init(&Profile, &Cheats, &TestLoc(), true);
	bool bFaderEver = false;
	auto Tick = [&FE, &bFaderEver](int32 N) { for (int32 I = 0; I < N; ++I) { FE.Tick(); bFaderEver |= FE.Fader.bActive; } };

	Tick(200);
	TestEqual(TEXT("eabig after the card check"), FE.MovieRequest, FString(TEXT("EABIG")));
	TestFalse(TEXT("eabig not skippable"), FE.bMovieSkippable);
	const ESSXFeKey All[] = { ESSXFeKey::Cross, ESSXFeKey::Start, ESSXFeKey::Triangle, ESSXFeKey::Circle, ESSXFeKey::Square, ESSXFeKey::Select };
	const int32 HooksBefore = FE.SoundHooks.Num();
	for (int32 Pad = 0; Pad < 2; ++Pad) { for (ESSXFeKey K : All) { FE.Key(Pad, K); } }
	TestEqual(TEXT("no key skips eabig"), FE.MovieRequest, FString(TEXT("EABIG")));
	TestEqual(TEXT("keys during eabig make no sound"), FE.SoundHooks.Num(), HooksBefore);
	Tick(30);
	TestEqual(TEXT("FE does not end eabig on a timer"), FE.MovieRequest, FString(TEXT("EABIG")));
	FE.MovieFinished();
	TestEqual(TEXT("ssxintro next"), FE.MovieRequest, FString(TEXT("SSXINTRO")));
	TestTrue(TEXT("ssxintro skippable"), FE.bMovieSkippable);
	TestFalse(TEXT("eabig's natural end plays no sound 6"), FE.SoundHooks.Contains(TEXT("ui6")));
	TestTrue(TEXT("no Title yet"), FE.Top() == nullptr);

	for (ESSXFeKey K : { ESSXFeKey::Triangle, ESSXFeKey::Circle, ESSXFeKey::Square, ESSXFeKey::Select, ESSXFeKey::Up })
	{
		FE.Key(0, K);
		FE.Key(1, K);
	}
	FE.Key(2, ESSXFeKey::Start);
	TestEqual(TEXT("other keys / pad 3 do not skip ssxintro"), FE.MovieRequest, FString(TEXT("SSXINTRO")));
	FE.Key(1, ESSXFeKey::Cross);
	TestTrue(TEXT("X on pad 2 skips ssxintro"), FE.MovieRequest.IsEmpty() && FE.Boot == ESSXFeBoot::FrontEnd);
	TestTrue(TEXT("skip plays sound 6"), FE.SoundHooks.Contains(TEXT("ui6")));
	TestEqual(TEXT("then Title"), FString(LexToString(FE.Current())), FString(TEXT("Title")));
	TestTrue(TEXT("Title opens with music event 0"), FE.SoundHooks.Contains(TEXT("music0")));
	TestFalse(TEXT("Title appears with a cut (no fader)"), FE.Fader.bActive);
	Tick(2);
	TestFalse(TEXT("no fader at any point of the boot chain"), bFaderEver);

	{
		FSSXFrontEnd F2;
		F2.Init(&Profile, &Cheats, &TestLoc(), true);
		for (int32 I = 0; I < 200 && F2.MovieRequest.IsEmpty(); ++I) F2.Tick();
		F2.MovieFinished();
		F2.MovieFinished();
		TestEqual(TEXT("ssxintro's end -> Title"), FString(LexToString(F2.Current())), FString(TEXT("Title")));
		TestFalse(TEXT("no sound 6 without a skip"), F2.SoundHooks.Contains(TEXT("ui6")));
		TestFalse(TEXT("no fader at Title"), F2.Fader.bActive);
	}

	FE.SoundHooks.Reset();
	Tick(FSSXFrontEnd::AttractFrames - 1 - FE.IdleFrames);
	TestTrue(TEXT("no attract before 3601 idle frames"), FE.MovieRequest.IsEmpty());
	Tick(1);
	TestEqual(TEXT("attract at 3601"), FE.MovieRequest, FString(TEXT("SSXINTRO")));
	TestTrue(TEXT("attract movie skippable"), FE.bMovieSkippable);
	FE.SoundHooks.Reset();
	FE.MovieFinished();
	TestTrue(TEXT("attract over: menus"), FE.Boot == ESSXFeBoot::FrontEnd && FE.MovieRequest.IsEmpty());
	TestTrue(TEXT("re-show posts music event 0"), FE.SoundHooks.Contains(TEXT("music0")));
	TestEqual(TEXT("re-show restarts the open phase"), FE.Top() ? FE.Top()->Phase : -1, 0);
	Tick(2);
	TestTrue(TEXT("Title vfunc_20 speech 0 after the attract"), FE.SoundHooks.Contains(TEXT("speech0")));
	TestTrue(TEXT("idle counter cleared"), FE.IdleFrames <= 2);
	TestFalse(TEXT("no fader after the attract"), FE.Fader.bActive);
	FSSXMemCard::RootOverride().Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFrontEndFlowTest, "SSX.UI.FrontEndFlow", UITestFlags)
bool FSSXUIFrontEndFlowTest::RunTest(const FString& Parameters)
{
	{
		FFe F;
		TestEqual(TEXT("Title items"), F.Locs(), TArray<int32>({ 3696, 3315 }));
		TestFalse(TEXT("Title has no Back"), F.FE.Top()->bBack);
		F.X();
		TestEqual(TEXT("ModeSelect"), F.Screen(), FString(TEXT("ModeSelect")));
		TestEqual(TEXT("ModeSelect tab 3499, title bar 105"), F.FE.Top()->TitleLoc * 1000 + (int32)F.FE.Top()->BarH, 3499 * 1000 + 105);
		TestEqual(TEXT("ModeSelect help 0x200A"), F.FE.Top()->HelpMask, 0x200Au);
		TestEqual(TEXT("ModeSelect items"), F.Locs(), TArray<int32>({ 3594, 3595, 3596 }));
		TestEqual(TEXT("default Single Event (id1)"), F.FE.Top()->SelectedId(), 1);
		TestEqual(TEXT("camera modeSelectIntro"), F.FE.Camera.Current, FString(TEXT("modeSelectIntro")));
		F.X();
		TestEqual(TEXT("1 pad: SE -> CharacterSelect"), F.Screen(), FString(TEXT("CharacterSelect")));
		const FSSXFeState* CS = F.FE.Top();
		TestEqual(TEXT("title PLAYER1 SELECT CHARACTER"), CS->TitleLoc, 3495);
		TestEqual(TEXT("12 riders"), CS->Items.Num(), 12);
		TestFalse(TEXT("carousel is horizontal"), CS->bVertical);
		TestEqual(TEXT("slot 0 = Mac"), CS->Items[0].Text, FString(TEXT("Mac")));
		TestFalse(TEXT("Kaori locked"), CS->Items[1].bEnabled);
		TestEqual(TEXT("Kaori lock text 3525+7-4"), CS->Items[1].HelpLoc, 3528);
		TestTrue(TEXT("camera SE -> charSelect (ModeSelect vfunc_21, request 6)"), F.FE.Camera.History.Contains(TEXT("modeSelectSE_charSelect")));
		TestEqual(TEXT("then CharacterSelect vfunc_15: select<Name> frame 0 (FUN_0026B760)"), F.FE.Camera.Current, FString(TEXT("charSelect_selectEddie")));
		TestEqual(TEXT("opens on Eddie (slot 4)"), F.FE.Top()->Sel, 4);
		F.Key(ESSXFeKey::Down);
		TestEqual(TEXT("Up/Down do nothing on the carousel"), F.FE.Top()->Sel, 4);
		F.Key(ESSXFeKey::Left);
		TestEqual(TEXT("Left -> slot 3"), F.FE.Top()->Sel, 3);
		TestEqual(TEXT("cycleLeft camera"), F.FE.Camera.Current, FString(TEXT("charSelect_cycleLeft")));
		F.Key(ESSXFeKey::Left);
		F.Key(ESSXFeKey::Left);
		TestEqual(TEXT("Left x2 -> Kaori (slot 1)"), F.FE.Top()->Sel, 1);
		F.X();
		TestEqual(TEXT("X on a locked rider does nothing"), F.Screen(), FString(TEXT("CharacterSelect")));
		F.Key(ESSXFeKey::Left);
		F.X();
		TestEqual(TEXT("X on Mac -> CharacterAccept"), F.Screen(), FString(TEXT("CharacterAccept")));
		TestEqual(TEXT("camera selectMac"), F.FE.Camera.Current, FString(TEXT("charSelect_selectMac")));
		TestEqual(TEXT("Accept items"), F.Locs(), TArray<int32>({ 3585 }));
		TestEqual(TEXT("Accept title 3502"), F.FE.Top()->TitleLoc, 3502);
		F.X();
		TestEqual(TEXT("CharacterModify"), F.Screen(), FString(TEXT("CharacterModify")));
		TestEqual(TEXT("Modify items"), F.Locs(), TArray<int32>({ 3585, 3603, 3578, 3606, 3610, 3612 }));
		TestEqual(TEXT("Modify title bar 314"), (int32)F.FE.Top()->BarH, 314);
		F.Key(ESSXFeKey::Down); F.Key(ESSXFeKey::Down); F.X();
		TestEqual(TEXT("Board -> CustomBoard"), F.Screen(), FString(TEXT("CustomBoard")));
		TestEqual(TEXT("CUSTOMIZE BOARD 181"), F.FE.Top()->TitleLoc, 181);
		TestEqual(TEXT("12 boards"), F.FE.Top()->Items.Num(), 12);
		TestEqual(TEXT("board 2 lock text 3252"), F.FE.Top()->Items[2].HelpLoc, 3252);
		TestEqual(TEXT("board 11 lock text 3261"), F.FE.Top()->Items[11].HelpLoc, 3261);
		F.Key(ESSXFeKey::Down);
		TestEqual(TEXT("Up/Down ignored on CustomBoard (horizontal)"), F.Profile.Slot[0].Board, 0);
		F.Key(ESSXFeKey::Right);
		TestEqual(TEXT("live preview (Right)"), F.Profile.Slot[0].Board, 1);
		F.Key(ESSXFeKey::Triangle);
		TestEqual(TEXT("Back restores the board"), F.Profile.Slot[0].Board, 0);
		TestEqual(TEXT("back on Modify"), F.Screen(), FString(TEXT("CharacterModify")));
		F.Key(ESSXFeKey::Up); F.Key(ESSXFeKey::Up);
		F.X();
		TestEqual(TEXT("Continue -> EventSel"), F.Screen(), FString(TEXT("EventSel")));
		TestEqual(TEXT("SE events Race, Showoff, Time Challenge"), F.Locs(), TArray<int32>({ 3589, 3590, 3717 }));
		TestEqual(TEXT("3 items: title bar 224"), (int32)F.FE.Top()->BarH, 224);
		F.X();
		TestEqual(TEXT("Race -> DifficultSel"), F.Screen(), FString(TEXT("DifficultSel")));
		TestEqual(TEXT("difficulties"), F.Locs(), TArray<int32>({ 3587, 3593, 3592 }));
		F.Profile.VenueUnlocks |= 1u << 11;
		F.Key(ESSXFeKey::Down);
		F.X();
		TestEqual(TEXT("-> VenueSel"), F.Screen(), FString(TEXT("VenueSel")));
		TestEqual(TEXT("venue title 3501"), F.FE.Top()->TitleLoc, 3501);
		TestEqual(TEXT("10 venues"), F.FE.Top()->Items.Num(), 10);
		TestFalse(TEXT("Mesablanca locked on a new save (0x407)"), F.FE.Top()->Items[3].bEnabled);
		TestTrue(TEXT("Elysium open on a new save"), F.FE.Top()->Items[2].bEnabled);
		TestEqual(TEXT("camera venueSelect_intro"), F.FE.Camera.Current, FString(TEXT("venueSelect_intro")));
		// 0x163BB8
		F.Key(ESSXFeKey::Right); F.Key(ESSXFeKey::Right); F.Key(ESSXFeKey::Right);
		TestEqual(TEXT("Right x3 -> Mesablanca (3)"), F.FE.Top()->SelectedId(), 3);
		F.FE.SoundHooks.Reset();
		F.X();
		TestTrue(TEXT("locked Mesablanca: refused sound (VenueSel +0x110 = FE 3), no accept"), F.FE.SoundHooks.Contains(TEXT("ui3"))
			&& !F.FE.SoundHooks.Contains(TEXT("ui1")) && !F.FE.bLaunchPending && F.Screen() == TEXT("VenueSel"));
		F.Key(ESSXFeKey::Left); F.Key(ESSXFeKey::Left); F.Key(ESSXFeKey::Left);
		F.Key(ESSXFeKey::Left);
		F.Key(ESSXFeKey::Left);
		F.Key(ESSXFeKey::Left);
		TestEqual(TEXT("VenueSel wraps (+0x158 = 1): Left x3 from Garibaldi = Alaska (11)"), F.FE.Top()->SelectedId(), 11);
		F.FE.Key(0, ESSXFeKey::Cross);
		F.FE.Tick();
		TestTrue(TEXT("accept closes the screen (phase 3/4)"), F.FE.bLaunchPending && F.FE.Top()->Phase >= 3);
		// FUN_0026C2C0
		TestEqual(TEXT("camera script 0x97 for Alaska (index 7)"), F.FE.Camera.Current, FString(TEXT("venueSelect_chooseUntracked")));
		TestTrue(TEXT("scene function FEAlas1 (.data 0x347180)"), F.FE.SceneEvents.Contains(TEXT("FEAlas1")));
		// 0x29C238 / FUN_001FF648
		TestTrue(TEXT("map pick: fade to black 1 s, hold 10 s"), F.FE.Fader.bActive && F.FE.Fader.Up == 1.f && F.FE.Fader.Hold == 10.f
			&& F.FE.Fader.Down == 0.f && F.FE.Fader.Color == FLinearColor::Black);
		F.FE.Key(0, ESSXFeKey::Triangle);
		TestEqual(TEXT("no input while closing"), F.Screen(), FString(TEXT("VenueSel")));
		const int32 Frames = F.RunToLaunch();
		TestTrue(TEXT("launch requested"), F.FE.bLaunchRequested);
		if (TestCameras().IsLoaded())
		{
			// Phase 4 waits for the 2 s choose camera (60 Hz), then the loading screen is drawn first.
			TestTrue(TEXT("waits for the 2 s choose camera"), Frames >= 120 && Frames < 130);
		}
		TestTrue(TEXT("loading screen (cFlakeSpinnerLoad)"), F.FE.Boot == ESSXFeBoot::Loading);
		TestTrue(TEXT("hint n in 0..18, never 17"), F.FE.HintIndex >= 0 && F.FE.HintIndex < 19 && F.FE.HintIndex != 17);
		{
			FSSXFeDrawList D;
			F.FE.Draw(D);
			TestFalse(TEXT("no loading... text in the first frames"), D.Texts.Contains(TEXT("loading...")));
		}
		TestEqual(TEXT("mode 2 PRACTICE_RACE (SE race)"), F.FE.Launch.GameMode, 2);
		TestEqual(TEXT("track 11"), F.FE.Launch.Track, 11);
		TestEqual(TEXT("difficulty Semi Pro"), F.FE.Launch.Difficulty, 1);
		TestEqual(TEXT("rider Mac (3)"), F.FE.Launch.Slots[0].Char, 3);
		TestTrue(TEXT("scene event FEAlas1 (.data 0x347180)"), F.FE.SceneEvents.Contains(TEXT("FEAlas1")));
	}
	{
		FFe F;
		F.X();
		F.Key(ESSXFeKey::Left);
		TestEqual(TEXT("Left = Practice, group 1"), F.FE.Group, 1);
		TestEqual(TEXT("swing camera SE -> PR"), F.FE.Camera.Current, FString(TEXT("modeSelectSE_PR")));
		TestTrue(TEXT("onPROpen"), F.FE.SceneEvents.Contains(TEXT("onPROpen")));
		F.X();
		TestEqual(TEXT("EventSel"), F.Screen(), FString(TEXT("EventSel")));
		TestEqual(TEXT("Practice events"), F.Locs(), TArray<int32>({ 3588, 3591 }));
		TestEqual(TEXT("2 items: title bar 194"), (int32)F.FE.Top()->BarH, 194);
		F.Key(ESSXFeKey::Down);
		F.X();
		TestEqual(TEXT("Tutorial -> CharacterSelect"), F.Screen(), FString(TEXT("CharacterSelect")));
		TestEqual(TEXT("mode 6"), F.FE.GameMode, 6);
		F.X(); F.X(); F.X();
		F.RunToLaunch();
		TestTrue(TEXT("Continue launches the tutorial"), F.FE.bLaunchRequested);
		TestEqual(TEXT("track 10"), F.FE.Launch.Track, 10);
	}
	{
		FFe F;
		F.X();
		F.Key(ESSXFeKey::Right);
		TestEqual(TEXT("Right = WC"), F.FE.Group, 2);
		F.X();
		TestEqual(TEXT("CharacterSelect"), F.Screen(), FString(TEXT("CharacterSelect")));
		F.X();
		TestEqual(TEXT("WC -> UserName"), F.Screen(), FString(TEXT("UserName")));
		TestEqual(TEXT("default name PLAYER1 (P+0x1630)"), F.FE.Top()->Name, FString(TEXT("PLAYER1")));
		TestEqual(TEXT("cursor starts on END (FUN_002B0428 'e')"), F.FE.Top()->GridY * 14 + F.FE.Top()->GridX, 41);
		F.Key(ESSXFeKey::Left);
		for (int32 I = 0; I < 7; ++I) { F.X(); }
		F.Key(ESSXFeKey::Down);
		for (int32 I = 0; I < 12; ++I) { F.Key(ESSXFeKey::Left); }
		TestEqual(TEXT("name cleared"), F.FE.Top()->Name, FString());
		TestEqual(TEXT("ENTER YOUR NAME 3505"), F.FE.Top()->TitleLoc, 3505);
		F.X();
		F.Key(ESSXFeKey::Right); F.X();
		TestEqual(TEXT("name AB"), F.FE.Top()->Name, FString(TEXT("AB")));
		F.Key(ESSXFeKey::Left); F.Key(ESSXFeKey::Left);
		F.Key(ESSXFeKey::Up);
		TestEqual(TEXT("END cell"), (int32)SSXFeUserName::Cell(F.FE.Top()->GridX, F.FE.Top()->GridY), (int32)TEXT('e'));
		F.X();
		TestEqual(TEXT("END -> CharacterAccept"), F.Screen(), FString(TEXT("CharacterAccept")));
		TestEqual(TEXT("name stored"), F.Profile.Slot[0].UserName, FString(TEXT("AB")));
		F.X(); F.X();
		TestEqual(TEXT("-> EventSel"), F.Screen(), FString(TEXT("EventSel")));
		TestEqual(TEXT("WC events Race, Showoff"), F.Locs(), TArray<int32>({ 3589, 3590 }));
		F.Key(ESSXFeKey::Down);
		F.X();
		TestEqual(TEXT("Showoff -> VenueSel"), F.Screen(), FString(TEXT("VenueSel")));
		TestEqual(TEXT("mode 5 CIRCUIT_SHOWOFF"), F.FE.GameMode, 5);
	}
	{
		FFe F(2);
		F.X();
		F.X();
		TestEqual(TEXT("NumPlayers"), F.Screen(), FString(TEXT("NumPlayers")));
		TestEqual(TEXT("items"), F.Locs(), TArray<int32>({ 3597, 3611 }));
		TestEqual(TEXT("title bar 194"), (int32)F.FE.Top()->BarH, 194);
		F.Key(ESSXFeKey::Down);
		F.X();
		TestEqual(TEXT("2 players"), F.FE.NumPlayers, 2);
		F.X(); F.X(); F.X();
		TestEqual(TEXT("player 2 select"), F.Screen(), FString(TEXT("CharacterSelect")));
		TestEqual(TEXT("PLAYER2 SELECT CHARACTER"), F.FE.Top()->TitleLoc, 3496);
		F.X(0);
		TestEqual(TEXT("pad 1 cannot drive player 2"), F.Screen(), FString(TEXT("CharacterSelect")));
		F.X(1);
		TestEqual(TEXT("pad 2 accepts"), F.Screen(), FString(TEXT("CharacterAccept")));
		TestEqual(TEXT("PLAYER2 SETUP"), F.FE.Top()->TitleLoc, 3503);
	}
	{
		FFe F;
		F.Key(ESSXFeKey::Down);
		F.X();
		TestEqual(TEXT("DVD Content"), F.Screen(), FString(TEXT("DVDContent")));
		TestEqual(TEXT("DVD items"), F.Locs(), TArray<int32>({ 3323, 3319, 3318, 3321, 3320, 3322, 3317, 3324, 3325 }));
		TestEqual(TEXT("DVD type id 11"), SSXFeTypeId(F.FE.Current()), 11);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIListAxesTest, "SSX.UI.ListAxes", UITestFlags)

bool FSSXUIListAxesTest::RunTest(const FString& Parameters)
{
	struct FRow { ESSXFeScreen S; bool bVertical; bool bWrap; };
	const FRow Rows[] = {
		{ ESSXFeScreen::Title, true, false }, { ESSXFeScreen::ModeSelect, false, false }, { ESSXFeScreen::NumPlayers, true, false },
		{ ESSXFeScreen::CharacterSelect, false, true }, { ESSXFeScreen::UserName, false, false }, { ESSXFeScreen::CharacterAccept, false, false },
		{ ESSXFeScreen::CharacterModify, true, false }, { ESSXFeScreen::CustomBody, false, false }, { ESSXFeScreen::CustomBoard, false, false },
		{ ESSXFeScreen::RiderProfile, false, false }, { ESSXFeScreen::TrickBook, false, false }, { ESSXFeScreen::EventSel, true, false },
		{ ESSXFeScreen::DifficultSel, true, false }, { ESSXFeScreen::VenueSel, false, true }, { ESSXFeScreen::DVDContent, true, false },
		{ ESSXFeScreen::Options, true, false },
	};
	FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXCheats Cheats;
	Cheats.bUnlockAll = true;
	int32 Ok = 0;
	for (const FRow& R : Rows)
	{
		FSSXFrontEnd FE;
		FE.Init(&Profile, &Cheats, nullptr, false);
		FE.GoTo(R.S);
		const FSSXFeState* T = FE.Top();
		const bool bMatch = T && T->Screen == R.S && T->bVertical == R.bVertical && T->bWrap == R.bWrap;
		Ok += bMatch;
		TestTrue(*FString::Printf(TEXT("%s: %s%s"), LexToString(R.S), R.bVertical ? TEXT("Up/Down") : TEXT("Left/Right"), R.bWrap ? TEXT(", wrap") : TEXT("")), bMatch);
		if (T && T->Screen == R.S && T->Items.Num() > 1 && R.S != ESSXFeScreen::UserName && R.S != ESSXFeScreen::Options)
		{
			const int32 Before = T->Sel;
			FE.Key(0, R.bVertical ? ESSXFeKey::Left : ESSXFeKey::Up);
			FE.Tick();
			TestEqual(*FString::Printf(TEXT("%s ignores the other axis"), LexToString(R.S)), FE.Top()->Sel, Before);
		}
	}
	AddInfo(FString::Printf(TEXT("list axes: %d / %d screens match"), Ok, UE_ARRAY_COUNT(Rows)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIVenueHelpTest, "SSX.UI.VenueHelp", UITestFlags)

bool FSSXUIVenueHelpTest::RunTest(const FString& Parameters)
{
	if (!TestLoc().IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("strings missing: skipped"));
		return true;
	}
	FFe F;
	F.GoTo(ESSXFeScreen::VenueSel);
	FSSXFeState* S = F.FE.TopMut();
	if (!TestTrue(TEXT("VenueSel"), S && S->Screen == ESSXFeScreen::VenueSel))
	{
		return false;
	}
	auto SelectTrack = [S](int32 T) { for (int32 I = 0; I < S->Items.Num(); ++I) { if (S->Items[I].Id == T) { S->Sel = I; } } };
	SelectTrack(0);
	TestEqual(TEXT("Garibaldi (open) = LOC 3277"), F.FE.CurrentHelpLine(), TestLoc().ByIndex(3277));
	SelectTrack(3);
	TestEqual(TEXT("Mesablanca (locked, mode 2) = LOC 322 with %s = LOC 49"), F.FE.CurrentHelpLine(),
		TestLoc().ByIndex(322).Replace(TEXT("%s"), *TestLoc().ByIndex(49)));
	SelectTrack(6);
	TestEqual(TEXT("Pipedream locked = LOC 498"), F.FE.CurrentHelpLine(), TestLoc().ByIndex(498));
	SelectTrack(7);
	TestEqual(TEXT("Untracked locked = LOC 578"), F.FE.CurrentHelpLine(), TestLoc().ByIndex(578));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIOptionsTest, "SSX.UI.Options", UITestFlags)
bool FSSXUIOptionsTest::RunTest(const FString& Parameters)
{
	FFe F;
	F.Key(ESSXFeKey::Square);
	TestEqual(TEXT("Square -> Options"), F.Screen(), FString(TEXT("Options")));
	TestEqual(TEXT("OPTIONS 3493"), F.FE.Top()->TitleLoc, 3493);
	TestEqual(TEXT("top menu"), F.Locs(), TArray<int32>({ 3608, 176, 186, 204, 180, -1 }));   // + the PC-only Video item (text, no LOC)
	F.Key(ESSXFeKey::Square);
	TestEqual(TEXT("no Options on Options"), F.FE.Depth(), 2);
	F.X();
	TestEqual(TEXT("Sound page"), F.FE.Top()->Page, 0);
	const TArray<SSXFeOptions::FRow>& Rows = SSXFeOptions::PageRows(0);
	TArray<int32> SpeakerLocs;
	for (const SSXFeOptions::FValue& V : Rows[0].Values) { SpeakerLocs.Add(V.Loc); }
	TestEqual(TEXT("speaker values"), SpeakerLocs, TArray<int32>({ 206, 228, 229, 184 }));
	TestEqual(TEXT("page ends with DONE"), Rows.Last().Loc, 3533);
	const float Before = F.Profile.Settings.Master;
	F.Key(ESSXFeKey::Down); F.Key(ESSXFeKey::Down);
	F.Key(ESSXFeKey::Left);
	TestTrue(TEXT("master bar step 1/11"), FMath::IsNearlyEqual(F.Profile.Settings.Master, Before - 1.f / 11.f, 1e-4f));
	F.Key(ESSXFeKey::Triangle);
	TestTrue(TEXT("Back restores the snapshot"), FMath::IsNearlyEqual(F.Profile.Settings.Master, Before));
	TestEqual(TEXT("back on the top list"), F.FE.Top()->Page, -1);
	F.Key(ESSXFeKey::Triangle);
	TestEqual(TEXT("Back closes Options"), F.Screen(), FString(TEXT("Title")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIMemCardTest, "SSX.UI.MemCard", UITestFlags)
bool FSSXUIMemCardTest::RunTest(const FString& Parameters)
{
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCard"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	TestEqual(TEXT("settings file name"), FSSXMemCard::SettingsName(), FString(TEXT("BASLUS-20326-SET0001")));
	TestEqual(TEXT("replay file name"), FSSXMemCard::ReplayName(0), FString(TEXT("BASLUS-20326-REP0001")));
	TestEqual(TEXT("KB settings 141"), FSSXMemCard::KbRequired(FSSXProfile::SettingsBlobSize), 141);

	FSSXProfile P = FSSXProfile::MakeDefault();
	P.Settings.Music = 0.25f;
	FSSXMemCard M;
	auto Run = [&M](int32 Frames) { for (int32 I = 0; I < Frames; ++I) M.Tick(); };
	M.Begin(FSSXMemCard::EMode::Game, true, &P);
	TestEqual(TEXT("checking (LOC 64)"), (int32)M.State, 2);
	TestEqual(TEXT("LOC 64"), M.MessageLoc(), 64);
	Run(181);
	TestEqual(TEXT("reading (LOC 416)"), M.MessageLoc(), 416);
	Run(91);
	TestEqual(TEXT("save list"), (int32)M.State, 7);
	TestEqual(TEXT("help 0xB00"), M.HelpMask(), 0xB00u);
	M.Input(FSSXMemCard::InAccept);
	TestEqual(TEXT("empty slot -> saving 0xD"), (int32)M.State, 0xD);
	Run(130);
	TestEqual(TEXT("save complete 0x13 (LOC 447)"), M.MessageLoc(), 447);
	TestTrue(TEXT("file written"), M.FileExists(0));
	M.Input(FSSXMemCard::InAccept);
	TestEqual(TEXT("-> idle"), (int32)M.State, 1);
	M.Begin(FSSXMemCard::EMode::Game, true, &P);
	Run(300);
	M.Input(FSSXMemCard::InAccept);
	TestEqual(TEXT("overwrite 9 (LOC 404, yes/no)"), M.MessageLoc() * 1000 + (int32)M.DialogMask(), 404 * 1000 + 0xC0);
	M.Input(FSSXMemCard::InBack);
	TestEqual(TEXT("Triangle -> list"), (int32)M.State, 7);
	M.Input(FSSXMemCard::InBack);
	FSSXProfile Q = FSSXProfile::MakeDefault();
	M.Begin(FSSXMemCard::EMode::Game, false, &Q);
	Run(300);
	TestEqual(TEXT("load list 0x1C"), (int32)M.State, 0x1C);
	M.Input(FSSXMemCard::InAccept);
	Run(130);
	TestEqual(TEXT("load complete 0x21 (LOC 371)"), M.MessageLoc(), 371);
	TestTrue(TEXT("settings loaded"), FMath::IsNearlyEqual(Q.Settings.Music, 0.25f));
	M.Input(FSSXMemCard::InAccept);
	// 0x1CF040
	M.Begin(FSSXMemCard::EMode::Game, false, &Q);
	Run(300);
	M.Input(FSSXMemCard::InCircle);
	Run(3);
	TestTrue(TEXT("load list: Circle ignored"), M.State == FSSXMemCard::LoadList && M.FileExists(0));
	M.Input(FSSXMemCard::InBack);
	Run(2);
	M.Begin(FSSXMemCard::EMode::Game, true, &Q);
	Run(300);
	M.Input(FSSXMemCard::InCircle);
	TestEqual(TEXT("delete? 0x17 (LOC 346)"), M.MessageLoc(), 346);
	M.Input(FSSXMemCard::InAccept);
	Run(3);
	TestEqual(TEXT("delete complete (LOC 344)"), M.MessageLoc(), 344);
	TestFalse(TEXT("file gone"), M.FileExists(0));
	// 0x1BA0F0
	{
		FSSXMemCard R;
		R.Mode = FSSXMemCard::EMode::Replay;
		R.bInRace = true;
		R.MovePort(true);
		TestTrue(TEXT("port down from 0 (of 2): navigate, port 1"), R.Port == 1 && R.Sounds.Num() == 1 && R.Sounds[0] == SSXUISound::ERace::Navigate);
		R.MovePort(true);
		TestTrue(TEXT("port down at the end: silent, no wrap"), R.Port == 1 && R.Sounds.Num() == 1);
		R.MovePort(false);
		TestTrue(TEXT("port up from 1: navigate"), R.Port == 0 && R.Sounds.Num() == 2);
		R.MovePort(false);
		TestTrue(TEXT("port up at 0: silent, no wrap"), R.Port == 0 && R.Sounds.Num() == 2);
		R.AcceptPort();
		TestTrue(TEXT("vfunc_50: accept"), R.Sounds.Num() == 3 && R.Sounds[2] == SSXUISound::ERace::Accept);
		R.Sounds.Reset();
		R.State = FSSXMemCard::SaveList;
		R.Input(FSSXMemCard::InDown);
		R.Input(FSSXMemCard::InDown);
		R.Input(FSSXMemCard::InDown);
		TestTrue(TEXT("three file moves down: three navigates, file 3"), R.Selected == 3 && R.Sounds.Num() == 3);
		R.Input(FSSXMemCard::InDown);
		TestTrue(TEXT("down at the last file: silent, no wrap (+0x3C = 0)"), R.Selected == 3 && R.Sounds.Num() == 3);
		R.Input(FSSXMemCard::InUp);
		TestTrue(TEXT("up from 3: navigate"), R.Selected == 2 && R.Sounds.Num() == 4);
		R.Input(FSSXMemCard::InUp);
		R.Input(FSSXMemCard::InUp);
		R.Input(FSSXMemCard::InUp);
		TestTrue(TEXT("up at file 0: silent, no wrap"), R.Selected == 0 && R.Sounds.Num() == 6);
		FSSXMemCard F;
		F.State = FSSXMemCard::SaveList;
		F.Mode = FSSXMemCard::EMode::Replay;
		F.Input(FSSXMemCard::InDown);
		F.MovePort(true);
		F.AcceptPort();
		TestTrue(TEXT("FE overlay: no sounds"), F.Sounds.Num() == 0 && F.Selected == 1);
	}
	FSSXMemCard::RootOverride().Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUICheatsTest, "SSX.UI.Cheats", UITestFlags)
bool FSSXUICheatsTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	FFe F;
	F.X(); F.X();
	F.FE.SetHeld(0, PadL1 | PadR1);
	const ESSXFeKey Mmm[] = { ESSXFeKey::Cross, ESSXFeKey::Cross, ESSXFeKey::Right, ESSXFeKey::Cross, ESSXFeKey::Cross, ESSXFeKey::Down,
		ESSXFeKey::Cross, ESSXFeKey::Cross, ESSXFeKey::Left, ESSXFeKey::Cross, ESSXFeKey::Cross, ESSXFeKey::Up };
	for (ESSXFeKey K : Mmm) F.Key(K);
	TestTrue(TEXT("Mix Master Mike on"), F.Cheats.bMixMasterMike);
	TestEqual(TEXT("entry does not navigate"), F.Screen(), FString(TEXT("CharacterSelect")));
	TestTrue(TEXT("FE sound 3"), F.FE.SoundHooks.Contains(TEXT("ui3")));
	for (ESSXFeKey K : Mmm) F.Key(K);
	TestFalse(TEXT("second entry toggles off"), F.Cheats.bMixMasterMike);
	const ESSXFeKey All[] = { ESSXFeKey::Cross, ESSXFeKey::Triangle, ESSXFeKey::Right, ESSXFeKey::Circle, ESSXFeKey::Square, ESSXFeKey::Down,
		ESSXFeKey::Triangle, ESSXFeKey::Square, ESSXFeKey::Left, ESSXFeKey::Circle, ESSXFeKey::Cross, ESSXFeKey::Up };
	for (ESSXFeKey K : All) F.Key(K);
	TestTrue(TEXT("unlock all"), F.Cheats.bUnlockAll);
	TestTrue(TEXT("Kaori now selectable"), F.FE.Top()->Items[1].bEnabled);
	// FUN_00278840 / FUN_001635B8 / 0x16BBA0
	F.Profile.Session.Heat = 1;
	F.Profile.Session.HeatFactor = SSXHeats::HeatFactor(0);
	F.FE.SetHeld(0, 0);
	F.Key(ESSXFeKey::Right);
	TestTrue(TEXT("CharacterSelect highlight clears C+0x14 / C+0x18"), F.Profile.Session.Heat == 0 && F.Profile.Session.HeatFactor == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIPauseTest, "SSX.UI.Pause", UITestFlags)
bool FSSXUIPauseTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCheats C;
	auto Fresh = [&P, &C](FSSXOverlayManager& O) { O.Init(&P, &C, &TestLoc()); };
	TArray<int32> Locs;
	for (const FSSXOverlayManager::FPauseItem& I : FSSXOverlayManager::PauseItems()) Locs.Add(I.Loc);
	TestEqual(TEXT("pause items"), Locs, TArray<int32>({ 475, 480, 477, 478, 495 }));
	{
		FSSXOverlayManager O; Fresh(O);
		O.Pad(0, PadStart);
		TestTrue(TEXT("Start pauses"), O.IsActive(OvPauseMenu));
		TestTrue(TEXT("pause is modal"), O.AnyModal());
		O.Pad(0, PadStart);
		TestTrue(TEXT("Start does not close the menu"), O.IsActive(OvPauseMenu));
		O.Pad(0, PadDown); O.Pad(0, PadDown); O.Pad(0, PadCross);
		TestEqual(TEXT("Options: Start, Down, Down, X"), O.PauseDepth, 1);
		TestEqual(TEXT("options rows (1 player)"), O.PauseOptionRows(), TArray<int32>({ 483, 484, 481, 489, 486, 490, 488, 487, 3225 }));
		O.Pad(0, PadTriangle);
		TestEqual(TEXT("Triangle pops"), O.PauseDepth, 0);
		O.Pad(0, PadTriangle);
		TestFalse(TEXT("Triangle on main closes"), O.IsActive(OvPauseMenu));
	}
	{
		FSSXOverlayManager O; Fresh(O);
		O.Pad(0, PadStart); O.Pad(0, PadUp); O.Pad(0, PadCross);
		TestTrue(TEXT("Trick book: Start, Up (wraps to row 4), X"), O.bTrickBook);
		// 0x1ACE00 / 0x1A8648 / 0x1A8658
		using ER = SSXUISound::ERace;
		const int32 First = FSSXOverlayManager::ChapterFirstPage(O.TrickBookChapter());
		O.TrickPage = First;
		O.Sounds.Reset();
		O.Pad(0, PadLeft);
		TestTrue(TEXT("Left on the chapter's first page: silent, stays"), O.Sounds.Num() == 0 && O.TrickPage == First);
		O.Pad(0, PadRight);
		TestTrue(TEXT("Right: next page, navigate sound"), O.TrickPage == First + 1 && O.Sounds == TArray<ER>({ ER::Navigate }));
		O.Pad(0, PadLeft);
		TestTrue(TEXT("Left: previous page, navigate sound"), O.TrickPage == First && O.Sounds == TArray<ER>({ ER::Navigate, ER::Navigate }));
		for (int32 K = 0; K < 10; ++K) O.Pad(0, 0x80000);
		TestTrue(TEXT("analog Right to the chapter's last page, one sound per page"), O.TrickPage == First + 4 && O.Sounds.Num() == 6);
		O.Pad(0, PadUp); O.Pad(0, PadDown); O.Pad(0, PadCross);
		TestTrue(TEXT("Up / Down / X in the book: nothing"), O.Sounds.Num() == 6 && O.bTrickBook && O.TrickPage == First + 4);
		O.Pad(0, PadTriangle);
		TestTrue(TEXT("Triangle: back sound, the book closes, the menu stays"), !O.bTrickBook && O.IsActive(OvPauseMenu) && O.Sounds.Last() == ER::Back);
		O.Sounds.Reset();
		O.Pad(0, PadTriangle);
		TestTrue(TEXT("Triangle on the main menu: back sound, closes"), !O.IsActive(OvPauseMenu) && O.Sounds == TArray<ER>({ ER::Back }));
	}
	{
		FSSXOverlayManager O; Fresh(O);
		O.Pad(0, PadStart); O.Pad(0, PadDown); O.Pad(0, PadCross);
		TestTrue(TEXT("restart asks 326"), O.Dialog.bActive && O.Dialog.Line1 == 326);
		(void)O.ConsumeAction();
		O.Pad(0, PadCross);
		TestTrue(TEXT("Restart: Start, Down, X, X"), O.ConsumeAction() == FSSXOverlayManager::EAction::Restart);
	}
	{
		FSSXOverlayManager O; Fresh(O);
		P.bSettingsDirty = false;
		O.Pad(0, PadStart); O.Pad(0, PadUp); O.Pad(0, PadUp); O.Pad(0, PadCross);
		TestTrue(TEXT("quit asks 326 yes/no"), O.Dialog.bActive && O.Dialog.LocX == 470 && O.Dialog.LocTriangle == 392);
		(void)O.ConsumeAction();
		O.Pad(0, PadCross);
		TestTrue(TEXT("Quit: Start, Up, Up, X, X"), O.ConsumeAction() == FSSXOverlayManager::EAction::QuitToFrontEnd);
	}
	{
		FSSXOverlayManager O; Fresh(O);
		P.bSettingsDirty = true;
		O.Pad(0, PadStart); O.Pad(0, PadUp); O.Pad(0, PadUp); O.Pad(0, PadCross);
		TestTrue(TEXT("dirty settings ask 444 yes/no/cancel"), O.Dialog.Line1 == 444 && O.Dialog.LocCircle == 392 && O.Dialog.LocTriangle == 340);
		P.bSettingsDirty = false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIPostRaceTest, "SSX.UI.PostRace", UITestFlags)
bool FSSXUIPostRaceTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCheats C;
	FSSXRaceUIHostStub H;
	FSSXOverlayManager O;
	O.Init(&P, &C, &TestLoc());
	O.SetHost(&H);
	H.Phase = 3;
	H.Countdown = -298.0;   // 0x113520
	O.Tick();
	TestTrue(TEXT("countdown overlay"), O.IsActive(OvCountdown));
	O.Pad(0, PadStart);
	TestTrue(TEXT("pause allowed during the countdown (phase 3)"), O.IsActive(OvPauseMenu));
	O.Pad(0, PadTriangle);
	H.Phase = 1;
	O.Pad(0, PadStart);
	TestFalse(TEXT("no pause in PreRace"), O.IsActive(OvPauseMenu));
	H.Phase = 4;
	O.Tick();
	TestFalse(TEXT("countdown off in Race"), O.IsActive(OvCountdown));
	H.Phase = 5;
	H.bPostRaceReady = true;
	O.Tick();
	TestTrue(TEXT("results (slot 1)"), O.IsActive(OvRaceResults));
	TestEqual(TEXT("SE race items"), O.ResultsItems(), TArray<int32>({ 0, 2, 1, 4, 3 }));
	O.Pad(0, PadDown); O.Pad(0, PadDown); O.Pad(0, PadDown);
	O.Pad(0, PadCross);
	TestTrue(TEXT("records -> slot 11"), O.IsActive(OvViewHiScores));
	O.Pad(0, PadTriangle);
	TestTrue(TEXT("Triangle -> slot 1"), O.IsActive(OvRaceResults));
	O.ResultsSel = 0;
	O.Pad(0, PadCross);
	TestTrue(TEXT("next event -> slot 17"), O.IsActive(OvSelectNextEvent));
	TestEqual(TEXT("default Race (mode 2)"), O.NextEventSel, 0);
	O.Pad(0, PadCross);
	TestTrue(TEXT("Race -> slot 16"), O.IsActive(OvSelectNextVenue));
	TestEqual(TEXT("race venue list + blank + Quit"), O.NextVenueList(), TArray<int32>({ 0, 1, 2, 3, 4, 8, 5, 11, -1, -2 }));
	P.VenueUnlocks |= 1u << 11;
	for (int32 I = 0; I < 7; ++I) O.Pad(0, PadDown);
	O.Pad(0, PadDown);
	TestEqual(TEXT("blank row skipped -> Quit"), O.NextVenueSel, 9);
	O.Pad(0, PadUp);
	O.Pad(0, PadCross);
	TestEqual(TEXT("next venue Alaska, mode 2"), H.NextVenueMode * 100 + H.NextVenueTrack, 2 * 100 + 11);
	H.Mode = 1; TestEqual(TEXT("Freeride items"), O.ResultsItems(), TArray<int32>({ 0, 2, 1, 3 }));
	H.Mode = 6; TestEqual(TEXT("Tutorial items"), O.ResultsItems(), TArray<int32>({ 2, 3 }));
	H.Mode = 7; TestEqual(TEXT("TC items"), O.ResultsItems(), TArray<int32>({ 0, 2, 1, 3 }));
	FSSXOverlayManager U;
	U.Init(&P, &C, &TestLoc());
	FSSXRaceUIHostStub H2;
	H2.Phase = 5;
	H2.bPostRaceReady = true;
	H2.Unlocks.Add({ 0, 3, 10 });
	U.SetHost(&H2);
	U.Tick();
	TestTrue(TEXT("Unlocked (slot 13) first"), U.IsActive(OvUnlocked));
	U.Pad(0, PadCross);
	TestTrue(TEXT("X -> results"), U.IsActive(OvRaceResults));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUICountdownTest, "SSX.UI.Countdown", UITestFlags)
bool FSSXUICountdownTest::RunTest(const FString& Parameters)
{
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCheats C;
	FSSXRaceUIHostStub H;
	FSSXOverlayManager O;
	O.Init(&P, &C, &TestLoc());
	O.SetHost(&H);
	H.Phase = 3;
	int32 Bad = 0, Updates[6] = {};
	for (double Clock = -298.0; Clock < 0.0; Clock += 2.0)
	{
		H.Countdown = Clock;
		O.Tick();
		const int32 Expect = Clock <= -240 ? 5 : Clock <= -180 ? 4 : Clock <= -120 ? 3 : Clock <= -60 ? 2 : 1;
		const int32 D = O.GetCountdownDigit();
		Bad += D != Expect;
		if (D >= 1 && D <= 5) { ++Updates[D]; }
		const float S = O.GetCountdownScale();
		Bad += !(S > 1.f - 1e-4f && S < 2.f);
		if (Clock == -162.0) { TestEqual(TEXT("RAM: clock -162 -> digit 3"), D, 3); }
		if (Clock == -148.0) { TestTrue(TEXT("RAM: scale 1.4667 (clock -148)"), FMath::IsNearlyEqual(S, 1.4667f, 1e-3f)); }
	}
	TestEqual(TEXT("digit 5 4 3 2 1 by clock range"), Bad, 0);
	TestTrue(TEXT("30 updates per digit (digit 1: clock -58..-2 = 29, GO at 0)"), Updates[1] == 29 && Updates[2] == 30 && Updates[3] == 30 && Updates[4] == 30 && Updates[5] == 30);
	H.Phase = 4;
	H.Countdown = 0.0;
	int32 Go = 0;
	float LastF = -1.f;
	bool bRising = true;
	for (int32 I = 0; I < 120; ++I)
	{
		O.Tick();
		if (O.IsGoShowing()) { ++Go; bRising &= O.GetCountdownScale() > LastF; LastF = O.GetCountdownScale(); }
	}
	TestEqual(TEXT("GO! for 90 updates"), Go, 90);
	TestTrue(TEXT("GO f rises 0 -> 1"), bRising && FMath::IsNearlyEqual(LastF, 1.f));
	AddInfo(FString::Printf(TEXT("countdown: digits 5/4/3/2/1 for %d/%d/%d/%d/%d updates, GO %d"), Updates[5], Updates[4], Updates[3], Updates[2], Updates[1], Go));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUICameraTest, "SSX.UI.Camera", UITestFlags)
bool FSSXUICameraTest::RunTest(const FString& Parameters)
{
	FSSXFeCamPose Pose;
	Pose.Focal = 25.f;
	TestTrue(TEXT("FOV = 2 atan(8.997951/25)"), FMath::IsNearlyEqual(Pose.FovDeg(), 39.6f, 0.1f));
	TestTrue(TEXT("rendered horizontal FOV = 2 x that (SetProjection half angle)"), FMath::IsNearlyEqual(Pose.HorizontalFovDeg(), 79.2f, 0.1f));
	Pose.Pos = FVector(0, 750, 100);
	Pose.RotDeg = FVector(87.02, 0, -180);
	FVector F, U;
	Pose.Axes(F, U);
	TestTrue(TEXT("looks toward -Y"), F.Y < -0.99);
	TestTrue(TEXT("slightly down"), F.Z < 0.0 && F.Z > -0.1);
	TestTrue(TEXT("up is +Z"), U.Z > 0.99);
	FSSXFeCameraSet Set;
	const FString Json = FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json"));
	if (!Set.Load(Json))
	{
		SSXTest::Skip(*this, TEXT("fe_cameras.json not converted (tools/export_fe_cameras.py); data checks skipped"));
		return true;
	}
	TestEqual(TEXT("62 cameras"), Set.Cameras.Num(), 62);
	const FSSXFeCamera* Cyc = Set.Find(TEXT("charSelect_cycleRight"));
	TestTrue(TEXT("cycleRight present"), Cyc != nullptr);
	if (Cyc)
	{
		TestTrue(TEXT("6.4 s = 12 x 16 frames at 30 fps"), FMath::IsNearlyEqual(Cyc->Duration, 6.4f, 0.01f));
		const FSSXFeCamPose K = Cyc->Sample(16.f / 30.f);
		TestTrue(TEXT("slot 1 camera near Kaori's ring position"), FVector::Dist2D(K.Pos, FVector(-375, 649.5, 0)) < 15.0);
	}
	const FSSXFeCamera* Sel = Set.Find(TEXT("charSelect_selectMac"));
	TestTrue(TEXT("selectMac ends at the notes' pose"), Sel && FVector::Dist(Sel->Sample(Sel->Duration).Pos, FVector(61.42, 676.99, 120.04)) < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFontTest, "SSX.UI.Font", UITestFlags)
bool FSSXUIFontTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FSSXBitmapFont> F = MakeShared<FSSXBitmapFont>();
	FSSXBitmapGlyph A;
	A.Size = FVector2f(8, 10);
	A.Offset = FVector2f(1, 3);
	A.Advance = 9;
	F->Glyphs.Add(TEXT('A'), A);
	FSSXBitmapGlyph B = A;
	B.Size = FVector2f(8, 14);
	B.Offset = FVector2f(1, 2);
	F->Glyphs.Add(TEXT('B'), B);
	F->RecomputeLineHeight();
	TestEqual(TEXT("line height = max(h + yoffset)"), F->LineHeight, 16.f);
	F->ScaleX = SSXFe::MenuScaleX;
	F->ScaleY = SSXFe::MenuScaleY;
	TestTrue(TEXT("width = sum(advance) * scale * scaleX"), FMath::IsNearlyEqual(F->Width(TEXT("AB"), 0.5f), 18.f * 0.5f * 1.8f));
	TestEqual(TEXT("no case folding"), F->Width(TEXT("a"), 1.f), 0.f);
	F->Atlas = nullptr;
	FSSXFeAssets Assets;
	Assets.Menu = F;
	FSSXFeDrawList D(&Assets);
	const FVector2f Ext = D.Text(SSXFe::EFont::Menu, 10.f, 20.f, 1.f, SSXFe::White, TEXT("AB"), ESSXAlign::Left, true, 5);
	TestTrue(TEXT("shadow adds 2 px"), FMath::IsNearlyEqual(Ext.X, 18.f * 1.8f + 2.f) && FMath::IsNearlyEqual(Ext.Y, 16.f * 1.4f + 2.f));
	const FString Base = FPaths::Combine(SSXFrontEnd::FontDir(), TEXT("MENU"));
	UTexture2D* Dummy = NewObject<UTexture2D>(GetTransientPackage());
	TSharedPtr<FSSXBitmapFont> Menu = FPaths::FileExists(Base + TEXT(".json"))
		? FSSXBitmapFont::Load(Base + TEXT(".json"), Dummy, FPaths::Combine(SSXFrontEnd::SfnDir(), TEXT("MENU.SFN"))) : nullptr;
	if (!Menu.IsValid() || !TestLoc().IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("MENU font / strings missing: tab size checks skipped"));
		return true;
	}
	Menu->ScaleX = SSXFe::MenuScaleX;
	Menu->ScaleY = SSXFe::MenuScaleY;
	TestEqual(TEXT("MENU line height 15"), Menu->LineHeight, 15.f);
	struct FTab { int32 Loc; int32 Tw; };
	const FTab Tabs[] = { { 3499, 141 }, { 3495, 290 }, { 3496, 296 }, { 3502, 278 }, { 3500, 294 }, { 3498, 148 }, { 3497, 202 },
		{ 3501, 150 }, { 3505, 186 }, { 181, 186 }, { 182, 191 }, { 3494, 152 }, { 3504, 119 }, { 3493, 87 } };
	int32 Ok = 0;
	for (const FTab& T : Tabs)
	{
		const FString Text = TestLoc().ByIndex(T.Loc);
		const int32 Tw = FMath::RoundToInt(Menu->BoxWidth(Text, 1.f) + SSXFe::ShadowOffset);
		Ok += Tw == T.Tw;
		TestEqual(*FString::Printf(TEXT("tab \"%s\" tw"), *Text), Tw, T.Tw);
	}
	AddInfo(FString::Printf(TEXT("frame tab widths: %d / %d match (th = 17)"), Ok, UE_ARRAY_COUNT(Tabs)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIVenueNameLocTest, "SSX.UI.VenueNameLoc", UITestFlags)

bool FSSXUIVenueNameLocTest::RunTest(const FString& Parameters)
{
	// FUN_0016B650 / 0x37EBF0
	const int32 Want[] = { 3613, 3688, 3576, 3646, 3645, 3573, 3685, 3692, 3689, 3685, 3610, 3693 };
	for (int32 T = 0; T < 12; ++T)
	{
		TestEqual(*FString::Printf(TEXT("track %d"), T), SSXFeTables::TrackNameLoc(T), Want[T]);
	}
	TestEqual(TEXT("track 12 -> default"), SSXFeTables::TrackNameLoc(12), 3613);
	TestEqual(TEXT("track -1 (unsigned) -> default"), SSXFeTables::TrackNameLoc(-1), 3613);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIMartySignTest, "SSX.UI.MartySign", UITestFlags)

bool FSSXUIMartySignTest::RunTest(const FString& Parameters)
{
	// 0x3470F8
	if (!SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json"))) || SSXFeTables::MartySignInstance() == INDEX_NONE)
	{
		SSXTest::Skip(*this, TEXT("fe_tables.json without marty_sign (rerun tools/export_fe_tables.py): skipped"));
		return true;
	}
	const int32 M = SSXFeTables::MartySignInstance();
	TestTrue(TEXT("instance index"), M >= 0);
	for (int32 S = 0; S < SSXFeTables::NumRiders; ++S)
	{
		TestNotEqual(*FString::Printf(TEXT("not sign %d"), S), M, SSXFeTables::RiderSignInstance(S));
		TestNotEqual(*FString::Printf(TEXT("not platform %d"), S), M, SSXFeTables::PlatformInstance(S));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIVenueMedalsTest, "SSX.UI.VenueMedals", UITestFlags)

bool FSSXUIVenueMedalsTest::RunTest(const FString& Parameters)
{
	FSSXProfile P = FSSXProfile::MakeDefault();
	if (P.Slot[0].Char < 0) { P.Slot[0].Char = 0; }
	for (int32 T : SSXFeTables::VenueOrder())
	{
		for (int32 Mode : { 1, 2, 3, 4, 5, 7 })
		{
			TestEqual(*FString::Printf(TEXT("new save: no medal (track %d mode %d)"), T, Mode), P.VenueMedal(Mode, T), int32(SSXProg::NoMedal));
		}
	}
	FSSXCareer& C = *P.SlotCareer(0);
	C.Tracks[0].RaceMedal = SSXProg::Gold;
	C.Tracks[0].ShowoffMedal = SSXProg::NoMedal;
	C.Tracks[1].ShowoffMedal = SSXProg::Bronze;
	C.Tracks[2].RaceMedal = SSXProg::Silver;
	TestEqual(TEXT("WC race: race byte gold"), P.VenueMedal(4, 0), int32(SSXProg::Gold));
	TestEqual(TEXT("WC showoff: showoff byte none"), P.VenueMedal(5, 0), int32(SSXProg::NoMedal));
	TestEqual(TEXT("WC showoff: showoff byte bronze"), P.VenueMedal(5, 1), int32(SSXProg::Bronze));
	TestEqual(TEXT("WC race: race byte silver"), P.VenueMedal(4, 2), int32(SSXProg::Silver));
	TestEqual(TEXT("single event: no medal (FUN_00163E28 returns 3 outside modes 4 / 5)"), P.VenueMedal(2, 0), int32(SSXProg::NoMedal));
	{
		FSSXProfile Empty = P;
		Empty.Slot[0].Char = -1;
		TestEqual(TEXT("empty slot 0: no medal"), Empty.VenueMedal(4, 0), int32(SSXProg::NoMedal));
	}
	C.Tracks[3].RaceMedal = 0xFF;
	TestEqual(TEXT("out-of-range byte: no medal"), P.VenueMedal(4, 3), int32(SSXProg::NoMedal));
	SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
	// FUN_00262D80 / 0x3A8AF0 / 0x347158
	const int32 Expect[][2] = { { 0, 0 }, { 1, 9 }, { 2, 8 }, { 3, 7 }, { 4, 6 }, { 8, 5 }, { 5, 4 }, { 11, 3 }, { 7, 2 }, { 6, 1 } };
	for (const auto& E : Expect)
	{
		TestEqual(*FString::Printf(TEXT("track %d -> mdl_medal_%02d"), E[0], E[1]), SSXFeCams::VenueMedalModel(E[0]), E[1]);
	}
	return true;
}

#endif
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIPauseOptionsTest, "SSX.UI.PauseOptions", UITestFlags)
bool FSSXUIPauseOptionsTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCheats C;
	FSSXRaceUIHostStub H;
	H.Phase = 4;
	H.Song = TEXT("superwoman");
	FSSXOverlayManager O;
	O.Init(&P, &C, &TestLoc());
	O.SetHost(&H);
	O.Pad(0, PadStart);
	TestTrue(TEXT("pause open"), O.IsActive(OvPauseMenu));
	O.Pad(0, PadDown); O.Pad(0, PadDown);
	O.Pad(0, PadCross);
	TestEqual(TEXT("Options page"), O.PauseDepth, 1);
	TestEqual(TEXT("rows"), O.PauseOptionRows(), TArray<int32>({ 483, 484, 481, 489, 486, 490, 488, 487, 3225 }));
	O.Sounds.Reset();
	O.Pad(0, PadDown);
	TestTrue(TEXT("Down: move sound"), O.Sounds.Contains(SSXUISound::ERace::Navigate) && O.OptionsSel == 1);
	O.Pad(0, PadUp);
	const int32 B0 = O.OptionValue(483);
	O.Sounds.Reset();
	O.Pad(0, PadRight);
	TestEqual(TEXT("Right: brightness value +1 (0..50)"), O.OptionValue(483), FMath::Min(50, B0 + 1));
	TestTrue(TEXT("brightness change: accept sound"), O.Sounds.Contains(SSXUISound::ERace::Accept));
	O.Pad(0, PadDown);
	const int32 H0 = O.GetHudDetail();
	O.Pad(0, PadLeft);
	TestEqual(TEXT("HUD detail wraps 0 -> 2"), O.GetHudDetail(), (H0 + 2) % 3);
	TestEqual(TEXT("HUD detail stored in the settings (0x32CA40)"), P.Settings.HudDetail, (H0 + 2) % 3);
	O.Pad(0, PadRight);
	for (int32 I = 0; I < 3; ++I) O.Pad(0, PadDown);
	const float M0 = P.Settings.Master;
	O.Sounds.Reset();
	O.Pad(0, PadLeft);
	TestTrue(TEXT("Left: master volume -1/11"), FMath::IsNearlyEqual(P.Settings.Master, FMath::Max(0.f, M0 - 1.f / 11.f), 1e-4f));
	TestEqual(TEXT("row value trunc(level x 50)"), O.OptionValue(486), FMath::TruncToInt(P.Settings.Master * 50.f + 1e-4f));
	TestTrue(TEXT("volume: preview sound, no UI sound"), O.Sounds.Contains(SSXUISound::ERace::PreviewMaster) && !O.Sounds.Contains(SSXUISound::ERace::Navigate));
	TestTrue(TEXT("audio levels flagged for the audio owner"), O.bAudioLevelsChanged);
	for (int32 I = 0; I < 12; ++I) O.Pad(0, PadLeft);
	O.Sounds.Reset();
	O.Pad(0, PadLeft);
	TestTrue(TEXT("at 0: no preview (value unchanged)"), O.OptionValue(486) == 0 && O.Sounds.Num() == 0);
	TestEqual(TEXT("song row: the playing song"), O.PauseOptionValue(489), FString(TEXT("superwoman")));
	for (int32 F = 0; F < 120; ++F) O.Tick();
	FSSXFeDrawList D;
	O.Draw(D);
	int32 Values = 0;
	for (int32 Row : O.PauseOptionRows())
	{
		const FString V = O.PauseOptionValue(Row);
		const float S = O.PauseOptionSlider(Row);
		const FString Want = !V.IsEmpty() ? V : (S >= 0.f ? FString::FromInt(O.OptionValue(Row)) : FString());
		Values += !Want.IsEmpty() && D.Texts.Contains(Want);
	}
	AddInfo(FString::Printf(TEXT("pause options: %d of %d rows draw a value"), Values, O.PauseOptionRows().Num()));
	const int32 Expect = TestLoc().IsLoaded() ? 9 : 7;
	TestTrue(TEXT("every row shows its value"), Values >= Expect);
	return true;
}
// FUN_0028D668 / FUN_0028DE90
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUICustomBoardLockedTest, "SSX.UI.CustomBoardLocked", UITestFlags)
bool FSSXUICustomBoardLockedTest::RunTest(const FString& Parameters)
{
	FFe F;
	F.Profile.Slot[0].Char = 0;
	F.FE.GoTo(ESSXFeScreen::CustomBoard);
	for (int32 N = 0; N < 200 && !F.FE.IsReady(); ++N) { F.FE.Tick(); }
	FSSXFeState* S = F.FE.TopMut();
	if (!TestTrue(TEXT("on CustomBoard"), S && S->Screen == ESSXFeScreen::CustomBoard)) { return false; }
	const int32 Locked = S->Items.IndexOfByPredicate([](const FSSXFeItem& I) { return !I.bEnabled; });
	const int32 Open = S->Items.IndexOfByPredicate([](const FSSXFeItem& I) { return I.bEnabled; });
	if (!TestTrue(TEXT("a new save has locked and open boards"), Locked != INDEX_NONE && Open != INDEX_NONE)) { return false; }
	const FString StatLabel = TestLoc().ByIndex(37);
	auto Drawn = [&](const FString& Str)
	{
		FSSXFeDrawList D;
		F.FE.Draw(D);
		return D.Texts.Contains(Str);
	};
	const int32 Char = 0;
	S->Sel = Open;
	TestTrue(TEXT("open board: stat rows drawn"), Drawn(StatLabel));
	TestTrue(TEXT("open board: no help line"), F.FE.CurrentHelpLine().IsEmpty());
	S->Sel = Locked;
	TestFalse(TEXT("locked board: stat group hidden"), Drawn(StatLabel));
	TestEqual(TEXT("locked board: lock text (0x3AFB78[board - 2])"), F.FE.CurrentHelpLine(), TestLoc().ByIndex(S->Items[Locked].HelpLoc));
	const int32 TypeLoc = SSXFeTables::BoardTypeLoc(SSXFeTables::Board(Char, Locked).Type);
	if (TypeLoc >= 0)
	{
		TestTrue(TEXT("locked board: class text still drawn"), Drawn(TestLoc().ByIndex(TypeLoc)));
	}
	F.FE.GoTo(ESSXFeScreen::CustomBody);
	for (int32 N = 0; N < 200 && !F.FE.IsReady(); ++N) { F.FE.Tick(); }
	S = F.FE.TopMut();
	if (TestTrue(TEXT("on CustomBody"), S && S->Screen == ESSXFeScreen::CustomBody))
	{
		const int32 OpenO = S->Items.IndexOfByPredicate([](const FSSXFeItem& I) { return I.bEnabled; });
		const int32 LockedO = S->Items.IndexOfByPredicate([](const FSSXFeItem& I) { return !I.bEnabled && I.Id >= 1 && I.Id <= 6; });
		if (OpenO != INDEX_NONE) { S->Sel = OpenO; TestEqual(TEXT("open outfit: LOC 3243"), F.FE.CurrentHelpLine(), TestLoc().ByIndex(3243)); }
		if (LockedO != INDEX_NONE)
		{
			S->Sel = LockedO;
			TestEqual(TEXT("locked outfit: its lock text"), F.FE.CurrentHelpLine(), TestLoc().ByIndex(S->Items[LockedO].HelpLoc));
		}
	}
	return true;
}

// FUN_0028D668 / 0x267C20 / 0x28DD50 / 0x267C48
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUILockedGearPreviewTest, "SSX.UI.LockedGearPreview", UITestFlags)
bool FSSXUILockedGearPreviewTest::RunTest(const FString& Parameters)
{
	for (const ESSXFeScreen Scr : { ESSXFeScreen::CustomBody, ESSXFeScreen::CustomBoard })
	{
		const bool bBody = Scr == ESSXFeScreen::CustomBody;
		FFe F;
		F.Profile.Slot[0].Char = 0;
		F.Profile.Slot[0].Board = 0;
		F.Profile.Slot[0].Outfit = 0;
		F.FE.GoTo(Scr);
		for (int32 N = 0; N < 200 && !F.FE.IsReady(); ++N) { F.FE.Tick(); }
		FSSXFeState* S = F.FE.TopMut();
		if (!TestTrue(TEXT("on the screen"), S && S->Screen == Scr)) { return false; }
		const int32 Locked = S->Items.IndexOfByPredicate([](const FSSXFeItem& I) { return !I.bEnabled; });
		if (!TestTrue(TEXT("a new save has a locked item"), Locked != INDEX_NONE)) { return false; }
		TestEqual(TEXT("all items listed"), S->Items.Num(), bBody ? SSXFeTables::OutfitsPerRider : SSXFeTables::BoardsPerRider);
		for (int32 N = 0; N < 20 && S->Sel != Locked; ++N)
		{
			F.FE.Key(0, ESSXFeKey::Right);
			F.FE.Tick();
			S = F.FE.TopMut();
		}
		if (!TestEqual(TEXT("cursor reaches the locked item"), S->Sel, Locked)) { return false; }
		int32 Board = -1, Outfit = -1;
		F.FE.RiderGear(0, Board, Outfit);
		TestEqual(TEXT("highlighted locked item shown on the rider"), bBody ? Outfit : Board, S->Items[Locked].Id);
		F.FE.Key(0, ESSXFeKey::Cross);
		F.FE.Tick();
		TestEqual(TEXT("X on a locked item is refused"), F.FE.Current(), Scr);
		F.FE.Key(0, ESSXFeKey::Triangle);
		for (int32 N = 0; N < 200 && F.FE.Current() == Scr; ++N) { F.FE.Tick(); }
		F.FE.RiderGear(0, Board, Outfit);
		TestEqual(TEXT("Back restores the entry value"), bBody ? Outfit : Board, 0);
		// FUN_00267B18
		(bBody ? F.Profile.Slot[0].Outfit : F.Profile.Slot[0].Board) = Locked;
		F.FE.RiderGear(0, Board, Outfit);
		TestEqual(TEXT("saved locked item gated to 0 elsewhere"), bBody ? Outfit : Board, 0);
	}
	return true;
}

// FUN_00267B18 / 0x263B30 / 0x163D30 / 0x163D98
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFeRiderSpawnGearTest, "SSX.UI.FeRiderSpawnGear", UITestFlags)
bool FSSXUIFeRiderSpawnGearTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ssx.FE.AllRidersSavedGear"));
	if (!TestNotNull(TEXT("cvar ssx.FE.AllRidersSavedGear"), CVar)) { return false; }
	const int32 OldCVar = CVar->GetInt();
	ON_SCOPE_EXIT { CVar->Set(OldCVar, ECVF_SetByCode); };
	const int32 EddieSlot = FSSXFrontEnd::DefaultCarouselSlot;
	const int32 Eddie = SSXFeTables::CarouselRider(EddieSlot);
	const int32 OtherSlot = (EddieSlot + 3) % SSXFeRiders::NumRiders;
	const int32 Other = SSXFeTables::CarouselRider(OtherSlot);
	auto SetRecord = [](FSSXProfile& P, int32 C, int32 Board, int32 Outfit)
	{
		int32 Rec[5];
		FMemory::Memcpy(Rec, P.RiderConfig[C], sizeof(Rec));
		Rec[3] = Outfit;
		Rec[4] = Board;
		FMemory::Memcpy(P.RiderConfig[C], Rec, sizeof(Rec));
	};
	auto Gear = [](const FFe& F, int32 Slot) { int32 B = -1, O = -1; F.FE.ModelGear(Slot, B, O); return FString::Printf(TEXT("%d/%d"), B, O); };
	auto G = [](int32 B, int32 O) { return FString::Printf(TEXT("%d/%d"), B, O); };
	for (const int32 All : { 0, 1 })
	{
		CVar->Set(All, ECVF_SetByCode);
		const FString Tag = All ? TEXT("[all riders] ") : TEXT("[original] ");
		{
			FFe F;
			F.Cheats.bUnlockAll = true;
			F.Profile.Slot[0].Char = -1;
			SetRecord(F.Profile, Eddie, 2, 3);
			SetRecord(F.Profile, Other, 1, 4);
			F.FE.Init(&F.Profile, &F.Cheats, &TestLoc(), false);
			TestEqual(Tag + TEXT("pad 0's rider (Eddie) in his saved gear at spawn"), Gear(F, EddieSlot), G(2, 3));
			TestEqual(Tag + TEXT("another rider at spawn"), Gear(F, OtherSlot), All ? G(1, 4) : G(0, 0));
			F.Profile.Slot[0].Char = Other;
			F.Profile.Slot[0].Board = 2;
			F.Profile.Slot[0].Outfit = 5;
			for (int32 N = 0; N < 10; ++N) { F.FE.Tick(); }
			TestEqual(Tag + TEXT("no reload outside 0x267B18"), Gear(F, OtherSlot), All ? G(1, 4) : G(0, 0));
			TestEqual(Tag + TEXT("Eddie keeps his gear"), Gear(F, EddieSlot), G(2, 3));
			// 0x1637F8
			F.Profile.CommitSlotConfigs(1);
			F.FE.Init(&F.Profile, &F.Cheats, &TestLoc(), false);
			TestEqual(Tag + TEXT("saved gear shown at the next build"), Gear(F, OtherSlot), G(2, 5));
			TestEqual(Tag + TEXT("Eddie (no longer pad 0's rider) at the next build"), Gear(F, EddieSlot), All ? G(2, 3) : G(0, 0));
		}
		{
			FFe F;
			F.Profile.Slot[0].Char = Other;
			F.Profile.Slot[0].Board = 0;
			int32 LockedO = INDEX_NONE;
			for (int32 O = 1; O < SSXFeTables::OutfitsPerRider && LockedO == INDEX_NONE; ++O)
			{
				if (!F.Profile.IsOutfitUnlocked(Other, O, false) && !F.Profile.IsOutfitUnlocked(Eddie, O, false)) { LockedO = O; }
			}
			if (!TestTrue(TEXT("a new save has a locked outfit"), LockedO != INDEX_NONE)) { return false; }
			F.Profile.Slot[0].Outfit = LockedO;
			SetRecord(F.Profile, Eddie, 0, LockedO);
			F.FE.Init(&F.Profile, &F.Cheats, &TestLoc(), false);
			TestEqual(Tag + TEXT("locked saved outfit spawns as outfit 0"), Gear(F, OtherSlot), G(0, 0));
			TestEqual(Tag + TEXT("locked outfit in another rider's record spawns as 0"), Gear(F, EddieSlot), G(0, 0));
			F.Cheats.bUnlockAll = true;
			F.FE.Init(&F.Profile, &F.Cheats, &TestLoc(), false);
			TestEqual(Tag + TEXT("open gate: the saved outfit at spawn"), Gear(F, OtherSlot), G(0, LockedO));
			TestEqual(Tag + TEXT("open gate: another rider's record"), Gear(F, EddieSlot), All ? G(0, LockedO) : G(0, 0));
		}
	}
	return true;
}

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUISlotConfigTest, "SSX.UI.SlotConfig", UITestFlags)
bool FSSXUISlotConfigTest::RunTest(const FString& Parameters)
{
	// 0x1637F8 / 0x1635B8
	FSSXProfile P = FSSXProfile::MakeDefault();
	P.bSettingsDirty = false;
	auto Rec = [&P](int32 C) { int32 R[5]; FMemory::Memcpy(R, P.RiderConfig[C], sizeof(R)); return TArray<int32>(R, 5); };
	P.Slot[0].Char = 1;
	P.Slot[0].Outfit = 4;
	P.Slot[0].Board = 7;
	TestTrue(TEXT("Kaori's outfit 4 / board 7 committed"), P.CommitSlotConfigs(1));
	TestEqual(TEXT("RAM rider 1 = {1, 0, 1, 4, 7}"), Rec(1), TArray<int32>{ 1, 0, 1, 4, 7 });
	TestTrue(TEXT("save dirty (P+0x3258)"), P.bSettingsDirty);
	P.bSettingsDirty = false;
	TestFalse(TEXT("same record: nothing copied"), P.CommitSlotConfigs(1));
	TestFalse(TEXT("not dirty"), P.bSettingsDirty);
	P.Slot[1].Char = 1;
	P.Slot[1].Outfit = 2;
	TestFalse(TEXT("slot 1 inactive in 1P"), P.CommitSlotConfigs(1));
	P.Slot[0].Char = 2;
	P.Slot[0].Outfit = 3;
	P.Slot[0].Board = 1;
	P.Slot[1].Char = 2;
	P.Slot[1].Outfit = 5;
	P.CommitSlotConfigs(2);
	TestEqual(TEXT("lowest pad wins Zoe's record"), Rec(2), TArray<int32>{ 2, 0, 2, 3, 1 });
	P.Slot[1] = FSSXPlayerSlot();
	P.LoadSlotConfig(1, 1);
	TestTrue(TEXT("slot loads Kaori's outfit 4 / board 7"), P.Slot[1].Outfit == 4 && P.Slot[1].Board == 7);
	P.LoadSlotConfig(1, 5);
	TestTrue(TEXT("a never-configured rider: outfit 0 / board 0"), P.Slot[1].Outfit == 0 && P.Slot[1].Board == 0);
	return true;
}
