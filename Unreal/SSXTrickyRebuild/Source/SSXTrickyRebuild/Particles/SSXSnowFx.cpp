#include "Particles/SSXSnowFx.h"

namespace SSXParticles
{
	namespace
	{
		constexpr double Dt = 1.0 / 60.0;

		FVector Unit(const FVector& V, const FVector& Fallback)
		{
			const double L = V.Size();
			return L > 1e-9 ? V / L : Fallback;
		}
		FVector Tangent(const FVector& V, const FVector& N) { return V - N * FVector::DotProduct(V, N); }
		FVector LightRgb(const FRiderFxInput& In) { return FVector(In.LightTotal.Y, In.LightTotal.Z, In.LightTotal.W); }
		uint8 Byte(double V) { return uint8(FMath::Clamp(int32(V), 0, 255)); }   // (int) truncation, clamped to a byte

		double Jitter(double J, FRandU U)
		{
			J += U() * 0.6 - 0.3;
			if (J < 0.8) { J = 1.6 - J; }
			if (J > 1.2) { J = 2.4 - J; }
			return J;
		}

		FVector SplashCentreVelocity(const FVector& Vel, const FVector& N)
		{
			const FVector Ut = Tangent(Vel, N);
			const double S = Ut.Size() * 0.7;
			if (S > 833.3333)
			{
				return Ut * (833.3333 / S) + N * 58.333332;
			}
			return Ut + N * (S * 0.07);
		}
	}

	FVector DesatToMean(const FVector& Rgb, double K)
	{
		const double M = (Rgb.X + Rgb.Y + Rgb.Z) * 0.33333334;
		return FVector(Rgb.X + (M - Rgb.X) * K, Rgb.Y + (M - Rgb.Y) * K, Rgb.Z + (M - Rgb.Z) * K);
	}

	FSplashSlot& FSplashFx::Push()
	{
		if (Count < Slots) { ++Count; }
		Head = (Head + Slots - 1) % Slots;
		FSplashSlot& S = Ring[Head];
		S = FSplashSlot();
		return S;
	}

	void FSplashFx::LayoutRing(FSplashSlot& S, double Speed, double Asym, double WScale, const FVector& N, const FVector& F,
	                           const FVector& Side, FRandU U)
	{
		S.N = FSplashSlot::MaxPoints;
		double J = 1.0;
		for (int32 I = 0; I < S.N - 1; ++I)
		{
			J = Jitter(J, U);
			const double Th = 2.0 * I * UE_DOUBLE_PI / (S.N - 1);
			const double Wrap = FMath::Abs((Th - FMath::FloorToDouble(Th * 0.15915494 + 0.5) * 6.2831855) / UE_DOUBLE_PI);
			const FVector Dir = F * FMath::Cos(Th) + Side * FMath::Sin(Th);
			S.P[I] = FVector::ZeroVector;
			S.V[I] = (Dir + N * 0.4) * ((1.0 - Asym * (2.0 * Wrap - 1.0)) * J * Speed);
			S.W[I] = WScale * J;
		}
		S.P[S.N - 1] = S.P[0];
		S.V[S.N - 1] = S.V[0];
		S.W[S.N - 1] = S.W[0];
	}

	void FSplashFx::LayoutFan(FSplashSlot& S, double Speed, double WScale, const FVector& N, const FVector& F, const FVector& Side,
	                          const FVector& BoardLong, FRandU U)
	{
		S.N = 12;
		const double Half = U() * 0.4363323 + 0.5235988;
		double J = 1.0;
		for (int32 I = 0; I < S.N; ++I)
		{
			J = Jitter(J, U);
			const double T = 2.0 * I / (S.N - 1) - 1.0;
			const double T2 = T * T;
			const double Wt = 1.0 - T2 * T2;
			const FVector Dir = F * FMath::Cos(T * Half) + Side * FMath::Sin(T * Half);
			S.P[I] = BoardLong * (80.0 * T);
			S.V[I] = (Dir + N * 0.4) * (J * Speed * Wt);
			S.W[I] = WScale * J * Wt;
		}
	}

	void FSplashFx::Impact(const FVector& C, const FVector& V, const FVector& N, const FRiderFxInput& In, FRandU U)
	{
		if (In.ContactKind == 2 || In.SurfWidth < 0.02 || N.Z < 0.3)
		{
			return;
		}
		const double Dist = FVector::Dist(C, Last);
		if (Count != 0 && (Dist == 0.0 || Cooldown > 0.3))
		{
			return;
		}
		Cooldown += Dist > 0.0 ? 100.0 / Dist : double(MAX_flt);
		Last = C;
		const FVector Side = Unit(FVector::CrossProduct(Tangent(V, N), N), In.Right);
		const FVector F = FVector::CrossProduct(N, Side);
		FSplashSlot& S = Push();
		S.Light = LightRgb(In);
		S.Centre = C + N * 5.0;
		S.CV = SplashCentreVelocity(In.Vel, N);
		S.Life = MaxLife;
		const double Speed = U() * 150.0 + 250.0;
		const double Asym = U() * 0.3;
		LayoutRing(S, Speed, Asym, In.SurfWidth * 0.8, N, F, Side, U);
	}

	void FSplashFx::Land(double Vn, const FRiderFxInput& In, FRandU U)
	{
		if (In.ContactKind == 2 || In.SurfWidth < 0.02)
		{
			return;
		}
		double S0 = Vn * -0.28114587;
		if (!(S0 > 154.46002))
		{
			return;
		}
		S0 = FMath::Min(S0, 490.5319);
		const FVector N = In.GroundNormal;
		const FVector Side = Unit(FVector::CrossProduct(Tangent(In.Vel, N), N), In.Right);
		const FVector F = FVector::CrossProduct(N, Side);
		FSplashSlot& S = Push();
		S.Light = LightRgb(In);
		S.Life = (S0 * 0.0016308827 + 0.2) * 0.9949894;
		S.Centre = In.Contact + N * 5.0;
		S.CV = SplashCentreVelocity(In.Vel, N);
		const double Speed = In.Vel.Size();
		const double Asym = FMath::Clamp(Speed > 0.0 ? S.CV.Size() / Speed : 0.8, 0.2, 0.8);
		LayoutRing(S, S0, Asym, In.SurfWidth * 0.8, N, F, Side, U);
	}

	void FSplashFx::Frame(const FRiderFxInput& In, FRandU U)
	{
		Cooldown *= 0.6666666;
		if (In.ContactKind == 2 || !(FMath::Abs(In.Brake) > 0.7) || In.Motion != MotionGround)
		{
			BrakeTimer = 0.0;
		}
		else if (BrakeTimer > 0.0 && !(In.SurfWidth < 0.02))
		{
			BrakeTimer -= Dt;
		}
		else
		{
			double S0 = In.Vel.Size() * 0.5396713;
			if (S0 > 71.93751)
			{
				S0 = FMath::Min(S0, 608.3426);
				const FVector N = In.GroundNormal;
				const FVector Vt = Tangent(In.Vel, N);
				const FVector F = Unit(Vt, In.Fwd);
				const FVector Lp = Unit(Tangent(In.BoardLong, N), F);
				FVector Side = FVector::CrossProduct(F, N);
				if (FVector::DotProduct(Lp, Side) < 0.0) { Side = -Side; }
				FSplashSlot& S = Push();
				S.Light = LightRgb(In);
				S.Life = (S0 * 0.0013150485 + 0.2) * 0.9949894;
				S.Centre = In.Contact;
				S.CV = Vt;
				LayoutFan(S, S0, In.SurfWidth * 0.4, N, F, Side, Lp, U);
				BrakeTimer += (1.0 - S0 * 0.0009862864) * 0.25 + U() * 0.08;
			}
		}
		for (int32 J = 0; J < Count; ++J)
		{
			FSplashSlot& S = Ring[(Head + J) % Slots];
			S.Age += Dt;
			if (S.Age > MaxLife)
			{
				Count = J;
				break;
			}
			S.CV *= 0.98197895;
			S.Centre += S.CV * Dt;
		}
	}

	FVector FSplashFx::Point(const FSplashSlot& Slot, int32 I, double S)
	{
		const double K = 1.0 - FMath::Exp(-1.25 * Slot.Age);
		return Slot.Centre + Slot.P[I] + Slot.V[I] * (S * K) + FVector(0.0, 0.0, -55.555557 * S * (0.2 * S + 0.8) * Slot.Age);
	}

	FColor FSplashFx::Colour(const FVector& L)
	{
		const double M = (L.X + L.Y + L.Z) * 0.33333334;
		auto Ch = [M](double C) { return Byte(FMath::Clamp(C + (M - C) * 0.75041556 + 0.5286169, 0.0, 1.9941003) * 128.0); };
		return FColor(Ch(L.X), Ch(L.Y), Ch(L.Z), 0);
	}

	bool FSplashFx::Strips(const FSplashSlot& Slot, TArray<FFxVertex> Out[4])
	{
		if (Slot.Age > Slot.Life || Slot.N <= 0)
		{
			return false;
		}
		const FColor C = Colour(Slot.Light);
		FVector Prev[FSplashSlot::MaxPoints];
		for (int32 I = 0; I < Slot.N; ++I) { Prev[I] = Point(Slot, I, 0.0); }
		double VPrev = 0.0;   // quirk: band 1's root row uses V = 0
		for (int32 Row = 1; Row <= 4; ++Row)
		{
			const double S = Row * 0.25;
			const double VCur = 1.0 - S;
			TArray<FFxVertex>& Strip = Out[Row - 1];
			Strip.Reset();
			for (int32 I = 0; I < Slot.N; ++I)
			{
				const uint8 A = Byte((Slot.Life - Slot.Age) * (Slot.W[I] * 128.0 / MaxLife));
				const FVector Cur = Point(Slot, I, S);
				FFxVertex V0, V1;
				V0.Pos = Prev[I]; V0.UV = FVector2D(0.75 * I, VPrev); V0.Color = FColor(C.R, C.G, C.B, Row == 1 ? 0 : A);
				V1.Pos = Cur; V1.UV = FVector2D(0.75 * I, VCur); V1.Color = FColor(C.R, C.G, C.B, A);
				Strip.Add(V0);
				Strip.Add(V1);
				Prev[I] = Cur;
			}
			VPrev = VCur;
		}
		return true;
	}

	FPuffSprite PuffSprite(const FPuff& P)
	{
		FPuffSprite S;
		double Size;
		if (P.Age < 0.2)
		{
			S.Alpha = P.Age * P.Scale / 0.2;
			Size = P.Age * 120.0 / 0.2;
		}
		else
		{
			const double F = (2.0 - P.Age) * 0.5555556;
			S.Alpha = F * (F * 0.4 + 0.6) * P.Scale;
			Size = (P.Age - 0.2) * 130.0 / 1.8 + 120.0;
		}
		S.Half = Size;
		S.Pos = P.Pos + P.N * (Size * 0.9);
		S.Image = 24 + P.Variant;
		return S;
	}

	void FPuffFx::Spawn(double Scale, const FVector& P, const FVector& N, const FVector& V, const FRiderFxInput& In, FRandU U,
	                    FRandBits Bits)
	{
		if (Puffs.Num() >= PuffRing)
		{
			Puffs.RemoveAt(0);
		}
		FPuff& Pf = Puffs.AddDefaulted_GetRef();
		Pf.FlipU = int32(Bits() & 1);
		Pf.FlipV = int32(Bits() & 1);
		Pf.Pos = P + Tangent(V, N) * (U() * 0.1 + 0.1);
		Pf.N = N;
		Pf.Variant = int32(Bits() & 3);
		Pf.Scale = Scale;
		const FVector C = DesatToMean(LightRgb(In), 0.5);
		Pf.Rgb = FVector(FMath::Clamp(C.X, 0.0, 1.0), FMath::Clamp(C.Y, 0.0, 1.0), FMath::Clamp(C.Z, 0.0, 1.0));
		Acc += 1.0;
	}

	int32 FPuffFx::Land(double Vn, const FRiderFxInput& In, FRandU U, FRandBits Bits)
	{
		Splash.Land(Vn, In, U);
		if (In.ContactKind == 2)
		{
			return 0;
		}
		const int32 N = LandingPuffCount(Vn);
		for (int32 I = 0; I < N; ++I)
		{
			FVector P = In.Position;
			P += In.ContactForward * (U() * 80.0 - 40.0);
			P += In.ContactLeft * (U() * 80.0 - 40.0);
			Spawn(0.1, P, In.GroundNormal, In.Vel, In, U, Bits);
		}
		Intensity = FMath::Min(Intensity + LandingIntensityAdd(Vn), 1.5);
		Acc = 1.0;
		return N;
	}

	void FPuffFx::Impact(const FVector& C, const FVector& V, const FVector& N, const FRiderFxInput& In, FRandU U, FRandBits Bits)
	{
		Splash.Impact(C, V, N, In, U);
		if (In.ContactKind == 2)
		{
			return;
		}
		if (Acc < 0.7)
		{
			Spawn(0.1, C, N, V, In, U, Bits);
		}
		Intensity = FMath::Min(Intensity + 0.5, 2.0);
	}

	void FPuffFx::Frame(const FRiderFxInput& In, FRandU U, FRandBits Bits)
	{
		Splash.Frame(In, U);
		if (In.ContactKind != 2 && FMath::Abs(In.Brake) > 0.7 && In.Motion == MotionGround && In.Vel.Size() > 277.77777)
		{
			Intensity = FMath::Min(Intensity + 0.05, 1.0);
		}
		if (Intensity > 0.0)
		{
			if (Acc < Intensity * 0.1 + 0.5)
			{
				Spawn(Intensity * 0.1 * 0.5, In.Contact, In.GroundNormal, In.Vel, In, U, Bits);
			}
			Intensity -= Dt;
		}
		Acc *= 0.6666666;
		for (FPuff& P : Puffs) { P.Age += Dt; }
		int32 Dead = 0;
		while (Dead < Puffs.Num() && Puffs[Dead].Age > PuffLife) { ++Dead; }
		if (Dead) { Puffs.RemoveAt(0, Dead); }
	}

	void FIceGlints::Frame(const FRiderFxInput& In, FRandU U, FRandBits Bits)
	{
		for (FGlint& G : Glints) { G.Age += Dt; }
		int32 Dead = 0;
		while (Dead < Glints.Num() && Glints[Dead].Age > 1.0) { ++Dead; }
		if (Dead) { Glints.RemoveAt(0, Dead); }
		const FVector C = DesatToMean(LightRgb(In), 0.5);
		Rgb = FVector(FMath::Clamp(C.X, 0.0, 0.7), FMath::Clamp(C.Y, 0.0, 0.7), FMath::Clamp(C.Z, 0.0, 0.7));
		P = In.Motion == MotionGround ? In.Contact : In.Position;
		N = In.GroundNormal;
		F = Unit(Tangent(In.Vel, N), F);
		S = FVector::CrossProduct(F, N);
		if (In.ContactKind == 2 || In.Motion != MotionGround || !(In.Surface == 3 || In.Surface == 4))
		{
			return;
		}
		const double Speed = In.Vel.Size();
		const int32 Num = FMath::Max(1, int32(Speed / 1270.6951 + 0.5));
		for (int32 I = 0; I < Num; ++I)
		{
			if (Glints.Num() >= Ring) { Glints.RemoveAt(0); }
			FGlint& G = Glints.AddDefaulted_GetRef();
			G.FlipU = int32(Bits() & 1);
			G.FlipV = int32(Bits() & 1);
			G.Df = U() * 88.33769 - 44.168846;
			G.Ds = U() * 88.33769 - 44.168846;
			G.Variant = int32(Bits() & 3);
			G.Life = (U() * 0.40000004 + 0.8) * 0.44652596;
			if (Speed / 972.22217 <= 1.0) { G.Life *= Speed / 972.22217; }
			G.Size0 = (U() * 0.40000004 + 0.8) * 18.493296;
			G.Size1 = (U() * 0.40000004 + 0.8) * 113.950874;
		}
	}

	bool FIceGlints::Sprite(const FGlint& G, FVector& OutPos, double& OutHalf, double& OutAlpha) const
	{
		if (G.Age > G.Life || G.Life <= 0.0)
		{
			return false;
		}
		OutHalf = FMath::Max(G.Size0, G.Size1 / G.Life * G.Age);
		OutAlpha = (G.Life - G.Age) * Intensity / G.Life;
		OutPos = P + F * (G.Df - G.Age * 200.0) + S * G.Ds + N * (OutHalf * 0.5);
		return true;
	}

	double FCarveCurtain::SegmentU(int32 Kc)
	{
		const double X = Kc * 0.25;
		return X - (double(int32(X * 0.015625)) * 64.0 + 32.0);
	}

	void FCarveCurtain::Push(const FVector4& Light, bool bStart, FRandU U)
	{
		Jit += U() * 0.26666668 - 0.13333334;
		if (Jit > 0.3) { Jit = 0.6 - Jit; }
		if (Jit < -0.3) { Jit = -0.6 - Jit; }
		if (Count < Segments) { ++Count; }
		Head = (Head + Segments - 1) % Segments;
		FCurtainSeg& S = Ring[Head];
		S = FCurtainSeg();
		const FVector Cl = DesatToMean(FVector(Light.Y, Light.Z, Light.W), 0.8);
		S.A = Light.X; S.R = Cl.X; S.G = Cl.Y; S.B = Cl.Z;
		S.bStart = bStart;
		S.U = SegmentU(K++);
	}

	void FCarveCurtain::Place(double Cv, double Width, const FVector& P, const FVector& L, const FVector& Dir, double Speed)
	{
		FCurtainSeg& S = Ring[Head];
		S.Age = 0.0;
		S.A = FMath::Clamp(Width * (Jit * 1.2 + 1.0), 0.0, 1.0);
		S.R = FMath::Clamp(S.R, 0.0, 1.0);
		S.G = FMath::Clamp(S.G, 0.0, 1.0);
		S.B = FMath::Clamp(S.B, 0.0, 1.0);
		const FVector Base = P - L * 50.0;
		for (int32 I = 0; I < 4; ++I)
		{
			S.V[I] = Dir * (I * 0.25 * Cv * (Jit + 1.0));
			S.P[I] = Speed > 0.0 ? Base + S.V[I] * (100.0 / Speed) : Base;
		}
		Anchor = P + L * 50.0;
	}

	void FCarveCurtain::Frame(const FRiderFxInput& In, FRandU U)
	{
		for (int32 Jn = 0; Jn < Count; ++Jn)
		{
			FCurtainSeg& S = Ring[(Head + Jn) % Segments];
			S.Age += Dt;
			if (S.Age > Life)
			{
				Count = Jn;
				break;
			}
			for (int32 I = 0; I < 4; ++I)
			{
				const double Q = I * 0.25;
				const FVector Acc = S.V[I] * -3.5 + FVector(0.0, 0.0, Q * -2800.0 * Q);
				S.P[I] += S.V[I] * Dt;
				S.V[I] += Acc * Dt;
			}
		}
		double CRaw = In.Steer * (1.0 - FMath::Abs(In.Brake));
		if (In.bGoofy) { CRaw = -CRaw; }
		const FVector L = In.bGoofy ? -In.BoardLong : In.BoardLong;
		const FVector N = In.GroundNormal;
		const FVector X = FVector::CrossProduct(In.BoardLong, N);
		const double Off = CRaw <= 0.0 ? -In.SurfTilt : In.SurfTilt;
		const FVector DRaw = Unit(X + N * Off, N);
		const double Speed = In.Vel.Size();
		const FVector P = In.Contact;
		const double WRaw = FMath::Max(In.SurfWidth * (FMath::Abs(CRaw) - 0.2), 0.0);
		const double Ct = In.SurfSpeed * Speed * CRaw;
		const bool bGround = In.Motion == MotionGround && In.ContactKind != 2;
		if (!bActive)
		{
			if (bGround && FMath::Abs(CRaw) > 0.3)
			{
				Dir0 = DRaw;
				Carve = 0.0;
				Wd = WRaw;
				K = 0;
				Push(In.LightTotal, true, U);
				Place(0.0, 0.0, P, L, Dir0, Speed);
				Last = P;
				Push(In.LightTotal, false, U);
				Place(Carve, Wd, P, L, Dir0, Speed);
				bActive = true;
			}
			return;
		}
		if (Carve == 0.0 || (Ct < 0.0) == (Carve < 0.0))
		{
			Carve = Ct * 0.033333335 + Carve * 0.96666664;
			Dir0 = Unit(DRaw / 6.0 + Dir0 * 0.8333333, Dir0);
			Wd = (bGround ? WRaw : 0.0) * 0.33333334 + Wd * 0.6666666;
			if (Wd > 0.02)
			{
				if (FVector::Dist(P, Last) > 25.0)
				{
					Push(In.LightTotal, false, U);
					Last = P;
				}
				Place(Carve, Wd, P, L, Dir0, Speed);
			}
			else
			{
				bActive = false;
				Place(Carve, 0.0, P, L, Dir0, Speed);
			}
		}
		else
		{
			bActive = false;
			Place(Jit * Carve + Carve, 0.0, P, L, Dir0, Speed);
		}
	}

	void FCarveCurtain::Strips(TArray<TArray<FFxVertex>>& Out) const
	{
		if (Count == 0)
		{
			return;
		}
		auto Col = [](const FCurtainSeg& S)
		{
			return FColor(Byte(S.R * 128.0), Byte(S.G * 128.0), Byte(S.B * 128.0), Byte(S.A * (0.9 - S.Age) * 1.1111112 * 128.0));
		};
		{
			const FCurtainSeg& S = Get(0);
			const FColor C0 = Col(S);
			TArray<FFxVertex>& Fan = Out.AddDefaulted_GetRef();
			for (int32 I = 0; I < 4; ++I)
			{
				FFxVertex A, B;
				A.Pos = Anchor; A.UV = FVector2D(S.U + 1.0, 0.5); A.Color = FColor(C0.R, C0.G, C0.B, 0);
				B.Pos = S.P[I]; B.UV = FVector2D(S.U, 0.98 - 0.32 * I); B.Color = C0;
				Fan.Add(A);
				Fan.Add(B);
			}
		}
		for (int32 Row = 1; Row <= 3; ++Row)
		{
			TArray<FFxVertex> Strip;
			for (int32 Jn = 0; Jn < Count - 1; ++Jn)
			{
				const FCurtainSeg& S = Get(Jn);
				const FColor C0 = Col(S);
				FFxVertex A, B;
				A.Pos = S.P[Row - 1]; A.UV = FVector2D(S.U, 0.98 - 0.32 * (Row - 1)); A.Color = C0;
				B.Pos = S.P[Row]; B.UV = FVector2D(S.U, 0.98 - 0.32 * Row); B.Color = C0;
				Strip.Add(A);
				Strip.Add(B);
				if (S.bStart)
				{
					if (Strip.Num() >= 3) { Out.Add(MoveTemp(Strip)); }
					Strip.Reset();
				}
			}
			if (Strip.Num() >= 3) { Out.Add(MoveTemp(Strip)); }
		}
	}

	const FVector FSpinRibbons::Tint = FVector(0.8, 0.8, 0.9);

	void BoardCorners(const FRiderFxInput& In, FVector Out[4])
	{
		static const double Ext[3][3] = { { 96.09368, 98.02858, 16.22651 }, { 84.922966, 84.07263, 16.232784 }, { 114.15547, 124.23839, 15.998993 } };
		const int32 Cls = FMath::Clamp(In.BoardClass, 0, 2);
		double Front = Ext[Cls][0], Back = Ext[Cls][1];
		if (In.ProfileStance != 0) { Swap(Front, Back); }
		const FVector F = In.BoardLong * (Front * In.SizeScale), B = In.BoardLong * (Back * In.SizeScale);
		const FVector A = In.BoardAcross * (Ext[Cls][2] * In.SizeScale);
		Out[0] = In.BoardOrigin + F + A;
		Out[1] = In.BoardOrigin + F - A;
		Out[2] = In.BoardOrigin - B + A;
		Out[3] = In.BoardOrigin - B - A;
	}

	void LiveBoardCorners(const FRiderFxInput& In, FVector Out[4])
	{
		FRiderFxInput L = In;
		L.BoardLong = In.LiveBoardLong;
		L.BoardAcross = In.LiveBoardAcross;
		L.BoardOrigin = In.LiveBoardOrigin;
		BoardCorners(L, Out);
	}

	void FSpinRibbons::Frame(bool bSpin, const FSpinSample& Sample)
	{
		if (bSpin)
		{
			Samples[Head] = Sample;
			Head = (Head + 1) % RingSize;
			Count = FMath::Min(Count + 1, RingSize);
		}
		else if (Count > 0)
		{
			--Count;
		}
	}
}
