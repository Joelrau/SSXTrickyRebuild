#include "Effects/BoostVolumes.h"

#include "Effects/TrackEffects.h"
#include "Sim/Ground/RiderCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

namespace SSXEffects
{
FBox RiderProxyBox(const FRiderSphereSet& Shape)
{
	FBox B(Shape.BoundCentre, Shape.BoundCentre);
	for (int32 K = 0; K < FMath::Min(Shape.Count, FRiderSphereSet::MaxSpheres); ++K)
	{
		const FVector R(Shape.Radii[K]);
		B.Min = B.Min.ComponentMin(Shape.Centres[K] - R);
		B.Max = B.Max.ComponentMax(Shape.Centres[K] + R);
	}
	return B;
}

bool RiderPairsInstance(const FPatchWorld& World, int32 Inst, const FRiderSim& Sim)
{
	const int32 K = World.FindInstance(Inst);
	FRiderSphereSet Shape;
	if (K == INDEX_NONE || !SSXRiderHit::RiderShape(Sim, Shape))
	{
		return false;
	}
	const FBox A = RiderProxyBox(Shape);
	const FBox& B = World.GetInstance(K).Bounds;
	return A.Min.X <= B.Max.X && B.Min.X <= A.Max.X && A.Min.Y <= B.Max.Y && B.Min.Y <= A.Max.Y && A.Min.Z <= B.Max.Z && B.Min.Z <= A.Max.Z;
}

bool RiderInsideInstance(const FPatchWorld& World, int32 Inst, const FRiderSim& Sim)
{
	const int32 K = World.FindInstance(Inst);
	FRiderSphereSet Shape;
	if (K == INDEX_NONE || !SSXRiderHit::RiderShape(Sim, Shape))
	{
		return false;
	}
	Shape.Mask = -1;
	// TODO: the cWorldBoarder t_ref slot is not read here;
	FRiderWorldHit Hit;
	return World.InstanceNarrowPhase(World.GetInstance(K), Shape, ESimTriVariant::Abs, -1.0, Hit) && Hit.T > 0.0;
}

void BindBoostQueries(FEffectWorld& Effects, const TSharedPtr<FPatchWorld>& World)
{
	const TWeakPtr<FPatchWorld> Weak = World;
	Effects.BoostQueries.Pairs = [Weak](int32 Inst, const FEffectRider& R)
	{
		const TSharedPtr<FPatchWorld> W = Weak.Pin();
		return W && R.Sim && RiderPairsInstance(*W, Inst, *R.Sim);
	};
	Effects.BoostQueries.Inside = [Weak](int32 Inst, const FEffectRider& R)
	{
		const TSharedPtr<FPatchWorld> W = Weak.Pin();
		return W && R.Sim && RiderInsideInstance(*W, Inst, *R.Sim);
	};
	Effects.BoostQueries.Bounds = [Weak](int32 Inst, FBox& Out)
	{
		const TSharedPtr<FPatchWorld> W = Weak.Pin();
		const int32 K = W ? W->FindInstance(Inst) : INDEX_NONE;
		if (K == INDEX_NONE)
		{
			return false;
		}
		Out = W->GetInstance(K).Bounds;
		return true;
	};
}
}
