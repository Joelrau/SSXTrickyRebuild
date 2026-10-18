#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/GroundStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AnimTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	int32 FramesIn(FPatchWorld& World, int32 BoardClass, TFunctionRef<void(FRiderSim&)> Enter, ERiderControl Control)
	{
		static const SSXAnim::FAnimTable Defaults;
		FRiderSim Sim;
		Sim.SetWorld(&World);
		Sim.Profile.BoardClass = BoardClass;
		RegisterGroundStates(Sim);
		SSXAnim::FindChannel(Sim)->Table = &Defaults;
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = 1200.0 * FVector(0.0, FMath::Cos(FMath::DegreesToRadians(15.0)), -FMath::Sin(FMath::DegreesToRadians(15.0)));
		Sim.State.BoardClass = BoardClass;
		Enter(Sim);
		int32 Rows = 0;
		while (Sim.State.Control == Control && Rows < 1000)
		{
			Sim.Step(FRiderInputWord());
			Rows += Sim.State.Control == Control ? 1 : 0;
			if (Sim.State.Control != Control) { break; }
		}
		return Rows;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimDurationsTest, "SSX.Anim.Durations", AnimTestFlags)

bool FSSXAnimDurationsTest::RunTest(const FString& Parameters)
{
	using namespace SSXAnim;
	const FAnimTable Defaults;
	TestEqual(TEXT("L_TAIL bx 26 f -> 51 frames (0.83 s + 1 frame)"), ControlFrames(Defaults.Frames(LandTail, 0)), 51);
	TestEqual(TEXT("L_TAILBWD bx 26 f -> 51 frames"), ControlFrames(Defaults.Frames(LandTailBwd, 0)), 51);
	TestEqual(TEXT("R_SWITCH bx 21 f -> 41 frames"), ControlFrames(Defaults.Frames(SwitchFakie, 0)), 41);
	TestEqual(TEXT("R_SWITCH fr 15 f -> 29 frames"), ControlFrames(Defaults.Frames(SwitchFakie, 1)), 29);
	TestEqual(TEXT("J_2GO bx 36 f: push plays 70 frames"), OneShotFrames(Defaults.Frames(SkatePush, 0)), 70);
	TestEqual(TEXT("GU_FROMBUTTRIGHT bx 35 f -> 69 frames (1.15 s)"), ControlFrames(Defaults.Frames(GetUpDefault, 0)), 69);
	const FAnimTable& Disc = FAnimTable::Shared();
	AddInfo(FString::Printf(TEXT("animation table: %s; L_TAIL bx %d, ex %d; get-up 0x24C bx %d"), Disc.IsFromDisc() ? TEXT("SimData/anim_table.bin") : TEXT("notes' defaults"),
		Disc.Frames(LandTail, 0), Disc.Frames(LandTail, 2), Disc.Frames(GetUpDefault, 0)));

	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	const int32 LandBx = FramesIn(World, 0, [](FRiderSim& S) { FindChannel(S)->LandHardAnim = LandTail; S.SetControl(ERiderControl::LandHard, TEXT("T53")); }, ERiderControl::LandHard);
	const int32 LandEx = FramesIn(World, 2, [](FRiderSim& S) { FindChannel(S)->LandHardAnim = LandTail; S.SetControl(ERiderControl::LandHard, TEXT("T53")); }, ERiderControl::LandHard);
	const int32 Atomic = FramesIn(World, 0, [](FRiderSim& S) { FindChannel(S)->LandHardAnim = LandAtomic; S.SetControl(ERiderControl::LandHard, TEXT("T53")); }, ERiderControl::LandHard);
	const int32 Fakie = FramesIn(World, 0, [](FRiderSim& S) { S.SetControl(ERiderControl::ChangeFakie, TEXT("T27")); }, ERiderControl::ChangeFakie);
	const int32 Gate = FramesIn(World, 0, [](FRiderSim& S) { S.SetControl(ERiderControl::GateToBase, TEXT("T43")); }, ERiderControl::GateToBase);
	const int32 GetUp = FramesIn(World, 0, [](FRiderSim& S) { S.ApplyTransition(TEXT("T63")); }, ERiderControl::WipeOutRecover);
	const int32 GetUpN = FAnimTable::Shared().Frames(GetUpDefault, 0);
	AddInfo(FString::Printf(TEXT("sim: LandHard L_TAIL bx %d, ex %d, L_ATOMIC bx %d; ChangeFakie %d; Gate2Base %d; get-up %d frames"),
		LandBx, LandEx, Atomic, Fakie, Gate, GetUp));
	TestEqual(TEXT("LandHard L_TAIL bx lasts 51 frames"), LandBx, 51);
	TestEqual(TEXT("LandHard L_TAIL ex (25 f) lasts 49 frames"), LandEx, 49);
	TestEqual(TEXT("LandHard L_ATOMIC bx (31 f) lasts 61 frames"), Atomic, 61);
	TestEqual(TEXT("ChangeFakie bx lasts 41 frames"), Fakie, 41);
	TestEqual(TEXT("Gate2Base bx lasts 41 frames"), Gate, 41);
	TestEqual(TEXT("WipeOutRecover get-up (0x24C) lasts 2 (n - 1) + 1 frames"), GetUp, ControlFrames(GetUpN));
	TestEqual(TEXT("bx get-up 0x24C is 35 frames -> 69 (1.15 s)"), ControlFrames(GetUpN), 69);
	return true;
}

#endif
