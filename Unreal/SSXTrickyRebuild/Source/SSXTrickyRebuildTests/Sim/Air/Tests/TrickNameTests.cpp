#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/GameRng.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Air/TrickName.h"
#include "UI/SSXFrontEndData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TrickTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrickNamesTest, "SSX.Trick.Names", TrickTestFlags)

bool FSSXTrickNamesTest::RunTest(const FString& Parameters)
{
	using namespace SSXTrick;
	TestEqual(TEXT("round 90"), Round180(90), 0);
	TestEqual(TEXT("round -90"), Round180(-90), 0);
	TestEqual(TEXT("round 91"), Round180(91), 180);
	TestEqual(TEXT("round -91"), Round180(-91), -180);
	TestEqual(TEXT("round 270"), Round180(270), 180);

	const FTrickTables& T = FTrickTables::Get();
	if (!T.IsLoaded())
	{
		AddError(TEXT("converted/hud/trick_grids.json or trick_names.json missing (tools/export_trick_data.py, tools/export_hud_data.py)"));
		return false;
	}
	FString Text;
	const FString Path = FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("hud"), TEXT("trick_name_reference.json"));
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		AddError(FString::Printf(TEXT("%s missing (tools/export_trick_data.py)"), *Path));
		return false;
	}
	int32 Cases = 0, BadRec = 0, BadText = 0, Named = 0;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cases")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		FRecordInput In;
		In.SpinDeg = C->GetIntegerField(TEXT("spin"));
		In.FlipDeg = C->GetIntegerField(TEXT("flip"));
		In.bSwitch = C->GetIntegerField(TEXT("switch")) != 0;
		In.SegType = C->GetIntegerField(TEXT("type"));
		In.bLate = C->GetIntegerField(TEXT("late")) != 0;
		In.bAltFlag = C->GetIntegerField(TEXT("alt")) != 0;
		const TArray<TSharedPtr<FJsonValue>>& G = C->GetArrayField(TEXT("grabs"));
		for (int32 K = 0; K < G.Num() && K < 3; ++K) { In.Grabs[K] = int32(G[K]->AsNumber()); }
		In.bToRail = C->GetIntegerField(TEXT("to_rail")) != 0;
		In.bLandSwitch = C->GetIntegerField(TEXT("land_switch")) != 0;
		In.bAltSelect = C->GetIntegerField(TEXT("alt_select")) != 0;
		const FRecord R = BuildTrickRecord(In, T);
		const uint32 W0 = uint32(C->GetNumberField(TEXT("w0"))), W1 = uint32(C->GetNumberField(TEXT("w1")));
		const bool bValid = C->GetIntegerField(TEXT("valid")) != 0;
		if (R.W0 != W0 || R.W1 != W1 || R.bValid != bValid)
		{
			if (++BadRec <= 5)
			{
				AddError(FString::Printf(TEXT("record spin %d flip %d: %08x %08x vs model %08x %08x"), In.SpinDeg, In.FlipDeg, R.W0, R.W1, W0, W1));
			}
		}
		else if (T.Compose(R.W0, R.W1) != C->GetStringField(TEXT("text")))
		{
			if (++BadText <= 5)
			{
				AddError(FString::Printf(TEXT("text of %08x %08x differs from the model"), W0, W1));
			}
		}
		Named += ((W0 & 0xFF) != 0 && (W0 & 0xFF) != Unnameable) ? 1 : 0;
		++Cases;
	}
	TestTrue(TEXT("reference has cases"), Cases > 1000);
	TestTrue(TEXT("named flips covered"), Named > 0);
	TestEqual(TEXT("records equal to models/trick_name.py"), BadRec, 0);
	TestEqual(TEXT("texts equal to models/trick_name.py"), BadText, 0);

	// FUN_00156618
	const FRecord Rail = RailTrickRecord(1, 370);
	TestEqual(TEXT("rail record w1"), Rail.W1, (1u << 12) | (2u << 4));
	TestEqual(TEXT("tier 0"), T.PointsTier(0.f), 0);
	TestEqual(TEXT("tier at the first threshold"), T.PointsTier(T.Tiers[1].Min), 1);
	TestEqual(TEXT("tier just below"), T.PointsTier(T.Tiers[1].Min - 1.f), 0);
	TestEqual(TEXT("top tier"), T.PointsTier(1e9f), NumTiers - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRngTest, "SSX.Rng", TrickTestFlags)

bool FSSXRngTest::RunTest(const FString& Parameters)
{
	// 0x3A4A00
	const FGameRng D, Z(0u);
	bool bSame = true;
	for (int32 I = 0; I < 6; ++I) { bSame = bSame && D.S[I] == Z.S[I]; }
	TestTrue(TEXT("seed(0) == default state"), bSame);
	TestEqual(TEXT("default s0"), D.S[0], 0xF22D0E56u);

	FGameRng A(0u);
	const uint32 Want0[5] = { 0x559a51edu, 0x274ea7f5u, 0xe827f7e0u, 0x76b65632u, 0x7f1b3febu };
	for (int32 I = 0; I < 5; ++I) { TestEqual(FString::Printf(TEXT("seed 0 call %d"), I), A.Next(), Want0[I]); }
	FGameRng B(0x12345678u);
	for (int32 I = 0; I < 1000; ++I) { B.Next(); }
	TestEqual(TEXT("seed 0x12345678 call 1001"), B.Next(), 0x74ce6937u);
	const uint32 WantS[6] = { 0x74ce6937u, 0x07c6b350u, 0x6a6d899eu, 0x2af90a92u, 0x4ef68e45u, 0x821395c5u };
	bSame = true;
	for (int32 I = 0; I < 6; ++I) { bSame = bSame && B.S[I] == WantS[I]; }
	TestTrue(TEXT("state after 1001 calls"), bSame);
	FGameRng W;
	const uint32 Init[6] = { 1u, 2u, 3u, 4u, 0xFFFFFFFFu, 0xFFFFFFFFu };
	FMemory::Memcpy(W.S, Init, sizeof(Init));
	TestEqual(TEXT("wrap result"), W.Next(), 0xAu);
	TestTrue(TEXT("wrap state"), W.S[0] == 0xA && W.S[1] == 9 && W.S[2] == 7 && W.S[3] == 3 && W.S[4] == 0xFFFFFFFFu && W.S[5] == 0);
	FGameRng U(7u);
	TestTrue(TEXT("uniform(-1, 1)"), FMath::IsNearlyEqual(U.Uniform(-1.f, 1.f), -0.5887396335601807f, 1e-6f));
	TestEqual(TEXT("randint 19"), U.RandInt(19), 12u);
	TestEqual(TEXT("clock seed fold"), FGameRng::ClockSeed(0x10u, 0x3u), 0x10u ^ (0x3u << 3));
	return true;
}

#endif
