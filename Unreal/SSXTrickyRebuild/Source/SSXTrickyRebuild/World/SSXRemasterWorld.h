// Remastered look in one world (World/SSXRenderLook.h): lit world materials, a sun from the track's own directional
// light, a sky light and a Lumen post-process volume. Render-only; Classic restores every material and volume.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"
#include "SSXRemasterWorld.generated.h"

class AActor;
class APostProcessVolume;
class ULevel;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UTexture;
struct FSSXLight;

namespace SSXRemaster
{
	/** Remastered parent for a Classic world material (by the root material's name), or null when the material keeps
	 *  its Classic look (riders and particles have their own swaps; lines, crowd). */
	SSXTRICKYREBUILD_API const TCHAR* RemasteredParentPath(const FString& ClassicRootName);
	/** /Game/RemasterData object path of a derived map (Suffix "C", "N" or "M") for a texture imported from
	 *  converted/<root>/<stem>/textures/<name>.png (/Game/Tracks/<Map>/<stem>/Textures/<name>,
	 *  /Game/Characters/<rider>/.../<stem>/Textures/<name>); empty when the path has no Textures folder. */
	SSXTRICKYREBUILD_API FString DerivedMapPath(const FString& TexturePackage, const TCHAR* Suffix);
	/** Index of the track's sun: the strongest (|r + g + b|) directional light that is not a modulator; INDEX_NONE if none. */
	SSXTRICKYREBUILD_API int32 SunLight(TConstArrayView<FSSXLight> Lights);

	constexpr const TCHAR* ScenePostPath = TEXT("/Game/Remastered/PP_RmScene.PP_RmScene");
	constexpr const TCHAR* CollectionPath = TEXT("/Game/Remastered/MPC_SSXRemaster.MPC_SSXRemaster");
	/** Lit rider parent for a Classic rider parent's name (M_SSXRiderLit, _Masked, Chrome, Chrome_Masked). */
	SSXTRICKYREBUILD_API const TCHAR* RemasteredRiderPath(const FString& ClassicParentName);

	/** Remastered tuning of one track (art direction, no original counterpart). */
	struct FTrackTuning
	{
		const TCHAR* Stem = nullptr;
		float SunLux = 2.0f;
		float SunSaturation = 0.6f;     // share of the track light's colour kept (the rest white): the baked ambient carries the palette
		float SkyLight = 0.5f;
		float BakedAmbient = 0.55f;     // MPC_SSXRemaster values
		float Exposure = 1.0f;
		float Saturation = 1.15f;
		float Emissive = 3.0f;
		float FogDensity = 0.02f;
		float Bloom = 0.5f;             // bloom intensity, threshold 1 (highlights only)
		float LightCandela = 15.f;      // track point / spot lights: candela per unit of the original colour
		float RiderFill = 0.6f;         // share of the Classic rider shading kept under the new light (1 = all of it)
		int32 MaxLights = 48;
		bool bLightShadows = false;     // every point / spot light casts shadows at every quality (the front end: few lights)
	};
	SSXTRICKYREBUILD_API const FTrackTuning& TuningFor(const FString& Stem);
	/** Attenuation radius (cm) of a track light: where the original falloff (FSSXLightWorld::FalloffFactor) is 5 %. */
	SSXTRICKYREBUILD_API float LightRadius(int32 FalloffKind);
	/** Indices of the track's point and spot lights (not modulators) by falling strength, at most Max. */
	SSXTRICKYREBUILD_API TArray<int32> StrongestLocalLights(TConstArrayView<FSSXLight> Lights, int32 Max);
	/** The Remastered look draws real rider shadows in this world (a sun of at least 1 lux); elsewhere the original
	 *  projected shadow stays (Particles/SSXParticleWorld.cpp). */
	SSXTRICKYREBUILD_API bool RealRiderShadows(const UWorld& World);
	/** The Classic material of a slot, also while the Remastered look shows a lit twin there (game code reading
	 *  parameters, e.g. the flipbooks' BaseTex). */
	SSXTRICKYREBUILD_API UMaterialInterface* ClassicMaterial(UMeshComponent& C, int32 Slot);
	/** CreateAndSetMaterialInstanceDynamic on the Classic side: game code that animates a slot (UV scroll, flipbooks,
	 *  movie screens) always drives a Classic instance; in the Remastered look its lit twin follows it every frame. */
	SSXTRICKYREBUILD_API UMaterialInstanceDynamic* ClassicMid(UMeshComponent& C, int32 Slot);
	/** How many of the strongest track lights cast shadows at a quality: Epic all, High 16, Medium / Low none. */
	SSXTRICKYREBUILD_API int32 ShadowedLights(int32 Quality, int32 NumLights);
	/** Remastered surface class of a sim surface id (SimData/surfaces.bin physics): 0 snow, 1 ice (no grip, fast),
	 *  2 powder (deep sink), 3 wet / slush (no grip, heavy drag). */
	SSXTRICKYREBUILD_API int32 SurfaceClass(float Grip, float DragQuadratic, float SinkDepth, float CruiseKmh);
	/** Terrain texture index -> surface class for a track: each texture takes the most common class of the sim patches
	 *  that use it (SimData/<stem>.patches.bin source index = <stem>.terrainlod.bin pbd index). */
	SSXTRICKYREBUILD_API TMap<int32, int32> TerrainSurfaceClasses(const FString& Stem);
	/** The Remastered draw distance as a multiple of the original's: Low 1.5, Medium 2, High 3, Epic 4. */
	SSXTRICKYREBUILD_API float DrawDistanceScale(int32 Quality);
	constexpr double SkyDistance = 60000.0;
	constexpr int32 TrailTexels = 2048;
	constexpr double TrailSize = 8000.0;    // cm covered by the trail map
	constexpr double TrailRadius = 22.0;    // cm, board groove half width
	constexpr double TrailStep = 10.0;      // cm between stamps   // cm: the sky dome (<= 17.5 m x 50) and beyond become the sky light
}

UCLASS()
class SSXTRICKYREBUILD_API USSXRemasterWorld : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual bool IsTickableInEditor() const override { return false; }

	void Apply();
	void Revert();
	UMaterialInterface* ClassicMaterial(UMeshComponent& C, int32 Slot);
	UMaterialInstanceDynamic* ClassicMid(UMeshComponent& C, int32 Slot);
	bool IsApplied() const { return bApplied; }
	int32 NumSwapped() const { return Swaps.Num(); }

private:
	void SwapLevel(ULevel& Level);
	void SwapComponent(UMeshComponent& C);
	UMaterialInterface* RemasteredFor(UMaterialInterface* Classic);
	void OnLevelAdded(ULevel* Level, UWorld* World);
	/** Lit display twins for the riders' Classic instances (new ones appear with outfit changes: every tick). */
	void SwapRiders();

	struct FSwap
	{
		TWeakObjectPtr<UMeshComponent> Component;
		int32 Slot = 0;
		TWeakObjectPtr<UMaterialInterface> Original;
		TWeakObjectPtr<UMaterialInstanceDynamic> Follower;   // per-slot twin of a Classic instance game code animates
		TWeakObjectPtr<UTexture> FollowedBase;
	};
	TArray<FSwap> Swaps;
	TMap<TPair<TWeakObjectPtr<UMeshComponent>, int32>, int32> SwapIndex;
	FSwap* FindSwap(UMeshComponent& C, int32 Slot);
	UMaterialInterface* RemasteredParentOf(UMaterialInterface* Classic);
	void ApplyDerivedMaps(UMaterialInstanceDynamic& Mid, UTexture* Base);
	UMaterialInstanceDynamic* MakeFollower(FSwap& S, UMaterialInstanceDynamic& Classic, UMeshComponent& C);
	void SyncFollowers();
	void UpdateFrontEndSpots();
	/** Depth of field on the rider nearest the view centre in the front end and replays (never while racing). */
	void UpdateDepthOfField();
	/** Snow trails: grounded riders' boards stamp the trail map around the camera, which follows the camera. */
	void UpdateTrails();
	void SetTrailParams();
	UPROPERTY(Transient) TObjectPtr<class UTextureRenderTarget2D> TrailMap;
	UPROPERTY(Transient) TObjectPtr<class UTextureRenderTarget2D> TrailScratch;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TrailStamp;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TrailShift;
	FVector2D TrailOrigin = FVector2D::ZeroVector;
	TMap<TWeakObjectPtr<AActor>, FVector> TrailLast;   // each rider's last stamped board position
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>> FrontEndSpots;   // carousel rider -> its stage spotlight
	TArray<TWeakObjectPtr<APostProcessVolume>> DisabledVolumes;
	TArray<FSwap> RiderSwaps;
	TArray<TWeakObjectPtr<UMeshComponent>> ShadowCasters;   // rider parts made to cast shadows (Classic: none do)
	TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, TWeakObjectPtr<UMaterialInstanceDynamic>> RiderTwins;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> Remastered;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Spawned;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ClassicMids;   // Classic instances made while a twin is shown

	TWeakObjectPtr<APostProcessVolume> Volume;
	TArray<TWeakObjectPtr<class ULocalLightComponent>> LocalLights;   // strongest first
	TWeakObjectPtr<class UExponentialHeightFogComponent> Fog;
	double FogStartBase = 0.0;   // fog start at draw distance scale 1, cm
	struct FShadowFlags { TWeakObjectPtr<UPrimitiveComponent> Component; bool bCast = false; bool bHidden = false; };
	TArray<FShadowFlags> TerrainShadows;
	TMap<int32, int32> SurfaceClasses;   // terrain texture index -> SSXRemaster::SurfaceClass   // the hidden full-detail terrain meshes, made to cast the sun's shadow
	bool bAllLightShadows = false;
	int32 AppliedQuality = -1;
	void ApplyQuality();
	bool bApplied = false;
	FDelegateHandle LevelAddedHandle;
};

/** Editor scripts (offscreen captures): apply or revert the Remastered look in an editor world, which the subsystem
 *  never does by itself. */
UCLASS()
class SSXTRICKYREBUILD_API USSXRemasterLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns the number of material slots switched (0 when reverting). */
	UFUNCTION(BlueprintCallable, Category = "SSX", meta = (WorldContext = "WorldContext"))
	static int32 SetRemasteredInWorld(UObject* WorldContext, bool bRemastered);
	/** Editor only: finish shader, distance-field and texture compilation and stream every texture in. */
	UFUNCTION(BlueprintCallable, Category = "SSX")
	static void FinishLoadingForCapture();
};
