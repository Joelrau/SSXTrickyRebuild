// 0x113E10, 0x1141E0, 0x164FC0, 0x1699A0, 0x1109B0, 0x110A40, 0x110AB8, 0x1FF4C8, 0x193C68, 0x210A38
#pragma once

#include "CoreMinimal.h"
#include "Race/PreRaceIntro.h"

namespace SSXEndRace
{
	constexpr int32 NumSlots = 15;
	constexpr int32 NoScene = 0x10;
	constexpr int32 SceneWin = 3, SceneLose = 4, SceneUserVs = 5, SceneVsUser = 10;
	constexpr int32 RivalScoreMin = 2;
	constexpr int32 OverlayHud = 0, OverlayPopFinish = 7;
	constexpr int32 ResultsShown = 10;

	SSXTRICKYREBUILD_API int32 RivalScene(TConstArrayView<TPair<int32, int32>> Others);
	// 0x16BAA8
	SSXTRICKYREBUILD_API int32 ResultScene(int32 Mode, int32 Rank, int32 NumRacers, int32 ShowoffMedal, int32 FinishCs, int32 TimeToBeatCs);

	class SSXTRICKYREBUILD_API FHandler
	{
	public:
		void Enter(SSXIntro::IEnv& Env, const SSXIntro::FSlot InSlots[NumSlots], int32 InScene1, int32 InScene2);
		// 0x1A7BE0
		int32 Update(SSXIntro::IEnv& Env, bool bExitPending = false, bool bOverlayBusy = false);
		int32 GetState() const { return State; }
		int32 GetScene(int32 K) const { return K == 0 ? Scene1 : Scene2; }
		bool IsChainOpen() const { return bChainOpen; }
		bool IsActive() const { return bActive; }
		void Reset() { bActive = false; bChainOpen = false; State = 0; }

	private:
		bool SceneMissing(int32 Slot) const;
		void StartScene(SSXIntro::IEnv& Env, int32 Slot);
		void PlayScene(SSXIntro::IEnv& Env, int32 Slot);
		bool Skip(SSXIntro::IEnv& Env);
		SSXIntro::FSlot Slots[NumSlots];
		int32 Scene1 = NoScene, Scene2 = NoScene;
		int32 State = 0;
		bool bActive = false;
		bool bChainOpen = false;
		bool bSubState9 = false;
	};
}
