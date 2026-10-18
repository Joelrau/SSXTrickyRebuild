// 0x1272E8, 0x156E80, 0x156148, 0x156568
#pragma once

#include "CoreMinimal.h"
#include "Sim/Air/RiderMessages.h"

struct FRiderState;

namespace SSXAir
{
	constexpr double TrickValueSpinStep = double(0.06995432f);   // 0x155E00
	constexpr double TrickValueFlipStep = double(0.2499898f);    // 0x155E68
	constexpr double TrickValuePrevRail = 0.2;
	constexpr double TrickValueFromRail = 0.18;
	constexpr double TrickValueRailK = 0.0025;
	constexpr double TrickValueRailSpin = 0.06995432;
	constexpr double GrabRegisterValue = 0.04249751;
	constexpr double GrabHoldValue = 0.00075006403;
	constexpr double TrickFinishValue = 0.04500384;
	constexpr int32 MaxGrabNames = 3;
	constexpr int32 RepeatHistory = 5;
	constexpr double MeterScale = 0.6786545;
	constexpr double PointsScale = 10000.0;
	constexpr double BailMeter = double(-0.09999456f);   // 0x156DE0
	constexpr double RespawnMeter = double(-0.11997082f);  // 0x155AA8
	constexpr double MeterNearFullFrom = double(0.85f);    // 0x11B018, 0x2191C0
	// 0x108378, 0x3625C8
	constexpr double LandReactionThreshold[10] = { 0.75, 0.68, 0.6, 0.55, 0.5, 0.45, 0.4, 0.35, 0.3, 0.25 };
	constexpr double UberReactionStrength = 100.0; // 0x11F3B8
	constexpr double TrickyWindowSeconds = 20.0;
	constexpr int32 InfiniteBoostUbers = 5;
	constexpr double RailStartPointsMult = 1.3;
	constexpr double AnimFps = 30.0;

	// 0x157198
	SSXTRICKYREBUILD_API int32 Round180Signed(double Deg);
	SSXTRICKYREBUILD_API int32 Round180(double Deg);

	SSXTRICKYREBUILD_API double TrickHoldRate(int32 AnimId);
	SSXTRICKYREBUILD_API int32 TrickNameIndex(int32 AnimId);
	SSXTRICKYREBUILD_API bool IsUberAnim(int32 AnimId);

	struct FTrickRecord
	{
		uint32 W0 = 0, W1 = 0;
		bool bValid = false;

		bool IsUnnameable() const { return (W0 & 0xFF) == 0x2C; }
		bool IsUnrepeatable() const { return IsUnnameable() || ((W0 >> 16) & 0xFF) == 0x65; }
		bool operator==(const FTrickRecord& O) const { return W0 == O.W0 && W1 == O.W1; }
	};

	constexpr int32 ComboBonusStep = 4000;
	constexpr int32 ComboBonusMaxSteps = 4;
	constexpr double BigAirMinSeconds = 4.0;
	constexpr int32 BigAirBonusStep = 1000;
	constexpr double RailComboSeconds = 1.0;
	constexpr int32 RailKindUnknown = 1;

	struct FTrickSummary
	{
		int32 Spins180 = 0;   // signed count of 180 deg steps (TRICKDEF units)
		int32 Flips = 0;
		double Value = 0.0;
	};

	struct SSXTRICKYREBUILD_API FTrickScore
	{
		double Value = 0.0;
		double PointsMult = 1.0;
		int32 BestSpin = 0;
		int32 BestFlip = 0;
		double SpinDeg = 0.0;
		double FlipDeg = 0.0;
		double TotalSpinDeg = 0.0;
		double TotalFlipDeg = 0.0;
		int32 AirFrames = 0;
		int32 HoldFrames = -1;
		double HoldRate = 0.0;
		int32 RailFrames = -1;
		TArray<int32, TInlineAllocator<MaxGrabNames>> GrabNames;
		int32 GrabCount = 0;
		int32 PendingUbers = 0;
		int32 UberCount = 0;
		double HoldSecondsTotal = 0.0;
		double HoldSecondsBest = 0.0;   // 0x157008
		bool bSegmentFakie = false;
		bool bSegmentLate = false;
		int32 SegmentType = 0;
		bool bAltFlag = false;        // TODO: meaning
		int32 Combo = 0;
		TArray<FTrickRecord> History;
		int32 SegmentStarts = 0;      // FUN_00156618
		// FUN_001579E0, 0x1569B0, 0x156618, 0x2190F0
		TFunction<bool(uint32 W0, uint32 W1)> BookCheck;

		FRiderMessages Messages;
		FTrickRecord Announced;
		FString TrickText;            // FUN_001551c0
		int32 Score = 0;
		int32 TierCounts[6] = {};
		int32 ScoredTricks = 0;
		int32 BestTrickPoints = 0;
		FTrickRecord BestTrick;
		double AirSecondsTotal = 0.0;
		double AirSecondsBest = 0.0;
		double HoldSecondsRace = 0.0;
		double GrabSecondsBest = 0.0;
		double RailSecondsTotal = 0.0;
		double RailSecondsBest = 0.0;
		int32 SpinDegRace = 0;
		int32 FlipDegRace = 0;
		int32 GrabsRace = 0;
		int32 RailsRace = 0;
		int32 CrashCount = 0;
		int32 KnockdownCount = 0;
		int32 WipeOutCount = 0;
		int32 RespawnCount = 0;
		float SpeedKmhSum = 0.f;      // 0x155DC8
		float SpeedFrames = 0.f;      // 0x1B1278

		// FUN_00155dc8, 0x117198
		void SampleSpeed(float Speed);

		double LastReward = 0.0;
		int32 LastPoints = 0;
		int32 LastRepeatCount = 0;
		FTrickRecord LastRecord;
		FTrickSummary LastSummary;

		void UpdateAngles(double SpinDegrees, double FlipDegrees);
		void GrabReachesHold(int32 AnimId);
		void TrickUpgraded(int32 AnimId);
		void TickHold();
		void FinishTrick(int32 AnimId, double FinishValue);
		void RailFrame(double Speed);
		void RailSpin(int32 Degrees);
		FTrickSummary Recognise() const;
		void BankSegment();
		FTrickRecord BuildRecord(bool bToRail = false, bool bLandSwitch = false, bool bSteep = false) const;
		// FUN_00157088
		void Announce(const FTrickRecord& R, int32 N);
		void ResetRaceStats();
		void CheckBook(const FTrickRecord& R);
		// 0x1284E0
		double StartSegment(bool bSwitch, int32 Type, bool bLate, bool bAlt = false);
		// FUN_00155440, FUN_001554c0
		static int32 PointsFor(double V, double M);
		/** Copies of R among the last 5 records (0 if R never counts); R is then stored. */
		int32 RepeatCount(const FTrickRecord& R);
		double LateTrick(bool bGoofy);
		// FUN_001569b0
		double Land(bool bPrevWasRail, int32 ToRailKind, bool bLandSwitch = false, bool bFinished = false, bool bSteep = false);
		// FUN_00156de0
		double Bail(bool bCrash = false);
		// FUN_00155aa8, 0x11CAE8
		double Respawn();
		// FUN_00155cd0
		bool ScriptMultiplier(float M);
		int32 MultiplierCounts[3] = {};

	private:
		void ClearSegment();
	};

	SSXTRICKYREBUILD_API double BoostAdd(FRiderState& State, double Reward, double Gain);
}
