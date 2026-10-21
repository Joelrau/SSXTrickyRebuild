#include "Race/RaceSession.h"
#include "HAL/IConsoleManager.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderPose.h"
#include "SSXTrickyRebuild.h"
#include "Race/LessonMan.h"

#include "Input/PadMapper.h"
#include "Race/RaceTrackData.h"
#include "Sim/Air/RiderMessages.h"
#include "Sim/Air/AirMath.h"
#include "Sim/Ground/RiderCollision.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/SurfaceTable.h"

#include "Algo/StableSort.h"

#include "Sim/Air/AirStates.h"

static TAutoConsoleVariable<int32> CVarPairListOrder(TEXT("ssx.Rider.PairListOrder"), 0,
	TEXT("1: rider contacts walk the pair list in its own order (pairs made since the replay's last sort at the tail); 0: paired racers in racer order"));

namespace
{
	using namespace SSXRace;

	SSXAir::FTrickScore* StatsOf(FRiderSim* Sim)
	{
		SSXAir::FAirContext* Air = Sim ? SSXAir::GetAirContext(*Sim) : nullptr;
		return Air ? &Air->Score : nullptr;
	}
	void PushMsg(FRiderSim* Sim, int32 Type, int32 Value, float Life)
	{
		if (SSXAir::FTrickScore* St = StatsOf(Sim)) { St->Messages.Push(Type, Value, Life); }
	}

	class FRacerHooks final : public IRiderRaceHooks
	{
	public:
		FRacerHooks(FRaceSession* InSession, int32 InIndex) : Session(InSession), Index(InIndex) {}
		virtual void TrackScan(FRiderSim& Sim) override
		{
			// 0x117198, 0x155DC8
			if (SSXAir::FTrickScore* St = StatsOf(&Sim))
			{
				St->SampleSpeed(SSXEE::Length(SSXEE::F(Sim.State.Velocity)));
			}
			Session->TrackScan(Index);
		}
		virtual bool BuildInput(FRiderSim& Sim, FRiderInputWord& InOut) override { return Session->BuildInput(Index, InOut); }
		virtual bool OnForcedReset(FRiderSim& Sim) override { return Session->OnForcedReset(Index); }
		virtual bool PlaceRespawn(FRiderSim& Sim, FVector& P, FVector& H, FVector& V) override
		{
			return Session->PlaceRespawn(Index, P, H, V);
		}
		virtual void RiderCollision(FRiderSim& Sim) override { Session->RiderCollision(Index); }
		virtual bool RacerTable(const FRiderSim& Sim, FRacerTable& Out) const override { return Session->RacerTable(Index, Out); }
		virtual void OnTaunt(FRiderSim& Sim, int32 Rival) override { Session->OnTaunt(Index, Rival); }
		virtual void OnWinTaunt(FRiderSim& Sim, double Strength) override { Session->OnWinTaunt(Index, Strength); }
		virtual void SphereSetRebuilt(FRiderSim& Sim, bool bNow) override { Session->RiderSphereSetRebuilt(Index, bNow); }
		virtual void PoseFed(FRiderSim& Sim) override { Session->RiderPoseFed(Index); }
		virtual void SphereSetMoved(FRiderSim& Sim, const FVector& D) override { Session->RiderSphereSetMoved(Index, D); }
	private:
		FRaceSession* Session;
		int32 Index;
	};

	class FSitAndWaitControl final : public IRiderControlState
	{
	public:
		explicit FSitAndWaitControl(const FRaceSession* InSession) : Session(InSession) {}
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override
		{
			if (Session->GetPhase() == ERacePhase::Race)
			{
				// 0x105BC0
				const int32 Racer = Session->RacerOf(&Sim);
				if (Session->OnCameraScript && Racer >= 0 && Session->GetRacer(Racer).bHuman)
				{
					Session->OnCameraScript(Racer, TEXT("RaceStarted2"));
				}
				Sim.ApplyTransition(TEXT("T40"));
			}
		}
	private:
		const FRaceSession* Session;
	};
}

FRaceSession::FRaceSession() = default;
FRaceSession::~FRaceSession() = default;

void FRaceSession::DetachRacer(int32 Index)
{
	if (Racers.IsValidIndex(Index) && Racers[Index].Sim)
	{
		FRiderSim* Sim = Racers[Index].Sim;
		if (Sim->GetRaceHooks() == Racers[Index].Hooks.Get())
		{
			Sim->SetRaceHooks(nullptr);
		}
		Sim->SetControlState(ERiderControl::SitAndWait, nullptr);
		Sim->SetControlState(ERiderControl::FinishLine, nullptr);
		Racers[Index].Sim = nullptr;
	}
}

bool FRaceSession::Init(const FRaceTrackData& InData, const FRaceConfig& InConfig)
{
	TrackData = &InData;
	Config = InConfig;
	PathSet = &InData.PathsFor(IsScoreMode(Config.Mode));
	if (!PathSet->IsValid())
	{
		return false;
	}
	// 0x337FB8, 0x1983D0, 0x116AB8
	LapLength = PathSet->RaceLength();
	SkillLevels(Config.Mode, Config.Difficulty, SkillAhead, SkillBehind);
	Rng = FRaceRandom();
	FGameRng::StreamB().Seed(Config.RandomSeed);
	SitAndWait = MakeShared<FSitAndWaitControl>(this);
	Phase = PendingPhase = ERacePhase::None;
	Frame = 0;
	Clock = 0.0;
	RiderPairs.Reset();
	PendingBoxes = 0;
	return true;
}

int32 FRaceSession::AddRacer(FRiderSim* Sim, bool bHuman)
{
	if (!Sim || Racers.Num() >= MaxRacers || !TrackData)
	{
		return -1;
	}
	const int32 Index = Racers.Num();
	FRacerRecord& R = Racers.AddDefaulted_GetRef();
	R.Sim = Sim;
	R.bHuman = bHuman;
	R.Index = Index;
	R.Hooks = MakeShared<FRacerHooks>(this, Index);
	Sim->SetRaceHooks(R.Hooks.Get());
	Sim->DebugName = FString::Printf(TEXT("racer %d (%s, character %d)"), Index, bHuman ? TEXT("human") : TEXT("AI"), Sim->Profile.Character);
	Sim->bLogTransitions = bHuman;   // ssx.Rider.StateLog 1 (default): the players' transitions
	if (TrackData->bHasWorldFloor)
	{
		Sim->SetWorldFloor(TrackData->WorldFloorZ);
	}
	Sim->SetControlState(ERiderControl::SitAndWait, SitAndWait);
	// 0x106600, 0x106668
	R.FinishLine = MakeShared<SSXFinishLine::FControl>();
	R.FinishLine->IsWinner = [this, Index](const FRiderSim&) { return IsFinishWinner(Index); };
	R.FinishLine->RaceFrame = [this]() { return Frame; };
	Sim->SetControlState(ERiderControl::FinishLine, R.FinishLine);
	SSXRaceAI::ResetAI(R.AI, SkillAhead, SkillBehind);
	// 0x11B348, 0x14FA70
	RiderPairs.AddProxy();
	return Index;
}

void FRaceSession::SaveReplay(FRaceSessionKeyframe& Out) const
{
	Out.Racers = Racers;
	Out.Results = Results;
	Out.Cues = Cues;
	Out.Rng = Rng;
	Out.Phase = Phase;
	Out.PendingPhase = PendingPhase;
	Out.Clock = Clock;
	Out.Frame = Frame;
	Out.Placements = Placements;
	Out.RiderKnockdowns = RiderKnockdowns;
	Out.IntroMusicEnds = IntroMusicEnds;
	Out.SkillAhead = SkillAhead;
	Out.SkillBehind = SkillBehind;
	Out.EndRace = EndRace;
	Out.Intro = Intro;
	Out.IntroEntry = IntroEntry;
	Out.RiderPairs = RiderPairs;
	Out.PendingBoxes = PendingBoxes;
}

void FRaceSession::LoadReplay(const FRaceSessionKeyframe& In)
{
	for (int32 I = 0; I < Racers.Num() && I < In.Racers.Num(); ++I)
	{
		FRacerRecord& R = Racers[I];
		FRiderSim* const Sim = R.Sim;
		const TSharedPtr<IRiderRaceHooks> Hooks = R.Hooks;
		const TSharedPtr<SSXFinishLine::FControl> Finish = R.FinishLine;
		R = In.Racers[I];
		R.Sim = Sim;
		R.Hooks = Hooks;
		R.FinishLine = Finish;
	}
	Results = In.Results;
	Cues = In.Cues;
	Rng = In.Rng;
	Phase = In.Phase;
	PendingPhase = In.PendingPhase;
	Clock = In.Clock;
	Frame = In.Frame;
	Placements = In.Placements;
	RiderKnockdowns = In.RiderKnockdowns;
	IntroMusicEnds = In.IntroMusicEnds;
	SkillAhead = In.SkillAhead;
	SkillBehind = In.SkillBehind;
	EndRace = In.EndRace;
	Intro = In.Intro;
	IntroEntry = In.IntroEntry;
	if (In.RiderPairs.NumProxies() == Racers.Num())
	{
		RiderPairs = In.RiderPairs;
		PendingBoxes = In.PendingBoxes;
	}
}

int32 FRaceSession::RacerOf(const FRiderSim* Sim) const
{
	for (int32 I = 0; I < Racers.Num(); ++I)
	{
		if (Racers[I].Sim == Sim) return I;
	}
	return -1;
}

void FRaceSession::PlayCameraScript(const TCHAR* Script)
{
	if (!OnCameraScript) return;
	for (int32 I = 0; I < Racers.Num(); ++I)
	{
		if (Racers[I].bHuman) OnCameraScript(I, Script);
	}
}

void FRaceSession::Start(SSXIntro::EEntry Entry)
{
	Results.Reset();
	FinalRows.Reset();
	EndRace.Reset();
	IntroEntry = Entry;
	// 0x111108, 0x118068, 0x116AB8, 0x11CAE8
	for (int32 I = 0; I < Racers.Num(); ++I) PlaceAtStart(I);
	for (FRacerRecord& R : Racers) { R.Sim->State.Highlight(20); }
	Clock = 0.0;
	// FUN_001148D0
	PendingPhase = Lesson && Config.Mode == ERaceEventMode::Lesson ? ERacePhase::LoadLesson : ERacePhase::PreRace;
	if (Phase == ERacePhase::PreRace)
	{
		Phase = ERacePhase::None;
	}
}

class FRaceIntroEnv final : public SSXIntro::IEnv
{
public:
	explicit FRaceIntroEnv(FRaceSession& InS) : S(InS) {}
	virtual bool DirectorRunning() const override { return S.IsIntroBusy && S.IsIntroBusy(); }
	virtual bool SkipPressed() override { return !S.IsSkipPressed || S.IsSkipPressed(); }
	virtual bool HelpNeeded() const override { return S.IsHelpNeeded && S.IsHelpNeeded(); }
	virtual int32 LineKey(int32 LineId) const override { return S.IntroLineKey ? S.IntroLineKey(LineId) : LineId; }
	virtual void Act(const FString& Name, int32 Arg, const FString& Text) override
	{
		S.IntroLog.Add(FString::Printf(TEXT("%s:%d:%s"), *Name, Arg, *Text));
		if (Name == TEXT("director_reset"))
		{
			if (S.OnDirectorReset) S.OnDirectorReset();
		}
		else if (Name == TEXT("director_play"))
		{
			S.PlayCameraScript(*Text);
		}
		else if (Name == TEXT("place_racers"))
		{
			for (int32 I = 0; I < S.Racers.Num(); ++I) S.PlaceForRace(I, Arg != 0);   // 0x11A840
			++S.Placements;
		}
		else if (Name == TEXT("racers_intro_reset"))
		{
			// 0x116AB8
			for (int32 I = 0; I < S.Racers.Num(); ++I) S.PlaceAtStart(I);
		}
		else if (Name == TEXT("phase_next"))
		{
			S.SetPhase(ERacePhase::StartRace);
		}
		else if (Name == TEXT("intro_music_end"))
		{
			++S.IntroMusicEnds;
		}
		if (S.OnIntroAction) S.OnIntroAction(Name, Arg, Text);
	}
private:
	FRaceSession& S;
};

bool FRaceSession::UsesGates() const
{
	return UsesGateStart(Config.Mode, TrackData ? TrackData->TrackId : -1);
}

void FRaceSession::AddCue(ERaceCueKind Kind, int32 Racer, int32 Type, int32 Param, int32 Extra)
{
	if (Cues.Num() >= 1024)
	{
		Cues.RemoveAt(0, 256, EAllowShrinking::No);
	}
	Cues.Add({ Kind, Racer, Type, Param, Extra, Frame });
}

void FRaceSession::Tick()
{
	if (PendingPhase != Phase)
	{
		ExitPhase(Phase);
		Phase = PendingPhase;
		EnterPhase(Phase);
	}
	UpdatePhase(Phase);
	// 0x11EFD8
	for (FRacerRecord& R : Racers)
	{
		GrudgeCooldown(R.Grudge, R.Rivals, Frame);
		if (Phase != ERacePhase::EndRace) { ++R.StatsFrames; }   // 0x155DC8
	}
	++Frame;
}

void FRaceSession::ExitPhase(ERacePhase P)
{
}

void FRaceSession::EnterPhase(ERacePhase P)
{
	switch (P)
	{
	case ERacePhase::PreRace:
		for (FRacerRecord& R : Racers)
		{
			R.Sim->State.bRacing = false;
		}
		{
			// 0x112B88
			FRaceIntroEnv Env(*this);
			Intro.Enter(Env, int32(Config.Mode), TrackData ? TrackData->TrackId : 0, IntroSlots, IntroEntry);
		}
		break;
	case ERacePhase::Countdown:
	{
		// 0x113520, 0x112848, 0x11A7D0
		if (UsesGates())
		{
			Clock = CountdownStart;
		}
		const ERiderControl Ctl = UsesGates() ? ERiderControl::GateAnticipate : ERiderControl::SitAndWait;
		for (FRacerRecord& R : Racers)
		{
			R.Sim->SetControl(Ctl, TEXT("countdown enter"));
		}
		break;
	}
	case ERacePhase::Race:
		Clock = IsScoreMode(Config.Mode) ? ShowoffTimeLimit(TrackData->GameTrack) : 0.0;
		for (FRacerRecord& R : Racers)
		{
			if (R.Sim->State.Control == ERiderControl::GateAnticipate)
			{
				R.Sim->ApplyTransition(TEXT("T46"));
			}
			R.Sim->State.bRacing = true;
		}
		AddCue(ERaceCueKind::Go, -1);
		break;
	case ERacePhase::EndRace:
		if (FinalRows.Num() == 0)
		{
			ComputeResults();
		}
		for (FRacerRecord& R : Racers)
		{
			R.Sim->State.bRacing = false;
		}
		EnterEndRace();   // 0x113E10
		break;
	case ERacePhase::RestartLesson:
		// 0x14EC48
		if (Lesson) { Lesson->Requested = SSXLesson::EState::Intro; }
		break;
	default:
		// 0x14ECA0, 0x14ED40
		break;
	}
}

void FRaceSession::UpdatePhase(ERacePhase P)
{
	switch (P)
	{
	case ERacePhase::PreRace:
	{
		// 0x112D10
		FRaceIntroEnv Env(*this);
		Intro.Update(Env);
		break;
	}
	case ERacePhase::StartRace:
		SetPhase(ERacePhase::Countdown);
		break;
	case ERacePhase::Countdown:
	{
		const double Prev = Clock;
		Clock += CountdownRate;
		// 0x113600
		if (Clock < 0.0 && FMath::FloorToInt(Prev / CountdownCuePeriod) != FMath::FloorToInt(Clock / CountdownCuePeriod))
		{
			AddCue(ERaceCueKind::CountdownTick, -1, 0, int32(Clock));
		}
		if (Clock >= 0.0)
		{
			SetPhase(ERacePhase::Race);
		}
		break;
	}
	case ERacePhase::Race:
	{
		UpdateRivalGeometry();
		if (Frame % RankPeriod == 0)
		{
			RankRacers();
		}
		if (IsScoreMode(Config.Mode))
		{
			Clock -= FrameDt;
			if (Clock <= 0.0)
			{
				Clock = 0.0;
				for (int32 I = 0; I < Racers.Num(); ++I)
				{
					FinishRacer(I);
				}
			}
		}
		else
		{
			Clock += FrameDt;
		}
		// 0x113820, 0x11D998, 0x1065C8, 0x21D610
		bool bAny = false, bAll = true;
		for (const FRacerRecord& R : Racers)
		{
			if (R.bHuman) { bAny = true; bAll = bAll && IsFinishDone(R); }
		}
		if (!bAny)
		{
			for (const FRacerRecord& R : Racers) { bAll = bAll && IsFinishDone(R); }
		}
		if (bAll && Racers.Num() > 0)
		{
			// 0x111738, 0x115040, 0x114F28, 0x115038, 0x111820
			AddCue(ERaceCueKind::RaceEnd, -1);
			if (bLoadedReplay)
			{
				SetPhase(ERacePhase::ReplayEndRace);
			}
			else
			{
				ComputeResults();
				SetPhase(ERacePhase::EndRace);
			}
		}
		break;
	}
	case ERacePhase::EndRace:
	{
		// 0x1141E0
		Clock += FrameDt;
		FRaceIntroEnv Env(*this);
		EndRace.Update(Env, IsReplayLooping && IsReplayLooping(), IsReplayOverlayUp && IsReplayOverlayUp());
		break;
	}
	case ERacePhase::LoadLesson:
		// 0x14ECB0, 0x112920
		PlaceForLesson();
		if (Lesson) { Lesson->Requested = SSXLesson::EState::Intro; }   // FUN_0014BAA8
		SetPhase(ERacePhase::PreLesson);
		break;
	case ERacePhase::RestartLesson:
		SetPhase(ERacePhase::SelectLesson);   // 0x14EC60
		break;
	case ERacePhase::EndLesson:
		Clock += FrameDt;   // 0x14ED68
		break;
	default:
		break;   // 0x14ED38, 0x115098
	}
}

void FRaceSession::PlaceForLesson()
{
	if (Racers.Num() == 0)
	{
		return;
	}
	// FUN_0014D898, 0x118068, FUN_00197EF8, 0x11AFE8, 0x11A840
	PlaceAtStart(0, SSXRace::StartSlots);
	Racers[0].Sim->SetControl(ERiderControl::LessonWait, TEXT("T08 lesson placement"));
	PlaceForRace(0, false, SSXRace::StartSlots);
	++Placements;
}

void FRaceSession::StampFinish(int32 I)
{
	if (!Racers.IsValidIndex(I) || Racers[I].IsFinished())
	{
		return;
	}
	if (!Lesson)
	{
		FinishRacer(I);
		return;
	}
	// 0x11DA40
	Racers[I].Sim->State.FinishResult = int32(Clock * 100.0);
}

bool FRaceSession::IsFinishDone(const FRacerRecord& R) const
{
	// 0x11D998, 0x1065C8
	return R.IsFinished() && R.Sim && R.Sim->State.Control == ERiderControl::FinishLine && R.FinishLine && R.FinishLine->IsDone();
}

bool FRaceSession::IsFinishWinner(int32 I) const
{
	const FRacerRecord& R = Racers[I];
	const int32 Medal = ShowoffMedalOf ? ShowoffMedalOf(int32(R.Score)) : 3;
	return SSXFinishLine::IsWinner(int32(Config.Mode), R.Rank, Racers.Num(), Medal, R.FinishCs(), TimeToBeatCs,
		IsLessonPassed && IsLessonPassed());
}

void FRaceSession::EnterEndRace()
{
	// 0x113E10, 0x115A30, 0x180848, 0x181D60
	FRaceIntroEnv Env(*this);
	Env.Act(TEXT("finish_event"), 0, FString());   // 0x164FC0, 0x166FA0, 0x166FE0
	int32 Focus = EndFocus;
	if (!Racers.IsValidIndex(Focus))
	{
		Focus = 0;
		for (int32 I = 0; I < Racers.Num(); ++I) { if (Racers[I].bHuman) { Focus = I; break; } }
	}
	int32 Scene1 = SSXEndRace::NoScene, Scene2 = SSXEndRace::NoScene;
	if (Racers.IsValidIndex(Focus))
	{
		TArray<TPair<int32, int32>> Pairs;
		for (int32 J = 0; J < Racers.Num(); ++J)
		{
			if (J != Focus) { Pairs.Add({ Racers[J].Rivals[Focus].Score, Racers[Focus].Rivals[J].Score }); }
		}
		Scene1 = SSXEndRace::RivalScene(Pairs);
		const FRacerRecord& F = Racers[Focus];
		const int32 Medal = ShowoffMedalOf ? ShowoffMedalOf(int32(F.Score)) : 3;
		Scene2 = SSXEndRace::ResultScene(int32(Config.Mode), F.Rank, Racers.Num(), Medal, F.FinishCs(), TimeToBeatCs);
	}
	EndRace.Enter(Env, RaceSlots, Scene1, Scene2);
	for (FRacerRecord& R : Racers)
	{
		// 0x11AFA8, 0x15EE30
		if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(*R.Sim))
		{
			Ch->bResetAnim = true;
			Ch->ClearPlayRequests();
		}
		R.Sim->State.Ch1Anim = 0x300;
		R.Sim->State.Ch1EndFrame = 0;
		R.Sim->State.Ch0Anim = 0x300;
		R.Sim->SetControl(ERiderControl::Void, TEXT("end race enter (0x11AFA8)"));
		R.Sim->SetMotion(ERiderMotion::Static, TEXT("end race enter (0x11AFA8)"));
	}
}

void FRaceSession::PlaceAtStart(int32 I, int32 SlotRacers)
{
	FRacerRecord& R = Racers[I];
	FRiderSim& Sim = *R.Sim;
	Sim.Reset();
	// 0x116AB8
	for (int32 J = 0; J < SSXRace::MaxRacers; ++J) { ResetRivalry(R.Rivals[J], R.RivalSetup[J]); }
	// 0x1579E0
	if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		Air->Score.ResetRaceStats();
	}
	FVector Pos;
	double Heading = 0.0;
	const int32 Path = StartPose(*PathSet, I, SlotRacers > 0 ? SlotRacers : Racers.Num(), Sim.Profile.SizeScale, Pos, Heading);
	if (Path < 0)
	{
		return;
	}
	// 0x11CAE8, 0x15EE30, 0x116BB0, 0x128AE8, 0x116DA8
	Sim.SpawnYaw(Pos, float(Heading), FVector::ZeroVector, 0.0, false);
	Sim.SetMotion(ERiderMotion::Static, TEXT("T01"));
	Sim.SetControl(ERiderControl::Void, TEXT("T01"));
	Sim.State.SurfaceId = 1;
	Sim.State.SurfaceP7 = Sim.Surface(1).SinkDepth();
	Sim.State.SurfaceP8 = Sim.Surface(1).SinkMax();
	Sim.State.ContactPoint = Sim.State.Position;
	Sim.State.SpawnPosition = Sim.State.Position;
	Sim.State.BoostMeter = R.StartMeter;
	Sim.State.bRacing = false;
	R.Race.SetPath(Path);
	const FRacePath* Main = PathSet->MainLine();
	R.Line = FRaceLineProgress();
	R.Line.SetLine(Main ? Main->Index : 0);
	R.BestLapMark = 0.0;
	R.Line.LapCount = 0;
	const bool bLapped = TrackData && TrackData->TrackId == LappedTrackId;
	if (bLapped)
	{
		for (const FRacePath& L : PathSet->LinePaths)
		{
			const FRacePathEvent* Lap = L.Events.FindByPredicate([](const FRacePathEvent& E) { return E.Type == SSXRaceEvent::Lap; });
			if (Lap)
			{
				R.BestLapMark = double(float(L.DistToGoAtStart) - float(Lap->Start));
				break;
			}
		}
		R.Line.LapCount = LappedTrackLaps;
	}
	for (int32 J = 0; J < MaxRacers; ++J) { R.RivalDist[J] = 1e10; }
	// 0x115100, 0x180A10
	R.CatchupNear = 0.0;
	R.CatchupFar = 0.0;
	R.Rank = 0;
	R.PrevRank = -1;
	R.CatchupRival = -1;
	R.Grudge.bActive = false;
	R.StatsFrames = 0;
	R.bTimedOut = false; // 0x155A20
	R.Checkpoints = 0;
	R.ResetAdvance = 0.0;
	SSXRaceAI::ResetAI(R.AI, SkillAhead, SkillBehind);
	R.AI.TimeScale = Sim.State.TimeScale;
	R.Race.Reproject(*PathSet, Pos);
	R.Line.Measure(*PathSet, Pos);
	R.Line.Best = R.Line.DistToGo;
	R.RaceTotal = R.Line.DistToGo;
	if (bLapped)
	{
		R.RaceTotal = double(float(R.Line.DistToGo) + float(LapLength) * 3.0f);
	}
	WritePathFields(I);
}

void FRaceSession::PlaceForRace(int32 I, bool bGates, int32 SlotRacers)
{
	PlaceAtStart(I, SlotRacers);
	FRiderSim& Sim = *Racers[I].Sim;
	// 0x11A840
	Sim.State.GateLaunch.Reset();
	Sim.State.SuspensionOffset = bGates ? double(float(Sim.Profile.SizeScale) * -100.0f + 75.0f) : 0.0;
	if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim))
	{
		// 0x15F390, 0x15EE30
		Ch->RequestReset();
		Ch->ScenePlays.Reset();
		Sim.State.Ch1Anim = 0x300;
		Sim.State.Ch1EndFrame = 0;
		Sim.State.Ch0Anim = 0x300;
		Ch->RequestPlay(bGates ? 0x1FC : 0);
		Ch->RequestPlay(SSXAnim::SitIdleId(Sim.Profile.Character));
	}
}

void FRaceSession::WritePathFields(int32 I)
{
	FRacerRecord& R = Racers[I];
	FRiderState& S = R.Sim->State;
	S.PathParam = R.Race.Param;
	S.PathLateral = R.Race.Lateral;
	S.PathPoint = R.Race.Point;
	S.PathLookahead = R.Race.Look;
	S.PathHeading = R.Race.Heading;
	S.TrackDistToGo = R.Line.DistToGo;
}

double FRaceSession::SkillProb(const FRacerRecord& R) const
{
	const int32 D = Config.SingleRaceDifficulty >= 0 ? Config.SingleRaceDifficulty : Config.Difficulty;
	if (D == 0)
	{
		return 0.0;
	}
	return R.AI.Skill == 1.0 ? 1.0 : R.AI.Skill / SSXRaceAI::SkillLinePick;
}

void FRaceSession::TrackScan(int32 I)
{
	// FUN_00156e80
	if (SSXAir::FTrickScore* St = StatsOf(Racers[I].Sim))
	{
		St->Messages.Age();
		// 0x156E80, 0x11DA40, 0x111820, 0x16BAA8
		if (Racers[I].bTimedOut) { St->Score = 0; }
		Racers[I].Score = St->Score;
	}
	if (Racers[I].bTimedOut) { Racers[I].Score = 0.0; }
	if (Phase != ERacePhase::Race)
	{
		return;
	}
	FRacerRecord& R = Racers[I];
	const FRiderState& S = R.Sim->State;
	const bool bFinished = R.IsFinished();
	TArray<FRaceTriggerEvent> Events;
	R.Line.Update(*PathSet, S.Position, S.Velocity, Frame, bFinished, LapLength, Events);
	for (const FRaceTriggerEvent& E : Events)
	{
		OnTrigger(I, E);
	}
	R.Race.Follow(*PathSet, S.Position, S.Velocity, Frame, bFinished, [this, &R]()
	{
		const FRiderState& St = R.Sim->State;
		return R.bHuman ? -1 : SSXRaceTrack::LineClassWanted(St.BoardClass, R.Rank, St.BoostMeter, SkillProb(R), Rng);
	});
	WritePathFields(I);
}

int32 FRaceSession::HumanRank() const
{
	for (const FRacerRecord& R : Racers)
	{
		if (R.bHuman)
		{
			return R.Rank;
		}
	}
	return 0;
}

void FRaceSession::BuildAIView(int32 I, FAIRiderView& V) const
{
	const FRacerRecord& R = Racers[I];
	const FRiderSim& Sim = *R.Sim;
	const FRiderState& S = Sim.State;
	V = FAIRiderView();
	V.Index = I;
	V.Pos = S.Position;
	V.Vel = S.Velocity;
	if (S.bRenderRowsValid)
	{
		// 0x129628, 0x1372F8
		V.Side = S.RenderRow0;
		V.Fwd = S.RenderRow1;
		V.Up = S.RenderRow2;
	}
	else
	{
		V.Side = S.BasisSide;
		V.Fwd = S.BasisForward;
		V.Up = S.BasisUp;
	}
	V.Yaw = S.Yaw;
	V.Motion = int32(S.Motion);
	V.Control = int32(S.Control);
	V.Path = PathSet->AIPaths.IsValidIndex(R.Race.Path) ? &PathSet->AIPaths[R.Race.Path] : nullptr;
	V.PathParam = R.Race.Param;
	V.PathParamPrev = R.Race.ParamPrev;
	V.PathLateral = R.Race.Lateral;
	V.Lookahead = R.Race.Look;
	V.PathHeading = R.Race.Heading;
	V.BoostMeter = S.BoostMeter;
	V.Rank = R.Rank;
	V.BoardClass = S.BoardClass;
	V.ProfileBoardClass = Sim.Profile.BoardClass;
	V.Character = Sim.Profile.GetTrickCharacter();
	V.StatAir = Sim.Profile.StatBytes[0x14 - 0x0E];
	V.StatTrick = Sim.Profile.StatBytes[0x1D - 0x0E];
	V.GateCur = S.GateLaunch.Cur;
	V.GateTarget = S.GateLaunch.Target;
	V.PredTimeLeft = S.Prediction.TimeLeft;
	V.AirElapsed = S.Prediction.AirElapsed;
	V.PredLandNormal = S.Prediction.LandNormal;
	V.SpinAngle = S.SpinAngle;
	V.FlipAngle = S.FlipAngle;
	if (const SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		V.SpinRate = Air->Spin.SpinRate;
		V.FlipRate = Air->Spin.FlipRate;
	}
	V.Channel1Anim = S.ShoveAnim != 0 ? SSXAnim::CategoryShove : 0;
	for (int32 J = 0; J < MaxRacers; ++J)
	{
		V.RivalDist[J] = R.RivalDist[J];
		V.RivalBearing[J] = R.RivalBearing[J];
	}
	V.CatchupRival = R.CatchupRival;
	V.CatchupNear = R.CatchupNear;
	V.CatchupFar = R.CatchupFar;
	V.Grudge = R.Grudge;
	V.bFinished = R.IsFinished();
}

bool FRaceSession::BuildInput(int32 I, FRiderInputWord& InOut)
{
	FRacerRecord& R = Racers[I];
	FRiderState& S = R.Sim->State;
	if (R.bHuman)
	{
		// 0x115100, 0x115A30
		S.TimeScale = StepTimeScale(S.TimeScale, CatchupFactorPlayer(false, 0.0, 0.0));
		return false;
	}
	FAIRiderView V;
	BuildAIView(I, V);
	Others.SetNum(Racers.Num());
	for (int32 J = 0; J < Racers.Num(); ++J)
	{
		const FRiderState& O = Racers[J].Sim->State;
		Others[J] = { J, O.Position, O.Velocity, Racers[J].Rank, Racers[J].bHuman };
	}
	FAIRaceView RV;
	RV.Frame = Frame;
	RV.Clock = Clock;
	RV.Mode = Config.Mode;
	RV.Riders = Others;
	RV.HumanRank = HumanRank();
	const uint32 Word = SSXRaceAI::AIWord(R.AI, V, RV, Rng);
	S.TimeScale = R.AI.TimeScale;
	InOut = FPadMapper::DecodeWord(S.Control, Word);
	return true;
}

void FRaceSession::OnTaunt(int32 Racer, int32 Rival)
{
	AddCue(ERaceCueKind::Taunt, Racer, 0, Rival);
}

void FRaceSession::OnWinTaunt(int32 Racer, double Strength)
{
	AddCue(ERaceCueKind::WinTaunt, Racer);
	Cues.Last().Value = Strength;
}

bool FRaceSession::OnForcedReset(int32 I)
{
	FRacerRecord& R = Racers[I];
	const FRiderState& S = R.Sim->State;
	if (PathSet->AIPaths.IsValidIndex(R.Race.Path))
	{
		for (const FRacePathEvent& E : PathSet->AIPaths[R.Race.Path].Events)
		{
			if (E.Type == SSXRaceEvent::AINoReset && E.Start <= S.PathParam && S.PathParam <= E.End)
			{
				return false;
			}
		}
	}
	double Advance = 0.0;
	const int32 N = Racers.Num();
	if (Config.Mode == ERaceEventMode::PracticeKnockdown && N > 1)
	{
		double Sum = 0.0;
		for (const FRacerRecord& O : Racers)
		{
			if (O.Index != I) { Sum += O.Line.DistToGo; }
		}
		Advance = FMath::Max(0.0, Sum / (N - 1));
	}
	else if (N > 1 && R.Line.DistToGo > RespawnRubberMinLeft && TrackData->TrackId != LappedTrackId
		&& (Config.Mode == ERaceEventMode::PracticeRace || Config.Mode == ERaceEventMode::CircuitRace
			|| Config.Mode == ERaceEventMode::PracticeTimedRace) && R.bHuman)
	{
		const int32 Want = R.Rank + 1 < N ? R.Rank + 1 : R.Rank - 1;
		for (const FRacerRecord& O : Racers)
		{
			if (O.Rank == Want && O.Index != I)
			{
				Advance = O.Sim->State.Velocity.Size() * RespawnRubberFactor;
				break;
			}
		}
	}
	R.ResetAdvance = Advance;
	return true;
}

bool FRaceSession::PlaceRespawn(int32 I, FVector& OutPos, FVector& OutHeading, FVector& OutVel)
{
	FRacerRecord& R = Racers[I];
	if (SSXAir::FTrickScore* St = StatsOf(R.Sim))
	{
		St->Respawn();   // 0x116AB8, FUN_00155aa8
	}
	if (!PathSet->AIPaths.IsValidIndex(R.Race.Path))
	{
		return false;
	}
	// 0x118C08, 0x197F08
	const FRacePath& Cur = PathSet->AIPaths[R.Race.Path];
	const double Adv = R.ResetAdvance;
	R.ResetAdvance = 0.0;
	const double Len = Cur.Length();
	FVector Q;
	if (R.Race.Param + Adv <= Len)
	{
		Q = Cur.PointAt(R.Race.Param + Adv);
	}
	else
	{
		Q = Cur.PointAt(Len) + Cur.TangentAt(Len) * (Adv - (Len - R.Race.Param));
	}
	{
		TArray<TPair<double, int32>> Keys;
		for (const FRacePath& A : PathSet->AIPaths)
		{
			// 0x197F08 runs 0x197F28 with the candidate filter: lines with candidate flag 0 sort last
			Keys.Add({ A.CandidateFlag == 0 ? 1e37 : A.BoundsDistance(Q), A.Index });
		}
		Algo::StableSortBy(Keys, [](const TPair<double, int32>& K) { return K.Key; });
		double Best = 1e37;
		int32 Pick = INDEX_NONE;
		for (int32 K = 0; K < FMath::Min(6, Keys.Num()); ++K)
		{
			const FRacePath& Cand = PathSet->AIPaths[Keys[K].Value];
			FVector Pt;
			double Lat;
			const double T = Cand.Project(Q, Pt, Lat);
			if (Cand.Length() - 200.0 < T)
			{
				continue;
			}
			const double Score = (Q - Cand.PointAt(T + 800.0)).SizeSquared() + (Q - Pt).SizeSquared();
			if (Score < Best)
			{
				Best = Score;
				Pick = Cand.Index == R.Race.Path ? INDEX_NONE : Cand.Index;
			}
		}
		if (Pick != INDEX_NONE)
		{
			R.Race.SetPath(Pick);
		}
	}
	double Param;
	{
		// 0x197478 with the rider's own cache (+0x390): on the same line the search runs forward from the rider's
		// segment, at most 3000 cm; a switched line has cleared it (+0x3A4 = -1), so that one searches every segment
		FVector Pt;
		double Lat;
		Param = PathSet->AIPaths[R.Race.Path].Project(Q, Pt, Lat, &R.Race.Cache);
	}
	// 0x119220, 0x119D30
	FVector P, V;
	double H = 0.0;
	RespawnPoint(PathSet->AIPaths[R.Race.Path], Param, {}, P, H, V, [](const FVector&) { return true; });
	R.Race.PlaceAt(*PathSet, Param);
	R.Line.Cache = FRacePathCache();
	R.Line.Measure(*PathSet, P);
	WritePathFields(I);
	OutPos = P;
	OutHeading = FVector(FMath::Cos(H), FMath::Sin(H), 0.0);
	OutVel = V;
	return true;
}

void FRaceSession::FinishRacer(int32 I)
{
	FRacerRecord& R = Racers[I];
	if (R.IsFinished())
	{
		return;
	}
	R.Sim->State.FinishResult = int32(Clock * 100.0);
	if (IsScoreMode(Config.Mode) && R.Sim->State.FinishResult == 0)
	{
		R.bTimedOut = true;   // 0x11DA40
		AddCue(ERaceCueKind::TimedOut, I);
	}
	AddCue(ERaceCueKind::Finish, I, 0, R.Sim->State.FinishResult);
}

void FRaceSession::OnTrigger(int32 I, const FRaceTriggerEvent& E)
{
	FRacerRecord& R = Racers[I];
	if (!E.bEnter)
	{
		AddCue(ERaceCueKind::TriggerLeave, I, E.Type);
		return;
	}
	if (Lesson)
	{
		// 0x11A348, FUN_0014D928, FUN_0014D930
		if (E.Type == SSXRaceEvent::Finish)
		{
			Lesson->EndTrigger();
		}
		return;
	}
	if (E.Type == SSXRaceEvent::Finish && R.Line.LapCount == 0)
	{
		FinishRacer(I);
	}
	if (E.Type == SSXRaceEvent::Lap && R.Line.LapCount > 0)
	{
		if (R.Line.LapCount >= 2)
		{
			// FUN_00155ca0
			PushMsg(R.Sim, SSXMsg::Laps, R.Line.LapCount, SSXMsg::LifeLaps);
		}
		R.Line.LapCount -= 1;
		AddCue(ERaceCueKind::Lap, I, E.Type, R.Line.LapCount);
	}
	if (E.Type == SSXRaceEvent::Checkpoint && !R.IsFinished())
	{
		if (IsScoreMode(Config.Mode))
		{
			// showoff: the first racer to reach this checkpoint count adds param seconds (Garibaldi 60)
			R.Checkpoints += 1;
			bool bFirst = true;
			for (const FRacerRecord& O : Racers)
			{
				if (O.Index != I && O.Checkpoints >= R.Checkpoints) { bFirst = false; }
			}
			if (bFirst)
			{
				Clock += double(E.Param);
				PushMsg(R.Sim, SSXMsg::TimeBonus, int32(E.Param), SSXMsg::LifeTimeBonus);   // FUN_00155c60
				AddCue(ERaceCueKind::TimeBonus, I, E.Type, int32(E.Param));
			}
		}
		else if (IsGapMode(Config.Mode))
		{
			R.GapCs = CheckpointGapCs(R);
			PushMsg(R.Sim, SSXMsg::Checkpoint, -R.GapCs, SSXMsg::LifeCheckpoint);
		}
		AddCue(ERaceCueKind::Checkpoint, I, E.Type, R.Rank, R.PrevRank);
		R.PrevRank = R.Rank;
	}
	AddCue(ERaceCueKind::TriggerEnter, I, E.Type, int32(E.Param), R.Line.LapCount);
}

void FRaceSession::UpdateRivalGeometry()
{
	// 0x126748
	const int32 N = Racers.Num();
	TArray<FVector, TInlineAllocator<8>> Centre;
	for (int32 I = 0; I < N; ++I)
	{
		const FRiderSim& Sim = *Racers[I].Sim;
		FRiderSphereSet Set;
		Centre.Add(SSXRider::BuildPostAISphereSet(Sim.Profile, Sim.State, SSXRider::MaskAll, Set) ? Set.BoundCentre : Sim.State.Position);
	}
	for (int32 I = 0; I < N; ++I)
	{
		for (int32 J = I + 1; J < N; ++J)
		{
			// 0x115A80, 0x2510F8
			const float Dx = float(Centre[I].X) - float(Centre[J].X), Dy = float(Centre[I].Y) - float(Centre[J].Y);
			const float Dist = float(FMath::Sqrt(double(SSXEE::FpuAdd(Dx * Dx, Dy * Dy))));
			const float A = SSXAnim::Atan2EE(Dy, Dx);
			const float Ap = SSXEE::FpuAdd(A, 3.1415927f);
			const float Kf = SSXEE::FpuAdd(Ap * 0.15915494f, 0.5f);
			float Fl = float(int32(Kf));
			if (Kf < Fl)
			{
				Fl = SSXEE::FpuSub(Fl, 1.0f);
			}
			Racers[I].RivalDist[J] = Racers[J].RivalDist[I] = Dist;
			Racers[J].RivalBearing[I] = A;
			Racers[I].RivalBearing[J] = SSXEE::FpuSub(Ap, Fl * 6.2831855f);
		}
	}
}

double FRaceSession::RemainingDistance(const FRacerRecord& R) const
{
	const FRaceLineProgress& P = R.Line;
	if (P.LapCount == 0)
	{
		return P.DistToGo;
	}
	const int32 Laps = P.DistToGo < R.BestLapMark ? P.LapCount : P.LapCount - 1;
	return P.DistToGo + Laps * FMath::Max(LapLength, 1.0);
}

int32 FRaceSession::ElapsedCs(const FRacerRecord& R) const
{
	if (R.IsFinished())
	{
		return FMath::Max(R.FinishCs(), 0);
	}
	return FMath::Max(int32(Clock * 100.0), 0);
}

int32 FRaceSession::CheckpointGapCs(const FRacerRecord& R) const
{
	for (const FRacerRecord& O : Racers)
	{
		if ((R.Rank == 0 && O.Rank == 1) || (R.Rank > 0 && O.Rank == 0))
		{
			const double Total = R.RaceTotal;
			const double DoneO = Total - RemainingDistance(O);
			const double DoneR = Total - RemainingDistance(R);
			double Pace;
			if (DoneR <= 0.0)
			{
				if (DoneO <= 0.0)
				{
					return 0;
				}
				Pace = ElapsedCs(O) / DoneO;
			}
			else
			{
				Pace = ElapsedCs(R) / DoneR;
			}
			return int32(Pace * (DoneO - DoneR)) + ElapsedCs(R) - ElapsedCs(O);
		}
	}
	return 0;
}

int32 FRaceSession::EstimatedFinishCs(const FRacerRecord& R) const
{
	if (R.IsFinished())
	{
		return ElapsedCs(R);
	}
	// 0x11F2A0
	const float Total = float(R.RaceTotal);
	const float Done = Total - float(RemainingDistance(R));
	if (Done <= 0.0f)
	{
		return ElapsedCs(R);
	}
	return int32((float(ElapsedCs(R)) * Total) / Done);
}

void FRaceSession::RankRacers()
{
	const int32 N = Racers.Num();
	TArray<TPair<double, int32>> Keys;
	for (const FRacerRecord& R : Racers)
	{
		double Key;
		if (Config.Mode == ERaceEventMode::PracticeKnockdown)
		{
			Key = double(R.Knockdowns);
		}
		else if (IsScoreMode(Config.Mode))
		{
			Key = R.Score;
		}
		else if (Config.Mode == ERaceEventMode::Lesson)
		{
			Key = double(R.Index);
		}
		else if (R.IsFinished())
		{
			Key = double(2147483647 - R.FinishCs());
		}
		else
		{
			Key = -(RemainingDistance(R) + R.Rank * RankHysteresis);
		}
		Keys.Add({ -Key, R.Index });
	}
	Algo::StableSortBy(Keys, [](const TPair<double, int32>& K) { return K.Key; });
	TArray<int32> Order;
	for (int32 Rank = 0; Rank < N; ++Rank)
	{
		FRacerRecord& R = Racers[Keys[Rank].Value];
		if (R.Rank != Rank)
		{
			AddCue(ERaceCueKind::RankChange, R.Index, 0, Rank, R.Rank);
		}
		R.Rank = Rank;
		Order.Add(R.Index);
	}
	AssignCatchup(Order);
}

void FRaceSession::AssignCatchup(const TArray<int32>& Order)
{
	if (Racers.Num() < 2)
	{
		return;
	}
	TArray<int32> Humans, AIs;
	for (int32 I : Order)
	{
		(Racers[I].bHuman ? Humans : AIs).Add(I);
	}
	if (Humans.Num() == 0)
	{
		return;
	}
	if (Config.Mode == ERaceEventMode::PracticeKnockdown)
	{
		for (FRacerRecord& R : Racers)
		{
			R.CatchupNear = -KnockdownCatchup;
			R.CatchupFar = KnockdownCatchup;
		}
		Racers[Order[0]].CatchupRival = Order[1];
		for (int32 K = 1; K < Order.Num(); ++K)
		{
			Racers[Order[K]].CatchupRival = Order[K - 1];
		}
		return;
	}
	if (!IsGapMode(Config.Mode))
	{
		for (FRacerRecord& R : Racers)
		{
			R.CatchupRival = -1;
		}
		return;
	}
	const double L = LapLength > 0.0 ? LapLength : 1.0;
	const bool bCircuit = Config.Mode == ERaceEventMode::CircuitRace;
	double Keep, OffsetScale;
	CatchupDifficulty(bCircuit, Config.Difficulty, Keep, OffsetScale);
	double Late = 0.0, Offset = 0.0;
	// 0x115100
	CatchupTrack(TrackData->GameTrack, Late, Offset);
	const double* NearTab = bCircuit ? CatchupNearCircuit : CatchupNearRace;
	const double* FarTab = bCircuit ? CatchupFarCircuit : CatchupFarRace;
	const int32 PerHuman = FMath::Max(1, Racers.Num() / Humans.Num());
	int32 H = 0;
	for (int32 K = 0; K < AIs.Num(); ++K)
	{
		FRacerRecord& AI = Racers[AIs[K]];
		const FRacerRecord& Hu = Racers[Humans[H]];
		AI.CatchupRival = Humans[H];
		AI.CatchupFar = L;
		AI.CatchupNear = -L;
		const double HumanLeft = Hu.Line.Best;
		const int32 Ix = FMath::Clamp(AI.Index, 0, MaxRacers - 1);
		const float Ofs = float(Offset) * float(OffsetScale);
		if (AI.Rank < Hu.Rank)
		{
			const float Far = FarTab[Ix] >= 0.0 ? SSXEE::FpuAdd(float(FarTab[Ix]), Ofs) : float(L);
			const float Extra = float(HumanLeft) < float(Late) ? float(HumanLeft) * SSXEE::FpuSub(1.0f, float(Keep)) : 0.0f;
			AI.CatchupFar = SSXEE::FpuAdd(Far, Extra);
		}
		else if (!(float(HumanLeft) < float(Late)))
		{
			AI.CatchupNear = NearTab[Ix] <= 0.0 ? SSXEE::FpuAdd(float(NearTab[Ix]), Ofs) : -float(L);
		}
		if ((K + 1) % PerHuman == 0)
		{
			H = FMath::Min(H + 1, Humans.Num() - 1);
		}
	}
}

void FRaceSession::ComputeResults()
{
	// 0x111820, 0x11F2A0
	const bool bTakeTable = FinalRows.Num() == 0;
	if (bTakeTable && (Config.Mode == ERaceEventMode::PracticeRace || Config.Mode == ERaceEventMode::CircuitRace))
	{
		// 0x11F050
		for (FRacerRecord& R : Racers)
		{
			for (int32 J = 0; J < Racers.Num() && J < SSXRace::MaxRacers; ++J)
			{
				if (J != R.Index) { BlendLevel(R.Rivals[J]); }
			}
		}
	}
	TArray<TPair<double, int32>> Rows;
	for (FRacerRecord& R : Racers)
	{
		if (bTakeTable && R.bHuman && !R.IsFinished() && R.Sim)
		{
			R.Sim->State.FinishResult = 0;
			R.EstimatedCs = 50000;
		}
		else
		{
			R.EstimatedCs = EstimatedFinishCs(R);
		}
		double Key;
		if (IsScoreMode(Config.Mode)) { Key = R.Score; }
		else if (Config.Mode == ERaceEventMode::Lesson) { Key = -double(R.Index); }
		else { Key = -double(R.EstimatedCs); }
		Rows.Add({ Key, R.Index });
	}
	Rows.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
	{
		return A.Key != B.Key ? A.Key > B.Key : A.Value > B.Value;
	});
	Results.Reset();
	for (int32 K = 0; K < Rows.Num(); ++K)
	{
		Results.Add(Rows[K].Value);
		Racers[Rows[K].Value].Rank = K;
	}
	if (!bTakeTable)
	{
		return;
	}
	int32 Human = -1;
	for (const FRacerRecord& R : Racers) { if (R.bHuman) { Human = R.Index; break; } }
	for (int32 I : Results)
	{
		const FRacerRecord& R = Racers[I];
		FRaceResultRow& Row = FinalRows.AddDefaulted_GetRef();
		Row.Racer = I;
		Row.bHuman = R.bHuman;
		Row.TimeCs = R.EstimatedCs;
		Row.Score = R.Score;
		if (const SSXAir::FTrickScore* St = StatsOf(R.Sim))
		{
			Row.SpinDeg = St->SpinDegRace;
			Row.FlipDeg = St->FlipDegRace;
			Row.Grabs = St->GrabsRace;
			Row.Rails = St->RailsRace;
			Row.Tricks = St->ScoredTricks;
			Row.Wipeouts = St->WipeOutCount;
			Row.AirTime = float(St->AirSecondsTotal);
			Row.GrabTime = float(St->HoldSecondsRace);
			Row.RailTime = float(St->RailSecondsTotal);
			Row.BiggestAir = float(St->AirSecondsBest);
			Row.SpeedKmhSum = St->SpeedKmhSum;
			Row.SpeedFrames = St->SpeedFrames;
			Row.BestTrickW0 = St->BestTrick.W0;   // 0x156E80
			Row.BestTrickW1 = St->BestTrick.W1;
		}
		if (I == Human)
		{
			Row.Kind = 3;
		}
		else if (Human >= 0 && Human < SSXRace::MaxRacers)
		{
			const SSXRace::FRivalry& P = R.Rivals[Human];
			Row.Kind = P.Kind;
			Row.LevelStart = P.StartLevel;
			Row.Level = P.Level;
			Row.PairScore = P.Score;
		}
	}
}

void FRaceSession::GetStandings(TArray<FRaceStanding>& Out) const
{
	Out.Reset();
	for (const FRacerRecord& R : Racers)
	{
		FRaceStanding& S = Out.AddDefaulted_GetRef();
		S.Racer = R.Index;
		S.Rank = R.Rank;
		S.bHuman = R.bHuman;
		S.bFinished = R.IsFinished();
		S.FinishCs = R.FinishCs();
		S.EstimatedCs = Phase == ERacePhase::EndRace ? R.EstimatedCs : EstimatedFinishCs(R);
		S.Remaining = RemainingDistance(R);
		S.LapsLeft = R.Line.LapCount;
		S.Score = R.Score;
	}
	Out.Sort([](const FRaceStanding& A, const FRaceStanding& B) { return A.Rank < B.Rank; });
}

void FRaceSession::RiderCollision(int32 I)
{
	if (!Racers.IsValidIndex(I) || int32(Phase) <= 2 || Phase == ERacePhase::EndRace)
	{
		return;
	}
	using namespace SSXRiderHit;
	const bool bMode0 = Config.Mode == ERaceEventMode::PracticeKnockdown;
	FRacerRecord& A = Racers[I];
	// 0x124920, 0x115A80
	auto HeadingOk = [this](int32 Target, int32 Other)
	{
		const FRacerRecord& T = Racers[Target];
		return HeadingWithin(T.Sim->State.Yaw, T.RivalBearing[Other]);
	};
	// 0x11EAF8, 0x11EA70, 0x11EBF8
	auto Bookkeep = [this](EHit H, int32 Victim, int32 Attacker)
	{
		FRacerRecord& V = Racers[Victim];
		FRacerRecord& A = Racers[Attacker];
		if (H == EHit::Bump)
		{
			OnBump(V.Grudge, V.Rivals, A.Grudge, A.Rivals, Victim, Attacker, !V.bHuman, !A.bHuman, Frame, A.Sim->State.ShoveAnim != 0);
		}
		else if (H == EHit::Knockdown)
		{
			OnKnockdown(V.Grudge, V.Rivals, A.Grudge, A.Rivals, Victim, Attacker, !V.bHuman, !A.bHuman, Frame);
		}
	};
	auto Reward = [this, bMode0](int32 Victim, int32 Attacker, bool bAttackerGate)
	{
		// 0x123FC8, 0x11E9A0, 0x155D98, 0x11E988, 0x156DE0
		FRiderSim& Att = *Racers[Attacker].Sim;
		if (bAttackerGate && (Att.State.Motion == ERiderMotion::WipeOut || Att.State.Motion == ERiderMotion::WipeOutRecover))
		{
			return;
		}
		SSXAir::BoostAdd(Att.State, KnockdownReward, SSXAir::BoostGain(Att.Profile));
		++RiderKnockdowns;
		NotifyKnockdown(Victim, Attacker);
		if (SSXAir::FTrickScore* St = StatsOf(Racers[Victim].Sim)) { St->Bail(bMode0); }
	};
	FlushRiderBoxes();
	auto VisitPair = [&](int32 J)
	{
		if (J == I || !Racers.IsValidIndex(J) || !Racers[J].Sim)
		{
			return;
		}
		FRacerRecord& B = Racers[J];
		const bool bCool = Frame - A.PairStamp[J] >= PairCooldownFrames;
		const FPairResult R = BodyContact(*A.Sim, *B.Sim, bCool, bMode0, HeadingOk(I, J), HeadingOk(J, I), A.RivalDist[J], B.RivalDist[I]);
		if (R.bContact)
		{
			++A.RiderContacts;
		}
		if (R.bImpulse)
		{
			A.PairStamp[J] = Frame;
			B.PairStamp[I] = Frame;
			++A.RiderImpulses;
			// 0x234D00
			auto HitCue = [&](EHit H, int32 Victim, int32 Attacker)
			{
				if (H == EHit::Bump || H == EHit::Knockdown)
				{
					Bookkeep(H, Victim, Attacker);
					AddCue(ERaceCueKind::RiderHit, Victim, H == EHit::Knockdown ? 2 : 1, Attacker, Racers[Attacker].Sim->State.ShoveAnim == 0 ? 1 : 0);
				}
			};
			HitCue(R.HitA, I, J);
			HitCue(R.HitB, J, I);
			const bool bKoA = R.HitA == EHit::Knockdown, bKoB = R.HitB == EHit::Knockdown;
			if (bKoA != bKoB)
			{
				Reward(bKoA ? I : J, bKoA ? J : I, true);
			}
			else if (bKoA)
			{
				// 0x11B018
				for (const int32 Who : { J, I })
				{
					if (SSXAir::FTrickScore* St = StatsOf(Racers[Who].Sim))
					{
						FRiderSim& Sm = *Racers[Who].Sim;
						SSXAir::BoostAdd(Sm.State, St->Bail(false), SSXAir::BoostGain(Sm.Profile));
					}
				}
				FGameRng::StreamB().Next();
			}
		}
	};
	auto ShovePass = [&](int32 J)
	{
		if (J == I || !Racers.IsValidIndex(J) || !Racers[J].Sim)
		{
			return;
		}
		FRacerRecord& B = Racers[J];
		if (B.ShoveStamp[I] < Frame - ShovePairGap && A.ShoveStamp[J] <= Frame - ShovePairGap)
		{
			double Js = 0.0;
			const EHit H = ShoveHit(*A.Sim, *B.Sim, bMode0, HeadingOk(J, I), &Js, B.RivalDist[I]);
			if (Js > 0.0 && H != EHit::None)
			{
				A.ShoveStamp[J] = Frame;
				B.ShoveStamp[I] = Frame;
				UE_LOG(LogSSX, Verbose, TEXT("[shove] racer %d -> %d: J %.1f, hit %d"), I, J, Js, int32(H));
				if (A.ShoveHitJ.Num() >= 64) { A.ShoveHitJ.RemoveAt(0); }
				A.ShoveHitJ.Add(Js);
				++A.RiderImpulses;
				if (H == EHit::Bump || H == EHit::Knockdown)
				{
					Bookkeep(H, J, I);
					AddCue(ERaceCueKind::RiderHit, J, H == EHit::Knockdown ? 2 : 1, I, A.Sim->State.ShoveAnim == 0 ? 1 : 0);
				}
				AddCue(ERaceCueKind::Shove, I, 0, J);   // 0x216960, 0x233CB8
				if (H == EHit::Knockdown)
				{
					Reward(J, I, false);
				}
			}
		}
	};
	if (CVarPairListOrder.GetValueOnAnyThread() != 0)
	{
		RiderPairs.Walk(I, VisitPair);
	}
	else
	{
		for (int32 J = 0; J < Racers.Num(); ++J)
		{
			if (RiderPairs.IsPaired(I, J))
			{
				VisitPair(J);
			}
		}
	}
	for (int32 J = 0; J < Racers.Num(); ++J)
	{
		ShovePass(J);
	}
}

void FRaceSession::RiderSphereSetRebuilt(int32 I, bool bNow)
{
	if (!Racers.IsValidIndex(I))
	{
		return;
	}
	if (!bNow)
	{
		PendingBoxes |= 1u << I;
		return;
	}
	FlushRiderBoxes();
	SetRiderBox(I);
}

void FRaceSession::RiderPoseFed(int32 I)
{
	if (Racers.IsValidIndex(I) && (PendingBoxes & (1u << I)))
	{
		PendingBoxes &= ~(1u << I);
		SetRiderBox(I);
	}
}

void FRaceSession::FlushRiderBoxes()
{
	for (int32 I = 0; PendingBoxes && I < Racers.Num(); ++I)
	{
		if (PendingBoxes & (1u << I))
		{
			PendingBoxes &= ~(1u << I);
			SetRiderBox(I);
		}
	}
	PendingBoxes = 0;
}

void FRaceSession::SetRiderBox(int32 I)
{
	// 0x126748, 0x236758, 0x14FE68
	if (!Racers[I].Sim)
	{
		return;
	}
	const FRiderSim& Sim = *Racers[I].Sim;
	FRiderSphereSet Set;
	if (!SSXRider::BuildPostAISphereSet(Sim.Profile, Sim.State, SSXRider::MaskAll, Set))
	{
		return;
	}
	FVector3f Min(Set.BoundCentre), Max(Set.BoundCentre);
	for (int32 K = 0; K < FMath::Min(Set.Count, FRiderSphereSet::MaxSpheres); ++K)
	{
		const FVector3f C(Set.Centres[K]);
		const float R = float(Set.Radii[K]);
		for (int32 A = 0; A < 3; ++A)
		{
			Min[A] = FMath::Min(Min[A], C[A] - R);
			Max[A] = FMath::Max(Max[A], C[A] + R);
		}
	}
	RiderPairs.SetBox(I, Min, Max);
}

void FRaceSession::ReplayPairUpkeep(bool bRebuildBoxes)
{
	FlushRiderBoxes();
	if (bRebuildBoxes)
	{
		for (int32 I = 0; I < Racers.Num(); ++I)
		{
			SetRiderBox(I);
		}
	}
	RiderPairs.SortLists();   // 0x14FE18, 0x150428
}

void FRaceSession::RiderSphereSetMoved(int32 I, const FVector& D)
{
	if (PendingBoxes & (1u << I))
	{
		FlushRiderBoxes();
		return;
	}
	FlushRiderBoxes();
	RiderPairs.Move(I, FVector3f(D));   // 0x1267A0, 0x14FE68
}

bool FRaceSession::RacerTable(int32 I, IRiderRaceHooks::FRacerTable& Out) const
{
	if (!Racers.IsValidIndex(I))
	{
		return false;
	}
	const FRacerRecord& R = Racers[I];
	Out.Self = I;
	Out.NumRacers = FMath::Min(Racers.Num(), int32(UE_ARRAY_COUNT(Out.Dist)));
	for (int32 J = 0; J < Out.NumRacers && J < MaxRacers; ++J)
	{
		Out.Dist[J] = R.RivalDist[J];
		Out.Bearing[J] = R.RivalBearing[J];
	}
	Out.bGrudge = R.Grudge.bActive;
	Out.GrudgeRival = R.Grudge.Rival;
	Out.GrudgeKind = R.Grudge.Kind;
	Out.RaceFrame = Frame;
	return true;
}

void FRaceSession::NotifyKnockdown(int32 Victim, int32 Attacker)
{
	if (!Racers.IsValidIndex(Victim) || !Racers.IsValidIndex(Attacker))
	{
		return;
	}
	FRacerRecord& V = Racers[Victim];
	FRacerRecord& A = Racers[Attacker];
	// 0x11EBF8, 0x124920
	A.Knockdowns += 1;
	V.Knockdowns -= 1;
	// FUN_00155d98, 0x11B018
	if (SSXAir::FTrickScore* St = StatsOf(A.Sim))
	{
		++St->KnockdownCount;
		St->Messages.Push(SSXMsg::Knockdown, 0, SSXMsg::LifeKnockdown);
	}
}
