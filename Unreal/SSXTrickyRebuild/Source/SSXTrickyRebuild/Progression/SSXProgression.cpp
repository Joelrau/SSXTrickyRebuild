#include "Progression/SSXProgression.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/GameRng.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Rider/RiderProfiles.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFrontEndData.h"

namespace
{
	const int32 Circuit[SSXProg::NumCircuit] = { 0, 1, 2, 3, 4, 8, 5, 11 };   // 0x3352F0, 0x335370
	struct FThresh { int32 Venue; int32 T[3]; };
	const FThresh Showoff[] = {
		{ 0, { 55000, 40000, 25000 } }, { 1, { 95000, 65000, 35000 } }, { 2, { 225000, 150000, 75000 } },
		{ 3, { 225000, 150000, 75000 } }, { 4, { 275000, 175000, 125000 } }, { 8, { 350000, 225000, 100000 } },
		{ 5, { 175000, 115000, 75000 } }, { 11, { 500000, 300000, 150000 } },
		{ 6, { 800000, 500000, 250000 } },   // 0x16BB58
	};
	const int32 MedalPts[4] = { 15, 10, 5, 0 };                                  // 0x37EE40
	const int32 RankPts[10] = { 240, 200, 160, 120, 90, 60, 35, 20, 5, 0 };      // 0x16C420, FUN_00164840
	const int32 UnlockOrder[SSXProg::NumRiders] = { 3, 4, 7, 0, 10, 5, 6, 1, 11, 8, 9, 2 };   // 0x3352C0
	const int32 Costs[12] = { 0, 0, 5, 20, 35, 60, 90, 120, 160, 200, 240, 999 };            // 0x332154

	uint16 CrcTable[256];
	bool BuildCrc()
	{
		for (int32 I = 0; I < 256; ++I)
		{
			uint16 C = (uint16)I;
			for (int32 K = 0; K < 8; ++K) { C = (C & 1) ? (uint16)((C >> 1) ^ 0xA001) : (uint16)(C >> 1); }
			CrcTable[I] = C;
		}
		return true;
	}

	struct FDefaults
	{
		bool bTried = false;
		bool bLoaded = false;
		FString Names[SSXProg::NumTracks][3][5];
		int32 Values[SSXProg::NumTracks][3][5] = {};
	};
	FDefaults& Defaults() { static FDefaults D; return D; }

	struct FW
	{
		TArray<uint8>& B;
		void U8(int32 O, uint8 V) { B[O] = V; }
		void I32(int32 O, int32 V) { FMemory::Memcpy(B.GetData() + O, &V, 4); }
		void U32(int32 O, uint32 V) { FMemory::Memcpy(B.GetData() + O, &V, 4); }
		void F32(int32 O, float V) { FMemory::Memcpy(B.GetData() + O, &V, 4); }
		void Bytes(int32 O, const uint8* V, int32 N) { FMemory::Memcpy(B.GetData() + O, V, N); }
	};
	struct FR
	{
		const TArray<uint8>& B;
		uint8 U8(int32 O) const { return B[O]; }
		int32 I32(int32 O) const { int32 V; FMemory::Memcpy(&V, B.GetData() + O, 4); return V; }
		uint32 U32(int32 O) const { uint32 V; FMemory::Memcpy(&V, B.GetData() + O, 4); return V; }
		float F32(int32 O) const { float V; FMemory::Memcpy(&V, B.GetData() + O, 4); return V; }
		void Bytes(int32 O, uint8* V, int32 N) const { FMemory::Memcpy(V, B.GetData() + O, N); }
	};
	constexpr int32 OffRiderConfig = 0x10 - 8;
	constexpr int32 OffCareer = 0x100 - 8;
	constexpr int32 CareerSize = 0x1C4;
	constexpr int32 OffNames = 0x1630 - 8;
	constexpr int32 OffSettings = 0x1658 - 8;
	constexpr int32 OffRecords = 0x16C4 - 8;
	constexpr int32 TrackRecordSize = 0x150;
	constexpr int32 TableSize = 0x70;

	FString CString(const uint8* P, int32 N)
	{
		FString S;
		for (int32 I = 0; I < N && P[I]; ++I) { S.AppendChar((TCHAR)P[I]); }
		return S;
	}
	void SetCString(uint8* P, int32 N, const FString& S)
	{
		const int32 L = FMath::Min(S.Len(), N - 1);
		for (int32 I = 0; I < L; ++I) { P[I] = (uint8)FMath::Clamp<int32>(S[I], 0, 255); }
		P[L] = 0;
	}
}

int32 SSXProg::CircuitVenue(int32 I) { return I >= 0 && I < NumCircuit ? Circuit[I] : INDEX_NONE; }
int32 SSXProg::CircuitIndex(int32 V)
{
	for (int32 I = 0; I < NumCircuit; ++I) { if (Circuit[I] == V) { return I; } }
	return INDEX_NONE;
}
bool SSXProg::ShowoffThresholds(int32 Venue, int32 Out[3])
{
	for (const FThresh& T : Showoff)
	{
		if (T.Venue == Venue) { Out[0] = T.T[0]; Out[1] = T.T[1]; Out[2] = T.T[2]; return true; }
	}
	return false;
}
int32 SSXProg::MedalRankPoints(int32 M) { return M >= 0 && M < 4 ? MedalPts[M] : 0; }
int32 SSXProg::RankThreshold(int32 R) { return R >= 0 && R < 10 ? RankPts[R] : 0; }
int32 SSXProg::RankFromPoints(int32 P)
{
	for (int32 R = 0; R < 9; ++R) { if (P >= RankPts[R]) { return R; } }
	return 9;
}
bool SSXProg::HelpRaceNeeded(int32 P) { return RankFromPoints(P) > 7; }
int32 SSXProg::RiderUnlockOrder(int32 K) { return K >= 0 && K < NumRiders ? UnlockOrder[K] : INDEX_NONE; }
int32 SSXProg::BoardCost(int32 B) { return B >= 0 && B < 12 ? Costs[B] : 999; }
int32 SSXProg::Scaled(int32 V) { return (int32)((float)V * 0.4f + 0.5f); }

int32 SSXProg::ShowoffMedal(int32 Score, int32 Venue)
{
	int32 T[3];
	if (!ShowoffThresholds(Venue, T)) { return NoMedal; }
	for (int32 M = 0; M < 3; ++M) { if (Score >= T[M]) { return M; } }
	return NoMedal;
}

uint16 SSXProg::Crc16(const uint8* Data, int32 Num, uint16 Init)
{
	static const bool bBuilt = BuildCrc();
	(void)bBuilt;
	uint16 Crc = Init;
	for (int32 I = 0; I < Num; ++I) { Crc = (uint16)((Crc >> 8) ^ CrcTable[(Crc ^ Data[I]) & 0xFF]); }
	return Crc;
}

FString FSSXRecordRow::GetName() const { return CString(Name, 12); }
void FSSXRecordRow::SetName(const FString& S) { SetCString(Name, 12, S); }

int32 FSSXRecordTable::Insert(int32 Value, const FString& InName, int32 InChar)
{
	const int32 N = FMath::Clamp(Count, 0, 5);
	for (int32 I = 0; I < N; ++I)
	{
		const bool bBetter = HigherBetter ? Value > Rows[I].Value : Value < Rows[I].Value;
		if (!bBetter) { continue; }
		for (int32 K = N - 1; K > I; --K) { Rows[K] = Rows[K - 1]; }
		Rows[I].SetName(InName);
		Rows[I].Char = InChar;
		Rows[I].Value = Value;
		return I;
	}
	return INDEX_NONE;
}

FString SSXProg::RecordDefaultsPath() { return FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("progression_tables.json")); }

bool SSXProg::LoadRecordDefaults(const FString& Path)
{
	FDefaults& D = Defaults();
	D.bTried = true;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogSSX, Warning, TEXT("[progression] no record defaults at %s (run tools/export_progression_data.py)"), *Path);
		D.bLoaded = false;
		return false;
	}
	const TSharedPtr<FJsonObject>* Rec = nullptr;
	if (!Root->TryGetObjectField(TEXT("records"), Rec))
	{
		return D.bLoaded = false;
	}
	static const TCHAR* Kinds[3] = { TEXT("race"), TEXT("showoff"), TEXT("tc") };
	for (int32 T = 0; T < NumTracks; ++T)
	{
		const TSharedPtr<FJsonObject>* Tab = nullptr;
		if (!(*Rec)->TryGetObjectField(FString::FromInt(T), Tab)) { continue; }
		for (int32 K = 0; K < 3; ++K)
		{
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (!(*Tab)->TryGetArrayField(Kinds[K], Rows)) { continue; }
			for (int32 E = 0; E < Rows->Num() && E < 5; ++E)
			{
				const TArray<TSharedPtr<FJsonValue>>& Row = (*Rows)[E]->AsArray();
				D.Names[T][K][E] = Row[0]->AsString();
				D.Values[T][K][E] = (int32)Row[1]->AsNumber();
			}
		}
	}
	D.bLoaded = true;
	return true;
}

bool SSXProg::RecordDefaultsLoaded()
{
	if (!Defaults().bTried) { LoadRecordDefaults(RecordDefaultsPath()); }
	return Defaults().bLoaded;
}

void SSXProg::DefaultRecords(FSSXTrackRecords& Out, int32 Track)
{
	// 0x1668F0, 0x161B40
	static const int32 Hb[3] = { 0, 1, 0 }, Tm[3] = { 1, 0, 1 }, Cnt[3] = { 5, 5, 1 };
	const bool bHave = RecordDefaultsLoaded();
	const FDefaults& D = Defaults();
	for (int32 K = 0; K < 3; ++K)
	{
		FSSXRecordTable& T = Out.Tables[K];
		T = FSSXRecordTable();
		T.Count = Cnt[K];
		T.HigherBetter = Hb[K];
		T.IsTime = Tm[K];
		for (int32 E = 0; E < Cnt[K]; ++E)
		{
			// 0x182858
			T.Rows[E].Char = (int32)FGameRng::StreamA().RandInt(12);
			if (bHave && Track >= 0 && Track < NumTracks)
			{
				T.Rows[E].SetName(D.Names[Track][K][E]);
				T.Rows[E].Value = D.Values[Track][K][E];
			}
		}
	}
}

FSSXCareer::FSSXCareer()
{
	for (int32 T = 0; T < SSXProg::NumTracks; ++T) { BestTime[T] = -1; BestScore[T] = -1; }
}

bool FSSXCareer::operator==(const FSSXCareer& O) const
{
	auto Eq = [](const void* A, const void* B, SIZE_T N) { return FMemory::Memcmp(A, B, N) == 0; };
	if (RawPoints != O.RawPoints || Points[0] != O.Points[0] || Points[1] != O.Points[1] || !Eq(Attr, O.Attr, 4) || !Eq(StatPoints, O.StatPoints, 4)
		|| BookChar != O.BookChar || BookDone[0] != O.BookDone[0] || BookDone[1] != O.BookDone[1] || BookComplete != O.BookComplete
		|| Boards != O.Boards || Outfits != O.Outfits)
	{
		return false;
	}
	for (int32 T = 0; T < SSXProg::NumTracks; ++T)
	{
		if (!(Tracks[T] == O.Tracks[T]) || EventCount[T] != O.EventCount[T] || BestTime[T] != O.BestTime[T] || BestScore[T] != O.BestScore[T]) return false;
	}
	for (int32 I = 0; I < 7; ++I) { if (!(Bests[I] == O.Bests[I])) return false; }
	for (int32 I = 0; I < 6; ++I) { if (!(TrickColour[I] == O.TrickColour[I])) return false; }
	return Eq(Opponent, O.Opponent, sizeof(Opponent));
}

int32 SSXProg::AttributeCap(int32 Char, int32 A)
{
	const SSXRider::FRiderTable& T = SSXRider::FRiderTable::Shared();
	return T.IsLoaded() && A >= 0 && A < NumAttributes ? T.Rider(Char).Alt[A] : 0;
}

int32 SSXProg::AttributeBase(int32 Char, int32 A)
{
	const SSXRider::FRiderTable& T = SSXRider::FRiderTable::Shared();
	return T.IsLoaded() && A >= 0 && A < NumAttributes ? T.AttributeBase(Char, A) : 0;
}

int32 SSXProg::StatBarMax(int32 Char, int32 A) { return Scaled(AttributeCap(Char, A)); }

void SSXProg::NewCareer(FSSXCareer& C, int32 Char)
{
	// 0x166600
	C = FSSXCareer();
	for (int32 A = 0; A < NumAttributes; ++A)
	{
		const int32 Base = AttributeBase(Char, A);
		C.Attr[A] = (uint8)Base;
		C.StatPoints[A] = (uint8)Scaled(Base);
	}
	C.BookChar = Char;
	C.Tracks[0].RaceOpen = C.Tracks[0].ShowoffOpen = 1;
	C.Boards = 0;
	for (int32 B = 0; B < 12; ++B) { if (Costs[B] == 0) { C.Boards |= 1u << B; } }
	C.Outfits = 1;
	for (FSSXCareerBest& B : C.TrickColour) { B.Track = 0; }
	// 0x167DA0
	const SSXHeats::FData& Agg = SSXHeats::FData::Get();
	if (Agg.bLoaded && Char >= 0 && Char < NumRiders)
	{
		for (int32 K = 0; K < NumRiders; ++K) { C.Opponent[K][1] = Agg.Agg[K][Char][1]; }
	}
}

FSSXSaveData FSSXSaveData::NewSave()
{
	FSSXSaveData S;
	S.VenueUnlocks = SSXProg::NewSaveVenueMask;
	S.RiderUnlocks = 0;
	for (int32 K = 0; K < SSXProg::NewSaveRiders; ++K) { S.RiderUnlocks |= 1u << UnlockOrder[K]; }
	for (int32 C = 0; C < SSXProg::NumRiders; ++C)
	{
		// 0x166600, 0x165660, 0x331EA8, 0x1637F8
		static const int32 Stance[SSXProg::NumRiders] = { 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0 };
		static const int32 Word30[SSXProg::NumRiders] = { 0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7 };
		const int32 Cfg[3] = { C, Stance[C], Word30[C] };
		FMemory::Memcpy(S.RiderConfig[C], Cfg, sizeof(Cfg));
		SSXProg::NewCareer(S.Career[C], C);
	}
	for (int32 N = 0; N < SSXProg::NumNames; ++N)
	{
		// 0x379F80
		S.SetUserName(N, FString::Printf(TEXT("PLAYER%d"), N + 1));
	}
	S.Settings = FSSXSettings();
	for (int32 T = 0; T < SSXProg::NumTracks; ++T) { SSXProg::DefaultRecords(S.Records[T], T); }
	return S;
}

FString FSSXSaveData::GetUserName(int32 S) const { return S >= 0 && S < SSXProg::NumNames ? CString(Names[S], 10) : FString(); }
void FSSXSaveData::SetUserName(int32 S, const FString& Name) { if (S >= 0 && S < SSXProg::NumNames) { SetCString(Names[S], 10, Name); } }

TArray<uint8> FSSXSaveData::ToBlob() const
{
	TArray<uint8> B;
	B.SetNumZeroed(SSXProg::BlobSize);
	FW W{ B };
	W.U32(0, VenueUnlocks);
	W.U32(4, RiderUnlocks);
	W.Bytes(OffRiderConfig, &RiderConfig[0][0], sizeof(RiderConfig));
	for (int32 C = 0; C < SSXProg::NumRiders; ++C)
	{
		const FSSXCareer& R = Career[C];
		const int32 O = OffCareer + C * CareerSize;
		W.I32(O + 0x00, R.RawPoints);
		W.I32(O + 0x04, R.Points[0]);
		W.I32(O + 0x08, R.Points[1]);
		W.Bytes(O + 0x0C, R.Attr, 4);
		W.Bytes(O + 0x10, R.StatPoints, 4);
		W.I32(O + 0x14, R.BookChar);
		W.U32(O + 0x18, R.BookDone[0]);
		W.U32(O + 0x1C, R.BookDone[1]);
		W.I32(O + 0x20, R.BookComplete);
		for (int32 T = 0; T < SSXProg::NumTracks; ++T)
		{
			const FSSXTrackState& S = R.Tracks[T];
			const uint8 Bytes[4] = { S.ShowoffMedal, S.RaceMedal, S.RaceOpen, S.ShowoffOpen };
			W.Bytes(O + 0x24 + 4 * T, Bytes, 4);
			W.I32(O + 0x98 + 4 * T, R.EventCount[T]);
			W.I32(O + 0xFC + 4 * T, R.BestTime[T]);
			W.I32(O + 0x130 + 4 * T, R.BestScore[T]);
		}
		W.U32(O + 0x58, R.Boards);
		W.U32(O + 0x5C, R.Outfits);
		for (int32 I = 0; I < 7; ++I) { W.U32(O + 0x60 + 8 * I, R.Bests[I].Value); W.I32(O + 0x64 + 8 * I, R.Bests[I].Track); }
		for (int32 I = 0; I < 6; ++I) { W.U32(O + 0xCC + 8 * I, R.TrickColour[I].Value); W.I32(O + 0xD0 + 8 * I, R.TrickColour[I].Track); }
		for (int32 K = 0; K < SSXProg::NumRiders; ++K) { W.I32(O + 0x164 + 8 * K, R.Opponent[K][0]); W.I32(O + 0x168 + 8 * K, R.Opponent[K][1]); }
	}
	W.Bytes(OffNames, &Names[0][0], sizeof(Names));
	const FSSXSettings& S = Settings;
	const int32 O = OffSettings;
	W.F32(O + 0x00, S.Master); W.F32(O + 0x04, S.Music); W.F32(O + 0x08, S.McSpeech);
	W.F32(O + 0x0C, S.Volume3); W.F32(O + 0x10, S.CharSpeech); W.F32(O + 0x14, S.Sfx);
	W.I32(O + 0x18, S.Speaker); W.I32(O + 0x1C, S.ArcadeSfx); W.I32(O + 0x20, S.Vibration[0]); W.I32(O + 0x24, S.Vibration[1]);
	W.I32(O + 0x28, S.Units); W.I32(O + 0x2C, S.Widescreen); W.I32(O + 0x30, S.SplitScreen);
	for (int32 P = 0; P < 4; ++P) { W.I32(O + 0x34 + 4 * P, S.Controller[P]); }
	W.I32(O + 0x44, S.Word169C); W.I32(O + 0x48, S.Word16A0); W.I32(O + 0x4C, S.Autoload); W.I32(O + 0x50, S.Camera1P);
	W.I32(O + 0x54, S.Camera2P); W.I32(O + 0x58, S.HudDetail); W.F32(O + 0x5C, S.Brightness); W.I32(O + 0x60, S.MaxBoarders);
	W.I32(O + 0x64, S.Replay); W.I32(O + 0x68, S.Language);
	for (int32 T = 0; T < SSXProg::NumTracks; ++T)
	{
		for (int32 K = 0; K < 3; ++K)
		{
			const FSSXRecordTable& Tab = Records[T].Tables[K];
			const int32 TB = OffRecords + T * TrackRecordSize + K * TableSize;
			const int32 Rows = 5;
			for (int32 E = 0; E < Rows; ++E)
			{
				W.Bytes(TB + E * 0x14, Tab.Rows[E].Name, 12);
				W.I32(TB + E * 0x14 + 0xC, Tab.Rows[E].Char);
				W.I32(TB + E * 0x14 + 0x10, Tab.Rows[E].Value);
			}
			W.I32(TB + 0x64, Tab.Count);
			W.I32(TB + 0x68, Tab.HigherBetter);
			W.I32(TB + 0x6C, Tab.IsTime);
		}
	}
	W.U32(SSXProg::BlobMagicOffset, SSXProg::BlobMagic);
	W.U32(SSXProg::BlobChecksum, 0);
	W.U32(SSXProg::BlobChecksum, SSXProg::Crc16(B.GetData(), B.Num()));
	return B;
}

uint16 FSSXSaveData::BlobCrc(const TArray<uint8>& Blob)
{
	TArray<uint8> Copy = Blob;
	if (Copy.Num() >= SSXProg::BlobChecksum + 4) { FMemory::Memzero(Copy.GetData() + SSXProg::BlobChecksum, 4); }
	return SSXProg::Crc16(Copy.GetData(), Copy.Num());
}

bool FSSXSaveData::FromBlob(const TArray<uint8>& B, bool bVerify)
{
	if (B.Num() != SSXProg::BlobSize)
	{
		return false;
	}
	FR R{ B };
	if (bVerify && R.U32(SSXProg::BlobChecksum) != (uint32)BlobCrc(B))
	{
		return false;
	}
	FSSXSaveData D;
	D.VenueUnlocks = R.U32(0);
	D.RiderUnlocks = R.U32(4);
	R.Bytes(OffRiderConfig, &D.RiderConfig[0][0], sizeof(D.RiderConfig));
	for (int32 C = 0; C < SSXProg::NumRiders; ++C)
	{
		FSSXCareer& Rc = D.Career[C];
		const int32 O = OffCareer + C * CareerSize;
		Rc.RawPoints = R.I32(O);
		Rc.Points[0] = R.I32(O + 4);
		Rc.Points[1] = R.I32(O + 8);
		R.Bytes(O + 0x0C, Rc.Attr, 4);
		R.Bytes(O + 0x10, Rc.StatPoints, 4);
		Rc.BookChar = R.I32(O + 0x14);
		Rc.BookDone[0] = R.U32(O + 0x18);
		Rc.BookDone[1] = R.U32(O + 0x1C);
		Rc.BookComplete = R.I32(O + 0x20);
		for (int32 T = 0; T < SSXProg::NumTracks; ++T)
		{
			FSSXTrackState& S = Rc.Tracks[T];
			S.ShowoffMedal = R.U8(O + 0x24 + 4 * T);
			S.RaceMedal = R.U8(O + 0x25 + 4 * T);
			S.RaceOpen = R.U8(O + 0x26 + 4 * T);
			S.ShowoffOpen = R.U8(O + 0x27 + 4 * T);
			Rc.EventCount[T] = R.I32(O + 0x98 + 4 * T);
			Rc.BestTime[T] = R.I32(O + 0xFC + 4 * T);
			Rc.BestScore[T] = R.I32(O + 0x130 + 4 * T);
		}
		Rc.Boards = R.U32(O + 0x58);
		Rc.Outfits = R.U32(O + 0x5C);
		for (int32 I = 0; I < 7; ++I) { Rc.Bests[I].Value = R.U32(O + 0x60 + 8 * I); Rc.Bests[I].Track = R.I32(O + 0x64 + 8 * I); }
		for (int32 I = 0; I < 6; ++I) { Rc.TrickColour[I].Value = R.U32(O + 0xCC + 8 * I); Rc.TrickColour[I].Track = R.I32(O + 0xD0 + 8 * I); }
		for (int32 K = 0; K < SSXProg::NumRiders; ++K) { Rc.Opponent[K][0] = R.I32(O + 0x164 + 8 * K); Rc.Opponent[K][1] = R.I32(O + 0x168 + 8 * K); }
	}
	R.Bytes(OffNames, &D.Names[0][0], sizeof(D.Names));
	FSSXSettings& S = D.Settings;
	const int32 O = OffSettings;
	S.Master = R.F32(O); S.Music = R.F32(O + 4); S.McSpeech = R.F32(O + 8); S.Volume3 = R.F32(O + 0xC); S.CharSpeech = R.F32(O + 0x10); S.Sfx = R.F32(O + 0x14);
	S.Speaker = R.I32(O + 0x18); S.ArcadeSfx = R.I32(O + 0x1C); S.Vibration[0] = R.I32(O + 0x20); S.Vibration[1] = R.I32(O + 0x24);
	S.Units = R.I32(O + 0x28); S.Widescreen = R.I32(O + 0x2C); S.SplitScreen = R.I32(O + 0x30);
	for (int32 P = 0; P < 4; ++P) { S.Controller[P] = R.I32(O + 0x34 + 4 * P); }
	S.Word169C = R.I32(O + 0x44); S.Word16A0 = R.I32(O + 0x48); S.Autoload = R.I32(O + 0x4C); S.Camera1P = R.I32(O + 0x50);
	S.Camera2P = R.I32(O + 0x54); S.HudDetail = R.I32(O + 0x58); S.Brightness = R.F32(O + 0x5C); S.MaxBoarders = R.I32(O + 0x60);
	S.Replay = R.I32(O + 0x64); S.Language = R.I32(O + 0x68);
	S.Preset = Settings.Preset;
	for (int32 T = 0; T < SSXProg::NumTracks; ++T)
	{
		for (int32 K = 0; K < 3; ++K)
		{
			FSSXRecordTable& Tab = D.Records[T].Tables[K];
			const int32 TB = OffRecords + T * TrackRecordSize + K * TableSize;
			const int32 Rows = 5;
			for (int32 E = 0; E < Rows; ++E)
			{
				R.Bytes(TB + E * 0x14, Tab.Rows[E].Name, 12);
				Tab.Rows[E].Char = R.I32(TB + E * 0x14 + 0xC);
				Tab.Rows[E].Value = R.I32(TB + E * 0x14 + 0x10);
			}
			Tab.Count = R.I32(TB + 0x64);
			Tab.HigherBetter = R.I32(TB + 0x68);
			Tab.IsTime = R.I32(TB + 0x6C);
		}
	}
	*this = D;
	return true;
}

bool SSXProg::VenueUnlocked(const FSSXSaveData& S, int32 Mode, int32 Venue, int32 Slot0Char, uint32 ModeMask, bool bUnlockAll)
{
	// 0x163BB8
	if (Venue < 0 || Venue >= 32) { return false; }
	if (bUnlockAll) { return ((ModeMask >> Venue) & 1) != 0; }
	if (Mode == WCRace || Mode == WCShowoff || Mode == Tutorial)
	{
		if (Venue == TrackTutorial) { return true; }
		if (Slot0Char < 0 || Slot0Char >= NumRiders || Venue >= NumTracks) { return false; }
		const FSSXTrackState& T = S.Career[Slot0Char].Tracks[Venue];
		return (Mode == WCRace ? T.RaceOpen : T.ShowoffOpen) != 0;
	}
	return (S.VenueUnlocks & (1u << Venue) & ModeMask) != 0;
}

namespace
{
	int32 AttributeRoom(const FSSXCareer& C, int32 Char)
	{
		int32 Cap = 0;
		for (int32 A = 0; A < SSXProg::NumAttributes; ++A) { Cap += SSXProg::AttributeCap(Char, A) - C.Attr[A]; }
		return Cap;
	}

	void Unlocks(FSSXSaveData& S, FSSXProgressSession& Sess, FSSXCareer& C, int32 Char, int32 Mode, int32 Venue,
		int32 OldRP, int32 NewRP, int32 Medal, int32 Player, uint32 EventTricks, TArray<FSSXUnlock>& Out)
	{
		using namespace SSXProg;
		// 0x16C420
		if (Medal != NoMedal) { Out.Add({ Player, UMedal, Medal }); }
		// 0x164020, 0x164258, 0x164110
		int32 Done = 0;
		for (int32 Ch = 0; Ch < 5; ++Ch)
		{
			if (!C.ChapterDone(Ch)) continue;
			++Done;
			if (!((C.Outfits >> (Ch + 1)) & 1)) { Out.Add({ Player, UOutfit, Ch + 1 }); }
		}
		const bool bBook = C.ChapterDone(5) && Done == 5;
		if (bBook && !C.BookComplete) { Out.Add({ Player, UBookDone, Char }); }
		// 0x163F88
		if (const int32 New = FMath::CountBits(EventTricks)) { Out.Add({ Player, UBookEntries, New }); }
		for (int32 Ch = 0; Ch < 5; ++Ch) { if (C.ChapterDone(Ch)) { C.Outfits |= 1u << (Ch + 1); } }
		if (bBook)
		{
			C.BookComplete = 1;
			C.Boards |= 1u << UberBoard;
		}
		if (OldRP == NewRP) { return; }
		const int32 Idx = CircuitIndex(Venue);
		if (Idx != INDEX_NONE && Idx < NumCircuit - 1)
		{
			const int32 V = CircuitVenue(Idx + 1);
			if (!((S.VenueUnlocks >> V) & 1)) { S.VenueUnlocks |= 1u << V; Out.Add({ Player, UCourse, V }); }
			FSSXTrackState& T = C.Tracks[V];
			if (Mode == WCRace) { if (!T.RaceOpen) { T.RaceOpen = 1; Out.Add({ Player, URaceVenue, V }); } }
			else if (!T.ShowoffOpen) { T.ShowoffOpen = 1; Out.Add({ Player, UShowoffVenue, V }); }
		}
		const FSSXTrackState& Last = C.Tracks[CircuitVenue(NumCircuit - 1)];
		if (Last.RaceMedal < NoMedal && !((S.VenueUnlocks >> TrackUntracked) & 1))
		{
			S.VenueUnlocks |= 1u << TrackUntracked;
			Out.Add({ Player, UCourse, TrackUntracked });
			Sess.bRaceCircuitBeaten = true;
		}
		if (Last.ShowoffMedal < NoMedal && !((S.VenueUnlocks >> TrackPipedream) & 1))
		{
			S.VenueUnlocks |= 1u << TrackPipedream;
			Out.Add({ Player, UCourse, TrackPipedream });
			Sess.bShowoffCircuitBeaten = true;
		}
		if (Medal == Gold)
		{
			for (int32 K = 0; K < NumRiders; ++K)
			{
				const int32 R = RiderUnlockOrder(K);
				if (!((S.RiderUnlocks >> R) & 1)) { S.RiderUnlocks |= 1u << R; Out.Add({ Player, URider, R }); break; }
			}
		}
		for (int32 B = 0; B < 12; ++B)
		{
			if (BoardCost(B) <= NewRP && !((C.Boards >> B) & 1)) { C.Boards |= 1u << B; Out.Add({ Player, UBoard, B }); }
		}
		int32 Crossed = -1;
		for (int32 R = 0; R < 10; ++R) { if (NewRP >= RankThreshold(R) && OldRP < RankThreshold(R)) { Crossed = R; } }
		if (Crossed >= 0)
		{
			Sess.bRankUp = true;
			Out.Add({ Player, URank, Crossed });
			if (Crossed == 0) { C.Outfits |= 1u << ChromeOutfit; }
		}
	}
}

TArray<FSSXUnlock> SSXProg::AwardEvent(FSSXSaveData& S, FSSXProgressSession& Sess, int32 Char, int32 Mode, int32 Venue,
	int32 Heat, int32 Place, int32 Score, int32 Player, uint32 EventTricks)
{
	TArray<FSSXUnlock> Out;
	Sess.bRaceCircuitBeaten = Sess.bShowoffCircuitBeaten = Sess.bRankUp = false;
	if (Char < 0 || Char >= NumRiders || Venue < 0 || Venue >= NumTracks)
	{
		return Out;
	}
	FSSXCareer& C = S.Career[Char];
	int32 Medal = NoMedal, OldRP = 0, NewRP = 0;
	if (Mode == WCShowoff || (Mode == WCRace && Heat == 2))
	{
		// 0x16BCB0
		FSSXTrackState& T = C.Tracks[Venue];
		const int32 Old = Mode == WCShowoff ? T.ShowoffMedal : T.RaceMedal;
		Medal = Mode == WCShowoff ? ShowoffMedal(Score, Venue) : RaceMedal(Place);
		OldRP = C.RankPoints();
		if (Medal < Old)
		{
			const int32 Delta = MedalRankPoints(Medal) - MedalRankPoints(Old);
			if (Mode == WCShowoff) { C.Points[1] += Delta; T.ShowoffMedal = (uint8)Medal; }
			else { C.Points[0] += Delta; T.RaceMedal = (uint8)Medal; }
			C.RawPoints += Delta;
			for (int32 K = 0; K < NumRiders; ++K) { C.Opponent[K][0] += Delta; }   // 0x16D1F8
			const int32 Cap = AttributeRoom(C, Char);
			if (Cap < C.RawPoints)
			{
				C.RawPoints = Cap;
			}
			else
			{
				bool bAllGold = true;
				for (int32 I = 0; I < NumCircuit; ++I)
				{
					const FSSXTrackState& V = C.Tracks[CircuitVenue(I)];
					bAllGold &= V.RaceMedal == Gold && V.ShowoffMedal == Gold;
				}
				if (bAllGold) { C.RawPoints = Cap; }
			}
		}
		NewRP = C.RankPoints();
	}
	Sess.LastMedal = Medal;
	Sess.RankBefore = RankFromPoints(OldRP);
	Sess.RankAfter = RankFromPoints(NewRP);
	Unlocks(S, Sess, C, Char, Mode, Venue, OldRP, NewRP, Medal, Player, EventTricks, Out);
	return Out;
}

int32 SSXProg::AdvanceHeat(FSSXProgressSession& Sess, int32 PlayerPlace)
{
	// 0x16D0B0, 0x16C3B0
	if (PlayerPlace >= 0 && PlayerPlace < 3) { Sess.HeatFactor = SSXHeats::HeatFactor(PlayerPlace); }
	if (PlayerPlace >= 0 && PlayerPlace < 3 && Sess.Heat < 2)
	{
		++Sess.Heat;
		return SSXHeats::ExitNextHeat;
	}
	Sess.Heat = 0;
	return SSXHeats::ExitDone;
}

int32 SSXProg::CircuitHeatEnd(FSSXProgressSession& Sess, const TArray<int32>& FinishRiders, int32 PlayerRider, int32 Mode,
	FSSXCareer* Career)
{
	const bool bAdvance = SSXHeats::HeatResult(Sess.Elim, FinishRiders, PlayerRider, Sess.Heat, Sess.HeatFactor);
	// 0x16D0B0, 0x16C328
	if (Career && Sess.Heat == 2 && PlayerRider >= 0 && PlayerRider < NumRiders && Sess.Elim.Flag[PlayerRider] == 0)
	{
		for (int32 K = 0; K < NumRiders; ++K)
		{
			if (Sess.Carried[K] >= 0) { Career->Opponent[K][1] = Sess.Carried[K]; }
		}
	}
	const int32 Code = SSXHeats::CircuitExitCode(Mode, bAdvance);
	if (Code == SSXHeats::ExitNextHeat)
	{
		++Sess.Heat;
		Sess.bNextHeatEntry = true;
	}
	else
	{
		Sess.Heat = 0;
		Sess.bNextHeatEntry = false;
	}
	return Code;
}

void SSXProg::BuildPointSession(const FSSXSaveData& S, int32 Char, bool bMaxAttributes, FSSXPointSession& P)
{
	// 0x166A58
	P = FSSXPointSession();
	if (Char < 0 || Char >= NumRiders) { return; }
	const FSSXCareer& C = S.Career[Char];
	P.Char = Char;
	P.RawAtOpen = C.RawPoints;
	P.Left = P.Initial = bMaxAttributes ? 0 : Scaled(C.RawPoints);
	int32 Room = 0;
	for (int32 A = 0; A < NumAttributes; ++A)
	{
		P.Max[A] = StatBarMax(Char, A);
		P.Cur[A] = P.Floor[A] = bMaxAttributes ? P.Max[A] : C.StatPoints[A];
		Room += P.Max[A] - P.Cur[A];
	}
	if (Room < P.Initial) { P.Initial = P.Left = Room; }
}

namespace
{
	void SyncPoint(FSSXSaveData& S, FSSXPointSession& P, int32 A)
	{
		FSSXCareer& C = S.Career[P.Char];
		C.RawPoints = P.Initial ? (int32)((float)P.Left / (float)P.Initial * (float)P.RawAtOpen) : 0;
		const float Alt = (float)SSXProg::AttributeCap(P.Char, A);
		C.Attr[A] = P.Max[A] ? (uint8)(int32)((float)P.Cur[A] / (float)P.Max[A] * Alt) : C.Attr[A];
	}
}

bool SSXProg::PointAdd(FSSXSaveData& S, FSSXPointSession& P, int32 A)
{
	if (P.Char < 0 || A < 0 || A >= NumAttributes || P.Left <= 0 || P.Cur[A] >= P.Max[A]) { return false; }
	--P.Left;
	++P.Cur[A];
	++S.Career[P.Char].StatPoints[A];
	SyncPoint(S, P, A);
	return true;
}

bool SSXProg::PointRemove(FSSXSaveData& S, FSSXPointSession& P, int32 A)
{
	if (P.Char < 0 || A < 0 || A >= NumAttributes || P.Cur[A] <= P.Floor[A]) { return false; }
	++P.Left;
	--P.Cur[A];
	--S.Career[P.Char].StatPoints[A];
	SyncPoint(S, P, A);
	return true;
}

void SSXProg::CareerBests(FSSXCareer& C, const FSSXRaceStatBlock& B, int32 Track)
{
	// 0x166CB0
	auto Int = [Track](FSSXCareerBest& Dst, int32 V)
	{
		if (int32(Dst.Value) < V) { Dst.Value = uint32(V); Dst.Track = Track; }
	};
	auto Flt = [Track](FSSXCareerBest& Dst, float V)
	{
		float Cur = 0.f;
		FMemory::Memcpy(&Cur, &Dst.Value, sizeof(Cur));
		if (Cur < V) { FMemory::Memcpy(&Dst.Value, &V, sizeof(V)); Dst.Track = Track; }
	};
	Int(C.Bests[0], B.Knockdowns);
	Int(C.Bests[1], B.Wipeouts);
	Int(C.Bests[2], B.Tricks);
	Int(C.Bests[3], B.Ubers);
	Flt(C.Bests[4], B.AirBest);
	Flt(C.Bests[5], B.GrabBest);
	Flt(C.Bests[6], B.RailBest);
	for (int32 K = 0; K < 6; ++K) { Int(C.TrickColour[K], B.Colour[K]); }
}

bool SSXProg::CareerBestsMode(int32 Mode)
{
	return Mode == SERace || Mode == SEShowoff || Mode == WCRace || Mode == WCShowoff || Mode == TimeChallenge;
}

void SSXProg::CountEvent(FSSXCareer& C, int32 Track)
{
	// 0x167010
	if (Track >= 0 && Track < NumTracks) { ++C.EventCount[Track]; }
}

void SSXProg::EndEvent(FSSXSaveData& S, FSSXProgressSession& Sess, const FSSXEventResult& R, bool bMaxAttributes)
{
	// 0x164FC0
	Sess.Unlocks.Reset();
	Sess.PendingRecords.Reset();
	Sess.bUnlocksAdded = false;
	Sess.LastMedal = NoMedal;
	if (R.Venue < 0 || R.Venue >= NumTracks)
	{
		return;
	}
	const int32 Kind = (R.Mode == SERace || R.Mode == WCRace) ? 0 : (R.Mode == SEShowoff || R.Mode == WCShowoff) ? 1 : R.Mode == TimeChallenge ? 2 : -1;
	if (Kind >= 0)
	{
		for (const FSSXEventHuman& H : R.Humans)
		{
			const int32 Value = Kind == 1 ? H.Score : H.TimeCs;
			const int32 Row = S.Records[R.Venue].Tables[Kind].Insert(Value, H.Name, H.Char);
			if (Row != INDEX_NONE && Sess.PendingRecords.Num() < MaxPendingRecords)
			{
				Sess.PendingRecords.Add({ H.Player, Kind, R.Venue, Row });
			}
		}
	}
	// 0x166FA0, 0x166FE0
	const FSSXEventHuman* First = R.Humans.Num() > 0 ? &R.Humans[0] : nullptr;
	for (const FSSXEventHuman& H : R.Humans) { if (H.Player == 0) { First = &H; break; } }
	if (First && First->Char >= 0 && First->Char < NumRiders)
	{
		FSSXCareer& C = S.Career[First->Char];
		if (R.Mode == WCRace && First->TimeCs >= 0 && (C.BestTime[R.Venue] == -1 || First->TimeCs < C.BestTime[R.Venue]))
		{
			C.BestTime[R.Venue] = First->TimeCs;
		}
		if (R.Mode == WCShowoff && C.BestScore[R.Venue] < First->Score)
		{
			C.BestScore[R.Venue] = First->Score;
		}
	}
	if (R.Humans.Num() > 0)
	{
		const FSSXEventHuman& H = R.Humans[0];
		Sess.Unlocks = AwardEvent(S, Sess, H.Char, R.Mode, R.Venue, Sess.Heat, H.Place, H.Score, H.Player, H.EventTricks);
		for (int32 I = 1; I < R.Humans.Num(); ++I)
		{
			FSSXProgressSession Other;
			FSSXSaveData& Save = S;
			const FSSXEventHuman& O = R.Humans[I];
			if (O.Char < 0 || O.Char >= NumRiders) continue;
			TArray<FSSXUnlock> More;
			FSSXCareer& C = Save.Career[O.Char];
			Unlocks(Save, Other, C, O.Char, 0, R.Venue, 0, 0, NoMedal, O.Player, O.EventTricks, More);
			Sess.Unlocks.Append(More);
		}
	}
	Sess.bUnlocksAdded = Sess.Unlocks.Num() > 0;
	// 0x166A58
	for (const FSSXEventHuman& H : R.Humans)
	{
		if (H.Player >= 0 && H.Player < 2) { BuildPointSession(S, H.Char, bMaxAttributes, Sess.Points[H.Player]); }
	}
}
