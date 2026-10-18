// 0x1C9D58 / 0x1CA5A0 / 0x2FB5C8 / 0x1CAD28
#pragma once

#include "CoreMinimal.h"

namespace SSXFog
{
	constexpr int32 Texture = 34;
	constexpr double FarFull = 12500.374, FarZero = 20000.182;
	constexpr double NearCull = 500.0, NearFull = 2601.9497;
	constexpr double DepthKeyFar = 40000.0;

	SSXTRICKYREBUILD_API double FarFactor(double D);
	SSXTRICKYREBUILD_API double NearFactor(double W);
	SSXTRICKYREBUILD_API int32 DepthKey(double W);

	// 0x33EAD0
	struct FSortKey { int32 Index = 0; int32 W = 0; };
	// 0x2FB5C8 / 0x1CAD28
	SSXTRICKYREBUILD_API void EeQsortFogKeys(TArrayView<FSortKey> Keys);

	struct FPoint
	{
		FVector Pos = FVector::ZeroVector;   // instance space (game cm)
		FVector Rgb = FVector::OneVector;
		double Radius = 0.0;
	};

	struct FInstance
	{
		FVector X = FVector(1, 0, 0), Y = FVector(0, 1, 0), Z = FVector(0, 0, 1), T = FVector::ZeroVector;
		int32 Model = 0;
		FVector BoxMin = FVector::ZeroVector, BoxMax = FVector::ZeroVector;
		FVector Point(const FVector& P) const { return X * P.X + Y * P.Y + Z * P.Z + T; }
		FVector BoxCentre() const { return (BoxMin + BoxMax) * 0.5; }
	};

	// One puff to draw: game-space centre, world half-size, GS RGBA bytes (truncated as the packet, 128 = 1.0).
	struct FPuff
	{
		FVector Pos = FVector::ZeroVector;
		double Half = 0.0;
		FVector4 Rgba = FVector4(128, 128, 128, 128);
		int32 Instance = -1;
	};

	/** Renderer slot 80 / 81 (0x1E1B10 / 0x1E1E10, VU0 micro 100): the 8 box corners through the view-projection, CLIPw.xyz: 0 inside, 1 outside (every corner outside one plane), 2 intersecting. */
	struct SSXTRICKYREBUILD_API FClipView
	{
		FVector Pos = FVector::ZeroVector;
		FVector Right = FVector(1, 0, 0), Up = FVector(0, 0, 1), Fwd = FVector(0, 1, 0);
		double CotH = 1.0, CotV = 4.0 / 3.0;
		double Near = 15.0, Far = 30000.0;
		int32 ClipFlags(const FVector& P) const;
		int32 BoxTest(const FVector& Lo, const FVector& Hi) const;
	};

	struct FSubCell
	{
		FVector Lo = FVector::ZeroVector, Hi = FVector::ZeroVector;
		bool bEmpty = true;
		TArray<int32> Instances;
	};
	struct FCell
	{
		int32 Index = 0;
		FVector Lo = FVector::ZeroVector, Hi = FVector::ZeroVector;
		FSubCell Subs[16];
	};

	struct SSXTRICKYREBUILD_API FFogData
	{
		TArray<TArray<FPoint>> Models;
		TArray<FInstance> Instances;
		double GridMinX = 0.0, GridMinY = 0.0, CellSize = 10000.0;
		int32 NX = 0, NY = 0;
		TArray<FCell> Cells;
		TMap<int32, int32> CellOfIndex;
		bool HasGrid() const { return NX > 0 && NY > 0; }
		/** FUN_00260278 + FUN_00260458 for a camera: visible cells in list order (capacity 162) with masks +0x36 (visible) / +0x32 (inside). */
		struct FVisible { int32 Cell = 0; uint16 Vis = 0; uint16 In = 0; };
		void VisibleCells(const FClipView& View, int32 Radius, TArray<FVisible>& Out) const;
		void SubmissionOrder(const FClipView& View, int32 Radius, TArray<int32>& Out) const;

		static FString DefaultPath(const FString& Stem);
		bool Load(const FString& Path, FString* OutError = nullptr);
		int32 NumPuffs() const;
		/** This frame's puffs for a camera at CamPos looking along CamForward (game space, unit), in draw order (instances far to near by depth key, each instance's points in qsort order). */
		void Gather(const FVector& CamPos, const FVector& CamForward, TArray<FPuff>& Out) const;
		void Gather(const FClipView& View, int32 Radius, TArray<FPuff>& Out) const;
		void GatherOrder(const FVector& CamPos, const FVector& CamForward, const TArray<int32>& Order, TArray<FPuff>& Out) const;
	};
}
