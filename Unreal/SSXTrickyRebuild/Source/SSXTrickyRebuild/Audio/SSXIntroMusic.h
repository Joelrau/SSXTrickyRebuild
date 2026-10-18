// TODO: the queue calls pass 1000 as a parameter (crossfade / gap, units unknown);
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "SSXIntroMusic.generated.h"

struct FSSXPcm;

class SSXTRICKYREBUILD_API FSSXSegmentQueuePlayer
{
public:
	FSSXSegmentQueuePlayer(int32 InRate, int32 InChannels) : Rate(InRate), Channels(InChannels) {}

	void Queue(TSharedPtr<const FSSXPcm> Segment);
	void ClearQueue();
	void Stop();
	int32 GetQueueLength() const { return QueueLength.load(); }
	int64 GetRemainingFrames();
	void Generate(int16* Out, int32 Frames);

	const int32 Rate;
	const int32 Channels;

private:
	FCriticalSection Lock;
	TArray<TSharedPtr<const FSSXPcm>> Pending;
	TSharedPtr<const FSSXPcm> Current;
	int64 Pos = 0;
	std::atomic<int32> QueueLength{ 0 };
};

UCLASS()
class SSXTRICKYREBUILD_API USSXSegmentQueueWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	void Init(TSharedPtr<FSSXSegmentQueuePlayer> InPlayer);
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override
	{
		return Audio::EAudioMixerStreamDataFormat::Int16;
	}

private:
	TSharedPtr<FSSXSegmentQueuePlayer> Player;
};

namespace SSXIntroMusic
{
	SSXTRICKYREBUILD_API FString SegmentSuffix(int32 Index);
}
