#include "Race/RacePathTracker.h"
#include "HAL/IConsoleManager.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderPose.h"

#include "Race/RaceRandom.h"

#include "Algo/StableSort.h"

static TAutoConsoleVariable<int32> CVarLineHintReset(TEXT("ssx.Race.LineHintReset"), 0,
	TEXT("1: the line-path re-pick 0x118610(0) drops the projection hint when the current line's probe projection is > 1600 cm off its distance to go"));

namespace SSXRaceTrack
{
	int32 LineClassWanted(int32 BoardClass, int32 Rank, double BoostMeter, double SkillProb, FRaceRandom& Rng)
	{
		int32 Want = LineClassDefault;
		if (BoardClass == 2)
		{
			Want = Rank == 0 ? 0 : (BoostMeter < 0.0 ? 100 : LineClassDefault);
		}
		else if (BoardClass == 0)
		{
			Want = BoostMeter >= ClassMeterBoard0 ? ((Rank - 2 >= 0 && Rank - 2 < 2) ? 0 : LineClassDefault) : 100;
		}
		else
		{
			Want = BoostMeter >= ClassMeterBoard1 ? ((Rank - 3 >= 0 && Rank - 3 < 2) ? 0 : LineClassDefault) : 100;
		}
		// 0x118830
		if (int32(SkillProb * 100.0) < Rng.Mod(100))
		{
			Want = LineClassDefault;
		}
		return Want;
	}

	namespace
	{
		FVector Probe(const FVector& Pos, const FVector& Vel)
		{
			const double Sp = Vel.Size();
			return Sp > 1e-6 ? Pos + Vel / Sp * Lookahead : Pos;
		}

		// FUN_00197F28, FUN_00197830
		void Nearest(const TArray<FRacePath>& Paths, const FVector& P, int32 N, TArray<int32>& Out)
		{
			TArray<TPair<double, int32>> Keys;
			Keys.Reserve(Paths.Num());
			for (const FRacePath& Path : Paths)
			{
				Keys.Add({ Path.BoundsDistance(P), Path.Index });
			}
			Algo::StableSortBy(Keys, [](const TPair<double, int32>& K) { return K.Key; });
			Out.Reset();
			for (int32 I = 0; I < FMath::Min(N, Keys.Num()); ++I)
			{
				Out.Add(Keys[I].Value);
			}
		}
	}

	int32 ChooseLine(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Current, bool bExcludeCurrent, int32 WantClass)
	{
		const FVector P = Probe(Pos, Vel);
		TArray<int32> Ranked;
		Nearest(Set.AIPaths, P, AICandidates, Ranked);
		int32 Best = -1;
		double BestCost = 1e37;
		for (int32 Index : Ranked)
		{
			if (bExcludeCurrent && Index == Current)
			{
				continue;
			}
			const FRacePath& Path = Set.AIPaths[Index];
			FVector Q;
			double Lat;
			const double T = Path.Project(P, Q, Lat);
			if (T > Path.Length() - EndMargin)
			{
				continue;
			}
			const FVector Ahead = Path.PointAt(SSXEE::FpuAdd(float(T), float(Lookahead)));
			double Cost = (P - Ahead).SizeSquared() + (Pos - Q).SizeSquared();
			if (WantClass >= 0)
			{
				Cost -= (100 - FMath::Abs(Path.LineClass - WantClass)) * LineClassWeight;
			}
			if (Cost < BestCost)
			{
				Best = Index;
				BestCost = Cost;
			}
		}
		return Best == Current ? -1 : Best;
	}

	// 0x118610
	int32 PickLinePath(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Exclude, int32 Current,
	                   double CurrentToGo, bool* OutResetHint)
	{
		const FVector P = Probe(Pos, Vel);
		TArray<int32> Cands;
		Nearest(Set.LinePaths, P, LineCandidates, Cands);
		if (Cands.Num() == 0)
		{
			return Exclude;
		}
		int32 Best = Exclude >= 0 ? Exclude : Cands[0];
		double BestCost = 1e37;
		for (int32 Index : Cands)
		{
			if (Index == Exclude)
			{
				continue;
			}
			const FRacePath& Path = Set.LinePaths[Index];
			FVector Q;
			double Lat;
			const double T = Path.Project(P, Q, Lat);
			if (Index == Current && OutResetHint
				&& 1600.0f < FMath::Abs(SSXEE::FpuSub(SSXEE::FpuSub(float(Path.DistToGoAtStart), float(CurrentToGo)), float(T))))
			{
				*OutResetHint = true;
			}
			const FVector Ahead = Path.PointAt(SSXEE::FpuAdd(float(T), float(Lookahead)));
			const double Cost = (P - Ahead).SizeSquared() + (Pos - Q).SizeSquared();
			if (Cost < BestCost)
			{
				Best = Index;
				BestCost = Cost;
			}
		}
		return Best;
	}
}

void FRaceLineFollow::Follow(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Frame, bool bFinished,
                             TFunctionRef<int32()> WantClass)
{
	using namespace SSXRaceTrack;
	if (!Set.AIPaths.IsValidIndex(Path))
	{
		return;
	}
	ParamPrev = Param;
	const FRacePath* L = &Set.AIPaths[Path];
	Param = L->Project(Pos, Point, Lateral, &Cache);
	Look = L->PointAt(SSXEE::FpuAdd(float(Param), float(Lookahead)));
	if (!bFinished)
	{
		int32 New = -1;
		if (Lateral > RepickLateral && Frame % RepickPeriod == 0)
		{
			New = ChooseLine(Set, Pos, Vel, Path, false, WantClass());
		}
		else if (L->Length() - EndMargin < Param)
		{
			New = ChooseLine(Set, Pos, Vel, Path, true, WantClass());
		}
		else if (L->FindEvent(ParamPrev, Param, SSXRaceEvent::Repick))
		{
			New = ChooseLine(Set, Pos, Vel, Path, false, WantClass());
		}
		if (New >= 0)
		{
			SetPath(New);
			L = &Set.AIPaths[Path];
			Param = L->Project(Pos, Point, Lateral, &Cache);
			Look = L->PointAt(SSXEE::FpuAdd(float(Param), float(Lookahead)));
			ParamPrev = Param;
		}
	}
	Heading = SSXAnim::Atan2EE(SSXEE::FpuSub(float(Look.Y), float(Point.Y)), SSXEE::FpuSub(float(Look.X), float(Point.X)));   // 0x2510F8, 0x118090
}

void FRaceLineFollow::Reproject(const FRacePathSet& Set, const FVector& Pos)
{
	if (!Set.AIPaths.IsValidIndex(Path))
	{
		return;
	}
	const FRacePath& L = Set.AIPaths[Path];
	Cache = FRacePathCache();
	Param = L.Project(Pos, Point, Lateral, &Cache);
	ParamPrev = Param;
	Look = L.PointAt(SSXEE::FpuAdd(float(Param), float(SSXRaceTrack::Lookahead)));
	Heading = SSXAnim::Atan2EE(SSXEE::FpuSub(float(Look.Y), float(Point.Y)), SSXEE::FpuSub(float(Look.X), float(Point.X)));   // 0x2510F8, 0x118090
}

void FRaceLineFollow::PlaceAt(const FRacePathSet& Set, double InParam)
{
	if (!Set.AIPaths.IsValidIndex(Path))
	{
		return;
	}
	const FRacePath& L = Set.AIPaths[Path];
	Param = ParamPrev = InParam;
	Point = L.PointAt(InParam);
	Lateral = 0.0;
	Look = L.PointAt(SSXEE::FpuAdd(float(InParam), float(SSXRaceTrack::Lookahead)));
	Heading = SSXAnim::Atan2EE(SSXEE::FpuSub(float(Look.Y), float(Point.Y)), SSXEE::FpuSub(float(Look.X), float(Point.X)));   // 0x2510F8, 0x118090
	Cache.Segment = L.SegmentAt(InParam);
}

void FRaceLineProgress::Query(const FRacePath& Path, double LoToGo, double HiToGo, TArray<const FRacePathEvent*>& Out) const
{
	const double Base = Path.DistToGoAtStart;
	TArray<FRacePathHit> Hits;
	Path.EventsIn(Base - HiToGo, Base - LoToGo, Hits);
	for (const FRacePathHit& H : Hits)
	{
		Out.Add(H.Event);
	}
}

void FRaceLineProgress::Measure(const FRacePathSet& Set, const FVector& Pos)
{
	if (!Set.LinePaths.IsValidIndex(Line))
	{
		return;
	}
	const FRacePath& L = Set.LinePaths[Line];
	FVector Q;
	double Lat;
	DistToGo = L.DistToGoAtStart - L.Project(Pos, Q, Lat, &Cache);
}

bool FRaceLineProgress::RepickMarker(const FRacePath& Path, double BestToGo, double ToGo)
{
	// 0x1969D8, 0x197B50
	const float L = float(Path.DistToGoAtStart);
	const float A = BestToGo < 0.0 ? 0.0f : SSXEE::FpuSub(L, float(BestToGo));
	const float B = ToGo < 0.0 ? float(Path.Length()) : SSXEE::FpuSub(L, float(ToGo));
	for (const FRacePathEvent& E : Path.Events)
	{
		if (float(E.Start) <= B && A <= float(E.End) && E.Type == SSXRaceEvent::Repick)   // 0x197B90
		{
			return true;
		}
	}
	return false;
}

void FRaceLineProgress::Update(const FRacePathSet& Set, const FVector& Pos, const FVector& Vel, int32 Frame, bool bFinished,
                               double LapLength, TArray<FRaceTriggerEvent>& OutEvents)
{
	using namespace SSXRaceTrack;
	MaskPrev = Mask;
	Mask = 0;
	if (!Set.LinePaths.IsValidIndex(Line))
	{
		return;
	}
	const FRacePath* L = &Set.LinePaths[Line];
	FVector Q;
	double Lateral;
	double Param = L->Project(Pos, Q, Lateral, &Cache);
	DistToGo = L->DistToGoAtStart - Param;
	if (LapCount > 0 && LapLength > 0.0 && DistToGo - Best > 0.5 * LapLength)
	{
		Best = DistToGo;
	}
	const bool bProgressed = DistToGo < Best;
	TArray<const FRacePathEvent*> Hits;
	Query(*L, bProgressed ? DistToGo : Best, Best, Hits);
	if (bProgressed)
	{
		Best = DistToGo;
	}
	if (!bFinished)
	{
		// 0x118298, 0x118610, 0x1969D8, 0x197B90
		const int32 Old = Line;
		bool bResetHint = false;
		if (Lateral > RepickLateral && Frame % RepickPeriod == 0)
		{
			Line = PickLinePath(Set, Pos, Vel, -1, Line, DistToGo, &bResetHint);
		}
		else if (L->DistToGoAtStart - L->Length() + EndMargin > DistToGo)
		{
			Line = PickLinePath(Set, Pos, Vel, Line);
		}
		else if (RepickMarker(*L, Best, DistToGo))
		{
			Line = PickLinePath(Set, Pos, Vel, -1, Line, DistToGo, &bResetHint);
		}
		if (bResetHint && Line == Old && CVarLineHintReset.GetValueOnAnyThread() != 0)
		{
			Cache = FRacePathCache();
		}
		if (Line != Old)
		{
			Cache = FRacePathCache();
			L = &Set.LinePaths[Line];
			Param = L->Project(Pos, Q, Lateral, &Cache);
			DistToGo = L->DistToGoAtStart - Param;
			if (DistToGo < Best)
			{
				Query(*L, DistToGo, Best, Hits);
				Best = DistToGo;
			}
		}
	}
	for (const FRacePathEvent* E : Hits)
	{
		Mask |= 1u << (E->Type & 31);
		OutEvents.Add({ E->Type, E->Param, true });
	}
	for (int32 B = 0; B < 32; ++B)
	{
		if (((MaskPrev & ~Mask) >> B) & 1u)
		{
			OutEvents.Add({ B, 0u, false });
		}
	}
}
