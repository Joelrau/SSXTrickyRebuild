#include "World/SSXPropMovie.h"

#include "Effects/TrackEffects.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/RaceTrackData.h"
#include "UI/SSXFrontEndData.h"

namespace SSXPropMovie
{
	FString FileForTrackId(int32 TrackId)
	{
		switch (FMath::Clamp(TrackId, 0, 12))
		{
		case 0: return TEXT("jgari");
		case 1: return TEXT("jsnow");
		case 2: return TEXT("jelysium");
		case 3: return TEXT("jmesa");
		case 4: return TEXT("jmerquer");
		case 5: return TEXT("jaloha");
		case 6: return TEXT("jpipe");
		case 8: return TEXT("jmegaple");
		case 11: return TEXT("jalaska");
		default: return TEXT("jumbo");
		}
	}

	FString DefaultPath(const FString& TrackStem)
	{
		const int32 Id = SSXEffects::TrackIdForStem(TrackStem);
		if (Id < 0)
		{
			return FString();
		}
		return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("extracted"), TEXT("DATA"), TEXT("VIDEO"),
			FileForTrackId(FRaceTrackData::GameTrackOf(Id)).ToUpper() + TEXT(".SSS"));
	}

	namespace
	{
		// 0x2C27C8
		uint16 To5551(uint16 C)
		{
			return C == 0 ? 0 : uint16(((C & 0x1F) << 10) | ((C >> 1) & 0x3E0) | 0x8000 | (C >> 11));
		}

		uint16 Mix(uint16 A, uint16 B, int32 Wa, int32 Wb)
		{
			return uint16((((A & 0xF800) * Wa + (B & 0xF800) * Wb) >> 3) & 0xF800
				| (((A & 0x7E0) * Wa + (B & 0x7E0) * Wb) >> 3) & 0x7E0
				| (((A & 0x1F) * Wa + (B & 0x1F) * Wb) >> 3) & 0x1F);
		}
	}

	void DecodeBlock(const uint8* Block, uint16 Out[16])
	{
		uint16 C[4];
		C[0] = uint16(Block[0] | Block[1] << 8);
		C[1] = uint16(Block[2] | Block[3] << 8);
		if (C[1] < C[0])
		{
			C[2] = Mix(C[0], C[1], 5, 3);
			C[3] = Mix(C[0], C[1], 3, 5);
		}
		else
		{
			C[3] = 0;
			C[2] = uint16((((C[0] & 0xF800) + (C[1] & 0xF800)) >> 1) & 0xF800
				| (((C[0] & 0x7E0) + (C[1] & 0x7E0)) >> 1) & 0x7E0
				| (((C[0] & 0x1F) + (C[1] & 0x1F)) >> 1));
		}
		if (C[0] == 0) { C[0] = 1; }
		if (C[1] == 0) { C[1] = 1; }
		for (uint16& V : C) { V = To5551(V); }
		for (int32 Row = 0; Row < 4; ++Row)
		{
			const uint8 B = Block[4 + Row];
			for (int32 X = 0; X < 4; ++X) { Out[Row * 4 + X] = C[(B >> (2 * X)) & 3]; }
		}
	}

	FColor TexelColour(uint16 T)
	{
		return FColor(uint8((T & 0x1F) << 3), uint8(((T >> 5) & 0x1F) << 3), uint8(((T >> 10) & 0x1F) << 3), (T & 0x8000) ? 255 : 0);
	}

	void DecodeFrame(const uint8* Blocks, int32 W, int32 H, TArray<FColor>& Out)
	{
		Out.SetNumUninitialized(W * H);
		uint16 T[16];
		for (int32 By = 0; By < H / 4; ++By)
		{
			for (int32 Bx = 0; Bx < W / 4; ++Bx, Blocks += 8)
			{
				DecodeBlock(Blocks, T);
				for (int32 K = 0; K < 16; ++K) { Out[(By * 4 + K / 4) * W + Bx * 4 + K % 4] = TexelColour(T[K]); }
			}
		}
	}

	bool FSssFile::Load(const FString& Path, FString* OutError)
	{
		TArray<uint8> Bytes;
		if (Path.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
		{
			if (OutError) { *OutError = FString::Printf(TEXT("%s not found"), *Path); }
			return false;
		}
		return LoadFromMemory(MoveTemp(Bytes), OutError);
	}

	bool FSssFile::LoadFromMemory(TArray<uint8>&& Bytes, FString* OutError)
	{
		Data = MoveTemp(Bytes);
		Frames.Reset();
		auto U16 = [this](int32 O) { return int32(Data[O] | Data[O + 1] << 8); };
		int32 Pos = 0;
		while (Pos + ChunkHeader + ShapeHeader <= Data.Num())
		{
			if (FMemory::Memcmp(Data.GetData() + Pos, "SSSF", 4) != 0)
			{
				if (OutError) { *OutError = FString::Printf(TEXT("chunk %d at 0x%X: no SSSF tag"), Frames.Num(), Pos); }
				return false;
			}
			const int32 Size = int32(Data[Pos + 4] | Data[Pos + 5] << 8 | Data[Pos + 6] << 16 | uint32(Data[Pos + 7]) << 24);
			FFrame F;
			F.W = U16(Pos + ChunkHeader + 4);
			F.H = U16(Pos + ChunkHeader + 6);
			F.Offset = Pos + ChunkHeader + ShapeHeader;
			// 0x1FF928
			if (Size <= ChunkHeader || Pos + Size > Data.Num() || F.W <= 0 || F.H <= 0 || F.W % 4 || F.H % 4
				|| F.Offset + F.W * F.H / 2 > Pos + Size)
			{
				if (OutError) { *OutError = FString::Printf(TEXT("chunk %d at 0x%X: bad size %d / %d x %d"), Frames.Num(), Pos, Size, F.W, F.H); }
				return false;
			}
			Frames.Add(F);
			Pos += Size;
		}
		if (Frames.Num() == 0 && OutError) { *OutError = TEXT("no frames"); }
		return Frames.Num() > 0;
	}

	void FStream::Acquire()
	{
		if (Refs++ == 0)
		{
			bOpen = true;
			Counter = 0;
			LastDecode = 0;
			Next = 0;
		}
	}

	void FStream::Release()
	{
		if (Refs > 0 && --Refs == 0)
		{
			bOpen = false;
		}
	}

	bool FStream::Step(int32 NumFrames)
	{
		if (!bOpen || NumFrames <= 0)
		{
			return false;
		}
		++Counter;
		if (Counter - LastDecode < FramesPerVideoFrame)
		{
			return false;
		}
		LastDecode = Counter;
		Shown = Next;
		++Decodes;
		if (++Next >= NumFrames)
		{
			Next = 0;   // 0x2C73D0
		}
		return true;
	}
}
