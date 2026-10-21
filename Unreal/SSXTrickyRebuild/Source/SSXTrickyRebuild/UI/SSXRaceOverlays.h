#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXUISounds.h"
#include "UI/SSXFeTypes.h"
#include "UI/SSXReplayControl.h"
#include "UI/SSXMemCard.h"

class FSSXFeDrawList;
class FSSXLocStrings;
class ISSXRaceUIHost;
struct FSSXProfile;
struct FSSXCheats;
struct FSSXPointSession;

// 0x1A6FB8
enum ESSXOverlay : int32
{
	OvHUD = 0, OvRaceResults = 1, OvHiScoreRecord = 2, OvNextRace = 3, OvCountdown = 4, OvReplayControl = 5,
	OvPauseMenu = 6, OvPopFinish = 7, OvPopAsyncLoad = 8, OvReplay = 9, OvLesson = 10, OvViewHiScores = 11,
	OvBlackFade = 12, OvUnlocked = 13, OvLessonController = 14, OvPointDistribution = 15, OvSelectNextVenue = 16,
	OvSelectNextEvent = 17, OvHelpRace = 18, OvCount = 19,
};
SSXTRICKYREBUILD_API const TCHAR* LexToString(ESSXOverlay O);

/**
 * cBackdropOverlay shared panel (ctor 0x1A8900, animations 0x1A89B0 / 0x1A8AD8 / 0x1A8B70, tick 0x1A8BD8).
 * The ctor sets the animation flags +0x60 / +0x7C / +0x90 to 1 (= none): every overlay shows its box at full size,
 * alpha 0.65 and text alpha 1. Only the pause menu's open (0x1ABC00) starts the grow (flag 0x40: 60 -> the drawn
 * height at 13 / frame), the alpha ramp (flag 2: 0 -> 0.88 at 0.034), the text-alpha ramp and the title-tab slide
 * (x from -850 by +25 / frame).
 */
struct SSXTRICKYREBUILD_API FSSXBackdrop
{
	float Height = 60.f;
	float Target = 500.f;
	float Alpha = 0.65f;
	float AlphaCap = 0.65f;
	float TextAlpha = 1.f;
	float TabX = 0.f;
	bool bAnimated = false;
	bool bOpen = false;
	void Open();
	// 0x1ABC00
	void OpenAnimated(float InAlphaCap);
	void Tick();
	// 0x1A9BB8
	void Box(FSSXFeDrawList& D, float H, int32 Layer);
	bool IsGrown() const { return Height == Target; }
};

// 0x1AA090 / 0x1AA268 / 0x1AAC08
struct SSXTRICKYREBUILD_API FSSXOvDialog
{
	bool bActive = false;
	int32 Line1 = -1, Line2 = -1;
	int32 LocX = -1, LocCircle = -1, LocTriangle = -1;
	int32 Context = 0;
	void Show(int32 InContext, int32 InLine1, int32 InX, int32 InCircle, int32 InTriangle, int32 InLine2 = -1);
	int32 Result(uint32 NewBits) const;
	// 0x1AA268
	void Draw(FSSXFeDrawList& D, const FSSXLocStrings& Loc, FSSXBackdrop& Bd) const;
};

class SSXTRICKYREBUILD_API FSSXOverlayManager
{
public:
	void Init(FSSXProfile* InProfile, FSSXCheats* InCheats, const FSSXLocStrings* InLoc);
	void SetHost(ISSXRaceUIHost* InHost) { Host = InHost; }

	// 0x1A7848 / 0x1A7860
	void Activate(int32 Idx, int32 Player = -1);
	void Deactivate(int32 Idx);
	bool IsActive(int32 Idx) const { return Idx >= 0 && Idx < OvCount && Active[Idx]; }
	/** 0x1A77A8: an active overlay whose +0x10 is set (0x1A6FB8: slots 1, 2, 6, 10, 11, 13, 15, 16, 17): the HUD is drawn
	 *  at alpha 0.5 / 0.35 (FUN_001A2C40 / FUN_001A4F70). */
	bool AnyHudDim() const
	{
		return Active[OvRaceResults] || Active[OvHiScoreRecord] || Active[OvPauseMenu] || Active[OvLesson] || Active[OvViewHiScores]
			|| Active[OvUnlocked] || Active[OvPointDistribution] || Active[OvSelectNextVenue] || Active[OvSelectNextEvent];
	}
	// 0x1A77F8
	bool AnyModal() const { return Active[OvPauseMenu] || (Active[OvReplay] && ReplayState != 1); }
	// 0x1EF990
	bool IsReplayMenuOpen() const { return Active[OvReplay] && ReplayState == 2; }
	/** The HUD gate's card-save test (cHUDOverlay vfunc_13/14): the pause (0x1A7C58) or results (0x1A7C88 -> +4) overlay in +0x430 == 2, the memory-card save of its quit prompt. */
	bool IsHudCardSave() const { return SaveFrames > 0 && bQuitAfterSave && (QuitFrom == QuitFromPause || QuitFrom == QuitFromResults); }

	void Pad(int32 Player, uint32 NewBits);
	/** Held pad word of player Player (pad +0x10: buttons, left stick 0x20000.., right stick 0x200000..), every frame
	 *  before Tick; the replay control (slot 5) reads held and previous words. */
	void SetPadHeld(int32 Player, uint32 Held) { if (Player >= 0 && Player < 2) PadHeld[Player] = Held; }
	const FSSXReplayControl& GetReplayControl() const { return ReplayCtl; }
	// 0x181638
	bool CanPause() const;
	void Tick();
	void Draw(FSSXFeDrawList& D) const;

	enum class EAction : uint8 { None, Restart, QuitToFrontEnd, Resume, Pause };
	EAction ConsumeAction() { const EAction A = PendingAction; PendingAction = EAction::None; return A; }
	/** UI sounds played this frame (FUN_002138F0 family); drained by SSXGameAudio::TickRaceOverlays. Every accept / navigate / back / point change / denied jal (0x1A8668 / 0x1A8648 / 0x1A8658 / 0x1A8638 / 0x1A8678) in the overlay pad handlers is wired, plus the replay control's 0x1EB6D0 sounds and the in-race card manager's (cSaveOverlay vfunc_48..54 0x1BA0F0..; FSSXMemCard::Sounds). */
	TArray<SSXUISound::ERace> Sounds;

	int32 PauseDepth = -1;
	int32 PauseSel = 0;
	int32 OptionsSel = 0;
	bool bTrickBook = false;
	int32 TrickPage = 0;
	/** HUD detail 0 full, 1 minimal, 2 none = settings 0x32CA40 (getter 0x17F158, setter 0x17F168): saved with the
	 *  settings and kept across races. Without a profile an overlay-local value stands in (tests). */
	int32 GetHudDetail() const;
	void SetHudDetail(int32 V);
	int32 PausePlayer = 0;
	FSSXOvDialog Dialog;

	// 0x38E328
	struct FPauseItem { int32 Loc; int32 Action; };
	static const TArray<FPauseItem>& PauseItems();
	// 0x38E380
	TArray<int32> PauseOptionRows() const;
	FString PauseOptionValue(int32 RowLoc) const;
	float PauseOptionSlider(int32 RowLoc) const;
	int32 OptionValue(int32 RowLoc) const;
	bool bAudioLevelsChanged = false;
	const FSSXProfile* GetProfile() const { return Profile; }
	// FUN_00164060 / FUN_00164110
	int32 TrickBookChapter() const;
	static int32 ChapterFirstPage(int32 Chapter) { return Chapter >= 0 && Chapter <= 6 ? Chapter * 5 : 0; }

	int32 ResultsSel = 0;
	TArray<int32> ResultsItems() const;
	int32 NextEventSel = 0;
	bool bNextEventBack = false;
	int32 NextVenueSel = 0;
	bool bNextVenueBackToResults = false;
	int32 NextVenueMode = 2;
	TArray<int32> NextVenueList() const;
	int32 PointSel = 0;
	int32 Frame = 0;
	int32 HiScorePending = 0;
	int32 HiGridX = 0, HiGridY = 0;
	FString HiName;
	bool bHiReplace = true;
	int32 RankCardFrames = 0;
	int32 ReplaySel = 0;
	/** cReplayOverlay state +0x438 (0x1EF990): 1 playing (ReplayControl, slot 5, active), 2 menu, 0 the memory-card save
	 *  (vfunc_15 0x1EF750: the manager runs until idle, then state 2), 4 the loaded replay's exit confirm (0x1F00C8: X ->
	 *  5, Triangle -> 2); 5 exit is transient. */
	int32 ReplayState = 1;
	// 0x1CE778
	FSSXMemCard ReplayCard;
	// 0x1AB148
	bool BlinkOn(int32 Idx) const { return Idx >= 0 && Idx < OvCount && Blink[Idx] < 36 && Blink[Idx] > 0; }
	// FUN_001F1448
	int32 UnlockReveal(int32 Row) const;
	// 0x1B5EF8 / FUN_001B5BF8
	struct FUnlockRow { FString Text; int32 Icon = -1; };
	TArray<FUnlockRow> UnlockRows;
	TArray<int32> UnlockPlayers;
	float GetPulse() const { return Pulse; }

private:
	void PausePad(uint32 Bits);
	void OptionsPad(uint32 Bits);
	void PauseDialogResult(int32 R);
	// 0x215488
	void ClosePause(bool bResume = true);
	void ResultsPad(uint32 Bits);
	void NextEventPad(uint32 Bits);
	void NextVenuePad(uint32 Bits);
	void PointsPad(uint32 Bits);
	void HiScorePad(uint32 Bits);
	void ReplayPad(uint32 Bits);
	/** 0x1EF990(.., 5): slots 9 and 5 off, 0x191BE0 unless loaded, the return slot (+0x440) or, with none (-1: a loaded
	 *  replay started by cGame::vfunc_05), the race is left (world+0x54 = 1). */
	void ReplayExit();
	void OpenHiScoreEntry();
	void AfterHiScores();
	void AfterPoints();
	void QuitPrompt(int32 From);
	void Quit();
	void Exit(int32 Code);
	void StartPostRace();
	void SaveSettingsInRace();
	void BuildUnlockRows();

	void DrawPause(FSSXFeDrawList& D) const;
	void DrawPauseTrickBook(FSSXFeDrawList& D, float Top) const;
	void DrawPauseOptions(FSSXFeDrawList& D) const;
	// 0x1AA630
	void DrawList(FSSXFeDrawList& D, float X, float Y, float Scale, uint32 Align, const TArray<FString>& Strs, int32 Sel,
		const TArray<bool>* Enabled, int32 Icon, float A, int32 Layer, bool bPulse = true) const;
	// 0x1A9928
	void DrawTab(FSSXFeDrawList& D, float Y, const FSSXBackdrop& Bd, const FString& Str, int32 Layer) const;
	// 0x1AAEC0
	void DrawVenueTitleBar(FSSXFeDrawList& D, FSSXBackdrop& Bd, float H) const;
	// 0x1AB1A0
	struct FTable;
	void DrawTable(FSSXFeDrawList& D, const FSSXBackdrop& Bd, const FTable& T, int32 Mode) const;
	void DrawResults(FSSXFeDrawList& D) const;
	void DrawNextEvent(FSSXFeDrawList& D) const;
	void DrawNextVenue(FSSXFeDrawList& D) const;
	void DrawPoints(FSSXFeDrawList& D) const;
	void DrawUnlocked(FSSXFeDrawList& D) const;
	void DrawViewHiScores(FSSXFeDrawList& D) const;
	void DrawHiScoreRecord(FSSXFeDrawList& D) const;
	void BuildRecordTable(FTable& T, int32 Track, int32 Kind, int32 Highlight) const;
	void DrawRankCard(FSSXFeDrawList& D) const;
	FSSXPointSession* PointSession() const;
	void DrawCountdown(FSSXFeDrawList& D) const;
	void DrawPopFinish(FSSXFeDrawList& D) const;
	void DrawHelpRace(FSSXFeDrawList& D) const;
	void DrawNextRace(FSSXFeDrawList& D) const;
	void DrawReplayMenu(FSSXFeDrawList& D) const;
	/** cReplayControlOverlay::vfunc_13 0x1EC808 (slot 5) and its parts 0x1ECEA0 / 0x1ED490 / 0x1EDEF0 / 0x1EE720 /
	 *  0x1EEE00. */
	void DrawReplayControl(FSSXFeDrawList& D) const;
	// 0x1EB6D0
	void TickReplayControl();
	FSSXReplayControl ReplayCtl;
	uint32 PadHeld[2] = { 0, 0 };
	// 0x1A87A0
	uint32 RepeatLast = 0;
	int32 RepeatCount = 0;
	/** 0x1A87A0 for the held word Held: the word on a change, then while unchanged at hold counts 12, 24, 36, 48, 60 and
	 *  every 2nd frame after; 0 otherwise. bChanged = this call was a change (the press, delivered by Pad). */
	uint32 RepeatWord(uint32 Held, bool& bChanged);
	// 0x1ADD28
	void TickPauseRepeat();
	uint32 ReplayPrevHeld = 0;
	/** The replay menu closed during this frame's input: the replay control (slot 5) updates before the replay overlay
	 *  (slot 9) in the original, so it still sees the menu busy and the closing press (X / Start) does not reach it. */
	bool bReplayMenuClosedThisFrame = false;
	/** Trick Tutorial (UI/SSXLessonDraw.cpp): the lesson object's render cLessonMan::vfunc_03 0x14BBE8 (the pad diagram
	 *  in Preview / Perform) and cLessonOverlay::vfunc_13 0x1B8548 (slot 10: the lesson menu and the result pages). */
	void DrawLesson(FSSXFeDrawList& D) const;
	void DrawSaveBox(FSSXFeDrawList& D, FSSXBackdrop& Bd) const;
	// 0x1B8718
	void DrawSaveProgress(FSSXFeDrawList& D, FSSXBackdrop& Bd, float P, const FString& S) const;
public:
	void DrawCardRace(FSSXFeDrawList& D, FSSXBackdrop& Bd, const FSSXMemCard& Card) const;
private:
	FString L(int32 Id) const;
	int32 Mode() const;
	int32 NumHumans() const;

	bool Active[OvCount] = {};
	int32 ActiveFrame[OvCount] = {};
	int32 Blink[OvCount] = {};
	mutable FSSXBackdrop Backdrop[OvCount];
	// Drawn pulses step once per 60 Hz frame (Tick), not per drawn frame (0x1AA630 ran at the PS2's 60 Hz).
	mutable bool bPulseDrawn = false, bLessonPulseDrawn = false, bAggressionDrawn = false;
	float Pulse = 1.f, PulseStep = 0.004f;
	float AggressionAnim = 0.f;
	float LessonPulse = 1.f, LessonPulseStep = 0.004f;
	ISSXRaceUIHost* Host = nullptr;
	FSSXProfile* Profile = nullptr;
	int32 LocalHudDetail = 0;
	FSSXCheats* Cheats = nullptr;
	const FSSXLocStrings* Loc = nullptr;
	EAction PendingAction = EAction::None;

	int32 GoFrames = -1;
	int32 CountdownDigit = 0;
	float CountdownScale = 1.f;
	int32 HudOpensSeen = 0;
	int32 CountdownOpensSeen = 0;
public:
	int32 GetCountdownDigit() const { return CountdownDigit; }
	float GetCountdownScale() const { return CountdownScale; }
	bool IsGoShowing() const { return !Active[OvCountdown] && GoFrames >= 0; }
private:
	int32 LastPhase = -1;
	/** cPopFinishOverlay per-viewport record (stride 0x3C): +0x30 delay 8, +0x34 on, +0x2C place shown, +0x38 Time
	 *  Challenge, +0x3C medal icon, +0x40 place char, +0x42 value string, +0x64 alpha (+0.05 / frame) [0x1AE7F0]. */
	struct FFinish
	{
		bool bOn = false; int32 Delay = 8; float Alpha = 0.f;
		bool bPlace = false; bool bTC = false; int32 Icon = -1; TCHAR PlaceChar = 0; FString Value;
		bool bScored = true;
		int32 TimeCs = 0;
	};
	FFinish Finish[2];
	int32 AsyncFrames = 0;
	int32 UnlockStep = 0;
	TArray<int32> UnlockValue, UnlockTarget;
	bool bPostRaceStarted = false;
	int32 PostRaceFade = 0;
	int32 SaveFrames = 0;
	float SaveWarnAlpha = 1.f;    // 0x1CEEE0
	float SaveWarnDir = -1.f;
	int32 AfterSaveExit = 0;
	bool bQuitAfterSave = false;
public:
	enum EQuitFrom : int32 { QuitFromPause = 0, QuitFromResults = 1, QuitFromNext = 2 };
	int32 QuitFrom = QuitFromPause;
private:
	int32 ReplayReturn = OvRaceResults;
	int32 TimeToBeatCs = -1;
};
