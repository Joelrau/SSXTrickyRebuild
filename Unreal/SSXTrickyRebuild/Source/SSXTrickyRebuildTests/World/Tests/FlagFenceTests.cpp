// 0x144EC8 / 0x143CD0 / 0x143B68 / 0x144428
#include "Effects/TrackEffects.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/ScopeExit.h"
#include "World/PropNodes.h"
#include "World/SSXPropAnimation.h"
#include "World/SSXWorldPresentation.h"
#include "Particles/SSXSpriteRenderer.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXPropNodes;

namespace
{
	constexpr EAutomationTestFlags FlagTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FStripSource MakeQuad(const FVector& Scale)
	{
		FStripSource S;
		S.Inst = 0;
		S.Scale = Scale;
		S.Pos[0] = FVector(0.5, -1.0, 1.0); S.Pos[1] = FVector(0.5, 1.0, 1.0);
		S.Pos[2] = FVector(-0.5, -1.0, 0.0); S.Pos[3] = FVector(-0.5, 1.0, 0.0);
		S.UV[0] = FVector2D(0, 0); S.UV[1] = FVector2D(2, 0); S.UV[2] = FVector2D(0, 1); S.UV[3] = FVector2D(2, 1);
		for (FVector& N : S.Nrm) { N = FVector(0, 0, 1); }
		S.LightDir[0] = FVector(1, 0, 0); S.LightDir[1] = FVector(0, 0, 1); S.LightDir[2] = FVector(0, 1, 0);
		S.LightColour[0] = FVector4(40, 50, 60, 0); S.LightColour[1] = FVector4(100, 100, 100, 0); S.LightColour[2] = FVector4(0, 0, 0, 0);
		S.Ambient = FVector4(60, 60, 60, 128);
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFlagsMotionTest, "SSX.World.Flags.Motion", FlagTestFlags)
bool FSSXWorldFlagsMotionTest::RunTest(const FString&)
{
	// Flag: record (dir, 1.5 Hz, 40, forever), model scale 0.84 -> divisor 250
	FFlagStrip F;
	F.Init(0, 1.5, 40.0, 0.0, 0.84, []() { return 0.25; });
	TestEqual(TEXT("flag rate = Hz / 60"), F.Rate, 1.5 / 60.0, 1e-12);
	TestEqual(TEXT("flag amplitude / (max(sx, 1) x 250)"), F.Amplitude, 40.0 / 250.0, 1e-12);
	TestEqual(TEXT("flag start phase = draw"), F.Phase, 0.25, 1e-12);
	double O[StripCols];
	F.Offsets(O);
	TestEqual(TEXT("dir 0: pole column 0 stays"), O[0], 0.0);
	for (int32 K = 1; K < StripCols; ++K)
	{
		TestEqual(*FString::Printf(TEXT("dir 0 column %d"), K), O[K], F.Amplitude * (K / 8.0) * FMath::Sin(-2.0 * PI * 0.25 + 2.0 * PI * K / 8.0), 1e-12);
	}
	F.Direction = 1;
	double M[StripCols];
	F.Offsets(M);
	TestEqual(TEXT("dir 1: column 7 stays (as compiled)"), M[7], 0.0);
	for (int32 K = 1; K < StripCols; ++K)
	{
		TestEqual(*FString::Printf(TEXT("dir 1 column %d = dir 0 k = %d"), 7 - K, K), M[7 - K], O[K], 1e-12);
	}
	FFlagStrip T;
	T.Init(0, 1.5, 25.0, 2.0, 1.0, []() { return 0.0; });
	int32 Frames = 0;
	while (!T.Tick() && Frames < 1000) { ++Frames; }
	TestEqual(TEXT("flag timer: done(1) on frame seconds x 60"), Frames + 1, 120);
	FFlagStrip R;
	R.Init(0, 1.5, -1.0, 0.0, 1.0, []() { return 0.5; });
	TestEqual(TEXT("random amplitude U(5, 50) / 60 / 250"), R.Amplitude, (0.5 * 45.0 + 5.0) / 60.0 / 250.0, 1e-12);

	FFenceStrip G;
	G.Scale = 0.5 / 250.0;
	G.Touch(0.625);
	TestEqual(TEXT("touch: A = |amount|"), G.Amp, 0.625, 1e-12);
	TestEqual(TEXT("touch: rate = +0.1 A"), G.Rate, 0.0625, 1e-12);
	TestTrue(TEXT("touch arms both timers"), G.Timer40 == 4 && G.Timer44 == 4);
	TArray<TPair<int32, double>> Fired;
	int32 Done = -1;
	for (int32 Fr = 1; Fr <= 400 && Done < 0; ++Fr)
	{
		if (G.Tick([&Fired](int32 Side, double A) { Fired.Add({ Side, A }); })) { Done = Fr; }
		if (Fr == 4)
		{
			TestEqual(TEXT("both timers fire on frame 4"), Fired.Num(), 2);
			if (Fired.Num() == 2)
			{
				TestTrue(TEXT("side 1 (+0xC8) first, then side 0"), Fired[0].Key == 1 && Fired[1].Key == 0);
				TestEqual(TEXT("neighbour amount = -0.4 A while swinging +"), Fired[0].Value, -0.25, 1e-12);
			}
		}
		if (Fr == 10)
		{
			TestEqual(TEXT("first half swing clamps at A on frame 10"), G.Theta, 0.625, 1e-12);
			TestEqual(TEXT("A halves"), G.Amp, 0.3125, 1e-12);
			TestEqual(TEXT("rate reverses at 0.1 A"), G.Rate, -0.03125, 1e-12);
		}
	}
	TestTrue(TEXT("fence settles (done 1)"), Done > 0);
	FFenceStrip H;
	H.Amp = 1.0;
	H.Touch(0.5);
	TestTrue(TEXT("no touch at A = 1"), H.Timer40 == 0 && H.Amp == 1.0);
	FFenceStrip P;
	P.Scale = 2.0;
	P.Theta = 0.5;
	double Q[StripCols];
	P.Offsets(Q);
	TestEqual(TEXT("fence profile column 3 = theta scale 200 x 0.924"), Q[3], 0.5 * 2.0 * 200.0 * 0.9239, 1e-9);
	TestTrue(TEXT("fence ends stay"), Q[0] == 0.0 && Q[7] == 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFlagsStripTest, "SSX.World.Flags.Strip", FlagTestFlags)
bool FSSXWorldFlagsStripTest::RunTest(const FString&)
{
	const FStripSource S = MakeQuad(FVector(3.0, 200.0, 400.0));
	double Zero[StripCols] = {};
	FVector L[StripVerts];
	FVector2D UV[StripVerts];
	StripLocal(S, Zero, true, L, UV);
	const FMatrix Inst = FTranslationMatrix(FVector(1000, 2000, 3000));
	const FMatrix D = StripDrawMatrix(S, Inst);
	for (int32 K = 0; K < StripCols; ++K)
	{
		const double T = K / 7.0;
		const FVector Top = (S.Pos[0] + (S.Pos[1] - S.Pos[0]) * T) * S.Scale + FVector(1000, 2000, 3000);
		const FVector Bot = (S.Pos[2] + (S.Pos[3] - S.Pos[2]) * T) * S.Scale + FVector(1000, 2000, 3000);
		TestTrue(*FString::Printf(TEXT("top column %d on the quad"), K), D.TransformPosition(L[2 * K]).Equals(Top, 1e-6));
		TestTrue(*FString::Printf(TEXT("bottom column %d on the quad"), K), D.TransformPosition(L[2 * K + 1]).Equals(Bot, 1e-6));
		TestTrue(*FString::Printf(TEXT("uv column %d"), K), UV[2 * K].Equals(FVector2D(2 * T, 0), 1e-9) && UV[2 * K + 1].Equals(FVector2D(2 * T, 1), 1e-9));
	}
	double Off[StripCols] = { 0, 0, 0, 0.01, 0, 0, 0, 0 };
	StripLocal(S, Off, false, L, UV);
	TestEqual(TEXT("offset on the top row"), D.TransformPosition(L[6]).X - (S.Pos[0].X * 3.0 + 1000.0), 0.01 * 3.0 * 250.0, 1e-6);
	TestEqual(TEXT("fence mode 0: the bottom row stays"), D.TransformPosition(L[7]).X, S.Pos[2].X * 3.0 + 1000.0, 1e-6);
	const FStripSource Thin = MakeQuad(FVector(0.84, 150.0, 900.0));
	StripLocal(Thin, Zero, true, L, UV);
	TestEqual(TEXT("sx < 1: x drawn at scale 1"), StripDrawMatrix(Thin, FMatrix::Identity).TransformPosition(L[0]).X, 0.5, 1e-9);
	const FColor Cf = StripColour(S, FVector(1, 0, 0));
	TestTrue(TEXT("flag normal (1, 0, 0): ambient + light 0"), Cf == FColor(100, 110, 120, 128));
	const FColor Cn = StripColour(S, FVector(0.6, 0, 0.8));
	TestTrue(TEXT("fence corner-0 normal"), Cn == FColor(uint8(60 + 24 + 80), uint8(60 + 30 + 80), uint8(60 + 36 + 80), 128));
	FStripSource Hot = S;
	Hot.Ambient = FVector4(250, 250, 250, 128);
	TestTrue(TEXT("clamped at 255"), StripColour(Hot, FVector(0, 0, 1)).R == 255);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFlagsDataTest, "SSX.World.Flags.Data", FlagTestFlags)
bool FSSXWorldFlagsDataTest::RunTest(const FString&)
{
	struct FExpect { const TCHAR* Stem; int32 Flags; int32 Fences; };
	const FExpect Expect[] = { { TEXT("elysium"), 22, 0 }, { TEXT("gari"), 0, 359 }, { TEXT("megaple"), 12, 172 },
		{ TEXT("merquer"), 0, 680 }, { TEXT("mesa"), 38, 0 }, { TEXT("snow"), 47, 577 } };
	int32 Loaded = 0, TotalFlags = 0, TotalFences = 0;
	for (const FExpect& E : Expect)
	{
		FPropAnimData A;
		SSXEffects::FEffectData Fx;
		FString Err;
		if (!A.Load(FPropAnimData::DefaultPath(E.Stem), &Err) || !Fx.Load(SSXEffects::FEffectData::DefaultPath(E.Stem), &Err))
		{
			AddWarning(Err);
			continue;
		}
		++Loaded;
		int32 NFlag = 0, NFence = 0;
		for (int32 I = 0; I < Fx.NumInstances(); ++I)
		{
			bool bFlag = false, bFence = false;
			for (int32 Slot = 0; Slot < 5; ++Slot)
			{
				const int32 Ef = Fx.EffectFor(I, Slot);
				if (!Fx.Effects.IsValidIndex(Ef)) { continue; }
				for (int32 K = 0; K < Fx.Effects[Ef].Y; ++K)
				{
					const SSXEffects::FRecord& R = Fx.Records[Fx.Effects[Ef].X + K];
					bFlag |= R.Type == 0 && Fx.I(R, 0) == SSXEffects::TypeFlag;
					bFence |= R.Type == 0 && Fx.I(R, 0) == SSXEffects::TypeFence;
				}
			}
			if (!bFlag && !bFence) { continue; }
			NFlag += bFlag;
			NFence += bFence;
			const FStripSource* S = A.StripOf(I);
			if (!TestNotNull(*FString::Printf(TEXT("%s instance %d has a strip source"), E.Stem, I), S)) { continue; }
			if (bFence)
			{
				for (int32 Nb : { S->NeighbourC4, S->NeighbourC8 })
				{
					TestTrue(*FString::Printf(TEXT("%s fence %d neighbour %d is a strip"), E.Stem, I, Nb), Nb == INDEX_NONE || A.StripOf(Nb) != nullptr);
				}
			}
			double Zero[StripCols] = {};
			FVector L[StripVerts];
			FVector2D UV[StripVerts];
			StripLocal(*S, Zero, true, L, UV);
			const FVector P0 = StripDrawMatrix(*S, FMatrix::Identity).TransformPosition(L[0]);
			const FVector Q0 = S->Pos[0] * S->Scale;
			const double Tol = 0.01 + Q0.GetAbsMax() / 32767.0;
			TestTrue(*FString::Printf(TEXT("%s strip %d corner 0 y, z on the quad"), E.Stem, I), FMath::Abs(P0.Y - Q0.Y) < Tol && FMath::Abs(P0.Z - Q0.Z) < Tol);
			if (S->Scale.X >= 1.0) { TestTrue(*FString::Printf(TEXT("%s strip %d corner 0 x"), E.Stem, I), FMath::Abs(P0.X - Q0.X) < Tol); }
		}
		TestEqual(*FString::Printf(TEXT("%s flag instances"), E.Stem), NFlag, E.Flags);
		TestEqual(*FString::Printf(TEXT("%s fence instances"), E.Stem), NFence, E.Fences);
		TestEqual(*FString::Printf(TEXT("%s strip sources"), E.Stem), A.Strips.Num(), E.Flags + E.Fences);
		TotalFlags += NFlag;
		TotalFences += NFence;
	}
	if (Loaded == 0)
	{
		SSXTest::Skip(*this, TEXT("no SimData propanim v3 / effects (tools/export_prop_anim.py, export_effects.py): skipped"));
		return true;
	}
	if (Loaded == UE_ARRAY_COUNT(Expect))
	{
		TestEqual(TEXT("119 flags"), TotalFlags, 119);
		TestEqual(TEXT("1788 fences"), TotalFences, 1788);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFlagsDrawTest, "SSX.World.Flags.Draw", FlagTestFlags)
bool FSSXWorldFlagsDrawTest::RunTest(const FString&)
{
	SSXEffects::FEffectData Fx;
	FPropAnimData A;
	FString Err;
	if (!Fx.Load(SSXEffects::FEffectData::DefaultPath(TEXT("snow")), &Err) || !A.Load(FPropAnimData::DefaultPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s"), *Err));
		return true;
	}
	int32 FlagInst = INDEX_NONE, FlagRec = INDEX_NONE, FenceInst = INDEX_NONE, FenceRec = INDEX_NONE;
	for (int32 I = 0; I < Fx.NumInstances() && (FlagInst == INDEX_NONE || FenceInst == INDEX_NONE); ++I)
	{
		for (int32 Slot : { 0, 1 })
		{
			const int32 Ef = Fx.EffectFor(I, Slot);
			if (!Fx.Effects.IsValidIndex(Ef)) { continue; }
			for (int32 K = 0; K < Fx.Effects[Ef].Y; ++K)
			{
				const int32 Ri = Fx.Effects[Ef].X + K;
				const SSXEffects::FRecord& R = Fx.Records[Ri];
				if (R.Type != 0) { continue; }
				if (Fx.I(R, 0) == SSXEffects::TypeFlag && FlagInst == INDEX_NONE) { FlagInst = I; FlagRec = Ri; }
				const FStripSource* S = A.StripOf(I);
				if (Fx.I(R, 0) == SSXEffects::TypeFence && FenceInst == INDEX_NONE && S && S->NeighbourC8 != INDEX_NONE) { FenceInst = I; FenceRec = Ri; }
			}
		}
	}
	if (!TestTrue(TEXT("snow has a flag and a fence"), FlagInst != INDEX_NONE && FenceInst != INDEX_NONE))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXFlagFenceTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	AStaticMeshActor* FlagActor = World->SpawnActor<AStaticMeshActor>();
	if (!TestNotNull(TEXT("plane mesh"), Plane) || !TestNotNull(TEXT("actor"), FlagActor))
	{
		return false;
	}
	FlagActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	FlagActor->GetStaticMeshComponent()->SetStaticMesh(Plane);
	FlagActor->Tags.Add(*FString::Printf(TEXT("SSXInst%d"), FlagInst));
	USSXPropAnimation* Props = World->GetSubsystem<USSXPropAnimation>();
	if (!TestNotNull(TEXT("prop subsystem"), Props))
	{
		return false;
	}
	Props->BeginTrack(TEXT("snow"), &Fx, nullptr, nullptr);

	Props->OnNodeCreated(FlagInst, SSXEffects::TypeFlag, FlagRec, nullptr, false);
	const FFlagStrip* F = Props->FindFlag(FlagInst);
	if (!TestNotNull(TEXT("flag node"), F))
	{
		return false;
	}
	const FStripSource* S = A.StripOf(FlagInst);
	TestEqual(TEXT("flag amplitude 40 / (max(sx, 1) x 250)"), F->Amplitude, 40.0 / (FMath::Max(S->Scale.X, 1.0) * 250.0), 1e-9);
	FVector P0[StripVerts], P1[StripVerts];
	FVector2D UV[StripVerts];
	FColor C;
	TestTrue(TEXT("strip available"), Props->GetStripGame(FlagInst, P0, UV, C));
	for (int32 Fr = 0; Fr < 10; ++Fr) { Props->StepFrame(); }
	Props->GetStripGame(FlagInst, P1, UV, C);
	TestTrue(TEXT("pole column stays (dir of the record)"), P0[F->Direction == 0 ? 0 : 14].Equals(P1[F->Direction == 0 ? 0 : 14], 1e-6));
	TestFalse(TEXT("the cloth moves"), P0[F->Direction == 0 ? 14 : 0].Equals(P1[F->Direction == 0 ? 14 : 0], 1e-3));
	Props->ApplyRender();
	TestFalse(TEXT("the model is replaced by the strip"), FlagActor->GetStaticMeshComponent()->IsVisible());
	TestEqual(TEXT("one strip drawn"), Props->NumStripsDrawn(), 1);
	FlagActor->SetActorHiddenInGame(true);
	Props->ApplyRender();
	TestEqual(TEXT("hidden instance: no strip"), Props->NumStripsDrawn(), 0);
	FlagActor->SetActorHiddenInGame(false);
	Props->OnNodeDestroyed(FlagInst, SSXEffects::TypeFlag);
	Props->ApplyRender();
	TestTrue(TEXT("node gone: the model draws again"), FlagActor->GetStaticMeshComponent()->IsVisible());
	TestEqual(TEXT("node gone: no strip"), Props->NumStripsDrawn(), 0);

	const FMatrix& M = Fx.InstMatrix[FenceInst];
	FContactRecord Touch;
	Touch.VHat = M.GetScaledAxis(EAxis::X).GetSafeNormal();
	Touch.N = -Touch.VHat;
	Touch.A = 2000.0;
	Props->OnNodeCreated(FenceInst, SSXEffects::TypeFence, FenceRec, &Touch, true);
	const FFenceStrip* G = Props->FindFence(FenceInst);
	if (!TestNotNull(TEXT("fence node"), G))
	{
		return false;
	}
	TestEqual(TEXT("creating touch: A = min(|n' . v| |a| 0.001, 1)"), G->Amp, 1.0, 1e-9);
	TestTrue(TEXT("creating touch: timers idle"), G->Timer40 == 0 && G->Timer44 == 0);
	const int32 Nb = A.StripOf(FenceInst)->NeighbourC8;
	Props->StepFrame();
	const FFenceStrip* H = Props->FindFence(Nb);
	if (TestNotNull(TEXT("neighbour fence node"), H))
	{
		TestEqual(TEXT("neighbour A = 0.4"), H->Amp, 0.4, 1e-9);
		TestTrue(TEXT("neighbour timers armed"), H->Timer40 == 4 && H->Timer44 == 4);
	}
	Props->OnFenceTouch(FenceInst, Touch);
	TestEqual(TEXT("no slot-18 touch while A = 1"), Props->FindFence(FenceInst)->Timer40, 0);
	return true;
}

// 0x1E6E28
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFlagsFenceHitTest, "SSX.World.Flags.FenceHit", FlagTestFlags)
bool FSSXWorldFlagsFenceHitTest::RunTest(const FString&)
{
	SSXEffects::FEffectData Fx;
	FPropAnimData A;
	FString Err;
	if (!Fx.Load(SSXEffects::FEffectData::DefaultPath(TEXT("gari")), &Err) || !A.Load(FPropAnimData::DefaultPath(TEXT("gari")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s"), *Err));
		return true;
	}
	int32 FenceInst = INDEX_NONE, FenceRec = INDEX_NONE;
	for (int32 I = 0; I < Fx.NumInstances() && FenceInst == INDEX_NONE; ++I)
	{
		const int32 Ef = Fx.EffectFor(I, 1);
		if (!Fx.Effects.IsValidIndex(Ef) || !A.StripOf(I)) { continue; }
		for (int32 K = 0; K < Fx.Effects[Ef].Y; ++K)
		{
			const int32 Ri = Fx.Effects[Ef].X + K;
			if (Fx.Records[Ri].Type == 0 && Fx.I(Fx.Records[Ri], 0) == SSXEffects::TypeFence) { FenceInst = I; FenceRec = Ri; break; }
		}
	}
	if (!TestTrue(TEXT("gari has a fence strip"), FenceInst != INDEX_NONE))
	{
		return false;
	}
	const FString MeshPath = FString::Printf(TEXT("/Game/Tracks/Gari/gari/StaticMeshes/gari_inst%04d_obj0.gari_inst%04d_obj0"), FenceInst, FenceInst);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Mesh)
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s not imported"), *MeshPath));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXFenceHitTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
	if (!TestNotNull(TEXT("actor"), Actor))
	{
		return false;
	}
	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Actor->Tags.Add(*FString::Printf(TEXT("SSXInst%d"), FenceInst));
	USSXPropAnimation* Props = World->GetSubsystem<USSXPropAnimation>();
	if (!TestNotNull(TEXT("prop subsystem"), Props))
	{
		return false;
	}
	Props->BeginTrack(TEXT("gari"), &Fx, nullptr, nullptr);

	TArray<UMaterialInterface*> Before;
	bool bSoft = false;
	for (int32 I = 0; I < Comp->GetNumMaterials(); ++I)
	{
		Before.AddUnique(Comp->GetMaterial(I));
		bSoft |= USSXWorldPresentation::IsSoftBlendMaterial(Comp->GetMaterial(I));
	}
	Props->ApplyRender();
	TestTrue(TEXT("before: the model draws"), Comp->IsVisible());
	TestEqual(TEXT("before: no strip"), Props->NumStripsDrawn(), 0);
	TestTrue(TEXT("the fence mesh has a core and a soft blend section (tex 7, 0x55008)"), Before.Num() == 2 && bSoft);

	const FMatrix& M = Fx.InstMatrix[FenceInst];
	FContactRecord Touch;
	Touch.VHat = M.GetScaledAxis(EAxis::X).GetSafeNormal();
	Touch.N = -Touch.VHat;
	Touch.A = 300.0;
	Props->OnNodeCreated(FenceInst, SSXEffects::TypeFence, FenceRec, &Touch, true);
	TestNotNull(TEXT("fence node"), Props->FindFence(FenceInst));
	Props->ApplyRender();
	TestFalse(TEXT("after: the model is replaced by the strip"), Comp->IsVisible());
	TestEqual(TEXT("after: one strip"), Props->NumStripsDrawn(), 1);
	const USSXSpriteRenderComponent* R = Props->GetStripRenderer();
	if (!TestNotNull(TEXT("strip renderer"), R))
	{
		return false;
	}
	TArray<UMaterialInterface*> After;
	for (const FSSXSpriteBatch& B : R->GetBatches())
	{
		if (B.Strips.Num() > 0) { After.AddUnique(B.Material); }
		for (const TArray<FSSXStripVertex>& St : B.Strips) { TestEqual(TEXT("8 x 2 strip"), St.Num(), StripVerts); }
	}
	TestTrue(TEXT("after: the strip draws every section of the model (core and blend)"),
		After.Num() == Before.Num() && Before.FilterByPredicate([&After](UMaterialInterface* X) { return !After.Contains(X); }).Num() == 0);
	TestTrue(TEXT("after: the soft blend writes custom depth as the model did"), R->bRenderCustomDepth == bSoft);

	const FStaticMeshLODResources& Lod = Mesh->GetRenderData()->LODResources[0];
	const FColorVertexBuffer& Cv = Lod.VertexBuffers.ColorVertexBuffer;
	FVector Pos[StripVerts];
	FVector2D UV[StripVerts];
	FColor C;
	Props->GetStripGame(FenceInst, Pos, UV, C);
	if (Cv.GetNumVertices() > 0 && Cv.GetVertexData())
	{
		const FColor V0 = Cv.VertexColor(0);
		TestTrue(*FString::Printf(TEXT("strip colour %s = mesh vertex colour %s"), *C.ToString(), *V0.ToString()),
			FMath::Abs(V0.R - C.R) <= 1 && FMath::Abs(V0.G - C.G) <= 1 && FMath::Abs(V0.B - C.B) <= 1 && FMath::Abs(V0.A - C.A) <= 1);
	}
	else
	{
		SSXTest::Skip(*this, TEXT("mesh vertex colours not CPU-readable: colour check skipped"));
	}
	return true;
}

#endif
