#include "Audio/SSXZoneAmbience.h"

int32 FSSXZoneAmbience::KindOf(int32 InternalType)
{
	switch (InternalType)
	{
	case 14: return 0;
	case 23: return 1;
	case 24: return 2;
	default: return INDEX_NONE;
	}
}

FSSXAmbientStart FSSXZoneAmbience::Push(int32 InKind)
{
	FSSXAmbientStart P;
	P.b3D = false;
	if (InKind == 2)
	{
		P.Bank = 2;
		P.Index = TrackBankIndex;
		P.Volume127 = DefaultVolume;
	}
	else
	{
		P.Bank = 6;
		P.Index = 0;
		P.SwapBank = InKind == 0 ? TEXT("Tunnel1") : TEXT("RiverSmall");   // 0x39F1C0 / 0x39F1D8
		P.Volume127 = SwapVolume;
	}
	return P;
}

void FSSXZoneAmbience::Trigger(int32 Racer, int32 InternalType, bool bEnter)
{
	const int32 K = KindOf(InternalType);
	if (K == INDEX_NONE || Racer < 0)
	{
		return;
	}
	if (Masks.Num() <= Racer)
	{
		Masks.SetNumZeroed(Racer + 1);
	}
	const uint32 Bit = 1u << InternalType;
	const bool bWasIn = (Masks[Racer] & Bit) != 0;
	if (bEnter)
	{
		Masks[Racer] |= Bit;
		if (!bWasIn)
		{
			Kind = K;            // 0x21D0B0
			bPending = true;
		}
	}
	else
	{
		Masks[Racer] &= ~Bit;
		if (bWasIn)
		{
			StopVoice();
		}
	}
}

void FSSXZoneAmbience::Update()
{
	if (!bPending)
	{
		return;
	}
	// 0x21D59C
	Voice = Start ? Start(Push(Kind)) : INDEX_NONE;
	bPending = false;
}

void FSSXZoneAmbience::StopVoice()
{
	if (Voice >= 0 && Stop)
	{
		Stop(Voice, StopFade);
	}
	Voice = INDEX_NONE;
}

void FSSXZoneAmbience::Reset()
{
	Voice = INDEX_NONE;
	bPending = false;
	Kind = 0;
	Masks.Reset();
}

void FSSXZoneAmbience::Restart()
{
	StopVoice();
	Masks.Reset();
}
