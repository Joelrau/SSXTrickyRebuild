#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Harness/FixedStepClock.h"
#include "Race/IntroDialogue.h"
#include "Race/LessonMan.h"
#include "Race/RaceSession.h"
#include "Race/SSXReplay.h"
#include "Race/SSXReplayKeyframe.h"
#include "Race/SSXReplayFile.h"
#include "Effects/SSXTrackEffects.h"
#include "Particles/SSXParticleWorld.h"
#include "World/SSXPropAnimation.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXRaceUIBridge.h"
#include "SSXRaceManager.generated.h"

class ASnowboarderPawn;
class ASSXRaceManager;

class SSXTRICKYREBUILD_API FSSXRaceReplayWorld : public SSXReplay::IReplayWorld
{
public:
	explicit FSSXRaceReplayWorld(ASSXRaceManager* InOwner) : Owner(InOwner) {}
	virtual void CaptureKeyframe(int32 Key, uint32 Frame, bool bDropScripts) override;
	virtual void RestoreKeyframe(int32 Key, uint32 Frame) override;
	virtual void ResetRiderEffects() override;
	virtual SSXReplay::FCamera GetCamera() const override;
	virtual void ApplyCamera(const SSXReplay::FCamera& Cam, bool bBlend) override;
	virtual void SetCameraRider(bool bReplay, int32 Rider) override;
	virtual void SetCameraTarget(int32 Rider) override;
	virtual void SetCameraMode(int32 Mode) override;
	virtual void StartCameraFlash() override;
	virtual int32 FocusPlayer() const override;
	virtual void Periodic60() override;
	virtual void Periodic10() override;
	virtual void FileSnapshot(int32 Key, TArray<uint8>& Out) const override;
	virtual void RebuiltKeyframe(int32 Key, uint32 Frame, const TArray<uint8>& Original) override;
	TArray<SSXReplayFile::FSnapshotCheck> SnapshotChecks;
	int32 PortStreamBEqual = 0, PortStreamBDiffers = 0;
	uint32 PortFirstDiffer = MAX_uint32;

	struct FKey
	{
		FSSXRaceKeyframe Race;
		USSXTrackEffects::FReplayKeyframe Fx;
		USSXPropAnimation::FReplayKeyframe Props;
		USSXParticleWorld::FReplayKeyframe Particles;
		TArray<TOptional<SSXAnim::FRiderAnimDriver>> Anim;
	};
	TArray<FKey> Keys;
	int32 Captures = 0, Restores = 0;
	// 0x178968, 0x1786C0, 0x1785C0
	SSXReplay::FCamera Camera;
	bool bReplayCamera = false;

private:
	ASSXRaceManager* Owner = nullptr;
};

namespace SSXRaceGate
{
	// 0x17C6E8, 0x180D08, FUN_002116D8
	enum class EFrame : uint8 { Setup, Hold, Start, Run };
	inline EFrame Next(bool bReady, bool bLoaderUp, bool bStarted)
	{
		return !bReady ? EFrame::Setup : (bLoaderUp ? EFrame::Hold : (bStarted ? EFrame::Run : EFrame::Start));
	}
}

UCLASS()
class SSXTRICKYREBUILD_API ASSXRaceManager : public AActor
{
	GENERATED_BODY()

public:
	ASSXRaceManager();

	virtual void Tick(float DeltaSeconds) override;

	static ASSXRaceManager* Find(const UObject* WorldContext);

	const FRaceSession* GetSession() const { return bReady ? &Session : nullptr; }
	int32 GetRacerIndex(const ASnowboarderPawn* Pawn) const { return Pawns.IndexOfByKey(Pawn); }
	int32 GetHumanPlayer(int32 Racer) const;
	int32 GetPlayerRacer(int32 Player) const;
	const FRaceTrackData& GetTrackData() const { return TrackData; }
	ASnowboarderPawn* GetPawn(int32 Racer) const { return Pawns.IsValidIndex(Racer) ? Pawns[Racer].Get() : nullptr; }
	ASnowboarderPawn* GetPlayerPawn() const;
	// 0x180A10
	void RestartRace();
	FSSXRaceUIBridge& GetUIHost() { return UIHost; }
	TArray<int32> HeatRiders;
	static SSXIntroDialogue::FSlot RaceStateSlots[SSXIntroDialogue::NumSlots];
	const SSXIntroDialogue::FScenePlayer& GetScene() const { return Scene; }
	const SSXLesson::FLessonMan* GetLesson() const { return bLesson ? &Lesson : nullptr; }
	// FUN_0014D6E0
	void LessonPad(uint32 Bits) { if (bLesson) { Lesson.AddPad(Bits); } }
	const FSSXReplay& GetReplay() const { return Replay; }
	FSSXReplay& GetReplayMutable() { return Replay; }
	const TArray<SSXReplayFile::FSnapshotCheck>& GetSnapshotChecks() const;
	const FSSXRaceReplayWorld& GetReplayWorld() const { return ReplayWorld; }
	// 0x1EF990, 0x191B60
	bool StartResultsReplay();
	// 0x191BE0
	void StopResultsReplay();
	int32 GetViewRacer() const;
	// 0x3368B0
	class FSSXReplayCamera* GetReplayCamera() const;
	// 0x210A38, 0x193C68, 0x215288
	void RestartBoardVoices();
	// 0x229FA0, 0x2153F8, 0x215488, 0x225280
	void ReplayStartAudio();
	void ReplayExitAudio();
	bool BuildReplayFile(TArray<uint8>& Out) const;
	bool IsLoadedReplayRace() const { return bLoadedReplayRace; }
	static constexpr int32 ReplayRebuildStepsPerTick = 600;
	/** Automation only (SSX.Replay.InGameRerun): called after every world-running game frame. */
	TFunction<void(ASSXRaceManager&)> TestOnFrame;
	/** Automation only (SSX.Replay.InGameLookSwitch): called before a live race takes its seeds from stream A. */
	static TFunction<void()> TestBeforeSeed;

	/** Event mode: the FE launch's DAT_0032DE8C (BeginPlay); this default (World Circuit race) only without an FE selection (tests). */
	UPROPERTY(EditAnywhere, Category = "Race") int32 EventMode = int32(ERaceEventMode::CircuitRace);
	/** Difficulty 0..2. Default 0. */
	UPROPERTY(EditAnywhere, Category = "Race") int32 Difficulty = 0;
	/** Racers in the race, 1..6 (the original races six). */
	UPROPERTY(EditAnywhere, Category = "Race") int32 NumRacers = 6;
	/** Racer index of the player without an FE selection (4); with one, the heat / single-event grid decides it (0x16D300 / 0x165CF0). */
	UPROPERTY(EditAnywhere, Category = "Race") int32 PlayerRacerIndex = 4;
	/** AI opponents (character index), in racer order, without an FE selection (Brodi, Eddie, Psymon, Marisol, JP); with one, the grid of 0x16D300 / 0x165CF0. */
	UPROPERTY(EditAnywhere, Category = "Race") TArray<int32> AICharacters;
	/** Optional board slot per AI (0..11). Empty / negative entries are drawn as the computer-rider builder does (0x16AE68): outfit = rand() % 5, then board = rand() % 12 of the rider's own list. */
	UPROPERTY(EditAnywhere, Category = "Race") TArray<int32> AIBoards;

	// 0x16AE68
	static void DrawComputerBoard(struct FGameRng& Rng, int32& OutBoard, int32& OutOutfit);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	bool Setup();
	// 0x180D08, FUN_002116D8
	void GameStart();
	bool bRaceStarted = false;
	int32 StartMode = 0, StartHeatWord = 0, StartPlayerChar = -1;

	FRaceTrackData TrackData;
	FRaceSession Session;
	FSSXReplay Replay;
	FSSXRaceReplayWorld ReplayWorld{ this };
	SSXReplayFile::FSetup ReplaySetup;
	bool bLoadedReplayRace = false;
	bool bLoadedReplayStarted = false;
	friend class FSSXRaceReplayWorld;
	// 0x111738, 0x191230
	bool bRaceEndRoutine = false;
	// 0x191298, 0x18F020
	bool ReplayFrame();
	void BindReplayInput();
	FFixedStepClock Clock;
	UPROPERTY(Transient) TArray<TObjectPtr<ASnowboarderPawn>> Pawns;
	bool bReady = false;
	bool bFailed = false;
	FString StatusText;
	FSSXRaceUIBridge UIHost{ this };
	int32 LastPlacements = 0;
	void HandleCues();
	void CarryRivalries();
	void OnSceneOp(int32 Op, const FString& Name);
	SSXIntroDialogue::FObject IntroObject;
	SSXIntroDialogue::FScenePlayer Scene;
	int32 IntroFocus = -1;
	// 0x17E148
	SSXLesson::FLessonMan Lesson;
	bool bLesson = false;
	friend class FRaceLessonEnv;

};
