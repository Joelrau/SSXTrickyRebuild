// 0x140498 / 0x140688 / 0x140838 / 0x1408D0
#include "Effects/BoostVolumes.h"
#include "Effects/TrackEffects.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/TriggerEffects.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SSXBoostTests
{
	using namespace SSXEffects;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Report(FAutomationTestBase* T, const FString& Msg)
	{
		T->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Effects.Boost] %s"), *Msg);
	}

	bool IsBoostFamily(int32 Sub) { return Sub == TypeBoost || Sub == TypeLapBoost || Sub == TypeZBoost || Sub == TypeTubeEndBoost || Sub == TypeRandomBoost; }

	struct FFound { int32 Inst; int32 Slot; int32 Rec; };

	/** Factory records of the Boost family reachable from the instance event slots (op 7 chains followed: the node is made
	 *  on the op's target instance), as the audit scan. */
	void Scan(const FEffectData& D, TArray<FFound>& Out)
	{
		TSet<TPair<int32, int32>> Seen;
		TFunction<void(int32, int32, int32)> Walk = [&](int32 E, int32 Inst, int32 Slot)
		{
			if (E < 0 || E >= D.Effects.Num() || Seen.Contains({ E, Inst }))
			{
				return;
			}
			Seen.Add({ E, Inst });
			for (int32 K = D.Effects[E].X; K < D.Effects[E].X + D.Effects[E].Y; ++K)
			{
				const FRecord& R = D.Records[K];
				if (R.Type == 0 && IsBoostFamily(D.I(R, 0))) { Out.Add({ Inst, Slot, K }); }
				else if (R.Type == 7) { Walk(D.I(R, 1), D.I(R, 0), Slot); }
			}
		};
		for (int32 Inst = 0; Inst < D.NumInstances(); ++Inst)
		{
			for (int32 Slot = 0; Slot < NumSlots; ++Slot)
			{
				Seen.Reset();
				Walk(D.EffectFor(Inst, Slot), Inst, Slot);
			}
		}
	}

	FVector3f Dir3(const FEffectData& D, const FRecord& R, int32 K) { return FVector3f(D.F(R, K), D.F(R, K + 1), D.F(R, K + 2)); }

	struct FScriptedRider
	{
		FRiderSim Sim;
		FEffectRider R;
		explicit FScriptedRider(int32 Slot, bool bHuman) { R.Sim = &Sim; R.Slot = Slot; R.bHuman = bHuman; }
	};

	// 0x140838
	FVector Law(const FVector& V, const FVector3f& Dir, float Target, float Gain)
	{
		const FVector3f Vf(V);
		const float S = Target - (Vf.X * Dir.X + Vf.Y * Dir.Y + Vf.Z * Dir.Z);
		return S > 0.0f ? FVector(Vf + Dir * (S * Gain * 0.016666668f)) : V;
	}

	bool LoadTrack(FAutomationTestBase* T, const TCHAR* Stem, FEffectData& Data, TSharedPtr<FPatchWorld>* World = nullptr)
	{
		FString Err;
		if (!Data.Load(FEffectData::DefaultPath(Stem), &Err))
		{
			SSXTest::Skip(*T, FString::Printf(TEXT("%s: %s; skipped"), Stem, *Err));
			return false;
		}
		if (World)
		{
			*World = MakeShared<FPatchWorld>();
			if (!(*World)->LoadTrack(FPatchWorld::DefaultTrackPath(Stem), &Err) || (*World)->NumInstances() == 0)
			{
				SSXTest::Skip(*T, FString::Printf(TEXT("%s patches / instcol: %s; skipped"), Stem, *Err));
				return false;
			}
		}
		return true;
	}
}

using namespace SSXBoostTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoostTrackDataTest, "SSX.Effects.Boost.TrackData", SSXBoostTests::Flags)
bool FSSXBoostTrackDataTest::RunTest(const FString&)
{
	struct FExpect { const TCHAR* Stem; int32 Boost, Lap, Z, Tube; };
	static const FExpect Expect[] = {
		{ TEXT("alaska"), 7, 0, 0, 0 }, { TEXT("megaple"), 37, 1, 3, 1 }, { TEXT("merquer"), 18, 0, 0, 0 },
		{ TEXT("gari"), 0, 0, 0, 0 }, { TEXT("snow"), 0, 0, 0, 0 }, { TEXT("elysium"), 0, 0, 0, 0 }, { TEXT("mesa"), 0, 0, 0, 0 },
		{ TEXT("aloha"), 0, 0, 0, 0 }, { TEXT("pipe"), 0, 0, 0, 0 }, { TEXT("untrack"), 0, 0, 0, 0 }, { TEXT("trick"), 0, 0, 0, 0 },
		{ TEXT("ssxfe"), 0, 0, 0, 0 } };
	int32 Loaded = 0;
	for (const FExpect& E : Expect)
	{
		FEffectData D;
		if (!D.Load(FEffectData::DefaultPath(E.Stem)))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s.effects.bin missing"), E.Stem));
			continue;
		}
		++Loaded;
		TArray<FFound> F;
		Scan(D, F);
		int32 N[25] = {};
		int32 Mode1 = 0, NotSlot1 = 0, InCol = 0;
		TSharedRef<FPatchWorld> W = MakeShared<FPatchWorld>();
		const bool bCol = W->LoadTrack(FPatchWorld::DefaultTrackPath(E.Stem)) && W->NumInstances() > 0;
		for (const FFound& X : F)
		{
			const FRecord& R = D.Records[X.Rec];
			const int32 Sub = D.I(R, 0);
			++N[Sub];
			Mode1 += (Sub == TypeBoost || Sub == TypeTubeEndBoost) && D.S16(R, 0x0C) == 1;
			NotSlot1 += X.Slot != SlotTouch;
			const int32 K = bCol ? W->FindInstance(X.Inst) : INDEX_NONE;
			InCol += K != INDEX_NONE && W->GetInstance(K).Bounds.IsValid && W->GetInstance(K).NumNodes > 0;
		}
		Report(this, FString::Printf(TEXT("%s: Boost %d, LapBoost %d, ZBoost %d, TubeEndBoost %d, RandomBoost %d; mode 1: %d; all slot 1: %s; with collision + AABB: %d"),
			E.Stem, N[7], N[15], N[18], N[24], N[16], Mode1, NotSlot1 == 0 ? TEXT("yes") : TEXT("no"), InCol));
		TestEqual(FString::Printf(TEXT("%s Boost"), E.Stem), N[7], E.Boost);
		TestEqual(FString::Printf(TEXT("%s LapBoost"), E.Stem), N[15], E.Lap);
		TestEqual(FString::Printf(TEXT("%s ZBoost"), E.Stem), N[18], E.Z);
		TestEqual(FString::Printf(TEXT("%s TubeEndBoost"), E.Stem), N[24], E.Tube);
		TestEqual(FString::Printf(TEXT("%s RandomBoost (no retail record)"), E.Stem), N[16], 0);
		TestEqual(FString::Printf(TEXT("%s every Boost / TubeEnd record is mode 1"), E.Stem), Mode1, E.Boost + E.Tube);
		TestEqual(FString::Printf(TEXT("%s all made by the slot-1 touch effect"), E.Stem), NotSlot1, 0);
		if (bCol)
		{
			TestEqual(FString::Printf(TEXT("%s every volume has instance collision and an AABB"), E.Stem), InCol, F.Num());
		}
		else if (F.Num() > 0)
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s instcol missing: collision check skipped"), E.Stem));
		}

		auto Rec = [&](int32 Inst) -> const FRecord*
		{
			for (const FFound& X : F) { if (X.Inst == Inst) { return &D.Records[X.Rec]; } }
			return nullptr;
		};
		if (FCString::Stricmp(E.Stem, TEXT("alaska")) == 0)
		{
			static const FVector3f Dirs[7] = { { 0.981f, 0.f, 0.196f }, { 0.894f, 0.f, 0.447f }, { 0.781f, 0.f, 0.625f }, { 0.625f, 0.f, 0.781f },
				{ 0.6f, 0.f, 0.8f }, { 0.6f, 0.f, 0.8f }, { 0.6f, 0.f, 0.8f } };
			for (int32 K = 0; K < 7; ++K)
			{
				const FRecord* R = Rec(788 + K);
				if (!TestNotNull(FString::Printf(TEXT("alaska ForceWind instance %d"), 788 + K), R)) { continue; }
				TestTrue(FString::Printf(TEXT("ForceWind %d: mode 1, 0 s, gain %d, 45 m/s, dir"), 788 + K, K == 0 ? 3 : 4),
					D.S16(*R, 0x0C) == 1 && D.F(*R, 2) == 0.f && D.F(*R, 3) == (K == 0 ? 3.f : 4.f) && D.F(*R, 4) == 45.f
					&& Dir3(D, *R, 5).Equals(Dirs[K], 1e-3f));
			}
		}
		else if (FCString::Stricmp(E.Stem, TEXT("megaple")) == 0)
		{
			const FRecord* Lap = Rec(556);
			const FRecord* Z = Rec(622);
			const FRecord* Shaft = Rec(1212);
			const FRecord* Tube = Rec(531);
			const FRecord* Conv = Rec(102);
			if (TestNotNull(TEXT("Endboost_Lap_1000"), Lap))
			{
				TestTrue(TEXT("LapBoost: gain 5, 25 m/s, up"), D.F(*Lap, 1) == 5.f && D.F(*Lap, 2) == 25.f && Dir3(D, *Lap, 3).Equals(FVector3f(0, 0, 1)));
			}
			if (TestNotNull(TEXT("Endboost_Z_1000"), Z))
			{
				TestTrue(TEXT("ZBoost end: gain 4, 25 m/s, up, z top 0, range 400000"), D.F(*Z, 1) == 4.f && D.F(*Z, 2) == 25.f && D.F(*Z, 6) == 0.f && D.F(*Z, 7) == 400000.f);
			}
			if (TestNotNull(TEXT("twinAirShaft_BOOST_0"), Shaft))
			{
				TestTrue(TEXT("ZBoost shaft: gain 4, 20 m/s, z top -12000, range 0"), D.F(*Shaft, 1) == 4.f && D.F(*Shaft, 2) == 20.f && D.F(*Shaft, 6) == -12000.f && D.F(*Shaft, 7) == 0.f);
			}
			if (TestNotNull(TEXT("Endboost_End_1000"), Tube))
			{
				TestTrue(TEXT("TubeEnd: mode 1, gain 2, targets 27 / 35 / 35 m/s"), D.S16(*Tube, 0x0C) == 1 && D.F(*Tube, 3) == 2.f
					&& D.F(*Tube, 17) == 27.f && D.F(*Tube, 18) == 35.f && D.F(*Tube, 19) == 35.f && Dir3(D, *Tube, 8).Equals(FVector3f(0.159f, 0.954f, 0.254f), 1e-3f));
			}
			if (TestNotNull(TEXT("Conveyor_BOOST_Volume_11"), Conv))
			{
				TestTrue(TEXT("conveyor: mode 1, 3 s, gain 10, 60 m/s"), D.S16(*Conv, 0x0C) == 1 && D.F(*Conv, 2) == 3.f && D.F(*Conv, 3) == 10.f && D.F(*Conv, 4) == 60.f);
			}
		}
		else if (FCString::Stricmp(E.Stem, TEXT("merquer")) == 0)
		{
			const FRecord* B0 = Rec(3712);
			const FRecord* B1 = Rec(3711);
			if (TestNotNull(TEXT("Trigger_Boost_5000"), B0) && TestNotNull(TEXT("Trigger_Boost_5001"), B1))
			{
				TestTrue(TEXT("Trigger_Boost_5000: 5 s, gain 2, 175 m/s; 5001: 3 s, gain 1"), D.F(*B0, 2) == 5.f && D.F(*B0, 3) == 2.f && D.F(*B0, 4) == 175.f
					&& D.F(*B1, 2) == 3.f && D.F(*B1, 3) == 1.f && D.F(*B1, 4) == 175.f);
			}
		}
	}
	if (Loaded == 0)
	{
		SSXTest::Skip(*this, TEXT("no effects data: skipped"));
		return true;
	}

	FEffectData A;
	if (LoadTrack(this, TEXT("alaska"), A))
	{
		FEffectWorld W;
		W.Init(&A, 0, 10);
		FScriptedRider P(0, true);
		W.Racers.Add(&P.R);
		W.Touch(794, P.R);
		const FController* C = W.FindController(794);
		if (TestNotNull(TEXT("ForceWind touch makes a Boost node"), C) && TestEqual(TEXT("type 7"), C->Type, TypeBoost))
		{
			TestTrue(TEXT("node: mode 1, window 0 frames, target 4500 cm/s, gain 4, dir (0.6, 0, 0.8)"), C->Boost.Mode == 1 && C->Boost.Frames == 0
				&& C->Boost.Active == 0 && C->Boost.Target == 4500.f && C->Boost.Gain == 4.f && C->Boost.Dir.Equals(FVector3f(0.6f, 0.f, 0.8f), 1e-6f));
			const uint16 F0 = W.GetFlags(794);
			W.Touch(794, P.R);
			TestTrue(TEXT("touches are ignored while the node lives"), W.FindController(794) == C && W.GetFlags(794) == F0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoostLawTest, "SSX.Effects.Boost.Law", SSXBoostTests::Flags)
bool FSSXBoostLawTest::RunTest(const FString&)
{
	// 0x140838
	{
		FVector V(1200.0, 0.0, 1600.0);   // 2000 cm/s along (0.6, 0, 0.8)
		const bool bCh = SSXTriggers::BoostNodeVelocity(V, FVector3f(0.6f, 0.f, 0.8f), 4500.f, 4.f);
		const float Dv = (4500.f - 2000.f) * 4.f * 0.016666668f;
		TestTrue(TEXT("law: dv = dir (4500 - 2000) 4/60 = 166.67 along the wind"), bCh && V.Equals(FVector(1200.0 + 0.6 * Dv, 0.0, 1600.0 + 0.8 * Dv), 1e-2));
		FVector Fast(3000.0, 0.0, 4000.0);
		TestTrue(TEXT("law: no change above the target"), !SSXTriggers::BoostNodeVelocity(Fast, FVector3f(0.6f, 0.f, 0.8f), 4500.f, 4.f) && Fast == FVector(3000.0, 0.0, 4000.0));
	}
	FEffectData A;
	if (!LoadTrack(this, TEXT("alaska"), A))
	{
		return true;
	}
	FEffectWorld W;
	W.Init(&A, 0, 10);
	W.bKeepLog = true;
	FScriptedRider Human(0, true), Ai(1, false);
	W.Racers = { &Human.R, &Ai.R };
	bool bInside[2] = { true, true };
	W.BoostQueries.Pairs = [&](int32, const FEffectRider& R) { return bInside[R.Slot]; };
	W.BoostQueries.Inside = [&](int32, const FEffectRider& R) { return bInside[R.Slot]; };
	const FVector3f Dir(0.6f, 0.f, 0.8f);
	Human.Sim.State.Velocity = FVector(1200.0, 0.0, 1600.0);
	Ai.Sim.State.Velocity = FVector(2000.0, 300.0, -100.0);
	W.Touch(794, Human.R);
	TestTrue(TEXT("no push in the touching frame"), Human.Sim.State.Velocity == FVector(1200.0, 0.0, 1600.0));
	const FVector H0 = Human.Sim.State.Velocity, A0 = Ai.Sim.State.Velocity;
	W.Frame();
	TestTrue(TEXT("frame 1: the human inside gets the law"), Human.Sim.State.Velocity.Equals(Law(H0, Dir, 4500.f, 4.f), 1e-3));
	TestTrue(TEXT("frame 1: the AI racer inside gets the law too"), Ai.Sim.State.Velocity.Equals(Law(A0, Dir, 4500.f, 4.f), 1e-3)
		&& !Ai.Sim.State.Velocity.Equals(A0, 1.0));
	for (int32 F = 0; F < 59; ++F) { W.Frame(); }
	const double Along = FVector::DotProduct(Human.Sim.State.Velocity, FVector(Dir));
	Report(this, FString::Printf(TEXT("ForceWind 5000: 2000 cm/s along the wind -> %.1f cm/s after 1 s inside (target 4500, gain 4)"), Along));
	TestTrue(TEXT("1 s inside: 4500 - 2500 e^-4 (discrete) along the wind"), Along > 4400.0 && Along < 4500.0);
	TestNotNull(TEXT("node alive while a racer is inside"), W.FindController(794));
	bInside[0] = bInside[1] = false;
	const FVector Hv = Human.Sim.State.Velocity;
	W.Frame();
	TestTrue(TEXT("nobody inside: node destroyed (done 1), velocity untouched"), W.FindController(794) == nullptr && Human.Sim.State.Velocity == Hv);
	TestEqual(TEXT("flags back to the initial half | display"), int32(W.GetFlags(794) & ~FlCellActive), int32(A.InitialFlags(794) | FlDisplay));

	FEffectData Mq;
	if (LoadTrack(this, TEXT("merquer"), Mq))
	{
		FEffectWorld M;
		M.Init(&Mq, 0, 4);
		FScriptedRider P(0, true);
		M.Racers = { &P.R };
		bool bIn = false;
		M.BoostQueries.Pairs = [&](int32, const FEffectRider&) { return bIn; };
		M.BoostQueries.Inside = [&](int32, const FEffectRider&) { return bIn; };
		M.Touch(3712, P.R);
		const FController* C = M.FindController(3712);
		if (TestNotNull(TEXT("Trigger_Boost_5000 node"), C))
		{
			TestEqual(TEXT("window 300 frames at creation (mode 1)"), C->Boost.Active, 300);
			int32 Lived = 0;
			while (M.FindController(3712) && Lived < 1000) { M.Frame(); ++Lived; }
			TestEqual(TEXT("empty node ends after its 5 s window (frame 300)"), Lived, 300);
			M.Touch(3712, P.R);
			bIn = true;
			P.Sim.State.Velocity = FVector(1000.0, 0.0, 0.0);
			const FVector V0 = P.Sim.State.Velocity;
			const FVector3f D3712 = M.FindController(3712) ? M.FindController(3712)->Boost.Dir : FVector3f::ZeroVector;
			M.Frame();
			TestTrue(TEXT("Trigger_Boost_5000: law with gain 2 toward 175 m/s"), D3712.Equals(FVector3f(0.958f, 0.f, 0.287f), 1e-3f)
				&& P.Sim.State.Velocity.Equals(Law(V0, D3712, 17500.f, 2.f), 1e-3));
		}
		FEffectData Mp;
		if (LoadTrack(this, TEXT("megaple"), Mp))
		{
			FEffectWorld X;
			X.Init(&Mp, 0, 8);
			FScriptedRider Q(0, true);
			X.Racers = { &Q.R };
			X.BoostQueries.Pairs = [](int32, const FEffectRider&) { return false; };
			X.BoostQueries.Inside = [](int32, const FEffectRider&) { return false; };
			X.Touch(102, Q.R);
			if (TestNotNull(TEXT("Megaplex conveyor 102 node"), X.FindController(102)))
			{
				TestEqual(TEXT("conveyor window 180 frames"), X.FindController(102)->Boost.Active, 180);
				int32 F = 0;
				while (X.FindController(102) && F < 1000)
				{
					if (F == 100) { X.MessageInstance(102, 1, 0.f); }
					X.Frame();
					++F;
				}
				TestEqual(TEXT("message 1 at frame 100 re-arms the 3 s window: ends at frame 280"), F, 280);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoostZBoostTest, "SSX.Effects.Boost.ZBoost", SSXBoostTests::Flags)
bool FSSXBoostZBoostTest::RunTest(const FString&)
{
	FEffectData Mp;
	if (!LoadTrack(this, TEXT("megaple"), Mp))
	{
		return true;
	}
	FEffectWorld W;
	W.Init(&Mp, 0, 8);
	FScriptedRider P(0, true), Ai(3, false);
	W.Racers = { &P.R, &Ai.R };
	bool bIn[4] = { false, false, false, false };
	W.BoostQueries.Pairs = [&](int32, const FEffectRider& R) { return bIn[R.Slot]; };
	W.BoostQueries.Inside = [&](int32, const FEffectRider& R) { return bIn[R.Slot]; };
	P.Sim.State.Position = FVector(0.0, 0.0, -20000.0);
	P.Sim.State.Velocity = FVector(1500.0, -700.0, -300.0);
	Ai.Sim.State.Position = FVector(0.0, 0.0, -11000.0);
	Ai.Sim.State.Velocity = FVector(800.0, 0.0, 0.0);
	W.Touch(1212, P.R);
	bIn[0] = bIn[3] = true;
	W.Frame();
	const float Lift = (2000.f - (-300.f)) * 4.f * 0.016666668f;
	TestTrue(TEXT("shaft: horizontal velocity removed, vertical law (20 m/s, gain 4)"), P.Sim.State.Velocity.Equals(FVector(0.0, 0.0, -300.0 + Lift), 1e-2));
	TestTrue(TEXT("shaft: a racer inside above z top is not touched"), Ai.Sim.State.Velocity == FVector(800.0, 0.0, 0.0));
	bIn[0] = bIn[3] = false;
	P.Sim.State.Velocity.X = 900.0;
	W.Frame();
	TestTrue(TEXT("shaft: still lifted outside the volume while below z top (flag kept)"), P.Sim.State.Velocity.X == 0.0 && W.FindController(1212));
	P.Sim.State.Position.Z = -11999.0;
	W.Frame();
	TestTrue(TEXT("shaft: at z top the flag clears and nobody is lifted: done(1)"), W.FindController(1212) == nullptr);

	P.Sim.State.Position = FVector(100.0, 200.0, -5000.0);
	P.Sim.State.Velocity = FVector(100.0, 2000.0, -50.0);
	W.Touch(622, P.R);
	bIn[0] = true;
	W.Frame();
	TestTrue(TEXT("end tube: put at z top 0, velocity untouched"), P.Sim.State.Position.Equals(FVector(100.0, 200.0, 0.0)) && P.Sim.State.Velocity == FVector(100.0, 2000.0, -50.0));
	W.Frame();
	TestTrue(TEXT("end tube: at z top -> done(1)"), W.FindController(622) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoostLapBoostTest, "SSX.Effects.Boost.LapBoost", SSXBoostTests::Flags)
bool FSSXBoostLapBoostTest::RunTest(const FString&)
{
	FEffectData Mp;
	if (!LoadTrack(this, TEXT("megaple"), Mp))
	{
		return true;
	}
	const int32 Lap = 556, Tube = 531;
	const FBox Box(FVector(-500.0, -500.0, 0.0), FVector(500.0, 500.0, 400.0));
	auto Make = [&](FEffectWorld& W, TArray<FScriptedRider*> Riders, bool& bPair)
	{
		W.Init(&Mp, 8, 8);
		W.Racers.Reset();
		for (FScriptedRider* R : Riders) { W.Racers.Add(&R->R); }
		W.BoostQueries.Pairs = [&bPair](int32, const FEffectRider&) { return bPair; };
		W.BoostQueries.Inside = [&bPair](int32, const FEffectRider&) { return bPair; };
		W.BoostQueries.Bounds = [Box](int32, FBox& Out) { Out = Box; return true; };
	};
	{
		FEffectWorld W;
		FScriptedRider P(0, true);
		bool bPair = true;
		Make(W, { &P }, bPair);
		P.R.LapsLeft = 0;
		P.Sim.State.Position = FVector(100.0, 0.0, 100.0);
		P.Sim.State.Velocity = FVector(1000.0, 500.0, 0.0);
		W.Touch(Lap, P.R);
		for (int32 F = 0; F < 30; ++F) { W.Frame(); }
		TestTrue(TEXT("0 laps left: LapBoost inert (position and velocity untouched)"), P.Sim.State.Position == FVector(100.0, 0.0, 100.0)
			&& P.Sim.State.Velocity == FVector(1000.0, 500.0, 0.0));
		TestEqual(TEXT("0 laps left: lap copy -1"), int32(W.FindController(Lap)->Boost.LapCopy[0]), -1);
	}
	{
		FEffectWorld W;
		FScriptedRider P(0, true), Ai(1, false);
		bool bPair = true;
		Make(W, { &P, &Ai }, bPair);
		P.R.LapsLeft = 2;
		Ai.R.LapsLeft = 2;
		P.Sim.State.Position = FVector(300.0, 400.0, 100.0);
		P.Sim.State.Velocity = FVector(1000.0, 500.0, 0.0);
		Ai.Sim.State.Position = FVector(1300.0, 0.0, 100.0);
		Ai.Sim.State.Velocity = FVector(700.0, 0.0, 0.0);
		W.Touch(Lap, P.R);
		const FController* C = W.FindController(Lap);
		if (!TestNotNull(TEXT("LapBoost node"), C))
		{
			return true;
		}
		TestEqual(TEXT("lap copy at creation = laps left"), int32(C->Boost.LapCopy[0]), 2);
		W.Frame();
		FVector V = FVector(1000.0 * 0.92f, 500.0 * 0.92f, 0.0);
		V = Law(FVector(float(V.X), float(V.Y), 0.0), FVector3f(0, 0, 1), 2500.f, 5.f);
		TestTrue(TEXT("first frame: pulled 20 cm toward the centre and 10 up"), P.Sim.State.Position.Equals(FVector(300.0 - 12.0, 400.0 - 16.0, 110.0), 1e-3));
		TestTrue(TEXT("first frame: v_xy x 0.92, then the law up (25 m/s, gain 5)"), P.Sim.State.Velocity.Equals(V, 1e-2));
		TestTrue(TEXT("AI racer 1300 away: paired but not pulled"), Ai.Sim.State.Velocity == FVector(700.0, 0.0, 0.0));
		TestEqual(TEXT("lap copy one below the laps left"), int32(C->Boost.LapCopy[0]), 1);
		W.Frame();
		TestTrue(TEXT("second frame: no second pull of the position"), P.Sim.State.Position.Equals(FVector(288.0, 384.0, 110.0), 1e-3));
		P.R.LapsLeft = 1;
		const FVector V1 = P.Sim.State.Velocity;
		W.Frame();
		TestTrue(TEXT("laps left 1: acts (copy 0)"), !P.Sim.State.Velocity.Equals(V1, 1e-6) && C->Boost.LapCopy[0] == 0);
		P.R.LapsLeft = 0;
		const FVector V2 = P.Sim.State.Velocity;
		W.Frame();
		TestTrue(TEXT("laps left 0: copy -1, no more boost"), P.Sim.State.Velocity == V2 && C->Boost.LapCopy[0] == -1);
		bPair = false;
		W.Frame();
		TestEqual(TEXT("no pair: pull flag cleared"), int32(C->Boost.LapFlag[0]), 0);
		// 0x141190 / 0x141198
		W.Deactivate(Lap);
		TestNotNull(TEXT("LapBoost survives its cell"), W.FindController(Lap));
		W.NodeDone(Lap);
		TestNotNull(TEXT("LapBoost survives done(1)"), W.FindController(Lap));
	}
	{
		FEffectWorld W;
		FScriptedRider P(0, true), Q(1, false), R(2, false);
		bool bPair = true;
		Make(W, { &P, &Q, &R }, bPair);
		for (FScriptedRider* X : { &P, &Q, &R }) { X->R.LapsLeft = 3; }
		P.Sim.State.Position = FVector(0.0, 0.0, 500.0);
		const FMatrix& M = Mp.InstMatrix[Lap];
		const FVector Ax = FVector(M.M[0][0], M.M[0][1], 0.0);
		Q.Sim.State.Position = FVector(-Ax.X * 100.0, -Ax.Y * 100.0, 1200.0);
		R.Sim.State.Position = FVector(Ax.X * 100.0, Ax.Y * 100.0, 1200.0);
		W.Touch(Lap, P.R);
		W.Frame();
		TestTrue(TEXT("exit sides 0 / 1 / 2"), W.TubeEndSide[0] == 0 && W.TubeEndSide[1] == 1 && W.TubeEndSide[2] == 2);
		W.Touch(Tube, P.R);
		const FController* T = W.FindController(Tube);
		if (TestNotNull(TEXT("TubeEndBoost node"), T))
		{
			for (FScriptedRider* X : { &P, &Q, &R }) { X->Sim.State.Velocity = FVector::ZeroVector; }
			W.Frame();
			const int32 Sides[3] = { 0, 1, 2 };
			const float Targets[3] = { 2700.f, 3500.f, 3500.f };
			int32 Ok = 0;
			for (int32 K = 0; K < 3; ++K)
			{
				const FVector3f E = T->Boost.Exit[Sides[K]].GetSafeNormal();
				Ok += T->Boost.ExitDir[K].Equals(E, 1e-5f) && T->Boost.ExitTarget[Sides[K]] == Targets[K];
			}
			TestEqual(TEXT("TubeEnd: exit direction per racer = its side's, normalised and cached; targets 27 / 35 / 35 m/s"), Ok, 3);
			bPair = false;
			W.Frame();
			TestTrue(TEXT("TubeEnd: nobody inside -> done(1), cache gone with the node"), W.FindController(Tube) == nullptr);
		}
	}
	return true;
}

namespace SSXBoostTests
{
	/** A headless ride on a track's collision through its effects (no UWorld): node update (group 3), then the rider step
	 *  with PropCollision touches routed to the interpreter (group 8). */
	struct FRide
	{
		FEffectData Data;
		TSharedPtr<FPatchWorld> World;
		FEffectWorld Fx;
		FRiderSim Sim;
		FEffectRider R;
		int32 BoostFrames = 0;
		bool Setup(FAutomationTestBase* T, const TCHAR* Stem, int32 Track, bool bBoost)
		{
			if (!LoadTrack(T, Stem, Data, &World))
			{
				return false;
			}
			Fx.Init(&Data, 0, Track);
			Fx.OnFlagsChanged = [W = World](int32 Inst, uint16, uint16 New) { W->SetInstanceRunFlags(Inst, New); };
			if (bBoost) { BindBoostQueries(Fx, World); }
			SSXRider::FPlayerOptions O;
			SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(SSXRider::FRiderTable::Shared(), 10, O), Sim.Profile);
			SSXRiderSetup::RegisterStates(Sim);
			Sim.SetWorld(World.Get());
			R.Sim = &Sim;
			R.Slot = 0;
			R.bHuman = false;
			Fx.Racers = { &R };
			Sim.OnPropContact = [this](FRiderSim&, const SSXGround::FPropContactEvent& E)
			{
				Fx.Touch(E.Hit.InstanceIndex, R, true, FVector::DotProduct(E.Contact.Direction, E.Contact.Normal), E.Contact.Speed);
			};
			FRiderSphereSet Shape;
			if (!SSXRider::BuildSphereSet(Sim.Profile, false, FVector::ZeroVector, FQuat::Identity, -1, Shape))
			{
				SSXTest::Skip(*T, TEXT("no rider bones (SimData riders): skipped"));
				return false;
			}
			return true;
		}
		void Launch(const FVector& C, const FVector& V)
		{
			Sim.Reset();
			const FVector H = FVector(V.X, V.Y, 0.0).GetSafeNormal();
			Sim.SetOrientationFromAxes(H.IsNearlyZero() ? FVector(0, 1, 0) : H, FVector::UpVector);
			FRiderSphereSet Shape;
			SSXRider::BuildSphereSet(Sim.Profile, false, FVector::ZeroVector, Sim.State.Orientation, -1, Shape);
			Sim.State.Position = C - Shape.BoundCentre;
			Sim.State.Velocity = V;
			Sim.ApplyTransition(TEXT("T02"));
		}
		void Step(TConstArrayView<int32> Volumes)
		{
			const FVector V0 = Sim.State.Velocity;
			bool bAny = false;
			for (int32 I : Volumes) { bAny |= Fx.FindController(I) != nullptr; }
			Fx.Frame();
			BoostFrames += bAny && !Sim.State.Velocity.Equals(V0, 1e-6);
			Sim.Step(FRiderInputWord());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoostAlaskaRideTest, "SSX.Effects.Boost.AlaskaRide", SSXBoostTests::Flags)
bool FSSXBoostAlaskaRideTest::RunTest(const FString&)
{
	const int32 Wind[7] = { 794, 793, 792, 791, 790, 789, 788 };
	FRide On, Off;
	if (!On.Setup(this, TEXT("alaska"), 10, true) || !Off.Setup(this, TEXT("alaska"), 10, false))
	{
		return true;
	}
	FString Boxes;
	for (int32 I : Wind)
	{
		const int32 K = On.World->FindInstance(I);
		if (!TestTrue(FString::Printf(TEXT("ForceWind %d in the instance collision"), I), K != INDEX_NONE)) { return true; }
		const FSimInstance& S = On.World->GetInstance(K);
		Boxes += FString::Printf(TEXT(" %d: type %d flags 0x%04X centre (%.0f %.0f %.0f) size (%.0f %.0f %.0f);"), I, S.ModelType, S.RunFlags,
			S.Bounds.GetCenter().X, S.Bounds.GetCenter().Y, S.Bounds.GetCenter().Z, S.Bounds.GetSize().X, S.Bounds.GetSize().Y, S.Bounds.GetSize().Z);
	}
	Report(this, FString::Printf(TEXT("alaska wind volumes:%s"), *Boxes));
	// ride into the first volume at 2000 cm/s along its wind (0.6, 0, 0.8) from its centre, both with and without the nodes
	const FSimInstance& First = On.World->GetInstance(On.World->FindInstance(794));
	const FVector C = First.Bounds.GetCenter();
	const FVector V0 = FVector(1200.0, 0.0, 1600.0);
	On.Launch(C, V0);
	Off.Launch(C, V0);
	TestTrue(TEXT("the rider sphere set is inside volume 794 at the start"), RiderInsideInstance(*On.World, 794, On.Sim));
	int32 Created = 0;
	TSet<int32> Touched;
	for (int32 F = 0; F < 180; ++F)
	{
		On.Step(Wind);
		Off.Step(Wind);
		for (int32 I : Wind)
		{
			if (On.Fx.FindController(I)) { Touched.Add(I); }
		}
	}
	Created = Touched.Num();
	const double Dz = On.Sim.State.Position.Z - Off.Sim.State.Position.Z;
	const double Dx = On.Sim.State.Position.X - Off.Sim.State.Position.X;
	Report(this, FString::Printf(TEXT("alaska ride, 3 s from volume 794: %d volumes made a node, %d boosted frames; with the wind z %.0f x %.0f |v| %.0f, without z %.0f x %.0f |v| %.0f (+%.0f cm up, +%.0f cm along)"),
		Created, On.BoostFrames, On.Sim.State.Position.Z, On.Sim.State.Position.X, On.Sim.State.Velocity.Size(), Off.Sim.State.Position.Z, Off.Sim.State.Position.X,
		Off.Sim.State.Velocity.Size(), Dz, Dx));
	TestTrue(TEXT("the wind volume made its node on the touch"), Created >= 1);
	TestTrue(TEXT("the node pushed the rider on several frames"), On.BoostFrames >= 5);
	TestTrue(TEXT("pushed back into the air: higher than without the wind"), Dz > 50.0);
	TestEqual(TEXT("without the queries nothing is pushed"), Off.BoostFrames, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoostMerquryRideTest, "SSX.Effects.Boost.MerquryRide", SSXBoostTests::Flags)
bool FSSXBoostMerquryRideTest::RunTest(const FString&)
{
	FRide On;
	if (!On.Setup(this, TEXT("merquer"), 4, true))
	{
		return true;
	}
	for (const int32 Inst : { 4274, 3712 })
	{
		const int32 K = On.World->FindInstance(Inst);
		if (!TestTrue(FString::Printf(TEXT("instance %d in the instance collision"), Inst), K != INDEX_NONE))
		{
			return true;
		}
		const FSimInstance& S = On.World->GetInstance(K);
		const FVector Dir = Inst == 4274 ? FVector(0.865, -0.501, 0.0) : FVector(0.958, 0.0, 0.287);
		On.Fx.Init(&On.Data, 0, 4);
		On.Fx.Racers = { &On.R };
		On.Launch(S.Bounds.GetCenter(), Dir * 1500.0);
		const bool bInside = RiderInsideInstance(*On.World, Inst, On.Sim);
		const int32 Before = On.BoostFrames;
		int32 Alive = 0, Inside = 0;
		for (int32 F = 0; F < 400; ++F)
		{
			On.Step({ Inst });
			Alive += On.Fx.FindController(Inst) != nullptr;
			Inside += RiderInsideInstance(*On.World, Inst, On.Sim);
		}
		Report(this, FString::Printf(TEXT("merquer %d: start inside %d, %d boosted frames, %d frames inside, node alive %d frames; v %.0f (along %.0f)"),
			Inst, bInside ? 1 : 0, On.BoostFrames - Before, Inside, Alive, On.Sim.State.Velocity.Size(), FVector::DotProduct(On.Sim.State.Velocity, Dir)));
		TestTrue(FString::Printf(TEXT("%d: rider starts inside"), Inst), bInside);
		TestTrue(FString::Printf(TEXT("%d: boosted while inside"), Inst), On.BoostFrames - Before >= 1);
		if (Inst == 3712)
		{
			TestTrue(TEXT("Trigger_Boost_5000: the node outlives the rider's stay for its 5 s window"), Alive >= 299 && Alive > Inside);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrackMegaplexLaunchTest, "SSX.Track.MegaplexLaunch", SSXBoostTests::Flags)
bool FSSXTrackMegaplexLaunchTest::RunTest(const FString&)
{
	// 0x140B90 / 0x116AB8
	const int32 Lap = 556;
	FRaceTrackData Data, Gari;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("megaple")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("megaple race data: %s; skipped"), *Err));
		return true;
	}
	FRide On, Off;
	if (!On.Setup(this, TEXT("megaple"), 8, true) || !Off.Setup(this, TEXT("megaple"), 8, true))
	{
		return true;
	}
	TestEqual(TEXT("Megaplex track id 8"), Data.TrackId, 8);
	FRaceSession Session;
	FRaceConfig Cfg;
	if (!TestTrue(TEXT("session init"), Session.Init(Data, Cfg)))
	{
		return true;
	}
	Session.AddRacer(&On.Sim, true);
	Session.Start();
	Session.Tick();
	const FRacerRecord& Rec0 = Session.GetRacer(0);
	const double L = Session.GetLapLength();
	TestEqual(TEXT("4 laps left at the placement (+0x114)"), Session.GetLapsLeft(0), 4);
	TestTrue(TEXT("lap length = line path 0 distance to go at its start (136174.56)"), FMath::Abs(L - 136174.56) < 0.1);
	TestTrue(TEXT("lap mark +0x37C = line path 1 lap event: 42928 - 39316 = 3612"), FMath::Abs(Rec0.BestLapMark - 3612.0) < 1.0);
	TestTrue(TEXT("race total +0x380 = distance to go + 3 lap lengths"),
		FMath::Abs(Rec0.RaceTotal - double(float(Rec0.Line.DistToGo) + float(L) * 3.0f)) < 0.05);
	Report(this, FString::Printf(TEXT("megaplex placement: laps left %d, lap length %.2f, lap mark %.2f, to go %.1f, race total %.1f"),
		Session.GetLapsLeft(0), L, Rec0.BestLapMark, Rec0.Line.DistToGo, Rec0.RaceTotal));
	if (Gari.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err))
	{
		FRiderSim G;
		SSXRiderSetup::RegisterStates(G, true);
		FRaceSession GS;
		GS.Init(Gari, Cfg);
		GS.AddRacer(&G, false);
		GS.Start();
		GS.Tick();
		TestEqual(TEXT("Garibaldi: 0 laps left"), GS.GetLapsLeft(0), 0);
		TestTrue(TEXT("Garibaldi: race total +0x380 = the placement's distance to go +0x374"),
			GS.GetRacer(0).RaceTotal == GS.GetRacer(0).Line.DistToGo);
	}
	// (b) the lap line (internal 13 on line path 1, 3612 cm to go) in phase 4: laps left 4 -> 3
	Session.SetPhase(ERacePhase::Race);
	Session.Tick();
	FRacerRecord& Rec = Session.GetRacerMutable(0);
	const FRacePath& Line1 = Session.Paths().LinePaths[1];
	Rec.Line.SetLine(1);
	On.Sim.State.Position = Line1.PointAt(Line1.DistToGoAtStart - 3912.0);
	Rec.Line.Measure(Session.Paths(), On.Sim.State.Position);
	Rec.Line.Best = Rec.Line.DistToGo;
	On.Sim.State.Position = Line1.PointAt(Line1.DistToGoAtStart - 3312.0);
	Session.ClearCues();
	Session.TrackScan(0);
	TestEqual(TEXT("lap line crossed: 3 laps left"), Session.GetLapsLeft(0), 3);
	TestTrue(TEXT("lap cue"), Session.GetCues().ContainsByPredicate([](const FRaceCue& C) { return C.Kind == ERaceCueKind::Lap; }));
	TestTrue(TEXT("not finished"), !Rec.IsFinished());

	On.Fx.LapsLeftOf = [&Session](const FEffectRider&) { return Session.GetLapsLeft(0); };
	Off.Fx.LapsLeftOf = [](const FEffectRider&) { return 0; };
	const int32 K = On.World->FindInstance(Lap);
	if (!TestTrue(TEXT("Endboost_Lap 556 in the instance collision"), K != INDEX_NONE))
	{
		return true;
	}
	FBox Box(ForceInit);
	TestTrue(TEXT("Endboost_Lap AABB"), On.Fx.BoostQueries.Bounds && On.Fx.BoostQueries.Bounds(Lap, Box));
	const FVector C = Box.GetCenter();
	const FVector Start(C.X + 300.0, C.Y, Box.Min.Z + 400.0);
	const FVector V0(-1500.0, 0.0, 0.0);
	On.Launch(Start, V0);
	Off.Launch(Start, V0);
	On.Fx.Touch(Lap, On.R);
	Off.Fx.Touch(Lap, Off.R);
	const FController* N = On.Fx.FindController(Lap);
	if (!TestNotNull(TEXT("LapBoost node"), N) || !TestEqual(TEXT("type 15"), N->Type, TypeLapBoost))
	{
		return true;
	}
	TestEqual(TEXT("lap copy at creation = laps left 3"), int32(N->Boost.LapCopy[0]), 3);
	TestTrue(TEXT("the rider pairs with the node"), On.Fx.BoostQueries.Pairs(Lap, On.R));
	const FVector P0 = On.Sim.State.Position;
	On.Fx.Frame();
	Off.Fx.Frame();
	// 0x140B90
	const float Dx = float(C.X) - float(P0.X), Dy = float(C.Y) - float(P0.Y);
	const float D = FMath::Sqrt(Dx * Dx + Dy * Dy);
	const FVector ExpP = P0 + FVector(Dx / D * 20.0f, Dy / D * 20.0f, 10.0f);
	const FVector ExpV = Law(FVector(float(V0.X) * 0.92f, float(V0.Y) * 0.92f, 0.0), FVector3f(0, 0, 1), 2500.f, 5.f);
	TestTrue(TEXT("first frame: pulled 20 cm toward the centre and 10 cm up"), On.Sim.State.Position.Equals(ExpP, 1e-2));
	TestTrue(TEXT("first frame: v = (-1380, 0, 208.33)"), On.Sim.State.Velocity.Equals(ExpV, 1e-2) && FMath::Abs(ExpV.Z - 208.3333) < 1e-2);
	TestTrue(TEXT("lap copy one below the laps left"), N->Boost.LapCopy[0] == 2);
	TestTrue(TEXT("0 laps left (the port before the fix): inert"), Off.Sim.State.Velocity == V0 && Off.Sim.State.Position == P0);
	On.Sim.Step(FRiderInputWord());
	Off.Sim.Step(FRiderInputWord());
	int32 Acting = 1;
	for (int32 F = 1; F < 60; ++F)
	{
		const FVector Vb = On.Sim.State.Velocity;
		On.Fx.Frame();
		Acting += !On.Sim.State.Velocity.Equals(Vb, 1e-6);
		On.Sim.Step(FRiderInputWord());
		Off.Step({ Lap });
	}
	const double Dz = On.Sim.State.Position.Z - Off.Sim.State.Position.Z;
	Report(this, FString::Printf(TEXT("megaplex launch from (%.0f %.0f %.0f), box z %.0f..%.0f: %d acting frames; after 1 s z %.0f vz %.0f |vxy| %.0f; laps left 0: z %.0f vz %.0f (+%.0f cm)"),
		Start.X, Start.Y, Start.Z, Box.Min.Z, Box.Max.Z, Acting, On.Sim.State.Position.Z, On.Sim.State.Velocity.Z,
		FVector2D(On.Sim.State.Velocity).Size(), Off.Sim.State.Position.Z, Off.Sim.State.Velocity.Z, Dz));
	TestTrue(TEXT("launched: rising after 1 s"), On.Sim.State.Velocity.Z > 1000.0);
	TestTrue(TEXT("launched: higher than the same ride with 0 laps left"), Dz > 300.0);
	// 0x118C08 -> 0x197F08: a reset above the boost exit respawns on a candidate AI line (the snow run ahead), not on the
	// candidate-flag-0 line 60 the line follow picked during the launch
	for (int32 F = 0; F < 150; ++F)
	{
		On.Fx.Frame();
		On.Sim.Step(FRiderInputWord());
		Session.Tick();
	}
	const FRacerRecord& Rr = Session.GetRacer(0);
	const int32 LineBefore = Rr.Race.Path;
	const FVector Above = On.Sim.State.Position;
	On.Sim.ForcedReset(false, TEXT("test"));
	const uint32 Serial = On.Sim.State.RespawnSerial;
	for (int32 F = 0; On.Sim.State.RespawnSerial == Serial && F < 120; ++F)
	{
		On.Fx.Frame();
		On.Sim.Step(FRiderInputWord());
		Session.Tick();
	}
	const FRacePath& Picked = Session.Paths().AIPaths[Rr.Race.Path];
	Report(this, FString::Printf(TEXT("reset at (%.0f %.0f %.0f) on AI line %d: respawn on line %d (candidate %d) param %.0f at (%.0f %.0f %.0f)"),
		Above.X, Above.Y, Above.Z, LineBefore, Picked.Index, Picked.CandidateFlag, Rr.Race.Param,
		On.Sim.State.Position.X, On.Sim.State.Position.Y, On.Sim.State.Position.Z));
	TestEqual(TEXT("respawned"), On.Sim.State.RespawnSerial, Serial + 1);
	TestEqual(TEXT("respawn on a candidate AI line"), Picked.CandidateFlag, 1);
	TestTrue(TEXT("respawn ahead of the boost exit, within 50 m"), (On.Sim.State.Position - Above).Size() < 5000.0);

	// Showoff (.sop lines), PCSX2 RAM: a reset above the boost exit on line 20 at parameter 0 respawns at 2403, the end
	// of the line's AI105 zone over Mdl_EndholeCover_A_1000 (0x119220 -> 0x196758)
	FRide So;
	if (!So.Setup(this, TEXT("megaple"), 8, true))
	{
		return true;
	}
	FRaceSession Show;
	FRaceConfig ShowCfg;
	ShowCfg.Mode = ERaceEventMode::PracticeShowoff;
	Show.Init(Data, ShowCfg);
	Show.AddRacer(&So.Sim, true);
	Show.Start();
	Show.Tick();
	Show.SetPhase(ERacePhase::Race);
	Show.Tick();
	So.Fx.LapsLeftOf = [&Show](const FEffectRider&) { return Show.GetLapsLeft(0); };
	So.Launch(Start, V0);
	FRacerRecord& Sr = Show.GetRacerMutable(0);
	const FRacePath& Into = Show.Paths().AIPaths[25];
	Sr.Race.SetPath(25);
	Sr.Race.PlaceAt(Show.Paths(), Into.Length() - 100.0);
	So.Fx.Touch(Lap, So.R);
	// the RAM snapshot: (513 -2457 4588) on line 20 at parameter 0
	for (int32 F = 0; F < 400 && !(Sr.Race.Path == 20 && So.Sim.State.Position.Y > -2500.0); ++F)
	{
		So.Fx.Frame();
		So.Sim.Step(FRiderInputWord());
		Show.Tick();
	}
	TestTrue(TEXT("showoff: line 20 at parameter 0 above the boost exit"), Sr.Race.Path == 20 && Sr.Race.Param == 0.0);
	double ZoneEnd = -1.0;
	for (const FRacePathEvent& E : Show.Paths().AIPaths[20].Events)
	{
		if (E.Type == SSXRaceEvent::AI105 && E.Start <= 0.0 && 0.0 <= E.End) { ZoneEnd = E.End; }
	}
	TestTrue(TEXT("line 20 AI105 zone 0..2403"), FMath::Abs(ZoneEnd - 2403.0) < 1.0);
	So.Sim.ForcedReset(false, TEXT("test"));
	const uint32 ShowSerial = So.Sim.State.RespawnSerial;
	for (int32 F = 0; So.Sim.State.RespawnSerial == ShowSerial && F < 120; ++F)
	{
		So.Fx.Frame();
		So.Sim.Step(FRiderInputWord());
		Show.Tick();
	}
	Report(this, FString::Printf(TEXT("showoff reset: respawn on line %d param %.1f at (%.0f %.0f %.0f)"), Sr.Race.Path, Sr.Race.Param,
		So.Sim.State.Position.X, So.Sim.State.Position.Y, So.Sim.State.Position.Z));
	TestTrue(TEXT("showoff: respawn at the AI105 zone end (PCSX2: 2417 one frame later)"), Sr.Race.Path == 20 && FMath::Abs(Sr.Race.Param - ZoneEnd) < 30.0);
	return true;
}

#endif
