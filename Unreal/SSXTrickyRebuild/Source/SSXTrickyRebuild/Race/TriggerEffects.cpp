#include "Race/TriggerEffects.h"

#include "Sim/RiderSim.h"

namespace SSXTriggers
{
	void ApplyBoostBurst(FRiderSim& Sim, double Seconds)
	{
		Sim.State.BoostBurstTimer = FMath::Max(Sim.State.BoostBurstTimer, Seconds);
	}

	void ApplyLaunchBoost(FRiderSim& Sim, double Seconds)
	{
		Sim.State.LaunchBoostTimer = FMath::Max(Sim.State.LaunchBoostTimer, Seconds);
	}

	void ApplyForcedReset(FRiderSim& Sim)
	{
		Sim.ForcedReset(true, TEXT("T05 trigger effect 13"));
	}

	bool BoostNodeVelocity(FVector& V, const FVector3f& Dir, float Target, float Gain)
	{
		const FVector3f Vf(V);
		const float Short = Target - (Vf.X * Dir.X + Vf.Y * Dir.Y + Vf.Z * Dir.Z);
		if (!(Short > 0.0f))
		{
			return false;
		}
		const float K = Short * Gain * 0.016666668f;
		V = FVector(Vf + Dir * K);
		return true;
	}
}
