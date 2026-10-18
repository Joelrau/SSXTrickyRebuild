#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Race/GameRng.h"
#include "Race/SSXReplay.h"
#include "Race/RaceRules.h"
#include "Race/SSXRaceManager.h"
#include "Race/SSXReplayFile.h"
#include "Sim/RiderSim.h"
#include "SnowboarderPawn.h"
#include "Tests/AutomationCommon.h"
#include "UI/SSXFeTypes.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXPlayerController.h"
#include "UI/SSXRaceOverlays.h"
#include "World/SSXRenderLook.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

namespace SSXReplayRerun
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	constexpr uint32 LiveFrames = 2400;

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

	struct FRiderFrame
	{
		FVector P, V;
		FQuat Q;
		uint8 Motion = 0, Control = 0;
		uint32 Word = 0;
	};
	struct FFrame
	{
		TArray<FRiderFrame> Riders;
		uint32 RngA[6] = {}, RngB[6] = {};
		double Clock = 0.0;
		int32 Phase = 0, RaceFrame = 0;
	};
	using FTrace = TMap<uint32, FFrame>;

	FFrame Capture(ASSXRaceManager& Race)
	{
		FFrame F;
		const int32 N = Race.GetSession() ? Race.GetSession()->NumRacers() : 0;
		for (int32 I = 0; I < N; ++I)
		{
			FRiderFrame R;
			const ASnowboarderPawn* P = Race.GetPawn(I);
			if (P && P->HasSim())
			{
				const FRiderState& S = P->GetSim()->State;
				R.P = S.Position; R.V = S.Velocity; R.Q = S.Orientation;
				R.Motion = uint8(S.Motion); R.Control = uint8(S.Control);
				R.Word = P->bAIRider ? 0u : P->GetLastWord();
			}
			F.Riders.Add(R);
		}
		FMemory::Memcpy(F.RngA, FGameRng::StreamA().S, sizeof(F.RngA));
		FMemory::Memcpy(F.RngB, FGameRng::StreamB().S, sizeof(F.RngB));
		if (const FRaceSession* S = Race.GetSession()) { F.Clock = S->GetClock(); F.Phase = int32(S->GetPhase()); F.RaceFrame = S->GetFrame(); }
		return F;
	}

	FString Diff(const FFrame& A, const FFrame& B)
	{
		if (FMemory::Memcmp(A.RngB, B.RngB, sizeof(A.RngB)) != 0) return TEXT("random stream B");
		for (int32 I = 0; I < FMath::Min(A.Riders.Num(), B.Riders.Num()); ++I)
		{
			const FRiderFrame& X = A.Riders[I];
			const FRiderFrame& Y = B.Riders[I];
			if (X.Word != Y.Word) return FString::Printf(TEXT("racer %d word %08X vs %08X"), I, X.Word, Y.Word);
			if (X.Motion != Y.Motion || X.Control != Y.Control)
				return FString::Printf(TEXT("racer %d motion/control %d/%d vs %d/%d"), I, X.Motion, X.Control, Y.Motion, Y.Control);
			if (X.P != Y.P) return FString::Printf(TEXT("racer %d position (%s) vs (%s), %.4f cm"), I, *X.P.ToString(), *Y.P.ToString(), FVector::Dist(X.P, Y.P));
			if (X.V != Y.V) return FString::Printf(TEXT("racer %d velocity (%s) vs (%s)"), I, *X.V.ToString(), *Y.V.ToString());
			if (!(X.Q == Y.Q)) return FString::Printf(TEXT("racer %d orientation"), I);
		}
		if (A.Riders.Num() != B.Riders.Num()) return TEXT("racer count");
		if (A.RaceFrame != B.RaceFrame) return FString::Printf(TEXT("race frame counter (cAI +0x18) %d vs %d"), A.RaceFrame, B.RaceFrame);
		if (A.Phase != B.Phase || A.Clock != B.Clock) return FString::Printf(TEXT("race phase/clock %d %.4f vs %d %.4f"), A.Phase, A.Clock, B.Phase, B.Clock);
		return FString();
	}

	void Script(uint32 N, FSSXPadHardware& Pad)
	{
		Pad = FSSXPadHardware();
		const double T = double(N);
		Pad.LX = uint8(FMath::Clamp(128.0 + 110.0 * FMath::Sin(T * 0.017) + 60.0 * FMath::Sin(T * 0.091), 0.0, 255.0));
		Pad.LY = uint8(FMath::Clamp(128.0 + 90.0 * FMath::Sin(T * 0.0071), 0.0, 255.0));
		const uint32 Cyc = N % 173;
		if (Cyc < 45) Pad.Buttons |= SSXPadButton::Cross;
		if (Cyc >= 50 && Cyc < 95)
		{
			Pad.Buttons |= ((N / 173) & 1) ? SSXPadButton::R1 : (SSXPadButton::L1 | SSXPadButton::R2);
			Pad.LX = ((N / 173) % 3 == 0) ? 0 : 255;
		}
		if (N % 311 >= 200 && N % 311 < 260) Pad.Buttons |= SSXPadButton::Square;
		if (N % 97 < 6) Pad.Buttons |= SSXPadButton::Circle;
	}

	void ContactScript(uint32 N, FSSXPadHardware& Pad, const ASSXRaceManager* Race)
	{
		Pad = FSSXPadHardware();
		const ASnowboarderPawn* Me = Race ? Race->GetPlayerPawn() : nullptr;
		if (!Me || !Me->HasSim())
		{
			return;
		}
		const FRiderState& S = Me->GetSim()->State;
		const FVector Fwd = S.Velocity.Size2D() > 1.0 ? S.Velocity.GetSafeNormal2D() : S.Orientation.RotateVector(FVector(0, 1, 0)).GetSafeNormal2D();
		double Best = 4000.0, Side = 0.0;
		for (int32 I = 0; Race->GetPawn(I); ++I)
		{
			const ASnowboarderPawn* O = Race->GetPawn(I);
			if (!O->bAIRider || !O->HasSim()) continue;
			const FVector D = (O->GetSim()->State.Position - S.Position) * FVector(1, 1, 0);
			const double L = D.Size();
			if (L < Best && FVector::DotProduct(D, Fwd) > -150.0)
			{
				Best = L;
				Side = (Fwd.X * D.Y - Fwd.Y * D.X) / FMath::Max(L, 1.0);
			}
		}
		const uint32 Wall = N % 900;
		if (Wall >= 700 && Wall < 760)
		{
			Pad.LX = ((N / 900) & 1) ? 0 : 255;
		}
		else if (Best < 4000.0)
		{
			Pad.LX = uint8(FMath::Clamp(128.0 - Side * 600.0, 0.0, 255.0));
			if (Best < 400.0 && (N & 15) < 8)
			{
				Pad.RX = Side > 0.0 ? 0 : 255;
				Pad.RY = ((N >> 4) % 3 == 0) ? 0 : ((N >> 4) % 3 == 1) ? 255 : 128;
			}
			if (Best > 700.0) Pad.Buttons |= SSXPadButton::Square;
		}
		else
		{
			Pad.LX = uint8(FMath::Clamp(128.0 + 100.0 * FMath::Sin(double(N) * 0.021), 0.0, 255.0));
			if (N % 311 < 120) Pad.Buttons |= SSXPadButton::Square;
		}
		const uint32 Cyc = N % 420;
		if (Cyc >= 300 && Cyc < 340) Pad.Buttons |= SSXPadButton::Cross;
		if (Cyc >= 345 && Cyc < 380) { Pad.Buttons |= SSXPadButton::R1; Pad.LX = ((N / 420) & 1) ? 0 : 255; }
	}

	struct FRun
	{
		bool bContact = false;
		bool bRestart = false;
		// > 0: flip ssx.Render.Look every LookPeriod sim frames, half a period later in playback than in the live race.
		uint32 LookPeriod = 0;
		// Run after this one (same set-up); Baseline: an earlier run whose live race this one must match bit for bit
		// (stream A, the presentation stream, is reported only: front-end and render timing differ between live runs).
		TSharedPtr<struct FRun> Next, Baseline;
		// Two live races that must match: skip the intro after a count of pre-race sim steps (not engine ticks, whose
		// pacing differs between runs) and start both from the same stream A.
		bool bSameStart = false;
		uint32 PreRaceSteps = 0;
		uint32 Need = LiveFrames;
		bool bRestarted = false;
		bool bEndWait = false;
		int32 PauseStage = 0, PauseTicks = 0;
		int32 Stage = 0;
		double WallStart = 0.0;
		int32 Ticks = 0, Frames = 0;
		uint32 PadSteps = 0;
		bool bHooked = false;
		FTrace Live, PlayA, Rebuild, PlayB, PlayKey;
		int32 SeekStage = 0, SeekJumps = 0;
		bool bSeekPass = false;
		TArray<uint8> File;
		bool bPrevFixed = false;
		double PrevFixedDt = 0.0;
		FSSXFeLaunch Launch;
		FString Report;
	};

	bool Compare(FAutomationTestBase* Test, const TCHAR* Name, const FTrace& Live, const FTrace& Rerun, uint32 Need, bool bErrors = true)
	{
		uint32 Matched = 0, FirstA = MAX_uint32;
		ON_SCOPE_EXIT { if (FirstA != MAX_uint32) { Test->AddInfo(FString::Printf(TEXT("%s: stream A (presentation) first differs at frame %u"), Name, FirstA)); } };
		for (uint32 F = 0; F < Need; ++F)
		{
			const FFrame* A = Live.Find(F);
			const FFrame* B = Rerun.Find(F);
			if (!A || !B)
			{
				const FString M = FString::Printf(TEXT("%s: frame %u missing (live %d, re-run %d)"), Name, F, A ? 1 : 0, B ? 1 : 0);
				if (bErrors) { Test->AddError(M); } else { Test->AddInfo(M); }
				return false;
			}
			if (FirstA == MAX_uint32 && FMemory::Memcmp(A->RngA, B->RngA, sizeof(A->RngA)) != 0) { FirstA = F; }
			const FString D = Diff(*A, *B);
			if (!D.IsEmpty())
			{
				const FString M = FString::Printf(TEXT("%s: first divergent frame %u: %s"), Name, F, *D);
				if (bErrors) { Test->AddError(M); } else { Test->AddInfo(M); }
				return false;
			}
			++Matched;
		}
		Test->AddInfo(FString::Printf(TEXT("%s: %u frames bit-exact"), Name, Matched));
		return true;
	}

	void QueueNext(FAutomationTestBase* Test, const TSharedRef<FRun>& Run);

	class FCommand : public IAutomationLatentCommand
	{
	public:
		FCommand(FAutomationTestBase* InTest, TSharedRef<FRun> InRun) : Test(InTest), Run(InRun) {}
		virtual bool Update() override;
	private:
		bool Finish(const FString& Failure = FString())
		{
			if (!Failure.IsEmpty()) { Test->AddError(FString::Printf(TEXT("[stage %d] %s"), Run->Stage, *Failure)); }
			if (Run->LookPeriod > 0) { SSXRenderLook::Set(ESSXRenderLook::Classic); }
			if (Failure.IsEmpty() && Run->Baseline.IsValid())
			{
				Compare(Test, TEXT("live race vs the baseline run"), Run->Baseline->Live, Run->Live, Run->Need);
			}
			FApp::SetUseFixedTimeStep(Run->bPrevFixed);
			FApp::SetFixedDeltaTime(Run->PrevFixedDt);
			if (GEditor && GEditor->PlayWorld) { GEditor->RequestEndPlayMap(); }
			if (Run->bSameStart) { ASSXRaceManager::TestBeforeSeed = nullptr; }
			if (Failure.IsEmpty() && Run->Next.IsValid()) { QueueNext(Test, Run); }
			return true;
		}
		void Hook(ASSXRaceManager* Race, bool bScript)
		{
			TSharedRef<FRun> R = Run;
			Race->TestOnFrame = [R](ASSXRaceManager& M)
			{
				const FSSXReplay& Rp = M.GetReplay();
				if (R->bSameStart && M.GetSession() && M.GetSession()->GetPhase() == ERacePhase::PreRace && ++R->PreRaceSteps % 700 == 0)
				{
					M.GetUIHost().PressIntroSkip();
				}
				if (const uint32 P = R->LookPeriod)
				{
					const bool bPlay = Rp.GetState() == SSXReplay::Play;
					const uint32 F = (bPlay ? Rp.GetPlayFrame() : Rp.GetRecordFrame()) + (bPlay ? P / 2 : 0);
					SSXRenderLook::Set((F / P) % 2 ? ESSXRenderLook::Remastered : ESSXRenderLook::Classic);
				}
				if (Rp.GetState() == SSXReplay::Record && Rp.GetRecordFrame() > 0)
				{
					if (Rp.IsRebuilding()) { R->Rebuild.Add(Rp.GetRecordFrame() - 1, Capture(M)); }
					else if (!Rp.IsEnded() && !Rp.IsLoaded()) { R->Live.Add(Rp.GetRecordFrame() - 1, Capture(M)); }
				}
				else if (Rp.GetState() == SSXReplay::Play && Rp.GetPlayFrame() > 0)
				{
					(Rp.IsLoaded() ? R->PlayB : R->bSeekPass ? R->PlayKey : R->PlayA).Add(Rp.GetPlayFrame() - 1, Capture(M));
				}
			};
			if (ASnowboarderPawn* P = bScript ? Race->GetPlayerPawn() : nullptr)
			{
				TWeakObjectPtr<ASSXRaceManager> W(Race);
				P->TestPadScript = [R, W](FSSXPadHardware& Pad)
				{
					if (R->bContact) { ContactScript(R->PadSteps++, Pad, W.Get()); }
					else { Script(R->PadSteps++, Pad); }
				};
			}
		}
		FAutomationTestBase* Test;
		TSharedRef<FRun> Run;
	};

	bool FCommand::Update()
	{
		using namespace SSXFe;
		FRun& R = *Run;
		if (FPlatformTime::Seconds() - R.WallStart > (R.bContact ? 3000.0 : 1500.0))
		{
			return Finish(TEXT("timed out"));
		}
		// PIE-like pacing: the engine delta varies tick to tick (0, 1 or several 60 Hz steps per tick)
		static const double Dts[] = { 1.0 / 60.0, 1.0 / 25.0, 1.0 / 144.0, 1.0 / 60.0, 1.0 / 37.0, 1.0 / 90.0, 1.0 / 20.0, 1.0 / 240.0, 1.0 / 48.0 };
		FApp::SetFixedDeltaTime(Dts[R.Ticks++ % UE_ARRAY_COUNT(Dts)]);
		UWorld* World = GameWorld();
		USSXFrontEndSubsystem* FE = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
		const FString Map = World ? UWorld::RemovePIEPrefix(World->GetMapName()) : FString();
		ASSXRaceManager* Race = World ? ASSXRaceManager::Find(World) : nullptr;
		ASSXPlayerController* PC = World ? Cast<ASSXPlayerController>(World->GetFirstPlayerController()) : nullptr;
		FSSXOverlayManager* O = PC && PC->GetOverlayWidget() ? &PC->GetOverlayWidget()->GetOverlays() : nullptr;
		switch (R.Stage)
		{
		case 0:
			if (!FE || Map != TEXT("FrontEnd") || !World->HasBegunPlay()) return false;
			R.Launch.GameMode = int32(ERaceEventMode::CircuitRace);
			R.Launch.Track = 1;
			R.Launch.Slots[0].Char = 4;
			R.Launch.Slots[0].Board = 0;
			if (FE->RequestLaunch(R.Launch) != ESSXLoadResult::Loading) return Finish(TEXT("RequestLaunch(gari)"));
			R.Stage = 1;
			return false;
		case 1:
			if (!Race || Map != TEXT("Snow") || !Race->GetSession() || !Race->GetPlayerPawn()) return false;
			if (!R.bHooked) { Hook(Race, true); R.bHooked = true; }
			if (!R.bSameStart && Race->GetSession()->GetPhase() == ERacePhase::PreRace && ++R.Frames % 700 == 0)
			{
				Race->GetUIHost().PressIntroSkip();
			}
			if (!R.bEndWait)
			{
				if (R.bContact)
				{
					const FSSXReplay& Rp = Race->GetReplay();
					const bool bRacing = Race->GetSession()->GetPhase() == ERacePhase::Race;
					if (R.bRestart && !R.bRestarted && bRacing && Rp.GetRecordFrame() >= 700)
					{
						Race->RestartRace();
						R.bRestarted = true;
						return false;
					}
					if ((R.bRestarted || !R.bRestart) && R.PauseStage == 0 && bRacing && Rp.GetRecordFrame() >= 1500)
					{
						Race->GetUIHost().SetPaused(true);
						R.PauseStage = 1;
						return false;
					}
					if (R.PauseStage == 1)
					{
						if (++R.PauseTicks >= 40) { Race->GetUIHost().SetPaused(false); R.PauseStage = 2; }
						return false;
					}
					if (!Rp.IsEnded() && Rp.GetRecordFrame() < (R.bRestart ? 4000u : 60u * 60u * 4u)) return false;
					Race->GetReplayMutable().EndRecording();
					R.Need = Race->GetReplay().GetRecordedLength() - 1;
					const FRaceSession& Ses = *Race->GetSession();
					int32 Contacts = 0, Impulses = 0, Wipe = 0, Rail = 0;
					for (int32 I = 0; I < Ses.NumRacers(); ++I) { Contacts += Ses.GetRacer(I).RiderContacts; Impulses += Ses.GetRacer(I).RiderImpulses; }
					const int32 Human = Race->GetRacerIndex(Race->GetPlayerPawn());
					for (const TPair<uint32, FFrame>& F : R.Live)
					{
						if (!F.Value.Riders.IsValidIndex(Human)) continue;
						Wipe += F.Value.Riders[Human].Control == uint8(ERiderControl::WipeOut) ? 1 : 0;
						Rail += F.Value.Riders[Human].Control == uint8(ERiderControl::RailSlide) ? 1 : 0;
					}
					Test->AddInfo(FString::Printf(TEXT("live race: %u frames (ended %d), rider contacts %d, impulses / shove hits %d, knock-downs %d, human wipe-out frames %d, rail frames %d"),
						R.Need, Rp.IsEnded() ? 1 : 0, Contacts, Impulses, Ses.GetRiderKnockdowns(), Wipe, Rail));
					Test->TestTrue(TEXT("the contact pad reached the AI riders"), Impulses > 0);
				}
				else
				{
					if (Race->GetReplay().GetRecordFrame() < LiveFrames + 1) return false;
					Race->GetReplayMutable().EndRecording();
				}
				R.bEndWait = true;
			}
			// 0x18F0F8
			if (Race->GetReplay().GetState() != SSXReplay::RecordEnded) return false;
			if (!Race->BuildReplayFile(R.File) || !SSXReplayFile::Validate(R.File)) return Finish(TEXT("replay file"));
			if (!Race->StartResultsReplay()) return Finish(TEXT("StartResultsReplay"));
			R.Frames = 0;
			R.Stage = 2;
			return false;
		case 2:
		{
			if (!Race) return Finish(TEXT("race gone"));
			FSSXReplay& Rp = Race->GetReplayMutable();
			if (R.SeekStage == 0 && Rp.GetState() == SSXReplay::Paused && Rp.GetPlayFrame() == 0 && Rp.GetCommand() == SSXReplay::CmdNone) { Rp.SetCommand(SSXReplay::CmdPlayToggle); }
			if (R.SeekStage == 0)
			{
				if (Rp.GetPlayFrame() < R.Need && ++R.Frames < 60 * 900) return false;
				if (!R.bContact) { R.SeekStage = 3; }
				else
				{
					Rp.SetCommand(SSXReplay::CmdToStart);
					R.SeekStage = 1;
					return false;
				}
			}
			if (R.SeekStage == 1)
			{
				if (Rp.GetCommand() != SSXReplay::CmdNone || Rp.GetState() != SSXReplay::Paused) return ++R.Frames > 60 * 1200 ? Finish(TEXT("seek")) : false;
				if (Rp.GetPlayFrame() < R.Need / 2 && R.SeekJumps < 200) { Rp.SetCommand(SSXReplay::CmdNextKey); ++R.SeekJumps; return false; }
				Test->AddInfo(FString::Printf(TEXT("results replay (A2): played from the keyframe at frame %u (%d next-key seeks)"), Rp.GetPlayFrame(), R.SeekJumps));
				R.bSeekPass = true;
				Rp.SetCommand(SSXReplay::CmdPlayToggle);
				R.SeekStage = 2;
				return false;
			}
			if (R.SeekStage == 2)
			{
				if (Rp.GetPlayFrame() < R.Need && ++R.Frames < 60 * 1200) return false;
				R.bSeekPass = false;
			}
			Race->StopResultsReplay();
			Race->TestOnFrame = nullptr;
			if (ASnowboarderPawn* P = Race->GetPlayerPawn()) { P->TestPadScript = nullptr; }
			Race->GetUIHost().RequestExit(2);
			R.Frames = 0;
			R.Stage = 3;
			return false;
		}
		case 3:
		{
			if (!FE || Map != TEXT("FrontEnd") || !World->HasBegunPlay()) return false;
			SSXReplayFile::FSetup Setup;
			if (!SSXReplayFile::ReadSetup(R.File, Setup)) return Finish(TEXT("setup"));
			FSSXFeLaunch L = R.Launch;
			L.GameMode = Setup.GameMode;
			L.Track = Setup.Venue;
			L.Difficulty = Setup.Difficulty;
			L.Players = FMath::Max(1, Setup.NumHumans());
			FE->PendingReplay = R.File;
			if (FE->RequestLaunch(L) != ESSXLoadResult::Loading) return Finish(TEXT("RequestLaunch(loaded replay)"));
			R.bHooked = false;
			R.Stage = 4;
			return false;
		}
		case 4:
		{
			if (!Race || Map != TEXT("Snow") || !O || !Race->GetSession()) return false;
			if (!R.bHooked) { Hook(Race, false); R.bHooked = true; }
			if (!Race->IsLoadedReplayRace()) return Finish(TEXT("the race did not take the loaded replay"));
			FSSXReplay& Rp = Race->GetReplayMutable();
			if (!Rp.IsRebuilding())
			{
				O->SetHost(&Race->GetUIHost());
				O->Tick();
				if (Rp.GetState() == SSXReplay::Paused && Rp.GetPlayFrame() == 0)
				{
					O->SetPadHeld(0, PadCross);
					O->Tick();
					O->SetPadHeld(0, 0);
				}
			}
			if ((Rp.IsRebuilding() || Rp.GetPlayFrame() < R.Need) && ++R.Frames < 60 * 1500) return false;
			Race->TestOnFrame = nullptr;
			bool bOk = Compare(Test, TEXT("results replay (A)"), R.Live, R.PlayA, R.Need);
			if (R.bContact)
			{
				uint32 From = MAX_uint32;
				for (const TPair<uint32, FFrame>& F : R.PlayKey) { From = FMath::Min(From, F.Key); }
				FTrace Tail;
				for (uint32 F = 0; F < R.Need; ++F) { if (const FFrame* X = F >= From ? R.PlayKey.Find(F) : R.Live.Find(F)) { Tail.Add(F, *X); } }
				bOk &= Compare(Test, TEXT("results replay from a mid-race keyframe (A2)"), R.Live, Tail, R.Need);
			}
			const FSSXRaceReplayWorld& RW = Race->GetReplayWorld();
			if (R.bRestart)
			{
				// 0x10A0D8
				Compare(Test, TEXT("restarted race, loaded file rebuild (B, not checked: port files carry no handler snapshot)"), R.Live, R.Rebuild, R.Need, false);
			}
			else
			{
				bOk &= Compare(Test, TEXT("loaded replay keyframe rebuild (B)"), R.Live, R.Rebuild, R.Need);
				bOk &= Compare(Test, TEXT("loaded replay playback (C)"), R.Live, R.PlayB, R.Need);
				Test->TestEqual(TEXT("loaded replay: every rebuilt keyframe's stream B is the recording's"), RW.PortStreamBDiffers, 0);
			}
			return Finish();
		}
		default:
			return Finish(TEXT("bad stage"));
		}
	}
}

namespace SSXReplayRerun
{
	void Begin(FAutomationTestBase* Test, const TSharedRef<FRun>& Run)
	{
		Run->WallStart = FPlatformTime::Seconds();
		FGameRng::StreamA() = FGameRng(0x5EED1234u);
		FGameRng::StreamB() = FGameRng();
		Run->bPrevFixed = FApp::UseFixedTimeStep();
		Run->PrevFixedDt = FApp::GetFixedDeltaTime();
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / 60.0);
		if (Run->bSameStart)
		{
			ASSXRaceManager::TestBeforeSeed = []() { FGameRng::StreamA() = FGameRng(0x5EED1234u); };
		}
		AutomationOpenMap(TEXT("/Game/Maps/FrontEnd"), true);
		ADD_LATENT_AUTOMATION_COMMAND(FCommand(Test, Run));
	}

	void QueueNext(FAutomationTestBase* Test, const TSharedRef<FRun>& Run)
	{
		const TSharedRef<FRun> Next = Run->Next.ToSharedRef();
		Run->Next.Reset();
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, Next]()
		{
			if (GEditor && GEditor->PlayWorld) { return false; }
			Begin(Test, Next);
			return true;
		}));
	}

	bool Start(FAutomationTestBase* Test, bool bContact, bool bRestart = false, uint32 LookPeriod = 0)
	{
		const FString Sim = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"));
		for (const TCHAR* F : { TEXT("snow.patches.bin"), TEXT("snow.race.bin"), TEXT("snow.effects.bin"), TEXT("anim_clips.bin") })
		{
			if (!FPaths::FileExists(FPaths::Combine(Sim, F))) { SSXTest::Skip(*Test, FString::Printf(TEXT("SKIPPED: SimData/%s missing"), F)); return true; }
		}
		for (const TCHAR* M : { TEXT("Snow"), TEXT("FrontEnd") })
		{
			if (!FPaths::FileExists(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Maps"), FString(M) + TEXT(".umap"))))
			{
				SSXTest::Skip(*Test, FString::Printf(TEXT("SKIPPED: Maps/%s missing"), M));
				return true;
			}
		}
		TSharedRef<FRun> Run = MakeShared<FRun>();
		Run->bContact = bContact;
		Run->bRestart = bRestart;
		Run->LookPeriod = LookPeriod;
		if (LookPeriod > 0)
		{
			TSharedRef<FRun> Baseline = MakeShared<FRun>();
			Baseline->bContact = bContact;
			Baseline->bRestart = bRestart;
			Baseline->Next = Run;
			Baseline->bSameStart = Run->bSameStart = true;
			Run->Baseline = Baseline;
			Begin(Test, Baseline);
		}
		else
		{
			Begin(Test, Run);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayInGameRerunTest, "SSX.Replay.InGameRerun", SSXReplayRerun::Flags)
bool FSSXReplayInGameRerunTest::RunTest(const FString& Parameters)
{
	return SSXReplayRerun::Start(this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayInGameContactRerunTest, "SSX.Replay.InGameContactRerun", SSXReplayRerun::Flags)
bool FSSXReplayInGameContactRerunTest::RunTest(const FString& Parameters)
{
	return SSXReplayRerun::Start(this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayInGameRestartRerunTest, "SSX.Replay.InGameRestartRerun", SSXReplayRerun::Flags)
bool FSSXReplayInGameRestartRerunTest::RunTest(const FString& Parameters)
{
	return SSXReplayRerun::Start(this, true, true);
}

// The render look never touches the sim: a Classic race, then the same race with ssx.Render.Look flipping every 97
// frames (half a period later in the playbacks). Both live races match bit for bit and the second race's replays
// match it as usual.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayInGameLookSwitchTest, "SSX.Replay.InGameLookSwitch", SSXReplayRerun::Flags)
bool FSSXReplayInGameLookSwitchTest::RunTest(const FString& Parameters)
{
	return SSXReplayRerun::Start(this, false, false, 97);
}

#endif
