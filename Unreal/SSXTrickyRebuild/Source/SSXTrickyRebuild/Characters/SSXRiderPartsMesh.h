// 0x1DF478
#pragma once

#include "CoreMinimal.h"
#include "Sim/Anim/RiderParts.h"

class UPoseableMeshComponent;
struct FReferenceSkeleton;

namespace SSXCharacters
{
	SSXTRICKYREBUILD_API FName FaceMorphName(int32 Key);

	struct SSXTRICKYREBUILD_API FPartBoneMap
	{
		struct FEntry
		{
			int32 Part = 0;
			int32 FileId = 0;
			int32 Bone = 0;
			int32 MeshBone = INDEX_NONE;
			FMatrix BindInverse = FMatrix::Identity;
		};
		TArray<FEntry> Entries;

		void Init(const FReferenceSkeleton& Ref, const SSXAnim::FAflRiderParts* Parts, TConstArrayView<FMatrix> BodyBindWorld);
		void Reset() { Entries.Reset(); }
	};

	SSXTRICKYREBUILD_API void PosePartBones(const FPartBoneMap& Map, const SSXAnim::FRiderPartsPose& Pose, TConstArrayView<FMatrix> RefComp,
		const FMatrix& CompToModel, const FMatrix& Pre, const FMatrix& Post, TArray<FMatrix>& Comp, TArray<bool>& Driven);

	SSXTRICKYREBUILD_API int32 ApplyFaceMorphs(UPoseableMeshComponent* Mesh, const SSXAnim::FRiderPartsPose& Pose, double Scale);

	SSXTRICKYREBUILD_API void FaceMorphWeights(const SSXAnim::FRiderPartsPose& Pose, double Scale, TArray<double>& Out);
}
