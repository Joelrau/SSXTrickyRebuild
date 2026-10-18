#include "UI/SSXFeProfile.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SSXTrickyRebuild.h"
#include "Sim/Rider/RiderProfiles.h"
#include "UI/SSXFeTypes.h"
#include "UI/SSXFeTrickBook.h"

namespace
{
	const TCHAR* const Names[] = { TEXT("Eddie"), TEXT("Kaori"), TEXT("Luther"), TEXT("Mac"), TEXT("Moby"), TEXT("Zoe"),
		TEXT("JP"), TEXT("Elise"), TEXT("Psymon"), TEXT("Seeiah"), TEXT("Brodi"), TEXT("Marisol") };
	const TCHAR* const Shorts[] = { TEXT("Eddi"), TEXT("Kaor"), TEXT("Luth"), TEXT("Mac"), TEXT("Moby"), TEXT("Zoe"),
		TEXT("JP"), TEXT("Elis"), TEXT("Psym"), TEXT("Seei"), TEXT("Brod"), TEXT("Mari") };
	// 0x3470B8
	const int32 Carousel[] = { 3, 1, 8, 10, 0, 7, 11, 9, 2, 6, 5, 4 };

	TArray<SSXFeTables::FBoard>& BoardTable()
	{
		static TArray<SSXFeTables::FBoard> T;
		if (T.Num() == 0)
		{
			T.SetNum(SSXFeTables::NumRiders * SSXFeTables::BoardsPerRider);
			for (int32 I = 0; I < T.Num(); ++I)
			{
				T[I].Index = I % SSXFeTables::BoardsPerRider;
				T[I].Cost = SSXProg::BoardCost(T[I].Index);
			}
		}
		return T;
	}
	bool GBoardsLoaded = false;

	struct FFeTables
	{
		bool bLoaded = false;
		FString Outfits[SSXFeTables::NumRiders][SSXFeTables::OutfitsPerRider];
		int32 OutfitValues[SSXFeTables::NumRiders][SSXFeTables::OutfitsPerRider] = {};
		TArray<uint32> VenueMasks;
		FString Grid;
		TArray<int32> VenueScreens;   // 0x347130
		TArray<int32> RiderSigns;     // 0x3470C8
		TArray<int32> Platforms;      // 0x347100
		int32 MartySign = INDEX_NONE; // 0x3470F8
		/** String / int tables by key (track_names, dvd_talents, dvd_char_names, dvd_jukebox.title / .artist / .length,
		 *  dvd_char_order); a NULL string entry is empty. */
		TMap<FString, TArray<FString>> Strings;
		TMap<FString, TArray<int32>> Ints;
	};
	FFeTables& Tables() { static FFeTables T; return T; }
}

const TCHAR* SSXFeTables::RiderName(int32 Char) { return Char >= 0 && Char < NumRiders ? Names[Char] : TEXT(""); }
const TCHAR* SSXFeTables::RiderShort(int32 Char) { return Char >= 0 && Char < NumRiders ? Shorts[Char] : TEXT(""); }
int32 SSXFeTables::CarouselRider(int32 Slot) { return Carousel[((Slot % NumRiders) + NumRiders) % NumRiders]; }
int32 SSXFeTables::CarouselSlot(int32 Char)
{
	for (int32 S = 0; S < NumRiders; ++S) { if (Carousel[S] == Char) { return S; } }
	return 0;
}
int32 SSXFeTables::UnlockOrder(int32 K) { return SSXProg::RiderUnlockOrder(K); }
int32 SSXFeTables::UnlockRank(int32 Char)
{
	for (int32 K = 0; K < NumRiders; ++K) { if (UnlockOrder(K) == Char) { return K; } }
	return -1;
}

const TArray<int32>& SSXFeTables::VenueOrder()
{
	static const TArray<int32> Order = { 0, 1, 2, 3, 4, 8, 5, 11, 7, 6 };   // 0x3A8AF0
	return Order;
}

int32 SSXFeTables::TrackCatalogIndex(int32 TrackId)
{
	switch (TrackId)
	{
	case 0: return 0;  case 1: return 1;  case 2: return 2;  case 3: return 3;  case 4: return 4;
	case 5: return 6;  case 6: return 8;  case 7: return 9;  case 8: return 5;  case 10: return 10; case 11: return 7;
	default: return INDEX_NONE;
	}
}

int32 SSXFeTables::TrackNameLoc(int32 TrackId)
{
	// FUN_0016B650 / 0x37EBF0
	switch (TrackId)
	{
	case 0: return 3613; case 1: return 3688; case 2: return 3576; case 3: return 3646; case 4: return 3645;
	case 5: return 3573; case 6: return 3685; case 7: return 3692; case 8: return 3689; case 9: return 3685;
	case 10: return 3610; case 11: return 3693;
	default: return 3613;
	}
}

int32 SSXFeTables::BoardCost(int32 Board) { return SSXProg::BoardCost(Board); }

int32 SSXFeTables::RankFromPoints(int32 P) { return SSXProg::RankFromPoints(P); }   // FUN_00164840

int32 SSXFeTables::BoardTypeLoc(int32 Type)
{
	switch (Type) { case 0: return 19; case 1: return 20; case 2: return 18; default: return -1; }
}

const SSXFeTables::FBoard& SSXFeTables::Board(int32 Char, int32 Slot)
{
	TArray<FBoard>& T = BoardTable();
	return T[FMath::Clamp(Char, 0, NumRiders - 1) * BoardsPerRider + FMath::Clamp(Slot, 0, BoardsPerRider - 1)];
}

bool SSXFeTables::BoardsLoaded() { return GBoardsLoaded; }

bool SSXFeTables::LoadBoardsCsv(const FString& Path)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		return false;
	}
	TArray<FBoard>& T = BoardTable();
	int32 Count = 0;
	for (const FString& L : Lines)
	{
		TArray<FString> F;
		L.ParseIntoArray(F, TEXT(","), false);
		if (F.Num() < 11 || !F[0].IsNumeric())
		{
			continue;
		}
		const int32 C = FCString::Atoi(*F[0]), S = FCString::Atoi(*F[1]);
		if (C < 0 || C >= NumRiders || S < 0 || S >= BoardsPerRider)
		{
			continue;
		}
		FBoard& B = T[C * BoardsPerRider + S];
		B.Name = F[3].TrimQuotes();
		B.Index = FCString::Atoi(*F[4]);
		B.Type = FCString::Atoi(*F[5]);
		B.Cost = FCString::Atoi(*F[6]);
		for (int32 I = 0; I < 4; ++I) { B.Stats[I] = (uint8)FCString::Atoi(*F[7 + I]); }
		++Count;
	}
	GBoardsLoaded = Count == NumRiders * BoardsPerRider;
	UE_LOG(LogSSX, Log, TEXT("[fe] board table: %d records from %s"), Count, *Path);
	return GBoardsLoaded;
}

bool SSXFeTables::LoadTablesJson(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogSSX, Warning, TEXT("[fe] no FE tables at %s (run tools/export_fe_tables.py)"), *Path);
		return false;
	}
	FFeTables& T = Tables();
	TArray<FBoard>& B = BoardTable();
	int32 Boards = 0;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("boards"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>& O = V->AsObject();
			const int32 C = O->GetIntegerField(TEXT("char")), S = O->GetIntegerField(TEXT("slot"));
			if (C < 0 || C >= NumRiders || S < 0 || S >= BoardsPerRider) continue;
			FBoard& R = B[C * BoardsPerRider + S];
			R.Name = O->GetStringField(TEXT("name"));
			R.Index = O->GetIntegerField(TEXT("index"));
			R.Type = O->GetIntegerField(TEXT("type"));
			R.Cost = O->GetIntegerField(TEXT("cost"));
			const TArray<TSharedPtr<FJsonValue>>& St = O->GetArrayField(TEXT("stats"));
			for (int32 I = 0; I < 4 && I < St.Num(); ++I) { R.Stats[I] = (uint8)St[I]->AsNumber(); }
			++Boards;
		}
	}
	GBoardsLoaded = Boards == NumRiders * BoardsPerRider;
	if (Root->TryGetArrayField(TEXT("outfits"), Arr))
	{
		for (int32 C = 0; C < NumRiders && C < Arr->Num(); ++C)
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = (*Arr)[C]->AsArray();
			for (int32 O = 0; O < OutfitsPerRider && O < Row.Num(); ++O)
			{
				T.Outfits[C][O] = Row[O]->AsObject()->GetStringField(TEXT("name"));
				T.OutfitValues[C][O] = Row[O]->AsObject()->GetIntegerField(TEXT("value"));
			}
		}
	}
	T.VenueMasks.Reset();
	if (Root->TryGetArrayField(TEXT("venue_masks"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { T.VenueMasks.Add((uint32)(int64)V->AsNumber()); }
	}
	T.Grid = Root->GetStringField(TEXT("name_grid"));
	T.VenueScreens.Reset();
	if (Root->TryGetArrayField(TEXT("venue_screens"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { T.VenueScreens.Add((int32)V->AsObject()->GetIntegerField(TEXT("instance"))); }
	}
	T.RiderSigns.Reset();
	T.MartySign = INDEX_NONE;
	{
		const TSharedPtr<FJsonObject>* Marty = nullptr;
		if (Root->TryGetObjectField(TEXT("marty_sign"), Marty) && Marty && Marty->IsValid())
		{
			T.MartySign = (int32)(*Marty)->GetIntegerField(TEXT("instance"));
		}
	}
	T.Platforms.Reset();
	if (Root->TryGetArrayField(TEXT("rider_signs"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { T.RiderSigns.Add((int32)V->AsObject()->GetIntegerField(TEXT("instance"))); }
	}
	if (Root->TryGetArrayField(TEXT("platforms"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { T.Platforms.Add((int32)V->AsObject()->GetIntegerField(TEXT("instance"))); }
	}
	T.Strings.Reset();
	T.Ints.Reset();
	for (const TCHAR* Key : { TEXT("track_names"), TEXT("dvd_talents"), TEXT("dvd_talents_full"), TEXT("dvd_char_names") })
	{
		if (Root->TryGetArrayField(Key, Arr))
		{
			TArray<FString>& L = T.Strings.Add(Key);
			for (const TSharedPtr<FJsonValue>& V : *Arr) { L.Add(V->Type == EJson::String ? V->AsString() : FString()); }
		}
	}
	if (Root->TryGetArrayField(TEXT("dvd_char_order"), Arr))
	{
		TArray<int32>& L = T.Ints.Add(TEXT("dvd_char_order"));
		for (const TSharedPtr<FJsonValue>& V : *Arr) { L.Add((int32)V->AsNumber()); }
	}
	if (Root->TryGetArrayField(TEXT("dvd_jukebox"), Arr))
	{
		for (const TCHAR* F : { TEXT("title"), TEXT("artist"), TEXT("length") })
		{
			TArray<FString>& L = T.Strings.Add(FString(TEXT("dvd_jukebox.")) + F);
			for (const TSharedPtr<FJsonValue>& V : *Arr) { L.Add(V->AsObject()->GetStringField(F)); }
		}
	}
	T.bLoaded = GBoardsLoaded && T.VenueMasks.Num() > 0 && T.Grid.Len() == 42;
	UE_LOG(LogSSX, Log, TEXT("[fe] FE tables: %d boards, %d venue masks, grid %d chars from %s"), Boards, T.VenueMasks.Num(), T.Grid.Len(), *Path);
	return T.bLoaded;
}

const TArray<FString>& SSXFeTables::StringTable(const TCHAR* Key)
{
	static const TArray<FString> Empty;
	const TArray<FString>* L = Tables().Strings.Find(Key);
	return L ? *L : Empty;
}

const TArray<int32>& SSXFeTables::IntTable(const TCHAR* Key)
{
	static const TArray<int32> Empty;
	const TArray<int32>* L = Tables().Ints.Find(Key);
	return L ? *L : Empty;
}

FString SSXFeTables::TrackTableName(int32 TrackId)
{
	const TArray<FString>& L = StringTable(TEXT("track_names"));
	return L.IsValidIndex(TrackId) ? L[TrackId] : FString();
}

FString SSXFeTables::OutfitName(int32 Char, int32 Outfit)
{
	return Char >= 0 && Char < NumRiders && Outfit >= 0 && Outfit < OutfitsPerRider ? Tables().Outfits[Char][Outfit] : FString();
}

int32 SSXFeTables::OutfitValue(int32 Char, int32 Outfit)
{
	return Char >= 0 && Char < NumRiders && Outfit >= 0 && Outfit < OutfitsPerRider ? Tables().OutfitValues[Char][Outfit] : 0;
}

uint32 SSXFeTables::VenueModeMask(int32 GameMode)
{
	const TArray<uint32>& M = Tables().VenueMasks;
	return M.IsValidIndex(GameMode) ? M[GameMode] : 0xFFFFFFFFu;
}

const FString& SSXFeTables::NameGrid() { return Tables().Grid; }

int32 SSXFeTables::VenueScreenInstance(int32 I)
{
	const TArray<int32>& V = Tables().VenueScreens;
	return V.IsValidIndex(I) ? V[I] : INDEX_NONE;
}

int32 SSXFeTables::RiderSignInstance(int32 Slot)
{
	const TArray<int32>& V = Tables().RiderSigns;
	return V.IsValidIndex(Slot) ? V[Slot] : INDEX_NONE;
}

int32 SSXFeTables::MartySignInstance() { return Tables().MartySign >= 0 ? Tables().MartySign : INDEX_NONE; }

int32 SSXFeTables::PlatformInstance(int32 Slot)
{
	const TArray<int32>& V = Tables().Platforms;
	return V.IsValidIndex(Slot) ? V[Slot] : INDEX_NONE;
}

FSSXProfile FSSXProfile::MakeDefault()
{
	// 0x161B40 / 0x166600
	FSSXProfile P;
	static_cast<FSSXSaveData&>(P) = FSSXSaveData::NewSave();
	for (int32 S = 0; S < 2; ++S) { P.Slot[S].UserName = P.GetUserName(S); }
	return P;
}

bool FSSXProfile::IsVenueUnlocked(int32 GameMode, int32 Venue, bool bUnlockAll) const
{
	return SSXProg::VenueUnlocked(*this, GameMode, Venue, Slot[0].Char, SSXFeTables::VenueModeMask(GameMode), bUnlockAll);
}

int32 FSSXProfile::VenueMedal(int32 InGameMode, int32 Venue) const
{
	const FSSXCareer* Car = SlotCareer(0);
	if (!Car || Venue < 0 || Venue >= SSXProg::NumTracks || (InGameMode != 4 && InGameMode != 5))
	{
		return SSXProg::NoMedal;
	}
	const int32 M = InGameMode == 4 ? Car->Tracks[Venue].RaceMedal : Car->Tracks[Venue].ShowoffMedal;
	return M >= SSXProg::Gold && M <= SSXProg::Bronze ? M : SSXProg::NoMedal;
}

TArray<uint8> FSSXProfile::SaveBlob() const
{
	FSSXSaveData D = *this;
	for (int32 S = 0; S < 2; ++S)
	{
		if (D.GetUserName(S) != Slot[S].UserName) { D.SetUserName(S, Slot[S].UserName); }
	}
	return D.ToBlob();
}

bool FSSXProfile::LoadBlob(const TArray<uint8>& Blob)
{
	FSSXSaveData D;
	D.Settings.Preset = Settings.Preset;
	if (!D.FromBlob(Blob, true))
	{
		return false;
	}
	static_cast<FSSXSaveData&>(*this) = D;
	for (int32 S = 0; S < 2; ++S)
	{
		Slot[S].UserName = GetUserName(S);
		// 0x161A98
		SSXProg::BuildPointSession(*this, Slot[S].Char, false, Session.Points[S]);
	}
	bSettingsDirty = false;
	return true;
}

void FSSXProfile::DropEventTricks(int32 S)
{
	// 0x165400
	if (S < 0 || S >= 2)
	{
		return;
	}
	if (FSSXCareer* C = SlotCareer(S))
	{
		C->BookDone[1] &= ~Slot[S].EventTricks;
		Slot[S].EventTricks = 0;
	}
}

uint32 FSSXProfile::LandBookTrick(int32 S, uint32 W0, uint32 W1)
{
	// 0x1579E0
	FSSXCareer* C = SlotCareer(S);
	return C ? SSXFeTrickBook::LandBookTrick(SSXFeTrickBook::FTable::Get(), *C, Slot[S].EventTricks, W0, W1) : 0u;
}

void FSSXProfile::ShowoffEndTricks(int32 S, int32 Mode, int32 FinishResult)
{
	if ((Mode == SSXProg::SEShowoff || Mode == SSXProg::WCShowoff) && FinishResult < 1)
	{
		DropEventTricks(S);
	}
}

void FSSXProfile::ReloadSettings(bool bMaxAttributes)
{
	// 0x161A98 / 0x211DB0 / 0x212210 / 0x212270
	bSettingsDirty = false;
	for (int32 S = 0; S < 2; ++S)
	{
		SSXProg::BuildPointSession(*this, Slot[S].Char, bMaxAttributes, Session.Points[S]);
	}
}

void FSSXProfile::LoadSlotConfig(int32 S, int32 Char)
{
	if (S < 0 || S >= 2 || Char < 0 || Char >= SSXProg::NumRiders)
	{
		return;
	}
	int32 Rec[5];
	FMemory::Memcpy(Rec, RiderConfig[Char], sizeof(Rec));
	Slot[S].Outfit = Rec[3];
	Slot[S].Board = Rec[4];
}

bool FSSXProfile::CommitSlotConfigs(int32 Players)
{
	bool bChanged = false;
	for (int32 C = 0; C < SSXProg::NumRiders; ++C)
	{
		int32 Src = -1;
		for (int32 S = 0; S < FMath::Min(Players, 2) && Src < 0; ++S)
		{
			if (Slot[S].Char == C) { Src = S; }
		}
		if (Src < 0)
		{
			continue;
		}
		int32 Rec[5];
		FMemory::Memcpy(Rec, RiderConfig[C], sizeof(Rec));
		Rec[3] = Slot[Src].Outfit;
		Rec[4] = Slot[Src].Board;
		if (FMemory::Memcmp(Rec, RiderConfig[C], sizeof(Rec)) != 0)
		{
			FMemory::Memcpy(RiderConfig[C], Rec, sizeof(Rec));
			bSettingsDirty = true;
			bChanged = true;
		}
	}
	return bChanged;
}

void FSSXProfile::ResetEventState()
{
	// 0x161840 / 0x16BCA8 / 0x165400
	Session.Heat = 0;
	for (int32 S = 0; S < 2; ++S)
	{
		DropEventTricks(S);
	}
}

bool FSSXProfile::BootLoadBlob(const TArray<uint8>& Blob)
{
	constexpr int32 AutoloadOffset = 0x16A4 - 8;
	if (Blob.Num() != SettingsBlobSize)
	{
		return false;
	}
	int32 Autoload = 0;
	FMemory::Memcpy(&Autoload, Blob.GetData() + AutoloadOffset, 4);
	if (Autoload == 0)
	{
		Settings.Autoload = 0;   // 0x184C34
		return false;
	}
	return LoadBlob(Blob);
}

FSSXCheats::ECode FSSXCheats::Match(const TArray<uint32>& B)
{
	using namespace SSXFe;
	// FUN_0026CF60
	static const uint32 Mmm[] = { PadCross, PadCross, PadRight, PadCross, PadCross, PadDown, PadCross, PadCross, PadLeft, PadCross, PadCross, PadUp };
	static const uint32 All[] = { PadCross, PadTriangle, PadRight, PadCircle, PadSquare, PadDown, PadTriangle, PadSquare, PadLeft, PadCircle, PadCross, PadUp };
	static const uint32 Max[] = { PadTriangle, PadTriangle, PadRight, PadTriangle, PadTriangle, PadDown, PadCross, PadCross, PadLeft, PadCross, PadCross, PadUp };
	static const uint32 Mal[] = { PadCross, PadCross, PadRight, PadCircle, PadCircle, PadDown, PadTriangle, PadTriangle, PadLeft, PadSquare, PadSquare, PadUp };
	if (B.Num() < 12)
	{
		return ECode::None;
	}
	auto Is = [&B](const uint32* Seq)
	{
		const int32 Off = B.Num() - 12;
		for (int32 I = 0; I < 12; ++I) { if (B[Off + I] != Seq[I]) { return false; } }
		return true;
	};
	if (Is(Mmm)) return ECode::MixMasterMike;
	if (Is(All)) return ECode::UnlockAll;
	if (Is(Max)) return ECode::MaxAttributes;
	if (Is(Mal)) return ECode::Mallora;
	return ECode::None;
}

void FSSXCheats::Apply(ECode C)
{
	switch (C)
	{
	case ECode::MixMasterMike: bMixMasterMike = !bMixMasterMike; break;   // 0x3352B8
	case ECode::UnlockAll: bUnlockAll = !bUnlockAll; break;
	case ECode::MaxAttributes: bMaxAttributes = !bMaxAttributes; break;
	case ECode::Mallora: bMallora = !bMallora; break;
	default: break;
	}
}
