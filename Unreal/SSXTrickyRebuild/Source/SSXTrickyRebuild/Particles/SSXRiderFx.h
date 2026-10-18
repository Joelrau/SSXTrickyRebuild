#pragma once

#include "CoreMinimal.h"
#include "Particles/SSXParticles.h"

// Plain C++ in GAME space (Z up, cm, s), one call per 60 Hz frame; read-only on the rider state.
struct FSurfaceParams;

namespace SSXParticles
{
	constexpr int32 MotionAir = 1, MotionGround = 2, MotionRail = 3;
	constexpr int32 SprayRing = 40;
	constexpr int32 CarveCloudRing = 64;
	constexpr double CarveCloudSpacing = 70.0, CarveCloudLife = 2.0, CarveCloudSizeMin = 80.0, CarveCloudSizeEnd = 400.0;
	constexpr int32 PuffRing = 30;
	constexpr double PuffLife = 2.0;
	constexpr int32 GhostRing = 7;
	constexpr int32 PowerFxPool = 250, PowerFxSlots = 12;

	SSXTRICKYREBUILD_API bool IsSparkSurface(int32 Surface);

	struct FRiderFxInput
	{
		int32 Motion = 0;
		int32 ContactKind = 3;   // +0x2E0 (2 = animated prop: no snow effects)
		int32 Surface = 1;
		double Steer = 0.0;
		bool bGoofy = false;
		double Brake = 0.0;
		FVector Vel = FVector::ZeroVector;
		FVector Contact = FVector::ZeroVector;
		FVector Fwd = FVector(0, 1, 0), Right = FVector(1, 0, 0), Up = FVector(0, 0, 1);
		// 0x129628
		// the board frame the boarder update reads: the PREVIOUS pose (cPostAI rebuilds it after the update)
		FVector BoardLong = FVector(0, 1, 0), BoardUp = FVector(0, 0, 1), BoardAcross = FVector(1, 0, 0);
		FVector BoardOrigin = FVector::ZeroVector;
		double Height = 0.0;
		double BoostStrength = 0.0;
		double BurstTimer = 0.0;
		bool bLaunchFlag = false;
		double LaunchGlow = 0.0;
		FVector4 LightTotal = FVector4(1, 0.5, 0.5, 0.5);
		FVector GroundNormal = FVector(0, 0, 1);
		double TrailLitA = 0.0, TrailLitB = 0.0;
		FVector Position = FVector::ZeroVector;
		FVector ContactForward = FVector(0, 1, 0);
		FVector ContactLeft = FVector(-1, 0, 0);
		int32 Control = 0;
		int32 BoardClass = 0;
		int32 ProfileStance = 0;
		double SizeScale = 1.0;
		double SurfWidth = 0.0, SurfTilt = 0.0, SurfSpeed = 0.0;
		bool bTrailBones = false;
		FVector TrailBone12 = FVector::ZeroVector, TrailBone8 = FVector::ZeroVector;
		// 0x136E38 / 0x1364D0
		// this pose's board frame, for the draw-time readers
		FVector LiveBoardLong = FVector(0, 1, 0), LiveBoardAcross = FVector(1, 0, 0), LiveBoardOrigin = FVector::ZeroVector;
	};

	struct SSXTRICKYREBUILD_API FSurfaceSpray
	{
		double Rate = 0.0, Size = 0.0, LifeLo = 0.0, LifeHi = 0.0;
		int32 Texture = 13;
		double Desat = 0.7, Brighten = 0.2, Alpha = 0.25;
		int32 Blend = BlendAdditive;

		static FSurfaceSpray FromParams(const FSurfaceParams& P);
		static FSurfaceSpray FromFields(TConstArrayView<double> F13To21);
	};

	SSXTRICKYREBUILD_API FVector SprayColour(const FVector4& LightTotal, double Desat, double Brighten);

	class SSXTRICKYREBUILD_API FSprayFx
	{
	public:
		int32 Frame(const FRiderFxInput& In, const FSurfaceSpray& Surf, TFunctionRef<double()> Rng);
		const TArray<FParticleBurst>& GetBursts() const { return Ring; }
		FVector4 GetFlash() const { return Flash; }
		static FParticleBurst MakeSprayBurst(int32 N, const FVector& Point, const FVector& A, const FVector& U, const FVector& L,
			const FVector& V0, double SpreadA, double SpreadU, double SpreadL, double SizeLo, double SizeHi, double LifeLo,
			double LifeHi, const FVector& Rgb, double Alpha, int32 Texture, int32 Blend, TFunctionRef<double()> Rng);
		void Reset() { *this = FSprayFx(); }

	private:
		void Push(FParticleBurst&& B);
		void Sparks(const FRiderFxInput& In, double Speed, TFunctionRef<double()> Rng);

		TArray<FParticleBurst> Ring;
		bool bWasAir = false;
		double AirRate = 70.0;
		FVector4 Flash = FVector4(0, 0, 0, 0);
		FVector PrevPoint = FVector::ZeroVector;
		bool bSparkActive = false;
	};

	struct FCarveCloud
	{
		double Age = 0.0;
		FVector Pos = FVector::ZeroVector, Up = FVector(0, 0, 1);
		int32 FlipU = 0, FlipV = 0, Variant = 0;
		double Intensity = 0.0;
		FVector Rgb = FVector::OneVector;
		double FadeIn = 0.2;
	};

	struct FCloudSprite { FVector Pos; double Size = 0.0; double Alpha = 0.0; int32 Texture = 24; };
	SSXTRICKYREBUILD_API bool CarveCloudSprite(const FCarveCloud& C, FCloudSprite& Out);

	class SSXTRICKYREBUILD_API FCarveClouds
	{
	public:
		void Frame(const FRiderFxInput& In, TFunctionRef<uint32()> RngBits);
		const TArray<FCarveCloud>& Get() const { return Clouds; }
		void Reset() { Clouds.Reset(); bHaveLast = false; }
		/** 0x12EC98 (the replay restore's FX clear 0x1128B8): count / head (+0x1C10 / +0x1C14) zeroed; the last spawn origin (FX +0) stays, so the spacing test goes on against it. */
		void ClearRing() { Clouds.Reset(); }

	private:
		TArray<FCarveCloud> Clouds;
		FVector LastOrigin = FVector::ZeroVector;
		bool bHaveLast = false;
	};

	SSXTRICKYREBUILD_API int32 LandingPuffCount(double Impact);
	SSXTRICKYREBUILD_API double LandingIntensityAdd(double Impact);

	SSXTRICKYREBUILD_API bool GhostTint(double BoostStrength, double BurstTimer, FVector4& OutTint);

	class SSXTRICKYREBUILD_API FAfterimages
	{
	public:
		bool Frame(int32 FrameNo, double BoostStrength, double BurstTimer);
		bool IsDrawn() const { return Count > 1; }
		int32 GetCount() const { return Count; }
		int32 GetHead() const { return Head; }
		FVector4 GetTint(int32 Slot) const { return Tints[Slot]; }
		void Reset() { *this = FAfterimages(); }

	private:
		int32 Head = 0, Count = 0;
		FVector4 Tints[GhostRing];
	};

	/** Boost ribbons (not rider copies): every FAfterimages push stores one sample of two arm segments; the draw sweeps them into two fading ribbons. Per arm the outer point = hand bone + 15 cm along normalize(hand - forearm), the inner point = the upper-arm bone; arms (pose +0x230 / +0x1F0 / +0x1B0) and (+0x330 / +0x2F0 / +0x2B0) = palette bones (8, 7, 6) and (12, 11, 10). */
	struct FRibbonSample
	{
		FVector Outer[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector Inner[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector4 Tint = FVector4(0, 0, 0, 0);
	};

	class SSXTRICKYREBUILD_API FBoostRibbons
	{
	public:
		static constexpr double Extend = 15.001;
		static const double Alpha[8];           // 0x33F398

		static FRibbonSample MakeSample(const FVector& Hand0, const FVector& Fore0, const FVector& Upper0,
		                                const FVector& Hand1, const FVector& Fore1, const FVector& Upper1, const FVector4& Tint);
		/** One frame: the ring rule of FAfterimages (every 3rd race frame); Sample is stored on a push (0x136148 -> 0x136C48: the previous pose's bones; the live sample is drawn from this pose). */
		void Frame(int32 FrameNo, double BoostStrength, double BurstTimer, const FRibbonSample& Sample);
		bool IsDrawn() const { return Ring.IsDrawn(); }
		int32 GetCount() const { return Ring.GetCount(); }
		const FRibbonSample& Get(int32 J) const { return Samples[(Ring.GetHead() - 1 - J + 2 * GhostRing) % GhostRing]; }
		/** Outer-edge alpha of strip entry K: 0 = the live sample, 1..Count = the ring newest first (the live sample and the newest ring sample share the first value); inner alpha = tint pad = 0. */
		static double OuterAlpha(int32 Count, int32 K);
		void Reset() { *this = FBoostRibbons(); }

	private:
		FAfterimages Ring;
		FRibbonSample Samples[GhostRing];
	};

	// U per cm across the trail (S = width x scale); negative = no trail on the surface.
	SSXTRICKYREBUILD_API double TrailSurfaceScale(int32 Surface);
	SSXTRICKYREBUILD_API bool TrailActive(int32 Surface, int32 Motion, int32 ContactKind);
	SSXTRICKYREBUILD_API void TrailSideWeights(double Side, double Steer, double& OutKA, double& OutKB);
	SSXTRICKYREBUILD_API double TrailSideStep(double Side, double LitA, double LitB, bool bActiveBefore);
	SSXTRICKYREBUILD_API bool TrailShouldPush(double Dist, double Speed, double DWidth, double CosDir, bool bActive);
	// 0x132648
	SSXTRICKYREBUILD_API FVector TrailAcross(const FVector& Vel, const FVector& Normal);
	/** The four trail edge points (sp+0..+0x30 in 0x132648): Ground = board corners P0..P3 (+0x5760..+0x5790); WipeOut / WipeOutRecover = (pose bone 12, pose bone 8, P0, P3); corners and bones of the previous pose. */
	SSXTRICKYREBUILD_API void TrailEdgePoints(const FRiderFxInput& In, FVector Out[4]);
	SSXTRICKYREBUILD_API double TrailPushWidth(const FVector Q[4], const FVector& D);
	/** Live-head half vector and width: of e1 = Q0 - Q1 and e2 = Q0 - Q2 the one more along D (e2 only when strictly),
	 *  width = its length, direction = it flattened onto the ground plane, turned to the +D side. */
	SSXTRICKYREBUILD_API FVector TrailLiveHalf(const FVector Q[4], const FVector& D, const FVector& Normal, double& OutWidth);

	/** The ground trail (0x132648, FX +0x8020): a live head (+0x00) plus a 100-entry FIFO (+0x40), header +0x1940
	 *  {head, count, run alpha, side}, +0x1950 live flag, +0x1960 last push contact, +0x1970 last push direction, +0x1980
	 *  last push width. */
	class SSXTRICKYREBUILD_API FGroundTrail
	{
	public:
		static constexpr int32 Entries = 100;
		static constexpr int32 FadeFrom = 90;
		static constexpr double PushDist = 110.141884;
		static constexpr double PushSpeed = 138.88889;
		static constexpr double PushDWidth = 10.0799465;
		static constexpr double PushCos = 0.99604887;
		static constexpr double Lift = 10.0;
		static constexpr double SideStep = 1.0 / 30.0;
		static constexpr int32 Texture = 23;

		/** Draw offset of every trail vertex (0x1333C0): v -= Lift x row 2 of the world->view matrix (renderer slot 31 0x1E9970), i.e. -Lift x (Right.z, Up.z, Forward.z) as a world vector (game space). Not the view axis used for the riders and not along the ray: it does not turn with the camera yaw. */
		static FVector DrawOffset(const FVector& CamRight, const FVector& CamUp, const FVector& CamForward)
		{
			return -Lift * FVector(CamRight.Z, CamUp.Z, CamForward.Z);
		}

		struct FEntry
		{
			FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
			double KA = 0.0, KB = 0.0, Alpha = 0.0, S = 0.0;
			bool bBreak = false;
		};

		void Frame(const FRiderFxInput& In, double LitA, double LitB);
		bool IsActive() const { return bActive; }
		// 0x1333C0
		int32 Num() const { return Count; }
		const FEntry& Get(int32 J) const { return J == 0 ? Live : Ring[(Head - J + 2 * Entries) % Entries]; }
		bool HasLiveHead() const { return bActive; }
		double GetRunAlpha() const { return RunAlpha; }
		void Reset() { *this = FGroundTrail(); }

	private:
		FEntry Ring[Entries];
		FEntry Live;
		int32 Head = 0, Count = 0;
		bool bActive = false;
		float RunAlpha = 0.f;
		double Side = 0.0;
		FVector LastPos = FVector::ZeroVector, LastDir = FVector::ZeroVector;
		double LastWidth = 0.0;
	};

	// One vertex of a trail draw pass: position (offset applied), ST (U, T), GS alpha byte.
	struct FTrailVertex
	{
		FVector Pos = FVector::ZeroVector;
		float U = 0.f, T = 0.f;
		int32 Alpha = 0;
	};

	/** One pass of the trail draw 0x1333C0 (pass 1 bBrighten = false: As = int(k alpha x -64), pass 2: int(k alpha x 128), negatives 0). The live head (while active) then count - 1 stored entries newest first, 2 vertices each (A, B), each + Offset. The stronger edge of a pair is the one with the larger clamped alpha of THIS pass (B on a tie): it gets U = 0, the other U = S. When the stronger side differs from the previous pair's (kept across a break) both pairs get alpha 0. T: head 0, then stored pairs 1, 0, 1, .. (0, 1, .. without a head). A break flag ends the strip after its pair; strips of fewer than 3 vertices are dropped. */
	SSXTRICKYREBUILD_API void TrailStrips(const FGroundTrail& Trail, bool bBrighten, const FVector& Offset, TArray<TArray<FTrailVertex>>& Out);
	SSXTRICKYREBUILD_API void TrailStrips(const FGroundTrail::FEntry* Head, TConstArrayView<FGroundTrail::FEntry> Stored, bool bBrighten, const FVector& Offset,
		TArray<TArray<FTrailVertex>>& Out);

	class SSXTRICKYREBUILD_API FPowerFxSlot
	{
	public:
		struct FPart { FVector Pos, Vel; int32 Frames = 0; double Size = 0.0, DSize = 0.0; FVector4 Argb, DArgb; };
		static FVector EmitPoint(const FVector& BoneA, const FVector& BoneB);
		int32 Frame(const FVector& Point, const FVector& RiderVelPerFrame, int32 PoolFree = PowerFxPool);
		const TArray<FPart>& GetParts() const { return Parts; }
		bool HasPoint() const { return bHaveCur; }
		FVector GetPoint() const { return Cur; }

		static constexpr double Rate = 0.4455;
		static constexpr int32 LifeFrames = 60;
		static constexpr double Size0 = 22.386;
		static constexpr double Size1 = 2.5005;
		static constexpr double Damping = 0.99035;
		static constexpr double DriftZ = -52.3157 / 60.0;
		static constexpr double EmitOffset = 3.068;
		static constexpr double GlowSize = 18.872;
		static constexpr double Follow = 0.14930999;
		static const FVector4 Colour0;
		static const FVector4 Colour1;

	private:
		double Acc = 0.0;
		TArray<FPart> Parts;
		FVector Cur = FVector::ZeroVector, Prev = FVector::ZeroVector;
		bool bHaveCur = false;
	};

	FORCEINLINE int32 ShadowTextureSize(int32 Racers) { return Racers == 1 ? 256 : 128; }
	/** k of 0x1364D0's (1, k, k, k): 0.3, lower as the board sinks up to 5 cm on the ground. Not a colour: 0x207F18 only tests k == 0.3f (ShadowBoardInSilhouette). */
	SSXTRICKYREBUILD_API double ShadowSilhouetteLevel(int32 Motion, double HeightAboveGround);
	/** 0x207F18 / slot 95: the board mesh group (+0x4A == 1, palette slot 19) is drawn into the silhouette only when k == 0.3f in float32, i.e. not on the ground below the surface (h < about -2.5e-7). */
	SSXTRICKYREBUILD_API bool ShadowBoardInSilhouette(int32 Motion, double HeightAboveGround);
	/** The silhouette texel: every vertex (38, 38, 38, 128), stored PSMCT16 (5 bits) and read back as 32; the patch pass subtracts Cs As >> 7 = 32 per channel at full cover. */
	constexpr double ShadowSubtract = 32.0 / 255.0;
	/** Light-view bounds (0x1C7E90): x / y min / max of (P - Eye) . (Ax, Ay) over the points, +-20 cm (a rectangle). The points are the world bone translations {4, 8, 12, 15, 18, 0} and the 4 board corners rider +0x5760 (BoardCorners: written by 0x129628, read at draw time after cPostAI). */
	SSXTRICKYREBUILD_API FBox2D ShadowExtents(const FVector& Eye, const FVector& Ax, const FVector& Ay, TConstArrayView<FVector> Points);
	constexpr double ShadowDepthAbove = 100.0, ShadowDepthBelow = 3000.0;

	FORCEINLINE double FlakeHalfSize(double A, double B, double U) { return (2.0 * A - B) + (B - A) * (1.0 + U); }

	class SSXTRICKYREBUILD_API FSnowfall
	{
	public:
		static constexpr double Cube = 350.0;
		static constexpr double Wrap = 700.0;
		static constexpr double FallSpeed = 300.0;
		static constexpr double DrawRadius = 2450.0;
		static constexpr double FadeStart = 2100.0;
		static constexpr double Alpha = 24.0;
		static constexpr double Grey = 128.0;
		static constexpr int32 Texture = 19;
		static constexpr int32 Slots = 8;

		/** Per race: intensity I = 0.05 + 0.15 x, a = 5 + r1, b = 7 - r2 (three uniform 0..1 draws). Rng (stream A in the game) gives four VU seeds per slot (0x1C8EC8); flake positions and sizes are the VU RNEXT steps of those seeds (program 7). */
		void Init(double X, double R1, double R2, TFunctionRef<double()> Rng);
		void Frame() { Offset = FMath::Fmod(Offset + FallSpeed * Frame_(), Wrap); }
		static int32 FlakesPerSlot(double I) { return int32(I * Cube * Cube * Cube / 1.0e6); }
		void Gather(const FVector& Camera, TArray<FSprite>& Out) const;
		double GetIntensity() const { return Intensity; }

	private:
		static constexpr double Frame_() { return 1.0 / 60.0; }
		struct FFlake { FVector P; double Half; };
		TArray<FFlake> Slot[Slots];
		double Intensity = 0.0, A = 5.0, B = 7.0, Offset = 0.0;
		bool bInit = false;
	};
}
