// 0x25B878
#include "Sim/PatchWorld.h"

#include "Sim/EEFloat.h"

namespace
{
	constexpr double FilterDot = 0.8;

	// 0x236BB8
	bool EdgeEnters(const FVector3f& Cf, const FVector3f& Af, const FVector3f& Bf, float C2, float R2)
	{
		using namespace SSXEE;
		const FVector3f E = Sub(Bf, Af), Cr = Sub(Cf, Af);
		const float L = Length(E);
		const float T = Dot(Cr, E) / L;
		if (T < 0.0f)
		{
			return false;
		}
		const float M = FpuSub(C2, T * T);
		if (R2 < M)
		{
			return false;
		}
		return !(L < FpuSub(T, float(FMath::Sqrt(double(FpuSub(R2, M))))));
	}

	// 0x23B6B0
	bool SphereTouches(const FVector3f& Cf, float R, float D, const FVector3f T[3], const FVector3f& Nf, FVector3f& OutFoot)
	{
		using namespace SSXEE;
		OutFoot = Sub(Cf, Scale(D, Nf));
		if (MicroInside(T[0], T[1], T[2], OutFoot))
		{
			return true;
		}
		const float R2 = R * R;
		const FVector3f C0 = Sub(Cf, T[0]);
		const float C02 = Dot(C0, C0);
		if (C02 < R2)
		{
			return true;
		}
		if (EdgeEnters(Cf, T[0], T[1], C02, R2) || EdgeEnters(Cf, T[0], T[2], C02, R2))
		{
			return true;
		}
		const FVector3f C1 = Sub(Cf, T[1]);
		return EdgeEnters(Cf, T[1], T[2], Dot(C1, C1), R2);
	}

	// 0x257988
	double ShapeTriangle(const FRiderSphereSet& Shape, const FVector Tri[3], const FVector& N, FVector& OutPoint, FVector& OutNormal)
	{
		using namespace SSXEE;
		const FVector3f T[3] = { F(Tri[0]), F(Tri[1]), F(Tri[2]) };
		const FVector3f Nf = F(N);
		const FVector3f Cb = F(Shape.BoundCentre);
		const float Rb = float(Shape.BoundRadius);
		const float Db = Dot(Nf, Sub(Cb, T[0]));
		FVector3f Foot;
		if (Rb < FMath::Abs(Db) || !SphereTouches(Cb, Rb, Db, T, Nf, Foot))
		{
			return -1.0;
		}
		FVector3f Push, Contact;
		if (Shape.Mask == 0 || Shape.Count <= 0)
		{
			Push = Scale(FpuSub(Rb, Db), Nf);
			Contact = Foot;
		}
		else
		{
			const float Unset = 1.0e10f;
			float Best = Unset;
			for (int32 I = 0; I < FMath::Min(Shape.Count, FRiderSphereSet::MaxSpheres); ++I)
			{
				if (!((Shape.Mask >> I) & 1))
				{
					continue;
				}
				const FVector3f Ci = F(Shape.Centres[I]);
				const float Ri = float(Shape.Radii[I]);
				const float Di = Dot(Nf, Sub(Ci, T[0]));
				if (!(Di < Ri) || !(Di < Best))
				{
					continue;
				}
				FVector3f FootI;
				if (!SphereTouches(Ci, Ri, Di, T, Nf, FootI))
				{
					continue;
				}
				Push = Scale(FpuSub(Ri, Di), Nf);
				Contact = FootI;
				Best = Di;
			}
			if (Best == Unset)
			{
				return -1.0;
			}
		}
		const float Depth = Length(Push);
		const float Q = 1.0f / Depth;
		OutNormal = D(Scale(Q, Push));
		OutPoint = D(Contact);
		return Depth;
	}
}

FBox FPatchWorld::SphereSetBox(const FRiderSphereSet& Shape)
{
	// 0x236830
	using namespace SSXEE;
	if (Shape.bHasQueryBox)
	{
		return FBox(Shape.QueryBoxMin, Shape.QueryBoxMax);
	}
	if (Shape.Mask == 0 || Shape.Count <= 0)
	{
		const FVector3f C = F(Shape.BoundCentre), R(float(Shape.BoundRadius));
		return FBox(D(Sub(C, R)), D(Add(C, R)));
	}
	FVector3f Lo(1.0e10f), Hi(-1.0e10f);
	for (int32 I = 0; I < FMath::Min(Shape.Count, FRiderSphereSet::MaxSpheres); ++I)
	{
		if (!((Shape.Mask >> I) & 1))
		{
			continue;
		}
		const FVector3f C = F(Shape.Centres[I]), R(float(Shape.Radii[I]));
		const FVector3f A = Sub(C, R), B = Add(C, R);
		Lo = FVector3f(FMath::Min(Lo.X, A.X), FMath::Min(Lo.Y, A.Y), FMath::Min(Lo.Z, A.Z));
		Hi = FVector3f(FMath::Max(Hi.X, B.X), FMath::Max(Hi.Y, B.Y), FMath::Max(Hi.Z, B.Z));
	}
	return FBox(D(Lo), D(Hi));
}

bool FPatchWorld::PatchSphereTest(int32 K, const FRiderSphereSet& Shape, const FBox& Query, bool bFine, double TRef,
                                  const FVector* FilterDir, FRiderWorldHit& Best, bool& bHave) const
{
	const FSimPatch& Patch = Patches[K];
	if (Patch.Surface == IgnoredSurface || (Patch.Flags < 0 && !bAlternateSetMode))
	{
		return false;
	}
	if (!Patch.Bounds.Intersect(Query))
	{
		return false;
	}
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
				continue;
			}
			for (int32 Which = 0; Which < 2; ++Which)
			{
				const FVector Tri[3] = { Which == 0 ? V11 : V01, V10, Which == 0 ? V01 : V00 };
				FVector Nrm;
				if (!CellNormal(Tri, Nrm))
				{
					continue;
				}
				FVector Point, PushN;
				const double T = ShapeTriangle(Shape, Tri, Nrm, Point, PushN);
				if (T <= 0.0)
				{
					continue;
				}
				// 0x25AFF8
				using SSXEE::FpuSub;
				if (TRef < 0.0 || !bHave || FMath::Abs(FpuSub(float(T), float(TRef))) < FMath::Abs(FpuSub(float(Best.T), float(TRef))))
				{
					if (FilterDir && SSXEE::Dot(SSXEE::F(PushN), SSXEE::F(*FilterDir)) > float(FilterDot))   // 0x2578C0
					{
						return false;
					}
					Best = FRiderWorldHit();
					Best.Kind = 0;
					Best.Point = Point;
					Best.Normal = PushN;
					Best.Surface = Patch.Surface;
					Best.T = T;
					Best.PatchIndex = K;
					Best.CellRow = R;
					Best.CellCol = Col;
					Best.CellTri = Which;
					bHave = true;
				}
				return TRef < 0.0;   // 0x25C7C8
			}
		}
	}
	return false;
}

bool FPatchWorld::QueryWalls(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir, FRiderWorldHit& Out) const
{
	if (Shape.BoundRadius <= 0.0)
	{
		return false;
	}
	const FBox Query = SphereSetBox(Shape);
	const double TRef = bFine ? 2.0 * Shape.BoundRadius : -1.0;
	bool bHave = false;
	FRiderWorldHit Best;
	auto OnInstance = [&](int32 K) -> bool
	{
		const FSimInstance& I = Instances[K];
		if (!I.IsCollidable() || I.Surface == -1 || !(I.Bounds.Min.X < Query.Max.X && Query.Min.X < I.Bounds.Max.X
			&& I.Bounds.Min.Y < Query.Max.Y && Query.Min.Y < I.Bounds.Max.Y && I.Bounds.Min.Z < Query.Max.Z && Query.Min.Z < I.Bounds.Max.Z))
		{
			return false;
		}
		FRiderWorldHit Hit;
		if (!InstanceNarrowPhase(I, Shape, ESimTriVariant::Abs, TRef, Hit))
		{
			return false;
		}
		if (bHave && !(FMath::Abs(Hit.T - TRef) < FMath::Abs(Best.T - TRef)))
		{
			return false;
		}
		if (FilterDir && SSXEE::Dot(SSXEE::F(Hit.Normal), SSXEE::F(*FilterDir)) > float(FilterDot))
		{
			return false;
		}
		Best = Hit;
		bHave = true;
		return TRef < 0.0;
	};
	auto OnPatch = [&](int32 K) -> bool { return PatchSphereTest(K, Shape, Query, bFine, TRef, FilterDir, Best, bHave); };
	bool bDone = false;
	if (Ltg.IsValid())
	{
		bDone = WalkLtg(Query, OnPatch, true, OnInstance);
	}
	else
	{
		GatherCandidates(Query);
		const TArray<int32> Candidates = Scratch;
		for (const int32 K : Candidates)
		{
			if (OnPatch(K))
			{
				bDone = true;
				break;
			}
		}
		for (int32 K = 0; !bDone && K < Instances.Num(); ++K)
		{
			bDone = Instances[K].IsActive(bAlternateSetMode) && OnInstance(K);
		}
	}
	if (bHave)
	{
		Out = Best;
	}
	return bHave;
}
