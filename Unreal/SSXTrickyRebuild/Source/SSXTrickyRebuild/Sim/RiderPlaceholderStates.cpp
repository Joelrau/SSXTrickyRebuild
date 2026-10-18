#include "Sim/RiderPlaceholderStates.h"

#include "Sim/Air/LandingPredictor.h"
#include "Sim/Air/AirMath.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/Ground/WallCollision.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/SurfaceTable.h"

namespace SSXSimPlaceholders
{
namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;
	const FVector Z = FVector::UpVector;

	FORCEINLINE FVector Unit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}

	bool IsDeepSnow(int32 Surface) { return Surface == 3 || Surface == 4; }
	bool IsBailSurface(int32 Surface) { return Surface == FRiderSim::BailSurfaceA || Surface == FRiderSim::BailSurfaceB; }

	double NormalSupport(double H, double Vn, double G, double P7, double P8, double Damping)
	{
		if (H > 0.0)
		{
			double A = -(G / 30.0) * H;
			if (Vn > 0.0)
			{
				A -= Damping * Vn;
			}
			return A;
		}
		if (H > -P7)
		{
			return -G * H / P7 - Damping * Vn;
		}
		const double Depth = FMath::Min(-H, P8);
		return G * (1.0 + 2.0 * (Depth - P7) / (P8 - P7)) - Damping * Vn;
	}

	double SwitchScale(const FRiderSim& Sim)
	{
		return 1.0;
	}

	double LongitudinalDrag(const FRiderSim& Sim, double Vf, const FSurfaceParams& S)
	{
		const FRiderState& St = Sim.State;
		const double StatSpeed = Sim.Profile.Stat(0x0E), StatBrake = Sim.Profile.Stat(0x11), StatAero = Sim.Profile.Stat(0x19);
		double CLin = St.BoardClass == 2 ? 0.7076 - 0.3020 * StatSpeed : 1.0650 - 0.3085 * StatSpeed;
		double CBrk = 0.6151 + 0.9182 * StatBrake;
		double CQuad = 1.2848 - 0.2904 * StatAero;
		const double K = SwitchScale(Sim);
		CLin *= K; CBrk *= K; CQuad *= K;
		const double Ice = IsDeepSnow(St.SurfaceId) ? 0.5 - St.GroundHeight / St.SurfaceP8 : 1.0;
		const double X = FMath::Abs(Vf) / 1000.0;
		const double Coef = S.DragLinear() * CLin + 0.10573 * (1.0 - St.Crouch.Cur)
			+ 1.7513 * (1.0 + 1.2154 * St.BoostStrength) * St.Brake.Cur * St.Brake.Cur * CBrk
			+ X * (S.DragQuadratic() + X * S.DragCubic() * CQuad);
		return -Vf * Ice * Coef;
	}

	double GripCurve(double X)
	{
		if (X < 555.56) return 0.20104 + 8.8997e-4 * X;
		if (X < 1388.89) return 0.69547 + 3.6130e-4 * (X - 555.56);
		return 0.99655 - 1.0441e-4 * (X - 1388.89);
	}

	double LateralGrip(const FRiderSim& Sim, double Vs, double Vf, const FSurfaceParams& S)
	{
		const FRiderState& St = Sim.State;
		double X = Vs * GripCurve(FMath::Abs(Vf));
		const double E = FMath::Abs(St.Steer.Cur) * 1.00015;
		if (E >= 1.0)
		{
			X *= E;
		}
		const double GStat = (0.001 + 1.1412 * Sim.Profile.Stat(0x13)) * SwitchScale(Sim);
		double A = -X * S.Grip() * GStat;
		if (St.BoostStrength > 0.0)
		{
			A /= (1.0 + 3.5 * St.BoostStrength);
		}
		return A;
	}

	void YawStep(FRiderSim& Sim, double Dt)
	{
		FRiderState& St = Sim.State;
		const FVector V = St.Velocity;
		const double Speed = V.Size();
		if (Speed < 1e-6)
		{
			return;
		}
		const FVector N = St.GroundNormal, F = St.ContactForward;
		const double Beta = FMath::Asin(FMath::Clamp(FVector::DotProduct(FVector::CrossProduct(V, F) / Speed, N), -0.99999, 0.99999));
		const double Steer = St.Steer.Cur;
		static const double Lead[3][2] = { { 0.3002, 0.3492 }, { 0.4, 0.5240 }, { 0.2083, 0.2622 } };
		const int32 Bc = FMath::Clamp(St.BoardClass, 0, 2);
		const double Ka = Lead[Bc][0], Kb = Lead[Bc][1];
		const double LeadAngle = Steer * Kb * (1.0 + 0.5 * Ka * (1.0 - Steer * Steer)) / (1.0 + 0.01 * St.Crouch.Cur);
		double Err = LeadAngle - Beta;
		if (FVector::DotProduct(V, F) < 0.0)
		{
			Err = -Err;
		}
		const double G0 = FMath::Min(1.0, 2.0030e-5 * Speed * Speed * Dt);
		double E = FMath::Min(Speed / 555.56, 1.0) * (0.5 - FMath::Abs(FVector::DotProduct(V, St.FallLine)) / Speed) * 40.0;
		E = FMath::Min(FMath::Max(E, 0.0) + 0.01004, 1.0);
		const double SteerDir = Err > 0.0 ? Steer : -Steer;
		const double Gain = G0 * (SteerDir >= E ? SteerDir : E);
		const double DYaw = FMath::Clamp(Err * Gain, -0.10472, 0.10472);
		Sim.RotateBody(N, DYaw);
	}

	void ProbeFromAbove(const FRiderSim& Sim, FGroundProbeResult& R, bool& bFound)
	{
		FGroundProbeInput In;
		In.Position = Sim.State.Position;
		In.GroundNormal = Z;
		In.Forward = Sim.State.BasisForward;
		In.ContactLeft = Sim.State.ContactLeft;
		In.LeanDeg = 0.0;
		bFound = Sim.GetWorld() && Sim.GetWorld()->ProbeGround(In, R);
	}
}

void FPlaceholderGroundMotion::Update(FRiderSim& Sim)
{
	FRiderState& St = Sim.State;
	const FSurfaceParams& S = Sim.Surface();
	const double Dt = Sim.MotionDt();
	const FVector N = St.GroundNormal, F = St.ContactForward, L = St.ContactLeft;
	const double Vn = FVector::DotProduct(St.Velocity, N);
	const double Vf = FVector::DotProduct(St.Velocity, F);
	const double Vs = FVector::DotProduct(St.Velocity, L);
	const double G = S.Gravity();

	double Support = NormalSupport(St.GroundHeight, Vn, G, St.SurfaceP7, St.SurfaceP8, S.NormalDamping());
	const double Drag = LongitudinalDrag(Sim, Vf, S);
	const double Grip = LateralGrip(Sim, Vs, Vf, S);
	const double Propulsion = 0.0;
	if (N.Z < 0.0 && Support < 0.0)
	{
		Support = 0.0;
	}
	const double Theta = FMath::DegreesToRadians(S.EdgeDeg()) * St.Steer.Cur;
	const double MEdge = FMath::Min(Support, 2.0 * G);
	const double CosT = FMath::Cos(Theta), SinT = FMath::Sin(Theta);
	const FVector Tilted = CosT * N + SinT * L;
	FVector A = (Support - MEdge / CosT) * N + (MEdge / CosT) * Tilted + (Drag + Propulsion) * F + Grip * L - G * Z;

	const double D = FMath::Max(St.SurfaceP8 + St.GroundHeight, -10.0);
	if (D < 0.0)
	{
		St.Position -= D * N;
		St.GroundHeight -= D;
		St.Velocity -= Vn * N;
		const double An = FVector::DotProduct(A, N);
		if (An < 0.0)
		{
			A -= An * N;
		}
	}
	St.Position += St.Velocity * Dt;
	St.Velocity += A * Dt;

	if (!(St.Control == C::Prewind && St.Steer.Cur == 0.0))
	{
		YawStep(Sim, Dt);
	}

	const bool bOnGround = Sim.RunGroundProbe();

	if (!IsDeepSnow(St.SurfaceId))
	{
		const double Speed = St.Velocity.Size();
		const FVector Redirected = St.Velocity - 0.4 * FVector::DotProduct(St.Velocity, St.GroundNormal) * St.GroundNormal;
		St.Velocity = Speed * Unit(Redirected, St.Velocity / FMath::Max(Speed, 1e-9));
	}

	if (!St.bWallHitThisFrame && (!bOnGround || St.GroundHeight > Sim.Surface().StickHeight()))
	{
		St.LaunchCharge = St.Crouch.Cur;
		const bool bWasJump = St.Control == C::Jump;
		Sim.SetMotion(M::Air, TEXT("T28"));
		Sim.SetControl(bWasJump ? C::Spin : C::NaturalAir, TEXT("T28"));
		return;
	}
	if (IsBailSurface(St.SurfaceId))
	{
		FRiderImpact Impact;
		Impact.Point = St.ContactPoint;
		Impact.Direction = Unit(St.Velocity, St.BasisForward);
		Impact.Normal = St.GroundNormal;
		Impact.Speed = FVector::DotProduct(St.Velocity, St.GroundNormal);
		Sim.StartWipeOut(Impact, TEXT("T60 bail surface"));
		return;
	}
	if (St.SurfaceId != FRiderSim::OutOfBoundsSurface)
	{
		St.LastGroundPosition = St.Position;
		St.LastGroundHeading = St.ContactForward;
	}

	const FVector TargetUp = Unit(St.GroundNormal + FVector(0, 0, 0.5 * St.GroundNormal.Z), Z);
	const FVector Cr = FVector::CrossProduct(TargetUp, St.BasisUp);
	const double Sc = Cr.Size();
	if (Sc > 1e-5)
	{
		Sim.RotateBody(Cr, -FMath::Asin(FMath::Min(Sc, 0.99999)) * 0.23334);
	}
}

void FPlaceholderAirMotion::Enter(FRiderSim& Sim, ERiderMotion Previous)
{
	Sim.State.Prediction.AirElapsed = 0.0;
}

void FPlaceholderAirMotion::Update(FRiderSim& Sim)
{
	FRiderState& St = Sim.State;
	// 0x108378, 0x12B340
	SSXAir::BallisticStep(St.Position, St.Velocity, double(float(St.TimeScale) * 0.016666668f));
	St.Prediction.AirElapsed = SSXEE::FpuAdd(float(St.Prediction.AirElapsed), 0.016666668f);   // 0x123EC8

	FGroundProbeResult R;
	bool bFound = false;
	ProbeFromAbove(Sim, R, bFound);
	if (!bFound)
	{
		return;
	}
	const FVector N = R.GroundNormal;
	if (!(R.Height < 0.5 * Sim.Surface(R.Surface).StickHeight() && FVector::DotProduct(N, St.Velocity) < 0.0))
	{
		return;
	}
	if (R.Height < 0.0)
	{
		St.Position -= R.Height * N;
	}
	const double Vn = FVector::DotProduct(St.Velocity, N);
	St.Velocity -= FMath::Min(Vn, 0.0) * N;
	St.GroundNormal = N;
	Sim.SetOrientationFromAxes(St.BasisForward - FVector::DotProduct(St.BasisForward, N) * N, N);
	Sim.RunGroundProbe();
	if (IsBailSurface(R.Surface) || R.Surface == FRiderSim::OutOfBoundsSurface)
	{
		FRiderImpact Impact;
		Impact.Point = R.ContactPoint;
		Impact.Direction = Unit(St.Velocity, St.BasisForward);
		Impact.Normal = N;
		Impact.Speed = Vn;
		Sim.StartWipeOut(Impact, TEXT("T60 landing surface"));
		return;
	}
	Sim.SetMotion(M::Ground, TEXT("T52"));
	Sim.SetControl(C::LandNormal, TEXT("T52"));
}

void FPlaceholderRailMotion::Update(FRiderSim& Sim)
{
	Sim.ApplyTransition(TEXT("T31"));
}

void FPlaceholderGroundControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	FRiderState& St = Sim.State;
	const double Speed = St.Velocity.Size();
	double Turn = In.Turn;
	if (St.BoostStrength <= 0.0)
	{
		Turn = FMath::Clamp(Turn, -0.905, 0.905);
	}
	if (Speed < 1130.0)
	{
		Turn *= Speed / 1130.0;
	}
	double SteerRate = FMath::Clamp(FMath::Abs(Turn - St.Steer.Target) * 7.017, 0.1, 8.018);
	if (IsDeepSnow(St.SurfaceId))
	{
		SteerRate *= 0.6;
	}
	St.Steer.Target = Turn;
	St.Steer.Rate = SteerRate;
	St.Brake.Rate = 5.035 * FMath::Max(FMath::Abs(In.Brake - St.Brake.Target), 0.1);
	St.Brake.Target = FMath::Max(In.Brake, 0.0);

	switch (St.Control)
	{
	case C::Cruise:
	case C::FakieCruise:
		if (St.Brake.Cur == 0.0)
		{
			St.Crouch.Rate = FMath::Max(FMath::Abs(In.Crouch - St.Crouch.Target), 0.1) * 5.0;
			St.Crouch.Target = FMath::Max(In.Crouch, 0.0);
		}
		if (In.bJumpPressed && St.Brake.Cur == 0.0)
		{
			Sim.ApplyTransition(TEXT("T11"));
		}
		break;
	case C::Prewind:
		St.Crouch.Target = 1.0;
		St.Crouch.Rate = 2.0;
		if (!In.bJumpHeld)
		{
			St.LaunchCharge = St.Crouch.Cur;
			St.Crouch.Target = 0.0;
			St.Crouch.Rate = 13.33;
			Sim.ApplyTransition(TEXT("T24"));
		}
		break;
	case C::Jump:
		if (St.Crouch.Cur <= 0.0 && St.Motion == M::Ground)
		{
			const double Fv = Speed < 993.48 ? 0.88093 * Speed + 24.678 : 899.868;
			const double J = FMath::Max(St.LaunchCharge * St.LaunchCharge * (0.09851 + 0.78700 * Sim.Profile.Stat(0x17)) * Fv, 630.88);
			St.Velocity += J * Unit(St.GroundNormal + 0.2 * St.ContactForward, Z);
			Sim.ApplyTransition(TEXT("T25"));
			Sim.ApplyTransition(TEXT("T26"));
		}
		break;
	case C::LandNormal:
		St.Crouch.Target = 0.25;
		St.Crouch.Rate = 8.3333;
		if (St.Crouch.Cur >= 0.25)
		{
			Sim.ApplyTransition(In.bJumpHeld ? TEXT("T22") : TEXT("T21"));
		}
		break;
	case C::LandHard:
		if (St.ControlTime >= 0.5)
		{
			Sim.ApplyTransition(In.bJumpHeld ? TEXT("T19") : TEXT("T18"));
		}
		break;
	case C::ChangeFakie:
		Sim.ApplyTransition(TEXT("T13"));
		break;
	case C::Bump:
		Sim.ApplyTransition(TEXT("T16"));
		break;
	case C::GateToBase:
		Sim.ApplyTransition(TEXT("T44"));
		break;
	case C::GetupFromSit:
	{
		St.Velocity = FVector::ZeroVector;
		const int32 N = SSXAnim::FAnimTable::Shared().Frames(SSXAnim::SitUp, St.BoardClass, Sim.Profile.GetTrickCharacter());
		if (SSXAnim::FramesInControl(St) >= SSXAnim::ControlFrames(N > 0 ? N : 56))
		{
			Sim.ApplyTransition(TEXT("T41"));
			St.Velocity += 416.67 * St.BasisForward;
			St.Crouch.Cur = St.Crouch.Target = 0.0;
			St.Brake.Cur = St.Brake.Target = 0.0;
		}
		break;
	}
	default:
		break;
	}
}

void FPlaceholderLessonWaitControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	Sim.ApplyTransition(TEXT("T07"));
}

void FPlaceholderResetWaitControl::Exit(FRiderSim& Sim, ERiderControl Next)
{
	// 0x106A48
	Sim.State.RenderAlpha = 1.0;
	Sim.State.ResetFade = 0.0;
}

void FPlaceholderResetWaitControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x106A90, 0x1A7848, 0x118C08, 0x119220, 0x106A48
	FRiderState& St = Sim.State;
	const float T = SSXEE::FpuAdd(float(St.ResetWaitTime), 0.016666668f);
	St.ResetWaitTime = T;
	float Alpha = SSXEE::FpuSub(0.5f, T);
	Alpha = SSXEE::FpuAdd(Alpha, Alpha);
	St.RenderAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (0.2f < T)
	{
		St.ResetFade = FMath::Clamp(SSXEE::FpuSub(1.0f, SSXEE::FpuSub(0.5f, T) / 0.3f), 0.0f, 1.0f);
	}
	if (0.8f < T)
	{
		Sim.Respawn();
	}
}

}
