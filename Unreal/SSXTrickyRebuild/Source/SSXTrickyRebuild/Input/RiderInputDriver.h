#pragma once

#include "CoreMinimal.h"
#include "Input/PadMapper.h"
#include "Sim/RiderSim.h"

class SSXTRICKYREBUILD_API FRiderInputDriver
{
public:
	FRiderInputDriver();

	void Install(FRiderSim& Sim, TFunctionRef<void(FRiderSim&)> Register);

	void Step(FRiderSim& Sim, const FPadActions& Actions);

	void StepWord(FRiderSim& Sim, uint32 Word);

	ERiderControl GetWordControl() const { return Shared->WordControl; }
	uint32 GetLastWord() const { return LastWord; }
	const FRiderSim* GetRegistry() const { return Shared->Registry.Get(); }

	struct FShared
	{
		const FPadActions* Current = nullptr;
		ERiderControl WordControl = ERiderControl::None;
		TSharedPtr<FRiderSim> Registry;
		FRiderInputWord* PlayInput = nullptr;
		uint32 PlayWord = 0;
	};

private:
	TSharedRef<FShared> Shared;
	uint32 LastWord = 0;
};
