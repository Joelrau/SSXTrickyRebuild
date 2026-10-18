// 0x2209C8 / 0x220CE8 / 0x2212A0 / 0x21FC98
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXCrowdSequencer.h"

struct FSSXInfFile;

namespace SSXCrowd
{
	constexpr int32 MaxEntries = 15;      // crowd +0x20, 12 bytes each
	constexpr int32 MaxRecords = 3;       // crowd +0xE0, 0x30 bytes each
	constexpr int32 MaxPerList = 5;       // 0x21ED30
	constexpr int32 Levels = 3;
	constexpr float NoFade = 0.f;

	inline int32 CueLevel(double X) { return X <= 0.0 ? -1 : (X < 0.25 ? 0 : (X < 0.65 ? 1 : 2)); }
}

struct SSXTRICKYREBUILD_API FSSXCrowdStart
{
	int32 Listener = 0;
	int32 SoundIndex = -1;
	const SSXCrowdSeq::FSequence* Sequence = nullptr;
	int32 Volume = 0;
	FVector3f Position = FVector3f::ZeroVector;  // game cm (follows SetPosition)
};

class SSXTRICKYREBUILD_API FSSXCrowdSystem
{
public:
	struct FRecord
	{
		TArray<int32> Lists[3];
		float Attack = 0.f, Decay = 0.f, MaxBend = 0.f;
	};
	struct FEntry { bool bUsed = false; FVector3f Position = FVector3f::ZeroVector; int32 Weight = 0; int32 Record = -1; };
	struct FVoiceRec { int32 Index = -1; int32 Handle = -1; int32 Refs = 0; FVector3f Position = FVector3f::ZeroVector; };
	struct FCrowd
	{
		int32 Listener = -1;
		FVector3f Position = FVector3f::ZeroVector;
		FEntry Entries[SSXCrowd::MaxEntries];
		int32 Count = 0;
		FVoiceRec Records[SSXCrowd::MaxRecords];
		int32 Voices = 0;
		int32 Reaction = -1;
		int32 Volume = 0;
		int32 ArmState = 0;
		int32 LastLevel = 3;
		float Hold = 0.f;
		int32 State = 0;
	};

	// 0x20C200
	TFunction<int32(const FSSXCrowdStart&)> Start;
	TFunction<bool(int32 Handle)> IsPlaying;
	TFunction<void(int32 Handle, int32 Volume)> SetVolume;
	TFunction<void(int32 Handle, const FVector3f& Pos)> SetPosition;
	TFunction<void(int32 Handle, float Fade)> Stop;
	// 0x23D4A8
	TFunction<uint32()> Rand;
	// 0x20EA38
	TFunction<int32(int32 Listener)> Anticipation;

	// 0x220CE8
	void LoadConfig(const FSSXInfFile& Inf, const FString& TrackSection, TFunctionRef<bool(const FString& Path, TArray<uint8>& Out)> LoadFile);
	// 0x2209C8
	void Begin(int32 NumCrowds);
	// 0x220BB8 / 0x221330
	void End();
	/** 0x221330: every crowd's used entries removed with fade 0 (0x220338 -> 0x2201F8); the crowds stay. Part of the sound manager's stop-all (0x2213C8). */
	void RemoveAllEntries();

	// 0x2212A0
	int32 AddEmitter(int32 Listener, const FVector3f& Pos, int32 Weight, int32 SoundIndex, bool bPositioned = true);
	// 0x221000
	void SetEmitterWeight(int32 Handle, int32 Weight);
	// 0x220F98
	void RemoveEmitter(int32 Handle, float Fade);

	// 0x220968
	void Frame();
	// 0x217D50 / 0x2181E8
	void Cue(int32 Listener, double X, bool bBail);
	void Cheer(int32 Listener, int32 Level);
	void Groan(int32 Listener, int32 Level);
	void Neutral(int32 Listener);
	// 0x2210A8 / 0x2206C8
	void Arm(int32 Listener);
	// 0x20EA38 / 0x155410
	static int32 AnticipationLevel(float TimeLeft, float PendingReward);
	// 0x2213F8
	int32 Level(int32 Listener) const;

	const FRecord& GetRecord(int32 Level) const { return Records[Level]; }
	const TArray<SSXCrowdSeq::FSequence>& GetSequences() const { return Sequences; }
	const FCrowd* GetCrowd(int32 Listener) const { return Crowds.IsValidIndex(Listener) ? &Crowds[Listener] : nullptr; }
	int32 NumCrowds() const { return Crowds.Num(); }

private:
	void Reaction(FCrowd& C, int32 Level, int32 List, bool bGroan);
	void StopReaction(FCrowd& C);           // 0x220720
	void RemoveEntry(FCrowd& C, int32 E, float Fade);   // 0x2201F8
	void CrowdFrame(FCrowd& C);             // 0x21EFB0
	int32 Pick(const TArray<int32>& List);  // 0x21ED98

	FRecord Records[SSXCrowd::Levels];
	TArray<SSXCrowdSeq::FSequence> Sequences;
	TArray<FCrowd> Crowds;
};
