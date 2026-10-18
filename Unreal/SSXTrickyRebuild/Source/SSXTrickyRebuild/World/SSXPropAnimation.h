// TODO: the neighbour node creation runs at the end of the frame (the original creates it inside the node update / con
#pragma once

#include "CoreMinimal.h"
#include "Race/GameRng.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/PropControllers.h"
#include "World/PropNodes.h"
#include "World/SSXPropMovie.h"
#include "SSXPropAnimation.generated.h"

class AActor;
class FPatchWorld;
class FRiderSim;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
struct FRiderSphereSet;
namespace SSXEffects { class FEffectData; class FEffectWorld; }
namespace SSXWipeOut { struct FTumbleBody; }
namespace SSXAnim { struct FRiderAnimChannel; }

UCLASS()
class SSXTRICKYREBUILD_API USSXPropAnimation : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* ParamU = TEXT("UVScrollU");
	static constexpr const TCHAR* ParamV = TEXT("UVScrollV");
	static constexpr const TCHAR* CrowdMaterialPath = TEXT("/Game/World/M_SSXPropCrowd.M_SSXPropCrowd");

	static USSXPropAnimation* Get(const UObject* WorldContext);

	SSXProps::FPropControllers& GetControllers() { return Controllers; }

	void BeginTrack(const FString& Track, const SSXEffects::FEffectData* Data, SSXEffects::FEffectWorld* Fx, const TSharedPtr<FPatchWorld>& Sim);
	void RestartTrack();
	void BindSim(FRiderSim& Sim);

	/** Node of type Type on Inst from record Record; Contact = the touch's contact record (or null);
	 *  bByTouch = created by a rider's touch (AnimObject cooldown starts loaded). */
	void OnNodeCreated(int32 Inst, int32 Type, int32 Record, const SSXPropNodes::FContactRecord* Contact, bool bByTouch);
	void OnNodeDestroyed(int32 Inst, int32 Type);
	void OnNodeRestart(int32 Inst);
	void OnRollerStop(int32 Inst);
	void OnNodeMessage(int32 Inst, int32 Type, int32 Msg, double Value);
	// 0x144428
	void OnFenceTouch(int32 Inst, const SSXPropNodes::FContactRecord& Contact);
	void SetCrowdInput(bool bPreRace, TArray<TPair<FVector, int32>>&& Humans) { bCrowdPreRace = bPreRace; CrowdHumans = MoveTemp(Humans); }

	// One 60 Hz frame of every controller and node. Once per sim frame per world.
	void StepFrame();
	void ApplyRender();

	const SSXPropNodes::FPropAnimData& GetData() const { return AnimData; }
	const SSXPropNodes::FAnimObject* FindAnim(int32 Inst) const;
	const SSXPropNodes::FFlagStrip* FindFlag(int32 Inst) const;
	const SSXPropNodes::FFenceStrip* FindFence(int32 Inst) const;
	bool GetStripGame(int32 Inst, FVector OutPos[SSXPropNodes::StripVerts], FVector2D OutUV[SSXPropNodes::StripVerts], FColor& OutColour) const;
	int32 NumStripsDrawn() const { return StripsDrawn; }
	const class USSXSpriteRenderComponent* GetStripRenderer() const { return StripRenderer; }
	int32 NumNodes(int32 Type) const;
	int32 GetRollersKnocked() const { return RollersKnocked; }
	double GetMaxRollerTravel() const { return MaxRollerTravel; }
	int32 GetCrowdSteps() const { return CrowdSteps; }
	/** Movie node (23) state (World/SSXPropMovie.h): the track's shared video stream, its file, the instances that show
	 *  it, the texture and the material slots swapped by the last ApplyRender. */
	const SSXPropMovie::FStream& GetMovieStream() const { return MovieStream; }
	const SSXPropMovie::FSssFile& GetMovieFile() const { return MovieFile; }
	bool HasMovie(int32 Inst) const { return MovieInsts.Contains(Inst); }
	UTexture2D* GetMovieTexture() const { return MovieTexture; }
	/** Front end cFE3DMovieGeometry (vfunc_11 0x269D10, the venue screens): while a stream plays, the instance's model is
	 *  drawn with the stream texture in place of its own. Tex != nullptr binds every BaseTex slot of the instance to Tex
	 *  (kept over flipbook changes); nullptr gives the slots back and re-applies the instance's flipbook frame. */
	void SetTextureOverride(int32 Inst, UTexture* Tex);
	/** Vertex colour 128 (texture x 1.0) on every vertex of LOD 0 of a component, or the mesh's baked colours back
	 *  (the original draws an overridden screen with a flat colour of 128, the baked lighting ignored). */
	static void SetFlatVertexColour(class UStaticMeshComponent* C, bool bFlat);
	static bool HasFlatVertexColour(const class UStaticMeshComponent* C);
	int32 GetMovieFrameUploaded() const { return MovieUploaded; }
	int32 NumMovieSlots(int32 Inst) const { const TArray<FFlipSlot>* S = MovieSlots.Find(Inst); return S ? S->Num() : 0; }

	// 0x7FFFFF
	static double Rand01() { return double(FGameRng::StreamA().Uniform(0.f, 1.f)); }
	static uint32 RandU32() { return FGameRng::StreamA().Next(); }
private:
	struct FNode
	{
		int32 Type = 0;
		int32 Inst = INDEX_NONE;
		SSXPropNodes::FAnimObject Anim;
		SSXPropNodes::FCrowdBox Crowd;
		SSXPropNodes::FRoller Roller;
		FVector X0 = FVector::ZeroVector;
		FQuat4d Q0 = FQuat4d::Identity;
		int32 TreeNode = INDEX_NONE;
		SSXPropNodes::FMeshAnim Mesh;
		TArray<FTransform> PieceStart;
		SSXPropNodes::FFlagStrip Flag;
		SSXPropNodes::FFenceStrip Fence;
		int32 Proxy = INDEX_NONE;
		int32 Record = INDEX_NONE;
		bool bDone = false;
	};
	struct FFlipSlot
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;
		int32 BaseTexture = INDEX_NONE;
		TWeakObjectPtr<class UTexture> Original;
	};
	struct FActorRef
	{
		TWeakObjectPtr<AActor> Actor;
		int32 Obj = 0;
		FTransform Initial;
	};

	void BuildActors();
	void TickNode(FNode& N);
	void MovingObjects(FRiderSim& Sim, const FRiderSphereSet& Shape, SSXAnim::FRiderAnimChannel* Anim);
	// 0x10ED68
	int32 TumbleNeighbours(FRiderSim& Sim, const FRiderSphereSet& Shape, SSXWipeOut::FTumbleBody& Body);
	void SetPoseDeltas(int32 Inst, TConstArrayView<FMatrix> GameDeltas);
	void ResetPose(int32 Inst);
	void UpdateAnimCollision(int32 Inst, TConstArrayView<FMatrix> World);
	/** The animation-node controller on Inst went away: collision back to the static object matrices and the
	 * PropCollision broad phase back to the instance AABB. */
	void EndAnimCollision(int32 Inst);
	FMatrix InstMatrix(int32 Inst) const;
	FMatrix RollerDelta(const FNode& N) const;
	bool BodyHit(const FNode& N, bool bTerrain, SSXPropNodes::FBodyHit& Out) const;
	void ApplyCrowd();
	void ApplyFlipbooks();
	void ApplyStrips();
	// 0x143FF8
	void InitFence(FNode& N, int32 Record);
	// 0x143928
	void FenceNeighbour(int32 FromInst, int32 Side, double Amount);
	void FlushFenceCreations();
	struct FFenceCreate { int32 Inst; int32 Record; double Amount; };
	TArray<FFenceCreate> PendingFences;
	UTexture2D* FlipTexture(int32 Index);
	UTexture2D* CrowdAtlas();

	SSXProps::FPropControllers Controllers;
	SSXPropNodes::FPropAnimData AnimData;
	SSXPropNodes::FMovingObjects Moving;
	const SSXEffects::FEffectData* FxData = nullptr;
	SSXEffects::FEffectWorld* Fx = nullptr;
	TWeakPtr<FPatchWorld> SimWorld;
	TMap<int32, FNode> Nodes;
	TMap<int32, TArray<FMatrix>> LastAnimWorld;
	TMap<int32, TArray<FMatrix>> PendingDeltas;
	TSet<int32> PendingReset;
	TMap<const FRiderSim*, int32> RiderProxies;
	int32 FrameNo = 0;
	int32 RollersKnocked = 0;
	double MaxRollerTravel = 0.0;
	int32 CrowdSteps = 0;
	bool bCrowdPreRace = false;
	TArray<TPair<FVector, int32>> CrowdHumans;
	TMap<int32, TArray<int32>> StaticCrowdFrames;

	TMap<int32, TArray<TWeakObjectPtr<UMaterialInstanceDynamic>>> Materials;
	TMap<int32, TArray<TWeakObjectPtr<UMaterialInstanceDynamic>>> CrowdMids;
	TMap<int32, TArray<FActorRef>> Actors;
	bool bActorsBuilt = false;
	bool bCrowdBuilt = false;
	FString TrackStem;
	TMap<int32, int32> CrackFrames;
	TMap<int32, int32> FlipShown;
	TMap<int32, TArray<FFlipSlot>> FlipSlots;
	TSet<int32> MissingFlipTextures;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UTexture2D>> FlipTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> Atlas;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CrowdMaterial;
	UPROPERTY(Transient) TObjectPtr<class USSXSpriteRenderComponent> StripRenderer;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> StripMaterials;
	TSet<int32> StripHidden;
	int32 StripsDrawn = 0;
	bool bStripSoftDepth = false;

	// 0x1FFF30
	void ApplyMovies();
	void UploadMovieFrame();
	TSet<int32> MovieInsts;
	SSXPropMovie::FStream MovieStream;
	SSXPropMovie::FSssFile MovieFile;
	bool bMovieFileTried = false;
	bool bMovieDirty = false;
	int32 MovieUploaded = INDEX_NONE;
	TMap<int32, TWeakObjectPtr<UTexture>> TexOverrides;
	TMap<int32, TArray<FFlipSlot>> OverrideSlots;
	TMap<int32, TArray<TWeakObjectPtr<UStaticMeshComponent>>> OverrideComps;
	void ApplyTextureOverrides();
	TMap<int32, TArray<FFlipSlot>> MovieSlots;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MovieTexture;

public:
	/** Replay keyframe of the prop nodes. The original's keyframe (0x18F448) has every scene group 3 / 4 node save itself (vfunc 0x44) and a restore (0x18F6C0) deletes every group 3 / 0xD / 4 node and rebuilds each from its saved bytes (0x18F800 -> 0x18F8A8): the nodes go on as they were. A Movie node's restore constructor 0x148AD0 re-opens the stream (0x148B28 -> 0x1FFF30) after the deletion released it (0x148CC8), so the video restarts from its first frame while the texture keeps the old frame. */
	struct FReplayKeyframe
	{
		bool bValid = false;
		TMap<int32, FNode> Nodes;
		TMap<int32, TArray<FMatrix>> LastAnimWorld;
		SSXProps::FPropControllers::FState Controllers;
		SSXPropNodes::FMovingObjects Moving;
		TMap<const FRiderSim*, int32> RiderProxies;
		TMap<int32, int32> CrackFrames;
		TSet<int32> MovieInsts;
		TArray<FFenceCreate> PendingFences;
		int32 FrameNo = 0;
	};
	void SaveReplay(FReplayKeyframe& Out) const;
	/** Put the keyframe back: the current poses / bodies / collision go back to static, the saved nodes return with their
	 *  poses, collision and moving-object proxies; Movie streams restart (see above). */
	void LoadReplay(const FReplayKeyframe& In, TConstArrayView<int32> WasAnimated = {});
	TArray<int32> AnimatedInstances() const { TArray<int32> Out; LastAnimWorld.GetKeys(Out); return Out; }
};
