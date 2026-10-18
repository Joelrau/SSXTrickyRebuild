#include "Progression/SSXHeats.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SSXHeats
{
float HeatFactor(int32 Place)
{
	static const uint32 Bits[3] = { 0x3F553F3Cu, 0x3EAAD18Bu, 0x3E2AD18Bu };
	if (Place < 0 || Place > 2)
	{
		return 0.f;
	}
	float F;
	FMemory::Memcpy(&F, &Bits[Place], 4);
	return F;
}

int32 FStoryRecord::Score(int32 InRider, int32 InVenue, int32 InMode, int32 InHeat) const
{
	const int32 V[4] = { Rider, Venue, Mode, Heat };
	const int32 Want[4] = { InRider, InVenue, InMode, InHeat };
	const int32 Wild[4] = { 12, 13, 10, 3 };
	const int32 Any[4] = { 13, 14, 11, 4 };
	int32 Total = 0;
	for (int32 K = 0; K < 4; ++K)
	{
		if ((V[K] != Wild[K] && V[K] == Want[K]) || V[K] == Any[K])
		{
			++Total;
		}
		else if (V[K] != Wild[K])
		{
			return -1;
		}
	}
	return Total;
}

FStoryRecord FStoryRecord::FromBytes(const uint8* B)
{
	FStoryRecord R;
	R.Rider = B[0]; R.Venue = B[1]; R.Mode = B[2]; R.Heat = B[3];
	for (int32 K = 0; K < 5; ++K) R.Riders[K] = B[4 + K];
	R.IntroId = uint16(B[9] | (B[10] << 8));
	R.IntroType = B[11];
	return R;
}

FString FData::DefaultPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("heats_data.json"));
}

bool FData::LoadJson(const FString& Path, FString* OutError)
{
	bLoaded = false;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		if (OutError) *OutError = Path + TEXT(" missing (tools/export_heats_data.py)");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Tabs = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* AggRows = nullptr;
	if (!Root->TryGetArrayField(TEXT("story_tables"), Tabs) || Tabs->Num() != 3 || !Root->TryGetArrayField(TEXT("aggression"), AggRows) || AggRows->Num() != NumRiders)
	{
		if (OutError) *OutError = Path + TEXT(": bad layout");
		return false;
	}
	for (int32 T = 0; T < 3; ++T)
	{
		Tables[T].Reset();
		for (const TSharedPtr<FJsonValue>& Rec : (*Tabs)[T]->AsArray())
		{
			const TArray<TSharedPtr<FJsonValue>>& B = Rec->AsArray();
			if (B.Num() != 12) continue;
			uint8 Bytes[12];
			for (int32 K = 0; K < 12; ++K) Bytes[K] = uint8(B[K]->AsNumber());
			Tables[T].Add(FStoryRecord::FromBytes(Bytes));
		}
	}
	for (int32 M = 0; M < NumRiders; ++M)
	{
		const TArray<TSharedPtr<FJsonValue>>& Row = (*AggRows)[M]->AsArray();
		for (int32 O = 0; O < NumRiders && O < Row.Num(); ++O)
		{
			const TArray<TSharedPtr<FJsonValue>>& W = Row[O]->AsArray();
			for (int32 K = 0; K < 4 && K < W.Num(); ++K) Agg[M][O][K] = int32(W[K]->AsNumber());
		}
	}
	bLoaded = Tables[0].Num() > 0;
	return bLoaded;
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

FStoryResult StoryLookup(const FData& D, int32 Rider, int32 Venue, int32 Mode, int32 Heat, FRand Rand)
{
	FStoryResult Out;
	for (int32 T = 0; T < 3; ++T)
	{
		int32 Best = 0;
		TArray<int32> Picks;
		for (int32 I = 0; I < D.Tables[T].Num(); ++I)
		{
			const int32 S = D.Tables[T][I].Score(Rider, Venue, Mode, Heat);
			if (S >= 0 && S >= Best)
			{
				if (S > Best) { Picks.Reset(); Best = S; }
				Picks.Add(I);
			}
		}
		if (Picks.Num() > 0)
		{
			const FStoryRecord& R = D.Tables[T][Picks[int32(Rand() % uint32(Picks.Num()))]];
			for (int32 K = 0; K < 5; ++K)
			{
				if (R.Riders[K] != None) Out.Riders[K] = R.Riders[K];
			}
			Out.IntroIds[T] = R.IntroId;
			Out.IntroTypes[T] = R.IntroType;
		}
	}
	return Out;
}

void FElimination::Reset()
{
	for (int32 R = 0; R < NumRiders; ++R)
	{
		Flag[R] = 0;
		for (int32 K = 0; K < NumHeats; ++K) Status[R][K] = Free;
	}
}

void FElimination::InitHeat0(const FData& D, int32 PlayerRider, int32 Venue, int32 Mode, FRand Rand)
{
	FStoryResult PerHeat[NumHeats];
	for (int32 K = 0; K < NumHeats; ++K)
	{
		PerHeat[K] = StoryLookup(D, PlayerRider, Venue, Mode, K, Rand);
	}
	for (int32 R = 0; R < NumRiders; ++R)
	{
		Flag[R] = 0;
		for (int32 K = 0; K < NumHeats; ++K) Status[R][K] = Free;
		for (int32 K = 0; K < NumHeats; ++K)
		{
			bool bStory = false;
			for (int32 J = 0; J < 5; ++J) bStory |= PerHeat[K].Riders[J] == R;
			if (R == PlayerRider)
			{
				Status[R][K] = Taken;
			}
			else if (bStory)
			{
				Status[R][K] = Taken;
				for (int32 J = 0; J < K; ++J) Status[R][J] = Held;
			}
		}
	}
}

FStoryResult BuildGrid(FElimination& E, const FData& D, int32 PlayerRider, int32 Venue, int32 Mode, int32 Heat, FRand Rand, int32 OutGrid[GridSize])
{
	const FStoryResult Story = StoryLookup(D, PlayerRider, Venue, Mode, Heat, Rand);
	for (int32 I = 0; I < GridSize; ++I) OutGrid[I] = None;
	const uint32 R = Rand();
	int32 P = int32(R & 3) + 1;
	if (Venue == AlaskaVenue && ((R & 3) == 1 || (R & 3) == 2))
	{
		P = 1;
	}
	OutGrid[P] = PlayerRider;
	auto Take = [&E, Heat](int32 Rider) { E.Flag[Rider] = 0; E.Status[Rider][Heat] = Taken; };
	if (Story.Riders[0] != None) { OutGrid[P - 1] = Story.Riders[0]; Take(Story.Riders[0]); }
	if (Story.Riders[1] != None) { OutGrid[P + 1] = Story.Riders[1]; Take(Story.Riders[1]); }
	for (int32 K = 2; K < 5; ++K)
	{
		const int32 Rider = Story.Riders[K];
		if (Rider == None) continue;
		for (int32 I = 0; I < GridSize; ++I)
		{
			if (OutGrid[I] == None) { OutGrid[I] = Rider; Take(Rider); break; }
		}
	}
	int64 Draws[NumRiders];
	for (int32 X = 0; X < NumRiders; ++X)
	{
		Draws[X] = (E.Flag[X] == 0 && E.Status[X][Heat] == Free) ? int64(Rand() % 10000u) : -1;
	}
	uint32 Used = 0;
	for (int32 I = 0; I < GridSize; ++I)
	{
		if (OutGrid[I] == None)
		{
			int64 Best = -1;
			int32 Pick = None;
			for (int32 X = 0; X < NumRiders; ++X)
			{
				if (Draws[X] > Best) { Best = Draws[X]; Pick = X; }
			}
			if (Pick == None)
			{
				Pick = None;
				for (int32 X = 0; X < NumRiders; ++X)
				{
					if (!((Used >> X) & 1)) { Pick = X; break; }
				}
			}
			else
			{
				Draws[Pick] = -1;
			}
			OutGrid[I] = Pick;
		}
		Used |= 1u << (OutGrid[I] & 31);
	}
	return Story;
}

FStoryResult SingleEventGrid(const FData& D, const TArray<int32>& HumanRiders, int32 N, int32 Venue, int32 Mode, FRand Rand,
	int32 OutGrid[GridSize], int32 OutHuman[GridSize])
{
	N = FMath::Clamp(N, 1, int32(GridSize));
	int32* G = OutGrid;
	int32* H = OutHuman;
	for (int32 I = 0; I < GridSize; ++I) { G[I] = None; H[I] = -1; }
	uint32 Mask = 0;
	int32 Count = 0;
	for (int32 S = 0; S < HumanRiders.Num() && S < 4 && Count < N; ++S)
	{
		H[Count] = S;
		G[Count] = HumanRiders[S];
		Mask |= 1u << (HumanRiders[S] & 31);
		++Count;
	}
	if (G[0] == None)
	{
		G[0] = int32(Rand() % uint32(NumRiders));
		Mask |= 1u << G[0];
		++Count;
	}
	const FStoryResult Story = StoryLookup(D, G[0], Venue, Mode, 3, Rand);
	for (int32 K = 0; K < 5 && Count < N; ++K)
	{
		const int32 R = Story.Riders[K];
		if (R != None && !((Mask >> R) & 1)) { G[Count++] = R; Mask |= 1u << R; }
	}
	if (Count < N)
	{
		int64 Draws[NumRiders];
		for (int32 X = 0; X < NumRiders; ++X) { Draws[X] = ((Mask >> X) & 1) ? -1 : int64(Rand() % 10000u); }
		while (Count < N)
		{
			int64 Best = -1;
			int32 Pick = None;
			for (int32 X = 0; X < NumRiders; ++X)
			{
				if (Draws[X] > Best) { Best = Draws[X]; Pick = X; }
			}
			G[Count++] = Pick;
			if (Pick != None) { Draws[Pick] = -1; }
		}
	}
	auto SwapEntry = [G, H](int32 A, int32 B) { ::Swap(G[A], G[B]); ::Swap(H[A], H[B]); };
	if (N > 1)
	{
		const int32 First = G[0];
		for (int32 I = 0; I < N; ++I) { SwapEntry(I, int32(Rand() % uint32(N))); }
		int32 P = 0;
		while (P < N && G[P] != First) { ++P; }
		if (Venue == AlaskaVenue && (P == 2 || P == 3)) { SwapEntry(1, P); P = 1; }
		auto Find = [G, N](int32 Rider) { for (int32 I = 0; I < N; ++I) { if (G[I] == Rider) return I; } return -1; };
		if (Story.Riders[0] != None)
		{
			int32 S = Find(Story.Riders[0]);
			if (S >= 0)
			{
				if (P == 0)
				{
					if (S == 1) { S = 0; }
					SwapEntry(0, 1);
					P = 1;
				}
				SwapEntry(P - 1, S);
			}
		}
		if (Story.Riders[1] != None)
		{
			int32 S = Find(Story.Riders[1]);
			if (S >= 0)
			{
				int32 Q = P;
				if (P == N - 1)
				{
					Q = N - 2;
					if (S == Q) { S = P; }
					SwapEntry(P, Q);
				}
				SwapEntry(Q + 1, S);
			}
		}
	}
	return Story;
}

bool HeatResult(FElimination& E, const TArray<int32>& FinishRiders, int32 PlayerRider, int32 Heat, float& InOutFactor)
{
	bool bAdvance = Heat != 2;
	for (int32 Place = 0; Place < FinishRiders.Num(); ++Place)
	{
		const int32 Rider = FinishRiders[Place];
		if (Rider == PlayerRider)
		{
			if (Place <= 2) InOutFactor = HeatFactor(Place);
			else bAdvance = false;
		}
		if (Place > 2 && Rider >= 0 && Rider < NumRiders)
		{
			E.Flag[Rider] = 1;
		}
	}
	return bAdvance;
}

void InitialLevels(const FData& D, const TArray<int32>& Riders, int32 HumanIndex, int32 Heat, const int32 Carried[NumRiders],
	const int32 Career[NumRiders], TArray<TArray<int32>>& Out)
{
	const int32 N = Riders.Num();
	Out.SetNum(N);
	for (int32 I = 0; I < N; ++I)
	{
		Out[I].SetNum(N);
		for (int32 J = 0; J < N; ++J)
		{
			const int32 Me = Riders[I], Other = Riders[J];
			if (J != HumanIndex) Out[I][J] = D.Agg[Me][Other][1];
			else if (Heat != 0 && Carried[Me] >= 0) Out[I][J] = Carried[Me];
			else Out[I][J] = Career[Me];
		}
	}
	for (int32 I = 0; I < N; ++I)
	{
		for (int32 J = 0; J < N; ++J)
		{
			const int32 Me = Riders[I], Other = Riders[J];
			const int32 Kind = D.Agg[Me][Other][3];
			const int32 Sub = Kind == 0 ? 2 : (Kind == 1 ? 3 : (Kind == 2 ? 1 : 0));
			Out[I][J] = FMath::Max(FMath::Max(Out[I][J] - Sub, 0), D.Agg[Me][Other][1]);
		}
	}
}

void SetupLevels(const FData& D, const TArray<int32>& Riders, int32 HumanIndex, const int32 Career[NumRiders], TArray<TArray<int32>>& Out)
{
	const int32 N = Riders.Num();
	Out.SetNum(N);
	for (int32 I = 0; I < N; ++I)
	{
		Out[I].SetNum(N);
		for (int32 J = 0; J < N; ++J)
		{
			// 0x167EA0, 0x167DA0
			Out[I][J] = J != HumanIndex ? D.Agg[Riders[I]][Riders[J]][1] : Career[Riders[I]];
		}
	}
}

void CarryOver(const TArray<int32>& Riders, int32 HumanIndex, const TArray<int32>& LevelsTowardHuman, int32 Carried[NumRiders])
{
	for (int32 J = 0; J < Riders.Num() && J < LevelsTowardHuman.Num(); ++J)
	{
		if (J != HumanIndex && Riders[J] >= 0 && Riders[J] < NumRiders)
		{
			Carried[Riders[J]] = LevelsTowardHuman[J];
		}
	}
}
}
