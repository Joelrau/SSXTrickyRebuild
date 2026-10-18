#pragma once

#include "CoreMinimal.h"
#include "Input/PadMapper.h"
#include "SSXInputSetup.generated.h"

// Every control maps onto a virtual DualShock 2 (16 buttons, four stick bytes); d-pad and sticks stay separate.
//   PS2 pad      gamepad (XInput)   keyboard
//   d-pad        d-pad              W A S D / arrows
//   left stick   left stick         (none)
//   right stick  right stick        I J K L
//   Cross        A                  Space
//   Square       X                  Left Shift
//   Circle       B                  F
//   Triangle     Y                  G
//   L1 / R1      LB / RB            Q / E
//   L2 / R2      LT / RT            Z / C
//   L3 / R3      stick clicks       Left Ctrl / V
//   Select       View / Back        R
//   Start        Menu / Start       Enter
//   (restart)    -                  Backspace
//
class UEnhancedPlayerInput;
class UInputAction;
class UInputMappingContext;

USTRUCT()
struct FSSXInputBindings
{
	GENERATED_BODY()

	/** One boolean action per pad button, index = bit position in SSXPadButton. */
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Buttons;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LeftStick = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RightStick = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Restart = nullptr;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Context = nullptr;
};

namespace SSXInput
{
	SSXTRICKYREBUILD_API void CreateBindings(UObject* Outer, FSSXInputBindings& Out);

	SSXTRICKYREBUILD_API FSSXPadHardware ReadHardware(const UEnhancedPlayerInput* PlayerInput, const FSSXInputBindings& B);

	/** Button bits and raw UE stick values (+X right, +Y up) -> DualShock 2 poll (stick bytes, Y down). */
	SSXTRICKYREBUILD_API FSSXPadHardware HardwareFromUE(uint32 Buttons, const FVector2D& LeftUE, const FVector2D& RightUE);

	SSXTRICKYREBUILD_API FSSXPadFrame MakePad(uint32 Buttons, const FVector2D& LeftUE, const FVector2D& RightUE);

	SSXTRICKYREBUILD_API bool IsRestartHeld(const UEnhancedPlayerInput* PlayerInput, const FSSXInputBindings& B);
}
