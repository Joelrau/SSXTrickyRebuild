#include "Sim/Ground/RailCore.h"
#include "Sim/EEFloat.h"
#include "Sim/EEQuat.h"

namespace SSXRail
{
namespace
{

}

const TCHAR* LexCatchVerdict(ECatchVerdict V)
{
	switch (V)
	{
	case ECatchVerdict::NoVelocity: return TEXT("no velocity");
	case ECatchVerdict::NoRail: return TEXT("no rail");
	case ECatchVerdict::Behind: return TEXT("rail behind (c.v < -0.2)");
	case ECatchVerdict::TrickBlocks: return TEXT("channel-0 grab/tweak/uber without flag 4");
	case ECatchVerdict::OutsideR0: return TEXT("outside the box along R0");
	case ECatchVerdict::OutsideR1: return TEXT("outside the box along R1 (height)");
	case ECatchVerdict::OutsideR2: return TEXT("outside the box along R2 (across)");
	case ECatchVerdict::Caught: return TEXT("caught");
	}
	return TEXT("?");
}

bool CatchTest(FRailRider& R, const IRiderWorld& World, bool bTrickBlocks, FCatchDiag* OutDiag)
{
	// 0x125CB8
	using namespace SSXEE;
	FCatchDiag Local;
	FCatchDiag& Dg = OutDiag ? *OutDiag : Local;
	Dg = FCatchDiag();
	Dg.Box = float(R.BoxFilter.Cur);
	const FVector3f V = F(R.Vel);
	const float V2 = Dot(V, V);
	if (V2 == 0.0f)
	{
		Dg.Verdict = ECatchVerdict::NoVelocity;
		return false;
	}
	const FVector3f Vh = Scale(Rsqrt(V2), V);
	FRiderRailQuery Q;
	Q.Origin = R.Board.Origin;
	FRiderRailHit Hit;
	if (!World.QueryRail(Q, Hit))
	{
		Dg.Verdict = ECatchVerdict::NoRail;
		return false;
	}
	FVector3f T = F(Hit.Tangent);
	if (Dot(Vh, T) < 0.0f)
	{
		T = Scale(-1.0f, T);
	}
	const FVector3f R0 = F(R.Board.R0), R1 = F(R.Board.R1), R2 = F(R.Board.R2);
	const FVector3f C = Add(Sub(F(Hit.Point), F(R.Board.Origin)), Scale(-30.0f, R1));
	const float Rf = float(R.BoxFilter.Cur);
	const float Om = FpuSub(1.0f, Rf);
	const float L0 = FpuAdd(Rf * 150.0f, Om * 80.0f) * 0.9f, L1 = FpuAdd(Rf * 50.0f, Om * 50.0f) * 0.9f,
		L2 = FpuAdd(Rf * 50.0f, Om * 30.0f) * 0.9f;
	Dg.Spline = Hit.Spline;
	Dg.C0 = Dot(R0, C);
	Dg.C1 = Dot(R1, C);
	Dg.C2 = Dot(R2, C);
	Dg.L0 = L0;
	Dg.L1 = L1;
	Dg.L2 = L2;
	Dg.Cv = Dot(C, Vh);
	if (Dg.Cv < -0.2f || bTrickBlocks)
	{
		Dg.Verdict = bTrickBlocks && !(Dg.Cv < -0.2f) ? ECatchVerdict::TrickBlocks : ECatchVerdict::Behind;
		return false;
	}
	if (!(FMath::Abs(Dg.C0) < L0 && FMath::Abs(Dg.C1) < L1 && FMath::Abs(Dg.C2) < L2))
	{
		Dg.Verdict = !(FMath::Abs(Dg.C0) < L0) ? ECatchVerdict::OutsideR0 : (!(FMath::Abs(Dg.C1) < L1) ? ECatchVerdict::OutsideR1 : ECatchVerdict::OutsideR2);
		return false;
	}
	Dg.Verdict = ECatchVerdict::Caught;
	const float Vt = Dot(V, T);
	const float Floor = 555.55554f < Vt ? Vt : 555.55554f;
	const float Len = Sqrt(V2);
	const float NewSpeed = Floor < Len ? Floor : Len;
	FVector3f Vn = Scale(NewSpeed, T);
	Vn.Z = Vn.Z * 0.1f;
	R.Vel = D(Vn);
	// 0x2510F8
	const float A = SSXAnim::Atan2EE(Dot(R2, T), Dot(R0, T));
	R.GrindType = !(FMath::Abs(A) <= 2.6179938f) ? 2 : (A < -0.5235988f ? 4 : (0.5235988f < A ? 3 : 1));
	R.Surface = Hit.Surface;
	R.RailPoint = Hit.Point;
	R.Tangent = D(T);
	return true;
}

void EnterRail(FRailRider& R)
{
	R.RailTime = 0.0;
	R.Roll = 0.0;
	R.BoxFilter.Target = 0.0;
	R.BoxFilter.Rate = FilterRate;
}

void ExitRail(FRailRider& R)
{
	R.Steer = 0.0;
	R.BoxFilter.Cur = FilterExitValue;
	R.BoxFilter.Target = 1.0;
	R.BoxFilter.Rate = FilterRate;
}

ESlideResult SlideStep(FRailRider& R, const IRiderWorld& World)
{
	// 0x10B0D0
	using namespace SSXEE;
	const float Dt = float(R.TimeScale) * 0.016666668f;
	const FVector3f V0 = F(R.Vel);
	const float V2 = Dot(V0, V0);
	const FVector3f Vh = V2 > 0.0f ? Scale(Rsqrt(V2), V0) : FVector3f::ZeroVector;

	// 0x259330
	FRiderRailQuery Q;
	Q.Origin = R.Board.Origin;
	FRiderRailHit Hit;
	if (!World.QueryRail(Q, Hit))
	{
		return ESlideResult::NoHit;
	}
	R.Surface = Hit.Surface;
	R.RailPoint = Hit.Point;
	FVector3f T = F(Hit.Tangent);
	if (Dot(Vh, T) < 0.0f)
	{
		T = Scale(-1.0f, T);
	}
	R.Tangent = D(T);

	const float Speed = Sqrt(V2);
	const float K = Speed < 555.55554f ? 0.5f : Speed * 0.00090000004f;
	const float KK = K * K;

	const FVector3f O = F(R.Board.Origin);
	const FVector3f Dv = Sub(F(Hit.Point), O);
	const FVector3f C = Add(Dv, Scale(-30.0f, F(R.Board.R1)));
	const float A0 = Dot(F(R.Board.R0), C), A1 = Dot(F(R.Board.R1), C), A2 = Dot(F(R.Board.R2), C);
	float Rf = float(R.BoxFilter.Cur);
	if (float(R.RailTime) < 0.6f)
	{
		Rf = 1.0f;
		R.BoxFilter.Cur = 1.0;
	}
	const float Om = FpuSub(1.0f, Rf);
	const float H0 = FpuAdd(Rf * 150.0f, Om * 80.0f);
	const float H1 = FpuAdd(Rf * 50.0f, Om * 50.0f);
	const float H2 = FpuAdd(Rf * 50.0f, Om * 30.0f);
	if (!(FMath::Abs(A0) < H0) || !(FMath::Abs(A1) < H1) || !(FMath::Abs(A2) < H2))
	{
		FVector3f U = Normalize(Dv);
		if (0.5f < FMath::Abs(Dot(U, T)))
		{
			U = T;
		}
		if (Dot(V0, U) < 0.0f)
		{
			const FVector3f Vn = Normalize(V0);
			U = Normalize(Sub(U, Scale(Dot(U, Vn), Vn)));
		}
		R.Vel = D(Sub(V0, Scale(277.77777f, U)));
		return ESlideResult::FellOff;
	}

	const FVector3f Side = F(R.Side), Up = F(R.Up);
	const float Lat = Dot(Side, Dv);
	R.Lateral = Lat;
	const float Time = FpuAdd(float(R.RailTime), Dt);
	R.RailTime = Time;
	float Hs = Dot(Dv, Up);
	Hs = Hs < -8.333333f ? -8.333333f : (8.333333f < Hs ? 8.333333f : Hs);
	FVector3f P = Add(F(R.Pos), Scale(Hs, Up));
	const float Vt = Dot(V0, T);
	const float Pull = Dt * 30.0f;
	FVector3f V = Add(Scale(Pull, Scale(Vt, T)), Scale(FpuSub(1.0f, Pull), V0));
	FVector3f Acc = Scale(Dot(FVector3f(0.0f, 0.0f, -980.0f), T), T);
	if (0.0f < float(R.BoostStrength))
	{
		Acc = Add(Acc, Scale(float(R.BoostStrength) * (0.0f < Vt ? 2450.8232f : -2450.8232f), T));
	}
	V = Add(V, Scale(Dt, Acc));
	P = Add(P, Scale(Dt, V));

	const float K2 = K < 0.5f ? 0.5f : (2.0f < K ? 2.0f : K);
	const bool bUpsideDown = Up.Z < 0.0f;
	const float Steer = float(R.Steer);
	const float SteerSide = bUpsideDown ? Steer : -Steer;
	float Cb = (0.92f < FMath::Abs(T.Z) || Time < 0.6f) ? -5.0f : 1.8153523f;
	if (FMath::Abs(Lat) < 2.5000005f && -1.0f < Cb)
	{
		Cb = -1.0f;
	}
	float M = 1.0f / KK;
	M = M < 1.0f ? 1.0f : (4.0f < M ? 4.0f : M);
	float LatC = Lat;
	if (0.0f <= Cb)
	{
		LatC = Lat < -20.0f ? -20.0f : (20.0f < Lat ? 20.0f : Lat);
	}
	float W = FpuSub(SteerSide * (K2 * 145.07674f), LatC * (Cb * M));
	if (bUpsideDown && FMath::Abs(Lat) < 20.0f)
	{
		if (0.0f < Lat) { W = -290.15347f < W ? -290.15347f : W; }
		else { W = W < 290.15347f ? 290.15347f : W; }
	}
	P = Add(P, Scale(W * Dt, Side));
	R.Pos = D(P);
	R.Vel = D(V);

	const bool bSideways = R.GrindType == 3 || R.GrindType == 4;
	float Roll = bSideways ? (Lat * 0.6981317f) * 0.0125f : (Lat * 0.34906584f) * 0.033333335f;
	if (bUpsideDown)
	{
		Roll = -Roll;
	}
	const float Prev = float(R.Roll);
	if (Prev > FpuAdd(Roll, 5.0f)) { Roll = FpuSub(Prev, 5.0f); }
	else if (FpuSub(Roll, 5.0f) > Prev) { Roll = FpuAdd(Prev, 5.0f); }
	R.Roll = Roll;
	return ESlideResult::OnRail;
}

FQuat RailFrame(const FVector& InT, const FVector& CurrentUp, double InRoll, FVector& OutRailUp)
{
	// 0x10B0D0, 0x250C10
	using namespace SSXEE;
	const FVector3f T = F(InT), Up = F(CurrentUp);
	float S, C;
	SSXAnim::SinCosEE(float(InRoll), S, C);
	FVector3f U;
	if (FMath::Abs(T.Z) <= 0.92f)
	{
		// z made perpendicular to t (VMUL by t.z, VSUB, VRSQRT), negated (x -1) when it points away from the body up
		U = Normalize(Sub(FVector3f(0.0f, 0.0f, 1.0f), Scale(T.Z, T)));
		if (Dot(U, Up) < 0.0f)
		{
			U = Scale(-1.0f, U);
		}
	}
	else
	{
		const FVector3f Sx = Normalize(Cross(Up, T));
		const FVector3f U0 = Cross(T, Sx);
		U = Sub(Scale(C, U0), Scale(S, Sx));
	}
	OutRailUp = D(U);
	// 0x10C120, 0x2508B8, 0x129008
	const FVector3f Wv = Add(Scale(C, U), Scale(S, Cross(U, T)));
	FQf Q{ 1.0f, 0.0f, 0.0f, 0.0f };
	QFromTo(Q, FVector3f(0.0f, 0.0f, 1.0f), Wv);
	QFromTo(Q, QRot(Q, FVector3f(0.0f, 1.0f, 0.0f)), T);
	return FQuat(Q.X, Q.Y, Q.Z, Q.W);
}

FQuat GameSlerp(const FQuat& A, const FQuat& B, double F)
{
	// 0x250A48, 0x129008
	const SSXEE::FQf R = SSXEE::QSlerp({ float(A.W), float(A.X), float(A.Y), float(A.Z) }, { float(B.W), float(B.X), float(B.Y), float(B.Z) }, float(F));
	return FQuat(R.X, R.Y, R.Z, R.W);
}
}
