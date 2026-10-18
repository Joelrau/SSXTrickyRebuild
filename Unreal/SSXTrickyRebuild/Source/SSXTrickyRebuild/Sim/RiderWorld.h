#pragma once

#include "CoreMinimal.h"

struct FSurfaceParams;

enum class ERiderLineQuery : uint8
{
	Coarse = 0,
	Fine = 1,
	FineRefined = 2,
};

struct FRiderWorldHit
{
	int32 Kind = 0;
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;      // unit normal (spheres: push direction)
	int32 Surface = -1;
	double T = -1.0;
	int32 PatchIndex = INDEX_NONE;
	int32 InstanceIndex = INDEX_NONE;
	int32 CellRow = 0, CellCol = 0, CellTri = 0;
	bool bRefined = false;
	double U = 0.0, V = 0.0;
};

struct FGroundProbeInput
{
	FVector Position = FVector::ZeroVector;
	FVector GroundNormal = FVector::UpVector;
	FVector Forward = FVector(0, 1, 0);
	FVector ContactLeft = FVector(-1, 0, 0);
	double LeanDeg = 0.0;                          // lean_cur (+0x244), degrees
	double RiderScale = 1.0;
};

struct FGroundProbeResult
{
	bool bFound = false;
	int32 Surface = -1;
	FVector GroundNormal = FVector::UpVector;
	FVector ContactPoint = FVector::ZeroVector;
	FVector ContactForward = FVector(0, 1, 0);
	FVector ContactLeft = FVector(-1, 0, 0);
	double Height = 0.0;
	FVector FallLine = FVector(0, 1, 0);
	int32 ContactKind = 3;
	FVector ProbePoint = FVector::ZeroVector;
	FRiderWorldHit Hit;
};

struct FRiderPropInfo
{
	int32 Instance = INDEX_NONE;
	uint32 Flags = 0;
	double Mass = 1000.0;
	double Restitution = 0.5;
	int32 TouchEffect = -1;
	bool bMovable = false;
	bool Reacts() const { return ((Flags >> 16) & 0x80) != 0; }
};

struct FRiderSphereSet
{
	static constexpr int32 MaxSpheres = 18;
	FVector BoundCentre = FVector::ZeroVector;
	double BoundRadius = 0.0;
	int32 Count = 0;
	FVector Centres[MaxSpheres];
	double Radii[MaxSpheres] = {};
	int32 Mask = -1;
	// 0x257810, 0x236830, 0x10E190
	bool bHasQueryBox = false;
	FVector QueryBoxMin = FVector::ZeroVector, QueryBoxMax = FVector::ZeroVector;
	// 0x25B878
	bool bFineQuery = false;
};

struct FRiderRailQuery
{
	FVector Origin = FVector::ZeroVector;
};

struct FRiderRailHit
{
	static constexpr int32 FallbackSurface = 13;
	FVector Point = FVector::ZeroVector;
	FVector Tangent = FVector::ZeroVector;         // unit dP/dt at Param
	int32 Surface = FallbackSurface;
	int32 Segment = INDEX_NONE;
	int32 Spline = INDEX_NONE;
	double Param = 0.0;
	double Distance = 0.0;
};

class IRiderWorld
{
public:
	virtual ~IRiderWorld() = default;

	virtual bool ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const = 0;

	virtual bool LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef,
	                       FRiderWorldHit& Out) const = 0;

	virtual bool QueryWalls(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir,
	                        FRiderWorldHit& Out) const
	{
		return false;
	}

	virtual bool QueryProps(const FRiderSphereSet& Shape, FRiderWorldHit& Out, FRiderPropInfo& OutInfo) const
	{
		return false;
	}

	virtual bool IsPropMovable(int32 Instance) const { return false; }

	// 0x125088
	virtual bool GetPropFlags(int32 Instance, uint32& OutFlags) const { return false; }

	virtual bool QueryRail(const FRiderRailQuery& In, FRiderRailHit& Out) const { return false; }

	virtual const FSurfaceParams& SurfaceParams(int32 SurfaceId) const = 0;

	virtual double FloorZ() const = 0;
};
