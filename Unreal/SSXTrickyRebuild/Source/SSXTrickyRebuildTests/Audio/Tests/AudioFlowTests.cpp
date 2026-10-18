#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXPathfinder.h"
#include "Audio/SSXRaceAudioFlow.h"
#include "Audio/SSXUISounds.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/RaceRules.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTypes.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXRaceOverlays.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags FlowTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	USSXAudioSubsystem* MakeFlowAudio()
	{
		return NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	}

	TSharedPtr<FJsonObject> LoadFlowReference(FAutomationTestBase& T)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("audio_flow_reference.json"));
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
		{
			SSXTest::Skip(T, TEXT("SimData/audio_flow_reference.json missing (tools/make_audio_flow_reference.py); reference checks skipped"));
			return nullptr;
		}
		return Root;
	}

	struct FSeqRand
	{
		TArray<int32> V;
		int32 I = 0;
		int32 operator()() { return V.IsValidIndex(I) ? V[I++] : 0; }
	};

	TArray<int32> Ints(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<int32> Out;
		for (const TSharedPtr<FJsonValue>& V : A) { Out.Add(int32(V->AsNumber())); }
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioUiSoundsTest, "SSX.Audio.UiSounds", FlowTestFlags)

bool FSSXAudioUiSoundsTest::RunTest(const FString& Parameters)
{
	using namespace SSXUISound;
	struct FRow { int32 Id; int32 Sound; int32 Voice; int32 Class; };
	const FRow Rows[] = {
		{ 0, 16, 1, 4 }, { 2, 48, 0, 4 }, { 6, 48, 0, 4 }, { 10, 48, 0, 4 }, { 3, 49, 0, 4 }, { 7, 49, 0, 4 }, { 11, 49, 0, 4 },
		{ 4, 17, 2, 4 }, { 8, 17, 2, 4 }, { 20, 17, 2, 4 }, { 23, 17, 2, 4 }, { 17, 17, 2, 4 }, { 18, 17, 2, 4 }, { 22, 17, 2, 4 },
		{ 12, 18, 0, 4 }, { 13, 50, 0, 4 }, { 19, 64, 0, 2 }, { 21, 64, 0, 3 }, { 14, -1, 0, 4 }, { 16, -1, 0, 4 }, { 24, -1, 0, 4 },
	};
	for (const FRow& R : Rows)
	{
		const FFeSound S = FeSoundForId(R.Id, 0);
		TestTrue(*FString::Printf(TEXT("id %d -> zbxfe #%d"), R.Id, R.Sound), S.Sound == R.Sound && S.Voice == R.Voice && S.LevelClass == R.Class);
	}
	for (const int32 Id : { 1, 5, 9, 15 })
	{
		TestEqual(*FString::Printf(TEXT("id %d silent below FE music state 4"), Id), FeSoundForId(Id, 3).Sound, -1);
		TestEqual(*FString::Printf(TEXT("id %d = #50 in state >= 4"), Id), FeSoundForId(Id, 4).Sound, 50);
	}
	TestEqual(TEXT("race accept #112 byte 0x5C"), RaceSound(ERace::Accept).Sound * 1000 + RaceSound(ERace::Accept).FrameByte1, 112 * 1000 + 0x5C);
	TestEqual(TEXT("race navigate #112"), RaceSound(ERace::Navigate).Sound, 112);
	TestEqual(TEXT("race back #112 byte 0x18"), RaceSound(ERace::Back).Sound * 1000 + RaceSound(ERace::Back).FrameByte1, 112 * 1000 + 0x18);
	TestEqual(TEXT("race point change #124"), RaceSound(ERace::PointChange).Sound, 124);
	TestTrue(TEXT("race denied #113, stops the shared voice"), RaceSound(ERace::Denied).Sound == 113 && RaceSound(ERace::Denied).bStopShared);

	{
		static bool bTables = SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
		(void)bTables;
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		Profile.Slot[0].Char = 3;   // FUN_00263270
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FE.Init(&Profile, &Cheats, nullptr, false);
		auto Settle = [&FE]() { for (int32 N = 0; N < 3000 && !FE.IsReady(); ++N) { FE.Tick(); } };
		auto Press = [&FE, &Settle](ESSXFeKey K) { Settle(); FE.Key(0, K); FE.Tick(); Settle(); };
		Settle();
		FE.SoundHooks.Reset();
		Press(ESSXFeKey::Down);
		Press(ESSXFeKey::Down);
		Press(ESSXFeKey::Up);
		TestEqual(TEXT("Title: move 4, end bump 7"), FE.SoundHooks, TArray<FString>({ TEXT("ui4"), TEXT("ui7"), TEXT("ui4") }));
		FE.SoundHooks.Reset();
		Press(ESSXFeKey::Cross);
		TestEqual(TEXT("Title X = id 5, ModeSelect posts music 1 from vfunc_20"), FE.SoundHooks,
			TArray<FString>({ TEXT("ui5"), TEXT("whooshA"), TEXT("stinger0"), TEXT("whooshB"), TEXT("speech4"), TEXT("music1"), TEXT("speech3") }));
		FE.SoundHooks.Reset();
		Press(ESSXFeKey::Right);
		Press(ESSXFeKey::Left);
		Press(ESSXFeKey::Up);
		TestEqual(TEXT("ModeSelect move = id 0 (Left/Right only) + vfunc_23 speech 5 (WC) / 4 (SE)"), FE.SoundHooks,
			TArray<FString>({ TEXT("ui0"), TEXT("speech5"), TEXT("ui0"), TEXT("speech4") }));
		FE.SoundHooks.Reset();
		Press(ESSXFeKey::Cross);
		Press(ESSXFeKey::Left);
		Press(ESSXFeKey::Cross);
		Press(ESSXFeKey::Triangle);
		TestEqual(TEXT("CharacterSelect: accept 1, stinger after the slide-out, music 2, move 0 + rider name, refused 3, back 2"), FE.SoundHooks,
			TArray<FString>({ TEXT("ui1"), TEXT("whooshA"), TEXT("stinger1"), TEXT("whooshB"), TEXT("speech16"), TEXT("stopAnimVoice"), TEXT("music2"),
				TEXT("ui0"), TEXT("riderName"), TEXT("ui3"), TEXT("ui2"), TEXT("stopSpeech"), TEXT("stopAnimVoice"), TEXT("whooshA"), TEXT("whooshB"),
				TEXT("speech4"), TEXT("music1"), TEXT("speech3") }));
		FE.SoundHooks.Reset();
		Press(ESSXFeKey::Left);
		Press(ESSXFeKey::Cross);
		TestEqual(TEXT("ModeSelect Practice: speech 6, accept 1, no stinger; EventSel vfunc_15 speech 7 + music 3"), FE.SoundHooks,
			TArray<FString>({ TEXT("ui0"), TEXT("speech6"), TEXT("ui1"), TEXT("whooshA"), TEXT("whooshB"), TEXT("speech7"), TEXT("music3") }));
		FE.SoundHooks.Reset();
		Press(ESSXFeKey::Square);     // FUN_00262608
		Press(ESSXFeKey::Down);
		TestEqual(TEXT("Options: speech 0x24, music 5, move 4"), FE.SoundHooks,
			TArray<FString>({ TEXT("whooshA"), TEXT("whooshB"), TEXT("speech36"), TEXT("music5"), TEXT("ui4") }));
	}

	USSXAudioSubsystem* Audio = MakeFlowAudio();
	Audio->UISoundLog.Reset();
	Audio->PlayUISound(TEXT("ui1"));
	TestEqual(TEXT("#50 silent in FE music state 0"), Audio->UISoundLog.Num(), 0);
	Audio->PlayUISound(TEXT("music5"));
	TestEqual(TEXT("music event remembered (snd+0x3714)"), Audio->GetFeMusicState(), 5);
	Audio->PlayUISound(TEXT("ui1"));
	Audio->PlayUISound(TEXT("ui3"));
	Audio->PlayUISound(TEXT("ui6"));
	Audio->PlayUISound(TEXT("ui0"));
	Audio->PlayUISound(TEXT("ui4"));
	TestEqual(TEXT("FE ids -> zbxfe"), Audio->UISoundLog, TArray<FString>({ TEXT("sfx/zbxfe/50"), TEXT("sfx/zbxfe/49"), TEXT("sfx/zbxfe/48"), TEXT("sfx/zbxfe/16"), TEXT("sfx/zbxfe/17") }));
	Audio->UISoundLog.Reset();
	TestEqual(TEXT("whoosh A"), Audio->PlayFeWhoosh(false), FString(TEXT("sfx/zbxfe/32")));
	TestEqual(TEXT("whoosh B"), Audio->PlayFeWhoosh(true), FString(TEXT("sfx/zbxfe/33")));
	TestEqual(TEXT("stinger (entry 15/25) = zbxfe #1"), Audio->PlayFeStinger(1), FString(TEXT("sfx/zbxfe/01")));
	TestEqual(TEXT("stinger (entry 16) = zbxfe #2"), Audio->PlayFeStinger(2), FString(TEXT("sfx/zbxfe/02")));

	// 0x1AAC08
	{
		using namespace SSXFe;
		FSSXProfile P = FSSXProfile::MakeDefault();
		FSSXCheats C;
		FSSXOverlayManager O;
		O.Init(&P, &C, nullptr);
		O.Pad(0, PadStart);
		O.Pad(0, PadDown);
		O.Pad(0, PadCross);
		O.Pad(0, PadTriangle);
		O.Pad(0, PadUp);
		O.Pad(0, PadCross);
		// 0x1AC410 / 0x215458
		TArray<ERace> Expect = { ERace::PauseOpen, ERace::Navigate, ERace::Accept, ERace::Back, ERace::Navigate, ERace::Accept };
		TestTrue(TEXT("pause sounds: open, navigate, accept, (dialog Triangle) back, navigate, accept"), O.Sounds == Expect);
		Audio->UISoundLog.Reset();
		SSXGameAudio::TickRaceOverlays(*Audio, O);
		TestEqual(TEXT("drained"), O.Sounds.Num(), 0);
		TestEqual(TEXT("#126, then zbxsfx #112"), Audio->UISoundLog, TArray<FString>({ TEXT("sfx/zBxsfx/126"), TEXT("sfx/zBxsfx/112"), TEXT("sfx/zBxsfx/112"), TEXT("sfx/zBxsfx/112"), TEXT("sfx/zBxsfx/112"), TEXT("sfx/zBxsfx/112") }));
	}

	FSSXAudioManifest Manifest;
	if (!Manifest.Load(SSXAudio::ManifestPath()))
	{
		SSXTest::Skip(*this, TEXT("converted audio missing; asset checks skipped"));
		return true;
	}
	TSet<FString> Ids;
	for (int32 Id = 0; Id < 32; ++Id)
	{
		const FFeSound S = FeSoundForId(Id, 5);
		if (S.Sound >= 0) Ids.Add(FeId(S.Sound));
	}
	Ids.Add(FeId(FeWhooshA));
	Ids.Add(FeId(FeWhooshB));
	Ids.Add(FeId(1));
	Ids.Add(FeId(2));
	for (const ERace K : { ERace::Accept, ERace::Navigate, ERace::Back, ERace::PointChange, ERace::Denied }) { Ids.Add(RaceId(RaceSound(K).Sound)); }
	Ids.Add(RaceId(CountdownBeep));
	for (const TPair<FName, FString>& KV : Audio->UISounds) { Ids.Add(KV.Value); }
	int32 Found = 0;
	for (const FString& Id : Ids)
	{
		const bool bOk = Manifest.Find(Id) != nullptr && Audio->LoadSound(Id) != nullptr;
		Found += bOk;
		TestTrue(*FString::Printf(TEXT("%s imported"), *Id), bOk);
	}
	AddInfo(FString::Printf(TEXT("UI sound ids: %d / %d resolve to imported SoundWaves (zbxfe 16/17/18/32/33/48/49/50/64, zBxsfx 78/112/113/124)"), Found, Ids.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioRaceFlowTest, "SSX.Audio.RaceFlow", FlowTestFlags)

bool FSSXAudioRaceFlowTest::RunTest(const FString& Parameters)
{
	using namespace SSXRaceAudio;
	{
		TestTrue(TEXT("session countdown clock -300 + 2 per frame"), SSXRace::CountdownStart == -300.0 && SSXRace::CountdownRate == 2.0);
		FFlow F;
		FSeqRand R;
		F.Load(0, false, false, false, 0.7, [&R]() { return R(); });
		F.Commands.Reset();
		TArray<int32> Beeps;
		int32 GoFrame = -1;
		double Clock = SSXRace::CountdownStart;
		for (int32 Frame = 1; Frame <= 200 && GoFrame < 0; ++Frame)
		{
			Clock += SSXRace::CountdownRate;
			F.Countdown(Clock, [&R]() { return R(); });
			for (const FCommand& C : F.Commands)
			{
				if (C.Kind == FCommand::Beep) { Beeps.Add(Frame); TestEqual(TEXT("beep = zbxsfx #78"), C.Value, 78); }
				if (C.Kind == FCommand::UnpauseSong) { GoFrame = Frame; }
			}
			F.Commands.Reset();
		}
		TestEqual(TEXT("beeps at frames 30, 60, 90, 120"), Beeps, TArray<int32>({ 30, 60, 90, 120 }));
		TestEqual(TEXT("GO (song unpaused) at frame 150"), GoFrame, 150);
		TestTrue(TEXT("GO: state 6, duck off"), F.State == 6 && F.DuckMode == 4);
		AddInfo(FString::Printf(TEXT("countdown: beeps at %d/%d/%d/%d, GO at %d"), Beeps.IsValidIndex(3) ? Beeps[0] : -1, Beeps.IsValidIndex(3) ? Beeps[1] : -1,
			Beeps.IsValidIndex(3) ? Beeps[2] : -1, Beeps.IsValidIndex(3) ? Beeps[3] : -1, GoFrame));
	}

	TSharedPtr<FJsonObject> Ref = LoadFlowReference(*this);
	FSSXInfFile MusicInf, MusicMap;
	const bool bInf = MusicInf.Load(SSXAudio::ConfigPath(TEXT("MUSIC.INF"))) && MusicMap.Load(SSXAudio::ConfigPath(TEXT("MUSICMAP.INF")));
	if (!Ref.IsValid() || !bInf)
	{
		if (!bInf) SSXTest::Skip(*this, TEXT("converted audio configs missing; skipped"));
		return true;
	}
	const TArray<FString> Songs = SongList(MusicInf);
	TArray<FString> RefSongs;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetObjectField(TEXT("songs"))->GetArrayField(TEXT("list"))) { RefSongs.Add(V->AsString()); }
	TestEqual(TEXT("MUSIC.INF song list = race_audio.song_list"), Songs, RefSongs);
	int32 MaskOk = 0, MaskN = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Ref->GetObjectField(TEXT("songs"))->GetObjectField(TEXT("masks"))->Values)
	{
		++MaskN;
		const uint32 M = MusicMapMask(MusicMap, Songs, KV.Key);
		MaskOk += M == uint32(KV.Value->AsNumber());
		TestEqual(*FString::Printf(TEXT("MUSICMAP mask %s"), *KV.Key), int64(M), int64(KV.Value->AsNumber()));
	}
	int32 PickOk = 0, PickN = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("pick")))
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		FSeqRand R{ { int32(P->GetNumberField(TEXT("rand"))) } };
		++PickN;
		PickOk += PickSong(uint32(P->GetNumberField(TEXT("mask"))), Songs.Num(), [&R]() { return R(); }) == int32(P->GetNumberField(TEXT("song")));
	}
	TestEqual(TEXT("song picks = race_audio.pick_race_song"), PickOk, PickN);
	int32 IntroOk = 0, IntroN = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("intro")))
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		FSeqRand R{ Ints(P->GetArrayField(TEXT("rand"))) };
		++IntroN;
		IntroOk += InitialIntroQueue(int32(P->GetNumberField(TEXT("track"))), P->GetBoolField(TEXT("restart")), [&R]() { return R(); }) == Ints(P->GetArrayField(TEXT("queue")));
	}
	TestEqual(TEXT("intro queues = race_audio.initial_intro_queue"), IntroOk, IntroN);
	int32 IntOk = 0, IntN = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("intensity")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		const int32 Want = A[4]->IsNull() ? -1 : int32(A[4]->AsNumber());
		++IntN;
		IntOk += Intensity(int32(A[0]->AsNumber()), int32(A[1]->AsNumber()), A[2]->AsNumber(), int32(A[3]->AsNumber())) == Want;
	}
	TestEqual(TEXT("intensity = race_audio.intensity"), IntOk, IntN);
	int32 ZoneOk = 0, ZoneN = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("zones")))
	{
		const TSharedPtr<FJsonObject> Z = V->AsObject();
		FMusicZones M;
		for (const TSharedPtr<FJsonValue>& Op : Z->GetArrayField(TEXT("script")))
		{
			const FString Name = Op->AsArray()[0]->AsString();
			const int32 Arg = int32(Op->AsArray()[1]->AsNumber());
			if (Name == TEXT("enter")) M.Enter(Arg);
			else if (Name == TEXT("leave")) M.Leave(Arg);
			else if (Name == TEXT("tick")) M.Tick(Arg);
			else M.Reset();
		}
		++ZoneN;
		ZoneOk += M.Posted == Ints(Z->GetArrayField(TEXT("posted")));
	}
	TestEqual(TEXT("breakdown zones = race_audio.MusicZones"), ZoneOk, ZoneN);
	int32 TrigOk = 0, TrigN = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("triggers")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		++TrigN;
		TrigOk += TriggerEvent(int32(A[0]->AsNumber()), int32(A[1]->AsNumber())) == (A[2]->IsNull() ? -1 : int32(A[2]->AsNumber()));
	}
	TestEqual(TEXT("trigger events = race_audio.trigger_event"), TrigOk, TrigN);

	{
		const TSharedPtr<FJsonObject> T = Ref->GetObjectField(TEXT("timeline"));
		FSeqRand R{ Ints(T->GetArrayField(TEXT("rand"))) };
		auto Rand = [&R]() { return R(); };
		const int32 Drain = int32(T->GetNumberField(TEXT("drain_every"))), IntroEndAt = int32(T->GetNumberField(TEXT("intro_end"))), GoAt = int32(T->GetNumberField(TEXT("go")));
		FFlow F;
		TArray<int32> Queue;
		auto Apply = [&]()
		{
			for (const FCommand& C : F.Commands)
			{
				if (C.Kind == FCommand::QueueIntro) Queue.Add(C.Value);
				if (C.Kind == FCommand::ClearIntro || C.Kind == FCommand::StopIntro) Queue.Reset();
			}
			F.Commands.Reset();
		};
		F.Load(0, false, false, false, 0.70, Rand);
		Apply();
		int32 Mismatch = 0, Rows = 0;
		for (const TSharedPtr<FJsonValue>& V : T->GetArrayField(TEXT("rows")))
		{
			const TSharedPtr<FJsonObject> Row = V->AsObject();
			const int32 Frame = int32(Row->GetNumberField(TEXT("frame")));
			if (Frame > 0)
			{
				if (Frame % Drain == 0 && Queue.Num()) Queue.RemoveAt(0);
				if (Frame == IntroEndAt) { F.IntroEnd(); Apply(); }
				if (Frame == GoAt) { F.Go(Rand); Apply(); }
				F.Frame(Queue.Num(), int32(T->GetNumberField(TEXT("racers"))), int32(T->GetNumberField(TEXT("rank"))), T->GetNumberField(TEXT("boost")), Rand);
				Apply();
			}
			++Rows;
			bool bOk = F.State == int32(Row->GetNumberField(TEXT("state"))) && Queue == Ints(Row->GetArrayField(TEXT("queue")));
			if (Frame > 0)
			{
				bOk &= F.DuckMode == int32(Row->GetNumberField(TEXT("duck_mode"))) && FMath::Abs(F.DuckLevel - Row->GetNumberField(TEXT("duck"))) < 1e-9
					&& F.Control == int32(Row->GetNumberField(TEXT("control")));
			}
			if (!bOk && Mismatch++ < 3) AddError(FString::Printf(TEXT("timeline frame %d: state %d queue %d"), Frame, F.State, Queue.Num()));
		}
		TestEqual(TEXT("timeline = race_audio.RaceAudio frame by frame"), Mismatch, 0);
		AddInfo(FString::Printf(TEXT("RaceFlow vs race_audio.py: %d/%d MUSICMAP masks, %d/%d picks, %d/%d intro queues, %d/%d intensity, %d/%d zone scripts, %d/%d triggers, timeline %d frames with %d mismatches"),
			MaskOk, MaskN, PickOk, PickN, IntroOk, IntroN, IntOk, IntN, ZoneOk, ZoneN, TrigOk, TrigN, Rows, Mismatch));
	}

	USSXAudioSubsystem* Audio = MakeFlowAudio();
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("ram")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		TArray<FString> RamSongs;
		for (const TSharedPtr<FJsonValue>& N : S->GetArrayField(TEXT("songs"))) { RamSongs.Add(N->AsString()); }
		TestEqual(TEXT("RAM song list = MUSIC.INF order"), RamSongs, Songs);
		const uint32 Mask = MusicMapMask(MusicMap, Songs, TEXT("gari"));
		TestEqual(TEXT("RAM mask (Garibaldi)"), int64(Mask), int64(S->GetNumberField(TEXT("mask"))));
		TestTrue(TEXT("RAM song is an allowed song"), ((Mask >> int32(S->GetNumberField(TEXT("song")))) & 1u) != 0);
		AddInfo(FString::Printf(TEXT("RAM %s: mask 0x%X (port 0x%X), song %d, duck %.3f, state %d"), *S->GetStringField(TEXT("session")),
			int32(S->GetNumberField(TEXT("mask"))), Mask, int32(S->GetNumberField(TEXT("song"))), S->GetNumberField(TEXT("duck")), int32(S->GetNumberField(TEXT("state")))));
	}

	const bool bSong = Audio->BeginRace(TEXT("gari"), 0, 2);
	TestTrue(TEXT("race song prepared"), bSong && Audio->GetPathfinderPlayer().IsValid());
	if (Audio->GetPathfinderPlayer().IsValid())
	{
		TestTrue(TEXT("song paused at control 80"), Audio->GetPathfinderPlayer()->IsPaused() && Audio->GetPathfinderPlayer()->GetControl() == 80);
	}
	const uint32 GariMask = MusicMapMask(MusicMap, Songs, TEXT("gari"));
	int32 SongIdx = INDEX_NONE;
	for (int32 I = 0; I < Songs.Num(); ++I)
	{
		const FSSXInfFile::FSection* Sec = MusicInf.Find(Songs[I]);
		if (Sec && FPaths::GetBaseFilename(Sec->Get(TEXT("PATHDATA"))).ToLower() == Audio->GetCurrentSongName()) { SongIdx = I; }
	}
	TestTrue(TEXT("song from the [GARI] mask"), SongIdx >= 0 && ((GariMask >> SongIdx) & 1u));
	TestEqual(TEXT("intro big = INTROMUS [GARI] gari.big"), Audio->GetIntroBig(), FString(TEXT("GARI")));
	const TArray<FString>& Intro = Audio->GetIntroLog();
	TestTrue(TEXT("4 intro segments queued, A1 first, one of A2..A4, two B"), Intro.Num() == 4 && Intro[0].EndsWith(TEXT("-A1"))
		&& Intro[1].Contains(TEXT("-A")) && Intro[2].Contains(TEXT("-B")) && Intro[3].Contains(TEXT("-B")));
	for (const FString& Id : Intro) { TestNotNull(*FString::Printf(TEXT("%s imported"), *Id), Audio->LoadSound(Id)); }
	TestEqual(TEXT("audio state 2"), Audio->GetRaceFlow().State, 2);
	Audio->GetRaceFlow().Frame(4, 6, 1, 0.0, [Audio]() { return Audio->Rand15(); });
	TestTrue(TEXT("state 2 ducks to AUDIO.INF INTRODUCK 70 % (RAM 0.70)"), Audio->GetRaceFlow().DuckMode == 0 && FMath::IsNearlyEqual(Audio->GetRaceFlow().DuckLevel, 0.70, 1e-6));
	Audio->UISoundLog.Reset();
	double Clock = SSXRace::CountdownStart;
	for (int32 Frame = 1; Frame <= 150; ++Frame)
	{
		Clock += SSXRace::CountdownRate;
		Audio->GetRaceFlow().Countdown(Clock, [Audio]() { return Audio->Rand15(); });
		Audio->ExecuteRaceCommands();
	}
	TestEqual(TEXT("4 countdown beeps (zBxsfx #78)"), Audio->UISoundLog, TArray<FString>({ TEXT("sfx/zBxsfx/78"), TEXT("sfx/zBxsfx/78"), TEXT("sfx/zBxsfx/78"), TEXT("sfx/zBxsfx/78") }));
	if (Audio->GetPathfinderPlayer().IsValid())
	{
		TestFalse(TEXT("GO unpauses the song"), Audio->GetPathfinderPlayer()->IsPaused());
	}
	TestEqual(TEXT("intro stopped at GO"), Audio->GetIntroQueueLength(), 0);
	Audio->BeginRace(TEXT("elysium"), 2, 2);
	Audio->GetRaceFlow().Frame(4, 6, 1, 0.0, [Audio]() { return Audio->Rand15(); });
	TestTrue(TEXT("Elysium duck 45 %"), FMath::IsNearlyEqual(Audio->GetRaceFlow().DuckLevel, 0.45, 1e-6));
	Audio->GetRaceFlow().Commands.Reset();
	Audio->GetRaceFlow().Trigger(10, true, 0);
	Audio->GetRaceFlow().Trigger(18, true, 0);
	Audio->GetRaceFlow().Trigger(2, true, 1);
	int32 N10 = 0, N1 = 0;
	for (const FCommand& C : Audio->GetRaceFlow().Commands) { N10 += C.Kind == FCommand::PostEvent && C.Value == 10; N1 += C.Kind == FCommand::PostEvent && C.Value == 1; }
	TestTrue(TEXT("event 10 once (snd+0x3700 gate), zone 1 enter -> event 1"), N10 == 1 && N1 == 1);
	return true;
}

// 0x20D9F0 / 0x212018 / 0x211FB8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioDuckTest, "SSX.Audio.Duck", FlowTestFlags)
bool FSSXAudioDuckTest::RunTest(const FString&)
{
	using namespace SSXAudioDuck;
	TestTrue(TEXT("selectors: m 0 / 1 -> 3, 2 -> 1, 3 -> 4, 4 -> 5"), Selector(0) == 3 && Selector(1) == 3 && Selector(2) == 1 && Selector(3) == 4
		&& Selector(4) == 5);
	TestTrue(TEXT("selector 3 (announcer speaks): music, CHAR, SFX, jukebox, UBER - not PA"), Scales(3, EBus::Music) && Scales(3, EBus::CharSpeech)
		&& Scales(3, EBus::Sfx) && Scales(3, EBus::Jukebox) && Scales(3, EBus::Uber) && !Scales(3, EBus::PaSpeech));
	TestTrue(TEXT("selector 4: all but CHAR"), !Scales(4, EBus::CharSpeech) && Scales(4, EBus::PaSpeech) && Scales(4, EBus::Music));
	TestTrue(TEXT("selector 1: not music / UBER"), !Scales(1, EBus::Music) && !Scales(1, EBus::Uber) && Scales(1, EBus::Sfx));
	TestTrue(TEXT("selector 5: nothing"), !Scales(5, EBus::Music) && !Scales(5, EBus::Sfx));
	FFade F;
	for (int32 I = 0; I < 30; ++I) { F.Tick(0, true, 0.65, 1.f / 60.f); }
	TestTrue(TEXT("half way after 30 frames"), FMath::IsNearlyEqual(F.For(EBus::Music), 0.825f, 1e-4f) && F.For(EBus::CharSpeech) == F.Gain
		&& F.For(EBus::PaSpeech) == 1.f);
	for (int32 I = 0; I < 30; ++I) { F.Tick(0, true, 0.65, 1.f / 60.f); }
	TestTrue(TEXT("0.65 after 60 frames"), FMath::IsNearlyEqual(F.Gain, 0.65f, 1e-4f));
	F.Tick(0, false, 0.65, 1.f / 60.f);
	for (int32 I = 0; I < 58; ++I) { F.Tick(0, false, 0.65, 1.f / 60.f); }
	TestTrue(TEXT("not yet at 1 after 59 frames"), F.Gain < 1.f);
	F.Tick(0, false, 0.65, 1.f / 60.f);
	TestTrue(TEXT("back at 1 after 60 frames"), FMath::IsNearlyEqual(F.Gain, 1.f, 1e-4f));
	FFade G;
	for (int32 I = 0; I < 60; ++I) { G.Tick(0, true, 0.7, 1.f / 60.f); }
	G.Tick(4, false, 0.7, 1.f / 60.f);
	TestTrue(TEXT("mode 4: selector 3 kept, music still ducked"), G.Sel == 3 && G.For(EBus::Music) < 1.f);
	return true;
}

#endif
