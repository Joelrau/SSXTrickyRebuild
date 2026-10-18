#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "World/PropControllers.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CtlTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
	const int32 RefFrames[] = { 1, 30, 60, 90, 120, 600 };

	TArray<uint8> FromHex(const FString& Hex)
	{
		TArray<uint8> Out;
		for (int32 I = 0; I + 1 < Hex.Len(); I += 2)
		{
			Out.Add(uint8(FParse::HexNumber(*Hex.Mid(I, 2))));
		}
		return Out;
	}

	FString SimData(const TCHAR* Name) { return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Name); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldControllersTest, "SSX.World.Controllers", CtlTestFlags)
bool FSSXWorldControllersTest::RunTest(const FString&)
{
	using namespace SSXProps;
	{
		FUVScroll S;
		S.Step = FVector2D(0.0, -0.0042);
		S.RunTime = 1.0;
		for (int32 F = 0; F < 60; ++F) { S.Tick(); }
		TestTrue(FString::Printf(TEXT("mode 0: 60 frames -> v = -0.252 (%.6f)"), S.Offset.Y), FMath::IsNearlyEqual(S.Offset.Y, -0.252, 1e-5));
		for (int32 F = 0; F < 240; ++F) { S.Tick(); }
		TestTrue(TEXT("offset wraps into [-1, 1] (one +1 step)"), FMath::Abs(S.Offset.Y) <= 1.0 && FMath::IsNearlyEqual(S.Offset.Y, -0.0042 * 300 + 1.0, 1e-4));
	}
	{
		FUVScroll S;   // 0x1422E0
		S.Mode = 2;
		S.Step = FVector2D(0.01, 0.0);
		S.RunTime = 0.5;
		S.PauseTime = 0.5;
		TArray<double> X;
		for (int32 F = 0; F < 120; ++F) { S.Tick(); X.Add(S.Offset.X); }
		TestTrue(FString::Printf(TEXT("mode 2: run +0.01 per frame (%.4f at frame 20)"), X[19]), FMath::IsNearlyEqual(X[19], 0.20, 1e-5));
		TestTrue(TEXT("mode 2: pause holds (frames 40..55)"), X[39] == X[54] && X[39] > 0.28);
		TestTrue(TEXT("mode 2: next run goes back (frame 80 < frame 60)"), X[79] < X[59] - 0.15);
	}
	{
		FUVScroll S;
		S.Step = FVector2D(0.1, 0.1);
		for (int32 F = 0; F < 10; ++F) { S.Tick(); }
		TestTrue(TEXT("A, B <= 0: still"), S.Offset.IsZero());
		FUVScroll D;   // 0x1422E0
		D.Step = FVector2D(0.0, 0.01);
		D.RunTime = 1.0;
		D.Delay = 0.5;
		for (int32 F = 0; F < 25; ++F) { D.Tick(); }
		const bool bMoving = FMath::IsNearlyEqual(D.Offset.Y, 0.25, 1e-5) && D.DoneCalls == 0;
		for (int32 F = 0; F < 10; ++F) { D.Tick(); }
		TestTrue(TEXT("the start delay does not hold the scroll"), bMoving);
		TestTrue(TEXT("its expiry frame: done(1), no step (35 frames -> 34 steps)"), D.DoneCalls == 1 && FMath::IsNearlyEqual(D.Offset.Y, 0.34, 1e-5));
	}

	FPropControllers Set;
	FPropController Scroll;
	Scroll.Type = EControllerType::UVScroll;
	Scroll.Instance = 7;
	Scroll.Scroll.Step = FVector2D(0.0, -0.0042);
	Scroll.Scroll.RunTime = 1.0;
	Scroll.Scroll.Delay = 100.0;
	FPropController Fence;
	Fence.Type = EControllerType::Fence;
	Fence.Instance = 8;
	FPropController Cracked;
	Cracked.Instance = 9;
	{
		uint8 Rec[20];
		const int32 W[3] = { 0, 20, 14 };
		const float F[2] = { -1.f, 5.f };
		FMemory::Memcpy(Rec, W, 12);
		FMemory::Memcpy(Rec + 12, F, 8);
		TestTrue(TEXT("cracked record parses (timer -60 frames, health 5, cooldown starts at 30)"),
			ParseCrackedRecord(Rec, 20, Cracked) && Cracked.Timer == -60 && Cracked.Health == 5.0 && Cracked.Cooldown == 30);
	}
	Set.Add(Scroll);
	Set.Add(Fence);
	Set.Add(Cracked);
	TArray<FIntPoint> Dispatched;
	Set.RunEffect = [&Dispatched](int32 Instance, int32 Slot, const SSXGround::FPropContactEvent*) { Dispatched.Add(FIntPoint(Instance, Slot)); };

	FRiderSim Sim;
	Set.Bind(Sim);
	auto Touch = [&Sim](int32 Instance, const FVector& V, const FVector& N)
	{
		SSXGround::FPropContactEvent E;
		E.Hit.InstanceIndex = Instance;
		E.Contact.Normal = N;
		E.Contact.Direction = V.GetSafeNormal();
		E.Contact.Speed = -FVector::DotProduct(N, V);
		Sim.OnPropContact(Sim, E);
	};
	const FVector V(0, 100, 0), N(0, -1, 0);   // head-on, a = 100 cm/s
	Touch(7, V, N);
	TestTrue(TEXT("scroll: touch timer starts at 30, first touch debounced, no slot 1"), Dispatched.Num() == 0 && Set.Find(7)->TouchTimer == 30);
	for (int32 F = 0; F < 30; ++F) { Set.Tick(); }
	Touch(7, V, N);
	TestTrue(TEXT("scroll: after 30 frames a touch runs slot 1 and re-arms the timer"),
		Dispatched.Num() == 1 && Dispatched[0] == FIntPoint(7, EventSlot::Touch) && Set.Find(7)->TouchTimer == 30);
	TestTrue(TEXT("scroll: a touch does not activate the node"), !Set.Find(7)->Scroll.bActive);
	TestTrue(TEXT("explicit start (message 1)"), Set.Activate(7) && Set.Find(7)->Scroll.bActive);
	const double Before = Set.Find(7)->Scroll.Offset.Y;
	Set.Tick();
	TestTrue(TEXT("the scroll runs from creation (its delay does not hold it, 0x1422E0)"),
		Before < 0.0 && FMath::IsNearlyEqual(Set.Find(7)->Scroll.Offset.Y - Before, -0.0042, 1e-5));
	Dispatched.Reset();
	Touch(8, V, N);
	TestTrue(FString::Printf(TEXT("fence wobble (n.v_hat) a 0.001 = -0.1 (%.4f), timers 4, no slot 1"), Set.Find(8)->Wobble),
		FMath::IsNearlyEqual(Set.Find(8)->Wobble, -0.1, 1e-12) && Set.Find(8)->FenceTimers[0] == 4 && Set.Find(8)->FenceTimers[1] == 4 && Dispatched.Num() == 0);
	Touch(9, V, N);
	TestTrue(FString::Printf(TEXT("cracked: health 5 - |(v_hat.n) a| 0.036 = 1.4 (%.3f), cooldown 30"), Set.Find(9)->Health),
		FMath::IsNearlyEqual(Set.Find(9)->Health, 1.4, 1e-9) && Set.Find(9)->Cooldown == 30);
	Touch(9, V, N);
	TestTrue(TEXT("cracked: no damage during the cooldown"), FMath::IsNearlyEqual(Set.Find(9)->Health, 1.4, 1e-9));
	for (int32 F = 0; F < 30; ++F) { Set.Tick(); }
	Touch(9, V, N);
	Set.Tick();
	TestTrue(TEXT("cracked: health <= 0 -> slot 4 dispatched once"), Set.Find(9)->bBroken && Dispatched.Num() == 1 && Dispatched[0] == FIntPoint(9, EventSlot::CounterOrHealthOut));
	Set.Tick();
	TestEqual(TEXT("cracked: slot 4 not repeated"), Dispatched.Num(), 1);
	SSXGround::FPropContactEvent None;
	None.Hit.InstanceIndex = 3;
	TestTrue(TEXT("no controller: slot 1 dispatched"), Set.OnContact(None) == EContactResult::NoController && Dispatched.Last() == FIntPoint(3, EventSlot::Touch));

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *SimData(TEXT("controller_reference.csv"))))
	{
		SSXTest::Skip(*this, TEXT("no SimData/controller_reference.csv (tools/make_controller_reference.py): reference part skipped"));
		return true;
	}
	int32 Records = 0, Matched = 0, Flips = 0, FlipsOk = 0;
	int32 Traces = 0, TracesOk = 0, FirstBad = -1;
	TMap<FString, TArray<FString>> Books;
	double MaxErr = 0.0;
	for (const FString& L : Lines)
	{
		if (L.IsEmpty() || L.StartsWith(TEXT("#")))
		{
			continue;
		}
		TArray<FString> C;
		L.ParseIntoArray(C, TEXT(","), false);
		if (C[0] == TEXT("texflip_trace") && C.Num() >= 5)
		{
			const TArray<uint8> Raw = FromHex(C[2]);
			FTexFlip T;
			if (!ParseTexFlipRecord(Raw.GetData(), Raw.Num(), T)) { continue; }
			int32 K = 0;
			T.Rng = [&K]() { ++K; return FMath::Fmod(K * 0.6180339887498949, 1.0); };
			TArray<int32> Tex;
			for (int32 I = 0; I < FCString::Atoi(*C[3]); ++I) { Tex.Add(100 + I); }
			T.Create(Tex);
			TArray<FString> Exp;
			C[4].ParseIntoArray(Exp, TEXT("."), true);
			bool bOk = Exp.Num() == 601 && T.Frame == FCString::Atoi(*Exp[0]) && T.TextureIndex() == 100 + T.Frame;
			for (int32 F = 1; F < Exp.Num() && bOk; ++F)
			{
				T.Tick();
				bOk &= T.Frame == FCString::Atoi(*Exp[F]);
				if (!bOk && FirstBad < 0) { FirstBad = Traces; AddInfo(FString::Printf(TEXT("TexFlip %s n %s: frame %d got %d expected %s"), *C[1], *C[3], F, T.Frame, *Exp[F])); }
			}
			++Traces;
			TracesOk += bOk;
			continue;
		}
		if (C[0] == TEXT("flipbook") && C.Num() >= 4)
		{
			Books.FindOrAdd(C[1]).Add(C[3]);
			continue;
		}
		if (C[0] == TEXT("texflip"))
		{
			const TArray<uint8> Raw = FromHex(C[3]);
			FTexFlip T;
			++Flips;
			FlipsOk += ParseTexFlipRecord(Raw.GetData(), Raw.Num(), T) && T.StartFrame == FCString::Atoi(*C[4])
				&& T.Direction == FCString::Atoi(*C[5]) && FMath::IsNearlyEqual(T.Fps, FCString::Atod(*C[6]), 1e-6)
				&& FMath::IsNearlyEqual(T.Delay, FCString::Atod(*C[7]), 1e-6) && T.Mode == FCString::Atoi(*C[8]);
			continue;
		}
		if (C.Num() < 3 + 2 * UE_ARRAY_COUNT(RefFrames))
		{
			continue;
		}
		const TArray<uint8> Raw = FromHex(C[2]);
		FUVScroll S;
		++Records;
		if (!ParseUVScrollRecord(Raw.GetData(), Raw.Num(), S))
		{
			AddError(FString::Printf(TEXT("%s @%s: record does not parse"), *C[0], *C[1]));
			continue;
		}
		bool bOk = true;
		int32 Frame = 0;
		for (int32 K = 0; K < UE_ARRAY_COUNT(RefFrames); ++K)
		{
			while (Frame < RefFrames[K]) { S.Tick(); ++Frame; }
			const double EU = FMath::Abs(S.Offset.X - FCString::Atod(*C[3 + 2 * K]));
			const double EV = FMath::Abs(S.Offset.Y - FCString::Atod(*C[4 + 2 * K]));
			MaxErr = FMath::Max(MaxErr, FMath::Max(EU, EV));
			bOk &= EU < 1e-9 && EV < 1e-9;
		}
		Matched += bOk;
	}
	AddInfo(FString::Printf(TEXT("UVScroll records vs models/controllers.py: %d / %d match at frames 1..600 (max |err| %.3g); "
		"TexFlip records parsed: %d / %d"), Matched, Records, MaxErr, FlipsOk, Flips));
	TestTrue(TEXT("UVScroll records found"), Records > 0);
	TestEqual(TEXT("every UVScroll record matches the reference"), Matched, Records);
	TestEqual(TEXT("every TexFlip record parses"), FlipsOk, Flips);
	AddInfo(FString::Printf(TEXT("TexFlip vs models/controllers.TexFlip: %d / %d traces (n = 2, 3, 5) match over 600 frames"), TracesOk, Traces));
	TestTrue(TEXT("TexFlip traces present"), Traces > 0);
	TestEqual(TEXT("every TexFlip trace matches the reference model"), TracesOk, Traces);
	int32 BooksOk = 0, BooksAll = 0;
	TArray<FString> Pbds;
	IFileManager::Get().FindFilesRecursive(Pbds, *FPaths::Combine(FPaths::ProjectDir(), TEXT("../../unpacked/MODELS")), TEXT("*.pbd"), true, false);
	for (const FString& Path : Pbds)
	{
		const FString Stem = FPaths::GetBaseFilename(Path);
		const TArray<FString>* Ref = Books.Find(Stem);
		if (!Ref) { continue; }
		TArray<uint8> Bytes;
		TArray<TArray<int32>> Got;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !ParsePbdFlipbooks(Bytes, Got)) { AddError(Stem + TEXT(": flipbooks do not parse")); continue; }
		for (int32 K = 0; K < Ref->Num(); ++K)
		{
			++BooksAll;
			BooksOk += Got.IsValidIndex(K) && FString::JoinBy(Got[K], TEXT("."), [](int32 V) { return FString::FromInt(V); }) == (*Ref)[K];
		}
	}
	AddInfo(FString::Printf(TEXT(".pbd texture flipbooks: %d / %d match the reference (n, texture ids)"), BooksOk, BooksAll));
	TestEqual(TEXT("every .pbd flipbook parses as the reference"), BooksOk, BooksAll);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldShowOffPatchesTest, "SSX.World.ShowOffPatches", CtlTestFlags)
bool FSSXWorldShowOffPatchesTest::RunTest(const FString&)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *SimData(TEXT("showoff_reference.csv"))))
	{
		SSXTest::Skip(*this, TEXT("no SimData/showoff_reference.csv (tools/make_controller_reference.py): skipped"));
		return true;
	}
	int32 Tracks = 0;
	for (const FString& L : Lines)
	{
		if (L.IsEmpty() || L.StartsWith(TEXT("#")))
		{
			continue;
		}
		TArray<FString> C;
		L.ParseIntoArray(C, TEXT(","), false);
		const FString Track = C[0];
		const int32 PbdPatches = FCString::Atoi(*C[1]), PbdShowOff = FCString::Atoi(*C[2]);
		FPatchWorld World;
		if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(Track)))
		{
			AddWarning(FString::Printf(TEXT("%s: no patches.bin"), *Track));
			continue;
		}
		++Tracks;
		int32 SimShowOff = 0, Probe = INDEX_NONE;
		for (int32 I = 0; I < World.NumPatches(); ++I)
		{
			if (World.GetPatch(I).Flags < 0)
			{
				++SimShowOff;
				Probe = Probe == INDEX_NONE ? I : Probe;
			}
		}
		int32 GltfShowOff = -1;
		FString Json;
		const FString Gltf = FPaths::Combine(FPaths::ProjectDir(), TEXT("../../converted/tracks"), Track, Track + TEXT(".gltf"));
		TSharedPtr<FJsonObject> Doc;
		if (FFileHelper::LoadFileToString(Json, *Gltf) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Doc) && Doc)
		{
			GltfShowOff = 0;
			const TArray<TSharedPtr<FJsonValue>>& Meshes = Doc->GetArrayField(TEXT("meshes"));
			const TArray<TSharedPtr<FJsonValue>>& Accessors = Doc->GetArrayField(TEXT("accessors"));
			for (const TSharedPtr<FJsonValue>& M : Meshes)
			{
				if (M->AsObject()->GetStringField(TEXT("name")) != Track + TEXT("_showoff_terrain"))
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& P : M->AsObject()->GetArrayField(TEXT("primitives")))
				{
					const int32 Idx = int32(P->AsObject()->GetNumberField(TEXT("indices")));
					GltfShowOff += int32(Accessors[Idx]->AsObject()->GetNumberField(TEXT("count"))) / (3 * 98);
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: ShowOff patches .pbd %d / sim %d / glTF %d (of %d)"), *Track, PbdShowOff, SimShowOff, GltfShowOff, PbdPatches));
		TestEqual(*FString::Printf(TEXT("%s: patch count"), *Track), World.NumPatches(), PbdPatches);
		TestEqual(*FString::Printf(TEXT("%s: sim ShowOff patches = .pbd"), *Track), SimShowOff, PbdShowOff);
		if (GltfShowOff >= 0)
		{
			TestEqual(*FString::Printf(TEXT("%s: glTF ShowOff patches = .pbd"), *Track), GltfShowOff, PbdShowOff);
		}
		if (Probe != INDEX_NONE)
		{
			const FSimPatch& P = World.GetPatch(Probe);
			// 0x23B6B0
			FVector C0, Du, Dv;
			P.Evaluate(0.47, 0.52, C0, Du, Dv);
			const FVector Nrm = FVector::CrossProduct(Du, Dv).GetSafeNormal();
			auto Hits = [&](bool bMode)
			{
				World.bAlternateSetMode = bMode;
				FRiderWorldHit H;
				return World.LineQuery(C0 + 50.0 * Nrm, C0 - 50.0 * Nrm, ERiderLineQuery::Fine, 0.5, H) && (H.PatchIndex == Probe || H.PatchIndex == World.GetPatch(Probe).SourceIndex);
			};
			const bool bOff = Hits(false), bOn = Hits(true);
			TestTrue(*FString::Printf(TEXT("%s: ShowOff patch %d hit only in modes 3/5 (off %d, on %d)"), *Track, P.SourceIndex, bOff, bOn), !bOff && bOn);
		}
	}
	TestTrue(TEXT("tracks checked"), Tracks > 0);
	return true;
}

#endif
