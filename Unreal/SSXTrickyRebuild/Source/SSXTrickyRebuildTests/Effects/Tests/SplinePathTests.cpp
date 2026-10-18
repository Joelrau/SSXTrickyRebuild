#include "Misc/AutomationTest.h"
#include "TestSkip.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/LineBatchComponent.h"
#include "Effects/SSXSplineLineComponent.h"
#include "Effects/SSXTrackEffects.h"
#include "Effects/SplinePath.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Effects/TrackEffects.h"
#include "Sim/PatchWorld.h"

using namespace SSXEffects;

namespace
{
	constexpr EAutomationTestFlags SplineTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	TArray<FSplinePathSegment> SyntheticSegments()
	{
		TArray<FSplinePathSegment> S;
		S.SetNum(2);
		S[0].Coef[0] = FVector3f(10, 20, 0); S[0].Coef[1] = FVector3f(-30, 50, 5);
		S[0].Coef[2] = FVector3f(1000, 100, 40); S[0].Coef[3] = FVector3f(0, 0, 0);
		S[0].Poly[0] = 0.0001f; S[0].Poly[1] = -0.001f; S[0].Poly[2] = 0.1f; S[0].Poly[3] = 0.f;
		S[0].Prev = INDEX_NONE; S[0].Next = 1; S[0].Length = 1000.f; S[0].Start = 0.f;
		S[1].Coef[0] = FVector3f(0, 0, 0); S[1].Coef[1] = FVector3f(0, -200, 0);
		S[1].Coef[2] = FVector3f(-300, 900, -60); S[1].Coef[3] = FVector3f(980, 170, 45);
		S[1].Poly[0] = 0.f; S[1].Poly[1] = 0.f; S[1].Poly[2] = 0.08f; S[1].Poly[3] = 0.f;
		S[1].Prev = 0; S[1].Next = INDEX_NONE; S[1].Length = 1250.f; S[1].Start = 1000.f;
		return S;
	}

	FSplinePathState Run(const TArray<FSplinePathSegment>& Segs, int32 Mode, int32 Facing, int32 Count, float Speed, int32 Frames)
	{
		FSplinePathConfig C;
		C.Spline = 0; C.EndMode = Mode; C.Facing = Facing; C.Count = Count; C.Speed = Speed; C.Heading = 0.25f;
		FSplinePathState S;
		SplinePath::Init(S, C, Segs, 0, 2);
		for (int32 K = 0; K < Frames; ++K) { SplinePath::Step(S, Segs); }
		return S;
	}

	bool Near(float A, float B, float Tol) { return FMath::Abs(A - B) <= Tol * FMath::Max(1.f, FMath::Abs(B)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSplinePathModelTest, "SSX.Effects.SplinePath.Model", SplineTestFlags)
bool FSSXSplinePathModelTest::RunTest(const FString&)
{
	const TArray<FSplinePathSegment> Segs = SyntheticSegments();
	constexpr float Tol = 2e-5f;
	// wrap, yaw + pitch, 3 copies, speed 600 (1000 cm / frame), 4 frames
	{
		const FSplinePathState S = Run(Segs, 1, 0, 3, 600.f, 4);
		TestTrue(TEXT("wrap: length 2250, spacing 750, dist 1750, speed 1000"),
			S.Length == 2250.f && S.Spacing == 750.f && S.Dist == 1750.f && Near(S.Speed, 1000.f, Tol));
		const float Want[3][6] = {
			{ 723.67529296875f, 108.64569091796875f, 32.14552307128906f, -0.047605693340301514f, 0.f, 1.6118830442428589f },
			{ 860.0f, 497.9999694824219f, 21.000001907348633f, 0.07492978870868683f, 0.f, -0.13515758514404297f },
			{ 0.f, 0.f, 0.f, -0.03976999968290329f, 0.f, 1.7211277484893799f } };
		for (int32 I = 0; I < 3; ++I)
		{
			const FSplinePathPoint& P = S.Points[I];
			const bool bOk = Near(P.Pos.X, Want[I][0], Tol) && Near(P.Pos.Y, Want[I][1], Tol) && Near(P.Pos.Z, Want[I][2], Tol)
				&& P.Pos.W == 1.f && Near(P.Angles.X, Want[I][3], Tol) && P.Angles.Y == 0.f && Near(P.Angles.Z, Want[I][5], Tol);
			TestTrue(FString::Printf(TEXT("wrap: copy %d pose (%s / %s)"), I, *P.Pos.ToString(), *P.Angles.ToString()), bOk);
		}
		const FMatrix44f M = SplinePath::PointMatrix(S.Points[1]);
		const float WantM[4][3] = { { 0.9908801317214966f, 0.1347464621067047f, 0.f },
			{ -0.13436837494373322f, 0.9880997538566589f, -0.07485969364643097f },
			{ -0.01008707843720913f, 0.07417698204517365f, 0.9971940517425537f }, { 860.f, 497.9999694824219f, 21.000001907348633f } };
		bool bM = true;
		for (int32 R = 0; R < 4; ++R) for (int32 K = 0; K < 3; ++K) { bM &= FMath::Abs(M.M[R][K] - WantM[R][K]) <= 2e-5f * (R == 3 ? 1000.f : 1.f); }
		TestTrue(TEXT("copy matrix = R(x, pitch) R(z, yaw), position row"), bM);
	}
	{
		const FSplinePathState S = Run(Segs, 2, 2, 1, 700.f, 4);
		const FSplinePathPoint& P = S.Points[0];
		TestTrue(FString::Printf(TEXT("ping-pong: dist 0, speed -1166.67 (%f %f)"), S.Dist, S.Speed), S.Dist == 0.f && Near(S.Speed, -1166.6666f, Tol));
		TestTrue(FString::Printf(TEXT("ping-pong: pose (%s / %s)"), *P.Pos.ToString(), *P.Angles.ToString()),
			Near(P.Pos.X, 940.f, Tol) && Near(P.Pos.Y, 286.44439697265625f, Tol) && Near(P.Pos.Z, 37.000003814697266f, Tol)
			&& Near(P.Angles.X, -0.06664837896823883f, Tol) && Near(P.Angles.Z, 4.96238899230957f, Tol));
	}
	{
		FSplinePathState S = Run(Segs, 0, 3, 2, 900.f, 3);
		const FSplinePathPoint& P = S.Points[1];
		TestTrue(TEXT("stop: done, dist 3000"), S.bDone && S.Dist == 3000.f);
		TestTrue(FString::Printf(TEXT("stop: copy 1 (%s / %s)"), *P.Pos.ToString(), *P.Angles.ToString()),
			Near(P.Pos.X, 362.6787414550781f, Tol) && Near(P.Pos.Y, 44.308868408203125f, Tol) && Near(P.Pos.Z, 15.318989753723145f, Tol)
			&& P.Angles.X == 0.f && Near(P.Angles.Z, 1.8207963705062866f, Tol));
		const FVector4f Before = S.Points[0].Pos;
		SplinePath::Step(S, Segs);
		TestTrue(TEXT("stop: a done node no longer moves"), S.Points[0].Pos == Before && S.Dist == 3000.f);
	}
	// 0x250C10
	{
		float Sn, Cs;
		bool bOk = true;
		const float In[3] = { 2.f, -4.f, 0.3f };
		const float Want[3][2] = { { 0.90929747f, -0.41614679f }, { 0.7568025f, -0.6536436f }, { 0.29552022f, 0.9553365f } };
		for (int32 K = 0; K < 3; ++K)
		{
			SplinePath::SinCos(In[K], Sn, Cs);
			bOk &= FMath::Abs(Sn - Want[K][0]) <= 1e-6f && FMath::Abs(Cs - Want[K][1]) <= 1e-6f;
		}
		TestTrue(TEXT("sincos 0x250C10"), bOk);
	}
	{
		FVector3f P[10];
		SplinePath::SegmentLinePoints(Segs[1], P);
		TestTrue(TEXT("line strip ends at the segment ends"), P[0].Equals(Segs[1].Coef[3], 1e-4f)
			&& P[9].Equals(Segs[1].Coef[1] + Segs[1].Coef[2] + Segs[1].Coef[3], 0.05f));
		FSplinePathConfig C;
		C.Colour[0] = 1.f; C.Colour[1] = 0.1f; C.Colour[2] = 0.1f; C.Colour[3] = 0.1f;
		TestTrue(TEXT("line colour (12, 12, 12, 128)"), SplinePath::LineColour(C) == FColor(12, 12, 12, 128));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSplinePathTrackTest, "SSX.Effects.SplinePath.Track", SplineTestFlags)
bool FSSXSplinePathTrackTest::RunTest(const FString&)
{
	struct FCase { const TCHAR* Stem; int32 Effect; int32 Inst; int32 Count; float Speed; bool bLine; bool bDiesOnUnload; };
	const FCase Cases[2] = { { TEXT("snow"), 109, 2008, 15, 1000.f / 60.f, true, false }, { TEXT("merquer"), 752, 344, 1, 3500.f / 60.f, false, true } };
	for (const FCase& K : Cases)
	{
		FEffectData D;
		FPatchWorld Rails;
		FString Err;
		if (!D.Load(FEffectData::DefaultPath(K.Stem), &Err) || !Rails.LoadRails(FPatchWorld::DefaultRailPath(K.Stem), &Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("skipped %s: %s"), K.Stem, *Err));
			continue;
		}
		if (!D.Effects.IsValidIndex(K.Effect) || D.Effects[K.Effect].Y < 1)
		{
			AddError(FString::Printf(TEXT("%s: effect %d missing"), K.Stem, K.Effect));
			continue;
		}
		const FRecord& Last = D.Records[D.Effects[K.Effect].X + D.Effects[K.Effect].Y - 1];
		if (Last.Type != 2 || D.I(Last, 0) != 1)
		{
			AddError(FString::Printf(TEXT("%s: effect %d does not end with a SplinePath emitter"), K.Stem, K.Effect));
			continue;
		}
		TArray<FSplinePathSegment> Segs;
		Segs.SetNum(Rails.NumRailSegments());
		for (int32 I = 0; I < Segs.Num(); ++I)
		{
			const FSimRailSegment& G = Rails.GetRailSegment(I);
			for (int32 R = 0; R < 4; ++R) { Segs[I].Coef[R] = FVector3f(G.Coef[R]); Segs[I].Poly[R] = G.DistPoly[R]; }
			Segs[I].Prev = G.Prev; Segs[I].Next = G.Next; Segs[I].Length = float(G.Length); Segs[I].Start = float(G.StartDist);
		}
		TArray<FIntPoint> Spl;
		for (int32 I = 0; I < Rails.NumRailSplines(); ++I) { Spl.Add(FIntPoint(Rails.GetRailSpline(I).FirstSegment, Rails.GetRailSpline(I).SegmentCount)); }
		FEffectWorld W;
		W.Init(&D, 4, D.TrackId);
		W.SetSplines(MoveTemp(Segs), MoveTemp(Spl));
		int32 Died = 0;
		W.OnOutcome = [&Died](const FOutcome& O) { Died += O.Kind == EOutcome::EmitterDied && O.A == 1; };
		W.RunEffect(K.Effect, K.Inst, nullptr);
		if (!TestEqual(FString::Printf(TEXT("%s: one SplinePath node"), K.Stem), W.NumEmitters(), 1)) { continue; }
		const FEmitter& E = W.GetEmitters()[0];
		TestTrue(FString::Printf(TEXT("%s: %d copies, spacing = length / count, speed %f"), K.Stem, K.Count, K.Speed),
			E.Kind == 1 && E.Path.bValid && E.Path.Points.Num() == K.Count && E.Path.Config.bDrawSpline == K.bLine
			&& FMath::IsNearlyEqual(E.Path.Spacing, E.Path.Length / K.Count, 0.01f) && FMath::IsNearlyEqual(E.Path.Speed, K.Speed, 1e-3f));
		TArray<float> StepLen;
		FVector4f Prev = FVector4f(0, 0, 0, 0);
		for (int32 F = 0; F < 3600; ++F)
		{
			W.Frame();
			const FSplinePathState& S = W.GetEmitters()[0].Path;
			if (F > 0) { StepLen.Add(FVector3f(S.Points[0].Pos - Prev).Size()); }
			Prev = S.Points[0].Pos;
		}
		TestTrue(FString::Printf(TEXT("%s: still one node after 3600 frames"), K.Stem), W.NumEmitters() == 1 && Died == 0);
		StepLen.Sort();
		const float Median = StepLen.Num() ? StepLen[StepLen.Num() / 2] : 0.f;
		TestTrue(FString::Printf(TEXT("%s: median frame step %.3f ~ speed %.3f"), K.Stem, Median, K.Speed), FMath::Abs(Median - K.Speed) < 0.02f * K.Speed);
		W.RunEffect(K.Effect, K.Inst, nullptr);
		TestEqual(FString::Printf(TEXT("%s: re-run makes no second node"), K.Stem), W.NumEmitters(), 1);
		// 0x1FA6A0
		W.Deactivate(K.Inst);
		W.Frame();
		TestEqual(FString::Printf(TEXT("%s: node after the instance's cell unload"), K.Stem), W.NumEmitters(), K.bDiesOnUnload ? 0 : 1);
		TestEqual(FString::Printf(TEXT("%s: EmitterDied outcomes"), K.Stem), Died, K.bDiesOnUnload ? 1 : 0);
	}
	return true;
}

// 0x251748
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSplinePathCablesTest, "SSX.Effects.SplinePath.Cables", SplineTestFlags)
bool FSSXSplinePathCablesTest::RunTest(const FString&)
{
	TSharedPtr<FPatchWorld> Rails = MakeShared<FPatchWorld>();
	FString Err;
	FEffectData Probe;
	if (!Probe.Load(FEffectData::DefaultPath(TEXT("snow")), &Err) || !Rails->LoadRails(FPatchWorld::DefaultRailPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s"), *Err));
		return true;
	}
	UPackage* Package = CreatePackage(TEXT("/Temp/SSXSplineCableTest/snow"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("snow"), Package);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	USSXTrackEffects* Fx = USSXTrackEffects::Get(World);
	if (!TestNotNull(TEXT("track effects subsystem"), Fx) || !TestTrue(TEXT("BeginTrack snow"), Fx->BeginTrack(TEXT("snow"), 1, 0, Rails)))
	{
		return false;
	}
	struct FLift { int32 Effect, Inst, Spline; };
	const FLift Lifts[3] = { { 109, 2008, 55 }, { 122, 939, 222 }, { 134, 93, 292 } };
	TMap<int32, int32> WantPoints;
	for (const FLift& L : Lifts)
	{
		Fx->GetEffectWorld().RunEffect(L.Effect, L.Inst, nullptr);
		WantPoints.FindOrAdd(USSXSplineLineComponent::PointsPerStrip * Rails->GetRailSpline(L.Spline).SegmentCount)++;
	}
	Fx->ApplyRender();
	const TArray<USSXSplineLineComponent*> Lines = Fx->GetSplineLineComponents();
	TestEqual(TEXT("one cable component per lift"), Lines.Num(), 3);
	for (const USSXSplineLineComponent* L : Lines)
	{
		int32* N = WantPoints.Find(L->NumPoints());
		TestTrue(FString::Printf(TEXT("%d points = 10 x the lift spline's segments"), L->NumPoints()), N && *N > 0);
		if (N) { --*N; }
		TestTrue(TEXT("colour (12, 12, 12, 128)"), L->GetColour() == FColor(12, 12, 12, 128));
		TestTrue(TEXT("registered scene primitive"), L->IsRegistered() && L->GetWorld() == World);
		const UMaterialInterface* M = L->GetMaterial(0);
		TestTrue(FString::Printf(TEXT("opaque unlit material (%s)"), M ? *M->GetPathName() : TEXT("none")),
			M && M->GetBlendMode() == BLEND_Opaque && M->GetShadingModels().IsUnlit());
		TestFalse(TEXT("casts no shadow"), L->CastShadow);
	}
	if (const ULineBatchComponent* Batcher = World->GetLineBatcher(UWorld::ELineBatcherType::World))
	{
		TestEqual(TEXT("no lines in the World line batcher"), Batcher->BatchedLines.Num(), 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
