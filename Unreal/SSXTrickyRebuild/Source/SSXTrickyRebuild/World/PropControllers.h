// TODO: );
#pragma once

#include "CoreMinimal.h"

namespace SSXGround { struct FPropContactEvent; }
class FRiderSim;

namespace SSXProps
{
	constexpr double FrameDt = 1.0 / 60.0;
	constexpr int32 TouchTimerFrames = 30;
	constexpr int32 FenceTimerFrames = 4;
	constexpr double FenceWobbleK = 0.001;
	constexpr int32 CrackedCooldownFrames = 30;
	constexpr double CrackedDamageK = 0.036;
	constexpr double TexFlipFlickerRate = 1.0 / 6.0;

	enum class EControllerType : int32
	{
		UVScroll = 10, TexFlip = 11, Fence = 12, Cracked = 14, UVScrollTexFlip = 19,
	};

	struct SSXTRICKYREBUILD_API FUVScroll
	{
		int32 Mode = 0;
		FVector2D Step = FVector2D::ZeroVector;
		double RunTime = 0.0;
		double PauseTime = 0.0;
		double Delay = 0.0;
		// 0x141FF0
		bool bActive = false;
		int32 DelayFrames = -1;
		int32 DoneCalls = 0;
		bool bRunning = true;
		float Clock = 0.f;
		FVector2D Offset = FVector2D::ZeroVector;

		void Tick();
	};

	/** TexFlip (node 11). The frame list is the .pbd texture flipbook
	 *  bound to the instance's model (frame count n, texture index per frame); r = stream A uniforms [0, 1). */
	struct SSXTRICKYREBUILD_API FTexFlip
	{
		int32 StartFrame = 0;
		int32 Direction = 0;
		double Fps = 0.0;
		double Delay = 0.0;
		int32 Mode = 0;
		TArray<int32> Textures;
		int32 FrameCount = 0;
		bool bActive = false;
		bool bRun = true;
		double Rate = 0.0;                        // frames advanced per 60 Hz frame (fps / 60)
		double BaseRate = 0.0;
		double Phase = 0.0;
		int32 Frame = 0;
		bool bRandomSteps = false;
		int32 DelayFrames = 0;
		int32 DoneCalls = 0;
		TFunction<double()> Rng;

		/** Create + bind (0x142E90 / 0x142AC8): rate = fps / 60, delay counter trunc(delay x 60), start frame < 0 ->
		 *  ceil(8 r), then SetFrame. InTextures = the flipbook (n = its length). */
		void Create(const TArray<int32>& InTextures);
		void SetFrame(int32 F);
		int32 TextureIndex() const { return Textures.IsValidIndex(Frame) ? Textures[Frame] : INDEX_NONE; }
		void Tick();
		double Random() const;
	};

	SSXTRICKYREBUILD_API bool ParseFlipbooks(const uint8* Section, int32 Size, int32 Count, TArray<TArray<int32>>& Out);
	SSXTRICKYREBUILD_API bool ParsePbdFlipbooks(const TArray<uint8>& Pbd, TArray<TArray<int32>>& Out);

	struct SSXTRICKYREBUILD_API FPropController
	{
		EControllerType Type = EControllerType::UVScroll;
		int32 Instance = INDEX_NONE;
		FUVScroll Scroll;
		FTexFlip Flip;
		int32 TouchTimer = TouchTimerFrames;
		double Gate38 = 0.0;                      // TODO: what writes it)
		double Wobble = 0.0;
		int32 FenceTimers[2] = { 0, 0 };
		double Health = 0.0;
		int32 Timer = -60;
		int32 Cooldown = 0;
		bool bDone = false;
		bool bBroken = false;

		bool HasScroll() const { return Type == EControllerType::UVScroll || Type == EControllerType::UVScrollTexFlip; }
		bool HasFlip() const { return Type == EControllerType::TexFlip || Type == EControllerType::UVScrollTexFlip; }

		void Tick();
		/** Slot 18 contact method. n = contact normal (after the Ground projection), VHat = unit(v), A = -n . v.
		 *  Returns true when the instance's slot-1 effect runs (scroll / flip nodes). */
		bool OnContact(const FVector& N, const FVector& VHat, double A);
	};

	// Parse node records ({u32 0, u32 size, u32 subtype, payload}, little endian).
	SSXTRICKYREBUILD_API bool ParseUVScrollRecord(const uint8* Record, int32 Size, FUVScroll& Out);
	SSXTRICKYREBUILD_API bool ParseTexFlipRecord(const uint8* Record, int32 Size, FTexFlip& Out);
	SSXTRICKYREBUILD_API bool ParseCrackedRecord(const uint8* Record, int32 Size, FPropController& Out);

	// 0x13BD40
	enum class EContactResult : uint8
	{
		NoController,
		RunSlot1,
		Handled,
	};

	namespace EventSlot
	{
		constexpr int32 CellActive = 0, Touch = 1, CellInactive = 2, ControllerDone = 3, CounterOrHealthOut = 4;
	}

	/** Event-slot dispatch: called with the instance and the event slot to run: 1 for every rider touch that reaches
	 *  the slot-1 effect (no controller, or a scroll / flip node whose debounce let it through; Contact set), 3 when a
	 *  node is done, 4 when a Cracked node's health runs out (Contact null). USSXTrackEffects binds it to the effect
	 *  interpreter (FEffectWorld::RunEffect of the instance's event slot). */
	using FEffectDispatch = TFunction<void(int32 Instance, int32 Slot, const SSXGround::FPropContactEvent* Contact)>;

	class SSXTRICKYREBUILD_API FPropControllers
	{
	public:
		FEffectDispatch RunEffect;
		/** When bound (USSXTrackEffects), every rider contact goes here instead: the interpreter's touch handler
		 * decides, including the forwarding of scroll / flip / fence nodes. */
		TFunction<void(FRiderSim&, const SSXGround::FPropContactEvent&)> EffectTouch;

		/** Create a node (the interpreter's type-0 record; the interpreter applies the replace / keep
		 *  rules before it calls this). An existing node on the instance is replaced. */
		int32 Add(const FPropController& C);
		bool Remove(int32 Instance);
		/** Controller slot 24 messages for the visual node: TexFlip 1 frame rate, 2 frame ceil(v)
		 *  (0 <= v < 9), 3 next frame (wrapping to 1), 4 run flag (v != 0). */
		void Message(int32 Instance, int32 Msg, double Value);
		void OnFenceContact(const SSXGround::FPropContactEvent& Event);
		/** Explicit start (message 1 "activate"): starts the instance's scroll / flip now. Returns false without one.
		 * The nodes also activate themselves when their start delay runs out. */
		bool Activate(int32 Instance);
		void Reset() { Nodes.Reset(); ByInstance.Reset(); }
		/** Replay keyframe (0x18F448: the UV scroll / TexFlip / Cracked nodes save themselves, vfunc 0x44): the nodes by
		 *  value; the bound hooks stay. */
		struct FState { TArray<FPropController> Nodes; TMap<int32, int32> ByInstance; };
		FState SaveState() const { return { Nodes, ByInstance }; }
		void LoadState(const FState& S) { Nodes = S.Nodes; ByInstance = S.ByInstance; }
		int32 Num() const { return Nodes.Num(); }
		const FPropController& Get(int32 I) const { return Nodes[I]; }
		int32 IndexOf(int32 Instance) const;
		const FPropController* Find(int32 Instance) const;

		void Tick();
		/** Touch handler 0x13BD40 for a rider contact: the instance's controller contact method, and the slot-1
		 *  effect through RunEffect when it applies. */
		EContactResult OnContact(const SSXGround::FPropContactEvent& Event, FRiderSim* Sim = nullptr);
		void Bind(FRiderSim& Sim);

	private:
		TArray<FPropController> Nodes;
		TMap<int32, int32> ByInstance;
	};
}
