// 0x134288 / 0x1343D0 / 0x134830
#include "Misc/AutomationTest.h"
#include "Particles/SSXAngerMarker.h"
#include "Race/RaceRules.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXParticles;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAngerMarkerTest, "SSX.Particles.AngerMarker.Marker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAngerMarkerTest::RunTest(const FString&)
{
	auto Near = [](double A, double B, double Tol = 1e-4) { return FMath::Abs(A - B) <= Tol; };
	FAngerMarkerInput In;
	In.bShown = true;
	In.Human = 0;
	In.Level = 70;
	In.Bone4 = FVector(100, 200, 300);
	In.Up = FVector(0, 0, 1);
	FAngerSprite S;

	{
		FAngerMarker M;
		FAngerMarkerInput Off = In;
		Off.bShown = false;
		Off.bGrudge = true;
		M.Frame(Off);
		TestFalse(TEXT("not shown outside modes 2 / 4, phase 4, for humans or in a replay"), M.Sprite(1000.0, S));
		M.Frame(In);
		TestFalse(TEXT("calm rider: no marker"), M.Sprite(1000.0, S));
		TestTrue(TEXT("lift 95.9 cm along the up row above bone 4"), Near(M.GetPos().Z, 300.0 + 95.90865325927734, 1e-3));
	}
	{
		FAngerMarker M;
		M.Frame(In);
		In.Anger = 9;
		M.Frame(In);
		TestTrue(TEXT("flash starts on the rise"), M.IsFlashing() && M.Sprite(1000.0, S));
		TestTrue(TEXT("flash frame 0 alpha 0"), Near(S.Argb.X, 0.0));
		M.Frame(In);
		TestTrue(TEXT("flash frame 1"), M.Sprite(1000.0, S));
		const double Fade = 0.016652344 / 0.1;
		TestTrue(TEXT("flash alpha = fade x 0.77786"), Near(S.Argb.X, Fade * 0.77786));
		const double Mood = 1.0 + 10.0 / 20.989258;
		TestTrue(TEXT("mood colour R"), Near(S.Argb.Y, 1.99));
		TestTrue(TEXT("mood colour G"), Near(S.Argb.Z, 1.99 + (1.0 - 1.99) * (Mood - 1.0), 1e-3));
		TestTrue(TEXT("mood colour B"), Near(S.Argb.W, 0.0));
		TestTrue(TEXT("flash size 33.2 at 10 m"), Near(S.HalfX, 33.20431) && Near(S.HalfY, 33.20431));
		int32 Frames = 2;
		while (M.IsFlashing() && Frames < 200) { M.Frame(In); ++Frames; }
		TestEqual(TEXT("flash lasts 62 frames (61 steps of 0.016652 then cleared)"), Frames, 63);
		TestFalse(TEXT("gone after the flash"), M.Sprite(1000.0, S));
	}
	{
		FAngerMarker M;
		FAngerMarkerInput G = In;
		G.Anger = 0;
		G.bGrudge = true;
		G.GrudgeRival = 0;
		G.GrudgeKind = 2;
		M.Frame(G);
		TestTrue(TEXT("pop starts"), M.IsPopping() && M.IsTargeting() && M.Sprite(1000.0, S));
		TestTrue(TEXT("pop frame 0: narrow (0.2 x 0.3)"), Near(S.HalfX, 33.20431 * 0.2) && Near(S.HalfY, 33.20431 * 0.3));
		TestTrue(TEXT("kind 2 = red, key alpha"), Near(S.Argb.Y, 1.99) && Near(S.Argb.Z, 0.0) && Near(S.Argb.W, 0.0) && Near(S.Argb.X, 0.33327362));
		for (int32 K = 0; K < 30; ++K) { M.Frame(G); }
		TestTrue(TEXT("pop mid"), M.Sprite(1000.0, S));
		const double T = 30 * 0.016386366, U = (T - 1.0 / 3.0) * 3.0;
		TestTrue(TEXT("pop middle third shape"), Near(S.HalfX / 33.20431, U * 1.3982834 + 0.2, 1e-3) && Near(S.HalfY / 33.20431, U * -0.9675763 + 1.401849, 1e-3));
		while (M.IsPopping()) { M.Frame(G); }
		M.Frame(G);
		TestTrue(TEXT("pulse after the pop"), M.Sprite(1000.0, S));
		TestTrue(TEXT("pulse: square, 1 .. 1.34"), Near(S.HalfX, S.HalfY) && S.HalfX >= 33.20431 - 1e-3 && S.HalfX < 33.20431 * 1.35);
		TestTrue(TEXT("pulse alpha above the key"), S.Argb.X >= 0.3332 && S.Argb.X < 0.7779);
		FAngerMarkerInput Off = G;
		Off.bGrudge = false;
		M.Frame(Off);
		TestFalse(TEXT("grudge over: gone"), M.Sprite(1000.0, S));
		M.Frame(G);
		TestFalse(TEXT("same kind again: no pop"), M.IsPopping());
		FAngerMarkerInput Other = G;
		Other.GrudgeRival = 3;
		M.Frame(Other);
		TestFalse(TEXT("grudge on another racer: not shown"), M.Sprite(1000.0, S));
		FAngerMarkerInput Y = G;
		Y.GrudgeKind = 1;
		M.Frame(Y);
		TestTrue(TEXT("new kind pops"), M.IsPopping() && M.Sprite(1000.0, S));
		TestTrue(TEXT("kind 1 = yellow"), Near(S.Argb.Y, 1.99) && Near(S.Argb.Z, 1.99) && Near(S.Argb.W, 0.0));
	}
	{
		FAngerMarker M;
		FAngerMarkerInput G = In;
		G.bGrudge = true;
		G.GrudgeRival = 0;
		G.GrudgeKind = 0;
		M.Frame(G);
		while (M.IsPopping()) { M.Frame(G); }
		M.Frame(G);
		TestTrue(TEXT("near"), M.Sprite(250.0, S));
		const double Pulse = S.HalfX / (0.5 * 33.20431);
		TestTrue(TEXT("kind 0 = white"), Near(S.Argb.Y, 1.0) && Near(S.Argb.Z, 1.0) && Near(S.Argb.W, 1.0));
		TestTrue(TEXT("far"), M.Sprite(3200.0, S));
		TestTrue(TEXT("beyond 30 m: z / 3000 x 33.2"), Near(S.HalfX, 3200.0 / 3000.0 * 33.20431 * Pulse, 1e-3));
		const double A = S.Argb.X;
		TestTrue(TEXT("fade"), A > 0.0 && A < 0.78 * (1.0 - (3200.0 - 2777.401) / 922.6775) + 1e-3);
		TestFalse(TEXT("beyond 37 m: culled"), M.Sprite(3800.0, S));
		TestFalse(TEXT("behind the camera: culled"), M.Sprite(-10.0, S));
	}
	// 0x1343D0 / 0x129628
	{
		FPrevRow Row;
		const FVector U0(0, 0, 1), U1 = FVector(0.3, 0, 1).GetSafeNormal(), U2 = FVector(0, -0.5, 1).GetSafeNormal();
		TestTrue(TEXT("first frame: no previous row, this one"), Row.Read(10, U0).Equals(U0));
		TestTrue(TEXT("next sim frame: the previous frame's row"), Row.Read(11, U1).Equals(U0));
		TestTrue(TEXT("read twice in one sim frame: the same"), Row.Read(11, U2).Equals(U0));
		TestTrue(TEXT("frame after: the row seen at frame 11"), Row.Read(12, U2).Equals(U1));
		TestTrue(TEXT("a skipped sim frame: no exact previous, this frame's"), Row.Read(14, U0).Equals(U0));
		TestTrue(TEXT("and the next one exact again"), Row.Read(15, U1).Equals(U0));
		FAngerMarker M;
		FAngerMarkerInput G = In;
		G.bGrudge = true;
		G.GrudgeRival = 0;
		FPrevRow Up;
		Up.Read(20, U0);
		G.Up = Up.Read(21, U1);
		M.Frame(G);
		TestTrue(TEXT("point = bone 4 + previous up x 95.9"), M.GetPos().Equals(In.Bone4 + U0 * FAngerMarker::Lift, 1e-3));
		G.Up = Up.Read(22, U2);
		M.Frame(G);
		TestTrue(TEXT("one frame later: along U1"), M.GetPos().Equals(In.Bone4 + U1 * FAngerMarker::Lift, 1e-3));
	}
	return true;
}

// 0x116AB8 / 0x11ECF8 / 0x11EF00 / 0x11EFD8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAngerMarkerGrudgeLifetimeTest, "SSX.Particles.AngerMarker.GrudgeLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAngerMarkerGrudgeLifetimeTest::RunTest(const FString&)
{
	using namespace SSXRace;
	constexpr int32 Human = 0, Ai = 1;
	FGrudge AiG, HumanG;
	FRivalry AiRiv[MaxRacers], HumanRiv[MaxRacers];
	AiRiv[Human].Level = 70;
	AiRiv[Human].AngerLimit = 30;
	FAngerMarker M;
	FAngerSprite S;
	int32 Frame = 100;
	auto Step = [&](bool bShown = true)
	{
		GrudgeCooldown(AiG, AiRiv, Frame);
		FAngerMarkerInput In;
		In.bShown = bShown;
		In.Human = Human;
		In.Level = AiRiv[Human].Level;
		In.Anger = AiRiv[Human].Anger;
		In.bGrudge = AiG.bActive;
		In.GrudgeRival = AiG.Rival;
		In.GrudgeKind = AiG.Kind;
		M.Frame(In);
		++Frame;
		return M.Sprite(1000.0, S);
	};

	OnBump(AiG, AiRiv, HumanG, HumanRiv, Ai, Human, true, false, Frame, false);
	TestTrue(TEXT("bump: flash"), Step());
	int32 On = 1;
	for (int32 K = 0; K < 300; ++K) { On += Step() ? 1 : 0; }
	TestEqual(TEXT("bump flash drawn for 62 frames"), On, 62);
	TestFalse(TEXT("bump flash over: undrawn"), Step());

	OnKnockdown(AiG, AiRiv, HumanG, HumanRiv, Ai, Human, true, false, Frame);
	TestTrue(TEXT("grudge started, kind 1"), AiG.bActive && AiG.Rival == Human && AiG.Kind == 1);
	const int32 Start = AiG.Frame;
	TestTrue(TEXT("grudge: marker on"), Step());
	while (Frame <= Start + 360) { Step(); }
	TestFalse(TEXT("not cooled at start + 360 (0x11EFD8: start + 360.44952 < now)"), AiG.bCooled);
	Step();
	TestTrue(TEXT("cooled at start + 361"), AiG.bCooled);
	TestTrue(TEXT("cooled grudge stays active: marker still on"), AiG.bActive && Step());
	for (int32 K = 0; K < 3000; ++K) { Step(); }
	TestTrue(TEXT("50 s later the grudge (and the marker) is still on"), Step());

	{
		FGrudge G;
		FRivalry R[MaxRacers];
		R[Human].Level = 90;
		R[Human].Anger = 0;
		StartGrudge(G, R, Human, 0);
		R[Human].Anger = 20;
		GrudgeCooldown(G, R, 5000);
		TestTrue(TEXT("kind 2: never cooled, anger kept"), G.Kind == 2 && !G.bCooled && R[Human].Anger == 20);
		FGrudge G0;
		FRivalry R0[MaxRacers];
		StartGrudge(G0, R0, Human, 0);
		R0[Human].Anger = 20;
		GrudgeCooldown(G0, R0, 5000);
		TestTrue(TEXT("kind 0: never cooled, anger kept"), G0.Kind == 0 && !G0.bCooled && R0[Human].Anger == 20);
	}

	TestFalse(TEXT("gate off (race over / replay): undrawn"), Step(false));
	TestTrue(TEXT("back in phase 4 with the grudge: drawn again"), Step());
	AiG.bActive = false;   // 0x116AB8
	TestFalse(TEXT("race reset clears the grudge: undrawn"), Step());
	return true;
}

#endif
