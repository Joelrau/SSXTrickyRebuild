#include "Sim/Air/LandingPredictor.h"

#include "Sim/Air/AirMath.h"
#include "Sim/RiderWorld.h"
#include "Sim/EEFloat.h"

namespace SSXAir
{
namespace
{
	// 0x25AFF8, 0x123B10
	bool SweepChord(const IRiderWorld& World, const FVector& A, const FVector& B, double Stretch, ERiderLineQuery Type, FRiderWorldHit& Hit, double& OutU)
	{
		using namespace SSXEE;
		const FVector3f Df = Scale(float(Stretch), Sub(F(B), F(A)));
		const FVector A2 = D(Sub(F(A), Df)), B2 = D(Add(F(B), Df));
		if (!World.LineQuery(A2, B2, Type, 0.0, Hit))
		{
			return false;
		}
		OutU = Hit.T;
		return true;
	}

	// 0x12B340
	void GameAdvance(FVector& P, FVector& V, float T, bool bMode1)
	{
		for (float Rem = T; 0.0f < Rem; Rem -= 0.2f)
		{
			BallisticStep(P, V, double(bMode1 ? FMath::Min(Rem, 0.2f) : T));
		}
	}
}

FLandingPredictorResult PredictLanding(const IRiderWorld* World, const FVector& Position, const FVector& Velocity, bool bRefineHit, ERiderLineQuery QueryType)
{
	FLandingPredictorResult R;
	R.Velocity = Velocity;

	// 0x123B10
	if (Velocity.Z > 0.0)
	{
		float Hi = 4.0f, Lo = 0.0f, Mid = 2.0f;
		for (int32 I = 1;; ++I)
		{
			FVector P = Position, V = Velocity;
			GameAdvance(P, V, Mid, false);
			if (V.Z < 0.0) { Hi = Mid; } else { Lo = Mid; }
			if (I > 9) { break; }
			Mid = SSXEE::FpuAdd(Hi, Lo) * 0.5f;
			if (SSXEE::FpuSub(Hi, Lo) < 0.01f) { break; }
		}
		R.ApexTime = Mid;
	}
	if (!World)
	{
		return R;
	}

	FVector P = Position, V = Velocity;
	float T = 0.13333334f;
	GameAdvance(P, V, T, true);
	float Scale = 1.0f;
	for (int32 K = 0; K < PredictorChords; ++K, Scale *= 1.28f)
	{
		const float Step = Scale * 0.016666668f;
		T = SSXEE::FpuAdd(T, Step);
		FVector P1 = P, V1 = V;
		GameAdvance(P1, V1, Step, true);
		FRiderWorldHit Hit;
		double U = 0.0;
		if (SweepChord(*World, P, P1, PredictorStretch, QueryType, Hit, U))
		{
			R.bHit = true;
			R.ChordIndex = K;
			const float Uf = float(U), Rest = SSXEE::FpuSub(1.0f, Uf);
			R.CoarseHitTime = double(SSXEE::FpuSub(T, Step * Rest));
			R.HitTime = R.CoarseHitTime;
			R.Point = Hit.Point;
			R.Normal = Hit.Normal;
			R.Surface = Hit.Surface;
			R.Velocity = SSXEE::D(SSXEE::Add(SSXEE::Scale(Uf, SSXEE::F(V1)), SSXEE::Scale(Rest, SSXEE::F(V))));
			const double ChordStart = double(T - Step);
			if (bRefineHit)
			{
				FVector Ps = P, Vs = V;
				const double Frame = 1.0 / 60.0;
				const int32 N = FMath::CeilToInt32(Step / Frame) + 1;
				for (int32 J = 0; J < N; ++J)
				{
					FVector Pn = Ps, Vn = Vs;
					BallisticStep(Pn, Vn, Frame);
					FRiderWorldHit SubHit;
					double Us = 0.0;
					if (SweepChord(*World, Ps, Pn, 0.0, QueryType, SubHit, Us))
					{
						R.HitTime = ChordStart + (J + Us) * Frame;
						R.Point = SubHit.Point;
						R.Normal = SubHit.Normal;
						R.Surface = SubHit.Surface;
						R.Velocity = FMath::Lerp(Vs, Vn, Us);
						break;
					}
					Ps = Pn;
					Vs = Vn;
				}
			}
			return R;
		}
		P = P1;
		V = V1;
	}
	return R;
}
}
