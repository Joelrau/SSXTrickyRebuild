// 0x105070, 0x15F050, 0x15FCF8
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimPrewindFadeTest, "SSX.Anim.PrewindFade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAnimPrewindFadeTest::RunTest(const FString& Parameters)
{
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	if (!Lib)
	{
		SSXTest::Skip(*this, TEXT("no exported animation library: skipped"));
		return true;
	}
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);
	FRiderSim Sim;
	FRiderInputDriver Input;
	SSXRiderSetup::InstallStates(Sim, Input, false);
	Sim.SetWorld(&World);
	SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, 0, 0);
	Sim.Reset();
	Sim.Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
	Sim.State.Velocity = 1500.0 * Sim.State.ContactForward;
	SSXAnim::FRiderAnimDriver Anim;
	Anim.Init(Lib, TEXT("eddie"), 0, 0);
	SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim);
	if (!TestNotNull(TEXT("animation channel"), Ch))
	{
		return false;
	}

	auto Ch1Weight = [&Anim]()
	{
		double W = 0.0;
		for (const SSXAnim::FPlayInstance& I : Anim.GetController().Instances(1)) { W += FMath::Max(I.Weight, 0.0); }
		return W;
	};
	FPadActions Hold;
	Hold.Held = 1ull << FPadMapper::ActJump;
	auto Step = [&](const FPadActions& A)
	{
		Input.Step(Sim, A);
		Anim.Step(Sim);
	};

	Step(FPadActions());
	Ch->LandBobble(Sim);
	for (int32 K = 0; K < 20; ++K) { Step(FPadActions()); }
	TestEqual(TEXT("land-bobble cycle 0x216 on channel 1"), Anim.GetController().CurrentId(1), SSXAnim::LandBobbleAnim);
	TestTrue(TEXT("cycle layer weight > 0"), Ch1Weight() > 0.5);
	TestTrue(TEXT("sim sees channel 1 busy before the crouch"), Sim.State.Frame < Sim.State.Ch1EndFrame);

	// hold jump: Cruise -> Prewind (0x105070)
	int32 F = 0, EnteredAt = -1, FadedAt = -1;
	FString Log;
	for (; F < 60; ++F)
	{
		FPadActions A = Hold;
		A.Pressed = F == 0 ? Hold.Held : 0;
		Step(A);
		if (EnteredAt < 0 && Sim.State.Control == ERiderControl::Prewind) { EnteredAt = F; }
		if (EnteredAt >= 0 && FadedAt < 0 && Ch1Weight() <= 0.0) { FadedAt = F; }
		if (F % 5 == 0)
		{
			Log += FString::Printf(TEXT("f%d %s/%s ch0 %#x ch1 %#x w %.3f\n"), F, LexToString(Sim.State.Motion), LexToString(Sim.State.Control),
				Anim.GetController().CurrentId(0), Anim.GetController().CurrentId(1), Ch1Weight());
		}
		if (FadedAt >= 0 && F > EnteredAt + 25) { break; }
	}
	AddInfo(Log);
	if (!TestTrue(TEXT("entered Prewind with jump held"), EnteredAt >= 0))
	{
		return false;
	}
	TestTrue(TEXT("still crouching in Prewind"), Sim.State.Control == ERiderControl::Prewind);
	TestEqual(TEXT("crouch 0x217 on channel 0"), Anim.GetController().CurrentId(0), 0x217);
	// 0x15A600: 10 animation frames = 20 updates
	TestTrue(FString::Printf(TEXT("cycle faded within the 10-anim-frame fade after the Prewind entry (entered f%d, faded f%d)"), EnteredAt, FadedAt),
		FadedAt >= EnteredAt && FadedAt <= EnteredAt + 21);
	TestTrue(TEXT("sim sees channel 1 free after the fade"), Sim.State.Frame >= Sim.State.Ch1EndFrame);
	return true;
}

#endif
