// 0x106600, 0x106668, 0x1065C8
#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderSim.h"

namespace SSXFinishLine
{
	constexpr double SlowSpeed = 277.77777;   // cm/s (10 km/h): enter phase 0 above it, leave it below
	constexpr int32 WaitLimit = 0x12D;   // every wait gives up at 301 frames
	constexpr int32 DoneFrames = 0xF0;   // done: phase > 1 and more than 240 frames
	constexpr double VelocityDecay = 0.93;   // per frame in phase 1
	constexpr int32 FinishAnim = 0x2EE, CycleAnim = 0x2EF, WinTaunt = 0x214, LoseTaunt = 0x215, RidingLoop = 0x1FF;
	constexpr int32 TauntWindow = 0x168;         // 0x128150, 0x127E80
	constexpr double TauntSpeed = 0.5;

	// 0x11F3B8, 0x11F3F8, 0x16BAA8
	SSXTRICKYREBUILD_API bool IsWinner(int32 Mode, int32 Rank, int32 NumRacers, int32 ShowoffMedal, int32 FinishCs, int32 TimeToBeatCs,
	                             bool bLessonPassed);

	class SSXTRICKYREBUILD_API FControl : public IRiderControlState
	{
	public:
		TFunction<bool(const FRiderSim&)> IsWinner;
		TFunction<int32()> RaceFrame;

		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override;
		virtual void Update(FRiderSim& Sim, const FRiderInputWord& Input) override;

		int32 GetPhase() const { return Phase; }
		int32 GetFrames() const { return Frames; }
		bool IsDone() const { return Phase > 1 && Frames > DoneFrames; }
		int32 GetResult() const { return Result; }
		int32 WinStamp = -1;
		int32 LoseStamp = -1;
		int32 TauntPlayed = 0;

		virtual TSharedPtr<FRiderStateBlob> SaveState() const override
		{
			return SSXRiderBlob::Save(TArray<int32>({ WinStamp, LoseStamp, TauntPlayed, Phase, Frames, Result, AnimStart }));
		}
		virtual void LoadState(const FRiderStateBlob& B) override
		{
			TArray<int32> V;
			SSXRiderBlob::Load(B, V);
			if (V.Num() != 7) return;
			WinStamp = V[0]; LoseStamp = V[1]; TauntPlayed = V[2]; Phase = V[3]; Frames = V[4]; Result = V[5]; AnimStart = V[6];
		}

	private:
		int32 Phase = 0;
		int32 Frames = 0;
		int32 Result = -1;
		int32 AnimStart = 0;
	};
}
