// 0x15F7C8, 0x11D4E8, 0x202B00, 0x206818, 0x2029D8, 0x1DF478
#pragma once

#include "CoreMinimal.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/AnimController.h"
#include "Sim/Anim/RiderPose.h"

namespace SSXAnim
{
	constexpr int32 IdleStill = 0x2F0, IdleBwd = 0x2F1, IdleBwdFast = 0x2F2, IdleDown = 0x2F3, IdleFwd = 0x2F4,
		IdleFwdFast = 0x2F5, IdleLeft = 0x2F6, IdleRight = 0x2F7, IdleUp = 0x2F8;
	constexpr int32 IdleSelectPeriod = 18;
	constexpr int32 FaceFileId = 10;
	constexpr int32 MaxPoseLayers = 10;         // 0x202930
	constexpr double FaceKeyThreshold = 0.01;
	constexpr double FaceMorphCmPerUnit = 2560.0 / 32768.0;

	SSXTRICKYREBUILD_API bool HairIdleDue(int32 RaceFrame, int32 Control);
	SSXTRICKYREBUILD_API int32 HairIdleId(const FVector4f& V, const FVector4f& X, const FVector4f& Y, const FVector4f& Z,
		int32 Character, int32 Channel0Id);

	struct FPartPalette
	{
		int32 FileId = 0;
		TArray<FRowMat> Bones;
	};

	struct FRiderPartsPose
	{
		TArray<FPartPalette> Parts;
		TArray<double> FaceWeights;
		bool bFaceSampled = false;
		SSXTRICKYREBUILD_API const FPartPalette* Find(int32 FileId) const;
	};

	SSXTRICKYREBUILD_API void GatherPartLayers(const FAnimController& Controller, int32 FileId, TArray<FPoseLayer>& Out);

	SSXTRICKYREBUILD_API void SampleFaceWeights(const FAflLibrary& Lib, TConstArrayView<FPoseLayer> Layers, int32 NumKeys, TArray<double>& Out);

	// 0x202868
	SSXTRICKYREBUILD_API void BuildPartsPose(const FAflLibrary& Lib, const FAflRiderParts& Parts, const FAnimController& Controller,
		TConstArrayView<FRowMat> Body, double Scale, FRiderPartsPose& Out);

	inline double FaceKeyDrawWeight(int32 Key, double Weight)
	{
		return (Key == 0 || Weight > FaceKeyThreshold) ? Weight : 0.0;
	}
}
