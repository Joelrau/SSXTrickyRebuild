#pragma once

#include "CoreMinimal.h"

constexpr FLinearColor FeColor(float A, float R, float G, float B) { return FLinearColor(R, G, B, A); }

namespace SSXFe
{
	constexpr float ScreenW = 640.f;
	constexpr float ScreenH = 480.f;
	constexpr int32 FrameRate = 60;

	// FUN_002b1138 / FUN_002719c0
	inline const FLinearColor Gold = FeColor(1.f, 0.953f, 0.729f, 0.106f);
	inline const FLinearColor White = FeColor(1.f, 1.f, 1.f, 1.f);
	inline const FLinearColor Grey = FeColor(1.f, 0.5f, 0.5f, 0.5f);
	inline const FLinearColor Black = FeColor(1.f, 0.f, 0.f, 0.f);
	inline const FLinearColor OptionsPanel = FeColor(0.88f, 0.1f, 0.1f, 0.1f);
	inline const FLinearColor RiderProfilePanel = FeColor(1.f, 0.6235f, 0.8392f, 0.8274f);
	constexpr int32 SelectPulseFrames = 30;   // FUN_002742e8
	constexpr int32 SlideFrames = 30;         // FUN_002ac3b0
	// FUN_001FF000
	constexpr int32 FadeLayer = 64;

	enum class EFont : uint8 { Title, Menu, Small };
	constexpr float TitleScaleX = 1.4f, TitleScaleY = 1.3f;
	constexpr float MenuScaleX = 1.8f, MenuScaleY = 1.4f;
	constexpr float ShadowOffset = 2.f;       // FUN_0019d3f0

	constexpr float ListX = 320.f;
	// 0x2B5EF0 / 0x2B5668 / 0x26DD60 / 0x26DF58
	constexpr float ListY93 = 93.f;           // FUN_00274108 / FUN_00274278
	constexpr float ListSpacing = 4.f;
	constexpr float TitleLineScale = 0.8f;

	// FUN_002720F8
	constexpr int32 SpriteLayer(int32 L) { return 2 * L; }
	constexpr int32 TextLayer(int32 L) { return 2 * L + 1; }
	constexpr int32 FrameLayer = 6;
	constexpr float TopGroupH = 79.f;
	constexpr float PanelH = 39.f, StripH = 40.f;
	constexpr float TabTextX = 20.f, TabTextY = 20.f, TabPadW = 20.f, TabPadH = 27.f, TabCapW = 29.f;
	constexpr int32 SprTabBody = 33, SprTabCap = 32, SprPanelL = 13, SprPanelR = 14, SprStripL = 5, SprStripR = 6;
	constexpr float BottomGroupY = 365.f, BottomGroupH = 115.f;   // FUN_002755E8
	constexpr float BottomFillY = 40.f, BottomFillH = 75.f, BottomFillRepV = 75.f / 39.f;
	constexpr int32 SprFillL = 11, SprFillR = 12, SprBStripL = 7, SprBStripR = 8;
	inline const FLinearColor FrameGrey = FeColor(1.f, 0.8f, 0.8f, 0.8f);
	// FUN_002719C0 / FUN_002AE230
	constexpr float HelpBarX = 595.f, HelpBarY = 37.f;
	constexpr float HelpIconSize = 17.f, HelpIconGap = 4.f, HelpSlotH = 18.f, HelpRowPitch = 20.f, HelpLabelScale = 0.7f;
	constexpr float HelpLineX = 20.f, HelpLineY = 37.f, HelpLineW = 480.f, HelpLineScale = 0.8f;
	// FUN_00273F18
	constexpr int32 TitleBarLayer = 2;
	constexpr float BarEdgeH = 42.f, BarNotchY = 35.f, BarNotchX = 115.f, BarNotchW = 410.f, BarNotchH = 14.f;
	constexpr int32 SprBarEdgeL = 17, SprBarEdgeR = 18;
	constexpr float HBarH = 105.f;
	// FUN_00274000
	constexpr float ItemCX = 320.f, ItemCY = 63.f;
	constexpr int32 SprArrowL = 3, SprArrowR = 4;
	constexpr float ArrowLX = 138.f, ArrowRX = 485.f, ArrowY = 56.f, ArrowSize = 18.f;
	constexpr int32 ArrowFlashFrames = 15;
	inline const FLinearColor ArrowFlash = FeColor(1.f, 0.8f, 0.1f, 0.1f);   // 0x3D36F0
	// 0x293528
	constexpr float TitleListX = 20.f, TitleListY = 330.f, TitleHelpY = 428.f;

	// FUN_001F1580
	constexpr int32 IconCircle = 79, IconTriangle = 80, IconCross = 81, IconSquare = 82;
	constexpr int32 IconRankActive = 27, IconRankInactive = 28;
	constexpr int32 SprPip = 29, SprBarCap = 30, SprBarTrack = 31;

	constexpr int32 HudCross = 0x74, HudTriangle = 0x73, HudCircle = 0x72, HudStart = 0x6B, HudSelect = 0x6A;
	constexpr int32 HudL2 = 0x6F, HudR2 = 0x71, HudDpadL = 0x62, HudDpadR = 0x63;
	constexpr int32 HudMedalGold = 0x29, HudMedalSilver = 0x28, HudMedalBronze = 0x27;
	constexpr int32 HudArrowL = 0x2B, HudArrowR = 0x2C;
	constexpr int32 HudBoxCorner = 0x37, HudTabL = 0x3E, HudTabM = 0x3F, HudTabR = 0x40;
	constexpr int32 HudSlider = 0x56, HudSliderFill = 0x57;
}

// 0x3857C0
enum class ESSXFeKey : int32
{
	None = 0,
	Cross = 'd',
	Start = 0x0D,
	Triangle = 'b',
	Select = 0x1B,
	Square = 'a',
	Circle = 'c',
	Up = 0x4800,
	Down = 0x5000,
	Left = 0x4B00,
	Right = 0x4D00,
	L1 = 'l',
	L2 = 'L',
	R1 = 'r',
	R2 = 'R',
};

namespace SSXFe
{
	inline bool IsAccept(ESSXFeKey K) { return K == ESSXFeKey::Cross || K == ESSXFeKey::Start; }       // FUN_0026ce08
	inline bool IsBack(ESSXFeKey K) { return K == ESSXFeKey::Triangle || K == ESSXFeKey::Select; }     // FUN_0026ce30

	enum EPadBit : uint32
	{
		PadUp = 0x1, PadDown = 0x2, PadRight = 0x4, PadLeft = 0x8, PadSquare = 0x10, PadTriangle = 0x20,
		PadCircle = 0x40, PadStart = 0x80, PadSelect = 0x100, PadL1 = 0x200, PadR1 = 0x400, PadCross = 0x800,
		PadL2 = 0x1000, PadR2 = 0x2000,
	};
	// 0x3857C0
	SSXTRICKYREBUILD_API ESSXFeKey KeyForPadBit(uint32 Bit);

	/** Left-stick direction bits of the pad word (cPS2Device translate 0x17B278): strict |v| > 0.5, no
	 *  hysteresis, PS2 axes (y down). */
	enum EStickBit : uint32 { PadLJoyUp = 0x20000, PadLJoyDown = 0x40000, PadLJoyRight = 0x80000, PadLJoyLeft = 0x100000 };
	SSXTRICKYREBUILD_API uint32 StickBits(float X, float Y);
	/** The FE key of a pad word: the first row of .data 0x3857C0 whose mask hits it (Select, Start, Square, Triangle, Circle,
	 *  Cross, Left|LJOY_LEFT, Right|LJOY_RIGHT, Down|LJOY_DOWN, Up|LJOY_UP, R1, R2, L1, L2); its shoulder column when any of
	 *  L1/R1/L2/R2 (0x3600) is held (e.g. Right -> 0x4F00, which no menu reads). 0 for none. */
	SSXTRICKYREBUILD_API int32 FeKeyForWord(uint32 Word);
	/** The pad object's FE key emitter (+0x2C emitted, +0x30 last, +0x34 counter): a key is emitted on the frame it
	 *  changes; while it stays the same the counter counts up from -7 and, only with repeat enabled (+0x08), re-emits once
	 *  past 7 (15 frames, then every 8). cFE3D::vfunc_02 turns repeat OFF for both FE pads (FUN_0026CF28(pad, 0)), so in the
	 *  front end a held key is emitted once. */
	struct SSXTRICKYREBUILD_API FFeKeyEmitter
	{
		int32 Last = -1;     // 0x17AA08
		int32 Counter = 0;
		bool bRepeat = false;
		int32 Step(uint32 Word);
	};

	// 0x347F20
	struct FHelpEntry { uint32 Mask; int32 Icon; int32 Loc; };
	SSXTRICKYREBUILD_API const TArray<FHelpEntry>& HelpBarTable();
}
