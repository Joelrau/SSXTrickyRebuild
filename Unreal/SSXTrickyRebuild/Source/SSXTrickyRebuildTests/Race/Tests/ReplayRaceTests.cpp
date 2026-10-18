// 0x1927D8, 0x191B60, 0x191BE0
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Harness/RiderSetup.h"
#include "Input/RiderInputDriver.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXReplay.h"
#include "Race/SSXReplayKeyframe.h"
#include "Race/SSXReplayFile.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXReplay;

	constexpr int32 NumRacers = 4;

	FPadActions HumanPad(int32 F)
	{
		FPadActions A;
		A.Axes[FPadMapper::ATurn] = FMath::Sin(F * 0.021) * 0.7;
		const int32 Ph = F % 200;
		if (Ph < 28) { A.Held |= 1ull << FPadMapper::ActJump; }
		if (Ph == 0) { A.Pressed |= 1ull << FPadMapper::ActJump; }
		if (Ph >= 34 && Ph < 70) { A.Held |= 1ull; A.Axes[FPadMapper::ASpin] = 1.0; }
		if (Ph == 34) { A.Pressed |= 1ull; }
		if ((F / 300) % 2 == 1) { A.Held |= 1ull << FPadMapper::ActBoost; }
		if (F % 300 == 300 / 2) { A.Pressed |= 1ull << FPadMapper::ActBoost; }
		return A;
	}

	struct FLiveRace : public IReplayWorld
	{
		FRaceTrackData Data;
		FPatchWorld World;
		FRaceSession Session;
		TUniquePtr<FRiderSim> Sims[NumRacers];
		FRiderInputDriver Driver;
		FSSXReplay Replay;
		TArray<FSSXRaceKeyframe> Keys;
		int32 Restores = 0;
		uint32 RestoredFrame = 0;
		bool bEndRoutine = false;
		int32 HighlightCalls[21] = {};

		bool Load(FString& Err)
		{
			return Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) && World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err);
		}
		void Build()
		{
			static const int32 Chars[NumRacers] = { 7, 1, 9, 11 };
			const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
			for (int32 I = 0; I < NumRacers; ++I)
			{
				Sims[I] = MakeUnique<FRiderSim>();
				FRiderSim& S = *Sims[I];
				if (I == 0) { SSXRiderSetup::InstallStates(S, Driver, true); }
				else { SSXRiderSetup::RegisterStates(S, true); }
				S.SetWorld(&World);
				S.Profile.Character = Chars[I];
				if (Table.IsLoaded())
				{
					if (I == 0) { SSXRider::ApplyNewSavePlayerProfile(S.Profile, Chars[I], 0); }
					else { SSXRider::ApplyComputerProfile(S.Profile, Chars[I], 0, SSXRider::ResolveBoard(Table, Chars[I], INDEX_NONE, 2)); }
				}
				S.Reset();
			}
			FRaceConfig Cfg;
			Cfg.Mode = ERaceEventMode::CircuitRace;
			Session.Init(Data, Cfg);
			for (int32 I = 0; I < NumRacers; ++I) { Session.AddRacer(Sims[I].Get(), I == 0); }
			Replay.Init(NumRacers, 1, false, this);
			Replay.AttachHuman(0);
			Session.Start();
		}

		virtual void CaptureKeyframe(int32 Key, uint32 Frame, bool) override
		{
			if (Keys.Num() <= Key) { Keys.SetNum(Key + 1); }
			Keys[Key].Save(Session, Frame);
		}
		virtual void RestoreKeyframe(int32 Key, uint32 Frame) override
		{
			++Restores;
			RestoredFrame = Frame;
			Keys[Key].Load(Session);
			Session.ClearCues();
		}
		virtual int32 FocusPlayer() const override { return 0; }

		bool Frame(int32 F)
		{
			Replay.Update();
			if (Replay.IsPlaying() && Replay.IsWorldFrozen()) return false;
			const ERacePhase Prev = Session.GetPhase();
			Session.Tick();
			if (Prev != ERacePhase::StartRace && Session.GetPhase() == ERacePhase::StartRace) { Replay.OnStartRace(); }
			if (!bEndRoutine && Session.GetRequestedPhase() == ERacePhase::EndRace) { bEndRoutine = true; Replay.EndRecording(); }
			if (Replay.IsPlayingInput())
			{
				uint32 W = 0;
				Replay.RecordOrPlayInput(0, W);
				Driver.StepWord(*Sims[0], W);
			}
			else
			{
				Driver.Step(*Sims[0], HumanPad(F));
				uint32 W = Driver.GetLastWord();
				Replay.RecordOrPlayInput(0, W);
			}
			for (int32 I = 1; I < NumRacers; ++I) { Sims[I]->Step(FRiderInputWord()); }
			Session.ClearCues();
			for (int32 I = 0; I < NumRacers; ++I)
			{
				for (const FRiderState::FHighlightCall& C : Sims[I]->State.TakeHighlightCalls())
				{
					if (I == 0)
					{
						++HighlightCalls[FMath::Clamp(C.Slot, 0, 20)];
						Replay.HighlightCall(0, C.Slot, C.Arg);
					}
				}
			}
			return true;
		}
		void Positions(TArray<FVector>& Out) const
		{
			Out.SetNum(NumRacers);
			for (int32 I = 0; I < NumRacers; ++I) { Out[I] = Sims[I]->State.Position; }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayLiveRaceTest, "SSX.Replay.LiveRace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayLiveRaceTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FLiveRace> Race = MakeUnique<FLiveRace>();
	FString Err;
	if (!Race->Load(Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("gari race / sim data not available (%s): skipped"), *Err));
		return true;
	}
	FLiveRace& W = *Race;
	W.Build();
	FSSXReplay& R = W.Replay;

	int32 Guard = 0;
	while (R.GetState() != Record && Guard++ < 60 * 120) { W.Frame(0); }
	if (!TestEqual(TEXT("recording after StartRace (0xB stage reset, 0xC record start)"), R.GetState(), int32(Record)))
	{
		return false;
	}
	constexpr int32 N = 2600;
	TArray<TArray<FVector>> Rec;
	Rec.SetNum(N + 1);
	W.Positions(Rec[0]);
	int32 F = 0;
	while (int32(R.GetRecordFrame()) < N)
	{
		W.Frame(F++);
		const int32 Idx = int32(R.GetRecordFrame()) - 1;
		if (Rec.IsValidIndex(Idx)) { W.Positions(Rec[Idx]); }
	}
	TestEqual(TEXT("recorded frames"), int32(R.GetRecordedLength()), N);
	TestTrue(TEXT("the race is running (phase Race)"), W.Session.GetPhase() == ERacePhase::Race);
	TestTrue(TEXT("the human's words are run-length coded"), R.GetInput(0).NumRuns() > 20 && R.GetInput(0).NumRuns() < N);
	AddInfo(FString::Printf(TEXT("highlight calls: start %d value %d impact %d commit %d clear %d"), W.HighlightCalls[15], W.HighlightCalls[16],
		W.HighlightCalls[17], W.HighlightCalls[19], W.HighlightCalls[20]));
	TestTrue(TEXT("the jumps raise highlight starts (slot 15, Cruise jump latch)"), W.HighlightCalls[15] > 0);
	TestTrue(TEXT("and commits (slot 19, landing / spawn)"), W.HighlightCalls[19] > 0);
	int32 Clips = 0;
	for (const int32 C : R.GetHighlights(0).Slots) { Clips += C >= 0 ? 1 : 0; }
	TestTrue(TEXT("highlight slots filled (0x1937D0)"), Clips > 0);

	// 0x191230
	R.EndRecording();
	W.Frame(F);
	TestEqual(TEXT("recording ended: 0xD"), R.GetState(), int32(RecordEnded));
	const FVector EndPos = W.Sims[0]->State.Position;   // 0x191B60
	const TArray<uint32> KeyFrames = R.KeyframeFrames();
	TestTrue(TEXT("keyframes: start, the 2400 lock, the end (plus the highlight keyframes)"),
		KeyFrames.Contains(0u) && KeyFrames.Contains(2400u) && KeyFrames.Contains(uint32(N)));

	R.StartFullReplay();
	TestEqual(TEXT("state 7"), R.GetState(), int32(Started));
	R.SetCommand(CmdToStart);
	W.Frame(0);
	W.Frame(0);
	R.SetCommand(CmdPlayToggle);
	int32 Played = 0, Mismatch = 0, FirstBad = -1;
	TArray<FVector> Now;
	for (Guard = 0; Guard < N + 20 && (R.GetState() == Play || R.GetCommand() == CmdPlayToggle); ++Guard)
	{
		if (W.Frame(0))
		{
			const int32 P = int32(R.GetPlayFrame()) - 1;
			++Played;
			W.Positions(Now);
			if (!Rec.IsValidIndex(P) || Now != Rec[P]) { ++Mismatch; if (FirstBad < 0) FirstBad = P; }
		}
	}
	TestEqual(TEXT("world frames played (N - 1, 0x18F020)"), Played, N - 1);
	TestEqual(TEXT("every racer bit-exact per frame (first mismatch frame logged)"), Mismatch, 0);
	if (FirstBad >= 0) { AddInfo(FString::Printf(TEXT("first mismatch at frame %d"), FirstBad)); }
	TestEqual(TEXT("paused at the end"), R.GetState(), int32(Paused));

	uint32 PrevKey = 0;
	for (const uint32 K : KeyFrames) { if (K < uint32(N)) PrevKey = FMath::Max(PrevKey, K); }
	R.SetCommand(CmdPrevKey);
	W.Frame(0);
	TestEqual(TEXT("seek to the previous keyframe"), R.GetPlayFrame(), PrevKey);
	TestTrue(TEXT("a mid-race keyframe"), PrevKey >= 2400u);
	W.Frame(0);
	R.SetCommand(CmdPlayToggle);
	Mismatch = 0;
	for (int32 I = 0; I < 150; ++I)
	{
		if (W.Frame(0))
		{
			const int32 P = int32(R.GetPlayFrame()) - 1;
			W.Positions(Now);
			if (!Rec.IsValidIndex(P) || Now != Rec[P]) { ++Mismatch; }
		}
	}
	TestEqual(TEXT("exact from the mid-race keyframe"), Mismatch, 0);

	// 0x191BE0
	R.StopPlayback();
	TestEqual(TEXT("exit: 0xD"), R.GetState(), int32(RecordEnded));
	TestEqual(TEXT("exit restores the end keyframe"), W.Sims[0]->State.Position, EndPos);

	// 0x193C68, 0x190878, 0x18F0D8
	TestTrue(TEXT("state 9 set"), R.OnEndRaceAttract());
	W.Frame(0);
	TestTrue(TEXT("attract: looping playlist of the highlights"), R.IsLooping() && R.HasPlaylist() && R.GetPlaylist().Num() == Clips);
	Mismatch = 0;
	int32 ClipFrames = 0;
	FString ClipInfo;
	for (const int32 C : R.GetPlaylist()) { ClipInfo += FString::Printf(TEXT(" [key f%u len %d type %d v %.3f]"), R.GetKey(R.GetClip(C).Key).Frame, R.GetClip(C).Length, R.GetClip(C).Type, R.GetClip(C).Value); }
	AddInfo(TEXT("playlist:") + ClipInfo);
	int32 Expect = int32(W.RestoredFrame);
	for (int32 I = 0; I < 600; ++I)
	{
		const int32 RestoresBefore = W.Restores;
		const bool bWorld = W.Frame(0);
		if (W.Restores != RestoresBefore) { Expect = int32(W.RestoredFrame); }
		if (bWorld)
		{
			const int32 P = Expect++;
			W.Positions(Now);
			++ClipFrames;
			if (!Rec.IsValidIndex(P) || Now != Rec[P])
			{
				if (Mismatch == 0)
				{
					AddInfo(FString::Printf(TEXT("first clip mismatch: play frame %d, clip %d, rider0 %s vs %s"), P, R.GetPlaylistIndex(),
						*Now[0].ToString(), Rec.IsValidIndex(P) && Rec[P].Num() ? *Rec[P][0].ToString() : TEXT("-")));
				}
				++Mismatch;
			}
		}
	}
	TestTrue(TEXT("highlight clips played"), ClipFrames > 0);
	TestEqual(TEXT("highlight clips bit-exact"), Mismatch, 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayLiveFileTest, "SSX.Replay.FileRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayLiveFileTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FLiveRace> A = MakeUnique<FLiveRace>();
	FString Err;
	if (!A->Load(Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("gari race / sim data not available (%s): skipped"), *Err));
		return true;
	}
	const FGameRng RngB(0xB0Bu);
	FGameRng::StreamA() = FGameRng(0xA11CEu);
	FGameRng::StreamB() = RngB;
	SSXReplayFile::FSetup Setup;
	Setup.NumRacers = NumRacers;
	Setup.Racers[0].Port = 0;
	A->Build();
	FSSXReplay& R = A->Replay;
	int32 Guard = 0;
	while (R.GetState() != Record && Guard++ < 60 * 120) { A->Frame(0); }
	constexpr int32 N = 2600;
	TArray<TArray<FVector>> Rec;
	Rec.SetNum(N + 1);
	A->Positions(Rec[0]);
	int32 F = 0;
	while (int32(R.GetRecordFrame()) < N)
	{
		A->Frame(F++);
		const int32 Idx = int32(R.GetRecordFrame()) - 1;
		if (Rec.IsValidIndex(Idx)) { A->Positions(Rec[Idx]); }
	}
	R.EndRecording();
	A->Frame(F);
	R.StartFullReplay();
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("replay file written"), SSXReplayFile::Write(R, Setup, Bytes)))
	{
		return false;
	}
	TArray<uint32> Saved = R.KeyframeFrames();
	Saved.RemoveAt(Saved.Num() - 1);   // 0x191B60

	TUniquePtr<FLiveRace> B = MakeUnique<FLiveRace>();
	B->Load(Err);
	SSXReplayFile::FSetup Loaded;
	TestTrue(TEXT("setup read"), SSXReplayFile::ReadSetup(Bytes, Loaded) && Loaded.NumRacers == NumRacers && Loaded.RacerOfHuman(0) == 0);
	FGameRng::StreamA() = FGameRng(0xA11CEu);
	FGameRng::StreamB() = RngB;
	B->Build();
	FSSXReplay& L = B->Replay;
	if (!TestTrue(TEXT("replay object read"), SSXReplayFile::ReadReplay(Bytes, L)))
	{
		return false;
	}
	L.BeginRebuild();
	Guard = 0;
	while (L.IsRebuilding() && Guard++ < 60 * 120 + N + 10) { B->Frame(0); }
	TestFalse(TEXT("keyframes rebuilt"), L.IsRebuilding());
	TestEqual(TEXT("the same keyframes"), L.KeyframeFrames(), Saved);
	TestTrue(TEXT("loaded: state 8, command 1"), L.IsLoaded() && L.GetState() == int32(State8) && L.GetCommand() == int32(CmdToStart));
	B->Frame(0);   // 0x191830
	L.SetCommand(CmdPlayToggle);
	int32 Played = 0, Mismatch = 0, FirstBad = -1;
	TArray<FVector> Now;
	for (Guard = 0; Guard < N + 20 && (L.GetState() == Play || L.GetCommand() == CmdPlayToggle); ++Guard)
	{
		if (B->Frame(0))
		{
			const int32 P = int32(L.GetPlayFrame()) - 1;
			++Played;
			B->Positions(Now);
			if (!Rec.IsValidIndex(P) || Now != Rec[P]) { ++Mismatch; if (FirstBad < 0) FirstBad = P; }
		}
	}
	TestEqual(TEXT("world frames played (N - 1)"), Played, N - 1);
	TestEqual(TEXT("the loaded replay is bit-exact against the recorded race"), Mismatch, 0);
	if (FirstBad >= 0) { AddInfo(FString::Printf(TEXT("first mismatch at frame %d"), FirstBad)); }
	L.SetCommand(CmdPrevKey);
	B->Frame(0);
	const uint32 Mid = L.GetPlayFrame();
	TestTrue(TEXT("seek to a mid-race keyframe"), Mid >= 2400u && Mid < uint32(N));
	B->Frame(0);
	L.SetCommand(CmdPlayToggle);
	Mismatch = 0;
	for (int32 I = 0; I < 120; ++I)
	{
		if (B->Frame(0))
		{
			const int32 P = int32(L.GetPlayFrame()) - 1;
			B->Positions(Now);
			if (!Rec.IsValidIndex(P) || Now != Rec[P]) { ++Mismatch; }
		}
	}
	TestEqual(TEXT("exact from the rebuilt mid-race keyframe"), Mismatch, 0);
	return !HasAnyErrors();
}

#endif
