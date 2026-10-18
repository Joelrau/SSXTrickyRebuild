#pragma once

#include "CoreMinimal.h"

namespace SSXTerrainLod
{
	constexpr int32 VertsPerPatch = 108;
	constexpr int32 MaxTris = 98;
	constexpr int32 NumSignatures = 81;
	constexpr int32 Den = 105;             // lattice coordinates u = U / 105
	constexpr float RadiusA = 3000.0f;     // BezierMan +0x10 (cm)
	constexpr float RadiusB = 15000.0f;    // BezierMan +0x14 (cm)

	/** The four edge levels (4 / 6 / 8) of a patch with corner points C (game space) seen from Eye. Single precision, in
	 *  the original's operation order. */
	SSXTRICKYREBUILD_API void EdgeLevels(const FVector3f C[4], const FVector3f& Eye, uint8 Out[4]);

	SSXTRICKYREBUILD_API int32 SignatureIndex(const uint8 Levels[4]);
	SSXTRICKYREBUILD_API void SignatureLevels(int32 Signature, uint8 Out[4]);

	SSXTRICKYREBUILD_API FIntPoint LatticeUV(int32 Index);
	SSXTRICKYREBUILD_API int32 LatticeIndex(const FIntPoint& UV105);

	SSXTRICKYREBUILD_API void PatchStrips(const uint8 Levels[4], TArray<TArray<int32>>& Out);

	/** Index template of a signature: triangles over the block (3 indices each), counter-clockwise in (u, v); strip triangles with a zero area are dropped (none occur). */
	SSXTRICKYREBUILD_API const TArray<uint32>& Template(int32 Signature);
}

struct SSXTRICKYREBUILD_API FSSXTerrainLodData
{
	struct FPatch
	{
		int32 PbdIndex = 0;
		int32 Texture = 0;
		bool bShowOff = false;
		FVector3f Corners[4];
		FVector2f LightmapOrigin = FVector2f::ZeroVector;
	};
	struct FVertex
	{
		FVector3f Position;
		int8 Normal[4];
		FVector2f Uv0;
		FVector2f Uv1;
	};
	TArray<FPatch> Patches;
	TArray<FVertex> Vertices;

	static FString DefaultPath(const FString& Stem);
	bool Load(const FString& Path, FString* OutError = nullptr);
	const FVertex* Block(int32 Patch) const { return &Vertices[Patch * SSXTerrainLod::VertsPerPatch]; }
};
