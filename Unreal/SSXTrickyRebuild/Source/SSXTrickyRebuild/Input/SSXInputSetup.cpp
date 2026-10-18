#include "Input/SSXInputSetup.h"

#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace SSXInput
{
	void CreateBindings(UObject* Outer, FSSXInputBindings& Out)
	{
		using namespace SSXPadButton;
		static const TCHAR* Names[NumButtons] = { TEXT("Up"), TEXT("Down"), TEXT("Right"), TEXT("Left"), TEXT("Square"),
			TEXT("Triangle"), TEXT("Circle"), TEXT("Start"), TEXT("Select"), TEXT("L1"), TEXT("R1"), TEXT("Cross"),
			TEXT("L2"), TEXT("R2"), TEXT("L3"), TEXT("R3") };
		auto MakeAction = [Outer](const FString& Name, EInputActionValueType Type)
		{
			UInputAction* A = NewObject<UInputAction>(Outer, *Name);
			A->ValueType = Type;
			return A;
		};
		Out.Buttons.Reset();
		for (int32 I = 0; I < NumButtons; ++I)
		{
			Out.Buttons.Add(MakeAction(FString(TEXT("IA_Pad_")) + Names[I], EInputActionValueType::Boolean));
		}
		Out.LeftStick = MakeAction(TEXT("IA_Pad_LeftStick"), EInputActionValueType::Axis2D);
		Out.RightStick = MakeAction(TEXT("IA_Pad_RightStick"), EInputActionValueType::Axis2D);
		Out.Restart = MakeAction(TEXT("IA_Restart"), EInputActionValueType::Boolean);

		UInputMappingContext* Ctx = NewObject<UInputMappingContext>(Outer, TEXT("IMC_SSXPad"));
		Out.Context = Ctx;
		auto Btn = [&Out, Ctx](uint32 Bit, std::initializer_list<FKey> Keys)
		{
			const int32 Index = FMath::FloorLog2(Bit);
			for (const FKey& K : Keys)
			{
				Ctx->MapKey(Out.Buttons[Index], K);
			}
		};
		Btn(Up, { EKeys::W, EKeys::Up, EKeys::Gamepad_DPad_Up });
		Btn(Down, { EKeys::S, EKeys::Down, EKeys::Gamepad_DPad_Down });
		Btn(Left, { EKeys::A, EKeys::Left, EKeys::Gamepad_DPad_Left });
		Btn(Right, { EKeys::D, EKeys::Right, EKeys::Gamepad_DPad_Right });
		Btn(Cross, { EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom });
		Btn(Square, { EKeys::LeftShift, EKeys::Gamepad_FaceButton_Left });
		Btn(Circle, { EKeys::F, EKeys::Gamepad_FaceButton_Right });
		Btn(Triangle, { EKeys::G, EKeys::Gamepad_FaceButton_Top });
		Btn(L1, { EKeys::Q, EKeys::Gamepad_LeftShoulder });
		Btn(R1, { EKeys::E, EKeys::Gamepad_RightShoulder });
		Btn(L2, { EKeys::Z, EKeys::Gamepad_LeftTrigger });
		Btn(R2, { EKeys::C, EKeys::Gamepad_RightTrigger });
		Btn(L3, { EKeys::LeftControl, EKeys::Gamepad_LeftThumbstick });
		Btn(R3, { EKeys::V, EKeys::Gamepad_RightThumbstick });
		Btn(Select, { EKeys::R, EKeys::Gamepad_Special_Left });
		Btn(Start, { EKeys::Enter, EKeys::Gamepad_Special_Right });
		Ctx->MapKey(Out.Restart, EKeys::BackSpace);

		// Sticks: raw values, no UE dead zone (the mapper applies the game's 0.38047 dead zone).
		Ctx->MapKey(Out.LeftStick, EKeys::Gamepad_Left2D);
		{
			// FSceneViewport::OnAnalogValueChanged negates Gamepad_RightY (only it) for every pad Slate delivers: undone here
			// on the pad key only, so the action is UE +Y = up for pads and keyboard.
			UInputModifierNegate* PadY = NewObject<UInputModifierNegate>(Ctx);
			PadY->bX = false;
			PadY->bY = true;
			PadY->bZ = false;
			Ctx->MapKey(Out.RightStick, EKeys::Gamepad_Right2D).Modifiers.Add(PadY);
		}
		// Keyboard right stick on I J K L.
		auto Swizzle = [Ctx](FEnhancedActionKeyMapping& M)
		{
			UInputModifierSwizzleAxis* S = NewObject<UInputModifierSwizzleAxis>(Ctx);
			S->Order = EInputAxisSwizzle::YXZ;
			M.Modifiers.Add(S);
		};
		auto Negate = [Ctx](FEnhancedActionKeyMapping& M) { M.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx)); };
		Ctx->MapKey(Out.RightStick, EKeys::L);
		Negate(Ctx->MapKey(Out.RightStick, EKeys::J));
		Swizzle(Ctx->MapKey(Out.RightStick, EKeys::I));
		{
			FEnhancedActionKeyMapping& M = Ctx->MapKey(Out.RightStick, EKeys::K);
			Swizzle(M);
			Negate(M);
		}
	}

	FSSXPadHardware HardwareFromUE(uint32 Buttons, const FVector2D& LeftUE, const FVector2D& RightUE)
	{
		// sticks through the pad's byte encoding; UE +Y is up, the PS2 byte axis is down
		FSSXPadHardware H;
		H.Buttons = Buttons & 0xFFFFu;
		H.LX = FSSXPadDevice::ByteFromUnit(float(LeftUE.X));
		H.LY = FSSXPadDevice::ByteFromUnit(float(-LeftUE.Y));
		H.RX = FSSXPadDevice::ByteFromUnit(float(RightUE.X));
		H.RY = FSSXPadDevice::ByteFromUnit(float(-RightUE.Y));
		return H;
	}

	FSSXPadFrame MakePad(uint32 Buttons, const FVector2D& LeftUE, const FVector2D& RightUE)
	{
		FSSXPadDevice D;
		FSSXPadFrame P = D.Update(HardwareFromUE(Buttons, LeftUE, RightUE));
		P.Buttons = P.ButtonsFiltered;   // stateless: no previous frame
		return P;
	}

	FSSXPadHardware ReadHardware(const UEnhancedPlayerInput* PlayerInput, const FSSXInputBindings& B)
	{
		if (!PlayerInput || B.Buttons.Num() != SSXPadButton::NumButtons)
		{
			return FSSXPadHardware();
		}
		uint32 Buttons = 0;
		for (int32 I = 0; I < SSXPadButton::NumButtons; ++I)
		{
			if (PlayerInput->GetActionValue(B.Buttons[I]).Get<bool>())
			{
				Buttons |= 1u << I;
			}
		}
		const FVector2D L = PlayerInput->GetActionValue(B.LeftStick).Get<FVector2D>();
		const FVector2D R = PlayerInput->GetActionValue(B.RightStick).Get<FVector2D>();
		return HardwareFromUE(Buttons, L, R);
	}

	bool IsRestartHeld(const UEnhancedPlayerInput* PlayerInput, const FSSXInputBindings& B)
	{
		return PlayerInput && B.Restart && PlayerInput->GetActionValue(B.Restart).Get<bool>();
	}
}
