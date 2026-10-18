// 0x3B01F0 / FUN_00154580 / 0x31E210 / FUN_00154638
#pragma once

#include "CoreMinimal.h"
#include "UI/SSXFeTypes.h"
#include "World/SSXPropMovie.h"

struct FSSXCareer;

namespace SSXFeTrickBook
{
	constexpr int32 Chapters = 6, TricksPerChapter = 5, TricksPerRider = 30, RecordSize = 0x1C;

	// FUN_001551C0
	struct FEntry
	{
		uint32 W0 = 0, W1 = 0;
		/** The rest of the record (FUN_001546C8 builds the button string from it): +8 flip, +10 spin (180 deg units),
		 *  +0xC rail, +0x10 / +0x12 tweak flags, +0x14 / +0x18 grab actions (0x1A = none). */
		int16 Flip = 0, Spin = 0, Rail = 0, Tweak1 = 0, Tweak2 = 0;
		int32 ActA = 0x1A, ActB = 0x1A;
	};

	class SSXTRICKYREBUILD_API FTable
	{
	public:
		static const FTable& Get();
		bool LoadFile(const FString& Path);
		bool LoadBytes(const TArray<uint8>& Bytes);
		bool IsLoaded() const { return Entries.Num() == 12 * TricksPerRider; }
		// FUN_00154638
		const FEntry* Find(int32 Char, int32 K) const;
		// FUN_00154638 / FUN_001551C0
		FString TrickName(int32 Char, int32 K) const;
		TArray<FEntry> Entries;
	};

	/** FUN_00157C18: a landed trick record {W0, W1} (Sim/Air/TrickName.h) is the entry when word 0 is equal and word 1
	 *  agrees in the flip bits 0x07000F00 (flip count bits 24..26, flip name bits 8..11); the prefix (Switch / Late /
	 *  Rail To), the spin direction, the suffix (To Fakie / Air / To Rail), the second-grab link and the bail text are
	 *  ignored. */
	SSXTRICKYREBUILD_API bool Matches(const FEntry& E, uint32 W0, uint32 W1);

	/** In-race trick-book step of FUN_001579E0 for one landed record (air landing 0x1569B0 or rail exit 0x156618): for k = 0..29 of the book's rider (career +0x14), entry k matches, is done in neither word (+0x18 / +0x1C) and its chapter (FUN_001640A8: k / 5) is the current chapter (FUN_00164060, evaluated again for every k, so finishing a chapter lets a later entry of the next one count on the same landing) -> bit k is set in the career word +0x1C and the slot's event word (slot+0x10, FUN_00163F50). Returns the bits set (the caller then pushes HUD message 0x11 for 1.2 s and plays zbxsfx #95 x3, 0x2190F0). */
	SSXTRICKYREBUILD_API uint32 LandBookTrick(const FTable& T, FSSXCareer& C, uint32& EventBits, uint32 W0, uint32 W1);
	SSXTRICKYREBUILD_API FString ExplainNoBook(const FTable& T, const FSSXCareer& C, uint32 W0, uint32 W1);

	// 0x27E3C0
	// FUN_001F9EE8
	constexpr int32 SprVideo = 0x7B;
	/** "data\\video\\tb\\%s_%03d.sss" (.data 0x3AF808) with the rider's short name (0x331EB0 + rider x 0x38: Eddi, Kaor,
	 *  Luth, Mac, Moby, Zoe, Jp, Elis, Psym, Seei, Brod, Mari) and k = chapter x 5 + trick. */
	SSXTRICKYREBUILD_API FString VideoEntry(int32 Char, int32 K);
	/** The entry from the git-ignored extracted disc (extracted/DATA/VIDEO/TB/TB_<SHORT>.BIG, an EA "C0FB" archive, or
	 *  "BIGF" for the three past 16 MB; the .sss entries are stored raw). */
	SSXTRICKYREBUILD_API bool ReadVideo(int32 Char, int32 K, TArray<uint8>& Out);
	// C0FB / BIGF directory lookup (case-insensitive name) in archive bytes.
	SSXTRICKYREBUILD_API bool FindBigEntry(const TArray<uint8>& Archive, const FString& Name, TArray<uint8>& Out);

	/** The picture's stream: FUN_001FFF30(stream manager, path, 128, 128, loop 1, fps 15), released (0x200098) by the next
	 *  highlight (vfunc_23), a chapter load (vfunc_18) and the state close (vfunc_16). Same stream rules as the in-track
	 *  movies (World/SSXPropMovie.h): one 60 Hz step per frame, a chunk every 4th, looping; nothing decoded = blank. */
	class SSXTRICKYREBUILD_API FVideo
	{
	public:
		bool Step(int32 Char, int32 K);
		bool HasFrame() const { return CurK >= 0 && Stream.Shown != INDEX_NONE && Pixels.Num() > 0; }
		TArray<FColor> Pixels;
		int32 W = 0, H = 0;
		int32 CurChar = -1, CurK = -1;
		SSXPropMovie::FSssFile File;
		SSXPropMovie::FStream Stream;
	};

	SSXTRICKYREBUILD_API FName CoverPage(int32 Char);
	SSXTRICKYREBUILD_API FName TrickPage(int32 Char, int32 Chapter, int32 Trick);
	SSXTRICKYREBUILD_API FString CoverFolder(int32 Char);
	SSXTRICKYREBUILD_API FString TrickFolder(int32 Char, int32 Chapter);
	/** Converted file of image Trick (0..4) inside a TrickFolder: by index ("<nnn>_*.png"), since the image names in
	 *  tb_<short><chapter>.ssh run on through the chapters (tb_<short>2 = tb10..tb14) and the game binds by index. */
	SSXTRICKYREBUILD_API FString TrickImagePattern(int32 Trick);

	// 0x27A5B8
	// background "f3icnblackcirc": untextured 640 x 480, (1, 1.1137, 0.8627, 0) (x128 -> (143, 110, 0)), layer 0.
	inline const FLinearColor Background = FeColor(1.f, 1.1137f, 0.8627f, 0.f);
	constexpr int32 SprBand = 60;
	constexpr float BandY = 320.f, BandH = 80.f, BandRepU = 6.f;
	inline const FLinearColor BandColor = FeColor(0.3921f, 1.f, 1.f, 1.f);
	constexpr int32 SprCover = 0x86;
	constexpr float CoverX = 0.f, CoverY = 150.f, CoverSize = 195.f;
	constexpr float NameY = 105.f;
	// FUN_0019E840
	constexpr int32 SprChapter = 43;
	constexpr float ChapterX = 480.f, ChapterY = 105.f;
	constexpr float TrickNameX = 460.f, TrickNameY = 142.f, TrickNameW = 150.f, TrickNameScale = 0.85f;
	constexpr float PicX = 320.f, PicY = 205.f, PicW = 257.f, PicH = 158.f;
	struct FFramePiece { float X, Y, W; uint32 Flags; };
	SSXTRICKYREBUILD_API const FFramePiece* FramePieces();
	constexpr int32 SprFrame = 41;
	/** tricks group "f3wgrpTricks" at (0, 305), layer 3: top edge sprite 41 320 x h41 (flags 1 / 0), black panel 640 x 165
	 *  at (0, h41); the trick list (layer +1) centred by vfunc_18: x = (640 - 5 (90 + 9)) / 2, y 10; icons 90 x 50, spacing
	 *  9; arrows 30 x 40 (sprites 3 / 4) at y 15, left at x - 30 - 9, right at x + 5 x 99; the slider (sprite 46, 100 x 60,
	 *  layer +2) on the highlighted icon at its position - (5, 5). */
	constexpr float GroupY = 305.f, TricksPanelH = 165.f;
	constexpr float TrickListY = 10.f, IconW = 90.f, IconH = 50.f, IconSpacing = 9.f;
	constexpr float TrickArrowW = 30.f, TrickArrowH = 40.f, TrickArrowY = 15.f;
	constexpr int32 SprSlider = 46;
	constexpr float SliderW = 100.f, SliderH = 60.f, SliderInset = 5.f;
	inline const FLinearColor IconDisabled = FeColor(1.f, 1.f, 0.3f, 0.3f);
	constexpr float TrickListX = (640.f - TricksPerChapter * (IconW + IconSpacing)) * 0.5f;
	/** counter (+0x108): cFE3DWBufferedSingleLineText TITLE 0.7 gold, layer 4 of the bottom frame group, x (640 - w) / 2,
	 *  y 65; LOC 3691 "%d / %d Tricks Completed" with (done in the chapter, 5). */
	constexpr float CounterY = 65.f, CounterScale = 0.7f;
	constexpr int32 CounterLoc = 3691;
	constexpr int32 ChapterHelpLoc = 3545, TrickHelpLoc = 3546;
	constexpr uint32 ChapterLegend = 0x800A, TrickLegend = 0x800C;
}
