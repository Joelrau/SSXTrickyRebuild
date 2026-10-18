#include "Sim/Rider/RiderShape.h"
#include "Sim/EEFloat.h"

#include "Sim/RiderState.h"
#include "Sim/Rider/RiderProfiles.h"

namespace SSXRider
{
namespace
{
	FVector ToBody(const FVector& M, double Scale, bool bGoofy)
	{
		FVector B(-M.Y, M.X, M.Z);
		B *= Scale;
		if (bGoofy)
		{
			B.X = -B.X;
			B.Y = -B.Y;
		}
		return B;
	}

	const FVector* BonesFor(const FRiderProfile& P)
	{
		return FRiderTable::Shared().BindBones(P.Character, P.bMixMasterMike);
	}
}

namespace
{
	void BoardSpheres(FVector Out[NumShapeSpheres])
	{
		const FVector Mid = 0.5 * (Out[12] + Out[13]);
		const FVector D = (Out[12] - Out[13]).GetSafeNormal();
		Out[14] = Mid + BoardSphereLong * D;
		Out[15] = Mid - BoardSphereLong * D;
		Out[16] = Mid + BoardSphereShort * D;
		Out[17] = Mid - BoardSphereShort * D;
	}
}

bool PosedBodyCentres(const FVector* Bones, int32 NumBones, const FVector& Position, const FQuat& Orientation, FVector Out[NumShapeSpheres])
{
	if (!Bones || NumBones <= 18)
	{
		return false;
	}
	const FQuat Inv = Orientation.GetNormalized().Inverse();
	for (int32 I = 0; I < NumBoneSpheres; ++I)
	{
		Out[I] = Inv.RotateVector(Bones[SphereBone[I]] - Position);
	}
	// 0x2361B0, 0x102F50
	using namespace SSXEE;
	const FVector3f C12 = F(Bones[SphereBone[12]]), C13 = F(Bones[SphereBone[13]]);
	const FVector3f D = Sub(C12, C13);
	const FVector3f U = Scale(Rsqrt(Dot(D, D)), D);
	const FVector3f M = Scale(0.5f, Add(C12, C13));
	static constexpr float K[4] = { 70.0f, -70.0f, 45.0f, -45.0f };
	for (int32 I = 0; I < 4; ++I)
	{
		Out[NumBoneSpheres + I] = Inv.RotateVector(SSXEE::D(Add(M, Scale(K[I], U))) - Position);
	}
	return true;
}

bool BodyCentres(const FRiderProfile& Profile, bool bGoofy, FVector Out[NumShapeSpheres])
{
	const FVector* Bones = BonesFor(Profile);
	if (!Bones)
	{
		return false;
	}
	for (int32 I = 0; I < NumBoneSpheres; ++I)
	{
		Out[I] = ToBody(Bones[SphereBone[I]], Profile.SizeScale, bGoofy);
	}
	BoardSpheres(Out);
	return true;
}

bool BodyPivot(const FRiderProfile& Profile, bool bGoofy, FVector& Out)
{
	const FVector* Bones = BonesFor(Profile);
	if (!Bones)
	{
		return false;
	}
	Out = ToBody(Bones[RootBone], Profile.SizeScale, bGoofy);
	return true;
}

bool BuildSphereSet(const FRiderProfile& Profile, bool bGoofy, const FVector& Position, const FQuat& Rendered, int32 Mask,
                    FRiderSphereSet& Out)
{
	Out = FRiderSphereSet();
	FVector Body[NumShapeSpheres];
	if (!BodyCentres(Profile, bGoofy, Body))
	{
		return false;
	}
	Out.Count = NumShapeSpheres;
	for (int32 I = 0; I < NumShapeSpheres; ++I)
	{
		Out.Centres[I] = Position + Rendered.RotateVector(Body[I]);
		Out.Radii[I] = SphereRadius[I];   // 0x236050
	}
	Out.BoundCentre = 0.5 * (Out.Centres[8] + Out.Centres[9]);
	Out.BoundRadius = BoundRadiusPerScale * Profile.SizeScale;
	Out.Mask = Mask;
	return true;
}

bool BuildPostAISphereSet(const FRiderProfile& Profile, const FRiderState& State, int32 Mask, FRiderSphereSet& Out)
{
	if (!State.bSphereFrameValid)
	{
		return BuildSphereSet(Profile, State.bStanceGoofy, State.Position, State.Orientation, Mask, Out);
	}
	Out = FRiderSphereSet();
	FVector Body[NumShapeSpheres];
	if (State.bPosedSpheresValid)
	{
		for (int32 I = 0; I < NumShapeSpheres; ++I) { Body[I] = State.PosedSpheres[I]; }
	}
	else if (!BodyCentres(Profile, State.bStanceGoofy, Body))
	{
		return false;
	}
	Out.Count = NumShapeSpheres;
	for (int32 I = 0; I < NumShapeSpheres; ++I)
	{
		Out.Centres[I] = State.SphereFrameP + State.SphereFrameQ.RotateVector(Body[I]);
		Out.Radii[I] = SphereRadius[I];
	}
	// 0x2361B0, 0x102F50
	Out.BoundCentre = SSXEE::D(SSXEE::Scale(0.5f, SSXEE::Add(SSXEE::F(Out.Centres[8]), SSXEE::F(Out.Centres[9]))));
	Out.BoundRadius = BoundRadiusPerScale * Profile.SizeScale;
	Out.Mask = Mask;
	return true;
}

}
