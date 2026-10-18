#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/SSXFeRiders.h"
#include "Characters/SSXRiderPartsMesh.h"
#include "SSXFeRiderActor.generated.h"

// The actor sits at the world origin: the palette is game world space (ssxfe.pbd), mapped by game (x, y, z) -> Unreal (x, -y, z).
class USSXCharacterAppearance;

UCLASS()
class SSXTRICKYREBUILD_API ASSXFeRiderActor : public AActor
{
	GENERATED_BODY()

public:
	ASSXFeRiderActor();

	bool Setup(int32 InSlot, int32 InCharacter, int32 Board, int32 Outfit);
	void SetBoardOutfit(int32 Board, int32 Outfit);
	void StepFrame(SSXFeRiders::FRider& Rider, SSXFeRiders::FSession& Session, TArray<int32>& OutEvents);
	void ApplyPose();
	/** Selection lights: the blended light block (ambient + three directional lights, directions in FE camera space); USSXWorldPresentation feeds it to the rider materials (FeRiderLightInputs). */
	void SetLights(const SSXFeRiders::FLightSet& InLights) { Lights = InLights; bLightsValid = true; }
	const SSXFeRiders::FLightSet* GetLights() const { return bLightsValid ? &Lights : nullptr; }

	int32 GetSlot() const { return Slot; }
	USSXCharacterAppearance* GetAppearance() const { return Appearance; }

	/** The FE rider descriptor's stance (+0x40): FUN_00266208 always writes 0, so FE riders draw the regular board model
	 * (0x201138 name index class + 3 x stance) whatever the rider's own stance. */
	static constexpr bool FeProfileGoofy = false;

private:
	bool EnsureMapping();

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USSXCharacterAppearance> Appearance;

	SSXFeRiders::FPoser Poser;
	int32 Slot = 0;
	int32 Character = 0;
	int32 ShownBoard = -1;
	int32 ShownOutfit = -1;
	FString RiderId;
	TArray<int32> MeshBoneOfAnimBone;
	TArray<FMatrix> BindInverse;
	SSXCharacters::FPartBoneMap PartMap;
	double MeshLift = 0.0;
	bool bMapped = false;
	SSXFeRiders::FLightSet Lights;
	bool bLightsValid = false;
};
