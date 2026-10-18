// 0x22D8A8 / 0x22E388 / 0x22E2D0
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXPropSounds.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/RiderState.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString BankOf(const FString& Stem)
	{
		static const TMap<FString, FString> M = {
			{ TEXT("alaska"), TEXT("alaska1") }, { TEXT("aloha"), TEXT("iceberg") }, { TEXT("elysium"), TEXT("elysium1") },
			{ TEXT("gari"), TEXT("garibaldi1") }, { TEXT("megaple"), TEXT("megaplex1") }, { TEXT("merquer"), TEXT("merqurycity1") },
			{ TEXT("mesa"), TEXT("mesabanca1") }, { TEXT("pipe"), TEXT("pipedream1") }, { TEXT("snow"), TEXT("snowdream1") },
			{ TEXT("trick"), TEXT("tricktutorial") }, { TEXT("untrack"), TEXT("untracked1") } };
		const FString* B = M.Find(Stem);
		return B ? *B : FString();
	}

	TSharedPtr<FJsonObject> LoadRef(FAutomationTestBase& T)
	{
		FString Text;
		TSharedPtr<FJsonObject> Ref;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("prop_sounds_reference.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ref) || !Ref.IsValid())
		{
			T.AddError(TEXT("SimData/prop_sounds_reference.json missing (run tools/export_prop_sounds.py)"));
			return nullptr;
		}
		return Ref->GetObjectField(TEXT("tracks"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPropSoundsResolveTest, "SSX.Audio.PropSounds.Resolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXPropSoundsResolveTest::RunTest(const FString& Parameters)
{
	auto R = [](uint32 Id) { int32 B = -1, I = -1; return FSSXPropSounds::Resolve(Id, B, I) ? B * 1000 + I : -1; };
	TestEqual(TEXT("7 -> bank 2 #0x32"), R(7), 2000 + 0x32);
	TestEqual(TEXT("0x1F -> bank 2 #0x10"), R(0x1F), 2000 + 0x10);
	TestEqual(TEXT("0x2D -> bank 2 #0x14"), R(0x2D), 2000 + 0x14);
	TestEqual(TEXT("0x48 -> bank 2 #0x31"), R(0x48), 2000 + 0x31);
	TestEqual(TEXT("0x4E -> bank 2 #0x35"), R(0x4E), 2000 + 0x35);
	TestEqual(TEXT("0x3A -> bank 2 #0"), R(0x3A), 2000);
	TestEqual(TEXT("0x61 / 0x62 / 0x63 -> Crowd #0 / #1 / #2"), R(0x61) * 1000000 + R(0x62) * 1000 + R(0x63), 3000 * 1000000 + 3001 * 1000 + 3002);
	TestEqual(TEXT("0x9A -> bank 2 #0x75"), R(0x9A), 2000 + 0x75);
	TestEqual(TEXT("0xB6 -> bank 2 #13"), R(0xB6), 2013);
	TestTrue(TEXT("swap ids (0x4F..0x60, 0x66, 0x67..0x93, 0x9F..0xB2, 0xB7..0xB9) -> bank 6 #0"),
		R(0x4F) == 6000 && R(0x60) == 6000 && R(0x66) == 6000 && R(0x93) == 6000 && R(0xB2) == 6000 && R(0xB9) == 6000);
	TestTrue(TEXT("ids without a case (0, 1, 0x64, 0xBA) leave the index unset"), R(0) < 0 && R(1) < 0 && R(0x64) < 0 && R(0xBA) < 0);
	TestEqual(TEXT("manifest id"), FSSXPropSounds::ManifestId(7, TEXT("garibaldi1")), FString(TEXT("sfx/garibaldi1/50")));
	TestEqual(TEXT("crowd manifest id"), FSSXPropSounds::ManifestId(0x62, TEXT("garibaldi1")), FString(TEXT("sfx/Crowd/01")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPropSoundsTracksTest, "SSX.Audio.PropSounds.Tracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXPropSoundsTracksTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Tracks = LoadRef(*this);
	if (!Tracks.IsValid())
	{
		return false;
	}
	FString Manifest;
	FFileHelper::LoadFileToString(Manifest, *FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT(".."), TEXT("converted"), TEXT("audio"), TEXT("manifest.json")));
	if (Manifest.IsEmpty()) { SSXTest::Skip(*this, TEXT("converted/audio/manifest.json missing: sound existence not checked")); }
	int32 Resolved = 0, Missing = 0, Unresolved = 0, NTracks = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Tracks->Values)
	{
		const FString& Stem = KV.Key;
		const TSharedPtr<FJsonObject> T = KV.Value->AsObject();
		FSSXPropSounds P;
		FString Err;
		if (!P.Load(FSSXPropSounds::DefaultPath(Stem), &Err))
		{
			AddError(Err);
			continue;
		}
		++NTracks;
		TestEqual(Stem + TEXT(": .adl records"), P.NumRecords, int32(T->GetNumberField(TEXT("records"))));
		TestEqual(Stem + TEXT(": records attached"), P.NumAttached, int32(T->GetNumberField(TEXT("attached"))));
		TestEqual(Stem + TEXT(": instances with a sound list"), P.Entries.Num(), int32(T->GetNumberField(TEXT("entries"))));
		TMap<uint32, int32> Hist;
		for (const FSSXPropSoundEntry& E : P.Entries) { ++Hist.FindOrAdd(E.HitId); }
		const TSharedPtr<FJsonObject> RefHist = T->GetObjectField(TEXT("hit_id_counts"));
		bool bHist = RefHist->Values.Num() == Hist.Num();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& H : RefHist->Values)
		{
			const int32* N = Hist.Find(uint32(FCString::Atoi(*H.Key)));
			bHist &= N && *N == int32(H.Value->AsNumber());
		}
		TestTrue(Stem + TEXT(": hit-id histogram"), bHist);
		int32 BadSamples = 0;
		for (const TSharedPtr<FJsonValue>& S : T->GetArrayField(TEXT("samples")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = S->AsArray();
			const FSSXPropSoundEntry* E = P.Find(int32(A[0]->AsNumber()));
			BadSamples += !E || E->HitId != uint32(A[1]->AsNumber());
		}
		TestEqual(Stem + TEXT(": sample instances"), BadSamples, 0);
		const FString Bank = BankOf(Stem);
		for (const FSSXPropSoundEntry& E : P.Entries)
		{
			const FString Id = FSSXPropSounds::ManifestId(E.HitId, Bank);
			if (Id.IsEmpty()) { ++Unresolved; continue; }
			++Resolved;
			Missing += !Manifest.IsEmpty() && !Manifest.Contains(TEXT("\"") + Id + TEXT("\""));
		}
	}
	TestEqual(TEXT("tracks with prop sound lists"), NTracks, 11);
	AddInfo(FString::Printf(TEXT("prop hit sounds: %d resolved, %d not in the bank, %d unresolved"), Resolved, Missing, Unresolved));
	TestTrue(TEXT("resolved hit sounds exist in their bank (>= 99%)"), Resolved > 0 && Missing * 100 <= Resolved);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPropSoundsHitTest, "SSX.Audio.PropSounds.Hit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXPropSoundsHitTest::RunTest(const FString& Parameters)
{
	FSSXPropSounds Gari;
	FString Err;
	if (!Gari.Load(FSSXPropSounds::DefaultPath(TEXT("gari")), &Err))
	{
		AddError(Err);
		return false;
	}
	const FSSXPropSoundEntry* Hit7 = nullptr;
	for (const FSSXPropSoundEntry& E : Gari.Entries) { if (E.HitId == 7) { Hit7 = &E; break; } }
	if (!TestNotNull(TEXT("a gari prop with hit id 7"), Hit7))
	{
		return false;
	}
	int32 NoList = 0;
	while (Gari.Find(NoList)) { ++NoList; }
	Gari.BindToBoardSounds(TEXT("garibaldi1"));
	USSXBoardSoundComponent* C = NewObject<USSXBoardSoundComponent>();
	FRiderState S;
	S.Frame = 0;
	C->UpdateFromRider(S, 1.f / 60.f);
	auto Step = [&]() { ++S.Frame; C->OneShotLog.Reset(); C->UpdateFromRider(S, 1.f / 60.f); };
	S.PropHitCount = 1; S.PropHitInstance = NoList; S.PropHitSpeed = 1500.0;
	Step();
	TestEqual(TEXT("instance without a sound record: silent"), C->OneShotLog.Num(), 0);
	S.PropHitCount = 2; S.PropHitInstance = Hit7->Instance;
	Step();
	TestEqual(TEXT("hit id 7 plays track bank #0x32 (3D)"), C->OneShotLog, TArray<FString>({ TEXT("sfx/garibaldi1/50") }));
	TestTrue(TEXT("3D at the rider"), C->OneShot3DLog.Num() == 1 && C->OneShot3DLog[0]);
	TestEqual(TEXT("the prop-sound owner"), Gari.LastOwner, Hit7->Instance);
	S.PropHitCount = 3;
	Step();
	TestEqual(TEXT("ring hold 50 on the same instance"), C->OneShotLog.Num(), 0);
	S.PropHitCount = 4; S.Frame += SSXBoardAudio::PropHitHold;
	Step();
	TestEqual(TEXT("after the hold it plays again"), C->OneShotLog.Num(), 1);

	FSSXPropSounds Merq;
	if (Merq.Load(FSSXPropSounds::DefaultPath(TEXT("merquer")), &Err))
	{
		const FSSXPropSoundEntry* Alarm = nullptr;
		for (const FSSXPropSoundEntry& E : Merq.Entries) { if (E.ShapeIds[0] == 0x1C) { Alarm = &E; break; } }
		if (TestNotNull(TEXT("a merquer prop with a hit-activated emitter (shape id 0x1C)"), Alarm))
		{
			Merq.BindToBoardSounds(TEXT("merqurycity1"));
			TestFalse(TEXT("not in the hit list before"), Merq.IsInHitList(Alarm->Instance));
			S.PropHitCount = 5; S.PropHitInstance = Alarm->Instance;
			Step();
			TestTrue(TEXT("hit -> hit list (0x22E2D0)"), Merq.IsInHitList(Alarm->Instance));
			TestEqual(TEXT("its hit id is unresolved: no sound"), C->OneShotLog.Num(), Alarm->HitId == 0 ? 0 : 1);
			Merq.NotePropSoundOwner(Alarm->Instance);
			TestEqual(TEXT("no duplicate entry"), Merq.HitList.Num(), 1);
		}
	}
	else
	{
		AddError(Err);
	}
	FSSXPropSounds::UnbindBoardSounds();
	return !HasAnyErrors();
}

#endif
