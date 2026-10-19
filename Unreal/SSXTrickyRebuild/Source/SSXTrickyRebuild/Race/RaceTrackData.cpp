#include "Race/RaceTrackData.h"
#include "Sim/EEFloat.h"

#include "Effects/TrackEffects.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	struct FReader
	{
		const TArray<uint8>& B;
		int64 Pos = 0;
		bool bOk = true;

		explicit FReader(const TArray<uint8>& InB) : B(InB) {}

		bool Need(int64 N)
		{
			if (!bOk || Pos + N > B.Num()) { bOk = false; return false; }
			return true;
		}
		uint32 U32()
		{
			if (!Need(4)) { return 0; }
			uint32 V;
			FMemory::Memcpy(&V, B.GetData() + Pos, 4);
			Pos += 4;
			return V;
		}
		int32 I32() { return int32(U32()); }
		double F32()
		{
			if (!Need(4)) { return 0.0; }
			float V;
			FMemory::Memcpy(&V, B.GetData() + Pos, 4);
			Pos += 4;
			return double(V);
		}
		FVector Vec() { const double X = F32(); const double Y = F32(); const double Z = F32(); return FVector(X, Y, Z); }
	};

	bool ReadPath(FReader& R, FRacePath& P, bool bLine, int32 Index)
	{
		P.Index = Index;
		P.bLinePath = bLine;
		P.LineClass = R.I32();
		P.CandidateFlag = R.I32();
		P.DistToGoAtStart = R.F32();
		P.Start = R.Vec();
		P.BoundsMin = R.Vec();
		P.BoundsMax = R.Vec();
		const uint32 NSeg = R.U32();
		const uint32 NEv = R.U32();
		if (!R.bOk || NSeg > 1000000 || NEv > 1000000)
		{
			return false;
		}
		P.Dirs.SetNum(NSeg);
		P.Lengths.SetNum(NSeg);
		for (uint32 I = 0; I < NSeg; ++I)
		{
			P.Dirs[I] = R.Vec();
			P.Lengths[I] = R.F32();
		}
		P.Events.SetNum(NEv);
		for (uint32 I = 0; I < NEv; ++I)
		{
			FRacePathEvent& E = P.Events[I];
			E.Type = R.I32();
			E.FileType = R.I32();
			E.Param = R.U32();
			E.Start = R.F32();
			E.End = R.F32();
		}
		P.Finalise();
		return R.bOk;
	}

	bool ReadSet(FReader& R, FRacePathSet& S)
	{
		const uint32 NStart = R.U32();
		for (uint32 I = 0; I < NStart && R.bOk; ++I)
		{
			S.StartPaths.Add(R.I32());
		}
		const uint32 NAI = R.U32();
		S.AIPaths.SetNum(NAI);
		for (uint32 I = 0; I < NAI; ++I)
		{
			if (!ReadPath(R, S.AIPaths[I], false, I)) { return false; }
		}
		const uint32 NLine = R.U32();
		S.LinePaths.SetNum(NLine);
		for (uint32 I = 0; I < NLine; ++I)
		{
			if (!ReadPath(R, S.LinePaths[I], true, I)) { return false; }
		}
		return R.bOk;
	}
}

void FRacePath::Finalise()
{
	const int32 N = Dirs.Num();
	Knots.SetNum(N + 1);
	Cum.SetNum(N + 1);
	Knots[0] = Start;
	Cum[0] = 0.0;
	// 0x197478, 0x1979F8, 0x102F50
	for (int32 I = 0; I < N; ++I)
	{
		Knots[I + 1] = SSXEE::D(SSXEE::Add(SSXEE::F(Knots[I]), SSXEE::Scale(float(Lengths[I]), SSXEE::F(Dirs[I]))));
		Cum[I + 1] = SSXEE::FpuAdd(float(Cum[I]), float(Lengths[I]));
	}
}

int32 FRacePath::SegmentAt(double D) const
{
	const int32 N = Lengths.Num();
	if (N == 0)
	{
		return 0;
	}
	int32 Lo = 0, Hi = Cum.Num();
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Cum[Mid] <= D) { Lo = Mid + 1; } else { Hi = Mid; }
	}
	return FMath::Clamp(Lo - 1, 0, N - 1);
}

FVector FRacePath::PointAt(double D) const
{
	if (Lengths.Num() == 0)
	{
		return Start;
	}
	if (D >= Cum.Last())
	{
		return Knots.Last();
	}
	// 0x1979F8
	float Df = float(D);
	int32 I = 0;
	while (I < Lengths.Num() - 1 && !(Df < float(Lengths[I])))
	{
		Df = SSXEE::FpuSub(Df, float(Lengths[I]));
		++I;
	}
	return SSXEE::D(SSXEE::Add(SSXEE::F(Knots[I]), SSXEE::Scale(Df, SSXEE::F(Dirs[I]))));
}

FVector FRacePath::TangentAt(double D) const
{
	if (Lengths.Num() == 0)
	{
		return FVector(1, 0, 0);
	}
	const FVector T = Dirs[SegmentAt(D)];
	const double L = T.Size();
	return L > 0.0 ? T / L : FVector(1, 0, 0);
}

double FRacePath::Project(const FVector& P, FVector& OutPoint, double& OutLateral, FRacePathCache* Cache) const
{
	const int32 N = Lengths.Num();
	if (N == 0)
	{
		OutPoint = Start;
		OutLateral = FVector2D(P.X - Start.X, P.Y - Start.Y).Size();
		return 0.0;
	}
	const int32 StartSeg = Cache ? Cache->Segment : -1;
	// 0x197478
	const FVector3f Pf = SSXEE::F(P);
	auto Seg = [&](int32 K, double& OutRaw, double& OutT, FVector& OutQ) -> double
	{
		const FVector3f A = SSXEE::F(Knots[K]), Dd = SSXEE::F(Dirs[K]);
		const FVector3f R = SSXEE::Sub(Pf, A);
		const float Raw = SSXEE::FpuAdd(R.X * Dd.X, R.Y * Dd.Y);
		const float Len = float(Lengths[K]);
		const float T = Raw < 0.0f ? 0.0f : (Len < Raw ? Len : Raw);
		const FVector3f Q = SSXEE::Add(A, SSXEE::Scale(T, Dd));
		const FVector3f R2 = SSXEE::Sub(Pf, Q);
		OutRaw = Raw;
		OutT = T;
		OutQ = SSXEE::D(Q);
		return SSXEE::FpuAdd(R2.X * R2.X, R2.Y * R2.Y);
	};
	if (StartSeg < 0 || StartSeg >= N)
	{
		double Best = TNumericLimits<double>::Max(), BestT = 0.0;
		int32 BestK = 0;
		FVector BestQ = Start;
		for (int32 K = 0; K < N; ++K)
		{
			double Raw, T;
			FVector Q;
			const double D2 = Seg(K, Raw, T, Q);
			if (D2 < Best)
			{
				Best = D2; BestK = K; BestT = T; BestQ = Q;
			}
		}
		if (Cache) { Cache->Segment = BestK; }
		OutPoint = BestQ;
		OutLateral = float(FMath::Sqrt(Best));
		return SSXEE::FpuAdd(float(Cum[BestK]), float(BestT));
	}
	const float Limit = SSXEE::FpuAdd(float(Cum[StartSeg]), 3000.0f);
	bool bHave = false;
	double Best = 0.0, BestT = 0.0;
	int32 BestK = StartSeg;
	FVector BestQ = Start;
	for (int32 K = StartSeg; K < N; ++K)
	{
		double Raw, T;
		FVector Q;
		const double D2 = Seg(K, Raw, T, Q);
		if (!bHave || D2 < Best)
		{
			bHave = true; Best = D2; BestK = K; BestT = T; BestQ = Q;
			if (T == Raw)
			{
				break;
			}
		}
		if (Limit < float(Cum[K]))
		{
			break;
		}
	}
	Cache->Segment = BestK;
	OutPoint = BestQ;
	OutLateral = float(FMath::Sqrt(Best));
	return SSXEE::FpuAdd(float(Cum[BestK]), float(BestT));
}

double FRacePath::BoundsDistance(const FVector& P) const
{
	double M = 0.0;
	for (int32 A = 0; A < 3; ++A)
	{
		const double Out = FMath::Max(BoundsMin[A] - P[A], 0.0) + FMath::Max(P[A] - BoundsMax[A], 0.0);
		M = FMath::Max(M, Out);
	}
	return M;
}

void FRacePath::EventsIn(double D0, double D1, TArray<FRacePathHit>& Out) const
{
	Out.Reset();
	if (D1 < D0)
	{
		return;
	}
	for (const FRacePathEvent& E : Events)
	{
		if (D1 < E.Start || E.End < D0)
		{
			continue;
		}
		Out.Add({ &E, D0 < E.Start || D1 < E.End });
	}
}

const FRacePathEvent* FRacePath::FindEvent(double D0, double D1, int32 Type, bool* bOutEdge) const
{
	const FRacePathEvent* Found = nullptr;
	bool bEdge = false;
	if (D1 >= D0)
	{
		for (const FRacePathEvent& E : Events)
		{
			if (E.Type != Type || D1 < E.Start || E.End < D0)
			{
				continue;
			}
			if (!Found) { Found = &E; }
			bEdge = bEdge || D0 < E.Start || D1 < E.End;
		}
	}
	if (bOutEdge) { *bOutEdge = bEdge; }
	return Found;
}

const FRacePath* FRacePathSet::MainLine() const
{
	const FRacePath* Best = nullptr;
	for (const FRacePath& P : LinePaths)
	{
		if (!Best || P.DistToGoAtStart > Best->DistToGoAtStart)
		{
			Best = &P;
		}
	}
	return Best;
}

double FRacePathSet::RaceLength() const
{
	const FRacePath* M = MainLine();
	return M ? M->DistToGoAtStart : 0.0;
}

FString FRaceTrackData::DefaultPath(const FString& Track)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Track.ToLower() + TEXT(".race.bin"));
}

bool FRaceTrackData::Load(const FString& File, FString* OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s (run tools/export_race_data.py)"), *File); }
		return false;
	}
	if (Bytes.Num() < 8 || FMemory::Memcmp(Bytes.GetData(), "SSXR", 4) != 0)
	{
		if (OutError) { *OutError = TEXT("not a race data file"); }
		return false;
	}
	FReader R(Bytes);
	R.Pos = 4;
	const uint32 Version = R.U32();
	if (Version != 1)
	{
		if (OutError) { *OutError = FString::Printf(TEXT("unsupported race data version %u"), Version); }
		return false;
	}
	*this = FRaceTrackData();
	TrackId = R.I32();
	if (TrackId < 0)
	{
		TrackId = SSXEffects::TrackIdForStem(FPaths::GetBaseFilename(FPaths::GetBaseFilename(File)));
	}
	GameTrack = GameTrackOf(TrackId);
	const uint32 Flags = R.U32();
	WorldFloorZ = R.F32();
	LtgBoundsMin = R.Vec();
	LtgBoundsMax = R.Vec();
	bHasWorldFloor = (Flags & 1u) != 0;
	const uint32 NSets = R.U32();
	if (NSets < 1 || !ReadSet(R, Race))
	{
		if (OutError) { *OutError = TEXT("corrupt .aip set"); }
		return false;
	}
	if (NSets > 1 && !ReadSet(R, Showoff))
	{
		if (OutError) { *OutError = TEXT("corrupt .sop set"); }
		return false;
	}
	const uint32 NBurst = R.U32();
	for (uint32 I = 0; I < NBurst && R.bOk; ++I) { BurstSeconds.Add(R.F32()); }
	const uint32 NLaunch = R.U32();
	for (uint32 I = 0; I < NLaunch && R.bOk; ++I) { LaunchSeconds.Add(R.F32()); }
	ResetEffectCount = R.I32();
	const uint32 NNodes = R.U32();
	for (uint32 I = 0; I < NNodes && R.bOk; ++I)
	{
		FRaceSsfNode& N = Nodes.AddDefaulted_GetRef();
		N.FileOffset = R.U32();
		N.NodeClass = R.I32();
		N.Mode = R.I32();
		N.Seconds = R.F32();
		N.Gain = R.F32();
		N.TargetSpeed = R.F32();
		N.Direction = R.Vec();
	}
	if (!R.bOk)
	{
		if (OutError) { *OutError = TEXT("truncated race data file"); }
		return false;
	}
	return true;
}

int32 FRaceTrackData::GameTrackOf(int32 DataTrackId)
{
	// tools/export_race_data.py numbers the tracks by stem order (trick 9, alaska 10); the game has 9 = Big Air Dome
	// (no track data), 10 = trick tutorial, 11 = Alaska (fe_tables track_names, showoff thresholds 0x16BB58).
	switch (DataTrackId)
	{
	case 9: return 10;
	case 10: return 11;
	default: return DataTrackId;
	}
}
