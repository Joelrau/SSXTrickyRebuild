#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/SSXAmbientEmitters.h"
#include "Audio/SSXPropSounds.h"
#include "Effects/TrackEffects.h"
#include "Race/RaceRules.h"
#include "SSXTrackEffects.generated.h"

class FPatchWorld;
class FRiderSim;
class AActor;
namespace SSXGround { struct FPropContactEvent; }

UCLASS()
class SSXTRICKYREBUILD_API USSXTrackEffects : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static USSXTrackEffects* Get(const UObject* WorldContext);

	/** Load the track's effect tables and start a fresh effect state for the game mode. False (idle) without SimData/<track>.effects.bin. bFrontEndLoad = the FE world load (0x25EDF0 with track -1): skips the AI lines and the .adl, so no prop sound lists or ambient emitters. */
	bool BeginTrack(const FString& Track, int32 TrackId, int32 GameMode, const TSharedPtr<FPatchWorld>& SimWorld, bool bFrontEndLoad = false);
	bool IsActive() const { return bActive; }
	/** Race restart (0x180A10): cell slots freed (0x115DC0 -> 0x115DF0, no deactivation), playing track sounds stopped (0x22E338, 0.25 s fade), replay reset to idle (0x18EBD0, state 10) so the StartRace enter resets the stage again. */
	void RestartTrack();
	/** Trick Tutorial reset FUN_0014D7D0: nodes of scene groups 3 / 0xD / 4 deleted (0x17DA50), cell slots freed with the instance reset (0x115DC0 -> 0x115DF0 / 0x115FE8); done at once (no StartRace). */
	void LessonReset() { if (bActive) { ResetStage(); } }
	void AddRider(FRiderSim* Sim, bool bHuman, int32 Slot);
	void OnRaceFrame(ERacePhase Prev, ERacePhase Now, double Clock);
	void RunFlow(SSXEffects::EFlowPhase Phase);
	/** First thing in a race frame: the replay update (0x191298). Replay state 0xB (set by StartRace enter, 0x113478) resets the stage: all script / controller / emitter nodes deleted, instance flags reset (0x115FE8, also sets every racer's +0x42C to -1), cell slots freed (0x115DF0), prop nodes and debris removed. */
	void BeginFrame();
	/** Cell update (cAIWorld 0x115E28) around the registered racers, in racer order. RequestedPhase = race state +0x1C; before StartRace the view-0 camera position (may be null) is marked too, but only when racer 0 has not changed cell. From StartRace on it is skipped when no racer changed cell (+0x42C == +0x16C for all). */
	void StepCells(ERacePhase RequestedPhase = ERacePhase::Race, const FVector* Camera = nullptr);
	void StepFrame();
	// 0x11A840 / 0x116AB8
	void OnRacersPlaced();
	// 0x114730
	void PostRiders();
	/** Replay keyframe of the effect state: the interpreter's nodes, instance flags and rails, cell slots and the racers' cell markers. */
	struct FReplayKeyframe
	{
		SSXEffects::FEffectKeyframe World;
		SSXEffects::FCellTracker Cells;
		TArray<SSXEffects::FEffectRider> Riders;
		bool bCountdownEnded = false;
		bool bPreRace = false;
		bool bValid = false;
	};
	void SaveReplay(FReplayKeyframe& Out) const;
	/** Put the keyframe back: current nodes end (NodeDestroyed), saved ones return without outcomes, the sim world's flags and rails follow; prop nodes and track emitters are cleared here and restored by the race's replay world from their own keyframes. */
	void LoadReplay(const FReplayKeyframe& In);
	int32 NumStageResets() const { return StageResets; }
	void ApplyRender();
	void RollSnowfall(int32 Humans, int32 Racers);
	int32 NumActiveCells() const { return Cells.NumActive(); }

	/** Rider touch of an instance outside PropCollision (the Roller wake routine 0x13F448 re-runs the slot-1 effect of an asleep body). */
	void TouchInstance(FRiderSim& Sim, int32 Inst);
	void MarkDrawDirty(int32 Inst) { DirtyDraw.Add(Inst); }

	const FSSXPropSounds& GetPropSounds() const { return PropSounds; }
	virtual void Deinitialize() override;

	/** Ambient emitters, once per game frame after the riders: deferred-call clock, query around the local listener (0x1166F0 -> 0x22B370; Listener = view-0 camera position, game cm; null = no query), slot update (0x22D370), then voice volumes (int)(vol x 127) with the 30 m falloff (0x20AAB8) from Ear (0x20E730: camera + 100 cm forward - 25 cm up, game cm) for 3D slots. */
	void StepAmbient(const FVector* Listener, const FVector& Ear);
	// 0x22D370 / 0x1EBFC0
	void ExtraAmbientPass() { if (Ambient.IsBuilt()) { Ambient.Update(); } }
	// 0x22E400
	void SetAmbientEnabled(bool bOn, bool bDeferred) { Ambient.SetEnabled(bOn, bDeferred); }
	FSSXAmbientEmitters& GetAmbient() { return Ambient; }
	/** The sound manager's stop-all (0x2213C8) kills the ambient voices but not the slots: they stay active with dead handles (no restart until a query drops and re-registers them). */
	void KillAmbientVoices() { StopAmbientVoices(); }
	int32 NumPlayingAmbientVoices() const;
	/** Race module teardown (0x22E338): every ambient slot stopped, hit list reset, voices stopped. */
	void StopAudio();

	SSXEffects::FEffectWorld& GetEffectWorld() { return World; }
	const SSXEffects::FEffectData& GetData() const { return Data; }

	/** Laps left of a racer slot (rider +0x114, read by the LapBoost node); set by the race manager after BeginTrack; unset (free ride): 0. */
	TFunction<int32(int32 Slot)> LapsLeftOf;

private:
	void OnOutcome(const SSXEffects::FOutcome& O);
	void Touch(FRiderSim& Sim, const SSXGround::FPropContactEvent& E);
	SSXEffects::FEffectRider* RiderFor(FRiderSim* Sim);
	void PlayTrackSound(int32 Inst, int32 Id, bool b2D, int32 RaceFrame);
	void BuildActorMap();
	// 0x191298
	void ResetStage();

	SSXEffects::FEffectData Data;
	SSXEffects::FEffectWorld World;
	SSXEffects::FCellTracker Cells;
	TArray<TUniquePtr<SSXEffects::FEffectRider>> Riders;
	TWeakPtr<FPatchWorld> SimWorld;
	FString TrackStem;
	FString SoundBank;
	FSSXPropSounds PropSounds;
	FSSXAmbientEmitters Ambient;
	struct FAmbientVoice
	{
		TWeakObjectPtr<class UAudioComponent> Comp;
		bool b3D = false;
		FVector3f Position = FVector3f::ZeroVector;
		int32 Volume127 = 0;
	};
	TMap<int32, FAmbientVoice> AmbientVoices;
	int32 NextAmbientHandle = 1;
	UPROPERTY(Transient)
	TObjectPtr<class USoundAttenuation> AmbientSpatial;
	void BindAmbientVoices();
	void StopAmbientVoices();
	bool bActive = false;
	bool bCountdownEnded = false;
	bool bSnowfall = false;
	bool bShowOffMode = false;
	bool bActorsBuilt = false;
	bool bPreRace = false;
	bool bStageResetPending = false;
	int32 StageResets = 0;
	int32 GameModeId = 0;
	/** Contact record of the touch being handled and of the last one: a MeshAnim created later by a slot-4 effect (glass break) reads it. TODO: which contact record the slot-4 MeshAnim reads is not stated; the last rider contact is used. */
	bool bInTouch = false;
	bool bHaveContact = false;
	struct FContactRec { FVector C, VHat, N; double A = 0.0; } LastContact;
	TMap<int32, TArray<TWeakObjectPtr<AActor>>> ActorsByInstance;
	TSet<int32> DirtyDraw;
	/** SplinePath nodes (op 2 kind 1): per node serial, one mesh component per (copy, model object) and the objects' matrices relative to the instance. */
	struct FSplinePathDraw
	{
		TArray<TWeakObjectPtr<class UStaticMeshComponent>> Comps;
		TArray<FMatrix> ObjectLocal;
		TWeakObjectPtr<class USSXSplineLineComponent> Line;
	};
	TMap<int32, FSplinePathDraw> SplinePathDraws;
	/** Instances whose display bits a SplinePath ctor cleared (0x6): their own actors stay hidden while the active flag
	 *  half lacks 0x2. */
	TSet<int32> SplinePathInstances;
	UPROPERTY(Transient)
	TObjectPtr<AActor> SplinePathActor;
	void SyncSplinePaths();
	void BuildSplinePathDraw(const SSXEffects::FEmitter& E, FSplinePathDraw& Out);
	void ClearSplinePaths();
	// 0x22E338
	TArray<TWeakObjectPtr<class UAudioComponent>> TrackSounds;
public:
	int32 NumPlayingTrackSounds() const;
	TArray<class USSXSplineLineComponent*> GetSplineLineComponents() const;
};
