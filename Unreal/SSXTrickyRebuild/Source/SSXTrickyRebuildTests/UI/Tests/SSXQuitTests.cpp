// 0x180E00 / 0x186620 / 0x263B30 / 0x161A98
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "Widgets/SOverlay.h"
#include "UI/SSXLoadingScreen.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXMemCard.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"
#include "UI/SSXTrackLoad.h"
#include "Sim/Air/TrickScore.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXFe;
	constexpr EAutomationTestFlags QtFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FQuitRig
	{
		FSSXProfile P = FSSXProfile::MakeDefault();
		FSSXCheats C;
		FSSXRaceUIHostStub H;
		FSSXOverlayManager O;
		explicit FQuitRig(int32 Mode)
		{
			P.Slot[0].Char = 3;
			P.Career[3].BookDone[1] = 0x7;
			P.Slot[0].EventTricks = 0x5;
			P.Session.Heat = 1;
			P.Session.bQuit = true;   // 0x1AB9F8
			H.Mode = Mode;
			H.Phase = 4;
			O.Init(&P, &C, nullptr);
			O.SetHost(&H);
		}
		void Ticks(int32 N) { for (int32 I = 0; I < N; ++I) O.Tick(); }
		void ToResults() { H.Phase = 5; H.bPostRaceReady = true; O.Tick(); }
	};

	struct FSeq
	{
		TArray<uint32> V;
		int32 I = 0;
		uint32 operator()() { return V.IsValidIndex(I) ? V[I++] : 0u; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitDestinationTest, "SSX.UI.Quit.Destination", QtFlags)
bool FSSXQuitDestinationTest::RunTest(const FString& Parameters)
{
	using namespace SSXTrackLoad;
	for (int32 Mode = 1; Mode <= 7; ++Mode)
	{
		const FReturnPick P = PickAfterRace(false, false, true, Mode, false, false, 1);
		TestTrue(FString::Printf(TEXT("mode %d pause quit -> cFELoadFromQuit(0)"), Mode), P.Kind == EKind::FromQuit && !P.bHintAnyRank && !P.bRankVideo);
	}
	TestEqual(TEXT("Freeride results quit -> cFELoadFromQuit"), int32(PickAfterRace(false, false, false, 1, false, false, 1).Kind), int32(EKind::FromQuit));
	TestEqual(TEXT("Tutorial results quit -> cFELoadFromQuit"), int32(PickAfterRace(false, false, false, 6, false, false, 1).Kind), int32(EKind::FromQuit));
	for (int32 Mode : { 2, 3, 4, 5, 7 })
	{
		TestEqual(FString::Printf(TEXT("mode %d results quit -> c1PFELoad"), Mode), int32(PickAfterRace(false, false, false, Mode, false, false, 1).Kind), int32(EKind::FEOneP));
	}
	TestEqual(TEXT("2 players -> c2PFELoad"), int32(PickAfterRace(false, false, false, 2, false, false, 2).Kind), int32(EKind::FETwoP));
	TestEqual(TEXT("pause quit beats the rank-up"), int32(PickAfterRace(false, false, true, 4, true, false, 1).Kind), int32(EKind::FromQuit));
	TestEqual(TEXT("rank-up -> cRiderRankingLoad"), int32(PickAfterRace(false, false, false, 4, true, false, 1).Kind), int32(EKind::RiderRanking));
	TestEqual(TEXT("next venue -> c1PTrackLoad"), int32(PickAfterRace(false, false, false, 2, false, true, 1).Kind), int32(EKind::OneP));
	TestEqual(TEXT("next venue 2P -> c2PTrackLoad"), int32(PickAfterRace(false, false, false, 2, false, true, 2).Kind), int32(EKind::TwoP));
	TestEqual(TEXT("Freeride next venue: the venue flag comes first"), int32(PickAfterRace(false, false, false, 1, false, true, 1).Kind), int32(EKind::OneP));
	const FReturnPick A = PickAfterRace(false, true, false, 2, false, false, 1);
	TestTrue(TEXT("DAT_0032EAA0 -> cFELoadFromQuit(1)"), A.Kind == EKind::FromQuit && A.bHintAnyRank);
	const FReturnPick V = PickAfterRace(true, false, false, 4, true, false, 1);
	TestTrue(TEXT("rank video then cRiderRankingLoad"), V.bRankVideo && V.Kind == EKind::RiderRanking);
	TestEqual(TEXT("after the video, Freeride beats the rank-up"), int32(PickAfterRace(true, false, false, 1, true, false, 1).Kind), int32(EKind::FromQuit));

	FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXCheats Cheats;
	FSSXFrontEnd FE;
	FE.Init(&Profile, &Cheats, nullptr, false);
	TestTrue(TEXT("the FE lands on Title"), FE.Top() && FE.Top()->Screen == ESSXFeScreen::Title && FE.Boot == ESSXFeBoot::FrontEnd);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitAutoAdvanceTest, "SSX.UI.Quit.AutoAdvance", QtFlags)
bool FSSXQuitAutoAdvanceTest::RunTest(const FString& Parameters)
{
	// FUN_00265AF8 / 0x293938 / 0x29A4E0 / 0x29AB48
	struct FRun
	{
		FSSXProfile P = FSSXProfile::MakeDefault();
		FSSXCheats C;
		FSSXFrontEnd FE;
		TArray<ESSXFeScreen> Seen;
		FRun(int32 Flag, int32 Mode, int32 Pads, int32 Humans)
		{
			P.Slot[0].Char = 3;
			FE.Init(&P, &C, nullptr, false, Flag);
			FE.GameMode = Mode;
			FE.NumPlayers = Humans;
			FE.PadsConnected = Pads;
			Seen.Add(FE.Current());
			for (int32 I = 0; I < 12; ++I)
			{
				FE.Tick();
				if (Seen.Last() != FE.Current()) { Seen.Add(FE.Current()); }
			}
		}
		bool HasHook(const TCHAR* H) const { return FE.SoundHooks.Contains(FString(H)); }
	};
	using S = ESSXFeScreen;
	{
		FRun R(2, 2, 1, 1);
		TestTrue(TEXT("exit 2 (quit): Title, no auto-advance, no fade"), R.Seen.Num() == 1 && R.FE.Current() == S::Title && !R.FE.IsAutoAdvance() && !R.FE.Fader.bActive);
	}
	{
		FRun R(1, 2, 1, 1);
		TestTrue(TEXT("flag 1: no auto-advance"), R.Seen.Num() == 1 && R.FE.Current() == S::Title);
	}
	{
		FSSXProfile P0 = FSSXProfile::MakeDefault();
		FSSXCheats C0;
		FSSXFrontEnd F0;
		F0.Init(&P0, &C0, nullptr, false, 0x10);
		TestTrue(TEXT("0x10: 1 s black fade (0, 1, 0.5) at the FE start"), F0.Fader.bActive && F0.Fader.Up == 0.f && F0.Fader.Hold == 1.f && F0.Fader.Down == 0.5f);
		TestTrue(TEXT("0x10: Title item 0, phase 5 (vfunc_08 built ModeSelect)"), F0.Top()->Sel == 0 && F0.Top()->Phase == 5);
		FRun R(0x10, 2, 1, 1);
		TestTrue(TEXT("0x10 SE: Title -> ModeSelect -> CharacterSelect -> CharacterAccept, one screen per update"),
			R.Seen == TArray<S>({ S::Title, S::ModeSelect, S::CharacterSelect, S::CharacterAccept }) && R.FE.Depth() == 4);
		TestTrue(TEXT("ModeSelect: item 1 (mode 2), fe+0xF0 0"), R.FE.Group == 0 && R.FE.NumPlayers == 1);
		const int32 MacSlot = SSXFeTables::CarouselSlot(3);
		TestTrue(TEXT("CharacterSelect: the profile rider's slot, select<Name> played (mode 9)"), R.FE.CarouselSlot[0] == MacSlot && R.FE.CamMode == 9);
		TestTrue(TEXT("CharacterAccept turned 0x10 into 2: the auto-advance ends"), R.FE.ExitFlag == 2 && !R.FE.IsAutoAdvance());
		TestTrue(TEXT("no vfunc_21: no onTitleOpen, no Title / ModeSelect speech"), !R.FE.SceneEvents.Contains(TEXT("onTitleOpen")) && !R.HasHook(TEXT("speech0")) && !R.HasHook(TEXT("speech3")));
	}
	{
		FRun R(0x30 & ~0x20, 4, 1, 1);
		TestTrue(TEXT("0x30 WC: Title -> ModeSelect (item 2) -> CharacterSelect -> UserName -> CharacterAccept"),
			R.Seen == TArray<S>({ S::Title, S::ModeSelect, S::CharacterSelect, S::UserName, S::CharacterAccept }) && R.FE.Group == 2 && R.FE.ExitFlag == 2);
	}
	{
		FRun R(0x10, 3, 2, 2);
		TestTrue(TEXT("2P SE: ... -> NumPlayers (2 players) -> CharacterSelect player 0 -> CharacterAccept"),
			R.Seen == TArray<S>({ S::Title, S::ModeSelect, S::NumPlayers, S::CharacterSelect, S::CharacterAccept }) && R.FE.NumPlayers == 2
			&& R.FE.Top()->Player == 0);
	}
	{
		FRun R(4, 1, 1, 1);
		TestTrue(TEXT("mode 1: ModeSelect item 0 -> EventSel, stops there"), R.Seen == TArray<S>({ S::Title, S::ModeSelect, S::EventSel }) && R.FE.Group == 1
			&& R.FE.IsAutoAdvance());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitPauseTest, "SSX.UI.Quit.Pause", QtFlags)
bool FSSXQuitPauseTest::RunTest(const FString& Parameters)
{
	{
		FQuitRig R(4);
		TestFalse(TEXT("race start clears DAT_0032EA94"), R.P.Session.bQuit);
		R.O.Pad(0, PadStart);
		TestTrue(TEXT("paused"), R.O.IsActive(OvPauseMenu) && R.H.bPaused);
		R.O.Pad(0, PadUp); R.O.Pad(0, PadUp); R.O.Pad(0, PadCross);
		TestTrue(TEXT("quit: 326 with X 470 / Triangle 392, no Circle"), R.O.Dialog.bActive && R.O.Dialog.Line1 == 326
			&& R.O.Dialog.LocX == 470 && R.O.Dialog.LocTriangle == 392 && R.O.Dialog.LocCircle < 0);
		R.O.Pad(0, PadCircle);
		TestTrue(TEXT("Circle does nothing on the yes/no box"), R.O.Dialog.bActive && R.H.ExitCode < 0);
		R.O.Pad(0, PadTriangle);
		TestTrue(TEXT("Triangle cancels back to the menu"), !R.O.Dialog.bActive && R.O.IsActive(OvPauseMenu) && R.H.ExitCode < 0);
		R.O.Pad(0, PadCross);
		R.O.Pad(0, PadCross);
		TestEqual(TEXT("X: exit code 2"), R.H.ExitCode, 2);
		TestTrue(TEXT("DAT_0032EA94 = 1"), R.P.Session.bQuit);
		TestEqual(TEXT("0x161840: heat back to 0"), R.P.Session.Heat, 0);
		TestTrue(TEXT("0x165400: this event's tricks leave the book"), R.P.Career[3].BookDone[1] == 0x2 && R.P.Slot[0].EventTricks == 0);
		TestTrue(TEXT("pause closed without resuming (world+0x54)"), !R.O.IsActive(OvPauseMenu) && R.H.bPaused);
		TestEqual(TEXT("loader: cFELoadFromQuit"), int32(SSXTrackLoad::PickAfterRace(false, false, R.P.Session.bQuit, 4, false, false, 1).Kind),
			int32(SSXTrackLoad::EKind::FromQuit));
	}
	{
		// 0x165400
		FQuitRig R(4);
		R.O.Pad(0, PadStart); R.O.Pad(0, PadDown); R.O.Pad(0, PadCross);
		TestTrue(TEXT("restart asks 326"), R.O.Dialog.bActive && R.O.Dialog.Line1 == 326);
		R.O.Pad(0, PadCross);
		TestEqual(TEXT("restart requested"), R.H.Restarts, 1);
		TestTrue(TEXT("restart drops the event tricks"), R.P.Career[3].BookDone[1] == 0x2 && R.P.Slot[0].EventTricks == 0);
		TestTrue(TEXT("restart keeps the heat, no quit flag, resumes"), R.P.Session.Heat == 1 && !R.P.Session.bQuit && !R.H.bPaused);
	}
	{
		FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardQuit"));
		FQuitRig R(2);
		R.P.bSettingsDirty = true;
		R.O.Pad(0, PadStart); R.O.Pad(0, PadUp); R.O.Pad(0, PadUp); R.O.Pad(0, PadCross);
		TestTrue(TEXT("dirty: 444 X 470 / Circle 392 / Triangle 340"), R.O.Dialog.Line1 == 444 && R.O.Dialog.LocX == 470
			&& R.O.Dialog.LocCircle == 392 && R.O.Dialog.LocTriangle == 340);
		R.O.Pad(0, PadTriangle);
		TestTrue(TEXT("Triangle: back"), !R.O.Dialog.bActive && R.H.ExitCode < 0 && R.P.bSettingsDirty);
		R.O.Pad(0, PadCross);
		R.O.Pad(0, PadCircle);
		TestEqual(TEXT("Circle: quit without saving"), R.H.ExitCode, 2);
		TestTrue(TEXT("0x161A98 clears the dirty flag"), !R.P.bSettingsDirty && R.P.Session.bQuit);

		FQuitRig S(2);
		S.P.bSettingsDirty = true;
		S.O.Pad(0, PadStart); S.O.Pad(0, PadUp); S.O.Pad(0, PadUp); S.O.Pad(0, PadCross);
		S.O.Pad(0, PadCross);
		TestTrue(TEXT("X: saving first (no exit yet), tricks dropped before the save"), S.H.ExitCode < 0 && S.P.Slot[0].EventTricks == 0);
		S.Ticks(130);
		TestEqual(TEXT("after the save: exit code 2"), S.H.ExitCode, 2);
		TestTrue(TEXT("save-then-quit sets the quit flag, heat 0"), S.P.Session.bQuit && S.P.Session.Heat == 0 && S.H.bPaused);
		IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
		FSSXMemCard::RootOverride().Reset();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitPostRaceTest, "SSX.UI.Quit.PostRace", QtFlags)
bool FSSXQuitPostRaceTest::RunTest(const FString& Parameters)
{
	{
		FQuitRig R(2);
		R.ToResults();
		TestTrue(TEXT("results"), R.O.IsActive(OvRaceResults));
		for (int32 I = 0; I < 4; ++I) R.O.Pad(0, PadDown);
		TestEqual(TEXT("quit row"), R.O.ResultsItems()[R.O.ResultsSel], 3);
		R.O.Pad(0, PadCross);
		TestTrue(TEXT("quit box 326, selection held at 0"), R.O.Dialog.bActive && R.O.Dialog.Line1 == 326 && R.O.ResultsSel == 0);
		R.O.Pad(0, PadTriangle);
		TestTrue(TEXT("cancel: results again at row 0"), !R.O.Dialog.bActive && R.O.ResultsSel == 0 && R.H.ExitCode < 0);
		for (int32 I = 0; I < 4; ++I) R.O.Pad(0, PadDown);
		R.O.Pad(0, PadCross);
		R.O.Pad(0, PadCross);
		TestEqual(TEXT("results quit: exit code 2"), R.H.ExitCode, 2);
		TestTrue(TEXT("no DAT_0032EA94 from the results"), !R.P.Session.bQuit);
		TestTrue(TEXT("0x161840: heat 0, tricks dropped"), R.P.Session.Heat == 0 && R.P.Slot[0].EventTricks == 0);
		TestEqual(TEXT("SE results quit -> c1PFELoad"), int32(SSXTrackLoad::PickAfterRace(false, false, R.P.Session.bQuit, 2, false, false, 1).Kind),
			int32(SSXTrackLoad::EKind::FEOneP));
	}
	{
		// 0x161A98 / 0x113BB0 / 0x165400
		FQuitRig R(2);
		R.P.bSettingsDirty = false;
		R.ToResults();
		R.O.Pad(0, PadCross);
		TestTrue(TEXT("slot 17"), R.O.IsActive(OvSelectNextEvent));
		for (int32 I = 0; I < 4; ++I) R.O.Pad(0, PadDown);
		R.O.Pad(0, PadCross);
		TestTrue(TEXT("quit box"), R.O.Dialog.bActive && R.O.Dialog.Line1 == 326);
		R.O.Pad(0, PadCross);
		TestEqual(TEXT("next event quit: exit code 2"), R.H.ExitCode, 2);
		TestTrue(TEXT("heat 0, the event tricks stay, no quit flag"), R.P.Session.Heat == 0 && R.P.Slot[0].EventTricks == 0x5 && !R.P.Session.bQuit);
	}
	{
		FQuitRig R(1);
		R.ToResults();
		R.O.Pad(0, PadCross);
		TestTrue(TEXT("slot 16"), R.O.IsActive(OvSelectNextVenue));
		R.O.Pad(0, PadUp);
		TestEqual(TEXT("Quit row"), R.O.NextVenueList()[R.O.NextVenueSel], -2);
		R.O.Pad(0, PadCross);
		R.O.Pad(0, PadCross);
		TestEqual(TEXT("next venue quit: exit code 2"), R.H.ExitCode, 2);
		TestEqual(TEXT("Freeride quit -> cFELoadFromQuit"), int32(SSXTrackLoad::PickAfterRace(false, false, R.P.Session.bQuit, 1, false, false, 1).Kind),
			int32(SSXTrackLoad::EKind::FromQuit));
	}
	{
		// 0x161A98
		FQuitRig R(2);
		R.P.bSettingsDirty = true;
		R.ToResults();
		R.O.Pad(0, PadCross);
		for (int32 I = 0; I < 3; ++I) R.O.Pad(0, PadDown);
		R.O.Pad(0, PadCross);
		TestTrue(TEXT("setup character asks 361"), R.O.Dialog.bActive && R.O.Dialog.Line1 == 361);
		R.O.Pad(0, PadCross);
		TestTrue(TEXT("exit code 0x10, dirty cleared, heat 0"), R.H.ExitCode == 0x10 && !R.P.bSettingsDirty && R.P.Session.Heat == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitHintTest, "SSX.UI.Quit.Hint", QtFlags)
bool FSSXQuitHintTest::RunTest(const FString& Parameters)
{
	using namespace SSXTrackLoad;
	{
		FSeq S; S.V = { 17, 17 + 19, 5 };
		TestEqual(TEXT("17 is drawn again"), PickHint(false, 100, S), 5);
		TestEqual(TEXT("three draws"), S.I, 3);
	}
	{
		FSeq S; S.V = { 0, 1, 2, 5, 18, 6 };
		TestEqual(TEXT("ranks 8 / 9 keep only 3, 4, 6, 15"), PickHint(false, 19, S), 6);
		FSeq T; T.V = { 15 };
		TestEqual(TEXT("15 allowed"), PickHint(false, 0, T), 15);
		FSeq U; U.V = { 17, 2 };
		TestEqual(TEXT("ctor argument 1: any rank"), PickHint(true, 0, U), 2);
		FSeq W; W.V = { 2 };
		TestEqual(TEXT("20 rank points: any hint"), PickHint(false, 20, W), 2);
	}
	{
		uint32 X = 12345u;
		auto Lcg = [&X]() { X = X * 1103515245u + 12345u; return X; };
		TSet<int32> Seen;
		for (int32 I = 0; I < 2000; ++I) { Seen.Add(PickHint(true, 300, Lcg)); }
		TestTrue(TEXT("18 hints, never 17"), Seen.Num() == 18 && !Seen.Contains(17));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitLoaderTest, "SSX.UI.Quit.Loader", QtFlags)
bool FSSXQuitLoaderTest::RunTest(const FString& Parameters)
{
	using EPhase = FSSXTrackLoader::EPhase;
	using namespace SSXTrackLoad;
	FSSXLocStrings Loc;
	const bool bLoc = Loc.Load(SSXFrontEnd::LangDir());
	FSSXProfile Profile = FSSXProfile::MakeDefault();
	Profile.Slot[0].Char = 3;
	FSSXFeLaunch QL;
	QL.Slots[0].Char = 3;
	FSeq Draw; Draw.V = { 4 };
	FSSXTrackLoader T;
	T.BeginReturn(EKind::FromQuit, false, QL, &Profile, bLoc ? &Loc : nullptr, Draw);
	TestTrue(TEXT("ctor: Prepare, opaque, hint 4 (new save = rank 9)"), T.Phase == EPhase::Prepare && T.Fade == 1.f && T.Page.Hint == 4);
	TestTrue(TEXT("queue: 3 typed channels, steps 1..3"), T.Seq.Num == 3 && T.Seq.MaxStep == 3);
	if (bLoc)
	{
		TestEqual(TEXT("title LOC 0xE30"), T.Page.QuitTitle, Loc.ByIndex(0xE30));
		TestEqual(TEXT("hint title LOC 0xCFF + 8"), T.Page.HintTitle, Loc.ByIndex(0xCFF + 8));
		TestEqual(TEXT("hint body LOC 0xD00 + 8"), T.Page.HintBody, Loc.ByIndex(0xD00 + 8));
	}
	T.Start();
	TestTrue(TEXT("start: Run, bank-13 #0"), T.Phase == EPhase::Run && T.MusicStarts == 1);
	TestEqual(TEXT("3 s stop fade"), T.MusicStopFade(), 3.f);
	TestFalse(TEXT("skip refused before the FE is loaded"), T.RequestSkip(false));
	int32 Frames = 0;
	while (!T.bIntroDone() && Frames < 20000) { T.Step(false, 3); ++Frames; }
	const int32 Expect = bLoc ? 2 * T.Page.QuitTitle.Len() + T.Page.HintTitle.Len() + T.Page.HintBody.Len() : 0;
	AddInfo(FString::Printf(TEXT("quit loader intro: %d frames (texts %d)"), Frames, Expect));
	TestTrue(TEXT("intro = the typed counters (2 frames per title character, 1 per hint character)"), FMath::Abs(Frames - Expect) <= 6);
	for (int32 N = 0; N < 60; ++N) T.Step(false, 7);
	TestEqual(TEXT("PC: stays up until loaded"), int32(T.Phase), int32(EPhase::Run));
	TestEqual(TEXT("voice on up to app+8 = 7"), T.MusicStops, 0);
	T.Step(false, 8);
	TestEqual(TEXT("app+8 > 7 stops the voice"), T.MusicStops, 1);
	T.Step(true, 10);
	TestEqual(TEXT("loaded + intro: Finish"), int32(T.Phase), int32(EPhase::Finish));
	int32 N = 0;
	while (T.Phase == EPhase::Finish && N < 100) { T.Step(true, 10); ++N; }
	TestTrue(TEXT("Done once black again"), T.Phase == EPhase::Done && N >= 29 && N <= 31);

	FSeq D2; D2.V = { 4 };
	FSSXTrackLoader U;
	U.BeginReturn(EKind::FromQuit, false, QL, &Profile, bLoc ? &Loc : nullptr, D2);
	U.Start();
	U.Step(true, 10);
	if (bLoc)
	{
		TestEqual(TEXT("loaded but the intro runs"), int32(U.Phase), int32(EPhase::Run));
		TestTrue(TEXT("skip accepted once loaded"), U.RequestSkip(true));
		U.Step(true, 10);
		TestTrue(TEXT("skip: Finish"), U.Phase == EPhase::Finish && !U.bIntroDone());
	}

	FSeq D3;
	FSSXTrackLoader B;
	FSSXFeLaunch L2 = QL;
	L2.Players = 2;
	L2.GameMode = 2;
	B.BeginReturn(EKind::FETwoP, false, L2, &Profile, nullptr, D3);
	B.Start();
	TestTrue(TEXT("c2PFELoad: bank-13 voice, no intro, no rand"), B.MusicStarts == 1 && B.bIntroDone() && D3.I == 0 && B.Seq.Num == 0);
	for (int32 K = 0; K < 30; ++K) B.Step(false, 5);
	TestEqual(TEXT("c2PFELoad waits for the load"), int32(B.Phase), int32(EPhase::Run));
	B.Step(true, 10);
	TestEqual(TEXT("c2PFELoad closes once loaded"), int32(B.Phase), int32(EPhase::Finish));

	if (!FPaths::FileExists(JsonPath()) || !FPaths::FileExists(FPaths::Combine(SSXFrontEnd::FontDir(), TEXT("MENU.json"))) || !bLoc)
	{
		SSXTest::Skip(*this, TEXT("loading screen art / fonts / LOC not converted; quit page draw checks skipped"));
		return true;
	}
	static UTexture2D* Dummy = nullptr;
	if (!Dummy) { Dummy = NewObject<UTexture2D>(GetTransientPackage()); Dummy->AddToRoot(); }
	FSSXFeAssets Fe;
	for (const TCHAR* Name : { TEXT("TITLE"), TEXT("MENU") })
	{
		const FString Base = FPaths::Combine(SSXFrontEnd::FontDir(), Name);
		(FString(Name) == TEXT("TITLE") ? Fe.Title : Fe.Menu) = FSSXBitmapFont::Load(Base + TEXT(".json"), Dummy,
			FPaths::Combine(SSXFrontEnd::SfnDir(), FString(Name) + TEXT(".SFN")));
	}
	FSSXTrackLoadAssets Art;
	TestTrue(TEXT("loader art"), Art.Load(Fe, [](const FString&) { return Dummy; }));
	FSeq D4; D4.V = { 0 };
	FSSXTrackLoader P;
	P.BeginReturn(EKind::FromQuit, true, QL, &Profile, &Loc, D4);
	P.Start();
	for (int32 K = 0; K < 80; ++K) P.Step(false, 3);
	FSSXFeDrawList D(&Art.Assets);
	P.Draw(D);
	const FSSXFeDrawCmd* Pic = nullptr;
	int32 Pieces = 0, Notches = 0;
	for (const FSSXFeDrawCmd& C : D.Cmds)
	{
		if (C.SpriteId == 16) Pic = &C;
		Pieces += C.SpriteId >= 6 && C.SpriteId < 16;
		Notches += C.SpriteId == 24;
	}
	TestTrue(TEXT("bands and 10 progress pieces"), Notches == 8 && Pieces == 10);
	if (TestNotNull(TEXT("record 16 picture"), Pic))
	{
		const FSSXSprite* R16 = Art.Sprites.Find(16);
		TestTrue(TEXT("record 16 x 1.15 at x 28, centred on y 235"), R16 && FMath::IsNearlyEqual(Pic->Size.X, R16->W * 1.15f, 0.01f)
			&& FMath::IsNearlyEqual(Pic->Pos.X, 28.f, 0.01f) && FMath::IsNearlyEqual(Pic->Pos.Y + Pic->Size.Y * 0.5f, 235.f, 0.01f));
	}
	auto Find = [&D](const FString& S) -> const FSSXFeTextRec* { for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == S) return &R; } return nullptr; };
	if (const FSSXFeTextRec* Title = Find(P.Page.QuitTitle); TestNotNull(TEXT("title"), Title))
	{
		TestEqual(TEXT("title at (30, 27)"), Title->Pos, FVector2f(30.f, 27.f));
	}
	if (const FSSXFeTextRec* Ht = Find(P.Page.HintTitle); TestNotNull(TEXT("hint title"), Ht))
	{
		const float W = FMath::TruncToFloat(D.TextInkWidth(EFont::Menu, P.Page.HintTitle, 1.2f));
		TestEqual(TEXT("hint title x = min(320, 620 - w), y 127"), Ht->Pos, FVector2f(FMath::Min(320.f, 620.f - W), 127.f));
	}
	const TArray<FString> Lines = WrapBox(D, EFont::Menu, P.Page.HintBody, 1.07f, 280.f);
	if (Lines.Num() > 0)
	{
		if (const FSSXFeTextRec* L0 = Find(Lines[0]); TestNotNull(TEXT("body line 1"), L0))
		{
			TestEqual(TEXT("body at (320, 205)"), L0->Pos, FVector2f(320.f, 205.f));
		}
		for (const FString& L : Lines)
		{
			TestTrue(TEXT("body lines fit 280"), FMath::TruncToFloat(D.TextInkWidth(EFont::Menu, L, 1.07f)) <= 280.f || !L.Contains(TEXT(" ")));
		}
	}
	return true;
}

namespace
{
	bool LoadQuitArt(FSSXTrackLoadAssets& Art)
	{
		if (!FPaths::FileExists(SSXTrackLoad::JsonPath()) || !FPaths::FileExists(FPaths::Combine(SSXFrontEnd::FontDir(), TEXT("MENU.json"))))
		{
			return false;
		}
		static UTexture2D* Dummy = nullptr;
		if (!Dummy) { Dummy = NewObject<UTexture2D>(GetTransientPackage()); Dummy->AddToRoot(); }
		FSSXFeAssets Fe;
		for (const TCHAR* Name : { TEXT("TITLE"), TEXT("MENU") })
		{
			const FString Base = FPaths::Combine(SSXFrontEnd::FontDir(), Name);
			(FString(Name) == TEXT("TITLE") ? Fe.Title : Fe.Menu) = FSSXBitmapFont::Load(Base + TEXT(".json"), Dummy,
				FPaths::Combine(SSXFrontEnd::SfnDir(), FString(Name) + TEXT(".SFN")));
		}
		return Art.Load(Fe, [](const FString&) { return Dummy; });
	}

	const FSSXFeTextRec* FindText(const FSSXFeDrawList& D, const FString& S)
	{
		for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == S) return &R; }
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitFePagesTest, "SSX.UI.Quit.FePages", QtFlags)
bool FSSXQuitFePagesTest::RunTest(const FString& Parameters)
{
	using EPhase = FSSXTrackLoader::EPhase;
	using namespace SSXTrackLoad;
	FSSXLocStrings Loc;
	const bool bLoc = Loc.Load(SSXFrontEnd::LangDir());
	const FSSXLocStrings* Lp = bLoc ? &Loc : nullptr;
	// FUN_0024A188 / FUN_0024BBD0 / FUN_00243B88
	TestEqual(TEXT("Freeride summary 0xE7D"), FeSummaryLoc(1, false), 0xE7D);
	TestTrue(TEXT("Single Event summary 0xE7E (2 / 3 / 7)"), FeSummaryLoc(2, false) == 0xE7E && FeSummaryLoc(3, true) == 0xE7E && FeSummaryLoc(7, false) == 0xE7E);
	TestTrue(TEXT("World Circuit summary: c1PFELoad 0xE7F, c2PFELoad none"), FeSummaryLoc(4, false) == 0xE7F && FeSummaryLoc(5, true) == -1);
	TestEqual(TEXT("time 2.5 s"), StatTime(2.5f, nullptr), FString(TEXT("0:02.50")));
	TestEqual(TEXT("time 75.25 s"), StatTime(75.25f, nullptr), FString(TEXT("1:15.25")));
	TestEqual(TEXT("time >= 3600 s"), StatTime(4000.f, nullptr), FString(TEXT("59:59.99")));
	if (bLoc)
	{
		TestEqual(TEXT("time 500 s = 50000 cs -> DNF (LOC 0x15D)"), StatTime(500.f, Lp), Loc.ByIndex(0x15D));
		TestEqual(TEXT("negative -> N/A (LOC 0x18C)"), StatTime(-1.f, Lp), Loc.ByIndex(0x18C));
	}

	// 0x156148 / 0x1569B0 / 0x157008
	SSXAir::FTrickScore Sc;
	auto Grab = [&Sc](int32 Anim, int32 Frames) { Sc.GrabReachesHold(Anim); for (int32 I = 0; I < Frames; ++I) { Sc.TickHold(); } Sc.FinishTrick(Anim, 0.0); };
	Grab(0x27A, 60);
	Grab(0x282, 30);
	TestTrue(TEXT("grab end: +0x50 1.5 s, +0x54 1.0 s, +0xD4 not yet"), FMath::IsNearlyEqual(Sc.HoldSecondsTotal, 1.5, 1e-5) && FMath::IsNearlyEqual(Sc.HoldSecondsBest, 1.0, 1e-5) && Sc.GrabSecondsBest == 0.0);
	Sc.Land(false, 0);
	TestTrue(TEXT("landing: +0xD4 1.0 s, +0xC8 1.5 s, +0x50 / +0x54 zeroed"), FMath::IsNearlyEqual(Sc.GrabSecondsBest, 1.0, 1e-5) && FMath::IsNearlyEqual(Sc.HoldSecondsRace, 1.5, 1e-5)
		&& Sc.HoldSecondsTotal == 0.0 && Sc.HoldSecondsBest == 0.0);
	Grab(0x27A, 150);
	Sc.Bail();
	TestTrue(TEXT("bailed 2.5 s grab: +0xD4 stays 1.0 s"), FMath::IsNearlyEqual(Sc.GrabSecondsBest, 1.0, 1e-5) && Sc.HoldSecondsBest == 0.0);
	Grab(0x27A, 135);
	Sc.Land(false, 0);
	TestTrue(TEXT("landed 2.25 s grab: +0xD4 2.25 s"), FMath::IsNearlyEqual(Sc.GrabSecondsBest, 2.25, 1e-5));
	Grab(0x27A, 200);
	Sc.Land(false, 0, false, true);
	TestTrue(TEXT("after the finish: +0xD4 unchanged"), FMath::IsNearlyEqual(Sc.GrabSecondsBest, 2.25, 1e-5) && FMath::IsNearlyEqual(Sc.HoldSecondsRace, 1.5 + 2.25, 1e-5));

	FSSXProfile Profile = FSSXProfile::MakeDefault();
	Profile.Session.RaceStats[0].Knockdowns = 2;
	Profile.Session.RaceStats[0].Wipeouts = 1;
	Profile.Session.RaceStats[0].Tricks = 17;
	Profile.Session.RaceStats[0].Ubers = 3;
	Profile.Session.RaceStats[0].AirBest = 3.25f;
	Profile.Session.RaceStats[0].GrabBest = float(Sc.GrabSecondsBest);
	Profile.Session.RaceStats[0].RailBest = 1.5f;
	FSSXFeLaunch L;
	L.GameMode = 2;
	L.Track = 2;
	L.Slots[0].Char = 3;
	FSeq R;
	FSSXTrackLoader T;
	T.BeginReturn(EKind::FEOneP, false, L, &Profile, Lp, R);
	TestTrue(TEXT("ctor: Prepare, no rand"), T.Phase == EPhase::Prepare && R.I == 0);
	TestTrue(TEXT("queue: 14 channels, steps 1..13"), T.Seq.Num == 14 && T.Seq.MaxStep == 13);
	const FSSXTrackLoadPage& P = T.Page;
	TestEqual(TEXT("row 0 rider name"), P.FeValues[0][0], FString(SSXFeTables::RiderName(3)));
	TestTrue(TEXT("stat rows %d"), P.FeValues[0][3] == TEXT("2") && P.FeValues[0][4] == TEXT("1") && P.FeValues[0][5] == TEXT("17") && P.FeValues[0][6] == TEXT("3"));
	TestTrue(TEXT("time rows"), P.FeValues[0][7] == TEXT("0:03.25") && P.FeValues[0][8] == TEXT("0:02.25") && P.FeValues[0][9] == TEXT("0:01.50"));
	TestEqual(TEXT("chapter 1 on a new save"), P.FeValues[0][1], FString(TEXT("1")));
	if (bLoc)
	{
		TestEqual(TEXT("title LOC 0xE7E"), P.FeTitle, Loc.ByIndex(0xE7E));
		TestEqual(TEXT("sub-title LOC 0xE2C"), P.FeSubTitle, Loc.ByIndex(0xE2C));
		TestTrue(TEXT("labels rider / trick chapter / ranking / .. / longest rail"), P.FeLabels[0] == Loc.ByIndex(0x121) && P.FeLabels[1] == Loc.ByIndex(0x123)
			&& P.FeLabels[2] == Loc.ByIndex(0x120) && P.FeLabels[3] == Loc.ByIndex(0x125) && P.FeLabels[9] == Loc.ByIndex(299));
		TestEqual(TEXT("rank LOC 23 + 9 (Newbie)"), P.FeValues[0][2], Loc.ByIndex(32));
	}
	T.Start();
	TestTrue(TEXT("start: Run, bank-13 #0, 3 s stop"), T.Phase == EPhase::Run && T.MusicStarts == 1 && T.MusicStopFade() == 3.f);
	int32 Frames = 0;
	int32 Expect = P.FeTitle.Len() + P.FeSubTitle.Len() + P.FeVenue.Len();
	for (int32 Rw = 0; Rw < FSSXTrackLoadPage::FeRows; ++Rw) { Expect += P.FeValues[0][Rw].Len() + (Rw == 9 ? 0x3C : 8); }
	while (!T.bIntroDone() && Frames < 20000) { T.Step(false, 3); ++Frames; }
	AddInfo(FString::Printf(TEXT("c1PFELoad intro: %d frames (expected about %d)"), Frames, Expect));
	TestTrue(TEXT("intro = one frame per queued character"), FMath::Abs(Frames - Expect) <= 20);
	TestTrue(TEXT("typing sounds (FUN_0019E460 rows)"), T.TypeSounds > 0);
	TestEqual(TEXT("PC: stays up until loaded"), int32(T.Phase), int32(EPhase::Run));
	T.Step(false, 8);
	TestEqual(TEXT("app+8 > 7 stops the voice"), T.MusicStops, 1);
	T.Step(true, 10);
	TestEqual(TEXT("loaded + intro: Finish"), int32(T.Phase), int32(EPhase::Finish));

	FSSXFeLaunch L2 = L;
	L2.Players = 2;
	L2.Slots[1].Char = 5;
	Profile.Session.RaceStats[1].Tricks = 9;
	FSSXTrackLoader T2;
	T2.BeginReturn(EKind::FETwoP, false, L2, &Profile, Lp, R);
	TestTrue(TEXT("c2PFELoad: player 2's rider and stats"), T2.Page.FeValues[1][0] == SSXFeTables::RiderName(5) && T2.Page.FeValues[1][5] == TEXT("9"));
	TestTrue(TEXT("c2PFELoad: both rider pictures"), T2.Page.FePictures[0] == RiderPicture(3) && T2.Page.FePictures[1] == RiderPicture(5));

	FSSXTrackLoadAssets Art;
	if (!bLoc || !LoadQuitArt(Art))
	{
		SSXTest::Skip(*this, TEXT("loading screen art / fonts / LOC not converted; FE page draw checks skipped"));
		return true;
	}
	{
		FSSXFeDrawList D(&Art.Assets);
		T.Draw(D);
		bool bRec16 = false;
		for (const FSSXFeDrawCmd& C : D.Cmds) { bRec16 |= C.SpriteId == 16; }
		TestTrue(TEXT("c1PFELoad page: record 16"), bRec16);
		if (const FSSXFeTextRec* Ti = FindText(D, P.FeTitle); TestNotNull(TEXT("summary title"), Ti))
		{
			TestEqual(TEXT("summary title at (30, 30)"), Ti->Pos, FVector2f(30.f, 30.f));
		}
		TestNull(TEXT("venue / sub-title not drawn"), FindText(D, P.FeVenue));
		if (const FSSXFeTextRec* Kd = FindText(D, P.FeLabels[3]); TestNotNull(TEXT("knockdowns label"), Kd))
		{
			const float W = FMath::TruncToFloat(D.TextInkWidth(SSXFe::EFont::Menu, P.FeLabels[3], 1.16f));
			TestTrue(TEXT("label right edge 460 at y 190"), FMath::IsNearlyEqual(Kd->Pos.X + W, 460.f, 0.5f) && Kd->Pos.Y == 190.f);
		}
		if (const FSSXFeTextRec* Tv = FindText(D, P.FeValues[0][5]); TestNotNull(TEXT("tricks value"), Tv))
		{
			TestEqual(TEXT("tricks value at (475, 242)"), Tv->Pos, FVector2f(475.f, 242.f));
		}
	}
	{
		T2.Start();
		T2.Step(false, 3);
		FSSXFeDrawList D(&Art.Assets);
		T2.Draw(D);
		int32 Pics = 0;
		for (const FSSXFeDrawCmd& C : D.Cmds) { Pics += C.Size == FVector2f(254.f, 79.f); }
		TestEqual(TEXT("c2PFELoad: 2 pictures x (black copy + picture)"), Pics, 4);
		if (const FSSXFeTextRec* V2 = FindText(D, T2.Page.FeValues[1][5]); TestNotNull(TEXT("player 2 tricks"), V2))
		{
			TestEqual(TEXT("player 2 values left at 430, tricks y 320"), V2->Pos, FVector2f(430.f, 320.f));
		}
		if (const FSSXFeTextRec* V1 = FindText(D, T2.Page.FeValues[0][5]); TestNotNull(TEXT("player 1 tricks"), V1))
		{
			const float W = FMath::TruncToFloat(D.TextInkWidth(SSXFe::EFont::Menu, T2.Page.FeValues[0][5], 1.16f));
			TestTrue(TEXT("player 1 values right edge 210"), FMath::IsNearlyEqual(V1->Pos.X + W, 210.f, 0.5f) && V1->Pos.Y == 320.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitRankLoaderTest, "SSX.UI.Quit.RankLoader", QtFlags)
bool FSSXQuitRankLoaderTest::RunTest(const FString& Parameters)
{
	using EPhase = FSSXTrackLoader::EPhase;
	using namespace SSXTrackLoad;
	FSSXLocStrings Loc;
	const bool bLoc = Loc.Load(SSXFrontEnd::LangDir());
	const FSSXLocStrings* Lp = bLoc ? &Loc : nullptr;
	TestTrue(TEXT("board type texts 0x81 / 0xB9 / 0x79"), RankBoardTypeLoc(0) == 0x81 && RankBoardTypeLoc(1) == 0xB9 && RankBoardTypeLoc(2) == 0x79 && RankBoardTypeLoc(3) == -1);
	TArray<FSSXUnlock> U;
	TestEqual(TEXT("no board unlock -> board 0"), RankUpBoard(U), 0);
	U.Add({ 0, SSXProg::URank, 2 });
	U.Add({ 0, SSXProg::UBoard, 4 });
	U.Add({ 0, SSXProg::UBoard, 5 });
	TestEqual(TEXT("the first board entry"), RankUpBoard(U), 4);

	FSSXProfile Profile = FSSXProfile::MakeDefault();
	Profile.Session.Unlocks = U;
	FSSXFeLaunch L;
	L.GameMode = 4;
	L.Slots[0].Char = 3;
	FSeq R;
	FSSXTrackLoader T;
	T.BeginReturn(EKind::RiderRanking, false, L, &Profile, Lp, R);
	const FSSXTrackLoadPage& P = T.Page;
	TestTrue(TEXT("queue: 5 channels, steps 1..5"), T.Seq.Num == 5 && T.Seq.MaxStep == 5);
	TestEqual(TEXT("board name from the table"), P.RankValues[1], SSXFeTables::Board(3, 4).Name);
	TestEqual(TEXT("rider picture"), P.RankPicture, RiderPicture(3));
	if (bLoc)
	{
		TestTrue(TEXT("LOC 0xD2A..0xD2E"), P.RankTitle == Loc.ByIndex(0xD2A) && P.RankBody == Loc.ByIndex(0xD2B) && P.RankLabels[0] == Loc.ByIndex(0xD2C)
			&& P.RankLabels[1] == Loc.ByIndex(0xD2D) && P.RankLabels[2] == Loc.ByIndex(0xD2E));
		if (SSXFeTables::BoardsLoaded())
		{
			TestEqual(TEXT("board type text"), P.RankValues[2], Loc.ByIndex(RankBoardTypeLoc(SSXFeTables::Board(3, 4).Type)));
		}
	}
	T.Start();
	TestTrue(TEXT("start: bank-13 #0"), T.Phase == EPhase::Run && T.MusicStarts == 1);
	int32 Frames = 0;
	while (!T.bIntroDone() && Frames < 20000) { T.Step(false, 3); ++Frames; }
	const int32 Expect = 2 * P.RankTitle.Len() + P.RankBody.Len() + 2 * (P.RankValues[0].Len() + P.RankValues[1].Len() + P.RankValues[2].Len() + 12);
	AddInfo(FString::Printf(TEXT("cRiderRankingLoad intro: %d frames (expected about %d)"), Frames, Expect));
	TestTrue(TEXT("intro: 2 frames per title / value character, 1 per body character"), FMath::Abs(Frames - Expect) <= 10);
	TestEqual(TEXT("no FUN_0019E460 rows: no typing sound"), T.TypeSounds, 0);
	T.Step(false, 8);
	TestEqual(TEXT("app+8 = 8: the voice runs on"), T.MusicStops, 0);
	T.Step(false, 9);
	TestEqual(TEXT("app+8 == 9 stops it"), T.MusicStops, 1);
	T.Step(true, 10);
	TestEqual(TEXT("loaded + intro: Finish"), int32(T.Phase), int32(EPhase::Finish));
	FSSXTrackLoader T10;
	T10.BeginReturn(EKind::RiderRanking, false, L, &Profile, Lp, R);
	T10.Start();
	T10.Step(false, 10);
	TestEqual(TEXT("app+8 = 10 without 9: no stop (== 9 only)"), T10.MusicStops, 0);

	FSSXTrackLoadAssets Art;
	if (!bLoc || !LoadQuitArt(Art))
	{
		SSXTest::Skip(*this, TEXT("loading screen art / fonts / LOC not converted; rank page draw checks skipped"));
		return true;
	}
	FSSXFeDrawList D(&Art.Assets);
	T.Draw(D);
	bool bGlobe = false;
	for (const FSSXFeDrawCmd& C : D.Cmds) { bGlobe |= C.SpriteId == 0 && C.Pos == FVector2f(10.f, 0.f); }
	TestTrue(TEXT("record 0 at (10, 0)"), bGlobe);
	if (const FSSXFeTextRec* Ti = FindText(D, P.RankTitle); TestNotNull(TEXT("title"), Ti))
	{
		TestEqual(TEXT("title at (320, 110)"), Ti->Pos, FVector2f(320.f, 110.f));
	}
	if (const FSSXFeTextRec* V = FindText(D, P.RankValues[0]); TestNotNull(TEXT("new rank value"), V))
	{
		TestEqual(TEXT("rank value at (410, 290)"), V->Pos, FVector2f(410.f, 290.f));
	}
	const TArray<FString> Lines = WrapBox(D, SSXFe::EFont::Menu, P.RankBody, 1.06f, 270.f);
	if (Lines.Num() > 0)
	{
		if (const FSSXFeTextRec* L0 = FindText(D, Lines[0]); TestNotNull(TEXT("body line 1 (fully typed)"), L0))
		{
			TestEqual(TEXT("body at (320, 152)"), L0->Pos, FVector2f(320.f, 152.f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitRankVideoTest, "SSX.UI.Quit.RankVideo", QtFlags)
bool FSSXQuitRankVideoTest::RunTest(const FString& Parameters)
{
	using namespace SSXTrackLoad;
	// 0x1BA330
	TestEqual(TEXT("Mac"), RankVideoName(3), FString(TEXT("RR_MAC")));
	TestEqual(TEXT("JP"), RankVideoName(6), FString(TEXT("RR_JP")));
	TestEqual(TEXT("Marisol"), RankVideoName(11), FString(TEXT("RR_MARI")));
	TestEqual(TEXT("Kaori"), RankVideoName(1), FString(TEXT("RR_KAOR")));

	FSSXRankVideo V;
	TestFalse(TEXT("results quit: no video"), V.Begin(PickAfterRace(false, false, false, 4, true, false, 1), 3));
	TestTrue(TEXT("no video: off"), V.Phase == FSSXRankVideo::EPhase::Off && !V.PressSkip() && !V.Finish());
	const FReturnPick Pick = PickAfterRace(true, false, false, 4, true, false, 1);
	TestTrue(TEXT("exit 0x30: video"), V.Begin(Pick, 3));
	TestTrue(TEXT("playing RR_MAC, skippable (flag 1)"), V.IsPlaying() && V.Name == TEXT("RR_MAC") && V.bSkippable);
	TestTrue(TEXT("X / Start skips"), V.PressSkip() && V.bSkipped);
	TestTrue(TEXT("finish once"), V.Finish() && !V.Finish() && V.Phase == FSSXRankVideo::EPhase::Done);
	TestEqual(TEXT("then cRiderRankingLoad"), int32(Pick.Kind), int32(EKind::RiderRanking));
	TestFalse(TEXT("no skip once done"), V.PressSkip());
	TestEqual(TEXT("video then c2PFELoad"), int32(PickAfterRace(true, false, false, 2, false, false, 2).Kind), int32(EKind::FETwoP));
	const FString Path = FPaths::Combine(SSXFrontEnd::VideoDir(), TEXT("RR_MAC.m2ts"));
	if (!FPaths::FileExists(Path))
	{
		SSXTest::Skip(*this, TEXT("rank videos not converted (tools/mpc.py extracted/DATA/VIDEO converted/frontend/video RR_...)"));
	}
	else
	{
		TestTrue(TEXT("RR_MAC.wav beside the movie"), FPaths::FileExists(FPaths::Combine(SSXFrontEnd::VideoDir(), TEXT("RR_MAC.wav"))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXQuitRankVideoOverlayTest, "SSX.UI.Quit.RankVideoOverlay", QtFlags)
bool FSSXQuitRankVideoOverlayTest::RunTest(const FString& Parameters)
{
	using namespace SSXTrackLoad;
	UGameInstance* GI = NewObject<UGameInstance>();
	USSXLoadingScreen* LS = NewObject<USSXLoadingScreen>(GI);
	UGameViewportClient* VC = NewObject<UGameViewportClient>(GEngine);
	TSharedRef<SOverlay> Overlay = SNew(SOverlay);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	VC->SetViewportOverlayWidget(nullptr, Overlay);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	LS->ViewportOverride = VC;

	FSSXFeLaunch L;
	L.GameMode = 4;
	L.Track = 0;
	L.Players = 1;
	L.Slots[0].Char = 3;
	const FReturnPick Pick = PickAfterRace(true, false, false, 4, true, false, 1);
	LS->BeginReturn(Pick, L, FString());
	TestTrue(TEXT("rank video playing, one overlay slot"), LS->GetRankVideo().IsPlaying() && Overlay->GetNumWidgets() == 1);
	TestTrue(TEXT("skip the movie"), LS->PressSkip());
	LS->TickLoader(1.f / 60.f);
	TestTrue(TEXT("cRiderRankingLoad runs after the movie"), !LS->GetRankVideo().IsPlaying() && LS->IsActive()
		&& LS->GetLoader().Kind == EKind::RiderRanking);
	TestEqual(TEXT("the loader shares the movie's overlay slot (no second slot)"), Overlay->GetNumWidgets(), 1);
	for (int32 I = 0; I < 30; ++I)
	{
		LS->TickLoader(1.f / 60.f);
	}
	TestEqual(TEXT("still one overlay slot while the loader runs"), Overlay->GetNumWidgets(), 1);
	LS->End();
	TestEqual(TEXT("loader done: nothing of the loading screen is left over the FE"), Overlay->GetNumWidgets(), 0);
	TestFalse(TEXT("the FE is no longer held"), LS->IsActive());

	LS->BeginReturn(PickAfterRace(false, false, false, 4, false, false, 1), L, FString());
	TestEqual(TEXT("c1PFELoad: one overlay slot"), Overlay->GetNumWidgets(), 1);
	LS->End();
	TestEqual(TEXT("c1PFELoad done: none left"), Overlay->GetNumWidgets(), 0);
	LS->ViewportOverride.Reset();

	FSSXProfile P = FSSXProfile::MakeDefault();
	P.Slot[0].Char = 3;
	FSSXCheats C;
	FSSXFrontEnd FE;
	FE.Init(&P, &C, nullptr, false, 0x30 & ~0x20);
	FE.GameMode = 4;
	FE.NumPlayers = 1;
	FE.PadsConnected = 1;
	TestTrue(TEXT("FE opens under the black fade"), FE.Fader.bActive);
	for (int32 I = 0; I < 120; ++I)
	{
		FE.Tick();
	}
	TestTrue(TEXT("after 2 s the fade is gone (alpha 0)"), !FE.Fader.bActive && FE.Fader.Alpha == 0.f);
	TestTrue(TEXT("FE on CharacterAccept, select camera (mode 9)"), FE.Current() == ESSXFeScreen::CharacterAccept && FE.CamMode == 9);
	return true;
}

#endif
