#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Race/GameRng.h"
#include "Race/RaceRules.h"
#include "Race/SSXRaceManager.h"
#include "Race/SSXReplay.h"
#include "SnowboarderPawn.h"
#include "Sim/RiderSim.h"
#include "Tests/AutomationCommon.h"
#include "UI/SSXFeTypes.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXPlayerController.h"
#include "UI/SSXRaceOverlays.h"
#include "World/SSXPropAnimation.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

namespace SSXReplayStart
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	constexpr int32 Samples = 200;

	UWorld* GameWorld()
	{
		for (const FWorldContext& C : GEngine->GetWorldContexts())
		{
			if ((C.WorldType == EWorldType::PIE || C.WorldType == EWorldType::Game) && C.World())
			{
				return C.World();
			}
		}
		return nullptr;
	}

	struct FSample
	{
		int32 Frame = 0;
		int32 Phase = 0;
		double Clock = 0.0;
		bool bOverlay = false;
		int32 Digit = 0;
		bool bGo = false;
		TArray<int32> Control;
		TArray<double> Gate;
		TArray<int32> Lights;
		FString Str() const
		{
			FString S = FString::Printf(TEXT("frame %d ph %d clock %.0f ov %d digit %d go %d ctl"), Frame, Phase, Clock, bOverlay ? 1 : 0, Digit, bGo ? 1 : 0);
			for (const int32 C : Control) S += FString::Printf(TEXT(" %d"), C);
			S += TEXT(" gate");
			for (const double G : Gate) S += FString::Printf(TEXT(" %.3f"), G);
			int32 Sum = 0;
			for (const int32 L : Lights) Sum = Sum * 31 + L;
			return S + FString::Printf(TEXT(" lights %08x"), Sum);
		}
	};

	struct FRun
	{
		int32 Stage = 0;
		double WallStart = 0.0;
		int32 Frames = 0;
		int32 LastFrame = -1;
		uint32 LastPlay = MAX_uint32;
		uint32 MenuFrame = 0;
		bool bPrevFixed = false;
		double PrevFixedDt = 0.0;
		TArray<FSample> Live;
		TArray<FSample> Play;
	};

	FSample Take(ASSXRaceManager& Race, FSSXOverlayManager& O)
	{
		O.SetHost(&Race.GetUIHost());
		O.Tick();
		FSample S;
		const FRaceSession& Ses = *Race.GetSession();
		S.Frame = int32(Ses.GetFrame());
		S.Phase = int32(Ses.GetPhase());
		S.Clock = Ses.GetClock();
		S.bOverlay = O.IsActive(OvCountdown);
		S.Digit = O.GetCountdownDigit();
		S.bGo = O.IsGoShowing();
		for (int32 I = 0; I < Ses.NumRacers(); ++I)
		{
			const ASnowboarderPawn* P = Race.GetPawn(I);
			S.Control.Add(P && P->HasSim() ? int32(P->GetSim()->State.Control) : -1);
			S.Gate.Add(P && P->HasSim() ? P->GetSim()->State.GateLaunch.Cur : 0.0);
		}
		if (USSXPropAnimation* Pa = USSXPropAnimation::Get(&Race))
		{
			const SSXProps::FPropControllers& C = Pa->GetControllers();
			for (int32 I = 0; I < C.Num(); ++I)
			{
				if (C.Get(I).HasFlip()) S.Lights.Add(C.Get(I).Flip.Frame);
			}
		}
		return S;
	}

	class FCommand : public IAutomationLatentCommand
	{
	public:
		FCommand(FAutomationTestBase* InTest, TSharedRef<FRun> InRun) : Test(InTest), Run(InRun) {}
		virtual bool Update() override;
	private:
		bool Finish(const FString& Failure = FString())
		{
			if (!Failure.IsEmpty()) { Test->AddError(FString::Printf(TEXT("[stage %d] %s"), Run->Stage, *Failure)); }
			FApp::SetUseFixedTimeStep(Run->bPrevFixed);
			FApp::SetFixedDeltaTime(Run->PrevFixedDt);
			if (GEditor && GEditor->PlayWorld) { GEditor->RequestEndPlayMap(); }
			return true;
		}
		void Compare();
		FAutomationTestBase* Test;
		TSharedRef<FRun> Run;
	};

	bool FCommand::Update()
	{
		FRun& R = *Run;
		if (FPlatformTime::Seconds() - R.WallStart > 900.0) return Finish(TEXT("timed out"));
		UWorld* World = GameWorld();
		USSXFrontEndSubsystem* FE = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
		const FString Map = World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString();
		ASSXRaceManager* Race = World ? ASSXRaceManager::Find(World) : nullptr;
		ASSXPlayerController* PC = World ? Cast<ASSXPlayerController>(World->GetFirstPlayerController()) : nullptr;
		FSSXOverlayManager* O = PC && PC->GetOverlayWidget() ? &PC->GetOverlayWidget()->GetOverlays() : nullptr;
		switch (R.Stage)
		{
		case 0:
		{
			if (!FE || Map != TEXT("FrontEnd") || !World->HasBegunPlay()) return false;
			FSSXFeLaunch L;
			L.GameMode = int32(ERaceEventMode::CircuitRace);
			L.Track = 0;
			L.Slots[0].Char = 4;
			L.Slots[0].Board = 0;
			if (FE->RequestLaunch(L) != ESSXLoadResult::Loading) return Finish(TEXT("RequestLaunch(gari)"));
			R.Stage = 1;
			return false;
		}
		case 1:
		{
			if (!Race || Map != TEXT("Gari") || !Race->GetSession() || !O) return false;
			const FRaceSession& Ses = *Race->GetSession();
			if (Ses.GetPhase() == ERacePhase::PreRace && ++R.Frames > 90) { Race->GetUIHost().PressIntroSkip(); }
			const FSSXReplay& Rp = Race->GetReplay();
			if (int32(Ses.GetPhase()) < int32(ERacePhase::Countdown) || int32(Ses.GetFrame()) == R.LastFrame) return false;
			R.LastFrame = Ses.GetFrame();
			R.Live.Add(Take(*Race, *O));
			if (Rp.GetRecordFrame() < uint32(Samples + 40)) return false;
			Race->GetReplayMutable().EndRecording();
			R.Stage = 2;
			return false;
		}
		case 2:
		{
			if (!Race || !O) return Finish(TEXT("race gone"));
			if (Race->GetReplay().GetState() != SSXReplay::RecordEnded) return false;
			if (!Race->StartResultsReplay()) return Finish(TEXT("StartResultsReplay"));
			Race->GetReplayMutable().SetCommand(SSXReplay::CmdToStart);
			R.Stage = 3;
			R.Frames = 0;
			return false;
		}
		case 3:
			if (!Race || !O) return Finish(TEXT("race gone"));
			if (Race->GetReplay().GetState() != SSXReplay::Paused) return ++R.Frames > 600 ? Finish(TEXT("never paused at frame 0")) : false;
			Race->GetReplayMutable().SetCommand(SSXReplay::CmdPlayToggle);
			R.Stage = 4;
			return false;
		case 4:
		{
			if (!Race || !O) return Finish(TEXT("race gone"));
			const FSSXReplay& Rp = Race->GetReplay();
			if (Rp.GetState() != SSXReplay::Play || Rp.GetPlayFrame() == R.LastPlay) return false;
			if (R.LastPlay != MAX_uint32 && Rp.GetPlayFrame() != R.LastPlay + 1)
			{
				Test->AddWarning(FString::Printf(TEXT("replay frame %u after %u (more than one world frame per tick)"), Rp.GetPlayFrame(), R.LastPlay));
			}
			R.LastPlay = Rp.GetPlayFrame();
			R.Play.Add(Take(*Race, *O));
			if (R.Play.Num() < Samples) return false;
			Compare();
			// 0x1EFDB0 / 0x1EF990(2): the replay menu is modal, and a modal overlay pauses the game (0x17F078 / 0x1A77F8)
			Race->GetUIHost().SetReplayMenu(true);
			R.MenuFrame = Rp.GetPlayFrame();
			R.Frames = 0;
			R.Stage = 5;
			return false;
		}
		case 5:
			if (!Race) return Finish(TEXT("race gone"));
			if (++R.Frames < 30) return false;
			Test->TestEqual(TEXT("the replay holds its frame while the replay menu is open"), Race->GetReplay().GetPlayFrame(), R.MenuFrame);
			Race->GetUIHost().SetReplayMenu(false);   // 0x1EF990(3): continue
			R.Frames = 0;
			R.Stage = 6;
			return false;
		case 6:
			if (!Race) return Finish(TEXT("race gone"));
			if (Race->GetReplay().GetPlayFrame() > R.MenuFrame) return Finish();
			return ++R.Frames > 120 ? Finish(TEXT("the replay did not continue after the menu closed")) : false;
		default:
			return Finish(TEXT("bad stage"));
		}
	}

	void FCommand::Compare()
	{
		const FRun& R = *Run;
		int32 Matched = 0, LiveDigits = 0, LiveGo = 0, PlayOverlay = 0, Mismatch = 0, Gate = 0, Launch = 0, Rock = 0;
		bool bLightsEqual = true;
		for (int32 I = 0; I < R.Play.Num(); ++I)
		{
			const FSample& P = R.Play[I];
			const FSample* L = R.Live.FindByPredicate([&P](const FSample& S) { return S.Frame == P.Frame; });
			UE_LOG(LogTemp, Display, TEXT("[replaystart] r%03d play: %s | live: %s"), I, *P.Str(), L ? *L->Str() : TEXT("-"));
			PlayOverlay += (P.bOverlay || P.bGo || P.Digit != 0) ? 1 : 0;
			Gate += P.Control.Num() > 0 && P.Control[0] == int32(ERiderControl::GateAnticipate) ? 1 : 0;
			Launch += P.Control.Num() > 0 && P.Control[0] == int32(ERiderControl::GateLaunch) ? 1 : 0;
			if (!L) continue;
			++Matched;
			LiveDigits += L->bOverlay && L->Digit != 0 ? 1 : 0;
			LiveGo += L->bGo ? 1 : 0;
			bLightsEqual &= L->Lights == P.Lights;
			if (L->Phase != P.Phase || L->Clock != P.Clock || L->Control != P.Control || L->Gate != P.Gate) { ++Mismatch; }
			if (I > 0 && P.Phase == int32(ERacePhase::Countdown) && P.Gate != R.Play[I - 1].Gate) { ++Rock; }
		}
		const FSample& F0 = R.Play[0];
		Test->AddInfo(FString::Printf(TEXT("replay frame 0: %s; %d frames lined up with the live race, %d mismatched, start lights %s"),
			*F0.Str(), Matched, Mismatch, bLightsEqual ? TEXT("equal") : TEXT("differ")));
		Test->TestTrue(TEXT("the replay starts in the countdown (keyframe 0: phase 3, clock < 0)"), F0.Phase == int32(ERacePhase::Countdown) && F0.Clock < 0.0);
		Test->TestTrue(TEXT("every replay frame lines up with a live frame"), Matched == R.Play.Num());
		Test->TestEqual(TEXT("the replay's countdown / GO / launch frames are the live race's (phase, clock, racer controls, gate rock)"), Mismatch, 0);
		Test->TestTrue(TEXT("replay: the gate rock moves during the countdown"), Rock > 0);
		Test->TestTrue(TEXT("the start lights (CountDownStart TexFlip frames) change as live"), bLightsEqual);
		Test->TestTrue(TEXT("live: the countdown digits and GO were shown (0x113520 opened slot 4)"), LiveDigits > 0 && LiveGo > 0);
		Test->TestEqual(TEXT("replay: no countdown digits / GO (the restore runs no 0x113520)"), PlayOverlay, 0);
		Test->TestTrue(TEXT("replay: the racers rock in the gates (GateAnticipate) and launch at GO (GateLaunch)"), Gate > 100 && Launch > 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayStartInGameTest, "SSX.Replay.StartInGame", SSXReplayStart::Flags)
bool FSSXReplayStartInGameTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayStart;
	const FString Sim = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"));
	for (const TCHAR* F : { TEXT("gari.patches.bin"), TEXT("gari.race.bin"), TEXT("gari.effects.bin"), TEXT("anim_clips.bin") })
	{
		if (!FPaths::FileExists(FPaths::Combine(Sim, F))) { SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: SimData/%s missing"), F)); return true; }
	}
	for (const TCHAR* M : { TEXT("Gari"), TEXT("FrontEnd") })
	{
		if (!FPaths::FileExists(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Maps"), FString(M) + TEXT(".umap"))))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: Maps/%s missing"), M));
			return true;
		}
	}
	TSharedRef<FRun> Run = MakeShared<FRun>();
	Run->WallStart = FPlatformTime::Seconds();
	FGameRng::StreamA() = FGameRng(0x5EED1234u);
	FGameRng::StreamB() = FGameRng();
	Run->bPrevFixed = FApp::UseFixedTimeStep();
	Run->PrevFixedDt = FApp::GetFixedDeltaTime();
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / 60.0);
	AutomationOpenMap(TEXT("/Game/Maps/FrontEnd"), true);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, Run));
	return true;
}

#endif
