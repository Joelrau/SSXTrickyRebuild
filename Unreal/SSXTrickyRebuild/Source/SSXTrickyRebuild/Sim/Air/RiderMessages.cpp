#include "Sim/Air/RiderMessages.h"

float SSXMsg::LifeTrickName()
{
	const uint32 Bits = 0x404004DAu;
	float F;
	FMemory::Memcpy(&F, &Bits, sizeof(F));
	return F;
}

int32 FRiderMessages::Push(int32 Type, int32 Value, float Life, int32 Extra)
{
	int32 Slot = -1;
	if (Type == SSXMsg::TrickNew || Type == SSXMsg::TrickRepeat)
	{
		for (int32 I = 0; I < NumSlots && Slot < 0; ++I)
		{
			if (Slots[I].Type == SSXMsg::TrickNew || Slots[I].Type == SSXMsg::TrickRepeat) { Slot = I; }
		}
	}
	else if (Type == SSXMsg::Multiplier)
	{
		for (int32 I = 0; I < NumSlots && Slot < 0; ++I)
		{
			if (Slots[I].Type == SSXMsg::Multiplier) { Slot = I; }
		}
	}
	if (Slot < 0)
	{
		int32 LastLive = -1;
		for (int32 I = 0; I < NumSlots; ++I)
		{
			if (Slots[I].Type == 0)
			{
				Slot = I;
				break;
			}
			if (Slots[I].Elapsed / Slots[I].Life < 1.f)
			{
				LastLive = I;
			}
		}
		if (Slot < 0)
		{
			Slot = LastLive;
		}
	}
	if (Slot >= 0)
	{
		Slots[Slot] = { Type, Value, Life, 0.f, Extra };
	}
	if (OnPush)
	{
		OnPush(Type, Value);
	}
	return Slot;
}

void FRiderMessages::Age()
{
	for (FRiderMessage& M : Slots)
	{
		if (M.Type != 0 && M.Life > 0.f)
		{
			M.Elapsed += SSXMsg::AgeStep;
			if (M.Elapsed >= M.Life)
			{
				M = FRiderMessage();
			}
		}
	}
}

void FRiderMessages::Clear()
{
	for (FRiderMessage& M : Slots)
	{
		M = FRiderMessage();
	}
}

void FRiderMessages::RemoveType(int32 Type)
{
	for (FRiderMessage& M : Slots)
	{
		if (M.Type == Type)
		{
			M.Type = 0;
		}
	}
}

const FRiderMessage* FRiderMessages::Find(int32 Type) const
{
	for (const FRiderMessage& M : Slots)
	{
		if (M.Type == Type) { return &M; }
	}
	return nullptr;
}
