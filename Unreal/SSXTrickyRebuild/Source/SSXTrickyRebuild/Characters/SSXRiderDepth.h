// 0x1364D0, 0x11F620, FUN_00207C30, 0x1DCC10, 0x1DC3E0, 0x208010, 0x318080
#pragma once

#include "CoreMinimal.h"

namespace SSXRiderDepth
{
	constexpr double PullCm = 35.02286148071289;
	constexpr double MinNear = 10.0;
	constexpr double MinNearPreRace = 100.0;
	constexpr double ZScale = -8388486.0;

	SSXTRICKYREBUILD_API double PullD(double Wp, double Near, double Far, double Pull = PullCm);
	constexpr double GuardSpanX = 896.0355834960938;
	constexpr double GuardSpanY = 1024.032470703125;
	constexpr double FrameW = 512.0;
	constexpr double FrameH = 448.0;

	SSXTRICKYREBUILD_API FVector2D GuardNdc(double ViewTop = 0.0, double ViewHeight = 1.0);
	SSXTRICKYREBUILD_API double VertexScale(double W, double D);
	SSXTRICKYREBUILD_API double GsZOffset(double Wp, double Near, double Far, double Pull = PullCm);
	SSXTRICKYREBUILD_API FVector Wpo(const FVector& Rel, const FVector& RootRel, const FVector& Fwd, double Pull, double Near, double Far,
		bool bOrtho = false);
	/** HLSL of the "RiderDepthPull" node; must match what Scripts/make_world_materials.py emits. */
	SSXTRICKYREBUILD_API bool VertexClipped(double W, double Near, double Far);
	SSXTRICKYREBUILD_API bool VertexDropped(const FVector& Rel, const FVector& Fwd, double Pull, double Near, double Far, bool bOrtho = false);
	SSXTRICKYREBUILD_API bool VertexOutsideGuard(const FVector& Rel, const FVector& Right, const FVector& Up, const FVector& Fwd,
		double CotX, double CotY, const FVector2D& Guard);
	SSXTRICKYREBUILD_API bool VertexDropped(const FVector& Rel, const FVector& Right, const FVector& Up, const FVector& Fwd, double CotX,
		double CotY, const FVector2D& Guard, double Pull, double Near, double Far, bool bOrtho = false);
	SSXTRICKYREBUILD_API const TCHAR* WpoHlsl();
	SSXTRICKYREBUILD_API const TCHAR* ClipHlsl();
	SSXTRICKYREBUILD_API const TCHAR* KeepHlsl();
}
