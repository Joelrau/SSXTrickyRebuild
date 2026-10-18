#include "Sim/PatchWorld.h"

#include "Sim/EEFloat.h"

namespace
{
	const FVector TreeOctants[8] = {
		FVector(-1, -1, -1), FVector(-1, -1, 1), FVector(-1, 1, -1), FVector(-1, 1, 1),
		FVector(1, -1, -1), FVector(1, -1, 1), FVector(1, 1, -1), FVector(1, 1, 1) };

	FORCEINLINE FVector RowPos(const FMatrix& M, const FVector& P)
	{
		return FVector(P.X * M.M[0][0] + P.Y * M.M[1][0] + P.Z * M.M[2][0] + M.M[3][0],
		               P.X * M.M[0][1] + P.Y * M.M[1][1] + P.Z * M.M[2][1] + M.M[3][1],
		               P.X * M.M[0][2] + P.Y * M.M[1][2] + P.Z * M.M[2][2] + M.M[3][2]);
	}
	FORCEINLINE FVector RowDir(const FMatrix& M, const FVector& P)
	{
		return FVector(P.X * M.M[0][0] + P.Y * M.M[1][0] + P.Z * M.M[2][0],
		               P.X * M.M[0][1] + P.Y * M.M[1][1] + P.Z * M.M[2][1],
		               P.X * M.M[0][2] + P.Y * M.M[1][2] + P.Z * M.M[2][2]);
	}

	bool EdgeEnters(const FVector& C, double R, const FVector& A, const FVector& B)
	{
		const FVector E = B - A, Cr = C - A;
		const double L = E.Size();
		if (L <= 0.0)
		{
			return false;
		}
		const double T = FVector::DotProduct(Cr, E) / L;
		if (T < 0.0)
		{
			return false;
		}
		const double M = Cr.SizeSquared() - T * T;
		if (R * R < M)
		{
			return false;
		}
		return !(L < T - FMath::Sqrt(R * R - M));
	}

	bool CrossesEdge(const FVector& C, double R, const FVector Tri[3])
	{
		if ((C - Tri[0]).SizeSquared() <= R * R)
		{
			return true;
		}
		return EdgeEnters(C, R, Tri[0], Tri[1]) || EdgeEnters(C, R, Tri[0], Tri[2]) || EdgeEnters(C, R, Tri[1], Tri[2]);
	}

	struct FTreeTriCtx
	{
		const FSimSphereTree& T;
		FVector Oct[8];
		int32 Order[8];
		const FVector* Tri;
		FVector N;
		double Best = 0.0;
		FVector Point = FVector::ZeroVector;
	};

	void TreeTriVisit(FTreeTriCtx& X, const FVector& C, int32 Level, int32 Idx)
	{
		const FSimSphereTree& T = X.T;
		const double D = FVector::DotProduct(X.N, C - X.Tri[0]);
		const double R = T.Levels[Level].Radius;
		if (Level == 0 && D < 0.0)
		{
			return;   // one-sided: the tree centre must be in front of the face
		}
		if (R + X.Best <= D)
		{
			return;
		}
		const uint8 Bits = T.Occupancy[Idx];
		const bool bLeaf = Level >= T.Depth || Level > 2 || Bits == 0;
		const FVector Foot = C - X.N * D;
		const bool bInside = SSXEE::MicroInside(SSXEE::F(X.Tri[0]), SSXEE::F(X.Tri[1]), SSXEE::F(X.Tri[2]), SSXEE::F(Foot));
		if (!bInside)
		{
			if (bLeaf || R < 20.0 || !CrossesEdge(C, R, X.Tri))
			{
				return;
			}
		}
		if (bLeaf)
		{
			const double Pen = D - R;
			if (Pen < X.Best)
			{
				X.Best = Pen;
				X.Point = Foot;
			}
			return;
		}
		const double Off = T.Levels[Level + 1].ChildOffset;
		const int32 Stride = T.Levels[Level].Stride;
		for (int32 O = 0; O < 8; ++O)
		{
			const int32 K = X.Order[O];
			const int32 Child = (K + 1) * Stride + Idx;
			if (((Bits >> K) & 1) && T.Occupancy.IsValidIndex(Child))
			{
				TreeTriVisit(X, C + X.Oct[K] * Off, Level + 1, Child);
			}
		}
	}

	// 0x23A570, 0x23A9A0
	bool TreeChainVisit(const FSimSphereTree& T, const FVector Oct[8], const FVector& C, int32 Level, int32 Idx,
	                    TFunctionRef<bool(const FVector&, double)> Touch)
	{
		if (!(Level < T.Depth) || Level > 3)
		{
			return true;
		}
		const uint8 Bits = T.Occupancy[Idx];
		if (Bits == 0)
		{
			return true;
		}
		const double Off = T.Levels[Level + 1].ChildOffset;
		const double R = T.Levels[Level].Radius;
		const int32 Stride = T.Levels[Level].Stride;
		for (int32 K = 0; K < 8; ++K)
		{
			const int32 Child = (K + 1) * Stride + Idx;
			if (!((Bits >> K) & 1) || !T.Occupancy.IsValidIndex(Child))
			{
				continue;
			}
			const FVector Cc = C + Oct[K] * Off;
			if (Touch(Cc, R) && TreeChainVisit(T, Oct, Cc, Level + 1, Child, Touch))
			{
				return true;
			}
		}
		return false;
	}

	FORCEINLINE bool Overlaps(const FVector& AMin, const FVector& AMax, const FBox& Q)
	{
		return AMin.X < Q.Max.X && Q.Min.X < AMax.X && AMin.Y < Q.Max.Y && Q.Min.Y < AMax.Y && AMin.Z < Q.Max.Z && Q.Min.Z < AMax.Z;
	}

	// 0x23B6E8
	double TreeSphereBox(const FVector& C, double R, const FVector& Lo, const FVector& Hi, FVector& OutN, FVector& OutPoint)
	{
		double Best = -1.0;
		for (int32 A = 0; A < 3; ++A)
		{
			const int32 O1 = (A + 1) % 3, O2 = (A + 2) % 3;
			if (!(Lo[O1] <= C[O1] && C[O1] <= Hi[O1] && Lo[O2] <= C[O2] && C[O2] <= Hi[O2]))
			{
				continue;
			}
			const double Depths[2] = { C[A] - Lo[A] + R, Hi[A] - C[A] + R };
			if (Depths[0] < 0.0 || Depths[1] < 0.0)
			{
				continue;
			}
			for (int32 Side = 0; Side < 2; ++Side)
			{
				if (Best < 0.0 || Depths[Side] < Best)
				{
					Best = Depths[Side];
					OutN = FVector::ZeroVector;
					OutN[A] = Side == 0 ? -1.0 : 1.0;
					OutPoint = C;
					OutPoint[A] = Side == 0 ? Lo[A] : Hi[A];
				}
			}
		}
		return Best;
	}
}

double FPatchWorld::TreeTriangle(const FSimSphereTree& Tree, const FMatrix& TreeToGame, const FVector Tri[3], const FVector& N,
                                 FVector& OutPoint)
{
	if (Tree.Occupancy.Num() == 0 || Tree.Levels.Num() == 0)
	{
		return -1.0;
	}
	FTreeTriCtx X{ Tree };
	X.Tri = Tri;
	X.N = N;
	int32 Keys[8];
	for (int32 K = 0; K < 8; ++K)
	{
		X.Oct[K] = RowDir(TreeToGame, TreeOctants[K]);
		Keys[K] = int32(FVector::DotProduct(X.Oct[K], N * -1000.0));   // 0x23B398
		X.Order[K] = K;
	}
	for (int32 I = 1; I < 8; ++I)
	{
		for (int32 J = I; J > 0 && Keys[X.Order[J]] > Keys[X.Order[J - 1]]; --J)
		{
			Swap(X.Order[J], X.Order[J - 1]);
		}
	}
	TreeTriVisit(X, RowPos(TreeToGame, Tree.Centre), 0, 0);
	if (!(X.Best < 0.0))
	{
		return -1.0;
	}
	OutPoint = X.Point;
	return -X.Best;
}

bool FPatchWorld::QueryTree(const FSimSphereTree& Tree, const FMatrix& TreeToGame, bool bUnsurfacedProps, FRiderWorldHit& Out) const
{
	if (Tree.Occupancy.Num() == 0 || Tree.Levels.Num() == 0)
	{
		return false;
	}
	const FVector Centre = RowPos(TreeToGame, Tree.Centre);
	const double Radius = Tree.Levels[0].Radius;
	const FBox Query(Centre - FVector(Radius), Centre + FVector(Radius));
	FVector Oct[8];
	for (int32 K = 0; K < 8; ++K) { Oct[K] = RowDir(TreeToGame, TreeOctants[K]); }
	bool bHave = false;
	auto Hit = [&](double T, const FVector& P, const FVector& Nrm, int32 Surface, int32 Kind)
	{
		Out = FRiderWorldHit();
		Out.Kind = Kind;
		Out.Point = P;
		Out.Normal = Nrm;
		Out.Surface = Surface;
		Out.T = T;
		bHave = true;
	};
	auto OnPatch = [&](int32 K) -> bool
	{
		const FSimPatch& Patch = Patches[K];
		if (Patch.Surface == IgnoredSurface || (Patch.Flags < 0 && !bAlternateSetMode) || !Patch.Bounds.Intersect(Query))
		{
			return false;
		}
		constexpr int32 Step = 3;
		constexpr int32 N = FSimPatch::TessN;
		for (int32 R = 0; R < N - 1; R += Step)
		{
			for (int32 Col = 0; Col < N - 1; Col += Step)
			{
				const FVector& V00 = Patch.Grid[R * N + Col];
				const FVector& V01 = Patch.Grid[R * N + Col + Step];
				const FVector& V10 = Patch.Grid[(R + Step) * N + Col];
				const FVector& V11 = Patch.Grid[(R + Step) * N + Col + Step];
				if (!CellOverlaps(V00, V01, V10, V11, Query))
				{
					continue;
				}
				for (int32 Which = 0; Which < 2; ++Which)
				{
					const FVector Tri[3] = { Which == 0 ? V11 : V01, V10, Which == 0 ? V01 : V00 };
					FVector Nrm, P;
					if (!CellNormal(Tri, Nrm))
					{
						continue;
					}
					const double T = TreeTriangle(Tree, TreeToGame, Tri, Nrm, P);
					if (T > 0.0)
					{
						Hit(T, P, Nrm, Patch.Surface, 0);
						Out.PatchIndex = K;
						Out.CellRow = R;
						Out.CellCol = Col;
						Out.CellTri = Which;
						return true;
					}
				}
			}
		}
		return false;
	};
	auto OnInstance = [&](int32 K) -> bool
	{
		const FSimInstance& I = Instances[K];
		if (!I.IsCollidable() || (I.Surface == -1) != bUnsurfacedProps || !Overlaps(I.Bounds.Min, I.Bounds.Max, Query))
		{
			return false;
		}
		const int32 Kind = I.Surface == -1 ? 1 : 0;
		for (int32 Nk = 0; Nk < I.NumNodes; ++Nk)
		{
			const FSimInstanceNode& Node = InstanceNodes[I.FirstNode + Nk];
			if (Node.Kind == 1)
			{
				const bool bTwo = (Node.Bits & FSimInstanceNode::BitTwoSided) != 0;
				for (int32 Ti = 0; Ti < Node.NumTris; ++Ti)
				{
					const FSimInstanceTri& St = InstanceTris[Node.FirstTri + Ti];
					const FVector Tri[3] = { RowPos(Node.NodeToGame, St.V[0]), RowPos(Node.NodeToGame, St.V[1]), RowPos(Node.NodeToGame, St.V[2]) };
					FVector Nrm = RowDir(Node.NodeToGame, St.N).GetSafeNormal();
					if (bTwo)
					{
						// 0x2395C0
						const double D = FVector::DotProduct(Nrm, Centre - Tri[0]);
						if (FMath::Abs(D) > Radius)
						{
							continue;
						}
						if (D < 0.0) { Nrm = -Nrm; }
					}
					FVector P;
					const double T = TreeTriangle(Tree, TreeToGame, Tri, Nrm, P);
					if (T > 0.0)
					{
						Hit(T, P, Nrm, I.Surface, Kind);
						Out.InstanceIndex = I.SourceIndex;
						return true;
					}
				}
			}
			else if (Node.Kind == 2 || (Node.Kind == 3 && Trees.IsValidIndex(Node.Tree)))
			{
				FVector Push = FVector::ZeroVector, Contact = FVector::ZeroVector;
				auto Touch = [&](const FVector& Cw, double R) -> bool
				{
					const FVector Cl = RowPos(Node.GameToNode, Cw);
					FVector Pl, Ql;
					if (Node.Kind == 3)
					{
						if (!Trees[Node.Tree].QuerySphere(Cl, R, Pl, Ql))
						{
							return false;
						}
					}
					else
					{
						FVector Nl;
						const double D = TreeSphereBox(Cl, R, Node.BoxMin, Node.BoxMax, Nl, Ql);
						if (!(D > 0.0))
						{
							return false;
						}
						Pl = Nl * D;
					}
					Push = RowDir(Node.NodeToGame, Pl);
					Contact = RowPos(Node.NodeToGame, Ql);
					return true;
				};
				// the root must touch first (guard: a depth-0 tree would otherwise report a hit with no contact)
				if (Touch(Centre, Radius) && TreeChainVisit(Tree, Oct, Centre, 0, 0, Touch) && !Push.IsNearlyZero())
				{
					const double T = Push.Size();
					Hit(T, Contact, Push / T, I.Surface, Kind);
					Out.InstanceIndex = I.SourceIndex;
					return true;
				}
			}
		}
		return false;
	};
	if (Ltg.IsValid())
	{
		WalkLtg(Query, OnPatch, !bUnsurfacedProps, OnInstance);
	}
	else
	{
		bool bDone = false;
		if (!bUnsurfacedProps)
		{
			GatherCandidates(Query);
			const TArray<int32> Candidates = Scratch;
			for (const int32 K : Candidates)
			{
				if (OnPatch(K)) { bDone = true; break; }
			}
		}
		for (int32 K = 0; !bDone && K < Instances.Num(); ++K)
		{
			bDone = Instances[K].IsActive(bAlternateSetMode) && OnInstance(K);
		}
	}
	return bHave;
}
