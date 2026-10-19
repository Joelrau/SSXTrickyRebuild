#include "Particles/SSXParticleWorld.h"

#include "Race/GameRng.h"
#include "World/SSXRemasterWorld.h"
#include "World/SSXRenderLook.h"

#include "Camera/PlayerCameraManager.h"
#include "Characters/SSXCharacterAppearance.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Effects/SSXTrackEffects.h"
#include "Effects/TrackEffects.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Camera/RaceCamera.h"
#include "Particles/SSXLensFX.h"
#include "Particles/SSXSpriteRenderer.h"
#include "Race/SSXRaceManager.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/RiderSim.h"
#include "Sim/SimCoords.h"
#include "Sim/SurfaceTable.h"
#include "SnowboarderPawn.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFrontEndData.h"
#include "World/SSXWorldPresentation.h"

using namespace SSXParticles;

namespace
{
	TAutoConsoleVariable<int32> CVarParticles(TEXT("ssx.Particles.Enable"), 1,
		TEXT("Particle and rider effects: 0 = off."));
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarShadowMode(TEXT("ssx.Shadow.Mode"), 0,
		TEXT("Rider shadow projection: 0 = subtractive, GS ALPHA mode 7 Cd - Cs As clamped (default), "
			 "1 = multiply by (1 - grey) (diagnostic), -1 = off."));
	TAutoConsoleVariable<int32> CVarSnowForce(TEXT("ssx.Snowfall.Force"), 0,
		TEXT("Dev aid: 1 forces the snowfall visual on (draws 0.5, 0.5, 0.5) to check it on tracks where the rule "
			 "keeps it off."));
	TAutoConsoleVariable<int32> CVarPreview(TEXT("ssx.Particles.Preview"), -1,
		TEXT("Dev aid: replay the first emitter record of this track effect index 25 m in front of and 6 m above the camera (e.g. 120 = the "
			 "Garibaldi firework fountain), restarting when it ends; -1 = off."));
#endif
	TAutoConsoleVariable<int32> CVarFog(TEXT("ssx.Fog.Enable"), 1,
		TEXT("Draw the fog-volume billboards of the map (0 = off, for comparison)."));
	TAutoConsoleVariable<int32> CVarLens(TEXT("ssx.LensFX.Enable"), 1,
		TEXT("Light halos and the sun flare: 0 = off, for comparison."));
	TAutoConsoleVariable<int32> CVarTrails(TEXT("ssx.Particles.Trails"), 1,
		TEXT("Boost ribbons and the ground trail: 0 = off."));

	FColor GsColor(const FVector4& Rgba)
	{
		auto B = [](double V) { return uint8(FMath::Clamp(FMath::RoundToInt32(V), 0, 255)); };
		return FColor(B(Rgba.X), B(Rgba.Y), B(Rgba.Z), B(Rgba.W));
	}

	FVector4 ArgbGs(const FVector4& K) { return FVector4(K.Y * 128.0, K.Z * 128.0, K.W * 128.0, K.X * 128.0); }

	int32 BaseIndexFor(int32 Mode)
	{
		switch (Mode)
		{
		case BlendAdditive: return 0;
		case BlendDarken: return 2;
		case BlendBrighten: return 3;
		case BlendDarken | USSXParticleWorld::TrailLayerFlag: return 4;
		case BlendBrighten | USSXParticleWorld::TrailLayerFlag: return 5;
		default: return 1;
		}
	}
}

USSXParticleWorld* USSXParticleWorld::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<USSXParticleWorld>() : nullptr;
}

bool USSXParticleWorld::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

TStatId USSXParticleWorld::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USSXParticleWorld, STATGROUP_Tickables);
}

void USSXParticleWorld::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	for (const TCHAR* Path : { AddMaterialPath, AlphaMaterialPath, DarkenMaterialPath })
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!M)
		{
			UE_LOG(LogSSX, Warning, TEXT("[particles] %s missing (Scripts/make_particle_materials.py); particles off"), Path);
			return;
		}
		BaseMaterials.Add(M);
	}
	for (const TCHAR* Path : { BrightenMaterialPath, TrailDarkenMaterialPath, TrailBrightenMaterialPath })
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!M)
		{
			UE_LOG(LogSSX, Warning, TEXT("[particles] %s missing (Scripts/make_particle_materials.py): %s"), Path,
				Path == BrightenMaterialPath ? TEXT("the ground trail's lit wall is not drawn")
				: TEXT("the ground trail falls back to the plain sprite material and can cover the board"));
		}
		BaseMaterials.Add(M);
	}
	ShadowMaterial = LoadObject<UMaterialInterface>(nullptr, ShadowMaterialPath);
	if (UMaterialInterface* LensSprite = LoadObject<UMaterialInterface>(nullptr, LensSpriteMaterialPath))
	{
		if (UTexture2D* Lens = Texture(SSXLensFX::LensHandle))
		{
			LensSpriteMid = UMaterialInstanceDynamic::Create(LensSprite, this);
			LensSpriteMid->SetTextureParameterValue(TEXT("Tex"), Lens);
		}
		BaseMaterials.Add(LensSprite);
	}
	LensFanMaterial = LoadObject<UMaterialInterface>(nullptr, LensFanMaterialPath);
	if (LensFanMaterial) { BaseMaterials.Add(LensFanMaterial); }
	if (!LensSpriteMid || !LensFanMaterial)
	{
		UE_LOG(LogSSX, Warning, TEXT("[particles] lens FX materials missing (Scripts/make_particle_materials.py): no halos / sun flare"));
	}
	{
		FString Stem = UWorld::RemovePIEPrefix(InWorld.GetMapName()).ToLower();
		if (Stem == TEXT("frontend")) { Stem = TEXT("ssxfe"); }
		bFogVisibility = Stem != TEXT("ssxfe");
		FString Err;
		if (Fog.Load(SSXFog::FFogData::DefaultPath(Stem), &Err))
		{
			UE_LOG(LogSSX, Display, TEXT("[particles] fog volumes %s: %d instances, %d puffs"), *Stem, Fog.Instances.Num(), Fog.NumPuffs());
		}
		else
		{
			UE_LOG(LogSSX, Log, TEXT("[particles] fog volumes: %s"), *Err);
		}
	}
	FActorSpawnParameters P;
	P.ObjectFlags |= RF_Transient;
	P.Name = TEXT("SSXParticles");
	RenderActor = InWorld.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	if (!RenderActor)
	{
		return;
	}
	Renderer = NewObject<USSXSpriteRenderComponent>(RenderActor, TEXT("Sprites"));
	TArray<UMaterialInterface*> Bases;
	for (UMaterialInterface* M : BaseMaterials) { if (M) { Bases.Add(M); } }
	Renderer->SetBaseMaterials(Bases);
	Renderer->SetTranslucentSortPriority(SpriteSortPriority);
	RenderActor->SetRootComponent(Renderer);
	Renderer->RegisterComponent();
	bActive = true;
}

void USSXParticleWorld::AddTrackEmitter(const SSXEffects::FEffectData& Data, int32 Inst, int32 Kind, int32 Record, const FRiderSim* Rider)
{
	if (!Data.Records.IsValidIndex(Record) || (Kind != 0 && Kind != 2))
	{
		return;
	}
	const SSXEffects::FRecord& R = Data.Records[Record];
	uint32 Words[51];
	for (int32 K = 0; K < 51; ++K) { Words[K] = uint32(Data.I(R, K + 1)); }
	const FEmitterConfig C = FEmitterConfig::FromWords(Words);
	if (C.Count <= 0 || C.TimeScale <= 0.0)
	{
		return;
	}
	FTrackEmitter E;
	E.Inst = Inst;
	E.Kind = Kind;
	E.Record = Record;
	auto Draw = []() { return double(FGameRng::StreamA().Uniform(0.f, 1.f)); };   // 0x1D5180
	if (Kind == 2)
	{
		// TODO: node +0x30 / +0x40 are probably the contact point and velocity;
		const FRiderState* S = Rider ? &Rider->GetState() : nullptr;
		const FVector Origin = S ? S->ContactPoint : C.Origin;
		const FVector Vel = S ? S->Velocity : C.Vel;
		E.Burst = FParticleBurst::FromEmitter(C, FInstanceMatrix(), Draw, &Origin, &Vel);
	}
	else
	{
		const FInstanceMatrix M = Data.InstMatrix.IsValidIndex(Inst) ? FInstanceMatrix::FromMatrix(Data.InstMatrix[Inst]) : FInstanceMatrix();
		E.Burst = FParticleBurst::FromEmitter(C, M, Draw);
	}
	Emitters.Add(MoveTemp(E));
}

void USSXParticleWorld::RemoveTrackEmitter(int32 Inst, int32 Kind, int32 Record)
{
	const int32 K = Emitters.IndexOfByPredicate([&](const FTrackEmitter& E) { return E.Inst == Inst && E.Kind == Kind && E.Record == Record; });
	if (K != INDEX_NONE)
	{
		Emitters.RemoveAt(K);
	}
}

void USSXParticleWorld::OnLaunchBoost(const FRiderSim* Sim)
{
	SyncRiders();
	if (FRiderFx* R = FindRider(Sim))
	{
		R->bLaunch = true;
	}
}

void USSXParticleWorld::StartSnowfall(double X, double R1, double R2, const TArray<double>* Seeds)
{
	// 0x1C8EC8
	int32 Next = 0;
	Snow.Init(X, R1, R2, [Seeds, &Next]()
		{ return Seeds && Seeds->IsValidIndex(Next) ? (*Seeds)[Next++] : double(FGameRng::StreamA().Uniform(0.f, 1.f)); });
	bSnow = true;
	UE_LOG(LogSSX, Display, TEXT("[particles] snowfall: intensity %.3f, %d flakes per slot"), Snow.GetIntensity(),
		FSnowfall::FlakesPerSlot(Snow.GetIntensity()));
}

void USSXParticleWorld::SyncRiders()
{
	Riders.RemoveAll([](const FRiderFx& R) { return !R.Pawn.IsValid(); });
	for (TActorIterator<ASnowboarderPawn> It(GetWorld()); It; ++It)
	{
		if (!It->HasSim() || Riders.ContainsByPredicate([&](const FRiderFx& R) { return R.Pawn.Get() == *It; }))
		{
			continue;
		}
		FRiderFx& R = Riders.AddDefaulted_GetRef();
		R.Pawn = *It;
	}
}

void USSXParticleWorld::ResetRiderEffects(const ASnowboarderPawn* Only)
{
	// 0x1128B8 / 0x136EA0 / 0x135200
	++RiderEffectResets;
	for (FRiderFx& R : Riders)
	{
		if (Only && R.Pawn.Get() != Only) continue;
		R.Clouds.ClearRing();   // 0x12EC98
		R.Spray.Reset();        // 0x131160
		R.Trail.Reset();        // 0x132518
		R.Puffs.Reset();        // 0x12DFD0 / 0x12C178
		R.Curtain.Reset();      // 0x12FED8
		R.Glints.Reset();       // 0x12F4B0
		R.Ribbons.Reset();
		R.Spin.Reset();
	}
}

int32 USSXParticleWorld::GetSprayBurstCount(const ASnowboarderPawn* Pawn) const
{
	const FRiderFx* R = Riders.FindByPredicate([&](const FRiderFx& X) { return X.Pawn.Get() == Pawn; });
	return R ? R->SprayBursts : 0;
}

bool USSXParticleWorld::IsShadowActive(const ASnowboarderPawn* Pawn) const
{
	const FRiderFx* R = Riders.FindByPredicate([&](const FRiderFx& X) { return X.Pawn.Get() == Pawn; });
	return R && R->Capture.IsValid() && R->Capture->bCaptureEveryFrame && R->Projector.IsValid() && R->Projector->IsVisible();
}

USSXParticleWorld::FRiderFx* USSXParticleWorld::FindRider(const FRiderSim* Sim)
{
	return Riders.FindByPredicate([&](const FRiderFx& R) { return R.Pawn.IsValid() && R.Pawn->GetSim() == Sim; });
}

bool USSXParticleWorld::UpdatePoseBones(const ASnowboarderPawn& Pawn, FVector Out[19])
{
	static_assert(FRiderState::FxPoseBoneCount == 19, "UpdatePoseBones array size");
	// 0x129628
	const FRiderState* S = Pawn.GetSimState();
	if (!S)
	{
		return false;
	}
	const FVector* Src = S->bFxPoseBonesValid ? S->FxPoseBones : S->bPoseBonesValid ? S->PoseBones : nullptr;
	if (!Src)
	{
		return false;
	}
	for (int32 K = 0; K < FRiderState::FxPoseBoneCount; ++K) { Out[K] = Src[K]; }
	return true;
}

bool USSXParticleWorld::BuildInput(const ASnowboarderPawn& Pawn, FRiderFxInput& In) const
{
	const FRiderState* S = Pawn.GetSimState();
	if (!S)
	{
		return false;
	}
	In.Motion = int32(S->Motion);
	In.ContactKind = S->ContactKind;
	In.Surface = S->SurfaceId;
	In.Steer = S->Steer.Cur;
	In.bGoofy = S->bStanceGoofy;
	In.Brake = S->Brake.Cur;
	In.Vel = S->Velocity;
	In.Contact = S->ContactPoint;
	In.Fwd = S->BasisForward;
	In.Right = S->BasisSide;
	In.Up = S->BasisUp;
	// 0x129628
	if (S->bFxBoardValid)
	{
		In.BoardLong = S->FxBoardRow0;
		In.BoardUp = S->FxBoardRow1;
		In.BoardAcross = S->FxBoardRow2;
		In.BoardOrigin = S->FxBoardOrigin;
	}
	else if (S->bBoardValid)
	{
		In.BoardLong = S->BoardRow0;
		In.BoardUp = S->BoardRow1;
		In.BoardAcross = S->BoardRow2;
		In.BoardOrigin = S->BoardOrigin;
	}
	else
	{
		In.BoardLong = S->BasisForward;
		In.BoardUp = S->BasisUp;
		In.BoardAcross = S->BasisSide;
		In.BoardOrigin = S->Position;
	}
	In.LiveBoardLong = S->bBoardValid ? S->BoardRow0 : In.BoardLong;
	In.LiveBoardAcross = S->bBoardValid ? S->BoardRow2 : In.BoardAcross;
	In.LiveBoardOrigin = S->bBoardValid ? S->BoardOrigin : In.BoardOrigin;
	In.Height = S->GroundHeight;
	In.BoostStrength = S->BoostStrength;
	In.BurstTimer = S->BoostBurstTimer;
	In.LightTotal = FVector4(1.0, 0.5, 0.5, 0.5);
	if (const USSXWorldPresentation* WP = GetWorld()->GetSubsystem<USSXWorldPresentation>())
	{
		if (WP->GetLightWorld().IsLoaded())
		{
			const FRiderFx* R = Riders.FindByPredicate([&](const FRiderFx& X) { return X.Pawn.Get() == &Pawn; });
			const FVector4 F = R ? R->Spray.GetFlash() : FVector4(0, 0, 0, 0);
			const FSSXRiderLight L = WP->GetLightWorld().Probe(S->Position, WP->GetRiderLightAdjust(), FVector(F.Y, F.Z, F.W));
			In.LightTotal = FVector4(1.0, L.Total.X, L.Total.Y, L.Total.Z);
			// TODO: which colour channel(s) the comparison reads;
			auto Lit = [&L](const FVector& D)
			{
				double V = L.Ambient.X + L.Ambient.Y + L.Ambient.Z;
				for (int32 K = 0; K < FSSXRiderLight::Slots; ++K)
				{
					V += (L.Colour[K].X + L.Colour[K].Y + L.Colour[K].Z) * FVector::DotProduct(D, L.Direction[K]);
				}
				return V;
			};
			// 0x132648 / 0x1325B8
			const FVector TrailD = TrailAcross(S->Velocity, S->GroundNormal);
			In.TrailLitA = Lit(TrailD);
			In.TrailLitB = Lit(-TrailD);
		}
	}
	In.GroundNormal = S->GroundNormal;
	In.Position = S->Position;
	In.ContactForward = S->ContactForward;
	In.ContactLeft = S->ContactLeft;
	In.Control = int32(S->Control);
	In.BoardClass = S->BoardClass;
	if (const FRiderSim* Sim = Pawn.GetSim())
	{
		In.ProfileStance = Sim->Profile.Stance;
		In.SizeScale = Sim->Profile.SizeScale;
		const FSurfaceParams& Sp = Sim->Surface(In.Surface);
		In.SurfWidth = Sp.F[22];
		In.SurfTilt = Sp.F[23];
		In.SurfSpeed = Sp.F[24];
	}
	return true;
}

void USSXParticleWorld::StepRider(FRiderFx& R, int32 FrameNo)
{
	ASnowboarderPawn* Pawn = R.Pawn.Get();
	FRiderFxInput In;
	if (!Pawn || !BuildInput(*Pawn, In))
	{
		return;
	}
	const FRiderSim* Sim = Pawn->GetSim();
	R.RenderAlpha = Pawn->IsHidden() ? 0.0 : Pawn->GetSimState()->RenderAlpha;
	if ((R.LastMotion == MotionAir && In.Motion != MotionAir) || In.Motion == 0)
	{
		R.bLaunch = false;
	}
	In.bLaunchFlag = R.bLaunch;
	// 0x12C190 / 0x1311B0
	auto U = []() { return double(FGameRng::StreamA().Uniform(0.f, 1.f)); };
	auto Bits = []() { return FGameRng::StreamA().Next(); };
	R.SprayBursts += R.Spray.Frame(In, FSurfaceSpray::FromParams(Sim->Surface(In.Surface)), U) > 0 ? 1 : 0;
	R.Clouds.Frame(In, Bits);
	R.Curtain.Frame(In, U);
	for (const FRiderState::FFxImpact& Im : Pawn->GetSimState()->FxImpacts)
	{
		if (Im.Serial > R.LastImpactSerial)
		{
			R.Puffs.Impact(Im.Point, Im.Velocity, Im.Normal, In, U, Bits);
			R.LastImpactSerial = Im.Serial;
		}
	}
	if (R.LastMotion == MotionAir && In.Motion == MotionGround)
	{
		// 0x136E90
		R.Puffs.Land(Pawn->GetSimState()->LastImpact.Speed, In, U, Bits);
	}
	R.Puffs.Frame(In, U, Bits);
	R.Glints.Frame(In, U, Bits);
	// 0x136E38 / 0x1364D0
	FSpinSample SpinSample;
	BoardCorners(In, SpinSample.P);
	R.Spin.Frame(In.Control == FSpinRibbons::ControlSpin, SpinSample);
	LiveBoardCorners(In, R.LiveSpin.P);
	// TODO: pose base = palette bone 0 assumed.
	if (R.bLaunch)
	{
		// 0x1D9208 / 0x1356F8
		FVector Bn[FRiderState::FxPoseBoneCount];
		const bool bBones = UpdatePoseBones(*Pawn, Bn);
		int32 Free = PowerFxPool;
		for (const FRiderFx& X : Riders) { Free -= X.Power[0].GetParts().Num() + X.Power[1].GetParts().Num(); }
		const int32 Pairs[2][2] = { { 11, 12 }, { 7, 8 } };
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			FVector P = In.BoardOrigin + In.Up * 100.0;
			if (bBones)
			{
				P = FPowerFxSlot::EmitPoint(Bn[Pairs[Slot][0]], Bn[Pairs[Slot][1]]);
			}
			R.Power[Slot].Frame(P, In.Vel / 60.0, Free);
		}
		R.bPowerSlots = true;
	}
	else if (R.bPowerSlots)
	{
		R.Power[0] = FPowerFxSlot();
		R.Power[1] = FPowerFxSlot();
		R.bPowerSlots = false;
	}
	StepRibbons(R, In, FrameNo);
	if (In.Motion == 5 || In.Motion == 6)
	{
		FVector Bn[FRiderState::FxPoseBoneCount];
		if (UpdatePoseBones(*Pawn, Bn))
		{
			In.bTrailBones = true;
			In.TrailBone12 = Bn[12];
			In.TrailBone8 = Bn[8];
		}
	}
	R.Trail.Frame(In, In.TrailLitA, In.TrailLitB);
	if (USSXWorldPresentation* WP = GetWorld()->GetSubsystem<USSXWorldPresentation>())
	{
		const FVector4 F = R.Spray.GetFlash();
		WP->SetRiderFlash(Pawn, FVector(F.Y, F.Z, F.W));
	}
	StepAngerMarker(R, *Pawn);
	R.LastMotion = In.Motion;
	R.LastInput = In;
	R.bHaveInput = true;
}

// 0x1343D0
void USSXParticleWorld::StepAngerMarker(FRiderFx& R, const ASnowboarderPawn& Pawn)
{
	FAngerMarkerInput A;
	const ASSXRaceManager* Race = ASSXRaceManager::Find(this);
	const FRaceSession* S = Race ? Race->GetSession() : nullptr;
	const FRiderState* St = Pawn.GetSimState();
	// 0x129628 / 0x1343D0
	const FVector PrevUp = St ? R.AngerUp.Read(St->Frame, St->RenderRow2) : FVector(0, 0, 1);
	if (S && St)
	{
		const ERaceEventMode Mode = S->GetMode();
		const int32 Replay = Race->GetReplay().GetState();
		const bool bGate = (Mode == ERaceEventMode::PracticeRace || Mode == ERaceEventMode::CircuitRace) && !(Replay >= 1 && Replay <= 9)
			&& S->GetRequestedPhase() == ERacePhase::Race;
		int32 Me = -1, Human = -1;
		for (int32 I = 0; I < S->NumRacers(); ++I)
		{
			const FRacerRecord& Rec = S->GetRacer(I);
			if (Rec.Sim && Rec.Sim == Pawn.GetSim()) { Me = I; }
			if (Rec.bHuman && Human < 0) { Human = I; }   // 0x166598
		}
		FVector Bn[FRiderState::FxPoseBoneCount];
		if (bGate && Me >= 0 && Human >= 0 && !S->GetRacer(Me).bHuman && UpdatePoseBones(Pawn, Bn))
		{
			const FRacerRecord& Rec = S->GetRacer(Me);
			A.bShown = true;
			A.Level = Rec.Rivals[Human].Level;
			A.Anger = Rec.Rivals[Human].Anger;
			A.bGrudge = Rec.Grudge.bActive;
			A.GrudgeRival = Rec.Grudge.Rival;
			A.GrudgeKind = Rec.Grudge.Kind;
			A.Human = Human;
			A.Bone4 = Bn[4];
			A.Up = PrevUp;
		}
	}
	R.Anger.Frame(A);
}

void USSXParticleWorld::StepFrame()
{
	++FrameCounter;
	for (int32 K = Emitters.Num() - 1; K >= 0; --K)
	{
		Emitters[K].Burst.Update();
		if (Emitters[K].Burst.IsFinished())
		{
			Emitters.RemoveAt(K);
		}
	}
	SyncRiders();
	for (FRiderFx& R : Riders) { StepRider(R, FrameCounter); }
	if (bSnow)
	{
		Snow.Frame();
	}
}

void USSXParticleWorld::Tick(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (CVarSnowForce.GetValueOnGameThread() != 0 && !bSnow)
	{
		StartSnowfall(0.5, 0.5, 0.5);
	}
	if (const int32 Fx = CVarPreview.GetValueOnGameThread(); Fx >= 0 && FrameCounter > 120 && !Emitters.ContainsByPredicate([](const FTrackEmitter& E) { return E.Inst == -2; }))
	{
		const USSXTrackEffects* Track = USSXTrackEffects::Get(this);
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const SSXEffects::FEffectData* D = Track ? &Track->GetData() : nullptr;
		if (D && D->Effects.IsValidIndex(Fx) && PC && PC->PlayerCameraManager)
		{
			for (int32 K = 0; K < D->Effects[Fx].Y; ++K)
			{
				const int32 Rec = D->Effects[Fx].X + K;
				if (D->Records[Rec].Type == 2 && D->I(D->Records[Rec], 0) == 0)
				{
					const FVector P = PC->PlayerCameraManager->GetCameraLocation() + PC->PlayerCameraManager->GetActorForwardVector() * 2500.0 + FVector(0, 0, 600.0);
					AddTrackEmitter(*D, INDEX_NONE, 0, Rec, nullptr);
					if (Emitters.Num())
					{
						FTrackEmitter& E = Emitters.Last();
						E.Inst = -2;
						E.Burst.P0 += SSXSim::UnrealToGame(P);
					}
					break;
				}
			}
		}
	}
#endif
	Accum = FMath::Min(Accum + double(DeltaTime), 4.0 / 60.0);
	while (Accum >= Frame)
	{
		Accum -= Frame;
		StepFrame();
	}
	Render();
}

UTexture2D* USSXParticleWorld::Texture(int32 Handle, bool bRepeat)
{
	// 0x1CAEF8
	const int32 Image = SSXParticles::ImageOfHandle(Handle);
	if (Image == INDEX_NONE)
	{
		return nullptr;
	}
	const int32 TexKey = bRepeat ? Image + 0x10000 : Image;
	if (TObjectPtr<UTexture2D>* T = Textures.Find(TexKey))
	{
		return *T;
	}
	if (MissingTextures.Contains(Image))
	{
		return nullptr;
	}
	const FString Dir = FPaths::Combine(SSXFrontEnd::TextureDir(), TEXT("PARTICLE"));
	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, FString::Printf(TEXT("%03d_*.png"), Image)), true, false);
	UTexture2D* Tex = Found.Num() ? FImageUtils::ImportFileAsTexture2D(FPaths::Combine(Dir, Found[0])) : nullptr;
	if (!Tex)
	{
		MissingTextures.Add(Image);
		UE_LOG(LogSSX, Warning, TEXT("[particles] PARTICLE.SSH image %d not found in %s (tools/sshtex.py)"), Image, *Dir);
		return nullptr;
	}
	ConfigureSpriteTexture(Tex);
	if (bRepeat)
	{
		Tex->AddressX = TA_Wrap;
		Tex->AddressY = TA_Wrap;
		Tex->UpdateResource();
	}
	Textures.Add(TexKey, Tex);
	return Tex;
}

void USSXParticleWorld::ConfigureSpriteTexture(UTexture2D* Tex)
{
	Tex->SRGB = false;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	Tex->UpdateResource();
}

UMaterialInterface* USSXParticleWorld::SpriteMaterial(int32 Handle, int32 BlendMode)
{
	const bool bRepeat = (BlendMode & RepeatFlag) != 0;
	BlendMode &= ~RepeatFlag;
	int32 Base = BaseIndexFor(BlendMode);
	if ((BlendMode & TrailLayerFlag) && !(BaseMaterials.IsValidIndex(Base) && BaseMaterials[Base]))
	{
		Base = BaseIndexFor(BlendMode & ~TrailLayerFlag);
	}
	UMaterialInterface* Parent = BaseMaterials.IsValidIndex(Base) ? BaseMaterials[Base].Get() : nullptr;
	bool bRemastered = false;
	if (SSXRenderLook::Get() == ESSXRenderLook::Remastered && (Base == 0 || Base == 1))
	{
		static TWeakObjectPtr<UMaterialInterface> Loaded[2];
		if (!Loaded[Base].IsValid())
		{
			Loaded[Base] = LoadObject<UMaterialInterface>(nullptr, Base == 0 ? RemasteredAddMaterialPath : RemasteredAlphaMaterialPath);
		}
		if (UMaterialInterface* R = Loaded[Base].Get())
		{
			Parent = R;
			bRemastered = true;
			Keep.AddUnique(R);
		}
	}
	const int32 Key = (Handle * 16 + Base) | (bRepeat ? 0x1000000 : 0) | (bRemastered ? 0x2000000 : 0);
	if (TObjectPtr<UMaterialInstanceDynamic>* M = SpriteMids.Find(Key))
	{
		return *M;
	}
	UTexture2D* Tex = Texture(Handle, bRepeat);
	if (!Tex || !Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this);
	Mid->SetTextureParameterValue(TEXT("Tex"), Tex);
	SpriteMids.Add(Key, Mid);
	return Mid;
}

namespace
{
	struct FBatcher
	{
		TArray<FSSXSpriteBatch>& Batches;
		TMap<UMaterialInterface*, int32> Index;

		FSSXSpriteBatch* For(UMaterialInterface* M)
		{
			if (!M)
			{
				return nullptr;
			}
			if (const int32* K = Index.Find(M))
			{
				return &Batches[*K];
			}
			const int32 K = Batches.AddDefaulted();
			Batches[K].Material = M;
			Index.Add(M, K);
			return &Batches[K];
		}
	};

	void PushSprite(FSSXSpriteBatch& B, const FVector& GamePos, double Half, const FColor& C, const FVector4f& UV = FVector4f(0, 0, 1, 1))
	{
		FSSXSpriteVertexData& V = B.Sprites.AddDefaulted_GetRef();
		V.Pos = FVector3f(SSXSim::GameToUnreal(GamePos));
		V.Half = float(Half);
		V.Color = C;
		V.UV = UV;
	}
}

void USSXParticleWorld::DrawTrail(FBatchFor For, const FGroundTrail& T)
{
	// TODO: layer-3 props also cover the trail in the original;
	if (T.Num() < 2)
	{
		return;
	}
	// 0x1333C0
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	FVector Offset = FVector::ZeroVector;
	if (PC && PC->PlayerCameraManager)
	{
		const FRotationMatrix CamM(PC->PlayerCameraManager->GetCameraRotation());
		Offset = FGroundTrail::DrawOffset(CamM.GetUnitAxis(EAxis::Y), CamM.GetUnitAxis(EAxis::Z), CamM.GetUnitAxis(EAxis::X));
	}
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		FSSXSpriteBatch* Out = For(FGroundTrail::Texture, (Pass == 0 ? BlendDarken : BlendBrighten) | TrailLayerFlag);
		if (!Out)
		{
			continue;
		}
		TArray<TArray<FTrailVertex>> Strips;
		TrailStrips(T, Pass == 1, Offset, Strips);
		for (const TArray<FTrailVertex>& S : Strips)
		{
			TArray<FSSXStripVertex>& Strip = Out->Strips.AddDefaulted_GetRef();
			for (const FTrailVertex& V : S)
			{
				FSSXStripVertex W;
				W.Pos = FVector3f(SSXSim::GameToUnreal(V.Pos));
				W.Color = FColor(128, 128, 128, uint8(FMath::Clamp(V.Alpha, 0, 255)));
				W.UV = FVector2f(V.U, V.T);
				Strip.Add(W);
			}
		}
	}
}

void USSXParticleWorld::DrawRibbons(FBatchFor For, const FRiderFx& R)
{
	if (R.Spin.IsDrawn())
	{
		if (FSSXSpriteBatch* Out = For(29, BlendAlpha))
		{
			const int32 N = R.Spin.GetCount();
			const FVector T = FSpinRibbons::Tint * 128.0;
			for (int32 Rb = 0; Rb < 2; ++Rb)
			{
				TArray<FSSXStripVertex>& Strip = Out->Strips.AddDefaulted_GetRef();
				for (int32 K = 0; K <= N; ++K)
				{
					const FSpinSample& S = K == 0 ? R.LiveSpin : R.Spin.Get(K - 1);
					const float V = float(K) / float(FMath::Max(N, 1));
					FSSXStripVertex Vo, Vi;
					Vo.Pos = FVector3f(SSXSim::GameToUnreal(S.P[2 * Rb])); Vo.Color = GsColor(FVector4(T.X, T.Y, T.Z, FBoostRibbons::OuterAlpha(N, K) * 128.0)); Vo.UV = FVector2f(0.f, V);
					Vi.Pos = FVector3f(SSXSim::GameToUnreal(S.P[2 * Rb + 1])); Vi.Color = GsColor(FVector4(T.X, T.Y, T.Z, 0.0)); Vi.UV = FVector2f(1.f, V);
					Strip.Add(Vo);
					Strip.Add(Vi);
				}
			}
		}
	}
	const int32 Count = R.Ribbons.GetCount();
	if (!R.Ribbons.IsDrawn() || !R.bHaveLiveRibbon)
	{
		return;
	}
	FSSXSpriteBatch* Out = For(29, BlendAlpha);
	if (!Out)
	{
		return;
	}
	for (int32 Arm = 0; Arm < 2; ++Arm)
	{
		TArray<FSSXStripVertex>& Strip = Out->Strips.AddDefaulted_GetRef();
		for (int32 K = 0; K <= Count; ++K)
		{
			const FRibbonSample& S = K == 0 ? R.LiveRibbon : R.Ribbons.Get(K - 1);
			const FColor C = GsColor(FVector4(S.Tint.Y * 128.0, S.Tint.Z * 128.0, S.Tint.W * 128.0, FBoostRibbons::OuterAlpha(Count, K) * 128.0));
			const FColor Ci = GsColor(FVector4(S.Tint.Y * 128.0, S.Tint.Z * 128.0, S.Tint.W * 128.0, S.Tint.X * 128.0));
			// 0x1F08D0
			const float V = float(K) / float(FMath::Max(Count, 1));
			FSSXStripVertex Vo, Vi;
			Vo.Pos = FVector3f(SSXSim::GameToUnreal(S.Outer[Arm])); Vo.Color = C; Vo.UV = FVector2f(0.f, V);
			Vi.Pos = FVector3f(SSXSim::GameToUnreal(S.Inner[Arm])); Vi.Color = Ci; Vi.UV = FVector2f(1.f, V);
			Strip.Add(Vo);
			Strip.Add(Vi);
		}
	}
}

void USSXParticleWorld::Render()
{
	if (!Renderer)
	{
		return;
	}
	TArray<FSSXSpriteBatch> Batches;
	FBatcher Bt{ Batches };
	const bool bOn = CVarParticles.GetValueOnGameThread() != 0;
	auto Burst = [&](const FParticleBurst& B)
	{
		FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(B.Texture, B.BlendMode));
		if (!Out)
		{
			return;
		}
		Scratch.Reset();
		B.Evaluate(Scratch);
		for (const FSprite& S : Scratch) { PushSprite(*Out, S.Pos, S.HalfSize, GsColor(S.Rgba)); }
	};
	if (bOn)
	{
		for (const FTrackEmitter& E : Emitters) { Burst(E.Burst); }
		int32 Racers = 0;
		for (const FRiderFx& R : Riders) { Racers += R.Pawn.IsValid() ? 1 : 0; }
		for (FRiderFx& R : Riders)
		{
			UpdateShadow(R, Racers);
			if (!R.bHaveInput || R.RenderAlpha <= 0.0)
			{
				continue;
			}
			auto Strip = [](FSSXSpriteBatch& B, const TArray<FFxVertex>& Src)
			{
				TArray<FSSXStripVertex>& Dst = B.Strips.AddDefaulted_GetRef();
				Dst.Reserve(Src.Num());
				for (const FFxVertex& V : Src)
				{
					FSSXStripVertex& O = Dst.AddDefaulted_GetRef();
					O.Pos = FVector3f(SSXSim::GameToUnreal(V.Pos));
					O.Color = V.Color;
					O.UV = FVector2f(V.UV);
				}
			};
			auto Uv = [](int32 FlipU, int32 FlipV) { return FVector4f(float(FlipU), float(FlipV), float(1 - FlipU), float(1 - FlipV)); };
			auto Bytes = [](double V) { return uint8(FMath::Clamp(int32(V), 0, 255)); };
			for (const FCarveCloud& C : R.Clouds.Get())
			{
				FCloudSprite S;
				if (!CarveCloudSprite(C, S))
				{
					continue;
				}
				if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(S.Texture, BlendAdditive)))
				{
					const FVector4f UV(C.FlipU ? 1.f : 0.f, C.FlipV ? 1.f : 0.f, C.FlipU ? 0.f : 1.f, C.FlipV ? 0.f : 1.f);
					// 0x12F178
					PushSprite(*Out, S.Pos, S.Size, GsColor(FVector4(C.Rgb.X * 128.0, C.Rgb.Y * 128.0, C.Rgb.Z * 128.0, S.Alpha * 128.0)), UV);
				}
			}
			if (R.Curtain.Num() > 0)
			{
				if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(3, BlendAdditive | RepeatFlag)))
				{
					TArray<TArray<FFxVertex>> Strips;
					R.Curtain.Strips(Strips);
					for (const TArray<FFxVertex>& S : Strips) { Strip(*Out, S); }
				}
			}
			for (const FParticleBurst& B : R.Spray.GetBursts()) { Burst(B); }
			const FSplashFx& Sp = R.Puffs.GetSplash();
			if (Sp.Num() > 0)
			{
				if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(3, BlendAlpha | RepeatFlag)))
				{
					TArray<FFxVertex> Bands[4];
					for (int32 J = 0; J < Sp.Num(); ++J)
					{
						if (FSplashFx::Strips(Sp.Get(J), Bands))
						{
							for (const TArray<FFxVertex>& S : Bands) { Strip(*Out, S); }
						}
					}
				}
			}
			for (const FPuff& P : R.Puffs.Get())
			{
				const FPuffSprite S = PuffSprite(P);
				if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(S.Image, BlendAdditive)))
				{
					PushSprite(*Out, S.Pos, S.Half, FColor(Bytes(P.Rgb.X * 128.0), Bytes(P.Rgb.Y * 128.0), Bytes(P.Rgb.Z * 128.0), Bytes(S.Alpha * 128.0)), Uv(P.FlipU, P.FlipV));
				}
			}
			for (const FGlint& G : R.Glints.Get())
			{
				FVector Pos;
				double Half, Alpha;
				if (!R.Glints.Sprite(G, Pos, Half, Alpha))
				{
					continue;
				}
				if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(24 + G.Variant, BlendAdditive)))
				{
					const FVector C = R.Glints.GetRgb();
					PushSprite(*Out, Pos, Half, FColor(Bytes(C.X * 128.0), Bytes(C.Y * 128.0), Bytes(C.Z * 128.0), Bytes(Alpha * 128.0)), Uv(G.FlipU, G.FlipV));
				}
			}
			if (CVarTrails.GetValueOnGameThread() != 0)
			{
				auto For = [&](int32 Img, int32 Bl) { return Bt.For(SpriteMaterial(Img, Bl)); };
				DrawTrail(For, R.Trail);
				DrawRibbons(For, R);
			}
			// 0x1356F8 / 0x1D9700
			if (R.bPowerSlots)
			{
				for (const FPowerFxSlot& P : R.Power)
				{
					if (!P.HasPoint())
					{
						continue;
					}
					if (FSSXSpriteBatch* G = Bt.For(SpriteMaterial(28, BlendAlpha)))
					{
						const double Glow = R.LastInput.LaunchGlow;
						PushSprite(*G, P.GetPoint(), FPowerFxSlot::GlowSize,
							GsColor(ArgbGs(FVector4(1.613 * Glow + 0.377, 0.998, 0.998, 0.9905))), FVector4f(0, 0, 0.5f, 0.5f));
					}
					if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(20, BlendAlpha)))
					{
						for (const FPowerFxSlot::FPart& Part : P.GetParts()) { PushSprite(*Out, Part.Pos, Part.Size, GsColor(ArgbGs(Part.Argb))); }
					}
				}
			}
			// 0x134830
			if (R.Anger.IsActive())
			{
				const APlayerController* PC = GetWorld()->GetFirstPlayerController();
				if (PC && PC->PlayerCameraManager)
				{
					const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
					const FVector Eye = SSXSim::UnrealToGame(View.Location);
					const FVector Fwd = SSXSim::UnrealToGame(FRotationMatrix(View.Rotation).GetUnitAxis(EAxis::X));
					FAngerSprite A;
					if (R.Anger.Sprite(FVector::DotProduct(R.Anger.GetPos() - Eye, Fwd), A))
					{
						if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(FAngerMarker::Image, BlendAlpha)))
						{
							PushSprite(*Out, A.Pos, A.HalfX, GsColor(ArgbGs(A.Argb)));
							Out->Sprites.Last().HalfY = float(A.HalfY);
						}
					}
				}
			}
		}
		if (bSnow)
		{
			const APlayerController* PC = GetWorld()->GetFirstPlayerController();
			if (PC && PC->PlayerCameraManager)
			{
				Scratch.Reset();
				Snow.Gather(SSXSim::UnrealToGame(PC->PlayerCameraManager->GetCameraLocation()), Scratch);
				if (FSSXSpriteBatch* Out = Bt.For(SpriteMaterial(FSnowfall::Texture, BlendAdditive)))
				{
					for (const FSprite& S : Scratch) { PushSprite(*Out, S.Pos, S.HalfSize, GsColor(S.Rgba)); }
				}
			}
		}
	}
	LastFog = 0;
	if (bOn && CVarFog.GetValueOnGameThread() != 0 && Fog.Instances.Num())
	{
		const APlayerController* PC = GetWorld()->GetFirstPlayerController();
		UMaterialInterface* FogMaterial = SpriteMaterial(SSXFog::Texture, BlendAlpha);
		if (PC && PC->PlayerCameraManager && FogMaterial)
		{
			FogScratch.Reset();
			SSXFog::FClipView Cv;
			int32 Radius = 0;
			FrameClipView(*PC, Cv, Radius);
			if (bFogVisibility)
			{
				Fog.Gather(Cv, Radius, FogScratch);
			}
			else
			{
				Fog.Gather(Cv.Pos, Cv.Fwd, FogScratch);
			}
			if (FogScratch.Num())
			{
				FSSXSpriteBatch& Out = Batches.AddDefaulted_GetRef();
				Out.Material = FogMaterial;
				for (const SSXFog::FPuff& P : FogScratch) { PushSprite(Out, P.Pos, P.Half, GsColor(P.Rgba)); }
				LastFog = FogScratch.Num();
			}
		}
	}
	// TODO: the port draws it in the scene, under the HUD.
	DrawLensFX(Batches);
	LastSprites = 0;
	for (const FSSXSpriteBatch& B : Batches) { LastSprites += B.Sprites.Num(); }
	if (FrameCounter % 60 == 0)
	{
		UE_LOG(LogSSX, Verbose, TEXT("[particles] frame %d: %d track emitters, %d riders, %d sprites in %d batches%s"), FrameCounter,
			Emitters.Num(), Riders.Num(), LastSprites, Batches.Num(),
			Emitters.Num() ? *FString::Printf(TEXT(", first at %s"), *SSXSim::GameToUnreal(Emitters[0].Burst.P0).ToString()) : TEXT(""));
	}
	Renderer->SetBatches(MoveTemp(Batches));
}

// 0x1C72E8
float USSXParticleWorld::FrameFar(const FSSXTrackPresentation& Track, int32 Viewports) const
{
	int32 Phase = int32(ERacePhase::Race), ReplayState = 0;
	if (const ASSXRaceManager* Race = ASSXRaceManager::Find(this))
	{
		if (const FRaceSession* S = Race->GetSession())
		{
			Phase = int32(S->GetRequestedPhase());
		}
		ReplayState = Race->GetReplay().GetState();
	}
	return SSXCamera::ProjectionFar(float(Track.DrawDist1P), float(Track.DrawDist2P), Viewports, Phase, ReplayState);
}

void USSXParticleWorld::FrameClipView(const APlayerController& PC, SSXFog::FClipView& Cv, int32& Radius) const
{
	const FMinimalViewInfo& View = PC.PlayerCameraManager->GetCameraCacheView();
	const FRotationMatrix Rm(View.Rotation);
	Cv = SSXFog::FClipView();
	Cv.Pos = SSXSim::UnrealToGame(View.Location);
	Cv.Fwd = SSXSim::UnrealToGame(Rm.GetUnitAxis(EAxis::X));
	Cv.Right = SSXSim::UnrealToGame(Rm.GetUnitAxis(EAxis::Y));
	Cv.Up = SSXSim::UnrealToGame(Rm.GetUnitAxis(EAxis::Z));
	Cv.CotH = 1.0 / FMath::Tan(FMath::DegreesToRadians(double(View.FOV)) * 0.5);
	Cv.CotV = Cv.CotH * (View.AspectRatio > 0.f ? double(View.AspectRatio) : 4.0 / 3.0);
	Cv.Near = FMath::Max(double(GNearClippingPlane), 1.0);
	const USSXWorldPresentation* Pres = GetWorld()->GetSubsystem<USSXWorldPresentation>();
	const FSSXTrackPresentation* Track = Pres ? Pres->GetTrack() : nullptr;
	Radius = SSXCamera::TerrainCellRadius(SSXCamera::DefaultFar);
	if (Track)
	{
		Cv.Far = FrameFar(*Track, 1);
		Radius = SSXCamera::TerrainCellRadius(SSXCamera::TerrainFar(float(Track->DrawDist1P), float(Track->DrawDist2P), 1));
	}
}

void USSXParticleWorld::DrawLensFX(TArray<FSSXSpriteBatch>& Batches)
{
	LensStats = FLensStats();
	const USSXWorldPresentation* Pres = GetWorld()->GetSubsystem<USSXWorldPresentation>();
	const FSSXTrackPresentation* Track = Pres ? Pres->GetTrack() : nullptr;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!Track || !PC || !PC->PlayerCameraManager || !LensSpriteMid || !LensFanMaterial || CVarParticles.GetValueOnGameThread() == 0
		|| CVarLens.GetValueOnGameThread() == 0)
	{
		return;
	}
	// TODO: one node per viewport in the original;
	const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
	const SSXLensFX::FParams P = SSXLensFX::FParams::From(*Track);
	SSXLensFX::FCamera Cam;
	Cam.Pos = View.Location;
	const FRotationMatrix M(View.Rotation);
	Cam.Fwd = M.GetUnitAxis(EAxis::X);
	Cam.Right = M.GetUnitAxis(EAxis::Y);
	Cam.Up = M.GetUnitAxis(EAxis::Z);
	Cam.CotH = 1.0 / FMath::Tan(FMath::DegreesToRadians(double(View.FOV)) * 0.5);
	Cam.CotV = Cam.CotH * (View.AspectRatio > 0.f ? double(View.AspectRatio) : 4.0 / 3.0);
	Cam.Far = FrameFar(*Track, 1);
	SSXLensFX::FLensFrame F;
	// 0x2006E8 / FUN_0025ECB8
	const TArray<int32>* Halos = &Pres->GetLightWorld().GetHaloSequence();
	if (bFogVisibility && Fog.HasGrid())
	{
		SSXFog::FClipView Cv;
		int32 Radius = 0;
		FrameClipView(*PC, Cv, Radius);
		TArray<SSXFog::FFogData::FVisible> Vis;
		Fog.VisibleCells(Cv, Radius, Vis);
		TArray<TPair<int32, uint16>> Cells;
		Cells.Reserve(Vis.Num());
		for (const SSXFog::FFogData::FVisible& V : Vis) { Cells.Add({ Fog.Cells[V.Cell].Index, V.Vis }); }
		Pres->GetLightWorld().VisibleHaloSequence(Cells, HaloScratch);
		Halos = &HaloScratch;
	}
	SSXLensFX::BuildFrame(P, Pres->GetLightWorld().GetLights(), *Halos, Cam, F);
	LensStats.Halos = F.Halos.Num();
	LensStats.Elements = F.Sprites.Num() + F.Streaks.Num();
	LensStats.FanTris = F.Fan.Num();
	LensStats.bSun = F.bSun;
	auto Occ = [](const SSXLensFX::FOcclusion& O) { return FVector4f(float(O.Point.X), float(O.Point.Y), float(O.Point.Z), float(O.PullIn)); };
	auto Blk = [](const SSXLensFX::FOcclusion& O) { return FVector2f(float(O.BlockW), float(O.BlockH)); };
	if (F.Sprites.Num() || F.Streaks.Num())
	{
		FSSXSpriteBatch& B = Batches.AddDefaulted_GetRef();
		B.Material = LensSpriteMid;
		B.bOcclusion = true;
		for (const SSXLensFX::FSprite& S : F.Sprites)
		{
			FSSXSpriteVertexData& V = B.Sprites.AddDefaulted_GetRef();
			V.Pos = FVector3f(S.Centre);
			V.Half = float(S.Half);
			V.Color = S.Colour;
			V.UV = S.UV;
			V.Occ = Occ(S.Occ);
			V.Block = Blk(S.Occ);
		}
		for (const SSXLensFX::FQuad& Q : F.Streaks)
		{
			TArray<FSSXStripVertex>& St = B.Strips.AddDefaulted_GetRef();
			const FVector2f UV[4] = { { Q.UV.X, Q.UV.Y }, { Q.UV.Z, Q.UV.Y }, { Q.UV.X, Q.UV.W }, { Q.UV.Z, Q.UV.W } };
			for (int32 K = 0; K < 4; ++K)
			{
				FSSXStripVertex V;
				V.Pos = FVector3f(Q.Corner[K]);
				V.Color = Q.Colour;
				V.UV = UV[K];
				V.Occ = Occ(Q.Occ);
				V.Block = Blk(Q.Occ);
				St.Add(V);
			}
		}
	}
	if (F.Fan.Num())
	{
		FSSXSpriteBatch& B = Batches.AddDefaulted_GetRef();
		B.Material = LensFanMaterial;
		B.bOcclusion = true;
		for (const SSXLensFX::FTri& T : F.Fan)
		{
			TArray<FSSXStripVertex>& St = B.Strips.AddDefaulted_GetRef();
			const int32 Order[4] = { 0, 1, 0, 2 };
			for (const int32 K : Order)
			{
				FSSXStripVertex V;
				V.Pos = FVector3f(T.P[K]);
				V.Color = T.C[K];
				V.Occ = Occ(T.Occ);
				V.Block = Blk(T.Occ);
				St.Add(V);
			}
		}
	}
}

void USSXParticleWorld::UpdateShadow(FRiderFx& R, int32 Racers)
{
	ASnowboarderPawn* Pawn = R.Pawn.Get();
#if !UE_BUILD_SHIPPING
	const int32 Mode = CVarShadowMode.GetValueOnGameThread();
#else
	constexpr int32 Mode = 0;   // subtractive (the original's)
#endif
	// Where the Remastered look has a real sun the riders cast real shadows instead (World/SSXRemasterWorld.h).
	const bool bOn = Pawn && ShadowMaterial && Mode >= 0 && CVarParticles.GetValueOnGameThread() != 0 && R.bHaveInput && R.RenderAlpha > 0.0
		&& !SSXRemaster::RealRiderShadows(*GetWorld());
	USSXCharacterAppearance* App = Pawn ? Pawn->GetAppearance() : nullptr;
	UPoseableMeshComponent* Mesh = App ? App->GetRiderMesh() : nullptr;
	if (!bOn || !Mesh || !Mesh->IsVisible())
	{
		if (R.Capture.IsValid()) { R.Capture->bCaptureEveryFrame = false; }
		if (R.Projector.IsValid()) { R.Projector->SetVisibility(false); }
		return;
	}
	const int32 Size = ShadowTextureSize(Racers);
	if (!R.Capture.IsValid() || R.ShadowSize != Size)
	{
		UTextureRenderTarget2D* Rt = NewObject<UTextureRenderTarget2D>(this);
		Rt->RenderTargetFormat = RTF_RGBA16f;
		Rt->ClearColor = FLinearColor(0, 0, 0, 1);
		Rt->InitAutoFormat(Size, Size);
		Rt->UpdateResourceImmediate(true);
		Keep.Add(Rt);
		USceneCaptureComponent2D* Cap = R.Capture.Get();
		if (!Cap)
		{
			Cap = NewObject<USceneCaptureComponent2D>(RenderActor);
			Cap->ProjectionType = ECameraProjectionMode::Orthographic;
			Cap->CaptureSource = SCS_SceneColorHDR;
			Cap->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			Cap->bCaptureOnMovement = false;
			Cap->ShowFlags.SetPostProcessing(false);
			Cap->ShowFlags.SetAtmosphere(false);
			Cap->ShowFlags.SetFog(false);
			Cap->SetupAttachment(RenderActor->GetRootComponent());
			Cap->RegisterComponent();
			Keep.Add(Cap);
			UStaticMeshComponent* Box = NewObject<UStaticMeshComponent>(RenderActor);
			Box->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Box->SetCastShadow(false);
			Box->SetupAttachment(RenderActor->GetRootComponent());
			Box->RegisterComponent();
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(ShadowMaterial, this);
			Box->SetMaterial(0, Mid);
			Keep.Add(Box);
			Keep.Add(Mid);
			R.Capture = Cap;
			R.Projector = Box;
			R.ShadowMid = Mid;
		}
		Cap->TextureTarget = Rt;
		R.ShadowTarget = Rt;
		R.ShadowSize = Size;
		R.ShadowMid->SetTextureParameterValue(TEXT("ShadowTex"), Rt);
	}
	USceneCaptureComponent2D* Cap = R.Capture.Get();
	const FRiderState* S = Pawn->GetSimState();
	const FVector RenderOffset = Pawn->GetActorLocation() - SSXSim::GameToUnreal(S->Position);
	// 0x207F18
	UStaticMeshComponent* Board = App->GetBoardMesh();
	const bool bBoard = Board && ShadowBoardInSilhouette(int32(S->Motion), S->GroundHeight);
	Cap->ShowOnlyComponents.Reset();
	Cap->ShowOnlyComponents.Add(Mesh);
	if (bBoard) { Cap->ShowOnlyComponents.Add(Board); }
	// 0x1C7E90 / 0x129628
	TArray<FVector, TInlineAllocator<16>> Pts;
	FVector Eye = S->Position;
	if (const SSXAnim::FRiderAnimDriver* Anim = App->GetAnimator(); Anim && Anim->IsReady())
	{
		SSXAnim::FRiderPalette Pal;
		Anim->ComputePalette(*Pawn->GetSim(), Pal);
		if (Pal.bValid && Pal.Body.Num() > 18)
		{
			for (const int32 B : { 4, 8, 12, 15, 18, 0 }) { Pts.Add(FVector(Pal.Body[B].Row(3))); }
			Eye = (FVector(Pal.Body[15].Row(3)) + FVector(Pal.Body[18].Row(3))) * 0.5;
		}
	}
	FVector Corners[4];
	LiveBoardCorners(R.LastInput, Corners);
	Pts.Append(Corners, 4);
	const FVector Down(0, 0, -1);
	const FVector R0 = FVector::CrossProduct(S->BasisUp, Down);
	if (!R0.IsNearlyZero(1e-12)) { R.ShadowAxisX = R0.GetUnsafeNormal(); }
	const FVector Ax = R.ShadowAxisX;
	const FVector Ay = FVector::CrossProduct(Down, Ax).GetSafeNormal();
	const FBox2D Ext = ShadowExtents(Eye, Ax, Ay, Pts);
	const double X0 = Ext.Min.X, X1 = Ext.Max.X, Y0 = Ext.Min.Y, Y1 = Ext.Max.Y;
	double Top = Eye.Z;
	for (const FVector& P : Pts) { Top = FMath::Max(Top, P.Z); }
	const double HalfX = 0.5 * (X1 - X0), HalfY = 0.5 * (Y1 - Y0);
	const FVector CentreG = Eye + Ax * (0.5 * (X0 + X1)) + Ay * (0.5 * (Y0 + Y1));
	const FVector Centre = SSXSim::GameToUnreal(CentreG) + RenderOffset;
	const FVector UpU = SSXSim::GameToUnreal(Ay);
	const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(UpU.Y, UpU.X));
	Cap->bUseCustomProjectionMatrix = true;
	Cap->CustomProjectionMatrix = FReversedZOrthoMatrix(float(HalfX), float(HalfY), 1.f / (UE_FLOAT_HUGE_DISTANCE / 4.f), 0.f);
	Cap->OrthoWidth = float(2.0 * HalfX);
	Cap->SetWorldLocationAndRotation(Centre + FVector(0, 0, Top - CentreG.Z + 50.0), FRotator(-90.0, Yaw, 0.0));
	Cap->bCaptureEveryFrame = true;
	const double ZMax = ShadowDepthAbove, ZMin = -ShadowDepthBelow;
	UStaticMeshComponent* Proj = R.Projector.Get();
	Proj->SetVisibility(true);
	Proj->SetWorldLocationAndRotation(Centre + FVector(0, 0, 0.5 * (ZMax + ZMin)), FRotator(0.0, Yaw, 0.0));
	Proj->SetWorldScale3D(FVector(2.0 * HalfY / 100.0, 2.0 * HalfX / 100.0, -(ZMax - ZMin) / 100.0));
	UMaterialInstanceDynamic* Mid = R.ShadowMid.Get();
	Mid->SetVectorParameterValue(TEXT("ShadowCentre"), FLinearColor(float(Centre.X), float(Centre.Y), float(Centre.Z), 0.f));
	Mid->SetVectorParameterValue(TEXT("ShadowAxis"), FLinearColor(float(UpU.X), float(UpU.Y), 0.f, 0.f));
	Mid->SetScalarParameterValue(TEXT("ShadowHalfX"), float(HalfX));
	Mid->SetScalarParameterValue(TEXT("ShadowHalfY"), float(HalfY));
	Mid->SetScalarParameterValue(TEXT("ShadowZMin"), float(ZMin));
	Mid->SetScalarParameterValue(TEXT("ShadowZMax"), float(ZMax));
	Mid->SetScalarParameterValue(TEXT("ShadowSub"), float(ShadowSubtract));
	Mid->SetScalarParameterValue(TEXT("ShadowMode"), float(Mode));
}

void USSXParticleWorld::StepRibbons(FRiderFx& R, const FRiderFxInput& In, int32 FrameNo)
{
	// TODO: the bone names;
	R.bHaveLiveRibbon = false;
	FVector4 Tint;
	const bool bBoost = GhostTint(In.BoostStrength, In.BurstTimer, Tint);
	if (!bBoost && R.Ribbons.GetCount() == 0)
	{
		return;
	}
	ASnowboarderPawn* Pawn = R.Pawn.Get();
	const USSXCharacterAppearance* App = Pawn ? Pawn->GetAppearance() : nullptr;
	const SSXAnim::FRiderAnimDriver* Anim = App ? App->GetAnimator() : nullptr;
	const FRiderSim* Sim = Pawn ? Pawn->GetSim() : nullptr;
	SSXAnim::FRiderPalette Pal;
	if (Anim && Sim && Anim->IsReady())
	{
		Anim->ComputePalette(*Sim, Pal);
	}
	if (!Pal.bValid || !Pal.Body.IsValidIndex(12))
	{
		return;
	}
	auto B = [&Pal](int32 K) { return FVector(Pal.Body[K].Row(3)); };
	if (!bBoost)
	{
		Tint = R.Ribbons.Get(0).Tint;
	}
	R.LiveRibbon = FBoostRibbons::MakeSample(B(8), B(7), B(6), B(12), B(11), B(10), Tint);
	R.bHaveLiveRibbon = true;
	// 0x136C48 / 0x1364D0
	FVector Bn[FRiderState::FxPoseBoneCount];
	const FRibbonSample Sample = UpdatePoseBones(*Pawn, Bn) ? FBoostRibbons::MakeSample(Bn[8], Bn[7], Bn[6], Bn[12], Bn[11], Bn[10], Tint) : R.LiveRibbon;
	R.Ribbons.Frame(FrameNo, In.BoostStrength, In.BurstTimer, Sample);
}
