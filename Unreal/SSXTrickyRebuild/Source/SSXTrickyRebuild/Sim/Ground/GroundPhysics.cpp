#include "Sim/Ground/GroundPhysics.h"

#include "Sim/RiderSim.h"
#include "Sim/SurfaceTable.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderPose.h"

namespace SSXGround
{
namespace
{
	FORCEINLINE FVector Unit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}
}

void YawLeadConstants(int32 BoardClass, double& OutKa, double& OutKb)
{
	switch (BoardClass)
	{
	case 1: OutKa = 0.4; OutKb = 0.5240; break;
	case 2: OutKa = 0.2083; OutKb = 0.2622; break;
	default: OutKa = 0.3002; OutKb = 0.3492; break;
	}
}

double SteerKnee(int32 BoardClass)
{
	switch (BoardClass)
	{
	case 1: return double(1119.0095f);   // 0x126948
	case 2: return double(1138.2727f);
	default: return double(1134.9739f);
	}
}

double FGroundRider::SwitchScale() const
{
	if (!bRidingSwitch || BoardClass == 1)
	{
		return 1.0;
	}
	return BoardClass == 0 ? 0.85 : 0.70;
}

float FGroundRider::SwitchScaleF() const
{
	if (!bRidingSwitch || BoardClass == 1)
	{
		return 1.0f;
	}
	return BoardClass == 0 ? 0.84998363f : 0.69987833f;
}

namespace
{
	FORCEINLINE float StatLerp(float Byte, float K, float C) { return SSXEE::FpuAdd((Byte * K) * 0.003921569f, C); }
	FORCEINLINE float SqrtVU(float X) { return float(FMath::Sqrt(double(FMath::Max(X, 0.0f)))); }
}

float NormalSupport(float H, float Vn, const FSurfaceParams& S, float P7, float P8)
{
	// 0x109878
	const float G = S.F[0], Damp = S.F[9];
	if (0.0f < H)
	{
		float A = -(G / 30.0f) * H;
		if (0.0f < Vn)
		{
			A = SSXEE::FpuAdd(A, (-Damp) * Vn);
		}
		return A;
	}
	float A;
	if (-P7 < H)
	{
		A = ((-G) * H) / P7;
	}
	else
	{
		const float Hc = -P8 < H ? H : -P8;
		const float T = SSXEE::FpuAdd(Hc, P7);
		A = G * SSXEE::FpuSub(1.0f, SSXEE::FpuAdd(T, T) / SSXEE::FpuSub(P8, P7));
	}
	return SSXEE::FpuAdd(A, (-Damp) * Vn);
}

float GripCurve(float AbsVf)
{
	// 0x109EF8
	if (AbsVf < 555.55554f)
	{
		return SSXEE::FpuAdd(SSXEE::FpuSub(AbsVf, 0.0f) * 0.0008899726f, 0.20103703f);
	}
	if (AbsVf < 1388.8888f)
	{
		return SSXEE::FpuAdd(SSXEE::FpuSub(AbsVf, 555.55554f) * 0.00036129795f, 0.6954663f);
	}
	return SSXEE::FpuAdd(SSXEE::FpuSub(AbsVf, 1388.8888f) * -0.00010441317f, 0.9965479f);
}

float LateralGrip(const FGroundRider& R, float Vs, float Vf, float Steer, float BoostStrength, const FSurfaceParams& S)
{
	// 0x109EF8
	float X = Vs * GripCurve(FMath::Abs(Vf));
	const float E = SSXEE::FpuAdd(FMath::Abs(Steer) * 1.0001484f, 0.0f);
	if (!(E < 1.0f))
	{
		X = X * E;
	}
	const float GStat = StatLerp(R.Byte13, 1.1412175f, 0.001f) * R.SwitchScaleF();
	float A = (X * -S.F[4]) * GStat;
	if (0.0f < BoostStrength)
	{
		A = A * (1.0f / SSXEE::FpuAdd(BoostStrength * 3.4999945f, 1.0f));
	}
	return A;
}

float LongitudinalDrag(const FGroundRider& R, float Vf, float Load, float Crouch, float Brake, float BoostStrength,
                       int32 SurfaceId, float Height, float P8, const FSurfaceParams& S)
{
	// 0x109CB8
	const float L = 1.0f <= Load ? Load : 1.0f;
	float CQuad = StatLerp(R.Byte19, -0.29035342f, 1.2848105f);
	float CBrk = StatLerp(R.Byte11, 0.9181778f, 0.6150995f);
	float CLin = R.BoardClass == 2 ? StatLerp(R.Byte0E, -0.30197912f, 0.70764244f) : StatLerp(R.Byte0E, -0.30845523f, 1.0649822f);
	if (R.bRidingSwitch && R.BoardClass != 1)
	{
		const float Sw = R.SwitchScaleF();
		CBrk = CBrk * Sw;
		CQuad = CQuad * Sw;
		CLin = CLin * Sw;
	}
	const float Ice = IsDeepSnow(SurfaceId) ? SSXEE::FpuSub(0.5f, Height / P8) : 1.0f;
	const float X = FMath::Abs(Vf) * 0.001f;
	const float Lin = (L * S.F[1]) * CLin;
	const float Stand = SSXEE::FpuSub(1.0f, Crouch) * 0.10572864f;
	const float Brk = (((SSXEE::FpuAdd(BoostStrength * 1.2153687f, 1.0f) * 1.7513076f) * Brake) * Brake) * CBrk;
	const float Quad = X * SSXEE::FpuAdd(L * S.F[2], X * ((L * S.F[3]) * CQuad));
	return ((-Vf) * Ice) * SSXEE::FpuAdd(SSXEE::FpuAdd(SSXEE::FpuAdd(Lin, Stand), Brk), Quad);
}

float PathAlignment(float Yaw, float PathHeading)
{
	// 0x109950
	const float D = SSXEE::FpuSub(Yaw, PathHeading);
	const float T = SSXEE::FpuAdd(D * 0.15915494f, 0.5f);
	float Fl = float(int32(T));
	if (T < Fl)
	{
		Fl = SSXEE::FpuSub(Fl, 1.0f);
	}
	const float W = SSXEE::FpuSub(1.0471976f, FMath::Abs(SSXEE::FpuSub(D, Fl * 6.2831855f))) / 0.5235988f;
	return W < 1.0f ? W : 1.0f;
}

float Propulsion(const FGroundRider& R, float Speed, float Steer, float Brake, float BoostStrength,
                 float BurstTimer, float W, float ForwardZ, bool bPushAnim, const FSurfaceParams& S)
{
	// 0x109950
	if (0.0f < Brake)
	{
		return 0.0f;
	}
	float B = BoostStrength;
	if (0.0f < BurstTimer && B < W * 5.5f)
	{
		B = W * 5.5f;
	}
	float Acc = 0.0f;
	if (0.0f < B)
	{
		const float Gate = SSXEE::FpuSub(0.07983315f, FMath::Abs(Steer));
		if (0.0f < Gate)
		{
			const float BG = B * Gate;
			Acc = SSXEE::FpuAdd(0.0f, ForwardZ < 0.0f ? BG * 2350.3096f : BG * SSXEE::FpuAdd(ForwardZ * 1053.4484f, 2350.3096f));
		}
	}
	float Deficit = SSXEE::FpuSub(S.F[11] * 27.777777f, Speed);
	if (!(Deficit < 1111.1111f))
	{
		Deficit = 1111.1111f;
	}
	if (0.0f < Deficit)
	{
		const float Ks = R.BoardClass == 2 ? StatLerp(R.Byte0E, 0.3049984f, 1.2049915f) : StatLerp(R.Byte0E, 0.27690744f, 0.7380923f);
		const float Aligned = 0.0f <= W ? SSXEE::FpuAdd((W * Ks) * Deficit, 0.0f) : 0.0f;
		const float Pushing = bPushAnim ? SSXEE::FpuAdd((Ks * 0.2f) * Deficit, 0.0f) : 0.0f;
		Acc = SSXEE::FpuAdd(Acc, Pushing < Aligned ? Aligned : Pushing);
	}
	return S.F[12] * Acc;
}

float YawIncrement(const FVector3f& V, const FVector3f& N, const FVector3f& F, const FVector3f& FallLine, float Steer,
                   float Crouch, int32 BoardClass, float Dt)
{
	// 0x10A0D8, 0x250FD0
	const float Speed = SqrtVU(SSXEE::Dot(V, V));
	const float Q = 1.0f / Speed;
	float Sb = SSXEE::Dot(SSXEE::Scale(Q, SSXEE::Cross(V, F)), N);
	Sb = Sb < -0.99999f ? -0.99999f : (0.99999f < Sb ? 0.99999f : Sb);
	const float Beta = SSXAnim::AsinEE(Sb);
	float Ka, Kb;
	if (BoardClass == 2) { Ka = 0.20833896f; Kb = 0.2622103f; }
	else if (BoardClass == 0) { Ka = 0.3001892f; Kb = 0.34918553f; }
	else { Ka = 0.4f; Kb = 0.5239824f; }
	float Err = SSXEE::FpuAdd(-Beta, ((SSXEE::FpuAdd((Ka * 0.5f) * SSXEE::FpuSub(1.0f, Steer * Steer), 1.0f) * Steer) * Kb) / SSXEE::FpuAdd(Crouch * 0.01f, 1.0f));
	if (SSXEE::Dot(V, F) < 0.0f)
	{
		Err = -Err;
	}
	float Gain = ((Speed * 2.0029847e-05f) * Speed) * Dt;
	if (1.0f < Gain)
	{
		Gain = 1.0f;
	}
	const float VFall = SSXEE::Dot(V, FallLine);
	float E1 = Speed / 555.55554f;
	if (1.0f < E1)
	{
		E1 = 1.0f;
	}
	float E = (E1 * SSXEE::FpuSub(0.5f, FMath::Abs(VFall) / Speed)) * 40.0f;
	if (E < 0.0f)
	{
		E = 0.0f;
	}
	E = SSXEE::FpuAdd(E, 0.01004205f);
	if (1.0f < E)
	{
		E = 1.0f;
	}
	const float SteerDir = Err <= 0.0f ? -Steer : Steer;
	Gain = SteerDir < E ? Gain * E : Gain * SteerDir;
	float A = Err * Gain;
	return A < -0.10471976f ? -0.10471976f : (0.10471976f < A ? 0.10471976f : A);
}

double LaunchImpulse(double Speed, double Charge, double StatJump, double JMin)
{
	// 0x1284E0
	const float Sp = float(Speed), C = float(Charge);
	const float Fv = Sp < 993.4801f ? SSXEE::FpuAdd(Sp * 0.88093346f, 24.678448f) : 899.86835f;
	const float S = StatLerp(float(StatJump * 255.0), 0.78700274f, 0.09851468f);
	const float J = (C * (C * S)) * Fv;
	return J < float(JMin) ? float(JMin) : J;
}

FVector LaunchVelocity(const FVector& V, const FVector& N, const FVector& F, const FVector& U, double J, bool bOnRail, bool* OutWall)
{
	if (OutWall)
	{
		*OutWall = false;
	}
	// 0x1284E0
	const float Jf = float(J);
	const FVector3f Vf = SSXEE::F(V), Nf = SSXEE::F(N), Ff = SSXEE::F(F);
	if (bOnRail)
	{
		return SSXEE::D(SSXEE::Add(Vf, SSXEE::Scale(Jf, SSXEE::F(U))));
	}
	const float C50 = SSXAnim::CosEE(0.8726646f), C70 = SSXAnim::CosEE(1.2217305f);
	const float Steep = SSXEE::FpuSub(Nf.Z, C50) * (1.0f / SSXEE::FpuSub(C70, C50));
	if (Ff.Z < 0.0f || Steep < 0.0f || Vf.Z < 0.0f)
	{
		const FVector3f D = SSXEE::Normalize(SSXEE::Add(Nf, SSXEE::Scale(0.2f, Ff)));
		return SSXEE::D(SSXEE::Add(Vf, SSXEE::Scale(Jf, D)));
	}
	if (OutWall)
	{
		*OutWall = true;
	}
	const FVector3f Hn = SSXEE::Normalize(FVector3f(Nf.X, Nf.Y, 0.0f));
	const FVector3f Q = SSXEE::Normalize(SSXEE::Sub(Ff, SSXEE::Scale(SSXEE::Dot(Ff, Hn), Hn)));
	const float S0 = SSXAnim::SinEE(0.0f), S20 = SSXAnim::SinEE(0.34906584f);
	const float Bt = Steep * SSXEE::FpuSub(Q.Z, S0) * (1.0f / SSXEE::FpuSub(S20, S0));
	const float B = Bt < 0.0f ? 0.0f : (0.9f < Bt ? 0.9f : Bt);
	const float Om = SSXEE::FpuSub(1.0f, B);
	const FVector3f D = SSXEE::Normalize(SSXEE::Add(SSXEE::Scale(Om, Nf), SSXEE::Scale(B, Q)));
	const FVector3f Mix = SSXEE::Add(SSXEE::Scale(Om, Vf), SSXEE::Scale(B * SSXEE::Length(Vf), Q));
	return SSXEE::D(SSXEE::Add(SSXEE::Scale(0.5f, Mix), SSXEE::Scale(Jf, D)));
}

double ApplyLaunch(FRiderSim& Sim, double JMin, double StatJump)
{
	FRiderState& St = Sim.State;
	const float ChargeF = SSXEE::FpuSub(float(St.LaunchCharge), float(St.Crouch.Cur));
	const double Charge = ChargeF < 0.0f ? 0.0f : ChargeF;
	const double J = LaunchImpulse(SSXEE::Length(SSXEE::F(St.Velocity)), Charge, StatJump, JMin);
	St.Velocity = LaunchVelocity(St.Velocity, St.GroundNormal, St.ContactForward, St.BasisUp, J,
	                             St.Motion == ERiderMotion::RailSlide, &St.bLaunchWall);
	return J;
}

double LandingSpeedFactor(double YawErr, double Pitch)
{
	const double P = FMath::Clamp(FMath::Abs(Pitch), FMath::DegreesToRadians(15.0), FMath::DegreesToRadians(50.0));
	const double Y = FMath::Clamp(FMath::Abs(YawErr), FMath::DegreesToRadians(25.0), FMath::DegreesToRadians(80.0));
	return (1.06425 - 0.24542 * P) * (1.11359 - 0.26032 * Y);
}

double LandingAbsorb(double ImpactVn)
{
	return FMath::Clamp(-K::AbsorbK * ImpactVn, 0.25, 1.0);
}

FLandingResult ClassifyLanding(double YawErr, double Pitch, double ImpactVn, double StatLanding, int32 BoardClass, bool bRidingSwitch)
{
	FLandingResult R;
	R.SpeedFactor = 1.0;
	R.Absorb = 0.0;
	const double Scaled = Pitch * (2.0408 - 1.0374 * StatLanding);
	if (!(Scaled >= -2.968 && Scaled <= 2.684))
	{
		R.Kind = ELandingKind::WipeOut;
		return R;
	}
	const bool bOkPitch = Pitch >= -UE_DOUBLE_PI / 4.0;
	const bool bSoft = ImpactVn >= K::LandImpactHard;
	if (BoardClass == 2)
	{
		const bool bForward = FMath::Abs(YawErr) <= UE_DOUBLE_PI / 2.0;
		R.Kind = (bForward && !bRidingSwitch && bSoft && bOkPitch) ? ELandingKind::Normal : ELandingKind::Hard;
	}
	else
	{
		R.Kind = (bOkPitch && bSoft) ? ELandingKind::Normal : ELandingKind::Hard;
	}
	R.SpeedFactor = LandingSpeedFactor(YawErr, Pitch);
	R.Absorb = LandingAbsorb(ImpactVn);
	return R;
}
}
