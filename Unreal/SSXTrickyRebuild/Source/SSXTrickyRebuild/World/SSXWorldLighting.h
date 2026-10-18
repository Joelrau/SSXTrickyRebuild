#pragma once

#include "CoreMinimal.h"

struct SSXTRICKYREBUILD_API FSSXTrackPresentation
{
	int32 Id = 0;
	FString Name;
	double DrawDist1P = 30000.0;
	double DrawDist2P = 20000.0;
	bool bMipLodOn = false;
	double MipDist1P = 0.0;          // 0x6C D, cm
	int32 MipShift1P = 0;
	double MipDist2P = 0.0;
	int32 MipShift2P = 0;
	double SnowChance = 0.0;
	double RiderAmbBlend = 0.0;
	FVector RiderAmbTarget = FVector::ZeroVector;
	double RiderLightScale = 1.991;
	FIntVector ClearRGB = FIntVector::ZeroValue;
	// 0x1CBE20 / 0x1CD040 / 0x1CC190
	bool bFlareOn = false;
	FIntVector GlareRGB = FIntVector::ZeroValue;    // 0x04 glare-fan colour bytes [flare_rgb1]
	double GlareAlpha = 0.0;
	double SunRadius = 0.0;                         // 0x14 sun sprite world radius, cm [flare_size]
	FIntVector SunRGB = FIntVector::ZeroValue;      // 0x18 sun sprite colour bytes [flare_rgb2]
	double SunAlpha = 0.0;
	double SunAzimuthDeg = 0.0;
	double SunElevationDeg = 0.0;
	double SunDistance = 0.0;                       // 0x30 cm
	double HaloRingBase = 0.0;
	double HaloStreakBase = 0.0;
	FVector HaloAlpha = FVector::ZeroVector;
	double HaloRange = 0.0;                         // 0x5C halo view-depth range, cm [f5c]

	/** Frame clear RGBA bytes: table 0x48 round-tripped through / 255 and x 255 (identical bytes for all 13 records), alpha 0x80. */
	FColor ClearColour() const;
	FIntVector MipLodParams1P() const;

	static int32 IdForStem(const FString& Stem);
	static bool LoadTable(const FString& Path, TArray<FSSXTrackPresentation>& Out, FString* OutError = nullptr);
	static FString DefaultTablePath();
};

struct FSSXLight
{
	enum EType : int32 { Directional = 0, Spot = 1, Point = 2, Ambient = 3 };
	static constexpr uint32 FlagModulator = 0x100;
	static constexpr uint32 FlagNotModulated = 0x200;

	int32 Type = Ambient;
	uint32 Flags = 0;
	double Intensity = 1.0;
	FVector Colour = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	FVector Position = FVector::ZeroVector;
	double CosCone = 0.0;
	int32 Falloff = 0;
	double Exponent = 0.0;
};

/** What the rider renderer receives for one frame (after the per-track adjustment 0x135EB8).
 * Three directional slots in the GAME's order: ascending score abs(r + g + b), so the
 *  strongest light is slot 2 and a single light lands in slot 2; unused slots are colour 0, direction (1, 0, 0). */
struct FSSXRiderLight
{
	static constexpr int32 Slots = 3;
	FVector Ambient = FVector::ZeroVector;
	FVector Colour[Slots];
	FVector Direction[Slots];
	FVector Modulator = FVector::OneVector;
	FVector Total = FVector::ZeroVector;

	FSSXRiderLight()
	{
		for (int32 I = 0; I < Slots; ++I) { Colour[I] = FVector::ZeroVector; Direction[I] = FVector(1, 0, 0); }
	}
};

// 0x25A028 / 0x25A160
struct SSXTRICKYREBUILD_API FSSXLightSlots
{
	double Score[FSSXRiderLight::Slots] = { 0.0, 0.0, 0.0 };
	FVector Colour[FSSXRiderLight::Slots] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	FVector Direction[FSSXRiderLight::Slots] = { FVector(1, 0, 0), FVector(1, 0, 0), FVector(1, 0, 0) };

	/** score = abs(r + g + b); enters only if strictly greater than slot 0's score, goes to its sorted
	 *  position and the old slot 0 drops out (ties keep the earlier light). Returns the slot taken or -1. */
	int32 Insert(const FVector& InColour, const FVector& InDirection);
};

struct FSSXRiderLightAdjust
{
	double Blend = 0.0;
	FVector Target = FVector::ZeroVector;
	double Scale = 1.991;

	static FSSXRiderLightAdjust From(const FSSXTrackPresentation& T) { return { T.RiderAmbBlend, T.RiderAmbTarget, T.RiderLightScale }; }
};

class SSXTRICKYREBUILD_API FSSXLightWorld
{
public:
	static constexpr double FalloffKnee = 100.0;
	static constexpr int32 SubSide = 4;

	bool Load(const FString& Path, FString* OutError = nullptr);
	static FString DefaultPath(const FString& Stem);

	void SetLights(TArray<FSSXLight>&& InLights) { Lights = MoveTemp(InLights); }
	const TArray<FSSXLight>& GetLights() const { return Lights; }

	const TArray<int32>& SubCellLights(const FVector& GameP) const;

	FSSXRiderLight Probe(const FVector& GameP, const FSSXRiderLightAdjust& Adjust, const FVector& Flash = FVector::ZeroVector) const;
	FSSXRiderLight ProbeList(const TArray<int32>& Indices, const FVector& GameP, const FSSXRiderLightAdjust& Adjust,
		const FVector& Flash = FVector::ZeroVector) const;

	/** The lights with a halo (flags & 0x70) in the .ltg sub-cell halo lists (+0x2C count / +0x40 list, lights.bin
	 * version 2), cell index order, sub-cell k = sy * 4 + sx, list order: what 0x2006E8 walks. Every halo
	 *  light is listed exactly once on every track. The frame's walk is VisibleHaloSequence. */
	const TArray<int32>& GetHaloSequence() const { return HaloSequence; }
	/** 0x2006E8: the halo lists of the viewport's visible cells only (the FUN_0025ECB8 list, in its order: pairs {cell index
	 *  iy * NX + ix, sub-cell mask +0x36}), sub-cells k = 0..15 with their mask bit set, each list in order. */
	void VisibleHaloSequence(TConstArrayView<TPair<int32, uint16>> Cells, TArray<int32>& Out) const;

	static double FalloffFactor(int32 Kind, double D);
	bool IsLoaded() const { return bLoaded; }

private:
	bool Attenuation(const FSSXLight& L, const FVector& P, double& OutAtten, FVector& OutToLight) const;

	TArray<FSSXLight> Lights;
	double MinX = 0.0, MinY = 0.0, Cell = 10000.0, Sub = 2500.0;
	int32 NX = 0, NY = 0;
	TArray<int32> CellBase;
	TArray<TArray<int32>> SubLists;
	TArray<int32> HaloSequence;
	TArray<TArray<int32>> HaloSubLists;
	TArray<int32> Empty;
	bool bLoaded = false;
};

/** Terrain lightmap look: the C++ mirror of the M_SSXTerrain* custom nodes written by Scripts/make_world_materials.py (TERRAIN_SHADE mode 0, LIGHTMAP_UV); checked by SSX.World.LightmapBlend. */
namespace SSXTerrainLook
{
	/** out = clamp((C1 - L.rgb) * L.a / 128, 0, 255), C1 = the (opaque) base texel; 0..255 units, GS ALPHA
	 *  mode 6 = (Cd - Cs) As >> 7 (the material omits the >> 7 truncation: under one 8-bit step). */
	SSXTRICKYREBUILD_API FVector3d Blend(const FVector3d& Base, const FVector4d& Lm);
	/** CLAMP_2 REGION_CLAMP (MINU, MAXU, MINV, MAXV) of a patch rect (x, y, w, h): MIN = trunc(128 x), MAX = MIN + trunc(128 w) - 1 (w for both axes). */
	SSXTRICKYREBUILD_API FIntVector4 RegionClamp(const FVector4d& Rect);
	/** Atlas UV of patch parameter (u, v) for a tile at atlas origin Origin (8 x 4 pages): transposed (page s follows v), the sample point clamped to the centres of the tile's edge texels. */
	SSXTRICKYREBUILD_API FVector2D AtlasUV(const FVector2D& Local, const FVector2D& Origin, bool bSwapUV = true);
	constexpr int32 AtlasCols = 8, AtlasRows = 4, PageSize = 128, TileTexels = 8;
}
