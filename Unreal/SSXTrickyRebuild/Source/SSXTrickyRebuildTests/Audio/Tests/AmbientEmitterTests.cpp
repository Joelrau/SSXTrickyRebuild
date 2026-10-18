// 0x22BC40 / 0x22C648 / 0x22B008 / 0x22DC38
#include "Audio/SSXAmbientEmitters.h"
#include "Audio/SSXPropSounds.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/PreRaceIntro.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AmbientFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FSSXAmbientShape Shape(int32 Type, int32 Id, const FVector3f& Off, std::initializer_list<float> Rest)
	{
		FSSXAmbientShape S;
		S.W[0] = uint32(Type);
		S.W[1] = uint32(Id);
		const float O[3] = { Off.X, Off.Y, Off.Z };
		for (int32 K = 0; K < 3; ++K) { FMemory::Memcpy(&S.W[2 + K], &O[K], 4); }
		int32 K = 5;
		for (const float V : Rest) { FMemory::Memcpy(&S.W[K++], &V, 4); }
		return S;
	}

	void SynthGrid(FSSXPropSounds& P)
	{
		P.Reset();
		P.GridMinX = P.GridMinY = -50000.f;
		P.GridCell = 10000.f;
		P.GridNx = P.GridNy = 10;
	}

	void AddRecord(FSSXPropSounds& P, int32 Inst, const FVector3f& Pos, const FSSXAmbientShape& S)
	{
		FSSXAmbientRecord& R = P.AmbientRecords.AddDefaulted_GetRef();
		R.Instance = Inst;
		R.Position = Pos;
		R.Shapes.Add(S);
	}

	struct FLogBackend
	{
		TArray<FSSXAmbientStart> Starts;
		TArray<FString> Log;
		int32 Next = 100;
		void Bind(FSSXAmbientEmitters& E)
		{
			E.Start = [this](const FSSXAmbientStart& P) { Starts.Add(P); Log.Add(FString::Printf(TEXT("start:%d:%d"), P.SoundId, P.Volume127)); return Next++; };
			E.IsPlaying = [](int32) { return true; };
			E.SetVolume = [this](int32 H, int32 V) { Log.Add(FString::Printf(TEXT("vol:%d:%d"), H, V)); };
			E.Stop = [this](int32 H, float F) { Log.Add(FString::Printf(TEXT("stop:%d:%.2f"), H, F)); };
		}
	};

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

	class FLogEnv final : public SSXIntro::IEnv
	{
	public:
		TArray<int32> Ambient;
		virtual void Act(const FString& Name, int32 Arg, const FString&) override
		{
			if (Name == TEXT("ambient_enable")) { Ambient.Add(Arg); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAmbientRulesTest, "SSX.Audio.Ambient.Rules", AmbientFlags)
bool FSSXAmbientRulesTest::RunTest(const FString&)
{
	auto C = [](int32 K, float X) { float V = -99.f; return SSXAmbient::Curve(K, X, V) ? V : -99.f; };
	TestEqual(TEXT("curve 0 (1 - x^2)"), C(0, 0.5f), 0.75f);
	TestEqual(TEXT("curve 1"), C(1, 0.5f), 1.f - 0.5f / 1.25f);
	TestEqual(TEXT("curve 2 (1 - x)"), C(2, 0.25f), 0.75f);
	TestEqual(TEXT("curve 3"), C(3, 0.5f), 0.5f / 1.25f);
	TestEqual(TEXT("curve 4 ((1 - x)^2)"), C(4, 0.5f), 0.25f);
	TestEqual(TEXT("curve 5 flat to 0.7"), C(5, 0.7f), 1.f);
	TestTrue(TEXT("curve 5 then linear"), FMath::IsNearlyEqual(C(5, 0.85f), 0.5f, 1e-5f));
	float V = 0.f;
	TestFalse(TEXT("curve 6 has no case"), SSXAmbient::Curve(6, 0.5f, V));
	TestEqual(TEXT("0x52 GoGo2"), SSXAmbient::SwapBankName(0x52), FString(TEXT("GoGo2")));
	TestEqual(TEXT("0x7A Birds_Tropical"), SSXAmbient::SwapBankName(0x7A), FString(TEXT("Birds_Tropical")));
	TestEqual(TEXT("0xB0 Super_Conducting_Collider"), SSXAmbient::SwapBankName(0xB0), FString(TEXT("Super_Conducting_Collider")));
	TestEqual(TEXT("0xB9 UFO"), SSXAmbient::SwapBankName(0xB9), FString(TEXT("UFO")));
	TestEqual(TEXT("0x5F traffic (flag 1)"), SSXAmbient::SwapBankName(0x5F), FString(TEXT("Trafficloop")));
	TestEqual(TEXT("0x86 traffic (flag 0)"), SSXAmbient::SwapBankName(0x86, true, false), FString(TEXT("Traffic_Loop2")));
	TestTrue(TEXT("0x66 rider bank: TODO"), SSXAmbient::SwapBankName(0x66).IsEmpty());
	TestTrue(TEXT("0x94 no swap case"), SSXAmbient::SwapBankName(0x94).IsEmpty());
	for (int32 Id = 0; Id < 0x100; ++Id)
	{
		// 0x22E388
		if (FSSXPropSounds::IsSwapId(uint32(Id)) && Id != 0x66 && SSXAmbient::SwapBankName(Id).IsEmpty())
		{
			AddError(FString::Printf(TEXT("swap id 0x%X without a bank"), Id));
		}
	}
	TestTrue(TEXT("stream ids 0x61..0x63"), SSXAmbient::IsStreamId(0x61) && SSXAmbient::IsStreamId(0x63) && !SSXAmbient::IsStreamId(0x60) && !SSXAmbient::IsStreamId(0x64));
	TestTrue(TEXT("hit-activated 0x10 / 0x1C / 0x39"), SSXAmbient::IsHitActivated(0x10) && SSXAmbient::IsHitActivated(0x1C) && SSXAmbient::IsHitActivated(0x39) && !SSXAmbient::IsHitActivated(0x11));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAmbientTracksTest, "SSX.Audio.Ambient.Tracks", AmbientFlags)
bool FSSXAmbientTracksTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Tracks = LoadRef(*this);
	if (!Tracks.IsValid())
	{
		return false;
	}
	int32 Total0 = 0, Total1 = 0, NTracks = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : Tracks->Values)
	{
		const FString& Stem = KV.Key;
		const TSharedPtr<FJsonObject> T = KV.Value->AsObject();
		if (!T->HasField(TEXT("emitter_records")))
		{
			AddError(Stem + TEXT(": reference without emitters (re-run tools/export_prop_sounds.py)"));
			continue;
		}
		FSSXPropSounds P;
		FString Err;
		if (!P.Load(FSSXPropSounds::DefaultPath(Stem), &Err))
		{
			AddError(Err);
			continue;
		}
		++NTracks;
		TestEqual(Stem + TEXT(": emitter records"), P.AmbientRecords.Num(), int32(T->GetNumberField(TEXT("emitter_records"))));
		TMap<int32, int32> Types;
		int32 HitAct = 0;
		for (const FSSXAmbientRecord& R : P.AmbientRecords)
		{
			for (const FSSXAmbientShape& S : R.Shapes)
			{
				++Types.FindOrAdd(S.Type());
				HitAct += SSXAmbient::IsHitActivated(S.SoundId()) ? 1 : 0;
			}
		}
		const TSharedPtr<FJsonObject> RefTypes = T->GetObjectField(TEXT("shape_types"));
		bool bTypes = RefTypes->Values.Num() == Types.Num();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& RT : RefTypes->Values)
		{
			const int32* N = Types.Find(FCString::Atoi(*RT.Key));
			bTypes &= N && *N == int32(RT.Value->AsNumber());
		}
		TestTrue(Stem + TEXT(": shape types"), bTypes);
		Total0 += Types.FindRef(0);
		Total1 += Types.FindRef(1);
		TestEqual(Stem + TEXT(": hit-activated shapes"), HitAct, int32(T->GetNumberField(TEXT("hit_activated_shapes"))));
		const TArray<TSharedPtr<FJsonValue>>& G = T->GetArrayField(TEXT("grid"));
		TestTrue(Stem + TEXT(": grid"), G.Num() == 5 && P.GridMinX == float(G[0]->AsNumber()) && P.GridMinY == float(G[1]->AsNumber())
			&& P.GridCell == float(G[2]->AsNumber()) && P.GridNx == int32(G[3]->AsNumber()) && P.GridNy == int32(G[4]->AsNumber()));
		FSSXAmbientEmitters E;
		E.Build(&P);
		int32 Missing = 0, Outside = 0;
		for (int32 RI = 0; RI < P.AmbientRecords.Num(); ++RI)
		{
			const FSSXAmbientRecord& R = P.AmbientRecords[RI];
			for (const FSSXAmbientShape& S : R.Shapes)
			{
				const FVector3f C = R.Position + S.Offset();
				const int32 Col = int32(((C.X - P.GridMinX) + P.GridCell) / P.GridCell) - 1;
				const int32 Row = int32(((C.Y - P.GridMinY) + P.GridCell) / P.GridCell) - 1;
				if (Col < 0 || Row < 0 || Col >= P.GridNx || Row >= P.GridNy) { ++Outside; continue; }
				Missing += E.CellRecords(E.CellIndex(C.X, C.Y)).Contains(RI) ? 0 : 1;
			}
		}
		TestEqual(Stem + TEXT(": shapes missing from their centre's cell"), Missing, 0);
		if (Outside) { AddInfo(FString::Printf(TEXT("%s: %d emitter shapes outside the grid"), *Stem, Outside)); }
	}
	TestEqual(TEXT("tracks"), NTracks, 11);
	TestEqual(TEXT("type-0 shapes of attached records (all tracks)"), Total0, 553);
	TestEqual(TEXT("type-1 shapes (all tracks)"), Total1, 13);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAmbientPathTest, "SSX.Audio.Ambient.Path", AmbientFlags)
bool FSSXAmbientPathTest::RunTest(const FString&)
{
	{
		FSSXPropSounds P;
		SynthGrid(P);
		AddRecord(P, 7, FVector3f(0.f), Shape(0, 0x52, FVector3f(0.f), { 1000.f, 2.f }));
		AddRecord(P, 8, FVector3f(3000.f, 0.f, 0.f), Shape(3, 0x30, FVector3f(0.f), { 500.f }));
		FSSXAmbientEmitters E;
		E.Build(&P);
		FLogBackend B;
		B.Bind(E);
		TArray<FString> Expected;
		for (float X = -1500.f; X <= 4000.f; X += 250.f)
		{
			E.Query(FVector3f(X, 0.f, 0.f), 0);
			E.Update();
		}
		TestEqual(TEXT("two starts"), B.Starts.Num(), 2);
		if (B.Starts.Num() == 2)
		{
			const FSSXAmbientStart& S0 = B.Starts[0];
			TestTrue(TEXT("sphere: swap bank GoGo2, 3D at the centre"), S0.Bank == 6 && S0.SwapBank == TEXT("GoGo2") && S0.b3D && S0.Position == FVector3f(0.f));
			TestEqual(TEXT("sphere starts at x = -750 with (int)(0.25 x 127)"), S0.Volume127, 31);
			const FSSXAmbientStart& S1 = B.Starts[1];
			TestTrue(TEXT("2D sphere: track bank #0x30, not 3D, volume 127"), S1.Bank == 2 && S1.Index == 0x30 && !S1.b3D && S1.Volume127 == 127);
		}
		const int32 IStart0 = B.Log.IndexOfByKey(TEXT("start:82:31"));
		const int32 IStop0 = B.Log.IndexOfByKey(TEXT("stop:100:0.25"));
		TestTrue(TEXT("sphere volumes 63 (x = -500), 127 (x = 0), 63 (x = 500), then stop at x = 1000"),
			IStart0 >= 0 && IStop0 > IStart0 && B.Log.Find(TEXT("vol:100:63")) > IStart0 && B.Log.Find(TEXT("vol:100:127")) > IStart0
			&& B.Log.FindLast(TEXT("vol:100:63")) < IStop0 && B.Log.FindLast(TEXT("vol:100:31")) < IStop0);
		const int32 NVol0 = B.Log.FilterByPredicate([](const FString& L) { return L.StartsWith(TEXT("vol:100:")); }).Num();
		TestEqual(TEXT("sphere volume updates x = -500 .. 750"), NVol0, 6);
		TestTrue(TEXT("2D sphere stops at x = 3500"), B.Log.Contains(TEXT("stop:101:0.25")) && B.Log.Last() == TEXT("stop:101:0.25"));
		TestEqual(TEXT("no slot left"), E.NumActive(), 0);
	}
	{
		FSSXPropSounds P;
		SynthGrid(P);
		AddRecord(P, 1, FVector3f(0.f), Shape(1, 0x30, FVector3f(0.f), { 1000.f, 500.f, 250.f, 1.f, 0.f, 0.f, 0.f }));
		FVector3f Lo, Hi;
		FSSXAmbientEmitters::RecordBounds(P.AmbientRecords[0], Lo, Hi);
		TestTrue(TEXT("record AABB = union of the shapes (0x22AAA0)"), Lo == FVector3f(-1000.f, -500.f, -250.f) && Hi == FVector3f(1000.f, 500.f, 250.f));
		{
			FSSXAmbientRecord Far;
			Far.Position = FVector3f(100.f, 0.f, 0.f);
			Far.Shapes.Add(Shape(0, 0x30, FVector3f(20000.f, 0.f, 0.f), { 100.f, 2.f }));
			FSSXAmbientEmitters::RecordBounds(Far, Lo, Hi);
			TestTrue(TEXT("record AABB min starts at 10000: a shape 200 m out keeps min x at 10000"), Lo.X == 10100.f && Hi.X == 20200.f && Lo.Y == -100.f);
		}
		FSSXAmbientEmitters::ShapeBounds(P.AmbientRecords[0].Shapes[0], Lo, Hi);
		TestTrue(TEXT("ellipsoid AABB = centre +- (A a + B b + C c)"), Lo == FVector3f(-1000.f, -500.f, -250.f) && Hi == FVector3f(1000.f, 500.f, 250.f));
		FSSXAmbientEmitters E;
		E.Build(&P);
		FLogBackend B;
		B.Bind(E);
		E.Query(FVector3f(500.f, 0.f, 0.f), 0);
		TestEqual(TEXT("ellipsoid q 0.25 -> 0.9375"), E.GetSlot(0).Volume, 0.9375f);
		E.Update();
		E.Query(FVector3f(0.f, 400.f, 0.f), 0);
		TestTrue(TEXT("ellipsoid q 0.64 -> 0.5904"), FMath::IsNearlyEqual(E.GetSlot(0).Volume, 0.5904f, 1e-5f));
		E.Update();
		E.Query(FVector3f(0.f, 0.f, 300.f), 0);
		E.Update();
		TestTrue(TEXT("ellipsoid: outside on z stops"), B.Log.Num() == 3 && B.Log[0] == TEXT("start:48:119") && B.Log[1] == TEXT("vol:100:74") && B.Log[2] == TEXT("stop:100:0.25"));

		FSSXPropSounds Q;
		SynthGrid(Q);
		AddRecord(Q, 2, FVector3f(0.f), Shape(2, 0x30, FVector3f(0.f), { 2000.f, 1.f, 0.f, 0.f, 2.f, 0.5f, 2.f }));
		FSSXAmbientEmitters C;
		C.Build(&Q);
		C.Query(FVector3f(1000.f, 0.f, 0.f), 0);
		TestTrue(TEXT("cone on axis at 10 m: 0.5 x curve2(0)"), C.NumActive() == 1 && FMath::IsNearlyEqual(C.GetSlot(0).Volume, 0.5f, 1e-5f));
		C.Update();
		C.Query(FVector3f(500.f, 1000.f, 0.f), 0);
		C.Update();
		TestEqual(TEXT("cone: outside the angle"), C.NumActive(), 0);
		C.Query(FVector3f(-1000.f, 0.f, 0.f), 0);
		TestEqual(TEXT("cone: behind"), C.NumActive(), 0);
	}
	{
		FSSXPropSounds P;
		SynthGrid(P);
		AddRecord(P, 1, FVector3f(0.f), Shape(0, 0x52, FVector3f(0.f), { 1000.f, 2.f }));
		AddRecord(P, 2, FVector3f(0.f), Shape(0, 0x58, FVector3f(0.f), { 1000.f, 2.f }));
		FSSXAmbientEmitters E;
		E.Build(&P);
		FLogBackend B;
		B.Bind(E);
		for (int32 F = 0; F < 3; ++F) { E.Query(FVector3f(0.f), 0); E.Update(); }
		TestTrue(TEXT("swap: only the first registered (the later record: cells list the newest first) plays"),
			B.Starts.Num() == 1 && B.Starts[0].SwapBank == TEXT("River"));
		E.StopAll();
		B = FLogBackend();
		B.Bind(E);
		E.Query(FVector3f(0.f), 0);
		E.Query(FVector3f(0.f), 1);
		E.Update();
		TestTrue(TEXT("two listeners: the listener-0 slot is reset, listener 1 plays"), B.Starts.Num() == 1 && B.Starts[0].Listener == 1);
		E.StopAll();
		E.SetEnabled(false, false);
		B = FLogBackend();
		B.Bind(E);
		E.Query(FVector3f(0.f), 0);
		E.Update();
		TestEqual(TEXT("disabled: nothing registered"), E.NumActive(), 0);
		E.SetEnabled(true, true);
		E.AdvanceMs(240.0);
		TestFalse(TEXT("deferred enable not before 250 ms"), E.IsEnabled());
		E.SetEnabled(false, false);
		E.AdvanceMs(10.0);
		TestTrue(TEXT("deferred enable at 250 ms"), E.IsEnabled());

		FSSXPropSounds H;
		SynthGrid(H);
		AddRecord(H, 5, FVector3f(0.f), Shape(0, 0x1C, FVector3f(0.f), { 3200.f, 2.f }));
		FSSXAmbientEmitters A;
		A.Build(&H);
		FLogBackend BA;
		BA.Bind(A);
		A.Query(FVector3f(0.f), 0);
		A.Update();
		TestTrue(TEXT("hit gate: not in the hit list -> slot dropped, silent"), BA.Starts.Num() == 0 && A.NumActive() == 0);
		H.HitList.Add(5);
		A.Query(FVector3f(0.f), 0);
		A.Update();
		TestTrue(TEXT("hit gate: in the hit list -> track bank #0x18"), BA.Starts.Num() == 1 && BA.Starts[0].Bank == 2 && BA.Starts[0].Index == 0x18);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAmbientMerquryAlarmTest, "SSX.Audio.Ambient.MerquryAlarm", AmbientFlags)
bool FSSXAmbientMerquryAlarmTest::RunTest(const FString&)
{
	FSSXPropSounds P;
	FString Err;
	if (!P.Load(FSSXPropSounds::DefaultPath(TEXT("merquer")), &Err))
	{
		AddError(Err);
		return false;
	}
	const FSSXAmbientRecord* Alarm = nullptr;
	int32 NAlarms = 0;
	for (const FSSXAmbientRecord& R : P.AmbientRecords)
	{
		for (const FSSXAmbientShape& S : R.Shapes)
		{
			if (S.SoundId() == 0x1C || S.SoundId() == 0x10 || S.SoundId() == 0x39)
			{
				++NAlarms;
				const FSSXPropSoundEntry* E = P.Find(R.Instance);
				if (!Alarm && S.SoundId() == 0x1C && R.Shapes.Num() == 1 && E && E->ShapeIds[0] == 0x1C) { Alarm = &R; }
			}
		}
	}
	TestEqual(TEXT("53 hit-activated emitters"), NAlarms, 53);
	if (!TestNotNull(TEXT("an alarm prop (shape id 0x1C)"), Alarm))
	{
		return false;
	}
	FSSXAmbientEmitters E;
	E.Build(&P);
	FLogBackend B;
	B.Bind(E);
	const FVector3f At = Alarm->Position + Alarm->Shapes[0].Offset();
	for (int32 F = 0; F < 5; ++F) { E.Query(At, 0); E.Update(); }
	const bool bSilent = !B.Starts.ContainsByPredicate([&](const FSSXAmbientStart& S) { return S.Instance == Alarm->Instance; });
	TestTrue(TEXT("silent before the hit"), bSilent);
	P.NotePropSoundOwner(Alarm->Instance);   // 0x22D508
	TestTrue(TEXT("in the hit list"), P.IsInHitList(Alarm->Instance));
	E.Query(At, 0);
	E.Update();
	const FSSXAmbientStart* S = B.Starts.FindByPredicate([&](const FSSXAmbientStart& X) { return X.Instance == Alarm->Instance; });
	if (TestNotNull(TEXT("alarm starts after the hit"), S))
	{
		TestTrue(TEXT("merqurycity1 #24, 3D, full volume at the centre"), S->Bank == 2 && S->Index == 24 && S->b3D && S->Volume127 == 127);
	}
	const int32 Before = B.Starts.Num();
	for (int32 F = 0; F < 5; ++F) { E.Query(At, 0); E.Update(); }
	TestEqual(TEXT("started once"), B.Starts.Num(), Before);
	E.RaceRestart();
	TestTrue(TEXT("restart: stopped, hit list empty"), E.NumActive() == 0 && P.HitList.Num() == 0 && B.Log.Last().StartsWith(TEXT("stop:")));
	for (int32 F = 0; F < 3; ++F) { E.Query(At, 0); E.Update(); }
	const int32 AlarmStarts = B.Starts.FilterByPredicate([&](const FSSXAmbientStart& X) { return X.Instance == Alarm->Instance; }).Num();
	TestEqual(TEXT("silent again after the restart (started once in all)"), AlarmStarts, 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAmbientPreRaceTest, "SSX.Audio.Ambient.PreRace", AmbientFlags)
bool FSSXAmbientPreRaceTest::RunTest(const FString&)
{
	// 0x112B88 / 0x112D10
	SSXIntro::FSlot Slots[3];
	Slots[0].Type = 21;
	{
		FLogEnv Env;
		SSXIntro::FHandler H;
		H.Enter(Env, 1, 0, Slots, SSXIntro::EEntry::Normal);
		for (int32 F = 0; F < 4 && !H.IsDone(); ++F) { H.Update(Env); }
		TestEqual(TEXT("normal entry, slot 0 played"), Env.Ambient, TArray<int32>{ 0, 2, 2, 1 });
	}
	{
		SSXIntro::FSlot Null[3];
		FLogEnv Env;
		SSXIntro::FHandler H;
		H.Enter(Env, 1, 0, Null, SSXIntro::EEntry::Normal);
		for (int32 F = 0; F < 4 && !H.IsDone(); ++F) { H.Update(Env); }
		TestEqual(TEXT("normal entry, Null slots (no step 4)"), Env.Ambient, TArray<int32>{ 0, 2, 1 });
	}
	{
		FLogEnv Env;
		SSXIntro::FHandler H;
		H.Enter(Env, 1, 0, Slots, SSXIntro::EEntry::Restart);
		for (int32 F = 0; F < 4 && !H.IsDone(); ++F) { H.Update(Env); }
		TestEqual(TEXT("restart entry: step 0x12 only"), Env.Ambient, TArray<int32>{ 1 });
	}
	return !HasAnyErrors();
}

#endif
