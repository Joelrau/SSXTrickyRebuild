#pragma once

#include "CoreMinimal.h"
#include "UI/SSXFeDraw.h"

// 0x33CA40
struct SSXTRICKYREBUILD_API FSSXHudRec
{
	float X = 0.f, Y = 0.f;
	uint32 Flags = 0;
	float Sx = 1.f, Sy = 1.f;
};

// 0x338008
struct SSXTRICKYREBUILD_API FSSXHudBoostSeg
{
	float Lo = 0.2f, Hi = 1.f;
	int32 Sprite = 0;
	float Dx = 0.f, Dy = 0.f;
};

// 0x338198
struct SSXTRICKYREBUILD_API FSSXHudLetter
{
	int32 Rec = 0, Dark = 0, Lit = 0;
};

class SSXTRICKYREBUILD_API FSSXHudData
{
public:
	static constexpr int32 NumLayouts = 3;
	static constexpr int32 NumRecs = 53;

	static FString DefaultDir();
	bool Load(const FString& Dir = FString());
	bool IsLoaded() const { return bLoaded; }

	const FSSXHudRec& L(int32 Layout, int32 N) const;

	// FUN_0019e888
	static void BuildBoostTable(TArray<FSSXHudBoostSeg>& Table);
	TArray<FSSXHudBoostSeg> Boost;
	TArray<FSSXHudLetter> Letters;
	FString UnitKmh, UnitMph;

	/**
	 * FUN_001551c0: the trick name sprintf("%s" x 14) over the trick record {w0, w1}. Fields index
	 * the pointer area .data 0x320970.. as the game's pointer arithmetic does; a NULL or out-of-area slot
	 * prints nothing (the game would print whatever the pointer holds: not reproduced).
	 */
	FString ComposeTrickName(uint32 W0, uint32 W1) const;
	FString Slot(uint32 Va) const;

	FSSXHudRec Layouts[NumLayouts][NumRecs];

private:
	uint32 SlotBase = 0;
	TArray<TOptional<FString>> Slots;
	bool bLoaded = false;
};

struct SSXTRICKYREBUILD_API FSSXHudSheet
{
	FSSXSpriteTable Sprites;
	TMap<FName, FString> PagePaths;
	// 0x1A6FB8
	bool Load(const FString& Json, bool bTrickSheet);
};
