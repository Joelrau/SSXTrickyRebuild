#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class USoundWave;

/** Keeps the runtime procedural waves (song player, intro / speech streams, FE and rank movie WAVs) alive until the audio mixer has released every source rendering them: a stopped voice is released on the render thread a few callbacks later, and a GC in that window trips a check() in AudioMixerSourceBuffer.cpp. Held waves are dropped once they no longer generate audio and at least MinHoldSeconds after the last Track / Retire. */
namespace SSXWaveKeepAlive
{
	constexpr double MinHoldSeconds = 2.0;

	SSXTRICKYREBUILD_API void Track(USoundWave* Wave);
	SSXTRICKYREBUILD_API void Retire(UAudioComponent* Component);
	SSXTRICKYREBUILD_API void Prune(double NowSeconds);
	SSXTRICKYREBUILD_API bool IsHeld(const USoundWave* Wave);
}
