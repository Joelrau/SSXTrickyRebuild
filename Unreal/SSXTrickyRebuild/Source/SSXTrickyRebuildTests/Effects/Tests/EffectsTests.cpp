#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Effects/TrackEffects.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXEffects;

namespace
{
	constexpr EAutomationTestFlags FxTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> LoadReference(FAutomationTestBase* Test)
	{
		static TSharedPtr<FJsonObject> Cached;
		if (Cached.IsValid())
		{
			return Cached;
		}
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("effects_reference.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			SSXTest::Skip(*Test, TEXT("skipped: no SimData/effects_reference.json (run tools/make_effects_reference.py)"));
			return nullptr;
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Cached) || !Cached.IsValid())
		{
			Test->AddError(TEXT("effects_reference.json does not parse"));
			return nullptr;
		}
		return Cached;
	}

	bool LoadTrack(FAutomationTestBase* Test, const FString& Stem, FEffectData& Out)
	{
		FString Err;
		if (!Out.Load(FEffectData::DefaultPath(Stem), &Err))
		{
			SSXTest::Skip(*Test, FString::Printf(TEXT("skipped %s: %s"), *Stem, *Err));
			return false;
		}
		return true;
	}

	TArray<int32> Ints(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		TArray<int32> Out;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O.IsValid() && O->TryGetArrayField(Field, A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A) { Out.Add(int32(V->AsNumber())); }
		}
		return Out;
	}

	TArray<int32> DeadSet(const FEffectWorld& W)
	{
		TArray<TPair<int32, const FController*>> C;
		W.GetControllers(C);
		TArray<int32> Out;
		for (const TPair<int32, const FController*>& P : C)
		{
			if (P.Value->Type == TypeDead) { Out.Add(P.Key); }
		}
		Out.Sort();
		return Out;
	}

	TArray<int32> RailBits(const FEffectWorld& W)
	{
		TArray<int32> Out;
		for (int32 K = 0; K < W.NumRails(); ++K) { Out.Add(W.IsRailEnabled(K) ? 1 : 0); }
		return Out;
	}

	int32 Milli(double V) { return int32(FMath::RoundHalfToEven(V * 1000.0)); }

	FString OutcomeString(const FOutcome& O)
	{
		switch (O.Kind)
		{
		case EOutcome::Node: return FString::Printf(TEXT("node %d %d"), O.Instance, O.A);
		case EOutcome::NodeRestart: return FString::Printf(TEXT("restart %d"), O.Instance);
		case EOutcome::Hide: return FString::Printf(TEXT("hide %d %s"), O.Instance, O.A == 4 ? TEXT("restore") : TEXT("dead"));
		case EOutcome::Message: return FString::Printf(TEXT("msg %d %d %d %d"), O.Instance, O.A, O.B, Milli(O.V));
		case EOutcome::Emitter:
			return O.A == 1 ? FString::Printf(TEXT("emit 1 %d %d"), O.Instance, O.B) : FString::Printf(TEXT("emit %d %d"), O.A, O.Instance);
		case EOutcome::Sound: return FString::Printf(TEXT("sound %d %d %d"), O.B, O.Instance, O.Extra);
		case EOutcome::Rail: return FString::Printf(TEXT("rail %d %d"), O.A, O.B);
		case EOutcome::Camera: return FString::Printf(TEXT("camera %d"), O.Instance);
		case EOutcome::RollerStop: return FString::Printf(TEXT("rollerstop %d"), O.Instance);
		case EOutcome::FenceWobble: return FString::Printf(TEXT("fence %d"), O.Instance);
		case EOutcome::Reset: return TEXT("rider reset");
		case EOutcome::BoostBurst: return FString::Printf(TEXT("rider boost_burst %d"), Milli(O.V));
		case EOutcome::LaunchBoost: return FString::Printf(TEXT("rider launch_boost %d"), Milli(O.V));
		case EOutcome::BoostSound: return TEXT("rider boost_sound");
		case EOutcome::LaunchSound: return TEXT("rider launch_sound");
		case EOutcome::Multiplier: return FString::Printf(TEXT("rider mult %d"), O.A);
		case EOutcome::MultiplierSound: return FString::Printf(TEXT("rider multiplier_sound %d"), Milli(O.V));
		case EOutcome::ShowOffPoints: return FString::Printf(TEXT("rider showoff_points %d"), O.A);
		case EOutcome::MeterReward: return FString::Printf(TEXT("rider meter_reward %d"), Milli(O.V));
		case EOutcome::AddClock: return FString::Printf(TEXT("rider add_clock %d"), Milli(O.V));
		case EOutcome::TimeSound: return FString::Printf(TEXT("rider time_sound %d"), Milli(O.V));
		case EOutcome::CameraShake: return TEXT("rider camera_shake");
		case EOutcome::Teleport: return FString::Printf(TEXT("rider teleport %d"), O.Instance);
		default: return FString();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEffectsFlowTest, "SSX.Effects.Flow", FxTestFlags)
bool FSSXEffectsFlowTest::RunTest(const FString&)
{
	const TCHAR* Expect[10] = { TEXT("RaceMode"), TEXT("FreerideMode"), TEXT("RaceMode"), TEXT("ShowoffMode"), TEXT("RaceMode"),
		TEXT("ShowoffMode"), TEXT("FreerideMode"), TEXT("RaceMode"), TEXT("FreerideMode"), TEXT("RaceMode") };
	for (int32 M = 0; M < 10; ++M)
	{
		TestEqual(FString::Printf(TEXT("mode script %d"), M), FString(ModeScript(M)), FString(Expect[M]));
		TestEqual(FString::Printf(TEXT("countdown mode %d"), M), HasCountdown(M, 0), M != 1 && M != 6 && M != 8);
		TestFalse(TEXT("no countdown on Untracked"), HasCountdown(M, 7));
	}
	TestEqual(TEXT("race PreRace"), FlowScripts(EFlowPhase::PreRace, 4, 0), TArray<FString>{ TEXT("RaceMode") });
	TestEqual(TEXT("race StartRace"), FlowScripts(EFlowPhase::StartRace, 4, 0).Num(), 0);
	TestEqual(TEXT("race countdown"), FlowScripts(EFlowPhase::CountdownEnter, 4, 0), TArray<FString>{ TEXT("StartCountdown"), TEXT("RaceMode") });
	TestEqual(TEXT("race countdown end"), FlowScripts(EFlowPhase::CountdownEnd, 4, 0), TArray<FString>{ TEXT("EndCountdown"), TEXT("RaceMode") });
	TestEqual(TEXT("freeride PreRace"), FlowScripts(EFlowPhase::PreRace, 1, 0), TArray<FString>{ TEXT("NoCountdown"), TEXT("FreerideMode") });
	TestEqual(TEXT("freeride StartRace"), FlowScripts(EFlowPhase::StartRace, 1, 0), TArray<FString>{ TEXT("NoCountdown"), TEXT("FreerideMode") });
	TestEqual(TEXT("Untracked race: no countdown"), FlowScripts(EFlowPhase::CountdownEnter, 4, 7).Num(), 0);
	TestEqual(TEXT("track ids"), TrackIdForStem(TEXT("gari")) * 100 + TrackIdForStem(TEXT("alaska")), 10);
	TestTrue(TEXT("snow: freeride, 1 human, 1 racer, draw below the chance"), SnowfallEnabled(1, 1, 1, 0.2, 0.25));
	TestFalse(TEXT("snow: draw above the chance"), SnowfallEnabled(1, 1, 1, 0.3, 0.25));
	TestFalse(TEXT("snow: never in modes 2 / 4"), SnowfallEnabled(4, 1, 1, 0.0, 1.0) || SnowfallEnabled(2, 1, 1, 0.0, 1.0));
	TestFalse(TEXT("snow: never with 3 racers"), SnowfallEnabled(3, 1, 3, 0.0, 1.0));
	TestFalse(TEXT("snow: never with 2 humans"), SnowfallEnabled(3, 2, 2, 0.0, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEffectsParseTest, "SSX.Effects.Parse", FxTestFlags)
bool FSSXEffectsParseTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(this);
	if (!Ref) { return true; }
	const TSharedPtr<FJsonObject> Parse = Ref->GetObjectField(TEXT("parse"));
	int32 Tables = 0, Clean = 0, Records = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Parse->Values)
	{
		const TSharedPtr<FJsonObject> R = P.Value->AsObject();
		FEffectData D;
		if (!LoadTrack(this, P.Key, D)) { continue; }
		++Tables;
		const TArray<FString> Errs = D.Validate();
		if (Errs.Num() == 0 && R->GetBoolField(TEXT("ok"))) { ++Clean; }
		else { AddError(FString::Printf(TEXT("%s: %s"), *P.Key, *FString::Join(Errs, TEXT("; ")))); }
		TestEqual(P.Key + TEXT(" instances"), D.NumInstances(), int32(R->GetNumberField(TEXT("n_inst"))));
		TestEqual(P.Key + TEXT(" events"), D.Events.Num() / NumSlots, int32(R->GetNumberField(TEXT("n_event"))));
		TestEqual(P.Key + TEXT(" effects"), D.Effects.Num(), int32(R->GetNumberField(TEXT("n_effect"))));
		TestEqual(P.Key + TEXT(" functions"), D.Functions.Num(), int32(R->GetNumberField(TEXT("n_func"))));
		TestEqual(P.Key + TEXT(" records"), D.Records.Num(), int32(R->GetNumberField(TEXT("n_record"))));
		TestEqual(P.Key + TEXT(" splines"), D.Splines.Num(), int32(R->GetNumberField(TEXT("n_spline"))));
		TMap<int32, int32> Hist;
		for (const FRecord& Rec : D.Records) { Hist.FindOrAdd(Rec.Type)++; }
		const TSharedPtr<FJsonObject> Types = R->GetObjectField(TEXT("types"));
		TestEqual(P.Key + TEXT(" record types"), Hist.Num(), Types->Values.Num());
		for (const TPair<FString, TSharedPtr<FJsonValue>>& T : Types->Values)
		{
			TestEqual(P.Key + TEXT(" type ") + T.Key, Hist.FindRef(FCString::Atoi(*T.Key)), int32(T.Value->AsNumber()));
		}
		Records += D.Records.Num();
	}
	AddInfo(FString::Printf(TEXT("%d tables, %d clean, %d records (expected 12 / 9059)"), Tables, Clean, Records));
	if (Tables == 12) { TestEqual(TEXT("records over all 12 tables"), Records, 9059); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEffectsModeScriptsTest, "SSX.Effects.ModeScripts", FxTestFlags)
bool FSSXEffectsModeScriptsTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(this);
	if (!Ref) { return true; }
	int32 Cases = 0, Same = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& T : Ref->GetObjectField(TEXT("modes"))->Values)
	{
		FEffectData D;
		if (!LoadTrack(this, T.Key, D)) { continue; }
		for (const TPair<FString, TSharedPtr<FJsonValue>>& M : T.Value->AsObject()->Values)
		{
			const TSharedPtr<FJsonObject> R = M.Value->AsObject();
			FEffectWorld W;
			W.Init(&D, FCString::Atoi(*M.Key), TrackIdForStem(T.Key));
			W.RunFlow(EFlowPhase::PreRace);
			bool bOk = DeadSet(W) == Ints(R, TEXT("dead")) && RailBits(W) == Ints(R, TEXT("rails"))
				&& W.NumScripts() == int32(R->GetNumberField(TEXT("scripts")));
			if (R->HasField(TEXT("flags")))
			{
				const TArray<int32> F = Ints(R, TEXT("flags"));
				for (int32 I = 0; I < F.Num() && bOk; ++I) { bOk = W.GetFlags(I) == F[I]; }
			}
			++Cases;
			Same += bOk;
			if (!bOk) { AddError(FString::Printf(TEXT("%s mode %s: state after the PreRace scripts differs from the model"), *T.Key, *M.Key)); }
		}
	}
	AddInfo(FString::Printf(TEXT("mode scripts: %d / %d track x mode cases = model"), Same, Cases));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEffectsRamTest, "SSX.Effects.Ram", FxTestFlags)
bool FSSXEffectsRamTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(this);
	FEffectData D;
	if (!Ref || !LoadTrack(this, TEXT("gari"), D)) { return true; }
	const TSharedPtr<FJsonObject> R = Ref->GetObjectField(TEXT("ram"));
	FEffectWorld W;
	W.Init(&D, int32(R->GetNumberField(TEXT("mode"))), int32(R->GetNumberField(TEXT("track"))));
	W.RunFlow(EFlowPhase::PreRace);
	const TArray<int32> RamDead = Ints(R, TEXT("dead"));
	const TArray<int32> Dead = DeadSet(W);
	int32 Common = 0, FlagsOk = 0;
	const TSharedPtr<FJsonObject> DeadFlags = R->GetObjectField(TEXT("dead_flags"));
	for (int32 I : Dead)
	{
		if (!RamDead.Contains(I)) { continue; }
		++Common;
		const uint32 Ram = uint32(DeadFlags->GetNumberField(FString::FromInt(I)));
		const uint16 Low = uint16(Ram & 0xFFFF), Ours = W.GetFlags(I);
		FlagsOk += (Low & 0xF2) == (Ours & 0xF2) && (Low & 0x4) == (Ours & 0x4) && (Low & 0x09) == (Ours & 0x09);
	}
	TestEqual(TEXT("cDeadNode set = RAM (instances)"), Dead, RamDead);
	TestEqual(TEXT("dead flag words fit RAM"), FlagsOk, RamDead.Num());
	const TArray<int32> RamRails = Ints(R, TEXT("rails"));
	const TArray<int32> Rails = RailBits(W);
	int32 RailSame = 0;
	for (int32 K = 0; K < FMath::Min(RamRails.Num(), Rails.Num()); ++K) { RailSame += RamRails[K] == Rails[K]; }
	TestEqual(TEXT("rail enable bits = RAM"), RailSame, RamRails.Num());
	FCellTracker Cells;
	Cells.Init(&D, IsShowOffMode(W.GetGameMode()));
	TArray<FVector2D> Pts;
	for (const TSharedPtr<FJsonValue>& V : R->GetArrayField(TEXT("points")))
	{
		const TArray<TSharedPtr<FJsonValue>>& P = V->AsArray();
		Pts.Add(FVector2D(P[0]->AsNumber(), P[1]->AsNumber()));
	}
	Cells.Step(W, Pts);
	W.RunFlow(EFlowPhase::CountdownEnter);
	TArray<TPair<int32, const FController*>> Ctl;
	W.GetControllers(Ctl);
	TMap<int32, int32> Ours;
	for (const TPair<int32, const FController*>& P : Ctl)
	{
		if (P.Value->Type != TypeDead) { Ours.Add(P.Key, P.Value->Type); }
	}
	const TSharedPtr<FJsonObject> RamCtl = R->GetObjectField(TEXT("ram_controllers"));
	int32 Match = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& P : RamCtl->Values)
	{
		const int32* T = Ours.Find(FCString::Atoi(*P.Key));
		Match += T && *T == int32(P.Value->AsNumber());
	}
	TestEqual(TEXT("active cells"), Cells.NumActive(), int32(R->GetNumberField(TEXT("active_cells"))));
	TestEqual(TEXT("controllers = RAM count"), Ours.Num(), RamCtl->Values.Num());
	TestEqual(TEXT("controllers = RAM (instance and type)"), Match, RamCtl->Values.Num());
	AddInfo(FString::Printf(TEXT("RAM: dead %d / %d (flags %d), rails %d / %d, active-cell controllers %d / %d (ours %d)"),
		Common, RamDead.Num(), FlagsOk, RailSame, RamRails.Num(), Match, RamCtl->Values.Num(), Ours.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEffectsLiveTest, "SSX.Effects.Live", FxTestFlags)
bool FSSXEffectsLiveTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(this);
	FEffectData D;
	if (!Ref || !LoadTrack(this, TEXT("gari"), D)) { return true; }
	for (const TPair<FString, TSharedPtr<FJsonValue>>& S : Ref->GetObjectField(TEXT("live"))->Values)
	{
		const TSharedPtr<FJsonObject> L = S.Value->AsObject();
		const int32 Frames = int32(L->GetNumberField(TEXT("frames"))), NR = int32(L->GetNumberField(TEXT("riders")));
		FEffectWorld W;
		W.Init(&D, 4, 0);
		W.bCrackedCreateDamage = false;
		W.CallFunction(TEXT("RaceMode"));
		FCellTracker Cells;
		Cells.Init(&D, false);
		TArray<FEffectRider> Riders;
		Riders.SetNum(NR);
		for (int32 K = 0; K < NR; ++K) { Riders[K].Slot = K; }
		struct FEv { int32 F, K, Op; double V; int32 Inst; };
		TArray<FEv> Ours;
		int32 CurFrame = 0, CurRider = 0, CurInst = -1;
		W.OnOutcome = [&](const FOutcome& O)
		{
			if (O.Kind == EOutcome::BoostBurst || O.Kind == EOutcome::LaunchBoost)
			{
				Ours.Add({ CurFrame, CurRider, O.Kind == EOutcome::BoostBurst ? 17 : 18, O.V, CurInst });
			}
		};
		const TArray<TSharedPtr<FJsonValue>>& Points = L->GetArrayField(TEXT("points"));
		const TArray<TSharedPtr<FJsonValue>>& Contacts = L->GetArrayField(TEXT("contacts"));
		int32 Ci = 0, NotCollidable = 0;
		TArray<FVector2D> Pts;
		for (int32 F = 0; F < Frames; ++F)
		{
			CurFrame = F;
			const TArray<TSharedPtr<FJsonValue>>& P = Points[F]->AsArray();
			Pts.Reset();
			for (int32 K = 0; K < NR; ++K) { Pts.Add(FVector2D(P[2 * K]->AsNumber(), P[2 * K + 1]->AsNumber())); }
			Cells.Step(W, Pts);
			while (Ci < Contacts.Num() && int32(Contacts[Ci]->AsArray()[0]->AsNumber()) == F)
			{
				const TArray<TSharedPtr<FJsonValue>>& C = Contacts[Ci++]->AsArray();
				CurRider = int32(C[1]->AsNumber());
				CurInst = int32(C[2]->AsNumber());
				NotCollidable += (W.GetFlags(CurInst) & FlCollidable) == 0;
				Riders[CurRider].Motion = int32(C[3]->AsNumber());
				W.Touch(CurInst, Riders[CurRider]);
			}
			W.Frame();
		}
		const TArray<TSharedPtr<FJsonValue>>& Pred = L->GetArrayField(TEXT("predicted"));
		int32 SameAsModel = 0;
		for (int32 K = 0; K < FMath::Min(Pred.Num(), Ours.Num()); ++K)
		{
			const TArray<TSharedPtr<FJsonValue>>& E = Pred[K]->AsArray();
			SameAsModel += int32(E[0]->AsNumber()) == Ours[K].F && int32(E[1]->AsNumber()) == Ours[K].K && int32(E[2]->AsNumber()) == Ours[K].Op
				&& FMath::IsNearlyEqual(E[3]->AsNumber(), Ours[K].V, 1e-6) && int32(E[4]->AsNumber()) == Ours[K].Inst;
		}
		const TArray<TSharedPtr<FJsonValue>>& Rec = L->GetArrayField(TEXT("recorded"));
		TArray<bool> Used;
		Used.Init(false, Rec.Num());
		int32 Matched = 0;
		for (const FEv& E : Ours)
		{
			for (int32 K = 0; K < Rec.Num(); ++K)
			{
				const TArray<TSharedPtr<FJsonValue>>& R = Rec[K]->AsArray();
				if (!Used[K] && int32(R[1]->AsNumber()) == E.K && int32(R[2]->AsNumber()) == E.Op
					&& FMath::Abs(int32(R[0]->AsNumber()) - E.F) <= 2 && FMath::Abs(R[3]->AsNumber() - E.V) < 0.1)
				{
					Used[K] = true;
					++Matched;
					break;
				}
			}
		}
		const TArray<int32> Dead = DeadSet(W);
		const TArray<int32> Fw = Ints(L, TEXT("fw_triggers"));
		int32 Fired = 0;
		for (int32 I : Fw) { Fired += Dead.Contains(I) && !Ints(Ref->GetObjectField(TEXT("ram")), TEXT("dead")).Contains(I); }
		TestEqual(S.Key + TEXT(": contacts on collidable instances"), NotCollidable, 0);
		TestEqual(S.Key + TEXT(": op 17 / 18 count = model"), Ours.Num(), Pred.Num());
		TestEqual(S.Key + TEXT(": op 17 / 18 = model"), SameAsModel, Pred.Num());
		TestEqual(S.Key + TEXT(": recorded timer rises predicted"), Matched, Rec.Num());
		TestEqual(S.Key + TEXT(": every prediction recorded"), Matched, Ours.Num());
		TestEqual(S.Key + TEXT(": fireworks triggers fired"), Fired, Fw.Num());
		TestEqual(S.Key + TEXT(": end dead set = model"), Dead, Ints(L, TEXT("dead_end")));
		TestEqual(S.Key + TEXT(": end controllers = model"), W.NumControllers(), int32(L->GetNumberField(TEXT("controllers_end"))));
		AddInfo(FString::Printf(TEXT("%s: %d frames, %d contacts; op 17/18 %d (model %d, same %d), recorded rises %d, matched %d; "
			"fireworks triggers fired %d / %d"), *S.Key, Frames, Contacts.Num(), Ours.Num(), Pred.Num(), SameAsModel, Rec.Num(),
			Matched, Fired, Fw.Num()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXEffectsScenariosTest, "SSX.Effects.Scenarios", FxTestFlags)
bool FSSXEffectsScenariosTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(this);
	if (!Ref) { return true; }
	int32 Cases = 0, Same = 0, Lines = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("scenarios")))
	{
		const TSharedPtr<FJsonObject> S = V->AsObject();
		const FString Stem = S->GetStringField(TEXT("track"));
		const int32 Mode = int32(S->GetNumberField(TEXT("mode")));
		FEffectData D;
		if (!LoadTrack(this, Stem, D)) { continue; }
		const int32 Tid = TrackIdForStem(Stem);
		FEffectWorld W;
		W.Init(&D, Mode, Tid);
		W.bCrackedCreateDamage = false;
		W.bCollideEmitterQuota = false;
		int32 RngI = 0;
		W.Rng = [&RngI]() { const double R = double((int64(RngI) * 7919) % 997) / 997.0; ++RngI; return R; };
		int32 Frame = 0;
		TArray<FString> Log;
		W.OnOutcome = [&](const FOutcome& O)
		{
			const FString Str = OutcomeString(O);
			if (!Str.IsEmpty()) { Log.Add(FString::Printf(TEXT("%d %s"), Frame, *Str)); }
		};
		W.RunFlow(EFlowPhase::PreRace);
		for (int32 I = 0; I < D.NumInstances(); ++I) { W.Activate(I); }
		W.RunFlow(EFlowPhase::CountdownEnter);
		const TArray<int32> Trig = Ints(S, TEXT("triggers"));
		FEffectRider Riders[2];
		Riders[0].Slot = 0; Riders[0].bHuman = true;
		Riders[1].Slot = 1; Riders[1].bHuman = false;
		for (int32 K = 0; K < Trig.Num(); ++K)
		{
			FEffectRider& R = Riders[K % 2];
			R.Motion = K % 3 == 0 ? 1 : 2;
			W.Touch(Trig[K], R, true, 0.5, 400.0);
			if (K >= 3) { W.Touch(Trig[K - 3], Riders[(K + 1) % 2], true, 0.5, 400.0); }
			++Frame;
			W.Frame();
		}
		W.RunFlow(EFlowPhase::CountdownEnd);
		for (int32 F = 0; F < 300; ++F) { ++Frame; W.Frame(); }
		for (int32 I = 0; I < D.NumInstances(); I += 2) { W.Deactivate(I); }
		for (int32 F = 0; F < 300; ++F) { ++Frame; W.Frame(); }

		TArray<FString> Model;
		for (const TSharedPtr<FJsonValue>& L : S->GetArrayField(TEXT("log"))) { Model.Add(L->AsString()); }
		int32 FirstDiff = INDEX_NONE;
		for (int32 K = 0; K < FMath::Max(Model.Num(), Log.Num()) && FirstDiff == INDEX_NONE; ++K)
		{
			if (!Model.IsValidIndex(K) || !Log.IsValidIndex(K) || Model[K] != Log[K]) { FirstDiff = K; }
		}
		const bool bOk = FirstDiff == INDEX_NONE && DeadSet(W) == Ints(S, TEXT("dead")) && RailBits(W) == Ints(S, TEXT("rails"))
			&& W.NumControllers() == int32(S->GetNumberField(TEXT("controllers"))) && W.NumScripts() == int32(S->GetNumberField(TEXT("scripts")));
		++Cases;
		Same += bOk;
		Lines += Log.Num();
		if (!bOk)
		{
			AddError(FString::Printf(TEXT("%s mode %d: first difference at outcome %d: ours '%s', model '%s' (%d / %d outcomes)"), *Stem, Mode,
				FirstDiff, Log.IsValidIndex(FirstDiff) ? *Log[FirstDiff] : TEXT("-"), Model.IsValidIndex(FirstDiff) ? *Model[FirstDiff] : TEXT("-"),
				Log.Num(), Model.Num()));
		}
	}
	AddInfo(FString::Printf(TEXT("scenarios: %d / %d identical to the model (%d outcomes)"), Same, Cases, Lines));
	return true;
}

#endif
