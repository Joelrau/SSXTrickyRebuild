// 0x256188
#pragma once

#include "CoreMinimal.h"

struct FSurfaceParams
{
	static constexpr int32 NumFields = 25;
	float F[NumFields] = {};

	double Gravity() const        { return F[0]; }   // g on the ground and in wipe-out slides
	double DragLinear() const     { return F[1]; }
	double DragQuadratic() const  { return F[2]; }
	double DragCubic() const      { return F[3]; }
	double Grip() const           { return F[4]; }   // lateral grip
	double EdgeDeg() const        { return F[5]; }   // edge / yaw-rate angle (deg)
	double StickHeight() const    { return F[6]; }   // take-off when h > this
	double SinkDepth() const      { return F[7]; }   // rest sink depth (p7)
	double SinkMax() const        { return F[8]; }   // spring max (p8)
	double NormalDamping() const  { return F[9]; }
	double P10() const            { return F[10]; }
	double CruiseKmh() const      { return F[11]; }   // skating target speed (km/h)
};

class SSXTRICKYREBUILD_API FSurfaceTable
{
public:
	bool LoadFromFile(const FString& Path, FString* OutError = nullptr);

	const FSurfaceParams& Get(int32 SurfaceId) const;

	int32 Num() const { return Records.Num(); }
	bool IsLoaded() const { return Records.Num() > 0; }

	static FString DefaultPath();

private:
	TArray<FSurfaceParams> Records;
	FSurfaceParams Fallback = MakeFallback();
	static FSurfaceParams MakeFallback();
};
