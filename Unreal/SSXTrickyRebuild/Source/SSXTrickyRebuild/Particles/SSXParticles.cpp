#include "Particles/SSXParticles.h"

namespace SSXParticles
{
	namespace
	{
		constexpr uint32 OneBits = 127u << 23;
		constexpr uint32 Mantissa = (1u << 23) - 1;

		float WordFloat(uint32 W) { float F; FMemory::Memcpy(&F, &W, 4); return F; }

		FVector4 ArgbTo128(const FVector4& K) { return FVector4(K.Y * 128.0, K.Z * 128.0, K.W * 128.0, K.X * 128.0); }
	}

	int32 EmitterBlendMode(int32 Word)
	{
		static const int32 Table[8] = { 5, 3, 2, 1, 4, 0, 6, 7 };
		const int32 K = FMath::Clamp(Word, 0, 10);
		return K < 8 ? Table[K] : -1;   // TODO: 8..10 read past the table (no retail data)
	}

	uint32 VuRInit(float Seed)
	{
		uint32 Bits; FMemory::Memcpy(&Bits, &Seed, 4);
		return OneBits | (Bits & Mantissa);
	}

	uint32 VuRNext(uint32 R)
	{
		const uint32 X = (R >> 4) & 1u, Y = (R >> 22) & 1u;
		return (((R << 1) ^ X ^ Y) & Mantissa) | OneBits;
	}

	float AdvanceSeed(float Seed) { return WordFloat(VuRNext(VuRInit(Seed))); }

	FEmitterConfig FEmitterConfig::FromWords(TConstArrayView<uint32> W)
	{
		check(W.Num() >= 51);
		auto F = [&](int32 K) { return double(WordFloat(W[K])); };
		auto V3 = [&](int32 K) { return FVector(F(K), F(K + 1), F(K + 2)); };
		auto V4 = [&](int32 K) { return FVector4(F(K), F(K + 1), F(K + 2), F(K + 3)); };
		FEmitterConfig C;
		C.Count = int32(W[0]);
		C.Copies = int32(W[1]);
		C.EmitTime = F(2);
		C.TimeScale = F(3);
		C.Size = F(4);
		C.Life = F(5);
		C.SizeRange = F(6);
		C.LifeRange = F(7);
		C.CopySpacing = F(8);
		C.Origin = V3(9);
		C.PosA = V3(12);
		C.PosB = V3(15);
		C.Vel = V3(18);
		C.VelA = V3(21);
		C.VelB = V3(24);
		C.VelC = V3(27);
		C.Drift = V3(30);
		C.Key0 = V4(33);
		C.Key1 = V4(37);
		C.Jit0 = V4(41);
		C.Jit1 = V4(45);
		C.Texture = int32(W[49]);
		C.Blend = int32(W[50]);
		return C;
	}

	FInstanceMatrix FInstanceMatrix::FromMatrix(const FMatrix& M)
	{
		FInstanceMatrix R;
		R.X = FVector(M.M[0][0], M.M[0][1], M.M[0][2]);
		R.Y = FVector(M.M[1][0], M.M[1][1], M.M[1][2]);
		R.Z = FVector(M.M[2][0], M.M[2][1], M.M[2][2]);
		R.T = FVector(M.M[3][0], M.M[3][1], M.M[3][2]);
		return R;
	}

	FParticleBurst FParticleBurst::FromEmitter(const FEmitterConfig& C, const FInstanceMatrix& M, TFunctionRef<double()> Rng,
		const FVector* OriginOverride, const FVector* VelocityOverride)
	{
		FParticleBurst B;
		const double Ts = C.TimeScale;
		const bool bCont = C.IsContinuous();
		const double SpanT = bCont ? C.MaxLife() : C.EmitTime;
		B.N = FMath::Max(C.Count, 1);
		B.Copies = C.Copies;
		B.T = bCont ? C.MaxLife() * Ts : 0.0;
		B.Interval = SpanT * Ts / double(B.N);
		B.Ts = Ts;
		B.SizeLo = C.Size - C.SizeRange * 0.5;
		B.SizeSpan = C.SizeRange;
		B.LifeLo = (C.Life - C.LifeRange * 0.5) * Ts;
		B.LifeSpan = C.LifeRange * Ts;
		B.CopyDt = C.CopySpacing * Ts;
		for (float& S : B.Seeds) { S = float(Rng() + 1.0); }
		B.Drift = C.Drift / (Ts * Ts);
		B.P0 = M.Point(OriginOverride ? *OriginOverride : C.Origin);
		B.PA = M.Rotate(C.PosA);
		B.PB = M.Rotate(C.PosB);
		B.PosScale = 0.5;
		B.V0 = M.Rotate(VelocityOverride ? *VelocityOverride : C.Vel) / Ts;
		B.VA = M.Rotate(C.VelA) / Ts;
		B.VB = M.Rotate(C.VelB) / Ts;
		B.VC = M.Rotate(C.VelC) / Ts;
		B.C0 = ArgbTo128(C.Key0);
		B.CSlope = (ArgbTo128(C.Key1) - ArgbTo128(C.Key0)) * (1.0 / (C.MaxLife() * Ts));
		B.CJ0 = ArgbTo128(C.Jit0);
		B.CJ1 = ArgbTo128(C.Jit1);
		B.bContinuous = bCont;
		B.Texture = C.Texture;
		const int32 Mode = EmitterBlendMode(C.Blend);
		B.BlendMode = Mode >= 0 ? Mode : BlendAdditive;
		return B;
	}

	void FParticleBurst::Update(double Dt)
	{
		T += Dt * Ts;
		if (bContinuous && Interval > 0.0)
		{
			// 0x1D5548
			while (T > N * Interval)
			{
				T -= Interval;
				for (float& S : Seeds) { S = AdvanceSeed(S); }
			}
		}
	}

	bool FParticleBurst::IsFinished() const
	{
		if (bContinuous)
		{
			return false;
		}
		const double LastBirth = (N - 1) * Interval;
		const double MaxAge = LifeLo + LifeSpan + FMath::Max(1, Copies) * CopyDt;
		return T - LastBirth > MaxAge;
	}

	int32 FParticleBurst::Evaluate(TArray<FSprite>& Out) const
	{
		const int32 Start = Out.Num();
		float S[9];
		FMemory::Memcpy(S, Seeds, sizeof(S));
		const int32 NC = FMath::Max(1, Copies);
		for (int32 K = 0; K < N; ++K)
		{
			double R[9];
			for (int32 I = 0; I < 9; ++I) { S[I] = AdvanceSeed(S[I]); R[I] = double(S[I]) - 1.5; }
			const double Age = T - K * Interval;
			if (Age < 0.0)
			{
				break;
			}
			const FVector P = P0 + PA * (R[0] * 2.0 * PosScale) + PB * (R[1] * 2.0 * PosScale);
			const FVector V = V0 + VA * R[2] + VB * R[3] + VC * R[4];
			const FVector4 Col = C0 + CJ0 * R[5] + CJ1 * R[6] + CSlope * Age;
			const double Size = SizeLo + SizeSpan * double(S[7]) - SizeSpan;
			const double Life = LifeLo + LifeSpan * double(S[8]) - LifeSpan;
			for (int32 J = 0; J < NC; ++J)
			{
				const double Aj = Age - J * CopyDt;
				if (!(Aj >= 0.0 && Aj < Life))
				{
					continue;
				}
				const double Q = Drag(Aj);
				FSprite& Sp = Out.AddDefaulted_GetRef();
				Sp.Pos = P + V * Q + Drift * (Aj - Q);
				const double M = 1.0 - double(J) / double(NC);
				Sp.Rgba = FVector4(FMath::Clamp(Col.X, 0.0, 255.0), FMath::Clamp(Col.Y, 0.0, 255.0),
					FMath::Clamp(Col.Z, 0.0, 255.0), FMath::Clamp(Col.W * M, 0.0, 255.0));
				Sp.HalfSize = Size;
				Sp.Particle = K;
				Sp.Copy = J;
			}
		}
		return Out.Num() - Start;
	}
}
