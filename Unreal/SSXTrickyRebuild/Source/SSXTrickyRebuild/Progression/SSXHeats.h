// 0x37B390, 0x37B400, 0x37BA40
#pragma once

#include "CoreMinimal.h"

namespace SSXHeats
{
	constexpr int32 None = 12, NumRiders = 12, GridSize = 6, NumHeats = 3;
	constexpr int32 Free = 2, Held = 1, Taken = 0;
	constexpr int32 ExitNextHeat = 4, ExitDone = 2;
	constexpr int32 DefaultIntroId = 0x1AD, DefaultIntroType = 0x39;
	constexpr int32 AlaskaVenue = 11;
	SSXTRICKYREBUILD_API float HeatFactor(int32 Place);

	using FRand = TFunctionRef<uint32()>;

	struct SSXTRICKYREBUILD_API FStoryRecord
	{
		uint8 Rider = 0, Venue = 0, Mode = 0, Heat = 0;
		uint8 Riders[5] = { None, None, None, None, None };
		uint16 IntroId = 0;
		uint8 IntroType = 0;
		int32 Score(int32 InRider, int32 InVenue, int32 InMode, int32 InHeat) const;
		static FStoryRecord FromBytes(const uint8* B);
	};

	struct FStoryResult
	{
		int32 Riders[5] = { None, None, None, None, None };
		int32 IntroIds[15];
		int32 IntroTypes[15];
		FStoryResult() { for (int32 K = 0; K < 15; ++K) { IntroIds[K] = DefaultIntroId; IntroTypes[K] = DefaultIntroType; } }
	};

	struct SSXTRICKYREBUILD_API FData
	{
		TArray<FStoryRecord> Tables[3];
		int32 Agg[NumRiders][NumRiders][4] = {};
		bool bLoaded = false;
		bool LoadJson(const FString& Path, FString* OutError = nullptr);
		static const FData& Get();
		static FString DefaultPath();
	};

	SSXTRICKYREBUILD_API FStoryResult StoryLookup(const FData& D, int32 Rider, int32 Venue, int32 Mode, int32 Heat, FRand Rand);

	struct SSXTRICKYREBUILD_API FElimination
	{
		int32 Status[NumRiders][NumHeats];
		int32 Flag[NumRiders];
		FElimination() { Reset(); }
		void Reset();
		void InitHeat0(const FData& D, int32 PlayerRider, int32 Venue, int32 Mode, FRand Rand);
	};

	// 0x16D300
	SSXTRICKYREBUILD_API FStoryResult BuildGrid(FElimination& E, const FData& D, int32 PlayerRider, int32 Venue, int32 Mode, int32 Heat,
		FRand Rand, int32 OutGrid[GridSize]);
	// 0x165CF0, 0x1664B0
	SSXTRICKYREBUILD_API FStoryResult SingleEventGrid(const FData& D, const TArray<int32>& HumanRiders, int32 N, int32 Venue, int32 Mode,
		FRand Rand, int32 OutGrid[GridSize], int32 OutHuman[GridSize]);
	// 0x16D0B0, 0x16BBA0, 0x1635B8
	SSXTRICKYREBUILD_API bool HeatResult(FElimination& E, const TArray<int32>& FinishRiders, int32 PlayerRider, int32 Heat, float& InOutFactor);
	// 0x16C3B0
	inline int32 CircuitExitCode(int32 Mode, bool bAdvance) { return (Mode == 4 && bAdvance) ? ExitNextHeat : ExitDone; }
	// 0x16C008
	SSXTRICKYREBUILD_API void InitialLevels(const FData& D, const TArray<int32>& Riders, int32 HumanIndex, int32 Heat,
		const int32 Carried[NumRiders], const int32 Career[NumRiders], TArray<TArray<int32>>& Out);
	// 0x167EA0, 0x165778, 0x165A40, 0x165B98, 0x165688, 0x168028, 0x168180
	SSXTRICKYREBUILD_API void SetupLevels(const FData& D, const TArray<int32>& Riders, int32 HumanIndex, const int32 Career[NumRiders],
		TArray<TArray<int32>>& Out);
	// 0x16C228
	SSXTRICKYREBUILD_API void CarryOver(const TArray<int32>& Riders, int32 HumanIndex, const TArray<int32>& LevelsTowardHuman, int32 Carried[NumRiders]);
}
