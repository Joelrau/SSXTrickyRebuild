// 0x17B270, 0x337C50, 0x17B590, 0x17B5B0, 0x17B5E8, 0x1532C0, 0x150A80, 0x153350, 0x150BE8, 0x153390, 0x150D18, 0x32CA08, 0x32CA0C, 0x17E148, 0x2A9848, 0x26D1C8, 0x2637A0
#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderState.h"

namespace SSXVibration
{
	constexpr float ImpactK = -1.3888888f;
	constexpr float ImpactMax = 2.0f;
	constexpr float ImpactThreshold = 0.3f;
	constexpr float FeedbackThreshold = 0.5f;
	constexpr float FeedbackFloor = 0.8f;
	constexpr float StrengthMax = 2.0f;
	constexpr float SmallTimeK = 1.6666666f;      // seconds = clamp(1.6666666 x, 0.02, 0.1) (0x3FD55555)
	constexpr float SmallTimeMin = 0.02f;
	constexpr float SmallTimeMax = 0.1f;
	constexpr float DecayMul = 0.98333335f;
	constexpr float DecaySub = 0.025f;
	constexpr int32 BigFrames = 2;
	constexpr int32 FrontEndFrames = 10;
	constexpr int32 NumPorts = 2;   // pad ports 0 / 1 = Options 1P / 2P

	SSXTRICKYREBUILD_API int32 SmallMotorFrames(float X);

	struct SSXTRICKYREBUILD_API FPadActuators
	{
		bool bEnabled = true;
		uint8 Small = 0;   // +0x78 act[0] small motor: 0 / 1
		uint8 Big = 0;   // +0x79 act[1] big motor: 0..255
		bool bDirty = true;
		int32 SmallFrames = -1;   // +0x84, -1 = no timeout
		int32 BigFramesLeft = -1;
		uint8 SentSmall = 0;
		uint8 SentBig = 0;
		int32 SendCount = 0;

		void SetSmall(uint8 On, int32 Frames);
		void SetBig(float Level, int32 Frames);
		void SetEnabled(bool bOn);
		// 0x17B1B4
		bool Poll(bool bStable = true);
	};

	// all three calls are skipped while a replay plays (replay object state 1..9)
	struct SSXTRICKYREBUILD_API FPlayerRumble
	{
		float Strength = 0.f;

		void Impact(FPadActuators& Pad, const FVector& Velocity, const FVector& Normal, bool bReplayPlaying = false);
		void Feedback(FPadActuators& Pad, float X, bool bReplayPlaying = false);
		void Update(FPadActuators& Pad, bool bReplayPlaying = false);
		void Apply(FPadActuators& Pad, const FRiderPadFeedback& Call, bool bReplayPlaying = false);
	};

	SSXTRICKYREBUILD_API void RiderFrame(FPadActuators& Pad, FPlayerRumble& Player, TConstArrayView<FRiderPadFeedback> Calls,
	                               bool bReplayPlaying = false);

	SSXTRICKYREBUILD_API void ApplyRaceLoadSetting(FPadActuators& Pad, int32 VibrationSetting);

	SSXTRICKYREBUILD_API void FrontEndOneShot(FPadActuators& Pad);
}
