// 0x11B018 / FUN_0021DCE0 / FUN_0021C610 / FUN_0021C6D0
#pragma once

#include "CoreMinimal.h"

namespace SSXTrickyAudio
{
	enum class ECmd : uint8
	{
		Zbxsfx,
		TrickyBank,
		PhraseSlice,
		PhraseStop,
		Speech,
	};

	struct FCmd
	{
		ECmd Kind = ECmd::Zbxsfx;
		int32 Value = 0;
		double AtMs = 0.0;
	};

	constexpr int32 MeterFullSound = 107;
	constexpr double MeterFullDelayMs = 500.0;
	constexpr int32 PointsTickSound = 77;     // FUN_0021a210
	constexpr int32 UberSound = 0, WarnSound = 1, EndSound = 2;
	constexpr double WarnTimer = 2.7;
	constexpr int32 McTricky = 0x204C;
	constexpr int32 DeferredSlots = 48;
	constexpr int32 PhraseAlign = 16, BeatsPerPhrase = 8;
	constexpr int32 AsyncVolume = 127;
}

// FUN_0021C610
struct SSXTRICKYREBUILD_API FSSXTrickyPhraseContext
{
	bool bSongLoaded = false;
	bool bSongEndPending = false;
	int32 Mode = 0;
	double BeatMs = 0.0;
	double MsToNextBeat = 0.0;
	int32 LastPhraseBase = -1;
};

class SSXTRICKYREBUILD_API FSSXTrickyAudio
{
public:
	using ECmd = SSXTrickyAudio::ECmd;
	using FCmd = SSXTrickyAudio::FCmd;

	void Reset();
	void Frame(double Timer, int32 Ubers);
	void MeterFull(double TimerBefore, double TimerNow, int32 Ubers, const FSSXTrickyPhraseContext& Ctx);
	void Advance(double Ms);   // the sound-library clock snd+0x80 (ms)

	bool bArcadeSfx = true;          // 0x32CA04
	bool bTrickyRider = false;
	int32 SavedUbers = 0;
	bool bFlagEnd = false;
	bool bFlagWarn = false;
	bool bFlagUber = false;
	bool bPhraseOn = false;
	double NowMs = 0.0;

	// FUN_00222CC0
	void Defer(double DelayMs, int32 Sound)
	{
		if (Deferred.Num() < SSXTrickyAudio::DeferredSlots) { Deferred.Add({ NowMs + DelayMs, Sound }); }
	}
	TArray<FCmd> Out;
	int32 NumDeferred() const { return Deferred.Num(); }

private:
	void Emit(ECmd Kind, int32 Value) { Out.Add({ Kind, Value, NowMs }); }
	void StopPhrase();

	struct FDeferred { double DueMs = 0.0; int32 Sound = 0; };
	TArray<FDeferred> Deferred;
	int32 PhraseBase = 0;
	int32 PhraseBeat = 0;
	bool bPhraseRepeat = false;
	double NextBeatMs = 0.0;
	double BeatMs = 0.0;
};
