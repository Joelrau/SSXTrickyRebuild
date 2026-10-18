#include "World/PropControllers.h"

#include "Race/GameRng.h"
#include "Sim/EEFloat.h"

#include "Sim/Ground/PropCollision.h"
#include "Sim/RiderSim.h"

namespace SSXProps
{
namespace
{
	int32 I32(const uint8* P) { int32 V; FMemory::Memcpy(&V, P, 4); return V; }
	float F32(const uint8* P) { float V; FMemory::Memcpy(&V, P, 4); return V; }

}

void FUVScroll::Tick()
{
	// 0x1422E0 / 0x141FF0 / 0x13A980
	if (DelayFrames < 0)
	{
		DelayFrames = int32(float(Delay) * 60.f);
	}
	if (DelayFrames > 0 && --DelayFrames == 0)
	{
		++DoneCalls;
		return;
	}
	const float A = float(RunTime), B = float(PauseTime);
	if (A <= 0.f && B <= 0.f)
	{
		return;
	}
	Clock = SSXEE::FpuAdd(Clock, 0.016666668f);
	if (!bRunning)
	{
		if (Clock < B)
		{
			return;
		}
		Clock = 0.f;
		if (A > 0.f)
		{
			bRunning = true;
		}
		return;
	}
	if (A <= Clock)
	{
		Clock = 0.f;
		if (B > 0.f)
		{
			bRunning = false;
		}
		if (Mode == 1 || Mode == 2)
		{
			Step = -Step;
		}
	}
	if (!bRunning)
	{
		return;
	}
	float U = float(Offset.X), V = float(Offset.Y);
	const float Du = float(Step.X), Dv = float(Step.Y);
	if (Mode == 1)
	{
		float T;
		if (Du < 0.f)
		{
			T = Clock < A * 0.5f ? Clock : SSXEE::FpuSub(A, Clock);
		}
		else
		{
			const float R = SSXEE::FpuSub(A, Clock);
			T = R < A * 0.5f ? R : SSXEE::FpuSub(A, R);
		}
		U = SSXEE::FpuAdd(U, Du * T * (1.f / A));
		V = SSXEE::FpuAdd(V, Dv * T * (1.f / A));
	}
	else
	{
		U = SSXEE::FpuAdd(U, Du);
		V = SSXEE::FpuAdd(V, Dv);
	}
	if (1.f < U) U = SSXEE::FpuSub(U, 1.f); else if (U < -1.f) U = SSXEE::FpuAdd(U, 1.f);
	if (1.f < V) V = SSXEE::FpuSub(V, 1.f); else if (V < -1.f) V = SSXEE::FpuAdd(V, 1.f);
	Offset = FVector2D(U, V);
}

double FTexFlip::Random() const
{
	return Rng ? Rng() : double(FGameRng::StreamA().Uniform(0.f, 1.f));
}

void FTexFlip::Create(const TArray<int32>& InTextures)
{
	Textures = InTextures;
	FrameCount = Textures.Num();
	DelayFrames = FMath::TruncToInt32(Delay * 60.0);
	Rate = BaseRate = Fps / 60.0;
	Phase = 0.0;
	DoneCalls = 0;
	int32 F = StartFrame;
	if (F < 0)
	{
		F = FMath::CeilToInt32(8.0 * Random());
	}
	bRandomSteps = StartFrame == -2;
	SetFrame(F);
}

void FTexFlip::SetFrame(int32 F)
{
	const int32 N = FrameCount;
	if (F < 0)
	{
		F = N > 2 ? FMath::CeilToInt32((N - 1.0) * Random() + 1.0) : 1;
	}
	Frame = FMath::Min(F, N - 1);
}

void FTexFlip::Tick()
{
	// 0x1430B8
	if (FrameCount <= 0 || !bRun)
	{
		return;
	}
	if (DelayFrames > 0)
	{
		if (--DelayFrames == 0)
		{
			++DoneCalls;
			return;
		}
	}
	if (Rate <= 0.0)
	{
		return;
	}
	Phase += Rate;
	if (Phase < 1.0)
	{
		return;
	}
	Phase -= 1.0;
	const int32 N = FrameCount;
	int32 F = Frame;
	auto Fwd = [N](int32 X) { return X + 1 < N ? X + 1 : 0; };
	if (Mode == 0)
	{
		if (bRandomSteps && N > 1) { F = FMath::CeilToInt32((N - 1.0) * Random()); }
		else if (Direction == 0) { F = Fwd(F); }
		else { F = F - 1 >= 0 ? F - 1 : N - 1; }
	}
	else if (Mode == 1)
	{
		Mode = 2;
		F = Fwd(F);
		Rate = TexFlipFlickerRate;
	}
	else if (Mode == 2)
	{
		Mode = 1;
		F = Fwd(F);
		Rate = BaseRate * (0.25 + 0.75 * Random());
	}
	SetFrame(F);
}

bool ParseFlipbooks(const uint8* Section, int32 Size, int32 Count, TArray<TArray<int32>>& Out)
{
	Out.Reset();
	int32 O = 0;
	for (int32 K = 0; K < Count; ++K)
	{
		if (O + 4 > Size) { return false; }
		const int32 N = I32(Section + O);
		if (N < 1 || O + 4 + 4 * N > Size) { return false; }
		TArray<int32>& B = Out.AddDefaulted_GetRef();
		for (int32 I = 0; I < N; ++I) { B.Add(I32(Section + O + 4 + 4 * I) & 0xFFFF); }
		O += 4 + 4 * N;
	}
	return true;
}

bool ParsePbdFlipbooks(const TArray<uint8>& Pbd, TArray<TArray<int32>>& Out)
{
	Out.Reset();
	if (Pbd.Num() < 0x40 + 18 * 4)
	{
		return false;
	}
	const int32 Count = I32(Pbd.GetData() + 4 + 9 * 4);
	const int32 Off = I32(Pbd.GetData() + 0x40 + 9 * 4);
	if (Count <= 0)
	{
		return Count == 0;
	}
	return Off > 0 && Off < Pbd.Num() && ParseFlipbooks(Pbd.GetData() + Off, Pbd.Num() - Off, Count, Out);
}

void FPropController::Tick()
{
	if (TouchTimer > 0) { --TouchTimer; }
	if (HasScroll()) { Scroll.Tick(); }
	if (HasFlip()) { Flip.Tick(); }
	for (int32& T : FenceTimers) { T = FMath::Max(T - 1, 0); }
	if (Type == EControllerType::Cracked)
	{
		if (Cooldown > 0) { --Cooldown; }
		if (Timer > 0 && --Timer == 0) { bDone = true; }
		if (Health <= 0.0) { bBroken = true; }
	}
}

bool FPropController::OnContact(const FVector& N, const FVector& VHat, double A)
{
	switch (Type)
	{
	case EControllerType::UVScroll:
	case EControllerType::TexFlip:
	case EControllerType::UVScrollTexFlip:
		if (TouchTimer < 1)
		{
			TouchTimer = TouchTimerFrames;
			return true;
		}
		return false;
	case EControllerType::Fence:
		if (Gate38 < 1.0)
		{
			Wobble = FVector::DotProduct(N, VHat) * A * FenceWobbleK;
			FenceTimers[0] = FenceTimers[1] = FenceTimerFrames;
		}
		return false;
	case EControllerType::Cracked:
		if (Cooldown == 0 && Health > 0.0)
		{
			Cooldown = CrackedCooldownFrames;
			Health -= FMath::Abs(FVector::DotProduct(VHat, N) * A) * CrackedDamageK;
		}
		return false;
	}
	return false;
}

bool ParseUVScrollRecord(const uint8* R, int32 Size, FUVScroll& Out)
{
	if (!R || Size < 36 || I32(R) != 0 || I32(R + 4) != 36 || I32(R + 8) != int32(EControllerType::UVScroll))
	{
		return false;
	}
	Out = FUVScroll();
	Out.Mode = I32(R + 0x0C);
	Out.Step = FVector2D(F32(R + 0x10), F32(R + 0x14));
	Out.RunTime = F32(R + 0x18);
	Out.PauseTime = F32(R + 0x1C);
	Out.Delay = F32(R + 0x20);
	return true;
}

bool ParseTexFlipRecord(const uint8* R, int32 Size, FTexFlip& Out)
{
	if (!R || Size < 32 || I32(R) != 0 || I32(R + 4) != 32 || I32(R + 8) != int32(EControllerType::TexFlip))
	{
		return false;
	}
	Out = FTexFlip();
	Out.StartFrame = I32(R + 0x0C);
	Out.Direction = I32(R + 0x10);
	Out.Fps = F32(R + 0x14);
	Out.Delay = F32(R + 0x18);
	Out.Mode = I32(R + 0x1C);
	Out.Frame = 0;
	return true;
}

bool ParseCrackedRecord(const uint8* R, int32 Size, FPropController& Out)
{
	if (!R || Size < 20 || I32(R) != 0 || I32(R + 4) != 20 || I32(R + 8) != int32(EControllerType::Cracked))
	{
		return false;
	}
	Out.Type = EControllerType::Cracked;
	Out.Timer = int32(F32(R + 0x0C) * 60.f);   // 0x145458
	Out.Health = F32(R + 0x10);
	Out.Cooldown = CrackedCooldownFrames;
	return true;
}

int32 FPropControllers::IndexOf(int32 Instance) const
{
	const int32* I = ByInstance.Find(Instance);
	return I ? *I : INDEX_NONE;
}

int32 FPropControllers::Add(const FPropController& C)
{
	if (C.Instance != INDEX_NONE && ByInstance.Contains(C.Instance))
	{
		Remove(C.Instance);
	}
	const int32 I = Nodes.Add(C);
	if (C.Instance != INDEX_NONE)
	{
		ByInstance.Add(C.Instance, I);
	}
	return I;
}

const FPropController* FPropControllers::Find(int32 Instance) const
{
	const int32* I = ByInstance.Find(Instance);
	return I ? &Nodes[*I] : nullptr;
}

bool FPropControllers::Activate(int32 Instance)
{
	const int32* I = ByInstance.Find(Instance);
	if (!I)
	{
		return false;
	}
	FPropController& C = Nodes[*I];
	C.Scroll.bActive |= C.HasScroll();
	C.Flip.bActive |= C.HasFlip();
	return C.HasScroll() || C.HasFlip();
}

void FPropControllers::Tick()
{
	for (FPropController& C : Nodes)
	{
		const bool bWasDone = C.bDone, bWasBroken = C.bBroken;
		C.Tick();
		if (C.bDone && !bWasDone && RunEffect) { RunEffect(C.Instance, EventSlot::ControllerDone, nullptr); }
		if (C.bBroken && !bWasBroken && RunEffect) { RunEffect(C.Instance, EventSlot::CounterOrHealthOut, nullptr); }
	}
}

bool FPropControllers::Remove(int32 Instance)
{
	const int32* I = ByInstance.Find(Instance);
	if (!I)
	{
		return false;
	}
	Nodes.RemoveAt(*I);
	ByInstance.Reset();
	for (int32 K = 0; K < Nodes.Num(); ++K)
	{
		if (Nodes[K].Instance != INDEX_NONE) { ByInstance.Add(Nodes[K].Instance, K); }
	}
	return true;
}

void FPropControllers::Message(int32 Instance, int32 Msg, double V)
{
	const int32* I = ByInstance.Find(Instance);
	if (!I || !Nodes[*I].HasFlip())
	{
		return;
	}
	FTexFlip& F = Nodes[*I].Flip;
	switch (Msg)
	{
	case 1: F.Fps = V; F.Rate = F.BaseRate = V / 60.0; break;   // TODO: units of v (read as fps)
	case 2:
		if (V >= 0.0 && V < 9.0) { F.SetFrame(FMath::CeilToInt32(V)); }
		break;
	case 3:
		F.SetFrame(F.FrameCount > 0 && F.Frame + 1 >= F.FrameCount ? 1 : F.Frame + 1);
		break;
	case 4: F.bRun = V != 0.0; break;
	default: break;
	}
}

void FPropControllers::OnFenceContact(const SSXGround::FPropContactEvent& Event)
{
	const int32* I = ByInstance.Find(Event.Hit.InstanceIndex);
	if (I && Nodes[*I].Type == EControllerType::Fence)
	{
		Nodes[*I].OnContact(Event.Contact.Normal, Event.Contact.Direction, Event.Contact.Speed);
	}
}

EContactResult FPropControllers::OnContact(const SSXGround::FPropContactEvent& Event, FRiderSim* Sim)
{
	if (EffectTouch && Sim)
	{
		EffectTouch(*Sim, Event);
		return EContactResult::Handled;
	}
	const int32 Instance = Event.Hit.InstanceIndex;
	const int32* I = ByInstance.Find(Instance);
	EContactResult R = EContactResult::NoController;
	if (I)
	{
		R = Nodes[*I].OnContact(Event.Contact.Normal, Event.Contact.Direction, Event.Contact.Speed)
			? EContactResult::RunSlot1 : EContactResult::Handled;
	}
	if (R != EContactResult::Handled && RunEffect)
	{
		RunEffect(Instance, EventSlot::Touch, &Event);
	}
	return R;
}

void FPropControllers::Bind(FRiderSim& Sim)
{
	TFunction<void(FRiderSim&, const SSXGround::FPropContactEvent&)> Prev = MoveTemp(Sim.OnPropContact);
	Sim.OnPropContact = [this, Prev = MoveTemp(Prev)](FRiderSim& S, const SSXGround::FPropContactEvent& E)
	{
		OnContact(E, &S);
		if (Prev)
		{
			Prev(S, E);
		}
	};
}
}
