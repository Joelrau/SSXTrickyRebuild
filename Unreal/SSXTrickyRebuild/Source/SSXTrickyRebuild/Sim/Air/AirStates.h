#pragma once

#include "CoreMinimal.h"
#include "Sim/Air/AirMath.h"
#include "Sim/Air/LandingPredictor.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/Anim/RiderAnim.h"

class FRiderSim;
struct FRiderState;

namespace SSXAir
{
	struct FSpinControlState
	{
		int32 Phase = 2;
		double SpinHi = 0.0, SpinLo = 0.0, SpinFloor = 0.0;
		double FlipHi = 0.0, FlipLo = 0.0, FlipFloor = 0.0;
		double SpinRate = 0.0, FlipRate = 0.0;
		double AlignAngle = 0.0;                   // remaining auto-align rotation (6)
		FVector AlignAxis = FVector(0, 0, 1);
		double CapSpin = 2.0 * PI, CapFlip = 2.0 * PI;
		bool bLateMode = false;
		bool bAirTurnMode = false;
		bool bAirTiltInvert = false;
		// 0x100598
		bool bAuto180Toggle = false;
		int32 Auto180Count = 0;
		// 0x100908
		double PlayOutProgress = 0.0;
		double SettleOffset = 0.0;
	};

	// 0x15FDD0, 0x15EEC0, 0x15FE70
	struct FSpinAnimModel
	{
		int32 Id = -1;
		double Frame = 0.0;
		double Speed = 1.0;
		int32 NFrames = 0;
		int32 StartFrame = -1;
	};

	struct FAirLandingRecord
	{
		bool bValid = false;
		int32 Frame = 0;
		ELandingVerdict Verdict = ELandingVerdict::LandNormal;
		FLandingResult Result;
		FVector Normal = FVector::UpVector;
		FVector VelocityBefore = FVector::ZeroVector;
		int32 Surface = 1;
		int32 AirFrames = 0;
		double Reward = 0.0;
		int32 Points = 0;
		FTrickSummary Summary;
		double TotalSpinDeg = 0.0;
		double TotalFlipDeg = 0.0;
		bool bBodyContactBail = false;
		int32 AnimCategory = 0;
		double AnimFrame = 0.0;
		int32 LandHardAnim = -1;
	};

	struct FAirConfig
	{
		bool bRefinePredictorHit = false;
		ERiderLineQuery PredictorQueryType = ERiderLineQuery::Coarse;
		TFunction<bool(const FRiderSim&, int32 GrabId)> HasUber;
	};

	struct FAirContext
	{
		FAirConfig Config;
		TSharedPtr<SSXAnim::FRiderAnimChannel> Anim;
		FSpinControlState Spin;
		FTrickScore Score;
		FLandingPredictorResult LastPrediction;
		FAirLandingRecord LastLanding;
		// seeds handed to the next Spin Enter (late trick / NaturalAir)
		bool bSeedsPending = false;
		double PendingSeedSpin = 0.0;
		double PendingSeedFlip = 0.0;
		bool bPendingLate = false;
		bool bTookOffFromRail = false;
		bool bTrickRegistered = false;
		// 0x126E30, 0x1272E8
		bool bChannel0AirIdle = false;
		FSpinAnimModel SpinAnim;
		int32 AirFrames = 0;
		double ApexZ = -1e30;
	};

	SSXTRICKYREBUILD_API FQuat GetRenderedOrientation(const FRiderState& State);

	SSXTRICKYREBUILD_API FAirContext* GetAirContext(const FRiderSim& Sim);
}

SSXTRICKYREBUILD_API void RegisterAirStates(FRiderSim& Sim);
