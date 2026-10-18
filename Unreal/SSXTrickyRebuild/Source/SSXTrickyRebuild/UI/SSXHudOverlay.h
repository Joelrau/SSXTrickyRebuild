// FUN_001a2c40 / FUN_001a4f70
#pragma once

#include "CoreMinimal.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXHudData.h"

class FSSXFeDrawList;
class FSSXLocStrings;
struct FRacePathSet;
struct FSSXSettings;

/** Rider message queue entry {type, value, life_s, elapsed_s, extra} (race_stats+0x15C):
 *  life and elapsed are float seconds; progress = elapsed / life. Life <= 0 (multiplier) never ages. */
struct SSXTRICKYREBUILD_API FSSXHudMsg
{
	int32 Type = 0, Value = 0;
	float Total = 1.f, Elapsed = 0.f;
	int32 Extra = 0;
	float Progress() const { return Total > 0.f ? Elapsed / Total : 0.f; }
	bool IsLive() const { return Type != 0 && (Total <= 0.f || Elapsed < Total); }
};

namespace SSXHudMsg
{
	enum : int32 { Points1 = 1, Points2 = 2, Points3 = 3, Knockdown = 4, TrickAir = 7, TrickLanded = 8 /* 7 new / 8 repeated trick */, CheckpointDelta = 0xA,
		TimeBonus = 0xB, Multiplier = 0xC, Laps = 0xD, Combo = 0xE, ComboBonus = 0xF, BigAir = 0x10, Camera = 0x11 };
}

namespace SSXHudBit
{
	enum : uint32 { RaceTime = 0x1, Score = 0x8, Switch = 0x80, Strip = 0x100, Rank = 0x200, LapMsg = 0x400, Speed = 0x800,
		Boost = 0x1000, Orb = 0x2000, Chain = 0x4000, Letters = 0x8000, Points = 0x10000, TrickName = 0x20000, Demo = 0x80000,
		Multiplier = 0x100000, TimePopup = 0x400000, FinalSeconds = 0x800000, Medal = 0x1000000, PlateStyle = 0x2000000,
		ComboBonus = 0x4000000, BigAir = 0x8000000, Knockdown = 0x10000000, TimeToBeat = 0x20000000, CameraIcon = 0x40000000 };
}

struct SSXTRICKYREBUILD_API FSSXHudPlayerInput
{
	float VpX = 0.f, VpY = 0.f, VpW = 640.f, VpH = 480.f;   // already scaled to 640x480
	int32 Racer = 0;
	double BoostMeter = 0.0;
	double TrickValue = 0.0;
	double PointsMultiplier = 1.0;  // FUN_00155440
	double TrickyTimer = 0.0;
	int32 UberCount = 0;   // > 5 = infinite boost
	bool bSwitch = false;
	double Speed = 0.0;             // |velocity| cm/s
	int32 Rank = 0;
	int32 FinishResult = -1;
	double Score = 0.0;
	int32 ComboCount = 0;           // FUN_00156900
	int32 Character = 0;
	int32 UberHintIndex = 0;
	/** FUN_00164840 (rank from the rider's career rank points) >= 8, i.e. Rookie / Newbie: the uber hint's
	 * beginner test: record +4 + +8 = the race and showoff rank points of the player slot's career (0x166550). */
	bool bBeginner = false;
	int32 PadLayout = 0;
	int32 BoardClass = 0;
	bool bFlipSpin = false;
	FString TrickText;             // FUN_001551c0
	TArray<FSSXHudMsg> Messages;
};

struct SSXTRICKYREBUILD_API FSSXHudInput
{
	int32 Mode = 4;
	int32 Track = 0;
	int32 Phase = 4;
	double Clock = 0.0;   // race clock, s
	double RaceTimeSec = 0.0;       // FUN_0011ea28
	int32 RacerCount = 1;
	uint32 RaceFlags = 0;   // the race-state frame counter (bit 0x20 blinks the uber label)
	bool bMph = false;
	int32 HudDetail = 0;   // 0 full, 1 minimal, 2 none
	/** The settings the HUD reads per draw: Units 0x32CA10 (0x1A4F70: 0 km/h, else mph) and HUD detail 0x32CA40
	 *  (0x17F158). */
	void ApplySettings(const FSSXSettings& S);
	bool bModal = false;            // FUN_001a77f8
	bool bDimOverlay = false;       // FUN_001a77a8
	bool bLessonObject = false;
	int32 LessonState = -1;   // -1 = no lesson object
	bool bReplayActive = false;     // FUN_001a7be0
	bool bCardSave = false;         // FUN_001a7c58 / FUN_001a7c88
	bool bReplayFull = false;
	int32 TimeToBeatCs = 0;         // FUN_002657A8 / 0x180A10
	TArray<double> RacerDist;   // FUN_001170d0: distance to go (cm), 0 once finished
	TArray<bool> RacerComputer;
	TArray<FSSXHudPlayerInput> Players;
};

struct SSXTRICKYREBUILD_API FSSXHudMarker
{
	int32 Kind = 0;
	double Dist = 0.0;   // distance to go (cm)
};

class SSXTRICKYREBUILD_API FSSXHudOverlay
{
public:
	static constexpr int32 MaxPlayers = 2;
	static constexpr int32 MaxMarkers = 25;

	static void BuildMasks(int32 Mode, int32 Track, int32 Racers, int32 ShowoffSum, uint32& OutMinimal, uint32& OutFull, uint32& OutNone);
	// FUN_001a6860
	static int32 LayoutFor(int32 Player, int32 NumPlayers);
	// FUN_001a68f8
	static float Pop(float P, float K);
	// FUN_001a1e50 / FUN_001a1968
	static void BuildMarkers(const FRacePathSet& Paths, int32 Track, double LapLength, TArray<FSSXHudMarker>& Out);
	// FUN_001a4f70 / 0x154B18
	// 0x31E0F0 / 0x31E090
	struct FUberIcon { TCHAR Ch; uint32 Mask; float Xo, Yo, Adv; int32 Sprite; };
	static const FUberIcon* UberIcons();
	static const FUberIcon* FindUberIcon(TCHAR Ch);
	static bool UberHintActive(const FSSXHudInput& In, const FSSXHudPlayerInput& Pl);
	static float UberHintAlpha(const FSSXHudPlayerInput& Pl);
	static bool UberLabelOn(uint32 RaceFrame) { return (RaceFrame & 0x20u) != 0; }
	static int32 UberHintRecord(int32 HintIndex) { return 0x19 + HintIndex; }
	/** FUN_001546C8: the button string of a trickdef record. ActionMask(action) = the rider's mapper mask for
	 *  action 0..40 (0xFFFFFFFF = none); bFlipSpin = rider_profile stance flag (negates the spin). */
	static FString UberComboString(const SSXFeTrickBook::FEntry& E, TFunctionRef<uint32(int32)> ActionMask, bool bFlipSpin);
	// FUN_001a4f70
	static float UberLabelFrac(bool b2P) { return b2P ? 0.2f : 0.16f; }
	static float UberComboFrac(bool b2P) { return b2P ? 0.14f : 0.09f; }
	// FUN_001A6BA0
	static FString FormatTime(int32 Cs);
	/** FUN_001A6BA0 complete: exactly 50000 cs (the unfinished-human time of 0x111820) -> LOC 0x15D, negative -> LOC 0x18C,
	 *  else FormatTime. */
	FString TimeText(int32 Cs) const;
	// FUN_001A6C90
	static FString FormatGap(int32 Cs);

	void Init(const FSSXHudData* InData, const FSSXLocStrings* InLoc) { Data = InData; Loc = InLoc; }
	// FUN_001a1968
	void Activate(int32 Mode, int32 Track, int32 Racers, int32 ShowoffSum, const FRacePathSet* Paths, double LapLength);
	// vfunc_15, one 60 Hz frame.
	void Update(const FSSXHudInput& In);
	void Draw(FSSXFeDrawList& D, const FSSXHudInput& In) const;

	// FUN_001a1eb0
	uint32 MaskFor(int32 HudDetail) const { return HudDetail == 1 ? MaskMinimal : (HudDetail == 2 ? MaskNone : MaskFull); }
	/** Whole HUD hidden: vfunc_13 0x1A27D0 / vfunc_14 0x1A2A00 draw nothing while the replay overlay (slot 9) is active or while the pause / results overlay runs its memory-card save (+0x430 == 2). The pause menu itself dims the HUD (0x1A77A8, overlay +0x10) instead. */
	/** Overlay 0 is closed by the pre-race enter (0x112B88 closes every overlay) and opened again by the countdown
	 *  enter (0x11A7D0 per racer), so it is not drawn in race phases 1 (PreRace) and 2 (StartRace). */
	static bool IsHidden(const FSSXHudInput& In) { return In.bReplayActive || In.bCardSave || In.Phase == 1 || In.Phase == 2; }

	struct FPlayerState
	{
		float PointsShown = 0.f;
		float PointsPending = 0.f;
		float SegScale[14] = {};
		bool SegLit[14] = {};
		float BoostShown = 0.f;
		float PendingShown = 0.f;
		float PointsPop = 0.f;
		float PopStart = 1.f;
		float OrbW = 1.f;
		float Pulse = 1.f;
		float ChainProgress = 0.f;
		float SpeedPop = 1.f;
		bool bSpeedPopDone = false;
		int32 MedalTier = 0;
		float MedalPop = 1.f;
		int32 PointsTier = -1;         // FUN_0019e7a0
		bool bSwitch = false;
		float Bounce = 0.f;
		float Phase = 1.f;
		float RivalD[6] = {};
		float MarkerD[MaxMarkers] = {};
	};
	const FPlayerState& GetPlayer(int32 I) const { return P[I]; }
	const TArray<FSSXHudMarker>& GetMarkers() const { return Markers; }

	/** Medal thresholds HUD +0xF8 / +0xFC / +0x100 (RAM 55000 / 40000 / 25000), per track from the constructor
	 * FUN_0019e888. */
	int32 MedalGold = 55000, MedalSilver = 40000, MedalBronze = 25000;
	// FUN_002cc808 / 0x1A6FB8
	static constexpr int32 UnwrittenThreshold = (int32)0xDEADC0DEu;
	// FUN_0019e888
	static bool MedalThresholds(int32 Mode, int32 Track, int32& OutGold, int32& OutSilver, int32& OutBronze);
	static float OrbFold(float Phase);
	static float TierPop(float E, float Sx);
	static int32 Round10(float X);
	void DrawPlate(FSSXFeDrawList& D, float X, float Y, float S, float W, const FLinearColor& C) const;
	int32 ConsumeTicks() { const int32 N = Ticks; Ticks = 0; return N; }
	// FUN_00216930
	int32 ConsumeShowoffBeeps() { const int32 N = ShowoffBeeps; ShowoffBeeps = 0; return N; }

private:
	void DrawPass1(FSSXFeDrawList& D, const FSSXHudInput& In, int32 Pl, uint32 Mask, float Alpha) const;
	void DrawPass2(FSSXFeDrawList& D, const FSSXHudInput& In, int32 Pl, uint32 Mask, float Alpha) const;
	// FUN_001c0068
	void Spr(FSSXFeDrawList& D, int32 Id, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C) const;
	// FUN_001a82e8 / FUN_0019d6e0
	float Txt(FSSXFeDrawList& D, bool bMenuFont, float X, float Y, float Scale, uint32 Flags, const FLinearColor& C, const FString& S) const;
	FString L(int32 Id) const;
	const FSSXHudRec& R(int32 Layout, int32 N) const { return Data ? Data->L(Layout, N) : Zero; }

	const FSSXHudData* Data = nullptr;
	const FSSXLocStrings* Loc = nullptr;
	FSSXHudRec Zero;
	uint32 MaskMinimal = 0, MaskFull = 0, MaskNone = 0;
	TArray<FSSXHudMarker> Markers;
	FPlayerState P[MaxPlayers];
	int32 Frame = 0;
	int32 Ticks = 0;
	int32 ShowoffBeeps = 0;
	int32 ClockWhole = 0;
};
