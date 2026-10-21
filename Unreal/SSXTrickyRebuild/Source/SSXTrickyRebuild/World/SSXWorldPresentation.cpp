#include "World/SSXWorldPresentation.h"

#include "Camera/PlayerCameraManager.h"
#include "Characters/SSXRiderDepth.h"
#include "Characters/SSXRiderGloss.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Progression/SSXSettingsEffects.h"
#include "Race/SSXRaceManager.h"
#include "World/SSXTerrainLodComponent.h"
#include "Sim/SimCoords.h"
#include "SnowboarderPawn.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFeRiderActor.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXFeRiders.h"

const FName USSXWorldPresentation::SkyTag(TEXT("SSXSky"));

namespace
{
	TAutoConsoleVariable<float> CVarSoftDepthRange(TEXT("ssx.Render.SoftDepthRange"), 20000.f,
		TEXT("Soft props further than this from the camera (cm) write no custom depth: their soft texels then hide no ")
		TEXT("sprites or later soft layers (render cost; default 200 m; 0 = every soft prop, as the original)."));
#if !UE_BUILD_SHIPPING   // screenshot / lightmap debug switches, compiled out of the release build
	TAutoConsoleVariable<float> CVarShotAfter(TEXT("ssx.World.ShotAfter"), 0.0f,
		TEXT("Take a 1280x720 HighResShot this many seconds after the track starts, then quit (0 = off)."));

	TAutoConsoleVariable<float> CVarLightmapMode(TEXT("ssx.World.LightmapMode"), -1.0f,
		TEXT("Terrain lightmap blend: 0 = tex x A/128 (default), 1 = + lightmap RGB, 2 = no lightmap, 3 = lightmap alpha only."));
	TAutoConsoleVariable<float> CVarLightmapSwap(TEXT("ssx.World.LightmapSwapUV"), -1.0f, TEXT("Swap u/v inside the lightmap tile (1/0)."));
	TAutoConsoleVariable<float> CVarLightmapFlipU(TEXT("ssx.World.LightmapFlipU"), -1.0f, TEXT("Mirror u inside the lightmap tile (1/0)."));
	TAutoConsoleVariable<float> CVarLightmapFlipV(TEXT("ssx.World.LightmapFlipV"), -1.0f, TEXT("Mirror v inside the lightmap tile (1/0)."));

	TAutoConsoleVariable<int32> CVarShotHideRiders(TEXT("ssx.World.ShotHideRiders"), 0,
		TEXT("With ssx.World.ShotAfter: hide every rider so the shot shows track scenery only."));

	TAutoConsoleVariable<int32> CVarShotUI(TEXT("ssx.World.ShotUI"), 0,
		TEXT("With ssx.World.ShotAfter: take a window shot including the HUD / overlay widgets (\"shot showui\") instead of HighResShot."));

	TAutoConsoleVariable<FString> CVarShotView(TEXT("ssx.World.ShotView"), TEXT(""),
		TEXT("With ssx.World.ShotAfter: put the player pawn at X_Y_Z_Pitch_Yaw (Unreal space) before the shot."));
#endif

	FLinearColor ToColour(const FVector& V) { return FLinearColor(float(V.X), float(V.Y), float(V.Z), 1.0f); }
}

bool USSXWorldPresentation::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

TStatId USSXWorldPresentation::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USSXWorldPresentation, STATGROUP_Tickables);
}

void USSXWorldPresentation::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const FString Stem = UWorld::RemovePIEPrefix(InWorld.GetMapName()).ToLower();
	const int32 Id = FSSXTrackPresentation::IdForStem(Stem);
	if (Id < 0)
	{
		return;
	}

	TArray<FSSXTrackPresentation> Table;
	FString Err;
	const int32 Row = FSSXTrackPresentation::RowForStem(Stem);
	if (FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table, &Err) && Table.IsValidIndex(Row) && Table[Row].Id == Row)
	{
		Track = Table[Row];
		bHaveTrack = true;
		Adjust = FSSXRiderLightAdjust::From(Track);
	}
	else
	{
		UE_LOG(LogSSX, Warning, TEXT("[world] presentation table unavailable (%s); riders get no track adjustment"), *Err);
	}
	const FString LightStem = Stem == TEXT("frontend") ? FString(TEXT("ssxfe")) : Stem;
	if (!LightWorld.Load(FSSXLightWorld::DefaultPath(LightStem), &Err))
	{
		UE_LOG(LogSSX, Warning, TEXT("[world] %s"), *Err);
	}

	Collection = LoadObject<UMaterialParameterCollection>(nullptr, CollectionPath);
	if (UMaterialParameterCollection* Mpc = Collection)
	{
		if (UMaterialParameterCollectionInstance* Inst = InWorld.GetParameterCollectionInstance(Mpc))
		{
			if (bHaveTrack)
			{
				Inst->SetScalarParameterValue(TEXT("DrawDistance"), float(Track.DrawDist1P));
				const FIntVector Lod = Track.MipLodParams1P();
				Inst->SetScalarParameterValue(TEXT("MipLodOn"), Lod.X == 0 ? 1.0f : 0.0f);
				Inst->SetScalarParameterValue(TEXT("MipLodL"), float(Lod.Y));
				Inst->SetScalarParameterValue(TEXT("MipLodK"), float(Lod.Z));
				Inst->SetVectorParameterValue(TEXT("ClearColour"), ClearColourFor(Id, Track));
			}
		}
	}
	else
	{
		UE_LOG(LogSSX, Warning, TEXT("[world] %s missing (run Scripts/make_world_materials.py)"), CollectionPath);
	}

	RiderMaterial = LoadObject<UMaterialInterface>(nullptr, RiderMaterialPath);
	RiderMaskedMaterial = LoadObject<UMaterialInterface>(nullptr, RiderMaskedMaterialPath);
	// FUN_0025EDF0
	SetGlossSpecmap(SSXRiderGloss::SpecmapForTrackId(Id));

	SkyActors.Reset();
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		if (It->ActorHasTag(SkyTag))
		{
			if (USceneComponent* Root = It->GetRootComponent())
			{
				Root->SetMobility(EComponentMobility::Movable);
			}
			SkyActors.Add(*It);
		}
	}
	if (bHaveTrack)
	{
		SpawnClearShell(InWorld);
	}
	SoftDepthProps.Reset();
	const int32 SoftProps = MarkSoftPropDepth(InWorld, &SoftDepthProps);
	TerrainLods.Reset();
	const int32 LodMeshes = USSXTerrainLodComponent::AttachToTerrain(InWorld, TerrainLods);
	bActive = true;
	UE_LOG(LogSSX, Display, TEXT("[world] %s (track %d): draw distance %.0f, %d lights, sky actors %d, rider adjust f=%.3f C=(%s) scale=%.3f, %d soft props write custom depth, %d terrain meshes at the original LOD"),
		*Stem, Id, Track.DrawDist1P, LightWorld.GetLights().Num(), SkyActors.Num(), Adjust.Blend, *Adjust.Target.ToString(), Adjust.Scale,
		SoftProps, LodMeshes);
}

bool USSXWorldPresentation::IsSoftBlendMaterial(const UMaterialInterface* M)
{
	while (const UMaterialInstance* Mi = Cast<UMaterialInstance>(M))
	{
		M = Mi->Parent;
	}
	const UMaterial* Root = Cast<UMaterial>(M);
	if (!Root)
	{
		return false;
	}
	const FName N = Root->GetFName();
	return N == FName(TEXT("M_SSXProp_Translucent")) || N == FName(TEXT("M_SSXProp_Translucent_TwoSided"));
}

int32 USSXWorldPresentation::MarkSoftPropDepth(UWorld& InWorld, TArray<TWeakObjectPtr<UStaticMeshComponent>>* OutComponents)
{
	// FUN_00200390 / 0x1E1F70
	int32 N = 0;
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
		for (UStaticMeshComponent* C : Comps)
		{
			bool bSoft = false;
			for (int32 I = 0; I < C->GetNumMaterials() && !bSoft; ++I)
			{
				bSoft = IsSoftBlendMaterial(C->GetMaterial(I));
			}
			if (bSoft)
			{
				C->SetRenderCustomDepth(true);
				if (OutComponents) { OutComponents->Add(C); }
				++N;
			}
		}
	}
	return N;
}

void USSXWorldPresentation::Tick(float DeltaTime)
{
	USSXTerrainLodComponent::ApplyCVar(TerrainLods);
	UpdateSoftDepthRange();
	UpdateSky();
	UpdateRiders();
#if !UE_BUILD_SHIPPING
	if (Collection)
	{
		if (UMaterialParameterCollectionInstance* Inst = GetWorld()->GetParameterCollectionInstance(Collection))
		{
			const TPair<const TCHAR*, TAutoConsoleVariable<float>*> Switches[] = { { TEXT("LightmapMode"), &CVarLightmapMode },
				{ TEXT("LightmapSwapUV"), &CVarLightmapSwap }, { TEXT("LightmapFlipU"), &CVarLightmapFlipU }, { TEXT("LightmapFlipV"), &CVarLightmapFlipV } };
			for (const auto& S : Switches)
			{
				const float V = S.Value->GetValueOnGameThread();
				if (V >= 0.0f)
				{
					Inst->SetScalarParameterValue(S.Key, V);
				}
			}
		}
	}

	const float ShotAfter = CVarShotAfter.GetValueOnGameThread();
	if (ShotAfter > 0.0f && GEngine)
	{
		ShotClock += DeltaTime;
		if (CVarShotHideRiders.GetValueOnGameThread() != 0)
		{
			for (TActorIterator<ASnowboarderPawn> It(GetWorld()); It; ++It)
			{
				It->SetActorHiddenInGame(true);
			}
		}
		TArray<FString> View;
		CVarShotView.GetValueOnGameThread().Replace(TEXT("_"), TEXT(" ")).ParseIntoArrayWS(View);
		APlayerController* ShotPC = GetWorld()->GetFirstPlayerController();
		if (View.Num() == 5 && ShotPC && ShotPC->GetPawn())
		{
			ShotPC->GetPawn()->SetActorLocation(FVector(FCString::Atod(*View[0]), FCString::Atod(*View[1]), FCString::Atod(*View[2])),
				false, nullptr, ETeleportType::TeleportPhysics);
			ShotPC->SetControlRotation(FRotator(FCString::Atod(*View[3]), FCString::Atod(*View[4]), 0.0));
		}
		if (!bShotTaken && ShotClock >= ShotAfter)
		{
			bShotTaken = true;
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PC->ConsoleCommand(CVarShotUI.GetValueOnGameThread() != 0 ? TEXT("shot showui") : TEXT("HighResShot 1280x720"));
			}
		}
		else if (bShotTaken && ShotClock >= ShotAfter + 5.0f)
		{
			GEngine->Exec(GetWorld(), TEXT("quit"));
		}
	}
#endif
}

FLinearColor USSXWorldPresentation::ClearColourParam(const FSSXTrackPresentation& T)
{
	const FColor C = T.ClearColour();
	return FLinearColor(C.R / 255.0f, C.G / 255.0f, C.B / 255.0f, C.A / 255.0f);
}

FLinearColor USSXWorldPresentation::ClearColourFor(int32 TrackId, const FSSXTrackPresentation& T)
{
	return TrackId == FrontEndId ? FLinearColor(0.f, 0.f, 0.f, 0x80 / 255.0f) : ClearColourParam(T);
}

void USSXWorldPresentation::SpawnClearShell(UWorld& InWorld)
{
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, ClearMaterialPath);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Mat || !Sphere)
	{
		UE_LOG(LogSSX, Warning, TEXT("[world] clear colour shell not drawn: %s or the engine sphere missing (run Scripts/make_world_materials.py)"), ClearMaterialPath);
		return;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	AActor* A = InWorld.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!A)
	{
		return;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(A, TEXT("ClearShell"));
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Sphere);
	C->SetMaterial(0, Mat);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->bNeverDistanceCull = true;
	C->SetWorldScale3D(FVector(ClearShellRadius / 50.0));   // the engine sphere has a 50 cm radius
	A->SetRootComponent(C);
	C->RegisterComponent();
#if WITH_EDITOR
	A->SetActorLabel(TEXT("SSXClearColour"));
#endif
	ClearShell = A;
}

void USSXWorldPresentation::UpdateSoftDepthRange()
{
	if (++SoftDepthFrame < 8) { return; }   // a few times a second: each change rebuilds the component's render state
	SoftDepthFrame = 0;
	const double Range = CVarSoftDepthRange.GetValueOnGameThread();
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager) { return; }
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	for (const TWeakObjectPtr<UStaticMeshComponent>& W : SoftDepthProps)
	{
		UStaticMeshComponent* C = W.Get();
		if (!C) { continue; }
		const double Gap = FVector::Dist(C->Bounds.Origin, Cam) - C->Bounds.SphereRadius;
		// 10 % hysteresis so a prop at the edge does not flip every update
		const bool bWant = Range <= 0.0 || Gap < (C->bRenderCustomDepth ? Range * 1.1 : Range);
		if (C->bRenderCustomDepth != bWant) { C->SetRenderCustomDepth(bWant); }
	}
}

void USSXWorldPresentation::UpdateSky()
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	for (const TWeakObjectPtr<AActor>& Sky : SkyActors)
	{
		if (AActor* A = Sky.Get())
		{
			A->SetActorLocation(Cam);
		}
	}
	if (AActor* A = ClearShell.Get())
	{
		A->SetActorLocation(Cam);
	}
}

void USSXWorldPresentation::UpdateRiders()
{
	if (!RiderMaterial)
	{
		return;
	}
	// 0x1DC3E0
	double Near = SSXRiderDepth::MinNear;
	for (TActorIterator<ASSXRaceManager> It(GetWorld()); It; ++It)
	{
		const FRaceSession* Session = It->GetSession();
		if (Session && Session->GetPhase() == ERacePhase::PreRace)
		{
			Near = SSXRiderDepth::MinNearPreRace;
		}
		break;
	}
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		const float CamNear = PC->PlayerCameraManager->GetCameraCacheView().PerspectiveNearClipPlane;
		Near = FMath::Max(Near, double(CamNear > 0.0f ? CamNear : GNearClippingPlane));
	}
	RiderDepthNear = Near;
	// 0x1DC3E0
	{
		const USSXFrontEndSubsystem* FrontEndSub = USSXFrontEndSubsystem::Get(this);
		const SSXSettings::FWidescreenView Ws = SSXSettings::WidescreenView(FrontEndSub ? FrontEndSub->Profile.Settings.Widescreen : 0);
		RiderGuard = SSXRiderDepth::GuardNdc(Ws.ViewTop, Ws.ViewHeight);
	}
	for (TActorIterator<ASnowboarderPawn> It(GetWorld()); It; ++It) { LightActor(**It); }
	for (TActorIterator<ASSXFeRiderActor> It(GetWorld()); It; ++It) { LightActor(**It); }
}

void USSXWorldPresentation::LightActor(AActor& Actor)
{
	FRiderLightInputs In;
	const ASSXFeRiderActor* FeRider = Cast<ASSXFeRiderActor>(&Actor);
	if (FeRider)
	{
		const SSXFeRiders::FLightSet* L = FeRider->GetLights();
		if (!L)
		{
			return;
		}
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const FRotator CamRot = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
		In = FeRiderLightInputs(*L, CamRot);
	}
	else
	{
		const FVector* Flash = RiderFlash.Find(&Actor);
		const FSSXRiderLight L = LightWorld.Probe(SSXSim::UnrealToGame(Actor.GetActorLocation()), Adjust,
			Flash ? *Flash : FVector::ZeroVector);
		In.Ambient = L.Ambient;
		for (int32 I = 0; I < FSSXRiderLight::Slots; ++I)
		{
			In.Colour[I] = L.Colour[I];
			In.Dir[I] = SSXSim::GameToUnreal(L.Direction[I]);
		}
	}
	TInlineComponentArray<UMeshComponent*> Meshes(&Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->IsVisible())
		{
			continue;
		}
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInstanceDynamic* Mid = WrapRiderMaterial(*Mesh, Slot))
			{
				ApplyRiderLight(*Mid, In);
				// 0x1364D0 / 0x208010
				ApplyRiderDepth(*Mid, FeRider ? 0.0 : SSXRiderDepth::PullCm, Actor.GetActorLocation(), RiderDepthNear, RiderGuard);
				if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Shown = DisplayedRider.Find(Mid))
				{
					if (UMaterialInstanceDynamic* D = Shown->Get())
					{
						static const FName Vectors[] = { TEXT("Ambient"), TEXT("Light0Colour"), TEXT("Light0Dir"), TEXT("Light1Colour"),
							TEXT("Light1Dir"), TEXT("Light2Colour"), TEXT("Light2Dir"), TEXT("RiderRoot"), TEXT("RiderGuard") };
						static const FName Scalars[] = { TEXT("RiderDepthPull"), TEXT("RiderDepthNear") };
						for (const FName& N : Vectors) { D->SetVectorParameterValue(N, Mid->K2_GetVectorParameterValue(N)); }
						for (const FName& N : Scalars) { D->SetScalarParameterValue(N, Mid->K2_GetScalarParameterValue(N)); }
					}
				}
			}
		}
	}
}

UMaterialInstanceDynamic* USSXWorldPresentation::WrapRiderMaterial(UMeshComponent& Mesh, int32 Slot)
{
	UMaterialInterface* Src = Mesh.GetMaterial(Slot);
	if (!Src)
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Classic = ClassicRider.Find(Cast<UMaterialInstanceDynamic>(Src)))
	{
		if (Classic->IsValid()) { return Classic->Get(); }
	}
	if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Src))
	{
		if (Mid->Parent == RiderMaterial || (RiderMaskedMaterial && Mid->Parent == RiderMaskedMaterial) || IsChromeParent(Mid->Parent))
		{
			return Mid;
		}
	}
	UMaterialInterface* Parent = (Src->GetBlendMode() == BLEND_Masked && RiderMaskedMaterial) ? RiderMaskedMaterial.Get() : RiderMaterial.Get();
	UMaterialInstanceDynamic* Mid = MakeRiderMid(*Src, Parent, &Mesh);
	if (Mid)
	{
		ApplyGloss(*Mid, *Src);
		Mesh.SetMaterial(Slot, Mid);
	}
	return Mid;
}

void USSXWorldPresentation::SetDisplayedRiderMaterial(UMaterialInstanceDynamic* Classic, UMaterialInstanceDynamic* Displayed)
{
	if (!Classic) { return; }
	if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Old = DisplayedRider.Find(Classic)) { ClassicRider.Remove(*Old); }
	DisplayedRider.Remove(Classic);
	if (Displayed)
	{
		DisplayedRider.Add(Classic, Displayed);
		ClassicRider.Add(Displayed, Classic);
	}
}

bool USSXWorldPresentation::IsRiderMaterial(const UMaterialInterface* M) const
{
	const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(M);
	return Mid && (Mid->Parent == RiderMaterial || (RiderMaskedMaterial && Mid->Parent == RiderMaskedMaterial) || IsChromeParent(Mid->Parent));
}

void USSXWorldPresentation::SetGlossSpecmap(int32 SpecmapIndex)
{
	GlossSpecmap = SpecmapIndex;
	auto Load = [this, SpecmapIndex](bool bEnv) -> UTexture2D*
	{
		FString& Path = GlossTexPath[bEnv ? 1 : 0];
		Path = SSXRiderGloss::SpecmapPath(SpecmapIndex, bEnv);
		if (SpecmapIndex == INDEX_NONE || !FPaths::FileExists(Path))
		{
			Path = SSXRiderGloss::FallbackPath(bEnv);
		}
		UTexture2D* Tex = FPaths::FileExists(Path) ? FImageUtils::ImportFileAsTexture2D(Path) : nullptr;
		if (!Tex)
		{
			UE_LOG(LogSSX, Warning, TEXT("[world] rider gloss map %s missing (run tools/convert_characters.py)"), *Path);
			return nullptr;
		}
		Tex->SRGB = true;
		Tex->AddressX = TA_Clamp;
		Tex->AddressY = TA_Clamp;
		Tex->UpdateResource();
		return Tex;
	};
	GlossSpecTex = Load(false);
	GlossEnvTex = Load(true);
}

void USSXWorldPresentation::ApplyGloss(UMaterialInstanceDynamic& Mid, const UMaterialInterface& Src) const
{
	if (IsChromeParent(Mid.Parent))
	{
		return;
	}
	const int32 Gloss = SSXRiderGloss::GlossOfMaterial(&Src);
	UTexture2D* Tex = GetGlossTexture(Gloss < 0);
	if (Gloss == 0 || !Tex)
	{
		return;
	}
	Mid.SetTextureParameterValue(TEXT("GlossTex"), Tex);
	Mid.SetScalarParameterValue(TEXT("GlossFix"), float(SSXRiderGloss::Fix(Gloss)) / 128.0f);
}

void USSXWorldPresentation::ApplyRiderDepth(UMaterialInstanceDynamic& Mid, double Pull, const FVector& Root, double Near,
	const FVector2D& Guard)
{
	Mid.SetScalarParameterValue(TEXT("RiderDepthPull"), float(Pull));
	Mid.SetVectorParameterValue(TEXT("RiderRoot"), FLinearColor(float(Root.X), float(Root.Y), float(Root.Z), 1.0f));
	Mid.SetScalarParameterValue(TEXT("RiderDepthNear"), float(Near));
	Mid.SetVectorParameterValue(TEXT("RiderGuard"), FLinearColor(float(Guard.X), float(Guard.Y), 0.0f, 0.0f));
}

bool USSXWorldPresentation::IsChromeParent(const UMaterialInterface* Parent)
{
	return Parent && Parent->GetPathName().StartsWith(TEXT("/Game/World/M_SSXRiderLitChrome"));
}

UMaterialInstanceDynamic* USSXWorldPresentation::MakeRiderMid(UMaterialInterface& Src, UMaterialInterface* Parent, UObject* Outer)
{
	if (!Parent)
	{
		return nullptr;
	}
	UTexture* Tex = nullptr;
	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	Src.GetAllTextureParameterInfo(Infos, Ids);
	for (int32 Pass = 0; Pass < 2 && !Tex; ++Pass)
	{
		for (const FMaterialParameterInfo& P : Infos)
		{
			const FString N = P.Name.ToString();
			if (Pass == 0 && !N.Contains(TEXT("BaseColor")) && !N.Contains(TEXT("Diffuse")))
			{
				continue;
			}
			UTexture* T = nullptr;
			if (Src.GetTextureParameterValue(P, T) && T)
			{
				Tex = T;
				break;
			}
		}
	}
	FLinearColor Tint = FLinearColor::White;
	Infos.Reset();
	Ids.Reset();
	Src.GetAllVectorParameterInfo(Infos, Ids);
	for (const FMaterialParameterInfo& P : Infos)
	{
		const FString N = P.Name.ToString();
		if (N.Contains(TEXT("BaseColor")) || N == TEXT("Color"))
		{
			Src.GetVectorParameterValue(P, Tint);
			break;
		}
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
	if (Tex)
	{
		Mid->SetTextureParameterValue(TEXT("BaseTex"), Tex);
	}
	Mid->SetVectorParameterValue(TEXT("Tint"), Tint);
	return Mid;
}

USSXWorldPresentation::FRiderLightInputs USSXWorldPresentation::FeRiderLightInputs(const SSXFeRiders::FLightSet& L, const FRotator& CamRot)
{
	FRiderLightInputs In;
	const FRotationMatrix M(CamRot);
	const FVector Fwd = M.GetUnitAxis(EAxis::X), Right = M.GetUnitAxis(EAxis::Y), Up = M.GetUnitAxis(EAxis::Z);
	In.Ambient = FVector(L.Ambient[1], L.Ambient[2], L.Ambient[3]);
	for (int32 I = 0; I < FRiderLightInputs::Slots; ++I)
	{
		const FVector D(L.Direction[I]);
		In.Colour[I] = FVector(L.Colour[I][1], L.Colour[I][2], L.Colour[I][3]);
		In.Dir[I] = Right * D.X + Up * D.Y + Fwd * D.Z;
	}
	return In;
}

FVector USSXWorldPresentation::RiderVertexColour(const FRiderLightInputs& In, const FVector& N)
{
	// 0x318080
	FVector V = In.Ambient;
	for (int32 I = 0; I < FRiderLightInputs::Slots; ++I)
	{
		V += FMath::Max(0.0, FVector::DotProduct(N, In.Dir[I].GetSafeNormal())) * In.Colour[I];
	}
	const double Max = 255.0 / 128.0;
	return FVector(FMath::Clamp(V.X, 0.0, Max), FMath::Clamp(V.Y, 0.0, Max), FMath::Clamp(V.Z, 0.0, Max));
}

void USSXWorldPresentation::ApplyRiderLight(UMaterialInstanceDynamic& Mid, const FRiderLightInputs& In)
{
	static const FName Colours[] = { TEXT("Light0Colour"), TEXT("Light1Colour"), TEXT("Light2Colour") };
	static const FName Dirs[] = { TEXT("Light0Dir"), TEXT("Light1Dir"), TEXT("Light2Dir") };
	Mid.SetVectorParameterValue(TEXT("Ambient"), ToColour(In.Ambient));
	for (int32 I = 0; I < FRiderLightInputs::Slots; ++I)
	{
		Mid.SetVectorParameterValue(Colours[I], ToColour(In.Colour[I]));
		Mid.SetVectorParameterValue(Dirs[I], ToColour(In.Dir[I]));
	}
}
