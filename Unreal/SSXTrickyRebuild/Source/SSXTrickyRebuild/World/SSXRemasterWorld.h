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
	 *  its Classic look (riders, sky, particles, translucent props). */
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
		float LightCandela = 15.f;      // track point / spot lights: candela per unit of the original colour
		int32 MaxLights = 48;
	};
	SSXTRICKYREBUILD_API const FTrackTuning& TuningFor(const FString& Stem);
	/** Attenuation radius (cm) of a track light: where the original falloff (FSSXLightWorld::FalloffFactor) is 5 %. */
	SSXTRICKYREBUILD_API float LightRadius(int32 FalloffKind);
	/** Indices of the track's point and spot lights (not modulators) by falling strength, at most Max. */
	SSXTRICKYREBUILD_API TArray<int32> StrongestLocalLights(TConstArrayView<FSSXLight> Lights, int32 Max);
	constexpr double SkyDistance = 60000.0;   // cm: the sky dome (<= 17.5 m x 50) and beyond become the sky light
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
	};
	TArray<FSwap> Swaps;
	TArray<TWeakObjectPtr<APostProcessVolume>> DisabledVolumes;
	TArray<FSwap> RiderSwaps;
	TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, TWeakObjectPtr<UMaterialInstanceDynamic>> RiderTwins;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>> Remastered;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Spawned;

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
