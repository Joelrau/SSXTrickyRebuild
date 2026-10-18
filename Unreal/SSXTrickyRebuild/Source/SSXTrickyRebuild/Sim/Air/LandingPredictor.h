// 0x123B10
#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderWorld.h"

class IRiderWorld;

namespace SSXAir
{
	constexpr double PredictorFirstT = 8.0 / 60.0;
	constexpr int32 PredictorChords = 25;
	constexpr double PredictorGrowth = 1.28;
	constexpr double PredictorStretch = 0.02;
	constexpr double ApexSearchMax = 4.0;
	constexpr int32 ApexIterations = 10;
	constexpr double ApexTolerance = 0.01;

	struct FLandingPredictorResult
	{
		bool bHit = false;
		double HitTime = 0.0;          // seconds from the launch state (refined if enabled)
		double CoarseHitTime = 0.0;
		FVector Point = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		int32 Surface = -1;
		double ApexTime = 0.0;
		int32 ChordIndex = -1;
	};

	SSXTRICKYREBUILD_API FLandingPredictorResult PredictLanding(const IRiderWorld* World, const FVector& Position,
	                                                      const FVector& Velocity, bool bRefineHit,
	                                                      ERiderLineQuery QueryType = ERiderLineQuery::Coarse);
}
