// FUN_002BF490 / FUN_002BF508 / FUN_002BF290
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXAudioDiag.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXPathfinder.h"
#include "Audio/SSXRaceAudioFlow.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Race/GameRng.h"
#include "Misc/Paths.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags MusicStopFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	// One 60 Hz frame of song audio, as the audio thread pulls it.
	void Pump(FSSXPathfinderPlayer& P, TArray<int16>& Buf)
	{
		const FSSXPathfinderSong& S = P.GetSong();
		const int32 Frames = FMath::Max(S.Rate / 60, 1);
		Buf.SetNumUninitialized(Frames * FMath::Max(S.Channels, 1));
		P.Generate(Buf.GetData(), Frames);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioSongEventsTest, "SSX.Audio.SongEvents", MusicStopFlags)
bool FSSXAudioSongEventsTest::RunTest(const FString&)
{
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("music/pathfinder")), TEXT("*.json"), true, false);
	int32 RaceSongs = 0;
	TArray<FString> Stops, NoAction, RunsOut, NoEnd;
	bool bLoopField = true;
	for (const FString& F : Files)
	{
		TSharedPtr<FSSXPathfinderSong> Song = MakeShared<FSSXPathfinderSong>();
		if (!Song->LoadJson(F) || Song->Events.Num() != 11)
		{
			continue;
		}
		++RaceSongs;
		bLoopField &= Song->bHasLoopField;
		for (int32 E = 0; E < Song->Events.Num(); ++E)
		{
			if (!Song->Events[E].Acts()) NoAction.Add(FString::Printf(TEXT("%s #%d"), *Song->Name, E));
		}
		TSharedPtr<FSSXPcm> Pcm = MakeShared<FSSXPcm>();
		Pcm->Rate = Song->Rate;
		Pcm->Channels = Song->Channels;
		FSSXPathfinderPlayer P(Song, Pcm);
		TArray<int16> Buf;
		P.SetControl(SSXRaceAudio::SongStartControl);
		P.PostEvent(0);
		for (int32 K = 0; K < 120; ++K) Pump(P, Buf);
		for (int32 E = 1; E <= 8; ++E)
		{
			P.PostEvent(E);
			bool bStopped = false;
			for (int32 K = 0; K < 360 && !bStopped; ++K)
			{
				Pump(P, Buf);
				bStopped = !P.IsPlaying();
			}
			if (bStopped)
			{
				Stops.Add(FString::Printf(TEXT("%s event %d"), *Song->Name, E));
				P.PostEvent(0);
				Pump(P, Buf);
			}
		}
		// FUN_002BF290
		FSSXPathfinderPlayer Free(Song, Pcm);
		Free.SetControl(SSXRaceAudio::SongStartControl);
		Free.PostEvent(0);
		int32 Frames = 0;
		do
		{
			if (Frames % 600 == 0) Free.SetControl((Frames / 600) * 37 % 100);
			Pump(Free, Buf);
			++Frames;
		} while ((Free.IsPlaying() || Frames < 2) && Frames < 60 * 60 * 20);
		if (!Free.IsPlaying())
		{
			RunsOut.Add(FString::Printf(TEXT("%s after %.0f s"), *Song->Name, Frames / 60.0));
		}
		Free.PostEvent(10);
		int32 Tail = 0;
		while (Free.IsPlaying() && Tail < 60 * 60) { Pump(Free, Buf); ++Tail; }
		if (Free.IsPlaying() || !Free.ReachedEnd())
		{
			NoEnd.Add(Song->Name);
		}
		AddInfo(FString::Printf(TEXT("%s: event 10 -> end node after %.1f s"), *Song->Name, Tail / 60.0));
	}
	if (RaceSongs == 0)
	{
		SSXTest::Skip(*this, TEXT("no converted race songs (tools/convert_audio.py): skipped"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d race songs; no-action events: %s"), RaceSongs, *FString::Join(NoAction, TEXT(", "))));
	TestEqual(TEXT("race events 1..8 never stop a playing song (flags & 3 == 0 = no action, FUN_002BF508)"), FString::Join(Stops, TEXT(", ")), FString());
	TestEqual(TEXT("no song runs out by itself (the long ending loops back, FUN_002BF290)"), FString::Join(RunsOut, TEXT(", ")), FString());
	TestEqual(TEXT("event 10 (short ending) ends every song at its end node"), FString::Join(NoEnd, TEXT(", ")), FString());
	TestTrue(TEXT("songs converted with the node loop byte (tools/audio_mpf.py 'loop')"), bLoopField);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioMusicRaceSnowTest, "SSX.Audio.MusicRaceSnow", MusicStopFlags)
bool FSSXAudioMusicRaceSnowTest::RunTest(const FString&)
{
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("snow")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("snow race data / patches: %s: skipped"), *Err));
		return true;
	}
	USSXAudioSubsystem* Audio = NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	FSSXInfFile MusicInf;
	MusicInf.Load(SSXAudio::ConfigPath(TEXT("MUSIC.INF")));
	const TArray<FString> Songs = SSXRaceAudio::SongList(MusicInf);
	int32 Seed = -1;
	// 0x228830
	Audio->SeedRaceRandom(0);
	Audio->LoadSpeech(FSSXSpeechSystem::ESet::Race);
	const uint32 DrawsBeforePick = FGameRng::StreamA().S[5] - FGameRng(0u).S[5];
	for (int32 S = 1; S < 2000 && Seed < 0; ++S)
	{
		Audio->SeedRaceRandom(S);
		for (uint32 K = 0; K < DrawsBeforePick; ++K) { FGameRng::StreamA().Next(); }
		const int32 I = Audio->PickTrackSong(TEXT("snow"));
		const FSSXInfFile::FSection* Sec = Songs.IsValidIndex(I) ? MusicInf.Find(Songs[I]) : nullptr;
		if (Sec && FPaths::GetBaseFilename(Sec->Get(TEXT("PATHDATA"))).Equals(TEXT("shakemomma"), ESearchCase::IgnoreCase)) Seed = S;
	}
	if (Seed < 0)
	{
		SSXTest::Skip(*this, TEXT("MUSIC.INF / MUSICMAP.INF not available or shakemomma not in Snowdream's pool: skipped"));
		return true;
	}
	SSXAudioDiag::Reset();
	Audio->SeedRaceRandom(Seed);
	SSXGameAudio::OnRaceSetup(*Audio, TEXT("snow"), Data.TrackId, int32(ERaceEventMode::CircuitRace));
	TSharedPtr<FSSXPathfinderPlayer> P = Audio->GetPathfinderPlayer();
	if (!P.IsValid() || Audio->GetRaceSongStem() != TEXT("shakemomma"))
	{
		AddError(FString::Printf(TEXT("race song not prepared (stem '%s')"), *Audio->GetRaceSongStem()));
		return false;
	}
	TestTrue(TEXT("race load: the song is prepared and paused"), P->IsPaused());

	TMap<int32, int32> TypeCount;
	for (const FRacePath& Path : Data.Race.LinePaths)
	{
		for (const FRacePathEvent& E : Path.Events) { if (E.Type >= 1 && E.Type <= 18) ++TypeCount.FindOrAdd(E.Type); }
	}
	TArray<FString> Types;
	for (const TPair<int32, int32>& T : TypeCount) Types.Add(FString::Printf(TEXT("%d x%d"), T.Key, T.Value));
	Types.Sort();
	AddInfo(FString::Printf(TEXT("snow music trigger types: %s"), *FString::Join(Types, TEXT(", "))));

	const int32 Riders[6] = { 10, 0, 8, 4, 11, 6 };
	constexpr int32 Human = 3;
	TArray<TUniquePtr<FRiderSim>> Sims;
	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::CircuitRace;
	TestTrue(TEXT("session"), Session.Init(Data, Cfg));
	for (int32 I = 0; I < 6; ++I)
	{
		TUniquePtr<FRiderSim> S = MakeUnique<FRiderSim>();
		SSXRiderSetup::RegisterStates(*S, true);
		S->SetWorld(&World);
		S->Profile.Character = Riders[I];
		Session.AddRacer(S.Get(), I == Human);
		Sims.Add(MoveTemp(S));
	}
	USSXBoardSoundComponent* Board = NewObject<USSXBoardSoundComponent>();
	Board->bIsAI = false;
	Session.IsSkipPressed = []() { return true; };
	Session.Start();

	TArray<int16> Buf;
	int32 GoFrame = -1, RaceEnd = -1, Silent = 0, PausedOutside = 0, FirstBad = -1, Ended = -1, MinByte = 127, MusicEvents = 0;
	bool bZoneInjected = false, bTricky = false, bPauseOk = true;
	int32 PauseAt = -1;
	TArray<FString> TriggerLog;
	constexpr int32 PreRaceLimit = 60 * 120, RaceFrames = 60 * 200;
	for (int32 F = 0; F < PreRaceLimit + RaceFrames; ++F)
	{
		const ERacePhase Prev = Session.GetPhase();
		Session.Tick();
		SSXGameAudio::RaceFrame(*Audio, Session, Human, Prev);
		for (const TUniquePtr<FRiderSim>& S : Sims) S->Step(FRiderInputWord());
		for (const FRaceCue& C : Session.GetCues())
		{
			if (C.Racer == Human && (C.Kind == ERaceCueKind::TriggerEnter || C.Kind == ERaceCueKind::TriggerLeave) && TriggerLog.Num() < 24)
			{
				TriggerLog.Add(FString::Printf(TEXT("%s%d@%d"), C.Kind == ERaceCueKind::TriggerEnter ? TEXT("+") : TEXT("-"), C.Type, F - FMath::Max(GoFrame, 0)));
			}
			MusicEvents += SSXGameAudio::HandleRaceCue(*Audio, C, C.Racer == Human) ? 1 : 0;
			SSXGameAudio::HandleSpeechCue(*Audio, Session, C);
		}
		Session.ClearCues();
		Board->UpdateFromRider(Sims[Human]->State, 1.f / 60.f);
		Audio->SetBigAirMusicByte(Board->LastMusicByte);
		MinByte = FMath::Min(MinByte, Board->LastMusicByte);
		const ERacePhase Phase = Session.GetPhase();
		if (GoFrame < 0 && Phase == ERacePhase::Race) GoFrame = F;
		if (GoFrame >= 0)
		{
			const int32 T = F - GoFrame;
			if (T == 60 * 10 && !bZoneInjected)
			{
				bZoneInjected = true;
				for (const TPair<ERaceCueKind, int32>& K : { TPair<ERaceCueKind, int32>(ERaceCueKind::TriggerEnter, 6), TPair<ERaceCueKind, int32>(ERaceCueKind::TriggerLeave, 6) })
				{
					FRaceCue C; C.Kind = K.Key; C.Racer = Human; C.Type = K.Value; C.Extra = 1; C.Frame = F;
					MusicEvents += SSXGameAudio::HandleRaceCue(*Audio, C, true) ? 1 : 0;
				}
			}
			if (T == 60 * 18)
			{
				for (const TPair<ERaceCueKind, int32>& K : { TPair<ERaceCueKind, int32>(ERaceCueKind::TriggerEnter, 7), TPair<ERaceCueKind, int32>(ERaceCueKind::TriggerLeave, 7) })
				{
					FRaceCue C; C.Kind = K.Key; C.Racer = Human; C.Type = K.Value; C.Extra = 1; C.Frame = F;
					MusicEvents += SSXGameAudio::HandleRaceCue(*Audio, C, true) ? 1 : 0;
				}
			}
			if (T == 60 * 25 && !bTricky)
			{
				bTricky = true;   // 0x11B018
				Sims[Human]->State.BoostMeter = 0.95;
				SSXAir::BoostAdd(Sims[Human]->State, 0.2, 1.0);
			}
			if (T == 60 * 40) { PauseAt = F; Audio->PauseGameAudio(); }   // 0x2153F8
			if (T == 60 * 43) { Audio->ResumeGameAudio(); }               // 0x215488
		}
		if (Phase == ERacePhase::EndRace && RaceEnd < 0) RaceEnd = F;
		Pump(*P, Buf);
		if (GoFrame >= 0 && RaceEnd < 0 && F > GoFrame + 2)
		{
			const bool bInPause = PauseAt >= 0 && F >= PauseAt && F < PauseAt + 180;
			if (bInPause)
			{
				bPauseOk &= P->IsPaused();
			}
			else if (P->IsPaused())
			{
				++PausedOutside;
			}
			const bool bEvent10 = !Audio->GetRaceFlow().bEvent10Armed;
			if (!P->IsPlaying() && P->ReachedEnd() && Ended < 0)
			{
				Ended = F - GoFrame;
			}
			const bool bBad = (!P->IsPlaying() && !(bEvent10 && P->ReachedEnd()))
				|| (!bInPause && Audio->GetMusicGain() <= 0.f && Board->LastMusicByte == 127);
			if (bBad)
			{
				++Silent;
				if (FirstBad < 0) FirstBad = F - GoFrame;
			}
		}
		if (RaceEnd >= 0 && F > RaceEnd + 60) break;
		if (GoFrame < 0 && F >= PreRaceLimit) break;
	}
	TArray<FString> SongLog;
	for (const SSXAudioDiag::FRecord& R : SSXAudioDiag::Records())
	{
		if (R.System == TEXT("song") && !R.Id.StartsWith(TEXT("control")) && SongLog.Num() < 40) SongLog.Add(R.Id);
	}
	AddInfo(FString::Printf(TEXT("GO at frame %d, race end %d, music events %d, min big-air byte %d, song end node after %d frames"),
		GoFrame, RaceEnd, MusicEvents, MinByte, Ended));
	AddInfo(FString::Printf(TEXT("song log: %s"), *FString::Join(SongLog, TEXT(" | "))));
	AddInfo(FString::Printf(TEXT("human trigger cues: %s"), *FString::Join(TriggerLog, TEXT(" "))));
	TestTrue(TEXT("the race reached GO"), GoFrame >= 0);
	TestTrue(TEXT("zone 3 and the Tricky activation were exercised"), bZoneInjected && bTricky);
	TestEqual(TEXT("from GO the song plays (stopped only by the short ending after event 10, never at zero gain)"), Silent, 0);
	if (FirstBad >= 0) AddError(FString::Printf(TEXT("first silent frame %d after GO"), FirstBad));
	TestTrue(TEXT("the pause menu pauses the song"), bPauseOk && PauseAt >= 0);
	TestEqual(TEXT("the song is never left paused outside the pause"), PausedOutside, 0);
	TestFalse(TEXT("snd+0x3830 cleared after the resume"), Audio->IsMoviePaused());
	int32 SongRecords = 0, Event9 = 0;
	for (const SSXAudioDiag::FRecord& R : SSXAudioDiag::Records())
	{
		SongRecords += R.System == TEXT("song") ? 1 : 0;
		Event9 += R.System == TEXT("song") && R.Id.StartsWith(TEXT("event 9 ")) ? 1 : 0;
	}
	TestTrue(TEXT("event 9 posted once per entry of its line, not every frame inside it"), Event9 <= 2);
	TestTrue(TEXT("ssx.Audio.Log song records (start, unpause, event, pause, resume)"), SongRecords >= 5);
	return true;
}

// FUN_002C15C8 / FUN_002C0488 / FUN_002BF508
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioPathfinderQueueTest, "SSX.Audio.PathfinderQueue", MusicStopFlags)
bool FSSXAudioPathfinderQueueTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FSSXPathfinderSong> Song = MakeShared<FSSXPathfinderSong>();
	Song->Name = TEXT("synthetic");
	Song->Rate = 600;
	Song->Channels = 1;
	constexpr int32 SegFrames = 100, Step = 10;
	for (int32 N = 0; N < 4; ++N)
	{
		FSSXPathfinderNode Nd;
		Nd.Segment = N;
		Nd.Branches.Add({ 0, 127, N });
		Song->Nodes.Add(Nd);
		Song->Segments.Add({ int64(N) * SegFrames, SegFrames });
	}
	auto Ev = [&](int32 Op, int32 Node, bool bImm) { FSSXPathfinderEvent E; E.Op = Op; E.Node = Node; E.bImmediate = bImm; Song->Events.Add(E); };
	Ev(1, 0, false);
	Ev(1, 1, false);
	Ev(0, -1, false);
	Ev(1, 2, true);
	Ev(1, 3, false);
	TSharedPtr<FSSXPcm> Pcm = MakeShared<FSSXPcm>();
	Pcm->Rate = Song->Rate;
	Pcm->Channels = 1;
	Pcm->Samples.SetNumZeroed(4 * SegFrames);
	TArray<int16> Buf;
	Buf.SetNumZeroed(Step);
	FSSXPathfinderPlayer P(Song, Pcm);
	auto Tick = [&](int32 N) { for (int32 K = 0; K < N; ++K) { P.Generate(Buf.GetData(), Step); } };

	P.PostEvent(0, 1);
	Tick(1);
	TestEqual(TEXT("nothing playing: the head acts at once"), P.GetCurrentNode(), 0);
	Tick(9);
	P.PostEvent(1);
	P.PostEvent(4);
	Tick(5);
	TestEqual(TEXT("pending: still node 0"), P.GetCurrentNode(), 0);
	Tick(5);
	TestEqual(TEXT("first boundary: event 1 -> node 1"), P.GetCurrentNode(), 1);
	TestEqual(TEXT("event 4 still queued"), P.GetQueuedEvents(), 1);
	Tick(10);
	TestEqual(TEXT("next boundary: event 4 -> node 3"), P.GetCurrentNode(), 3);
	P.PostEvent(2);
	P.PostEvent(1);
	Tick(10);
	TestEqual(TEXT("no action consumed at the boundary: node 3 plays on"), P.GetCurrentNode(), 3);
	Tick(10);
	TestEqual(TEXT("then event 1 -> node 1"), P.GetCurrentNode(), 1);
	P.PostEvent(4);
	P.PostEvent(3);
	Tick(2);
	TestEqual(TEXT("immediate behind a pending head: not yet"), P.GetCurrentNode(), 1);
	Tick(8);
	TestEqual(TEXT("head at the boundary: node 3"), P.GetCurrentNode(), 3);
	Tick(1);
	TestEqual(TEXT("then the immediate one at once: node 2"), P.GetCurrentNode(), 2);
	P.PostEvent(1);
	P.PostEvent(4, 1);
	Tick(10);
	TestEqual(TEXT("priority 1 first: node 3"), P.GetCurrentNode(), 3);
	Tick(10);
	TestEqual(TEXT("then priority 0: node 1"), P.GetCurrentNode(), 1);
	for (int32 K = 0; K < 17; ++K) { P.PostEvent(2); }
	P.PostEvent(99);
	Tick(1);
	TestEqual(TEXT("queue holds 16"), P.GetQueuedEvents(), 16);
	return !HasAnyErrors();
}

#endif
