#pragma once

#include "CoreMinimal.h"
#include "SSXAudioTypes.generated.h"

// 0x210C38
UENUM(BlueprintType)
enum class ESSXSnowSurface : uint8
{
	Pack, Powder, Loose, Ice, Metal, Wood, Rail, Rock, Glass, Chute,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ESSXSnowMode : uint8
{
	Glide, AIGlide, Carve,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ESSXRiderAudioEvent : uint8
{
	Jump,
	BigAirStart,
	Land,
	HardLand,
	TrickLanded,
	Wipeout,
	Recover,
	BoostStart,
	RailStart,
	RailEnd,
	Finish,
	TrickyStart,
};
