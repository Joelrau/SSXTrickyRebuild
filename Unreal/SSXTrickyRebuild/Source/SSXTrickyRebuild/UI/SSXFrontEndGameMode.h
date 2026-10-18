#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "SSXFrontEndGameMode.generated.h"

class USSXFeWidget;
class ACameraActor;
class ASSXFeRiderActor;
class UTexture2D;
namespace SSXFeVenueVideo { class FVideo; }

UCLASS()
class SSXTRICKYREBUILD_API ASSXFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASSXFrontEndGameMode();
	virtual void RestartPlayer(AController* NewPlayer) override {}
};

UCLASS()
class SSXTRICKYREBUILD_API ASSXFrontEndPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASSXFrontEndPlayerController();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	USSXFeWidget* GetMenu() const { return Menu; }

private:
	void TickCamera();
	void TickRiders(float DeltaSeconds);
	/** FE scene functions: the FEStartScript events (onPROpen ...) run as ssxfe.ssf functions in the track effect interpreter; its AnimObject nodes move the doors (USSXPropAnimation), 60 Hz. */
	void TickScene(float DeltaSeconds);
	void SpawnRiders();
	void TickMenuShots(float DeltaSeconds);
	/** Venue-select medal models (actors tagged SSXFeMedal<NN>): shown only for a venue whose medal is set (FSSXProfile::VenueMedal; a new save shows none). */
	void TickMedals();

	UPROPERTY(Transient) TObjectPtr<USSXFeWidget> Menu;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> SceneCamera;
	UPROPERTY(Transient) TArray<TObjectPtr<ASSXFeRiderActor>> Riders;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> MedalActors;
	TArray<int32> MedalModel;
	TArray<int32> MedalInstance;
	TArray<int32> MedalFrameSent;
	int32 VenueScreenSerialSent = -1;
	void TickVenueScreens();
	/** FUN_00263000 / cFE3DMovieGeometry: the playing venue screen's stream, stepped once per 60 Hz scene frame, its frame
	 *  uploaded into VenueVideoTex, which replaces the screen instance's textures while it plays (FSSXFrontEnd::VenueVideo). */
	void StepVenueVideo();
	void ApplyVenueVideo();
	TSharedPtr<SSXFeVenueVideo::FVideo> VenueVideo;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> VenueVideoTex;
	int32 VenueVideoInst = INDEX_NONE;
	float RiderClock = 0.f;   // fixed 60 Hz FE frames for the rider animation
	float SceneClock = 0.f;   // fixed 60 Hz FE frames for the scene functions
	int32 SceneEventsDone = 0;
	bool bSceneBegun = false;
	int32 CarouselSerialSent = -1;

	int32 ShotStep = 0;
	float ShotClock = 0.f;
	bool bShotTaken = false;
};

namespace SSXMenuShots
{
	SSXTRICKYREBUILD_API void Take(int32 Index, const FString& Name);
}
