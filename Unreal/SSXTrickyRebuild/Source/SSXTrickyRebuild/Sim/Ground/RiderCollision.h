#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderSim.h"

namespace SSXAnim { struct FAflClip; }

namespace SSXRiderHit
{
	constexpr double SeparationShare = 0.55;
	constexpr int32 PairCooldownFrames = 3;
	constexpr double ImmovableMass = 1e30;
	constexpr double HitClamp = 500.0;           // cm/s
	constexpr double HitLaunch = 1000.0;
	constexpr double HitLaunchHeadingDeg = 45.0;
	constexpr double HitBump = 167.31738;
	constexpr double HitKnockdown = 733.9661;
	constexpr double HitKnockdownMode0 = 530.50287;   // event mode 0 (practice knockdown)
	constexpr double KnockdownReward = 1.0;
	// 0x127918, 0x123FC8
	constexpr double ShoveAlong = 150.0;         // cm along the attacker's body up (|d.Z|)
	constexpr double ShoveLateral = 180.0;       // cm off the body up axis
	constexpr double ShoveAngle = 0.9599311;   // rad (55 deg) between the target angle and +0x1E0
	constexpr double ShoveJ0 = 108.58082;
	constexpr double ShoveJ0Mode0 = 250.59537;
	constexpr double ShoveRelGain = 6.601650;
	constexpr double ShoveScaleA = 0.54663557;
	constexpr double ShoveScaleB = 0.739932;
	constexpr int32 ShovePairGap = 3;

	enum class EHit : uint8
	{
		None,
		Push,
		Bump,
		Knockdown,
		Launch,
	};

	SSXTRICKYREBUILD_API double PairMass(const FRiderSim& Sim);
	// 0x11D998, 0x1065C8
	SSXTRICKYREBUILD_API bool IsImmovable(const FRiderSim& Sim);

	SSXTRICKYREBUILD_API bool RiderShape(const FRiderSim& Sim, FRiderSphereSet& Out);
	SSXTRICKYREBUILD_API bool ContactShape(const FRiderSim& Sim, FRiderSphereSet& Out);
	// 0x124920
	SSXTRICKYREBUILD_API bool HeadingWithin(double Yaw, double Bearing);
	SSXTRICKYREBUILD_API FVector HitPoint(const FRiderState& Target, const FVector& U, double RivalDist);

	// 0x236A00
	SSXTRICKYREBUILD_API bool SphereContact(const FRiderSphereSet& A, const FRiderSphereSet& B, FVector& OutD, FVector& OutPoint);

	SSXTRICKYREBUILD_API EHit ApplyRiderHit(FRiderSim& Target, double J, const FVector& U, const FVector& ContactPoint, bool bMode0,
	                                  bool bHeadingOk);

	struct FPairResult
	{
		bool bContact = false;
		bool bImpulse = false;
		FVector D = FVector::ZeroVector;
		FVector Point = FVector::ZeroVector;
		double JA = 0.0, JB = 0.0;
		EHit HitA = EHit::None, HitB = EHit::None;
	};

	SSXTRICKYREBUILD_API FPairResult BodyContact(FRiderSim& A, FRiderSim& B, bool bImpulseAllowed, bool bMode0, bool bHeadingOkA,
	                                       bool bHeadingOkB, double DistA = -1.0, double DistB = -1.0);

	SSXTRICKYREBUILD_API void Exchange(FRiderSim& A, FRiderSim& B, const FVector& U, const FVector& Point, bool bMode0, bool bHeadingOkA,
	                             bool bHeadingOkB, FPairResult& R, double DistA = -1.0, double DistB = -1.0);

	SSXTRICKYREBUILD_API bool ShoveInRange(const FRiderState& A, const FRiderState& B);
	SSXTRICKYREBUILD_API double ShoveImpulse(const FRiderSim& A, const FRiderSim& B, bool bMode0, FVector& OutU);

	// a = atan2(side, fwd): side -1 (raw 3, stick right) = right, fwd +1 (raw 1, right stick UP) = ahead;
	// |a| < 30 deg 0x207 (toward the nose), |a| > 150 deg 0x208, else 0x205 / 0x206 by side and stance
	SSXTRICKYREBUILD_API bool ShoveInput(FRiderSim& Sim, int32 Side, int32 Fwd);
	SSXTRICKYREBUILD_API void ShoveWindow(int32 Anim, int32 BoardClass, int32& OutOpen, int32& OutClose);
	SSXTRICKYREBUILD_API void ShoveWindowOfClip(const SSXAnim::FAflClip& Clip, int32& OutOpen, int32& OutClose);
	SSXTRICKYREBUILD_API EHit ShoveHit(FRiderSim& A, FRiderSim& B, bool bMode0, bool bHeadingOk, double* OutJ = nullptr, double DistB = -1.0);
	SSXTRICKYREBUILD_API void TickShove(FRiderSim& Sim);
}
