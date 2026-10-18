// 0x32B390, 0x16C420, 0x166A58, 0x164398, 0x1644E8
#pragma once

#include "CoreMinimal.h"
#include "Progression/SSXHeats.h"

namespace SSXProg
{
	constexpr int32 NumRiders = 12;
	constexpr int32 NumTracks = 13;
	constexpr int32 NumAttributes = 4;
	constexpr int32 NumNames = 4;
	constexpr int32 NumCircuit = 8;
	constexpr int32 Gold = 0, Silver = 1, Bronze = 2, NoMedal = 3;
	constexpr int32 TrackPipedream = 6, TrackUntracked = 7, TrackTutorial = 10, NoTrack = 13;
	constexpr int32 UberBoard = 11, ChromeOutfit = 6;
	constexpr int32 TrickBookChapters = 6, TricksPerChapter = 5;
	constexpr uint32 NewSaveVenueMask = 0x407;   // 0x161B40
	constexpr int32 NewSaveRiders = 4;
	constexpr int32 BlobSize = 0x27D4;
	constexpr int32 BlobChecksum = 0x27CC;
	constexpr int32 BlobMagicOffset = 0x27D0;
	constexpr uint32 BlobMagic = 0x12300003;
	constexpr uint16 CrcInit = 0xFBEA;
	constexpr int32 MaxPendingRecords = 5;       // 0x32ECA0
	constexpr int32 MaxNameChars = 9;

	enum EMode : int32 { Freeride = 1, SERace = 2, SEShowoff = 3, WCRace = 4, WCShowoff = 5, Tutorial = 6, TimeChallenge = 7 };
	enum EUnlock : int32 { UShowoffVenue = 0, URaceVenue, UCourse, URider, UBoard, UOutfit, URank, UMedal, UBookDone, UBookEntries };

	// 0x3352F0, 0x335370
	SSXTRICKYREBUILD_API int32 CircuitVenue(int32 Index);
	SSXTRICKYREBUILD_API int32 CircuitIndex(int32 Venue);
	SSXTRICKYREBUILD_API bool ShowoffThresholds(int32 Venue, int32 Out[3]);
	SSXTRICKYREBUILD_API int32 MedalRankPoints(int32 Medal);   // 0x37EE40
	SSXTRICKYREBUILD_API int32 RankThreshold(int32 Rank);
	SSXTRICKYREBUILD_API int32 RankFromPoints(int32 RankPoints);   // FUN_00164840
	// 0x164840, FUN_00112AF0
	SSXTRICKYREBUILD_API bool HelpRaceNeeded(int32 RankPoints);
	SSXTRICKYREBUILD_API int32 RiderUnlockOrder(int32 K);      // 0x3352C0
	SSXTRICKYREBUILD_API int32 BoardCost(int32 Board);         // 0x332154
	SSXTRICKYREBUILD_API int32 Scaled(int32 V);
	inline int32 RaceMedal(int32 Place) { return Place >= 0 && Place < 3 ? Place : NoMedal; }
	SSXTRICKYREBUILD_API int32 ShowoffMedal(int32 Score, int32 Venue);
	// 0x2CE04C
	SSXTRICKYREBUILD_API uint16 Crc16(const uint8* Data, int32 Num, uint16 Init = CrcInit);
}

// 0x335400, 0x3353F8
struct SSXTRICKYREBUILD_API FSSXUnlock
{
	int32 Player = 0;
	int32 Type = 0;   // SSXProg::EUnlock (7 = medal record, never triggers the screen)
	int32 Value = 0;
	bool operator==(const FSSXUnlock&) const = default;
};

struct SSXTRICKYREBUILD_API FSSXTrackState
{
	uint8 ShowoffMedal = SSXProg::NoMedal;
	uint8 RaceMedal = SSXProg::NoMedal;
	uint8 RaceOpen = 0;
	uint8 ShowoffOpen = 0;
	bool operator==(const FSSXTrackState&) const = default;
};

struct SSXTRICKYREBUILD_API FSSXCareerBest
{
	uint32 Value = 0;
	int32 Track = SSXProg::NoTrack;
	bool operator==(const FSSXCareerBest&) const = default;
};

// 0x32B490
struct SSXTRICKYREBUILD_API FSSXCareer
{
	int32 RawPoints = 0;
	int32 Points[2] = { 0, 0 };
	uint8 Attr[4] = { 0, 0, 0, 0 };
	uint8 StatPoints[4] = { 0, 0, 0, 0 };
	int32 BookChar = 0;
	uint32 BookDone[2] = { 0, 0 };
	int32 BookComplete = 0;
	FSSXTrackState Tracks[SSXProg::NumTracks];
	uint32 Boards = 0;
	uint32 Outfits = 1;
	FSSXCareerBest Bests[7];
	int32 EventCount[SSXProg::NumTracks] = {};
	FSSXCareerBest TrickColour[6];
	int32 BestTime[SSXProg::NumTracks];   // best WC race time (cs, -1 none)
	int32 BestScore[SSXProg::NumTracks];   // best WC showoff score (-1 none)
	int32 Opponent[SSXProg::NumRiders][2] = {};
	                                             // 0x166600, 0x167DA0, 0x16C328

	FSSXCareer();
	int32 RankPoints() const { return Points[0] + Points[1]; }
	uint32 TrickBits() const { return BookDone[0] | BookDone[1]; }
	bool TrickDone(int32 K) const { return ((TrickBits() >> K) & 1) != 0; }
	bool ChapterDone(int32 Ch) const { return ((TrickBits() >> (Ch * SSXProg::TricksPerChapter)) & 0x1F) == 0x1F; }
	bool operator==(const FSSXCareer&) const;
};

// 0x32C9E8, 0x32CA50
struct SSXTRICKYREBUILD_API FSSXSettings
{
	// 0x211DB0, 0x211F20, 0x161928, 0x161A98, 0x20DB18, FUN_0021BCA8, 0x212210
	float Master = 10.f / 11.f;
	float Music = 10.f / 11.f;       // 0x32C9EC
	float McSpeech = 10.f / 11.f;    // 0x32C9F0
	// 0x32C9F4, 0x235D58
	float Volume3 = 10.f / 11.f;
	float CharSpeech = 10.f / 11.f;  // 0x32C9F8
	float Sfx = 10.f / 11.f;         // 0x32C9FC
	int32 Speaker = 2;           // 0x32CA00
	// 0x32CA04
	int32 ArcadeSfx = 1;
	int32 Vibration[2] = { 1, 1 };   // 0x32CA08 / 0x32CA0C: pad motor enable per port
	int32 Units = 1;   // 0x32CA10: 0 km/h, 1 mph
	int32 Widescreen = 0;        // 0x32CA14, 0x1DD450
	// 0x32CA18, 0x17F1A0, 0x180350
	int32 SplitScreen = 0;
	int32 Controller[4] = { 0, 0, 0, 0 };   // 0x32CA1C: per port 0 Default / 1 Pro (BTNMAP0 / 1)
	int32 Word169C = 0;          // 0x32CA2C
	int32 Word16A0 = 0;          // 0x32CA30
	int32 Autoload = 1;          // 0x32CA34
	int32 Camera1P = 1;          // 0x32CA38, 0x180030, 0x173EB0
	int32 Camera2P = 1;          // 0x32CA3C
	int32 HudDetail = 0;   // 0x32CA40: 0 full, 1 minimal, 2 none
	float Brightness = 0.5f;     // 0x32CA44, 0x17B980
	int32 MaxBoarders = 6;   // 0x32CA48: 2..6
	int32 Replay = 1;            // 0x32CA4C
	int32 Language = 5;          // 0x17C150
	int32 Preset = 0;
	bool operator==(const FSSXSettings&) const = default;
};

struct SSXTRICKYREBUILD_API FSSXRecordRow
{
	uint8 Name[12] = {};
	int32 Char = 0;
	int32 Value = 0;
	FString GetName() const;
	void SetName(const FString& S);
	bool operator==(const FSSXRecordRow&) const = default;
};

struct SSXTRICKYREBUILD_API FSSXRecordTable
{
	FSSXRecordRow Rows[5];
	int32 Count = 5;
	int32 HigherBetter = 0;
	int32 IsTime = 1;
	// 0x166968
	int32 Insert(int32 Value, const FString& Name, int32 Char);
	bool operator==(const FSSXRecordTable&) const = default;
};

struct SSXTRICKYREBUILD_API FSSXTrackRecords
{
	FSSXRecordTable Tables[3];
	bool operator==(const FSSXTrackRecords&) const = default;
};

struct SSXTRICKYREBUILD_API FSSXSaveData
{
	uint32 VenueUnlocks = SSXProg::NewSaveVenueMask;
	uint32 RiderUnlocks = 0;
	uint8 RiderConfig[SSXProg::NumRiders][0x14] = {};  // 0x165660
	FSSXCareer Career[SSXProg::NumRiders];
	uint8 Names[SSXProg::NumNames][10] = {};
	FSSXSettings Settings;
	FSSXTrackRecords Records[SSXProg::NumTracks];

	// 0x161B40, 0x166600, 0x1668F0
	static FSSXSaveData NewSave();
	FString GetUserName(int32 S) const;
	void SetUserName(int32 S, const FString& Name);

	// 0x291470
	TArray<uint8> ToBlob() const;
	// 0x2912E8
	bool FromBlob(const TArray<uint8>& Blob, bool bVerify = true);
	static uint16 BlobCrc(const TArray<uint8>& Blob);
};

struct SSXTRICKYREBUILD_API FSSXPointSession
{
	int32 Char = -1;
	int32 RawAtOpen = 0;
	int32 Left = 0;
	int32 Initial = 0;
	int32 Cur[4] = {};
	int32 Max[4] = {};
	int32 Floor[4] = {};
};

// 0x32ECA0
struct SSXTRICKYREBUILD_API FSSXPendingRecord
{
	int32 Player = 0;
	int32 Kind = 0;
	int32 Track = 0;
	int32 Row = 0;
};

// 0x164FC0
struct SSXTRICKYREBUILD_API FSSXEventHuman
{
	int32 Player = 0;
	int32 Char = 0;
	int32 Place = 0;
	int32 TimeCs = 50000;   // 0x111820: race time in centiseconds; 50000 = did not finish
	int32 Score = 0;
	FString Name;
	uint32 EventTricks = 0;
};

struct SSXTRICKYREBUILD_API FSSXEventResult
{
	int32 Mode = SSXProg::SERace;
	int32 Venue = 0;
	TArray<FSSXEventHuman> Humans;
};

// 0x111820, FUN_0024A188, FUN_0024BBD0, 0x166CB0
struct SSXTRICKYREBUILD_API FSSXRaceStatBlock
{
	int32 Tricks = 0;      // 0x32E634
	int32 Ubers = 0;       // 0x32E638
	int32 Wipeouts = 0;    // 0x32E640
	int32 Knockdowns = 0;  // 0x32E66C
	float AirBest = 0.f;   // 0x32E680
	float GrabBest = 0.f;  // 0x32E684
	float RailBest = 0.f;  // 0x32E688
	int32 Colour[6] = {};
};

struct SSXTRICKYREBUILD_API FSSXProgressSession
{
	FSSXRaceStatBlock RaceStats[2];     // FUN_00166598
	TArray<FSSXUnlock> Unlocks;         // 0x335400, 0x3353F8
	bool bRaceCircuitBeaten = false;    // 0x3353F4
	bool bShowoffCircuitBeaten = false; // 0x3353F0
	bool bRankUp = false;
	bool bQuit = false;
	bool bNextVenue = false;
	bool bUnlocksAdded = false;
	int32 RankBefore = 9, RankAfter = 9;
	int32 Heat = 0;
	float HeatFactor = 0.f;
	                                    // 0x16CFD8, 0x16D0B0
	SSXHeats::FElimination Elim;        // 0x335658
	int32 Carried[SSXHeats::NumRiders] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	bool bNextHeatEntry = false;
	int32 LastMedal = SSXProg::NoMedal;
	FSSXPointSession Points[2];
	TArray<FSSXPendingRecord> PendingRecords;

	// 0x16BBA0, 0x1635B8, 0x278840, 0x263670, 0x167060
	void ClearCircuitContext() { Heat = 0; HeatFactor = 0.f; }
};

namespace SSXProg
{
	SSXTRICKYREBUILD_API int32 AttributeCap(int32 Char, int32 A);
	SSXTRICKYREBUILD_API int32 AttributeBase(int32 Char, int32 A);
	SSXTRICKYREBUILD_API int32 StatBarMax(int32 Char, int32 A);

	// 0x166600
	SSXTRICKYREBUILD_API void NewCareer(FSSXCareer& C, int32 Char);

	// 0x163BB8, 0x379FD0
	SSXTRICKYREBUILD_API bool VenueUnlocked(const FSSXSaveData& S, int32 Mode, int32 Venue, int32 Slot0Char, uint32 ModeMask, bool bUnlockAll);

	// 0x16BCB0, 0x16C420
	SSXTRICKYREBUILD_API TArray<FSSXUnlock> AwardEvent(FSSXSaveData& S, FSSXProgressSession& Sess, int32 Char, int32 Mode, int32 Venue,
		int32 Heat, int32 Place, int32 Score, int32 Player = 0, uint32 EventTricks = 0);

	// 0x16D0B0, 0x163AB0
	SSXTRICKYREBUILD_API int32 AdvanceHeat(FSSXProgressSession& Sess, int32 PlayerPlace);
	// 0x16C3B0, 0x16C328
	SSXTRICKYREBUILD_API int32 CircuitHeatEnd(FSSXProgressSession& Sess, const TArray<int32>& FinishRiders, int32 PlayerRider, int32 Mode,
		FSSXCareer* Career = nullptr);

	// 0x166CB0, 0x111820
	SSXTRICKYREBUILD_API void CareerBests(FSSXCareer& C, const FSSXRaceStatBlock& B, int32 Track);
	SSXTRICKYREBUILD_API bool CareerBestsMode(int32 Mode);
	// 0x167010, 0x112B88
	SSXTRICKYREBUILD_API void CountEvent(FSSXCareer& C, int32 Track);

	// 0x164FC0
	SSXTRICKYREBUILD_API void EndEvent(FSSXSaveData& S, FSSXProgressSession& Sess, const FSSXEventResult& R, bool bMaxAttributes);

	// 0x166A58
	SSXTRICKYREBUILD_API void BuildPointSession(const FSSXSaveData& S, int32 Char, bool bMaxAttributes, FSSXPointSession& Out);
	// 0x164398, 0x1644E8
	SSXTRICKYREBUILD_API bool PointAdd(FSSXSaveData& S, FSSXPointSession& P, int32 A);
	SSXTRICKYREBUILD_API bool PointRemove(FSSXSaveData& S, FSSXPointSession& P, int32 A);

	SSXTRICKYREBUILD_API bool LoadRecordDefaults(const FString& Path);
	SSXTRICKYREBUILD_API bool RecordDefaultsLoaded();
	SSXTRICKYREBUILD_API FString RecordDefaultsPath();
	// 0x1668F0, 0x161B40
	SSXTRICKYREBUILD_API void DefaultRecords(FSSXTrackRecords& Out, int32 Track);

	// 0x17B980
	inline float BrightnessScale(const FSSXSettings& S) { return FMath::Clamp(S.Brightness + 0.5f, 0.f, 2.f); }
}
