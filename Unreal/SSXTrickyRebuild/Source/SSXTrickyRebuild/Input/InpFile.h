#pragma once

#include "CoreMinimal.h"
#include "Input/PadMapper.h"

// Tutorial .inp: 24-byte header (u32 frames, u32 frames, 16 zero bytes), then 44-byte frames
// (u32 word, u32 bools, u32 grabs, u32 buttons, u32 filtered, f32 pad x / y, left x / y, right x / y).
class SSXTRICKYREBUILD_API FInpFile
{
public:
	static constexpr int32 HeaderSize = 24;
	static constexpr int32 FrameSize = 44;
	static constexpr int32 FrameCapacity = 7200;

	static bool Load(const FString& Path, TArray<FInpFrame>& OutFrames, FString* OutError = nullptr);

	static FString TutorialPath(const FString& Name);

	static FString UnpackedDir();
};
