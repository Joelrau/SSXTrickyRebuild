#include "Sim/Ground/GroundControls.h"
#include "HAL/IConsoleManager.h"

#include "Sim/Ground/GroundMotion.h"
#include "Sim/Ground/GroundPhysics.h"
#include "Sim/EEFloat.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Race/GameRng.h"

namespace SSXGround
{
namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;
	constexpr float GateDt = 0.016666668f;

	bool MovingForward(const FRiderState& St) { return FVector::DotProduct(St.Velocity, St.BasisForward) > 0.0; }
	bool OnBoard(const FRiderState& St) { return St.Motion == M::Ground || St.Motion == M::RailSlide; }

	bool AnimControlDone(const FRiderSim& Sim, const FGroundContext& Ctx, int32 AnimId, int32 FallbackFrames)
	{
		return SSXAnim::FramesInControl(Sim.State) >= Ctx.Anim->ControlFramesFor(Sim, AnimId, FallbackFrames);
	}

	float GateGain(const FRiderSim& Sim, const FGroundContext& Ctx)
	{
		// 0x105F58, 0x106228, 0x1062B0
		const float B = float(MakeGroundRider(Sim, Ctx).StatGate * 255.0);
		return SSXEE::FpuAdd(B * float(K::GateGainStat) * 0.003921569f, float(K::GateGainBase));
	}

	// 0x15FDE0, 0x158798
	double GateMarkerPhase(const FRiderSim& Sim, const FGroundContext& Ctx, int32 N)
	{
		const double Fallback = N == 1 ? Ctx.Config.GateRockPhase : Ctx.Config.GatePushPhase;
		const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
		if (!Lib)
		{
			return Fallback;
		}
		const SSXAnim::FAflClip* Clip = Lib->Clip(Lib->Resolve(0x1FC, Sim.State.BoardClass, Sim.Profile.GetTrickCharacter()));
		if (!Clip)
		{
			return Fallback;
		}
		return double(float(uint16(Clip->Markers[N - 1])) / float(FMath::Max(Clip->NFrames - 1, 1)));
	}

	// 0x11E9D8
	double GateSuspension(const FRiderSim& Sim) { return double(SSXEE::FpuAdd(float(Sim.Profile.SizeScale) * -100.0f, 75.0f)); }

	void StepGate(FApproachFilter& F)
	{
		const float Cur = float(F.Cur), Target = float(F.Target);
		const float Step = float(F.Rate) * GateDt;
		if (SSXEE::FpuAdd(Target, Step) < Cur) { F.Cur = SSXEE::FpuSub(Cur, Step); }
		else if (Cur < SSXEE::FpuSub(Target, Step)) { F.Cur = SSXEE::FpuAdd(Cur, Step); }
		else { F.Cur = F.Target; }
	}
}

void SetSteerTarget(FRiderSim& Sim, double Turn)
{
	// 0x126948
	FRiderState& St = Sim.State;
	float T = float(Turn);
	if (St.BoostStrength == 0.0)
	{
		T = FMath::Clamp(T, -float(K::SteerClamp), float(K::SteerClamp));
	}
	const float Ratio = SSXEE::Length(SSXEE::F(St.Velocity)) / float(SteerKnee(St.BoardClass));
	if (Ratio < 1.0f)
	{
		T = T * Ratio;
	}
	float Rate = FMath::Clamp(FMath::Abs(SSXEE::FpuSub(T, float(St.Steer.Cur))) * float(K::SteerRateK), float(K::SteerRateMin), float(K::SteerRateMax));
	if (IsDeepSnow(St.SurfaceId))
	{
		Rate = Rate * float(K::DeepSnowSteerRate);
	}
	St.Steer.Target = T;
	St.Steer.Rate = Rate;
}

void SetCrouchTarget(FRiderSim& Sim, double Value)
{
	FRiderState& St = Sim.State;
	if (Value > 0.0 && St.Brake.Cur != 0.0)
	{
		Value = 0.0;
	}
	// 0x126C00
	St.Crouch.Target = Value;
	const float D = FMath::Abs(SSXEE::FpuSub(float(Value), float(St.Crouch.Cur)));
	St.Crouch.Rate = (D < 0.1f ? 0.1f : D) * float(K::CrouchRateK);
}

void SetBrakeTarget(FRiderSim& Sim, double Value)
{
	FRiderState& St = Sim.State;
	if (Value != 0.0 && St.Crouch.Cur > 0.0)
	{
		Value = 0.0;
	}
	// 0x126C78
	float D = FMath::Abs(SSXEE::FpuSub(float(Value), float(St.Brake.Cur)));
	D = D < 0.1f ? 0.1f : D;
	if (Value == 0.0)
	{
		D = SSXEE::FpuSub(1.1f, D);
	}
	St.Brake.Target = float(Value);
	St.Brake.Rate = D * float(K::BrakeRateK);
}

void BoostHandler(FRiderSim& Sim, bool bHeld, bool bPressed)
{
	FRiderState& St = Sim.State;
	if (!bHeld || St.BoostMeter <= 0.0)
	{
		if (bHeld && bPressed && St.BoostMeter <= 0.0)
		{
			++St.BoostEmptyPressCount;   // 0x11D038, 0x2197D8
		}
		St.BoostStrength = 0.0;
		St.BoostPose.Rate = K::BoostPoseOffRate;
		St.BoostPose.Target = 0.0;
		return;
	}
	if (St.BoostStrength == 0.0)
	{
		// 0x2192A8
		++St.BoostStartCount;
		St.bBoostStartPressed = bPressed;
		if (bPressed)
		{
			const double M0 = St.BoostMeter;
			St.BoostPose.Rate = K::BoostPoseOnRate;
			St.BoostPose.Target = M0 > K::BoostTierHigh ? K::BoostPoseHigh : (M0 > K::BoostTierMid ? K::BoostPoseMid : K::BoostPoseLow);
			St.BoostFlash.Cur = 1.0;
			St.BoostFlash.Rate = 0.0;
			St.BoostFlash.Target = 1.0;
		}
	}
	const double Meter = St.BoostMeter;
	const double PoseLimit = Meter > K::BoostTierHigh ? K::BoostPoseHigh : (Meter > K::BoostTierMid ? K::BoostPoseMid : K::BoostPoseLow);
	St.BoostStrength = Meter > K::BoostTierHigh ? 1.0 : (Meter > K::BoostTierMid ? K::BoostStrengthMid : K::BoostStrengthLow);
	if (!St.bInfiniteBoost)
	{
		// 0x11B1F8
		St.BoostMeter = Meter - K::BoostDrainPerFrame;
	}
	if (PoseLimit <= St.BoostPose.Cur)
	{
		if (St.BoostFlash.Cur == 1.0)
		{
			St.BoostFlash.Target = 0.0;
			St.BoostFlash.Rate = FMath::Abs(0.0 - St.BoostFlash.Cur) / K::BoostFlashOutSeconds;
		}
		else if (St.BoostFlash.Cur <= 0.0)
		{
			St.BoostPose.Target = 0.0;
			St.BoostPose.Rate = K::BoostPoseOffRate;
		}
	}
}


static TAutoConsoleVariable<int32> CVarControlInputs(TEXT("ssx.Rider.ControlInputs"), 0,
	TEXT("Bit mask of the original's per-control ground / air inputs (GroundControls.h ControlInputsMask); 0 = the shared Cruise inputs"));

int32 ControlInputsMask()
{
	return CVarControlInputs.GetValueOnAnyThread();
}

void FGroundControlBase::RideInputs(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	SetSteerTarget(Sim, In.Turn);
	double CrouchIn = FMath::Max(In.Crouch, 0.0);
	const double BrakeIn = FMath::Max(In.Brake, 0.0);
	if (St.BoardClass == 2)
	{
		CrouchIn = FMath::Min(CrouchIn, 0.5);
	}
	CrouchBrake(Sim, CrouchIn, BrakeIn);
	// 0x102FC8
	St.SteerFollow.Target = (ControlInputsMask() & 4) && Ctx->bPushAnim ? 0.0 : St.Steer.Cur;
	St.SteerFollow.Rate = K::SteerFollowRate;
}

void FGroundControlBase::CrouchBrake(FRiderSim& Sim, double CrouchIn, double BrakeIn)
{
	FRiderState& St = Sim.State;
	if (CrouchIn > 0.0 && St.Brake.Cur != 0.0)
	{
		St.Brake.Target = 0.0;
		St.Brake.Rate = K::BrakeRateK;
	}
	else if (BrakeIn != 0.0 && St.Crouch.Cur > 0.0)
	{
		St.Crouch.Target = 0.0;
		St.Crouch.Rate = K::CrouchRateK;
	}
	else
	{
		SetCrouchTarget(Sim, CrouchIn);
		SetBrakeTarget(Sim, BrakeIn);
	}
}

void FCruiseControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	Sim.State.GrindType = 0;   // 0x102FB0
	Sim.State.CruiseLookTimer = 0.0;
	Sim.State.CruiseTauntTimer = 0.0;
}

void CruiseOverlays(FRiderSim& Sim)
{
	// 0x102FC8, 0x103660, 0x1033D0, 0x103B88, 0x15EEC0
	FRiderState& St = Sim.State;
	SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim);
	IRiderRaceHooks::FRacerTable T;
	const int32 N = (Sim.GetRaceHooks() && Sim.GetRaceHooks()->RacerTable(Sim, T)) ? T.NumRacers : 0;
	auto Ch1Idle = [&St]() { return St.Frame >= St.Ch1EndFrame; };
	auto Play = [&](int32 Id)
	{
		if (Ch)
		{
			Ch->PlayOverlay(Sim, Id);
			return;
		}
		if (SSXAnim::RandomVariantCount(Id) > 0) { FGameRng::StreamB().Next(); }   // 0x15ADA8
		St.Ch1Anim = Id;
		St.Ch1EndFrame = St.Frame + 1;
	};
	auto Rel = [&](int32 J)
	{
		const float D = float(T.Bearing[J]) - float(St.Yaw);
		const float F = D * 0.15915494f + 0.5f;
		float Fl = float(int32(F));
		if (F < Fl) { Fl -= 1.0f; }
		return D - Fl * 6.2831855f;
	};
	const uint32 Goofy = St.bStanceGoofy ? 1u : 0u;

	// 0x128150, 0x108378, 0x235298, 0x127E80
	if (St.PositiveReactionFrame >= 0 && Ch1Idle())
	{
		if (St.Frame - St.PositiveReactionFrame < 360)
		{
			Play(0x214);
			if (Sim.GetRaceHooks()) { Sim.GetRaceHooks()->OnWinTaunt(Sim, St.PositiveReactionStrength); }
		}
		St.PositiveReactionFrame = -1;
	}

	// 0x234F68
	if (Ch1Idle())
	{
		St.CruiseTauntTimer = double(float(St.CruiseTauntTimer) + 0.016666668f);
		if (float(St.CruiseTauntTimer) >= 1.0253273f && FGameRng::StreamB().Next() % 100u < 0x5Bu && N > 0)
		{
			for (int32 J = 0; J < N; ++J)
			{
				if (J == T.Self || !(T.bGrudge && T.GrudgeRival == J && T.GrudgeKind >= 0 && T.GrudgeKind < 3))
				{
					continue;
				}
				const float A = Rel(J);
				if (float(T.Dist[J]) < 1000.13214f && FMath::Abs(A) > 1.0471976f)
				{
					if (Sim.GetRaceHooks()) { Sim.GetRaceHooks()->OnTaunt(Sim, J); }
					Play(FMath::Abs(A) > 2.3561945f ? 0x211 : ((A < 0.0f ? 1u : 0u) == Goofy ? 0x213 : 0x212));
					break;
				}
			}
		}
	}
	else
	{
		St.CruiseTauntTimer = 0.0;
	}

	if (Ch1Idle())
	{
		St.CruiseLookTimer = double(float(St.CruiseLookTimer) + 0.016666668f);
		if (float(St.CruiseLookTimer) >= 1.0f)
		{
			for (int32 J = 0; J < N; ++J)
			{
				if (J == T.Self)
				{
					continue;
				}
				const float A = Rel(J);
				if (float(T.Dist[J]) < 1477.6294f && FMath::Abs(A) > 2.0943952f)
				{
					Play((A < 0.0f ? 1u : 0u) == Goofy ? 0x210 : 0x20F);
					break;
				}
			}
		}
	}
	else
	{
		St.CruiseLookTimer = 0.0;
	}

	if (Ch1Idle())
	{
		const float Steer = float(St.Steer.Cur);
		if (FMath::Abs(Steer) > 0.5502836f && Steer * float(St.Steer.Rate) > 0.0f && FGameRng::StreamB().Next() % 0x60u == 0u)
		{
			const float Crouch = float(St.Crouch.Cur);
			if (Crouch < 0.2f)
			{
				Play(FMath::Abs(Steer) <= 0.8000276f ? 0x20A : 0x20B);
			}
			else if (Crouch > 0.9f)
			{
				Play(0x209);
			}
		}
	}
}

void FCruiseControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	BoostHandler(Sim, In.bBoostHeld, In.bBoostPressed);
	RideInputs(Sim, In);
	FGroundContext& X = *Ctx;
	X.PushFramesLeft = FMath::Max(0, X.PushFramesLeft - 1);
	if (X.bPushAnim && X.PushFramesLeft <= 0)
	{
		X.bPushAnim = false;
	}
	if (!X.bPushAnim && In.Crouch > 0.0 && FMath::Abs(St.Steer.Cur) < 0.2 && St.Brake.Cur == 0.0
		&& St.Velocity.Size() < K::PushAnimSpeed)
	{
		X.bPushAnim = true;
		X.PushFramesLeft = SSXAnim::OneShotFrames(X.Anim->Frames(Sim, SSXAnim::SkatePush));
		X.Anim->RequestPlay(SSXAnim::SkatePush);
	}
	if (St.FinishResult >= 0 && !Ctx->bPushAnim)
	{
		Sim.ApplyTransition(TEXT("T10"));
		return;
	}
	if (In.bJumpPressed && St.Brake.Cur == 0.0)
	{
		Sim.ApplyTransition(TEXT("T11"));
		St.Highlight(15, 1.f);   // 0x102FC8
	}
}

void FCruiseControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	// 0x1033C0, 0x219760
	BoostHandler(Sim, false);
	Ctx->bPushAnim = false;
	Ctx->PushFramesLeft = 0;
}

// 0x103EB8, 0x104030, 0x104178
bool ChangeFakieSkipsSwitch(const FRiderSim& Sim)
{
	return Sim.State.BoardClass == 2 && (Sim.State.bStanceGoofy ? 1 : 0) == (Sim.Profile.Stance != 0 ? 1 : 0);
}

void FChangeFakieControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	FRiderState& St = Sim.State;
	St.bStanceGoofy = !St.bStanceGoofy;
	Sim.RotateBody(St.BasisUp, UE_DOUBLE_PI);
	if (Ctx->Anim.IsValid())
	{
		Ctx->Anim->LayerYawAdd += UE_DOUBLE_PI;   // 0x15EEF8
	}
	St.Steer.Cur = -St.Steer.Cur;
	St.Lean.Cur = -St.Lean.Cur;
	St.SteerFollow.Target = 0.0;
	St.SteerFollow.Rate = K::SteerFollowRate;
}

void FChangeFakieControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x104030, 0x11D038
	if (ControlInputsMask() & 1)
	{
		SetSteerTarget(Sim, In.Turn);
		SetCrouchTarget(Sim, FMath::Max(In.Crouch, 0.0));
	}
	else
	{
		RideInputs(Sim, In);
	}
	// 0x325FA8, 0x1613A0
	const bool bSwitch = !ChangeFakieSkipsSwitch(Sim);
	if (bSwitch && !AnimControlDone(Sim, *Ctx, SSXAnim::SwitchFakie, 0))
	{
		return;
	}
	if (bSwitch && Sim.State.BoardClass == 2)
	{
		Sim.ApplyTransition(TEXT("T12"));
		return;
	}
	Sim.ApplyTransition(In.bJumpHeld ? TEXT("T14") : TEXT("T13"));
}

void FChangeFakieControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	Sim.State.Brake = FApproachFilter();   // 0x104178
}

void FFakieCruiseControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	Ctx->bTurnAround = false;
	TurnAroundTime = 0.0;
}

void FFakieCruiseControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x1043F8, 0x104688
	FRiderState& St = Sim.State;
	if (!(ControlInputsMask() & 2))
	{
		RideInputs(Sim, In);
	}
	if (Ctx->bTurnAround)
	{
		if (Turn180Anim != SSXAnim::UnSwitch)
		{
			St.Velocity *= 0.99;
		}
		TurnAroundTime += FRiderSim::Dt;
		const int32 Fallback = FMath::RoundToInt32(Ctx->Config.TurnAroundSeconds / FRiderSim::Dt);
		const int32 Need = Ctx->Anim->ControlFramesFor(Sim, Turn180Anim, Fallback);
		if (FMath::RoundToInt32(TurnAroundTime / FRiderSim::Dt) >= Need)
		{
			// 0x325FA8
			Ctx->bTurnAround = false;
			Sim.ApplyTransition(TEXT("T17"));
		}
		return;
	}
	double T = St.FinishResult >= 0 ? 1.0 : In.Turn;
	if (FMath::Abs(T) <= 0.5 && FVector::DotProduct(St.Velocity, St.BasisForward) >= 0.0)
	{
		return;
	}
	St.bStanceGoofy = !St.bStanceGoofy;
	if (Sim.Profile.Stance != 0)
	{
		T = -T;
	}
	if (T > 0.5 || T < -0.5)
	{
		Turn180Anim = T > 0.5 ? SSXAnim::TurnAroundHS : SSXAnim::TurnAroundTS;
	}
	else
	{
		Sim.RotateBody(St.BasisUp, UE_DOUBLE_PI);
		Turn180Anim = SSXAnim::UnSwitch;
	}
	Ctx->bTurnAround = true;
	TurnAroundTime = 0.0;
	Ctx->Anim->RequestPlay(Turn180Anim);
}

void FFakieCruiseControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	Ctx->bTurnAround = false;
	Sim.State.Brake = FApproachFilter();    // 0x104688
	Sim.State.Crouch = FApproachFilter();
}

// 0x1041B0, 0x128E90, 0x15FE70, 0x104190, 0x126948, 0x126B38, 0x104388
void FBumpControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	if (ControlInputsMask() & 8)
	{
		SetSteerTarget(Sim, In.Turn);
		CrouchBrake(Sim, 0.0, FMath::Max(In.Brake, 0.0));
	}
	else
	{
		RideInputs(Sim, In);
	}
	if (AnimControlDone(Sim, *Ctx, Ctx->Anim->BumpAnim, SSXAnim::ControlFrames(28)))
	{
		Sim.ApplyTransition(In.bJumpHeld ? TEXT("T15") : TEXT("T16"));
		return;
	}
	if (ControlInputsMask() & 64)
	{
		FRiderState& St = Sim.State;
		const int32 N = Ctx->Anim->Frames(Sim, Ctx->Anim->BumpAnim);
		const float P = N > 1 ? FMath::Min(0.5f * float(SSXAnim::FramesInControl(St)) / float(N - 1), 1.0f) : 1.0f;
		const float Y = St.BumpYaw * P;
		Sim.RotateBody(St.BasisUp, SSXEE::FpuSub(St.BumpYawDone, Y));
		St.BumpYawDone = Y;
	}
}

void FBumpControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	if (ControlInputsMask() & 8)
	{
		Sim.State.SteerFollow.Target = 0.0;   // 0x104190
		Sim.State.SteerFollow.Rate = K::SteerFollowRate;
	}
}

void FBumpControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	// 0x104388
	Sim.State.BumpYaw = 0.0f;
	Sim.State.BumpYawDone = 0.0f;
}

void FPrewindControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	FRiderState& St = Sim.State;
	for (FApproachFilter* F : { &St.Steer, &St.SteerFollow, &St.Brake })
	{
		F->Target = 0.0;
		F->Rate = K::PrewindReleaseRate;
	}
	// 0x105070: 0x15F050 (channel 1 fades over 10 animation frames) before the crouch clip 0x217 (0x15FCF8)
	if (Ctx->Anim.IsValid())
	{
		Ctx->Anim->FadeChannel1(Sim);
	}
	Ctx->bPrewindAbort = false;
}

void FPrewindControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	// 0x1050E8
	if (St.GrindType == 0 && (In.bJumpAbort || Ctx->bPrewindAbort || St.FinishResult >= 0))
	{
		Ctx->bPrewindAbort = true;
		for (FApproachFilter* F : { &St.PrewindA, &St.PrewindB })
		{
			F->Target = 0.0;
			F->Rate = K::PrewindChargeRate;
		}
		if (St.PrewindA.Cur == 0.0 && St.PrewindB.Cur == 0.0)
		{
			Sim.ApplyTransition(TEXT("T23"));
		}
		return;
	}
	if (In.bJumpHeld)
	{
		BoostHandler(Sim, In.bBoostHeld);   // 0x11D038
		SetCrouchTarget(Sim, 1.0);          // 0x126C00
		// 0x105A60, 0x126E30
		const float Base = float(St.GrindType != 0 ? K::PrewindChargeRateRail : K::PrewindChargeRate);
		const float StatMul = SSXEE::FpuAdd(float(Sim.Profile.Stat(0x18) * 255.0) * float(K::PrewindChargeStatK) * 0.003921569f, float(K::PrewindChargeStatBase));
		auto Charge = [Base, StatMul](FApproachFilter& F, double Target)
		{
			F.Target = float(Target);
			F.Rate = (Base * FMath::Abs(SSXEE::FpuSub(float(Target), float(F.Cur)))) * StatMul;
		};
		Charge(St.PrewindA, In.PrewindSpin);
		Charge(St.PrewindB, St.bStanceGoofy ? In.PrewindFlip : -In.PrewindFlip);
		const double Turn = FMath::Clamp(In.PrewindTurn, -K::PrewindTurnClamp, K::PrewindTurnClamp);
		if (MovingForward(St))
		{
			SetSteerTarget(Sim, Turn);
		}
		else if (Turn != 0.0)
		{
			Ctx->bPrewindAbort = true;
		}
		return;
	}
	St.LaunchCharge = St.Crouch.Cur;
	St.Crouch.Target = 0.0;
	St.Crouch.Rate = K::CrouchUnwindRate;
	auto Clear = [](FApproachFilter& F) { F.Cur = F.Rate = F.Target = 0.0; };
	if (FMath::Abs(St.PrewindA.Cur) < K::PrewindMinCharge) { Clear(St.PrewindA); }
	if (FMath::Abs(St.PrewindB.Cur) < K::PrewindMinCharge) { Clear(St.PrewindB); }
	const double Spin = St.PrewindA.Cur, Flip = St.PrewindB.Cur;
	if (Spin != 0.0 && Flip != 0.0)
	{
		const float Angle = FMath::Atan(float(FMath::Abs(Flip)) / float(FMath::Abs(Spin)));
		if (Angle > 1.3962256f) { Clear(St.PrewindA); }
		else if (Angle < 0.17457072f) { Clear(St.PrewindB); }
	}
	Sim.ApplyTransition(TEXT("T24"));
}

void FPrewindControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	BoostHandler(Sim, false);   // 0x1055A8
	Sim.State.Brake.Cur = Sim.State.Brake.Target = 0.0;
}

void FJumpControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	// 0x1046B0, 0x1055A8
	if (St.Crouch.Cur <= 0.0 && OnBoard(St))
	{
		Ctx->LastLaunchImpulse = ApplyLaunch(Sim, K::JumpMin, MakeGroundRider(Sim, *Ctx).StatJump);
		Sim.ApplyTransition(TEXT("T25"));
		Sim.ApplyTransition(TEXT("T26"));
		return;
	}
	SetSteerTarget(Sim, In.Turn);
}

void FLandNormalControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	const FRiderState& St = Sim.State;
	const double Vn = Previous == ERiderControl::Void ? St.LastImpact.Speed : FVector::DotProduct(St.Velocity, St.GroundNormal);
	Ctx->Absorb = LandingAbsorb(FMath::Min(Vn, 0.0));
	// 0x1049F8, 0x104E68
	if (Ctx->Absorb > 0.25 && Ctx->Anim)
	{
		Ctx->Anim->PlayOverlay(Sim, SSXAnim::LandArmsId(St.Velocity, St.BasisUp, St.BasisForward, St.BasisSide, St.bStanceGoofy));
	}
}

void FLandNormalControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	// 0x104E68
	SetSteerTarget(Sim, MovingForward(St) ? In.Turn : -In.Turn);
	St.Crouch.Target = FMath::Max(Ctx->Absorb, FMath::Max(In.Crouch, 0.0));
	St.Crouch.Rate = K::AbsorbRate;
	if (St.Crouch.Cur >= Ctx->Absorb)
	{
		Sim.ApplyTransition(In.bJumpHeld ? TEXT("T22") : TEXT("T21"));
	}
}

void FLandHardControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x104800, 0x325FA8
	if (AnimControlDone(Sim, *Ctx, Ctx->Anim->LandHardAnim, 31))
	{
		if (Ctx->Anim->LandHardAnim == 0x240)
		{
			Sim.ApplyTransition(TEXT("T20"));
		}
		else
		{
			Sim.ApplyTransition(In.bJumpHeld ? TEXT("T19") : TEXT("T18"));
		}
	}
	if (ControlInputsMask() & 16)
	{
		FRiderState& St = Sim.State;
		const bool bBack = SSXEE::Dot(SSXEE::F(St.Velocity), SSXEE::F(St.BasisForward)) < 0.0f;
		SetSteerTarget(Sim, bBack ? -In.Turn : In.Turn);
		CrouchBrake(Sim, 0.0, FMath::Max(In.Brake, 0.0));
	}
}

void FLandHardControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	FRiderState& St = Sim.State;
	St.Highlight(19);   // 0x104790
	for (FApproachFilter* F : { &St.Crouch, &St.Brake, &St.SteerFollow })
	{
		F->Cur = F->Target = 0.0;
	}
}

// 0x106DC8, 0x1073E0, 0x1075F0, 0x107730, 0x107628
namespace
{
	constexpr double HalfPi = UE_DOUBLE_HALF_PI;
	constexpr double RailPoseRampRate = 3.3333333;

	const FRailSwitchRow SwitchRows[2][5] = {
		{ { 0, 0, 0.0, 0.0 }, { 0x25A, 4, HalfPi, -HalfPi }, { 0x259, 3, -HalfPi, HalfPi }, { 0x25C, 1, 0.0, -UE_DOUBLE_PI }, { 0x25F, 2, -UE_DOUBLE_PI, 0.0 } },
		{ { 0, 0, 0.0, 0.0 }, { 0x258, 3, -HalfPi, HalfPi }, { 0x25B, 4, HalfPi, -HalfPi }, { 0x25D, 2, -UE_DOUBLE_PI, 0.0 }, { 0x25E, 1, 0.0, -UE_DOUBLE_PI } },
	};
	int32 PendingOf(int32 GrindType) { return GrindType == 4 ? 1 : GrindType == 3 ? 2 : GrindType == 1 ? 3 : GrindType == 2 ? 4 : 0; }
	int32 GrindOfPending(int32 Pending) { return Pending == 1 ? 4 : Pending == 2 ? 3 : Pending == 3 ? 1 : Pending == 4 ? 2 : 0; }

	bool AlpineGoofy(const FRiderSim& Sim) { return Sim.State.BoardClass == 2 && Sim.Profile.Stance != 0; }

	void AddLayerYaw(FGroundContext& X, double A)
	{
		if (X.Anim.IsValid())
		{
			X.Anim->LayerYawAdd += A;   // 0x15EEF8
		}
	}

	void RequestClip(FGroundContext& X, int32 Clip)
	{
		if (X.Anim.IsValid())
		{
			X.Anim->RequestPlay(Clip);   // 0x15FCF8
		}
	}

	// 0x107FC8, 0x128E90
	void StanceFlip(FRiderSim& Sim, FGroundContext& X, bool bLayerPi)
	{
		FRiderState& St = Sim.State;
		St.bStanceGoofy = !St.bStanceGoofy;
		Sim.RotateBodyEE(SSXEE::F(St.BasisUp), 3.1415927f);
		St.Steer.Cur = -St.Steer.Cur;
		St.Steer.Target = -St.Steer.Target;
		St.Lean.Cur = -St.Lean.Cur;
		St.AirTurnSmoothed = -St.AirTurnSmoothed;
		St.AirTiltSmoothed = -St.AirTiltSmoothed;
		St.bAnimMirror = St.bStanceGoofy;
		if (bLayerPi)
		{
			AddLayerYaw(X, UE_DOUBLE_PI);
		}
	}

	// 0x126C00
	void RailCrouchTarget(FRiderState& St, double Value)
	{
		St.Crouch.Target = Value;
		const float D = FMath::Abs(SSXEE::FpuSub(float(Value), float(St.Crouch.Cur)));
		St.Crouch.Rate = (D < 0.1f ? 0.1f : D) * float(K::CrouchRateK);
	}
}

bool RailSwitchRow(int32 Dir, int32 GrindType, FRailSwitchRow& Out)
{
	if (Dir < 0 || Dir > 1 || GrindType < 1 || GrindType > 4)
	{
		return false;
	}
	Out = SwitchRows[Dir][GrindType];
	return true;
}

int32 RailCycleClip(int32 GrindType)
{
	return GrindType == 4 ? 0x257 : GrindType == 3 ? 0x256 : GrindType == 2 ? 0x255 : 0x254;
}

int32 RailSwitchFrames(const FRiderSim& Sim, const SSXAnim::FRiderAnimChannel* Anim, int32 Clip)
{
	int32 N = Anim ? Anim->Frames(Sim, Clip) : 0;
	if (N <= 0)
	{
		N = SSXAnim::DefaultOneShotFrames(Clip, Sim.State.BoardClass);
	}
	// 0x15FCF8
	return SSXAnim::OneShotFrames(FMath::Max(N, 1));
}

void FRailSlideControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	FRiderState& St = Sim.State;
	FGroundContext& X = *Ctx;
	X.RailPending = 0;
	X.RailSpinDeg = 0;
	X.bRailLatch = false;
	X.RailSwitchClip = 0;
	X.RailSwitchFrame = 0;
	if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		Air->Score.RailSpin(0);   // 0x155E00
	}
	SetBrakeTarget(Sim, 0.0);     // 0x126C78
	St.Ramp1F0.Rate = RailPoseRampRate;
	St.Ramp1F0.Target = 0.0;
	// 0x15F050, 0x15A600, 0x159D88
	if (X.Anim.IsValid())
	{
		X.Anim->FadeChannel1(Sim);
	}
	St.AnimYaw = St.bStanceGoofy ? -UE_DOUBLE_PI : 0.0;
	St.bAnimMirror = St.bStanceGoofy;
	St.bRailAnimPose = true;

	const bool bAG = AlpineGoofy(Sim);
	if (bAG && (St.GrindType == 1 || St.GrindType == 2))
	{
		St.GrindType = 3 - St.GrindType;
	}
	const int32 Gt = St.GrindType;
	const bool bStance = St.bStanceGoofy;
	if (Gt == 3 || Gt == 4)
	{
		const double Sgn = Gt == 4 ? 1.0 : -1.0;
		double Body;
		if (St.BoardClass == 2)
		{
			if (bStance != bAG)
			{
				StanceFlip(Sim, X, true);
			}
			Body = bAG ? -Sgn * HalfPi : Sgn * HalfPi;
		}
		else
		{
			if (bStance)
			{
				StanceFlip(Sim, X, false);
			}
			Body = Sgn * HalfPi;
		}
		Sim.RotateBodyEE(SSXEE::F(St.BasisUp), float(Body));   // 0x128E90
		AddLayerYaw(X, Sgn * HalfPi);
		St.AnimYaw = Body;
	}
	else if (Gt == 2)
	{
		if (bAG)
		{
			if (bStance)
			{
				StanceFlip(Sim, X, false);
				St.bAnimMirror = true;
				AddLayerYaw(X, UE_DOUBLE_PI);
				St.AnimYaw = 0.0;
			}
		}
		else if (!bStance)
		{
			StanceFlip(Sim, X, false);
			if (St.BoardClass == 2)
			{
				St.bAnimMirror = false;
			}
			AddLayerYaw(X, UE_DOUBLE_PI);
			St.AnimYaw = -UE_DOUBLE_PI;
		}
	}
	else
	{
		if (bAG)
		{
			if (!bStance)
			{
				StanceFlip(Sim, X, true);
				St.AnimYaw = -UE_DOUBLE_PI;
			}
		}
		else if (bStance)
		{
			StanceFlip(Sim, X, true);
			St.AnimYaw = 0.0;
		}
	}
	RequestClip(X, RailCycleClip(Gt));
}

void FRailSlideControl::StartSwitch(FRiderSim& Sim, bool bAxisPositive)
{
	// 0x107730
	FRiderState& St = Sim.State;
	FGroundContext& X = *Ctx;
	const bool bAG = AlpineGoofy(Sim);
	const int32 Dir = (bAxisPositive ? 1 : 0) ^ (bAG ? 1 : 0);
	AddLayerYaw(X, Dir == 0 ? HalfPi : -HalfPi);
	X.RailSpinDeg += Dir == 0 ? 90 : -90;
	if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		Air->Score.RailSpin(X.RailSpinDeg);   // 0x155E00
	}
	FRailSwitchRow Row;
	if (!RailSwitchRow(Dir, St.GrindType, Row))
	{
		return;   // grind type outside 1..4 (never on a rail): no clip, nothing pending
	}
	if (St.GrindType == 2)
	{
		if (bAG)
		{
			St.bStanceGoofy = true;
		}
		else
		{
			St.bStanceGoofy = false;
			St.bAnimMirror = false;
			if (X.Anim.IsValid())
			{
				X.Anim->bClearLayerMirrors = true;   // 0x15EF48
			}
		}
	}
	St.AnimYaw = bAG ? Row.YawAlpineGoofy : Row.Yaw;
	RequestClip(X, Row.Clip);
	X.RailSwitchClip = Row.Clip;
	X.RailSwitchFrame = 0;
	X.RailPending = PendingOf(Row.Next);
	++X.RailSwitchCount;
}

void FRailSlideControl::CompleteSwitch(FRiderSim& Sim)
{
	// 0x107628
	FRiderState& St = Sim.State;
	FGroundContext& X = *Ctx;
	if (X.RailPending == 0)
	{
		return;
	}
	const int32 Next = GrindOfPending(X.RailPending);
	if (Next == 2)
	{
		if (St.BoardClass == 2)
		{
			St.bStanceGoofy = Sim.Profile.Stance == 0;
		}
		else
		{
			St.bAnimMirror = true;
			St.bStanceGoofy = true;
		}
	}
	St.GrindType = Next;
	RequestClip(X, RailCycleClip(Next));
	X.RailPending = 0;
	X.RailSwitchClip = 0;
}

void FRailSlideControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	FGroundContext& X = *Ctx;
	BoostHandler(Sim, In.bBoostHeld);
	X.bRailLatch = X.bRailLatch ? (In.bJumpPressed || In.bJumpHeld) : In.bJumpPressed;
	if (X.RailPending == 0)
	{
		if (!In.bRailSpinMode && In.RailSpin == 0.0)
		{
			// 0x126AC8
			St.Steer.Target = In.Turn;
			St.Steer.Rate = FMath::Clamp(FMath::Abs(SSXEE::FpuSub(float(In.Turn), float(St.Steer.Cur))) * float(K::SteerRateK), float(K::SteerRateMin), float(K::SteerRateMax));
			RailCrouchTarget(St, In.Crouch);
			if (X.bRailLatch)
			{
				Sim.ApplyTransition(TEXT("T30"));
			}
		}
		else
		{
			const double D = In.RailSpin != 0.0 ? In.RailSpin : In.Turn;
			RailCrouchTarget(St, 0.0);
			if (St.Crouch.Cur == 0.0 && D != 0.0)
			{
				StartSwitch(Sim, D > 0.0);
			}
		}
	}
	else
	{
		++X.RailSwitchFrame;
		if (X.RailSwitchFrame >= RailSwitchFrames(Sim, X.Anim.Get(), X.RailSwitchClip))
		{
			CompleteSwitch(Sim);
		}
	}
}

void FRailSlideControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	// 0x1075F0, 0x11D038
	BoostHandler(Sim, false);
	CompleteSwitch(Sim);
}

void FGateAnticipateControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	// 0x105F48
	FGroundContext& X = *Ctx;
	X.GateStill = X.GateMax = X.GateMin = 0.0;
}

void FGateAnticipateControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x105F58
	FRiderState& St = Sim.State;
	FGroundContext& X = *Ctx;
	const double Prev = St.GateLaunch.Cur;
	const float M1 = float(GateMarkerPhase(Sim, X, 1));   // 0x15FDE0
	const float Target = In.GateRock > 0.0 ? M1 : (In.GateRock < 0.0 ? 0.0f : M1 * 0.5f);
	St.GateLaunch.Target = Target;
	{
		const float D = FMath::Abs(SSXEE::FpuSub(float(Target), float(St.GateLaunch.Cur)));
		const float GK = GateGain(Sim, X) * float(K::GateRateK);
		St.GateLaunch.Rate = GK * (D < float(K::GateRateMin) ? float(K::GateRateMin) : D);
	}
	StepGate(St.GateLaunch);
	St.SuspensionOffset = GateSuspension(Sim);   // 0x11E9D8
	const double Cur = St.GateLaunch.Cur;
	if (Cur == Prev)
	{
		X.GateStill = SSXEE::FpuAdd(float(X.GateStill), GateDt);
	}
	else
	{
		X.GateStill = 0.0;
		(Cur > Prev ? X.GateMax : X.GateMin) = Cur;
	}
}

void FGateAnticipateControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	// 0x106140
	FRiderState& St = Sim.State;
	const FGroundContext& X = *Ctx;
	double Push = K::GatePushMin;
	if (St.GateLaunch.Target < 0.5 && X.GateStill < K::GateStillMax)
	{
		const float P = SSXEE::FpuSub(float(X.GateMax), float(X.GateMin)) * float(K::GatePushK) * SSXEE::FpuSub(1.0f, float(St.GateLaunch.Cur)) / SSXEE::FpuAdd(float(X.GateStill), 1.0f);
		Push = P < float(K::GatePushMin) ? K::GatePushMin : double(P);
	}
	St.LaunchCharge = Push;
}

void FGateLaunchControl::Enter(FRiderSim& Sim, ERiderControl Previous)
{
	// 0x106228
	FRiderState& St = Sim.State;
	St.GateLaunch.Target = 1.0;
	St.GateLaunch.Rate = GateGain(Sim, *Ctx) * float(K::GateRateK) * float(K::GateRateMin);
	Ctx->bGatePushed = false;
}

void FGateLaunchControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x1062B0
	FRiderState& St = Sim.State;
	StepGate(St.GateLaunch);
	if (St.Velocity.Size() == 0.0 && GateMarkerPhase(Sim, *Ctx, 2) <= St.GateLaunch.Cur)
	{
		const float GV = GateGain(Sim, *Ctx) * float(St.LaunchCharge);
		St.Velocity = FVector(GV * float(St.BasisForward.X), GV * float(St.BasisForward.Y), GV * float(St.BasisForward.Z));
		Ctx->bGatePushed = true;
		Sim.ApplyTransition(TEXT("T42"));
	}
	if (St.GateLaunch.Cur == 1.0)
	{
		Sim.ApplyTransition(TEXT("T43"));
	}
	St.SuspensionOffset = GateSuspension(Sim);   // 0x11E9D8
}
void FGateToBaseControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	BoostHandler(Sim, In.bBoostHeld, In.bBoostPressed);
	// 0x105DC0, 0x126948, 0x127918
	SetSteerTarget(Sim, In.Turn);
	if (AnimControlDone(Sim, *Ctx, SSXAnim::GateToBase, 41) && !In.bResetRequest && St.SurfaceId != FRiderSim::OutOfBoundsSurface)
	{
		Sim.ApplyTransition(TEXT("T44"));
	}
}
}
