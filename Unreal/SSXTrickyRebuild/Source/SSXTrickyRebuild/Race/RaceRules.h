#pragma once

#include "CoreMinimal.h"

// Units cm, s, rad; 60 Hz.
//
class FRacePath;
struct FRacePathSet;

enum class ERaceEventMode : uint8
{
	PracticeKnockdown = 0,
	PracticeFreeride = 1,
	PracticeRace = 2,
	PracticeShowoff = 3,
	CircuitRace = 4,
	CircuitShowoff = 5,
	Lesson = 6,
	PracticeTimedRace = 7,
	Demo = 8,
	Test = 9,
};

enum class ERacePhase : uint8
{
	None = 0,
	PreRace = 1,
	StartRace = 2,
	Countdown = 3,
	Race = 4,
	EndRace = 5,
	LoadLesson = 6,
	PreLesson = 7,
	RestartLesson = 8,
	SelectLesson = 9,
	EndLesson = 10,
	ReplayEndRace = 11,
};

SSXTRICKYREBUILD_API const TCHAR* LexToString(ERacePhase Phase);
SSXTRICKYREBUILD_API const TCHAR* LexToString(ERaceEventMode Mode);

namespace SSXRace
{
	constexpr double FrameDt = 1.0 / 60.0;
	constexpr double KmhToCms = 27.777777;   // event params km/h -> cm/s

	constexpr int32 StartSlots = 6;
	constexpr int32 MaxRacers = 6;
	constexpr double StartSizeSpacing = 90.0;       // cm per unit of rider size along the start tangent
	constexpr double CountdownStart = -300.0;       // race clock at countdown enter (clock units)
	constexpr double CountdownRate = 2.0;   // clock units per frame; GO when >= 0 (150 frames)
	constexpr double CountdownCuePeriod = 60.0;     // clock units between countdown cues

	constexpr int32 RankPeriod = 6;
	constexpr double RankHysteresis = 20.0;
	constexpr double KnockdownCatchup = double(517.34393f);
	constexpr double AICatchupGain = 1.1359849;
	constexpr double AICatchupFarGain = 2.0487268;
	constexpr double AITimeScaleMin = 0.70005476;
	constexpr double AITimeScaleMax = 1.5022597;
	constexpr double PlayerCatchupGain = 1.0941;
	constexpr double PlayerTimeScaleMax = 1.1657;
	constexpr double TimeScaleStep = 0.008446341;
	extern SSXTRICKYREBUILD_API const double CatchupNearRace[6];
	extern SSXTRICKYREBUILD_API const double CatchupFarRace[6];
	extern SSXTRICKYREBUILD_API const double CatchupNearCircuit[6];
	extern SSXTRICKYREBUILD_API const double CatchupFarCircuit[6];

	constexpr int32 SkillPeriod = 60;

	constexpr double RespawnDrop = 101.46;
	constexpr double RespawnSpeed = 833.3333;
	constexpr double RespawnCrowdRange = 1000.0;
	constexpr double RespawnCrowdLateral = 199.0;
	constexpr double RespawnStep = 200.0;
	constexpr double RespawnMaxShift = 800.0;
	constexpr double RespawnAlignCos = 0.866;
	constexpr double RespawnFallbackToGo = 500.0;
	constexpr double RespawnRubberMinLeft = 10000.0;
	constexpr int32 LappedTrackId = 8;
	constexpr int32 LappedTrackLaps = 4;    // 0x116AB8
	constexpr double RespawnRubberFactor = 0.8003195;
	constexpr double WorldFloorMargin = 10000.0;

	constexpr int32 GrudgeAngerPerHit = 30;
	constexpr int32 GrudgeLevelStep = 15;
	constexpr double GrudgeKind2Level = 80.98926;
	constexpr double GrudgeKind1Level = 60.0;
	constexpr float GrudgeCoolFrames = 360.44952f;   // 0x11EFD8

	SSXTRICKYREBUILD_API double WrapAngle(double A);

	SSXTRICKYREBUILD_API bool IsScoreMode(ERaceEventMode M);
	SSXTRICKYREBUILD_API bool IsGapMode(ERaceEventMode M);   // practice / circuit race (checkpoint gaps, catch-up rivals)
	SSXTRICKYREBUILD_API bool UsesGateStart(ERaceEventMode M, int32 TrackId);
	SSXTRICKYREBUILD_API double ShowoffTimeLimit(int32 TrackId);
	SSXTRICKYREBUILD_API bool CatchupTrack(int32 TrackId, double& OutLate, double& OutOffset);
	SSXTRICKYREBUILD_API void CatchupDifficulty(bool bCircuit, int32 Difficulty, double& OutKeep, double& OutK);

	SSXTRICKYREBUILD_API int32 StartSlot(int32 RacerIndex, int32 RacerCount);
	SSXTRICKYREBUILD_API int32 StartPose(const FRacePathSet& Paths, int32 RacerIndex, int32 RacerCount, double RiderSize,
	                               FVector& OutPos, double& OutHeading);

	SSXTRICKYREBUILD_API double CatchupLead(double RivalDist, double RivalBearing, double PathHeading);
	SSXTRICKYREBUILD_API double CatchupFactorAI(bool bHasLead, double Lead, double Near, double Far);
	SSXTRICKYREBUILD_API double CatchupFactorPlayer(bool bHasLead, double Lead, double Near);
	SSXTRICKYREBUILD_API double StepTimeScale(double Current, double Target);

	SSXTRICKYREBUILD_API void SkillLevels(ERaceEventMode Mode, int32 Difficulty, double& OutAhead, double& OutBehind);
	SSXTRICKYREBUILD_API double SelectSkill(double Current, double Ahead, double Behind, int32 Frame, bool bHasRival,
	                                  int32 MyRank, int32 HumanRank);

	struct FCrowdRacer
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
	};
	// 0x119220
	SSXTRICKYREBUILD_API bool RespawnPoint(const FRacePath& Path, double Param, TConstArrayView<FCrowdRacer> Others,
	                                 FVector& OutPos, double& OutHeading, FVector& OutVel,
	                                 TFunctionRef<bool(const FVector&)> SphereFree);

	// 0x11EA70, 0x11EAF8, 0x11EBF8, 0x16C008
	// per (racer, other racer) record cBoarder +0x50 + 0x20 j: byte 0 level, 1 level max, 2 anger limit, 3 anger,
	// 4 start level, 5 relation kind (1 = friend); +6 / +8 / +0xA bump / shove / knock-down counters, +0xC score
	struct FRivalry
	{
		int32 Level = 0;
		int32 LevelMax = 100;
		int32 AngerLimit = 30;
		int32 Anger = 0;
		bool bFriend = false;
		int32 Hits = 0;
		int32 Score = 0;
		int32 StartLevel = 0;    // 0x32E6C8
		int32 Kind = 0;
		int32 Bumps = 0;
		int32 Shoves = 0;
	};

	SSXTRICKYREBUILD_API void ResetRivalry(FRivalry& R, const FRivalry& Setup);
	// 0x11F050
	SSXTRICKYREBUILD_API void BlendLevel(FRivalry& R);

	struct FGrudge
	{
		bool bActive = false;
		int32 Frame = 0;
		int32 Rival = -1;
		int32 Kind = 0;
		bool bCooled = false;
		int32 Kind2Starts = 0;   // 0x11ECF8, 0x2336B8
	};

	SSXTRICKYREBUILD_API void StartGrudge(FGrudge& G, FRivalry* Rivals, int32 Target, int32 Frame);
	// 0x11EF00
	// on the rival's anger (byte 3), never the level: How 0 cooled (-15), 1 (-45), 2 satisfied (-max(45, anger / 2))
	SSXTRICKYREBUILD_API void RelieveGrudge(FGrudge& G, FRivalry* Rivals, int32 How);
	SSXTRICKYREBUILD_API void GrudgeCooldown(FGrudge& G, FRivalry* Rivals, int32 Frame);
	SSXTRICKYREBUILD_API bool GrudgeWantsAttack(const FGrudge& G, int32 Other, ERaceEventMode Mode);
	SSXTRICKYREBUILD_API void OnKnockdown(FGrudge& VictimGrudge, FRivalry* VictimRivals, FGrudge& AttackerGrudge,
	                                FRivalry* AttackerRivals, int32 VictimIndex, int32 AttackerIndex, bool bVictimAI,
	                                bool bAttackerAI, int32 Frame);
	// 0x124920
	SSXTRICKYREBUILD_API void OnBump(FGrudge& VictimGrudge, FRivalry* VictimRivals, FGrudge& AttackerGrudge,
	                           FRivalry* AttackerRivals, int32 VictimIndex, int32 AttackerIndex, bool bVictimAI,
	                           bool bAttackerAI, int32 Frame, bool bShove);
}
