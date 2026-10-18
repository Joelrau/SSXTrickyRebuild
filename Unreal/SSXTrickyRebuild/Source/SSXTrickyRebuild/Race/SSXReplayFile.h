// 0x1F0308, 0x1F0400, 0x2912E8, 0x166870, 0x180C70, 0x1668A0, 0x194910, 0x32DB6C, 0x2CE04C, 0x167060, 0x193F70, 0x194138, 0x193B00, 0x1938C8, 0x194750, 0x194820, 0x23FF18, 0x18F448, 0x38AAC0
#pragma once

#include "CoreMinimal.h"
#include "Race/IntroDialogue.h"
#include "Sim/Rider/RiderProfiles.h"

struct FRiderState;

class FSSXReplay;
struct FRiderProfile;
namespace SSXRider { struct FBuiltProfile; }

// One 0x80000-byte file buffer: [0, 0x920) the race-setup block (FSetup; CRC-16 of the block with the word zeroed at
// +0x334), then the replay object stream (header 0x44, camera tracks 0xE24 each, keyframes, clips, input and highlight
// recorders) zero-padded to 0x7F6D8; +0x7FFF8 the streamed byte count, +0x7FFFC 0xDEADBEEF + the sum of the signed stream bytes.
namespace SSXReplayFile
{
	constexpr int32 FileBytes = 0x80000;       // 0x1CE778
	constexpr int32 SetupBytes = 0x920;        // FUN_001D1D80
	constexpr int32 SetupCrcAt = 0x334;
	constexpr int32 StreamMax = 0x7F6D8;
	constexpr int32 LengthAt = 0x7FFF8;
	constexpr int32 ChecksumAt = 0x7FFFC;
	constexpr uint32 ChecksumSeed = 0xDEADBEEF;
	constexpr uint32 HeaderMagic = 0x12300003;
	constexpr int32 HeaderBytes = 0x44;
	constexpr int32 HeaderWritten = 0x30;
	constexpr uint32 StaleChunkWord = 0xDEADC0ED;
	constexpr uint32 KeyTag = 0x11111113, KeyTail = 0x11111111, InputTag = 0x11111112, HighlightTag = 0x11111114, ClipTag = 0x11111115;
	constexpr int32 TrackBytes = 0xE24;
	constexpr int32 RecorderWordBytes = 0x6664, RecorderRunBytes = 0x1999;   // 0x194550
	constexpr uint32 RecorderFill = 0xDEADC0DE;
	constexpr int32 MaxRacers = 6;
	constexpr int32 ProfileBytes = 0x84;

	struct SSXTRICKYREBUILD_API FRacer
	{
		int32 Slot = 0;   // +0x00 grid slot
		uint8 Name[10] = {};
		uint8 Stats[16] = {};   // +0x0E stat bytes 0..255
		uint8 Mass = 0;
		uint8 Pad1F = 0;
		uint8 Rival[MaxRacers][4] = {};   // +0x20 + 4 j: level, level max, anger limit, kind toward racer j (0x16C008)
		int32 Char38 = 0;   // +0x38 character index (0 Eddie .. 11 Marisol)
		int32 Char3C = 0;
		int32 Stance = 0;
		int32 BoardClass = 0;
		int32 BoardId = 0;   // +0x48 char * 12 + board (computer riders; 0 for a human)
		int32 OutfitId = 0;   // +0x4C char * 7 + outfit (computer riders; 0 for a human)
		int32 BodyId = 0, HeadTexId = 0, HeadModelId = 0;
		int32 TrickSet = 0;
		float Scale = 1.f;   // +0x60 rider size
		uint8 Byte64 = 0;
		int8 Port = -1;   // +0x65 controller port, < 0 = computer rider
		uint8 Byte66 = 0xFF, Byte67 = 0xFF;
		int32 Word68 = 0;
		int32 Word6C = 0;
		int32 Word70 = 0;
		                                 // 0x16CFD8, 0x16A9D8, 0x16AE68
		int32 Rank = 0;                  // 0x164840
		int32 Word78 = 0, Word7C = 0;    // 0x3352A4, 0x3352A8
		int32 Type80 = 2;   // +0x80 (0 = the MMM cheat rider)

		bool IsHuman() const { return Port >= 0; }
		int32 Board() const { return IsHuman() ? -1 : BoardId - Char38 * 12; }
		int32 Outfit() const { return IsHuman() ? -1 : OutfitId - Char38 * 7; }
		FString NameString() const;
	};

	struct SSXTRICKYREBUILD_API FSetup
	{
		int32 NumRacers = 0;
		FRacer Racers[MaxRacers];
		int32 Venue = 0;
		int32 GameMode = 0;
		uint32 Seed2B00 = 0;
		uint32 Seed2B04 = 0;
		int32 TimeToBeat = 0;
		int32 HeatWord = 0;   // +0x330 heat + 0x10000 * WC venue index
		uint32 Crc = 0;
		int32 Difficulty = 0;
		SSXIntroDialogue::FSlot Slots[SSXIntroDialogue::NumSlots];
		int32 Focus = 0;
		int32 Word91C = 0;

		int32 NumHumans() const;
		int32 RacerOfHuman(int32 P) const;
	};

	struct SSXTRICKYREBUILD_API FSnapshot
	{
		uint32 StreamB[6] = {};                    // 0x23D5B0, 0x345E70
		// 0x112790
		float AiValue14 = 0.f;
		int32 AiFrame = -1, AiPhase = -1, AiOldPhase = -1;
		TArray<FVector> Positions;
		TArray<FVector> Velocities;
		TArray<int32> Motions, Controls;
		TArray<int32> RecordAt;
	};
	SSXTRICKYREBUILD_API bool ParseSnapshot(const TArray<uint8>& Snap, FSnapshot& Out);
	// 0x18F6C0
	SSXTRICKYREBUILD_API void RestoreBoarderRecord(const uint8* Rec, ::FRiderState& S);
	struct FAiRecord
	{
		float Clock = 0.f;
		int32 Frame = 0, Phase = 0, OldPhase = 0;
	};
	SSXTRICKYREBUILD_API void PortSnapshot(const uint32 StreamB[6], TArray<uint8>& Out, const FAiRecord* Ai = nullptr);
	inline bool IsPortSnapshot(const TArray<uint8>& Snap) { return Snap.Num() == 0x18 || Snap.Num() == 0x30; }
	struct SSXTRICKYREBUILD_API FSnapshotCheck
	{
		int32 FileRiders = 0;
		TArray<double> Errors;
		double MaxError = -1.0;
		int32 WorstRacer = -1;
		bool bStreamBEqual = false;
		int64 StreamBCalls = 0;
	};
	SSXTRICKYREBUILD_API bool CheckSnapshot(const TArray<uint8>& Snap, TConstArrayView<FVector> PortPositions, const uint32 PortStreamB[6],
		FSnapshotCheck& Out);

	SSXTRICKYREBUILD_API SSXRider::FBuiltProfile RacerProfile(const FRacer& R);
	SSXTRICKYREBUILD_API FRacer MakeRacer(const FRiderProfile& P, int32 Slot, int32 Player, int32 Outfit, int32 PadLayout, const FString& Name,
		bool bMode6);

	inline float StartMeter(const FRacer& R) { float F; FMemory::Memcpy(&F, &R.Word70, 4); return F; }
	inline void SetStartMeter(FRacer& R, float F) { FMemory::Memcpy(&R.Word70, &F, 4); }
	SSXTRICKYREBUILD_API void EncodeSetup(FSetup& InOut, uint8* Out /* SetupBytes */);
	SSXTRICKYREBUILD_API void DecodeSetup(const uint8* In /* SetupBytes */, FSetup& Out);
	SSXTRICKYREBUILD_API bool SetupCrcValid(const uint8* In);

	SSXTRICKYREBUILD_API bool Write(const FSSXReplay& Replay, FSetup Setup, TArray<uint8>& Out);
	SSXTRICKYREBUILD_API bool Validate(const TArray<uint8>& In);
	SSXTRICKYREBUILD_API bool ReadSetup(const TArray<uint8>& In, FSetup& Out);
	SSXTRICKYREBUILD_API bool ReadReplay(const TArray<uint8>& In, FSSXReplay& Replay);
}
