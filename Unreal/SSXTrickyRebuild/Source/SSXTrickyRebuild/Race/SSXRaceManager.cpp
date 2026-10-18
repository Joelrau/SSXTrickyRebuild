#include "Race/SSXRaceManager.h"

#include "Progression/SSXSettingsEffects.h"

#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXCrowdVoices.h"
#include "Race/GameRng.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Anim/RiderAnim.h"
#include "Progression/SSXHeats.h"
#include "Progression/SSXProgression.h"
#include "Characters/SSXCharacterLibrary.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SnowboarderPawn.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXPlayerController.h"
#include "UI/SSXHudOverlay.h"
#include "UI/SSXLoadingScreen.h"
#include "UI/SSXRaceUIHost.h"
#include "SSXShowOffDraw.h"
#include "World/SSXPropAnimation.h"
#include "Effects/SSXTrackEffects.h"
#include "Input/InpFile.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTrickBook.h"
#include "Camera/RaceCamera.h"

class FRaceLessonEnv final : public SSXLesson::ILessonEnv
{
public:
	explicit FRaceLessonEnv(ASSXRaceManager& InM) : M(InM) {}

	virtual void SetRacePhase(ERacePhase P) override { M.Session.SetPhase(P); }
	virtual void StopSpeech() override
	{
		if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(&M)) { A->StopSpeech(0); }   // FUN_00228D10
	}
	virtual void Narration(int32 Step) override
	{
		if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(&M)) { A->GetAnnouncer().LessonNarration(A->GetSpeech(), Step); }
	}
	virtual void Sound(SSXLesson::ESound S) override
	{
		if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(&M))
		{
			A->PlayRaceUISound(S == SSXLesson::ESound::Move ? SSXUISound::ERace::Navigate
				: (S == SSXLesson::ESound::Back ? SSXUISound::ERace::Back : SSXUISound::ERace::Accept));
		}
	}
	virtual bool CameraIdle() const override
	{
		// FUN_00175E50
		const FSSXRaceCamera* Cam = Player() ? Player()->GetRaceCamera() : nullptr;
		return !Cam || !Cam->IsDirectorOn();
	}
	virtual void ResetWorld() override
	{
		// FUN_0014D7D0, 0x173A98, FUN_00136EA0, FUN_0014D898, FUN_00177DB0, FUN_00174C48
		if (USSXTrackEffects* Fx = USSXTrackEffects::Get(&M)) { Fx->LessonReset(); }
		if (USSXParticleWorld* Pw = USSXParticleWorld::Get(&M)) { Pw->ResetRiderEffects(Player()); }
		FSSXRaceCamera* Cam = Player() ? Player()->GetRaceCamera() : nullptr;
		if (Cam) { Cam->Reset(); }
		M.Session.PlaceForLesson();
		if (Cam) { Cam->MarkCut(); Cam->ForceFlash(); }
	}
	virtual void SetBoost(double Meter, double TrickyTimer) override
	{
		if (FRiderSim* Sim = Player() ? Player()->GetSim() : nullptr)
		{
			Sim->State.BoostMeter = Meter;
			Sim->State.TrickyTimer = TrickyTimer;
		}
	}
	virtual void UseTutorialMapping(bool bTutorial) override
	{
		if (ASnowboarderPawn* P = Player()) { P->SetTutorialMapping(bTutorial); }
	}
	virtual bool LoadInp(int32 Rider, int32 InLesson, TArray<FInpFrame>& Out) override
	{
		const FString Name = FString::Printf(TEXT("%s%02d"), *FString(SSXFeTables::RiderShort(Rider)).ToLower(), InLesson + 1);
		FString Err;
		if (!FInpFile::Load(FInpFile::TutorialPath(Name), Out, &Err))
		{
			UE_LOG(LogSSX, Warning, TEXT("[lesson] %s.inp: %s"), *Name, *Err);
			return false;
		}
		return true;
	}
	virtual void StampFinish() override { M.Session.StampFinish(0); }
	virtual void QuitRace() override
	{
		M.UIHost.RequestExit(2);
	}
	virtual void CameraFlash() override
	{
		if (FSSXRaceCamera* Cam = Player() ? Player()->GetRaceCamera() : nullptr) { Cam->ForceFlash(); }
	}
	virtual int32 Rand() override { return int32(FGameRng::StreamA().Next() & 0x7FFFFFFF); }
	virtual bool HasTrickText() const override
	{
		const SSXAir::FAirContext* Air = Player() && Player()->GetSim() ? SSXAir::GetAirContext(*Player()->GetSim()) : nullptr;
		return Air && !Air->Score.TrickText.IsEmpty();
	}
	virtual void ClearTrickText() override
	{
		if (SSXAir::FAirContext* Air = Player() && Player()->GetSim() ? SSXAir::GetAirContext(*Player()->GetSim()) : nullptr)
		{
			Air->Score.TrickText.Reset();
		}
	}

private:
	ASnowboarderPawn* Player() const { return M.GetPawn(0); }
	ASSXRaceManager& M;
};

TFunction<void()> ASSXRaceManager::TestBeforeSeed;

ASSXRaceManager::ASSXRaceManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AICharacters = { 10, 0, 8, 11, 6 };
}

ASSXRaceManager* ASSXRaceManager::Find(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ASSXRaceManager> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ASnowboarderPawn* ASSXRaceManager::GetPlayerPawn() const
{
	for (const TObjectPtr<ASnowboarderPawn>& P : Pawns)
	{
		if (P && !P->bAIRider) return P.Get();
	}
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return PC ? Cast<ASnowboarderPawn>(PC->GetPawn()) : nullptr;
}

void ASSXRaceManager::RestartRace()
{
	// 0x180A10
	if (bReady && bLesson)
	{
		// 0x1AC130, FUN_0014B638
		FRaceLessonEnv Env(*this);
		Lesson.Restart(Env);
		return;
	}
	if (bReady)
	{
		UIHost.PendingFinish.Reset();
		UIHost.bPostRaceChain = false;
		Session.ClearCues();
		Session.Start(SSXIntro::EEntry::Restart);
		if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this)) { Pw->ResetRiderEffects(); }   // 0x1128B8
		if (!bLesson) Replay.ResetIdle();   // 0x18EBD0
		bRaceEndRoutine = false;
		// 0x115DC0, 0x22E338
		if (USSXTrackEffects* Fx = USSXTrackEffects::Get(this))
		{
			Fx->RestartTrack();
		}
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
		{
			// FUN_00211B58
			SSXGameAudio::OnRaceRestart(*Audio, UWorld::RemovePIEPrefix(GetWorld()->GetMapName()).ToLower(), TrackData.TrackId, int32(Session.GetMode()));
		}
	}
}

void ASSXRaceManager::DrawComputerBoard(FGameRng& Rng, int32& OutBoard, int32& OutOutfit)
{
	// 0x16AE68
	OutOutfit = int32(Rng.RandInt(5));
	OutBoard = int32(Rng.RandInt(12));
}

void ASSXRaceManager::BeginPlay()
{
	Super::BeginPlay();
	if (const USSXFrontEndSubsystem* FrontEnd = USSXFrontEndSubsystem::Get(this); FrontEnd && FrontEnd->bHasSelection)
	{
		EventMode = FrontEnd->Launch.GameMode;
		Difficulty = FrontEnd->Launch.Difficulty;
	}
	const FString Track = UWorld::RemovePIEPrefix(GetWorld()->GetMapName()).ToLower();
	FString Err;
	if (!TrackData.Load(FRaceTrackData::DefaultPath(Track), &Err))
	{
		StatusText = FString::Printf(TEXT("no race data for '%s': %s"), *Track, *Err);
		UE_LOG(LogSSX, Error, TEXT("[race] %s"), *StatusText);
		bFailed = true;
	}
}

void ASSXRaceManager::EndPlay(const EEndPlayReason::Type Reason)
{
	if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
	{
		Audio->SetReplayPlayback(false, INDEX_NONE, nullptr);
		Audio->SetReplayRebuild(false);
	}
	for (int32 I = 0; I < Pawns.Num(); ++I)
	{
		if (IsValid(Pawns[I]) && Pawns[I]->HasSim())
		{
			if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(*Pawns[I]->GetSim())) { Air->Score.Messages.OnPush = nullptr; }
			if (FSSXRaceCamera* Cam = Pawns[I]->GetRaceCamera()) { Cam->OnScriptOp = nullptr; }
			if (FSSXReplayCamera* RC = Pawns[I]->GetReplayCamera())
			{
				RC->NumRacers = nullptr;
				RC->CutPlayerCamera = nullptr;
				RC->OnNarrationStop = nullptr;
				RC->GetCam().NumRacers = nullptr;
				RC->GetCam().RacerInput = nullptr;
				RC->GetCam().ReplayFrozen = nullptr;
			}
			Pawns[I]->ReplayPlayWord = nullptr;
			Pawns[I]->ReplayRecordWord = nullptr;
			Pawns[I]->ReplayCameraSubject = nullptr;
			Pawns[I]->RacePhaseForCamera = nullptr;
			Session.DetachRacer(I);
		}
	}
	bReady = false;
	bRaceStarted = false;
	if (SSXRaceUI::GetHost(GetWorld()) == &UIHost)
	{
		SSXRaceUI::SetHost(GetWorld(), nullptr);
	}
	Super::EndPlay(Reason);
}

bool ASSXRaceManager::Setup()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ASnowboarderPawn* Player = PC ? Cast<ASnowboarderPawn>(PC->GetPawn()) : nullptr;
	if (!Player || !Player->HasSim())
	{
		return false;
	}
	int32 N = FMath::Clamp(NumRacers, 1, SSXRace::MaxRacers);
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode(FMath::Clamp(EventMode, 0, 9));
	Cfg.Difficulty = Difficulty;
	// 0x16D300
	TArray<int32> Characters = AICharacters;
	int32 GridPlayerIndex = PlayerRacerIndex;
	SSXIntro::FSlot Slots[3];
	SSXIntro::EEntry Entry = SSXIntro::EEntry::Normal;
	double PlayerStartMeter = 0.0;
	TArray<int32> GridRiders;
	TArray<int32> SetupRiders;   // 0x165CF0, 0x167EA0
	USSXFrontEndSubsystem* FESub = USSXFrontEndSubsystem::Get(this);
	// 0x166870, 0x167060
	SSXReplayFile::FSetup LoadedSetup;
	bLoadedReplayRace = Cfg.Mode != ERaceEventMode::Lesson && FESub && FESub->PendingReplay.Num() > 0
		&& SSXReplayFile::ReadSetup(FESub->PendingReplay, LoadedSetup) && LoadedSetup.NumRacers >= 1;
	if (FESub && FESub->bHasSelection && !bLoadedReplayRace)
	{
		// 0x2657A8, 0x165558
		N = SSXSettings::LaunchRacerCount(int32(Cfg.Mode), FMath::Max(1, FESub->Launch.Players), FESub->Profile.Settings.MaxBoarders,
			FESub->Launch.Racers);
	}
	const SSXHeats::FData& Heats = SSXHeats::FData::Get();
	// 0x168720
	SSXHeats::FStoryResult IntroStory;
	bool bIntroStory = false;
	int32 IntroHeatWord = SSXIntroDialogue::SingleRaceHeatWord;
	const int32 IntroPlayerChar = FESub && FESub->bHasSelection ? FESub->Character : Player->Character;
	if (bLoadedReplayRace)
	{
		N = FMath::Clamp(LoadedSetup.NumRacers, 1, FMath::Min(int32(SSXRace::MaxRacers), SSXReplayFile::MaxRacers));
		GridPlayerIndex = FMath::Max(0, LoadedSetup.RacerOfHuman(0));
		IntroHeatWord = LoadedSetup.HeatWord;
	}
	else if (Cfg.Mode == ERaceEventMode::CircuitRace && FESub && FESub->bHasSelection && Heats.bLoaded)
	{
		FSSXProgressSession& Sess = FESub->Profile.Session;
		auto RandA = []() { return FGameRng::StreamA().Next(); };
		const int32 PlayerChar = FESub->Character;
		const int32 Venue = TrackData.TrackId;
		if (Sess.Heat == 0)
		{
			Sess.Elim.InitHeat0(Heats, PlayerChar, Venue, int32(Cfg.Mode), RandA);
			for (int32& C : Sess.Carried) { C = -1; }
		}
		int32 Grid[SSXHeats::GridSize];
		const SSXHeats::FStoryResult Story = SSXHeats::BuildGrid(Sess.Elim, Heats, PlayerChar, Venue, int32(Cfg.Mode), Sess.Heat, RandA, Grid);
		N = SSXHeats::GridSize;
		Characters.Reset();
		for (int32 I = 0; I < SSXHeats::GridSize; ++I)
		{
			GridRiders.Add(Grid[I]);
			if (Grid[I] == PlayerChar) { GridPlayerIndex = I; }
			else { Characters.Add(Grid[I]); }
		}
		for (int32 K = 0; K < 3; ++K) { Slots[K].Type = Story.IntroTypes[K]; }
		IntroStory = Story;
		bIntroStory = true;
		IntroHeatWord = Sess.Heat;
		// 0x16CFD8
		PlayerStartMeter = double(Sess.HeatFactor);
		Entry = Sess.bNextHeatEntry ? SSXIntro::EEntry::NextHeat : SSXIntro::EEntry::Normal;
		Sess.bNextHeatEntry = false;
		UE_LOG(LogSSX, Display, TEXT("[race] WC heat %d grid %d %d %d %d %d %d (player slot %d), intro %s / %s / %s"), Sess.Heat,
			Grid[0], Grid[1], Grid[2], Grid[3], Grid[4], Grid[5], GridPlayerIndex, *SSXIntro::ScriptName(Slots[0].Type),
			*SSXIntro::ScriptName(Slots[1].Type), *SSXIntro::ScriptName(Slots[2].Type));
	}
	else if (Heats.bLoaded && FESub && FESub->bHasSelection
		&& (Cfg.Mode == ERaceEventMode::PracticeKnockdown || Cfg.Mode == ERaceEventMode::PracticeFreeride || Cfg.Mode == ERaceEventMode::PracticeRace
			|| Cfg.Mode == ERaceEventMode::PracticeShowoff || Cfg.Mode == ERaceEventMode::PracticeTimedRace))
	{
		// 0x1664B0
		int32 Grid[SSXHeats::GridSize], Human[SSXHeats::GridSize];
		N = FMath::Min(N, int32(SSXHeats::GridSize));
		IntroStory = SSXHeats::SingleEventGrid(Heats, { IntroPlayerChar }, N, TrackData.TrackId, int32(Cfg.Mode),
			[]() { return FGameRng::StreamA().Next(); }, Grid, Human);
		bIntroStory = true;
		Characters.Reset();
		for (int32 I = 0; I < N; ++I)
		{
			if (Human[I] == 0) { GridPlayerIndex = I; }
			else { Characters.Add(Grid[I]); }
			SetupRiders.Add(Grid[I]);
		}
		UE_LOG(LogSSX, Display, TEXT("[race] single-event grid %d %d %d %d %d %d (N %d, player slot %d)"), Grid[0], Grid[1], Grid[2], Grid[3],
			Grid[4], Grid[5], N, GridPlayerIndex);
	}
	else if (Heats.bLoaded && Cfg.Mode != ERaceEventMode::Lesson)
	{
		// 0x165778, 0x165A40, 0x165B98, 0x165688
		IntroStory = SSXHeats::StoryLookup(Heats, IntroPlayerChar, TrackData.TrackId, int32(Cfg.Mode), SSXIntroDialogue::SingleRaceHeatWord,
			[]() { return FGameRng::StreamA().Next(); });
		bIntroStory = true;
	}
	const int32 PlayerIndex = FMath::Clamp(GridPlayerIndex, 0, N - 1);
	if (const TSharedPtr<FPatchWorld> SimWorld = Player->GetSharedPatchWorld())
	{
		SimWorld->bAlternateSetMode = SSXRace::IsScoreMode(Cfg.Mode);
		SSXShowOff::ApplyDrawRules(GetWorld(), *SimWorld, SimWorld->bAlternateSetMode);
	}
	if (!Session.Init(TrackData, Cfg))
	{
		StatusText = TEXT("race data has no paths for this mode");
		bFailed = true;
		return false;
	}

	Pawns.Reset();
	int32 NextAI = 0;
	FGameRng& BoardRng = FGameRng::StreamA();
	const FTransform At = Player->GetActorTransform();
	for (int32 I = 0; I < N; ++I)
	{
		if (I == PlayerIndex)
		{
			if (bLoadedReplayRace && Player->HasSim())
			{
				// 0x16A9D8
				Player->ProfileOverride = SSXReplayFile::RacerProfile(LoadedSetup.Racers[I]);
				Player->ApplyRiderProfile(*Player->GetSim());
			}
			Pawns.Add(Player);
			continue;
		}
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASnowboarderPawn* AI = GetWorld()->SpawnActor<ASnowboarderPawn>(ASnowboarderPawn::StaticClass(), At, Params);
		if (!AI)
		{
			continue;
		}
		const int32 K = NextAI++;
		AI->bAIRider = true;
		AI->bRaceDriven = true;
		AI->bStartRestarts = false;
		if (bLoadedReplayRace)
		{
			const SSXReplayFile::FRacer& FR = LoadedSetup.Racers[I];
			AI->ProfileOverride = SSXReplayFile::RacerProfile(FR);
			AI->Character = FR.Char38;
			AI->Board = AI->ProfileOverride->Board;
			AI->Outfit = FMath::Max(0, FR.Outfit());
		}
		else
		{
		AI->Character = Characters.IsValidIndex(K) ? Characters[K] : K;
		int32 AiBoard = 0, AiOutfit = 0;
		DrawComputerBoard(BoardRng, AiBoard, AiOutfit);
		AI->Board = AIBoards.IsValidIndex(K) && AIBoards[K] >= 0 ? AIBoards[K] : AiBoard;
		AI->Outfit = AiOutfit;
		}
		if (const USSXFrontEndSubsystem* FE = USSXFrontEndSubsystem::Get(this); FE && FE->bHasSelection && !bLoadedReplayRace)
		{
			if (const FSSXCareer* Car = FE->Profile.SlotCareer(0); Car && AI->Character >= 0 && AI->Character < SSXProg::NumRiders)
			{
				AI->ComputerLevel = Car->Opponent[AI->Character][0];
			}
		}
		AI->CharacterId = SSXCharacters::IdForIndex(AI->Character);
		AI->SetSharedWorld(Player->GetSharedPatchWorld());
		AI->FinishSpawning(At);
		if (!AI->HasSim())
		{
			AI->Destroy();
			continue;
		}
		Pawns.Add(AI);
	}
	// FUN_00168B88
	const SSXIntroDialogue::FData& Dialogue = SSXIntroDialogue::FData::Get();
	IntroFocus = -1;
	if (bIntroStory && Dialogue.bLoaded)
	{
		if (Cfg.Mode == ERaceEventMode::CircuitShowoff && FESub)
		{
			IntroHeatWord = FESub->Profile.Session.Heat;   // 0x16D230
		}
		SSXIntroDialogue::FContext Ctx = SSXIntroDialogue::FContext::FromStory(IntroStory, IntroPlayerChar);
		TArray<SSXIntroDialogue::FRacer> IntroRacers;
		for (const TObjectPtr<ASnowboarderPawn>& P : Pawns)
		{
			IntroRacers.Add({ P ? P->Character : 0, SSXIntroDialogue::NormalType80, P == Player });
		}
		IntroFocus = SSXIntroDialogue::BuildSlots(Ctx, IntroRacers, TrackData.TrackId, int32(Cfg.Mode), IntroHeatWord, Heats, Dialogue,
			[]() { return FGameRng::StreamA().Next(); }, RaceStateSlots);
		for (int32 K = 0; K < 3; ++K) { Slots[K].Type = RaceStateSlots[K].Type; }
		UE_LOG(LogSSX, Display, TEXT("[race] intro slots %s / %s / %s (ids %d %d %d, variants %d %d %d)"), *SSXIntro::ScriptName(Slots[0].Type),
			*SSXIntro::ScriptName(Slots[1].Type), *SSXIntro::ScriptName(Slots[2].Type), Ctx.Ids[0], Ctx.Ids[1], Ctx.Ids[2], Ctx.Variants[0],
			Ctx.Variants[1], Ctx.Variants[2]);
	}
	if (bLoadedReplayRace)
	{
		for (int32 K = 0; K < SSXIntroDialogue::NumSlots; ++K) { RaceStateSlots[K] = LoadedSetup.Slots[K]; }
		IntroFocus = LoadedSetup.Focus;
		for (int32 K = 0; K < 3; ++K) { Slots[K].Type = RaceStateSlots[K].Type; }
	}
	// 0x163980
	uint32 Seed2B04 = 0, Seed2B00 = 0;
	if (bLoadedReplayRace)
	{
		Seed2B04 = LoadedSetup.Seed2B04;
		Seed2B00 = LoadedSetup.Seed2B00;
	}
	else
	{
		if (TestBeforeSeed) { TestBeforeSeed(); }
		FGameRng& A = FGameRng::StreamA();
		Seed2B04 = A.Next();
		Seed2B00 = A.Next();
	}
	FGameRng::StreamB().Seed(Seed2B00);
	for (ASnowboarderPawn* P : Pawns)
	{
		const int32 Idx = Session.AddRacer(P->GetSim(), P == Player);
		AddTickPrerequisiteActor(P);   // the pads are sampled in the pawns' Tick before the race steps
		if (Idx >= 0 && P == Player)
		{
			Session.GetRacerMutable(Idx).StartMeter = PlayerStartMeter;
		}
	}
	// 0x16C008
	if (bLoadedReplayRace)
	{
		for (int32 I = 0; I < Session.NumRacers() && I < SSXReplayFile::MaxRacers; ++I)
		{
			Session.GetRacerMutable(I).StartMeter = double(SSXReplayFile::StartMeter(LoadedSetup.Racers[I]));
			for (int32 J = 0; J < Session.NumRacers() && J < SSXReplayFile::MaxRacers; ++J)
			{
				FRacerRecord& Rec = Session.GetRacerMutable(I);
				SSXRace::FRivalry& Setup = Rec.RivalSetup[J];
				const uint8* B = LoadedSetup.Racers[I].Rival[J];
				Setup.Level = B[0];
				Setup.LevelMax = B[1];
				Setup.AngerLimit = B[2];
				Setup.Kind = B[3];
				SSXRace::ResetRivalry(Rec.Rivals[J], Setup);
			}
		}
	}
	else if ((GridRiders.Num() == Session.NumRacers() || SetupRiders.Num() == Session.NumRacers()) && FESub && Heats.bLoaded)
	{
		const FSSXProgressSession& Sess = FESub->Profile.Session;
		int32 Career[SSXHeats::NumRiders] = {};
		if (const FSSXCareer* Car = FESub->Profile.SlotCareer(0))
		{
			for (int32 R = 0; R < SSXHeats::NumRiders; ++R) { Career[R] = Car->Opponent[R][1]; }
		}
		const bool bHeat = GridRiders.Num() == Session.NumRacers();
		const TArray<int32>& Riders = bHeat ? GridRiders : SetupRiders;
		TArray<TArray<int32>> Levels;
		if (bHeat) { SSXHeats::InitialLevels(Heats, Riders, PlayerIndex, Sess.Heat, Sess.Carried, Career, Levels); }
		else { SSXHeats::SetupLevels(Heats, Riders, PlayerIndex, Career, Levels); }
		for (int32 I = 0; I < Session.NumRacers(); ++I)
		{
			for (int32 J = 0; J < Session.NumRacers(); ++J)
			{
				// 0x116AB8
				const int32 Me = Riders[I], Other = Riders[J];
				FRacerRecord& Rec = Session.GetRacerMutable(I);
				SSXRace::FRivalry& Setup = Rec.RivalSetup[J];
				Setup.Level = Levels[I][J];
				Setup.LevelMax = Heats.Agg[Me][Other][2];
				Setup.AngerLimit = Heats.Agg[Me][Other][0];
				Setup.Kind = Heats.Agg[Me][Other][3];
				SSXRace::ResetRivalry(Rec.Rivals[J], Setup);
			}
		}
		if (bHeat) { HeatRiders = GridRiders; }
	}
	// FUN_00169698
	IntroObject = SSXIntroDialogue::FObject();
	Scene.Stop();
	if (IntroFocus >= 0 || bIntroStory)
	{
		SSXIntroDialogue::CopyToObject(RaceStateSlots, IntroObject, []() { return FGameRng::StreamA().Next(); });
		for (int32 K = 0; K < 3; ++K)
		{
			Slots[K].Lines.Reset();
			for (int32 J = 0; J < SSXIntroDialogue::NumLines; ++J)
			{
				const SSXIntroDialogue::FObjLine& L = IntroObject.Lines[K][J];
				Slots[K].Lines.Add({ L.Speaker, L.LineId, RaceStateSlots[K].Lines[J].Aux, L.Speech });
			}
		}
	}
	for (int32 K = 0; K < 3; ++K) { Session.IntroSlots[K] = Slots[K]; }
	// 0x16BAA8
	for (int32 K = 0; K < SSXEndRace::NumSlots; ++K)
	{
		SSXIntro::FSlot& E = Session.RaceSlots[K];
		E.Type = RaceStateSlots[K].Type;
		E.Lines.Reset();
		for (int32 J = 0; J < SSXIntroDialogue::NumLines; ++J)
		{
			const SSXIntroDialogue::FObjLine& L = IntroObject.Lines[K][J];
			E.Lines.Add({ L.Speaker, L.LineId, RaceStateSlots[K].Lines[J].Aux, L.Speech });
		}
	}
	Session.EndFocus = IntroFocus;
	{
		const int32 Track = TrackData.TrackId;
		Session.ShowoffMedalOf = [Track](int32 Score) { return SSXProg::ShowoffMedal(Score, Track); };
		const USSXFrontEndSubsystem* FE = USSXFrontEndSubsystem::Get(this);
		Session.TimeToBeatCs = FE && Track >= 0 && Track < SSXProg::NumTracks ? FE->Profile.Records[Track].Tables[2].Rows[0].Value : MAX_int32;
	}
	Session.IntroLineKey = [](int32 LineId)
	{
		const SSXIntroDialogue::FLineClip* C = SSXIntroDialogue::FData::Get().Clip(LineId);
		return C ? C->Key() : -1;
	};
	if (bLoadedReplayRace)
	{
		ReplaySetup = LoadedSetup;
	}
	else
	{
		ReplaySetup = SSXReplayFile::FSetup();
		ReplaySetup.NumRacers = Pawns.Num();
		const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
		for (int32 I = 0; I < Pawns.Num() && I < SSXReplayFile::MaxRacers; ++I)
		{
			const ASnowboarderPawn* P = Pawns[I];
			if (!P || !P->HasSim()) continue;
			const FRiderProfile& Prof = P->GetSim()->Profile;
			const bool bHuman = P == Player;
			const FString Name = bHuman && FESub && FESub->bHasSelection ? FESub->Launch.Slots[0].UserName
				: (Table.IsLoaded() ? Table.Rider(Prof.Character).Name : FString());
			const int32 Pad = bHuman && FESub ? SSXSettings::PadLayout(FESub->Profile.Settings, 0, int32(Cfg.Mode)) : 0;
			ReplaySetup.Racers[I] = SSXReplayFile::MakeRacer(Prof, I, bHuman ? GetHumanPlayer(I) : -1, P->Outfit, Pad, Name,
				Cfg.Mode == ERaceEventMode::Lesson);
			if (I < Session.NumRacers())
			{
				SSXReplayFile::SetStartMeter(ReplaySetup.Racers[I], float(Session.GetRacer(I).StartMeter));
			}
			for (int32 J = 0; J < Session.NumRacers() && J < SSXReplayFile::MaxRacers; ++J)
			{
				const SSXRace::FRivalry& R = Session.GetRacer(I).RivalSetup[J];
				uint8* B = ReplaySetup.Racers[I].Rival[J];
				B[0] = uint8(R.Level); B[1] = uint8(R.LevelMax); B[2] = uint8(R.AngerLimit); B[3] = uint8(R.Kind);
			}
		}
		ReplaySetup.Venue = TrackData.TrackId;
		ReplaySetup.GameMode = int32(Cfg.Mode);
		ReplaySetup.Seed2B00 = Seed2B00;
		ReplaySetup.Seed2B04 = Seed2B04;
		ReplaySetup.TimeToBeat = Cfg.Mode == ERaceEventMode::PracticeTimedRace && Session.TimeToBeatCs != MAX_int32 ? Session.TimeToBeatCs : 0;
		ReplaySetup.HeatWord = IntroHeatWord;
		ReplaySetup.Difficulty = Difficulty;
		for (int32 K = 0; K < SSXIntroDialogue::NumSlots; ++K) { ReplaySetup.Slots[K] = RaceStateSlots[K]; }
		ReplaySetup.Focus = IntroFocus;
	}
	// 0x17E148, 0x18E850
	bRaceEndRoutine = false;
	if (Cfg.Mode != ERaceEventMode::Lesson)
	{
		const USSXFrontEndSubsystem* ReplayFE = USSXFrontEndSubsystem::Get(this);
		ReplayWorld.Keys.Reset();
		ReplayWorld.SnapshotChecks.Reset();
		ReplayWorld.PortStreamBEqual = ReplayWorld.PortStreamBDiffers = 0;
		ReplayWorld.PortFirstDiffer = MAX_uint32;
		Replay.Init(Pawns.Num(), 1, ReplayFE && ReplayFE->Profile.Settings.Replay == 1, &ReplayWorld);
		// 0x150E38
		for (int32 I = 0; I < Pawns.Num(); ++I) { if (Pawns[I] == Player) Replay.AttachHuman(GetHumanPlayer(I)); }
		BindReplayInput();
		if (bLoadedReplayRace)
		{
			// 0x180C70, 0x1668A0, 0x194910, 0x166840
			bLoadedReplayRace = SSXReplayFile::ReadReplay(FESub->PendingReplay, Replay);
			if (bLoadedReplayRace) { Replay.BeginRebuild(); }
			else { Replay.ResetIdle(); }
		}
	}
	if (FESub) { FESub->PendingReplay.Reset(); }
	if (USSXTrackEffects* Fx = USSXTrackEffects::Get(this))
	{
		Fx->BeginTrack(UWorld::RemovePIEPrefix(GetWorld()->GetMapName()), TrackData.TrackId, int32(Cfg.Mode), Player->GetSharedPatchWorld());
		for (int32 I = 0; I < Pawns.Num(); ++I)
		{
			Fx->AddRider(Pawns[I]->GetSim(), Pawns[I] == Player, I);
		}
		Fx->LapsLeftOf = [this](int32 Slot) { return Session.GetLapsLeft(Slot); };
		Fx->RollSnowfall(1, Pawns.Num());
	}
	Session.OnCameraScript = [this](int32 Racer, const TCHAR* Script)
	{
		ASnowboarderPawn* P = GetPawn(Racer);
		FSSXRaceCamera* Cam = P ? P->GetRaceCamera() : nullptr;
		const bool bOk = Cam && Cam->PlayScript(Script);
		UE_LOG(LogSSX, Display, TEXT("[race] camera script %s for racer %d: %s"), Script, Racer, bOk ? TEXT("playing") : TEXT("not found"));
	};
	Session.IsIntroBusy = [this]()
	{
		const ASnowboarderPawn* P = GetPlayerPawn();
		const FSSXRaceCamera* Cam = P ? P->GetRaceCamera() : nullptr;
		return Cam && Cam->IsScriptRunning();
	};
	if (FSSXRaceCamera* Cam = Player->GetRaceCamera())
	{
		Cam->OnScriptOp = [this](int32 Op, int32 Arg)
		{
			if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(this)) { SSXGameAudio::OnCameraOp(*A, Op, Arg); }
		};
		Cam->OnSceneOp = [this](int32 Op, const FString& Name) { OnSceneOp(Op, Name); };
		// FUN_00173c50
		Cam->NumRacers = [this]() { return Session.NumRacers(); };
		Cam->OwnRacer = [this]() { return Pawns.IndexOfByKey(GetPlayerPawn()); };
		Cam->RacerInput = [this](int32 Racer, FSSXCameraRiderInput& Out)
		{
			ASnowboarderPawn* P = GetPawn(Racer);
			return P && P->GetCameraInput(Out);
		};
	}
	// 0x3368B0
	if (FSSXReplayCamera* RC = Player->GetReplayCamera())
	{
		FSSXRaceCamera& RCam = RC->GetCam();
		RCam.NumRacers = [this]() { return Session.NumRacers(); };
		RCam.RacerInput = [this](int32 Racer, FSSXCameraRiderInput& Out)
		{
			ASnowboarderPawn* P = GetPawn(Racer);
			return P && P->GetCameraInput(Out);
		};
		RCam.ReplayFrozen = [this]()
		{
			const int32 St = Replay.GetState();
			return St == SSXReplay::Paused || (St != 0 && St < 10 && Replay.GetPlayFrame() == Replay.GetRecordedLength());
		};
		RC->NumRacers = [this]() { return Session.NumRacers(); };
		RC->CutPlayerCamera = [this]()
		{
			if (ASnowboarderPawn* P = GetPlayerPawn()) { if (FSSXRaceCamera* C = P->GetRaceCamera()) { C->MarkCut(); } }
		};
		RC->OnNarrationStop = [this]()
		{
			// 0x175EB8, FUN_00231318
			if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(this)) { SSXGameAudio::OnCameraOp(*A, 0x22, 0); }
		};
	}
	// 0x235780
	Scene.OnEvent = [this](int32 Racer, int32 Value)
	{
		USSXAudioSubsystem* A = USSXAudioSubsystem::Get(this);
		const ASnowboarderPawn* P = GetPawn(Racer);
		if (A && P)
		{
			A->OnAnimEvent(Value, Racer, P->GetActorLocation(), !P->bAIRider);
		}
	};
	Scene.OnPlace = [this](int32 Racer, const FVector& Pos, double Yaw)
	{
		// FUN_0011CAE8
		ASnowboarderPawn* P = GetPawn(Racer);
		if (P && P->HasSim())
		{
			P->GetSim()->Spawn(Pos, FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.0), FVector::ZeroVector, 0.0, true);
		}
	};
	Scene.OnLineStart = [this](int32 Racer, int32 LineId)
	{
		// 0x169BE8
		ASnowboarderPawn* P = GetPawn(Racer);
		SSXAnim::FRiderAnimChannel* Ch = P && P->HasSim() ? SSXAnim::FindChannel(*P->GetSim()) : nullptr;
		if (Ch)
		{
			Ch->RequestScenePlay(LineId);
		}
	};
	if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(*Player->GetSim()))
	{
		FRiderSim* PlayerSim = Player->GetSim();
		Air->Score.Messages.OnPush = [this, PlayerSim](int32 Type, int32 Value)
		{
			if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(this))
			{
				SSXGameAudio::OnRiderMessage(*A, *PlayerSim, Type, Value);
			}
		};
		// FUN_001579E0, FUN_00154638, 0x14C2A8
		ASnowboarderPawn* BookPawn = Player;
		Air->Score.BookCheck = [this, BookPawn](uint32 W0, uint32 W1)
		{
			if (!bLesson && Replay.IsPlaying())
			{
				return false;
			}
			if (bLesson)
			{
				const USSXFrontEndSubsystem* FE = USSXFrontEndSubsystem::Get(this);
				const int32 BookChar = FE && FE->bHasSelection ? FE->Profile.Slot[0].Char : (BookPawn ? BookPawn->Character : 0);
				SSXLesson::LessonBookCheck(Lesson, BookChar, W0, W1);
				return false;
			}
			FRiderSim* LogSim = BookPawn && BookPawn->HasSim() ? BookPawn->GetSim() : nullptr;
			auto Note = [LogSim](const FString& S) { if (LogSim) { LogSim->LogNote(S); } };
			if (Session.GetMode() == ERaceEventMode::Lesson) { Note(TEXT("book: rejected (lesson mode)")); return false; }
			USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this);
			FSSXCareer* Car = Sub ? Sub->Profile.SlotCareer(0) : nullptr;
			if (!Car) { Note(TEXT("book: rejected (no front-end profile / career slot)")); return false; }
			const uint32 Set = Sub->Profile.LandBookTrick(0, W0, W1);
			if (Set == 0)
			{
				Note(FString::Printf(TEXT("book: rejected (%s)"), *SSXFeTrickBook::ExplainNoBook(SSXFeTrickBook::FTable::Get(), *Car, W0, W1)));
				return false;
			}
			for (int32 K = 0; K < 32; ++K)
			{
				if (Set & (1u << K)) { Note(FString::Printf(TEXT("book: +bit %d (rider %d, career +0x1C %08x)"), K, Car->BookChar, Car->BookDone[1])); }
			}
			if (USSXBoardSoundComponent* Snd = BookPawn ? BookPawn->GetBoardSound() : nullptr) { Snd->TrickBookFirst(); }   // 0x2190F0
			return true;
		};
	}
	UIHost.IntroCards = 0;
	UIHost.SkipPresses = 0;
	Session.IsSkipPressed = [this]()
	{
		if (UIHost.SkipPresses > 0) { --UIHost.SkipPresses; return true; }
		return false;
	};
	// 0x112AF0, 0x164840
	Session.IsHelpNeeded = [this]()
	{
		const USSXFrontEndSubsystem* FE = USSXFrontEndSubsystem::Get(this);
		if (!FE) return false;
		for (int32 I = 0; I < Session.NumRacers(); ++I)
		{
			const FRacerRecord& R = Session.GetRacer(I);
			const int32 Char = R.Sim ? R.Sim->Profile.Character : -1;
			if (R.bHuman && Char >= 0 && Char < SSXProg::NumRiders && SSXProg::HelpRaceNeeded(FE->Profile.Career[Char].RankPoints()))
			{
				return true;
			}
		}
		return false;
	};
	Session.IsReplayLooping = [this]() { return Replay.IsLooping(); };
	Session.IsReplayOverlayUp = [this]()
	{
		const ASSXPlayerController* PC = Cast<ASSXPlayerController>(GetWorld()->GetFirstPlayerController());
		USSXFeWidget* W = PC ? PC->GetOverlayWidget() : nullptr;
		return W && W->GetOverlays().IsActive(OvReplay);
	};
	Session.OnDirectorReset = [this]()
	{
		ASnowboarderPawn* P = GetPlayerPawn();
		if (FSSXRaceCamera* Cam = P ? P->GetRaceCamera() : nullptr)
		{
			// FUN_00175eb8, 0x110AB8, 0x110A40
			if (Cam->ResetDirector())
			{
				if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(this)) { SSXGameAudio::OnCameraOp(*A, 0x22, 0); }
			}
		}
		Scene.Stop();   // FUN_00176F38
	};
	Session.OnIntroAction = [this](const FString& Name, int32 Arg, const FString& Text)
	{
		const int32 Bit = Arg == SSXIntro::OverlayHelpRace ? 1 : (Arg == SSXIntro::OverlayNextRace ? 2 : 0);
		if (Name == TEXT("overlay_open"))
		{
			UIHost.IntroCards |= Bit;
			if (Arg == SSXIntro::OverlayNextRace) { UIHost.SkipPresses = 0; }
		}
		else if (Name == TEXT("overlay_close")) { UIHost.IntroCards &= ~Bit; }
		else if (Name == TEXT("overlays_close_all")) { UIHost.IntroCards = 0; }
		else if (Name == TEXT("slot_start")) { IntroObject.Slot = Arg; }   // 0x1699A0
		else if (Name == TEXT("finish_event"))
		{
			UIHost.SkipPresses = 0;
			UIHost.bPostRaceChain = false;
			UIHost.FinishEvent();     // 0x164FC0
		}
		else if (Name == TEXT("post_race_chain")) { UIHost.bPostRaceChain = true; UIHost.bFirstChainOverlay = true; }
		else if (Name == TEXT("replay_state") && !bLesson)
		{
			// 0x193C68, 0x210A38
			if (Replay.OnEndRaceAttract())
			{
				RestartBoardVoices();
			}
		}
		else if (Name == TEXT("ambient_enable"))
		{
			// 0x22E400
			if (!bLoadedReplayRace)
			{
				if (USSXTrackEffects* Fx = USSXTrackEffects::Get(this)) { Fx->SetAmbientEnabled(Arg != 0, Arg == 2); }
			}
		}
		USSXAudioSubsystem* A = USSXAudioSubsystem::Get(this);
		if (!A) { return; }
		// 0x1109B0, 0x2355C8, 0x2356F0, 0x110980
		if (Name == TEXT("overlay_open") && Arg == SSXIntro::OverlayNextRace)
		{
			// 0x231FF8
			int32 Human = -1, Humans = 0, Strong = 0, Weak = 0;
			for (int32 I = 0; I < Session.NumRacers(); ++I) { if (Session.GetRacer(I).bHuman) { ++Humans; if (Human < 0) Human = I; } }
			for (int32 I = 0; Human >= 0 && I < Session.NumRacers(); ++I)
			{
				if (Session.GetRacer(I).bHuman) continue;
				const double Lv = Session.GetRacer(I).Rivals[Human].Level;
				Strong += Lv > 80.0 ? 1 : 0;
				Weak += Lv <= 20.0 ? 1 : 0;
			}
			A->GetAnnouncer().PreRaceCard(A->GetSpeech(), A->IsChannelFree(0), Strong, Weak, Humans);
		}
		if (Name == TEXT("intro_speech_begin")) { A->IntroSpeechBegin(); }
		else if (Name == TEXT("intro_speech_end")) { A->IntroSpeechEnd(); }
		else if (Name == TEXT("anim_speech")) { A->IntroSpeechPost(Arg); }
	};
	// FUN_0014AAA0
	bLesson = Cfg.Mode == ERaceEventMode::Lesson;
	Session.Lesson = nullptr;
	if (bLesson)
	{
		Lesson.Construct(Player->Character);
		Session.Lesson = &Lesson;
		Session.IsLessonPassed = [this]() { return Lesson.bPassed; };
		Player->LessonPadInject = [this](FSSXPadFrame& Pad) { return Lesson.InjectPad(Pad); };
		Player->LessonPadDone = [this]() { Lesson.AfterPad(); };
		UE_LOG(LogSSX, Display, TEXT("[lesson] lesson object for rider %d"), Player->Character);
	}
	if (bLoadedReplayRace)
	{
		// 0x180D08, 0x1EF990
		Entry = SSXIntro::EEntry::Restart;
	}
	Session.Start(Entry);
	if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this)) { Pw->ResetRiderEffects(); }   // 0x111108, 0x1128B8
	// 0x112B88, 0x167010
	if (Entry == SSXIntro::EEntry::Normal && FESub && FESub->bHasSelection && Player->Character >= 0 && Player->Character < SSXProg::NumRiders)
	{
		SSXProg::CountEvent(FESub->Profile.Career[Player->Character], TrackData.TrackId);
	}
	Clock.Reset();
	LastPlacements = Session.GetPlacements();
	SSXRaceUI::SetHost(GetWorld(), &UIHost);
	// FUN_002116D8
	StartMode = int32(Cfg.Mode);
	StartHeatWord = IntroHeatWord;
	StartPlayerChar = IntroPlayerChar;
	bRaceStarted = false;
	bReady = true;
	StatusText = FString::Printf(TEXT("race: %s, %d racers, track id %d"), LexToString(Cfg.Mode), Pawns.Num(), TrackData.TrackId);
	UE_LOG(LogSSX, Display, TEXT("[race] %s"), *StatusText);
	return true;
}

void ASSXRaceManager::GameStart()
{
	// 0x180D08, 0x17C6E8, FUN_0022E338, FUN_002116D8
	bRaceStarted = true;
	if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
	{
		// 0x32CA04
		if (const USSXFrontEndSubsystem* FE = USSXFrontEndSubsystem::Get(this); FE && FE->bHasSelection)
		{
			Audio->ArcadeSfx = FE->Profile.Settings.ArcadeSfx;
		}
		SSXGameAudio::OnRaceSetup(*Audio, UWorld::RemovePIEPrefix(GetWorld()->GetMapName()).ToLower(), TrackData.TrackId, StartMode, bLoadedReplayRace);
		Audio->GetAnnouncer().HeatWord = StartHeatWord;
		Audio->GetAnnouncer().PlayerCharacter = StartPlayerChar;
		if (const USSXFrontEndSubsystem* FE = USSXFrontEndSubsystem::Get(this); FE && FE->bHasSelection && StartPlayerChar >= 0
			&& StartPlayerChar < SSXProg::NumRiders)
		{
			Audio->GetAnnouncer().PlayerRankIndex = SSXProg::RankFromPoints(FE->Profile.Career[StartPlayerChar].RankPoints());
		}
		// 0x20F9B0
		Audio->RacerLocation = [this](int32 Racer)
		{
			const ASnowboarderPawn* P = GetPawn(Racer);
			return P ? P->GetActorLocation() : FVector::ZeroVector;
		};
		Audio->ListenerZone = [this](const FVector& GamePos)
		{
			const ASnowboarderPawn* View = GetPlayerPawn();
			const FSSXRaceCamera* Cam = View ? View->GetRaceCamera() : nullptr;
			if (!Cam)
			{
				return 0;
			}
			return FVector::DistSquared(Cam->GetPosition(), GamePos) < FMath::Square(4000.0) ? 0 : -1;   // 40 m in game cm
		};
	}
}

void ASSXRaceManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFailed)
	{
		return;
	}
	if (!bReady && !Setup())
	{
		return;
	}
	// 0x17C6E8
	const SSXRaceGate::EFrame Gate = SSXRaceGate::Next(bReady, USSXLoadingScreen::HoldsGame(this), bRaceStarted);
	if (Gate == SSXRaceGate::EFrame::Hold)
	{
		return;
	}
	if (Gate == SSXRaceGate::EFrame::Start)
	{
		GameStart();
	}
	int32 Steps = Clock.Advance(DeltaSeconds);
	const bool bRebuilding = !bLesson && Replay.IsRebuilding();
	if (bRebuilding)
	{
		Steps = ReplayRebuildStepsPerTick;
	}
	if (USSXAudioSubsystem* RebuildAudio = USSXAudioSubsystem::Get(this))
	{
		RebuildAudio->SetReplayRebuild(bRebuilding);
	}
	USSXAudioSubsystem* Audio = bRebuilding ? nullptr : USSXAudioSubsystem::Get(this);
	int32 HumanRacer = -1;
	for (int32 I = 0; I < Session.NumRacers() && HumanRacer < 0; ++I)
	{
		if (Session.GetRacer(I).bHuman) { HumanRacer = I; }
	}
	USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this);
	USSXTrackEffects* Fx = USSXTrackEffects::Get(this);
	for (int32 S = 0; S < Steps; ++S)
	{
		// 0x181638, 0x1814C0
		const bool bWorldRuns = ReplayFrame();
		if (Audio)
		{
			// 0x21BFD0, 0x20ED20, 0x193CE0
			const bool bPlaying = !bLesson && Replay.IsPlaying();
			const int32 Target = bPlaying ? GetViewRacer() : INDEX_NONE;
			Audio->SetReplayPlayback(bPlaying, Target, Pawns.IsValidIndex(Target) ? static_cast<const UObject*>(Pawns[Target]) : nullptr);
		}
		if (!bWorldRuns)
		{
			continue;   // 0x18F020
		}
		if (Fx)
		{
			Fx->BeginFrame();   // 0x191298
			const ASnowboarderPawn* View0 = GetPlayerPawn();
			const FSSXRaceCamera* Cam = View0 ? View0->GetRaceCamera() : nullptr;
			const FVector CamPos = Cam ? Cam->GetPosition() : FVector::ZeroVector;
			Fx->StepCells(Session.GetRequestedPhase(), Cam ? &CamPos : nullptr);   // 0x115E28
			Fx->StepFrame();
		}
		if (PropAnim)
		{
			PropAnim->StepFrame();
		}
		if (bLesson)
		{
			FRaceLessonEnv LessonEnv(*this);
			Lesson.Update(LessonEnv);
		}
		const ERacePhase PrevPhase = Session.GetPhase();
		Session.SetLoadedReplay(!bLesson && Replay.IsLoaded());
		Session.Tick();
		if (PrevPhase != ERacePhase::StartRace && Session.GetPhase() == ERacePhase::StartRace && !bLesson)
		{
			Replay.OnStartRace();   // 0x113478
		}
		if (PrevPhase != ERacePhase::Countdown && Session.GetPhase() == ERacePhase::Countdown && !(!bLesson && Replay.IsRebuilding()))
		{
			// 0x113520, 0x11A7D0
			++UIHost.HudOpens;
		}
		if (PrevPhase != ERacePhase::Countdown && Session.GetPhase() == ERacePhase::Countdown && Session.GetClock() < 0.0
			&& !(!bLesson && Replay.IsRebuilding()))
		{
			// 0x18F6C0
			++UIHost.CountdownOpens;
		}
		if (PrevPhase != ERacePhase::EndRace && Session.GetPhase() == ERacePhase::EndRace)
		{
			CarryRivalries();   // 0x16C228
		}
		if (!bLesson && !bRaceEndRoutine && (Session.GetRequestedPhase() == ERacePhase::EndRace
			|| Session.GetRequestedPhase() == ERacePhase::ReplayEndRace))
		{
			// 0x113820, 0x111738, 0x191230
			bRaceEndRoutine = true;
			Replay.EndRecording();
		}
		if (Fx)
		{
			Fx->OnRaceFrame(PrevPhase, Session.GetPhase(), Session.GetClock());
		}
		if (Audio)
		{
			SSXGameAudio::RaceFrame(*Audio, Session, HumanRacer, PrevPhase, !bLesson && Replay.IsPlaying());
		}
		const bool bPlaced = Session.GetPlacements() != LastPlacements;
		LastPlacements = Session.GetPlacements();
		if (bPlaced && Fx)
		{
			Fx->OnRacersPlaced();   // 0x116AB8
		}
		for (int32 I = Pawns.Num() - 1; I >= 0; --I)   // the boarders update last racer first, as the original
		{
			if (ASnowboarderPawn* P = Pawns[I])
			{
				P->RaceSimFrame();
			}
		}
		for (ASnowboarderPawn* P : Pawns)
		{
			if (P)
			{
				P->RacePostAI();
			}
		}
		if (!bLesson && Session.GetPhase() == ERacePhase::Race && !Replay.IsPlaying())
		{
			// 0x114730, 0x113A88
			for (ASnowboarderPawn* P : Pawns)
			{
				if (P)
				{
					P->StepCameraReverse(P->GetReplayCamera() && P->GetReplayCamera()->IsActive());
				}
			}
		}
		for (ASnowboarderPawn* P : Pawns)
		{
			if (P)
			{
				P->RacePostFrame();
			}
		}
		for (int32 I = 0; I < Pawns.Num(); ++I)
		{
			ASnowboarderPawn* P = Pawns[I];
			if (!P || !P->HasSim())
			{
				continue;
			}
			const TArray<FRiderState::FHighlightCall> Calls = P->GetSim()->State.TakeHighlightCalls();
			for (const FRiderState::FHighlightCall& C : Calls)
			{
				if (!bLesson && !P->bAIRider) { Replay.HighlightCall(GetHumanPlayer(I), C.Slot, C.Arg); }
			}
		}
		if (bLesson)
		{
			const ASnowboarderPawn* P0 = GetPawn(0);
			const SSXAir::FAirContext* Air = P0 && P0->GetSim() ? SSXAir::GetAirContext(*P0->GetSim()) : nullptr;
			Lesson.RiderTrickText = Air ? Air->Score.TrickText : FString();
		}
		Scene.Step(IntroObject, SSXIntroDialogue::FData::Get());
		if (Fx)
		{
			Fx->PostRiders();
			// 0x1166F0, 0x22D370
			const ASnowboarderPawn* View0 = GetPlayerPawn();
			if (Audio)
			{
				// 0x20EA38, 0x155410
				const int32 L = (!bLesson && Replay.IsPlaying()) ? GetViewRacer() : HumanRacer;
				const ASnowboarderPawn* LP = GetPawn(L);
				const FRiderSim* LS = LP ? LP->GetSim() : nullptr;
				const SSXAir::FAirContext* LA = LS ? SSXAir::GetAirContext(*LS) : nullptr;
				Audio->GetCrowdVoices().SetListenerAir(LS ? float(LS->State.Prediction.TimeLeft) : 0.f,
					LA ? FMath::Max(0.f, float(LA->Score.Value) * float(SSXAir::MeterScale)) : 0.f);
			}
			if (bRebuilding)
			{
			}
			else if (const FSSXRaceCamera* Cam = View0 ? View0->GetRaceCamera() : nullptr)
			{
				FVector Fwd, Up;
				Cam->GetViewAxes(Fwd, Up);
				const FVector L = Cam->GetPosition();
				Fx->StepAmbient(&L, L + Fwd * SSXBoardAudio::EarForwardCm - Up * SSXBoardAudio::EarUpCm);
			}
			else
			{
				Fx->StepAmbient(nullptr, FVector::ZeroVector);
			}
		}
		if (bPlaced)
		{
			for (ASnowboarderPawn* P : Pawns)
			{
				if (P) { P->ResetRenderState(); }
			}
		}
		if (S >= Steps - 2)
		{
			for (ASnowboarderPawn* P : Pawns)
			{
				if (P) { P->CaptureRenderPose(); }
			}
		}
		if (TestOnFrame) { TestOnFrame(*this); }
	}
	for (ASnowboarderPawn* P : Pawns)
	{
		if (P)
		{
			P->RaceRender(Clock.Alpha(), DeltaSeconds);
		}
	}
	if (PropAnim)
	{
		PropAnim->ApplyRender();
	}
	if (Fx)
	{
		Fx->ApplyRender();
	}
	HandleCues();
	if (bLoadedReplayRace && !bLoadedReplayStarted && !Replay.IsRebuilding())
	{
		bLoadedReplayStarted = true;
		UIHost.bLoadedReplayStart = true;
	}
}

SSXIntroDialogue::FSlot ASSXRaceManager::RaceStateSlots[SSXIntroDialogue::NumSlots];

void ASSXRaceManager::OnSceneOp(int32 Op, const FString& Name)
{
	if (Op == 15)
	{
		Scene.Stop();
		return;
	}
	const ASnowboarderPawn* View = GetPlayerPawn();
	const FSSXRaceCamera* Cam = View ? View->GetRaceCamera() : nullptr;
	const FSSXCameraDefSet* Defs = Cam ? Cam->GetDefs() : nullptr;
	const TArray<FSSXSceneActor>* Actors = Defs ? Defs->FindScene(Name) : nullptr;
	if (!Actors)
	{
		UE_LOG(LogSSX, Warning, TEXT("[race] ANIMATION scene %s not found"), *Name);
		return;
	}
	// FUN_00176720
	TArray<SSXIntroDialogue::FSceneRacer> Racers;
	int32 HumanSlot = 0;
	for (int32 I = 0; I < Session.NumRacers(); ++I)
	{
		const ASnowboarderPawn* P = GetPawn(I);
		SSXIntroDialogue::FSceneRacer R;
		R.PlayerSlot = Session.GetRacer(I).bHuman ? HumanSlot++ : -1;
		R.Character = P ? P->Character : 0;
		Racers.Add(R);
	}
	Scene.Start(*Actors, *Defs, Racers, IntroObject, SSXIntroDialogue::FData::Get());
}

void ASSXRaceManager::CarryRivalries()
{
	USSXFrontEndSubsystem* FESub = USSXFrontEndSubsystem::Get(this);
	if (!FESub || Session.GetMode() != ERaceEventMode::CircuitRace || HeatRiders.Num() != Session.NumRacers())
	{
		return;
	}
	int32 Human = -1;
	for (int32 I = 0; I < Session.NumRacers(); ++I) { if (Session.GetRacer(I).bHuman) { Human = I; } }
	if (Human < 0)
	{
		return;
	}
	TArray<int32> Toward;
	for (int32 J = 0; J < Session.NumRacers(); ++J) { Toward.Add(Session.GetRacer(J).Rivals[Human].Level); }
	SSXHeats::CarryOver(HeatRiders, Human, Toward, FESub->Profile.Session.Carried);
}

void ASSXRaceManager::HandleCues()
{
	// 0x11DA40
	if (!bLesson && Replay.IsRebuilding())
	{
		Session.ClearCues();
		return;
	}
	USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
	for (const FRaceCue& C : Session.GetCues())
	{
		if (Audio && C.Racer >= 0 && C.Racer < Session.NumRacers())
		{
			SSXGameAudio::HandleRaceCue(*Audio, C, Session.GetRacer(C.Racer).bHuman);
			// 0x11A348, 0x21D0B0, 0x21D5C8
			if (C.Kind == ERaceCueKind::TriggerEnter || C.Kind == ERaceCueKind::TriggerLeave)
			{
				Audio->GetZoneAmbience().Trigger(C.Racer, C.Type, C.Kind == ERaceCueKind::TriggerEnter);
			}
			// 0x21BFD0, 0x20ED20
			SSXGameAudio::HandleSpeechCue(*Audio, Session, C);
		}
		if (C.Kind != ERaceCueKind::Finish || !Session.GetRacer(C.Racer).bHuman)
		{
			continue;
		}
		if (!bLesson && Replay.IsPlaying())
		{
			continue;
		}
		FSSXRaceUIBridge::FFinish F;
		F.Player = 0;
		F.Result = Session.GetRacer(C.Racer).Rank;
		if (SSXRace::IsScoreMode(Session.GetMode()))
		{
			// 0x1AE7F0, 0x16BAA8
			F.Result = C.Param > 0 ? 0 : -1;
			F.Value = FString::FromInt(int32(Session.GetRacer(C.Racer).Score));
		}
		else
		{
			F.Value = FSSXHudOverlay::FormatTime(C.Param);
		}
		UIHost.PendingFinish.Add(F);
	}
	if (Audio)
	{
		Audio->GetZoneAmbience().Update();   // 0x21D0C0, 0x20EE58
	}
	Session.ClearCues();
}

bool ASSXRaceManager::ReplayFrame()
{
	if (bLesson)
	{
		return true;
	}
	Replay.Update();   // 0x191298
	// 0x18F020, 0x17F078
	return !(Replay.IsPlaying() && Replay.IsWorldFrozen());
}

void ASSXRaceManager::BindReplayInput()
{
	for (int32 I = 0; I < Pawns.Num(); ++I)
	{
		ASnowboarderPawn* P = Pawns[I];
		if (!P || P->bAIRider)
		{
			continue;
		}
		// 0x151250, 0x1927D8
		const int32 Human = GetHumanPlayer(I);
		P->ReplayPlayWord = [this, Human](uint32& Word)
		{
			if (!Replay.IsPlayingInput())
			{
				return false;
			}
			Word = 0;
			Replay.RecordOrPlayInput(Human, Word);
			return true;
		};
		P->ReplayRecordWord = [this, Human](uint32 Word) { Replay.RecordOrPlayInput(Human, Word); };
		P->RacePhaseForCamera = [this]() { return uint8(Session.GetPhase()); };
		P->ReplayCameraSubject = [this, P]() -> ASnowboarderPawn*
		{
			if (const FSSXReplayCamera* RC = GetReplayCamera())
			{
				ASnowboarderPawn* T = RC->IsActive() ? GetPawn(RC->GetTarget()) : nullptr;
				return T != P ? T : nullptr;
			}
			if (!Replay.IsPlaying() || !ReplayWorld.bReplayCamera) { return nullptr; }
			ASnowboarderPawn* T = GetPawn(GetViewRacer());
			return T != P ? T : nullptr;
		};
	}
}

FSSXReplayCamera* ASSXRaceManager::GetReplayCamera() const
{
	const ASnowboarderPawn* P = GetPlayerPawn();
	return P ? P->GetReplayCamera() : nullptr;
}

int32 ASSXRaceManager::GetViewRacer() const
{
	if (const FSSXReplayCamera* RC = GetReplayCamera())
	{
		return (RC->IsActive() && Pawns.IsValidIndex(RC->GetTarget())) ? RC->GetTarget() : Pawns.IndexOfByKey(GetPlayerPawn());
	}
	if (ReplayWorld.bReplayCamera && Pawns.IsValidIndex(ReplayWorld.Camera.Target))
	{
		return ReplayWorld.Camera.Target;
	}
	return Pawns.IndexOfByKey(GetPlayerPawn());
}

void ASSXRaceManager::RestartBoardVoices()
{
	for (ASnowboarderPawn* P : Pawns)
	{
		if (USSXBoardSoundComponent* B = P ? P->GetBoardSound() : nullptr) { B->RestartVoices(); }
	}
}

void ASSXRaceManager::ReplayStartAudio()
{
	USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
	if (!Audio || bLesson)
	{
		return;
	}
	// 0x1EF990, 0x229FA0, 0x2213C8, 0x215288
	Audio->StopSpeech(0);
	Audio->StopAllSounds();
	if (USSXTrackEffects* Fx = USSXTrackEffects::Get(this)) { Fx->KillAmbientVoices(); }   // 0x22E338
	Audio->GetRaceFlow().ReplayGo();
	Audio->ExecuteRaceCommands();
	RestartBoardVoices();                     // 0x210A38
	Audio->SpeechWatch.GoFrame = Session.GetFrame();
	Audio->PauseGameAudio();                  // 0x2153F8
}

void ASSXRaceManager::ReplayExitAudio()
{
	USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
	if (!Audio || bLesson)
	{
		return;
	}
	// 0x1EF990, 0x229FA0, 0x2213C8, 0x215488, 0x225280
	Audio->StopSpeech(0);
	Audio->GetRaceFlow().Commands.Add({ SSXRaceAudio::FCommand::StopIntro, 0 });
	Audio->ExecuteRaceCommands();
	Audio->StopAllSounds();
	if (USSXTrackEffects* Fx = USSXTrackEffects::Get(this)) { Fx->KillAmbientVoices(); }
	Audio->ResumeGameAudio();
	Audio->StopMusic(0.f);
}

bool ASSXRaceManager::BuildReplayFile(TArray<uint8>& Out) const
{
	if (bLesson || !bReady || (!Replay.HasRecording() && !Replay.IsLoaded()))
	{
		return false;
	}
	return SSXReplayFile::Write(Replay, ReplaySetup, Out);
}

bool ASSXRaceManager::StartResultsReplay()
{
	// 0x1EF990, 0x191B60
	if (bLesson || !bReady || (!Replay.HasRecording() && !Replay.IsLoaded()))
	{
		return false;
	}
	ReplayWorld.SetCameraRider(true, FMath::Max(0, ReplayWorld.FocusPlayer()));
	// 0x175EB8
	if (Session.OnDirectorReset) { Session.OnDirectorReset(); }
	if (!Replay.IsLoaded())
	{
		Replay.StartFullReplay();
	}
	return true;
}

void ASSXRaceManager::StopResultsReplay()
{
	// 0x1EF990, 0x191BE0
	if (bLesson || !bReady)
	{
		return;
	}
	if (!Replay.IsLoaded())
	{
		Replay.StopPlayback();
	}
	ReplayWorld.SetCameraRider(false, 0);
}

void FSSXRaceReplayWorld::CaptureKeyframe(int32 Key, uint32 Frame, bool bDropScripts)
{
	if (Key < 0 || !Owner || !Owner->bReady)
	{
		return;
	}
	if (Keys.Num() <= Key) { Keys.SetNum(Key + 1); }
	FKey& K = Keys[Key];
	++Captures;
	K.Race.Save(Owner->Session, Frame);
	K.Fx = USSXTrackEffects::FReplayKeyframe();
	K.Props = USSXPropAnimation::FReplayKeyframe();
	K.Particles = USSXParticleWorld::FReplayKeyframe();
	if (USSXTrackEffects* Fx = USSXTrackEffects::Get(Owner))
	{
		Fx->SaveReplay(K.Fx);
		if (bDropScripts)
		{
			// 0x18F448, 0x191B60
			K.Fx.World.Controllers.Reset();
			K.Fx.World.Scripts.Reset();
			K.Fx.World.Emitters.Reset();
		}
	}
	// 0x18F5D0
	if (const USSXPropAnimation* Pa = USSXPropAnimation::Get(Owner))
	{
		if (bDropScripts) { K.Props.bValid = true; } else { Pa->SaveReplay(K.Props); }
	}
	if (const USSXParticleWorld* Pw = USSXParticleWorld::Get(Owner))
	{
		if (bDropScripts) { K.Particles.bValid = true; } else { Pw->SaveReplay(K.Particles); }
	}
	K.Anim.SetNum(Owner->Pawns.Num());
	for (int32 I = 0; I < Owner->Pawns.Num(); ++I)
	{
		const ASnowboarderPawn* P = Owner->Pawns[I];
		const SSXAnim::FRiderAnimDriver* A = P ? P->GetAnimatorForReplay() : nullptr;
		K.Anim[I] = A ? TOptional<SSXAnim::FRiderAnimDriver>(*A) : TOptional<SSXAnim::FRiderAnimDriver>();
		if (A && P->HasSim()) { K.Anim[I]->SaveEventsForReplay(*P->GetSim()); }
	}
}

void FSSXRaceReplayWorld::RestoreKeyframe(int32 Key, uint32 Frame)
{
	if (!Owner || !Owner->bReady || !Keys.IsValidIndex(Key) || !Keys[Key].Race.bValid)
	{
		return;
	}
	const FKey& K = Keys[Key];
	++Restores;
	K.Race.Load(Owner->Session);
	Owner->Session.ClearCues();
	USSXPropAnimation* Pa = USSXPropAnimation::Get(Owner);
	const TArray<int32> WasAnimated = Pa ? Pa->AnimatedInstances() : TArray<int32>();
	if (USSXTrackEffects* Fx = USSXTrackEffects::Get(Owner))
	{
		Fx->LoadReplay(K.Fx);
	}
	if (Pa)
	{
		Pa->LoadReplay(K.Props, WasAnimated);   // 0x18F800
	}
	if (USSXParticleWorld* Pw = USSXParticleWorld::Get(Owner))
	{
		Pw->LoadReplay(K.Particles);
	}
	for (int32 I = 0; I < Owner->Pawns.Num(); ++I)
	{
		ASnowboarderPawn* P = Owner->Pawns[I];
		if (!P)
		{
			continue;
		}
		if (SSXAnim::FRiderAnimDriver* A = P->GetAnimatorForReplay(); A && K.Anim.IsValidIndex(I) && K.Anim[I].IsSet())
		{
			*A = K.Anim[I].GetValue();
			if (P->HasSim()) { A->RestoreEventsForReplay(*P->GetSim()); }
		}
		P->ResetRenderState();
		USSXBoardSoundComponent* B = P->GetBoardSound();
		if (B && P->HasSim())
		{
			B->OnRiderStateRestored(P->GetSim()->State);
		}
	}
	Owner->LastPlacements = Owner->Session.GetPlacements();
	Owner->bRaceEndRoutine = Owner->Session.GetRequestedPhase() == ERacePhase::EndRace
		|| Owner->Session.GetRequestedPhase() == ERacePhase::ReplayEndRace;
}

void FSSXRaceReplayWorld::ResetRiderEffects()
{
	// 0x1128B8
	if (USSXParticleWorld* Pw = Owner && Owner->bReady ? USSXParticleWorld::Get(Owner) : nullptr)
	{
		Pw->ResetRiderEffects();
	}
}

SSXReplay::FCamera FSSXRaceReplayWorld::GetCamera() const
{
	// 0x178968, 0x1786C0
	if (const FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr)
	{
		SSXReplay::FCamera C;
		RC->Get(C.A, C.B, C.C, C.D, C.Target);
		C.Mode = RC->GetMode();
		return C;
	}
	return Camera;
}

void FSSXRaceReplayWorld::ApplyCamera(const SSXReplay::FCamera& Cam, bool bBlend)
{
	// 0x1953C8, 0x1786D0, 0x178A28, 0x191298
	Camera = Cam;
	if (FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr)
	{
		RC->SetMode(Cam.Mode);
		RC->Set(Cam.A, Cam.B, Cam.C, Cam.D, Cam.Target, true);
		Camera = GetCamera();
	}
}

void FSSXRaceReplayWorld::SetCameraTarget(int32 Player)
{
	// 0x190878
	const int32 Rider = Owner ? Owner->GetPlayerRacer(Player) : Player;
	Camera.Target = Rider;
	if (FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr)
	{
		float A, B, C, D;
		int32 T;
		RC->Get(A, B, C, D, T);
		RC->Set(A, B, C, D, Rider, false);
		Camera = GetCamera();
	}
}

void FSSXRaceReplayWorld::SetCameraMode(int32 Mode)
{
	Camera.Mode = Mode;
	if (FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr)
	{
		RC->SetMode(Mode);
		Camera = GetCamera();
	}
}

void FSSXRaceReplayWorld::SetCameraRider(bool bReplay, int32 Player)
{
	// 0x1785C0
	const int32 Rider = Owner ? Owner->GetPlayerRacer(Player) : Player;
	bReplayCamera = bReplay;
	Camera.Target = bReplay ? Rider : 0;
	if (FSSXReplayCamera* RC = Owner ? Owner->GetReplayCamera() : nullptr)
	{
		RC->Activate(bReplay, Rider);
		Camera = GetCamera();
	}
}

void FSSXRaceReplayWorld::StartCameraFlash()
{
	FSSXRaceCamera::StartFlash();   // FUN_00174C30
}

void FSSXRaceReplayWorld::Periodic60()
{
	Owner->Session.ReplayPairUpkeep(true);    // 0x1147F8
}

void FSSXRaceReplayWorld::Periodic10()
{
	Owner->Session.ReplayPairUpkeep(false);   // 0x14FE18, 0x150428
}

int32 FSSXRaceReplayWorld::FocusPlayer() const
{
	// 0x191298, 0x190878, 0x180518, 0x17E148
	return 0;
}

const TArray<SSXReplayFile::FSnapshotCheck>& ASSXRaceManager::GetSnapshotChecks() const
{
	return ReplayWorld.SnapshotChecks;
}

void FSSXRaceReplayWorld::FileSnapshot(int32 Key, TArray<uint8>& Out) const
{
	// 0x18F448
	if (Keys.IsValidIndex(Key) && Keys[Key].Race.bValid)
	{
		const FRaceSessionKeyframe& S = Keys[Key].Race.Session;
		SSXReplayFile::FAiRecord Ai;
		Ai.Clock = float(S.Clock);
		Ai.Frame = S.Frame;
		Ai.Phase = int32(S.Phase);
		Ai.OldPhase = int32(S.Phase);
		SSXReplayFile::PortSnapshot(Keys[Key].Race.RngB.S, Out, &Ai);
	}
}

void FSSXRaceReplayWorld::RebuiltKeyframe(int32 Key, uint32 Frame, const TArray<uint8>& Original)
{
	if (!Owner)
	{
		return;
	}
	if (SSXReplayFile::IsPortSnapshot(Original))
	{
		const bool bEqual = FMemory::Memcmp(Original.GetData(), FGameRng::StreamB().S, 0x18) == 0;
		uint32 FileB[6];
		FMemory::Memcpy(FileB, Original.GetData(), 0x18);
		const uint32 Base = FGameRng(Owner->ReplaySetup.Seed2B00).S[5];
		UE_LOG(LogSSX, Display, TEXT("[replay] port snapshot frame %u: stream B %s (calls: file %d, re-run %d)"), Frame,
			bEqual ? TEXT("equal") : TEXT("differs"), int32(FileB[5] - Base), int32(FGameRng::StreamB().S[5] - Base));
		++(bEqual ? PortStreamBEqual : PortStreamBDiffers);
		if (!bEqual && PortFirstDiffer == MAX_uint32) { PortFirstDiffer = Frame; }
	}
	else if (Original.Num() < 0x18)
	{
		return;
	}
	TArray<FVector> Pos;
	for (int32 I = 0; I < Owner->Session.NumRacers(); ++I)
	{
		const FRiderSim* Sim = Owner->Session.GetRacer(I).Sim;
		Pos.Add(Sim ? Sim->State.Position : FVector::ZeroVector);
	}
	SSXReplayFile::FSnapshotCheck C;
	if (!SSXReplayFile::IsPortSnapshot(Original) && SSXReplayFile::CheckSnapshot(Original, Pos, FGameRng::StreamB().S, C))
	{
		UE_LOG(LogSSX, Display, TEXT("[replay] snapshot frame %u: %d riders in the file, max position error %.2f (racer %d), stream B %s (calls %+lld)"),
			Frame, C.FileRiders, C.MaxError, C.WorstRacer, C.bStreamBEqual ? TEXT("equal") : TEXT("differs"), C.StreamBCalls);
		SnapshotChecks.Add(C);
	}
	SSXReplayFile::FSnapshot File;
	if (Frame == 0 && SSXReplayFile::ParseSnapshot(Original, File) && File.AiFrame > 0)
	{
		// 0x190878, 0x18F6C0, 0x1127E0, 0x115100
		FRaceSessionKeyframe K;
		Owner->Session.SaveReplay(K);
		K.Frame = File.AiFrame;
		Owner->Session.LoadReplay(K);
		if (Keys.IsValidIndex(Key) && Keys[Key].Race.bValid)
		{
			Keys[Key].Race.Session.Frame = File.AiFrame;
		}
	}
	if (Frame == 0 && !SSXReplayFile::IsPortSnapshot(Original) && SSXReplayFile::ParseSnapshot(Original, File))
	{
		const int32 N = Owner->Session.NumRacers();
		for (int32 I = 0; I < N; ++I)
		{
			const int32 R = N - 1 - I;
			if (!File.RecordAt.IsValidIndex(R) || File.RecordAt[R] + 0x450 > Original.Num()) continue;
			const uint8* Rec = Original.GetData() + File.RecordAt[R];
			if (FRiderSim* Sim = Owner->Session.GetRacerMutable(I).Sim) { SSXReplayFile::RestoreBoarderRecord(Rec, Sim->State); }
			if (Keys.IsValidIndex(Key) && Keys[Key].Race.bValid && Keys[Key].Race.Riders.IsValidIndex(I))
			{
				SSXReplayFile::RestoreBoarderRecord(Rec, Keys[Key].Race.Riders[I].State);
			}
		}
	}
}

int32 ASSXRaceManager::GetHumanPlayer(int32 Racer) const
{
	return SSXReplay::PlayerOfRacer([this](int32 I) { return Pawns[I] && !Pawns[I]->bAIRider; }, Pawns.Num(), Racer);
}

int32 ASSXRaceManager::GetPlayerRacer(int32 Player) const
{
	return SSXReplay::RacerOfPlayer([this](int32 I) { return Pawns[I] && !Pawns[I]->bAIRider; }, Pawns.Num(), Player);
}
