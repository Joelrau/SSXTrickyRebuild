#include "Camera/CameraDefs.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/IntroDialogue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DialogueTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	namespace ID = SSXIntroDialogue;

	bool LoadData(FAutomationTestBase& T, const ID::FData*& OutD, const SSXHeats::FData*& OutH)
	{
		OutD = &ID::FData::Get();
		OutH = &SSXHeats::FData::Get();
		if (!OutD->bLoaded || !OutH->bLoaded)
		{
			T.AddError(TEXT("SimData/intro_dialogue.json or heats_data.json missing (tools/export_intro_dialogue.py, tools/export_heats_data.py)"));
			return false;
		}
		return true;
	}

	TArray<int32> Ints(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<int32> Out;
		for (const TSharedPtr<FJsonValue>& V : A) Out.Add(int32(V->AsNumber()));
		return Out;
	}

	struct FSeq
	{
		TArray<uint32> V;
		int32 I = 0;
		uint32 operator()() { return V.IsValidIndex(I) ? V[I++] : 0u; }
	};

	struct FRamSnapshot
	{
		FString Name;
		int32 Venue = 0, Mode = 0, HeatWord = 0, Focus = -1;
		TArray<int32> Riders, Type80, Port;
		ID::FContext Ctx;
		ID::FSlot Slots[ID::NumSlots];
		int32 ObjSlot = 0;
		TArray<int32> ObjList;
	};

	bool LoadRam(FAutomationTestBase& T, TArray<FRamSnapshot>& Out)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *ID::FData::DefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
		{
			T.AddError(TEXT("SimData/intro_dialogue.json missing"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* RamArr = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("ram"), RamArr))
		{
			return true;   // exported without recordings
		}
		for (const TSharedPtr<FJsonValue>& V : *RamArr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FRamSnapshot S;
			S.Name = O->GetStringField(TEXT("name"));
			S.Venue = int32(O->GetNumberField(TEXT("venue")));
			S.Mode = int32(O->GetNumberField(TEXT("mode")));
			S.HeatWord = int32(O->GetNumberField(TEXT("heat_word")));
			S.Focus = int32(O->GetNumberField(TEXT("focus")));
			S.Riders = Ints(O->GetArrayField(TEXT("riders")));
			S.Type80 = Ints(O->GetArrayField(TEXT("type80")));
			S.Port = Ints(O->GetArrayField(TEXT("port")));
			const TArray<int32> CR = Ints(O->GetArrayField(TEXT("ctx_riders")));
			for (int32 K = 0; K < 5; ++K) S.Ctx.Riders[K] = CR[K];
			S.Ctx.Player = int32(O->GetNumberField(TEXT("ctx_player")));
			const TArray<int32> Ids = Ints(O->GetArrayField(TEXT("ctx_ids")));
			const TArray<int32> Ty = Ints(O->GetArrayField(TEXT("ctx_types")));
			const TArray<int32> Va = Ints(O->GetArrayField(TEXT("ctx_variants")));
			for (int32 K = 0; K < ID::NumSlots; ++K) { S.Ctx.Ids[K] = Ids[K]; S.Ctx.Types[K] = Ty[K]; S.Ctx.Variants[K] = Va[K]; }
			const TArray<TSharedPtr<FJsonValue>>& Sl = O->GetArrayField(TEXT("slots"));
			for (int32 K = 0; K < ID::NumSlots && K < Sl.Num(); ++K)
			{
				const TSharedPtr<FJsonObject> SO = Sl[K]->AsObject();
				S.Slots[K].Type = int32(SO->GetNumberField(TEXT("type")));
				const TArray<TSharedPtr<FJsonValue>>& Ls = SO->GetArrayField(TEXT("lines"));
				for (int32 J = 0; J < ID::NumLines; ++J)
				{
					const TArray<int32> W = Ints(Ls[J]->AsArray());
					S.Slots[K].Lines[J] = { W[0], W[1], W[2], W[3] };
				}
			}
			S.ObjSlot = int32(O->GetNumberField(TEXT("obj_slot")));
			S.ObjList = Ints(O->GetArrayField(TEXT("obj_list")));
			Out.Add(MoveTemp(S));
		}
		return true;
	}

	bool SkipWithoutRam(FAutomationTestBase& T, const TArray<FRamSnapshot>& Ram)
	{
		if (Ram.Num() > 0)
		{
			return false;
		}
		SSXTest::Skip(T, TEXT("skipped: intro_dialogue.json has no RAM reference (export with SSX_TRACES set)"));
		return true;
	}

	FSeq VariantDraws(const ID::FContext& C)
	{
		FSeq Q;
		for (int32 K = 0; K < ID::NumSlots; ++K)
		{
			if (C.Ids[K] != ID::DefaultId) Q.V.Add(uint32(C.Variants[K]));
		}
		return Q;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXIntroDialogueRamTest, "SSX.Race.IntroDialogue.Ram", DialogueTestFlags)
bool FSSXIntroDialogueRamTest::RunTest(const FString&)
{
	const ID::FData* D = nullptr;
	const SSXHeats::FData* H = nullptr;
	TArray<FRamSnapshot> Ram;
	if (!LoadData(*this, D, H) || !LoadRam(*this, Ram))
	{
		return false;
	}
	if (SkipWithoutRam(*this, Ram))
	{
		return true;
	}
	// FUN_00168360
	int32 Missing = 0;
	for (const TArray<SSXHeats::FStoryRecord>* T : { &H->Tables[0], &H->Tables[1], &H->Tables[2], &D->Table3, &D->Table4, &D->CountTable, &D->PairTable })
	{
		for (const SSXHeats::FStoryRecord& R : *T)
		{
			const ID::FIntro* In = D->Intros.Find(R.IntroId);
			Missing += (R.IntroId != ID::DefaultId && (!In || In->Count <= 0)) ? 1 : 0;
		}
	}
	TestEqual(TEXT("story-table intro ids with variants"), Missing, 0);

	int32 LinesOk = 0, LinesAll = 0, PicksOk = 0, PicksAll = 0;
	for (const FRamSnapshot& S : Ram)
	{
		ID::FContext Ctx;
		for (int32 K = 0; K < 5; ++K) Ctx.Riders[K] = S.Ctx.Riders[K];
		Ctx.Player = S.Ctx.Player;
		for (int32 K = 0; K < 3; ++K) { Ctx.Ids[K] = S.Ctx.Ids[K]; Ctx.Types[K] = S.Ctx.Types[K]; }
		TArray<ID::FRacer> Racers;
		for (int32 I = 0; I < S.Riders.Num(); ++I) Racers.Add({ S.Riders[I], S.Type80[I], S.Port[I] >= 0 });
		ID::FSlot Slots[ID::NumSlots];
		TArray<TArray<int32>> Cands;
		FSeq Zero;
		const int32 Focus = ID::BuildSlots(Ctx, Racers, S.Venue, S.Mode, S.HeatWord, *H, *D, [&Zero]() { return Zero(); }, Slots, &Cands);
		TestEqual(FString::Printf(TEXT("%s: focus RS+0x918"), *S.Name), Focus, S.Focus);
		for (int32 K = 0; K < 3; ++K)
		{
			TestEqual(FString::Printf(TEXT("%s: story slot %d kept (all story riders race)"), *S.Name, K), Cands[K].Num(), 0);
		}
		for (int32 K = 3; K < ID::NumSlots; ++K)
		{
			++PicksAll;
			const bool bOk = Cands[K].Contains(S.Ctx.Ids[K]) || (S.Ctx.Ids[K] == ID::DefaultId && Cands[K].Num() == 0);
			PicksOk += bOk ? 1 : 0;
			if (!bOk) AddError(FString::Printf(TEXT("%s slot %d: RAM id %d not among %d candidates"), *S.Name, K, S.Ctx.Ids[K], Cands[K].Num()));
		}
		ID::FContext Full = S.Ctx;
		FSeq Draws = VariantDraws(S.Ctx);
		ID::FSlot Built[ID::NumSlots];
		ID::FillLines(Full, *D, Built, [&Draws]() { return Draws(); });
		for (int32 K = 0; K < ID::NumSlots; ++K)
		{
			TestEqual(FString::Printf(TEXT("%s slot %d type"), *S.Name, K), Built[K].Type, S.Slots[K].Type);
			for (int32 J = 0; J < ID::NumLines; ++J)
			{
				++LinesAll;
				const bool bOk = Built[K].Lines[J] == S.Slots[K].Lines[J];
				LinesOk += bOk ? 1 : 0;
				if (!bOk)
				{
					const ID::FLine& A = Built[K].Lines[J];
					const ID::FLine& B = S.Slots[K].Lines[J];
					AddError(FString::Printf(TEXT("%s slot %d line %d: {%d %d %d %d} vs RAM {%d %d %d %d}"), *S.Name, K, J, A.Speaker, A.LineId, A.Aux, A.Value,
						B.Speaker, B.LineId, B.Aux, B.Value));
				}
			}
		}
		ID::FObject Obj;
		FSeq Any;
		ID::CopyToObject(Built, Obj, [&Any]() { return Any(); });
		const TArray<int32> Sorted = ID::SortedSpeech(Obj, S.ObjSlot, *D);
		TestEqual(FString::Printf(TEXT("%s: intro object slot %d speech list head (RAM %d)"), *S.Name, S.ObjSlot, S.ObjList[0]),
			Sorted.Num() > 0 ? Sorted[0] : -1, S.ObjList[0]);
	}
	TestEqual(TEXT("RAM lines rebuilt"), LinesOk, LinesAll);
	TestEqual(TEXT("RAM intro ids among the lookup candidates"), PicksOk, PicksAll);
	{
		const FRamSnapshot& S = Ram[0];
		ID::FObject Obj;
		FSeq Any;
		ID::CopyToObject(S.Slots, Obj, [&Any]() { return Any(); });
		TestEqual(TEXT("session 1 slot 2 speech order"), ID::SortedSpeech(Obj, 2, *D), TArray<int32>{ 1025, 1026 });
		TestEqual(TEXT("line 220 key = event frame 46"), D->Clip(220) ? D->Clip(220)->Key() : -1, 46);
		TestEqual(TEXT("line 221 key = event frame 200"), D->Clip(221) ? D->Clip(221)->Key() : -1, 200);
	}
	AddInfo(FString::Printf(TEXT("intro dialogue: %d intros, %d line records, %d line clips; RAM: %d/%d lines rebuilt, %d/%d lookups hold the RAM id"),
		D->Intros.Num(), D->Records.Num(), D->Clips.Num(), LinesOk, LinesAll, PicksOk, PicksAll));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXIntroDialogueSelectionTest, "SSX.Race.IntroDialogue.Selection", DialogueTestFlags)
bool FSSXIntroDialogueSelectionTest::RunTest(const FString&)
{
	const ID::FData* D = nullptr;
	const SSXHeats::FData* H = nullptr;
	if (!LoadData(*this, D, H))
	{
		return false;
	}
	const TArray<ID::FRacer> Grid1 = { { 10 }, { 0 }, { 8 }, { 11 }, { 4, 2, true }, { 6 } };
	auto Build = [&](ID::FContext& C, TConstArrayView<ID::FRacer> Racers, int32 Mode, int32 Heat, TArray<TArray<int32>>& Cands)
	{
		ID::FSlot Slots[ID::NumSlots];
		FSeq Zero;
		return ID::BuildSlots(C, Racers, 0, Mode, Heat, *H, *D, [&Zero]() { return Zero(); }, Slots, &Cands);
	};
	{
		ID::FContext C;
		C.Riders[1] = 6;
		C.Player = 4;
		C.Ids[2] = 236; C.Types[2] = 21;
		TArray<TArray<int32>> Cands;
		const int32 Focus = Build(C, Grid1, 4, 0, Cands);
		TestEqual(TEXT("present: focus = the player's racer"), Focus, 4);
		TestEqual(TEXT("present: slot 2 kept"), C.Ids[2], 236);
		TestTrue(TEXT("present: no new lookup of slots 0..2"), Cands[0].Num() == 0 && Cands[1].Num() == 0 && Cands[2].Num() == 0);
		TestTrue(TEXT("one human: slots 3 / 4 looked up"), C.Ids[3] == ID::DefaultId || Cands[3].Contains(C.Ids[3]));
		int32 Pairs = 0;
		for (int32 K = 10; K < 15; ++K) Pairs += Cands[K].Num() > 0 ? 1 : 0;
		TestEqual(TEXT("six racers: an end-race pair lookup per other racer"), Pairs, 5);
	}
	{
		const TArray<ID::FRacer> Grid = { { 10 }, { 0 }, { 8 }, { 11 }, { 4, 2, true }, { 2 } };
		ID::FContext C;
		C.Riders[1] = 6;
		C.Player = 4;
		TArray<TArray<int32>> Cands;
		Build(C, Grid, 4, 0, Cands);
		TestTrue(TEXT("missing story rider: slot 1 looked up again"), Cands[1].Num() > 0);
		bool bAllRace = true;
		for (int32 T = 0; T < 3; ++T)
		{
			for (const SSXHeats::FStoryRecord& R : H->Tables[T])
			{
				if (!Cands[T].Contains(R.IntroId)) continue;
				for (int32 K = 0; K < 5; ++K)
				{
					const int32 W = int8(R.Riders[K]);
					if (W != 12 && !TArray<int32>({ 10, 0, 8, 11, 2 }).Contains(W)) bAllRace = false;
				}
			}
		}
		TestTrue(TEXT("candidates name only riders of the race"), bAllRace);
	}
	{
		const TArray<ID::FRacer> Grid = { { 4, 2, true }, { 4, 2, true }, { 8 } };
		ID::FContext C;
		C.Player = 4;
		TArray<TArray<int32>> Cands;
		const int32 Focus = Build(C, Grid, 2, ID::SingleRaceHeatWord, Cands);
		TestEqual(TEXT("twice: focus = first player racer"), Focus, 0);
		int32 Other = 0;
		for (int32 K = 3; K < 15; ++K) Other += Cands[K].Num() + (C.Ids[K] != ID::DefaultId ? 1 : 0);
		TestEqual(TEXT("twice: slots 3..14 stay Null"), Other, 0);
	}
	{
		const TArray<ID::FRacer> Grid = { { 10 }, { 4, 0, true }, { 8 } };
		ID::FContext C;
		C.Player = 4;
		TArray<TArray<int32>> Cands;
		TestEqual(TEXT("MMM: focus = that racer"), Build(C, Grid, 2, ID::SingleRaceHeatWord, Cands), 1);
		TestEqual(TEXT("MMM: slot 10 Null"), C.Ids[10], ID::DefaultId);
	}
	{
		const TArray<ID::FRacer> Grid = { { 10, 2, true }, { 4, 2, true }, { 8 } };
		ID::FContext C;
		C.Player = 4;
		TArray<TArray<int32>> Cands;
		Build(C, Grid, 2, ID::SingleRaceHeatWord, Cands);
		TestTrue(TEXT("two humans: slots 3 / 4 not looked up"), Cands[3].Num() == 0 && Cands[4].Num() == 0 && C.Ids[3] == ID::DefaultId);
	}
	// FUN_001688F0
	{
		SSXHeats::FStoryRecord R;
		R.Rider = 4; R.Venue = 0; R.Mode = 4; R.Heat = 3;
		R.Riders[0] = 6;
		TestEqual(TEXT("record rider in the race: +1"), ID::ScoreWithRiders(R, 4, 0, 4, 0, { 6, 10 }), 4);
		TestEqual(TEXT("record rider missing: rejected"), ID::ScoreWithRiders(R, 4, 0, 4, 0, { 10 }), -1);
		TestEqual(TEXT("no others: rider-named record rejected"), ID::ScoreWithRiders(R, 4, 0, 4, 0, {}), -1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXIntroDialogueSceneTest, "SSX.Race.IntroDialogue.Scene", DialogueTestFlags)
bool FSSXIntroDialogueSceneTest::RunTest(const FString&)
{
	const ID::FData* D = nullptr;
	const SSXHeats::FData* H = nullptr;
	TArray<FRamSnapshot> Ram;
	if (!LoadData(*this, D, H) || !LoadRam(*this, Ram))
	{
		return false;
	}
	if (SkipWithoutRam(*this, Ram))
	{
		return true;
	}
	FSSXCameraDefSet Defs;
	if (!Defs.LoadForTrack(TEXT("gari")) || !Defs.FindScene(TEXT("FOE_1")))
	{
		AddError(TEXT("SimData/cameras (tools/export_cameras.py) without the scenes"));
		return false;
	}
	const FRamSnapshot& S = Ram[0];
	ID::FObject Obj;
	FSeq Any;
	ID::CopyToObject(S.Slots, Obj, [&Any]() { return Any(); });
	ID::SortedSpeech(Obj, 2, *D);
	TArray<ID::FSceneRacer> Racers;
	for (int32 I = 0; I < S.Riders.Num(); ++I) Racers.Add({ S.Port[I] >= 0 ? 0 : -1, S.Riders[I] });
	const TArray<FSSXSceneActor>& Actors = *Defs.FindScene(TEXT("FOE_1"));
	TestEqual(TEXT("FOE_1 roles: USER = player 1, FOE_1 = the racer after, FF_1..4 = the others in order"), ID::AssignActors(Actors, Racers),
		TArray<int32>{ 4, 5, 0, 1, 2, 3, -1, -1 });
	ID::FScenePlayer P;
	TArray<FIntVector> Events;
	TArray<FIntPoint> Starts;
	int32 Step = 0;
	P.OnEvent = [&](int32 Racer, int32 Value) { Events.Add(FIntVector(Step, Racer, Value)); };
	P.OnLineStart = [&](int32 Racer, int32 Line) { Starts.Add(FIntPoint(Racer, Line)); };
	P.Start(Actors, Defs, Racers, Obj, *D);
	TestEqual(TEXT("six line clips start (USER 220, FOE 221, FF 184..187)"), Starts,
		TArray<FIntPoint>{ { 4, 220 }, { 5, 221 }, { 0, 184 }, { 1, 185 }, { 2, 186 }, { 3, 187 } });
	for (Step = 1; Step <= 700; ++Step)
	{
		P.Step(Obj, *D);
	}
	TestEqual(TEXT("speech events: Moby (1025) at step 94, JP (1026) at 402, Moby's replay at 694"), Events,
		TArray<FIntVector>{ FIntVector(94, 4, 1025), FIntVector(402, 5, 1026), FIntVector(694, 4, 1025) });
	TestTrue(TEXT("actors' clip events come from the scene player"), P.DeliversEventsOf(4) && P.DeliversEventsOf(5) && P.DeliversEventsOf(0));
	TestFalse(TEXT("no actor: the controller delivers"), P.DeliversEventsOf(-1) || P.DeliversEventsOf(7));
	P.Stop();
	TestFalse(TEXT("STOP_ANIMATIONS"), P.IsRunning());
	TestFalse(TEXT("after the stop the controllers deliver again"), P.DeliversEventsOf(4));
	return true;
}

#endif
