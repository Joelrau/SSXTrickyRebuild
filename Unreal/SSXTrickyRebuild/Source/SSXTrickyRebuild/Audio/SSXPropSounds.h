// 0x25EDF0 / 0x22A518 / 0x22A5F0 / 0x216C50
#pragma once

#include "CoreMinimal.h"

/** One ambient emitter shape: word 0 type (0 sphere, 1 ellipsoid, 2 cone, 3 2D sphere), word 1 sound id, words 2..4 offset from the instance position, the rest per type; float words as stored. */
struct SSXTRICKYREBUILD_API FSSXAmbientShape
{
	uint32 W[12] = {};
	int32 Type() const { return int32(W[0]); }
	int32 SoundId() const { return int32(W[1]); }
	float F(int32 K) const { float V; FMemory::Memcpy(&V, &W[K], 4); return V; }
	FVector3f Offset() const { return FVector3f(F(2), F(3), F(4)); }
	static int32 NumWords(int32 Type) { return Type == 0 ? 7 : (Type == 1 || Type == 2) ? 12 : Type == 3 ? 6 : 0; }
};

/** An attached .adl record with shapes, entered into the emitter grid by 0x22B008 (every one in .adl order, also records a later record overwrote at instance +0xF4). */
struct SSXTRICKYREBUILD_API FSSXAmbientRecord
{
	int32 Instance = INDEX_NONE;
	FVector3f Position = FVector3f::ZeroVector;   // instance +0x30 (game cm)
	TArray<FSSXAmbientShape> Shapes;
};

struct SSXTRICKYREBUILD_API FSSXPropSoundEntry
{
	int32 Instance = INDEX_NONE;
	uint32 HitId = 0;
	int32 NumShapes = 0;
	int32 ShapeIds[4] = { 0, 0, 0, 0 };
};

class SSXTRICKYREBUILD_API FSSXPropSounds
{
public:
	static FString DefaultPath(const FString& TrackStem);
	bool Load(const FString& Path, FString* OutError = nullptr);
	void Reset();

	const FSSXPropSoundEntry* Find(int32 Instance) const;

	/** 0x22D8A8: sound id -> bank slot (2 track bank, 3 Crowd.bnk, 6 swap bank) and sound index; false when the index is left unset (ids without a case). */
	static bool Resolve(uint32 Id, int32& OutBank, int32& OutIndex);
	// 0x22E388
	static bool IsSwapId(uint32 Id);
	/** Manifest id of a resolved sound ("sfx/<track bank>/NN", "sfx/Crowd/NN"); empty when unresolved or bank 6. */
	static FString ManifestId(uint32 Id, const FString& TrackBank);

	/** 0x22D508 head: the prop-sound owner changes to Instance; a new owner with a hit-activated shape id (0x10 / 0x1C / 0x39 among its first four shapes) enters the hit list (max 200, no duplicates). */
	void NotePropSoundOwner(int32 Instance);
	// 0x22E270
	bool IsInHitList(int32 Instance) const { return HitList.Contains(Instance); }

	/** Install this table as the board sounds' prop-hit lookup with the track's bank-2 name; clears the prop-sound ring. The table must outlive the binding. */
	void BindToBoardSounds(const FString& TrackBank);
	static void UnbindBoardSounds(const FSSXPropSounds* Only = nullptr);

	int32 NumRecords = 0;
	int32 NumAttached = 0;
	int32 NumInstances = 0;
	TArray<FSSXPropSoundEntry> Entries;
	TArray<int32> HitList;
	int32 LastOwner = INDEX_NONE;

	// 0x22E360 / 0x22E338
	void ResetHitList() { HitList.Reset(); LastOwner = INDEX_NONE; }

	/** World grid the emitter grid copies (0x22A6D8: world object 0x346488 = the .ltg, +4 / +8 min, +0x28 cell, +0x2C / +0x30 counts). */
	float GridMinX = 0.f, GridMinY = 0.f, GridCell = 0.f;
	int32 GridNx = 0, GridNy = 0;
	TArray<FSSXAmbientRecord> AmbientRecords;

private:
	TMap<int32, int32> ByInstance;
};
