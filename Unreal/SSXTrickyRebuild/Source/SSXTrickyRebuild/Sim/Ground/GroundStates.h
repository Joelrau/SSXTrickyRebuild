#pragma once

#include "CoreMinimal.h"
#include "Sim/Anim/RiderAnim.h"

class FRiderSim;

struct FGroundConfig
{
	bool bUsePathHeading = false;
	double StatOverride = -1.0;
	double TurnAroundSeconds = 0.5;
	// 0x15FDE0
	double GateRockPhase = 1.0;
	double GatePushPhase = 0.5;
};

struct FGroundContext
{
	FGroundConfig Config;
	bool bPushAnim = false;
	int32 PushFramesLeft = 0;
	TSharedPtr<SSXAnim::FRiderAnimChannel> Anim;
	double Absorb = 0.25;
	double LastLaunchImpulse = -1.0;
	double ChatterPhase = 0.0;
	double RailTime = 0.0;
	double RailLateral = 0.0;
	double RailRoll = 0.0;
	int32 RailPending = 0;
	int32 RailSpinDeg = 0;
	bool bRailLatch = false;
	int32 RailSwitchClip = 0;
	int32 RailSwitchFrame = 0;
	int32 RailSwitchCount = 0;
	bool bPrewindAbort = false;
	bool bTurnAround = false;
	// 0x105F48, 0x105F58, 0x106140
	double GateMax = 0.0, GateMin = 0.0, GateStill = 0.0;
	bool bGatePushed = false;
};

SSXTRICKYREBUILD_API TSharedRef<FGroundContext> RegisterGroundStates(FRiderSim& Sim, const FGroundConfig& Config);
SSXTRICKYREBUILD_API void RegisterGroundStates(FRiderSim& Sim);
