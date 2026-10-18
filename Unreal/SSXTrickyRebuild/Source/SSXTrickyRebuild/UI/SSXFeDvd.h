// 0x3B3270 / 0x2986C0 / 0x293E10 / FUN_00297178
#pragma once

#include "CoreMinimal.h"
#include "UI/SSXFeTypes.h"

class FSSXFeDrawList;
class FSSXLocStrings;

/** One cFE3DWidget tween channel (FUN_002AC348 / FUN_002AC3B0, update 0x2AD380): while active, each update applies lerp(from, to, n / frames) (ends swapped when reversed), then n += 1; past frames n returns to 0 and mode 0 deactivates, mode 1 loops, mode 2 flips the direction. Activation keeps n; the first update after the setup resets n and the direction (vfunc_08). */
struct SSXTRICKYREBUILD_API FSSXFeTween
{
	float From = 0.f, To = 0.f;
	int32 Frames = 1;
	int32 Mode = 0;
	int32 N = 0;
	bool bActive = false;
	bool bReverse = false;
	bool bInit = false;
	float Cur = 0.f;
	void Setup(float InFrom, float InTo, int32 InFrames, int32 InMode) { From = InFrom; To = InTo; Frames = FMath::Max(InFrames, 1); Mode = InMode; N = 0; bInit = false; bReverse = false; Cur = InFrom; }
	void Activate(bool bRev) { bActive = true; bReverse = bRev; }
	float Progress() const { return float(N) / float(Frames); }
	void Tick()
	{
		if (!bActive) { return; }
		if (!bInit) { bInit = true; N = 0; bReverse = false; }
		const float T = float(N) / float(Frames);
		Cur = bReverse ? To + (From - To) * T : From + (To - From) * T;
		if (++N > Frames)
		{
			N = 0;
			if (Mode == 0) { bActive = false; }
			else if (Mode == 2) { bReverse = !bReverse; }
		}
	}
};

struct SSXTRICKYREBUILD_API FSSXFeDvd
{
	enum EList : int32 { None = -1, Main = 0, Voices = 1, Talent = 2, Jukebox = 3, Previews = 4, NumLists = 5 };

	struct FItem
	{
		int32 Id = 0;
		FString Text;
		SSXFe::EFont Font = SSXFe::EFont::Menu;
		float Scale = 0.9f;
		FLinearColor Color = FLinearColor(0.f, 0.f, 0.f, 1.f);
		bool bShadow = true;
		bool bSelectable = true;
		bool bIcon = false;
		float X0 = 0.f;
		float Y = 0.f;
		bool bAbsY = false;
		float BaseY = 0.f;
		int32 Row = 0;
		int32 Column = 0;
		FSSXFeTween Slide;
		float X() const { return X0 + Slide.Cur; }
		float YAt(const FSSXFeDrawList& D) const;
	};

	struct FList
	{
		TArray<FItem> Items;
		int32 Sel = 0;
	};

	FList Lists[NumLists];
	EList Focus = None;
	/** +0xA8: 0 idle, 1 main opening, 2 main closing to a sub-page, 3 voices opening, 4 voices -> talent, 5 voices -> main,
	 *  6 talent opening, 7 talent -> voices, 8 jukebox opening, 9 jukebox -> main, 10 previews opening, 11 previews -> main. */
	int32 Phase = 1;
	EList ChainList = Main;
	int32 ChainItem = 0;
	uint32 LegendMask = 10;      // FUN_002719C0
	int32 TitleLoc = 3324;
	FString TitleText;           // FUN_002995A8
	FSSXFeTween TitleSlide;
	FSSXFeTween VoiceSlide;
	FString VoiceLabel, VoiceValue;
	FSSXFeTween TileFade;
	FSSXFeTween PortraitFade;
	bool bTileShown = false, bPortraitShown = false;
	int32 TileSprite = 0x68;
	int32 PortraitSprite = -5;
	FSSXFeTween Pulse[4];
	bool bPulseShown = true;
	FSSXFeTween Chevron[5];
	FSSXFeTween Arrow;
	bool bTalentNameShown = false;
	FString TalentName;          // 0x348038
	bool bJukeboxInfo = false;
	int32 Song = 0;
	bool bSongEnded = false;
	static constexpr int32 NumSongs = 21;
	// FUN_00299338
	float PlayIconX = 0.f;
	int32 PlayIconItem = INDEX_NONE;
	int32 Char = 0;

	// 0x293E10 / FUN_00297178 / FUN_00297FA0 / FUN_00298368
	void Build(const FSSXLocStrings* Loc);
	// 0x2986C0
	void Tick(TArray<FString>& OutHooks, const FSSXLocStrings* Loc);
	// 0x299700
	void Highlight(TArray<FString>& OutHooks, const FSSXLocStrings* Loc);
	// FUN_00275468
	void SetFocus(EList L, TArray<FString>& OutHooks, const FSSXLocStrings* Loc);
	// FUN_00296870
	void BuildTalent(int32 C, const FSSXLocStrings* Loc);
	/** FUN_00299338: play jukebox song S (cFE3DSound entry 29), the play icon at its item, the CURRENTLY PLAYING / ARTIST
	 *  values. */
	void PlaySong(int32 S, TArray<FString>& OutHooks);
	/** The focused menu's Up / Down (and the jukebox columns' Left / Right): the menu sound hook to play ("move", "refused"
	 *  or empty). */
	FString Move(int32 DY, int32 DX, TArray<FString>& OutHooks, const FSSXLocStrings* Loc);
	const FItem* Selected() const;
	static float ItemH(const FSSXFeDrawList& D, const FItem& I);
	void Draw(FSSXFeDrawList& D, const FSSXLocStrings* Loc) const;
	void StartChain(EList L, bool bReverse);

	// 0x293E10
	static constexpr float ListX0 = -640.f;
	static constexpr float ListSlide = 740.f;
	static constexpr float MainY = 132.f;
	static constexpr float PreviewsY = 200.f;
	static constexpr float Spacing = 10.f;
	static constexpr float JukeY = 160.f;
	static constexpr float JukeX0 = -600.f;
	static constexpr float JukeX1 = 1020.f;
	static constexpr float JukeSlide = 640.f;
	static constexpr int32 MainFrames = 40;
	static constexpr int32 SubFrames = 30;
	static constexpr int32 TalentFrames = 15;
	static constexpr float TitleX0 = 1235.f;
	static constexpr float TitleY = 20.f;
	static constexpr float TitleShadowDX = 20.f;
	static constexpr float TitleSlideBy = -640.f;
	static constexpr int32 TitleFrames = 35;
	static constexpr float VoiceX0 = -620.f;
	static constexpr float VoiceY = 440.f;
	static constexpr float VoiceSlideBy = 640.f;
	static constexpr float VoiceScale = 0.8f;
	static constexpr int32 VoiceFrames = 40;
	static constexpr float TileX = 355.f;
	static constexpr float TileY = 124.f;
	static constexpr float TileW = 262.f;
	static constexpr float TileH = 224.f;
	static constexpr float PortraitX = 349.f;
	static constexpr float PortraitY = 133.f;
	static constexpr float PortraitW = 297.f;
	static constexpr float PortraitH = 283.f;
	static constexpr float TalentNameX = 90.f;
	static constexpr float TalentNameY = 94.f;
	static constexpr float TalentX = 90.f;
	static constexpr float TalentY0 = 130.f;
	static constexpr float LegendX = 595.f;
	static constexpr float LegendY = 425.f;
	static constexpr float InfoX = 250.f;
	static constexpr float InfoY0 = 115.f;
	static constexpr float InfoY1 = 145.f;
	static constexpr float InfoScale = 0.9f;
	static constexpr int32 SprBackground = 47;
	static constexpr int32 SprLogo = 48;
	static constexpr int32 SprChevron = 53;
	static constexpr int32 SprPlay = 55;
};

namespace SSXFeDvdTables
{
	/** Main list LOCs (.data 0x3B2420): THIS IS SSX, TRICKS, TRACKS, CHARACTERS, CHARACTER VOICES, MUSIC, AUDIO, JUKEBOX,
	 *  PREVIEWS. */
	SSXTRICKYREBUILD_API int32 MainLoc(int32 Item);
	/** The movie of a main item (vfunc_11 0x293C20: 0 evolve, 1 tricks, 2 tracks, 3 charactr, 5 music, 6 audio), empty
	 *  for the sub-pages; the converted movie name (data/video/<name>.mpc upper case). */
	SSXTRICKYREBUILD_API FString MainMovie(int32 Item);
	// FUN_00296870 / 0x3B2530
	SSXTRICKYREBUILD_API bool TalentRange(int32 Char, int32& OutFirst, int32& OutLast);
	SSXTRICKYREBUILD_API int32 MainSpeech(int32 Item);
	SSXTRICKYREBUILD_API int32 VoiceSpeech(int32 Char);
	/** DVD tile / portrait sprites (vfunc_23): main 0x68 + DVD_MAIN image (TRICKS 0x6A, TRACKS 0x69); voices 0x71..0x7A =
	 *  the DVD_CHAR images by character; -5 none. */
	SSXTRICKYREBUILD_API int32 MainTile(int32 Item);
	SSXTRICKYREBUILD_API int32 VoicePortrait(int32 Char);
	SSXTRICKYREBUILD_API FName PageOf(int32 Sprite);
	SSXTRICKYREBUILD_API FString FolderOf(int32 Sprite);
	SSXTRICKYREBUILD_API FString ImageOf(int32 Sprite);
}
