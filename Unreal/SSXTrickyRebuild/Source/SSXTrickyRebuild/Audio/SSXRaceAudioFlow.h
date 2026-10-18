// 0x21E6C0
#pragma once

#include "CoreMinimal.h"

struct FSSXInfFile;

namespace SSXRaceAudio
{
	using FRand15 = TFunctionRef<int32()>;

	constexpr int32 EndSegment = 16;
	constexpr int32 SongStartControl = 80;     // FUN_00215220
	constexpr double ZoneReturnDelay = 1000.0; // TODO: probably ms

	SSXTRICKYREBUILD_API TArray<FString> SongList(const FSSXInfFile& MusicInf);
	SSXTRICKYREBUILD_API uint32 MusicMapMask(const FSSXInfFile& MusicMap, const TArray<FString>& Songs, const FString& TrackName);
	SSXTRICKYREBUILD_API int32 NextAllowed(int32 Index, uint32 Mask, int32 NumSongs);
	SSXTRICKYREBUILD_API int32 PickSong(uint32 Mask, int32 NumSongs, FRand15 Rand15);
	SSXTRICKYREBUILD_API TArray<int32> InitialIntroQueue(int32 Track, bool bRestart, FRand15 Rand15);
	SSXTRICKYREBUILD_API bool IsSequentialTrack(int32 Track);
	SSXTRICKYREBUILD_API int32 Intensity(int32 Racers, int32 Rank, double Boost, int32 Track);   // 0..99, -1 = not updated (one racer on track 10); Rank 1-based
	SSXTRICKYREBUILD_API int32 TriggerEvent(int32 InternalType, int32 LapsLeft);

	struct SSXTRICKYREBUILD_API FMusicZones
	{
		int32 Zone = 0;
		int32 ExitZone = 0;
		int32 InEnter = 0;
		int32 InExit = 0;
		struct FPending { double TimeLeft; int32 Kind; };
		TArray<FPending> Pending;
		TArray<int32> Posted;

		static bool IsZoneKind(int32 Kind) { return Kind >= 2 && Kind <= 7; }
		void Enter(int32 Kind);
		void Leave(int32 Kind, double Delay = ZoneReturnDelay);
		void Tick(double Dt);
		void Reset();
	};

	struct FCommand
	{
		// FUN_00215220
		enum EKind : uint8 { Beep, QueueIntro, ClearIntro, StopIntro, UnpauseSong, PostEvent, SetControl, PrepareSong };
		EKind Kind = Beep;
		int32 Value = 0;
	};

	class SSXTRICKYREBUILD_API FFlow
	{
	public:
		/** Race load (FUN_002116D8). bTutorial = mode 6 (song never paused, control = rand % 100, state 6). IntroDuck = AUDIO.INF INTRODUCK / 100. bRestart = restart (FUN_00211B58): state 5 without intro, except circuit modes 4/5 (bCircuit): state 2 and one B segment. */
		void Load(int32 InTrack, bool bRestart, bool bCircuit, bool bTutorial, double IntroDuck, FRand15 Rand15);
		/** Race load of a loaded replay (0x2116D8, flag +0x4C): duck mode 4, audio state 6 only; no song pick, intro queue or random draws. */
		void LoadReplay(int32 InTrack);
		// FUN_0020EE58
		void Frame(int32 IntroQueueLen, int32 Racers, int32 PlayerRank, double PlayerBoost, FRand15 Rand15);
		void Countdown(double Clock, FRand15 Rand15);
		// FUN_00214E08
		void IntroEnd();
		// FUN_00215288
		void Go(FRand15 Rand15);
		/** Same helper in replay playback (0x21BFD0, overlay start 0x1EF990(1)): duck mode 4 at 1.0, audio state 6, stream 2 stopped, 0x215220 posts song event 0 with control 80 and the song is unpaused (0x225260); 0x215220 rebuilds the song player so the race song restarts from event 0. */
		void ReplayGo();
		/** A human player's line-path trigger: acts on the rising edge of the trigger mask only, so a repeated enter of a set bit is dropped and a leave clears it. */
		void Trigger(int32 InternalType, bool bEnter, int32 LapsLeft);
		uint32 TriggerMask = 0;
		void TickZones(double Dt);
		/** A human's forced reset (0x218EB0 -> 0x21C4D0): an active breakdown zone posts its return event 2 x zone and is cleared (snd+0x6E50 / +0x6E54 / +0x3704 = 0). */
		void ForcedReset();

		TArray<FCommand> Commands;
		int32 State = 0;
		int32 DuckMode = 4;   // 4 = none
		double DuckLevel = 1.0;
		int32 Control = SongStartControl;
		bool bIntroQueued = false;
		bool bGo = false;
		bool bEvent10Armed = true;
		int32 Track = 0;
		FMusicZones Zones;

	private:
		void Emit(FCommand::EKind K, int32 V = 0) { Commands.Add({ K, V }); }
		void FlushZones();
		int32 SeqCounter = 0;
		double IntroDuckLevel = 0.75;
		bool bTutorialMode = false;
	};
}
