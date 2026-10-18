#include "World/SSXRemasterWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
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
#include "SnowboarderPawn.h"
#include "UI/SSXFeRiderActor.h"

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

const SSXRemaster::FTrackTuning& SSXRemaster::TuningFor(const FString& Stem)
{
	auto Make = [](const TCHAR* S, float Sun, float SunSat, float Sky, float Amb, float Emis, float Cd)
	{
		FTrackTuning T;
		T.Stem = S; T.SunLux = Sun; T.SunSaturation = SunSat; T.SkyLight = Sky; T.BakedAmbient = Amb; T.Emissive = Emis; T.LightCandela = Cd;
		return T;
	};
	// Night (Snowdream, Merqury City): a faint coloured moon, the baked light and the lamps carry the scene; indoor
	// (Tokyo Megaplex): hardly any sun.
	static const FTrackTuning Table[] = {
		Make(TEXT("snow"), 0.25f, 0.9f, 0.2f, 1.0f, 5.f, 60.f),
		Make(TEXT("merquer"), 0.25f, 0.9f, 0.2f, 1.0f, 5.f, 60.f),
		Make(TEXT("megaple"), 0.2f, 0.6f, 0.3f, 0.95f, 4.f, 40.f),
	};
	static const FTrackTuning Default;
	for (const FTrackTuning& T : Table)
	{
		if (Stem == T.Stem) { return T; }
	}
	return Default;
}

const TCHAR* SSXRemaster::RemasteredRiderPath(const FString& ClassicParentName)
{
	if (ClassicParentName == TEXT("M_SSXRiderLit")) { return TEXT("/Game/Remastered/M_RmRider.M_RmRider"); }
	if (ClassicParentName == TEXT("M_SSXRiderLit_Masked")) { return TEXT("/Game/Remastered/M_RmRider_Masked.M_RmRider_Masked"); }
	if (ClassicParentName == TEXT("M_SSXRiderLitChrome")) { return TEXT("/Game/Remastered/M_RmRiderChrome.M_RmRiderChrome"); }
	if (ClassicParentName == TEXT("M_SSXRiderLitChrome_Masked")) { return TEXT("/Game/Remastered/M_RmRiderChrome_Masked.M_RmRiderChrome_Masked"); }
	return nullptr;
}

float SSXRemaster::LightRadius(int32 FalloffKind)
{
	switch (FalloffKind)
	{
	case 1: return 2000.f;   // 100 / d
	case 2: return 450.f;    // 1e4 / d^2
	case 3: return 270.f;    // 1e6 / d^3
	default: return 3000.f;
	}
}

TArray<int32> SSXRemaster::StrongestLocalLights(TConstArrayView<FSSXLight> Lights, int32 Max)
{
	TArray<int32> Out;
	for (int32 I = 0; I < Lights.Num(); ++I)
	{
		const FSSXLight& L = Lights[I];
		if ((L.Type == FSSXLight::Point || L.Type == FSSXLight::Spot) && !(L.Flags & FSSXLight::FlagModulator)
			&& L.Colour.GetMax() > 0.0)
		{
			Out.Add(I);
		}
	}
	Out.StableSort([&Lights](int32 A, int32 B) { return Lights[A].Colour.GetMax() > Lights[B].Colour.GetMax(); });
	if (Out.Num() > Max) { Out.SetNum(Max); }
	return Out;
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
	if (bApplied)
	{
		SwapRiders();
		if (AppliedQuality != SSXRenderLook::GetQuality()) { ApplyQuality(); }
	}
}

void USSXRemasterWorld::ApplyQuality()
{
	AppliedQuality = SSXRenderLook::GetQuality();
	if (APostProcessVolume* V = Volume.Get())
	{
		const bool bLumen = SSXRenderLook::QualityUsesLumen(AppliedQuality);
		V->Settings.DynamicGlobalIlluminationMethod = bLumen ? EDynamicGlobalIlluminationMethod::Lumen : EDynamicGlobalIlluminationMethod::None;
		V->Settings.ReflectionMethod = bLumen ? EReflectionMethod::Lumen : EReflectionMethod::ScreenSpace;
	}
}

void USSXRemasterWorld::SwapRiders()
{
	UWorld* W = GetWorld();
	USSXWorldPresentation* Pres = W ? W->GetSubsystem<USSXWorldPresentation>() : nullptr;
	if (!Pres) { return; }
	auto Swap = [this, Pres](AActor& Actor)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(&Actor);
		for (UMeshComponent* Mesh : Meshes)
		{
			for (int32 S = 0; Mesh && S < Mesh->GetNumMaterials(); ++S)
			{
				UMaterialInstanceDynamic* Classic = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(S));
				if (!Classic || !Pres->IsRiderMaterial(Classic)) { continue; }
				UMaterialInstanceDynamic* Twin = nullptr;
				if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Found = RiderTwins.Find(Classic)) { Twin = Found->Get(); }
				if (!Twin)
				{
					const TCHAR* Path = SSXRemaster::RemasteredRiderPath(Classic->Parent ? Classic->Parent->GetName() : FString());
					UMaterialInterface* Parent = Path ? LoadObject<UMaterialInterface>(nullptr, Path) : nullptr;
					if (!Parent) { continue; }
					Twin = UMaterialInstanceDynamic::Create(Parent, Mesh);
					Twin->CopyInterpParameters(Classic);
					RiderTwins.Add(Classic, Twin);
				}
				Mesh->SetMaterial(S, Twin);
				Pres->SetDisplayedRiderMaterial(Classic, Twin);
				RiderSwaps.Add({ Mesh, S, Classic });
			}
		}
	};
	for (TActorIterator<ASnowboarderPawn> It(W); It; ++It) { Swap(**It); }
	for (TActorIterator<ASSXFeRiderActor> It(W); It; ++It) { Swap(**It); }
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
		Volume = V;
		ApplyQuality();
	}

	const FString Stem = UWorld::RemovePIEPrefix(W->GetMapName()).ToLower();
	const SSXRemaster::FTrackTuning& T = SSXRemaster::TuningFor(Stem);
	if (UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr, SSXRemaster::CollectionPath))
	{
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("BakedAmbient"), T.BakedAmbient);
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("Exposure"), T.Exposure);
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("Saturation"), T.Saturation);
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("Emissive"), T.Emissive);
	}

	const USSXWorldPresentation* Pres = W->GetSubsystem<USSXWorldPresentation>();
	FSSXLightWorld EditorLights;   // editor worlds (captures) have no presentation subsystem
	FSSXTrackPresentation EditorTrack;
	const FSSXTrackPresentation* Track = Pres ? Pres->GetTrack() : nullptr;
	if (!Pres)
	{
		EditorLights.Load(FSSXLightWorld::DefaultPath(Stem == TEXT("frontend") ? FString(TEXT("ssxfe")) : Stem));
		TArray<FSSXTrackPresentation> Table;
		const int32 Id = FSSXTrackPresentation::IdForStem(Stem);
		if (Id >= 0 && FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table) && Table.IsValidIndex(Id))
		{
			EditorTrack = Table[Id];
			Track = &EditorTrack;
		}
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
			SunColour = FMath::Lerp(FLinearColor::White, FLinearColor(L.Colour.X / Max, L.Colour.Y / Max, L.Colour.Z / Max), T.SunSaturation);
		}
	}
	if (ADirectionalLight* D = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, SunDir.Rotation(), P))
	{
		UDirectionalLightComponent* C = CastChecked<UDirectionalLightComponent>(D->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable);
		C->SetIntensity(T.SunLux);
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
		C->SetIntensity(T.SkyLight);
		C->RecaptureSky();
		Spawned.Add(K);
	}

	for (const int32 I : SSXRemaster::StrongestLocalLights(Lights, T.MaxLights))
	{
		const FSSXLight& L = Lights[I];
		const FVector At = SSXSim::GameToUnreal(L.Position);
		const double Max = L.Colour.GetMax();
		const FLinearColor Colour(L.Colour.X / Max, L.Colour.Y / Max, L.Colour.Z / Max);
		ULocalLightComponent* C = nullptr;
		if (L.Type == FSSXLight::Spot)
		{
			ASpotLight* A = W->SpawnActor<ASpotLight>(At, SSXSim::GameToUnreal(L.Direction).Rotation(), P);
			if (!A) { continue; }
			USpotLightComponent* S = CastChecked<USpotLightComponent>(A->GetLightComponent());
			const float Outer = FMath::Clamp(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(float(L.CosCone), -1.f, 1.f))), 1.f, 80.f);
			S->SetOuterConeAngle(Outer);
			S->SetInnerConeAngle(Outer * 0.7f);
			C = S;
			Spawned.Add(A);
		}
		else
		{
			APointLight* A = W->SpawnActor<APointLight>(At, FRotator::ZeroRotator, P);
			if (!A) { continue; }
			C = CastChecked<ULocalLightComponent>(A->GetLightComponent());
			Spawned.Add(A);
		}
		C->SetMobility(EComponentMobility::Movable);
		C->SetIntensityUnits(ELightUnits::Candelas);
		C->SetIntensity(T.LightCandela * float(FMath::Min(Max, 2.0)));
		C->SetLightColor(Colour);
		C->SetAttenuationRadius(SSXRemaster::LightRadius(L.Falloff));
		C->SetCastShadows(false);
	}

	if (Track)
	{
		if (AExponentialHeightFog* F = W->SpawnActor<AExponentialHeightFog>(P))
		{
			UExponentialHeightFogComponent* C = F->GetComponent();
			C->SetFogInscatteringColor(FLinearColor(Track->ClearColour()));
			C->SetFogDensity(T.FogDensity);
			C->SetFogHeightFalloff(0.0002f);
			C->SetStartDistance(float(Track->DrawDist1P * 0.35));
			C->SetFogMaxOpacity(0.85f);
			C->SetFogCutoffDistance(float(SSXRemaster::SkyDistance * 0.8));   // the sky dome keeps its own colours
			C->SetVolumetricFog(true);
			Spawned.Add(F);
		}
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
	USSXWorldPresentation* Pres = GetWorld() ? GetWorld()->GetSubsystem<USSXWorldPresentation>() : nullptr;
	for (int32 I = RiderSwaps.Num() - 1; I >= 0; --I)
	{
		const FSwap& S = RiderSwaps[I];
		UMeshComponent* C = S.Component.Get();
		UMaterialInstanceDynamic* Classic = Cast<UMaterialInstanceDynamic>(S.Original.Get());
		if (C && Classic && C->GetMaterial(S.Slot) != Classic) { C->SetMaterial(S.Slot, Classic); }
		if (Pres && Classic) { Pres->SetDisplayedRiderMaterial(Classic, nullptr); }
	}
	RiderSwaps.Reset();
	RiderTwins.Reset();
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
