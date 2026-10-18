#include "Audio/SSXBoardSoundComponent.h"

#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXCrowdVoices.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SSXTrickyRebuild.h"
#include "Sim/RiderState.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

namespace BA = SSXBoardAudio;

int32 USSXBoardSoundComponent::GRailSurface = 0;
int32 USSXBoardSoundComponent::GLastBumpFrame = MIN_int32 / 2;
TFunction<bool(int32, FString&)> USSXBoardSoundComponent::PropSoundLookup;
TFunction<void(int32)> USSXBoardSoundComponent::PropSoundOwner;
TMap<int32, int32> USSXBoardSoundComponent::PropSoundRing;

bool USSXBoardSoundComponent::IsInstanceSoundHeld(int32 Instance, int32 Frame)
{
	const int32* Held = PropSoundRing.Find(Instance);
	return Held && Frame - *Held < SSXBoardAudio::PropHitHold;
}

USSXBoardSoundComponent::USSXBoardSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USSXBoardSoundComponent::BeginPlay()
{
	Super::BeginPlay();
	FString Err;
	bProgramsTried = true;
	if (!Programs.LoadDefault(&Err))
	{
		UE_LOG(LogSSX, Warning, TEXT("[audio] SNOW.INF: %s (the glide / carve voices stay silent)"), *Err);
	}
}

void USSXBoardSoundComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	for (TObjectPtr<UAudioComponent>& C : VoiceComps) { StopComp(C, 0.f, TEXT("end play")); }
	StopComp(Wind, 0.f, TEXT("end play"));
	StopComp(Slide, 0.f, TEXT("end play"));
	StopComp(BoostVoice, 0.f, TEXT("end play"));
	bSlideHeld = false;
	if (!bIsAI)
	{
		if (USSXAudioSubsystem* A = Audio()) { A->SetBigAirMusicByte(127); }
	}
	Super::EndPlay(Reason);
}

void USSXBoardSoundComponent::RestartVoices()
{
	++VoiceRestarts;
	for (TObjectPtr<UAudioComponent>& C : VoiceComps) { StopComp(C, 0.f, TEXT("voice restart 0x210B50")); }
	Voices.Reset();
	VoiceComps.Reset();
	StopComp(Wind, 0.f, TEXT("voice restart 0x210B50"));
	bWindStarted = false;
	LastWindLevel = 0.0;
	StopComp(BoostVoice, 0.f, TEXT("voice restart 0x210B50"));
}

void USSXBoardSoundComponent::OnRiderStateRestored(const FRiderState& S)
{
	bHaveState = false;
	if (S.Motion != ERiderMotion::WipeOut)
	{
		// TODO: whether the original's keyframe load leaves the slide handle +0x5AE8 set (the replay start's stop-all 0x2213C8
		StopComp(Slide, 0.f, TEXT("keyframe restore"));
		bSlideHeld = false;
		SlideGain = 0.f;
	}
	if (S.BoostStrength <= 0.0)
	{
		StopComp(BoostVoice, 0.f, TEXT("keyframe restore"));   // 0x219760
	}
	bLastBoost = S.BoostStrength > 0.0;
}

USSXAudioSubsystem* USSXBoardSoundComponent::Audio() const
{
	return USSXAudioSubsystem::Get(this);
}

TArray<FString> USSXBoardSoundComponent::AllSoundIds()
{
	TArray<FString> Ids;
	for (int32 Idx = 0; Idx < int32(ESSXSnowSurface::Count); ++Idx)
	{
		if (Idx == int32(ESSXSnowSurface::Rail)) { continue; }
		for (const int32 K : { 1, 3, 4, 5, 6 }) { Ids.AddUnique(BA::ZboardId(Idx * 8 + K)); }
	}
	Ids.AddUnique(BA::ZboardId(0));
	for (const int32 S : { BA::WindSound, 48, 49, 50, 64, BA::SlideSound, 90, 91, 120, 121, 122, BA::ResetSound, BA::LeadSound }) { Ids.Add(BA::ZbxsfxId(S)); }
	return Ids;
}

float USSXBoardSoundComponent::BendToPitch(int32 Bend) const
{
	const double Semis = (FMath::Clamp(Bend, 0, 127) - 64.0) / 64.0 * BendRangeSemitones;
	return float(FMath::Pow(2.0, Semis / 12.0));
}

USoundAttenuation* USSXBoardSoundComponent::Spatial()
{
	if (!SpatialSettings)
	{
		SpatialSettings = NewObject<USoundAttenuation>(this, NAME_None, RF_Transient);
		SpatialSettings->Attenuation.bAttenuate = false;
		const USSXAudioSubsystem* A = Audio();
		SpatialSettings->Attenuation.bSpatialize = !(A && A->GetSpeaker() == 0);
	}
	return SpatialSettings;
}

UAudioComponent* USSXBoardSoundComponent::MakeVoice(const FString& Id, bool b3D, float Volume, double RadiusM)
{
	USSXAudioSubsystem* A = Audio();
	USoundWave* Wave = A ? Cast<USoundWave>(A->LoadSound(Id)) : nullptr;
	const TCHAR* Sys = bIsAI ? TEXT("board-ai") : TEXT("board");
	const float StartVol = FMath::Max(Volume, 0.f);
	if (!Wave)
	{
		SSXAudioDiag::Note(Sys, Id, b3D, Volume, RadiusM, A ? A->MissReason(Id) : SSXAudioDiag::EResult::NoWorld);
		return nullptr;
	}
	const FSSXAudioEntry* E = A->GetManifest().Find(Id);
	Wave->bLooping = E && E->LoopStart >= 0;
	UAudioComponent* C = nullptr;
	if (b3D)
	{
		C = UGameplayStatics::SpawnSoundAttached(Wave, this, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
			false, StartVol, 1.f, 0.f, Spatial(), nullptr, false);
	}
	else if (UWorld* World = GetWorld())
	{
		C = UGameplayStatics::CreateSound2D(World, Wave, StartVol, 1.f, 0.f, nullptr, false, false);
		USSXAudioSubsystem::MakeGameSound(C);   // 0x20BFE8
	}
	SSXAudioDiag::Note(Sys, Id, b3D, Volume, RadiusM, C ? SSXAudioDiag::EResult::Played : SSXAudioDiag::EResult::SpawnFailed, Wave->GetPathName());
	SSXAudioDiag::NoteLoop(Sys, Id, C, b3D);
	A->AddBankVoice(C);   // 0x2213C8
	return C;
}

void USSXBoardSoundComponent::StopComp(TObjectPtr<UAudioComponent>& C, float Fade, const TCHAR* Why)
{
	if (C)
	{
		SSXAudioDiag::NoteStop(C, Why);
		if (Fade > 0.f) { C->FadeOut(Fade, 0.f); }
		else { C->Stop(); }
		C->bAutoDestroy = true;
		C = nullptr;
	}
}

bool USSXBoardSoundComponent::ListenerDistance(double& OutM) const
{
	// 0x20E730
	const APlayerCameraManager* Cam = GetWorld() ? UGameplayStatics::GetPlayerCameraManager(this, 0) : nullptr;
	if (!Cam)
	{
		return false;
	}
	const FQuat Q = Cam->GetCameraRotation().Quaternion();
	const FVector Ear = Cam->GetCameraLocation() + Q.GetForwardVector() * BA::EarForwardCm - Q.GetUpVector() * BA::EarUpCm;
	OutM = FVector::Dist(GetComponentLocation(), Ear) * 0.01;
	return true;
}

void USSXBoardSoundComponent::PlayOneShotSound(int32 Sound, int32 Volume127, bool bZboard, bool b3D, double RadiusM)
{
	PlayOneShotId(bZboard ? BA::ZboardId(Sound) : BA::ZbxsfxId(Sound), Volume127, b3D, RadiusM);
}

void USSXBoardSoundComponent::PlayOneShotId(const FString& Id, int32 Volume127, bool b3D, double RadiusM)
{
	double Dist = 0.0;
	const bool bHaveListener = ListenerDistance(Dist);
	if (bIsAI && bHaveListener && Dist >= BA::AIOneShotRange)
	{
		return;   // 0x20F9B0
	}
	if (bIsAI && !b3D)
	{
		// TODO: the per-event 3D flag of the non-wall pushes.
		b3D = true;
		RadiusM = BA::AIOneShotRange;
	}
	int32 Vol = FMath::Clamp(Volume127, 0, 127);
	const TCHAR* Sys = bIsAI ? TEXT("board-ai") : TEXT("board");
	if (b3D)
	{
		Vol = BA::Falloff(Vol, bHaveListener ? Dist : 0.0, RadiusM);
		if (Vol <= 0)
		{
			SSXAudioDiag::Note(Sys, Id, b3D, 0.f, RadiusM, SSXAudioDiag::EResult::Volume0);
			return;
		}
	}
	OneShotLog.Add(Id);
	OneShot3DLog.Add(b3D);
	USSXAudioSubsystem* A = Audio();
	USoundBase* S = A && !Id.IsEmpty() ? A->LoadSound(Id) : nullptr;
	const float Gain = A ? FMath::Clamp(Vol / 127.f * A->GetSfxGain(), 0.f, 1.f) : 0.f;
	if (!S)
	{
		SSXAudioDiag::Note(Sys, Id, b3D, Gain, RadiusM, A ? A->MissReason(Id) : SSXAudioDiag::EResult::NoWorld);
		return;
	}
	UAudioComponent* C = nullptr;
	if (b3D)
	{
		C = UGameplayStatics::SpawnSoundAttached(S, this, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, false,
			Gain, 1.f, 0.f, Spatial());
	}
	else if (UWorld* World = GetWorld())
	{
		if ((C = UGameplayStatics::CreateSound2D(World, S, Gain, 1.f, 0.f, nullptr, false, true)) != nullptr)
		{
			USSXAudioSubsystem::MakeGameSound(C);   // 0x20BFE8
			C->Play();
		}
	}
	SSXAudioDiag::Note(Sys, Id, b3D, Gain, RadiusM, SSXAudioDiag::SpawnResult(C, false), S->GetPathName());
	SSXAudioDiag::NoteLoop(Sys, Id, C, b3D);
	A->AddBankVoice(C);
}

void USSXBoardSoundComponent::TrickBookFirst()
{
	if (bIsAI)
	{
		return;
	}
	if (USSXAudioSubsystem* A = Audio())
	{
		for (const double Ms : BA::BookDelaysMs)
		{
			A->GetTricky().Defer(Ms, BA::BookSound);
		}
	}
}

int32 USSXBoardSoundComponent::NumPlayingLoops() const
{
	int32 N = 0;
	for (const TObjectPtr<UAudioComponent>& C : VoiceComps) { N += (C && C->IsPlaying()) ? 1 : 0; }
	N += (Wind && Wind->IsPlaying()) ? 1 : 0;
	return N;
}

void USSXBoardSoundComponent::UpdateFromRider(const FRiderState& S, float DeltaSeconds)
{
	if (!bProgramsTried)
	{
		bProgramsTried = true;
		Programs.LoadDefault();
	}
	++UpdateCount;
	USSXAudioSubsystem* A = Audio();
	if (A && A->IsReplayRebuild())
	{
		for (TObjectPtr<UAudioComponent>& C : VoiceComps) { StopComp(C, 0.f, TEXT("replay rebuild")); }
		StopComp(Wind, 0.f, TEXT("replay rebuild"));
		StopComp(Slide, 0.f, TEXT("replay rebuild"));
		StopComp(BoostVoice, 0.f, TEXT("replay rebuild"));
		bSlideHeld = false;
		bWindStarted = false;
		LastTweakStartCount = S.TweakStartCount;
		LastCrashSoundCount = S.CrashSoundCount;
		LastSlideSoundCount = S.SlideSoundCount;
		LastBoostStartCount = S.BoostStartCount;
		LastBoostEmptyPressCount = S.BoostEmptyPressCount;
		LastForcedResetCount = S.ForcedResetCount;
		LastPropHitCount = S.PropHitCount;
		LastCrowdCueCount = S.CrowdCueCount;
		LastMotion = uint8(S.Motion);
		LastControl = uint8(S.Control);
		LastWallSoundCount = S.WallSoundCount;
		bLastBoost = S.BoostStrength > 0.0;
		bHaveState = true;
		return;
	}
	if (bHaveState && (S.CrashSoundCount < LastCrashSoundCount || S.SlideSoundCount < LastSlideSoundCount
		|| S.TweakStartCount < LastTweakStartCount || S.BoostStartCount < LastBoostStartCount || S.PropHitCount < LastPropHitCount
		|| S.BoostEmptyPressCount < LastBoostEmptyPressCount || S.CrowdCueCount < LastCrowdCueCount || S.WallSoundCount < LastWallSoundCount
		|| S.ForcedResetCount < LastForcedResetCount))
	{
		OnRiderStateRestored(S);
	}
	const float Level = A ? A->GetSfxGain() : 1.f;
	const int32 Motion = int32(S.Motion);
	auto Rand15 = [A]() { return A ? A->Rand15() : 0; };

	if (S.Motion == ERiderMotion::RailSlide && (!bHaveState || ERiderMotion(LastMotion) != ERiderMotion::RailSlide))
	{
		GRailSurface = S.SurfaceId;   // 0x1261F0 / 0x21E670
	}
	if (bHaveState)
	{
		const ERiderMotion PrevMotion = ERiderMotion(LastMotion);
		if (PrevMotion == ERiderMotion::Air && S.Motion == ERiderMotion::Ground)
		{
			// TODO: the writer of +0x798 is not traced, so the same v.n is assumed.
			const int32 Impact = int32(-S.LastImpact.Speed);
			const ESSXSnowSurface Idx = BA::SnowIndex(S.SurfaceId, Motion, GRailSurface);
			PlayOneShotSound(BA::LandSound(Idx, !bIsAI), BA::LandVolume(Impact, !bIsAI), true, true, BA::DefaultPushRadius);
			// 0x217D50 / 0x221068
		}
		if (S.CrashSoundCount != LastCrashSoundCount)
		{
			// 0x218328 / 0x10F9A0 / 0x235090
			const int32 Vol = BA::Curve(7, int32(S.LastImpact.Speed));
			PlayOneShotSound(BA::CrashSound(Rand15(), S.SurfaceBeforeWipeout), Vol, false, true, BA::DefaultPushRadius);
		}
		if (S.SlideSoundCount != LastSlideSoundCount && !bSlideHeld)
		{
			// TODO: the mute while handler+8 > 0.1.
			const int32 Vol = FMath::Clamp(BA::Curve(7, int32(S.LastImpact.Speed)), 0, 127);
			double Dist = 0.0;
			const bool bHaveDist = ListenerDistance(Dist);
			const bool bGate = bIsAI && bHaveDist && Dist >= BA::AIOneShotRange;
			if (!bGate)
			{
				bSlideHeld = true;
				SlideVolume127 = Vol;
				SlideGain = FMath::Clamp(BA::Falloff(Vol, bHaveDist ? Dist : 0.0, BA::DefaultPushRadius) / 127.f * Level, 0.f, 1.f);
				StopComp(Slide);
				if ((Slide = MakeVoice(BA::ZbxsfxId(BA::SlideSound), true, SlideGain, BA::DefaultPushRadius)) != nullptr)
				{
					Slide->SetVolumeMultiplier(SlideGain);
					if (!Slide->IsPlaying()) { Slide->Play(); }
				}
				OneShotLog.Add(BA::ZbxsfxId(BA::SlideSound));
				OneShot3DLog.Add(true);
			}
		}
		if (PrevMotion == ERiderMotion::WipeOut && S.Motion != ERiderMotion::WipeOut)
		{
			// 0x10D680 / 0x218B20
			StopComp(Slide, float(BA::SlideStopFade), TEXT("wipe-out exit 0x218B20"));
			bSlideHeld = false;
			SlideGain = 0.f;
		}
		// 0x216960
		if (S.WallSoundCount != LastWallSoundCount && S.Frame - LastWallSoundFrame >= BA::WallSoundHoldFrames)
		{
			// 0x126250 / 0x2162F0
			LastWallSoundFrame = S.Frame;
			PlayOneShotSound(BA::WallSound, 127, false, true, BA::PlayerVoiceRadius);
		}
		if (!bIsAI && S.ForcedResetCount != LastForcedResetCount)
		{
			// 0x118F10 / 0x218EB0
			PlayOneShotSound(BA::ResetSound, 127, false);
		}
		const bool bBoost = S.BoostStrength > 0.0;
		// 0x21BFD0 / 0x11D038 / 0x2192A8 / 0x2197D8
		const bool bReplay = A && A->IsReplayPlayback();
		if (!bIsAI && !bReplay && S.BoostStartCount != LastBoostStartCount && bBoost)
		{
			// 0x11D038 / 0x2192A8
			if (S.bBoostStartPressed)
			{
				PlayOneShotSound(BA::BoostBurstSound(S.BoostMeter), 127, false);
			}
			StopComp(BoostVoice);
			if ((BoostVoice = MakeVoice(BA::ZbxsfxId(BA::BoostSound(S.BoostMeter)), false, Level, 0.0)) != nullptr)
			{
				BoostVoice->SetVolumeMultiplier(Level);
				BoostVoice->Play();
			}
			OneShotLog.Add(BA::ZbxsfxId(BA::BoostSound(S.BoostMeter)));
			OneShot3DLog.Add(false);
		}
		if (!bBoost && bLastBoost && !bReplay)
		{
			StopComp(BoostVoice);   // 0x219760
		}
		if (!bIsAI && !bReplay && S.BoostEmptyPressCount != LastBoostEmptyPressCount)
		{
			// 0x2197D8 / 0x233E78
			PlayOneShotSound(BA::NoBoostSound, 127, false);
		}
		if (S.PropHitCount != LastPropHitCount)
		{
			// 0x216C50 / 0x2154E0 / 0x215998 / 0x22D508
			FString Id;
			const bool bHasList = PropSoundLookup && S.PropHitInstance != INDEX_NONE && PropSoundLookup(S.PropHitInstance, Id);
			double Dist = 0.0;
			const bool bFar = ListenerDistance(Dist) && Dist > BA::AIOneShotRange;
			if (bHasList && !bFar && !IsInstanceSoundHeld(S.PropHitInstance, S.Frame))
			{
				if (PropSoundOwner) { PropSoundOwner(S.PropHitInstance); }
				HoldInstanceSound(S.PropHitInstance, S.Frame);
				if (!Id.IsEmpty())   // ids the original leaves unresolved (e.g. 0) play bank 2 with a garbage index (not reproduced)
				{
					PlayOneShotId(Id, BA::PropHitVolume(S.PropHitSpeed), true, BA::AIOneShotRange);
				}
			}
		}
		// 0x217D50 / 0x2181E8 / 0x20ED20
		if (S.CrowdCueCount != LastCrowdCueCount && (A ? A->IsListenerPawn(GetOwner(), !bIsAI) : !bIsAI))
		{
			// 0x217D50 / 0x2181E8 / 0x2206E0 / 0x2210C8
			const int32 Slot = (A && A->IsReplayPlayback()) ? 0 : PlayerSlot;
			if (A)
			{
				A->GetCrowdVoices().Crowd.Cue(Slot, S.CrowdCueX, S.bCrowdCueBail);
			}
			const int32 L = BA::CrowdLevel(S.CrowdCueX);
			if (L < 0)
			{
				CrowdLog.Add(TEXT("neutral"));
			}
			else
			{
				CrowdState = S.bCrowdCueBail ? ~L : L + 1;
				CrowdLog.Add(FString::Printf(TEXT("%s%d"), S.bCrowdCueBail ? TEXT("groan") : TEXT("cheer"), L));
			}
		}
		bLastBoost = bBoost;
		if (!bIsAI && S.TweakStartCount != LastTweakStartCount && A)
		{
			// 0x1272E8 / 0x218B78
			A->OnTweakStarted();
		}
	}
	LastTweakStartCount = S.TweakStartCount;
	LastCrashSoundCount = S.CrashSoundCount;
	LastSlideSoundCount = S.SlideSoundCount;
	LastBoostStartCount = S.BoostStartCount;
	LastBoostEmptyPressCount = S.BoostEmptyPressCount;
	LastForcedResetCount = S.ForcedResetCount;
	LastPropHitCount = S.PropHitCount;
	LastCrowdCueCount = S.CrowdCueCount;
	LastMotion = uint8(S.Motion);
	LastControl = uint8(S.Control);
	LastWallSoundCount = S.WallSoundCount;
	bHaveState = true;

	if (Voices.Num() == 0)
	{
		if (bIsAI)
		{
			Voices.Add({ BA::EVoice::AIGlide });
		}
		else
		{
			Voices.Add({ BA::EVoice::Speed });
			Voices.Add({ BA::EVoice::Glide });
			Voices.Add({ BA::EVoice::Carve });
		}
		VoiceComps.SetNum(Voices.Num());
	}
	const FSSXSnowInputs In = BA::Inputs(S);
	const ESSXSnowSurface Idx = BA::SnowIndex(S.SurfaceId, Motion, GRailSurface);
	LastInputs = In;
	CurrentSurface = Idx;
	double Dist = 0.0;
	const bool bHaveListener = ListenerDistance(Dist);
	const double Radius = bIsAI ? BA::AIVoiceRadius : BA::PlayerVoiceRadius;
	for (int32 V = 0; V < Voices.Num(); ++V)
	{
		FVoice& Voice = Voices[V];
		TObjectPtr<UAudioComponent>& Comp = VoiceComps[V];
		const int32 Sound = BA::VoiceSound(Voice.Role, Idx);
		const FSSXSnowResult Out = BA::Callback(Voice.Role, Programs, Motion, Idx, In);
		const double D = bHaveListener ? Dist : 0.0;
		const bool bInRange = D < Radius;                       // 0x20B688
		const int32 Vol = BA::Falloff(Out.Volume, D, Radius);   // 0x20AAB8
		if (Sound != Voice.Sound)
		{
			StopComp(Comp);
			Voice.Sound = Sound;
			Comp = MakeVoice(BA::ZboardId(Sound), true, FMath::Clamp(Vol / 127.f * Level, 0.f, 1.f), Radius);
		}
		double Pitch = BendToPitch(Out.Bend);
		if (bHaveListener && Voice.PrevDist >= 0.0 && DeltaSeconds > 0.f)
		{
			Pitch *= BA::Doppler(Voice.PrevDist, D, DeltaSeconds);   // 0x20AB48
		}
		Voice.PrevDist = bHaveListener ? D : -1.0;
		const float Final = FMath::Clamp(Vol / 127.f * Level, 0.f, 1.f);
		if (Voice.Role == BA::EVoice::Glide || Voice.Role == BA::EVoice::AIGlide)
		{
			LastResult = Out;
			LastVolume = Final;
			LastPitch = float(Pitch);
		}
		if (Comp)
		{
			Comp->SetVolumeMultiplier(Final);
			Comp->SetPitchMultiplier(float(Pitch));
			if (bInRange && !Comp->IsPlaying()) { Comp->Play(); }
			else if (!bInRange && Comp->IsPlaying()) { Comp->Stop(); }
		}
	}
	if (bSlideHeld)
	{
		// 0x20B700
		SlideGain = FMath::Clamp(BA::Falloff(SlideVolume127, bHaveListener ? Dist : 0.0, BA::DefaultPushRadius) / 127.f * Level, 0.f, 1.f);
		if (Slide) { Slide->SetVolumeMultiplier(SlideGain); }
	}

	if (!bIsAI)
	{
		const double TLand = S.Motion == ERiderMotion::Air ? S.Prediction.FlightTime : 0.0;
		int32 Byte = 127;
		double WindLevel = 0.0;
		// 0x21B828 / 0x21BFD0 / 0x20F8C0
		const bool bReplayDuck = A && A->IsReplayPlayback();
		if (!bReplayDuck && BA::BigAir(TLand, S.Prediction.AirElapsed, S.Prediction.ApexLeft, bGameMode7, Byte, WindLevel))
		{
			if (!bWindStarted)
			{
				bWindStarted = true;
				StopComp(Wind);
				Wind = MakeVoice(BA::ZbxsfxId(BA::WindSound), false, float(WindLevel) * Level, 0.0);
				if (Wind) { Wind->Play(); }
			}
			if (Wind) { Wind->SetVolumeMultiplier(float(WindLevel) * Level); }
		}
		else
		{
			bWindStarted = false;
			StopComp(Wind);
		}
		LastMusicByte = Byte;
		LastWindLevel = WindLevel;
		if (A)
		{
			A->SetBigAirMusicByte(Byte);
			A->UpdateRaceMusic(S, DeltaSeconds);
		}
	}
}
