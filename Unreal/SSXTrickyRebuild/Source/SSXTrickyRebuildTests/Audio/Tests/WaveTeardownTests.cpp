#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXIntroMusic.h"
#include "Audio/SSXWaveKeepAlive.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundWaveProcedural.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioProceduralWaveTeardownTest, "SSX.Audio.ProceduralWaveTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAudioProceduralWaveTeardownTest::RunTest(const FString&)
{
	SSXWaveKeepAlive::Retire(nullptr);
	SSXWaveKeepAlive::Track(nullptr);
	USoundWave* Plain = NewObject<USoundWave>(GetTransientPackage());
	SSXWaveKeepAlive::Track(Plain);
	TestFalse(TEXT("a non-procedural wave is not held"), SSXWaveKeepAlive::IsHeld(Plain));
	NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>())->StopStreamVoices();

	struct FState
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UAudioComponent> StreamVoice, MovieVoice;
		TStrongObjectPtr<USoundWave> Stream, Movie;
		TWeakObjectPtr<USoundWave> WeakStream, WeakMovie;
		double Start = 0.0;
	};
	TSharedRef<FState> S = MakeShared<FState>();
	UPackage* Package = CreatePackage(TEXT("/Temp/SSXAudioTest/wave_teardown"));
	S->World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("wave_teardown"), Package);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(S->World);

	USSXSegmentQueueWave* Stream = NewObject<USSXSegmentQueueWave>(GetTransientPackage());
	Stream->Init(MakeShared<FSSXSegmentQueuePlayer>(36000, 2));
	SSXWaveKeepAlive::Track(Stream);
	S->Stream.Reset(Stream);
	S->StreamVoice.Reset(UGameplayStatics::CreateSound2D(S->World, Stream, 1.f, 1.f, 0.f, nullptr, true, false));
	USoundWaveProcedural* Movie = NewObject<USoundWaveProcedural>(GetTransientPackage());
	Movie->SetSampleRate(22050);
	Movie->NumChannels = 1;
	Movie->Duration = INDEFINITELY_LOOPING_DURATION;
	TArray<uint8> Silence;
	Silence.SetNumZeroed(22050 * 2 * 10);
	Movie->QueueAudio(Silence.GetData(), Silence.Num());
	SSXWaveKeepAlive::Track(Movie);
	S->Movie.Reset(Movie);
	S->MovieVoice.Reset(UGameplayStatics::CreateSound2D(S->World, Movie, 1.f, 1.f, 0.f, nullptr, false, false));
	if (S->StreamVoice) S->StreamVoice->Play();
	if (S->MovieVoice) S->MovieVoice->Play();
	TestTrue(TEXT("both waves held while playing"), SSXWaveKeepAlive::IsHeld(Stream) && SSXWaveKeepAlive::IsHeld(Movie));
	S->Start = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]() -> bool
	{
		const bool bLive = S->Stream->IsGeneratingAudio() && S->Movie->IsGeneratingAudio();
		if (!bLive && FPlatformTime::Seconds() - S->Start < 5.0)
		{
			return false;
		}
		if (!bLive) AddInfo(TEXT("no live mixer sources (no audio device / -nosound): the hold logic is checked without them"));
		else AddInfo(TEXT("both waves rendered by live mixer sources"));

		SSXWaveKeepAlive::Retire(S->StreamVoice.Get());
		SSXWaveKeepAlive::Retire(S->MovieVoice.Get());
		S->WeakStream = S->Stream.Get();
		S->WeakMovie = S->Movie.Get();
		S->StreamVoice.Reset();
		S->MovieVoice.Reset();
		S->Stream.Reset();
		S->Movie.Reset();
		GEngine->DestroyWorldContext(S->World);
		S->World->DestroyWorld(false);
		S->World = nullptr;
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TestTrue(TEXT("the stream wave survives the teardown GC"), S->WeakStream.IsValid());
		TestTrue(TEXT("the movie wave survives the teardown GC"), S->WeakMovie.IsValid());
		TestTrue(TEXT("both still held"), SSXWaveKeepAlive::IsHeld(S->WeakStream.Get()) && SSXWaveKeepAlive::IsHeld(S->WeakMovie.Get()));
		SSXWaveKeepAlive::Prune(FPlatformTime::Seconds() + 3600.0);
		for (const TWeakObjectPtr<USoundWave>& W : { S->WeakStream, S->WeakMovie })
		{
			if (W.IsValid() && W->IsGeneratingAudio())
			{
				TestTrue(TEXT("a wave still rendered stays held"), SSXWaveKeepAlive::IsHeld(W.Get()));
			}
		}
		S->Start = FPlatformTime::Seconds();
		return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]() -> bool
	{
		SSXWaveKeepAlive::Prune(FPlatformTime::Seconds());
		if (S->WeakStream.IsValid() || S->WeakMovie.IsValid())
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		const bool bGone = !S->WeakStream.IsValid() && !S->WeakMovie.IsValid();
		if (bGone || FPlatformTime::Seconds() - S->Start > 15.0)
		{
			TestTrue(TEXT("the retired waves are released and collected after the mixer let go"), bGone);
			AddInfo(FString::Printf(TEXT("released after %.2f s"), FPlatformTime::Seconds() - S->Start));
			return true;
		}
		return false;
	}));
	return true;
}

#endif