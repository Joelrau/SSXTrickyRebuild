#include "Characters/SSXRiderPartsMesh.h"

#include "Animation/MorphTarget.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

namespace SSXCharacters
{
namespace
{
	FMatrix PartsToMatrix(const SSXAnim::FRowMat& R)
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
}

FName FaceMorphName(int32 Key)
{
	return FName(*FString::Printf(TEXT("ssx_face_%02d"), Key));
}

void FPartBoneMap::Init(const FReferenceSkeleton& Ref, const SSXAnim::FAflRiderParts* Parts, TConstArrayView<FMatrix> BodyBindWorld)
{
	Entries.Reset();
	if (!Parts)
	{
		return;
	}
	TArray<TArray<FMatrix>> PartBind;
	PartBind.SetNum(Parts->Parts.Num());
	for (int32 P = 0; P < Parts->Parts.Num(); ++P)
	{
		const SSXAnim::FAflPart& Part = Parts->Parts[P];
		const int32 NB = Part.Skeleton.Bones.Num();
		PartBind[P].SetNum(NB);
		for (int32 B = 0; B < NB; ++B)
		{
			const SSXAnim::FAflBone& Bone = Part.Skeleton.Bones[B];
			const FMatrix L = PartsToMatrix(SSXAnim::LocalRows(Bone.Euler, Bone.Translation));
			FMatrix Parent = FMatrix::Identity;
			if (Bone.ParentFile == 0 && BodyBindWorld.IsValidIndex(Bone.Parent))
			{
				Parent = BodyBindWorld[Bone.Parent];
			}
			else if (Bone.ParentFile == Part.FileId && Bone.Parent < B)
			{
				Parent = PartBind[P][Bone.Parent];
			}
			else
			{
				for (int32 Q = 0; Q < P; ++Q)
				{
					if (Parts->Parts[Q].FileId == Bone.ParentFile && PartBind[Q].IsValidIndex(Bone.Parent))
					{
						Parent = PartBind[Q][Bone.Parent];
					}
				}
			}
			PartBind[P][B] = L * Parent;
			FEntry& E = Entries.AddDefaulted_GetRef();
			E.Part = P;
			E.FileId = Part.FileId;
			E.Bone = B;
			E.MeshBone = Ref.FindBoneIndex(FName(*Bone.Name));
			E.BindInverse = PartBind[P][B].Inverse();
		}
	}
}

void PosePartBones(const FPartBoneMap& Map, const SSXAnim::FRiderPartsPose& Pose, TConstArrayView<FMatrix> RefComp,
	const FMatrix& CompToModel, const FMatrix& Pre, const FMatrix& Post, TArray<FMatrix>& Comp, TArray<bool>& Driven)
{
	for (const FPartBoneMap::FEntry& E : Map.Entries)
	{
		if (E.MeshBone == INDEX_NONE || !RefComp.IsValidIndex(E.MeshBone) || !Comp.IsValidIndex(E.MeshBone))
		{
			continue;
		}
		const SSXAnim::FPartPalette* P = Pose.Find(E.FileId);
		if (!P || !P->Bones.IsValidIndex(E.Bone))
		{
			continue;
		}
		Comp[E.MeshBone] = RefComp[E.MeshBone] * CompToModel * E.BindInverse * Pre * PartsToMatrix(P->Bones[E.Bone]) * Post;
		Driven[E.MeshBone] = true;
	}
}

void FaceMorphWeights(const SSXAnim::FRiderPartsPose& Pose, double Scale, TArray<double>& Out)
{
	Out.Init(0.0, Pose.FaceWeights.Num());
	if (!Pose.bFaceSampled)
	{
		return;
	}
	for (int32 K = 0; K < Pose.FaceWeights.Num(); ++K)
	{
		Out[K] = SSXAnim::FaceKeyDrawWeight(K, Pose.FaceWeights[K]) * Scale;
	}
}

int32 ApplyFaceMorphs(UPoseableMeshComponent* Mesh, const SSXAnim::FRiderPartsPose& Pose, double Scale)
{
	USkeletalMesh* SM = Mesh ? Cast<USkeletalMesh>(Mesh->GetSkinnedAsset()) : nullptr;
	if (!SM)
	{
		return 0;
	}
	const int32 NT = SM->GetMorphTargets().Num();
	Mesh->ActiveMorphTargets.Reset();
	Mesh->MorphTargetWeights.SetNumZeroed(NT);
	for (float& W : Mesh->MorphTargetWeights)
	{
		W = 0.0f;
	}
	if (NT == 0)
	{
		return 0;
	}
	TArray<double> Weights;
	FaceMorphWeights(Pose, Scale, Weights);
	int32 Active = 0;
	for (int32 K = 0; K < Weights.Num(); ++K)
	{
		if (Weights[K] == 0.0)
		{
			continue;
		}
		int32 Index = INDEX_NONE;
		if (const UMorphTarget* T = SM->FindMorphTargetAndIndex(FaceMorphName(K), Index); T && Mesh->MorphTargetWeights.IsValidIndex(Index))
		{
			Mesh->ActiveMorphTargets.Add(T, Index);
			Mesh->MorphTargetWeights[Index] = float(Weights[K]);
			++Active;
		}
	}
	return Active;
}
}
