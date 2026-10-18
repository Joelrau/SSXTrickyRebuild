#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderSim.h"
#include "Sim/Ground/GroundPhysics.h"
#include "Sim/Ground/GroundStates.h"

namespace SSXGround
{
	FGroundRider MakeGroundRider(const FRiderSim& Sim, const FGroundContext& Ctx);

	// 0x125CB8
	SSXTRICKYREBUILD_API bool TryRailAttach(FRiderSim& Sim);
	// 0x126540
	SSXTRICKYREBUILD_API void LogRailPropBounce(FRiderSim& Sim, ERiderMotion Before, int32 Instance, const FVector& Normal, double Dv, double Approach);

	void NaturalTakeOff(FRiderSim& Sim, FGroundContext& Ctx);

	class SSXTRICKYREBUILD_API FGroundMotion : public IRiderMotionState
	{
	public:
		explicit FGroundMotion(TSharedRef<FGroundContext> InCtx) : Ctx(InCtx) {}
		virtual void Enter(FRiderSim& Sim, ERiderMotion Previous) override;
		virtual void Update(FRiderSim& Sim) override;
		virtual TSharedPtr<FRiderStateBlob> SaveState() const override;
		virtual void LoadState(const FRiderStateBlob& Blob) override;
	private:
		TSharedRef<FGroundContext> Ctx;
	};

	class SSXTRICKYREBUILD_API FRailSlideMotion : public IRiderMotionState
	{
	public:
		explicit FRailSlideMotion(TSharedRef<FGroundContext> InCtx) : Ctx(InCtx) {}
		virtual void Enter(FRiderSim& Sim, ERiderMotion Previous) override;
		virtual void Update(FRiderSim& Sim) override;
		virtual void Exit(FRiderSim& Sim, ERiderMotion Next) override;
	private:
		void Leave(FRiderSim& Sim);
		TSharedRef<FGroundContext> Ctx;
	};
}
