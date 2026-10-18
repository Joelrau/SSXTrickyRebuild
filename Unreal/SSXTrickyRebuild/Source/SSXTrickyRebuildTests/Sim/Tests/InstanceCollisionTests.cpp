#include "Effects/TrackEffects.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/RiderSim.h"
#include "Sim/SurfaceTable.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags InstTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void InstReport(FAutomationTestBase* Test, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Collision.Instances] %s"), *Msg);
	}

	struct FInstCsv
	{
		TMap<FString, int32> Col;
		TArray<TArray<FString>> Rows;
		bool Load(const FString& Path)
		{
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 2)
			{
				return false;
			}
			TArray<FString> H;
			Lines[0].ParseIntoArray(H, TEXT(","), false);
			for (int32 I = 0; I < H.Num(); ++I)
			{
				Col.Add(H[I].TrimStartAndEnd(), I);
			}
			for (int32 I = 1; I < Lines.Num(); ++I)
			{
				TArray<FString> C;
				Lines[I].ParseIntoArray(C, TEXT(","), false);
				if (C.Num() == H.Num())
				{
					Rows.Add(MoveTemp(C));
				}
			}
			return true;
		}
		const FString& S(const TArray<FString>& R, const TCHAR* Name) const { return R[Col.FindChecked(Name)]; }
		double D(const TArray<FString>& R, const TCHAR* Name) const { return FCString::Atod(*S(R, Name)); }
		int32 I(const TArray<FString>& R, const TCHAR* Name) const { return FCString::Atoi(*S(R, Name)); }
		FVector V(const TArray<FString>& R, const TCHAR* X, const TCHAR* Y, const TCHAR* Z) const { return FVector(D(R, X), D(R, Y), D(R, Z)); }
	};

	FString SimData(const FString& File) { return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), File); }

	void ParseSubs(const FString& Txt, FRiderSphereSet& Shape)
	{
		TArray<FString> Subs;
		Txt.ParseIntoArray(Subs, TEXT(";"));
		for (const FString& Sub : Subs)
		{
			TArray<FString> V;
			Sub.ParseIntoArray(V, TEXT(" "));
			if (V.Num() == 4 && Shape.Count < FRiderSphereSet::MaxSpheres)
			{
				Shape.Centres[Shape.Count] = FVector(FCString::Atod(*V[0]), FCString::Atod(*V[1]), FCString::Atod(*V[2]));
				Shape.Radii[Shape.Count] = FCString::Atod(*V[3]);
				++Shape.Count;
			}
		}
	}

	FPatchWorld* WorldFor(TMap<FString, TUniquePtr<FPatchWorld>>& Worlds, const FString& Track, FAutomationTestBase* Test)
	{
		TUniquePtr<FPatchWorld>& W = Worlds.FindOrAdd(Track);
		if (!W)
		{
			W = MakeUnique<FPatchWorld>();
			FString Err;
			if (!W->LoadInstances(FPatchWorld::DefaultInstancePath(Track), &Err))
			{
				Test->AddError(FString::Printf(TEXT("%s: %s"), *Track, *Err));
				W.Reset();
				return nullptr;
			}
		}
		return W.Get();
	}

	/** Axis-aligned cube of node-space triangles centred at C with half size H (outward normals). */
	void AddCube(TArray<FSimInstanceTri>& Tris, const FVector& C, double H)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (double Sign : { 1.0, -1.0 })
			{
				FVector N = FVector::ZeroVector;
				N[Axis] = Sign;
				const FVector U = FVector(N.Y, N.Z, N.X), V = FVector::CrossProduct(N, U);
				const FVector F = C + N * H;
				const FVector P[4] = { F + (-U - V) * H, F + (U - V) * H, F + (U + V) * H, F + (-U + V) * H };
				Tris.Add({ { P[0], P[1], P[2] }, N });
				Tris.Add({ { P[0], P[2], P[3] }, N });
			}
		}
	}

	FSimInstance& AddInst(TArray<FSimInstance>& Insts, TArray<FSimInstanceNode>& Nodes, int32 Source, uint32 HighFlags, int32 Surface,
	                      int32 Kind, const FBox& Bounds)
	{
		FSimInstance& I = Insts.AddDefaulted_GetRef();
		I.SourceIndex = Source;
		I.Flags = HighFlags << 16;
		I.Surface = Surface;
		I.ModelType = Kind;
		I.Bounds = Bounds;
		I.FirstNode = Nodes.Num();
		I.NumNodes = 1;
		FSimInstanceNode& N = Nodes.AddDefaulted_GetRef();
		N.Kind = Kind;
		return I;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInstanceCountsTest, "SSX.Collision.Instances.Counts", InstTestFlags)
bool FSSXInstanceCountsTest::RunTest(const FString& Parameters)
{
	FInstCsv Csv, TreeCsv;
	if (!Csv.Load(SimData(TEXT("instcol_counts.csv"))) || !TreeCsv.Load(SimData(TEXT("instcol_trees.csv"))))
	{
		SSXTest::Skip(*this, TEXT("SimData/instcol_counts.csv / instcol_trees.csv missing (tools/make_instance_collision_reference.py); skipped"));
		return true;
	}
	const TMap<FString, int32> NotesCollidable = { { TEXT("gari"), 3098 }, { TEXT("alaska"), 1338 }, { TEXT("megaple"), 801 }, { TEXT("mesa"), 2606 } };
	const TMap<FString, int32> NotesSurfaced = { { TEXT("gari"), 0 }, { TEXT("alaska"), 2 }, { TEXT("aloha"), 21 }, { TEXT("elysium"), 5 },
		{ TEXT("megaple"), 88 }, { TEXT("merquer"), 163 }, { TEXT("snow"), 27 }, { TEXT("mesa"), 1 }, { TEXT("untrack"), 1 } };
	TMap<FString, TUniquePtr<FPatchWorld>> Worlds;
	int32 Tracks = 0, TreeRows = 0, TreeBad = 0, TotalLeaves = 0;
	for (const TArray<FString>& R : Csv.Rows)
	{
		const FString Track = Csv.S(R, TEXT("track"));
		const FPatchWorld* W = WorldFor(Worlds, Track, this);
		if (!W)
		{
			continue;
		}
		int32 Coll = 0, Surf = 0, Unsurf = 0, Alt = 0, TreeInst = 0, Nodes = 0, TwoSided = 0, MeshNodes = 0;
		for (int32 K = 0; K < W->NumInstances(); ++K)
		{
			const FSimInstance& I = W->GetInstance(K);
			Coll += I.IsCollidable();
			Surf += I.Surface != -1;
			Unsurf += I.Surface == -1;
			Alt += I.SetBits == FSimInstance::SetAlternate;
			TreeInst += I.ModelType == 3;
			Nodes += I.NumNodes;
			for (int32 N = 0; N < I.NumNodes; ++N)
			{
				const FSimInstanceNode& Node = W->GetInstanceNode(I.FirstNode + N);
				MeshNodes += Node.Kind == 1;
				TwoSided += Node.Kind == 1 && (Node.Bits & FSimInstanceNode::BitTwoSided) != 0;
			}
		}
		int32 Leaves = 0;
		for (int32 T = 0; T < W->NumSphereTrees(); ++T)
		{
			Leaves += W->GetSphereTree(T).CountLeaves();
		}
		auto Eq = [&](const TCHAR* What, int32 Got, int32 Want) { TestEqual(*FString::Printf(TEXT("%s %s"), *Track, What), Got, Want); };
		Eq(TEXT("collidable"), Coll, Csv.I(R, TEXT("collidable")));
		Eq(TEXT("entries"), W->NumInstances(), Csv.I(R, TEXT("collidable")));
		Eq(TEXT("surfaced"), Surf, Csv.I(R, TEXT("surfaced")));
		Eq(TEXT("unsurfaced"), Unsurf, Csv.I(R, TEXT("unsurfaced")));
		Eq(TEXT("modes-3/5 only"), Alt, Csv.I(R, TEXT("alt_only")));
		Eq(TEXT("sphere-tree instances"), TreeInst, Csv.I(R, TEXT("sphere_tree")));
		Eq(TEXT("geometry nodes"), Nodes, Csv.I(R, TEXT("nodes")));
		Eq(TEXT("two-sided mesh nodes"), TwoSided, Csv.I(R, TEXT("two_sided")));
		Eq(TEXT("trees"), W->NumSphereTrees(), Csv.I(R, TEXT("trees")));
		Eq(TEXT("tree leaf spheres"), Leaves, Csv.I(R, TEXT("leaves")));
		if (const int32* N = NotesCollidable.Find(Track)) { Eq(TEXT("collidable"), Coll, *N); }
		if (const int32* N = NotesSurfaced.Find(Track)) { Eq(TEXT("surfaced"), Surf, *N); }
		if (Track == TEXT("gari"))
		{
			Eq(TEXT("trees (RAM: 56)"), W->NumSphereTrees(), 56);
			Eq(TEXT("tree instances (150)"), TreeInst, 150);
			Eq(TEXT("mesh nodes (2599)"), MeshNodes, 2599);
			Eq(TEXT("two-sided mesh nodes (483)"), TwoSided, 483);
		}
		if (Track == TEXT("merquer"))
		{
			Eq(TEXT("tree instances (1271)"), TreeInst, 1271);
		}
		TotalLeaves += Leaves;
		InstReport(this, FString::Printf(TEXT("%s: %d collidable (%d surfaced, %d modes-3/5 only), %d nodes (%d two-sided mesh), %d trees / %d leaf spheres"),
			*Track, Coll, Surf, Alt, Nodes, TwoSided, W->NumSphereTrees(), Leaves));
		++Tracks;
	}
	for (const TArray<FString>& R : TreeCsv.Rows)
	{
		const FPatchWorld* W = WorldFor(Worlds, TreeCsv.S(R, TEXT("track")), this);
		const int32 T = TreeCsv.I(R, TEXT("tree"));
		++TreeRows;
		if (!W || T >= W->NumSphereTrees())
		{
			++TreeBad;
			continue;
		}
		const FSimSphereTree& Tree = W->GetSphereTree(T);
		TreeBad += Tree.Depth != TreeCsv.I(R, TEXT("depth")) || Tree.CountLeaves() != TreeCsv.I(R, TEXT("leaves"))
			|| !Tree.Centre.Equals(TreeCsv.V(R, TEXT("cx"), TEXT("cy"), TEXT("cz")), 1e-6);
	}
	InstReport(this, FString::Printf(TEXT("%d tracks; %d trees vs the model (depth, root centre, leaves): %d mismatches; %d leaf spheres"),
		Tracks, TreeRows, TreeBad, TotalLeaves));
	TestEqual(TEXT("tree mismatches"), TreeBad, 0);
	TestTrue(TEXT("tracks checked"), Tracks >= 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInstanceReferenceTest, "SSX.Collision.Instances.Reference", InstTestFlags)
bool FSSXInstanceReferenceTest::RunTest(const FString& Parameters)
{
	FInstCsv Csv;
	if (!Csv.Load(SimData(TEXT("instcol_cases.csv"))))
	{
		SSXTest::Skip(*this, TEXT("SimData/instcol_cases.csv missing (tools/make_instance_collision_reference.py); skipped"));
		return true;
	}
	TMap<FString, TUniquePtr<FPatchWorld>> Worlds;
	const TCHAR* Names[5] = { TEXT("line"), TEXT("sphere"), TEXT("wall"), TEXT("prop"), TEXT("order") };
	int32 Cases[5] = {}, Hits[5] = {}, Bad[5] = {}, TreeHits = 0, BoxHits = 0, MeshHits = 0, LineKind2 = 0;
	double MaxDt[5] = {}, MaxDn[5] = {};
	for (const TArray<FString>& R : Csv.Rows)
	{
		const FString Query = Csv.S(R, TEXT("query"));
		int32 Q = 0;
		while (Q < 5 && Query != Names[Q]) { ++Q; }
		if (Q == 5)
		{
			continue;
		}
		FPatchWorld* W = WorldFor(Worlds, Csv.S(R, TEXT("track")), this);
		if (!W)
		{
			return false;
		}
		W->bAlternateSetMode = Csv.I(R, TEXT("alt")) != 0;
		const FVector P0 = Csv.V(R, TEXT("x0"), TEXT("y0"), TEXT("z0"));
		++Cases[Q];
		if (Q == 4)
		{
			const double Rad = Csv.D(R, TEXT("x1_or_r"));
			TArray<int32> Seq;
			const FBox Box(P0 - FVector(Rad), P0 + FVector(Rad));
			W->CollectLtgOrder(Box, Seq);
			TArray<FString> Ref;
			Csv.S(R, TEXT("extra")).ParseIntoArray(Ref, TEXT(" "));
			bool bOk = Ref.Num() == Seq.Num();
			for (int32 K = 0; bOk && K < Seq.Num(); ++K)
			{
				bOk = FCString::Atoi(*Ref[K]) == Seq[K];
			}
			Hits[Q] += Seq.Num();
			Bad[Q] += !bOk;
			continue;
		}
		FRiderWorldHit Hit;
		bool bHit;
		if (Q == 0)
		{
			bHit = W->InstanceLineQuery(P0, Csv.V(R, TEXT("x1_or_r"), TEXT("y1"), TEXT("z1")), Csv.D(R, TEXT("tref")), Hit);
		}
		else
		{
			FRiderSphereSet Shape;
			Shape.BoundCentre = P0;
			Shape.BoundRadius = Csv.D(R, TEXT("x1_or_r"));
			Shape.Mask = Csv.I(R, TEXT("mask"));
			FString SubTxt, FdTxt;
			Csv.S(R, TEXT("extra")).Split(TEXT("|"), &SubTxt, &FdTxt);
			ParseSubs(SubTxt, Shape);
			if (Q == 3)
			{
				FRiderPropInfo Info;
				bHit = W->QueryProps(Shape, Hit, Info);
				if (bHit && Info.Instance != Hit.InstanceIndex)
				{
					++Bad[Q];
				}
			}
			else
			{
				TArray<FString> F;
				FdTxt.ParseIntoArray(F, TEXT(" "));
				const FVector Fd = F.Num() == 3 ? FVector(FCString::Atod(*F[0]), FCString::Atod(*F[1]), FCString::Atod(*F[2])) : FVector::ZeroVector;
				bHit = W->InstanceSphereQuery(Shape, Csv.I(R, TEXT("fine")) != 0, F.Num() == 3 ? &Fd : nullptr, Q == 2,
					ESimTriVariant::Abs, Hit);
			}
		}
		const bool bRefHit = Csv.I(R, TEXT("hit")) != 0;
		Hits[Q] += bRefHit;
		bool bOk = bHit == bRefHit;
		if (bOk && bHit)
		{
			const FVector RefN = Csv.V(R, TEXT("nx"), TEXT("ny"), TEXT("nz"));
			const double Dt = FMath::Abs(Hit.T - Csv.D(R, TEXT("t")));
			const double Dn = (Hit.Normal - RefN).Size();
			MaxDt[Q] = FMath::Max(MaxDt[Q], Dt);
			MaxDn[Q] = FMath::Max(MaxDn[Q], Dn);
			bOk = Dt < (Q == 0 ? 1e-5 : 1e-3) && Dn < 1e-4 && Hit.Surface == Csv.I(R, TEXT("surface"))
				&& Hit.InstanceIndex == Csv.I(R, TEXT("instance"));
			if (Hit.Kind != Csv.I(R, TEXT("kind")))
			{
				const bool bLineKindRule = Q == 0 && Hit.Kind == 2 && Csv.I(R, TEXT("kind")) == 0;
				LineKind2 += bLineKindRule;
				bOk &= bLineKindRule;
			}
			if (Q == 3)
			{
				const int32 Type = W->GetInstance(W->FindInstance(Hit.InstanceIndex)).ModelType;
				TreeHits += Type == 3;
				BoxHits += Type == 2;
				MeshHits += Type == 1;
			}
		}
		if (!bOk)
		{
			++Bad[Q];
			if (Bad[0] + Bad[1] + Bad[2] + Bad[3] <= 8)
			{
				AddInfo(FString::Printf(TEXT("mismatch %s %s: hit %d/%d t %.6f/%s inst %d/%s kind %d/%s"), Names[Q], *Csv.S(R, TEXT("track")),
					bHit, bRefHit, Hit.T, *Csv.S(R, TEXT("t")), Hit.InstanceIndex, *Csv.S(R, TEXT("instance")), Hit.Kind, *Csv.S(R, TEXT("kind"))));
			}
		}
	}
	for (int32 Q = 0; Q < 4; ++Q)
	{
		InstReport(this, FString::Printf(TEXT("%s: %d cases (%d hits), %d mismatches, max |dt| %.2e, max |dn| %.2e"), Names[Q], Cases[Q], Hits[Q], Bad[Q], MaxDt[Q], MaxDn[Q]));
		TestEqual(*FString::Printf(TEXT("%s mismatches"), Names[Q]), Bad[Q], 0);
	}
	InstReport(this, FString::Printf(TEXT("prop hits by model type: mesh %d, box %d, sphere tree %d; line hits on animated-flag nodes (kind 2, model 0): %d"),
		MeshHits, BoxHits, TreeHits, LineKind2));
	InstReport(this, FString::Printf(TEXT("order: %d query boxes (%d visits), %d mismatches"), Cases[4], Hits[4], Bad[4]));
	TestEqual(TEXT("order mismatches"), Bad[4], 0);
	TestTrue(TEXT("cases with hits"), Hits[0] > 50 && Hits[1] > 50 && Hits[3] > 200 && TreeHits > 20 && BoxHits > 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInstanceProbeTest, "SSX.Collision.Instances.Probe", InstTestFlags)
bool FSSXInstanceProbeTest::RunTest(const FString& Parameters)
{
	FPatchWorld W;
	FString Err;
	if (!FPaths::FileExists(FPatchWorld::DefaultInstancePath(TEXT("megaple"))) || !W.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("megaple")), &Err))
	{
		SSXTest::Skip(*this, TEXT("SimData megaple.patches.bin / .instcol.bin missing; skipped"));
		return true;
	}
	int32 Probes = 0, OnProp = 0, Wrong = 0;
	for (int32 K = 0; K < W.NumInstances(); ++K)
	{
		const FSimInstance& I = W.GetInstance(K);
		if (I.Surface == -1 || !I.IsActive(false))
		{
			continue;
		}
		const FVector Top(I.Bounds.GetCenter().X, I.Bounds.GetCenter().Y, I.Bounds.Max.Z);
		FGroundProbeInput In;
		In.Position = Top + FVector(0, 0, 10);
		FGroundProbeResult Out;
		W.ProbeGround(In, Out);
		FRiderWorldHit PropHit;
		// 0x128AE8
		const FVector A(float(In.Position.X), float(In.Position.Y), float(In.Position.Z) + -float(FPatchWorld::ProbeBelow));
		const FVector B(float(In.Position.X), float(In.Position.Y), float(In.Position.Z) + float(FPatchWorld::ProbeAbove));
		if (!W.InstanceLineQuery(A, B, FPatchWorld::ProbeTRef, PropHit))
		{
			continue;
		}
		++Probes;
		if (Out.bFound && Out.Hit.InstanceIndex != INDEX_NONE)
		{
			++OnProp;
			Wrong += Out.Surface != PropHit.Surface || FMath::Abs(Out.Hit.T - PropHit.T) > 1e-9;
		}
		else if (Out.bFound && FMath::Abs(Out.Hit.T - FPatchWorld::ProbeTRef) > FMath::Abs(PropHit.T - FPatchWorld::ProbeTRef))
		{
			++Wrong;
		}
	}
	InstReport(this, FString::Printf(TEXT("megaple: %d probes over surfaced props, %d stand on the prop, %d wrong"), Probes, OnProp, Wrong));
	TestTrue(TEXT("probes stand on props"), OnProp > 20);
	TestEqual(TEXT("wrong selections"), Wrong, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInstanceRulesTest, "SSX.Collision.Instances.Rules", InstTestFlags)
bool FSSXInstanceRulesTest::RunTest(const FString& Parameters)
{
	FPatchWorld W;
	{
		TArray<FSimPatch> P;
		FSimPatch& Patch = P.AddDefaulted_GetRef();
		for (FVector& C : Patch.C) { C = FVector::ZeroVector; }
		Patch.C[0] = FVector(-5000, -5000, 0);
		Patch.C[4] = FVector(10000, 0, 0);
		Patch.C[1] = FVector(0, 10000, 0);
		Patch.Surface = 1;
		Patch.Flags = -1;
		W.SetPatches(MoveTemp(P));
	}
	TArray<FSimInstance> Insts;
	TArray<FSimInstanceNode> Nodes;
	TArray<FSimInstanceTri> Tris;
	const double X[5] = { -2000, 0, 2000, 4000, 6000 };
	for (int32 K = 0; K < 5; ++K)
	{
		const FVector C(X[K], 0, 300);
		FSimInstance& I = AddInst(Insts, Nodes, 10 + K, K == 3 ? 0x01u : 0xA1u, (K == 1 || K == 4) ? -1 : 13, 1, FBox(C - FVector(100), C + FVector(100)));
		I.SetBits = K == 2 ? FSimInstance::SetAlternate : FSimInstance::SetNormal;
		FSimInstanceNode& N = Nodes.Last();
		N.NodeToGame = FTranslationMatrix(C);
		N.GameToNode = FTranslationMatrix(-C);
		N.Bits = K == 4 ? FSimInstanceNode::BitTwoSided : 0;
		N.FirstTri = Tris.Num();
		AddCube(Tris, FVector::ZeroVector, 100.0);
		N.NumTris = Tris.Num() - N.FirstTri;
	}
	W.SetInstances(MoveTemp(Insts), MoveTemp(Nodes), MoveTemp(Tris));

	// 0x23B6B0
	auto Probe = [&W](double PX) { FGroundProbeInput In; In.Position = FVector(PX + 7.0, 3.0, 410); FGroundProbeResult Out; W.ProbeGround(In, Out); return Out; };
	FGroundProbeResult Out = Probe(-2000);
	TestTrue(TEXT("probe stands on the surfaced prop"), Out.bFound && Out.Hit.InstanceIndex == 10 && Out.Surface == 13 && Out.ContactKind == 0);
	// 0x25D9E0
	TestTrue(TEXT("prop top at z 400"), FMath::IsNearlyEqual(Out.ContactPoint.Z, 310.0 + double(90.0f / 300.0f) * 300.0, 1e-6));
	TestFalse(TEXT("unsurfaced prop not seen by the probe"), Probe(0).bFound);
	TestFalse(TEXT("modes-3/5 prop absent in race mode"), Probe(2000).bFound);
	TestFalse(TEXT("non-collidable prop ignored"), Probe(4000).bFound);
	W.bAlternateSetMode = true;
	Out = Probe(2000);
	TestTrue(TEXT("modes-3/5 prop present in ShowOff"), Out.bFound && Out.Hit.InstanceIndex == 12);
	FRiderWorldHit Hit;
	TestTrue(TEXT("ShowOff patch collides in modes 3/5"), W.LineQuery(FVector(4500, 2000, 100), FVector(4500, 2000, -100), ERiderLineQuery::Fine, 0.5, Hit) && Hit.PatchIndex == 0);
	W.bAlternateSetMode = false;
	TestFalse(TEXT("ShowOff patch absent otherwise"), W.LineQuery(FVector(4500, 2000, 100), FVector(4500, 2000, -100), ERiderLineQuery::Fine, 0.5, Hit));

	FRiderSphereSet S;
	S.BoundCentre = FVector(0, -150, 300);   // 50 cm into the -y face of prop 1
	S.BoundRadius = 100.0;
	FRiderPropInfo Info;
	TestTrue(TEXT("unsurfaced prop: QueryProps contact"), W.QueryProps(S, Hit, Info) && Hit.Kind == 1 && Hit.Surface == -1
		&& Hit.InstanceIndex == 11 && Info.Instance == 11 && FMath::IsNearlyEqual(Hit.T, 50.0, 1e-6) && Hit.Normal.Equals(FVector(0, -1, 0), 1e-9));
	TestFalse(TEXT("unsurfaced props are not walls"), W.QueryWalls(S, true, nullptr, Hit));
	S.BoundCentre = FVector(-2000, -150, 300);
	TestFalse(TEXT("surfaced prop not in the prop query"), W.QueryProps(S, Hit, Info));
	TestTrue(TEXT("surfaced prop is a wall (main query, kind 0, its surface)"), W.QueryWalls(S, false, nullptr, Hit) && Hit.Kind == 0
		&& Hit.Surface == 13 && Hit.InstanceIndex == 10);
	const FVector Filter(0, -1, 0);
	TestFalse(TEXT("main query filter n.d > 0.8 rejects"), W.QueryWalls(S, false, &Filter, Hit));
	S.BoundCentre = FVector(0, -60, 300);
	S.BoundRadius = 50.0;
	TestFalse(TEXT("one-sided triangles: centre behind the faces, no prop contact"), W.QueryProps(S, Hit, Info));
	S.BoundCentre = FVector(6000, -80, 300);
	S.BoundRadius = 30.0;
	TestTrue(TEXT("two-sided node: normal flipped to the sphere side"), W.QueryProps(S, Hit, Info) && Hit.InstanceIndex == 14
		&& Hit.Normal.Equals(FVector(0, 1, 0), 1e-9) && FMath::IsNearlyEqual(Hit.T, 10.0, 1e-9));

	{
		TArray<FSimInstance> BI;
		TArray<FSimInstanceNode> BN;
		AddInst(BI, BN, 20, 0xA1u, -1, 2, FBox(FVector(-100, -50, 0), FVector(100, 50, 200)));
		BN[0].NodeToGame = FTranslationMatrix(FVector(0, 0, 100));
		BN[0].GameToNode = FTranslationMatrix(FVector(0, 0, -100));
		BN[0].BoxMin = FVector(-100, -50, -100);
		BN[0].BoxMax = FVector(100, 50, 100);
		FPatchWorld BW;
		BW.SetInstances(MoveTemp(BI), MoveTemp(BN), TArray<FSimInstanceTri>());
		FRiderSphereSet BS;
		BS.BoundCentre = FVector(0, 60, 100);    // 10 cm past the +y face, radius 30 -> depth 20 along +y
		BS.BoundRadius = 30.0;
		BS.Count = 1;
		BS.Centres[0] = FVector(1000, 1000, 1000);
		BS.Radii[0] = 1.0;
		TestTrue(TEXT("box: depth to the nearest face"), BW.QueryProps(BS, Hit, Info) && FMath::IsNearlyEqual(Hit.T, 20.0, 1e-9)
			&& Hit.Normal.Equals(FVector(0, 1, 0), 1e-9) && Hit.InstanceIndex == 20);
		BS.Mask = 0x2;
		TestFalse(TEXT("box: no enabled sub-sphere bit below the count -> no hit"), BW.QueryProps(BS, Hit, Info));
		BS.Mask = -1;
		BS.BoundCentre = FVector(110, 60, 100);   // past an edge diagonally: never hits
		TestFalse(TEXT("box: centre outside two slabs never hits"), BW.QueryProps(BS, Hit, Info));
	}
	{
		FSimSphereTree Tree;
		Tree.Depth = 1;
		Tree.Levels = { { 100.0, 0.0, 1 }, { 40.0, 30.0, 8 } };
		Tree.Occupancy.Init(0, 9);
		Tree.Occupancy[0] = 0x80;
		TestEqual(TEXT("tree leaves"), Tree.CountLeaves(), 1);
		TArray<FSimInstance> TI;
		TArray<FSimInstanceNode> TN;
		AddInst(TI, TN, 30, 0xA1u, -1, 3, FBox(FVector(-100), FVector(100)));
		TN[0].Tree = 0;
		TArray<FSimSphereTree> Trees = { Tree };
		FPatchWorld TW;
		TW.SetInstances(MoveTemp(TI), MoveTemp(TN), TArray<FSimInstanceTri>(), MoveTemp(Trees));
		FRiderSphereSet TS;
		TS.BoundCentre = FVector(30, 30, 100);
		TS.BoundRadius = 50.0;
		TestTrue(TEXT("tree: leaf push along unit(q - C)"), TW.QueryProps(TS, Hit, Info) && FMath::IsNearlyEqual(Hit.T, 20.0, 1e-9)
			&& Hit.Normal.Equals(FVector(0, 0, 1), 1e-9));
		TS.BoundCentre = FVector(-60, -60, -60);
		TestFalse(TEXT("tree: interior node without a hit child -> no hit"), TW.QueryProps(TS, Hit, Info));
		TS.BoundCentre = FVector(30, 30, 100);
		TS.Count = 2;
		TS.Mask = 0x1;
		TS.Centres[0] = FVector(-500, 0, 0);
		TS.Radii[0] = 10.0;
		TS.Centres[1] = FVector(30, 30, 85);
		TS.Radii[1] = 20.0;
		TestTrue(TEXT("tree: sub-spheres ignore the mask, first hit"), TW.QueryProps(TS, Hit, Info) && FMath::IsNearlyEqual(Hit.T, 5.0, 1e-9));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPropResponseModelTest, "SSX.Collision.PropResponse.Model", InstTestFlags)
bool FSSXPropResponseModelTest::RunTest(const FString& Parameters)
{
	FInstCsv Csv;
	if (!Csv.Load(SimData(TEXT("instcol_response.csv"))))
	{
		SSXTest::Skip(*this, TEXT("SimData/instcol_response.csv missing (tools/make_instance_collision_reference.py); skipped"));
		return true;
	}
	const TMap<FString, SSXGround::EPropReaction> Names = { { TEXT("none"), SSXGround::EPropReaction::None },
		{ TEXT("skipped"), SSXGround::EPropReaction::Skipped }, { TEXT("bump"), SSXGround::EPropReaction::Bump },
		{ TEXT("bounce"), SSXGround::EPropReaction::Bounce }, { TEXT("movable"), SSXGround::EPropReaction::Movable } };
	TMap<FString, int32> PerClass;
	int32 Bad = 0;
	double MaxDv = 0.0;
	for (const TArray<FString>& R : Csv.Rows)
	{
		FRiderPropInfo P;
		P.Mass = Csv.D(R, TEXT("mass"));
		P.Restitution = Csv.D(R, TEXT("e"));
		P.Flags = uint32(FCString::Strtoui64(*Csv.S(R, TEXT("flags")), nullptr, 10));
		P.bMovable = Csv.I(R, TEXT("movable")) != 0;
		const SSXGround::FPropResponse Out = SSXGround::ComputePropResponse(P, Csv.D(R, TEXT("depth")), Csv.V(R, TEXT("nx"), TEXT("ny"), TEXT("nz")),
			Csv.V(R, TEXT("vx"), TEXT("vy"), TEXT("vz")), ERiderMotion(Csv.I(R, TEXT("motion"))), ERiderControl(Csv.I(R, TEXT("control"))),
			Csv.V(R, TEXT("gx"), TEXT("gy"), TEXT("gz")), Csv.D(R, TEXT("rider_mass")));
		const FString Cls = Csv.S(R, TEXT("reaction"));
		PerClass.FindOrAdd(Cls)++;
		const double Dv = (Out.Velocity - Csv.V(R, TEXT("ovx"), TEXT("ovy"), TEXT("ovz"))).Size();
		MaxDv = FMath::Max(MaxDv, Dv);
		const bool bOk = Names.Contains(Cls) && Out.Reaction == Names[Cls] && Dv < 1e-6
			&& (Out.Push - Csv.V(R, TEXT("dpx"), TEXT("dpy"), TEXT("dpz"))).Size() < 1e-9
			&& (Out.Normal - Csv.V(R, TEXT("onx"), TEXT("ony"), TEXT("onz"))).Size() < 1e-9
			&& FMath::Abs(Out.Approach - Csv.D(R, TEXT("approach"))) < 1e-6 && FMath::Abs(Out.Dv - Csv.D(R, TEXT("impulse"))) < 1e-6
			&& FMath::Abs(Out.Rumble - Csv.D(R, TEXT("rumble"))) < 1e-9;
		Bad += !bOk;
	}
	FString Classes;
	for (const TPair<FString, int32>& Pc : PerClass) { Classes += FString::Printf(TEXT(" %s %d"), *Pc.Key, Pc.Value); }
	InstReport(this, FString::Printf(TEXT("prop_response: %d cases (%s), %d mismatches, max |dv| %.2e cm/s"), Csv.Rows.Num(), *Classes, Bad, MaxDv));
	TestEqual(TEXT("response mismatches"), Bad, 0);
	TestTrue(TEXT("all reaction classes covered"), PerClass.Num() == 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPropResponseLiveTest, "SSX.Collision.PropResponse.Live", InstTestFlags)
bool FSSXPropResponseLiveTest::RunTest(const FString& Parameters)
{
	FInstCsv Csv;
	if (!Csv.Load(SimData(TEXT("instcol_live.csv"))))
	{
		SSXTest::Skip(*this, TEXT("SimData/instcol_live.csv missing (tools/make_instance_collision_reference.py, needs the live trace); skipped"));
		return true;
	}
	TMap<FString, TUniquePtr<FPatchWorld>> Worlds;
	FPatchWorld* W = WorldFor(Worlds, TEXT("gari"), this);
	if (!W)
	{
		return false;
	}
	W->bAlternateSetMode = false;
	TMap<FString, int32> Frames, Pushed;
	int32 Bad = 0;
	TArray<double> SeenPush;
	for (const TArray<FString>& R : Csv.Rows)
	{
		FRiderSphereSet Shape;
		Shape.BoundCentre = Csv.V(R, TEXT("x0"), TEXT("y0"), TEXT("z0"));
		Shape.BoundRadius = Csv.D(R, TEXT("r"));
		Shape.Mask = Csv.I(R, TEXT("mask"));
		ParseSubs(Csv.S(R, TEXT("subs")), Shape);
		FRiderWorldHit Hit;
		FRiderPropInfo Info;
		const bool bHit = W->QueryProps(Shape, Hit, Info);
		const FString Cls = Csv.S(R, TEXT("class"));
		Frames.FindOrAdd(Cls)++;
		const FVector V0 = Csv.V(R, TEXT("vx"), TEXT("vy"), TEXT("vz"));
		bool bOk = bHit && Hit.InstanceIndex == Csv.I(R, TEXT("instance")) && FMath::Abs(Hit.T - Csv.D(R, TEXT("t"))) < 1e-3
			&& (Hit.Normal - Csv.V(R, TEXT("nx"), TEXT("ny"), TEXT("nz"))).Size() < 1e-4;
		if (bOk)
		{
			const SSXGround::FPropResponse Out = SSXGround::ComputePropResponse(Info, Hit.T, Hit.Normal, V0, ERiderMotion(Csv.I(R, TEXT("motion"))),
				ERiderControl(Csv.I(R, TEXT("control"))), Csv.V(R, TEXT("gx"), TEXT("gy"), TEXT("gz")));
			bOk = (Out.Push - Csv.V(R, TEXT("dpx"), TEXT("dpy"), TEXT("dpz"))).Size() < 1e-3 && (Out.Velocity - Csv.V(R, TEXT("ovx"), TEXT("ovy"), TEXT("ovz"))).Size() < 1e-3;
			// trigger boxes (no 0x80) and mass-0 props never push or change v (the recorded next frame agrees)
			if (Cls != TEXT("bounce"))
			{
				const bool bNoPush = Out.Push.IsZero() && Out.Velocity.Equals(V0, 0.0);
				bOk &= bNoPush;
				Pushed.FindOrAdd(Cls) += !bNoPush;
				SeenPush.Add(Csv.D(R, TEXT("push_seen")) / FMath::Max(Hit.T, 1e-6));
			}
		}
		Bad += !bOk;
	}
	SeenPush.Sort();
	const double MedianPush = SeenPush.Num() ? SeenPush[SeenPush.Num() / 2] : 0.0;
	InstReport(this, FString::Printf(TEXT("gari live trace: %d rider-frames (none %d, bump %d, bounce %d), %d mismatches vs the model; C++ pushes: none %d, bump %d; recorded next-frame push / depth median %.3f"),
		Csv.Rows.Num(), Frames.FindRef(TEXT("none")), Frames.FindRef(TEXT("bump")), Frames.FindRef(TEXT("bounce")), Bad,
		Pushed.FindRef(TEXT("none")), Pushed.FindRef(TEXT("bump")), MedianPush));
	TestTrue(TEXT("recorded riders were not pushed (median push / depth ~ 0)"), FMath::Abs(MedianPush) < 0.05);
	TestEqual(TEXT("live-trace mismatches"), Bad, 0);
	TestTrue(TEXT("trigger and bump frames present"), Frames.FindRef(TEXT("none")) > 100 && Frames.FindRef(TEXT("bump")) > 0);
	return true;
}

namespace
{
	class FScriptedPropWorld : public IRiderWorld
	{
	public:
		FRiderWorldHit PropHit;
		FRiderPropInfo Prop;
		FSurfaceTable Surfaces;
		virtual bool ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const override
		{
			Out = FGroundProbeResult();
			Out.bFound = true;
			Out.Surface = 1;
			Out.GroundNormal = FVector::UpVector;
			Out.ContactPoint = FVector(In.Position.X, In.Position.Y, 0.0);
			Out.Height = In.Position.Z;
			Out.ContactKind = 0;
			return true;
		}
		virtual bool LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef, FRiderWorldHit& Out) const override { return false; }
		virtual bool QueryProps(const FRiderSphereSet& Shape, FRiderWorldHit& Out, FRiderPropInfo& OutInfo) const override
		{
			Out = PropHit;
			OutInfo = Prop;
			return true;
		}
		mutable uint32 LiveFlags = 0;
		bool bLiveFlags = false;
		virtual bool GetPropFlags(int32 Instance, uint32& OutFlags) const override
		{
			OutFlags = LiveFlags;
			return bLiveFlags;
		}
		virtual const FSurfaceParams& SurfaceParams(int32 SurfaceId) const override { return Surfaces.Get(SurfaceId); }
		virtual double FloorZ() const override { return -1e6; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPropResponsePassTest, "SSX.Collision.PropResponse.Pass", InstTestFlags)
bool FSSXPropResponsePassTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("mask Ground"), SSXGround::PropMaskForMotion(ERiderMotion::Ground), -1);
	TestEqual(TEXT("mask Air"), SSXGround::PropMaskForMotion(ERiderMotion::Air), -1);
	TestEqual(TEXT("mask RailSlide"), SSXGround::PropMaskForMotion(ERiderMotion::RailSlide), 0x003);
	TestEqual(TEXT("mask WipeOut"), SSXGround::PropMaskForMotion(ERiderMotion::WipeOut), 0xFC3);
	TestEqual(TEXT("mask WipeOutRecover"), SSXGround::PropMaskForMotion(ERiderMotion::WipeOutRecover), 0xFC3);
	TestEqual(TEXT("no pass when Static"), SSXGround::PropMaskForMotion(ERiderMotion::Static), 0);

	FScriptedPropWorld World;
	World.PropHit.Kind = 1;
	World.PropHit.Surface = -1;
	World.PropHit.Normal = FVector(0, -1, 0);
	World.PropHit.T = 10.0;
	World.PropHit.Point = FVector(0, 30, 100);
	World.PropHit.InstanceIndex = 7;
	World.Prop.Instance = 7;
	World.Prop.TouchEffect = 42;
	auto Run = [&](uint32 HighFlags, double Mass, double Speed, ERiderControl Control, SSXGround::FPropContactEvent& Ev, FRiderSim& Sim) -> bool
	{
		SSXRider::FPlayerOptions O;
		SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(SSXRider::FRiderTable::Shared(), 10, O), Sim.Profile);
		Sim.SetWorld(&World);
		Sim.Reset();
		Sim.SetOrientationFromAxes(FVector(0, 1, 0), FVector::UpVector);
		Sim.State.Motion = ERiderMotion::Ground;
		Sim.State.Control = Control;
		Sim.State.Velocity = FVector(0, Speed, 0);
		World.Prop.Flags = HighFlags << 16;
		World.Prop.Mass = Mass;
		World.Prop.Restitution = 0.5;
		int32 Hooked = 0;
		Sim.OnPropContact = [&Hooked](FRiderSim&, const SSXGround::FPropContactEvent& E) { Hooked += E.TouchEffect == 42; };
		SSXAnim::FRiderAnimChannel Ch;
		const bool bHit = SSXGround::PropCollision(Sim, ERiderMotion::Ground, &Ch, &Ev);
		return bHit && Hooked == 1;
	};
	{
		FRiderSim Probe;
		SSXRider::FPlayerOptions O;
		SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(SSXRider::FRiderTable::Shared(), 10, O), Probe.Profile);
		FRiderSphereSet Shape;
		if (!SSXRider::BuildSphereSet(Probe.Profile, false, FVector::ZeroVector, FQuat::Identity, -1, Shape))
		{
			SSXTest::Skip(*this, TEXT("no rider bones (SimData riders): the PropCollision pass needs the sphere set; pass part skipped"));
			return true;
		}
	}
	{
		FRiderSim Sim;
		SSXGround::FPropContactEvent Ev;
		TestTrue(TEXT("trigger (no 0x80): contact, touch effect hook"), Run(0x21, 1e30, 1000.0, ERiderControl::Cruise, Ev, Sim));
		TestTrue(TEXT("trigger: no push, v unchanged"), Sim.State.Position.IsZero() && Sim.State.Velocity.Equals(FVector(0, 1000, 0), 0.0)
			&& Ev.Response.Reaction == SSXGround::EPropReaction::None && Ev.bHitSound);
	}
	{
		FRiderSim Sim;
		SSXGround::FPropContactEvent Ev;
		TestTrue(TEXT("mass 0: contact"), Run(0xA1, 0.0, 1000.0, ERiderControl::Cruise, Ev, Sim));
		TestTrue(TEXT("mass 0: bump animation only, no push"), Sim.State.Position.IsZero()
			&& Sim.State.Velocity.Equals(FVector(0, 1000, 0), 0.0) && Sim.State.Control == ERiderControl::Bump);
	}
	{
		FRiderSim Sim;
		SSXGround::FPropContactEvent Ev;
		TestTrue(TEXT("push and bounce: contact"), Run(0xA1, 1e30, 1000.0, ERiderControl::Cruise, Ev, Sim));
		TestTrue(TEXT("push 1.1 t n"), Sim.State.Position.Equals(FVector(0, -11, 0), 1e-9));
		TestTrue(TEXT("dv = max(a (1 + e), a + 55.56): v.n = 500"), Sim.State.Velocity.Equals(FVector(0, -500, 0), 1e-9));
		TestTrue(TEXT("rumble request (x = 1.2 > 0.5, 0.1 s)"), Ev.bRumble && FMath::IsNearlyEqual(Ev.RumbleSeconds, 0.1));
		TestTrue(TEXT("feedback virtual (slot 13) recorded: 0.0012 a = 1.2"), Sim.State.PadFeedback.Num() == 1
			&& !Sim.State.PadFeedback[0].bImpact && FMath::IsNearlyEqual(Sim.State.PadFeedback[0].Amount, 1.2, 1e-9));
		TestTrue(TEXT("stuck_a fed"), FMath::IsNearlyEqual(Sim.State.StuckAccumA, 1.0));
	}
	{
		FRiderSim Sim;
		SSXGround::FPropContactEvent Ev;
		Run(0xA1, 1e30, -300.0, ERiderControl::Cruise, Ev, Sim);
		TestTrue(TEXT("separating rider: outgoing normal speed set to 55.56"), FMath::IsNearlyEqual(-Sim.State.Velocity.Y, SSXGround::PropBounceMin, 1e-9));
	}
	{
		FRiderSim Sim;
		SSXGround::FPropContactEvent Ev;
		Run(0xA1, 1e30, 1000.0, ERiderControl::ResetWait, Ev, Sim);
		TestTrue(TEXT("ResetWait: no reaction"), Ev.Response.Reaction == SSXGround::EPropReaction::Skipped && Sim.State.Position.IsZero());
	}
	return true;
}

// 0x125088, 0x13BD40
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPropTouchKillsTest, "SSX.Collision.PropResponse.TouchKills", InstTestFlags)
bool FSSXPropTouchKillsTest::RunTest(const FString& Parameters)
{
	using namespace SSXEffects;
	{
		FRiderSim Probe;
		SSXRider::FPlayerOptions O;
		SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(SSXRider::FRiderTable::Shared(), 10, O), Probe.Profile);
		FRiderSphereSet Shape;
		if (!SSXRider::BuildSphereSet(Probe.Profile, false, FVector::ZeroVector, FQuat::Identity, -1, Shape))
		{
			SSXTest::Skip(*this, TEXT("no rider bones (SimData riders): the PropCollision pass needs the sphere set; skipped"));
			return true;
		}
	}
	FScriptedPropWorld World;
	World.PropHit.Kind = 1;
	World.PropHit.Surface = -1;
	World.PropHit.Normal = FVector(0, -1, 0);
	World.PropHit.T = 10.0;
	World.PropHit.Point = FVector(0, 30, 100);
	World.PropHit.InstanceIndex = 589;
	World.Prop.Instance = 589;
	World.Prop.Flags = 0x10A1u << 16;
	World.Prop.Mass = 1e30;
	World.Prop.Restitution = 0.5;
	World.Prop.TouchEffect = 81;
	auto Run = [&](bool bTouchPops, FRiderSim& Sim, SSXGround::FPropContactEvent& Ev)
	{
		SSXRider::FPlayerOptions O;
		SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(SSXRider::FRiderTable::Shared(), 10, O), Sim.Profile);
		Sim.SetWorld(&World);
		Sim.Reset();
		Sim.SetOrientationFromAxes(FVector(0, 1, 0), FVector::UpVector);
		Sim.State.Motion = ERiderMotion::Ground;
		Sim.State.Control = ERiderControl::Cruise;
		Sim.State.Velocity = FVector(0, 2600, 0);
		World.bLiveFlags = true;
		World.LiveFlags = World.Prop.Flags;
		Sim.OnPropContact = [&World, bTouchPops](FRiderSim&, const SSXGround::FPropContactEvent&)
		{
			if (bTouchPops) { World.LiveFlags = 0x1005u << 16; }
		};
		SSXAnim::FRiderAnimChannel Ch;
		return SSXGround::PropCollision(Sim, ERiderMotion::Ground, &Ch, &Ev);
	};
	{
		FRiderSim Sim;
		SSXGround::FPropContactEvent Ev;
		const bool bHit = Run(false, Sim, Ev);
		TestTrue(TEXT("prop kept alive by its touch: bounce, dv 3900 > 1944.44 -> knocked down"), bHit
			&& Ev.Response.Reaction == SSXGround::EPropReaction::Bounce && Sim.State.Motion == ERiderMotion::WipeOut);
	}
	{
		FRiderSim Sim;
		SSXGround::FPropContactEvent Ev;
		const bool bHit = Run(true, Sim, Ev);
		TestTrue(TEXT("touch pops the prop: no reaction, no push, v unchanged, still riding"), bHit
			&& Ev.Response.Reaction == SSXGround::EPropReaction::None && Sim.State.Position.IsZero()
			&& Sim.State.Velocity.Equals(FVector(0, 2600, 0), 0.0) && Sim.State.Motion == ERiderMotion::Ground
			&& Sim.State.Control == ERiderControl::Cruise && Ev.bHitSound);
	}

	const int32 Balloons[] = { 589, 590, 728, 764, 1002, 1551, 2517, 2518, 2519, 2652, 2807, 2815, 3057, 3071 };
	FEffectData Data;
	FString Err;
	TSharedRef<FPatchWorld> W = MakeShared<FPatchWorld>();
	if (!Data.Load(FEffectData::DefaultPath(TEXT("snow")), &Err) || !FPaths::FileExists(FPatchWorld::DefaultInstancePath(TEXT("snow")))
		|| !W->LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("SimData snow effects / patches / instcol missing (%s); track part skipped"), *Err));
		return true;
	}
	FEffectWorld Fx;
	Fx.Init(&Data, 0, 1);
	Fx.OnFlagsChanged = [W](int32 Inst, uint16, uint16 New) { W->SetInstanceRunFlags(Inst, New); };
	int32 Ok = 0, Popped = 0, Hits = 0, KnockedDown = 0, Reacted = 0;
	for (const int32 Inst : Balloons)
	{
		const int32 K = W->FindInstance(Inst);
		if (K == INDEX_NONE)
		{
			continue;
		}
		const FSimInstance& I = W->GetInstance(K);
		const bool bRecord = Fx.GetFlags(Inst) == 0x10A1 && I.Mass > 1e29 && FMath::IsNearlyEqual(I.Restitution, 0.5) && I.ModelType == 2
			&& Data.EffectFor(Inst, 1) != INDEX_NONE;
		Ok += bRecord;
		Fx.RunEffect(Data.EffectFor(Inst, 0), Inst, nullptr);
		Fx.Frame();
		// a rider riding into the balloon's box at 2600 cm/s from each side until one contact
		FRiderSim Sim;
		SSXRider::FPlayerOptions O;
		SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(SSXRider::FRiderTable::Shared(), 10, O), Sim.Profile);
		Sim.SetWorld(&W.Get());
		FEffectRider Rider;
		Sim.OnPropContact = [&Fx, &Rider](FRiderSim&, const SSXGround::FPropContactEvent& E)
		{
			Fx.Touch(E.Hit.InstanceIndex, Rider, true, FVector::DotProduct(E.Contact.Direction, E.Contact.Normal), E.Contact.Speed);
		};
		const FVector C = I.Bounds.GetCenter(), Ext = I.Bounds.GetExtent();
		const FVector Dirs[4] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0) };
		for (const FVector& D : Dirs)
		{
			Sim.Reset();
			Sim.SetOrientationFromAxes(D, FVector::UpVector);
			FRiderSphereSet Shape;
			if (!SSXRider::BuildSphereSet(Sim.Profile, false, FVector::ZeroVector, Sim.State.Orientation, -1, Shape))
			{
				break;
			}
			// bounding sphere 20 cm inside the face the rider rides into
			const FVector Target = C - D * (FMath::Abs(FVector::DotProduct(Ext, D)) + Shape.BoundRadius - 20.0);
			Sim.State.Position = Target - Shape.BoundCentre;
			Sim.State.Motion = ERiderMotion::Ground;
			Sim.State.Control = ERiderControl::Cruise;
			Sim.State.GroundNormal = FVector::UpVector;
			Sim.State.Velocity = D * 2600.0;
			SSXAnim::FRiderAnimChannel Ch;
			SSXGround::FPropContactEvent Ev;
			if (SSXGround::PropCollision(Sim, ERiderMotion::Ground, &Ch, &Ev) && Ev.Hit.InstanceIndex == Inst)
			{
				++Hits;
				KnockedDown += Sim.State.Motion == ERiderMotion::WipeOut;
				Reacted += Ev.Response.Reaction != SSXGround::EPropReaction::None;
				break;
			}
		}
		uint32 After = 0;
		Popped += W->GetPropFlags(Inst, After) && ((After >> 16) & 0xA0u) == 0 && (Fx.GetFlags(Inst) & 0xA0) == 0;
	}
	InstReport(this, FString::Printf(TEXT("snow balloons: %d/%d records (flags 0x10A1, mass 1e30, e 0.5, box, slot-1 effect), %d rider contacts, %d popped (0x20 / 0x80 cleared), %d reactions, %d knocked down"),
		Ok, int32(UE_ARRAY_COUNT(Balloons)), Hits, Popped, Reacted, KnockedDown));
	TestEqual(TEXT("balloon records"), Ok, int32(UE_ARRAY_COUNT(Balloons)));
	TestTrue(TEXT("riders reach the balloons"), Hits >= 10);
	TestEqual(TEXT("touched balloons pop"), Popped, Hits);
	TestEqual(TEXT("no reaction after the pop"), Reacted, 0);
	TestEqual(TEXT("balloons never knock the rider down"), KnockedDown, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTreeQueryTest, "SSX.Collision.TreeQuery", InstTestFlags)
bool FSSXTreeQueryTest::RunTest(const FString& Parameters)
{
	FSimSphereTree Tree;
	Tree.Depth = 1;
	Tree.Levels = { { 100.0, 0.0, 1 }, { 40.0, 30.0, 8 } };
	Tree.Occupancy.Init(0, 9);
	Tree.Occupancy[0] = 0x80;
	const FVector Tri[3] = { FVector(-1000, -1000, 0), FVector(3000, -1000, 0), FVector(-1000, 3000, 0) };
	FVector P;
	double D = FPatchWorld::TreeTriangle(Tree, FMatrix::Identity, Tri, FVector(0, 0, 1), P);
	TestTrue(TEXT("leaf 30 cm above the face, radius 40: depth 10 at the foot"), FMath::IsNearlyEqual(D, 10.0, 1e-9) && P.Equals(FVector(30, 30, 0), 1e-9));
	const FVector Tri2[3] = { FVector(-1000, -1000, 50), FVector(3000, -1000, 50), FVector(-1000, 3000, 50) };
	TestTrue(TEXT("one-sided: tree centre behind the face -> no hit"), FPatchWorld::TreeTriangle(Tree, FMatrix::Identity, Tri2, FVector(0, 0, 1), P) < 0.0);
	TestTrue(TEXT("leaf 50 cm above the face: no hit"), FPatchWorld::TreeTriangle(Tree, FTranslationMatrix(FVector(0, 0, 20)), Tri, FVector(0, 0, 1), P) < 0.0);
	const FVector Far[3] = { FVector(500, 500, 0), FVector(900, 500, 0), FVector(500, 900, 0) };
	TestTrue(TEXT("foot outside a small triangle: no hit"), FPatchWorld::TreeTriangle(Tree, FMatrix::Identity, Far, FVector(0, 0, 1), P) < 0.0);

	TArray<FSimInstance> Insts;
	TArray<FSimInstanceNode> Nodes;
	TArray<FSimInstanceTri> Tris;
	{
		const FVector C(0, 0, 200);
		AddInst(Insts, Nodes, 40, 0xA1u, 13, 1, FBox(C - FVector(100), C + FVector(100)));
		FSimInstanceNode& N = Nodes.Last();
		N.NodeToGame = FTranslationMatrix(C);
		N.GameToNode = FTranslationMatrix(-C);
		N.FirstTri = Tris.Num();
		AddCube(Tris, FVector::ZeroVector, 100.0);
		N.NumTris = Tris.Num() - N.FirstTri;
	}
	{
		const FVector C(2000, 0, 0);
		AddInst(Insts, Nodes, 41, 0xA1u, -1, 3, FBox(C - FVector(100), C + FVector(100)));
		FSimInstanceNode& N = Nodes.Last();
		N.NodeToGame = FTranslationMatrix(C);
		N.GameToNode = FTranslationMatrix(-C);
		N.Tree = 0;
	}
	TArray<FSimSphereTree> Trees = { Tree };
	FPatchWorld W;
	W.SetInstances(MoveTemp(Insts), MoveTemp(Nodes), MoveTemp(Tris), MoveTemp(Trees));
	FRiderWorldHit Hit;
	TestTrue(TEXT("body on the cube top: surfaced hit, depth 10, up normal"),
		W.QueryTree(Tree, FTranslationMatrix(FVector(0, 0, 300)), false, Hit) && FMath::IsNearlyEqual(Hit.T, 10.0, 1e-6)
		&& Hit.Normal.Equals(FVector(0, 0, 1), 1e-6) && Hit.InstanceIndex == 40);
	TestFalse(TEXT("the prop query does not see the surfaced cube"), W.QueryTree(Tree, FTranslationMatrix(FVector(0, 0, 300)), true, Hit));
	TestTrue(TEXT("tree vs tree: first chain to a leaf, push 40 up"),
		W.QueryTree(Tree, FTranslationMatrix(FVector(2000, 0, 100)), true, Hit) && FMath::IsNearlyEqual(Hit.T, 40.0, 1e-6)
		&& Hit.Normal.Equals(FVector(0, 0, 1), 1e-6) && Hit.InstanceIndex == 41);
	TestFalse(TEXT("tree vs tree: body child misses -> no hit"), W.QueryTree(Tree, FTranslationMatrix(FVector(2000, 0, 150)), true, Hit));
	TestFalse(TEXT("the main query does not see the unsurfaced tree prop"), W.QueryTree(Tree, FTranslationMatrix(FVector(2000, 0, 100)), false, Hit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInstanceAnimBroadPhaseTest, "SSX.Collision.Instances.AnimBroadPhase", InstTestFlags)
bool FSSXInstanceAnimBroadPhaseTest::RunTest(const FString& Parameters)
{
	// 0x25B878
	const FVector P(1000, 2000, 300);
	const FVector Lo(-79.9, -609.7, -544.8), Hi(78.9, 609.7, 544.6);
	auto MakeWorld = [&](FPatchWorld& W)
	{
		TArray<FSimInstance> Insts;
		TArray<FSimInstanceNode> Nodes;
		FSimInstance& I = AddInst(Insts, Nodes, 5, 0x21u, -1, 2, FBox(P + Lo, P + Hi));
		I.Origin = P;
		FSimInstanceNode& N = Nodes.Last();
		N.NodeToGame = FTranslationMatrix(P);
		N.GameToNode = FTranslationMatrix(-P);
		N.BoxMin = Lo;
		N.BoxMax = Hi;
		W.SetInstances(MoveTemp(Insts), MoveTemp(Nodes), TArray<FSimInstanceTri>());
	};
	auto Hits = [](const FPatchWorld& W, const FVector& C)
	{
		FRiderSphereSet S;
		S.BoundCentre = C;
		S.BoundRadius = 87.0;
		S.Mask = 0;
		FRiderWorldHit Hit;
		FRiderPropInfo Info;
		return W.QueryProps(S, Hit, Info) && Hit.InstanceIndex == 5;
	};
	const FMatrix Turned = FRotationMatrix(FRotator(0.0, 90.0, 0.0)) * FTranslationMatrix(P);

	FPatchWorld W;
	MakeWorld(W);
	const FBox Cube = FPatchWorld::PropBroadPhaseBox(W.GetInstance(W.FindInstance(5)));
	TestTrue(TEXT("static instance: the broad phase is the instance AABB"), Cube.Min.Equals(P + Lo, 1e-3) && Cube.Max.Equals(P + Hi, 1e-3));
	TestTrue(TEXT("static: a rider 4 m beside the gem in its plane (y) hits"), Hits(W, P + FVector(0, 400, 0)));
	TestFalse(TEXT("static: a rider 4 m beside the gem across its plane (x) misses"), Hits(W, P + FVector(400, 0, 0)));
	W.SetInstanceNodeMatrix(5, 0, Turned);
	TestFalse(TEXT("turned node without a controller flag: the AABB broad phase rejects the 4 m side rider"), Hits(W, P + FVector(400, 0, 0)));

	W.SetInstanceAnimController(5, true);
	const FBox Live = FPatchWorld::PropBroadPhaseBox(W.GetInstance(W.FindInstance(5)));
	TestTrue(TEXT("live controller: the cube P +- 609.7 on every axis"),
		Live.Min.Equals(P - FVector(609.7), 1e-2) && Live.Max.Equals(P + FVector(609.7), 1e-2));
	TestTrue(TEXT("live controller, node turned 90 deg: the rider 4 m to the side (x) hits"), Hits(W, P + FVector(400, 0, 0)));
	TestTrue(TEXT("live controller, node turned 90 deg: 6 m to the side still inside the slab"), Hits(W, P + FVector(600, 0, 0)));
	TestFalse(TEXT("live controller: beyond the original's reach (7 m) misses"), Hits(W, P + FVector(700, 0, 0)));
	TestFalse(TEXT("live controller, node turned: across the turned slab (y 4 m) misses"), Hits(W, P + FVector(0, 400, 0)));

	W.SetInstanceAnimController(5, false);
	TestFalse(TEXT("controller gone: the AABB broad phase again"), Hits(W, P + FVector(400, 0, 0)));
	W.SetInstanceNodeMatrix(5, 0, FTranslationMatrix(P));
	TestTrue(TEXT("controller gone, static node: the in-plane 4 m rider hits as before"), Hits(W, P + FVector(0, 400, 0)));
	return true;
}

#endif
