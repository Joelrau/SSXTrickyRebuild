#include "SSXShowOffDraw.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Sim/PatchWorld.h"

namespace SSXShowOff
{
namespace
{
	int32 ParseLabel(const FString& Label)
	{
		const int32 At = Label.Find(TEXT("_inst"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		int32 P = At + 5, Value = 0, Digits = 0;
		while (P < Label.Len() && FChar::IsDigit(Label[P]))
		{
			Value = Value * 10 + (Label[P] - TEXT('0'));
			++P;
			++Digits;
		}
		return (Digits > 0 && Label.Mid(P).StartsWith(TEXT("_obj"), ESearchCase::CaseSensitive)) ? Value : INDEX_NONE;
	}
}

int32 InstanceOfActor(const AActor* Actor)
{
	if (!Actor)
	{
		return INDEX_NONE;
	}
	for (const FName& Tag : Actor->Tags)
	{
		const FString S = Tag.ToString();
		if (S.StartsWith(TEXT("SSXInst"), ESearchCase::CaseSensitive) && S.Len() > 7 && S.Mid(7).IsNumeric())
		{
			return FCString::Atoi(*S.Mid(7));
		}
	}
#if WITH_EDITOR
	return ParseLabel(Actor->GetActorLabel());
#else
	return INDEX_NONE;
#endif
}

bool IsShowOffTerrainActor(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	if (Actor->Tags.Contains(FName(ShowOffTerrainTag)))
	{
		return true;
	}
#if WITH_EDITOR
	return Actor->GetActorLabel().EndsWith(TEXT("_showoff_terrain"), ESearchCase::CaseSensitive);
#else
	return false;
#endif
}

int32 ApplyDrawRules(UWorld* World, const FPatchWorld& Sim, bool bShowOffMode)
{
	if (!World)
	{
		return 0;
	}
	const bool bInstances = Sim.NumDrawRecords() > 0;
	int32 Hidden = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsShowOffTerrainActor(*It))
		{
			It->SetActorHiddenInGame(!bShowOffMode);
			Hidden += !bShowOffMode;
			continue;
		}
		if (!bInstances)
		{
			continue;
		}
		const int32 Inst = InstanceOfActor(*It);
		if (Inst == INDEX_NONE)
		{
			continue;
		}
		const bool bDrawn = Sim.IsInstanceDrawn(Inst, bShowOffMode);
		It->SetActorHiddenInGame(!bDrawn);
		Hidden += !bDrawn;
	}
	return Hidden;
}
}
