// 0x22A6D8 / 0x22B008 / 0x22AAA0 / 0x22ACC0
#pragma once

#include "CoreMinimal.h"

class FSSXPropSounds;
struct FSSXAmbientShape;

namespace SSXAmbient
{
	constexpr int32 MaxSlots = 40;              // 0x22D098
	constexpr float StopFade = 0.25f;           // 0x22C5A0 / 0x20C2A0 / 0x220F98
	constexpr double DeferredEnableMs = 250.0;  // 0x22E400 / 0x222CC0 / 0x21E498
	constexpr double FalloffRadiusM = 30.0;     // 0x226DD8 / 0x20AAB8

	/** 0x22BC40: falloff curve Kind (0..5) at X; false otherwise (the original returns an uninitialised register: not reproduced). */
	SSXTRICKYREBUILD_API bool Curve(int32 Kind, float X, float& Out);
	// 0x22E240
	inline bool IsHitActivated(int32 Id) { return Id == 0x10 || Id == 0x1C || Id == 0x39; }
	// 0x22C638
	inline bool IsStreamId(int32 Id) { return uint32(Id - 0x61) < 3u; }
	/** 0x22C648: swap bank file (base name) of a swap id; empty for 0x66 (rider-specific chant bank) and for ids without a case. */
	SSXTRICKYREBUILD_API FString SwapBankName(int32 Id, bool bTraffic4440 = true, bool bTraffic4444 = true);
	constexpr int32 RiderAmbienceId = 0x66;   // 0x22C648
}

/** CHANT.INF (0x21C510 -> 0x22DCE0): [COUNT] CHARCHANTBANKS / GENCHANTBANKS, [GLOBAL] <RIDER>%d / GENERAL%d (1..20) bank paths. */
struct SSXTRICKYREBUILD_API FSSXChantBanks
{
	TArray<FString> Character[12];
	TArray<FString> General;
	int32 CharCount = -1;
	int32 GenCount = -1;
	void Load(const struct FSSXInfFile& Inf);
	/** 0x22C648 case 0x66: rank < 2 own list; rank < 4 own list if (rand x 101 / 0x7FFF) < 50 else generic; else generic. Index = (rand & 0x7FFF) x count / 0x7FFF. */
	FString Pick(int32 CharacterIndex, int32 Rank, TFunctionRef<uint32()> Rand) const;
};

// 0x22BF60
struct SSXTRICKYREBUILD_API FSSXAmbientStart
{
	int32 Slot = 0;
	int32 Instance = INDEX_NONE;
	int32 SoundId = 0;
	int32 Bank = 2;          // 0x22D8A8
	int32 Index = 0;
	FString SwapBank;
	bool bChant = false;
	bool bStream = false;    // 0x2212A0
	int32 Volume127 = 0;
	bool b3D = false;
	FVector3f Position = FVector3f::ZeroVector;   // game cm
	int32 Listener = 0;
};

class SSXTRICKYREBUILD_API FSSXAmbientEmitters
{
public:
	struct FSlot
	{
		bool bActive = false;
		int32 Instance = INDEX_NONE;
		int32 Shape = 0;
		int32 SoundId = 0;
		float Volume = 0.f;
		bool b3D = false;
		FVector3f Position = FVector3f::ZeroVector;
		int32 Handle = INDEX_NONE;
		bool bTouched = false;
		bool bStream = false;
		int32 Listener = 0;
		int32 Swap = INDEX_NONE;
		FString SwapBank;
	};

	/** Crowd ids 0x61..0x63: the crowd object of the slot's listener (0x2212A0 add, 0x221000 weight, 0x220F98 remove); unset = a plain voice through Start (tests). */
	class FSSXCrowdSystem* Crowd = nullptr;
	TFunction<FString(int32 Listener)> RiderAmbienceBank;

	/** Voice side: Start returns a handle (-1 = none); IsPlaying = 0x20C200; SetVolume = volume byte (int)(vol x 127); Stop fades over the given seconds. */
	TFunction<int32(const FSSXAmbientStart&)> Start;
	TFunction<bool(int32 Handle)> IsPlaying;
	TFunction<void(int32 Handle, int32 Volume127)> SetVolume;
	TFunction<void(int32 Handle, float Fade)> Stop;
	// TODO: the async load time).
	TFunction<bool(const FString& SwapBank)> SwapReady;

	// 0x22A6D8
	void Build(FSSXPropSounds* InTable);
	void Reset();
	bool IsBuilt() const { return Table != nullptr; }

	// 0x22B370
	void Query(const FVector3f& P, int32 Listener = 0);   // P = the listener position (game cm)
	// 0x22D370
	void Update();
	// 0x22D408
	void StopAll();
	// 0x22E338 / 0x22E360
	void RaceRestart();
	// 0x22E400
	void SetEnabled(bool bOn, bool bDeferred);
	void AdvanceMs(double Ms);
	bool IsEnabled() const { return bEnabled; }
	bool CarryEnabled() const { return bEnabled || PendingEnables.Num() > 0; }
	// 0x22E400
	void RestoreEnabled(bool bOn) { bEnabled = bOn; }
	int32 NumEnableCalls = 0;

	// 0x22B008
	int32 CellIndex(float X, float Y) const;
	const TArray<int32>& CellRecords(int32 Cell) const;
	const FSlot& GetSlot(int32 I) const { return Slots[I]; }
	int32 NumActive() const;
	// 0x22AAA0
	static void RecordBounds(const struct FSSXAmbientRecord& R, FVector3f& OutMin, FVector3f& OutMax);
	static void ShapeBounds(const FSSXAmbientShape& S, FVector3f& OutMin, FVector3f& OutMax);

private:
	void Register(float Vol, int32 Instance, int32 Shape, int32 Listener, int32 SoundId, bool b3D, const FVector3f& Pos);
	void UpdateSlot(int32 I);
	void StopSlot(FSlot& S);
	static void ClearSlot(FSlot& S);

	FSSXPropSounds* Table = nullptr;
	TArray<TArray<int32>> Cells;
	FSlot Slots[SSXAmbient::MaxSlots];
	bool bEnabled = true;       // 0x22D098
	double ClockMs = 0.0;
	TArray<double> PendingEnables;
};
