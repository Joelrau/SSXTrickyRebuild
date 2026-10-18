// 0x127D08, 0x15F050, 0x10C198, 0x10F9A0
#include "Harness/RiderSetup.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimWipeOutFlailTest, "SSX.Anim.WipeOutFlail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAnimWipeOutFlailTest::RunTest(const FString& Parameters)
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
	auto Step = [&]()
	{
		Input.Step(Sim, FPadActions());
		Anim.Step(Sim);
	};

	Step();
	Ch->LandBobble(Sim);
	for (int32 K = 0; K < 20; ++K) { Step(); }
	TestEqual(TEXT("flail 0x216 on channel 1"), Anim.GetController().CurrentId(1), SSXAnim::LandBobbleAnim);
	TestTrue(TEXT("flail layer weight > 0"), Ch1Weight() > 0.5);

	// 0x11EBF8
	FRiderImpact Impact;
	Impact.Point = Sim.State.Position + 30.0 * Sim.State.ContactForward;
	Impact.Normal = -Sim.State.ContactForward;
	Impact.Speed = 1500.0;
	Sim.StartWipeOut(Impact, TEXT("test knock-down"));
	FString Log;
	int32 F = 0, FadedAt = -1;
	bool bRecovered = false;
	for (; F < 1200; ++F)
	{
		Step();
		const double W = Ch1Weight();
		if (FadedAt < 0 && W <= 0.0) { FadedAt = F; }
		if (F % 10 == 0)
		{
			Log += FString::Printf(TEXT("f%d %s/%s ch0 %#x ch1 %#x w %.3f\n"), F, LexToString(Sim.State.Motion), LexToString(Sim.State.Control),
				Anim.GetController().CurrentId(0), Anim.GetController().CurrentId(1), W);
		}
		if (Sim.State.Motion == ERiderMotion::WipeOutRecover) { bRecovered = true; }
		if (bRecovered && Sim.State.Motion == ERiderMotion::Ground && Sim.State.Control == ERiderControl::Cruise) { break; }
	}
	AddInfo(Log);
	TestTrue(TEXT("got up (WipeOutRecover -> Ground / Cruise)"), bRecovered && F < 1200);
	// 0x15A600
	TestTrue(FString::Printf(TEXT("flail faded within the 10-anim-frame fade after the hit (faded at f%d)"), FadedAt), FadedAt >= 0 && FadedAt <= 21);
	for (int32 K = 0; K < 30; ++K) { Step(); }
	TestEqual(TEXT("no channel-1 layer after the get-up"), Ch1Weight(), 0.0);
	TestTrue(TEXT("sim sees channel 1 free after the get-up"), Sim.State.Frame >= Sim.State.Ch1EndFrame);
	return true;
}

#endif
