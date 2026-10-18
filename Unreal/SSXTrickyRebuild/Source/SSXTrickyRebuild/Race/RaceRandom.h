// 0x23D4D8, 0x139D58, 0x138BB8, 0x138D60, 0x11D368, 0x118830, 0x2402C0
#pragma once

#include "CoreMinimal.h"
#include "Race/GameRng.h"

struct FRaceRandom
{
	FRaceRandom() = default;
	explicit FRaceRandom(uint32 Seed) : Local(Seed), bLocal(true) {}

	// 0x2401E0, 0x345E70
	uint32 Next() { return Gen().Next(); }
	int32 Mod(uint32 N) { return N ? int32(Next() % N) : 0; }

	FGameRng& Gen() { return bLocal ? Local : FGameRng::StreamB(); }

private:
	FGameRng Local;
	bool bLocal = false;
};
