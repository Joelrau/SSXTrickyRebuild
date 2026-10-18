// 0x11A348 / 0x21D0C0 / 0x21D5C8
#include "Audio/SSXZoneAmbience.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXZoneAmbienceTest, "SSX.Audio.ZoneAmbience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXZoneAmbienceTest::RunTest(const FString& Parameters)
{
	FSSXZoneAmbience Z;
	TArray<FSSXAmbientStart> Started;
	TArray<TPair<int32, float>> Stopped;
	int32 Next = 100;
	Z.Start = [&](const FSSXAmbientStart& P) { Started.Add(P); return Next++; };
	Z.Stop = [&](int32 H, float Fade) { Stopped.Add({ H, Fade }); };
	Z.Reset();

	// 0x21D0C0
	const FSSXAmbientStart A = FSSXZoneAmbience::Push(0), B = FSSXZoneAmbience::Push(1), C = FSSXZoneAmbience::Push(2);
	TestTrue(TEXT("kind 0: swap bank Tunnel1 index 0, volume 0x23, 2D"), A.Bank == 6 && A.SwapBank == TEXT("Tunnel1") && A.Index == 0 && A.Volume127 == 0x23 && !A.b3D);
	TestTrue(TEXT("kind 1: swap bank RiverSmall"), B.Bank == 6 && B.SwapBank == TEXT("RiverSmall") && B.Index == 0 && B.Volume127 == 0x23);
	TestTrue(TEXT("kind 2: track bank index 9, volume 100"), C.Bank == 2 && C.Index == 9 && C.Volume127 == 100 && !C.b3D);
	TestTrue(TEXT("types 14 / 23 / 24 only"), FSSXZoneAmbience::KindOf(14) == 0 && FSSXZoneAmbience::KindOf(23) == 1
		&& FSSXZoneAmbience::KindOf(24) == 2 && FSSXZoneAmbience::KindOf(13) == INDEX_NONE && FSSXZoneAmbience::KindOf(2) == INDEX_NONE);

	Z.Trigger(0, 14, true);
	TestTrue(TEXT("request pending, nothing yet"), Z.bPending && Started.Num() == 0 && Z.Voice == INDEX_NONE);
	Z.Update();
	Z.Trigger(0, 14, true);
	Z.Update();
	TestTrue(TEXT("one Tunnel1 voice"), Started.Num() == 1 && Started[0].SwapBank == TEXT("Tunnel1") && Z.Voice == 100);

	// 0x218EB0
	Z.StopVoice();
	TestTrue(TEXT("forced reset stops the voice"), Stopped.Num() == 1 && Stopped[0].Key == 100 && Stopped[0].Value == 2.0f && Z.Voice == INDEX_NONE);
	Z.StopVoice();
	TestEqual(TEXT("no voice: no stop"), Stopped.Num(), 1);
	Z.Trigger(0, 14, true);
	Z.Update();
	Z.Trigger(0, 14, false);
	TestTrue(TEXT("no restart inside the zone"), Started.Num() == 1 && Stopped.Num() == 1);

	Z.Trigger(3, 23, true);
	Z.Update();
	Z.Trigger(0, 24, true);
	Z.Update();
	TestTrue(TEXT("AI then player: two starts, the second handle kept"), Started.Num() == 3 && Started[2].Bank == 2 && Z.Voice == 102 && Stopped.Num() == 1);
	Z.Trigger(3, 23, false);
	TestTrue(TEXT("AI exit stops the current voice"), Stopped.Num() == 2 && Stopped[1].Key == 102 && Z.Voice == INDEX_NONE);
	Z.Trigger(5, 14, false);
	TestEqual(TEXT("exit without entry: nothing"), Stopped.Num(), 2);

	// 0x211B58
	Z.Trigger(0, 24, false);
	Z.Trigger(0, 24, true);
	Z.Update();
	Z.Restart();
	TestTrue(TEXT("restart stops"), Stopped.Num() == 3 && Z.Voice == INDEX_NONE);
	Z.Trigger(0, 24, true);
	Z.Update();
	TestEqual(TEXT("entry after a restart starts again"), Started.Num(), 5);
	return !HasAnyErrors();
}

#endif
