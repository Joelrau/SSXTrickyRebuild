#include "Effects/TrackEffects.h"
#include "Misc/AutomationTest.h"
#include "World/SSXPropAnimation.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldMultiplierGemsTest, "SSX.World.MultiplierGems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXWorldMultiplierGemsTest::RunTest(const FString&)
{
	SSXEffects::FEffectData D;
	FString Err;
	if (!D.Load(SSXEffects::FEffectData::DefaultPath(TEXT("pipe")), &Err))
	{
		AddError(Err);
		return false;
	}
	int32 Gems = 0, Spinning = 0;
	for (int32 Inst = 0; Inst < D.NumInstances(); ++Inst)
	{
		if (!USSXPropAnimation::IsMultiplierGem(D, Inst)) { continue; }
		++Gems;
		Spinning += D.EffectFor(Inst, 0) >= 0 ? 1 : 0;   // the cell-activation effect that starts the spin
	}
	AddInfo(FString::Printf(TEXT("pipe: %d multiplier crystals, %d with a spin effect"), Gems, Spinning));
	TestTrue(TEXT("Pipedream has multiplier crystals"), Gems > 0);
	TestEqual(TEXT("every crystal has its cell-activation (spin) effect"), Spinning, Gems);
	return true;
}

#endif
