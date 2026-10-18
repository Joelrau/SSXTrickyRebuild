#include "Audio/SSXIntroMusic.h"

#include "Audio/SSXPathfinder.h"
#include "Misc/ScopeLock.h"

void FSSXSegmentQueuePlayer::Queue(TSharedPtr<const FSSXPcm> Segment)
{
	if (!Segment.IsValid() || Segment->Channels != Channels || Segment->Rate != Rate)
	{
		return;
	}
	FScopeLock L(&Lock);
	Pending.Add(MoveTemp(Segment));
	QueueLength.store(Pending.Num() + (Current.IsValid() ? 1 : 0));
}

void FSSXSegmentQueuePlayer::ClearQueue()
{
	FScopeLock L(&Lock);
	Pending.Reset();
	QueueLength.store(Current.IsValid() ? 1 : 0);
}

int64 FSSXSegmentQueuePlayer::GetRemainingFrames()
{
	FScopeLock L(&Lock);
	int64 N = Current.IsValid() ? FMath::Max<int64>(Current->NumFrames() - Pos, 0) : 0;
	for (const TSharedPtr<const FSSXPcm>& P : Pending)
	{
		N += P.IsValid() ? P->NumFrames() : 0;
	}
	return N;
}

void FSSXSegmentQueuePlayer::Stop()
{
	FScopeLock L(&Lock);
	Pending.Reset();
	Current.Reset();
	Pos = 0;
	QueueLength.store(0);
}

void FSSXSegmentQueuePlayer::Generate(int16* Out, int32 Frames)
{
	FScopeLock L(&Lock);
	int32 Done = 0;
	while (Done < Frames)
	{
		if (!Current.IsValid())
		{
			if (Pending.Num() == 0)
			{
				FMemory::Memzero(Out + int64(Done) * Channels, int64(Frames - Done) * Channels * sizeof(int16));
				break;
			}
			Current = Pending[0];
			Pending.RemoveAt(0);
			Pos = 0;
		}
		const int64 N = FMath::Min<int64>(Frames - Done, Current->NumFrames() - Pos);
		if (N > 0)
		{
			FMemory::Memcpy(Out + int64(Done) * Channels, Current->Samples.GetData() + Pos * Channels, N * Channels * sizeof(int16));
			Done += int32(N);
			Pos += N;
		}
		if (Pos >= Current->NumFrames())
		{
			Current.Reset();
		}
	}
	QueueLength.store(Pending.Num() + (Current.IsValid() ? 1 : 0));
}

void USSXSegmentQueueWave::Init(TSharedPtr<FSSXSegmentQueuePlayer> InPlayer)
{
	Player = MoveTemp(InPlayer);
	SetSampleRate(Player->Rate);
	NumChannels = Player->Channels;
	Duration = INDEFINITELY_LOOPING_DURATION;
	SoundGroup = SOUNDGROUP_Music;
	bLooping = false;
	// 0x2D5148
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
}

int32 USSXSegmentQueueWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	if (!Player.IsValid() || NumChannels <= 0)
	{
		return 0;
	}
	const int32 Frames = NumSamples / NumChannels;
	OutAudio.SetNumUninitialized(Frames * NumChannels * sizeof(int16));
	Player->Generate(reinterpret_cast<int16*>(OutAudio.GetData()), Frames);
	return Frames * NumChannels;
}

FString SSXIntroMusic::SegmentSuffix(int32 Index)
{
	if (Index >= 0 && Index < 4) return FString::Printf(TEXT("-A%d"), Index + 1);
	if (Index >= 4 && Index < 8) return FString::Printf(TEXT("-B%d"), Index - 3);
	if (Index >= 8 && Index < 16) return FString::Printf(TEXT("-C%d"), Index - 7);
	return TEXT("end");
}
