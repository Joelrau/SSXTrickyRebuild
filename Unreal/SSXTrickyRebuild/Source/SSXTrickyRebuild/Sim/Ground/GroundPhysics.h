#pragma once

#include "CoreMinimal.h"

// Units cm, s, rad (edge angle from degrees). Game space: right-handed, Z up. Positive steer turns the board to its
// left (CCW from above); "left" vectors are n x f.
//
class FRiderSim;
struct FSurfaceParams;

namespace SSXGround
{
	constexpr int32 SurfaceIdFromPatchType(int32 PatchType) { return PatchType; }

	// deep snow (records 3 / 4, 15-25 cm sink)
	inline bool IsDeepSnow(int32 SurfaceId) { return SurfaceId == 3 || SurfaceId == 4; }
	inline bool IsBailSurface(int32 SurfaceId) { return SurfaceId == 6 || SurfaceId == 10; }

	namespace K
	{
		constexpr double KmhToCms = 1000.0 / 36.0;
		constexpr double PullDivisor = 30.0;          // 2: g per 30 cm above the plane
		constexpr double DepthFloor = -10.0;
		constexpr double FakieSpeed = 111.11;
		constexpr double Redirect = 0.4;
		constexpr double TiltRate = 0.23334;
		constexpr double YawGain = 2.0030e-5;
		constexpr double YawClamp = 0.10472;   // 6 deg per frame
		constexpr double TraverseSpeed = 555.56;
		constexpr double TraverseGain = 40.0;
		constexpr double TraverseFloor = 0.01004;
		constexpr double GripBreak1 = 555.56;
		constexpr double GripBreak2 = 1388.89;
		constexpr double BoostGripCut = 3.5;
		constexpr double StandingDrag = 0.10573;
		constexpr double DragLoadK = 1.0;
		constexpr double BrakeDrag = 1.7513;
		constexpr double BrakeBoost = 1.2154;
		constexpr double BoostThrust = 2350.31;
		constexpr double BoostThrustUphill = 1053.45;
		constexpr double BoostSteerGate = 0.0798;
		constexpr double BurstAlignment = 5.5;
		constexpr double SkateClamp = 1111.11;
		constexpr double PushAnimSpeed = 833.33;   // cm/s: Cruise push animation below 30 km/h
		constexpr double JumpMin = double(630.8786f);   // 0x1046B0
		constexpr double JumpCurveBreak = 993.48;
		constexpr double JumpCurveCap = 899.868;
		constexpr double JumpForwardTilt = 0.2;
		constexpr double LandImpactHard = -2777.1;
		constexpr double AbsorbK = 0.00045;
		constexpr double AbsorbRate = double(8.33333397f);   // 0x104E68
		constexpr double ChatterSpeed = 0.00036;
		constexpr double ChatterPhase = 2.65;
		constexpr double ChatterAmp = 7.5;
		constexpr double SteerClamp = double(0.90518558f);   // 0x126948
		constexpr double SteerRateK = double(7.0173593f);
		constexpr double SteerRateMin = double(0.1f);
		constexpr double SteerRateMax = double(8.0183487f);
		constexpr double DeepSnowSteerRate = double(0.5999726f);
		constexpr double CrouchRateK = double(5.0024881f);   // 0x126C00
		constexpr double BrakeRateK = double(5.0353317f);    // 0x126B38, 0x126C78
		constexpr double SteerFollowRate = 2.0;
		constexpr double PrewindReleaseRate = double(6.66666651f);
		constexpr double CrouchUnwindRate = double(13.333333f);   // 0x1050E8
		constexpr double PrewindChargeRate = double(2.00050497f);
		constexpr double PrewindChargeRateRail = double(4.00100994f);
		constexpr double PrewindChargeStatBase = double(0.81243169f); // 0x105A60
		constexpr double PrewindChargeStatK = double(0.32901537f);
		constexpr double PrewindTurnClamp = 0.5;
		constexpr double PrewindMinCharge = 0.2;
		constexpr double BoostDrainPerFrame = 0.0007501507;
		constexpr double BoostTierHigh = 0.66599226;
		constexpr double BoostTierMid = 0.33362994;
		constexpr double BoostStrengthMid = double(0.60135353f);   // 0x11D038
		constexpr double BoostStrengthLow = double(0.2500284f);
		constexpr double BoostPoseOffRate = double(1.5258708f);
		constexpr double BoostPoseOnRate = double(2.1442268f);
		constexpr double BoostPoseHigh = double(0.7499248f);
		constexpr double BoostPoseMid = double(0.55031186f);
		constexpr double BoostPoseLow = double(0.6500709f);
		constexpr double BoostFlashOutSeconds = double(0.06430332f);
		// 0x105F58, 0x106140, 0x106228, 0x1062B0
		constexpr double GateRateK = double(4.437673f);
		constexpr double GateRateMin = double(0.2146618f);
		constexpr double GatePushMin = double(555.55554f);
		constexpr double GatePushK = double(2024.8037f);
		constexpr double GateStillMax = double(0.006666667f); // the rock must move on the last countdown update
		constexpr double GateGainBase = double(0.4875571f);
		constexpr double GateGainStat = double(0.71808594f);
	}

	void YawLeadConstants(int32 BoardClass, double& OutKa, double& OutKb);
	double SteerKnee(int32 BoardClass);

	struct FGroundRider
	{
		double StatSpeed = 0.5;
		double StatBrake = 0.5;
		double StatGrip = 0.5;
		double StatGate = 0.5;
		double StatLanding = 0.5;
		double StatJump = 0.5;
		double StatAero = 0.5;
		int32 BoardClass = 0;
		bool bRidingSwitch = false;
		float Byte0E = 127.5f, Byte11 = 127.5f, Byte13 = 127.5f, Byte19 = 127.5f;

		double SwitchScale() const;
		// 0x109CB8, 0x109EF8
		float SwitchScaleF() const;
	};

	// 0x10A0D8

	// 0x109878
	// normal support acceleration from the signed height h (cm) and normal speed vn
	float NormalSupport(float H, float Vn, const FSurfaceParams& S, float P7, float P8);

	float GripCurve(float AbsVf);

	float LateralGrip(const FGroundRider& R, float Vs, float Vf, float Steer, float BoostStrength, const FSurfaceParams& S);

	float LongitudinalDrag(const FGroundRider& R, float Vf, float Load, float Crouch, float Brake, float BoostStrength,
	                       int32 SurfaceId, float Height, float P8, const FSurfaceParams& S);

	// 0x109950
	// 1 within 30 deg of the race-path heading, 0 at 60, negative beyond
	float PathAlignment(float Yaw, float PathHeading);

	float Propulsion(const FGroundRider& R, float Speed, float Steer, float Brake, float BoostStrength,
	                 float BurstTimer, float Alignment, float ForwardZ, bool bPushAnim, const FSurfaceParams& S);


	float YawIncrement(const FVector3f& V, const FVector3f& N, const FVector3f& F, const FVector3f& FallLine, float Steer,
	                   float Crouch, int32 BoardClass, float Dt);

	SSXTRICKYREBUILD_API double LaunchImpulse(double Speed, double Charge, double StatJump, double JMin);

	SSXTRICKYREBUILD_API FVector LaunchVelocity(const FVector& V, const FVector& N, const FVector& F, const FVector& U, double J, bool bOnRail,
	                       bool* OutWall = nullptr);

	SSXTRICKYREBUILD_API double ApplyLaunch(FRiderSim& Sim, double JMin, double StatJump);

	enum class ELandingKind : uint8 { Normal, Hard, WipeOut };
	struct FLandingResult
	{
		ELandingKind Kind = ELandingKind::Normal;
		double SpeedFactor = 1.0;
		double Absorb = 0.25;
	};
	// pitch sign-corrected: negative = nose up; yaw error in [0, pi]
	SSXTRICKYREBUILD_API FLandingResult ClassifyLanding(double YawErr, double Pitch, double ImpactVn, double StatLanding,
	                                             int32 BoardClass = 0, bool bRidingSwitch = false);
	SSXTRICKYREBUILD_API double LandingSpeedFactor(double YawErr, double Pitch);
	SSXTRICKYREBUILD_API double LandingAbsorb(double ImpactVn);
}
