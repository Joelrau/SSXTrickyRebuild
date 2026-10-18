// 0x265398 / 0x17C6E8 / 0x17C9D8 / 0x243F00
#pragma once

#include "CoreMinimal.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFrontEnd.h"

class FSSXLocStrings;
struct FSSXProfile;
struct FSSXUnlock;

/**
 * The FE event queue [code: reset 0x1F1258, add 0x1F11C0, tick 0x1F12D0, step 0x1F0E80, value 0x1F0F48, count 0x1F1448,
 * value by id 0x1F1488]: up to 20 channels {id, step, speed, mode, from, to}; mode 0 counts up (typed text), 1 down,
 * 2 a sine between to / 1000 and from / 1000 (period = speed frames), 3 a linear ramp from / 1000 -> to / 1000 over
 * speed frames. Modes 0 / 1 run while their step is current, 2 / 3 from their step on.
 */
struct SSXTRICKYREBUILD_API FSSXLoadSequencer
{
	struct FChannel
	{
		int32 Id = -1;
		int32 Step = 0;
		int32 Frames = 0;
		int32 To = 0;
		float Speed = 0.f;
		int32 Value = 0;
		int32 From = 0;
		int32 Mode = 0;
	};
	static constexpr int32 MaxChannels = 20;
	int32 Cur = 0;
	int32 Active = 0;
	int32 Num = 0;
	bool bDone = true;
	int32 MaxStep = 0;
	FChannel Ch[MaxChannels];

	void Reset();
	void Add(int32 Id, int32 Step, int32 Speed, int32 Mode, int32 From, int32 To);
	void Tick();
	// 0x1F1448
	int32 Count(int32 Id) const;
	// 0x1F1488
	float Value(int32 Id) const;
	static bool StepChannel(FChannel& C);
	static float Eval(const FChannel& C);
};

namespace SSXTrackLoad
{
	/** Loader classes (all on cFlakeSpinnerLoad): the FE -> race loaders of cFE3D::vfunc_06 0x265398 (OneP c1PTrackLoad,
	 *  TwoP c2PTrackLoad, Tutorial cTrickTutorialLoad) and the race -> FE loaders of cGame::vfunc_06 0x180E00 / cVideoModule::
	 *  vfunc_06 0x186620 (FromQuit cFELoadFromQuit, FEOneP c1PFELoad, FETwoP c2PFELoad, RiderRanking cRiderRankingLoad). */
	enum class EKind : uint8 { OneP, TwoP, Tutorial, FromQuit, FEOneP, FETwoP, RiderRanking };
	SSXTRICKYREBUILD_API EKind KindFor(const FSSXFeLaunch& L);

	/**
	 * What follows a race once world+0x54 = 1 [code: cGame::vfunc_06 0x180E00, cVideoModule::vfunc_06 0x186620]:
	 *   P+4 (rank video pending, set with exit code 0x30) -> cVideoLoadModule (the rr_ movie; afterwards cVideoModule picks
	 *   cRiderRankingLoad, DAT_0032EA9C being set);
	 *   else DAT_0032EAA0 == 1 -> cFELoadFromQuit(1) (no writer of 1 in the executable: always 0 in play);
	 *   else DAT_0032EA94 (pause quit) or mode 6 -> cFELoadFromQuit(0);
	 *   else DAT_0032EA9C (rank-up) -> cRiderRankingLoad;
	 *   else DAT_0032EA98 (next venue) -> c1PTrackLoad / c2PTrackLoad (by the human count);
	 *   else mode 1 (Freeride) -> cFELoadFromQuit(0);
	 *   else c1PFELoad / c2PFELoad.
	 */
	struct FReturnPick
	{
		bool bRankVideo = false;
		EKind Kind = EKind::FromQuit;
		bool bHintAnyRank = false;
	};
	SSXTRICKYREBUILD_API FReturnPick PickAfterRace(bool bRankVideo, bool bEAA0, bool bQuit, int32 Mode, bool bRankUp, bool bNextVenue, int32 Humans);

	/** cFELoadFromQuit hint page FUN_0024D1A0: n = rand() % 19 (17 re-drawn); with the ctor argument 0 and player 1's rank
	 *  LOC 0x1F / 0x20 (FUN_00164730: rank points < 20, ranks 8 / 9) only n in {3, 4, 6, 15} is kept. */
	SSXTRICKYREBUILD_API int32 PickHint(bool bAnyRank, int32 RankPoints, TFunctionRef<uint32()> Rand);
	constexpr int32 QuitTitleLoc = 0xE30;       // FUN_0024D1A0
	constexpr int32 HintTitleLoc = 0xCFF;
	constexpr int32 NumHints = 19;
	/** Text box line break FUN_00196218: words split at spaces, '\' ends a line, a line takes words while its ink width
	 *  (FUN_0019DC78, int) stays <= int(Width). */
	SSXTRICKYREBUILD_API TArray<FString> WrapBox(const FSSXFeDrawList& D, SSXFe::EFont Font, const FString& Str, float Scale, float Width);
	// FUN_002459B0 / FUN_00245A48 / FUN_00245B68
	SSXTRICKYREBUILD_API FString ModePicture(int32 Mode);
	SSXTRICKYREBUILD_API FString TrackPicture(int32 Track);
	SSXTRICKYREBUILD_API FString RiderPicture(int32 Char);
	// FUN_00246700
	SSXTRICKYREBUILD_API int32 ModeTitleLoc(int32 Mode);
	SSXTRICKYREBUILD_API int32 SubTitleLoc(int32 Mode);
	// FUN_0016B330
	SSXTRICKYREBUILD_API int32 StatLoc(int32 K, int32 Track);
	// FUN_0016B6D8 / FUN_0016B7B0
	SSXTRICKYREBUILD_API FVector2f MapMarker(int32 Track);
	// FUN_00163E28
	SSXTRICKYREBUILD_API int32 MedalLoc(int32 Medal);

	SSXTRICKYREBUILD_API FString DataDir();
	SSXTRICKYREBUILD_API FString JsonPath();

	constexpr float FrameStep = 0.016666668f;
	constexpr float CloseStep = 0.033333335f;
	constexpr int32 MusicStopProgress = 7;
	constexpr int32 RankMusicStopProgress = 9;  // 0x24F8C0
	constexpr float MusicStopFade = 8.f;        // FUN_00214D10 / FUN_0020C2A0
	constexpr float QuitMusicStopFade = 3.f;    // FUN_00214D10 / FUN_0020C2A0
	constexpr int32 BlinkReset = 70;            // FUN_0019E460

	/** c1PFELoad (FUN_0024A188) / c2PFELoad (FUN_0024BBD0) summary title +0x6A0: 1 Freeride 0xE7D, 2 / 3 / 7 Single Event
	 *  0xE7E, 4 / 5 World Circuit 0xE7F (c1PFELoad only; c2PFELoad leaves it empty); -1 = none. */
	SSXTRICKYREBUILD_API int32 FeSummaryLoc(int32 Mode, bool bTwoP);
	/** FUN_00243B88(int(seconds x 100)): 50000 -> LOC 0x15D "DNF", < 0 -> LOC 0x18C "N/A", < 360000 -> "%d:%02d.%02d"
	 *  (minutes, seconds, centiseconds), else "59:59.99". */
	SSXTRICKYREBUILD_API FString StatTime(float Seconds, const FSSXLocStrings* Loc);
	// FUN_0024F520
	SSXTRICKYREBUILD_API int32 RankBoardTypeLoc(int32 Type);
	// FUN_0024F520
	SSXTRICKYREBUILD_API int32 RankUpBoard(const TArray<FSSXUnlock>& Unlocks);
	/** The rank video of player 1's rider: "data/video/rr_%s.mpc" with the short name (&PTR_00331EB0)[char * 0xE] (cPointDistributionOverlay::vfunc_15 0x1BA330) -> the converted movie name RR_<SHORT>. */
	SSXTRICKYREBUILD_API FString RankVideoName(int32 Char);
	// 0x2BA560
	constexpr const TCHAR* MissingVideoFallback = TEXT("EABIG");
}

/** The rank video before the race -> FE loader (cGame::vfunc_06 0x180E00, cApplication::vfunc_04 0x17BBD8, cVideoModule 0x1864A0..0x186AA8, cPS2VideoPlayer 0x2BA560 / 0x2BA9F8): the rank-up card (PointDistribution 0x1BA330) sets exit code 0x30, P+4 (DAT_0032B394) = 1 and the movie name (0x163F10 -> 0x32EAA4); cGame::vfunc_06 sees P+4, clears it and loads cVideoLoadModule (loads nothing); the app (state 2, exit bit 0x20, cleared) builds cVideoModule(name, 1): vfunc_05 plays the whole movie (skippable: X / Start on pad 1 or 2, no sound), then vfunc_06 builds the loader (DAT_0032EA94 / mode 6 / mode 1 -> cFELoadFromQuit(0), DAT_0032EA9C -> cRiderRankingLoad, else c1PFELoad / c2PFELoad) which loads the FE. */
struct SSXTRICKYREBUILD_API FSSXRankVideo
{
	enum class EPhase : uint8 { Off, Playing, Done };
	EPhase Phase = EPhase::Off;
	FString Name;
	bool bSkippable = false;
	bool bSkipped = false;
	bool Begin(const SSXTrackLoad::FReturnPick& Pick, int32 Char);
	bool PressSkip();
	bool Finish();
	bool IsPlaying() const { return Phase == EPhase::Playing; }
};

// FUN_00246700
struct SSXTRICKYREBUILD_API FSSXTrackLoadPage
{
	int32 Mode = 0, Track = 0, Char = 0;
	bool bTexts = false;
	FString ModeTitle;
	FString SubTitle;
	FString VenueName;
	FString Labels[8];
	FString Values[7];
	FString Medal;
	FString WcTitle[2];
	FString Pictures[3];
	FString Loading;
	// FUN_0024D1A0
	int32 Hint = -1;
	FString QuitTitle, HintTitle, HintBody;
	// FUN_0024A188 / FUN_0024BBD0
	static constexpr int32 FeRows = 10;
	FString FeTitle, FeSubTitle, FeVenue;
	FString FeLabels[FeRows];
	FString FeValues[2][FeRows];
	FString FePictures[2];
	// FUN_0024F520 / FUN_00164808
	FString RankTitle, RankBody, RankLabels[3], RankValues[3];
	FString RankPicture;
	// FUN_0024D7F8 / FUN_0024E228 / 0x24E3A8
	FString TutorialTitle;
	bool bTutorialPro = false;
	// FUN_0019D0C0 / FUN_0019D370 / FUN_0019D278
	struct FTutorialText { FString Str; float X = 0.f, Y = 0.f, Scale = 1.f; int32 Align = 0; int32 Loc = -1; };
	TArray<FTutorialText> TutorialTexts;
	static FSSXTrackLoadPage BuildTutorial(const FSSXProfile* Profile, const FSSXLocStrings* Loc);

	static FSSXTrackLoadPage Build(const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc);
	static FSSXTrackLoadPage BuildQuit(int32 N, const FSSXLocStrings* Loc);
	/** The c1PFELoad (bTwoP false) / c2PFELoad page strings from the event's launch, the profile and the end-of-race stat
	 *  copies (Profile->Session.RaceStats). */
	static FSSXTrackLoadPage BuildFe(bool bTwoP, const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc);
	static FSSXTrackLoadPage BuildRanking(const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc);
	bool IsWorldCircuit() const { return uint32(Mode - 4) < 2u; }
};

/**
 * The loader's draw resources (cFlakeSpinnerLoad::vfunc_03 0x243F00): its own fonts data/fonts/title.sfn and menu.sfn at
 * x 1.25 / y 1.0 (Small = title.sfn at the sub-title's x 1.27 / y 1.34, FUN_002447A8), the 28 records of
 * c1PTrackLoad::vfunc_11 (ids 0..27 = loader +0xB14 + 0x1C k), the loading.ssh pages and the PS2LOAD pictures (keyed by
 * name). Keep it in place after Load (Assets points at Sprites).
 */
struct SSXTRICKYREBUILD_API FSSXTrackLoadAssets
{
	FSSXFeAssets Assets;
	FSSXSpriteTable Sprites;
	bool bLoaded = false;
	bool Load(const FSSXFeAssets& Fe, TFunctionRef<UTexture2D*(const FString&)> LoadPng);
};

// PC differences (user request): the screen stays until the track is really loaded; the page intro plays to its end for
// every track; X / Start skips the rest of the intro, accepted only once everything is loaded (RequestSkip).
class SSXTRICKYREBUILD_API FSSXTrackLoader
{
public:
	/** Off; Prepare = the loader's own files (vfunc_03 states 0..4, the FE still up); Run = app state 2 (update / draw
	 *  per vsync); Finish = app state 3 (vfunc_04 each vsync: back to black); Done = deleted, the race may start. */
	enum class EPhase : uint8 { Off, Prepare, Run, Finish, Done };

	// 0x245840 / 0x243C78 / FUN_00246700
	void Begin(const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc);
	/** A race -> FE loader (cGame::vfunc_06 / cVideoModule::vfunc_06). FromQuit: ctor 0x24C748 -> FUN_0024D1A0 (hint pick
	 *  from Rand); FEOneP: 0x249758 -> FUN_0024A188; FETwoP: 0x24B028 -> FUN_0024BBD0; RiderRanking: 0x24EA20 ->
	 *  FUN_0024F520 (L = the event that just ended: mode, track, the players' riders). */
	void BeginReturn(SSXTrackLoad::EKind InKind, bool bHintAnyRank, const FSSXFeLaunch& L, const FSSXProfile* Profile,
		const FSSXLocStrings* Loc, TFunctionRef<uint32()> Rand);
	// FUN_00214D10
	float MusicStopFade() const
	{
		// 0x24E268 / 0x248AD0 / FUN_0020C2A0
		return UsesTrackVoice() ? SSXTrackLoad::MusicStopFade : SSXTrackLoad::QuitMusicStopFade;
	}
	/** The race loaders (c1PTrackLoad, c2PTrackLoad 0x248A60, cTrickTutorialLoad 0x24E1F0) call FUN_00214910(snd, 1), the
	 *  race -> FE loaders FUN_00214910(snd, 0). */
	bool UsesTrackVoice() const
	{
		using SSXTrackLoad::EKind;
		return Kind == EKind::OneP || Kind == EKind::TwoP || Kind == EKind::Tutorial;
	}
	/** FUN_00214910's argument picks the sound of bank 13 (loadingscreen.bnk): request word +0xA44 = the sound index that
	 *  FUN_0020C948 hands to FUN_002D1718 (as cBankSys::vfunc_13 (bank, sound)): 1 -> #0, 0 -> #1. */
	int32 PageMusicSound() const { return UsesTrackVoice() ? 0 : 1; }
	/** The kind starts the bank-13 voice (vfunc_05 FUN_00214910) and stops it in vfunc_06: every loader (c1PTrackLoad,
	 *  c2PTrackLoad, cTrickTutorialLoad (stop at app+8 >= 8) and the four race -> FE loaders). */
	bool HasPageMusic() const
	{
		using SSXTrackLoad::EKind;
		return Kind == EKind::OneP || Kind == EKind::TwoP || Kind == EKind::Tutorial || Kind == EKind::FromQuit || Kind == EKind::FEOneP
			|| Kind == EKind::FETwoP || Kind == EKind::RiderRanking;
	}
	// 0x248AD0
	bool TicksPageQueue() const { return Kind != SSXTrackLoad::EKind::TwoP; }
	bool MusicStopDue(int32 InProgress) const
	{
		return Kind == SSXTrackLoad::EKind::RiderRanking ? InProgress == SSXTrackLoad::RankMusicStopProgress : InProgress > SSXTrackLoad::MusicStopProgress;
	}
	void Start();
	void Step(bool bLoaded, int32 Progress);
	bool RequestSkip(bool bLoaded);
	void Draw(FSSXFeDrawList& D) const;

	EPhase Phase = EPhase::Off;
	SSXTrackLoad::EKind Kind = SSXTrackLoad::EKind::OneP;
	FSSXTrackLoadPage Page;
	FSSXLoadSequencer Lines;
	FSSXLoadSequencer Seq;
	float Fade = 1.f;
	bool bClosing = false;
	float Timer = 0.f;
	int32 Progress = 0;
	bool bSkipped = false;
	bool bIntroDone() const { return Seq.bDone; }
	bool bMusicOn = false;
	int32 MusicStarts = 0;         // FUN_00214910
	int32 MusicStops = 0;          // FUN_00214D10
	int32 TypeSounds = 0;          // FUN_0019E460 / FUN_00214DD0
	// FUN_0019E460
	int32 BlinkCount = SSXTrackLoad::BlinkReset;
	bool bBlink = false;
	bool RowBlink[10] = {};

	static constexpr int32 RowIds[8] = { 9, 10, 11, 12, 14, 15, 16, 0x11 };
	static constexpr int32 RowSteps[8] = { 3, 4, 5, 6, 7, 8, 9, 1 };
	// 0x24AA70
	static constexpr int32 FeRowId(int32 Row) { return 5 + Row; }
	static constexpr int32 FeRowStep(int32 Row) { return 4 + Row; }
	static constexpr float FeRowY[10] = { 98.f, 124.f, 150.f, 190.f, 216.f, 242.f, 268.f, 308.f, 334.f, 360.f };

private:
	void Update();
	void DrawTimeEffects();
	void DrawPage(FSSXFeDrawList& D) const;
	// FUN_00244FC8 / FUN_002449F8
	void DrawBase(FSSXFeDrawList& D) const;
	// FUN_00244540
	void DrawProgress(FSSXFeDrawList& D) const;
	void DrawTexts(FSSXFeDrawList& D) const;
	// 0x24D3E8 / 0x24D4D0
	void DrawQuitPage(FSSXFeDrawList& D) const;
	void DrawQuitTexts(FSSXFeDrawList& D) const;
	// 0x24AA70 / 0x24A988
	void DrawFeTexts(FSSXFeDrawList& D) const;
	// 0x24C2D0 / 0x24C428
	void DrawFe2Page(FSSXFeDrawList& D) const;
	void DrawFe2Texts(FSSXFeDrawList& D) const;
	// 0x24F918 / 0x24FA48
	void DrawRankPage(FSSXFeDrawList& D) const;
	void DrawRankTexts(FSSXFeDrawList& D) const;
	// 0x24E2C0 / 0x24E3A8
	void DrawTutorialPage(FSSXFeDrawList& D) const;
	void DrawTutorialTexts(FSSXFeDrawList& D) const;
	// FUN_002447A8
	void DrawPicture(FSSXFeDrawList& D, const FString& Name, float X, float Y, float Alpha) const;
	FString RowText(int32 Row) const { return Row < 7 ? Page.Values[Row] : Page.Medal; }
};
