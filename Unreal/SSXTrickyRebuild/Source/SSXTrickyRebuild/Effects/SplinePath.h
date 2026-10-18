// 0x13D130 / 0x1FA108 / 0x1FA6A0 / 0x1FA838
#pragma once

#include "CoreMinimal.h"

// Single precision as the game; positions in game space (Z up, cm).
namespace SSXEffects
{
	struct FSplinePathSegment
	{
		FVector3f Coef[4];   // P(t) = A t^3 + B t^2 + C t + D, t in [0, 1]
		float Poly[4] = {};
		int32 Prev = INDEX_NONE;
		int32 Next = INDEX_NONE;
		float Length = 0.f;         // cm
		float Start = 0.f;          // cm from the spline start
	};

	// 0x1FA108
	struct FSplinePathConfig
	{
		int32 Spline = INDEX_NONE;   // 0x13D130: .pbd spline, -1 = no node
		int32 EndMode = 0;   // 0 stop (node done), 1 wrap, 2 ping-pong, 3 hold (+ leader chase)
		int32 Facing = 0;   // 0 yaw + pitch from the tangent, 1 yaw, 2 pitch, 3 neither
		int32 Count = 0;   // model copies
		float Speed = 0.f;          // word 5 (+0x1C): x 100 / 60 = cm per frame (node +0x4C)
		float Heading = 0.f;        // word 6 (+0x20, node +0x44): yaw offset, radians
		bool bDrawSpline = false;   // 0x251748
		float Colour[4] = {};
	};

	// One 0x20-byte point record (node +0x7C): position, then the angles about x (+0x10), y (+0x14, always 0), z (+0x18).
	struct FSplinePathPoint
	{
		FVector4f Pos = FVector4f(0.f, 0.f, 0.f, 1.f);
		FVector3f Angles = FVector3f::ZeroVector;
	};

	struct FSplinePathState
	{
		FSplinePathConfig Config;
		bool bValid = false;
		bool bDone = false;
		float Dist = 0.f;
		float Speed = 0.f;          // +0x4C, cm per frame
		float Spacing = 0.f;
		float Length = 0.f;
		int32 Cursor = INDEX_NONE;
		int32 FirstSegment = INDEX_NONE;
		int32 SegmentCount = 0;
		bool bPlaced = false;
		FVector4f Tangent = FVector4f(0.f, 0.f, 0.f, 0.f);
		TArray<FSplinePathPoint> Points;
	};

	namespace SplinePath
	{
		FSplinePathConfig ConfigFromWords(TFunctionRef<int32(int32)> I, TFunctionRef<float(int32)> F);
		/** Ctor 0x1FA108 (minus the instance side: bounding cube and flag clear 0x6, done by the caller). Segments = the track's segment array; First / Count = the spline's first segment and count. */
		SSXTRICKYREBUILD_API void Init(FSplinePathState& S, const FSplinePathConfig& C, TConstArrayView<FSplinePathSegment> Segments,
			int32 First, int32 Count);
		// 0x2515C8
		void Eval(FSplinePathState& S, TConstArrayView<FSplinePathSegment> Segments, float D, FVector4f& OutPos,
			FVector4f& OutTangent);
		/** vfunc_02 after its alive test (the caller deletes the node when the instance has no controller or flags & 0x800): pose every copy at the current distance, then advance. bReacts = instance flags & 0x80; Leader = position of the lowest-ranked racer (+0x110, first in racer order on ties), read only by end mode 3. */
		SSXTRICKYREBUILD_API void Step(FSplinePathState& S, TConstArrayView<FSplinePathSegment> Segments, bool bReacts = false,
			const FVector3f* Leader = nullptr);
		/** vfunc_03: the copy's model matrix, game space, row vectors (v' = v M): identity, then axis rotations z (+0x18), y (+0x14), x (+0x10), each multiplied on the left; row 3 = the position. */
		SSXTRICKYREBUILD_API FMatrix44f PointMatrix(const FSplinePathPoint& P);
		// 0x250C10
		SSXTRICKYREBUILD_API void SinCos(float X, float& OutSin, float& OutCos);
		// 0x251748
		SSXTRICKYREBUILD_API void SegmentLinePoints(const FSplinePathSegment& Seg, FVector3f Out[10]);
		// 0x251748
		SSXTRICKYREBUILD_API FColor LineColour(const FSplinePathConfig& C);
	}
}
