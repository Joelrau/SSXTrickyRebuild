#pragma once

#include "CoreMinimal.h"
#include "Sim/RiderState.h"

namespace SSXPadButton
{
	enum : uint32
	{
		Up = 1u << 0,
		Down = 1u << 1,
		Right = 1u << 2,
		Left = 1u << 3,
		Square = 1u << 4,
		Triangle = 1u << 5,
		Circle = 1u << 6,
		Start = 1u << 7,
		Select = 1u << 8,
		L1 = 1u << 9,
		R1 = 1u << 10,
		Cross = 1u << 11,
		L2 = 1u << 12,
		R2 = 1u << 13,
		L3 = 1u << 14,
		R3 = 1u << 15,
		Shoulders = L1 | R1 | L2 | R2,
		LJoyUp = 1u << 17,
		LJoyDown = 1u << 18,
		LJoyRight = 1u << 19,
		LJoyLeft = 1u << 20,
		RJoyUp = 1u << 21,
		RJoyDown = 1u << 22,
		RJoyRight = 1u << 23,
		RJoyLeft = 1u << 24,
	};
	constexpr int32 NumButtons = 16;
}

// PS2 axis convention: X right = +1, Y DOWN = +1; sticks (byte - 127.5) / 127.5.
struct FSSXPadFrame
{
	uint32 Buttons = 0;   // +0x0C the PREVIOUS frame's +0x10 word
	uint32 ButtonsFiltered = 0;   // +0x10 this frame's word (game bits + stick direction bits)
	float PadX = 0.f;   // +0x14 d-pad buttons only: Left = -1 (Left wins), Right = +1
	float PadY = 0.f;   // +0x18 Up = -1 (Up wins), Down = +1
	float LeftX = 0.f;
	float LeftY = 0.f;
	float RightX = 0.f;
	float RightY = 0.f;

	/** Stick value as the PS2 pad reports it: quantised through a 0..255 byte. */
};

struct FSSXPadHardware
{
	uint32 Buttons = 0;
	uint8 LX = 128, LY = 128, RX = 128, RY = 128;   // 0..255, 128 = centre; X right = 255, Y down = 255
};

struct SSXTRICKYREBUILD_API FSSXPadDevice
{
	FSSXPadFrame Frame;
	bool bKeyRepeat = false;
	int32 FeKey = 0;   // +0x2C FE key emitted this frame (0 = none)
	int32 FeLast = -1;
	int32 FeCount = 0;

	// 0x337C08
	static uint32 GameFromHardware(uint32 HwActiveHigh);
	static float StickFromByte(uint8 B) { return float((double(B) - 127.5) / 127.5); }
	/** Unit stick value (-1..1, PS2 axis directions) -> the pad byte. */
	static uint8 ByteFromUnit(float V);

	const FSSXPadFrame& Update(const FSSXPadHardware& Hw, bool bAnalogMode = true, bool bConnected = true);
	void Reset() { *this = FSSXPadDevice(); }
};

struct FInpFrame
{
	uint32 RecordedWord = 0;
	uint32 RecordedBools = 0;   // held mask of the 14 bool actions (bit k = action 27 + k)
	uint32 RecordedGrabs = 0;
	FSSXPadFrame Pad;
};

struct FPadActions
{
	static constexpr int32 NumAnalog = 16;
	uint64 Held = 0;   // bit = action index (0..25 trick slots, 27..40 bool actions)
	uint64 Pressed = 0;   // held now, not held on the previous mapper step
	double Axes[NumAnalog] = {};

	bool IsHeld(int32 Action) const { return (Held >> Action) & 1ull; }
	bool WasPressed(int32 Action) const { return (Pressed >> Action) & 1ull; }
};

class SSXTRICKYREBUILD_API FPadMapper
{
public:
	static constexpr int32 GrabCount = 26;
	static constexpr int32 NoTrick = 26;
	static constexpr int32 ActJump = 27;
	static constexpr int32 ActJumpAbort = 28;
	static constexpr int32 ActBoost = 29;
	static constexpr int32 ActCameraToggle = 30;
	static constexpr int32 ActCameraReverse = 31;
	static constexpr int32 ActTweak = 32;
	static constexpr int32 ActLateSpinMode = 33;
	static constexpr int32 ActRailSpinMode = 34;
	static constexpr int32 ActPrewindTurnMode = 35;
	static constexpr int32 ActRandomGrab = 36;
	static constexpr int32 ActReset = 37;
	static constexpr int32 ActShoveLeft = 38;
	static constexpr int32 ActShoveRight = 39;
	static constexpr int32 ActLessonContinue = 40;
	static constexpr int32 FirstBoolAction = ActJump;
	static constexpr int32 NumBoolActions = 14;

	enum EAnalog : int32
	{
		ABrake, ACrouch, ASpin, AFlip, ATurn, AShoveSide, AShoveFwd, ARailSpin,
		ALateSpin, ALateFlip, AAirTurn, AAirTilt, APrewindTurn, APrewindSpin, APrewindFlip, AGateRock,
	};

	static constexpr double DeadZone = 0.38046828;
	static constexpr int32 NumCharacters = 12;
	static constexpr int32 NumBoardClasses = 3;
	static constexpr int32 NumGrabCombos = 15;

	explicit FPadMapper(int32 InCharacter = 0, int32 InBoardClass = 0);
	void SetRider(int32 InCharacter, int32 InBoardClass) { Character = InCharacter; BoardClass = InBoardClass; }
	// Layout = btnmap%d.dat (rider_profile+0x6C): 0 = BTNMAP0 "Default" (also the tutorial), 1 = BTNMAP1 "Pro"
	// (the left stick also drives Spin / Flip / PrewindSpin / PrewindFlip).
	void SetLayout(int32 InLayout) { Layout = InLayout == 1 ? 1 : 0; }
	int32 GetLayout() const { return Layout; }

	void ResetEdges() { PrevHeld = 0; }

	FPadActions Step(const FSSXPadFrame& Pad);
	FPadActions Step(const FInpFrame& Frame, bool bUseRecordedMasks);

	static uint32 BoolMask(uint32 ButtonsFiltered, int32 InLayout = 0);
	static uint32 GrabMask(uint32 Buttons, int32 Character, int32 BoardClass);
	// 0x1546C8
	static uint32 ActionMask(int32 Action, int32 InLayout, int32 InCharacter, int32 InBoardClass);
	static int32 GrabSlot(int32 Character, int32 BoardClass, int32 Combo);
	static double Analog(const FSSXPadFrame& Pad, int32 Index, int32 InLayout = 0);

	static int32 Quant6(double V) { return int32(V * 31.0) & 63; }
	static int32 Sign2(double V) { return V < -0.5 ? 3 : (V > 0.5 ? 1 : 0); }

	static uint32 EncodeWord(ERiderControl Control, const FPadActions& A);
	static FRiderInputWord DecodeWord(ERiderControl Control, uint32 Word);
	static FRiderInputWord FullWord(const FPadActions& A);
	static void InferControls(uint32 Word, const FPadActions& A, TArray<ERiderControl>& Out);

	static bool TutorialRider(const FString& FileName, int32& OutCharacter, int32& OutBoardClass);

private:
	int32 Character = 0;
	int32 BoardClass = 0;
	int32 Layout = 0;
	uint64 PrevHeld = 0;
};
