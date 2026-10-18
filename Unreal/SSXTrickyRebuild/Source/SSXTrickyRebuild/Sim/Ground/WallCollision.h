// 0x126250, 0x126540, 0x124DD0
#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderState.h"
#include "Sim/RiderWorld.h"

class FRiderSim;
namespace SSXAnim { struct FRiderAnimChannel; }

namespace SSXGround
{
	constexpr double WallPushOutK = 1.1;
	constexpr double WallBounceK = 0.5;
	constexpr double WallBounceMin = 55.5556;
	constexpr double WallFilterDot = 0.8;
	constexpr double StuckBAirSpeed = 833.33;
	constexpr double StuckBK = 2.5;
	constexpr double StuckBMinZ = 0.2;
	constexpr double ImpactBumpK = 833.33;
	constexpr double ImpactWipeOutBase = 1944.44;
	constexpr double BumpBoost = -0.02;
	constexpr double BumpAxisCos = 0.707;
	constexpr double HardBumpTangentSpeed = 1111.1;
	constexpr double HardBumpSpeed = 1388.9;
	constexpr double HeadingTargetDeg = 110.0;
	constexpr double HeadingMaxStepDeg = 20.0;

	// 0x125C48
	SSXTRICKYREBUILD_API void FeedStuckA(FRiderState& St, const FVector& M);

	// 0x1257A0
	SSXTRICKYREBUILD_API double TurnAwayFromWall(FRiderSim& Sim, const FVector& M);

	SSXTRICKYREBUILD_API void WallImpact(FRiderSim& Sim, SSXAnim::FRiderAnimChannel* Anim, double Bounce, const FRiderImpact& Info, const FVector& M, const FVector& Up);

	SSXTRICKYREBUILD_API void BumpReaction(FRiderSim& Sim, SSXAnim::FRiderAnimChannel* Anim, const FVector& T, bool bBoostLoss = true);

	SSXTRICKYREBUILD_API bool WallCollision(FRiderSim& Sim, int32 Mask, SSXAnim::FRiderAnimChannel* Anim, FRiderWorldHit* OutHit = nullptr);

	SSXTRICKYREBUILD_API double RiderCollisionMass(const FRiderProfile& Profile, double BoostStrength);

	// 0x125088
}
