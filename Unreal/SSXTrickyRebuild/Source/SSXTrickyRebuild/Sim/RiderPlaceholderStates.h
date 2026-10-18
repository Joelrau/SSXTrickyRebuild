#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderSim.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/WipeOut/TumbleBody.h"

namespace SSXSimPlaceholders
{
	class SSXTRICKYREBUILD_API FPlaceholderGroundMotion : public IRiderMotionState
	{
	public:
		virtual void Update(FRiderSim& Sim) override;
	};

	class SSXTRICKYREBUILD_API FPlaceholderAirMotion : public IRiderMotionState
	{
	public:
		static constexpr double GravityUp = -850.2414;
		static constexpr double GravityDown = -1900.8359;
		static constexpr double HorizontalDrag = 0.20002478;
		static constexpr double AirSpeedCap = 3347.2222;
		virtual void Enter(FRiderSim& Sim, ERiderMotion Previous) override;
		virtual void Update(FRiderSim& Sim) override;
	};

	class SSXTRICKYREBUILD_API FPlaceholderRailMotion : public IRiderMotionState
	{
	public:
		virtual void Update(FRiderSim& Sim) override;
	};

	struct FWipeOutContext
	{
		double AirTime = 0.0;
		double TotalTime = 0.0;
		double PhaseTime = 0.0;
		double AirTurn = 0.0;
		double AirTilt = 0.0;
		FRiderImpact Info;
		// 0x15FDD0, 0x15EEC0
		int32 AnimId = -1;
		int32 AnimPlayFrame = 0;
		int32 AnimFrames = 0;
		bool bTumblePoseSimulated = false;
		SSXWipeOut::FTumbleBody Body;
		FVector BindCentres[18];
		double Spin = 0.0;
		double LyingAngle = 0.0;
		int32 GetUpAnim = 0x24C;            // 0x10F178
		int32 Contacts = 0;
		SSXWipeOut::FTumbleContact LastContact;
		int32 SettleFrame = -1;
		TSharedPtr<SSXAnim::FRiderAnimChannel> Anim;
	};

	class SSXTRICKYREBUILD_API FWipeOutMotion : public IRiderMotionState
	{
	public:
		explicit FWipeOutMotion(TSharedRef<FWipeOutContext> InCtx) : Ctx(InCtx) {}
		virtual void Enter(FRiderSim& Sim, ERiderMotion Previous) override;
		virtual void Exit(FRiderSim& Sim, ERiderMotion Next) override;
		virtual void Update(FRiderSim& Sim) override;
		virtual FString DebugString() const override;
		FWipeOutContext& GetContext() const { return *Ctx; }
		virtual TSharedPtr<FRiderStateBlob> SaveState() const override { return SSXRiderBlob::Save(*Ctx); }
		virtual void LoadState(const FRiderStateBlob& B) override
		{
			const TSharedPtr<SSXAnim::FRiderAnimChannel> Keep = Ctx->Anim;
			SSXRiderBlob::Load(B, *Ctx);
			Ctx->Anim = Keep;
		}
	private:
		void EnterBody(FRiderSim& Sim);     // 0x10DAD8
		void TumbleRigid(FRiderSim& Sim, int32 Anim);
		void TumbleReduced(FRiderSim& Sim, int32 Anim);
		void Slide(FRiderSim& Sim, int32 Anim);
		void Recover(FRiderSim& Sim);       // 0x1102C8
		void BeginRecover(FRiderSim& Sim);
		TSharedRef<FWipeOutContext> Ctx;
	};


	SSXTRICKYREBUILD_API TSharedRef<FWipeOutContext> InstallWipeOutStates(FRiderSim& Sim);

	// 0x140B90
	SSXTRICKYREBUILD_API FWipeOutContext* FindWipeOutContext(const FRiderSim& Sim);

	class SSXTRICKYREBUILD_API FPlaceholderGroundControl : public IRiderControlState
	{
	public:
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override;
	};

	class SSXTRICKYREBUILD_API FPlaceholderLessonWaitControl : public IRiderControlState
	{
	public:
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override;
	};

	class SSXTRICKYREBUILD_API FPlaceholderResetWaitControl : public IRiderControlState
	{
	public:
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override;
		// 0x106A48, 0x1A7860
		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override;
	};

	// 0x108140
	class SSXTRICKYREBUILD_API FWipeOutControl : public IRiderControlState
	{
	public:
		explicit FWipeOutControl(TSharedRef<FWipeOutContext> InCtx) : Ctx(InCtx) {}
		// 0x108110
		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override { Sim.State.Highlight(19); }
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override;
	private:
		TSharedRef<FWipeOutContext> Ctx;
	};

	// 0x108198, 0x108280
	class SSXTRICKYREBUILD_API FWipeOutRecoverControl : public IRiderControlState
	{
	public:
		explicit FWipeOutRecoverControl(TSharedRef<FWipeOutContext> InCtx) : Ctx(InCtx) {}
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override;
	private:
		TSharedRef<FWipeOutContext> Ctx;
	};
}
