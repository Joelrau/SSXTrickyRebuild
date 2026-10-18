#include "Race/GameRng.h"

#include "Misc/DateTime.h"

namespace
{
	// 0x2402C0, 0x3A4A00
	constexpr uint32 SeedAdd[6] = { 0xF22D0E56u, 0x96041893u, 0x3DF3B646u, 0x40DDE76Du, 0x97327AE1u, 0xD1A9FBE7u };
	constexpr uint32 DefaultState[6] = { 0xF22D0E56u, 0x883126E9u, 0xC624DD2Fu, 0x0702C49Cu, 0x9E353F7Du, 0x6FDF3B64u };
}

FGameRng::FGameRng()
{
	FMemory::Memcpy(S, DefaultState, sizeof(S));
}

void FGameRng::Seed(uint32 X)
{
	uint32 Acc = X;
	for (int32 I = 0; I < 6; ++I)
	{
		Acc += SeedAdd[I];
		S[I] = Acc;
	}
}

uint32 FGameRng::Next()
{
	const uint32 Old5 = S[5], Old4 = S[4];
	uint32 A = Old5 + Old4;
	uint32 C = (A < Old5 || A < Old4) ? 1u : 0u;
	S[4] = A;
	for (int32 I = 3; I >= 0; --I)
	{
		const uint32 Addend = S[I];
		A = A + Addend + C;
		C = A < Addend ? 1u : 0u;
		S[I] = A;
	}
	S[5] = Old5 + 1u;
	if (S[5] == 0u)
	{
		for (int32 I = 4; I >= 0; --I)
		{
			S[I] += 1u;
			if (S[I] != 0u)
			{
				break;
			}
		}
		A = S[0];
	}
	return A;
}

float FGameRng::Uniform(float Lo, float Hi)
{
	const uint32 Bits = (Next() & ((1u << 23) - 1u)) | (127u << 23);
	float F;
	FMemory::Memcpy(&F, &Bits, sizeof(F));
	return Lo + (Hi - Lo) * (F - 1.f);
}

uint32 FGameRng::BootSeed(const FDateTime& T)
{
	auto Bcd = [](int32 V) { return uint32(((V / 10) % 10) << 4 | (V % 10)); };
	// 0x182A20
	const uint32 W0 = Bcd(T.GetSecond()) << 8 | Bcd(T.GetMinute()) << 16 | Bcd(T.GetHour()) << 24;
	const uint32 W1 = Bcd(T.GetDay()) << 8 | Bcd(T.GetMonth()) << 16 | Bcd(T.GetYear() % 100) << 24;
	return ClockSeed(W0, W1);
}

FGameRng& FGameRng::StreamA()
{
	// 0x182858, 0x2E9D78
	static FGameRng A(BootSeed(FDateTime::UtcNow() + FTimespan::FromHours(9.0)));
	return A;
}

FGameRng& FGameRng::StreamB()
{
	static FGameRng B;
	return B;
}
