#include "Input/SSXKeyBinds.h"

#include "Input/PadMapper.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SSXKeyBinds)

namespace SSXKeyBinds
{
	namespace
	{
		FSet& Live()
		{
			static FSet S = []()
			{
				FSet Out = Defaults();
				const TArray<FString>& Names = GetDefault<USSXKeyBindSettings>()->Keys;
				if (!GIsAutomationTesting && Names.Num() == NumButtons * NumSlots)
				{
					for (int32 R = 0; R < NumButtons; ++R)
					{
						for (int32 Sl = 0; Sl < NumSlots; ++Sl)
						{
							const FKey K(*Names[R * NumSlots + Sl]);
							Out.Keys[R][Sl] = K.IsValid() && !IsReserved(K) ? K : EKeys::Invalid;
						}
					}
				}
				return Out;
			}();
			return S;
		}
	}

	const FButton& Button(int32 Row)
	{
		using namespace SSXPadButton;
		static const FButton T[NumButtons] = {
			{ Up, TEXT("UP") }, { Down, TEXT("DOWN") }, { Left, TEXT("LEFT") }, { Right, TEXT("RIGHT") },
			{ Cross, TEXT("CROSS") }, { Square, TEXT("SQUARE") }, { Circle, TEXT("CIRCLE") }, { Triangle, TEXT("TRIANGLE") },
			{ L1, TEXT("L1") }, { R1, TEXT("R1") }, { L2, TEXT("L2") }, { R2, TEXT("R2") },
			{ Select, TEXT("SELECT") }, { Start, TEXT("START") } };
		return T[FMath::Clamp(Row, 0, NumButtons - 1)];
	}

	const FSet& Defaults()
	{
		static const FSet D = []()
		{
			FSet S;
			const FKey T[NumButtons][NumSlots] = {
				{ EKeys::W, EKeys::Up }, { EKeys::S, EKeys::Down }, { EKeys::A, EKeys::Left }, { EKeys::D, EKeys::Right },
				{ EKeys::SpaceBar, EKeys::Invalid }, { EKeys::LeftShift, EKeys::Invalid }, { EKeys::F, EKeys::Invalid },
				{ EKeys::G, EKeys::Invalid }, { EKeys::Q, EKeys::Invalid }, { EKeys::E, EKeys::Invalid },
				{ EKeys::Z, EKeys::Invalid }, { EKeys::C, EKeys::Invalid }, { EKeys::R, EKeys::Invalid },
				{ EKeys::Enter, EKeys::Invalid } };
			for (int32 R = 0; R < NumButtons; ++R)
			{
				for (int32 Sl = 0; Sl < NumSlots; ++Sl) { S.Keys[R][Sl] = T[R][Sl]; }
			}
			return S;
		}();
		return D;
	}

	const FSet& Current() { return Live(); }

	void SetCurrent(const FSet& In) { Live() = In; }

	bool IsReserved(const FKey& Key)
	{
		return !Key.IsValid() || Key.IsGamepadKey() || Key.IsMouseButton() || Key.IsAxis1D() || Key.IsAxis2D()
			|| Key == EKeys::Escape || Key == EKeys::Delete || Key == EKeys::BackSpace
			|| Key == EKeys::I || Key == EKeys::J || Key == EKeys::K || Key == EKeys::L;
	}

	void Assign(FSet& Set, int32 Row, int32 Slot, const FKey& Key)
	{
		const FKey Old = Set.Keys[Row][Slot];
		if (Key.IsValid())
		{
			for (int32 R = 0; R < NumButtons; ++R)
			{
				for (int32 Sl = 0; Sl < NumSlots; ++Sl)
				{
					if ((R != Row || Sl != Slot) && Set.Keys[R][Sl] == Key) { Set.Keys[R][Sl] = Old; }
				}
			}
		}
		Set.Keys[Row][Slot] = Key;
	}

	uint32 PadBits(const FKey& Key)
	{
		if (!Key.IsValid())
		{
			return 0;
		}
		uint32 Bits = 0;
		const FSet& S = Current();
		for (int32 R = 0; R < NumButtons; ++R)
		{
			for (int32 Sl = 0; Sl < NumSlots; ++Sl)
			{
				if (S.Keys[R][Sl] == Key) { Bits |= Button(R).Bit; }
			}
		}
		return Bits;
	}

	FString KeyText(const FKey& Key)
	{
		return Key.IsValid() ? Key.GetDisplayName(false).ToString().ToUpper() : FString(TEXT("-"));
	}
}

void USSXKeyBindSettings::Save()
{
	USSXKeyBindSettings* S = GetMutableDefault<USSXKeyBindSettings>();
	const SSXKeyBinds::FSet& Cur = SSXKeyBinds::Current();
	S->Keys.Reset();
	for (int32 R = 0; R < SSXKeyBinds::NumButtons; ++R)
	{
		for (int32 Sl = 0; Sl < SSXKeyBinds::NumSlots; ++Sl)
		{
			S->Keys.Add(Cur.Keys[R][Sl].IsValid() ? Cur.Keys[R][Sl].ToString() : FString(TEXT("None")));
		}
	}
	S->SaveConfig();
}
