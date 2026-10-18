// 0x1DF478, 0x318080, FUN_00205670, 0x205670, FUN_0025EDF0, 0x25EDF0, 0x346F60
#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

namespace SSXRiderGloss
{
	constexpr int32 NumSpecmaps = 12;
	constexpr int32 FrontEndSpecmap = 11;

	SSXTRICKYREBUILD_API const TCHAR* SpecmapStem(int32 Index);
	SSXTRICKYREBUILD_API FString SpecmapName(int32 Index, bool bEnv);
	// 0x32DE88
	SSXTRICKYREBUILD_API int32 SpecmapForGameTrack(int32 GameTrack, bool bFrontEnd);
	SSXTRICKYREBUILD_API int32 SpecmapForTrackId(int32 TrackId);
	SSXTRICKYREBUILD_API FString SpecmapPath(int32 Index, bool bEnv);
	SSXTRICKYREBUILD_API FString FallbackPath(bool bEnv);

	SSXTRICKYREBUILD_API int32 GlossOfMpfMaterial(float Float0, bool bSlot3, bool bSlot4);
	inline int32 Fix(int32 Gloss) { return FMath::Abs(Gloss) & 0xFF; }
	SSXTRICKYREBUILD_API int32 GlossFromName(const FString& Name);
	SSXTRICKYREBUILD_API int32 GlossOfMaterial(const UMaterialInterface* M);
	SSXTRICKYREBUILD_API FString MaterialKey(const FString& Slot, int32 Gloss);

	/** HLSL of the M_SSXRiderLit gloss nodes; must match what Scripts/make_world_materials.py emits. */
	SSXTRICKYREBUILD_API const TCHAR* UvHlsl();
	SSXTRICKYREBUILD_API const TCHAR* ShadeHlsl();

	SSXTRICKYREBUILD_API int32 GsPixel(int32 Tb, int32 Tg, int32 Cv, int32 FixValue);
	SSXTRICKYREBUILD_API double ShadePixel(double T, double G, double V, double FixF);
	SSXTRICKYREBUILD_API FVector2D Uv(const FVector& N, const FVector& Right, const FVector& Up, const FVector& Fwd);
}
