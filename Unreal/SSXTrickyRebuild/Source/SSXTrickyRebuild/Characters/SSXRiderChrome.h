// FUN_001364D0, 0x1DF478, 0x318080, FUN_001C5C88
#pragma once

#include "CoreMinimal.h"

namespace SSXRiderChrome
{
	constexpr int32 ChromeOutfit = 6;
	constexpr int32 EnvFix = 33;
	constexpr int32 EnvTextureIndex = 36;  // 0x33EDF0

	static constexpr const TCHAR* MaterialPath = TEXT("/Game/World/M_SSXRiderLitChrome.M_SSXRiderLitChrome");
	static constexpr const TCHAR* MaskedMaterialPath = TEXT("/Game/World/M_SSXRiderLitChrome_Masked.M_SSXRiderLitChrome_Masked");

	SSXTRICKYREBUILD_API bool IsEnvPassMaterial(const class UMaterialInterface* M);
	SSXTRICKYREBUILD_API bool IsEnvPassName(const FString& Name);
	SSXTRICKYREBUILD_API FString EnvTexturePath();

	/** HLSL of the material's custom nodes; must match what Scripts/make_world_materials.py emits. */
	SSXTRICKYREBUILD_API const TCHAR* EnvUvHlsl();
	SSXTRICKYREBUILD_API const TCHAR* ShadeHlsl();     // "RiderChromeShade": Tex, Env (sRGB samples), V (lit colour, PS2 units)

	SSXTRICKYREBUILD_API FVector2D EnvUv(const FVector& N, const FVector& Right, const FVector& Up, const FVector& Fwd);
	SSXTRICKYREBUILD_API FVector2D EnvStVu(const FMatrix& View, const FMatrix& BoneRows, const FVector& LocalN);
	SSXTRICKYREBUILD_API int32 GsPixel(int32 Tb, int32 Te, int32 Cv);
	SSXTRICKYREBUILD_API double ShadePixel(double T, double E, double V);
}
