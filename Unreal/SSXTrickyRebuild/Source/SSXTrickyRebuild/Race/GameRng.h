// 0x345E58, 0x182858, 0x2E9D78, 0x345E70
#pragma once

#include "CoreMinimal.h"

struct FDateTime;

struct SSXTRICKYREBUILD_API FGameRng
{
	uint32 S[6];

	// 0x3A4A00
	FGameRng();
	explicit FGameRng(uint32 InSeed) { Seed(InSeed); }

	// 0x2402C0
	void Seed(uint32 X);
	// 0x2401E0
	uint32 Next();
	uint32 RandInt(uint32 N) { return N ? Next() % N : 0u; }
	// 0x23D4F8
	float Uniform(float Lo, float Hi);

	// 0x21E6C0
	static FGameRng& StreamA();
	static FGameRng& StreamB();
	static uint32 ClockSeed(uint32 W0, uint32 W1) { return W0 ^ (W1 << 3); }
	static uint32 BootSeed(const FDateTime& RtcTime);
};
