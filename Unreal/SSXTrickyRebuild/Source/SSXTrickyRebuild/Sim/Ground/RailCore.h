#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderState.h"
#include "Sim/RiderWorld.h"

// Units cm, s, rad; game space (Z up). Board matrix = cBoarder+0x4A60: rows R0 long axis, R1 up, R2 across,
// origin O (+0x4A90), the one built BEFORE this frame's motion.
//
namespace SSXRail
{
	constexpr double BoxFull[3] = { 150.0, 50.0, 50.0 };
	constexpr double BoxSmall[3] = { 80.0, 50.0, 30.0 };
	constexpr double CatchMargin = 0.9;
	constexpr double CatchUpOffset = 30.0;
	constexpr double CatchBehind = -0.2;
	constexpr double MinSpeed = 555.55554;                   // cm/s, catch speed floor, k knee
	constexpr double CatchVzKeep = 0.1;
	constexpr double GrindBackDeg = 150.0;
	constexpr double GrindSideDeg = 30.0;
	constexpr double Gravity = 980.0;
	constexpr double Boost = 2450.8232;                      // cm/s^2 x boost strength
	constexpr double TangentPull = 30.0;   // 1/s
	constexpr double HeightSnap = 8.333333;                  // cm per frame
	constexpr double FallPush = 277.77777;                   // cm/s (0x438AE38E)
	constexpr double SettleTime = 0.6;
	constexpr double SteepTz = 0.92;
	constexpr double BalKSlow = 0.5;
	constexpr double BalKPerSpeed = 0.0009;
	constexpr double BalSteer = 145.07674;
	constexpr double BalCentre = -5.0;
	constexpr double BalUnstable = 1.8153523;
	constexpr double BalNear = -1.0;
	constexpr double BalNearDist = 2.5000005;
	constexpr double BalClamp = 20.0;
	constexpr double BalUpsideMin = 290.15347;
	constexpr double RollSlideAmp = 0.6981317007977318;   // 40 deg, grind types 3 / 4
	constexpr double RollSlidePer = 0.0125;
	constexpr double RollGrindAmp = 0.3490658503988659;   // 20 deg, grind types 1 / 2
	constexpr double RollGrindPer = 1.0 / 30.0;
	constexpr double RollSlew = 5.0;   // rad per frame
	constexpr double OrientBlend = 0.083333336;   // per frame
	constexpr double FilterRate = 1.6666666;   // rail box filter rate, per s
	constexpr double FilterExitValue = -0.25;

	struct FRailBoard
	{
		FVector R0 = FVector(0, 1, 0);
		FVector R1 = FVector(0, 0, 1);
		FVector R2 = FVector(1, 0, 0);
		FVector Origin = FVector::ZeroVector;
	};

	struct FRailRider
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		FRailBoard Board;
		FVector Up = FVector::UpVector;   // body up +0x1A0
		FVector Side = FVector(1, 0, 0);   // body side +0x190
		double Steer = 0.0;
		double BoostStrength = 0.0;
		double TimeScale = 1.0;
		FApproachFilter BoxFilter;
		double RailTime = 0.0;
		double Roll = 0.0;
		int32 GrindType = 0;
		int32 Surface = 0;
		FVector RailPoint = FVector::ZeroVector;
		FVector Tangent = FVector::ZeroVector;   // this frame's t (oriented along v)
		double Lateral = 0.0;
	};

	enum class ESlideResult : uint8
	{
		OnRail,
		NoHit,
		FellOff,
	};

	/** Diagnostics of one catch test (ssx.Rail.Log; no game behaviour): why it failed and the window terms. */
	enum class ECatchVerdict : uint8 { NoVelocity, NoRail, Behind, TrickBlocks, OutsideR0, OutsideR1, OutsideR2, Caught };
	struct FCatchDiag
	{
		ECatchVerdict Verdict = ECatchVerdict::NoRail;
		int32 Spline = INDEX_NONE;
		float C0 = 0.f, C1 = 0.f, C2 = 0.f;
		float L0 = 0.f, L1 = 0.f, L2 = 0.f;
		float Cv = 0.f;
		float Box = 0.f;
	};
	SSXTRICKYREBUILD_API const TCHAR* LexCatchVerdict(ECatchVerdict V);

	SSXTRICKYREBUILD_API bool CatchTest(FRailRider& R, const IRiderWorld& World, bool bTrickBlocks, FCatchDiag* OutDiag = nullptr);

	SSXTRICKYREBUILD_API void EnterRail(FRailRider& R);
	SSXTRICKYREBUILD_API void ExitRail(FRailRider& R);

	SSXTRICKYREBUILD_API ESlideResult SlideStep(FRailRider& R, const IRiderWorld& World);

	SSXTRICKYREBUILD_API FQuat RailFrame(const FVector& T, const FVector& CurrentUp, double Roll, FVector& OutRailUp);

	// 0x250A48
	SSXTRICKYREBUILD_API FQuat GameSlerp(const FQuat& A, const FQuat& B, double F);
}
