#include "UI/SSXFeWidget.h"

#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXWaveKeepAlive.h"
#include "Components/AudioComponent.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundWaveProcedural.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXLoadingScreen.h"
#include "UI/SSXRaceUIHost.h"
#include "UObject/Package.h"
#include "World/SSXRenderLook.h"

using namespace SSXFe;

void USSXFeWidget::InitFrontEnd(USSXFrontEndSubsystem* InSub, bool bColdBoot)
{
	Sub = InSub;
	Mode = EMode::FrontEnd;
	// 0x17BBD8
	FE.Init(Sub ? &Sub->Profile : nullptr, Sub ? &Sub->Cheats : nullptr, Sub ? &Sub->Strings() : nullptr, bColdBoot,
		Sub ? (Sub->LastExitCode & ~0x20) : 2);
	if (Sub && !bColdBoot)
	{
		// FUN_00263280 / FUN_00164288
		FE.GameMode = Sub->Launch.GameMode;
		FE.Track = Sub->Launch.Track;
		FE.Difficulty = Sub->Launch.Difficulty;
		FE.NumPlayers = FMath::Max(1, Sub->Launch.Players);
	}
	DrawList.Assets = Sub ? &Sub->Assets() : nullptr;
	FE.Camera.CameraSet = Sub && Sub->Cameras().IsLoaded() ? &Sub->Cameras() : nullptr;
	SetIsFocusable(true);
}

void USSXFeWidget::InitRace(USSXFrontEndSubsystem* InSub)
{
	Sub = InSub;
	Mode = EMode::Race;
	Overlays.Init(Sub ? &Sub->Profile : nullptr, Sub ? &Sub->Cheats : nullptr, Sub ? &Sub->Strings() : nullptr);
	DrawList.Assets = Sub ? &Sub->Assets() : nullptr;
	SetIsFocusable(true);
}

TSharedRef<SWidget> USSXFeWidget::RebuildWidget()
{
	Canvas = SNew(SSSXFeCanvas).DrawList(&DrawList).FillBlack(false);
	return Canvas.ToSharedRef();
}

void USSXFeWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Canvas.Reset();
}

void USSXFeWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void USSXFeWidget::NativeDestruct()
{
	StopMovie();
	Super::NativeDestruct();
}

uint32 USSXFeWidget::PadBitForKey(const FKey& K)
{
	if (K == EKeys::Gamepad_FaceButton_Bottom || K == EKeys::SpaceBar) return PadCross;
	if (K == EKeys::Gamepad_FaceButton_Right || K == EKeys::LeftControl) return PadCircle;
	if (K == EKeys::Gamepad_FaceButton_Top || K == EKeys::BackSpace) return PadTriangle;
	if (K == EKeys::Gamepad_FaceButton_Left || K == EKeys::Tab) return PadSquare;
	if (K == EKeys::Gamepad_Special_Right || K == EKeys::Enter) return PadStart;
	if (K == EKeys::Gamepad_Special_Left || K == EKeys::Escape) return PadSelect;
	if (K == EKeys::Gamepad_LeftShoulder || K == EKeys::Q) return PadL1;
	if (K == EKeys::Gamepad_RightShoulder || K == EKeys::E) return PadR1;
	if (K == EKeys::Gamepad_LeftTrigger || K == EKeys::One) return PadL2;
	if (K == EKeys::Gamepad_RightTrigger || K == EKeys::Three) return PadR2;
	if (K == EKeys::Up || K == EKeys::Gamepad_DPad_Up || K == EKeys::Gamepad_LeftStick_Up) return PadUp;
	if (K == EKeys::Down || K == EKeys::Gamepad_DPad_Down || K == EKeys::Gamepad_LeftStick_Down) return PadDown;
	if (K == EKeys::Left || K == EKeys::Gamepad_DPad_Left || K == EKeys::Gamepad_LeftStick_Left) return PadLeft;
	if (K == EKeys::Right || K == EKeys::Gamepad_DPad_Right || K == EKeys::Gamepad_LeftStick_Right) return PadRight;
	return 0;
}

void USSXFeWidget::PressPad(int32 Pad, uint32 Bit)
{
	Pad = FMath::Clamp(Pad, 0, 1);
	if (Mode == EMode::FrontEnd)
	{
		FE.SetHeld(Pad, Held[Pad]);
		FE.Key(Pad, KeyForPadBit(Bit));
	}
	else
	{
		Overlays.Pad(Pad, Bit);
	}
}

FReply USSXFeWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const uint32 Bit = PadBitForKey(Key);
	if (!Bit)
	{
		return FReply::Unhandled();
	}
	const int32 Pad = Key.IsGamepadKey() ? FMath::Clamp((int32)InKeyEvent.GetUserIndex(), 0, 1) : 0;
	if (Mode == EMode::FrontEnd && Key == EKeys::Escape && !InKeyEvent.IsRepeat() && FE.Boot == ESSXFeBoot::FrontEnd
		&& FE.MovieRequest.IsEmpty() && FE.Current() == ESSXFeScreen::Title)
	{
		// PC port only (the PS2 game has no quit): Esc on the Title menu closes the game (ends PIE in the editor)
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		return FReply::Handled();
	}
	if (Mode == EMode::FrontEnd)
	{
		// the FE reads the stick from the analog value (> 0.5) once per frame (StepPads), not from Slate's stick buttons
		const bool bStickKey = Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down
			|| Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right;
		if (!bStickKey)
		{
			HeldKeys[Pad].Add(Key);
		}
		return FReply::Handled();
	}
	const bool bDir = (Bit & (PadUp | PadDown | PadLeft | PadRight)) != 0;
	if (InKeyEvent.IsRepeat() && !bDir)
	{
		return FReply::Handled();
	}
	Held[Pad] |= Bit;
	PressPad(Pad, Bit);
	return FReply::Handled();
}

FReply USSXFeWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const uint32 Bit = PadBitForKey(Key);
	const int32 Pad = Key.IsGamepadKey() ? FMath::Clamp((int32)InKeyEvent.GetUserIndex(), 0, 1) : 0;
	HeldKeys[Pad].Remove(Key);
	if (Mode != EMode::FrontEnd)
	{
		Held[Pad] &= ~Bit;
		FE.SetHeld(Pad, Held[Pad]);
	}
	return Bit ? FReply::Handled() : FReply::Unhandled();
}

FReply USSXFeWidget::NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent)
{
	const FKey Key = InAnalogEvent.GetKey();
	const int32 Pad = FMath::Clamp((int32)InAnalogEvent.GetUserIndex(), 0, 1);
	if (Key == EKeys::Gamepad_LeftX) { Stick[Pad].X = InAnalogEvent.GetAnalogValue(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_LeftY) { Stick[Pad].Y = -InAnalogEvent.GetAnalogValue(); return FReply::Handled(); }   // UE up = +1, PS2 up = -1
	return Super::NativeOnAnalogValueChanged(InGeometry, InAnalogEvent);
}

void USSXFeWidget::StepPads()
{
	// 0x17B278 / FUN_0026CF60 / 0x3857C0
	for (int32 Pad = 0; Pad < 2; ++Pad)
	{
		uint32 Word = SSXFe::StickBits(Stick[Pad].X, Stick[Pad].Y);
		for (const FKey& K : HeldKeys[Pad]) { Word |= PadBitForKey(K); }
		const uint32 New = Word & ~PrevWord[Pad];
		PrevWord[Pad] = Word;
		Held[Pad] = Word & 0xFFFF;
		FE.SetHeld(Pad, Held[Pad]);
		const int32 Key = Emitter[Pad].Step(Word);
		if ((Word & (PadL1 | PadR1)) == (PadL1 | PadR1))
		{
			for (uint32 Bit = 1; Bit <= PadR2; Bit <<= 1)
			{
				if ((New & Bit) && Bit != PadL1 && Bit != PadR1) { FE.Key(Pad, KeyForPadBit(Bit)); }
			}
			continue;
		}
		if (Key != 0)
		{
			FE.Key(Pad, static_cast<ESSXFeKey>(Key));
		}
	}
}

FReply USSXFeWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

void USSXFeWidget::Step()
{
	if (Mode == EMode::FrontEnd)
	{
		StepPads();
		FE.Tick();
		StepBookVideo();
	}
	else
	{
		Overlays.SetHost(SSXRaceUI::GetHost(GetWorld()));
		Overlays.Tick();
	}
}

void USSXFeWidget::StepBookVideo()
{
	const FSSXFeState* S = FE.Top();
	const bool bOn = S && S->Screen == ESSXFeScreen::TrickBook && S->BookVideo >= 0 && FE.Profile;
	const int32 Char = bOn ? FMath::Clamp(FE.Profile->Slot[S->Player].Char, 0, 11) : -1;
	if (BookVideo.Step(Char, bOn ? S->BookVideo : -1))
	{
		if (!BookVideoTex || BookVideoTex->GetSizeX() != BookVideo.W || BookVideoTex->GetSizeY() != BookVideo.H)
		{
			BookVideoTex = UTexture2D::CreateTransient(BookVideo.W, BookVideo.H, PF_B8G8R8A8);
			BookVideoTex->Filter = TF_Bilinear;
			BookVideoTex->SRGB = true;
			BookVideoTex->UpdateResource();
		}
		const int32 Bytes = BookVideo.W * BookVideo.H * 4;
		uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes));
		FMemory::Memcpy(Copy, BookVideo.Pixels.GetData(), Bytes);
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, BookVideo.W, BookVideo.H);
		BookVideoTex->UpdateTextureRegions(0, 1, Region, BookVideo.W * 4, 4, Copy,
			[](uint8* Data, const FUpdateTextureRegion2D* R) { FMemory::Free(Data); delete R; });
	}
	FE.BookVideoTexture = BookVideo.HasFrame() ? BookVideoTex.Get() : nullptr;
}

void USSXFeWidget::Redraw()
{
	DrawList.Reset();
	if (Mode == EMode::FrontEnd)
	{
		if (MediaTexture && bMovieStarted)
		{
			// Movies fill the 640x480 frame on black.
			DrawList.RectScreen(0.f, 0.f, ScreenW, ScreenH, Black, -10);
			DrawList.Image(MediaTexture, 0.f, 0.f, ScreenW, ScreenH, White, -9);
		}
		else if (!FE.MovieRequest.IsEmpty() || FE.Boot != ESSXFeBoot::FrontEnd)
		{
			DrawList.RectScreen(0.f, 0.f, ScreenW, ScreenH, Black, -10);
		}
		FE.Draw(DrawList);
	}
	else
	{
		Overlays.Draw(DrawList);
	}
	if (Canvas.IsValid())
	{
		Canvas->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void USSXFeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (USSXLoadingScreen::HoldsGame(this))
	{
		Accum = 0.f;
		return;
	}
	// Fixed 60 Hz steps (the game's frame counts: 30-frame tweens, 3601-frame attract, ...).
	Accum = FMath::Min(Accum + InDeltaTime, 0.25f);
	while (Accum >= 1.f / FrameRate)
	{
		Accum -= 1.f / FrameRate;
		Step();
	}
	if (Mode == EMode::FrontEnd)
	{
		TickMovie(InDeltaTime);
		if (FE.SoundHooks.Contains(TEXT("saveVideo")))
		{
			USSXVideoSettings::Save();
		}
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
		{
			SSXGameAudio::TickFrontEnd(*Audio, FE);
		}
	}
	else
	{
		if (const USSXFrontEndSubsystem* FrontEnd = USSXFrontEndSubsystem::Get(this))
		{
			FrontEnd->ApplySettings(GetWorld());
		}
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
		{
			SSXGameAudio::TickRaceOverlays(*Audio, Overlays);
		}
	}
	Redraw();
}

void USSXFeWidget::SkipMovie()
{
	StopMovie();
	if (!FE.MovieRequest.IsEmpty())
	{
		FE.MovieFinished();
	}
}

void USSXFeWidget::TickMovie(float Dt)
{
	if (FE.MovieRequest.IsEmpty())
	{
		if (MediaPlayer) StopMovie();
		return;
	}
	if (PlayingMovie != FE.MovieRequest)
	{
		StartMovie(FE.MovieRequest);
		if (!MediaPlayer)
		{
			UE_LOG(LogSSX, Warning, TEXT("[fe] movie %s missing in %s"), *FE.MovieRequest, *SSXFrontEnd::VideoDir());
			StopMovie();
			FE.MovieFinished();
			return;
		}
	}
	MovieClock += Dt;
	if (!bMovieStarted && MediaPlayer->IsPlaying())
	{
		bMovieStarted = true;
		const FString Wav = FPaths::Combine(SSXFrontEnd::VideoDir(), PlayingMovie + TEXT(".wav"));
		MovieAudioSeconds = StartMovieAudio(Wav) ? SSXFrontEnd::WavSeconds(Wav) : 0.0;
		MovieAudioClock = 0.0;
	}
	bool bDone = MediaPlayer->HasError() || (!bMovieStarted && MovieClock > 5.f);
	if (bMovieStarted)
	{
		MovieAudioClock += Dt;
		const double Dur = MediaPlayer->GetDuration().GetTotalSeconds();
		const double Now = MediaPlayer->GetTime().GetTotalSeconds();
		bMovieVideoEnded |= (!MediaPlayer->IsPlaying() && !MediaPlayer->IsPreparing()) || (Dur > 0.0 && Now >= Dur - 0.05);
		// 0x2BA810
		bDone |= SSXFrontEnd::MovieDone(false, bMovieVideoEnded, MovieAudioSeconds, MovieAudioClock)
			|| MovieClock > FMath::Max(Dur, MovieAudioSeconds) + 3.0;
	}
	if (bDone)
	{
		StopMovie();
		FE.MovieFinished();
	}
}

void USSXFeWidget::StartMovie(const FString& Name)
{
	StopMovie();
	if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
	{
		Audio->PauseForMovie();
	}
	PlayingMovie = Name;
	MovieClock = 0.f;
	bMovieStarted = false;
	bMovieVideoEnded = false;
	MovieAudioSeconds = MovieAudioClock = 0.0;
	const FString Path = FPaths::Combine(SSXFrontEnd::VideoDir(), Name + TEXT(".m2ts"));
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
		UE_LOG(LogSSX, Warning, TEXT("[fe] media player refused %s"), *Path);
		MediaPlayer = nullptr;
		MediaTexture = nullptr;
	}
}

void USSXFeWidget::StopMovie()
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
	PlayingMovie.Reset();
	if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
	{
		Audio->ResumeAfterMovie();
	}
}

bool USSXFeWidget::StartMovieAudio(const FString& WavPath)
{
	TArray<uint8> D;
	if (!FFileHelper::LoadFileToArray(D, *WavPath) || D.Num() < 44 || FMemory::Memcmp(D.GetData(), "RIFF", 4) != 0)
	{
		return false;
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
		return false;
	}
	MovieWave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	MovieWave->SetSampleRate(Rate);
	MovieWave->NumChannels = Channels;
	MovieWave->Duration = INDEFINITELY_LOOPING_DURATION;
	MovieWave->SoundGroup = SOUNDGROUP_Music;
	MovieWave->bLooping = false;
	MovieWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	MovieWave->QueueAudio(D.GetData() + DataOff, DataLen);
	SSXWaveKeepAlive::Track(MovieWave);
	// 0x2BA728 / 0x2D4A60 / 0x2D2A88 / 0x2D2F90
	const USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
	const float Volume = (Sub ? Sub->Profile.Settings.Music : 1.f) * (Audio ? Audio->GetMasterGain() : 1.f);
	MovieAudio = UGameplayStatics::CreateSound2D(this, MovieWave, Volume, 1.f, 0.f, nullptr, false, false);
	if (MovieAudio)
	{
		MovieAudio->Play();
	}
	return MovieAudio != nullptr;
}
