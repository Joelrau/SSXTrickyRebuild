// FUN_002BFBB8 / FUN_002BF108 / FUN_002BF038 / FUN_002BF290
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "SSXPathfinder.generated.h"

struct SSXTRICKYREBUILD_API FSSXPcm
{
	int32 Rate = 0;
	int32 Channels = 0;
	TArray<int16> Samples;
	int64 NumFrames() const { return Channels > 0 ? Samples.Num() / Channels : 0; }

	static bool LoadWav(const FString& Path, FSSXPcm& Out, FString* OutError = nullptr);
};

struct FSSXPathfinderBranch
{
	int32 Lo = 0;
	int32 Hi = 127;
	int32 Next = 0;
};

struct FSSXPathfinderNode
{
	int32 Segment = -1;   // -1 = no audio (section start / control node)
	int32 Section = 0;
	bool bSectionStart = false;
	int32 Marker = 0;   // FUN_002BF290: 1-based remap list, 0 = none
	int32 Loop = 0;   // FUN_002BF108 / FUN_002BF038: loop counter start, 0 = none
	TArray<FSSXPathfinderBranch> Branches;
};

struct FSSXPathfinderEvent
{
	// FUN_002BF490 / FUN_002BF508
	int32 Op = 0;
	bool Acts() const { return (Op & 3) != 0; }
	bool bImmediate = false;
	int32 Node = -1;
};

struct FSSXPathfinderSegment
{
	int64 StartFrame = 0;
	int32 Frames = 0;
};

struct SSXTRICKYREBUILD_API FSSXPathfinderSong
{
	FString Name;
	int32 Rate = 0;
	int32 Channels = 0;
	FString SegmentsWav;
	TArray<FSSXPathfinderNode> Nodes;
	TArray<FSSXPathfinderEvent> Events;
	TArray<FSSXPathfinderSegment> Segments;
	TArray<int32> MainPathPos;
	// FUN_002BF290
	TArray<TArray<TPair<int32, int32>>> Remaps;
	bool bHasLoopField = false;   // converted with the node byte 9 ("loop")

	bool LoadJson(const FString& Path, FString* OutError = nullptr);
	// FUN_002BF038
	int32 NextNode(int32 Node, int32 Control) const;
	// FUN_002BF290
	int32 Remap(int32 From, int32 Next) const;
	void BuildMainPath();
};

class SSXTRICKYREBUILD_API FSSXPathfinderPlayer
{
public:
	FSSXPathfinderPlayer(TSharedPtr<const FSSXPathfinderSong> InSong, TSharedPtr<const FSSXPcm> InPcm);

	/** Post a game event (0..events-1) to the engine queue (FUN_002C15C8): 16 entries ordered by Priority (higher first, FIFO among equals; a full queue drops the post); 0 = FUN_00225318, 1 = FUN_00225380. Only the head is worked on (FUN_002C0488): immediate (action flags & 0x80) acts at once, others at the end of the current segment (a no-action event too); the next waits. */
	void PostEvent(int32 Event, int32 Priority = 0);
	int32 GetQueuedEvents() const { FScopeLock Lock(&EventLock); return Inbox.Num() + QueueNum.load(); }
	void SetControl(int32 Value) { Control.store(FMath::Clamp(Value, 0, 127)); }
	int32 GetControl() const { return Control.load(); }
	// FUN_002243E8 / FUN_00224420
	void SetPaused(bool bInPaused) { bPaused.store(bInPaused); }
	bool IsPaused() const { return bPaused.load(); }

	void Generate(int16* Out, int32 Frames);

	bool IsPlaying() const { return bPlaying.load(); }
	bool ReachedEnd() const { return bReachedEnd.load(); }
	int32 GetCurrentNode() const { return CurrentNode.load(); }
	double GetPlayedSeconds() const { return Song->Rate > 0 ? double(FramesPlayed.load()) / Song->Rate : 0.0; }
	const FSSXPathfinderSong& GetSong() const { return *Song; }

private:
	void ApplyEvent(const FSSXPathfinderEvent& E);
	/** FUN_002BF108: From's remap of Next, then through the nodes without audio (loop counters, remaps) to an audio node, entered;
	 *  stops if the graph ends. */
	void GoTo(int32 From, int32 Next);
	// FUN_002BF038
	int32 CountedNext(int32 N);
	void Advance();

	TSharedPtr<const FSSXPathfinderSong> Song;
	TSharedPtr<const FSSXPcm> Pcm;

	mutable FCriticalSection EventLock;
	struct FQueued { int32 Event = -1; int32 Priority = 0; };
	TArray<FQueued> Inbox;
	bool bCancelPending = false;
	TArray<FQueued> Queue;          // audio thread: the engine queue (+0x34, 16 x 0x14 bytes)
	std::atomic<int32> QueueNum{ 0 };
	static constexpr int32 QueueSize = 16;

	int32 Node = -1;
	int32 SegIndex = -1;
	int32 SegPos = 0;
	int32 FadeInLeft = 0;
	int32 LoopNode = -1;
	uint8 LoopCount = 0;

	std::atomic<int32> Control{ 64 };
	std::atomic<bool> bPlaying{ false };
	std::atomic<bool> bPaused{ false };
	std::atomic<bool> bReachedEnd{ false };
	std::atomic<int32> CurrentNode{ -1 };
	std::atomic<int32> CurrentSection{ -1 };
	std::atomic<int32> LastMainPos{ -1 };
	std::atomic<int32> SegmentsPlayed{ 0 };
	std::atomic<int64> FramesPlayed{ 0 };
};

UCLASS()
class SSXTRICKYREBUILD_API USSXPathfinderWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	void Init(TSharedPtr<FSSXPathfinderPlayer> InPlayer);
	TSharedPtr<FSSXPathfinderPlayer> GetPlayer() const { return Player; }

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override
	{
		return Audio::EAudioMixerStreamDataFormat::Int16;
	}

private:
	TSharedPtr<FSSXPathfinderPlayer> Player;
};
