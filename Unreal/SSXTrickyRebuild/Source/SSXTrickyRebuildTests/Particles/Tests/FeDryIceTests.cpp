// 0x147E60 / 0x25EDF0
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Effects/TrackEffects.h"
#include "Particles/SSXParticles.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXEffects;
using namespace SSXParticles;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXFeDryIceTest, "SSX.Particles.FrontEndDryIce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXFeDryIceTest::RunTest(const FString& Parameters)
{
	FEffectData Fe;
	FString Err;
	if (!Fe.Load(FEffectData::DefaultPath(TEXT("ssxfe")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s (tools/export_effects.py)"), *Err));
		return true;
	}
	const TArray<int32> DryIce = { 221, 249, 309, 281, 222, 223, 282, 283, 310, 250, 251, 224 };
	TestTrue(TEXT("FE instances"), Fe.NumInstances() == 337);
	TestTrue(TEXT("FE .ltg grid exported"), Fe.Cells.Num() > 0);

	{
		FEffectWorld W;
		W.Init(&Fe, 0, 11);
		int32 Started = 0, Ended = 0;
		W.OnOutcome = [&](const FOutcome& O)
		{
			Started += O.Kind == EOutcome::Emitter ? 1 : 0;
			Ended += O.Kind == EOutcome::EmitterDied ? 1 : 0;
		};
		TestTrue(TEXT("FEStartScript found"), W.CallFunction(TEXT("FEStartScript")));
		for (int32 F = 0; F < 5; ++F) { W.Frame(); }
		TestEqual(TEXT("no FE load: 12 emitters created"), Started, 12);
		TestEqual(TEXT("no FE load: all 12 end (instance not cell-active)"), Ended, 12);
		TestEqual(TEXT("no FE load: no emitter left"), W.NumEmitters(), 0);
	}

	FEffectWorld W;
	W.Init(&Fe, 0, 11);
	W.LoadFrontEndFlags();
	int32 NotActive = 0;
	for (int32 I = 0; I < Fe.NumInstances(); ++I)
	{
		const uint16 F = W.GetFlags(I);
		NotActive += ((F & (FlCellActive | FlHeld)) != FlCellActive || !(F & FlDisplay)) ? 1 : 0;
	}
	TestEqual(TEXT("every FE instance cell-active and displayed after the load"), NotActive, 0);
	TestEqual(TEXT("dry-ice flags = 0x1001 | 0x102"), int32(W.GetFlags(221)), 0x1103);

	struct FStarted { int32 Inst; int32 Record; };
	TArray<FStarted> Emitters;
	int32 Died = 0;
	W.OnOutcome = [&](const FOutcome& O)
	{
		if (O.Kind == EOutcome::Emitter && O.A == 0) { Emitters.Add({ O.Instance, O.Record }); }
		if (O.Kind == EOutcome::EmitterDied) { ++Died; }
	};
	TestTrue(TEXT("FEStartScript found"), W.CallFunction(TEXT("FEStartScript")));
	const int32 Frames = 600;
	for (int32 F = 0; F < Frames; ++F) { W.Frame(); }
	TestEqual(TEXT("12 emitters started"), Emitters.Num(), 12);
	TestEqual(TEXT("12 emitter nodes alive after 10 s"), W.NumEmitters(), 12);
	TestEqual(TEXT("no emitter ended"), Died, 0);
	for (int32 K = 0; K < FMath::Min(Emitters.Num(), DryIce.Num()); ++K)
	{
		TestEqual(*FString::Printf(TEXT("emitter %d instance"), K), Emitters[K].Inst, DryIce[K]);
	}

	uint32 Seed = 12345u;
	auto Rng = [&Seed]() { Seed = Seed * 1664525u + 1013904223u; return double(Seed >> 8) / double(1u << 24); };
	int32 Total = 0, Outward = 0, Below = 0, BadColour = 0;
	for (const FStarted& E : Emitters)
	{
		const FRecord& R = Fe.Records[E.Record];
		uint32 Words[51];
		for (int32 K = 0; K < 51; ++K) { Words[K] = uint32(Fe.I(R, K + 1)); }
		const FEmitterConfig C = FEmitterConfig::FromWords(Words);
		const bool bConfig = C.Count == 75 && C.Copies == 2 && C.IsContinuous() && FMath::IsNearlyEqual(C.TimeScale, 0.6, 1e-6)
			&& C.Texture == 2 && EmitterBlendMode(C.Blend) == BlendAdditive && FMath::IsNearlyEqual(C.Drift.Z, -500.0);
		TestTrue(*FString::Printf(TEXT("instance %d config (N 75, 2 copies, continuous, ts 0.6, brk3, additive, drift -500)"), E.Inst), bConfig);
		const FInstanceMatrix M = FInstanceMatrix::FromMatrix(Fe.InstMatrix[E.Inst]);
		FParticleBurst B = FParticleBurst::FromEmitter(C, M, Rng);
		for (int32 F = 0; F < Frames; ++F) { B.Update(); }
		TArray<FSprite> S;
		B.Evaluate(S);
		Total += S.Num();
		TestTrue(*FString::Printf(TEXT("instance %d draws %d sprites (75 particles x up to 2 copies)"), E.Inst, S.Num()), S.Num() >= 75 && S.Num() <= 150);
		const double R0 = FVector2D(B.P0.X, B.P0.Y).Size();
		TestTrue(*FString::Printf(TEXT("instance %d emits from the ring at z 656 (r %.0f)"), E.Inst, R0), FMath::Abs(B.P0.Z - 656.0) < 5.0 && R0 > 650.0 && R0 < 850.0);
		for (const FSprite& P : S)
		{
			Outward += FVector2D(P.Pos.X, P.Pos.Y).Size() > R0 + 100.0 ? 1 : 0;
			Below += P.Pos.Z < B.P0.Z - 500.0 ? 1 : 0;
			BadColour += (P.Rgba.W > 0.45 * 128.0 || P.Rgba.X > 0.65 * 128.0) ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("character room: %d dry-ice sprites after %d frames, %d moved > 1 m outward, %d fell > 5 m"), Total, Frames, Outward, Below));
	TestTrue(TEXT("sprites drawn"), Total >= 12 * 75);
	TestTrue(TEXT("streams move outward through the camera ring"), Outward > Total / 4);
	TestTrue(TEXT("streams fall"), Below > Total / 4);
	TestEqual(TEXT("faint additive colours"), BadColour, 0);
	return true;
}

#endif
