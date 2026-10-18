// 0x1CF040
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXUISounds.h"

class FSSXFeDrawList;
class FSSXLocStrings;
struct FSSXProfile;

// PLATFORM ADAPTATION: memory card slot 1 is the local folder <project>/Saved/MemoryCard/ with the original directory and file
// names; the card is always present and formatted, slot 2 always empty, free space reported for an 8 MB card.
class SSXTRICKYREBUILD_API FSSXMemCard
{
public:
	enum class EMode : uint8 { Replay = 0, Game = 1 };

	enum EState : int32
	{
		Idle = 1, Checking = 2, CheckDone = 3, NoCard = 4, NoCard2 = 5, Reading = 6, SaveList = 7, Overwrite = 9,
		Unformatted = 0xA, Formatting = 0xB, FormatFailed = 0xC, SaveTitle = 0xD, SaveDeleteOld = 0xE, SaveMkdir = 0xF,
		SaveStream = 0x11, SaveWait = 0x12, SaveDone = 0x13, SaveFailed = 0x14, TooBig = 0x15, TooBig2 = 0x16,
		DeleteAsk = 0x17, Deleting = 0x18, Deleting2 = 0x19, DeleteDone = 0x1A, DeleteFailed = 0x1B,
		LoadList = 0x1C, Loading = 0x1D, Loading2 = 0x1E, Loading3 = 0x1F, Loading4 = 0x20, LoadDone = 0x21, LoadFailed = 0x22,
	};

	// 0x290DE0
	enum EInput : uint32 { InAccept = 0x800, InBack = 0x20, InCircle = 0x40, InUp = 0x20001, InDown = 0x40002, InStart = 0x80 };

	static FString RootDir();
	static FString& RootOverride();
	static FString SettingsName() { return TEXT("BASLUS-20326-SET0001"); }
	static FString ReplayName(int32 Index) { return FString::Printf(TEXT("BASLUS-20326-REP%04d"), Index + 1); }
	static FString FilePath(const FString& Name) { return RootDir() / Name / Name; }
	// 0x23FF18
	static int32 KbRequired(int32 Bytes) { return (Bytes + 1023) / 1024 + 131; }

	// 0x28F260
	void Begin(EMode InMode, bool bInSave, FSSXProfile* InProfile);
	void Tick();
	void Input(uint32 Bits);
	bool IsActive() const { return State != Idle; }

	void DrawFe(FSSXFeDrawList& D, const FSSXLocStrings& Loc, int32 Frame) const;

	/** In-race screen (cSaveOverlay) of the current state: the manager's per-state draw 0x1CF6E8 -> vslots 17..42, with cSaveOverlay's overrides of vslot 14 (message box 0x1B9530), 16 (status 0x1B9968), 17 (save list 0x1B8D08), 18 (saving 0x1B8718) and 27 (warning 0x1B8BB8). Drawn by FSSXOverlayManager. */
	struct FRaceView
	{
		enum EKind : uint8 { None, Status, Dialog, List, Saving } Kind = None;
		int32 TextLoc = -1;     // 0x1D2FE8
		int32 Button1 = -1;
		int32 Button2 = -1;
		bool bXFirst = true;
		bool bWarning = false;
	};
	FRaceView RaceView() const;
	// 0x1D2FE8
	FString MessageText(const FSSXLocStrings& Loc) const;
	/** cPS2MemCard vfunc_14 (vslot 0x74, 0x23FB60): a file's list name = the title the save wrote into its icon.sys (card slot
	 *  record +0x1E8, read back by 0x23D680), LOC 351 when the slot is empty. */
	FString FileTitle(const FSSXLocStrings& Loc, int32 I) const;
	/** The save title 0x1CE8F8 (state 0xD): replays sprintf(LOC 460 "SSX Tricky Replay%d %S", slot + 1, the first 4 chars of the
	 *  track table 0x332F28 [venue] +0x10 = map stem), settings LOC 459. */
	FString IconTitle(const FSSXLocStrings& Loc, int32 Slot, int32 Venue) const;
	// 0x1CE8F8 / 0x332F28
	static FString TrackCode(int32 Venue);
	static int32 FileVenue(int32 I);
	// 0x1CF040
	bool FileValid(int32 I) const { return FileExists(I); }
	/** Legend entries greyed (vslot 0x104(0)) [FUN_00292968 on the list states and moves, FUN_002924d0(0) on the status states,
	 *  FUN_002923b8 -> FUN_002924d0(1) on every other state change, vfunc_45 0x40 for an unusable port]. */
	uint32 LegendDisabledMask() const;
	// FUN_00275468
	bool ListFocused() const { return State == SaveList || State == LoadList; }
	int32 RaceTitleLoc() const { return Mode == EMode::Replay ? 450 : 452; }
	// 0x1D0870
	float SaveProgress() const { return State == SaveWait || State == SaveDone ? 1.f : 0.f; }

	EState State = Idle;
	EMode Mode = EMode::Game;
	bool bSave = true;
	int32 Selected = 0;
	int32 Timer = 0;
	float WarnAlpha = 1.f;
	float WarnDir = -1.f;
	bool bLoadedSettings = false;
	/** Replay mode (manager +0 == 0, Race/SSXReplayFile.h): the file a save writes (cReplayOverlay vfunc_04 / 05 stream it
	 *  in the original; the in-race replay menu hands the finished 0x80000 bytes over before Begin). */
	TArray<uint8> ReplayFile;
	/** A replay load that passed the checksum (0x2912E8): the bytes (0x166800's buffer) and the flag +0x3710 (0x166870);
	 *  the caller starts the loaded replay (0x2917B0 -> 0x167060). A replay load shows no LOC 371 (settings only). */
	bool bLoadedReplay = false;
	TArray<uint8> LoadedReplay;
	/** cSaveOverlay (in race) sounds, vfunc_48..54 0x1BA0F0: the navigate sound 0x1A8648 plays when the move is not at
	 *  the list's end (even when the list would wrap); accept (vfunc_50 / 53) and back (vfunc_54) on the state handlers'
	 *  buttons (see Input). The FE overlay's slots are empty stubs, so only bInRace emits them. Drained by the race UI into
	 *  FSSXOverlayManager::Sounds (played by SSXGameAudio::TickRaceOverlays). +0x3C wrap flag: the list wraps at its ends. */
	bool bInRace = false;
	bool bWrap = false;   // 0x1CE490
	TArray<SSXUISound::ERace> Sounds;
	void MovePort(bool bDown);
	void AcceptPort();
	/** The port menu (overlay menu id 0, vfunc_12 0x28F3E0) is shown at the build and hidden by vfunc_50 0x2916C8 on the
	 *  manager's first update with the card idle (0x1CEF78), before the checking starts: with the folder card it is never
	 *  seen (Begin clears it). Its entries keep the build's " " text in the FE (vfunc_45 runs only from vfunc_48 / 49). */
	bool bPortMenu = false;
	int32 Port = 0;
	/** Port entry text FUN_001D2D50 (vfunc_45 / cSaveOverlay vfunc_24): port record +0x174 / +0x168 == 0 -> LOC 366, +0x16C
	 *  == 0 -> 384, else "%s %d %s" (LOC 390, free KB, LOC 378). The records are zeroed by the ctor and filled (0x1D3818)
	 *  only when the reading ends, so it reads 366 before that. Port 0 is the folder card; port 1 has no card. */
	bool PortValid(int32 P) const { return P == 0; }
	int32 FreeKb() const;
	int32 MaxFiles() const { return Mode == EMode::Replay ? 4 : 1; }
	FString FileName(int32 I) const { return Mode == EMode::Replay ? ReplayName(I) : SettingsName(); }
	bool FileExists(int32 I) const;
	// 0x291E00
	int32 MessageLoc() const;
	uint32 DialogMask() const;
	uint32 HelpMask() const;
	int32 FeTitleLoc() const { return Mode == EMode::Replay ? 3543 : (bSave ? 3541 : 3542); }

	/** The load list's deferred buttons (0x1D1498: DAT_0033EFF4 X, EFF8 Circle, EFFC Triangle) and 0x22's X (0x1D0FF0: DAT_0033EFF0):
	 *  the press sets the flag (and plays its sound), the next update acts on it and reads no input that frame. */
	enum class EPending : uint8 { None, X, Circle, Triangle, FailX };
	EPending Pending = EPending::None;

private:
	void Go(EState S, int32 Frames = 0) { State = S; Timer = Frames; }
	// 0x1CE9F8
	void BeginLoad();
	bool bPendingFresh = false;
	/** cSaveOverlay vfunc_53 (accept 0x2138F0) / vfunc_54 (back 0x213F88), called by the state handlers on their buttons;
	 *  the FE overlay's slots are empty stubs. */
	void SndAccept() { if (bInRace) Sounds.Add(SSXUISound::ERace::Accept); }
	void SndBack() { if (bInRace) Sounds.Add(SSXUISound::ERace::Back); }
	void MoveFile(bool bDown);
	bool WriteSettings();
	bool ReadSettings();
	bool WriteReplay();
	bool ReadReplay();
	// 0x23FF18
	int32 SaveBytes() const;
	FSSXProfile* Profile = nullptr;
};
