#include "Audio/SSXPathfinder.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 FadeInFrames = 256;
	constexpr int32 MaxPassThrough = 256;
}

bool FSSXPcm::LoadWav(const FString& Path, FSSXPcm& Out, FString* OutError)
{
	auto Fail = [OutError, &Path](const TCHAR* Msg)
	{
		if (OutError) { *OutError = FString::Printf(TEXT("%s: %s"), *Path, Msg); }
		return false;
	};
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return Fail(TEXT("cannot read"));
	}
	if (Bytes.Num() < 12 || FMemory::Memcmp(Bytes.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(Bytes.GetData() + 8, "WAVE", 4) != 0)
	{
		return Fail(TEXT("not a RIFF/WAVE file"));
	}
	auto U16 = [&Bytes](int64 P) { return uint16(Bytes[P] | (Bytes[P + 1] << 8)); };
	auto U32 = [&Bytes](int64 P) { return uint32(Bytes[P]) | (uint32(Bytes[P + 1]) << 8) | (uint32(Bytes[P + 2]) << 16) | (uint32(Bytes[P + 3]) << 24); };
	int64 P = 12;
	int32 Format = 0, Bits = 0;
	Out = FSSXPcm();
	while (P + 8 <= Bytes.Num())
	{
		const uint32 Size = U32(P + 4);
		const int64 Body = P + 8;
		if (FMemory::Memcmp(Bytes.GetData() + P, "fmt ", 4) == 0 && Body + 16 <= Bytes.Num())
		{
			Format = U16(Body);
			Out.Channels = U16(Body + 2);
			Out.Rate = int32(U32(Body + 4));
			Bits = U16(Body + 14);
		}
		else if (FMemory::Memcmp(Bytes.GetData() + P, "data", 4) == 0)
		{
			if (Format != 1 || Bits != 16 || Out.Channels <= 0)
			{
				return Fail(TEXT("only 16-bit PCM is supported"));
			}
			const int64 Avail = FMath::Min<int64>(Size, Bytes.Num() - Body);
			Out.Samples.SetNumUninitialized(int32(Avail / 2));
			FMemory::Memcpy(Out.Samples.GetData(), Bytes.GetData() + Body, Out.Samples.Num() * 2);
			return true;
		}
		P = Body + Size + (Size & 1);
	}
	return Fail(TEXT("no data chunk"));
}

bool FSSXPathfinderSong::LoadJson(const FString& Path, FString* OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s"), *Path); }
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		if (OutError) { *OutError = FString::Printf(TEXT("bad JSON in %s"), *Path); }
		return false;
	}
	Name = Root->GetStringField(TEXT("song"));
	Rate = int32(Root->GetNumberField(TEXT("rate")));
	Channels = int32(Root->GetNumberField(TEXT("channels")));
	SegmentsWav = Root->GetStringField(TEXT("segments_wav"));

	Nodes.Reset();
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("nodes")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FSSXPathfinderNode N;
		N.Segment = int32(O->GetNumberField(TEXT("segment")));
		N.Section = int32(O->GetNumberField(TEXT("section")));
		N.bSectionStart = O->GetBoolField(TEXT("section_start"));
		N.Marker = int32(O->GetNumberField(TEXT("marker")));
		double LoopByte = 0.0;
		if (O->TryGetNumberField(TEXT("loop"), LoopByte))
		{
			N.Loop = int32(LoopByte);
			bHasLoopField = true;
		}
		for (const TSharedPtr<FJsonValue>& B : O->GetArrayField(TEXT("branches")))
		{
			const TArray<TSharedPtr<FJsonValue>>& T = B->AsArray();
			if (T.Num() == 3)
			{
				N.Branches.Add({ int32(T[0]->AsNumber()), int32(T[1]->AsNumber()), int32(T[2]->AsNumber()) });
			}
		}
		Nodes.Add(MoveTemp(N));
	}
	Events.Reset();
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("events")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FSSXPathfinderEvent E;
		E.Op = int32(O->GetNumberField(TEXT("op")));
		E.bImmediate = O->GetBoolField(TEXT("immediate"));
		E.Node = int32(O->GetNumberField(TEXT("node")));
		Events.Add(E);
	}
	Remaps.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Lists = nullptr;
	if (Root->TryGetArrayField(TEXT("control_lists"), Lists))
	{
		for (const TSharedPtr<FJsonValue>& L : *Lists)
		{
			// u16 pairs (to, from): the engine reads them as u32 from << 16 | to
			TArray<TPair<int32, int32>>& R = Remaps.AddDefaulted_GetRef();
			const TArray<TSharedPtr<FJsonValue>>& U = L->AsArray();
			for (int32 I = 0; I + 1 < U.Num(); I += 2)
			{
				R.Add({ int32(U[I + 1]->AsNumber()), int32(U[I]->AsNumber()) });
			}
		}
	}
	Segments.Reset();
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("segments")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FSSXPathfinderSegment S;
		S.StartFrame = int64(O->GetNumberField(TEXT("start_sample")));
		S.Frames = int32(O->GetNumberField(TEXT("samples")));
		Segments.Add(S);
	}
	for (const FSSXPathfinderNode& N : Nodes)
	{
		if (N.Segment >= Segments.Num())
		{
			if (OutError) { *OutError = TEXT("node references a missing segment"); }
			return false;
		}
	}
	BuildMainPath();
	return Nodes.Num() > 0 && Segments.Num() > 0;
}

int32 FSSXPathfinderSong::NextNode(int32 Node, int32 Control) const
{
	if (!Nodes.IsValidIndex(Node) || Nodes[Node].Branches.Num() == 0)
	{
		return -1;
	}
	for (const FSSXPathfinderBranch& B : Nodes[Node].Branches)
	{
		if (Control >= B.Lo && Control <= B.Hi)
		{
			return B.Next;
		}
	}
	return -1;   // FUN_002BF038
}

int32 FSSXPathfinderSong::Remap(int32 From, int32 Next) const
{
	if (!Nodes.IsValidIndex(From) || Nodes[From].Marker <= 0 || !Remaps.IsValidIndex(Nodes[From].Marker - 1))
	{
		return Next;
	}
	for (const TPair<int32, int32>& P : Remaps[Nodes[From].Marker - 1])
	{
		if (Next == P.Key)
		{
			Next = P.Value;
		}
	}
	return Next;
}

void FSSXPathfinderSong::BuildMainPath()
{
	MainPathPos.Init(-1, Nodes.Num());
	if (Events.Num() == 0 || !Nodes.IsValidIndex(Events[0].Node))
	{
		return;
	}
	int32 N = Events[0].Node;
	int32 Pos = 0;
	while (Nodes.IsValidIndex(N) && MainPathPos[N] < 0)
	{
		MainPathPos[N] = Pos++;
		N = NextNode(N, 64);
	}
}

FSSXPathfinderPlayer::FSSXPathfinderPlayer(TSharedPtr<const FSSXPathfinderSong> InSong, TSharedPtr<const FSSXPcm> InPcm)
	: Song(MoveTemp(InSong)), Pcm(MoveTemp(InPcm))
{
}

void FSSXPathfinderPlayer::PostEvent(int32 Event, int32 Priority)
{
	FScopeLock Lock(&EventLock);
	Inbox.Add({ Event, Priority });
}

void FSSXPathfinderPlayer::ApplyEvent(const FSSXPathfinderEvent& E)
{
	// FUN_002BF508
	if (E.Acts() && Song->Nodes.IsValidIndex(E.Node))
	{
		GoTo(Node, E.Node);   // FUN_002BF108
		FadeInLeft = FadeInFrames;
	}
	else
	{
		// TODO: an acting action without a target node (0xFFFF) is not in the race / menu songs;
		bPlaying.store(false);
		Node = SegIndex = -1;
		CurrentNode.store(-1);
	}
}

int32 FSSXPathfinderPlayer::CountedNext(int32 N)
{
	int32 Ctl = Control.load();
	if (N >= 0 && N == LoopNode)
	{
		// FUN_002BF038
		Ctl = LoopCount & 0x7F;
		if (Song->Nodes.IsValidIndex(N) && Song->Nodes[N].Loop != 0)
		{
			if (LoopCount == 0) { LoopNode = -1; }
			--LoopCount;
		}
	}
	return Song->NextNode(N, Ctl);
}

void FSSXPathfinderPlayer::GoTo(int32 From, int32 Next)
{
	int32 N = Song->Remap(From, Next);
	for (int32 Guard = 0; Guard < MaxPassThrough; ++Guard)
	{
		if (!Song->Nodes.IsValidIndex(N))
		{
			bReachedEnd.store(N < 0);
			bPlaying.store(false);
			Node = SegIndex = -1;
			CurrentNode.store(-1);
			return;
		}
		const FSSXPathfinderNode& Nd = Song->Nodes[N];
		if (Nd.Segment >= 0 && Song->Segments[Nd.Segment].Frames > 0)
		{
			Node = N;
			SegIndex = Nd.Segment;
			SegPos = 0;
			bPlaying.store(true);
			bReachedEnd.store(false);
			CurrentNode.store(N);
			CurrentSection.store(Nd.Section);
			if (Song->MainPathPos.IsValidIndex(N) && Song->MainPathPos[N] >= 0)
			{
				LastMainPos.store(Song->MainPathPos[N]);
			}
			SegmentsPlayed.fetch_add(1);
			return;
		}
		// FUN_002BF108
		if (Nd.Loop != 0 && LoopNode != N)
		{
			LoopCount = uint8(Nd.Loop);
			LoopNode = N;
		}
		N = Song->Remap(From, CountedNext(N));
	}
	bPlaying.store(false);
}

void FSSXPathfinderPlayer::Advance()
{
	if (Queue.Num() > 0)
	{
		// FUN_002C0488
		const FQueued Head = Queue[0];
		Queue.RemoveAt(0);
		QueueNum.store(Queue.Num());
		const FSSXPathfinderEvent& Ev = Song->Events[Head.Event];
		if (Ev.Acts())
		{
			ApplyEvent(Ev);
			FadeInLeft = 0;
			return;
		}
		// FUN_002BF508
	}
	// FUN_002BFBB8 / FUN_002BF038 / FUN_002BF108
	const int32 From = Node;
	GoTo(From, CountedNext(From));
}

void FSSXPathfinderPlayer::Generate(int16* Out, int32 Frames)
{
	const int32 Ch = FMath::Max(Song->Channels, 1);
	{
		FScopeLock Lock(&EventLock);
		if (bCancelPending)
		{
			Queue.Reset();
			bCancelPending = false;
		}
		for (const FQueued& Q : Inbox)
		{
			// FUN_002C15C8
			if (!Song->Events.IsValidIndex(Q.Event))
			{
				continue;
			}
			int32 At = 0;
			while (At < Queue.Num() && Q.Priority <= Queue[At].Priority) { ++At; }
			if (At < QueueSize)
			{
				Queue.Insert(Q, At);
				if (Queue.Num() > QueueSize) { Queue.Pop(); }
			}
		}
		Inbox.Reset();
		QueueNum.store(Queue.Num());
	}
	// FUN_002C0488
	if (Queue.Num() > 0)
	{
		const FSSXPathfinderEvent& Ev = Song->Events[Queue[0].Event];
		if (Ev.bImmediate || !bPlaying.load())
		{
			Queue.RemoveAt(0);
			QueueNum.store(Queue.Num());
			if (Ev.Acts())
			{
				ApplyEvent(Ev);
			}
		}
	}
	if (bPaused.load())
	{
		// FUN_002243E8
		FMemory::Memzero(Out, int64(Frames) * Ch * sizeof(int16));
		return;
	}

	int32 Done = 0;
	while (Done < Frames)
	{
		if (!bPlaying.load() || SegIndex < 0 || !Pcm.IsValid())
		{
			FMemory::Memzero(Out + Done * Ch, (Frames - Done) * Ch * sizeof(int16));
			return;
		}
		const FSSXPathfinderSegment& Seg = Song->Segments[SegIndex];
		const int32 N = FMath::Min(Frames - Done, Seg.Frames - SegPos);
		const int64 Src = (Seg.StartFrame + SegPos) * Ch;
		const int64 Avail = FMath::Max<int64>(0, FMath::Min<int64>(int64(N) * Ch, Pcm->Samples.Num() - Src));
		if (Avail > 0)
		{
			FMemory::Memcpy(Out + Done * Ch, Pcm->Samples.GetData() + Src, Avail * sizeof(int16));
		}
		if (Avail < int64(N) * Ch)
		{
			FMemory::Memzero(Out + Done * Ch + Avail, (int64(N) * Ch - Avail) * sizeof(int16));
		}
		if (FadeInLeft > 0)
		{
			for (int32 F = 0; F < N && FadeInLeft > 0; ++F, --FadeInLeft)
			{
				const float G = 1.f - float(FadeInLeft) / FadeInFrames;
				for (int32 C = 0; C < Ch; ++C)
				{
					int16& S = Out[(Done + F) * Ch + C];
					S = int16(S * G);
				}
			}
		}
		Done += N;
		SegPos += N;
		FramesPlayed.fetch_add(N);
		if (SegPos >= Seg.Frames)
		{
			Advance();
		}
	}
}

void USSXPathfinderWave::Init(TSharedPtr<FSSXPathfinderPlayer> InPlayer)
{
	Player = MoveTemp(InPlayer);
	const FSSXPathfinderSong& Song = Player->GetSong();
	SetSampleRate(Song.Rate);
	NumChannels = Song.Channels;
	Duration = INDEFINITELY_LOOPING_DURATION;
	SoundGroup = SOUNDGROUP_Music;
	bLooping = false;
	// 0x2D5148
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
}

int32 USSXPathfinderWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
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
