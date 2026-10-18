#include "Sim/PatchWorld.h"
#include "Sim/EEFloat.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 RailMagic = 0x52585353;
	constexpr int32 RailHeaderSize = 32;
	constexpr int32 RailSplineRecord = 48;
	constexpr int32 RailSegmentRecord = 112;
	constexpr uint32 RailFlagGameSpace = 1;
	constexpr uint32 RailFlagEnableApprox = 2;

	constexpr int32 CoarseSteps = 4;
	constexpr double GoldenLo = 0.381966;
	constexpr double GoldenHi = 0.618034;
	constexpr int32 GoldenMaxIter = 24;
	constexpr double GoldenTol = 0.0005;
	// 0x3A78E0, 0x3A78F8
	constexpr double BracketLo[5] = { 0.0, 0.0, 0.0, 0.25, 0.25 };
	constexpr double BracketHi[5] = { 0.75, 0.75, 1.0, 1.0, 1.0 };

	// 0x259330
	FORCEINLINE bool InsideGrownF(const FVector3f& P, const FBox& B)
	{
		using SSXEE::FpuAdd; using SSXEE::FpuSub;
		return float(B.Min.X) <= FpuAdd(P.X, 300.0f) && FpuSub(P.X, 300.0f) <= float(B.Max.X)
			&& float(B.Min.Y) <= FpuAdd(P.Y, 300.0f) && FpuSub(P.Y, 300.0f) <= float(B.Max.Y)
			&& float(B.Min.Z) <= FpuAdd(P.Z, 300.0f) && FpuSub(P.Z, 300.0f) <= float(B.Max.Z);
	}
}

FString FPatchWorld::DefaultRailPath(const FString& Track)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Track.ToLower() + TEXT(".rails.bin"));
}

bool FPatchWorld::LoadRails(const FString& RailFile, FString* OutError)
{
	auto Fail = [OutError](const FString& Msg) { if (OutError) { *OutError = Msg; } return false; };
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *RailFile))
	{
		return Fail(FString::Printf(TEXT("cannot read %s (run tools/export_rails.py)"), *RailFile));
	}
	if (Bytes.Num() < RailHeaderSize)
	{
		return Fail(TEXT("rail file too short"));
	}
	const uint8* Data = Bytes.GetData();
	auto U32 = [Data](int64 Off) { uint32 X; FMemory::Memcpy(&X, Data + Off, 4); return X; };
	auto I32 = [Data](int64 Off) { int32 X; FMemory::Memcpy(&X, Data + Off, 4); return X; };
	auto F32 = [Data](int64 Off) { float X; FMemory::Memcpy(&X, Data + Off, 4); return double(X); };
	auto Vec = [&F32](int64 Off) { return FVector(F32(Off), F32(Off + 4), F32(Off + 8)); };
	if (U32(0) != RailMagic || U32(4) != 1)
	{
		return Fail(TEXT("rail file: bad magic or version"));
	}
	const int32 NSpl = int32(U32(8)), NSeg = int32(U32(12));
	const uint32 Flags = U32(16);
	if ((Flags & RailFlagGameSpace) == 0)
	{
		return Fail(TEXT("rail file is not in game space"));
	}
	const int64 SegBase = RailHeaderSize + int64(NSpl) * RailSplineRecord;
	if (Bytes.Num() < SegBase + int64(NSeg) * RailSegmentRecord)
	{
		return Fail(TEXT("rail file truncated"));
	}
	TArray<FSimRailSpline> Spl;
	Spl.SetNum(NSpl);
	for (int32 K = 0; K < NSpl; ++K)
	{
		const int64 O = RailHeaderSize + int64(K) * RailSplineRecord;
		FSimRailSpline& S = Spl[K];
		S.Bounds = FBox(Vec(O), Vec(O + 12));
		S.FirstSegment = I32(O + 24);
		S.SegmentCount = I32(O + 28);
		S.Surface = I32(O + 32);
		S.bEnabled = U32(O + 40) != 0;
		S.bInitiallyEnabled = U32(O + 44) != 0;
	}
	TArray<FSimRailSegment> Seg;
	Seg.SetNum(NSeg);
	for (int32 K = 0; K < NSeg; ++K)
	{
		const int64 O = SegBase + int64(K) * RailSegmentRecord;
		FSimRailSegment& S = Seg[K];
		for (int32 R = 0; R < 4; ++R)
		{
			S.Coef[R] = Vec(O + 12 * R);
		}
		for (int32 R = 0; R < 4; ++R)
		{
			S.DistPoly[R] = float(F32(O + 48 + 4 * R));
		}
		S.Prev = I32(O + 64);
		S.Next = I32(O + 68);
		S.Spline = I32(O + 72);
		S.Bounds = FBox(Vec(O + 76), Vec(O + 88));
		S.Length = F32(O + 100);
		S.StartDist = F32(O + 104);
		if (S.Spline < 0 || S.Spline >= NSpl)
		{
			return Fail(FString::Printf(TEXT("rail file: segment %d has bad spline %d"), K, S.Spline));
		}
	}
	SetRails(MoveTemp(Seg), MoveTemp(Spl));
	bRailEnableApprox = (Flags & RailFlagEnableApprox) != 0;
	return true;
}

void FPatchWorld::SetRails(TArray<FSimRailSegment>&& InSegments, TArray<FSimRailSpline>&& InSplines)
{
	RailSegments = MoveTemp(InSegments);
	RailSplines = MoveTemp(InSplines);
	bRailEnableApprox = false;
}

void FPatchWorld::ResetRailsToInitial()
{
	for (FSimRailSpline& S : RailSplines)
	{
		S.bEnabled = S.bInitiallyEnabled;
	}
}

void FPatchWorld::SetRailEnabled(int32 Spline, bool bEnabled)
{
	if (RailSplines.IsValidIndex(Spline))
	{
		RailSplines[Spline].bEnabled = bEnabled;
	}
}

namespace
{
	// 0x259330
	FORCEINLINE FVector3f EvalF(const FVector3f C[4], float T)
	{
		const float T2 = T * T, T3 = T2 * T;
		return SSXEE::Add(SSXEE::Add(SSXEE::Add(SSXEE::Scale(T3, C[0]), SSXEE::Scale(T2, C[1])), SSXEE::Scale(T, C[2])), C[3]);
	}
	FORCEINLINE float DistF(const FVector3f& A, const FVector3f& B) { return SSXEE::Length(SSXEE::Sub(A, B)); }
}

double FPatchWorld::ClosestOnRailSegment(const FSimRailSegment& Seg, const FVector& InP, double& OutT, FVector& OutPoint)
{
	// 0x259330
	using namespace SSXEE;
	const FVector3f C[4] = { F(Seg.Coef[0]), F(Seg.Coef[1]), F(Seg.Coef[2]), F(Seg.Coef[3]) };
	const FVector3f P = F(InP);
	FVector3f Prev = EvalF(C, 0.0f);
	float BestD = DistF(P, Prev);
	int32 BestI = 0;
	for (int32 I = 1; I <= CoarseSteps; ++I)
	{
		const FVector3f Cur = EvalF(C, float(I) * 0.25f);
		const FVector3f Chord = Sub(Cur, Prev);
		float U = Dot(Sub(P, Prev), Chord) / Dot(Chord, Chord);
		if (U < 0.0f) { U = 0.0f; }
		if (1.0f < U) { U = 1.0f; }
		const float D = DistF(P, Add(Prev, Scale(U, Chord)));
		if (!(BestD <= D))
		{
			BestD = D;
			BestI = I;
		}
		Prev = Cur;
	}
	float A = float(BracketLo[BestI]), B = float(BracketHi[BestI]);
	float X1 = FpuAdd(A, FpuSub(B, A) * 0.381966f);
	float X2 = FpuAdd(A, FpuSub(B, A) * 0.618034f);
	float F1 = DistF(EvalF(C, X1), P);
	float F2 = DistF(EvalF(C, X2), P);
	float LastT = X2, LastD = F2;
	FVector3f LastP = EvalF(C, B);
	for (int32 It = 0; It < GoldenMaxIter && 0.0005f < FMath::Abs(FpuSub(B, A)); ++It)
	{
		if (F2 < F1)
		{
			const float Xn = FpuAdd(X2 * 0.618034f, B * 0.381966f);
			LastP = EvalF(C, Xn);
			LastD = DistF(LastP, P);
			A = X1; X1 = X2; F1 = F2; X2 = Xn; F2 = LastD;
			LastT = Xn;
		}
		else
		{
			const float Xn = FpuAdd(X1 * 0.618034f, A * 0.381966f);
			LastP = EvalF(C, Xn);
			LastD = DistF(LastP, P);
			B = X2; X2 = X1; F2 = F1; X1 = Xn; F1 = LastD;
			LastT = Xn;
		}
	}
	OutT = LastT;
	OutPoint = D(LastP);
	return LastD;
}

bool FPatchWorld::QueryRail(const FRiderRailQuery& In, FRiderRailHit& Out) const
{
	using namespace SSXEE;
	const FVector& P = In.Origin;
	const FVector3f Pf = F(P);
	bool bFound = false;
	float BestD = 0.0f;
	for (int32 S = 0; S < RailSegments.Num(); ++S)
	{
		const FSimRailSegment& Seg = RailSegments[S];
		if (!RailSplines[Seg.Spline].bEnabled || !InsideGrownF(Pf, Seg.Bounds))
		{
			continue;
		}
		double T;
		FVector Pt;
		const float Dd = float(ClosestOnRailSegment(Seg, P, T, Pt));
		if (!bFound || Dd < BestD)
		{
			bFound = true;
			BestD = Dd;
			Out.Point = Pt;
			const float Tf = float(T);
			const FVector3f Dv = Add(Add(Scale((Tf * 3.0f) * Tf, F(Seg.Coef[0])), Scale(FpuAdd(Tf, Tf), F(Seg.Coef[1]))), F(Seg.Coef[2]));
			Out.Tangent = D(Normalize(Dv));
			Out.Surface = RailSplines[Seg.Spline].Surface;
			Out.Segment = S;
			Out.Spline = Seg.Spline;
			Out.Param = T;
			Out.Distance = Dd;
		}
	}
	return bFound;
}
