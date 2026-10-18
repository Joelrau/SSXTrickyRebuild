#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderSim.h"
#include "Sim/Ground/GroundStates.h"

namespace SSXGround
{
	void SetSteerTarget(FRiderSim& Sim, double Turn);
	// 0x104030, 0x126948, 0x126C00, 0x1043F8, 0x102FC8, 0x104190, 0x1041B0, 0x126B38, 0x104800, 0x1000E0
	SSXTRICKYREBUILD_API int32 ControlInputsMask();
	void SetCrouchTarget(FRiderSim& Sim, double Value);
	void SetBrakeTarget(FRiderSim& Sim, double Value);
	// 0x11D038
	SSXTRICKYREBUILD_API void BoostHandler(FRiderSim& Sim, bool bHeld, bool bPressed = false);

	class SSXTRICKYREBUILD_API FGroundControlBase : public IRiderControlState
	{
	public:
		explicit FGroundControlBase(TSharedRef<FGroundContext> InCtx) : Ctx(InCtx) {}
	protected:
		void RideInputs(FRiderSim& Sim, const FRiderInputWord& In);
		static void CrouchBrake(FRiderSim& Sim, double CrouchIn, double BrakeIn);
		TSharedRef<FGroundContext> Ctx;
	};

	class SSXTRICKYREBUILD_API FCruiseControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
	};

	class SSXTRICKYREBUILD_API FChangeFakieControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
	};

	// 0x103660, 0x1033D0, 0x103B88
	SSXTRICKYREBUILD_API void CruiseOverlays(FRiderSim& Sim);

	SSXTRICKYREBUILD_API bool ChangeFakieSkipsSwitch(const FRiderSim& Sim);

	class SSXTRICKYREBUILD_API FFakieCruiseControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
		virtual TSharedPtr<FRiderStateBlob> SaveState() const override { return SSXRiderBlob::Save(TTuple<double, int32>(TurnAroundTime, Turn180Anim)); }
		virtual void LoadState(const FRiderStateBlob& B) override
		{
			TTuple<double, int32> T;
			SSXRiderBlob::Load(B, T);
			TurnAroundTime = T.Get<0>();
			Turn180Anim = T.Get<1>();
		}
	private:
		double TurnAroundTime = 0.0;
		int32 Turn180Anim = SSXAnim::TurnAroundHS;
	};

	class SSXTRICKYREBUILD_API FBumpControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
	};

	class SSXTRICKYREBUILD_API FPrewindControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
	};

	class SSXTRICKYREBUILD_API FJumpControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
	};

	class SSXTRICKYREBUILD_API FLandNormalControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		// 0x104E38
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override { Sim.State.Highlight(19); }
	};

	class SSXTRICKYREBUILD_API FLandHardControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
	};

	class SSXTRICKYREBUILD_API FRailSlideControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
		// 0x107730
		void StartSwitch(FRiderSim& Sim, bool bAxisPositive);
		// 0x107628
		void CompleteSwitch(FRiderSim& Sim);
	};

	struct FRailSwitchRow { int32 Clip; int32 Next; double Yaw; double YawAlpineGoofy; };
	SSXTRICKYREBUILD_API bool RailSwitchRow(int32 Dir, int32 GrindType, FRailSwitchRow& Out);
	SSXTRICKYREBUILD_API int32 RailCycleClip(int32 GrindType);
	SSXTRICKYREBUILD_API int32 RailSwitchFrames(const FRiderSim& Sim, const SSXAnim::FRiderAnimChannel* Anim, int32 Clip);

	class SSXTRICKYREBUILD_API FGateAnticipateControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
	};

	class SSXTRICKYREBUILD_API FGateLaunchControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
	};

	class SSXTRICKYREBUILD_API FGateToBaseControl : public FGroundControlBase
	{
	public:
		using FGroundControlBase::FGroundControlBase;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override;
	};
}
