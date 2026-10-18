// 0x113E10, 0x1141E0, 0x106600, 0x106668, 0x1065C8
#include "Misc/AutomationTest.h"
#include "Race/EndRaceHandler.h"
#include "Race/FinishLineControl.h"
#include "Audio/SSXAnnouncer.h"
#include "Audio/SSXSpeech.h"
#include "Audio/SSXSpeechSystem.h"
#include "Sim/RiderSim.h"
#include "Sim/Anim/RiderAnim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags EndTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	class FEndEnv final : public SSXIntro::IEnv
	{
	public:
		int32 Busy = 0;
		int32 Skips = 0;
		TArray<FString> Log;
		virtual bool DirectorRunning() const override { return Busy > 0; }
		virtual bool SkipPressed() override { if (Skips > 0) { --Skips; return true; } return false; }
		virtual void Act(const FString& Name, int32 Arg, const FString& Text) override
		{
			if (Name == TEXT("loading_service")) return;
			Log.Add(Text.IsEmpty() ? FString::Printf(TEXT("%s:%d"), *Name, Arg) : FString::Printf(TEXT("%s:%s"), *Name, *Text));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEndRaceScenesTest, "SSX.Race.EndRace.Scenes", EndTestFlags)
bool FSSXEndRaceScenesTest::RunTest(const FString& Parameters)
{
	using namespace SSXEndRace;
	// 0x113E10
	TestEqual(TEXT("no score above 2"), RivalScene({ { 2, 1 }, { 0, 2 } }), NoScene);
	TestEqual(TEXT("their score 3 -> FL_*_VS_USR slot 10 + k"), RivalScene({ { 0, 0 }, { 3, 0 } }), 11);
	TestEqual(TEXT("my score -> FL_USR_VS_* slot 5 + k"), RivalScene({ { 0, 4 }, { 3, 0 } }), 5);
	TestEqual(TEXT("theirs wins a tie with mine (tested first)"), RivalScene({ { 4, 4 } }), 10);
	TestEqual(TEXT("a later equal score does not replace"), RivalScene({ { 3, 0 }, { 3, 0 } }), 10);
	TestEqual(TEXT("race: top half wins"), ResultScene(2, 2, 6, 3, 0, 0), SceneWin);
	TestEqual(TEXT("race: 4th of 6 loses"), ResultScene(4, 3, 6, 3, 0, 0), SceneLose);
	TestEqual(TEXT("showoff: medal wins"), ResultScene(5, 5, 1, 2, 0, 0), SceneWin);
	TestEqual(TEXT("showoff: no medal loses"), ResultScene(3, 0, 1, 3, 0, 0), SceneLose);
	TestEqual(TEXT("time challenge: beat the time"), ResultScene(7, 0, 1, 3, 9000, 9500), SceneWin);
	TestEqual(TEXT("time challenge: slower"), ResultScene(7, 0, 1, 3, 9600, 9500), SceneLose);

	SSXIntro::FSlot Slots[NumSlots];
	{
		FEndEnv Env;
		FHandler H;
		H.Enter(Env, Slots, NoScene, SceneWin);
		TestEqual(TEXT("enter actions"), Env.Log, TArray<FString>{ TEXT("overlay_close:0"), TEXT("overlay_close:7"), TEXT("director_reset:0"),
			TEXT("director_play:PreLoad") });
		Env.Log.Reset();
		TestEqual(TEXT("Null scenes: results in one update"), H.Update(Env), ResultsShown);
		TestEqual(TEXT("state 8 / 10 actions"), Env.Log, TArray<FString>{ TEXT("fade_out:1000"), TEXT("intro_speech_end:0"),
			TEXT("post_race_chain:0"), TEXT("director_reset:0"), TEXT("director_play:PreLoad"), TEXT("replay_state:9"),
			TEXT("board_voices_restart:0") });
		TestTrue(TEXT("chain open"), H.IsChainOpen());
	}
	Slots[11].Type = 33 + 12 + 6;
	Slots[SceneWin].Type = 27;
	{
		FEndEnv Env;
		FHandler H;
		H.Enter(Env, Slots, 11, SceneWin);
		Env.Busy = 3;
		Env.Log.Reset();
		TestEqual(TEXT("scene 1 plays and waits"), H.Update(Env), 3);
		TestTrue(TEXT("scene 1 script"), Env.Log.Contains(TEXT("director_play:FL_JP_VS_USR")));
		TestTrue(TEXT("fade-in 1 s"), Env.Log.Contains(TEXT("fade_in:1000")));
		--Env.Busy; H.Update(Env); --Env.Busy; H.Update(Env); --Env.Busy;
		Env.Busy = 0;
		Env.Log.Reset();
		const int32 S = H.Update(Env);
		TestTrue(TEXT("scene 2 script"), Env.Log.Contains(TEXT("director_play:FL_WIN_1")));
		TestEqual(TEXT("director idle at the check: scene 2 ends in the same update"), S, ResultsShown);
	}
	{
		FEndEnv Env;
		FHandler H;
		H.Enter(Env, Slots, 11, SceneWin);
		Env.Busy = 100;
		H.Update(Env);
		Env.Skips = 1;
		Env.Log.Reset();
		TestEqual(TEXT("skip in scene 1 skips scene 2"), H.Update(Env), ResultsShown);
		TestFalse(TEXT("scene 2 never played"), Env.Log.Contains(TEXT("director_play:FL_WIN_1")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEndRaceFinishLineTest, "SSX.Race.EndRace.FinishLine", EndTestFlags)
bool FSSXEndRaceFinishLineTest::RunTest(const FString& Parameters)
{
	using namespace SSXFinishLine;
	TestTrue(TEXT("race top half"), IsWinner(4, 2, 6, 3, 0, 0, false));
	TestFalse(TEXT("race rank 3 of 6"), IsWinner(4, 3, 6, 3, 0, 0, false));
	TestTrue(TEXT("tutorial: lesson flag"), IsWinner(6, 5, 1, 3, 0, 0, true));

	FRiderSim Sim;
	Sim.State.Velocity = FVector(1000.0, 0.0, 0.0);
	Sim.State.BoardClass = 0;
	const TSharedRef<SSXAnim::FRiderAnimChannel> Ch = SSXAnim::AcquireChannel(Sim);
	FControl C;
	int32 Frame = 500;
	C.RaceFrame = [&Frame]() { return Frame; };
	C.IsWinner = [](const FRiderSim&) { return true; };
	C.Enter(Sim, ERiderControl::Cruise);
	TestEqual(TEXT("fast: phase 0"), C.GetPhase(), 0);
	TArray<FIntPoint> Plays;
	int32 DoneAt = -1, Phase3At = -1;
	auto Step = [&]()
	{
		C.Update(Sim, FRiderInputWord());
		for (const int32 Id : Ch->PlayRequests) { Plays.Add(FIntPoint(C.GetFrames(), Id)); }
		Ch->PlayRequests.Reset();
		if (Phase3At < 0 && C.GetPhase() >= 3) { Phase3At = C.GetFrames(); }
		if (DoneAt < 0 && C.IsDone()) { DoneAt = C.GetFrames(); }
		for (FApproachFilter* F : { &Sim.State.Brake, &Sim.State.Steer, &Sim.State.Crouch }) F->Tick(1.0 / 60.0);
		++Frame;
	};
	Step();
	TestEqual(TEXT("brake target 1"), Sim.State.Brake.Target, 1.0);
	int32 N = 0;
	while (C.GetPhase() == 0 && N++ < 400) Step();
	TestEqual(TEXT("phase 1 once the brake is full"), C.GetPhase(), 1);
	TestTrue(TEXT("left phase 0 by the brake, before the limit"), C.GetFrames() < WaitLimit);
	const double V0 = Sim.State.Velocity.X;
	Step();
	if (C.GetPhase() == 1)
	{
		TestTrue(TEXT("velocity decays by 0.93"), FMath::IsNearlyEqual(Sim.State.Velocity.X, V0 * VelocityDecay, 1e-6));
	}
	while (C.GetPhase() == 1 && N++ < 800) Step();
	TestEqual(TEXT("phase 2 when the filters are back at 0"), C.GetPhase(), 2);
	TestTrue(TEXT("stopped"), Sim.State.Velocity.IsZero());
	const int32 StopAt = C.GetFrames();
	TestTrue(TEXT("R_CRUISE2FINISH 0x2EE requested at the stop"), Plays.Num() == 1 && Plays[0] == FIntPoint(StopAt, FinishAnim));
	while (C.GetPhase() == 2 && N++ < 800) Step();
	// 0x15FF40
	const int32 ClipFrames = SSXAnim::OneShotFrames(SSXAnim::FAnimTable::Shared().Frames(FinishAnim, 0, 0));
	TestEqual(TEXT("0x2EE: 20 frames for board class 0"), SSXAnim::FAnimTable::Shared().Frames(FinishAnim, 0, 0), 20);
	TestEqual(TEXT("phase 2 lasts the 0x2EE clip"), C.GetFrames() - StopAt, ClipFrames);
	TestEqual(TEXT("winner"), C.GetResult(), 1);
	TestEqual(TEXT("win taunt"), C.TauntPlayed, WinTaunt);
	TestTrue(TEXT("0x2EF then the win taunt 0x214 in the same frame"), Plays.Num() == 3
		&& Plays[1] == FIntPoint(StopAt + ClipFrames, CycleAnim) && Plays[2] == FIntPoint(StopAt + ClipFrames, WinTaunt));
	TestEqual(TEXT("Static after phase 2"), Sim.State.Motion, ERiderMotion::Static);
	while (DoneAt < 0 && N++ < 800) Step();
	// 0x1065C8, 0x113820, 0x11AFA8
	AddInfo(FString::Printf(TEXT("crossing -> stop %d, 0x2EF + taunt %d, done %d frames"), StopAt, Phase3At, DoneAt));
	TestEqual(TEXT("done at counter 241"), DoneAt, 241);
	TestTrue(TEXT("the celebration starts before the race end"), Phase3At > 0 && Phase3At < DoneAt);
	const int32 TauntGameFrames = 2 * SSXAnim::OneShotFrames(SSXAnim::FAnimTable::Shared().Frames(WinTaunt, 0, 0));
	TestTrue(TEXT("the half-speed taunt outlasts the race end"), Phase3At + TauntGameFrames > DoneAt);
	TestTrue(TEXT("done (phase > 1, > 240 frames)"), C.IsDone());

	FControl Slow;
	Sim.State.Velocity = FVector(100.0, 0.0, 0.0);
	Slow.Enter(Sim, ERiderControl::Cruise);
	TestEqual(TEXT("slow: phase 1 at once"), Slow.GetPhase(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEndRaceNarrationTest, "SSX.Race.EndRace.Narration", EndTestFlags)
bool FSSXEndRaceNarrationTest::RunTest(const FString& Parameters)
{
	// 0x231548, 0x231800, 0x2315E0, 0x231C50
	FSSXSpeechSystem Speech;
	FSSXAnnouncer A;
	A.Reset(4, 0, 2);
	FSSXAnnouncer::FRaceEndInfo I;
	for (int32 R = 0; R < 6; ++R)
	{
		FSSXAnnouncer::FRaceEndInfo::FRacer X;
		X.Slot = R == 0 ? 0 : -1;
		X.Character = R;
		X.Frames = 12000.0;
		I.Racers.Add(X);
	}
	I.Order = { 3, 0, 1, 2, 4, 5 };
	I.HeatWord = 2;
	TestEqual(TEXT("performance: fast (<= 30000 frames at Garibaldi) and no knock-downs"), A.PerformanceMask(I, 0), 0x10Cu | 0x101u);
	A.ResultsOpen(Speech, I, true);
	const uint32 Narr8007 = SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8007), Narr8009 = SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8009);
	const uint32 Narr8015 = SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8015), Narr800C = SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x800C);
	TestEqual(TEXT("results open: race end, WC final medal, 0x8015, top-half heat line"), A.Posted,
		TArray<uint32>{ Narr8007, Narr8009, Narr8015, Narr800C });
	TestEqual(TEXT("flags 1 | 0x10"), A.EndFlags, 0x11u);
	A.Posted.Reset();
	A.ResultsOpen(Speech, I, true);
	A.HiScoreRow(Speech, I, 0);
	TestEqual(TEXT("once per race; the record row line after it"), A.Posted, TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x800B) });
	FSSXAnnouncer B;
	B.Reset(2, 0, 3);
	I.bReplayState = true;
	B.ResultsOpen(Speech, I, true);
	TestTrue(TEXT("replay state 1..9: no race-end line"), !B.Posted.Contains(Narr8007));
	return true;
}

#endif