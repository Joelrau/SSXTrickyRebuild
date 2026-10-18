#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderWorld.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UWorld;

// Walls / props via UE overlap + MTD: spheres mirrored to UE space with SSXSim::GameToUnreal and results mirrored back;
// hit.T = penetration depth (cm), hit.Normal = push-out direction; terrain contacts kind 0, prop contacts kind 1.
// Ground probe, line queries and surfaces go to the analytic patch world (Inner).
class SSXTRICKYREBUILD_API FUnrealRiderWorld : public IRiderWorld
{
public:
	static constexpr double FilterDot = 0.8;

	FUnrealRiderWorld(const IRiderWorld* InInner, UWorld* InWorld, AActor* InIgnore = nullptr);

	virtual bool ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const override { return Inner->ProbeGround(In, Out); }
	virtual bool LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef, FRiderWorldHit& Out) const override
	{
		return Inner->LineQuery(A, B, Type, TRef, Out);
	}
	virtual bool QueryRail(const FRiderRailQuery& In, FRiderRailHit& Out) const override { return Inner->QueryRail(In, Out); }
	virtual const FSurfaceParams& SurfaceParams(int32 SurfaceId) const override { return Inner->SurfaceParams(SurfaceId); }
	virtual double FloorZ() const override { return Inner->FloorZ(); }

	virtual bool QueryWalls(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir, FRiderWorldHit& Out) const override;

	mutable int32 LastWallContacts = 0;
	mutable int32 TotalWallQueries = 0;

private:
	const IRiderWorld* Inner;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> Ignore;
};
