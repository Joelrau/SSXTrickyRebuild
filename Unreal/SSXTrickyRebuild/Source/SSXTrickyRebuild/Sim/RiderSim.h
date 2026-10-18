#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderState.h"
#include "Sim/RiderWorld.h"

class FRiderSim;
struct FSurfaceParams;
namespace SSXGround { struct FPropContactEvent; }
namespace SSXWipeOut { struct FTumbleBody; }
namespace SSXAnim { struct FRiderAnimChannel; }

struct FRiderStateBlob
{
	virtual ~FRiderStateBlob() = default;
};
template<typename T>
struct TRiderStateBlob final : FRiderStateBlob
{
	T Value;
	explicit TRiderStateBlob(const T& In) : Value(In) {}
};
namespace SSXRiderBlob
{
	template<typename T> TSharedPtr<FRiderStateBlob> Save(const T& V) { return MakeShared<TRiderStateBlob<T>>(V); }
	template<typename T> void Load(const FRiderStateBlob& B, T& Out) { Out = static_cast<const TRiderStateBlob<T>&>(B).Value; }
}

class IRiderMotionState
{
public:
	virtual ~IRiderMotionState() = default;
	virtual void Enter(FRiderSim& Sim, ERiderMotion Previous) {}
	virtual void Update(FRiderSim& Sim) {}
	virtual void Exit(FRiderSim& Sim, ERiderMotion Next) {}
	virtual FString DebugString() const { return FString(); }
	virtual TSharedPtr<FRiderStateBlob> SaveState() const { return nullptr; }
	virtual void LoadState(const FRiderStateBlob& Blob) {}
};

class IRiderControlState
{
public:
	virtual ~IRiderControlState() = default;
	virtual void Enter(FRiderSim& Sim, ERiderControl Previous) {}
	virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) {}
	virtual void Exit(FRiderSim& Sim, ERiderControl Next) {}
	virtual TSharedPtr<FRiderStateBlob> SaveState() const { return nullptr; }
	virtual void LoadState(const FRiderStateBlob& Blob) {}
};

struct FRiderSimKeyframe
{
	FRiderState State;
	int32 TakeOffCount = 0;
	TArray<TSharedPtr<FRiderStateBlob>> Motions;
	TArray<TSharedPtr<FRiderStateBlob>> Controls;
};

class IRiderRaceHooks
{
public:
	virtual ~IRiderRaceHooks() = default;
	virtual void TrackScan(FRiderSim& Sim) {}
	// 0x1384B8
	virtual bool BuildInput(FRiderSim& Sim, FRiderInputWord& InOut) { return false; }
	// 0x118F10
	virtual bool OnForcedReset(FRiderSim& Sim) { return true; }
	// 0x118C08, 0x119220, 0x119D30
	virtual bool PlaceRespawn(FRiderSim& Sim, FVector& OutPos, FVector& OutHeading, FVector& OutVelocity) { return false; }
	// 0x123FC8
	virtual void RiderCollision(FRiderSim& Sim) {}
	// 0x1033D0, 0x103660
	struct FRacerTable
	{
		int32 Self = -1;
		int32 NumRacers = 0;
		double Dist[8] = {};
		double Bearing[8] = {};
		bool bGrudge = false;
		int32 GrudgeRival = -1;
		int32 GrudgeKind = 0;
		int32 RaceFrame = -1;   // 0x11D4E8
	};
	virtual bool RacerTable(const FRiderSim& Sim, FRacerTable& Out) const { return false; }
	// 0x234F68
	virtual void OnTaunt(FRiderSim& Sim, int32 Rival) {}
	// 0x128150, 0x235298
	virtual void OnWinTaunt(FRiderSim& Sim, double Strength) {}
	// 0x126748, 0x11CAE8
	virtual void SphereSetRebuilt(FRiderSim& Sim, bool bNow) {}
	virtual void PoseFed(FRiderSim& Sim) {}
	// 0x1267A0
	virtual void SphereSetMoved(FRiderSim& Sim, const FVector& D) {}
};

struct FRiderTransition
{
	const TCHAR* Id;
	const TCHAR* Context;
	bool bSetsMotion;
	ERiderMotion Motion;
	bool bSetsControl;
	ERiderControl Control;
	const TCHAR* Condition;
	const TCHAR* Owner;
	bool bOpen;
};

struct FRiderSimEvent
{
	int32 Frame = 0;
	bool bMotion = false;
	uint8 From = 0;
	uint8 To = 0;
	const TCHAR* Reason = TEXT("");
};

class SSXTRICKYREBUILD_API FRiderSim
{
public:
	static constexpr double Dt = 1.0 / 60.0;
	static constexpr double SpeedTiers[4] = { 2788.8394, 2932.43, 3072.1418, 3347.2222 };   // cm/s
	static constexpr double SpeedCapDropPerFrame = 3.472222;
	static constexpr double BoostTierHigh = 0.66599226;
	static constexpr double BoostTierMid = 0.33362994;
	static constexpr double StuckADecay = 0.95603746;
	static constexpr double StuckALimit = 4.492007;
	static constexpr double StuckBDecay = 0.9782825;
	static constexpr double StuckBLimit = 12.002146;
	static constexpr double ImpactNormalDecay = 0.98;
	static constexpr double FloorMargin = 10000.0;
	static constexpr double LeanPerSteer = -60.0;
	static constexpr double LeanRate = double(66.666664f);
	static constexpr double LeanFadeSpeed = 1.44;
	static constexpr double TrickyWindow = 20.0;
	// 0x11B1F8
	static double TrickyTimerStep(double T, bool bAir, double Step = Dt);
	static constexpr double PassiveDrainA = 15.483508;
	static constexpr double PassiveDrainB = -15.210359;
	static constexpr double PassiveDrainK = 9.166667e-06;
	static constexpr double RespawnDrop = 101.46;        // cm, passed by every forced reset
	static constexpr double RespawnSpeed = 833.3333;     // cm/s along the path tangent
	static constexpr double ResetWaitDuration = 0.8;
	static constexpr int32 BailSurfaceA = 6;
	static constexpr int32 BailSurfaceB = 10;
	static constexpr int32 OutOfBoundsSurface = 0;

	FRiderSim();
	~FRiderSim();
	FRiderSim(const FRiderSim&) = delete;
	FRiderSim& operator=(const FRiderSim&) = delete;

	/** The world is not owned; it must outlive the sim. */
	void SetWorld(const IRiderWorld* InWorld) { World = InWorld; }
	const IRiderWorld* GetWorld() const { return World; }
	FRiderProfile Profile;

	TFunction<void(FRiderSim&, const SSXGround::FPropContactEvent&)> OnPropContact;
	TFunction<void(FRiderSim&, const FRiderSphereSet&, SSXAnim::FRiderAnimChannel*)> OnMovingObjects;
	// 0x10ED68
	TFunction<int32(FRiderSim&, const FRiderSphereSet&, SSXWipeOut::FTumbleBody&)> OnTumbleNeighbours;

	void SetRaceHooks(IRiderRaceHooks* InHooks) { RaceHooks = InHooks; }
	IRiderRaceHooks* GetRaceHooks() const { return RaceHooks; }
	// 0x1267A0
	void NotifySphereSetMoved(const FVector& D) { if (RaceHooks) { RaceHooks->SphereSetMoved(*this, D); } }

	void SetWorldFloor(double FloorZ) { WorldFloorZ = FloorZ; bHasWorldFloor = true; }
	double GetWorldFloor() const;

	void Reset();

	// 0x11CAE8, 0x15EE30
	void Spawn(const FVector& Position, const FVector& Heading, const FVector& Velocity, double DropHeight, bool bInitial);
	// 0x2510F8
	void SpawnYaw(const FVector& Position, float Yaw, const FVector& Velocity, double DropHeight, bool bInitial);

	// One fixed 1/60 s frame, in the game's order: filters / timers, motion update, speed cap (not in Air), track scan and
	// input word (masked to the control reached AFTER the motion), control update, reset rule, decays / stuck / world floor,
	// race handler, rider collision, lean target / smoothed up, ++Frame, PostAI (render rows, sphere set).
	void Step(const FRiderInputWord& Input);
	// 0x114730, 0x129628, 0x126748
	void PostAI();
	bool bDeferPostAI = false;

	FRiderState State;
	const FRiderState& GetState() const { return State; }

	void SetMotion(ERiderMotion NewMotion, const TCHAR* Reason = TEXT(""));
	void SetControl(ERiderControl NewControl, const TCHAR* Reason = TEXT(""));

	bool ApplyTransition(const TCHAR* Id);
	static TConstArrayView<FRiderTransition> GetTransitionTable();
	static const FRiderTransition* FindTransition(const TCHAR* Id);
	static bool IsResetRuleControl(ERiderControl Control);

	void SetMotionState(ERiderMotion Id, TSharedPtr<IRiderMotionState> Handler);
	void SetControlState(ERiderControl Id, TSharedPtr<IRiderControlState> Handler);

	void SetRegistrationSim(const FRiderSim* InSim) { RegistrationSim = InSim; }
	const FRiderSim* GetRegistrationSim() const { return RegistrationSim; }
	IRiderMotionState* GetMotionState(ERiderMotion Id) const;
	IRiderControlState* GetControlState(ERiderControl Id) const;

	void SaveReplay(FRiderSimKeyframe& Out) const;
	void LoadReplay(const FRiderSimKeyframe& In);

	bool RunGroundProbe(FGroundProbeResult* OutResult = nullptr);
	FGroundProbeResult LastProbe;

	const FSurfaceParams& Surface() const;
	const FSurfaceParams& Surface(int32 SurfaceId) const;

	void StartWipeOut(const FRiderImpact& Impact, const TCHAR* Reason = TEXT("T60"), bool bTrickPending = false);

	// 0x118F10
	void ForcedReset(bool bAuto, const TCHAR* Reason = TEXT("T05"));

	void Respawn();
	void PayRespawn();

	// Basis: forward = q*(0,1,0), side (RIGHT) = q*(1,0,0), up = q*(0,0,1).
	void SetOrientationFromAxes(const FVector& Forward, const FVector& Up);
	void RotateBody(const FVector& Axis, double Angle);   // right-handed world rotation (axis normalised first)
	// 0x128E90
	void RotateBodyEE(const FVector3f& Axis, float Angle);
	// 0x129008
	void UpdateBasis();

	void ApplySpeedCap();

	const TArray<FRiderSimEvent>& GetEvents() const { return Events; }

	FString DebugName;
	FString DumpState() const;
	bool bLogTransitions = false;
	void LogNote(const FString& Text);
	TArray<FString> Notes;
	uint32 DebugPadButtons = 0;
	uint64 DebugHeldActions = 0;
	int32 RailLogWindowFrame = -1000, RailLogLines = 0, RailLogCatchFrame = -1;
	FString RailLogCatch;
	int32 WatchdogReports = 0;
	int32 ResetsWithoutRiding = 0;
	/** Seconds spent in WipeOut + WipeOutRecover since the last other motion. */
	double WipeOutSeconds = 0.0;
	static constexpr double WatchdogWipeOutLimit = 11.0;
	static constexpr double WatchdogStaticLimit = 1.6;
	static constexpr int32 WatchdogResetLoop = 3;
	static constexpr double WatchdogNoProgressLimit = 3.0;
	int32 CountTakeOffs() const { return TakeOffCount; }

	// 0x108378, 0x12B340
	double MotionDt() const { return double(float(State.TimeScale) * 0.016666668f); }   // dt = TimeScale x 0.016666668 (mul.s)

private:
	void TickFilters();
	void RaceHandler();
	void LeanAndUp();
	void InstallPlaceholders();
	void ApplyResetRule(ERiderControl Ran, ERiderControl After, const FRiderInputWord& Word);
	void LogEvent(bool bMotion, uint8 From, uint8 To, const TCHAR* Reason);

	const IRiderWorld* World = nullptr;
	const FRiderSim* RegistrationSim = nullptr;
	IRiderRaceHooks* RaceHooks = nullptr;
	double WorldFloorZ = 0.0;
	bool bHasWorldFloor = false;
	TSharedPtr<IRiderMotionState> MotionStates[int32(ERiderMotion::Count)];
	TSharedPtr<IRiderControlState> ControlStates[int32(ERiderControl::Count)];
	TArray<FRiderSimEvent> Events;
	int32 TakeOffCount = 0;
	void Watchdog();
	void WatchdogResetWait(ERiderControl Old, ERiderControl New);
	bool bWatchdogWipeOut = false;
	bool bWatchdogStatic = false;
	bool bWatchdogNonFinite = false;
	bool bWatchdogResetLoop = false;
	bool bWatchdogNoProgress = false;
	bool bWatchdogInvisible = false;
	FVector WatchdogAnchor = FVector::ZeroVector;
	ERiderMotion WatchdogMotion = ERiderMotion::None;
	ERiderControl WatchdogControl = ERiderControl::None;
	double WatchdogStillSeconds = 0.0;
	double WatchdogInvisibleSeconds = 0.0;
	int32 WatchdogRespawnFrame = -100000;
	struct FLogRepeat { int32 LastFrame = -100000; int32 Suppressed = 0; };
	TMap<uint64, FLogRepeat> LogRepeats;
};
