#include "Input/SSXDualSense.h"

#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "GenericPlatform/IInputInterface.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"

#include <atomic>

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <setupapi.h>
#include <hidsdi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSSXPad, Log, All);

namespace SSXPad
{
	bool IsDualSense(uint16 Pid) { return Pid == 0x0CE6 || Pid == 0x0DF2; }
	bool IsSupportedProduct(uint16 Pid) { return IsDualSense(Pid) || Pid == 0x05C4 || Pid == 0x09CC; }

	namespace
	{
		void ReadFull(const uint8* D, FSonyPadState& S)
		{
			S.LX = D[0]; S.LY = D[1]; S.RX = D[2]; S.RY = D[3];
			S.L2Analog = D[4]; S.R2Analog = D[5];
			const uint8 B0 = D[7], B1 = D[8], B2 = D[9];
			S.Hat = B0 & 0x0F;
			S.Square = (B0 & 0x10) != 0; S.Cross = (B0 & 0x20) != 0; S.Circle = (B0 & 0x40) != 0; S.Triangle = (B0 & 0x80) != 0;
			S.L1 = (B1 & 0x01) != 0; S.R1 = (B1 & 0x02) != 0; S.L2 = (B1 & 0x04) != 0; S.R2 = (B1 & 0x08) != 0;
			S.Select = (B1 & 0x10) != 0; S.Start = (B1 & 0x20) != 0; S.L3 = (B1 & 0x40) != 0; S.R3 = (B1 & 0x80) != 0;
			S.PS = (B2 & 0x01) != 0; S.TouchPad = (B2 & 0x02) != 0;
		}
		void ReadSimple(const uint8* D, FSonyPadState& S)
		{
			S.LX = D[0]; S.LY = D[1]; S.RX = D[2]; S.RY = D[3];
			const uint8 B0 = D[4], B1 = D[5], B2 = D[6];
			S.L2Analog = D[7]; S.R2Analog = D[8];
			S.Hat = B0 & 0x0F;
			S.Square = (B0 & 0x10) != 0; S.Cross = (B0 & 0x20) != 0; S.Circle = (B0 & 0x40) != 0; S.Triangle = (B0 & 0x80) != 0;
			S.L1 = (B1 & 0x01) != 0; S.R1 = (B1 & 0x02) != 0; S.L2 = (B1 & 0x04) != 0; S.R2 = (B1 & 0x08) != 0;
			S.Select = (B1 & 0x10) != 0; S.Start = (B1 & 0x20) != 0; S.L3 = (B1 & 0x40) != 0; S.R3 = (B1 & 0x80) != 0;
			S.PS = (B2 & 0x01) != 0; S.TouchPad = (B2 & 0x02) != 0;
		}
	}

	FSonyPadState ParseReport(const uint8* Data, int32 Len, uint16 Pid, bool bBluetooth)
	{
		FSonyPadState S;
		if (!Data || Len < 10)
		{
			return S;
		}
		const uint8 Id = Data[0];
		if (IsDualSense(Pid))
		{
			if (Id == 0x01 && !bBluetooth && Len >= 11) { ReadFull(Data + 1, S); S.bValid = true; }
			else if (Id == 0x31 && Len >= 12) { ReadFull(Data + 2, S); S.bValid = true; }
			else if (Id == 0x01) { ReadSimple(Data + 1, S); S.bValid = true; }
		}
		else
		{
			if (Id == 0x01) { ReadSimple(Data + 1, S); S.bValid = true; }
			else if (Id == 0x11 && Len >= 12) { ReadSimple(Data + 3, S); S.bValid = true; }
		}
		if (S.bValid)
		{
			S.ReportId = Id;
		}
		return S;
	}

	namespace
	{
		float Axis(uint8 V, bool bInvert)
		{
			const float F = FMath::Clamp((float(V) - 127.5f) / 127.5f, -1.f, 1.f);
			return bInvert ? -F : F;
		}
	}

	void SticksToUE(const FSonyPadState& S, FVector2D& OutLeft, FVector2D& OutRight)
	{
		OutLeft = FVector2D(Axis(S.LX, false), Axis(S.LY, true));
		OutRight = FVector2D(Axis(S.RX, false), Axis(S.RY, true));
	}

	uint32 Crc32(const uint8* Data, int32 Len, uint32 Crc)
	{
		Crc = ~Crc;
		for (int32 I = 0; I < Len; ++I)
		{
			Crc ^= Data[I];
			for (int32 K = 0; K < 8; ++K)
			{
				Crc = (Crc >> 1) ^ (0xEDB88320u & (0u - (Crc & 1u)));
			}
		}
		return ~Crc;
	}

	uint32 BluetoothOutputCrc(const uint8* Data, int32 Len)
	{
		const uint8 Seed = 0xA2;
		return Crc32(Data, Len, Crc32(&Seed, 1));
	}

	namespace
	{
		void PutCrc(TArray<uint8>& R)
		{
			const int32 N = R.Num() - 4;
			const uint32 C = BluetoothOutputCrc(R.GetData(), N);
			R[N] = uint8(C); R[N + 1] = uint8(C >> 8); R[N + 2] = uint8(C >> 16); R[N + 3] = uint8(C >> 24);
		}
	}

	TArray<uint8> BuildRumbleReport(uint16 Pid, bool bBluetooth, uint8 SmallRight, uint8 LargeLeft, uint8 Seq, bool bVibrationV2)
	{
		TArray<uint8> R;
		if (IsDualSense(Pid))
		{
			R.SetNumZeroed(bBluetooth ? DualSenseBtReportSize : DualSenseUsbReportSize);
			int32 C = 1;
			if (bBluetooth)
			{
				R[0] = 0x31;
				R[1] = uint8((Seq & 0x0F) << 4);
				R[2] = 0x10;
				C = 3;
			}
			else
			{
				R[0] = 0x02;
			}
			R[C + 0] = 0x01 | 0x02;
			R[C + 1] = 0x00;
			R[C + 2] = SmallRight;
			R[C + 3] = LargeLeft;
			R[C + 38] = bVibrationV2 ? 0x04 : 0x00;
			if (bBluetooth)
			{
				PutCrc(R);
			}
		}
		else if (IsSupportedProduct(Pid))
		{
			R.SetNumZeroed(bBluetooth ? DualShock4BtReportSize : DualShock4UsbReportSize);
			int32 C = 1;
			if (bBluetooth)
			{
				R[0] = 0x11;
				R[1] = 0x80 | 0x40;
				R[2] = 0x00;
				C = 3;
			}
			else
			{
				R[0] = 0x05;
			}
			R[C + 0] = 0x01;
			R[C + 3] = SmallRight;
			R[C + 4] = LargeLeft;
			if (bBluetooth)
			{
				PutCrc(R);
			}
		}
		return R;
	}

	bool DualSenseVibrationV2(uint16 Pid, const uint8* Feature20, int32 Len)
	{
		if (Pid == 0x0DF2)
		{
			return true;
		}
		if (Pid != 0x0CE6 || !Feature20 || Len < 46 || Feature20[0] != 0x20)
		{
			return false;
		}
		const uint16 UpdateVersion = uint16(Feature20[44] | (Feature20[45] << 8));
		return UpdateVersion >= 0x0215;
	}

	namespace
	{
		std::atomic<uint32> GRumbleRequest{ 0 };
	}

#if PLATFORM_WINDOWS
	namespace
	{
		class FSonyPadReader : public FRunnable
		{
		public:
			FCriticalSection Lock;
			FSonyPadState Latest;
			bool bConnected = false;
			uint16 ConnPid = 0;
			bool bConnBt = false;
			TAtomic<bool> bStop{ false };

			HANDLE WakeEvent = nullptr;

			FSonyPadReader() { WakeEvent = CreateEventW(nullptr, 0, 0, nullptr); }
			virtual ~FSonyPadReader() override { if (WakeEvent) { CloseHandle(WakeEvent); } }

			struct FOutput
			{
				HANDLE H;
				uint16 Pid;
				bool bBt;
				int32 OutputLen;
				bool bV2 = false;
				uint8 Seq = 0;
				uint32 LastSent = 0xFFFFFFFFu;
				double LastSendTime = 0.0;
				OVERLAPPED WOv = {};

				FOutput(HANDLE InH, uint16 InPid, bool bInBt, int32 InOutputLen) : H(InH), Pid(InPid), bBt(bInBt), OutputLen(InOutputLen)
				{
					WOv.hEvent = CreateEventW(nullptr, 1, 0, nullptr);
					if (IsDualSense(Pid))
					{
						uint8 F[64] = { 0x20 };
						bV2 = HidD_GetFeature(H, F, sizeof(F)) ? DualSenseVibrationV2(Pid, F, sizeof(F)) : Pid == 0x0DF2;
					}
				}
				~FOutput() { if (WOv.hEvent) { CloseHandle(WOv.hEvent); } }

				bool Service(uint32 Request, bool bForce)
				{
					const double Now = FPlatformTime::Seconds();
					if (bForce ? LastSent == 0xFFFFFFFFu : (Request == LastSent && Now - LastSendTime < 1.0))
					{
						return true;   // unchanged (refresh not due) / never written: nothing to stop
					}
					TArray<uint8> R = BuildRumbleReport(Pid, bBt, uint8(Request), uint8(Request >> 8), Seq, bV2);
					if (R.Num() == 0)
					{
						return true;
					}
					Seq = uint8((Seq + 1) & 0x0F);
					const int32 Need = bBt ? R.Num() : (IsDualSense(Pid) ? 48 : 11);
					if (OutputLen >= Need)
					{
						R.SetNumZeroed(OutputLen);
					}
					LastSent = Request;
					LastSendTime = Now;
					ResetEvent(WOv.hEvent);
					if (!WriteFile(H, R.GetData(), (DWORD)R.Num(), nullptr, &WOv))
					{
						const DWORD E = GetLastError();
						if (E != ERROR_IO_PENDING)
						{
							return E != ERROR_DEVICE_NOT_CONNECTED && E != ERROR_INVALID_HANDLE;
						}
						if (WaitForSingleObject(WOv.hEvent, 100) == WAIT_TIMEOUT)
						{
							CancelIoEx(H, &WOv);
						}
						DWORD Done = 0;
						GetOverlappedResult(H, &WOv, &Done, 1);
					}
					return true;
				}
			};

			virtual uint32 Run() override
			{
				while (!bStop)
				{
					uint16 Pid = 0;
					bool bBt = false;
					int32 ReportLen = 0;
					int32 OutputLen = 0;
					HANDLE H = OpenPad(Pid, bBt, ReportLen, OutputLen);
					if (H == INVALID_HANDLE_VALUE)
					{
						for (int32 I = 0; I < 20 && !bStop; ++I) { FPlatformProcess::Sleep(0.1f); }
						continue;
					}
					FOutput Out(H, Pid, bBt, OutputLen);
					{
						FScopeLock L(&Lock);
						ConnPid = Pid;
						bConnBt = bBt;
					}
					UE_LOG(LogSSXPad, Log, TEXT("Sony controller connected (pid %04x, %s, output report %d bytes%s)"), Pid,
						bBt ? TEXT("Bluetooth") : TEXT("USB"), OutputLen, Out.bV2 ? TEXT(", vibration v2") : TEXT(""));
					TArray<uint8> Buf;
					Buf.SetNumZeroed(FMath::Max(ReportLen, 64));
					OVERLAPPED Ov = {};
					Ov.hEvent = CreateEventW(nullptr, 1, 0, nullptr);
					bool bPending = false;
					while (!bStop)
					{
						if (!bPending)
						{
							ResetEvent(Ov.hEvent);
							if (!ReadFile(H, Buf.GetData(), (DWORD)Buf.Num(), nullptr, &Ov) && GetLastError() != ERROR_IO_PENDING)
							{
								break;
							}
							bPending = true;
						}
						HANDLE Wait[2] = { Ov.hEvent, WakeEvent };
						const DWORD W = WaitForMultipleObjects(2, Wait, 0, 100);
						if (W == WAIT_OBJECT_0)
						{
							DWORD Got = 0;
							bPending = false;
							if (!GetOverlappedResult(H, &Ov, &Got, 0))
							{
								break;
							}
							const FSonyPadState S = ParseReport(Buf.GetData(), (int32)Got, Pid, bBt);
							if (S.bValid)
							{
								FScopeLock L(&Lock);
								Latest = S;
								bConnected = true;
							}
						}
						else if (W == WAIT_FAILED)
						{
							break;
						}
						if (!Out.Service(GRumbleRequest.load(), false))
						{
							break;
						}
					}
					if (bPending)
					{
						DWORD Got = 0;
						CancelIoEx(H, &Ov);
						GetOverlappedResult(H, &Ov, &Got, 1);
					}
					Out.Service(0, true);
					CloseHandle(Ov.hEvent);
					CloseHandle(H);
					{
						FScopeLock L(&Lock);
						Latest = FSonyPadState();
						bConnected = false;
					}
					UE_LOG(LogSSXPad, Log, TEXT("Sony controller disconnected"));
				}
				return 0;
			}

			static HANDLE OpenPad(uint16& OutPid, bool& bOutBt, int32& OutReportLen, int32& OutOutputLen)
			{
				GUID HidGuid;
				HidD_GetHidGuid(&HidGuid);
				HDEVINFO Set = SetupDiGetClassDevsW(&HidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
				if (Set == INVALID_HANDLE_VALUE)
				{
					return INVALID_HANDLE_VALUE;
				}
				HANDLE Found = INVALID_HANDLE_VALUE;
				SP_DEVICE_INTERFACE_DATA If = { sizeof(SP_DEVICE_INTERFACE_DATA) };
				for (DWORD I = 0; Found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(Set, nullptr, &HidGuid, I, &If); ++I)
				{
					DWORD Need = 0;
					SetupDiGetDeviceInterfaceDetailW(Set, &If, nullptr, 0, &Need, nullptr);
					TArray<uint8> DetailBuf;
					DetailBuf.SetNumZeroed(Need + sizeof(WCHAR));
					auto* Detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(DetailBuf.GetData());
					Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
					if (!SetupDiGetDeviceInterfaceDetailW(Set, &If, Detail, Need, nullptr, nullptr))
					{
						continue;
					}
					HANDLE H = CreateFileW(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
						nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
					if (H == INVALID_HANDLE_VALUE)
					{
						continue;
					}
					HIDD_ATTRIBUTES Attr = { sizeof(HIDD_ATTRIBUTES) };
					PHIDP_PREPARSED_DATA Pre = nullptr;
					HIDP_CAPS Caps = {};
					const bool bOk = HidD_GetAttributes(H, &Attr) && Attr.VendorID == SonyVendorId && IsSupportedProduct(Attr.ProductID)
						&& HidD_GetPreparsedData(H, &Pre);
					if (bOk && HidP_GetCaps(Pre, &Caps) == HIDP_STATUS_SUCCESS && Caps.UsagePage == 0x01 && (Caps.Usage == 0x05 || Caps.Usage == 0x04))
					{
						OutPid = Attr.ProductID;
						OutReportLen = Caps.InputReportByteLength;
						OutOutputLen = Caps.OutputReportByteLength;
						bOutBt = Caps.InputReportByteLength != 64;
						Found = H;
					}
					if (Pre)
					{
						HidD_FreePreparsedData(Pre);
					}
					if (Found != H)
					{
						CloseHandle(H);
					}
				}
				SetupDiDestroyDeviceInfoList(Set);
				return Found;
			}
		};

		FSonyPadReader* GReader = nullptr;
		FRunnableThread* GThread = nullptr;
		FTSTicker::FDelegateHandle GTick;
		FSonyPadState GPrev;
		bool GPrevConnected = false;
		FInputDeviceId GDevice = INPUTDEVICEID_NONE;

		constexpr float StickButtonThreshold = 7849.f / 32767.f;
		constexpr uint8 TriggerThreshold = 30;

		void Button(FGenericApplicationMessageHandler& Msg, FPlatformUserId User, const FGamepadKeyNames::Type& Key, bool bNow, bool bWas)
		{
			if (bNow && !bWas) { Msg.OnControllerButtonPressed(Key, User, GDevice, false); }
			else if (!bNow && bWas) { Msg.OnControllerButtonReleased(Key, User, GDevice, false); }
		}

		bool HatHas(uint8 Hat, int32 Dir)   // Dir: 0 up, 2 right, 4 down, 6 left
		{
			if (Hat > 7) { return false; }
			const int32 D = (int32(Hat) - Dir + 8) % 8;
			return D == 0 || D == 1 || D == 7;
		}

		bool Pump(float)
		{
			if (!GReader || !FSlateApplication::IsInitialized())
			{
				return true;
			}
			FSonyPadState S;
			bool bConn = false;
			{
				FScopeLock L(&GReader->Lock);
				S = GReader->Latest;
				bConn = GReader->bConnected;
			}
			IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
			const FPlatformUserId User = Mapper.GetPrimaryPlatformUser();
			if (bConn && !GPrevConnected)
			{
				GDevice = Mapper.GetDefaultInputDevice();
			}
			if (!bConn && !GPrevConnected)
			{
				return true;
			}
			FGenericApplicationMessageHandler& Msg = FSlateApplication::Get();
			const FSonyPadState& P = GPrev;
			Button(Msg, User, FGamepadKeyNames::FaceButtonBottom, S.Cross, P.Cross);
			Button(Msg, User, FGamepadKeyNames::FaceButtonRight, S.Circle, P.Circle);
			Button(Msg, User, FGamepadKeyNames::FaceButtonLeft, S.Square, P.Square);
			Button(Msg, User, FGamepadKeyNames::FaceButtonTop, S.Triangle, P.Triangle);
			Button(Msg, User, FGamepadKeyNames::LeftShoulder, S.L1, P.L1);
			Button(Msg, User, FGamepadKeyNames::RightShoulder, S.R1, P.R1);
			Button(Msg, User, FGamepadKeyNames::LeftTriggerThreshold, S.L2Analog > TriggerThreshold, P.L2Analog > TriggerThreshold);
			Button(Msg, User, FGamepadKeyNames::RightTriggerThreshold, S.R2Analog > TriggerThreshold, P.R2Analog > TriggerThreshold);
			Button(Msg, User, FGamepadKeyNames::SpecialLeft, S.Select, P.Select);
			Button(Msg, User, FGamepadKeyNames::SpecialRight, S.Start, P.Start);
			Button(Msg, User, FGamepadKeyNames::LeftThumb, S.L3, P.L3);
			Button(Msg, User, FGamepadKeyNames::RightThumb, S.R3, P.R3);
			Button(Msg, User, FGamepadKeyNames::DPadUp, HatHas(S.Hat, 0), HatHas(P.Hat, 0));
			Button(Msg, User, FGamepadKeyNames::DPadRight, HatHas(S.Hat, 2), HatHas(P.Hat, 2));
			Button(Msg, User, FGamepadKeyNames::DPadDown, HatHas(S.Hat, 4), HatHas(P.Hat, 4));
			Button(Msg, User, FGamepadKeyNames::DPadLeft, HatHas(S.Hat, 6), HatHas(P.Hat, 6));
			FVector2D L, R, PL, PR;
			SticksToUE(S, L, R);
			SticksToUE(P, PL, PR);
			const float LX = L.X, LY = L.Y, PLX = PL.X, PLY = PL.Y;
			const float RX = R.X, RY = R.Y, PRX = PR.X, PRY = PR.Y;
			Button(Msg, User, FGamepadKeyNames::LeftStickUp, LY > StickButtonThreshold, PLY > StickButtonThreshold);
			Button(Msg, User, FGamepadKeyNames::LeftStickDown, LY < -StickButtonThreshold, PLY < -StickButtonThreshold);
			Button(Msg, User, FGamepadKeyNames::LeftStickRight, LX > StickButtonThreshold, PLX > StickButtonThreshold);
			Button(Msg, User, FGamepadKeyNames::LeftStickLeft, LX < -StickButtonThreshold, PLX < -StickButtonThreshold);
			Button(Msg, User, FGamepadKeyNames::RightStickUp, RY > StickButtonThreshold, PRY > StickButtonThreshold);
			Button(Msg, User, FGamepadKeyNames::RightStickDown, RY < -StickButtonThreshold, PRY < -StickButtonThreshold);
			Button(Msg, User, FGamepadKeyNames::RightStickRight, RX > StickButtonThreshold, PRX > StickButtonThreshold);
			Button(Msg, User, FGamepadKeyNames::RightStickLeft, RX < -StickButtonThreshold, PRX < -StickButtonThreshold);
			Msg.OnControllerAnalog(FGamepadKeyNames::LeftAnalogX, User, GDevice, LX);
			Msg.OnControllerAnalog(FGamepadKeyNames::LeftAnalogY, User, GDevice, LY);
			Msg.OnControllerAnalog(FGamepadKeyNames::RightAnalogX, User, GDevice, RX);
			Msg.OnControllerAnalog(FGamepadKeyNames::RightAnalogY, User, GDevice, RY);
			Msg.OnControllerAnalog(FGamepadKeyNames::LeftTriggerAnalog, User, GDevice, S.L2Analog / 255.f);
			Msg.OnControllerAnalog(FGamepadKeyNames::RightTriggerAnalog, User, GDevice, S.R2Analog / 255.f);
			GPrev = S;
			GPrevConnected = bConn;
			return true;
		}
	}

	void StartSonyPadSupport()
	{
		if (GReader || IsRunningCommandlet() || !FApp::CanEverRender())
		{
			return;
		}
		GReader = new FSonyPadReader();
		GThread = FRunnableThread::Create(GReader, TEXT("SSXSonyPad"), 0, TPri_AboveNormal);
		GTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Pump));
	}

	bool GetSonyPadDebug(FSonyPadState& OutState, uint16& OutProductId, bool& bOutBluetooth)
	{
		if (!GReader)
		{
			return false;
		}
		FScopeLock L(&GReader->Lock);
		OutState = GReader->Latest;
		OutProductId = GReader->ConnPid;
		bOutBluetooth = GReader->bConnBt;
		return GReader->bConnected;
	}

	void SetSonyPadRumble(uint8 SmallRight, uint8 LargeLeft)
	{
		const uint32 V = uint32(SmallRight) | (uint32(LargeLeft) << 8);
		if (GRumbleRequest.exchange(V) != V && GReader && GReader->WakeEvent)
		{
			SetEvent(GReader->WakeEvent);
		}
	}

	void StopSonyPadSupport()
	{
		GRumbleRequest.store(0);
		if (!GReader)
		{
			return;
		}
		FTSTicker::RemoveTicker(GTick);
		GReader->bStop = true;
		SetEvent(GReader->WakeEvent);
		if (GThread)
		{
			GThread->WaitForCompletion();
			delete GThread;
			GThread = nullptr;
		}
		delete GReader;
		GReader = nullptr;
	}
#else
	bool GetSonyPadDebug(FSonyPadState&, uint16&, bool&) { return false; }
	void StartSonyPadSupport() {}
	void StopSonyPadSupport() {}
	void SetSonyPadRumble(uint8 SmallRight, uint8 LargeLeft) { GRumbleRequest.store(uint32(SmallRight) | (uint32(LargeLeft) << 8)); }
#endif
}
