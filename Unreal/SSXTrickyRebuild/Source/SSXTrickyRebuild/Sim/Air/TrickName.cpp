#include "Sim/Air/TrickName.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SSXFrontEndData.h"

namespace SSXTrick
{
namespace
{
	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
		{
			return nullptr;
		}
		return Root;
	}

	// FUN_001551c0
	struct FField { uint32 Table; int32 Word; int32 Shift; uint32 Mask; };
	constexpr FField Fields[14] = {
		{ 0x320B08, 1, 12, 15 }, { 0x320B20, 1, 4, 15 }, { 0x320970, 1, 16, 15 }, { 0x320988, 1, 27, 7 },
		{ 0x320998, 0, 24, 15 }, { 0x3209C8, 1, 24, 7 }, { 0x3209D8, 1, 8, 15 }, { 0x3209F0, 0, 0, 0xFF },
		{ 0x320AA8, 0, 28, 15 }, { 0x320B58, 0, 8, 0xFF }, { 0x320B50, 1, 30, 3 }, { 0x320B58, 0, 16, 0xFF },
		{ 0x320AD8, 1, 20, 15 }, { 0x320AE8, 1, 0, 15 },
	};

	int32 CDiv(int32 A, int32 B) { return A / B; }
}

const FTrickTables& FTrickTables::Get()
{
	static FTrickTables T = []()
	{
		FTrickTables X;
		X.Load(FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("hud")));
		return X;
	}();
	return T;
}

bool FTrickTables::Load(const FString& Dir)
{
	bGrids = bNames = false;
	if (const TSharedPtr<FJsonObject> G = ReadJson(FPaths::Combine(Dir, TEXT("trick_grids.json"))))
	{
		const TCHAR* Keys[2] = { TEXT("normal"), TEXT("alt") };
		bool bOk = true;
		for (int32 K = 0; K < 2; ++K)
		{
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			bOk = bOk && G->TryGetArrayField(Keys[K], Rows) && Rows->Num() == GridRows;
			for (int32 R = 0; bOk && R < GridRows; ++R)
			{
				const TArray<TSharedPtr<FJsonValue>>& Cols = (*Rows)[R]->AsArray();
				bOk = Cols.Num() == GridCols;
				for (int32 C = 0; bOk && C < GridCols; ++C)
				{
					Grid[K][R][C] = uint8(Cols[C]->AsNumber());
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Ts = nullptr;
		bOk = bOk && G->TryGetArrayField(TEXT("tiers"), Ts) && Ts->Num() == NumTiers;
		for (int32 I = 0; bOk && I < NumTiers; ++I)
		{
			const TSharedPtr<FJsonObject> O = (*Ts)[I]->AsObject();
			Tiers[I] = { float(O->GetNumberField(TEXT("min"))), float(O->GetNumberField(TEXT("a"))), float(O->GetNumberField(TEXT("r"))),
				float(O->GetNumberField(TEXT("g"))), float(O->GetNumberField(TEXT("b"))) };
		}
		bGrids = bOk;
		if (!bOk)
		{
			FMemory::Memzero(Grid, sizeof(Grid));
		}
	}
	if (const TSharedPtr<FJsonObject> N = ReadJson(FPaths::Combine(Dir, TEXT("trick_names.json"))))
	{
		SlotBase = uint32(N->GetNumberField(TEXT("base")));
		Slots.Reset();
		for (const TSharedPtr<FJsonValue>& V : N->GetArrayField(TEXT("slots")))
		{
			Slots.Add(V->IsNull() ? TOptional<FString>() : TOptional<FString>(V->AsString()));
		}
		bNames = Slots.Num() > 0;
	}
	return IsLoaded();
}

FString FTrickTables::Compose(uint32 W0, uint32 W1) const
{
	FString Out;
	for (const FField& F : Fields)
	{
		const uint32 Va = F.Table + 4 * (((F.Word ? W1 : W0) >> F.Shift) & F.Mask);
		const int32 I = int32((Va - SlotBase) / 4);
		if (Va >= SlotBase && Slots.IsValidIndex(I) && Slots[I].IsSet())
		{
			Out += Slots[I].GetValue();
		}
	}
	return Out;
}

int32 FTrickTables::PointsTier(float Points) const
{
	int32 Tier = 0;
	for (int32 I = 0; bGrids && I < NumTiers; ++I)
	{
		if (Tiers[I].Min <= Points) { Tier = I; }
	}
	return Tier;
}

int32 Round180(int32 Deg)
{
	const int32 R = Deg % 180;
	if (R < -90) { return Deg - 180 - R; }
	if (R <= 90) { return Deg - R; }
	return Deg + 180 - R;
}

FRecord BuildTrickRecord(const FRecordInput& In, const FTrickTables& T)
{
	int32 Spin = In.bSwitch ? -In.SpinDeg : In.SpinDeg;
	int32 Flip = In.SegType == 4 ? -In.FlipDeg : In.FlipDeg;
	Spin = Round180(Spin);
	Flip = Round180(Flip);
	const int32 F = FMath::Abs(CDiv(Flip, 360));
	const int32 S = FMath::Abs(CDiv(Spin, 180));
	const int32 Tn = (F >= 1 && S >= 3) ? FMath::Min(F, S / 3) : 0;
	const int32 Row = (Spin > 0 || S > 10) ? S + 10 : S;
	const int32 Col = (Flip < 0 || F > 3) ? F + 3 : F;
	int32 NGrabs = 0;
	while (NGrabs < 3 && In.Grabs[NGrabs] != 0) { ++NGrabs; }

	FRecord R;
	uint32& W0 = R.W0;
	uint32& W1 = R.W1;
	const uint32 Prefix = In.SegType == 0 ? (In.bLate ? 4u : (In.bSwitch ? 1u : 0u)) : (In.bSwitch ? 3u : 2u);
	W1 |= Prefix << 16;
	bool bOverflow = false;
	if (Row < GridRows && Col < GridCols)
	{
		W0 |= T.Grid[(In.bAltFlag && In.bAltSelect) ? 1 : 0][Row][Col];
	}
	else
	{
		bOverflow = true;
	}
	bool bSpinAfter = true;
	if ((W0 & 0xFF) == 0)
	{
		if (S > 0) { W1 |= (Spin > 0 ? 2u : 1u) << 27; }
		if (F == 0) { W0 |= uint32(S & 15) << 24; }
		W1 |= uint32(F & 7) << 24;
		if (F > 0)
		{
			const uint32 Name = (Tn > 0 && Tn == F) ? (Flip < 1 ? 3u : 4u) : (Flip < 1 ? 2u : 1u);
			W1 |= Name << 8;
		}
		bSpinAfter = F > 0;
	}
	if (bSpinAfter && F > 0 && S != 3 * F && S < 11)
	{
		W0 |= uint32(S & 15) << 28;
	}
	if (In.bToRail)
	{
		W1 |= 3u << 20;
	}
	else if ((W0 & 0xFF) == 0 && (Tn == 0 || (W0 >> 28) != 0))
	{
		if (In.bLandSwitch && !In.bSwitch) { W1 |= 2u << 20; }
		else if (S == 0 && F == 0 && NGrabs > 0) { W1 |= 1u << 20; }
	}
	if (NGrabs > 0)
	{
		W0 |= uint32(In.Grabs[0] & 0xFF) << 8;
		if (NGrabs > 1)
		{
			W1 |= 1u << 30;
			W0 |= uint32((NGrabs == 2 ? In.Grabs[1] : ComboGrab) & 0xFF) << 16;
		}
	}
	if (bOverflow)
	{
		W0 = Unnameable;
		W1 = 0;
	}
	R.bValid = S > 0 || F > 0 || NGrabs > 0;
	return R;
}

FRecord RailTrickRecord(int32 RailKind, int32 RailSpinDeg)
{
	FRecord R;
	R.W1 = (uint32(RailKind & 15) << 12) | (uint32((FMath::Abs(Round180(RailSpinDeg)) / 180) & 15) << 4);
	R.bValid = true;
	return R;
}
}
