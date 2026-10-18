#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXAmbientEmitters.h"
#include "Audio/SSXCrowdSequencer.h"
#include "Audio/SSXCrowdSystem.h"

class UAudioComponent;
class USoundAttenuation;
class USSXAudioSubsystem;
class UWorld;

class SSXTRICKYREBUILD_API FSSXCrowdVoices
{
public:
	// 0x2209C8
	bool Begin(USSXAudioSubsystem* Audio, UWorld* World, const FString& TrackSection = FString());
	// 0x220BB8
	void End();
	/** Once per game frame after the ambient update: crowd frame (0x220968), sequencer ticks (10 ms) and the 3D tick of every crowd voice. */
	void Step(const FVector& Ear, double DeltaMs);
	// 0x20ED20
	void SetListenerRider(int32 Character, int32 Rank) { ListenerCharacter = Character; ListenerRank = Rank; }
	/** Listener rider's (0x20ED20) time to landing +0x57B8 and pending reward 0x155410, read by the crowd's anticipation query (0x20EA38). */
	void SetListenerAir(float TimeLeft, float PendingReward) { ListenerTimeLeft = TimeLeft; ListenerReward = PendingReward; }
	FString PickChantBank(int32 Listener, TFunctionRef<uint32()> Rand) const;

	FSSXCrowdSystem Crowd;
	FSSXChantBanks Chants;
	SSXCrowdSeq::FPatch Patches[32];
	SSXCrowdSeq::FLibRandom LibRng;
	bool IsBegun() const { return bBegun; }
	int32 NumVoices() const { return Voices.Num(); }
	TArray<FString> Log;

private:
	struct FVoice
	{
		TWeakObjectPtr<UAudioComponent> Comp;
		TSharedPtr<SSXCrowdSeq::FPlayer> Player;
		FVector3f Position = FVector3f::ZeroVector;
		int32 Volume = 0;
	};
	struct FNoteVoice
	{
		TWeakObjectPtr<UAudioComponent> Comp;
		int32 Owner = -1;
		int32 Volume127 = 0;
		int32 Level = 0;
	};
	int32 StartVoice(const FSSXCrowdStart& S);
	int32 StartNote(int32 Owner, const SSXCrowdSeq::FNoteOn& N);
	UAudioComponent* Spawn(const FString& Id, const FVector3f& Pos, bool bLoopFromManifest, float Pitch);

	TWeakObjectPtr<USSXAudioSubsystem> AudioRef;
	TWeakObjectPtr<UWorld> WorldRef;
	TWeakObjectPtr<USoundAttenuation> Spatial;
	TMap<int32, FVoice> Voices;
	TMap<int32, FNoteVoice> Notes;
	int32 NextHandle = 0, NextNote = 0;
	double TickAccMs = 0.0;
	int32 ListenerCharacter = 0, ListenerRank = 0;
	float ListenerTimeLeft = 0.f, ListenerReward = 0.f;
	bool bBegun = false;
};
