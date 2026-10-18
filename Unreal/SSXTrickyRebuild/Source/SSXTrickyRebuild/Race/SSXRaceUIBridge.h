#pragma once

#include "CoreMinimal.h"
#include "UI/SSXRaceUIHost.h"

class ASSXRaceManager;

class SSXTRICKYREBUILD_API FSSXRaceUIBridge : public ISSXRaceUIHost
{
public:
	explicit FSSXRaceUIBridge(ASSXRaceManager* InOwner) : Owner(InOwner) {}

	virtual int32 GetRacePhase() const override;
	virtual int32 GetEventMode() const override;
	virtual int32 GetTrackId() const override;
	virtual double GetCountdownTimer() const override;
	// 0x113520
	virtual int32 GetCountdownOpens() const override { return CountdownOpens; }
	int32 CountdownOpens = 0;
	// 0x11A7D0
	virtual int32 GetHudOpens() const override { return HudOpens; }
	int32 HudOpens = 0;
	virtual bool IsPostRaceReady() const override;
	bool bPostRaceChain = false;
	// 0x21BFD0
	bool bFirstChainOverlay = false;
	virtual void PostRaceNarration(int32 Kind, int32 Arg) override;
	virtual bool GetResults(TArray<FSSXRacerResult>& Out) const override;
	virtual bool GetEntrants(TArray<FSSXRacerResult>& Out) const override;
	virtual bool GetShowoffStats(int32 Player, FSSXShowoffStats& Out) const override;
	// 0x1B1278, 0x1B0998
	static void ResultsStats(const struct FRaceResultRow& Row, FSSXShowoffStats& Out);
	virtual bool ConsumeFinish(int32& OutPlayer, int32& OutResult, FString& OutValue) override;
	// 0x164FC0
	virtual void FinishEvent() override;
	virtual bool HasPendingHiScore() const override;
	virtual void GetUnlocks(TArray<FSSXUnlock>& Out) const override;
	virtual bool IsEliminated() const override;
	virtual bool IsCircuitBeaten() const override;
	// 0x163AB0, 0x16D0B0, 0x180B58
	virtual void RequestNextHeat() override;
	virtual void SetPaused(bool bPaused) override;
	virtual bool RequestReplay() override;
	virtual void StopReplay() override;
	virtual bool BuildReplayFile(TArray<uint8>& Out) override;
	virtual bool TakeLoadedReplayStart() override { const bool b = bLoadedReplayStart; bLoadedReplayStart = false; return b; }
	bool bLoadedReplayStart = false;
	virtual void SetReplayMenu(bool bOpen) override;
	virtual FSSXReplay* GetReplay() override;
	virtual int32 GetReplayPadRacer() const override;
	virtual int32 GetRacerCount() const override;
	virtual int32 GetReplayCameraMode() const override;
	virtual void ReplayCameraNudge(float Amount, int32 Op) override;
	virtual void ReplayNextCamera() override;
	virtual FString GetReplayTargetName() const override;
	virtual void ReplayAudio(bool bPause) override;
	virtual void ReplaySpeechStop() override;
	virtual void ReplayTargetAudio() override;
	virtual void RequestRestart() override;
	virtual void RequestExit(int32 Code) override;
	virtual void RequestNextVenue(int32 GameMode, int32 Track) override;
	virtual void CycleCameraView(int32 Player, int32 Step) override;
	virtual int32 GetCameraView(int32 Player) const override;
	// 0x106A90
	virtual float GetRiderFade(int32 Player) const override;
	virtual FString GetSongName() const override;
	virtual void StepSong(int32 Dir) override;
	virtual void PauseMenuAudio(bool bPause) override;
	virtual void PauseSong() override;
	virtual bool GetTrickInput(int32 Player, int32& PadLayout, int32& BoardClass, bool& bFlipSpin) const override;
	virtual int32 GetIntroCards() const override { return IntroCards; }
	virtual void PressIntroSkip() override { ++SkipPresses; }
	virtual const SSXLesson::FLessonMan* GetLesson() const override;
	virtual void LessonPad(uint32 Bits) override;
	int32 IntroCards = 0;
	int32 SkipPresses = 0;

	// 0x11DA40
	struct FFinish { int32 Player = 0; int32 Result = 0; FString Value; };
	TArray<FFinish> PendingFinish;

private:
	ASSXRaceManager* Owner = nullptr;
};
