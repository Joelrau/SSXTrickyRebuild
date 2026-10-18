#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Race/GameRng.h"
#include "Race/RacePathTracker.h"
#include "Race/RaceRandom.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Misc/DateTime.h"
#include "Particles/SSXRiderFx.h"
#include "World/SSXPropAnimation.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/GroundControls.h"
#include "Sim/Ground/RiderCollision.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags RngTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	uint32 CallsSince(const FGameRng& Before) { return FGameRng::StreamB().S[5] - Before.S[5]; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRngRaceDrawsTest, "SSX.Rng.RaceDraws", RngTestFlags)

bool FSSXRngRaceDrawsTest::RunTest(const FString& Parameters)
{
	const FGameRng Saved = FGameRng::StreamB();
	ON_SCOPE_EXIT { FGameRng::StreamB() = Saved; };

	{
		FGameRng::StreamB().Seed(0xAEE0F2A5u);
		FGameRng Ref(0xAEE0F2A5u);
		FRaceRandom G;
		bool bSame = true;
		for (int32 I = 0; I < 50; ++I) { bSame = bSame && G.Next() == Ref.Next(); }
		TestTrue(TEXT("default FRaceRandom = global stream B"), bSame && FGameRng::StreamB().S[5] == Ref.S[5]);
		FRaceRandom L(77u);
		FGameRng R77(77u);
		const FGameRng Before = FGameRng::StreamB();
		bSame = true;
		for (int32 I = 0; I < 50; ++I) { bSame = bSame && uint32(L.Mod(100)) == R77.Next() % 100u; }
		TestTrue(TEXT("FRaceRandom(seed): next_B() % n unsigned, own state"), bSame && CallsSince(Before) == 0u);
	}

	// 0x118830
	for (uint32 Seed = 1; Seed <= 20; ++Seed)
	{
		FRaceRandom L(Seed);
		const uint32 R = FGameRng(Seed).Next() % 100u;
		const int32 Got = SSXRaceTrack::LineClassWanted(2, 0, 0.5, 0.4, L);
		const int32 Want = 40 < int32(R) ? SSXRaceTrack::LineClassDefault : 0;
		TestEqual(FString::Printf(TEXT("line class seed %u (roll %u)"), Seed, R), Got, Want);
		TestEqual(FString::Printf(TEXT("line class seed %u: one draw"), Seed), L.Gen().S[5] - FGameRng(Seed).S[5], 1u);
	}

	// 0x15ADA8
	{
		TestTrue(TEXT("0x211 is a random-variant id"), SSXAnim::RandomVariantCount(0x211) > 0);
		TestEqual(TEXT("0x210 has no variants"), SSXAnim::RandomVariantCount(0x210), 0);
		TestEqual(TEXT("0x20B has no variants"), SSXAnim::RandomVariantCount(0x20B), 0);
		TestTrue(TEXT("0x207 draws (count 1)"), SSXAnim::RandomVariantCount(0x207) > 0);
		SSXAnim::FRiderAnimChannel Ch;
		FGameRng::StreamB().Seed(5u);
		FGameRng Ref(5u);
		const FGameRng Before = FGameRng::StreamB();
		const int32 V = Ch.RequestPlay(0x211);
		const uint32 R = Ref.Next();
		TestEqual(TEXT("taunt play: one draw"), CallsSince(Before), 1u);
		TestEqual(TEXT("taunt variant = next_B() % count"), V, int32(R % uint32(SSXAnim::RandomVariantCount(0x211))));
		Ch.RequestPlay(0x210);
		TestEqual(TEXT("head check play: no draw"), CallsSince(Before), 1u);
		uint32 Taken = 0;
		TestTrue(TEXT("the driver takes the pre-drawn value"), Ch.TakeDraw(0x211, Taken) && Taken == R);
		TestFalse(TEXT("taken once"), Ch.TakeDraw(0x211, Taken));
	}

	// 0x127918
	{
		TUniquePtr<FRiderSim> Sim = MakeUnique<FRiderSim>();
		const FGameRng Before = FGameRng::StreamB();
		TestTrue(TEXT("shove started"), SSXRiderHit::ShoveInput(*Sim, 0, 1));
		TestEqual(TEXT("shove 0x207: one draw"), CallsSince(Before), 1u);
		TestTrue(TEXT("shove holds channel 1"), Sim->State.Ch1Anim == 0x207 && Sim->State.Ch1EndFrame > Sim->State.Frame);
	}

	// 0x102FC8
	auto RunCruise = [](double Steer, double Crouch, int32 Frames, TArray<uint32>& OutPerFrame)
	{
		TUniquePtr<FRiderSim> Sim = MakeUnique<FRiderSim>();
		FRiderState& St = Sim->State;
		St.Steer.Cur = Steer;
		St.Steer.Rate = 1.0;
		St.Crouch.Cur = Crouch;
		OutPerFrame.Reset();
		for (int32 F = 0; F < Frames; ++F)
		{
			const FGameRng Before = FGameRng::StreamB();
			SSXGround::CruiseOverlays(*Sim);
			OutPerFrame.Add(CallsSince(Before));
			++St.Frame;
		}
		return St.CruiseTauntTimer;
	};
	{
		FGameRng::StreamB().Seed(9u);
		TArray<uint32> D;
		RunCruise(0.0, 0.0, 70, D);
		int32 Bad = 0;
		for (int32 F = 0; F < D.Num(); ++F) { Bad += D[F] != (F + 1 >= 62 ? 1u : 0u); }
		TestEqual(TEXT("taunt: no draw before frame 62, then one per frame"), Bad, 0);
		RunCruise(0.6, 0.5, 70, D);
		Bad = 0;
		for (int32 F = 0; F < D.Num(); ++F) { Bad += D[F] != (F + 1 >= 62 ? 2u : 1u); }
		TestEqual(TEXT("bobble: one roll per carving frame (+ taunt from 62)"), Bad, 0);
		RunCruise(-0.6, 0.5, 10, D);
		Bad = 0;
		for (const uint32 N : D) { Bad += N != 0u; }
		TestEqual(TEXT("bobble: steer x rate <= 0 never rolls"), Bad, 0);
	}
	{
		TUniquePtr<FRiderSim> Sim = MakeUnique<FRiderSim>();
		FRiderState& St = Sim->State;
		St.Steer.Cur = 0.7;
		St.Steer.Rate = 1.0;
		St.CruiseTauntTimer = 2.0;
		St.CruiseLookTimer = 2.0;
		St.Ch1Anim = 0x20C;
		St.Ch1EndFrame = St.Frame + 5;
		const FGameRng Before = FGameRng::StreamB();
		SSXGround::CruiseOverlays(*Sim);
		TestEqual(TEXT("channel 1 busy: no draw"), CallsSince(Before), 0u);
		TestTrue(TEXT("channel 1 busy: timers restart"), St.CruiseTauntTimer == 0.0 && St.CruiseLookTimer == 0.0);
	}
	{
		// 0x103660, 0x234F68, 0x128150, 0x235298
		struct FHooks final : public IRiderRaceHooks
		{
			TArray<int32> Taunts;
			TArray<double> Wins;
			virtual bool RacerTable(const FRiderSim&, FRacerTable& Out) const override
			{
				Out.Self = 0;
				Out.NumRacers = 2;
				Out.Dist[1] = 500.0;
				Out.Bearing[1] = 3.0;
				Out.bGrudge = true;
				Out.GrudgeRival = 1;
				Out.GrudgeKind = 0;
				return true;
			}
			virtual void OnTaunt(FRiderSim&, int32 Rival) override { Taunts.Add(Rival); }
			virtual void OnWinTaunt(FRiderSim&, double Strength) override { Wins.Add(Strength); }
		} Hooks;
		TUniquePtr<FRiderSim> Sim = MakeUnique<FRiderSim>();
		Sim->SetRaceHooks(&Hooks);
		FRiderState& St = Sim->State;
		St.Frame = 1000;
		St.PositiveReactionFrame = 1000 - 359;
		St.PositiveReactionStrength = 0.7;
		SSXGround::CruiseOverlays(*Sim);
		TestTrue(TEXT("win taunt: overlay 0x214, hook with the strength, stamp cleared"), St.Ch1Anim == 0x214 && Hooks.Wins.Num() == 1
			&& Hooks.Wins[0] == 0.7 && St.PositiveReactionFrame == -1);
		St.Frame = 2000;
		St.Ch1EndFrame = 0;
		St.PositiveReactionFrame = 2000 - 360;
		SSXGround::CruiseOverlays(*Sim);
		TestTrue(TEXT("win taunt: a stamp 360 frames old is cleared silently"), Hooks.Wins.Num() == 1 && St.PositiveReactionFrame == -1);
		int32 Tries = 0;
		while (Hooks.Taunts.Num() == 0 && Tries < 20)
		{
			FGameRng::StreamB().Seed(uint32(100 + Tries++));
			St.Frame += 10;
			St.Ch1EndFrame = 0;
			St.CruiseTauntTimer = 2.0;
			SSXGround::CruiseOverlays(*Sim);
		}
		TestTrue(TEXT("taunt: hook with the grudge rival, then overlay 0x211"), Hooks.Taunts.Num() == 1 && Hooks.Taunts[0] == 1 && St.Ch1Anim == 0x211);
		Sim->SetRaceHooks(nullptr);
	}
	return !HasAnyErrors();
}

// 0x345E58, 0x182858, 0x21E6C0, 0x1C8EC8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRngStreamATest, "SSX.Rng.StreamA", RngTestFlags)

bool FSSXRngStreamATest::RunTest(const FString& Parameters)
{
	const FGameRng SavedA = FGameRng::StreamA(), SavedB = FGameRng::StreamB();
	ON_SCOPE_EXIT { FGameRng::StreamA() = SavedA; FGameRng::StreamB() = SavedB; };

	const FDateTime T(2001, 11, 5, 12, 34, 56);
	TestEqual(TEXT("boot seed from the BCD clock block"), FGameRng::BootSeed(T), 0x12345600u ^ (0x01110500u << 3));
	TestEqual(TEXT("boot seed, midnight 1 Jan 2000"), FGameRng::BootSeed(FDateTime(2000, 1, 1)), (0x00010100u << 3));

	FGameRng::StreamA().Seed(0x1234u);
	FGameRng Ref(0x1234u);
	bool bRand = true;
	for (int32 I = 0; I < 20; ++I) { bRand &= USSXAudioSubsystem::Rand15() == int32(Ref.Next() & 0x7FFFu); }
	TestTrue(TEXT("Rand15 = stream A next() & 0x7FFF"), bRand);

	const FGameRng BBefore = FGameRng::StreamB();
	FGameRng::StreamA().Seed(99u);
	FGameRng P(99u);
	TestEqual(TEXT("prop Rand01 = stream A uniform(0, 1)"), USSXPropAnimation::Rand01(), double(P.Uniform(0.f, 1.f)));
	TestEqual(TEXT("prop RandU32 = stream A next()"), USSXPropAnimation::RandU32(), P.Next());

	{
		int32 Draws = 0;
		TArray<double> Values;
		SSXParticles::FSnowfall S;
		S.Init(1.0, 0.0, 0.0, [&]() { ++Draws; const double V = 0.125 * (Draws % 8); Values.Add(V); return V; });
		TestEqual(TEXT("snowfall: 32 stream-A draws (4 seeds x 8 slots)"), Draws, 32);
		TArray<SSXParticles::FSprite> Out;
		S.Gather(FVector::ZeroVector, Out);
		const double Expect = SSXParticles::FlakeHalfSize(5.0, 7.0, double(SSXParticles::AdvanceSeed(float(Values[3] + 1.0))) - 1.0);
		bool bFound = false;
		for (const SSXParticles::FSprite& F : Out) { bFound |= FMath::IsNearlyEqual(F.HalfSize, Expect, 1e-6); }
		TestTrue(TEXT("snowfall: slot 0 flake 0 size = RNEXT of the fourth seed"), bFound);
	}
	TestTrue(TEXT("stream B untouched"), FMemory::Memcmp(BBefore.S, FGameRng::StreamB().S, sizeof(BBefore.S)) == 0);
	return !HasAnyErrors();
}

#endif
