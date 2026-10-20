#pragma once

#include "CoreMinimal.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeDvd.h"
#include "UI/SSXFeOptions.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeRiders.h"
#include "UI/SSXFeTypes.h"
#include "World/SSXPropMovie.h"
#include "UI/SSXMemCard.h"
#include "Race/SSXReplayFile.h"

class FSSXFeDrawList;
class FSSXLocStrings;

enum class ESSXFeScreen : uint8
{
	Title, ModeSelect, EventSel, NumPlayers, CharacterSelect, CharacterAccept, CharacterModify, TrickBook,
	VenueSel, DifficultSel, UserName, DVDContent, Video, CustomBoard, CustomBody, RiderProfile, Options, MemCard,
};
SSXTRICKYREBUILD_API const TCHAR* LexToString(ESSXFeScreen S);
SSXTRICKYREBUILD_API int32 SSXFeTypeId(ESSXFeScreen S);

enum class ESSXFeBoot : uint8 { StartScreen, FirstLoad, EaLogo, Intro, FrontEnd, Attract, Loading };

struct SSXTRICKYREBUILD_API FSSXFeItem
{
	int32 Id = 0;
	int32 Loc = -1;
	FString Text;
	bool bEnabled = true;
	int32 HelpLoc = -1;
};

struct SSXTRICKYREBUILD_API FSSXFeState
{
	ESSXFeScreen Screen = ESSXFeScreen::Title;
	/** +0x10 [cFE3DState::vfunc_10 0x274C78]: 0 opening (slide-in; vfunc_07 -> 2 when done), 1 closing by Back (slide-out,
	 *  then vfunc_08 pops), 2 active (input), 3 closing forward (slide-out, then vfunc_21 -> 4), 4 closed-wait-camera
	 *  (then vfunc_08 builds the next state), 5 done (the next state is up). */
	int32 Phase = 0;
	int32 Frame = 0;
	int32 TitleLoc = -1;        // FUN_002720F8
	float BarH = 0.f;           // FUN_00273F18
	TArray<FSSXFeItem> Items;
	int32 Sel = 0;
	bool bVertical = true;
	bool bSelectedOnly = false; // FUN_00274000
	bool bWrap = false;
	SSXFe::EFont ItemFont = SSXFe::EFont::Title;
	int32 OpenFrame = 0;
	/** Frames since the close started (cFE3DState::vfunc_06: the three group tweens replayed in reverse, phase 3);
	 * -1 = not closing. */
	int32 CloseFrame = -1;
	int32 ArrowFlashAt[2] = { -1000, -1000 };
	uint32 HelpMask = 0;
	bool bBack = true;
	/** +0x20 in-transition: the fade vfunc_05 (Back) starts, colour white: 1 = FUN_001FF648(0.5, 0, 0.5), 2 = (0, 0, 1.5),
	 * 0 none. ctor FUN_002715C8 = 1; the screens' vfunc_12 / vfunc_15 change it. */
	int32 InFade = 1;
	/** +0x24 out-transition: the fade vfunc_06 (Accept) starts, colour black (same kinds); ctor 0, reset by
	 *  cFE3DState::vfunc_15, set by the screens' vfunc_27. */
	int32 OutFade = 0;
	bool bCamSkip = true;
	bool bSquare = true;
	int32 Player = 0;
	int32 Page = -1;
	int32 PageSel = 0;
	FSSXFeOptionsUI Opt;
	// FUN_00284F60 / FUN_00284C70
	FSSXFeScrollText ProfileText[2];
	// FUN_002720F8 / 0x28B968
	float HelpW = SSXFe::HelpLineW;
	int32 SavedValue = 0;
	int32 Chapter = -1;         // FUN_0027A288
	bool bBookTricks = false;   // FUN_00275468
	int32 BookTrick = 0;
	FString BookTrickName;
	int32 BookPicSprite = -5;
	FLinearColor BookPicColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
	int32 BookVideo = -1;
	FSSXFeDvd Dvd;
	int32 BookArrowFlashAt[2] = { -1000, -1000 };
	int32 GridX = 0, GridY = 0;
	FString Name;
	bool bNameFresh = false;
	/** The main list (+0x3C) holds the focus (FUN_00275468); false when another widget has it (UserName: the grid), so the
	 *  "< Item >" list shows its item in the normal colour. */
	bool bListFocused = true;
	int32 PanelFrame = 0;
	bool bRankingView = true;

	/** Slide position of the frame groups (title bar + list, top group, bottom group): 0 = off screen (the build
	 * start), 1 = in place. Opening: OpenFrame / 30; closing: the same tween in reverse. */
	float SlideT() const;
	/** The close's reverse tweens have finished: the groups are off screen and nothing of the screen is drawn
	 * (phase 3 -> vfunc_21 -> phase 4). */
	bool IsSlidOut() const;
	bool IsSlidIn() const;
	/** The state has frame groups (title bar +0x38, top group +0x40, bottom group +0x44) whose tweens the phases 0 / 1 / 3
	 *  wait for; Title and DVDContent have none, so their phases advance on the next update. */
	bool HasGroups() const { return TitleLoc >= 0 || BarH > 0.f; }
	const FSSXFeItem* Selected() const { return Items.IsValidIndex(Sel) ? &Items[Sel] : nullptr; }
	int32 SelectedId() const { return Items.IsValidIndex(Sel) ? Items[Sel].Id : -1; }
};

struct SSXTRICKYREBUILD_API FSSXFeLaunch
{
	int32 GameMode = 0;
	int32 Track = 0;
	int32 Difficulty = 0;
	int32 Players = 1;
	FSSXPlayerSlot Slots[2];
	/** Racer count chosen by the post-race next-event / Freeride branch (DAT_0032EBAC, cApplication vfunc_04 0x17BBD8 ->
	 *  RS+0); -1 for an FE launch, whose count comes from 0x2657A8 (SSXSettings::RacerCount). */
	int32 Racers = -1;
};

/** The screen fader app+0x72C, type 2 (FUN_001FF648 start, FUN_001FECE8 update, FUN_001FF000 draw): a full-screen quad
 *  of one colour whose alpha ramps 0 -> 1 over Up seconds, holds Hold seconds, ramps 1 -> 0 over Down seconds (+1/60
 *  per frame); blend 3 (alpha). A start is ignored while a fade runs. */
struct SSXTRICKYREBUILD_API FSSXFeFader
{
	bool bActive = false;
	int32 Stage = 0;
	float T = 0.f;
	float Up = 0.f, Hold = 0.f, Down = 0.f;
	float Alpha = 0.f;
	FLinearColor Color = FLinearColor::Black;
	bool Start(float InUp, float InHold, float InDown, const FLinearColor& InColor);
	void Tick();
	bool StartKind(int32 Kind, const FLinearColor& InColor);
};

class SSXTRICKYREBUILD_API FSSXFrontEnd
{
public:
	/** bColdBoot: cFE3D(1) runs the boot chain; cFE3D(0) (after a race) opens on Title [2.1, 9]. InExitFlag: the race exit
	 *  code DAT_0032E5F0 as the FE sees it (warm start only; cApplication::vfunc_04 0x17BBD8 has cleared bit 0x20 before the
	 *  rank video, so 0x30 arrives as 0x10). The cold boot sets 1 (FUN_00263280(fe, 1)). */
	void Init(FSSXProfile* InProfile, FSSXCheats* InCheats, const FSSXLocStrings* InLoc, bool bColdBoot, int32 InExitFlag = 2);

	/** DAT_0032E5F0: 1 cold boot (-> 2 when cFE3D::vfunc_05 0x2637A0 queues the boot videos),
	 *  2 quit, 4 next venue, 0x10 setup character (0x30 rank video, bit 0x20 cleared before the FE); CharacterAccept
	 *  vfunc_15 0x27A000 turns 0x10 into 2. */
	int32 ExitFlag = 2;
	/** FUN_00265AF8: (DAT_0032E5F0 & 3) == 0 -- the FE was entered from a race that wants the rider set-up again: Title,
	 *  ModeSelect, NumPlayers, CharacterSelect and UserName accept at once (their vfunc_15 calls vfunc_08) and cFE3DSound
	 *  vfunc_02 (FE speech requests) is silent. */
	bool IsAutoAdvance() const { return (ExitFlag & 3) == 0; }

	void Key(int32 Pad, ESSXFeKey K);
	void SetHeld(int32 Pad, uint32 HeldBits) { Held[FMath::Clamp(Pad, 0, 1)] = HeldBits; }
	void Tick();
	void Draw(FSSXFeDrawList& D) const;

	ESSXFeBoot Boot = ESSXFeBoot::StartScreen;
	int32 BootFrame = 0;
	int32 BootState = 0;             // 0x1845E0 / 0x1850E8
	int32 BootTimer = 0;
	FString MovieRequest;
	bool bMovieSkippable = false;
	void MovieFinished();

	const FSSXFeState* Top() const { return Stack.Num() ? &Stack.Last() : nullptr; }
	FSSXFeState* TopMut() { return Stack.Num() ? &Stack.Last() : nullptr; }
	ESSXFeScreen Current() const { return Stack.Num() ? Stack.Last().Screen : ESSXFeScreen::Title; }
	int32 Depth() const { return Stack.Num(); }
	/** The FE model's board / outfit for pad Pad's saved rider. FUN_00267B18 (FE init, CharacterSelect / CharacterModify
	 *  entry) gates the saved gear through 0x163D30 / 0x163D98 (closed -> 0); the CustomBoard / CustomBody highlight
	 *  (FUN_0028D668 -> 0x267C20, vfunc_23 0x28DD50 -> 0x267C48) puts the highlighted item on the model ungated, so a
	 *  locked board / outfit is previewed on the rider while that screen is up. */
	void RiderGear(int32 Pad, int32& OutBoard, int32& OutOutfit) const;
	/** The board / outfit on ring rider Slot's model (cFE3DCharacter +0x158 / +0x159). Written only by FUN_00267B18 for all 12
	 *  (LatchRiderGear) and by the CustomBoard / CustomBody highlight (0x267C20 / 0x267C48, SyncCustomGear); the
	 *  CharacterSelect highlight and accept do not touch it. */
	void ModelGear(int32 Slot, int32& OutBoard, int32& OutOutfit) const;
	/** FUN_00267B18(rider, Pad) for every ring rider: the rider of pad Pad's slot (Char, its config Board / Outfit) gets the
	 *  saved gear through the gates 0x163D30 / 0x163D98 (closed -> 0), every other rider board 0 / outfit 0. Char < 0 uses
	 *  the slot's own rider and config. */
	void LatchRiderGear(int32 Pad, int32 Char = -1, int32 Board = 0, int32 Outfit = 0);
	/** cFE3D::vfunc_02 0x263B30 rider build: with no player yet, player 0 is created with carousel rider 4 (Eddie) and
	 *  its saved config (FUN_001635B8(P, 0, 0, rider)); then FUN_00267B18(rider, 0) for all 12. Runs at the FE load:
	 *  Init after a race, the end of the start screen on a cold boot (cStartScreenSingle precedes cFE3D(1)). */
	void BuildRiderGear();
	/** After a save is loaded from the memory card screen: the models take the loaded gear (BuildRiderGear). */
	void RefreshRiderGear();
	void GoTo(ESSXFeScreen S, int32 Player = 0);

	int32 NumPlayers = 1;
	int32 Group = 0;
	int32 GameMode = 2;
	int32 Track = 0;
	int32 Difficulty = 0;
	static constexpr int32 DefaultCarouselSlot = 4;
	int32 CarouselSlot[2] = { DefaultCarouselSlot, DefaultCarouselSlot };
	int32 AcceptedSlot[2] = { 0, 0 };
	int32 PadsConnected = 1;
	int32 IdleFrames = 0;
	static constexpr int32 AttractFrames = 3601;

	FSSXFeCameraDirector Camera;
	TArray<FString> SceneEvents;     // FUN_0013BAD8
	/** cFE3DSound calls in order: "ui<id>" = entry 10 play(u8 id), "music<e>" = FE music event e (entries 19..24), "riderName" = entry 3 rider-name speech (TODO: Events.evt not decoded); "saveVideo" = write the PC Video settings (no original counterpart). */
	TArray<FString> SoundHooks;
	/** cFE3DSound entry 31 (the jukebox stream ended), written by the audio side after it ran the hooks (SSXGameAudio::
	 *  TickFrontEnd); the DVD page reads it in its idle update. */
	bool bJukeboxSongEnded = false;
	/** The 12 character-select riders: states requested by the screens; the scene
	 *  (ASSXFrontEndPlayerController) runs their animation. */
	SSXFeRiders::FRing Riders;
	int32 RiderModelBoard[SSXFeRiders::NumRiders] = {};
	int32 RiderModelOutfit[SSXFeRiders::NumRiders] = {};
	// 0x267C20 / 0x267C48
	void SyncCustomGear(const FSSXFeState& S);
	/** snd+0x7DD0: animation-event sounds (the riders' TRANS speech) enabled; cFE3DSound entry 11, set by
	 * CharacterSelect vfunc_15. */
	bool bAnimEventSounds = false;
	bool bLaunchRequested = false;
	/** A replay loaded from the card (Options item 2, Race/SSXReplayFile.h): the vfunc_55 0x2917B0 -> 0x167060 start
	 *  launches its race; the host hands the file to USSXFrontEndSubsystem::PendingReplay with the launch. */
	TArray<uint8> PendingReplay;
	bool bReplayLaunch = false;
	SSXReplayFile::FSetup ReplayLaunchSetup;
	bool bLaunchPending = false;
	/** An Accept is leaving the top screen for PendingOpen / PendingPlayer: the screen closes (vfunc_06: out fade, 31-frame
	 *  slide-out, phase 3), its vfunc_21 runs (camera requests such as the Title's modeSelectIntro fly-in or the
	 *  CharacterModify venueSelect_intro), the state waits for the camera (phase 4), then vfunc_08 builds the next screen
	 *. false: none. */
	bool bHasPendingOpen = false;
	/** The venue room's ten video screens (mdl_videoscreen, fe+0xFC+4i, a TexFlip node each from FEStartScript with rate 0):
	 *  VenueSel::vfunc_15 -> FUN_0029BAA0 sends screen i (venue VenueOrder()[i]) channel 2 = 1.0 when the venue is open,
	 *  0.0 when locked (FUN_00163BB8), i.e. TexFlip message 2 = flipbook frame 1 (the venue's colour logo) or 0 (the
	 *  dark grey-green embossed logo). -1 = not sent yet (frame 0 stays). VenueScreenSerial counts the sends; the scene
	 * (ASSXFrontEndPlayerController) forwards each new set to the TexFlip nodes. */
	int32 VenueScreenFrame[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	int32 VenueScreenSerial = 0;
	/** FUN_00263000(fe, i, enabled) (VenueSel vfunc_15 with the selected item, vfunc_23 with the new one; vfunc_16 with -1):
	 *  every call first stops the streams of all ten screens; then, for i < 10 and an enabled (open) venue item, screen
	 *  fe+0xFC+4i (cFE3DMovieGeometry) opens data/video/<.data 0x3471D0[i]>.sss as a 128 x 128 looping 15 fps stream
	 *  (FUN_00269C70 -> 0x1FFF30) and is drawn with the stream texture in place of its own (vfunc_11 0x269D10) until it
	 *  is stopped. The push / pop of another state runs VenueSel's vfunc_16 (FUN_00275B10 / FUN_00275B90). -1 = none;
	 * VenueVideoSerial counts the calls (each one reopens the file at frame 0). */
	int32 VenueVideo = -1;
	int32 VenueVideoSerial = 0;
	/** The carousel window of FUN_00261B38(fe, pad, _, p): riders p-2..p+2 drawn (rider vslot 0x2C
	 *  -> +0x14), the selected light flag (+0x154) on p only, TexFlip message 2 on the rider signs mdl_cs<Name>_0 (the
	 *  pad's previous slot fe+0x7D+2pad -> 0, p -> 1) and on the platforms mdl_Cplatform_* (all 0, p -> 1); flipbook
	 *  frame 1 is the lit texture (the light pool under the selected rider). Called by the request-6 hooks (with p = 4 after
	 *  FUN_00263270 reset both pads' slot to 4) BEFORE the swing into the char room, by CharacterSelect vfunc_15 / vfunc_23
	 *  and CharacterModify vfunc_15. INDEX_NONE before the first call: every rider is drawn (the ctor sets +0x14 = 1),
	 *  no flag, every flipbook at frame 0 (FEStartScript). CarouselSerial counts the calls. */
	int32 CarouselWindow = INDEX_NONE;
	int32 CarouselSignFrame[12] = {};
	int32 CarouselPlatformFrame[12] = {};
	int32 CarouselSerial = 0;
	// FUN_00261B38
	void SetCarouselWindow(int32 Pad, int32 Slot);
	// FUN_00278840
	void CharacterRefresh(const FSSXFeState& S);
	bool IsCarouselRiderDrawn(int32 Slot) const;
	ESSXFeScreen PendingOpen = ESSXFeScreen::Title;
	int32 PendingPlayer = 0;
	FSSXFeFader Fader;
	bool IsReady() const;
	int32 HintIndex = 0;
	FSSXFeLaunch Launch;
	void LaunchFailed();
	FSSXMemCard MemCard;
	FSSXProfile* Profile = nullptr;
	/** The TrickBook video texture (sprite 0x7B) once its stream decoded a frame; the host steps the stream
	 *  (SSXFeTrickBook::FVideo) from the top state's BookVideo. Null = nothing to show yet. */
	UObject* BookVideoTexture = nullptr;
	FSSXCheats* Cheats = nullptr;

	FString CurrentHelpLine() const { return Stack.Num() ? HelpLine(Stack.Last()) : FString(); }
	int32 ViewSlot() const;

private:
	FSSXFeState Make(ESSXFeScreen S, int32 Player = 0) const;
	void Push(FSSXFeState S);
	void Pop();
	void OnOpen(FSSXFeState& S);
	void Accept(FSSXFeState& S);
	void Back(FSSXFeState& S);
	void Move(FSSXFeState& S, int32 Dir, bool bVerticalKey);
	void OnHighlight(FSSXFeState& S, int32 OldSel);
	void ScreenKey(FSSXFeState& S, ESSXFeKey K);
	void OptionsKey(FSSXFeState& S, ESSXFeKey K);
	/** FUN_0029FB40 (vfunc_15): the page widgets from the settings (FUN_0029EF50 sound, FUN_0029F430 controller,
	 *  FUN_0029F5F0 game). */
	void OptionsSync(FSSXFeState& S);
	// 0x3B4290
	void OptionsEnterPage(FSSXFeState& S, int32 P);
	// FUN_0029F178 / FUN_0029F508 / FUN_0029F720
	void OptionsCommit(FSSXFeState& S, int32 P);
	void OptionsLeavePage(FSSXFeState& S);
	// FUN_002A9720 / FUN_002A9848 / FUN_002A99A0
	void OptionsApplyList(FSSXFeState& S, int32 Row);
	void OptionsApplyBar(FSSXFeState& S, int32 Row);
	// FUN_002A9350
	void OptionsDiagram(FSSXFeState& S);
	int32 OptionsSelectedId(const FSSXFeState& S) const;
	void UserNameKey(FSSXFeState& S, ESSXFeKey K);
	/** TrickBook input: the chapter list (title bar) and the trick list share the menu bytes (+0x112 / +0x114 / +0x116 = 4);
	 *  vfunc_27 0x27E3C0 (X), vfunc_22 0x27E338 (Back), vfunc_25 0x27E7F8 (Circle = Trick Tutorial). */
	void TrickBookKey(FSSXFeState& S, ESSXFeKey K);
	/** DVDContent input: the focused menu's sound bytes (main +0x112 / +0x114 / +0x115 = 0xFF; the sub lists keep accept 5,
	 *  move 4, back 6, refused 7; the talent page only back), then vfunc_27 0x299E10 / vfunc_22 0x299FF0. */
	void DvdKey(FSSXFeState& S, ESSXFeKey K);
	/** FUN_0027A288: load chapter C's page (no-op when it is the loaded one); vfunc_18 then binds sprites 0x7C..0x80 to the
	 *  five icons (enabled = trick done), resets the trick list to item 0 and gives the picture item 0's sprite / colour. */
	void TrickBookLoad(FSSXFeState& S, int32 C);
	// 0x27E5D0
	void TrickBookHighlight(FSSXFeState& S);
	bool TrickBookDone(const FSSXFeState& S, int32 Trick) const;
	void AfterModifyContinue(int32 Player);
	void BeginClose(FSSXFeState& S);
	void Leave(FSSXFeState& S, ESSXFeScreen Next, int32 NextPlayer = 0);
	void OnClosed(FSSXFeState& S);
	void OnOpened(FSSXFeState& S);
	void BackHook(FSSXFeState& S);
	void BeginBack(FSSXFeState& S, bool bHook);
	// FUN_00263270 / FUN_00261B38
	void CarouselSwingReset();
	// FUN_0026BE38
	void RequestCharSelectCamera();
	// FUN_0026BE38
	void RequestTitleCamera();
	void RequestLaunch();
	void BeginLaunch();
	/** Menu action hooks "accept" / "refused" / "back" / "move" -> the screen's play(u8 id) (cFE3DMenu
	 * +0x110..+0x113 and the per-screen overrides); other hooks are kept as given. */
	void Sound(const TCHAR* Hook);
	void PlayId(int32 Id) { SoundHooks.Add(FString::Printf(TEXT("ui%d"), Id)); }
	/** cFE3DSound entry 2 = vfunc_02 0x270378 (FE speech request Id): returns at once while the FE exists and
	 *  FUN_00265AF8 is set (no speech stop, no request). */
	void Speech(int32 Id) { if (!IsAutoAdvance()) { SoundHooks.Add(FString::Printf(TEXT("speech%d"), Id)); } }
	// FUN_00265AF8
	void StopSpeechRequest() { if (!IsAutoAdvance()) { SoundHooks.Add(TEXT("stopSpeech")); } }
	/** ModeSelect vfunc_23 0x29A6B0 speech by group fe+0xF0: Practice 6 (FUN_002627F0), Single Event 4 (FUN_00262840),
	 *  World Circuit 5 (FUN_00262890). */
	static int32 ModeSelectSpeechId(int32 InGroup) { return InGroup == 1 ? 6 : (InGroup == 2 ? 5 : 4); }
	/** FUN_00263000 (VenueSel vfunc_15 / vfunc_23) sound part: cFE3DSound entry 5 (vfunc_05 0x2704F0) with the selected
	 *  venue's track: tracks 0..8 -> speech request 0x30, 0x31, 0x32, 0x33, 0x34, 0x36, 0x39, 0x38, 0x35; track 11 -> 0x37;
	 *  others none. */
	void VenueSpeech(const FSSXFeState& S);
	/** The sound part of the screens' vfunc_15 (the push and every re-show by Back): FE music events and speech requests
	 *. */
	void ShowSounds(FSSXFeState& S);
	/** The auto-advance tail of the five screens' vfunc_15: vfunc_08 called at once (phase != 1): vfunc_11 builds Next into
	 *  +0x14 and the state goes to phase 5; the next update (cFE3DState::vfunc_18) pushes it. No vfunc_27 (out fade, accept
	 *  sound, data), no slide-out, no vfunc_21 (camera requests, request-6 hooks). */
	void AutoAccept(FSSXFeState& S, ESSXFeScreen Next, int32 NextPlayer = 0);
	/** ModeSelect::vfunc_11 0x29A258 for item Id (0 EventSel, 1 FUN_00265A58: pads < 2 -> players 1 + CharacterSelect,
	 *  else NumPlayers; 2 players 1 + CharacterSelect). */
	ESSXFeScreen ModeSelectNext(int32 Id);
	// 0x293938 / 0x29AB48 / 0x278598 / 0x29D2D0
	void TitleAuto(FSSXFeState& S);
	void NumPlayersAuto(FSSXFeState& S);
	void CharacterSelectAuto(FSSXFeState& S);
	void UserNameAuto(FSSXFeState& S);
	// 0x29A4E0
	void ModeSelectShow(FSSXFeState& S);
	void MusicEvent(int32 Event) { SoundHooks.Add(FString::Printf(TEXT("music%d"), Event)); }
	void CheatInput(int32 Pad, ESSXFeKey K);
	FString L(int32 Id) const;

	// FUN_002623D8 / FUN_0026BE38
	void RequestModeSelectCamera();
	void EventSelCamera(const FSSXFeState& S);
	// 0x29BD88 / FUN_0029BAA0
	void VenueScreens(const FSSXFeState& S);
	// FUN_00263000
	void SetVenueVideo(int32 I, bool bEnabled);
	void VenueVideoForSelection(const FSSXFeState& S);
	// 0x279A80
	void CharacterModifyCamera(const FSSXFeState& S);
	void OnReshow(FSSXFeState& S);
	// 0x293938
	void TitleShow();

	void DrawStandard(FSSXFeDrawList& D, const FSSXFeState& S) const;
	void DrawTitleScreen(FSSXFeDrawList& D, const FSSXFeState& S) const;
	// FUN_002720F8
	void DrawFrame(FSSXFeDrawList& D, const FSSXFeState& S, int32 TabLoc, uint32 HelpMask) const;
	void DrawTitleBar(FSSXFeDrawList& D, float H, float Y) const;
	void DrawHelpBar(FSSXFeDrawList& D, uint32 Mask, float X, float Y, float LabelScale, int32 L, uint32 DisabledMask = 0) const;
	void DrawCharPanel(FSSXFeDrawList& D, const FSSXFeState& S, int32 Char, float GroupY) const;
	void DrawStatRows(FSSXFeDrawList& D, int32 Char, int32 Board, float GroupY) const;
	// FUN_002B0F68
	FString HelpLine(const FSSXFeState& S) const;
	void DrawOptions(FSSXFeDrawList& D, const FSSXFeState& S) const;
	/** Legend entries drawn greyed (the cFE3DTextIcon disabled colour): Options vfunc_23 / vfunc_27 / vfunc_22 switch the
	 *  "select" entry (mask 2) off on the value pages and the credits. */
	uint32 LegendDisabledMask(const FSSXFeState& S) const;
	void DrawUserName(FSSXFeDrawList& D, const FSSXFeState& S) const;
	void DrawRiderProfile(FSSXFeDrawList& D, const FSSXFeState& S) const;
	// FUN_001A6BA0
	FString FeTime(int32 Cs) const;
	void DrawTrickBook(FSSXFeDrawList& D, const FSSXFeState& S) const;
	void DrawBoot(FSSXFeDrawList& D) const;
public:
	/** cStartScreenSingle message box FUN_00183538 (box (112, 96, 416, 4 lines), MENU 1.0 (menu.sfn x1.8 / y1.4), white,
	 *  centred lines, no shadow, line pitch font +0xC + 3 + 4, '\' breaks a line) and, for Buttons != 0, the button row
	 *  FUN_00183C20 (bCentred: one row at y 340 centred on 320; else stacked from (160, 340), +30). */
	void DrawStartBox(FSSXFeDrawList& D, const FString& Msg, uint32 Buttons, bool bCentred) const;
private:
	void DrawScreens(FSSXFeDrawList& D) const;

	TArray<FSSXFeState> Stack;
	const FSSXLocStrings* Loc = nullptr;
	uint32 Held[2] = { 0, 0 };
	TArray<uint32> CheatBuffer;
	int32 Frame = 0;
	bool bStartScreenLoaded = false;
public:
	/** cFE3DCamera +0x180 mode (0 none yet, 1 intro, 2 mode select, 3 WC, 4 PR, 5 SE, 6 char select, 8 select<Name> frame 0,
	 *  10 select<Name> end frame (CharacterModify), 0xB venue; -1 other). */
	int32 CamMode = 0;
	/** cFE3DCamera +0x185: the venue list index the venue cycle scripts left the camera at (0x3A8AF0 order); the 0xB
	 *  request sets 0, VenueSel's highlight (vfunc_23) the new index; VenueSel's default highlight and the choose script
	 *  (0x90 + index, FUN_0026C2C0) read it. */
	int32 VenueCamSlot = 0;
};

namespace SSXFeUserName
{
	/** 14 x 3 grid (cFE3DWNameEntry string "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890 !()de", data 0x3B7F58): letters, digits,
	 *  " !()", 'd' delete, 'e' END [2.2, 9]. */
	SSXTRICKYREBUILD_API TCHAR Cell(int32 X, int32 Y);
	constexpr int32 Cols = 14, Rows = 3, MaxChars = 9, NumCells = 42;
	constexpr float CellW = 30.f, CellH = 25.f;   // 0x348464 / 0x348468
	// 0x2B0850
	SSXTRICKYREBUILD_API int32 StepLeft(int32 Idx);
	SSXTRICKYREBUILD_API int32 StepRight(int32 Idx);
	SSXTRICKYREBUILD_API int32 StepUp(int32 Idx);
	SSXTRICKYREBUILD_API int32 StepDown(int32 Idx);
	/** vfunc_38: w = 14 x 30 - 30 / 2 = 405, h = ((14 + 41) / 14) x 25 = 75; vfunc_12 0x29C370 places it at
	 *  ((640 - w) / 2, 300 - h - 73) in the title bar. */
	constexpr float GridW = Cols * CellW - CellW * 0.5f, GridH = ((Cols + 41) / Cols) * CellH;
	constexpr float GridX0 = (640.f - GridW) * 0.5f, GridY0 = 300.f - GridH - 73.f;
	constexpr int32 SprCursor = 42, SprDelete = 59, SprCap = 44;
	constexpr float CursorScale = 1.1f;
	constexpr float EndScale = 0.8f;
	constexpr float BoxX = 320.f, BoxY = 94.f, BoxW = 245.f, BoxH = 30.f;
	constexpr float CapLX = 197.5f, CapRX = 442.5f;
	constexpr float NameY = 100.f, NameScale = 0.7f;
}

// FUN_00263000 / FUN_00269C70 / 0x3471D0
namespace SSXFeVenueVideo
{
	constexpr int32 Size = 128;   // FUN_001FFF30
	/** The file of ring index I (0..9; empty otherwise): GARI, SNOW, ELYSIUM, MESA, MERQUER, MEGAPLE, ALOHA, ALASKA,
	 *  UNTRACK, PIPE (.SSS, read from the git-ignored extracted/DATA/VIDEO). */
	SSXTRICKYREBUILD_API FString FileName(int32 I);
	SSXTRICKYREBUILD_API FString Path(int32 I);
	/** The stream of the screen that plays (SSXPropMovie::FStream rules: one step per 60 Hz frame, a chunk every 4th,
	 *  looping). Step follows FSSXFrontEnd::VenueVideo / VenueVideoSerial; true when a new frame went into Pixels. */
	class SSXTRICKYREBUILD_API FVideo
	{
	public:
		bool Step(int32 I, int32 Serial);
		int32 Cur = -1;
		int32 CurSerial = -1;
		TArray<FColor> Pixels;
		int32 W = 0, H = 0;
		bool bFile = false;
		SSXPropMovie::FSssFile File;
		SSXPropMovie::FStream Stream;
	};
}
