// 0x2213F8 / 0x19A390 / 0x25AFF8
#pragma once

#include "CoreMinimal.h"

namespace SSXPropNodes
{
	constexpr double Frame = 1.0 / 60.0;
	constexpr double KeyRate = 30.0;   // keyframe times are frames / 30 (seconds)

	struct FKeySegment
	{
		double A = 0.0, B = 0.0, C = 0.0, D = 0.0, T0 = 0.0, T1 = 0.0;
		double Value(double T) const { return ((A * T + B) * T + C) * T + D; }
	};

	struct SSXTRICKYREBUILD_API FNodeAnim
	{
		double Base[6] = { 0, 0, 0, 0, 0, 0 };     // tx, ty, tz, rx, ry, rz (degrees)
		uint32 Mask = 0;
		TArray<FKeySegment> Tracks[6];

		/** Channel K at T (s): the cached segment if t0 <= t < t1, else the next, else the first with t < t1 (the last
		 *  if none); the polynomial is evaluated at the clamped end outside [t0, t1]. Untracked channels: the base. */
		double Channel(int32 K, double T, int32* Cache = nullptr) const;
		// cMeshAnimFrame (row vectors): a = -rx, b = -ry, g = -rz in degrees; Cache = int32[6] or null.
		FMatrix Matrix(double T, int32* Cache = nullptr) const;
	};

	struct FPropModelInfo
	{
		double EndFrame = 0.0;
		uint32 Flags = 0;
		int32 FirstObject = 0, NumObjects = 0;
		bool IsCrowd() const { return (Flags & 1) != 0; }
	};

	struct FPropObjectInfo
	{
		int32 Parent = INDEX_NONE;
		int32 Anim = INDEX_NONE;
		int32 Meshes = 0;
		FMatrix Matrix = FMatrix::Identity;
	};

	/** Flag / Fence strip source of one instance (propanim version 3): the four corners renderer
	 *  vfunc_86 (0x1E2948) reads from the first packet of the model's first mesh (positions and normals s16 / 32767, UVs
	 *  s16 / 4096; 0, 1 = top edge, 2, 3 = bottom edge), the model scale (header +0x18, raw), the instance's baked
	 *  lighting block (model space, PS2 colour units) and its fence neighbours (+0xC4 / +0xC8, flags +0xE8). */
	struct FStripSource
	{
		int32 Inst = INDEX_NONE;
		FVector Scale = FVector::OneVector;
		FVector Pos[4];
		FVector Nrm[4];
		FVector2D UV[4];
		FVector LightDir[3];
		FVector4 LightColour[3];
		FVector4 Ambient = FVector4(0, 0, 0, 128);
		int32 NeighbourC4 = INDEX_NONE, NeighbourC8 = INDEX_NONE;
		uint32 InstFlags = 0;
	};

	class SSXTRICKYREBUILD_API FPropAnimData
	{
	public:
		static FString DefaultPath(const FString& Track);
		bool Load(const FString& Path, FString* OutError = nullptr);
		bool LoadFromMemory(TConstArrayView<uint8> Bytes, FString* OutError = nullptr);
		bool IsLoaded() const { return bLoaded; }

		TArray<FPropModelInfo> Models;
		TArray<FPropObjectInfo> Objects;
		TArray<FNodeAnim> Anims;
		TArray<int32> InstanceModel;
		/** Version 2: the .pbd texture flipbooks (track texture index per frame) and, per model, its material
		 *  block (the instance's +0xF0 list) as {texture index (material +0x00), flipbook index (material +0x44, -1 =
		 * none)}. */
		TArray<TArray<int32>> Flipbooks;
		struct FMaterialRef { int32 Texture = -1; int32 Flipbook = -1; };
		TArray<TArray<FMaterialRef>> ModelMaterials;
		TMap<int32, FStripSource> Strips;

		const FPropModelInfo* ModelOfInstance(int32 Inst) const;
		const FStripSource* StripOf(int32 Inst) const { return Strips.Find(Inst); }
		/** The flipbook the TexFlip / Cracked binding takes its frame count from: the first entry of the instance's
		 *  material block with a flipbook (0x142AF0); nullptr when the model has none. */
		const TArray<int32>* FirstFlipbookOfInstance(int32 Inst) const;
		/** The texture shown by a material of the instance's model at a binding frame: flipbook[frame] of that material
		 *  when it has one (frame clamped to its last entry; a shorter flipbook than the first one is read past its end by the
		 *  original: its own out-of-range read, not reproduced), else INDEX_NONE (the mesh keeps its own texture). Keyed by the
		 *  material's base texture index (the run-time slot match). */
		int32 FlipTextureFor(int32 Inst, int32 BaseTexture, int32 FlipFrame) const;
		/** Every object's world matrix of an instance (row vectors): local = static matrix, or the keyframes at T when
		 *  bAnimated and the object has some; world = local x parent world, roots x InstMatrix. */
		void ObjectWorldMatrices(int32 Inst, const FMatrix& InstMatrix, double T, bool bAnimated, TArray<FMatrix>& Out) const;
		// 0x1CB658
		void ComboWorldMatrices(int32 Inst, const FMatrix& InstMatrix, double T, double SnapshotT, TArray<FMatrix>& Out) const;

	private:
		void WorldMatrices(int32 Inst, const FMatrix& InstMatrix, double T, bool bAnimated, const double* SnapshotT, TArray<FMatrix>& Out) const;
		bool bLoaded = false;
	};

	enum EAnimMode : int32 { AnimOnce = 0, AnimLoop = 1, AnimPingPong = 2 };

	enum EAnimKind : int32 { KindAnimObject = 0x100, KindAnimDelta = 0x101, KindAnimCombo = 0x102, KindAnimTexFlip = 0x103 };

	// AnimObject-family record (44 bytes, AnimCombo 60; payload word k at record +0x08 + 4k, word 0 = subtype).
	struct SSXTRICKYREBUILD_API FAnimObjectRecord
	{
		int32 Subtype = KindAnimObject;
		int32 Mode = 0;
		double Start = 0.0;   // word 2, frames
		double End = -1.0;   // word 3, frames (< 0: model end frame)
		double Speed = 30.0;   // word 4, keyframes per second
		double SpeedRand = 0.0;
		double Cooldown = 0.0;
		int32 RandomStart = 0;
		int32 Direction = 0;   // word 8: 4 = backwards
		// 0x199F20
		double ComboStart = -1.0;    // word 9, frames (< 0: word 3 as stored, quirk of the ctor)
		double ComboEnd = -1.0;
		double ComboSpeed = 0.0;
		int32 ComboHold = 0;
		static bool Parse(const uint8* Record, int32 Size, FAnimObjectRecord& Out);
	};

	/** AnimObject (0x100), AnimDelta (0x101) and AnimCombo (0x102) play state. AnimDelta (ctor 0x199D10, update 0x199DB8, message 0x199EB0): an AnimObject that starts stopped; message 2 adds v / 30 s to a budget (f32 +0x68); each update spends |rate| of it (clamped at 0), runs the AnimObject update with the previous frame's running flag and then sets running = budget > 0; a negative budget freezes the node (only the cooldown and delay count). AnimCombo (ctor 0x199F20, update 0x19A218, message 0x19A2D0, evaluate 0x19A478, compose 0x1CB658): message 3 (unless a combo runs or holds) snapshots the evaluated object matrices and plays [combo start, combo end] at the combo rate once, each animated object's matrix = keys(combo t) x snapshot; the AnimObject time stands still meanwhile. At the end it holds (word 12 != 0) or resumes the AnimObject play. */
	class SSXTRICKYREBUILD_API FAnimObject
	{
	public:
		// 0x23D4D8
		void Init(const FAnimObjectRecord& R, double ModelEndFrame, TFunctionRef<double(double, double)> RandRange, bool bCreatedByTouch);
		/** Init with the game's draws: the ctor 0x198810 takes uniform(start, end) 0x23D4F8 and mid + range x uniform(-1, 1)
		 *  0x23D560 from the gameplay stream B (FGameRng::StreamB), the in-race nodes and the headless replay re-run alike. */
		void InitGame(const FAnimObjectRecord& R, double ModelEndFrame, bool bCreatedByTouch);
		/** Slot 24: 1 = delay v x 60 frames, 4 = set t = v / 30 inside [start, end]; AnimDelta 2 = budget += v / 30;
		 *  AnimCombo 3 = start the combo (7 / 8, the hold bit, are the effect interpreter's). */
		void Message(int32 Msg, double V);
		/** One 60 Hz frame; true on the frame a play-once animation finishes (done(1)). bEvaluated: the node evaluated
		 *  its objects this frame (a finished AnimObject / AnimDelta, a budget-less AnimDelta and a held AnimCombo do not). */
		bool Tick();
		// 0x19A478
		bool ComboPose() const { return bCombo || (bComboHeld && bComboHeldPose); }

		int32 Kind = KindAnimObject;
		double T = 0.0, TStart = 0.0, TEnd = 0.0, Rate = 0.0;
		int32 Mode = AnimOnce;
		bool bRunning = true, bFinished = false;
		int32 Delay = 0, Cooldown = 0, CooldownFrames = 0;
		bool bEvaluated = false;
		double EvalT = 0.0;
		float Budget = 0.0f;
		double ComboT = 0.0, ComboStart = 0.0, ComboEnd = 0.0, ComboRate = 0.0, SnapshotT = 0.0;
		bool bCombo = false, bComboHeld = false;
		bool bComboHoldAtEnd = false, bComboHeldPose = false;

	private:
		bool TickObject();                         // 0x199058
		void TickCooldown();                       // 0x199030
		void Evaluated();
	};

	class SSXTRICKYREBUILD_API FCrowdBox
	{
	public:
		static constexpr int32 Slots = 16;
		static constexpr int32 Tick = 5;
		static constexpr double Range = 20000.0;            // cm: human riders that drive the state
		static const int32 Seq[4][16];
		static const int32 PauseChance[7];

		void Init(int32 InRows, int32 InCols, TFunctionRef<uint32()> Rand);
		/** Every race frame (acts on frames % 5 == 0): pre-race state 7, else 0, then (level + 4) & 7 for every human
		 *  rider within 20000 cm (Humans: distance, crowd-audio level). */
		void Update(int32 RaceFrame, bool bPreRace, TConstArrayView<TPair<double, int32>> Humans, TFunctionRef<uint32()> Rand);
		static int32 FrameOf(int32 Counter, int32 Anim, int32 Person);

		int32 Rows = 0, Cols = 0, State = 0;
		int32 Counter[Slots] = {}, Anim[Slots] = {}, Frames[Slots] = {};

	private:
		void Step(TFunctionRef<uint32()> Rand);
	};

	constexpr int32 StripCols = 8;

	constexpr int32 StripVerts = 2 * StripCols;
	constexpr double StripCornerXScale = 0.004;   // 0x144938 / 0x143FF8
	constexpr double StripModelScaleK = 250.0;

	inline FVector StripDrawScale(const FVector& ModelScale) { return FVector(FMath::Max(ModelScale.X, 1.0) * StripModelScaleK, ModelScale.Y, ModelScale.Z); }

	/** Flag (13) [setup 0x144938, update 0x144E48, deform 0x144EC8]: phase += Hz / 60 per frame (wraps), column k =
	 *  1..7 pushed along the strip's local X by A (k / 8) sin(-2 pi phase + 2 pi k / 8); dir 0: column k, dir 1: column
	 *  7 - k (column 7 stays, as compiled); timer seconds x 60 (0 = forever). */
	struct SSXTRICKYREBUILD_API FFlagStrip
	{
		int32 Direction = 0;
		double Rate = 0.0;   // cycles per frame
		double Amplitude = 0.0;
		double Phase = 0.0;
		int32 Timer = 0;
		// Record words dir, Hz, amplitude, seconds; ModelScale = model scale x; Rand01 = uniform [0, 1).
		void Init(int32 Dir, double Hz, double Amp, double Seconds, double ModelScale, TFunctionRef<double()> Rand01);
		bool Tick();
		void Offsets(double Out[StripCols]) const;
	};

	/** Fence (12) [ctor 0x143550 / neighbour ctor 0x143740, setup 0x143FF8, update 0x143B68, touch 0x144428, hit 0x1444D0,
	 *  neighbour 0x143928 / 0x143B08, deform 0x143CD0]: triangular swing that halves every half cycle; two 4-frame
	 * timers pass +-0.4 A to the neighbour panels. */
	struct SSXTRICKYREBUILD_API FFenceStrip
	{
		int32 Mode = 0;
		double Scale = 0.0;
		double Theta = 0.0, Amp = 0.0, Rate = 0.0;
		int32 Timer40 = 0;
		int32 Timer44 = 0;
		void Hit(double Amount);     // 0x1444D0
		// 0x144428
		void Touch(double Amount);
		// 0x143928
		double NeighbourAmount() const { return Amp * (Rate < 0.0 ? 0.4 : -0.4); }
		// 0x143B68
		bool Tick(TFunctionRef<void(int32 Side, double Amount)> Fire);
		void Offsets(double Out[StripCols]) const;
	};
	constexpr double FenceNeighbourMin = 0.25;

	/** The 16 strip vertices in the strip's local space (setup + deform): top row = lerp(corner 0, corner 1, k / 7),
	 *  bottom row = lerp(corner 2, corner 3, k / 7) for positions (corner x x 0.004) and UVs; Offsets[k] added to x of
	 *  the top row and, when bBothRows, the bottom row. Vertex 2k = top column k, 2k + 1 = bottom column k. */
	SSXTRICKYREBUILD_API void StripLocal(const FStripSource& S, const double Offsets[StripCols], bool bBothRows,
	                               FVector OutPos[StripVerts], FVector2D OutUV[StripVerts]);
	/** Draw matrix of the strip (Flag / Fence vfunc_03 0x145258 / 0x144578): the instance matrix rows 0..2 scaled by
	 *  StripDrawScale (row vectors: game = local x M). */
	SSXTRICKYREBUILD_API FMatrix StripDrawMatrix(const FStripSource& S, const FMatrix& InstMatrix);
	/** Strip vertex colour (cPS2GridMesh::vfunc_08 0x1E6E28 uploads the instance light block; the VU1 lighting as the
	 * props): min(ambient + sum colour_i max(0, n . dir_i), 255), truncated, all four channels. Every strip
	 *  vertex carries the same normal: Flag (1, 0, 0), Fence corner 0's normal. */
	SSXTRICKYREBUILD_API FColor StripColour(const FStripSource& S, const FVector& Normal);

	// Row-major 3x3 (rows as in the model).
	struct FMat3
	{
		double M[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		FVector MulVec(const FVector& V) const;
		FMat3 operator*(const FMat3& B) const;
		FMat3 Transposed() const;
		FMat3 Inverse() const;
		FMat3 Scaled(double S) const;
	};

	SSXTRICKYREBUILD_API FMat3 QuatToMatrix(const FQuat4d& Q);

	struct FContactRecord
	{
		FVector C = FVector::ZeroVector;
		FVector VHat = FVector::ZeroVector;
		FVector N = FVector::UpVector;
		double A = 0.0;
	};

	struct FBodyHit
	{
		double Depth = 0.0;
		FVector N = FVector::UpVector;
		FVector C = FVector::ZeroVector;
	};

	class SSXTRICKYREBUILD_API FRigidBody
	{
	public:
		static constexpr double InertiaScale = 0.075;
		static constexpr double OmegaMax = 23.561945;

		void Init(double Mass, const FMat3& InvInertiaBody, const FVector& ComOffset, const FVector& Pos, const FQuat4d& Q);
		void UpdateInertia();
		void Derive();
		void SetOmega(const FVector& W);
		void Integrate(const FVector& F, const FVector& T, double Dt = Frame);
		FVector PointVelocity(const FVector& C) const;
		void ApplyImpulse(const FVector& R, const FVector& J);
		void ContactImpulse(double E, double Mu, const FVector& C, const FVector& N);
		double K(const FVector& R, const FVector& D) const;

		double InvMass = 1.0;
		FMat3 InvIBody, InvI;
		FVector ComOffset = FVector::ZeroVector;
		FVector X = FVector::ZeroVector;
		FQuat4d Q = FQuat4d::Identity;
		FVector P = FVector::ZeroVector, L = FVector::ZeroVector, V = FVector::ZeroVector, W = FVector::ZeroVector;
	};

	struct SSXTRICKYREBUILD_API FRollerRecord
	{
		double Mass = 1.0;
		double W2 = 0.0, W3 = 0.0;
		FVector Velocity = FVector::ZeroVector;
		static bool Parse(const uint8* Record, int32 Size, FRollerRecord& Out);
	};

	class SSXTRICKYREBUILD_API FRoller
	{
	public:
		static constexpr double RestEnergy = 10000.0;
		static constexpr double RestTime = 10.0;
		static constexpr double GroundRestTime = 5.0;
		static constexpr double ContactDamp = 0.96666664;
		static constexpr double MinPush = 2.0;
		static constexpr double Friction = 0.5;
		static constexpr double Restitution = 0.0;
		static constexpr double SlowSpeed = 277.77777;
		static constexpr double KickUpMax = 600.0;
		static constexpr double SpinKick = 10.0;
		static constexpr double Gravity = 980.0;

		/** Kick: launch velocity, else v = 0.5 a v_hat + (0, 0, min(100 + 5000 / m, 600)),
		 *  w = 10 n. Contact may be null (then only the upward part, no spin). */
		void Init(const FRollerRecord& R, const FRigidBody& InBody, const FContactRecord* Contact);
		void Wake();
		/** One frame; true on the frame it comes to rest (done(1)). Query callbacks: the body's tree against the terrain
		 *  (surface query) and the props (instance query); return false for no contact. */
		bool Tick(TFunctionRef<bool(const FRigidBody&, FBodyHit&)> TerrainQuery, TFunctionRef<bool(const FRigidBody&, FBodyHit&)> PropQuery);

		FRigidBody Body;
		double Mass = 1.0;
		double T = 0.0, ContactTime = 0.0, Energy = 0.0;
		bool bAsleep = false;
	};

	/** Rider -> body impulse: n from the body to the rider; when v_n < 0 the body gets J n at r
	 *  with restitution 0.3; the rider nothing. Returns false when the contact separates. */
	SSXTRICKYREBUILD_API bool RiderHitsBody(FRigidBody& Body, const FVector& RiderV, double RiderMass, const FVector& C, const FVector& N, FVector* OutJ = nullptr);

	struct SSXTRICKYREBUILD_API FMeshAnimRecord
	{
		int32 Keep = 0;   // word 1: 1 = stay gone
		double Gravity = 0.0;
		double Seconds = 0.0;
		FVector Velocity = FVector::ZeroVector;
		FVector Spread = FVector::ZeroVector;
		double VelScale = 1.0;
		static bool Parse(const uint8* Record, int32 Size, FMeshAnimRecord& Out);
	};

	class SSXTRICKYREBUILD_API FMeshAnim
	{
	public:
		static constexpr int32 MaxPieces = 24;
		static constexpr double DebrisGravity = 16.333334;   // cm / frame^2 x record gravity

		struct FPiece
		{
			FVector Pos = FVector::ZeroVector;
			FQuat4d Q = FQuat4d::Identity;
			FVector Vel = FVector::ZeroVector;   // cm per frame
			FVector Axis = FVector::UpVector;
			double Spin = 0.0;   // rad per frame
		};
		enum class EResult : uint8 { None, Done, Stop };

		void Init(const FMeshAnimRecord& R, TConstArrayView<TPair<FVector, FQuat4d>> Start, const FMatrix& InstRot,
		          const FContactRecord* Contact, TFunctionRef<double()> Rand01, TFunctionRef<int32()> RandBit);
		EResult Tick();

		TArray<FPiece> Pieces;
		int32 Timer = 0, Keep = 0;
		double Gravity = 0.0;
		bool bActive = true;
	};

	class SSXTRICKYREBUILD_API FMovingObjects
	{
	public:
		static constexpr int32 Capacity = 256;
		enum class EKind : uint8 { Rider, Roller, AnimObject };
		struct FProxy { int32 Owner = INDEX_NONE; EKind Kind = EKind::Rider; FBox Box = FBox(ForceInit); bool bUsed = false; };

		int32 Add(int32 Owner, EKind Kind, const FBox& Box);
		void Update(int32 Id, const FBox& Box);
		void Remove(int32 Id);
		void Reset() { Proxies.Reset(); }
		void Pairs(int32 Id, TArray<int32>& Out) const;
		const FProxy& Get(int32 Id) const { return Proxies[Id]; }
		int32 Num() const;

	private:
		TArray<FProxy> Proxies;
	};
}
