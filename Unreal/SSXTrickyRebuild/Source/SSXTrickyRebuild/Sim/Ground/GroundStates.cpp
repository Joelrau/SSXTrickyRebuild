#include "Sim/Ground/GroundStates.h"

#include "Sim/Ground/GroundControls.h"
#include "Sim/Ground/GroundMotion.h"
#include "Sim/RiderSim.h"

TSharedRef<FGroundContext> RegisterGroundStates(FRiderSim& Sim, const FGroundConfig& Config)
{
	using namespace SSXGround;
	using C = ERiderControl;
	TSharedRef<FGroundContext> Ctx = MakeShared<FGroundContext>();
	Ctx->Config = Config;
	Ctx->Anim = SSXAnim::AcquireChannel(Sim);

	Sim.SetMotionState(ERiderMotion::Ground, MakeShared<FGroundMotion>(Ctx));
	Sim.SetMotionState(ERiderMotion::RailSlide, MakeShared<FRailSlideMotion>(Ctx));

	Sim.SetControlState(C::Cruise, MakeShared<FCruiseControl>(Ctx));
	Sim.SetControlState(C::ChangeFakie, MakeShared<FChangeFakieControl>(Ctx));
	Sim.SetControlState(C::FakieCruise, MakeShared<FFakieCruiseControl>(Ctx));
	Sim.SetControlState(C::Bump, MakeShared<FBumpControl>(Ctx));
	Sim.SetControlState(C::Prewind, MakeShared<FPrewindControl>(Ctx));
	Sim.SetControlState(C::Jump, MakeShared<FJumpControl>(Ctx));
	Sim.SetControlState(C::LandNormal, MakeShared<FLandNormalControl>(Ctx));
	Sim.SetControlState(C::LandHard, MakeShared<FLandHardControl>(Ctx));
	Sim.SetControlState(C::RailSlide, MakeShared<FRailSlideControl>(Ctx));
	Sim.SetControlState(C::GateAnticipate, MakeShared<FGateAnticipateControl>(Ctx));
	Sim.SetControlState(C::GateLaunch, MakeShared<FGateLaunchControl>(Ctx));
	Sim.SetControlState(C::GateToBase, MakeShared<FGateToBaseControl>(Ctx));
	return Ctx;
}

void RegisterGroundStates(FRiderSim& Sim)
{
	RegisterGroundStates(Sim, FGroundConfig());
}
