#pragma once

#include "CoreMinimal.h"

namespace SSXUISound
{
	// 0x270610 / FUN_00212300 / 0x2129F8
	struct FFeSound
	{
		int32 Sound = -1;
		int32 LevelClass = 4;
		int32 Voice = 0;
	};
	SSXTRICKYREBUILD_API FFeSound FeSoundForId(int32 Id, int32 FeMusicState);
	SSXTRICKYREBUILD_API FString FeId(int32 Sound);
	constexpr int32 FeWhooshA = 32;
	constexpr int32 FeWhooshB = 33;

	// FUN_002138F0 / 0x213D60 / 0x213F88 / 0x213B38
	/** Preview*: Options volume preview 0x212E98 (sound 0x70 at level class 0 master, 1 music, 2 PA speech, 3 character speech, 4 SFX). */
	enum class ERace : uint8 { Accept, Navigate, Back, PointChange, Denied, PreviewMaster, PreviewMusic, PreviewSpeech, PreviewChar, PreviewSfx,
		ReplayHelp, ReplayCamera, PauseOpen };   // 0x214428 / 0x214650 / 0x213D60
	struct FRaceSound
	{
		int32 Sound = -1;
		int32 FrameByte1 = -1;     // TODO: meaning (pan?)
		bool bStopShared = false;
	};
	// 0x215458
	SSXTRICKYREBUILD_API FRaceSound RaceSound(ERace Kind);
	SSXTRICKYREBUILD_API FString RaceId(int32 Sound);
	constexpr int32 CountdownBeep = 78;   // FUN_00216930
}
