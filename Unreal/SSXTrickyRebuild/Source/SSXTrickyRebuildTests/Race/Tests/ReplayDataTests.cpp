#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Race/GameRng.h"
#include "Audio/SSXAnnouncer.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXCrowdSystem.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXRaceAudioFlow.h"
#include "Audio/SSXSpeechSystem.h"
#include "Effects/TrackEffects.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Particles/SSXParticleWorld.h"
#include "Race/RaceRules.h"
#include "Race/SSXReplay.h"
#include "Race/SSXReplayFile.h"
#include "Sim/Air/RiderMessages.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXMemCard.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"
#include "World/SSXPropAnimation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SSXReplayDataTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Record(FSSXReplay& R, int32 N)
	{
		R.Init(4, 1, true, nullptr);
		R.AttachHuman(0);
		R.OnStartRace();
		for (int32 I = 0; I < N + 2; ++I)
		{
			R.Update();
			uint32 W = uint32(I / 25) * 3 + 1;
			R.RecordOrPlayInput(0, W);
		}
		R.EndRecording();
		R.Update();
	}

	struct FFound { int32 Inst = INDEX_NONE; int32 Rec = INDEX_NONE; };
	FFound FindNode(const SSXEffects::FEffectData& D, TFunctionRef<bool(int32)> TypeOk, int32 NotInst = INDEX_NONE)
	{
		FFound Out;
		TSet<TPair<int32, int32>> Seen;
		TFunction<void(int32, int32)> Walk = [&](int32 E, int32 Inst)
		{
			if (Out.Inst != INDEX_NONE || E < 0 || E >= D.Effects.Num() || Seen.Contains({ E, Inst })) return;
			Seen.Add({ E, Inst });
			for (int32 K = D.Effects[E].X; K < D.Effects[E].X + D.Effects[E].Y && Out.Inst == INDEX_NONE; ++K)
			{
				const SSXEffects::FRecord& Rc = D.Records[K];
				if (Rc.Type == 0 && TypeOk(D.I(Rc, 0)) && Inst != NotInst) { Out = { Inst, K }; }
				else if (Rc.Type == 7) { Walk(D.I(Rc, 1), D.I(Rc, 0)); }
			}
		};
		for (int32 Inst = 0; Inst < D.NumInstances() && Out.Inst == INDEX_NONE; ++Inst)
		{
			for (int32 Slot = 0; Slot < SSXEffects::NumSlots && Out.Inst == INDEX_NONE; ++Slot)
			{
				Seen.Reset();
				Walk(D.EffectFor(Inst, Slot), Inst);
			}
		}
		return Out;
	}
}

using namespace SSXReplayDataTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayDataAudioGatesTest, "SSX.Replay.Data.AudioGates", SSXReplayDataTests::Flags)
bool FSSXReplayDataAudioGatesTest::RunTest(const FString&)
{
	auto RandA = []() { return 0u; };
	// 0x231398, 0x234610, 0x21BFD0
	{
		FSSXSpeechSystem Sys;
		FSSXAnnouncer A;
		A.Reset(2, 0, 3);
		A.bReplay = true;
		A.Bail(Sys, false, 0, RandA);
		A.Message(Sys, SSXMsg::BigAir, 0, false, RandA);
		A.Grudge(Sys, false, RandA);
		A.ShoveHit(Sys, false, RandA);
		A.KnockedDown(Sys, false, 0, RandA);
		A.TrackBoostOp(Sys, 17);
		A.BoostEmpty(Sys, false, RandA);
		A.TrickAnnounce(Sys, 0x0100, 0, 5000, false, false, RandA);
		A.Go(Sys, RandA);
		TestEqual(TEXT("replay playback: no MC request"), A.Posted.Num(), 0);
		A.bReplay = false;
		A.TrackBoostOp(Sys, 17);
		TestTrue(TEXT("outside playback: track boost op 17 posts MC 0x2047 (no class gate)"), A.Posted.Num() == 1 && (A.Posted[0] & 0xFFFF) == 0x2047);
	}
	// 0x113600, 0x216930, 0x215288
	{
		USSXAudioSubsystem* Audio = NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
		SSXRaceAudio::FFlow& F = Audio->GetRaceFlow();
		F.Load(0, false, false, false, 1.0, [Audio]() { return Audio->Rand15(); });
		F.Commands.Reset();
		SSXGameAudio::CountdownFrame(*Audio, ERacePhase::Countdown, ERacePhase::Countdown, -60.0, 100, true);
		SSXGameAudio::CountdownFrame(*Audio, ERacePhase::Countdown, ERacePhase::Countdown, 0.0, 101, true);
		TestTrue(TEXT("playback: no beep, no GO"), F.Commands.Num() == 0 && !F.bGo);
		SSXGameAudio::CountdownFrame(*Audio, ERacePhase::Countdown, ERacePhase::Countdown, -60.0, 102, false);
		TestTrue(TEXT("live: beep #78"), F.Commands.Num() == 1 && F.Commands[0].Kind == SSXRaceAudio::FCommand::Beep && F.Commands[0].Value == 78);
		// 0x1EF990, 0x215220
		F.Commands.Reset();
		F.ReplayGo();
		const bool bOrder = F.Commands.Num() == 3 && F.Commands[0].Kind == SSXRaceAudio::FCommand::StopIntro
			&& F.Commands[1].Kind == SSXRaceAudio::FCommand::PrepareSong
			&& F.Commands[2].Kind == SSXRaceAudio::FCommand::UnpauseSong;
		TestTrue(TEXT("replay GO: stop intro, prepare the song (event 0, control 80), unpause"), bOrder);
		TestEqual(TEXT("replay GO: audio state 6"), F.State, 6);
		TestEqual(TEXT("replay GO: control 80"), F.Control, 80);
		// 0x2116D8
		Audio->SeedRaceRandom(77);
		const uint32 CallsBefore = FGameRng::StreamA().S[5];
		Audio->BeginLoadedReplayRace(TEXT("gari"), 0);
		TestTrue(TEXT("loaded replay race load: audio state 6, duck mode 4, no command, not GO"),
			F.State == 6 && F.DuckMode == 4 && F.Commands.Num() == 0 && !F.bGo && !F.bIntroQueued);
		TestTrue(TEXT("loaded replay race load: no song player, no intro stream"),
			!Audio->GetPathfinderPlayer().IsValid() && Audio->GetIntroBig().IsEmpty() && Audio->GetIntroQueueLength() == 0
			&& Audio->GetRaceCommandLog().Num() == 0);
		TestEqual(TEXT("loaded replay race load: no rand15 draw (song advance, intro queue; stream A call counter)"),
			FGameRng::StreamA().S[5], CallsBefore);
		// 0x20ED20
		UObject* Target = NewObject<UGameInstance>();
		UObject* Other = NewObject<UGameInstance>();
		Audio->SetReplayPlayback(true, 2, Target);
		TestTrue(TEXT("playback: the target is the listener (AI too)"), Audio->IsListenerPawn(Target, false) && !Audio->IsListenerPawn(Other, true));
		TestEqual(TEXT("playback: target racer"), Audio->GetReplayTargetRacer(), 2);
		Audio->SetReplayPlayback(false, 2, Target);
		TestTrue(TEXT("live: the human"), Audio->IsListenerPawn(Other, true) && !Audio->IsListenerPawn(Target, false) && Audio->GetReplayTargetRacer() == INDEX_NONE);
	}
	// 0x193C68, 0x210A38
	{
		FSSXReplay R;
		R.Init(4, 1, false, nullptr);
		TestTrue(TEXT("0x193C68 sets state 9"), R.OnEndRaceAttract() && R.GetState() == int32(SSXReplay::Attract));
		TestFalse(TEXT("only once"), R.OnEndRaceAttract());
		USSXBoardSoundComponent* B = NewObject<USSXBoardSoundComponent>();
		B->RestartVoices();
		TestTrue(TEXT("0x210A38: board voices dropped (made again by the next update)"), B->VoiceRestarts == 1 && B->NumPlayingLoops() == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayDataPropKeyframeTest, "SSX.Replay.Data.PropKeyframe", SSXReplayDataTests::Flags)
bool FSSXReplayDataPropKeyframeTest::RunTest(const FString&)
{
	SSXEffects::FEffectData Fx;
	if (!Fx.Load(SSXEffects::FEffectData::DefaultPath(TEXT("gari"))))
	{
		SSXTest::Skip(*this, TEXT("gari.effects.bin missing; skipped"));
		return true;
	}
	const FFound Movie = FindNode(Fx, [](int32 T) { return T == SSXEffects::TypeMovie; });
	if (!TestTrue(TEXT("gari has a Movie record"), Movie.Inst != INDEX_NONE))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXReplayPropWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	USSXPropAnimation* Props = World->GetSubsystem<USSXPropAnimation>();
	if (!TestNotNull(TEXT("prop subsystem"), Props))
	{
		return false;
	}
	Props->BeginTrack(TEXT("gari"), &Fx, nullptr, nullptr);
	Props->OnNodeCreated(Movie.Inst, SSXEffects::TypeMovie, Movie.Rec, nullptr, false);
	FFound Clock = FindNode(Fx, [](int32 T) { return T >= SSXEffects::TypeAnimObject && T <= 0x102; }, Movie.Inst);
	int32 ClockType = INDEX_NONE;
	if (Clock.Inst != INDEX_NONE)
	{
		ClockType = Fx.I(Fx.Records[Clock.Rec], 0);
		Props->OnNodeCreated(Clock.Inst, ClockType, Clock.Rec, nullptr, false);
		if (!Props->FindAnim(Clock.Inst)) { ClockType = INDEX_NONE; }
	}
	SSXProps::FPropController Ctl;
	Ctl.Type = SSXProps::EControllerType::UVScroll;
	Ctl.Instance = 7;
	Props->GetControllers().Add(Ctl);

	for (int32 K = 0; K < 37; ++K) { Props->StepFrame(); }
	USSXPropAnimation::FReplayKeyframe Key;
	Props->SaveReplay(Key);
	const int32 MovieRefs = Props->GetMovieStream().Refs;
	const double T0 = ClockType != INDEX_NONE ? Props->FindAnim(Clock.Inst)->T : 0.0;
	for (int32 K = 0; K < 10; ++K) { Props->StepFrame(); }
	const double T10 = ClockType != INDEX_NONE ? Props->FindAnim(Clock.Inst)->T : 0.0;
	const int32 Counter47 = Props->GetMovieStream().Counter;
	for (int32 K = 0; K < 100; ++K) { Props->StepFrame(); }
	Props->OnNodeDestroyed(Movie.Inst, SSXEffects::TypeMovie);
	Props->GetControllers().Remove(7);
	TestFalse(TEXT("before the restore: the movie node is gone"), Props->HasMovie(Movie.Inst));

	// 0x18F6C0, 0x18F800
	Props->LoadReplay(Key);
	TestTrue(TEXT("restore: the Movie node is back"), Props->HasMovie(Movie.Inst));
	TestTrue(TEXT("restore: the stream re-opened from its start (0x148AD0 -> 0x1FFF30)"), Props->GetMovieStream().bOpen
		&& Props->GetMovieStream().Refs == MovieRefs && Props->GetMovieStream().Counter == 0);
	TestNotNull(TEXT("restore: the UV scroll node is back (cUVScrollNode saves itself)"), Props->GetControllers().Find(7));
	if (ClockType != INDEX_NONE)
	{
		TestEqual(TEXT("restore: the animation clock is the saved one (not restarted)"), Props->FindAnim(Clock.Inst)->T, T0);
	}
	else
	{
		SSXTest::Skip(*this, TEXT("no AnimObject node (propanim data missing): clock check skipped"));
	}
	for (int32 K = 0; K < 10; ++K) { Props->StepFrame(); }
	if (ClockType != INDEX_NONE)
	{
		TestEqual(TEXT("10 frames on: the same clock as 10 frames after the save"), Props->FindAnim(Clock.Inst)->T, T10);
	}
	TestTrue(TEXT("the video counts from the restore, not from the save"), Props->GetMovieStream().Counter == 10 && Counter47 == 47);

	// 0x147F30
	if (USSXParticleWorld* Pw = World->GetSubsystem<USSXParticleWorld>())
	{
		for (int32 Ri = 0; Ri < Fx.Records.Num() && Pw->NumTrackEmitters() == 0; ++Ri)
		{
			if (Fx.Records[Ri].Size >= 52 * 4) { Pw->AddTrackEmitter(Fx, 3, 0, Ri, nullptr); }
		}
		if (Pw->NumTrackEmitters() > 0)
		{
			USSXParticleWorld::FReplayKeyframe PKey;
			Pw->SaveReplay(PKey);
			Pw->ClearTrackEmitters();
			Pw->LoadReplay(PKey);
			TestEqual(TEXT("restore: the track emitter is back"), Pw->NumTrackEmitters(), PKey.Emitters.Num());
		}
		else
		{
			SSXTest::Skip(*this, TEXT("no emitter record built a burst: emitter check skipped"));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayDataFileTest, "SSX.Replay.Data.File", SSXReplayDataTests::Flags)
bool FSSXReplayDataFileTest::RunTest(const FString&)
{
	FSSXReplay R;
	Record(R, 2500);
	SSXReplay::FCamera Cam;
	Cam.A = 1.5f; Cam.Target = 2; Cam.Mode = 8;
	R.GetTrack(0).Insert(1200, Cam, true);
	TestEqual(TEXT("recording ended (0xD)"), R.GetState(), int32(SSXReplay::RecordEnded));
	R.StartFullReplay();
	SSXReplayFile::FSetup Setup;
	Setup.GameMode = 2; Setup.Venue = 3; Setup.Difficulty = 1; Setup.NumRacers = 4; Setup.Seed2B00 = 12345u;
	Setup.Racers[1].Port = 0; Setup.Racers[1].Char38 = 7; Setup.Racers[1].BoardClass = 2; Setup.Racers[2].BoardId = 4 * 12 + 4;
	Setup.Racers[2].Char38 = 4;
	TArray<uint8> Bytes;
	TestTrue(TEXT("written"), SSXReplayFile::Write(R, Setup, Bytes));
	TestEqual(TEXT("the 0x80000-byte buffer (0x1CE778(0))"), Bytes.Num(), 0x80000);
	TestEqual(TEXT("643 KB on the card (0x23FF18)"), FSSXMemCard::KbRequired(Bytes.Num()), 643);
	int32 Len = 0;
	FMemory::Memcpy(&Len, Bytes.GetData() + SSXReplayFile::LengthAt, 4);
	TestEqual(TEXT("+0x7FFF8 = 0x7F6D8 (padded stream)"), Len, SSXReplayFile::StreamMax);
	uint32 Magic = 0;
	FMemory::Memcpy(&Magic, Bytes.GetData() + SSXReplayFile::SetupBytes, 4);
	TestEqual(TEXT("stream header magic 0x12300003 at 0x920"), Magic, SSXReplayFile::HeaderMagic);
	TestTrue(TEXT("checksum 0xDEADBEEF + signed bytes (0x2912E8)"), SSXReplayFile::Validate(Bytes));
	TArray<uint8> Bad = Bytes;
	Bad[0x920 + 100] ^= 0x40;
	TestFalse(TEXT("a changed byte fails the check"), SSXReplayFile::Validate(Bad));
	SSXReplayFile::FSetup Back;
	TestTrue(TEXT("setup read"), SSXReplayFile::ReadSetup(Bytes, Back));
	TestTrue(TEXT("setup round trip (RS fields)"), Back.GameMode == 2 && Back.Venue == 3 && Back.Difficulty == 1 && Back.NumRacers == 4
		&& Back.Seed2B00 == 12345u && Back.NumHumans() == 1 && Back.RacerOfHuman(0) == 1 && Back.Racers[1].Char38 == 7 && Back.Racers[2].Board() == 4);
	TestTrue(TEXT("setup CRC-16 at +0x334 (0x2CE04C)"), SSXReplayFile::SetupCrcValid(Bytes.GetData()));

	FSSXReplay L;
	L.Init(4, 1, true, nullptr);
	L.AttachHuman(0);
	TestTrue(TEXT("replay object read (0x194910)"), SSXReplayFile::ReadReplay(Bytes, L));
	TestTrue(TEXT("loaded (+0x4C), state 8, command 1"), L.IsLoaded() && L.GetState() == int32(SSXReplay::State8) && L.GetCommand() == int32(SSXReplay::CmdToStart));
	TArray<uint32> Saved = R.KeyframeFrames();
	Saved.RemoveAt(Saved.Num() - 1);   // 0x191B60
	TestEqual(TEXT("keyframes but the end one, in order"), L.KeyframeFrames(), Saved);
	TestTrue(TEXT("frames and length"), L.GetRecordFrame() == R.GetRecordFrame() && L.GetRecordedLength() == R.GetRecordedLength());
	TestTrue(TEXT("the input recorder"), L.GetInput(0).Words == R.GetInput(0).Words && L.GetInput(0).Runs == R.GetInput(0).Runs);
	TestTrue(TEXT("the camera track (Advanced cut at 1200)"), L.GetTrack(0).Count == R.GetTrack(0).Count && L.GetTrack(0).Cuts.Last().Frame == 1200u
		&& L.GetTrack(0).Cuts.Last().Cam == Cam);

	L.BeginRebuild();
	TestTrue(TEXT("rebuilding"), L.IsRebuilding());
	L.OnStartRace();
	int32 Frames = 0;
	uint32 FirstWord = 0;
	for (; L.IsRebuilding() && Frames < 4000; ++Frames)
	{
		L.Update();
		uint32 W = 0;
		if (L.IsPlayingInput() && L.RecordOrPlayInput(0, W) && Frames == 2) { FirstWord = W; }
	}
	TestFalse(TEXT("rebuild done"), L.IsRebuilding());
	TestTrue(TEXT("then state 8 + command 1 (as 0x194910 leaves it)"), L.GetState() == int32(SSXReplay::State8) && L.GetCommand() == int32(SSXReplay::CmdToStart));
	TestEqual(TEXT("the words play from the start"), FirstWord, 1u);
	TestEqual(TEXT("the keyframe list is unchanged"), L.KeyframeFrames(), Saved);
	TestEqual(TEXT("+0x2C put back"), L.GetRecordFrame(), R.GetRecordFrame());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayDataSaveFlowTest, "SSX.Replay.Data.SaveFlow", SSXReplayDataTests::Flags)
bool FSSXReplayDataSaveFlowTest::RunTest(const FString&)
{
	using namespace SSXFe;
	const FString PrevRoot = FSSXMemCard::RootOverride();
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardReplay"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
		FSSXMemCard::RootOverride() = PrevRoot;
	};
	FSSXReplay R;
	Record(R, 300);
	SSXReplayFile::FSetup Setup;
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCheats C;
	{
		FSSXRaceUIHostStub H;
		FSSXOverlayManager O;
		O.Init(&P, &C, nullptr);
		O.SetHost(&H);
		H.ReplayObj = &R;
		H.bCanReplay = true;
		TestTrue(TEXT("file built"), SSXReplayFile::Write(R, Setup, H.ReplayFileBytes));
		O.Activate(OvReplay);
		O.Pad(0, PadStart);
		TestTrue(TEXT("menu (state 2), exit selected"), O.ReplayState == 2 && O.ReplaySel == 1);
		O.Pad(0, PadUp);
		O.Pad(0, PadCross);
		TestTrue(TEXT("save: state 0, the card manager runs (LOC 64)"), O.ReplayState == 0 && O.ReplayCard.IsActive()
			&& O.ReplayCard.Mode == FSSXMemCard::EMode::Replay && O.ReplayCard.MessageLoc() == 64 && O.AnyModal());
		for (int32 N = 0; N < 400 && O.ReplayCard.State != FSSXMemCard::SaveList; ++N) { O.Tick(); }
		TestEqual(TEXT("the save list (4 replay files)"), int32(O.ReplayCard.State), int32(FSSXMemCard::SaveList));
		TestEqual(TEXT("4 files"), O.ReplayCard.MaxFiles(), 4);
		O.Pad(0, PadCross);
		TestEqual(TEXT("saving (LOC 451)"), O.ReplayCard.MessageLoc(), 451);
		for (int32 N = 0; N < 400 && O.ReplayCard.State != FSSXMemCard::SaveDone && O.ReplayCard.State != FSSXMemCard::SaveFailed; ++N) { O.Tick(); }
		TestEqual(TEXT("save complete (LOC 447)"), int32(O.ReplayCard.State), int32(FSSXMemCard::SaveDone));
		TArray<uint8> OnCard;
		TestTrue(TEXT("BASLUS-20326-REP0001 written, valid"), FFileHelper::LoadFileToArray(OnCard, *FSSXMemCard::FilePath(FSSXMemCard::ReplayName(0)))
			&& OnCard == H.ReplayFileBytes && SSXReplayFile::Validate(OnCard));
		O.Pad(0, PadCross);
		O.Tick();
		TestEqual(TEXT("the manager idle: back to the menu (0x1EF750 -> state 2)"), O.ReplayState, 2);
		TestTrue(TEXT("the menu is up"), O.IsReplayMenuOpen());
	}
	{
		// the FE load (Options item 2): the file passes the checksum, the loaded replay is handed over
		FSSXMemCard M;
		M.Begin(FSSXMemCard::EMode::Replay, false, &P);
		for (int32 N = 0; N < 400 && M.State != FSSXMemCard::LoadList; ++N) { M.Tick(); }
		M.Input(FSSXMemCard::InAccept);
		for (int32 N = 0; N < 300 && M.IsActive(); ++N) { M.Tick(); }
		TestTrue(TEXT("load: idle with the loaded replay (no LOC 371)"), !M.IsActive() && M.bLoadedReplay && SSXReplayFile::Validate(M.LoadedReplay));
		TArray<uint8> Broken = M.LoadedReplay;
		Broken[0x1000] ^= 1;
		FFileHelper::SaveArrayToFile(Broken, *FSSXMemCard::FilePath(FSSXMemCard::ReplayName(0)));
		M.Begin(FSSXMemCard::EMode::Replay, false, &P);
		for (int32 N = 0; N < 400 && M.State != FSSXMemCard::LoadList; ++N) { M.Tick(); }
		M.Input(FSSXMemCard::InAccept);
		for (int32 N = 0; N < 20 && M.State != FSSXMemCard::LoadFailed; ++N) { M.Tick(); }
		TestTrue(TEXT("checksum failure -> 0x22 LOC 372"), M.State == FSSXMemCard::LoadFailed && M.MessageLoc() == 372 && !M.bLoadedReplay);
	}
	{
		FSSXRaceUIHostStub H;
		FSSXOverlayManager O;
		O.Init(&P, &C, nullptr);
		O.SetHost(&H);
		FSSXReplay L;
		Record(L, 300);
		L.SetLoaded(true);
		H.ReplayObj = &L;
		H.bCanReplay = true;
		H.bLoadedReplayStart = true;
		O.Tick();
		TestTrue(TEXT("loaded replay: the replay overlay and its control"), O.IsActive(OvReplay) && O.IsActive(OvReplayControl) && O.ReplayState == 1);
		O.Pad(0, PadStart);
		O.Pad(0, PadCross);
		TestEqual(TEXT("exit of a loaded replay: the confirm (state 4)"), O.ReplayState, 4);
		TestTrue(TEXT("modal while it asks"), O.AnyModal() && O.IsActive(OvReplay));
		O.Pad(0, PadTriangle);
		TestEqual(TEXT("Triangle (0x1AAC08 = 2): back to the menu"), O.ReplayState, 2);
		O.Pad(0, PadCross);
		O.Pad(0, PadCross);
		TestTrue(TEXT("X: exit (state 5), no return slot: the race is left"), !O.IsActive(OvReplay) && !O.IsActive(OvReplayControl)
			&& H.ReplayStops == 1 && H.ExitCode == 2);
	}
	return true;
}

// 0x1128B8, 0x2213C8, 0x221330
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayDataRestoreAudioTest, "SSX.Replay.Data.RestoreAndAudio", SSXReplayDataTests::Flags)
bool FSSXReplayDataRestoreAudioTest::RunTest(const FString&)
{
	{
		// 0x190878, 0x18F6C0, 0x191BE0
		struct FWorld : SSXReplay::IReplayWorld
		{
			int32 Restores = 0, Resets = 0;
			virtual void RestoreKeyframe(int32, uint32) override { ++Restores; }
			virtual void ResetRiderEffects() override { ++Resets; }
		} W;
		FSSXReplay R;
		R.Init(2, 1, false, &W);
		R.OnStartRace();
		for (int32 F = 0; F < 3002; ++F) { R.Update(); }
		R.EndRecording();
		R.Update();
		R.StartFullReplay();
		R.Update();
		const int32 R0 = W.Restores, X0 = W.Resets;
		TestTrue(TEXT("the playback start restores keyframe 0 and clears the rider effects"), R0 > 0 && X0 == R0);
		R.SetCommand(SSXReplay::CmdToEnd);
		R.Update();
		TestTrue(TEXT("a seek: restore + 0x1128B8"), W.Restores == R0 + 1 && W.Resets == X0 + 1);
		R.StopPlayback();
		TestTrue(TEXT("exit (0x191BE0): the end keyframe restored without 0x1128B8"), W.Restores == R0 + 2 && W.Resets == X0 + 1);
	}
	{
		// 0x12EC98
		SSXParticles::FCarveClouds Cl;
		SSXParticles::FRiderFxInput In;
		In.Motion = SSXParticles::MotionGround;
		In.ContactKind = 3;
		In.Surface = 3;
		In.Steer = 0.5;
		In.BoardOrigin = FVector(0, 0, 0);
		uint32 Bits = 0;
		Cl.Frame(In, [&Bits]() { return Bits++; });
		const int32 Spawned = Cl.Get().Num();
		Cl.ClearRing();
		In.BoardOrigin = FVector(10, 0, 0);   // within 70 cm of the last origin
		Cl.Frame(In, [&Bits]() { return Bits++; });
		TestTrue(TEXT("ClearRing: empty, and no spawn within 70 cm of the kept origin"), Spawned == 1 && Cl.Get().Num() == 0);
		Cl.Reset();
		Cl.Frame(In, [&Bits]() { return Bits++; });
		TestEqual(TEXT("a full reset forgets the origin: spawns at once"), Cl.Get().Num(), 1);
	}
	{
		FSSXCrowdSystem Cr;
		int32 Handle = 0, Stops = 0;
		bool bNoFade = true;
		Cr.Start = [&Handle](const FSSXCrowdStart&) { return Handle++; };
		Cr.Stop = [&Stops, &bNoFade](int32, float Fade) { ++Stops; bNoFade &= Fade == 0.f; };
		Cr.Begin(1);
		Cr.AddEmitter(0, FVector3f(0.f), 100, 3);
		Cr.AddEmitter(0, FVector3f(100.f, 0.f, 0.f), 100, 3);
		Cr.AddEmitter(0, FVector3f(0.f, 100.f, 0.f), 100, 5);
		TestEqual(TEXT("three entries, two voices"), Cr.GetCrowd(0)->Count, 3);
		Cr.RemoveAllEntries();
		TestTrue(TEXT("RemoveAllEntries: no entries, both voices stopped at once, the crowd kept"),
			Cr.GetCrowd(0) && Cr.GetCrowd(0)->Count == 0 && Stops == 2 && bNoFade && Cr.NumCrowds() == 1);
	}
	return true;
}

#endif
