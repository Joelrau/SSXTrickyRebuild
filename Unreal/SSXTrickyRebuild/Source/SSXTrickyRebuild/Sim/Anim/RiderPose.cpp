#include "Sim/Anim/RiderPose.h"
#include "Sim/EEFloat.h"

namespace SSXAnim
{
namespace
{
	constexpr int32 LThigh = 13, LCalf = 14, LFoot = 15, RThigh = 16, RCalf = 17, RFoot = 18;
	constexpr int32 NumFkBones = 13;   // 0x205A78

	FVector3f Cross(const FVector3f& A, const FVector3f& B)
	{
		return FVector3f(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X);
	}

	float Dot(const FVector3f& A, const FVector3f& B)
	{
		return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
	}

	float Sqrt32(float V)
	{
		return float(FMath::Sqrt(double(V)));
	}

	// 0x205E98, 0x12AE18
	void AxisRows(const FVector3f& Ax, float S, float C, float R[3][3])
	{
		const float X = Ax.X, Y = Ax.Y, Z = Ax.Z;
		const float K = 1.0f - C;
		R[0][0] = K * X * X + C;     R[0][1] = K * Y * X - S * Z; R[0][2] = K * Z * X + S * Y;
		R[1][0] = K * X * Y + S * Z; R[1][1] = K * Y * Y + C;     R[1][2] = K * Z * Y - S * X;
		R[2][0] = K * X * Z - S * Y; R[2][1] = K * Y * Z + S * X; R[2][2] = K * Z * Z + C;
	}

	void RotateUpper(FRowMat& M, const float R[3][3])
	{
		float Out[3][3];
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				Out[I][J] = M.M[I][0] * R[0][J] + M.M[I][1] * R[1][J] + M.M[I][2] * R[2][J];
			}
		}
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				M.M[I][J] = Out[I][J];
			}
		}
	}

	FRowMat RowsFromRows(const float R0[4], const float R1[4], const float R2[4], const float R3[4])
	{
		FRowMat M;
		for (int32 J = 0; J < 4; ++J)
		{
			M.M[0][J] = R0[J]; M.M[1][J] = R1[J]; M.M[2][J] = R2[J]; M.M[3][J] = R3[J];
		}
		return M;
	}

	void RowTimes(const float V[4], const FRowMat& M, float Out[4])
	{
		for (int32 J = 0; J < 4; ++J)
		{
			Out[J] = V[0] * M.M[0][J] + V[1] * M.M[1][J] + V[2] * M.M[2][J] + V[3] * M.M[3][J];
		}
	}

	struct FD44 { double M[4][4]; };
	FD44 ToD(const FRowMat& A)
	{
		FD44 D;
		for (int32 I = 0; I < 4; ++I) { for (int32 J = 0; J < 4; ++J) { D.M[I][J] = A.M[I][J]; } }
		return D;
	}
	FD44 MulD(const FD44& A, const FD44& B)
	{
		FD44 C;
		for (int32 I = 0; I < 4; ++I)
		{
			for (int32 J = 0; J < 4; ++J)
			{
				double S = 0.0;
				for (int32 K = 0; K < 4; ++K) { S += A.M[I][K] * B.M[K][J]; }
				C.M[I][J] = S;
			}
		}
		return C;
	}
	bool Inverse3(const double A[3][3], double Out[3][3])
	{
		const double C00 = A[1][1] * A[2][2] - A[1][2] * A[2][1];
		const double C01 = A[1][2] * A[2][0] - A[1][0] * A[2][2];
		const double C02 = A[1][0] * A[2][1] - A[1][1] * A[2][0];
		const double Det = A[0][0] * C00 + A[0][1] * C01 + A[0][2] * C02;
		if (FMath::Abs(Det) < 1e-12) { return false; }
		const double Id = 1.0 / Det;
		Out[0][0] = C00 * Id; Out[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) * Id; Out[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * Id;
		Out[1][0] = C01 * Id; Out[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) * Id; Out[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) * Id;
		Out[2][0] = C02 * Id; Out[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) * Id; Out[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) * Id;
		return true;
	}

	const float AsinC0 = 0.16770227253437042f, AsinC1 = 0.06765850633382797f,
		AsinC2 = 0.05200503021478653f, AsinC3 = 0.05498472973704338f;   // 0x3BB900, 0x3BB90C
}

FRowMat FRowMat::Identity()
{
	FRowMat R;
	for (int32 I = 0; I < 4; ++I) { for (int32 J = 0; J < 4; ++J) { R.M[I][J] = I == J ? 1.0f : 0.0f; } }
	return R;
}

FRowMat FRowMat::operator*(const FRowMat& B) const
{
	FRowMat C;
	for (int32 I = 0; I < 4; ++I)
	{
		for (int32 J = 0; J < 4; ++J)
		{
			C.M[I][J] = M[I][0] * B.M[0][J] + M[I][1] * B.M[1][J] + M[I][2] * B.M[2][J] + M[I][3] * B.M[3][J];
		}
	}
	return C;
}

FVector3f FRowMat::TransformPoint(const FVector3f& V) const
{
	return FVector3f(V.X * M[0][0] + V.Y * M[1][0] + V.Z * M[2][0] + M[3][0],
		V.X * M[0][1] + V.Y * M[1][1] + V.Z * M[2][1] + M[3][1],
		V.X * M[0][2] + V.Y * M[1][2] + V.Z * M[2][2] + M[3][2]);
}

// 0x250C10, 0x250E98, 0x250D60, 0x250FD0
using SSXEE::FpuAdd;
using SSXEE::FpuSub;

void SinCosEE(float A, float& OutSin, float& OutCos)
{
	const float K = A < 0.0f ? FpuSub(A * 0.63661975f, 0.5f) : FpuAdd(A * 0.63661975f, 0.5f);
	const int32 Ki = int32(K);
	const int32 Q = Ki & 3;
	const float X = FpuSub(A, float(Ki) * 1.5707964f);
	const float X2 = X * X;
	float P = X2 * 2.7557319e-06f;
	P = FpuAdd(P, -0.0001984127f);
	P = FpuAdd(P * X2, 0.008333334f);
	P = FpuAdd(P * X2, -0.16666667f);
	P = P * X2;
	P = FpuAdd(P, 1.0f);
	const float S = P * X;
	const float C = Sqrt32(FMath::Max(FpuSub(1.0f, S * S), 0.0f));
	switch (Q)
	{
	case 0: OutSin = S; OutCos = C; break;
	case 1: OutSin = C; OutCos = -S; break;
	case 2: OutSin = -S; OutCos = -C; break;
	default: OutSin = -C; OutCos = S; break;
	}
}

namespace
{
	FORCEINLINE float EvenPoly(float R2)
	{
		return FpuAdd(FpuAdd(FpuAdd(FpuAdd(R2 * 2.4801588e-05f, -0.0013888889f) * R2, 0.041666668f) * R2, -0.5f) * R2, 1.0f);
	}
	FORCEINLINE float OddPoly(float R2, float R)
	{
		return FpuAdd((FpuAdd(FpuAdd(FpuAdd(R2 * 2.7557319e-06f, -0.0001984127f) * R2, 0.008333334f) * R2, -0.16666667f) * R2) * R, R);
	}
}

float CosEE(float X)
{
	const float K = X < 0.0f ? FpuSub(X * 0.63661975f, 0.5f) : FpuAdd(X * 0.63661975f, 0.5f);
	const int32 Ki = int32(K);
	const uint32 U = uint32(Ki);
	const float R = FpuSub(X, float(Ki) * 1.5707964f);
	const float R2 = R * R;
	const float Out = (U & 1u) == 0u ? EvenPoly(R2) : OddPoly(R2, R);
	return ((U + 1u) & 2u) != 0u ? -Out : Out;
}

float SinEE(float X)
{
	const float K = X < 0.0f ? FpuSub(X * 0.63661975f, 0.5f) : FpuAdd(X * 0.63661975f, 0.5f);
	const int32 Ki = int32(K);
	const uint32 U = uint32(Ki);
	const float R = FpuSub(X, float(Ki) * 1.5707964f);
	const float R2 = R * R;
	const float Out = (U & 1u) == 0u ? OddPoly(R2, R) : EvenPoly(R2);
	return (U & 2u) != 0u ? -Out : Out;
}

float AsinEE(float X)
{
	auto Poly = [](float U) { return U * FpuAdd(AsinC0, U * FpuAdd(AsinC1, U * FpuAdd(AsinC2, U * AsinC3))); };
	if (X < -0.70710677f)
	{
		const float U = FpuSub(1.0f, X * X);
		return FpuSub(Sqrt32(U) * FpuAdd(Poly(U), 1.0f), 1.5707964f);
	}
	const float U = X * X;
	if (X <= 0.70710677f)
	{
		return FpuAdd(Poly(U) * X, X);
	}
	const float V = FpuSub(1.0f, U);
	return FpuSub(1.5707964f, Sqrt32(V) * FpuAdd(Poly(V), 1.0f));
}

float AtanEE(float X)
{
	auto Series = [](float F) { return Sqrt32(F) * FpuAdd(F * FpuAdd(AsinC0, F * FpuAdd(AsinC1, F * FpuAdd(AsinC2, F * AsinC3))), 1.0f); };
	if (X < -1.0f)
	{
		return FpuAdd(Series(1.0f / FpuAdd(X * X, 1.0f)), -1.5707964f);
	}
	const float U = X * X;
	if (1.0f < X)
	{
		return FpuSub(1.5707964f, Series(1.0f / FpuAdd(U, 1.0f)));
	}
	const float R = Series(U / FpuAdd(U, 1.0f));
	return X < 0.0f ? -R : R;
}

float Atan2EE(float Y, float X)
{
	if (X == 0.0f)
	{
		return Y == 0.0f ? Y : (Y < 0.0f ? -1.5707964f : 1.5707964f);
	}
	const float A = AtanEE(Y / X);
	if (X < 0.0f)
	{
		return 0.0f < Y ? FpuAdd(A, 3.1415927f) : FpuSub(A, 3.1415927f);
	}
	return A;
}

FRowMat LocalRows(const FVector3d& E, const FVector3d& T)
{
	float Sx, Cx, Sy, Cy, Sz, Cz;
	SinCosEE(float(E.X), Sx, Cx);
	SinCosEE(float(E.Y), Sy, Cy);
	SinCosEE(float(E.Z), Sz, Cz);
	FRowMat M;
	M.M[0][0] = Cy * Cz;                M.M[0][1] = -Cy * Sz;               M.M[0][2] = Sy;       M.M[0][3] = 0.0f;
	M.M[1][0] = Sx * Sy * Cz + Cx * Sz; M.M[1][1] = Cx * Cz - Sx * Sy * Sz; M.M[1][2] = -Sx * Cy; M.M[1][3] = 0.0f;
	M.M[2][0] = Sx * Sz - Cx * Sy * Cz; M.M[2][1] = Cx * Sy * Sz + Sx * Cz; M.M[2][2] = Cx * Cy;  M.M[2][3] = 0.0f;
	M.M[3][0] = float(T.X);             M.M[3][1] = float(T.Y);             M.M[3][2] = float(T.Z); M.M[3][3] = 1.0f;
	return M;
}

FRowMat RootFrame(const FRiderFrameInput& In, double HipsZ, bool bIkBranch)
{
	const float W = float(In.Orientation.W), X = float(In.Orientation.X), Y = float(In.Orientation.Y), Z = float(In.Orientation.Z);
	FRowMat M = FRowMat::Identity();
	// 0x129628
	{
		const float X2 = FpuAdd(X, X), Y2 = FpuAdd(Y, Y), Z2 = FpuAdd(Z, Z);
		const float XX = X * X2, YY = Y * Y2, ZZ = Z * Z2, XY = X * Y2, XZ = X * Z2, YZ = Y * Z2, WX = W * X2, WY = W * Y2, WZ = W * Z2;
		M.M[0][0] = FpuSub(FpuSub(1.0f, YY), ZZ); M.M[0][1] = FpuAdd(XY, WZ); M.M[0][2] = FpuSub(XZ, WY);
		M.M[1][0] = FpuSub(XY, WZ); M.M[1][1] = FpuSub(FpuSub(1.0f, XX), ZZ); M.M[1][2] = FpuAdd(YZ, WX);
		M.M[2][0] = FpuAdd(XZ, WY); M.M[2][1] = FpuSub(YZ, WX); M.M[2][2] = FpuSub(FpuSub(1.0f, XX), YY);
	}
	if (In.Steer != 0.0 && In.Motion != 3)
	{
		float S, C;
		SinCosEE(float(In.Steer) * (In.BoardClass == 2 ? 1.134464f : 0.87266463f), S, C);
		const float Yy = FpuAdd(FpuSub(1.0f, C), C);
		const float R0[4] = { C, 0, S, 0 }, R1[4] = { 0, Yy, 0, 0 }, R2[4] = { -S, 0, C, 0 }, R3[4] = { 0, 0, 0, 1 };
		M = RowsFromRows(R0, R1, R2, R3) * M;
	}
	const FVector3f Pos(In.Position), Up(In.Up), Left(In.Left);
	const float K = float(In.Scale) * float(In.Lean);
	M.SetRow(3, Pos + Up * float(In.S308) + Left * K, 1.0f);
	if (In.Spin != 0.0 || In.Flip != 0.0)
	{
		const float Hz = float(HipsZ);
		float V[4] = { 0, 0, Hz, 1 }, Out[4];
		RowTimes(V, M, Out);
		for (int32 J = 0; J < 4; ++J) { M.M[3][J] = Out[J]; }
		float S, C;
		SinCosEE(float(In.Spin), S, C);
		{
			const float Zz = FpuAdd(FpuSub(1.0f, C), C);
			const float R0[4] = { C, -S, 0, 0 }, R1[4] = { S, C, 0, 0 }, R2[4] = { 0, 0, Zz, 0 }, R3[4] = { 0, 0, 0, 1 };
			M = RowsFromRows(R0, R1, R2, R3) * M;
		}
		SinCosEE(float(In.Flip), S, C);
		{
			const float Xx = FpuAdd(FpuSub(1.0f, C), C);
			const float R0[4] = { Xx, 0, 0, 0 }, R1[4] = { 0, C, -S, 0 }, R2[4] = { 0, S, C, 0 }, R3[4] = { 0, 0, 0, 1 };
			M = RowsFromRows(R0, R1, R2, R3) * M;
		}
		float V2[4] = { 0, 0, -Hz, 1 };
		RowTimes(V2, M, Out);
		for (int32 J = 0; J < 4; ++J) { M.M[3][J] = Out[J]; }
	}
	if (bIkBranch && (In.Motion == 2 || In.Motion == 4))
	{
		const FVector3f D = Up * float(In.Suspension) * float(In.Scale);
		float V[4] = { D.X, D.Y, D.Z, 1 }, Out[4];
		RowTimes(V, M, Out);
		for (int32 J = 0; J < 4; ++J) { M.M[3][J] = Out[J]; }
	}
	return M;
}

FRowMat BoardFrame(const FRowMat& RootNoSusp, const FBonePose& BoardPose, const FRiderFrameInput& In)
{
	FRowMat B = LocalRows(BoardPose.Euler, BoardPose.Trans) * RootNoSusp;
	if (In.Motion == 2)
	{
		const FVector3f N(In.Up);
		const FVector3f Bx = B.Row(0);
		FVector3f Ax = Cross(N, Bx);
		float S = Sqrt32(Dot(Ax, Ax));
		if (S > 1e-8f)
		{
			Ax = Ax * (1.0f / S);
			S = FMath::Min(S, 0.99999f);
			const float A = 1.5707964f - AsinEE(S);
			float Sn, Cs;
			SinCosEE(Dot(N, Bx) > 0.0f ? -A : A, Sn, Cs);
			float R[3][3];
			AxisRows(Ax, Sn, Cs, R);
			RotateUpper(B, R);
		}
	}
	if (In.Motion == 2 && In.BoardChatter > 0.0)
	{
		// 0x12AE18
		const FVector3f O = FVector3f(In.GroundNormal) * float(In.BoardChatter);
		float V[4] = { O.X, O.Y, O.Z, 1 }, Out[4];
		RowTimes(V, B, Out);
		for (int32 J = 0; J < 4; ++J) { B.M[3][J] = Out[J]; }
	}
	return B;
}

bool FootRotations(const FAflLibrary& Lib, const FAflSkeleton& Body, int32 BoardClass, bool bMirror,
	double OutLeft[3][3], double OutRight[3][3])
{
	const FAflClip* Clip = Lib.Clip(Lib.Riding(BoardClass, 4));
	const FAflSkeleton* Board = Lib.BoardSkeleton();
	if (!Clip || !Board || !Clip->Block(0) || !Clip->Block(1) || Body.Bones.Num() <= RFoot)
	{
		return false;
	}
	const double Yaw = bMirror ? -UE_DOUBLE_PI : 0.0;
	TArray<FBonePose> E, Eb;
	SampleBlock(Lib, *Clip->Block(0), Body, 0.5, bMirror, Yaw, 1.0, 1.0, E);
	SampleBlock(Lib, *Clip->Block(1), *Board, 0.5, bMirror, Yaw, 1.0, 1.0, Eb);
	TArray<FD44> Rows;
	Rows.SetNum(Body.Bones.Num());
	for (int32 I = 0; I < Body.Bones.Num(); ++I)
	{
		const FD44 L = ToD(LocalRows(E[I].Euler, E[I].Trans));
		const int32 P = Body.Bones[I].Parent;
		Rows[I] = P < 0 ? L : MulD(L, Rows[P]);
	}
	const FD44 B = ToD(LocalRows(Eb[0].Euler, Eb[0].Trans));
	double B3[3][3], Binv[3][3];
	for (int32 I = 0; I < 3; ++I) { for (int32 J = 0; J < 3; ++J) { B3[I][J] = B.M[I][J]; } }
	if (!Inverse3(B3, Binv))
	{
		return false;
	}
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = 0; J < 3; ++J)
		{
			double L = 0.0, R = 0.0;
			for (int32 K = 0; K < 3; ++K)
			{
				L += Rows[LFoot].M[I][K] * Binv[K][J];
				R += Rows[RFoot].M[I][K] * Binv[K][J];
			}
			OutLeft[I][J] = L;
			OutRight[I][J] = R;
		}
	}
	return true;
}

void LegIK(const FRowMat& ParentRows, const FVector3d& ThighEuler, const FVector3d BindT[3],
	const FVector3f& Target, FRowMat& OutThigh, FRowMat& OutCalf, FVector3f& OutFoot)
{
	FRowMat T = LocalRows(ThighEuler, BindT[0]) * ParentRows;
	const FVector3f Hip = T.Row(3);
	const FVector3f D = Target - Hip;
	const float Dist = Sqrt32(Dot(D, D));
	const float Inv = 1.0f / Dist;
	const FVector3f Dir = D * Inv;
	const float Lt = float(BindT[1].X);
	const float Lc = float(BindT[2].X);
	const float B = Lc * Inv;
	const float A = Lt * Inv;
	float Ang = 0.0f;
	if (A + B > 1.0f)
	{
		const float S11 = A * A + B * B;
		const float S13 = A * A - B * B;
		float H = ((S11 + S11) - S13 * S13) - 1.0f;
		if (H >= 0.0f)
		{
			H = Sqrt32(H) * 0.5f;
			const float X1 = FMath::Clamp(H / A, -0.99999f, 0.99999f);
			const float X2 = FMath::Clamp(H / B, -0.99999f, 0.99999f);
			Ang = AsinEE(X1) + AsinEE(X2);
		}
	}
	if (Ang < 0.2f)
	{
		Ang = 0.2f;
	}
	float S, C;
	SinCosEE(Ang, S, C);
	const FVector3f P = T.TransformPoint(FVector3f(Lt + C * Lc, S * Lc, 0.0f));
	FVector3f V = P - Hip;
	V = V * float(1.0 / FMath::Sqrt(double(Dot(V, V))));
	FVector3f Ax = Cross(Dir, V);
	const float Sn = Sqrt32(Dot(Ax, Ax));
	if (Sn > 0.001f)
	{
		Ax = Ax * (1.0f / Sn);
		const float A2 = AsinEE(FMath::Min(Sn, 0.99999f));
		float Si, Co;
		SinCosEE(A2, Si, Co);
		float R[3][3];
		AxisRows(Ax, Si, Co, R);
		RotateUpper(T, R);
	}
	const FRowMat Calf = LocalRows(FVector3d(0.0, 0.0, -double(Ang)), BindT[1]) * T;
	OutThigh = T;
	OutCalf = Calf;
	OutFoot = Calf.TransformPoint(FVector3f(BindT[2]));
}

bool FRiderSkeletonSet::Init(const FAflLibrary& Lib, const FAflSkeleton* InBody, int32 InBoardClass)
{
	Body = InBody;
	Board = Lib.BoardSkeleton();
	BoardClass = FMath::Clamp(InBoardClass, 0, 2);
	if (!Body || !Board || Body->Bones.Num() < 19 || Body->Bones[RFoot].Name != TEXT("r_foot") || Body->Bones[LThigh].Name != TEXT("l_thigh"))
	{
		return false;
	}
	for (int32 M = 0; M < 2; ++M)
	{
		bFeet[M] = FootRotations(Lib, *Body, BoardClass, M == 1, FootLeft[M], FootRight[M]);
	}
	return bFeet[0] && bFeet[1];
}

void BuildPalette(const FAflLibrary& Lib, const FRiderSkeletonSet& Set, const FRowMat& Root,
	TConstArrayView<FBonePose> Pose, const FRowMat& BoardWorld, bool bMirror, double Scale, TArray<FRowMat>& Out)
{
	const FAflSkeleton& Body = *Set.Body;
	const int32 N = Body.Bones.Num();
	Out.SetNum(N);
	Out[0] = LocalRows(Pose[0].Euler, Pose[0].Trans) * Root;
	for (int32 I = 1; I < NumFkBones; ++I)
	{
		Out[I] = LocalRows(Pose[I].Euler, Body.Bones[I].Translation * Scale) * Out[Body.Bones[I].Parent];
	}
	const FAflBoardModel* Model = Lib.BoardModel(Set.BoardClass, bMirror);
	const int32 Mi = bMirror ? 1 : 0;
	struct FLeg { int32 Thigh, Calf, Foot, Ik; const double (*Rot)[3]; };
	const FLeg Legs[2] = { { RThigh, RCalf, RFoot, 1, Set.FootRight[Mi] }, { LThigh, LCalf, LFoot, 0, Set.FootLeft[Mi] } };
	for (const FLeg& L : Legs)
	{
		const FVector3d Ik = Model ? Model->IK[L.Ik] * Scale : FVector3d::ZeroVector;
		double Tg[3];
		for (int32 J = 0; J < 3; ++J)
		{
			Tg[J] = Ik.X * BoardWorld.M[0][J] + Ik.Y * BoardWorld.M[1][J] + Ik.Z * BoardWorld.M[2][J] + 1.0 * BoardWorld.M[3][J];
		}
		const FVector3d BindT[3] = { Body.Bones[L.Thigh].Translation * Scale, Body.Bones[L.Calf].Translation * Scale,
			Body.Bones[L.Foot].Translation * Scale };
		FVector3f Foot;
		LegIK(Out[Body.Bones[L.Thigh].Parent], Pose[L.Thigh].Euler, BindT, FVector3f(float(Tg[0]), float(Tg[1]), float(Tg[2])),
			Out[L.Thigh], Out[L.Calf], Foot);
		FRowMat F = FRowMat::Identity();
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				const double V = L.Rot[I][0] * BoardWorld.M[0][J] + L.Rot[I][1] * BoardWorld.M[1][J] + L.Rot[I][2] * BoardWorld.M[2][J];
				F.M[I][J] = float(V);
			}
		}
		F.SetRow(3, Foot, 1.0f);
		Out[L.Foot] = F;
	}
}

void BuildPaletteFK(const FAflSkeleton& Body, const FRowMat& Root, TConstArrayView<FBonePose> Pose, double Scale, TArray<FRowMat>& Out)
{
	const int32 N = Body.Bones.Num();
	Out.SetNum(N);
	for (int32 I = 0; I < N; ++I)
	{
		const int32 P = Body.Bones[I].Parent;
		const FVector3d T = (Body.Bones[I].Flags & 1) ? Pose[I].Trans : Body.Bones[I].Translation * Scale;
		Out[I] = P < 0 ? LocalRows(Pose[I].Euler, T) * Root : LocalRows(Pose[I].Euler, T) * Out[P];
	}
}
}
