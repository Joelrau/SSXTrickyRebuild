// 0x2361B0
#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderWorld.h"

struct FRiderProfile;
struct FRiderState;

namespace SSXRider
{
	constexpr int32 NumShapeSpheres = 18;
	constexpr int32 NumBoneSpheres = 14;
	// spheres 0..13 on these skeleton bones; 14..17 the board spheres M +-70 d, M +-45 d (M = midpoint of spheres 12 / 13,
	// d = unit(c12 - c13)); radii in cm, not scaled
	constexpr int32 SphereBone[NumBoneSpheres] = { 2, 4, 6, 10, 7, 11, 8, 12, 13, 16, 14, 17, 15, 18 };
	constexpr double SphereRadius[NumShapeSpheres] = { 25, 20, 15, 15, 15, 15, 10, 10, 25, 25, 20, 20, 30, 30, 20, 20, 20, 20 };
	// 0x3A3F68
	constexpr double SphereMass[NumShapeSpheres] = { 4, 2, 2, 2, 2, 2, 2, 2, 4, 4, 2, 2, 2, 2, 3, 3, 3, 3 };
	constexpr double BoardSphereLong = 70.0;
	constexpr double BoardSphereShort = 45.0;
	constexpr double BoundRadiusPerScale = 100.0;   // bounding radius = 100 x profile+0x60
	constexpr int32 MaskGroundWall = 0x003;   // Ground / RailSlide wall test: chest + head
	constexpr int32 MaskAir = 0x0CF;
	constexpr int32 MaskWipeOut = 0xFC3;   // WipeOut slide / recover
	constexpr int32 MaskTumble = 0x273C3;   // tumble contact
	constexpr int32 MaskAll = -1;
	constexpr int32 RootBone = 0;

	// Bind-pose REDUCTION in the body frame (cm): model space (Z up, nose +X, facing -Y) -> body (side +X, nose +Y, up +Z)
	// as (x, y, z) -> (-y, x, z), scaled by profile+0x60, turned by pi about up when goofy.
	SSXTRICKYREBUILD_API bool BodyCentres(const FRiderProfile& Profile, bool bGoofy, FVector Out[NumShapeSpheres]);

	// posed centres: world bone translations to the body frame about the rider, Q^-1 (p - Position)
	SSXTRICKYREBUILD_API bool PosedBodyCentres(const FVector* Bones, int32 NumBones, const FVector& Position, const FQuat& Orientation,
	                                     FVector Out[NumShapeSpheres]);

	SSXTRICKYREBUILD_API bool BodyPivot(const FRiderProfile& Profile, bool bGoofy, FVector& Out);

	SSXTRICKYREBUILD_API bool BuildSphereSet(const FRiderProfile& Profile, bool bGoofy, const FVector& Position, const FQuat& Rendered,
	                                   int32 Mask, FRiderSphereSet& Out);

	// 0x114730, 0x126748, 0x1267A0
	SSXTRICKYREBUILD_API bool BuildPostAISphereSet(const FRiderProfile& Profile, const FRiderState& State, int32 Mask, FRiderSphereSet& Out);

}
