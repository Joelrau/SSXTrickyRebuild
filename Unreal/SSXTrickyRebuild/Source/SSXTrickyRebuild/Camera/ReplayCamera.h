// 0x3368B0, 0x1784D0, 0x1786D0, 0x178680, 0x178BE8
#pragma once

#include "CoreMinimal.h"
#include "Camera/RaceCamera.h"

class SSXTRICKYREBUILD_API FSSXReplayCamera
{
public:
	explicit FSSXReplayCamera(FSSXCameraDefSet* InDefs = nullptr);
	FSSXReplayCamera(const FSSXReplayCamera&) = delete;
	FSSXReplayCamera& operator=(const FSSXReplayCamera&) = delete;
	void SetDefs(FSSXCameraDefSet* InDefs) { Defs = InDefs; Cam.SetDefs(InDefs); }

	// 0x1785C0, 0x173A88, 0x173A98, 0x178A28
	void Activate(bool bReplay, int32 Rider);
	bool IsActive() const { return bActive; }
	// 0x1786C0
	int32 GetMode() const { return Mode; }
	int32 GetTarget() const { return Target; }
	// 0x175EB8, 0x336608
	void SetMode(int32 InMode);
	void NextMode();
	// 0x178968
	void Get(float& A, float& B, float& C, float& D, int32& OutTarget) const;
	// 0x173C50
	void Set(float A, float B, float C, float D, int32 InTarget, bool bCut);
	void Nudge(float Amount, int32 Op);

	FSSXRaceCamera& GetCam() { return Cam; }
	const FSSXRaceCamera& GetCam() const { return Cam; }

	TFunction<int32()> NumRacers;
	// FUN_00177DB0
	TFunction<void()> CutPlayerCamera;
	// 0x231318
	TFunction<void()> OnNarrationStop;

private:
	FSSXCameraDefSet* Defs = nullptr;
	FSSXRaceCamera Cam;
	bool bActive = false;
	int32 Mode = 7;
	int32 Target = 0;
};
