#pragma once

#include "CoreMinimal.h"
#include <cmath>

namespace SSXEE
{
	FORCEINLINE FVector3f F(const FVector& V) { return FVector3f(float(V.X), float(V.Y), float(V.Z)); }
	FORCEINLINE FVector D(const FVector3f& V) { return FVector(V.X, V.Y, V.Z); }

	FORCEINLINE float Dot(const FVector3f& A, const FVector3f& B)
	{
		const float Mx = A.X * B.X, My = A.Y * B.Y, Mz = A.Z * B.Z;
		return (Mx + My) + Mz;
	}

	FORCEINLINE FVector3f Cross(const FVector3f& A, const FVector3f& B)
	{
		return FVector3f(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X);
	}

	// 0x102F50, 0x102F80
	FORCEINLINE FVector3f Scale(float S, const FVector3f& V) { return FVector3f(S * V.X, S * V.Y, S * V.Z); }
	FORCEINLINE FVector3f Add(const FVector3f& A, const FVector3f& B) { return FVector3f(A.X + B.X, A.Y + B.Y, A.Z + B.Z); }
	FORCEINLINE FVector3f Sub(const FVector3f& A, const FVector3f& B) { return FVector3f(A.X - B.X, A.Y - B.Y, A.Z - B.Z); }

	// 0x105F58, 0x1062B0
	FORCEINLINE float FpuAdd(float A, float B)
	{
		uint32 UA, UB;
		FMemory::Memcpy(&UA, &A, 4);
		FMemory::Memcpy(&UB, &B, 4);
		const int32 D = int32((UA >> 23) & 0xFF) - int32((UB >> 23) & 0xFF);
		if (D >= 25) { UB &= 0x80000000u; }
		else if (D > 1) { UB &= 0xFFFFFFFFu << (D - 1); }
		else if (D <= -25) { UA &= 0x80000000u; }
		else if (D < -1) { UA &= 0xFFFFFFFFu << (-D - 1); }
		float RA, RB;
		FMemory::Memcpy(&RA, &UA, 4);
		FMemory::Memcpy(&RB, &UB, 4);
		return RA + RB;
	}
	FORCEINLINE float FpuSub(float A, float B) { return FpuAdd(A, -B); }

	FORCEINLINE float ChopToFloat(double S, double Err)
	{
		float F = float(S);
		if (FMath::Abs(double(F)) > FMath::Abs(S) || (double(F) == S && Err != 0.0 && (Err < 0.0) != (S < 0.0)))
		{
			F = std::nextafter(F, 0.0f);
		}
		return F;
	}
	FORCEINLINE float MicroMul(float A, float B) { return ChopToFloat(double(A) * double(B), 0.0); }
	FORCEINLINE float MicroAdd(float A, float B)
	{
		const double S = double(A) + double(B);
		const double Bb = S - double(A);
		return ChopToFloat(S, (double(A) - (S - Bb)) + (double(B) - Bb));
	}

	FORCEINLINE float MicroRecip(float X)
	{
		float Q = ChopToFloat(1.0 / double(X), 0.0);
		if (FMath::Abs(double(Q) * double(X)) > 1.0)
		{
			Q = std::nextafter(Q, 0.0f);
		}
		return Q;
	}

	// 0x23B6B0, 0x31CAE0
	FORCEINLINE bool MicroInside(const FVector3f& V0, const FVector3f& V1, const FVector3f& V2, const FVector3f& P)
	{
		auto Sb = [](float A, float B) { return MicroAdd(A, -B); };
		auto Sub3 = [&Sb](const FVector3f& A, const FVector3f& B) { return FVector3f(Sb(A.X, B.X), Sb(A.Y, B.Y), Sb(A.Z, B.Z)); };
		// OPMULA ACC, a, b; OPMSUB d, b, a: d = a x b with the second product rounded before the subtract
		auto Cross3 = [&Sb](const FVector3f& A, const FVector3f& B)
		{
			return FVector3f(Sb(MicroMul(A.Y, B.Z), MicroMul(B.Y, A.Z)), Sb(MicroMul(A.Z, B.X), MicroMul(B.Z, A.X)), Sb(MicroMul(A.X, B.Y), MicroMul(B.X, A.Y)));
		};
		auto Dot3 = [](const FVector3f& A, const FVector3f& B) { return MicroAdd(MicroAdd(MicroMul(A.X, B.X), MicroMul(A.Y, B.Y)), MicroMul(A.Z, B.Z)); };
		const FVector3f E1 = Sub3(V1, V0), E2 = Sub3(V2, V0), W = Sub3(P, V0);
		const FVector3f N = Cross3(E1, E2), S = Cross3(W, E1), M = Cross3(N, E2);
		const float Den = Dot3(E1, M);
		constexpr float Eps = 1.00000001e-10f;
		if (!(Den > Eps || Den < -Eps))
		{
			return false;
		}
		const float Q = MicroRecip(Den);
		const float U = MicroMul(Dot3(W, M), Q), V = MicroMul(Dot3(N, S), Q);
		const float U1 = MicroAdd(U, -1.0f);
		const float UV = MicroAdd(U1, V);
		return !std::signbit(U) && !std::signbit(V) && std::signbit(U1) && std::signbit(UV);
	}

	FORCEINLINE float Sqrt(float X) { return float(FMath::Sqrt(double(FMath::Max(X, 0.0f)))); }

	FORCEINLINE float Length(const FVector3f& V) { return Sqrt(Dot(V, V)); }

	FORCEINLINE float Rsqrt(float X) { return 1.0f / float(FMath::Sqrt(double(X))); }

	FORCEINLINE FVector3f Normalize(const FVector3f& V) { return Scale(Rsqrt(Dot(V, V)), V); }
}
