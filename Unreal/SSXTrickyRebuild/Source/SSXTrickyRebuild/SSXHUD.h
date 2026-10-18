#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Harness/FixedStepClock.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXHudData.h"
#include "UI/SSXHudOverlay.h"
#include "SSXHUD.generated.h"

class SSSXFeCanvas;
class SWidget;

UCLASS()
class SSXTRICKYREBUILD_API ASSXHUD : public AHUD
{
	GENERATED_BODY()

public:
	ASSXHUD();
	virtual void Tick(float DeltaSeconds) override;

	const FSSXFeDrawList& GetDrawList() const { return DrawList; }
	void BuildInput(FSSXHudInput& Out) const;
	static void FillRider(const class FRiderSim& Sim, FSSXHudPlayerInput& Pl);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void LoadAssets();
	void ActivateIfNeeded();

	FSSXHudData Data;
	FSSXHudSheet Sheet;
	FSSXFeAssets Assets;
	TMap<FName, UTexture*> HudPages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture>> PageRefs;
	FSSXHudOverlay Overlay;
	FSSXFeDrawList DrawList;
	FFixedStepClock Clock;
	TSharedPtr<SSSXFeCanvas> Canvas;
	TSharedPtr<SWidget> CanvasHolder;
	const void* ActiveSession = nullptr;
	int32 ActivePlacements = -1;
	bool bActivated = false;
};
