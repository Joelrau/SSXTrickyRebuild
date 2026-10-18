#include "Race/SSXReplay.h"

using namespace SSXReplay;

// 0x110BF8

int32 SSXReplay::PlayerOfRacer(TFunctionRef<bool(int32)> IsHuman, int32 NumRacers, int32 Racer)
{
	if (Racer < 0 || Racer >= NumRacers || !IsHuman(Racer)) return -1;
	int32 Player = 0;
	for (int32 I = 0; I < Racer; ++I) { Player += IsHuman(I) ? 1 : 0; }
	return Player;
}

int32 SSXReplay::RacerOfPlayer(TFunctionRef<bool(int32)> IsHuman, int32 NumRacers, int32 Player)
{
	int32 First = -1;
	for (int32 I = 0, P = 0; I < NumRacers; ++I)
	{
		if (!IsHuman(I)) continue;
		if (P == Player) return I;
		if (First < 0) First = I;
		++P;
	}
	return FMath::Max(0, First);   // 0x1785C0
}

// 0x194D78, 0x1953C8

bool FCameraTrack::Insert(uint32 Frame, const FCamera& Cam, bool bAdvanced)
{
	if (Frame == 0)
	{
		Single = Cam;
	}
	for (int32 I = 0; I < Count; ++I)
	{
		if (Cuts[I].Frame == Frame)
		{
			Cuts[I].Cam = Cam;
			Seek(Frame, bAdvanced);
			return false;
		}
	}
	if (Count >= MaxCuts)
	{
		return false;
	}
	int32 At = Count;
	for (int32 I = Count - 1; I >= 0; --I)
	{
		if (Cuts[I].Frame < Frame) { At = I + 1; break; }
	}
	FCut C;
	C.Cam = Cam;
	C.Frame = Frame;
	Cuts.Insert(C, At);
	Count = Cuts.Num();
	Seek(Frame, bAdvanced);
	return true;
}

void FCameraTrack::Set(uint32 Frame, const FCamera& Cam, bool bAdvanced)
{
	if (!bAdvanced)
	{
		Single = Cam;
		return;
	}
	int32 I = Count;
	do
	{
		if (--I < 0) return;
	} while (Frame < Cuts[I].Frame);
	Cuts[I].Cam = Cam;
}

bool FCameraTrack::Delete(uint32 Frame, bool bAdvanced)
{
	if (!bAdvanced || Count <= 1) return false;
	for (int32 I = 1; I < Count; ++I)
	{
		if (Frame <= Cuts[I].Frame)
		{
			Cuts.RemoveAt(I);
			Count = Cuts.Num();
			return true;
		}
	}
	return false;
}

void FCameraTrack::Seek(uint32 Frame, bool bAdvanced)
{
	if (!bAdvanced) { Cur = 0; return; }
	int32 I = Count;
	do
	{
		if (--I < 0) { Cur = Count - 1; return; }
	} while (Frame < Cuts[I].Frame);
	Cur = I;
}

bool FCameraTrack::Advance(uint32 Frame, bool bAdvanced)
{
	if (!bAdvanced) return false;
	const int32 N = Cur + 1;
	if (Count < 1 || N >= Count) return false;
	if (Cuts[N].Frame <= Frame) { Cur = N; return true; }
	return false;
}

const FCamera& FCameraTrack::Current(bool bAdvanced) const
{
	return (bAdvanced && Cuts.IsValidIndex(Cur)) ? Cuts[Cur].Cam : Single;
}

// 0x1928F0, 0x192880, 0x192998, 0x192758

void FInputRecorder::Clear()
{
	// 0x194550
	if (Words.Num() > 0)
	{
		Image(MemWords, MemRuns);
	}
	Words.Reset();
	Runs.Reset();
	Read = 0;
	Left = 0;
}

void FInputRecorder::Image(TArray<uint32>& OutWords, TArray<uint8>& OutRuns) const
{
	if (MemWords.Num() == MaxRuns && MemRuns.Num() == MaxRuns)
	{
		OutWords = MemWords;
		OutRuns = MemRuns;
	}
	else
	{
		// the allocator fill 0xDEADC0DE over the one buffer (words +0, run bytes +0x6664: the pattern by address)
		OutWords.Init(0xDEADC0DEu, MaxRuns);
		OutRuns.SetNumUninitialized(MaxRuns);
		static const uint8 Fill[4] = { 0xDE, 0xC0, 0xAD, 0xDE };
		for (int32 I = 0; I < MaxRuns; ++I) { OutRuns[I] = Fill[(0x6664 + I) & 3]; }
	}
	for (int32 I = 0; I < Words.Num() && I < MaxRuns; ++I)
	{
		OutWords[I] = Words[I];
		OutRuns[I] = Runs[I];
	}
}

bool FInputRecorder::Record(uint32 Wd)
{
	const int32 N = Words.Num();
	if (N >= 1 && Words[N - 1] == Wd && Runs[N - 1] != 0xFF)
	{
		++Runs[N - 1];
		return true;
	}
	if (N > MaxRuns - 1)   // 0x192880
	{
		return false;
	}
	Runs.Add(1);
	Words.Add(Wd);
	return true;
}

bool FInputRecorder::Play(uint32& InOut)
{
	InOut = 0;
	if (Read >= Words.Num()) return false;
	if (Left < 1) Left = Runs[Read];
	InOut = Words[Read];
	if (--Left < 1) ++Read;
	return true;
}

bool FInputRecorder::Seek(uint32 Frame)
{
	const int32 N = Words.Num();
	if (N <= 0) return false;
	int32 I = 0;
	uint32 Acc = Runs[0];
	while (Acc <= Frame)
	{
		if (++I >= N) return false;
		Acc += Runs[I];
	}
	Read = I;
	Left = int32(Acc - Frame);
	return true;
}

void FSSXReplay::Init(int32 Racers, int32 Humans, bool bInAdvanced, IReplayWorld* InWorld)
{
	World = InWorld;
	bAdvanced = bInAdvanced;
	const int32 Size = Racers * 0xC00 + 0x8000;
	const int32 Pool = FMath::Min(16, 0xC9800 / Size);
	Keys.SetNum(Pool);
	LockInterval = ((uint32(4 * 0xE10) / uint32(FMath::Max(1, Pool - 10))) / 10) * 10;
	if (Humans == 1) { QuotaSlots = 4; QuotaTricks = 3; QuotaWipeOuts = 1; }
	else { QuotaSlots = 2; QuotaTricks = 1; QuotaWipeOuts = 1; }
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		Hl[R] = FHighlights();
		Hl[R].Rider = R;
		bHasInput[R] = false;
		Input[R].Fresh();
		RiderClips[R].Reset();
	}
	ResetIdle();
}

void FSSXReplay::AttachHuman(int32 Rider)
{
	if (Rider < 0 || Rider >= MaxRiders) return;
	Hl[Rider].bActive = true;
	Hl[Rider].Rider = Rider;
	bHasInput[Rider] = true;
}

void FSSXReplay::ResetIdle()
{
	bRebuild = false;
	RebuildKeys.Reset();
	RebuildNext = 0;
	State = Idle;
	Command = CmdNone;
	bLoop = bLoaded = bFull = bEnded = bStepped = false;
	FullMessage = 0;
	RecFrame = RecLength = PlayFrame = 0;
	EndKey = -1;
	Tail = Current = EvictCursor = -1;
	Playlist.Reset();
	PlaylistCur = 0;
	MarkClip = -1;
	MarkStart = -1;
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		if (bHasInput[R]) Input[R].Clear();
		if (Hl[R].bActive)
		{
			Hl[R].Pending = -1;
			Hl[R].PendingKey = -1;
			for (int32& S : Hl[R].Slots) S = -1;
		}
		for (const int32 C : RiderClips[R]) { Clips[C] = FClip(); }   // 0x1956C8
		RiderClips[R].Reset();
	}
	Head = -1;
	FreeKeys = -1;
	for (int32 K = Keys.Num() - 1; K >= 0; --K)
	{
		Keys[K] = FKeyframe();
		Keys[K].Next = FreeKeys;
		FreeKeys = K;
	}
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		Tracks[R].Reset();
		Tracks[R].Seek(0, bAdvanced);
	}
	FreeClips = -1;
	for (int32 C = ClipPool - 1; C >= 0; --C)
	{
		Clips[C] = FClip();
		Clips[C].NextFree = FreeClips;
		FreeClips = C;
	}
}

bool FSSXReplay::OnEndRaceAttract()
{
	if (!bLoop && State != Attract)
	{
		State = Attract;
		return true;
	}
	return false;
}

void FSSXReplay::EndRecording()
{
	if (!bEnded && !bLoaded) DropPendingHighlights();
	bEnded = true;
}

void FSSXReplay::Update(bool bGamePaused)
{
	if (bGamePaused) return;   // 0x17F0F8
	if (FullMessage > 0) --FullMessage;
	bStepped = false;
	if (Command == CmdPause) State = Paused;
	switch (State)
	{
	case Record:
	case RecordStart:
		if (bRebuild)
		{
			RebuildFrame();
			break;
		}
		State = Record;
		EndKey = -1;
		if (!bEnded)
		{
			if (!bFull && RecFrame % KeyframeEvery == 0)
			{
				Prune(false);
				const int32 K = InsertKeyframe(RecFrame, false);
				if (LockInterval != 0 && RecFrame % LockInterval == 0 && K >= 0) ++Keys[K].Lock;
			}
			else if (RecFrame % 10 == 0)
			{
				W().Periodic10();
			}
			if (!bFull) ++RecLength;
			++RecFrame;
		}
		else
		{
			if (bLoaded) return;
			Prune(true);
			const int32 K = InsertKeyframe(RecFrame, false);
			if (K >= 0) ++Keys[K].Lock;
			State = RecordEnded;
		}
		break;
	default:
		Execute();
		break;
	case ClipPlay:
		if (PlayFrame % 10 == 0)
		{
			State = MarkClipStep() ? Play : Paused;
		}
		Execute();
		return;
	case Paused:
		EndClipMark();
		Execute();
		return;
	case State8:
		Execute();
	case StepFrame:
	case Seeked:
	case SeekedStart:
	case Started:
		State = Paused;
		break;
	case Attract:
		BuildAttract();
		break;
	case Idle:
	case RecordEnded:
		break;
	case StageReset:
		W().StageReset();
		if (!bRebuild) ResetCameraTracks();
		State = RecordStart;
		break;
	}
}

void FSSXReplay::Execute()
{
	auto Tail_ = [this](int32 NewState)
	{
		State = NewState;
		W().StartCameraFlash();
		Command = CmdNone;
	};
	switch (Command)
	{
	case CmdToStart:
		Playlist.Reset();
		Rewind();
		Step(true);
		Tail_(PlayFrame == 0 ? SeekedStart : Seeked);
		return;
	case CmdToEnd:
	{
		Playlist.Reset();
		bStepped = false;
		Current = FindKeyframe(100000000u);
		PlayFrame = Keys[Current].Frame;
		SeekAll(PlayFrame);
		Step(true);
		Tail_(PlayFrame == 0 ? SeekedStart : Seeked);
		return;
	}
	case CmdPrevKey:
	{
		Playlist.Reset();
		const int32 K = FindKeyframe(PlayFrame);
		Current = K;
		const int32 N = Keys[K].Next;
		if (PlayFrame == Keys[K].Frame || (N >= 0 && Keys[N].Frame == PlayFrame))
		{
			// 0x1911E0, 0x190EE8, 0x191100
			const int32 P = PrevKeyframe(PlayFrame);
			Current = P;
			PlayFrame = Keys[P].Frame;
			Current = FindKeyframe(PlayFrame);
			PlayFrame = Keys[Current].Frame;
			SeekAll(PlayFrame);
		}
		Step(true);
		Tail_(PlayFrame == 0 ? SeekedStart : Seeked);
		return;
	}
	case CmdNextKey:
	{
		Playlist.Reset();
		if (Current < 0 || Keys[Current].Next < 0)
		{
			Command = CmdNone;
			return;
		}
		// 0x191190, 0x190E78
		const int32 N = NextKeyframe(PlayFrame);
		Current = N;
		PlayFrame = Keys[N].Frame;
		Current = FindKeyframe(PlayFrame);
		PlayFrame = Keys[Current].Frame;
		SeekAll(PlayFrame);
		Step(true);
		Tail_(Seeked);
		return;
	}
	case CmdStep:
		Playlist.Reset();
		Step(false);
		State = StepFrame;
		break;
	case CmdPlayToggle:
		State = (State == Paused) ? Play : Paused;
		break;
	case CmdPause:
		State = Paused;
		break;
	case CmdClip:
		State = ClipPlay;
		break;
	case CmdNextClip:
	case CmdPrevClip:
		Rewind();
		Step(true);
		State = Seeked;
		break;
	default:
		if (State != Paused)
		{
			bStepped = true;
			Step(false);
		}
		break;
	}
	Command = CmdNone;
}

uint32 FSSXReplay::PlaylistClipEnd() const
{
	if (!Playlist.IsValidIndex(PlaylistCur)) return 0;
	const FClip& C = Clips[Playlist[PlaylistCur]];
	return (Keys.IsValidIndex(C.Key) ? Keys[C.Key].Frame : 0) + uint32(C.Length);
}

void FSSXReplay::Step(bool bRestore)
{
	while (!bRestore)
	{
		if (PlayFrame < RecLength)
		{
			if (HasPlaylist() && PlayFrame >= PlaylistClipEnd())
			{
				if (PlaylistCur >= Playlist.Num() - 1)   // 0x195488
				{
					if (bLoop)
					{
						Focus = FMath::Max(0, W().FocusPlayer());
						Playlist.Reset();
						PlayFrame = 0;
						Rewind();
						Step(true);
						W().SetCameraTarget(Focus);   // 0x178968, 0x178A28
						return;
					}
					State = Paused;
					return;
				}
				++PlaylistCur;
				Rewind();
				Step(true);
			}
			if (Tracks[Focus].Advance(PlayFrame, bAdvanced)) ApplyFocusCamera();
			if (PlayFrame % KeyframeEvery == 0) W().Periodic60();
			else if (PlayFrame % 10 == 0) W().Periodic10();
			++PlayFrame;
			return;
		}
		if (!bLoop)
		{
			PlayFrame = RecLength;
			State = Paused;
			return;
		}
		Playlist.Reset();
		PlayFrame = 0;
		Rewind();
		bRestore = true;
	}
	Current = HasPlaylist() ? Clips[Playlist[PlaylistCur]].Key : FindKeyframe(PlayFrame);
	if (!Keys.IsValidIndex(Current)) return;
	PlayFrame = Keys[Current].Frame;
	SeekAll(PlayFrame);
	W().RestoreKeyframe(Current, PlayFrame);
	W().ResetRiderEffects();   // 0x1128B8
	if (bLoop && HasPlaylist())
	{
		W().SetCameraTarget(Clips[Playlist[PlaylistCur]].Rider);
		W().SetCameraMode(7);                                      // 0x1786D0
		W().StartCameraFlash();
	}
	else
	{
		Tracks[Focus].Seek(PlayFrame, bAdvanced);
		ApplyFocusCamera();
		if (HasPlaylist()) W().StartCameraFlash();
	}
	W().Periodic60();
}

void FSSXReplay::Rewind()
{
	bStepped = false;
	if (!HasPlaylist())
	{
		PlayFrame = 0;
		Current = Head;
	}
	else
	{
		Current = Clips[Playlist[PlaylistCur]].Key;
		PlayFrame = Keys.IsValidIndex(Current) ? Keys[Current].Frame : 0;
	}
	SeekAll(PlayFrame);
}

void FSSXReplay::StartPlayback(int32 PlaylistMarker)
{
	DropPendingHighlights();
	if (!bLoaded && EndKey < 0)
	{
		EndKey = InsertKeyframe(RecFrame, true);
	}
	if (PlaylistMarker < 0) Playlist.Reset();
	PlaylistCur = 0;
	Command = CmdNone;
	bLoop = false;
	State = Started;
	Rewind();
	Step(true);
}

void FSSXReplay::StopPlayback()
{
	if (bLoop)
	{
		Focus = 0;
		W().SetCameraRider(false, 0);   // 0x1785C0
	}
	bLoop = false;
	DropPendingHighlights();
	if (bLoaded)
	{
		return;
	}
	Playlist.Reset();
	PlaylistCur = 0;
	if (EndKey >= 0)
	{
		W().RestoreKeyframe(EndKey, Keys[EndKey].Frame);   // 0x18F6C0
	}
	if (!bEnded)
	{
		if (EndKey >= 0)
		{
			FreeKeyframe(EndKey);
			EndKey = -1;
		}
		W().Periodic60();
		State = Record;
	}
	else
	{
		W().Periodic60();
		State = RecordEnded;
	}
}

void FSSXReplay::BuildAttract()
{
	Playlist.Reset();
	PlaylistCur = 0;
	Focus = FMath::Max(0, W().FocusPlayer());
	const int32 Other = Focus != 0 ? 0 : 1;
	int32 Found = 0;
	auto Collect = [&](int32 Rider, bool bTricks)
	{
		if (Rider >= MaxRiders || !Hl[Rider].bActive) return;
		for (const int32 C : Hl[Rider].Slots)
		{
			if (C >= 0 && ((Clips[C].Type == 1) == bTricks))
			{
				++Found;
				if (Playlist.Num() < MaxPlaylist) Playlist.Add(C);
			}
		}
	};
	Collect(Focus, true);
	Collect(Other, true);
	Collect(Focus, false);
	Collect(Other, false);
	ResetCameraTracks();
	W().SetCameraRider(true, Focus);
	StartPlayback(Found > 0 ? 0 : -1);
	bLoop = true;
	State = Play;
}

void FSSXReplay::ResetCameraTracks()
{
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		Tracks[R].Reset();
		W().SetCameraRider(true, R);
		Tracks[R].Insert(0, W().GetCamera(), bAdvanced);
		W().SetCameraRider(false, R);
		Tracks[R].Seek(0, bAdvanced);
	}
}

void FSSXReplay::ApplyFocusCamera()
{
	W().ApplyCamera(Tracks[Focus].Current(bAdvanced), true);
}

void FSSXReplay::StoreCamera()
{
	Tracks[Focus].Set(PlayFrame, W().GetCamera(), bAdvanced);
}

bool FSSXReplay::DeleteCamera(uint32 Frame)
{
	if (!Tracks[Focus].Delete(Frame, bAdvanced)) return false;
	if (Tracks[Focus].Advance(Frame, bAdvanced)) ApplyFocusCamera();
	return true;
}

int32 FSSXReplay::ClipMarkAllowed() const
{
	if (PlayFrame == RecLength) return -1;
	const TArray<int32>& L = RiderClips[Focus];
	for (const int32 C : L)
	{
		const int32 Start = Keys.IsValidIndex(Clips[C].Key) ? int32(Keys[Clips[C].Key].Frame) : 0;
		const int32 D = int32(PlayFrame) - Start;
		if (-1 < D && D <= Clips[C].Length) return 1;
	}
	return L.Num() < MaxClipsPerRider ? 1 : 0;
}

void FSSXReplay::GetTimeline(FTimeline& Out) const
{
	Out = FTimeline();   // 0x1956B0
	int32 N = 0;
	for (int32 K = Head; K >= 0; K = Keys[K].Next) { ++N; }   // 0x190FF0
	Out.KeyCount = N;
	Out.Length = RecLength;
	int32 I = 0;
	for (int32 K = Head; K >= 0; K = Keys[K].Next, ++I)
	{
		if ((Keys[K].Refs == 0 || K == EndKey) && I < 16) Out.Keys[I] = Keys[K].Frame;
	}
	Out.Clips = RiderClips[Focus];
	Out.Track = &Tracks[Focus];
}

void FSSXReplay::SetPlaylist(const TArray<int32>* List, int32 Cur)
{
	if (List) { Playlist = *List; } else { Playlist.Reset(); }
	PlaylistCur = List ? Cur : 0;
	if (List && List->Num() > 0)
	{
		Rewind();
		Step(true);
	}
}

bool FSSXReplay::IsWorldFrozen() const
{
	switch (State)
	{
	case Play:
	case ClipPlay:
	case Started:
	{
		const bool b = RecLength <= PlayFrame || !bStepped;
		if (HasPlaylist() && PlayFrame >= PlaylistClipEnd()) return true;
		return b;
	}
	case Paused:
	case Seeked:
	case SeekedStart:
	case State8:
	case Attract:
		return true;
	case StepFrame:
		return RecLength <= PlayFrame;
	default:
		return false;
	}
}

bool FSSXReplay::RecordOrPlayInput(int32 Rider, uint32& InOut)
{
	if (Rider < 0 || Rider >= MaxRiders || !bHasInput[Rider]) return false;
	switch (State)
	{
	case Record:
		if (bRebuild) return Input[Rider].Play(InOut);
		if (bEnded || bLoaded || bFull) return false;
		if (!Input[Rider].Record(InOut))
		{
			FullMessage = FullMessageFrames;   // 0x192880
			bFull = true;
		}
		return true;
	case Play:
	case ClipPlay:
	case StepFrame:
		return Input[Rider].Play(InOut);
	default:
		return false;
	}
}

int32 FSSXReplay::AllocKeyframe()
{
	if (FreeKeys < 0) EvictKeyframe();
	const int32 K = FreeKeys;
	if (K < 0) return -1;
	FreeKeys = Keys[K].Next;
	Keys[K].Lock = Keys[K].Refs = 0;
	Keys[K].Next = Keys[K].Prev = -1;
	Keys[K].bInUse = true;
	Keys[K].Snapshot.Reset();
	return K;
}

int32 FSSXReplay::FindEvictable(int32 From) const
{
	if (From < 0 || !Keys[From].bInUse) From = Head;
	if (From < 0) return -1;
	int32 A = Keys[From].Next;
	while (true)
	{
		if (A < 0) return -1;
		if (Keys[A].Refs == 0) break;
		A = Keys[A].Next;
	}
	int32 B = Keys[A].Next;
	if (B < 0) return -1;
	if (Keys[B].Refs == 0) return A;
	for (B = Keys[B].Next; B >= 0; B = Keys[B].Next)
	{
		if (Keys[B].Refs == 0) return A;
	}
	return -1;
}

void FSSXReplay::EvictKeyframe()
{
	int32 A = FindEvictable(EvictCursor);
	if (A < 0 && Head >= 0) A = FindEvictable(Keys[Head].Next);
	if (A < 0) return;
	EvictCursor = Keys[A].Next;
	int32 V = Keys[A].Next;
	while (V >= 0 && Keys[V].Refs != 0) V = Keys[V].Next;
	if (V >= 0) FreeKeyframe(V);
}

void FSSXReplay::FreeKeyframe(int32 K)
{
	if (Tail == K) Tail = Keys[K].Prev;
	FKeyframe& N = Keys[K];
	N.Frame = 0;
	N.Refs = N.Lock = 0;
	if (N.Prev >= 0) Keys[N.Prev].Next = N.Next;
	if (N.Next >= 0) Keys[N.Next].Prev = N.Prev;
	N.Prev = -1;
	N.Next = FreeKeys;
	N.bInUse = false;
	N.Snapshot.Reset();
	FreeKeys = K;
}

void FSSXReplay::MaybeFreeKeyframe(int32 K)
{
	if (!Keys.IsValidIndex(K)) return;
	const FKeyframe& N = Keys[K];
	if (N.Prev >= 0 && N.Next >= 0 && Keys[N.Next].Next >= 0 && N.Lock == 0 && N.Refs == 0)
	{
		FreeKeyframe(K);
	}
}

void FSSXReplay::Prune(bool bAll)
{
	if (Head < 0) return;
	const int32 A = Keys[Head].Next;
	if (A < 0) return;
	if (!bAll && Keys[A].Next < 0) return;
	int32 Prev = A;
	int32 Cur = Keys[A].Next;
	while (true)
	{
		if (Keys[Prev].Lock == 0 && Keys[Prev].Refs == 0) FreeKeyframe(Prev);
		if (Cur < 0) break;
		const int32 Nx = Keys[Cur].Next;
		if (!bAll && Nx < 0) return;
		Prev = Cur;
		Cur = Nx;
	}
}

int32 FSSXReplay::InsertKeyframe(uint32 Frame, bool bDropScripts)
{
	const int32 K = AllocKeyframe();
	if (K < 0) return -1;
	if (Tail < 0)
	{
		Head = K;
	}
	else
	{
		int32 P = Head;
		int32 N = Keys[P].Next;
		bool bAppend = (N < 0);
		while (!bAppend && Keys[N].Frame <= Frame)
		{
			P = N;
			if (Keys[N].Next < 0) { bAppend = true; break; }
			N = Keys[N].Next;
		}
		if (bAppend)
		{
			Keys[K].Next = -1;
		}
		else
		{
			Keys[N].Prev = K;
			Keys[K].Next = Keys[P].Next;
		}
		Keys[P].Next = K;
		Keys[K].Prev = P;
	}
	Keys[K].Frame = Frame;
	W().CaptureKeyframe(K, Frame, bDropScripts);
	W().FileSnapshot(K, Keys[K].Snapshot);
	W().Periodic10();   // 0x14FE18, 0x150428
	// 0x192C10
	if (Keys[K].Next < 0) Tail = K;
	return K;
}

int32 FSSXReplay::FindKeyframe(uint32 Frame) const
{
	int32 Cur = Head;
	if (Cur < 0) return -1;
	while (true)
	{
		int32 K;
		uint32 F;
		do
		{
			K = Cur;
			Cur = Keys[K].Next;
			if (Cur < 0) return K;
			F = Keys[K].Frame;
			if (F == Frame)
			{
				if (Keys[K].Refs == 0 || IsFocusClipKey(K)) return K;
				Cur = Keys[K].Next;
				F = Keys[K].Frame;
			}
		} while (Frame <= F || Keys[Cur].Frame <= Frame);
		if (Keys[K].Refs == 0 || IsFocusClipKey(K)) return K;
		Cur = Keys[K].Next;
	}
}

int32 FSSXReplay::NextKeyframe(uint32 Frame) const
{
	const int32 K = FindKeyframe(Frame);
	int32 N = Keys[K].Next;
	while (N >= 0 && Keys[N].Refs != 0 && !IsFocusClipKey(N)) N = Keys[N].Next;
	return N >= 0 ? N : K;
}

int32 FSSXReplay::PrevKeyframe(uint32 Frame) const
{
	const int32 K = FindKeyframe(Frame);
	int32 P = Keys[K].Prev;
	while (P >= 0 && Keys[P].Refs != 0 && !IsFocusClipKey(P)) P = Keys[P].Prev;
	return P >= 0 ? P : K;
}

bool FSSXReplay::IsFocusClipKey(int32 K) const
{
	for (const int32 C : RiderClips[Focus])
	{
		if (Clips[C].Key == K) return true;
	}
	return false;
}

int32 FSSXReplay::TailPrevKey() const
{
	if (Tail < 0) return -1;
	return Keys[Tail].Prev >= 0 ? Keys[Tail].Prev : Tail;
}

void FSSXReplay::SeekAll(uint32 Frame)
{
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		if (bHasInput[R]) Input[R].Seek(Frame);
	}
}

TArray<uint32> FSSXReplay::KeyframeFrames() const
{
	TArray<uint32> Out;
	for (int32 K = Head; K >= 0; K = Keys[K].Next) Out.Add(Keys[K].Frame);
	return Out;
}

int32 FSSXReplay::CreateClip(uint32 Frame, int32 Rider)
{
	TArray<int32>& L = RiderClips[Rider];
	if (L.Num() > MaxClipsPerRider - 1) return -1;
	int32 At = L.Num();
	for (int32 I = 0; I < L.Num(); ++I)
	{
		const int32 K = Clips[L[I]].Key;
		const uint32 F = Keys.IsValidIndex(K) ? Keys[K].Frame : 0;
		if (F == Frame) return -1;
		if (Frame <= F) { At = I; break; }
	}
	const int32 C = FreeClips;
	if (C < 0) return -1;
	FreeClips = Clips[C].NextFree;
	Clips[C] = FClip();   // 0x193A78
	Clips[C].Rider = Rider;
	L.Insert(C, At);
	return C;
}

void FSSXReplay::DeleteClip(int32 Rider, int32 C)
{
	if (C < 0) return;
	if (Clips[C].Key >= 0)
	{
		--Keys[Clips[C].Key].Refs;
		MaybeFreeKeyframe(Clips[C].Key);
	}
	Clips[C].Key = -1;
	const int32 I = RiderClips[Rider].Find(C);
	if (I != INDEX_NONE)
	{
		Clips[C].NextFree = FreeClips;
		FreeClips = C;
		RiderClips[Rider].RemoveAt(I);
	}
}

void FSSXReplay::HighlightCancel(int32 Rider)
{
	FHighlights& H = Hl[Rider];
	if (H.PendingKey >= 0)
	{
		--Keys[H.PendingKey].Refs;
		MaybeFreeKeyframe(H.PendingKey);
	}
	H.PendingKey = -1;
	if (H.Pending >= 0)
	{
		DeleteClip(H.Rider, H.Pending);
		H.Pending = -1;
	}
}

void FSSXReplay::HighlightStart(int32 Rider, int32 Type)
{
	if (Rider < 0 || Rider >= MaxRiders || !Hl[Rider].bActive) return;
	if (bFull || !(State == Record || State > 9) || bEnded || bRebuild) return;
	FHighlights& H = Hl[Rider];
	HighlightCancel(Rider);
	const int32 K = TailPrevKey();
	if (K < 0) return;
	++Keys[K].Refs;
	H.PendingKey = K;
	const int32 C = CreateClip(Keys[K].Frame, H.Rider);
	H.Pending = C;
	if (C < 0) { HighlightCancel(Rider); return; }
	Clips[C].Type = Type;
}

void FSSXReplay::HighlightValue(int32 Rider, float V)
{
	if (Rider >= 0 && Rider < MaxRiders && Hl[Rider].bActive && Hl[Rider].Pending >= 0 && V != 0.f) Clips[Hl[Rider].Pending].Value = V;
}

void FSSXReplay::HighlightImpact(int32 Rider, float V)
{
	if (Rider >= 0 && Rider < MaxRiders && Hl[Rider].bActive && Hl[Rider].Pending >= 0 && V != 0.f) Clips[Hl[Rider].Pending].Impact = V;
}

int32 FSSXReplay::LowestSlot(int32 Rider, int32 Type) const
{
	auto Val = [&](int32 C) { return Type == 1 ? Clips[C].Value : (Type == 2 ? Clips[C].Impact : 0.f); };
	int32 Best = -1;
	float Low = 1e10f;
	for (int32 I = 0; I < QuotaSlots && I < HighlightSlots; ++I)
	{
		const int32 C = Hl[Rider].Slots[I];
		if (C >= 0 && Val(C) < Low) { Low = Val(C); Best = I; }
	}
	return Best;
}

int32 FSSXReplay::PickSlot(int32 Rider, int32 C) const
{
	const FHighlights& H = Hl[Rider];
	const int32 T = Clips[C].Type;
	auto Val = [&](int32 X, int32 Ty) { return Ty == 1 ? Clips[X].Value : (Ty == 2 ? Clips[X].Impact : 0.f); };
	auto Count = [&](int32 Ty) { int32 N = 0; for (const int32 S : H.Slots) { if (S >= 0 && Clips[S].Type == Ty) ++N; } return N; };
	auto Quota = [&](int32 Ty) { return Ty == 1 ? QuotaTricks : (Ty == 2 ? QuotaWipeOuts : 0); };
	if (T == 1 && Val(C, 1) <= 0.f) return -1;
	int32 Idx = -1;
	for (int32 I = 0; I < QuotaSlots && I < HighlightSlots; ++I)
	{
		if (H.Slots[I] < 0) { Idx = I; break; }
	}
	if (Idx < 0)
	{
		if (Count(T) < Quota(T) - 1)
		{
			if (T == 1) { if (Count(2) >= Quota(2)) Idx = LowestSlot(Rider, 2); }
			else if (T == 2) { if (Count(1) >= Quota(1)) Idx = LowestSlot(Rider, 1); }
		}
		else
		{
			Idx = LowestSlot(Rider, T);
			if (Idx >= 0 && Val(C, T) < Val(H.Slots[Idx], T)) Idx = -1;
		}
	}
	return Idx;
}

void FSSXReplay::HighlightCommit(int32 Rider)
{
	if (Rider < 0 || Rider >= MaxRiders || !Hl[Rider].bActive) return;
	if (IsPlaying() || bRebuild) return;
	FHighlights& H = Hl[Rider];
	if (bFull) { HighlightCancel(Rider); return; }
	if (H.Pending >= 0)
	{
		if (H.PendingKey < 0) { HighlightCancel(Rider); return; }
		Clips[H.Pending].Length = int32(RecFrame - Keys[H.PendingKey].Frame) + 60;   // 0x193AD8
	}
	// 0x1937D0
	const int32 C = H.Pending;
	if (C >= 0 && Clips[C].Type != 0)
	{
		const int32 S = PickSlot(Rider, C);
		if (S >= 0)
		{
			if (H.Slots[S] >= 0) DeleteClip(Clips[C].Rider, H.Slots[S]);
			H.Slots[S] = C;
			H.Pending = -1;
			Clips[C].Key = H.PendingKey;
			H.PendingKey = -1;
			return;
		}
	}
	HighlightCancel(Rider);
}

void FSSXReplay::HighlightClear(int32 Rider)
{
	if (Rider < 0 || Rider >= MaxRiders || !Hl[Rider].bActive || bRebuild) return;
	HighlightCancel(Rider);
	for (int32& S : Hl[Rider].Slots)
	{
		if (S >= 0) { DeleteClip(Hl[Rider].Rider, S); S = -1; }
	}
}

void FSSXReplay::DropPendingHighlights()
{
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		if (Hl[R].bActive) HighlightCancel(R);
	}
}

bool FSSXReplay::DeleteClipAtFrame()
{
	const int32 R = Focus;
	for (const int32 C : RiderClips[R])
	{
		const uint32 F = Keys.IsValidIndex(Clips[C].Key) ? Keys[Clips[C].Key].Frame : 0;
		if (int32(PlayFrame - F) <= Clips[C].Length)
		{
			if (Hl[R].bActive)
			{
				for (int32& S : Hl[R].Slots) { if (S == C) S = -1; }   // 0x1938A0
			}
			DeleteClip(R, C);
			return true;
		}
	}
	return false;
}

bool FSSXReplay::MarkClipStep()
{
	// 0x1922A8
	if (MarkClip >= 0) return false;
	const uint32 F = PlayFrame;
	if (F == RecLength) return false;
	const int32 R = Focus;
	for (const int32 C : RiderClips[R])
	{
		const uint32 KF = Keys.IsValidIndex(Clips[C].Key) ? Keys[Clips[C].Key].Frame : 0;
		const int32 D = int32(F - KF);
		if (D >= 0 && D <= Clips[C].Length + 11)
		{
			MarkStart = int32(F);
			MarkClip = C;
			Clips[C].Length = D;
			return true;
		}
	}
	const int32 C = CreateClip(F, R);
	if (C < 0) return false;
	MarkClip = C;
	MarkStart = int32(F);
	int32 K = FindKeyframe(F);
	Clips[C].Key = K;
	if (Keys[K].Frame != uint32(MarkStart))
	{
		K = InsertKeyframe(uint32(MarkStart), false);
		Clips[C].Key = K;
		if (K < 0)
		{
			MarkClip = -1;
			DeleteClip(R, C);
			return false;
		}
	}
	Clips[C].Value = 0.f;
	Clips[C].Impact = 0.f;
	++Keys[Clips[C].Key].Refs;
	Clips[C].Length = 0;
	Clips[C].Rider = Focus;
	return true;
}

void FSSXReplay::EndClipMark()
{
	// 0x1924A0
	const int32 C = MarkClip;
	if (C >= 0)
	{
		const int32 R = Clips[C].Rider;
		Clips[C].Length += int32(PlayFrame) - MarkStart;
		const uint32 S = Keys.IsValidIndex(Clips[C].Key) ? Keys[Clips[C].Key].Frame : 0;
		const uint32 F = PlayFrame;
		if (R >= 0 && R < MaxRiders)
		{
			for (int32 I = 0; I < RiderClips[R].Num(); ++I)
			{
				const int32 O = RiderClips[R][I];
				const uint32 OF = Keys.IsValidIndex(Clips[O].Key) ? Keys[Clips[O].Key].Frame : 0;
				if (O != C && S <= OF && OF < F)
				{
					const int32 Extra = Clips[O].Length - int32(F - OF);
					if (Extra > 0) Clips[C].Length += Extra;
					if (Hl[R].bActive) { for (int32& Sl : Hl[R].Slots) { if (Sl == O) Sl = -1; } }
					--I;
					DeleteClip(R, O);
				}
			}
		}
	}
	MarkClip = -1;
}
