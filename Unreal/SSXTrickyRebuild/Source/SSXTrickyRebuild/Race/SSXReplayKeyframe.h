#pragma once

#include "CoreMinimal.h"
#include "Race/GameRng.h"
#include "Race/RaceSession.h"
#include "Sim/RiderSim.h"

struct SSXTRICKYREBUILD_API FSSXRaceKeyframe
{
	FRaceSessionKeyframe Session;
	TArray<FRiderSimKeyframe> Riders;
	FGameRng RngA, RngB;
	uint32 Frame = 0;
	bool bValid = false;

	void Save(const FRaceSession& S, uint32 InFrame);
	void Load(FRaceSession& S) const;
};
