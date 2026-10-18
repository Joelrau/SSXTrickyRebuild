#include "Input/PadMapper.h"
#include "Misc/Paths.h"

namespace
{
	namespace PB = SSXPadButton;
	using C = ERiderControl;

	constexpr uint32 AllBits = 0xFFFFFFFFu;   // BTNMAP "ALL"/"NONE" token: never held as a pressed mask

	// 0x179238
	struct FBoolRow { uint32 Pressed; uint32 NotPressed; };
	const FBoolRow BoolRows0[FPadMapper::NumBoolActions] = {
		{ PB::Cross, 0 },
		{ PB::L3, 0 },
		{ PB::Square, 0 },
		{ AllBits, 0 },
		{ AllBits, 0 },
		{ PB::Square, 0 },
		{ AllBits, 0 },
		{ AllBits, 0 },
		{ AllBits, 0 },
		{ AllBits, PB::Shoulders },// 36 RandomGrab (never with the retail BTNMAP)
		{ PB::Select, 0 },
		{ AllBits, 0 },
		{ AllBits, 0 },
		{ PB::Cross, 0 },
	};
	const FBoolRow BoolRows1[FPadMapper::NumBoolActions] = {
		{ PB::Cross, 0 },
		{ AllBits, 0 },
		{ PB::Square, 0 },
		{ AllBits, 0 },
		{ PB::Triangle, 0 },
		{ PB::Square, 0 },
		{ AllBits, 0 },
		{ AllBits, 0 },
		{ AllBits, 0 },
		{ AllBits, PB::Shoulders },// 36 RandomGrab (never with the retail BTNMAP)
		{ PB::Select, 0 },
		{ AllBits, 0 },
		{ AllBits, 0 },
		{ PB::Cross, 0 },
	};


	enum : uint32
	{
		JoyLX = 1u, JoyLPosX = 2u, JoyLNegX = 4u, JoyLY = 8u, JoyLPosY = 16u, JoyLNegY = 32u,
		JoyRX = 64u, JoyRPosX = 128u, JoyRNegX = 256u, JoyRY = 512u, JoyRPosY = 1024u, JoyRNegY = 2048u,
		PadAX = 4096u, PadAPosX = 8192u, PadANegX = 1u << 14, PadAY = 1u << 15, PadAPosY = 1u << 16, PadANegY = 1u << 17,
	};
	constexpr uint32 PosBits = JoyLPosX | JoyLPosY | JoyRPosX | JoyRPosY | PadAPosX | PadAPosY;
	constexpr uint32 NegBits = JoyLNegX | JoyLNegY | JoyRNegX | JoyRNegY | PadANegX | PadANegY;
	constexpr uint32 XBits = JoyLX | JoyLPosX | JoyLNegX | JoyRX | JoyRPosX | JoyRNegX | PadAX | PadAPosX | PadANegX;
	constexpr uint32 PadGroup = PadAX | PadAPosX | PadANegX | PadAY | PadAPosY | PadANegY;
	constexpr uint32 LeftGroup = JoyLX | JoyLPosX | JoyLNegX | JoyLY | JoyLPosY | JoyLNegY;
	constexpr uint32 RightGroup = JoyRX | JoyRPosX | JoyRNegX | JoyRY | JoyRPosY | JoyRNegY;

	// BTNMAP0 IS_ANALOG rows: (source mask, negate). Order = FPadMapper::EAnalog.
	struct FAnalogRow { uint32 Mask; bool bNegate; };
	const FAnalogRow AnalogRows0[FPadActions::NumAnalog] = {
		{ PadANegY | JoyLNegY, true },
		{ PadAPosY | JoyLPosY, false },
		{ PadAX, true },
		{ PadAY, true },
		{ PadAX | JoyLX, false },
		{ JoyRX, false },
		{ JoyRY, false },
		{ PadAX, false },
		{ PadAX, true },
		{ PadAY, true },
		{ JoyLX, true },
		{ JoyLY, true },
		{ JoyLX, false },
		{ PadAX, true },
		{ PadAY, true },
		{ JoyLY, false },
	};

	const FAnalogRow AnalogRows1[FPadActions::NumAnalog] = {
		{ PadANegY | JoyLNegY, true },
		{ PadAPosY | JoyLPosY, false },
		{ PadAX | JoyLX, true },
		{ PadAY | JoyLY, true },
		{ PadAX | JoyLX, false },
		{ JoyRX, false },
		{ JoyRY, false },
		{ PadAX, false },
		{ PadAX, true },
		{ PadAY, true },
		{ JoyLX, true },
		{ JoyLY, true },
		{ 0u, false },
		{ PadAX | JoyLX, true },
		{ PadAY | JoyLY, true },
		{ PadAY | JoyLY, false },
	};

	const FBoolRow* BoolRowsFor(int32 Layout) { return Layout == 1 ? BoolRows1 : BoolRows0; }
	const FAnalogRow* AnalogRowsFor(int32 Layout) { return Layout == 1 ? AnalogRows1 : AnalogRows0; }

	const uint32 GrabCombos[FPadMapper::NumGrabCombos] = {
		PB::L2, PB::R1, PB::R2, PB::L1, PB::R1 | PB::R2, PB::L1 | PB::L2, PB::L1 | PB::R2, PB::L2 | PB::R2, PB::L1 | PB::R1, PB::R1 | PB::L2,
		PB::L1 | PB::R1 | PB::R2, PB::R1 | PB::L2 | PB::R2, PB::L1 | PB::L2 | PB::R1, PB::L1 | PB::L2 | PB::R2, PB::Shoulders,
	};

	const uint8 GrabSlotTable[12][3][15] = {
		{ { 22, 12, 11, 6, 13, 25, 14, 5, 9, 8, 21, 0, 2, 16, 4 },
		  { 22, 12, 11, 6, 13, 25, 14, 5, 9, 8, 2, 0, 7, 16, 4 },
		  { 22, 12, 11, 6, 13, 25, 14, 5, 8, 9, 3, 18, 2, 16, 4 } },
		{ { 11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4 } },
		{ { 11, 6, 22, 12, 25, 13, 14, 5, 21, 9, 2, 8, 19, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 20, 7, 21, 9, 16, 1, 2, 8, 19 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 8, 9, 3, 18, 2, 16, 4 } },
		{ { 11, 6, 22, 12, 25, 13, 0, 15, 21, 16, 2, 9, 14, 8, 4 },
		  { 11, 6, 22, 12, 25, 13, 24, 5, 21, 23, 20, 19, 9, 2, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 8, 9, 3, 18, 2, 16, 4 } },
		{ { 22, 12, 11, 6, 13, 25, 1, 5, 21, 23, 0, 16, 2, 8, 4 },
		  { 22, 12, 11, 6, 13, 25, 23, 5, 15, 8, 2, 10, 19, 9, 4 },
		  { 22, 12, 11, 6, 13, 25, 14, 5, 8, 9, 3, 18, 2, 16, 4 } },
		{ { 11, 6, 22, 12, 25, 13, 15, 5, 21, 10, 2, 19, 8, 24, 4 },
		  { 11, 6, 22, 12, 25, 13, 17, 5, 0, 24, 7, 10, 2, 23, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4 } },
		{ { 22, 12, 11, 6, 13, 25, 20, 14, 15, 8, 1, 9, 2, 16, 19 },
		  { 22, 12, 11, 6, 13, 25, 15, 5, 21, 9, 2, 16, 7, 8, 19 },
		  { 22, 12, 11, 6, 13, 25, 14, 5, 8, 9, 3, 18, 2, 16, 4 } },
		{ { 22, 12, 11, 6, 13, 25, 16, 7, 21, 8, 2, 0, 9, 15, 4 },
		  { 22, 12, 11, 6, 13, 25, 16, 7, 21, 8, 2, 0, 9, 15, 4 },
		  { 22, 12, 11, 6, 13, 25, 14, 5, 21, 8, 9, 18, 2, 7, 4 } },
		{ { 11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4 } },
		{ { 22, 12, 11, 6, 13, 25, 17, 5, 15, 0, 24, 10, 8, 16, 4 },
		  { 22, 12, 11, 6, 13, 25, 14, 5, 21, 10, 0, 8, 2, 16, 4 },
		  { 22, 12, 11, 6, 13, 25, 14, 5, 21, 8, 9, 18, 2, 7, 4 } },
		{ { 11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4 } },
		{ { 11, 6, 22, 12, 25, 13, 17, 5, 15, 0, 24, 10, 8, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 10, 0, 8, 2, 16, 4 },
		  { 11, 6, 22, 12, 25, 13, 14, 5, 21, 8, 9, 18, 2, 7, 4 } },
	};

	const TCHAR* const CharacterPrefix[12] = {
		TEXT("eddi"), TEXT("kaor"), TEXT("luth"), TEXT("mac"), TEXT("moby"), TEXT("zoe"),
		TEXT("jp"), TEXT("elis"), TEXT("psym"), TEXT("seei"), TEXT("brod"), TEXT("mari") };
	const int32 TutorialBoardClass[12] = { 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 2, 2 };

	bool IsGroundLayout(C Ctl)
	{
		switch (Ctl)
		{
		case C::Bump: case C::Cruise: case C::ChangeFakie: case C::FakieCruise:
		case C::GateToBase: case C::LandHard: case C::LandNormal: case C::GetupFromSit:
			return true;
		default:
			return false;
		}
	}
	bool IsGateLayout(C Ctl) { return Ctl == C::GateAnticipate || Ctl == C::GateLaunch || Ctl == C::SitAndWait; }

	FORCEINLINE uint32 Bit(bool b) { return b ? 1u : 0u; }

	double S6(uint32 W, int32 Lo)
	{
		const int32 V = int32((W >> Lo) & 63u);
		return double((V & 32) ? V - 64 : V) / 31.0;
	}
	int32 S2(uint32 W, int32 Lo)
	{
		const int32 V = int32((W >> Lo) & 3u);
		return (V & 2) ? V - 4 : V;
	}

	int32 ShoveSide(const FPadActions& A)
	{
		if (A.WasPressed(FPadMapper::ActShoveLeft)) { return 3; }
		if (A.WasPressed(FPadMapper::ActShoveRight)) { return 1; }
		return FPadMapper::Sign2(A.Axes[FPadMapper::AShoveSide]);
	}

	bool AnyGrabHeld(const FPadActions& A)
	{
		return (A.Held & ((1ull << FPadMapper::GrabCount) - 1ull)) != 0;
	}
}

uint32 FSSXPadDevice::GameFromHardware(uint32 Hw)
{
	// 0x337C08
	static constexpr uint32 T[16] = { 0x1000, 0x4000, 0x2000, 0x8000, 0x80, 0x10, 0x20, 0x800, 0x100, 0x4, 0x8, 0x40,
		0x1, 0x2, 0x200, 0x400 };
	uint32 B = 0;
	for (int32 I = 0; I < 16; ++I)
	{
		if (Hw & T[I]) { B |= 1u << I; }
	}
	return B;
}

uint8 FSSXPadDevice::ByteFromUnit(float V)
{
	return uint8(FMath::Clamp(FMath::RoundToInt(FMath::Clamp(V, -1.f, 1.f) * 127.5f + 127.5f), 0, 255));
}

const FSSXPadFrame& FSSXPadDevice::Update(const FSSXPadHardware& Hw, bool bAnalogMode, bool bConnected)
{
	namespace PB = SSXPadButton;
	uint32 Game = Hw.Buttons & 0xFFFFu;
	if (!bConnected)
	{
		Game = 0;
		bAnalogMode = false;
	}
	FSSXPadFrame& F = Frame;
	F.Buttons = F.ButtonsFiltered;
	uint32 B = Game;
	if (bAnalogMode)
	{
		F.LeftX = StickFromByte(Hw.LX);
		F.LeftY = StickFromByte(Hw.LY);
		F.RightX = StickFromByte(Hw.RX);
		F.RightY = StickFromByte(Hw.RY);
	}
	else
	{
		F.LeftX = F.LeftY = F.RightX = F.RightY = 0.f;
	}
	// d-pad axes from the d-pad bits only (Left / Up win), in both modes; they never read the sticks
	F.PadX = (B & PB::Left) ? -1.f : ((B & PB::Right) ? 1.f : 0.f);
	F.PadY = (B & PB::Up) ? -1.f : ((B & PB::Down) ? 1.f : 0.f);
	if (bAnalogMode)
	{
		const struct { float V; uint32 Neg, Pos; } Sticks[4] = {
			{ F.LeftX, PB::LJoyLeft, PB::LJoyRight }, { F.LeftY, PB::LJoyUp, PB::LJoyDown },
			{ F.RightX, PB::RJoyLeft, PB::RJoyRight }, { F.RightY, PB::RJoyUp, PB::RJoyDown } };
		for (const auto& S : Sticks)
		{
			if (S.V < -0.5f) { B |= S.Neg; }
			if (S.V > 0.5f) { B |= S.Pos; }
		}
	}
	F.ButtonsFiltered = B;

	// 0x3857C0
	static const struct { uint32 Mask; int32 Key, KeyShoulder; } Keys[] = {
		{ PB::Select, 0x1B, 0x1B }, { PB::Start, 0x0D, 0x0D }, { PB::Square, 0x61, 0x41 }, { PB::Triangle, 0x62, 0x42 },
		{ PB::Circle, 0x63, 0x43 }, { PB::Cross, 0x64, 0x44 }, { PB::Left | PB::LJoyLeft, 0x4B00, 0x4700 },
		{ PB::Right | PB::LJoyRight, 0x4D00, 0x4F00 }, { PB::Down | PB::LJoyDown, 0x5000, 0x5100 },
		{ PB::Up | PB::LJoyUp, 0x4800, 0x4900 }, { PB::R1, 0x72, 0x72 }, { PB::R2, 0x52, 0x52 }, { PB::L1, 0x6C, 0x6C },
		{ PB::L2, 0x4C, 0x4C } };
	int32 Key = 0;
	for (const auto& K : Keys)
	{
		if (B & K.Mask)
		{
			Key = (B & PB::Shoulders) ? K.KeyShoulder : K.Key;
			break;
		}
	}
	FeKey = 0;
	if (Key == 0 || Key != FeLast)
	{
		FeKey = Key;
		FeCount = -7;
		FeLast = Key;
	}
	else if (++FeCount > 7 && bKeyRepeat)
	{
		FeKey = Key;
		FeCount = 0;
	}
	return F;
}

FPadMapper::FPadMapper(int32 InCharacter, int32 InBoardClass)
	: Character(InCharacter), BoardClass(InBoardClass)
{
}

uint32 FPadMapper::BoolMask(uint32 ButtonsFiltered, int32 InLayout)
{
	uint32 Out = 0;
	const FBoolRow* Rows = BoolRowsFor(InLayout);
	for (int32 K = 0; K < NumBoolActions; ++K)
	{
		const FBoolRow& Row = Rows[K];
		// held when (buttons & (P|N)) == P; a pressed mask of ALL means never
		if (Row.Pressed != AllBits && (ButtonsFiltered & (Row.Pressed | Row.NotPressed)) == Row.Pressed)
		{
			Out |= 1u << K;
		}
	}
	return Out;
}

int32 FPadMapper::GrabSlot(int32 InCharacter, int32 InBoardClass, int32 Combo)
{
	if (InCharacter < 0 || InCharacter >= NumCharacters || InBoardClass < 0 || InBoardClass >= NumBoardClasses
		|| Combo < 0 || Combo >= NumGrabCombos)
	{
		return NoTrick;
	}
	return GrabSlotTable[InCharacter][InBoardClass][Combo];
}

uint32 FPadMapper::ActionMask(int32 Action, int32 InLayout, int32 InCharacter, int32 InBoardClass)
{
	if (Action >= FirstBoolAction && Action < FirstBoolAction + NumBoolActions)
	{
		return BoolRowsFor(InLayout)[Action - FirstBoolAction].Pressed;
	}
	uint32 Out = AllBits;
	for (int32 K = 0; K < NumGrabCombos; ++K)
	{
		if (Action >= 0 && Action < GrabCount && GrabSlot(InCharacter, InBoardClass, K) == Action)
		{
			Out = GrabCombos[K];
		}
	}
	return Out;
}

uint32 FPadMapper::GrabMask(uint32 Buttons, int32 InCharacter, int32 InBoardClass)
{
	const uint32 Sh = Buttons & SSXPadButton::Shoulders;
	uint32 Out = 0;
	for (int32 K = 0; K < NumGrabCombos; ++K)
	{
		if (Sh == GrabCombos[K])
		{
			const int32 Slot = GrabSlot(InCharacter, InBoardClass, K);
			if (Slot < GrabCount)
			{
				Out |= 1u << Slot;
			}
		}
	}
	return Out;
}

double FPadMapper::Analog(const FSSXPadFrame& Pad, int32 Index, int32 InLayout)
{
	const FAnalogRow& Row = AnalogRowsFor(InLayout)[Index];
	double Value = 0.0;
	struct FGroup { uint32 Bits; float X; float Y; };
	const FGroup Groups[3] = { { PadGroup, Pad.PadX, Pad.PadY }, { LeftGroup, Pad.LeftX, Pad.LeftY },
	                           { RightGroup, Pad.RightX, Pad.RightY } };
	for (const FGroup& G : Groups)
	{
		if (Value == 0.0 && (Row.Mask & G.Bits))
		{
			const bool bX = (Row.Mask & G.Bits & XBits) != 0;
			Value = -double(bX ? G.X : G.Y);   // first non-zero source of d-pad, left stick, right stick; value = -raw
		}
	}
	if ((Row.Mask & PosBits) && Value < 0.0) { return 0.0; }
	if ((Row.Mask & NegBits) && Value > 0.0) { return 0.0; }
	if (FMath::Abs(Value) < DeadZone) { return 0.0; }
	return Row.bNegate ? -Value : Value;
}

FPadActions FPadMapper::Step(const FSSXPadFrame& Pad)
{
	FInpFrame F;
	F.Pad = Pad;
	return Step(F, false);
}

FPadActions FPadMapper::Step(const FInpFrame& Frame, bool bUseRecordedMasks)
{
	FPadActions A;
	const uint32 Bools = bUseRecordedMasks ? Frame.RecordedBools : BoolMask(Frame.Pad.ButtonsFiltered, Layout);
	for (int32 K = 0; K < NumBoolActions; ++K)
	{
		if ((Bools >> K) & 1u)
		{
			A.Held |= 1ull << (FirstBoolAction + K);
		}
	}
	uint32 Grabs = Frame.RecordedGrabs;
	if (!bUseRecordedMasks && Character >= 0)
	{
		Grabs = GrabMask(Frame.Pad.ButtonsFiltered, Character, BoardClass);
	}
	A.Held |= uint64(Grabs & ((1u << GrabCount) - 1u));
	A.Pressed = A.Held & ~PrevHeld;
	PrevHeld = A.Held;
	for (int32 I = 0; I < FPadActions::NumAnalog; ++I)
	{
		A.Axes[I] = Analog(Frame.Pad, I, Layout);
	}
	return A;
}

uint32 FPadMapper::EncodeWord(ERiderControl Control, const FPadActions& A)
{
	const double* Ax = A.Axes;
	uint32 W = 0;
	auto Q = [](double V) { return uint32(Quant6(V)); };
	auto S = [](double V) { return uint32(Sign2(V)); };
	if (IsGroundLayout(Control))
	{
		W |= Bit(A.WasPressed(ActReset)) | Bit(A.WasPressed(ActJump)) << 1 | Bit(A.IsHeld(ActJump)) << 2;
		W |= Bit(A.IsHeld(ActBoost)) << 3 | Bit(A.WasPressed(ActBoost)) << 4;
		W |= Q(Ax[ATurn]) << 5 | Q(Ax[ACrouch]) << 11 | Q(Ax[ABrake]) << 17;
		W |= uint32(ShoveSide(A)) << 23 | S(Ax[AShoveFwd]) << 25;
	}
	else if (IsGateLayout(Control))
	{
		W |= Q(Ax[AGateRock]);
	}
	else switch (Control)
	{
	case C::Jump:
		W |= Bit(A.IsHeld(ActJump)) << 1 | Bit(A.WasPressed(ActReset));
		break;
	case C::NaturalAir:
	{
		const bool bTrick = A.IsHeld(ActRandomGrab) || AnyGrabHeld(A);
		W |= Bit(bTrick) | Bit(A.WasPressed(ActReset)) << 1;
		W |= Q(Ax[ACrouch]) << 2 | Q(Ax[ATurn]) << 8;
		W |= Q(Ax[APrewindSpin]) << 14 | Q(Ax[APrewindFlip]) << 20;
		W |= uint32(ShoveSide(A)) << 26 | S(Ax[AShoveFwd]) << 28;
		break;
	}
	case C::Prewind:
		W |= Q(Ax[APrewindTurn]) << 17 | Q(Ax[APrewindSpin]) << 5 | Q(Ax[APrewindFlip]) << 11;
		W |= Bit(A.WasPressed(ActReset)) | Bit(A.IsHeld(ActJump)) << 1 | Bit(A.IsHeld(ActJumpAbort)) << 2;
		W |= Bit(A.IsHeld(ActBoost)) << 3 | Bit(A.IsHeld(ActPrewindTurnMode)) << 4;
		break;
	case C::RailSlide:
		W |= Q(Ax[ATurn]) << 5 | Q(Ax[ACrouch]) << 11 | Q(Ax[ARailSpin]) << 17;
		W |= Bit(A.WasPressed(ActJump)) << 1 | Bit(A.IsHeld(ActJump)) << 2 | Bit(A.WasPressed(ActReset));
		W |= Bit(A.IsHeld(ActRailSpinMode)) << 3 | Bit(A.IsHeld(ActBoost)) << 4;
		break;
	case C::Spin:
	{
		W |= Bit(A.WasPressed(ActReset)) << 8 | Bit(A.IsHeld(ActTweak)) << 9 | Bit(A.IsHeld(ActLateSpinMode)) << 10;
		W |= S(Ax[ASpin]) << 12 | S(Ax[AFlip]) << 14 | S(Ax[ALateSpin]) << 16;
		W |= S(Ax[ALateFlip]) << 18 | S(Ax[AAirTurn]) << 20 | S(Ax[AAirTilt]) << 22;
		W |= uint32(ShoveSide(A)) << 24 | S(Ax[AShoveFwd]) << 26;
		int32 Trick = NoTrick;
		for (int32 K = 0; K < GrabCount; ++K)
		{
			if (A.IsHeld(K)) { Trick = K; }
		}
		W |= uint32(Trick);
		break;
	}
	case C::WipeOut:
		W |= Q(Ax[AAirTurn]) << 1 | Q(Ax[AAirTilt]) << 7 | Bit(A.WasPressed(ActReset));
		break;
	case C::WipeOutRecover:
		W |= Bit(A.WasPressed(ActReset));
		break;
	case C::LessonWait:
		W |= Bit(A.WasPressed(ActLessonContinue));
		break;
	default:
		break;
	}
	return W;
}

FRiderInputWord FPadMapper::DecodeWord(ERiderControl Control, uint32 W)
{
	FRiderInputWord Out;
	if (IsGroundLayout(Control))
	{
		Out.bResetRequest = (W & 1u) != 0; Out.bJumpPressed = (W & 2u) != 0; Out.bJumpHeld = (W & 4u) != 0;
		Out.bBoostHeld = (W & 8u) != 0; Out.bBoostPressed = (W & 16u) != 0;
		Out.Turn = S6(W, 5); Out.Crouch = S6(W, 11); Out.Brake = S6(W, 17);
		Out.ShoveSide = S2(W, 23); Out.ShoveFwd = S2(W, 25);
	}
	else if (IsGateLayout(Control))
	{
		Out.GateRock = S6(W, 0);
	}
	else switch (Control)
	{
	case C::Jump:
		Out.bResetRequest = (W & 1u) != 0; Out.bJumpHeld = (W & 2u) != 0;
		break;
	case C::NaturalAir:
		Out.bTrickPressed = (W & 1u) != 0; Out.bResetRequest = (W & 2u) != 0;
		Out.Crouch = S6(W, 2); Out.Turn = S6(W, 8); Out.PrewindSpin = S6(W, 14); Out.PrewindFlip = S6(W, 20);
		Out.ShoveSide = S2(W, 26); Out.ShoveFwd = S2(W, 28);
		break;
	case C::Prewind:
		Out.bResetRequest = (W & 1u) != 0; Out.bJumpHeld = (W & 2u) != 0; Out.bJumpAbort = (W & 4u) != 0; Out.bBoostHeld = (W & 8u) != 0;
		Out.PrewindSpin = S6(W, 5); Out.PrewindFlip = S6(W, 11); Out.PrewindTurn = S6(W, 17);
		break;
	case C::RailSlide:
		Out.bResetRequest = (W & 1u) != 0; Out.bJumpPressed = (W & 2u) != 0; Out.bJumpHeld = (W & 4u) != 0; Out.bBoostHeld = (W & 16u) != 0;
		Out.bRailSpinMode = (W & 8u) != 0;
		Out.Turn = S6(W, 5); Out.Crouch = S6(W, 11); Out.RailSpin = S6(W, 17);
		break;
	case C::Spin:
		Out.TrickId = int32(W & 255u);
		Out.bResetRequest = ((W >> 8) & 1u) != 0; Out.bTweak = ((W >> 9) & 1u) != 0;
		Out.bTrickPressed = Out.TrickId != NoTrick;
		Out.Spin = S2(W, 12); Out.Flip = S2(W, 14); Out.LateSpin = S2(W, 16); Out.LateFlip = S2(W, 18);
		Out.AirTurn = double(S2(W, 20)); Out.AirTilt = double(S2(W, 22));
		Out.ShoveSide = S2(W, 24); Out.ShoveFwd = S2(W, 26);
		break;
	case C::WipeOut:
		Out.bResetRequest = (W & 1u) != 0;
		Out.AirTurn = S6(W, 1); Out.AirTilt = S6(W, 7);
		break;
	case C::WipeOutRecover:
		Out.bResetRequest = (W & 1u) != 0;
		break;
	case C::LessonWait:
		Out.bLessonContinue = (W & 1u) != 0;
		break;
	default:
		break;
	}
	return Out;
}

FRiderInputWord FPadMapper::FullWord(const FPadActions& A)
{
	const FRiderInputWord G = DecodeWord(C::Cruise, EncodeWord(C::Cruise, A));
	const FRiderInputWord Gate = DecodeWord(C::GateAnticipate, EncodeWord(C::GateAnticipate, A));
	const FRiderInputWord N = DecodeWord(C::NaturalAir, EncodeWord(C::NaturalAir, A));
	const FRiderInputWord P = DecodeWord(C::Prewind, EncodeWord(C::Prewind, A));
	const FRiderInputWord R = DecodeWord(C::RailSlide, EncodeWord(C::RailSlide, A));
	const FRiderInputWord S = DecodeWord(C::Spin, EncodeWord(C::Spin, A));
	const FRiderInputWord L = DecodeWord(C::LessonWait, EncodeWord(C::LessonWait, A));

	FRiderInputWord W = G;
	W.GateRock = Gate.GateRock;
	W.bTrickPressed = N.bTrickPressed;
	W.PrewindSpin = N.PrewindSpin;
	W.PrewindFlip = N.PrewindFlip;
	W.bJumpAbort = P.bJumpAbort;
	W.PrewindTurn = P.PrewindTurn;
	W.RailSpin = R.RailSpin;
	W.bRailSpinMode = R.bRailSpinMode;
	W.TrickId = S.TrickId;
	W.bTweak = S.bTweak;
	W.Spin = S.Spin; W.Flip = S.Flip; W.LateSpin = S.LateSpin; W.LateFlip = S.LateFlip;
	W.AirTurn = S.AirTurn;
	W.AirTilt = S.AirTilt;
	W.bLessonContinue = L.bLessonContinue;
	return W;
}

void FPadMapper::InferControls(uint32 Word, const FPadActions& A, TArray<ERiderControl>& Out)
{
	Out.Reset();
	static const C Candidates[] = {
		C::Bump, C::Cruise, C::ChangeFakie, C::FakieCruise, C::GateToBase, C::GateAnticipate, C::GateLaunch,
		C::Jump, C::LandHard, C::LandNormal, C::NaturalAir, C::Prewind, C::RailSlide, C::Spin, C::SitAndWait,
		C::GetupFromSit, C::WipeOut, C::WipeOutRecover, C::LessonWait };
	for (C Ctl : Candidates)
	{
		if (EncodeWord(Ctl, A) == Word)
		{
			Out.Add(Ctl);
		}
	}
}

bool FPadMapper::TutorialRider(const FString& FileName, int32& OutCharacter, int32& OutBoardClass)
{
	FString Base = FPaths::GetBaseFilename(FileName).ToLower();
	if (Base.Len() < 3)
	{
		return false;
	}
	Base.LeftChopInline(2);
	for (int32 I = 0; I < 12; ++I)
	{
		if (Base == CharacterPrefix[I])
		{
			OutCharacter = I;
			OutBoardClass = TutorialBoardClass[I];
			return true;
		}
	}
	return false;
}
