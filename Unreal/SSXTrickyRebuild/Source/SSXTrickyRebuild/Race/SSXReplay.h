// 0x18E850, 0x18EAC8, 0x18EBD0, 0x191298, 0x38AAC0, 0x191778, 0x1927D8, 0x1928F0, 0x192880, 0x1937D0, 0x1935D8, 0x1950B8, 0x1953C8, 0x32CA4C, 0x194D78, 0x21BFD0, 0x190878, 0x192998, 0x18F020
#pragma once

#include "CoreMinimal.h"


namespace SSXReplay
{
	enum EState : int32
	{
		Record = 0, Play = 1, ClipPlay = 2, Paused = 3, StepFrame = 4, Seeked = 5, SeekedStart = 6, Started = 7,
		State8 = 8, Attract = 9, Idle = 10, StageReset = 11, RecordStart = 12, RecordEnded = 13,
	};
	// 0x191830
	enum ECommand : int32
	{
		CmdNone = 0, CmdToStart = 1, CmdToEnd = 2, CmdPrevKey = 3, CmdNextKey = 4, CmdStep = 5, CmdPlayToggle = 6,
		CmdPause = 7, CmdClip = 8, CmdNextClip = 9, CmdPrevClip = 10,
	};

	constexpr int32 KeyframeEvery = 60;
	constexpr int32 MaxRiders = 4;

	// 0x110BF8, 0x150E38, 0x191CA0, 0x191A18
	SSXTRICKYREBUILD_API int32 PlayerOfRacer(TFunctionRef<bool(int32)> IsHuman, int32 NumRacers, int32 Racer);
	// 0x1785C0
	SSXTRICKYREBUILD_API int32 RacerOfPlayer(TFunctionRef<bool(int32)> IsHuman, int32 NumRacers, int32 Player);
	constexpr int32 MaxRuns = 0x1999;
	constexpr int32 FullMessageFrames = 180;
	constexpr int32 MaxClipsPerRider = 10;    // 0x191EB0
	constexpr int32 ClipPool = 11;
	constexpr int32 MaxPlaylist = 10;         // 0x1955B8
	constexpr int32 MaxCuts = 128;
	constexpr int32 HighlightSlots = 4;

	// 0x178968, 0x1786C0
	struct FCamera
	{
		float A = 0.f, B = 0.f, C = 0.f, D = 0.f;
		int32 Target = 0;
		int32 Mode = 0;
		bool operator==(const FCamera& O) const { return A == O.A && B == O.B && C == O.C && D == O.D && Target == O.Target && Mode == O.Mode; }
	};

	struct SSXTRICKYREBUILD_API FCameraTrack
	{
		int32 Count = 0;
		int32 Cur = 0;
		FCamera Single;
		struct FCut { FCamera Cam; uint32 Frame = 0; };
		TArray<FCut> Cuts;

		void Reset() { Count = 0; Cur = 0; Single = FCamera(); Cuts.Reset(); }   // 0x194CC0
		// 0x1952D8
		bool Insert(uint32 Frame, const FCamera& Cam, bool bAdvanced);
		void Set(uint32 Frame, const FCamera& Cam, bool bAdvanced);
		// 0x1951F8
		bool Delete(uint32 Frame, bool bAdvanced);
		void Seek(uint32 Frame, bool bAdvanced);
		// 0x195340
		bool Advance(uint32 Frame, bool bAdvanced);
		const FCamera& Current(bool bAdvanced) const;
	};

	struct SSXTRICKYREBUILD_API FInputRecorder
	{
		TArray<uint32> Words;
		TArray<uint8> Runs;
		int32 Read = 0;
		int32 Left = 0;

		// 0x192738
		void Clear();
		void Fresh() { Clear(); MemWords.Reset(); MemRuns.Reset(); }
		void Image(TArray<uint32>& OutWords, TArray<uint8>& OutRuns) const;
		TArray<uint32> MemWords;
		TArray<uint8> MemRuns;
		bool Record(uint32 W);
		bool Play(uint32& InOut);
		// 0x192758
		bool Seek(uint32 Frame);
		int32 NumRuns() const { return Words.Num(); }
	};

	struct FKeyframe
	{
		int32 Next = -1, Prev = -1;
		int32 Refs = 0;
		int32 Lock = 0;
		uint32 Frame = 0;
		bool bInUse = false;
		// 0x18F448
		TArray<uint8> Snapshot;
	};

	struct FClip
	{
		int32 Key = -1;
		int32 Length = 0;
		int32 Type = 0;
		float Value = 0.f;
		float Impact = 0.f;
		int32 NextFree = -1;
		int32 Rider = -1;
	};

	class IReplayWorld
	{
	public:
		virtual ~IReplayWorld() {}
		virtual void CaptureKeyframe(int32 Key, uint32 Frame, bool bDropScripts) {}
		// 0x18F6C0, 0x1A7860
		virtual void RestoreKeyframe(int32 Key, uint32 Frame) {}
		// 0x1128B8, 0x191BE0
		virtual void ResetRiderEffects() {}
		// 0x178A28, 0x1786D0
		virtual FCamera GetCamera() const { return FCamera(); }
		virtual void ApplyCamera(const FCamera& Cam, bool bBlend) {}
		virtual void SetCameraRider(bool bReplay, int32 Player) {}
		virtual void SetCameraTarget(int32 Player) {}
		virtual void SetCameraMode(int32 Mode) {}
		// 0x174C30
		virtual void StartCameraFlash() {}
		// 0x1147F8, 0x14FE18, 0x150428
		virtual void Periodic60() {}
		virtual void Periodic10() {}
		// 0x17DA50, 0x115DF0, 0x115FE8
		virtual void StageReset() {}
		virtual int32 FocusPlayer() const { return 0; }
		virtual void FileSnapshot(int32 Key, TArray<uint8>& Out) const {}
		virtual void RebuiltKeyframe(int32 Key, uint32 Frame, const TArray<uint8>& Original) {}
	};

	struct FHighlights
	{
		int32 Rider = 0;
		int32 Pending = -1;
		int32 Slots[HighlightSlots] = { -1, -1, -1, -1 };
		int32 PendingKey = -1;
		bool bActive = false;
	};
}

class SSXTRICKYREBUILD_API FSSXReplay
{
public:
	// 0x18E850, 0x18EAC8
	void Init(int32 Racers, int32 Humans, bool bAdvanced, SSXReplay::IReplayWorld* InWorld);
	// 0x18EBD0, 0x180A10
	void ResetIdle();
	// 0x113478
	void OnStartRace() { if (State == SSXReplay::Idle) State = SSXReplay::StageReset; }
	// 0x193C68, 0x210A38
	bool OnEndRaceAttract();
	// 0x191230, 0x192458
	void EndRecording();
	// 0x1EF990, 0x191B60
	void StartFullReplay() { StartPlayback(-1); }
	// 0x191BE0, 0x1785C0
	void StopPlayback();
	// 0x1A4F70
	bool ShowFullMessage() const { return bFull && State == SSXReplay::Record && FullMessage > 0; }
	bool HasRecording() const { return Head >= 0 && RecLength > 0; }

	// 0x191298, 0x17F0F8
	void Update(bool bGamePaused = false);
	// 0x18F020, 0x17F078
	bool IsWorldFrozen() const;
	// 0x21BFD0
	bool IsPlaying() const { return State != 0 && State < 10; }
	// 0x1927D8
	bool RecordOrPlayInput(int32 Rider, uint32& InOut);
	bool IsPlayingInput() const { return State == SSXReplay::Play || State == SSXReplay::ClipPlay || State == SSXReplay::StepFrame || (State == SSXReplay::Record && bRebuild); }

	// 0x193F70
	void SaveFileData(TArray<uint8>& Out) const;
	int32 NumHumans() const;
	// 0x194910
	bool LoadFileData(const uint8* Data, int32 Num);
	void BeginRebuild();
	bool IsRebuilding() const { return bRebuild; }

	void SetCommand(int32 Cmd) { Command = Cmd; }
	// 0x190C28
	void ApplyFocusCamera();
	// 0x190C70
	void StoreCamera();
	// 0x190C40, 0x194D78
	bool InsertCameraCut(uint32 Frame) { return Tracks[Focus].Insert(Frame, W().GetCamera(), bAdvanced); }
	// 0x190C88, 0x1951D0
	bool CanInsertCameraCut() const { return bAdvanced && Tracks[Focus].Count < SSXReplay::MaxCuts; }
	// 0x1921E0
	int32 ClipMarkAllowed() const;
	// 0x191D88, 0x190FF0
	struct FTimeline
	{
		int32 KeyCount = 0;
		uint32 Length = 0;
		uint32 Keys[16] = {};
		TArray<int32> Clips;
		const SSXReplay::FCameraTrack* Track = nullptr;
	};
	void GetTimeline(FTimeline& Out) const;
	// 0x190CB8
	bool DeleteCamera(uint32 Frame);
	// 0x1925F8
	bool DeleteClipAtFrame();
	// 0x191B18
	void SetPlaylist(const TArray<int32>* List, int32 Cur = 0);

	void HighlightStart(int32 Rider, int32 Type);   // 0x193370
	void HighlightValue(int32 Rider, float V);
	void HighlightImpact(int32 Rider, float V);
	void HighlightCommit(int32 Rider);              // 0x193448
	void HighlightClear(int32 Rider);
	void HighlightCall(int32 Rider, int32 Slot, float Arg)
	{
		switch (Slot)
		{
		case 15: HighlightStart(Rider, int32(Arg)); break;
		case 16: HighlightValue(Rider, Arg); break;
		case 17: HighlightImpact(Rider, Arg); break;
		case 19: HighlightCommit(Rider); break;
		case 20: HighlightClear(Rider); break;
		default: break;
		}
	}
	void AttachHuman(int32 Rider);

	int32 GetState() const { return State; }
	int32 GetCommand() const { return Command; }
	uint32 GetRecordFrame() const { return RecFrame; }
	uint32 GetRecordedLength() const { return RecLength; }
	uint32 GetPlayFrame() const { return PlayFrame; }
	int32 GetFocus() const { return Focus; }
	void SetFocus(int32 R) { Focus = R; }
	bool IsFull() const { return bFull; }
	int32 GetFullMessage() const { return FullMessage; }
	bool IsEnded() const { return bEnded; }
	bool IsLoaded() const { return bLoaded; }
	void SetLoaded(bool b) { bLoaded = b; }
	bool IsLooping() const { return bLoop; }
	bool HasPlaylist() const { return Playlist.Num() > 0; }
	int32 GetKeyframePool() const { return Keys.Num(); }
	uint32 GetLockInterval() const { return LockInterval; }
	int32 GetHighlightSlots() const { return QuotaSlots; }
	TArray<uint32> KeyframeFrames() const;
	const SSXReplay::FKeyframe& GetKey(int32 I) const { return Keys[I]; }
	const SSXReplay::FClip& GetClip(int32 I) const { return Clips[I]; }
	const TArray<int32>& GetRiderClips(int32 Rider) const { return RiderClips[Rider]; }
	const SSXReplay::FHighlights& GetHighlights(int32 Rider) const { return Hl[Rider]; }
	SSXReplay::FInputRecorder& GetInput(int32 Rider) { return Input[Rider]; }
	SSXReplay::FCameraTrack& GetTrack(int32 Rider) { return Tracks[Rider]; }
	bool IsAdvanced() const { return bAdvanced; }
	// 0x195460
	uint32 PlaylistClipEnd() const;
	int32 GetPlaylistIndex() const { return PlaylistCur; }
	const TArray<int32>& GetPlaylist() const { return Playlist; }

private:
	void Execute();                                  // 0x191830
	void Step(bool bRestore);                        // 0x190878
	void Rewind();                                   // 0x18F0D8
	void StartPlayback(int32 PlaylistMarker);
	void BuildAttract();
	int32 InsertKeyframe(uint32 Frame, bool bDropScripts);   // 0x18F328
	int32 AllocKeyframe();                           // 0x18EE68
	void EvictKeyframe();                            // 0x18EF20
	int32 FindEvictable(int32 From) const;           // 0x18EEC0
	void FreeKeyframe(int32 K);                      // 0x18EFA0
	void Prune(bool bAll);                           // 0x191778
	void MaybeFreeKeyframe(int32 K);                 // 0x191728
	int32 FindKeyframe(uint32 Frame) const;          // 0x190DB0
	int32 NextKeyframe(uint32 Frame) const;          // 0x190E78
	int32 PrevKeyframe(uint32 Frame) const;          // 0x190EE8
	bool IsFocusClipKey(int32 K) const;              // 0x190F58
	void SeekAll(uint32 Frame);                      // 0x192758
	int32 CreateClip(uint32 Frame, int32 Rider);     // 0x191EB0
	void DeleteClip(int32 Rider, int32 C);           // 0x192058
	void HighlightCancel(int32 Rider);               // 0x1932F0
	int32 PickSlot(int32 Rider, int32 C) const;      // 0x1935D8
	int32 LowestSlot(int32 Rider, int32 Type) const; // 0x1934F0
	void DropPendingHighlights();
	void ResetCameraTracks();                        // 0x190D40
	int32 TailPrevKey() const;                       // 0x191820
	bool MarkClipStep();                             // 0x1922A8
	void EndClipMark();                              // 0x1924A0
	int32 MarkClip = -1;
	int32 MarkStart = -1;
	void RebuildFrame();
	bool bRebuild = false;
	TArray<int32> RebuildKeys;
	int32 RebuildNext = 0;
	uint32 RebuildRecFrame = 0;

	SSXReplay::IReplayWorld* World = nullptr;
	SSXReplay::IReplayWorld DefaultWorld;
	SSXReplay::IReplayWorld& W() const { return World ? *World : const_cast<SSXReplay::IReplayWorld&>(DefaultWorld); }

	int32 Head = -1;
	int32 FreeKeys = -1;
	int32 Tail = -1;
	int32 Current = -1;
	int32 EvictCursor = -1;
	int32 EndKey = -1;
	int32 State = SSXReplay::Idle;
	int32 Focus = 0;
	int32 Command = 0;
	uint32 RecFrame = 0;
	uint32 RecLength = 0;
	uint32 PlayFrame = 0;
	uint32 LockInterval = 2400;
	int32 FullMessage = 0;
	bool bFull = false;
	bool bStepped = false;
	bool bLoaded = false;
	bool bEnded = false;
	bool bLoop = false;
	bool bAdvanced = false;
	int32 QuotaSlots = 4, QuotaTricks = 3, QuotaWipeOuts = 1;

	TArray<SSXReplay::FKeyframe> Keys;
	SSXReplay::FClip Clips[SSXReplay::ClipPool];
	int32 FreeClips = -1;
	TArray<int32> RiderClips[SSXReplay::MaxRiders];
	TArray<int32> Playlist;
	int32 PlaylistCur = 0;
	SSXReplay::FHighlights Hl[SSXReplay::MaxRiders];
	SSXReplay::FInputRecorder Input[SSXReplay::MaxRiders];
	bool bHasInput[SSXReplay::MaxRiders] = {};
	SSXReplay::FCameraTrack Tracks[SSXReplay::MaxRiders];
};
