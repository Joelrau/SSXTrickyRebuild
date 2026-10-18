#include "Input/SSXDualSense.h"
#include "Input/SSXPadVibration.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags VibFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	void RunRider(SSXVibration::FPadActuators& Pad, SSXVibration::FPlayerRumble& Pl, TConstArrayView<FRiderPadFeedback> Calls, int32 Frames,
	              TArray<uint8>& OutSmall, TArray<uint8>& OutBig)
	{
		for (int32 F = 0; F < Frames; ++F)
		{
			Pad.Poll();
			OutSmall.Add(Pad.SentSmall);
			OutBig.Add(Pad.SentBig);
			if (F == 0)
			{
				for (const FRiderPadFeedback& C : Calls) { Pl.Apply(Pad, C); }
			}
			Pl.Update(Pad);
		}
	}

	FRiderPadFeedback ImpactCall(const FVector& V, const FVector& N)
	{
		FRiderPadFeedback C; C.bImpact = true; C.Velocity = V; C.Normal = N; return C;
	}
	FRiderPadFeedback FeedbackCall(double X)
	{
		FRiderPadFeedback C; C.Amount = X; return C;
	}
	int32 CountOf(const TArray<uint8>& A, uint8 V) { int32 N = 0; for (uint8 X : A) { N += X == V; } return N; }
	int32 LastNonZero(const TArray<uint8>& A) { for (int32 I = A.Num() - 1; I >= 0; --I) { if (A[I]) { return I; } } return -1; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXVibrationDeviceTest, "SSX.Pawn.Vibration.Device", VibFlags)
bool FSSXVibrationDeviceTest::RunTest(const FString& Parameters)
{
	using namespace SSXVibration;
	{
		FPadActuators P;
		TestTrue(TEXT("ctor: enabled, dirty, no timeouts"), P.bEnabled && P.bDirty && P.SmallFrames == -1 && P.BigFramesLeft == -1);
		TestTrue(TEXT("first poll sends the zero block"), P.Poll() && P.SentSmall == 0 && P.SentBig == 0 && !P.Poll());
		P.SetSmall(1, 6);
		TArray<uint8> On;
		for (int32 F = 0; F < 9; ++F) { P.Poll(); On.Add(P.SentSmall); }
		TestEqual(TEXT("small motor: 6-frame request is on for polls 1..6, off at 7"), On, TArray<uint8>({ 1, 1, 1, 1, 1, 1, 0, 0, 0 }));
		TestEqual(TEXT("sends: the initial block, on, off"), P.SendCount, 3);
		P.SetSmall(1, 0);
		P.Poll();
		TestEqual(TEXT("0-frame request is cleared on the same poll: never reaches the pad"), P.SentSmall, uint8(0));
		P.SetBig(0.5f, 2);
		TestEqual(TEXT("big = (int)(0.5 * 255)"), P.Big, uint8(127));
		P.SetBig(1.0f, -1);
		P.Poll();
		TestEqual(TEXT("big 255, no timeout"), P.SentBig, uint8(255));
		for (int32 F = 0; F < 20; ++F) { P.Poll(); }
		TestEqual(TEXT("no timeout: stays on"), P.SentBig, uint8(255));
		P.SetEnabled(false);
		P.Poll();
		TestTrue(TEXT("disable clears both motors at the next poll"), P.SentBig == 0 && P.SentSmall == 0 && !P.bEnabled);
		P.SetSmall(1, 6);
		P.SetBig(1.f, 2);
		P.Poll();
		TestTrue(TEXT("disabled: the setters do nothing"), P.SentSmall == 0 && P.SentBig == 0 && P.SmallFrames == -1);
		const int32 Sends = P.SendCount;
		P.SetEnabled(false);
		P.Poll();
		TestEqual(TEXT("enable unchanged: no dirty"), P.SendCount, Sends);
		P.SetEnabled(true);
		P.SetSmall(1, 3);
		P.Poll(/*bStable=*/false);
		TestTrue(TEXT("unstable pad: bytes cleared without a send"), P.Small == 0 && P.SmallFrames == -1);
	}
	TestEqual(TEXT("frames(0.012) = int(0.02 * 60) = 1"), SmallMotorFrames(0.012f), 1);
	TestEqual(TEXT("frames(0.03) = int(0.05 * 60) = 2 in float"), SmallMotorFrames(0.03f), 2);
	TestEqual(TEXT("frames(0.6) = int(0.1 * 60) = 6"), SmallMotorFrames(0.6f), 6);
	TestEqual(TEXT("frames(2) = 6"), SmallMotorFrames(2.f), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXVibrationPlayerTest, "SSX.Pawn.Vibration.Player", VibFlags)
bool FSSXVibrationPlayerTest::RunTest(const FString& Parameters)
{
	using namespace SSXVibration;
	// wipe-out impact (slot 12): v.n = -100 cm/s -> x = clamp(138.9, 0, 2) = 2
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		TArray<uint8> S, B;
		const FRiderPadFeedback C[] = { ImpactCall(FVector(0, 300, -100), FVector(0, 0, 1)) };
		RunRider(Pad, Pl, C, 60, S, B);
		TestEqual(TEXT("impact: small motor on for frames 1..6"), CountOf(S, 1), 6);
		TestTrue(TEXT("impact: small motor frames 1 and 6 on, 7 off"), S[1] == 1 && S[6] == 1 && S[7] == 0 && S[0] == 0);
		TestEqual(TEXT("impact: strength 2 -> big motor full for 21 frames"), CountOf(B, 255), 21);
		TestEqual(TEXT("impact: big motor fades out after frame 50"), LastNonZero(B), 50);
		TestTrue(TEXT("impact: fade tail 22, 15, 9, 0"), B[48] == 22 && B[49] == 15 && B[50] == 9 && B[51] == 0);
	}
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		Pl.Impact(Pad, FVector(0, 0, -0.2), FVector(0, 0, 1));
		TestTrue(TEXT("impact x = 0.278: no small motor"), Pad.SmallFrames == -1 && Pad.Small == 0);
		TestTrue(TEXT("impact x = 0.278: strength = x"), FMath::IsNearlyEqual(Pl.Strength, 0.2f * 1.3888888f, 1e-6f));
		Pl.Impact(Pad, FVector(0, 0, 50), FVector(0, 0, 1));
		TestTrue(TEXT("separating impact: x = 0, strength kept"), FMath::IsNearlyEqual(Pl.Strength, 0.2f * 1.3888888f, 1e-6f));
	}
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		TArray<uint8> S, B;
		const FRiderPadFeedback C[] = { FeedbackCall(double(-1000.f * -0.00072000007f)) };
		RunRider(Pad, Pl, C, 60, S, B);
		TestEqual(TEXT("landing 0.72: small motor 6 frames"), CountOf(S, 1), 6);
		TestEqual(TEXT("landing 0.72: strength 1.52 -> big full 12 frames"), CountOf(B, 255), 12);
		TestEqual(TEXT("landing 0.72: big motor ends after frame 41"), LastNonZero(B), 41);
	}
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		TArray<uint8> S, B;
		const FRiderPadFeedback C[] = { FeedbackCall(0.0) };
		RunRider(Pad, Pl, C, 40, S, B);
		TestEqual(TEXT("bump 0: no small motor"), CountOf(S, 1), 0);
		TestEqual(TEXT("bump 0: big = (int)(0.8 * 255) = 204 first"), B[1], uint8(204));
		TestEqual(TEXT("bump 0: big motor ends after frame 25"), LastNonZero(B), 25);
	}
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		Pl.Feedback(Pad, 500.f * 0.0012f);
		TestTrue(TEXT("hit 0.6: small motor 6 frames, strength 1.4"), Pad.SmallFrames == 6 && FMath::IsNearlyEqual(Pl.Strength, 1.4f, 1e-6f));
		Pl.Feedback(Pad, 0.1f);
		TestTrue(TEXT("weaker call keeps the strength"), FMath::IsNearlyEqual(Pl.Strength, 1.4f, 1e-6f));
		Pl.Feedback(Pad, 5.f);
		TestEqual(TEXT("strength capped at 2"), Pl.Strength, 2.f);
		Pl.Feedback(Pad, -0.6f);
		TestEqual(TEXT("negative Jc: no change above 0.2"), Pl.Strength, 2.f);
	}
	{
		FPadActuators Pad;
		ApplyRaceLoadSetting(Pad, 0);
		FPlayerRumble Pl;
		TArray<uint8> S, B;
		const FRiderPadFeedback C[] = { ImpactCall(FVector(0, 0, -500), FVector(0, 0, 1)), FeedbackCall(0.9) };
		RunRider(Pad, Pl, C, 60, S, B);
		TestTrue(TEXT("vibration off: small motor never on"), CountOf(S, 0) == S.Num());
		TestTrue(TEXT("vibration off: big motor never on"), CountOf(B, 0) == B.Num());
		FPadActuators On;
		ApplyRaceLoadSetting(On, 1);
		TestTrue(TEXT("vibration on: enabled"), On.bEnabled);
	}
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		TArray<uint8> S, B;
		const FRiderPadFeedback C[] = { ImpactCall(FVector(0, 0, -500), FVector(0, 0, 1)) };
		RunRider(Pad, Pl, C, 6, S, B);
		TArray<uint8> Paused;
		for (int32 F = 0; F < 4; ++F) { Pad.Poll(); Paused.Add(Pad.SentBig); }
		TestEqual(TEXT("pause: big 255 for two polls, then 0"), Paused, TArray<uint8>({ 255, 255, 0, 0 }));
		TestEqual(TEXT("pause: small motor ran out too"), Pad.SentSmall, uint8(0));
	}
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		Pl.Impact(Pad, FVector(0, 0, -500), FVector(0, 0, 1), /*bReplayPlaying=*/true);
		Pl.Feedback(Pad, 1.f, true);
		Pl.Update(Pad, true);
		TestTrue(TEXT("replay playing: no request, no strength"), Pl.Strength == 0.f && !Pad.bDirty && Pad.SmallFrames == -1);
	}
	{
		FPadActuators Pad; Pad.Poll();
		FPlayerRumble Pl;
		const FRiderPadFeedback C[] = { FeedbackCall(0.6) };
		RiderFrame(Pad, Pl, C);
		TestTrue(TEXT("RiderFrame: requests wait for the next poll"), Pad.SentSmall == 0 && Pad.Small == 1 && Pad.Big == 255);
		RiderFrame(Pad, Pl, {});
		TestTrue(TEXT("RiderFrame: sent at the next frame's poll"), Pad.SentSmall == 1 && Pad.SentBig == 255);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXVibrationFrontEndTest, "SSX.Pawn.Vibration.FrontEnd", VibFlags)
bool FSSXVibrationFrontEndTest::RunTest(const FString& Parameters)
{
	using namespace SSXVibration;
	FPadActuators Pad;
	ApplyRaceLoadSetting(Pad, 0);
	Pad.Poll();
	FrontEndOneShot(Pad);
	TArray<uint8> On;
	for (int32 F = 0; F < 12; ++F) { Pad.Poll(); On.Add(Pad.SentSmall); }
	TestTrue(TEXT("FE On: the device enable is forced on"), Pad.bEnabled);
	TestEqual(TEXT("FE On: small motor for 10 frames"), On, TArray<uint8>({ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 }));
	TestEqual(TEXT("FE On: big motor unused"), Pad.SentBig, uint8(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXVibrationReportsTest, "SSX.Pawn.Vibration.Reports", VibFlags)
bool FSSXVibrationReportsTest::RunTest(const FString& Parameters)
{
	using namespace SSXPad;
	const uint8 Check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
	TestEqual(TEXT("CRC-32 check value"), Crc32(Check, 9), 0xCBF43926u);
	const uint8 Seed = 0xA2;
	TestEqual(TEXT("CRC chaining"), Crc32(Check, 9, Crc32(&Seed, 1)), BluetoothOutputCrc(Check, 9));

	auto Le32 = [](const TArray<uint8>& R, int32 At) { return uint32(R[At]) | (uint32(R[At + 1]) << 8) | (uint32(R[At + 2]) << 16) | (uint32(R[At + 3]) << 24); };
	auto ZeroExcept = [](const TArray<uint8>& R, std::initializer_list<int32> Keep)
	{
		for (int32 I = 0; I < R.Num(); ++I)
		{
			bool bKeep = false;
			for (int32 K : Keep) { bKeep |= K == I; }
			if (!bKeep && R[I] != 0) { return false; }
		}
		return true;
	};

	TArray<uint8> R = BuildRumbleReport(0x0CE6, false, 255, 128);
	TestEqual(TEXT("DualSense USB: 63 bytes"), R.Num(), 63);
	TestTrue(TEXT("DualSense USB: 02 03 00 FF 80"), R[0] == 0x02 && R[1] == 0x03 && R[2] == 0x00 && R[3] == 255 && R[4] == 128);
	TestTrue(TEXT("DualSense USB: LEDs / audio / flag2 untouched"), ZeroExcept(R, { 0, 1, 3, 4 }));
	R = BuildRumbleReport(0x0CE6, false, 0, 0, 0, true);
	TestTrue(TEXT("DualSense USB v2: valid_flag2 bit 2 at byte 39"), R[39] == 0x04 && ZeroExcept(R, { 0, 1, 39 }));

	R = BuildRumbleReport(0x0CE6, true, 255, 128, 3);
	TestEqual(TEXT("DualSense BT: 78 bytes"), R.Num(), 78);
	TestTrue(TEXT("DualSense BT: 31 30 10 03 00 FF 80"), R[0] == 0x31 && R[1] == 0x30 && R[2] == 0x10 && R[3] == 0x03 && R[4] == 0 && R[5] == 255 && R[6] == 128);
	TestTrue(TEXT("DualSense BT: rest zero"), ZeroExcept(R, { 0, 1, 2, 3, 5, 6, 74, 75, 76, 77 }));
	TestEqual(TEXT("DualSense BT: CRC32 (zlib reference 0x8DF32AEE)"), Le32(R, 74), 0x8DF32AEEu);
	R = BuildRumbleReport(0x0CE6, true, 0, 0, 0x10, true);
	TestTrue(TEXT("DualSense BT: sequence wraps at 16, flag2 at 41"), R[1] == 0x00 && R[41] == 0x04);
	TestEqual(TEXT("DualSense BT v2 zero: CRC32 (zlib reference 0x4DE42700)"), Le32(R, 74), 0x4DE42700u);
	TestEqual(TEXT("DualSense Edge uses the same layout"), BuildRumbleReport(0x0DF2, true, 255, 128, 3), BuildRumbleReport(0x0CE6, true, 255, 128, 3));

	R = BuildRumbleReport(0x09CC, false, 255, 200);
	TestEqual(TEXT("DS4 USB: 32 bytes"), R.Num(), 32);
	TestTrue(TEXT("DS4 USB: 05 01 00 00 FF C8"), R[0] == 0x05 && R[1] == 0x01 && R[4] == 255 && R[5] == 200 && ZeroExcept(R, { 0, 1, 4, 5 }));
	R = BuildRumbleReport(0x05C4, true, 255, 200);
	TestEqual(TEXT("DS4 BT: 78 bytes"), R.Num(), 78);
	TestTrue(TEXT("DS4 BT: 11 C0 00 01 00 00 FF C8"), R[0] == 0x11 && R[1] == 0xC0 && R[3] == 0x01 && R[6] == 255 && R[7] == 200
		&& ZeroExcept(R, { 0, 1, 3, 6, 7, 74, 75, 76, 77 }));
	TestEqual(TEXT("DS4 BT: CRC32 (zlib reference 0xFFAD177A)"), Le32(R, 74), 0xFFAD177Au);
	TestEqual(TEXT("unsupported product: no report"), BuildRumbleReport(0x1234, false, 1, 1).Num(), 0);

	uint8 F[64] = { 0x20 };
	F[44] = 0x15; F[45] = 0x02;
	TestTrue(TEXT("v2: DualSense update 2.21"), DualSenseVibrationV2(0x0CE6, F, 64));
	F[44] = 0x14;
	TestFalse(TEXT("v2: DualSense update 2.20"), DualSenseVibrationV2(0x0CE6, F, 64));
	TestTrue(TEXT("v2: DualSense Edge always"), DualSenseVibrationV2(0x0DF2, nullptr, 0));
	TestFalse(TEXT("v2: DualShock 4 never"), DualSenseVibrationV2(0x09CC, F, 64));
	return true;
}

#endif
