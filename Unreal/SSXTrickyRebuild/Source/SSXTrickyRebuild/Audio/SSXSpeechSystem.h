// 0x343710
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXSpeech.h"

class SSXTRICKYREBUILD_API FSSXSpeechSystem
{
public:
	enum class ESet : uint8 { None, Race, FrontEnd };

	/** Race: MC, CHAR, NARR, ANIM. FE: FE only. SpeechDir = converted/audio/speech (each section's headers.big). */
	/** Seed: the engine RNG seed; the game passes the sound manager's vtable +0x24 value (0x21E6C0 = stream A next(), "SPCH_Init: rNum=%d", 0x228830), one draw per speech init. */
	bool Load(ESet Set, const FString& SpeechDir, FString* OutError = nullptr, uint32 Seed = 0);
	ESet GetSet() const { return Set; }
	bool IsLoaded() const { return Set != ESet::None; }

	/** Post, then service the request's channel. bQueue = the player's queue flag (back to back). Returns the lines handed to the player. */
	TArray<SSXSpeech::FLine> Request(const SSXSpeech::FRequest& R, bool bQueue = false);
	/** Post only (the MC wrappers 0x22E6A8.. call 0x2BBA80): the request waits for the per-frame update. Returns whether it took a slot (a chance roll can drop it). */
	bool Post(const SSXSpeech::FRequest& R);
	/** Per-frame update for one channel (0x228998 -> 0x228370, called when the channel is free): service its best pending request. bQueue = the channel's stream is still ending. */
	TArray<SSXSpeech::FLine> UpdateChannel(int32 Channel, bool bQueue);
	/** The same with the channel player's hold flag (+0x34C = -1, 0x235BA8): segments are returned, not handed to OnSegment, for the intro ring. */
	TArray<SSXSpeech::FLine> RequestHeld(const SSXSpeech::FRequest& R, TArray<SSXSpeech::FResolvedSample>& OutSegments);
	// 0x228D10
	void Stop(int32 Channel);

	// 0x227B00
	TFunction<void(int32 Channel, const SSXSpeech::FSampleGroup& Group, const SSXSpeech::FResolvedSample& Sample, bool bQueue)> OnSegment;
	TFunction<void(int32 Channel)> OnStop;

	SSXSpeech::FEngine Engine;
	struct FLog
	{
		SSXSpeech::FRequest Req;
		bool bPosted = false;
		int32 Lines = 0;
	};
	TArray<FLog> Log;

	/** chance[class][level] for the levels the game reaches (0, or 1 at 100 %): 1024 for every class except 8 and 16 (0). */
	static int32 ClassChance(int32 Class, int32 Level);
	static bool ClassGate(int32 Class, int32 Level, uint32 RandA) { return int32(RandA & 0x3FF) < ClassChance(Class, Level); }

	int32 LoadNarration(const FString& NascriptText, const FString& Track, int32 Language);
	/** Camera op 0x21: post every element of the script (queue flag set) unless channel 0 plays; returns the number posted. Break points push a marker into the 10-entry ring (WAIT_AUDIO, op 0x28). */
	int32 Narrate(int32 ScriptId, bool bChannel0Playing);
	void StopNarration();
	int32 Language = 0;

	// 0x343710
	bool LoadFeTable(const FString& JsonPath);
	bool FeRequest(int32 Id);
	static int32 RiderNameId(int32 Character);
	TArray<TPair<uint32, uint32>> FeTable;

private:
	ESet Set = ESet::None;
	TMap<int32, TArray<int32>> Scripts;
	int32 BreakMarkers = 0;
};
