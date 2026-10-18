#pragma once

#include "CoreMinimal.h"

namespace SSXPad
{
	struct FSonyPadState
	{
		uint8 LX = 128, LY = 128, RX = 128, RY = 128;   // as sent: 128 = centre, y down
		uint8 L2Analog = 0, R2Analog = 0;
		uint8 Hat = 8;   // 0 = up, clockwise to 7 = up-left, 8 = released
		bool Square = false, Cross = false, Circle = false, Triangle = false;
		bool L1 = false, R1 = false, L2 = false, R2 = false;
		bool Select = false, Start = false, L3 = false, R3 = false;
		bool PS = false, TouchPad = false;
		bool bValid = false;
		uint8 ReportId = 0;
	};

	constexpr uint16 SonyVendorId = 0x054C;
	SSXTRICKYREBUILD_API bool IsSupportedProduct(uint16 ProductId);
	SSXTRICKYREBUILD_API bool IsDualSense(uint16 ProductId);

	// Data[0] = report id, as ReadFile returns it.
	SSXTRICKYREBUILD_API FSonyPadState ParseReport(const uint8* Data, int32 Len, uint16 ProductId, bool bBluetooth);

	// UE convention (+Y = stick up): (byte - 127.5) / 127.5 with Y negated.
	SSXTRICKYREBUILD_API void SticksToUE(const FSonyPadState& S, FVector2D& OutLeft, FVector2D& OutRight);

	/** The last report the reader decoded (diagnostics: ssx.Shove.Log). False when no Sony pad is connected. */
	SSXTRICKYREBUILD_API bool GetSonyPadDebug(FSonyPadState& OutState, uint16& OutProductId, bool& bOutBluetooth);

	SSXTRICKYREBUILD_API void StartSonyPadSupport();
	SSXTRICKYREBUILD_API void StopSonyPadSupport();

	constexpr int32 DualSenseUsbReportSize = 63;   // id 0x02 + 47-byte common block + padding
	constexpr int32 DualSenseBtReportSize = 78;
	constexpr int32 DualShock4UsbReportSize = 32;
	constexpr int32 DualShock4BtReportSize = 78;

	SSXTRICKYREBUILD_API uint32 Crc32(const uint8* Data, int32 Len, uint32 Crc = 0);
	// CRC-32 over the seed byte 0xA2 followed by Data[0..Len).
	SSXTRICKYREBUILD_API uint32 BluetoothOutputCrc(const uint8* Data, int32 Len);

	// Right motor = weak (the PS2 small motor), left motor = strong (the PS2 big motor).
	SSXTRICKYREBUILD_API TArray<uint8> BuildRumbleReport(uint16 ProductId, bool bBluetooth, uint8 SmallRight, uint8 LargeLeft,
	                                               uint8 Seq = 0, bool bVibrationV2 = false);

	// Feature report 0x20: the improved vibration on the Edge and on firmware update version (le16 at byte 44) >= 0x0215.
	SSXTRICKYREBUILD_API bool DualSenseVibrationV2(uint16 ProductId, const uint8* Feature20, int32 Len);

	SSXTRICKYREBUILD_API void SetSonyPadRumble(uint8 SmallRight, uint8 LargeLeft);
}
