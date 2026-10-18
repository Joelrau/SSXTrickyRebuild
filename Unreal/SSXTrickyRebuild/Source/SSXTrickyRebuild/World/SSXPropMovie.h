// TODO: the disc / IOP streaming latency (a chunk not yet read delays the decode to a later frame) is not modelled: th
#pragma once

#include "CoreMinimal.h"

namespace SSXPropMovie
{
	constexpr int32 Width = 128, Height = 64;
	constexpr int32 Fps = 15;                         // 0x148B28 / FUN_001FFF30
	constexpr int32 FramesPerVideoFrame = 60 / Fps;   // 0x1FFA88
	constexpr int32 ChunkHeader = 8;
	constexpr int32 ShapeHeader = 16;
	constexpr uint8 TypeDxt1 = 0x60;

	// 0x148B28
	SSXTRICKYREBUILD_API FString FileForTrackId(int32 TrackId);
	SSXTRICKYREBUILD_API FString DefaultPath(const FString& TrackStem);

	// 0x2C27C8
	SSXTRICKYREBUILD_API void DecodeBlock(const uint8* Block, uint16 Out[16]);
	SSXTRICKYREBUILD_API FColor TexelColour(uint16 T);
	// A frame's blocks (W x H / 2 bytes) -> W x H colours (row-major, top row first).
	SSXTRICKYREBUILD_API void DecodeFrame(const uint8* Blocks, int32 W, int32 H, TArray<FColor>& Out);

	class SSXTRICKYREBUILD_API FSssFile
	{
	public:
		bool Load(const FString& Path, FString* OutError = nullptr);
		bool LoadFromMemory(TArray<uint8>&& Bytes, FString* OutError = nullptr);
		int32 NumFrames() const { return Frames.Num(); }
		int32 FrameWidth(int32 F) const { return Frames[F].W; }
		int32 FrameHeight(int32 F) const { return Frames[F].H; }
		// The block data of frame F (W x H / 2 bytes).
		const uint8* FrameBlocks(int32 F) const { return Data.GetData() + Frames[F].Offset; }
		void Decode(int32 F, TArray<FColor>& Out) const { DecodeFrame(FrameBlocks(F), Frames[F].W, Frames[F].H, Out); }

	private:
		struct FFrame { int32 Offset = 0; int32 W = 0; int32 H = 0; };
		TArray<uint8> Data;
		TArray<FFrame> Frames;
	};

	// The shared stream of a track (cVideoStreamMan entry), stepped once per 60 Hz frame.
	struct SSXTRICKYREBUILD_API FStream
	{
		int32 Refs = 0;
		bool bOpen = false;
		int32 Counter = 0;
		int32 LastDecode = 0;
		int32 Next = 0;
		int32 Shown = INDEX_NONE;
		int32 Decodes = 0;

		// 0x1FFF30 / 0x1FF990
		void Acquire();
		// 0x200098 / 0x1FFA10
		void Release();
		// 0x1FFA88
		bool Step(int32 NumFrames);
	};
}
