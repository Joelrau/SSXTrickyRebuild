// TODO: here and in the .cpp: - frame order: the scene groups run 2 (cAIWorld: cells), 3 (script
#pragma once

#include "CoreMinimal.h"
#include "Effects/SplinePath.h"

class FRiderSim;

namespace SSXEffects
{
	constexpr int32 NumSlots = 7;
	constexpr int32 SlotActivate = 0, SlotTouch = 1, SlotDeactivate = 2, SlotDone = 3, SlotExpired = 4;

	constexpr uint16 FlDrawn = 0x1, FlDisplay = 0x2, FlNodeDrawn = 0x4, FlCollidable = 0x20, FlReacts = 0x80;
	constexpr uint16 FlCellActive = 0x100, FlHeld = 0x800;

	constexpr int32 TypeRoller = 0, TypeDebounce = 2, TypeState = 5, TypeCounter = 6, TypeBoost = 7, TypeTimer = 8;
	constexpr int32 TypeUVScroll = 10, TypeTexFlip = 11, TypeFence = 12, TypeFlag = 13, TypeCracked = 14;
	constexpr int32 TypeCrowdBox = 17, TypeUVScrollTexFlip = 19, TypeMeshAnim = 20, TypeTrickTrigger = 21, TypeMovie = 23;
	constexpr int32 TypeLapBoost = 15, TypeRandomBoost = 16, TypeZBoost = 18, TypeTubeEndBoost = 24;
	constexpr int32 TypeAnimObject = 0x100, TypeAnimTexFlip = 0x103;
	constexpr int32 TypeDead = 5;
	constexpr int32 TypeRestore = 0x3EE;

	constexpr int32 TouchForwardFrames = 30;
	constexpr int32 CrackedCooldownFrames = 30;
	constexpr double CrackDamage = 0.036;
	constexpr double KmhToCms = 27.777777;
	constexpr double TeleportSpacing = 300.0;
	constexpr double ShowOffPickupReward = 0.0397949;
	constexpr int32 CellSlots = 108;
	constexpr int32 CellRadius = 2;
	constexpr int32 HumanCollideEmitters = 3, AICollideEmitters = 1;
	// 0x31D338
	constexpr int32 BoostMaxRacers = 6;
	constexpr float BoostFrameGain = 0.016666668f;    // 0x140838
	constexpr float LapBoostSideRise = 1000.0f;       // 0x140B90
	constexpr float LapBoostCatchRadius = 1000.0f;
	constexpr float LapBoostPullXY = 20.0f;   // first pull frame: p += 20 unit(d_xy) + (0, 0, 10)
	constexpr float LapBoostPullZ = 10.0f;
	constexpr float LapBoostDamp = 0.92f;   // vx, vy every pulling frame
	constexpr float LapBoostWipeDamp = 0.95f;
	constexpr float TubeEndUnset = 1e10f;

	SSXTRICKYREBUILD_API bool IsShowOffMode(int32 GameMode);
	SSXTRICKYREBUILD_API const TCHAR* ModeScript(int32 GameMode);
	// FUN_00112848
	SSXTRICKYREBUILD_API bool HasCountdown(int32 GameMode, int32 Track);
	enum class EFlowPhase : uint8 { PreRace, StartRace, CountdownEnter, CountdownEnd };
	SSXTRICKYREBUILD_API TArray<FString> FlowScripts(EFlowPhase Phase, int32 GameMode, int32 Track);
	SSXTRICKYREBUILD_API int32 TrackIdForStem(const FString& Stem);
	/** Snowfall enable: mode not 2 / 4, at most 1 human, fewer than 3 racers, and the stream-A draw below the track's snow chance. */
	SSXTRICKYREBUILD_API bool SnowfallEnabled(int32 GameMode, int32 Humans, int32 Racers, double Draw, double SnowChance);
	// 0x17E148
	SSXTRICKYREBUILD_API bool SnowfallNeedsDraw(int32 GameMode, int32 Humans, int32 Racers);

	// One script record: {u32 type, u32 size, payload} at Offset in the blob.
	struct FRecord
	{
		int32 Type = 0;
		int32 Size = 0;
		int32 Offset = 0;
	};

	class SSXTRICKYREBUILD_API FEffectData
	{
	public:
		struct FFunction { int32 First = 0; int32 Count = 0; FString Name; };
		struct FCell { int32 NumNormal = -1; int32 FirstNormal = 0; int32 NumAlt = -1; int32 FirstAlt = 0; };

		bool Load(const FString& Path, FString* OutError = nullptr);
		bool LoadFromMemory(TConstArrayView<uint8> Bytes, FString* OutError = nullptr);
		static FString DefaultPath(const FString& Track);
		bool IsLoaded() const { return bLoaded; }

		int32 TrackId = -1;
		TArray<int32> InstEvent;   // per instance: event record (-1 none)
		TArray<uint32> InstFlags;   // per instance: .ssf flag word (high half = initial)
		TArray<FMatrix> InstMatrix;   // per instance: .pbd matrix (row vectors)
		TArray<int32> Events;
		TArray<FIntPoint> Effects;
		TArray<FFunction> Functions;
		TArray<FRecord> Records;
		TArray<uint32> Splines;
		int32 Nx = 0, Ny = 0;
		double MinX = 0.0, MinY = 0.0, CellSize = 0.0;
		TArray<FCell> Cells;
		TArray<int32> CellRefs;
		TArray<uint8> Blob;

		int32 NumInstances() const { return InstEvent.Num(); }
		uint16 InitialFlags(int32 Inst) const { return uint16((InstFlags[Inst] >> 16) & 0xFFFF); }
		int32 EffectFor(int32 Inst, int32 Slot) const;
		int32 FunctionIndex(const FString& Name) const;
		int32 I(const FRecord& R, int32 K) const;
		float F(const FRecord& R, int32 K) const;
		int16 S16(const FRecord& R, int32 ByteOff) const;
		const uint8* RecordBytes(const FRecord& R) const { return Blob.GetData() + R.Offset; }
		static int32 ExpectedSize(int32 Type, int32 Subtype);
		/** Validates the exported tables: record sizes, references in range, effect and function bodies tiling the blob. Empty = OK. */
		TArray<FString> Validate() const;

	private:
		bool bLoaded = false;
	};

	struct SSXTRICKYREBUILD_API FEffectRider
	{
		FRiderSim* Sim = nullptr;
		int32 Slot = 0;
		bool bHuman = false;
		int32 Motion = 2;
		double Speed = 0.0;                // |v| cm/s
		double BurstTimer = 0.0;
		double LaunchTimer = 0.0;
		double PointsMult = 1.0;
		bool bResetRequested = false;
		int32 LaunchFlag = 0;              // TODO: no reader in the port)
		int32 CollideEmitters = 0;
		int32 Cell = -1;                   // 0x11D438
		int32 PrevCell = -1;               // 0x114730
		int32 LapsLeft = 0;

		int32 GetMotion() const;
		double GetSpeed() const;
	};

	enum class EOutcome : uint8
	{
		Node,
		NodeRestart,
		NodeDestroyed,
		Hide,
		Message,
		Emitter,
		EmitterDied,
		Sound,
		Rail,
		Camera,
		CameraShake,
		Reset,
		BoostBurst,
		LaunchBoost,
		BoostSound,       // FUN_002340B8
		LaunchSound,      // FUN_00234308
		Multiplier,
		MultiplierSound,  // 0x219D70
		ShowOffPoints,
		MeterReward,
		AddClock,
		TimeSound,        // 0x11E8A0 / 0x219FC0
		Teleport,
		FenceWobble,
		RollerStop,       // 0x149E18
	};

	struct FOutcome
	{
		EOutcome Kind = EOutcome::Node;
		int32 Instance = INDEX_NONE;
		FEffectRider* Rider = nullptr;
		int32 A = 0;
		int32 B = 0;
		int32 Extra = 0;
		double V = 0.0;
		int32 Record = INDEX_NONE;
		FVector P = FVector::ZeroVector;
	};

	/** Boost-family node state (cBoostNode / cLapBoostNode / cZBoostNode / cTubeEndBoostNode offsets; single precision as in the game). */
	struct FBoostState
	{
		int32 Mode = 0;                    // Boost / TubeEnd +0x60: record s16 +0x0C (1 in every retail record)
		int32 Frames = 0;
		int32 Active = 0;
		FVector3f Dir = FVector3f::ZeroVector;
		float Target = 0.f;   // m/s x 100
		float Gain = 0.f;   // 1/s
		float ZTop = 0.f;
		float ZRange = 0.f;
		int16 LapCopy[BoostMaxRacers] = {};
		int16 LapFlag[BoostMaxRacers] = {};
		uint8 Flag[BoostMaxRacers] = {};
		FVector3f Exit[3];
		float ExitTarget[3] = {};   // m/s x 100
		FVector3f ExitDir[BoostMaxRacers];
	};

	struct FController
	{
		int32 Type = 0;
		int32 Timer = -1;
		int32 Count = 0;
		int32 TouchCooldown = 0;
		int32 TouchCooldownReset = 30;
		bool bRollerLive = false;
		double Health = 0.0;
		uint32 Mask = 0;
		int32 Record = INDEX_NONE;
		FBoostState Boost;
	};

	struct FBoostQueries
	{
		/** Moving-object pair (sweep-and-prune): the rider's proxy AABB (0x236758) overlaps the instance AABB (+0xCC / +0xD8) on all three axes. */
		TFunction<bool(int32 Inst, const FEffectRider& Rider)> Pairs;
		/** 0x13AAD0 -> 0x25BF48: the rider's sphere set (0x257810, current mask) against the instance's own collision model;
		 *  true when the result t is > 0. */
		TFunction<bool(int32 Inst, const FEffectRider& Rider)> Inside;
		TFunction<bool(int32 Inst, FBox& Out)> Bounds;
	};

	struct FScript
	{
		int32 First = 0;
		int32 Count = 0;
		int32 Inst = INDEX_NONE;
		FEffectRider* Rider = nullptr;
		int32 Function = INDEX_NONE;
		int32 Index = 0;
		float Delay = 0.f;
		bool bAlive = true;
	};

	struct FEmitter
	{
		int32 Inst = INDEX_NONE;
		FEffectRider* Rider = nullptr;
		int32 Kind = 0;
		int32 Record = INDEX_NONE;
		bool bContinuous = false;
		double Life = 0.0;
		int32 Serial = 0;
		FSplinePathState Path;
	};

	/** Replay keyframe of the effect state (FEffectWorld::SaveKeyframe): script / controller / emitter nodes plus the instance flag and rail words the original's keyframe saves. */
	struct FEffectKeyframe
	{
		TArray<uint16> Flags;
		TArray<uint8> RailEnabled;
		TArray<TPair<int32, FController>> Controllers;
		TArray<FScript> Scripts;
		TArray<FEmitter> Emitters;
		TArray<int32> TubeEndSide;
	};

	class SSXTRICKYREBUILD_API FEffectWorld
	{
	public:
		void Init(const FEffectData* InData, int32 InGameMode, int32 InTrack);
		/** Race restart (0x180A10 -> 0x115DC0 -> 0x115FE8): every instance's controller node destroyed (NodeDestroyed; emitter nodes end with EmitterDied), its active flag half reset to initial half | display (cell bit cleared); running scripts end; rail bits back to their initial halves. */
		void RestartReset();
		/** Front-end load (0x25EDF0 with -1): every instance of the .ltg cells' +0x28 lists gets 0x102 in its initial half and initial | 0x102 | 2 as its active half (cell-active, no slot-0 effect, no tracker). */
		void LoadFrontEndFlags();
		const FEffectData* GetData() const { return Data; }
		int32 GetGameMode() const { return GameMode; }
		int32 GetTrack() const { return Track; }

		TFunction<double()> Rng;
		TFunction<void(const FOutcome&)> OnOutcome;
		TFunction<void(int32 Inst, uint16 Old, uint16 New)> OnFlagsChanged;
		bool bCollideEmitterQuota = true;
		/** The creating touch damages a Cracked node; the reference-model comparisons turn this off. */
		bool bCrackedCreateDamage = true;
		bool bKeepLog = false;
		TArray<FOutcome> Log;

		TArray<FEffectRider*> Racers;
		FBoostQueries BoostQueries;
		TFunction<int32(const FEffectRider&)> LapsLeftOf;
		/** DAT_0031D338: per racer slot, the TubeEndBoost exit side (0 / 1 / 2) written by the first LapBoost contact. A game global: zero at load, never reset by a race restart. */
		int32 TubeEndSide[BoostMaxRacers] = {};

		void RunEffect(int32 Effect, int32 Inst, FEffectRider* Rider);
		bool CallFunction(const FString& Name);
		void CallFunctionIndex(int32 K, FEffectRider* Rider);
		void RunFlow(EFlowPhase Phase);

		/** Rider contact with an unsurfaced collidable prop (touch handler 0x13BD40). bImpact: (VN, A) = (v_hat . n, a)
		 *  of the contact record, used by Cracked. */
		void Touch(int32 Inst, FEffectRider& Rider, bool bImpact = false, double VN = 0.0, double A = 0.0);
		void Activate(int32 Inst);
		void Deactivate(int32 Inst);
		void MessageInstance(int32 Inst, int32 Msg, float Value);
		void Frame();

		/** done(1) of a node whose behaviour lives outside the interpreter (AnimObject play-once end, Roller rest, MeshAnim time up, Flag timer, Fence settled): the instance's slot-3 effect, else Roller slot 13 (asleep: RollerStop) or restore the flags and destroy the node. */
		void NodeDone(int32 Inst);
		/** Roller slot 12 / 13 / 14 state (node +0x140 = asleep): asleep Rollers pass touches to the slot-1 effect, whose factory call restarts the node (NodeRestart). */
		void SetRollerAsleep(int32 Inst, bool bAsleep);
		/** Instance flag changes a node constructor makes (Roller: clear 0x22, set 0x45; animation / MeshAnim / Flag / Fence / CrowdBox: clear 0x2, set 0x4); kept out of Factory. */
		void ApplyNodeFlags(int32 Inst, uint16 Clear, uint16 Set);
		/** A node made by game code rather than an effect (the Fence neighbour node, 0x143928 -> 0x143740: base ctor 0x13A840, type 12, the originating node's record). Only on an instance without a node; no outcome emitted. Returns false when the instance already has one. */
		bool CreateCodeNode(int32 Inst, int32 Type, int32 Record);

		uint16 GetFlags(int32 Inst) const { return Flags.IsValidIndex(Inst) ? Flags[Inst] : 0; }
		const FController* FindController(int32 Inst) const;
		void GetControllers(TArray<TPair<int32, const FController*>>& Out) const;
		int32 NumControllers() const { return CtlOrder.Num(); }
		int32 NumScripts() const { return Scripts.Num(); }
		int32 NumEmitters() const { return Emitters.Num(); }
		bool IsRailEnabled(int32 Spline) const { return RailEnabled.IsValidIndex(Spline) && RailEnabled[Spline] != 0; }
		int32 NumRails() const { return RailEnabled.Num(); }
		const TArray<FEmitter>& GetEmitters() const { return Emitters; }
		/** The track's .pbd spline segments and (first segment, count) per spline for the SplinePath nodes; without them a SplinePath node lives but poses nothing. */
		void SetSplines(TArray<FSplinePathSegment>&& InSegments, TArray<FIntPoint>&& InSplines)
		{
			PathSegments = MoveTemp(InSegments);
			PathSplines = MoveTemp(InSplines);
		}
		TConstArrayView<FSplinePathSegment> GetPathSegments() const { return PathSegments; }
		/** SplinePath end mode 3 with instance flags & 0x80 (0x1FAAD0): position of the lowest-ranked racer (+0x110, first in racer-list order on ties). Unset: the chase is skipped. */
		TFunction<bool(FVector3f& OutPos)> LeaderPosition;
		bool IsScriptHidden(int32 Inst) const;
		/** Replay keyframe: save / put back the node and flag state (no outcome, no flag callback: the owner re-applies the flags and rails). */
		void SaveKeyframe(FEffectKeyframe& Out) const;
		void LoadKeyframe(const FEffectKeyframe& In);

	private:
		bool UpdateScript(FScript& S);
		int32 Exec(FScript& S, const FRecord& R);
		int32 Condition(const FScript& S, const FRecord& R);
		void Factory(const FScript& S, const FRecord& R, int32 RecIndex);
		void SetState(int32 Inst, int32 K);
		void RestoreFlags(int32 Inst, bool bKeepCellBit);
		void Destroy(int32 Inst);
		void Message(int32 Inst, FController& C, int32 Msg, float Value);
		void CounterMessage(FController& C, int32 Msg, float Value);
		void SetEnabled(int32 Inst, FController& C, int32 On);
		void DeactivateController(int32 Inst, FController& C);
		void ControllerFrame(int32 Inst, FController& C);
		void BoostFrame(int32 Inst, FController& C);      // 0x140688
		void LapBoostFrame(int32 Inst, FController& C);   // 0x140B90
		void ZBoostFrame(int32 Inst, FController& C);     // 0x141CD8
		void TubeEndLaw(FController& C, FEffectRider& R); // 0x1418A8
		int32 LapsLeft(const FEffectRider& R) const { return LapsLeftOf ? LapsLeftOf(R) : R.LapsLeft; }
		bool RunSlot(int32 Inst, int32 Slot, FEffectRider* Rider);
		void Emitter(const FScript& S, const FRecord& R, int32 RecIndex);
		void SetRail(int32 Spline, int32 Enable);
		void Teleport(FEffectRider& Rider, int32 Inst);
		void Out(const FOutcome& O);
		void SetFlags(int32 Inst, uint16 New);
		void StartScript(const TSharedPtr<FScript>& S);
		TSharedPtr<FController> AddController(int32 Inst, const FController& C);
		void RemoveController(int32 Inst);
		FController* Ctl(int32 Inst) const;

		const FEffectData* Data = nullptr;
		int32 GameMode = 0;
		int32 Track = -1;
		bool bShowOff = false;
		TArray<uint16> Flags;
		TArray<uint16> Initial;
		TArray<uint8> RailEnabled;
		TMap<int32, TSharedPtr<FController>> Controllers;
		TArray<TPair<int32, TSharedPtr<FController>>> CtlOrder;
		TArray<TSharedPtr<FScript>> Scripts;
		TArray<FEmitter> Emitters;
		int32 NextEmitterSerial = 1;
		TArray<FSplinePathSegment> PathSegments;
		TArray<FIntPoint> PathSplines;
	};

	/** Per-frame active-cell set. cAIWorld keeps 108 fixed slots {cell, mark count, new flag} (0x1162C0 / 0x1160F8 / 0x116420 / 0x1162F8): each update drops every occupied slot's count by one, marks the 3x3 block around each point in order (a cell already in a slot gets count 1, a new one takes the lowest free slot with count 1 and the new flag), then walks the slots in order: a new cell activates its instances (slot 0), an unmarked one (count < 1) deactivates them (slot 2) and frees its slot. */
	class SSXTRICKYREBUILD_API FCellTracker
	{
	public:
		void Init(const FEffectData* InData, bool bShowOffMode);
		void Step(FEffectWorld& World, TConstArrayView<FVector2D> Points);
		/** cAIWorld::vfunc_02 (0x115E28) for the racers: every racer's +0x16C first (0x11D438), then walk the racers while +0x42C == +0x16C: racer 0 changed -> update without the camera point; racer 0 unchanged before StartRace (race state +0x1C < 2) -> update with the camera point; from StartRace on skipped when no racer changed cell. The caller copies +0x42C = +0x16C after the riders (cPostAI). */
		void StepRacers(FEffectWorld& World, TConstArrayView<FEffectRider*> Riders, bool bBeforeStartRace, const FVector* Camera);
		int32 NumActive() const;
		// 0x1160F8
		void CellsAround(double X, double Y, TArray<int32>& Out) const;
		/** A racer's cell marker (rider +0x16C, 0x11D438): the cell index of (x, y) one step down-left of the block
		 *  corner, clamped to the grid. -1 without a grid. */
		int32 CellOf(double X, double Y) const;

	private:
		const FEffectData* Data = nullptr;
		bool bShowOff = false;
		struct FSlot { int32 Cell = -1; int16 Count = 0; bool bNew = false; };
		TArray<FSlot> Slots;
		void Mark(int32 Cell);
		void ForInstances(int32 Cell, TFunctionRef<void(int32)> Fn) const;
	};
}
