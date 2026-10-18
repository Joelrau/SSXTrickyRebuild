#pragma once

#include "CoreMinimal.h"
#include "Progression/SSXProgression.h"

namespace SSXFeTables
{
	constexpr int32 NumRiders = 12;
	constexpr int32 BoardsPerRider = 12;
	constexpr int32 OutfitsPerRider = 7;
	constexpr int32 TrickTutorialTrack = 10;

	// 0x331EA8
	SSXTRICKYREBUILD_API const TCHAR* RiderName(int32 Char);
	SSXTRICKYREBUILD_API const TCHAR* RiderShort(int32 Char);
	// 0x3470B8
	SSXTRICKYREBUILD_API int32 CarouselRider(int32 Slot);
	SSXTRICKYREBUILD_API int32 CarouselSlot(int32 Char);
	// FUN_00163B10
	SSXTRICKYREBUILD_API int32 UnlockOrder(int32 K);
	SSXTRICKYREBUILD_API int32 UnlockRank(int32 Char);

	// 0x3A8AF0
	SSXTRICKYREBUILD_API const TArray<int32>& VenueOrder();
	SSXTRICKYREBUILD_API int32 TrackCatalogIndex(int32 TrackId);
	/** Track id -> LOC of its name: FUN_0016B650, jump table 0x37EBF0 (tracks 0..11; track 9 = track 6's 3685, track 10 = 3610, anything else 3613). */
	SSXTRICKYREBUILD_API int32 TrackNameLoc(int32 TrackId);

	SSXTRICKYREBUILD_API int32 BoardCost(int32 Board);
	// FUN_00164840
	SSXTRICKYREBUILD_API int32 RankFromPoints(int32 Points);
	SSXTRICKYREBUILD_API int32 BoardTypeLoc(int32 Type);

	// 0x332148
	struct FBoard
	{
		FString Name;
		int32 Index = 0;
		int32 Type = -1;
		int32 Cost = 0;
		uint8 Stats[4] = { 0, 0, 0, 0 };
	};
	/** Board table (.data 0x332148). The 144 records load from converted/frontend/fe_tables.json or a CSV of the same table (LoadBoardsCsv). Without either, names are empty and types unknown. */
	SSXTRICKYREBUILD_API const FBoard& Board(int32 Char, int32 Slot);
	SSXTRICKYREBUILD_API bool BoardsLoaded();
	SSXTRICKYREBUILD_API bool LoadBoardsCsv(const FString& Path);

	/** The exe tables of the front end (converted/frontend/fe_tables.json): boards, outfits .data 0x332C88, venue mode masks .data 0x379FD0 and the UserName grid .data 0x3B7F58. Returns false when the file is missing. */
	SSXTRICKYREBUILD_API bool LoadTablesJson(const FString& Path);
	/** ssxfe.pbd instance of the venue room's video screen i (mdl_videoscreen name table .data 0x347130, cFE3D fe+0xFC+4i); VenueSel drives screen i for VenueOrder()[i]. INDEX_NONE without the tables. */
	SSXTRICKYREBUILD_API int32 VenueScreenInstance(int32 I);
	/** ssxfe.pbd instances of the carousel slot's rider sign mdl_cs<Name>_0 (.data 0x3470C8) and platform mdl_Cplatform_* (.data 0x347100); FUN_00261B38 sets their TexFlip frame. INDEX_NONE without the tables. */
	SSXTRICKYREBUILD_API int32 RiderSignInstance(int32 Slot);
	/** ssxfe.pbd instance of mdl_csMarty_0 (.data 0x3470F8; INDEX_NONE without the tables). cFE3D::vfunc_02 0x263B30 builds a temporary cFE3DGeometry on it (FUN_00269A70 turns the world draw off, FUN_00269BE8(0, geom, 4) clears its show channel) and destroys it at once without joining the FE scene, so Marty is never shown. */
	SSXTRICKYREBUILD_API int32 MartySignInstance();
	SSXTRICKYREBUILD_API int32 PlatformInstance(int32 Slot);
	// 0x332C88
	SSXTRICKYREBUILD_API FString OutfitName(int32 Char, int32 Outfit);
	/** fe_tables.json string / int tables by key: track_names (.data 0x332F28 +4 by track id), dvd_talents / dvd_char_names (0x348068 / 0x348098 by character), dvd_jukebox.title / .artist / .length (0x3480C8, 21 songs), dvd_char_order (0x3B2918). Empty when not exported. */
	SSXTRICKYREBUILD_API const TArray<FString>& StringTable(const TCHAR* Key);
	SSXTRICKYREBUILD_API const TArray<int32>& IntTable(const TCHAR* Key);
	// 0x332F2C
	SSXTRICKYREBUILD_API FString TrackTableName(int32 TrackId);
	/** The outfit record's int (0,0,50,120,200,250,25000). Never read by the game (the only reference, 0x28DAF0, loads the name word); kept for completeness. */
	SSXTRICKYREBUILD_API int32 OutfitValue(int32 Char, int32 Outfit);
	// 0x379FD0
	SSXTRICKYREBUILD_API uint32 VenueModeMask(int32 GameMode);
	// 0x3B7F58
	SSXTRICKYREBUILD_API const FString& NameGrid();
}

struct SSXTRICKYREBUILD_API FSSXPlayerSlot
{
	int32 Char = -1;
	int32 Outfit = 0;
	int32 Board = 0;
	FString UserName;
	/** slot+0x10: trick-book bits learned this event (set in the race with the career word +0x1C by 0x1579E0, LandBookTrick; counted as unlock type 9; 0x165400 takes them back out of +0x1C on a restart / quit and on an unfinished / zero showoff; the event end 0x164FC0 clears them, keeping the career bits). */
	uint32 EventTricks = 0;
};

/** Profile manager 0x32B390: the saved blob (FSSXSaveData) plus the session state the menus and post-race overlays use (player slots, unlock list, heats, point sessions). */
struct SSXTRICKYREBUILD_API FSSXProfile : public FSSXSaveData
{
	FSSXPlayerSlot Slot[2];
	FSSXProgressSession Session;
	bool bSettingsDirty = false; // 0x161928

	// 0x3352A0
	bool IsRiderUnlocked(int32 Char, bool bUnlockAll) const { return bUnlockAll || (RiderUnlocks & (1u << Char)) != 0; }
	bool IsBoardUnlocked(int32 Char, int32 Board, bool bUnlockAll) const { return bUnlockAll || (Career[Char].Boards & (1u << Board)) != 0; }
	bool IsOutfitUnlocked(int32 Char, int32 Outfit, bool bUnlockAll) const { return bUnlockAll || (Career[Char].Outfits & (1u << Outfit)) != 0; }
	// FUN_00267B18 / 0x163D30 / 0x163D98
	void ShownGear(int32 Char, int32 Board, int32 Outfit, bool bUnlockAll, int32& OutBoard, int32& OutOutfit) const
	{
		OutBoard = IsBoardUnlocked(Char, Board, bUnlockAll) ? Board : 0;
		OutOutfit = IsOutfitUnlocked(Char, Outfit, bUnlockAll) ? Outfit : 0;
	}
	bool ChapterComplete(int32 Char, int32 Chapter) const { return Career[Char].ChapterDone(Chapter); }
	// 0x163BB8
	bool IsVenueUnlocked(int32 GameMode, int32 Venue, bool bUnlockAll) const;
	/** Medal the venue-select stage shows on a venue's mdl_medal model: WC race (mode 4) = the slot-0 career's race byte +0x25, WC showoff (5) = showoff byte +0x24; codes 0 gold, 1 silver, 2 bronze; anything else (3 = none, a new save) = no medal. FUN_00163E28 returns 3 in every other mode and for an empty slot 0, and FUN_00262D80 hides the models outside World Circuit; the G/S/B frame (channel 2 = 2 - code) is sent by ASSXFrontEndPlayerController. */
	int32 VenueMedal(int32 GameMode, int32 Venue) const;
	FSSXCareer* SlotCareer(int32 S) { return S >= 0 && S < 2 && Slot[S].Char >= 0 && Slot[S].Char < SSXFeTables::NumRiders ? &Career[Slot[S].Char] : nullptr; }
	const FSSXCareer* SlotCareer(int32 S) const { return const_cast<FSSXProfile*>(this)->SlotCareer(S); }

	// 0x161B40
	static FSSXProfile MakeDefault();

	// 0x1AC130 / 0x1B32C0 / 0x1BC8A0 / 0x1BBB30
	// 0x165400
	void DropEventTricks(int32 S);
	/** 0x1579E0 for a human in slot S landing the trick record {W0, W1}: the matching entries of the slot career's current
	 *  chapter go into career +0x1C and slot+0x10 (SSXFeTrickBook::LandBookTrick). Returns the bits set (0: no feedback). */
	uint32 LandBookTrick(int32 S, uint32 W0, uint32 W1);
	/** 0x164FC0 first step: in a showoff (modes 3 / 5) a human whose finish result (rider +0x418: the score once finished,
	 *  else < 0) is below 1 loses this event's entries (0x165400). */
	void ShowoffEndTricks(int32 S, int32 Mode, int32 FinishResult);
	/** 0x161A98: the settings words go back to the sound system (the port applies them every frame), the save dirty flag
	 *  P+0x3258 is cleared and the point sessions are rebuilt (0x166A58). */
	void ReloadSettings(bool bMaxAttributes);
	/** 0x1635B8 / 0x278840 (CharacterSelect activate / highlight of an unlocked rider, pad -1): the slot's config record
	 *  (slot+0x1C, 20 bytes) is loaded from the rider's P+0x10 record, i.e. its saved outfit (+0xC) and board (+0x10). */
	void LoadSlotConfig(int32 S, int32 Char);
	/** 0x1637F8 (CharacterAccept accept by the last player 0x27A1A8, race start 0x163980): for every rider, the config record of the active slot with the lowest pad riding it is copied into the rider's P+0x10 record when they differ (CRC 0x2CE04C), marking the save dirty (P+0x3258 = 1). Slots >= Players are inactive. True when one changed. */
	bool CommitSlotConfigs(int32 Players);
	// 0x161840 / 0x16BCA8 / 0x165400
	void ResetEventState();

	static constexpr int32 SettingsBlobSize = SSXProg::BlobSize;
	static constexpr uint32 SettingsMagic = SSXProg::BlobMagic;
	TArray<uint8> SaveBlob() const;
	// 0x2912E8 / 0x161A98
	bool LoadBlob(const TArray<uint8>& Blob);
	/** Boot auto-load (cStartScreenSingle vfunc_06 0x1845E0): a file whose auto-load word (blob +0x169C = P+0x16A4) is 0
	 *  is not loaded, only 0x32CA34 = 0 is kept; otherwise the checksum is verified and the settings loaded (LoadBlob).
	 *  True when the file was loaded. */
	bool BootLoadBlob(const TArray<uint8>& Blob);
};

struct SSXTRICKYREBUILD_API FSSXCheats
{
	bool bUnlockAll = false;      // 0x3352A0
	bool bMaxAttributes = false;  // 0x3352AC
	bool bMallora = false;        // 0x3352B0
	bool bMixMasterMike = false;  // 0x3352B8

	enum class ECode : uint8 { None, UnlockAll, MaxAttributes, Mallora, MixMasterMike };
	static ECode Match(const TArray<uint32>& Buttons);
	void Apply(ECode C);
};
