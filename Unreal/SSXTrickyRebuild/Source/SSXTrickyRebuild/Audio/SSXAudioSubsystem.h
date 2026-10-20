// 0x11A348 / FUN_0021C0A0
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXAudioDiag.h"
#include "Audio/SSXAudioTypes.h"
#include "Audio/SSXRaceAudioFlow.h"
#include "Audio/SSXAnnouncer.h"
#include "Audio/SSXRiderSpeech.h"
#include "Audio/SSXSpeechSystem.h"
#include "Audio/SSXTrickyAudio.h"
#include "Audio/SSXUISounds.h"
#include "Race/GameRng.h"
#include "Audio/SSXZoneAmbience.h"
#include "Math/RandomStream.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SSXAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundAttenuation;

namespace SSXAudioFalloff
{
	constexpr double InnerCm = 50.0;            // 0x20AAB8
	constexpr double DefaultRadiusM = 30.0;
	constexpr double TrackSoundRadiusM = 30.0;  // FUN_00226DD8 / 0x2165C8
	constexpr double AnimSpeechRangeM = 1500.0; // 0x235BA8 / 0x228BD8 / 0x226DD8
}

// 0x212018
namespace SSXAudioDuck
{
	enum class EBus : uint8 { Music, PaSpeech, CharSpeech, Sfx, Jukebox, Uber };
	// 0x20D9F0
	inline int32 Selector(int32 Mode) { return Mode == 0 || Mode == 1 ? 3 : (Mode == 2 ? 1 : (Mode == 3 ? 4 : 5)); }
	/** 0x212018: whether the selector scales the bus (level = base x duck gain). Selector 3 scales all but the PA speech; 4 all but the character speech. */
	inline bool Scales(int32 Sel, EBus B)
	{
		switch (Sel)
		{
		case 0: return B == EBus::Music || B == EBus::PaSpeech || B == EBus::CharSpeech || B == EBus::Jukebox;
		case 1: return B == EBus::PaSpeech || B == EBus::CharSpeech || B == EBus::Sfx || B == EBus::Jukebox;
		case 2: return B == EBus::Music || B == EBus::Sfx || B == EBus::Jukebox || B == EBus::Uber;
		case 3: return B != EBus::PaSpeech;
		case 4: return B != EBus::CharSpeech;
		default: return false;
		}
	}
	// 0x20EE58 / 0x211FB8
	struct FFade
	{
		float Gain = 1.f;
		float Target = 1.f;
		float Step = 0.f;
		int32 Sel = 5;
		bool Tick(int32 Mode, bool bStreamPlaying, double Level, float Dt)
		{
			const float T = (Mode != 4 && bStreamPlaying) ? float(Level) : 1.f;
			if (T != Target)
			{
				Target = T;
				Step = (T - Gain) / 1.f;
				if (Mode != 4) { Sel = Selector(Mode); }   // 0x21D610
			}
			const float Prev = Gain;
			if (Step != 0.f)
			{
				Gain += Step * Dt;
				if ((Step < 0.f && Gain < Target) || (Step > 0.f && Gain > Target)) { Gain = Target; }
				if (Gain == Target) { Step = 0.f; }
			}
			return Gain != Prev;
		}
		float For(EBus B) const { return Scales(Sel, B) ? Gain : 1.f; }
	};
}

/** FUN_00225468(seconds, snd, target) song volume-byte fade (snd+0x36F0 -> +0x10): step = int((target - byte) / 25) / seconds, advanced from a 40 ms deferred call (0x2254F8); ends at the target (a step of 0 or |target - byte| < 25 ends at the first call). */
namespace SSXMusicFade
{
	constexpr float PeriodMs = 40.f;
	struct FFade
	{
		bool bActive = false;
		float Value = 0.f;
		float Step = 0.f;
		int32 Target = 127;
		float Ms = 0.f;
		void Start(int32 Byte, int32 InTarget, float Seconds)
		{
			if (InTarget == Byte) { return; }
			Target = InTarget;
			Value = float(Byte);
			Step = float((InTarget - Byte) / 25) / Seconds;
			Ms = 0.f;
			bActive = true;
		}
		// 0x2254F8
		int32 Call()
		{
			Value += Step;
			if ((Step > 0.f && Value < float(Target)) || (Step < 0.f && float(Target) < Value))
			{
				return int32(Value);
			}
			bActive = false;
			return Target;
		}
		bool Tick(float Dt, int32& InOutByte)
		{
			bool bChanged = false;
			Ms += Dt * 1000.f;
			while (bActive && Ms >= PeriodMs)
			{
				Ms -= PeriodMs;
				const int32 B = Call();
				bChanged |= B != InOutByte;
				InOutByte = B;
			}
			return bChanged;
		}
	};
}
class USSXPathfinderWave;
class USSXSegmentQueueWave;
class FSSXPathfinderPlayer;
class FSSXSegmentQueuePlayer;
class FSSXCrowdVoices;
struct FSSXPathfinderSong;
struct FSSXPcm;
struct FRiderState;

UCLASS()
class SSXTRICKYREBUILD_API USSXAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static USSXAudioSubsystem* Get(const UObject* WorldContext);

	/** Front-end song from its event-0 loop: MUSIC.INF [QuadMenu] (femenuquad) when the speaker setting is 3, else [Menu] (ssxmenu); Event 0..5 picks a theme. */
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void PlayMenuMusic(int32 Event = 0);
	/** Switch the menu song to another of its 6 themes (bar-synced); remembered as the FE music state (snd+0x3714). */
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void PostMenuMusicEvent(int32 Event);
	/** Front end, once per frame: starts the menu song when bMenuActive turns on, stops it when it turns off (movies). */
	void UpdateFrontEndMusic(bool bMenuActive);
	bool IsMenuMusicActive() const { return bMenuMusic && !CurrentSong.IsEmpty(); }
	int32 PickTrackSong(const FString& Track);
	/** Speaker setting (0x32CA00 = snd+0x3838): 3 selects the QuadMenu song, 0 (mono) zeroes the 3D angles; a change rebuilds the sound system (vfunc_39 0x270C30) and restarts the FE song (0x20F430). */
	void SetSpeaker(int32 InSpeaker);
	int32 GetSpeaker() const { return Speaker; }
	int32 GetSpeakerRestarts() const { return SpeakerRestarts; }
	/** Big-air music duck (0x21B828): music stream volume byte (snd+0x36F0 -> +0x10), 127 = full. */
	void SetBigAirMusicByte(int32 Byte);
	int32 GetBigAirMusicByte() const { return BigAirMusicByte; }
	int32 GetFeMusicState() const { return FeMusicState; }

	/** Replay gate 0x21BFD0 (replay state 1..9) and the replay camera's target racer / pawn, set once per race frame. 0x20ED20 returns the first rider the replay camera targets (0x193CE0) during playback, else the first human (0x17F1F0). */
	void SetReplayPlayback(bool bPlaying, int32 TargetRacer, const UObject* TargetPawn)
	{
		bReplayPlayback = bPlaying;
		ReplayTargetRacer = bPlaying ? TargetRacer : INDEX_NONE;
		ReplayTargetPawn = bPlaying ? TargetPawn : nullptr;
	}
	bool IsReplayPlayback() const { return bReplayPlayback; }
	/** Set during a loaded replay's silent keyframe rebuild: board sounds start nothing. */
	void SetReplayRebuild(bool b) { bReplayRebuild = b; }
	bool IsReplayRebuild() const { return bReplayRebuild; }
	int32 GetReplayTargetRacer() const { return ReplayTargetRacer; }
	/** 0x20ED20 for a rider's own audio (0x217D50 / 0x2181E8): the listener is this pawn; outside playback the caller's human test applies (bHumanListener). */
	bool IsListenerPawn(const UObject* Pawn, bool bHumanListener) const
	{
		return bReplayPlayback ? (Pawn != nullptr && Pawn == ReplayTargetPawn.Get()) : bHumanListener;
	}

	/** Race load (FUN_002116D8): song picked, prepared (event 0, control 80) and paused; venue intro stream started; audio state 2. Mode 6 = trick tutorial (song plays, no intro). */
	bool BeginRace(const FString& Track, int32 TrackId, int32 GameMode, bool bRestart = false);
	/** Race load for a loaded replay (+0x4C): only audio state 6 and duck mode 4; no song pick, intro stream or random draws. */
	bool BeginLoadedReplayRace(const FString& Track, int32 TrackId);
	const FString& GetRaceSongStem() const { return RaceSongStem; }
	/** Current song title (snd+0x12E0 + 32 x snd+0x36E8, the MUSIC.INF section name, 0x224A98); empty before a race load. */
	FString GetRaceSongTitle() const;
	/** Pause options song row: Left = 0x225618 (previous allowed song, wrapping), Right = 0x225660, over the MUSICMAP mask snd+0x36EC, then 0x215220 (song player rebuilt: event 0, control 80). */
	void StepRaceSong(int32 Dir);
	// FUN_00225240 / 0x1AC580
	void PauseRaceSong();
	SSXRaceAudio::FFlow& GetRaceFlow() { return RaceFlow; }
	FSSXZoneAmbience& GetZoneAmbience() { return ZoneAmbience; }
	/** Sound manager's random value masked to 15 bits by its callers: vtable +0x24 = 0x21E6C0 calls the game PRNG stream A next() (0x23D4A8). */
	static int32 Rand15() { return int32(FGameRng::StreamA().Next() & 0x7FFFu); }
	static void SeedRaceRandom(int32 Seed) { FGameRng::StreamA().Seed(uint32(Seed)); }
	void ExecuteRaceCommands();
	int32 GetIntroQueueLength() const;
	const TArray<FString>& GetIntroLog() const { return IntroLog; }
	const TArray<SSXRaceAudio::FCommand>& GetRaceCommandLog() const { return RaceCommandLog; }
	FString GetIntroBig() const { return IntroBig; }
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") bool PlaySong(const FString& Song, int32 StartEvent = 0);
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") int32 GetJukeboxCount();
	/** cFE3DSound entry 28 (0x270A60): the song's volume byte fades to 0 over 0.5 s; stream 2 opened at the JUKEBOX level class (5) on data/audio/jukebox.big. */
	void JukeboxOpen();
	/** Entry 29 (0x270A70): stream 2 stopped (0x21D020 -> 0x229FA0); song S < JUKEBOX.INF count with a non-empty name plays from jukebox.big (FUN_00229ED0). True when a song started. */
	bool JukeboxPlay(int32 Song);
	/** Entry 31 (0x270AC0 -> 0x21D028): stream 2 neither playing (0x229F60) nor pending (0x22A178). PC: false until a song was started. */
	bool JukeboxSongEnded() const;
	// 0x270AE8 / 0x229D40
	void JukeboxStop();
	/** Level class 5: master x 1.0 x [FELEVELSCALING] JUKEBOXLEVEL / 100 x duck gain (0x211DB0); no Options slider sets class 5. */
	float GetJukeboxGain() const;
	bool IsJukeboxOpen() const { return bJukeboxOpen; }
	int32 GetJukeboxSong() const { return JukeboxSong; }
	// FUN_00225468
	void TickMusicFade(float Dt);
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void StopMusic(float FadeSeconds = 1.f);
	void StopStreamVoices();
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void PostMusicEvent(int32 Event);
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") FString GetCurrentSongName() const { return CurrentSong; }
	bool IsPathfinderPlaying() const;
	TSharedPtr<FSSXPathfinderPlayer> GetPathfinderPlayer() const { return Player; }

	/** Race music is driven by the race flow (GetRaceFlow); this does nothing. */
	void UpdateRaceMusic(const FRiderState& S, float DeltaSeconds);
	/** Posts MusicEventForRiderEvent[Event] if mapped (nothing is mapped). */
	void OnRiderEvent(ESSXRiderAudioEvent Event, float Magnitude = 1.f);

	/** Load the race (MC, CHAR, NARR, ANIM) or FE speech databases from converted/audio/speech/<section>/headers.big; false when missing (speech skipped). */
	bool LoadSpeech(FSSXSpeechSystem::ESet Set);
	FSSXSpeechSystem& GetSpeech() { return Speech; }
	FSSXAnnouncer& GetAnnouncer() { return Announcer; }
	SSXRiderSpeech::FHelpers& GetRiderSpeech() { return RiderSpeech; }
	/** Race hooks: a racer's world location and the listener zone of a game position (0x20F9B0: first listener within 40 m, -1 none). Unset: the game position / zone 0. */
	TFunction<FVector(int32 Racer)> RacerLocation;
	TFunction<int32(const FVector& GamePos)> ListenerZone;
	struct FSpeechWatch
	{
		int32 Crash[6] = {}, Bump[6] = {}, Grudge[6] = {};
		int32 CrowdCue = 0, BoostEmpty = 0, Segment = 0, ForcedReset = 0;
		bool bInit = false;
		int32 GoFrame = 0;
		int32 LastBumpFrame = MIN_int32 / 2;   // 0x216960
	};
	FSpeechWatch SpeechWatch;
	void PlayZbxsfxAt(int32 Sound, const FVector& Pos, float Volume01 = 1.f);
	/** A channel player still has audio (0x228958 uses the stream end). TODO: the 500-unit end window. */
	bool IsSpeechPlaying(int32 Channel) const;
	// 0x228D10
	void StopSpeech(int32 Channel);
	/** FE Video state: cFE3DSound entry 33 (0x270AF8) stops channel 0, then pauses voices, song and streams (FUN_002153F8: snd+0x3830 = 1); entry 34 (0x270B08) resumes them only when snd+0x3830 is set. */
	void PauseForMovie();
	void ResumeAfterMovie();
	// FUN_002153F8 / FUN_00215488
	void PauseGameAudio();
	void ResumeGameAudio() { ResumeAfterMovie(); }
	bool IsMoviePaused() const { return bMoviePaused; }
	UAudioComponent* GetMusicComponent() const { return MusicComponent; }
	/** Sound manager vtable +0x34 (0x2213C8): removes every crowd entry (0x221330) and releases all 64 monitored bank voices (0x20C0F8); held board voices are restarted right after at the replay start (0x210A38). */
	void StopAllSounds();
	void AddBankVoice(UAudioComponent* C);
	int32 StopAllCalls = 0;
	/** Ambient emitter enable flag (snd2 +0x443C, 0x22E400) lives in the sound manager so it outlives a track; initial 1 (0x22D098). A loaded replay keeps this value. */
	bool bAmbientEnableCarry = true;
	int32 MoviePauses = 0, MovieResumes = 0;
	TArray<FString> SpeechLog;
	int32 SpeechMissing = 0;
	/** Duck (0x20EE58, 0x21D610, 0x211FB8, 0x212018): mode m (4 = none) and level; while speech stream m plays the gain fades to the level, back to 1 when it stops, each fade exactly 1.0 s (step = (target - start) / 60 per frame). Dt in seconds. */
	void TickDuck(int32 Mode, double Level, float Dt);
	float DuckFor(SSXAudioDuck::EBus Bus) const { return DuckFade.For(Bus); }

	/** 0x2355C8 (pre-race slot start with speech): when the ring is off, turn it on (20 entries, snd+0x7DD4) and stop channel 1. */
	void IntroSpeechBegin();
	/** 0x2356F0 -> 0x235BA8 (hold): ANIM {Value} (>= 1000) on channel 1 with its stream held; the line (-1 if nothing started) and a -2 separator enter the ring. */
	void IntroSpeechPost(int32 Value);
	// 0x110980
	void IntroSpeechEnd();
	bool IsIntroRingOn() const { return bIntroRing; }
	/** 0x235780 (rider animation event, snd+0x7DD0 on): >= 1000 -> during the intro held lines up to the next separator start at the rider, else ANIM {Value} at the rider; 0xD0 / 0xD1 -> zboard #80 / #81; < 0x80 -> bank slot 7. RiderPos in Unreal space. */
	void OnAnimEvent(int32 Value, int32 Racer, const FVector& RiderPos, bool bHuman);
	bool bAnimEventSounds = true;
	TArray<FString> AnimEventLog;
	/** Speaker position (0x228BD8 +0x350, pointer to rider +0x140) and range (+0x368; <= 0 -> 30 m): falloff = cull = range, ((R - d) / R)^2. Null = 2D (channel 0 / FE). */
	void SetSpeechPosition(int32 Channel, const FVector* Pos, int32 Racer = -1, double RangeM = 0.0);
	void FollowSpeakers();
	double GetSpeechRange(int32 Channel) const { return Channel >= 0 && Channel < 2 ? PlayingRange[Channel] : 0.0; }
	int32 GetSpeechRacer(int32 Channel) const { return Channel >= 0 && Channel < 2 ? PlayingRacer[Channel] : -1; }
	int32 GetSpeechQueueLength(int32 Channel) const;
	/** Per-frame speech update (0x228998): each free channel services its best pending request. */
	void UpdateSpeech();
	/** 0x228958 / 0x2282E0: the channel has no stream, or its stream is ending (fewer than 500 units left; units read as ms). */
	bool IsChannelFree(int32 Channel);
	/** Character-speech helper's post + service of channel 1 (0x228BD8) with the speaker position and voice tag +0x354 (the line's voice record +0x80). */
	void CharRequest(const SSXSpeech::FRequest& R, const FVector& SpeakerPos, int32 Tag, int32 Racer = -1);
	// 0x234BB8
	void StopChannel1IfTag(int32 Tag);
	void PlayZbxsfx(int32 Sound);
	/** Tweak started by the player with no uber available (0x1272E8 -> 0x218B78): zbxsfx #119 when snd+0x3844 (mirrors the Arcade SFX option 0x32CA04) is set. */
	void OnTweakStarted();
	int32 ArcadeSfx = 1;

	FSSXTrickyAudio& GetTricky() { return Tricky; }
	/** FUN_0021C610 inputs from the playing race song: MUSIC.INF BPM / LOOPDATA, the beat phase of the Pathfinder stream, snd+0x3700. */
	FSSXTrickyPhraseContext TrickyPhraseContext(int32 Mode);
	void ExecuteTrickyCommands(TFunctionRef<uint32()> RandA);
	void PlayPointsTick();
	int32 SeenTrickyFull = 0;
	int32 SeenTrickyFresh = 0;
	int32 SeenIntroMusicEnds = 0;

	// UI sound by name: "ui<id>" = FE play(u8 id) (PlayFeSound), "music<e>" = FE music event, else UISounds.
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void PlayUISound(FName Name);
	/** cFE3DSound entry 10 play(u8 id): zbxfe.bnk, single voices for #16 / #17, #50 gated by the FE music state. Returns the manifest id played (empty = nothing). */
	FString PlayFeSound(int32 Id);
	/** Screen whooshes (cFE3DSound entries 26 / 27): zbxfe #32 / #33 when the FE music state != 0, stopping the previous whoosh voice (snd+0x7438). */
	FString PlayFeWhoosh(bool bEnter);
	FString PlayFeStinger(int32 Sound);
	FString PlayRaceUISound(SSXUISound::ERace Kind);
	/** FE rider reaction line: sound Value of speech/charfe/feselect<Variant>.bnk, level class 3. TODO: played 2D; the game plays it at the rider's position. */
	FString PlayRiderSpeech(int32 Variant, int32 Value);
	TArray<FString> UISoundLog;
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") UAudioComponent* PlaySound2D(const FString& Id, float Volume = 1.f, float Pitch = 1.f);
	USoundBase* LoadSound(const FString& Id);
	SSXAudioDiag::EResult MissReason(const FString& Id);
	// 0x20AAB8 / 0x20B688
	USoundAttenuation* GameFalloff(double RadiusM);
	/** Central one-shot: resolve, spawn (Pos null = 2D, else 3D with GameFalloff(RadiusM)) and note to SSXAudioDiag under System. bUI sounds keep playing while paused; game sounds pause with the game (0x2153F8). */
	UAudioComponent* PlayGameSound(const TCHAR* System, const FString& Id, float Gain, const FVector* Pos = nullptr, double RadiusM = 30.0,
		float Pitch = 1.f, bool bUI = false);
	// 0x2153F8
	static void MakeGameSound(UAudioComponent* C);
	/** The crowd (CROWD.INF reactions, Crowd.bnk streams, CHANT.INF banks) and its voices; begun by the track effects at track load. */
	FSSXCrowdVoices& GetCrowdVoices();

	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void SetMasterVolume(float V);
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void SetMusicVolume(float V);
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void SetSfxVolume(float V);
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void SetSpeechVolume(float V);
	UFUNCTION(BlueprintCallable, Category = "SSX|Audio") void SetCharSpeechVolume(float V);
	float GetMasterVolume() const { return MasterVolume; }
	/** EA library master every voice is levelled by (0x2D2F90 byte int(level x 127) / 127, law 0x2D5148); applies to every sound; 0 at master 0. */
	float GetMasterGain() const;
	float GetIntroGain() const;
	float GetMusicVolume() const { return MusicVolume; }
	/** Level class 1 (MUSIC) of the last bank load (FUN_0021CA20): master x music x AUDIO.INF MUSICLEVEL of [FELEVELSCALING] (FE banks, 0x20F160) or [IGLEVELSCALING] (race banks, 0x211098). */
	float MusicClassLevel(bool bFrontEndTable) const;
	float GetSfxVolume() const { return SfxVolume; }
	float GetSpeechVolume() const { return SpeechVolume; }
	float GetCharSpeechVolume() const { return CharSpeechVolume; }
	// Gain the music component plays at (master x music x song level x big-air byte x duck).
	float GetMusicGain() const;
	float GetSpeechGain(int32 Channel) const { return SpeechGain(Channel); }
	float GetSfxGain() const;

	const FSSXAudioManifest& GetManifest();

	/** Pathfinder event posted when a song starts; event 0's action is "immediately go to the first node of section 1" in every song. */
	int32 StartEvent = 0;
	/** UI name -> manifest id: race overlay sounds, the countdown beep, FE screen whooshes. FE menu ids go through PlayFeSound ("ui<id>"). */
	TMap<FName, FString> UISounds;
	/** Rider event -> Pathfinder event; empty as in the original (race events 1..10 come from the track triggers, 0x11A348). */
	TMap<ESSXRiderAudioEvent, int32> MusicEventForRiderEvent;

private:
	TSharedPtr<FSSXCrowdVoices> CrowdVoices;
	bool StartPathfinder(const FString& Stem, int32 Event, float Gain, int32 InControl = 0, bool bPaused = false);
	FString SongStemByIndex(int32 Index, float* OutPathLevel) const;
	FString TrackKey(const FSSXInfFile& Inf, const FString& Track) const;
	bool StartIntroStream(const FString& Track);
	void QueueIntroSegment(int32 Segment);
	UAudioComponent* PlayOneShot(const FString& Id, float Gain, const TCHAR* System = TEXT("zbxsfx"), bool bUI = false);
	UPROPERTY(Transient) TMap<int32, TObjectPtr<USoundAttenuation>> FalloffCache;
	bool StartSoundWave(const FString& Id, float Gain, bool bLoop);
	void ApplyMusicVolume();
	void LoadConfigs();
	UWorld* GetAudioWorld() const;
	FString ResolveSongStem(const FString& SongKey, float* OutPathLevel = nullptr) const;

	UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
	UPROPERTY(Transient) TObjectPtr<USSXPathfinderWave> MusicWave;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<USoundBase>> SoundCache;

	TSharedPtr<FSSXPathfinderPlayer> Player;
	bool bMoviePaused = false;
	TMap<FString, TSharedPtr<const FSSXPathfinderSong>> SongCache;
	TSharedPtr<const FSSXPcm> CurrentPcm;
	FString CurrentPcmStem;
	FString CurrentSong;
	float CurrentSongGain = 1.f;
	FString RaceSongStem;
	float RaceSongGain = 1.f;
	int32 RaceSongIndex = -1;
	uint32 RaceSongMask = 0;
	bool bMenuMusic = false;
	TWeakObjectPtr<const UWorld> FrontEndMusicWorld;
	bool bFrontEndMusicOn = false;

	FSSXAudioManifest Manifest;
	bool bManifestTried = false;
	FSSXInfFile MusicInf, MusicMapInf, JukeboxInf, AudioInf, IntroMusInf;
	bool bConfigsLoaded = false;

	float MasterVolume = 1.f;
	float MusicVolume = 1.f;
	float SfxVolume = 1.f;
	float SpeechVolume = 1.f;
	float CharSpeechVolume = 1.f;
	int32 SpeakerRestarts = 0;
	void ApplySpeechVolume();

	float Intensity = 0.f;

	int32 Speaker = 1;
	int32 BigAirMusicByte = 127;
	SSXMusicFade::FFade MusicFade;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> JukeboxComponent;
	bool bJukeboxOpen = false;
	bool bJukeboxStarted = false;
	int32 JukeboxSong = INDEX_NONE;
	void ApplyJukeboxVolume();
	bool bReplayPlayback = false;
	bool bReplayRebuild = false;
	int32 ReplayTargetRacer = INDEX_NONE;
	TWeakObjectPtr<const UObject> ReplayTargetPawn;
	int32 FeMusicState = 0;
	TWeakObjectPtr<UAudioComponent> FeVoice16, FeVoice17, FeWhooshVoice, FeStingerVoice, RaceUIVoice;
	TArray<TWeakObjectPtr<UAudioComponent>> BankVoices;

	SSXRaceAudio::FFlow RaceFlow;
	FSSXZoneAmbience ZoneAmbience;
	TArray<SSXRaceAudio::FCommand> RaceCommandLog;
	TSharedPtr<FSSXSegmentQueuePlayer> IntroPlayer;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> IntroComponent;
	UPROPERTY(Transient) TObjectPtr<USSXSegmentQueueWave> IntroWave;
	void PlaySpeechSegment(int32 Channel, const SSXSpeech::FSampleGroup& Group, const SSXSpeech::FResolvedSample& Smp, bool bQueue);
	void EnsureSpeechPlayers();
	float SpeechGain(int32 Channel) const;
	FSSXSpeechSystem Speech;
	FSSXAnnouncer Announcer;
	SSXRiderSpeech::FHelpers RiderSpeech;
	FSSXTrickyAudio Tricky;
	FString TrickyLoopStem;
	float TrickyLoopLevel = 1.f;
	TWeakObjectPtr<UAudioComponent> TrickyVoice, TickVoice, PhraseVoice;
	TSharedPtr<FSSXSegmentQueuePlayer> SpeechPlayers[2];
	UPROPERTY(Transient) TObjectPtr<USSXSegmentQueueWave> SpeechWaves[2];
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> SpeechComps[2];
	/** The game voices PauseGameAudio paused (0x20BFE8: every playing non-UI voice), resumed by ResumeAfterMovie. */
	TArray<TWeakObjectPtr<UAudioComponent>> PausedVoices;
	TWeakObjectPtr<UWorld> SpeechWorld;
	SSXAudioDuck::FFade DuckFade;
	float LoggedDuckTarget = 1.f;
	bool bLoggedSongPlaying = false;
	void NoteSongState();
	static constexpr int32 RingSize = 20;
	bool bIntroRing = false;
	int32 Ring[RingSize] = {};
	int32 RingWrite = 0, RingRead = 0;
	bool RingPush(int32 V);
	struct FHeldLine { TArray<SSXSpeech::FResolvedSample> Segments; int32 Value = 0; };
	TArray<FHeldLine> HeldLines;
	void PlaySpeechLineAt(const FHeldLine& Line, const FVector& Pos, int32 Racer);
	void PlaceSpeech(UAudioComponent* C, bool bPositioned, const FVector& Pos, double RangeM, float Gain);
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> ReleasedSpeech;
	TArray<TSharedPtr<FSSXSegmentQueuePlayer>> ReleasedPlayers;
	TArray<int32> ReleasedRacers;
	bool bSpeechPos[2] = { false, false };
	FVector SpeechPos[2];
	int32 SpeechRacer[2] = { -1, -1 };
	double SpeechRange[2] = { 0.0, 0.0 };
	int32 PlayingRacer[2] = { -1, -1 };
	double PlayingRange[2] = { 0.0, 0.0 };
	int32 SpeechTag[2] = { 0, 0 };
	int32 PendingTag[2] = { 0, 0 };
	float IntroBaseVolume = 1.f;
	void ApplyIntroVolume();
	TMap<int32, FString> IntroSegments;
	TArray<FString> IntroLog;
	FString IntroBig;
};
