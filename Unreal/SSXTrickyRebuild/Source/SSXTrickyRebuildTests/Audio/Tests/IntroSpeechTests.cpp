// 0x2355C8 / 0x2356F0 / 0x235BA8 / 0x235780
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXSpeech.h"
#include "Misc/Paths.h"
#include "Race/IntroDialogue.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioIntroSpeechTest, "SSX.Audio.IntroSpeech",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXAudioIntroSpeechTest::RunTest(const FString&)
{
	USSXAudioSubsystem* Audio = NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	if (!Audio->LoadSpeech(FSSXSpeechSystem::ESet::Race))
	{
		AddError(TEXT("race speech not converted (tools/convert_speech.py)"));
		return false;
	}
	const FVector Moby(100, 0, 0), Jp(200, 0, 0);
	Audio->OnAnimEvent(0xD0, 4, Moby, true);
	Audio->OnAnimEvent(0xD1, 4, Moby, true);
	Audio->OnAnimEvent(0xD0, 1, Jp, false);
	Audio->OnAnimEvent(97, 4, Moby, true);
	TestEqual(TEXT("0xD0 / 0xD1 / AI / bank 7"), Audio->AnimEventLog, TArray<FString>{ TEXT("zboard:80"), TEXT("zboard:81"), TEXT("anim:97") });
	TestTrue(TEXT("zboard #80 / #81 played"), Audio->UISoundLog.Contains(TEXT("sfx/zboard/80")) && Audio->UISoundLog.Contains(TEXT("sfx/zboard/81")));
	Audio->AnimEventLog.Reset();

	Audio->IntroSpeechBegin();
	TestTrue(TEXT("ring on"), Audio->IsIntroRingOn());
	Audio->IntroSpeechPost(1025);
	Audio->IntroSpeechPost(1026);
	Audio->IntroSpeechPost(999);   // 0x2356F0
	TestEqual(TEXT("two held posts"), Audio->AnimEventLog, TArray<FString>{ TEXT("begin"), TEXT("post:1025:0"), TEXT("post:1026:1") });
	const int32 HeardBefore = Audio->SpeechLog.Num();
	TestEqual(TEXT("held lines do not play"), HeardBefore, 0);
	Audio->AnimEventLog.Reset();
	Audio->OnAnimEvent(1025, 4, Moby, true);
	Audio->OnAnimEvent(1026, 5, Jp, false);
	Audio->OnAnimEvent(1025, 4, Moby, true);
	TestEqual(TEXT("released in ring order at the speaking rider"), Audio->AnimEventLog,
		TArray<FString>{ TEXT("release:4:1025"), TEXT("release:5:1026"), TEXT("release:4:none") });
	TestEqual(TEXT("two ANIM samples played"), Audio->SpeechLog.Num(), 2);
	for (const FString& K : Audio->SpeechLog)
	{
		TestTrue(TEXT("ANIM bank sample"), K.StartsWith(TEXT("anim/anim_Banks/")));
	}
	Audio->IntroSpeechBegin();
	Audio->IntroSpeechPost(1025);
	Audio->AnimEventLog.Reset();
	Audio->OnAnimEvent(1300, 2, Jp, false);
	TestEqual(TEXT("any event >= 1000 pops the next line"), Audio->AnimEventLog, TArray<FString>{ TEXT("release:2:1025") });
	Audio->IntroSpeechEnd();
	TestFalse(TEXT("ring off"), Audio->IsIntroRingOn());
	Audio->AnimEventLog.Reset();
	const int32 Before = Audio->SpeechLog.Num();
	Audio->OnAnimEvent(1025, 4, Moby, true);
	TestEqual(TEXT("ANIM posted outside the intro"), Audio->AnimEventLog, TArray<FString>{ TEXT("anim:1025") });
	TestEqual(TEXT("and played"), Audio->SpeechLog.Num(), Before + 1);
	// 0x235BA8 / 0x228BD8 / 0x226DD8
	TestTrue(TEXT("ANIM line: range 1500, follows racer 4"), Audio->GetSpeechRange(1) == 1500.0 && Audio->GetSpeechRacer(1) == 4);
	// 0x2D2728
	Audio->OnAnimEvent(1026, 5, Jp, false);
	if (Audio->GetSpeechQueueLength(1) > 0)
	{
		TestEqual(TEXT("both lines queued back to back"), Audio->GetSpeechQueueLength(1), Audio->SpeechLog.Num() - Before);
	}
	else
	{
		AddInfo(TEXT("no audio world: the speech streams are not created, append not checked"));
	}
	TestEqual(TEXT("the stream now follows racer 5"), Audio->GetSpeechRacer(1), 5);
	Audio->StopSpeech(1);
	const int32 BeforeChar = Audio->SpeechLog.Num();
	Audio->CharRequest(SSXSpeech::MakeRequest({ SSXSpeech::RequestId(SSXSpeech::DbChar, 0x6024), 0x40, 0x40 }), Moby, 6, 3);
	if (Audio->SpeechLog.Num() > BeforeChar)
	{
		TestTrue(TEXT("CHAR line: range 0 (30 m), follows racer 3"), Audio->GetSpeechRange(1) == 0.0 && Audio->GetSpeechRacer(1) == 3);
	}
	Audio->bAnimEventSounds = false;
	Audio->AnimEventLog.Reset();
	Audio->OnAnimEvent(0xD0, 4, Moby, true);
	TestEqual(TEXT("animation-event sounds off"), Audio->AnimEventLog.Num(), 0);
	const SSXIntroDialogue::FData& D = SSXIntroDialogue::FData::Get();
	if (D.bLoaded)
	{
		TSet<int32> Values;
		for (const SSXIntroDialogue::FData::FRecord& R : D.Records) { if (R.Value >= 1000) Values.Add(R.Value); }
		int32 Resolved = 0, Decoded = 0;
		const TArray<SSXSpeech::FSampleGroup>& Groups = Audio->GetSpeech().Engine.GetGroups();
		for (const int32 V : Values)
		{
			TArray<SSXSpeech::FResolvedSample> Segs;
			Audio->GetSpeech().RequestHeld(SSXSpeech::AnimRequest(V), Segs);
			Resolved += Segs.Num() > 0 ? 1 : 0;
			bool bAll = Segs.Num() > 0;
			for (const SSXSpeech::FResolvedSample& S : Segs)
			{
				const SSXSpeech::FSampleGroup& G = Groups[S.Group];
				const FString Path = FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("speech"),
					FString::Printf(TEXT("%s/%s/%08llx.wav"), *G.Section, *G.DatStem, (unsigned long long)S.Offset));
				bAll &= FPaths::FileExists(Path);
			}
			Decoded += bAll ? 1 : 0;
		}
		TestEqual(TEXT("intro speech values resolve to an ANIM sample"), Resolved, Values.Num());
		TestEqual(TEXT("and every sample is decoded"), Decoded, Values.Num());
		AddInfo(FString::Printf(TEXT("intro speech: %d values, %d resolve, %d decoded"), Values.Num(), Resolved, Decoded));
	}
	return true;
}

#endif
