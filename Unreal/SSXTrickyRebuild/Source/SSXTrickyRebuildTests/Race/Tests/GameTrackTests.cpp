#include "Misc/AutomationTest.h"
#include "Progression/SSXProgression.h"
#include "Race/RaceRules.h"
#include "Race/RaceTrackData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags GameTrackFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

// The race data numbers Alaska 10 (stem order); the game's per-track tables use its track index 11. User's PCSX2
// screenshot of an Alaska Single Event Showoff: clock 2:14.78 right after GO, BRONZE 150000.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceGameTrackTest, "SSX.Race.GameTrack", GameTrackFlags)
bool FSSXRaceGameTrackTest::RunTest(const FString&)
{
	TestEqual(TEXT("trick tutorial: data 9 -> game 10"), FRaceTrackData::GameTrackOf(9), 10);
	TestEqual(TEXT("Alaska: data 10 -> game 11"), FRaceTrackData::GameTrackOf(10), 11);
	for (int32 T = 0; T <= 8; ++T)
	{
		TestEqual(FString::Printf(TEXT("track %d unchanged"), T), FRaceTrackData::GameTrackOf(T), T);
	}
	TestEqual(TEXT("Alaska showoff time limit (0x332F34)"), SSXRace::ShowoffTimeLimit(FRaceTrackData::GameTrackOf(10)), 135.0);
	int32 T[3] = {};
	TestTrue(TEXT("Alaska has showoff medal thresholds"), SSXProg::ShowoffThresholds(FRaceTrackData::GameTrackOf(10), T));
	TestEqual(TEXT("Alaska bronze"), T[2], 150000);
	return true;
}

#endif
