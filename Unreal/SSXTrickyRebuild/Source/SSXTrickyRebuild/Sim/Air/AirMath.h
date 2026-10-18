#pragma once

#include "CoreMinimal.h"

// Game space (SimCoords.h): cm, s, rad, Z up; FQuat Hamilton product (v' = q v q*).
// Board body axes: side (RIGHT) +X, forward (nose) +Y, up +Z.
//
struct FRiderProfile;

namespace SSXAir
{
	constexpr double GravityUp = -850.2414;   // vz > 0
	constexpr double GravityDown = -1900.8359;   // vz <= 0
	constexpr double HorizontalDrag = 0.20002478;   // horizontal only
	constexpr double AirSpeedCap = 3347.2222;
	constexpr double PredictorSubStep = 0.2;

	constexpr double SeedPrewind = 11.5172;
	constexpr double SeedPrewindFlipScale = 2.0 / 3.0;
	constexpr double SeedLate = 5.2530804;
	constexpr double LaunchBoostMult = 1.6;
	constexpr double FakieMult = 0.8;
	constexpr double LoFraction = 0.3;
	constexpr double RateFloorMax = 1.3962634;   // 80 deg/s
	constexpr double Slew = 75.0;   // rad/s^2
	constexpr double SettleGain = 150.53665;
	constexpr double BandNear = 0.6981317;   // 40 deg
	constexpr double BandFar = 2.4434610;   // 140 deg
	constexpr double LateRateMax = 0.7;
	constexpr double LateAxisFlipOnly = 1.3962634;   // 80 deg
	constexpr double LateAxisSpinOnly = 0.17453293;   // 10 deg
	constexpr double AirTurnRange = 2.0943951;   // 120 deg
	constexpr double AlignRateMax = 1.3962634;   // 80 deg/s
	constexpr double AlignLimitMin = 1.3962634;   // 80 deg
	constexpr double AlignLimitScale = 2.0943951;   // 120 deg x n'.z
	constexpr double NormalBias = 0.5;

	constexpr double LandPitchMin = -2.967825;
	constexpr double LandPitchMax = 2.6840878;
	constexpr double NoseOkPitch = -0.78539816;   // -45 deg (phi < 0 = nose up)
	constexpr double HardImpactVn = -2777.1052;
	constexpr double ImpactStrengthK = 0.00045;

	constexpr double WallPushOut = 1.1;
	constexpr double WallAlignBase = 0.6;
	constexpr double WallAlignExtra = 0.3;
	constexpr double WallAlignK = 0.00045;
	constexpr double WallAlignRate = 0.10471976;   // 6 deg per frame

	constexpr int32 NoTrick = 0x1A;   // trick id "none"

	SSXTRICKYREBUILD_API double SpinGain(const FRiderProfile& P);
	SSXTRICKYREBUILD_API double SpinDecay(const FRiderProfile& P);   // per-frame envelope decay, profile+0x1A
	SSXTRICKYREBUILD_API double LandScale(const FRiderProfile& P);
	SSXTRICKYREBUILD_API double BoostGain(const FRiderProfile& P);
	double AirTurnRate(const FRiderProfile& P);
	double GrabAnimSpeed(const FRiderProfile& P);

	void BallisticStep(FVector& P, FVector& V, double Dt);

	double WrapPi(double A);
	/** Shortest rotation taking unit-ish A onto B. */
	FQuat TrickRotation(double SpinAngle, double FlipAngle);
	FQuat FoldTrick(const FQuat& Q, double SpinAngle, double FlipAngle);

	void AutoAlignSolve(const FQuat& Q, const FVector& PredictedNormal, int32 BoardClass, int32 TakeoffType,
	                    double& OutAngle, FVector& OutAxis);

	enum class ELandingVerdict : uint8 { LandNormal, LandHard, WipeOut };
	SSXTRICKYREBUILD_API const TCHAR* LexToString(ELandingVerdict V);

	// yaw error psi in [0, pi] and pitch phi = -atan2(F.n, U.n) (negative = nose up)
	SSXTRICKYREBUILD_API void LandingAngles(const FQuat& Q, const FVector& Velocity, const FVector& Normal, double& OutPsi, double& OutPhi);

	double LandingSpeedFactor(double Psi, double Phi);

	struct FLandingInput
	{
		FQuat BasisQ = FQuat::Identity;
		double PendingSpin = 0.0;
		double PendingFlip = 0.0;
		FVector Velocity = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		double KLand = 1.5;
		int32 BoardClass = 0;
		bool bRidingFakie = false;
		int32 Surface = 1;
		bool bGrabActive = false;
		bool bGrabLandable = false;
		bool bGrabCategory = false;
	};

	struct FLandingResult
	{
		ELandingVerdict Verdict = ELandingVerdict::LandNormal;
		bool bBailGrab = false;
		bool bBailSurface = false;
		bool bBailPitch = false;
		double Psi = 0.0;   // yaw error of the folded pose, [0, pi]
		double PhiBasis = 0.0;
		double PhiEff = 0.0;   // PhiBasis + pending flip, wrapped
		double Vn = 0.0;   // n.v at touchdown (< 0 into the surface)
		double SpeedFactor = 1.0;
		double ImpactStrength = 0.25;   // clamp(-0.00045 v_n, 0.25, 1)
		bool bStanceFlip = false;
		FQuat FoldedQ = FQuat::Identity;   // BasisQ * Rz(-spin) * Rx(-flip)
	};

	SSXTRICKYREBUILD_API FLandingResult EvaluateLanding(const FLandingInput& In);

	SSXTRICKYREBUILD_API int32 LandHardAnimation(const FLandingResult& R, int32 BoardClass, bool bRidingFakie);
}
