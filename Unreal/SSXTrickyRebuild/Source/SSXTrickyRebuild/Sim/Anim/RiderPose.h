// 0x250C10, 0x250FD0
#pragma once

#include "CoreMinimal.h"
#include "Sim/Anim/AflClips.h"

namespace SSXAnim
{
	// row-vector float32 matrix like the game: v' = v M, row 3 = translation
	struct SSXTRICKYREBUILD_API FRowMat
	{
		float M[4][4];

		static FRowMat Identity();
		FRowMat operator*(const FRowMat& B) const;
		FVector3f Row(int32 R) const { return FVector3f(M[R][0], M[R][1], M[R][2]); }
		void SetRow(int32 R, const FVector3f& V, float W) { M[R][0] = V.X; M[R][1] = V.Y; M[R][2] = V.Z; M[R][3] = W; }
		FVector3f TransformPoint(const FVector3f& V) const;
	};

	SSXTRICKYREBUILD_API void SinCosEE(float A, float& OutSin, float& OutCos);
	SSXTRICKYREBUILD_API float AsinEE(float X);
	// 0x250E98
	SSXTRICKYREBUILD_API float CosEE(float X);
	// 0x250D60
	SSXTRICKYREBUILD_API float SinEE(float X);
	// 0x2510F8
	SSXTRICKYREBUILD_API float AtanEE(float X);
	// 0x129008, 0x116AB8
	SSXTRICKYREBUILD_API float Atan2EE(float Y, float X);   // x = 0 gives +-pi/2 (0 for y = 0); x < 0 adds / subtracts pi by the sign of y

	// 0x205A78, 0x205E98, 0x207198
	// Euler (x, y, z) rad: local rotation Rz(-z) Ry(-y) Rx(-x) (column vectors), as rows; translation in row 3
	SSXTRICKYREBUILD_API FRowMat LocalRows(const FVector3d& Euler, const FVector3d& Trans);

	// 0x129628, 0x12AE18
	struct FRiderFrameInput
	{
		FQuat Orientation = FQuat::Identity;
		FVector Position = FVector::ZeroVector;
		double Steer = 0.0;
		int32 Motion = 2;
		int32 BoardClass = 0;
		FVector Up = FVector::UpVector;
		double S308 = 0.0;
		FVector Left = FVector(1, 0, 0);
		double Scale = 1.0;
		double Lean = 0.0;
		double Spin = 0.0;
		double Flip = 0.0;
		double Suspension = 0.0;
		double BoardChatter = 0.0;
		FVector GroundNormal = FVector::UpVector;
	};

	SSXTRICKYREBUILD_API FRowMat RootFrame(const FRiderFrameInput& In, double HipsZ, bool bIkBranch);

	SSXTRICKYREBUILD_API FRowMat BoardFrame(const FRowMat& RootNoSusp, const FBonePose& BoardPose, const FRiderFrameInput& In);

	SSXTRICKYREBUILD_API bool FootRotations(const FAflLibrary& Lib, const FAflSkeleton& Body, int32 BoardClass, bool bMirror,
		double OutLeft[3][3], double OutRight[3][3]);

	SSXTRICKYREBUILD_API void LegIK(const FRowMat& ParentRows, const FVector3d& ThighEuler, const FVector3d BindT[3],
		const FVector3f& Target, FRowMat& OutThigh, FRowMat& OutCalf, FVector3f& OutFoot);

	/** Inputs of the palette builder that do not change per frame. */
	struct FRiderSkeletonSet
	{
		const FAflSkeleton* Body = nullptr;
		const FAflSkeleton* Board = nullptr;
		double FootLeft[2][3][3] = {};
		double FootRight[2][3][3] = {};
		bool bFeet[2] = { false, false };
		int32 BoardClass = 0;
		SSXTRICKYREBUILD_API bool Init(const FAflLibrary& Lib, const FAflSkeleton* InBody, int32 InBoardClass);
	};

	SSXTRICKYREBUILD_API void BuildPalette(const FAflLibrary& Lib, const FRiderSkeletonSet& Set, const FRowMat& Root,
		TConstArrayView<FBonePose> Pose, const FRowMat& BoardWorld, bool bMirror, double Scale, TArray<FRowMat>& Out);

	// 0x2057A0
	SSXTRICKYREBUILD_API void BuildPaletteFK(const FAflSkeleton& Body, const FRowMat& Root, TConstArrayView<FBonePose> Pose,
		double Scale, TArray<FRowMat>& Out);
}
