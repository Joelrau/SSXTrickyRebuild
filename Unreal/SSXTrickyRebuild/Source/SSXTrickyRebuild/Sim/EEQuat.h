// 0x102E80, 0x250C10, 0x129008, 0x11DB00, 0x2508B8, 0x250A48
#pragma once

#include "CoreMinimal.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderPose.h"

namespace SSXEE
{
	struct FQf { float W, X, Y, Z; };

	FORCEINLINE FQf QMul(const FQf& A, const FQf& B)
	{
		return { FpuSub(FpuSub(FpuSub(A.W * B.W, A.X * B.X), A.Y * B.Y), A.Z * B.Z),
			FpuSub(FpuAdd(FpuAdd(A.W * B.X, A.X * B.W), A.Y * B.Z), A.Z * B.Y),
			FpuSub(FpuAdd(FpuAdd(A.W * B.Y, A.Y * B.W), A.Z * B.X), A.X * B.Z),
			FpuSub(FpuAdd(FpuAdd(A.W * B.Z, A.Z * B.W), A.X * B.Y), A.Y * B.X) };
	}

	FORCEINLINE FQf QAxis(const FVector3f& Ax, float Angle)
	{
		float Sn, Cs;
		SSXAnim::SinCosEE(Angle * 0.5f, Sn, Cs);
		return { Cs, Sn * Ax.X, Sn * Ax.Y, Sn * Ax.Z };
	}

	// 0x10B0D0
	FORCEINLINE FVector3f QRot(const FQf& Q, const FVector3f& E)
	{
		const float Cx = FpuSub(Q.Y * E.Z, Q.Z * E.Y), Cy = FpuSub(Q.Z * E.X, Q.X * E.Z), Cz = FpuSub(Q.X * E.Y, Q.Y * E.X);
		const float Dx = FpuSub(Q.Y * Cz, Q.Z * Cy), Dy = FpuSub(Q.Z * Cx, Q.X * Cz), Dz = FpuSub(Q.X * Cy, Q.Y * Cx);
		const float W2 = FpuAdd(Q.W, Q.W);
		return FVector3f(FpuAdd(FpuAdd(E.X, W2 * Cx), 2.0f * Dx), FpuAdd(FpuAdd(E.Y, W2 * Cy), 2.0f * Dy), FpuAdd(FpuAdd(E.Z, W2 * Cz), 2.0f * Dz));
	}

	// 0x250FD0
	FORCEINLINE void QFromTo(FQf& Q, const FVector3f& A, const FVector3f& B)
	{
		FVector3f C = Cross(A, B);
		float S = Length(C);
		if (S == 0.0f)
		{
			return;
		}
		C = Scale(1.0f / S, C);
		S = 0.99999f < S ? 0.99999f : S;
		const float Ang = Dot(A, B) < 0.0f ? FpuSub(3.1415927f, SSXAnim::AsinEE(S)) : SSXAnim::AsinEE(S);
		Q = QMul(QAxis(C, Ang), Q);
	}

	// 0x250D60
	FORCEINLINE FQf QSlerp(const FQf& A, const FQf& InB, float T)
	{
		FQf B = InB;
		float Dt = FpuAdd(FpuAdd(FpuAdd(A.X * B.X, A.Y * B.Y), A.Z * B.Z), A.W * B.W);
		if (Dt < 0.0f)
		{
			B = { -B.W, -B.X, -B.Y, -B.Z };
			Dt = -Dt;
		}
		float Ka, Kb = T;
		constexpr float Lerp = 0.001f;
		if (Lerp < FpuSub(1.0f, Dt))
		{
			const float Ang = FpuSub(1.5707964f, SSXAnim::AsinEE(Dt));
			const float Sa = SSXAnim::SinEE(Ang);
			Ka = SSXAnim::SinEE(FpuSub(1.0f, T) * Ang) / Sa;
			Kb = SSXAnim::SinEE(T * Ang) / Sa;
		}
		else
		{
			Ka = FpuSub(1.0f, T);
		}
		return { FpuAdd(Ka * A.W, Kb * B.W), FpuAdd(Ka * A.X, Kb * B.X), FpuAdd(Ka * A.Y, Kb * B.Y), FpuAdd(Ka * A.Z, Kb * B.Z) };
	}
}
