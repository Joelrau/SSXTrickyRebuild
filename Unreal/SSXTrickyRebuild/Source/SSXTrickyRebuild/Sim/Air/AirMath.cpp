#include "Sim/Air/AirMath.h"
#include "Sim/EEFloat.h"
#include "Sim/EEQuat.h"
#include "Sim/Anim/RiderPose.h"

#include "Sim/Anim/RiderAnim.h"
#include "Sim/RiderState.h"

namespace SSXAir
{
namespace
{
	FORCEINLINE double Blend(double A, double B, double S01) { return A + (B - A) * S01; }

	/** normalize, or V unchanged when zero. */
	FORCEINLINE FVector UnitOrSame(const FVector& V)
	{
		const double L = V.Size();
		return L > 0.0 ? V / L : V;
	}

	FORCEINLINE FQuat AxisAngle(const FVector& Axis, double Angle)
	{
		const double S = FMath::Sin(Angle * 0.5);
		return FQuat(Axis.X * S, Axis.Y * S, Axis.Z * S, FMath::Cos(Angle * 0.5));
	}

	FORCEINLINE FQuat Normalised(const FQuat& Q)
	{
		const double N = FMath::Sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
		return N > 0.0 ? FQuat(Q.X / N, Q.Y / N, Q.Z / N, Q.W / N) : FQuat::Identity;
	}

	const FVector AxisX(1, 0, 0), AxisY(0, 1, 0), AxisZ(0, 0, 1);
}

double SpinGain(const FRiderProfile& P)      { return Blend(0.5449139, 0.5449139 + 0.6741781, P.Stat(0x1A)); }
double SpinDecay(const FRiderProfile& P)     { return Blend(0.9930117, 0.9930117 + 0.002014339, P.Stat(0x1A)); }
double LandScale(const FRiderProfile& P)     { return Blend(2.040804, 2.040804 - 1.0374061, P.Stat(0x16)); }
double BoostGain(const FRiderProfile& P)     { return Blend(0.9787758, 0.9787758 + 0.35966223, P.Stat(0x1D)); }
double AirTurnRate(const FRiderProfile& P)   { return Blend(0.54741764, 0.54741764 + 0.6545031, P.Stat(0x10)) * 1.3750083; }
double GrabAnimSpeed(const FRiderProfile& P) { return SSXAnim::GrabSpeed(P.Stat(0x14)); }

void BallisticStep(FVector& P, FVector& V, double Dt)
{
	// 0x12B340
	using namespace SSXEE;
	const float T = float(Dt);
	auto Accel = [](const FVector3f& U)
	{
		const float G = 0.0f < U.Z ? -850.24139f : -1900.8359f;
		return Add(FVector3f(0.0f, 0.0f, G), FVector3f(U.X * -0.20002478f, U.Y * -0.20002478f, 0.0f));
	};
	const float Half = 1.0f / 2.0f, Sixth = 1.0f / 6.0f, Third = 1.0f / 3.0f;
	const FVector3f P0 = F(P), V0 = F(V);
	const FVector3f K1x = Scale(T, V0), K1v = Scale(T, Accel(V0));
	const FVector3f V2 = Add(V0, Scale(Half, K1v));
	const FVector3f K2x = Scale(T, V2), K2v = Scale(T, Accel(V2));
	const FVector3f V3 = Add(V0, Scale(Half, K2v));
	const FVector3f K3x = Scale(T, V3), K3v = Scale(T, Accel(V3));
	const FVector3f V4 = Add(V0, K3v);
	const FVector3f K4x = Scale(T, V4), K4v = Scale(T, Accel(V4));
	const FVector3f P1 = Add(Add(P0, Scale(Sixth, Add(K1x, K4x))), Scale(Third, Add(K2x, K3x)));
	FVector3f V1 = Add(Add(V0, Scale(Sixth, Add(K1v, K4v))), Scale(Third, Add(K2v, K3v)));
	const float Speed = Length(V1);
	const float Cap = float(AirSpeedCap);
	if (Cap < Speed)
	{
		V1 = Scale(Cap / Speed, V1);
	}
	P = D(P1);
	V = D(V1);
}

double WrapPi(double A)
{
	return A - 2.0 * PI * FMath::Floor(A / (2.0 * PI) + 0.5);
}

FQuat TrickRotation(double SpinAngle, double FlipAngle)
{
	return AxisAngle(AxisZ, -SpinAngle) * AxisAngle(AxisX, -FlipAngle);
}

FQuat FoldTrick(const FQuat& Q, double SpinAngle, double FlipAngle)
{
	return Normalised(Q * TrickRotation(SpinAngle, FlipAngle));
}

namespace
{
	// 0x11DB00
	using SSXEE::FQf; using SSXEE::QMul; using SSXEE::QAxis; using SSXEE::QRot;
	// 0x250D60
	void RelAngleAxis(const FQf& Rel, float& OutAngle, FVector3f& OutAxis)
	{
		using SSXEE::FpuAdd; using SSXEE::FpuSub;
		float C, H;
		if (0.0f < Rel.W) { C = Rel.W <= 1.0f ? Rel.W : 1.0f; H = 0.5f; }
		else { C = -1.0f <= Rel.W ? -Rel.W : 1.0f; H = -0.5f; }
		const float A = SSXAnim::AsinEE(C);
		OutAngle = FpuAdd(FpuSub(1.5707964f, A), FpuSub(1.5707964f, A));
		const float Q = 1.0f / SSXAnim::SinEE(OutAngle * H);
		OutAxis = SSXEE::Scale(Q, FVector3f(Rel.X, Rel.Y, Rel.Z));
	}
	float WrapAbs(float A)
	{
		const float T = SSXEE::FpuAdd(A * 0.15915494f, 0.5f);
		float K = float(int32(T));
		if (T < K) { K = SSXEE::FpuSub(K, 1.0f); }
		return FMath::Abs(SSXEE::FpuSub(A, K * 6.2831855f));
	}
}

void AutoAlignSolve(const FQuat& Q, const FVector& NPred, int32 BoardClass, int32 TakeoffType, double& OutAngle, FVector& OutAxis)
{
	// 0x11DB00, 0x100598
	using namespace SSXEE;
	const FVector3f Np = F(NPred);
	const FVector3f N1 = Normalize(Add(Np, FVector3f(0.0f, 0.0f, Np.Z * 0.5f)));
	// 0x2508B8
	const FVector3f Zf(0.0f, 0.0f, 1.0f);
	FQf QUp{ 1.0f, 0.0f, 0.0f, 0.0f };
	{
		FVector3f Cz = Cross(Zf, N1);
		float Sz = Length(Cz);
		if (Sz != 0.0f)
		{
			Cz = Scale(1.0f / Sz, Cz);
			Sz = Sz < 0.99999f ? Sz : 0.99999f;
			const float Ang = Dot(Zf, N1) < 0.0f ? FpuSub(3.1415927f, SSXAnim::AsinEE(Sz)) : SSXAnim::AsinEE(Sz);
			QUp = QMul(QAxis(Cz, Ang), QUp);
		}
	}
	const FVector3f YUp = QRot(QUp, FVector3f(0.0f, 1.0f, 0.0f));
	const FQf Qq{ float(Q.W), float(Q.X), float(Q.Y), float(Q.Z) };
	const FVector3f Fwd = QRot(Qq, FVector3f(0.0f, 1.0f, 0.0f));   // 0x129008
	const FVector3f Ft = Normalize(Sub(Fwd, Scale(Dot(Fwd, N1), N1)));
	FVector3f Ax = Cross(YUp, Ft);
	const float S = Length(Ax);
	float TwistAng = 0.0f;
	FQf T0 = QUp;
	if (0.0001f < S)
	{
		Ax = Scale(1.0f / S, Ax);
		const float Sc = S < 0.99999f ? S : 0.99999f;
		TwistAng = Dot(YUp, Ft) < 0.0f ? FpuSub(3.1415927f, SSXAnim::AsinEE(Sc)) : SSXAnim::AsinEE(Sc);
		T0 = QMul(QAxis(Ax, TwistAng), QUp);
	}
	else if (Dot(YUp, Ft) < 0.0f)
	{
		T0 = QMul(QAxis(N1, 3.1415927f), QUp);
	}
	const FQf Conj{ Qq.W, -Qq.X, -Qq.Y, -Qq.Z };
	float Angle;
	FVector3f Axis;
	RelAngleAxis(QMul(Conj, T0), Angle, Axis);
	if (BoardClass != 2)
	{
		FQf T1 = QUp;
		if (0.0001f < S)
		{
			T1 = QMul(QAxis(Ax, FpuSub(TwistAng, 3.1415927f)), QUp);
		}
		else if (0.0f < Dot(YUp, Ft))
		{
			T1 = QMul(QAxis(N1, 3.1415927f), QUp);
		}
		float Angle1;
		FVector3f Axis1;
		RelAngleAxis(QMul(Conj, T1), Angle1, Axis1);
		if (WrapAbs(Angle1) < WrapAbs(Angle))
		{
			Angle = Angle1;
			Axis = Axis1;
		}
	}
	const float UpZ = QRot(Qq, FVector3f(0.0f, 0.0f, 1.0f)).Z;
	if (TakeoffType == 0 || 0.0f < UpZ)
	{
		float Limit = N1.Z * 2.09439516f;
		if (Limit < 1.3962634f) { Limit = 1.3962634f; }
		if (Limit < FMath::Abs(Angle)) { Angle = 0.0f; }
	}
	OutAngle = Angle;
	OutAxis = D(Axis);
}

const TCHAR* LexToString(ELandingVerdict V)
{
	switch (V)
	{
	case ELandingVerdict::LandNormal: return TEXT("LandNormal");
	case ELandingVerdict::LandHard: return TEXT("LandHard");
	default: return TEXT("WipeOut");
	}
}

void LandingAngles(const FQuat& Q, const FVector& Velocity, const FVector& Normal, double& OutPsi, double& OutPhi)
{
	const FVector Fwd = Q.RotateVector(AxisY), Up = Q.RotateVector(AxisZ);
	const FVector VHat = UnitOrSame(Velocity);
	const FVector Vb = UnitOrSame(VHat - FVector::DotProduct(VHat, Up) * Up);
	OutPsi = HALF_PI - FMath::Asin(FMath::Clamp(FVector::DotProduct(Fwd, Vb), -0.9999999, 0.9999999));
	OutPhi = -FMath::Atan2(FVector::DotProduct(Fwd, Normal), FVector::DotProduct(Up, Normal));
}

double LandingSpeedFactor(double Psi, double Phi)
{
	const double D15 = FMath::DegreesToRadians(15.0), D50 = FMath::DegreesToRadians(50.0);
	const double D25 = FMath::DegreesToRadians(25.0), D80 = FMath::DegreesToRadians(80.0);
	const double Pc = FMath::Clamp(FMath::Abs(Phi), D15, D50);
	const double Yc = FMath::Clamp(FMath::Abs(Psi), D25, D80);
	double F = 1.0;
	if (Pc > D15)
	{
		F *= 1.0642503 - 0.24541846 * Pc;
	}
	if (Yc > D25)
	{
		F *= 1.113588 - 0.26032454 * Yc;
	}
	return F;
}

FLandingResult EvaluateLanding(const FLandingInput& In)
{
	FLandingResult R;
	R.Vn = FVector::DotProduct(In.Normal, In.Velocity);
	R.ImpactStrength = FMath::Clamp(-ImpactStrengthK * R.Vn, 0.25, 1.0);
	R.FoldedQ = FoldTrick(In.BasisQ, In.PendingSpin, In.PendingFlip);
	double PsiBasis = 0.0, PhiFolded = 0.0;
	LandingAngles(R.FoldedQ, In.Velocity, In.Normal, R.Psi, PhiFolded);
	LandingAngles(In.BasisQ, In.Velocity, In.Normal, PsiBasis, R.PhiBasis);
	R.PhiEff = WrapPi(R.PhiBasis + In.PendingFlip);

	if (In.bGrabActive && !In.bGrabLandable)
	{
		R.bBailGrab = true;
		R.Verdict = ELandingVerdict::WipeOut;
		R.SpeedFactor = 1.0;
		return R;
	}
	if (In.Surface == 0 || In.Surface == 6 || In.Surface == 10)
	{
		R.bBailSurface = true;
		R.Verdict = ELandingVerdict::WipeOut;
		R.SpeedFactor = 1.0;
		return R;
	}
	const double PhiTest = R.Psi > HALF_PI ? -R.PhiEff : R.PhiEff;
	const double Scaled = PhiTest * In.KLand;
	if (!(Scaled >= LandPitchMin && Scaled <= LandPitchMax))
	{
		R.bBailPitch = true;
		R.Verdict = ELandingVerdict::WipeOut;
		R.SpeedFactor = 1.0;
		return R;
	}
	const bool bNoseOk = R.PhiEff >= NoseOkPitch;
	const bool bSoft = R.Vn >= HardImpactVn;
	if (In.bGrabActive && In.bGrabCategory)
	{
		R.Verdict = ELandingVerdict::LandNormal;
	}
	else if (In.BoardClass == 2)
	{
		if (R.Psi <= HALF_PI)
		{
			R.Verdict = (!In.bRidingFakie && bSoft && bNoseOk) ? ELandingVerdict::LandNormal : ELandingVerdict::LandHard;
		}
		else
		{
			R.Verdict = ELandingVerdict::LandHard;
			R.bStanceFlip = true;
		}
	}
	else
	{
		R.Verdict = (bNoseOk && bSoft) ? ELandingVerdict::LandNormal : ELandingVerdict::LandHard;
	}
	R.SpeedFactor = LandingSpeedFactor(R.Psi, R.PhiEff);
	return R;
}

int32 LandHardAnimation(const FLandingResult& R, int32 BoardClass, bool bRidingFakie)
{
	const bool bNoseOk = R.PhiEff >= NoseOkPitch;
	const bool bBackwards = R.Psi > HALF_PI;
	if (BoardClass == 2)
	{
		if (bBackwards || bRidingFakie)
		{
			return SSXAnim::LandFakie;
		}
		return R.Vn < HardImpactVn ? SSXAnim::LandAtomic : SSXAnim::LandTail;
	}
	if (!bBackwards)
	{
		return bNoseOk ? SSXAnim::LandAtomic : SSXAnim::LandTail;
	}
	return bNoseOk ? SSXAnim::LandAtomicBwd : SSXAnim::LandTailBwd;
}
}
