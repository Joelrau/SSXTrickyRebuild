#include "Race/SSXReplayFile.h"
#include "Race/GameRng.h"
#include "Race/SSXReplay.h"
#include "Race/SSXHuffCodec.h"
#include "Progression/SSXProgression.h"
#include "Sim/RiderState.h"
#include "Sim/Rider/RiderProfiles.h"

using namespace SSXReplay;

namespace
{
	struct FWriter
	{
		TArray<uint8>& B;
		explicit FWriter(TArray<uint8>& InB) : B(InB) {}
		void U32(uint32 V) { const int32 At = B.AddUninitialized(4); FMemory::Memcpy(B.GetData() + At, &V, 4); }
		void I32(int32 V) { U32(uint32(V)); }
		void F32(float V) { uint32 U; FMemory::Memcpy(&U, &V, 4); U32(U); }
		void Bytes(const void* P, int32 N) { if (N > 0) { const int32 At = B.AddUninitialized(N); FMemory::Memcpy(B.GetData() + At, P, N); } }
		void Zeros(int32 N) { if (N > 0) B.AddZeroed(N); }
		// 0x1942F0, 0x194550, 0x243A60
		void Huff(const void* P, int32 N)
		{
			TArray<uint8> C;
			SSXHuff::Encode(static_cast<const uint8*>(P), N, C);
			I32(C.Num());
			Bytes(C.GetData(), C.Num());
		}
	};

	struct FReader
	{
		const uint8* P;
		int32 N;
		int32 At = 0;
		bool bOk = true;
		FReader(const uint8* InP, int32 InN) : P(InP), N(InN) {}
		bool Need(int32 K) { if (K < 0 || At + K > N) { bOk = false; } return bOk; }
		uint32 U32() { if (!Need(4)) return 0; uint32 V; FMemory::Memcpy(&V, P + At, 4); At += 4; return V; }
		int32 I32() { return int32(U32()); }
		float F32() { const uint32 U = U32(); float F; FMemory::Memcpy(&F, &U, 4); return F; }
		void Skip(int32 K) { if (Need(K)) At += K; }
		// 0x192D60, 0x192A40, 0x241E28
		bool Huff(TArray<uint8>& Out)
		{
			const int32 Len = I32();
			if (!bOk || !Need(Len)) return false;
			const bool bDecoded = Len == 0 ? (Out.Reset(), true) : SSXHuff::Decode(P + At, Len, Out);
			At += Len;
			if (!bDecoded) bOk = false;
			return bOk;
		}
	};

	void PutCamera(FWriter& W, const FCamera& C, uint32 Frame)
	{
		W.F32(C.A); W.F32(C.B); W.F32(C.C); W.F32(C.D); W.I32(C.Target); W.I32(C.Mode); W.U32(Frame);
	}
	FCamera GetCamera(FReader& R, uint32& Frame)
	{
		FCamera C;
		C.A = R.F32(); C.B = R.F32(); C.C = R.F32(); C.D = R.F32(); C.Target = R.I32(); C.Mode = R.I32();
		Frame = R.U32();
		return C;
	}

	uint32 StreamSum(const uint8* P, int32 N)
	{
		uint32 S = SSXReplayFile::ChecksumSeed;
		for (int32 I = 0; I < N; ++I) { S += uint32(int32(int8(P[I]))); }   // 0x193F10
		return S;
	}

	void Put32(uint8* P, int32 At, uint32 V) { FMemory::Memcpy(P + At, &V, 4); }
	uint32 Get32(const uint8* P, int32 At) { uint32 V; FMemory::Memcpy(&V, P + At, 4); return V; }
}

// 0x193F70, 0x194910

int32 FSSXReplay::NumHumans() const
{
	int32 N = 0;
	for (int32 R = 0; R < MaxRiders; ++R) { N += bHasInput[R] ? 1 : 0; }
	return N;
}

void FSSXReplay::SaveFileData(TArray<uint8>& Out) const
{
	using namespace SSXReplayFile;
	FWriter W(Out);
	// 0x191078
	int32 Keyframes = 0;
	if (Tail >= 0)
	{
		for (int32 K = Head; K >= 0; K = Keys[K].Next) { if (K != EndKey) ++Keyframes; }
	}
	// 0x191038
	auto KeyIndex = [this](int32 Key) -> int32
	{
		int32 I = 0;
		for (int32 K = Head; K >= 0; K = Keys[K].Next)
		{
			if (K == Key) return I;
			if (K != EndKey) ++I;
		}
		return 0;
	};
	// 0x193B00
	auto PutClip = [&](int32 C)
	{
		const FClip& Cl = Clips[C];
		W.U32(ClipTag);
		if (!Keys.IsValidIndex(Cl.Key))
		{
			W.I32(0); W.I32(-1); W.I32(0); W.I32(0); W.I32(0); W.I32(-1); W.I32(0);
			return;
		}
		W.I32(Cl.Type);
		W.U32(Keys[Cl.Key].Frame);
		W.I32(Cl.Length);
		W.F32(Cl.Value);
		W.F32(Cl.Impact);
		W.I32(KeyIndex(Cl.Key));
		W.I32(Cl.Rider);
	};
	const int32 Humans = NumHumans();
	// 0x194138
	W.U32(HeaderMagic);
	W.I32(Keyframes);
	W.U32(LockInterval);
	W.U32(bFull ? 1u : 0u);
	W.U32(RecFrame);
	W.U32(RecLength);
	W.I32(Humans);
	for (int32 R = 0; R < MaxRiders; ++R) { W.I32(RiderClips[R].Num()); }
	W.I32(Focus);
	for (int32 I = HeaderWritten; I < HeaderBytes; I += 4) { W.U32(StaleChunkWord); }
	for (int32 P = 0; P < Humans && P < MaxRiders; ++P)
	{
		const FCameraTrack& T = Tracks[P];
		W.I32(T.Count);
		W.I32(T.Cur);
		PutCamera(W, T.Single, 0);
		for (int32 C = 0; C < MaxCuts; ++C)
		{
			if (T.Cuts.IsValidIndex(C)) { PutCamera(W, T.Cuts[C].Cam, T.Cuts[C].Frame); }
			else { W.Zeros(0x1C); }
		}
	}
	// 0x1942B0, 0x1942F0
	if (Tail >= 0)
	{
		for (int32 K = Head; K >= 0; K = Keys[K].Next)
		{
			if (K == EndKey) continue;
			const FKeyframe& Key = Keys[K];
			W.U32(KeyTag);
			W.I32(Key.Snapshot.Num());
			W.U32(Key.Frame);
			W.I32(Key.Refs);
			W.I32(Key.Lock);
			W.U32(KeyTail);
			W.Huff(Key.Snapshot.GetData(), Key.Snapshot.Num());
		}
	}
	// 0x194418
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		for (const int32 C : RiderClips[R]) { PutClip(C); }
	}
	// 0x194518, 0x194550
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		if (!bHasInput[R]) continue;
		const FInputRecorder& In = Input[R];
		TArray<uint32> Words;
		TArray<uint8> Runs;
		In.Image(Words, Runs);
		W.U32(InputTag);
		W.I32(In.Words.Num());
		W.Huff(Words.GetData(), RecorderWordBytes);
		W.Huff(Runs.GetData(), RecorderRunBytes);
	}
	// 0x1946D0, 0x1938C8
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		const FHighlights& H = Hl[R];
		if (!H.bActive) continue;
		TArray<int32, TInlineAllocator<HighlightSlots>> Filled;
		for (const int32 S : H.Slots) { if (S >= 0 && S < ClipPool) Filled.Add(S); }
		W.U32(HighlightTag);
		W.I32(Filled.Num());
		for (int32 I = 0; I < HighlightSlots; ++I)
		{
			W.U32(Filled.IsValidIndex(I) && Keys.IsValidIndex(Clips[Filled[I]].Key) ? Keys[Clips[Filled[I]].Key].Frame : 0u);
		}
		for (const int32 S : Filled) { PutClip(S); }
	}
}

bool FSSXReplay::LoadFileData(const uint8* Data, int32 Num)
{
	using namespace SSXReplayFile;
	FReader Rd(Data, Num);
	ResetIdle();
	if (Rd.U32() != HeaderMagic) return false;   // 0x194910
	const int32 Keyframes = Rd.I32();
	const uint32 Lock = Rd.U32();
	const bool bFileFull = Rd.U32() != 0;
	const uint32 FileRecFrame = Rd.U32();
	const uint32 FileLength = Rd.U32();
	const int32 Humans = Rd.I32();
	int32 ClipCount[MaxRiders];
	for (int32 R = 0; R < MaxRiders; ++R) { ClipCount[R] = Rd.I32(); }
	const int32 FileFocus = Rd.I32();
	Rd.Skip(HeaderBytes - HeaderWritten);
	if (!Rd.bOk || Humans != NumHumans() || Keyframes < 0 || Keyframes > Keys.Num()) return false;
	bLoaded = true;
	LockInterval = Lock;
	RecFrame = FileRecFrame;
	bFull = bFileFull;
	RecLength = FileLength;
	PlayFrame = 0;
	Focus = FMath::Clamp(FileFocus, 0, MaxRiders - 1);
	for (int32 P = 0; P < Humans; ++P)
	{
		FCameraTrack& T = Tracks[P];
		T.Reset();
		const int32 Count = Rd.I32();
		T.Cur = Rd.I32();
		uint32 Frame = 0;
		T.Single = GetCamera(Rd, Frame);
		for (int32 C = 0; C < MaxCuts; ++C)
		{
			FCameraTrack::FCut Cut;
			Cut.Cam = GetCamera(Rd, Cut.Frame);
			if (C < Count) T.Cuts.Add(Cut);
		}
		T.Count = T.Cuts.Num();
	}
	// 0x18EE68, 0x193088, 0x192D60
	for (int32 I = 0; I < Keyframes; ++I)
	{
		const uint32 Tag = Rd.U32();
		const int32 Bytes = Rd.I32();
		const uint32 Frame = Rd.U32();
		const int32 Refs = Rd.I32();
		const int32 KeyLock = Rd.I32();
		const uint32 TailTag = Rd.U32();
		if (Tag != KeyTag || TailTag != KeyTail) return false;
		const int32 K = AllocKeyframe();
		if (K < 0) return false;
		if (!Rd.Huff(Keys[K].Snapshot) || Keys[K].Snapshot.Num() != Bytes) return false;
		if (Tail < 0) { Head = K; }
		else { Keys[Tail].Next = K; Keys[K].Prev = Tail; }
		Tail = K;
		Keys[K].Frame = Frame;
		Keys[K].Refs = Refs;
		Keys[K].Lock = KeyLock;
	}
	// 0x190D88
	auto KeyAtIndex = [this](int32 Index) -> int32
	{
		if (Index < 0) return -1;
		int32 K = Head;
		for (int32 I = 0; I < Index && K >= 0; ++I) { K = Keys[K].Next; }
		return K;
	};
	// 0x193BD0
	auto GetClip = [&](int32 C)
	{
		Rd.U32();
		const int32 Type = Rd.I32();
		Rd.U32();
		const int32 Length = Rd.I32();
		const float Value = Rd.F32();
		const float Impact = Rd.F32();
		const int32 Index = Rd.I32();
		const int32 Rider = Rd.I32();
		FClip& Cl = Clips[C];
		Cl.Key = KeyAtIndex(Index);
		Cl.Type = Type;
		Cl.Rider = Rider;
		Cl.Length = Length;
		Cl.Value = Value;
		Cl.Impact = Impact;
	};
	for (int32 R = 0; R < MaxRiders; ++R)
	{
		for (int32 I = 0; I < ClipCount[R]; ++I)
		{
			const int32 C = FreeClips;
			if (!Rd.bOk || C < 0 || RiderClips[R].Num() >= MaxClipsPerRider) return false;
			FreeClips = Clips[C].NextFree;
			Clips[C] = FClip();
			GetClip(C);
			RiderClips[R].Add(C);
		}
	}
	// 0x192A40
	for (int32 R = 0; R < Humans; ++R)
	{
		if (!bHasInput[R]) continue;
		Rd.U32();
		const int32 Count = Rd.I32();
		TArray<uint8> WordBytes, RunBytes;
		if (!Rd.Huff(WordBytes) || !Rd.Huff(RunBytes)) return false;
		if (Count < 0 || Count > MaxRuns || WordBytes.Num() != RecorderWordBytes || RunBytes.Num() != RecorderRunBytes) return false;
		FInputRecorder& In = Input[R];
		In.Fresh();
		In.MemWords.SetNumUninitialized(MaxRuns);
		FMemory::Memcpy(In.MemWords.GetData(), WordBytes.GetData(), RecorderWordBytes);
		In.MemRuns = RunBytes;
		In.Words.Append(In.MemWords.GetData(), Count);
		In.Runs.Append(RunBytes.GetData(), Count);
	}
	// 0x1939A0
	for (int32 R = 0; R < Humans; ++R)
	{
		FHighlights& H = Hl[R];
		if (!H.bActive) continue;
		Rd.U32();
		const int32 Count = Rd.I32();
		uint32 Frames[HighlightSlots];
		for (uint32& F : Frames) { F = Rd.U32(); }
		if (!Rd.bOk || Count < 0 || Count > HighlightSlots) return false;
		for (int32 I = 0; I < Count; ++I)
		{
			int32 Found = -1;   // 0x192178
			for (const int32 C : RiderClips[FMath::Clamp(H.Rider, 0, MaxRiders - 1)])
			{
				if (Keys.IsValidIndex(Clips[C].Key) && Keys[Clips[C].Key].Frame == Frames[I]) { Found = C; break; }
			}
			if (Found < 0) return false;
			H.Slots[I] = Found;
			GetClip(Found);
		}
		H.Pending = -1;
		H.PendingKey = -1;
	}
	if (!Rd.bOk) return false;
	// 0x135EB8
	Command = CmdToStart;
	State = State8;
	Playlist.Reset();
	PlaylistCur = 0;
	EndKey = -1;
	Current = Head;
	SeekAll(0);
	return true;
}

void FSSXReplay::BeginRebuild()
{
	RebuildKeys.Reset();
	for (int32 K = Head; K >= 0; K = Keys[K].Next) { RebuildKeys.Add(K); }
	RebuildNext = 0;
	RebuildRecFrame = RecFrame;
	RecFrame = 0;
	bRebuild = RebuildKeys.Num() > 0;
	Command = CmdNone;
	State = bRebuild ? Idle : State8;   // 0x113478
	if (!bRebuild) Command = CmdToStart;
	SeekAll(0);
}

void FSSXReplay::RebuildFrame()
{
	// 0x18F328
	State = Record;
	while (RebuildKeys.IsValidIndex(RebuildNext) && Keys[RebuildKeys[RebuildNext]].Frame <= RecFrame)
	{
		const int32 K = RebuildKeys[RebuildNext++];
		if (Keys[K].Frame == RecFrame)
		{
			if (RecFrame == 0 && Keys[K].Snapshot.Num() >= 0x18)
			{
				// 0x18F6C0, 0x23D5C0
				for (int32 I = 0; I < 6; ++I) { FMemory::Memcpy(&FGameRng::StreamB().S[I], Keys[K].Snapshot.GetData() + 4 * I, 4); }
			}
			W().CaptureKeyframe(K, RecFrame, false);
			W().RebuiltKeyframe(K, RecFrame, Keys[K].Snapshot);
		}
	}
	if (RecFrame % 10 == 0) W().Periodic10();   // 0x14FE18, 0x150428
	++RecFrame;
	if (RebuildNext >= RebuildKeys.Num())
	{
		// 0x194910
		bRebuild = false;
		RecFrame = RebuildRecFrame;
		State = State8;
		Command = CmdToStart;
		PlayFrame = 0;
		Current = Head;
		SeekAll(0);
	}
}

namespace SSXReplayFile
{
	FString FRacer::NameString() const
	{
		ANSICHAR S[sizeof(Name) + 1] = {};
		FMemory::Memcpy(S, Name, sizeof(Name));
		return FString(ANSI_TO_TCHAR(S));
	}

	int32 FSetup::NumHumans() const
	{
		int32 N = 0;
		for (int32 I = 0; I < FMath::Min(NumRacers, MaxRacers); ++I) { N += Racers[I].IsHuman() ? 1 : 0; }
		return N;
	}

	int32 FSetup::RacerOfHuman(int32 P) const
	{
		for (int32 I = 0; I < FMath::Min(NumRacers, MaxRacers); ++I)
		{
			if (Racers[I].IsHuman() && P-- == 0) return I;
		}
		return -1;
	}

	void EncodeSetup(FSetup& S, uint8* Out)
	{
		FMemory::Memzero(Out, SetupBytes);
		Put32(Out, 0, uint32(S.NumRacers));
		for (int32 I = 0; I < MaxRacers; ++I)
		{
			const FRacer& R = S.Racers[I];
			uint8* P = Out + 4 + I * ProfileBytes;
			Put32(P, 0x00, uint32(R.Slot));
			FMemory::Memcpy(P + 0x04, R.Name, sizeof(R.Name));
			FMemory::Memcpy(P + 0x0E, R.Stats, sizeof(R.Stats));
			P[0x1E] = R.Mass;
			P[0x1F] = R.Pad1F;
			FMemory::Memcpy(P + 0x20, R.Rival, sizeof(R.Rival));
			const int32 Words[] = { R.Char38, R.Char3C, R.Stance, R.BoardClass, R.BoardId, R.OutfitId, R.BodyId, R.HeadTexId, R.HeadModelId, R.TrickSet };
			for (int32 K = 0; K < UE_ARRAY_COUNT(Words); ++K) { Put32(P, 0x38 + 4 * K, uint32(Words[K])); }
			FMemory::Memcpy(P + 0x60, &R.Scale, 4);
			P[0x64] = R.Byte64;
			P[0x65] = uint8(R.Port);
			P[0x66] = R.Byte66;
			P[0x67] = R.Byte67;
			const int32 Tail[] = { R.Word68, R.Word6C, R.Word70, R.Rank, R.Word78, R.Word7C, R.Type80 };
			for (int32 K = 0; K < UE_ARRAY_COUNT(Tail); ++K) { Put32(P, 0x68 + 4 * K, uint32(Tail[K])); }
		}
		Put32(Out, 0x31C, uint32(S.Venue));
		Put32(Out, 0x320, uint32(S.GameMode));
		Put32(Out, 0x324, S.Seed2B00);
		Put32(Out, 0x328, S.Seed2B04);
		Put32(Out, 0x32C, uint32(S.TimeToBeat));
		Put32(Out, 0x330, uint32(S.HeatWord));
		Put32(Out, 0x338, uint32(S.Difficulty));
		for (int32 K = 0; K < SSXIntroDialogue::NumSlots; ++K)
		{
			const int32 At = 0x33C + 100 * K;
			Put32(Out, At, uint32(S.Slots[K].Type));
			for (int32 J = 0; J < SSXIntroDialogue::NumLines; ++J)
			{
				const SSXIntroDialogue::FLine& L = S.Slots[K].Lines[J];
				Put32(Out, At + 4 + 16 * J, uint32(L.Speaker));
				Put32(Out, At + 8 + 16 * J, uint32(L.LineId));
				Put32(Out, At + 12 + 16 * J, uint32(L.Aux));
				Put32(Out, At + 16 + 16 * J, uint32(L.Value));
			}
		}
		Put32(Out, 0x918, uint32(S.Focus));
		Put32(Out, 0x91C, uint32(S.Word91C));
		// 0x1F0308
		S.Crc = SSXProg::Crc16(Out, SetupBytes);
		Put32(Out, SetupCrcAt, S.Crc);
	}

	void DecodeSetup(const uint8* In, FSetup& S)
	{
		S = FSetup();
		S.NumRacers = int32(Get32(In, 0));
		for (int32 I = 0; I < MaxRacers; ++I)
		{
			FRacer& R = S.Racers[I];
			const uint8* P = In + 4 + I * ProfileBytes;
			R.Slot = int32(Get32(P, 0));
			FMemory::Memcpy(R.Name, P + 0x04, sizeof(R.Name));
			FMemory::Memcpy(R.Stats, P + 0x0E, sizeof(R.Stats));
			R.Mass = P[0x1E];
			R.Pad1F = P[0x1F];
			FMemory::Memcpy(R.Rival, P + 0x20, sizeof(R.Rival));
			int32* Words[] = { &R.Char38, &R.Char3C, &R.Stance, &R.BoardClass, &R.BoardId, &R.OutfitId, &R.BodyId, &R.HeadTexId, &R.HeadModelId, &R.TrickSet };
			for (int32 K = 0; K < UE_ARRAY_COUNT(Words); ++K) { *Words[K] = int32(Get32(P, 0x38 + 4 * K)); }
			FMemory::Memcpy(&R.Scale, P + 0x60, 4);
			R.Byte64 = P[0x64];
			R.Port = int8(P[0x65]);
			R.Byte66 = P[0x66];
			R.Byte67 = P[0x67];
			int32* Tail[] = { &R.Word68, &R.Word6C, &R.Word70, &R.Rank, &R.Word78, &R.Word7C, &R.Type80 };
			for (int32 K = 0; K < UE_ARRAY_COUNT(Tail); ++K) { *Tail[K] = int32(Get32(P, 0x68 + 4 * K)); }
		}
		S.Venue = int32(Get32(In, 0x31C));
		S.GameMode = int32(Get32(In, 0x320));
		S.Seed2B00 = Get32(In, 0x324);
		S.Seed2B04 = Get32(In, 0x328);
		S.TimeToBeat = int32(Get32(In, 0x32C));
		S.HeatWord = int32(Get32(In, 0x330));
		S.Crc = Get32(In, SetupCrcAt);
		S.Difficulty = int32(Get32(In, 0x338));
		for (int32 K = 0; K < SSXIntroDialogue::NumSlots; ++K)
		{
			const int32 At = 0x33C + 100 * K;
			S.Slots[K].Type = int32(Get32(In, At));
			for (int32 J = 0; J < SSXIntroDialogue::NumLines; ++J)
			{
				SSXIntroDialogue::FLine& L = S.Slots[K].Lines[J];
				L.Speaker = int32(Get32(In, At + 4 + 16 * J));
				L.LineId = int32(Get32(In, At + 8 + 16 * J));
				L.Aux = int32(Get32(In, At + 12 + 16 * J));
				L.Value = int32(Get32(In, At + 16 + 16 * J));
			}
		}
		S.Focus = int32(Get32(In, 0x918));
		S.Word91C = int32(Get32(In, 0x91C));
	}

	bool SetupCrcValid(const uint8* In)
	{
		uint8 Copy[SetupBytes];
		FMemory::Memcpy(Copy, In, SetupBytes);
		Put32(Copy, SetupCrcAt, 0);
		return SSXProg::Crc16(Copy, SetupBytes) == Get32(In, SetupCrcAt);
	}

	bool ParseSnapshot(const TArray<uint8>& Snap, FSnapshot& Out)
	{
		Out = FSnapshot();
		if (Snap.Num() < 0x18) return false;
		for (int32 I = 0; I < 6; ++I) { Out.StreamB[I] = Get32(Snap.GetData(), 4 * I); }
		for (int32 At = 0x18; At + 0x18 <= Snap.Num(); At += 4)
		{
			if (Get32(Snap.GetData(), At) != 0x18 || Get32(Snap.GetData(), At + 4) != 0xDEADBEF1) continue;
			FMemory::Memcpy(&Out.AiValue14, Snap.GetData() + At + 8, 4);
			Out.AiFrame = int32(Get32(Snap.GetData(), At + 12));
			Out.AiPhase = int32(Get32(Snap.GetData(), At + 16));
			Out.AiOldPhase = int32(Get32(Snap.GetData(), At + 20));
			break;
		}
		// 0x117D20
		for (int32 At = 0x18; At + 0x150 <= Snap.Num(); At += 4)
		{
			if (Get32(Snap.GetData(), At) != 0x450 || Get32(Snap.GetData(), At + 4) != 0xDEADBEF8) continue;
			float V[6];
			FMemory::Memcpy(V, Snap.GetData() + At + 0x130, 12);
			FMemory::Memcpy(V + 3, Snap.GetData() + At + 0x140, 12);
			Out.Positions.Add(FVector(V[0], V[1], V[2]));
			Out.Velocities.Add(FVector(V[3], V[4], V[5]));
			Out.Motions.Add(int32(Get32(Snap.GetData(), At + 0x414)));
			Out.Controls.Add(int32(Get32(Snap.GetData(), At + 0x418)));
			Out.RecordAt.Add(At);
			At += 0x450 - 4;
		}
		return true;
	}

	void RestoreBoarderRecord(const uint8* Rec, ::FRiderState& S)
	{
		auto F = [Rec](int32 Off) { float V; FMemory::Memcpy(&V, Rec + Off - 0x10, 4); return double(V); };
		auto V3 = [&F](int32 Off) { return FVector(F(Off), F(Off + 4), F(Off + 8)); };
		auto Filt = [&F](int32 Off, FApproachFilter& P) { P.Cur = F(Off); P.Rate = F(Off + 4); P.Target = F(Off + 8); };
		S.BoostMeter = F(0x1C);
		S.TrickyTimer = F(0x24);
		S.TimeScale = F(0x12C);
		S.BoostStrength = F(0x130);
		S.BoostBurstTimer = F(0x134);
		S.LaunchBoostTimer = F(0x138);
		S.Position = V3(0x140);
		S.Velocity = V3(0x150);
		S.GroundDragAccel = F(0x160);
		S.ForwardDistance = F(0x164);
		S.RefDirAngle = F(0x168);
		S.Orientation = FQuat(F(0x174), F(0x178), F(0x17C), F(0x170));
		S.BasisForward = V3(0x180);
		S.BasisSide = V3(0x190);
		S.BasisUp = V3(0x1A0);
		S.Yaw = F(0x1B0);
		S.GroundHeight = F(0x1BC);
		S.SuspensionOffset = F(0x1C0);
		S.SpeedCap = F(0x1C4);
		S.LaunchCharge = F(0x1C8);
		S.BoardChatter = F(0x1CC);
		S.SpinAngle = F(0x1D0);
		S.FlipAngle = F(0x1D4);
		S.EdgeAngle = F(0x1D8);
		S.ShoveAngle = F(0x1E0);
		Filt(0x1F0, S.Ramp1F0); Filt(0x1FC, S.Brake); Filt(0x208, S.Crouch); Filt(0x214, S.Steer); Filt(0x220, S.PrewindA);
		Filt(0x22C, S.PrewindB); Filt(0x238, S.GateLaunch); Filt(0x244, S.Lean); Filt(0x250, S.BoostPose); Filt(0x25C, S.BoostFlash);
		Filt(0x268, S.SteerFollow);
		S.AirTurnOffset = F(0x274);
		S.AirTiltOffset = F(0x278);
		S.AirTurnSmoothed = F(0x27C);
		S.AirTiltSmoothed = F(0x280);
		S.SurfaceP7 = F(0x294);
		S.SurfaceP8 = F(0x298);
		S.GroundNormal = V3(0x2A0);
		S.FallLine = V3(0x2B0);
		S.SmoothedUp = V3(0x2C0);
		S.ContactPoint = V3(0x2D0);
		S.StuckAccumA = F(0x2E4);
		S.LastImpactNormal = V3(0x2F0);
		S.StuckAccumB = F(0x300);
		S.SurfaceP10 = F(0x308);
		S.GroundBlend = F(0x30C);
		S.ContactForward = V3(0x320);
		S.ContactLeft = V3(0x330);
		S.RenderAlpha = F(0x448);
		S.ResetFade = F(0x44C);
		S.SpawnPosition = V3(0x450);
	}

	SSXRider::FBuiltProfile RacerProfile(const FRacer& R)
	{
		SSXRider::FBuiltProfile B;
		FMemory::Memcpy(B.Stats, R.Stats, sizeof(B.Stats));
		B.Character = R.Char38;
		B.BoardClass = R.BoardClass;
		B.Board = R.Board();
		if (B.Board < 0 || B.Board >= SSXRider::NumBoards)
		{
			const SSXRider::FRiderTable& T = SSXRider::FRiderTable::Shared();
			B.Board = T.IsLoaded() ? FMath::Max(0, T.FirstBoardOfClass(R.Char38, R.BoardClass)) : 0;
		}
		B.Scale = R.Scale;
		B.Stance = R.Stance;
		B.TrickSet = R.TrickSet;
		B.MassByte = R.Mass;
		B.bComputer = !R.IsHuman();
		B.Byte64 = R.Byte64;
		B.Char3C = R.Char3C;
		B.Byte80 = R.Type80;
		B.bMixMasterMike = R.Type80 == 0;
		B.Rank = R.Rank;
		return B;
	}

	FRacer MakeRacer(const FRiderProfile& P, int32 Slot, int32 Player, int32 Outfit, int32 PadLayout, const FString& Name, bool bMode6)
	{
		FRacer R;
		R.Slot = Slot;
		const FTCHARToUTF8 U(*Name);
		FMemory::Memcpy(R.Name, U.Get(), FMath::Min<int32>(U.Length(), sizeof(R.Name)));
		FMemory::Memcpy(R.Stats, P.StatBytes, sizeof(R.Stats));
		R.Mass = uint8(P.MassByte);
		R.Char38 = P.Character;
		R.Char3C = (P.bMixMasterMike && !bMode6) ? 1 : P.Character;
		R.Stance = P.Stance;
		R.BoardClass = P.BoardClass;
		R.TrickSet = P.GetTrickCharacter();
		R.Scale = float(P.SizeScale);
		R.Byte64 = uint8(P.GetVoice());
		R.Type80 = P.bMixMasterMike ? 0 : 2;
		if (Player >= 0)
		{
			// 0x16A9D8
			R.Port = int8(Player);
			R.Word68 = Player;
			R.Word6C = PadLayout;
			R.Rank = P.Level;
		}
		else
		{
			// 0x16AE68, 0x16B1D0, 0x16B270
			R.Port = -1;
			R.Word68 = 0xFF;
			R.Word6C = -1;
			R.Rank = 0;
			R.BoardId = P.Character * 12 + P.Board;
			R.OutfitId = P.Character * 7 + FMath::Min(FMath::Max(Outfit, 0), 5);
			R.BodyId = R.HeadTexId = R.HeadModelId = P.Character;
		}
		// 0x3352A4, 0x3352A8
		return R;
	}

	void PortSnapshot(const uint32 StreamB[6], TArray<uint8>& Out, const FAiRecord* Ai)
	{
		Out.SetNumUninitialized(Ai ? 0x30 : 0x18);
		FMemory::Memcpy(Out.GetData(), StreamB, 0x18);
		if (Ai)
		{
			uint32 ClockBits;
			FMemory::Memcpy(&ClockBits, &Ai->Clock, 4);
			Put32(Out.GetData(), 0x18, 0x18u);
			Put32(Out.GetData(), 0x1C, 0xDEADBEF1u);
			Put32(Out.GetData(), 0x20, ClockBits);
			Put32(Out.GetData(), 0x24, uint32(Ai->Frame));
			Put32(Out.GetData(), 0x28, uint32(Ai->Phase));
			Put32(Out.GetData(), 0x2C, uint32(Ai->OldPhase));
		}
	}

	bool CheckSnapshot(const TArray<uint8>& Snap, TConstArrayView<FVector> PortPositions, const uint32 PortStreamB[6], FSnapshotCheck& Out)
	{
		Out = FSnapshotCheck();
		FSnapshot S;
		if (!ParseSnapshot(Snap, S)) return false;
		Out.FileRiders = S.Positions.Num();
		Out.bStreamBEqual = FMemory::Memcmp(S.StreamB, PortStreamB, sizeof(S.StreamB)) == 0;
		Out.StreamBCalls = int64(int32(PortStreamB[5] - S.StreamB[5]));
		for (int32 I = 0; I < PortPositions.Num(); ++I)
		{
			const int32 F = S.Positions.Num() - 1 - I;
			const double E = S.Positions.IsValidIndex(F) ? FVector::Dist(S.Positions[F], PortPositions[I]) : -1.0;
			Out.Errors.Add(E);
			if (E > Out.MaxError) { Out.MaxError = E; Out.WorstRacer = I; }
		}
		return true;
	}

	bool Write(const FSSXReplay& Replay, FSetup Setup, TArray<uint8>& Out)
	{
		Out.Reset();
		Out.SetNumZeroed(FileBytes);
		EncodeSetup(Setup, Out.GetData());
		TArray<uint8> Stream;
		Replay.SaveFileData(Stream);
		if (Stream.Num() > StreamMax)
		{
			return false;   // 0x194750
		}
		FMemory::Memcpy(Out.GetData() + SetupBytes, Stream.GetData(), Stream.Num());   // 0x1947A0
		Put32(Out.GetData(), LengthAt, uint32(StreamMax));
		Put32(Out.GetData(), ChecksumAt, StreamSum(Out.GetData() + SetupBytes, StreamMax));
		return true;
	}

	bool Validate(const TArray<uint8>& In)
	{
		if (In.Num() != FileBytes) return false;
		const int32 Len = int32(Get32(In.GetData(), LengthAt));
		return Len > 0 && Len <= StreamMax && StreamSum(In.GetData() + SetupBytes, Len) == Get32(In.GetData(), ChecksumAt);
	}

	bool ReadSetup(const TArray<uint8>& In, FSetup& Out)
	{
		if (!Validate(In)) return false;
		DecodeSetup(In.GetData(), Out);
		return true;
	}

	bool ReadReplay(const TArray<uint8>& In, FSSXReplay& Replay)
	{
		if (!Validate(In)) return false;
		const int32 Len = int32(Get32(In.GetData(), LengthAt));
		return Replay.LoadFileData(In.GetData() + SetupBytes, Len);
	}
}
