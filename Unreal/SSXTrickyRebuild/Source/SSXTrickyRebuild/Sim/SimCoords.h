// The sim runs in GAME space: right-handed, Z up, cm, s, rad (lean in degrees), fixed 60 Hz.
// basis_forward = q*(0,1,0) (nose), basis_side = q*(1,0,0) is the RIGHT axis, contact_left = n x f points LEFT,
// positive steer turns left (CCW about +Z), patch normals normalize(dP/du x dP/dv) point up.
// Unreal = game with Y mirrored: game (x, y, z) -> Unreal (x, -y, z). The mirror flips handedness (cross products
// negate, a CCW game yaw is a CW Unreal yaw); only boundary code (render transform, camera, UE queries) uses these helpers.
#pragma once

#include "CoreMinimal.h"

namespace SSXSim
{
	FORCEINLINE FVector RsqrtNormal(const FVector& V, const FVector& IfZero = FVector::ZeroVector)
	{
		const double S = V.SizeSquared();
		return S > 0.0 && FMath::IsFinite(S) ? V * FMath::InvSqrt(S) : IfZero;
	}

	FORCEINLINE FVector GameToUnreal(const FVector& V) { return FVector(V.X, -V.Y, V.Z); }

	FORCEINLINE FVector UnrealToGame(const FVector& V) { return FVector(V.X, -V.Y, V.Z); }

	// rotation under the Y mirror: (x, y, z, w) -> (-x, y, -z, w)
	FORCEINLINE FQuat GameQuatToUnreal(const FQuat& Q) { return FQuat(-Q.X, Q.Y, -Q.Z, Q.W); }

	FORCEINLINE FQuat RiderBodyToUnrealRotation(const FVector& GameForward, const FVector& GameUp)
	{
		return FRotationMatrix::MakeFromXZ(GameToUnreal(GameForward), GameToUnreal(GameUp)).ToQuat();
	}

	constexpr double SimHz = 60.0;
	constexpr double SimDt = 1.0 / 60.0;
}
