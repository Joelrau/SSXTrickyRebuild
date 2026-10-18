// TODO: whether the memory-card StartScreen already plays it;
#pragma once

#include "CoreMinimal.h"

class FRaceSession;
class FRiderSim;
class FSSXFrontEnd;
class FSSXOverlayManager;
class USSXAudioSubsystem;
struct FRaceCue;
struct FSSXSettings;
enum class ERacePhase : uint8;
namespace SSXAnim { class FAnimController; class FAflLibrary; }

namespace SSXGameAudio
{
	SSXTRICKYREBUILD_API bool WantsMenuMusic(const FSSXFrontEnd& FE);
	SSXTRICKYREBUILD_API void TickFrontEnd(USSXAudioSubsystem& Audio, FSSXFrontEnd& FE);
	/** Race load (FUN_002116D8): menu song off; track song picked, prepared and paused; venue intro queued. Track = map name, TrackId = game track id, GameMode = DAT_0032DE8C. */
	SSXTRICKYREBUILD_API void OnRaceSetup(USSXAudioSubsystem& Audio, const FString& Track, int32 TrackId, int32 GameMode, bool bLoadedReplay = false);
	// FUN_00211B58
	SSXTRICKYREBUILD_API void OnRaceRestart(USSXAudioSubsystem& Audio, const FString& Track, int32 TrackId, int32 GameMode);
	/** Race module teardown (0x17ECF8): ambient slots 0x22E338, 0x211960 (song destroyed, stream 2, sfx one-shots, 48 voice slots, speech players) and 0x211B18 (crowd, animation-event voice); no fade. RaceWorld's playing game sounds are stopped, UI sounds kept. Returns the number of world sound components stopped. */
	SSXTRICKYREBUILD_API int32 OnRaceUnload(USSXAudioSubsystem& Audio, UWorld* RaceWorld);
	SSXTRICKYREBUILD_API void RaceFrame(USSXAudioSubsystem& Audio, const FRaceSession& Session, int32 PlayerRacer, ERacePhase PrevPhase, bool bReplayPlaying = false);
	/** Countdown part of RaceFrame (0x113600): beeps, and GO (FUN_00215288) at clock 0. Every countdown phase ends in one GO, so each trick-tutorial attempt (Preview / Perform / Practice / restart) runs it again. GO does not stop speech channel 0 (0x2151C8 only clears its queue flag), so lesson narration posted in the same frame keeps playing. Frame = race-state frame (snd+0x7DDC). */
	SSXTRICKYREBUILD_API void CountdownFrame(USSXAudioSubsystem& Audio, ERacePhase Phase, ERacePhase PrevPhase, double Clock, int32 Frame, bool bReplayPlaying = false);
	/** Tricky cues of the focus player from RaceFrame: meter-full events, per-frame update, ms scheduler step, command execution. */
	SSXTRICKYREBUILD_API void TrickyFrame(USSXAudioSubsystem& Audio, const FRaceSession& Session, int32 PlayerRacer, bool bReplayPlaying = false);
	/** The same for a focus rider without a race session (free ride, mode 1); Sim null = not racing (only the scheduler advances). */
	SSXTRICKYREBUILD_API void TrickyFrame(USSXAudioSubsystem& Audio, const FRiderSim* Sim, int32 Mode);
	/** A race cue: line-path triggers of a human player -> Pathfinder events / breakdown zones. True if it
	 *  posted a music event. */
	SSXTRICKYREBUILD_API bool HandleRaceCue(USSXAudioSubsystem& Audio, const FRaceCue& Cue, bool bHumanRacer);
	/** Settings' sound state into the sound system (0x161A98): level classes 0 master, 1 music, 2 PA speech, 3 character speech, 4 SFX (0x211DB0), announcer gate from snd+0x383C (Volume3, 0x235D58), arcade flag snd+0x3844, speaker mode (a change rebuilds the sound system). Called every frame by ApplySettings. */
	SSXTRICKYREBUILD_API void ApplySettings(USSXAudioSubsystem& Audio, const FSSXSettings& S);
	SSXTRICKYREBUILD_API void TickRaceOverlays(USSXAudioSubsystem& Audio, FSSXOverlayManager& Overlays);

	SSXTRICKYREBUILD_API bool FeSpeech(USSXAudioSubsystem& Audio, int32 Id);
	SSXTRICKYREBUILD_API void HandleSpeechCue(USSXAudioSubsystem& Audio, const FRaceSession& Session, const FRaceCue& Cue);
	SSXTRICKYREBUILD_API void OnTrackBoostOp(USSXAudioSubsystem& Audio, int32 Op);
	/** Track script op 14 (points multiplier) for a human, any mode (0x11E660 -> 0x219D70): zbxsfx MultiplierSound(Value) at the SFX level; no replay or arcade-SFX gate. */
	SSXTRICKYREBUILD_API void OnTrackMultiplierOp(USSXAudioSubsystem& Audio, double Value);
	/** Rider message push of the focus player: trick name pushes 7 / 8 carry the announcement (0x157088: 0x232498 / 0x2333E0), then 0x233220. */
	SSXTRICKYREBUILD_API void OnRiderMessage(USSXAudioSubsystem& Audio, const FRiderSim& Sim, int32 Type, int32 Value);
	/** Rider remarks and announcer calls from per-racer counters: crash (0x218328 -> 0x235090), bump (0x235398), focus bail (0x2181E8 -> 0x233D38), empty-boost press (0x233E78), trick segment start (0x156618), grudge starts (0x2336B8). */
	SSXTRICKYREBUILD_API void RiderSpeechFrame(USSXAudioSubsystem& Audio, const FRaceSession& Session, int32 PlayerRacer);
	/** Rider animation events (0x15A030: an event fires when the channel's frame passes it, old <= e < new; 0x15F300 -> handler 0x235780): newest instance of each channel before / after one controller frame. */
	struct FAnimSnapshot
	{
		int32 Clip[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
		double Frame[3] = { 0.0, 0.0, 0.0 };
		int32 Count[3] = { 0, 0, 0 };
	};
	SSXTRICKYREBUILD_API FAnimSnapshot SnapshotAnim(const SSXAnim::FAnimController& C);
	SSXTRICKYREBUILD_API void CrossedAnimEvents(const FAnimSnapshot& Before, const FAnimSnapshot& After, const SSXAnim::FAflLibrary& Lib,
		TArray<int32>& OutValues);
	SSXTRICKYREBUILD_API void BindRiderSpeech(USSXAudioSubsystem& Audio);
	SSXTRICKYREBUILD_API void OnCameraOp(USSXAudioSubsystem& Audio, int32 Op, int32 Arg);
}
