#pragma once

#include "CoreMinimal.h"
#include "Progression/SSXProgression.h"

class UWorld;
class FSSXReplay;
namespace SSXLesson { class FLessonMan; }

struct SSXTRICKYREBUILD_API FSSXRacerResult
{
	FString Name;
	int32 Char = 0;          // 0x331EA8
	int32 Player = -1;
	int32 Place = 0;
	double TimeSec = -1.0;
	int32 Score = 0;
	/** Aggression bar of an AI row (FUN_001A91C8 on the results, FUN_001A8FA0 on the pre-race card): the racer's level
	 *  toward human 1 (results: racer table +0x6C8 now / +0x6D0 the value it moves to; pre-race: racer byte +0x1C) and
	 * toward human 2 (+0x6CC / +0x6D4). */
	int32 Aggression = 0;
	int32 AggressionTo = 0;
	int32 Aggression2 = 0;
	int32 AggressionTo2 = 0;
	int32 Medal = -1;
};

/** Results stats: Showoff rows score 3524, 180s 502, flips 507, grabs 508, rails 510, biggest air 506 [FUN_001B0998];
 *  Freeride rows style points 3558, average speed 504, tricks landed 514, wipeouts 515, biggest air 506, air time 503,
 *  grab time 509, rail time 511, (blank), total time 513 [FUN_001B1278]; best trick 505 [FUN_001B2158]. */
struct SSXTRICKYREBUILD_API FSSXShowoffStats
{
	int32 Score = 0;
	int32 SpinDeg = 0;
	int32 FlipDeg = 0;
	int32 Grabs = 0;
	int32 Rails = 0;
	int32 StylePoints = 0;
	double AvgSpeed = 0.0;
	int32 TricksLanded = 0;
	int32 Wipeouts = 0;
	double BiggestAir = 0.0;
	double AirTime = 0.0;
	double GrabTime = 0.0;
	double RailTime = 0.0;
	double TotalTime = 0.0;
	FString BestTrick;
};

class SSXTRICKYREBUILD_API ISSXRaceUIHost
{
public:
	virtual ~ISSXRaceUIHost() = default;

	virtual int32 GetRacePhase() const = 0;
	virtual int32 GetGameSubState() const { return 0; }
	virtual int32 GetEventMode() const = 0;
	virtual int32 GetTrackId() const = 0;
	virtual int32 GetNumHumanPlayers() const { return 1; }
	virtual double GetCountdownTimer() const = 0;
	/** Countdown overlay opens: the count of cCountdownHandler enters that opened slot 4 (0x113520 with a countdown). A replay restore puts phase 3 back without the enter, so no digits / GO in a replay. -1 = the host does not count them (the overlay then opens on phase 3 with a negative clock). */
	virtual int32 GetCountdownOpens() const { return -1; }
	/** HUD slot 0 opens: the count of live countdown enters (0x113520 -> per racer 0x11A7D0 opens overlay 0, with or without a countdown). A replay restore puts the phases back without the enter, so it never reopens the HUD. -1 = the host does not count them (the slot then opens on a phase change into 3). */
	virtual int32 GetHudOpens() const { return -1; }
	virtual bool IsPostRaceReady() const = 0;
	virtual bool IsResultsMenuHidden() const { return false; }
	virtual bool IsLesson() const { return GetLesson() != nullptr; }
	virtual const SSXLesson::FLessonMan* GetLesson() const { return nullptr; }
	// FUN_0014D6E0
	virtual void LessonPad(uint32 Bits) {}
	virtual float GetRiderFade(int32 Player) const { return 0.f; }
	// 0x112920
	virtual bool IsAsyncLoading() const { return false; }
	// 0x112AF0 / 0x164840
	virtual bool WantsHelpRace() const { return false; }
	/** Cards opened by the pre-race intro handler: bit 0 HelpRace (18), bit 1 NextRace (3); -1 = the
	 *  host does not drive them (the overlay manager's own rule applies). */
	virtual int32 GetIntroCards() const { return -1; }
	// 0x110AB8
	virtual void PressIntroSkip() {}

	virtual bool GetResults(TArray<FSSXRacerResult>& Out) const = 0;
	// FUN_001B4250
	virtual bool GetEntrants(TArray<FSSXRacerResult>& Out) const { return GetResults(Out); }
	virtual bool GetShowoffStats(int32 Player, FSSXShowoffStats& Out) const { return false; }
	/** Rider finish event for PopFinish (0x11DA40): result = the 0-based place (race modes; < 0: the rider's finish
	 *  flag +0x418 is 0, TIME UP); text = the race time string or the "%d" score. */
	virtual bool ConsumeFinish(int32& OutPlayer, int32& OutResult, FString& OutValue) { return false; }
	/** End of the event 0x164FC0 (records, WC bests, medal, rank points, unlock list, point sessions)
	 *; the end-race handler calls it at its enter (0x113E10). */
	virtual void FinishEvent() {}
	/** Race-end narration of the post-race overlays: Kind 0 RaceResults open (0x231C50), 1 HiScoreRecord row Arg of the new
	 *  record (0x232240), 2 Unlocked open (0x232330); each runs 0x231548 first. 3 PointDistribution activate with the rank-up flag (NARR 0x8010, Arg =
	 *  the rank 0x164840; 0x2314E0), 4 PointDistribution deactivate (channel 0 stopped, 0x1BA2F0). */
	virtual void PostRaceNarration(int32 Kind, int32 Arg) {}
	// 0x335400
	virtual bool HasPendingHiScore() const { return false; }
	virtual void GetUnlocks(TArray<FSSXUnlock>& Out) const {}
	virtual bool IsEliminated() const { return false; }
	// 0x1618E0
	virtual bool IsCircuitBeaten() const { return false; }

	virtual void SetPaused(bool bPaused) = 0;
	// 0x180A10 / 0x14B638
	virtual void RequestRestart() = 0;
	/** Results item 1 replay -> RH state 0xC -> hide 1, show 9: 0x1EF990(.., 1) (the camera to the focus rider, 0x191B60 unless
	 *  loaded). True when a replay plays; the replay overlay (slot 9) and its control (slot 5) run while it does. */
	virtual bool RequestReplay() { return false; }
	virtual FSSXReplay* GetReplay() { return nullptr; }
	// 0x1EC548
	virtual int32 GetReplayPadRacer() const { return 0; }
	virtual int32 GetRacerCount() const { return 1; }
	// 0x1786C0 / 0x178BE8 / 0x178680
	virtual int32 GetReplayCameraMode() const { return 0; }
	// 0x178968 / 0x32DB74
	virtual FString GetReplayTargetName() const { return FString(); }
	virtual void ReplayCameraNudge(float Amount, int32 Op) {}
	virtual void ReplayNextCamera() {}
	// 0x2153F8 / 0x215488 / 0x228D10
	virtual void ReplayAudio(bool bPause) {}
	virtual void ReplaySpeechStop() {}
	// 0x1EBFC0 / 0x22D370 / 0x215370
	virtual void ReplayTargetAudio() {}
	virtual void StopReplay() {}
	/** Replay menu "save" (391): the memory-card replay file of the race's replay (Race/SSXReplayFile.h; the original
	 *  streams it from the replay object, cReplayOverlay vfunc_04 / 05). False = none. */
	virtual bool BuildReplayFile(TArray<uint8>& Out) { return false; }
	/** cGame::vfunc_05 with a loaded replay (+0x4C): the replay overlay is shown in state 1 with no return slot; true once
	 *  when it is due (the port: after the keyframe rebuild). */
	virtual bool TakeLoadedReplayStart() { return false; }
	/** cReplayOverlay 0x1EF990 state 2 (true: ReplayControl +0x49C busy, game audio paused 0x2153F8) / state 3 (false:
	 *  not busy, audio resumed 0x215488). */
	virtual void SetReplayMenu(bool bOpen) {}
	// 0x163AB0
	virtual void RequestNextHeat() {}
	virtual void RequestExit(int32 Code) = 0;
	virtual void RequestNextVenue(int32 GameMode, int32 Track) = 0;
	// FUN_00173e70
	virtual void CycleCameraView(int32 Player, int32 Step) {}
	virtual int32 GetCameraView(int32 Player) const { return -1; }
	virtual FString GetSongName() const { return FString(); }
	// 0x225618 / 0x225660 / 0x215220
	virtual void StepSong(int32 Dir) {}
	/** Pause menu submenu stack (0x1AC410 / 0x1AC580): the first push pauses the game audio (0x2153F8) and plays zbxsfx
	 *  #126 (0x215458); the pop of a lower level pauses the song (0x225240); the last pop resumes the audio (0x215488)
	 *  unless the race is being left. */
	virtual void PauseMenuAudio(bool bPause) {}
	virtual void PauseSong() {}
	/** The player's controller for the trick button strings (rider_profile +0x6C pad layout, +0x44 board class, the spin
	 *  flip of rider +0x464 -> +0x40). False = defaults (layout 0, class 0, no flip). */
	virtual bool GetTrickInput(int32 Player, int32& PadLayout, int32& BoardClass, bool& bFlipSpin) const { return false; }
};

/** Fixed-state host for tests and -SSXMenuShots (no race logic). Not used by gameplay. */
class SSXTRICKYREBUILD_API FSSXRaceUIHostStub : public ISSXRaceUIHost
{
public:
	int32 Phase = 4, Mode = 2, Track = 11, Players = 1;
	double Countdown = 0.0;
	bool bPostRaceReady = false, bPaused = false;
	TArray<FSSXRacerResult> Results;
	TArray<FSSXUnlock> Unlocks;
	int32 Restarts = 0, ExitCode = -1, NextVenueMode = -1, NextVenueTrack = -1, NextHeats = 0;
	int32 CameraView[2] = { 1, 1 };
	TArray<int32> CameraSteps;

	virtual int32 GetRacePhase() const override { return Phase; }
	virtual int32 GetEventMode() const override { return Mode; }
	virtual int32 GetTrackId() const override { return Track; }
	virtual int32 GetNumHumanPlayers() const override { return Players; }
	virtual double GetCountdownTimer() const override { return Countdown; }
	int32 HudOpens = -1;
	virtual int32 GetHudOpens() const override { return HudOpens; }
	virtual bool IsPostRaceReady() const override { return bPostRaceReady; }
	virtual bool GetResults(TArray<FSSXRacerResult>& Out) const override { Out = Results; return Results.Num() > 0; }
	virtual void GetUnlocks(TArray<FSSXUnlock>& Out) const override { Out = Unlocks; }
	virtual void SetPaused(bool b) override { bPaused = b; }
	virtual void RequestRestart() override { ++Restarts; }
	virtual void RequestNextHeat() override { ++NextHeats; }
	virtual void RequestExit(int32 Code) override { ExitCode = Code; }
	virtual void RequestNextVenue(int32 M, int32 T) override { NextVenueMode = M; NextVenueTrack = T; }
	virtual void CycleCameraView(int32 P, int32 Step) override { CameraSteps.Add(P * 100 + Step); CameraView[P & 1] = ((CameraView[P & 1] + Step) % 4 + 4) % 4; }
	virtual int32 GetCameraView(int32 P) const override { return CameraView[P & 1]; }
	FString Song;
	virtual FString GetSongName() const override { return Song; }
	TArray<int32> SongSteps;
	int32 MenuAudioPauses = 0, MenuAudioResumes = 0, SongPauses = 0;
	virtual void StepSong(int32 Dir) override { SongSteps.Add(Dir); }
	virtual void PauseMenuAudio(bool bPause) override { ++(bPause ? MenuAudioPauses : MenuAudioResumes); }
	virtual void PauseSong() override { ++SongPauses; }
	float RiderFade = 0.f;
	virtual float GetRiderFade(int32 P) const override { return P == 0 ? RiderFade : 0.f; }
	TArray<FSSXShowoffStats> Stats;
	virtual bool GetShowoffStats(int32 P, FSSXShowoffStats& Out) const override { if (!Stats.IsValidIndex(P)) return false; Out = Stats[P]; return true; }
	bool bEliminated = false, bCircuitBeaten = false, bHiScore = false, bCanReplay = false;
	int32 ReplayStops = 0;
	virtual bool IsEliminated() const override { return bEliminated; }
	virtual bool IsCircuitBeaten() const override { return bCircuitBeaten; }
	virtual bool HasPendingHiScore() const override { return bHiScore; }
	virtual bool RequestReplay() override;
	FSSXReplay* ReplayObj = nullptr;
	virtual FSSXReplay* GetReplay() override { return ReplayObj; }
	FString ReplayTarget;
	virtual FString GetReplayTargetName() const override { return ReplayTarget; }
	int32 ReplayAudioPauses = 0, ReplayAudioResumes = 0;
	virtual void ReplayAudio(bool bPause) override { ++(bPause ? ReplayAudioPauses : ReplayAudioResumes); }
	int32 ReplayTargetAudios = 0;
	int32 RacerCount = 1;
	virtual int32 GetRacerCount() const override { return RacerCount; }
	virtual void ReplayTargetAudio() override { ++ReplayTargetAudios; }
	virtual void StopReplay() override { ++ReplayStops; }
	TArray<uint8> ReplayFileBytes;
	virtual bool BuildReplayFile(TArray<uint8>& Out) override { Out = ReplayFileBytes; return Out.Num() > 0; }
	bool bLoadedReplayStart = false;
	virtual bool TakeLoadedReplayStart() override { const bool b = bLoadedReplayStart; bLoadedReplayStart = false; return b; }
	int32 ReplayMenuOpens = 0, ReplayMenuCloses = 0;
	virtual void SetReplayMenu(bool bOpen) override { ++(bOpen ? ReplayMenuOpens : ReplayMenuCloses); }
	TArray<FSSXRacerResult> Entrants;
	virtual bool GetEntrants(TArray<FSSXRacerResult>& Out) const override { Out = Entrants.Num() ? Entrants : Results; return Out.Num() > 0; }
	const SSXLesson::FLessonMan* Lesson = nullptr;
	uint32 LessonBits = 0;
	int32 TrickLayout = 0;
	virtual const SSXLesson::FLessonMan* GetLesson() const override { return Lesson; }
	virtual void LessonPad(uint32 Bits) override { LessonBits |= Bits; }
	virtual bool GetTrickInput(int32 P, int32& PadLayout, int32& BoardClass, bool& bFlipSpin) const override
	{
		PadLayout = TrickLayout; BoardClass = 0; bFlipSpin = false; return true;
	}
	TArray<int32> Finishes;
	TArray<FString> FinishValues;
	virtual bool ConsumeFinish(int32& OutPlayer, int32& OutResult, FString& OutValue) override
	{
		if (Finishes.Num() < 2) return false;
		OutPlayer = Finishes[0]; OutResult = Finishes[1]; Finishes.RemoveAt(0, 2);
		OutValue = FinishValues.Num() ? FinishValues[0] : FString();
		if (FinishValues.Num()) FinishValues.RemoveAt(0);
		return true;
	}
};

namespace SSXRaceUI
{
	SSXTRICKYREBUILD_API void SetHost(const UWorld* World, ISSXRaceUIHost* Host);
	SSXTRICKYREBUILD_API ISSXRaceUIHost* GetHost(const UWorld* World);
}
