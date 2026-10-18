#include "World/SSXRemasterWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Level.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "AssetCompilingManager.h"
#include "ContentStreaming.h"
#include "RenderingThread.h"
#include "Sim/SimCoords.h"
#include "World/SSXRenderLook.h"
#include "World/SSXWorldLighting.h"
#include "World/SSXWorldPresentation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SSXRemasterWorld)

namespace
{
	const UMaterial* RootOf(const UMaterialInterface* M)
	{
		while (const UMaterialInstance* MI = Cast<UMaterialInstance>(M)) { M = MI->Parent; }
		return Cast<UMaterial>(M);
	}

	UTexture* LoadIfExists(const FString& ObjectPath)
	{
		if (ObjectPath.IsEmpty() || !FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath))) { return nullptr; }
		return LoadObject<UTexture>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	bool IsClassicPresentation(const APostProcessVolume& V)
	{
		for (const FWeightedBlendable& B : V.Settings.WeightedBlendables.Array)
		{
			if (B.Object && B.Object->GetName() == TEXT("PP_SSXAuthoredColour")) { return true; }
		}
		return false;
	}
}

const TCHAR* SSXRemaster::RemasteredParentPath(const FString& ClassicRootName)
{
	static const TMap<FString, const TCHAR*> Map = {
		{ TEXT("M_SSXTerrain"), TEXT("/Game/Remastered/M_RmTerrain.M_RmTerrain") },
		{ TEXT("M_SSXTerrain_Masked"), TEXT("/Game/Remastered/M_RmTerrain_Masked.M_RmTerrain_Masked") },
		{ TEXT("M_SSXTerrainOpaque"), TEXT("/Game/Remastered/M_RmTerrainOpaque.M_RmTerrainOpaque") },
		{ TEXT("M_SSXProp"), TEXT("/Game/Remastered/M_RmProp.M_RmProp") },
		{ TEXT("M_SSXProp_TwoSided"), TEXT("/Game/Remastered/M_RmProp.M_RmProp") },
		{ TEXT("M_SSXProp_Masked"), TEXT("/Game/Remastered/M_RmProp_Masked.M_RmProp_Masked") },
		{ TEXT("M_SSXProp_Masked_TwoSided"), TEXT("/Game/Remastered/M_RmProp_Masked.M_RmProp_Masked") },
		{ TEXT("M_SSXPropOpaque"), TEXT("/Game/Remastered/M_RmPropOpaque.M_RmPropOpaque") },
		{ TEXT("M_SSXPropOpaque_TwoSided"), TEXT("/Game/Remastered/M_RmPropOpaque.M_RmPropOpaque") },
		{ TEXT("M_SSXProp_TranslucentCore"), TEXT("/Game/Remastered/M_RmProp_Core.M_RmProp_Core") },
		{ TEXT("M_SSXProp_TranslucentCore_TwoSided"), TEXT("/Game/Remastered/M_RmProp_Core.M_RmProp_Core") },
	};
	const TCHAR* const* P = Map.Find(ClassicRootName);
	return P ? *P : nullptr;
}

FString SSXRemaster::DerivedMapPath(const FString& TexturePackage, const TCHAR* Suffix)
{
	TArray<FString> Parts;
	TexturePackage.ParseIntoArray(Parts, TEXT("/"));
	const int32 Tex = Parts.Find(TEXT("Textures"));
	if (Parts.Num() < 4 || Parts[0] != TEXT("Game") || Tex < 2 || Tex != Parts.Num() - 2) { return FString(); }
	const FString Name = FString::Printf(TEXT("T_%s_%s"), *Parts.Last(), Suffix);
	return FString::Printf(TEXT("/Game/RemasterData/%s/%s/textures/%s.%s"), *Parts[1].ToLower(), *Parts[Tex - 1], *Name, *Name);
}

int32 SSXRemaster::SunLight(TConstArrayView<FSSXLight> Lights)
{
	int32 Best = INDEX_NONE;
	double BestF = 0.0;
	for (int32 I = 0; I < Lights.Num(); ++I)
	{
		const FSSXLight& L = Lights[I];
		if (L.Type != FSSXLight::Directional || (L.Flags & FSSXLight::FlagModulator)) { continue; }
		const double F = FMath::Abs(L.Colour.X + L.Colour.Y + L.Colour.Z);
		if (F > BestF) { BestF = F; Best = I; }
	}
	return Best;
}

bool USSXRemasterWorld::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE || W->WorldType == EWorldType::Editor);
}

void USSXRemasterWorld::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &USSXRemasterWorld::OnLevelAdded);
}

void USSXRemasterWorld::Deinitialize()
{
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	Super::Deinitialize();
}

TStatId USSXRemasterWorld::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USSXRemasterWorld, STATGROUP_Tickables);
}

void USSXRemasterWorld::Tick(float DeltaTime)
{
	const UWorld* W = GetWorld();
	if (!W || !W->IsGameWorld() || !W->HasBegunPlay()) { return; }
	const bool bWant = SSXRenderLook::Get() == ESSXRenderLook::Remastered;
	if (bWant != bApplied)
	{
		bWant ? Apply() : Revert();
	}
}

void USSXRemasterWorld::OnLevelAdded(ULevel* Level, UWorld* World)
{
	if (bApplied && Level && World == GetWorld()) { SwapLevel(*Level); }
}

UMaterialInterface* USSXRemasterWorld::RemasteredFor(UMaterialInterface* Classic)
{
	if (TObjectPtr<UMaterialInterface>* Found = Remastered.Find(Classic)) { return *Found; }
	UMaterialInterface* Result = nullptr;
	const UMaterial* Root = RootOf(Classic);
	const TCHAR* ParentPath = Root ? SSXRemaster::RemasteredParentPath(Root->GetName()) : nullptr;
	UMaterialInterface* Parent = ParentPath ? LoadObject<UMaterialInterface>(nullptr, ParentPath) : nullptr;
	if (Parent)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
		TArray<const UMaterialInstance*> Chain;
		for (const UMaterialInstance* MI = Cast<UMaterialInstance>(Classic); MI; MI = Cast<UMaterialInstance>(MI->Parent)) { Chain.Insert(MI, 0); }
		for (const UMaterialInstance* MI : Chain)
		{
			for (const FScalarParameterValue& V : MI->ScalarParameterValues) { Mid->SetScalarParameterValue(V.ParameterInfo.Name, V.ParameterValue); }
			for (const FVectorParameterValue& V : MI->VectorParameterValues) { Mid->SetVectorParameterValue(V.ParameterInfo.Name, V.ParameterValue); }
			for (const FTextureParameterValue& V : MI->TextureParameterValues) { Mid->SetTextureParameterValue(V.ParameterInfo.Name, V.ParameterValue); }
		}
		UTexture* Base = nullptr;
		if (Mid->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseTex")), Base) && Base)
		{
			const FString Pkg = Base->GetOutermost()->GetName();
			if (UTexture* C = LoadIfExists(SSXRemaster::DerivedMapPath(Pkg, TEXT("C")))) { Mid->SetTextureParameterValue(TEXT("BaseTex"), C); }
			if (UTexture* N = LoadIfExists(SSXRemaster::DerivedMapPath(Pkg, TEXT("N")))) { Mid->SetTextureParameterValue(TEXT("RmNormal"), N); }
			if (UTexture* M = LoadIfExists(SSXRemaster::DerivedMapPath(Pkg, TEXT("M")))) { Mid->SetTextureParameterValue(TEXT("RmMasks"), M); }
		}
		Result = Mid;
	}
	Remastered.Add(Classic, Result);
	return Result;
}

void USSXRemasterWorld::SwapComponent(UMeshComponent& C)
{
	for (int32 S = 0; S < C.GetNumMaterials(); ++S)
	{
		UMaterialInterface* M = C.GetMaterial(S);
		UMaterialInterface* R = M ? RemasteredFor(M) : nullptr;
		if (!R) { continue; }
		Swaps.Add({ &C, S, M });
		C.SetMaterial(S, R);
	}
}

void USSXRemasterWorld::SwapLevel(ULevel& Level)
{
	for (AActor* A : Level.Actors)
	{
		if (!A) { continue; }
		TArray<UMeshComponent*> Meshes;
		A->GetComponents(Meshes);
		for (UMeshComponent* C : Meshes) { SwapComponent(*C); }
	}
}

void USSXRemasterWorld::Apply()
{
	UWorld* W = GetWorld();
	if (!W || bApplied) { return; }
	bApplied = true;
	for (ULevel* L : W->GetLevels())
	{
		if (L) { SwapLevel(*L); }
	}

	for (TActorIterator<APostProcessVolume> It(W); It; ++It)
	{
		if (It->bEnabled && IsClassicPresentation(**It))
		{
			It->bEnabled = false;
			DisabledVolumes.Add(*It);
		}
	}

	FActorSpawnParameters P;
	P.ObjectFlags = RF_Transient;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (APostProcessVolume* V = W->SpawnActor<APostProcessVolume>(P))
	{
		V->bUnbound = true;
		V->Priority = 10.f;
		FPostProcessSettings& S = V->Settings;
		S.bOverride_AutoExposureMethod = true;
		S.AutoExposureMethod = AEM_Manual;
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = 0.f;
		S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		S.AutoExposureApplyPhysicalCameraExposure = false;
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = 0.f;
		S.bOverride_FilmGrainIntensity = true;
		S.FilmGrainIntensity = 0.f;
		S.bOverride_MotionBlurAmount = true;
		S.MotionBlurAmount = 0.f;
		S.bOverride_SceneFringeIntensity = true;
		S.SceneFringeIntensity = 0.f;
		S.bOverride_LensFlareIntensity = true;
		S.LensFlareIntensity = 0.f;
		S.bOverride_DynamicGlobalIlluminationMethod = true;
		S.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
		S.bOverride_ReflectionMethod = true;
		S.ReflectionMethod = EReflectionMethod::Lumen;
		if (UObject* PP = LoadObject<UObject>(nullptr, SSXRemaster::ScenePostPath))
		{
			S.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, PP));
		}
		Spawned.Add(V);
	}

	const USSXWorldPresentation* Pres = W->GetSubsystem<USSXWorldPresentation>();
	FSSXLightWorld EditorLights;   // editor worlds (captures) have no presentation subsystem
	if (!Pres)
	{
		const FString Stem = UWorld::RemovePIEPrefix(W->GetMapName()).ToLower();
		EditorLights.Load(FSSXLightWorld::DefaultPath(Stem == TEXT("frontend") ? FString(TEXT("ssxfe")) : Stem));
	}
	const TArray<FSSXLight>& Lights = Pres ? Pres->GetLightWorld().GetLights() : EditorLights.GetLights();
	const int32 Sun = SSXRemaster::SunLight(Lights);
	FVector SunDir(0.3, 0.2, -0.93);
	FLinearColor SunColour(1.f, 0.97f, 0.92f);
	if (Sun != INDEX_NONE)
	{
		const FSSXLight& L = Lights[Sun];
		SunDir = SSXSim::GameToUnreal(L.Direction).GetSafeNormal();
		const double Max = FMath::Max3(L.Colour.X, L.Colour.Y, L.Colour.Z);
		if (Max > 0.0)
		{
			SunColour = FMath::Lerp(FLinearColor::White, FLinearColor(L.Colour.X / Max, L.Colour.Y / Max, L.Colour.Z / Max), SSXRemaster::SunSaturation);
		}
	}
	if (ADirectionalLight* D = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, SunDir.Rotation(), P))
	{
		UDirectionalLightComponent* C = CastChecked<UDirectionalLightComponent>(D->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable);
		C->SetIntensity(float(SSXRemaster::SunLux));
		C->SetLightColor(SunColour);
		C->SetCastShadows(true);
		Spawned.Add(D);
	}
	if (ASkyLight* K = W->SpawnActor<ASkyLight>(P))
	{
		USkyLightComponent* C = K->GetLightComponent();
		C->SetMobility(EComponentMobility::Movable);
		C->SourceType = SLS_CapturedScene;
		C->SkyDistanceThreshold = float(SSXRemaster::SkyDistance);
		C->SetIntensity(SSXRemaster::SkyLightIntensity);
		C->RecaptureSky();
		Spawned.Add(K);
	}
}

void USSXRemasterWorld::Revert()
{
	if (!bApplied) { return; }
	bApplied = false;
	for (int32 I = Swaps.Num() - 1; I >= 0; --I)
	{
		const FSwap& S = Swaps[I];
		if (UMeshComponent* C = S.Component.Get()) { C->SetMaterial(S.Slot, S.Original.Get()); }
	}
	Swaps.Reset();
	for (const TWeakObjectPtr<APostProcessVolume>& V : DisabledVolumes)
	{
		if (V.IsValid()) { V->bEnabled = true; }
	}
	DisabledVolumes.Reset();
	for (AActor* A : Spawned)
	{
		if (IsValid(A)) { A->Destroy(); }
	}
	Spawned.Reset();
}

int32 USSXRemasterLibrary::SetRemasteredInWorld(UObject* WorldContext, bool bRemastered)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	USSXRemasterWorld* R = W ? W->GetSubsystem<USSXRemasterWorld>() : nullptr;
	if (!R) { return 0; }
	bRemastered ? R->Apply() : R->Revert();
	return R->NumSwapped();
}

void USSXRemasterLibrary::FinishLoadingForCapture()
{
#if WITH_EDITOR
	FAssetCompilingManager::Get().FinishAllCompilation();
	IStreamingManager::Get().StreamAllResources(30.f);
	FAssetCompilingManager::Get().FinishAllCompilation();
	FlushRenderingCommands();
#endif
}
