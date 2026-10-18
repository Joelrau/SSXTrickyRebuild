// FUN_001579E0, 0x116AB8, FUN_00157C18
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Air/RiderMessages.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTrickBook.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceBookAfterStartTest, "SSX.Race.BookAfterStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXRaceBookAfterStartTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	const SSXFeTrickBook::FTable& Book = SSXFeTrickBook::FTable::Get();
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err)
		|| !SSXRider::FRiderTable::Shared().IsLoaded() || !Book.IsLoaded())
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("race / rider / trick-book data not available (%s): skipped"), *Err));
		return true;
	}
	constexpr int32 Elise = 7;
	FRiderSim Sim;
	SSXRiderSetup::RegisterStates(Sim, true);
	SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, Elise, 0);
	Sim.SetWorld(&World);
	TestEqual(TEXT("Elise's profile stance +0x40 (static record: goofy)"), Sim.Profile.Stance, 1);
	TestEqual(TEXT("Elise's board 0 (Kamoniwana) is class 0 (BX)"), Sim.Profile.BoardClass, 0);

	FSSXProfile Profile;
	static_cast<FSSXSaveData&>(Profile) = FSSXSaveData::NewSave();
	Profile.Slot[0].Char = Elise;

	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::PracticeRace;
	Session.Init(Data, Cfg);
	Session.AddRacer(&Sim, true);

	SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim);
	if (!TestNotNull(TEXT("air context"), Air))
	{
		return false;
	}
	int32 BookCalls = 0, CameraMessages = 0;
	Air->Score.BookCheck = [&Profile, &BookCalls](uint32 W0, uint32 W1) { ++BookCalls; return Profile.LandBookTrick(0, W0, W1) != 0; };
	Air->Score.Messages.OnPush = [&CameraMessages](int32 Type, int32) { CameraMessages += Type == int32(SSXMsg::Camera) ? 1 : 0; };

	Session.Start();
	for (int32 F = 0; F < 60 * 60 && Session.GetPhase() != ERacePhase::Race; ++F)
	{
		Session.Tick();
		Sim.Step(FRiderInputWord());
	}
	TestTrue(TEXT("race phase reached"), Session.GetPhase() == ERacePhase::Race);
	TestTrue(TEXT("trick-book hook survives the start placement (reset race_stats)"), (bool)Air->Score.BookCheck);
	TestTrue(TEXT("rider-message hook survives the start placement"), (bool)Air->Score.Messages.OnPush);
	TestTrue(TEXT("race start in the natural stance (reset 0x116AB8: +0x1B4 = profile +0x40): no Switch"),
		Sim.State.bStanceGoofy == (Sim.Profile.Stance != 0));

	Air->Score.StartSegment(true, 0, false);
	Air->Score.GrabNames.Add(0x0D);
	Air->Score.Land(false, 0, false, false);
	AddInfo(FString::Printf(TEXT("record %08x %08x '%s'"), Air->Score.LastRecord.W0, Air->Score.LastRecord.W1, *Air->Score.TrickText));
	TestEqual(TEXT("prefix Switch (w1 bits 16..19 = 1)"), int32((Air->Score.LastRecord.W1 >> 16) & 0xF), 1);
	TestEqual(TEXT("the book check ran once"), BookCalls, 1);
	TestTrue(TEXT("Elise's entry 0 (Mute Air) learned: career +0x1C bit 0"), (Profile.Career[Elise].BookDone[1] & 1u) != 0);
	TestEqual(TEXT("event word slot+0x10"), Profile.Slot[0].EventTricks, 1u);
	TestEqual(TEXT("HUD message 0x11 (camera icon) pushed"), CameraMessages, 1);
	const auto Landed = Air->Score.LastRecord;

	Session.Start(SSXIntro::EEntry::Restart);
	TestTrue(TEXT("trick-book hook survives a restart placement"), (bool)Air->Score.BookCheck);
	TestTrue(TEXT("rider-message hook survives a restart placement"), (bool)Air->Score.Messages.OnPush);
	TestEqual(TEXT("race_stats reset by the placement"), Air->Score.Score, 0);

	const FString Why = SSXFeTrickBook::ExplainNoBook(Book, Profile.Career[Elise], Landed.W0, Landed.W1);
	TestEqual(TEXT("book: rejected reason for a learned entry"), Why, FString(TEXT("entry 0 already learned")));
	return !HasAnyErrors();
}

#endif
