// 0x140838
#pragma once

#include "CoreMinimal.h"

class FRiderSim;

namespace SSXTriggers
{
	constexpr double ResetDrop = 101.46;          // cm (effect 13)

	SSXTRICKYREBUILD_API void ApplyBoostBurst(FRiderSim& Sim, double Seconds);   // timer = max(timer, seconds)
	// 0x1311B0, 0x1356F8
	SSXTRICKYREBUILD_API void ApplyLaunchBoost(FRiderSim& Sim, double Seconds);
	SSXTRICKYREBUILD_API void ApplyForcedReset(FRiderSim& Sim);

	// s = target - v.dir; if s > 0: v += dir s gain x 0.016666668 (single precision); target cm/s, gain 1/s
	SSXTRICKYREBUILD_API bool BoostNodeVelocity(FVector& V, const FVector3f& Dir, float Target, float Gain);
}
