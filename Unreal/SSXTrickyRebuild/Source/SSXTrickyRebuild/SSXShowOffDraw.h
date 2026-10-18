#pragma once

#include "CoreMinimal.h"

class UWorld;
class FPatchWorld;

namespace SSXShowOff
{
	SSXTRICKYREBUILD_API int32 InstanceOfActor(const class AActor* Actor);

	constexpr const TCHAR* ShowOffTerrainTag = TEXT("SSXShowOffTerrain");

	SSXTRICKYREBUILD_API bool IsShowOffTerrainActor(const class AActor* Actor);

	SSXTRICKYREBUILD_API int32 ApplyDrawRules(UWorld* World, const FPatchWorld& Sim, bool bShowOffMode);
}
