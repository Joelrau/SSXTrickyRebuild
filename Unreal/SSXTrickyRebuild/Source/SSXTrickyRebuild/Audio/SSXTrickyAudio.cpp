#include "Audio/SSXTrickyAudio.h"

using namespace SSXTrickyAudio;

void FSSXTrickyAudio::Reset()
{
	const bool bArcade = bArcadeSfx;
	*this = FSSXTrickyAudio();
	bArcadeSfx = bArcade;
}

void FSSXTrickyAudio::StopPhrase()
{
	if (bPhraseOn)
	{
		bPhraseOn = false;
		Emit(ECmd::PhraseStop, 0);
	}
}

void FSSXTrickyAudio::Frame(double Timer, int32 Ubers)
{
	// FUN_0021C6D0
	if (bPhraseOn && Timer <= 0.0)
	{
		StopPhrase();
	}
	if (!bTrickyRider)
	{
		return;
	}
	if (bFlagUber && SavedUbers < Ubers)
	{
		StopPhrase();
		Emit(ECmd::TrickyBank, UberSound);
		bFlagWarn = bFlagEnd = bFlagUber = false;
		SavedUbers = Ubers;
		bTrickyRider = false;
		return;
	}
	if (bFlagEnd && Timer <= 0.0)
	{
		Emit(ECmd::TrickyBank, EndSound);
		bFlagWarn = bFlagEnd = false;
		bTrickyRider = false;
		return;
	}
	if (bFlagWarn && Timer < WarnTimer)
	{
		StopPhrase();
		Emit(ECmd::TrickyBank, WarnSound);
		bFlagWarn = false;
	}
}

void FSSXTrickyAudio::MeterFull(double TimerBefore, double TimerNow, int32 Ubers, const FSSXTrickyPhraseContext& Ctx)
{
	// FUN_0021DCE0
	Frame(TimerNow, Ubers);
	bTrickyRider = true;
	SavedUbers = Ubers;
	if (Deferred.Num() < DeferredSlots)
	{
		Deferred.Add({ NowMs + MeterFullDelayMs, MeterFullSound });
	}
	if (bArcadeSfx)
	{
		if (Ubers < 6)
		{
			bFlagEnd = bFlagWarn = true;
		}
		bFlagUber = true;
	}
	// FUN_0021C610
	if (TimerBefore > 0.0 || !bArcadeSfx || !Ctx.bSongLoaded || Ctx.bSongEndPending)
	{
		return;
	}
	if (!bPhraseOn && Ctx.LastPhraseBase >= 0 && Ctx.BeatMs > 0.0)
	{
		// FUN_002246A8
		bPhraseOn = true;
		bPhraseRepeat = true;
		PhraseBase = Ctx.LastPhraseBase;
		PhraseBeat = 0;
		BeatMs = Ctx.BeatMs;
		NextBeatMs = NowMs + Ctx.MsToNextBeat;
	}
	if (Ctx.Mode != 6 && Ubers < 6)
	{
		Emit(ECmd::Speech, McTricky);
	}
}

void FSSXTrickyAudio::Advance(double Ms)
{
	NowMs += Ms;
	for (int32 I = 0; I < Deferred.Num();)
	{
		if (Deferred[I].DueMs <= NowMs)
		{
			Emit(ECmd::Zbxsfx, Deferred[I].Sound);
			Deferred.RemoveAt(I);
		}
		else
		{
			++I;
		}
	}
	while (bPhraseOn && NextBeatMs <= NowMs)
	{
		if (PhraseBeat >= BeatsPerPhrase)
		{
			if (!bPhraseRepeat)
			{
				StopPhrase();
				break;
			}
			bPhraseRepeat = false;
			PhraseBeat = 0;
		}
		Emit(ECmd::PhraseSlice, PhraseBase + PhraseBeat);
		++PhraseBeat;
		NextBeatMs += BeatMs;
	}
}
