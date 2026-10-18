#pragma once

#include "CoreMinimal.h"

namespace SSXGs
{
	/** The GsSprite custom node of every sprite material (Tex = linear texture sample, VC = vertex colour bytes / 255): returns (Cs / 255, As / 128). Must match what Scripts/make_particle_materials.py writes. */
	SSXTRICKYREBUILD_API const TCHAR* SpriteHlsl();

	// Stored PNG alpha byte (min(2 At, 255)) -> the GS texel alpha At (0..128).
	inline int32 TexelAlphaFromPng(int32 P) { return FMath::Min((P + 1) / 2, 128); }

	struct FGsSource { int32 R = 0, G = 0, B = 0, A = 0; };
	SSXTRICKYREBUILD_API FGsSource GsModulate(const FIntVector& Ct, int32 At, const FIntVector& Cv, int32 Av);
	SSXTRICKYREBUILD_API int32 GsBlend(int32 Mode, int32 Cs, int32 As, int32 Cd);

	struct FSpriteNode { FVector3f Cs = FVector3f::ZeroVector; float A = 0.f; };
	SSXTRICKYREBUILD_API FSpriteNode SpriteNode(const FLinearColor& Tex, const FColor& Vc);
	SSXTRICKYREBUILD_API float MaterialBlend(int32 Mode, float Cs, float A, float Cd);

	// FUN_00200390 / 0x1E1F70
	constexpr int32 PropAlphaRef = 1;
	SSXTRICKYREBUILD_API const TCHAR* PropAlphaHlsl();
	SSXTRICKYREBUILD_API float PropAlphaNode(float TexA, float VertexA);
	/** The port's prop pixel on the frame value Cd (unit): core (As >= 128) -> Cs; AREF < As < 128 -> the translucent
	 *  blend; As <= AREF -> Cd (alpha test). Cs = the PROP_SHADE colour (unit). */
	SSXTRICKYREBUILD_API float PropMaterialPixel(float Cs, float As, float Cd);
	SSXTRICKYREBUILD_API int32 GsPropPixel(int32 Cs, int32 As, int32 Cd);

	constexpr float SoftDepthClip = 1.f / 256.f;
	SSXTRICKYREBUILD_API const TCHAR* SoftDepthHlsl();
	SSXTRICKYREBUILD_API const TCHAR* SoftDepthSpriteHlsl();
	SSXTRICKYREBUILD_API bool SoftDepthFront(float PixelDepth, float CustomDepth);
}
