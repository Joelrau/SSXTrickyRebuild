// 0x2D6928 / 0x2213F8
#include "Audio/SSXAmbientEmitters.h"
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXCrowdSequencer.h"
#include "Audio/SSXCrowdSystem.h"
#include "Audio/SSXPropSounds.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CrowdFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FString CrowdFile(const FString& Name) { return FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("crowd"), Name); }

	bool LoadSeq(const FString& Name, SSXCrowdSeq::FSequence& Out, TArray<uint8>* Raw = nullptr)
	{
		TArray<uint8> B;
		if (!FFileHelper::LoadFileToArray(B, *CrowdFile(Name + TEXT(".eam")), FILEREAD_Silent))
		{
			return false;
		}
		if (Raw) { *Raw = B; }
		Out.Name = Name;
		return Out.Parse(B);
	}

	FString Ev(const SSXCrowdSeq::FEvent& E)
	{
		return E.Data2 >= 0 ? FString::Printf(TEXT("%02X %02X %02X %02X"), E.Wait, E.Status, E.Data1, E.Data2)
			: FString::Printf(TEXT("%02X %02X %02X"), E.Wait, E.Status, E.Data1);
	}

	struct FNotes
	{
		TArray<FString> Log;
		TSet<int32> Live;
		int32 Next = 0;
		void Bind(SSXCrowdSeq::FPlayer& P, const int32* Step)
		{
			P.NoteOn = [this, Step](const SSXCrowdSeq::FNoteOn& N)
			{
				Log.Add(FString::Printf(TEXT("on:%d:%d:%d:%d:%d"), *Step, N.Program, N.Key, N.Velocity, N.Level));
				Live.Add(Next);
				return Next++;
			};
			P.NoteOff = [this, Step](int32 V) { Log.Add(FString::Printf(TEXT("off:%d:%d"), *Step, V)); Live.Remove(V); };
			P.StopVoice = [this](int32 V) { Log.Add(FString::Printf(TEXT("stop:%d"), V)); Live.Remove(V); };
			P.IsVoicePlaying = [this](int32 V) { return Live.Contains(V); };
			P.SetVoiceLevel = [this](int32 V, int32 L) { Log.Add(FString::Printf(TEXT("level:%d:%d"), V, L)); };
		}
	};

	struct FCrowdLog
	{
		TArray<FString> Log;
		TMap<int32, FSSXCrowdStart> Live;
		TMap<int32, int32> Vol;
		TMap<int32, FVector3f> Pos;
		int32 Next = 0;
		void Bind(FSSXCrowdSystem& C)
		{
			C.Start = [this](const FSSXCrowdStart& S)
			{
				Log.Add(S.Sequence ? FString::Printf(TEXT("seq:%s:%d"), *S.Sequence->Name, S.Volume)
					: FString::Printf(TEXT("stream:%d:%d"), S.SoundIndex, S.Volume));
				Live.Add(Next, S);
				return Next++;
			};
			C.IsPlaying = [this](int32 H) { return Live.Contains(H); };
			C.SetVolume = [this](int32 H, int32 V) { Vol.Add(H, V); };
			C.SetPosition = [this](int32 H, const FVector3f& P) { Pos.Add(H, P); };
			C.Stop = [this](int32 H, float F) { Log.Add(FString::Printf(TEXT("stop:%d:%.2f"), H, F)); Live.Remove(H); };
		}
	};

	bool LoadCrowdConfig(FSSXCrowdSystem& C)
	{
		FSSXInfFile Inf;
		if (!Inf.Load(SSXAudio::ConfigPath(TEXT("CROWD.INF"))))
		{
			return false;
		}
		C.LoadConfig(Inf, FString(), [](const FString& Path, TArray<uint8>& Out)
		{
			return FFileHelper::LoadFileToArray(Out, *CrowdFile(FPaths::GetCleanFilename(Path.Replace(TEXT("\\"), TEXT("/")))), FILEREAD_Silent);
		});
		return C.GetSequences().Num() == 9;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCrowdSequenceTest, "SSX.Audio.Crowd.Sequence", CrowdFlags)
bool FSSXCrowdSequenceTest::RunTest(const FString&)
{
	using namespace SSXCrowdSeq;
	static const TCHAR* const Names[] = { TEXT("Cheer10"), TEXT("Cheer20"), TEXT("Cheer30"), TEXT("Ahh10"), TEXT("Ahh20"), TEXT("Ahh30"),
		TEXT("Anticipation10"), TEXT("Anticipation20"), TEXT("Anticipation30") };
	int32 Loaded = 0;
	for (const TCHAR* N : Names)
	{
		FSequence S;
		TArray<uint8> Raw;
		if (!LoadSeq(N, S, &Raw))
		{
			continue;
		}
		++Loaded;
		// the events re-encode to the bytes after "MIDx" up to the end token (one trailing byte follows it)
		TArray<uint8> Re;
		for (const SSXCrowdSeq::FEvent& E : S.Events)
		{
			Re.Add(E.Wait); Re.Add(E.Status); Re.Add(E.Data1);
			if (E.Data2 >= 0) { Re.Add(uint8(E.Data2)); }
		}
		TestTrue(FString::Printf(TEXT("%s: events = file bytes"), N), Re.Num() + 1 == S.Data.Num()
			&& FMemory::Memcmp(Re.GetData(), S.Data.GetData(), Re.Num()) == 0 && Raw.Num() == S.Data.Num() + 4);
		TestEqual(FString::Printf(TEXT("%s: ends with F0 2F"), N), Ev(S.Events.Last()), FString(TEXT("00 F0 2F")));
	}
	if (Loaded == 0)
	{
		SSXTest::Skip(*this, TEXT("no .eam files (run tools/convert_audio.py --only crowd); synthetic cases only"));
	}
	FSequence Cheer10, Ahh30, Ant20, Ahh10;
	if (LoadSeq(TEXT("Cheer10"), Cheer10) && LoadSeq(TEXT("Ahh30"), Ahh30) && LoadSeq(TEXT("Anticipation20"), Ant20) && LoadSeq(TEXT("Ahh10"), Ahh10))
	{
		TArray<FString> A;
		for (int32 I = 0; I < 5; ++I) { A.Add(Ev(Cheer10.Events[I])); }
		TestEqual(TEXT("Cheer10: program 0x12, pan 64, volume 127, note 0x3E vel 127, 255-tick wait"), FString::Join(A, TEXT("|")),
			FString(TEXT("01 C0 12|00 B0 0A 40|03 B0 07 7F|FF 90 3E 7F|FF FF F8")));
		TestEqual(TEXT("Ahh30: two notes (0x3C, 0x3E) on program 0x10"), Ev(Ahh30.Events[3]) + TEXT("|") + Ev(Ahh30.Events[4]),
			FString(TEXT("00 90 3C 7F|FF 90 3E 7F")));
		TestEqual(TEXT("Anticipation20: program 0x11, no pan, notes 0x3D vel 0x5A + 0x3C"), Ev(Ant20.Events[0]) + TEXT("|") + Ev(Ant20.Events[2])
			+ TEXT("|") + Ev(Ant20.Events[3]), FString(TEXT("01 C0 11|03 90 3D 5A|FF 90 3C 7F")));
		TestEqual(TEXT("Ahh10: 2002 ticks of waits before the end"), Ahh10.LengthTicks(), 2002);

		FPlayer P;
		int32 Step = 0;
		FNotes N;
		N.Bind(P, &Step);
		P.Start(Ahh10, 100);
		for (Step = 0; Step < 2100 && P.IsPlaying(); ++Step)
		{
			P.Tick();
		}
		TestEqual(TEXT("Ahh10: on at step 2 (program 0x10, key 0x3C, level 0x6E x 100 >> 7), off at step 2002"), FString::Join(N.Log, TEXT("|")),
			FString(TEXT("on:2:16:60:127:85|off:2002:0")));
		TestEqual(TEXT("Ahh10: the slot stops at the end token on step 2002"), Step, 2003);
		TestTrue(TEXT("Ahh10: 2003 ticks of one 384-sample block at 36 kHz (10.667 ms) = 21.37 s"),
			FMath::IsNearlyEqual(double(P.TicksRun()) * TickMs, 2003.0 * 384.0 / 36.0, 1e-6));

		FPlayer W;
		FNotes N2;
		N2.Bind(W, &Step);
		W.NoteOff = [](int32) {};
		W.Start(Ahh10, 127);
		for (Step = 0; Step < 2100; ++Step) { W.Tick(); }
		TestTrue(TEXT("end token waits while a voice sounds"), W.IsPlaying());
		N2.Live.Reset();
		W.Tick();
		TestFalse(TEXT("then the slot stops"), W.IsPlaying());
	}

	{
		const uint8 Bytes[] = { 'M', 'I', 'D', 'x',
			0x00, 0xC0, 0x05,  0x00, 0xB0, 0x14, 0x00,  0x02, 0x90, 0x40, 0x64,  0x00, 0x80, 0x40, 0x00,
			0x01, 0xB0, 0x1E, 0x02,  0x00, 0xB0, 0x07, 0x40,  0x00, 0xF0, 0x2F, 0x00 };
		FSequence S;
		TestTrue(TEXT("synthetic parses"), S.Parse(MakeArrayView(Bytes, UE_ARRAY_COUNT(Bytes))));
		FPlayer P;
		int32 Step = 0;
		FNotes N;
		N.Bind(P, &Step);
		P.Start(S, 127);
		for (Step = 0; Step < 50 && P.IsPlaying(); ++Step) { P.Tick(); }
		TestEqual(TEXT("loop body x3 (the 0x1E count 2 jumps back twice), note every 3 ticks"), FString::Join(N.Log, TEXT("|")),
			FString(TEXT("on:0:5:64:100:126|off:2:0|on:3:5:64:100:126|off:5:1|on:6:5:64:100:126|off:8:2")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCrowdNotesTest, "SSX.Audio.Crowd.Notes", CrowdFlags)
bool FSSXCrowdNotesTest::RunTest(const FString&)
{
	using namespace SSXCrowdSeq;
	FLibRandom R;
	const uint32 R0 = R.Next(), R1 = R.Next(), R2v = R.Next();
	TestEqual(TEXT("0x2D6928 first outputs from its seed"), FString::Printf(TEXT("%08X %08X %08X"), R0, R1, R2v),
		FString(TEXT("559A51ED 274EA7F5 E827F7E0")));
	FLibRandom R2;
	TestEqual(TEXT("0x2DFF90(200) of the first draw: (0x51ED - 0x4000) x 200 >> 14"), R2.Spread(200), 56);

	FPatch P16, P18;
	P16.SetTag(0x06, 95); P16.SetTag(0x07, 62); P16.SetTag(0x0A, 7); P16.SetTag(0x11, 200);
	P18.SetTag(0x06, 95); P18.SetTag(0x07, 63); P18.SetTag(0x0A, 7); P18.SetTag(0x0E, 105); P18.SetTag(0x11, 200);
	FLibRandom Rng;
	FNote N;
	TestTrue(TEXT("#18 key 0x3E in zone"), ResolveNote(P18, 0x3E, 0x7F, Rng, N));
	TestEqual(TEXT("#18 key 0x3E: -100 cents + detune 56"), N.Cents, -44);
	TestEqual(TEXT("#18 volume 105 x 127 / 127"), N.Volume127, 105);
	TestEqual(TEXT("#18 bend range 7 semitones"), N.BendRangeCents, 700);
	FLibRandom Rng2;
	ResolveNote(P16, 0x3C, 0x5A, Rng2, N);
	TestEqual(TEXT("#16 key 0x3C vel 0x5A: -200 + 56 cents, volume 127 x 90 / 127"), FString::Printf(TEXT("%d %d"), N.Cents, N.Volume127),
		FString(TEXT("-144 90")));
	FPatch Z;
	Z.SetTag(0x03, 0x40); Z.SetTag(0x04, 0x48);
	TestFalse(TEXT("key below the zone (tag 3) starts nothing"), ResolveNote(Z, 0x3F, 0x7F, Rng, N));
	TestTrue(TEXT("default root 0x3C, no detune: key 0x48 = +1200"), ResolveNote(Z, 0x48, 0x7F, Rng, N) && N.Cents == 1200);
	// 0x2D5148
	TestEqual(TEXT("voice level: RAM voice (75, 115, 127, 34) -> 18"), VoiceLevel(75, 115, 127, 34), 18);
	TestEqual(TEXT("voice level: RAM voice (127, 112, 127, 34) -> 29"), VoiceLevel(127, 112, 127, 34), 29);
	TestEqual(TEXT("voice level: integer truncation, master 127"), VoiceLevel(105, 64), 52);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCrowdSystemTest, "SSX.Audio.Crowd.System", CrowdFlags)
bool FSSXCrowdSystemTest::RunTest(const FString&)
{
	FSSXCrowdSystem C;
	FCrowdLog L;
	L.Bind(C);
	C.Begin(1);
	if (!LoadCrowdConfig(C))
	{
		SSXTest::Skip(*this, TEXT("CROWD.INF / .eam missing (run tools/convert_audio.py --only crowd)"));
		return true;
	}
	const FSSXCrowdSystem::FRecord& R2 = C.GetRecord(2);
	TestEqual(TEXT("record 2 lists: Cheer30 / Ahh30 / Anticipation30"), C.GetSequences()[R2.Lists[0][0]].Name + TEXT(" ")
		+ C.GetSequences()[R2.Lists[1][0]].Name + TEXT(" ") + C.GetSequences()[R2.Lists[2][0]].Name, FString(TEXT("Cheer30 Ahh30 Anticipation30")));
	TestTrue(TEXT("ATTACK / DECAY / MAXBEND 1 / 4 / 32"), R2.Attack == 1.f && R2.Decay == 4.f && R2.MaxBend == 32.f);

	C.Cue(0, 0.7, false);
	TestEqual(TEXT("cheer without a crowd emitter: nothing"), L.Log.Num(), 0);
	TestEqual(TEXT("level 0"), C.Level(0), 0);

	const int32 A = C.AddEmitter(0, FVector3f(0, 0, 0), 100, 0);
	const int32 B = C.AddEmitter(0, FVector3f(1400, 0, 0), 40, 0);
	TestEqual(TEXT("handles = entry << 8 | listener"), FString::Printf(TEXT("%d %d"), A, B), FString(TEXT("0 256")));
	TestEqual(TEXT("one stream for index 0 (started by the first entry with its weight)"), FString::Join(L.Log, TEXT("|")), FString(TEXT("stream:0:100")));
	C.Frame();
	const FSSXCrowdSystem::FCrowd* Cr = C.GetCrowd(0);
	TestEqual(TEXT("volume = 100 + 40 >> 2"), Cr->Volume, 110);
	TestTrue(TEXT("position: t = 40 / 140 toward the second"), FMath::IsNearlyEqual(Cr->Position.X, 400.f, 0.01f));
	TestEqual(TEXT("the stream follows the volume"), L.Vol.FindRef(0), 110);
	const int32 D = C.AddEmitter(0, FVector3f(0, 1000, 0), 80, 2);
	C.Frame();
	TestEqual(TEXT("index 2 adds a second stream; voices 2"), FString::Printf(TEXT("%s %d"), *L.Log.Last(), Cr->Voices), FString(TEXT("stream:2:80 2")));
	TestEqual(TEXT("per record: #0 keeps 110, #2 80; the crowd 100 + 40/4 + 80/4"), FString::Printf(TEXT("%d %d %d"), L.Vol.FindRef(0), L.Vol.FindRef(1), Cr->Volume),
		FString(TEXT("110 80 130")));

	C.Cue(0, 0.7, false);
	TestEqual(TEXT("landing x 0.7 -> TRICK3 (Cheer30) at the crowd volume"), L.Log.Last(), FString(TEXT("seq:Cheer30:130")));
	TestEqual(TEXT("level 3 -> CrowdBox state (3 + 4) & 7 = 7"), FString::Printf(TEXT("%d %d"), C.Level(0), (C.Level(0) + 4) & 7), FString(TEXT("3 7")));
	const int32 Seq1 = L.Next - 1;
	C.Cue(0, 0.1, true);
	TestEqual(TEXT("bail x 0.1 stops the cheer, plays FALL1 (Ahh10)"), L.Log[L.Log.Num() - 2] + TEXT("|") + L.Log.Last(),
		FString::Printf(TEXT("stop:%d:0.00|seq:Ahh10:130"), Seq1));
	TestEqual(TEXT("groan level ~0 = -1 -> CrowdBox state 3"), FString::Printf(TEXT("%d %d"), C.Level(0), (C.Level(0) + 4) & 7), FString(TEXT("-1 3")));
	C.Cue(0, 0.0, false);
	TestEqual(TEXT("neutral (x <= 0) keeps the reaction and its level"), C.Level(0), -1);
	C.SetEmitterWeight(D, 120);
	C.Frame();
	const int32 Seq2 = L.Next - 1;
	TestEqual(TEXT("the reaction follows the crowd volume (120 + 100/4 + 40/4)"), L.Vol.FindRef(Seq2), 155);
	L.Live.Remove(Seq2);
	C.Frame();
	TestEqual(TEXT("ended reaction -> level 0"), C.Level(0), 0);

	C.Cue(0, 0.3, false);
	const int32 Seq3 = L.Next - 1;
	C.RemoveEmitter(D, 0.25f);
	TestEqual(TEXT("record #2 freed: stream stopped with 0.25 s, level 0"), FString::Printf(TEXT("%s %d"), *L.Log.Last(), C.Level(0)), FString(TEXT("stop:1:0.25 0")));
	C.RemoveEmitter(B, 0.25f);
	C.RemoveEmitter(A, 0.25f);
	TestEqual(TEXT("last entry: stream #0 stopped, the reaction keeps playing untracked"), L.Log.Last(), FString(TEXT("stop:0:0.25")));
	TestTrue(TEXT("crowd empty: volume 0, listener -1, no tracked reaction"), Cr->Count == 0 && Cr->Volume == 0 && Cr->Listener == -1 && Cr->Reaction == -1
		&& L.Live.Contains(Seq3));
	C.AddEmitter(0, FVector3f::ZeroVector, 50, 1);
	C.Cue(0, 0.3, false);
	const int32 Seq4 = L.Next - 1;
	C.RemoveEmitter(0, 0.f);
	TestFalse(TEXT("removal without fade stops the reaction"), L.Live.Contains(Seq4));
	TestEqual(TEXT("2D request (no position pointer) refused"), C.AddEmitter(0, FVector3f::ZeroVector, 50, 1, false), -1);

	// 0x2210A8 / 0x215370 / 0x20EA38 / 0x21FA1C
	TestEqual(TEXT("0x20EA38: time to landing > 2 s -> 3"), FSSXCrowdSystem::AnticipationLevel(2.1f, 0.6f), 3);
	TestEqual(TEXT("0x20EA38: no pending reward -> 3"), FSSXCrowdSystem::AnticipationLevel(1.f, 0.f), 3);
	TestEqual(TEXT("0x20EA38: levels 0 / 1 / 2 by 0.25 / 0.5"), FString::Printf(TEXT("%d %d %d %d"), FSSXCrowdSystem::AnticipationLevel(2.f, 0.1f),
		FSSXCrowdSystem::AnticipationLevel(1.f, 0.25f), FSSXCrowdSystem::AnticipationLevel(0.f, 0.49f), FSSXCrowdSystem::AnticipationLevel(0.f, 0.5f)),
		FString(TEXT("0 1 1 2")));
	int32 Q = 3;
	C.Anticipation = [&Q](int32) { return Q; };
	C.AddEmitter(0, FVector3f::ZeroVector, 60, 1);
	C.Arm(0);
	TestEqual(TEXT("armed"), Cr->ArmState, 1);
	const int32 Before = L.Log.Num();
	C.Frame();
	TestTrue(TEXT("query 3 (still high): nothing plays, stays armed"), L.Log.Num() == Before && Cr->ArmState == 1);
	Q = 2;
	C.Frame();
	TestEqual(TEXT("level 2: ANTICIPATE3 (Anticipation30) at the crowd volume, +0x180 = 2, +0x18C = 2"),
		FString::Printf(TEXT("%s %d %d"), *L.Log.Last(), Cr->ArmState, C.Level(0)), FString(TEXT("seq:Anticipation30:60 2 2")));
	const int32 Ant = L.Next - 1;
	Q = 0;
	C.Frame();
	TestTrue(TEXT("playing: no second start; level 0 not written to +0x18C"), L.Next - 1 == Ant && C.Level(0) == 2);
	C.Neutral(0);
	TestTrue(TEXT("neutral stops the playing anticipation (+0x180 == 2)"), !L.Live.Contains(Ant) && Cr->ArmState == 0);
	C.Arm(0);
	C.Arm(0);
	TestEqual(TEXT("arm only from 0"), Cr->ArmState, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCrowdAmbientTest, "SSX.Audio.Crowd.Ambient", CrowdFlags)
bool FSSXCrowdAmbientTest::RunTest(const FString&)
{
	FSSXPropSounds P;
	P.Reset();
	P.GridMinX = P.GridMinY = -50000.f;
	P.GridCell = 10000.f;
	P.GridNx = P.GridNy = 10;
	auto Shape = [](int32 Type, int32 Id, float R)
	{
		FSSXAmbientShape S;
		S.W[0] = uint32(Type);
		S.W[1] = uint32(Id);
		const float Rad = R, Kind = 2.f;
		FMemory::Memcpy(&S.W[5], &Rad, 4);
		FMemory::Memcpy(&S.W[6], &Kind, 4);
		return S;
	};
	FSSXAmbientRecord& R = P.AmbientRecords.AddDefaulted_GetRef();
	R.Instance = 7;
	R.Position = FVector3f(0, 0, 0);
	R.Shapes.Add(Shape(0, 0x62, 1000.f));
	R.Shapes.Add(Shape(3, 0x61, 1000.f));
	FSSXAmbientEmitters E;
	E.Build(&P);
	FSSXCrowdSystem C;
	FCrowdLog L;
	L.Bind(C);
	C.Begin(1);
	E.Crowd = &C;
	E.Query(FVector3f(500, 0, 0));
	E.Update();
	TestEqual(TEXT("sphere at half radius: crowd entry, stream #1 at 63"), FString::Join(L.Log, TEXT("|")), FString(TEXT("stream:1:63")));
	TestEqual(TEXT("2D shape refused (no position): its slot keeps no handle"), E.GetSlot(1).Handle, int32(INDEX_NONE));
	E.Query(FVector3f(250, 0, 0));
	E.Update();
	C.Frame();
	TestEqual(TEXT("the slot volume becomes the entry weight (95)"), C.GetCrowd(0)->Volume, 95);
	E.Query(FVector3f(5000, 0, 0));
	E.Update();
	TestEqual(TEXT("leaving: entry removed with the 0.25 s fade"), L.Log.Last(), FString(TEXT("stop:0:0.25")));
	TestEqual(TEXT("crowd empty"), C.GetCrowd(0)->Count, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCrowdChantTest, "SSX.Audio.Crowd.Chant", CrowdFlags)
bool FSSXCrowdChantTest::RunTest(const FString&)
{
	FSSXInfFile Inf;
	if (!Inf.Load(SSXAudio::ConfigPath(TEXT("CHANT.INF"))))
	{
		SSXTest::Skip(*this, TEXT("CHANT.INF missing"));
		return true;
	}
	FSSXChantBanks B;
	B.Load(Inf);
	TestEqual(TEXT("counts 4 / 12"), FString::Printf(TEXT("%d %d"), B.CharCount, B.GenCount), FString(TEXT("4 12")));
	TestEqual(TEXT("Mac (3) list"), FString::Join(B.Character[3], TEXT(" ")), FString(TEXT("c_mac_01 c_mac_02 c_mac_03 c_mac_04")));
	TestEqual(TEXT("generic list size"), B.General.Num(), 12);
	TArray<uint32> Draws;
	auto Rand = [&Draws]() { const uint32 V = Draws.Num() ? Draws[0] : 0u; if (Draws.Num()) { Draws.RemoveAt(0); } return V; };
	Draws = { 0x4000 };
	TestEqual(TEXT("rank 0: own list, 0x4000 x 4 / 0x7FFF = 2"), B.Pick(3, 0, Rand), FString(TEXT("c_mac_03")));
	Draws = { 0x7FFF };
	TestEqual(TEXT("draw reaching the count: no bank"), B.Pick(3, 1, Rand), FString());
	Draws = { 0x1000, 0x7000 };
	TestEqual(TEXT("rank 2: 0x1000 x 101 / 0x7FFF = 12 < 50 -> own list"), B.Pick(3, 2, Rand), FString(TEXT("c_mac_04")));
	Draws = { 0x7000, 0x2000 };
	TestEqual(TEXT("rank 3: 88 >= 50 -> generic 0x2000 x 12 / 0x7FFF = 3"), B.Pick(3, 3, Rand), B.General[3]);
	Draws = { 0 };
	TestEqual(TEXT("rank 4+: generic"), B.Pick(3, 5, Rand), B.General[0]);
	return true;
}

#endif
