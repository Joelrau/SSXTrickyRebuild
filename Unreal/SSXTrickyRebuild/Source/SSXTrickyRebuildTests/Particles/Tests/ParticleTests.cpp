// 0x1CAEF8 / 0x33EDF0
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UI/SSXFrontEndData.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Effects/TrackEffects.h"
#include "Particles/SSXParticles.h"
#include "Particles/SSXRiderFx.h"
#include "Particles/SSXParticleWorld.h"
#include "Particles/SSXLensFX.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSceneTexture.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXParticles;

namespace
{
	constexpr EAutomationTestFlags PtTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> LoadRef(FAutomationTestBase* Test)
	{
		static TSharedPtr<FJsonObject> Cached;
		if (Cached.IsValid())
		{
			return Cached;
		}
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("particles_reference.json"))))
		{
			SSXTest::Skip(*Test, TEXT("skipped: no SimData/particles_reference.json (run tools/make_particles_reference.py)"));
			return nullptr;
		}
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(R, Cached) || !Cached.IsValid())
		{
			Test->AddError(TEXT("particles_reference.json does not parse"));
			return nullptr;
		}
		return Cached;
	}

	TArray<double> Nums(const TSharedPtr<FJsonValue>& V)
	{
		TArray<double> Out;
		for (const TSharedPtr<FJsonValue>& X : V->AsArray()) { Out.Add(X->AsNumber()); }
		return Out;
	}
	TArray<double> Nums(const TSharedPtr<FJsonObject>& O, const TCHAR* F) { return Nums(O->Values.FindChecked(F)); }
	FVector V3(const TArray<double>& A, int32 K = 0) { return FVector(A[K], A[K + 1], A[K + 2]); }

	bool Close(double A, double B, double Tol = 1e-3) { return FMath::Abs(A - B) <= Tol * FMath::Max(1.0, FMath::Abs(B)); }

	struct FSeq
	{
		int32 I = 0;
		double operator()() { ++I; return double((I * 7919) % 997) / 997.0; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesRecordsTest, "SSX.Particles.Records", PtTestFlags)
bool FSSXParticlesRecordsTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref)
	{
		return true;
	}
	int32 Total = 0, Bad = 0, Tracks = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& T : Ref->GetObjectField(TEXT("counts"))->Values)
	{
		SSXEffects::FEffectData D;
		FString Err;
		if (!D.Load(SSXEffects::FEffectData::DefaultPath(T.Key), &Err))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *T.Key, *Err));
			continue;
		}
		++Tracks;
		TMap<int32, int32> Tex, Blend, Copies;
		int32 N = 0;
		for (const FIntPoint& E : D.Effects)
		{
			for (int32 K = 0; K < E.Y; ++K)
			{
				const SSXEffects::FRecord& R = D.Records[E.X + K];
				if (R.Type != 2 || (D.I(R, 0) != 0 && D.I(R, 0) != 2))
				{
					continue;
				}
				uint32 W[51];
				for (int32 J = 0; J < 51; ++J) { W[J] = uint32(D.I(R, J + 1)); }
				const FEmitterConfig C = FEmitterConfig::FromWords(W);
				++N;
				Tex.FindOrAdd(C.Texture)++;
				Blend.FindOrAdd(C.Blend)++;
				Copies.FindOrAdd(C.Copies)++;
				const FParticleBurst B = FParticleBurst::FromEmitter(C, FInstanceMatrix(), [] { return 0.5; });
				const double Span = (C.IsContinuous() ? C.MaxLife() : C.EmitTime) * C.TimeScale;
				if (!(C.Texture >= 0 && C.Texture < NumTextures) || EmitterBlendMode(C.Blend) < 0 || C.Count <= 0
					|| C.TimeScale <= 0.0 || C.Life - C.LifeRange / 2 < -1e-6 || FMath::Abs(B.Interval * C.Count - Span) > 1e-3)
				{
					++Bad;
				}
			}
		}
		const TSharedPtr<FJsonObject> Want = T.Value->AsObject();
		TestEqual(*FString::Printf(TEXT("%s emitter records"), *T.Key), N, int32(Want->GetNumberField(TEXT("n"))));
		auto Hist = [&](const TCHAR* Field, const TMap<int32, int32>& Got)
		{
			const TSharedPtr<FJsonObject> H = Want->GetObjectField(Field);
			TestEqual(*FString::Printf(TEXT("%s %s classes"), *T.Key, Field), Got.Num(), H->Values.Num());
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : H->Values)
			{
				const int32* G = Got.Find(FCString::Atoi(*P.Key));
				TestEqual(*FString::Printf(TEXT("%s %s %s"), *T.Key, Field, *P.Key), G ? *G : 0, int32(P.Value->AsNumber()));
			}
		};
		Hist(TEXT("textures"), Tex);
		Hist(TEXT("blend"), Blend);
		Hist(TEXT("copies"), Copies);
		Total += N;
	}
	TestEqual(TEXT("track files"), Tracks, 12);
	TestEqual(TEXT("emitter records (979)"), Total, 979);
	TestEqual(TEXT("records failing decode / range / interval checks"), Bad, 0);
	AddInfo(FString::Printf(TEXT("%d emitter records on %d track files decode"), Total, Tracks));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesEvaluateTest, "SSX.Particles.Evaluate", PtTestFlags)
bool FSSXParticlesEvaluateTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref)
	{
		return true;
	}
	int32 Samples = 0, Frames = 0, Sprites = 0, CountBad = 0, SpriteBad = 0;
	double MaxPos = 0.0, MaxCol = 0.0, MaxSize = 0.0;
	TSet<FString> TracksSeen;
	for (const TSharedPtr<FJsonValue>& SV : Ref->GetArrayField(TEXT("samples")))
	{
		const TSharedPtr<FJsonObject> S = SV->AsObject();
		const TArray<double> Wd = Nums(S, TEXT("words"));
		uint32 W[51];
		for (int32 J = 0; J < 51; ++J) { W[J] = uint32(int64(Wd[J])); }
		const FEmitterConfig C = FEmitterConfig::FromWords(W);
		const TArray<TSharedPtr<FJsonValue>>& MRows = S->GetArrayField(TEXT("matrix"));
		FInstanceMatrix M;
		M.X = V3(Nums(MRows[0])); M.Y = V3(Nums(MRows[1])); M.Z = V3(Nums(MRows[2])); M.T = V3(Nums(MRows[3]));
		FSeq Seq;
		FParticleBurst B = FParticleBurst::FromEmitter(C, M, [&Seq] { return Seq(); });
		TestEqual(TEXT("blend mode"), B.BlendMode, int32(S->GetNumberField(TEXT("blend_mode"))));
		int32 Done = 0;
		TArray<FSprite> Out;
		for (const TSharedPtr<FJsonValue>& FV : S->GetArrayField(TEXT("frames")))
		{
			const TSharedPtr<FJsonObject> F = FV->AsObject();
			const int32 FrameNo = int32(F->GetNumberField(TEXT("frame")));
			while (Done < FrameNo) { B.Update(); ++Done; }
			Out.Reset();
			B.Evaluate(Out);
			++Frames;
			if (Out.Num() != int32(F->GetNumberField(TEXT("count"))) || !Close(B.T, F->GetNumberField(TEXT("t")), 1e-6))
			{
				if (++CountBad <= 5)
				{
					AddError(FString::Printf(TEXT("%s effect %d frame %d: %d sprites (model %d), t %.6f (model %.6f)"),
						*S->GetStringField(TEXT("track")), int32(S->GetNumberField(TEXT("effect"))), FrameNo, Out.Num(),
						int32(F->GetNumberField(TEXT("count"))), B.T, F->GetNumberField(TEXT("t"))));
				}
				continue;
			}
			const int32 Step = int32(F->GetNumberField(TEXT("step")));
			const TArray<TSharedPtr<FJsonValue>>& Want = F->GetArrayField(TEXT("sprites"));
			for (int32 K = 0; K < Want.Num(); ++K)
			{
				const TArray<double> E = Nums(Want[K]);
				const FSprite& G = Out[K * Step];
				const double DP = FVector::Dist(G.Pos, V3(E)) / FMath::Max(1.0, V3(E).Size());
				const double DS = FMath::Abs(G.HalfSize - E[3]);
				double DC = 0.0;
				for (int32 I = 0; I < 4; ++I) { DC = FMath::Max(DC, FMath::Abs(G.Rgba[I] - E[4 + I])); }
				MaxPos = FMath::Max(MaxPos, DP);
				MaxSize = FMath::Max(MaxSize, DS);
				MaxCol = FMath::Max(MaxCol, DC);
				++Sprites;
				if (DP > 1e-5 || DS > 1e-4 || DC > 1e-3 || G.Particle != int32(E[8]) || G.Copy != int32(E[9]))
				{
					if (++SpriteBad <= 5)
					{
						AddError(FString::Printf(TEXT("%s effect %d frame %d sprite %d: pos (%.3f %.3f %.3f) vs (%.3f %.3f %.3f), size %.4f vs %.4f, k %d/%d copy %d/%d"),
							*S->GetStringField(TEXT("track")), int32(S->GetNumberField(TEXT("effect"))), FrameNo, K * Step,
							G.Pos.X, G.Pos.Y, G.Pos.Z, E[0], E[1], E[2], G.HalfSize, E[3], G.Particle, int32(E[8]), G.Copy, int32(E[9])));
					}
				}
			}
		}
		TracksSeen.Add(S->GetStringField(TEXT("track")));
		++Samples;
	}
	TestEqual(TEXT("frames with a different sprite count or time"), CountBad, 0);
	TestEqual(TEXT("sprites different from the model"), SpriteBad, 0);
	AddInfo(FString::Printf(TEXT("%d records on %d tracks, %d sampled frames, %d sprites compared: max rel pos err %.2e, max size err %.2e cm, max colour err %.2e GS"),
		Samples, TracksSeen.Num(), Frames, Sprites, MaxPos, MaxSize, MaxCol));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesSprayTest, "SSX.Particles.Spray", PtTestFlags)
bool FSSXParticlesSprayTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
	if (!Ref || !Ref->TryGetArrayField(TEXT("spray"), Cases))
	{
		if (Ref) { SSXTest::Skip(*this, TEXT("skipped: no RAM spray cases (paused_race_1.bin missing)")); }
		return true;
	}
	int32 Fields = 0, Bad = 0;
	for (const TSharedPtr<FJsonValue>& CV : *Cases)
	{
		const TSharedPtr<FJsonObject> C = CV->AsObject();
		FSurfaceSpray Surf = FSurfaceSpray::FromFields(Nums(C, TEXT("row")));
		const TArray<double> RowInt = Nums(C, TEXT("row_int"));
		Surf.Texture = int32(RowInt[0]);
		Surf.Blend = int32(RowInt[1]);
		const TArray<double> L = Nums(C, TEXT("light"));
		const int32 N = int32(C->GetNumberField(TEXT("N")));
		const FVector Rgb = SprayColour(FVector4(L[0], L[1], L[2], L[3]), Surf.Desat, Surf.Brighten);
		const FParticleBurst B = FSprayFx::MakeSprayBurst(N, FVector::ZeroVector, FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0),
			FVector::ZeroVector, 300.0, 0.0, 0.0, Surf.Size * 0.8, Surf.Size * 1.2, Surf.LifeLo, Surf.LifeHi, Rgb, Surf.Alpha,
			Surf.Texture, Surf.Blend, [] { return 0.5; });
		const TArray<double> C0 = Nums(C, TEXT("c0")), Sl = Nums(C, TEXT("slope"));
		const double T = C->GetNumberField(TEXT("t"));
		struct FCheck { const TCHAR* Key; double Ram; double Port; };
		const FCheck Checks[] = {
			{ TEXT("copies"), C->GetNumberField(TEXT("copies")), double(B.Copies) },
			{ TEXT("interval"), C->GetNumberField(TEXT("interval")), B.Interval },
			{ TEXT("size_span"), C->GetNumberField(TEXT("size_span")), B.SizeSpan },
			{ TEXT("life_span"), C->GetNumberField(TEXT("life_span")), B.LifeSpan },
			{ TEXT("size_base"), C->GetNumberField(TEXT("size_base")), B.SizeLo - B.SizeSpan },
			{ TEXT("life_base"), C->GetNumberField(TEXT("life_base")), B.LifeLo - B.LifeSpan },
			{ TEXT("copy_dt"), C->GetNumberField(TEXT("copy_dt")), B.CopyDt },
			{ TEXT("ts"), C->GetNumberField(TEXT("ts")), B.Ts },
			{ TEXT("drift_z"), C->GetNumberField(TEXT("drift_z")), B.Drift.Z },
			{ TEXT("texture"), C->GetNumberField(TEXT("texture")), double(B.Texture) },
			{ TEXT("blend"), C->GetNumberField(TEXT("blend")), double(B.BlendMode) },
			{ TEXT("c0_r"), C0[0], B.C0.X }, { TEXT("c0_g"), C0[1], B.C0.Y }, { TEXT("c0_b"), C0[2], B.C0.Z },
			{ TEXT("c0_a"), C0[3], B.C0.W },
			{ TEXT("slope_r"), Sl[0], B.CSlope.X }, { TEXT("slope_a"), Sl[3], B.CSlope.W },
			{ TEXT("t_frames"), T * 12.0, double(FMath::RoundToInt32(T * 12.0)) },
			{ TEXT("N 1..3"), double(N >= 1 && N <= 3), 1.0 },
			{ TEXT("across jitter >= 300 cm/s"), double(V3(Nums(C, TEXT("va"))).Size() * 5.0 >= 299.5), 1.0 },
			{ TEXT("no up jitter"), double(V3(Nums(C, TEXT("vb"))).IsZero()), 1.0 },
		};
		for (const auto& K : Checks)
		{
			++Fields;
			if (!Close(K.Ram, K.Port))
			{
				if (++Bad <= 8)
				{
					AddError(FString::Printf(TEXT("rider %d burst: %s RAM %.6f, port %.6f"), int32(C->GetNumberField(TEXT("rider"))),
						K.Key, K.Ram, K.Port));
				}
			}
		}
	}
	TestEqual(TEXT("spray bursts"), Cases->Num(), 36);
	TestEqual(TEXT("spray fields different from RAM"), Bad, 0);
	AddInfo(FString::Printf(TEXT("%d RAM spray bursts, %d fields reproduced"), Cases->Num(), Fields));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesCloudsTest, "SSX.Particles.Clouds", PtTestFlags)
bool FSSXParticlesCloudsTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
	if (!Ref || !Ref->TryGetArrayField(TEXT("clouds"), Cases))
	{
		if (Ref) { SSXTest::Skip(*this, TEXT("skipped: no RAM cloud cases")); }
		return true;
	}
	int32 Bad = 0;
	double MaxAlpha = 0.0;
	int32 PrevRider = -1;
	FVector Prev = FVector::ZeroVector;
	for (const TSharedPtr<FJsonValue>& CV : *Cases)
	{
		const TSharedPtr<FJsonObject> C = CV->AsObject();
		const bool bConsts = Close(C->GetNumberField(TEXT("life")), CarveCloudLife) && Close(C->GetNumberField(TEXT("fade_in")), 0.2)
			&& Close(C->GetNumberField(TEXT("size_min")), CarveCloudSizeMin) && Close(C->GetNumberField(TEXT("size_end")), CarveCloudSizeEnd);
		FCarveCloud Cl;
		Cl.Age = C->GetNumberField(TEXT("age"));
		Cl.Intensity = C->GetNumberField(TEXT("intensity"));
		Cl.FadeIn = C->GetNumberField(TEXT("fade_in"));
		Cl.Variant = int32(C->GetNumberField(TEXT("variant")));
		Cl.Pos = V3(Nums(C, TEXT("pos")));
		Cl.Up = V3(Nums(C, TEXT("up")));
		FCloudSprite S;
		const bool bDrawn = CarveCloudSprite(Cl, S);
		const double DA = bDrawn ? FMath::Abs(S.Alpha - C->GetNumberField(TEXT("alpha"))) : 1.0;
		MaxAlpha = FMath::Max(MaxAlpha, DA);
		const int32 Rider = int32(C->GetNumberField(TEXT("rider")));
		const bool bSpacing = Rider != PrevRider || FVector::Dist(Prev, Cl.Pos) > CarveCloudSpacing;
		if (!bConsts || DA > 2e-3 || Cl.Variant < 0 || Cl.Variant > 3 || Cl.Intensity < 0.04 - 1e-6 || !bSpacing)
		{
			++Bad;
			AddError(FString::Printf(TEXT("cloud rider %d age %.3f: consts %d alpha err %.4f variant %d intensity %.3f spacing %d"),
				Rider, Cl.Age, bConsts, DA, Cl.Variant, Cl.Intensity, bSpacing));
		}
		PrevRider = Rider;
		Prev = Cl.Pos;
	}
	TestEqual(TEXT("carve clouds"), Cases->Num(), 32);
	TestEqual(TEXT("clouds different from RAM"), Bad, 0);
	AddInfo(FString::Printf(TEXT("%d RAM carve clouds: max alpha error %.2e"), Cases->Num(), MaxAlpha));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesLawsTest, "SSX.Particles.Laws", PtTestFlags)
bool FSSXParticlesLawsTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref)
	{
		return true;
	}
	const TSharedPtr<FJsonObject> L = Ref->GetObjectField(TEXT("laws"));
	int32 N = 0;
	for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("flakes")))
	{
		const TArray<double> E = Nums(V);
		TestTrue(TEXT("flake half-size"), Close(FlakeHalfSize(E[0], E[1], E[2]), E[3], 1e-9)); ++N;
	}
	for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("ghosts")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		const TArray<double> Want = Nums(A[2]);
		FVector4 T;
		const bool bOn = GhostTint(A[0]->AsNumber(), A[1]->AsNumber(), T);
		TestEqual(TEXT("ghost on"), bOn, Want.Num() == 4);
		if (bOn && Want.Num() == 4)
		{
			for (int32 I = 0; I < 4; ++I) { TestTrue(TEXT("ghost tint"), Close(T[I], Want[I], 1e-9)); }
		}
		++N;
	}
	for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("landing")))
	{
		const TArray<double> E = Nums(V);
		TestEqual(TEXT("landing puffs"), LandingPuffCount(E[0]), int32(E[1]));
		TestTrue(TEXT("landing intensity"), Close(LandingIntensityAdd(E[0]), E[2], 1e-9)); ++N;
	}
	for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("shadow_k")))
	{
		const TArray<double> E = Nums(V);
		TestTrue(TEXT("shadow grey"), Close(ShadowSilhouetteLevel(int32(E[0]), E[1]), E[2], 1e-9)); ++N;
	}
	for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("cloud_law")))
	{
		const TArray<double> E = Nums(V);
		FCarveCloud C;
		C.Age = E[0]; C.Intensity = 0.1; C.FadeIn = 0.2; C.Variant = 2;
		FCloudSprite S;
		TestTrue(TEXT("cloud drawn"), CarveCloudSprite(C, S));
		TestTrue(TEXT("cloud size"), Close(S.Size, E[1], 1e-9));
		TestTrue(TEXT("cloud alpha"), Close(S.Alpha, E[2], 1e-9));
		TestTrue(TEXT("cloud height"), Close(S.Pos.Z, E[3], 1e-9));
		++N;
	}
	for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("spray_colour")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		const TArray<double> Li = Nums(A[0]), Want = Nums(A[3]);
		const FVector C = SprayColour(FVector4(Li[0], Li[1], Li[2], Li[3]), A[1]->AsNumber(), A[2]->AsNumber());
		for (int32 I = 0; I < 3; ++I) { TestTrue(TEXT("spray colour"), Close(C[I], Want[I], 1e-9)); }
		++N;
	}
	FPowerFxSlot Slot;
	const TArray<TSharedPtr<FJsonValue>>& Seq = L->GetArrayField(TEXT("powerfx"));
	int32 K = 0;
	double MaxErr = 0.0;
	for (int32 F = 0; F < 90; ++F)
	{
		const FVector P(10.0 * F, 2.0 * F * F / 60.0, 100.0 + 5.0 * FMath::Sin(F / 7.0));
		const int32 Made = Slot.Frame(P, FVector(8, 0, 0));
		if (F % 15 == 14 && Seq.IsValidIndex(K))
		{
			const TSharedPtr<FJsonObject> E = Seq[K++]->AsObject();
			TestEqual(TEXT("powerfx made"), Made, int32(E->GetNumberField(TEXT("made"))));
			TestEqual(TEXT("powerfx live"), Slot.GetParts().Num(), int32(E->GetNumberField(TEXT("n"))));
			for (const TCHAR* Which : { TEXT("first"), TEXT("last") })
			{
				const TArray<double> W = Nums(E, Which);
				const FPowerFxSlot::FPart& Pt = FCString::Strcmp(Which, TEXT("first")) == 0 ? Slot.GetParts()[0] : Slot.GetParts().Last();
				MaxErr = FMath::Max(MaxErr, FVector::Dist(Pt.Pos, V3(W)));
				MaxErr = FMath::Max(MaxErr, FMath::Abs(Pt.Size - W[3]));
				for (int32 I = 0; I < 4; ++I) { MaxErr = FMath::Max(MaxErr, FMath::Abs(Pt.Argb[I] - W[4 + I])); }
			}
			++N;
		}
	}
	TestTrue(TEXT("powerfx particles = model (1e-6)"), MaxErr < 1e-6);
	AddInfo(FString::Printf(TEXT("%d law cases = the model; PowerFX max error %.2e"), N, MaxErr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesSnowfallTest, "SSX.Particles.Snowfall", PtTestFlags)
bool FSSXParticlesSnowfallTest::RunTest(const FString&)
{
	int32 Cases = 0;
	for (int32 Mode = 0; Mode < 10; ++Mode)
	for (int32 Humans = 0; Humans <= 2; ++Humans)
	for (int32 Racers = 1; Racers <= 6; ++Racers)
	for (double Draw : { 0.1, 0.5, 0.9 })
	{
		const double Chance = 0.5;
		const bool bWant = Mode != 2 && Mode != 4 && Humans <= 1 && Racers < 3 && Draw < Chance;
		TestEqual(TEXT("snowfall enable"), SSXEffects::SnowfallEnabled(Mode, Humans, Racers, Draw, Chance), bWant);
		++Cases;
	}
	TestFalse(TEXT("Garibaldi RAM: chance 0, 1 human, 6 racers -> off"), SSXEffects::SnowfallEnabled(4, 1, 6, 0.0, 0.0));
	TestEqual(TEXT("flakes per slot at I = 0.109 (RAM)"), FSnowfall::FlakesPerSlot(0.109), 4);
	TestEqual(TEXT("flakes per slot at I = 0.2"), FSnowfall::FlakesPerSlot(0.2), 8);
	TestEqual(TEXT("flakes per slot at I = 0.05"), FSnowfall::FlakesPerSlot(0.05), 2);
	FSnowfall S;
	FRandomStream R(7);
	S.Init(0.4, 0.3, 0.2, [&R] { return double(R.FRand()); });
	TestTrue(TEXT("intensity 0.05 + 0.15 x"), Close(S.GetIntensity(), 0.11, 1e-12));
	TArray<FSprite> Out;
	const FVector Cam(1234.0, -5678.0, 910.0);
	S.Gather(Cam, Out);
	bool bSize = true, bAlpha = true, bFade = false;
	for (const FSprite& F : Out)
	{
		bSize &= F.HalfSize >= 5.3 - 1e-9 && F.HalfSize < 6.8;
		bAlpha &= F.Rgba.W <= FSnowfall::Alpha + 1e-9 && F.Rgba.X == FSnowfall::Grey;
		bFade |= F.Rgba.W < FSnowfall::Alpha - 1e-6;
	}
	TestTrue(TEXT("flake half-size in [a, b) (10..14 cm across)"), bSize);
	TestTrue(TEXT("flake colour 128, alpha <= 24"), bAlpha);
	TestTrue(TEXT("fade over 2100..2450 cm"), bFade);
	TestTrue(TEXT("flakes drawn"), Out.Num() > 1000);
	// fall: (0, 0, -300) cm/s, wrapping every 700 cm (140 frames)
	TArray<FSprite> A, B;
	S.Gather(Cam, A);
	S.Frame();
	S.Gather(Cam, B);
	TestTrue(TEXT("falls 5 cm per frame"), A.Num() == B.Num() && Close(A[0].Pos.Z - B[0].Pos.Z, 5.0, 1e-9));
	for (int32 I = 0; I < 139; ++I) { S.Frame(); }
	B.Reset();
	S.Gather(Cam, B);
	TestTrue(TEXT("wraps after 700 cm"), A.Num() == B.Num() && Close(A[0].Pos.Z, B[0].Pos.Z, 1e-6));
	TestEqual(TEXT("flake handle 19 (game+0x30 +0x4C) = 'str2'"), FSnowfall::Texture, 19);
	AddInfo(FString::Printf(TEXT("%d enable cases; %d flakes around the camera at I = 0.11"), Cases, Out.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesRulesTest, "SSX.Particles.Rules", PtTestFlags)
bool FSSXParticlesRulesTest::RunTest(const FString&)
{
	const int32 Want[8] = { 5, 3, 2, 1, 4, 0, 6, 7 };
	for (int32 I = 0; I < 8; ++I) { TestEqual(TEXT("emitter blend table"), EmitterBlendMode(I), Want[I]); }
	TestEqual(TEXT("past the table"), EmitterBlendMode(9), -1);
	TestTrue(TEXT("q(2.7)"), Close(Drag(2.7), 0.73 * 2.7 - 0.113 * 2.7 * 2.7, 1e-12));
	TestTrue(TEXT("q(5) = q(2.7)"), Close(Drag(5.0), Drag(2.7), 1e-12));
	FRiderFxInput In;
	In.Motion = MotionGround;
	In.ContactKind = 0;
	In.Surface = 1;
	In.Steer = 0.8;
	In.Vel = FVector(0, 1500, 0);
	FSurfaceSpray Surf;
	Surf.Rate = 0.0752297; Surf.Size = 3.00818; Surf.LifeLo = 0.136181; Surf.LifeHi = 0.24252; Surf.Texture = 16;
	Surf.Desat = 0.537259; Surf.Brighten = 0.136696; Surf.Alpha = 0.400919; Surf.Blend = 3;
	FSprayFx Spray;
	FRandomStream R(3);
	int32 Total = 0;
	for (int32 F = 0; F < 60; ++F) { Total += Spray.Frame(In, Surf, [&R] { return double(R.FRand()); }); }
	const double Rate = (0.64 * 149.0 * 1500.0 * 0.006 + 1500.0 * 0.006) * Surf.Rate;
	TestTrue(TEXT("spray particles per second = rate (+-8)"), FMath::Abs(Total - Rate) <= 8.0);
	TestTrue(TEXT("ring keeps 40 bursts"), Spray.GetBursts().Num() <= SprayRing);
	const FParticleBurst& B = Spray.GetBursts().Last();
	TestTrue(TEXT("ground spray: across spread >= 300"), B.VA.Size() * 5.0 >= 300.0 - 1e-6);
	TestTrue(TEXT("ground spray: no up spread"), B.VB.IsZero());
	TestEqual(TEXT("ground spray texture from the row"), B.Texture, 16);
	TestEqual(TEXT("ground spray blend from the row (no remap)"), B.BlendMode, 3);
	In.Motion = MotionRail;
	In.Surface = 13;
	FSprayFx Sparks;
	int32 Flashes = 0, Frames = 6000;
	double MaxFlashR = 0.0;
	for (int32 F = 0; F < Frames; ++F)
	{
		const FVector4 Before = Sparks.GetFlash();
		Sparks.Frame(In, Surf, [&R] { return double(R.FRand()); });
		const FVector4 After = Sparks.GetFlash();
		if (After.Y > Before.Y + 1e-9) { ++Flashes; MaxFlashR = FMath::Max(MaxFlashR, After.Y); }
	}
	const FParticleBurst& S = Sparks.GetBursts().Last();
	TestEqual(TEXT("sparks: 7 copies"), S.Copies, 7);
	TestEqual(TEXT("sparks: image 0 brk1"), S.Texture, 0);
	TestEqual(TEXT("sparks: additive"), S.BlendMode, int32(BlendAdditive));
	TestTrue(TEXT("sparks: colour (200, 90, 0, 80)"), S.C0 == FVector4(200, 90, 0, 80));
	TestTrue(TEXT("big burst about 1 frame in 60"), Flashes > 60 && Flashes < 140);
	TestTrue(TEXT("flash (0.8, 0.6, 0.2) - 1/12"), Close(MaxFlashR, 0.8 - 1.0 / 12.0, 1e-9));
	TestEqual(TEXT("no snow spray on metal rails"), Sparks.GetBursts().FilterByPredicate([](const FParticleBurst& X) { return X.Texture == 13; }).Num(), 0);
	// carve clouds: 70 cm spacing, |steer| > 0.2
	FCarveClouds Clouds;
	In.Motion = MotionGround;
	In.Surface = 1;
	uint32 Bit = 0;
	for (int32 F = 0; F < 120; ++F)
	{
		In.BoardOrigin = FVector(0, 25.0 * F, 0);   // 25 cm per frame
		Clouds.Frame(In, [&Bit] { return Bit++; });
	}
	TestEqual(TEXT("clouds every 75 cm over 2 s (life 2 s)"), Clouds.Get().Num(), 40);
	In.Steer = 0.1;
	FCarveClouds None;
	None.Frame(In, [&Bit] { return Bit++; });
	TestEqual(TEXT("no cloud at |steer| 0.1"), None.Get().Num(), 0);
	FAfterimages G;
	for (int32 F = 0; F < 30; ++F) { G.Frame(F, 0.6, 0.0); }
	TestEqual(TEXT("ghost ring full"), G.GetCount(), 7);
	for (int32 F = 30; F < 48; ++F) { G.Frame(F, 0.0, 0.0); }
	TestEqual(TEXT("ghost count falls 1 per 3 frames"), G.GetCount(), 1);
	TestFalse(TEXT("ghosts hidden at count 1"), G.IsDrawn());
	TestEqual(TEXT("shadow 256 with one racer"), ShadowTextureSize(1), 256);
	TestEqual(TEXT("shadow 128 with six"), ShadowTextureSize(6), 128);
	SSXEffects::FEffectData D;
	if (D.Load(SSXEffects::FEffectData::DefaultPath(TEXT("gari"))) && D.Effects.IsValidIndex(120))
	{
		const FIntPoint E = D.Effects[120];
		for (int32 K = 0; K < E.Y; ++K)
		{
			const SSXEffects::FRecord& Rec = D.Records[E.X + K];
			if (Rec.Type != 2)
			{
				continue;
			}
			uint32 W[51];
			for (int32 J = 0; J < 51; ++J) { W[J] = uint32(D.I(Rec, J + 1)); }
			const FEmitterConfig C = FEmitterConfig::FromWords(W);
			TestEqual(TEXT("fountain N"), C.Count, 200);
			TestEqual(TEXT("fountain copies"), C.Copies, 8);
			FParticleBurst F = FParticleBurst::FromEmitter(C, FInstanceMatrix(), [] { return 0.37; });
			double Peak = 0.0;
			TArray<FSprite> Out;
			for (int32 I = 0; I < 150; ++I)
			{
				F.Update();
				Out.Reset();
				F.Evaluate(Out);
				for (const FSprite& Sp : Out) { Peak = FMath::Max(Peak, Sp.Pos.Z); }
			}
			TestTrue(TEXT("fountain peak 13..21 m"), Peak > 1300.0 && Peak < 2100.0);
			AddInfo(FString::Printf(TEXT("Garibaldi fountain peak %.0f cm"), Peak));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesTextureHandlesTest, "SSX.Particles.TextureHandles", PtTestFlags)
bool FSSXParticlesTextureHandlesTest::RunTest(const FString&)
{
	// 0x1CAEF8 / 0x33EDF0
	static const TCHAR* Names[NumTextures] = {
		TEXT("part"), TEXT("snfl"), TEXT("clod"), TEXT("spry"), TEXT("halo"), TEXT("brk1"), TEXT("brk2"), TEXT("brk3"),
		TEXT("ndl1"), TEXT("ndl2"), TEXT("swd1"), TEXT("swd2"), TEXT("swp1"), TEXT("swp2"), TEXT("cnf1"), TEXT("cnf2"),
		TEXT("blb1"), TEXT("blb2"), TEXT("str1"), TEXT("str2"), TEXT("str3"), TEXT("nois"), TEXT("strk"), TEXT("tral"),
		TEXT("ex06"), TEXT("ex07"), TEXT("ex08"), TEXT("ex09"), TEXT("lens"), TEXT("blnk"), TEXT("mip1"), TEXT("mip1"),
		TEXT("mip2"), TEXT("beam"), TEXT("fog0"), TEXT("spec"), TEXT("envr"), TEXT("exlm") };
	TestEqual(TEXT("out of range -> none"), ImageOfHandle(NumTextures), int32(INDEX_NONE));
	TestEqual(TEXT("negative -> none"), ImageOfHandle(-1), int32(INDEX_NONE));
	const FString Dir = FPaths::Combine(SSXFrontEnd::TextureDir(), TEXT("PARTICLE"));
	auto FileOf = [&](int32 Handle)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, FString::Printf(TEXT("%03d_*.png"), ImageOfHandle(Handle))), true, false);
		return Found.Num() ? Found[0] : FString();
	};
	if (FileOf(0).IsEmpty())
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("no converted PARTICLE.SSH images in %s (tools/sshtex.py): file names not checked"), *Dir));
	}
	else
	{
		for (int32 H = 0; H < NumTextures; ++H)
		{
			TestEqual(FString::Printf(TEXT("handle %d image"), H), FileOf(H), FString::Printf(TEXT("%03d_%s.png"), ImageOfHandle(H), Names[H]));
		}
	}
	TestEqual(TEXT("PowerFX particles: handle 20 (game+0x30 +0x50) = 'str3'"), FString(Names[20]), FString(TEXT("str3")));
	TestEqual(TEXT("PowerFX glow: handle 28 (+0x70) = 'lens' image 26"), ImageOfHandle(28), 26);
	TestEqual(TEXT("boost / spin ribbons: handle 29 (+0x74) = 'blnk' image 30"), ImageOfHandle(29), 30);
	TestEqual(TEXT("ground trail: handle 23 (+0x5C) = 'tral' image 29"), ImageOfHandle(FGroundTrail::Texture), 29);
	TestEqual(TEXT("lens FX handle"), ImageOfHandle(SSXLensFX::LensHandle), 26);
	SSXEffects::FEffectData D;
	FString Err;
	if (!D.Load(SSXEffects::FEffectData::DefaultPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("snow effects not loaded (%s): balloon records not checked"), *Err));
		return true;
	}
	int32 Pops = 0;
	for (const int32 Fx : { 77, 79, 81 })
	{
		if (!TestTrue(FString::Printf(TEXT("snow effect %d exists"), Fx), D.Effects.IsValidIndex(Fx)))
		{
			continue;
		}
		const FIntPoint E = D.Effects[Fx];
		int32 Stars = 0;
		for (int32 K = 0; K < E.Y; ++K)
		{
			const SSXEffects::FRecord& Rec = D.Records[E.X + K];
			if (Rec.Type != 2 || D.I(Rec, 0) != 0)
			{
				continue;
			}
			uint32 W[51];
			for (int32 J = 0; J < 51; ++J) { W[J] = uint32(D.I(Rec, J + 1)); }
			const FEmitterConfig C = FEmitterConfig::FromWords(W);
			Stars += C.Texture == 18 && ImageOfHandle(C.Texture) == 13 && C.Count == 200 && EmitterBlendMode(C.Blend) == BlendAlpha ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("balloon pop effect %d: two 200-particle 'str1' star emitters, alpha blend"), Fx), Stars, 2);
		Pops += Stars;
	}
	AddInfo(FString::Printf(TEXT("balloon pop star emitters: %d"), Pops));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXShadowExcludesRiderTest, "SSX.Particles.ShadowExcludesRider", PtTestFlags)

bool FSSXShadowExcludesRiderTest::RunTest(const FString& Parameters)
{
	UMaterial* M = LoadObject<UMaterial>(nullptr, USSXParticleWorld::ShadowMaterialPath);
	if (!M)
	{
		SSXTest::Skip(*this, TEXT("M_SSXShadowProject missing (Scripts/make_particle_materials.py): skipped"));
		return true;
	}
#if WITH_EDITORONLY_DATA
	bool bCustomDepth = false, bMask = false;
	for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
	{
		if (const UMaterialExpressionSceneTexture* T = Cast<UMaterialExpressionSceneTexture>(E))
		{
			bCustomDepth |= T->SceneTextureId == PPI_CustomDepth;
		}
		if (const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E))
		{
			bMask |= C->Code.Contains(TEXT("CD.r <= SD"));
		}
	}
	TestTrue(TEXT("the shadow material samples custom depth"), bCustomDepth);
	TestTrue(TEXT("rider / board pixels (custom depth = scene depth) are not shaded"), bMask);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrailUnderRiderTest, "SSX.Particles.TrailUnderRider", PtTestFlags)

bool FSSXTrailUnderRiderTest::RunTest(const FString& Parameters)
{
	// 0x1331A8
#if WITH_EDITORONLY_DATA
	struct FWant { const TCHAR* Path; bool bTrail; };
	const FWant Want[] = {
		{ USSXParticleWorld::TrailDarkenMaterialPath, true }, { USSXParticleWorld::TrailBrightenMaterialPath, true },
		{ USSXParticleWorld::DarkenMaterialPath, false }, { USSXParticleWorld::BrightenMaterialPath, false } };
	for (const FWant& W : Want)
	{
		UMaterial* M = LoadObject<UMaterial>(nullptr, W.Path);
		if (!M)
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s missing (Scripts/make_particle_materials.py): skipped"), W.Path));
			continue;
		}
		bool bCustomDepth = false, bSceneDepth = false, bSkip = false;
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			if (const UMaterialExpressionSceneTexture* T = Cast<UMaterialExpressionSceneTexture>(E))
			{
				bCustomDepth |= T->SceneTextureId == PPI_CustomDepth;
			}
			bSceneDepth |= E && E->GetClass()->GetName() == TEXT("MaterialExpressionSceneDepth");
			if (const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E))
			{
				bSkip |= C->Code.Contains(TEXT("CD.r <= SD")) && C->Code.Contains(TEXT("float3(1.0, 1.0, 1.0)"));
			}
		}
		const FString N = M->GetName();
		TestEqual(N + TEXT(": modulate (GS modes 4 / 8 on the scene colour)"), int32(M->GetBlendMode()), int32(BLEND_Modulate));
		TestTrue(N + TEXT(": two-sided"), M->IsTwoSided());
		TestEqual(N + TEXT(": rider pixels left unchanged only on the layer-2 trail"), bCustomDepth && bSceneDepth && bSkip, W.bTrail);
	}
#endif
	return true;
}

#endif
