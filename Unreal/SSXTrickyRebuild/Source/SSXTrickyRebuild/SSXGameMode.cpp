#include "SSXGameMode.h"

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/Paths.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXRaceManager.h"
#include "SnowboarderPawn.h"
#include "SSXHUD.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXPlayerController.h"

ASSXGameMode::ASSXGameMode()
{
	DefaultPawnClass = ASnowboarderPawn::StaticClass();
	HUDClass = ASSXHUD::StaticClass();
	PlayerControllerClass = ASSXPlayerController::StaticClass();
}

APawn* ASSXGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	FActorSpawnParameters Params;
	Params.Instigator = GetInstigator();
	Params.ObjectFlags |= RF_Transient;
	Params.bDeferConstruction = true;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* Pawn = GetWorld()->SpawnActor<APawn>(GetDefaultPawnClassForController(NewPlayer), SpawnTransform, Params);
	if (ASnowboarderPawn* Rider = Cast<ASnowboarderPawn>(Pawn))
	{
		Rider->bStartRestarts = false;
		Rider->bRaceDriven = HasRaceData();
		const USSXFrontEndSubsystem* FrontEnd = USSXFrontEndSubsystem::Get(this);
		if (FrontEnd && FrontEnd->bHasSelection)
		{
			ApplySelection(*FrontEnd, *Rider);
		}
	}
	if (Pawn)
	{
		Pawn->FinishSpawning(SpawnTransform);
	}
	return Pawn;
}

void ASSXGameMode::ApplySelection(const USSXFrontEndSubsystem& FrontEnd, ASnowboarderPawn& Rider)
{
	// 0x16A9D8
	Rider.Character = FrontEnd.Character;
	Rider.BoardClass = FrontEnd.BoardClass;
	Rider.Board = FrontEnd.BoardIndex;
	Rider.Outfit = FrontEnd.Outfit;
	Rider.PlayerOptions = FrontEnd.MakePlayerOptions();
	if (FrontEnd.IsMixMasterMike())
	{
		Rider.CharacterId = TEXT("zz_mmm");
	}
}

bool ASSXGameMode::HasRaceData() const
{
	const FString Track = UWorld::RemovePIEPrefix(GetWorld()->GetMapName()).ToLower();
	return FPaths::FileExists(FRaceTrackData::DefaultPath(Track));
}

void ASSXGameMode::StartPlay()
{
	if (HasRaceData())
	{
		if (AWorldSettings* Settings = GetWorldSettings())
		{
			Settings->bEnableWorldBoundsChecks = false;
		}
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		GetWorld()->SpawnActor<ASSXRaceManager>(ASSXRaceManager::StaticClass(), FTransform::Identity, Params);
	}
	Super::StartPlay();
}
