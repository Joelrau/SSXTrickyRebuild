// 0x11B1F8
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXTrickyAudio.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/RiderSim.h"
#include "UI/SSXHudData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TrickyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
	using SSXTrickyAudio::ECmd;
	constexpr double FrameMs = 1000.0 / 60.0;

	FSSXTrickyPhraseContext SongCtx()
	{
		FSSXTrickyPhraseContext C;
		C.bSongLoaded = true;
		C.Mode = 4;
		C.BeatMs = 60000.0 / 142.0;
		C.MsToNextBeat = 100.0;
		C.LastPhraseBase = 80;
		return C;
	}

	FString Label(const SSXTrickyAudio::FCmd& C)
	{
		switch (C.Kind)
		{
		case ECmd::TrickyBank: return FString::Printf(TEXT("tricky.bnk #%d (class 6)"), C.Value);
		case ECmd::Speech: return TEXT("MC 0x204C (class 19 gate; not mode 6)");
		default: return FString();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioTrickyTest, "SSX.Audio.Tricky", TrickyTestFlags)
bool FSSXAudioTrickyTest::RunTest(const FString& Parameters)
{
	{
		FSSXTrickyAudio T;
		T.MeterFull(0.0, 20.0, 0, SongCtx());
		TestTrue(TEXT("fresh activation: MC 0x204C at once"), T.Out.Num() == 1 && T.Out[0].Kind == ECmd::Speech && T.Out[0].Value == 0x204C);
		TestTrue(TEXT("zbxsfx #107 deferred"), T.NumDeferred() == 1 && T.bPhraseOn && T.bFlagEnd && T.bFlagWarn && T.bFlagUber);
		T.Out.Reset();
		double Timer = 20.0;
		int32 F107 = -1, FWarn = -1, FEnd = -1, FStop = -1, FirstSlice = -1;
		TArray<int32> Slices;
		for (int32 K = 0; K < 1300; ++K)
		{
			Timer = FRiderSim::TrickyTimerStep(Timer, false);
			T.Frame(Timer, 0);
			T.Advance(FrameMs);
			for (const SSXTrickyAudio::FCmd& C : T.Out)
			{
				if (C.Kind == ECmd::Zbxsfx && C.Value == 107) F107 = K;
				if (C.Kind == ECmd::TrickyBank && C.Value == 1) FWarn = K;
				if (C.Kind == ECmd::TrickyBank && C.Value == 2) FEnd = K;
				if (C.Kind == ECmd::PhraseStop) FStop = K;
				if (C.Kind == ECmd::PhraseSlice) { Slices.Add(C.Value); if (FirstSlice < 0) FirstSlice = K; }
			}
			T.Out.Reset();
		}
		AddInfo(FString::Printf(TEXT("#107 at frame %d (%.0f ms), first slice at %d, phrase stop %d, #1 at %d, #2 at %d"), F107, (F107 + 1) * FrameMs,
			FirstSlice, FStop, FWarn, FEnd));
		TestTrue(TEXT("#107 at +500 ms (frame 29: 30 * 16.67 ms)"), F107 == 29 || F107 == 30);
		TestEqual(TEXT("#1 (2.7 s left) at frame 1037 as the model"), FWarn, 1037);
		TestEqual(TEXT("#2 when the timer reaches 0 (frame 1199)"), FEnd, 1199);
		const TArray<int32> Want = { 80, 81, 82, 83, 84, 85, 86, 87, 80, 81, 82, 83, 84, 85, 86, 87 };
		TestTrue(TEXT("last phrase 80..87 twice"), Slices == Want);
		TestTrue(TEXT("first slice on the next beat (100 ms -> frame 5)"), FirstSlice == 5);
		TestTrue(TEXT("phrase stopped at its second end (16 beats at 142 BPM = 6.76 s)"), FStop > 400 && FStop < 420);
	}
	{
		FSSXTrickyAudio T;
		T.MeterFull(0.0, 20.0, 0, SongCtx());
		T.Out.Reset();
		T.MeterFull(12.0, 20.0, 0, SongCtx());
		TestTrue(TEXT("refill: no MC, a second #107 queued"), T.Out.Num() == 0 && T.NumDeferred() == 2);
		T.Frame(19.0, 1);
		TestTrue(TEXT("uber during Tricky: phrase stop + tricky.bnk #0"), T.Out.Num() == 2 && T.Out[0].Kind == ECmd::PhraseStop
			&& T.Out[1].Kind == ECmd::TrickyBank && T.Out[1].Value == 0 && !T.bTrickyRider && !T.bFlagEnd && !T.bFlagWarn && !T.bFlagUber);
		FSSXTrickyAudio Off;
		Off.bArcadeSfx = false;
		Off.MeterFull(0.0, 20.0, 0, SongCtx());
		Off.Frame(0.0, 0);
		TestTrue(TEXT("Arcade SFX off: only #107"), Off.Out.Num() == 0 && Off.NumDeferred() == 1 && !Off.bPhraseOn);
		FSSXTrickyAudio Inf;
		Inf.MeterFull(0.0, 20.0, 6, SongCtx());
		TestTrue(TEXT("6 ubers: no end / warning arming, no MC"), !Inf.bFlagEnd && !Inf.bFlagWarn && Inf.bFlagUber && Inf.Out.Num() == 0);
		FSSXTrickyPhraseContext Ending = SongCtx();
		Ending.bSongEndPending = true;
		FSSXTrickyAudio E;
		E.MeterFull(0.0, 20.0, 0, Ending);
		TestTrue(TEXT("song ending posted (snd+0x3700 == 0): no phrase, no MC"), !E.bPhraseOn && E.Out.Num() == 0);
	}
	{
		FSSXAudioManifest M;
		if (M.Load(FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("manifest.json"))))
		{
			const float Want[3] = { 2.11f, 2.42f, 1.26f };
			for (int32 K = 0; K < 3; ++K)
			{
				const FSSXAudioEntry* E = M.Find(FString::Printf(TEXT("sfx/tricky/%02d"), K));
				TestTrue(FString::Printf(TEXT("tricky.bnk #%d converted (%.2f s)"), K, E ? E->Duration : 0.f), E && FMath::IsNearlyEqual(E->Duration, Want[K], 0.01f));
			}
			const FSSXAudioEntry* Z = M.Find(TEXT("sfx/zBxsfx/107"));
			TestTrue(TEXT("zbxsfx #107 1.10 s"), Z && FMath::IsNearlyEqual(Z->Duration, 1.10f, 0.01f));
		}
		else
		{
			AddError(TEXT("no converted/audio/manifest.json (tools/convert_audio.py)"));
		}
	}
	FString Text;
	TSharedPtr<FJsonObject> Ref;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FSSXHudData::DefaultDir(), TEXT("hud_anim_ref.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ref) || !Ref.IsValid())
	{
		SSXTest::Skip(*this, TEXT("no converted/hud/hud_anim_ref.json (tools/export_hud_anim_ref.py): live check skipped"));
		return true;
	}
	int32 Steps = 0, Bad = 0, Acts = 0, Fresh = 0, Clears = 0, EvTotal = 0, EvMismatch = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("live")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& Fr = O->GetArrayField(TEXT("frames"));
		TArray<FString> Want, Got;
		for (const TSharedPtr<FJsonValue>& E : O->GetArrayField(TEXT("events")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = E->AsArray();
			const FString L = A[1]->AsString();
			if (L.StartsWith(TEXT("tricky.bnk")) || L.StartsWith(TEXT("MC "))) Want.Add(FString::Printf(TEXT("%d %s"), (int32)A[0]->AsNumber(), *L));
		}
		FSSXTrickyAudio T;
		double Prev = -1.0;
		int32 PrevFrame = -1, PrevMotion = 0;
		for (int32 K = 0; K < Fr.Num(); ++K)
		{
			const TArray<TSharedPtr<FJsonValue>>& R = Fr[K]->AsArray();
			const int32 Frame = (int32)R[0]->AsNumber();
			const double Tm = R[1]->AsNumber();
			const int32 Motion = (int32)R[2]->AsNumber();
			const double Meter = R[3]->AsNumber();
			if (Prev >= 0.0 && Tm > Prev + 0.5)
			{
				if (Frame == PrevFrame + 1) ++Acts;
				Fresh += Prev <= 0.0 ? 1 : 0;
				T.MeterFull(Prev, Tm, 0, SongCtx());
			}
			else if (Prev >= 0.0 && Frame == PrevFrame + 1)
			{
				// 0x11B1F8
				++Steps;
				const double Ta = FMath::Abs(Prev - 1.0 / 60.0) < 1e-6 ? FRiderSim::Dt : Prev;
				const double E1 = FRiderSim::TrickyTimerStep(Ta, PrevMotion == 1), E2 = FRiderSim::TrickyTimerStep(Ta, Motion == 1);
				if (FMath::Abs(Tm - E1) > 1e-4 && FMath::Abs(Tm - E2) > 1e-4)
				{
					if (Tm == 0.0 && Meter <= 0.6667) ++Clears; else ++Bad;
				}
			}
			T.Frame(Tm, 0);
			for (const SSXTrickyAudio::FCmd& C : T.Out)
			{
				const FString L = Label(C);
				if (!L.IsEmpty()) Got.Add(FString::Printf(TEXT("%d %s"), K, *L));
			}
			T.Out.Reset();
			T.Advance(FrameMs);
			T.Out.Reset();
			Prev = Tm;
			PrevFrame = Frame;
			PrevMotion = Motion;
		}
		EvTotal += Want.Num();
		if (Want != Got)
		{
			++EvMismatch;
			AddError(FString::Printf(TEXT("%s rider %s: cues differ from the model (%d vs %d)"), *O->GetStringField(TEXT("session")),
				*O->GetStringField(TEXT("rider")), Got.Num(), Want.Num()));
		}
	}
	AddInfo(FString::Printf(TEXT("live: %d timer steps, %d off-rule, %d wipe-out clears; %d activations (%d fresh); %d model cues, %d riders differ"),
		Steps, Bad, Clears, Acts, Fresh, EvTotal, EvMismatch));
	TestEqual(TEXT("timer rule holds on every live frame step"), Bad, 0);
	TestEqual(TEXT("36 live activations"), Acts, 36);
	TestEqual(TEXT("cue schedule = model on every rider"), EvMismatch, 0);
	return true;
}

#endif
