#include "Sim/PatchWorld.h"
#include "Sim/EEFloat.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 PatchMagic = 'S' | ('S' << 8) | ('X' << 16) | ('P' << 24);
	constexpr int32 PatchHeaderSize = 96;
	constexpr int32 PatchRecordSize = 232;
	constexpr uint32 PatchFlagGameSpace = 1;

	FORCEINLINE FVector SafeUnit(const FVector& V)
	{
		const double L = V.Size();
		return L > 0.0 ? V / L : V;
	}

	bool InsideTriangle(const FVector& P, const FVector Tri[3], const FVector& N)
	{
		// 0x23B6B0
		return SSXEE::MicroInside(SSXEE::F(Tri[0]), SSXEE::F(Tri[1]), SSXEE::F(Tri[2]), SSXEE::F(P));
	}

	double SegmentTriangle(const FVector& A, const FVector& D, const FVector Tri[3], const FVector& N)
	{
		// 0x25D9E0
		const FVector3f Af = SSXEE::F(A), Df = SSXEE::F(D), Nf = SSXEE::F(N);
		const float Denom = SSXEE::Dot(Df, Nf);
		if (FMath::Abs(Denom) < 1.00000001e-10f)
		{
			return -1.0;
		}
		const float T = SSXEE::Dot(SSXEE::Sub(SSXEE::F(Tri[0]), Af), Nf) / Denom;
		if (T < 0.0f || 1.0f < T)
		{
			return -1.0;
		}
		return InsideTriangle(SSXEE::D(SSXEE::Add(Af, SSXEE::Scale(T, Df))), Tri, N) ? T : -1.0;
	}

}

double FPatchWorld::SegmentTriangle(const FVector& A, const FVector& D, const FVector Tri[3], const FVector& N)
{
	return ::SegmentTriangle(A, D, Tri, N);
}

void FSimPatch::Evaluate(double U, double V, FVector& OutP, FVector& OutDu, FVector& OutDv) const
{
	const double Pu[4] = { 1.0, U, U * U, U * U * U };
	const double Dpu[4] = { 0.0, 1.0, 2.0 * U, 3.0 * U * U };
	const double Pv[4] = { 1.0, V, V * V, V * V * V };
	const double Dpv[4] = { 0.0, 1.0, 2.0 * V, 3.0 * V * V };
	OutP = OutDu = OutDv = FVector::ZeroVector;
	for (int32 I = 0; I < 4; ++I)
	{
		for (int32 J = 0; J < 4; ++J)
		{
			const FVector& Cij = C[4 * I + J];
			OutP += (Pu[I] * Pv[J]) * Cij;
			OutDu += (Dpu[I] * Pv[J]) * Cij;
			OutDv += (Pu[I] * Dpv[J]) * Cij;
		}
	}
}

FVector FSimPatch::Evaluate(double U, double V) const
{
	FVector P, Du, Dv;
	Evaluate(U, V, P, Du, Dv);
	return P;
}

FString FPatchWorld::DefaultTrackPath(const FString& Track)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Track.ToLower() + TEXT(".patches.bin"));
}

bool FPatchWorld::LoadTrack(const FString& PatchFile, FString* OutError)
{
	auto Fail = [OutError](const FString& Msg) { if (OutError) { *OutError = Msg; } return false; };
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *PatchFile))
	{
		return Fail(FString::Printf(TEXT("cannot read %s (run tools/export_sim_data.py)"), *PatchFile));
	}
	if (Bytes.Num() < PatchHeaderSize)
	{
		return Fail(TEXT("patch file too short"));
	}
	const uint8* Data = Bytes.GetData();
	auto U32 = [Data](int32 Off) { uint32 X; FMemory::Memcpy(&X, Data + Off, 4); return X; };
	auto I32 = [Data](int32 Off) { int32 X; FMemory::Memcpy(&X, Data + Off, 4); return X; };
	auto F32 = [Data](int32 Off) { float X; FMemory::Memcpy(&X, Data + Off, 4); return double(X); };
	auto Vec = [&F32](int32 Off) { return FVector(F32(Off), F32(Off + 4), F32(Off + 8)); };

	if (U32(0) != PatchMagic || U32(4) != 1)
	{
		return Fail(TEXT("patch file: bad magic or version"));
	}
	const int32 Count = int32(U32(8));
	if ((U32(12) & PatchFlagGameSpace) == 0)
	{
		return Fail(TEXT("patch file is not in game space (expected flag bit 0)"));
	}
	if (Bytes.Num() < PatchHeaderSize + int64(Count) * PatchRecordSize)
	{
		return Fail(TEXT("patch file truncated"));
	}
	StartPos = Vec(40);
	StartForward = Vec(52);
	bHasStart = U32(64) != 0;

	TArray<FSimPatch> Loaded;
	Loaded.SetNum(Count);
	for (int32 K = 0; K < Count; ++K)
	{
		const int32 O = PatchHeaderSize + K * PatchRecordSize;
		FSimPatch& P = Loaded[K];
		for (int32 N = 0; N < 16; ++N)
		{
			P.C[N] = Vec(O + N * 12);
		}
		P.Bounds = FBox(Vec(O + 192), Vec(O + 204));
		P.Surface = I32(O + 216);
		P.Flags = I32(O + 220);
		P.SourceIndex = I32(O + 224);
	}
	SetPatches(MoveTemp(Loaded));

	{
		const FString RailFile = FPaths::Combine(FPaths::GetPath(PatchFile),
			FPaths::GetBaseFilename(PatchFile).Replace(TEXT(".patches"), TEXT("")) + TEXT(".rails.bin"));
		if (FPaths::FileExists(RailFile))
		{
			LoadRails(RailFile);
		}
	}

	{
		const FString InstFile = FPaths::Combine(FPaths::GetPath(PatchFile),
			FPaths::GetBaseFilename(PatchFile).Replace(TEXT(".patches"), TEXT("")) + TEXT(".instcol.bin"));
		if (FPaths::FileExists(InstFile))
		{
			LoadInstances(InstFile);
		}
	}

	if (!Surfaces.IsLoaded())
	{
		Surfaces.LoadFromFile(FPaths::Combine(FPaths::GetPath(PatchFile), TEXT("surfaces.bin")));
	}
	return true;
}

void FPatchWorld::SetPatches(TArray<FSimPatch>&& InPatches)
{
	Patches = MoveTemp(InPatches);
	WorldBounds = FBox(ForceInit);
	// 0x251F50, 0x2576A8, 0x257708, 0x31D1E8, 0x25C7C8
	static const uint32 PowBits[10][3] = {
		{ 0x00000000u, 0x00000000u, 0x00000000u }, { 0x3AB3CC07u, 0x3C4A4588u, 0x3DE38E39u }, { 0x3C33CC07u, 0x3D4A4588u, 0x3E638E39u },
		{ 0x3D17B426u, 0x3DE38E39u, 0x3EAAAAAAu }, { 0x3DB3CC07u, 0x3E4A4588u, 0x3EE38E39u }, { 0x3E2F953Fu, 0x3E9E0652u, 0x3F0E38E3u },
		{ 0x3E97B426u, 0x3EE38E39u, 0x3F2AAAAAu }, { 0x3EF0E65Du, 0x3F1ADD3Cu, 0x3F471C71u }, { 0x3F33CC07u, 0x3F4A4588u, 0x3F638E39u },
		{ 0x3F800000u, 0x3F800000u, 0x3F800000u } };
	float Pow[10][4];
	for (int32 K = 0; K < 10; ++K)
	{
		for (int32 J = 0; J < 3; ++J) { FMemory::Memcpy(&Pow[K][J], &PowBits[K][J], 4); }
		Pow[K][3] = 1.0f;
	}
	for (FSimPatch& P : Patches)
	{
		FVector3f Sl[16];
		for (int32 N = 0; N < 16; ++N) { Sl[N] = SSXEE::F(P.C[15 - N]); }   // the .pbd slots hold the coefficients highest power first
		for (int32 R = 0; R < FSimPatch::TessN; ++R)
		{
			for (int32 Col = 0; Col < FSimPatch::TessN; ++Col)
			{
				const float* A = Pow[R];
				const float* B = Pow[Col];
				auto Sum4 = [](float V0, float V1, float V2, float V3, const float* W)
				{
					using SSXEE::MicroMul; using SSXEE::MicroAdd;
					return MicroAdd(MicroAdd(MicroAdd(MicroMul(V0, W[0]), MicroMul(V1, W[1])), MicroMul(V2, W[2])), MicroMul(V3, W[3]));
				};
				FVector3f G[4];
				for (int32 K = 0; K < 4; ++K)
				{
					const FVector3f& S0 = Sl[4 * K]; const FVector3f& S1 = Sl[4 * K + 1]; const FVector3f& S2 = Sl[4 * K + 2]; const FVector3f& S3 = Sl[4 * K + 3];
					G[K] = FVector3f(Sum4(S0.X, S1.X, S2.X, S3.X, A), Sum4(S0.Y, S1.Y, S2.Y, S3.Y, A), Sum4(S0.Z, S1.Z, S2.Z, S3.Z, A));
				}
				const FVector V(Sum4(G[0].X, G[1].X, G[2].X, G[3].X, B), Sum4(G[0].Y, G[1].Y, G[2].Y, G[3].Y, B), Sum4(G[0].Z, G[1].Z, G[2].Z, G[3].Z, B));
				P.Grid[R * FSimPatch::TessN + Col] = V;
				P.Bounds += V;   // the AABB must contain every tessellated triangle
			}
		}
		WorldBounds += P.Bounds;
	}
	BuildBroadPhase();
	RebuildSourceMaps();
}

bool FPatchWorld::GetStartMarker(FVector& OutPos, FVector& OutForward) const
{
	OutPos = StartPos;
	OutForward = StartForward;
	return bHasStart;
}

void FPatchWorld::BuildBroadPhase()
{
	Cells.Reset();
	GridW = GridH = 0;
	if (Patches.Num() == 0)
	{
		return;
	}
	GridOrigin = FVector2D(WorldBounds.Min.X, WorldBounds.Min.Y);
	GridW = FMath::Max(1, FMath::CeilToInt32((WorldBounds.Max.X - GridOrigin.X) / BroadCellSize) + 1);
	GridH = FMath::Max(1, FMath::CeilToInt32((WorldBounds.Max.Y - GridOrigin.Y) / BroadCellSize) + 1);
	Cells.SetNum(GridW * GridH);
	for (int32 K = 0; K < Patches.Num(); ++K)
	{
		const FBox& B = Patches[K].Bounds;
		const int32 X0 = FMath::Clamp(FMath::FloorToInt32((B.Min.X - GridOrigin.X) / BroadCellSize), 0, GridW - 1);
		const int32 X1 = FMath::Clamp(FMath::FloorToInt32((B.Max.X - GridOrigin.X) / BroadCellSize), 0, GridW - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((B.Min.Y - GridOrigin.Y) / BroadCellSize), 0, GridH - 1);
		const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((B.Max.Y - GridOrigin.Y) / BroadCellSize), 0, GridH - 1);
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				Cells[Y * GridW + X].Add(K);
			}
		}
	}
}

void FPatchWorld::GatherCandidates(const FBox& Query) const
{
	Scratch.Reset();
	if (GridW == 0 || !Query.Intersect(WorldBounds))
	{
		return;
	}
	const int32 X0 = FMath::Clamp(FMath::FloorToInt32((Query.Min.X - GridOrigin.X) / BroadCellSize), 0, GridW - 1);
	const int32 X1 = FMath::Clamp(FMath::FloorToInt32((Query.Max.X - GridOrigin.X) / BroadCellSize), 0, GridW - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((Query.Min.Y - GridOrigin.Y) / BroadCellSize), 0, GridH - 1);
	const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((Query.Max.Y - GridOrigin.Y) / BroadCellSize), 0, GridH - 1);
	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			Scratch.Append(Cells[Y * GridW + X]);
		}
	}
	if (X0 != X1 || Y0 != Y1)
	{
		Scratch.Sort();
		int32 Out = 0;
		for (int32 I = 0; I < Scratch.Num(); ++I)
		{
			if (Out == 0 || Scratch[Out - 1] != Scratch[I])
			{
				Scratch[Out++] = Scratch[I];
			}
		}
		Scratch.SetNum(Out, EAllowShrinking::No);
	}
}

bool FPatchWorld::CellOverlaps(const FVector& V00, const FVector& V01, const FVector& V10, const FVector& V11, const FBox& Query)
{
	// 0x25C7C8
	const FVector Lo = V00.ComponentMin(V01).ComponentMin(V10.ComponentMin(V11));
	const FVector Hi = V00.ComponentMax(V01).ComponentMax(V10.ComponentMax(V11));
	return Lo.X < Query.Max.X && Query.Min.X < Hi.X && Lo.Y < Query.Max.Y && Query.Min.Y < Hi.Y && Lo.Z < Query.Max.Z && Query.Min.Z < Hi.Z;
}

bool FPatchWorld::CellNormal(const FVector Tri[3], FVector& OutN)
{
	// 0x25C7C8
	const FVector3f T0 = SSXEE::F(Tri[0]), T1 = SSXEE::F(Tri[1]), T2 = SSXEE::F(Tri[2]);
	const FVector3f NrmF = SSXEE::Cross(SSXEE::Sub(T0, T1), SSXEE::Sub(T1, T2));
	const float Len2 = SSXEE::Dot(NrmF, NrmF);
	if (!(Len2 > 0.0f))
	{
		return false;
	}
	OutN = SSXEE::D(SSXEE::Scale(SSXEE::Rsqrt(Len2), NrmF));
	return true;
}

bool FPatchWorld::PatchLineTest(const FSimPatch& Patch, int32 PatchIndex, const FVector& A, const FVector& D,
                                const FBox& Query, bool bFine, double TRef, FRiderWorldHit& Best, bool& bHaveBest) const
{
	const int32 Step = bFine ? 1 : 3;
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
				continue;   // 0x25C7C8
			}
			for (int32 Which = 0; Which < 2; ++Which)
			{
				const FVector Tri[3] = { Which == 0 ? V11 : V01, V10, Which == 0 ? V01 : V00 };
				FVector Nrm;
				if (!CellNormal(Tri, Nrm))
				{
					continue;
				}
				const double T = SegmentTriangle(A, D, Tri, Nrm);
				if (T < 0.0)
				{
					continue;
				}
				// 0x25AFF8
				if (TRef < 0.0 || !bHaveBest
					|| FMath::Abs(SSXEE::FpuSub(float(T), float(TRef))) < FMath::Abs(SSXEE::FpuSub(float(Best.T), float(TRef))))
				{
					Best = FRiderWorldHit();
					Best.Kind = 0;
					Best.Point = SSXEE::D(SSXEE::Add(SSXEE::F(A), SSXEE::Scale(float(T), SSXEE::F(D))));
					Best.Normal = Nrm;
					Best.Surface = Patch.Surface;
					Best.T = T;
					Best.PatchIndex = PatchIndex;
					Best.CellRow = R / Step;
					Best.CellCol = Col / Step;
					Best.CellTri = Which;
					bHaveBest = true;
				}
				return TRef < 0.0;
			}
		}
	}
	return false;
}

bool FPatchWorld::NewtonRefine(const FSimPatch& Patch, const FVector& A, const FVector& D, double U, double V, double T,
                               FVector& OutPoint, FVector& OutNormal, double& OutU, double& OutV) const
{
	// 0x25DF50
	FVector3f S[16];
	for (int32 N = 0; N < 16; ++N)
	{
		S[N] = SSXEE::F(Patch.C[15 - N]);
	}
	const FVector3f Q = SSXEE::F(A), Dir = SSXEE::F(D);
	float Av = float(V), Bu = float(U);
	FVector3f P, Ja, Jb;
	auto Group = [&S](int32 K, const float W[4])
	{
		const FVector3f& S0 = S[4 * K]; const FVector3f& S1 = S[4 * K + 1]; const FVector3f& S2 = S[4 * K + 2]; const FVector3f& S3 = S[4 * K + 3];
		return FVector3f(((S0.X * W[0] + S1.X * W[1]) + S2.X * W[2]) + S3.X * W[3],
			((S0.Y * W[0] + S1.Y * W[1]) + S2.Y * W[2]) + S3.Y * W[3],
			((S0.Z * W[0] + S1.Z * W[1]) + S2.Z * W[2]) + S3.Z * W[3]);
	};
	auto Dot4 = [](float R0, float R1, float R2, float R3, const float W[4])
	{
		const float M0 = R0 * W[0], M1 = R1 * W[1], M2 = R2 * W[2], M3 = R3 * W[3];
		return ((M0 + M1) + M2) + M3;
	};
	auto Eval = [&]()
	{
		const float Bb = Bu * Bu, Aa = Av * Av;
		const float PowB[4] = { Bb * Bu, Bb, Bu, 1.0f };
		const float PowA[4] = { Aa * Av, Aa, Av, 1.0f };
		const float DerB[4] = { (Bu * 3.0f) * Bu, Bu + Bu, 1.0f, 0.0f };
		const float DerA[4] = { (Av * 3.0f) * Av, Av + Av, 1.0f, 0.0f };
		FVector3f R[4];
		for (int32 K = 0; K < 4; ++K) { R[K] = Group(K, PowA); }
		P = FVector3f(Dot4(R[0].X, R[1].X, R[2].X, R[3].X, PowB), Dot4(R[0].Y, R[1].Y, R[2].Y, R[3].Y, PowB), Dot4(R[0].Z, R[1].Z, R[2].Z, R[3].Z, PowB));
		Jb = FVector3f(Dot4(R[0].X, R[1].X, R[2].X, R[3].X, DerB), Dot4(R[0].Y, R[1].Y, R[2].Y, R[3].Y, DerB), Dot4(R[0].Z, R[1].Z, R[2].Z, R[3].Z, DerB));
		for (int32 K = 0; K < 4; ++K) { R[K] = Group(K, DerA); }
		Ja = FVector3f(Dot4(R[0].X, R[1].X, R[2].X, R[3].X, PowB), Dot4(R[0].Y, R[1].Y, R[2].Y, R[3].Y, PowB), Dot4(R[0].Z, R[1].Z, R[2].Z, R[3].Z, PowB));
	};
	Eval();
	const FVector3f Md(-Dir.X, -Dir.Y, -Dir.Z);
	for (int32 It = 0; It < NewtonIterations; ++It)
	{
		const FVector3f Fr = SSXEE::Sub(Q, P);
		using SSXEE::FpuAdd; using SSXEE::FpuSub;
		const float C9 = FpuSub(Ja.Y * Jb.Z, Ja.Z * Jb.Y);
		const float C5 = FpuSub(Md.Y * Ja.Z, Md.Z * Ja.Y);
		const float C4 = FpuSub(Jb.Y * Md.Z, Jb.Z * Md.Y);
		const float Det = FpuAdd(FpuAdd(Ja.X * C4, Jb.X * C5), Md.X * C9);
		const float C11 = FpuSub(Md.X * Jb.Z, Md.Z * Jb.X);
		const float C8 = FpuSub(Ja.X * Md.Z, Ja.Z * Md.X);
		const float C7 = FpuSub(Jb.X * Md.Y, Jb.Y * Md.X);
		const float C2 = FpuSub(Md.X * Ja.Y, Md.Y * Ja.X);
		const float Inv = 1.0f / Det;
		const float M0 = C4 * Inv, M1 = C5 * Inv, M3 = C11 * Inv, M4 = C8 * Inv, M6 = C7 * Inv, M7 = C2 * Inv;
		Av = FpuAdd(Av, FpuAdd(FpuAdd(M0 * Fr.X, M3 * Fr.Y), M6 * Fr.Z));
		Bu = FpuAdd(Bu, FpuAdd(FpuAdd(M1 * Fr.X, M4 * Fr.Y), M7 * Fr.Z));
		if (Av < 0.0f || 1.0f < Av || Bu < 0.0f || 1.0f < Bu)
		{
			return false;
		}
		Eval();
	}
	const FVector3f Nrm = SSXEE::Cross(Jb, Ja);
	const float Len2 = SSXEE::Dot(Nrm, Nrm);
	if (!(Len2 > 0.0f))
	{
		return false;
	}
	OutPoint = SSXEE::D(P);
	OutNormal = SSXEE::D(SSXEE::Scale(SSXEE::Rsqrt(Len2), Nrm));
	OutU = Bu;
	OutV = Av;
	return true;
}

bool FPatchWorld::LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef, FRiderWorldHit& Out) const
{
	const FVector DirF = SSXEE::D(SSXEE::Sub(SSXEE::F(B), SSXEE::F(A)));   // 0x25D7F0
	const FVector D = DirF;
	const FBox Query(A.ComponentMin(B), A.ComponentMax(B));
	const bool bFine = Type != ERiderLineQuery::Coarse;
	GatherCandidates(Query);

	FRiderWorldHit Best;
	bool bHaveBest = false;
	for (const int32 K : Scratch)
	{
		const FSimPatch& Patch = Patches[K];
		if (Patch.Surface == IgnoredSurface || (Patch.Flags < 0 && !bAlternateSetMode))
		{
			continue;
		}
		if (!Patch.Bounds.Intersect(Query))
		{
			continue;
		}
		if (PatchLineTest(Patch, K, A, D, Query, bFine, TRef, Best, bHaveBest))
		{
			break;
		}
	}
	if (!(bHaveBest && TRef < 0.0) && NumLineInstances > 0)
	{
		FRiderWorldHit InstHit;
		if (InstanceLineQuery(A, B, TRef, InstHit)
			&& (!bHaveBest || FMath::Abs(InstHit.T - TRef) < FMath::Abs(Best.T - TRef)))
		{
			Out = InstHit;
			return true;
		}
	}
	if (!bHaveBest)
	{
		return false;
	}
	if (Type == ERiderLineQuery::FineRefined)
	{
		// 0x25C7C8
		const double U0 = SSXEE::FpuAdd(float(Best.CellCol) * 0.11111111f, 0.055555556f);
		const double V0 = SSXEE::FpuAdd(float(Best.CellRow) * 0.11111111f, 0.055555556f);
		FVector P, Nrm;
		double U, V;
		if (NewtonRefine(Patches[Best.PatchIndex], A, DirF, U0, V0, Best.T, P, Nrm, U, V))
		{
			Best.Point = P;
			Best.Normal = Nrm;
			Best.bRefined = true;
			Best.U = U;
			Best.V = V;
		}
	}
	Out = Best;
	return true;
}

FVector FPatchWorld::FallLineFor(const FVector& N, const FVector& Forward)
{
	// 0x128A18
	const FVector3f Nf = SSXEE::F(N);
	FVector3f A = SSXEE::Cross(FVector3f(0.0f, 0.0f, 1.0f), Nf);
	const float L = SSXEE::Length(A);
	if (!(9.99999975e-05f < L))
	{
		return Forward;
	}
	A = SSXEE::Scale(1.0f / L, A);
	return SSXEE::D(SSXEE::Cross(A, Nf));
}

bool FPatchWorld::ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const
{
	// 0x128AE8
	const FVector3f Nf = SSXEE::F(In.GroundNormal);
	const FVector3f Pf = SSXEE::Add(SSXEE::F(In.Position), SSXEE::Scale(float(In.RiderScale) * float(In.LeanDeg), SSXEE::F(In.ContactLeft)));
	const FVector P = SSXEE::D(Pf);
	const FVector A = SSXEE::D(SSXEE::Add(Pf, SSXEE::Scale(-float(ProbeBelow), Nf)));
	const FVector B = SSXEE::D(SSXEE::Add(Pf, SSXEE::Scale(float(ProbeAbove), Nf)));
	Out = FGroundProbeResult();
	Out.ProbePoint = P;

	FRiderWorldHit Hit;
	if (!LineQuery(A, B, ERiderLineQuery::FineRefined, ProbeTRef, Hit))
	{
		const FVector Z = FVector::UpVector;
		Out.bFound = false;
		Out.GroundNormal = Z;
		const FVector3f Zf(0.0f, 0.0f, 1.0f), Ff = SSXEE::F(In.Forward);
		const FVector3f Fw = SSXEE::Normalize(SSXEE::Sub(Ff, SSXEE::Scale(SSXEE::Dot(Ff, Zf), Zf)));
		Out.ContactForward = SSXEE::D(Fw);
		Out.ContactLeft = SSXEE::D(SSXEE::Cross(Zf, Fw));
		Out.FallLine = In.Forward;
		Out.ContactKind = NoGroundKind;
		return false;
	}
	const FVector N2 = Hit.Normal;
	Out.bFound = true;
	Out.Hit = Hit;
	Out.Surface = Hit.Surface;
	Out.GroundNormal = N2;
	Out.ContactPoint = Hit.Point;
	const FVector3f N2f = SSXEE::F(N2), Ff = SSXEE::F(In.Forward);
	const FVector3f Fw = SSXEE::Normalize(SSXEE::Sub(Ff, SSXEE::Scale(SSXEE::Dot(Ff, N2f), N2f)));
	Out.ContactForward = SSXEE::D(Fw);
	Out.ContactLeft = SSXEE::D(SSXEE::Cross(N2f, Fw));
	Out.Height = SSXEE::Dot(SSXEE::Sub(Pf, SSXEE::F(Hit.Point)), N2f);
	Out.FallLine = FallLineFor(N2, In.Forward);
	Out.ContactKind = Hit.Kind;
	return true;
}
