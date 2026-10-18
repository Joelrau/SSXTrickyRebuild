#include "Sim/Ground/PropCollision.h"
#include "Sim/Ground/GroundMotion.h"
#include "Sim/SimCoords.h"

#include "Sim/Air/AirStates.h"
#include "Sim/Ground/WallCollision.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/RiderSim.h"

namespace SSXGround
{
int32 PropMaskForMotion(ERiderMotion Motion)
{
	switch (Motion)
	{
	case ERiderMotion::Air:
	case ERiderMotion::Ground:
		return -1;
	case ERiderMotion::RailSlide:
		return 0x003;
	case ERiderMotion::WipeOut:
	case ERiderMotion::WipeOutRecover:
		return 0xFC3;
	default:
		return 0;
	}
}

FPropResponse ComputePropResponse(const FRiderPropInfo& Prop, double Depth, const FVector& HitNormal, const FVector& V,
                                  ERiderMotion Motion, ERiderControl Control, const FVector& GroundNormal, double RiderMass)
{
	FPropResponse R;
	FVector N = HitNormal;
	if (Motion == ERiderMotion::Ground)
	{
		N = SSXSim::RsqrtNormal((N - FVector::DotProduct(N, GroundNormal) * GroundNormal));
	}
	R.Normal = N;
	R.Approach = -FVector::DotProduct(N, V);
	R.Velocity = V;
	if (!Prop.Reacts())
	{
		return R;
	}
	if (Control == ERiderControl::ResetWait)
	{
		R.Reaction = EPropReaction::Skipped;
		return R;
	}
	if (Prop.Mass == 0.0)
	{
		R.Reaction = EPropReaction::Bump;
		return R;
	}
	const double E = Prop.Restitution;
	const double A = R.Approach;
	double Dv;
	if (!Prop.bMovable)
	{
		FVector D = PropPushOutK * Depth * N;
		const double Dg = FVector::DotProduct(D, GroundNormal);
		if (Motion == ERiderMotion::Ground && Dg < 0.0)
		{
			D -= Dg * GroundNormal;   // 0x1267A0
		}
		R.Push = D;
		Dv = FMath::Max(A * E + A, A + PropBounceMin);
		R.Reaction = EPropReaction::Bounce;
	}
	else
	{
		const double K = 1.0 / (RiderMass / Prop.Mass + 1.0);
		const double Ak = A * K;
		Dv = FMath::Sqrt(FMath::Max(Ak * Ak - (1.0 - E) * K, 0.0)) - Ak;
		R.Reaction = EPropReaction::Movable;
	}
	R.Rumble = A * PropRumbleK;
	R.Velocity = V + N * Dv;
	R.Dv = Dv;
	return R;
}

bool PropCollision(FRiderSim& Sim, ERiderMotion Motion, SSXAnim::FRiderAnimChannel* Anim, FPropContactEvent* OutEvent, int32 MaskOverride)
{
	FRiderState& St = Sim.State;
	const IRiderWorld* World = Sim.GetWorld();
	const int32 Mask = MaskOverride != 0 ? MaskOverride : PropMaskForMotion(Motion);
	if (!World || Mask == 0)
	{
		return false;
	}
	// 0x257810(q, +0x470, 1): every motion queries the cPostAI set of the previous update (moved by this update's pushes)
	FRiderSphereSet Shape;
	if (!SSXRider::BuildPostAISphereSet(Sim.Profile, St, Mask, Shape))
	{
		return false;
	}
	FPropContactEvent Ev;
	if (!World->QueryProps(Shape, Ev.Hit, Ev.Prop))
	{
		if (Sim.OnMovingObjects)
		{
			Sim.OnMovingObjects(Sim, Shape, Anim);
		}
		return false;
	}
	const FVector G = St.GroundNormal;
	auto Respond = [&]()
	{
		const double RiderMass = Ev.Prop.bMovable ? RiderCollisionMass(Sim.Profile, St.BoostStrength) : 1.0;
		Ev.Response = ComputePropResponse(Ev.Prop, Ev.Hit.T, Ev.Hit.Normal, St.Velocity, Motion, St.Control, G, RiderMass);
	};
	Respond();
	const FVector N = Ev.Response.Normal;
	Ev.Contact.Point = Ev.Hit.Point;
	Ev.Contact.Direction = St.Velocity.GetSafeNormal(1e-9, St.BasisForward);
	Ev.Contact.Normal = N;
	Ev.Contact.Speed = Ev.Response.Approach;
	Ev.TouchEffect = Ev.Prop.TouchEffect;
	Ev.bHitSound = true;
	if (Sim.OnPropContact)
	{
		Sim.OnPropContact(Sim, Ev);
	}
	// 0x125088, 0x13BD40, 0x125A00
	bool bRespond = false;
	uint32 FlagsNow = 0;
	if (World->GetPropFlags(Ev.Prop.Instance, FlagsNow) && FlagsNow != Ev.Prop.Flags)
	{
		Ev.Prop.Flags = FlagsNow;
		bRespond = true;
	}
	if (!Ev.Prop.bMovable && World->IsPropMovable(Ev.Prop.Instance))
	{
		Ev.Prop.bMovable = true;
		bRespond = true;
	}
	if (bRespond)
	{
		Respond();
	}
	switch (Ev.Response.Reaction)
	{
	case EPropReaction::Bump:
		BumpReaction(Sim, Anim, N, /*bBoostLoss=*/false);
		break;
	case EPropReaction::Bounce:
	case EPropReaction::Movable:
		FeedStuckA(St, N);
		if (Ev.Response.Reaction == EPropReaction::Bounce)
		{
			St.Position += Ev.Response.Push;
			St.SphereFrameP += Ev.Response.Push;   // 0x1267A0
			Sim.NotifySphereSetMoved(Ev.Response.Push);
			if (Motion == ERiderMotion::Ground)
			{
				TurnAwayFromWall(Sim, N);
			}
		}
		St.AddPadFeedback(Ev.Response.Rumble);   // 0x125BCC
		if (Ev.Response.Rumble > PropRumbleThreshold)
		{
			Ev.bRumble = true;
			Ev.RumbleSeconds = FMath::Clamp(PropRumbleTimeK * Ev.Response.Rumble, PropRumbleMinTime, PropRumbleMaxTime);
		}
		St.Velocity += Ev.Response.Dv * N;
		// 0x126540
		{
			const ERiderMotion Before = St.Motion;
			WallImpact(Sim, Anim, Ev.Response.Dv, Ev.Contact, N, SSXAir::GetRenderedOrientation(St).RotateVector(FVector::UpVector));
			if (Before != St.Motion && St.Motion == ERiderMotion::WipeOut)
			{
				LogRailPropBounce(Sim, Before, Ev.Hit.InstanceIndex, N, Ev.Response.Dv, Ev.Response.Approach);
			}
		}
		break;
	default:
		break;
	}
	// 0x216C50
	++St.PropHitCount;
	St.PropHitInstance = Ev.Hit.InstanceIndex;
	St.PropHitSpeed = St.Velocity.Size();
	if (Sim.OnMovingObjects)
	{
		Sim.OnMovingObjects(Sim, Shape, Anim);
	}
	if (OutEvent)
	{
		*OutEvent = Ev;
	}
	return true;
}
}
