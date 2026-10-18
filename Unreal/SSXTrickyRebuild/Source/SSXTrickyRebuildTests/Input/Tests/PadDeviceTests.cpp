#include "Input/InpFile.h"
#include "Input/PadMapper.h"
#include "Input/SSXInputSetup.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnPadDeviceTest, "SSX.Pawn.PadDevice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXPawnPadDeviceTest::RunTest(const FString& Parameters)
{
	namespace PB = SSXPadButton;

	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(FInpFile::UnpackedDir(), TEXT("TUTORIAL")), TEXT("*.inp"), true, false);
		Files.Sort();
		int64 Total = 0, Ok = 0, BitExact = 0, StickBitFrames = 0, StickBitDpadZero = 0;
		int32 Bad = 0;
		for (const FString& Path : Files)
		{
			TArray<FInpFrame> Frames;
			if (!FInpFile::Load(Path, Frames))
			{
				AddError(FString::Printf(TEXT("%s: unreadable"), *Path));
				continue;
			}
			FSSXPadDevice Dev;
			for (int32 K = 0; K < Frames.Num(); ++K)
			{
				const FSSXPadFrame& R = Frames[K].Pad;
				FSSXPadHardware Hw;
				Hw.Buttons = R.ButtonsFiltered & 0xFFFFu;
				Hw.LX = FSSXPadDevice::ByteFromUnit(R.LeftX);
				Hw.LY = FSSXPadDevice::ByteFromUnit(R.LeftY);
				Hw.RX = FSSXPadDevice::ByteFromUnit(R.RightX);
				Hw.RY = FSSXPadDevice::ByteFromUnit(R.RightY);
				Dev.Update(Hw);
				if (K == 0)
				{
					Dev.Frame.Buttons = R.Buttons;
				}
				const FSSXPadFrame& D = Dev.Frame;
				auto Ulp = [](float A, float B) { int32 X, Y; FMemory::Memcpy(&X, &A, 4); FMemory::Memcpy(&Y, &B, 4); return FMath::Abs(X - Y); };
				const int32 MaxUlp = FMath::Max(FMath::Max(Ulp(D.LeftX, R.LeftX), Ulp(D.LeftY, R.LeftY)), FMath::Max(Ulp(D.RightX, R.RightX), Ulp(D.RightY, R.RightY)));
				const bool bGood = D.Buttons == R.Buttons && D.ButtonsFiltered == R.ButtonsFiltered && D.PadX == R.PadX
					&& D.PadY == R.PadY && MaxUlp <= 1;
				++Total;
				Ok += bGood ? 1 : 0;
				BitExact += (bGood && MaxUlp == 0) ? 1 : 0;
				if (!bGood && ++Bad <= 5)
				{
					AddError(FString::Printf(TEXT("%s f%d: prev %x/%x word %x/%x pad %g,%g/%g,%g left %g,%g/%g,%g"),
						*FPaths::GetBaseFilename(Path), K, D.Buttons, R.Buttons, D.ButtonsFiltered, R.ButtonsFiltered,
						D.PadX, D.PadY, R.PadX, R.PadY, D.LeftX, D.LeftY, R.LeftX, R.LeftY));
				}
				if (R.ButtonsFiltered & (PB::LJoyUp | PB::LJoyDown | PB::LJoyLeft | PB::LJoyRight))
				{
					++StickBitFrames;
					StickBitDpadZero += (R.PadX == 0.f && R.PadY == 0.f) ? 1 : 0;
				}
			}
		}
		const FString Msg = FString::Printf(TEXT("tutorial pad blocks rebuilt by FSSXPadDevice: %lld / %lld frames (%d files; %lld bit-exact, the rest a stick float 1 ulp off); %lld frames with a left-stick bit, d-pad axes 0 on %lld"),
			Ok, Total, Files.Num(), BitExact, StickBitFrames, StickBitDpadZero);
		AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[paddevice] %s"), *Msg);
		TestTrue(TEXT("tutorial files present (unpacked/TUTORIAL)"), Files.Num() == 360);
		TestEqual(TEXT("every tutorial pad block"), Ok, Total);
		TestEqual(TEXT("stick past 0.5 never moves the d-pad axes"), StickBitDpadZero, StickBitFrames);
	}

	{
		const FString Csv = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("pad_live_prewind.csv"));
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Csv) || Lines.Num() < 2)
		{
			AddError(FString::Printf(TEXT("%s missing (run tools/make_pad_reference.py)"), *Csv));
		}
		else
		{
			auto Prewind = [](int32 Layout, uint32 Buttons, uint8 LX)
			{
				FSSXPadDevice Dev;
				FSSXPadHardware Hw;
				Hw.Buttons = Buttons;
				Hw.LX = LX;
				FPadMapper M(10, 2);
				M.SetLayout(Layout);
				return FPadMapper::DecodeWord(ERiderControl::Prewind, FPadMapper::EncodeWord(ERiderControl::Prewind, M.Step(Dev.Update(Hw))));
			};
			TSet<float> Turn0, Turn1;
			bool bStickWinds1 = false, bStickWinds0 = false;
			for (int32 B = 0; B < 256; ++B)
			{
				const FRiderInputWord W0 = Prewind(0, 0, uint8(B)), W1 = Prewind(1, 0, uint8(B));
				Turn0.Add(float(W0.PrewindTurn));
				Turn1.Add(float(W1.PrewindTurn));
				bStickWinds0 |= W0.PrewindSpin != 0.0;
				bStickWinds1 |= W1.PrewindSpin != 0.0;
			}
			int32 Frames = 0, Dpad = 0, DpadOk = 0, Stick = 0, Both = 0, StickOk0 = 0, StickOk1 = 0;
			for (int32 I = 1; I < Lines.Num(); ++I)
			{
				TArray<FString> F;
				Lines[I].ParseIntoArray(F, TEXT(","));
				if (F.Num() < 5) { continue; }
				++Frames;
				const float A = FCString::Atof(*F[2]), Bt = FCString::Atof(*F[3]), St = FCString::Atof(*F[4]);
				const bool bDpad = FMath::Abs(A) > 0.5f || FMath::Abs(Bt) > 0.5f;
				const bool bStick = St != 0.f;
				Both += (bDpad && bStick) ? 1 : 0;
				if (bDpad)
				{
					++Dpad;
					const uint32 Buttons = (A > 0.5f ? PB::Right : 0u) | (A < -0.5f ? PB::Left : 0u) | (Bt > 0.5f ? PB::Down : 0u) | (Bt < -0.5f ? PB::Up : 0u);
					const FRiderInputWord W = Prewind(0, Buttons, 128);
					DpadOk += (float(W.PrewindSpin) == A && float(W.PrewindFlip) == Bt && W.PrewindTurn == 0.0) ? 1 : 0;
				}
				else if (bStick)
				{
					++Stick;
					StickOk0 += (Turn0.Num() > 1 && FMath::Abs(A) <= 0.5f && FMath::Abs(Bt) <= 0.5f) ? 1 : 0;
					StickOk1 += Turn1.Num() > 1 ? 1 : 0;
				}
			}
			const FString Msg = FString::Printf(TEXT("live Prewind frames %d: d-pad-driven %d (C++ BTNMAP0 d-pad reproduces the exact +-1 targets on %d), stick-steer %d (consistent with BTNMAP0 %d, BTNMAP1 %d), both %d; stick winds a prewind: BTNMAP0 %d, BTNMAP1 %d"),
				Frames, Dpad, DpadOk, Stick, StickOk0, StickOk1, Both, bStickWinds0, bStickWinds1);
			AddInfo(Msg);
			UE_LOG(LogTemp, Display, TEXT("[paddevice] %s"), *Msg);
			TestEqual(TEXT("live: 436 d-pad Prewind frames"), Dpad, 436);
			TestEqual(TEXT("live: 492 stick Prewind frames"), Stick, 492);
			TestEqual(TEXT("live: d-pad targets reproduced"), DpadOk, Dpad);
			TestEqual(TEXT("live: stick-steer frames explained by BTNMAP0"), StickOk0, Stick);
			TestEqual(TEXT("live: BTNMAP1 cannot steer in Prewind"), StickOk1, 0);
			TestEqual(TEXT("live: no frame needs d-pad and stick together"), Both, 0);
			TestFalse(TEXT("BTNMAP0: the stick never winds a prewind"), bStickWinds0);
			TestTrue(TEXT("BTNMAP1: the stick winds a prewind"), bStickWinds1);
		}
	}

	{
		FSSXPadDevice Dev;
		Dev.bKeyRepeat = true;
		FSSXPadHardware Hw;
		Hw.LX = 0;
		TArray<int32> Emits;
		for (int32 F = 0; F < 40; ++F)
		{
			Dev.Update(Hw);
			if (Dev.FeKey) { Emits.Add(F); }
		}
		TestTrue(TEXT("stick bit set, d-pad axis untouched"), (Dev.Frame.ButtonsFiltered & PB::LJoyLeft) && Dev.Frame.PadX == 0.f);
		TestEqual(TEXT("FE key from the stick"), Dev.FeLast, 0x4B00);
		TestTrue(TEXT("first repeat 15 frames after the press, then every 8"), Emits.Num() >= 4 && Emits[0] == 0 && Emits[1] == 15 && Emits[2] == 23 && Emits[3] == 31);
		Hw.LX = 64;
		Dev.Update(Hw);
		TestEqual(TEXT("strict 0.5 threshold"), Dev.Frame.ButtonsFiltered & PB::LJoyLeft, 0u);
		const FSSXPadFrame P = SSXInput::MakePad(PB::Left | PB::Right | PB::Up | PB::Down, FVector2D::ZeroVector, FVector2D::ZeroVector);
		TestTrue(TEXT("Left and Up win"), P.PadX == -1.f && P.PadY == -1.f);
		TestEqual(TEXT("libpad -> game bits (Cross 0x40 -> bit 11, Up 0x1000 -> bit 0)"), FSSXPadDevice::GameFromHardware(0x40 | 0x1000), uint32(PB::Cross | PB::Up));
		bool bBytesExact = true;
		for (int32 B = 0; B < 256; ++B)
		{
			bBytesExact &= FSSXPadDevice::ByteFromUnit(FSSXPadDevice::StickFromByte(uint8(B))) == B;
		}
		TestTrue(TEXT("stick byte round trip (a DualSense byte reaches the pad object unchanged)"), bBytesExact);
	}
	return !HasAnyErrors();
}

#endif
