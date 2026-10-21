// Rider simulation state. All vectors GAME space (SimCoords.h): right-handed, Z up, cm; units cm, s, rad (lean and edge angles in degrees). +0x offsets are cBoarder fields.
#pragma once

#include "CoreMinimal.h"
#include "Sim/EEFloat.h"

enum class ERiderMotion : uint8
{
	None = 0,
	Air = 1,
	Ground = 2,
	RailSlide = 3,
	Static = 4,   // frozen: no motion update at all
	WipeOut = 5,
	WipeOutRecover = 6,   // also the get-up after the start-line sit
	Count
};

enum class ERiderControl : uint8
{
	None = 0,
	Void = 1,
	Bump = 2,
	Cruise = 3,
	ChangeFakie = 4,
	FakieCruise = 5,
	FinishLine = 6,
	GateToBase = 7,
	GateAnticipate = 8,
	GateLaunch = 9,
	Jump = 10,
	LandHard = 11,
	LandNormal = 12,
	NaturalAir = 13,
	Prewind = 14,
	RailSlide = 15,
	Spin = 16,   // in-air trick control (spins, flips, grabs)
	SitAndWait = 17,
	GetupFromSit = 18,
	WipeOut = 19,
	WipeOutRecover = 20,
	LessonWait = 21,
	ResetWait = 22,
	Count
};

SSXTRICKYREBUILD_API const TCHAR* LexToString(ERiderMotion M);
SSXTRICKYREBUILD_API const TCHAR* LexToString(ERiderControl C);

// (current, rate, target): current moves toward target by at most rate x dt per frame
struct FApproachFilter
{
	double Cur = 0.0;
	double Rate = 0.0;
	double Target = 0.0;

	void Tick(double Dt)
	{
		// 0x117198
		const float C = float(Cur), T = float(Target), S = float(Rate) * float(Dt);
		if (SSXEE::FpuAdd(T, S) < C) { Cur = SSXEE::FpuSub(C, S); }
		else if (C < SSXEE::FpuSub(T, S)) { Cur = SSXEE::FpuAdd(C, S); }
		else { Cur = Target; }
	}
	void Reset() { Cur = Rate = Target = 0.0; }
};

// Decoded control word, masked to the layout of the control reached after the motion phase (absent fields read 0).
// Analog values in [-1, 1] after the mapper's dead zone (|v| < 0.38047 -> 0).
struct FRiderInputWord
{
	static constexpr int32 NoTrick = 26;

	bool bResetRequest = false;
	bool bJumpPressed = false;
	bool bJumpHeld = false;
	bool bJumpAbort = false;
	bool bBoostHeld = false;
	bool bBoostPressed = false;
	bool bTweak = false;
	bool bTrickPressed = false;
	bool bLessonContinue = false;
	bool bRailSpinMode = false;
	int32 TrickId = NoTrick;   // Spin layout: highest held trick slot, 26 = none
	double Turn = 0.0;   // + = turn left (d-pad Left gives +1)
	double Crouch = 0.0;
	double Brake = 0.0;
	double RailSpin = 0.0;
	double PrewindTurn = 0.0;
	double PrewindSpin = 0.0;
	double PrewindFlip = 0.0;
	double GateRock = 0.0;
	int32 Spin = 0;   // -1 / 0 / +1 direction fields (Spin layout)
	int32 Flip = 0;
	int32 LateSpin = 0;
	int32 LateFlip = 0;
	double AirTurn = 0.0;
	double AirTilt = 0.0;
	int32 ShoveSide = 0;
	int32 ShoveFwd = 0;

	SSXTRICKYREBUILD_API FRiderInputWord MaskedFor(ERiderControl Control) const;
};

struct FRiderProfile
{
	// Stat bytes 0..255 at profile+0x0E..+0x1D (index = offset - 0x0E).
	uint8 StatBytes[16] = { 128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128 };
	int32 Character = 0;
	int32 TrickCharacter = -1;   // +0x5C grab table index; -1 = same as Character
	int32 Voice = -1;   // +0x64 voice byte (12 = Mix Master Mike); -1 = same as Character
	                           // 0x234698, 0x234730
	int32 Stance = 0;   // +0x40 goofy flag
	int32 BoardClass = 0;
	double SizeScale = 1.0;   // +0x60 rider size scale
	double InitialBoost = 0.0;
	int32 Level = 9;   // +0x74 = 0x164840(slot): 0 Master .. 9 Newbie (9 = new career)
	int32 MassByte = 0;   // +0x1E mass byte; 0 = no profile loaded
	int32 Board = 0;
	bool bComputer = false;   // +0x65 < 0
	bool bMixMasterMike = false; // 0x3352B8
	bool bProfileLoaded = false;

	int32 GetTrickCharacter() const { return TrickCharacter >= 0 ? TrickCharacter : Character; }
	int32 GetVoice() const { return Voice >= 0 ? Voice : Character; }

	double Stat(int32 ProfileOffset) const
	{
		const int32 I = ProfileOffset - 0x0E;
		return (I >= 0 && I < 16) ? StatBytes[I] / 255.0 : 0.5;
	}
};

struct FLandingPrediction
{
	double TimeLeft = 0.0;   // +0x57B8 flight time - elapsed
	FVector LandPoint = FVector::ZeroVector;
	FVector LandVelocity = FVector::ZeroVector;
	FVector LandNormal = FVector::UpVector;
	int32 LandSurface = 1;
	double FlightTime = 0.0;
	double ApexTime = 0.0;
	double AirElapsed = 0.0;   // +0x57FC, +1/60 per frame
	double ApexLeft = 0.0;
};

/** Impact handed to the wipe-out entry (T60): hit point, travel direction, normal, normal speed. */
struct FRiderImpact
{
	FVector Point = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	double Speed = 0.0;
};

// 0x11A798, 0x1532C0, 0x150A80, 0x153350
struct FRiderPadFeedback
{
	bool bImpact = false;
	FVector Velocity = FVector::ZeroVector;  // slot 12: the velocity argument (cm/s)
	FVector Normal = FVector::ZeroVector;
	double Amount = 0.0;
};

struct FRiderState
{
	ERiderMotion Motion = ERiderMotion::None;
	ERiderControl Control = ERiderControl::None;
	int32 Frame = 0;
	double MotionTime = 0.0;                        // seconds since the last SetMotion (rebuild helper)
	double ControlTime = 0.0;                       // seconds since the last SetControl (rebuild helper)

	double BoostMeter = 0.0;   // +0x1C, 0..1
	double TrickyTimer = 0.0;   // +0x24 s (20 s window when the meter fills)
	int32 UberHintIndex = 0;
	int32 TrickyFullCount = 0;
	double TrickyTimerBeforeFull = 0.0;
	int32 TrickyFreshCount = 0;
	int32 MeterNearFullCount = 0;     // 0x11B018
	                                  // 0x2191C0
	int32 UberLandCount = 0;          // 0x108378
	int32 InfiniteBoostCallCount = 0; // 0x233448
	// 0x11F3B8, 0x128150
	int32 PositiveReactionFrame = -1;   // +0x2C, -1 = none
	double PositiveReactionStrength = 0.0;
	bool bLaunchWall = false;         // 0x1284E0
	                                  // 0x156618
	double TimeScale = 1.0;   // +0x12C catch-up time scale: motions integrate dt = TimeScale / 60
	double BoostStrength = 0.0;
	double BoostBurstTimer = 0.0;
	int32 BoostStartCount = 0;        // 0x2192A8
	bool bBoostStartPressed = false;
	int32 BoostEmptyPressCount = 0;   // 0x2197D8
	// 0x217D50, 0x2181E8
	int32 CrowdCueCount = 0;
	double CrowdCueX = 0.0;
	bool bCrowdCueBail = false;
	// 0x216C50
	int32 PropHitCount = 0;
	int32 PropHitInstance = -1;
	double PropHitSpeed = 0.0;        // |v| (cm/s) at the contact
	double LaunchBoostTimer = 0.0;
	bool bInfiniteBoost = false;   // race_stats+0x88 > 5
	bool bRacing = true;   // race phase == 4
	int32 FinishResult = -1;   // +0x418, >= 0 once finished

	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	double GroundDragAccel = 0.0;
	double ForwardDistance = 0.0;
	double RefDirAngle = 0.0;
	FQuat Orientation = FQuat::Identity;   // +0x170 body quaternion
	FVector BasisForward = FVector(0, 1, 0);   // +0x180 q*(0,1,0), the nose
	FVector BasisSide = FVector(1, 0, 0);   // +0x190 q*(1,0,0), the RIGHT axis
	FVector BasisUp = FVector(0, 0, 1);   // +0x1A0 q*(0,0,1)
	double Yaw = 0.0;
	bool bStanceGoofy = false;
	double GroundHeight = 0.0;   // +0x1BC signed height above ground along n
	double SuspensionOffset = 0.0;
	double SpeedCap = 2788.8394;                // +0x1C4 cm/s
	double LaunchCharge = 0.0;
	double BoardChatter = 0.0;
	double SpinAngle = 0.0;
	double FlipAngle = 0.0;
	double EdgeAngle = 0.0;
	int32 GrindType = 0;
	int32 TrickId = 0x1A;   // +0x1E4, 0x1A = none
	int32 TrickPhase = 0;
	int32 TrickHoldFrames = 0;
	int32 TweakStartCount = 0;                  // 0x1272E8, 0x218B78
	int32 BoardClass = 0;

	FApproachFilter Ramp1F0;
	FApproachFilter Brake;
	FApproachFilter Crouch;
	FApproachFilter Steer;   // +0x214, + = left
	FApproachFilter SteerFollow;
	FApproachFilter PrewindA;
	FApproachFilter PrewindB;
	FApproachFilter Lean;          // +0x244 degrees, target -60*steer
	FApproachFilter BoostPose;
	FApproachFilter BoostFlash;
	// 0x116AB8, 0x11CAE8
	FApproachFilter RailBox{ 1.0, 0.0, 1.0 };
	FApproachFilter GateLaunch;
	double AirTurnOffset = 0.0;
	double AirTiltOffset = 0.0;
	double AirTurnSmoothed = 0.0;   // +0x27C -> AirTurnOffset at 5/s
	double AirTiltSmoothed = 0.0;   // +0x280 -> AirTiltOffset at 5/s

	int32 SurfaceId = 1;
	double SurfaceP7 = 0.5;   // +0x294 smoothed surface[7] (sink depth)
	double SurfaceP8 = 2.5;   // +0x298 smoothed surface[8]
	int32 SurfaceChanged = 0;
	FVector GroundNormal = FVector::UpVector;
	FVector FallLine = FVector(0, 1, 0);   // +0x2B0 downhill in the tangent plane
	FVector SmoothedUp = FVector::UpVector;
	FVector ContactPoint = FVector::ZeroVector;
	int32 ContactKind = 3;   // +0x2E0: 0 static, 1 prop without surface, 2 animated, 3 none
	FVector ContactForward = FVector(0, 1, 0);   // +0x320 normalize(f - (f.n) n)
	FVector ContactLeft = FVector(-1, 0, 0);   // +0x330 n x contact_forward (LEFT)
	double SurfaceP10 = 0.0;
	double GroundBlend = 0.0;
	int32 SurfaceBeforeWipeout = 1;
	bool bWallHitThisFrame = false;
	int32 WallSoundCount = 0;
	                                            // 0x126250, 0x2162F0

	double StuckAccumA = 0.0;
	FVector LastImpactNormal = FVector::ZeroVector;   // +0x2F0, decays x0.98 per frame
	double StuckAccumB = 0.0;
	bool bAutoReset = false;
	double RenderAlpha = 1.0;
	double ResetFade = 0.0;
	double ResetWaitTime = 0.0;
	double ResetDropHeight = 101.46;            // drop stored by the forced reset (cm)
	uint32 RespawnSerial = 0;                   // +1 per respawn (the human camera cuts, 0x119220 -> 0x173A60)
	FVector SpawnPosition = FVector::ZeroVector;
	FVector LastGroundPosition = FVector::ZeroVector;
	FVector LastGroundHeading = FVector(0, 1, 0);

	double PathParam = 0.0;
	double PathLateral = 0.0;
	FVector PathPoint = FVector::ZeroVector;
	FVector PathLookahead = FVector::ZeroVector;
	double PathHeading = 0.0;
	double TrackDistToGo = 0.0;

	int32 SphereMask = -1;
	FLandingPrediction Prediction;
	FRiderImpact LastImpact;

	// Controller yaw +0x46E0 / mirror +0x46D4: owned by the rail control while it runs; elsewhere derived from the
	// stance (goofy: yaw -pi, mirror 1).
	double AnimYaw = 0.0;
	bool bAnimMirror = false;
	bool bRailAnimPose = false;
	// 0x15EEC0
	int32 Ch1Anim = 0x300;   // 0x300 = nothing on channel 1
	int32 Ch1EndFrame = 0;
	// 0x15FDD0
	int32 Ch0Anim = 0x300;   // channel 0's id as the next update sees it; 0x300 = none
	// 0x1065C8, 0x11D998
	bool bFinishLineDone = false;
	// 0x127D08
	bool bLandBobbleCall = false;
	// 0x102FB0, 0x1033D0, 0x103660
	double CruiseLookTimer = 0.0;
	double CruiseTauntTimer = 0.0;

	int32 ShoveAnim = 0;   // 0x205..0x208, 0 = none
	int32 ShoveFramesLeft = 0;
	bool bShoveActive = false;
	FVector ShoveAxis = FVector::ZeroVector;    // attack axis (horizontal unit) of the current shove
	double ShoveAngle = 0.0;   // +0x1E0 atan2(side, fwd): 0 ahead, -pi/2 right
	int32 ShoveAge = 0;
	int32 ShoveOpen = 0;
	int32 ShoveClose = 0;
	int32 ShoveCount = 0;

	// Board matrix +0x4A60 rows long / up / across, origin +0x4A90: built by the pose code after the motion, so the
	// next frame's rail code reads the previous pose. Fed by the owner; bBoardValid = false falls back to the body frame.
	FVector BoardRow0 = FVector(0, 1, 0);
	FVector BoardRow1 = FVector(0, 0, 1);
	FVector BoardRow2 = FVector(1, 0, 0);
	FVector BoardOrigin = FVector::ZeroVector;
	bool bBoardValid = false;
	// 0x1350C0
	// the board matrix before the last feed: what the boarder update's effects read (the previous pose)
	FVector FxBoardRow0 = FVector(0, 1, 0);
	FVector FxBoardRow1 = FVector(0, 0, 1);
	FVector FxBoardRow2 = FVector(1, 0, 0);
	FVector FxBoardOrigin = FVector::ZeroVector;
	bool bFxBoardValid = false;
	// 0x129628, 0x205A78, 0x1D9208, 0x136C48, 0x132648
	// PoseBones = this pose's palette translations, FxPoseBones = the previous pose's (read by the update effects)
	static constexpr int32 FxPoseBoneCount = 19;
	FVector PoseBones[FxPoseBoneCount];
	FVector FxPoseBones[FxPoseBoneCount];
	bool bPoseBonesValid = false;
	bool bFxPoseBonesValid = false;

	// 0x2361B0
	// body frame about Position, Q^-1 (c - Position), of the last rendered pose; false: the bind-pose reduction
	FVector PosedSpheres[18];
	bool bPosedSpheresValid = false;
	FVector PosedPivot = FVector::ZeroVector;
	// 0x114730, 0x117180, 0x15F390, 0x1029D0
	FVector PoseRoot = FVector::ZeroVector;
	bool bPoseRootValid = false;
	// 0x1372F8
	// render matrix rows +0x4AA0 side / +0x4AB0 forward / +0x4AC0 up, stored at the end of Step: during an update they
	// are the previous frame's
	FVector RenderRow0 = FVector(1, 0, 0);
	FVector RenderRow1 = FVector(0, 1, 0);
	FVector RenderRow2 = FVector(0, 0, 1);
	FVector RenderRow3 = FVector::ZeroVector;   // +0x4AD0: the frame applied to the pose root
	bool bRenderRowsValid = false;
	// 0x126748, 0x10DAD8
	FVector SphereFrameP = FVector::ZeroVector;
	FQuat SphereFrameQ = FQuat::Identity;
	bool bSphereFrameValid = false;

	// 0x10E190, 0x12E008, 0x150A80
	int32 TumbleImpactCount = 0;
	FVector TumbleImpactPoint = FVector::ZeroVector;
	FVector TumbleImpactVelocity = FVector::ZeroVector;
	FVector TumbleImpactNormal = FVector::ZeroVector;
	// 0x218328, 0x10F9A0, 0x218700
	int32 CrashSoundCount = 0;
	// 0x118F10, 0x218EB0
	int32 ForcedResetCount = 0;
	int32 SlideSoundCount = 0;
	// 0x124DD0, 0x235398
	int32 BumpSpeechCount = 0;
	bool bBumpSpeechHard = false;
	// 0x368540, 0x1041B0, 0x104388
	float BumpYaw = 0.0f;
	float BumpYawDone = 0.0f;

	// 0x108E8C, 0x108DFC, 0x109278, 0x10AC28, 0x10E310, 0x125BCC, 0x124BA8
	TArray<FRiderPadFeedback> PadFeedback;
	static constexpr int32 MaxPadFeedback = 32;
	void AddPadImpact(const FVector& InVelocity, const FVector& InNormal)
	{
		if (PadFeedback.Num() >= MaxPadFeedback) { PadFeedback.RemoveAt(0); }
		FRiderPadFeedback& F = PadFeedback.AddDefaulted_GetRef();
		F.bImpact = true; F.Velocity = InVelocity; F.Normal = InNormal;
	}
	void AddPadFeedback(double InAmount)
	{
		if (PadFeedback.Num() >= MaxPadFeedback) { PadFeedback.RemoveAt(0); }
		PadFeedback.AddDefaulted_GetRef().Amount = InAmount;
	}
	TArray<FRiderPadFeedback> TakePadFeedback() { return MoveTemp(PadFeedback); }

	// 0x11A798, 0x136E70
	struct FFxImpact { FVector Point = FVector::ZeroVector, Velocity = FVector::ZeroVector, Normal = FVector::UpVector; int32 Serial = 0; };
	TArray<FFxImpact> FxImpacts;
	int32 FxImpactSerial = 0;
	static constexpr int32 MaxFxImpacts = 8;
	void AddFxImpact(const FVector& InPoint, const FVector& InVelocity, const FVector& InNormal)
	{
		if (FxImpacts.Num() >= MaxFxImpacts) { FxImpacts.RemoveAt(0); }
		FxImpacts.Add({ InPoint, InVelocity, InNormal, ++FxImpactSerial });
	}

	// 0x102FC8, 0x11D830, 0x104790, 0x104E38, 0x108110, 0x10D680, 0x112D10
	struct FHighlightCall { int32 Slot = 0; float Arg = 0.f; };
	TArray<FHighlightCall> HighlightCalls;
	static constexpr int32 MaxHighlightCalls = 32;
	void Highlight(int32 InSlot, float InArg = 0.f)
	{
		if (HighlightCalls.Num() >= MaxHighlightCalls) { HighlightCalls.RemoveAt(0); }
		HighlightCalls.Add({ InSlot, InArg });
	}
	TArray<FHighlightCall> TakeHighlightCalls() { return MoveTemp(HighlightCalls); }
};
