// 0x125088, 0x31D310, 0x13BD40, 0x125A00, 0x124DD0, 0x126540, 0x216C50, 0x20D928
#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderState.h"
#include "Sim/RiderWorld.h"

class FRiderSim;
namespace SSXAnim { struct FRiderAnimChannel; }

namespace SSXGround
{
	constexpr double PropPushOutK = 1.1;
	constexpr double PropBounceMin = 55.555553;     // cm/s
	constexpr double PropRumbleK = 0.0012;          // 0x125BC0
	constexpr double PropRumbleThreshold = 0.5;     // 0x150BE8
	constexpr double PropRumbleTimeK = 1.6666666;
	constexpr double PropRumbleMinTime = 0.02;
	constexpr double PropRumbleMaxTime = 0.1;
	constexpr double PropSoundListenerRange = 40.0;

	SSXTRICKYREBUILD_API int32 PropMaskForMotion(ERiderMotion Motion);

	enum class EPropReaction : uint8
	{
		None,
		Skipped,
		Bump,
		Bounce,
		Movable
	};

	struct FPropResponse
	{
		FVector Normal = FVector::ZeroVector;
		double Approach = 0.0;
		FVector Push = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		EPropReaction Reaction = EPropReaction::None;
		double Dv = 0.0;
		double Rumble = 0.0;
	};

	SSXTRICKYREBUILD_API FPropResponse ComputePropResponse(const FRiderPropInfo& Prop, double Depth, const FVector& HitNormal, const FVector& V,
	                                                 ERiderMotion Motion, ERiderControl Control, const FVector& GroundNormal,
	                                                 double RiderMass = 1.0);

	struct FPropContactEvent
	{
		FRiderWorldHit Hit;
		FRiderPropInfo Prop;
		FRiderImpact Contact;
		FPropResponse Response;
		int32 TouchEffect = -1;                 // TODO: effect interpreter
		bool bRumble = false;
		double RumbleSeconds = 0.0;
		bool bHitSound = false;                 // TODO: prop sound attachments
	};

	// 0x257810
	SSXTRICKYREBUILD_API bool PropCollision(FRiderSim& Sim, ERiderMotion Motion, SSXAnim::FRiderAnimChannel* Anim, FPropContactEvent* OutEvent = nullptr,
	                                  int32 MaskOverride = 0);
}
