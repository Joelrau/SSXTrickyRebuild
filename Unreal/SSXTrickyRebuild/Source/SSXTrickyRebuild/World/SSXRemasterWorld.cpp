#include "World/SSXRemasterWorld.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
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
#include "SSXShowOffDraw.h"
#include "SSXTrickyRebuild.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "AssetCompilingManager.h"
#include "ContentStreaming.h"
#include "RenderingThread.h"
#include "Sim/SimCoords.h"
#include "UObject/UObjectIterator.h"
#include "World/SSXRenderLook.h"
#include "World/SSXTerrainLod.h"
#include "World/SSXTerrainLodComponent.h"
#include "Sim/PatchWorld.h"
#include "Sim/SurfaceTable.h"
#include "Misc/FileHelper.h"
#include "World/SSXWorldLighting.h"
#include "World/SSXWorldPresentation.h"
#include "SnowboarderPawn.h"
#include "UI/SSXFeRiderActor.h"
#include "Race/SSXRaceManager.h"
#include "Characters/SSXCharacterAppearance.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Sim/RiderSim.h"
#include "Race/SSXReplay.h"

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
		{ TEXT("M_SSXProp_Translucent"), TEXT("/Game/Remastered/M_RmProp_Translucent.M_RmProp_Translucent") },
		{ TEXT("M_SSXSky"), TEXT("/Game/Remastered/M_RmSky.M_RmSky") },
		{ TEXT("M_SSXClearColour"), TEXT("/Game/Remastered/M_RmClearColour.M_RmClearColour") },
		{ TEXT("M_SSXProp_Translucent_TwoSided"), TEXT("/Game/Remastered/M_RmProp_Translucent.M_RmProp_Translucent") },
		{ TEXT("M_SSXPropCrowd"), TEXT("/Game/Remastered/M_RmPropCrowd.M_RmPropCrowd") },
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
	// The front-end scene (/Game/FrontEnd/Scene/ssxfe) is converted with the tracks (converted/tracks/ssxfe).
	const FString Root = Parts[1] == TEXT("FrontEnd") ? FString(TEXT("tracks")) : Parts[1].ToLower();
	return FString::Printf(TEXT("/Game/RemasterData/%s/%s/textures/%s.%s"), *Root, *Parts[Tex - 1], *Name, *Name);
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
		T.RiderFill = Sun < 1.f ? 1.f : 0.6f;
		T.SpriteGlow = Sun > 0.f && Sun < 1.f ? 0.5f : 0.35f;
		T.Clouds = Sun >= 1.f ? 0.6f : 0.f;   // day tracks; none at night, indoors or in the front end
		return T;
	};
	// Night (Snowdream, Merqury City, the trick tutorial): a faint coloured moon, the baked light and the lamps carry the
	// scene; indoor (Tokyo Megaplex, Pipedream): hardly any sun; the front end's dark ice backdrop: no sun, only its own light.
	static const FTrackTuning Table[] = {
		// Snowdream: its orange lamps at Merqury's strength turned the moonlit snow peach under large halos.
		[&Make]() { FTrackTuning T = Make(TEXT("snow"), 0.25f, 0.9f, 0.2f, 1.0f, 5.f, 25.f); T.Bloom = 0.45f; return T; }(),
		[&Make]() { FTrackTuning T = Make(TEXT("merquer"), 0.25f, 0.9f, 0.2f, 1.0f, 5.f, 60.f); T.Bloom = 0.8f; return T; }(),
		Make(TEXT("megaple"), 0.2f, 0.6f, 0.3f, 0.95f, 4.f, 40.f),
		Make(TEXT("pipe"), 0.2f, 0.6f, 0.3f, 0.95f, 4.f, 40.f),
		[&Make]() { FTrackTuning T = Make(TEXT("trick"), 0.25f, 0.9f, 0.2f, 1.0f, 5.f, 60.f); T.Bloom = 0.8f; return T; }(),
		// Elysium's orange sunset light would turn its pink-purple slopes peach: a paler sun, more of the baked light.
		Make(TEXT("elysium"), 1.6f, 0.3f, 0.5f, 0.7f, 3.f, 15.f),
		// Mesablanca's blue sky light cooled its warm sunset snow grey: less sky, more of the baked light, a warmer sun.
		Make(TEXT("mesa"), 2.0f, 0.85f, 0.25f, 0.8f, 3.f, 15.f),
		[&Make]() { FTrackTuning T = Make(TEXT("frontend"), 0.f, 0.6f, 0.05f, 1.0f, 3.f, 15.f); T.FogDensity = 0.f; T.Saturation = 1.f; T.Bloom = 0.3f; T.MaxLights = 0; T.RiderFill = 0.85f;
			T.Emissive = 0.f; T.Sparkle = 0.f; return T; }(),   // the ice backdrop's bright texels are not lamps
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

UMaterialInterface* SSXRemaster::ClassicMaterial(UMeshComponent& C, int32 Slot)
{
	UWorld* W = C.GetWorld();
	USSXRemasterWorld* R = W ? W->GetSubsystem<USSXRemasterWorld>() : nullptr;
	return R ? R->ClassicMaterial(C, Slot) : C.GetMaterial(Slot);
}

UMaterialInstanceDynamic* SSXRemaster::ClassicMid(UMeshComponent& C, int32 Slot)
{
	UWorld* W = C.GetWorld();
	USSXRemasterWorld* R = W ? W->GetSubsystem<USSXRemasterWorld>() : nullptr;
	return R ? R->ClassicMid(C, Slot) : C.CreateAndSetMaterialInstanceDynamic(Slot);
}

void SSXRemaster::SetClassicMaterial(UMeshComponent& C, int32 Slot, UMaterialInterface* M)
{
	UWorld* W = C.GetWorld();
	USSXRemasterWorld* R = W ? W->GetSubsystem<USSXRemasterWorld>() : nullptr;
	if (R) { R->SetClassicMaterial(C, Slot, M); }
	else { C.SetMaterial(Slot, M); }
}

void SSXRemaster::SetGlow(UMeshComponent& C, float Glow)
{
	UWorld* W = C.GetWorld();
	if (USSXRemasterWorld* R = W ? W->GetSubsystem<USSXRemasterWorld>() : nullptr) { R->SetGlow(C, Glow); }
}

bool SSXRemaster::RealRiderShadows(const UWorld& World)
{
	return SSXRenderLook::Get() == ESSXRenderLook::Remastered
		&& TuningFor(UWorld::RemovePIEPrefix(World.GetMapName()).ToLower()).SunLux >= 1.f;
}

int32 SSXRemaster::ShadowedLights(int32 Quality, int32 NumLights)
{
	return Quality >= 3 ? NumLights : Quality == 2 ? FMath::Min(NumLights, 16) : 0;
}

int32 SSXRemaster::SurfaceClass(float Grip, float DragQuadratic, float SinkDepth, float CruiseKmh)
{
	if (Grip < 0.15f && SinkDepth <= 1.f && CruiseKmh >= 60.f) { return 1; }
	if (Grip < 0.15f && DragQuadratic > 0.05f) { return 3; }
	if (SinkDepth >= 15.f) { return 2; }
	return 0;
}

TMap<int32, int32> SSXRemaster::TerrainSurfaceClasses(const FString& Stem)
{
	TMap<int32, int32> Out;
	FSurfaceTable Surfaces;
	TArray<uint8> Bytes;
	FSSXTerrainLodData Lod;
	if (!Surfaces.LoadFromFile(FSurfaceTable::DefaultPath()) || !Lod.Load(FSSXTerrainLodData::DefaultPath(Stem))
		|| !FFileHelper::LoadFileToArray(Bytes, *FPatchWorld::DefaultTrackPath(Stem)))
	{
		return Out;
	}
	// .patches.bin: 96-byte header (count at +8), 232-byte records (surface at +216, source index at +224).
	constexpr int32 Header = 96, Record = 232;
	auto I32 = [&Bytes](int64 Off) { int32 X = 0; if (Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&X, Bytes.GetData() + Off, 4); } return X; };
	const int32 Count = Bytes.Num() >= Header ? I32(8) : 0;
	TMap<int32, int32> ClassOfSource;
	for (int32 K = 0; K < Count && Header + int64(K + 1) * Record <= Bytes.Num(); ++K)
	{
		const FSurfaceParams& P = Surfaces.Get(I32(Header + int64(K) * Record + 216));
		ClassOfSource.Add(I32(Header + int64(K) * Record + 224),
			SurfaceClass(float(P.Grip()), float(P.DragQuadratic()), float(P.SinkDepth()), float(P.CruiseKmh())));
	}
	TMap<int32, TArray<int32, TInlineAllocator<4>>> Votes;
	for (const FSSXTerrainLodData::FPatch& P : Lod.Patches)
	{
		if (const int32* C = ClassOfSource.Find(P.PbdIndex))
		{
			TArray<int32, TInlineAllocator<4>>& V = Votes.FindOrAdd(P.Texture);
			V.SetNumZeroed(4);
			++V[*C];
		}
	}
	for (const TPair<int32, TArray<int32, TInlineAllocator<4>>>& T : Votes)
	{
		int32 Best = 0;
		for (int32 C = 1; C < 4; ++C) { if (T.Value[C] > T.Value[Best]) { Best = C; } }
		Out.Add(T.Key, Best);
	}
	return Out;
}

float SSXRemaster::DrawDistanceScale(int32 Quality)
{
	static const float Scale[] = { 1.5f, 2.f, 3.f, 4.f };
	return Scale[FMath::Clamp(Quality, 0, 3)];
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
		SyncFollowers();
		UpdateFrontEndSpots();
		UpdateDepthOfField();
		UpdateTrails();
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
	if (UWorld* W = GetWorld())
	{
		if (UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr, SSXRemaster::CollectionPath))
		{
			UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("DrawDistanceScale"), SSXRemaster::DrawDistanceScale(AppliedQuality));
		}
	}
	if (UExponentialHeightFogComponent* F = Fog.Get())
	{
		F->SetStartDistance(float(FogStartBase * SSXRemaster::DrawDistanceScale(AppliedQuality)));
	}
	if (APostProcessVolume* V = Volume.Get())
	{
		// Motion blur only at Epic, and gentle.
		V->Settings.bOverride_MotionBlurAmount = true;
		V->Settings.MotionBlurAmount = AppliedQuality >= 3 ? 0.25f : 0.f;
		V->Settings.bOverride_MotionBlurMax = true;
		V->Settings.MotionBlurMax = 2.f;
	}
	const int32 Shadowed = bAllLightShadows ? LocalLights.Num() : SSXRemaster::ShadowedLights(AppliedQuality, LocalLights.Num());
	for (int32 I = 0; I < LocalLights.Num(); ++I)
	{
		if (ULocalLightComponent* C = LocalLights[I].Get()) { C->SetCastShadows(I < Shadowed); }
	}
}

void USSXRemasterWorld::SetTrailParams()
{
	UWorld* W = GetWorld();
	UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr, SSXRemaster::CollectionPath);
	if (!W || !Mpc) { return; }
	UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("TrailX"), float(TrailOrigin.X));
	UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("TrailY"), float(TrailOrigin.Y));
	UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("TrailSize"), TrailMap ? float(SSXRemaster::TrailSize) : 1.0e9f);
}

void USSXRemasterWorld::UpdateTrails()
{
	UWorld* W = GetWorld();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!TrailMap || !TrailStamp || !TrailShift || !PC || !PC->PlayerCameraManager || !ASSXRaceManager::Find(W)) { return; }
	using namespace SSXRemaster;
	// The map follows the camera: once it is a quarter of the map away, shift the contents and recentre.
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector2D Cam2(Cam.X, Cam.Y);
	if (FVector2D::Distance(Cam2, TrailOrigin) > TrailSize * 0.25)
	{
		const double Texel = TrailSize / TrailTexels;
		const FVector2D NewOrigin(FMath::RoundToDouble(Cam2.X / Texel) * Texel, FMath::RoundToDouble(Cam2.Y / Texel) * Texel);
		const FVector2D Delta = (NewOrigin - TrailOrigin) / TrailSize;
		TrailShift->SetTextureParameterValue(TEXT("Prev"), TrailMap);
		TrailShift->SetVectorParameterValue(TEXT("Offset"), FLinearColor(float(Delta.X), float(Delta.Y), 0.f, 0.f));
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(W, TrailScratch, TrailShift);
		TrailShift->SetTextureParameterValue(TEXT("Prev"), TrailScratch);
		TrailShift->SetVectorParameterValue(TEXT("Offset"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(W, TrailMap, TrailShift);
		TrailOrigin = NewOrigin;
		SetTrailParams();
	}
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Ctx;
	bool bDrawing = false;
	const double PxPerCm = TrailTexels / TrailSize;
	for (TActorIterator<ASnowboarderPawn> It(W); It; ++It)
	{
		const FRiderSim* Sim = It->GetSim();
		const USSXCharacterAppearance* App = It->GetAppearance();
		const UStaticMeshComponent* Board = App ? App->GetBoardMesh() : nullptr;
		if (!Sim || !Board || Sim->GetState().Motion != ERiderMotion::Ground)
		{
			TrailLast.Remove(*It);
			continue;
		}
		const FVector Now = Board->GetComponentLocation();
		const FVector* Last = TrailLast.Find(*It);
		const FVector From = Last && FVector::Dist(*Last, Now) < 500.0 ? *Last : Now;
		const double Len = FVector::Dist(From, Now);
		if (Last && Len < TrailStep) { continue; }
		if (!bDrawing)
		{
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(W, TrailMap, Canvas, CanvasSize, Ctx);
			bDrawing = Canvas != nullptr;
			if (!bDrawing) { return; }
		}
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / TrailStep));
		for (int32 S = 1; S <= Steps; ++S)
		{
			const FVector P = FMath::Lerp(From, Now, double(S) / Steps);
			const FVector2D Uv = (FVector2D(P.X, P.Y) - TrailOrigin) / TrailSize + FVector2D(0.5, 0.5);
			const FVector2D Px = Uv * TrailTexels;
			const double R = TrailRadius * PxPerCm;
			Canvas->K2_DrawMaterial(TrailStamp, Px - FVector2D(R, R), FVector2D(2 * R, 2 * R), FVector2D::ZeroVector);
		}
		TrailLast.Add(*It, Now);
	}
	if (bDrawing) { UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(W, Ctx); }
}

void USSXRemasterWorld::UpdateDepthOfField()
{
	UWorld* W = GetWorld();
	APostProcessVolume* V = Volume.Get();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!V || !PC || !PC->PlayerCameraManager) { return; }
	const ASSXRaceManager* Race = ASSXRaceManager::Find(W);
	const int32 State = Race ? Race->GetReplay().GetState() : SSXReplay::Record;
	const bool bReplay = Race && State != SSXReplay::Record;
	const bool bFrontEnd = !Race;
	float Focus = 0.f;
	if (bReplay || bFrontEnd)
	{
		const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
		const FVector Fwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
		double Best = 0.0;
		auto Consider = [&](const AActor& A)
		{
			const FVector To = A.GetActorLocation() + FVector(0, 0, 90) - Cam;
			const double D = To.Size();
			const double Facing = D > 1.0 ? FVector::DotProduct(To / D, Fwd) : 0.0;
			if (Facing > 0.8 && Facing > Best) { Best = Facing; Focus = float(D); }
		};
		if (bFrontEnd) { for (TActorIterator<ASSXFeRiderActor> It(W); It; ++It) { Consider(**It); } }
		else { for (TActorIterator<ASnowboarderPawn> It(W); It; ++It) { Consider(**It); } }
	}
	FPostProcessSettings& S = V->Settings;
	S.bOverride_DepthOfFieldFocalDistance = Focus > 0.f;
	S.bOverride_DepthOfFieldFstop = Focus > 0.f;
	S.bOverride_DepthOfFieldSensorWidth = Focus > 0.f;
	S.DepthOfFieldFocalDistance = Focus;
	S.DepthOfFieldFstop = bFrontEnd ? 2.8f : 4.f;
	S.DepthOfFieldSensorWidth = 24.576f;
}

void USSXRemasterWorld::UpdateFrontEndSpots()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	// Front end: a stage spotlight above each carousel rider on the camera side (the light list's points sit inside
	// the riders: they only light the riders' own rig).
	FVector CamLoc = FVector::ZeroVector;
	bool bCam = false;
	if (const APlayerController* PC = W->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager) { CamLoc = PC->PlayerCameraManager->GetCameraLocation(); bCam = true; }
	}
	// Only the rider nearest the view centre casts its spotlight's shadow (every spot shadowing the whole stage
	// overflowed the virtual shadow maps).
	const AActor* Centre = nullptr;
	if (bCam)
	{
		const FVector Fwd = W->GetFirstPlayerController()->PlayerCameraManager->GetCameraRotation().Vector();
		double Best = 0.0;
		for (TActorIterator<ASSXFeRiderActor> It(W); It; ++It)
		{
			const FVector To = (It->GetActorLocation() - CamLoc).GetSafeNormal();
			const double F = FVector::DotProduct(To, Fwd);
			if (F > Best) { Best = F; Centre = *It; }
		}
	}
	for (TActorIterator<ASSXFeRiderActor> It(W); It; ++It)
	{
		const FVector Target = It->GetActorLocation() + FVector(0, 0, 100);
		FVector ToCam = bCam ? CamLoc - Target : It->GetActorForwardVector();
		ToCam.Z = 0.0;
		ToCam = ToCam.GetSafeNormal();
		if (ToCam.IsNearlyZero()) { ToCam = FVector::ForwardVector; }
		const FVector At = Target + ToCam * 250.0 + FVector(0, 0, 400);
		const FRotator Rot = (Target - At).Rotation();
		ASpotLight* Spot = Cast<ASpotLight>(FrontEndSpots.FindRef(*It).Get());
		if (!Spot)
		{
			FActorSpawnParameters P;
			P.ObjectFlags = RF_Transient;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Spot = W->SpawnActor<ASpotLight>(At, Rot, P);
			if (!Spot) { continue; }
			USpotLightComponent* C = CastChecked<USpotLightComponent>(Spot->GetLightComponent());
			C->SetMobility(EComponentMobility::Movable);
			C->SetIntensityUnits(ELightUnits::Candelas);
			C->SetIntensity(18.f);
			C->SetLightColor(FLinearColor(1.f, 0.95f, 0.88f));
			C->SetOuterConeAngle(28.f);
			C->SetInnerConeAngle(18.f);
			C->SetAttenuationRadius(800.f);
			FrontEndSpots.Add(*It, Spot);
		}
		else
		{
			Spot->SetActorLocationAndRotation(At, Rot);
		}
		if (ULightComponent* L = Spot->GetLightComponent())
		{
			const bool bShadow = *It == Centre;
			if (L->CastShadows != bShadow) { L->SetCastShadows(bShadow); }
		}
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
					UTexture* Base = nullptr;
					if (Classic->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseTex")), Base) && Base)
					{
						ApplyDerivedMaps(*Twin, Base);
					}
					RiderTwins.Add(Classic, Twin);
				}
				Mesh->SetMaterial(S, Twin);
				Pres->SetDisplayedRiderMaterial(Classic, Twin);
				RiderSwaps.Add({ Mesh, S, Classic });
				if (!Mesh->CastShadow)
				{
					Mesh->SetCastShadow(true);
					Mesh->bCastDynamicShadow = true;
					Mesh->MarkRenderStateDirty();
					ShadowCasters.Add(Mesh);
				}
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

UMaterialInterface* USSXRemasterWorld::RemasteredParentOf(UMaterialInterface* Classic)
{
	const UMaterial* Root = RootOf(Classic);
	const TCHAR* ParentPath = Root ? SSXRemaster::RemasteredParentPath(Root->GetName()) : nullptr;
	return ParentPath ? LoadObject<UMaterialInterface>(nullptr, ParentPath) : nullptr;
}

void USSXRemasterWorld::ApplyDerivedMaps(UMaterialInstanceDynamic& Mid, UTexture* Base)
{
	static TWeakObjectPtr<UTexture> FlatNormal, NeutralMasks;
	if (!FlatNormal.IsValid()) { FlatNormal = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/FlatNormal.FlatNormal")); }
	if (!NeutralMasks.IsValid()) { NeutralMasks = LoadObject<UTexture>(nullptr, TEXT("/Game/Remastered/T_RmMasksNeutral.T_RmMasksNeutral")); }
	const FString Pkg = Base ? Base->GetOutermost()->GetName() : FString();
	UTexture* C = Base ? LoadIfExists(SSXRemaster::DerivedMapPath(Pkg, TEXT("C"))) : nullptr;
	UTexture* N = Base ? LoadIfExists(SSXRemaster::DerivedMapPath(Pkg, TEXT("N"))) : nullptr;
	UTexture* M = Base ? LoadIfExists(SSXRemaster::DerivedMapPath(Pkg, TEXT("M"))) : nullptr;
	if (Base) { Mid.SetTextureParameterValue(TEXT("BaseTex"), C ? C : Base); }
	if (N || FlatNormal.IsValid()) { Mid.SetTextureParameterValue(TEXT("RmNormal"), N ? N : FlatNormal.Get()); }
	if (M || NeutralMasks.IsValid()) { Mid.SetTextureParameterValue(TEXT("RmMasks"), M ? M : NeutralMasks.Get()); }
}

UMaterialInstanceDynamic* USSXRemasterWorld::MakeFollower(FSwap& S, UMaterialInstanceDynamic& Classic, UMeshComponent& C)
{
	UMaterialInterface* Parent = RemasteredParentOf(&Classic);
	if (!Parent) { return nullptr; }
	UMaterialInstanceDynamic* F = UMaterialInstanceDynamic::Create(Parent, &C);
	F->CopyInterpParameters(&Classic);
	S.Follower = F;
	S.FollowedBase = nullptr;
	C.SetMaterial(S.Slot, F);
	return F;
}

USSXRemasterWorld::FSwap* USSXRemasterWorld::FindSwap(UMeshComponent& C, int32 Slot)
{
	const int32* I = SwapIndex.Find(TPair<TWeakObjectPtr<UMeshComponent>, int32>(&C, Slot));
	return I && Swaps.IsValidIndex(*I) ? &Swaps[*I] : nullptr;
}

UMaterialInterface* USSXRemasterWorld::ClassicMaterial(UMeshComponent& C, int32 Slot)
{
	const FSwap* S = FindSwap(C, Slot);
	return S ? S->Original.Get() : C.GetMaterial(Slot);
}

UMaterialInstanceDynamic* USSXRemasterWorld::ClassicMid(UMeshComponent& C, int32 Slot)
{
	FSwap* S = FindSwap(C, Slot);
	if (!S) { return C.CreateAndSetMaterialInstanceDynamic(Slot); }
	UMaterialInstanceDynamic* Classic = Cast<UMaterialInstanceDynamic>(S->Original.Get());
	if (!Classic)
	{
		UMaterialInterface* Orig = S->Original.Get();
		if (!Orig) { return nullptr; }
		Classic = UMaterialInstanceDynamic::Create(Orig, &C);
		ClassicMids.Add(Classic);
		S->Original = Classic;
	}
	if (!S->Follower.IsValid()) { MakeFollower(*S, *Classic, C); }
	return Classic;
}

void USSXRemasterWorld::SetClassicMaterial(UMeshComponent& C, int32 Slot, UMaterialInterface* M)
{
	FSwap* S = bApplied ? FindSwap(C, Slot) : nullptr;
	if (!S)
	{
		C.SetMaterial(Slot, M);
		if (bApplied) { SwapSlot(C, Slot); }
		return;
	}
	S->Original = M;
	S->Follower = nullptr;
	S->FollowedBase = nullptr;
	UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(M);
	UMaterialInterface* R = Mid ? nullptr : (M ? RemasteredFor(M) : nullptr);
	if (Mid && RemasteredParentOf(Mid))
	{
		ClassicMids.AddUnique(Mid);
		MakeFollower(*S, *Mid, C);
	}
	else { C.SetMaterial(Slot, R ? R : M); }
}

UMaterialInstanceDynamic* USSXRemasterWorld::OwnInstance(UMeshComponent& C, int32 Slot, UMaterialInstanceDynamic& Shared, UMaterialInterface* Parent)
{
	// A component's own copy of a shared lit twin (an instance's parent must be a material or constant instance).
	UMaterialInstanceDynamic* Own = UMaterialInstanceDynamic::Create(Parent ? Parent : Shared.Parent.Get(), &C);
	Own->CopyParameterOverrides(&Shared);
	C.SetMaterial(Slot, Own);
	return Own;
}

void USSXRemasterWorld::ApplySurfaceLook()
{
	UWorld* W = GetWorld();
	const FString Stem = W ? UWorld::RemovePIEPrefix(W->GetMapName()).ToLower() : FString();
	TArray<FString> Lines;
	if (Stem.IsEmpty() || !FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Stem + TEXT(".surfacelook.txt"))))
	{
		return;
	}
	TMap<int32, int32> Look;
	for (const FString& L : Lines)
	{
		FString A, B;
		if (!L.StartsWith(TEXT("#")) && L.Split(TEXT(" "), &A, &B)) { Look.Add(FCString::Atoi(*A), FCString::Atoi(*B)); }
	}
	UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Remastered/M_RmPropGlass.M_RmPropGlass"));
	int32 Slots = 0;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		const int32 Inst = SSXShowOff::InstanceOfActor(*It);
		if (Inst == INDEX_NONE || !Look.Contains(Inst)) { continue; }
		TInlineComponentArray<UMeshComponent*> Meshes(*It);
		for (UMeshComponent* C : Meshes)
		{
			for (int32 S = 0; S < C->GetNumMaterials(); ++S)
			{
				UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C->GetMaterial(S));
				const UMaterial* Root = Mid ? RootOf(Mid) : nullptr;
				if (!Root) { continue; }
				if (Root->GetName() == TEXT("M_RmProp_Translucent") && Glass)
				{
					OwnInstance(*C, S, *Mid, Glass);
					++Slots;
				}
				else if (Root->GetName().StartsWith(TEXT("M_RmProp")))
				{
					float Cur = 0.f;
					if (Mid->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("RmSurface")), Cur))
					{
						OwnInstance(*C, S, *Mid, nullptr)->SetScalarParameterValue(TEXT("RmSurface"), 4.f);
						++Slots;
					}
				}
			}
		}
	}
	UE_LOG(LogSSX, Display, TEXT("[remaster] %s: %d glass / water props, %d material slots reflective"), *Stem, Look.Num(), Slots);
}

void USSXRemasterWorld::SetGlow(UMeshComponent& C, float Glow)
{
	if (!bApplied) { return; }
	for (int32 S = 0; S < C.GetNumMaterials(); ++S)
	{
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(C.GetMaterial(S));
		float Cur = 0.f;
		if (!Mid || !Mid->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("RmGlow")), Cur) || Cur == Glow) { continue; }
		bool bShared = false;
		for (const TPair<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>>& P : Remastered) { bShared |= P.Value == Mid; }
		if (bShared)
		{
			// A lit twin shared by every slot with this Classic material: this component gets its own (an instance's parent
			// must be a material or constant instance, so it copies the twin's values onto the twin's parent).
			UMaterialInstanceDynamic* Own = UMaterialInstanceDynamic::Create(Mid->Parent, &C);
			Own->CopyParameterOverrides(Mid);
			C.SetMaterial(S, Own);
			Mid = Own;
		}
		Mid->SetScalarParameterValue(TEXT("RmGlow"), Glow);
	}
}

void USSXRemasterWorld::SyncFollowers()
{
	for (FSwap& S : Swaps)
	{
		UMaterialInstanceDynamic* F = S.Follower.Get();
		const UMaterialInstanceDynamic* Classic = Cast<UMaterialInstanceDynamic>(S.Original.Get());
		if (!F || !Classic) { continue; }
		for (const FScalarParameterValue& V : Classic->ScalarParameterValues) { F->SetScalarParameterValue(V.ParameterInfo.Name, V.ParameterValue); }
		for (const FVectorParameterValue& V : Classic->VectorParameterValues) { F->SetVectorParameterValue(V.ParameterInfo.Name, V.ParameterValue); }
		UTexture* Base = nullptr;
		Classic->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseTex")), Base);
		if (Base != S.FollowedBase.Get())
		{
			ApplyDerivedMaps(*F, Base);
			S.FollowedBase = Base;
		}
	}
}

UMaterialInterface* USSXRemasterWorld::RemasteredFor(UMaterialInterface* Classic)
{
	if (TObjectPtr<UMaterialInterface>* Found = Remastered.Find(Classic)) { return *Found; }
	UMaterialInterface* Result = nullptr;
	if (UMaterialInterface* Parent = RemasteredParentOf(Classic))
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
			// Terrain textures are named <index>_<n> (converted/tracks/<stem>/textures).
			const FString N = Base->GetName();
			int32 K = 0;
			while (K < N.Len() && FChar::IsDigit(N[K])) { ++K; }
			const UMaterial* Root = RootOf(Classic);
			if (K > 0 && Root && Root->GetName().StartsWith(TEXT("M_SSXTerrain")))
			{
				if (const int32* C = SurfaceClasses.Find(FCString::Atoi(*N.Left(K)))) { Mid->SetScalarParameterValue(TEXT("RmSurface"), float(*C)); }
			}
			ApplyDerivedMaps(*Mid, Base);
		}
		if (TrailMap && RootOf(Classic) && RootOf(Classic)->GetName().StartsWith(TEXT("M_SSXTerrain"))) { Mid->SetTextureParameterValue(TEXT("RmTrails"), TrailMap); }
		Result = Mid;
	}
	Remastered.Add(Classic, Result);
	return Result;
}

void USSXRemasterWorld::SwapComponent(UMeshComponent& C)
{
	for (int32 S = 0; S < C.GetNumMaterials(); ++S) { SwapSlot(C, S); }
}

void USSXRemasterWorld::SwapSlot(UMeshComponent& C, int32 S)
{
	UMaterialInterface* M = C.GetMaterial(S);
	if (UMaterialInstanceDynamic* Classic = Cast<UMaterialInstanceDynamic>(M))
	{
		// A Classic instance game code already animates: a per-slot twin that follows it. The slot shows the twin, so the
		// Classic instance is kept alive here (game code holds it weakly).
		if (!RemasteredParentOf(Classic)) { return; }
		ClassicMids.AddUnique(Classic);
		FSwap& Sw = Swaps.AddDefaulted_GetRef();
		Sw.Component = &C;
		Sw.Slot = S;
		Sw.Original = Classic;
		SwapIndex.Add(TPair<TWeakObjectPtr<UMeshComponent>, int32>(&C, S), Swaps.Num() - 1);
		MakeFollower(Sw, *Classic, C);
		return;
	}
	UMaterialInterface* R = M ? RemasteredFor(M) : nullptr;
	if (!R) { return; }
	Swaps.Add({ &C, S, M });
	SwapIndex.Add(TPair<TWeakObjectPtr<UMeshComponent>, int32>(&C, S), Swaps.Num() - 1);
	C.SetMaterial(S, R);
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
	if (SurfaceClasses.Num() == 0)
	{
		SurfaceClasses = SSXRemaster::TerrainSurfaceClasses(UWorld::RemovePIEPrefix(W->GetMapName()).ToLower());
	}
	if (ASSXRaceManager::Find(W))
	{
		auto MakeRt = [this]()
		{
			UTextureRenderTarget2D* Rt = NewObject<UTextureRenderTarget2D>(this);
			Rt->RenderTargetFormat = RTF_R16f;
			Rt->ClearColor = FLinearColor::Black;
			Rt->AddressX = TA_Clamp;
			Rt->AddressY = TA_Clamp;
			Rt->InitAutoFormat(SSXRemaster::TrailTexels, SSXRemaster::TrailTexels);
			Rt->UpdateResourceImmediate(true);
			return Rt;
		};
		TrailMap = MakeRt();
		TrailScratch = MakeRt();
		UMaterialInterface* Stamp = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Remastered/M_RmTrailStamp.M_RmTrailStamp"));
		UMaterialInterface* Shift = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Remastered/M_RmTrailShift.M_RmTrailShift"));
		TrailStamp = Stamp ? UMaterialInstanceDynamic::Create(Stamp, this) : nullptr;
		TrailShift = Shift ? UMaterialInstanceDynamic::Create(Shift, this) : nullptr;
		TrailLast.Reset();
		if (const APlayerController* PC = W->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager) { const FVector C = PC->PlayerCameraManager->GetCameraLocation(); TrailOrigin = FVector2D(C.X, C.Y); }
		}
		SetTrailParams();
	}
	for (ULevel* L : W->GetLevels())
	{
		if (L) { SwapLevel(*L); }
	}
	ApplySurfaceLook();
	// The terrain is drawn by USSXTerrainLodComponent (no shadow pass) with the full-detail mesh hidden: that mesh casts
	// the sun's shadow (overhangs, tunnels and cliffs block it).
	for (TObjectIterator<USSXTerrainLodComponent> It; It; ++It)
	{
		UStaticMeshComponent* M = It->GetWorld() == W ? It->Replaced.Get() : nullptr;
		if (!M) { continue; }
		TerrainShadows.Add({ M, M->CastShadow != 0, M->bCastHiddenShadow != 0 });
		M->SetCastShadow(true);
		M->bCastHiddenShadow = true;
		M->MarkRenderStateDirty();
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
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = SSXRemaster::TuningFor(UWorld::RemovePIEPrefix(W->GetMapName()).ToLower()).Bloom;
		S.bOverride_BloomThreshold = true;
		S.BloomThreshold = 1.f;
		S.bOverride_DynamicGlobalIlluminationMethod = true;
		S.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
		S.bOverride_ReflectionMethod = true;
		S.ReflectionMethod = EReflectionMethod::Lumen;
		S.bOverride_LumenFrontLayerTranslucencyReflections = true;   // the glass / water layers (M_RmPropGlass)
		S.LumenFrontLayerTranslucencyReflections = true;
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
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("RiderFill"), T.RiderFill);
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("SpriteGlow"), T.SpriteGlow);
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("Clouds"), T.Clouds);
		UKismetMaterialLibrary::SetScalarParameterValue(W, Mpc, TEXT("Sparkle"), T.Sparkle);
	}

	const USSXWorldPresentation* Pres = W->GetSubsystem<USSXWorldPresentation>();
	FSSXLightWorld EditorLights;   // editor worlds (captures) have no presentation subsystem
	FSSXTrackPresentation EditorTrack;
	const FSSXTrackPresentation* Track = Pres ? Pres->GetTrack() : nullptr;
	if (!Pres)
	{
		EditorLights.Load(FSSXLightWorld::DefaultPath(Stem == TEXT("frontend") ? FString(TEXT("ssxfe")) : Stem));
		TArray<FSSXTrackPresentation> Table;
		const int32 Row = FSSXTrackPresentation::RowForStem(Stem);
		if (Row >= 0 && FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table) && Table.IsValidIndex(Row))
		{
			EditorTrack = Table[Row];
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
	if (ADirectionalLight* D = T.SunLux > 0.f ? W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, SunDir.Rotation(), P) : nullptr)
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
		LocalLights.Add(C);
	}
	bAllLightShadows = T.bLightShadows;
	ApplyQuality();

	if (Track && T.FogDensity > 0.f)
	{
		if (AExponentialHeightFog* F = W->SpawnActor<AExponentialHeightFog>(P))
		{
			UExponentialHeightFogComponent* C = F->GetComponent();
			C->SetFogInscatteringColor(FLinearColor(Track->ClearColour()));
			C->SetFogDensity(T.FogDensity);
			C->SetFogHeightFalloff(0.0002f);
			FogStartBase = Track->DrawDist1P * 0.35;
			C->SetStartDistance(float(FogStartBase * SSXRemaster::DrawDistanceScale(SSXRenderLook::GetQuality())));
			Fog = C;
			C->SetFogMaxOpacity(0.85f);
			// No fog on the sky dome: cut it off before the dome's nearest point (some domes are low, wide caps whose
			// top is far nearer than their horizon; the dome is centred on the camera at run time).
			double Nearest = SSXRemaster::SkyDistance;
			for (TActorIterator<AActor> It(W); It; ++It)
			{
				if (!It->ActorHasTag(USSXWorldPresentation::SkyTag)) { continue; }
				FVector Origin, Extent;
				It->GetActorBounds(false, Origin, Extent);
				const FVector Lo = Origin - Extent - It->GetActorLocation(), Hi = Origin + Extent - It->GetActorLocation();
				for (const double D : { Hi.Z, Hi.X, -Lo.X, Hi.Y, -Lo.Y }) { if (D > 100.0) { Nearest = FMath::Min(Nearest, D); } }
			}
			C->SetFogCutoffDistance(float(Nearest * 0.8));
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
	SwapIndex.Reset();
	ClassicMids.Reset();
	for (const TPair<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>>& P : FrontEndSpots)
	{
		if (AActor* A = P.Value.Get()) { A->Destroy(); }
	}
	FrontEndSpots.Reset();
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
	for (const TWeakObjectPtr<UMeshComponent>& M : ShadowCasters)
	{
		if (UMeshComponent* C = M.Get())
		{
			C->SetCastShadow(false);
			C->bCastDynamicShadow = false;
			C->MarkRenderStateDirty();
		}
	}
	ShadowCasters.Reset();
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
	LocalLights.Reset();
	for (const FShadowFlags& F : TerrainShadows)
	{
		if (UPrimitiveComponent* C = F.Component.Get())
		{
			C->SetCastShadow(F.bCast);
			C->bCastHiddenShadow = F.bHidden;
			C->MarkRenderStateDirty();
		}
	}
	TerrainShadows.Reset();
	TrailMap = nullptr;
	TrailScratch = nullptr;
	TrailLast.Reset();
	Remastered.Reset();   // the cached terrain twins point at the released trail map
	SetTrailParams();
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
