#include "Audio/SSXAnnouncer.h"
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXRaceAudioFlow.h"
#include "Audio/SSXSpeech.h"
#include "Audio/SSXSpeechSystem.h"
#include "Harness/RiderSetup.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/PatchWorld.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Air/RiderMessages.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SpeechTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FString SpeechDir() { return FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("speech")); }

	TSharedPtr<FJsonObject> LoadSpeechRef(FAutomationTestBase& T)
	{
		static TSharedPtr<FJsonObject> Cached;
		if (Cached.IsValid()) return Cached;
		FString Text;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("speech_reference.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Cached))
		{
			SSXTest::Skip(T, TEXT("SimData/speech_reference.json missing (tools/make_speech_reference.py); reference checks skipped"));
			Cached.Reset();
		}
		return Cached;
	}

	bool MakeEngine(SSXSpeech::FEngine& E, std::initializer_list<const TCHAR*> Sections, FString& Err)
	{
		for (const TCHAR* S : Sections)
		{
			SSXSpeech::FEventDatabase Db;
			TArray<SSXSpeech::FSampleGroup> G;
			if (!SSXSpeech::LoadSection(FPaths::Combine(SpeechDir(), S), S, Db, G, &Err)) return false;
			E.Register(Db, G);
		}
		return true;
	}

	SSXSpeech::FRequest ReqOf(const TArray<TSharedPtr<FJsonValue>>& W)
	{
		SSXSpeech::FRequest R = SSXSpeech::MakeRequest({});
		for (int32 K = 0; K < W.Num() && K < SSXSpeech::RequestWords; ++K) R[K] = uint32(W[K]->AsNumber());
		return R;
	}

	FString LineKey(const SSXSpeech::FEngine& E, const SSXSpeech::FLine& L)
	{
		FString S = FString::Printf(TEXT("%x/%d"), L.Event ? L.Event->Id : 0, L.Variant);
		for (const SSXSpeech::FResolvedSample& Smp : L.Samples)
		{
			S += FString::Printf(TEXT(" %s#%d@%lld+%d"), *E.GetGroups()[Smp.Group].Path, Smp.Sample, Smp.Offset, Smp.Length);
		}
		return S;
	}

	FString LineKeyJson(const TSharedPtr<FJsonObject>& L)
	{
		FString S = FString::Printf(TEXT("%x/%d"), int32(L->GetNumberField(TEXT("event"))), int32(L->GetNumberField(TEXT("variant"))));
		for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("samples")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			S += FString::Printf(TEXT(" %s#%d@%lld+%d"), *A[0]->AsString(), int32(A[1]->AsNumber()), int64(A[2]->AsNumber()), int32(A[3]->AsNumber()));
		}
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechRngTest, "SSX.Speech.Rng", SpeechTestFlags)
bool FSSXSpeechRngTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadSpeechRef(*this);
	if (!Ref) return true;
	int32 Ok = 0, N = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("rng")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const uint32 Seed = uint32(O->GetNumberField(TEXT("seed")));
		SSXSpeech::FRng R(Seed);
		for (const TSharedPtr<FJsonValue>& X : O->GetArrayField(TEXT("values"))) { ++N; Ok += R.Next() == uint32(X->AsNumber()) ? 1 : 0; }
		SSXSpeech::FRng R2(Seed);
		SSXSpeech::FNoRepeatRandom Nrr;
		int32 K = 0;
		for (const TSharedPtr<FJsonValue>& X : O->GetArrayField(TEXT("draws")))
		{
			++N;
			Ok += Nrr.Draw(R2, uint32(K % 13 + 1), uint32(K % 3)) == uint32(X->AsNumber()) ? 1 : 0;
			++K;
		}
	}
	TestEqual(TEXT("SpeechRng / no-repeat values == model"), Ok, N);
	AddInfo(FString::Printf(TEXT("rng: %d/%d values match models/speech.py"), Ok, N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechDatabasesTest, "SSX.Speech.Databases", SpeechTestFlags)
bool FSSXSpeechDatabasesTest::RunTest(const FString&)
{
	struct FExp { const TCHAR* Name; int32 Db; int32 Events; int32 Groups; };
	const FExp Exp[] = { { TEXT("fe"), 0, 1, -1 }, { TEXT("mc"), 1, 39, 49 }, { TEXT("char"), 3, 21, 41 }, { TEXT("narr"), 4, 23, 24 }, { TEXT("anim"), 5, 1, 1 } };
	for (const FExp& E : Exp)
	{
		SSXSpeech::FEventDatabase Db;
		TArray<SSXSpeech::FSampleGroup> G;
		FString Err;
		if (!SSXSpeech::LoadSection(FPaths::Combine(SpeechDir(), E.Name), E.Name, Db, G, &Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s; database checks skipped"), *Err));
			return true;
		}
		TestEqual(FString::Printf(TEXT("%s db id"), E.Name), int32(Db.Db), E.Db);
		TestEqual(FString::Printf(TEXT("%s events"), E.Name), Db.Events.Num(), E.Events);
		if (E.Groups >= 0) TestEqual(FString::Printf(TEXT("%s groups"), E.Name), G.Num(), E.Groups);
		TestEqual(FString::Printf(TEXT("%s shorten limit"), E.Name), int32(Db.ShortenLimit), 100);
		TestEqual(FString::Printf(TEXT("%s interrupt priority"), E.Name), int32(Db.InterruptPriority), 500);
		TestEqual(FString::Printf(TEXT("%s channel"), E.Name), SSXSpeech::ChannelOfDb(Db.Db), (E.Db == 3 || E.Db == 5) ? 1 : 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechSelectionTest, "SSX.Speech.Selection", SpeechTestFlags)
bool FSSXSpeechSelectionTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadSpeechRef(*this);
	if (!Ref) return true;
	int32 Steps = 0, StepOk = 0, Lines = 0, Seqs = 0, RngOk = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("sequences")))
	{
		const TSharedPtr<FJsonObject> Sq = V->AsObject();
		SSXSpeech::FEngine E(uint32(Sq->GetNumberField(TEXT("seed"))));
		FString Err;
		if (!MakeEngine(E, { TEXT("mc"), TEXT("char"), TEXT("narr"), TEXT("anim") }, Err))
		{
			SSXTest::Skip(*this, Err + TEXT("; selection checks skipped"));
			return true;
		}
		++Seqs;
		bool bFirstFail = true;
		for (const TSharedPtr<FJsonValue>& SV : Sq->GetArrayField(TEXT("steps")))
		{
			const TSharedPtr<FJsonObject> St = SV->AsObject();
			const SSXSpeech::FRequest R = ReqOf(St->GetArrayField(TEXT("req")));
			const bool bPosted = E.Post(R);
			const TArray<SSXSpeech::FLine> Got = E.Service(int32(St->GetNumberField(TEXT("channel"))));
			const TArray<TSharedPtr<FJsonValue>>& Want = St->GetArrayField(TEXT("lines"));
			bool bOk = bPosted == St->GetBoolField(TEXT("posted")) && Got.Num() == Want.Num();
			for (int32 K = 0; bOk && K < Got.Num(); ++K) bOk = LineKey(E, Got[K]) == LineKeyJson(Want[K]->AsObject());
			++Steps;
			StepOk += bOk ? 1 : 0;
			Lines += Got.Num();
			if (!bOk && bFirstFail)
			{
				bFirstFail = false;
				AddError(FString::Printf(TEXT("sequence %d step %d: request %08x differs (posted %d, %d lines vs %d)"), int32(Sq->GetNumberField(TEXT("seed"))),
					Steps, R[0], bPosted ? 1 : 0, Got.Num(), Want.Num()));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>& Rng = Sq->GetArrayField(TEXT("rng"));
		bool bRng = true;
		for (int32 K = 0; K < 6; ++K) bRng &= E.GetRng().S[K] == uint32(Rng[K]->AsNumber());
		RngOk += bRng ? 1 : 0;
	}
	TestEqual(TEXT("steps equal to the model"), StepOk, Steps);
	TestEqual(TEXT("final RNG words equal to the model"), RngOk, Seqs);
	AddInfo(FString::Printf(TEXT("selection: %d/%d steps (%d lines) and %d/%d final RNG states match models/speech.py"), StepOk, Steps, Lines, RngOk, Seqs));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechRulesTest, "SSX.Speech.Rules", SpeechTestFlags)
bool FSSXSpeechRulesTest::RunTest(const FString&)
{
	using namespace SSXSpeech;
	FEngine E(7);
	FString Err;
	if (!MakeEngine(E, { TEXT("mc"), TEXT("narr") }, Err))
	{
		SSXTest::Skip(*this, Err + TEXT("; rule checks skipped"));
		return true;
	}
	E.Post(MakeRequest({ RequestId(DbMC, 0x2047), 2 }));
	E.Post(MakeRequest({ RequestId(DbMC, 0x2034), 1, 0x401 }));
	E.Post(MakeRequest({ RequestId(DbMC, 0x2049), 1, 0x400001, 12 }));
	const int32 P0 = E.PeekPick(0);
	TestTrue(TEXT("priority 800 first"), P0 >= 0 && E.SlotEvent(P0) && E.SlotEvent(P0)->Id == 0x2047);
	const TArray<FLine> L0 = E.Service(0);
	TestTrue(TEXT("0x2047 dispatched"), L0.Num() == 1 && L0[0].Event->Id == 0x2047);
	TestEqual(TEXT("the newer requests stay pending (only older ones are dropped)"), E.PendingCount(0), 2);
	E.Post(MakeRequest({ RequestId(DbMC, 0x2034), 1, 0x401 }));
	E.Post(MakeRequest({ RequestId(DbMC, 0x2049), 1, 0x400001, 12 }));
	const int32 P1 = E.PeekPick(0);
	TestTrue(TEXT("equal priority: most recent first"), P1 >= 0 && E.SlotEvent(P1)->Id == 0x2049);
	FEngine E2(3);
	MakeEngine(E2, { TEXT("mc") }, Err);
	E2.Post(MakeRequest({ RequestId(DbMC, 0x200F), 1, 1, 0, 0, 1, 0, 0, 1 }));
	const int32 H = E2.GetHead(0);
	const bool bChained = E2.Post(MakeRequest({ RequestId(DbMC, 0x2010), 2 }));
	TestTrue(TEXT("follow-on chained to the head"), bChained && H >= 0 && E2.SlotNext(H) >= 0);
	TestEqual(TEXT("a follow-on does not count as pending"), E2.PendingCount(0), 1);
	FEngine E3(5);
	MakeEngine(E3, { TEXT("mc") }, Err);
	E3.SetLastEvent(0, 0x2030, 1);
	FLine Out;
	TestFalse(TEXT("max immediate repeats"), E3.Resolve(MakeRequest({ RequestId(DbMC, 0x2030), 1 }), Out));
	FEngine E4(9);
	MakeEngine(E4, { TEXT("mc") }, Err);
	int32 Posted = 0;
	for (int32 K = 0; K < 20; ++K) Posted += E4.Post(MakeRequest({ RequestId(DbMC, 0x2036) })) ? 1 : 0;
	TestEqual(TEXT("equal-priority posts replace pending slots of the channel"), Posted, 20);
	TestEqual(TEXT("16 slots"), E4.PendingCount(0), 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechNascriptTest, "SSX.Speech.Nascript", SpeechTestFlags)
bool FSSXSpeechNascriptTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadSpeechRef(*this);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SSXAudio::ConfigPath(TEXT("NASCRIPT.INF"))))
	{
		SSXTest::Skip(*this, TEXT("NASCRIPT.INF missing (tools/convert_audio.py); skipped"));
		return true;
	}
	int32 Elements = 0, Resolved = 0, Match = 0, Scripts = 0;
	const TCHAR* Venues[] = { TEXT("ALASKA"), TEXT("SNOW"), TEXT("MESA"), TEXT("ELYSIUM"), TEXT("ALOHA"), TEXT("MERQUER"), TEXT("MEGAPLE"), TEXT("UNTRACK"), TEXT("PIPE"), TEXT("GARI") };
	TMap<FString, TArray<TSharedPtr<FJsonValue>>> Want;
	if (Ref)
	{
		for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("nascript")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			Want.Add(FString::Printf(TEXT("%s/%d"), *O->GetStringField(TEXT("venue")), int32(O->GetNumberField(TEXT("script")))), O->GetArrayField(TEXT("elements")));
		}
	}
	for (const TCHAR* Venue : Venues)
	{
		const TMap<int32, TArray<int32>> S = SSXSpeech::ParseNascript(Text, Venue, TEXT("_ENG"));
		TArray<int32> Keys;
		S.GetKeys(Keys);
		Keys.Sort();
		TestEqual(FString::Printf(TEXT("%s script ids"), Venue), Keys, TArray<int32>{ 1, 2, 101, 102, 201, 202 });
		for (const int32 Id : Keys)
		{
			++Scripts;
			SSXSpeech::FEngine E(1);
			FString Err;
			if (!MakeEngine(E, { TEXT("mc"), TEXT("narr") }, Err))
			{
				SSXTest::Skip(*this, Err + TEXT("; NASCRIPT resolution skipped"));
				return true;
			}
			const TArray<SSXSpeech::FRequest> Reqs = SSXSpeech::NarrationRequests(S[Id], 0);
			const TArray<TSharedPtr<FJsonValue>>* W = Want.Find(FString::Printf(TEXT("%s/%d"), Venue, Id));
			for (int32 K = 0; K < Reqs.Num(); ++K)
			{
				++Elements;
				E.Post(Reqs[K]);
				const TArray<SSXSpeech::FLine> L = E.Service(0);
				Resolved += L.Num() == 1 ? 1 : 0;
				if (W && W->IsValidIndex(K))
				{
					const TArray<TSharedPtr<FJsonValue>>& WL = (*W)[K]->AsObject()->GetArrayField(TEXT("lines"));
					Match += (L.Num() == WL.Num() && (L.Num() == 0 || LineKey(E, L[0]) == LineKeyJson(WL[0]->AsObject()))) ? 1 : 0;
				}
			}
		}
	}
	TestEqual(TEXT("NASCRIPT elements"), Elements, 180);
	TestEqual(TEXT("every element resolves"), Resolved, Elements);
	if (Ref) TestEqual(TEXT("elements equal to the model"), Match, Elements);
	AddInfo(FString::Printf(TEXT("NASCRIPT: %d scripts, %d/%d elements resolve, %d equal to models/speech.py"), Scripts, Resolved, Elements, Match));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechFrontEndTest, "SSX.Speech.FrontEnd", SpeechTestFlags)
bool FSSXSpeechFrontEndTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadSpeechRef(*this);
	FSSXSpeechSystem Sys;
	if (!Sys.LoadFeTable(FPaths::Combine(SpeechDir(), TEXT("speech_tables.json"))))
	{
		SSXTest::Skip(*this, TEXT("speech_tables.json missing (tools/convert_speech.py); skipped"));
		return true;
	}
	TestEqual(TEXT("FE request table ids"), Sys.FeTable.Num(), 81);
	int32 Ok = 0, Match = 0;
	for (int32 Id = 0; Id < Sys.FeTable.Num(); ++Id)
	{
		SSXSpeech::FEngine E{ uint32(Id) };
		FString Err;
		if (!MakeEngine(E, { TEXT("fe") }, Err))
		{
			SSXTest::Skip(*this, Err + TEXT("; skipped"));
			return true;
		}
		SSXSpeech::FLine L;
		const bool bOk = E.Resolve(SSXSpeech::MakeRequest({ 0, Sys.FeTable[Id].Key, Sys.FeTable[Id].Value, 1 }), L);
		Ok += bOk ? 1 : 0;
		if (Ref)
		{
			const TSharedPtr<FJsonObject> W = Ref->GetArrayField(TEXT("fe"))[Id]->AsObject();
			const TSharedPtr<FJsonObject>* WL = nullptr;
			const bool bWant = W->TryGetObjectField(TEXT("line"), WL);
			Match += (bOk == bWant && (!bOk || LineKey(E, L) == LineKeyJson(*WL))) ? 1 : 0;
		}
	}
	TestEqual(TEXT("FE ids resolving in English (75 of 81)"), Ok, 75);
	if (Ref) TestEqual(TEXT("FE resolutions equal to the model"), Match, 81);
	// 0x2703D0
	static const int32 Want[12] = { 0x15, 0x13, 0x14, 0x11, 0x12, 0x19, 0x16, 0x18, 0x1A, 0x1B, 0x1C, 0x17 };
	for (int32 C = 0; C < 12; ++C)
	{
		const int32 Id = FSSXSpeechSystem::RiderNameId(C);
		TestEqual(FString::Printf(TEXT("rider-name id of character %d"), C), Id, Want[C]);
		SSXSpeech::FEngine E{ uint32(Id) };
		FString Err;
		SSXSpeech::FLine L;
		TestTrue(FString::Printf(TEXT("rider-name id 0x%X resolves"), Id), Sys.FeTable.IsValidIndex(Id) && MakeEngine(E, { TEXT("fe") }, Err)
			&& E.Resolve(SSXSpeech::MakeRequest({ 0, Sys.FeTable[Id].Key, Sys.FeTable[Id].Value, 1 }), L));
	}
	TestEqual(TEXT("no character 12"), FSSXSpeechSystem::RiderNameId(12), -1);
	AddInfo(FString::Printf(TEXT("FE: %d/81 ids resolve, %d equal to models/speech.py"), Ok, Match));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechRamTest, "SSX.Speech.Ram", SpeechTestFlags)
bool FSSXSpeechRamTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadSpeechRef(*this);
	if (!Ref) return true;
	int32 N = 0, Ok = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("ram")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		SSXSpeech::FEngine E(0);
		FString Err;
		if (!MakeEngine(E, { TEXT("mc"), TEXT("char"), TEXT("narr"), TEXT("anim") }, Err))
		{
			SSXTest::Skip(*this, Err + TEXT("; skipped"));
			return true;
		}
		uint32 State[6];
		const TArray<TSharedPtr<FJsonValue>>& R = O->GetArrayField(TEXT("rng"));
		for (int32 K = 0; K < 6; ++K) State[K] = uint32(R[K]->AsNumber());
		E.GetRng().SetState(State);
		SSXSpeech::FLine L;
		const bool bOk = E.Resolve(ReqOf(O->GetArrayField(TEXT("req"))), L);
		const TSharedPtr<FJsonObject>* WL = nullptr;
		++N;
		Ok += (bOk && O->TryGetObjectField(TEXT("line"), WL) && LineKey(E, L) == LineKeyJson(*WL)) ? 1 : 0;
	}
	TestEqual(TEXT("RAM slot requests resolve as the model"), Ok, N);
	AddInfo(FString::Printf(TEXT("RAM: %d/%d slot requests (3 snapshots) resolve to the model's samples"), Ok, N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechAnnouncerTest, "SSX.Speech.Announcer", SpeechTestFlags)
bool FSSXSpeechAnnouncerTest::RunTest(const FString&)
{
	for (int32 C = 0; C < 24; ++C)
	{
		const bool bNever = C == 8 || C == 16;
		TestEqual(FString::Printf(TEXT("class %d gate"), C), FSSXSpeechSystem::ClassGate(C, 0, 0x3FF), !bNever);
	}
	// 0x233220
	TestEqual(TEXT("knockdown 0x40"), FSSXAnnouncer::MessageBit(SSXMsg::Knockdown, 0), 0x40u);
	TestEqual(TEXT("time bonus 4"), FSSXAnnouncer::MessageBit(SSXMsg::TimeBonus, 5), 4u);
	TestEqual(TEXT("multiplier 3 -> 0x10"), FSSXAnnouncer::MessageBit(SSXMsg::Multiplier, 3), 0x10u);
	TestEqual(TEXT("multiplier 4 -> none"), FSSXAnnouncer::MessageBit(SSXMsg::Multiplier, 4), 0u);
	TestEqual(TEXT("big air 1"), FSSXAnnouncer::MessageBit(SSXMsg::BigAir, 100), 1u);
	// 0x2334B0 / 0x233138
	const TArray<TArray<uint32>> Go = FSSXAnnouncer::GoRequests(4, 0, 0);
	TestEqual(TEXT("GO lines"), Go.Num(), 2);
	if (Go.Num() == 2)
	{
		TestEqual(TEXT("GO 0x204A words"), Go[0], TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbMC, 0x204A), 5, 0x400, 1 });
		TestEqual(TEXT("GO 0x2020 words"), Go[1], TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbMC, 0x2020), 5, 0x400, 1 });
	}
	TestEqual(TEXT("GO WC heat 1: 0x201F {5, 1, 0x205}"), FSSXAnnouncer::GoRequests(4, 3, 1)[1],
		TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbMC, 0x201F), 5, 1, 0x205 });
	TestEqual(TEXT("GO WC final on track 5: rank word 0x211"), FSSXAnnouncer::GoRankWord(2, 5, 4), 0x211u);
	TestEqual(TEXT("GO single race: 0x2020 {3, venue bit, 1}"), FSSXAnnouncer::GoRequests(2, 1, 3)[1],
		TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbMC, 0x2020), 3, 1, 1 });
	TestEqual(TEXT("GO WC showoff on venue 6: 0x201F with 0x441"), FSSXAnnouncer::GoRequests(5, 6, 0)[1],
		TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbMC, 0x201F), 5, 2, 0x441 });
	// 0x233AB0 / 0x233B70
	TestEqual(TEXT("from last to first"), FSSXAnnouncer::RankChangeWord(5, 0), 0x400101u);
	TestEqual(TEXT("first to second"), FSSXAnnouncer::RankChangeWord(0, 1), 0x400022u);
	TestEqual(TEXT("first to last"), FSSXAnnouncer::RankChangeWord(0, 5), 0x4090u);
	TestTrue(TEXT("rank words 12 / 10 / 9"), FSSXAnnouncer::RankWord(1) == 12 && FSSXAnnouncer::RankWord(4) == 10 && FSSXAnnouncer::RankWord(5) == 9);
	// 0x232498
	{
		uint32 W[8];
		int32 Follow = -1;
		TestFalse(TEXT("nothing nameable"), FSSXAnnouncer::TrickWords(0, 0, W, Follow));
		TestTrue(TEXT("Indy (byte 1 = 1)"), FSSXAnnouncer::TrickWords(0x0100, 0, W, Follow));
		TestEqual(TEXT("byte 1 page 0xF, byte 0 none, kind 2, rail 1"), W[5] * 1000000u + W[6] + W[1] * 100 + W[4] * 10, 0xFu * 1000000u + 0xFFFFFFFFu + 200u + 10u);
		TestTrue(TEXT("unnameable ',' -> 0x3D"), FSSXAnnouncer::TrickWords(0x2C, 0x100, W, Follow) && W[6] == 0x3D && W[2] == 1);
		TestTrue(TEXT("second grab -> follow-on page"), FSSXAnnouncer::TrickWords(0x020100, 0, W, Follow) && Follow == 0x15);
		TestTrue(TEXT("classes 0 / 1 / 2"), FSSXAnnouncer::TrickClass(3999) == 0 && FSSXAnnouncer::TrickClass(4000) == 1 && FSSXAnnouncer::TrickClass(10000) == 2);
	}
	{
		// 0x233A18
		FSSXSpeechSystem NoSys;
		FSSXAnnouncer A;
		A.Reset(4, 0, 0);
		int32 Draws = 0;
		auto Count = [&Draws]() { ++Draws; return 0u; };
		A.Posted.Reset();
		A.ForcedReset(NoSys, false, true, 0, Count);
		A.ForcedReset(NoSys, false, false, 1, Count);
		A.ForcedReset(NoSys, true, true, 1, Count);
		TestTrue(TEXT("reset without a zone / focus / racing: nothing, no draw"), A.Posted.Num() == 0 && Draws == 0);
		A.ForcedReset(NoSys, false, true, 1, Count);
		TestTrue(TEXT("reset in zone 1: MC 0x2052, one draw"), A.Posted == TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbMC, 0x2052) } && Draws == 1);
		A.Reset(6, 0, 0);
		A.ForcedReset(NoSys, false, true, 1, Count);
		TestEqual(TEXT("mode 6: no draw"), Draws, 1);
		SSXRaceAudio::FFlow F;
		F.Zones.Zone = 2;
		F.ForcedReset();
		TestTrue(TEXT("0x21C4D0: event 4 posted, zone cleared"), F.Zones.Zone == 0 && F.Commands.Num() == 1
			&& F.Commands[0].Kind == SSXRaceAudio::FCommand::PostEvent && F.Commands[0].Value == 4);
		F.Commands.Reset();
		F.ForcedReset();
		TestEqual(TEXT("no zone: nothing posted"), F.Commands.Num(), 0);
	}
	FSSXSpeechSystem Sys;
	FString Err;
	if (!Sys.Load(FSSXSpeechSystem::ESet::Race, SpeechDir(), &Err))
	{
		SSXTest::Skip(*this, Err + TEXT("; system checks skipped"));
		return true;
	}
	int32 Segs = 0, Queued = 0;
	Sys.OnSegment = [&](int32 Ch, const SSXSpeech::FSampleGroup&, const SSXSpeech::FResolvedSample&, bool bQueue) { ++Segs; Queued += bQueue ? 1 : 0; };
	FSSXAnnouncer A;
	A.Reset(4, 0, 0);
	auto RandA = []() { return 0u; };
	A.Go(Sys, RandA);
	TestEqual(TEXT("GO posted 0x204A then 0x2020"), A.Posted, TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbMC, 0x204A), SSXSpeech::RequestId(SSXSpeech::DbMC, 0x2020) });
	TestEqual(TEXT("MC helpers only post"), Segs, 0);
	const TArray<SSXSpeech::FLine> GoLines = Sys.UpdateChannel(0, false);
	TestTrue(TEXT("the per-frame update plays the newest (0x2020) and drops the older pending 0x204A"),
		GoLines.Num() == 1 && (GoLines[0].Request[0] & 0xFFFF) == 0x2020 && Sys.Engine.PendingCount(0) == 0);
	TestTrue(TEXT("GO lines handed to the player"), Segs > 0);
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *SSXAudio::ConfigPath(TEXT("NASCRIPT.INF"))))
	{
		TestEqual(TEXT("Garibaldi scripts"), Sys.LoadNarration(Text, TEXT("gari"), 0), 6);
		Segs = Queued = 0;
		const int32 Posted = Sys.Narrate(1, false);
		TestEqual(TEXT("op 0x21 arg 1: 3 elements"), Posted, 3);
		TestEqual(TEXT("narration segments queued back to back"), Queued, Segs);
		TestEqual(TEXT("op 0x21 ignored while channel 0 plays"), Sys.Narrate(1, true), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechDecodedTest, "SSX.Speech.Decoded", SpeechTestFlags)
bool FSSXSpeechDecodedTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadSpeechRef(*this);
	FString Text;
	TSharedPtr<FJsonObject> Index;
	if (!Ref || !FFileHelper::LoadFileToString(Text, *FPaths::Combine(SpeechDir(), TEXT("speech_index.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Index))
	{
		SSXTest::Skip(*this, TEXT("speech_index.json / reference missing; skipped"));
		return true;
	}
	int32 N = 0, Have = 0, Decoded = 0;
	auto Check = [&](const TSharedPtr<FJsonObject>& L)
	{
		for (const TSharedPtr<FJsonValue>& V : L->GetArrayField(TEXT("samples")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
			const FString Path = A[0]->AsString();
			TArray<FString> P;
			Path.ParseIntoArray(P, TEXT("/"));
			const FString Key = P[2].ToLower() + TEXT("/") + FPaths::GetBaseFilename(Path);
			const FString Off = FString::Printf(TEXT("%08llx"), (unsigned long long)int64(A[2]->AsNumber()));
			const TSharedPtr<FJsonObject>* G = nullptr;
			const TSharedPtr<FJsonObject>* E = nullptr;
			++N;
			if (Index->TryGetObjectField(Key, G) && (*G)->TryGetObjectField(Off, E))
			{
				++Have;
				double D = 0, S = 0;
				Decoded += ((*E)->TryGetNumberField(TEXT("decoded"), D) && (*E)->TryGetNumberField(TEXT("samples"), S) && D == S) ? 1 : 0;
			}
		}
	};
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("sequences")))
		for (const TSharedPtr<FJsonValue>& St : V->AsObject()->GetArrayField(TEXT("steps")))
			for (const TSharedPtr<FJsonValue>& L : St->AsObject()->GetArrayField(TEXT("lines"))) Check(L->AsObject());
	TestEqual(TEXT("every resolved sample starts an extracted SCHl stream"), Have, N);
	TestEqual(TEXT("decoded length == SCHl sample count"), Decoded, Have);
	AddInfo(FString::Printf(TEXT("decoded: %d/%d resolved samples have a stream, %d decoded to the header length"), Have, N, Decoded));
	return true;
}

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSpeechAnnouncerAu3Test, "SSX.Speech.AnnouncerAu3", SpeechTestFlags)
bool FSSXSpeechAnnouncerAu3Test::RunTest(const FString&)
{
	// 0x2314E0
	FSSXSpeechSystem NoSys;
	FSSXAnnouncer A;
	A.Reset(4, 0, 0);
	const uint32 Narr10 = SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8010);
	TestEqual(TEXT("request id 0x04008010"), Narr10, 0x04008010u);
	TestEqual(TEXT("rank 3 -> table 0x3A2750[3] = 8"), A.RankUpNarration(NoSys, 3),
		TArray<uint32>{ Narr10, 8u, SSXSpeech::LanguageMask(0) });
	TestEqual(TEXT("rank 0 (Master) -> 1"), A.RankUpNarration(NoSys, 0)[1], 1u);

	// 0x108378 / 0x233448
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("snow")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("snow race data / patches: %s: MC 0x204D checks skipped"), *Err));
		return true;
	}
	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::CircuitRace;
	TestTrue(TEXT("session"), Session.Init(Data, Cfg));
	TArray<TUniquePtr<FRiderSim>> Sims;
	for (int32 I = 0; I < 2; ++I)
	{
		TUniquePtr<FRiderSim> S = MakeUnique<FRiderSim>();
		SSXRiderSetup::RegisterStates(*S, true);
		S->SetWorld(&World);
		Session.AddRacer(S.Get(), I == 0);
		Sims.Add(MoveTemp(S));
	}
	const uint32 Mc4D = SSXSpeech::RequestId(SSXSpeech::DbMC, 0x204D);
	auto Count204D = [&A, Mc4D]() { int32 N = 0; for (uint32 W : A.Posted) { N += W == Mc4D ? 1 : 0; } return N; };
	auto RandA = []() { return 0u; };
	A.Reset(4, 0, 0);
	A.Frame(NoSys, Session, 0, RandA);
	TestEqual(TEXT("no landing: nothing"), Count204D(), 0);
	++Sims[1]->State.InfiniteBoostCallCount;   // 0x233448
	A.Frame(NoSys, Session, 0, RandA);
	TestEqual(TEXT("AI infinite-boost landing: MC 0x204D"), Count204D(), 1);
	A.Frame(NoSys, Session, 0, RandA);
	TestEqual(TEXT("posted once per landing"), Count204D(), 1);
	Sims[0]->State.FinishResult = 0;
	++Sims[0]->State.InfiniteBoostCallCount;
	A.Frame(NoSys, Session, 0, RandA);
	TestEqual(TEXT("finished (+0x418 >= 0): nothing"), Count204D(), 1);
	A.Reset(6, 0, 0);
	A.Frame(NoSys, Session, 0, RandA);
	++Sims[1]->State.InfiniteBoostCallCount;
	A.Frame(NoSys, Session, 0, RandA);
	TestEqual(TEXT("mode 6: nothing"), Count204D(), 0);
	A.Reset(4, 0, 0);
	A.bReplay = true;
	A.Frame(NoSys, Session, 0, RandA);
	++Sims[1]->State.InfiniteBoostCallCount;
	A.Frame(NoSys, Session, 0, RandA);
	TestEqual(TEXT("replay: nothing"), Count204D(), 0);

	FSSXSpeechSystem Sys;
	if (!Sys.Load(FSSXSpeechSystem::ESet::Race, SpeechDir(), &Err))
	{
		SSXTest::Skip(*this, Err + TEXT("; database checks skipped"));
		return true;
	}
	for (int32 Rank = 0; Rank < 10; ++Rank)
	{
		FSSXAnnouncer B;
		B.Reset(4, 0, 0);
		Sys.Stop(0);
		TArray<uint32> Words = B.RankUpNarration(Sys, Rank);
		const TArray<SSXSpeech::FLine> L = Sys.UpdateChannel(0, false);
		AddInfo(FString::Printf(TEXT("rank %d: %d lines (log %d)"), Rank, L.Num(), Sys.Log.Num() ? Sys.Log.Last().Lines : -1));
		TestTrue(FString::Printf(TEXT("NARR 0x8010 rank %d has a line"), Rank), Sys.Log.Num() > 0 && Sys.Log.Last().Lines > 0);
	}
	{
		Sys.Stop(0);
		SSXSpeech::FRequest R = SSXSpeech::MakeRequest({});
		R[0] = Mc4D;
		Sys.Post(R);
		TestTrue(TEXT("MC 0x204D has a line"), Sys.UpdateChannel(0, false).Num() > 0);
	}
	return true;
}
