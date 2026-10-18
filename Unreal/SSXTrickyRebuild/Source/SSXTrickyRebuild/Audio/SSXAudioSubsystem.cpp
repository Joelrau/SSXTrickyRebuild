#include "Audio/SSXAudioSubsystem.h"

#include "Audio/SSXCrowdVoices.h"
#include "Audio/SSXIntroMusic.h"
#include "Audio/SSXPathfinder.h"
#include "Audio/SSXWaveKeepAlive.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "SSXTrickyRebuild.h"
#include "Sim/RiderState.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

namespace
{
	float Level(const FSSXInfFile& Inf, const TCHAR* Section, const TCHAR* Key, float Default)
	{
		const FSSXInfFile::FSection* S = Inf.Find(Section);
		const FString V = S ? S->Get(Key) : FString();
		return V.IsEmpty() ? Default : FCString::Atof(*V) / 100.f;
	}
}

void USSXAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadConfigs();
	using namespace SSXUISound;
	UISounds.Add(TEXT("raceAccept"), RaceId(RaceSound(ERace::Accept).Sound));
	UISounds.Add(TEXT("raceNavigate"), RaceId(RaceSound(ERace::Navigate).Sound));
	UISounds.Add(TEXT("raceBack"), RaceId(RaceSound(ERace::Back).Sound));
	UISounds.Add(TEXT("racePointChange"), RaceId(RaceSound(ERace::PointChange).Sound));
	UISounds.Add(TEXT("raceDenied"), RaceId(RaceSound(ERace::Denied).Sound));
	UISounds.Add(TEXT("countdownBeep"), RaceId(CountdownBeep));
	UISounds.Add(TEXT("feWhooshA"), FeId(FeWhooshA));
	UISounds.Add(TEXT("feWhooshB"), FeId(FeWhooshB));
	// 0x227B00 / 0x228D10
	Speech.OnSegment = [this](int32 Ch, const SSXSpeech::FSampleGroup& G, const SSXSpeech::FResolvedSample& Smp, bool bQueue)
	{
		PlaySpeechSegment(Ch, G, Smp, bQueue);
	};
	Speech.OnStop = [this](int32 Ch)
	{
		if (Ch >= 0 && Ch < SSXSpeech::NumSpeechChannels && SpeechPlayers[Ch].IsValid()) { SpeechPlayers[Ch]->Stop(); }
	};
}

void USSXAudioSubsystem::Deinitialize()
{
	if (SSXAudioDiag::LogLevel() > 0)
	{
		for (const FString& L : SSXAudioDiag::Summary()) { UE_LOG(LogSSX, Display, TEXT("[audiostats] %s"), *L); }
		UE_LOG(LogSSX, Display, TEXT("[audiostats] peak active sources %d / cap %d"), SSXAudioDiag::PeakActiveSources(), SSXAudioDiag::MaxSources());
	}
	StopMusic(0.f);
	StopStreamVoices();
	Player.Reset();
	CurrentPcm.Reset();
	SongCache.Reset();
	Super::Deinitialize();
}

USSXAudioSubsystem* USSXAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<USSXAudioSubsystem>() : nullptr;
}

UWorld* USSXAudioSubsystem::GetAudioWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

const FSSXAudioManifest& USSXAudioSubsystem::GetManifest()
{
	if (!bManifestTried)
	{
		bManifestTried = true;
		FString Err;
		if (!Manifest.Load(SSXAudio::ManifestPath(), &Err))
		{
			UE_LOG(LogSSX, Warning, TEXT("[audio] %s"), *Err);
		}
	}
	return Manifest;
}

void USSXAudioSubsystem::LoadConfigs()
{
	if (bConfigsLoaded)
	{
		return;
	}
	bConfigsLoaded = true;
	MusicInf.Load(SSXAudio::ConfigPath(TEXT("MUSIC.INF")));
	MusicMapInf.Load(SSXAudio::ConfigPath(TEXT("MUSICMAP.INF")));
	JukeboxInf.Load(SSXAudio::ConfigPath(TEXT("JUKEBOX.INF")));
	AudioInf.Load(SSXAudio::ConfigPath(TEXT("AUDIO.INF")));
	IntroMusInf.Load(SSXAudio::ConfigPath(TEXT("INTROMUS.INF")));
}

FString USSXAudioSubsystem::ResolveSongStem(const FString& SongKey, float* OutPathLevel) const
{
	for (const FSSXInfFile::FSection& S : MusicInf.Sections)
	{
		const FString Path = S.Get(TEXT("PATHDATA"));
		if (Path.IsEmpty())
		{
			continue;
		}
		const FString Stem = FPaths::GetBaseFilename(Path);
		if (S.Name.StartsWith(SongKey, ESearchCase::IgnoreCase) || Stem.Equals(SongKey, ESearchCase::IgnoreCase))
		{
			if (OutPathLevel)
			{
				const FString L = S.Get(TEXT("PATHLEVEL"));
				*OutPathLevel = L.IsEmpty() ? 1.f : FCString::Atof(*L) / 100.f;
			}
			return Stem.ToLower();
		}
	}
	return FString();
}

bool USSXAudioSubsystem::StartPathfinder(const FString& Stem, int32 Event, float Gain, int32 InControl, bool bPaused)
{
	const FSSXAudioManifest& M = GetManifest();
	TSharedPtr<const FSSXPathfinderSong> Song;
	if (const TSharedPtr<const FSSXPathfinderSong>* Cached = SongCache.Find(Stem))
	{
		Song = *Cached;
	}
	else
	{
		const FString* Rel = M.GetPathfinderSongs().Find(Stem);
		const FString JsonPath = FPaths::Combine(SSXAudio::ConvertedDir(),
			Rel ? *Rel : FString::Printf(TEXT("music/pathfinder/%s/%s.json"), *Stem, *Stem));
		TSharedPtr<FSSXPathfinderSong> NewSong = MakeShared<FSSXPathfinderSong>();
		FString Err;
		if (!NewSong->LoadJson(JsonPath, &Err))
		{
			UE_LOG(LogSSX, Warning, TEXT("[audio] pathfinder song '%s': %s"), *Stem, *Err);
			return false;
		}
		Song = NewSong;
		SongCache.Add(Stem, Song);
	}
	if (CurrentPcmStem != Stem || !CurrentPcm.IsValid())
	{
		TSharedPtr<FSSXPcm> Pcm = MakeShared<FSSXPcm>();
		FString Err;
		if (!FSSXPcm::LoadWav(FPaths::Combine(SSXAudio::ConvertedDir(), Song->SegmentsWav), *Pcm, &Err)
			|| Pcm->Rate != Song->Rate || Pcm->Channels != Song->Channels)
		{
			UE_LOG(LogSSX, Warning, TEXT("[audio] pathfinder segments for '%s': %s"), *Stem, Err.IsEmpty() ? TEXT("format mismatch") : *Err);
			return false;
		}
		CurrentPcm = Pcm;
		CurrentPcmStem = Stem;
	}

	StopMusic(0.4f);
	Player = MakeShared<FSSXPathfinderPlayer>(Song, CurrentPcm);
	// TODO: the menu song's control is not recovered;
	Player->SetControl(InControl);
	Player->SetPaused(bPaused);
	Player->PostEvent(Event, 1);   // FUN_00225380 / 0x224470 / FUN_002C15C8
	CurrentSong = Stem;
	CurrentSongGain = Gain;
	Intensity = 0.f;
	bLoggedSongPlaying = false;
	SSXAudioDiag::Song(FString::Printf(TEXT("start %s event %d control %d%s"), *Stem, Event, InControl, bPaused ? TEXT(" paused") : TEXT("")),
		GetMusicGain());

	UWorld* World = GetAudioWorld();
	if (!World)
	{
		return true;
	}
	MusicWave = NewObject<USSXPathfinderWave>(GetTransientPackage());
	MusicWave->Init(Player);
	SSXWaveKeepAlive::Track(MusicWave);
	MusicComponent = UGameplayStatics::CreateSound2D(World, MusicWave, 1.f, 1.f, 0.f, nullptr, true, false);
	if (MusicComponent)
	{
		MakeGameSound(MusicComponent);   // 0x225240 / 0x2243E8
		ApplyMusicVolume();
		MusicComponent->Play();
	}
	SSXAudioDiag::Note(TEXT("music"), Stem, false, GetMusicGain(), 0.0, SSXAudioDiag::SpawnResult(MusicComponent, false));
	UE_LOG(LogSSX, Display, TEXT("[audio] pathfinder '%s' event %d control %d%s (%d nodes, %d segments)"), *Stem, Event, InControl,
		bPaused ? TEXT(" paused") : TEXT(""), Song->Nodes.Num(), Song->Segments.Num());
	return true;
}

bool USSXAudioSubsystem::StartSoundWave(const FString& Id, float Gain, bool bLoop)
{
	USoundWave* Wave = Cast<USoundWave>(LoadSound(Id));
	UWorld* World = GetAudioWorld();
	if (!Wave || !World)
	{
		return false;
	}
	StopMusic(0.4f);
	Player.Reset();
	Wave->bLooping = bLoop;
	MusicComponent = UGameplayStatics::CreateSound2D(World, Wave, 1.f, 1.f, 0.f, nullptr, true, false);
	CurrentSong = Id;
	CurrentSongGain = Gain;
	SSXAudioDiag::Note(TEXT("music"), Id, false, Gain, 0.0, SSXAudioDiag::SpawnResult(MusicComponent, false));
	if (MusicComponent)
	{
		MakeGameSound(MusicComponent);
		ApplyMusicVolume();
		MusicComponent->Play();
	}
	return MusicComponent != nullptr;
}

void USSXAudioSubsystem::PlayMenuMusic(int32 Event)
{
	LoadConfigs();
	float PathLevel = 0.85f;
	// FUN_0020F430
	const FString Stem = ResolveSongStem(Speaker == 3 ? TEXT("QuadMenu") : TEXT("Menu"), &PathLevel);
	const float Gain = Level(AudioInf, TEXT("FELEVELSCALING"), TEXT("MUSICLEVEL"), 0.95f) * PathLevel;
	bMenuMusic = true;
	if (CurrentSong == Stem && Player.IsValid() && Player->IsPlaying())
	{
		Player->PostEvent(Event);
		return;
	}
	if (!StartPathfinder(Stem.IsEmpty() ? TEXT("ssxmenu") : Stem, Event, Gain))
	{
		StartSoundWave(TEXT("music/menu/ssxmenu"), Gain, true);
	}
}

void USSXAudioSubsystem::PostMenuMusicEvent(int32 Event)
{
	FeMusicState = Event;
	if (bMenuMusic && Player.IsValid())
	{
		Player->PostEvent(Event);
	}
}

void USSXAudioSubsystem::UpdateFrontEndMusic(bool bMenuActive)
{
	const UWorld* World = GetAudioWorld();
	if (FrontEndMusicWorld.Get() != World)
	{
		FrontEndMusicWorld = World;
		bFrontEndMusicOn = false;
		FeMusicState = 0;
	}
	if (bMenuActive == bFrontEndMusicOn)
	{
		return;
	}
	bFrontEndMusicOn = bMenuActive;
	if (bMenuActive)
	{
		PlayMenuMusic(StartEvent);
	}
	else if (bMenuMusic)
	{
		StopMusic(0.4f);
	}
}

FString USSXAudioSubsystem::TrackKey(const FSSXInfFile& Inf, const FString& Track) const
{
	for (const FSSXInfFile::FSection& S : Inf.Sections)
	{
		if (Track.Contains(S.Name, ESearchCase::IgnoreCase))
		{
			return Track;
		}
	}
	for (const FSSXInfFile::FSection& S : Inf.Sections)
	{
		if (Track.Len() >= 4 && S.Name.Contains(Track, ESearchCase::IgnoreCase))
		{
			return S.Name;
		}
	}
	return Track;
}

FString USSXAudioSubsystem::SongStemByIndex(int32 Index, float* OutPathLevel) const
{
	const TArray<FString> Songs = SSXRaceAudio::SongList(MusicInf);
	const FSSXInfFile::FSection* S = Songs.IsValidIndex(Index) ? MusicInf.Find(Songs[Index]) : nullptr;
	const FString Path = S ? S->Get(TEXT("PATHDATA")) : FString();
	if (Path.IsEmpty())
	{
		return FString();
	}
	if (OutPathLevel)
	{
		const FString L = S->Get(TEXT("PATHLEVEL"));
		*OutPathLevel = L.IsEmpty() ? 1.f : FCString::Atof(*L) / 100.f;
	}
	return FPaths::GetBaseFilename(Path).ToLower();
}

int32 USSXAudioSubsystem::PickTrackSong(const FString& Track)
{
	LoadConfigs();
	const TArray<FString> Songs = SSXRaceAudio::SongList(MusicInf);
	const uint32 Mask = SSXRaceAudio::MusicMapMask(MusicMapInf, Songs, TrackKey(MusicMapInf, Track));
	RaceSongMask = Mask;
	RaceSongIndex = SSXRaceAudio::PickSong(Mask, Songs.Num(), [this]() { return Rand15(); });
	return RaceSongIndex;
}

FString USSXAudioSubsystem::GetRaceSongTitle() const
{
	const TArray<FString> Songs = SSXRaceAudio::SongList(MusicInf);
	return Songs.IsValidIndex(RaceSongIndex) ? Songs[RaceSongIndex] : FString();
}

void USSXAudioSubsystem::StepRaceSong(int32 Dir)
{
	LoadConfigs();
	const TArray<FString> Songs = SSXRaceAudio::SongList(MusicInf);
	const int32 N = FMath::Min(Songs.Num(), 32);
	if (N <= 0 || (RaceSongMask & ((N >= 32) ? ~0u : ((1u << N) - 1))) == 0)
	{
		return;
	}
	int32 I = RaceSongIndex;
	do
	{
		I = Dir < 0 ? (I - 1 < 0 ? N - 1 : I - 1) : (I + 1 >= N ? 0 : I + 1);
	} while (((RaceSongMask >> I) & 1u) == 0);
	RaceSongIndex = I;
	float PathLevel = 1.f;
	RaceSongStem = SongStemByIndex(I, &PathLevel);
	RaceSongGain = Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("MUSICLEVEL"), 0.93f) * PathLevel;
	// 0x215220 / 0x224E68 / 0x225380
	SSXRaceAudio::FCommand Prepare;
	Prepare.Kind = SSXRaceAudio::FCommand::PrepareSong;
	RaceFlow.Commands.Add(Prepare);
	ExecuteRaceCommands();
	UE_LOG(LogSSX, Log, TEXT("[audio] pause options song %d '%s'"), I, *GetRaceSongTitle());
}

void USSXAudioSubsystem::PauseRaceSong()
{
	if (Player.IsValid())
	{
		Player->SetPaused(true);
	}
	SSXAudioDiag::Song(FString::Printf(TEXT("pause %s (0x225240)"), *CurrentSong), GetMusicGain());
}

bool USSXAudioSubsystem::BeginRace(const FString& Track, int32 TrackId, int32 GameMode, bool bRestart)
{
	LoadConfigs();
	RaceCommandLog.Reset();
	IntroLog.Reset();
	const bool bTutorial = GameMode == 6;
	float PathLevel = 1.f;
	const FString Stem = SongStemByIndex(PickTrackSong(Track), &PathLevel);
	bool bSong = false;
	RaceSongStem = Stem;
	if (!Stem.IsEmpty())
	{
		bMenuMusic = false;
		const float Gain = Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("MUSICLEVEL"), 0.93f) * PathLevel;
		RaceSongGain = Gain;
		bSong = StartPathfinder(Stem, 0, Gain, SSXRaceAudio::SongStartControl, !bTutorial);
	}
	double Duck = Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("INTRODUCK"), 0.75f);
	const FString AudioKey = TrackKey(AudioInf, Track);
	for (const FSSXInfFile::FSection& S : AudioInf.Sections)
	{
		const FString V = S.Get(TEXT("INTRODUCK"));
		if (!V.IsEmpty() && !S.Name.Contains(TEXT("LEVELSCALING")) && AudioKey.Contains(S.Name, ESearchCase::IgnoreCase))
		{
			Duck = FCString::Atof(*V) / 100.0;
		}
	}
	if (!bTutorial && (!bRestart || GameMode == 4 || GameMode == 5))
	{
		StartIntroStream(Track);
	}
	else if (IntroPlayer.IsValid())
	{
		IntroPlayer->Stop();
	}
	RaceFlow.Load(TrackId, bRestart, GameMode == 4 || GameMode == 5, bTutorial, Duck, [this]() { return Rand15(); });
	ExecuteRaceCommands();
	UE_LOG(LogSSX, Display, TEXT("[audio] race load: song '%s'%s, intro '%s', duck %.2f"), *Stem, bTutorial ? TEXT("") : TEXT(" (paused)"), *IntroBig, Duck);
	return bSong;
}

bool USSXAudioSubsystem::BeginLoadedReplayRace(const FString& Track, int32 TrackId)
{
	// 0x2116D8 / 0x215488 / 0x224CF8 / 0x215288
	LoadConfigs();
	RaceCommandLog.Reset();
	IntroLog.Reset();
	ResumeAfterMovie();
	const TArray<FString> Songs = SSXRaceAudio::SongList(MusicInf);
	const uint32 Mask = SSXRaceAudio::MusicMapMask(MusicMapInf, Songs, TrackKey(MusicMapInf, Track));
	float PathLevel = 1.f;
	RaceSongMask = Mask;
	RaceSongIndex = SSXRaceAudio::PickSong(Mask, Songs.Num(), []() { return 0; });
	RaceSongStem = SongStemByIndex(RaceSongIndex, &PathLevel);
	RaceSongGain = Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("MUSICLEVEL"), 0.93f) * PathLevel;
	if (IntroPlayer.IsValid())
	{
		IntroPlayer->Stop();
	}
	IntroBig.Reset();
	RaceFlow.LoadReplay(TrackId);
	UE_LOG(LogSSX, Display, TEXT("[audio] loaded-replay race load: song '%s' (not prepared), no intro, state 6"), *RaceSongStem);
	return !RaceSongStem.IsEmpty();
}

bool USSXAudioSubsystem::StartIntroStream(const FString& Track)
{
	SSXWaveKeepAlive::Retire(IntroComponent);
	IntroComponent = nullptr;
	IntroWave = nullptr;
	IntroPlayer.Reset();
	IntroSegments.Reset();
	IntroBig.Reset();
	const FString Key = TrackKey(IntroMusInf, Track).ToLower();
	FString Big;
	for (const FSSXInfFile::FSection& S : IntroMusInf.Sections)
	{
		const FString Song = S.Get(TEXT("SONG"));
		if (!Song.IsEmpty() && Key.Contains(S.Name.ToLower()))
		{
			Big = Song;
			break;
		}
	}
	if (Big.IsEmpty())
	{
		return false;
	}
	IntroBig = FPaths::GetBaseFilename(Big).ToUpper();
	const FString Prefix = FString::Printf(TEXT("music/intro/%s/"), *IntroBig);
	int32 Rate = 0, Channels = 0;
	for (const TPair<FString, FSSXAudioEntry>& E : GetManifest().GetEntries())
	{
		if (!E.Key.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			continue;
		}
		for (int32 Seg = 0; Seg <= SSXRaceAudio::EndSegment; ++Seg)
		{
			if (E.Key.EndsWith(SSXIntroMusic::SegmentSuffix(Seg), ESearchCase::IgnoreCase))
			{
				IntroSegments.Add(Seg, E.Key);
				Rate = E.Value.Rate;
				Channels = E.Value.Channels;
			}
		}
	}
	if (IntroSegments.Num() == 0 || Rate <= 0 || Channels <= 0)
	{
		return false;
	}
	IntroPlayer = MakeShared<FSSXSegmentQueuePlayer>(Rate, Channels);
	if (UWorld* World = GetAudioWorld())
	{
		IntroWave = NewObject<USSXSegmentQueueWave>(GetTransientPackage());
		IntroWave->Init(IntroPlayer);
		SSXWaveKeepAlive::Track(IntroWave);
		IntroComponent = UGameplayStatics::CreateSound2D(World, IntroWave, 1.f, 1.f, 0.f, nullptr, true, false);
		if (IntroComponent)
		{
			MakeGameSound(IntroComponent);
			IntroBaseVolume = MusicVolume * Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("MUSICLEVEL"), 0.93f);
			ApplyIntroVolume();
			IntroComponent->Play();
		}
	}
	return true;
}

void USSXAudioSubsystem::QueueIntroSegment(int32 Segment)
{
	const FString* Id = IntroSegments.Find(Segment);
	if (!Id || !IntroPlayer.IsValid())
	{
		return;
	}
	IntroLog.Add(*Id);
	const FSSXAudioEntry* E = GetManifest().Find(*Id);
	TSharedPtr<FSSXPcm> Pcm = MakeShared<FSSXPcm>();
	FString Err;
	if (E && FSSXPcm::LoadWav(FPaths::Combine(SSXAudio::ConvertedDir(), E->File), *Pcm, &Err))
	{
		IntroPlayer->Queue(Pcm);
	}
	else
	{
		UE_LOG(LogSSX, Warning, TEXT("[audio] intro segment %s: %s"), **Id, *Err);
	}
}

int32 USSXAudioSubsystem::GetIntroQueueLength() const
{
	return IntroPlayer.IsValid() ? IntroPlayer->GetQueueLength() : 0;
}

void USSXAudioSubsystem::ExecuteRaceCommands()
{
	using SSXRaceAudio::FCommand;
	for (const FCommand& C : RaceFlow.Commands)
	{
		RaceCommandLog.Add(C);
		switch (C.Kind)
		{
		case FCommand::Beep:
			UISoundLog.Add(SSXUISound::RaceId(C.Value));
			PlayOneShot(SSXUISound::RaceId(C.Value), GetSfxGain(), TEXT("countdown"));
			break;
		case FCommand::QueueIntro: QueueIntroSegment(C.Value); break;
		case FCommand::ClearIntro: if (IntroPlayer.IsValid()) { IntroPlayer->ClearQueue(); } break;
		case FCommand::StopIntro:
			if (IntroPlayer.IsValid()) { IntroPlayer->Stop(); }
			if (IntroComponent) { IntroComponent->Stop(); }
			break;
		case FCommand::UnpauseSong:
			if (Player.IsValid()) { Player->SetPaused(false); }
			SSXAudioDiag::Song(FString::Printf(TEXT("unpause %s (0x225260)"), *CurrentSong), GetMusicGain());
			break;
		case FCommand::PostEvent: PostMusicEvent(C.Value); break;
		case FCommand::SetControl:
			if (Player.IsValid()) { Player->SetControl(C.Value); }
			SSXAudioDiag::Song(FString::Printf(TEXT("control %d"), C.Value), GetMusicGain());
			break;
		case FCommand::PrepareSong:
			// 0x215220 / 0x224E68 / 0x225090
			if (!RaceSongStem.IsEmpty())
			{
				bMenuMusic = false;
				StartPathfinder(RaceSongStem, 0, RaceSongGain, SSXRaceAudio::SongStartControl, false);
			}
			else if (Player.IsValid())
			{
				Player->PostEvent(0, 1);   // FUN_00225380
				Player->SetControl(SSXRaceAudio::SongStartControl);
			}
			break;
		}
	}
	RaceFlow.Commands.Reset();
}

bool USSXAudioSubsystem::PlaySong(const FString& Song, int32 InStartEvent)
{
	LoadConfigs();
	float PathLevel = 1.f;
	const FString Stem = ResolveSongStem(Song, &PathLevel);
	if (Stem.IsEmpty())
	{
		UE_LOG(LogSSX, Warning, TEXT("[audio] song '%s' not in MUSIC.INF"), *Song);
		return false;
	}
	const float Gain = Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("MUSICLEVEL"), 0.93f) * PathLevel;
	bMenuMusic = false;
	return StartPathfinder(Stem, InStartEvent, Gain);
}

int32 USSXAudioSubsystem::GetJukeboxCount()
{
	LoadConfigs();
	const FSSXInfFile::FSection* S = JukeboxInf.Find(TEXT("GLOBAL"));
	return S ? S->GetAll(TEXT("SONG")).Num() : 0;
}

void USSXAudioSubsystem::JukeboxOpen()
{
	// FUN_00225468 / FUN_00229AE8 / FUN_00229D18
	LoadConfigs();
	MusicFade.Start(BigAirMusicByte, 0, 0.5f);
	bJukeboxOpen = true;
}

bool USSXAudioSubsystem::JukeboxPlay(int32 Index)
{
	// 0x21D020 / 0x229FA0 / 0x229868
	LoadConfigs();
	if (JukeboxComponent)
	{
		JukeboxComponent->Stop();
		JukeboxComponent = nullptr;
	}
	bJukeboxStarted = false;
	JukeboxSong = Index;
	const FSSXInfFile::FSection* S = JukeboxInf.Find(TEXT("GLOBAL"));
	const TArray<FString> Songs = S ? S->GetAll(TEXT("SONG")) : TArray<FString>();
	if (Index < 0 || !Songs.IsValidIndex(Index) || Songs[Index].IsEmpty())
	{
		return false;
	}
	const FString Name = FPaths::GetCleanFilename(Songs[Index].Replace(TEXT("\\"), TEXT("/"))).ToLower();
	const FString Id = FString::Printf(TEXT("music/jukebox/%s"), *Name);
	USoundWave* Wave = Cast<USoundWave>(LoadSound(Id));
	UWorld* World = GetAudioWorld();
	if (!Wave || !World)
	{
		return false;
	}
	Wave->bLooping = false;
	JukeboxComponent = UGameplayStatics::CreateSound2D(World, Wave, 1.f, 1.f, 0.f, nullptr, true, false);
	SSXAudioDiag::Note(TEXT("jukebox"), Id, false, GetJukeboxGain(), 0.0, SSXAudioDiag::SpawnResult(JukeboxComponent, false));
	if (!JukeboxComponent)
	{
		return false;
	}
	MakeGameSound(JukeboxComponent);
	ApplyJukeboxVolume();
	JukeboxComponent->Play();
	bJukeboxStarted = true;
	return true;
}

bool USSXAudioSubsystem::JukeboxSongEnded() const
{
	return bJukeboxStarted && !(JukeboxComponent && JukeboxComponent->IsPlaying());
}

void USSXAudioSubsystem::JukeboxStop()
{
	// 0x229D40 / FUN_00225468
	if (JukeboxComponent)
	{
		JukeboxComponent->Stop();
		JukeboxComponent = nullptr;
	}
	bJukeboxStarted = false;
	bJukeboxOpen = false;
	MusicFade.Start(BigAirMusicByte, 127, 0.5f);
}

float USSXAudioSubsystem::GetJukeboxGain() const
{
	return GetMasterGain() * Level(AudioInf, TEXT("FELEVELSCALING"), TEXT("JUKEBOXLEVEL"), 1.f) * DuckFor(SSXAudioDuck::EBus::Jukebox);
}

void USSXAudioSubsystem::ApplyJukeboxVolume()
{
	if (JukeboxComponent)
	{
		JukeboxComponent->SetVolumeMultiplier(GetJukeboxGain());
	}
}

void USSXAudioSubsystem::TickMusicFade(float Dt)
{
	int32 Byte = BigAirMusicByte;
	if (MusicFade.bActive && MusicFade.Tick(Dt, Byte))
	{
		SetBigAirMusicByte(Byte);
	}
}

void USSXAudioSubsystem::StopMusic(float FadeSeconds)
{
	if (MusicComponent || !CurrentSong.IsEmpty())
	{
		SSXAudioDiag::Song(FString::Printf(TEXT("stop %s fade %.1f s"), *CurrentSong, FadeSeconds), GetMusicGain());
	}
	if (MusicComponent)
	{
		MusicComponent->bAutoDestroy = true;
		if (FadeSeconds > 0.f && MusicComponent->IsPlaying())
		{
			MusicComponent->FadeOut(FadeSeconds, 0.f);
		}
		else
		{
			SSXWaveKeepAlive::Retire(MusicComponent);
		}
	}
	SSXWaveKeepAlive::Track(MusicWave);
	MusicComponent = nullptr;
	MusicWave = nullptr;
	CurrentSong.Reset();
}

void USSXAudioSubsystem::StopStreamVoices()
{
	if (IntroPlayer.IsValid()) { IntroPlayer->Stop(); }
	SSXWaveKeepAlive::Retire(IntroComponent);
	IntroComponent = nullptr;
	IntroWave = nullptr;
	for (int32 C = 0; C < SSXSpeech::NumSpeechChannels; ++C)
	{
		if (SpeechPlayers[C].IsValid()) { SpeechPlayers[C]->Stop(); }
		SSXWaveKeepAlive::Retire(SpeechComps[C]);
		SpeechComps[C] = nullptr;
		SpeechWaves[C] = nullptr;
	}
	SpeechWorld.Reset();
	for (const TSharedPtr<FSSXSegmentQueuePlayer>& P : ReleasedPlayers) { if (P.IsValid()) P->Stop(); }
	for (TObjectPtr<UAudioComponent>& C : ReleasedSpeech) { if (C) { C->bAutoDestroy = true; SSXWaveKeepAlive::Retire(C); } }
	ReleasedPlayers.Reset();
	ReleasedSpeech.Reset();
	ReleasedRacers.Reset();
}

void USSXAudioSubsystem::PostMusicEvent(int32 Event)
{
	if (Player.IsValid())
	{
		const TArray<FSSXPathfinderEvent>& Events = Player->GetSong().Events;
		const FSSXPathfinderEvent* E = Events.IsValidIndex(Event) ? &Events[Event] : nullptr;
		SSXAudioDiag::Song(FString::Printf(TEXT("event %d %s: %s"), Event, *CurrentSong,
			!E ? TEXT("not in the song")
			: !E->Acts() ? TEXT("no action (plays on)")
			: *FString::Printf(TEXT("go node %d %s"), E->Node, E->bImmediate ? TEXT("now") : TEXT("at the segment end"))), GetMusicGain());
		Player->PostEvent(Event);
	}
}

bool USSXAudioSubsystem::IsPathfinderPlaying() const
{
	return Player.IsValid() && Player->IsPlaying();
}

void USSXAudioSubsystem::UpdateRaceMusic(const FRiderState& S, float DeltaSeconds)
{
	(void)S;
	(void)DeltaSeconds;
}

void USSXAudioSubsystem::OnRiderEvent(ESSXRiderAudioEvent Event, float Magnitude)
{
	(void)Magnitude;
	if (const int32* MusicEvent = MusicEventForRiderEvent.Find(Event))
	{
		PostMusicEvent(*MusicEvent);
	}
}

USoundBase* USSXAudioSubsystem::LoadSound(const FString& Id)
{
	if (TObjectPtr<USoundBase>* Found = SoundCache.Find(Id))
	{
		return *Found;
	}
	const FSSXAudioEntry* E = GetManifest().Find(Id);
	const FString Path = E ? SSXAudio::AssetPathForFile(E->File) : FString();
	USoundBase* Sound = E ? LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	if (!Sound)
	{
		UE_LOG(LogSSX, Warning, TEXT("[audio] unresolved sound id %s (%s)"), *Id,
			E ? *FString::Printf(TEXT("asset %s missing"), *Path) : TEXT("not in the manifest"));
	}
	else
	{
		// 0x2D5148
		Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	}
	SoundCache.Add(Id, Sound);
	return Sound;
}

SSXAudioDiag::EResult USSXAudioSubsystem::MissReason(const FString& Id)
{
	return GetManifest().Find(Id) ? SSXAudioDiag::EResult::AssetMissing : SSXAudioDiag::EResult::NotInManifest;
}

void USSXAudioSubsystem::MakeGameSound(UAudioComponent* C)
{
	// 0x2153F8 / 0x20BFE8 / 0x225240 / 0x229E00
	if (C)
	{
		C->bIsUISound = false;
	}
}

USoundAttenuation* USSXAudioSubsystem::GameFalloff(double RadiusM)
{
	// 0x20B700
	const int32 Key = FMath::RoundToInt32(RadiusM * 100.0);
	TObjectPtr<USoundAttenuation>& A = FalloffCache.FindOrAdd(Key);
	if (!A)
	{
		A = NewObject<USoundAttenuation>(this, NAME_None, RF_Transient);
		FSoundAttenuationSettings& S = A->Attenuation;
		S.bAttenuate = true;
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector(SSXAudioFalloff::InnerCm, 0.0, 0.0);
		S.FalloffDistance = float(RadiusM * 100.0);
		S.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
		FRichCurve* Curve = S.CustomAttenuationCurve.GetRichCurve();
		Curve->Reset();
		for (int32 I = 0; I <= 16; ++I)
		{
			const float X = I / 16.f;
			Curve->AddKey(X, (1.f - X) * (1.f - X));
		}
	}
	A->Attenuation.bSpatialize = Speaker != 0;
	return A;
}

UAudioComponent* USSXAudioSubsystem::PlayGameSound(const TCHAR* System, const FString& Id, float Gain, const FVector* Pos, double RadiusM,
	float Pitch, bool bUI)
{
	using SSXAudioDiag::EResult;
	const bool b3D = Pos != nullptr;
	USoundBase* Sound = Id.IsEmpty() ? nullptr : LoadSound(Id);
	UWorld* World = GetAudioWorld();
	if (!Sound)
	{
		SSXAudioDiag::Note(System, Id, b3D, Gain, RadiusM, MissReason(Id), FString(), bUI);
		return nullptr;
	}
	if (!World)
	{
		SSXAudioDiag::Note(System, Id, b3D, Gain, RadiusM, EResult::NoWorld, Sound->GetPathName(), bUI);
		return nullptr;
	}
	if (Gain <= 0.f)
	{
		SSXAudioDiag::Note(System, Id, b3D, Gain, RadiusM, EResult::Volume0, Sound->GetPathName(), bUI);
		return nullptr;
	}
	UAudioComponent* C = nullptr;
	if (b3D)
	{
		C = UGameplayStatics::SpawnSoundAtLocation(World, Sound, *Pos, FRotator::ZeroRotator, Gain, Pitch, 0.f, GameFalloff(RadiusM),
			nullptr, true);
	}
	else if ((C = UGameplayStatics::CreateSound2D(World, Sound, Gain, Pitch, 0.f, nullptr, false, true)) != nullptr)
	{
		C->bIsUISound = bUI;
		C->Play();
	}
	SSXAudioDiag::Note(System, Id, b3D, Gain, RadiusM, SSXAudioDiag::SpawnResult(C, b3D), Sound->GetPathName(), bUI);
	SSXAudioDiag::NoteLoop(System, Id, C, b3D);
	AddBankVoice(C);
	return C;
}

void USSXAudioSubsystem::StopAllSounds()
{
	// 0x2213C8 / 0x221330 / 0x20C0F8
	++StopAllCalls;
	GetCrowdVoices().Crowd.RemoveAllEntries();
	for (const TWeakObjectPtr<UAudioComponent>& V : BankVoices)
	{
		if (UAudioComponent* C = V.Get())
		{
			SSXAudioDiag::NoteStop(C, TEXT("stop-all 0x2213C8"));
			C->Stop();
		}
	}
	BankVoices.Reset();
}

void USSXAudioSubsystem::AddBankVoice(UAudioComponent* C)
{
	if (C)
	{
		BankVoices.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& V) { return !V.IsValid(); });
		BankVoices.Add(C);
	}
}

UAudioComponent* USSXAudioSubsystem::PlaySound2D(const FString& Id, float Volume, float Pitch)
{
	return PlayGameSound(TEXT("ui"), Id, Volume * GetSfxGain(), nullptr, 0.0, Pitch, true);
}

float USSXAudioSubsystem::MusicClassLevel(bool bFrontEndTable) const
{
	return GetMasterGain() * MusicVolume * (bFrontEndTable ? Level(AudioInf, TEXT("FELEVELSCALING"), TEXT("MUSICLEVEL"), 0.95f)
		: Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("MUSICLEVEL"), 0.93f));
}

UAudioComponent* USSXAudioSubsystem::PlayOneShot(const FString& Id, float Gain, const TCHAR* System, bool bUI)
{
	return PlayGameSound(System, Id, Gain, nullptr, 0.0, 1.f, bUI);
}

void USSXAudioSubsystem::PlayUISound(FName Name)
{
	const FString N = Name.ToString();
	if (N.StartsWith(TEXT("ui")) && N.Len() > 2 && N.Mid(2).IsNumeric())
	{
		PlayFeSound(FCString::Atoi(*N.Mid(2)));
		return;
	}
	if (N.StartsWith(TEXT("music")) && N.Len() > 5 && N.Mid(5).IsNumeric())
	{
		PostMenuMusicEvent(FCString::Atoi(*N.Mid(5)));
		return;
	}
	if (const FString* Id = UISounds.Find(Name))
	{
		UISoundLog.Add(*Id);
		PlaySound2D(*Id);
	}
}

FString USSXAudioSubsystem::PlayFeSound(int32 Id)
{
	const SSXUISound::FFeSound S = SSXUISound::FeSoundForId(Id, FeMusicState);
	if (S.Sound < 0)
	{
		return FString();
	}
	const FString Sid = SSXUISound::FeId(S.Sound);
	UISoundLog.Add(Sid);
	TWeakObjectPtr<UAudioComponent>* Voice = S.Voice == 1 ? &FeVoice16 : (S.Voice == 2 ? &FeVoice17 : nullptr);
	if (Voice && Voice->IsValid())
	{
		(*Voice)->Stop();
	}
	const TCHAR* Key = S.LevelClass == 2 ? TEXT("PASPCHLEVEL") : (S.LevelClass == 3 ? TEXT("CHARSPCHLEVEL") : TEXT("SFXLEVEL"));
	const float User = S.LevelClass == 4 ? SfxVolume : (S.LevelClass == 3 ? CharSpeechVolume : SpeechVolume);
	UAudioComponent* C = PlayOneShot(Sid, GetMasterGain() * User * Level(AudioInf, TEXT("FELEVELSCALING"), Key, 1.f), TEXT("fe"), true);
	if (Voice)
	{
		*Voice = C;
	}
	return Sid;
}

FString USSXAudioSubsystem::PlayFeStinger(int32 Sound)
{
	if (FeStingerVoice.IsValid())
	{
		FeStingerVoice->Stop();
	}
	const FString Sid = SSXUISound::FeId(Sound);
	UISoundLog.Add(Sid);
	FeStingerVoice = PlayOneShot(Sid, GetMasterGain() * SfxVolume * Level(AudioInf, TEXT("FELEVELSCALING"), TEXT("SFXLEVEL"), 1.f), TEXT("fe"), true);
	return Sid;
}

FString USSXAudioSubsystem::PlayFeWhoosh(bool bEnter)
{
	// TODO: meaning of +0x7454)
	if (FeMusicState == 0)
	{
		return FString();
	}
	if (FeWhooshVoice.IsValid())
	{
		FeWhooshVoice->Stop();
	}
	const FString Sid = SSXUISound::FeId(bEnter ? SSXUISound::FeWhooshB : SSXUISound::FeWhooshA);
	UISoundLog.Add(Sid);
	UAudioComponent* C = PlayOneShot(Sid, GetMasterGain() * SfxVolume * Level(AudioInf, TEXT("FELEVELSCALING"), TEXT("SFXLEVEL"), 1.f), TEXT("fe"), true);
	if (bEnter)
	{
		FeWhooshVoice = C;
	}
	return Sid;
}

FString USSXAudioSubsystem::PlayRaceUISound(SSXUISound::ERace Kind)
{
	const SSXUISound::FRaceSound S = SSXUISound::RaceSound(Kind);
	if (S.bStopShared && RaceUIVoice.IsValid())
	{
		RaceUIVoice->Stop();
	}
	// TODO: accept / back write parameter-frame byte 1 (0x5C / 0x18, probably pan with 0x40 = centre);
	const FString Sid = SSXUISound::RaceId(S.Sound);
	UISoundLog.Add(Sid);
	float Gain = GetSfxGain();
	switch (Kind)
	{
	// 0x212E98
	case SSXUISound::ERace::PreviewMaster: Gain = GetMasterGain(); break;
	case SSXUISound::ERace::PreviewMusic: Gain = GetMasterGain() * MusicVolume; break;
	case SSXUISound::ERace::PreviewSpeech: Gain = GetMasterGain() * SpeechVolume; break;
	case SSXUISound::ERace::PreviewChar: Gain = GetMasterGain() * CharSpeechVolume; break;
	default: break;
	}
	RaceUIVoice = PlayOneShot(Sid, Gain, TEXT("raceui"), true);   // 0x20B948
	return Sid;
}

FString USSXAudioSubsystem::PlayRiderSpeech(int32 Variant, int32 Value)
{
	// TODO: ).
	if (Value < 0 || Value >= 0x80)
	{
		return FString();
	}
	const FString Sid = FString::Printf(TEXT("speech/feselect%d/%02d"), Variant, Value);
	UISoundLog.Add(Sid);
	PlayOneShot(Sid, GetMasterGain() * CharSpeechVolume * Level(AudioInf, TEXT("FELEVELSCALING"), TEXT("CHARSPCHLEVEL"), 1.f), TEXT("fe"), true);
	return Sid;
}

float USSXAudioSubsystem::GetMusicGain() const
{
	return GetMasterGain() * MusicVolume * CurrentSongGain * (BigAirMusicByte / 127.f) * DuckFor(SSXAudioDuck::EBus::Music);
}

void USSXAudioSubsystem::ApplyMusicVolume()
{
	if (MusicComponent)
	{
		MusicComponent->SetVolumeMultiplier(GetMusicGain());
	}
}

void USSXAudioSubsystem::ApplySpeechVolume()
{
	// 0x211DB0
	for (int32 C = 0; C < SSXSpeech::NumSpeechChannels; ++C)
	{
		if (SpeechComps[C])
		{
			SpeechComps[C]->SetVolumeMultiplier(SpeechGain(C));
		}
	}
}

void USSXAudioSubsystem::SetSpeaker(int32 InSpeaker)
{
	if (InSpeaker == Speaker)
	{
		return;
	}
	Speaker = InSpeaker;
	++SpeakerRestarts;
	// TODO: the call is gated by the cBXAudio vfunc_02 result (1 = rebuild) whose source (0x227A48) is not identified;
	StopSpeech(0);
	StopSpeech(1);
	if (bMenuMusic && bFrontEndMusicOn)
	{
		FeMusicState = 0;
		StopMusic(0.f);
		Player.Reset();
		PlayMenuMusic(StartEvent);
	}
}

void USSXAudioSubsystem::SetBigAirMusicByte(int32 Byte)
{
	Byte = FMath::Clamp(Byte, 0, 127);
	if (Byte != BigAirMusicByte)
	{
		if (Byte == 127 || BigAirMusicByte == 127)
		{
			SSXAudioDiag::Song(Byte == 127 ? FString(TEXT("big-air duck off (byte 127)")) : FString::Printf(TEXT("big-air duck on (byte %d)"), Byte),
				GetMasterGain() * MusicVolume * CurrentSongGain * (Byte / 127.f) * DuckFor(SSXAudioDuck::EBus::Music));
		}
		BigAirMusicByte = Byte;
		ApplyMusicVolume();
	}
}

void USSXAudioSubsystem::SetMasterVolume(float V)
{
	// 0x211DB0 / 0x270FF8 / 0x2D2F90 / 0x2D5148
	MasterVolume = FMath::Clamp(V, 0.f, 1.f);
	ApplyMusicVolume(); ApplySpeechVolume(); ApplyJukeboxVolume(); ApplyIntroVolume();
}

float USSXAudioSubsystem::GetMasterGain() const
{
	// 0x2D5148
	return float(FMath::TruncToInt32(MasterVolume * 127.f)) / 127.f;
}

float USSXAudioSubsystem::GetIntroGain() const
{
	return GetMasterGain() * IntroBaseVolume * DuckFor(SSXAudioDuck::EBus::Music);
}

void USSXAudioSubsystem::ApplyIntroVolume()
{
	if (IntroComponent)
	{
		IntroComponent->SetVolumeMultiplier(GetIntroGain());
	}
}
void USSXAudioSubsystem::SetMusicVolume(float V) { MusicVolume = FMath::Clamp(V, 0.f, 1.f); ApplyMusicVolume(); }
void USSXAudioSubsystem::SetSfxVolume(float V) { SfxVolume = FMath::Clamp(V, 0.f, 1.f); }
void USSXAudioSubsystem::SetSpeechVolume(float V) { SpeechVolume = FMath::Clamp(V, 0.f, 1.f); ApplySpeechVolume(); }
void USSXAudioSubsystem::SetCharSpeechVolume(float V) { CharSpeechVolume = FMath::Clamp(V, 0.f, 1.f); ApplySpeechVolume(); }

float USSXAudioSubsystem::GetSfxGain() const
{
	const float InfLevel = Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("SFXLEVEL"), 1.f);
	return GetMasterGain() * SfxVolume * InfLevel * DuckFor(SSXAudioDuck::EBus::Sfx);
}

bool USSXAudioSubsystem::LoadSpeech(FSSXSpeechSystem::ESet Set)
{
	for (int32 C = 0; C < SSXSpeech::NumSpeechChannels; ++C)
	{
		StopSpeech(C);
	}
	const FString Dir = FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("speech"));
	FString Err;
	if (!Speech.Load(Set, Dir, &Err, FGameRng::StreamA().Next()))   // 0x228830
	{
		UE_LOG(LogSSX, Log, TEXT("[speech] not loaded: %s"), *Err);
		return false;
	}
	if (Set == FSSXSpeechSystem::ESet::FrontEnd)
	{
		Speech.LoadFeTable(FPaths::Combine(Dir, TEXT("speech_tables.json")));
	}
	if (!Speech.OnSegment)
	{
		// 0x227B00
		Speech.OnSegment = [this](int32 Ch, const SSXSpeech::FSampleGroup& G, const SSXSpeech::FResolvedSample& Smp, bool bQueue)
		{
			PlaySpeechSegment(Ch, G, Smp, bQueue);
		};
	}
	UE_LOG(LogSSX, Display, TEXT("[speech] %s databases loaded (%d sample groups)"),
		Set == FSSXSpeechSystem::ESet::Race ? TEXT("race") : TEXT("FE"), Speech.Engine.GetGroups().Num());
	return true;
}

void USSXAudioSubsystem::EnsureSpeechPlayers()
{
	UWorld* World = GetAudioWorld();
	if (!World || (SpeechWorld.Get() == World && SpeechComps[0] && SpeechComps[1]))
	{
		return;
	}
	SpeechWorld = World;
	for (int32 C = 0; C < SSXSpeech::NumSpeechChannels; ++C)
	{
		SSXWaveKeepAlive::Retire(SpeechComps[C]);
		SpeechPlayers[C] = MakeShared<FSSXSegmentQueuePlayer>(36000, 2);
		SpeechWaves[C] = NewObject<USSXSegmentQueueWave>(GetTransientPackage());
		SpeechWaves[C]->Init(SpeechPlayers[C]);
		SSXWaveKeepAlive::Track(SpeechWaves[C]);
		SpeechComps[C] = UGameplayStatics::CreateSound2D(World, SpeechWaves[C], 1.f, 1.f, 0.f, nullptr, true, false);
		if (SpeechComps[C])
		{
			MakeGameSound(SpeechComps[C]);   // 0x229E00
			SpeechComps[C]->SetVolumeMultiplier(SpeechGain(C));
			SpeechComps[C]->Play();
		}
	}
}

float USSXAudioSubsystem::SpeechGain(int32 Channel) const
{
	const bool bFe = Speech.GetSet() == FSSXSpeechSystem::ESet::FrontEnd;
	const TCHAR* Section = bFe ? TEXT("FELEVELSCALING") : TEXT("IGLEVELSCALING");
	const TCHAR* Key = Channel == 0 ? TEXT("PASPCHLEVEL") : TEXT("CHARSPCHLEVEL");
	return GetMasterGain() * (Channel == 0 ? SpeechVolume : CharSpeechVolume) * Level(AudioInf, Section, Key, 1.f)
		* DuckFor(Channel == 0 ? SSXAudioDuck::EBus::PaSpeech : SSXAudioDuck::EBus::CharSpeech);
}

void USSXAudioSubsystem::PlaySpeechSegment(int32 Channel, const SSXSpeech::FSampleGroup& Group, const SSXSpeech::FResolvedSample& Smp, bool bQueue)
{
	if (Channel < 0 || Channel >= SSXSpeech::NumSpeechChannels)
	{
		return;
	}
	const FString Key = FString::Printf(TEXT("%s/%s/%08llx"), *Group.Section, *Group.DatStem, (unsigned long long)Smp.Offset);
	const FString Path = FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("speech"), Key + TEXT(".wav"));
	SpeechLog.Add(Key);
	SpeechTag[Channel] = PendingTag[Channel];
	TSharedPtr<FSSXPcm> Pcm = MakeShared<FSSXPcm>();
	const TCHAR* DiagSys = Channel == 0 ? TEXT("speech-mc") : TEXT("speech-char");
	if (!FPaths::FileExists(Path) || !FSSXPcm::LoadWav(Path, *Pcm))
	{
		++SpeechMissing;
		SSXAudioDiag::Note(DiagSys, Key, bSpeechPos[Channel], SpeechGain(Channel), 0.0, SSXAudioDiag::EResult::AssetMissing, Path);
		return;
	}
	SSXAudioDiag::Note(DiagSys, Key, bSpeechPos[Channel], SpeechGain(Channel), 0.0,
		GetAudioWorld() ? SSXAudioDiag::EResult::Played : SSXAudioDiag::EResult::NoWorld, Path);
	if (Pcm->Channels == 1)
	{
		TArray<int16> St;
		St.SetNumUninitialized(Pcm->Samples.Num() * 2);
		for (int32 I = 0; I < Pcm->Samples.Num(); ++I) { St[2 * I] = St[2 * I + 1] = Pcm->Samples[I]; }
		Pcm->Samples = MoveTemp(St);
		Pcm->Channels = 2;
	}
	PlayingRacer[Channel] = SpeechRacer[Channel];
	PlayingRange[Channel] = SpeechRange[Channel];
	EnsureSpeechPlayers();
	FSSXSegmentQueuePlayer* P = SpeechPlayers[Channel].Get();
	if (!P)
	{
		return;
	}
	// 0x228400 / 0x2D2948 / 0x2D2728 / 0x228D10
	P->Queue(Pcm);
	if (SpeechComps[Channel])
	{
		// 0x228BD8
		UAudioComponent* C = SpeechComps[Channel];
		PlaceSpeech(C, bSpeechPos[Channel], SpeechPos[Channel], SpeechRange[Channel], SpeechGain(Channel));
		if (!C->IsPlaying()) { C->Play(); }
	}
}

bool USSXAudioSubsystem::IsSpeechPlaying(int32 Channel) const
{
	if (Channel == 1)
	{
		for (const TSharedPtr<FSSXSegmentQueuePlayer>& P : ReleasedPlayers)
		{
			if (P.IsValid() && P->GetQueueLength() > 0) return true;
		}
	}
	return Channel >= 0 && Channel < SSXSpeech::NumSpeechChannels && SpeechPlayers[Channel].IsValid() && SpeechPlayers[Channel]->GetQueueLength() > 0;
}

void USSXAudioSubsystem::PauseForMovie()
{
	StopSpeech(0);   // FUN_00228D10
	PauseGameAudio();
}

void USSXAudioSubsystem::PauseGameAudio()
{
	if (bMoviePaused)
	{
		return;   // FUN_002153F8
	}
	bMoviePaused = true;
	++MoviePauses;
	SSXAudioDiag::Song(FString::Printf(TEXT("pause %s (0x2153F8, snd+0x3830 = 1)"), *CurrentSong), GetMusicGain());
	if (Player.IsValid()) { Player->SetPaused(true); }
	for (UAudioComponent* C : SpeechComps)
	{
		if (C) { C->SetPaused(true); }
	}
}

void USSXAudioSubsystem::ResumeAfterMovie()
{
	if (!bMoviePaused)
	{
		return;
	}
	bMoviePaused = false;
	++MovieResumes;
	SSXAudioDiag::Song(FString::Printf(TEXT("resume %s (0x215488, snd+0x3830 = 0)"), *CurrentSong), GetMusicGain());
	for (UAudioComponent* C : SpeechComps)
	{
		if (C) { C->SetPaused(false); }
	}
	if (Player.IsValid()) { Player->SetPaused(false); }
}

void USSXAudioSubsystem::StopSpeech(int32 Channel)
{
	if (Speech.IsLoaded())
	{
		Speech.Stop(Channel);
	}
	else if (Channel >= 0 && Channel < SSXSpeech::NumSpeechChannels && SpeechPlayers[Channel].IsValid())
	{
		SpeechPlayers[Channel]->Stop();
	}
	if (Channel == 1)
	{
		for (const TSharedPtr<FSSXSegmentQueuePlayer>& P : ReleasedPlayers) { if (P.IsValid()) P->Stop(); }
		for (TObjectPtr<UAudioComponent>& C : ReleasedSpeech) { if (C) { C->bAutoDestroy = true; SSXWaveKeepAlive::Retire(C); } }
		ReleasedPlayers.Reset();
		ReleasedSpeech.Reset();
		ReleasedRacers.Reset();
	}
}

bool USSXAudioSubsystem::IsChannelFree(int32 Channel)
{
	if (Channel < 0 || Channel >= SSXSpeech::NumSpeechChannels)
	{
		return false;
	}
	constexpr int64 EndingFrames = 500 * 36;   // 500 units at 36000 Hz, units read as ms
	int64 Left = 0;
	if (SpeechPlayers[Channel].IsValid() && SpeechPlayers[Channel]->GetQueueLength() > 0)
	{
		Left = FMath::Max(Left, SpeechPlayers[Channel]->GetRemainingFrames());
	}
	if (Channel == 1)
	{
		for (const TSharedPtr<FSSXSegmentQueuePlayer>& P : ReleasedPlayers)
		{
			if (P.IsValid() && P->GetQueueLength() > 0) Left = FMath::Max(Left, P->GetRemainingFrames());
		}
	}
	return Left < EndingFrames;
}

void USSXAudioSubsystem::UpdateSpeech()
{
	if (!Speech.IsLoaded())
	{
		return;
	}
	for (int32 C = 0; C < SSXSpeech::NumSpeechChannels; ++C)
	{
		if (IsChannelFree(C))
		{
			PendingTag[C] = 0;
			Speech.UpdateChannel(C, IsSpeechPlaying(C));
		}
	}
}

void USSXAudioSubsystem::CharRequest(const SSXSpeech::FRequest& R, const FVector& SpeakerPos, int32 Tag, int32 Racer)
{
	SetSpeechPosition(1, &SpeakerPos, Racer);
	PendingTag[1] = Tag;
	Speech.Request(R);
	PendingTag[1] = 0;
	SetSpeechPosition(1, nullptr);
}

void USSXAudioSubsystem::StopChannel1IfTag(int32 Tag)
{
	if (IsSpeechPlaying(1) && SpeechTag[1] == Tag)
	{
		StopSpeech(1);
	}
}

void USSXAudioSubsystem::SetSpeechPosition(int32 Channel, const FVector* Pos, int32 Racer, double RangeM)
{
	if (Channel < 0 || Channel >= SSXSpeech::NumSpeechChannels)
	{
		return;
	}
	bSpeechPos[Channel] = Pos != nullptr;
	SpeechPos[Channel] = Pos ? *Pos : FVector::ZeroVector;
	SpeechRacer[Channel] = Pos ? Racer : -1;
	SpeechRange[Channel] = Pos ? RangeM : 0.0;
}

void USSXAudioSubsystem::PlaceSpeech(UAudioComponent* C, bool bPositioned, const FVector& Pos, double RangeM, float Gain)
{
	if (!C)
	{
		return;
	}
	// 0x226DD8 / 0x20FB68
	C->bAllowSpatialization = bPositioned;
	C->bOverrideAttenuation = false;
	C->AttenuationSettings = bPositioned ? GameFalloff(RangeM > 0.0 ? RangeM : SSXAudioFalloff::DefaultRadiusM) : nullptr;
	if (bPositioned) { C->SetWorldLocation(Pos); }
	C->SetVolumeMultiplier(Gain);
}

int32 USSXAudioSubsystem::GetSpeechQueueLength(int32 Channel) const
{
	return Channel >= 0 && Channel < SSXSpeech::NumSpeechChannels && SpeechPlayers[Channel].IsValid() ? SpeechPlayers[Channel]->GetQueueLength() : 0;
}

void USSXAudioSubsystem::FollowSpeakers()
{
	if (!RacerLocation)
	{
		return;
	}
	if (PlayingRacer[1] >= 0 && SpeechComps[1] && SpeechComps[1]->IsPlaying())
	{
		SpeechComps[1]->SetWorldLocation(RacerLocation(PlayingRacer[1]));
	}
	for (int32 I = 0; I < ReleasedSpeech.Num() && I < ReleasedRacers.Num(); ++I)
	{
		if (ReleasedRacers[I] >= 0 && ReleasedSpeech[I] && ReleasedSpeech[I]->IsPlaying())
		{
			ReleasedSpeech[I]->SetWorldLocation(RacerLocation(ReleasedRacers[I]));
		}
	}
}

bool USSXAudioSubsystem::RingPush(int32 V)
{
	if ((RingWrite + 1) % RingSize == RingRead)
	{
		return false;
	}
	Ring[RingWrite] = V;
	RingWrite = (RingWrite + 1) % RingSize;
	return true;
}

void USSXAudioSubsystem::IntroSpeechBegin()
{
	if (bIntroRing)
	{
		return;
	}
	bIntroRing = true;
	RingWrite = RingRead = 0;
	HeldLines.Reset();
	AnimEventLog.Add(TEXT("begin"));
	StopSpeech(1);
}

void USSXAudioSubsystem::IntroSpeechPost(int32 Value)
{
	if (Value < 1000)
	{
		return;   // 0x2356F0
	}
	if (!bIntroRing)
	{
		// 0x235BA8
		return;
	}
	TArray<SSXSpeech::FResolvedSample> Segs;
	Speech.RequestHeld(SSXSpeech::AnimRequest(Value), Segs);
	if (Segs.Num() > 0)
	{
		const int32 H = HeldLines.Add({ MoveTemp(Segs), Value });
		RingPush(H);
		AnimEventLog.Add(FString::Printf(TEXT("post:%d:%d"), Value, H));
	}
	else
	{
		RingPush(-1);
		AnimEventLog.Add(FString::Printf(TEXT("post:%d:-1"), Value));
	}
	RingPush(-2);
}

void USSXAudioSubsystem::IntroSpeechEnd()
{
	StopSpeech(1);   // 0x235B70
	if (bIntroRing)
	{
		StopSpeech(1);   // 0x2356A0
		bIntroRing = false;
		RingWrite = RingRead = 0;
		HeldLines.Reset();
		AnimEventLog.Add(TEXT("end"));
	}
}

void USSXAudioSubsystem::OnAnimEvent(int32 Value, int32 Racer, const FVector& RiderPos, bool bHuman)
{
	if (!bAnimEventSounds)
	{
		return;
	}
	if (Value < 1000)
	{
		if (Value < 0x80)
		{
			AnimEventLog.Add(FString::Printf(TEXT("anim:%d"), Value));
		}
		else if ((Value == 0xD0 || Value == 0xD1) && bHuman)
		{
			// 0x21D9B0 / 0x21D680
			const int32 Snd = Value == 0xD0 ? 0x50 : 0x51;
			const FString Id = FString::Printf(TEXT("sfx/zboard/%d"), Snd);
			AnimEventLog.Add(FString::Printf(TEXT("zboard:%d"), Snd));
			UISoundLog.Add(Id);
			// 0x226DD8 / 0x20AAB8
			PlayGameSound(TEXT("animevent"), Id, FMath::Clamp(GetSfxGain(), 0.f, 1.f), &RiderPos, SSXAudioFalloff::DefaultRadiusM);
		}
		return;
	}
	if (!bIntroRing)
	{
		// 0x235BA8
		AnimEventLog.Add(FString::Printf(TEXT("anim:%d"), Value));
		SetSpeechPosition(1, &RiderPos, Racer, SSXAudioFalloff::AnimSpeechRangeM);
		Speech.Request(SSXSpeech::AnimRequest(Value));
		SetSpeechPosition(1, nullptr);
		return;
	}
	// 0x20C200 / 0x22A1E0
	if (RingRead == RingWrite)
	{
		AnimEventLog.Add(FString::Printf(TEXT("release:%d:none"), Racer));
		return;
	}
	int32 E = Ring[RingRead];
	RingRead = (RingRead + 1) % RingSize;
	while (E != -2)
	{
		if (E >= 0 && HeldLines.IsValidIndex(E))
		{
			AnimEventLog.Add(FString::Printf(TEXT("release:%d:%d"), Racer, HeldLines[E].Value));
			PlaySpeechLineAt(HeldLines[E], RiderPos, Racer);
		}
		if (RingRead == RingWrite)
		{
			break;
		}
		E = Ring[RingRead];
		RingRead = (RingRead + 1) % RingSize;
	}
}

void USSXAudioSubsystem::PlaySpeechLineAt(const FHeldLine& Line, const FVector& Pos, int32 Racer)
{
	const TArray<SSXSpeech::FSampleGroup>& Groups = Speech.Engine.GetGroups();
	TSharedPtr<FSSXSegmentQueuePlayer> P;
	for (const SSXSpeech::FResolvedSample& Smp : Line.Segments)
	{
		if (!Groups.IsValidIndex(Smp.Group))
		{
			continue;
		}
		const SSXSpeech::FSampleGroup& Group = Groups[Smp.Group];
		const FString Key = FString::Printf(TEXT("%s/%s/%08llx"), *Group.Section, *Group.DatStem, (unsigned long long)Smp.Offset);
		SpeechLog.Add(Key);
		const FString Path = FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("speech"), Key + TEXT(".wav"));
		TSharedPtr<FSSXPcm> Pcm = MakeShared<FSSXPcm>();
		if (!FPaths::FileExists(Path) || !FSSXPcm::LoadWav(Path, *Pcm))
		{
			++SpeechMissing;
			continue;
		}
		if (Pcm->Channels == 1)
		{
			TArray<int16> St;
			St.SetNumUninitialized(Pcm->Samples.Num() * 2);
			for (int32 I = 0; I < Pcm->Samples.Num(); ++I) { St[2 * I] = St[2 * I + 1] = Pcm->Samples[I]; }
			Pcm->Samples = MoveTemp(St);
			Pcm->Channels = 2;
		}
		if (!P.IsValid())
		{
			P = MakeShared<FSSXSegmentQueuePlayer>(36000, 2);
		}
		P->Queue(Pcm);
	}
	UWorld* World = GetAudioWorld();
	if (!P.IsValid() || !World)
	{
		return;
	}
	// 0x235BA8
	USSXSegmentQueueWave* Wave = NewObject<USSXSegmentQueueWave>(GetTransientPackage());
	Wave->Init(P);
	SSXWaveKeepAlive::Track(Wave);
	UAudioComponent* C = UGameplayStatics::CreateSound2D(World, Wave, 1.f, 1.f, 0.f, nullptr, false, true);
	if (C)
	{
		MakeGameSound(C);
		PlaceSpeech(C, true, Pos, SSXAudioFalloff::AnimSpeechRangeM, SpeechGain(1));
		C->Play();
	}
	ReleasedSpeech.Add(C);
	ReleasedPlayers.Add(P);
	ReleasedRacers.Add(Racer);
	for (int32 I = ReleasedPlayers.Num() - 1; I >= 0; --I)
	{
		if (!ReleasedPlayers[I].IsValid() || ReleasedPlayers[I]->GetQueueLength() == 0)
		{
			if (ReleasedSpeech.IsValidIndex(I) && ReleasedSpeech[I]) { ReleasedSpeech[I]->bAutoDestroy = true; }
			ReleasedPlayers.RemoveAt(I);
			if (ReleasedSpeech.IsValidIndex(I)) ReleasedSpeech.RemoveAt(I);
			if (ReleasedRacers.IsValidIndex(I)) ReleasedRacers.RemoveAt(I);
		}
	}
}

void USSXAudioSubsystem::TickDuck(int32 Mode, double DuckLevel, float Dt)
{
	// 0x20EE58 / 0x211FB8 / 0x212018 / 0x20D9F0
	const bool bPlaying = Mode != 4 && Mode >= 0 && Mode < SSXSpeech::NumSpeechChannels && IsSpeechPlaying(Mode);
	const float Target = bPlaying ? float(DuckLevel) : 1.f;
	if (Target != LoggedDuckTarget)
	{
		LoggedDuckTarget = Target;
		SSXAudioDiag::Song(FString::Printf(TEXT("speech duck -> %.2f (mode %d)"), Target, Mode), GetMusicGain());
	}
	if (DuckFade.Tick(Mode, bPlaying, DuckLevel, Dt))
	{
		ApplyMusicVolume();
		ApplySpeechVolume();
		ApplyIntroVolume();
	}
	NoteSongState();
}

void USSXAudioSubsystem::NoteSongState()
{
	const bool bNow = Player.IsValid() && Player->IsPlaying();
	if (Player.IsValid() && bNow != bLoggedSongPlaying)
	{
		SSXAudioDiag::Song(FString::Printf(TEXT("stream %s %s"), *CurrentSong, bNow ? TEXT("playing")
			: (Player->ReachedEnd() ? TEXT("ended (the song's end node)") : TEXT("stopped"))), GetMusicGain());
		bLoggedSongPlaying = bNow;
	}
}

void USSXAudioSubsystem::PlayZbxsfx(int32 Sound)
{
	const FString Id = SSXUISound::RaceId(Sound);
	UISoundLog.Add(Id);
	PlayOneShot(Id, GetSfxGain());
}

void USSXAudioSubsystem::PlayZbxsfxAt(int32 Sound, const FVector& Pos, float Volume01)
{
	const FString Id = SSXUISound::RaceId(Sound);
	UISoundLog.Add(Id);
	// 0x216960 / 0x20AAB8 / 0x226DD8
	PlayGameSound(TEXT("zbxsfx3d"), Id, FMath::Clamp(Volume01 * GetSfxGain(), 0.f, 1.f), &Pos, SSXAudioFalloff::DefaultRadiusM);
}

void USSXAudioSubsystem::OnTweakStarted()
{
	if (ArcadeSfx != 0)
	{
		PlayZbxsfx(119);
	}
}

FSSXTrickyPhraseContext USSXAudioSubsystem::TrickyPhraseContext(int32 Mode)
{
	LoadConfigs();
	FSSXTrickyPhraseContext C;
	C.Mode = Mode;
	C.bSongEndPending = !RaceFlow.bEvent10Armed;
	C.bSongLoaded = !bMenuMusic && Player.IsValid() && !CurrentSong.IsEmpty();
	if (!C.bSongLoaded)
	{
		return C;
	}
	const FSSXInfFile::FSection* Sec = nullptr;
	for (const FString& Name : SSXRaceAudio::SongList(MusicInf))
	{
		const FSSXInfFile::FSection* S = MusicInf.Find(Name);
		if (S && FPaths::GetBaseFilename(S->Get(TEXT("PATHDATA"))).ToLower() == CurrentSong)
		{
			Sec = S;
			break;
		}
	}
	if (!Sec)
	{
		return C;
	}
	const double Bpm = FCString::Atod(*Sec->Get(TEXT("BPM"), TEXT("120")));
	C.BeatMs = Bpm > 0.0 ? 60000.0 / Bpm : 0.0;
	// TODO: the beat clock's origin is not given;
	const double PlayedMs = Player->GetPlayedSeconds() * 1000.0;
	C.MsToNextBeat = C.BeatMs > 0.0 ? C.BeatMs - FMath::Fmod(PlayedMs, C.BeatMs) : 0.0;
	TrickyLoopStem = FPaths::GetBaseFilename(Sec->Get(TEXT("LOOPDATA"))).ToLower();
	const FString Lvl = Sec->Get(TEXT("ASYNCLEVEL"));
	TrickyLoopLevel = Lvl.IsEmpty() ? 1.f : FCString::Atof(*Lvl) / 100.f;
	const FSSXAudioManifest& M = GetManifest();
	for (int32 K = 15; K >= 0 && !TrickyLoopStem.IsEmpty(); --K)
	{
		if (M.Find(FString::Printf(TEXT("music/%s/%02d"), *TrickyLoopStem, K * SSXTrickyAudio::PhraseAlign)))
		{
			C.LastPhraseBase = K * SSXTrickyAudio::PhraseAlign;
			break;
		}
	}
	return C;
}

void USSXAudioSubsystem::ExecuteTrickyCommands(TFunctionRef<uint32()> RandA)
{
	using SSXTrickyAudio::ECmd;
	const float UberLevel = Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("UBERLEVEL"), 0.85f);
	for (const SSXTrickyAudio::FCmd& C : Tricky.Out)
	{
		switch (C.Kind)
		{
		case ECmd::Zbxsfx:
			PlayZbxsfx(C.Value);
			break;
		case ECmd::TrickyBank:
		{
			if (TrickyVoice.IsValid()) TrickyVoice->Stop();
			const FString Id = FString::Printf(TEXT("sfx/tricky/%02d"), C.Value);
			UISoundLog.Add(Id);
			TrickyVoice = PlayOneShot(Id, GetMasterGain() * SfxVolume * UberLevel * DuckFor(SSXAudioDuck::EBus::Uber), TEXT("tricky"));
			break;
		}
		case ECmd::PhraseSlice:
		{
			// TODO: 90 while snd+0x6E50 is set;
			const FString Id = FString::Printf(TEXT("music/%s/%02d"), *TrickyLoopStem, C.Value);
			UISoundLog.Add(Id);
			const float Gain = GetMasterGain() * MusicVolume * TrickyLoopLevel * SSXTrickyAudio::AsyncVolume / 127.f
				* Level(AudioInf, TEXT("IGLEVELSCALING"), TEXT("MUSICLEVEL"), 0.93f);
			PhraseVoice = PlayOneShot(Id, Gain, TEXT("trickyphrase"));
			break;
		}
		case ECmd::PhraseStop:
			UISoundLog.Add(TEXT("tricky phrase stop"));
			break;
		case ECmd::Speech:
			Announcer.TrickyActivation(Speech, RandA);
			break;
		}
	}
	Tricky.Out.Reset();
}

void USSXAudioSubsystem::PlayPointsTick()
{
	if (TickVoice.IsValid()) TickVoice->Stop();
	const FString Id = SSXUISound::RaceId(SSXTrickyAudio::PointsTickSound);
	UISoundLog.Add(Id);
	TickVoice = PlayOneShot(Id, GetSfxGain());
}

