// 0x17C6E8, 0x180D08, FUN_002116D8, 0x112D10, FUN_00175eb8
#include "Camera/RaceCamera.h"
#include "Misc/AutomationTest.h"
#include "Race/PreRaceIntro.h"
#include "Race/SSXRaceManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags FlowTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	// FUN_00175de0
	class FDirectorEnv final : public SSXIntro::IEnv
	{
	public:
		explicit FDirectorEnv(FSSXRaceCamera& InCam) : Cam(InCam) {}
		FSSXRaceCamera& Cam;
		int32 Frame = 0;
		TSet<int32> Skips;
		int32 NarrationStops = 0;
		struct FEvent { int32 Frame; FString Name; int32 Arg; FString Text; };
		TArray<FEvent> Events;
		virtual bool DirectorRunning() const override { return Cam.IsScriptRunning(); }
		virtual bool SkipPressed() override { return Skips.Remove(Frame) > 0; }
		virtual void Act(const FString& Name, int32 Arg, const FString& Text) override
		{
			if (Name == TEXT("director_reset"))
			{
				NarrationStops += Cam.ResetDirector() ? 1 : 0;
			}
			else if (Name == TEXT("director_play"))
			{
				Cam.PlayScript(Text);
			}
			Events.Add({ Frame, Name, Arg, Text });
		}
		int32 FrameOf(const FString& Name, const FString& Text = FString(), int32 Nth = 0) const
		{
			for (const FEvent& E : Events)
			{
				if (E.Name == Name && (Text.IsEmpty() || E.Text == Text) && Nth-- == 0) return E.Frame;
			}
			return -1;
		}
		TArray<FString> Plays() const
		{
			TArray<FString> P;
			for (const FEvent& E : Events) if (E.Name == TEXT("director_play")) P.Add(E.Text);
			return P;
		}
	};

	void AddGariShapedScripts(FSSXCameraDefSet& Defs)
	{
		Defs.AddJson(TEXT("{\"scripts\":{")
			TEXT("\"PreLoad\":[{\"opcode\":0,\"name\":\"T2\",\"t0\":0.0,\"t1\":0.0},{\"opcode\":1,\"name\":\"T2a\",\"t0\":0.0,\"t1\":10.0},")
			TEXT("{\"opcode\":7,\"t0\":10.0,\"t1\":10.0}],")
			TEXT("\"RaceLoaded\":[{\"opcode\":10,\"name\":\"Fly01\",\"c\":100.0}],")
			TEXT("\"Fly01\":[{\"opcode\":33,\"arg\":1},{\"opcode\":0,\"name\":\"L1b\"},{\"opcode\":1,\"name\":\"L1\",\"t1\":5.82},")
			TEXT("{\"opcode\":0,\"name\":\"S1\",\"t0\":23.28,\"t1\":23.28},{\"opcode\":1,\"name\":\"S1a\",\"t0\":23.28,\"t1\":29.1}],")
			TEXT("\"STG_COM_1\":[{\"opcode\":10,\"name\":\"STG_COM_1a\",\"c\":100.0}],")
			TEXT("\"STG_COM_1a\":[{\"opcode\":41},{\"opcode\":14,\"name\":\"S_COM1\"},{\"opcode\":0,\"name\":\"E1\"},")
			TEXT("{\"opcode\":1,\"name\":\"E1a\",\"a\":2,\"t1\":3.5},{\"opcode\":0,\"name\":\"C1\",\"t0\":3.5,\"t1\":3.5},")
			TEXT("{\"opcode\":1,\"name\":\"C1a\",\"t0\":3.5,\"t1\":7.0},{\"opcode\":15,\"t0\":7.0,\"t1\":7.0}],")
			TEXT("\"GAT_FR_3\":[{\"opcode\":14,\"name\":\"FREIND_1\"},{\"opcode\":0,\"name\":\"G1\",\"t1\":2.5},")
			TEXT("{\"opcode\":0,\"name\":\"G2\",\"t0\":2.5,\"t1\":6.0},{\"opcode\":15,\"t0\":6.0,\"t1\":6.0}]}}"));
	}

	struct FFlowRun
	{
		FSSXCameraDefSet Defs;
		FSSXRaceCamera Cam{ &Defs };
		FDirectorEnv Env{ Cam };
		SSXIntro::FHandler H;
		TArray<TPair<int32, int32>> Ops;
		int32 PhaseNext = -1;

		void Run(int32 MaxFrames, const TArray<int32>& SkipFrames)
		{
			AddGariShapedScripts(Defs);
			Cam.OnScriptOp = [this](int32 Op, int32) { Ops.Add({ Env.Frame, Op }); };
			for (int32 F : SkipFrames) Env.Skips.Add(F);
			SSXIntro::FSlot Slots[3];
			Slots[0].Type = 0;
			Slots[1].Type = 9;
			Slots[1].Lines = { { 0, 120, 0, 1025 }, { 1, 121, 0, 1026 } };
			Slots[2].Type = 20;
			Slots[2].Lines = { { 0, 246, 0, 1030 } };
			const FSSXCameraRiderInput R;
			H.Enter(Env, 2, 0, Slots, SSXIntro::EEntry::Normal);
			Cam.Step(R);
			for (int32 F = 1; F <= MaxFrames && !H.IsDone(); ++F)
			{
				Env.Frame = F;
				if (H.GetStep() == 0x11 && F % 30 == 0) Env.Skips.Add(F);
				H.Update(Env);
				Cam.Step(R);
				if (H.IsDone() && PhaseNext < 0) PhaseNext = F;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceLoaderGateTest, "SSX.Race.LoaderGate", FlowTestFlags)
bool FSSXRaceLoaderGateTest::RunTest(const FString&)
{
	using SSXRaceGate::EFrame;
	bool bReady = false, bStarted = false;
	int32 Starts = 0, StartFrame = -1, RunWhileLoader = 0, FirstRun = -1;
	for (int32 F = 0; F < 400; ++F)
	{
		const bool bLoader = F < 300;
		const EFrame G = SSXRaceGate::Next(bReady, bLoader, bStarted);
		if (G == EFrame::Setup && F >= 40) bReady = true;
		if (G == EFrame::Start) { ++Starts; StartFrame = F; bStarted = true; }
		if ((G == EFrame::Start || G == EFrame::Run) && bLoader) ++RunWhileLoader;
		if ((G == EFrame::Start || G == EFrame::Run) && FirstRun < 0) FirstRun = F;
	}
	TestEqual(TEXT("no race frame (audio, PreRace, director, sim) while the loader is up"), RunWhileLoader, 0);
	TestEqual(TEXT("cGame::vfunc_05 runs once"), Starts, 1);
	TestEqual(TEXT("... on the first frame after the loader was deleted"), StartFrame, 300);
	TestEqual(TEXT("the first race update is that frame"), FirstRun, 300);
	TestTrue(TEXT("set-up (race state, racers) behind the loader"), SSXRaceGate::Next(true, true, false) == EFrame::Hold);
	TestTrue(TEXT("no loader (direct map load): start at once"), SSXRaceGate::Next(true, false, false) == EFrame::Start);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRacePreRaceFlowTest, "SSX.Race.PreRaceFlow", FlowTestFlags)
bool FSSXRacePreRaceFlowTest::RunTest(const FString&)
{
	{
		FFlowRun Run;
		Run.Run(6000, {});
		const FDirectorEnv& E = Run.Env;
		TestEqual(TEXT("scripts: PreLoad (enter), RaceLoaded, STG_COM_1, GAT_FR_3"), E.Plays(),
			TArray<FString>{ TEXT("PreLoad"), TEXT("RaceLoaded"), TEXT("STG_COM_1"), TEXT("GAT_FR_3") });
		const int32 F0 = E.FrameOf(TEXT("director_play"), TEXT("RaceLoaded"));
		const int32 F1 = E.FrameOf(TEXT("director_play"), TEXT("STG_COM_1"));
		const int32 F2 = E.FrameOf(TEXT("director_play"), TEXT("GAT_FR_3"));
		TestEqual(TEXT("slot 0 (venue flyover) on the first update (steps 2 -> 4 -> 5 in one frame)"), F0, 1);
		AddInfo(FString::Printf(TEXT("RaceLoaded @%d, STG_COM_1 @%d (+%d), GAT_FR_3 @%d (+%d), phase next @%d"), F0, F1, F1 - F0, F2, F2 - F1, Run.PhaseNext));
		TestTrue(TEXT("banter (slot 1) waits for the flyover's end: +1746..1750 frames"), F1 - F0 >= 1746 && F1 - F0 <= 1750);
		TestTrue(TEXT("slot 2 after slot 1's 7.0 s: +420..424"), F2 - F1 >= 420 && F2 - F1 <= 424);
		const int32 Speech1 = E.FrameOf(TEXT("intro_speech_begin"), FString(), 0);
		const int32 Anim1 = E.FrameOf(TEXT("anim_speech"));
		TestEqual(TEXT("slot 1's intro speech starts with its own script (step 6 -> 8), not during the flyover"), Speech1, F1);
		TestEqual(TEXT("first ANIM line posted on that frame"), Anim1, F1);
		TestEqual(TEXT("slot 2's speech with GAT_FR_3"), E.FrameOf(TEXT("intro_speech_begin"), FString(), 1), F2);
		int32 Narr = -1, Staging = -1;
		for (const TPair<int32, int32>& O : Run.Ops)
		{
			if (O.Value == 0x21 && Narr < 0) Narr = O.Key;
			if (O.Value == 0x29 && Staging < 0) Staging = O.Key;
		}
		TestEqual(TEXT("venue narration (op 0x21) at the flyover start"), Narr, F0);
		TestEqual(TEXT("staging narration (op 0x29) at the banter start"), Staging, F1);
		TestEqual(TEXT("director resets that stop the narration: the slot 0 play over PreLoad only"), E.NarrationStops, 1);
		TestEqual(TEXT("fade at step 0xE right after slot 2"), E.FrameOf(TEXT("fade")), E.FrameOf(TEXT("place_racers")));
		TestTrue(TEXT("NextRace card, then phase next (StartRace)"), E.FrameOf(TEXT("overlay_open")) >= F2 + 360 && Run.PhaseNext > E.FrameOf(TEXT("overlay_open")));
	}
	{
		FFlowRun Run;
		Run.Run(6000, { 166 });
		const FDirectorEnv& E = Run.Env;
		TestEqual(TEXT("skip: STG_COM_1 on the skip frame"), E.FrameOf(TEXT("director_play"), TEXT("STG_COM_1")), 166);
		TestEqual(TEXT("skip: intro speech end (0x110980) on the skip frame"), E.FrameOf(TEXT("intro_speech_end")), 166);
		TestEqual(TEXT("skip mid-flyover stops the venue narration (FUN_00175eb8(cam, 1) -> FUN_00231318)"), E.NarrationStops, 2);
		TestEqual(TEXT("skip: slot 2 after the full banter"), E.FrameOf(TEXT("director_play"), TEXT("GAT_FR_3")) - 166 >= 420, true);
	}
	{
		FSSXCameraDefSet Defs;
		AddGariShapedScripts(Defs);
		Defs.AddJson(TEXT("{\"scripts\":{\"T_End\":[{\"opcode\":0,\"name\":\"X\",\"t1\":0.5},{\"opcode\":32,\"t0\":0.5,\"t1\":0.5}]}}"));
		const FSSXCameraRiderInput R;
		FSSXRaceCamera Cam(&Defs);
		Cam.PlayScript(TEXT("PreLoad"));
		for (int32 I = 0; I < 700; ++I) Cam.Step(R);
		TestFalse(TEXT("PreLoad after RETURN: not running (0x175E50: +0x3C and +0x40)"), Cam.IsScriptRunning());
		TestTrue(TEXT("... but +0x34 on: the reset stops the narration"), Cam.ResetDirector());
		Cam.PlayScript(TEXT("GAT_FR_3"));
		for (int32 I = 0; I < 400; ++I) Cam.Step(R);
		TestFalse(TEXT("natural end: FUN_00175eb8(cam, 0), +0x34 off"), Cam.ResetDirector());
		Cam.PlayScript(TEXT("T_End"));
		for (int32 I = 0; I < 60; ++I) Cam.Step(R);
		TestFalse(TEXT("END (0x20): +0x34 cleared before its reset"), Cam.IsDirectorOn());
	}
	return true;
}

#endif
