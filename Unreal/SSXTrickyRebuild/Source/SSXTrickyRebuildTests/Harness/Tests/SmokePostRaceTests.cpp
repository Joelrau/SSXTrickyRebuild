// 0x11DA40, 0x113820, 0x113E10, 0x1141E0, 0x1B0BB8
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Race/RaceSession.h"
#include "Race/SSXRaceManager.h"
#include "Tests/AutomationCommon.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXMemCard.h"
#include "UI/SSXPlayerController.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

namespace SSXSmokePostRace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UWorld* GameWorld()
	{
		for (const FWorldContext& C : GEngine->GetWorldContexts())
		{
			if ((C.WorldType == EWorldType::PIE || C.WorldType == EWorldType::Game) && C.World()) return C.World();
		}
		return nullptr;
	}

	FString MapOf(const UWorld* W) { return W ? UWorld::RemovePIEPrefix(W->GetMapName()) : FString(); }

	enum class EEnd : uint8 { TimeUp, Finish };

	struct FRun
	{
		FString Map;
		int32 TrackId = 2;
		ERaceEventMode Mode = ERaceEventMode::PracticeShowoff;
		EEnd End = EEnd::TimeUp;
		TArray<int32> ExpectItems;
		int32 Stage = 0;
		double WallStart = 0.0;
		FString PrevMemCard;
		bool bPrevFixed = false;
		double PrevFixedDt = 0.0;
		int32 PreRaceFrames = 0, RaceFrames = 0, EndFrames = 0, LastFrame = -1;
		bool bClockCut = false, bManualOverlays = false, bHudSeen = false, bFinished = false, bEndRace = false;
		int32 FinishCs = -1, MaxEndState = -1, ResultsFrame = -1;
		bool bChainOpen = false, bPostReady = false, bChainStepDropped = false;
		double ResultsScore = -1.0;
		ERacePhase LastPhase = ERacePhase::PreRace;
		FString Failure, Trace;
	};

	FString ActiveList(const FSSXOverlayManager& O)
	{
		FString S;
		for (int32 I = 0; I < OvCount; ++I) { if (O.IsActive(I)) S += FString::Printf(TEXT("%d "), I); }
		return S;
	}

	class FPostRaceCommand : public IAutomationLatentCommand
	{
	public:
		FPostRaceCommand(FAutomationTestBase* InTest, TSharedRef<FRun> InRun) : Test(InTest), Run(InRun) {}
		virtual bool Update() override;
	private:
		bool Done(const FString& Fail = FString());
		FAutomationTestBase* Test;
		TSharedRef<FRun> Run;
	};

	bool FPostRaceCommand::Done(const FString& Fail)
	{
		FRun& R = *Run;
		if (!Fail.IsEmpty()) R.Failure = Fail;
		Test->AddInfo(FString::Printf(TEXT("[%s mode %d] finish %d (+0x418), end-race max state %d, chain %d, post-race ready %d, results at end-race frame %d, overlays stepped by %s; %s"),
			*R.Map, int32(R.Mode), R.FinishCs, R.MaxEndState, R.bChainOpen ? 1 : 0, R.bPostReady ? 1 : 0, R.ResultsFrame,
			R.bManualOverlays ? TEXT("the test") : TEXT("the widget"), *R.Trace));
		if (!R.Failure.IsEmpty()) Test->AddError(R.Failure);
		FSSXMemCard::RootOverride() = R.PrevMemCard;
		FApp::SetUseFixedTimeStep(R.bPrevFixed);
		FApp::SetFixedDeltaTime(R.PrevFixedDt);
		return true;
	}

	bool FPostRaceCommand::Update()
	{
		FRun& R = *Run;
		if (FPlatformTime::Seconds() - R.WallStart > 600.0) return Done(FString::Printf(TEXT("timed out in stage %d"), R.Stage));
		UWorld* World = GameWorld();
		if (R.Stage == 0)
		{
			USSXFrontEndSubsystem* FE = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
			if (!FE || MapOf(World) != TEXT("FrontEnd") || !World->HasBegunPlay()) return false;
			FSSXFeLaunch L;
			L.GameMode = int32(R.Mode);
			L.Track = R.TrackId;
			L.Slots[0].Char = 4;
			L.Slots[0].Board = 0;
			const ESSXLoadResult Res = FE->RequestLaunch(L);
			if (Res != ESSXLoadResult::Loading) return Done(FString::Printf(TEXT("RequestLaunch = %d"), int32(Res)));
			R.Stage = 1;
			return false;
		}
		if (!World || MapOf(World) != R.Map) return false;
		ASSXRaceManager* Race = ASSXRaceManager::Find(World);
		ASSXPlayerController* PC = Cast<ASSXPlayerController>(World->GetFirstPlayerController());
		USSXFeWidget* W = PC ? PC->GetOverlayWidget() : nullptr;
		const FRaceSession* S = Race ? Race->GetSession() : nullptr;
		if (!S || !W || !Race->GetPlayerPawn()) return false;
		if (S->GetFrame() == R.LastFrame) return false;
		R.LastFrame = S->GetFrame();
		FSSXOverlayManager& O = W->GetOverlays();
		const bool bChainFrame = S->GetPhase() == ERacePhase::EndRace && S->GetEndRace().IsChainOpen() && !R.bChainStepDropped;
		R.bChainStepDropped |= bChainFrame;
		if (R.bManualOverlays && !bChainFrame)
		{
			O.SetHost(&Race->GetUIHost());
			O.Tick();
		}
		const ERacePhase Ph = S->GetPhase();
		const int32 Player = Race->GetPlayerRacer(0);
		if (Ph == ERacePhase::PreRace)
		{
			if (++R.PreRaceFrames > 90 && R.PreRaceFrames % 30 == 0) Race->GetUIHost().PressIntroSkip();
			return false;
		}
		if (Ph == ERacePhase::Race && !R.bEndRace)
		{
			++R.RaceFrames;
			if (R.RaceFrames == 20 && !Race->GetUIHost().IsAsyncLoading()) O.Activate(OvPopAsyncLoad);
			if (R.RaceFrames == 30 && O.IsActive(OvPopAsyncLoad))
			{
				R.bManualOverlays = true;
				O.Deactivate(OvPopAsyncLoad);
			}
			if (R.RaceFrames == 120 && !R.bClockCut)
			{
				R.bClockCut = true;
				FRaceSession& Mut = const_cast<FRaceSession&>(*S);
				if (R.End == EEnd::TimeUp)
				{
					FRaceSessionKeyframe K;
					Mut.SaveReplay(K);
					K.Clock = 1.0;
					Mut.LoadReplay(K);
				}
				else
				{
					Mut.StampFinish(Player);
				}
			}
			if (S->GetRacer(Player).IsFinished() && !R.bFinished)
			{
				R.bFinished = true;
				R.FinishCs = S->GetRacer(Player).FinishCs();
			}
			if (R.RaceFrames > 60 * 60) return Done(TEXT("the race did not end within 60 s of race time"));
			return false;
		}
		if (Ph == ERacePhase::EndRace || R.bEndRace)
		{
			if (!R.bEndRace || Ph != R.LastPhase) R.Trace += FString::Printf(TEXT("f%d phase %d overlays %s; "), R.EndFrames, int32(Ph), *ActiveList(O));
			R.LastPhase = Ph;
			R.bEndRace = true;
			++R.EndFrames;
			// 0x110AB8
			if (R.End == EEnd::TimeUp && R.EndFrames == 240) O.Pad(0, SSXFe::PadCross);
			const int32 St = S->GetEndRace().GetState();
			if (St != R.MaxEndState && St > R.MaxEndState) R.Trace += FString::Printf(TEXT("f%d state %d; "), R.EndFrames, St);
			R.MaxEndState = FMath::Max(R.MaxEndState, St);
			R.bChainOpen |= S->GetEndRace().IsChainOpen();
			R.bPostReady |= Race->GetUIHost().IsPostRaceReady();
			if (R.ResultsFrame >= 0)
			{
				if (!O.IsActive(OvRaceResults)) return Done(FString::Printf(TEXT("results overlay closed %d frames after it opened (phase %d, overlays %s)"), R.EndFrames - R.ResultsFrame, int32(Ph), *ActiveList(O)));
				return R.EndFrames - R.ResultsFrame >= 60 * 40 ? Done() : false;
			}
			if (O.IsActive(OvRaceResults))
			{
				R.ResultsFrame = R.EndFrames;
				const TArray<int32> Items = O.ResultsItems();
				FString It;
				for (int32 I : Items) It += FString::Printf(TEXT("%d "), I);
				R.Trace += FString::Printf(TEXT("results items %s; "), *It);
				if (Items != R.ExpectItems) R.Failure = FString::Printf(TEXT("results menu items %s"), *It);
				if (S->GetFinalResults().Num() > 0) R.ResultsScore = S->GetFinalResults()[0].Score;
				if (R.End == EEnd::TimeUp && R.ResultsScore != 0.0) R.Failure += FString::Printf(TEXT(" time-up results score %.0f (expected 0)"), R.ResultsScore);
				if (R.End == EEnd::TimeUp && R.FinishCs != 0) R.Failure += FString::Printf(TEXT(" time-up +0x418 %d (expected 0)"), R.FinishCs);
				if (R.End == EEnd::Finish && R.FinishCs <= 0) R.Failure += FString::Printf(TEXT(" finish +0x418 %d (expected > 0)"), R.FinishCs);
				return false;
			}
			if (R.ResultsFrame < 0 && R.EndFrames > 60 * 60)
			{
				return Done(FString::Printf(TEXT("results overlay not up 60 s after the end-race enter: end-race state %d, chain %d, post-race ready %d, overlays %s"),
					St, S->GetEndRace().IsChainOpen() ? 1 : 0, Race->GetUIHost().IsPostRaceReady() ? 1 : 0, *ActiveList(O)));
			}
			return false;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEndPie, int32, Unused);
	bool FEndPie::Update()
	{
		if (GEditor && GEditor->PlayWorld) GEditor->RequestEndPlayMap();
		return true;
	}

	void Queue(FAutomationTestBase* Test, const FString& Track, const FString& Map, int32 TrackId, ERaceEventMode Mode, EEnd End, TArray<int32> Items)
	{
		const FString Sim = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"));
		for (const FString& F : { Track + TEXT(".patches.bin"), Track + TEXT(".race.bin"), FString(TEXT("anim_clips.bin")) })
		{
			if (!FPaths::FileExists(FPaths::Combine(Sim, F))) { SSXTest::Skip(*Test, TEXT("SKIPPED: missing SimData/") + F); return; }
		}
		for (const FString& Name : { Map, FString(TEXT("FrontEnd")) })
		{
			if (!FPaths::FileExists(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Maps"), Name + TEXT(".umap")))) { SSXTest::Skip(*Test, TEXT("SKIPPED: missing Maps/") + Name); return; }
		}
		TSharedRef<FRun> Run = MakeShared<FRun>();
		Run->Map = Map;
		Run->TrackId = TrackId;
		Run->Mode = Mode;
		Run->End = End;
		Run->ExpectItems = MoveTemp(Items);
		Run->WallStart = FPlatformTime::Seconds();
		Run->PrevMemCard = FSSXMemCard::RootOverride();
		FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardPostRace"));
		IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
		Run->bPrevFixed = FApp::UseFixedTimeStep();
		Run->PrevFixedDt = FApp::GetFixedDeltaTime();
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / 60.0);
		AutomationOpenMap(TEXT("/Game/Maps/FrontEnd"), true);
		ADD_LATENT_AUTOMATION_COMMAND(FPostRaceCommand(Test, Run));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPie(0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokePostRaceShowoffTimeUpTest, "SSX.Smoke.PostRace.ShowoffTimeUp", SSXSmokePostRace::Flags)
bool FSSXSmokePostRaceShowoffTimeUpTest::RunTest(const FString& Parameters)
{
	SSXSmokePostRace::Queue(this, TEXT("elysium"), TEXT("Elysium"), 2, ERaceEventMode::PracticeShowoff, SSXSmokePostRace::EEnd::TimeUp, { 0, 2, 1, 4, 3 });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokePostRaceWCShowoffTimeUpTest, "SSX.Smoke.PostRace.WCShowoffTimeUp", SSXSmokePostRace::Flags)
bool FSSXSmokePostRaceWCShowoffTimeUpTest::RunTest(const FString& Parameters)
{
	SSXSmokePostRace::Queue(this, TEXT("elysium"), TEXT("Elysium"), 2, ERaceEventMode::CircuitShowoff, SSXSmokePostRace::EEnd::TimeUp, { 0, 2, 1, 4 });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokePostRaceShowoffFinishTest, "SSX.Smoke.PostRace.ShowoffFinish", SSXSmokePostRace::Flags)
bool FSSXSmokePostRaceShowoffFinishTest::RunTest(const FString& Parameters)
{
	SSXSmokePostRace::Queue(this, TEXT("elysium"), TEXT("Elysium"), 2, ERaceEventMode::PracticeShowoff, SSXSmokePostRace::EEnd::Finish, { 0, 2, 1, 4, 3 });
	return true;
}

#endif
