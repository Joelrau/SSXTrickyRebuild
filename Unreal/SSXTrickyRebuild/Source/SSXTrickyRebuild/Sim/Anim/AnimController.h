#pragma once

#include "CoreMinimal.h"
#include "Sim/Anim/AflClips.h"

namespace SSXAnim
{
	constexpr int32 NoAnim = 0x300;     // 0x15F060

	struct FAnimDriveInputs
	{
		double SteerFollow = 0.0;
		double Brake = 0.0;
		double Crouch = 0.0;
		double Steer = 0.0;
		double AirTurn = 0.0;
		double AirTilt = 0.0;
		double Ramp1F0 = 0.0;
		double PrewindA = 0.0;
		double PrewindB = 0.0;
		double Gate = 0.0;
		int32 GrindType = 0;
		bool bGoofy = false;
	};

	struct FAnimSub
	{
		int32 Clip = INDEX_NONE;
		double Frame = 0.0;
		double Weight = 0.0;
	};

	struct FProceduralSubs
	{
		static constexpr double OnClock = -1.0;
		static constexpr double Sync = -2.0;   // 0x159E80
		int32 Num = 0;
		FAnimSub Subs[3];
		bool bClock = false;
		bool bCycle = false;
		bool bHandled = false;
	};

	SSXTRICKYREBUILD_API FProceduralSubs EvalProcedural(const FAflLibrary& Lib, int32 Mode, const FAnimDriveInputs& In,
		int32 BoardClass, int32 OwnClip);

	struct FPlayInstance
	{
		int32 AnimId = NoAnim;
		FAnimProps Props;
		int32 OwnClip = INDEX_NONE;
		int32 NumSubs = 1;
		FAnimSub Subs[3];
		double Weight = 1.0;
		double Rate = 0.0;   // per game frame
		double Speed = 1.0;
		double Yaw = 0.0;
		uint32 Mask = 0xFFFFFFFFu;
		bool bCycle = false;
		bool bDone = false;
		double Hold = -1.0;   // < 0 = none
		bool bMirror = false;
		bool bAutoChain = true;
		int32 AutoFadeStart = MAX_int32;
		double AutoFadeRate = 0.0;
		int32 Serial = 0;
	};

	class SSXTRICKYREBUILD_API FAnimController
	{
	public:
		static constexpr int32 NumChannels = 3;

		void Init(const FAflLibrary* InLib, int32 InBoardClass, int32 InCharacter);
		bool IsReady() const { return Lib != nullptr; }
		void Reset();

		bool bMirror = false;
		double Speed = 1.0;
		uint32 Mask = 0xFFFFFFFFu;
		double Yaw = 0.0;

		// 0x15FCF8
		int32 Play(int32 AnimId, double StartOffset = 0.0, int32 StartMode = 5, double SpeedOverride = -1.0);

		// 0x15F390, 0x117180, 0x15F440, 0x15F7C8, 0x15F700, 0x117190
		void Update(const FAnimDriveInputs& In);

		// 0x15F5B8
		void SweepDone();

		void GatherLayers(int32 Part, TArray<FPoseLayer>& Out) const;
		// 0x129628
		int32 PoseCurrentId() const { return bCaptured ? CapturedId0 : CurrentId(0); }
		int32 PoseCurrentCategory() const { return bCaptured ? CapturedCategory0 : CurrentCategory(0); }

		int32 CurrentId(int32 Channel = 0) const { return Current[FMath::Clamp(Channel, 0, NumChannels - 1)]; }
		// 0x15EEC0
		int32 CurrentCategory(int32 Channel = 0) const;
		const FPlayInstance* Newest(int32 Channel = 0) const;
		FPlayInstance* Newest(int32 Channel = 0);
		const TArray<FPlayInstance>& Instances(int32 Channel) const { return Channels[Channel]; }

		// 0x15A600, 0x15F050, 0x159D88
		void FadeChannel(int32 Channel, int32 B);
		// 0x15EF98
		void ReleaseHold(int32 Category);
		// 0x15EEF8
		void AddLayerYaw(double A);
		// 0x15EF48
		void ClearLayerMirrors();

		// 0x23D4D8
		TFunction<uint32()> RandomB;
		int32 LastVariant = 0;
		int32 ChainAction1 = 0;

		const FAflLibrary* GetLibrary() const { return Lib; }

	private:
		void Clock(FPlayInstance& I) const;
		int32 ChainTarget(const FPlayInstance& I) const;
		void GatherLive(int32 Part, TArray<FPoseLayer>& Out) const;
		void Invalidate() { if (!bInSweep) { bCaptured = false; } }

		TArray<FPoseLayer> CapturedLayers[2];
		int32 CapturedId0 = NoAnim;
		int32 CapturedCategory0 = 8;
		bool bCaptured = false;
		bool bInSweep = false;
		int32 SerialCounter = 0;

		const FAflLibrary* Lib = nullptr;
		int32 BoardClass = 0;
		int32 Character = 0;
		int32 Current[NumChannels] = { NoAnim, NoAnim, NoAnim };
		TArray<FPlayInstance> Channels[NumChannels];
	};
}
