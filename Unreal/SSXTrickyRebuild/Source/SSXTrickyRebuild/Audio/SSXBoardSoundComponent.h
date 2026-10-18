// 0x1579E0 / 0x21BFD0
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXAudioTypes.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXSnowSound.h"
#include "Components/SceneComponent.h"
#include "SSXBoardSoundComponent.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USSXAudioSubsystem;
struct FRiderState;

UCLASS(ClassGroup = (SSX), meta = (BlueprintSpawnableComponent))
class SSXTRICKYREBUILD_API USSXBoardSoundComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USSXBoardSoundComponent();

	void UpdateFromRider(const FRiderState& S, float DeltaSeconds);
	/** One-shot from the event table: zboard or zbxsfx sound, volume 0..127 (x SFX level), 2D or 3D with a cull / falloff radius (m). AI one-shots play only within 40 m of the listener. */
	void PlayOneShotSound(int32 Sound, int32 Volume127, bool bZboard, bool b3D = false, double RadiusM = SSXBoardAudio::AIOneShotRange);
	/** 0x210A38 (GO 0x215288, replay overlay start, end-race state 10): 0x210B50 stops the wind and held voices, 0x210AD0 restarts every rider's board voices; snd+0x38A0 (wind level) = 0. */
	void RestartVoices();
	// 0x1579E0
	void TrickBookFirst();
	void PlayOneShotId(const FString& Id, int32 Volume127, bool b3D = false, double RadiusM = SSXBoardAudio::AIOneShotRange);
	/** Instance -> true if it has a sound record (instance +0xF4), OutId = manifest id of its hit sound (empty if unresolved). Unset = none has one. */
	static TFunction<bool(int32 Instance, FString& OutId)> PropSoundLookup;
	// 0x22D508
	static TFunction<void(int32 Instance)> PropSoundOwner;
	/** Sound manager's hold ring (0x2154E0 / 0x215998, hold snd+0x80 + 50 ms = 3 frames) for world-instance keys: prop hit 0x216C50 and track op-8 sound 0x2165C8 share it. Instance -> race frame of its last sound. */
	static TMap<int32, int32> PropSoundRing;
	// 0x2154E0
	static bool IsInstanceSoundHeld(int32 Instance, int32 Frame);
	// 0x215998
	static void HoldInstanceSound(int32 Instance, int32 Frame) { PropSoundRing.Add(Instance, Frame); }
	TArray<FString> CrowdLog;
	int32 CrowdState = 0;
	// 0x217D50 / 0x2181E8
	int32 PlayerSlot = 0;

	const FSSXSnowPrograms& GetPrograms() const { return Programs; }
	/** 0..127 bend -> pitch multiplier (0x40 = 1.0). TODO: the bend scale of 0x2D6A18 is not recovered; +-BendRangeSemitones at 0 / 127 is used. */
	float BendToPitch(int32 Bend) const;
	static TArray<FString> AllSoundIds();

	UPROPERTY(EditAnywhere, Category = "Board Sound") bool bIsAI = false;
	UPROPERTY(EditAnywhere, Category = "Board Sound") float BendRangeSemitones = 2.f;
	/** Game mode 7 raises the big-air duck floor f from 16 to 50; replay playback (0x21BFD0) turns the duck off. TODO: set from the race mode. */
	UPROPERTY(EditAnywhere, Category = "Board Sound") bool bGameMode7 = false;

	ESSXSnowSurface CurrentSurface = ESSXSnowSurface::Pack;
	FSSXSnowInputs LastInputs;
	FSSXSnowResult LastResult;
	int32 LastTweakStartCount = 0; // 0x1272E8 / 0x218B78
	float LastVolume = 0.f;
	float LastPitch = 1.f;
	int32 LastMusicByte = 127;     // player: big-air music byte
	double LastWindLevel = 0.0;
	int32 UpdateCount = 0;
	int32 VoiceRestarts = 0;
	int32 NumPlayingLoops() const;
	// 0x218700 / 0x218B20
	bool IsSlideHeld() const { return bSlideHeld; }
	float SlideGain = 0.f;
	/** A replay keyframe was loaded into this rider: the next update takes the restored state as seen (no event edges); held slide / boost voices the restored state does not hold are released. */
	void OnRiderStateRestored(const FRiderState& S);
	TArray<FString> OneShotLog;
	TArray<bool> OneShot3DLog;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FVoice
	{
		SSXBoardAudio::EVoice Role = SSXBoardAudio::EVoice::Glide;
		int32 Sound = -1;
		double PrevDist = -1.0;
	};
	USSXAudioSubsystem* Audio() const;
	USoundAttenuation* Spatial();
	/** Held voice made at its first volume (Volume = final gain 0..1; RadiusM = game falloff / cull radius, 0 for 2D). */
	UAudioComponent* MakeVoice(const FString& Id, bool b3D, float Volume, double RadiusM);
	bool ListenerDistance(double& OutM) const;
	void StopComp(TObjectPtr<UAudioComponent>& C, float Fade = 0.f, const TCHAR* Why = TEXT("owner stop"));

	FSSXSnowPrograms Programs;
	bool bProgramsTried = false;

	TArray<FVoice> Voices;
	UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> VoiceComps;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> Wind;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> Slide;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> BoostVoice;
	UPROPERTY(Transient) TObjectPtr<USoundAttenuation> SpatialSettings;
	bool bWindStarted = false;
	int32 SlideVolume127 = 0;

	bool bHaveState = false;
	bool bSlideHeld = false;
	uint8 LastMotion = 0;
	uint8 LastControl = 0;
	bool bLastBoost = false;
	int32 LastWallSoundCount = 0;
	int32 LastWallSoundFrame = -1000000;   // 0x2154E0 / 0x215998
	int32 LastBoostStartCount = 0;
	int32 LastCrashSoundCount = 0;
	int32 LastSlideSoundCount = 0;
	int32 LastBoostEmptyPressCount = 0;
	int32 LastForcedResetCount = 0;
	int32 LastPropHitCount = 0;
	int32 LastCrowdCueCount = 0;

	static int32 GRailSurface;
	static int32 GLastBumpFrame;
};
