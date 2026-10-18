#include "Audio/SSXSpeechSystem.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"


bool FSSXSpeechSystem::Load(ESet InSet, const FString& SpeechDir, FString* OutError, uint32 Seed)
{
	Set = ESet::None;
	Engine.Clear(Seed);   // 0x228830 / 0x2BAF38 / 0x2BE8C8
	Log.Reset();
	static const TCHAR* RaceSections[] = { TEXT("mc"), TEXT("char"), TEXT("narr"), TEXT("anim") };
	static const TCHAR* FeSections[] = { TEXT("fe") };
	TArrayView<const TCHAR* const> Sections = InSet == ESet::FrontEnd ? TArrayView<const TCHAR* const>(FeSections) : TArrayView<const TCHAR* const>(RaceSections);
	if (InSet == ESet::None)
	{
		return false;
	}
	for (const TCHAR* S : Sections)
	{
		SSXSpeech::FEventDatabase Db;
		TArray<SSXSpeech::FSampleGroup> Groups;
		if (!SSXSpeech::LoadSection(FPaths::Combine(SpeechDir, S), S, Db, Groups, OutError))
		{
			Engine.Clear(0);
			return false;
		}
		Engine.Register(Db, Groups);
	}
	Set = InSet;
	return true;
}

TArray<SSXSpeech::FLine> FSSXSpeechSystem::Request(const SSXSpeech::FRequest& R, bool bQueue)
{
	FLog L;
	L.Req = R;
	TArray<SSXSpeech::FLine> Lines;
	if (IsLoaded())
	{
		const int32 Ch = SSXSpeech::ChannelOfDb((R[0] >> 24) & 0xFF);
		L.bPosted = Engine.Post(R);
		Lines = Engine.Service(Ch);
		L.Lines = Lines.Num();
		const TArray<SSXSpeech::FSampleGroup>& Groups = Engine.GetGroups();
		for (int32 K = 0; K < Lines.Num(); ++K)
		{
			for (int32 S = 0; S < Lines[K].Samples.Num(); ++S)
			{
				const SSXSpeech::FResolvedSample& Smp = Lines[K].Samples[S];
				if (OnSegment && Groups.IsValidIndex(Smp.Group))
				{
					OnSegment(Ch, Groups[Smp.Group], Smp, bQueue || K > 0 || S > 0);
				}
			}
		}
	}
	Log.Add(MoveTemp(L));
	return Lines;
}

bool FSSXSpeechSystem::Post(const SSXSpeech::FRequest& R)
{
	FLog L;
	L.Req = R;
	L.bPosted = IsLoaded() && Engine.Post(R);
	Log.Add(MoveTemp(L));
	return Log.Last().bPosted;
}

TArray<SSXSpeech::FLine> FSSXSpeechSystem::UpdateChannel(int32 Channel, bool bQueue)
{
	TArray<SSXSpeech::FLine> Lines;
	if (!IsLoaded() || Channel < 0 || Channel >= SSXSpeech::NumSpeechChannels)
	{
		return Lines;
	}
	Lines = Engine.Service(Channel);
	const TArray<SSXSpeech::FSampleGroup>& Groups = Engine.GetGroups();
	for (int32 K = 0; K < Lines.Num(); ++K)
	{
		for (int32 S = 0; S < Lines[K].Samples.Num(); ++S)
		{
			const SSXSpeech::FResolvedSample& Smp = Lines[K].Samples[S];
			if (OnSegment && Groups.IsValidIndex(Smp.Group))
			{
				OnSegment(Channel, Groups[Smp.Group], Smp, bQueue || K > 0 || S > 0);
			}
		}
	}
	if (Lines.Num() > 0)
	{
		FLog L;
		L.Req = Lines[0].Request;
		L.Lines = Lines.Num();
		Log.Add(MoveTemp(L));
	}
	return Lines;
}

TArray<SSXSpeech::FLine> FSSXSpeechSystem::RequestHeld(const SSXSpeech::FRequest& R, TArray<SSXSpeech::FResolvedSample>& OutSegments)
{
	const TFunction<void(int32, const SSXSpeech::FSampleGroup&, const SSXSpeech::FResolvedSample&, bool)> Saved = MoveTemp(OnSegment);
	OnSegment = [&OutSegments](int32, const SSXSpeech::FSampleGroup&, const SSXSpeech::FResolvedSample& Smp, bool)
	{
		OutSegments.Add(Smp);
	};
	TArray<SSXSpeech::FLine> Lines = Request(R, false);
	OnSegment = Saved;
	return Lines;
}

void FSSXSpeechSystem::Stop(int32 Channel)
{
	// 0x228D10
	const TFunction<void(int32, const SSXSpeech::FSampleGroup&, const SSXSpeech::FResolvedSample&, bool)> Saved = MoveTemp(OnSegment);
	OnSegment = nullptr;
	for (int32 K = 0; K < 4; ++K)
	{
		Engine.Service(Channel);
	}
	OnSegment = Saved;
	if (OnStop)
	{
		OnStop(Channel);
	}
}

int32 FSSXSpeechSystem::ClassChance(int32 Class, int32 Level)
{
	// 0x3A3528
	if (Level > 1)
	{
		Level = 1;
	}
	return (Class == 8 || Class == 16) ? 0 : 1024;
}

int32 FSSXSpeechSystem::LoadNarration(const FString& NascriptText, const FString& Track, int32 InLanguage)
{
	Language = InLanguage;
	Scripts.Reset();
	BreakMarkers = 0;
	const FString Venue = SSXSpeech::NascriptVenueFor(NascriptText, Track);
	if (!Venue.IsEmpty())
	{
		Scripts = SSXSpeech::ParseNascript(NascriptText, Venue, SSXSpeech::LanguageSuffix(InLanguage));
	}
	return Scripts.Num();
}

int32 FSSXSpeechSystem::Narrate(int32 ScriptId, bool bChannel0Playing)
{
	const TArray<int32>* Script = Scripts.Find(ScriptId);
	if (!Script || bChannel0Playing)
	{
		return 0;
	}
	int32 N = 0;
	for (const int32 V : *Script)
	{
		if (V == -1)
		{
			BreakMarkers = FMath::Min(BreakMarkers + 1, 10);
			continue;
		}
		const TArray<SSXSpeech::FRequest> Reqs = SSXSpeech::NarrationRequests({ V }, Language);
		for (const SSXSpeech::FRequest& R : Reqs)
		{
			Request(R, true);
			++N;
		}
	}
	return N;
}

void FSSXSpeechSystem::StopNarration()
{
	Stop(0);
	BreakMarkers = 0;
}

bool FSSXSpeechSystem::LoadFeTable(const FString& JsonPath)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *JsonPath))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Root->TryGetArrayField(TEXT("fe_requests"), Rows))
	{
		return false;
	}
	FeTable.Reset();
	for (const TSharedPtr<FJsonValue>& V : *Rows)
	{
		const TArray<TSharedPtr<FJsonValue>>& Pair = V->AsArray();
		if (Pair.Num() == 2)
		{
			FeTable.Add({ uint32(Pair[0]->AsNumber()), uint32(Pair[1]->AsNumber()) });
		}
	}
	return FeTable.Num() > 0;
}

bool FSSXSpeechSystem::FeRequest(int32 Id)
{
	Stop(0);
	if (Id < 0 || !FeTable.IsValidIndex(Id) || Set != ESet::FrontEnd)
	{
		return false;
	}
	return Request(SSXSpeech::MakeRequest({ SSXSpeech::RequestId(SSXSpeech::DbFE, 0), FeTable[Id].Key, FeTable[Id].Value, SSXSpeech::LanguageMask(Language) })).Num() > 0;
}

int32 FSSXSpeechSystem::RiderNameId(int32 Character)
{
	static const int32 Ids[12] = { 0x15, 0x13, 0x14, 0x11, 0x12, 0x19, 0x16, 0x18, 0x1A, 0x1B, 0x1C, 0x17 };
	return Character >= 0 && Character < 12 ? Ids[Character] : -1;
}
