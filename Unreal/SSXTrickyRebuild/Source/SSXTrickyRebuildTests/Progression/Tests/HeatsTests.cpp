#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Progression/SSXHeats.h"
#include "Progression/SSXProgression.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HeatsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> LoadHeatsRef(FAutomationTestBase& T)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!SSXHeats::FData::Get().bLoaded
			|| !FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("heats_reference.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
		{
			SSXTest::Skip(T, TEXT("SimData/heats_data.json / heats_reference.json missing (tools/export_heats_data.py); skipped"));
			return nullptr;
		}
		return Root;
	}

	struct FSeq
	{
		uint32 X;
		explicit FSeq(uint32 Seed) : X(Seed) {}
		uint32 operator()() { X = X * 1103515245u + 12345u; return X; }
	};

	TArray<int32> Ints(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<int32> O;
		for (const TSharedPtr<FJsonValue>& V : A) O.Add(int32(V->AsNumber()));
		return O;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHeatsRulesTest, "SSX.Heats.Rules", HeatsTestFlags)
bool FSSXHeatsRulesTest::RunTest(const FString&)
{
	auto Bits = [](float F) { uint32 B; FMemory::Memcpy(&B, &F, 4); return B; };
	TestEqual(TEXT("1st 0x3F553F3C"), Bits(SSXHeats::HeatFactor(0)), 0x3F553F3Cu);
	TestEqual(TEXT("2nd 0x3EAAD18B"), Bits(SSXHeats::HeatFactor(1)), 0x3EAAD18Bu);
	TestEqual(TEXT("3rd 0x3E2AD18B"), Bits(SSXHeats::HeatFactor(2)), 0x3E2AD18Bu);
	TestEqual(TEXT("exit next heat"), SSXHeats::CircuitExitCode(4, true), 4);
	TestEqual(TEXT("exit failed heat"), SSXHeats::CircuitExitCode(4, false), 2);
	TestEqual(TEXT("exit other mode"), SSXHeats::CircuitExitCode(2, true), 2);
	// CircuitHeatEnd: heat 0 won -> heat 1, next-heat entry; 4th in heat 1 -> heat 0, exit 2; final never advances
	FSSXProgressSession Sess;
	TestEqual(TEXT("won heat 0"), SSXProg::CircuitHeatEnd(Sess, { 4, 0, 1, 2, 3, 5 }, 4, 4), 4);
	TestTrue(TEXT("heat 1, factor, next-heat entry"), Sess.Heat == 1 && Sess.HeatFactor == SSXHeats::HeatFactor(0) && Sess.bNextHeatEntry);
	TestTrue(TEXT("riders 4th..6th eliminated"), Sess.Elim.Flag[2] == 1 && Sess.Elim.Flag[3] == 1 && Sess.Elim.Flag[5] == 1 && Sess.Elim.Flag[0] == 0);
	TestEqual(TEXT("4th in heat 1"), SSXProg::CircuitHeatEnd(Sess, { 0, 1, 6, 4, 7, 8 }, 4, 4), 2);
	TestEqual(TEXT("back to heat 0"), Sess.Heat, 0);
	Sess.Heat = 2;
	TestEqual(TEXT("the final never advances"), SSXProg::CircuitHeatEnd(Sess, { 4, 0, 1, 6, 7, 8 }, 4, 4), 2);
	// 0x16D0B0, 0x16BBA0
	FSSXProgressSession F;
	SSXProg::CircuitHeatEnd(F, { 0, 4, 1, 2, 3, 5 }, 4, 4);
	TestEqual(TEXT("2nd in heat 0 -> factor 2nd"), F.HeatFactor, SSXHeats::HeatFactor(1));
	SSXProg::CircuitHeatEnd(F, { 0, 1, 2, 4, 3, 5 }, 4, 4);
	TestTrue(TEXT("4th in heat 1 keeps the factor (next heat 0 starts with it)"), F.Heat == 0 && F.HeatFactor == SSXHeats::HeatFactor(1));
	F.Heat = 2;
	SSXProg::CircuitHeatEnd(F, { 4, 0, 1, 2, 3, 5 }, 4, 4);
	TestEqual(TEXT("1st in the final sets the factor"), F.HeatFactor, SSXHeats::HeatFactor(0));
	SSXProg::AdvanceHeat(F, 3);
	TestEqual(TEXT("AdvanceHeat 4th keeps it"), F.HeatFactor, SSXHeats::HeatFactor(0));
	F.Heat = 1;
	F.ClearCircuitContext();
	TestTrue(TEXT("0x16BBA0 clears heat and factor"), F.Heat == 0 && F.HeatFactor == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHeatsModelTest, "SSX.Heats.Model", HeatsTestFlags)
bool FSSXHeatsModelTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadHeatsRef(*this);
	if (!Ref) return true;
	const SSXHeats::FData& D = SSXHeats::FData::Get();
	TestEqual(TEXT("story table 0 records"), D.Tables[0].Num(), 9);
	TestEqual(TEXT("story table 1 records"), D.Tables[1].Num(), 133);
	TestEqual(TEXT("story table 2 records"), D.Tables[2].Num(), 159);
	int32 Lookups = 0, LookupOk = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("lookups")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FSeq Rnd(uint32(O->GetNumberField(TEXT("seed"))));
		int32 Draws = 0;
		const SSXHeats::FStoryResult R = SSXHeats::StoryLookup(D, int32(O->GetNumberField(TEXT("rider"))), int32(O->GetNumberField(TEXT("venue"))),
			int32(O->GetNumberField(TEXT("mode"))), int32(O->GetNumberField(TEXT("heat"))), [&]() { ++Draws; return Rnd(); });
		const TArray<int32> Riders = Ints(O->GetArrayField(TEXT("riders"))), Types = Ints(O->GetArrayField(TEXT("types"))), Ids = Ints(O->GetArrayField(TEXT("ids")));
		bool bOk = Draws == int32(O->GetNumberField(TEXT("draws")));
		for (int32 K = 0; K < 5; ++K) bOk &= R.Riders[K] == Riders[K];
		for (int32 K = 0; K < 3; ++K) bOk &= R.IntroTypes[K] == Types[K] && R.IntroIds[K] == Ids[K];
		++Lookups;
		LookupOk += bOk ? 1 : 0;
	}
	int32 Heats = 0, HeatOk = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("grids")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FSeq Rnd(uint32(O->GetNumberField(TEXT("seed"))));
		auto R = [&]() { return Rnd(); };
		const int32 Rider = int32(O->GetNumberField(TEXT("rider"))), Venue = int32(O->GetNumberField(TEXT("venue"))), Mode = int32(O->GetNumberField(TEXT("mode")));
		SSXHeats::FElimination E;
		E.InitHeat0(D, Rider, Venue, Mode, R);
		int32 H = 0;
		for (const TSharedPtr<FJsonValue>& HV : O->GetArrayField(TEXT("heats")))
		{
			const TSharedPtr<FJsonObject> W = HV->AsObject();
			int32 Grid[SSXHeats::GridSize];
			const SSXHeats::FStoryResult S = SSXHeats::BuildGrid(E, D, Rider, Venue, Mode, H, R, Grid);
			const TArray<int32> WG = Ints(W->GetArrayField(TEXT("grid"))), WT = Ints(W->GetArrayField(TEXT("types")));
			bool bOk = true;
			for (int32 I = 0; I < SSXHeats::GridSize; ++I) bOk &= Grid[I] == WG[I];
			for (int32 K = 0; K < 3; ++K) bOk &= S.IntroTypes[K] == WT[K];
			float F = 0.f;
			const bool bAdv = SSXHeats::HeatResult(E, Ints(W->GetArrayField(TEXT("finish"))), Rider, H, F);
			bOk &= bAdv == W->GetBoolField(TEXT("advance")) && F == float(W->GetNumberField(TEXT("factor")));
			const TArray<TSharedPtr<FJsonValue>>& St = W->GetArrayField(TEXT("status"));
			const TArray<int32> Fl = Ints(W->GetArrayField(TEXT("flag")));
			for (int32 X = 0; X < SSXHeats::NumRiders; ++X)
			{
				const TArray<int32> Row = Ints(St[X]->AsArray());
				for (int32 K = 0; K < 3; ++K) bOk &= E.Status[X][K] == Row[K];
				bOk &= E.Flag[X] == Fl[X];
			}
			++Heats;
			HeatOk += bOk ? 1 : 0;
			++H;
		}
	}
	int32 Cases = 0, LevelOk = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("levels")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const TArray<int32> Riders = Ints(O->GetArrayField(TEXT("riders"))), Car = Ints(O->GetArrayField(TEXT("carried"))), Cr = Ints(O->GetArrayField(TEXT("career")));
		int32 Carried[12], Career[12];
		for (int32 K = 0; K < 12; ++K) { Carried[K] = Car[K]; Career[K] = Cr[K]; }
		TArray<TArray<int32>> L;
		SSXHeats::InitialLevels(D, Riders, int32(O->GetNumberField(TEXT("human"))), int32(O->GetNumberField(TEXT("heat"))), Carried, Career, L);
		const TArray<TSharedPtr<FJsonValue>>& W = O->GetArrayField(TEXT("levels"));
		bool bOk = true;
		for (int32 I = 0; I < 6; ++I) bOk &= L[I] == Ints(W[I]->AsArray());
		++Cases;
		LevelOk += bOk ? 1 : 0;
	}
	TestEqual(TEXT("story lookups == model"), LookupOk, Lookups);
	TestEqual(TEXT("heat grids / results / elimination == model"), HeatOk, Heats);
	TestEqual(TEXT("relationship levels == model"), LevelOk, Cases);
	AddInfo(FString::Printf(TEXT("heats model: %d/%d lookups, %d/%d heats (grid, intro types, result, factor, elimination), %d/%d level cases"),
		LookupOk, Lookups, HeatOk, Heats, LevelOk, Cases));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHeatsRamTest, "SSX.Heats.Ram", HeatsTestFlags)
bool FSSXHeatsRamTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadHeatsRef(*this);
	if (!Ref) return true;
	const SSXHeats::FData& D = SSXHeats::FData::Get();
	const TArray<TSharedPtr<FJsonValue>>& Ram = Ref->GetArrayField(TEXT("ram"));
	if (Ram.Num() != 2) return true;
	TArray<TSharedPtr<FJsonObject>> Snap;
	for (const TSharedPtr<FJsonValue>& V : Ram) Snap.Add(V->AsObject());
	int32 TotalBytes = 0;
	for (const TSharedPtr<FJsonObject>& S : Snap)
	{
		const FString Name = S->GetStringField(TEXT("name"));
		const TArray<int32> Grid = Ints(S->GetArrayField(TEXT("grid")));
		const int32 Human = int32(S->GetNumberField(TEXT("human"))), Heat = int32(S->GetNumberField(TEXT("heat")));
		const int32 Venue = int32(S->GetNumberField(TEXT("venue"))), Mode = int32(S->GetNumberField(TEXT("mode")));
		const int32 Player = Grid[Human];
		TestTrue(Name + TEXT(": aggression JSON == RAM table"), S->GetBoolField(TEXT("agg_equal")));
		TestTrue(Name + TEXT(": player at grid index 1..4"), Human >= 1 && Human <= 4);
		TArray<int32> Choices[3];
		for (int32 T = 0; T < 3; ++T)
		{
			int32 Best = 0;
			for (int32 I = 0; I < D.Tables[T].Num(); ++I)
			{
				const int32 Sc = D.Tables[T][I].Score(Player, Venue, Mode, Heat);
				if (Sc >= 0 && Sc >= Best) { if (Sc > Best) { Choices[T].Reset(); Best = Sc; } Choices[T].Add(I); }
			}
			if (Choices[T].Num() == 0) Choices[T].Add(-1);
		}
		bool bFits = false;
		TSet<int32> StoryRiders;
		for (const int32 A : Choices[0]) for (const int32 B : Choices[1]) for (const int32 C : Choices[2])
		{
			int32 R[5] = { 12, 12, 12, 12, 12 };
			const int32 Pick[3] = { A, B, C };
			for (int32 T = 0; T < 3; ++T)
			{
				if (Pick[T] < 0) continue;
				for (int32 K = 0; K < 5; ++K) if (D.Tables[T][Pick[T]].Riders[K] != 12) R[K] = D.Tables[T][Pick[T]].Riders[K];
			}
			bool bOk = (R[0] == 12 || Grid[Human - 1] == R[0]) && (R[1] == 12 || Grid[Human + 1] == R[1]);
			for (int32 K = 2; K < 5; ++K) bOk &= R[K] == 12 || Grid.Contains(R[K]);
			if (bOk && !bFits) { bFits = true; for (int32 K = 0; K < 5; ++K) if (R[K] != 12) StoryRiders.Add(R[K]); }
		}
		TestTrue(Name + TEXT(": the grid fits a story outcome"), bFits);
		const TArray<TSharedPtr<FJsonValue>>& St = S->GetArrayField(TEXT("status"));
		const TArray<int32> Flag = Ints(S->GetArrayField(TEXT("flag")));
		for (int32 I = 0; I < 6; ++I)
		{
			const int32 R = Grid[I];
			if (I == Human || StoryRiders.Contains(R)) continue;
			TestTrue(Name + TEXT(": drawn rider eligible (flag 0, status free)"), Flag[R] == 0 && Ints(St[R]->AsArray())[Heat] == SSXHeats::Free);
		}
		const TArray<int32> Car = Ints(S->GetArrayField(TEXT("carried"))), Cr = Ints(S->GetArrayField(TEXT("career")));
		int32 Carried[12], Career[12];
		for (int32 K = 0; K < 12; ++K) { Carried[K] = Car[K]; Career[K] = Cr[K]; }
		TArray<TArray<int32>> L;
		SSXHeats::InitialLevels(D, Grid, Human, Heat, Carried, Career, L);
		const TArray<TSharedPtr<FJsonValue>>& W = S->GetArrayField(TEXT("levels"));
		int32 Hits = 0;
		for (int32 I = 0; I < 6; ++I) { const TArray<int32> Row = Ints(W[I]->AsArray()); for (int32 J = 0; J < 6; ++J) Hits += L[I][J] == Row[J] ? 1 : 0; }
		TestEqual(Name + TEXT(": relationship bytes"), Hits, 36);
		TotalBytes += Hits;
		const TArray<int32> Types = Ints(S->GetArrayField(TEXT("slot_types")));
		for (int32 T = 0; T < 3; ++T)
		{
			bool bIn = false;
			for (const int32 I : Choices[T]) bIn |= I >= 0 && D.Tables[T][I].IntroType == Types[T];
			TestTrue(Name + FString::Printf(TEXT(": intro slot %d type %d from story table %d"), T, Types[T], T), bIn);
		}
		if (Heat == 0)
		{
			TestEqual(Name + TEXT(": player row taken in every heat"), Ints(St[Player]->AsArray()), TArray<int32>{ 0, 0, 0 });
			bool bNone = true;
			for (const int32 C : Car) bNone &= C == -1;
			TestTrue(Name + TEXT(": nothing carried before heat 1"), bNone);
		}
	}
	const TArray<int32> G1 = Ints(Snap[0]->GetArrayField(TEXT("grid"))), G2 = Ints(Snap[1]->GetArrayField(TEXT("grid")));
	const int32 H1 = int32(Snap[0]->GetNumberField(TEXT("human"))), H2 = int32(Snap[1]->GetNumberField(TEXT("human")));
	const TArray<int32> Car2 = Ints(Snap[1]->GetArrayField(TEXT("carried"))), Flag2 = Ints(Snap[1]->GetArrayField(TEXT("flag")));
	TSet<int32> Opp1, CarriedSet, Elim;
	for (int32 I = 0; I < 6; ++I) if (I != H1) Opp1.Add(G1[I]);
	for (int32 R = 0; R < 12; ++R) { if (Car2[R] >= 0) CarriedSet.Add(R); if (Flag2[R]) Elim.Add(R); }
	TestTrue(TEXT("C+0x1C written for exactly the heat-0 opponents"), CarriedSet.Num() == Opp1.Num() && CarriedSet.Includes(Opp1));
	TestTrue(TEXT("three heat-0 riders eliminated"), Elim.Num() == 3 && Opp1.Includes(Elim));
	bool bNoElimInGrid = true;
	for (const int32 R : G2) bNoElimInGrid &= !Elim.Contains(R);
	TestTrue(TEXT("no eliminated rider in the heat-1 grid"), bNoElimInGrid);
	const TArray<TSharedPtr<FJsonValue>>& M2 = Snap[1]->GetArrayField(TEXT("meters"));
	TestEqual(TEXT("heat factor (won heat 0) = player's heat-1 start meter"), float(M2[H2]->AsNumber()), SSXHeats::HeatFactor(0));
	TestTrue(TEXT("live trace meter at frame 301"), FMath::Abs(Ref->GetNumberField(TEXT("live_meter_301")) - SSXHeats::HeatFactor(0)) < 1e-5);
	AddInfo(FString::Printf(TEXT("heats RAM: relationship bytes %d/72 (36 per snapshot), carried riders %d = heat-0 opponents, %d eliminated"),
		TotalBytes, CarriedSet.Num(), Elim.Num()));
	return true;
}

// 0x165CF0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHeatsSingleEventGridTest, "SSX.Heats.SingleEventGrid", HeatsTestFlags)

bool FSSXHeatsSingleEventGridTest::RunTest(const FString&)
{
	using namespace SSXHeats;
	FData Empty;
	for (int32 Seed = 0; Seed < 240; ++Seed)
	{
		FRandomStream Rs(Seed);
		const int32 Human = Seed % NumRiders;
		const int32 N = 1 + (Seed / NumRiders) % GridSize;
		int32 G[GridSize], H[GridSize];
		SingleEventGrid(Empty, { Human }, N, (Seed % 2) ? AlaskaVenue : 0, 2, [&Rs]() { return uint32(Rs.GetUnsignedInt()); }, G, H);
		uint32 Seen = 0;
		int32 Humans = 0;
		for (int32 I = 0; I < N; ++I)
		{
			TestTrue(TEXT("rider id"), G[I] >= 0 && G[I] < NumRiders);
			TestFalse(FString::Printf(TEXT("seed %d: rider %d twice"), Seed, G[I]), ((Seen >> G[I]) & 1) != 0);
			Seen |= 1u << G[I];
			if (H[I] == 0) { ++Humans; TestEqual(TEXT("the human's entry rides his rider"), G[I], Human); }
			else { TestNotEqual(FString::Printf(TEXT("seed %d: computer rider = the human's rider"), Seed), G[I], Human); }
		}
		TestEqual(TEXT("one human entry"), Humans, 1);
	}
	FData D;
	FStoryRecord Rec;
	Rec.Rider = 12; Rec.Venue = 13; Rec.Mode = 10; Rec.Heat = 3;
	Rec.Riders[0] = 7; Rec.Riders[1] = 2;
	D.Tables[0].Add(Rec);
	auto Run = [&](int32 Venue, std::initializer_list<uint32> Shuffle, int32 (&G)[GridSize], int32 (&H)[GridSize])
	{
		TArray<uint32> Seq;
		Seq.Add(0);
		for (int32 R : { 0, 1, 3, 4, 6, 8, 9, 10, 11 }) { Seq.Add(R == 11 ? 9999u : uint32(100 + R)); }
		for (uint32 V : Shuffle) { Seq.Add(V); }
		int32 At = 0;
		SingleEventGrid(D, { 5 }, 4, Venue, 2, [&]() { return Seq.IsValidIndex(At) ? Seq[At++] : 0u; }, G, H);
		TestEqual(TEXT("all draws used"), At, Seq.Num());
	};
	auto Expect = [&](const TCHAR* What, const int32 (&G)[GridSize], const int32 (&H)[GridSize], std::initializer_list<int32> Want, int32 HumanAt)
	{
		int32 I = 0;
		for (int32 W : Want) { TestEqual(FString::Printf(TEXT("%s entry %d"), What, I), G[I], W); ++I; }
		for (int32 K = 0; K < 4; ++K) { TestEqual(FString::Printf(TEXT("%s human map %d"), What, K), H[K], K == HumanAt ? 0 : -1); }
	};
	int32 G[GridSize], H[GridSize];
	Run(0, { 3, 3, 3, 3 }, G, H);
	Expect(TEXT("middle"), G, H, { 7, 5, 2, 11 }, 1);
	Run(0, { 0, 1, 2, 3 }, G, H);
	Expect(TEXT("first"), G, H, { 7, 5, 2, 11 }, 1);
	Run(0, { 3, 1, 2, 3 }, G, H);
	Expect(TEXT("last"), G, H, { 11, 7, 5, 2 }, 2);
	FData NoStory;
	auto RunPlain = [&](int32 Venue, int32 (&Gp)[GridSize], int32 (&Hp)[GridSize])
	{
		TArray<uint32> Seq;
		for (int32 R = 0; R < NumRiders; ++R) { if (R != 5) { Seq.Add(R == 7 ? 9500u : R == 2 ? 9000u : uint32(100 + R)); } }
		for (uint32 V : { 2u, 0u, 2u }) { Seq.Add(V); }
		int32 At = 0;
		SingleEventGrid(NoStory, { 5 }, 3, Venue, 2, [&]() { return Seq.IsValidIndex(At) ? Seq[At++] : 0u; }, Gp, Hp);
	};
	RunPlain(0, G, H);
	TestTrue(TEXT("plain shuffle [7, 2, 5]"), G[0] == 7 && G[1] == 2 && G[2] == 5 && H[2] == 0);
	RunPlain(AlaskaVenue, G, H);
	TestTrue(TEXT("Alaska: human from 2 to 1 [7, 5, 2]"), G[0] == 7 && G[1] == 5 && G[2] == 2 && H[1] == 0);
	return true;
}

#endif
