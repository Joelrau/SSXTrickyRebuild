// 0x22E6A8 / 0x2BBA80 / 0x228998
#pragma once

#include "CoreMinimal.h"

class FRaceSession;
class FSSXSpeechSystem;
struct FRaceCue;

class SSXTRICKYREBUILD_API FSSXAnnouncer
{
public:
	using FRandA = TFunctionRef<uint32()>;

	/** Race load: counters reset. Mode = DAT_0032DE8C, Venue = track id (DAT_0032DE88), HeatWord = RS+0x330 low half (0x1635B0). */
	void Reset(int32 InMode, int32 InVenue, int32 InHeatWord = 3);
	void Frame(FSSXSpeechSystem& Speech, const FRaceSession& Session, int32 Human, FRandA RandA);
	void Cue(FSSXSpeechSystem& Speech, const FRaceSession& Session, const FRaceCue& C, FRandA RandA);
	// FUN_00215288 / 0x2334B0 / 0x2151C8
	void Go(FSSXSpeechSystem& Speech, FRandA RandA);
	// FUN_002340B8 / FUN_00234308
	void TrackBoostOp(FSSXSpeechSystem& Speech, int32 Op);
	/** Tricky activation (0x21C610 -> 0x2337C8): MC 0x204C, class 19 gate, not mode 6. */
	void TrickyActivation(FSSXSpeechSystem& Speech, FRandA RandA);
	// 0x157330 / 0x233220
	void Message(FSSXSpeechSystem& Speech, int32 Type, int32 Value, bool bFinished, FRandA RandA);
	/** 0x156618 for the focus player at a new trick segment: snd+0x6E04 = 1, snd+0x6E00 = 0. */
	void SegmentStart();
	/** Trick announcement (0x157088): bRepeat outside mode 6 -> 0x2333E0 (MC 0x201A, class 4), else 0x232498 with the record words and the trick's points (0x155440). */
	void TrickAnnounce(FSSXSpeechSystem& Speech, uint32 W0, uint32 W1, int32 Points, bool bRepeat, bool bFinished, FRandA RandA);
	/** Bail of the focus player (0x2181E8 -> 0x233D38): class 8 -> MC 0x202E {rank word}; class 16 and modes 3 / 5 -> MC 0x2054 {2}. RankIndex = profile +0x74. */
	void Bail(FSSXSpeechSystem& Speech, bool bFinished, int32 RankIndex, FRandA RandA);
	// 0x2197D8 / 0x233E78
	void BoostEmpty(FSSXSpeechSystem& Speech, bool bFinished, FRandA RandA);
	/** A human's forced reset (0x218EB0 -> 0x233A18): unfinished, not replay, mode != 6, focus player, breakdown zone active (snd+0x6E50 != 0), class 22 -> MC 0x2052. */
	void ForcedReset(FSSXSpeechSystem& Speech, bool bFinished, bool bFocus, int32 MusicZone, FRandA RandA);
	/** Focus player knocked down (0x124920 -> 0x233858): class 20 -> MC 0x2050 {relation bit} (victim toward attacker: 0 -> 1, 1 -> 4, 2 -> 2). */
	void KnockedDown(FSSXSpeechSystem& Speech, bool bAttackerFinished, int32 RelationKind, FRandA RandA);
	// 0x123FC8 / 0x233CB8
	void ShoveHit(FSSXSpeechSystem& Speech, bool bFinished, FRandA RandA);
	// 0x11ECF8 / 0x2336B8
	void Grudge(FSSXSpeechSystem& Speech, bool bAIFinished, FRandA RandA);

	/** Staging narration (director ops 0x29 / 0x2A / 0x2B -> 0x2310C8, kind 0 / 2 / 1; first WC heat, mode 4, heat word 0): NARR 0x8006 {venue bit | 0x400, rider set, character bit, language bit}, channel 0 serviced at once. Returns the request (empty when not posted). */
	TArray<uint32> StagingNarration(FSSXSpeechSystem& Speech, int32 Kind, TFunctionRef<int32(int32 CharA, int32 CharB)> Relation);
	/** Pre-race card (0x231FF8; modes 2 / 4): when channel 0 is busy NARR 0x8015 {2} first, then NARR 0x8017 {field word, language} (Strong = count > 80, Weak = count <= 20). Returns the field word, -1 none. */
	int32 PreRaceCard(FSSXSpeechSystem& Speech, bool bChannel0Free, int32 Strong, int32 Weak, int32 CareerPlayers);
	static uint32 FieldWord(int32 Strong, int32 Weak);

	/** Race-end narration inputs: results tables (0x32DB6C, finish order 0x32E5F4, race_stats copy 0x32E624 + racer * 0xBC) and pending records 0x32ECA0. */
	struct FRaceEndInfo
	{
		struct FRacer
		{
			int32 Slot = -1;
			int32 Character = 0;
			int32 Respawns = 0;
			int32 WipeOuts = 0;
			int32 Knockdowns = 0;
			double Frames = 0.0;        // 0x155DC8
			int32 TowardHuman = 0;      // 0x111820
		};
		TArray<FRacer> Racers;
		TArray<int32> Order;
		TArray<int32> PendingSlots;     // 0x32ECA0
		int32 HeatWord = 3;             // 0x1635B0
		int32 ShowoffMedal0 = 3;        // 0x16BAA8
		bool bReplayState = false;      // 0x21BFD0
	};
	/** 0x231548: once per race (snd+0x7DCC bit 1), not in replay: modes 2 / 4 -> 0x231800, 3 / 5 -> 0x231AF8. Returns true when it posted. */
	bool RaceEndNarration(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info);
	/** 0x231800: NARR 0x8007 {place word, record word, language, performance, 1}; WC final: 0x8009 {medal word, 2, language,
	 *  performance, 1}. */
	bool RaceEndRace(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info);
	// 0x231AF8
	bool RaceEndShowoff(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info);
	/** 0x2315E0: performance mask of the racers riding in player slot Slot (per-venue frame thresholds, respawns, wipe-outs, knock-downs). */
	uint32 PerformanceMask(const FRaceEndInfo& Info, int32 Slot) const;
	// 0x1AF720 / 0x231C50
	void ResultsOpen(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info, bool bChannel0Free);
	/** cHiScoreRecordOverlay row of the new record (0x1B81B8 -> 0x232240): NARR 0x800B {mode word, row word, language} when 0x231548 did not post. */
	void HiScoreRow(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info, int32 Row);
	/** cUnlockedOverlay open (0x1B5EF8 -> 0x232330): NARR 0x800D {venue bit, language} per venue / course unlock. */
	void UnlockedOpen(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info, TConstArrayView<TPair<int32, int32>> Unlocks);
	/** Trick Tutorial narration FUN_00231398: not in replay, NARR 0x800F {step word, language, 1} (word 0 when a once-per-race step already ran) and channel 0 serviced at once (0x228BD8). */
	TArray<uint32> LessonNarration(FSSXSpeechSystem& Speech, int32 Step, bool bReplayState = false);
	/** cPointDistributionOverlay activate (0x1BA208) with rank-up flag DAT_0032EA9C == 1 -> 0x2314E0: NARR 0x8010 {table 0x3A2750[rank], language}, channel 0 serviced at once; no replay test. Deactivate (0x1BA2F0) stops channel 0 (0x228D10). */
	TArray<uint32> RankUpNarration(FSSXSpeechSystem& Speech, int32 Rank);
	/** snd+0x7DCC: bit 1 race-end narration done, bit 0x10 results-open narration done, 2 / 4 / 8 lesson steps 0..2 (cleared at race load). */
	uint32 EndFlags = 0;
	static uint32 NarrVenueBit(int32 Venue);       // 0x230910
	static uint32 NarrCharacterBit(int32 Character);   // 0x2307F0 / 0x230880
	// 0x2309A8
	int32 Language = 0;
	int32 PlayerCharacter = -1;
	int32 PlayerVoice = -1;

	static uint32 MessageBit(int32 Type, int32 Value);
	// 0x233220
	static uint32 ComboWord(int32 Value);
	/** GO requests 0x204A {mode word, venue bit, race type} then 0x201F {mode word, race type, rank word} or, when the rank word (0x233138) has bit 2, 0x2020 {mode word, venue bit, race type}. */
	static TArray<TArray<uint32>> GoRequests(int32 Mode, int32 Venue, int32 HeatWord = 3);
	// 0x233138
	static uint32 GoRankWord(int32 HeatWord, int32 Venue, int32 Mode);
	// 0x233AB0
	static uint32 RankChangeWord(int32 OldRank, int32 NewRank);
	// 0x233B70
	static uint32 RankWord(int32 ProfileRank);
	/** 0x232498 body: the eight words of MC 0x200F from the trick record and the 0x2010 page of byte 2; false when the record names nothing. */
	static bool TrickWords(uint32 W0, uint32 W1, uint32 OutWords[8], int32& OutFollowPage);
	static int32 GrabPage(uint8 B);
	static int32 FlipPage(uint8 B);
	// 0x3A38E8
	static int32 TrickClass(int32 Points);

	int32 Mode = 0;
	int32 Venue = 0;
	int32 HeatWord = 3;
	/** Replay playback (0x21BFD0, set every race frame): every MC helper 0x231398..0x234610 tests it and posts nothing; the meter-full hint 0x2191C0 is not queued and the checkpoint count 0x233EE0 does not advance. */
	bool bReplay = false;
	/** Class-gate level min(snd+0x6E68, int(snd+0x383C)), set from the Volume3 option (SSXSettings::AnnouncerLevel); 0 at start-up. */
	int32 GateLevel = 0;
	// 0x164840
	int32 PlayerRankIndex = 0;
	int32 Checkpoints[4] = {};
	int32 MeterHintFrames = -1;      // 0x2191C0
	int32 LastNearFullCount = -1;
	TArray<int32> LastInfiniteCalls;
	double LastClock = 0.0;
	bool bTimeOverPosted = false;
	int32 LastPhase = -1;
	int32 TrickGate = 1;
	int32 TrickNamed = 0;
	/** Lead taken (0x115100 -> 0x219A48): zbxsfx #109 when the new rank is 0 and the rider is human or the focus player, held 200 units (ring slot 0). */
	bool LeadTaken(const FRaceSession& Session, const FRaceCue& C);
	int32 LeadSoundFrame = -1000000;
	TArray<uint32> Posted;

private:
	bool Gate(int32 Class, FRandA RandA) const;
	bool Post(FSSXSpeechSystem& Speech, std::initializer_list<uint32> Words);
	bool PostWords(FSSXSpeechSystem& Speech, const uint32* Words, int32 N);
};
