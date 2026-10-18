// 0x32EDC0, FUN_00168B88, FUN_001688F0, FUN_00169378, FUN_00168360, FUN_00169698, FUN_001699A0, FUN_00176850, FUN_0016DA10, FUN_0016DAA8, FUN_00169BE8, 0x15A030, 0x235780
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraDefs.h"
#include "Progression/SSXHeats.h"

namespace SSXIntroDialogue
{
	constexpr int32 NumSlots = 15, NumLines = 6;
	constexpr int32 DefaultId = 0x1AD, DefaultType = 0x39;
	constexpr int32 NoSpeaker = 13, NoLine = 0x300, NoAux = 0x1BF;
	// 0x165778, 0x165A40, 0x165B98, 0x165CF0
	constexpr int32 SingleRaceHeatWord = 3;
	constexpr int32 NormalType80 = 2;

	using FRand = TFunctionRef<uint32()>;

	struct FLine
	{
		int32 Speaker = NoSpeaker, LineId = NoLine, Aux = NoAux, Value = -1;
		bool operator==(const FLine& O) const { return Speaker == O.Speaker && LineId == O.LineId && Aux == O.Aux && Value == O.Value; }
	};
	struct FSlot
	{
		int32 Type = DefaultType;
		FLine Lines[NumLines];
	};

	// 0x3CC028
	struct FIntro
	{
		uint8 Speakers[NumLines] = {};
		int32 Start = 0;
		int32 Count = 0;
	};

	struct FLineClip
	{
		FString Name;
		int32 Frames = 0;
		TArray<FIntPoint> Events;
		// 0x15FB08
		int32 Key() const { return Events.Num() > 0 ? Events[0].X : -1; }
	};

	struct SSXTRICKYREBUILD_API FData
	{
		TArray<SSXHeats::FStoryRecord> Table3, Table4, CountTable, PairTable;   // 0x37C1B8, 0x37C4B0, 0x37CD60, 0x37C720
		TMap<int32, FIntro> Intros;
		TArray<TArray<int32>> VariantLines;     // 0x32F468
		struct FRecord { int32 Id = 0, Aux = 0, Value = -1; };
		TArray<FRecord> Records;                // 0x37CD70
		TMap<int32, FLineClip> Clips;
		bool bLoaded = false;

		bool LoadJson(const FString& Path, FString* OutError = nullptr);
		static const FData& Get();
		static FString DefaultPath();
		const FRecord* FindRecord(int32 LineId) const;
		const FLineClip* Clip(int32 LineId) const { return Clips.Find(LineId); }
	};

	SSXTRICKYREBUILD_API void BuildIntroTable(TConstArrayView<int32> Ids, TConstArrayView<TArray<uint8>> SpeakerRows,
		TConstArrayView<int32> GroupSizes, TMap<int32, FIntro>& Out);

	// 0x32ECF0
	struct FContext
	{
		int32 Riders[5] = { 12, 12, 12, 12, 12 };
		int32 Player = 12;
		int32 Ids[NumSlots];
		int32 Types[NumSlots];
		int32 Variants[NumSlots];
		FContext() { for (int32 K = 0; K < NumSlots; ++K) { Ids[K] = DefaultId; Types[K] = DefaultType; Variants[K] = 0; } }
		// 0x168720
		static FContext FromStory(const SSXHeats::FStoryResult& S, int32 PlayerRider);
	};

	struct FRacer
	{
		int32 Rider = 0;
		int32 Type80 = NormalType80;
		bool bHuman = false;
	};

	SSXTRICKYREBUILD_API int32 ScoreWithRiders(const SSXHeats::FStoryRecord& R, int32 Rider, int32 Venue, int32 Mode, int32 Heat,
		TConstArrayView<int32> Others);
	// FUN_001691F0
	SSXTRICKYREBUILD_API TArray<int32> TableLookup(FContext& C, int32 Slot, TConstArrayView<SSXHeats::FStoryRecord> Table, int32 Rider,
		int32 Venue, int32 Mode, int32 Heat, TConstArrayView<int32> Others, FRand Rand);

	SSXTRICKYREBUILD_API int32 BuildSlots(FContext& Ctx, TConstArrayView<FRacer> Racers, int32 Venue, int32 Mode, int32 HeatWord,
		const SSXHeats::FData& Heats, const FData& D, FRand Rand, FSlot Slots[NumSlots], TArray<TArray<int32>>* OutCandidates = nullptr);
	SSXTRICKYREBUILD_API void FillLines(FContext& Ctx, const FData& D, FSlot Slots[NumSlots], FRand Rand, int32 ForceVariant = -1);

	struct FObjLine { int32 Speaker = 0, LineId = 0, Speech = -1; };
	struct FObject
	{
		FObjLine Lines[NumSlots][NumLines];
		int32 Slot = 0x10;                    // 0x169688
		TArray<int32> Aux;
	};
	SSXTRICKYREBUILD_API void CopyToObject(const FSlot Slots[NumSlots], FObject& Obj, FRand Rand);
	SSXTRICKYREBUILD_API TArray<int32> SortedSpeech(FObject& Obj, int32 Slot, const FData& D);

	struct FSceneRacer
	{
		int32 PlayerSlot = -1;
		int32 Character = 0;
	};
	// FUN_001741D0
	inline double WrapYaw(double A) { return A - FMath::FloorToDouble(A * (0.5 / UE_DOUBLE_PI) + 0.5) * UE_DOUBLE_TWO_PI; }
	// FUN_00176720
	SSXTRICKYREBUILD_API TArray<int32> AssignActors(TConstArrayView<FSSXSceneActor> Actors, TConstArrayView<FSceneRacer> Racers);

	class SSXTRICKYREBUILD_API FScenePlayer
	{
	public:
		struct FActor
		{
			int32 Racer = -1;
			bool bActive = false;
			TArray<int32> Codes;
			int32 Index = 0;
			bool bPlace = false;
			FVector Pos = FVector::ZeroVector;
			double Yaw = 0.0;
			int32 LineId = -1;
			int32 Frames = 0;
			TArray<FIntPoint> Events;
			double Frame = 0.0;
			bool bJustStarted = false;
		};
		// 0x11CAE8
		TFunction<void(int32 Racer, int32 Value)> OnEvent;
		TFunction<void(int32 Racer, int32 LineId)> OnLineStart;
		TFunction<void(int32 Racer, const FVector& Pos, double Yaw)> OnPlace;

		void Start(TConstArrayView<FSSXSceneActor> Actors, const FSSXCameraDefSet& Defs, TConstArrayView<FSceneRacer> Racers,
			const FObject& Obj, const FData& D);
		void Stop();
		// FUN_00177020
		void Step(const FObject& Obj, const FData& D);
		bool IsRunning() const { return bRunning; }
		// 0x169BE8
		bool DeliversEventsOf(int32 Racer) const
		{
			if (!bRunning || Racer < 0) return false;
			for (const FActor& A : Actors) { if (A.bActive && A.Racer == Racer && A.LineId >= 0) return true; }
			return false;
		}

	private:
		void PlayCode(FActor& A, int32 Code, const FObject& Obj, const FData& D);
		TArray<FActor> Actors;
		bool bRunning = false;
	};
}
