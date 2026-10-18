#include "Input/SSXDualSense.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSonyPadTest, "SSX.Pawn.SonyPad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXSonyPadTest::RunTest(const FString& Parameters)
{
	using namespace SSXPad;
	uint8 Usb[64] = { 0x01, 10, 250, 128, 0, 40, 255, 7, 0x22, 0x21, 0x01 };
	FSonyPadState S = ParseReport(Usb, 64, 0x0CE6, false);
	TestTrue(TEXT("usb valid"), S.bValid);
	TestTrue(TEXT("usb sticks/triggers"), S.LX == 10 && S.LY == 250 && S.RY == 0 && S.L2Analog == 40 && S.R2Analog == 255);
	TestTrue(TEXT("usb buttons"), S.Hat == 2 && S.Cross && !S.Square && S.L1 && S.Start && !S.Select && S.PS);

	uint8 Bt[78] = { 0x31, 0x00, 10, 250, 128, 0, 40, 255, 7, 0x88, 0x12, 0x00 };
	S = ParseReport(Bt, 78, 0x0CE6, true);
	TestTrue(TEXT("bt full"), S.bValid && S.LX == 10 && S.R2Analog == 255 && S.Hat == 8 && S.Triangle && S.R1 && S.Select);

	uint8 BtS[78] = { 0x01, 1, 2, 3, 4, 0x46, 0x40, 0x02, 99, 200 };
	S = ParseReport(BtS, 78, 0x0CE6, true);
	TestTrue(TEXT("bt simple"), S.bValid && S.LX == 1 && S.RY == 4 && S.Hat == 6 && S.Circle && S.L3 && S.TouchPad && S.L2Analog == 99 && S.R2Analog == 200);

	uint8 Ds4[64] = { 0x01, 1, 2, 3, 4, 0x10, 0x80, 0x00, 5, 6 };
	S = ParseReport(Ds4, 64, 0x09CC, false);
	TestTrue(TEXT("ds4 usb"), S.bValid && S.Square && S.R3 && S.L2Analog == 5 && S.R2Analog == 6);
	uint8 Ds4Bt[78] = { 0x11, 0xC0, 0x00, 1, 2, 3, 4, 0x20, 0x00, 0x00, 7, 8 };
	S = ParseReport(Ds4Bt, 78, 0x05C4, true);
	TestTrue(TEXT("ds4 bt"), S.bValid && S.LX == 1 && S.Cross && S.L2Analog == 7);

	TestFalse(TEXT("unknown report ignored"), ParseReport(Bt, 78, 0x09CC, true).bValid);
	TestTrue(TEXT("supported products"), IsSupportedProduct(0x0CE6) && IsSupportedProduct(0x0DF2) && !IsSupportedProduct(0x1234));
	return true;
}

#endif
