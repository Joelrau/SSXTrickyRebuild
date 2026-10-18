#pragma once

#include "CoreMinimal.h"
#include "Sim/Anim/AnimController.h"
#include "Sim/Anim/RiderParts.h"
#include "Sim/Anim/RiderPose.h"

namespace SSXFeRiders
{
	constexpr int32 NumRiders = 12;
	// 0x266DF8
	constexpr int32 AnimInit = 0x2F9;
	constexpr int32 AnimTrans1 = 0x2FA;
	constexpr int32 AnimCycle = 0x2FE;
	enum EState : int32 { Idle = 0, Picked = 1, PickedLoop = 2, None = 3 };


	// 0x347200 / 0x3471F8
	struct SSXTRICKYREBUILD_API FSession
	{
		int32 Pair = 0;
		int32 Flip = 0;
		int32 OnFeLoad() { Pair ^= 1; return Pair; }
		int32 NextTrans() { const int32 K = Flip + 2 * Pair; Flip ^= 1; return K; }
		static FSession& Global();
	};

	struct SSXTRICKYREBUILD_API FRider
	{
		int32 Character = 0;
		int32 Slot = 0;
		int32 Current = None;
		int32 Requested = None;
		bool bLoaded = false;
		int32 Playing = -1;
		/** One FE frame. bClipDone = channel-0 clip at frame >= n - 1. Returns the id to start (play mode 5: from frame 0) or -1. */
		int32 Update(FSession& Session, bool bClipDone);
	};

	/** The light block of one rider (+0xC0..+0x14F). Colours / ambient keep the game's 4-float layout (lead float, then three values; TODO: channel order). */
	struct SSXTRICKYREBUILD_API FLightSet
	{
		float Ambient[4];
		float Azimuth[3];
		float Elevation[3];
		float Colour[3][4];
		FVector3f Direction[3];
		void UpdateDirections();
		// FUN_00267670
		static FLightSet Lerp(const FLightSet& Unsel, const FLightSet& Sel, float T);
	};

	/** Game-derived constants from SimData/fe_riders.json (git-ignored): the ring radius / z (|xy| / z of ssxfe.pbd mdl_Cplatform_01) and the two light sets (FUN_002673F8 / FUN_00267508 immediates). */
	struct SSXTRICKYREBUILD_API FData
	{
		double RingRadius = 0.0;
		double RingZ = 0.0;
		FLightSet Unselected;
		FLightSet Selected;
		bool bLoaded = false;
		static FString DefaultPath();
		bool Load(const FString& Path, FString* OutError = nullptr);
		static const FData* Shared();
	};

	struct SSXTRICKYREBUILD_API FLightBlend
	{
		float Blend = 1.f;
		bool bSelected = false;
		bool Step();
	};

	// FUN_00261B38
	inline bool IsSlotDrawn(int32 Slot, int32 Selected)
	{
		const int32 D = ((Slot - Selected) % NumRiders + NumRiders) % NumRiders;
		return FMath::Min(D, NumRiders - D) <= 2;
	}

	// 0x2663F8
	SSXTRICKYREBUILD_API SSXAnim::FRowMat RiderRoot(int32 Slot, double Radius, double Z);
	/** FE rider size: FUN_00266208 copies character table +0x24 into the rider descriptor +0x60 (Mac 0.75, Kaori 0.70, ...); a uniform scale in rider-local space before the ring matrix, root translation included. 1.0 when the rider table (SimData/riders.bin) is not loaded. */
	SSXTRICKYREBUILD_API double SizeScale(int32 Character);
	/** The size scale applied to a game-space palette matrix of a rider on Root: M' = M Root^-1 S Root, i.e. the bone
	 *  axes times s and the translation pulled toward the ring point (Root's origin) by s. */
	SSXTRICKYREBUILD_API SSXAnim::FRowMat ScaleAboutRoot(const SSXAnim::FRowMat& M, const SSXAnim::FRowMat& Root, double Scale);

	struct SSXTRICKYREBUILD_API FRing
	{
		FRider Riders[NumRiders];
		FLightBlend Lights[NumRiders];
		FSession* Session = nullptr;
		int32 SpeechVariant = 0;

		/** FE load (cFE3D::vfunc_05): characters by slot, toggles the TRANS pair, state 3; models load synchronously, so they are marked loaded and every rider is asked for state 0. */
		void Init(const int32 CharacterBySlot[NumRiders], FSession* InSession = nullptr);
		void RequestAll(int32 State);
		void Request(int32 Slot, int32 State);
		// FUN_00261B38
		void Select(int32 Slot);
		void TickLights();
		FSession& GetSession() { return Session ? *Session : FSession::Global(); }
	};

	class SSXTRICKYREBUILD_API FPoser
	{
	public:
		bool Init(const SSXAnim::FAflLibrary* InLib, const SSXAnim::FAflSkeleton* InBody, int32 Character);
		bool IsReady() const { return Lib && Body && Board; }

		/** One FE frame: state machine -> Play, controller update at speed 1 (0.5 anim frame per frame).
		 *  OutEvents gets the animation-event values whose frame the channel-0 clip passed this frame. */
		void Step(FRider& Rider, FSession& Session, TArray<int32>* OutEvents = nullptr);
		// FUN_0015FF40
		bool IsClipDone() const;

		void ComputePalette(const SSXAnim::FRowMat& Root, TArray<SSXAnim::FRowMat>& OutBody, SSXAnim::FRowMat& OutBoard) const;
		/** Head parts (eyes, goggles, hair ...) and face weights of the clips' part blocks, hung on Body (the body palette, scale included; Sim/Anim/RiderParts.h). Empty without part data. */
		void ComputeParts(TConstArrayView<SSXAnim::FRowMat> InBody, SSXAnim::FRiderPartsPose& Out) const;
		const SSXAnim::FAflRiderParts* GetParts() const { return Parts; }

		SSXAnim::FAnimController& GetController() { return Controller; }
		const SSXAnim::FAnimController& GetController() const { return Controller; }

	private:
		const SSXAnim::FAflLibrary* Lib = nullptr;
		const SSXAnim::FAflSkeleton* Body = nullptr;
		const SSXAnim::FAflSkeleton* Board = nullptr;
		const SSXAnim::FAflRiderParts* Parts = nullptr;
		SSXAnim::FAnimController Controller;
	};
}
