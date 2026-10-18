#include "Input/RiderInputDriver.h"

namespace
{
	using FShared = FRiderInputDriver::FShared;

	class FMotionObserver final : public IRiderMotionState
	{
	public:
		FMotionObserver(IRiderMotionState* InInner, const TSharedRef<FShared>& InShared)
			: Inner(InInner), Shared(InShared) {}
		virtual void Enter(FRiderSim& Sim, ERiderMotion Previous) override { Inner->Enter(Sim, Previous); }
		virtual void Exit(FRiderSim& Sim, ERiderMotion Next) override { Inner->Exit(Sim, Next); }
		virtual FString DebugString() const override { return Inner->DebugString(); }
		virtual TSharedPtr<FRiderStateBlob> SaveState() const override { return Inner->SaveState(); }
		virtual void LoadState(const FRiderStateBlob& B) override { Inner->LoadState(B); }
		virtual void Update(FRiderSim& Sim) override
		{
			Inner->Update(Sim);
			Shared->WordControl = Sim.State.Control;
			if (Shared->PlayInput)
			{
				*Shared->PlayInput = FPadMapper::DecodeWord(Sim.State.Control, Shared->PlayWord);
			}
		}
	private:
		IRiderMotionState* Inner;
		TSharedRef<FShared> Shared;
	};

	class FExactLayoutControl final : public IRiderControlState
	{
	public:
		FExactLayoutControl(IRiderControlState* InInner, const TSharedRef<FShared>& InShared)
			: Inner(InInner), Shared(InShared) {}
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override { Inner->Enter(Sim, Previous); }
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override { Inner->Exit(Sim, Next); }
		virtual TSharedPtr<FRiderStateBlob> SaveState() const override { return Inner->SaveState(); }
		virtual void LoadState(const FRiderStateBlob& B) override { Inner->LoadState(B); }
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override
		{
			if (const FPadActions* A = Shared->Current)
			{
				const ERiderControl Ctl = Sim.State.Control;
				Inner->Update(Sim, FPadMapper::DecodeWord(Ctl, FPadMapper::EncodeWord(Ctl, *A)));
			}
			else if (Shared->PlayInput)
			{
				Inner->Update(Sim, FPadMapper::DecodeWord(Sim.State.Control, Shared->PlayWord));
			}
			else
			{
				Inner->Update(Sim, Input);
			}
		}
	private:
		IRiderControlState* Inner;
		TSharedRef<FShared> Shared;
	};
}

FRiderInputDriver::FRiderInputDriver()
	: Shared(MakeShared<FShared>())
{
}

void FRiderInputDriver::Install(FRiderSim& Sim, TFunctionRef<void(FRiderSim&)> Register)
{
	Shared->Registry = MakeShared<FRiderSim>();
	Register(*Shared->Registry);
	FRiderSim& R = *Shared->Registry;
	Sim.SetRegistrationSim(&R);
	for (int32 I = 1; I < int32(ERiderMotion::Count); ++I)
	{
		const ERiderMotion Id = ERiderMotion(I);
		IRiderMotionState* Inner = R.GetMotionState(Id);
		Sim.SetMotionState(Id, Inner ? TSharedPtr<IRiderMotionState>(MakeShared<FMotionObserver>(Inner, Shared)) : nullptr);
	}
	for (int32 I = 1; I < int32(ERiderControl::Count); ++I)
	{
		const ERiderControl Id = ERiderControl(I);
		IRiderControlState* Inner = R.GetControlState(Id);
		Sim.SetControlState(Id, Inner ? TSharedPtr<IRiderControlState>(MakeShared<FExactLayoutControl>(Inner, Shared)) : nullptr);
	}
}

void FRiderInputDriver::Step(FRiderSim& Sim, const FPadActions& Actions)
{
	Shared->Current = &Actions;
	Shared->WordControl = Sim.State.Control;
	Sim.DebugHeldActions = Actions.Held;
	Sim.Step(FPadMapper::FullWord(Actions));
	LastWord = FPadMapper::EncodeWord(Shared->WordControl, Actions);
	Shared->Current = nullptr;
}

void FRiderInputDriver::StepWord(FRiderSim& Sim, uint32 Word)
{
	FRiderInputWord In = FPadMapper::DecodeWord(Sim.State.Control, Word);
	Sim.DebugHeldActions = 0;
	Shared->Current = nullptr;
	Shared->PlayWord = Word;
	Shared->PlayInput = &In;
	Shared->WordControl = Sim.State.Control;
	Sim.Step(In);
	LastWord = Word;
	Shared->PlayInput = nullptr;
}
