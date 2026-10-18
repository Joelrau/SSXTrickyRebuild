#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/RaceCamera.h"
#include "Camera/ReplayCamera.h"
#include "Harness/FixedStepClock.h"
#include "Harness/UnrealRiderWorld.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Input/SSXInputSetup.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "SnowboarderPawn.generated.h"

class UCameraComponent;
class USSXBoardSoundComponent;
class USSXCharacterAppearance;
class USphereComponent;
namespace SSXAnim { class FRiderAnimDriver; }
class UStaticMeshComponent;

/** The player's rider, driven by the recovered simulation (FRiderSim) at a fixed 60 Hz. Input: Enhanced Input -> virtual PS2 pad (SSXInputSetup) -> BTNMAP mapper (PadLayout) -> control word per control state. Sim: FRiderSim on FPatchWorld (SimData/<track>.patches.bin, .rails.bin, .instcol.bin); all rider collision is the sim's (no Unreal collision on the track meshes). Render: interpolated between the last two sim states, converted with SSXSim. The visible rider is a placeholder built from engine primitives. */
UCLASS()
class SSXTRICKYREBUILD_API ASnowboarderPawn : public APawn
{
	GENERATED_BODY()

public:
	ASnowboarderPawn();
	virtual ~ASnowboarderPawn() override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;
	virtual FVector GetVelocity() const override { return RenderVelocity; }
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	void Restart();

	bool HasSim() const { return Sim != nullptr; }
	const FRiderState* GetSimState() const;
	double GetSpeedKmh() const;
	uint32 GetLastWord() const;
	int32 GetLastWallContacts() const;

	/** Player control layout = Options > Controller configuration: 0 "Default" = BTNMAP0 (spins, flips, late tricks and prewind spin / flip from the d-pad only; the left stick steers, crouches, brakes and turns in Prewind), 1 "Pro" = BTNMAP1 (the left stick also drives spins / flips / prewinds; no stick turn in Prewind). The game loads btnmap%d.dat with %d = rider_profile+0x6C. InitSim takes it from the front end's settings when a ride was started from the menus, else this value is used. TODO: the FE -> slot+4 copy outside World Circuit is not traced; settings Controller[0] is used. */
	UPROPERTY(EditAnywhere, Category = "Input") int32 PadLayout = 0;

	/** Rider character (0 Eddie .. 11 Marisol) and board class 0..2: select the grab slot table. */
	UPROPERTY(EditAnywhere, Category = "Rider") int32 Character = 10;
	UPROPERTY(EditAnywhere, Category = "Rider") int32 BoardClass = 2;
	/** Board slot 0..11 in the rider's board list. INDEX_NONE = the first board of BoardClass. With SimData/riders.bin the slot's board-table class replaces BoardClass. */
	UPROPERTY(EditAnywhere, Category = "Rider") int32 Board = INDEX_NONE;
	/** Outfit 0..6; INDEX_NONE keeps the imported textures. */
	UPROPERTY(EditAnywhere, Category = "Rider") int32 Outfit = INDEX_NONE;
	TOptional<SSXRider::FPlayerOptions> PlayerOptions;
	TOptional<int32> ComputerLevel;
	// 0x166870
	TOptional<SSXRider::FBuiltProfile> ProfileOverride;
	void ResolveBoardSelection();
	void ApplyRiderProfile(FRiderSim& InSim);

	/** Pad Start restarts at the spawn point (false when the game mode provides the pause menu). */
	UPROPERTY(EditAnywhere, Category = "Input") bool bStartRestarts = true;

	/** Rider model id (brodi, zoe, ... see Characters/SSXCharacterLibrary.h). Empty = follow Character.
	 *  The console variable ssx.Character overrides both. Falls back to the placeholder if not imported. */
	UPROPERTY(EditAnywhere, Category = "Rider") FString CharacterId;
	USSXCharacterAppearance* GetAppearance() const { return Appearance; }
	/** Board / wind / rail sounds and rider event one-shots, fed the sim state every 60 Hz step. */
	USSXBoardSoundComponent* GetBoardSound() const { return BoardSound; }

	/** Computer rider: input from the AI (race hooks), no camera, no pad. Set before BeginPlay. */
	UPROPERTY(EditAnywhere, Category = "Race") bool bAIRider = false;
	/** The race manager places the rider (start slots) and steps the sim; the pawn's own Tick only samples the pad. Set before BeginPlay (ASSXGameMode does when race data exists). */
	UPROPERTY(EditAnywhere, Category = "Race") bool bRaceDriven = false;
	void SetSharedWorld(TSharedPtr<FPatchWorld> InWorld) { SharedPatchWorld = MoveTemp(InWorld); }
	TSharedPtr<FPatchWorld> GetSharedPatchWorld() const { return PatchWorld; }
	FRiderSim* GetSim() const { return Sim.Get(); }
	void RaceStepFrame();
	void RaceSimFrame();
	void RacePostAI();
	void RacePostFrame();
	// 0x114730, 0x113A88, 0x1797A8, 0x179758
	void StepCameraReverse(bool bReplayFlag);
	void RaceRender(double Alpha, float DeltaSeconds);
	void ResetRenderState();
	void CaptureRenderPose();
	// 0x14B7C8, 0x14B8A8
	TFunction<bool(FSSXPadFrame& InOut)> LessonPadInject;
	TFunction<void()> LessonPadDone;
	// 0x1927D8
	TFunction<bool(uint32& OutWord)> ReplayPlayWord;
	TFunction<void(uint32 Word)> ReplayRecordWord;
	TFunction<ASnowboarderPawn*()> ReplayCameraSubject;
	// 0x1DC3E0
	TFunction<uint8()> RacePhaseForCamera;
	/** Automation only (SSX.Replay.InGameRerun): replaces the sampled pad before each 60 Hz race step. */
	TFunction<void(FSSXPadHardware& InOut)> TestPadScript;
	SSXAnim::FRiderAnimDriver* GetAnimatorForReplay() const;
	// FUN_0017A2F8
	void SetTutorialMapping(bool bTutorial) { Mapper.SetLayout(bTutorial ? 0 : PadLayout); }
	int32 GetMapperLayout() const { return Mapper.GetLayout(); }

	FSSXRaceCamera* GetRaceCamera() const { return RaceCamera.Get(); }
	// 0x3368B0, 0x1784D0
	FSSXReplayCamera* GetReplayCamera() const { return ReplayCamera.Get(); }
	// FUN_00173e70
	void CycleCameraView(int32 Step) { if (RaceCamera) { RaceCamera->CycleView(Step); } }
	bool GetCameraInput(FSSXCameraRiderInput& Out);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	bool InitSim();
	void SpawnRider();
	void StepSim(int32 Steps, const FSSXPadHardware& Pad);
	void FeedRumble();
	void LogShove(const FPadActions& Actions, const FSSXPadHardware& Hw);
	void ApplyRenderTransform(double Alpha);
	void UpdateCamera(double Alpha);
	void InitCamera();
	void ResetCamera();
	void StepCamera(bool bTeleported);
	void TrackTricks(float DeltaSeconds);
	void TickAutoTest(float DeltaSeconds);
	UStaticMeshComponent* AddRiderPart(const TCHAR* Name, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Scale, const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collider;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RiderRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> BodyRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USSXCharacterAppearance> Appearance;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USSXBoardSoundComponent> BoardSound;
	UPROPERTY(EditAnywhere, Category = "Rider") TObjectPtr<UMaterialInterface> RiderMaterial;
	UPROPERTY(Transient) FSSXInputBindings InputBindings;

	TSharedPtr<FPatchWorld> PatchWorld;
	TSharedPtr<FPatchWorld> SharedPatchWorld;
	FSSXPadHardware PendingPad;   // race mode: sampled in Tick, consumed by RaceStepFrame
	FSSXPadDevice PadDevice;   // the DualShock 2 pad object, updated once per 60 Hz frame
	TUniquePtr<FUnrealRiderWorld> CollisionWorld;
	TUniquePtr<FRiderSim> Sim;
	TUniquePtr<FRiderInputDriver> Driver;
	FPadMapper Mapper;
	int32 ShoveLogCount = 0;        // ssx.Shove.Log: Sim->State.ShoveCount last seen
	int32 ShoveLogAnim = 0;
	double ShoveLogHand[4] = {};
	double ShoveLogLastTime = -1e9;
	FString ShoveLogClip;
	bool bReverseHeld = false;
	bool bReversePressed = false;
	FSSXCameraReverseLatch ReverseLatch;
	FFixedStepClock Clock;

	FVector SpawnPos = FVector::ZeroVector;
	FVector SpawnHeading = FVector(0, 1, 0);

	FVector PrevPos = FVector::ZeroVector, CurPos = FVector::ZeroVector;
	bool bHiddenByResetFade = false;
	FQuat PrevRot = FQuat::Identity, CurRot = FQuat::Identity;
	FVector RenderVelocity = FVector::ZeroVector;
	int32 StepsLastFrame = 0;
	bool bLastStepTeleported = false;
	bool bRestartWasHeld = false;
	bool bStartWasHeld = false;

	FString TrackName;
	FString StatusText;
	FString StatesText;

	// in-race camera, stepped with the sim at 60 Hz and interpolated for rendering (game space)
	struct FCameraSample { FVector Pos = FVector::ZeroVector; double Yaw = 0.0, Pitch = 0.0, Roll = 0.0, Fov = 1.6, Near = 100.0, Flash = 0.0; };
	bool bCameraFadeApplied = false;
	TUniquePtr<FSSXCameraDefSet> CameraDefs;
	TUniquePtr<FSSXRaceCamera> RaceCamera;
	TUniquePtr<FSSXReplayCamera> ReplayCamera;
	FCameraSample CamPrev, CamCur;
	ASnowboarderPawn* LastCameraSubject = nullptr;

	ERiderMotion LastMotion = ERiderMotion::None;
	double AirSpinMax = 0.0, AirFlipMax = 0.0;
	int32 AirGrab = FRiderInputWord::NoTrick;
	FString TrickText;
	float TrickTextTime = 0.f;

	bool bAutoTest = false;
	float AutoTestClock = 0.f;
	float AutoTestNextLog = 0.f;
	bool bAutoTrick = false;
	uint32 AutoTrickButtons = 0;
	FVector2D AutoTrickStick = FVector2D::ZeroVector;
	ERiderMotion AutoTrickLastMotion = ERiderMotion::None;
	int32 AutoTrickLastFrame = -1;
	int32 AutoTestLastLanding = -1;
	void InjectAutoTrick(float DeltaSeconds);
};
