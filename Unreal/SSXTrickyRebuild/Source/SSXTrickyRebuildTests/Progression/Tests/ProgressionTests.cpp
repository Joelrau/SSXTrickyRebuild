#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Progression/SSXProgression.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Rider/RiderProfiles.h"
#include "UI/SSXFeProfile.h"

namespace
{
	constexpr EAutomationTestFlags ProgTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> LoadReference(FAutomationTestBase& T)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("progression_reference.json"));
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			SSXTest::Skip(T, TEXT("SimData/progression_reference.json missing (run tools/make_progression_reference.py); reference checks skipped"));
			return nullptr;
		}
		return Root;
	}

	TArray<int32> Ints(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<int32> R;
		for (const TSharedPtr<FJsonValue>& V : A) { R.Add((int32)(int64)V->AsNumber()); }
		return R;
	}

	bool RidersLoaded(FAutomationTestBase& T)
	{
		if (SSXRider::FRiderTable::Shared().IsLoaded()) return true;
		SSXTest::Skip(T, TEXT("SimData/riders.bin missing (tools/export_rider_profiles.py); rider-table checks skipped"));
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionRulesTest, "SSX.Progression.Rules", ProgTestFlags)
bool FSSXProgressionRulesTest::RunTest(const FString& Parameters)
{
	using namespace SSXProg;
	TestEqual(TEXT("scaled(15) = 6 (gold from none = 6 displayed points)"), Scaled(15), 6);
	TestEqual(TEXT("scaled(240) = 96"), Scaled(240), 96);
	TestEqual(TEXT("race medal 1st = gold"), RaceMedal(0), Gold);
	TestEqual(TEXT("race medal 4th = none"), RaceMedal(3), NoMedal);
	TestEqual(TEXT("Garibaldi 55000 = gold"), ShowoffMedal(55000, 0), Gold);
	TestEqual(TEXT("Garibaldi 54999 = silver"), ShowoffMedal(54999, 0), Silver);
	TestEqual(TEXT("Pipedream hard-coded bronze 250000"), ShowoffMedal(250000, TrackPipedream), Bronze);
	TestEqual(TEXT("Untracked has no showoff medal"), ShowoffMedal(9999999, TrackUntracked), NoMedal);
	TestEqual(TEXT("circuit order Tokyo 6th"), CircuitVenue(5), 8);
	TestEqual(TEXT("rank 240 = Master"), RankFromPoints(240), 0);
	TestEqual(TEXT("rank 4 = Newbie"), RankFromPoints(4), 9);

	FSSXSaveData S = FSSXSaveData::NewSave();
	TestEqual(TEXT("new save venues 0x407"), S.VenueUnlocks, 0x407u);
	TestEqual(TEXT("new save riders Mac, Moby, Elise, Eddie = 0x99"), S.RiderUnlocks, 0x99u);
	TestEqual(TEXT("new save boards 0/1"), S.Career[4].Boards, 0x3u);
	TestTrue(TEXT("venue 0 open in both WC modes"), S.Career[4].Tracks[0].RaceOpen && S.Career[4].Tracks[0].ShowoffOpen);
	TestEqual(TEXT("name PLAYER1"), S.GetUserName(0), FString(TEXT("PLAYER1")));
	// 0x165660
	{
		int32 W[5];
		FMemory::Memcpy(W, S.RiderConfig[1], sizeof(W));
		TestTrue(TEXT("rider 1 config {1, 0, 1, 0, 0}"), W[0] == 1 && W[1] == 0 && W[2] == 1 && W[3] == 0 && W[4] == 0);
		FMemory::Memcpy(W, S.RiderConfig[9], sizeof(W));
		TestTrue(TEXT("rider 9 config {9, 1, 7, 0, 0}"), W[0] == 9 && W[1] == 1 && W[2] == 7 && W[3] == 0 && W[4] == 0);
	}
	// 0x163BB8
	TestTrue(TEXT("WC: tutorial always open"), VenueUnlocked(S, WCRace, TrackTutorial, 4, 0, false));
	TestFalse(TEXT("WC: no slot-0 rider = locked"), VenueUnlocked(S, WCRace, 0, -1, ~0u, false));
	TestFalse(TEXT("WC: Snowdream locked on a new career"), VenueUnlocked(S, WCRace, 1, 4, ~0u, false));
	TestTrue(TEXT("SE: Elysium open"), VenueUnlocked(S, SERace, 2, -1, ~0u, false));
	TestFalse(TEXT("SE: mode mask applies"), VenueUnlocked(S, SERace, 2, -1, 0u, false));
	TestTrue(TEXT("unlock-all = mode mask"), VenueUnlocked(S, WCRace, 11, -1, 1u << 11, true));

	FSSXProgressSession Sess;
	TestEqual(TEXT("1st in the qualifier advances"), AdvanceHeat(Sess, 0), 4);
	TestEqual(TEXT("heat 1"), Sess.Heat, 1);
	TestTrue(TEXT("heat factor 2nd (f32 0x3EAAD18B)"), (AdvanceHeat(Sess, 1), Sess.HeatFactor == SSXHeats::HeatFactor(1)));
	TestEqual(TEXT("4th resets to heat 0 (exit code 2)"), AdvanceHeat(Sess, 3), 2);
	TestEqual(TEXT("heat 0"), Sess.Heat, 0);
	TestEqual(TEXT("no medal before the final"), AwardEvent(S, Sess, 4, WCRace, 0, 1, 0, 0).Num(), 0);
	TArray<FSSXUnlock> U = AwardEvent(S, Sess, 4, WCRace, 0, 2, 1, 0);
	TestEqual(TEXT("silver = 10 race points"), S.Career[4].Points[0], 10);
	TestTrue(TEXT("Snowdream race opened"), S.Career[4].Tracks[1].RaceOpen == 1);
	TestTrue(TEXT("unlock list starts with the medal record"), U.Num() > 0 && U[0] == FSSXUnlock{ 0, UMedal, Silver });
	AwardEvent(S, Sess, 4, WCRace, 0, 2, 2, 0);
	TestEqual(TEXT("worse medal keeps the points"), S.Career[4].Points[0], 10);
	U = AwardEvent(S, Sess, 4, WCRace, 0, 2, 0, 0);
	TestEqual(TEXT("gold over silver adds 5"), S.Career[4].Points[0], 15);
	TestEqual(TEXT("every opponent bonus += delta"), S.Career[4].Opponent[7][0], 15);
	TestTrue(TEXT("first gold unlocks Brodi"), (S.RiderUnlocks & (1u << 10)) != 0 && U.Contains(FSSXUnlock{ 0, URider, 10 }));
	TestTrue(TEXT("board 2 (cost 5) unlocked"), (S.Career[4].Boards & 4u) != 0);
	FSSXSaveData Q = FSSXSaveData::NewSave();
	U = AwardEvent(Q, Sess, 0, WCRace, 0, 2, 0, 0);
	TestTrue(TEXT("0 -> 15: Rookie (8)"), U.Contains(FSSXUnlock{ 0, URank, 8 }));
	AwardEvent(Q, Sess, 0, WCShowoff, 0, 2, 0, 40000);
	U = AwardEvent(Q, Sess, 0, WCRace, 1, 2, 1, 0);
	TestTrue(TEXT("25 -> 35: Natural (6)"), U.Contains(FSSXUnlock{ 0, URank, 6 }) && U.Num() > 0 && U.Last() == FSSXUnlock{ 0, URank, 6 });
	TestTrue(TEXT("rank-up flag"), Sess.bRankUp);
	FSSXSaveData B = FSSXSaveData::NewSave();
	B.Career[1].BookDone[1] = 0x1F;
	U = AwardEvent(B, Sess, 1, SERace, 0, 2, 0, 0);
	TestTrue(TEXT("chapter 1 -> outfit 1"), U.Contains(FSSXUnlock{ 0, UOutfit, 1 }) && (B.Career[1].Outfits & 2u));
	B.Career[1].BookDone[0] = (1u << 30) - 1;
	U = AwardEvent(B, Sess, 1, SERace, 0, 2, 0, 0);
	TestTrue(TEXT("full book -> type 8 + Uberboard"), U.Contains(FSSXUnlock{ 0, UBookDone, 1 }) && (B.Career[1].Boards & (1u << UberBoard)) && B.Career[1].BookComplete == 1);
	FSSXSaveData M = FSSXSaveData::NewSave();
	for (int32 I = 0; I < NumCircuit; ++I)
	{
		AwardEvent(M, Sess, 3, WCRace, CircuitVenue(I), 2, 0, 0);
		int32 T[3];
		ShowoffThresholds(CircuitVenue(I), T);
		AwardEvent(M, Sess, 3, WCShowoff, CircuitVenue(I), 2, 0, T[0]);
	}
	TestEqual(TEXT("16 golds = 240 rank points"), M.Career[3].RankPoints(), 240);
	TestTrue(TEXT("Master -> Chrome outfit 6"), (M.Career[3].Outfits & (1u << ChromeOutfit)) != 0);
	TestEqual(TEXT("Alaska medals open Untracked + Pipedream (0xDFF with the circuit)"), M.VenueUnlocks, 0xDFFu);
	TestTrue(TEXT("rank 240 does not unlock the Uberboard (cost 999)"), (M.Career[3].Boards & (1u << UberBoard)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionCareerReplayTest, "SSX.Progression.CareerReplay", ProgTestFlags)
bool FSSXProgressionCareerReplayTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	if (!Ref || !RidersLoaded(*this)) return true;
	FSSXSaveData S = FSSXSaveData::NewSave();
	FSSXProgressSession Sess;
	int32 Careers = 0, Events = 0;
	for (const TSharedPtr<FJsonValue>& CV : Ref->GetArrayField(TEXT("careers")))
	{
		const TSharedPtr<FJsonObject> C = CV->AsObject();
		const int32 Char = C->GetIntegerField(TEXT("char"));
		const TArray<int32> Book = Ints(C->GetArrayField(TEXT("book")));
		FSSXCareer& R = S.Career[Char];
		R.BookDone[0] = (uint32)Book[1];
		R.BookDone[1] = (uint32)Book[2];
		for (const TSharedPtr<FJsonValue>& EV : C->GetArrayField(TEXT("events")))
		{
			const TSharedPtr<FJsonObject> E = EV->AsObject();
			const TArray<FSSXUnlock> U = SSXProg::AwardEvent(S, Sess, Char, E->GetIntegerField(TEXT("mode")), E->GetIntegerField(TEXT("venue")),
				E->GetIntegerField(TEXT("heat")), E->GetIntegerField(TEXT("place")), E->GetIntegerField(TEXT("score")));
			TArray<FSSXUnlock> Want;
			for (const TSharedPtr<FJsonValue>& W : E->GetArrayField(TEXT("unlocks")))
			{
				const TArray<int32> X = Ints(W->AsArray());
				Want.Add({ X[0], X[1], X[2] });
			}
			TestTrue(FString::Printf(TEXT("char %d venue %d mode %d: unlock list = model"), Char, E->GetIntegerField(TEXT("venue")), E->GetIntegerField(TEXT("mode"))), U == Want);
			++Events;
		}
		const FString N = FString::Printf(TEXT("char %d"), Char);
		TestEqual(N + TEXT(" race rank points = RAM"), R.Points[0], C->GetIntegerField(TEXT("race_points")));
		TestEqual(N + TEXT(" showoff rank points = RAM"), R.Points[1], C->GetIntegerField(TEXT("showoff_points")));
		const TArray<TSharedPtr<FJsonValue>>& Tracks = C->GetArrayField(TEXT("tracks"));
		bool bTracks = true;
		for (int32 T = 0; T < SSXProg::NumTracks; ++T)
		{
			const TArray<int32> X = Ints(Tracks[T]->AsArray());
			const FSSXTrackState& Y = R.Tracks[T];
			bTracks &= X[0] == Y.ShowoffMedal && X[1] == Y.RaceMedal && X[2] == Y.RaceOpen && X[3] == Y.ShowoffOpen;
		}
		TestTrue(N + TEXT(" 13 x 4 track bytes = RAM"), bTracks);
		TestEqual(N + TEXT(" boards = RAM"), R.Boards, (uint32)(int64)C->GetNumberField(TEXT("boards")));
		TestEqual(N + TEXT(" outfits = RAM"), R.Outfits, (uint32)(int64)C->GetNumberField(TEXT("outfits")));
		const TArray<TSharedPtr<FJsonValue>>& Opp = C->GetArrayField(TEXT("opponents"));
		bool bOpp = true;
		for (int32 K = 0; K < SSXProg::NumRiders; ++K) { bOpp &= Ints(Opp[K]->AsArray())[0] == R.Opponent[K][0]; }
		TestTrue(N + TEXT(" 12 opponent bonuses = RAM"), bOpp);
		TestEqual(N + TEXT(" unspent raw points = model"), R.RawPoints, C->GetObjectField(TEXT("model"))->GetIntegerField(TEXT("raw")));
		const TArray<int32> Attr = Ints(C->GetArrayField(TEXT("attr"))), Pts = Ints(C->GetArrayField(TEXT("points")));
		for (int32 A = 0; A < 4; ++A)
		{
			if (Pts[A] == R.StatPoints[A]) { TestEqual(N + TEXT(" untouched attribute = base"), (int32)R.Attr[A], Attr[A]); continue; }
			const int32 Max = SSXProg::StatBarMax(Char, A);
			TestEqual(N + TEXT(" raised attribute = int(pts/max*alt)"), (int32)(uint8)(int32)((float)Pts[A] / (float)Max * (float)SSXProg::AttributeCap(Char, A)), Attr[A]);
		}
		++Careers;
	}
	const TSharedPtr<FJsonObject> Masks = Ref->GetObjectField(TEXT("masks"));
	TestEqual(TEXT("venue mask = RAM 0xDFF"), S.VenueUnlocks, (uint32)(int64)Masks->GetNumberField(TEXT("ram_venue")));
	TestEqual(TEXT("rider mask = RAM 0xFFF"), S.RiderUnlocks, (uint32)(int64)Masks->GetNumberField(TEXT("ram_rider")));
	TestEqual(TEXT("three careers replayed"), Careers, 3);
	AddInfo(FString::Printf(TEXT("replayed %d careers, %d events; masks %#x / %#x"), Careers, Events, S.VenueUnlocks, S.RiderUnlocks));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionStatsTest, "SSX.Progression.Stats", ProgTestFlags)
bool FSSXProgressionStatsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	if (!Ref || !RidersLoaded(*this)) return true;
	const TArray<TSharedPtr<FJsonValue>>& Max = Ref->GetArrayField(TEXT("stat_max"));
	const TArray<TSharedPtr<FJsonValue>>& Alt = Ref->GetArrayField(TEXT("alt"));
	int32 Same = 0;
	for (int32 C = 0; C < SSXProg::NumRiders; ++C)
	{
		const TArray<int32> M = Ints(Max[C]->AsArray()), A = Ints(Alt[C]->AsArray());
		for (int32 I = 0; I < 4; ++I)
		{
			Same += SSXProg::StatBarMax(C, I) == M[I] && SSXProg::AttributeCap(C, I) == A[I];
		}
	}
	TestEqual(TEXT("48 bar maxima round(0.4 alt) = model"), Same, 48);
	TestTrue(TEXT("Eddie bar maxima 34/38/34/34"), SSXProg::StatBarMax(0, 0) == 34 && SSXProg::StatBarMax(0, 1) == 38 && SSXProg::StatBarMax(0, 2) == 34 && SSXProg::StatBarMax(0, 3) == 34);
	// 0x166A58
	FSSXSaveData S = FSSXSaveData::NewSave();
	S.Career[0].RawPoints = 15;
	FSSXPointSession P;
	SSXProg::BuildPointSession(S, 0, false, P);
	TestEqual(TEXT("left = round(0.4 * 15) = 6"), P.Left, 6);
	const int32 Before = S.Career[0].StatPoints[1];
	TestTrue(TEXT("add"), SSXProg::PointAdd(S, P, 1));
	TestTrue(TEXT("add"), SSXProg::PointAdd(S, P, 1));
	TestTrue(TEXT("remove"), SSXProg::PointRemove(S, P, 1));
	TestEqual(TEXT("points +1"), (int32)S.Career[0].StatPoints[1], Before + 1);
	TestEqual(TEXT("raw = int(left / initial * rawAtOpen) = int(5/6*15) = 12"), S.Career[0].RawPoints, 12);
	TestEqual(TEXT("attr = int(cur / max * alt)"), (int32)S.Career[0].Attr[1], (int32)(uint8)(int32)((float)(Before + 1) / (float)P.Max[1] * (float)SSXProg::AttributeCap(0, 1)));
	TestFalse(TEXT("cannot remove below the floor"), (SSXProg::PointRemove(S, P, 1), SSXProg::PointRemove(S, P, 1)));
	SSXProg::BuildPointSession(S, 0, true, P);
	TestTrue(TEXT("max-attributes cheat: no points, value = max"), P.Left == 0 && P.Cur[0] == P.Max[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionRecordsTest, "SSX.Progression.Records", ProgTestFlags)
bool FSSXProgressionRecordsTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	if (!Ref) return true;
	if (!SSXProg::RecordDefaultsLoaded())
	{
		SSXTest::Skip(*this, TEXT("converted/frontend/progression_tables.json missing (tools/export_progression_data.py)"));
		return true;
	}
	const FSSXSaveData S = FSSXSaveData::NewSave();
	int32 Defaults = 0, Replayed = 0, Ok = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("records")))
	{
		const TSharedPtr<FJsonObject> R = V->AsObject();
		const int32 Track = R->GetIntegerField(TEXT("track"));
		const FString KindName = R->GetStringField(TEXT("kind"));
		const int32 Kind = KindName == TEXT("race") ? 0 : KindName == TEXT("showoff") ? 1 : 2;
		FSSXRecordTable T = S.Records[Track].Tables[Kind];
		TestTrue(TEXT("table header (count, higher-better, is-time) = RAM"), T.Count == R->GetIntegerField(TEXT("count"))
			&& T.HigherBetter == R->GetIntegerField(TEXT("higher_better")) && T.IsTime == R->GetIntegerField(TEXT("is_time")));
		const TArray<TSharedPtr<FJsonValue>>& Inserts = R->GetArrayField(TEXT("inserts"));
		for (const TSharedPtr<FJsonValue>& I : Inserts)
		{
			const TArray<TSharedPtr<FJsonValue>>& X = I->AsArray();
			const int32 Row = T.Insert((int32)X[1]->AsNumber(), X[0]->AsString(), 0);
			TestEqual(TEXT("insert row = model"), Row, (int32)X[2]->AsNumber());
		}
		(Inserts.Num() ? Replayed : Defaults) += 1;
		bool bSame = true;
		const TArray<TSharedPtr<FJsonValue>>& Rows = R->GetArrayField(TEXT("rows"));
		for (int32 E = 0; E < Rows.Num(); ++E)
		{
			const TArray<TSharedPtr<FJsonValue>>& X = Rows[E]->AsArray();
			bSame &= T.Rows[E].GetName() == X[0]->AsString() && T.Rows[E].Value == (int32)X[2]->AsNumber();
		}
		Ok += bSame;
		TestTrue(FString::Printf(TEXT("track %d %s rows = RAM"), Track, *KindName), bSame);
	}
	TestEqual(TEXT("23 tables equal the defaults"), Defaults, 23);
	TestEqual(TEXT("16 tables = defaults + JOE rows by the insertion rule"), Replayed, 16);
	AddInfo(FString::Printf(TEXT("records: %d default, %d replayed, %d / 39 equal RAM"), Defaults, Replayed, Ok));
	FSSXRecordTable T = S.Records[0].Tables[0];
	TestEqual(TEXT("tie with row 1 goes below it"), T.Insert(T.Rows[0].Value, TEXT("A"), 1), 1);
	TestEqual(TEXT("too slow: no entry"), T.Insert(999999, TEXT("B"), 1), (int32)INDEX_NONE);
	FSSXSaveData E = FSSXSaveData::NewSave();
	FSSXProgressSession Sess;
	FSSXEventResult Ev;
	Ev.Mode = SSXProg::WCRace;
	Ev.Venue = 0;
	Ev.Humans.Add({ 0, 4, 0, 20000, 0, TEXT("PLAYER1") });
	SSXProg::EndEvent(E, Sess, Ev, false);
	TestEqual(TEXT("new record is row 0, pending"), Sess.PendingRecords.Num() == 1 ? Sess.PendingRecords[0].Row : -1, 0);
	TestEqual(TEXT("WC best time stored"), E.Career[4].BestTime[0], 20000);
	TestEqual(TEXT("heat 0: no medal"), Sess.LastMedal, (int32)SSXProg::NoMedal);
	// 0x164FC0, 0x111820
	FSSXSaveData Dnf = FSSXSaveData::NewSave();
	FSSXProgressSession Sess2;
	FSSXEventResult Ev2;
	Ev2.Mode = SSXProg::SERace;
	Ev2.Venue = 0;
	Ev2.Humans.Add({ 0, 4, 5, 50000, 0, TEXT("PLAYER1") });
	const FSSXRecordTable Before = Dnf.Records[0].Tables[0];
	SSXProg::EndEvent(Dnf, Sess2, Ev2, false);
	FSSXRecordTable Expect = Before;
	const int32 Row = Expect.Insert(50000, TEXT("PLAYER1"), 4);
	TestEqual(TEXT("DNF 50000: the insertion rule decides"), Sess2.PendingRecords.Num(), Row == INDEX_NONE ? 0 : 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionRiderBestsTest, "SSX.Progression.RiderBests", ProgTestFlags)
bool FSSXProgressionRiderBestsTest::RunTest(const FString& Parameters)
{
	using namespace SSXProg;
	FSSXSaveData S = FSSXSaveData::NewSave();
	constexpr int32 Char = 4, Snow = 1;
	FSSXCareer& C = S.Career[Char];
	// 0x111820, 0x166CB0
	TestFalse(TEXT("freeride: no career bests"), CareerBestsMode(Freeride));
	TestFalse(TEXT("lesson: no career bests"), CareerBestsMode(Tutorial));
	TestTrue(TEXT("race / showoff / WC / TC modes"), CareerBestsMode(SERace) && CareerBestsMode(SEShowoff) && CareerBestsMode(WCRace)
		&& CareerBestsMode(WCShowoff) && CareerBestsMode(TimeChallenge));
	FSSXRaceStatBlock B;
	B.Knockdowns = 2; B.Wipeouts = 1; B.Tricks = 17; B.Ubers = 3; B.AirBest = 3.25f; B.GrabBest = 1.f; B.RailBest = 1.5f;
	const int32 Col[6] = { 5, 4, 3, 2, 1, 0 };
	for (int32 K = 0; K < 6; ++K) { B.Colour[K] = Col[K]; }
	CareerBests(C, B, Snow);
	auto F = [](const FSSXCareerBest& X) { float V; FMemory::Memcpy(&V, &X.Value, 4); return V; };
	TestTrue(TEXT("knockdowns 2 at Snowdream"), C.Bests[0].Value == 2u && C.Bests[0].Track == Snow);
	TestTrue(TEXT("tricks 17 at Snowdream"), C.Bests[2].Value == 17u && C.Bests[2].Track == Snow);
	TestTrue(TEXT("air 3.25 s at Snowdream"), F(C.Bests[4]) == 3.25f && C.Bests[4].Track == Snow);
	TestTrue(TEXT("green 5 at Snowdream"), C.TrickColour[0].Value == 5u && C.TrickColour[0].Track == Snow);
	TestTrue(TEXT("black 0 is not larger: new-save {0, 0} kept"), C.TrickColour[5].Value == 0u && C.TrickColour[5].Track == 0);
	FSSXRaceStatBlock B2 = B;
	B2.Knockdowns = 3; B2.Tricks = 10;
	CareerBests(C, B2, 0);
	TestTrue(TEXT("larger knockdowns move to Garibaldi"), C.Bests[0].Value == 3u && C.Bests[0].Track == 0);
	TestTrue(TEXT("smaller tricks keep 17 at Snowdream"), C.Bests[2].Value == 17u && C.Bests[2].Track == Snow);
	TestEqual(TEXT("tied air keeps its track"), C.Bests[4].Track, (int32)Snow);
	// 0x167010
	CountEvent(C, Snow); CountEvent(C, Snow);
	TestEqual(TEXT("two events at Snowdream"), C.EventCount[Snow], 2);
	// 0x166FA0, 0x166FE0
	auto Event = [&S](int32 Mode, int32 Time, int32 Score)
	{
		FSSXProgressSession Sess;
		FSSXEventResult Ev;
		Ev.Mode = Mode; Ev.Venue = Snow;
		Ev.Humans.Add({ 0, Char, 0, Time, Score, TEXT("PLAYER1") });
		EndEvent(S, Sess, Ev, false);
	};
	Event(WCRace, 17000, 0);
	TestEqual(TEXT("WC heat 1st 2:50.00 -> best time"), C.BestTime[Snow], 17000);
	Event(WCRace, 18000, 0);
	TestEqual(TEXT("slower WC heat: best kept"), C.BestTime[Snow], 17000);
	Event(SERace, 16000, 0);
	TestEqual(TEXT("single-event race does not count"), C.BestTime[Snow], 17000);
	Event(WCShowoff, -1, 50000);
	TestEqual(TEXT("WC showoff score"), C.BestScore[Snow], 50000);
	Event(SEShowoff, -1, 90000);
	TestEqual(TEXT("single-event showoff does not count"), C.BestScore[Snow], 50000);
	FSSXSaveData L;
	TestTrue(TEXT("blob loads"), L.FromBlob(S.ToBlob()));
	TestTrue(TEXT("career round trip"), L.Career[Char] == C);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionBlobTest, "SSX.Progression.Blob", ProgTestFlags)
bool FSSXProgressionBlobTest::RunTest(const FString& Parameters)
{
	const FSSXSaveData S = FSSXSaveData::NewSave();
	const TArray<uint8> B = S.ToBlob();
	TestEqual(TEXT("blob 0x27D4 bytes"), B.Num(), SSXProg::BlobSize);
	uint32 Magic = 0;
	FMemory::Memcpy(&Magic, B.GetData() + SSXProg::BlobMagicOffset, 4);
	TestEqual(TEXT("magic 0x12300003"), Magic, SSXProg::BlobMagic);
	FSSXSaveData L;
	TestTrue(TEXT("checksum verifies"), L.FromBlob(B));
	TestTrue(TEXT("round trip"), L.ToBlob() == B);
	TArray<uint8> Bad = B;
	Bad[0x1000] ^= 1;
	TestFalse(TEXT("a changed byte fails the checksum (LOC 372)"), FSSXSaveData().FromBlob(Bad));

	TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	if (!Ref) return true;
	int32 Cases = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("crc_cases")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		uint32 X = (uint32)(int64)C->GetNumberField(TEXT("seed"));
		TArray<uint8> G;
		G.SetNumUninitialized(SSXProg::BlobSize);
		for (int32 I = 0; I < G.Num(); ++I) { X = (X * 1103515245u + 12345u) & 0x7FFFFFFFu; G[I] = (uint8)((X >> 16) & 0xFF); }
		FMemory::Memzero(G.GetData() + SSXProg::BlobChecksum, 4);
		FMemory::Memcpy(G.GetData() + SSXProg::BlobMagicOffset, &SSXProg::BlobMagic, 4);
		TestEqual(FString::Printf(TEXT("crc seed %u = model"), (uint32)(int64)C->GetNumberField(TEXT("seed"))), (int32)FSSXSaveData::BlobCrc(G), C->GetIntegerField(TEXT("crc")));
		++Cases;
	}
	const TSharedPtr<FJsonObject> RB = Ref->GetObjectField(TEXT("blob"));
	TArray<uint8> Ram;
	const FString Hex = RB->GetStringField(TEXT("hex"));
	Ram.SetNumUninitialized(Hex.Len() / 2);
	for (int32 I = 0; I < Ram.Num(); ++I) { Ram[I] = (uint8)((FParse::HexDigit(Hex[2 * I]) << 4) | FParse::HexDigit(Hex[2 * I + 1])); }
	TestEqual(TEXT("RAM blob CRC = model"), (int32)FSSXSaveData::BlobCrc(Ram), RB->GetIntegerField(TEXT("crc")));
	FSSXSaveData R;
	TestTrue(TEXT("RAM blob parses (checksum word is 0 after a load)"), R.FromBlob(Ram, false));
	TArray<uint8> Out = R.ToBlob();
	FMemory::Memzero(Out.GetData() + SSXProg::BlobChecksum, 4);
	int32 FirstDiff = INDEX_NONE;
	for (int32 I = 0; I < FMath::Min(Out.Num(), Ram.Num()) && FirstDiff == INDEX_NONE; ++I) { if (Out[I] != Ram[I]) FirstDiff = I; }
	TestEqual(TEXT("RAM blob written back byte for byte (first differing blob offset)"), FirstDiff, (int32)INDEX_NONE);
	TestEqual(TEXT("RAM user name 1 'JOE'"), R.GetUserName(0), FString(TEXT("JOE")));
	if (RidersLoaded(*this))
	{
		const TArray<uint8> N = S.ToBlob();
		int32 Same = 0, Unplayed = 0;
		for (int32 C = 0; C < SSXProg::NumRiders; ++C)
		{
			if (R.Career[C].RankPoints() != 0) continue;
			++Unplayed;
			const int32 O = 0x100 - 8 + C * 0x1C4;
			Same += FMemory::Memcmp(N.GetData() + O, Ram.GetData() + O, 0x98) == 0 && FMemory::Memcmp(N.GetData() + O + 0xCC, Ram.GetData() + O + 0xCC, 0x164 - 0xCC) == 0;
		}
		TestEqual(TEXT("new-save career bytes = RAM for every unplayed rider"), Same, Unplayed);
		AddInfo(FString::Printf(TEXT("%d CRC cases; RAM blob round trip; %d / %d unplayed careers equal"), Cases, Same, Unplayed));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionBoardGateTest, "SSX.Progression.BoardGate", ProgTestFlags)
bool FSSXProgressionBoardGateTest::RunTest(const FString&)
{
	// 0x163D30, 0x163D98, 0x3352A0, FUN_00267B18, 0x1579E0
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCareer& C = P.Career[4];
	C.Boards = 1u;
	C.Outfits = 1u;
	TestFalse(TEXT("board 5 closed"), P.IsBoardUnlocked(4, 5, false));
	TestTrue(TEXT("unlock-all opens every board"), P.IsBoardUnlocked(4, 11, true));
	int32 B = -1, O = -1;
	P.ShownGear(4, 5, 3, true, B, O);
	TestTrue(TEXT("cheat on: picked board / outfit shown"), B == 5 && O == 3);
	P.ShownGear(4, 5, 3, false, B, O);
	TestTrue(TEXT("cheat off: board 0 / outfit 0 shown"), B == 0 && O == 0);
	C.Boards |= 1u << 5;
	P.ShownGear(4, 5, 3, false, B, O);
	TestTrue(TEXT("bought board shown, locked outfit 0"), B == 5 && O == 0);
	return true;
}

// FUN_00112AF0, 0x164840
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXProgressionHelpRaceTest, "SSX.Progression.HelpRace", ProgTestFlags)
bool FSSXProgressionHelpRaceTest::RunTest(const FString& Parameters)
{
	using namespace SSXProg;
	TestTrue(TEXT("new career (0 points, Newbie 9): shown"), HelpRaceNeeded(0));
	TestTrue(TEXT("4 points (9): shown"), HelpRaceNeeded(4));
	TestTrue(TEXT("5 points (8): shown"), HelpRaceNeeded(5));
	TestTrue(TEXT("19 points (8): shown"), HelpRaceNeeded(19));
	TestFalse(TEXT("20 points (7): not shown"), HelpRaceNeeded(20));
	TestFalse(TEXT("240 points (Master): not shown"), HelpRaceNeeded(240));
	return true;
}

#endif
