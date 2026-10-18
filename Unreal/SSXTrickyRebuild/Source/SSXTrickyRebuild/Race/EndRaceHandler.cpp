#include "Race/EndRaceHandler.h"

namespace SSXEndRace
{
int32 RivalScene(TConstArrayView<TPair<int32, int32>> Others)
{
	int32 Best = RivalScoreMin;
	int32 Theirs = -1, Mine = -1;
	for (int32 K = 0; K < Others.Num(); ++K)
	{
		if (Best < Others[K].Key)
		{
			Theirs = K;
			Mine = -1;
			Best = Others[K].Key;
		}
		else if (Best < Others[K].Value)
		{
			Theirs = -1;
			Mine = K;
			Best = Others[K].Value;
		}
	}
	if (Theirs >= 0 && Theirs < 5)
	{
		return SceneVsUser + Theirs;
	}
	if (Mine >= 0 && Mine < 5)
	{
		return SceneUserVs + Mine;
	}
	return NoScene;
}

int32 ResultScene(int32 Mode, int32 Rank, int32 NumRacers, int32 ShowoffMedal, int32 FinishCs, int32 TimeToBeatCs)
{
	if (Mode == 3 || Mode == 5)
	{
		return ShowoffMedal != 3 ? SceneWin : SceneLose;
	}
	if (Mode == 7)
	{
		return Rank == 0 && float(FinishCs) < float(TimeToBeatCs) ? SceneWin : SceneLose;
	}
	return Rank < (NumRacers + 1) / 2 ? SceneWin : SceneLose;
}

void FHandler::Enter(SSXIntro::IEnv& Env, const SSXIntro::FSlot InSlots[NumSlots], int32 InScene1, int32 InScene2)
{
	for (int32 K = 0; K < NumSlots; ++K) { Slots[K] = InSlots[K]; }
	Scene1 = InScene1;
	Scene2 = InScene2;
	State = 0;
	bActive = true;
	bChainOpen = false;
	bSubState9 = false;
	// 0x1A7860, 0x175EB8, 0x175DE0
	Env.Act(TEXT("overlay_close"), OverlayHud);
	Env.Act(TEXT("overlay_close"), OverlayPopFinish);
	Env.Act(TEXT("director_reset"));
	Env.Act(TEXT("director_play"), 0, TEXT("PreLoad"));
}

bool FHandler::SceneMissing(int32 Slot) const
{
	return Slot == NoScene || Slot < 0 || Slot >= NumSlots || Slots[Slot].Type == SSXIntro::NullScript;
}

void FHandler::StartScene(SSXIntro::IEnv& Env, int32 Slot)
{
	// 0x1699A0, 0x1109B0
	Env.Act(TEXT("slot_start"), Slot);
	TArray<SSXIntro::FSlot::FLine> Lines;
	for (const SSXIntro::FSlot::FLine& L : Slots[Slot].Lines)
	{
		if (L.Speech >= 0 && Env.LineKey(L.LineId) >= 0) Lines.Add(L);
	}
	Lines.StableSort([&Env](const SSXIntro::FSlot::FLine& A, const SSXIntro::FSlot::FLine& B) { return Env.LineKey(A.LineId) < Env.LineKey(B.LineId); });
	if (Lines.Num() > 0)
	{
		Env.Act(TEXT("intro_speech_begin"));
		for (const SSXIntro::FSlot::FLine& L : Lines)
		{
			if (L.Speech >= 1000) Env.Act(TEXT("anim_speech"), L.Speech);
		}
	}
}

void FHandler::PlayScene(SSXIntro::IEnv& Env, int32 Slot)
{
	// 0x110A40, 0x169BB8, 0x1FF2B0
	Env.Act(TEXT("director_reset"));
	Env.Act(TEXT("director_play"), 0, SSXIntro::ScriptName(Slots[Slot].Type));
	Env.Act(TEXT("fade_in"), 1000);
}

bool FHandler::Skip(SSXIntro::IEnv& Env)
{
	// 0x110AB8, 0x110980
	if (Env.SkipPressed())
	{
		Env.Act(TEXT("intro_speech_end"));
		Env.Act(TEXT("director_reset"));
		return true;
	}
	return false;
}

int32 FHandler::Update(SSXIntro::IEnv& Env, bool bExitPending, bool bOverlayBusy)
{
	if (!bActive)
	{
		return State;
	}
	Env.Act(TEXT("loading_service"));   // 0x112920
	int32 S = State;
	for (int32 K = 0; K < 2; ++K)
	{
		const int32 Base = K * 4;
		const int32 Slot = K == 0 ? Scene1 : Scene2;
		if (S == Base)
		{
			if (SceneMissing(Slot))
			{
				S = Base + 4;
				continue;
			}
			StartScene(Env, Slot);
			S = Base + 2;
		}
		if (S == Base + 1 && !Env.Loading())
		{
			S = Base + 2;
		}
		if (S == Base + 2)
		{
			PlayScene(Env, Slot);
			S = Base + 3;
		}
		if (S == Base + 3)
		{
			if (!Env.DirectorRunning() && K == 0)
			{
				S = 4;
			}
			else if (!Env.DirectorRunning() || Skip(Env))
			{
				S = 8;
			}
		}
	}
	if (S == 8 && !bExitPending && !bOverlayBusy)
	{
		Env.Act(TEXT("fade_out"), 1000);         // 0x1FF4C8
		Env.Act(TEXT("intro_speech_end"));       // 0x110980
		Env.Act(TEXT("post_race_chain"));        // 0x1A7848
		bChainOpen = true;
		Env.Act(TEXT("director_reset"));
		Env.Act(TEXT("director_play"), 0, TEXT("PreLoad"));
		S = Env.Loading() ? 9 : 10;
	}
	if (S == 9 && !Env.Loading())
	{
		S = 10;
	}
	if (S == ResultsShown && !bExitPending && !bSubState9)
	{
		// 0x193C68, 0x210A38
		bSubState9 = true;
		Env.Act(TEXT("replay_state"), 9);
		Env.Act(TEXT("board_voices_restart"));
	}
	State = S;
	return State;
}
}
