// 0x11A348 / 0x21D0B0 / 0x21D5C8 / 0x21D0C0
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXAmbientEmitters.h"

struct SSXTRICKYREBUILD_API FSSXZoneAmbience
{
	static constexpr float StopFade = 2.0f;        // 0x21D5C8 / 0x20C2A0
	static constexpr int32 SwapVolume = 0x23;
	static constexpr int32 DefaultVolume = 100;
	static constexpr int32 TrackBankIndex = 9;

	TFunction<int32(const FSSXAmbientStart&)> Start;
	TFunction<void(int32 Handle, float Fade)> Stop;

	int32 Voice = INDEX_NONE;
	bool bPending = false;
	int32 Kind = 0;

	static int32 KindOf(int32 InternalType);
	// 0x21D0C0
	static FSSXAmbientStart Push(int32 Kind);

	/** Trigger edge of racer R (0x11A348): enter -> request (0x21D0B0), leave -> StopVoice. Repeated enters and a leave without an enter are dropped. */
	void Trigger(int32 Racer, int32 InternalType, bool bEnter);
	// 0x21D0C0
	void Update();
	// 0x21D5C8
	void StopVoice();
	// 0x20DB18
	void Reset();
	// 0x211B58 / 0x21D5C8
	void Restart();

private:
	TArray<uint32> Masks;
};
