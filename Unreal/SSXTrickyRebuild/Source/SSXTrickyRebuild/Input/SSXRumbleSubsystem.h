#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Input/SSXPadVibration.h"
#include "SSXRumbleSubsystem.generated.h"

UCLASS()
class SSXTRICKYREBUILD_API USSXRumbleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static USSXRumbleSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

	// 0x17E148
	void BeginGame(int32 Vibration1P, int32 Vibration2P);
	void RiderFrame(int32 Port, TConstArrayView<FRiderPadFeedback> Calls, bool bReplayPlaying = false);
	// 0x2A9848, 0x26D1C8
	void FrontEndOneShot(int32 Port);

	const SSXVibration::FPlayerRumble& GetPlayer(int32 Port) const { return Players[FMath::Clamp(Port, 0, SSXVibration::NumPorts - 1)]; }

private:
	void PushOutput(bool bZero);

	SSXVibration::FPadActuators Pads[SSXVibration::NumPorts];
	SSXVibration::FPlayerRumble Players[SSXVibration::NumPorts];
	double LastRiderFrameTime[SSXVibration::NumPorts] = { -1.0, -1.0 };   // platform seconds of the last RiderFrame
	double ClockAccum = 0.0;
	bool bGameBegun = false;
	uint64 BigHandle[SSXVibration::NumPorts] = { 0, 0 };
	uint64 SmallHandle[SSXVibration::NumPorts] = { 0, 0 };
	TWeakObjectPtr<class APlayerController> HandleOwner[SSXVibration::NumPorts];
};
