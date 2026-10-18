#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Particles/SSXParticles.h"
#include "Particles/SSXRiderFx.h"
#include "Particles/SSXSnowFx.h"
#include "Particles/SSXFogVolumes.h"
#include "Particles/SSXAngerMarker.h"
#include "SSXParticleWorld.generated.h"

class AActor;
class ASnowboarderPawn;
class FRiderSim;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class UTextureRenderTarget2D;
class USceneCaptureComponent2D;
class UStaticMeshComponent;
class UPoseableMeshComponent;
class USSXSpriteRenderComponent;
namespace SSXEffects { class FEffectData; }

UCLASS()
class SSXTRICKYREBUILD_API USSXParticleWorld : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* AddMaterialPath = TEXT("/Game/Particles/M_SSXParticleAdd.M_SSXParticleAdd");
	/** Remastered look (World/SSXRenderLook.h): linear, soft-faded counterparts of the additive and alpha sprites. */
	static constexpr const TCHAR* RemasteredAddMaterialPath = TEXT("/Game/Remastered/M_RmParticleAdd.M_RmParticleAdd");
	static constexpr const TCHAR* RemasteredAlphaMaterialPath = TEXT("/Game/Remastered/M_RmParticleAlpha.M_RmParticleAlpha");
	static constexpr const TCHAR* AlphaMaterialPath = TEXT("/Game/Particles/M_SSXParticleAlpha.M_SSXParticleAlpha");
	static constexpr const TCHAR* DarkenMaterialPath = TEXT("/Game/Particles/M_SSXParticleDarken.M_SSXParticleDarken");
	static constexpr const TCHAR* ShadowMaterialPath = TEXT("/Game/Particles/M_SSXShadowProject.M_SSXShadowProject");
	static constexpr const TCHAR* BrightenMaterialPath = TEXT("/Game/Particles/M_SSXParticleBrighten.M_SSXParticleBrighten");
	/** The ground trail's two passes: drawn on layer 2, before every rider (layer 3), so a visible rider or board pixel always covers them; the materials skip the pixels where the rider is the scene. */
	static constexpr const TCHAR* TrailDarkenMaterialPath = TEXT("/Game/Particles/M_SSXTrailDarken.M_SSXTrailDarken");
	static constexpr const TCHAR* TrailBrightenMaterialPath = TEXT("/Game/Particles/M_SSXTrailBrighten.M_SSXTrailBrighten");
	/** Lens FX: textured `lens` elements and the untextured glare fan, additive, no depth test, with z-buffer block occlusion in the material. */
	static constexpr const TCHAR* LensSpriteMaterialPath = TEXT("/Game/Particles/M_SSXLensSprite.M_SSXLensSprite");
	static constexpr const TCHAR* LensFanMaterialPath = TEXT("/Game/Particles/M_SSXLensFan.M_SSXLensFan");
	static constexpr int32 TrailLayerFlag = 0x100;
	/** Added to a blend mode: the texture with REPEAT addressing (renderer slot 63 = 0; the splash sheets and the carve curtain). */
	static constexpr int32 RepeatFlag = 0x200;
	/** Translucency sort priority of the sprite renderer: above every prop (0), so sprites, then the fog (layer 4) and the lens FX draw after the soft-prop blend sections (the sprite primitive's sort point is the world origin). A sprite behind a soft texel is dropped by the SoftDepth node (SSXGsBlend.h). TODO: layer-3 particles interleave with the soft props by the 10-bit depth key in the original; here they all follow them. */
	static constexpr int32 SpriteSortPriority = 1;

	static USSXParticleWorld* Get(const UObject* WorldContext);
	// Sampler setup of a PARTICLE.SSH texture loaded at run time: linear (stored bytes), CLAMP.
	static void ConfigureSpriteTexture(UTexture2D* Tex);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

	void AddTrackEmitter(const SSXEffects::FEffectData& Data, int32 Inst, int32 Kind, int32 Record, const FRiderSim* Rider);
	void RemoveTrackEmitter(int32 Inst, int32 Kind, int32 Record);
	void ClearTrackEmitters() { Emitters.Reset(); }
	int32 NumTrackEmitters() const { return Emitters.Num(); }

	void OnLaunchBoost(const FRiderSim* Sim);
	void StartSnowfall(double X, double R1, double R2, const TArray<double>* Seeds = nullptr);
	void StopSnowfall() { bSnow = false; }

	struct FLensStats { int32 Halos = 0, Elements = 0, FanTris = 0; bool bSun = false; };
	int32 GetSprayBurstCount(const ASnowboarderPawn* Pawn) const;
	bool IsShadowActive(const ASnowboarderPawn* Pawn) const;
	/** 0x1128B8 (replay restore 0x190878 after 0x18F6C0; race start): every racer's 0x136EA0(handler) = the FX block cleared (0x135200: clouds 0x12EC98, curtain 0x12FED8, spray / sparks 0x131160, trail 0x132518, puffs 0x12DFD0, strip 0x12F4B0) and both ribbon rings emptied (handler +0x1E0 / +0x1E4, +0x3C0 / +0x3C4). The launch-boost trails (cParticleMan) run on. */
	void ResetRiderEffects(const ASnowboarderPawn* Only = nullptr);   // 0x136EA0 / 0x14D7D0
	int32 RiderEffectResets = 0;

private:
	struct FTrackEmitter
	{
		int32 Inst = INDEX_NONE, Kind = 0, Record = INDEX_NONE;
		SSXParticles::FParticleBurst Burst;
	};
	struct FRiderFx
	{
		TWeakObjectPtr<ASnowboarderPawn> Pawn;
		SSXParticles::FSprayFx Spray;
		SSXParticles::FCarveClouds Clouds;
		SSXParticles::FPuffFx Puffs;
		SSXParticles::FIceGlints Glints;
		SSXParticles::FCarveCurtain Curtain;
		SSXParticles::FSpinRibbons Spin;
		SSXParticles::FSpinSample LiveSpin;
		int32 LastImpactSerial = 0;
		SSXParticles::FBoostRibbons Ribbons;
		SSXParticles::FRibbonSample LiveRibbon;
		bool bHaveLiveRibbon = false;
		SSXParticles::FGroundTrail Trail;
		SSXParticles::FPowerFxSlot Power[2];
		SSXParticles::FAngerMarker Anger;      // 0x1343D0 / 0x134830
		SSXParticles::FPrevRow AngerUp;        // 0x1343D0
		bool bLaunch = false;
		bool bPowerSlots = false;
		int32 LastMotion = 0;
		SSXParticles::FRiderFxInput LastInput;
		bool bHaveInput = false;
		double RenderAlpha = 1.0;
		int32 SprayBursts = 0;
		TWeakObjectPtr<USceneCaptureComponent2D> Capture;
		TWeakObjectPtr<UStaticMeshComponent> Projector;
		TWeakObjectPtr<UMaterialInstanceDynamic> ShadowMid;
		TWeakObjectPtr<UTextureRenderTarget2D> ShadowTarget;
		int32 ShadowSize = 0;
		FVector ShadowAxisX = FVector(1, 0, 0);   // light-view x axis (game space)
	};

	void StepFrame();
	void StepRider(FRiderFx& R, int32 FrameNo);
	bool BuildInput(const ASnowboarderPawn& Pawn, SSXParticles::FRiderFxInput& Out) const;
	static bool UpdatePoseBones(const ASnowboarderPawn& Pawn, FVector Out[19]);
	void Render();
	void UpdateShadow(FRiderFx& R, int32 Racers);
	void StepRibbons(FRiderFx& R, const SSXParticles::FRiderFxInput& In, int32 FrameNo);
	void StepAngerMarker(FRiderFx& R, const ASnowboarderPawn& Pawn);
	using FBatchFor = TFunctionRef<struct FSSXSpriteBatch*(int32 Image, int32 BlendMode)>;
	void DrawTrail(FBatchFor For, const SSXParticles::FGroundTrail& T);
	void DrawRibbons(FBatchFor For, const FRiderFx& R);
	void DrawLensFX(TArray<struct FSSXSpriteBatch>& Batches);
	float FrameFar(const struct FSSXTrackPresentation& Track, int32 Viewports) const;
	void FrameClipView(const class APlayerController& PC, SSXFog::FClipView& Cv, int32& Radius) const;
	FRiderFx* FindRider(const FRiderSim* Sim);
	void SyncRiders();

	UTexture2D* Texture(int32 Handle, bool bRepeat = false);
	UMaterialInterface* SpriteMaterial(int32 Handle, int32 BlendMode);

	TArray<FTrackEmitter> Emitters;
	TArray<FRiderFx> Riders;

public:
	/** Replay keyframe of the track emitters: the emitter nodes (cEmitterNode 0x147F30, cCollideEmitterNode 0x1489F0, cParticleNode 0x1481D0) save their record and the emitter's two clocks (0x1D8D48: emitter +0 / +0x18) and are rebuilt by the restore; live cParticleMan particles (0x1D8E30) and rider effects are not in the keyframe (the restore clears the rider effects, 0x1128B8). */
	struct FReplayKeyframe
	{
		bool bValid = false;
		TArray<FTrackEmitter> Emitters;
	};
	void SaveReplay(FReplayKeyframe& Out) const { Out.bValid = true; Out.Emitters = Emitters; }
	void LoadReplay(const FReplayKeyframe& In) { if (In.bValid) { Emitters = In.Emitters; } }
private:
	SSXParticles::FSnowfall Snow;
	bool bSnow = false;
	bool bActive = false;
	double Accum = 0.0;
	int32 FrameCounter = 0;
	int32 LastSprites = 0;
	TArray<SSXParticles::FSprite> Scratch;
	SSXFog::FFogData Fog;
	// 0x1C7810
	bool bFogVisibility = false;
	TArray<SSXFog::FPuff> FogScratch;
	TArray<int32> HaloScratch;
	int32 LastFog = 0;
	FLensStats LensStats;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LensFanMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LensSpriteMid;

	UPROPERTY(Transient) TObjectPtr<AActor> RenderActor;
	UPROPERTY(Transient) TObjectPtr<USSXSpriteRenderComponent> Renderer;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> BaseMaterials;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> ShadowMaterial;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> SpriteMids;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Keep;
	TSet<int32> MissingTextures;
};
