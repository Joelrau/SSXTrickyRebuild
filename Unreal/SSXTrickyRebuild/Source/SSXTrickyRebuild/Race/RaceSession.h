#pragma once

#include "CoreMinimal.h"
#include "Race/AIInput.h"
#include "Race/EndRaceHandler.h"
#include "Race/FinishLineControl.h"
#include "Race/PreRaceIntro.h"
#include "Race/RacePathTracker.h"
#include "Race/RaceRandom.h"
#include "Race/RaceRules.h"
#include "Race/TriggerEffects.h"
#include "Sim/Ground/RiderPairs.h"
#include "Sim/RiderSim.h"

class FRaceTrackData;
class FRaceSession;
namespace SSXLesson { class FLessonMan; }

struct FRaceConfig
{
	ERaceEventMode Mode = ERaceEventMode::CircuitRace;
	int32 Difficulty = 0;
	int32 SingleRaceDifficulty = -1;
	uint32 RandomSeed = 1;
};

enum class ERaceCueKind : uint8
{
	CountdownTick,     // every 60 clock units of the countdown
	Go,
	TriggerEnter,
	TriggerLeave,
	Finish,
	TimedOut,
	Lap,
	Checkpoint,
	TimeBonus,         // showoff: Param seconds added to the clock
	RankChange,
	RaceEnd,
	RiderHit,          // 0x124920
	Shove,             // 0x123FC8
	Taunt,             // 0x103660, 0x234F68
	WinTaunt,          // 0x128150, 0x235298
};

struct FRaceCue
{
	ERaceCueKind Kind = ERaceCueKind::TriggerEnter;
	int32 Racer = -1;
	int32 Type = 0;
	int32 Param = 0;
	int32 Extra = 0;
	int32 Frame = 0;
	double Value = 0.0;
};

struct FRaceStanding
{
	int32 Racer = -1;
	int32 Rank = 0;
	bool bHuman = false;
	bool bFinished = false;
	int32 FinishCs = -1;
	int32 EstimatedCs = 0;
	double Remaining = 0.0;       // distance still to ride (cm)
	int32 LapsLeft = 0;
	double Score = 0.0;
};

// 0x111820, 0x113820, 0x1AF950, 0x1B0188
struct FRaceResultRow
{
	int32 Racer = -1;
	bool bHuman = false;
	int32 TimeCs = 0;           // 0x11F2A0
	double Score = 0.0;
	int32 Kind = 0;   // 0x32E6C0: relation kind toward the human; 3 = own row
	int32 LevelStart = 0;       // 0x32E6C8
	int32 Level = 0;            // 0x32E6D0, 0x11F050
	int32 PairScore = 0;        // 0x32E6D8
	// 0x1B1278, 0x1B0998
	int32 SpinDeg = 0;   // 0x32E624: sum of |spin deg| at scoring landings (shown / 180)
	int32 FlipDeg = 0;   // shown / 360
	int32 Grabs = 0;
	int32 Rails = 0;   // rail exits
	int32 Tricks = 0;   // 0x32E634: scoring landings
	int32 Wipeouts = 0;         // 0x32E640
	float AirTime = 0.f;   // 0x32E674: air seconds of the scoring landings
	float GrabTime = 0.f;       // 0x32E678
	float RailTime = 0.f;       // 0x32E67C
	float BiggestAir = 0.f;   // 0x32E680: the longest single air (s)
	float SpeedKmhSum = 0.f;    // 0x32E68C
	float SpeedFrames = 0.f;    // 0x32E690
	uint32 BestTrickW0 = 0;     // 0x32E698, 0x1B0BB8, 0x1551C0
	uint32 BestTrickW1 = 0;
};

struct FRacerRecord
{
	FRiderSim* Sim = nullptr;
	bool bHuman = false;
	int32 Index = 0;
	FRaceLineFollow Race;
	FRaceLineProgress Line;
	int32 Rank = 0;
	int32 PrevRank = 0;
	int32 Checkpoints = 0;
	double Score = 0.0;
	bool bTimedOut = false;            // 0x11DA40
	                                   // 0x156E80
	int32 CatchupRival = -1;
	double CatchupNear = 0.0;
	double CatchupFar = 0.0;
	double RivalDist[SSXRace::MaxRacers];
	double RivalBearing[SSXRace::MaxRacers];
	SSXRace::FGrudge Grudge;
	SSXRace::FRivalry Rivals[SSXRace::MaxRacers];
	SSXRace::FRivalry RivalSetup[SSXRace::MaxRacers];   // 0x16C008, 0x116AB8
	FAIState AI;
	double ResetAdvance = 0.0;
	int32 GapCs = 0;
	double RaceTotal = 0.0;
	double BestLapMark = 0.0;
	int32 EstimatedCs = 0;
	int32 Knockdowns = 0;
	double StartMeter = 0.0;
	int32 PairStamp[SSXRace::MaxRacers];
	int32 ShoveStamp[SSXRace::MaxRacers];
	int32 RiderContacts = 0;
	int32 RiderImpulses = 0;
	TArray<double> ShoveHitJ;
	TSharedPtr<IRiderRaceHooks> Hooks;
	TSharedPtr<SSXFinishLine::FControl> FinishLine;
	int32 StatsFrames = 0;            // 0x155DC8

	FRacerRecord()
	{
		for (int32 J = 0; J < SSXRace::MaxRacers; ++J) { RivalDist[J] = 1e10; RivalBearing[J] = 0.0; PairStamp[J] = MIN_int32 / 2; ShoveStamp[J] = MIN_int32 / 2; }
	}
	int32 FinishCs() const { return Sim ? Sim->State.FinishResult : -1; }
	bool IsFinished() const { return FinishCs() >= 0; }
};

struct FRaceSessionKeyframe
{
	TArray<FRacerRecord> Racers;
	TArray<int32> Results;
	TArray<FRaceCue> Cues;
	FRaceRandom Rng;
	ERacePhase Phase = ERacePhase::None;
	ERacePhase PendingPhase = ERacePhase::None;
	double Clock = 0.0;
	int32 Frame = 0;
	int32 Placements = 0;
	int32 RiderKnockdowns = 0;
	int32 IntroMusicEnds = 0;
	double SkillAhead = 1.0, SkillBehind = 1.0;
	SSXEndRace::FHandler EndRace;
	SSXIntro::FHandler Intro;
	SSXIntro::EEntry IntroEntry = SSXIntro::EEntry::Normal;
	SSXRiderHit::FRiderPairs RiderPairs;
	uint32 PendingBoxes = 0;
};

// Per 60 Hz frame the owner calls Tick() (phase change, race handler) and then every racer's Sim.Step.
// Phases: PreRace -> StartRace -> Countdown (-300..0 at 2 per frame) -> Race -> EndRace; a phase request applies at the next Tick.
class SSXTRICKYREBUILD_API FRaceSession
{
public:
	FRaceSession();
	~FRaceSession();
	FRaceSession(const FRaceSession&) = delete;
	FRaceSession& operator=(const FRaceSession&) = delete;

	/** Track data must outlive the session. */
	bool Init(const FRaceTrackData& InData, const FRaceConfig& InConfig);
	int32 AddRacer(FRiderSim* Sim, bool bHuman);
	void DetachRacer(int32 Index);
	void Start(SSXIntro::EEntry Entry = SSXIntro::EEntry::Normal);
	SSXIntro::FSlot IntroSlots[3];
	int32 GetIntroStep() const { return Intro.GetStep(); }
	TArray<FString> IntroLog;
	// FUN_00214E08
	int32 GetIntroMusicEnds() const { return IntroMusicEnds; }
	TFunction<bool()> IsSkipPressed;
	// 0x15FB08
	TFunction<int32(int32 LineId)> IntroLineKey;
	TFunction<bool()> IsHelpNeeded;
	TFunction<void()> OnDirectorReset;
	TFunction<void(const FString& Name, int32 Arg, const FString& Text)> OnIntroAction;

	SSXIntro::FSlot RaceSlots[SSXEndRace::NumSlots];
	int32 EndFocus = -1;
	// 0x16BAA8
	TFunction<int32(int32 Score)> ShowoffMedalOf;
	int32 TimeToBeatCs = MAX_int32;
	TFunction<bool()> IsLessonPassed;
	// 0x1141E0, 0x1A7BE0, 0x1A7DB8, 0x1A7E70
	TFunction<bool()> IsReplayLooping;
	TFunction<bool()> IsReplayOverlayUp;
	// 0x111108, 0x1148D0, 0x11A348, FUN_0014D930
	SSXLesson::FLessonMan* Lesson = nullptr;
	// FUN_0014D898, 0x118068, FUN_00197EF8, 0x11A840, 0x11AFE8
	void PlaceForLesson();
	// FUN_0011DA40
	void StampFinish(int32 Racer);
	const SSXEndRace::FHandler& GetEndRace() const { return EndRace; }

	void Tick();

	ERacePhase GetPhase() const { return Phase; }
	ERacePhase GetRequestedPhase() const { return PendingPhase; }
	void SetLoadedReplay(bool b) { bLoadedReplay = b; }
	ERaceEventMode GetMode() const { return Config.Mode; }
	double GetClock() const { return Clock; }
	int32 GetFrame() const { return Frame; }
	int32 NumRacers() const { return Racers.Num(); }
	const FRacerRecord& GetRacer(int32 Index) const { return Racers[Index]; }
	FRacerRecord& GetRacerMutable(int32 Index) { return Racers[Index]; }
	void GetStandings(TArray<FRaceStanding>& Out) const;
	int32 GetLapsLeft(int32 Racer) const { return Racers.IsValidIndex(Racer) ? Racers[Racer].Line.LapCount : 0; }
	double GetLapLength() const { return LapLength; }
	bool UsesGates() const;
	const TArray<int32>& GetResults() const { return Results; }
	const TArray<FRaceResultRow>& GetFinalResults() const { return FinalRows; }
	const TArray<FRaceCue>& GetCues() const { return Cues; }
	void ClearCues() { Cues.Reset(); }
	const FRacePathSet& Paths() const { return *PathSet; }
	const FRaceTrackData& Data() const { return *TrackData; }

	void NotifyKnockdown(int32 Victim, int32 Attacker);

	// FUN_00175de0
	TFunction<void(int32 Racer, const TCHAR* Script)> OnCameraScript;
	TFunction<bool()> IsIntroBusy;
	int32 GetPlacements() const { return Placements; }
	int32 RacerOf(const FRiderSim* Sim) const;

	void SaveReplay(FRaceSessionKeyframe& Out) const;
	void LoadReplay(const FRaceSessionKeyframe& In);

	void SetPhase(ERacePhase NewPhase) { PendingPhase = NewPhase; }

	void TrackScan(int32 Racer);
	bool BuildInput(int32 Racer, FRiderInputWord& InOut);
	bool OnForcedReset(int32 Racer);
	bool PlaceRespawn(int32 Racer, FVector& OutPos, FVector& OutHeading, FVector& OutVel);
	// 0x123FC8
	void RiderCollision(int32 Racer);
	// 0x126748, 0x1267A0
	void RiderSphereSetRebuilt(int32 Racer, bool bNow);
	void RiderPoseFed(int32 Racer);
	void RiderSphereSetMoved(int32 Racer, const FVector& D);
	// 0x14FE18, 0x150428, 0x1147F8
	void ReplayPairUpkeep(bool bRebuildBoxes);
	bool RacerTable(int32 Racer, IRiderRaceHooks::FRacerTable& Out) const;
	void OnTaunt(int32 Racer, int32 Rival);
	void OnWinTaunt(int32 Racer, double Strength);
	int32 GetRiderKnockdowns() const { return RiderKnockdowns; }
	void BuildAIView(int32 Racer, FAIRiderView& OutView) const;
	// 0x116AB8
	void PlaceAtStart(int32 Racer, int32 SlotRacers = -1);
	void PlaceForRace(int32 Racer, bool bGates, int32 SlotRacers = -1);

private:
	void EnterPhase(ERacePhase P);
	void ExitPhase(ERacePhase P);
	void UpdatePhase(ERacePhase P);
	void UpdateRivalGeometry();
	void RankRacers();
	void AssignCatchup(const TArray<int32>& Order);
	void ComputeResults();
	void OnTrigger(int32 Racer, const FRaceTriggerEvent& E);
	void FinishRacer(int32 Racer);
	void WritePathFields(int32 Racer);
	double RemainingDistance(const FRacerRecord& R) const;
	int32 ElapsedCs(const FRacerRecord& R) const;
	int32 CheckpointGapCs(const FRacerRecord& R) const;
	int32 EstimatedFinishCs(const FRacerRecord& R) const;
	int32 HumanRank() const;
	double SkillProb(const FRacerRecord& R) const;
	void AddCue(ERaceCueKind Kind, int32 Racer, int32 Type = 0, int32 Param = 0, int32 Extra = 0);
	void PlayCameraScript(const TCHAR* Script);
	void EnterEndRace();
	bool IsFinishDone(const FRacerRecord& R) const;
	bool IsFinishWinner(int32 Racer) const;
	SSXEndRace::FHandler EndRace;
	friend class FRaceIntroEnv;
	SSXIntro::FHandler Intro;
	SSXIntro::EEntry IntroEntry = SSXIntro::EEntry::Normal;
	int32 IntroMusicEnds = 0;

	const FRaceTrackData* TrackData = nullptr;
	const FRacePathSet* PathSet = nullptr;
	FRaceConfig Config;
	TArray<FRacerRecord> Racers;
	TArray<FAIOtherRider> Others;
	TArray<int32> Results;
	TArray<FRaceResultRow> FinalRows;
	TArray<FRaceCue> Cues;
	FRaceRandom Rng;
	ERacePhase Phase = ERacePhase::None;
	ERacePhase PendingPhase = ERacePhase::None;
	bool bLoadedReplay = false;
	double Clock = 0.0;
	int32 Frame = 0;
	int32 Placements = 0;
	int32 RiderKnockdowns = 0;
	double LapLength = 0.0;
	double SkillAhead = 1.0, SkillBehind = 1.0;
	TSharedPtr<IRiderControlState> SitAndWait;
	SSXRiderHit::FRiderPairs RiderPairs;
	uint32 PendingBoxes = 0;
	void SetRiderBox(int32 Racer);
	void FlushRiderBoxes();
};
