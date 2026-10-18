#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderWorld.h"
#include "Sim/SurfaceTable.h"

// Everything in GAME space (SimCoords.h), double precision: track coordinates reach 3e5 cm.
struct FSimPatch
{
	static constexpr int32 TessN = 10;   // vertices per side: u, v = k/9
	FVector C[16];   // P(u, v) = sum C[4i + j] u^i v^j
	FBox Bounds = FBox(ForceInit);
	int32 Surface = 1;   // patch type
	int32 Flags = 0;   // flag word, negative = alternate (ShowOff) set
	int32 SourceIndex = 0;
	FVector Grid[TessN * TessN];   // Grid[r*10 + c] = P(c/9, r/9)

	SSXTRICKYREBUILD_API void Evaluate(double U, double V, FVector& OutP, FVector& OutDu, FVector& OutDv) const;
	SSXTRICKYREBUILD_API FVector Evaluate(double U, double V) const;
};

struct FSimRailSegment
{
	FVector Coef[4];   // P(t) = Coef[0] t^3 + Coef[1] t^2 + Coef[2] t + Coef[3], t in [0, 1]
	FBox Bounds = FBox(ForceInit);
	int32 Prev = INDEX_NONE, Next = INDEX_NONE;
	int32 Spline = 0;
	double Length = 0.0;                        // cm
	double StartDist = 0.0;                     // cm from the spline start
	float DistPoly[4] = {};

	FVector Evaluate(double T) const { return ((Coef[0] * T + Coef[1]) * T + Coef[2]) * T + Coef[3]; }
	FVector Derivative(double T) const { return (3.0 * T * Coef[0] + 2.0 * Coef[1]) * T + Coef[2]; }
};

struct FSimRailSpline
{
	FBox Bounds = FBox(ForceInit);
	int32 FirstSegment = 0, SegmentCount = 0;
	int32 Surface = FRiderRailHit::FallbackSurface;
	bool bEnabled = true;   // bit 0 of the active flag half
	bool bInitiallyEnabled = true;   // bit 0 of the initial (high) half
};

struct FSimInstance
{
	static constexpr uint8 SetNormal = 1;
	static constexpr uint8 SetAlternate = 2;
	static constexpr uint32 FlagDrawn = 0x1, FlagCollidable = 0x20, FlagReacts = 0x80;
	int32 SourceIndex = 0;
	uint32 Flags = 0;   // .ssf flag word (high half = initial state)
	int32 Surface = -1;
	int32 ModelType = 0;   // .ssf +0x10: 0 none, 1 mesh, 2 box, 3 sphere tree
	uint8 SetBits = SetNormal;
	FBox Bounds = FBox(ForceInit);
	FVector Origin = FVector::ZeroVector;   // instance matrix row 3
	double Mass = 1000.0;   // .ssf +0x00: 0 = bump only
	double Restitution = 0.5;
	int32 TouchEffect = -1;   // event record slot 1, run on every contact (-1 none)
	int32 FirstNode = 0, NumNodes = 0;
	uint16 RunFlags = 0;
	bool bMovable = false;
	bool bAnimController = false;

	bool IsActive(bool bAlternateSetMode) const
	{
		return (SetBits & SetNormal) != 0 || (bAlternateSetMode && (SetBits & SetAlternate) != 0);
	}
	bool IsCollidable() const { return (RunFlags & FlagCollidable) != 0; }
	bool Reacts() const { return (RunFlags & FlagReacts) != 0; }
};

struct FSimInstanceNode
{
	static constexpr uint32 BitTwoSided = 1;   // LOD header +0x1C bit 0
	static constexpr uint32 BitAnimated = 2;
	int32 Kind = 0;
	uint32 Bits = 0;
	FMatrix NodeToGame = FMatrix::Identity;
	FMatrix GameToNode = FMatrix::Identity;
	int32 FirstTri = 0, NumTris = 0;
	int32 Tree = INDEX_NONE;
	FVector BoxMin = FVector::ZeroVector, BoxMax = FVector::ZeroVector;
};

/** Collision-mesh triangle in NODE space with the model's stored face normal (unit). */
struct FSimInstanceTri
{
	FVector V[3];
	FVector N;
};

struct SSXTRICKYREBUILD_API FSimSphereTree
{
	struct FLevel { double Radius = 0.0; double ChildOffset = 0.0; int32 Stride = 1; };
	int32 Depth = 0;
	FVector Centre = FVector::ZeroVector;
	TArray<FLevel> Levels;
	TArray<uint8> Occupancy;

	bool Parse(const uint8* Data, int32 Size, FString* OutError = nullptr);
	// 0x23A728
	bool QuerySphere(const FVector& Q, double R, FVector& OutPush, FVector& OutContact) const;
	int32 CountLeaves() const;

	FVector Com = FVector::ZeroVector;
	double Inertia[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	double InvInertia[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
};

struct FSimLtg
{
	struct FSub
	{
		FVector Min, Max;
		uint16 Counts[6] = {};
		int32 FirstPatch = 0, FirstInst = 0;
		bool IsEmpty() const { return !(Counts[0] | Counts[1] | Counts[2] | Counts[3] | Counts[4] | Counts[5]); }
	};
	struct FCell
	{
		FVector Min, Max;
		FSub Subs[16];
	};
	FVector Min = FVector::ZeroVector;
	double CellSize = 10000.0;
	int32 Nx = 0, Ny = 0;
	TArray<int32> CellIndex;   // Nx * Ny, -1 = empty
	TArray<FCell> Cells;
	TArray<int32> PatchRefs;
	TArray<int32> InstRefs;
	bool IsValid() const { return Nx > 0 && Ny > 0; }
};

enum class ESimTriVariant : uint8
{
	Abs,   // slot 5, main query: |d| <= R
	OneSided,   // slot 5 with query flag +0x2F0 = 1 (PropCollision): 0 <= d <= R
	TwoSided   // slot 6 (node two-sided bit): |d| <= R, n flipped to the sphere's side
};

class SSXTRICKYREBUILD_API FPatchWorld : public IRiderWorld
{
public:
	static constexpr double BroadCellSize = 2500.0;
	static constexpr int32 IgnoredSurface = 17;
	static constexpr double ProbeBelow = 100.0;
	static constexpr double ProbeAbove = 200.0;
	static constexpr double ProbeTRef = 0.5;
	static constexpr int32 NewtonIterations = 4;
	static constexpr double ParallelEps = 1e-10;
	static constexpr double InsideEps = 1e-4;
	static constexpr double FlatEps = 1e-4;
	static constexpr int32 NoGroundKind = 3;

	bool LoadTrack(const FString& PatchFile, FString* OutError = nullptr);
	bool LoadSurfaces(const FString& SurfaceFile, FString* OutError = nullptr) { return Surfaces.LoadFromFile(SurfaceFile, OutError); }

	void SetPatches(TArray<FSimPatch>&& InPatches);

	static FString DefaultTrackPath(const FString& Track);

	virtual bool ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const override;
	virtual bool LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef,
	                       FRiderWorldHit& Out) const override;
	virtual bool QueryWalls(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir,
	                        FRiderWorldHit& Out) const override;
	virtual bool QueryRail(const FRiderRailQuery& In, FRiderRailHit& Out) const override;
	// 0x25B878
	virtual bool QueryProps(const FRiderSphereSet& Shape, FRiderWorldHit& Out, FRiderPropInfo& OutInfo) const override;
	virtual const FSurfaceParams& SurfaceParams(int32 SurfaceId) const override { return Surfaces.Get(SurfaceId); }
	virtual double FloorZ() const override { return WorldBounds.Min.Z; }

	static constexpr double RailQueryMargin = 300.0;
	bool LoadRails(const FString& RailFile, FString* OutError = nullptr);
	void SetRails(TArray<FSimRailSegment>&& InSegments, TArray<FSimRailSpline>&& InSplines);
	void SetRailEnabled(int32 Spline, bool bEnabled);
	bool IsRailEnableApproximate() const { return bRailEnableApprox; }
	void ResetRailsToInitial();
	int32 NumRailSegments() const { return RailSegments.Num(); }
	int32 NumRailSplines() const { return RailSplines.Num(); }
	const FSimRailSegment& GetRailSegment(int32 Index) const { return RailSegments[Index]; }
	const FSimRailSpline& GetRailSpline(int32 Index) const { return RailSplines[Index]; }
	static double ClosestOnRailSegment(const FSimRailSegment& Seg, const FVector& P, double& OutT, FVector& OutPoint);
	static FString DefaultRailPath(const FString& Track);

	bool LoadInstances(const FString& InstanceFile, FString* OutError = nullptr);
	void SetInstances(TArray<FSimInstance>&& InInstances, TArray<FSimInstanceNode>&& InNodes, TArray<FSimInstanceTri>&& InTris,
	                  TArray<FSimSphereTree>&& InTrees = TArray<FSimSphereTree>(), FSimLtg&& InLtg = FSimLtg());
	static FString DefaultInstancePath(const FString& Track);
	bool InstanceLineQuery(const FVector& A, const FVector& B, double TRef, FRiderWorldHit& Out) const;
	bool InstanceSphereQuery(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir, bool bSurfaced,
	                         ESimTriVariant Variant, FRiderWorldHit& Out) const;
	// 0x25BF48
	bool InstanceNarrowPhase(const FSimInstance& I, const FRiderSphereSet& Shape, ESimTriVariant Variant, double TRef,
	                         FRiderWorldHit& Out) const;
	int32 NumInstances() const { return Instances.Num(); }
	const FSimInstance& GetInstance(int32 Index) const { return Instances[Index]; }
	void SetInstanceRunFlags(int32 SourceIndex, uint16 Low);
	void ResetInstanceRunFlags();
	void SetInstanceMovable(int32 SourceIndex, bool bMovable);
	virtual bool IsPropMovable(int32 Instance) const override;
	virtual bool GetPropFlags(int32 Instance, uint32& OutFlags) const override;
	void SetInstanceNodeMatrix(int32 SourceIndex, int32 NodeK, const FMatrix& NodeToGame);
	void SetInstanceAnimController(int32 SourceIndex, bool bLive);
	static FBox PropBroadPhaseBox(const FSimInstance& I);
	const FSimInstanceNode& GetInstanceNode(int32 Index) const { return InstanceNodes[Index]; }
	int32 NumSphereTrees() const { return Trees.Num(); }
	const FSimSphereTree& GetSphereTree(int32 Index) const { return Trees[Index]; }
	int32 FindInstance(int32 SourceIndex) const { return InstanceBySource.IsValidIndex(SourceIndex) ? InstanceBySource[SourceIndex] : INDEX_NONE; }
	bool IsInstanceDrawn(int32 SourceIndex, bool bShowOffMode) const;
	int32 NumDrawRecords() const { return DrawFlags.Num(); }
	void CollectLtgOrder(const FBox& Q, TArray<int32>& Out) const;

	// 0x25AFF8
	bool QueryTree(const FSimSphereTree& Tree, const FMatrix& TreeToGame, bool bUnsurfacedProps, FRiderWorldHit& Out) const;
	// 0x23ADA0
	static double TreeTriangle(const FSimSphereTree& Tree, const FMatrix& TreeToGame, const FVector Tri[3], const FVector& N,
	                           FVector& OutPoint);
	static double SegmentTriangle(const FVector& A, const FVector& D, const FVector Tri[3], const FVector& N);
	// 0x25C7C8
	static bool CellNormal(const FVector Tri[3], FVector& OutN);
	static bool CellOverlaps(const FVector& V00, const FVector& V01, const FVector& V10, const FVector& V11, const FBox& Query);
	// 0x236830
	static FBox SphereSetBox(const FRiderSphereSet& Shape);

	int32 NumPatches() const { return Patches.Num(); }
	const FSimPatch& GetPatch(int32 Index) const { return Patches[Index]; }
	const FSurfaceTable& GetSurfaces() const { return Surfaces; }

	/** Start marker written by the converter (same as the UE PlayerStart). Game space. */
	bool GetStartMarker(FVector& OutPos, FVector& OutForward) const;

	bool bAlternateSetMode = false;

	static FVector FallLineFor(const FVector& N, const FVector& Forward);

private:
	void BuildBroadPhase();
	void GatherCandidates(const FBox& Query) const;
	bool PatchLineTest(const FSimPatch& Patch, int32 PatchIndex, const FVector& A, const FVector& D, const FBox& Query,
	                   bool bFine, double TRef, FRiderWorldHit& Best, bool& bHaveBest) const;
	bool NewtonRefine(const FSimPatch& Patch, const FVector& A, const FVector& D, double U, double V, double T,
	                  FVector& OutPoint, FVector& OutNormal, double& OutU, double& OutV) const;

	TArray<FSimPatch> Patches;
	TArray<FSimInstance> Instances;
	TArray<FSimInstanceNode> InstanceNodes;
	TArray<FSimInstanceTri> InstanceTris;
	TArray<FSimSphereTree> Trees;
	FSimLtg Ltg;
	TArray<int32> InstanceBySource;
	TArray<int32> PatchBySource;
	TArray<uint32> DrawFlags;
	TArray<uint8> DrawSets;
	int32 NumLineInstances = 0;
	void RebuildSourceMaps();
	bool WalkLtg(const FBox& Q, TFunctionRef<bool(int32)> OnPatch, bool bPatches, TFunctionRef<bool(int32)> OnInstance) const;
	bool PatchSphereTest(int32 K, const FRiderSphereSet& Shape, const FBox& Query, bool bFine, double TRef,
	                     const FVector* FilterDir, FRiderWorldHit& Best, bool& bHave) const;
	TArray<FSimRailSegment> RailSegments;
	TArray<FSimRailSpline> RailSplines;
	bool bRailEnableApprox = false;
	FBox WorldBounds = FBox(ForceInit);
	FSurfaceTable Surfaces;
	FVector StartPos = FVector::ZeroVector;
	FVector StartForward = FVector(1, 0, 0);
	bool bHasStart = false;

	FVector2D GridOrigin = FVector2D::ZeroVector;
	int32 GridW = 0, GridH = 0;
	TArray<TArray<int32>> Cells;
	mutable TArray<int32> Scratch;
};
