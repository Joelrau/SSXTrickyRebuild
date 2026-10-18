#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/PreRaceIntro.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags IntroTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	class FScriptEnv final : public SSXIntro::IEnv
	{
	public:
		int32 Frame = 0, Busy = 0, Left = 0, LoadingFrames = 0;
		bool bHelp = false;
		TSet<int32> Skips;
		TMap<int32, int32> Keys;
		TArray<FString> Log;
		virtual bool Loading() const override { return Frame < LoadingFrames; }
		virtual bool DirectorRunning() const override { return Left > 0; }
		virtual bool SkipPressed() override { return Skips.Remove(Frame) > 0; }
		virtual int32 LineKey(int32 LineId) const override { const int32* K = Keys.Find(LineId); return K ? *K : LineId; }
		virtual bool HelpNeeded() const override { return bHelp; }
		virtual void Act(const FString& Name, int32 Arg, const FString& Text) override
		{
			if (Name == TEXT("slot_start")) return;   // 0x1699A0
			if (Name == TEXT("ambient_enable")) return;   // 0x22E400
			if (Name == TEXT("director_play") && Text != TEXT("PreLoad")) Left = Busy;
			if (Name == TEXT("director_reset")) Left = 0;
			Log.Add(FString::Printf(TEXT("%s:%d:%s"), *Name, Arg, *Text));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceIntroTest, "SSX.Race.Intro", IntroTestFlags)
bool FSSXRaceIntroTest::RunTest(const FString&)
{
	const SSXIntro::FTimeline T = SSXIntro::CountdownTimeline(211);
	TestEqual(TEXT("session 2 beeps"), T.Beeps, TArray<int32>{ 240, 270, 300, 330 });
	TestEqual(TEXT("session 2 GO"), T.Go, 360);
	TestEqual(TEXT("session 2 race enter"), T.RaceEnter, 361);
	TestEqual(TEXT("clock at frame 279 = -162"), SSXIntro::ClockStart + SSXIntro::ClockStep * (279 - 211 + 1), -162);
	TestTrue(TEXT("light offsets 60 / 90 / 120 / 150"), T.LightOffsets[0] == 60 && T.LightOffsets[1] == 90 && T.LightOffsets[2] == 120 && T.LightOffsets[3] == 150);
	TestEqual(TEXT("Null script"), SSXIntro::ScriptName(0x39), FString(TEXT("Null")));
	TestEqual(TEXT("GAT_FO_1"), SSXIntro::ScriptName(21), FString(TEXT("GAT_FO_1")));

	FString Text;
	TSharedPtr<FJsonObject> Ref;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("race_intro_reference.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ref))
	{
		SSXTest::Skip(*this, TEXT("SimData/race_intro_reference.json missing (tools/make_race_intro_reference.py); model checks skipped"));
		return true;
	}
	const TArray<TSharedPtr<FJsonValue>>& Names = Ref->GetArrayField(TEXT("script_names"));
	int32 NameOk = 0;
	for (int32 I = 0; I < Names.Num(); ++I) NameOk += SSXIntro::ScriptName(I) == Names[I]->AsString() ? 1 : 0;
	TestEqual(TEXT("director script names == model"), NameOk, Names.Num());
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("countdown")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const SSXIntro::FTimeline C = SSXIntro::CountdownTimeline(int32(O->GetNumberField(TEXT("enter"))));
		TArray<int32> B;
		for (const TSharedPtr<FJsonValue>& X : O->GetArrayField(TEXT("beeps"))) B.Add(int32(X->AsNumber()));
		TestEqual(TEXT("beeps == model"), C.Beeps, B);
		TestEqual(TEXT("GO == model"), C.Go, int32(O->GetNumberField(TEXT("go"))));
	}
	int32 Runs = 0, RunOk = 0, Updates = 0, UpdateOk = 0, Done = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("runs")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		FScriptEnv Env;
		Env.Busy = int32(C->GetNumberField(TEXT("busy")));
		Env.LoadingFrames = int32(C->GetNumberField(TEXT("loading")));
		Env.bHelp = C->GetBoolField(TEXT("help"));
		for (const TSharedPtr<FJsonValue>& X : C->GetArrayField(TEXT("skips"))) Env.Skips.Add(int32(X->AsNumber()));
		for (const TPair<FString, TSharedPtr<FJsonValue>>& K : C->GetObjectField(TEXT("keys"))->Values) Env.Keys.Add(FCString::Atoi(*K.Key), int32(K.Value->AsNumber()));
		SSXIntro::FSlot Slots[3];
		const TArray<TSharedPtr<FJsonValue>>& Types = C->GetArrayField(TEXT("types"));
		const TArray<TSharedPtr<FJsonValue>>& Lines = C->GetArrayField(TEXT("lines"));
		for (int32 K = 0; K < 3; ++K)
		{
			Slots[K].Type = int32(Types[K]->AsNumber());
			for (const TSharedPtr<FJsonValue>& L : Lines[K]->AsArray())
			{
				const TArray<TSharedPtr<FJsonValue>>& A = L->AsArray();
				Slots[K].Lines.Add({ int32(A[0]->AsNumber()), int32(A[1]->AsNumber()), int32(A[2]->AsNumber()), int32(A[3]->AsNumber()) });
			}
		}
		const FString E = C->GetStringField(TEXT("entry"));
		const SSXIntro::EEntry Entry = E == TEXT("normal") ? SSXIntro::EEntry::Normal : (E == TEXT("next_heat") ? SSXIntro::EEntry::NextHeat : SSXIntro::EEntry::Restart);
		SSXIntro::FHandler H;
		H.Enter(Env, int32(C->GetNumberField(TEXT("mode"))), int32(C->GetNumberField(TEXT("venue"))), Slots, Entry);
		const TArray<TSharedPtr<FJsonValue>>& Frames = C->GetArrayField(TEXT("frames"));
		bool bRun = true;
		for (int32 F = 0; F < Frames.Num(); ++F)
		{
			const TSharedPtr<FJsonObject> W = Frames[F]->AsObject();
			if (F > 0)
			{
				Env.Frame = F;
				Env.Log.Reset();
				if (Env.Left > 0) --Env.Left;
				H.Update(Env);
			}
			TArray<FString> WL;
			for (const TSharedPtr<FJsonValue>& X : W->GetArrayField(TEXT("log"))) WL.Add(X->AsString());
			const bool bOk = H.GetStep() == int32(W->GetNumberField(TEXT("step"))) && Env.Log == WL;
			++Updates;
			UpdateOk += bOk ? 1 : 0;
			if (!bOk && bRun)
			{
				AddError(FString::Printf(TEXT("run %d (%s, mode %d) update %d: step %d vs %d"), Runs, *E, int32(C->GetNumberField(TEXT("mode"))), F,
					H.GetStep(), int32(W->GetNumberField(TEXT("step")))));
			}
			bRun &= bOk;
		}
		++Runs;
		RunOk += bRun ? 1 : 0;
		Done += H.IsDone() ? 1 : 0;
	}
	TestEqual(TEXT("intro runs == model"), RunOk, Runs);
	AddInfo(FString::Printf(TEXT("pre-race intro: %d/%d runs, %d/%d updates (step + actions) equal models/race_intro.py; %d runs reach step 0x15; countdown enter 211 -> beeps 240/270/300/330, GO 360"),
		RunOk, Runs, UpdateOk, Updates, Done));
	return true;
}

#endif
