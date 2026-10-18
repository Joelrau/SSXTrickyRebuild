#include "UI/SSXLoadingScreen.h"

#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXWaveKeepAlive.h"
#include "Components/AudioComponent.h"
#include "GameFramework/GameModeBase.h"
#include "UI/SSXFrontEndGameMode.h"
#include "ContentStreaming.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundWaveProcedural.h"
#include "UI/SSXFrontEndData.h"
#include "Race/GameRng.h"
#include "Race/SSXRaceManager.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UObject/Package.h"

namespace
{
	/** X / Start on any pad or the keyboard (USSXFeWidget's PS2 pad mapping) skip; the mapped keys are kept from the
	 *  game below while the loader is up (Escape stays with the editor / platform). */
	class FSSXLoadInput : public IInputProcessor
	{
	public:
		explicit FSSXLoadInput(USSXLoadingScreen* InOwner) : Owner(InOwner) {}
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
		{
			USSXLoadingScreen* O = Owner.Get();
			if (!O || !O->IsActive() || E.GetKey() == EKeys::Escape)
			{
				return false;
			}
			const uint32 Bit = USSXFeWidget::PadBitForKey(E.GetKey());
			if ((Bit & (SSXFe::PadCross | SSXFe::PadStart)) && !E.IsRepeat())
			{
				O->PressSkip();
			}
			return Bit != 0;
		}
		virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& E) override
		{
			const USSXLoadingScreen* O = Owner.Get();
			return O && O->IsActive() && E.GetKey() != EKeys::Escape && USSXFeWidget::PadBitForKey(E.GetKey()) != 0;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("SSXLoadingScreen"); }
	private:
		TWeakObjectPtr<USSXLoadingScreen> Owner;
	};
}

UGameViewportClient* USSXLoadingScreen::ViewportClient() const
{
	if (ViewportOverride.IsValid())
	{
		return ViewportOverride.Get();
	}
	UGameViewportClient* VC = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	return VC ? VC : (GEngine ? GEngine->GameViewport.Get() : nullptr);
}

USSXLoadingScreen* USSXLoadingScreen::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<USSXLoadingScreen>() : nullptr;
}

bool USSXLoadingScreen::HoldsGame(const UObject* WorldContext)
{
	const USSXLoadingScreen* S = Get(WorldContext);
	return S && S->IsActive();
}

void USSXLoadingScreen::Deinitialize()
{
	End();
	Super::Deinitialize();
}

void USSXLoadingScreen::Begin(const FSSXFeLaunch& Launch, const FString& MapPackage)
{
	End();
	bModuleUnloaded = false;
	USSXFrontEndSubsystem* FE = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
	Loader.Begin(Launch, FE ? &FE->Profile : nullptr, FE ? &FE->Strings() : nullptr);
	if (!Art.bLoaded && FE)
	{
		if (!Art.Load(FE->Assets(), [FE](const FString& Path) { return FE->LoadPng(Path); }))
		{
			UE_LOG(LogSSX, Warning, TEXT("[load] no loading screen art at %s (run tools/export_loading_screen.py)"), *SSXTrackLoad::JsonPath());
		}
	}
	UE_LOG(LogSSX, Log, TEXT("[load] loader kind %d for mode %d track %d -> %s"), int32(Loader.Kind), Launch.GameMode, Launch.Track, *MapPackage);
	StartLoad(MapPackage);
}

void USSXLoadingScreen::BeginReturn(const SSXTrackLoad::FReturnPick& Pick, const FSSXFeLaunch& L, const FString& MapPackage)
{
	End();
	bModuleUnloaded = false;
	PendingPick = Pick;
	PendingLaunch = L;
	PendingMap = MapPackage;
	if (RankVideo.Begin(Pick, L.Slots[0].Char))
	{
		UnloadRaceModule();
		UE_LOG(LogSSX, Log, TEXT("[load] rank video %s before the return loader (kind %d)"), *RankVideo.Name, int32(Pick.Kind));
		StartRankMovie();
		ShowOverlay();
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USSXLoadingScreen::TickLoader));
		return;
	}
	BuildPendingReturn();
}

void USSXLoadingScreen::UnloadRaceModule()
{
	if (bModuleUnloaded)
	{
		return;
	}
	bModuleUnloaded = true;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	USSXAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXAudioSubsystem>() : nullptr;
	if (!World || !Audio || Cast<ASSXFrontEndGameMode>(World->GetAuthGameMode()))
	{
		return;
	}
	const int32 N = SSXGameAudio::OnRaceUnload(*Audio, World);
	UE_LOG(LogSSX, Log, TEXT("[load] race module unloaded: its audio stopped (%d world voices)"), N);
}

void USSXLoadingScreen::BuildPendingReturn()
{
	USSXFrontEndSubsystem* FE = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
	const SSXTrackLoad::FReturnPick& Pick = PendingPick;
	const FString MapPackage = PendingMap;
	Loader.BeginReturn(Pick.Kind, Pick.bHintAnyRank, PendingLaunch, FE ? &FE->Profile : nullptr, FE ? &FE->Strings() : nullptr,
		[]() { return FGameRng::StreamA().Next(); });   // FUN_0023D4A8
	if (!Art.bLoaded && FE)
	{
		if (!Art.Load(FE->Assets(), [FE](const FString& Path) { return FE->LoadPng(Path); }))
		{
			UE_LOG(LogSSX, Warning, TEXT("[load] no loading screen art at %s (run tools/export_loading_screen.py)"), *SSXTrackLoad::JsonPath());
		}
	}
	UE_LOG(LogSSX, Log, TEXT("[load] return loader kind %d (hint %d) -> %s"), int32(Loader.Kind), Loader.Page.Hint, *MapPackage);
	StartLoad(MapPackage);
}

void USSXLoadingScreen::StartLoad(const FString& MapPackage)
{
	DrawList.Assets = &Art.Assets;
	Target = MapPackage;
	bPreloading = bPackageReady = bTravelled = bLoaded = false;
	WorldFrames = Progress = 0;
	ReadyGate.Reset();
	WorldTextures.Reset();
	Accum = 0.f;
	SeenMusicStarts = SeenMusicStops = SeenTypeSounds = 0;

	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const FString Current = World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
	if (Current != Target && !Target.IsEmpty())
	{
		bPreloading = true;
		LoadPackageAsync(Target, FLoadPackageAsyncDelegate::CreateUObject(this, &USSXLoadingScreen::OnPackageLoaded));
	}
	else
	{
		bPackageReady = true;
	}
	ShowOverlay();
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USSXLoadingScreen::TickLoader));
	}
}

// 0x1864A0 / 0x186AA8 / 0x2BA560

void USSXLoadingScreen::StartRankMovie()
{
	StopRankMovie();
	MovieClock = 0.f;
	bMovieStarted = false;
	bMovieVideoEnded = false;
	MovieAudioSeconds = MovieAudioClock = 0.0;
	// FUN_002C7E38
	FString Name = RankVideo.Name;
	FString Path = FPaths::Combine(SSXFrontEnd::VideoDir(), Name + TEXT(".m2ts"));
	if (!FPaths::FileExists(Path))
	{
		UE_LOG(LogSSX, Warning, TEXT("[load] rank video %s not converted in %s (tools/mpc.py); the player's fallback %s"), *Name,
			*SSXFrontEnd::VideoDir(), SSXTrackLoad::MissingVideoFallback);
		Name = SSXTrackLoad::MissingVideoFallback;
		Path = FPaths::Combine(SSXFrontEnd::VideoDir(), Name + TEXT(".m2ts"));
	}
	PlayingMovieName = Name;
	if (!FPaths::FileExists(Path))
	{
		return;
	}
	MediaPlayer = NewObject<UMediaPlayer>(this);
	MediaPlayer->PlayOnOpen = true;
	MediaPlayer->SetLooping(false);
	MediaTexture = NewObject<UMediaTexture>(this);
	MediaTexture->AutoClear = false;
	MediaTexture->ClearColor = FLinearColor::Black;
	MediaTexture->SetMediaPlayer(MediaPlayer);
	MediaTexture->UpdateResource();
	if (!MediaPlayer->OpenFile(Path))
	{
		UE_LOG(LogSSX, Warning, TEXT("[load] media player refused %s"), *Path);
		MediaPlayer = nullptr;
		MediaTexture = nullptr;
	}
}

void USSXLoadingScreen::StopRankMovie()
{
	if (MovieAudio)
	{
		SSXWaveKeepAlive::Retire(MovieAudio);
		MovieAudio = nullptr;
	}
	MovieWave = nullptr;
	if (MediaPlayer)
	{
		MediaPlayer->Close();
		MediaPlayer = nullptr;
	}
	MediaTexture = nullptr;
	bMovieStarted = false;
	bMovieVideoEnded = false;
}

namespace
{
	USoundWaveProcedural* LoadMovieWave(UObject* Outer, const FString& WavPath)
	{
		TArray<uint8> D;
		if (!FFileHelper::LoadFileToArray(D, *WavPath) || D.Num() < 44 || FMemory::Memcmp(D.GetData(), "RIFF", 4) != 0)
		{
			return nullptr;
		}
		int32 Channels = 2, Rate = 48000, DataOff = -1, DataLen = 0;
		for (int32 P = 12; P + 8 <= D.Num();)
		{
			const int32 Len = D[P + 4] | (D[P + 5] << 8) | (D[P + 6] << 16) | (D[P + 7] << 24);
			if (FMemory::Memcmp(D.GetData() + P, "fmt ", 4) == 0)
			{
				Channels = D[P + 10] | (D[P + 11] << 8);
				Rate = D[P + 12] | (D[P + 13] << 8) | (D[P + 14] << 16) | (D[P + 15] << 24);
			}
			else if (FMemory::Memcmp(D.GetData() + P, "data", 4) == 0)
			{
				DataOff = P + 8;
				DataLen = FMath::Min(Len, D.Num() - DataOff);
				break;
			}
			P += 8 + Len + (Len & 1);
		}
		if (DataOff < 0)
		{
			return nullptr;
		}
		USoundWaveProcedural* W = NewObject<USoundWaveProcedural>(Outer);
		W->SetSampleRate(Rate);
		W->NumChannels = Channels;
		W->Duration = INDEFINITELY_LOOPING_DURATION;
		W->SoundGroup = SOUNDGROUP_Music;
		W->bLooping = false;
		W->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
		W->QueueAudio(D.GetData() + DataOff, DataLen);
		SSXWaveKeepAlive::Track(W);
		return W;
	}
}

void USSXLoadingScreen::TickRankMovie(float DeltaTime)
{
	bool bDone = RankVideo.bSkipped || !MediaPlayer;
	if (MediaPlayer && !bDone)
	{
		MovieClock += DeltaTime;
		if (!bMovieStarted && MediaPlayer->IsPlaying())
		{
			bMovieStarted = true;
			USSXFrontEndSubsystem* FE = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
			const FString Wav = FPaths::Combine(SSXFrontEnd::VideoDir(), PlayingMovieName + TEXT(".wav"));
			MovieWave = LoadMovieWave(GetTransientPackage(), Wav);
			MovieAudioSeconds = MovieWave ? SSXFrontEnd::WavSeconds(Wav) : 0.0;
			MovieAudioClock = 0.0;
			if (MovieWave)
			{
				// TODO: the player's own audio level is not decoded), times the library master (the video player streams through the E
				const USSXAudioSubsystem* MovieLevels = USSXAudioSubsystem::Get(this);
				const float MovieGain = (FE ? FE->Profile.Settings.Music : 1.f) * (MovieLevels ? MovieLevels->GetMasterGain() : 1.f);
				MovieAudio = UGameplayStatics::CreateSound2D(this, MovieWave, MovieGain, 1.f, 0.f, nullptr, false, false);
				if (MovieAudio)
				{
					MovieAudio->bIsUISound = true;
					MovieAudio->Play();
				}
			}
		}
		bDone = MediaPlayer->HasError() || (!bMovieStarted && MovieClock > 5.f);
		if (bMovieStarted)
		{
			MovieAudioClock += DeltaTime;
			const double Dur = MediaPlayer->GetDuration().GetTotalSeconds();
			const double Now = MediaPlayer->GetTime().GetTotalSeconds();
			bMovieVideoEnded |= (!MediaPlayer->IsPlaying() && !MediaPlayer->IsPreparing()) || (Dur > 0.0 && Now >= Dur - 0.05);
			// 0x2BA810
			bDone |= SSXFrontEnd::MovieDone(false, bMovieVideoEnded, MovieAudioSeconds, MovieAudioClock)
				|| MovieClock > FMath::Max(Dur, MovieAudioSeconds) + 3.0;
		}
	}
	if (bDone && RankVideo.Finish())
	{
		UE_LOG(LogSSX, Log, TEXT("[load] rank video %s %s"), *PlayingMovieName, RankVideo.bSkipped ? TEXT("skipped") : TEXT("ended"));
		StopRankMovie();
	}
}

void USSXLoadingScreen::OnPackageLoaded(const FName& Name, UPackage* Package, EAsyncLoadingResult::Type Result)
{
	if (!bPreloading || Name.ToString() != Target)
	{
		return;
	}
	if (Result == EAsyncLoadingResult::Succeeded && Package)
	{
		Preloaded.Reset(Package);
		PreloadedWorld.Reset(UWorld::FindWorldInPackage(Package));
	}
	else
	{
		UE_LOG(LogSSX, Warning, TEXT("[load] async load of %s failed (%d); the level opens with a blocking load"), *Target, int32(Result));
	}
	bPackageReady = true;
}

bool USSXLoadingScreen::PressSkip()
{
	if (RankVideo.IsPlaying())
	{
		return RankVideo.PressSkip();
	}
	const bool bOk = Loader.RequestSkip(bLoaded);
	if (bOk)
	{
		UE_LOG(LogSSX, Log, TEXT("[load] intro skipped at queue step %d"), Loader.Seq.Cur);
	}
	return bOk;
}

void USSXLoadingScreen::ShowOverlay()
{
	if (!Canvas.IsValid())
	{
		Canvas = SNew(SSSXFeCanvas).DrawList(&DrawList).FillBlack(false);
	}
	UGameViewportClient* VC = ViewportClient();
	if (VC && OverlayHost.Get() != VC)
	{
		if (UGameViewportClient* Old = OverlayHost.Get())
		{
			Old->RemoveViewportWidgetContent(Canvas.ToSharedRef());
		}
		VC->AddViewportWidgetContent(Canvas.ToSharedRef(), 10000);
		OverlayHost = VC;
	}
	if (FSlateApplication::IsInitialized() && !Input.IsValid())
	{
		Input = MakeShared<FSSXLoadInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Input, 0);
	}
}

void USSXLoadingScreen::HideOverlay()
{
	if (Canvas.IsValid())
	{
		UGameViewportClient* VC = OverlayHost.Get();
		if (!VC)
		{
			VC = ViewportClient();
		}
		if (VC)
		{
			VC->RemoveViewportWidgetContent(Canvas.ToSharedRef());
		}
		Canvas.Reset();
	}
	OverlayHost.Reset();
	if (Input.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(Input);
		}
		Input.Reset();
	}
}

void USSXLoadingScreen::End()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	HideOverlay();
	StopRankMovie();
	RankVideo.Finish();
	if (Music)
	{
		Music->Stop();   // FUN_00214D78
		Music = nullptr;
	}
	Preloaded.Reset();
	PreloadedWorld.Reset();
	if (IsActive())
	{
		Loader.Phase = FSSXTrackLoader::EPhase::Done;
	}
}

void USSXLoadingScreen::Travel()
{
	bTravelled = true;
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		UGameplayStatics::OpenLevel(World, FName(*Target));
	}
}

void USSXLoadingScreen::UpdateLoadState()
{
	const int32 Prev = Progress;
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const bool bInTarget = bTravelled && World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == Target && World->HasBegunPlay();
	if (!bInTarget)
	{
		const float Pct = bPreloading ? GetAsyncLoadPercentage(FName(*Target)) : -1.f;
		Progress = bPackageReady ? 7 : (Pct < 0.f ? 0 : FMath::Clamp(int32(Pct * 7.f / 100.f), 0, 7));
		bLoaded = false;
		WorldFrames = 0;
		return;
	}
	++WorldFrames;
	Progress = 8;
	if (WorldFrames == 1)
	{
		WorldTextures = SSXLoadReady::CollectWorldTextures(World);
		const int32 Forced = SSXLoadReady::ForceTexturesResident(WorldTextures, float(ReadyGate.Timeout) + SSXLoadReady::PostReleaseResidentSeconds);
		UE_LOG(LogSSX, Log, TEXT("[load] world begun: %d of its streamed textures forced resident"), Forced);
	}
	const ASSXRaceManager* Race = ASSXRaceManager::Find(World);
	if (Race && !Race->GetSession())
	{
		bLoaded = false;
		return;
	}
	Progress = 9;
	if (WorldFrames < 2 || Prev < 9)
	{
		return;
	}
	if (!ReadyGate.bReleased)
	{
		if (!ReadyGate.Update(SSXLoadReady::Probe(World, WorldTextures), FPlatformTime::Seconds()))
		{
			return;
		}
		if (ReadyGate.bTimedOut)
		{
			UE_LOG(LogSSX, Warning, TEXT("[load] ready gate timed out after %.2f s; still pending: %s%s%s"), ReadyGate.HeldSeconds(),
				*ReadyGate.Last.Describe(), ReadyGate.Last.StreamingDetail.IsEmpty() ? TEXT("") : TEXT("; textures below full (resident/requested/full mips): "),
				*ReadyGate.Last.StreamingDetail);
		}
		else
		{
			UE_LOG(LogSSX, Log, TEXT("[load] ready after %.2f s (most pending: %s)"), ReadyGate.HeldSeconds(), *ReadyGate.Waited.Describe());
		}
		SSXLoadReady::ForceTexturesResident(WorldTextures, SSXLoadReady::PostReleaseResidentSeconds);
	}
	Progress = 10;
	bLoaded = true;
}

void USSXLoadingScreen::PlaySideEffects()
{
	USSXAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXAudioSubsystem>() : nullptr;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (Loader.MusicStarts != SeenMusicStarts)
	{
		SeenMusicStarts = Loader.MusicStarts;
		// FUN_0021CA20 / 0x20F160 / 0x211098
		const bool bFeTable = World && Cast<ASSXFrontEndGameMode>(World->GetAuthGameMode()) != nullptr;
		// 0x17C6E8 / 0x17C998
		UnloadRaceModule();
		if (Audio)
		{
			Audio->StopMusic(0.f);
			// FUN_00214910
			const FString Id = FString::Printf(TEXT("sfx/LoadingScreen/%02d"), Loader.PageMusicSound());
			if (USoundBase* S = Audio->LoadSound(Id); S && World)
			{
				const float Gain = FMath::Clamp(100.f * Audio->MusicClassLevel(bFeTable), 0.f, 127.f) / 127.f;
				Music = UGameplayStatics::CreateSound2D(World, S, Gain, 1.f, 0.f, nullptr, true, false);
				if (Music)
				{
					Music->bIsUISound = true;
					Music->Play();
				}
			}
		}
	}
	if (Loader.MusicStops != SeenMusicStops)
	{
		SeenMusicStops = Loader.MusicStops;
		if (Music)
		{
			Music->FadeOut(Loader.MusicStopFade(), 0.f);   // FUN_0020C2A0
		}
	}
	if (Loader.TypeSounds != SeenTypeSounds)
	{
		const int32 N = Loader.TypeSounds - SeenTypeSounds;
		SeenTypeSounds = Loader.TypeSounds;
		// FUN_00214DD0
		if (Audio && World)
		{
			if (USoundBase* S = Audio->LoadSound(TEXT("sfx/LoadingScreen/16")))
			{
				for (int32 I = 0; I < N; ++I)
				{
					UGameplayStatics::PlaySound2D(World, S, Audio->GetSfxGain() * 100.f / 127.f, 1.f, 0.f, nullptr, nullptr, false);
				}
			}
		}
	}
}

void USSXLoadingScreen::StepOnce()
{
	if (Loader.Phase == FSSXTrackLoader::EPhase::Prepare)
	{
		Loader.Start();
	}
	UpdateLoadState();
	Loader.Step(bLoaded, Progress);
	if (bLoaded && GIsAutomationTesting && Loader.Phase == FSSXTrackLoader::EPhase::Run)
	{
		Loader.RequestSkip(true);
	}
	PlaySideEffects();
}

bool USSXLoadingScreen::TickLoader(float DeltaTime)
{
	if (RankVideo.IsPlaying())
	{
		TickRankMovie(DeltaTime);
		if (!RankVideo.IsPlaying())
		{
			BuildPendingReturn();
			return true;
		}
		if (!Canvas.IsValid())
		{
			ShowOverlay();
		}
		DrawList.Reset();
		// FUN_002BA290
		DrawList.RectScreen(0.f, 0.f, SSXFe::ScreenW, SSXFe::ScreenH, SSXFe::Black, -10);
		if (MediaTexture && bMovieStarted)
		{
			FIntPoint Dim = MediaPlayer ? MediaPlayer->GetVideoTrackDimensions(INDEX_NONE, INDEX_NONE) : FIntPoint::ZeroValue;
			if (Dim.X <= 0 || Dim.Y <= 0)
			{
				Dim = FIntPoint(512, 448);
			}
			const FBox2f R = SSXFrontEnd::MovieRect(Dim);
			DrawList.Image(MediaTexture, R.Min.X, R.Min.Y, R.Max.X - R.Min.X, R.Max.Y - R.Min.Y, SSXFe::White, -9);
		}
		if (Canvas.IsValid())
		{
			Canvas->Invalidate(EInvalidateWidgetReason::Paint);
		}
		return true;
	}
	// 0x17C9D8
	Accum = FMath::Min(Accum + DeltaTime, 0.25f);
	while (Accum >= 1.f / SSXFe::FrameRate && IsActive())
	{
		Accum -= 1.f / SSXFe::FrameRate;
		StepOnce();
		if (!bTravelled && bPackageReady && Loader.Phase != FSSXTrackLoader::EPhase::Prepare)
		{
			Travel();
		}
	}
	if (Loader.Phase == FSSXTrackLoader::EPhase::Done)
	{
		UE_LOG(LogSSX, Log, TEXT("[load] loader done (intro %s, progress %d): the %s starts"), Loader.bSkipped ? TEXT("skipped") : TEXT("played"),
			Progress, Loader.Kind == SSXTrackLoad::EKind::OneP || Loader.Kind == SSXTrackLoad::EKind::TwoP || Loader.Kind == SSXTrackLoad::EKind::Tutorial
			? TEXT("race") : TEXT("front end"));
		TickHandle.Reset();
		End();
		return false;
	}
	if (!Canvas.IsValid())
	{
		ShowOverlay();
	}
	DrawList.Reset();
	Loader.Draw(DrawList);
	if (Canvas.IsValid())
	{
		Canvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
	return true;
}
