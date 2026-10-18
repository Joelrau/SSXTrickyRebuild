#pragma once

#include "CoreMinimal.h"

namespace SSXRaceEvent
{
	enum : int32
	{
		Marker = 0,
		Cue1 = 1,
		Cue9 = 9,
		Finish = 10,
		Checkpoint = 12,
		Lap = 13,
		AudioZoneA = 14,
		FinalLapCue = 18,
		AudioZoneB = 23,
		AudioZoneC = 24,
		AIJump = 25,   // file 100: take-off zone; param bit 0 flip, bit 1 spin, >> 2 km/h
		AISpeed = 26,   // file 101: speed request km/h
		AI102 = 27,
		AINoReset = 29,
		AI105 = 30,
		Repick = 31,
	};
}

struct FRacePathEvent
{
	int32 Type = 0;
	int32 FileType = 0;
	uint32 Param = 0;
	double Start = 0.0;
	double End = 0.0;
};

struct FRacePathHit
{
	const FRacePathEvent* Event = nullptr;
	bool bEdge = false;
};

struct FRacePathCache
{
	int32 Segment = -1;
};

// GAME space (Z up, cm). Segments (dir_x, dir_y, slope, horizontal length): the point at horizontal length t is
// segment start + (dir_x, dir_y, slope) t; projections are horizontal.
class SSXTRICKYREBUILD_API FRacePath
{
public:
	int32 Index = -1;
	bool bLinePath = false;
	int32 LineClass = 50;
	int32 CandidateFlag = 1;
	double DistToGoAtStart = 0.0;
	FVector Start = FVector::ZeroVector;
	FVector BoundsMin = FVector::ZeroVector;
	FVector BoundsMax = FVector::ZeroVector;
	TArray<FVector> Dirs;
	TArray<double> Lengths;
	TArray<FVector> Knots;
	TArray<double> Cum;
	TArray<FRacePathEvent> Events;

	void Finalise();

	double Length() const { return Cum.Num() ? Cum.Last() : 0.0; }
	FVector PointAt(double D) const;
	/** Unit 3D direction of the segment containing D. */
	FVector TangentAt(double D) const;
	double Project(const FVector& P, FVector& OutPoint, double& OutLateral, FRacePathCache* Cache = nullptr) const;
	double BoundsDistance(const FVector& P) const;
	void EventsIn(double D0, double D1, TArray<FRacePathHit>& Out) const;
	const FRacePathEvent* FindEvent(double D0, double D1, int32 Type, bool* bOutEdge = nullptr) const;
	int32 SegmentAt(double D) const;
};

struct SSXTRICKYREBUILD_API FRacePathSet
{
	TArray<int32> StartPaths;
	TArray<FRacePath> AIPaths;
	TArray<FRacePath> LinePaths;

	bool IsValid() const { return AIPaths.Num() > 0 && LinePaths.Num() > 0; }
	const FRacePath* MainLine() const;
	double RaceLength() const;
};

struct FRaceSsfNode
{
	uint32 FileOffset = 0;
	int32 NodeClass = -1;
	int32 Mode = -1;
	double Seconds = 0.0;
	double Gain = 0.0;   // 1/s
	double TargetSpeed = 0.0;  // cm/s (file m/s x 100)
	FVector Direction = FVector::ZeroVector;
};

class SSXTRICKYREBUILD_API FRaceTrackData
{
public:
	int32 TrackId = -1;
	bool bHasWorldFloor = false;
	double WorldFloorZ = 0.0;
	FVector LtgBoundsMin = FVector::ZeroVector;
	FVector LtgBoundsMax = FVector::ZeroVector;
	FRacePathSet Race;
	FRacePathSet Showoff;
	TArray<double> BurstSeconds;
	TArray<double> LaunchSeconds;
	int32 ResetEffectCount = 0;
	TArray<FRaceSsfNode> Nodes;

	bool Load(const FString& File, FString* OutError = nullptr);
	bool IsLoaded() const { return Race.IsValid(); }
	const FRacePathSet& PathsFor(bool bShowoffMode) const { return (bShowoffMode && Showoff.IsValid()) ? Showoff : Race; }

	static FString DefaultPath(const FString& Track);
};
