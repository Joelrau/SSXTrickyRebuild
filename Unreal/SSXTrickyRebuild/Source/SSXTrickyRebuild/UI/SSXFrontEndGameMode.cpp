#include "UI/SSXFrontEndGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "SSXTrickyRebuild.h"
#include "Progression/SSXSettingsEffects.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeRiderActor.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEnd.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "Effects/SSXTrackEffects.h"
#include "World/SSXPropAnimation.h"
#include "UnrealClient.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFeRiders(TEXT("ssx.FeRiders"), 1,
		TEXT("Show the converted riders on the front-end carousel (0 = hidden; -SSXMenuShots hides them)."));
}

void SSXMenuShots::Take(int32 Index, const FString& Name)
{
	const FString File = FPaths::Combine(FPaths::ScreenShotDir(), FString::Printf(TEXT("SSXMenu_%02d_%s.png"), Index, *Name));
	FScreenshotRequest::RequestScreenshot(File, true, false);
	UE_LOG(LogSSX, Log, TEXT("[fe] screenshot %s"), *File);
}

ASSXFrontEndGameMode::ASSXFrontEndGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ASSXFrontEndPlayerController::StaticClass();
}

ASSXFrontEndPlayerController::ASSXFrontEndPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = false;
}

void ASSXFrontEndPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (!SceneCamera || It->ActorHasTag(TEXT("FrontEndCamera")))
		{
			SceneCamera = *It;
		}
	}
	if (!SceneCamera)
	{
		SceneCamera = GetWorld()->SpawnActor<ACameraActor>();
	}
	if (SceneCamera)
	{
		// The original frame is 4:3 (640x480); the FOV of the FE cameras applies to it.
		UCameraComponent* Cam = SceneCamera->GetCameraComponent();
		Cam->SetConstraintAspectRatio(true);
		Cam->SetAspectRatio(4.f / 3.f);
		SetViewTarget(SceneCamera);
	}

	MedalActors.Reset();
	MedalModel.Reset();
	MedalInstance.Reset();
	MedalFrameSent.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		int32 Inst = INDEX_NONE, Model = INDEX_NONE;
		for (const FName& Tag : It->Tags)
		{
			const FString T = Tag.ToString();
			if (T.StartsWith(TEXT("SSXFeMedal")) && T.Mid(10).IsNumeric()) { Model = FCString::Atoi(*T.Mid(10)); }
			if (T.StartsWith(TEXT("SSXInst")) && T.Mid(7).IsNumeric()) { Inst = FCString::Atoi(*T.Mid(7)); }
		}
		if (Inst != INDEX_NONE && Inst == SSXFeTables::MartySignInstance())
		{
			It->SetActorHiddenInGame(true);
		}
		if (Model != INDEX_NONE)
		{
			MedalActors.Add(*It);
			MedalModel.Add(Model);
			MedalInstance.Add(Inst);
			MedalFrameSent.Add(-1);
		}
	}

	USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this);
	if (Sub && Sub->bMenuShots)
	{
		CVarFeRiders->Set(0);
		if (GEngine) { GEngine->Exec(GetWorld(), TEXT("DisableAllScreenMessages")); }
	}
	Menu = CreateWidget<USSXFeWidget>(this, USSXFeWidget::StaticClass());
	if (!Menu)
	{
		return;
	}
	const bool bCold = !(Sub && Sub->bColdBootDone);
	Menu->InitFrontEnd(Sub, bCold);
	if (Sub)
	{
		Sub->bColdBootDone = true;
		Sub->ApplySettings(GetWorld());
	}
	Menu->AddToViewport();
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Menu->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	SpawnRiders();
}

void ASSXFrontEndPlayerController::SpawnRiders()
{
	for (int32 Slot = 0; Slot < SSXFeRiders::NumRiders; ++Slot)
	{
		const int32 Char = SSXFeTables::CarouselRider(Slot);
		ASSXFeRiderActor* A = GetWorld()->SpawnActor<ASSXFeRiderActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		int32 Board = 0, Outfit = 0;   // FUN_00267B18
		if (Menu) { Menu->GetFrontEnd().ModelGear(Slot, Board, Outfit); }
		if (A && !A->Setup(Slot, Char, Board, Outfit))
		{
			A->Destroy();
			A = nullptr;
		}
		if (A)
		{
			A->SetActorHiddenInGame(true);
		}
		Riders.Add(A);
	}
}

void ASSXFrontEndPlayerController::TickRiders(float DeltaSeconds)
{
	if (!Menu)
	{
		return;
	}
	FSSXFrontEnd& FE = Menu->GetFrontEnd();
	SSXFeRiders::FRing& Ring = FE.Riders;
	const SSXFeRiders::FData* Data = SSXFeRiders::FData::Shared();
	USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);

	// FUN_00267B18
	for (int32 Slot = 0; Slot < Riders.Num(); ++Slot)
	{
		if (!Riders[Slot]) continue;
		int32 Board = 0, Outfit = 0;
		FE.ModelGear(Slot, Board, Outfit);
		Riders[Slot]->SetBoardOutfit(Board, Outfit);
	}

	// cFE3DCharacter::vfunc_13 every FE frame (60 Hz): state machine, controller, light blend
	RiderClock = FMath::Min(RiderClock + DeltaSeconds, 0.25f);
	bool bStepped = false;
	while (RiderClock >= 1.f / SSXFe::FrameRate)
	{
		RiderClock -= 1.f / SSXFe::FrameRate;
		bStepped = true;
		// FUN_00261B38
		for (int32 Slot = 0; Slot < Riders.Num(); ++Slot)
		{
			if (!Riders[Slot]) continue;
			TArray<int32> Events;
			Riders[Slot]->StepFrame(Ring.Riders[Slot], Ring.GetSession(), Events);
			for (const int32 V : Events)
			{
				if (Audio && FE.bAnimEventSounds)
				{
					Audio->PlayRiderSpeech(Ring.SpeechVariant, V);
				}
			}
		}
		Ring.TickLights();
	}
	for (int32 Slot = 0; Slot < Riders.Num(); ++Slot)
	{
		if (!Riders[Slot]) continue;
		if (bStepped && Data)
		{
			Riders[Slot]->SetLights(SSXFeRiders::FLightSet::Lerp(Data->Unselected, Data->Selected, Ring.Lights[Slot].Blend));
		}
		Riders[Slot]->ApplyPose();
		// FUN_00261B38
		Riders[Slot]->SetActorHiddenInGame(!(CVarFeRiders.GetValueOnGameThread() != 0 && FE.Boot == ESSXFeBoot::FrontEnd
			&& FE.IsCarouselRiderDrawn(Slot)));
	}
}

void ASSXFrontEndPlayerController::TickMedals()
{
	if (!Menu)
	{
		return;
	}
	const FSSXFrontEnd& FE = Menu->GetFrontEnd();
	bool bShow[10] = {};
	for (int32 T : SSXFeTables::VenueOrder())
	{
		const int32 N = SSXFeCams::VenueMedalModel(T);
		bShow[N] = FE.Profile && FE.Profile->VenueMedal(FE.GameMode, T) != SSXProg::NoMedal;
	}
	for (int32 I = 0; I < MedalActors.Num(); ++I)
	{
		AActor* A = MedalActors[I].Get();
		const bool bOn = MedalModel[I] >= 0 && MedalModel[I] < 10 && bShow[MedalModel[I]];
		if (A && A->IsHidden() == bOn) { A->SetActorHiddenInGame(!bOn); }
	}
}

void ASSXFrontEndPlayerController::TickScene(float DeltaSeconds)
{
	USSXTrackEffects* Fx = USSXTrackEffects::Get(this);
	USSXPropAnimation* Props = USSXPropAnimation::Get(this);
	if (!Menu || !Fx || !Props)
	{
		return;
	}
	if (!bSceneBegun)
	{
		bSceneBegun = true;
		if (Fx->BeginTrack(TEXT("ssxfe"), 11, 0, nullptr, true))   // 0x25EDF0
		{
			// 0x25EDF0 / 0x147E60
			Fx->GetEffectWorld().LoadFrontEndFlags();
			const bool bStart = Fx->GetEffectWorld().CallFunction(TEXT("FEStartScript"));
			UE_LOG(LogSSX, Log, TEXT("[fe] FEStartScript %s"), bStart ? TEXT("started") : TEXT("not found in ssxfe.effects.bin"));
		}
	}
	if (!Fx->IsActive())
	{
		return;
	}
	FSSXFrontEnd& FE = Menu->GetFrontEnd();
	if (FE.SceneEvents.Num() < SceneEventsDone)
	{
		SceneEventsDone = 0;
	}
	for (; SceneEventsDone < FE.SceneEvents.Num(); ++SceneEventsDone)
	{
		Fx->GetEffectWorld().CallFunction(FE.SceneEvents[SceneEventsDone]);
	}
	SceneClock = FMath::Min(SceneClock + DeltaSeconds, 0.25f);
	while (SceneClock >= 1.f / SSXFe::FrameRate)
	{
		SceneClock -= 1.f / SSXFe::FrameRate;
		Fx->StepFrame();
		Props->StepFrame();
		StepVenueVideo();
	}
	TickVenueScreens();
	ApplyVenueVideo();
	Props->ApplyRender();
	Fx->ApplyRender();
}

void ASSXFrontEndPlayerController::StepVenueVideo()
{
	if (!Menu)
	{
		return;
	}
	const FSSXFrontEnd& FE = Menu->GetFrontEnd();
	if (!VenueVideo.IsValid())
	{
		VenueVideo = MakeShared<SSXFeVenueVideo::FVideo>();
	}
	SSXFeVenueVideo::FVideo& V = *VenueVideo;
	const bool bWasOpen = V.Cur >= 0;
	const bool bNew = V.Step(FE.VenueVideo, FE.VenueVideoSerial);
	if (!bWasOpen && V.Cur >= 0 && !V.bFile)
	{
		UE_LOG(LogSSX, Warning, TEXT("[fe] venue video %s missing (extracted/DATA/VIDEO)"), *SSXFeVenueVideo::Path(V.Cur));
	}
	if (V.Cur >= 0 && V.bFile && !VenueVideoTex)
	{
		// FUN_001FFE70
		const int32 N = SSXFeVenueVideo::Size;
		VenueVideoTex = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8, TEXT("SSXFeVenueVideo"));
		if (!VenueVideoTex)
		{
			return;
		}
		void* Mip = VenueVideoTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memzero(Mip, N * N * 4);
		VenueVideoTex->GetPlatformData()->Mips[0].BulkData.Unlock();
		VenueVideoTex->SRGB = true;
		VenueVideoTex->Filter = TF_Bilinear;
		VenueVideoTex->UpdateResource();
	}
	if (!bNew || !VenueVideoTex || V.W != VenueVideoTex->GetSizeX() || V.H != VenueVideoTex->GetSizeY())
	{
		return;
	}
	FTextureResource* Res = VenueVideoTex->GetResource();
	if (!Res || !Res->GetTexture2DResource() || V.Pixels.Num() != V.W * V.H)
	{
		return;
	}
	const int32 Bytes = V.W * V.H * 4;
	uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes));
	FMemory::Memcpy(Copy, V.Pixels.GetData(), Bytes);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, V.W, V.H);
	VenueVideoTex->UpdateTextureRegions(0, 1, Region, V.W * 4, 4, Copy,
		[](uint8* Data, const FUpdateTextureRegion2D* R) { FMemory::Free(Data); delete R; });
}

void ASSXFrontEndPlayerController::ApplyVenueVideo()
{
	// 0x269D10
	USSXPropAnimation* Props = USSXPropAnimation::Get(this);
	if (!Menu || !Props)
	{
		return;
	}
	const int32 Cur = VenueVideo.IsValid() && VenueVideo->bFile ? VenueVideo->Cur : -1;
	const int32 Inst = Cur >= 0 && VenueVideoTex ? SSXFeTables::VenueScreenInstance(Cur) : INDEX_NONE;
	if (Inst != VenueVideoInst && VenueVideoInst != INDEX_NONE)
	{
		Props->SetTextureOverride(VenueVideoInst, nullptr);
	}
	if (Inst != INDEX_NONE)
	{
		Props->SetTextureOverride(Inst, VenueVideoTex);
	}
	VenueVideoInst = Inst;
}

void ASSXFrontEndPlayerController::TickVenueScreens()
{
	// FUN_00269BE8
	USSXPropAnimation* Props = USSXPropAnimation::Get(this);
	if (!Menu || !Props)
	{
		return;
	}
	const FSSXFrontEnd& FE = Menu->GetFrontEnd();
	SSXProps::FPropControllers& Ctl = Props->GetControllers();
	if (FE.VenueScreenSerial != VenueScreenSerialSent)
	{
		bool bAll = true;
		for (int32 I = 0; I < 10; ++I)
		{
			const int32 Inst = SSXFeTables::VenueScreenInstance(I);
			if (Inst == INDEX_NONE || FE.VenueScreenFrame[I] < 0)
			{
				continue;
			}
			if (!Ctl.Find(Inst))
			{
				bAll = false;
				continue;
			}
			Ctl.Message(Inst, 2, FE.VenueScreenFrame[I]);
		}
		if (bAll) { VenueScreenSerialSent = FE.VenueScreenSerial; }
	}
	// FUN_00261B38
	if (FE.CarouselSerial != CarouselSerialSent)
	{
		bool bAll = true;
		for (int32 Slot = 0; Slot < SSXFeRiders::NumRiders; ++Slot)
		{
			const int32 Inst[2] = { SSXFeTables::RiderSignInstance(Slot), SSXFeTables::PlatformInstance(Slot) };
			const int32 Frame[2] = { FE.CarouselSignFrame[Slot], FE.CarouselPlatformFrame[Slot] };
			for (int32 K = 0; K < 2; ++K)
			{
				if (Inst[K] == INDEX_NONE) { continue; }
				if (!Ctl.Find(Inst[K])) { bAll = false; continue; }
				Ctl.Message(Inst[K], 2, Frame[K]);
			}
		}
		if (bAll) { CarouselSerialSent = FE.CarouselSerial; }
	}
	// FUN_00262D80
	for (int32 I = 0; I < MedalActors.Num(); ++I)
	{
		int32 Frame = -1;
		for (int32 T : SSXFeTables::VenueOrder())
		{
			if (SSXFeCams::VenueMedalModel(T) == MedalModel[I] && FE.Profile)
			{
				const int32 M = FE.Profile->VenueMedal(FE.GameMode, T);
				Frame = M >= SSXProg::Gold && M <= SSXProg::Bronze ? SSXProg::Bronze - M : -1;
			}
		}
		if (Frame >= 0 && Frame != MedalFrameSent[I] && MedalInstance[I] != INDEX_NONE && Ctl.Find(MedalInstance[I]))
		{
			MedalFrameSent[I] = Frame;
			Ctl.Message(MedalInstance[I], 2, Frame);
		}
	}
}

void ASSXFrontEndPlayerController::TickCamera()
{
	USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this);
	if (!SceneCamera || !Menu || !Sub)
	{
		return;
	}
	FSSXFrontEnd& FE = Menu->GetFrontEnd();
	if (FE.Camera.Current.IsEmpty())
	{
		// TODO: which script the Title screen holds is not stated;
		FE.Camera.Hold(TEXT("modeSelectIntro"), 0.f);
	}
	FSSXFeCamPose Pose;
	if (FE.Camera.GetPose(Sub->Cameras(), Pose))
	{
		FVector Loc;
		FRotator Rot;
		Pose.ToUnreal(Loc, Rot);
		SceneCamera->SetActorLocationAndRotation(Loc, Rot);
		// 0x2637A0
		const int32 Ws = Sub->Profile.Settings.Widescreen;
		UCameraComponent* Cam = SceneCamera->GetCameraComponent();
		Cam->SetAspectRatio(SSXSettings::ImageAspect(Ws));
		Cam->SetFieldOfView(float(FMath::RadiansToDegrees(SSXSettings::HorizontalFov(FMath::DegreesToRadians(double(Pose.HorizontalFovDeg())) * 0.5, Ws))));
	}
}

void ASSXFrontEndPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this);
	if (Menu && Sub)
	{
		FSSXFrontEnd& FE = Menu->GetFrontEnd();
		if (FE.bLaunchRequested)
		{
			FE.bLaunchRequested = false;
			Sub->PendingReplay = MoveTemp(FE.PendingReplay);   // 0x166800
			FE.PendingReplay.Reset();
			if (Sub->RequestLaunch(FE.Launch) != ESSXLoadResult::Loading)
			{
				FE.LaunchFailed();
			}
		}
		Sub->ApplySettings(GetWorld());
	}
	TickCamera();
	TickRiders(DeltaSeconds);
	TickScene(DeltaSeconds);
	TickMedals();
	if (Sub && Sub->bMenuShots)
	{
		TickMenuShots(DeltaSeconds);
	}
}

void ASSXFrontEndPlayerController::TickMenuShots(float DeltaSeconds)
{
	USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this);
	if (!Menu || !Sub)
	{
		return;
	}
	FSSXFrontEnd& FE = Menu->GetFrontEnd();
	struct FStep { const TCHAR* Name; float Hold; };
	static const FStep Steps[] = {
		{ TEXT("StartScreen"), 1.0f }, { TEXT("Title"), 1.0f }, { TEXT("ModeSelect"), 3.2f }, { TEXT("NumPlayers"), 0.8f },
		{ TEXT("EventSel"), 0.8f }, { TEXT("DifficultSel"), 0.8f }, { TEXT("CharacterSelect"), 1.0f }, { TEXT("CharacterSelectAttr"), 2.0f },
		{ TEXT("CharacterAccept"), 0.8f }, { TEXT("CharacterModify"), 0.8f }, { TEXT("FlyIn"), 2.0f }, { TEXT("CustomBoard"), 1.0f },
		{ TEXT("CustomBody"), 1.0f }, { TEXT("RiderProfile"), 0.8f }, { TEXT("TrickBook"), 0.8f }, { TEXT("UserName"), 0.8f },
		{ TEXT("VenueSel"), 4.2f }, { TEXT("VenueSelSnow"), 2.0f }, { TEXT("VenueSelMesa"), 2.0f },
		{ TEXT("DVDContent"), 0.8f }, { TEXT("Options"), 0.8f }, { TEXT("OptionsSound"), 0.8f }, { TEXT("OptionsGame"), 0.8f },
		{ TEXT("MemCardSave"), 4.5f }, { TEXT("Launch"), 0.5f },
	};
	constexpr int32 NumSteps = UE_ARRAY_COUNT(Steps);
	if (ShotStep >= NumSteps)
	{
		return;
	}
	const FString Name = Steps[ShotStep].Name;
	if (ShotClock == 0.f)
	{
		Menu->SkipMovie();
		if (Name == TEXT("StartScreen")) { /* boot screen as it is */ }
		else if (Name == TEXT("Title")) { FE.GoTo(ESSXFeScreen::Title); }
		else if (Name == TEXT("ModeSelect")) { FE.GoTo(ESSXFeScreen::ModeSelect); }
		else if (Name == TEXT("NumPlayers")) { FE.GoTo(ESSXFeScreen::NumPlayers); }
		else if (Name == TEXT("EventSel")) { FE.GoTo(ESSXFeScreen::EventSel); }
		else if (Name == TEXT("DifficultSel")) { FE.GoTo(ESSXFeScreen::DifficultSel); }
		else if (Name == TEXT("CharacterSelect")) { FE.GoTo(ESSXFeScreen::CharacterSelect); }
		else if (Name == TEXT("CharacterSelectAttr")) { /* same screen after the 120-frame ranking view */ }
		else if (Name == TEXT("CharacterAccept")) { FE.GoTo(ESSXFeScreen::CharacterAccept); }
		else if (Name == TEXT("CharacterModify")) { FE.GoTo(ESSXFeScreen::CharacterModify); }
		else if (Name == TEXT("FlyIn"))
		{
			FE.GoTo(ESSXFeScreen::CharacterModify);
			Menu->PressPad(0, SSXFe::PadCross);
		}
		else if (Name == TEXT("CustomBoard")) { FE.GoTo(ESSXFeScreen::CustomBoard); }
		else if (Name == TEXT("CustomBody")) { FE.GoTo(ESSXFeScreen::CustomBody); }
		else if (Name == TEXT("RiderProfile")) { FE.GoTo(ESSXFeScreen::RiderProfile); }
		else if (Name == TEXT("TrickBook")) { FE.GoTo(ESSXFeScreen::TrickBook); }
		else if (Name == TEXT("UserName")) { FE.GoTo(ESSXFeScreen::UserName); }
		else if (Name == TEXT("VenueSel")) { FE.GoTo(ESSXFeScreen::VenueSel); }
		else if (Name == TEXT("VenueSelSnow")) { Menu->PressPad(0, SSXFe::PadRight); }
		else if (Name == TEXT("VenueSelMesa")) { Menu->PressPad(0, SSXFe::PadRight); Menu->PressPad(0, SSXFe::PadRight); }
		else if (Name == TEXT("DVDContent")) { FE.GoTo(ESSXFeScreen::DVDContent); }
		else if (Name == TEXT("Options")) { FE.GoTo(ESSXFeScreen::Options); }
		else if (Name == TEXT("OptionsSound")) { Menu->PressPad(0, SSXFe::PadCross); }
		else if (Name == TEXT("OptionsGame"))
		{
			Menu->PressPad(0, SSXFe::PadTriangle);
			Menu->PressPad(0, SSXFe::PadDown);
			Menu->PressPad(0, SSXFe::PadDown);
			Menu->PressPad(0, SSXFe::PadCross);
		}
		else if (Name == TEXT("MemCardSave"))
		{
			FE.GoTo(ESSXFeScreen::Options);
			for (int32 I = 0; I < 3; ++I) { Menu->PressPad(0, SSXFe::PadDown); }
			Menu->PressPad(0, SSXFe::PadCross);
			Menu->PressPad(0, SSXFe::PadCross);
		}
		else if (Name == TEXT("Launch"))
		{
			Sub->MenuShotStage = 1;
			FSSXFeLaunch L;
			L.GameMode = 2;
			L.Track = 11;
			L.Slots[0].Char = 3;
			Sub->RequestLaunch(L);
			++ShotStep;
			return;
		}
		bShotTaken = false;
	}
	ShotClock += DeltaSeconds;
	if (!bShotTaken && ShotClock >= Steps[ShotStep].Hold)
	{
		SSXMenuShots::Take(ShotStep, Name);
		bShotTaken = true;
	}
	if (bShotTaken && ShotClock >= Steps[ShotStep].Hold + 0.3f)
	{
		++ShotStep;
		ShotClock = 0.f;
	}
}
