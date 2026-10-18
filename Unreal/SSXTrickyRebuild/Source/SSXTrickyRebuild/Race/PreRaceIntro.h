// 0x112B88, 0x112D10
#pragma once

#include "CoreMinimal.h"

namespace SSXIntro
{
	constexpr int32 NullScript = 0x39;
	constexpr int32 OverlayNextRace = 3, OverlayHelpRace = 18, OverlayAsyncLoad = 8;
	constexpr int32 Done = 0x15;

	// 0x32F380
	SSXTRICKYREBUILD_API FString ScriptName(int32 Type);
	inline bool GatesUsed(int32 Mode, int32 Venue) { return Venue != 7 && (Mode == 0 || Mode == 2 || Mode == 3 || Mode == 4 || Mode == 5 || Mode == 7 || Mode == 9); }

	struct FSlot
	{
		int32 Type = NullScript;
		struct FLine { int32 Speaker = 0, LineId = 0, Aux = 0, Speech = -1; };
		TArray<FLine> Lines;
	};

	enum class EEntry : uint8 { Normal, NextHeat, Restart };

	class SSXTRICKYREBUILD_API IEnv
	{
	public:
		virtual ~IEnv() = default;
		virtual bool Loading() const { return false; }
		virtual bool DirectorRunning() const { return false; }
		virtual bool SkipPressed() { return false; }
		virtual int32 LineKey(int32 LineId) const { return LineId; }   // 0x15FB08
		virtual bool HelpNeeded() const { return false; }
		virtual void Act(const FString& Name, int32 Arg = 0, const FString& Text = FString()) {}
	};

	class SSXTRICKYREBUILD_API FHandler
	{
	public:
		void Enter(IEnv& Env, int32 InMode, int32 InVenue, const FSlot InSlots[3], EEntry Entry);
		int32 Update(IEnv& Env);
		int32 GetStep() const { return Step; }
		bool IsDone() const { return Step == Done; }

	private:
		void StartSlot(IEnv& Env, int32 K);
		void PlaySlotScript(IEnv& Env, int32 K);
		bool Skip(IEnv& Env);
		int32 CardDecision(IEnv& Env);
		int32 Step = Done;
		int32 Mode = 0, Venue = 0;
		FSlot Slots[3];
	};

	constexpr int32 ClockStart = -300, ClockStep = 2;
	struct FTimeline { TArray<int32> Beeps; int32 Go = 0; int32 RaceEnter = 0; int32 LightOffsets[4] = { 60, 90, 120, 150 }; };
	SSXTRICKYREBUILD_API FTimeline CountdownTimeline(int32 EnterFrame);
}
