#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SSXGameMode.generated.h"

class ASnowboarderPawn;
class USSXFrontEndSubsystem;

UCLASS()
class SSXTRICKYREBUILD_API ASSXGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASSXGameMode();

	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

	virtual void StartPlay() override;

	static void ApplySelection(const USSXFrontEndSubsystem& FrontEnd, ASnowboarderPawn& Rider);

	bool HasRaceData() const;
};
