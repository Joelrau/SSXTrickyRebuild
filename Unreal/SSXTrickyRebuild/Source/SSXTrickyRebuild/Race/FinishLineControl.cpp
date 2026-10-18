#include "Race/FinishLineControl.h"
#include "Misc/ScopeExit.h"

#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/GroundControls.h"

namespace SSXFinishLine
{
namespace
{
	// 0x126B38, 0x126C00, 0x126C78
	void BrakeHard(FRiderSim& Sim)
	{
		FRiderState& St = Sim.State;
		if (St.Crouch.Cur > 0.0)
		{
			St.Crouch.Target = 0.0;
			St.Crouch.Rate = 5.0024881;
			return;
		}
		SSXGround::SetCrouchTarget(Sim, 0.0);
		SSXGround::SetBrakeTarget(Sim, 1.0);
	}

	int32 ClipFrames(const FRiderSim& Sim, int32 AnimId)
	{
		const SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim);
		return Ch ? Ch->Frames(Sim, AnimId)
			: SSXAnim::FAnimTable::Shared().Frames(AnimId, Sim.State.BoardClass, Sim.Profile.GetTrickCharacter());
	}

	void RequestPlay(const FRiderSim& Sim, int32 AnimId)
	{
		if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim))
		{
			Ch->RequestPlay(AnimId);
		}
	}
}

bool IsWinner(int32 Mode, int32 Rank, int32 NumRacers, int32 ShowoffMedal, int32 FinishCs, int32 TimeToBeatCs, bool bLessonPassed)
{
	if (Mode == 3 || Mode == 5)
	{
		return ShowoffMedal != 3;
	}
	if (Mode == 7)
	{
		return Rank == 0 && float(FinishCs) < float(TimeToBeatCs);
	}
	if (Mode == 6)
	{
		return bLessonPassed;
	}
	return Rank < (NumRacers + 1) / 2;
}

void FControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	// 0x106600
	Phase = Sim.State.Velocity.Size() > SlowSpeed ? 0 : 1;
	Frames = 0;
	Result = -1;
	TauntPlayed = 0;
	AnimStart = 0;
	Sim.State.bFinishLineDone = false;
}

void FControl::Update(FRiderSim& Sim, const FRiderInputWord& Input)
{
	FRiderState& St = Sim.State;
	++Frames;
	ON_SCOPE_EXIT { Sim.State.bFinishLineDone = IsDone(); };   // 0x1065C8, 0x11D998
	const bool bWait = Frames < WaitLimit;
	switch (Phase)
	{
	case 0:
		BrakeHard(Sim);
		if (bWait && FMath::Abs(St.Brake.Cur) != 1.0 && St.Velocity.Size() >= SlowSpeed)
		{
			return;
		}
		SSXGround::SetSteerTarget(Sim, 0.0);   // 0x126948
		Phase = 1;
		return;
	case 1:
		// 0x126C00, 0x126C78
		SSXGround::SetCrouchTarget(Sim, 0.0);
		SSXGround::SetSteerTarget(Sim, 0.0);
		SSXGround::SetBrakeTarget(Sim, 0.0);
		St.Velocity *= VelocityDecay;
		if (bWait && (St.Brake.Cur != 0.0 || St.Steer.Cur != 0.0))
		{
			return;
		}
		RequestPlay(Sim, FinishAnim);
		Phase = 2;
		AnimStart = Frames;
		St.Velocity = FVector::ZeroVector;
		return;
	case 2:
	{
		St.Velocity = FVector::ZeroVector;
		// 0x15FF40
		const int32 N = ClipFrames(Sim, FinishAnim);
		const bool bClipDone = N > 0 && Frames - AnimStart >= SSXAnim::OneShotFrames(N);
		if (bWait && !bClipDone)
		{
			return;
		}
		const int32 Now = RaceFrame ? RaceFrame() : 0;
		Result = IsWinner && IsWinner(Sim) ? 1 : 0;
		if (Result) { WinStamp = Now; LoseStamp = -1; } else { LoseStamp = Now; WinStamp = -1; }
		RequestPlay(Sim, CycleAnim);
		// 0x128150, 0x127E80, 0x235298, 0x235468
		if (WinStamp >= 0)
		{
			if (Now - WinStamp < TauntWindow) { RequestPlay(Sim, WinTaunt); TauntPlayed = WinTaunt; }
			WinStamp = -1;
		}
		if (LoseStamp >= 0)
		{
			if (Now - LoseStamp < TauntWindow) { RequestPlay(Sim, LoseTaunt); TauntPlayed = LoseTaunt; }
			LoseStamp = -1;
		}
		Sim.SetMotion(ERiderMotion::Static, TEXT("FinishLine phase 2 (0x11F610 motion 4)"));
		Phase = 3;
		AnimStart = Frames;
		return;
	}
	case 3:
	{
		// 0x15EEC0
		const int32 N = TauntPlayed ? ClipFrames(Sim, TauntPlayed) : 0;
		if (N > 0 && Frames - AnimStart < 2 * SSXAnim::OneShotFrames(N))
		{
			return;
		}
		Phase = 4;
		return;
	}
	default:
		return;
	}
}
}
