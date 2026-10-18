#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SSXPlayerController.generated.h"

class USSXFeWidget;

UCLASS()
class SSXTRICKYREBUILD_API ASSXPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASSXPlayerController();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	USSXFeWidget* GetOverlayWidget() const { return Overlay; }

private:
	void ForwardPad();
	void HandleActions();
	void TickMenuShots(float DeltaSeconds);

	UPROPERTY(Transient) TObjectPtr<USSXFeWidget> Overlay;
	float ShotClock = 0.f;
	int32 ShotPhase = 0;
};
