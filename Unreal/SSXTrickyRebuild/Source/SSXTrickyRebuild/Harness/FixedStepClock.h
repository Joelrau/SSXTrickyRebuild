#pragma once

#include "CoreMinimal.h"

// Fixed 60 Hz step accumulator with a per-render-frame step cap; the remainder gives the render interpolation alpha.
struct FFixedStepClock
{
	double StepSeconds = 1.0 / 60.0;
	int32 MaxStepsPerFrame = 4;
	double Accumulator = 0.0;

	int32 Advance(double DeltaSeconds)
	{
		Accumulator += FMath::Max(DeltaSeconds, 0.0);
		int32 Steps = int32(FMath::FloorToDouble(Accumulator / StepSeconds));
		if (Steps > MaxStepsPerFrame)
		{
			Steps = MaxStepsPerFrame;
			Accumulator = FMath::Min(Accumulator - Steps * StepSeconds, StepSeconds * 0.999);
		}
		else
		{
			Accumulator -= Steps * StepSeconds;
		}
		return Steps;
	}

	double Alpha() const { return FMath::Clamp(Accumulator / StepSeconds, 0.0, 1.0); }

	void Reset() { Accumulator = 0.0; }
};
