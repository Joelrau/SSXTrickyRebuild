// 0x3B42E8 / FUN_0029FB70 / FUN_002A36E8 / FUN_002A53D0
#pragma once

#include "CoreMinimal.h"
#include "UI/SSXFeTypes.h"

namespace SSXFeOptions
{
	constexpr int32 DoneId = int32(0xABCD1234);
	/** Pages 0..4 are the original's; VideoPage is the PC-only Video page (user request, not in the original). */
	constexpr int32 NumPages = 6;
	constexpr int32 VideoPage = 5;
	constexpr int32 MaxRows = 9;

	/** Row widget kinds: a cFE3DWTextEntry with a value list (cFE3DWList style 2, wrap 1) or a progress bar
	 *  (cFE3DProgressBar), the DONE entry (label only), or a plain single-line text item (Save/Load). */
	enum class EKind : uint8 { List, Bar, Done, Item };

	// 0x3B40E0
	struct FValue { int32 Id = 0; int32 Loc = -1; const TCHAR* Text = nullptr; };

	struct FRow
	{
		int32 Id = 0;
		int32 Loc = -1;
		EKind Kind = EKind::List;
		const TCHAR* Text = nullptr;   // label of a PC-only row (Loc -1)
		TArray<FValue> Values;
		float BarW = 0.f;
		float BarStep = 0.f;
	};

	SSXTRICKYREBUILD_API const TArray<FRow>& PageRows(int32 Page);
	/** Top-list items and page titles: 3608 Sound, 176 Configure Controller, 186 Game Options, 204 Save/Load, 180 Credits
	 *  (vfunc_12 0x2A8A30 writes them to state +0x7C + 12 i). */
	SSXTRICKYREBUILD_API int32 PageTitleLoc(int32 Page);
	/** Top-list item / page title text of the PC-only pages (no LOC entry); nullptr for the original's pages. */
	SSXTRICKYREBUILD_API const TCHAR* PageTitleText(int32 Page);
	/** Help line of the PC-only items (HelpLoc -1): the Video top-list item (Page -1) and the Video page rows. */
	SSXTRICKYREBUILD_API const TCHAR* HelpText(int32 Page, int32 ItemId);
	/** FUN_002AA998: DONE -> 3534; else table[item id] of page -1 (0x3481D0), 0 (0x3481E8), 1 (0x348238), 2 (0x3483F8),
	 *  3 (0x348450); page 4 sets "" (returns -1). Value items have their own entries in the same tables. */
	SSXTRICKYREBUILD_API int32 HelpLoc(int32 Page, int32 ItemId);

	constexpr float MenuLineH = 15.f;
	constexpr float TitleY = 60.f;
	constexpr float EntryGap = 15.f;
	constexpr float PageSpacing = 6.f;
	constexpr float CtrlSpacing = 2.f;
	constexpr float CtrlMenuY = 290.f;
	constexpr float CreditsY = 105.f;
	constexpr float TopGroupBoxH = 88.f;
	constexpr float BarEdge = 42.f;
	constexpr float BarH = 15.f;
	constexpr int32 SprSliderTrack = 25, SprSliderFill = 26;
	constexpr float OptArrowSize = 9.f;       // FUN_002A8520 / FUN_002ADF18
	constexpr float OptArrowGap = 5.f;        // FUN_0026D8C8 / FUN_002AE3C0
	/** y of the first cell of a page menu (vfunc_12): (88 - 70) + ((450 - 42) - rows * 17) / 2 (the size query sees
	 *  spacing 0), pages 0, 2, 3; page 1 at 290, page 4 at 105. */
	SSXTRICKYREBUILD_API float MenuTopY(int32 Page, int32 Rows, float CellH);

	// FUN_002A36E8 / FUN_002A9488 / FUN_002A9350
	constexpr float CtrlOriginY = 115.f;
	constexpr float CtrlIconScale = 0.8f;
	/** .data 0x3483C8 {x, y, -, sprite}; W / H = the sprite record's size (61 ps2c 256 x 177, 38 fe_2 48 x 38, 39 fe_2 38 x 48), drawn x 0.8; the origin x uses sprite 61's width. */
	struct FCtrlIcon { float X, Y; int32 Sprite; float W, H; };
	constexpr float CtrlBodyW = 256.f;
	struct FCtrlLabel { float X, Y; uint32 Flags; int32 Loc; };  // 0x3482C8 / 0x348338 / 0x348258
	SSXTRICKYREBUILD_API const TArray<FCtrlIcon>& CtrlIcons();
	SSXTRICKYREBUILD_API const TArray<FCtrlLabel>& CtrlLabels(int32 Group);
	constexpr float CtrlLabelScale = 0.8f;
}

namespace SSXFeRiderProfile
{
	// 0x3B0110 / 0x3AFAC0 / 0x28C1B8
	constexpr float PanelX = 180.f, PanelW = 460.f, ProfilePanelH = 480.f;            // FUN_0027E918
	constexpr float PortraitX = 0.f, PortraitY = 58.f, PortraitW = 180.f, PortraitH = 364.f;
	constexpr int32 SprEdge = 40;
	constexpr float EdgeW = 13.f, EdgeTileH = 30.f;
	constexpr float BarH = 58.f;
	constexpr float BioX = 350.f, BioY = 111.f, BioSpacing = 4.f, BioScale = 0.8f;   // FUN_0027F600
	constexpr float TextX = 205.f, TextY = 120.f, TextW = 350.f, TextH = 260.f;     // FUN_00284F60 / FUN_00284C70
	constexpr float TextScale = 0.8f, TextExtra = 8.f;
	SSXTRICKYREBUILD_API int32 PortraitSprite(int32 Page);
	// 0x33F3B8
	SSXTRICKYREBUILD_API const TCHAR* PortraitFolder(int32 Char);
	SSXTRICKYREBUILD_API FName PortraitPage(int32 Char, int32 Sprite);
	// FUN_0027F600
	SSXTRICKYREBUILD_API int32 BioValueLoc(int32 Char, int32 Row);
	// FUN_00284F60
	SSXTRICKYREBUILD_API void BackStoryRange(int32 Char, int32& OutFirst, int32& OutLast);
	// 0x3AF9C8
	SSXTRICKYREBUILD_API void InterviewRange(int32 Char, int32& OutFirst, int32& OutLast);

	// FUN_00282D30 / 0x3AF8D0 / FUN_0016B650 / FUN_001A6BA0
	constexpr float WcRaceX = 320.f, WcShowX = 470.f, WcListY = 150.f, WcSpacing = 6.f, WcGap = 15.f, WcCell = 70.f;
	constexpr float WcMedalW = 20.f, WcMedalH = 15.f, WcScale = 0.8f;
	constexpr int32 SprMedalGold = 0x3A, SprMedalSilver = 0x39, SprMedalBronze = 0x38;
	constexpr float RankX = 45.f, RankY = 100.f, RankCellW = 120.f, RankCellH = 20.f, RankSpacing = 7.f, RankScale = 0.7f;
	constexpr float RankCursorW = 180.f, RankCursorH = 30.f;
	inline const FLinearColor RankCursor = FeColor(0.7f, 1.5625f, 1.2499f, 0.2687f);
	// FUN_0016B650 / 0x37EBF0
	SSXTRICKYREBUILD_API int32 VenueNameLoc(int32 Track);

	// FUN_00285230 / FUN_001A6BA0
	constexpr float BestX = 390.f, BestY = 135.f, BestSpacing = 3.f, BestCell = 80.f;
}

/**
 * cFE3DScrollingText (ctor FUN_002B1B20, update vfunc_13 0x2B2420, draw vfunc_12 0x2B2080, key vfunc_29 0x2B2510):
 * a column of N = int(box h / pitch) line slots (+0x110); pitch = line height * scale + 3 (+0x1AC). Every update the
 * offset (+0x1A4) grows by the speed (+0x1A8, 0.3 px); once it exceeds one pitch the slots shift up one, the offset
 * drops by a pitch and the next LOC (+0x194, from +0x198 to +0x19C) enters the bottom slot; when every slot is empty
 * again the text restarts from the first LOC.
 */
struct SSXTRICKYREBUILD_API FSSXFeScrollText
{
	int32 First = 2143, Last = 2986;   // FUN_002A8380
	int32 Cur = 2143;
	int32 Dir = 0;
	float Offset = 0.f;
	float Speed = 0.3f;
	float Extra = 3.f;
	float Scale = 1.f;
	float LineH = SSXFeOptions::MenuLineH;
	TArray<int32> Slots;

	float Pitch() const { return LineH * Scale + Extra; }
	void Reset(float BoxH);
	void Tick();
	void Key(bool bUp);
	/** vfunc_12 alpha of slot I: the top slot fades out (1 - |off| / pitch), the bottom slot fades in (|off| / line height),
	 *  clamped to 0..1; Dir 1 inverts. */
	float SlotAlpha(int32 I) const;
};

struct SSXTRICKYREBUILD_API FSSXFeOptionsUI
{
	int32 RowSel[SSXFeOptions::NumPages] = {};
	int32 List[SSXFeOptions::NumPages][SSXFeOptions::MaxRows] = {};
	float Bar[SSXFeOptions::NumPages][SSXFeOptions::MaxRows] = {};
	int32 SnapList[SSXFeOptions::NumPages][SSXFeOptions::MaxRows] = {};
	float SnapBar[SSXFeOptions::NumPages][SSXFeOptions::MaxRows] = {};
	int32 ArrowFlashFrame[2] = { -1000, -1000 };
	int32 CtrlDiagram = -1;
	bool bCreditsActive = false;
	FSSXFeScrollText Credits;
};
