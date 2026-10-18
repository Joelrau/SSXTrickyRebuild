#pragma once

#include "CoreMinimal.h"
#include "Race/RaceRules.h"

class FRacePath;
struct FRaceRandom;

namespace SSXRaceAI
{
	enum EBehaviour : int32 { Jump = 0, Cruise = 1, Pass = 2, Attack = 3 };

	constexpr double SteerGain = 6.2897024;
	constexpr double SteerDead = 0.2;
	constexpr double SteerMax = 0.9705659;
	constexpr double SkillSteer = 1.0241041;
	constexpr double SkillPitch = 7.8941293;
	constexpr double SkillGate = 3.2585914;
	constexpr double SkillTuck = 2.211699;
	constexpr double SkillSpeed = 1.4103198;
	constexpr double SkillStuck = 31.410845;
	constexpr double SkillHold = 14.454015;
	constexpr double SkillLinePick = 1.104;
	constexpr double SpeedBand = 138.88889;
	constexpr double TuckSlow = 833.3333;
	constexpr double JumpWindow = 300.0;
	constexpr double PrewindWindow = 50.0;
	constexpr double SpeedWindow = 600.0;
	constexpr double JumpMaxLateral = 153.10164;
	constexpr double JumpMaxSteer = 0.536165;
	constexpr double GatePushTime = 30.0;
	constexpr double GateBackTime = 60.0;
	constexpr double TrickTimeA = 0.8, TrickTimeB = 0.8057834, TrickTimeC = 0.78212035;
	constexpr double TrickShortRelease = 0.3;
	constexpr int32 TrickStatOffset = 16;
	constexpr double ShortHoldLimit = 20.0;
	constexpr double ShoveRange = 200.0;
	constexpr int32 ShovePeriod = 6;
	constexpr int32 ShoveBlockAnim = 0x15;
	constexpr double TargetRange = 700.0;
	constexpr double TargetDrop = 1050.0;
	constexpr double TargetLateralDrop = 600.0;
	constexpr double TargetLateralMax = 540.0;
	constexpr int32 TargetPeriod = 12;
	constexpr double PassClearance = 150.0;
	constexpr double AttackLead = 300.0;
	constexpr double AttackWait = 200.0;
	constexpr int32 WrongWayFrames = 60;
	constexpr double OffPathFrames = 222.0;
	constexpr double OffPathLateral = 1000.0;
	constexpr double OffPathLateralKnockdown = 1500.0;
	constexpr double AirTimeout = 30.0;
	constexpr int32 NoTrick = 26;
}

struct FAIOtherRider
{
	int32 Index = 0;
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	int32 Rank = 0;
	bool bHuman = false;
};

struct FAIRiderView
{
	int32 Index = 0;
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	FVector Side = FVector(1, 0, 0);   // render matrix row 0 (right)
	FVector Fwd = FVector(0, 1, 0);   // row 1 (nose)
	FVector Up = FVector(0, 0, 1);   // row 2
	double Yaw = 0.0;
	int32 Motion = 0;
	int32 Control = 0;
	const FRacePath* Path = nullptr;
	double PathParam = 0.0;
	double PathParamPrev = 0.0;
	double PathLateral = 0.0;
	FVector Lookahead = FVector::ZeroVector;
	double PathHeading = 0.0;
	double BoostMeter = 0.0;
	int32 Rank = 0;
	int32 BoardClass = 0;
	int32 ProfileBoardClass = 0;
	int32 Character = 0;
	int32 StatAir = 128;
	int32 StatTrick = 128;
	double GateCur = 0.0;
	double GateTarget = 0.0;
	double PredTimeLeft = 0.0;
	double AirElapsed = 0.0;
	FVector PredLandNormal = FVector(0, 0, 1);
	double SpinAngle = 0.0;
	double FlipAngle = 0.0;
	double SpinRate = 1.0;
	double FlipRate = 1.0;
	int32 Channel1Anim = 0;
	double RivalDist[SSXRace::MaxRacers] = {};
	double RivalBearing[SSXRace::MaxRacers] = {};
	int32 CatchupRival = -1;
	double CatchupNear = 0.0;
	double CatchupFar = 0.0;
	SSXRace::FGrudge Grudge;
	bool bFinished = false;
};

struct FAIRaceView
{
	int32 Frame = 0;
	double Clock = 0.0;                 // race clock; countdown clock units while gating
	ERaceEventMode Mode = ERaceEventMode::CircuitRace;
	TConstArrayView<FAIOtherRider> Riders;
	int32 HumanRank = 0;
};

struct FAIState
{
	double TargetSpeed = 0.0;
	double Skill = 1.0;
	double SkillBehind = 1.0;
	double SkillAhead = 1.0;
	int32 Trick = SSXRaceAI::NoTrick;
	bool bSpinFlipOn = false;
	bool bShortHold = false;
	bool bTrickAllowed = false;
	double SpinDir = 0.0;
	double FlipDir = 0.0;
	int32 Target = -1;
	int32 OffPathFrames = 0;
	int32 WrongWayFrames = 0;
	int32 Behaviour = SSXRaceAI::Cruise;
	double TimeScale = 1.0;
};

namespace SSXRaceAI
{
	SSXTRICKYREBUILD_API void ResetAI(FAIState& St, double SkillAhead, double SkillBehind);
	// 0x139AB8
	SSXTRICKYREBUILD_API bool ResetDecision(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race);

	SSXTRICKYREBUILD_API uint32 AIWord(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, FRaceRandom& Rng);

	SSXTRICKYREBUILD_API double SteerToward(const FAIRiderView& R, const FVector& Target, double Skill);
}
