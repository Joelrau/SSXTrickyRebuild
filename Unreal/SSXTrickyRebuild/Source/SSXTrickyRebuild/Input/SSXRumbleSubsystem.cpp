#include "Input/SSXRumbleSubsystem.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Input/SSXDualSense.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr double FrameSeconds = 1.0 / 60.0;
	constexpr double RiderDrivenWindow = 0.1;   // seconds since the last RiderFrame for a port to count as rider-driven
	constexpr int32 MaxCatchUpFrames = 10;
}

USSXRumbleSubsystem* USSXRumbleSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<USSXRumbleSubsystem>() : nullptr;
}

bool USSXRumbleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

TStatId USSXRumbleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USSXRumbleSubsystem, STATGROUP_Tickables);
}

void USSXRumbleSubsystem::Deinitialize()
{
	SSXPad::SetSonyPadRumble(0, 0);
	for (int32 P = 0; P < SSXVibration::NumPorts; ++P)
	{
		if (APlayerController* PC = HandleOwner[P].Get())
		{
			PC->PlayDynamicForceFeedback(0.f, 0.f, false, false, false, false, EDynamicForceFeedbackAction::Stop, BigHandle[P]);
			PC->PlayDynamicForceFeedback(0.f, 0.f, false, false, false, false, EDynamicForceFeedbackAction::Stop, SmallHandle[P]);
		}
		BigHandle[P] = SmallHandle[P] = 0;
		HandleOwner[P] = nullptr;
	}
	Super::Deinitialize();
}

void USSXRumbleSubsystem::BeginGame(int32 Vibration1P, int32 Vibration2P)
{
	if (bGameBegun)
	{
		return;
	}
	bGameBegun = true;
	SSXVibration::ApplyRaceLoadSetting(Pads[0], Vibration1P);
	SSXVibration::ApplyRaceLoadSetting(Pads[1], Vibration2P);
	for (SSXVibration::FPlayerRumble& P : Players)
	{
		P = SSXVibration::FPlayerRumble();
	}
}

void USSXRumbleSubsystem::RiderFrame(int32 Port, TConstArrayView<FRiderPadFeedback> Calls, bool bReplayPlaying)
{
	if (Port < 0 || Port >= SSXVibration::NumPorts)
	{
		return;
	}
	SSXVibration::RiderFrame(Pads[Port], Players[Port], Calls, bReplayPlaying);
	LastRiderFrameTime[Port] = FPlatformTime::Seconds();
}

void USSXRumbleSubsystem::FrontEndOneShot(int32 Port)
{
	if (Port >= 0 && Port < SSXVibration::NumPorts)
	{
		SSXVibration::FrontEndOneShot(Pads[Port]);
	}
}

void USSXRumbleSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// the pad poll of ports no rider steps (front end, pause, no human rider), on this subsystem's own 60 Hz clock
	ClockAccum += FMath::Max(0.0, double(DeltaTime));
	int32 Frames = 0;
	while (ClockAccum >= FrameSeconds)
	{
		ClockAccum -= FrameSeconds;
		++Frames;
	}
	Frames = FMath::Min(Frames, MaxCatchUpFrames);
	const UWorld* World = GetWorld();
	const bool bPaused = World && World->IsPaused();
	const double Now = FPlatformTime::Seconds();
	for (int32 P = 0; P < SSXVibration::NumPorts; ++P)
	{
		const bool bRiderDriven = !bPaused && LastRiderFrameTime[P] >= 0.0 && Now - LastRiderFrameTime[P] < RiderDrivenWindow;
		if (!bRiderDriven)
		{
			for (int32 F = 0; F < Frames; ++F)
			{
				Pads[P].Poll();
			}
		}
	}
	const bool bActive = !FSlateApplication::IsInitialized() || FSlateApplication::Get().IsActive();
	PushOutput(!bActive);
}

void USSXRumbleSubsystem::PushOutput(bool bZero)
{
	const SSXVibration::FPadActuators& P0 = Pads[0];
	SSXPad::SetSonyPadRumble(bZero ? 0 : (P0.SentSmall ? 255 : 0), bZero ? 0 : P0.SentBig);

	// XInput pads through UE's force feedback: local player n = port n
	UWorld* World = GetWorld();
	for (int32 P = 0; P < SSXVibration::NumPorts; ++P)
	{
		APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, P) : nullptr;
		if (HandleOwner[P].Get() != PC)
		{
			if (APlayerController* Old = HandleOwner[P].Get())
			{
				Old->PlayDynamicForceFeedback(0.f, 0.f, false, false, false, false, EDynamicForceFeedbackAction::Stop, BigHandle[P]);
				Old->PlayDynamicForceFeedback(0.f, 0.f, false, false, false, false, EDynamicForceFeedbackAction::Stop, SmallHandle[P]);
			}
			BigHandle[P] = SmallHandle[P] = 0;
			HandleOwner[P] = PC;
		}
		if (!PC)
		{
			continue;
		}
		const float Big = bZero ? 0.f : Pads[P].SentBig / 255.f;
		const float Small = (bZero || !Pads[P].SentSmall) ? 0.f : 1.f;
		auto Drive = [PC](uint64& Handle, float Intensity, bool bLeftLarge, bool bRightSmall)
		{
			if (Intensity <= 0.f)
			{
				if (Handle)
				{
					PC->PlayDynamicForceFeedback(0.f, 0.f, false, false, false, false, EDynamicForceFeedbackAction::Stop, Handle);
					Handle = 0;
				}
				return;
			}
			if (!Handle)
			{
				Handle = PC->PlayDynamicForceFeedback(Intensity, -1.f, bLeftLarge, false, false, bRightSmall, EDynamicForceFeedbackAction::Start);
			}
			else
			{
				PC->PlayDynamicForceFeedback(Intensity, -1.f, bLeftLarge, false, false, bRightSmall, EDynamicForceFeedbackAction::Update, Handle);
			}
		};
		Drive(BigHandle[P], Big, true, false);
		Drive(SmallHandle[P], Small, false, true);
	}
}
