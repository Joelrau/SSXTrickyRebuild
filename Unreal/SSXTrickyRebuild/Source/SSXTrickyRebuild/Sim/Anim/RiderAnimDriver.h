// 0x15FCF8
#pragma once

#include "CoreMinimal.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/AnimController.h"
#include "Sim/Anim/RiderPose.h"
#include "Sim/Anim/RiderParts.h"
#include "Sim/RiderSim.h"

class FRiderSim;
struct FRiderState;

namespace SSXAnim
{
	struct FRiderAnimChannel;

	// 0x1050E8, 0x1055F0, 0x35FED0
	SSXTRICKYREBUILD_API int32 TakeoffRow(double PrewindA, double PrewindB, bool bGoofy);
	SSXTRICKYREBUILD_API int32 TakeoffClip(int32 Row, int32 TakeoffType);
	// 0x102158
	SSXTRICKYREBUILD_API int32 DirectionalTrickId(int32 SpinIn, int32 FlipIn, bool bGoofy);
	// 0x10F9A0
	SSXTRICKYREBUILD_API int32 TumbleId(double UpN, double SideN, double FwdN, int32 CurrentCategory);
	// 0x10D274
	inline int32 QuickRecoveryId(int32 CurrentCategory) { return CurrentCategory == 17 ? 0x250 : 0x251; }
	// 0x106DC8
	SSXTRICKYREBUILD_API int32 RailStartId(int32 TakeoffType, int32 BoardClass, bool bProfileStance);
	// 0x1049F8
	SSXTRICKYREBUILD_API int32 LandArmsId(const FVector& Velocity, const FVector& Up, const FVector& Forward, const FVector& Side, bool bGoofy);
	SSXTRICKYREBUILD_API uint32 OverlayMask(int32 AnimId, int32 Shift);
	// 0x11A840
	inline int32 SitIdleId(int32 Character) { return (Character == 2 || Character == 11) ? 0x2F4 : 0x2F0; }

	struct FRiderPalette
	{
		TArray<FRowMat> Body;
		FRowMat Board;
		FRiderPartsPose Parts;
		bool bValid = false;
	};

	class SSXTRICKYREBUILD_API FRiderAnimDriver
	{
	public:
		bool Init(const FAflLibrary* InLib, const FString& RiderId, int32 BoardClass, int32 Character);
		bool IsReady() const { return bReady; }
		const FAflSkeleton* GetSkeleton() const { return Set.Body; }
		const FAflRiderParts* GetParts() const { return Parts; }
		const FAflLibrary* GetLibrary() const { return Lib; }

		void Step(FRiderSim& Sim);

		void ComputePalette(const FRiderSim& Sim, FRiderPalette& Out) const;

		// 0x114730, 0x129628, 0x126748
		void FeedPose(FRiderSim& Sim, bool bPosedSpheres) const;

		FAnimController& GetController() { return Controller; }
		const FAnimController& GetController() const { return Controller; }

		void SaveEventsForReplay(const FRiderSim& Sim);
		void RestoreEventsForReplay(const FRiderSim& Sim);

		static FAnimDriveInputs DriveInputs(const FRiderState& S);
		static FRiderFrameInput FrameInputs(const FRiderState& S, double Scale);

	private:
		void OnControl(const FRiderSim& Sim, uint8 From, uint8 To, const TCHAR* Reason);
		void Play(int32 AnimId, double Speed = -1.0);
		int32 CurrentCategory() const;
		int32 TumbleAnim(const FRiderState& S) const;

		void StepHairIdle(const FRiderSim& Sim);

		const FAflLibrary* Lib = nullptr;
		const FAflRiderParts* Parts = nullptr;
		FRiderSkeletonSet Set;
		FAnimController Controller;
		bool bReady = false;
		bool bStarted = false;
		int32 EventsSeen = 0;
		TArray<FRiderSimEvent> KeyframePending;
		int32 LastTrickId = -1;
		bool bTrickWasActive = false;
		uint32 LastTrickStarts = 0;
		int32 LastAuto180 = 0;
		int32 MaskShift = 0;
		int32 Character = 0;
		// 0x107628
		int32 RailType = -1;
		FRiderAnimChannel* StepChannel = nullptr;
	};
}
