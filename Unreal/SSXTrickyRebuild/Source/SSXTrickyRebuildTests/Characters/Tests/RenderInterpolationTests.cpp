#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Harness/FixedStepClock.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "SnowboarderPawn.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Ground/GroundStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	double PoseDelta(const TArray<FTransform>& A, const TArray<FTransform>& B)
	{
		double D = 0.0;
		for (int32 I = 0; I < A.Num() && I < B.Num(); ++I)
		{
			D = FMath::Max(D, (A[I].GetTranslation() - B[I].GetTranslation()).Size());
			D = FMath::Max(D, double(A[I].GetRotation().AngularDistance(B[I].GetRotation())) * 10.0);   // ~cm at 10 cm
		}
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderInterpolationTest, "SSX.Render.Interpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXRenderInterpolationTest::RunTest(const FString& Parameters)
{
	{
		const double Rates[] = { 144.0, 50.0, 30.0 };
		for (const double Hz : Rates)
		{
			FFixedStepClock C;
			int32 Hist[5] = {};
			double Phase = 0.0, LastPhase = 0.0;
			int32 Steps = 0;
			bool bEven = true;
			for (int32 F = 0; F < int32(Hz); ++F)
			{
				const int32 N = C.Advance(1.0 / Hz);
				++Hist[FMath::Clamp(N, 0, 4)];
				Steps += N;
				Phase = Steps + C.Alpha();
				if (F > 0 && FMath::Abs((Phase - LastPhase) - 60.0 / Hz) > 1e-6) { bEven = false; }
				LastPhase = Phase;
			}
			AddInfo(FString::Printf(TEXT("%.0f Hz render, 1 s: frames with 0/1/2 steps = %d/%d/%d, %d steps; present-last-step froze the pose on %d frames"),
				Hz, Hist[0], Hist[1], Hist[2], Steps, Hist[0]));
			TestTrue(*FString::Printf(TEXT("%.0f Hz: the drawn sim time advances evenly (%.4f steps per frame)"), Hz, 60.0 / Hz), bEven);
		}
	}

	if (!SSXAnim::FAflLibrary::Shared())
	{
		SSXTest::Skip(*this, TEXT("SimData/anim_clips.bin not found: pose part skipped"));
		return true;
	}
	int32 First = INDEX_NONE;
	for (int32 I = 0; I < SSXCharacters::AllIds().Num() && First == INDEX_NONE; ++I)
	{
		if (SSXCharacters::LoadRiderMesh(SSXCharacters::IdForIndex(I))) { First = I; }
	}
	if (First == INDEX_NONE)
	{
		SSXTest::Skip(*this, TEXT("no imported riders under /Game/Characters: pose part skipped"));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXRenderInterpWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	ASnowboarderPawn* Pawn = World->SpawnActor<ASnowboarderPawn>();
	USSXCharacterAppearance* Appearance = Pawn ? Pawn->GetAppearance() : nullptr;
	if (!TestNotNull(TEXT("appearance"), Appearance))
	{
		return false;
	}
	USceneComponent* RiderRoot = nullptr;
	USceneComponent* BodyRoot = nullptr;
	TArray<USceneComponent*> Scene;
	Pawn->GetComponents(Scene);
	for (USceneComponent* C : Scene)
	{
		if (C->GetFName() == TEXT("RiderRoot")) { RiderRoot = C; }
		if (C->GetFName() == TEXT("BodyRoot")) { BodyRoot = C; }
	}
	if (!RiderRoot || !BodyRoot)
	{
		AddError(TEXT("pawn roots missing"));
		return false;
	}
	Appearance->Apply(RiderRoot, BodyRoot, SSXCharacters::IdForIndex(First), First, 2);
	UPoseableMeshComponent* Mesh = Appearance->GetRiderMesh();
	if (!TestNotNull(TEXT("rider mesh"), Mesh))
	{
		return false;
	}

	FPatchWorld Slope;
	SSXRiderSetup::BuildSlopeWorld(Slope, 15.0, 1);
	auto MakeSim = [&](FRiderSim& Sim)
	{
		Sim.SetWorld(&Slope);
		Sim.Profile.BoardClass = 2;
		Sim.Profile.Character = First;
		RegisterGroundStates(Sim);
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.BoardClass = 2;
	};
	auto Input = [](int32 Step) { FRiderInputWord In; In.Turn = (Step / 40) % 2 ? -0.8 : 0.8; return In; };

	const double Rates[] = { 144.0, 50.0, 30.0 };
	for (const double Hz : Rates)
	{
		FRiderSim Sim, Twin;
		MakeSim(Sim);
		MakeSim(Twin);
		SSXAnim::FRiderAnimDriver TwinAnim;
		TwinAnim.Init(SSXAnim::FAflLibrary::Shared(), SSXCharacters::IdForIndex(First), 2, FMath::Clamp(Sim.Profile.GetTrickCharacter(), 0, 11));
		Appearance->ResetPoseHistory();
		FFixedStepClock Clock;
		int32 StepIndex = 0, Frames = 0, Moved = 0, OldFrozen = 0;
		TArray<FTransform> Last;
		for (int32 F = 0; F < int32(Hz) * 2; ++F)
		{
			const int32 N = Clock.Advance(1.0 / Hz);
			for (int32 S = 0; S < N; ++S)
			{
				const FRiderInputWord In = Input(StepIndex++);
				Sim.Step(In);
				Appearance->StepAnimation(Sim);
				Twin.Step(In);
				TwinAnim.Step(Twin);
				TwinAnim.FeedPose(Twin, true);
				if (S >= N - 2)
				{
					Appearance->CaptureStepPose(Sim, Sim.State.Position, Sim.State.Orientation, false);
				}
			}
			if (StepIndex < 2)
			{
				continue;
			}
			const double Alpha = Clock.Alpha();
			Appearance->PresentPose(Alpha);
			const TArray<FTransform> Drawn = Mesh->BoneSpaceTransforms;
			if (Last.Num() > 0)
			{
				++Frames;
				Moved += PoseDelta(Drawn, Last) > 1e-5 ? 1 : 0;
				OldFrozen += N == 0 ? 1 : 0;
			}
			Last = Mesh->BoneSpaceTransforms;
		}
		AddInfo(FString::Printf(TEXT("%.0f Hz: %d frames, drawn pose moved on %d (present-last-step would have frozen %d), %d sim steps"),
			Hz, Frames, Moved, OldFrozen, StepIndex));
		TestEqual(*FString::Printf(TEXT("%.0f Hz: the drawn pose moves on every render frame"), Hz), Moved, Frames);
		Appearance->PresentPose(1.0);
		const TArray<FTransform> AtOne = Mesh->BoneSpaceTransforms;
		Appearance->ApplyAnimatedPose(Sim, Sim.State.Position, Sim.State.Orientation);
		TestTrue(*FString::Printf(TEXT("%.0f Hz: alpha 1 draws the step's own pose"), Hz), PoseDelta(AtOne, Mesh->BoneSpaceTransforms) < 1e-6);
		const bool bSame = Sim.State.Position == Twin.State.Position && Sim.State.Velocity == Twin.State.Velocity
			&& Sim.State.Orientation == Twin.State.Orientation;
		TestTrue(*FString::Printf(TEXT("%.0f Hz: sim bit-identical with and without the presentation"), Hz), bSame);
	}
	return true;
}

#endif
