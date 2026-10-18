#include "Race/PreRaceIntro.h"

namespace SSXIntro
{
FString ScriptName(int32 Type)
{
	static const TCHAR* Base[] = {
		TEXT("RaceLoaded"), TEXT("FreeLoaded"), TEXT("ShowLoaded"), TEXT("STG_VEN_1"), TEXT("STG_VEN_2"), TEXT("STG_VEN_3"),
		TEXT("STG_VEN_4"), TEXT("UNTR_VEN"), TEXT("PIPE_VEN"), TEXT("STG_COM_1"), TEXT("STG_COM_2"), TEXT("STG_COM_3"),
		TEXT("STG_COM_4"), TEXT("GAT_6COM_1"), TEXT("GAT_6COM_2"), TEXT("GAT_6COM_3"), TEXT("GAT_6COM_4"), TEXT("GAT_SO"),
		TEXT("GAT_FR_1"), TEXT("GAT_FR_2"), TEXT("GAT_FR_3"), TEXT("GAT_FO_1"), TEXT("GAT_FO_2"), TEXT("GAT_FO_3"),
		TEXT("CRD_1"), TEXT("CRD_2"), TEXT("CRD_3"), TEXT("FL_WIN_1"), TEXT("FL_WIN_2"), TEXT("FL_WIN_3"),
		TEXT("FL_LOS_1"), TEXT("FL_LOS_2"), TEXT("FL_LOS_3") };
	static const TCHAR* Riders[] = { TEXT("EDD"), TEXT("KAO"), TEXT("LUT"), TEXT("MAC"), TEXT("MOB"), TEXT("ZOE"),
		TEXT("JP"), TEXT("ELI"), TEXT("PSY"), TEXT("SEE"), TEXT("BRO"), TEXT("MAR") };
	constexpr int32 NB = UE_ARRAY_COUNT(Base);
	if (Type >= 0 && Type < NB) return Base[Type];
	if (Type >= NB && Type < NB + 12) return FString::Printf(TEXT("FL_USR_VS_%s"), Riders[Type - NB]);
	if (Type >= NB + 12 && Type < NB + 24) return FString::Printf(TEXT("FL_%s_VS_USR"), Riders[Type - NB - 12]);
	return TEXT("Null");
}

void FHandler::Enter(IEnv& Env, int32 InMode, int32 InVenue, const FSlot InSlots[3], EEntry Entry)
{
	Mode = InMode;
	Venue = InVenue;
	for (int32 K = 0; K < 3; ++K) Slots[K] = InSlots[K];
	Step = Entry == EEntry::Normal ? 2 : (Entry == EEntry::NextHeat ? 0 : 0x12);
	Env.Act(TEXT("overlays_close_all"));
	// 0x22E400
	if (Entry == EEntry::Normal)
	{
		Env.Act(TEXT("career_venue_counter"));
		Env.Act(TEXT("audio_intro_flag"), 0);
		Env.Act(TEXT("ambient_enable"), 0);
	}
	else if (Entry == EEntry::NextHeat)
	{
		Env.Act(TEXT("audio_intro_flag"), 0);
		Env.Act(TEXT("audio_intro_start"));
		Env.Act(TEXT("ambient_enable"), 0);
		Env.Act(TEXT("ambient_enable"), 2);
	}
	Env.Act(TEXT("director_reset"));
	Env.Act(TEXT("director_play"), 0, TEXT("PreLoad"));
	if (!GatesUsed(Mode, Venue))
	{
		Env.Act(TEXT("track_event"), 0, TEXT("NoCountdown"));
	}
	Env.Act(TEXT("race_state_hook"));   // 0x112550
}

void FHandler::StartSlot(IEnv& Env, int32 K)
{
	// 0x1699A0, 0x1109B0
	Env.Act(TEXT("slot_start"), K);
	TArray<FSlot::FLine> Lines;
	for (const FSlot::FLine& L : Slots[K].Lines)
	{
		if (L.Speech >= 0 && Env.LineKey(L.LineId) >= 0) Lines.Add(L);
	}
	Lines.StableSort([&Env](const FSlot::FLine& A, const FSlot::FLine& B) { return Env.LineKey(A.LineId) < Env.LineKey(B.LineId); });
	if (Lines.Num() > 0)
	{
		Env.Act(TEXT("intro_speech_begin"));
		for (const FSlot::FLine& L : Lines)
		{
			if (L.Speech >= 1000) Env.Act(TEXT("anim_speech"), L.Speech);
		}
	}
}

void FHandler::PlaySlotScript(IEnv& Env, int32 K)
{
	Env.Act(TEXT("director_reset"));
	Env.Act(TEXT("director_play"), 0, ScriptName(Slots[K].Type));
	Env.Act(TEXT("fade_in"), 1000);
}

bool FHandler::Skip(IEnv& Env)
{
	if (Env.SkipPressed())
	{
		Env.Act(TEXT("intro_speech_end"));
		Env.Act(TEXT("director_reset"));
		return true;
	}
	return false;
}

int32 FHandler::CardDecision(IEnv& Env)
{
	if (Env.HelpNeeded())
	{
		Env.Act(TEXT("overlay_open"), OverlayHelpRace);
		return 0x10;
	}
	if (Mode == 1)
	{
		return 0x12;
	}
	Env.Act(TEXT("overlay_open"), OverlayNextRace);
	return 0x11;
}

int32 FHandler::Update(IEnv& Env)
{
	if (Step == Done)
	{
		return Step;
	}
	Env.Act(TEXT("loading_service"));
	int32 S = Step;
	if (S == 0)
	{
		Env.Act(TEXT("director_reset"));
		Env.Act(TEXT("director_play"), 0, TEXT("PreLoad"));
		S = 1;
	}
	if (S == 1 && !Env.Loading())
	{
		S = 2;
	}
	static const int32 Steps[3][4] = { { 2, 3, 4, 5 }, { 6, 7, 8, 9 }, { 10, 11, 12, 13 } };
	static const int32 After[3] = { 6, 10, 0xE };
	static const int32 AfterSkip[3] = { 6, 0xE, 0xE };
	for (int32 K = 0; K < 3; ++K)
	{
		const int32 Start = Steps[K][0], WaitLoad = Steps[K][1], Play = Steps[K][2], Wait = Steps[K][3];
		if (S == Start)
		{
			if (K == 1) Env.Act(TEXT("ambient_enable"), 2);
			if (K == 1) Env.Act(TEXT("audio_intro_start"));
			if (Slots[K].Type == NullScript)
			{
				S = After[K];
			}
			else
			{
				if (K == 2) Env.Act(TEXT("racers_intro_reset"));
				StartSlot(Env, K);
				S = Play;
			}
		}
		if (S == WaitLoad && !Env.Loading())
		{
			S = Play;
		}
		if (S == Play)
		{
			if (K == 0) Env.Act(TEXT("ambient_enable"), 2);
			if (K == 0) Env.Act(TEXT("audio_intro_start"));
			PlaySlotScript(Env, K);
			S = Wait;
		}
		if (S == Wait)
		{
			if (!Env.DirectorRunning()) S = After[K];
			else if (Skip(Env)) S = AfterSkip[K];
		}
	}
	if (S == 0xE)
	{
		Env.Act(TEXT("fade"), 1000);
		Env.Act(TEXT("place_racers"), GatesUsed(Mode, Venue) ? 1 : 0);
		S = Env.Loading() ? 0xF : CardDecision(Env);
	}
	if (S == 0xF && !Env.Loading())
	{
		S = CardDecision(Env);
	}
	if (S == 0x10 && Skip(Env))
	{
		Env.Act(TEXT("overlay_close"), OverlayHelpRace);
		if (Mode == 1)
		{
			S = 0x12;
		}
		else
		{
			Env.Act(TEXT("overlay_open"), OverlayNextRace);
			S = 0x11;
		}
	}
	if (S == 0x11 && Skip(Env))
	{
		Env.Act(TEXT("overlay_close"), OverlayHelpRace);
		Env.Act(TEXT("overlay_close"), OverlayNextRace);
		S = 0x12;
	}
	if (S == 0x12)
	{
		Env.Act(TEXT("intro_music_end"));
		Env.Act(TEXT("ambient_enable"), 1);
		Env.Act(TEXT("audio_intro_flag"), 1);
		S = Env.Loading() ? 0x13 : 0x14;
	}
	if (S == 0x13 && !Env.Loading())
	{
		S = 0x14;
	}
	if (S == 0x14)
	{
		Env.Act(TEXT("director_reset"));
		Env.Act(TEXT("intro_speech_end"));
		Env.Act(TEXT("place_racers"), GatesUsed(Mode, Venue) ? 1 : 0);
		Env.Act(TEXT("phase_next"));
		S = Done;
	}
	Step = S;
	return S;
}

FTimeline CountdownTimeline(int32 E)
{
	FTimeline T;
	auto Clock = [E](int32 F) { return ClockStart + ClockStep * (F - E + 1); };
	int32 F = E;
	while (Clock(F) < 0)
	{
		if (Clock(F) % 60 == 0) T.Beeps.Add(F);
		++F;
	}
	T.Go = F;
	T.RaceEnter = F + 1;
	return T;
}
}
