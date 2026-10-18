// FUN_00157490, FUN_00157198, FUN_001551c0, FUN_00156618, FUN_0019e7a0, 0x320CF0, 0x320F40, 0x338120
#pragma once

#include "CoreMinimal.h"

namespace SSXTrick
{
	constexpr int32 GridRows = 21, GridCols = 7, NumTiers = 6;
	constexpr uint8 Unnameable = 0x2C;
	constexpr uint8 ComboGrab = 0x65;

	struct SSXTRICKYREBUILD_API FPointsTier
	{
		float Min = 0.f, A = 1.f, R = 1.f, G = 1.f, B = 1.f;
	};

	class SSXTRICKYREBUILD_API FTrickTables
	{
	public:
		static const FTrickTables& Get();
		bool Load(const FString& Dir);
		bool IsLoaded() const { return bGrids && bNames; }

		uint8 Grid[2][GridRows][GridCols] = {};
		FPointsTier Tiers[NumTiers];
		bool bGrids = false, bNames = false;

		// 0x320970
		FString Compose(uint32 W0, uint32 W1) const;
		int32 PointsTier(float Points) const;

	private:
		uint32 SlotBase = 0;
		TArray<TOptional<FString>> Slots;
	};

	struct FRecordInput
	{
		int32 SpinDeg = 0;        // +0x30 (negated when bSwitch)
		int32 FlipDeg = 0;        // +0x34 (negated when SegType == 4)
		bool bSwitch = false;
		int32 SegType = 0;
		bool bLate = false;
		bool bAltFlag = false;    // TODO: meaning
		int32 Grabs[3] = {};
		bool bToRail = false;
		bool bLandSwitch = false;
		bool bAltSelect = false;
	};

	struct FRecord
	{
		uint32 W0 = 0, W1 = 0;
		bool bValid = false;
	};

	SSXTRICKYREBUILD_API int32 Round180(int32 Deg);
	SSXTRICKYREBUILD_API FRecord BuildTrickRecord(const FRecordInput& In, const FTrickTables& T = FTrickTables::Get());
	SSXTRICKYREBUILD_API FRecord RailTrickRecord(int32 RailKind, int32 RailSpinDeg);
}
