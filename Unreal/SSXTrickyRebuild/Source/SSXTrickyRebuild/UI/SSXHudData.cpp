#include "UI/SSXHudData.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SSXFrontEndData.h"

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

	struct FTrickField { uint32 Table; int32 Word; int32 Shift; uint32 Mask; };
	constexpr FTrickField TrickFields[14] = {
		{ 0x320B08, 1, 12, 15 }, { 0x320B20, 1, 4, 15 }, { 0x320970, 1, 16, 15 }, { 0x320988, 1, 27, 7 },
		{ 0x320998, 0, 24, 15 }, { 0x3209C8, 1, 24, 7 }, { 0x3209D8, 1, 8, 15 }, { 0x3209F0, 0, 0, 0xFF },
		{ 0x320AA8, 0, 28, 15 }, { 0x320B58, 0, 8, 0xFF }, { 0x320B50, 1, 30, 3 }, { 0x320B58, 0, 16, 0xFF },
		{ 0x320AD8, 1, 20, 15 }, { 0x320AE8, 1, 0, 15 },
	};
}

FString FSSXHudData::DefaultDir()
{
	return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("hud"));
}

const FSSXHudRec& FSSXHudData::L(int32 Layout, int32 N) const
{
	static const FSSXHudRec Zero;
	return (Layout >= 0 && Layout < NumLayouts && N >= 0 && N < NumRecs) ? Layouts[Layout][N] : Zero;
}

void FSSXHudData::BuildBoostTable(TArray<FSSXHudBoostSeg>& T)
{
	// FUN_0019e888
	if (T.Num() == 0)
	{
		return;
	}
	constexpr float Seg = 0.071428575f;
	auto TierOf = [](float Hi) { return Hi >= 0.66599226f ? 2 : (Hi >= 0.33362994f ? 1 : 0); };
	static const int32 Sprites[3] = { 0x14, 0x13, 0x12 };
	float PrevHi = Seg;
	float Y = T[0].Dy;
	T[0].Sprite = Sprites[TierOf(PrevHi)];
	for (int32 I = 1; I < T.Num(); ++I)
	{
		const float Hi = PrevHi + Seg;
		const int32 Tier = TierOf(Hi);
		Y -= Tier != TierOf(PrevHi) ? 28.f : 18.f;
		T[I].Sprite = Sprites[Tier];
		T[I].Dy = Y;
		PrevHi = Hi;
	}
}

bool FSSXHudData::Load(const FString& InDir)
{
	bLoaded = false;
	const FString Dir = InDir.IsEmpty() ? DefaultDir() : InDir;
	const TSharedPtr<FJsonObject> Lay = ReadJson(FPaths::Combine(Dir, TEXT("hud_layout.json")));
	const TSharedPtr<FJsonObject> Names = ReadJson(FPaths::Combine(Dir, TEXT("trick_names.json")));
	if (!Lay.IsValid() || !Names.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Ls = Lay->GetArrayField(TEXT("layouts"));
	for (int32 K = 0; K < NumLayouts && K < Ls.Num(); ++K)
	{
		const TArray<TSharedPtr<FJsonValue>>& Rs = Ls[K]->AsArray();
		for (int32 N = 0; N < NumRecs && N < Rs.Num(); ++N)
		{
			const TSharedPtr<FJsonObject> O = Rs[N]->AsObject();
			FSSXHudRec& R = Layouts[K][N];
			R.X = (float)O->GetNumberField(TEXT("x"));
			R.Y = (float)O->GetNumberField(TEXT("y"));
			R.Flags = (uint32)O->GetNumberField(TEXT("flags"));
			R.Sx = (float)O->GetNumberField(TEXT("sx"));
			R.Sy = (float)O->GetNumberField(TEXT("sy"));
		}
	}
	Boost.Reset();
	for (const TSharedPtr<FJsonValue>& V : Lay->GetArrayField(TEXT("boost")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FSSXHudBoostSeg& S = Boost.AddDefaulted_GetRef();
		S.Lo = (float)O->GetNumberField(TEXT("lo"));
		S.Hi = (float)O->GetNumberField(TEXT("hi"));
		S.Sprite = O->GetIntegerField(TEXT("sprite"));
		S.Dx = (float)O->GetNumberField(TEXT("dx"));
		S.Dy = (float)O->GetNumberField(TEXT("dy"));
	}
	BuildBoostTable(Boost);
	Letters.Reset();
	for (const TSharedPtr<FJsonValue>& V : Lay->GetArrayField(TEXT("letters")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		Letters.Add({ O->GetIntegerField(TEXT("rec")), O->GetIntegerField(TEXT("dark")), O->GetIntegerField(TEXT("lit")) });
	}
	SlotBase = (uint32)Names->GetNumberField(TEXT("base"));
	Slots.Reset();
	for (const TSharedPtr<FJsonValue>& V : Names->GetArrayField(TEXT("slots")))
	{
		Slots.Add(V->IsNull() ? TOptional<FString>() : TOptional<FString>(V->AsString()));
	}
	const TSharedPtr<FJsonObject>* Units = nullptr;
	if (Names->TryGetObjectField(TEXT("units"), Units))
	{
		UnitKmh = (*Units)->GetStringField(TEXT("kmh"));
		UnitMph = (*Units)->GetStringField(TEXT("mph"));
	}
	bLoaded = Boost.Num() == 14 && Letters.Num() == 6 && Slots.Num() > 0;
	return bLoaded;
}

FString FSSXHudData::Slot(uint32 Va) const
{
	if (Va < SlotBase || (Va - SlotBase) % 4 != 0)
	{
		return FString();
	}
	const int32 I = int32((Va - SlotBase) / 4);
	return Slots.IsValidIndex(I) && Slots[I].IsSet() ? Slots[I].GetValue() : FString();
}

FString FSSXHudData::ComposeTrickName(uint32 W0, uint32 W1) const
{
	FString Out;
	for (const FTrickField& F : TrickFields)
	{
		const uint32 W = F.Word ? W1 : W0;
		Out += Slot(F.Table + 4 * ((W >> F.Shift) & F.Mask));
	}
	return Out;
}

bool FSSXHudSheet::Load(const FString& Json, bool bTrickSheet)
{
	PagePaths.Reset();
	if (!Sprites.LoadJson(Json))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Root = ReadJson(Json);
	const TSharedPtr<FJsonObject>* Sheets = nullptr;
	const TSharedPtr<FJsonObject>* Pages = nullptr;
	if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("sheets"), Sheets)
		|| !(*Sheets)->TryGetObjectField(bTrickSheet ? TEXT("HUDTRICK") : TEXT("HUDGAME"), Pages))
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Pages)->Values)
	{
		PagePaths.Add(FName(*P.Key), FPaths::Combine(SSXFrontEnd::TextureDir(), P.Value->AsString()));
	}
	return true;
}
