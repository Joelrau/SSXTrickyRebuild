// 0x140688 / 0x13AAD0 / 0x25BF48 / 0x257810
#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderWorld.h"

class FPatchWorld;
class FRiderSim;

namespace SSXEffects
{
	class FEffectWorld;

	// 0x236758
	SSXTRICKYREBUILD_API FBox RiderProxyBox(const FRiderSphereSet& Shape);

	SSXTRICKYREBUILD_API bool RiderPairsInstance(const FPatchWorld& World, int32 Inst, const FRiderSim& Sim);
	SSXTRICKYREBUILD_API bool RiderInsideInstance(const FPatchWorld& World, int32 Inst, const FRiderSim& Sim);

	SSXTRICKYREBUILD_API void BindBoostQueries(FEffectWorld& Effects, const TSharedPtr<FPatchWorld>& World);
}
