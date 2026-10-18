// 0x1EC548 / 0x1EB6D0 / 0x1EC7D8 / 0x1EC808
#pragma once

#include "CoreMinimal.h"
#include "Race/SSXReplay.h"

namespace SSXReplayCtl
{
	enum class EEvent : uint8
	{
		Navigate,      // 0x1A8648 / 0x213D60
		HelpSound,     // 0x214428
		CameraSound,   // 0x214650
		SpeechStop,    // 0x228D10
		AudioPause,    // 0x2153F8
		AudioResume,   // 0x215488
		BoardAudio,    // 0x22D370 / 0x215370
	};

	struct FEnv
	{
		TFunction<int32()> CameraMode;                       // 0x1786C0
		TFunction<void(float Amount, int32 Op)> CameraNudge; // 0x178BE8
		TFunction<void()> NextCamera;                        // 0x178680
		TFunction<void(int32 Loc)> Message;                  // 0x1EDED0
		int32 Racers = 1;
	};

	// 0x200000 / 0x400000 / 0x800000
	constexpr uint32 RStickA = 0x200000, RStickB = 0x400000, RStickC = 0x1000000, RStickD = 0x800000;
	constexpr uint32 PadL3 = 0x8000;   // TODO: 0x8000 is assumed to be a stick click
}

class SSXTRICKYREBUILD_API FSSXReplayControl
{
public:
	/** 0x1EC548: editor mode = Replay setting Advanced; the overlay pad's racer becomes the focus (not for a loaded
	 *  replay); the focus camera is applied; command 1 (to the start); help level / max 0 / 1 (Normal) or 1 / 2. */
	void Activate(FSSXReplay& R, int32 PadRacer, bool bAdvanced);
	// 0x1EB6D0
	void Update(FSSXReplay& R, uint32 Held, uint32 Prev, const SSXReplayCtl::FEnv& Env);

	TArray<SSXReplayCtl::EEvent> Events;
	bool bEditor = false;
	int32 HelpLevel = 0;
	int32 HelpMax = 1;
	bool bHelpChanged = false;
	float PanelY = 480.f;
	float PanelA = 360.f;
	float PanelB = 360.f;
	int32 Blink = 0;
	mutable int32 Slide = 0;  // 0x1ED490
	float Fade = 0.f;
	int32 MessageLoc = 0;     // 0x1EDED0
	int32 FadeDelay = 0;
	int32 RepeatCount = 0;
	uint32 RepeatMask = 0;
	bool bBusy = false;
	bool bHidden = false;     // 0x1EC808 / 0x1EF990
	bool bMarking = false;
	uint32 MarkFrame = 0;
	int32 PadRacer = 0;
	int32 LastCommand = 0;
	FSSXReplay::FTimeline Timeline;   // 0x191D88
	int32 WinLen = 0;
	mutable int32 WinStart = 0;
	int32 WinStep = 1;
	TArray<int32> List430;
	int32 List430Cur = 0;

	// 0x1EC728
	void SlidePanel();
	// 0x1ED490
	int32 ScrollWindow(int32 Play) const;

private:
	void Add(SSXReplayCtl::EEvent E) { Events.Add(E); }
};
