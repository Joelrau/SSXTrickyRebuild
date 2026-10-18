// 0x25B878, 0x125088, 0x1999E8, 0x198B48, 0x1998F8
#include "Sim/PatchWorld.h"
#include "Sim/EEFloat.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 InstMagic = 'S' | ('S' << 8) | ('X' << 16) | ('C' << 24);
	constexpr uint32 InstVersion = 2;
	constexpr int32 InstHeaderSize = 64;
	constexpr int32 InstEntrySize = 120;
	constexpr int32 InstNodeSize = 136;
	constexpr int32 InstMeshSize = 8;
	constexpr int32 InstTriSize = 48;
	constexpr int32 InstTreeSize = 8;
	constexpr int32 LtgSubSize = 44;
	constexpr int32 LtgCellSize = 24 + 16 * LtgSubSize;
	constexpr uint32 InstFlagGameSpace = 1;
	constexpr double InstFilterDot = 0.8;

	// 0x3439D0
	const FVector Octants[8] = {
		FVector(-1, -1, -1), FVector(-1, -1, 1), FVector(-1, 1, -1), FVector(-1, 1, 1),
		FVector(1, -1, -1), FVector(1, -1, 1), FVector(1, 1, -1), FVector(1, 1, 1) };

	FORCEINLINE FVector XformPos(const FMatrix& M, const FVector& P)
	{
		return FVector(P.X * M.M[0][0] + P.Y * M.M[1][0] + P.Z * M.M[2][0] + M.M[3][0],
		               P.X * M.M[0][1] + P.Y * M.M[1][1] + P.Z * M.M[2][1] + M.M[3][1],
		               P.X * M.M[0][2] + P.Y * M.M[1][2] + P.Z * M.M[2][2] + M.M[3][2]);
	}

	FORCEINLINE FVector XformDir(const FMatrix& M, const FVector& P)
	{
		return FVector(P.X * M.M[0][0] + P.Y * M.M[1][0] + P.Z * M.M[2][0],
		               P.X * M.M[0][1] + P.Y * M.M[1][1] + P.Z * M.M[2][1],
		               P.X * M.M[0][2] + P.Y * M.M[1][2] + P.Z * M.M[2][2]);
	}

	FMatrix Mul(const FMatrix& A, const FMatrix& B)
	{
		FMatrix R = FMatrix::Identity;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				R.M[Row][Col] = A.M[Row][0] * B.M[0][Col] + A.M[Row][1] * B.M[1][Col] + A.M[Row][2] * B.M[2][Col]
					+ (Row == 3 ? B.M[3][Col] : 0.0);
			}
		}
		return R;
	}

	FMatrix AffineInverse(const FMatrix& M)
	{
		const double A = M.M[0][0], B = M.M[0][1], C = M.M[0][2];
		const double D = M.M[1][0], E = M.M[1][1], F = M.M[1][2];
		const double G = M.M[2][0], H = M.M[2][1], K = M.M[2][2];
		const double Det = A * (E * K - F * H) - B * (D * K - F * G) + C * (D * H - E * G);
		FMatrix R = FMatrix::Identity;
		if (Det == 0.0)
		{
			return R;
		}
		const double Inv = 1.0 / Det;
		R.M[0][0] = (E * K - F * H) * Inv; R.M[0][1] = (C * H - B * K) * Inv; R.M[0][2] = (B * F - C * E) * Inv;
		R.M[1][0] = (F * G - D * K) * Inv; R.M[1][1] = (A * K - C * G) * Inv; R.M[1][2] = (C * D - A * F) * Inv;
		R.M[2][0] = (D * H - E * G) * Inv; R.M[2][1] = (B * G - A * H) * Inv; R.M[2][2] = (A * E - B * D) * Inv;
		for (int32 Col = 0; Col < 3; ++Col)
		{
			R.M[3][Col] = -(M.M[3][0] * R.M[0][Col] + M.M[3][1] * R.M[1][Col] + M.M[3][2] * R.M[2][Col]);
		}
		return R;
	}

	FORCEINLINE bool StrictOverlap(const FVector& AMin, const FVector& AMax, const FBox& Q)
	{
		return AMin.X < Q.Max.X && Q.Min.X < AMax.X && AMin.Y < Q.Max.Y && Q.Min.Y < AMax.Y && AMin.Z < Q.Max.Z && Q.Min.Z < AMax.Z;
	}

	bool InsideTri(const FVector& P, const FVector Tri[3], const FVector& N)
	{
		return SSXEE::MicroInside(SSXEE::F(Tri[0]), SSXEE::F(Tri[1]), SSXEE::F(Tri[2]), SSXEE::F(P));   // 0x23B6B0
	}
	bool EdgeRaySphere(const FVector& C, double R, const FVector& A, const FVector& B)
	{
		const FVector E = B - A;
		const double Len = E.Size();
		const FVector Rel = C - A;
		const double Proj = FVector::DotProduct(Rel, E) / Len;
		const double Perp2 = FVector::DotProduct(Rel, Rel) - Proj * Proj;
		if (Proj < 0.0 || Perp2 > R * R)
		{
			return false;
		}
		return !(Len < Proj - FMath::Sqrt(R * R - Perp2));
	}

	bool Touches(const FVector& C, double R, const FVector Tri[3], const FVector& N, double D)
	{
		return InsideTri(C - D * N, Tri, N) || EdgeRaySphere(C, R, Tri[0], Tri[1]) || EdgeRaySphere(C, R, Tri[0], Tri[2])
			|| EdgeRaySphere(C, R, Tri[1], Tri[2]);
	}

	double ShapeTriVariant(const FRiderSphereSet& S, const FVector Tri[3], FVector N, ESimTriVariant Variant, FVector& OutN, FVector& OutPoint)
	{
		double D = FVector::DotProduct(S.BoundCentre - Tri[0], N);
		if (Variant == ESimTriVariant::OneSided)
		{
			if (D < 0.0 || D > S.BoundRadius)
			{
				return -1.0;
			}
		}
		else if (FMath::Abs(D) > S.BoundRadius)
		{
			return -1.0;
		}
		if (Variant == ESimTriVariant::TwoSided && D < 0.0)
		{
			N = -N;
			D = -D;
		}
		if (!Touches(S.BoundCentre, S.BoundRadius, Tri, N, D))
		{
			return -1.0;
		}
		OutN = N;
		if (S.Count <= 0)
		{
			OutPoint = S.BoundCentre - D * N;
			return S.BoundRadius - D;
		}
		double BestD = 0.0;
		int32 BestI = INDEX_NONE;
		for (int32 I = 0; I < FMath::Min(S.Count, FRiderSphereSet::MaxSpheres); ++I)
		{
			if (((S.Mask >> I) & 1) == 0)
			{
				continue;
			}
			const double Di = FVector::DotProduct(S.Centres[I] - Tri[0], N);
			if (Di < S.Radii[I] && Touches(S.Centres[I], S.Radii[I], Tri, N, Di) && (BestI == INDEX_NONE || Di < BestD))
			{
				BestD = Di;
				BestI = I;
			}
		}
		if (BestI == INDEX_NONE)
		{
			return -1.0;
		}
		OutPoint = S.Centres[BestI] - BestD * N;
		return S.Radii[BestI] - BestD;
	}

	// 0x23B6E8
	double SphereBox(const FVector& C, double R, const FVector& Lo, const FVector& Hi, FVector& OutN, FVector& OutPoint)
	{
		double Best = -1.0;
		bool bHave = false;
		for (int32 A = 0; A < 3; ++A)
		{
			const int32 O1 = (A + 1) % 3, O2 = (A + 2) % 3;
			if (!(Lo[O1] <= C[O1] && C[O1] <= Hi[O1] && Lo[O2] <= C[O2] && C[O2] <= Hi[O2]))
			{
				continue;
			}
			const double DLo = C[A] - Lo[A] + R;
			const double DHi = Hi[A] - C[A] + R;
			if (DLo < 0.0 || DHi < 0.0)
			{
				continue;
			}
			const double Depths[2] = { DLo, DHi };
			for (int32 Side = 0; Side < 2; ++Side)
			{
				if (!bHave || Depths[Side] < Best)
				{
					bHave = true;
					Best = Depths[Side];
					OutN = FVector::ZeroVector;
					OutN[A] = Side == 0 ? -1.0 : 1.0;
					OutPoint = C;
					OutPoint[A] = Side == 0 ? Lo[A] : Hi[A];
				}
			}
		}
		return bHave ? Best : -1.0;
	}

	FRiderSphereSet ToNode(const FRiderSphereSet& S, const FMatrix& GameToNode)
	{
		FRiderSphereSet L = S;
		L.BoundCentre = XformPos(GameToNode, S.BoundCentre);
		for (int32 I = 0; I < FMath::Min(S.Count, FRiderSphereSet::MaxSpheres); ++I)
		{
			L.Centres[I] = XformPos(GameToNode, S.Centres[I]);
		}
		return L;
	}
}

bool FSimSphereTree::Parse(const uint8* Data, int32 Size, FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Msg) { if (OutError) { *OutError = Msg; } return false; };
	auto I32 = [Data](int32 Off) { int32 X; FMemory::Memcpy(&X, Data + Off, 4); return X; };
	auto F32 = [Data](int32 Off) { float X; FMemory::Memcpy(&X, Data + Off, 4); return double(X); };
	if (Size < 0x70)
	{
		return Fail(TEXT("sphere tree: block too short"));
	}
	const int32 Extra = I32(0), Packed = I32(4), bCompressed = I32(8);
	Depth = I32(12);
	if (Depth < 0 || Depth > 7 || 0x70 + 12 * (Depth + 1) + Packed + Extra != Size)
	{
		return Fail(TEXT("sphere tree: bad header"));
	}
	Centre = FVector(F32(0x10), F32(0x14), F32(0x18));
	Com = FVector(F32(0x1C), F32(0x20), F32(0x24));
	for (int32 K = 0; K < 9; ++K)
	{
		Inertia[K] = F32(0x28 + 4 * K);
		InvInertia[K] = F32(0x4C + 4 * K);
	}
	Levels.SetNum(Depth + 1);
	for (int32 L = 0; L <= Depth; ++L)
	{
		Levels[L].Radius = F32(0x70 + 12 * L);
		Levels[L].ChildOffset = F32(0x74 + 12 * L);
		Levels[L].Stride = I32(0x78 + 12 * L);
	}
	int64 Total = 0;
	for (int32 L = 0, P = 1; L <= Depth; ++L, P *= 8)
	{
		Total += P;
	}
	const int32 Start = 0x70 + 12 * (Depth + 1);
	Occupancy.Reset(int32(Total));
	if (!bCompressed)
	{
		// TODO: no uncompressed tree in the retail data (the model reads the bytes as they are)
		if (Packed < Total)
		{
			return Fail(TEXT("sphere tree: short raw occupancy"));
		}
		Occupancy.Append(Data + Start, int32(Total));
		return true;
	}
	// 0x239B20
	int32 Q = Start;
	const int32 End = Start + Packed;
	for (;;)
	{
		if (Q >= End)
		{
			return Fail(TEXT("sphere tree: RLE stream overruns"));
		}
		const int32 C = int8(Data[Q++]);
		if (C == 0)
		{
			break;
		}
		if (C < 0)
		{
			if (Q - C > End) { return Fail(TEXT("sphere tree: RLE literal overruns")); }
			Occupancy.Append(Data + Q, -C);
			Q -= C;
		}
		else
		{
			if (Q >= End) { return Fail(TEXT("sphere tree: RLE run overruns")); }
			for (int32 K = 0; K <= C; ++K) { Occupancy.Add(Data[Q]); }
			++Q;
		}
	}
	if (Q != End || Occupancy.Num() != Total)
	{
		return Fail(TEXT("sphere tree: unpacked size mismatch"));
	}
	return true;
}

namespace
{
	bool TreeVisit(const FSimSphereTree& T, const FVector& Q, double R, const FVector& C, int32 Level, int32 Idx, FVector& OutPush, FVector& OutContact)
	{
		const FVector D = Q - C;
		const double F = R + T.Levels[Level].Radius;
		const double DD = D.SizeSquared();
		if (!(DD < F * F))
		{
			return false;
		}
		const uint8 Bits = T.Occupancy[Idx];
		if (Level < T.Depth && Bits)
		{
			const double Off = T.Levels[Level + 1].ChildOffset;
			const int32 Stride = T.Levels[Level].Stride;
			for (int32 K = 0; K < 8; ++K)
			{
				if ((Bits >> K) & 1)
				{
					const int32 Child = (K + 1) * Stride + Idx;
					if (T.Occupancy.IsValidIndex(Child) && TreeVisit(T, Q, R, C + Octants[K] * Off, Level + 1, Child, OutPush, OutContact))
					{
						return true;
					}
				}
			}
			return false;
		}
		const double Dist = FMath::Sqrt(DD);
		const FVector N = Dist > 0.0 ? D / Dist : FVector(0, 0, 1);
		const double Depth = F - Dist;
		OutPush = N * Depth;
		OutContact = Q - N * (R - Depth);
		return true;
	}

	int32 TreeLeaves(const FSimSphereTree& T, int32 Level, int32 Idx)
	{
		const uint8 Bits = T.Occupancy[Idx];
		if (!(Level < T.Depth && Bits))
		{
			return 1;
		}
		int32 N = 0;
		for (int32 K = 0; K < 8; ++K)
		{
			if ((Bits >> K) & 1)
			{
				N += TreeLeaves(T, Level + 1, (K + 1) * T.Levels[Level].Stride + Idx);
			}
		}
		return N;
	}
}

bool FSimSphereTree::QuerySphere(const FVector& Q, double R, FVector& OutPush, FVector& OutContact) const
{
	return Occupancy.Num() > 0 && TreeVisit(*this, Q, R, Centre, 0, 0, OutPush, OutContact);
}

int32 FSimSphereTree::CountLeaves() const
{
	return Occupancy.Num() > 0 ? TreeLeaves(*this, 0, 0) : 0;
}

FString FPatchWorld::DefaultInstancePath(const FString& Track)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Track.ToLower() + TEXT(".instcol.bin"));
}

bool FPatchWorld::LoadInstances(const FString& InstanceFile, FString* OutError)
{
	auto Fail = [OutError](const FString& Msg) { if (OutError) { *OutError = Msg; } return false; };
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *InstanceFile))
	{
		return Fail(FString::Printf(TEXT("cannot read %s (run tools/export_instance_collision.py)"), *InstanceFile));
	}
	if (Bytes.Num() < InstHeaderSize)
	{
		return Fail(TEXT("instance file too short"));
	}
	const uint8* Data = Bytes.GetData();
	const int64 Size = Bytes.Num();
	auto U32 = [Data](int64 Off) { uint32 X; FMemory::Memcpy(&X, Data + Off, 4); return X; };
	auto I32 = [Data](int64 Off) { int32 X; FMemory::Memcpy(&X, Data + Off, 4); return X; };
	auto U16 = [Data](int64 Off) { uint16 X; FMemory::Memcpy(&X, Data + Off, 2); return X; };
	auto F32 = [Data](int64 Off) { float X; FMemory::Memcpy(&X, Data + Off, 4); return double(X); };
	auto F64 = [Data](int64 Off) { double X; FMemory::Memcpy(&X, Data + Off, 8); return X; };
	auto Vec = [&F32](int64 Off) { return FVector(F32(Off), F32(Off + 4), F32(Off + 8)); };
	if (U32(0) != InstMagic || U32(4) != InstVersion)
	{
		return Fail(TEXT("instance file: bad magic or version (re-run tools/export_instance_collision.py)"));
	}
	if ((U32(36) & InstFlagGameSpace) == 0)
	{
		return Fail(TEXT("instance file is not in game space"));
	}
	const int32 NPbd = int32(U32(8)), NEntry = int32(U32(12)), NNode = int32(U32(16)), NMesh = int32(U32(20)), NTri = int32(U32(24));
	const int32 NTree = int32(U32(28)), NBlob = int32(U32(32)), NCell = int32(U32(40)), NPatchRef = int32(U32(44)), NInstRef = int32(U32(48));
	const int64 OEntry = InstHeaderSize;
	const int64 ONode = OEntry + int64(NEntry) * InstEntrySize;
	const int64 OMesh = ONode + int64(NNode) * InstNodeSize;
	const int64 OTri = OMesh + int64(NMesh) * InstMeshSize;
	const int64 OTree = OTri + int64(NTri) * InstTriSize;
	const int64 OBlob = OTree + int64(NTree) * InstTreeSize;
	const int64 ODraw = OBlob + NBlob;
	const int64 OLtg = ODraw + int64(NPbd) * 8;
	if (Size < OLtg + 32)
	{
		return Fail(TEXT("instance file: truncated"));
	}
	FSimLtg L;
	L.Min = Vec(OLtg);
	L.CellSize = F32(OLtg + 12);
	L.Nx = I32(OLtg + 16);
	L.Ny = I32(OLtg + 20);
	const int64 OCellIdx = OLtg + 32;
	const int64 OCells = OCellIdx + int64(L.Nx) * L.Ny * 4;
	const int64 OPRef = OCells + int64(NCell) * LtgCellSize;
	const int64 OIRef = OPRef + int64(NPatchRef) * 4;
	if (Size != OIRef + int64(NInstRef) * 4 || I32(OLtg + 24) != 16)
	{
		return Fail(TEXT("instance file: size does not match its counts"));
	}

	TArray<FSimSphereTree> LTrees;
	LTrees.SetNum(NTree);
	for (int32 K = 0; K < NTree; ++K)
	{
		const int32 Off = I32(OTree + 8 * K), Len = I32(OTree + 8 * K + 4);
		FString Err;
		if (Off < 0 || Len < 0 || int64(Off) + Len > NBlob || !LTrees[K].Parse(Data + OBlob + Off, Len, &Err))
		{
			return Fail(FString::Printf(TEXT("instance file: tree %d: %s"), K, *Err));
		}
	}
	// node-space triangles (zero stored normals never collide: dropped)
	TArray<FSimInstanceTri> Tris;
	TArray<int32> MeshFirst, MeshCount;
	for (int32 M = 0; M < NMesh; ++M)
	{
		const int32 First = I32(OMesh + 8 * M), Count = I32(OMesh + 8 * M + 4);
		if (First < 0 || Count < 0 || First + Count > NTri)
		{
			return Fail(TEXT("instance file: mesh out of range"));
		}
		MeshFirst.Add(Tris.Num());
		for (int32 T = First; T < First + Count; ++T)
		{
			const int64 O = OTri + int64(T) * InstTriSize;
			FSimInstanceTri Tri;
			Tri.V[0] = Vec(O);
			Tri.V[1] = Vec(O + 12);
			Tri.V[2] = Vec(O + 24);
			const FVector N = Vec(O + 36);
			const double Len = N.Size();
			if (Len > 0.0)
			{
				Tri.N = N / Len;
				Tris.Add(Tri);
			}
		}
		MeshCount.Add(Tris.Num() - MeshFirst.Last());
	}
	struct FRawNode { int32 Kind; uint32 Bits; FMatrix Chain; int32 Mesh, Tree; FVector Lo, Hi; };
	TArray<FRawNode> Raw;
	Raw.SetNum(NNode);
	for (int32 K = 0; K < NNode; ++K)
	{
		const int64 O = ONode + int64(K) * InstNodeSize;
		FRawNode& R = Raw[K];
		R.Kind = I32(O);
		R.Bits = U32(O + 4);
		R.Chain = FMatrix::Identity;
		for (int32 J = 0; J < 12; ++J)
		{
			R.Chain.M[J / 3][J % 3] = F64(O + 8 + 8 * J);
		}
		R.Mesh = I32(O + 104);
		R.Tree = I32(O + 108);
		R.Lo = Vec(O + 112);
		R.Hi = Vec(O + 124);
		if ((R.Mesh != INDEX_NONE && (R.Mesh < 0 || R.Mesh >= NMesh)) || (R.Tree != INDEX_NONE && (R.Tree < 0 || R.Tree >= NTree)))
		{
			return Fail(TEXT("instance file: node reference out of range"));
		}
	}
	TArray<FSimInstance> Insts;
	TArray<FSimInstanceNode> Nodes;
	Insts.Reserve(NEntry);
	for (int32 E = 0; E < NEntry; ++E)
	{
		const int64 O = OEntry + int64(E) * InstEntrySize;
		FSimInstance& I = Insts.AddDefaulted_GetRef();
		I.SourceIndex = I32(O);
		I.Flags = U32(O + 4);
		I.Surface = I32(O + 8);
		I.ModelType = I32(O + 12);
		I.SetBits = uint8(U32(O + 20));
		I.Bounds = FBox(Vec(O + 24), Vec(O + 36));
		FMatrix M = FMatrix::Identity;
		for (int32 J = 0; J < 12; ++J)
		{
			M.M[J / 3][J % 3] = F32(O + 48 + 4 * J);
		}
		I.Origin = FVector(M.M[3][0], M.M[3][1], M.M[3][2]);
		I.Mass = F32(O + 96);
		I.Restitution = F32(O + 100);
		I.TouchEffect = I32(O + 104);
		const int32 FirstNode = I32(O + 112), NodeCount = I32(O + 116);
		if (FirstNode < 0 || NodeCount < 0 || FirstNode + NodeCount > NNode)
		{
			return Fail(TEXT("instance file: node range out of range"));
		}
		I.FirstNode = Nodes.Num();
		I.NumNodes = NodeCount;
		for (int32 K = FirstNode; K < FirstNode + NodeCount; ++K)
		{
			const FRawNode& R = Raw[K];
			FSimInstanceNode& N = Nodes.AddDefaulted_GetRef();
			N.Kind = R.Kind;
			N.Bits = R.Bits;
			N.NodeToGame = Mul(R.Chain, M);
			N.GameToNode = AffineInverse(N.NodeToGame);
			if (R.Mesh != INDEX_NONE)
			{
				N.FirstTri = MeshFirst[R.Mesh];
				N.NumTris = MeshCount[R.Mesh];
			}
			N.Tree = R.Tree;
			N.BoxMin = R.Lo;
			N.BoxMax = R.Hi;
		}
	}
	DrawFlags.SetNum(NPbd);
	DrawSets.SetNum(NPbd);
	for (int32 K = 0; K < NPbd; ++K)
	{
		DrawFlags[K] = U32(ODraw + 8 * K);
		DrawSets[K] = uint8(U32(ODraw + 8 * K + 4));
	}
	L.CellIndex.SetNum(L.Nx * L.Ny);
	for (int32 K = 0; K < L.CellIndex.Num(); ++K)
	{
		L.CellIndex[K] = I32(OCellIdx + 4 * K);
		if (L.CellIndex[K] >= NCell)
		{
			return Fail(TEXT("instance file: ltg cell index out of range"));
		}
	}
	L.Cells.SetNum(NCell);
	for (int32 C = 0; C < NCell; ++C)
	{
		const int64 O = OCells + int64(C) * LtgCellSize;
		FSimLtg::FCell& Cell = L.Cells[C];
		Cell.Min = Vec(O);
		Cell.Max = Vec(O + 12);
		for (int32 S = 0; S < 16; ++S)
		{
			const int64 SO = O + 24 + int64(S) * LtgSubSize;
			FSimLtg::FSub& Sub = Cell.Subs[S];
			Sub.Min = Vec(SO);
			Sub.Max = Vec(SO + 12);
			for (int32 J = 0; J < 6; ++J)
			{
				Sub.Counts[J] = U16(SO + 24 + 2 * J);
			}
			Sub.FirstPatch = I32(SO + 36);
			Sub.FirstInst = I32(SO + 40);
			if (Sub.FirstPatch < 0 || Sub.FirstPatch + Sub.Counts[0] > NPatchRef || Sub.FirstInst < 0
				|| Sub.FirstInst + FMath::Max(Sub.Counts[1], Sub.Counts[2]) > NInstRef)
			{
				return Fail(TEXT("instance file: ltg list out of range"));
			}
		}
	}
	L.PatchRefs.SetNum(NPatchRef);
	for (int32 K = 0; K < NPatchRef; ++K) { L.PatchRefs[K] = I32(OPRef + 4 * K); }
	L.InstRefs.SetNum(NInstRef);
	for (int32 K = 0; K < NInstRef; ++K) { L.InstRefs[K] = I32(OIRef + 4 * K); }

	TArray<uint32> KeepFlags = MoveTemp(DrawFlags);
	TArray<uint8> KeepSets = MoveTemp(DrawSets);
	SetInstances(MoveTemp(Insts), MoveTemp(Nodes), MoveTemp(Tris), MoveTemp(LTrees), MoveTemp(L));
	DrawFlags = MoveTemp(KeepFlags);
	DrawSets = MoveTemp(KeepSets);
	return true;
}

void FPatchWorld::SetInstances(TArray<FSimInstance>&& InInstances, TArray<FSimInstanceNode>&& InNodes, TArray<FSimInstanceTri>&& InTris,
                               TArray<FSimSphereTree>&& InTrees, FSimLtg&& InLtg)
{
	Instances = MoveTemp(InInstances);
	InstanceNodes = MoveTemp(InNodes);
	InstanceTris = MoveTemp(InTris);
	Trees = MoveTemp(InTrees);
	Ltg = MoveTemp(InLtg);
	DrawFlags.Reset();
	DrawSets.Reset();
	NumLineInstances = 0;
	for (FSimInstance& I : Instances)
	{
		I.RunFlags = uint16(I.Flags >> 16);
		NumLineInstances += I.IsCollidable() && I.Surface != -1;
	}
	RebuildSourceMaps();
}

void FPatchWorld::SetInstanceRunFlags(int32 SourceIndex, uint16 Low)
{
	const int32 K = FindInstance(SourceIndex);
	if (K != INDEX_NONE)
	{
		Instances[K].RunFlags = Low;
	}
}

void FPatchWorld::ResetInstanceRunFlags()
{
	for (FSimInstance& I : Instances)
	{
		I.RunFlags = uint16(I.Flags >> 16);
		I.bMovable = false;
	}
}

void FPatchWorld::SetInstanceMovable(int32 SourceIndex, bool bMovable)
{
	const int32 K = FindInstance(SourceIndex);
	if (K != INDEX_NONE)
	{
		Instances[K].bMovable = bMovable;
	}
}

bool FPatchWorld::IsPropMovable(int32 Instance) const
{
	const int32 K = FindInstance(Instance);
	return K != INDEX_NONE && Instances[K].bMovable;
}

bool FPatchWorld::GetPropFlags(int32 Instance, uint32& OutFlags) const
{
	const int32 K = FindInstance(Instance);
	if (K == INDEX_NONE)
	{
		return false;
	}
	const FSimInstance& I = Instances[K];
	OutFlags = (uint32(I.RunFlags) << 16) | (I.Flags & 0xFFFF);
	return true;
}

void FPatchWorld::SetInstanceNodeMatrix(int32 SourceIndex, int32 NodeK, const FMatrix& NodeToGame)
{
	const int32 K = FindInstance(SourceIndex);
	if (K == INDEX_NONE || NodeK < 0 || NodeK >= Instances[K].NumNodes)
	{
		return;
	}
	FSimInstanceNode& N = InstanceNodes[Instances[K].FirstNode + NodeK];
	N.NodeToGame = NodeToGame;
	N.GameToNode = NodeToGame.Inverse();
}

void FPatchWorld::SetInstanceAnimController(int32 SourceIndex, bool bLive)
{
	const int32 K = FindInstance(SourceIndex);
	if (K != INDEX_NONE)
	{
		Instances[K].bAnimController = bLive;
	}
}

FBox FPatchWorld::PropBroadPhaseBox(const FSimInstance& I)
{
	if (!I.bAnimController)
	{
		return I.Bounds;
	}
	// 0x25B878
	const float Px = float(I.Origin.X), Py = float(I.Origin.Y), Pz = float(I.Origin.Z);
	const float Lo = FMath::Min(FMath::Min(float(I.Bounds.Min.X) - Px, float(I.Bounds.Min.Y) - Py), float(I.Bounds.Min.Z) - Pz);
	const float Hi = FMath::Max(FMath::Max(float(I.Bounds.Max.X) - Px, float(I.Bounds.Max.Y) - Py), float(I.Bounds.Max.Z) - Pz);
	return FBox(FVector(Px + Lo, Py + Lo, Pz + Lo), FVector(Px + Hi, Py + Hi, Pz + Hi));
}

void FPatchWorld::RebuildSourceMaps()
{
	int32 MaxI = -1, MaxP = -1;
	for (const FSimInstance& I : Instances) { MaxI = FMath::Max(MaxI, I.SourceIndex); }
	for (const FSimPatch& P : Patches) { MaxP = FMath::Max(MaxP, P.SourceIndex); }
	InstanceBySource.Init(INDEX_NONE, MaxI + 1);
	PatchBySource.Init(INDEX_NONE, MaxP + 1);
	for (int32 K = 0; K < Instances.Num(); ++K)
	{
		if (Instances[K].SourceIndex >= 0) { InstanceBySource[Instances[K].SourceIndex] = K; }
	}
	for (int32 K = 0; K < Patches.Num(); ++K)
	{
		if (Patches[K].SourceIndex >= 0) { PatchBySource[Patches[K].SourceIndex] = K; }
	}
}

bool FPatchWorld::IsInstanceDrawn(int32 SourceIndex, bool bShowOffMode) const
{
	if (!DrawFlags.IsValidIndex(SourceIndex))
	{
		return true;
	}
	if (((DrawFlags[SourceIndex] >> 16) & FSimInstance::FlagDrawn) == 0)
	{
		return false;
	}
	const uint8 Set = DrawSets[SourceIndex];
	return (Set & FSimInstance::SetNormal) != 0 || (bShowOffMode && (Set & FSimInstance::SetAlternate) != 0);
}

bool FPatchWorld::WalkLtg(const FBox& Q, TFunctionRef<bool(int32)> OnPatch, bool bPatches, TFunctionRef<bool(int32)> OnInstance) const
{
	const FVector C = (Q.Min + Q.Max) * 0.5;
	const int32 Ix = int32((C.X - Ltg.Min.X + Ltg.CellSize) / Ltg.CellSize);
	const int32 Iy = int32((C.Y - Ltg.Min.Y + Ltg.CellSize) / Ltg.CellSize);
	for (int32 I = FMath::Max(Ix - 2, 0); I <= FMath::Min(FMath::Max(Ix, 0), Ltg.Nx - 1); ++I)
	{
		for (int32 J = FMath::Max(Iy - 2, 0); J <= FMath::Min(FMath::Max(Iy, 0), Ltg.Ny - 1); ++J)
		{
			const int32 CI = Ltg.CellIndex[J * Ltg.Nx + I];
			if (CI < 0)
			{
				continue;
			}
			const FSimLtg::FCell& Cell = Ltg.Cells[CI];
			if (!StrictOverlap(Cell.Min, Cell.Max, Q))
			{
				continue;
			}
			for (const FSimLtg::FSub& Sub : Cell.Subs)
			{
				if (Sub.IsEmpty() || !StrictOverlap(Sub.Min, Sub.Max, Q))
				{
					continue;
				}
				if (bPatches)
				{
					for (int32 K = 0; K < Sub.Counts[0]; ++K)
					{
						const int32 Src = Ltg.PatchRefs[Sub.FirstPatch + K];
						const int32 P = PatchBySource.IsValidIndex(Src) ? PatchBySource[Src] : INDEX_NONE;
						if (P != INDEX_NONE && OnPatch(P))
						{
							return true;
						}
					}
				}
				const int32 N = bAlternateSetMode ? Sub.Counts[2] : Sub.Counts[1];
				for (int32 K = 0; K < N; ++K)
				{
					const int32 Inst = FindInstance(Ltg.InstRefs[Sub.FirstInst + K]);
					if (Inst != INDEX_NONE && OnInstance(Inst))
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

void FPatchWorld::CollectLtgOrder(const FBox& Q, TArray<int32>& Out) const
{
	Out.Reset();
	if (Ltg.IsValid())
	{
		WalkLtg(Q, [](int32) { return false; }, false, [&](int32 K) { Out.Add(Instances[K].SourceIndex); return false; });
	}
}

bool FPatchWorld::InstanceNarrowPhase(const FSimInstance& I, const FRiderSphereSet& Shape, ESimTriVariant Variant, double TRef,
                                      FRiderWorldHit& Out) const
{
	bool bHave = false;
	for (int32 K = 0; K < I.NumNodes; ++K)
	{
		const FSimInstanceNode& Node = InstanceNodes[I.FirstNode + K];
		const FRiderSphereSet L = ToNode(Shape, Node.GameToNode);
		double T = -1.0;
		FVector NL, PL;
		int32 Sub = 0;
		if (Node.Kind == 1)
		{
			// 0x25D3D8
			const ESimTriVariant V = (Node.Bits & FSimInstanceNode::BitTwoSided) ? ESimTriVariant::TwoSided : Variant;
			double Last = 1e30;
			for (int32 Ti = 0; Ti < Node.NumTris; ++Ti)
			{
				const FSimInstanceTri& Tri = InstanceTris[Node.FirstTri + Ti];
				FVector N, P;
				const double D = ShapeTriVariant(L, Tri.V, Tri.N, V, N, P);
				if (D < 0.0)
				{
					continue;
				}
				if (FMath::Abs(D - TRef) < FMath::Abs(Last - TRef))
				{
					T = D;
					NL = N;
					PL = P;
					Sub = Ti;
					if (TRef < 0.0)
					{
						break;
					}
				}
				Last = D;
			}
		}
		else if (Node.Kind == 2)
		{
			// 0x238640
			bool bRun = Shape.Mask == 0;
			for (int32 B = 0; !bRun && B < FMath::Min(Shape.Count, FRiderSphereSet::MaxSpheres); ++B)
			{
				bRun = ((Shape.Mask >> B) & 1) != 0;
			}
			if (bRun)
			{
				// 0x23B6E8, 0x25B878
				const double D = SphereBox(L.BoundCentre, L.BoundRadius, Node.BoxMin, Node.BoxMax, NL, PL);
				T = D >= 0.0 ? D : -1.0;
			}
		}
		else if (Node.Kind == 3 && Trees.IsValidIndex(Node.Tree))
		{
			// 0x2394A8
			const FSimSphereTree& Tree = Trees[Node.Tree];
			FVector Push, Contact;
			bool bHit = Tree.QuerySphere(L.BoundCentre, L.BoundRadius, Push, Contact);
			if (bHit && L.Count > 0)
			{
				bHit = false;
				for (int32 S = 0; S < FMath::Min(L.Count, FRiderSphereSet::MaxSpheres) && !bHit; ++S)
				{
					bHit = Tree.QuerySphere(L.Centres[S], L.Radii[S], Push, Contact);
				}
			}
			const double D = bHit ? Push.Size() : 0.0;
			if (D > 0.0)
			{
				T = D;
				NL = Push / D;
				PL = Contact;
			}
		}
		if (T < 0.0)
		{
			continue;
		}
		if (bHave && !(FMath::Abs(T - TRef) < FMath::Abs(Out.T - TRef)))
		{
			continue;
		}
		Out = FRiderWorldHit();
		Out.Kind = I.Surface == -1 ? 1 : ((Node.Bits & FSimInstanceNode::BitAnimated) ? 2 : 0);
		Out.Point = XformPos(Node.NodeToGame, PL);
		Out.Normal = XformDir(Node.NodeToGame, NL);
		Out.Surface = I.Surface;
		Out.T = T;
		Out.InstanceIndex = I.SourceIndex;
		Out.CellRow = K;
		Out.CellTri = Sub;
		bHave = true;
		if (TRef < 0.0)
		{
			return true;
		}
	}
	return bHave;
}

bool FPatchWorld::InstanceSphereQuery(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir, bool bSurfaced,
                                      ESimTriVariant Variant, FRiderWorldHit& Out) const
{
	if (Shape.BoundRadius <= 0.0)
	{
		return false;
	}
	const FBox Query = Shape.bHasQueryBox ? FBox(Shape.QueryBoxMin, Shape.QueryBoxMax)
		: FBox(Shape.BoundCentre - FVector(Shape.BoundRadius), Shape.BoundCentre + FVector(Shape.BoundRadius));
	const double TRef = bFine ? 2.0 * Shape.BoundRadius : -1.0;
	bool bHave = false;
	FRiderWorldHit Best;
	auto Visit = [&](int32 K) -> bool
	{
		const FSimInstance& I = Instances[K];
		if (!I.IsCollidable() || (I.Surface != -1) != bSurfaced)
		{
			return false;
		}
		// 0x25B878, 0x25AFF8
		const FBox Broad = bSurfaced ? I.Bounds : PropBroadPhaseBox(I);
		if (!StrictOverlap(Broad.Min, Broad.Max, Query))
		{
			return false;
		}
		FRiderWorldHit Hit;
		if (!InstanceNarrowPhase(I, Shape, Variant, TRef, Hit))
		{
			return false;
		}
		if (bHave && !(FMath::Abs(Hit.T - TRef) < FMath::Abs(Best.T - TRef)))
		{
			return false;
		}
		if (FilterDir && FVector::DotProduct(Hit.Normal, *FilterDir) > InstFilterDot)
		{
			return false;
		}
		Best = Hit;
		bHave = true;
		return TRef < 0.0;
	};
	if (Ltg.IsValid())
	{
		WalkLtg(Query, [](int32) { return false; }, false, Visit);
	}
	else
	{
		for (int32 K = 0; K < Instances.Num(); ++K)
		{
			if (Instances[K].IsActive(bAlternateSetMode) && Visit(K))
			{
				break;
			}
		}
	}
	if (bHave)
	{
		Out = Best;
	}
	return bHave;
}

bool FPatchWorld::QueryProps(const FRiderSphereSet& Shape, FRiderWorldHit& Out, FRiderPropInfo& OutInfo) const
{
	// 0x25B878, 0x125088, 0x10E190
	if (!InstanceSphereQuery(Shape, Shape.bFineQuery, nullptr, /*bSurfaced=*/false, ESimTriVariant::OneSided, Out))
	{
		return false;
	}
	const int32 K = FindInstance(Out.InstanceIndex);
	if (K != INDEX_NONE)
	{
		const FSimInstance& I = Instances[K];
		OutInfo = FRiderPropInfo();
		OutInfo.Instance = I.SourceIndex;
		OutInfo.Flags = (uint32(I.RunFlags) << 16) | (I.Flags & 0xFFFF);
		OutInfo.Mass = I.Mass;
		OutInfo.Restitution = I.Restitution;
		OutInfo.TouchEffect = I.TouchEffect;
		OutInfo.bMovable = I.bMovable;
	}
	return true;
}

bool FPatchWorld::InstanceLineQuery(const FVector& A, const FVector& B, double TRef, FRiderWorldHit& Out) const
{
	const FVector D = B - A;
	const FBox Query(A.ComponentMin(B), A.ComponentMax(B));
	bool bHave = false;
	FRiderWorldHit Best;
	auto Visit = [&](int32 K) -> bool
	{
		const FSimInstance& I = Instances[K];
		if (!I.IsCollidable() || I.Surface == -1 || !StrictOverlap(I.Bounds.Min, I.Bounds.Max, Query))
		{
			return false;
		}
		bool bInst = false;
		FRiderWorldHit IB;
		for (int32 Ni = 0; Ni < I.NumNodes; ++Ni)
		{
			const FSimInstanceNode& Node = InstanceNodes[I.FirstNode + Ni];
			if (Node.Kind != 1)
			{
				continue;
			}
			const FVector LA = XformPos(Node.GameToNode, A), LB = XformPos(Node.GameToNode, B);
			double Last = 1e30, NT = -1.0;
			int32 NTri = INDEX_NONE;
			for (int32 Ti = 0; Ti < Node.NumTris; ++Ti)
			{
				const FSimInstanceTri& Tri = InstanceTris[Node.FirstTri + Ti];
				const double T = SegmentTriangle(LA, LB - LA, Tri.V, Tri.N);
				if (T < 0.0)
				{
					continue;
				}
				if (FMath::Abs(T - TRef) < FMath::Abs(Last - TRef))
				{
					NT = T;
					NTri = Ti;
					if (TRef < 0.0)
					{
						break;
					}
				}
				Last = T;
			}
			if (NTri == INDEX_NONE || (bInst && !(FMath::Abs(NT - TRef) < FMath::Abs(IB.T - TRef))))
			{
				continue;
			}
			IB = FRiderWorldHit();
			IB.Kind = (Node.Bits & FSimInstanceNode::BitAnimated) ? 2 : 0;
			IB.Point = A + NT * D;
			IB.Normal = XformDir(Node.NodeToGame, InstanceTris[Node.FirstTri + NTri].N);
			IB.Surface = I.Surface;
			IB.T = NT;
			IB.InstanceIndex = I.SourceIndex;
			IB.CellRow = Ni;
			IB.CellTri = NTri;
			bInst = true;
			if (TRef < 0.0)
			{
				break;
			}
		}
		if (!bInst)
		{
			return false;
		}
		if (TRef < 0.0 || !bHave || FMath::Abs(IB.T - TRef) < FMath::Abs(Best.T - TRef))
		{
			Best = IB;
			bHave = true;
		}
		return TRef < 0.0;
	};
	if (Ltg.IsValid())
	{
		WalkLtg(Query, [](int32) { return false; }, false, Visit);
	}
	else
	{
		for (int32 K = 0; K < Instances.Num(); ++K)
		{
			if (Instances[K].IsActive(bAlternateSetMode) && Visit(K))
			{
				break;
			}
		}
	}
	if (bHave)
	{
		Out = Best;
	}
	return bHave;
}
