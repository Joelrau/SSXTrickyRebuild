// 0x16A9D8, 0x16AE68, 0x3352AC, 0x3352B8, FUN_0016A998
#pragma once

#include "CoreMinimal.h"

struct FRiderProfile;

namespace SSXRider
{
	constexpr int32 NumRiders = 12;
	constexpr int32 NumBoards = 12;
	constexpr int32 NumStats = 16;
	constexpr int32 NumAttributes = 4;
	constexpr int32 NumBindBones = 19;
	constexpr float PlayerStatWeight = 0.8f;
	constexpr float BoardStatWeight = 0.2f;
	constexpr float AiLevelWeight = 0.25f;
	constexpr int32 MixMasterMikeByte64 = 12;
	constexpr double MixMasterMikeScale = 0.8;
	constexpr int32 MalloraCharacter = 7;          // 0x16AC10
	constexpr int32 MalloraOutfitTexture = 85;
	constexpr int32 MalloraBoardTexture = 144;

	// 0x331EA8
	struct FStaticRider
	{
		FString Name;
		uint8 Stats[NumStats] = {};
		uint8 Alt[NumAttributes] = {};
		uint8 Mass = 0;
		uint8 Rec34 = 0;
		float Scale = 1.0f;
		int32 Stance = 0;
		int32 TrickSet = 0;
		int32 Rec30 = 0;
		bool bHasBones = false;
		FVector Bones[NumBindBones];   // bind pose, model space (cm; Z up, nose +X, rider facing -Y)
	};

	// 0x332148
	struct FBoardRecord
	{
		FString Name;
		int32 Class = 0;
		int32 Cost = 0;
		uint8 Stats[NumAttributes] = {};
	};

	struct FBuiltProfile
	{
		uint8 Stats[NumStats] = {};
		int32 Character = 0;
		int32 Board = 0;
		int32 BoardClass = 0;
		double Scale = 1.0;
		int32 Stance = 0;
		int32 TrickSet = 0;
		int32 MassByte = 0;
		bool bComputer = false;
		int32 Byte64 = 0;
		int32 Char3C = 0;
		int32 Byte80 = 2;
		bool bMixMasterMike = false;
		bool bMallora = false;         // 0x3352B0, 0x16AC00
		int32 Rank = 9;                // 0x164840
	};

	struct FPlayerOptions
	{
		int32 Board = 0;
		bool bHaveCareer = false;
		int32 Career[NumAttributes] = { 0, 0, 0, 0 };
		int32 Stance = -1;
		bool bCheatAlt = false;
		bool bMixMasterMike = false;
		bool bMallora = false;
		bool bMode6 = false;
		int32 Rank = 9;                                // FUN_00164840
	};

	class SSXTRICKYREBUILD_API FRiderTable
	{
	public:
		static FString DefaultPath();
		static const FRiderTable& Shared();

		bool Load(const FString& Path, FString* OutError = nullptr);
		bool IsLoaded() const { return bLoaded; }

		const FStaticRider& Rider(int32 Character) const { return Riders[FMath::Clamp(Character, 0, NumRiders - 1)]; }
		const FBoardRecord& Board(int32 Character, int32 Index) const
		{
			return Boards[FMath::Clamp(Character, 0, NumRiders - 1) * NumBoards + FMath::Clamp(Index, 0, NumBoards - 1)];
		}
		TConstArrayView<int32> Group(int32 A) const { return MakeArrayView(Groups[A], GroupCount[A]); }
		// 0x16B298
		int32 AttributeBase(int32 Character, int32 A) const;
		const FVector* BindBones(int32 Character, bool bMixMasterMike) const;
		int32 FirstBoardOfClass(int32 Character, int32 BoardClass) const;

	private:
		bool bLoaded = false;
		FStaticRider Riders[NumRiders];
		FBoardRecord Boards[NumRiders * NumBoards];
		int32 Groups[NumAttributes][3] = {};
		int32 GroupCount[NumAttributes] = {};
		bool bMmmBones = false;
		FVector MmmBones[NumBindBones];
	};

	SSXTRICKYREBUILD_API FBuiltProfile BuildPlayerProfile(const FRiderTable& Table, int32 Character, const FPlayerOptions& Options);
	SSXTRICKYREBUILD_API FBuiltProfile BuildComputerProfile(const FRiderTable& Table, int32 Character, bool bHaveLevel, int32 Level, int32 Board);

	SSXTRICKYREBUILD_API void ApplyProfile(const FBuiltProfile& Built, FRiderProfile& Out);

	SSXTRICKYREBUILD_API bool ApplyNewSavePlayerProfile(FRiderProfile& Out, int32 Character, int32 BoardClass);
	SSXTRICKYREBUILD_API int32 ResolveBoard(const FRiderTable& Table, int32 Character, int32 Board, int32 BoardClass);
	SSXTRICKYREBUILD_API bool ApplySelectedPlayerProfile(FRiderProfile& Out, int32 Character, const FPlayerOptions& Options);
	SSXTRICKYREBUILD_API bool ApplyComputerProfile(FRiderProfile& Out, int32 Character, int32 Level, int32 Board);
}
