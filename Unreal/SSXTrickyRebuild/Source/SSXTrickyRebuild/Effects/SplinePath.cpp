#include "Effects/SplinePath.h"
#include "Sim/Anim/RiderPose.h"

namespace SSXEffects
{
namespace SplinePath
{
	namespace
	{
		constexpr float HalfPi = 1.5707964f;
		constexpr float Pi = 3.1415927f;

		/** 0x251508 (forward, Next) / 0x251568 (backward, Prev): walk from the cursor until a segment holds D; a segment
		 *  without a further link ends the walk whether or not it holds D. */
		int32 Walk(FSplinePathState& S, TConstArrayView<FSplinePathSegment> Segs, float D, bool bForward)
		{
			int32 K = S.Cursor;
			auto Link = [&](int32 I) { return bForward ? Segs[I].Next : Segs[I].Prev; };
			while (Link(K) != INDEX_NONE)
			{
				const FSplinePathSegment& G = Segs[K];
				if (G.Start <= D && D < G.Start + G.Length)
				{
					return K;
				}
				K = Link(K);
				S.Cursor = K;
			}
			return K;
		}

		// 0x1FA8D8
		void Place(FSplinePathState& S, TConstArrayView<FSplinePathSegment> Segs, int32 Index, float D)
		{
			FSplinePathPoint& P = S.Points[Index];
			FVector4f Tan;
			Eval(S, Segs, D, P.Pos, Tan);
			S.Tangent = Tan;
			const FSplinePathConfig& C = S.Config;
			float Yaw = C.Heading + HalfPi;
			if (C.EndMode == 2 && S.Speed < 0.f)
			{
				Yaw = Yaw + Pi;
			}
			if (C.Facing != 2 && C.Facing != 3)
			{
				// 0x2510F8
				const float X = Tan.X, Y = Tan.Y;
				float A;
				if (X == 0.f)
				{
					A = Y == 0.f ? 0.f : (0.f <= Y ? HalfPi : -HalfPi);
				}
				else
				{
					A = SSXAnim::AtanEE(Y / X);   // 0x2510F8
					if (X < 0.f)
					{
						A = 0.f < Y ? A + Pi : A - Pi;
					}
				}
				Yaw = Yaw - A;
			}
			float Pitch = 0.f;
			if (C.Facing != 1 && C.Facing != 3)
			{
				Pitch = S.Speed < 0.f ? 0.f + Tan.Z : 0.f - Tan.Z;   // the tangent's z itself, not an angle (as the game)
			}
			P.Angles = FVector3f(Pitch, 0.f, Yaw);
		}

		// 0x250C10
		FMatrix44f AxisRows(const FVector3f& Ax, float Sn, float Cs)
		{
			const float T = 1.f - Cs;
			const float Tz = T * Ax.Z, Tx = T * Ax.X, Ty = T * Ax.Y;
			FMatrix44f M = FMatrix44f::Identity;
			M.M[0][0] = Tx * Ax.X + Cs;       M.M[0][1] = Ty * Ax.X - Sn * Ax.Z; M.M[0][2] = Tz * Ax.X + Sn * Ax.Y;
			M.M[1][0] = Tx * Ax.Y + Sn * Ax.Z; M.M[1][1] = Ty * Ax.Y + Cs;       M.M[1][2] = Tz * Ax.Y - Sn * Ax.X;
			M.M[2][0] = Tx * Ax.Z - Sn * Ax.Y; M.M[2][1] = Ty * Ax.Z + Sn * Ax.X; M.M[2][2] = Tz * Ax.Z + Cs;
			return M;
		}
	}

	FSplinePathConfig ConfigFromWords(TFunctionRef<int32(int32)> I, TFunctionRef<float(int32)> F)
	{
		FSplinePathConfig C;
		C.Spline = I(1);
		C.EndMode = I(2);
		C.Facing = I(3);
		C.Count = I(4);
		C.Speed = F(5);
		C.Heading = F(6);
		C.bDrawSpline = I(7) != 0;
		for (int32 K = 0; K < 4; ++K)
		{
			C.Colour[K] = F(8 + K);
		}
		return C;
	}

	void Init(FSplinePathState& S, const FSplinePathConfig& C, TConstArrayView<FSplinePathSegment> Segs, int32 First, int32 Count)
	{
		S = FSplinePathState();
		S.Config = C;
		S.Points.SetNum(FMath::Max(C.Count, 0));
		S.Speed = (C.Speed * 100.f) / 60.f;
		S.bValid = Segs.IsValidIndex(First) && Count > 0;
		if (!S.bValid)
		{
			return;
		}
		// 0x2514C0 / 0x251968
		S.FirstSegment = First;
		S.SegmentCount = Count;
		S.Cursor = First;
		float Len = 0.f;
		for (int32 K = First, N = Count; N > 0 && Segs.IsValidIndex(K); --N)
		{
			Len = Len + Segs[K].Length;
			K = Segs[K].Next;
		}
		S.Length = Len;
		S.Spacing = C.Count != 0 ? S.Length / float(C.Count) : 0.f;
	}

	void Eval(FSplinePathState& S, TConstArrayView<FSplinePathSegment> Segs, float D, FVector4f& OutPos, FVector4f& OutTangent)
	{
		if (S.Length > 0.f)
		{
			while (S.Length <= D)
			{
				D = D - S.Length;
			}
		}
		const FSplinePathSegment& Cur = Segs[S.Cursor];
		if (D < Cur.Start)
		{
			S.Cursor = Walk(S, Segs, D, false);
		}
		else if (!(D <= Cur.Start + Cur.Length))
		{
			S.Cursor = Walk(S, Segs, D, true);
		}
		const FSplinePathSegment& G = Segs[S.Cursor];
		const float U = (D - G.Start) * 0.01f;
		const float T = ((G.Poly[0] * U + G.Poly[1]) * U + G.Poly[2]) * U + G.Poly[3];
		const float T2 = T * T;
		const FVector3f P = G.Coef[0] * (T2 * T) + G.Coef[1] * T2 + G.Coef[2] * T + G.Coef[3];
		FVector3f Tn = G.Coef[0] * (T2 * 3.f) + G.Coef[1] * (T + T) + G.Coef[2];
		Tn = Tn * (1.f / FMath::Sqrt(Tn.X * Tn.X + Tn.Y * Tn.Y + Tn.Z * Tn.Z));
		OutPos = FVector4f(P.X, P.Y, P.Z, 1.f);
		OutTangent = FVector4f(Tn.X, Tn.Y, Tn.Z, 0.f);
	}

	void Step(FSplinePathState& S, TConstArrayView<FSplinePathSegment> Segs, bool bReacts, const FVector3f* Leader)
	{
		if (S.bDone || !S.bValid)
		{
			return;
		}
		const FSplinePathConfig& C = S.Config;
		// 0x1FA838
		float D = S.Dist;
		for (int32 I = 0; I < S.Points.Num(); ++I)
		{
			Place(S, Segs, I, D);
			D = D + S.Spacing;
			if (S.Length <= D)
			{
				D = D - S.Length;
			}
		}
		S.bPlaced = true;
		const float Old = S.Speed;
		S.Dist = S.Dist + S.Speed;
		if (S.Dist < 0.f)
		{
			S.Dist = 0.f;
			if (C.EndMode == 2)
			{
				S.Speed = -Old;
			}
		}
		if (S.Length <= S.Dist)
		{
			switch (C.EndMode)
			{
			case 0: S.bDone = true; break;
			case 3: S.Speed = 0.f; break;
			case 2: S.Speed = -S.Speed; break;
			default: S.Dist = S.Dist - S.Length; break;
			}
		}
		else if (bReacts && C.EndMode == 3 && Leader && S.Points.Num() > 0)
		{
			// 0x1FAAD0
			const FVector3f T(S.Tangent.X, S.Tangent.Y, S.Tangent.Z);
			const FSplinePathPoint& P0 = S.Points[0];
			const float Pd = P0.Pos.X * T.X + P0.Pos.Y * T.Y + P0.Pos.Z * T.Z;
			const float Ld = (Leader->X * T.X + Leader->Y * T.Y + Leader->Z * T.Z) + 6000.f;
			S.Speed = (Pd - Ld) / -10.f;
		}
		// TODO: with one copy and flags & 0x80 the update also hands copy 0's position / angles to the controller's slot 25 (a
	}

	FMatrix44f PointMatrix(const FSplinePathPoint& P)
	{
		FMatrix44f M = FMatrix44f::Identity;
		const FVector3f Axes[3] = { FVector3f(0.f, 0.f, 1.f), FVector3f(0.f, 1.f, 0.f), FVector3f(1.f, 0.f, 0.f) };
		const float Ang[3] = { P.Angles.Z, P.Angles.Y, P.Angles.X };
		for (int32 K = 0; K < 3; ++K)
		{
			float Sn, Cs;
			SinCos(Ang[K], Sn, Cs);
			M = AxisRows(Axes[K], Sn, Cs) * M;
		}
		M.M[3][0] = P.Pos.X;
		M.M[3][1] = P.Pos.Y;
		M.M[3][2] = P.Pos.Z;
		M.M[3][3] = P.Pos.W;
		return M;
	}

	void SinCos(float X, float& OutSin, float& OutCos)
	{
		const float Q = X < 0.f ? X * 0.63661975f - 0.5f : X * 0.63661975f + 0.5f;
		const int32 N = int32(Q);
		const float R = X - float(N) * HalfPi;
		const float R2 = R * R;
		const float Sn = ((((R2 * 2.7557319e-06f + -0.0001984127f) * R2 + 0.008333334f) * R2 + -0.16666667f) * R2 + 1.f) * R;
		const float Cs = FMath::Sqrt(1.f - Sn * Sn);
		switch (N & 3)
		{
		case 0: OutSin = Sn; OutCos = Cs; break;
		case 1: OutSin = Cs; OutCos = -Sn; break;
		case 2: OutSin = -Sn; OutCos = -Cs; break;
		default: OutSin = -Cs; OutCos = Sn; break;
		}
	}

	void SegmentLinePoints(const FSplinePathSegment& G, FVector3f Out[10])
	{
		float T = 0.f;
		for (int32 K = 0; K < 10; ++K)
		{
			const float T2 = T * T;
			Out[K] = G.Coef[0] * (T2 * T) + G.Coef[1] * T2 + G.Coef[2] * T + G.Coef[3];
			T = T + 0.11111111f;
		}
	}

	FColor LineColour(const FSplinePathConfig& C)
	{
		auto B = [](float V) { return uint8(FMath::Clamp(int32(V * 128.f), 0, 255)); };
		return FColor(B(C.Colour[1]), B(C.Colour[2]), B(C.Colour[3]), B(C.Colour[0]));
	}
}
}
