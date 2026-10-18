#include "UI/SSXFeTrickBook.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sim/Air/TrickName.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndData.h"

namespace SSXFeTrickBook
{

const FTable& FTable::Get()
{
	static FTable T = []()
	{
		FTable X;
		X.LoadFile(FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("extracted"), TEXT("DATA"), TEXT("TUTORIAL"), TEXT("TRICKDEF.DAT")));
		return X;
	}();
	return T;
}

bool FTable::LoadFile(const FString& Path)
{
	TArray<uint8> Bytes;
	return FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) && LoadBytes(Bytes);
}

bool FTable::LoadBytes(const TArray<uint8>& Bytes)
{
	// FUN_00154580 / 0x31E210
	Entries.Reset();
	if (Bytes.Num() != 12 * TricksPerRider * RecordSize)
	{
		return false;
	}
	auto U32 = [&Bytes](int32 O) { return uint32(Bytes[O]) | (uint32(Bytes[O + 1]) << 8) | (uint32(Bytes[O + 2]) << 16) | (uint32(Bytes[O + 3]) << 24); };
	for (int32 O = 0; O < Bytes.Num(); O += RecordSize)
	{
		auto S16 = [&U32](int32 P) { return int16(U32(P) & 0xFFFF); };
		FEntry E;
		E.W0 = U32(O);
		E.W1 = U32(O + 4);
		E.Flip = S16(O + 8);
		E.Spin = S16(O + 10);
		E.Rail = S16(O + 12);
		E.Tweak1 = S16(O + 0x10);
		E.Tweak2 = S16(O + 0x12);
		E.ActA = int32(U32(O + 0x14));
		E.ActB = int32(U32(O + 0x18));
		Entries.Add(E);
	}
	return true;
}

const FEntry* FTable::Find(int32 Char, int32 K) const
{
	// FUN_00154638 / 0x31E210
	const int32 I = Char * TricksPerRider + K;
	return IsLoaded() && Char >= 0 && Char < 12 && K >= 0 && K < TricksPerRider ? &Entries[I] : nullptr;
}

FString FTable::TrickName(int32 Char, int32 K) const
{
	const FEntry* E = Find(Char, K);
	return E ? SSXTrick::FTrickTables::Get().Compose(E->W0, E->W1) : FString();
}

bool Matches(const FEntry& E, uint32 W0, uint32 W1)
{
	// FUN_00157C18
	return E.W0 == W0 && ((E.W1 ^ W1) & 0x07000F00u) == 0;
}

uint32 LandBookTrick(const FTable& T, FSSXCareer& C, uint32& EventBits, uint32 W0, uint32 W1)
{
	// FUN_001579E0
	uint32 Set = 0;
	for (int32 K = 0; K < TricksPerRider; ++K)
	{
		const FEntry* E = T.Find(C.BookChar, K);
		const uint32 Bit = 1u << K;
		if (!E || !Matches(*E, W0, W1) || (C.BookDone[1] & Bit) || (C.BookDone[0] & Bit))
		{
			continue;
		}
		int32 Current = 0;   // FUN_00164060
		while (C.ChapterDone(Current)) { ++Current; }
		if (K / TricksPerChapter == Current)   // FUN_001640A8
		{
			EventBits |= Bit;
			C.BookDone[1] |= Bit;
			Set |= Bit;
		}
	}
	return Set;
}

FString ExplainNoBook(const FTable& T, const FSSXCareer& C, uint32 W0, uint32 W1)
{
	if (!T.IsLoaded())
	{
		return TEXT("trickdef.dat not loaded");
	}
	int32 Current = 0;
	while (Current < Chapters && C.ChapterDone(Current)) { ++Current; }
	FString Why;
	for (int32 K = 0; K < TricksPerRider; ++K)
	{
		const FEntry* E = T.Find(C.BookChar, K);
		if (!E || !Matches(*E, W0, W1))
		{
			continue;
		}
		Why += Why.IsEmpty() ? TEXT("") : TEXT("; ");
		if (C.TrickDone(K)) { Why += FString::Printf(TEXT("entry %d already learned"), K); }
		else { Why += FString::Printf(TEXT("entry %d is in chapter %d, the current chapter is %d"), K, K / TricksPerChapter, Current); }
	}
	return Why.IsEmpty() ? FString::Printf(TEXT("no entry of book rider %d matches"), C.BookChar) : Why;
}

FString VideoEntry(int32 Char, int32 K)
{
	// 0x27E3C0 / FUN_002FCA28 / 0x3AF808 / 0x331EB0
	static const TCHAR* const Short[12] = { TEXT("Eddi"), TEXT("Kaor"), TEXT("Luth"), TEXT("Mac"), TEXT("Moby"), TEXT("Zoe"),
		TEXT("Jp"), TEXT("Elis"), TEXT("Psym"), TEXT("Seei"), TEXT("Brod"), TEXT("Mari") };
	return Char >= 0 && Char < 12 ? FString::Printf(TEXT("data\\video\\tb\\%s_%03d.sss"), Short[Char], K) : FString();
}

bool FindBigEntry(const TArray<uint8>& A, const FString& Name, TArray<uint8>& Out)
{
	auto Be = [&A](int32 P, int32 N) { int64 V = 0; for (int32 I = 0; I < N; ++I) { V = V << 8 | A[P + I]; } return V; };
	int32 Count = 0, Pos = 0, Field = 0;
	if (A.Num() >= 6 && A[0] == 0xC0 && A[1] == 0xFB)
	{
		Count = int32(Be(4, 2));
		Pos = 6;
		Field = 3;
	}
	else if (A.Num() >= 16 && FMemory::Memcmp(A.GetData(), "BIGF", 4) == 0)
	{
		Count = int32(Be(8, 4));
		Pos = 16;
		Field = 4;
	}
	else
	{
		return false;
	}
	for (int32 I = 0; I < Count && Pos + 2 * Field < A.Num(); ++I)
	{
		const int64 Offset = Be(Pos, Field);
		const int64 Size = Be(Pos + Field, Field);
		Pos += 2 * Field;
		FString Entry;
		while (Pos < A.Num() && A[Pos] != 0) { Entry.AppendChar(TCHAR(A[Pos++])); }
		++Pos;
		if (Entry.Equals(Name, ESearchCase::IgnoreCase) && Offset >= 0 && Size > 0 && Offset + Size <= A.Num())
		{
			Out = TArray<uint8>(A.GetData() + Offset, int32(Size));
			return true;
		}
	}
	return false;
}

bool ReadVideo(int32 Char, int32 K, TArray<uint8>& Out)
{
	const FString Name = VideoEntry(Char, K);
	if (Name.IsEmpty())
	{
		return false;
	}
	const FString Big = FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("extracted"), TEXT("DATA"), TEXT("VIDEO"), TEXT("TB"),
		FString::Printf(TEXT("TB_%s.BIG"), *FString(SSXFeTables::RiderShort(Char)).ToUpper()));
	TArray<uint8> Archive;
	return FFileHelper::LoadFileToArray(Archive, *Big, FILEREAD_Silent) && FindBigEntry(Archive, Name, Out);
}

bool FVideo::Step(int32 Char, int32 K)
{
	if (K != CurK || Char != CurChar)
	{
		// 0x200098 / 0x1FFF30
		if (CurK >= 0) { Stream.Release(); }
		CurChar = Char;
		CurK = K;
		Stream = SSXPropMovie::FStream();
		File = SSXPropMovie::FSssFile();
		if (K < 0)
		{
			return false;
		}
		TArray<uint8> Bytes;
		if (!ReadVideo(Char, K, Bytes) || !File.LoadFromMemory(MoveTemp(Bytes)))
		{
			return false;
		}
		Stream.Acquire();
	}
	if (CurK < 0 || !Stream.Step(File.NumFrames()))
	{
		return false;
	}
	W = File.FrameWidth(Stream.Shown);
	H = File.FrameHeight(Stream.Shown);
	File.Decode(Stream.Shown, Pixels);
	return true;
}

FString CoverFolder(int32 Char)
{
	// FUN_001F9B90 / 0x27E008
	return FString(TEXT("TBC_")) + FString(SSXFeTables::RiderShort(Char)).ToUpper();
}

FString TrickFolder(int32 Char, int32 Chapter)
{
	// FUN_0027A288 / FUN_001F9B08
	return FString::Printf(TEXT("TB_%s%d"), *FString(SSXFeTables::RiderShort(Char)).ToUpper(), FMath::Clamp(Chapter, 0, Chapters - 1));
}

FString TrickImagePattern(int32 Trick)
{
	return FString::Printf(TEXT("%03d_*.png"), FMath::Clamp(Trick, 0, TricksPerChapter - 1));
}

FName CoverPage(int32 Char)
{
	return FName(*(CoverFolder(Char) + TEXT("/tkb1")));
}

FName TrickPage(int32 Char, int32 Chapter, int32 Trick)
{
	return FName(*FString::Printf(TEXT("%s/tb%02d"), *TrickFolder(Char, Chapter), FMath::Clamp(Trick, 0, TricksPerChapter - 1)));
}

const FFramePiece* FramePieces()
{
	// 0x27A5B8
	static const FFramePiece P[8] = {
		{ 191.5f, 126.f, 128.5f, 0x05 }, { 320.f, 126.f, 128.5f, 0x04 }, { 448.5f, 126.f, 79.f, 0x12 }, { 448.5f, 205.f, 79.f, 0x13 },
		{ 320.f, 284.f, 128.5f, 0x02 }, { 191.5f, 284.f, 128.5f, 0x03 }, { 191.5f, 205.f, 79.f, 0x19 }, { 191.5f, 126.f, 79.f, 0x18 },
	};
	return P;
}

}
