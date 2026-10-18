// TODO: the VU random step is PCSX2's RNEXT LFSR (bits 4 and 22);
#pragma once

#include "CoreMinimal.h"

// Plain C++ in GAME space (Z up, cm, s; SimCoords.h maps to Unreal).
namespace SSXParticles
{
	constexpr double Frame = 1.0 / 60.0;
	constexpr double DragA = 0.73, DragB = 0.113, DragClamp = 2.7;
	constexpr int32 NumTextures = 38;

	/** Race texture handle -> PARTICLE.SSH image index. Every game-side texture number is a HANDLE: the loader 0x1CAEF8 (called by 0x17B8A8) looks each image up by name in the 38-entry name table at .data 0x33EDF0 (12-byte records) and the renderer numbers the records in that order. The name order is not the .ssh order: handle 18 = 'str1' (.ssh image 18 is 'strk'), 0 = 'part', 19 = 'str2', 20 = 'str3', 23 = 'tral', 28 = 'lens', 29 = 'blnk'. Handles 30 and 31 both name 'mip1' (record +8 = 2 / 1). */
	inline constexpr int32 HandleImage[NumTextures] = {
		16 /*part*/, 19 /*snfl*/, 20 /*clod*/, 27 /*spry*/, 21 /*halo*/, 0 /*brk1*/, 1 /*brk2*/, 2 /*brk3*/,
		3 /*ndl1*/, 4 /*ndl2*/, 5 /*swd1*/, 6 /*swd2*/, 7 /*swp1*/, 8 /*swp2*/, 9 /*cnf1*/, 10 /*cnf2*/,
		11 /*blb1*/, 12 /*blb2*/, 13 /*str1*/, 14 /*str2*/, 15 /*str3*/, 17 /*nois*/, 18 /*strk*/, 29 /*tral*/,
		22 /*ex06*/, 23 /*ex07*/, 24 /*ex08*/, 25 /*ex09*/, 26 /*lens*/, 30 /*blnk*/, 31 /*mip1*/, 31 /*mip1*/,
		32 /*mip2*/, 33 /*beam*/, 34 /*fog0*/, 35 /*spec*/, 36 /*envr*/, 37 /*exlm*/ };
	inline int32 ImageOfHandle(int32 Handle) { return Handle >= 0 && Handle < NumTextures ? HandleImage[Handle] : INDEX_NONE; }

	enum EBlendMode : int32
	{
		BlendOpaque = 0,
		BlendNoWrite = 1,
		BlendMultiply = 2,
		BlendAlpha = 3,
		BlendDarken = 4,
		BlendAdditive = 5,
		BlendReverse = 6,
		BlendSubtract = 7,
		BlendBrighten = 8,
	};
	// 0x392CF8
	SSXTRICKYREBUILD_API int32 EmitterBlendMode(int32 Word);

	SSXTRICKYREBUILD_API uint32 VuRInit(float Seed);
	SSXTRICKYREBUILD_API uint32 VuRNext(uint32 R);
	SSXTRICKYREBUILD_API float AdvanceSeed(float Seed);

	FORCEINLINE double Drag(double Age) { const double A = FMath::Min(Age, DragClamp); return DragA * A - DragB * A * A; }

	struct SSXTRICKYREBUILD_API FEmitterConfig
	{
		int32 Count = 0;
		int32 Copies = 0;
		double EmitTime = 0.0;
		double TimeScale = 1.0;
		double Size = 0.0;             // 0x04 half-size mean (cm)
		double Life = 0.0;
		double SizeRange = 0.0;
		double LifeRange = 0.0;
		double CopySpacing = 0.0;
		FVector Origin = FVector::ZeroVector;
		FVector PosA = FVector::ZeroVector;
		FVector PosB = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;      // 0x12 cm/s
		FVector VelA = FVector::ZeroVector;
		FVector VelB = FVector::ZeroVector;
		FVector VelC = FVector::ZeroVector;
		FVector Drift = FVector::ZeroVector;
		FVector4 Key0 = FVector4(0, 0, 0, 0);
		FVector4 Key1 = FVector4(0, 0, 0, 0);
		FVector4 Jit0 = FVector4(0, 0, 0, 0);
		FVector4 Jit1 = FVector4(0, 0, 0, 0);
		int32 Texture = 0;
		int32 Blend = 0;

		static FEmitterConfig FromWords(TConstArrayView<uint32> W);
		double MaxLife() const { return Life + LifeRange * 0.5; }
		bool IsContinuous() const { return EmitTime < 0.0; }
	};

	struct FSprite
	{
		FVector Pos = FVector::ZeroVector;
		double HalfSize = 0.0;                  // world cm, screen aligned
		FVector4 Rgba = FVector4(0, 0, 0, 0);   // GS units (128 = 1.0), clamped 0..255
		int32 Particle = 0;
		int32 Copy = 0;
	};

	struct FInstanceMatrix
	{
		FVector X = FVector(1, 0, 0), Y = FVector(0, 1, 0), Z = FVector(0, 0, 1), T = FVector::ZeroVector;
		FVector Rotate(const FVector& V) const { return X * V.X + Y * V.Y + Z * V.Z; }
		FVector Point(const FVector& V) const { return Rotate(V) + T; }
		static SSXTRICKYREBUILD_API FInstanceMatrix FromMatrix(const FMatrix& M);
	};

	struct SSXTRICKYREBUILD_API FParticleBurst
	{
		int32 N = 0;
		int32 Copies = 0;
		double T = 0.0;
		double Interval = 0.0;
		double Ts = 1.0;
		double SizeLo = 0.0, SizeSpan = 0.0;
		double LifeLo = 0.0, LifeSpan = 0.0;
		double CopyDt = 0.0;
		float Seeds[9] = { 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f };
		FVector Drift = FVector::ZeroVector;
		FVector P0 = FVector::ZeroVector;
		FVector PA = FVector::ZeroVector, PB = FVector::ZeroVector;
		double PosScale = 0.5;
		FVector V0 = FVector::ZeroVector, VA = FVector::ZeroVector, VB = FVector::ZeroVector, VC = FVector::ZeroVector;
		FVector4 C0 = FVector4(0, 0, 0, 0);   // RGBA GS units
		FVector4 CSlope = FVector4(0, 0, 0, 0);
		FVector4 CJ0 = FVector4(0, 0, 0, 0), CJ1 = FVector4(0, 0, 0, 0);
		bool bContinuous = false;
		int32 Texture = 0;
		int32 BlendMode = BlendAlpha;

		/** From an emitter record. Rng: uniform [0, 1) draws for the nine seeds. Overrides replace the spawn point / velocity (CollideEmitter). */
		static FParticleBurst FromEmitter(const FEmitterConfig& C, const FInstanceMatrix& M, TFunctionRef<double()> Rng,
			const FVector* OriginOverride = nullptr, const FVector* VelocityOverride = nullptr);

		void Update(double Dt = Frame);
		int32 Evaluate(TArray<FSprite>& Out) const;
		bool IsFinished() const;
	};
}
