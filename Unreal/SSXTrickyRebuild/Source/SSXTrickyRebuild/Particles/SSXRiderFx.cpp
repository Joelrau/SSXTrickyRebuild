#include "Particles/SSXRiderFx.h"
#include "Particles/SSXSnowFx.h"

#include "Sim/EEFloat.h"
#include "Sim/SurfaceTable.h"

namespace SSXParticles
{
	namespace { constexpr double Frame_Dt() { return SSXParticles::Frame; } }

	bool IsSparkSurface(int32 S) { return S == 9 || S == 13 || S == 18 || S == 19; }

	FSurfaceSpray FSurfaceSpray::FromFields(TConstArrayView<double> F)
	{
		check(F.Num() >= 9);
		FSurfaceSpray S;
		S.Rate = F[0];
		S.Size = F[1];
		S.LifeLo = F[2];
		S.LifeHi = F[3];
		S.Texture = FMath::RoundToInt32(F[4]);
		S.Desat = F[5];
		S.Brighten = F[6];
		S.Alpha = F[7];
		S.Blend = FMath::RoundToInt32(F[8]);
		return S;
	}

	FSurfaceSpray FSurfaceSpray::FromParams(const FSurfaceParams& P)
	{
		double F[9];
		for (int32 I = 0; I < 9; ++I) { F[I] = P.F[13 + I]; }
		return FromFields(F);
	}

	FVector SprayColour(const FVector4& L, double Desat, double Brighten)
	{
		const FVector Rgb(L.Y, L.Z, L.W);
		const double M = (Rgb.X + Rgb.Y + Rgb.Z) / 3.0;
		FVector Out;
		for (int32 I = 0; I < 3; ++I) { Out[I] = FMath::Clamp(Rgb[I] + Desat * (M - Rgb[I]) + Brighten, 0.0, 1.0); }
		return Out;
	}

	namespace
	{
		struct FBurstSpec
		{
			int32 N = 1, Copies = 1;
			double Emit = 0.025, CopySpacing = 0.005, Ts = 5.0, T0 = 0.0;
			FVector Drift = FVector::ZeroVector;
			double SizeLo = 0.0, SizeHi = 0.0, LifeLo = 0.0, LifeHi = 0.0;
			FVector Centre = FVector::ZeroVector, AX = FVector::ZeroVector, BX = FVector::ZeroVector;
			bool bSegment = false;
			FVector SegA = FVector::ZeroVector, SegB = FVector::ZeroVector;
			FVector V0 = FVector::ZeroVector, VA = FVector::ZeroVector, VB = FVector::ZeroVector, VC = FVector::ZeroVector;
			FVector4 Key0 = FVector4(0, 0, 0, 0), Key1 = FVector4(0, 0, 0, 0), J0 = FVector4(0, 0, 0, 0), J1 = FVector4(0, 0, 0, 0);
			int32 Texture = 0, Blend = BlendAdditive;
		};

		/** A rider burst (the board-spray update's helper): P +- AX +- BX (pre-centred P - 3(a + b), 2a, 2b), or the rail
		 *  segment. Colours RGBA GS units. */
		FParticleBurst MakeBurst(const FBurstSpec& S, TFunctionRef<double()> Rng)
		{
			FParticleBurst B;
			B.N = S.N;
			B.Copies = S.Copies;
			B.T = S.T0;
			B.Interval = S.Emit * S.Ts / S.N;
			B.Ts = S.Ts;
			B.SizeLo = S.SizeLo;
			B.SizeSpan = S.SizeHi - S.SizeLo;
			B.LifeLo = S.LifeLo * S.Ts;
			B.LifeSpan = (S.LifeHi - S.LifeLo) * S.Ts;
			B.CopyDt = S.CopySpacing * S.Ts;
			for (float& Seed : B.Seeds) { Seed = float(Rng() + 1.0); }
			B.Drift = S.Drift / (S.Ts * S.Ts);
			if (S.bSegment)
			{
				B.PA = S.SegB - S.SegA;
				B.PB = FVector::ZeroVector;
				B.P0 = S.SegA + B.PA * 0.5;
				B.PosScale = 0.5;
			}
			else
			{
				B.P0 = S.Centre;
				B.PA = S.AX;
				B.PB = S.BX;
				B.PosScale = 1.0;
			}
			B.V0 = S.V0 / S.Ts;
			B.VA = S.VA / S.Ts;
			B.VB = S.VB / S.Ts;
			B.VC = S.VC / S.Ts;
			B.C0 = S.Key0;
			B.CSlope = (S.Key1 - S.Key0) * (1.0 / (S.LifeHi * S.Ts));
			B.CJ0 = S.J0;
			B.CJ1 = S.J1;
			B.Texture = S.Texture;
			B.BlendMode = S.Blend;
			return B;
		}
	}

	FParticleBurst FSprayFx::MakeSprayBurst(int32 N, const FVector& Point, const FVector& A, const FVector& U, const FVector& L,
		const FVector& V0, double SpreadA, double SpreadU, double SpreadL, double SizeLo, double SizeHi, double LifeLo,
		double LifeHi, const FVector& Rgb, double Alpha, int32 Texture, int32 Blend, TFunctionRef<double()> Rng)
	{
		FBurstSpec S;
		S.N = N;
		S.Copies = 1;
		S.Emit = 0.025;
		S.CopySpacing = 0.005;
		S.Ts = 5.0;
		S.Drift = FVector(0, 0, -100.0);
		S.SizeLo = SizeLo; S.SizeHi = SizeHi;
		S.LifeLo = LifeLo; S.LifeHi = LifeHi;
		S.Centre = Point;
		S.AX = A * 15.0;             // 30 x 180 cm board footprint
		S.BX = L * 90.0;
		S.V0 = V0;
		S.VA = A * SpreadA;
		S.VB = U * SpreadU;
		S.VC = L * SpreadL;
		S.Key0 = FVector4(Rgb.X * 128.0, Rgb.Y * 128.0, Rgb.Z * 128.0, Alpha * 128.0);
		S.Key1 = FVector4(Rgb.X * 128.0, Rgb.Y * 128.0, Rgb.Z * 128.0, 0.0);
		S.Texture = Texture;
		S.Blend = Blend;
		return MakeBurst(S, Rng);
	}

	void FSprayFx::Push(FParticleBurst&& B)
	{
		Ring.Add(MoveTemp(B));
		if (Ring.Num() > SprayRing)
		{
			Ring.RemoveAt(0);
		}
	}

	int32 FSprayFx::Frame(const FRiderFxInput& In, const FSurfaceSpray& Surf, TFunctionRef<double()> Rng)
	{
		for (FParticleBurst& B : Ring) { B.T += Frame_Dt() * B.Ts; }
		const double Speed = In.Vel.Size();
		const FVector& A = In.BoardAcross;
		const FVector& U = In.BoardUp;
		const FVector& L = In.BoardLong;
		double SpreadA = 400.0, SpreadU = 50.0, SpreadL = 400.0, SizeLo = 2.0, SizeHi = 4.0, LifeLo = 0.4, LifeHi = 0.6;
		double Desat = 0.7, Brighten = 0.2, Alpha = 0.25;
		int32 Blend = BlendAdditive, Texture = 13;
		double Rate = 0.0;
		FVector V0 = In.Vel;
		if (In.Motion == MotionGround && In.ContactKind != 2)
		{
			if (!IsSparkSurface(In.Surface))
			{
				const double E = In.bGoofy ? -In.Steer : In.Steer;
				V0 = In.Vel + A * (E * 1.1 * Speed);
				Rate = (E * E * 149.0 * Speed * 0.006 + Speed * 0.006) * Surf.Rate;
				const double D = FVector::DotProduct(V0, A);
				if (FMath::Abs(D) < 450.0)
				{
					SpreadA = 300.0;
				}
				else
				{
					SpreadA = FMath::Abs(D) - 150.0;
					V0 = D < 0.0 ? V0 - A * ((D + 450.0) * 0.5) : V0 + A * ((450.0 - D) * 0.5);
				}
				const double Dl = FVector::DotProduct(V0, L);
				V0 -= L * (Dl * 0.5);
				SpreadU = 0.0;
				SpreadL = Dl;
				SizeLo = Surf.Size * 0.8; SizeHi = Surf.Size * 1.2;
				LifeLo = Surf.LifeLo; LifeHi = Surf.LifeHi;
				Desat = Surf.Desat; Brighten = Surf.Brighten; Alpha = Surf.Alpha;
				Blend = Surf.Blend; Texture = Surf.Texture;
			}
		}
		else if (In.Motion == MotionRail)
		{
			Rate = (In.Surface == 13 || In.Surface == 18 || In.Surface == 19) ? 0.0 : 30.0;
		}
		if (In.Motion == MotionAir)
		{
			if (!bWasAir)
			{
				Rate = 70.0;
				AirRate = 70.0;
			}
			else
			{
				Rate = AirRate;
				if (Rate > 10.0)
				{
					Rate *= 0.94666666;
					AirRate = Rate;
				}
			}
			bWasAir = true;
			if (Speed / 277.77777 <= 1.0)
			{
				Rate *= Speed / 277.77777;
			}
		}
		else
		{
			bWasAir = false;
		}
		const int32 N = int32(Rate * Frame_Dt() + Rng());
		if (N > 0)
		{
			const FVector Point = In.Motion == MotionGround ? In.Contact : In.BoardOrigin;
			const FVector Rgb = SprayColour(In.LightTotal, Desat, Brighten);
			Push(MakeSprayBurst(N, Point, A, U, L, V0, SpreadA, SpreadU, SpreadL, SizeLo, SizeHi, LifeLo, LifeHi, Rgb, Alpha,
				Texture, Blend, Rng));
		}
		// 0x131B20
		if (In.bLaunchFlag)
		{
			Flash = FVector4(0.0, In.LaunchGlow, In.LaunchGlow, In.LaunchGlow);
		}
		Sparks(In, Speed, Rng);
		Flash.X = FMath::Clamp(Flash.X, 0.0, 1.0);
		Flash.Y = FMath::Clamp(Flash.Y - 1.0 / 12.0, 0.0, 1.0);
		Flash.Z = FMath::Clamp(Flash.Z - 1.0 / 12.0, 0.0, 1.0);
		Flash.W = FMath::Clamp(Flash.W - 1.0 / 12.0, 0.0, 1.0);
		return FMath::Max(N, 0);
	}

	void FSprayFx::Sparks(const FRiderFxInput& In, double Speed, TFunctionRef<double()> Rng)
	{
		if ((In.Motion != MotionGround && In.Motion != MotionRail) || !IsSparkSurface(In.Surface))
		{
			bSparkActive = false;
			return;
		}
		FVector Point = In.Contact + In.Vel * 0.02;
		if (In.Motion == MotionRail)
		{
			Point += In.Vel * 0.02;
		}
		if (!bSparkActive)
		{
			PrevPoint = Point;
		}
		bSparkActive = true;
		double Rate = 50.0, LifeHi = 0.25, K = 2.0;
		if (Rng() < Frame_Dt())
		{
			Rate = 1050.0; LifeHi = 0.65; K = 3.0;
			Flash = FVector4(0.0, 0.8, 0.6, 0.2);
		}
		const int32 N = int32(Rate * Frame_Dt() + Rng());
		if (N > 0)
		{
			const double S = FMath::Min(Speed, 1666.6666) * K;
			FBurstSpec B;
			B.N = N;
			B.Copies = 7;
			B.Emit = 0.025;
			B.CopySpacing = 0.007;
			B.Ts = 12.0;
			B.T0 = 0.03 * 12.0;
			B.Drift = FVector(0, 0, -2000.0);
			B.SizeLo = 0.75; B.SizeHi = 1.25;
			B.LifeLo = 0.1; B.LifeHi = LifeHi;
			B.Centre = Point;
			B.AX = In.BoardAcross * 15.0;
			B.BX = In.BoardLong * 90.0;
			if (In.Motion == MotionRail)
			{
				B.bSegment = true;
				B.SegA = Point;
				B.SegB = PrevPoint;
			}
			B.V0 = In.Up * (S * 0.05) - In.Fwd * (0.1 * S);
			B.VA = In.Right * FMath::Min(S * 0.35, 1111.1111);
			B.VB = In.Fwd * (S * 0.2);
			B.VC = In.Up * (S * 0.2);
			B.Key0 = FVector4(200.0, 90.0, 0.0, 80.0);
			B.Key1 = FVector4(200.0, 90.0, 90.0, 0.0);
			B.J0 = FVector4(0.0, 50.0, 0.0, 0.0);
			B.J1 = FVector4(0.0, 0.0, 50.0, 0.0);
			B.Texture = 0;
			B.Blend = BlendAdditive;
			Push(MakeBurst(B, Rng));
		}
		PrevPoint = Point;
	}

	bool CarveCloudSprite(const FCarveCloud& C, FCloudSprite& Out)
	{
		if (C.Age > CarveCloudLife)
		{
			return false;
		}
		Out.Size = FMath::Max(CarveCloudSizeMin, CarveCloudSizeEnd / CarveCloudLife * C.Age);
		Out.Alpha = C.Age < C.FadeIn ? C.Intensity * C.Age / C.FadeIn
			: (CarveCloudLife - C.Age) * C.Intensity / (CarveCloudLife - C.FadeIn);
		Out.Pos = C.Pos + C.Up * (Out.Size * 0.5);
		Out.Texture = 24 + C.Variant;
		return true;
	}

	void FCarveClouds::Frame(const FRiderFxInput& In, TFunctionRef<uint32()> RngBits)
	{
		for (FCarveCloud& C : Clouds) { C.Age += Frame_Dt(); }
		Clouds.RemoveAll([](const FCarveCloud& C) { return C.Age > CarveCloudLife; });
		if (In.ContactKind == 2 || In.Motion != MotionGround)
		{
			return;
		}
		const bool bIce = In.Surface == 3 || In.Surface == 4;
		if (!bIce && FMath::Abs(In.Steer) <= 0.2)
		{
			return;
		}
		if (bHaveLast && FVector::Dist(In.BoardOrigin, LastOrigin) <= CarveCloudSpacing)
		{
			return;
		}
		FCarveCloud C;
		C.Pos = In.BoardOrigin;
		C.Up = In.Up;
		C.FlipU = RngBits() & 1;
		C.FlipV = RngBits() & 1;
		C.Variant = RngBits() & 3;
		C.Intensity = (bIce ? 1.0 : FMath::Abs(In.Steer)) * 0.2;
		const FVector Rgb = SprayColour(In.LightTotal, 0.5, 0.0);
		C.Rgb = FVector(FMath::Min(Rgb.X, 1.0), FMath::Min(Rgb.Y, 1.0), FMath::Min(Rgb.Z, 1.0));
		C.FadeIn = bIce ? 0.0 : 0.2;
		Clouds.Add(C);
		if (Clouds.Num() > CarveCloudRing)
		{
			Clouds.RemoveAt(0);
		}
		LastOrigin = In.BoardOrigin;
		bHaveLast = true;
	}

	int32 LandingPuffCount(double Impact) { return FMath::Clamp(int32(Impact * -0.00090000004), 2, 8); }
	double LandingIntensityAdd(double Impact) { return (0.2 - Impact * 0.00051428576) * 1.5; }

	bool GhostTint(double Strength, double Burst, FVector4& Out)
	{
		if (!(Strength > 0.0 || Burst > 0.0))
		{
			return false;
		}
		if (Burst > 0.0 || Strength > 0.66599226) { Out = FVector4(0.0, 1.0, 0.0758, 0.002); }
		else if (Strength > 0.33362994) { Out = FVector4(0.0, 1.0, 0.4746, 0.0); }
		else { Out = FVector4(0.0, 1.0, 0.9645, 0.0); }
		return true;
	}

	bool FAfterimages::Frame(int32 FrameNo, double Strength, double Burst)
	{
		if (FrameNo % 3 != 0)
		{
			return false;
		}
		FVector4 Tint;
		if (GhostTint(Strength, Burst, Tint))
		{
			Tints[Head] = Tint;
			Head = (Head + 1) % GhostRing;
			Count = FMath::Min(Count + 1, GhostRing);
			return true;
		}
		if (Count > 0) { --Count; }
		return false;
	}

	const double FBoostRibbons::Alpha[8] = { 0.65, 0.455, 0.3185, 0.2229, 0.1561, 0.1092, 0.0765, 0.0 };

	FRibbonSample FBoostRibbons::MakeSample(const FVector& Hand0, const FVector& Fore0, const FVector& Upper0,
	                                        const FVector& Hand1, const FVector& Fore1, const FVector& Upper1, const FVector4& Tint)
	{
		FRibbonSample S;
		S.Outer[0] = Hand0 + (Hand0 - Fore0).GetSafeNormal() * Extend;
		S.Inner[0] = Upper0;
		S.Outer[1] = Hand1 + (Hand1 - Fore1).GetSafeNormal() * Extend;
		S.Inner[1] = Upper1;
		S.Tint = Tint;
		return S;
	}

	void FBoostRibbons::Frame(int32 FrameNo, double BoostStrength, double BurstTimer, const FRibbonSample& Sample)
	{
		if (Ring.Frame(FrameNo, BoostStrength, BurstTimer))
		{
			const int32 Slot = (Ring.GetHead() + GhostRing - 1) % GhostRing;
			Samples[Slot] = Sample;
			Samples[Slot].Tint = Ring.GetTint(Slot);
		}
	}

	double FBoostRibbons::OuterAlpha(int32 Count, int32 K)
	{
		const int32 Start = 7 - FMath::Clamp(Count, 0, 7);
		return Alpha[FMath::Clamp(Start + FMath::Max(K - 1, 0), 0, 7)];
	}

	double TrailSurfaceScale(int32 Surface)
	{
		switch (Surface)
		{
		case 1: case 2: case 8: case 15: case 16: return 0.02855919;
		case 3: case 4: return 0.016661247;
		case 5: return 0.19974777;
		default: return -1.0;
		}
	}

	bool TrailActive(int32 Surface, int32 Motion, int32 ContactKind)
	{
		return TrailSurfaceScale(Surface) >= 0.0 && (Motion == 2 || Motion == 5 || Motion == 6) && ContactKind != 2 && ContactKind != 3;
	}

	void TrailSideWeights(double Side, double Steer, double& OutKA, double& OutKB)
	{
		double K0 = Side * 0.6666667, K1 = -Side * 0.6666667;
		if (Steer > 0.0)
		{
			K0 = Steer * 0.5 * K0 + K0;
		}
		else
		{
			K1 = K1 * (1.0 - Steer * 0.5);
		}
		OutKA = K0;
		OutKB = K1;
	}

	double TrailSideStep(double Side, double LitA, double LitB, bool bActiveBefore)
	{
		if (!bActiveBefore)
		{
			return LitB > LitA ? 1.0 : -1.0;
		}
		Side += LitB > LitA ? FGroundTrail::SideStep : -FGroundTrail::SideStep;
		return FMath::Clamp(Side, -1.0, 1.0);
	}

	bool TrailShouldPush(double Dist, double Speed, double DWidth, double CosDir, bool bActive)
	{
		if (Dist <= FGroundTrail::PushDist && bActive)
		{
			if (Speed <= FGroundTrail::PushSpeed)
			{
				return false;
			}
			if (FMath::Abs(DWidth) <= FGroundTrail::PushDWidth && CosDir >= FGroundTrail::PushCos)
			{
				return false;
			}
		}
		return true;
	}

	FVector TrailAcross(const FVector& Vel, const FVector& Normal)
	{
		return FVector::CrossProduct(Vel, Normal).GetSafeNormal(1e-12, FVector::ZeroVector);
	}

	void TrailEdgePoints(const FRiderFxInput& In, FVector Out[4])
	{
		// 0x132648 / 0x114730 / 0x129628
		FVector P[4];
		BoardCorners(In, P);
		if (In.Motion == 2 || !In.bTrailBones)
		{
			for (int32 K = 0; K < 4; ++K) { Out[K] = P[K]; }
			return;
		}
		Out[0] = In.TrailBone12;
		Out[1] = In.TrailBone8;
		Out[2] = P[0];
		Out[3] = P[3];
	}

	double TrailPushWidth(const FVector Q[4], const FVector& D)
	{
		return FMath::Max(FMath::Abs(FVector::DotProduct(Q[0] - Q[3], D)), FMath::Abs(FVector::DotProduct(Q[1] - Q[2], D)));
	}

	FVector TrailLiveHalf(const FVector Q[4], const FVector& D, const FVector& Normal, double& OutWidth)
	{
		const FVector E1 = Q[0] - Q[1], E2 = Q[0] - Q[2];
		const FVector Sel = FMath::Abs(FVector::DotProduct(E1, D)) < FMath::Abs(FVector::DotProduct(E2, D)) ? E2 : E1;
		OutWidth = Sel.Size();
		const FVector Dir = OutWidth > 0.0 ? Sel / OutWidth : FVector::ZeroVector;
		FVector Flat = (Dir - Normal * FVector::DotProduct(Dir, Normal)).GetSafeNormal(1e-12, FVector::ZeroVector);
		if (FVector::DotProduct(Flat, D) < 0.0)
		{
			Flat = -Flat;
		}
		return Flat * (OutWidth * 0.5);
	}

	void FGroundTrail::Frame(const FRiderFxInput& In, double LitA, double LitB)
	{
		auto Slot = [this](int32 J) -> FEntry& { return Ring[(Head - J + 2 * Entries) % Entries]; };
		if (!TrailActive(In.Surface, In.Motion, In.ContactKind))
		{
			if (RunAlpha != 0.f)
			{
				float R = 0.f;
				for (int32 J = 1; J <= Count; ++J)
				{
					Slot(J).Alpha *= R;
					R += 0.3f;
					if (R >= 1.f)
					{
						break;
					}
				}
			}
			RunAlpha = 0.f;
			bActive = false;
			FEntry& Next = Ring[Head];
			Next.KA = Next.KB = Next.Alpha = 0.0;
			return;
		}
		const double Scale = TrailSurfaceScale(In.Surface);
		const FVector N = In.GroundNormal;
		const FVector D = TrailAcross(In.Vel, N);
		FVector Q[4];
		TrailEdgePoints(In, Q);
		double Width = TrailPushWidth(Q, D);
		const bool bRun = RunAlpha != 0.f;
		Side = TrailSideStep(Side, LitA, LitB, bRun);
		double KA, KB;
		TrailSideWeights(Side, In.Steer, KA, KB);
		const FVector ToC = In.Contact - LastPos;
		const double Dist = ToC.Size();
		const FVector Dir = Dist != 0.0 ? ToC / Dist : ToC;
		if (In.Brake == 0.0)
		{
			Width *= 1.0 - FMath::Abs(In.Steer) * 0.85;
		}
		const FVector Base = In.Contact + N * Lift;
		if (TrailShouldPush(Dist, In.Vel.Size(), Width - LastWidth, FVector::DotProduct(Dir, LastDir), bRun))
		{
			FEntry& E = Ring[Head];
			E.A = Base + D * (Width * 0.5);
			E.B = Base - D * (Width * 0.5);
			E.S = Width * Scale;
			E.bBreak = !bRun;
			LastDir = Dir;
			LastPos = In.Contact;
			LastWidth = Width;
			RunAlpha = FMath::Min(RunAlpha + 0.3f, 1.f);
			Head = (Head + 1) % Entries;
			FEntry& Next = Ring[Head];
			Next.KA = KA;
			Next.KB = KB;
			Next.Alpha = RunAlpha;
			Count = FMath::Min(Count + 1, Entries);
			for (int32 J = FadeFrom; J < Count; ++J)
			{
				FEntry& Old = Slot(J);
				Old.Alpha = FMath::Max(Old.Alpha - 0.1, 0.0);
			}
		}
		double LiveWidth = 0.0;
		const FVector Half = TrailLiveHalf(Q, D, N, LiveWidth);
		Live.A = Base + Half;
		Live.B = Base - Half;
		Live.S = LiveWidth * Scale;
		Live.KA = KA;
		Live.KB = KB;
		Live.Alpha = RunAlpha;
		Live.bBreak = false;
		if (Count >= 2)
		{
			const FEntry& L = Slot(1);
			if (FVector::DotProduct(Live.A - L.A, In.Vel) < 0.0) { Live.A = L.A; }
			if (FVector::DotProduct(Live.B - L.B, In.Vel) < 0.0) { Live.B = L.B; }
		}
		bActive = true;
	}

	void TrailStrips(const FGroundTrail& Trail, bool bBrighten, const FVector& Offset, TArray<TArray<FTrailVertex>>& Out)
	{
		TArray<FGroundTrail::FEntry> Stored;
		for (int32 J = 1; J < Trail.Num(); ++J) { Stored.Add(Trail.Get(J)); }
		TrailStrips(Trail.HasLiveHead() ? &Trail.Get(0) : nullptr, Stored, bBrighten, Offset, Out);
	}

	void TrailStrips(const FGroundTrail::FEntry* Head, TConstArrayView<FGroundTrail::FEntry> Stored, bool bBrighten, const FVector& Offset,
		TArray<TArray<FTrailVertex>>& Out)
	{
		const float Mult = bBrighten ? 128.f : -64.f;
		auto PassAlpha = [Mult](double K, double Alpha) { return FMath::Max(int32((float(K) * float(Alpha)) * Mult), 0); };
		TArray<FTrailVertex> Cur;
		int32 Strong = -1;
		bool bT = false;
		auto AddPair = [&](const FGroundTrail::FEntry& E, float T)
		{
			int32 AlA = PassAlpha(E.KA, E.Alpha), AlB = PassAlpha(E.KB, E.Alpha);
			const int32 S = AlB < AlA ? 0 : 1;
			if (S != Strong)
			{
				if (Strong != -1)
				{
					AlA = AlB = 0;
					if (Cur.Num() >= 2)
					{
						Cur[Cur.Num() - 1].Alpha = 0;
						Cur[Cur.Num() - 2].Alpha = 0;
					}
				}
				Strong = S;
			}
			Cur.Add({ E.A + Offset, S == 0 ? 0.f : float(E.S), T, AlA });
			Cur.Add({ E.B + Offset, S == 1 ? 0.f : float(E.S), T, AlB });
		};
		if (Head)
		{
			const FGroundTrail::FEntry& L = *Head;
			const int32 AlA = PassAlpha(L.KA, L.Alpha), AlB = PassAlpha(L.KB, L.Alpha);
			Strong = AlA <= AlB ? 1 : 0;
			Cur.Add({ L.A + Offset, Strong == 0 ? 0.f : float(L.S), 0.f, AlA });
			Cur.Add({ L.B + Offset, Strong == 1 ? 0.f : float(L.S), 0.f, AlB });
			bT = true;
		}
		for (const FGroundTrail::FEntry& E : Stored)
		{
			AddPair(E, bT ? 1.f : 0.f);
			bT = !bT;
			if (E.bBreak)
			{
				if (Cur.Num() >= 3)
				{
					Out.Add(Cur);
				}
				Cur.Reset();
			}
		}
		if (Cur.Num() >= 3)
		{
			Out.Add(MoveTemp(Cur));
		}
	}

	const FVector4 FPowerFxSlot::Colour0(1.0, 1.3134, 1.2892, 1.0417);
	const FVector4 FPowerFxSlot::Colour1(0.0, 0.0, 0.0, 1.2399);

	FVector FPowerFxSlot::EmitPoint(const FVector& BoneA, const FVector& BoneB)
	{
		return BoneB + (BoneB - BoneA).GetSafeNormal() * EmitOffset;
	}

	int32 FPowerFxSlot::Frame(const FVector& Point, const FVector& RiderVel, int32 PoolFree)
	{
		Prev = bHaveCur ? Cur : Point;
		Cur = Point;
		bHaveCur = true;
		const FVector D = (Cur - Prev) - RiderVel;
		const FVector Vel = RiderVel + D * Follow;
		TArray<FPart> Alive;
		Alive.Reserve(Parts.Num() + 1);
		for (FPart& P : Parts)
		{
			if (P.Frames == 0)
			{
				continue;
			}
			P.Argb += P.DArgb;
			P.Size += P.DSize;
			P.Vel *= Damping;
			P.Pos += P.Vel + FVector(0, 0, DriftZ);
			--P.Frames;
			Alive.Add(P);
		}
		Parts = MoveTemp(Alive);
		const double X = Rate + Acc;
		const int32 N = int32(X);
		Acc = X - N;
		int32 Made = 0;
		for (int32 I = 0; I < N && PoolFree - Made > 0; ++I)
		{
			FPart P;
			P.Pos = Cur;
			P.Vel = Vel;
			P.Frames = LifeFrames;
			P.Size = Size0;
			P.DSize = (Size1 - Size0) / LifeFrames;
			P.Argb = Colour0;
			P.DArgb = (Colour1 - Colour0) * (1.0 / LifeFrames);
			Parts.Insert(P, 0);
			++Made;
		}
		return Made;
	}

	double ShadowSilhouetteLevel(int32 Motion, double H)
	{
		double X = 0.0;
		if (Motion == MotionGround && H < 0.0)
		{
			X = FMath::Max(H, -5.0);
		}
		return 0.3 - (X / -5.0) * 0.3;
	}

	FBox2D ShadowExtents(const FVector& Eye, const FVector& Ax, const FVector& Ay, TConstArrayView<FVector> Points)
	{
		FBox2D B(FVector2D(UE_DOUBLE_BIG_NUMBER), FVector2D(-UE_DOUBLE_BIG_NUMBER));
		for (const FVector& P : Points)
		{
			const FVector D = P - Eye;
			const FVector2D L(FVector::DotProduct(D, Ax), FVector::DotProduct(D, Ay));
			B.Min = FVector2D::Min(B.Min, L);
			B.Max = FVector2D::Max(B.Max, L);
		}
		B.Min -= FVector2D(20.0);
		B.Max += FVector2D(20.0);
		B.bIsValid = Points.Num() > 0;
		return B;
	}

	bool ShadowBoardInSilhouette(int32 Motion, double H)
	{
		// 0x136AEC / 0x136B84 / 0x207F18
		float X = 0.f;
		if (Motion == MotionGround && H < 0.0)
		{
			X = FMath::Max(float(H), -5.f);
		}
		const float K = SSXEE::FpuSub(0.3f, (X / -5.f) * 0.3f);
		return K == 0.3f;
	}

	void FSnowfall::Init(double X, double R1, double R2, TFunctionRef<double()> Rng)
	{
		Intensity = 0.05 + 0.15 * X;
		A = 5.0 + R1;
		B = 7.0 - R2;
		Offset = 0.0;
		const int32 N = FlakesPerSlot(Intensity);
		for (int32 S = 0; S < Slots; ++S)
		{
			// 0x1C8EC8 / 0x31C790
			float SeedX = float(Rng() + 1.0);
			float SeedY = float(Rng() + 1.0);
			float SeedZ = float(Rng() + 1.0);
			float SeedSize = float(Rng() + 1.0);
			Slot[S].Reset();
			for (int32 K = 0; K < N; ++K)
			{
				SeedX = AdvanceSeed(SeedX);
				SeedY = AdvanceSeed(SeedY);
				SeedZ = AdvanceSeed(SeedZ);
				SeedSize = AdvanceSeed(SeedSize);
				FFlake F;
				F.P = FVector((double(SeedX) - 1.0) * Cube, (double(SeedY) - 1.0) * Cube, (double(SeedZ) - 1.0) * Cube);
				F.Half = FlakeHalfSize(A, B, double(SeedSize) - 1.0);
				Slot[S].Add(F);
			}
		}
		bInit = true;
	}

	void FSnowfall::Gather(const FVector& Cam, TArray<FSprite>& Out, const FVector* ViewDir, double ConeHalfAngle) const
	{
		if (!bInit)
		{
			return;
		}
		const int32 R = FMath::CeilToInt32(DrawRadius / Cube) + 1;
		const FIntVector C(FMath::FloorToInt32(Cam.X / Cube), FMath::FloorToInt32(Cam.Y / Cube), FMath::FloorToInt32(Cam.Z / Cube));
		const FVector Fall(0, 0, -Offset);
		for (int32 Dz = -R; Dz <= R; ++Dz)
		for (int32 Dy = -R; Dy <= R; ++Dy)
		for (int32 Dx = -R; Dx <= R; ++Dx)
		{
			const FIntVector I = C + FIntVector(Dx, Dy, Dz);
			const FVector Origin(I.X * Cube, I.Y * Cube, I.Z * Cube);
			const FVector ToCube = Origin + FVector(Cube * 0.5) - Cam;
			const double D = ToCube.Size();
			if (D > DrawRadius)
			{
				continue;
			}
			// 0x1C9740: frustum-culled octree; here the cube's bounding sphere against the view cone
			constexpr double CubeRadius = Cube * 0.8660254;
			if (ViewDir && D > CubeRadius)
			{
				const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(ToCube / D, *ViewDir), -1.0, 1.0));
				if (Angle > ConeHalfAngle + FMath::Asin(CubeRadius / D)) { continue; }
			}
			const double Fade = D <= FadeStart ? 1.0 : (DrawRadius - D) / (DrawRadius - FadeStart);
			const int32 S = (I.X & 1) | ((I.Y & 1) << 1) | ((I.Z & 1) << 2);
			for (const FFlake& F : Slot[S])
			{
				FSprite& Sp = Out.AddDefaulted_GetRef();
				Sp.Pos = Origin + F.P + Fall;
				Sp.HalfSize = F.Half;
				Sp.Rgba = FVector4(Grey, Grey, Grey, Alpha * Fade);
			}
		}
	}
}
