// 0x118298, 0x118090
#pragma once

#include "CoreMinimal.h"
#include "Race/RaceTrackData.h"

struct FRaceRandom;

namespace SSXRaceTrack
{
	constexpr double Lookahead = 800.0;
	constexpr double RepickLateral = 500.0;
	constexpr int32 RepickPeriod = 60;
	constexpr double EndMargin = 200.0;
	constexpr int32 AICandidates = 6;
	constexpr int32 LineCandidates = 3;
	constexpr double LineClassWeight = 23187.357;
	constexpr int32 LineClassDefault = 50;
	constexpr double ClassMeterBoard0 = 0.10337617;
	constexpr double ClassMeterBoard1 = 0.31186903;

	SSXTRICKYREBUILD_API int32 LineClassWanted(int32 BoardClass, int32 Rank, double BoostMeter, double SkillProb, FRaceRandom& Rng);

	SSXTRICKYREBUILD_API int32 ChooseLine(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Current,
	                                bool bExcludeCurrent, int32 WantClass);

	SSXTRICKYREBUILD_API int32 PickLinePath(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Exclude, int32 Current = -1,
	                                  double CurrentToGo = 0.0, bool* OutResetHint = nullptr);
}

struct SSXTRICKYREBUILD_API FRaceLineFollow
{
	int32 Path = -1;
	double Param = 0.0;
	double ParamPrev = 0.0;
	double Lateral = 0.0;
	FVector Point = FVector::ZeroVector;
	FVector Look = FVector::ZeroVector;       // +0x360 line point 800 cm ahead
	double Heading = 0.0;
	FRacePathCache Cache;

	void SetPath(int32 InPath) { Path = InPath; Cache = FRacePathCache(); }

	void Follow(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Frame, bool bFinished,
	            TFunctionRef<int32()> WantClass);
	void Follow(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Frame, bool bFinished)
	{
		Follow(Set, Pos, Vel, Frame, bFinished, [] { return -1; });
	}
	void Reproject(const FRacePathSet& Set, const FVector& Pos);
	void PlaceAt(const FRacePathSet& Set, double InParam);
};

struct FRaceTriggerEvent
{
	int32 Type = 0;
	uint32 Param = 0;
	bool bEnter = true;
};

struct SSXTRICKYREBUILD_API FRaceLineProgress
{
	int32 Line = -1;
	double DistToGo = 0.0;
	double Best = 1e30;
	uint32 Mask = 0;
	uint32 MaskPrev = 0;
	int32 LapCount = 0;
	FRacePathCache Cache;

	void SetLine(int32 InLine) { Line = InLine; Cache = FRacePathCache(); }

	// 0x118298
	void Update(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Frame, bool bFinished,
	            double LapLength, TArray<FRaceTriggerEvent>& OutEvents);
	void Measure(const FRacePathSet& Set, const FVector& Pos);
	// 0x1969D8, 0x197B90
	static bool RepickMarker(const FRacePath& Path, double BestToGo, double ToGo);

private:
	void Query(const FRacePath& Path, double LoToGo, double HiToGo, TArray<const FRacePathEvent*>& Out) const;
};
