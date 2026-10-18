#include "Sim/Ground/WallCollision.h"
#include "Sim/EEFloat.h"

#include "Race/GameRng.h"
#include "Sim/Air/AirMath.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/RiderSim.h"

namespace SSXGround
{
namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;

	FORCEINLINE FVector Unit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}
}

void FeedStuckA(FRiderState& St, const FVector& Mn)
{
	const double D = FVector::DotProduct(Mn, St.LastImpactNormal);
	St.StuckAccumA += D > 0.0 ? 1.0 - D : 1.0;
	St.LastImpactNormal = Mn;
}

double TurnAwayFromWall(FRiderSim& Sim, const FVector& Mn)
{
	// a = unit(up x m), t = up x a, theta = atan2(forward . a, forward . t)
	const FRiderState& St = Sim.State;
	const FVector Up = St.BasisUp;
	const FVector A = Unit(FVector::CrossProduct(Up, Mn), FVector::ZeroVector);
	if (A.IsZero())
	{
		return 0.0;
	}
	const FVector T = FVector::CrossProduct(Up, A);
	const double Theta = FMath::Atan2(FVector::DotProduct(St.BasisForward, A), FVector::DotProduct(St.BasisForward, T));
	const double Target = FMath::DegreesToRadians(HeadingTargetDeg), Step = FMath::DegreesToRadians(HeadingMaxStepDeg);
	const double Delta = Theta > 0.0 ? FMath::Clamp(Target - Theta, 0.0, Step) : FMath::Clamp(-Target - Theta, -Step, 0.0);
	if (Delta != 0.0)
	{
		Sim.RotateBody(Up, -Delta);
	}
	return -Delta;
}

void BumpReaction(FRiderSim& Sim, SSXAnim::FRiderAnimChannel* Ch, const FVector& T, bool bBoostLoss)
{
	// 0x124DD0
	FRiderState& St = Sim.State;
	if (bBoostLoss && St.Control != C::Bump)
	{
		SSXAir::BoostAdd(St, BumpBoost, SSXAir::BoostGain(Sim.Profile));
	}
	if (St.Control != C::Cruise && St.Control != C::Prewind)
	{
		return;
	}
	const double Ft = FVector::DotProduct(St.BasisForward, T);
	int32 Anim;
	if (Ft > BumpAxisCos)
	{
		Anim = SSXAnim::BumpBwd;
	}
	else if (Ft < -BumpAxisCos)
	{
		Anim = SSXAnim::BumpFwd;
	}
	else
	{
		double Side = FVector::DotProduct(St.BasisSide, T);
		if (St.bStanceGoofy)
		{
			Side = -Side;
		}
		const bool bHard = FVector::DotProduct(St.Velocity, T) < HardBumpTangentSpeed && St.Velocity.Size() > HardBumpSpeed;
		Anim = (Side < 0.0 ? 0x247 : 0x246) + (bHard ? 2 : 0);
		if (bHard)
		{
			// 0x368540, 0x1041B0
			static const float BumpYaws[5] = { 0.0f, 3.1415927f, -3.1415927f, 6.2831855f, -6.2831855f };
			const uint32 R = FGameRng::StreamB().Next() % 5u;
			St.BumpYawDone = 0.0f;
			St.BumpYaw = BumpYaws[R];
		}
	}
	if (Ch)
	{
		Ch->BumpAnim = Anim;
	}
	// 0x235398
	++St.BumpSpeechCount;
	St.bBumpSpeechHard = Anim >= 0x248;
	Sim.SetControl(C::Bump, TEXT("T67 wall bump"));
}

void WallImpact(FRiderSim& Sim, SSXAnim::FRiderAnimChannel* Ch, double Bounce, const FRiderImpact& Info, const FVector& Mn, const FVector& Up)
{
	// 0x126540
	FRiderState& St = Sim.State;
	const double K = FVector::DotProduct(Up, Mn);
	if (St.Motion == M::Air && St.Velocity.Size() < StuckBAirSpeed)
	{
		St.StuckAccumB += (StuckBK - K) * FMath::Max(Mn.Z, StuckBMinZ);
	}
	if (St.Motion == M::WipeOut)
	{
		return;
	}
	if (Bounce > ImpactBumpK * K + ImpactWipeOutBase)
	{
		// 0x156DE0, 0x11B018
		bool bPending = false;
		const double Reward = (Ch && Ch->CrashScoring) ? Ch->CrashScoring(bPending) : SSXAir::BailMeter;
		Sim.StartWipeOut(Info, TEXT("T60 wall bounce"), bPending);
		SSXAir::BoostAdd(St, Reward, SSXAir::BoostGain(Sim.Profile));
		return;
	}
	if ((K <= 0.0 || St.Motion != M::Air) && Bounce > ImpactBumpK * K)
	{
		BumpReaction(Sim, Ch, Unit(Mn - K * Up, Mn));
	}
}

bool WallCollision(FRiderSim& Sim, int32 Mask, SSXAnim::FRiderAnimChannel* Ch, FRiderWorldHit* OutHit)
{
	FRiderState& St = Sim.State;
	const IRiderWorld* World = Sim.GetWorld();
	if (!World)
	{
		return false;
	}
	// 0x126250, 0x257810
	FRiderSphereSet Shape;
	if (!SSXRider::BuildPostAISphereSet(Sim.Profile, St, Mask, Shape))
	{
		return false;
	}
	St.SphereMask = Mask;
	const FVector N = St.GroundNormal;
	FRiderWorldHit Hit;
	const bool bHit = World->QueryWalls(Shape, /*bFine=*/false, &N, Hit);
	St.SphereMask = -1;
	if (!bHit || Hit.T <= 0.0)
	{
		return false;
	}
	if (OutHit)
	{
		*OutHit = Hit;
	}
	FVector Mn = Hit.Normal;
	const double Mdn = FVector::DotProduct(Mn, N);
	if (Mdn < 0.0)
	{
		Mn = Unit(Mn - Mdn * N, Mn);
	}
	FeedStuckA(St, Mn);
	// 0x1267A0
	FVector D = WallPushOutK * Hit.T * Mn;
	const bool bGround = St.Motion == M::Ground;
	const double Ddn = FVector::DotProduct(D, N);
	if (bGround && Ddn < 0.0)
	{
		D -= Ddn * N;
	}
	St.Position += D;
	St.SphereFrameP += D;
	Sim.NotifySphereSetMoved(D);
	const double A = -FVector::DotProduct(Mn, St.Velocity);
	if (A < 0.0)
	{
		return false;
	}
	const double Bounce = FMath::Max(WallBounceK * A, WallBounceMin);
	St.Velocity += (A + Bounce) * Mn;
	if (bGround)
	{
		TurnAwayFromWall(Sim, Mn);
	}
	FRiderImpact Info;
	Info.Point = Hit.Point;
	Info.Direction = Unit(St.Velocity, St.BasisForward);
	Info.Normal = Mn;
	Info.Speed = A;
	WallImpact(Sim, Ch, Bounce, Info, Mn, St.BasisUp);
	// 0x216C50, 0x2162F0
	if (Hit.InstanceIndex != INDEX_NONE)
	{
		++St.PropHitCount;
		St.PropHitInstance = Hit.InstanceIndex;
		St.PropHitSpeed = St.Velocity.Size();
	}
	++St.WallSoundCount;
	return true;
}

double RiderCollisionMass(const FRiderProfile& Profile, double BoostStrength)
{
	// 0x11D7B0
	using SSXEE::FpuAdd;
	const float K = FpuAdd((float(Profile.StatBytes[0x12 - 0x0E]) * 2.145779f) * 0.003921569f, 0.79599f);
	const float M = float(Profile.MassByte) * K;
	return double(FpuAdd((float(BoostStrength) * 7.999995f) * M, M));
}
}
