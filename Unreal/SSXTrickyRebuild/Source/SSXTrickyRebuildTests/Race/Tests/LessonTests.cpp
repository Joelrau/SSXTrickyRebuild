// FUN_0014B638, FUN_001579E0, FUN_0014BD98
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Race/FinishLineControl.h"
#include "Race/LessonMan.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"
#include "UI/SSXTrackLoad.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXLesson;
	constexpr EAutomationTestFlags LsFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FRecEnv : public ILessonEnv
	{
		ERacePhase Phase = ERacePhase::None;
		TArray<ERacePhase> Phases;
		TArray<int32> Narr;
		TArray<ESound> Sounds;
		int32 Resets = 0, Stamps = 0, Quits = 0, Flashes = 0, StopSpeeches = 0, FeKeys = 0, TextClears = 0;
		bool bTutorial = false, bCamIdle = true, bText = false, bBlocked = false;
		double Meter = -1.0, Tricky = -1.0;
		int32 LoadedRider = -1, LoadedLesson = -1, InpLen = 100, RandVal = 4;

		virtual bool IsBlocked() const override { return bBlocked; }
		virtual void SetRacePhase(ERacePhase P) override { Phase = P; Phases.Add(P); }
		virtual void StopSpeech() override { ++StopSpeeches; }
		virtual void Narration(int32 Step) override { Narr.Add(Step); }
		virtual void Sound(ESound S) override { Sounds.Add(S); }
		virtual bool CameraIdle() const override { return bCamIdle; }
		virtual void ResetWorld() override { ++Resets; }
		virtual void SetBoost(double M, double T) override { Meter = M; Tricky = T; }
		virtual void UseTutorialMapping(bool b) override { bTutorial = b; }
		virtual bool LoadInp(int32 Rider, int32 Lesson, TArray<FInpFrame>& Out) override
		{
			LoadedRider = Rider;
			LoadedLesson = Lesson;
			Out.SetNum(InpLen);
			for (int32 I = 0; I < InpLen; ++I)
			{
				Out[I].Pad.ButtonsFiltered = uint32(I + 1);
				Out[I].RecordedBools = 1u;
			}
			return true;
		}
		virtual void StampFinish() override { ++Stamps; }
		virtual void ClearFeKey() override { ++FeKeys; }
		virtual void QuitRace() override { ++Quits; }
		virtual void CameraFlash() override { ++Flashes; }
		virtual int32 Rand() override { return RandVal; }
		virtual bool HasTrickText() const override { return bText; }
		virtual void ClearTrickText() override { ++TextClears; }
	};

	void Step(FLessonMan& M, FRecEnv& E, uint32 Pad = 0)
	{
		M.AddPad(Pad);
		M.Update(E);
	}

	void ToWait(FLessonMan& M, FRecEnv& E, int32 Rider = 0)
	{
		M.Construct(Rider);
		Step(M, E);
		M.Requested = EState::Intro;
		Step(M, E);
		Step(M, E);
		Step(M, E);
	}

	void Pick(FLessonMan& M, FRecEnv& E, int32 K)
	{
		if (M.MenuLevel == 0) Step(M, E, PadCross);
		if (M.MenuLevel == 1) Step(M, E, PadCross);
		while (M.MenuSel > K) Step(M, E, PadUp);
		while (M.MenuSel < K) Step(M, E, PadDown);
		Step(M, E, PadCross);
		Step(M, E);
	}

	const FSSXLocStrings& LsLoc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	bool HasText(const FSSXFeDrawList& D, const FString& S)
	{
		return D.Texts.Contains(S);
	}
	bool HasTextPrefix(const FSSXFeDrawList& D, const FString& Prefix)
	{
		for (const FString& T : D.Texts) { if (!Prefix.IsEmpty() && T.StartsWith(Prefix)) return true; }
		return false;
	}
	int32 CountSprite(const FSSXFeDrawList& D, int32 Id)
	{
		int32 N = 0;
		for (const FSSXFeDrawCmd& C : D.Cmds) { N += C.SpriteId == Id ? 1 : 0; }
		return N;
	}
	const FSSXFeDrawCmd* FindSprite(const FSSXFeDrawList& D, int32 Id)
	{
		for (const FSSXFeDrawCmd& C : D.Cmds) { if (C.SpriteId == Id) return &C; }
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLessonStateMachineTest, "SSX.Lesson.StateMachine", LsFlags)
bool FSSXLessonStateMachineTest::RunTest(const FString& Parameters)
{
	FLessonMan M;
	FRecEnv E;
	M.Construct(3);
	TestEqual(TEXT("ctor: request Init (FUN_0014B5A8)"), int32(M.Requested), int32(EState::Init));
	TestEqual(TEXT("ctor: rider +0x510"), M.Rider, 3);
	TestEqual(TEXT("ctor: diagram at (20, 20)"), M.DiagramPosX, 20.f);
	Step(M, E);
	TestEqual(TEXT("first update enters Init"), int32(M.State), int32(EState::Init));
	Step(M, E);
	TestEqual(TEXT("Init waits for LoadLesson"), int32(M.State), int32(EState::Init));
	M.Requested = EState::Intro;
	Step(M, E);
	TestEqual(TEXT("Intro"), int32(M.State), int32(EState::Intro));
	TestTrue(TEXT("Intro enter: narration step 0, speech stopped, overlay 10"), E.Narr == TArray<int32>{ NarrIntro } && E.StopSpeeches == 1 && M.bOvLesson);
	TestEqual(TEXT("Intro update requests Start"), int32(M.Requested), int32(EState::Start));
	E.bCamIdle = false;
	Step(M, E);
	TestEqual(TEXT("Start"), int32(M.State), int32(EState::Start));
	TestEqual(TEXT("Start waits for the camera"), int32(M.Requested), int32(EState::None));
	E.bCamIdle = true;
	Step(M, E);
	TestEqual(TEXT("camera idle: Wait requested"), int32(M.Requested), int32(EState::Wait));
	Step(M, E);
	TestEqual(TEXT("Wait"), int32(M.State), int32(EState::Wait));
	TestTrue(TEXT("Wait enter: one item 'select', panel 435"), M.MenuCount == 1 && M.MenuItems[0] == LocSelect && M.PanelH == 435.f);

	Step(M, E, PadRight);
	TestTrue(TEXT("Right: chapter 2, its first lesson"), M.Chapter == 1 && M.Lesson == 5 && M.Scroll == 5 && M.Column == 0);
	TestTrue(TEXT("move sound"), E.Sounds.Num() == 1 && E.Sounds[0] == ESound::Move);
	Step(M, E, PadLeft);
	TestTrue(TEXT("Left: chapter 1"), M.Chapter == 0 && M.Lesson == 0);
	Step(M, E, PadLeft);
	TestTrue(TEXT("Left at chapter 1: nothing, no sound"), M.Chapter == 0 && E.Sounds.Num() == 2);
	for (int32 I = 0; I < 7; ++I) Step(M, E, PadRight);
	TestEqual(TEXT("Right stops at chapter 6"), M.Chapter, 5);
	for (int32 I = 0; I < 7; ++I) Step(M, E, PadLeft);
	Step(M, E, PadCross);
	TestEqual(TEXT("X: trick level"), M.MenuLevel, 1);
	for (int32 I = 0; I < 4; ++I) Step(M, E, PadRight);
	TestTrue(TEXT("Right x4: lesson 5, column 4"), M.Lesson == 4 && M.Column == 4 && M.Scroll == 0);
	Step(M, E, PadRight);
	TestEqual(TEXT("Right stops at the chapter's last lesson"), M.Lesson, 4);
	Step(M, E, PadLeft);
	TestTrue(TEXT("Left: lesson 4, column 3"), M.Lesson == 3 && M.Column == 3);
	Step(M, E, PadTriangle);
	TestTrue(TEXT("Triangle: back to the chapter level (back sound)"), M.MenuLevel == 0 && E.Sounds.Last() == ESound::Back);
	Step(M, E, PadCross);
	Step(M, E, PadCross);
	TestTrue(TEXT("X: action menu (show me / perform / free jump / quit)"), M.MenuLevel == 2 && M.MenuCount == 4 && M.MenuItems[0] == LocShowMe
		&& M.MenuItems[1] == LocPerform && M.MenuItems[2] == LocFreeJump && M.MenuItems[3] == LocQuit && M.MenuSel == 0);
	Step(M, E, PadUp);
	TestEqual(TEXT("Up at the top: stays"), M.MenuSel, 0);
	Step(M, E, PadDown);
	Step(M, E, PadCross);
	TestTrue(TEXT("perform requested, choice kept (+0x60)"), M.Requested == EState::Perform && M.MenuSaved == 1 && E.Sounds.Last() == ESound::Accept);

	E.Narr.Reset();
	Step(M, E);
	TestEqual(TEXT("Perform"), int32(M.State), int32(EState::Perform));
	TestTrue(TEXT("Perform enter: countdown, narration 2, lesson file of rider 3 lesson 4"), E.Phase == ERacePhase::Countdown
		&& E.Narr == TArray<int32>{ NarrPerform } && E.LoadedRider == 3 && E.LoadedLesson == 3);
	TestTrue(TEXT("overlay 10 off, 14 on; player's mapping"), !M.bOvLesson && M.bOvController && !E.bTutorial);
	TestTrue(TEXT("boost cleared (chapter 1)"), E.Meter == 0.0 && E.Tricky == 0.0);
	FSSXPadFrame Pad;
	TestFalse(TEXT("Perform: no pad injection"), M.InjectPad(Pad));
	for (int32 I = 0; I < 10; ++I) M.AfterPad();
	TestEqual(TEXT("Perform advances the index"), M.InpIndex, 10);
	M.EndTrigger();
	Step(M, E);
	TestEqual(TEXT("+0x54: End requested"), int32(M.Requested), int32(EState::End));
	E.Narr.Reset();
	Step(M, E);
	TestTrue(TEXT("Perform exit: failed narration 5"), E.Narr == TArray<int32>{ NarrFailed });
	TestTrue(TEXT("End enter: EndLesson, finish stamp, 3 items, panel 330"), M.State == EState::End && E.Phase == ERacePhase::EndLesson
		&& E.Stamps == 1 && M.MenuCount == 3 && M.MenuItems[0] == LocNewTrick && M.MenuItems[1] == LocRestart && M.PanelH == 330.f && M.bOvLesson);
	Step(M, E, PadDown);
	Step(M, E, PadCross);
	TestTrue(TEXT("End 'restart' -> FUN_0014B638: reset, countdown, Perform again"), E.Resets == 1 && E.Phase == ERacePhase::Countdown
		&& M.Requested == EState::Perform && M.InpIndex == 0 && !M.bOvLesson);
	Step(M, E);
	M.MarkPassed();
	M.EndTrigger();
	Step(M, E);
	E.Narr.Reset();
	Step(M, E);
	TestTrue(TEXT("passed: Perform exit narration 4"), E.Narr == TArray<int32>{ NarrPassed } && M.State == EState::End);
	Step(M, E, PadCross);
	TestTrue(TEXT("End 'new trick': reset, trick level, RestartLesson"), E.Resets == 2 && M.MenuLevel == 1 && E.Phase == ERacePhase::RestartLesson);
	M.Requested = EState::Intro;
	Step(M, E);
	Step(M, E);
	Step(M, E);
	TestTrue(TEXT("back in Wait at the trick level with 'select'"), M.State == EState::Wait && M.MenuLevel == 1 && M.MenuCount == 1);

	Step(M, E, PadCross);
	TestEqual(TEXT("action menu keeps the last choice"), M.MenuSel, 1);
	Step(M, E, PadUp);
	Step(M, E, PadCross);
	E.Narr.Reset();
	Step(M, E);
	TestTrue(TEXT("Preview enter: tutorial mapping, narration 1, countdown"), M.State == EState::Preview && E.bTutorial && M.bTutorialMapping
		&& E.Narr == TArray<int32>{ NarrPreview } && E.Phase == ERacePhase::Countdown);
	TestTrue(TEXT("Preview injects frame 0"), M.InjectPad(Pad) && Pad.ButtonsFiltered == 1u);
	for (int32 I = 0; I < E.InpLen + 5; ++I) M.AfterPad();
	TestEqual(TEXT("index stops at the frame count"), M.InpIndex, E.InpLen);
	TestTrue(TEXT("past the end: the file's zero slot"), M.InjectPad(Pad) && Pad.ButtonsFiltered == 0u);
	Step(M, E);
	TestEqual(TEXT("recording over: PreviewEnd requested"), int32(M.Requested), int32(EState::PreviewEnd));
	Step(M, E);
	TestTrue(TEXT("Preview exit restores the player's mapping"), !E.bTutorial && !M.bTutorialMapping);
	TestTrue(TEXT("PreviewEnd: new trick / perform / restart / quit, panel 240"), M.State == EState::PreviewEnd && M.MenuCount == 4
		&& M.MenuItems[1] == LocPerform && M.MenuItems[2] == LocRestart && M.PanelH == 240.f && E.Stamps == 3);
	Step(M, E, PadDown);
	Step(M, E, PadCross);
	TestTrue(TEXT("PreviewEnd 'perform': reset, countdown, Perform"), M.Requested == EState::Perform && E.Phase == ERacePhase::Countdown);

	// FUN_0014B768
	Step(M, E);
	const int32 R0 = E.Resets;
	M.Restart(E);
	TestTrue(TEXT("restart in Perform: reset, no request"), E.Resets == R0 + 1 && M.Requested == EState::None && M.State == EState::Perform);

	const int32 F0 = M.Frames;
	E.bBlocked = true;
	M.EndTrigger();
	Step(M, E);
	TestTrue(TEXT("paused: no update"), M.Frames == F0 && M.Requested == EState::None);
	E.bBlocked = false;

	{
		FLessonMan P;
		FRecEnv Pe;
		ToWait(P, Pe);
		Pick(P, Pe, 2);
		TestTrue(TEXT("Practice enter: rand % 3 word, trick text cleared"), P.State == EState::Practice && P.PracticeLoc == PracticeLocs[1]
			&& Pe.TextClears == 1 && Pe.Phase == ERacePhase::Countdown);
		Step(P, Pe);
		TestFalse(TEXT("no trick text: not passed"), P.bPassed);
		Pe.bText = true;
		Step(P, Pe);
		TestTrue(TEXT("a trick name on the rider: passed"), P.bPassed);
		P.EndTrigger();
		Step(P, Pe);
		Step(P, Pe);
		TestTrue(TEXT("PracticeEnd: 3 items, panel 240"), P.State == EState::PracticeEnd && P.MenuCount == 3 && P.PanelH == 240.f);
		Step(P, Pe, PadDown);
		Step(P, Pe, PadDown);
		Step(P, Pe, PadCross);
		TestTrue(TEXT("'quit': Exit requested, flash, overlay 10 off"), P.Requested == EState::Exit && Pe.Flashes == 1 && !P.bOvLesson);
		Step(P, Pe);
		TestTrue(TEXT("Exit update: world +0x54"), P.State == EState::Exit && Pe.Quits == 1 && !P.bOvController);
	}
	{
		FLessonMan Q;
		FRecEnv Qe;
		ToWait(Q, Qe);
		Step(Q, Qe, PadCross);
		Step(Q, Qe, PadCross);
		for (int32 I = 0; I < 3; ++I) Step(Q, Qe, PadDown);
		Step(Q, Qe, PadCross);
		TestTrue(TEXT("'quit' opens the message box"), Q.bQuitDialog && Q.Requested == EState::None);
		Step(Q, Qe, PadDown);
		TestEqual(TEXT("the box eats the pad"), Q.MenuSel, 3);
		Step(Q, Qe, PadTriangle);
		TestTrue(TEXT("Triangle: no"), !Q.bQuitDialog && Q.Requested == EState::None && Q.State == EState::Wait);
		Step(Q, Qe, PadCross);
		Step(Q, Qe, PadCross);
		TestEqual(TEXT("X: yes -> Exit"), int32(Q.Requested), int32(EState::Exit));
	}
	{
		FLessonMan U;
		FRecEnv Ue;
		ToWait(U, Ue);
		for (int32 I = 0; I < 5; ++I) Step(U, Ue, PadRight);
		Pick(U, Ue, 1);
		TestTrue(TEXT("chapter 6: meter 1, Tricky 20 (FUN_0014B768)"), U.Lesson == 25 && Ue.Meter == 1.0 && Ue.Tricky == 20.0);
	}
	// FUN_00231398
	uint32 Flags = 0;
	TestEqual(TEXT("step 0 first: 1"), NarrationWord(0, Flags), 1u);
	TestEqual(TEXT("step 0 again: 0"), NarrationWord(0, Flags), 0u);
	TestEqual(TEXT("step 1: 2"), NarrationWord(1, Flags), 2u);
	TestEqual(TEXT("step 2: 2"), NarrationWord(2, Flags), 2u);
	TestEqual(TEXT("step 2 again: 0"), NarrationWord(2, Flags), 0u);
	TestEqual(TEXT("passed: 8"), NarrationWord(NarrPassed, Flags), 8u);
	TestEqual(TEXT("failed: 0x10 every time"), NarrationWord(NarrFailed, Flags), 0x10u);
	TestEqual(TEXT("flags 2 / 4 / 8"), Flags, 0xEu);
	TestEqual(TEXT("FUN_00164110(6) = 30"), FirstLesson(6), 30);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLessonTrickTest, "SSX.Lesson.TrickTest", LsFlags)
bool FSSXLessonTrickTest::RunTest(const FString& Parameters)
{
	const SSXFeTrickBook::FTable& T = SSXFeTrickBook::FTable::Get();
	if (!T.IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("extracted/DATA/TUTORIAL/TRICKDEF.DAT missing: lesson trick test skipped"));
		return true;
	}
	FLessonMan M;
	FRecEnv E;
	ToWait(M, E, 0);
	Step(M, E, PadCross);
	for (int32 I = 0; I < 3; ++I) Step(M, E, PadRight);
	Pick(M, E, 1);
	TestEqual(TEXT("lesson 4 in Perform"), M.Lesson, 3);
	const SSXFeTrickBook::FEntry* Own = T.Find(0, 3);
	const SSXFeTrickBook::FEntry* Other = T.Find(0, 4);
	if (!TestNotNull(TEXT("entries"), Own) || !TestNotNull(TEXT("entries"), Other)) return false;
	if (!SSXFeTrickBook::Matches(*Own, Other->W0, Other->W1))
	{
		TestFalse(TEXT("another lesson's trick: no"), LessonBookCheck(M, 0, Other->W0, Other->W1));
		TestFalse(TEXT("not passed"), M.bPassed);
	}
	TestFalse(TEXT("flip count changed: no"), LessonBookCheck(M, 0, Own->W0, Own->W1 ^ 0x01000000u));
	TestFalse(TEXT("still not passed"), M.bPassed);
	// prefix / suffix / spin direction / link bits do not
	TestTrue(TEXT("the lesson's own trick (other word-1 bits changed): passed"), LessonBookCheck(M, 0, Own->W0, Own->W1 ^ 0x000000F0u));
	TestTrue(TEXT("+100 = 1"), M.bPassed);
	TestTrue(TEXT("FinishLine win test in mode 6 = the lesson flag"), SSXFinishLine::IsWinner(6, 0, 1, 3, 1000, 0, M.bPassed));
	TestFalse(TEXT("... and not without it"), SSXFinishLine::IsWinner(6, 0, 1, 3, 1000, 0, false));
	M.EndTrigger();
	Step(M, E);
	E.Narr.Reset();
	Step(M, E);
	TestTrue(TEXT("End: passed narration"), M.State == EState::End && E.Narr == TArray<int32>{ NarrPassed });
	// FUN_0014B768
	Step(M, E, PadDown);
	Step(M, E, PadCross);
	Step(M, E);
	TestFalse(TEXT("restart: +100 cleared"), M.bPassed);
	FSSXProfile P = FSSXProfile::MakeDefault();
	TestEqual(TEXT("no book bits"), P.Career[0].TrickBits(), 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLessonOverlaysTest, "SSX.Lesson.Overlays", LsFlags)
bool FSSXLessonOverlaysTest::RunTest(const FString& Parameters)
{
	// FUN_0014BD98
	FInpFrame F;
	F.RecordedBools = 1u << (FPadMapper::ActCameraReverse - FPadMapper::FirstBoolAction);
	const uint32 Pro = DiagramMask(F, [](int32 A) { return FPadMapper::ActionMask(A, 1, 0, 0); });
	const uint32 Def = DiagramMask(F, [](int32 A) { return FPadMapper::ActionMask(A, 0, 0, 0); });
	TestEqual(TEXT("Pro: camera reverse = Triangle"), Pro & 0xFFFFu, uint32(0x20));
	TestTrue(TEXT("Default: camera reverse is NONE, ORed as is (every glyph)"), (Def & 0x3FFFu) == 0x3FFFu);
	F.RecordedBools = 1u;
	TestEqual(TEXT("X held"), DiagramMask(F, [](int32 A) { return FPadMapper::ActionMask(A, 0, 0, 0); }) & 0xFFFFu, uint32(0x800));
	F.Pad.ButtonsFiltered = 0x8u | 0x10u;
	TestEqual(TEXT("d-pad bits of the frame"), DiagramMask(F, [](int32 A) { return FPadMapper::ActionMask(A, 0, 0, 0); }) & 0xFFFFu, uint32(0x808));
	F = FInpFrame();
	int32 Slot = -1;
	for (int32 K = 0; K < FPadMapper::GrabCount && Slot < 0; ++K) { if (FPadMapper::ActionMask(K, 0, 0, 0) != 0xFFFFFFFFu) Slot = K; }
	if (Slot >= 0)
	{
		F.RecordedGrabs = 1u << Slot;
		TestEqual(TEXT("grab slot buttons"), DiagramMask(F, [](int32 A) { return FPadMapper::ActionMask(A, 0, 0, 0); }),
			FPadMapper::ActionMask(Slot, 0, 0, 0));
	}

	const FSSXLocStrings& Loc = LsLoc();
	if (!Loc.IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("LOC data missing: lesson overlay pages skipped"));
		return true;
	}
	SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
	FSSXProfile P = FSSXProfile::MakeDefault();
	P.Slot[0].Char = 0;
	FSSXCheats C;
	FSSXRaceUIHostStub H;
	H.Mode = 6;
	FSSXOverlayManager O;
	O.Init(&P, &C, &Loc);
	O.SetHost(&H);
	FSSXFeDrawList D;
	FLessonMan M;
	FRecEnv E;
	H.Lesson = &M;
	ToWait(M, E, 0);
	auto Redraw = [&]() { D.Reset(); O.Draw(D); };
	Redraw();
	const FString Title = Loc.ByIndex(0xEA8).Replace(TEXT("%S"), SSXFeTables::RiderName(0));
	TestTrue(TEXT("Wait: title with the rider"), HasText(D, Title));
	TestTrue(TEXT("Wait: chapter 1"), HasText(D, Loc.ByIndex(0xE6A).Replace(TEXT("%d"), TEXT("1"))));
	TestTrue(TEXT("Wait: trick 1"), HasText(D, Loc.ByIndex(0xEA7).Replace(TEXT("%d"), TEXT("1"))));
	TestTrue(TEXT("Wait: 'Select chapter.'"), HasText(D, Loc.ByIndex(0xDD9)));
	TestTrue(TEXT("Wait: the menu 'select'"), HasText(D, Loc.ByIndex(LocSelect)));
	TestFalse(TEXT("Wait: the never drawn 'Tricks Completed'"), HasTextPrefix(D, Loc.ByIndex(0xE6B).Left(3)));
	TestFalse(TEXT("lesson 1 not done: no '(completed)'"), HasText(D, Loc.ByIndex(0xC93)));
	for (int32 K = 0; K < 5; ++K)
	{
		const FSSXFeDrawCmd* T = FindSprite(D, 0x7C + K);
		if (TestNotNull(FString::Printf(TEXT("thumbnail %d"), K), T))
		{
			static const float Fx[5] = { 0.166f, 0.333f, 0.5f, 0.666f, 0.833f };
			TestTrue(FString::Printf(TEXT("thumbnail %d at the .data 0x36E508 slot, y 215, 90 x 70"), K),
				FMath::IsNearlyEqual(T->Pos.X, Fx[K] * 640.f - 45.f, 0.5f) && T->Pos.Y == 215.f && T->Size.X == 90.f && T->Size.Y == 70.f);
		}
	}
	TestEqual(TEXT("chapter arrows: right only at chapter 1"), CountSprite(D, SSXFe::HudArrowL) * 10 + CountSprite(D, SSXFe::HudArrowR), 1);
	{
		// FUN_00164060, 0x14DCC0, 0x14C2D0, FUN_00164258
		Step(M, E, PadRight);
		Redraw();
		const FString Ch2 = Loc.ByIndex(0xE6A).Replace(TEXT("%d"), TEXT("2"));
		const FSSXFeTextRec* Rec = nullptr;
		for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == Ch2) Rec = &R; }
		TestTrue(TEXT("locked chapter 2: 'Chapter 2' drawn"), HasText(D, Ch2));
		if (Rec)
		{
			TestTrue(TEXT("locked chapter 2: title grey (1, .5, .5, .5)"), FMath::IsNearlyEqual(Rec->Color.R, 0.5f, 0.01f)
				&& FMath::IsNearlyEqual(Rec->Color.G, 0.5f, 0.01f) && FMath::IsNearlyEqual(Rec->Color.B, 0.5f, 0.01f));
		}
		int32 Thumbs = 0;
		for (int32 K = 0; K < 5; ++K) { Thumbs += FindSprite(D, 0x7C + K) ? 1 : 0; }
		TestEqual(TEXT("locked chapter 2: all five thumbnails drawn"), Thumbs, 5);
		const FSSXFeDrawCmd* T0 = FindSprite(D, 0x7C);
		TestTrue(TEXT("locked chapter 2: thumbnails grey"), T0 && FMath::IsNearlyEqual(T0->Color.G, 0.5f, 0.01f));
		Step(M, E, PadCross);
		TestTrue(TEXT("locked chapter 2: trick level reached (no gate)"), M.MenuLevel == 1 && M.Lesson == 5);
		Step(M, E, PadCross);
		TestTrue(TEXT("locked chapter 2: action menu reached (no gate)"), M.MenuLevel == 2);
		Step(M, E, PadTriangle);
		Step(M, E, PadTriangle);
		Step(M, E, PadLeft);
		TestTrue(TEXT("back at chapter 1"), M.MenuLevel == 0 && M.Chapter == 0);
	}
	{
		int32 Missing = 0, Folders = 0;
		for (int32 Ch = 0; Ch < SSXFeTrickBook::Chapters; ++Ch)
		{
			const FString Dir = FPaths::Combine(SSXFrontEnd::TextureDir(), SSXFeTrickBook::TrickFolder(0, Ch));
			if (!FPaths::DirectoryExists(Dir)) continue;
			++Folders;
			for (int32 T = 0; T < SSXFeTrickBook::TricksPerChapter; ++T)
			{
				TArray<FString> Found;
				IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, SSXFeTrickBook::TrickImagePattern(T)), true, false);
				Missing += Found.Num() == 1 ? 0 : 1;
			}
		}
		if (Folders == 0) SSXTest::Skip(*this, TEXT("converted TB_ textures missing: thumbnail binding check skipped"));
		TestEqual(TEXT("every chapter's thumbnails 0..4 resolve by index"), Missing, 0);
	}
	P.Career[0].BookDone[0] = 1u;
	Step(M, E, PadCross);
	Redraw();
	TestTrue(TEXT("done lesson: '(completed)'"), HasText(D, Loc.ByIndex(0xC93)));
	TestTrue(TEXT("trick level: 'Select trick.' and back"), HasText(D, Loc.ByIndex(0xDDA)) && HasText(D, Loc.ByIndex(0x1D7)));
	if (SSXFeTrickBook::FTable::Get().IsLoaded())
	{
		TestTrue(TEXT("the trick's name"), HasText(D, SSXFeTrickBook::FTable::Get().TrickName(0, 0)));
	}
	Step(M, E, PadCross);
	Redraw();
	TestTrue(TEXT("action menu items"), HasText(D, Loc.ByIndex(LocShowMe)) && HasText(D, Loc.ByIndex(LocPerform))
		&& HasText(D, Loc.ByIndex(LocFreeJump)) && HasText(D, Loc.ByIndex(LocQuit)));
	for (int32 I = 0; I < 3; ++I) Step(M, E, PadDown);
	Step(M, E, PadCross);
	Redraw();
	TestTrue(TEXT("quit box: 'Are you sure?', yes, no"), HasText(D, Loc.ByIndex(LocAreYouSure)) && HasText(D, Loc.ByIndex(LocYes))
		&& HasText(D, Loc.ByIndex(LocNo)));
	TestFalse(TEXT("quit box hides the menu"), HasText(D, Loc.ByIndex(LocShowMe)));
	Step(M, E, PadTriangle);
	Step(M, E, PadUp);
	Step(M, E, PadUp);
	Step(M, E, PadCross);
	Step(M, E);
	TestEqual(TEXT("Perform"), int32(M.State), int32(EState::Perform));
	Redraw();
	TestTrue(TEXT("diagram base sprite 0x60 at (20, 20)"), FindSprite(D, DiagramBaseSprite) && FindSprite(D, DiagramBaseSprite)->Pos == FVector2f(20.f, 20.f));
	TestEqual(TEXT("no glyph before frame 121"), CountSprite(D, 0x85), 0);
	TestFalse(TEXT("overlay 10 closed in Perform"), HasText(D, Title));
	E.InpLen = 200;
	M.Inp.SetNum(200);
	M.InpFrames = 200;
	for (int32 I = 0; I < 200; ++I) M.Inp[I].RecordedBools = 1u;
	for (int32 I = 0; I < 130; ++I) M.AfterPad();
	Redraw();
	const FSSXFeDrawCmd* X = FindSprite(D, 0x85);
	TestTrue(TEXT("X glyph 0x85 at (20 + 194, 20 + 82)"), X && X->Pos == FVector2f(214.f, 102.f));
	TestEqual(TEXT("only X lit"), CountSprite(D, 0x84) + CountSprite(D, 0x86) + CountSprite(D, 0x83), 0);
	H.TrickLayout = 1;
	for (int32 I = 0; I < 200; ++I) M.Inp[I].RecordedBools = 1u << (FPadMapper::ActCameraReverse - FPadMapper::FirstBoolAction);
	Redraw();
	TestTrue(TEXT("Pro layout: camera reverse lights Triangle 0x84 only"), CountSprite(D, 0x84) == 1 && CountSprite(D, 0x85) == 0);
	H.TrickLayout = 0;
	Redraw();
	TestEqual(TEXT("Default layout: NONE lights all 14 glyphs"), CountSprite(D, 0x7F) + CountSprite(D, 0x81) + CountSprite(D, 0x7C) + CountSprite(D, 0x7B), 4);
	M.EndTrigger();
	Step(M, E);
	Step(M, E);
	Redraw();
	const FString Fail = Loc.ByIndex(0x22E), Pass = Loc.ByIndex(0x22F);
	TestTrue(TEXT("End failed: 'Sorry, ...'"), HasTextPrefix(D, Fail.Left(6)) && !HasTextPrefix(D, Pass.Left(6)));
	TestTrue(TEXT("End menu: new trick / restart / quit"), HasText(D, Loc.ByIndex(LocNewTrick)) && HasText(D, Loc.ByIndex(LocRestart)));
	M.bPassed = true;
	Redraw();
	TestTrue(TEXT("End passed: 'Great! ...'"), HasTextPrefix(D, Pass.Left(6)));
	// FUN_00196218
	TestTrue(TEXT("End text box keeps the original's double space"), Pass.StartsWith(TEXT("Great!  ")) && HasTextPrefix(D, TEXT("Great!  You")));
	TestTrue(TEXT("End: 'trick %d' title and the menu's 'quit'"), HasText(D, Loc.ByIndex(0xEA7).Replace(TEXT("%d"), *FString::FromInt(M.Lesson + 1)))
		&& HasText(D, Loc.ByIndex(LocQuit)));
	O.Activate(OvPauseMenu);
	Redraw();
	TestFalse(TEXT("pause: no lesson page"), HasTextPrefix(D, Pass.Left(6)));
	O.Deactivate(OvPauseMenu);
	{
		FLessonMan Pm;
		FRecEnv Pe;
		H.Lesson = &Pm;
		ToWait(Pm, Pe, 0);
		Pick(Pm, Pe, 2);
		Pm.EndTrigger();
		Step(Pm, Pe);
		Step(Pm, Pe);
		Redraw();
		TestTrue(TEXT("PracticeEnd: 'Your trick:' + the rand word"), HasText(D, Loc.ByIndex(0xEBB)) && HasText(D, Loc.ByIndex(Pm.PracticeLoc)));
		Pm.RiderTrickText = TEXT("Backside 360 Mute");
		Redraw();
		TestTrue(TEXT("PracticeEnd: the rider's trick text"), HasText(D, TEXT("Backside 360 Mute")) && !HasText(D, Loc.ByIndex(Pm.PracticeLoc)));
	}
	H.Lesson = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLessonLoaderTest, "SSX.Lesson.Loader", LsFlags)
bool FSSXLessonLoaderTest::RunTest(const FString& Parameters)
{
	FSSXFeLaunch L;
	L.GameMode = 6;
	L.Track = SSXFeTables::TrickTutorialTrack;
	L.Slots[0].Char = 0;
	TestEqual(TEXT("mode 6 -> cTrickTutorialLoad"), int32(SSXTrackLoad::KindFor(L)), int32(SSXTrackLoad::EKind::Tutorial));
	FSSXFeLaunch R = L;
	R.GameMode = 2;
	TestEqual(TEXT("mode 2 -> c1PTrackLoad"), int32(SSXTrackLoad::KindFor(R)), int32(SSXTrackLoad::EKind::OneP));

	const FSSXLocStrings& Loc = LsLoc();
	const FSSXLocStrings* Lp = Loc.IsLoaded() ? &Loc : nullptr;
	FSSXProfile P = FSSXProfile::MakeDefault();
	P.Settings.Controller[0] = 0;
	FSSXTrackLoader T;
	T.Begin(L, &P, Lp);
	TestEqual(TEXT("kind"), int32(T.Kind), int32(SSXTrackLoad::EKind::Tutorial));
	auto HasLoc = [&T](int32 Id) { for (const FSSXTrackLoadPage::FTutorialText& X : T.Page.TutorialTexts) { if (X.Loc == Id) return true; } return false; };
	TestTrue(TEXT("Default: LOC 0xD28 centred at (320, 374)"), T.Page.TutorialTexts.Num() > 0 && T.Page.TutorialTexts[0].Loc == 0xD28
		&& T.Page.TutorialTexts[0].Align == 2 && T.Page.TutorialTexts[0].X == 320.f && T.Page.TutorialTexts[0].Y == 374.f);
	TestTrue(TEXT("Default set: 0xC7C .. 0xC82, 0xDE2; no Pro labels"), HasLoc(0xC7C) && HasLoc(0xC82) && HasLoc(0xDE2) && !HasLoc(0xC81) && !HasLoc(0xC8E));
	TestEqual(TEXT("Default: 7 + 1 + 7 labels"), T.Page.TutorialTexts.Num(), 15);
	if (Lp) { TestEqual(TEXT("title LOC 0xE80"), T.Page.TutorialTitle, Loc.ByIndex(0xE80)); }
	TestTrue(TEXT("no intro queue: the intro is over at once"), T.bIntroDone());
	TestTrue(TEXT("page music (vfunc_05 FUN_00214910) and an 8 s stop fade"), T.HasPageMusic() && T.MusicStopFade() == 8.f);
	T.Start();
	TestTrue(TEXT("Run, music started"), T.Phase == FSSXTrackLoader::EPhase::Run && T.MusicStarts == 1);
	for (int32 I = 0; I < 200; ++I) T.Step(false, 7);
	TestEqual(TEXT("PC rule: stays while not loaded"), int32(T.Phase), int32(FSSXTrackLoader::EPhase::Run));
	TestFalse(TEXT("skip refused before the load"), T.RequestSkip(false));
	TestEqual(TEXT("music still on at app+8 = 7"), T.MusicStops, 0);
	T.Step(false, 8);
	TestEqual(TEXT("app+8 >= 8 stops the voice"), T.MusicStops, 1);
	T.Step(true, 10);
	TestEqual(TEXT("loaded (intro done): Finish"), int32(T.Phase), int32(FSSXTrackLoader::EPhase::Finish));
	int32 N = 0;
	while (T.Phase != FSSXTrackLoader::EPhase::Done && N < 100) { T.Step(true, 10); ++N; }
	TestEqual(TEXT("fades to black and ends"), int32(T.Phase), int32(FSSXTrackLoader::EPhase::Done));

	P.Settings.Controller[0] = 1;
	FSSXTrackLoader Tp;
	Tp.Begin(L, &P, Lp);
	auto HasLocP = [&Tp](int32 Id) { for (const FSSXTrackLoadPage::FTutorialText& X : Tp.Page.TutorialTexts) { if (X.Loc == Id) return true; } return false; };
	TestTrue(TEXT("Pro: LOC 0xD29, 0xC84 .. 0xC89, 0xC81, 0xC83, 0xC8E"), Tp.Page.TutorialTexts[0].Loc == 0xD29 && HasLocP(0xC84) && HasLocP(0xC81)
		&& HasLocP(0xC83) && HasLocP(0xC8E) && !HasLocP(0xDE2) && !HasLocP(0xC7C));
	TestEqual(TEXT("Pro: 9 + 1 + 7 labels"), Tp.Page.TutorialTexts.Num(), 17);
	Tp.Start();
	FSSXFeDrawList D;
	Tp.Draw(D);
	if (Lp)
	{
		TestTrue(TEXT("draw: title and the configuration line"), D.Texts.Contains(Loc.ByIndex(0xE80)) && D.Texts.Contains(Loc.ByIndex(0xD29)));
	}
	return true;
}

#endif
