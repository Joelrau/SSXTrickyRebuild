#include "Race/IntroDialogue.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SSXIntroDialogue
{
namespace
{
	constexpr int32 SSXRace_MaxOthers = 6;

	bool ReadTable(const TSharedPtr<FJsonObject>& Tabs, const TCHAR* Key, TArray<SSXHeats::FStoryRecord>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Tabs->TryGetArrayField(Key, Rows))
		{
			return false;
		}
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& R : *Rows)
		{
			const TArray<TSharedPtr<FJsonValue>>& B = R->AsArray();
			if (B.Num() != 12) continue;
			uint8 Bytes[12];
			for (int32 K = 0; K < 12; ++K) Bytes[K] = uint8(B[K]->AsNumber());
			Out.Add(SSXHeats::FStoryRecord::FromBytes(Bytes));
		}
		return true;
	}

	TArray<int32> Ints(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<int32> Out;
		for (const TSharedPtr<FJsonValue>& V : A) Out.Add(int32(V->AsNumber()));
		return Out;
	}
}

FString FData::DefaultPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("intro_dialogue.json"));
}

const FData& FData::Get()
{
	static FData D;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		D.LoadJson(DefaultPath());
	}
	return D;
}

bool FData::LoadJson(const FString& Path, FString* OutError)
{
	bLoaded = false;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		if (OutError) *OutError = Path + TEXT(" missing (tools/export_intro_dialogue.py)");
		return false;
	}
	const TSharedPtr<FJsonObject>* Tabs = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* IdsJ = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* RowsJ = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* SizesJ = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* LinesJ = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* RecsJ = nullptr;
	const TSharedPtr<FJsonObject>* ClipsJ = nullptr;
	if (!Root->TryGetObjectField(TEXT("story_tables"), Tabs) || !Root->TryGetArrayField(TEXT("intro_ids"), IdsJ)
		|| !Root->TryGetArrayField(TEXT("speaker_rows"), RowsJ) || !Root->TryGetArrayField(TEXT("group_sizes"), SizesJ)
		|| !Root->TryGetArrayField(TEXT("variant_lines"), LinesJ) || !Root->TryGetArrayField(TEXT("line_records"), RecsJ)
		|| !Root->TryGetObjectField(TEXT("line_clips"), ClipsJ))
	{
		if (OutError) *OutError = Path + TEXT(": bad layout");
		return false;
	}
	if (!ReadTable(*Tabs, TEXT("t3"), Table3) || !ReadTable(*Tabs, TEXT("t4"), Table4) || !ReadTable(*Tabs, TEXT("count"), CountTable)
		|| !ReadTable(*Tabs, TEXT("pair"), PairTable))
	{
		if (OutError) *OutError = Path + TEXT(": story tables missing");
		return false;
	}
	const TArray<int32> Ids = Ints(*IdsJ);
	TArray<TArray<uint8>> Rows;
	for (const TSharedPtr<FJsonValue>& R : *RowsJ)
	{
		TArray<uint8>& Row = Rows.AddDefaulted_GetRef();
		for (const TSharedPtr<FJsonValue>& V : R->AsArray()) Row.Add(uint8(V->AsNumber()));
	}
	const TArray<int32> Sizes = Ints(*SizesJ);
	BuildIntroTable(Ids, Rows, Sizes, Intros);
	VariantLines.Reset();
	for (const TSharedPtr<FJsonValue>& R : *LinesJ) VariantLines.Add(Ints(R->AsArray()));
	Records.Reset();
	for (const TSharedPtr<FJsonValue>& R : *RecsJ)
	{
		const TArray<int32> W = Ints(R->AsArray());
		if (W.Num() == 3) Records.Add({ W[0], W[1], W[2] });
	}
	Clips.Reset();
	for (const auto& It : (*ClipsJ)->Values)
	{
		const TSharedPtr<FJsonObject> O = It.Value->AsObject();
		FLineClip C;
		O->TryGetStringField(TEXT("name"), C.Name);
		C.Frames = int32(O->GetNumberField(TEXT("frames")));
		const TArray<TSharedPtr<FJsonValue>>* Ev = nullptr;
		if (O->TryGetArrayField(TEXT("events"), Ev))
		{
			for (const TSharedPtr<FJsonValue>& E : *Ev)
			{
				const TArray<int32> P = Ints(E->AsArray());
				if (P.Num() == 2) C.Events.Add(FIntPoint(P[0], P[1]));
			}
		}
		Clips.Add(FCString::Atoi(*It.Key), MoveTemp(C));
	}
	bLoaded = Intros.Num() > 0 && VariantLines.Num() > 0;
	return bLoaded;
}

const FData::FRecord* FData::FindRecord(int32 LineId) const
{
	for (const FRecord& R : Records)
	{
		if (R.Id == LineId) return &R;
	}
	return nullptr;
}

void BuildIntroTable(TConstArrayView<int32> Ids, TConstArrayView<TArray<uint8>> SpeakerRows, TConstArrayView<int32> GroupSizes,
	TMap<int32, FIntro>& Out)
{
	// FUN_00168360
	Out.Reset();
	int32 Prev = DefaultId;
	int32 InGroup = 0;
	int32 Group = 0;
	for (int32 I = 0; I < Ids.Num(); ++I)
	{
		++InGroup;
		if (GroupSizes.IsValidIndex(Group) && GroupSizes[Group] < InGroup)
		{
			InGroup = 1;
			++Group;
		}
		const int32 V = Ids[I];
		if (V == Prev)
		{
			++Out.FindOrAdd(Prev).Count;
		}
		else
		{
			FIntro& E = Out.FindOrAdd(V);
			if (SpeakerRows.IsValidIndex(Group))
			{
				for (int32 J = 0; J < NumLines && J < SpeakerRows[Group].Num(); ++J) E.Speakers[J] = SpeakerRows[Group][J];
			}
			E.Start = I;
			E.Count = 1;
		}
		Prev = V;
	}
}

FContext FContext::FromStory(const SSXHeats::FStoryResult& S, int32 PlayerRider)
{
	FContext C;
	for (int32 K = 0; K < 5; ++K) C.Riders[K] = S.Riders[K];
	for (int32 K = 0; K < NumSlots; ++K)
	{
		C.Ids[K] = S.IntroIds[K];
		C.Types[K] = S.IntroTypes[K];
	}
	C.Player = PlayerRider;
	return C;
}

int32 ScoreWithRiders(const SSXHeats::FStoryRecord& R, int32 Rider, int32 Venue, int32 Mode, int32 Heat, TConstArrayView<int32> Others)
{
	int32 S = R.Score(Rider, Venue, Mode, Heat);   // FUN_001687F0
	if (S < 0)
	{
		return S;
	}
	if (Others.Num() == 0)
	{
		for (int32 K = 0; K < 5; ++K)
		{
			if (int8(R.Riders[K]) != SSXHeats::None) return -1;
		}
		return S;
	}
	for (int32 K = 0; K < 5; ++K)
	{
		const int32 Want = int8(R.Riders[K]);
		if (Want == SSXHeats::None) continue;
		if (!Others.Contains(Want)) return -1;
		++S;
	}
	return S;
}

TArray<int32> TableLookup(FContext& C, int32 Slot, TConstArrayView<SSXHeats::FStoryRecord> Table, int32 Rider, int32 Venue, int32 Mode,
	int32 Heat, TConstArrayView<int32> Others, FRand Rand)
{
	C.Ids[Slot] = DefaultId;
	C.Types[Slot] = DefaultType;
	TArray<int32> Picks;
	int32 Best = 0;
	for (int32 I = 0; I < Table.Num(); ++I)
	{
		const int32 S = ScoreWithRiders(Table[I], Rider, Venue, Mode, Heat, Others);
		if (S >= 0 && S >= Best)
		{
			if (S > Best) { Picks.Reset(); Best = S; }
			Picks.Add(I);
		}
	}
	if (Picks.Num() > 0)
	{
		const SSXHeats::FStoryRecord& R = Table[Picks[int32(Rand() % uint32(Picks.Num()))]];
		C.Ids[Slot] = R.IntroId;
		C.Types[Slot] = R.IntroType;
	}
	return Picks;
}

int32 BuildSlots(FContext& Ctx, TConstArrayView<FRacer> Racers, int32 Venue, int32 Mode, int32 HeatWord, const SSXHeats::FData& Heats,
	const FData& D, FRand Rand, FSlot Slots[NumSlots], TArray<TArray<int32>>* OutCandidates)
{
	const int32 N = Racers.Num();
	if (OutCandidates) { OutCandidates->Reset(); OutCandidates->SetNum(NumSlots); }
	auto Lookup = [&](int32 Slot, TConstArrayView<SSXHeats::FStoryRecord> Table, int32 Rider, TConstArrayView<int32> Others)
	{
		const TArray<int32> Picks = TableLookup(Ctx, Slot, Table, Rider, Venue, Mode, HeatWord & 0xFFFF, Others, Rand);
		if (OutCandidates) { for (const int32 P : Picks) (*OutCandidates)[Slot].Add(Table[P].IntroId); }
	};
	bool bAllPresent = true;
	for (int32 K = 0; K < 5 && bAllPresent; ++K)
	{
		if (Ctx.Riders[K] == SSXHeats::None) continue;
		bAllPresent = false;
		for (int32 I = 0; I < N && !bAllPresent; ++I) bAllPresent = Racers[I].Rider == Ctx.Riders[K];
	}
	int32 Others[SSXRace_MaxOthers] = { 12, 12, 12, 12, 12, 12 };
	int32 NumOthers = 0;
	int32 Focus = -1, FirstNot2 = -1, PlayerCount = 0, Humans = 0;
	for (int32 I = 0; I < N; ++I)
	{
		if (Racers[I].Type80 != NormalType80 && FirstNot2 < 0) FirstNot2 = I;
		if (Racers[I].Rider != Ctx.Player || Focus >= 0)
		{
			if (NumOthers < SSXRace_MaxOthers) Others[NumOthers] = Racers[I].Rider;
			++NumOthers;
		}
		else
		{
			Focus = I;
		}
		if (Racers[I].Rider == Ctx.Player) ++PlayerCount;
		if (Racers[I].bHuman) ++Humans;
	}
	const TConstArrayView<int32> List(Others, FMath::Clamp(N - 1, 0, SSXRace_MaxOthers));
	auto One = [&Others](int32 K) { return TConstArrayView<int32>(&Others[K], 1); };
	if (FirstNot2 >= 0 || PlayerCount >= 2)
	{
		if (FirstNot2 >= 0) Focus = FirstNot2;
		for (int32 K = 0; K < NumSlots; ++K) { Ctx.Ids[K] = DefaultId; Ctx.Types[K] = DefaultType; }
		for (int32 T = 0; T < 3; ++T)
		{
			Lookup(T, Heats.Tables[T], SSXHeats::None, List);
		}
	}
	else
	{
		if (!bAllPresent)
		{
			for (int32 T = 0; T < 3; ++T)
			{
				Lookup(T, Heats.Tables[T], Ctx.Player, List);
			}
		}
		if (Humans < 2)
		{
			Lookup(3, D.Table3, Ctx.Player, List);
			Lookup(4, D.Table4, Ctx.Player, List);
		}
		// 0x37CD60, 0x37C720
		for (int32 K = 4; K >= 0; --K)
		{
			if (N > K + 1)
			{
				Lookup(5 + K, D.CountTable, Ctx.Player, One(K));
				Lookup(10 + K, D.PairTable, Ctx.Player, One(K));
			}
		}
	}
	FillLines(Ctx, D, Slots, Rand);
	return Focus;
}

void FillLines(FContext& Ctx, const FData& D, FSlot Slots[NumSlots], FRand Rand, int32 ForceVariant)
{
	for (int32 K = 0; K < NumSlots; ++K)
	{
		FSlot& S = Slots[K];
		S.Type = Ctx.Types[K];
		const FIntro* In = Ctx.Ids[K] != DefaultId ? D.Intros.Find(Ctx.Ids[K]) : nullptr;
		if (!In || In->Count <= 0)
		{
			for (FLine& L : S.Lines) L = FLine();
			continue;
		}
		int32 Variant = int32(Rand() % uint32(In->Count));
		if (ForceVariant >= 0) Variant = ForceVariant;
		Ctx.Variants[K] = Variant;
		const int32 Row = In->Start + Variant;
		for (int32 J = 0; J < NumLines; ++J)
		{
			FLine& L = S.Lines[J];
			L.Speaker = int8(In->Speakers[J]);
			L.LineId = D.VariantLines.IsValidIndex(Row) && D.VariantLines[Row].IsValidIndex(J) ? D.VariantLines[Row][J] : NoLine;
			if (const FData::FRecord* R = D.FindRecord(L.LineId))
			{
				L.Aux = R->Aux;
				L.Value = R->Value;
			}
		}
	}
}

void CopyToObject(const FSlot Slots[NumSlots], FObject& Obj, FRand Rand)
{
	for (int32 K = 0; K < NumSlots; ++K)
	{
		for (int32 J = 0; J < NumLines; ++J)
		{
			const FLine& L = Slots[K].Lines[J];
			FObjLine& O = Obj.Lines[K][J];
			O.Speaker = L.Speaker;
			O.LineId = L.LineId;
			if (L.Value < 0)
			{
				O.Speech = -1;
			}
			else
			{
				(void)Rand();
				O.Speech = L.Value;
			}
		}
	}
	Obj.Aux.Reset();
	for (int32 K = 0; K < NumSlots; ++K)
	{
		for (int32 J = 0; J < NumLines; ++J)
		{
			const int32 A = Slots[K].Lines[J].Aux;
			if (A != NoAux && !Obj.Aux.Contains(A) && Obj.Aux.Num() < 0x5A) Obj.Aux.Add(A);
		}
	}
}

TArray<int32> SortedSpeech(FObject& Obj, int32 Slot, const FData& D)
{
	Obj.Slot = Slot;
	TArray<int32> Values, Keys, Out;
	if (Slot < 0 || Slot >= NumSlots)
	{
		return Out;
	}
	for (int32 J = 0; J < NumLines; ++J)
	{
		const FObjLine& L = Obj.Lines[Slot][J];
		if (L.Speech < 0) continue;
		const FLineClip* C = D.Clip(L.LineId);
		const int32 Key = C ? C->Key() : -1;
		if (Key >= 0)
		{
			Values.Add(L.Speech);
			Keys.Add(Key);
		}
	}
	TArray<bool> Used;
	Used.Init(false, Values.Num());
	for (int32 N = 0; N < Values.Num(); ++N)
	{
		int32 Pick = -1;
		for (int32 I = 0; I < Values.Num(); ++I)
		{
			if (!Used[I] && (Pick < 0 || Keys[I] < Keys[Pick])) Pick = I;
		}
		Used[Pick] = true;
		Out.Add(Values[Pick]);
	}
	return Out;
}

TArray<int32> AssignActors(TConstArrayView<FSSXSceneActor> Actors, TConstArrayView<FSceneRacer> Racers)
{
	const int32 N = Racers.Num();
	// FUN_00176720
	int32 P1 = -1, P2 = -1, Before = -1, After = -1;
	for (int32 I = 0; I < N; ++I)
	{
		if (Racers[I].PlayerSlot == 0) P1 = I;
		if (Racers[I].PlayerSlot == 1) P2 = I;
	}
	if (P1 > 0) Before = P1 - 1;
	if (P1 < N - 1) After = P1 + 1;
	auto ByCharacter = [&](int32 Role)
	{
		for (int32 I = 0; I < N; ++I) { if (Racers[I].Character == Role - 14) return I; }
		return -1;
	};
	auto Named = [&](int32 Role) -> int32
	{
		switch (Role)
		{
		case 0: return P1;
		case 10: return P2;
		case 11: return Before;
		case 12: return After;
		default: return (Role > 13 && Role < 26) ? ByCharacter(Role) : -1;
		}
	};
	TArray<bool> Used;
	Used.Init(false, N);
	for (const FSSXSceneActor& A : Actors)
	{
		if (!A.bEnabled) continue;
		const int32 R = Named(A.Role);
		if (R >= 0 && R < N) Used[R] = true;
	}
	TArray<int32> Free;
	for (int32 I = 0; I < N; ++I) { if (!Used[I]) Free.Add(I); }
	int32 Cursor = 0;
	TArray<int32> Out;
	for (const FSSXSceneActor& A : Actors)
	{
		int32 R = -1;
		if (A.bEnabled)
		{
			if (A.Role == 13) { if (Free.IsValidIndex(Cursor)) R = Free[Cursor++]; }
			else R = Named(A.Role);
		}
		Out.Add(R);
	}
	return Out;
}

void FScenePlayer::Start(TConstArrayView<FSSXSceneActor> InActors, const FSSXCameraDefSet& Defs, TConstArrayView<FSceneRacer> Racers,
	const FObject& Obj, const FData& D)
{
	Actors.Reset();
	bRunning = true;
	const TArray<int32> Who = AssignActors(InActors, Racers);
	for (int32 K = 0; K < InActors.Num() && K < 8; ++K)
	{
		const FSSXSceneActor& Src = InActors[K];
		FActor& A = Actors.AddDefaulted_GetRef();
		A.Racer = Who[K];
		if (A.Racer < 0)
		{
			continue;
		}
		if (const TArray<int32>* L = Defs.FindAnimation(Src.List)) A.Codes = *L;
		A.bPlace = Src.bPlace;
		A.Pos = Src.Pos;
		A.Yaw = Src.Yaw;
		if (Src.StageArea != 0)
		{
			// FUN_00172100, FUN_001777D0, FUN_001741D0
			double AreaYaw = 0.0;
			FVector P = Src.Pos;
			if (Defs.StageAreaTransform(Src.StageArea, P, AreaYaw))
			{
				A.Pos = P;
			}
			A.Yaw = WrapYaw(Src.Yaw + AreaYaw);
		}
		if (A.bPlace && OnPlace)
		{
			OnPlace(A.Racer, A.Pos, A.Yaw);   // FUN_0011CAE8
		}
		// FUN_0016DA10
		if (A.Codes.Num() > 0 && A.Codes[0] >= 0)
		{
			A.bActive = true;
			A.Index = 0;
			PlayCode(A, A.Codes[0], Obj, D);
			A.bJustStarted = true;
		}
	}
}

void FScenePlayer::Stop()
{
	Actors.Reset();
	bRunning = false;
}

void FScenePlayer::PlayCode(FActor& A, int32 Code, const FObject& Obj, const FData& D)
{
	// FUN_00169BE8
	if (Obj.Slot < 0 || Obj.Slot >= NumSlots)
	{
		return;
	}
	for (int32 J = 0; J < NumLines; ++J)
	{
		const FObjLine& L = Obj.Lines[Obj.Slot][J];
		if (L.Speaker != Code) continue;
		const FLineClip* C = D.Clip(L.LineId);
		A.LineId = L.LineId;
		A.Frames = C ? C->Frames : 0;
		A.Events = C ? C->Events : TArray<FIntPoint>();
		if (L.Speech >= 0 && A.Events.Num() > 0) A.Events[0].Y = L.Speech;
		A.Frame = 0.0;
		if (OnLineStart) OnLineStart(A.Racer, L.LineId);
		return;
	}
}

void FScenePlayer::Step(const FObject& Obj, const FData& D)
{
	if (!bRunning)
	{
		return;
	}
	// 0x15A030, 0x15F300
	for (FActor& A : Actors)
	{
		if (!A.bActive || A.LineId < 0 || A.Frames <= 0)
		{
			continue;
		}
		if (A.bJustStarted)
		{
			A.bJustStarted = false;
			continue;
		}
		const double Old = A.Frame;
		const double New = Old + 0.5;
		for (const FIntPoint& E : A.Events)
		{
			if (Old <= double(E.X) && double(E.X) < New && OnEvent)
			{
				OnEvent(A.Racer, E.Y);
			}
		}
		A.Frame = FMath::Min(New, double(A.Frames - 1));
	}
	// FUN_00177020, FUN_0016DAA8
	for (FActor& A : Actors)
	{
		if (!A.bActive)
		{
			continue;
		}
		if (A.bPlace && OnPlace)
		{
			OnPlace(A.Racer, A.Pos, A.Yaw);
		}
		if (A.LineId >= 0 && A.Frames > 0 && A.Frame >= double(A.Frames - 1))
		{
			++A.Index;
			if (!A.Codes.IsValidIndex(A.Index) || A.Codes[A.Index] < 0) A.Index = 0;
			PlayCode(A, A.Codes[A.Index], Obj, D);
		}
	}
}
}
