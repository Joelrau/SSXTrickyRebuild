// 0x127918
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Input/SSXDualSense.h"
#include "Input/SSXInputSetup.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "EnhancedActionKeyMapping.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Layout/Geometry.h"
#include "Slate/SceneViewport.h"
#include "UObject/Package.h"
#include "ViewportClient.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FShoveResult
	{
		int32 Anim = 0;
		double AxisDotNose = 0.0;
		uint8 PadRY = 128;
		double MapperFwd = 0.0;
	};

	FShoveResult RunShove(const FSSXPadHardware& Pressed, bool bGoofy, int32 Layout)
	{
		FShoveResult R;
		FPatchWorld World;
		SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);
		FRiderSim Sim;
		FRiderInputDriver Input;
		SSXRiderSetup::InstallStates(Sim, Input, false);
		Sim.SetWorld(&World);
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, 0, 0);
		Sim.Profile.Stance = bGoofy ? 1 : 0;
		Sim.Reset();
		Sim.Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = 800.0 * Sim.State.ContactForward;
		Sim.State.bStanceGoofy = bGoofy;
		FPadMapper Mapper(0, 0);
		Mapper.SetLayout(Layout);
		FSSXPadDevice Pad;
		for (int32 F = 0; F < 40 && R.Anim == 0; ++F)
		{
			const FSSXPadHardware Hw = (F >= 20 && F < 24) ? Pressed : SSXInput::HardwareFromUE(0, FVector2D::ZeroVector, FVector2D::ZeroVector);
			const FPadActions A = Mapper.Step(Pad.Update(Hw));
			Input.Step(Sim, A);
			if (Sim.State.ShoveAnim != 0)
			{
				R.Anim = Sim.State.ShoveAnim;
				R.AxisDotNose = FVector::DotProduct(Sim.State.ShoveAxis, Sim.State.BasisForward);
				R.PadRY = Hw.RY;
				R.MapperFwd = A.Axes[FPadMapper::AShoveFwd];
			}
		}
		return R;
	}

	FShoveResult RunDualSenseShove(const FVector2D& RightUE, bool bGoofy, int32 Layout)
	{
		return RunShove(SSXInput::HardwareFromUE(0, FVector2D::ZeroVector, RightUE), bGoofy, Layout);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInputDualSenseShoveTest, "SSX.Input.DualSenseShove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXInputDualSenseShoveTest::RunTest(const FString& Parameters)
{
	using namespace SSXPad;
	TestTrue(TEXT("RightAnalogY = Gamepad_RightY"), FKey(FGamepadKeyNames::RightAnalogY) == EKeys::Gamepad_RightY);
	TestTrue(TEXT("LeftAnalogY = Gamepad_LeftY"), FKey(FGamepadKeyNames::LeftAnalogY) == EKeys::Gamepad_LeftY);
	if (const TSharedPtr<FKeyDetails> D = EKeys::GetKeyDetails(EKeys::Gamepad_RightY))
	{
		TestTrue(TEXT("Gamepad_RightY is the Y of Gamepad_Right2D"), D->GetPairedAxis() == EPairedAxis::Y && D->GetPairedAxisKey() == EKeys::Gamepad_Right2D);
	}
	if (const TSharedPtr<FKeyDetails> D = EKeys::GetKeyDetails(EKeys::Gamepad_RightX))
	{
		TestTrue(TEXT("Gamepad_RightX is the X of Gamepad_Right2D"), D->GetPairedAxis() == EPairedAxis::X && D->GetPairedAxisKey() == EKeys::Gamepad_Right2D);
	}

	struct FLayout { const TCHAR* Name; int32 Len; bool bBt; int32 Sticks; uint8 Id; };
	const FLayout Layouts[] = { { TEXT("USB 0x01 full"), 64, false, 1, 0x01 }, { TEXT("BT 0x31 full"), 78, true, 2, 0x31 },
	                            { TEXT("BT 0x01 simple"), 10, true, 1, 0x01 } };
	FString Log;
	for (const FLayout& L : Layouts)
	{
		for (const bool bUp : { true, false })
		{
			uint8 Report[78] = {};
			Report[0] = L.Id;
			Report[L.Sticks + 0] = 128; Report[L.Sticks + 1] = 128; Report[L.Sticks + 2] = 128; Report[L.Sticks + 3] = bUp ? 0 : 255;
			Report[L.Sticks + (L.Id == 0x01 && L.bBt ? 4 : 7)] = 0x08;
			const FSonyPadState S = ParseReport(Report, L.Len, 0x0CE6, L.bBt);
			FVector2D LeftUE, RightUE;
			SticksToUE(S, LeftUE, RightUE);
			const FString Tag = FString::Printf(TEXT("%s right stick %s"), L.Name, bUp ? TEXT("up") : TEXT("down"));
			TestTrue(Tag + TEXT(": parsed"), S.bValid && S.RY == (bUp ? 0 : 255) && S.RX == 128 && S.LY == 128);
			TestTrue(Tag + TEXT(": UE +Y = up"), bUp ? RightUE.Y > 0.99 : RightUE.Y < -0.99);
			TestTrue(Tag + TEXT(": UE X centred, left centred"), FMath::Abs(RightUE.X) < 0.01 && LeftUE.Size() < 0.01);
			for (int32 Layout = 0; Layout < 2; ++Layout)
			{
				for (const bool bGoofy : { false, true })
				{
					const FShoveResult R = RunDualSenseShove(RightUE, bGoofy, Layout);
					const FString T = Tag + FString::Printf(TEXT(" layout %d %s"), Layout, bGoofy ? TEXT("goofy") : TEXT("regular"));
					TestEqual(T + TEXT(": PS2 pad RY byte (0 = up)"), int32(R.PadRY), bUp ? 0 : 255);
					TestEqual(T + TEXT(": shove clip"), R.Anim, bUp ? SSXAnim::ShoveCombatFwd : SSXAnim::ShoveCombatBwd);
					TestTrue(T + TEXT(": attack axis toward the nose (up) / tail (down)"), bUp ? R.AxisDotNose > 0.99 : R.AxisDotNose < -0.99);
					Log += FString::Printf(TEXT("%s: pad RY %u, mapper fwd %.3f, clip %#x, axis . nose %.3f\n"), *T, R.PadRY, R.MapperFwd, R.Anim, R.AxisDotNose);
				}
			}
		}
	}

	// left stick up through the same chain: UE +Y, PS2 byte 0, mapper crouch (tuck) > 0 and no brake
	{
		uint8 Report[78] = { 0x31, 0x00, 128, 0, 128, 128, 0, 0, 0, 0x08 };
		const FSonyPadState S = ParseReport(Report, 78, 0x0CE6, true);
		FVector2D LeftUE, RightUE;
		SticksToUE(S, LeftUE, RightUE);
		const FSSXPadHardware Hw = SSXInput::HardwareFromUE(0, LeftUE, RightUE);
		FSSXPadDevice Pad;
		FPadMapper Mapper(0, 0);
		const FPadActions A = Mapper.Step(Pad.Update(Hw));
		TestTrue(TEXT("left stick up: UE +Y, PS2 LY 0"), LeftUE.Y > 0.99 && Hw.LY == 0);
		TestTrue(TEXT("left stick up: crouch, no brake"), A.Axes[FPadMapper::ACrouch] > 0.9 && A.Axes[FPadMapper::ABrake] == 0.0);
	}
	AddInfo(Log);
	return true;
}

// 0x127918
namespace
{
	struct FCaptureViewportClient : public FViewportClient
	{
		float Value = 0.f;
		bool bGot = false;
		virtual bool InputAxis(const FInputKeyEventArgs& Args) override { Value = Args.AmountDepressed; bGot = true; return true; }
	};

	float ThroughSceneViewport(const FKey& Key, float Value, bool& bOk)
	{
		FCaptureViewportClient Client;
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			FSceneViewport Viewport(&Client, TSharedPtr<SViewport>());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			Viewport.OnAnalogValueChanged(FGeometry(), FAnalogInputEvent(Key, FModifierKeysState(),
				IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), false, 0, 0, Value));
		}
		bOk = bOk && Client.bGot;
		return Client.Value;
	}

	FVector2D ThroughContext(const FSSXInputBindings& B, const UInputAction* Action, const FKey& Key, const FVector& KeyValue, bool& bOk)
	{
		for (const FEnhancedActionKeyMapping& M : B.Context->GetMappings())
		{
			if (M.Action == Action && M.Key == Key)
			{
				FInputActionValue V(EInputActionValueType::Axis2D, KeyValue);
				for (const TObjectPtr<UInputModifier>& Mod : M.Modifiers) { V = Mod->ModifyRaw(nullptr, V, 1.f / 60.f); }
				for (const TObjectPtr<UInputModifier>& Mod : Action->Modifiers) { V = Mod->ModifyRaw(nullptr, V, 1.f / 60.f); }
				return V.Get<FVector2D>();
			}
		}
		bOk = false;
		return FVector2D::ZeroVector;
	}

	// a pad's four stick values as Slate gets them (+Y up) -> scene viewport -> IMC -> PS2 pad hardware
	FSSXPadHardware PadThroughLivePath(const FSSXInputBindings& B, const FVector2D& LeftSlate, const FVector2D& RightSlate, bool& bOk)
	{
		const FVector Left(ThroughSceneViewport(EKeys::Gamepad_LeftX, LeftSlate.X, bOk), ThroughSceneViewport(EKeys::Gamepad_LeftY, LeftSlate.Y, bOk), 0.0);
		const FVector Right(ThroughSceneViewport(EKeys::Gamepad_RightX, RightSlate.X, bOk), ThroughSceneViewport(EKeys::Gamepad_RightY, RightSlate.Y, bOk), 0.0);
		return SSXInput::HardwareFromUE(0, ThroughContext(B, B.LeftStick, EKeys::Gamepad_Left2D, Left, bOk),
			ThroughContext(B, B.RightStick, EKeys::Gamepad_Right2D, Right, bOk));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInputLivePathSignsTest, "SSX.Input.LivePathSigns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXInputLivePathSignsTest::RunTest(const FString& Parameters)
{
	using namespace SSXPad;
	FSSXInputBindings B;
	SSXInput::CreateBindings(GetTransientPackage(), B);
	if (!TestNotNull(TEXT("IMC_SSXPad"), B.Context.Get()))
	{
		return false;
	}
	{
		bool bOk = true;
		TestEqual(TEXT("viewport: Gamepad_RightY negated"), ThroughSceneViewport(EKeys::Gamepad_RightY, 0.75f, bOk), -0.75f);
		TestEqual(TEXT("viewport: Gamepad_LeftY kept"), ThroughSceneViewport(EKeys::Gamepad_LeftY, 0.75f, bOk), 0.75f);
		TestEqual(TEXT("viewport: Gamepad_RightX kept"), ThroughSceneViewport(EKeys::Gamepad_RightX, 0.75f, bOk), 0.75f);
		TestTrue(TEXT("viewport delivered the axis events"), bOk);
	}
	FString Log;
	for (const bool bUp : { true, false })
	{
		struct FDevice { FString Name; FSSXPadHardware Hw; };
		TArray<FDevice> Devices;
		bool bOk = true;
		{
			uint8 Report[78] = {};
			Report[0] = 0x31;
			Report[2] = 128; Report[3] = bUp ? 8 : 247; Report[4] = 127; Report[5] = bUp ? 8 : 247;
			Report[9] = 0x08;
			const FSonyPadState S = ParseReport(Report, 78, 0x0CE6, true);
			TestTrue(TEXT("DualSense report parsed"), S.bValid && S.RY == (bUp ? 8 : 247) && S.LY == (bUp ? 8 : 247));
			FVector2D L, R;
			SticksToUE(S, L, R);
			Devices.Add({ TEXT("DualSense BT 0x31"), PadThroughLivePath(B, L, R, bOk) });
		}
		{
			const float Y = bUp ? 32767.f / 32767.f : -32768.f / 32768.f;
			Devices.Add({ TEXT("XInput"), PadThroughLivePath(B, FVector2D(0.0, Y), FVector2D(0.0, Y), bOk) });
		}
		{
			const FVector2D R = ThroughContext(B, B.RightStick, bUp ? EKeys::I : EKeys::K, FVector(1.0, 0.0, 0.0), bOk);
			FSSXPadHardware Hw = SSXInput::HardwareFromUE(0, FVector2D::ZeroVector, R);
			Hw.LY = bUp ? 0 : 255;
			Devices.Add({ bUp ? TEXT("keyboard I") : TEXT("keyboard K"), Hw });
		}
		TestTrue(TEXT("every live-path step found its mapping / event"), bOk);
		for (const FDevice& D : Devices)
		{
			const FString Tag = D.Name + (bUp ? TEXT(" up") : TEXT(" down"));
			TestTrue(Tag + TEXT(": PS2 pad RY (0 = up)"), bUp ? D.Hw.RY <= 10 : D.Hw.RY >= 245);
			TestTrue(Tag + TEXT(": PS2 pad LY (0 = up)"), bUp ? D.Hw.LY <= 10 : D.Hw.LY >= 245);
			TestTrue(Tag + TEXT(": PS2 pad RX centred"), FMath::Abs(int32(D.Hw.RX) - 128) <= 1);
			for (int32 Layout = 0; Layout < 2; ++Layout)
			{
				for (const bool bGoofy : { false, true })
				{
					FSSXPadHardware Right = D.Hw;
					Right.LX = Right.LY = 128;
					const FShoveResult R = RunShove(Right, bGoofy, Layout);
					const FString T = Tag + FString::Printf(TEXT(" layout %d %s"), Layout, bGoofy ? TEXT("goofy") : TEXT("regular"));
					TestEqual(T + TEXT(": shove clip"), R.Anim, bUp ? SSXAnim::ShoveCombatFwd : SSXAnim::ShoveCombatBwd);
					TestTrue(T + TEXT(": attack axis toward the nose (up) / tail (down)"), bUp ? R.AxisDotNose > 0.99 : R.AxisDotNose < -0.99);
					Log += FString::Printf(TEXT("%s: pad LY %u RY %u, mapper fwd %.3f, clip %#x, axis . nose %.3f\n"), *T, D.Hw.LY, D.Hw.RY, R.MapperFwd, R.Anim, R.AxisDotNose);
				}
			}
		}
	}
	AddInfo(Log);
	return true;
}

#endif
