#include "Sim/Anim/RiderParts.h"

namespace SSXAnim
{
namespace
{
	// 0x11D4E8
	float IdleBits(uint32 U) { float F; FMemory::Memcpy(&F, &U, 4); return F; }
	const float IdleVStill = IdleBits(0x430AE38Eu);
	const float IdleVFast = IdleBits(0x44505555u);
	const float IdleVVertical = IdleBits(0x4418C71Cu);
	const float IdleVSide = IdleBits(0x43D05555u);

	float IdleDot4(const FVector4f& A, const FVector4f& B)
	{
		float S = A.X * B.X;
		S = S + A.Y * B.Y;
		S = S + A.Z * B.Z;
		S = S + A.W * B.W;
		return S;
	}

	constexpr int32 IdleControlVoid = 1, IdleControlResetWait = 0x16;
}

bool HairIdleDue(int32 RaceFrame, int32 Control)
{
	return Control != IdleControlVoid && Control != IdleControlResetWait && RaceFrame % IdleSelectPeriod == 0;
}

int32 HairIdleId(const FVector4f& V, const FVector4f& X, const FVector4f& Y, const FVector4f& Z, int32 Character, int32 Channel0Id)
{
	const float Speed = FMath::Sqrt(IdleDot4(V, V));
	if (Speed < IdleVStill)
	{
		if ((Character == 2 || Character == 11)
			&& (Channel0Id == 0 || Channel0Id == 0x1FE || Channel0Id == 0x1FD || Channel0Id == 0x1FC))
		{
			return IdleFwd;
		}
		return IdleStill;
	}
	const float Fwd = IdleDot4(V, Y);
	if (Speed < IdleVFast)
	{
		return Fwd < 0.0f ? IdleBwd : IdleFwd;
	}
	const float Side = IdleDot4(V, X);
	const float Up = IdleDot4(V, Z);
	if (FMath::Abs(Side) < FMath::Abs(Up) && IdleVVertical < FMath::Abs(Up))
	{
		return Up < 0.0f ? IdleUp : IdleDown;
	}
	if (IdleVSide < FMath::Abs(Side))
	{
		return Side < 0.0f ? IdleLeft : IdleRight;
	}
	return Fwd < 0.0f ? IdleBwdFast : IdleFwdFast;
}

const FPartPalette* FRiderPartsPose::Find(int32 FileId) const
{
	for (const FPartPalette& P : Parts)
	{
		if (P.FileId == FileId)
		{
			return &P;
		}
	}
	return nullptr;
}

void GatherPartLayers(const FAnimController& Controller, int32 FileId, TArray<FPoseLayer>& Out)
{
	Controller.GatherLayers(FileId, Out);
	if (Out.Num() > MaxPoseLayers)
	{
		Out.SetNum(MaxPoseLayers);
	}
}

void SampleFaceWeights(const FAflLibrary& Lib, TConstArrayView<FPoseLayer> Layers, int32 NumKeys, TArray<double>& Out)
{
	Out.Init(0.0, FMath::Max(NumKeys, 0));
	for (const FPoseLayer& L : Layers)
	{
		if (!L.Block)
		{
			continue;
		}
		const int32 NT = FMath::Min(L.Block->Tracks.Num(), NumKeys);
		for (int32 K = 0; K < NT; ++K)
		{
			Out[K] += L.Weight * Lib.EvalTrack(L.Block->Tracks[K], L.Frame);
		}
	}
}

void BuildPartsPose(const FAflLibrary& Lib, const FAflRiderParts& Parts, const FAnimController& Controller,
	TConstArrayView<FRowMat> Body, double Scale, FRiderPartsPose& Out)
{
	Out.Parts.Reset();
	Out.FaceWeights.Reset();
	Out.bFaceSampled = false;
	TArray<FPoseLayer> Layers;
	TArray<FBonePose> Pose;
	for (const FAflPart& Part : Parts.Parts)
	{
		GatherPartLayers(Controller, Part.FileId, Layers);
		const int32 NB = Part.Skeleton.Bones.Num();
		if (NB == 0)
		{
			if (Part.NumMorphKeys > 0 && Part.FileId == FaceFileId)
			{
				// 0x202868, 0x2029D8
				SampleFaceWeights(Lib, Layers, Part.NumMorphKeys, Out.FaceWeights);
				Out.bFaceSampled = Layers.Num() > 0;
			}
			continue;
		}
		// 0x202B00
		Pose.Reset();
		BlendLayers(Lib, Part.Skeleton, Layers, Scale, Scale, Part.PaletteShift, Pose);
		// 0x206818
		FPartPalette& P = Out.Parts.AddDefaulted_GetRef();
		P.FileId = Part.FileId;
		P.Bones.SetNum(NB);
		for (int32 I = 0; I < NB; ++I)
		{
			const FAflBone& B = Part.Skeleton.Bones[I];
			const FRowMat* Parent = nullptr;
			if (B.ParentFile == 0)
			{
				Parent = Body.IsValidIndex(B.Parent) ? &Body[B.Parent] : nullptr;
			}
			else if (B.ParentFile == Part.FileId)
			{
				Parent = B.Parent < I ? &P.Bones[B.Parent] : nullptr;
			}
			else if (const FPartPalette* Other = Out.Find(B.ParentFile))
			{
				Parent = Other->Bones.IsValidIndex(B.Parent) ? &Other->Bones[B.Parent] : nullptr;
			}
			const FRowMat L = LocalRows(Pose[I].Euler, Pose[I].Trans);
			P.Bones[I] = Parent ? L * *Parent : L;
		}
	}
}
}
