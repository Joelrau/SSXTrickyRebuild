#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Characters/SSXRiderPartsMesh.h"
#include "SSXCharacterAppearance.generated.h"

class FRiderSim;
class USceneComponent;
class UPoseableMeshComponent;
class UStaticMeshComponent;


UCLASS(ClassGroup = (SSX), meta = (BlueprintSpawnableComponent))
class SSXTRICKYREBUILD_API USSXCharacterAppearance : public UActorComponent
{
	GENERATED_BODY()

public:
	USSXCharacterAppearance();
	virtual ~USSXCharacterAppearance() override;

	// Board 0..11, Outfit 0..6 (INDEX_NONE keeps the imported textures). Both parents use the pawn's body frame:
	// board nose +X, up +Z, origin at the board contact.
	bool Apply(USceneComponent* BoardParent, USceneComponent* BodyParent, const FString& CharacterId,
		int32 CharacterIndex, int32 BoardClass, int32 Board = INDEX_NONE, int32 Outfit = INDEX_NONE);
	// 0x3352B0
	bool bMalloraLook = false;
	const TArray<FString>& GetAppliedTextures() const { return AppliedTextures; }

	// the sim turns the body by pi when the stance flips, so rider and board turn by pi in the body frame
	void SetGoofy(bool bGoofy);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// one controller frame per sim frame: call after every FRiderSim::Step; feeds the new board matrix and pose back to the sim
	void StepAnimation(FRiderSim& Sim);
	bool ApplyAnimatedPose(const FRiderSim& Sim, const FVector& BodyPos, const FQuat& BodyRot);
	bool CaptureStepPose(const FRiderSim& Sim, const FVector& BodyPos, const FQuat& BodyRot, bool bSnap);
	// render only (port): draws the blend of the last two captured step poses at the render alpha; never feeds the sim
	bool PresentPose(double Alpha);
	void ResetPoseHistory() { bPoseCurValid = false; }
	// camera points in game world space: hips anchor (+0x4AD0), body bones in skeleton order, board translation (+0x4A90)
	bool ComputeCameraPoints(const FRiderSim& Sim, TArray<FVector, TInlineAllocator<19>>& OutBones, FVector& OutBoard);
	bool IsAnimated() const { return bAnimated; }
	const SSXAnim::FRiderAnimDriver* GetAnimator() const { return Animator.Get(); }

	const FString& GetShownId() const { return ShownId; }
	UPoseableMeshComponent* GetRiderMesh() const { return RiderMesh; }
	UStaticMeshComponent* GetBoardMesh() const { return BoardMesh; }
	static void ConfigureShadowFlags(class UPrimitiveComponent& Part);
	// 0x1DF478, FUN_00207C30, FUN_00201138, 0x341998
	static bool BoardMeshToGame(const SSXAnim::FAflLibrary& Lib, int32 BoardClass, const SSXAnim::FRowMat& Board, double SizeScale, FMatrix& Out,
		bool bGoofyModel = false, bool bGoofyMesh = false);
	// 0x201138
	static class UStaticMesh* LoadDrawnBoardMesh(const FString& Id, int32 BoardClass, bool bGoofyProfile, bool& bOutGoofyMesh);
	void SelectBoardModel(bool bGoofyProfile);
	bool IsBoardMeshGoofy() const { return bBoardMeshGoofy; }

private:
	void SetPlaceholderVisible(bool bVisible);
	void ApplySelectionTextures(const FString& Id, int32 Board, int32 Outfit);
	void ApplyChrome();
	class UTexture2D* LoadTexture(const FString& Path);
	void UpdateStanceTransform();
	bool EnsureAnimator(const FRiderSim& Sim);

	UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> RiderMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BoardMesh;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> BoardParentRef;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> BodyParentRef;

	FString ShownId;
	FString RequestedId;
	FString LastConsoleValue;
	int32 RequestedIndex = -1;
	int32 RequestedBoardClass = 2;
	int32 RequestedBoard = INDEX_NONE;
	int32 RequestedOutfit = INDEX_NONE;
	TArray<FString> AppliedTextures;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> SelectionRefs;
	int32 BoardMeshClass = 2;
	UPROPERTY(Transient) TObjectPtr<class UStaticMesh> RegularBoardModel;
	UPROPERTY(Transient) TObjectPtr<class UStaticMesh> GoofyBoardModel;
	bool bBoardMeshGoofy = false;
	double BoardTop = 0.0;   // cm, height of the board deck the rider stands on
	bool bIsGoofy = false;

	TUniquePtr<SSXAnim::FRiderAnimDriver> Animator;
	int32 AnimBoardClass = -1;
	int32 AnimCharacter = -1;
	bool bAnimated = false;
	TArray<int32> MeshBoneOfAnimBone;   // animation skeleton bone -> mesh bone index
	TArray<FMatrix> BindInverse;   // inverse bind world (game, row vectors) per animation bone
	SSXCharacters::FPartBoneMap PartMap;
	double MeshLift = 0.0;
	struct FStepPose
	{
		TArray<FTransform> Bones;
		FTransform Board = FTransform::Identity;
		bool bBoard = false;
	};
	FStepPose PosePrev, PoseCur;
	bool bPoseCurValid = false;
};
