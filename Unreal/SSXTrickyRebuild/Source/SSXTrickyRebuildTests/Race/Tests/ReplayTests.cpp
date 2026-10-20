// 0x191298, 0x190878, 0x1927D8, 0x191830, 0x1928F0, 0x192880, 0x192998, 0x192758, 0x1950B8, 0x1953C8, 0x193370, 0x1935D8, 0x1EC548, 0x1EB6D0
#include "Misc/AutomationTest.h"
#include "Race/SSXReplay.h"
#include "Race/SSXRaceManager.h"
#include "Race/SSXReplayFile.h"
#include "UI/SSXReplayControl.h"
#include "UI/SSXFeTypes.h"
#include "Harness/RiderSetup.h"
#include "Input/RiderInputDriver.h"
#include "Input/SSXPadVibration.h"
#include "Sim/PatchWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ReplayTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	using namespace SSXReplay;

	FPadActions HumanInput(int32 F)
	{
		FPadActions A;
		A.Axes[FPadMapper::ATurn] = FMath::Sin(F * 0.05) * 0.8;
		const int32 Ph = F % 90;
		if (Ph >= 40 && Ph < 60) A.Held |= 1ull << FPadMapper::ActJump;
		if (Ph == 40) A.Pressed |= 1ull << FPadMapper::ActJump;
		if ((F / 120) % 2 == 1) A.Held |= 1ull << FPadMapper::ActBoost;
		return A;
	}

	FPadActions AiInput(const FRiderSim& S)
	{
		FPadActions A;
		A.Axes[FPadMapper::ATurn] = FMath::Clamp(-S.State.Position.X / 800.0, -1.0, 1.0);
		return A;
	}

	struct FTwoRiders : public IReplayWorld
	{
		FPatchWorld Slope;
		TUniquePtr<FRiderSim> Sim[2];
		TUniquePtr<FRiderInputDriver> Drv[2];
		FSSXReplay* Replay = nullptr;
		int32 Restores = 0;
		TArray<uint32> Captured;

		FTwoRiders() { SSXRiderSetup::BuildSlopeWorld(Slope, 15.0, 1); Build(); }
		void Build()
		{
			for (int32 I = 0; I < 2; ++I)
			{
				Sim[I] = MakeUnique<FRiderSim>();
				Drv[I] = MakeUnique<FRiderInputDriver>();
				Sim[I]->SetWorld(&Slope);
				SSXRiderSetup::InstallStates(*Sim[I], *Drv[I]);
				Sim[I]->Reset();
				Sim[I]->Spawn(FVector(I * 300.0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
			}
		}
		void StepFrame(const FPadActions& Human)
		{
			Drv[0]->Step(*Sim[0], Human);
			Drv[1]->Step(*Sim[1], AiInput(*Sim[1]));
		}
		virtual void CaptureKeyframe(int32 Key, uint32 Frame, bool) override { Captured.Add(Frame); }
		virtual void RestoreKeyframe(int32 Key, uint32 Frame) override
		{
			++Restores;
			Build();
			FInputRecorder Copy = Replay->GetInput(0);
			Copy.Seek(0);
			for (uint32 F = 0; F < Frame; ++F)
			{
				uint32 Wd = 0;
				Copy.Play(Wd);
				Drv[0]->StepWord(*Sim[0], Wd);
				Drv[1]->Step(*Sim[1], AiInput(*Sim[1]));
			}
		}
	};

	// 0x181638, 0x17F078, 0x1814C0
	bool GameFrame(FSSXReplay& R, FTwoRiders& W, int32 RecIndex)
	{
		R.Update();
		const bool bRecording = R.GetState() == Record;
		if (!bRecording && R.IsWorldFrozen()) return false;
		if (!bRecording && !R.IsPlaying()) return false;
		if (bRecording)
		{
			W.StepFrame(HumanInput(RecIndex));
			uint32 Wd = W.Drv[0]->GetLastWord();
			R.RecordOrPlayInput(0, Wd);
		}
		else
		{
			uint32 Wd = 0;
			R.RecordOrPlayInput(0, Wd);
			W.Drv[0]->StepWord(*W.Sim[0], Wd);
			W.Drv[1]->Step(*W.Sim[1], AiInput(*W.Sim[1]));
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayRecordPlaybackTest, "SSX.Replay.RecordPlayback", ReplayTestFlags)
bool FSSXReplayRecordPlaybackTest::RunTest(const FString& Parameters)
{
	FTwoRiders W;
	FSSXReplay R;
	W.Replay = &R;
	R.Init(2, 1, false, &W);
	R.AttachHuman(0);
	TestEqual(TEXT("idle after init (0x18EBD0)"), R.GetState(), int32(Idle));
	R.OnStartRace();
	TestEqual(TEXT("StartRace enter: idle -> 0xB"), R.GetState(), int32(StageReset));
	R.Update();
	TestEqual(TEXT("stage reset -> 0xC"), R.GetState(), int32(RecordStart));

	constexpr int32 N = 700;
	TArray<FVector> Pos[2];
	for (int32 F = 0; F < N; ++F)
	{
		GameFrame(R, W, F);
		Pos[0].Add(W.Sim[0]->State.Position);
		Pos[1].Add(W.Sim[1]->State.Position);
	}
	TestEqual(TEXT("recorded length = frames"), int32(R.GetRecordedLength()), N);
	TestTrue(TEXT("runs coded (fewer runs than frames)"), R.GetInput(0).NumRuns() < N && R.GetInput(0).NumRuns() > 10);
	R.EndRecording();
	R.Update();
	TestEqual(TEXT("ended recording -> 0xD"), R.GetState(), int32(RecordEnded));
	TestTrue(TEXT("keyframes: start + end"), R.KeyframeFrames() == TArray<uint32>({ 0u, uint32(N) }));

	R.StartFullReplay();
	TestEqual(TEXT("0x191B60: state 7"), R.GetState(), int32(Started));
	TestTrue(TEXT("playback gate"), R.IsPlaying());
	R.Update();
	TestEqual(TEXT("state 7 -> paused"), R.GetState(), int32(Paused));
	R.SetCommand(CmdPlayToggle);
	R.Update();
	TestEqual(TEXT("X: playing"), R.GetState(), int32(Play));
	int32 Mismatch = 0, Played = 0;
	for (int32 Guard = 0; Guard < N + 10 && R.GetState() == Play; ++Guard)
	{
		if (GameFrame(R, W, 0))
		{
			const int32 F = int32(R.GetPlayFrame()) - 1;
			++Played;
			if (W.Sim[0]->State.Position != Pos[0][F] || W.Sim[1]->State.Position != Pos[1][F]) ++Mismatch;
		}
	}
	TestEqual(TEXT("world frames played: N - 1 (0x18F020 freezes the frame that reaches the length)"), Played, N - 1);
	TestEqual(TEXT("bit-exact positions per frame (human + re-run AI)"), Mismatch, 0);
	TestEqual(TEXT("end of the replay: paused"), R.GetState(), int32(Paused));
	TestEqual(TEXT("frame clamped to the length"), int32(R.GetPlayFrame()), N);

	R.SetCommand(CmdToStart);
	R.Update();
	TestEqual(TEXT("to start: state 6"), R.GetState(), int32(SeekedStart));
	R.Update();
	R.SetCommand(CmdPlayToggle);
	R.Update();
	Mismatch = 0;
	for (int32 I = 0; I < 200; ++I)
	{
		if (GameFrame(R, W, 0))
		{
			const int32 F = int32(R.GetPlayFrame()) - 1;
			if (W.Sim[0]->State.Position != Pos[0][F] || W.Sim[1]->State.Position != Pos[1][F]) ++Mismatch;
		}
	}
	TestEqual(TEXT("exact after a restore"), Mismatch, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayKeyframeTest, "SSX.Replay.KeyframeSeek", ReplayTestFlags)
bool FSSXReplayKeyframeTest::RunTest(const FString& Parameters)
{
	FSSXReplay R;
	R.Init(2, 1, false, nullptr);
	TestEqual(TEXT("pool min(16, 0xC9800 / (2 * 0xC00 + 0x8000))"), R.GetKeyframePool(), 16);
	TestEqual(TEXT("lock interval ((4 * 3600) / 6) / 10 * 10"), int32(R.GetLockInterval()), 2400);
	R.OnStartRace();
	R.Update();
	for (int32 F = 0; F < 2941; ++F) R.Update();
	TestTrue(TEXT("pruned while recording: start, locked 2400, the last two"), R.KeyframeFrames() == TArray<uint32>({ 0u, 2400u, 2880u, 2940u }));
	for (int32 F = 2941; F < 3000; ++F) R.Update();
	R.EndRecording();
	R.Update();
	TestTrue(TEXT("end: unlocked dropped, locked end keyframe"), R.KeyframeFrames() == TArray<uint32>({ 0u, 2400u, 3000u }));

	R.StartFullReplay();
	R.Update();
	TestEqual(TEXT("paused at 0"), int32(R.GetPlayFrame()), 0);
	R.SetCommand(CmdToEnd); R.Update();
	TestTrue(TEXT("command 2: last keyframe, state 5"), R.GetPlayFrame() == 3000 && R.GetState() == Seeked);
	R.Update();
	TestEqual(TEXT("state 5 -> paused"), R.GetState(), int32(Paused));
	R.SetCommand(CmdPrevKey); R.Update(); R.Update();
	TestEqual(TEXT("command 3 on a keyframe: the previous one"), int32(R.GetPlayFrame()), 2400);
	R.SetCommand(CmdPlayToggle); R.Update();
	for (int32 I = 0; I < 100; ++I) R.Update();
	TestEqual(TEXT("played 100 frames"), int32(R.GetPlayFrame()), 2500);
	R.SetCommand(CmdPause); R.Update();
	R.SetCommand(CmdPrevKey); R.Update(); R.Update();
	TestEqual(TEXT("command 3 between keyframes: the one at or before"), int32(R.GetPlayFrame()), 2400);
	R.SetCommand(CmdNextKey); R.Update(); R.Update();
	TestEqual(TEXT("command 4: the next keyframe"), int32(R.GetPlayFrame()), 3000);
	R.SetCommand(CmdStep); R.Update();
	TestTrue(TEXT("command 5 at the end: no step past the length, state 4"), R.GetPlayFrame() == 3000 && R.GetState() == StepFrame);
	R.SetCommand(CmdToStart); R.Update();
	TestEqual(TEXT("state 4 -> 3 ignores the command that frame (0x191298 case 4)"), R.GetState(), int32(Paused));
	R.Update();
	R.Update();
	R.SetCommand(CmdStep); R.Update();
	TestTrue(TEXT("command 5: one frame, world runs once"), R.GetPlayFrame() == 1 && R.GetState() == StepFrame && !R.IsWorldFrozen());
	R.Update();
	TestTrue(TEXT("then paused and frozen"), R.GetState() == Paused && R.IsWorldFrozen());

	FCameraTrack T;
	FCamera C0; C0.A = 1.f; FCamera C1; C1.A = 2.f; FCamera C2; C2.A = 3.f;
	T.Insert(0, C0, true);
	T.Insert(300, C1, true);
	T.Insert(600, C2, true);
	T.Seek(0, true);
	TestTrue(TEXT("Advanced: 3 cuts, seek 0 -> cut 0"), T.Count == 3 && T.Current(true) == C0);
	TestTrue(TEXT("advance at 299: none"), !T.Advance(299, true));
	TestTrue(TEXT("advance at 300: cut 1"), T.Advance(300, true) && T.Current(true) == C1);
	T.Seek(650, true);
	TestTrue(TEXT("seek 650 -> cut 2"), T.Current(true) == C2);
	TestTrue(TEXT("Normal: one camera, never advances"), T.Current(false) == C0 && !T.Advance(700, false));
	TestTrue(TEXT("delete at/after 301 (from index 1)"), T.Delete(301, true) && T.Count == 2);
	FCamera C3; C3.A = 9.f;
	T.Set(400, C3, true);
	TestTrue(TEXT("Set overwrites the cut at or before"), T.Cuts[1].Cam == C3);
	T.Set(400, C2, false);
	TestTrue(TEXT("Normal Set: the single camera"), T.Single == C2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayInputTest, "SSX.Replay.InputRecorder", ReplayTestFlags)
bool FSSXReplayInputTest::RunTest(const FString& Parameters)
{
	FInputRecorder Rec;
	const uint32 A = 0, B = 1;
	for (int32 I = 0; I < 300; ++I) Rec.Record(A);
	Rec.Record(B);
	TestTrue(TEXT("runs cap at 255"), Rec.NumRuns() == 3 && Rec.Runs[0] == 255 && Rec.Runs[1] == 45 && Rec.Runs[2] == 1);
	TestTrue(TEXT("seek 299"), Rec.Seek(299));
	uint32 Out = 7;
	TestTrue(TEXT("frame 299 = A"), Rec.Play(Out) && Out == A);
	TestTrue(TEXT("frame 300 = B"), Rec.Play(Out) && Out == B);
	TestTrue(TEXT("past the end"), !Rec.Play(Out));
	TestFalse(TEXT("seek past the end"), Rec.Seek(301));

	FSSXReplay R;
	R.Init(2, 1, false, nullptr);
	R.AttachHuman(0);
	R.OnStartRace();
	R.Update();
	int32 F = 0;
	while (!R.IsFull() && F < 7000)
	{
		R.Update();
		uint32 Wd = uint32(F & 1);
		R.RecordOrPlayInput(0, Wd);
		++F;
	}
	TestEqual(TEXT("full on the run after 0x1999"), F, MaxRuns + 1);
	TestEqual(TEXT("replay full text 180 frames"), R.GetFullMessage(), FullMessageFrames);
	const uint32 Len = R.GetRecordedLength();
	const int32 Keys = R.KeyframeFrames().Num();
	for (int32 I = 0; I < 120; ++I) R.Update();
	TestTrue(TEXT("length and keyframes frozen while full"), R.GetRecordedLength() == Len && R.KeyframeFrames().Num() == Keys);
	TestEqual(TEXT("message counts down"), R.GetFullMessage(), FullMessageFrames - 120);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayInputDecodeTest, "SSX.Replay.InputDecode", ReplayTestFlags)
bool FSSXReplayInputDecodeTest::RunTest(const FString& Parameters)
{
	// 0x192A40, 0x192880, 0x192998
	FInputRecorder Rec;
	Rec.Words = { 0x00000000u, 0x00000034u, 0x00000021u, 0x0000001Fu, 0x0000F806u, 0x0001120Bu, 0x0000001Au };
	Rec.Runs = { 42, 1, 255, 1, 3, 47, 2 };
	TArray<uint32> Expect;
	for (int32 K = 0; K < Rec.Words.Num(); ++K) { for (int32 N = 0; N < FMath::Max(1, int32(Rec.Runs[K])); ++N) Expect.Add(Rec.Words[K]); }
	TArray<uint32> Got;
	uint32 W = 0;
	while (Rec.Play(W)) { Got.Add(W); }
	TestEqual(TEXT("0x192998 plays each word for its run byte"), Got, Expect);
	W = 0x1234u;
	TestTrue(TEXT("past the last run: false, word cleared (0x2CC808)"), !Rec.Play(W) && W == 0u);
	// 0x192758
	for (const uint32 F : { 0u, 41u, 42u, 43u, 297u, 298u, 300u, 349u, 350u, 351u })
	{
		const bool bSeek = Rec.Seek(F);
		const bool bIn = int32(F) < Expect.Num();
		uint32 Wd = 0;
		TestTrue(FString::Printf(TEXT("seek %u then play = frame %u's word"), F, F), bSeek == bIn && (!bIn || (Rec.Play(Wd) && Wd == Expect[F])));
	}

	// 0x151250
	using C = ERiderControl;
	const FRiderInputWord Gate = FPadMapper::DecodeWord(C::GateAnticipate, 0x21u);
	TestTrue(TEXT("gate rock 0x21 = -31/31"), FMath::IsNearlyEqual(Gate.GateRock, -1.0));
	TestTrue(TEXT("gate rock 0x34 = -12/31"), FMath::IsNearlyEqual(FPadMapper::DecodeWord(C::GateAnticipate, 0x34u).GateRock, -12.0 / 31.0));
	const FRiderInputWord G = FPadMapper::DecodeWord(C::Cruise, 0x0000F806u | (47u << 5) | (2u << 17) | (3u << 23) | (1u << 25));
	TestTrue(TEXT("ground: bit 0 reset, 1 jump pressed (0x1797A8), 2 held, 3/4 boost, turn << 5, crouch << 11, brake << 17, shoves << 23 / 25"),
		!G.bResetRequest && G.bJumpPressed && G.bJumpHeld && !G.bBoostHeld && FMath::IsNearlyEqual(G.Crouch, 1.0)
		&& FMath::IsNearlyEqual(G.Turn, -17.0 / 31.0) && FMath::IsNearlyEqual(G.Brake, 2.0 / 31.0) && G.ShoveSide == -1 && G.ShoveFwd == 1);
	const FRiderInputWord P = FPadMapper::DecodeWord(C::Prewind, 0x3E8u);
	TestTrue(TEXT("prewind 0x3E8: spin << 5 = +1, boost (bit 3), jump released"), FMath::IsNearlyEqual(P.PrewindSpin, 1.0) && P.bBoostHeld && !P.bJumpHeld);
	const FRiderInputWord S = FPadMapper::DecodeWord(C::Spin, 0x0001120Bu);
	TestTrue(TEXT("spin 0x1120B: trick byte 11, tweak (bit 9), spin +1 (<< 12), late spin +1 (<< 16)"),
		S.TrickId == 11 && S.bTrickPressed && S.bTweak && S.Spin == 1 && S.LateSpin == 1 && S.Flip == 0);
	TestTrue(TEXT("spin 0x1A: no trick"), !FPadMapper::DecodeWord(C::Spin, 0x1Au).bTrickPressed);
	const FRiderInputWord N = FPadMapper::DecodeWord(C::NaturalAir, 1u | (31u << 2) | (33u << 8));
	TestTrue(TEXT("natural air: bit 0 trick, 1 reset, crouch << 2, turn << 8"), N.bTrickPressed && !N.bResetRequest && FMath::IsNearlyEqual(N.Crouch, 1.0)
		&& FMath::IsNearlyEqual(N.Turn, -1.0));
	FPadActions A;
	A.Axes[FPadMapper::AGateRock] = -0.5;
	TestEqual(TEXT("encode -0.5 -> -15 & 0x3F"), FPadMapper::EncodeWord(C::GateAnticipate, A), 0x31u);
	A.Axes[FPadMapper::AGateRock] = 0.999;
	TestEqual(TEXT("encode 0.999 -> 30"), FPadMapper::EncodeWord(C::GateAnticipate, A), 30u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayHighlightTest, "SSX.Replay.Highlights", ReplayTestFlags)
bool FSSXReplayHighlightTest::RunTest(const FString& Parameters)
{
	FSSXReplay R;
	R.Init(2, 1, false, nullptr);
	R.AttachHuman(0);
	TestEqual(TEXT("one human: 4 slots"), R.GetHighlightSlots(), 4);
	R.OnStartRace();
	R.Update();
	auto Run = [&](int32 Frames) { for (int32 I = 0; I < Frames; ++I) R.Update(); };
	Run(200);
	auto Trick = [&](float V) { R.HighlightStart(0, 1); Run(30); R.HighlightValue(0, V); R.HighlightCommit(0); Run(100); };
	Trick(5.f);
	const FHighlights& H = R.GetHighlights(0);
	TestTrue(TEXT("slot 0 = the trick"), H.Slots[0] >= 0 && R.GetClip(H.Slots[0]).Type == 1);
	const FClip& C = R.GetClip(H.Slots[0]);
	TestTrue(TEXT("clip from the keyframe before the last, length = frame - key + 60"), R.GetKey(C.Key).Frame == 120 && C.Length == int32(230 - 120) + 60);
	R.HighlightStart(0, 1); Run(10); R.HighlightCommit(0);
	TestTrue(TEXT("a trick without value is dropped"), H.Slots[1] < 0 && H.Pending < 0);
	Trick(3.f); Trick(4.f); Trick(6.f);
	TestTrue(TEXT("4 slots filled"), H.Slots[1] >= 0 && H.Slots[2] >= 0 && H.Slots[3] >= 0);
	Trick(1.f);
	bool bHasOne = false;
	for (const int32 S : H.Slots) bHasOne |= R.GetClip(S).Value == 1.f;
	TestFalse(TEXT("full and quota reached: a weaker trick does not replace"), bHasOne);
	Trick(10.f);
	bool bHasTen = false, bHasThree = false;
	for (const int32 S : H.Slots) { bHasTen |= R.GetClip(S).Value == 10.f; bHasThree |= R.GetClip(S).Value == 3.f; }
	TestTrue(TEXT("a better trick replaces the lowest"), bHasTen && !bHasThree);

	TestTrue(TEXT("0x193C68 sets state 9"), R.OnEndRaceAttract());
	R.Update();
	TestTrue(TEXT("state 9 -> playing the playlist, looping"), R.GetState() == Play && R.IsLooping() && R.GetPlaylist().Num() == 4);
	TestTrue(TEXT("the first clip's keyframe restored"), R.GetPlayFrame() == R.GetKey(R.GetClip(R.GetPlaylist()[0]).Key).Frame);
	TestFalse(TEXT("0x193C68 again: looping, no change"), R.OnEndRaceAttract());
	TestTrue(TEXT("gate during the attract replay"), R.IsPlaying());
	R.ResetIdle();
	TestFalse(TEXT("restart: idle, no gate"), R.IsPlaying());
	return true;
}

namespace
{
	// 0x1785C0, 0x190878
	struct FGridWorld : public IReplayWorld
	{
		int32 Human = 0;
		int32 Racers = 6;
		FCamera Cam;
		int32 Racer(int32 Player) const { return RacerOfPlayer([this](int32 I) { return I == Human; }, Racers, Player); }
		virtual FCamera GetCamera() const override { return Cam; }
		virtual void ApplyCamera(const FCamera& C, bool) override { Cam = C; }
		virtual void SetCameraRider(bool bReplay, int32 Player) override { Cam.Target = bReplay ? Racer(Player) : 0; }
		virtual void SetCameraTarget(int32 Player) override { Cam.Target = Racer(Player); }
		virtual int32 FocusPlayer() const override { return FSSXRaceReplayWorld(nullptr).FocusPlayer(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayHumanSlotTest, "SSX.Replay.HumanRacerSlot", ReplayTestFlags)
bool FSSXReplayHumanSlotTest::RunTest(const FString& Parameters)
{
	for (int32 H = 0; H < 6; ++H)
	{
		const FString Tag = FString::Printf(TEXT("human in slot %d: "), H);
		auto IsHuman = [H](int32 I) { return I == H; };
		const int32 P = PlayerOfRacer(IsHuman, 6, H);
		TestEqual(Tag + TEXT("player number 0"), P, 0);
		TestEqual(Tag + TEXT("an AI slot has none"), PlayerOfRacer(IsHuman, 6, (H + 1) % 6), -1);
		TestTrue(Tag + TEXT("player 0 -> its slot; past the humans -> player 0 (0x1785C0)"), RacerOfPlayer(IsHuman, 6, 0) == H && RacerOfPlayer(IsHuman, 6, 3) == H);

		FGridWorld W;
		W.Human = H;
		FSSXReplay R;
		R.Init(6, 1, true, &W);
		R.AttachHuman(P);
		TestTrue(Tag + TEXT("recorders under the player number"), R.GetHighlights(0).bActive);
		R.OnStartRace();
		uint32 Wd = 1;
		auto Run = [&](int32 Frames)
		{
			for (int32 I = 0; I < Frames; ++I)
			{
				R.Update();
				Wd = (Wd + (I % 7 == 0 ? 1u : 0u)) & 0xFFu;
				uint32 X = Wd;
				R.RecordOrPlayInput(P, X);
			}
		};
		Run(200);
		R.HighlightCall(P, 15, 1.f); Run(30); R.HighlightCall(P, 16, 4.f); R.HighlightCall(P, 19, 0.f); Run(100);
		R.HighlightCall(P, 15, 2.f); R.HighlightCall(P, 17, 3.f); Run(20); R.HighlightCall(P, 19, 0.f); Run(100);
		R.EndRecording();
		Run(2);
		TestEqual(Tag + TEXT("recording ended"), R.GetState(), int32(RecordEnded));

		// 0x193C68
		TestTrue(Tag + TEXT("attract"), R.OnEndRaceAttract());
		R.Update();
		TestTrue(Tag + TEXT("attract plays the player's highlights, looping"), R.GetState() == Play && R.IsLooping() && R.GetPlaylist().Num() == 2);
		TestEqual(Tag + TEXT("focus = player 0"), R.GetFocus(), 0);
		TestEqual(Tag + TEXT("the clip camera follows the human's racer"), W.Cam.Target, H);
		for (int32 I = 0; I < 900; ++I) R.Update();
		TestEqual(Tag + TEXT("looping: still the human's racer"), W.Cam.Target, H);
		R.StopPlayback();

		// 0x1EF990, 0x1785C0, 0x191B60
		W.SetCameraRider(true, FMath::Max(0, W.FocusPlayer()));
		R.StartFullReplay();
		R.SetCommand(CmdToStart);
		for (int32 I = 0; I < 3; ++I) R.Update();
		R.SetCommand(CmdPlayToggle);
		for (int32 I = 0; I < 120; ++I) R.Update();
		TestTrue(Tag + TEXT("results replay plays on the focus track"), R.GetPlayFrame() > 100u && W.Cam.Target == H);
		R.StopPlayback();

		SSXReplayFile::FSetup Setup;
		Setup.NumRacers = 6;
		TArray<uint8> Bytes;
		R.StartFullReplay();
		TestTrue(Tag + TEXT("file written"), SSXReplayFile::Write(R, Setup, Bytes));
		FGridWorld LW;
		LW.Human = H;
		FSSXReplay L;
		L.Init(6, 1, true, &LW);
		L.AttachHuman(P);
		if (!TestTrue(Tag + TEXT("file read"), SSXReplayFile::ReadReplay(Bytes, L))) continue;
		TestEqual(Tag + TEXT("loaded focus = player 0"), L.GetFocus(), 0);
		L.BeginRebuild();
		L.OnStartRace();
		for (int32 F = 0; L.IsRebuilding() && F < 2000; ++F)
		{
			L.Update();
			uint32 X = 0;
			if (L.IsPlayingInput()) L.RecordOrPlayInput(P, X);
		}
		TestFalse(Tag + TEXT("rebuilt"), L.IsRebuilding());
		L.Update();
		L.SetCommand(CmdPlayToggle);
		for (int32 I = 0; I < 200; ++I) L.Update();
		TestTrue(Tag + TEXT("loaded replay plays, camera on the human's racer"), L.GetPlayFrame() > 0u && LW.Cam.Target == H);
		L.StopPlayback();
		TestTrue(Tag + TEXT("loaded attract"), L.OnEndRaceAttract());
		L.Update();
		for (int32 I = 0; I < 300; ++I) L.Update();
		TestTrue(Tag + TEXT("loaded attract loops on player 0"), L.IsLooping() && L.GetFocus() == 0 && LW.Cam.Target == H);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayControlTest, "SSX.Replay.Control", ReplayTestFlags)
bool FSSXReplayControlTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	using EE = SSXReplayCtl::EEvent;
	for (const bool bAdv : { false, true })
	{
		FSSXReplay R;
		R.Init(2, 1, bAdv, nullptr);
		R.AttachHuman(0);
		R.OnStartRace();
		for (int32 I = 0; I < 1000; ++I) R.Update();
		R.StartFullReplay();
		FSSXReplayControl Ctl;
		Ctl.Activate(R, 0, bAdv);
		TestEqual(TEXT("activate: command 1"), R.GetCommand(), int32(CmdToStart));
		TestTrue(TEXT("help level / max per setting"), bAdv ? (Ctl.HelpLevel == 1 && Ctl.HelpMax == 2 && Ctl.PanelY == 360.f) : (Ctl.HelpLevel == 0 && Ctl.HelpMax == 1 && Ctl.PanelY == 480.f));
		int32 NextCams = 0;
		SSXReplayCtl::FEnv Env;
		Env.CameraMode = []() { return 8; };
		Env.NextCamera = [&]() { ++NextCams; };
		Env.Racers = 2;
		uint32 Prev = 0;
		auto Frame = [&](uint32 Held) { Ctl.Events.Reset(); R.Update(); Ctl.Update(R, Held, Prev, Env); Prev = Held; };
		Frame(0);
		TestTrue(TEXT("state 7 -> 3 first, the control returns in state 7"), R.GetState() == Paused && R.GetCommand() == CmdToStart);
		Frame(0);
		TestEqual(TEXT("command 1 -> state 6"), R.GetState(), int32(SeekedStart));
		Frame(0);
		TestEqual(TEXT("paused at the start"), R.GetState(), int32(Paused));
		TestTrue(TEXT("paused: audio pause every frame"), Ctl.Events == TArray<EE>({ EE::AudioPause }));
		Frame(PadCross);
		TestTrue(TEXT("X paused: play (6), navigate + resume"), Ctl.LastCommand == CmdPlayToggle && Ctl.Events == TArray<EE>({ EE::Navigate, EE::AudioResume }));
		Ctl.Events.Reset();
		Ctl.Update(R, 0, Prev, Env);
		Prev = 0;
		TestTrue(TEXT("a second control step before the replay steps: the play command pending, no audio pause"), Ctl.Events.Num() == 0);
		Frame(0); Frame(0);
		TestEqual(TEXT("playing"), R.GetState(), int32(Play));
		Frame(PadCross);
		TestTrue(TEXT("X playing: pause (7)"), Ctl.LastCommand == CmdPause && Ctl.Events == TArray<EE>({ EE::Navigate, EE::AudioPause }));
		Frame(0);
		Frame(PadL2);
		TestTrue(TEXT("L2: previous keyframe (3), speech stop + navigate"), Ctl.LastCommand == CmdPrevKey && Ctl.Events == TArray<EE>({ EE::SpeechStop, EE::Navigate, EE::AudioPause }));
		int32 Repeats = 0;
		for (int32 I = 0; I < 20; ++I) { Frame(PadL2); Repeats += Ctl.LastCommand == CmdPrevKey ? 1 : 0; }
		TestEqual(TEXT("held L2: repeat after 14 frames (frame 0 is at the start: none)"), Repeats, 0);
		Frame(0);
		Frame(PadR2);
		TestEqual(TEXT("R2: next keyframe (4)"), Ctl.LastCommand, int32(CmdNextKey));
		Frame(0); Frame(0);
		Frame(PadL1); Frame(PadL1 | PadL2);
		TestEqual(TEXT("L1+L2: to the start (1)"), Ctl.LastCommand, int32(CmdToStart));
		Frame(0); Frame(0);
		Frame(PadR1);
		TestEqual(TEXT("R1: frame step only in Advanced (5)"), Ctl.LastCommand, bAdv ? int32(CmdStep) : 0);
		Frame(0); Frame(0);
		Frame(PadUp);
		TestTrue(TEXT("Up: help level up with sound 0x7D"), Ctl.HelpLevel == (bAdv ? 2 : 1) && Ctl.Events.Contains(EE::HelpSound));
		Frame(0);
		NextCams = 0;
		Frame(PadSelect);
		TestTrue(TEXT("Select: navigate + camera sound, next camera (twice past the replay cam in Normal)"), Ctl.Events.Contains(EE::CameraSound) && NextCams == (bAdv ? 1 : 2));
		Frame(0);
		Frame(PadRight);
		TestTrue(TEXT("Right with 2 racers: board audio"), Ctl.Events.Contains(EE::BoardAudio));
	}
	// 0x21BFD0
	SSXVibration::FPadActuators Pad;
	SSXVibration::FPlayerRumble P;
	P.Impact(Pad, FVector(0, 0, -3000), FVector(0, 0, 1), true);
	P.Feedback(Pad, 2.f, true);
	P.Update(Pad, true);
	TestTrue(TEXT("replay: no rumble"), Pad.Small == 0 && Pad.Big == 0);
	return true;
}

#endif
