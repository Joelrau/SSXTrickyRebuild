#include "Characters/SSXCharacterAppearance.h"

#include "Sim/Rider/RiderShape.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "ReferenceSkeleton.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/RiderSim.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarPosedSpheres(TEXT("ssx.WipeOut.PosedSpheres"), 1,
	TEXT("1 = the rigid wipe-out tumble uses the posed sphere centres of the rendered pose (0x2361B0, default); 0 = the old bind-pose reduction (A/B)."));

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

	const FMatrix MirrorY(FPlane(1, 0, 0, 0), FPlane(0, -1, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, 0, 0, 1));   // converted mesh: game (x, y, z) -> Unreal (x, -y, z)
	const FMatrix BodyToActor(FPlane(0, 1, 0, 0), FPlane(1, 0, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, 0, 0, 1));   // game body (right, forward, up) -> actor (forward +X, right +Y, up +Z): (a, b, c) -> (b, a, c)
}

bool USSXCharacterAppearance::EnsureAnimator(const FRiderSim& Sim)
{
	if (!RiderMesh || ShownId.IsEmpty() || !RiderMesh->GetSkinnedAsset())
	{
		return false;
	}
	const int32 BoardClass = FMath::Clamp(Sim.State.BoardClass, 0, 2);
	const int32 Character = Sim.Profile.GetTrickCharacter();
	if (Animator && BoardClass == AnimBoardClass && Character == AnimCharacter)
	{
		return Animator->IsReady();
	}
	AnimBoardClass = BoardClass;
	AnimCharacter = Character;
	Animator = MakeUnique<SSXAnim::FRiderAnimDriver>();
	// 0x3352B8
	if (!Animator->Init(SSXAnim::FAflLibrary::Shared(), ShownId, BoardClass, FMath::Clamp(Character, 0, 11)))
	{
		return false;
	}
	const SSXAnim::FAflSkeleton& Skel = *Animator->GetSkeleton();
	const FReferenceSkeleton& Ref = RiderMesh->GetSkinnedAsset()->GetRefSkeleton();
	const int32 N = Skel.Bones.Num();
	MeshBoneOfAnimBone.Init(INDEX_NONE, N);
	BindInverse.SetNum(N);
	TArray<FMatrix> BindWorld;
	BindWorld.SetNum(N);
	for (int32 J = 0; J < N; ++J)
	{
		const SSXAnim::FAflBone& B = Skel.Bones[J];
		const FMatrix L = ToMatrix(SSXAnim::LocalRows(B.Euler, B.Translation));
		BindWorld[J] = B.Parent < 0 ? L : L * BindWorld[B.Parent];
		BindInverse[J] = BindWorld[J].Inverse();
		MeshBoneOfAnimBone[J] = Ref.FindBoneIndex(FName(*B.Name));
	}
	PartMap.Init(Ref, Animator->GetParts(), BindWorld);
	const int32 Hips = MeshBoneOfAnimBone[0];
	if (Hips == INDEX_NONE)
	{
		Animator->Init(nullptr, ShownId, BoardClass, Character);
		return false;
	}
	FTransform HipsComp = FTransform::Identity;
	for (int32 I = Hips; I != INDEX_NONE; I = Ref.GetParentIndex(I))
	{
		HipsComp = HipsComp * Ref.GetRefBonePose()[I];
	}
	MeshLift = HipsComp.GetLocation().Z - BindWorld[0].GetOrigin().Z;
	return true;
}

void USSXCharacterAppearance::StepAnimation(FRiderSim& Sim)
{
	if (EnsureAnimator(Sim))
	{
		Animator->Step(Sim);
		Animator->FeedPose(Sim, CVarPosedSpheres.GetValueOnGameThread() != 0);
	}
}

bool USSXCharacterAppearance::ComputeCameraPoints(const FRiderSim& Sim, TArray<FVector, TInlineAllocator<19>>& OutBones, FVector& OutBoard)
{
	OutBones.Reset();
	if (!EnsureAnimator(Sim))
	{
		return false;
	}
	SSXAnim::FRiderPalette Pal;
	Animator->ComputePalette(Sim, Pal);
	if (!Pal.bValid || Pal.Body.Num() == 0)
	{
		return false;
	}
	for (const SSXAnim::FRowMat& M : Pal.Body)
	{
		OutBones.Add(FVector(M.Row(3)));   // row 3 = the bone's world translation
	}
	OutBoard = FVector(Pal.Board.Row(3));
	return true;
}

bool USSXCharacterAppearance::ApplyAnimatedPose(const FRiderSim& Sim, const FVector& BodyPos, const FQuat& BodyRot)
{
	return CaptureStepPose(Sim, BodyPos, BodyRot, true) && PresentPose(1.0);
}

bool USSXCharacterAppearance::CaptureStepPose(const FRiderSim& Sim, const FVector& BodyPos, const FQuat& BodyRot, bool bSnap)
{
	if (!EnsureAnimator(Sim))
	{
		return false;
	}
	SSXAnim::FRiderPalette Pal;
	Animator->ComputePalette(Sim, Pal);
	const FReferenceSkeleton& Ref = RiderMesh->GetSkinnedAsset()->GetRefSkeleton();
	const int32 NM = Ref.GetNum();
	if (!Pal.bValid || RiderMesh->BoneSpaceTransforms.Num() != NM || !BoardParentRef)
	{
		return false;
	}
	if (!bAnimated)
	{
		bAnimated = true;
		RiderMesh->AttachToComponent(BoardParentRef, FAttachmentTransformRules::KeepRelativeTransform);
		RiderMesh->SetRelativeTransform(FTransform::Identity);
		RiderMesh->SetBoundsScale(1.5f);
	}
	const FMatrix Post = FQuatRotationTranslationMatrix(BodyRot, BodyPos).Inverse() * BodyToActor;   // game world -> actor local: inverse body frame, then the body-to-actor swap
	const FMatrix CompToModel = MirrorY * FTranslationMatrix(FVector(0, 0, -MeshLift));
	const FMatrix ScaleM = FScaleMatrix(FVector(Sim.Profile.SizeScale));

	const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
	TArray<FMatrix> RefComp, Comp;
	TArray<bool> Driven;
	FStepPose Frame;
	Frame.Bones.SetNum(NM);
	RefComp.SetNum(NM);
	Comp.SetNum(NM);
	Driven.Init(false, NM);
	for (int32 I = 0; I < NM; ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		const FMatrix L = RefPose[I].ToMatrixWithScale();
		RefComp[I] = P == INDEX_NONE ? L : L * RefComp[P];
	}
	for (int32 J = 0; J < MeshBoneOfAnimBone.Num() && J < Pal.Body.Num(); ++J)
	{
		if (const int32 I = MeshBoneOfAnimBone[J]; I != INDEX_NONE)
		{
			Comp[I] = RefComp[I] * CompToModel * BindInverse[J] * ScaleM * ToMatrix(Pal.Body[J]) * Post;
			Driven[I] = true;
		}
	}
	// 0x206818
	SSXCharacters::PosePartBones(PartMap, Pal.Parts, RefComp, CompToModel, ScaleM, Post, Comp, Driven);
	for (int32 I = 0; I < NM; ++I)
	{
		const int32 P = Ref.GetParentIndex(I);
		if (Driven[I])
		{
			Frame.Bones[I] = FTransform(P == INDEX_NONE ? Comp[I] : Comp[I] * Comp[P].Inverse());
		}
		else
		{
			const FMatrix L = RefPose[I].ToMatrixWithScale();
			Comp[I] = P == INDEX_NONE ? L : L * Comp[P];
			Frame.Bones[I] = RefPose[I];
		}
	}
	// 0x2029D8, 0x1DF478
	SSXCharacters::ApplyFaceMorphs(RiderMesh, Pal.Parts, Sim.Profile.SizeScale);

	// 0x201138
	SelectBoardModel(Sim.Profile.Stance != 0);
	FMatrix BoardToGame;
	if (BoardMesh && BoardMeshToGame(*Animator->GetLibrary(), BoardMeshClass, Pal.Board, Sim.Profile.SizeScale, BoardToGame,
		Sim.Profile.Stance != 0, bBoardMeshGoofy))
	{
		Frame.Board = FTransform(BoardToGame * Post);
		Frame.bBoard = true;
	}
	if (bSnap || !bPoseCurValid || PoseCur.Bones.Num() != NM)
	{
		PosePrev = Frame;
	}
	else
	{
		PosePrev = MoveTemp(PoseCur);
	}
	PoseCur = MoveTemp(Frame);
	bPoseCurValid = true;
	return true;
}

bool USSXCharacterAppearance::PresentPose(double Alpha)
{
	if (!bPoseCurValid || !RiderMesh || RiderMesh->BoneSpaceTransforms.Num() != PoseCur.Bones.Num() || PosePrev.Bones.Num() != PoseCur.Bones.Num())
	{
		return false;
	}
	const float A = float(FMath::Clamp(Alpha, 0.0, 1.0));
	auto Blend = [A](const FTransform& From, const FTransform& To)
	{
		if (A >= 1.f) { return To; }
		if (A <= 0.f) { return From; }
		return FTransform(FQuat::Slerp(From.GetRotation(), To.GetRotation(), A).GetNormalized(),
			FMath::Lerp(From.GetTranslation(), To.GetTranslation(), double(A)), FMath::Lerp(From.GetScale3D(), To.GetScale3D(), double(A)));
	};
	for (int32 I = 0; I < PoseCur.Bones.Num(); ++I)
	{
		RiderMesh->BoneSpaceTransforms[I] = Blend(PosePrev.Bones[I], PoseCur.Bones[I]);
	}
	RiderMesh->MarkRefreshTransformDirty();
	if (BoardMesh && PoseCur.bBoard)
	{
		BoardMesh->SetRelativeTransform(PosePrev.bBoard ? Blend(PosePrev.Board, PoseCur.Board) : PoseCur.Board);
	}
	return true;
}

bool USSXCharacterAppearance::BoardMeshToGame(const SSXAnim::FAflLibrary& Lib, int32 BoardClass, const SSXAnim::FRowMat& Board, double SizeScale, FMatrix& Out,
	bool bGoofyModel, bool bGoofyMesh)
{
	const SSXAnim::FAflSkeleton* BoardSkel = Lib.BoardSkeleton();
	const SSXAnim::FAflBoardModel* Model = Lib.BoardModel(BoardClass, bGoofyModel && bGoofyMesh);
	if (!BoardSkel || BoardSkel->Bones.Num() == 0 || !Model)
	{
		return false;
	}
	const SSXAnim::FAflBone& B = BoardSkel->Bones[0];
	const FMatrix BoardBindInv = ToMatrix(SSXAnim::LocalRows(B.Euler, B.Translation)).Inverse();
	FMatrix MeshToModel = MirrorY * FTranslationMatrix(FVector(0, 0, Model->MinZ));
	if (bGoofyModel && !bGoofyMesh)
	{
		// 0x201138
		MeshToModel = MeshToModel * FMatrix(FPlane(-1, 0, 0, 0), FPlane(0, -1, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, 0, 0, 1));
	}
	Out = MeshToModel * BoardBindInv * FScaleMatrix(FVector(SizeScale)) * ToMatrix(Board);
	return true;
}
