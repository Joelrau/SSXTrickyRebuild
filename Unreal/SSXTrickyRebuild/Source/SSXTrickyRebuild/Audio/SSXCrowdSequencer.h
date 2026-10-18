// 0x2D3F00 / 0x3507F0 / 0x2D4440 / 0x2D4108
#pragma once

#include "CoreMinimal.h"

namespace SSXCrowdSeq
{
	/** Driver tick = one hardware block (step 0x2D4108): block length 0x2DBC10 follows the output rate (24000 -> 0x100, 36000 -> 0x180, else 0x200 samples), always 1/93.75 s; 384 samples = 10.667 ms at 36000 Hz. The game's counters treat a tick as 10 "ms" (0x21EA48 subtracts 10). */
	constexpr double TickMs = 1000.0 * 384.0 / 36000.0;
	constexpr int32 NumVoices = 24;     // 0x350EF0

	struct SSXTRICKYREBUILD_API FEvent
	{
		int32 Offset = 0;   // byte offset after "MIDx"
		uint8 Wait = 0;
		uint8 Status = 0;
		uint8 Data1 = 0;
		int32 Data2 = -1;   // -1 when the event has no second data byte
	};

	// A parsed .eam (MIDx) file; Data = the bytes after the 4-byte "MIDx" tag.
	struct SSXTRICKYREBUILD_API FSequence
	{
		FString Name;
		TArray<uint8> Data;
		TArray<FEvent> Events;
		bool Parse(TConstArrayView<uint8> File, FString* OutError = nullptr);
		int32 LengthTicks() const;
	};

	// 0x2D5410
	struct SSXTRICKYREBUILD_API FPatch
	{
		int32 VelLo = 0, VelHi = 0x7F, KeyLo = 0, KeyHi = 0x7F;
		int32 Priority = 0;
		int32 Root = 0x3C;
		int32 BendRange = 0;
		int32 Pan = 0x40;
		int32 PanRand = 0;
		int32 Volume = 0x7F;
		int32 VolRand = 0;
		int32 Fine = 0;
		int32 PitchRand = 0;
		bool bValid = false;
		void SetTag(int32 Tag, int32 Value);
	};

	// 0x2D6928
	struct SSXTRICKYREBUILD_API FLibRandom
	{
		uint32 W[6] = { 0xF22D0E56u, 0x883126E9u, 0xC624DD2Fu, 0x0702C49Cu, 0x9E353F7Du, 0x6FDF3B64u };
		uint32 Next();
		// 0x2DFF90
		int32 Spread(int32 Range);
	};

	// 0x2D59D8
	struct SSXTRICKYREBUILD_API FNote
	{
		int32 Cents = 0;
		int32 Volume127 = 0;
		int32 PanMidi = 0x40;    // 0x351998
		int32 BendRangeCents = 0;
	};
	/** Voice level law (0x2D5148): linear, trunc(velocity volume +0x48 x request volume +0x3A x envelope level +0x42 x library master +0x1D9 / 127^3); velocity curve and level table are null in RAM, the crowd envelope level is 127. The port applies the Options master through the SFX gain, hence Master 127 here. */
	FORCEINLINE int32 VoiceLevel(int32 VelVolume, int32 ReqVolume, int32 Env = 127, int32 Master = 127)
	{
		return (VelVolume * ReqVolume * Env * Master) / (127 * 127 * 127);
	}
	SSXTRICKYREBUILD_API bool ResolveNote(const FPatch& P, int32 Key, int32 Velocity, FLibRandom& Rng, FNote& Out);

	struct SSXTRICKYREBUILD_API FNoteOn
	{
		int32 Channel = 0;
		int32 Program = -1;      // bank patch (channel program byte, 0xFF = none)
		int32 Key = 0;
		int32 Velocity = 0;
		int32 Level = 0;
		int32 PanMidi = 0x40;    // 0x351998
		int32 Bend = 0x40;
	};

	// 0x3507F0
	class SSXTRICKYREBUILD_API FPlayer
	{
	public:
		TFunction<int32(const FNoteOn&)> NoteOn;
		TFunction<void(int32 Voice)> NoteOff;
		TFunction<bool(int32 Voice)> IsVoicePlaying;
		TFunction<void(int32 Voice, int32 Level)> SetVoiceLevel;
		// 0x2D4368 / 0x2D3260
		TFunction<void(int32 Voice)> StopVoice;

		// 0x2D4440
		void Start(const FSequence& Seq, int32 Volume, int32 Tempo = 100);
		// 0x2D4368
		void Stop();
		// 0x2D4108
		void Tick();
		// 0x2D3838
		void SetVolume(int32 Volume);
		bool IsPlaying() const { return State == 3; }
		int32 GetVolume() const { return Volume; }
		int32 TicksRun() const { return int32(Steps); }
		TArray<FString> EventLog;
		bool bLogEvents = false;

	private:
		void Step();
		void Dispatch(uint8 Status, uint8 D1);
		bool NoVoices() const;
		uint8 Byte(int32 At) const { return Seq && Seq->Data.IsValidIndex(At) ? Seq->Data[At] : 0; }

		struct FChannel { int32 Volume = 0x7F; int32 PanMidi = 0x40; int32 Bend = 0x40; int32 Program = 0xFF; };
		struct FVoiceRec { int32 Voice = -1; int32 Channel = 0; int32 Key = 0; int32 Velocity = -1; };

		const FSequence* Seq = nullptr;
		int32 Ptr = 0, LoopPtr = 0;
		uint8 LoopCount = 0xFF, Counter = 1, State = 0;
		uint32 Frame = 0, Steps = 0;
		int32 Tempo = 100, Volume = 0;
		FChannel Channels[16];
		FVoiceRec Voices[NumVoices];
		int32 NextVoice = 0;
	};
}
