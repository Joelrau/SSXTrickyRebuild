#include "SSXHUD.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Progression/SSXProgression.h"
#include "Race/SSXRaceManager.h"
#include "Sim/Air/AirStates.h"
#include "Sim/RiderSim.h"
#include "SnowboarderPawn.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXLoadingScreen.h"
#include "UI/SSXPlayerController.h"
#include "Widgets/SWeakWidget.h"

ASSXHUD::ASSXHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	SetTickableWhenPaused(true);
}

void ASSXHUD::BeginPlay()
{
	Super::BeginPlay();
	LoadAssets();
	DrawList.Assets = &Assets;
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		Canvas = SNew(SSSXFeCanvas).DrawList(&DrawList).FillBlack(false);
		CanvasHolder = SNew(SWeakWidget).PossiblyNullContent(Canvas);
		Viewport->AddViewportWidgetContent(CanvasHolder.ToSharedRef(), 5);
	}
}

void ASSXHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	if (CanvasHolder.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(CanvasHolder.ToSharedRef());
		}
	}
	CanvasHolder.Reset();
	Canvas.Reset();
	if (SSXFeDrawHud::GetSheet() == &Sheet.Sprites)
	{
		SSXFeDrawHud::SetSheet(nullptr, nullptr);
	}
	Super::EndPlay(Reason);
}

void ASSXHUD::LoadAssets()
{
	USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this);
	if (!Data.Load())
	{
		UE_LOG(LogSSX, Warning, TEXT("[hud] no HUD data in %s (run tools/export_hud_data.py)"), *FSSXHudData::DefaultDir());
	}
	// 0x1A6FB8
	const bool bTrick = Sub && Sub->bHasSelection && Sub->Launch.GameMode == 6;
	const FString Json = FPaths::Combine(FSSXHudData::DefaultDir(), TEXT("hud_sprites.json"));
	if (Sheet.Load(Json, bTrick) && Sub)
	{
		for (const TPair<FName, FString>& P : Sheet.PagePaths)
		{
			if (UTexture2D* T = Sub->LoadPng(P.Value))
			{
				HudPages.Add(P.Key, T);
				PageRefs.Add(T);
			}
		}
		SSXFeDrawHud::SetSheet(&Sheet.Sprites, &HudPages);
	}
	if (Sub)
	{
		Assets = Sub->Assets();
	}
	Assets.HudSprites = Sheet.Sprites.IsLoaded() ? &Sheet.Sprites : nullptr;
	for (const TPair<FName, UTexture*>& P : HudPages)
	{
		Assets.Pages.Add(P.Key, P.Value);
	}
	Overlay.Init(&Data, Sub ? &Sub->Strings() : nullptr);
}

void ASSXHUD::ActivateIfNeeded()
{
	const ASSXRaceManager* Race = ASSXRaceManager::Find(this);
	const FRaceSession* S = Race ? Race->GetSession() : nullptr;
	if (S)
	{
		if (S == ActiveSession && S->GetPlacements() == ActivePlacements)
		{
			return;
		}
		Overlay.Activate(int32(S->GetMode()), Race->GetTrackData().GameTrack, S->NumRacers(), 0, &S->Paths(), S->GetLapLength());
		ActiveSession = S;
		ActivePlacements = S->GetPlacements();
		bActivated = true;
	}
	else if (!bActivated)
	{
		Overlay.Activate(1, 0, 1, 0, nullptr, 0.0);
		bActivated = true;
	}
}

void ASSXHUD::BuildInput(FSSXHudInput& In) const
{
	const ASnowboarderPawn* Pawn = Cast<ASnowboarderPawn>(GetOwningPawn());
	const ASSXRaceManager* Race = ASSXRaceManager::Find(this);
	const FRaceSession* S = Race ? Race->GetSession() : nullptr;
	const USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this);
	if (Sub)
	{
		In.ApplySettings(Sub->Profile.Settings);   // 0x32CA10, 0x32CA40
	}
	if (const ASSXPlayerController* PC = Cast<ASSXPlayerController>(GetOwningPlayerController()))
	{
		if (USSXFeWidget* W = PC->GetOverlayWidget())
		{
			const FSSXOverlayManager& O = W->GetOverlays();
			In.HudDetail = O.GetHudDetail();
			In.bModal = O.AnyModal();
			In.bDimOverlay = O.AnyHudDim();
			In.bReplayActive = O.IsActive(OvReplay);
			In.bCardSave = O.IsHudCardSave();
		}
	}
	In.Mode = S ? int32(S->GetMode()) : 1;
	In.Track = Race ? Race->GetTrackData().TrackId : 0;
	In.Phase = S ? int32(S->GetPhase()) : 4;
	In.Clock = S && S->GetPhase() == ERacePhase::Race ? S->GetClock() : 0.0;
	In.RacerCount = S ? S->NumRacers() : 1;
	In.RaceFlags = S ? uint32(S->GetFrame()) : 0;
	// FUN_002657A8
	In.TimeToBeatCs = S && S->GetMode() == ERaceEventMode::PracticeTimedRace && S->TimeToBeatCs != MAX_int32 ? S->TimeToBeatCs : 0;
	In.bReplayFull = Race && Race->GetReplay().ShowFullMessage();   // 0x1A4F70

	In.bLessonObject = In.Mode == 6;
	if (const SSXLesson::FLessonMan* Lesson = Race ? Race->GetLesson() : nullptr)
	{
		In.LessonState = int32(Lesson->GetState());
	}
	FSSXHudPlayerInput& Pl = In.Players.AddDefaulted_GetRef();
	const FRiderSim* Sim = Pawn ? Pawn->GetSim() : nullptr;
	if (Sim)
	{
		FillRider(*Sim, Pl);
		Pl.PadLayout = Pawn->PadLayout;
		Pl.BoardClass = Sim->Profile.BoardClass;
		// FUN_00164840
		if (Sub && Pl.Character >= 0 && Pl.Character < SSXProg::NumRiders)
		{
			Pl.bBeginner = SSXProg::RankFromPoints(Sub->Profile.Career[Pl.Character].RankPoints()) >= 8;
		}
	}
	if (S)
	{
		Pl.Racer = Race->GetRacerIndex(Pawn);
		TArray<FRaceStanding> Rows;
		S->GetStandings(Rows);
		In.RacerDist.SetNumZeroed(S->NumRacers());
		In.RacerComputer.SetNumZeroed(S->NumRacers());
		for (const FRaceStanding& R : Rows)
		{
			if (!In.RacerDist.IsValidIndex(R.Racer)) continue;
			In.RacerDist[R.Racer] = R.bFinished ? 0.0 : R.Remaining;   // FUN_001170d0
			In.RacerComputer[R.Racer] = !R.bHuman;
		}
		if (Pl.Racer >= 0)
		{
			const FRacerRecord& Rec = S->GetRacer(Pl.Racer);
			Pl.Rank = Rec.Rank;
			Pl.Score = Rec.Score;
			// FUN_0011ea28
			if (Rec.IsFinished()) In.RaceTimeSec = FMath::Max(Rec.FinishCs(), 0) / 100.0;
			else if (S->GetPhase() == ERacePhase::Race) In.RaceTimeSec = S->GetClock();
		}
	}
}

void ASSXHUD::FillRider(const FRiderSim& Sim, FSSXHudPlayerInput& Pl)
{
	const FRiderState& St = Sim.State;
	Pl.BoostMeter = St.BoostMeter;
	Pl.TrickyTimer = St.TrickyTimer;
	Pl.UberHintIndex = St.UberHintIndex;
	Pl.bSwitch = St.bStanceGoofy != (Sim.Profile.Stance != 0);
	Pl.Speed = St.Velocity.Size();
	Pl.FinishResult = St.FinishResult;
	Pl.Character = Sim.Profile.Character;
	if (const SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		// FUN_00156900
		Pl.TrickValue = Air->Score.Value;
		Pl.PointsMultiplier = Air->Score.PointsMult;
		Pl.UberCount = Air->Score.UberCount;
		Pl.ComboCount = Air->Score.Combo;
		Pl.Score = Air->Score.Score;
		Pl.TrickText = Air->Score.TrickText.TrimEnd();
		Pl.Messages.Reset();
		for (const FRiderMessage& M : Air->Score.Messages.Slots)
		{
			FSSXHudMsg& H = Pl.Messages.AddDefaulted_GetRef();
			H.Type = M.Type;
			H.Value = M.Value;
			H.Total = M.Life;
			H.Elapsed = M.Elapsed;
			H.Extra = M.Extra;
		}
	}
	if (St.bInfiniteBoost)
	{
		Pl.UberCount = FMath::Max(Pl.UberCount, SSXAir::InfiniteBoostUbers + 1);
	}
}

void ASSXHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ActivateIfNeeded();
	FSSXHudInput In;
	BuildInput(In);
	// 0x113E10, 0x1A7AD0
	bool bHudSlotOn = true;
	if (const ASSXPlayerController* PC = Cast<ASSXPlayerController>(GetOwningPlayerController()))
	{
		if (USSXFeWidget* W = PC->GetOverlayWidget()) { bHudSlotOn = W->GetOverlays().IsActive(OvHUD); }
	}
	if (bHudSlotOn && !GetWorld()->IsPaused() && !USSXLoadingScreen::HoldsGame(this))
	{
		const int32 Steps = Clock.Advance(DeltaSeconds);
		for (int32 I = 0; I < Steps; ++I)
		{
			Overlay.Update(In);
		}
		if (Overlay.ConsumeTicks() > 0)
		{
			if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
			{
				Audio->PlayPointsTick();
			}
		}
		for (int32 N = Overlay.ConsumeShowoffBeeps(); N > 0; --N)
		{
			if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
			{
				Audio->PlayZbxsfx(78);   // FUN_00216930
			}
		}
	}
	DrawList.Reset();
	if (bHudSlotOn)
	{
		Overlay.Draw(DrawList, In);
	}
}
