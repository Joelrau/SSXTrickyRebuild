// 0x279A80
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "World/PropControllers.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FEvFe
	{
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFeCameraSet Set;
		FSSXFrontEnd FE;
		bool bCams = false;
		FEvFe()
		{
			SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
			bCams = Set.Load(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json")));
			FE.PadsConnected = 1;
			FE.Init(&Profile, &Cheats, nullptr, false);
			if (bCams) { FE.Camera.CameraSet = &Set; }
		}
		FString Screen() const { return LexToString(FE.Current()); }
		void Key(ESSXFeKey K) { Settle(); FE.Key(0, K); FE.Tick(); Settle(); }
		void Settle() { for (int32 N = 0; !FE.IsReady() && FE.Boot == ESSXFeBoot::FrontEnd && N < 3000; ++N) { FE.Tick(); } }
		int32 RunPending(int32 Max = 600)
		{
			int32 N = 0;
			while (FE.bHasPendingOpen && N < Max) { FE.Tick(); ++N; }
			return N;
		}
		/** Draw commands + strings of one frame without the fade quad (no assets: rects and strings are still recorded).
		 *  OutBarY = the first command's y (the title bar's dark rect, drawn first). */
		int32 DrawCount(float* OutBarY = nullptr) const
		{
			FSSXFeDrawList D;
			FE.Draw(D);
			int32 N = D.Texts.Num();
			bool bFirst = true;
			for (const FSSXFeDrawCmd& C : D.Cmds)
			{
				if (C.Layer == SSXFe::SpriteLayer(SSXFe::FadeLayer)) continue;
				if (bFirst && OutBarY) { *OutBarY = C.Pos.Y; }
				bFirst = false;
				++N;
			}
			if (bFirst && OutBarY) { *OutBarY = 0.f; }
			return N;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIEventSelCameraTest, "SSX.UI.EventSelCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXUIEventSelCameraTest::RunTest(const FString& Parameters)
{
	{
		FEvFe F;
		F.FE.GoTo(ESSXFeScreen::CharacterModify);
		TestEqual(TEXT("on CharacterModify"), F.Screen(), FString(TEXT("CharacterModify")));
		TestEqual(TEXT("Continue selected"), F.FE.Top()->SelectedId(), 0);
		for (int32 I = 0; I < 40; ++I) { F.FE.Tick(); }
		TestEqual(TEXT("CharacterModify camera (vfunc_15): mode 10"), F.FE.CamMode, 10);
		const FString ModifyCam = F.FE.Camera.Current;
		TestTrue(TEXT("CharacterModify camera = a held charSelect_select<Name> frame"),
			ModifyCam.StartsWith(TEXT("charSelect_select")) && !F.FE.Camera.IsPlaying());
		float BarIn = 1.f;
		TestTrue(TEXT("CharacterModify drawn (frame + list)"), F.DrawCount(&BarIn) > 0);
		TestTrue(TEXT("title bar in place (y 0)"), FMath::IsNearlyZero(BarIn, 0.01f));
		F.FE.Key(0, ESSXFeKey::Cross);
		TestTrue(TEXT("EventSel pending, CharacterModify still on top (closing)"), F.FE.bHasPendingOpen && F.Screen() == TEXT("CharacterModify"));
		TestEqual(TEXT("phase 3 closing"), F.FE.Top()->Phase, 3);
		TestEqual(TEXT("vfunc_21 not run yet: no 0xB request during the slide-out"), F.FE.Camera.Current, ModifyCam);
		TestTrue(TEXT("Continue: +0x24 = 1, the black out fade (0.5 / 0 / 0.5) starts at the press"),
			F.FE.Fader.bActive && F.FE.Fader.Color.R == 0.f && F.FE.Fader.Up == 0.5f && F.FE.Fader.Down == 0.5f);
		const float H = F.FE.Top()->BarH;
		for (int32 I = 0; I < 15; ++I) { F.FE.Tick(); }
		float BarMid = 0.f;
		TestTrue(TEXT("half-way through the slide-out the menu is still drawn"), F.DrawCount(&BarMid) > 0);
		TestTrue(TEXT("title bar half-way up (y = -H/2 after 15 of 30 frames)"), FMath::IsNearlyEqual(BarMid, -H * 0.5f, 0.5f));
		TestEqual(TEXT("still phase 3"), F.FE.Top()->Phase, 3);
		int32 Frames = 15;
		while (F.FE.Camera.Current != TEXT("venueSelect_intro") && Frames < 100) { F.FE.Tick(); ++Frames; }
		TestEqual(TEXT("0xB requested after the 31 tween updates of the slide-out (vfunc_21)"), Frames, 31);
		TestEqual(TEXT("Continue: request 0xB = venueSelect_intro (script 0x7B)"), F.FE.Camera.Current, FString(TEXT("venueSelect_intro")));
		TestEqual(TEXT("camera mode 0xB"), F.FE.CamMode, 0xB);
		TestEqual(TEXT("no menu drawn once slid out"), F.DrawCount(), 0);
		if (F.bCams)
		{
			const FSSXFeCamera* C = F.Set.Find(TEXT("venueSelect_intro"));
			TestTrue(TEXT("venueSelect_intro = whole clip, 4.000 s"), C && FMath::IsNearlyEqual(C->Duration, 4.f, 0.05f));
			F.FE.Key(0, ESSXFeKey::Triangle);
			TestEqual(TEXT("phase 4 closed-wait-camera"), F.FE.Top()->Phase, 4);
			TestTrue(TEXT("Back during the fly-in: still pending"), F.FE.bHasPendingOpen);
			int32 N = 1, MaxDraw = 0;
			while (F.FE.bHasPendingOpen && N < 600)
			{
				MaxDraw = FMath::Max(MaxDraw, F.DrawCount());
				F.FE.Tick();
				++N;
			}
			TestEqual(TEXT("no menu draw commands during the whole fly-in"), MaxDraw, 0);
			TestTrue(TEXT("EventSel opens when the 4 s fly-in has finished"), FMath::Abs(N - 4 * SSXFe::FrameRate) <= 3);
			if (C)
			{
				const FSSXFeCamPose P = C->Sample(C->Duration);
				TestTrue(TEXT("end pose in the venue room (0, -50000, 288)"), FVector(P.Pos).Equals(FVector(0.03, -50000.2, 288.0), 5.0));
				TestTrue(TEXT("end focal 40"), FMath::IsNearlyEqual(P.Focal, 40.f, 0.1f));
			}
			float Bar0 = 0.f;
			F.DrawCount(&Bar0);
			TestTrue(TEXT("EventSel title bar starts off screen (y = -H)"), FMath::IsNearlyEqual(Bar0, -F.FE.Top()->BarH, 0.5f));
			for (int32 I = 0; I < 30; ++I) { F.FE.Tick(); }
			float Bar30 = 1.f;
			F.DrawCount(&Bar30);
			TestTrue(TEXT("EventSel title bar in place after 30 frames"), FMath::IsNearlyZero(Bar30, 0.01f));
		}
		else
		{
			F.FE.Tick();
		}
		TestEqual(TEXT("-> EventSel"), F.Screen(), FString(TEXT("EventSel")));
		TestEqual(TEXT("EventSel requests no camera (stays on the fly-in)"), F.FE.Camera.Current, FString(TEXT("venueSelect_intro")));
		TestEqual(TEXT("camera mode still 0xB"), F.FE.CamMode, 0xB);
		F.Key(ESSXFeKey::Down);
		F.Key(ESSXFeKey::Up);
		F.Key(ESSXFeKey::Cross);
		TestEqual(TEXT("-> DifficultSel"), F.Screen(), FString(TEXT("DifficultSel")));
		TestEqual(TEXT("DifficultSel: venue room"), F.FE.Camera.Current, FString(TEXT("venueSelect_intro")));
		F.Key(ESSXFeKey::Triangle);
		TestEqual(TEXT("Back -> EventSel"), F.Screen(), FString(TEXT("EventSel")));
		TestEqual(TEXT("EventSel re-show: no request for the Race list"), F.FE.Camera.Current, FString(TEXT("venueSelect_intro")));
		F.Key(ESSXFeKey::Cross);
		const int32 HistBefore = F.FE.Camera.History.Num();
		F.Key(ESSXFeKey::Cross);
		TestEqual(TEXT("-> VenueSel"), F.Screen(), FString(TEXT("VenueSel")));
		TestEqual(TEXT("VenueSel entry: no camera script (already in mode 0xB)"), F.FE.Camera.History.Num(), HistBefore);
		F.FE.Key(0, ESSXFeKey::Triangle);
		TestEqual(TEXT("VenueSel Back: phase 1 (slide-out)"), F.FE.Top()->Phase, 1);
		int32 BackFrames = 0;
		while (F.Screen() != TEXT("DifficultSel") && BackFrames < 100) { F.FE.Tick(); ++BackFrames; }
		TestEqual(TEXT("Back pops after the 31-frame slide-out (vfunc_05, phase 1)"), BackFrames, 31);
		TestEqual(TEXT("VenueSel Back -> DifficultSel"), F.Screen(), FString(TEXT("DifficultSel")));
		TestEqual(TEXT("DifficultSel re-show (0x29B8C8): no camera request, venue room"), F.FE.Camera.History.Num(), HistBefore);
		TestEqual(TEXT("DifficultSel re-show: mode stays 0xB"), F.FE.CamMode, 0xB);
		TestEqual(TEXT("a re-shown screen slides in again (vfunc_15 -> vfunc_04)"), F.FE.Top()->OpenFrame, 0);
		TestEqual(TEXT("re-shown: phase 0"), F.FE.Top()->Phase, 0);
		F.Settle();
		F.Key(ESSXFeKey::Triangle);
		TestEqual(TEXT("DifficultSel Back -> EventSel"), F.Screen(), FString(TEXT("EventSel")));
		TestEqual(TEXT("EventSel re-show (race list): no camera request"), F.FE.Camera.History.Num(), HistBefore);
		F.Key(ESSXFeKey::Triangle);
		TestEqual(TEXT("EventSel Back -> CharacterModify"), F.Screen(), FString(TEXT("CharacterModify")));
		TestEqual(TEXT("CharacterModify re-show: hard cut to select<Name> end frame (script 0x3F + slot)"), F.FE.Camera.Current, ModifyCam);
		TestFalse(TEXT("a held frame (cut), not a swing"), F.FE.Camera.IsPlaying());
		TestEqual(TEXT("camera mode 10"), F.FE.CamMode, 10);
		if (F.bCams)
		{
			const FSSXFeCamera* C = F.Set.Find(ModifyCam);
			TestTrue(TEXT("held at the script's end"), C && FMath::IsNearlyEqual(F.FE.Camera.Time, C->Duration, 0.001f));
		}
		TestTrue(TEXT("CharacterModify drawn again"), F.DrawCount() > 0);
		F.Key(ESSXFeKey::Cross);
		TestTrue(TEXT("second Continue plays the fly-in again"), F.FE.Camera.History.Num() > 0 && F.FE.Camera.History.Last() == TEXT("venueSelect_intro"));
		TestEqual(TEXT("-> EventSel again"), F.Screen(), FString(TEXT("EventSel")));
	}
	{
		FEvFe F;
		F.FE.GoTo(ESSXFeScreen::CharacterModify);
		F.FE.Group = 4;
		F.FE.GameMode = 1;
		F.Settle();
		F.Key(ESSXFeKey::Cross);
		TestTrue(TEXT("Freeride Continue: venueSelect_intro"), F.FE.Camera.History.Contains(TEXT("venueSelect_intro")));
		TestEqual(TEXT("Freeride -> VenueSel after the fly-in"), F.Screen(), FString(TEXT("VenueSel")));
		F.Key(ESSXFeKey::Triangle);
		TestEqual(TEXT("VenueSel Back -> CharacterModify"), F.Screen(), FString(TEXT("CharacterModify")));
		TestTrue(TEXT("Freeride Back: select<Name> end frame, mode 10"),
			F.FE.Camera.Current.StartsWith(TEXT("charSelect_select")) && F.FE.CamMode == 10);
	}
	{
		FEvFe F;
		F.Settle();
		F.Key(ESSXFeKey::Cross);
		F.Key(ESSXFeKey::Left);
		F.Key(ESSXFeKey::Cross);
		TestEqual(TEXT("Practice EventSel"), F.Screen(), FString(TEXT("EventSel")));
		TestEqual(TEXT("Practice: mode-select room PR view"), F.FE.Camera.Current, FString(TEXT("modeSelectPR_SE")));
		TestFalse(TEXT("no fly-in"), F.FE.Camera.History.Contains(TEXT("venueSelect_intro")));
	}
	{
		FEvFe F;
		F.FE.GoTo(ESSXFeScreen::CharacterModify);
		F.FE.Group = 3;
		F.FE.GameMode = 6;
		F.Settle();
		F.FE.Key(0, ESSXFeKey::Cross);
		TestTrue(TEXT("Tutorial: launch pending"), F.FE.bLaunchPending && !F.FE.bHasPendingOpen);
		int32 N = 0;
		while (F.FE.Boot != ESSXFeBoot::Loading && N < 600) { F.FE.Tick(); ++N; }
		TestFalse(TEXT("Tutorial: no venueSelect_intro"), F.FE.Camera.History.Contains(TEXT("venueSelect_intro")));
		TestTrue(TEXT("Tutorial: loading screen after the slide-out"), F.FE.Boot == ESSXFeBoot::Loading && N >= 31);
	}
	{
		FEvFe F;
		F.Settle();
		TestEqual(TEXT("Title: mode 1, intro frame 0 held"), F.FE.CamMode, 1);
		TestTrue(TEXT("Title drawn"), F.DrawCount() > 0);
		F.FE.Key(0, ESSXFeKey::Cross);
		TestFalse(TEXT("Start Game: no out fade (+0x24 = 0)"), F.FE.Fader.bActive);
		TestEqual(TEXT("phase 3"), F.FE.Top()->Phase, 3);
		F.FE.Tick();
		TestEqual(TEXT("vfunc_21 on the first update: phase 4"), F.FE.Top()->Phase, 4);
		TestEqual(TEXT("Title still on top (closed, waiting for the camera)"), F.Screen(), FString(TEXT("Title")));
		TestTrue(TEXT("request 2 from mode 1: the whole modeSelectIntro (script 1)"),
			F.FE.Camera.Current == TEXT("modeSelectIntro") && F.FE.Camera.Start == 0.f && F.FE.Camera.End > 0.f);
		TestEqual(TEXT("camera mode 2"), F.FE.CamMode, 2);
		TestTrue(TEXT("onTitleOpen"), F.FE.SceneEvents.Contains(TEXT("onTitleOpen")));
		TestTrue(TEXT("entry 14(1)"), F.FE.SoundHooks.Contains(TEXT("stinger0")));
		TestEqual(TEXT("the Title is hidden during the fly-in"), F.DrawCount(), 0);
		if (F.bCams)
		{
			const FSSXFeCamera* C = F.Set.Find(TEXT("modeSelectIntro"));
			const float Dur = C ? C->Duration : 0.f;
			TestTrue(TEXT("modeSelectIntro has a length"), Dur > 0.5f);
			int32 N = 0, MaxDraw = 0;
			while (F.Screen() == TEXT("Title") && N < 2000) { MaxDraw = FMath::Max(MaxDraw, F.DrawCount()); F.FE.Tick(); ++N; }
			TestEqual(TEXT("nothing drawn during the whole fly-in"), MaxDraw, 0);
			TestTrue(FString::Printf(TEXT("ModeSelect is built when the %.2f s fly-in has finished (%d frames)"), Dur, N),
				FMath::Abs(N - Dur * SSXFe::FrameRate) <= 3);
			TestEqual(TEXT("ModeSelect: camera mode 5 (SE highlight, no new script)"), F.FE.CamMode, 5);
			TestEqual(TEXT("still on the intro clip's end"), F.FE.Camera.Current, FString(TEXT("modeSelectIntro")));
			TestEqual(TEXT("ModeSelect sliding in (phase 0)"), F.FE.Top()->Phase, 0);
			int32 In = 0;
			while (F.FE.Top()->Phase == 0 && In < 100) { F.FE.Tick(); ++In; }
			TestEqual(TEXT("input after the 31 slide-in updates"), In, 31);
			TestTrue(TEXT("vfunc_20: onTitleStop + music event 1"), F.FE.SceneEvents.Last() == TEXT("onTitleStop") && F.FE.SoundHooks.Contains(TEXT("music1")));
			const int32 Ev0 = F.FE.SceneEvents.Num(), Snd0 = F.FE.SoundHooks.Num();
			F.Key(ESSXFeKey::Triangle);
			TestEqual(TEXT("Back: Title again"), F.Screen(), FString(TEXT("Title")));
			TestEqual(TEXT("Back: camera mode 1"), F.FE.CamMode, 1);
			TestTrue(TEXT("Back: the title view (modeSelectIntro frame 0 held)"),
				F.FE.Camera.Current == TEXT("modeSelectIntro") && F.FE.Camera.Start == 0.f && F.FE.Camera.End == 0.f);
			TArray<FString> Ev;
			for (int32 I = Ev0; I < F.FE.SceneEvents.Num(); ++I) { Ev.Add(F.FE.SceneEvents[I]); }
			TestEqual(TEXT("Back: scene events = onSEClose only (no title-door event)"), FString::Join(Ev, TEXT(",")), FString(TEXT("onSEClose")));
			const TArray<FString> All = F.FE.SceneEvents;
			const int32 IOpen = All.Find(TEXT("onTitleOpen")), IStop = All.Find(TEXT("onTitleStop"));
			TestTrue(TEXT("onTitleOpen, then onTitleStop (static pose) before the Back"), IOpen != INDEX_NONE && IStop > IOpen && IStop < Ev0);
			bool bMusic0 = false;
			for (int32 I = Snd0; I < F.FE.SoundHooks.Num(); ++I) { bMusic0 |= F.FE.SoundHooks[I] == TEXT("music0"); }
			TestTrue(TEXT("Back: music event 0"), bMusic0);
			TestTrue(TEXT("Back: Title drawn"), F.DrawCount() > 0);
			FEvFe G;
			G.Settle();
			G.FE.Key(0, ESSXFeKey::Cross);
			G.FE.Tick();
			G.FE.Tick();
			G.FE.Key(0, ESSXFeKey::Cross);
			TestTrue(TEXT("skip: script speed x100"), G.FE.Camera.Speed == 100.f);
			TestTrue(TEXT("skip: black flash (0, 0, 0.5)"), G.FE.Fader.bActive && G.FE.Fader.Up == 0.f && G.FE.Fader.Down == 0.5f);
			int32 M = 0;
			while (G.Screen() == TEXT("Title") && M < 2000) { G.FE.Tick(); ++M; }
			TestTrue(FString::Printf(TEXT("skipped fly-in: ModeSelect after %d frames"), M), M <= 3);
		}
	}
	return true;
}

// FUN_0029BAA0 / 0x3A8AF0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIVenueScreensTest, "SSX.UI.VenueScreens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXUIVenueScreensTest::RunTest(const FString& Parameters)
{
	FEvFe F;
	const TArray<int32>& Order = SSXFeTables::VenueOrder();
	TestEqual(TEXT("nothing sent before VenueSel"), F.FE.VenueScreenFrame[1], -1);
	F.FE.GoTo(ESSXFeScreen::VenueSel);
	F.Settle();
	TestEqual(TEXT("on VenueSel"), F.Screen(), FString(TEXT("VenueSel")));
	for (int32 I = 0; I < 10; ++I)
	{
		const FSSXFeItem* It = F.FE.Top()->Items.FindByPredicate([&](const FSSXFeItem& X) { return X.Id == Order[I]; });
		TestTrue(FString::Printf(TEXT("screen %d (venue %d) frame = item enable (FUN_00163BB8)"), I, Order[I]),
			It && F.FE.VenueScreenFrame[I] == (It->bEnabled ? 1 : 0));
	}
	TestEqual(TEXT("Garibaldi (screen 0) open: frame 1"), F.FE.VenueScreenFrame[0], 1);
	TestEqual(TEXT("Snowdream (screen 1) open on a new save: frame 1"), F.FE.VenueScreenFrame[1], 1);
	TestEqual(TEXT("Elysium (screen 2) open: frame 1"), F.FE.VenueScreenFrame[2], 1);
	TestEqual(TEXT("Mesablanca (screen 3) locked: frame 0"), F.FE.VenueScreenFrame[3], 0);
	TestEqual(TEXT("Alaska (screen 7) locked: frame 0"), F.FE.VenueScreenFrame[7], 0);
	const int32 Serial = F.FE.VenueScreenSerial;
	F.Profile.VenueUnlocks |= 1u << 11;
	F.Key(ESSXFeKey::Triangle);
	F.Key(ESSXFeKey::Cross);
	TestEqual(TEXT("VenueSel again"), F.Screen(), FString(TEXT("VenueSel")));
	TestTrue(TEXT("sent again"), F.FE.VenueScreenSerial > Serial);
	TestEqual(TEXT("Alaska unlocked: frame 1"), F.FE.VenueScreenFrame[7], 1);
	if (SSXFeTables::VenueScreenInstance(0) != INDEX_NONE)
	{
		// 0x347130
		TestEqual(TEXT("screen 0 = mdl_videoscreen_00 (instance 148)"), SSXFeTables::VenueScreenInstance(0), 148);
		TestEqual(TEXT("screen 1 (Snowdream) = mdl_videoscreen_09 (instance 182)"), SSXFeTables::VenueScreenInstance(1), 182);
		TestEqual(TEXT("screen 9 = mdl_videoscreen_01 (instance 130)"), SSXFeTables::VenueScreenInstance(9), 130);
	}
	else
	{
		SSXTest::Skip(*this, TEXT("fe_tables.json without venue_screens (run tools/export_fe_tables.py)"));
	}
	SSXProps::FPropControllers Ctl;
	SSXProps::FPropController C;
	C.Type = SSXProps::EControllerType::TexFlip;
	C.Instance = 182;
	C.Flip.Rng = []() { return 0.5; };
	C.Flip.Create({ 27, 28 });
	Ctl.Add(C);
	TestEqual(TEXT("frame 0 at creation: dark logo (locked look)"), Ctl.Find(182)->Flip.TextureIndex(), 27);
	Ctl.Message(182, 2, 1.0);
	TestEqual(TEXT("message 2 = 1.0 (open): colour logo"), Ctl.Find(182)->Flip.TextureIndex(), 28);
	for (int32 K = 0; K < 120; ++K) { Ctl.Tick(); }
	TestEqual(TEXT("rate 0: the frame holds"), Ctl.Find(182)->Flip.TextureIndex(), 28);
	Ctl.Message(182, 2, 0.0);
	TestEqual(TEXT("message 2 = 0.0 (locked): dark logo"), Ctl.Find(182)->Flip.TextureIndex(), 27);
	return true;
}

// 0x17B278 / 0x3857C0 / FUN_0026CF28 / 0x29BF40
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIVenueInputTest, "SSX.UI.VenueInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXUIVenueInputTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	{
		FFeKeyEmitter E;
		int32 Rights = 0;
		for (int32 F = 0; F < 120; ++F) { Rights += E.Step(PadRight) == 0x4D00; }
		TestEqual(TEXT("Right held 2 s: one key (FE repeat off)"), Rights, 1);
		TestEqual(TEXT("release: nothing"), E.Step(0), 0);
		TestEqual(TEXT("press again: Right"), E.Step(PadRight), 0x4D00);
		TestEqual(TEXT("stick right added while the d-pad is held: same row, no key"), E.Step(PadRight | StickBits(0.9f, 0.f)), 0);
		TestEqual(TEXT("d-pad released, stick still right: no key"), E.Step(StickBits(0.9f, 0.f)), 0);
		TestTrue(TEXT("stick at exactly 0.5: no bit"), StickBits(0.5f, -0.5f) == 0u);
		TestTrue(TEXT("stick 0.51 right / up"), StickBits(0.51f, -0.51f) == uint32(PadLJoyRight | PadLJoyUp));
		E.Step(0);
		int32 Keys = 0;
		for (float V : { 0.49f, 0.51f, 0.49f, 0.51f, 0.49f }) { Keys += E.Step(StickBits(V, 0.f)) == 0x4D00; }
		TestEqual(TEXT("stick hovering at 0.5: a key per crossing (no hysteresis)"), Keys, 2);
		E.Step(0);
		TestEqual(TEXT("hat up-right: Right row before Up"), E.Step(PadUp | PadRight), 0x4D00);
		E.Step(0);
		TestEqual(TEXT("stick down-left: Left row first"), E.Step(StickBits(-0.8f, 0.8f)), 0x4B00);
		E.Step(0);
		TestEqual(TEXT("L1 + Right: shoulder column 0x4F00"), E.Step(PadL1 | PadRight), 0x4F00);
		E.Step(PadRight | PadCross);
		TestEqual(TEXT("X released with Right still held: the key changes back to Right"), E.Step(PadRight), 0x4D00);
		FFeKeyEmitter R;
		R.bRepeat = true;
		TArray<int32> At;
		for (int32 F = 0; F < 40; ++F) { if (R.Step(PadDown)) { At.Add(F); } }
		TestEqual(TEXT("repeat on (race pads): frames 0, 15, 23, 31, 39"), At, TArray<int32>({ 0, 15, 23, 31, 39 }));
	}
	FEvFe F;
	if (!F.bCams)
	{
		SSXTest::Skip(*this, TEXT("fe_cameras.json missing: venue walk skipped"));
		return true;
	}
	const TArray<int32>& Order = SSXFeTables::VenueOrder();
	TestEqual(TEXT("ten venues"), Order.Num(), 10);
	F.FE.GoTo(ESSXFeScreen::VenueSel);
	F.Settle();
	TestEqual(TEXT("default highlight = cam+0x185 = 0"), F.FE.Top()->Sel, 0);
	const float Seg = SSXFeCams::VenueFramesPerSlot / SSXFeCams::Fps;
	for (int32 Step = 1; Step <= 10; ++Step)
	{
		F.Key(ESSXFeKey::Right);
		const int32 New = Step % 10;
		TestEqual(*FString::Printf(TEXT("Right %d: list index"), Step), F.FE.Top()->Sel, New);
		TestEqual(*FString::Printf(TEXT("Right %d: cycleRight"), Step), F.FE.Camera.Current, FString(TEXT("venueSelect_cycleRight")));
		const int32 K = (New + 9) % 10;
		TestTrue(*FString::Printf(TEXT("Right %d: script 0x%X = one segment %d"), Step, 0x7C + New, K),
			FMath::IsNearlyEqual(F.FE.Camera.Start, K * Seg, 1e-4f) && FMath::IsNearlyEqual(F.FE.Camera.End, (K + 1) * Seg, 1e-4f));
		TestEqual(*FString::Printf(TEXT("Right %d: cam+0x185"), Step), F.FE.VenueCamSlot, New);
	}
	for (int32 Step = 1; Step <= 3; ++Step)
	{
		F.Key(ESSXFeKey::Left);
		const int32 New = (10 - Step) % 10;
		const int32 K = (9 - New + 10) % 10;
		TestTrue(*FString::Printf(TEXT("Left %d: cycleLeft script, one segment %d"), Step, K),
			F.FE.Camera.Current == TEXT("venueSelect_cycleLeft") && FMath::IsNearlyEqual(F.FE.Camera.Start, K * Seg, 1e-4f)
			&& FMath::IsNearlyEqual(F.FE.Camera.End, (K + 1) * Seg, 1e-4f));
		TestEqual(*FString::Printf(TEXT("Left %d: list index"), Step), F.FE.Top()->Sel, New);
	}
	F.Settle();
	F.FE.Key(0, ESSXFeKey::Right);
	const int32 After = F.FE.Top()->Sel;
	TestTrue(TEXT("segment playing"), F.FE.Camera.IsRunning());
	F.FE.Tick();
	F.FE.Key(0, ESSXFeKey::Right);
	F.FE.Key(0, ESSXFeKey::Right);
	TestEqual(TEXT("presses during the swing are dropped, not queued"), F.FE.Top()->Sel, After);
	int32 Frames = 1;
	while (F.FE.Camera.IsRunning() && Frames < 100) { F.FE.Tick(); ++Frames; }
	TestTrue(FString::Printf(TEXT("the 24-keyframe segment (0.8 s) lasts 48 FE frames (%d)"), Frames), FMath::Abs(Frames - 48) <= 1);
	TestEqual(TEXT("and leaves the selection where it was"), F.FE.Top()->Sel, After);
	while (F.FE.Top()->Sel != 5) { F.Key(ESSXFeKey::Right); }
	F.FE.SceneEvents.Reset();
	F.FE.TopMut()->Items[5].bEnabled = true;
	F.FE.Key(0, ESSXFeKey::Cross);
	F.FE.Tick();
	TestEqual(TEXT("Tokyo: choose script 0x90 + 5"), F.FE.Camera.Current, FString(TEXT("venueSelect_chooseAloha")));
	TestTrue(TEXT("Tokyo: FEMega1"), F.FE.SceneEvents.Contains(TEXT("FEMega1")));
	return true;
}

// FUN_0026BE38 / 0x26BE38 / 0x293938 / 0x293B18
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIFeCameraStackTest, "SSX.UI.FeCameraStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXUIFeCameraStackTest::RunTest(const FString& Parameters)
{
	FEvFe F;
	if (!F.bCams)
	{
		SSXTest::Skip(*this, TEXT("fe_cameras.json missing: camera stack walk skipped"));
		return true;
	}
	auto Room = [&F]() -> FString
	{
		FSSXFeCamPose P;
		if (!F.FE.Camera.GetPose(F.Set, P)) return TEXT("none");
		if (P.Pos.Y > 34000.0) return TEXT("title");
		if (P.Pos.Y > 25000.0) return TEXT("modeSelect");
		if (P.Pos.Y < -40000.0) return TEXT("venue");
		if (FMath::Abs(P.Pos.X) < 5000.0 && FMath::Abs(P.Pos.Y) < 5000.0) return TEXT("char");
		return FString::Printf(TEXT("? (%.0f, %.0f, %.0f)"), P.Pos.X, P.Pos.Y, P.Pos.Z);
	};
	auto Held0 = [&F]() { return F.FE.Camera.Start == 0.f && F.FE.Camera.End == 0.f && F.FE.Camera.Time == 0.f; };
	auto Step = [&](const TCHAR* What, const TCHAR* WantScreen, const TCHAR* WantRoom, int32 WantMode)
	{
		TestEqual(FString::Printf(TEXT("%s: screen"), What), F.Screen(), FString(WantScreen));
		TestEqual(FString::Printf(TEXT("%s: room"), What), Room(), FString(WantRoom));
		TestEqual(FString::Printf(TEXT("%s: camera mode"), What), F.FE.CamMode, WantMode);
	};

	F.Settle();
	Step(TEXT("boot"), TEXT("Title"), TEXT("title"), 1);
	TestTrue(TEXT("Title: modeSelectIntro frame 0 held"), F.FE.Camera.Current == TEXT("modeSelectIntro") && Held0());

	F.Key(ESSXFeKey::Cross);
	Step(TEXT("Start Game"), TEXT("ModeSelect"), TEXT("modeSelect"), 5);
	TestTrue(TEXT("Start Game: the whole modeSelectIntro played to its end"), F.FE.Camera.Current == TEXT("modeSelectIntro")
		&& F.FE.Camera.Start == 0.f && F.FE.Camera.End > 0.f && F.FE.Camera.Time == F.FE.Camera.End);
	F.Key(ESSXFeKey::Cross);
	Step(TEXT("-> CharacterSelect"), TEXT("CharacterSelect"), TEXT("char"), 8);
	TestTrue(TEXT("CharacterSelect: request 6 swing modeSelectSE_charSelect"), F.FE.Camera.History.Contains(TEXT("modeSelectSE_charSelect")));
	TestTrue(TEXT("CharacterSelect: select<Name> frame 0"), F.FE.Camera.Current == TEXT("charSelect_selectEddie") && Held0());
	F.Key(ESSXFeKey::Cross);
	const FString AcceptScreen = F.Screen();
	TestEqual(TEXT("-> CharacterAccept"), AcceptScreen, FString(TEXT("CharacterAccept")));
	TestEqual(TEXT("CharacterAccept: room"), Room(), FString(TEXT("char")));
	F.Key(ESSXFeKey::Cross);
	Step(TEXT("-> CharacterModify"), TEXT("CharacterModify"), TEXT("char"), 10);
	F.Key(ESSXFeKey::Cross);
	Step(TEXT("-> EventSel"), TEXT("EventSel"), TEXT("venue"), 0xB);
	TestEqual(TEXT("EventSel: after venueSelect_intro"), F.FE.Camera.Current, FString(TEXT("venueSelect_intro")));

	F.Key(ESSXFeKey::Triangle);
	Step(TEXT("Back -> CharacterModify"), TEXT("CharacterModify"), TEXT("char"), 10);
	TestTrue(TEXT("CharacterModify re-show: select<Name> end frame held (cut)"),
		F.FE.Camera.Current == TEXT("charSelect_selectEddie") && F.FE.Camera.Start == F.FE.Camera.End && F.FE.Camera.End > 0.f);
	F.Key(ESSXFeKey::Triangle);
	Step(TEXT("Back -> CharacterAccept"), *AcceptScreen, TEXT("char"), 10);   // 0x27A000
	F.Key(ESSXFeKey::Triangle);
	Step(TEXT("Back -> CharacterSelect"), TEXT("CharacterSelect"), TEXT("char"), 8);
	TestTrue(TEXT("CharacterSelect re-show: select<Name> frame 0"), F.FE.Camera.Current == TEXT("charSelect_selectEddie") && Held0());
	const int32 Swings = F.FE.Camera.History.Num();
	F.Key(ESSXFeKey::Triangle);
	Step(TEXT("Back -> ModeSelect"), TEXT("ModeSelect"), TEXT("modeSelect"), 5);
	TestTrue(TEXT("ModeSelect re-show: cut to modeSelectSE_WC frame 0 (script 3), no reverse swing"),
		F.FE.Camera.Current == TEXT("modeSelectSE_WC") && Held0() && F.FE.Camera.History.Num() == Swings + 1);
	F.Key(ESSXFeKey::Triangle);
	Step(TEXT("Back -> Title"), TEXT("Title"), TEXT("title"), 1);
	TestTrue(TEXT("Title re-show: cut to modeSelectIntro frame 0 (script 0)"), F.FE.Camera.Current == TEXT("modeSelectIntro") && Held0());

	const int32 Before = F.FE.Camera.History.Num();
	F.Key(ESSXFeKey::Cross);
	Step(TEXT("Start Game again"), TEXT("ModeSelect"), TEXT("modeSelect"), 5);
	TestTrue(TEXT("Start Game again: the full modeSelectIntro"), F.FE.Camera.History.Num() > Before
		&& F.FE.Camera.Current == TEXT("modeSelectIntro") && F.FE.Camera.End > 0.f);
	return true;
}

#endif
