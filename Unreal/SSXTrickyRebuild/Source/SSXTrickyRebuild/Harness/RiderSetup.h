#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderSim.h"

class FPatchWorld;
class FRiderInputDriver;

namespace SSXRiderSetup
{
	SSXTRICKYREBUILD_API FString RegisterStates(FRiderSim& Sim);

	SSXTRICKYREBUILD_API FString DescribeStates();

	SSXTRICKYREBUILD_API FString InstallStates(FRiderSim& Sim, FRiderInputDriver& Driver);

	SSXTRICKYREBUILD_API FString RegisterStates(FRiderSim& Sim, bool bUsePathHeading);
	SSXTRICKYREBUILD_API FString InstallStates(FRiderSim& Sim, FRiderInputDriver& Driver, bool bUsePathHeading);

	SSXTRICKYREBUILD_API void BuildSlopeWorld(FPatchWorld& World, double SlopeDeg = 15.0, int32 Surface = 1);

	SSXTRICKYREBUILD_API FQuat RenderedOrientation(const FRiderState& State);

	SSXTRICKYREBUILD_API bool LastLanding(const FRiderSim* Registry, int32& OutFrame, FString& OutText);

	// 0x11CAE8
	SSXTRICKYREBUILD_API void RestInSnow(FRiderSim& Sim);

	/** Topmost ground below GamePos (vertical line query from +5000 to -50000 cm). */
	SSXTRICKYREBUILD_API bool ProjectToGround(const IRiderWorld& World, const FVector& GamePos, FRiderWorldHit& OutHit);

	SSXTRICKYREBUILD_API bool FindGentleStart(const FPatchWorld& World, const FVector& Marker, const FVector& MarkerForward,
	                                    FVector& OutPos, FVector& OutHeading, int32* OutTried = nullptr);
}
