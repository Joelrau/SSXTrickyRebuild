#include "Sim/Rider/RiderProfiles.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sim/RiderState.h"

namespace SSXRider
{
namespace
{
	int32 PercentToByte(int32 V) { return (V * 255) / 100; }

	FCriticalSection& TableLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}
}

FString FRiderTable::DefaultPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("riders.bin")));
}

const FRiderTable& FRiderTable::Shared()
{
	static FRiderTable* Table = nullptr;
	FScopeLock Lock(&TableLock());
	if (!Table)
	{
		Table = new FRiderTable();
		const FString Path = DefaultPath();
		FString Err;
		if (!FPaths::FileExists(Path))
		{
			UE_LOG(LogTemp, Display, TEXT("[SSX.Rider] %s not found (run tools/export_rider_profiles.py); rider stats stay at 128"), *Path);
		}
		else if (!Table->Load(Path, &Err))
		{
			UE_LOG(LogTemp, Warning, TEXT("[SSX.Rider] %s; rider stats stay at 128"), *Err);
		}
	}
	return *Table;
}

bool FRiderTable::Load(const FString& Path, FString* OutError)
{
	TArray<uint8> Bytes;
	auto Fail = [OutError, &Path](const TCHAR* Why)
	{
		if (OutError) { *OutError = FString::Printf(TEXT("%s: %s"), *Path, Why); }
		return false;
	};
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return Fail(TEXT("cannot read"));
	}
	int64 Pos = 0;
	auto Read = [&Bytes, &Pos](void* Dst, int64 Size)
	{
		if (Pos + Size > Bytes.Num()) { return false; }
		FMemory::Memcpy(Dst, Bytes.GetData() + Pos, Size);
		Pos += Size;
		return true;
	};
	auto ReadName = [&Read](int32 Len, FString& Out)
	{
		TArray<ANSICHAR> Buf;
		Buf.SetNumZeroed(Len + 1);
		if (!Read(Buf.GetData(), Len)) { return false; }
		Out = FString(ANSI_TO_TCHAR(Buf.GetData()));
		return true;
	};
	auto ReadBones = [&Read](bool& bHas, FVector* Out)
	{
		uint32 Has = 0;
		if (!Read(&Has, 4)) { return false; }
		bHas = Has != 0;
		for (int32 B = 0; B < NumBindBones; ++B)
		{
			float V[3];
			if (!Read(V, sizeof(V))) { return false; }
			Out[B] = FVector(V[0], V[1], V[2]);
		}
		return true;
	};

	char Magic[4];
	uint32 Hdr[4];
	if (!Read(Magic, 4) || !Read(Hdr, sizeof(Hdr)) || FMemory::Memcmp(Magic, "SSXR", 4) != 0 || Hdr[0] != 1)
	{
		return Fail(TEXT("not an SSXR version 1 file"));
	}
	if (Hdr[1] != NumRiders || Hdr[2] != NumBoards || Hdr[3] != NumBindBones)
	{
		return Fail(TEXT("unexpected table dimensions"));
	}
	FRiderTable T;
	for (int32 A = 0; A < NumAttributes; ++A)
	{
		uint32 G[4];
		if (!Read(G, sizeof(G)) || G[0] < 1 || G[0] > 3) { return Fail(TEXT("bad attribute map")); }
		T.GroupCount[A] = int32(G[0]);
		for (int32 K = 0; K < 3; ++K)
		{
			if (G[1 + K] >= NumStats) { return Fail(TEXT("bad attribute map")); }
			T.Groups[A][K] = int32(G[1 + K]);
		}
	}
	for (FStaticRider& R : T.Riders)
	{
		uint8 B4[4];
		int32 I3[3];
		if (!ReadName(16, R.Name) || !Read(R.Stats, NumStats) || !Read(R.Alt, NumAttributes) || !Read(B4, 4)
			|| !Read(&R.Scale, 4) || !Read(I3, sizeof(I3)) || !ReadBones(R.bHasBones, R.Bones))
		{
			return Fail(TEXT("truncated rider records"));
		}
		R.Mass = B4[0];
		R.Rec34 = B4[1];
		R.Stance = I3[0];
		R.TrickSet = I3[1];
		R.Rec30 = I3[2];
	}
	for (FBoardRecord& B : T.Boards)
	{
		int32 I2[2];
		if (!ReadName(24, B.Name) || !Read(I2, sizeof(I2)) || !Read(B.Stats, NumAttributes))
		{
			return Fail(TEXT("truncated board table"));
		}
		B.Class = I2[0];
		B.Cost = I2[1];
	}
	if (!ReadBones(T.bMmmBones, T.MmmBones))
	{
		return Fail(TEXT("truncated Mix Master Mike bones"));
	}
	T.bLoaded = true;
	*this = T;
	return true;
}

int32 FRiderTable::AttributeBase(int32 Character, int32 A) const
{
	const FStaticRider& R = Rider(Character);
	int32 Sum = 0;
	for (int32 I : Group(A))
	{
		Sum += R.Stats[I];
	}
	return Sum / GroupCount[A];
}

const FVector* FRiderTable::BindBones(int32 Character, bool bMixMasterMike) const
{
	if (!bLoaded)
	{
		return nullptr;
	}
	if (bMixMasterMike)
	{
		return bMmmBones ? MmmBones : nullptr;
	}
	const FStaticRider& R = Rider(Character);
	return R.bHasBones ? R.Bones : nullptr;
}

int32 FRiderTable::FirstBoardOfClass(int32 Character, int32 BoardClass) const
{
	for (int32 K = 0; K < NumBoards; ++K)
	{
		if (Board(Character, K).Class == BoardClass)
		{
			return K;
		}
	}
	return INDEX_NONE;
}

FBuiltProfile BuildPlayerProfile(const FRiderTable& Table, int32 Character, const FPlayerOptions& O)
{
	// 0x16A9D8
	FBuiltProfile P;
	const FStaticRider& R = Table.Rider(Character);
	const FBoardRecord& B = Table.Board(Character, O.Board);
	int32 Stat[NumStats];
	for (int32 I = 0; I < NumStats; ++I)
	{
		Stat[I] = R.Stats[I];
	}
	const bool bCheats = !O.bMode6;
	// 0x3352B0, 0x3352AC, 0x16AD54, 0x16AD68
	const bool bMallora = O.bMallora && Character == MalloraCharacter;
	const bool bAltStats = bCheats && (O.bCheatAlt || O.bMixMasterMike || bMallora);
	for (int32 A = 0; A < NumAttributes; ++A)
	{
		if (bAltStats)
		{
			for (int32 I : Table.Group(A)) { Stat[I] = R.Alt[A]; }
			continue;
		}
		const int32 Base = Table.AttributeBase(Character, A);
		const int32 Car = O.bHaveCareer ? O.Career[A] : Base;           // 0x166600
		const float F2 = float(B.Stats[A]) * BoardStatWeight;
		const float F3 = Base == 100 ? 0.0f : float(Car - Base) / float(100 - Base);
		for (int32 I : Table.Group(A))
		{
			const float S = float(R.Stats[I]);
			Stat[I] = int32((S + F3 * (100.0f - S)) * PlayerStatWeight + F2);
		}
	}
	for (int32 I = 0; I < NumStats; ++I)
	{
		P.Stats[I] = uint8(FMath::Clamp(PercentToByte(Stat[I]), 0, 255));
	}
	P.Character = Character;
	P.Board = O.Board;
	P.BoardClass = B.Class;
	P.Scale = R.Scale;
	P.Stance = O.Stance >= 0 ? O.Stance : R.Stance;
	P.TrickSet = R.TrickSet;
	P.MassByte = R.Mass;
	P.bComputer = false;
	P.Char3C = Character;
	P.Byte80 = 2;
	P.Byte64 = R.Rec34;
	P.Rank = O.Rank;   // FUN_00164840, 0x32B390
	if (bCheats && O.bMixMasterMike)
	{
		// 0x3352B8
		P.bMixMasterMike = true;
		P.Byte80 = 0;
		P.Byte64 = MixMasterMikeByte64;
		P.Char3C = 1;
		P.Scale = MixMasterMikeScale;
	}
	else if (O.bMixMasterMike)
	{
		P.bMixMasterMike = true;
		P.Byte80 = 0;
		P.Byte64 = MixMasterMikeByte64;
	}
	// 0x16AC00
	P.bMallora = bMallora;
	return P;
}

FBuiltProfile BuildComputerProfile(const FRiderTable& Table, int32 Character, bool bHaveLevel, int32 Level, int32 Board)
{
	// 0x16AE68
	FBuiltProfile P;
	const FStaticRider& R = Table.Rider(Character);
	int32 Stat[NumStats];
	for (int32 I = 0; I < NumStats; ++I)
	{
		Stat[I] = R.Stats[I];
	}
	if (bHaveLevel)
	{
		for (int32 A = 0; A < NumAttributes; ++A)
		{
			const int32 Base = Table.AttributeBase(Character, A);
			float Gain = Base == 100 ? 0.0f : (float(Level) * AiLevelWeight) / float(100 - Base);
			Gain = FMath::Min(Gain, 1.0f);
			for (int32 I : Table.Group(A))
			{
				const float S = float(R.Stats[I]);
				Stat[I] = int32(0.0f + S + Gain * (100.0f - S));
			}
		}
	}
	for (int32 I = 0; I < NumStats; ++I)
	{
		P.Stats[I] = uint8(FMath::Clamp(PercentToByte(Stat[I]), 0, 255));
	}
	P.Character = Character;
	P.Board = FMath::Clamp(Board, 0, NumBoards - 1);
	P.BoardClass = Table.Board(Character, P.Board).Class;   // board stats never affect computer riders
	P.Scale = R.Scale;
	P.Stance = R.Stance;
	P.TrickSet = R.TrickSet;
	P.MassByte = R.Mass;
	P.bComputer = true;
	P.Rank = 0;
	P.Char3C = Character;
	P.Byte64 = R.Rec34;
	return P;
}

void ApplyProfile(const FBuiltProfile& Built, FRiderProfile& Out)
{
	for (int32 I = 0; I < NumStats; ++I)
	{
		Out.StatBytes[I] = Built.Stats[I];
	}
	Out.Character = Built.Character;
	Out.TrickCharacter = Built.TrickSet;
	Out.Voice = Built.Byte64;
	Out.Stance = Built.Stance;
	Out.BoardClass = Built.BoardClass;
	Out.SizeScale = Built.Scale;
	Out.MassByte = Built.MassByte;
	Out.Board = Built.Board;
	Out.bComputer = Built.bComputer;
	Out.bMixMasterMike = Built.bMixMasterMike;
	Out.Level = Built.Rank;
	Out.bProfileLoaded = true;
}

bool ApplyNewSavePlayerProfile(FRiderProfile& Out, int32 Character, int32 BoardClass)
{
	const FRiderTable& T = FRiderTable::Shared();
	if (!T.IsLoaded() || Character < 0 || Character >= NumRiders)
	{
		return false;
	}
	FPlayerOptions O;
	const int32 K = T.FirstBoardOfClass(Character, BoardClass);
	O.Board = K == INDEX_NONE ? 0 : K;
	ApplyProfile(BuildPlayerProfile(T, Character, O), Out);
	return true;
}

int32 ResolveBoard(const FRiderTable& Table, int32 Character, int32 Board, int32 BoardClass)
{
	if (Board >= 0 && Board < NumBoards)
	{
		return Board;
	}
	const int32 K = Table.IsLoaded() ? Table.FirstBoardOfClass(Character, BoardClass) : INDEX_NONE;
	return K == INDEX_NONE ? 0 : K;
}

bool ApplySelectedPlayerProfile(FRiderProfile& Out, int32 Character, const FPlayerOptions& Options)
{
	const FRiderTable& T = FRiderTable::Shared();
	if (!T.IsLoaded() || Character < 0 || Character >= NumRiders)
	{
		return false;
	}
	ApplyProfile(BuildPlayerProfile(T, Character, Options), Out);
	return true;
}

bool ApplyComputerProfile(FRiderProfile& Out, int32 Character, int32 Level, int32 Board)
{
	const FRiderTable& T = FRiderTable::Shared();
	if (!T.IsLoaded() || Character < 0 || Character >= NumRiders)
	{
		return false;
	}
	ApplyProfile(BuildComputerProfile(T, Character, true, Level, Board), Out);
	return true;
}
}
