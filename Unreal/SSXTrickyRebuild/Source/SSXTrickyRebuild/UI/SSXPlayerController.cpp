#include "UI/SSXPlayerController.h"
#include "Framework/Application/SlateApplication.h"

#include "Components/SkeletalMeshComponent.h"
#include "Input/SSXKeyBinds.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/UObjectIterator.h"
#include "SnowboarderPawn.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEndGameMode.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXRaceUIHost.h"

using namespace SSXFe;

ASSXPlayerController::ASSXPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetTickableWhenPaused(true);
}

void ASSXPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	SetInputMode(FInputModeGameOnly());
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
	Overlay = CreateWidget<USSXFeWidget>(this, USSXFeWidget::StaticClass());
	if (Overlay)
	{
		Overlay->InitRace(USSXFrontEndSubsystem::Get(this));
		Overlay->SetIsFocusable(false);
		Overlay->AddToViewport(10);
	}
}

void ASSXPlayerController::ForwardPad()
{
	if (!Overlay)
	{
		return;
	}
	// before the menus take this frame's buttons (the Continue press resumes the game below)
	if (ASnowboarderPawn* Rider = GetWorld() && GetWorld()->IsPaused() ? Cast<ASnowboarderPawn>(GetPawn()) : nullptr)
	{
		Rider->HoldPadFrame();
	}
	// 0x1A8688
	struct FMap { FKey Key; uint32 Bit; };
	static const FMap Map[] = {
		{ EKeys::Gamepad_Special_Right, PadStart }, { EKeys::Escape, PadStart },
		{ EKeys::Gamepad_Special_Left, PadSelect },
		{ EKeys::Gamepad_FaceButton_Bottom, PadCross }, { EKeys::Gamepad_FaceButton_Top, PadTriangle },
		{ EKeys::Gamepad_FaceButton_Right, PadCircle },
		{ EKeys::Gamepad_DPad_Up, PadUp }, { EKeys::Gamepad_DPad_Down, PadDown },
		{ EKeys::Gamepad_DPad_Left, PadLeft }, { EKeys::Gamepad_DPad_Right, PadRight },
	};
	constexpr uint32 PressBits = PadStart | PadSelect | PadCross | PadTriangle | PadCircle | PadUp | PadDown | PadLeft | PadRight;
	// keyboard: the KEYBINDS page
	const SSXKeyBinds::FSet& Kb = SSXKeyBinds::Current();
	uint32 Bits = 0, KeyHeld = 0;
	for (const FMap& M : Map)
	{
		if (WasInputKeyJustPressed(M.Key)) Bits |= M.Bit;
	}
	for (int32 Row = 0; Row < SSXKeyBinds::NumButtons; ++Row)
	{
		const uint32 Bit = SSXKeyBinds::Button(Row).Bit;
		for (const FKey& K : Kb.Keys[Row])
		{
			if (!K.IsValid()) continue;
			if ((Bit & PressBits) && WasInputKeyJustPressed(K)) Bits |= Bit;
			if (IsInputKeyDown(K)) KeyHeld |= Bit;
		}
	}
	// 0x100000 / 0x200000 / 0x400000 / 0x800000
	{
		static const FMap HeldMap[] = {
			{ EKeys::Gamepad_LeftShoulder, PadL1 }, { EKeys::Gamepad_RightShoulder, PadR1 },
			{ EKeys::Gamepad_LeftTrigger, PadL2 }, { EKeys::Gamepad_RightTrigger, PadR2 },
			{ EKeys::Gamepad_FaceButton_Left, PadSquare },
		};
		constexpr uint32 HeldBits = PressBits | PadL1 | PadR1 | PadL2 | PadR2 | PadSquare;
		uint32 Held = KeyHeld & HeldBits;
		for (const FMap& M : Map) { if (IsInputKeyDown(M.Key)) Held |= M.Bit; }
		for (const FMap& M : HeldMap) { if (IsInputKeyDown(M.Key)) Held |= M.Bit; }
		const float LX = GetInputAnalogKeyState(EKeys::Gamepad_LeftX), LY = GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
		const float RX = GetInputAnalogKeyState(EKeys::Gamepad_RightX), RY = -GetInputAnalogKeyState(EKeys::Gamepad_RightY);   // the scene viewport delivers Gamepad_RightY negated: back to + = up
		// stick bits past half deflection: left 0x20000 up / 0x40000 down / 0x80000 right / 0x100000 left,
		// right 0x200000 up / 0x400000 down / 0x800000 right / 0x1000000 left
		if (LY > 0.5f) Held |= 0x20000; else if (LY < -0.5f) Held |= 0x40000;
		if (LX > 0.5f) Held |= 0x80000; else if (LX < -0.5f) Held |= 0x100000;
		if (RY > 0.5f) Held |= 0x200000; else if (RY < -0.5f) Held |= 0x400000;
		if (RX > 0.5f) Held |= 0x800000; else if (RX < -0.5f) Held |= 0x1000000;
		Overlay->GetOverlays().SetPadHeld(0, Held);
	}
	if (Bits)
	{
		Overlay->GetOverlays().Pad(0, Bits);
	}
}

void ASSXPlayerController::HandleActions()
{
	if (!Overlay)
	{
		return;
	}
	switch (Overlay->GetOverlays().ConsumeAction())
	{
	case FSSXOverlayManager::EAction::Pause:
		SetPause(true);
		break;
	case FSSXOverlayManager::EAction::Resume:
		SetPause(false);
		break;
	case FSSXOverlayManager::EAction::Restart:
		SetPause(false);
		if (ASnowboarderPawn* Rider = Cast<ASnowboarderPawn>(GetPawn()))
		{
			Rider->Restart();
		}
		break;
	case FSSXOverlayManager::EAction::QuitToFrontEnd:
		SetPause(false);
		if (USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this))
		{
			Sub->ReturnToFrontEnd(2);
		}
		break;
	default:
		break;
	}
}

void ASSXPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ForwardPad();
	HandleActions();
	if (const USSXFrontEndSubsystem* Sub = USSXFrontEndSubsystem::Get(this); Sub && Sub->bMenuShots && Sub->MenuShotStage == 1)
	{
		TickMenuShots(DeltaSeconds);
	}
}

void ASSXPlayerController::TickMenuShots(float DeltaSeconds)
{
	ShotClock += DeltaSeconds;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->IsVisible()) It->SetVisibility(false, true);
	}
	FSSXOverlayManager* O = Overlay ? &Overlay->GetOverlays() : nullptr;
	switch (ShotPhase)
	{
	case 0: if (ShotClock > 4.f) { SSXMenuShots::Take(30, TEXT("Gameplay")); ++ShotPhase; } break;
	case 1: if (ShotClock > 4.6f && O) { O->Pad(0, PadStart); HandleActions(); ++ShotPhase; } break;
	case 2: if (ShotClock > 6.f) { SSXMenuShots::Take(31, TEXT("Pause")); ++ShotPhase; } break;
	case 3: if (ShotClock > 6.5f && O) { O->Pad(0, PadDown); O->Pad(0, PadDown); O->Pad(0, PadCross); ++ShotPhase; } break;
	case 4: if (ShotClock > 7.6f) { SSXMenuShots::Take(32, TEXT("PauseOptions")); ++ShotPhase; } break;
	case 5:
		if (ShotClock > 8.0f && O) { O->Pad(0, PadTriangle); for (int32 I = 0; I < 3; ++I) O->Pad(0, PadUp); O->Pad(0, PadCross); ++ShotPhase; }
		break;
	case 6: if (ShotClock > 9.0f) { SSXMenuShots::Take(33, TEXT("PauseTrickBook")); ++ShotPhase; } break;
	case 7:
		if (ShotClock > 9.4f && O) { O->Pad(0, PadTriangle); O->Pad(0, PadUp); O->Pad(0, PadCross); ++ShotPhase; }
		break;
	case 8: if (ShotClock > 10.4f) { SSXMenuShots::Take(34, TEXT("PauseQuitDialog")); ++ShotPhase; } break;
	case 9:
		if (ShotClock > 10.8f && O)
		{
			O->Pad(0, PadTriangle);
			O->Pad(0, PadTriangle);
			static FSSXRaceUIHostStub Stub;
			Stub.Phase = 5;
			Stub.Mode = 2;
			Stub.Track = 11;
			Stub.bPostRaceReady = true;
			const TCHAR* Names[] = { TEXT("Mac"), TEXT("Elise"), TEXT("Moby"), TEXT("Eddie") };
			for (int32 I = 0; I < 4; ++I)
			{
				FSSXRacerResult R;
				R.Name = Names[I];
				R.Place = I + 1;
				R.Player = I == 0 ? 0 : -1;
				R.TimeSec = 95.0 + I * 2.37;
				Stub.Results.Add(R);
			}
			SSXRaceUI::SetHost(GetWorld(), &Stub);
			++ShotPhase;
		}
		break;
	case 10: if (ShotClock > 12.5f) { SSXMenuShots::Take(35, TEXT("RaceResults")); ++ShotPhase; } break;
	case 11: if (ShotClock > 12.8f && O) { O->Pad(0, PadCross); ++ShotPhase; } break;
	case 12: if (ShotClock > 14.0f) { SSXMenuShots::Take(36, TEXT("SelectNextEvent")); ++ShotPhase; } break;
	case 13: if (ShotClock > 14.3f && O) { O->Pad(0, PadCross); ++ShotPhase; } break;
	case 14: if (ShotClock > 15.5f) { SSXMenuShots::Take(37, TEXT("SelectNextVenue")); ++ShotPhase; } break;
	case 15:
		if (ShotClock > 16.f)
		{
			++ShotPhase;
			UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		}
		break;
	default: break;
	}
}
