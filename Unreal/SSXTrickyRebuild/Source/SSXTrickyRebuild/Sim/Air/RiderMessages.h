#pragma once

#include "CoreMinimal.h"

namespace SSXMsg
{
	enum : int32
	{
		PointsLost = 1,
		Points = 2,
		PointsRepeat = 3,
		Knockdown = 4,
		Crash = 5,
		ShowoffPoints = 6,
		TrickNew = 7,
		TrickRepeat = 8,
		ShowoffTime = 9,
		Checkpoint = 0xA,
		TimeBonus = 0xB,    // showoff checkpoint "TIME BONUS" (value = seconds), life 2.5
		Multiplier = 0xC,   // multiplier X<n>, life -1 (never ages)
		Laps = 0xD,
		Combo = 0xE,
		ComboBonus = 0xF,
		BigAir = 0x10,
		Camera = 0x11,
	};

	constexpr float LifePointsLost = 1.5f;
	constexpr float LifePoints = 2.5f;
	constexpr float LifeKnockdown = 2.1f;
	constexpr float LifeCrash = 1.5f;
	constexpr float LifeCheckpoint = 5.0f;
	constexpr float LifeTimeBonus = 2.5f;
	constexpr float LifeLaps = 2.5f;
	constexpr float LifeBonus = 2.2f;
	constexpr float LifeCamera = 1.2f;
	constexpr float LifeNever = -1.f;
	SSXTRICKYREBUILD_API float LifeTrickName();
	// FUN_00156e80
	constexpr float AgeStep = 1.f / 60.f;
}

struct SSXTRICKYREBUILD_API FRiderMessage
{
	int32 Type = 0;
	int32 Value = 0;
	float Life = 0.f;    // seconds; <= 0 never ages
	float Elapsed = 0.f; // seconds
	int32 Extra = 0;
};

struct SSXTRICKYREBUILD_API FRiderMessages
{
	static constexpr int32 NumSlots = 12;
	FRiderMessage Slots[NumSlots];

	// FUN_00157330
	int32 Push(int32 Type, int32 Value, float Life, int32 Extra = 0);
	// FUN_00156e80
	void Age();
	// FUN_00155a20, FUN_001553a8
	void Clear();
	// FUN_00157308, 0x157008
	void RemoveType(int32 Type);
	const FRiderMessage* Find(int32 Type) const;

	// FUN_00233220
	TFunction<void(int32 Type, int32 Value)> OnPush;
};
