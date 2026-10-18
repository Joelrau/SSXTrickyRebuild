#include "Race/SSXReplayKeyframe.h"

void FSSXRaceKeyframe::Save(const FRaceSession& S, uint32 InFrame)
{
	S.SaveReplay(Session);
	Riders.SetNum(S.NumRacers());
	for (int32 I = 0; I < S.NumRacers(); ++I)
	{
		if (const FRiderSim* Sim = S.GetRacer(I).Sim) { Sim->SaveReplay(Riders[I]); }
	}
	RngA = FGameRng::StreamA();
	RngB = FGameRng::StreamB();
	Frame = InFrame;
	bValid = true;
}

void FSSXRaceKeyframe::Load(FRaceSession& S) const
{
	if (!bValid)
	{
		return;
	}
	S.LoadReplay(Session);
	for (int32 I = 0; I < S.NumRacers() && I < Riders.Num(); ++I)
	{
		if (FRiderSim* Sim = S.GetRacerMutable(I).Sim) { Sim->LoadReplay(Riders[I]); }
	}
	FGameRng::StreamA() = RngA;
	FGameRng::StreamB() = RngB;
}
