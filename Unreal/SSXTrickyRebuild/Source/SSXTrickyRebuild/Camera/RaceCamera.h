// 0x177078, 0x174238, 0x1722E8, 0x16FE60, 0x16E900, 0x337798, 0x175450
// Game space throughout (SimCoords.h); the pawn converts the output once with SSXSim::GameToUnreal.
// The FOV is HORIZONTAL: projection m00 = cot(half), m11 = cot(half) x 4/3.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraDefs.h"

struct FRiderState;
struct FRiderProfile;

namespace SSXCamera
{
	constexpr double Dt = 1.0 / 60.0;
	constexpr int32 ViewCount = 4;

	// FUN_001741d0
	SSXTRICKYREBUILD_API double Wrap(double A);
	SSXTRICKYREBUILD_API double Ease(int32 Kind, double U);
	SSXTRICKYREBUILD_API const TCHAR* ModeDefinition(int32 Mode);
	// 0x383040, 0x1786D0
	SSXTRICKYREBUILD_API const TCHAR* ReplayModeDefinition(int32 Mode);

	// 0x1C72E8, 0x32CA14, 0x1DC3E0
	struct FProjection { float M00 = 0.f, M11 = 0.f, M22 = 0.f, M23 = 1.f, M32 = 0.f; };
	SSXTRICKYREBUILD_API FProjection Projection(float HalfFov, float Near, float Far);
	constexpr float DefaultFar = 30000.f;
	SSXTRICKYREBUILD_API float ProjectionFar(float DrawDist1P, float DrawDist2P, int32 Viewports, int32 RacePhase, int32 ReplayState);
	// 0x1C7810
	inline float TerrainFar(float DrawDist1P, float DrawDist2P, int32 Viewports) { return Viewports < 2 ? DrawDist1P : DrawDist2P; }
	// FUN_00260278
	inline int32 TerrainCellRadius(float Far) { return int32(Far - 1.f) / 10000 + 1; }
	// FUN_001704B0
	SSXTRICKYREBUILD_API float Hermite(float P0, float P1, float M0, float M1, float T);
	// 0x173820
	struct SSXTRICKYREBUILD_API FFlash
	{
		bool bActive = false;
		float T = 0.f;
		int32 Hold = 0;
		// FUN_00174C30
		void Start() { T = 0.f; bActive = true; }
		// FUN_00174C48
		void Force() { Hold = 10; }
		// FUN_00174B38, 0x174C58
		float Update();
		void Clear() { *this = FFlash(); }
	};
	SSXTRICKYREBUILD_API FFlash& SharedFlash();
	inline double VerticalFov43(double HalfFov) { return 2.0 * FMath::Atan(FMath::Tan(HalfFov) * 0.75); }
}

struct FSSXCameraRiderInput
{
	FVector Velocity = FVector::ZeroVector;
	FVector Forward = FVector(0, 1, 0);
	double Yaw = 0.0;
	FVector Anchor = FVector::ZeroVector;
	FVector Contact = FVector::ZeroVector;
	double BoostStrength = 0.0;
	int32 Motion = 2;
	int32 Control = 3;
	FVector GroundNormal = FVector::UpVector;
	int32 Surface = 1;
	double PredTimeLeft = 0.0;
	FVector PredLandVelocity = FVector::ZeroVector;
	FVector PredLandNormal = FVector::UpVector;
	FVector PredLandPoint = FVector::ZeroVector;     // FUN_00171500
	int32 FinishResult = -1;
	bool bGoofy = false;
	int32 Character = -1;                            // FUN_00173c50
	TArray<FVector, TInlineAllocator<19>> Bones;
	FVector BoardPos = FVector::ZeroVector;
	bool bHasBoard = false;

	static SSXTRICKYREBUILD_API FSSXCameraRiderInput FromState(const FRiderState& S, const FVector& Anchor);
};

namespace SSXCamera
{
	SSXTRICKYREBUILD_API FVector RiderAnchor(const FRiderState& S, const FRiderProfile& Profile, const FQuat& Rendered);
}

// 0x113A88, 0x1797A8
struct SSXTRICKYREBUILD_API FSSXCameraReverseLatch
{
	bool bLatch = false;
	void Step(class FSSXRaceCamera& Cam, bool bHeld, bool bPressed, bool bReplayFlag);
};

class SSXTRICKYREBUILD_API FSSXRaceCamera
{
public:
	using FLineQuery = TFunction<bool(const FVector& A, const FVector& B, FVector& OutPoint, FVector& OutNormal)>;

	// FUN_00173820
	explicit FSSXRaceCamera(const FSSXCameraDefSet* InDefs = nullptr) { SetDefs(InDefs); SSXCamera::SharedFlash().Clear(); Reset(); }
	void SetDefs(const FSSXCameraDefSet* InDefs) { Defs = InDefs; }
	FLineQuery LineQuery;

	// FUN_00173a98
	void Reset();
	// FUN_00177db0
	void MarkCut() { bCut = true; bKeepCut = true; }
	// FUN_001776d0
	void SetMode(int32 InMode) { Mode = InMode; MarkCut(); }
	// FUN_001776F0, 0x114730, 0x113A88, 0x1776D0
	void SetReverseHold(bool bHold, bool bReplayFlag);
	// FUN_00173eb8
	void SetViewIndex(int32 Index);
	// FUN_00173e70
	void CycleView(int32 Step) { SetViewIndex(((ViewIndex + Step) % SSXCamera::ViewCount + SSXCamera::ViewCount) % SSXCamera::ViewCount); }
	// FUN_00175de0
	bool PlayScript(const FString& Name);
	bool IsScriptRunning() const { return Script != nullptr; }
	// FUN_00175eb8
	bool IsDirectorOn() const { return Script != nullptr || bReturned; }
	void StopScript() { Script = nullptr; Stack.Reset(); bReturned = false; }
	// 0x110AB8, 0x110A40, FUN_00231318, 0x382E38
	bool ResetDirector() { const bool bOn = IsDirectorOn(); if (bOn) { DirectorEndCut(); } StopScript(); return bOn; }
	// 0x20C3E8
	void DirectorEndCut() { if (Name != TEXT("chase near") || Mode != 4) { MarkCut(); } }
	// FUN_00175F50
	TFunction<void(int32 Op, int32 Arg)> OnScriptOp;
	TFunction<void(int32 Op, const FString& Name)> OnSceneOp;
	// FUN_00173c50, FUN_00174010, FUN_001814a8
	TFunction<int32()> NumRacers;
	TFunction<int32()> OwnRacer;
	TFunction<bool(int32 Racer, FSSXCameraRiderInput& Out)> RacerInput;
	static constexpr int32 NumSlots = 26;
	int32 GetSlotRacer(int32 Slot) const { return Slot >= 0 && Slot < NumSlots ? Entries[Slot].Racer : -1; }
	const FSSXCameraDefSet* GetDefs() const { return Defs; }
	const FString& GetEvaluatedName() const { return Name; }
	const FString& GetScriptName() const { return CurScript; }

	// 0x3368B0
	bool bReplay = false;
	bool bRegionsOff = false;
	// FUN_00174b38
	TFunction<bool()> ReplayFrozen;

	void Step(const FSSXCameraRiderInput& Rider);

	FVector GetPosition() const { return FVector(C[0], C[1], C[2]); }
	double GetYaw() const { return C[3]; }   // look yaw atan2(dy, dx)
	double GetPitch() const { return C[4]; }   // + = up
	double GetRoll() const { return C[5]; }
	double GetFov() const { return C[6]; }   // full horizontal FOV, rad
	// 0x1DC3E0
	double GetRenderNear(double MinNear = 10.0) const { return FMath::Max(C[7], MinNear); }   // near plane (cm)
	// 0x1E0FB8
	double GetRenderFlash() const { return FMath::Clamp(C[8], 0.0, 1.0); }
	// FUN_00174c48
	void ForceFlash() { SSXCamera::SharedFlash().Force(); }
	// FUN_00174c30
	static void StartFlash() { SSXCamera::SharedFlash().Start(); }
	void GetViewAxes(FVector& OutForward, FVector& OutUp) const;
	// forward from yaw / pitch; up = vertical made perpendicular to forward, turned by Roll about forward (right-hand rule)
	static void ViewAxes(double Yaw, double Pitch, double Roll, FVector& OutForward, FVector& OutUp);

	int32 GetMode() const { return Mode; }
	int32 GetViewIndex() const { return ViewIndex; }
	double GetWipeBlend() const { return Blend; }
	const double* GetC() const { return C; }
	const double* GetV() const { return V; }

private:
	struct FEntry
	{
		int32 Racer = -1;   // index into Inputs; -1 = no rider
		double Yaw = 0.0;
		FVector Dir = FVector::ZeroVector;
		FVector Anchor = FVector::ZeroVector;
	};
	// FUN_00173270
	void UpdateEntry(FEntry& E, const FSSXCameraRiderInput& R) const;
	void UpdateTable(const FSSXCameraRiderInput& R);
	const FEntry& SlotEntry(int32 Slot) const;
	const FSSXCameraRiderInput* SlotRider(int32 Slot) const;
	bool SlotBone(int32 Slot, int32 Bone, FVector& Out) const;
	// FUN_00174c98, FUN_00174888, 0x16E900
	void Evaluate(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, bool bSmooth);
	// FUN_00174d28, FUN_00175a08
	bool BlendTo(const FSSXCameraDef& To, const FSSXCameraRiderInput& R, double U);
	// 0x1722E8, 0x174D28
	void EvalScratch(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, double* W, bool bCutFlag);
	// FUN_0016e900
	FVector EvalPosition(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, bool& bHit, double* W, bool bCutFlag);
	// FUN_001722e8
	void EvalTarget(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, bool bLandingDip, double AddYaw, double* W);
	void Smooth();
	void FlipAndClear();
	FVector Collide(double OrbitDist, FVector P, const FVector& A, bool& bHit) const;
	FVector Clearance(FVector P, const FVector& A, double Radius) const;
	bool StepScript(const FSSXCameraRiderInput& R, double PrevClock);
public:
	// FUN_00171610, FUN_00171500
	bool RegionInside(const FSSXCameraRegion& G, bool bPrevTable) const;
private:
	// FUN_00171e28
	bool RegionEvent(const FSSXCameraRegion& G, int32 K) const;
	// 0x177078
	void StepRegions();
	bool Line(const FVector& A, const FVector& B, FVector& Pt, FVector& N) const { return LineQuery && LineQuery(A, B, Pt, N); }

	const FSSXCameraDefSet* Defs = nullptr;
	int32 ViewIndex = 1;
	int32 Mode = 4;
	int32 SavedMode = 4;
	bool bCut = true;
	bool bKeepCut = false;
	int32 StaticCount = 0;
	// 0x173A98
	double Clock = 500.0;
	bool bBlendDone = false;
	double Blend = 0.0;
	FString Name;
	bool bPushed = false;
	bool bRollForced = false;
	double C[22];
	double V[22];
	FVector S = FVector::ZeroVector;
	double StageYaw = 0.0;   // FUN_001777D0
	FEntry Entries[NumSlots];
	FEntry EvalEntries[NumSlots];
	FEntry PrevEntries[NumSlots];
	FString CurScript;
	TArray<FSSXCameraRiderInput, TInlineAllocator<8>> Inputs;
	double LastLookX = 1.0, LastLookY = 0.0;
	const TArray<FSSXCameraScriptEvent>* Script = nullptr;
	double ScriptClock = 0.0;
	int32 ScriptEvent = 0;
	bool bHold = false;
	bool bReturned = false;
	struct FScriptFrame { const TArray<FSSXCameraScriptEvent>* Script = nullptr; int32 Event = 0; double Clock = 0.0; };
	TArray<FScriptFrame, TInlineAllocator<16>> Stack;
	bool bBlendScratch = false;
	double BlendF[22];
	double BlendT[22];
	// 0x174C58
	void UpdateFlash() { C[8] = SSXCamera::SharedFlash().Update(); }
};
