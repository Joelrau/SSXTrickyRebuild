// 0x1CBBC8 / 0x1CBE20 / 0x2006E8 / 0x1CC088
#pragma once

#include "CoreMinimal.h"

struct FSSXTrackPresentation;
struct FSSXLight;

namespace SSXLensFX
{
	constexpr int32 LensHandle = 28;
	constexpr int32 MaxHalos = 10;                   // 0x1CC088
	constexpr double Ps2ViewW = 512.0;
	constexpr double Ps2ViewH = 448.0;
	constexpr int32 SunBlockW = 16, SunBlockH = 16;  // 0x1CD040
	constexpr int32 HaloBlockW = 16, HaloBlockH = 8; // 0x1CC190
	constexpr double Near = 15.0;
	constexpr int32 NumGlareRays = 37;
	// 0x391CF8
	SSXTRICKYREBUILD_API const double (&GlareRays())[NumGlareRays][2];

	/** Halo size class from the light flags (0x1CC190): radius and pull-in (cm). False for any other flags & 0x70 value (none in the data). */
	struct FHaloClass { double Radius = 0.0, PullIn = 0.0; };
	SSXTRICKYREBUILD_API bool HaloClass(uint32 Flags, FHaloClass& Out);

	struct SSXTRICKYREBUILD_API FParams
	{
		bool bSunOn = false;
		FIntVector GlareRGB = FIntVector::ZeroValue, SunRGB = FIntVector::ZeroValue;
		double GlareAlpha = 0.0, SunRadius = 0.0, SunAlpha = 0.0, AzimuthDeg = 0.0, ElevationDeg = 0.0, SunDist = 0.0;
		double RingBase = 0.0, StreakBase = 0.0, HaloRange = 0.0;
		FVector HaloAlpha = FVector::ZeroVector;
		double Intensity = 1.0;
		/** Camera far plane: track record +0x60, raised to >= 30000 in race modes 1..9 / race type 1. TODO: the exact set of modes that raise it; max(+0x60, 30000) is used. */
		double Far = 30000.0;
		static FParams From(const FSSXTrackPresentation& T);
	};

	struct SSXTRICKYREBUILD_API FCamera
	{
		FVector Pos = FVector::ZeroVector, Fwd = FVector::ForwardVector, Right = FVector::RightVector, Up = FVector::UpVector;
		double CotH = 1.0, CotV = 4.0 / 3.0, Far = 30000.0;
		bool Project(const FVector& P, FVector2D& OutNdc, double& OutDepth) const;
		/** Renderer point frustum test (0x1E8210, 0 = inside): the point through view x the screen matrix +0x1730 built by 0x1DC3E0 (rows (16 fx, 0, 0, 0), (0, -16 fy, 0, 0), (cx, cy, C, 1), (0, 0, D, 0)); x / w and y / w against the GS bounds +0x17B0 / +0x17C0 (|ndc| <= 1), w < 0 culled, and the undivided z' = C d + D against +0x17B8 / +0x17C8, i.e. near < depth < far. Near = vfunc_21's near (camera 15, at least 10; at least 100 in race-handler state 1), far = the projection far. */
		bool InFrustum(const FVector& P) const;
		FVector Unproject(const FVector2D& Ndc, double D) const;
	};

	// 0x1CC088
	struct FHaloPick { int32 Light = INDEX_NONE; double Depth = 0.0; };
	// 0x1CC088
	SSXTRICKYREBUILD_API void InsertHalo(TArray<FHaloPick>& Slots, int32 Light, double Depth);
	// 0x2006E8
	SSXTRICKYREBUILD_API void CollectHalos(const TArray<int32>& Sequence, const TArray<FSSXLight>& Lights, const FCamera& Cam,
		double Range, TArray<FHaloPick>& Out);

	SSXTRICKYREBUILD_API double CentreWeight(const FVector2D& Ndc);
	// 0x1CC190
	SSXTRICKYREBUILD_API FVector HaloSizes(double Radius, const FVector2D& Ndc, double RingBase, double StreakBase);
	SSXTRICKYREBUILD_API double StreakAngle(double NdcX);
	SSXTRICKYREBUILD_API double DistanceFade(double Depth, double Range);
	SSXTRICKYREBUILD_API FVector HaloColour(const FVector& Rgb, double Intensity);
	SSXTRICKYREBUILD_API FVector SunDirectionGame(double AzimuthDeg, double ElevationDeg);
	SSXTRICKYREBUILD_API FVector2D SunDistanceAndSize(double Dist, double Radius, double Far);
	SSXTRICKYREBUILD_API double Visibility(int32 Counted, int32 Inside, int32 Total);
	// 0x1CD040 / 0x1CC190
	SSXTRICKYREBUILD_API FIntPoint BlockOrigin(const FVector2D& Ps2Pixel, int32 W, int32 H);
	inline FVector2D Ps2Pixel(const FVector2D& Ndc) { return FVector2D((Ndc.X + 1.0) * 0.5 * Ps2ViewW, (Ndc.Y + 1.0) * 0.5 * Ps2ViewH); }

	// 0x1CD040
	struct FFanPoint { FVector2D Ndc = FVector2D::ZeroVector; double Weight = 0.0, Falloff = 0.0; };
	SSXTRICKYREBUILD_API void GlareFan(const FVector2D& SunNdc, TArray<FFanPoint>& Out);

	/** UV rect (u0, v0, u1, v1) of a 128 x 128 quadrant of the 256 x 256 `lens`: GS FST corners (u0, v0) and
	 *  (u0 + 127, v0 + 127) texels. Q = 0 TL (core), 1 TR (ring, sun), 2 BL (streak). */
	SSXTRICKYREBUILD_API FVector4f QuadrantUV(int32 Q);

	// GS vertex colour byte of a 0..1 value x 128 (float-to-int truncation as the packets).
	inline uint8 GsByte(double V) { return uint8(FMath::Clamp(int32(V * 128.0), 0, 255)); }

	struct FOcclusion { FVector Point = FVector::ZeroVector; double PullIn = 0.0; int32 BlockW = 0, BlockH = 0; };
	struct FSprite { FVector Centre = FVector::ZeroVector; double Half = 0.0; FVector4f UV; FColor Colour; FOcclusion Occ; };
	struct FQuad { FVector Corner[4]; FVector4f UV; FColor Colour; FOcclusion Occ; };
	struct FTri { FVector P[3]; FColor C[3]; FOcclusion Occ; };
	struct SSXTRICKYREBUILD_API FLensFrame
	{
		TArray<FHaloPick> Halos;
		TArray<FSprite> Sprites;
		TArray<FQuad> Streaks;
		TArray<FTri> Fan;
		bool bSun = false;
		void Reset() { Halos.Reset(); Sprites.Reset(); Streaks.Reset(); Fan.Reset(); bSun = false; }
	};
	// Plane depth (cm) the screen-space fan is built on (any depth inside the view: no depth test).
	constexpr double FanPlaneDepth = 100.0;
	/** One viewport's update (0x1CBE20 -> 0x2006E8, 0x1CD040, 0x1CC190) with V left to the GPU. Lights in GAME space (lights.bin), output in Unreal space. */
	SSXTRICKYREBUILD_API void BuildFrame(const FParams& P, const TArray<FSSXLight>& Lights, const TArray<int32>& HaloSequence,
		const FCamera& Cam, FLensFrame& Out);
}
