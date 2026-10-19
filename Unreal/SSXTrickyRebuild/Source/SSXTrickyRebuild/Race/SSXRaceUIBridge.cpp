#include "Race/SSXRaceUIBridge.h"

#include "Audio/SSXAudioSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "Progression/SSXProgression.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Air/TrickName.h"
#include "Progression/SSXSettingsEffects.h"
#include "Race/SSXRaceManager.h"
#include "Audio/SSXCrowdVoices.h"
#include "Camera/ReplayCamera.h"
#include "Effects/SSXTrackEffects.h"
#include "SnowboarderPawn.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndSubsystem.h"

namespace
{
	// 0x111820
	FSSXRaceStatBlock RaceStatBlock(const FRacerRecord& Rec)
	{
		FSSXRaceStatBlock B;
		if (const SSXAir::FAirContext* Air = Rec.Sim ? SSXAir::GetAirContext(*Rec.Sim) : nullptr)
		{
			B.Tricks = Air->Score.ScoredTricks;
			B.Ubers = Air->Score.UberCount;
			B.Wipeouts = Air->Score.WipeOutCount;
			B.Knockdowns = Air->Score.KnockdownCount;
			B.AirBest = float(Air->Score.AirSecondsBest);
			B.GrabBest = float(Air->Score.GrabSecondsBest);
			B.RailBest = float(Air->Score.RailSecondsBest);
			for (int32 K = 0; K < 6; ++K) { B.Colour[K] = Air->Score.TierCounts[K]; }
		}
		return B;
	}
}

int32 FSSXRaceUIBridge::GetRacePhase() const
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	return S ? int32(S->GetPhase()) : 0;
}

int32 FSSXRaceUIBridge::GetEventMode() const
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	return S ? int32(S->GetMode()) : (Owner ? Owner->EventMode : 4);
}

int32 FSSXRaceUIBridge::GetTrackId() const
{
	return Owner ? Owner->GetTrackData().GameTrack : 0;
}

double FSSXRaceUIBridge::GetCountdownTimer() const
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	return S ? S->GetClock() : 0.0;
}

bool FSSXRaceUIBridge::IsPostRaceReady() const
{
	// 0x1141E0, 0x1A7848, 0x193C68
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	return S && bPostRaceChain;
}

bool FSSXRaceUIBridge::GetResults(TArray<FSSXRacerResult>& Out) const
{
	Out.Reset();
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	if (!S)
	{
		return false;
	}
	const bool bScore = SSXRace::IsScoreMode(S->GetMode());
	const USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(Owner);
	if (S->GetFinalResults().Num() > 0)
	{
		// 0x111820, 0x1AF950, 0x1B0188
		const TArray<FRaceResultRow>& Table = S->GetFinalResults();
		for (int32 K = 0; K < Table.Num(); ++K)
		{
			const FRaceResultRow& Row = Table[K];
			const ASnowboarderPawn* P = Owner->GetPawn(Row.Racer);
			FSSXRacerResult R;
			R.Char = P ? P->Character : 0;
			R.Name = SSXFeTables::RiderName(R.Char);
			R.Player = Row.bHuman ? 0 : -1;
			if (Row.bHuman && Sub && !Sub->Profile.Slot[0].UserName.IsEmpty()) { R.Name = Sub->Profile.Slot[0].UserName; }
			R.Place = K + 1;
			R.TimeSec = bScore ? -1.0 : Row.TimeCs / 100.0;   // FUN_001A6BA0
			R.Score = (int32)Row.Score;
			if (S->GetMode() == ERaceEventMode::CircuitRace && Sub && Sub->Profile.Session.Heat == 2 && K < 3) { R.Medal = K; }
			if (!Row.bHuman)
			{
				R.Aggression = Row.LevelStart;   // 0x32E6C8
				R.AggressionTo = Row.Level;      // 0x32E6D0
			}
			Out.Add(R);
		}
		return Out.Num() > 0;
	}
	TArray<FRaceStanding> Rows;
	S->GetStandings(Rows);
	for (const FRaceStanding& Row : Rows)
	{
		const ASnowboarderPawn* P = Owner->GetPawn(Row.Racer);
		FSSXRacerResult R;
		R.Char = P ? P->Character : 0;
		R.Name = SSXFeTables::RiderName(R.Char);
		R.Player = Row.bHuman ? 0 : -1;
		if (Row.bHuman && Sub && !Sub->Profile.Slot[0].UserName.IsEmpty()) { R.Name = Sub->Profile.Slot[0].UserName; }
		R.Place = Row.Rank + 1;
		R.TimeSec = bScore ? -1.0 : (Row.bFinished ? Row.FinishCs / 100.0 : (Row.EstimatedCs > 0 ? Row.EstimatedCs / 100.0 : -1.0));
		R.Score = (int32)Row.Score;
		// 0x390828
		if (S->GetMode() == ERaceEventMode::CircuitRace && Sub && Sub->Profile.Session.Heat == 2 && Row.Rank < 3) { R.Medal = Row.Rank; }
		// FUN_001A91C8
		if (!Row.bHuman)
		{
			int32 Human = -1;
			for (int32 I = 0; I < S->NumRacers(); ++I) { if (S->GetRacer(I).bHuman) { Human = I; break; } }
			if (Human >= 0 && Row.Racer >= 0 && Row.Racer < S->NumRacers())
			{
				R.Aggression = S->GetRacer(Row.Racer).Rivals[Human].StartLevel;
				R.AggressionTo = S->GetRacer(Row.Racer).Rivals[Human].Level;
			}
		}
		Out.Add(R);
	}
	return Out.Num() > 0;
}

bool FSSXRaceUIBridge::GetEntrants(TArray<FSSXRacerResult>& Out) const
{
	// FUN_001B4250
	Out.Reset();
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	if (!S)
	{
		return false;
	}
	int32 Human = -1;
	for (int32 I = 0; I < S->NumRacers(); ++I) { if (S->GetRacer(I).bHuman) { Human = I; break; } }
	for (int32 I = 0; I < S->NumRacers(); ++I)
	{
		const FRacerRecord& Rec = S->GetRacer(I);
		const ASnowboarderPawn* P = Owner->GetPawn(I);
		FSSXRacerResult R;
		R.Char = P ? P->Character : 0;
		R.Name = SSXFeTables::RiderName(R.Char);
		R.Player = Rec.bHuman ? 0 : -1;
		R.Place = I + 1;
		if (!Rec.bHuman && Human >= 0) { R.Aggression = R.AggressionTo = Rec.Rivals[Human].Level; }
		Out.Add(R);
	}
	return Out.Num() > 0;
}

void FSSXRaceUIBridge::ResultsStats(const FRaceResultRow& Row, FSSXShowoffStats& Out)
{
	Out.Score = Out.StylePoints = (int32)Row.Score;      // 0x32E6A0
	// 0x1B1278
	Out.AvgSpeed = Row.SpeedFrames != 0.f ? double(Row.SpeedKmhSum / Row.SpeedFrames) : 0.0;
	Out.SpinDeg = Row.SpinDeg;
	Out.FlipDeg = Row.FlipDeg;
	Out.Grabs = Row.Grabs;
	Out.Rails = Row.Rails;
	Out.TricksLanded = Row.Tricks;
	Out.Wipeouts = Row.Wipeouts;
	Out.BiggestAir = Row.BiggestAir;                     // 0x1B0998
	Out.AirTime = Row.AirTime;
	Out.GrabTime = Row.GrabTime;
	Out.RailTime = Row.RailTime;
	Out.TotalTime = Row.TimeCs / 100.0;
	// 0x1B0BB8, 0x1551C0, 0x32E698, 0x3382D8, 0x338350, 0x1B2158
	Out.BestTrick = SSXTrick::FTrickTables::Get().Compose(Row.BestTrickW0, Row.BestTrickW1);
}

bool FSSXRaceUIBridge::GetShowoffStats(int32 Player, FSSXShowoffStats& Out) const
{
	// FUN_001B0998, FUN_001B1278, 0x111820
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	if (!S)
	{
		return false;
	}
	int32 N = 0;
	for (int32 I = 0; I < S->NumRacers(); ++I)
	{
		const FRacerRecord& Rec = S->GetRacer(I);
		if (!Rec.bHuman) { continue; }
		if (N++ != Player) { continue; }
		Out = FSSXShowoffStats();
		Out.Score = Out.StylePoints = (int32)Rec.Score;
		for (const FRaceResultRow& Row : S->GetFinalResults())
		{
			if (Row.Racer == I) { ResultsStats(Row, Out); }
		}
		return true;
	}
	return false;
}

bool FSSXRaceUIBridge::ConsumeFinish(int32& OutPlayer, int32& OutResult, FString& OutValue)
{
	if (PendingFinish.Num() == 0)
	{
		return false;
	}
	const FFinish F = PendingFinish[0];
	PendingFinish.RemoveAt(0);
	OutPlayer = F.Player;
	OutResult = F.Result;
	OutValue = F.Value;
	return true;
}

void FSSXRaceUIBridge::SetPaused(bool bPaused)
{
	if (Owner)
	{
		UGameplayStatics::SetGamePaused(Owner, bPaused);
	}
}

bool FSSXRaceUIBridge::RequestReplay()
{
	// 0x1EF990, 0x191B60
	if (!Owner || !Owner->StartResultsReplay())
	{
		return false;
	}
	Owner->ReplayStartAudio();
	FSSXRaceCamera::StartFlash();   // FUN_00174C30
	return true;
}

void FSSXRaceUIBridge::StopReplay()
{
	// 0x1EF990, 0x191BE0
	if (Owner)
	{
		Owner->StopResultsReplay();
		Owner->ReplayExitAudio();
	}
	FSSXRaceCamera::StartFlash();   // FUN_00174C30
}

bool FSSXRaceUIBridge::BuildReplayFile(TArray<uint8>& Out)
{
	FSSXRaceCamera::StartFlash();   // 0x1EF990, FUN_00174C30
	return Owner && Owner->BuildReplayFile(Out);
}

void FSSXRaceUIBridge::SetReplayMenu(bool bOpen)
{
	// 0x1EF990, 0x2153F8, 0x215488
	ReplayAudio(bOpen);
	FSSXRaceCamera::StartFlash();   // FUN_00174C30
}

FSSXReplay* FSSXRaceUIBridge::GetReplay()
{
	return Owner && Owner->GetSession() && !Owner->GetLesson() ? &Owner->GetReplayMutable() : nullptr;
}

int32 FSSXRaceUIBridge::GetReplayPadRacer() const
{
	// 0x1EC548
	return Owner ? FMath::Max(0, Owner->GetHumanPlayer(Owner->GetRacerIndex(Owner->GetPlayerPawn()))) : 0;
}

int32 FSSXRaceUIBridge::GetRacerCount() const
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	return S ? S->NumRacers() : 1;
}

int32 FSSXRaceUIBridge::GetReplayCameraMode() const
{
	// 0x1786C0
	if (const FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr) { return RC->GetMode(); }
	return Owner ? Owner->GetReplayWorld().Camera.Mode : 0;
}

void FSSXRaceUIBridge::ReplayCameraNudge(float Amount, int32 Op)
{
	if (FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr) { RC->Nudge(Amount, Op); }   // 0x178BE8
}

void FSSXRaceUIBridge::ReplayNextCamera()
{
	if (FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr) { RC->NextMode(); }   // 0x178680
}

FString FSSXRaceUIBridge::GetReplayTargetName() const
{
	// 0x32DB74
	TArray<FSSXRacerResult> Rows;
	if (!Owner || !GetEntrants(Rows))
	{
		return FString();
	}
	const int32 T = Owner->GetViewRacer();
	return Rows.IsValidIndex(T) ? Rows[T].Name : FString();
}

void FSSXRaceUIBridge::ReplayAudio(bool bPause)
{
	if (USSXAudioSubsystem* A = Owner ? USSXAudioSubsystem::Get(Owner) : nullptr)
	{
		if (bPause) { A->PauseGameAudio(); } else { A->ResumeGameAudio(); }
	}
}

void FSSXRaceUIBridge::ReplaySpeechStop()
{
	if (USSXAudioSubsystem* A = Owner ? USSXAudioSubsystem::Get(Owner) : nullptr) { A->StopSpeech(0); }
}

void FSSXRaceUIBridge::ReplayTargetAudio()
{
	// 0x1EB6D0, 0x178BE8, 0x22D370, 0x215370, 0x2210C8, 0x20ED20, 0x2210A8
	if (!Owner)
	{
		return;
	}
	if (USSXTrackEffects* Fx = USSXTrackEffects::Get(Owner))
	{
		Fx->ExtraAmbientPass();
	}
	USSXAudioSubsystem* A = USSXAudioSubsystem::Get(Owner);
	if (!A)
	{
		return;
	}
	FSSXCrowdSystem& Crowd = A->GetCrowdVoices().Crowd;
	Crowd.Neutral(0);
	const FSSXReplayCamera* RC = Owner->GetReplayCamera();
	const ASnowboarderPawn* P = RC ? Owner->GetPawn(RC->GetTarget()) : nullptr;
	const FRiderSim* S = P ? P->GetSim() : nullptr;
	if (S && S->State.Motion == ERiderMotion::Air && float(S->State.Prediction.FlightTime) > 2.f)
	{
		Crowd.Arm(0);
	}
}

void FSSXRaceUIBridge::RequestRestart()
{
	if (Owner)
	{
		UGameplayStatics::SetGamePaused(Owner, false);
		Owner->RestartRace();
	}
}

void FSSXRaceUIBridge::RequestExit(int32 Code)
{
	if (!Owner)
	{
		return;
	}
	// 0x1AC580
	UGameplayStatics::SetGamePaused(Owner, true);
	if (USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(Owner))
	{
		// 0x111820
		if (const FRaceSession* S = Owner->GetSession())
		{
			int32 N = 0;
			for (int32 I = 0; I < S->NumRacers() && N < 2; ++I)
			{
				const FRacerRecord& Rec = S->GetRacer(I);
				if (!Rec.bHuman) { continue; }
				Sub->Profile.Session.RaceStats[N++] = RaceStatBlock(Rec);
			}
		}
		Sub->ReturnToFrontEnd(Code);
	}
}

void FSSXRaceUIBridge::RequestNextVenue(int32 GameMode, int32 Track)
{
	if (!Owner)
	{
		return;
	}
	UGameplayStatics::SetGamePaused(Owner, false);
	if (USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(Owner))
	{
		Sub->Profile.Session.bNextVenue = true;   // 0x1BBB30
		Sub->LastExitCode = 4;
		FSSXFeLaunch L = Sub->Launch;
		L.GameMode = GameMode;
		L.Track = Track;
		L.Racers = SSXSettings::NextEventRacerCount(GameMode, FMath::Max(1, L.Players), Sub->Profile.Settings.MaxBoarders);
		Sub->RequestLaunch(L);
	}
}

void FSSXRaceUIBridge::CycleCameraView(int32 Player, int32 Step)
{
	// FUN_00173e70
	ASnowboarderPawn* P = Owner && Player == 0 ? Owner->GetPlayerPawn() : nullptr;
	if (P)
	{
		P->CycleCameraView(Step);
	}
}

int32 FSSXRaceUIBridge::GetCameraView(int32 Player) const
{
	const ASnowboarderPawn* P = Owner && Player == 0 ? Owner->GetPlayerPawn() : nullptr;
	const FSSXRaceCamera* Cam = P ? P->GetRaceCamera() : nullptr;
	return Cam ? Cam->GetViewIndex() : -1;
}

float FSSXRaceUIBridge::GetRiderFade(int32 Player) const
{
	// 0x1AF320
	const ASnowboarderPawn* P = Owner && Player == 0 ? Owner->GetPlayerPawn() : nullptr;
	const FRiderSim* Sim = P ? P->GetSim() : nullptr;
	return Sim ? float(Sim->State.ResetFade) : 0.f;
}

FString FSSXRaceUIBridge::GetSongName() const
{
	const USSXAudioSubsystem* Audio = Owner ? USSXAudioSubsystem::Get(Owner) : nullptr;
	return Audio ? Audio->GetRaceSongTitle() : FString();
}

void FSSXRaceUIBridge::StepSong(int32 Dir)
{
	if (USSXAudioSubsystem* A = Owner ? USSXAudioSubsystem::Get(Owner) : nullptr) { A->StepRaceSong(Dir); }
}

void FSSXRaceUIBridge::PauseMenuAudio(bool bPause)
{
	if (USSXAudioSubsystem* A = Owner ? USSXAudioSubsystem::Get(Owner) : nullptr)
	{
		if (bPause) { A->PauseGameAudio(); } else { A->ResumeGameAudio(); }
	}
}

void FSSXRaceUIBridge::PauseSong()
{
	if (USSXAudioSubsystem* A = Owner ? USSXAudioSubsystem::Get(Owner) : nullptr) { A->PauseRaceSong(); }
}

const SSXLesson::FLessonMan* FSSXRaceUIBridge::GetLesson() const
{
	return Owner ? Owner->GetLesson() : nullptr;
}

void FSSXRaceUIBridge::LessonPad(uint32 Bits)
{
	if (Owner)
	{
		Owner->LessonPad(Bits);
	}
}

bool FSSXRaceUIBridge::GetTrickInput(int32 Player, int32& PadLayout, int32& BoardClass, bool& bFlipSpin) const
{
	const ASnowboarderPawn* Pawn = Owner && Player == 0 ? Owner->GetPlayerPawn() : nullptr;
	const FRiderSim* Sim = Pawn ? Pawn->GetSim() : nullptr;
	if (!Sim)
	{
		return false;
	}
	PadLayout = Pawn->GetMapperLayout();
	BoardClass = Sim->Profile.BoardClass;
	bFlipSpin = false;
	return true;
}

// 0x164FC0, 0x163AB0

void FSSXRaceUIBridge::FinishEvent()
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	USSXFrontEndSubsystem* Sub = Owner ? USSXFrontEndSubsystem::Get(Owner) : nullptr;
	if (!S || !Sub)
	{
		return;
	}
	FSSXProfile& P = Sub->Profile;
	FSSXEventResult R;
	R.Mode = int32(S->GetMode());
	R.Venue = Owner->GetTrackData().TrackId;
	TArray<FRaceStanding> Rows;
	S->GetStandings(Rows);
	for (const FRaceStanding& Row : Rows)
	{
		if (!Row.bHuman) continue;
		const ASnowboarderPawn* Pawn = Owner->GetPawn(Row.Racer);
		FSSXEventHuman H;
		H.Player = 0;
		H.Char = Pawn ? Pawn->Character : P.Slot[0].Char;
		H.Place = Row.Rank;
		// 0x111820, 0x166968
		H.TimeCs = Row.bFinished ? (int32)Row.FinishCs : 50000;
		H.Score = (int32)Row.Score;
		for (const FRaceResultRow& F : S->GetFinalResults()) { if (F.Racer == Row.Racer) { H.TimeCs = F.TimeCs; H.Score = (int32)F.Score; break; } }
		H.Name = P.Slot[0].UserName;
		// 0x165400, 0x11DA40
		P.ShowoffEndTricks(H.Player, R.Mode, Row.bFinished ? (int32)Row.FinishCs : 0);
		H.EventTricks = P.Slot[H.Player].EventTricks;
		R.Humans.Add(H);
	}
	// 0x166CB0
	if (SSXProg::CareerBestsMode(R.Mode))
	{
		for (int32 I = 0; I < S->NumRacers(); ++I)
		{
			const FRacerRecord& Rec = S->GetRacer(I);
			if (!Rec.bHuman) { continue; }
			const ASnowboarderPawn* Pawn = Owner->GetPawn(I);
			const int32 Char = Pawn ? Pawn->Character : P.Slot[0].Char;
			if (Char >= 0 && Char < SSXProg::NumRiders)
			{
				SSXProg::CareerBests(P.Career[Char], RaceStatBlock(Rec), R.Venue);
			}
			break;
		}
	}
	SSXProg::EndEvent(P, P.Session, R, Sub->Cheats.bMaxAttributes);
	for (FSSXPlayerSlot& Sl : P.Slot) { Sl.EventTricks = 0; }
	P.bSettingsDirty = true;
	UE_LOG(LogTemp, Log, TEXT("[progression] event mode %d venue %d heat %d: medal %d, %d unlock(s), %d record(s), rank points %d"),
		R.Mode, R.Venue, P.Session.Heat, P.Session.LastMedal, P.Session.Unlocks.Num(), P.Session.PendingRecords.Num(),
		R.Humans.Num() && R.Humans[0].Char >= 0 ? P.Career[R.Humans[0].Char].RankPoints() : 0);
}

bool FSSXRaceUIBridge::HasPendingHiScore() const
{
	const USSXFrontEndSubsystem* Sub = Owner ? USSXFrontEndSubsystem::Get(Owner) : nullptr;
	return Sub && Sub->Profile.Session.PendingRecords.Num() > 0;
}

void FSSXRaceUIBridge::GetUnlocks(TArray<FSSXUnlock>& Out) const
{
	const USSXFrontEndSubsystem* Sub = Owner ? USSXFrontEndSubsystem::Get(Owner) : nullptr;
	Out = Sub ? Sub->Profile.Session.Unlocks : TArray<FSSXUnlock>();
}

// 0x111820
static void FinishOrder(const FRaceSession& S, TArray<FRaceStanding>& Rows)
{
	const TArray<FRaceResultRow>& Table = S.GetFinalResults();
	if (Table.Num() == 0)
	{
		S.GetStandings(Rows);
		return;
	}
	Rows.Reset();
	for (int32 K = 0; K < Table.Num(); ++K)
	{
		FRaceStanding& R = Rows.AddDefaulted_GetRef();
		R.Racer = Table[K].Racer;
		R.Rank = K;
		R.bHuman = Table[K].bHuman;
		R.Score = Table[K].Score;
	}
}

bool FSSXRaceUIBridge::IsEliminated() const
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	if (!S || S->GetMode() != ERaceEventMode::CircuitRace)
	{
		return false;
	}
	TArray<FRaceStanding> Rows;
	FinishOrder(*S, Rows);
	for (const FRaceStanding& Row : Rows)
	{
		if (Row.bHuman && Row.Rank < (Rows.Num() + 1) / 2) { return false; }
	}
	return true;
}

bool FSSXRaceUIBridge::IsCircuitBeaten() const
{
	// 0x1618C0, 0x1618E0, 0x3353F4, 0x3353F0
	const USSXFrontEndSubsystem* Sub = Owner ? USSXFrontEndSubsystem::Get(Owner) : nullptr;
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	if (!Sub || !S)
	{
		return false;
	}
	return SSXRace::IsScoreMode(S->GetMode()) ? Sub->Profile.Session.bShowoffCircuitBeaten : Sub->Profile.Session.bRaceCircuitBeaten;
}

void FSSXRaceUIBridge::RequestNextHeat()
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	USSXFrontEndSubsystem* Sub = Owner ? USSXFrontEndSubsystem::Get(Owner) : nullptr;
	if (!S || !Sub)
	{
		return;
	}
	int32 Place = -1;
	TArray<FRaceStanding> Rows;
	FinishOrder(*S, Rows);
	for (const FRaceStanding& Row : Rows) { if (Row.bHuman) { Place = Row.Rank; break; } }
	FSSXProfile& P = Sub->Profile;
	// 0x163AB0, 0x16D0B0, 0x16C3B0
	TArray<int32> Finish;
	for (const FRaceStanding& Row : Rows)
	{
		const ASnowboarderPawn* Pawn = Owner->GetPawn(Row.Racer);
		Finish.Add(Pawn ? Pawn->Character : -1);
	}
	const int32 Code = Owner->HeatRiders.Num() == Rows.Num()
		? SSXProg::CircuitHeatEnd(P.Session, Finish, Sub->Character, int32(S->GetMode()), P.SlotCareer(0))   // 0x16C328
		: SSXProg::AdvanceHeat(P.Session, Place);
	if (Code == SSXHeats::ExitNextHeat)
	{
		// 0x180B58, 0x16D300
		SSXProg::BuildPointSession(P, P.Slot[0].Char, Sub->Cheats.bMaxAttributes, P.Session.Points[0]);
		UGameplayStatics::SetGamePaused(Owner, false);
		if (Owner->HeatRiders.Num() > 0)
		{
			UWorld* World = Owner->GetWorld();
			UGameplayStatics::OpenLevel(World, FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())));
		}
		else
		{
			Owner->RestartRace();
		}
	}
	else
	{
		// 0x1141E0, 0x1B32C0, 0x113BB0, 0x1BBB30, FUN_00265AF8
		const USSXFrontEndSubsystem* Fe = USSXFrontEndSubsystem::Get(Owner);
		RequestExit(Fe ? Fe->LastExitCode : 2);
	}
}

// 0x231548

void FSSXRaceUIBridge::PostRaceNarration(int32 Kind, int32 Arg)
{
	const FRaceSession* S = Owner ? Owner->GetSession() : nullptr;
	USSXAudioSubsystem* A = Owner ? USSXAudioSubsystem::Get(Owner) : nullptr;
	const USSXFrontEndSubsystem* Sub = Owner ? USSXFrontEndSubsystem::Get(Owner) : nullptr;
	if (!S || !A)
	{
		return;
	}
	if (Kind == 3)
	{
		A->GetAnnouncer().RankUpNarration(A->GetSpeech(), Arg);   // 0x1BA208, 0x2314E0
		return;
	}
	if (Kind == 4)
	{
		A->GetSpeech().Stop(0);   // 0x1BA2F0, 0x228D10
		return;
	}
	// 0x111820
	FSSXAnnouncer::FRaceEndInfo I;
	int32 Human = -1;
	for (int32 R = 0; R < S->NumRacers(); ++R) { if (S->GetRacer(R).bHuman) { Human = R; break; } }
	for (int32 R = 0; R < S->NumRacers(); ++R)
	{
		const FRacerRecord& Rec = S->GetRacer(R);
		const ASnowboarderPawn* P = Owner->GetPawn(R);
		FSSXAnnouncer::FRaceEndInfo::FRacer X;
		X.Slot = Rec.bHuman ? 0 : -1;
		X.Character = P ? P->Character : 0;
		X.Frames = Rec.StatsFrames;
		if (const SSXAir::FAirContext* Air = Rec.Sim ? SSXAir::GetAirContext(*Rec.Sim) : nullptr)
		{
			X.Respawns = Air->Score.RespawnCount;
			X.WipeOuts = Air->Score.WipeOutCount;
			X.Knockdowns = Air->Score.KnockdownCount;
		}
		X.TowardHuman = (!Rec.bHuman && Human >= 0) ? Rec.Rivals[Human].Score : 0;
		I.Racers.Add(X);
	}
	I.Order = S->GetResults();
	if (Sub)
	{
		for (const FSSXPendingRecord& E : Sub->Profile.Session.PendingRecords) { I.PendingSlots.Add(E.Player); }
	}
	FSSXAnnouncer& Ann = A->GetAnnouncer();
	I.HeatWord = Ann.HeatWord;
	I.ShowoffMedal0 = S->NumRacers() > 0 ? SSXProg::ShowoffMedal(int32(S->GetRacer(0).Score), Owner->GetTrackData().TrackId) : 3;
	I.bReplayState = !bFirstChainOverlay && S->GetEndRace().GetState() >= SSXEndRace::ResultsShown;
	bFirstChainOverlay = false;
	FSSXSpeechSystem& Sp = A->GetSpeech();
	switch (Kind)
	{
	case 0: Ann.ResultsOpen(Sp, I, A->IsChannelFree(0)); break;
	case 1: Ann.HiScoreRow(Sp, I, Arg); break;
	case 2:
	{
		TArray<TPair<int32, int32>> U;
		if (Sub) { for (const FSSXUnlock& E : Sub->Profile.Session.Unlocks) { U.Add({ E.Type, E.Value }); } }
		Ann.UnlockedOpen(Sp, I, U);
		break;
	}
	default: break;
	}
}