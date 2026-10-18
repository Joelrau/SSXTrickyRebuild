#include "Misc/AutomationTest.h"
#include "SSXHUD.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Air/RiderMessages.h"
#include "Sim/Air/TrickName.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/RiderSim.h"
#include "UI/SSXHudOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags PopupTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	int32 CountType(const FRiderMessages& Q, int32 Type)
	{
		int32 N = 0;
		for (const FRiderMessage& M : Q.Slots) { N += M.Type == Type ? 1 : 0; }
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudPopupsTest, "SSX.Hud.Popups", PopupTestFlags)

bool FSSXHudPopupsTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	{
		FRiderMessages Q;
		TestEqual(TEXT("first push -> slot 0"), Q.Push(SSXMsg::Points, 100, SSXMsg::LifePoints), 0);
		TestEqual(TEXT("trick name -> first free"), Q.Push(SSXMsg::TrickNew, 0, SSXMsg::LifeTrickName()), 1);
		TestEqual(TEXT("repeat name reuses the 7/8 slot"), Q.Push(SSXMsg::TrickRepeat, 0, SSXMsg::LifeTrickName()), 1);
		TestEqual(TEXT("multiplier -> free slot"), Q.Push(SSXMsg::Multiplier, 2, SSXMsg::LifeNever), 2);
		TestEqual(TEXT("multiplier reuses its slot"), Q.Push(SSXMsg::Multiplier, 3, SSXMsg::LifeNever), 2);
		TestEqual(TEXT("multiplier value replaced"), Q.Slots[2].Value, 3);
		TestTrue(TEXT("trick life is 0x404004DA"), FMath::IsNearlyEqual(SSXMsg::LifeTrickName(), 3.0003f, 1e-3f));
		for (int32 F = 0; F < 149; ++F) { Q.Age(); }
		TestEqual(TEXT("points still queued at frame 149"), Q.Slots[0].Type, int32(SSXMsg::Points));
		Q.Age();
		Q.Age();
		TestEqual(TEXT("points freed after 2.5 s"), Q.Slots[0].Type, 0);
		TestEqual(TEXT("multiplier never ages"), Q.Slots[2].Elapsed, 0.f);
		FRiderMessages Full;
		for (int32 I = 0; I < FRiderMessages::NumSlots; ++I) { Full.Push(SSXMsg::Checkpoint, I, SSXMsg::LifeCheckpoint); }
		TestEqual(TEXT("full queue -> last live slot"), Full.Push(SSXMsg::BigAir, 1000, SSXMsg::LifeBonus), FRiderMessages::NumSlots - 1);
		Full.Clear();
		TestEqual(TEXT("reset clears"), Full.Slots[5].Type, 0);
	}

	{
		FTrickScore S;
		S.StartSegment(false, 0, false);
		for (int32 F = 1; F <= 36; ++F) { S.UpdateAngles(10.0 * F, 0.0); }
		const double V = S.Value;
		S.Land(false, false);
		const int32 Want = FTrickScore::PointsFor(V, 1.0);
		TestEqual(TEXT("new trick -> type 7"), CountType(S.Messages, SSXMsg::TrickNew), 1);
		const FRiderMessage* P = S.Messages.Find(SSXMsg::Points);
		if (TestNotNull(TEXT("points -> type 2"), P))
		{
			TestEqual(TEXT("type 2 value"), P->Value, Want);
			TestEqual(TEXT("type 2 extra = unmultiplied"), P->Extra, Want);
			TestEqual(TEXT("type 2 life 2.5"), P->Life, 2.5f);
		}
		TestEqual(TEXT("score banked (+0xF0)"), S.Score, Want);
		TestEqual(TEXT("scored tricks"), S.ScoredTricks, 1);
		TestEqual(TEXT("ground landing clears the combo"), S.Combo, 0);
		if (SSXTrick::FTrickTables::Get().IsLoaded())
		{
			TestFalse(TEXT("trick text built"), S.TrickText.IsEmpty());
		}
		for (int32 F = 1; F <= 36; ++F) { S.UpdateAngles(10.0 * F, 0.0); }
		S.Land(false, false);
		TestEqual(TEXT("repeat -> type 8 in the 7/8 slot"), CountType(S.Messages, SSXMsg::TrickRepeat) == 1 && CountType(S.Messages, SSXMsg::TrickNew) == 0, true);
		const FRiderMessage* P3 = S.Messages.Find(SSXMsg::PointsRepeat);
		if (TestNotNull(TEXT("repeat points -> type 3"), P3))
		{
			TestEqual(TEXT("repeat points halved"), P3->Value, Want / 2);
		}
		S.UpdateAngles(180.0, 0.0);
		const int32 Lost = FTrickScore::PointsFor(S.Value, S.PointsMult);
		S.Bail();
		const FRiderMessage* P1 = S.Messages.Find(SSXMsg::PointsLost);
		if (TestNotNull(TEXT("bail -> type 1"), P1))
		{
			TestEqual(TEXT("type 1 value"), P1->Value, Lost);
		}
	}

	{
		FTrickScore S;
		S.StartSegment(false, 0, false);
		for (int32 F = 1; F <= 36; ++F) { S.UpdateAngles(10.0 * F, 0.0); }
		S.Land(false, true);
		TestEqual(TEXT("to-rail landing keeps the combo"), S.Combo, 1);
		TestEqual(TEXT("no combo bonus onto a rail"), CountType(S.Messages, SSXMsg::ComboBonus), 0);
		for (int32 F = 0; F < 70; ++F) { S.RailFrame(900.0); }
		const int32 Before = S.Score;
		S.StartSegment(true, RailKindUnknown, false);
		TestEqual(TEXT("rail > 1 s counts for the combo"), S.Combo, 2);
		TestEqual(TEXT("rail exit pays points"), S.Score > Before, true);
		TestEqual(TEXT("M_pts 1.3 after a rail with the switch flag"), S.PointsMult, RailStartPointsMult);
		S.UpdateAngles(180.0, 0.0);
		S.AirFrames = 300;
		S.Land(true, false);
		const FRiderMessage* Cb = S.Messages.Find(SSXMsg::ComboBonus);
		if (TestNotNull(TEXT("combo bonus -> type 0xF"), Cb))
		{
			TestEqual(TEXT("combo 3 -> 8000"), Cb->Value, 8000);
			TestEqual(TEXT("bonus life 2.2"), Cb->Life, 2.2f);
		}
		const FRiderMessage* Ba = S.Messages.Find(SSXMsg::BigAir);
		if (TestNotNull(TEXT("big air -> type 0x10"), Ba))
		{
			TestEqual(TEXT("5 s -> 2000"), Ba->Value, 2000);
		}
		TestEqual(TEXT("M_pts back to 1"), S.PointsMult, 1.0);
		FTrickScore N;
		N.SegmentType = RailKindUnknown;
		N.StartSegment(false, 0, false);
		TestEqual(TEXT("M_pts 1.0 after a rail without the switch flag"), N.PointsMult, 1.0);
	}

	{
		FRiderSim Sim;
		RegisterAirStates(Sim);
		Sim.Reset();
		FAirContext* Air = GetAirContext(Sim);
		if (!TestNotNull(TEXT("air context"), Air)) return false;
		Air->Score.Messages.Push(SSXMsg::Laps, 2, SSXMsg::LifeLaps);
		Air->Score.Messages.Push(SSXMsg::TrickNew, 0, SSXMsg::LifeTrickName());
		Air->Score.TrickText = TEXT("X ");
		Air->Score.Score = 1234;
		FSSXHudPlayerInput Pl;
		ASSXHUD::FillRider(Sim, Pl);
		TestEqual(TEXT("12 queue entries fed"), Pl.Messages.Num(), FRiderMessages::NumSlots);
		TestTrue(TEXT("lap message fed"), Pl.Messages[0].Type == SSXHudMsg::Laps && Pl.Messages[0].Value == 2 && Pl.Messages[0].IsLive());
		TestEqual(TEXT("trick text fed"), Pl.TrickText, FString(TEXT("X")));
		TestEqual(TEXT("showoff score fed"), Pl.Score, 1234.0);
		FSSXHudMsg Mult;
		Mult.Type = SSXHudMsg::Multiplier;
		Mult.Total = -1.f;
		TestTrue(TEXT("a never-ageing entry is live"), Mult.IsLive() && Mult.Progress() == 0.f);
	}
	return true;
}

#endif
