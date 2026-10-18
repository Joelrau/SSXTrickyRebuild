#include "Misc/AutomationTest.h"
#include "Particles/SSXSnowFx.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXParticles;

namespace
{
	constexpr EAutomationTestFlags SnowTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FSeq
	{
		int32 I = 0;
		double operator()() { static const double V[10] = { 0.1, 0.9, 0.5, 0.3, 0.7, 0.2, 0.8, 0.4, 0.6, 0.05 }; return V[I++ % 10]; }
	};

	FRiderFxInput GroundInput()
	{
		FRiderFxInput In;
		In.Motion = MotionGround;
		In.ContactKind = 0;
		In.Surface = 1;
		In.GroundNormal = FVector(0, 0, 1);
		In.Vel = FVector(1500, 0, 0);
		In.Fwd = FVector(1, 0, 0);
		In.Right = FVector(0, -1, 0);
		In.BoardLong = FVector(1, 0, 0);
		In.BoardAcross = FVector(0, 1, 0);
		In.LightTotal = FVector4(1.0, 1.0, 0.8, 0.6);
		In.SurfWidth = 0.251414;
		In.SurfTilt = 0.4666098;
		In.SurfSpeed = 1.6606796;
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSnowFxTest, "SSX.Particles.SnowFx", SnowTestFlags)
bool FSSXSnowFxTest::RunTest(const FString& Parameters)
{
	{
		FRiderFxInput In;
		In.LightTotal = FVector4(1.0, 1.0, 0.5, 0.2);
		FPuffFx Fx;
		const uint32 BitSeq[3] = { 1, 0, 2 };
		int32 Bi = 0;
		Fx.Spawn(0.1, FVector::ZeroVector, FVector(0, 0, 1), FVector(1000, 0, -300), In, []() { return 0.5; }, [&]() { return BitSeq[Bi++]; });
		const FPuff P = Fx.Get()[0];
		TestTrue(TEXT("puff pushed ahead along the tangent velocity"), P.Pos.Equals(FVector(150, 0, 0), 1e-9));
		TestTrue(TEXT("puff rgb = light half desaturated"), P.Rgb.Equals(FVector(0.78333333, 0.53333333, 0.38333333), 1e-6));
		TestTrue(TEXT("puff flips / variant"), P.FlipU == 1 && P.FlipV == 0 && P.Variant == 2);
		const double Ages[4] = { 0.1, 0.2, 1.0, 1.9 };
		const double Want[4][4] = { { 54.0, 60.0, 0.05, 26 }, { 108.0, 120.0, 0.1000000112, 26 }, { 160.0, 177.7777778, 0.045679017, 26 }, { 218.5, 242.7777778, 0.0034567904, 26 } };
		for (int32 K = 0; K < 4; ++K)
		{
			FPuff Q = P;
			Q.Age = Ages[K];
			const FPuffSprite S = PuffSprite(Q);
			TestTrue(FString::Printf(TEXT("puff sprite at %.1f s"), Ages[K]), FMath::IsNearlyEqual(S.Pos.Z, Want[K][0], 1e-5)
				&& FMath::IsNearlyEqual(S.Half, Want[K][1], 1e-5) && FMath::IsNearlyEqual(S.Alpha, Want[K][2], 1e-7) && S.Image == int32(Want[K][3]));
		}
	}

	{
		FSeq U;
		FSplashSlot S;
		FSplashFx::LayoutRing(S, 300.0, 0.5, 0.2, FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, -1, 0), [&]() { return U(); });
		TestEqual(TEXT("ring points"), S.N, 25);
		TestTrue(TEXT("ring v0 (forward, 1 + asym)"), S.V[0].Equals(FVector(378.0, 0.0, 151.2), 1e-6));
		TestTrue(TEXT("ring v12 (back, 1 - asym)"), S.V[12].Equals(FVector(-169.5, 0.0, 67.8), 1e-6));
		TestTrue(TEXT("ring closed"), S.V[24].Equals(S.V[0], 0.0));
		S.Age = 0.3;
		S.Life = 0.75;
		S.Light = FVector(1.0, 0.8, 0.6);
		TestTrue(TEXT("splash point (0, s = 1)"), FSplashFx::Point(S, 0, 1.0).Equals(FVector(118.20465261701251, 0.0, 30.615193946805007), 1e-6));
		const FColor C = FSplashFx::Colour(S.Light);
		TestTrue(TEXT("splash colour"), C.R == 176 && C.G == 170 && C.B == 163);
		TArray<FFxVertex> Bands[4];
		TestTrue(TEXT("strips while alive"), FSplashFx::Strips(S, Bands));
		TestEqual(TEXT("2N vertices per band"), Bands[0].Num(), 50);
		TestTrue(TEXT("band 1 root vertex: V 0, alpha 0"), Bands[0][0].UV.Equals(FVector2D(0, 0)) && Bands[0][0].Color.A == 0 && Bands[0][0].Pos.IsZero());
		TestTrue(TEXT("band 1 row vertex"), Bands[0][1].UV.Equals(FVector2D(0, 0.75)) && Bands[0][1].Color.A == 9
			&& Bands[0][1].Pos.Equals(FVector(29.55116315425313, 0.0, 8.27879850295125), 1e-6));
		TestTrue(TEXT("band 4 tip"), Bands[3][1].UV.Equals(FVector2D(0, 0)) && Bands[3][1].Color.A == 9 && Bands[3][1].Pos.Equals(FVector(118.20465261701251, 0, 30.615193946805007), 1e-6));
		S.Age = 0.8;
		TestFalse(TEXT("no strips past life"), FSplashFx::Strips(S, Bands));
	}

	{
		FRiderFxInput In = GroundInput();
		FPuffFx Fx;
		auto U = []() { return 0.5; };
		auto Bits = []() { return 0u; };
		TestEqual(TEXT("landing puffs (f = -2000)"), Fx.Land(-2000.0, In, U, Bits), 2);
		TestEqual(TEXT("landing splash (s = 562 > 154)"), Fx.GetSplash().Num(), 1);
		TestTrue(TEXT("landing splash life"), FMath::IsNearlyEqual(Fx.GetSplash().Get(0).Life, (490.5319 * 0.0016308827 + 0.2) * 0.9949894, 1e-9));
		Fx.Impact(FVector(100, 0, 0), In.Vel, FVector(0, 0, 1), In, U, Bits);
		TestEqual(TEXT("impact after landing: no puff (acc 1 >= 0.7)"), Fx.Num(), 2);
		TestEqual(TEXT("impact splash added"), Fx.GetSplash().Num(), 2);
		Fx.Impact(FVector(100, 0, 0), In.Vel, FVector(0, 0, 1), In, U, Bits);
		TestEqual(TEXT("impact at the same point: no splash"), Fx.GetSplash().Num(), 2);
		FPuffFx Small;
		Small.Land(-300.0, In, U, Bits);
		TestEqual(TEXT("soft landing: no splash (s <= 154.46)"), Small.GetSplash().Num(), 0);
		FRiderFxInput Wall = In;
		Wall.SurfWidth = 0.0;
		FPuffFx NoSnow;
		NoSnow.Impact(FVector::ZeroVector, In.Vel, FVector(0, 0, 1), Wall, U, Bits);
		TestEqual(TEXT("surface without snow width: no splash"), NoSnow.GetSplash().Num(), 0);

		FSplashFx Brake;
		In.Brake = 0.8;
		In.Vel = FVector(1000, 0, 0);
		Brake.Frame(In, U);
		TestTrue(TEXT("brake fan: 12 points"), Brake.Num() == 1 && Brake.Get(0).N == 12);
		Brake.Frame(In, U);
		TestEqual(TEXT("brake fan waits for its timer"), Brake.Num(), 1);
		In.Brake = 0.0;
		for (int32 F = 0; F < 70; ++F) { Brake.Frame(In, U); }
		TestEqual(TEXT("splash slots die after 0.995 s"), Brake.Num(), 0);
	}

	{
		FIceGlints G;
		G.P = FVector::ZeroVector; G.F = FVector(1, 0, 0); G.S = FVector(0, -1, 0); G.N = FVector(0, 0, 1);
		FGlint E;
		E.Age = 0.1; E.Life = 0.4; E.Size0 = 18.0; E.Size1 = 110.0; E.Df = 10.0; E.Ds = -5.0; E.Variant = 1;
		FVector Pos;
		double Half, Alpha;
		TestTrue(TEXT("glint sprite"), G.Sprite(E, Pos, Half, Alpha) && Pos.Equals(FVector(-10, 5, 13.75), 1e-9)
			&& FMath::IsNearlyEqual(Half, 27.5, 1e-9) && FMath::IsNearlyEqual(Alpha, 0.1274766, 1e-7));
		FRiderFxInput In = GroundInput();
		G.Frame(In, []() { return 0.5; }, []() { return 0u; });
		TestEqual(TEXT("no glints on snow"), G.Num(), 0);
		In.Surface = 3;
		In.Vel = FVector(2600, 0, 0);
		G.Frame(In, []() { return 0.5; }, []() { return 0u; });
		TestEqual(TEXT("ice: round(|v| / 1270.7) glints"), G.Num(), 2);
	}

	{
		const double WantU[5] = { -32.0, -31.75, 31.75, -32.0, -21.0 };
		const int32 Ks[5] = { 0, 1, 255, 256, 300 };
		for (int32 I = 0; I < 5; ++I) { TestTrue(TEXT("segment U"), FMath::IsNearlyEqual(FCarveCurtain::SegmentU(Ks[I]), WantU[I], 1e-9)); }
		FRiderFxInput In = GroundInput();
		In.Steer = 0.8;
		FCarveCurtain Cu;
		auto U = []() { return 0.5; };
		Cu.Frame(In, U);
		TestTrue(TEXT("curtain starts: start cap + live segment"), Cu.IsActive() && Cu.Num() == 2 && Cu.Get(1).bStart && !Cu.Get(0).bStart);
		TestTrue(TEXT("anchor 50 cm ahead"), Cu.GetAnchor().Equals(FVector(50, 0, 0), 1e-9));
		TestTrue(TEXT("width alpha"), FMath::IsNearlyEqual(Cu.Get(0).A, 0.251414 * 0.6, 1e-6));
		Cu.Frame(In, U);
		const double Ct = 1.6606796 * 1500.0 * 0.8;
		const double C = Ct * 0.033333335;
		const FVector D = FVector(0, -1, 0.4666098).GetSafeNormal();
		TestTrue(TEXT("point 3 = tail + D (3/4) c 100 / |v|"), Cu.Get(0).P[3].Equals(FVector(-50, 0, 0) + D * (0.75 * C * 100.0 / 1500.0), 1e-4));
		TArray<TArray<FFxVertex>> Strips;
		Cu.Strips(Strips);
		TestTrue(TEXT("nose fan only (bands of one pair are dropped)"), Strips.Num() == 1 && Strips[0].Num() == 8 && Strips[0][0].Color.A == 0);
		In.Steer = -0.8;
		Cu.Frame(In, U);
		TestFalse(TEXT("carve sign flip ends the curtain"), Cu.IsActive());
	}

	{
		FRiderFxInput In;
		In.BoardLong = FVector(1, 0, 0);
		In.BoardAcross = FVector(0, 1, 0);
		In.BoardOrigin = FVector::ZeroVector;
		In.BoardClass = 0;
		In.ProfileStance = 1;
		In.SizeScale = 0.95;
		FVector P[4];
		BoardCorners(In, P);
		TestTrue(TEXT("board corners (class 0, swapped, RAM-checked)"), P[0].Equals(FVector(93.127151, 15.4151845, 0), 1e-4)
			&& P[1].Equals(FVector(93.127151, -15.4151845, 0), 1e-4) && P[2].Equals(FVector(-91.288996, 15.4151845, 0), 1e-4)
			&& P[3].Equals(FVector(-91.288996, -15.4151845, 0), 1e-4));
		FSpinRibbons R;
		FSpinSample S;
		R.Frame(true, S);
		TestFalse(TEXT("one sample: not drawn"), R.IsDrawn());
		R.Frame(true, S);
		R.Frame(true, S);
		R.Frame(false, S);
		TestTrue(TEXT("count falls by one per frame out of Spin"), R.GetCount() == 2 && R.IsDrawn());
		In.LiveBoardLong = FVector(0, 1, 0);
		In.LiveBoardAcross = FVector(-1, 0, 0);
		In.LiveBoardOrigin = FVector(0, 0, 10);
		FVector L[4];
		LiveBoardCorners(In, L);
		TestTrue(TEXT("live corners from LiveBoard* (this pose), update corners from Board*"), L[0].Equals(FVector(-15.4151845, 93.127151, 10), 1e-4)
			&& P[0].Equals(FVector(93.127151, 15.4151845, 0), 1e-4));
		FSpinSample Prev;
		BoardCorners(In, Prev.P);
		FSpinRibbons R2;
		R2.Frame(true, Prev);
		TestTrue(TEXT("spin ring stores the update's (previous-pose) sample"), R2.Get(0).P[0].Equals(P[0], 1e-6));
	}
	return true;
}

#endif
