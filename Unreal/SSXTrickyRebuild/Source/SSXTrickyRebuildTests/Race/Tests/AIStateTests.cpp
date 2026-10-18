// 0x139AB8, 0x11CAE8
#include "Dom/JsonObject.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/AIInput.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/RiderSim.h"
#include "Sim/PatchWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceAIStateTest, "SSX.Race.AIState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXRaceAIStateTest::RunTest(const FString& Parameters)
{
	{
		FPatchWorld Slope;
		SSXRiderSetup::BuildSlopeWorld(Slope, 15.0, 1);
		FRiderSim Sim;
		Sim.SetWorld(&Slope);
		SSXRiderSetup::RegisterStates(Sim);
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector(0, 500, 0), 300.0, false);
		TestEqual(TEXT("respawn drop: motion Air"), int32(Sim.State.Motion), int32(ERiderMotion::Air));
		TestEqual(TEXT("respawn drop: trick_phase 3"), Sim.State.TrickPhase, 3);
	}

	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("ai_state_reference.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("%s missing (AI state vectors recorded from the original game): trace checks skipped"), *Path));
		return !HasAnyErrors();
	}
	TArray<TSharedPtr<FJsonValue>> Traces;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Traces))
	{
		AddError(TEXT("ai_state_reference.json: parse error"));
		return false;
	}
	int32 Decisions = 0, Agree = 0, Releases = 0;
	for (const TSharedPtr<FJsonValue>& Tv : Traces)
	{
		const TSharedPtr<FJsonObject> T = Tv->AsObject();
		const FString Trace = T->GetStringField(TEXT("trace"));
		FAIRaceView Race;
		Race.Mode = ERaceEventMode(T->GetIntegerField(TEXT("game_mode")));
		for (const TSharedPtr<FJsonValue>& Rv : T->GetArrayField(TEXT("riders")))
		{
			const TSharedPtr<FJsonObject> Rd = Rv->AsObject();
			const int32 Racer = Rd->GetIntegerField(TEXT("racer"));
			for (const TSharedPtr<FJsonValue>& Wv : Rd->GetArrayField(TEXT("wipe_reset_frames")))
			{
				const TSharedPtr<FJsonObject> W = Wv->AsObject();
				FAIState St;
				St.Skill = W->GetNumberField(TEXT("skill"));
				St.Target = -1;
				FAIRiderView R;
				R.Control = W->GetIntegerField(TEXT("control_at_input"));
				R.Motion = W->GetIntegerField(TEXT("motion"));
				R.Yaw = R.PathHeading = 0.0;
				const int32 Count = W->GetIntegerField(TEXT("offpath"));
				St.OffPathFrames = R.Motion == 5 ? FMath::Max(Count - 1, 0) : Count;
				R.CatchupNear = W->GetNumberField(TEXT("near"));
				R.CatchupFar = W->GetNumberField(TEXT("far"));
				double Lead = 0.0;
				if (W->TryGetNumberField(TEXT("lead"), Lead))
				{
					R.CatchupRival = 0;
					R.RivalDist[0] = -Lead;
					R.RivalBearing[0] = R.PathHeading;
				}
				const bool bWant = W->GetBoolField(TEXT("reset_expected"));
				const bool bGot = SSXRaceAI::ResetDecision(St, R, Race);
				++Decisions;
				Agree += bGot == bWant ? 1 : 0;
				if (bGot != bWant)
				{
					AddError(FString::Printf(TEXT("%s racer %d frame %d: reset %d, original %d"), *Trace, Racer,
						W->GetIntegerField(TEXT("frame")), bGot ? 1 : 0, bWant ? 1 : 0));
				}
			}
			// 0x138D60
			const double Tl = SSXRaceAI::TrickTimeA / (Rd->GetNumberField(TEXT("stat_air")) / 255.0 * SSXRaceAI::TrickTimeB
				+ SSXRaceAI::TrickTimeC);
			for (const TSharedPtr<FJsonValue>& Pv : Rd->GetArrayField(TEXT("trick_plans")))
			{
				const TSharedPtr<FJsonObject> P = Pv->AsObject();
				double Rel = 0.0;
				bool bShort = false;
				if (P->GetIntegerField(TEXT("grab")) == SSXRaceAI::NoTrick || !P->TryGetNumberField(TEXT("release_time_left"), Rel)
					|| !P->TryGetBoolField(TEXT("short_hold"), bShort))
				{
					continue;
				}
				const double Step = 3.0 * P->GetNumberField(TEXT("time_scale")) / 60.0;
				const double Limit = bShort ? 0.3 : Tl;
				++Releases;
				TestTrue(FString::Printf(TEXT("%s racer %d press %d: release at time left %.3f within 3 frames below %.3f"), *Trace,
					Racer, P->GetIntegerField(TEXT("press_frame")), Rel, Limit), Rel < Limit + 0.002 && Rel > Limit - Step - 0.002);
			}
		}
	}
	AddInfo(FString::Printf(TEXT("reset decisions %d / %d agree, %d grab releases checked"), Agree, Decisions, Releases));
	TestTrue(TEXT("reset decisions present"), Decisions > 30);
	TestTrue(TEXT("grab releases present"), Releases > 50);
	return !HasAnyErrors();
}

#endif
