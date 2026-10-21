// TODO: in the notes).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/SSXWorldLighting.h"
#include "Characters/SSXRiderDepth.h"
#include "UI/SSXFeRiders.h"
#include "SSXWorldPresentation.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UMeshComponent;

UCLASS()
class SSXTRICKYREBUILD_API USSXWorldPresentation : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* CollectionPath = TEXT("/Game/World/MPC_SSXWorld.MPC_SSXWorld");
	static constexpr const TCHAR* RiderMaterialPath = TEXT("/Game/World/M_SSXRiderLit.M_SSXRiderLit");
	static constexpr const TCHAR* RiderMaskedMaterialPath = TEXT("/Game/World/M_SSXRiderLit_Masked.M_SSXRiderLit_Masked");
	static constexpr const TCHAR* ClearMaterialPath = TEXT("/Game/World/M_SSXClearColour.M_SSXClearColour");
	// Radius of the clear-colour shell, cm: beyond the scaled sky dome (<= 17.5 m x 50).
	static constexpr double ClearShellRadius = 200000.0;
	static constexpr int32 FrontEndId = 11;
	static const FName SkyTag;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

	const FSSXTrackPresentation* GetTrack() const { return bHaveTrack ? &Track : nullptr; }
	const FSSXLightWorld& GetLightWorld() const { return LightWorld; }
	const FSSXRiderLightAdjust& GetRiderLightAdjust() const { return Adjust; }
	/** Per-rider flash colour (cBoarder +0x4B84), added to the probe's ambient. Set each frame by the particle effects (SSXParticleWorld). */
	void SetRiderFlash(const AActor* Rider, const FVector& Flash) { RiderFlash.Add(Rider, Flash); }

	/** MPC_SSXWorld values for a track record: ClearColour (bytes / 255, the PS2-space value the post
	 *  process shows unchanged), MipLodOn / MipLodL / MipLodK. */
	static FLinearColor ClearColourParam(const FSSXTrackPresentation& T);
	/** The clear colour this world is drawn over: table 0x48 on a race track, black in the front end (no FE code sets renderer slot 13; the last setters before it are the renderer ctor / race teardown / spinner / video, all 0). */
	static FLinearColor ClearColourFor(int32 TrackId, const FSSXTrackPresentation& T);

	struct FRiderLightInputs
	{
		static constexpr int32 Slots = 3;
		FVector Ambient = FVector::ZeroVector;
		FVector Colour[Slots] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
		FVector Dir[Slots] = { FVector::UpVector, FVector::UpVector, FVector::UpVector };
	};
	static void ApplyRiderLight(UMaterialInstanceDynamic& Mid, const FRiderLightInputs& In);
	/** Front-end carousel rider lighting: the blended light block with its directions in CAMERA
	 *  space (x screen right, y up, z forward into the screen; cFE3DCharacter::vfunc_11 multiplies them by the transpose
	 *  of the FE camera's view matrix), turned into Unreal world space by the camera rotation CamRot. Colour floats 1..3
	 *  are r, g, b (FUN_001CE140 packs them as the colour columns, the lead float is the vertex alpha). */
	static FRiderLightInputs FeRiderLightInputs(const SSXFeRiders::FLightSet& L, const FRotator& CamRot);
	/** The per-vertex colour of M_SSXRiderLit (RIDER_LIGHT, the rider VU program image 0x318080): ambient + sum
	 *  max(0, N . D_i) C_i, clamped at 255/128 per channel (PS2 units, 1.0 = 0x80). N unit, Unreal space. */
	static FVector RiderVertexColour(const FRiderLightInputs& In, const FVector& N);
	static UMaterialInstanceDynamic* MakeRiderMid(UMaterialInterface& Src, UMaterialInterface* Parent, UObject* Outer);
	static bool IsChromeParent(const UMaterialInterface* Parent);

	/** Soft-prop depth write (Particles/SSXGsBlend.h SoftDepthClip): M's root is the soft
	 *  blend parent M_SSXProp_Translucent(_TwoSided), whose texels write depth in the original. */
	static bool IsSoftBlendMaterial(const UMaterialInterface* M);
	/** Every static mesh component of the world with a soft blend section renders custom depth (the emulated depth
	 *  write the later translucent pixels test). Called when a track or the front end begins; returns the count. */
	static int32 MarkSoftPropDepth(UWorld& InWorld, TArray<TWeakObjectPtr<UStaticMeshComponent>>* OutComponents = nullptr);

	/** Rider gloss pass (Characters/SSXRiderGloss.h, FUN_0025EDF0): load the specmap record's `<trk>s` / `<trk>e` images
	 *  (a missing one falls back to PARTICLE 'spec' / 'envr'). Called with the track's record when a track or the front end
	 *  begins; rider-lit instances made afterwards sample them. */
	void SetGlossSpecmap(int32 SpecmapIndex);
	class UTexture2D* GetGlossTexture(bool bEnv) const { return bEnv ? GlossEnvTex.Get() : GlossSpecTex.Get(); }
	const FString& GetGlossTexturePath(bool bEnv) const { return GlossTexPath[bEnv ? 1 : 0]; }
	/** GlossTex / GlossFix of a rider-lit instance from its source section material (SSXRiderGloss::GlossOfMaterial):
	 *  FIX = |gloss| / 128 and `<trk>s` (gloss > 0) or `<trk>e` (gloss < 0); gloss 0 leaves GlossFix 0 (no pass). */
	void ApplyGloss(UMaterialInstanceDynamic& Mid, const UMaterialInterface& Src) const;
	/** Rider depth pull (Characters/SSXRiderDepth.h, vfunc_23 0x1DCC10 from the rider draw 0x1364D0): RiderDepthPull
	 *  (cm, 0 = off), RiderRoot (the rider root +0x140, Unreal space), RiderDepthNear (the projection near n) and
	 *  RiderGuard (the rider VU clip test's x / y guard band in image NDC, SSXRiderDepth::GuardNdc). */
	static void ApplyRiderDepth(UMaterialInstanceDynamic& Mid, double Pull, const FVector& Root, double Near,
		const FVector2D& Guard = FVector2D(SSXRiderDepth::GuardSpanX / SSXRiderDepth::FrameW, SSXRiderDepth::GuardSpanY / SSXRiderDepth::FrameH));

private:
	void UpdateSky();
	void SpawnClearShell(UWorld& InWorld);
	void UpdateRiders();
	void LightActor(AActor& Actor);
	UMaterialInstanceDynamic* WrapRiderMaterial(UMeshComponent& Mesh, int32 Slot);

	FSSXTrackPresentation Track;
	FSSXRiderLightAdjust Adjust;
	FSSXLightWorld LightWorld;
	bool bHaveTrack = false;
	bool bActive = false;
	float ShotClock = 0.0f;
	bool bShotTaken = false;

	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> Collection;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RiderMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RiderMaskedMaterial;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<AActor>> SkyActors;
	TArray<TWeakObjectPtr<class USSXTerrainLodComponent>> TerrainLods;
	/** The soft props (MarkSoftPropDepth): only those near the camera write custom depth (ssx.Render.SoftDepthRange). */
	TArray<TWeakObjectPtr<UStaticMeshComponent>> SoftDepthProps;
	int32 SoftDepthFrame = 0;
	void UpdateSoftDepthRange();
	UPROPERTY(Transient) TWeakObjectPtr<AActor> ClearShell;
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> GlossSpecTex;
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> GlossEnvTex;
	int32 GlossSpecmap = INDEX_NONE;
	FString GlossTexPath[2];
	TMap<TWeakObjectPtr<const AActor>, FVector> RiderFlash;
	double RiderDepthNear = 10.0;
	FVector2D RiderGuard = FVector2D(SSXRiderDepth::GuardSpanX / SSXRiderDepth::FrameW, SSXRiderDepth::GuardSpanY / SSXRiderDepth::FrameH);
};
