#include "SnowboarderPawn.h"

#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXGameAudio.h"
#include "Race/SSXRaceManager.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/SSXCharacterAppearance.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "HAL/IConsoleManager.h"
#include "Input/SSXDualSense.h"
#include "Input/SSXRumbleSubsystem.h"
#include "Sim/Anim/AflClips.h"
#include "Harness/RiderSetup.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Progression/SSXSettingsEffects.h"
#include "SSXTrickyRebuild.h"
#include "SSXShowOffDraw.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/SimCoords.h"
#include "World/SSXPropAnimation.h"
#include "Effects/SSXTrackEffects.h"
#include "UObject/ConstructorHelpers.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXLoadingScreen.h"
#include "UnrealClient.h"

namespace
{
	constexpr float CameraRadius = 35.f;
	constexpr double TeleportDistance = 1000.0;   // cm in one step: respawn, do not interpolate

	// 0x15A030, 0x15F300, 0x235780
	void StepAnimationAndEvents(ASnowboarderPawn& Pawn, USSXCharacterAppearance& App, FRiderSim& Sim)
	{
		const SSXAnim::FRiderAnimDriver* D = App.GetAnimator();
		SSXGameAudio::FAnimSnapshot Before;
		if (D)
		{
			Before = SSXGameAudio::SnapshotAnim(D->GetController());
		}
		App.StepAnimation(Sim);
		D = App.GetAnimator();
		USSXAudioSubsystem* A = USSXAudioSubsystem::Get(&Pawn);
		if (!D || !A || !D->GetLibrary())
		{
			return;
		}
		TArray<int32> Values;
		SSXGameAudio::CrossedAnimEvents(Before, SSXGameAudio::SnapshotAnim(D->GetController()), *D->GetLibrary(), Values);
		if (Values.Num() == 0)
		{
			return;
		}
		const ASSXRaceManager* Race = ASSXRaceManager::Find(&Pawn);
		const int32 Racer = Race ? Race->GetRacerIndex(&Pawn) : -1;
		const bool bSceneActor = Race && Race->GetScene().DeliversEventsOf(Racer);
		for (const int32 V : Values)
		{
			if (bSceneActor && V >= 1000)
			{
				continue;
			}
			A->OnAnimEvent(V, Racer, Pawn.GetActorLocation(), !Pawn.bAIRider);
		}
	}

	const FLinearColor BoardColor(0.95f, 0.35f, 0.05f);
	const FLinearColor PantsColor(0.08f, 0.08f, 0.12f);
	const FLinearColor JacketColor(0.05f, 0.65f, 0.6f);
	const FLinearColor HelmetColor(0.92f, 0.92f, 0.95f);
	const FLinearColor GogglesColor(0.95f, 0.75f, 0.1f);

	FQuat RenderQuat(const FRiderState& S)
	{
		return SSXRiderSetup::RenderedOrientation(S);
	}
}

ASnowboarderPawn::ASnowboarderPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Collider = CreateDefaultSubobject<USphereComponent>(TEXT("Collider"));
	Collider->InitSphereRadius(CameraRadius);
	Collider->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Collider;

	RiderRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RiderRoot"));
	RiderRoot->SetupAttachment(Collider);
	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(RiderRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ColorMaterial(TEXT("/Game/Rider/M_RiderColor.M_RiderColor"));
	RiderMaterial = ColorMaterial.Object;

	// placeholder shapes (basic shapes are 100 cm across): board nose = actor +X (the sim's body forward), rider facing +Y
	UStaticMeshComponent* BoardPart = AddRiderPart(TEXT("Board"), Cube.Object, FVector(0, 0, 1.5f), FVector(1.55f, 0.28f, 0.03f), BoardColor);
	BoardPart->SetupAttachment(RiderRoot);
	AddRiderPart(TEXT("LegFront"), Cylinder.Object, FVector(22, 0, 25), FVector(0.15f, 0.15f, 0.47f), PantsColor);
	AddRiderPart(TEXT("LegBack"), Cylinder.Object, FVector(-22, 0, 25), FVector(0.15f, 0.15f, 0.47f), PantsColor);
	AddRiderPart(TEXT("Torso"), Cube.Object, FVector(0, 0, 72), FVector(0.34f, 0.24f, 0.48f), JacketColor);
	UStaticMeshComponent* ArmFront = AddRiderPart(TEXT("ArmFront"), Cylinder.Object, FVector(36, 0, 84), FVector(0.1f, 0.1f, 0.42f), JacketColor);
	UStaticMeshComponent* ArmBack = AddRiderPart(TEXT("ArmBack"), Cylinder.Object, FVector(-36, 0, 84), FVector(0.1f, 0.1f, 0.42f), JacketColor);
	ArmFront->SetRelativeRotation(FRotator(70.f, 0.f, 0.f));
	ArmBack->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
	AddRiderPart(TEXT("Helmet"), Sphere.Object, FVector(0, 0, 112), FVector(0.27f), HelmetColor);
	AddRiderPart(TEXT("Goggles"), Cube.Object, FVector(0, 11, 113), FVector(0.2f, 0.08f, 0.07f), GogglesColor);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Collider);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->bOverrideAspectRatioAxisConstraint = true;
	Camera->AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainXFOV;
	Camera->bConstrainAspectRatio = true;
	Camera->AspectRatio = 4.f / 3.f;

	Appearance = CreateDefaultSubobject<USSXCharacterAppearance>(TEXT("Appearance"));

	BoardSound = CreateDefaultSubobject<USSXBoardSoundComponent>(TEXT("BoardSound"));
	BoardSound->SetupAttachment(RiderRoot);

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

ASnowboarderPawn::~ASnowboarderPawn() = default;

UStaticMeshComponent* ASnowboarderPawn::AddRiderPart(const TCHAR* Name, UStaticMesh* Mesh, const FVector& Location,
	const FVector& Scale, const FLinearColor& Color)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(BodyRoot);
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(Scale);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->ComponentTags.Add(*FString::Printf(TEXT("Color=%s"), *Color.ToString()));
	return Part;
}

void ASnowboarderPawn::BeginPlay()
{
	BoardSound->bIsAI = bAIRider;
	Super::BeginPlay();

	TArray<UStaticMeshComponent*> Parts;
	GetComponents(Parts);
	for (UStaticMeshComponent* Part : Parts)
	{
		for (const FName& Tag : Part->ComponentTags)
		{
			FLinearColor Color = FLinearColor::White;
			const FString TagString = Tag.ToString();
			if (TagString.StartsWith(TEXT("Color=")) && Color.InitFromString(TagString.RightChop(6)))
			{
				UMaterialInstanceDynamic* Mid = RiderMaterial
					? UMaterialInstanceDynamic::Create(RiderMaterial, Part)
					: Part->CreateAndSetMaterialInstanceDynamic(0);
				if (Mid)
				{
					Mid->SetVectorParameterValue(TEXT("Color"), Color);
					Part->SetMaterial(0, Mid);
				}
			}
		}
	}

#if !UE_BUILD_SHIPPING
	if (!bAIRider && FParse::Param(FCommandLine::Get(), TEXT("SSXAutoTest")))
	{
		if (FParse::Value(FCommandLine::Get(), TEXT("-SSXRider="), Character)) { Board = INDEX_NONE; }
		FParse::Value(FCommandLine::Get(), TEXT("-SSXBoardClass="), BoardClass);
	}
#endif
	ResolveBoardSelection();
	// 0x3352B0, 0x16A9D8
	Appearance->bMalloraLook = !bAIRider && PlayerOptions.IsSet() && PlayerOptions->bMallora
		&& Character == SSXRider::MalloraCharacter;
	Appearance->Apply(RiderRoot, BodyRoot, CharacterId, Character, BoardClass, Board, Outfit);
	if (bAIRider)
	{
		Camera->Deactivate();
	}

#if !UE_BUILD_SHIPPING
	bAutoTest = FParse::Param(FCommandLine::Get(), TEXT("SSXAutoTest"));
	bAutoTrick = bAutoTest && !bAIRider && FParse::Param(FCommandLine::Get(), TEXT("SSXAutoTrick"));
#endif
	if (!InitSim())
	{
		UE_LOG(LogSSX, Error, TEXT("[pawn] %s"), *StatusText);
	}
}

void ASnowboarderPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	Sim.Reset();
	Driver.Reset();
	CollisionWorld.Reset();
	PatchWorld.Reset();
	Super::EndPlay(Reason);
}

void ASnowboarderPawn::ApplyRiderProfile(FRiderSim& InSim)
{
	if (ProfileOverride.IsSet())
	{
		SSXRider::ApplyProfile(*ProfileOverride, InSim.Profile);
		BoardClass = InSim.Profile.BoardClass;
		return;
	}
	const int32 Slot = SSXRider::ResolveBoard(SSXRider::FRiderTable::Shared(), Character, Board, BoardClass);
	if (bAIRider)
	{
		SSXRider::ApplyComputerProfile(InSim.Profile, Character, ComputerLevel.Get(0), Slot);
	}
	else
	{
		SSXRider::FPlayerOptions Options = PlayerOptions.Get(SSXRider::FPlayerOptions());
		Options.Board = Slot;
		SSXRider::ApplySelectedPlayerProfile(InSim.Profile, Character, Options);
	}
	if (InSim.Profile.bProfileLoaded)
	{
		BoardClass = InSim.Profile.BoardClass;
	}
}

void ASnowboarderPawn::ResolveBoardSelection()
{
	const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
	if (!Table.IsLoaded() || Character < 0 || Character >= SSXRider::NumRiders)
	{
		return;
	}
	Board = SSXRider::ResolveBoard(Table, Character, Board, BoardClass);
	BoardClass = Table.Board(Character, Board).Class;
}

bool ASnowboarderPawn::InitSim()
{
	UWorld* World = GetWorld();
	TrackName = UWorld::RemovePIEPrefix(World->GetMapName()).ToLower();

	if (SharedPatchWorld)
	{
		PatchWorld = SharedPatchWorld;
	}
	else
	{
		PatchWorld = MakeShared<FPatchWorld>();
		FString Err;
		if (!PatchWorld->LoadTrack(FPatchWorld::DefaultTrackPath(TrackName), &Err))
		{
			StatusText = FString::Printf(TEXT("no sim data for track '%s': %s"), *TrackName, *Err);
			PatchWorld.Reset();
			return false;
		}
		SSXShowOff::ApplyDrawRules(World, *PatchWorld, PatchWorld->bAlternateSetMode);
	}
	CollisionWorld.Reset();
	InitCamera();

	Sim = MakeUnique<FRiderSim>();
	Sim->bLogTransitions = !bAIRider;   // ssx.Rider.StateLog 1 (default): the player's transitions
	Sim->DebugName = bAIRider ? TEXT("AI rider") : TEXT("player");
	if (bAIRider)
	{
		StatesText = SSXRiderSetup::RegisterStates(*Sim, bRaceDriven);
	}
	else
	{
		Driver = MakeUnique<FRiderInputDriver>();
		StatesText = SSXRiderSetup::InstallStates(*Sim, *Driver, bRaceDriven);
	}
	Sim->SetWorld(PatchWorld.Get());
	Sim->Profile.Character = Character;
	if (USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this))
	{
		PropAnim->BindSim(*Sim);
	}
	if (!bRaceDriven && !bAIRider)
	{
		if (USSXTrackEffects* Fx = USSXTrackEffects::Get(this); Fx && Fx->BeginTrack(TrackName, -1, 1, PatchWorld))
		{
			Fx->AddRider(Sim.Get(), true, 0);
			Fx->RunFlow(SSXEffects::EFlowPhase::PreRace);
			Fx->RunFlow(SSXEffects::EFlowPhase::StartRace);
		}
	}
	Sim->Profile.TrickCharacter = Character;
	Sim->Profile.BoardClass = BoardClass;
	ApplyRiderProfile(*Sim);
	Mapper.SetRider(Character, BoardClass);
	if (const USSXFrontEndSubsystem* FrontEnd = (!bAIRider) ? USSXFrontEndSubsystem::Get(this) : nullptr; FrontEnd && FrontEnd->bHasSelection)
	{
		// 0x14AE74
		PadLayout = SSXSettings::PadLayout(FrontEnd->Profile.Settings, 0, FrontEnd->Launch.GameMode);
	}
	Mapper.SetLayout(PadLayout);
	PadDevice.Reset();
	if (USSXRumbleSubsystem* Rumble = (!bAIRider) ? USSXRumbleSubsystem::Get(this) : nullptr)
	{
		// 0x17E148
		const USSXFrontEndSubsystem* FrontEnd = USSXFrontEndSubsystem::Get(this);
		const FSSXSettings St = FrontEnd ? FrontEnd->Profile.Settings : FSSXSettings();
		Rumble->BeginGame(St.Vibration[0], St.Vibration[1]);
	}

	if (bRaceDriven)
	{
		StatusText = TEXT("race start placement");
		UE_LOG(LogSSX, Display, TEXT("[pawn] track %s: %d patches; %s; %s%s"), *TrackName, PatchWorld->NumPatches(),
			*StatusText, *StatesText, bAIRider ? TEXT(" (AI)") : TEXT(""));
		Sim->Reset();
		CurPos = PrevPos = Sim->State.Position;
		CurRot = PrevRot = RenderQuat(Sim->State);
		if (Appearance) { Appearance->ResetPoseHistory(); }
		return true;
	}

	FVector StartUE = GetActorLocation();
	FVector StartFwdUE = GetActorForwardVector();
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		StartUE = It->GetActorLocation();
		StartFwdUE = It->GetActorForwardVector();
		break;
	}
	const FVector Marker = SSXSim::UnrealToGame(StartUE);
	const FVector MarkerFwd = SSXSim::UnrealToGame(StartFwdUE);
	FRiderWorldHit Hit;
	const bool bHit = SSXRiderSetup::ProjectToGround(*PatchWorld, Marker, Hit);
	SpawnPos = bHit ? Hit.Point : Marker;
	SpawnHeading = MarkerFwd;
	StatusText = FString::Printf(TEXT("no race data: spawn at PlayerStart (%s)"),
		bHit ? *FString::Printf(TEXT("surface %d"), Hit.Surface) : TEXT("off the patches"));
	UE_LOG(LogSSX, Warning, TEXT("[pawn] track %s: %d patches; %s; %s"), *TrackName, PatchWorld->NumPatches(), *StatusText, *StatesText);
	SpawnRider();
	return true;
}

void ASnowboarderPawn::SpawnRider()
{
	if (!Sim)
	{
		return;
	}
	Sim->Reset();
	Sim->Spawn(SpawnPos, SpawnHeading, FVector::ZeroVector, 0.0, false);
	Mapper.ResetEdges();
	Clock.Reset();
	CurPos = PrevPos = Sim->State.Position;
	CurRot = PrevRot = RenderQuat(Sim->State);
	Appearance->ResetPoseHistory();
	LastMotion = Sim->State.Motion;
	if (BoardSound)
	{
		BoardSound->OnRiderStateRestored(Sim->State);
	}
	TrickText.Reset();
	TrickTextTime = 0.f;
	ApplyRenderTransform(0.0);
	ResetCamera();
}

void ASnowboarderPawn::Restart()
{
	if (bRaceDriven)
	{
		return;
	}
	SpawnRider();
}

void ASnowboarderPawn::FellOutOfWorld(const UDamageType& DamageType)
{
	Restart();
}

void ASnowboarderPawn::RaceStepFrame()
{
	RaceSimFrame();
	RacePostAI();
	RacePostFrame();
}

void ASnowboarderPawn::RacePostAI()
{
	if (Sim && Sim->bDeferPostAI)
	{
		Sim->PostAI();   // 0x129628, 0x126748
	}
}

void ASnowboarderPawn::RaceSimFrame()
{
	if (!Sim)
	{
		return;
	}
	PrevPos = CurPos;
	PrevRot = CurRot;
	Sim->bDeferPostAI = true;
	if (bAIRider || !Driver)
	{
		Sim->Step(FRiderInputWord());
	}
	else
	{
		if (TestPadScript) { TestPadScript(PendingPad); }
		FSSXPadFrame Pad = PadDevice.Update(PendingPad);
		if (LessonPadInject) { LessonPadInject(Pad); }   // 0x14B7C8
		const FPadActions Actions = Mapper.Step(Pad);
		Sim->DebugPadButtons = Pad.ButtonsFiltered;
		bReverseHeld = Actions.IsHeld(FPadMapper::ActCameraReverse);
		bReversePressed = Actions.WasPressed(FPadMapper::ActCameraReverse);
		if (LessonPadDone) { LessonPadDone(); }          // 0x14B8A8
		// 0x1927D8
		uint32 Word = 0;
		if (ReplayPlayWord && ReplayPlayWord(Word))
		{
			Driver->StepWord(*Sim, Word);
		}
		else
		{
			Driver->Step(*Sim, Actions);
			LogShove(Actions, PendingPad);
			if (ReplayRecordWord) { ReplayRecordWord(Driver->GetLastWord()); }
		}
	}
	FeedRumble();
}

void ASnowboarderPawn::StepCameraReverse(bool bReplayFlag)
{
	if (bAIRider || !RaceCamera)
	{
		return;   // 0x11CA88
	}
	ReverseLatch.Step(*RaceCamera, bReverseHeld, bReversePressed, bReplayFlag);
}

void ASnowboarderPawn::RacePostFrame()
{
	if (!Sim)
	{
		return;
	}
	// 0x114730
	StepAnimationAndEvents(*this, *Appearance, *Sim);
	BoardSound->UpdateFromRider(Sim->State, float(Clock.StepSeconds));
	CurPos = Sim->State.Position;
	CurRot = RenderQuat(Sim->State);
	const bool bTeleported = FVector::Dist(PrevPos, CurPos) > TeleportDistance;
	if (bTeleported)
	{
		PrevPos = CurPos;
		PrevRot = CurRot;
	}
	bLastStepTeleported = bTeleported;
	if (!bAIRider)
	{
		StepCamera(bTeleported);
	}
}

#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarShoveLog(TEXT("ssx.Shove.Log"), 1,
	TEXT("1: log every human shove (raw pad bytes, PS2 pad byte, mapper values, clip, stance, hands along the board nose)"));

void ASnowboarderPawn::LogShove(const FPadActions& Actions, const FSSXPadHardware& Hw)
{
	if (bAIRider || !Sim || CVarShoveLog.GetValueOnGameThread() == 0)
	{
		ShoveLogCount = Sim ? Sim->State.ShoveCount : 0;
		ShoveLogAnim = 0;
		return;
	}
	const FRiderState& St = Sim->State;
	const SSXAnim::FRiderAnimDriver* Anim = GetAnimatorForReplay();
	const FVector Nose = St.RenderRow1.GetSafeNormal();
	const FVector Travel = FVector(St.Velocity.X, St.Velocity.Y, 0.0).GetSafeNormal();
	if (St.ShoveCount != ShoveLogCount && St.ShoveAnim != 0)
	{
		ShoveLogCount = St.ShoveCount;
		const double Now = FPlatformTime::Seconds();
		if (Now - ShoveLogLastTime < 0.25)
		{
			ShoveLogAnim = 0;   // rate limit: at most four shoves a second
			return;
		}
		ShoveLogLastTime = Now;
		FString Dev = TEXT("no Sony pad (XInput / keyboard)");
		SSXPad::FSonyPadState Sony;
		uint16 Pid = 0;
		bool bBt = false;
		if (SSXPad::GetSonyPadDebug(Sony, Pid, bBt))
		{
			Dev = FString::Printf(TEXT("Sony pid %04x %s report 0x%02x raw LX %u LY %u RX %u RY %u (y 0 = up)"), Pid,
				bBt ? TEXT("BT") : TEXT("USB"), Sony.ReportId, Sony.LX, Sony.LY, Sony.RX, Sony.RY);
		}
		const uint32 Word = Driver ? Driver->GetLastWord() : 0u;
		const FRiderInputWord W = FPadMapper::DecodeWord(St.Control, Word);
		UE_LOG(LogSSX, Display, TEXT("[shove] #%d %s | PS2 pad RX %u RY %u (0 = up) | mapper side %.3f fwd %.3f -> word side %d fwd %d | clip %#x (0x207 = COMBATFWD toward the nose) angle %.0f deg | %s | attack axis . nose %.3f, nose . travel %.3f"),
			St.ShoveCount, *Dev, Hw.RX, Hw.RY, Actions.Axes[FPadMapper::AShoveSide], Actions.Axes[FPadMapper::AShoveFwd],
			W.ShoveSide, W.ShoveFwd, St.ShoveAnim, FMath::RadiansToDegrees(St.ShoveAngle),
			St.bStanceGoofy ? TEXT("goofy") : TEXT("regular"), FVector::DotProduct(St.ShoveAxis, Nose), FVector::DotProduct(Nose, Travel));
		ShoveLogAnim = St.ShoveAnim;
		ShoveLogClip = TEXT("?");
		ShoveLogHand[0] = ShoveLogHand[2] = 1e9;
		ShoveLogHand[1] = ShoveLogHand[3] = -1e9;
	}
	if (ShoveLogAnim == 0)
	{
		return;
	}
	if (St.ShoveAnim != 0 && Anim)
	{
		if (const SSXAnim::FPlayInstance* I = Anim->GetController().Newest(1); I && I->AnimId == ShoveLogAnim && I->NumSubs > 0)
		{
			const SSXAnim::FAflLibrary* Lib = Anim->GetLibrary();
			if (const SSXAnim::FAflClip* C = Lib ? Lib->Clip(I->Subs[0].Clip) : nullptr) { ShoveLogClip = C->Name; }
		}
		// hands relative to the hips on the evaluated palette (as SSX.Anim.ShovePose measures them)
		const SSXAnim::FAflSkeleton* Sk = Anim->GetSkeleton();
		int32 Hips = INDEX_NONE, LHand = INDEX_NONE, RHand = INDEX_NONE;
		for (int32 I = 0; Sk && I < Sk->Bones.Num(); ++I)
		{
			const FString& N = Sk->Bones[I].Name;
			if (N == TEXT("hips")) { Hips = I; } else if (N == TEXT("l_hand")) { LHand = I; } else if (N == TEXT("r_hand")) { RHand = I; }
		}
		SSXAnim::FRiderPalette P;
		Anim->ComputePalette(*Sim, P);
		if (P.bValid && Hips >= 0 && LHand >= 0 && RHand >= 0)
		{
			const FVector H(P.Body[Hips].Row(3));
			for (const int32 B : { LHand, RHand })
			{
				const FVector D = FVector(P.Body[B].Row(3)) - H;
				const double DN = FVector::DotProduct(D, Nose), DT = FVector::DotProduct(D, Travel);
				ShoveLogHand[0] = FMath::Min(ShoveLogHand[0], DN); ShoveLogHand[1] = FMath::Max(ShoveLogHand[1], DN);
				ShoveLogHand[2] = FMath::Min(ShoveLogHand[2], DT); ShoveLogHand[3] = FMath::Max(ShoveLogHand[3], DT);
			}
		}
		return;
	}
	UE_LOG(LogSSX, Display, TEXT("[shove] #%d clip %#x %s done: hands - hips along the board nose [%.1f, %.1f] cm, along the travel direction [%.1f, %.1f] cm (forward shove: max > 0 and |max| > |min|)"),
		ShoveLogCount, ShoveLogAnim, *ShoveLogClip, ShoveLogHand[0], ShoveLogHand[1], ShoveLogHand[2], ShoveLogHand[3]);
	ShoveLogAnim = 0;
}
#else
void ASnowboarderPawn::LogShove(const FPadActions&, const FSSXPadHardware&) {}
#endif

void ASnowboarderPawn::FeedRumble()
{
	// 0x150D18
	const TArray<FRiderPadFeedback> Calls = Sim->State.TakePadFeedback();
	if (bAIRider)
	{
		return;
	}
	if (USSXRumbleSubsystem* Rumble = USSXRumbleSubsystem::Get(this))
	{
		const APlayerController* PC = Cast<APlayerController>(GetController());
		const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
		const ASSXRaceManager* RaceMgr = ASSXRaceManager::Find(this);
		Rumble->RiderFrame(LP ? LP->GetLocalPlayerIndex() : 0, Calls, RaceMgr && RaceMgr->GetReplay().IsPlaying());
	}
}

void ASnowboarderPawn::RaceRender(double Alpha, float DeltaSeconds)
{
	if (!Sim)
	{
		return;
	}
	ApplyRenderTransform(Alpha);
	if (!bAIRider)
	{
		UpdateCamera(Alpha);
		TrackTricks(DeltaSeconds);
	}
}

SSXAnim::FRiderAnimDriver* ASnowboarderPawn::GetAnimatorForReplay() const
{
	return Appearance ? const_cast<SSXAnim::FRiderAnimDriver*>(Appearance->GetAnimator()) : nullptr;
}

void ASnowboarderPawn::ResetRenderState()
{
	if (!Sim)
	{
		return;
	}
	Mapper.ResetEdges();
	CurPos = PrevPos = Sim->State.Position;
	CurRot = PrevRot = RenderQuat(Sim->State);
	Appearance->ResetPoseHistory();
	LastMotion = Sim->State.Motion;
	ApplyRenderTransform(0.0);
	if (!bAIRider)
	{
		ResetCamera();
	}
}

void ASnowboarderPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	SSXInput::CreateBindings(this, InputBindings);
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputBindings.Context, 0);
		}
	}
}

void ASnowboarderPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Sim)
	{
		if (bAutoTest)
		{
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		return;
	}
	if (USSXLoadingScreen::HoldsGame(this))
	{
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	const UEnhancedPlayerInput* PlayerInput = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
	const FSSXPadHardware Pad = SSXInput::ReadHardware(PlayerInput, InputBindings);
	if (bRaceDriven)
	{
		PendingPad = Pad;
		if (bAutoTest && !bAIRider)
		{
			TickAutoTest(DeltaSeconds);
		}
		return;
	}

	const bool bRestart = SSXInput::IsRestartHeld(PlayerInput, InputBindings);
	const bool bStart = (Pad.Buttons & SSXPadButton::Start) != 0;
	if ((bRestart && !bRestartWasHeld) || (bStartRestarts && bStart && !bStartWasHeld))
	{
		Restart();
	}
	bRestartWasHeld = bRestart;
	bStartWasHeld = bStart;

	StepsLastFrame = Clock.Advance(DeltaSeconds);
	StepSim(StepsLastFrame, Pad);
	if (USSXPropAnimation* PropAnim = (!bAIRider) ? USSXPropAnimation::Get(this) : nullptr)
	{
		PropAnim->ApplyRender();
	}
	if (USSXTrackEffects* Fx = (!bAIRider) ? USSXTrackEffects::Get(this) : nullptr)
	{
		Fx->ApplyRender();
	}
	ApplyRenderTransform(Clock.Alpha());
	UpdateCamera(Clock.Alpha());
	TrackTricks(DeltaSeconds);
	if (bAutoTest)
	{
		TickAutoTest(DeltaSeconds);
	}
}

void ASnowboarderPawn::StepSim(int32 Steps, const FSSXPadHardware& Pad)
{
	for (int32 I = 0; I < Steps; ++I)
	{
		PrevPos = CurPos;
		PrevRot = CurRot;
		USSXTrackEffects* Fx = (!bAIRider) ? USSXTrackEffects::Get(this) : nullptr;
		USSXPropAnimation* PropAnim = (!bAIRider) ? USSXPropAnimation::Get(this) : nullptr;
		if (Fx)
		{
			Fx->BeginFrame();
			Fx->StepCells();
			Fx->StepFrame();
		}
		if (PropAnim)
		{
			PropAnim->StepFrame();
		}
		const FSSXPadFrame PadFrame = PadDevice.Update(Pad);
		const FPadActions Actions = Mapper.Step(PadFrame);   // one pad poll per game frame
		Sim->DebugPadButtons = PadFrame.ButtonsFiltered;
		Driver->Step(*Sim, Actions);
		LogShove(Actions, Pad);
		FeedRumble();
		if (Fx)
		{
			Fx->PostRiders();
			// 0x1166F0, 0x22D370
			if (RaceCamera)
			{
				FVector Fwd, Up;
				RaceCamera->GetViewAxes(Fwd, Up);
				const FVector L = RaceCamera->GetPosition();
				Fx->StepAmbient(&L, L + Fwd * SSXBoardAudio::EarForwardCm - Up * SSXBoardAudio::EarUpCm);
			}
		}
		StepAnimationAndEvents(*this, *Appearance, *Sim);
		BoardSound->UpdateFromRider(Sim->State, float(Clock.StepSeconds));
		if (!bAIRider)
		{
			// FUN_0021C6D0
			if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(this))
			{
				SSXGameAudio::TrickyFrame(*A, Sim.Get(), 1);
			}
		}
		CurPos = Sim->State.Position;
		CurRot = RenderQuat(Sim->State);
		const bool bTeleported = FVector::Dist(PrevPos, CurPos) > TeleportDistance;
		if (bTeleported)
		{
			PrevPos = CurPos;
			PrevRot = CurRot;
		}
		StepCamera(bTeleported);
		bLastStepTeleported = bTeleported;
		if (I >= Steps - 2)
		{
			CaptureRenderPose();
		}
	}
}

void ASnowboarderPawn::CaptureRenderPose()
{
	if (Sim && Appearance)
	{
		Appearance->CaptureStepPose(*Sim, CurPos, CurRot, bLastStepTeleported);
	}
}

void ASnowboarderPawn::ApplyRenderTransform(double Alpha)
{
	const FRiderState& S = Sim->State;
	const FVector P = FMath::Lerp(PrevPos, CurPos, Alpha);
	const FQuat Q = FQuat::Slerp(PrevRot, CurRot, Alpha).GetNormalized();
	const FVector Fwd = Q.RotateVector(FVector(0, 1, 0));   // body forward (nose)
	const FVector Up = Q.RotateVector(FVector(0, 0, 1));
	SetActorLocationAndRotation(SSXSim::GameToUnreal(P), SSXSim::RiderBodyToUnrealRotation(Fwd, Up));
	RenderVelocity = SSXSim::GameToUnreal(S.Velocity);

	FRotator Pose = FRotator::ZeroRotator;
	Pose.Roll = float(FMath::Clamp(-0.5 * S.Lean.Cur, -35.0, 35.0));   // placeholder pose: lean (deg, + with negative steer) rolls the body about the board axis
	BodyRoot->SetRelativeRotation(Pose);
	const float Crouch = float(FMath::Clamp(S.Crouch.Cur, 0.0, 1.0));
	BodyRoot->SetRelativeScale3D(FVector(1.f, 1.f, 1.f - 0.3f * Crouch));
	Appearance->SetGoofy(S.bStanceGoofy);
	if (!Appearance->PresentPose(Alpha) && Appearance->CaptureStepPose(*Sim, CurPos, CurRot, true))
	{
		Appearance->PresentPose(1.0);
	}
	// 0x11F620, 0x1364D0
	const bool bFadedOut = S.RenderAlpha <= 0.0;
	if (bFadedOut != bHiddenByResetFade)
	{
		bHiddenByResetFade = bFadedOut;
		SetActorHiddenInGame(bFadedOut);
	}
}

void ASnowboarderPawn::InitCamera()
{
	if (bAIRider)
	{
		return;
	}
	CameraDefs = MakeUnique<FSSXCameraDefSet>();
	CameraDefs->LoadForTrack(TrackName);
	if (!CameraDefs->Find(SSXCamera::ModeDefinition(4)))
	{
		UE_LOG(LogSSX, Warning, TEXT("[pawn] no camera data in SimData/cameras (run tools/export_cameras.py): camera not driven"));
		CameraDefs.Reset();
		return;
	}
	RaceCamera = MakeUnique<FSSXRaceCamera>(CameraDefs.Get());
	ReplayCamera = MakeUnique<FSSXReplayCamera>(CameraDefs.Get());
	// 0x180030, 0x32CA38, 0x173EB0
	if (const USSXFrontEndSubsystem* FrontEnd = USSXFrontEndSubsystem::Get(this); FrontEnd && FrontEnd->bHasSelection)
	{
		const int32 View = FrontEnd->Profile.Settings.Camera1P;
		RaceCamera->SetViewIndex(View >= 0 && View < SSXCamera::ViewCount ? View : 1);
	}
	// 0x16FE60, FUN_0025d7f0
	RaceCamera->LineQuery = [this](const FVector& A, const FVector& B, FVector& OutPoint, FVector& OutNormal)
	{
		const IRiderWorld* W = CollisionWorld ? static_cast<const IRiderWorld*>(CollisionWorld.Get()) : PatchWorld.Get();
		FRiderWorldHit Hit;
		if (!W || !W->LineQuery(A, B, ERiderLineQuery::FineRefined, 0.0, Hit))
		{
			return false;
		}
		OutPoint = Hit.Point;
		OutNormal = Hit.Normal;
		return true;
	};
	ReplayCamera->GetCam().LineQuery = RaceCamera->LineQuery;
}

void ASnowboarderPawn::ResetCamera()
{
	if (!RaceCamera || !Sim)
	{
		return;
	}
	const int32 View = RaceCamera->GetViewIndex();
	RaceCamera->Reset();
	if (View != RaceCamera->GetViewIndex())
	{
		RaceCamera->SetViewIndex(View);
	}
	StepCamera(true);
	UpdateCamera(1.0);
}

bool ASnowboarderPawn::GetCameraInput(FSSXCameraRiderInput& Out)
{
	if (!Sim)
	{
		return false;
	}
	const FRiderState& S = Sim->State;
	TArray<FVector, TInlineAllocator<19>> Bones;
	FVector BoardPos = FVector::ZeroVector;
	const bool bPosed = Appearance && Appearance->ComputeCameraPoints(*Sim, Bones, BoardPos) && Bones.Num() > 0;
	Out = FSSXCameraRiderInput::FromState(S, bPosed ? Bones[0] : SSXCamera::RiderAnchor(S, Sim->Profile, RenderQuat(S)));
	Out.Character = Character;
	if (bPosed)
	{
		Out.Bones = MoveTemp(Bones);
		Out.BoardPos = BoardPos;
		Out.bHasBoard = true;
	}
	return true;
}

void ASnowboarderPawn::StepCamera(bool bTeleported)
{
	if (!RaceCamera || !Sim)
	{
		return;
	}
	// 0x1784D0, 0x3368B0
	FSSXRaceCamera* Cam = (ReplayCamera && ReplayCamera->IsActive()) ? &ReplayCamera->GetCam() : RaceCamera.Get();
	if (bTeleported)
	{
		Cam->MarkCut();            // FUN_00177db0, FUN_0011a098
	}
	FSSXCameraRiderInput In;
	ASnowboarderPawn* Subject = ReplayCameraSubject ? ReplayCameraSubject() : nullptr;
	if (Subject != LastCameraSubject)
	{
		LastCameraSubject = Subject;   // 0x178BE8, 0x1786D0
	}
	(Subject ? Subject : this)->GetCameraInput(In);
	Cam->Step(In);
	CamPrev = CamCur;
	CamCur.Pos = Cam->GetPosition();
	CamCur.Yaw = Cam->GetYaw();
	CamCur.Pitch = Cam->GetPitch();
	CamCur.Roll = Cam->GetRoll();
	CamCur.Fov = Cam->GetFov();
	// 0x1DC3E0
	CamCur.Near = Cam->GetRenderNear(RacePhaseForCamera && RacePhaseForCamera() == uint8(ERacePhase::PreRace) ? 100.0 : 10.0);
	CamCur.Flash = Cam->GetRenderFlash();
	if (bTeleported)
	{
		CamPrev = CamCur;
	}
}

void ASnowboarderPawn::UpdateCamera(double Alpha)
{
	if (!RaceCamera || !Camera)
	{
		return;
	}
	// The camera runs at 60 Hz with the sim; rendering interpolates its last two outputs like the rider.
	const double A = FMath::Clamp(Alpha, 0.0, 1.0);
	const FVector P = FMath::Lerp(CamPrev.Pos, CamCur.Pos, A);
	const double Yaw = CamPrev.Yaw + SSXCamera::Wrap(CamCur.Yaw - CamPrev.Yaw) * A;
	const double Pitch = CamPrev.Pitch + SSXCamera::Wrap(CamCur.Pitch - CamPrev.Pitch) * A;
	const double Roll = CamPrev.Roll + SSXCamera::Wrap(CamCur.Roll - CamPrev.Roll) * A;
	const double Fov = FMath::Lerp(CamPrev.Fov, CamCur.Fov, A);
	FVector Fwd, Up;
	FSSXRaceCamera::ViewAxes(Yaw, Pitch, Roll, Fwd, Up);
	const FRotator Rot = FRotationMatrix::MakeFromXZ(SSXSim::GameToUnreal(Fwd), SSXSim::GameToUnreal(Up)).Rotator();
	Camera->SetWorldLocationAndRotation(SSXSim::GameToUnreal(P), Rot);
	// FUN_0017f390, 0x1DC3E0
	const double Half = FMath::Clamp(Fov / 2.0, 0.054831136, 1.3962634);
	const USSXFrontEndSubsystem* FrontEndSub = USSXFrontEndSubsystem::Get(this);
	const int32 Ws = FrontEndSub ? FrontEndSub->Profile.Settings.Widescreen : 0;
	Camera->SetConstraintAspectRatio(true);
	Camera->SetAspectRatio(SSXSettings::ImageAspect(Ws));
	SSXFeWide::SetImageAspect(SSXSettings::ImageAspect(Ws));
	Camera->SetFieldOfView(float(FMath::RadiansToDegrees(SSXSettings::HorizontalFov(Half, Ws))));
	// 0x1E0FB8
	const double Flash = A < 0.5 ? CamPrev.Flash : CamCur.Flash;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager)
	{
		if (Flash > 0.0)
		{
			PC->PlayerCameraManager->SetManualCameraFade(float(Flash), FLinearColor::Black, false);
			bCameraFadeApplied = true;
		}
		else if (bCameraFadeApplied)
		{
			PC->PlayerCameraManager->StopCameraFade();
			bCameraFadeApplied = false;
		}
	}
}

void ASnowboarderPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	if (RaceCamera && Camera && Camera->IsActive())
	{
		OutResult.PerspectiveNearClipPlane = float(CamCur.Near);   // slot+0x104 = max(C[7], 1) cm (game cm = UE cm)
	}
}

void ASnowboarderPawn::TrackTricks(float DeltaSeconds)
{
	TrickTextTime -= DeltaSeconds;
	const FRiderState& S = Sim->State;
	if (S.Motion == ERiderMotion::Air)
	{
		if (LastMotion != ERiderMotion::Air)
		{
			AirSpinMax = AirFlipMax = 0.0;
			AirGrab = FRiderInputWord::NoTrick;
		}
		AirSpinMax = FMath::Max(AirSpinMax, FMath::Abs(S.SpinAngle));
		AirFlipMax = FMath::Max(AirFlipMax, FMath::Abs(S.FlipAngle));
		if (S.TrickId >= 0 && S.TrickId < FRiderInputWord::NoTrick)
		{
			AirGrab = S.TrickId;
		}
	}
	else if (LastMotion == ERiderMotion::Air)
	{
		int32 LandFrame = 0;
		FString Text;
		if (SSXRiderSetup::LastLanding(Driver->GetRegistry(), LandFrame, Text) && LandFrame >= S.Frame - 5)
		{
			TrickText = Text;
		}
		else if (S.Motion == ERiderMotion::WipeOut)
		{
			TrickText = TEXT("Wipeout!");
		}
		else
		{
			TArray<FString> Parts;
			const int32 Spin = FMath::RoundToInt(FMath::RadiansToDegrees(AirSpinMax) / 180.0) * 180;
			const int32 Flips = FMath::RoundToInt(FMath::RadiansToDegrees(AirFlipMax) / 360.0);
			if (Spin >= 180) { Parts.Add(FString::Printf(TEXT("%d"), Spin)); }
			if (Flips > 0) { Parts.Add(FString::Printf(TEXT("%dx flip"), Flips)); }
			if (AirGrab != FRiderInputWord::NoTrick) { Parts.Add(FString::Printf(TEXT("grab #%d"), AirGrab)); }
			if (Parts.Num() == 0) { Parts.Add(TEXT("Air")); }
			TrickText = FString::Join(Parts, TEXT(" + ")) + FString::Printf(TEXT("   (%s)"), LexToString(S.Control));
		}
		TrickTextTime = 2.5f;
	}
	LastMotion = S.Motion;
}

const FRiderState* ASnowboarderPawn::GetSimState() const
{
	return Sim ? &Sim->State : nullptr;
}

double ASnowboarderPawn::GetSpeedKmh() const
{
	return Sim ? Sim->State.Velocity.Size() * 0.036 : 0.0;   // cm/s -> km/h
}

uint32 ASnowboarderPawn::GetLastWord() const
{
	return Driver ? Driver->GetLastWord() : 0u;
}

int32 ASnowboarderPawn::GetLastWallContacts() const
{
	return CollisionWorld ? CollisionWorld->LastWallContacts : 0;
}

void ASnowboarderPawn::TickAutoTest(float DeltaSeconds)
{
	if (AutoTestClock == 0.f)
	{
		const FRiderState& S = Sim->State;
		FRiderSphereSet Shape;
		Shape.Count = 1;
		Shape.Centres[0] = S.ContactPoint + S.GroundNormal * 20.0;
		Shape.Radii[0] = 40.0;
		Shape.BoundCentre = Shape.Centres[0];
		Shape.BoundRadius = 40.0;
		FRiderWorldHit Hit;
		const bool bHit = PatchWorld->QueryWalls(Shape, true, nullptr, Hit);
		FRiderWorldHit Filtered;
		const bool bFiltered = PatchWorld->QueryWalls(Shape, false, &S.GroundNormal, Filtered);
		UE_LOG(LogSSX, Display, TEXT("[autotest] wall bridge: unfiltered hit %d kind %d surface %d depth %.2f cm n.ground %.4f; filtered hit %d (expect 1, 0, ~20, ~1, 0)"),
			bHit, Hit.Kind, Hit.Surface, Hit.T, FVector::DotProduct(Hit.Normal, S.GroundNormal), bFiltered);
	}
	AutoTestClock += DeltaSeconds;
	if (const SSXAir::FAirContext* Air = SSXAir::GetAirContext(*Sim); Air && Air->LastLanding.bValid && Air->LastLanding.Frame != AutoTestLastLanding)
	{
		const SSXAir::FAirLandingRecord& L = Air->LastLanding;
		AutoTestLastLanding = L.Frame;
		UE_LOG(LogSSX, Display, TEXT("[autotest] landing f%d: %s after %d air frames, psi %.0f phi_eff %.0f vn %.0f, class %d, fakie %d, surface %d, speed x%.2f"),
			L.Frame, SSXAir::LexToString(L.Verdict), L.AirFrames, FMath::RadiansToDegrees(L.Result.Psi), FMath::RadiansToDegrees(L.Result.PhiEff),
			L.Result.Vn, Sim->State.BoardClass, Sim->State.bStanceGoofy != (Sim->Profile.Stance != 0), L.Surface, L.Result.SpeedFactor);
	}
	if (AutoTestClock >= AutoTestNextLog)
	{
		AutoTestNextLog += 0.5f;
		const FRiderState& S = Sim->State;
		UE_LOG(LogSSX, Display, TEXT("[autotest] t=%5.1f frame=%d pos=(%8.0f %8.0f %8.0f) speed=%5.1f km/h %s/%s surf=%d h=%.2f walls=%d/%d"),
			AutoTestClock, S.Frame, S.Position.X, S.Position.Y, S.Position.Z, GetSpeedKmh(), LexToString(S.Motion),
			LexToString(S.Control), S.SurfaceId, S.GroundHeight, GetLastWallContacts(), CollisionWorld ? CollisionWorld->TotalWallQueries : 0);
		if (bRaceDriven)
		{
			FString Others;
			for (TActorIterator<ASnowboarderPawn> It(GetWorld()); It; ++It)
			{
				if (const FRiderState* O = It->GetSimState())
				{
					Others += FString::Printf(TEXT(" %s%.0f/%d"), It->bAIRider ? TEXT("ai") : TEXT("P"), It->GetSpeedKmh(), int32(O->Motion));
				}
			}
			UE_LOG(LogSSX, Display, TEXT("[autotest] racers:%s"), *Others);
		}
	}
#if !UE_BUILD_SHIPPING
	static const float ShotTimes[] = { 0.6f, 3.0f, 6.0f, 9.0f };
	if (FParse::Param(FCommandLine::Get(), TEXT("SSXAutoShot")))
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(ShotTimes); ++i)
		{
			if (AutoTestClock - DeltaSeconds < ShotTimes[i] && AutoTestClock >= ShotTimes[i])
			{
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("autotest_%d"), i), false, false);
			}
		}
	}
#endif
	if (bAutoTrick)
	{
		InjectAutoTrick(DeltaSeconds);
	}
	if (AutoTestClock > ((bAutoTrick || bRaceDriven) ? 60.f : 20.f))
	{
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}

void ASnowboarderPawn::InjectAutoTrick(float DeltaSeconds)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Sim)
	{
		return;
	}
	using namespace SSXPadButton;
	static const uint32 Grabs[] = { R1, L1, R2, L2, R1 | R2 };
	uint32 Buttons = 0;
	FVector2D Stick = FVector2D::ZeroVector;
	if (AutoTestClock >= 8.f)
	{
		const float T = FMath::Fmod(AutoTestClock - 8.f, 4.f);
		const int32 Cycle = int32((AutoTestClock - 8.f) / 4.f);
		if (T >= 1.5f && T < 2.25f) { Buttons = Cross; Stick = FVector2D(1.0, 0.0); }
		else if (T >= 2.25f && T < 2.85f) { Buttons = Grabs[Cycle % UE_ARRAY_COUNT(Grabs)]; Stick = FVector2D(1.0, 0.0); }
		if (PadLayout == 0 && Buttons != 0)
		{
			Buttons |= Right;
			Stick = FVector2D::ZeroVector;
		}
	}
	const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
	struct FBit { uint32 Bit; FKey Key; };
	const FBit Keys[] = { { Cross, EKeys::Gamepad_FaceButton_Bottom }, { L1, EKeys::Gamepad_LeftShoulder },
		{ R1, EKeys::Gamepad_RightShoulder }, { L2, EKeys::Gamepad_LeftTrigger }, { R2, EKeys::Gamepad_RightTrigger },
		{ Right, EKeys::Gamepad_DPad_Right } };
	for (const FBit& K : Keys)
	{
		const bool bNow = (Buttons & K.Bit) != 0, bWas = (AutoTrickButtons & K.Bit) != 0;
		if (bNow != bWas)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(K.Key, bNow ? IE_Pressed : IE_Released, bNow ? 1.f : 0.f, -1, Device));
		}
	}
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftX, IE_Axis, float(Stick.X), 1, Device));
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftY, IE_Axis, float(Stick.Y), 1, Device));
	AutoTrickButtons = Buttons;
	AutoTrickStick = Stick;

	const FRiderState& S = Sim->State;
	const bool bNewFrame = S.Frame != AutoTrickLastFrame;
	AutoTrickLastFrame = S.Frame;
	if (bNewFrame && (S.Motion != AutoTrickLastMotion || S.Motion == ERiderMotion::Air || S.Control == ERiderControl::Prewind))
	{
		UE_LOG(LogSSX, Display, TEXT("[autotrick] t=%5.2f f%d inject %04x pad %04x word %08x %s/%s prewind %+.2f spin %+6.1f flip %+6.1f trick %d v %.0f"),
			AutoTestClock, S.Frame, Buttons, PendingPad.Buttons | 0u, GetLastWord(), LexToString(S.Motion), LexToString(S.Control),
			S.PrewindA.Cur, FMath::RadiansToDegrees(S.SpinAngle), FMath::RadiansToDegrees(S.FlipAngle), S.TrickId, S.Velocity.Size());
	}
	if (AutoTrickLastMotion == ERiderMotion::Air && S.Motion != ERiderMotion::Air)
	{
		int32 LandFrame = 0;
		FString Text;
		SSXRiderSetup::LastLanding(Driver ? Driver->GetRegistry() : nullptr, LandFrame, Text);
		const SSXAir::FAirContext* Air = SSXAir::GetAirContext(*Sim);
		int32 Live = 0;
		if (Air)
		{
			for (const FRiderMessage& M : Air->Score.Messages.Slots) { Live += M.Type != 0 ? 1 : 0; }
		}
		UE_LOG(LogSSX, Display, TEXT("[autotrick] landed: %s / HUD feed: %s, trick text '%s', %d messages, score %d"), *Text,
			Air ? TEXT("ok") : TEXT("MISSING"), Air ? *Air->Score.TrickText.TrimEnd() : TEXT(""), Live,
			Air ? Air->Score.Score : 0);
	}
	AutoTrickLastMotion = S.Motion;
}
