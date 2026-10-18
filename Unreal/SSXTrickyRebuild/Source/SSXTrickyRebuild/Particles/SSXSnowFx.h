#pragma once

#include "CoreMinimal.h"
#include "Particles/SSXRiderFx.h"

namespace SSXParticles
{
	using FRandU = TFunctionRef<double()>;
	using FRandBits = TFunctionRef<uint32()>;

	SSXTRICKYREBUILD_API FVector DesatToMean(const FVector& Rgb, double K);

	// One vertex of an effect strip (game space; colour = GS bytes, alpha GS).
	struct FFxVertex
	{
		FVector Pos = FVector::ZeroVector;
		FVector2D UV = FVector2D::ZeroVector;
		FColor Color = FColor(0, 0, 0, 0);
	};

	struct FSplashSlot
	{
		static constexpr int32 MaxPoints = 25;
		int32 N = 0;
		FVector P[MaxPoints], V[MaxPoints];
		double W[MaxPoints] = {};
		FVector Centre = FVector::ZeroVector, CV = FVector::ZeroVector;
		FVector Light = FVector::OneVector;
		double Age = 0.0, Life = 0.0;
	};

	class SSXTRICKYREBUILD_API FSplashFx
	{
	public:
		static constexpr int32 Slots = 5;
		static constexpr double MaxLife = 0.9949894;

		// 0x12C7E0
		void Impact(const FVector& C, const FVector& V, const FVector& N, const FRiderFxInput& In, FRandU U);
		// 0x12CCC8
		void Land(double Vn, const FRiderFxInput& In, FRandU U);
		// 0x12D108
		void Frame(const FRiderFxInput& In, FRandU U);
		int32 Num() const { return Count; }
		const FSplashSlot& Get(int32 Idx) const { return Ring[(Head + Idx) % Slots]; }
		void Reset() { *this = FSplashFx(); }

		// 0x12C190
		static void LayoutRing(FSplashSlot& S, double Speed, double Asym, double WScale, const FVector& N, const FVector& F,
		                       const FVector& Side, FRandU U);
		// 0x12C4D0
		static void LayoutFan(FSplashSlot& S, double Speed, double WScale, const FVector& N, const FVector& F, const FVector& Side,
		                      const FVector& BoardLong, FRandU U);
		// 0x12D6C0
		static FVector Point(const FSplashSlot& Slot, int32 I, double S);
		static FColor Colour(const FVector& Light);
		// 0x12D890
		static bool Strips(const FSplashSlot& Slot, TArray<FFxVertex> Out[4]);

	private:
		FSplashSlot& Push();
		FSplashSlot Ring[Slots];
		int32 Count = 0, Head = 0;
		FVector Last = FVector::ZeroVector;
		double Cooldown = 0.0, BrakeTimer = 0.0;
	};

	struct FPuff
	{
		FVector Pos = FVector::ZeroVector, N = FVector::UpVector, Rgb = FVector::OneVector;
		int32 FlipU = 0, FlipV = 0, Variant = 0;
		double Scale = 0.0;
		double Age = 0.0;
	};

	struct FPuffSprite { FVector Pos; double Half = 0.0; double Alpha = 0.0; int32 Image = 24; };
	SSXTRICKYREBUILD_API FPuffSprite PuffSprite(const FPuff& P);

	class SSXTRICKYREBUILD_API FPuffFx
	{
	public:
		// 0x12E0E8
		int32 Land(double Vn, const FRiderFxInput& In, FRandU U, FRandBits Bits);
		// 0x12E008
		void Impact(const FVector& C, const FVector& V, const FVector& N, const FRiderFxInput& In, FRandU U, FRandBits Bits);
		// 0x12E6E8
		void Frame(const FRiderFxInput& In, FRandU U, FRandBits Bits);
		int32 Num() const { return Puffs.Num(); }
		double GetIntensity() const { return Intensity; }
		const TArray<FPuff>& Get() const { return Puffs; }
		const FSplashFx& GetSplash() const { return Splash; }
		void Reset() { *this = FPuffFx(); }
		// 0x12E330
		void Spawn(double Scale, const FVector& P, const FVector& N, const FVector& V, const FRiderFxInput& In, FRandU U, FRandBits Bits);

	private:
		TArray<FPuff> Puffs;
		FSplashFx Splash;
		double Acc = 0.0, Intensity = 0.0;
	};

	struct FGlint
	{
		double Age = 0.0, Df = 0.0, Ds = 0.0, Life = 0.0, Size0 = 0.0, Size1 = 0.0;
		int32 FlipU = 0, FlipV = 0, Variant = 0;
	};

	class SSXTRICKYREBUILD_API FIceGlints
	{
	public:
		static constexpr int32 Ring = 64;
		static constexpr double Intensity = 0.16996880;
		void Frame(const FRiderFxInput& In, FRandU U, FRandBits Bits);
		int32 Num() const { return Glints.Num(); }
		const TArray<FGlint>& Get() const { return Glints; }
		bool Sprite(const FGlint& G, FVector& OutPos, double& OutHalf, double& OutAlpha) const;
		FVector GetRgb() const { return Rgb; }
		void Reset() { *this = FIceGlints(); }

		FVector P = FVector::ZeroVector, F = FVector(1, 0, 0), S = FVector(0, -1, 0), N = FVector::UpVector;
	private:
		TArray<FGlint> Glints;
		FVector Rgb = FVector::OneVector;
	};

	struct FCurtainSeg
	{
		FVector P[4], V[4];
		double A = 0.0, R = 0.0, G = 0.0, B = 0.0;
		double U = 0.0, Age = 0.0;
		bool bStart = false;
	};

	class SSXTRICKYREBUILD_API FCarveCurtain
	{
	public:
		static constexpr int32 Segments = 75;
		static constexpr double Life = 0.9;
		void Frame(const FRiderFxInput& In, FRandU U);
		bool IsActive() const { return bActive; }
		int32 Num() const { return Count; }
		const FCurtainSeg& Get(int32 Idx) const { return Ring[(Head + Idx) % Segments]; }
		FVector GetAnchor() const { return Anchor; }
		// 0x130B98
		void Strips(TArray<TArray<FFxVertex>>& Out) const;
		// 0x130960
		static double SegmentU(int32 K);
		void Reset() { *this = FCarveCurtain(); }

	private:
		void Push(const FVector4& Light, bool bStart, FRandU U);
		void Place(double C, double Width, const FVector& P, const FVector& L, const FVector& D, double Speed);
		FCurtainSeg Ring[Segments];
		int32 Count = 0, Head = 0, K = 0;
		bool bActive = false;
		FVector Anchor = FVector::ZeroVector, Last = FVector::ZeroVector, Dir0 = FVector::ZeroVector;
		double Jit = 0.0, Carve = 0.0, Wd = 0.0;
	};

	/** Board corners (0x129628 tail): P0 = C + front L + h A, P1 = C + front L - h A, P2 = C - back L + h A,
	 *  P3 = C - back L - h A, per board class, front / back swapped for profile +0x40 != 0, scaled by profile +0x60. */
	SSXTRICKYREBUILD_API void BoardCorners(const FRiderFxInput& In, FVector Out[4]);
	SSXTRICKYREBUILD_API void LiveBoardCorners(const FRiderFxInput& In, FVector Out[4]);

	struct FSpinSample { FVector P[4]; };

	class SSXTRICKYREBUILD_API FSpinRibbons
	{
	public:
		static constexpr int32 RingSize = 7;
		static constexpr int32 ControlSpin = 16;
		static const FVector Tint;
		/** Every frame: push the sample (0x136148 -> 0x136E38: the previous pose's corners) while in Spin, else the count falls by one. The drawn live sample is taken at draw time (this pose's corners). */
		void Frame(bool bSpin, const FSpinSample& Sample);
		bool IsDrawn() const { return Count > 1; }
		int32 GetCount() const { return Count; }
		const FSpinSample& Get(int32 Idx) const { return Samples[(Head - 1 - Idx + 2 * RingSize) % RingSize]; }
		void Reset() { *this = FSpinRibbons(); }

	private:
		FSpinSample Samples[RingSize];
		int32 Head = 0, Count = 0;
	};
}
