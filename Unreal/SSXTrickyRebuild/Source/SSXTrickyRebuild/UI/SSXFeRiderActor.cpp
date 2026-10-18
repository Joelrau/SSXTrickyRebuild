#include "UI/SSXFeRiderActor.h"

#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "ReferenceSkeleton.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Rider/RiderProfiles.h"

namespace
{
	FMatrix ToMatrix(const SSXAnim::FRowMat& R)
	{
		FMatrix M;
		for (int32 I = 0; I < 4; ++I)
		{
			for (int32 J = 0; J < 4; ++J)
			{
				M.M[I][J] = R.M[I][J];
			}
		}
		return M;
	}

	const FMatrix MirrorY(FPlane(1, 0, 0, 0), FPlane(0, -1, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, 0, 0, 1));
}

ASSXFeRiderActor::ASSXFeRiderActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Appearance = CreateDefaultSubobject<USSXCharacterAppearance>(TEXT("Appearance"));
}

bool ASSXFeRiderActor::Setup(int32 InSlot, int32 InCharacter, int32 Board, int32 Outfit)
{
	Slot = InSlot;
	Character = InCharacter;
	RiderId = SSXCharacters::IdForIndex(Character);
	SetActorTransform(FTransform::Identity);
	SetBoardOutfit(Board, Outfit);
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	Poser.Init(Lib, Lib ? Lib->Skeleton(RiderId) : nullptr, Character);
	return Appearance->GetRiderMesh() != nullptr;
}

void ASSXFeRiderActor::SetBoardOutfit(int32 Board, int32 Outfit)
{
	if (Board == ShownBoard && Outfit == ShownOutfit)
	{
		return;
	}
	ShownBoard = Board;
	ShownOutfit = Outfit;
	const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
	const int32 BoardClass = Table.IsLoaded() ? Table.Board(Character, Board).Class : 2;
	Appearance->Apply(Root, Root, RiderId, Character, BoardClass, Board, Outfit);
	// FUN_00266208 / 0x116A68 / 0x2021F8 / 0x201138
	Appearance->SelectBoardModel(FeProfileGoofy);
	bMapped = false;
	if (UPoseableMeshComponent* Mesh = Appearance->GetRiderMesh())
	{
		Mesh->SetRelativeTransform(FTransform::Identity);
		Mesh->SetBoundsScale(1.5f);
	}
}

void ASSXFeRiderActor::StepFrame(SSXFeRiders::FRider& Rider, SSXFeRiders::FSession& Session, TArray<int32>& OutEvents)
{
	Poser.Step(Rider, Session, &OutEvents);
}

bool ASSXFeRiderActor::EnsureMapping()
{
	UPoseableMeshComponent* Mesh = Appearance->GetRiderMesh();
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	const SSXAnim::FAflSkeleton* Skel = Lib ? Lib->Skeleton(RiderId) : nullptr;
	if (!Mesh || !Mesh->GetSkinnedAsset() || !Skel)
	{
		return false;
	}
	if (bMapped)
	{
		return true;
	}
	const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();
	const int32 N = Skel->Bones.Num();
	MeshBoneOfAnimBone.Init(INDEX_NONE, N);
	BindInverse.SetNum(N);
	TArray<FMatrix> BindWorld;
	BindWorld.SetNum(N);
	for (int32 J = 0; J < N; ++J)
	{
		const SSXAnim::FAflBone& B = Skel->Bones[J];
		const FMatrix L = ToMatrix(SSXAnim::LocalRows(B.Euler, B.Translation));
		BindWorld[J] = B.Parent < 0 ? L : L * BindWorld[B.Parent];
		BindInverse[J] = BindWorld[J].Inverse();
		MeshBoneOfAnimBone[J] = Ref.FindBoneIndex(FName(*B.Name));
	}
	PartMap.Init(Ref, Poser.GetParts(), BindWorld);
	const int32 Hips = MeshBoneOfAnimBone[0];
	if (Hips == INDEX_NONE)
	{
		return false;
	}
	FTransform HipsComp = FTransform::Identity;
	for (int32 I = Hips; I != INDEX_NONE; I = Ref.GetParentIndex(I))
	{
		HipsComp = HipsComp * Ref.GetRefBonePose()[I];
	}
	MeshLift = HipsComp.GetLocation().Z - BindWorld[0].GetOrigin().Z;
	bMapped = true;
	return true;
}

void ASSXFeRiderActor::ApplyPose()
{
	if (!Poser.IsReady() || !EnsureMapping())
	{
		return;
	}
	TArray<SSXAnim::FRowMat> Body;
	SSXAnim::FRowMat Board;
	const SSXFeRiders::FData* Data = SSXFeRiders::FData::Shared();
	if (!Data)
	{
		return;
	}
	const SSXAnim::FRowMat RootRows = SSXFeRiders::RiderRoot(Slot, Data->RingRadius, Data->RingZ);
	Poser.ComputePalette(RootRows, Body, Board);
	// TODO: whether the original scales the board too is not recovered (it shares M: scaled).
	const double Size = SSXFeRiders::SizeScale(Character);
	for (SSXAnim::FRowMat& M : Body) { M = SSXFeRiders::ScaleAboutRoot(M, RootRows, Size); }
	Board = SSXFeRiders::ScaleAboutRoot(Board, RootRows, Size);
	SSXAnim::FRiderPartsPose Parts;
	Poser.ComputeParts(Body, Parts);
	const FMatrix RootUE = MirrorY * ToMatrix(RootRows) * MirrorY;
	SetActorTransform(FTransform(RootUE));
	const FMatrix WorldToActor = RootUE.Inverse();
	UPoseableMeshComponent* Mesh = Appearance->GetRiderMesh();
	const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();
	const int32 NM = Ref.GetNum();
	if (Body.Num() == 0 || Mesh->BoneSpaceTransforms.Num() != NM)
	{
		return;
	}
	const FMatrix CompToModel = MirrorY * FTranslationMatrix(FVector(0, 0, -MeshLift));
	const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
	TArray<FMatrix> RefComp, Comp;
	TArray<bool> Driven;
	RefComp.SetNum(NM);
	Comp.SetNum(NM);
	Driven.Init(false, NM);
	for (int32 I = 0; I < NM; ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		const FMatrix L = RefPose[I].ToMatrixWithScale();
		RefComp[I] = P == INDEX_NONE ? L : L * RefComp[P];
	}
	for (int32 J = 0; J < MeshBoneOfAnimBone.Num() && J < Body.Num(); ++J)
	{
		if (const int32 I = MeshBoneOfAnimBone[J]; I != INDEX_NONE)
		{
			Comp[I] = RefComp[I] * CompToModel * BindInverse[J] * ToMatrix(Body[J]) * MirrorY * WorldToActor;
			Driven[I] = true;
		}
	}
	SSXCharacters::PosePartBones(PartMap, Parts, RefComp, CompToModel, FMatrix::Identity, MirrorY * WorldToActor, Comp, Driven);
	for (int32 I = 0; I < NM; ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		if (Driven[I])
		{
			Mesh->BoneSpaceTransforms[I] = FTransform(P == INDEX_NONE ? Comp[I] : Comp[I] * Comp[P].Inverse());
		}
		else
		{
			const FMatrix L = RefPose[I].ToMatrixWithScale();
			Comp[I] = P == INDEX_NONE ? L : L * Comp[P];
			Mesh->BoneSpaceTransforms[I] = RefPose[I];
		}
	}
	SSXCharacters::ApplyFaceMorphs(Mesh, Parts, Size);   // 0x1DF478
	Mesh->MarkRefreshTransformDirty();

	if (UStaticMeshComponent* BoardMesh = Appearance->GetBoardMesh())
	{
		const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
		const SSXAnim::FAflSkeleton* BoardSkel = Lib ? Lib->BoardSkeleton() : nullptr;
		const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
		const int32 BoardClass = Table.IsLoaded() ? Table.Board(Character, FMath::Max(ShownBoard, 0)).Class : 2;
		const SSXAnim::FAflBoardModel* Model = Lib ? Lib->BoardModel(BoardClass, FeProfileGoofy) : nullptr;
		if (BoardSkel && BoardSkel->Bones.Num() > 0 && Model)
		{
			const SSXAnim::FAflBone& B = BoardSkel->Bones[0];
			const FMatrix BoardBindInv = ToMatrix(SSXAnim::LocalRows(B.Euler, B.Translation)).Inverse();
			const FMatrix MeshToModel = MirrorY * FTranslationMatrix(FVector(0, 0, Model->MinZ));
			BoardMesh->SetRelativeTransform(FTransform(MeshToModel * BoardBindInv * ToMatrix(Board) * MirrorY * WorldToActor));
		}
	}
}
