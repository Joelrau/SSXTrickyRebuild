#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderSim.h"

struct FReplayRow
{
	int32 Frame = 0;
	double T = 0.0;
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	double Speed = 0.0;
	double Kmh = 0.0;
	FString Motion;
	FString Control;
	double BoostMeter = 0.0;
	double TrickyTimer = 0.0;
	double Steer = 0.0;
	double Crouch = 0.0;
	int32 Surface = 0;
	uint32 WordModel = 0;
	uint32 WordRecorded = 0;
	bool bWordMatch = false;
};

struct FReplayOptions
{
	FString InpPath;
	FString World = TEXT("slope15");
	bool bHaveSpawn = false;
	FVector SpawnPos = FVector::ZeroVector;
	FVector SpawnHeading = FVector(0, 1, 0);
	double SpawnSpeed = 0.0;
	int32 MaxFrames = -1;
};

struct FReplayResult
{
	bool bOk = false;
	FString Error;
	FString States;
	FString SpawnInfo;
	TArray<FReplayRow> Rows;
	TArray<FRiderSimEvent> Events;
};

struct FReplayDiff
{
	int32 Frames = 0;
	int32 FirstDivergence = INDEX_NONE;
	int32 FirstDrift = INDEX_NONE;          // first frame with |dpos| > 0.01 cm
	int32 FirstStateMismatch = INDEX_NONE;
	double MaxPosErr = 0.0;
	int32 MaxPosFrame = 0;
	double MaxVelErr = 0.0;
	int32 MaxVelFrame = 0;
	int32 StateMismatchFrames = 0;
	int32 WordModelMatches = 0;
	FString Subsystem;
	TArray<FString> Report;
	bool Passed() const { return Frames > 0 && FirstDivergence == INDEX_NONE; }
};

namespace SSXReplay
{
	const TCHAR* PyName(ERiderMotion M);
	const TCHAR* PyName(ERiderControl C);

	bool Run(const FReplayOptions& Options, FReplayResult& Out);
	bool WriteCsv(const FString& Path, const TArray<FReplayRow>& Rows);
	bool LoadCsv(const FString& Path, TArray<FReplayRow>& Rows, FString* OutError = nullptr);
	FReplayDiff Compare(const TArray<FReplayRow>& Ours, const TArray<FReplayRow>& Model,
	                                   double PosTol = 1.0, double VelTol = 1.0);

	FString OutputPath(const FString& Name);
	FString RunName(const FReplayOptions& Options);
	FString ReferencePath(const FString& Name);
}
