#pragma once

#include "CoreMinimal.h"

struct SSXTRICKYREBUILD_API FSSXTrackInfo
{
	FString Id;
	FString MapName;
};

namespace SSXFrontEnd
{
	SSXTRICKYREBUILD_API const TArray<FSSXTrackInfo>& Tracks();

	SSXTRICKYREBUILD_API FString RepoRoot();
	SSXTRICKYREBUILD_API FString LangDir();
	SSXTRICKYREBUILD_API FString SfnDir();
	SSXTRICKYREBUILD_API FString TextureDir();
	SSXTRICKYREBUILD_API FString FrontEndDir();
	SSXTRICKYREBUILD_API FString FontDir();
	SSXTRICKYREBUILD_API FString VideoDir();

	SSXTRICKYREBUILD_API double WavSeconds(const FString& WavPath);
	/**
	 * cPS2VideoPlayer::vfunc_04 0x2BA810: a movie is done when skipped (+8), or once the video stream has ended (+0x14,
	 * FUN_002EA2A8) AND, when it has an audio stream (+0xB4 >= 0), that stream reports state 3 (FUN_002D2C10 -> +0x58).
	 * The last picture stays up while a longer audio tail plays out (EABIG: 77 frames = 2.57 s, audio 3.50 s).
	 */
	SSXTRICKYREBUILD_API bool MovieDone(bool bSkipped, bool bVideoEnded, double AudioSeconds, double AudioElapsed);
	/**
	 * Where a movie picture of Picture (pixels) lands in the 640 x 480 front-end frame. cPS2VideoPlayer (ctor 0x2BA098)
	 * switches the display to its own 512 x 448 frame buffer (+0x18 / +0x1C) and FUN_002BA290 writes the picture there in
	 * 16-pixel columns from x = max(0, (512 - w) / 2), y = 0, clipped to 512 x 448. The frame buffer fills the screen as
	 * the game's own 512 x 448 one does, so every disc movie (all 512 x 448) fills the frame.
	 */
	SSXTRICKYREBUILD_API FBox2f MovieRect(FIntPoint Picture);

	SSXTRICKYREBUILD_API bool MapExists(const FString& MapName);
	SSXTRICKYREBUILD_API FString MapPackage(const FString& MapName);

	SSXTRICKYREBUILD_API extern const TCHAR* FrontEndMap;
}

/**
 * DATA/LANG string table. The executable passes LOC indexes directly to the text widgets
 *, so ByIndex() is the primary access;
 * CONSTANT.LOC supplies the key names (kFE_...) for Get(Key) and IndexOf(Key).
 */
class SSXTRICKYREBUILD_API FSSXLocStrings
{
public:
	bool Load(const FString& Dir, const FString& Language = TEXT("AMERICAN"));

	static bool ReadLocFile(const FString& Path, TArray<FString>& Out);

	FString ByIndex(int32 Id) const { return Texts.IsValidIndex(Id) ? Texts[Id] : FString(); }
	FString Get(const FString& Key) const { return ByIndex(IndexOf(Key)); }
	int32 IndexOf(const FString& Key) const { const int32* I = Keys.Find(Key); return I ? *I : INDEX_NONE; }
	FString KeyOf(int32 Id) const { return KeyNames.IsValidIndex(Id) ? KeyNames[Id] : FString(); }
	bool IsLoaded() const { return Texts.Num() > 0; }
	int32 Num() const { return Texts.Num(); }

private:
	TArray<FString> Texts;
	TArray<FString> KeyNames;
	TMap<FString, int32> Keys;
};
