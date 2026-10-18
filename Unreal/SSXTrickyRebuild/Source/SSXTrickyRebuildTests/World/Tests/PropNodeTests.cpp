#include "Dom/JsonObject.h"
#include "Effects/TrackEffects.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/PatchWorld.h"
#include "World/PropNodes.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXPropNodes;

namespace SSXPropNodeTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FLcg
	{
		uint32 X;
		explicit FLcg(uint32 Seed) : X(Seed) {}
		uint32 U32() { X = X * 1103515245u + 12345u; return X >> 8; }
		double U01() { return double(U32()) / double(1 << 24); }
	};

	TSharedPtr<FJsonObject> LoadRef(FAutomationTestBase* T)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("prop_anim_reference.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			SSXTest::Skip(*T, TEXT("skipped: no SimData/prop_anim_reference.json (run tools/make_prop_anim_reference.py)"));
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			T->AddError(TEXT("prop_anim_reference.json does not parse"));
			return nullptr;
		}
		return Root;
	}

	TArray<double> Nums(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<double> Out;
		for (const TSharedPtr<FJsonValue>& V : A) { Out.Add(V->AsNumber()); }
		return Out;
	}
	TArray<double> Nums(const TSharedPtr<FJsonObject>& O, const TCHAR* F) { return Nums(O->GetArrayField(F)); }
	FVector Vec(const TArray<double>& A, int32 I = 0) { return FVector(A[I], A[I + 1], A[I + 2]); }
}

using namespace SSXPropNodeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldCrowdTest, "SSX.World.Crowd", SSXPropNodeTests::Flags)
bool FSSXWorldCrowdTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref) { return true; }
	int32 People = 0, Match = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("crowd_ram")))
	{
		const TArray<double> P = Nums(V->AsArray());
		++People;
		Match += FCrowdBox::FrameOf(int32(P[0]), int32(P[1]), int32(P[2])) == int32(P[3]);
	}
	TestEqual(TEXT("RAM crowd people show the predicted frame"), Match, People);
	int32 Steps = 0, StepMatch = 0;
	for (const TSharedPtr<FJsonValue>& RV : Ref->GetArrayField(TEXT("crowd_runs")))
	{
		const TSharedPtr<FJsonObject> R = RV->AsObject();
		FLcg Rng(uint32(R->GetNumberField(TEXT("seed"))));
		FCrowdBox Box;
		const int32 Rows = int32(R->GetNumberField(TEXT("rows"))), Cols = int32(R->GetNumberField(TEXT("cols")));
		Box.Init(Rows, Cols, [&Rng]() { return Rng.U32(); });
		const TArray<TSharedPtr<FJsonValue>>& Frames = R->GetArrayField(TEXT("frames"));
		auto Same = [&](int32 K)
		{
			const TArray<double> F = Nums(Frames[K]->AsArray());
			bool bOk = true;
			for (int32 I = 0; I < Rows * Cols; ++I) { bOk &= Box.Frames[I] == int32(F[I]); }
			return bOk;
		};
		StepMatch += Same(0);
		++Steps;
		for (int32 F = 1, K = 1; F <= 1200; ++F)
		{
			const TPair<double, int32> H(F < 900 ? 500.0 : 30000.0, 0);
			Box.Update(F, F < 120, MakeArrayView(&H, 1), [&Rng]() { return Rng.U32(); });
			if (F % 5 == 0 && Frames.IsValidIndex(K))
			{
				StepMatch += Same(K++);
				++Steps;
			}
		}
	}
	TestEqual(TEXT("CrowdBox runs match the model at every step"), StepMatch, Steps);
	AddInfo(FString::Printf(TEXT("crowd: %d / %d RAM people, %d / %d model steps"), Match, People, StepMatch, Steps));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldAnimObjectTest, "SSX.World.AnimObject", SSXPropNodeTests::Flags)
bool FSSXWorldAnimObjectTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref) { return true; }
	TMap<FString, FPropAnimData> Data;
	int32 Samples = 0, Good = 0;
	double MaxErr = 0.0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("anim_samples")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		const FString Track = S->GetStringField(TEXT("track"));
		FPropAnimData* D = Data.Find(Track);
		if (!D)
		{
			D = &Data.Add(Track);
			FString Err;
			if (!D->Load(FPropAnimData::DefaultPath(Track), &Err))
			{
				AddError(FString::Printf(TEXT("%s: %s"), *Track, *Err));
				return false;
			}
		}
		const int32 Model = int32(S->GetNumberField(TEXT("model"))), Obj = int32(S->GetNumberField(TEXT("object")));
		const FPropObjectInfo& O = D->Objects[D->Models[Model].FirstObject + Obj];
		if (!TestTrue(TEXT("sampled object is animated"), D->Anims.IsValidIndex(O.Anim))) { continue; }
		const FNodeAnim& A = D->Anims[O.Anim];
		const TArray<double> Ts = Nums(S, TEXT("t"));
		const TArray<TSharedPtr<FJsonValue>>& Ch = S->GetArrayField(TEXT("ch"));
		const TArray<TSharedPtr<FJsonValue>>& Ms = S->GetArrayField(TEXT("m"));
		int32 Cache[6] = { 0, 0, 0, 0, 0, 0 };
		for (int32 I = 0; I < Ts.Num(); ++I)
		{
			const TArray<double> C = Nums(Ch[I]->AsArray());
			const TArray<double> M = Nums(Ms[I]->AsArray());
			const FMatrix Mm = A.Matrix(Ts[I], Cache);
			double E = 0.0;
			for (int32 K = 0; K < 6; ++K) { E = FMath::Max(E, FMath::Abs(A.Channel(K, Ts[I]) - C[K]) / FMath::Max(1.0, FMath::Abs(C[K]))); }
			for (int32 K = 0; K < 16; ++K) { E = FMath::Max(E, FMath::Abs(Mm.M[K / 4][K % 4] - M[K]) / FMath::Max(1.0, FMath::Abs(M[K]))); }
			MaxErr = FMath::Max(MaxErr, E);
			Good += E < 1e-4;
			++Samples;
		}
	}
	TestEqual(TEXT("keyframe channels / matrices match the model"), Good, Samples);
	int32 Runs = 0, RunsOk = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("anim_runs")))
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		FAnimObjectRecord Rec;
		Rec.Mode = int32(R->GetNumberField(TEXT("mode")));
		Rec.Start = R->GetNumberField(TEXT("start"));
		Rec.End = R->GetNumberField(TEXT("end"));
		Rec.Speed = R->GetNumberField(TEXT("speed"));
		Rec.Direction = int32(R->GetNumberField(TEXT("dir")));
		FAnimObject A;
		A.Init(Rec, 60.0, [](double Lo, double) { return Lo; }, false);
		const TArray<double> Ts = Nums(R, TEXT("t"));
		int32 Done = -1;
		bool bOk = true;
		for (int32 F = 0; F < Ts.Num(); ++F)
		{
			if (A.Tick() && Done < 0) { Done = F; }
			bOk &= FMath::Abs(A.T - Ts[F]) < 1e-9;
		}
		bOk &= Done == int32(R->GetNumberField(TEXT("done")));
		RunsOk += bOk;
		++Runs;
		if (!bOk) { AddError(FString::Printf(TEXT("AnimObject run %s differs"), *R->GetStringField(TEXT("name")))); }
	}
	SSXEffects::FEffectData Fe;
	FPropAnimData FeAnim;
	FString Err;
	int32 DoorChecks = 0, DoorOk = 0;
	if (Fe.Load(SSXEffects::FEffectData::DefaultPath(TEXT("ssxfe")), &Err) && FeAnim.Load(FPropAnimData::DefaultPath(TEXT("ssxfe")), &Err))
	{
		SSXEffects::FEffectWorld W;
		W.Init(&Fe, 0, 11);
		TMap<int32, FAnimObject> Nodes;
		W.OnOutcome = [&](const SSXEffects::FOutcome& O)
		{
			if (O.Kind == SSXEffects::EOutcome::Node && O.A == SSXEffects::TypeAnimObject)
			{
				FAnimObjectRecord Rec;
				FAnimObjectRecord::Parse(Fe.RecordBytes(Fe.Records[O.Record]), Fe.Records[O.Record].Size, Rec);
				FAnimObject A;
				A.Init(Rec, FeAnim.ModelOfInstance(O.Instance)->EndFrame, [](double Lo, double) { return Lo; }, false);
				Nodes.Add(O.Instance, A);
			}
			else if (O.Kind == SSXEffects::EOutcome::NodeDestroyed)
			{
				Nodes.Remove(O.Instance);
			}
		};
		auto Tz = [&](int32 Inst)
		{
			const FAnimObject* A = Nodes.Find(Inst);
			const double T = A ? A->T : 0.0;
			const FPropModelInfo* M = FeAnim.ModelOfInstance(Inst);
			const FPropObjectInfo& O = FeAnim.Objects[M->FirstObject];
			return FeAnim.Anims.IsValidIndex(O.Anim) ? FeAnim.Anims[O.Anim].Channel(2, T) : 0.0;
		};
		auto Step = [&](int32 Frames)
		{
			for (int32 F = 0; F < Frames; ++F)
			{
				W.Frame();
				TArray<int32> Done;
				for (TPair<int32, FAnimObject>& P : Nodes) { if (P.Value.Tick()) { Done.Add(P.Key); } }
				for (int32 I : Done) { W.NodeDone(I); }
			}
		};
		TArray<TSharedPtr<FJsonObject>> Pr;
		for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("doors")))
		{
			if (V->AsObject()->GetStringField(TEXT("menu")) == TEXT("PR")) { Pr.Add(V->AsObject()); }
		}
		auto Check = [&](const TCHAR* What, int32 RefIndex)
		{
			for (const TSharedPtr<FJsonObject>& D : Pr)
			{
				const int32 Inst = int32(D->GetNumberField(TEXT("inst")));
				const double Want = Nums(D, TEXT("tz"))[RefIndex], Got = Tz(Inst);
				++DoorChecks;
				const bool bOk = FMath::Abs(Want - Got) < 1e-3;
				DoorOk += bOk;
				if (!bOk) { AddError(FString::Printf(TEXT("door %s %s: tz %.2f, want %.2f"), *D->GetStringField(TEXT("name")), What, Got, Want)); }
			}
		};
		W.CallFunction(TEXT("onPROpen"));
		Check(TEXT("open frame 0"), 0);
		Step(20);
		Check(TEXT("open frame 10"), 1);
		Step(20);
		Check(TEXT("open frame 20"), 2);
		Step(120);
		Check(TEXT("held open 2 s later"), 2);
		W.CallFunction(TEXT("onPRClose"));
		Check(TEXT("close frame 20"), 2);
		Step(20);
		Check(TEXT("close frame 30"), 3);
		Step(30);
		Check(TEXT("closed"), 4);
		const int32 TitleDoors[2] = { 325, 326 };
		auto Offset = [&](int32 Inst)
		{
			const FAnimObject* A = Nodes.Find(Inst);
			const double T = A ? A->T : 0.0;
			const FPropModelInfo* M = FeAnim.ModelOfInstance(Inst);
			double Best = 0.0;
			for (int32 O = 0; M && O < M->NumObjects; ++O)
			{
				const FPropObjectInfo& Ob = FeAnim.Objects[M->FirstObject + O];
				if (!FeAnim.Anims.IsValidIndex(Ob.Anim)) { continue; }
				for (int32 K = 0; K < 6; ++K) { Best = FMath::Max(Best, FMath::Abs(FeAnim.Anims[Ob.Anim].Channel(K, T) - FeAnim.Anims[Ob.Anim].Channel(K, 0.0))); }
			}
			return Best;
		};
		W.CallFunction(TEXT("onTitleOpen"));
		Step(60);
		for (int32 I : TitleDoors)
		{
			++DoorChecks;
			const bool bOk = Nodes.Contains(I) && Offset(I) > 1000.0;
			DoorOk += bOk;
			if (!bOk) { AddError(FString::Printf(TEXT("title door %d not open 1 s after onTitleOpen (offset %.1f)"), I, Offset(I))); }
		}
		Step(62);
		W.CallFunction(TEXT("onTitleStop"));
		for (int32 I : TitleDoors)
		{
			++DoorChecks;
			const bool bOk = !Nodes.Contains(I) && Offset(I) < 1e-3;
			DoorOk += bOk;
			if (!bOk) { AddError(FString::Printf(TEXT("title door %d not back at its static pose after the 2 s play-once"), I)); }
		}
		W.CallFunction(TEXT("onPROpen"));
		Step(120);
		for (const TSharedPtr<FJsonObject>& D : Pr)
		{
			++DoorChecks;
			const bool bOk = Nodes.Contains(int32(D->GetNumberField(TEXT("inst"))));
			DoorOk += bOk;
			if (!bOk) { AddError(TEXT("mode door node not kept by its empty slot-3 effect")); }
		}
	}
	else
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("FE doors skipped: %s"), *Err));
	}
	TestEqual(TEXT("AnimObject runs match the model"), RunsOk, Runs);
	TestEqual(TEXT("FE door poses"), DoorOk, DoorChecks);
	AddInfo(FString::Printf(TEXT("keyframes %d / %d samples on %d files (max rel err %.2e); runs %d / %d; doors %d / %d"),
		Good, Samples, Data.Num(), MaxErr, RunsOk, Runs, DoorOk, DoorChecks));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXDynamicRollerTest, "SSX.Dynamic.Roller", SSXPropNodeTests::Flags)
bool FSSXDynamicRollerTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref) { return true; }
	const TSharedPtr<FJsonObject> P = Ref->GetObjectField(TEXT("dynamic"))->GetObjectField(TEXT("pylon"));
	const TArray<double> Com = Nums(P, TEXT("com"));
	FMat3 InvI;
	const TArray<TSharedPtr<FJsonValue>>& Rows = P->GetArrayField(TEXT("inv_inertia"));
	for (int32 I = 0; I < 3; ++I)
	{
		const TArray<double> R = Nums(Rows[I]->AsArray());
		for (int32 J = 0; J < 3; ++J) { InvI.M[I][J] = R[J]; }
	}
	FPatchWorld World;
	FString Err;
	if (World.LoadInstances(FPatchWorld::DefaultInstancePath(TEXT("alaska")), &Err))
	{
		const int32 K = World.FindInstance(int32(P->GetNumberField(TEXT("inst"))));
		bool bTree = false;
		if (K != INDEX_NONE)
		{
			const FSimInstance& SI = World.GetInstance(K);
			for (int32 J = 0; J < SI.NumNodes && !bTree; ++J)
			{
				const FSimInstanceNode& N = World.GetInstanceNode(SI.FirstNode + J);
				if (N.Tree == INDEX_NONE) { continue; }
				const FSimSphereTree& T = World.GetSphereTree(N.Tree);
				bTree = true;
				TestTrue(TEXT("tree centre of mass parsed"), T.Com.Equals(Vec(Com), 1e-3));
				bool bInv = true;
				for (int32 Q = 0; Q < 9; ++Q) { bInv &= FMath::Abs(T.InvInertia[Q] - InvI.M[Q / 3][Q % 3]) < 1e-6 * FMath::Max(1.0, FMath::Abs(InvI.M[Q / 3][Q % 3])); }
				TestTrue(TEXT("tree inverse inertia parsed"), bInv);
			}
		}
		TestTrue(TEXT("pylon instance has a sphere tree"), bTree);
	}
	else
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("tree parse skipped: %s"), *Err));
	}
	FRollerRecord Rec;
	Rec.Mass = P->GetNumberField(TEXT("mass"));
	FRigidBody Body;
	Body.Init(Rec.Mass, InvI, Vec(Com), FVector(0, 0, 40), FQuat4d::Identity);
	FContactRecord C;
	C.VHat = FVector(1, 0, 0);
	C.N = FVector(-1, 0, 0);
	C.A = 2000.0;
	FRoller R;
	R.Init(Rec, Body, &C);
	TestTrue(TEXT("kick v0 = 0.5 a v_hat + (0, 0, min(100 + 5000 / m, 600))"), R.Body.V.Equals(Vec(Nums(P, TEXT("v0"))), 1e-6));
	TestTrue(TEXT("kick spin w = 10 n"), R.Body.W.Equals(Vec(Nums(P, TEXT("omega0"))), 1e-6));
	auto Floor = [](const FRigidBody& B, FBodyHit& H)
	{
		if (B.X.Z >= 30.0) { return false; }
		H.Depth = 30.0 - B.X.Z; H.N = FVector(0, 0, 1); H.C = FVector(B.X.X, B.X.Y, 0.0);
		return true;
	};
	auto None = [](const FRigidBody&, FBodyHit&) { return false; };
	int32 Frames = 0;
	double Peak = 0.0, MaxErr = 0.0;
	TArray<TArray<double>> Track;
	for (const TSharedPtr<FJsonValue>& V : P->GetArrayField(TEXT("track"))) { Track.Add(Nums(V->AsArray())); }
	int32 Ti = 0;
	while (!R.Tick(Floor, None) && Frames < 2000)
	{
		++Frames;
		Peak = FMath::Max(Peak, R.Body.X.Z);
		if (Ti < Track.Num() && int32(Track[Ti][0]) == Frames)
		{
			MaxErr = FMath::Max(MaxErr, FVector::Dist(R.Body.X, Vec(Track[Ti], 1)));
			++Ti;
		}
	}
	TestEqual(TEXT("frames to rest"), Frames, int32(P->GetNumberField(TEXT("frames"))));
	TestTrue(TEXT("peak height"), FMath::Abs(Peak - P->GetNumberField(TEXT("peak"))) < 1e-3);
	TestTrue(TEXT("trajectory follows the model"), MaxErr < 1e-3);
	TestTrue(TEXT("at rest: asleep"), R.bAsleep);
	FVector J;
	const bool bHit = RiderHitsBody(R.Body, FVector(1500, 0, 0), 120.0, R.Body.X - FVector(20, 0, 0), FVector(-1, 0, 0), &J);
	TestTrue(TEXT("rider impulse on the body (restitution 0.3)"), bHit && J.Equals(Vec(Nums(P, TEXT("hit_J"))), 1e-6) && R.Body.V.Equals(Vec(Nums(P, TEXT("hit_v"))), 1e-6));
	FRiderPropInfo Info;
	Info.Flags = 0x80u << 16;
	Info.Mass = Rec.Mass;
	Info.Restitution = 0.2;
	Info.bMovable = true;
	const SSXGround::FPropResponse Resp = SSXGround::ComputePropResponse(Info, 1.0, FVector(-1, 0, 0), FVector(2000, 0, 0),
		ERiderMotion::Air, ERiderControl(0), FVector(0, 0, 1), 120.0);
	TestTrue(TEXT("movable-branch dv as the model (~0)"), FMath::Abs(Resp.Dv - P->GetNumberField(TEXT("movable_dv"))) < 1e-6 && FMath::Abs(Resp.Dv) < 1.0);
	AddInfo(FString::Printf(TEXT("pylon m %.1f: v0 %s, peak %.1f cm, rest after %d frames (%.2f s), trajectory max err %.2e cm, rider J %s, movable dv %.5f"),
		Rec.Mass, *Vec(Nums(P, TEXT("v0"))).ToString(), Peak, Frames, Frames / 60.0, MaxErr, *J.ToString(), Resp.Dv));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXDynamicDebrisTest, "SSX.Dynamic.Debris", SSXPropNodeTests::Flags)
bool FSSXDynamicDebrisTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref) { return true; }
	const TSharedPtr<FJsonObject> D = Ref->GetObjectField(TEXT("dynamic"))->GetObjectField(TEXT("debris"));
	FMeshAnimRecord Rec;
	Rec.Keep = 0; Rec.Gravity = 0.05; Rec.Seconds = 2.0; Rec.Spread = FVector(800, 800, 900); Rec.VelScale = 1.0;
	FLcg Rng(uint32(D->GetNumberField(TEXT("seed"))));
	TArray<TPair<FVector, FQuat4d>> Start;
	for (int32 K = 0; K < 3; ++K) { Start.Add({ FVector(100.0 * K, 50.0, 300.0), FQuat4d::Identity }); }
	FContactRecord C;
	C.VHat = FVector(0, 1, 0); C.N = FVector(0, -1, 0); C.A = 400.0;
	FMeshAnim M;
	M.Init(Rec, Start, FMatrix::Identity, &C, [&Rng]() { return Rng.U01(); }, [&Rng]() { return int32(Rng.U32() & 1); });
	TMap<int32, TArray<double>> Samples;
	for (const TSharedPtr<FJsonValue>& V : D->GetArrayField(TEXT("samples")))
	{
		const TArray<double> S = Nums(V->AsArray());
		Samples.Add(int32(S[0]), S);
	}
	int32 Done = -1;
	double MaxErr = 0.0;
	for (int32 F = 0; F < 130; ++F)
	{
		if (M.Tick() != FMeshAnim::EResult::None && Done < 0) { Done = F; }
		if (const TArray<double>* S = Samples.Find(F))
		{
			for (int32 K = 0; K < M.Pieces.Num(); ++K)
			{
				const FMeshAnim::FPiece& Pc = M.Pieces[K];
				const int32 B = 1 + 7 * K;
				MaxErr = FMath::Max(MaxErr, FVector::Dist(Pc.Pos, Vec(*S, B)));
				MaxErr = FMath::Max(MaxErr, FMath::Abs(Pc.Q.W - (*S)[B + 3]) + FMath::Abs(Pc.Q.X - (*S)[B + 4]) + FMath::Abs(Pc.Q.Y - (*S)[B + 5]) + FMath::Abs(Pc.Q.Z - (*S)[B + 6]));
			}
		}
	}
	TestEqual(TEXT("debris time up (2 s)"), Done, int32(D->GetNumberField(TEXT("done"))));
	TestTrue(TEXT("pieces follow the model"), MaxErr < 1e-6);
	AddInfo(FString::Printf(TEXT("debris: 3 pieces, done at frame %d, max err %.2e"), Done, MaxErr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXDynamicGlassTest, "SSX.Dynamic.Glass", SSXPropNodeTests::Flags)
bool FSSXDynamicGlassTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadRef(this);
	if (!Ref) { return true; }
	const TSharedPtr<FJsonObject> G = Ref->GetObjectField(TEXT("dynamic"))->GetObjectField(TEXT("glass"));
	SSXEffects::FEffectData Data;
	FString Err;
	if (!Data.Load(SSXEffects::FEffectData::DefaultPath(TEXT("megaple")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s"), *Err));
		return true;
	}
	const TArray<double> Panes = Nums(G, TEXT("panes")), Junk = Nums(G, TEXT("junk"));
	TSet<int32> JunkSet;
	for (double J : Junk) { JunkSet.Add(int32(J)); }
	int32 Ok = 0;
	for (double Pd : Panes)
	{
		const int32 Pane = int32(Pd);
		SSXEffects::FEffectWorld W;
		W.Init(&Data, 0, 8);
		W.bKeepLog = true;
		SSXEffects::FEffectRider Rider;
		W.Touch(Pane, Rider, true, -1.0, 300.0);   // |v_hat . n| a = 300 cm/s >= 139: breaks
		bool bCracked = false, bCrackSound = false, bBreakSound = false, bHidden = false, bDebris = false;
		for (const SSXEffects::FOutcome& O : W.Log)
		{
			bCracked |= O.Kind == SSXEffects::EOutcome::Node && O.Instance == Pane && O.A == SSXEffects::TypeCracked;
			bCrackSound |= O.Kind == SSXEffects::EOutcome::Sound && O.B == 65;
		}
		W.Log.Reset();
		W.Frame();
		for (const SSXEffects::FOutcome& O : W.Log)
		{
			bBreakSound |= O.Kind == SSXEffects::EOutcome::Sound && O.B == 64;
			bHidden |= O.Kind == SSXEffects::EOutcome::Hide && O.Instance == Pane;
			bDebris |= O.Kind == SSXEffects::EOutcome::Node && O.A == SSXEffects::TypeMeshAnim && JunkSet.Contains(O.Instance);
		}
		const bool bAll = bCracked && bCrackSound && bBreakSound && bHidden && bDebris && W.IsScriptHidden(Pane);
		Ok += bAll;
		if (!bAll)
		{
			AddError(FString::Printf(TEXT("pane %d: cracked %d sound65 %d sound64 %d hidden %d debris %d"), Pane, bCracked, bCrackSound, bBreakSound, bHidden, bDebris));
		}
	}
	TestEqual(TEXT("glass chain: crack (65), break (64), pane gone, junk MeshAnim"), Ok, Panes.Num());
	TestTrue(TEXT("break speed 139 cm/s"), FMath::Abs(G->GetNumberField(TEXT("break_speed")) - 5.0 / 0.036) < 1e-3);
	AddInfo(FString::Printf(TEXT("glass: %d / %d Megaplex panes follow the chain"), Ok, Panes.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXDynamicMovingObjectsTest, "SSX.Dynamic.MovingObjects", SSXPropNodeTests::Flags)
bool FSSXDynamicMovingObjectsTest::RunTest(const FString&)
{
	FMovingObjects M;
	const int32 A = M.Add(1, FMovingObjects::EKind::Rider, FBox(FVector(0), FVector(10)));
	const int32 B = M.Add(2, FMovingObjects::EKind::Roller, FBox(FVector(5), FVector(20)));
	const int32 C = M.Add(3, FMovingObjects::EKind::Roller, FBox(FVector(5, 30, 5), FVector(20, 40, 20)));
	TArray<int32> P;
	M.Pairs(A, P);
	TestTrue(TEXT("overlap on x, y and z pairs"), P.Num() == 1 && P[0] == B);
	M.Update(C, FBox(FVector(5, 5, 5), FVector(9, 9, 9)));
	M.Pairs(A, P);
	TestEqual(TEXT("moved proxy pairs"), P.Num(), 2);
	M.Remove(B);
	M.Pairs(A, P);
	TestTrue(TEXT("removed proxy unpaired"), P.Num() == 1 && P[0] == C);
	for (int32 I = M.Num(); I < FMovingObjects::Capacity; ++I) { M.Add(I, FMovingObjects::EKind::Roller, FBox(FVector(0), FVector(1))); }
	TestEqual(TEXT("capacity 256"), M.Add(999, FMovingObjects::EKind::Roller, FBox(FVector(0), FVector(1))), int32(INDEX_NONE));
	return true;
}

#endif
