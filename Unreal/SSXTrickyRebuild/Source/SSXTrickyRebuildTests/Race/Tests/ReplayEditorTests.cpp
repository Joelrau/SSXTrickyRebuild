// 0x1EB6D0, 0x190C88, 0x190C40, 0x190CB8, 0x1921E0, 0x195510, 0x1954B8, 0x1955E0, 0x191D88, 0x1ED490, 0x1EC728
#include "Misc/AutomationTest.h"
#include "Race/SSXReplay.h"
#include "UI/SSXReplayControl.h"
#include "UI/SSXFeTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags EdTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayEditorControlTest, "SSX.Replay.EditorControl", EdTestFlags)
bool FSSXReplayEditorControlTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	using namespace SSXReplay;
	using EE = SSXReplayCtl::EEvent;
	FSSXReplay R;
	R.Init(2, 1, true, nullptr);
	R.AttachHuman(0);
	R.OnStartRace();
	for (int32 I = 0; I < 1000; ++I) R.Update();
	R.StartFullReplay();
	FSSXReplayControl Ctl;
	Ctl.Activate(R, 0, true);
	SSXReplayCtl::FEnv Env;
	Env.CameraMode = []() { return 7; };
	Env.Racers = 2;
	uint32 Prev = 0;
	auto Frame = [&](uint32 Held) { Ctl.Events.Reset(); R.Update(); Ctl.Update(R, Held, Prev, Env); Prev = Held; };
	auto Press = [&](uint32 Bits) { Frame(Bits); const TArray<EE> Ev = Ctl.Events; const int32 Cmd = Ctl.LastCommand; Frame(0); Ctl.Events = Ev; Ctl.LastCommand = Cmd; };
	auto Settle = [&]() { for (int32 I = 0; I < 4; ++I) Frame(0); };
	Settle();
	TestTrue(TEXT("paused at frame 0"), R.GetState() == Paused && R.GetPlayFrame() == 0);
	TestTrue(TEXT("time line data: length, window = length / 1 below 1800 frames"), Ctl.Timeline.Length == R.GetRecordedLength() && Ctl.WinLen == int32(R.GetRecordedLength()) && Ctl.WinStep == 1);

	// 0x194D78
	Press(SSXReplayCtl::PadL3);
	TestTrue(TEXT("0x8000 at frame 0: overwrite only, silent"), !Ctl.Events.Contains(EE::Navigate) && Ctl.MessageLoc == 0 && R.GetTrack(0).Count == 1);
	Press(PadCircle);
	TestTrue(TEXT("Circle at frame 0: LOC 0x15B"), Ctl.MessageLoc == 0x15B && Ctl.Events.Contains(EE::Navigate));
	for (int32 I = 0; I < 3; ++I) { Press(PadR1); Settle(); }
	const uint32 F3 = R.GetPlayFrame();
	TestTrue(TEXT("R1 steps frames"), F3 == 3 && R.GetState() == Paused);
	Press(SSXReplayCtl::PadL3);
	TestTrue(TEXT("0x8000 at frame 3: a new cut, navigate + LOC 0x149"), Ctl.MessageLoc == 0x149 && Ctl.Events.Contains(EE::Navigate) && R.GetTrack(0).Count == 2 && R.GetTrack(0).Cuts[1].Frame == 3);
	Ctl.MessageLoc = 0;
	Press(SSXReplayCtl::PadL3);
	TestTrue(TEXT("0x8000 again on the same frame: overwritten, silent"), Ctl.MessageLoc == 0 && !Ctl.Events.Contains(EE::Navigate) && R.GetTrack(0).Count == 2);
	Press(PadCircle);
	TestTrue(TEXT("Circle at frame 3: the cut deleted, LOC 0x14F"), Ctl.MessageLoc == 0x14F && R.GetTrack(0).Count == 1);
	{
		// 0x1951D0
		FSSXReplay N;
		N.Init(2, 1, false, nullptr);
		N.AttachHuman(0);
		N.OnStartRace();
		for (int32 I = 0; I < 200; ++I) N.Update();
		N.StartFullReplay();
		FSSXReplayControl NC;
		NC.Activate(N, 0, true);
		uint32 NP = 0;
		for (int32 I = 0; I < 4; ++I) { N.Update(); NC.Update(N, 0, NP, Env); }
		N.Update();
		NC.Events.Reset();
		NC.Update(N, SSXReplayCtl::PadL3, 0, Env);
		TestTrue(TEXT("0x190C88 false (Replay setting Normal): navigate + LOC 0x153"), NC.MessageLoc == 0x153 && NC.Events.Contains(EE::Navigate));
	}

	// 0x1921E0
	Press(PadL1 | PadL2);
	Frame(PadL1);
	Frame(PadL1 | PadL2);
	Settle();
	TestEqual(TEXT("back at frame 0"), R.GetPlayFrame(), 0u);
	const auto MarkClip = [&](int32 Frames)
	{
		Press(PadTriangle);
		const bool bCmd = Ctl.LastCommand == CmdClip && Ctl.bMarking;
		for (int32 I = 0; I < Frames; ++I) Frame(0);
		Press(PadCross);
		Settle();
		return bCmd;
	};
	TestTrue(TEXT("Triangle: clip mark command 8, marking on"), MarkClip(30));
	TestEqual(TEXT("one focus clip"), R.GetRiderClips(0).Num(), 1);
	const auto StepTo = [&](uint32 F) { for (int32 I = 0; I < 100 && R.GetPlayFrame() < F; ++I) { Press(PadR1); Settle(); } };
	StepTo(60);   // 0x1922A8
	TestEqual(TEXT("R1 frame steps to 60"), R.GetPlayFrame(), 60u);
	MarkClip(20);
	const TArray<int32>& Clips = R.GetRiderClips(0);
	const auto Start = [&](int32 C) { return R.GetKey(R.GetClip(C).Key).Frame; };
	TestTrue(TEXT("two focus clips from 0 and 60"), Clips.Num() == 2 && Start(Clips[0]) == 0 && Start(Clips[1]) == 60);
	{
		FSSXReplay::FTimeline T;
		R.GetTimeline(T);
		int32 Zeros = 0;
		for (int32 K = 1; K < T.KeyCount && K < 16; ++K) Zeros += T.Keys[K] == 0 ? 1 : 0;
		TestTrue(TEXT("0x191D88: the clip-referenced keyframe at 60 leaves its slot 0; clips and track of the focus rider"),
			Zeros >= 1 && T.Clips == Clips && T.Track == &R.GetTrack(0));
	}
	TestEqual(TEXT("0x1921E0 with room: allowed"), R.ClipMarkAllowed(), 1);

	// 0x195510, 0x1954B8
	StepTo(120);
	TestEqual(TEXT("at frame 120"), R.GetPlayFrame(), 120u);
	Press(PadLJoyLeft);
	TestTrue(TEXT("L at 120: previous clip (10), index 1 (start 60 < 120)"), Ctl.LastCommand == CmdPrevClip && R.GetPlaylist().Num() == 2 && R.GetPlaylistIndex() == 1 && Ctl.List430Cur == 1);
	Settle();
	TestEqual(TEXT("command 10 rewinds to the clip start 60"), R.GetPlayFrame(), 60u);
	Press(PadLJoyRight);
	TestTrue(TEXT("R at 60: no clip starts later: no command, the playlist kept"), Ctl.LastCommand == 0 && R.GetPlaylist().Num() == 2);
	Press(PadLJoyLeft);
	TestTrue(TEXT("L at 60: a start equal to the frame -> the clip before (index 0)"), Ctl.LastCommand == CmdPrevClip && R.GetPlaylistIndex() == 0);
	Settle();
	TestEqual(TEXT("at the first clip"), R.GetPlayFrame(), 0u);
	Press(PadLJoyRight);
	TestTrue(TEXT("R at 0: the first clip starting later (index 1), command 9"), Ctl.LastCommand == CmdNextClip && R.GetPlaylistIndex() == 1);
	Settle();
	Press(PadLJoyUp);
	TestTrue(TEXT("U: play the clips not yet over (command 6, index 0)"), Ctl.LastCommand == CmdPlayToggle && R.GetPlaylistIndex() == 0 && R.GetPlaylist().Num() >= 1);

	// 0x1ED490
	FSSXReplayControl W;
	W.Timeline.Length = 4000;
	W.WinLen = 1800;
	W.WinStep = 180;
	TestEqual(TEXT("past the window: start = play - (window - step), slide 1 - step"), W.ScrollWindow(1900), 280 - 179);
	TestTrue(TEXT("+0x464 / +0x474"), W.WinStart == 280 && W.Slide == -179);
	W.Slide = 0;
	TestEqual(TEXT("before the window: start = play - step, slide step - 1"), W.ScrollWindow(200), 20 + 179);
	TestEqual(TEXT("inside: unchanged"), (W.Slide = 0, W.ScrollWindow(500)), 20);
	TestEqual(TEXT("clamped to length - window"), W.ScrollWindow(3990), 2200);
	TestTrue(TEXT("clamp clears the slide"), W.Slide == 0 && W.WinStart == 2200);
	W.Timeline.Length = 1000;
	W.WinLen = 1000;
	W.WinStep = 1;
	TestEqual(TEXT("short replay: the window is the whole replay"), W.ScrollWindow(700), 0);

	// 0x1EC728
	Settle();
	Frame(PadUp);
	TestEqual(TEXT("Up: help level 2"), Ctl.HelpLevel, 2);
	Frame(0);
	TestTrue(TEXT("one frame: 354, sliding"), Ctl.PanelY == 354.f && Ctl.bHelpChanged);
	for (int32 I = 0; I < 50; ++I) Frame(0);
	TestTrue(TEXT("settled at 105, +0x4A4 cleared"), Ctl.PanelY == 105.f && !Ctl.bHelpChanged);
	return true;
}

#endif
