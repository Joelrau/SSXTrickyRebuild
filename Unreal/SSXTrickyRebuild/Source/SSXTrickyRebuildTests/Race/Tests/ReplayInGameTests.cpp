// 0x2917B0, 0x167060, 0x180C70, 0x1668A0, 0x194910
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXPathfinder.h"
#include "Audio/SSXRaceAudioFlow.h"
#include "Input/SSXRumbleSubsystem.h"
#include "Race/GameRng.h"
#include "Race/SSXReplay.h"
#include "Race/RaceRules.h"
#include "Race/SSXRaceManager.h"
#include "Race/SSXReplayFile.h"
#include "Effects/SSXTrackEffects.h"
#include "Tests/AutomationCommon.h"
#include "UI/SSXFeTypes.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXMemCard.h"
#include "UI/SSXPlayerController.h"
#include "UI/SSXRaceOverlays.h"
#include "World/SSXPropAnimation.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Race/Tests/ReplayFileRace.inl"

namespace SSXReplayInGame
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

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

	FString MapOf(const UWorld* W) { return W ? UWorld::RemovePIEPrefix(W->GetMapName()) : FString(); }

	struct FRun
	{
		int32 Stage = 0;
		double WallStart = 0.0;
		int32 Frames = 0;
		int32 LastOvFrame = -1;
		float RebuildRumble = 0.f;
		int32 Resumes = 0;
		TArray<uint8> File;
		FString PrevMemCard;
		bool bPrevFixed = false;
		double PrevFixedDt = 0.0;
		FSSXFeLaunch Launch;
	};

	class FCommand : public IAutomationLatentCommand
	{
	public:
		FCommand(FAutomationTestBase* InTest, TSharedRef<FRun> InRun) : Test(InTest), Run(InRun) {}
		virtual bool Update() override;
	private:
		bool Finish(const FString& Failure = FString())
		{
			if (!Failure.IsEmpty()) { Test->AddError(FString::Printf(TEXT("[stage %d] %s"), Run->Stage, *Failure)); }
			IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
			FSSXMemCard::RootOverride() = Run->PrevMemCard;
			FApp::SetUseFixedTimeStep(Run->bPrevFixed);
			FApp::SetFixedDeltaTime(Run->PrevFixedDt);
			if (GEditor && GEditor->PlayWorld) { GEditor->RequestEndPlayMap(); }
			return true;
		}
		FAutomationTestBase* Test;
		TSharedRef<FRun> Run;
	};

	bool FCommand::Update()
	{
		using namespace SSXFe;
		FRun& R = *Run;
		if (FPlatformTime::Seconds() - R.WallStart > 900.0)
		{
			return Finish(TEXT("timed out"));
		}
		UWorld* World = GameWorld();
		USSXFrontEndSubsystem* FE = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
		const FString Map = MapOf(World);
		ASSXRaceManager* Race = World ? ASSXRaceManager::Find(World) : nullptr;
		ASSXPlayerController* PC = World ? Cast<ASSXPlayerController>(World->GetFirstPlayerController()) : nullptr;
		FSSXOverlayManager* O = PC && PC->GetOverlayWidget() ? &PC->GetOverlayWidget()->GetOverlays() : nullptr;
		switch (R.Stage)
		{
		case 0:
			if (!FE || Map != TEXT("FrontEnd") || !World->HasBegunPlay()) return false;
			R.Launch.GameMode = int32(ERaceEventMode::CircuitRace);
			R.Launch.Track = 0;
			R.Launch.Slots[0].Char = 4;
			R.Launch.Slots[0].Board = 0;
			if (FE->RequestLaunch(R.Launch) != ESSXLoadResult::Loading) return Finish(TEXT("RequestLaunch(gari)"));
			R.Stage = 1;
			return false;
		case 1:   // 0x113478
			if (!Race || Map != TEXT("Gari") || !Race->GetSession()) return false;
			if (Race->GetSession()->GetPhase() == ERacePhase::PreRace && ++R.Frames > 90)
			{
				Race->GetUIHost().PressIntroSkip();   // 0x110AB8
			}
			if (Race->GetReplay().GetRecordFrame() < 400u) return false;
			R.Frames = 0;
			Race->GetReplayMutable().EndRecording();   // 0x191230
			R.Stage = 2;
			return false;
		case 2:
		{
			if (!Race) return Finish(TEXT("race gone"));
			if (!Race->BuildReplayFile(R.File) || !SSXReplayFile::Validate(R.File)) return Finish(TEXT("replay file"));
			const FString Path = FSSXMemCard::FilePath(FSSXMemCard::ReplayName(0));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			if (!FFileHelper::SaveArrayToFile(R.File, *Path)) return Finish(TEXT("card write"));
			Race->GetUIHost().RequestExit(2);
			R.Stage = 3;
			return false;
		}
		case 3:
		{
			if (!FE || Map != TEXT("FrontEnd") || !World->HasBegunPlay()) return false;
			FSSXMemCard M;
			M.Begin(FSSXMemCard::EMode::Replay, false, nullptr);
			for (int32 N = 0; N < 600 && M.State != FSSXMemCard::LoadList; ++N) { M.Tick(); }
			M.Input(FSSXMemCard::InAccept);
			for (int32 N = 0; N < 300 && M.IsActive(); ++N) { M.Tick(); }
			SSXReplayFile::FSetup Setup;
			if (!M.bLoadedReplay || !SSXReplayFile::ReadSetup(M.LoadedReplay, Setup)) return Finish(TEXT("card load of the replay"));
			Test->TestEqual(TEXT("the loaded file is the saved one"), M.LoadedReplay, R.File);
			FSSXFeLaunch L = R.Launch;
			L.GameMode = Setup.GameMode;
			L.Track = Setup.Venue;
			L.Difficulty = Setup.Difficulty;
			L.Players = FMath::Max(1, Setup.NumHumans());
			// 0x22E400
			if (USSXAudioSubsystem* Snd = USSXAudioSubsystem::Get(FE))
			{
				Test->TestTrue(TEXT("ambient: on after the first race's intro (carried over the track change)"), Snd->bAmbientEnableCarry);
				Snd->bAmbientEnableCarry = false;
			}
			FE->PendingReplay = MoveTemp(M.LoadedReplay);
			if (FE->RequestLaunch(L) != ESSXLoadResult::Loading) return Finish(TEXT("RequestLaunch(loaded replay)"));
			R.Stage = 4;
			return false;
		}
		case 4:
		{
			if (!Race || Map != TEXT("Gari") || !O) return false;
			if (!Race->IsLoadedReplayRace()) return Finish(TEXT("the race did not take the loaded replay"));
			if (!Race->GetReplay().IsRebuilding() && O->Frame == R.LastOvFrame && R.Frames > 30)
			{
				O->SetHost(&Race->GetUIHost());
				O->Tick();
			}
			R.LastOvFrame = O->Frame;
			if (const USSXRumbleSubsystem* Rumble = Race->GetReplay().IsRebuilding() ? USSXRumbleSubsystem::Get(Race) : nullptr)
			{
				R.RebuildRumble = FMath::Max(R.RebuildRumble, Rumble->GetPlayer(0).Strength);
			}
			if (Race->GetReplay().IsRebuilding() || !O->IsActive(OvReplay))
			{
				if (++R.Frames <= 60 * 120) return false;
				const FSSXReplay& Rp = Race->GetReplay();
				return Finish(FString::Printf(TEXT("no replay overlay: rebuilding %d, phase %d, replay state %d, record frame %u / %u, overlay frame %d, ")
					TEXT("loaded %d, start pending %d, control %d, overlay state %d"),
					Rp.IsRebuilding() ? 1 : 0, Race->GetSession() ? int32(Race->GetSession()->GetPhase()) : -1, Rp.GetState(),
					Rp.GetRecordFrame(), Rp.GetRecordedLength(), O->Frame, Rp.IsLoaded() ? 1 : 0, Race->GetUIHost().bLoadedReplayStart ? 1 : 0,
					O->IsActive(OvReplayControl) ? 1 : 0, O->ReplayState));
			}
			// 0x153390: replay playback sends no rumble; the rebuild re-simulates the race as playback
			Test->TestEqual(TEXT("loaded replay: no rumble while the race is rebuilt"), R.RebuildRumble, 0.f);
			Test->TestTrue(TEXT("loaded replay: the object is loaded with its keyframes"), Race->GetReplay().IsLoaded()
				&& Race->GetReplay().KeyframeFrames().Num() > 0);
			Test->TestTrue(TEXT("replay overlay state 1 and its control"), O->ReplayState == 1 && O->IsActive(OvReplayControl));
			// 0x2116D8, 0x1EF990, 0x215288, 0x215220, 0x2153F8
			if (USSXAudioSubsystem* A = USSXAudioSubsystem::Get(Race))
			{
				using SSXRaceAudio::FCommand;
				const TArray<FCommand>& Log = A->GetRaceCommandLog();
				auto Find = [&Log](FCommand::EKind K) { return Log.IndexOfByPredicate([K](const FCommand& C) { return C.Kind == K; }); };
				const int32 IStop = Find(FCommand::StopIntro), IPrep = Find(FCommand::PrepareSong), IUnp = Find(FCommand::UnpauseSong);
				Test->TestTrue(TEXT("loaded replay audio: no intro queued, no countdown beep"), Find(FCommand::QueueIntro) == INDEX_NONE
					&& Find(FCommand::Beep) == INDEX_NONE && A->GetIntroLog().Num() == 0 && A->GetIntroBig().IsEmpty());
				Test->TestTrue(TEXT("loaded replay audio: overlay start = stop stream 2, prepare the song, unpause (in order, once)"),
					IStop != INDEX_NONE && IPrep > IStop && IUnp > IPrep
					&& Log.FilterByPredicate([](const FCommand& C) { return C.Kind == FCommand::PrepareSong; }).Num() == 1);
				Test->TestTrue(TEXT("loaded replay audio: state 6, duck mode 4, GO state"),
					A->GetRaceFlow().State == 6 && A->GetRaceFlow().DuckMode == 4 && A->GetRaceFlow().bGo);
				Test->TestTrue(TEXT("loaded replay audio: game audio paused until X (0x2153F8)"), A->IsMoviePaused());
				if (!A->GetRaceSongStem().IsEmpty() && A->GetPathfinderPlayer().IsValid())
				{
					Test->TestEqual(TEXT("loaded replay audio: the prepared song is the race's current song"), A->GetCurrentSongName(), A->GetRaceSongStem());
					Test->TestTrue(TEXT("loaded replay audio: the song is paused"), A->GetPathfinderPlayer()->IsPaused());
				}
				else
				{
					SSXTest::Skip(*Test, TEXT("song data not converted: the prepared / paused song checks are skipped"));
				}
				R.Resumes = A->MovieResumes;
			}
			R.Frames = 0;
			// 0x22E338
			USSXTrackEffects* Fx = USSXTrackEffects::Get(Race);
			if (Fx && Fx->GetAmbient().IsBuilt())
			{
				Test->TestEqual(TEXT("loaded replay ambient: no enable calls"), Fx->GetAmbient().NumEnableCalls, 0);
				Test->TestFalse(TEXT("loaded replay ambient: the flag is the sound manager's (off), not set by a PreRace step"), Fx->GetAmbient().IsEnabled());
				Test->TestEqual(TEXT("loaded replay ambient: no slot active at the overlay start"), Fx->GetAmbient().NumActive(), 0);
				Test->TestEqual(TEXT("loaded replay ambient: no voice playing at the overlay start"), Fx->NumPlayingAmbientVoices(), 0);
			}
			R.Stage = 6;
			return false;
		}
		case 6:   // 0x215488
		{
			if (!Race || !O) return Finish(TEXT("race gone"));
			USSXAudioSubsystem* A = USSXAudioSubsystem::Get(Race);
			if (Race->GetReplay().GetState() != int32(SSXReplay::Paused))
			{
				return ++R.Frames > 600 ? Finish(TEXT("the loaded replay never paused at its start")) : false;
			}
			O->SetPadHeld(0, PadCross);
			O->Tick();
			O->SetPadHeld(0, 0);
			Test->TestEqual(TEXT("X plays the replay (command 6)"), Race->GetReplay().GetCommand(), int32(SSXReplay::CmdPlayToggle));
			if (A)
			{
				Test->TestTrue(TEXT("X: the game audio resumes (0x215488)"), !A->IsMoviePaused() && A->MovieResumes == R.Resumes + 1);
				if (A->GetPathfinderPlayer().IsValid()) { Test->TestFalse(TEXT("X: the song plays"), A->GetPathfinderPlayer()->IsPaused()); }
			}
			R.Stage = 8;
			R.Frames = 0;
			R.LastOvFrame = -1;
			return false;
		}
		case 8:
		{
			if (!Race || !O || !Race->GetSession()) return Finish(TEXT("race gone"));
			const FSSXReplay& Rp = Race->GetReplay();
			if (Rp.GetState() != int32(SSXReplay::Play) || int32(Rp.GetPlayFrame()) == R.LastOvFrame) return false;
			R.LastOvFrame = int32(Rp.GetPlayFrame());
			O->SetHost(&Race->GetUIHost());
			O->Tick();
			const FRaceSession& Ses = *Race->GetSession();
			if (R.Frames == 0)
			{
				Test->TestTrue(TEXT("loaded replay start: the countdown (phase 3, clock < 0)"), Ses.GetPhase() == ERacePhase::Countdown && Ses.GetClock() < 0.0);
			}
			if (O->IsActive(OvCountdown) || O->IsGoShowing() || O->GetCountdownDigit() != 0)
			{
				return Finish(FString::Printf(TEXT("loaded replay frame %d: countdown overlay shown (digit %d)"), R.Frames, O->GetCountdownDigit()));
			}
			if (++R.Frames < 170) return false;
			Test->TestEqual(TEXT("loaded replay: GO reached in the first 170 frames (phase 4)"), int32(Ses.GetPhase()), int32(ERacePhase::Race));
			R.Stage = 7;
			return false;
		}
		case 7:
		{
			if (!Race || !O) return Finish(TEXT("race gone"));
			O->Pad(0, PadStart);
			Test->TestTrue(TEXT("Start: the menu (state 2), exit selected"), O->ReplayState == 2 && O->ReplaySel == 1);
			O->Pad(0, PadCross);
			Test->TestEqual(TEXT("exit of a loaded replay asks first (state 4)"), O->ReplayState, 4);
			if (FE) { FE->LastExitCode = -1; }
			O->Pad(0, PadCross);
			Test->TestEqual(TEXT("X: the race is left with exit code 2 (DAT_0032E5F0 as the FE left it)"), FE ? FE->LastExitCode : -1, 2);
			R.Stage = 5;
			return false;
		}
		case 5:
			if (!FE || Map != TEXT("FrontEnd")) { return ++R.Frames > 60 * 240 ? Finish(TEXT("no return to the front end")) : false; }
			Test->TestTrue(TEXT("the front end after the loaded replay"), Map == TEXT("FrontEnd"));
			return Finish();
		default:
			return Finish(TEXT("bad stage"));
		}
	}
}

namespace SSXReplayInGame
{
	class FUserFileCommand : public IAutomationLatentCommand
	{
	public:
		FUserFileCommand(FAutomationTestBase* InTest, TArray<uint8> InFile, bool bInPrevFixed, double InPrevFixedDt)
			: Test(InTest), File(MoveTemp(InFile)), bPrevFixed(bInPrevFixed), PrevFixedDt(InPrevFixedDt) { WallStart = FPlatformTime::Seconds(); }
		virtual bool Update() override
		{
			auto Finish = [this](const FString& Failure)
			{
				if (!Failure.IsEmpty()) { Test->AddError(Failure); }
				FApp::SetUseFixedTimeStep(bPrevFixed);
				FApp::SetFixedDeltaTime(PrevFixedDt);
				if (GEditor && GEditor->PlayWorld) { GEditor->RequestEndPlayMap(); }
				return true;
			};
			if (FPlatformTime::Seconds() - WallStart > 900.0) return Finish(TEXT("timed out"));
			UWorld* World = GameWorld();
			USSXFrontEndSubsystem* FE = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
			const FString Map = MapOf(World);
			ASSXRaceManager* Race = World ? ASSXRaceManager::Find(World) : nullptr;
			if (Stage == 0)
			{
				if (!FE || Map != TEXT("FrontEnd") || !World->HasBegunPlay()) return false;
				SSXReplayFile::FSetup S;
				if (!SSXReplayFile::ReadSetup(File, S)) return Finish(TEXT("setup"));
				{
					TUniquePtr<FFileRace> Head = MakeUnique<FFileRace>();
					FString Err;
					if (Head->Build(S, Err) && SSXReplayFile::ReadReplay(File, Head->Replay))
					{
						Head->Replay.BeginRebuild();
						for (int32 G = 0; G < 60 * 600 && Head->Replay.IsRebuilding(); ++G)
						{
							Head->Frame();
							for (int32 I = 0; I < Head->Sims.Num(); ++I) { Headless.Add((int64(I) << 32) | Head->Replay.GetRecordFrame(), Head->Sims[I]->State); }
							HeadlessLast = Head->Replay.GetRecordFrame();
						}
					}
					else
					{
						SSXTest::Skip(*Test, FString::Printf(TEXT("headless re-run not available (%s): no frame comparison"), *Err));
					}
				}
				const int32 H = S.RacerOfHuman(0);
				FSSXFeLaunch L;
				L.GameMode = S.GameMode;
				L.Track = S.Venue;
				L.Difficulty = S.Difficulty;
				L.Players = FMath::Max(1, S.NumHumans());
				L.Racers = S.NumRacers;
				L.Slots[0].Char = H >= 0 ? S.Racers[H].Char38 : 0;
				L.Slots[0].Board = H >= 0 ? SSXReplayFile::RacerProfile(S.Racers[H]).Board : 0;
				FE->PendingReplay = File;
				if (FE->RequestLaunch(L) != ESSXLoadResult::Loading) return Finish(TEXT("RequestLaunch(the file's venue)"));
				Stage = 1;
				return false;
			}
			if (!Race || Map == TEXT("FrontEnd") || !Race->GetSession())
			{
				return ++Frames > 60 * 240 ? Finish(TEXT("no race")) : false;
			}
			if (!Race->IsLoadedReplayRace()) return Finish(TEXT("the race did not take the loaded replay"));
			if (!bCompareHooked && Headless.Num())
			{
				bCompareHooked = true;
				Race->TestOnFrame = [this](ASSXRaceManager& M)
				{
					if (!M.GetReplay().IsRebuilding() || !M.GetSession()) return;
					const uint32 F = M.GetReplay().GetRecordFrame();
					if (F == 0 || F >= HeadlessLast || RollerFrame) return;
					if (const USSXPropAnimation* PA = USSXPropAnimation::Get(&M))
					{
						if (PA->NumNodes(SSXEffects::TypeRoller) + PA->NumNodes(SSXEffects::TypeMeshAnim) + PA->NumNodes(SSXEffects::TypeCracked) > 0)
						{
							RollerFrame = F;
							return;
						}
					}
					for (int32 I = 0; I < M.GetSession()->NumRacers(); ++I)
					{
						const FRiderSim* Sim = M.GetSession()->GetRacer(I).Sim;
						const FRiderState* Ref = Headless.Find((int64(I) << 32) | F);
						if (!Sim || !Ref) continue;
						const FRiderState& St = Sim->State;
						++Compared;
						FString Diff;
						auto Chk = [&Diff](bool bSame, const TCHAR* Name) { if (!bSame) { Diff += TEXT(" "); Diff += Name; } };
						Chk(St.Position == Ref->Position, TEXT("p")); Chk(St.Velocity == Ref->Velocity, TEXT("v"));
						Chk(St.Orientation == Ref->Orientation, TEXT("q")); Chk(St.Motion == Ref->Motion && St.Control == Ref->Control, TEXT("state"));
						Chk(St.RenderRow3 == Ref->RenderRow3, TEXT("render")); Chk(St.SphereFrameP == Ref->SphereFrameP, TEXT("setP"));
						Chk(St.PosedSpheres[8] == Ref->PosedSpheres[8], TEXT("posed")); Chk(St.BoostMeter == Ref->BoostMeter, TEXT("meter"));
						Chk(St.Steer.Cur == Ref->Steer.Cur, TEXT("steer")); Chk(St.BoardOrigin == Ref->BoardOrigin, TEXT("board"));
						Chk(St.ShoveAnim == Ref->ShoveAnim && St.ShoveAge == Ref->ShoveAge, TEXT("shove"));
						if (!Diff.IsEmpty())
						{
							if (FirstMismatch.IsEmpty())
							{
								FirstMismatch = FString::Printf(TEXT("prop %d / %d, walls %d / %d, impact %s / %s; "), St.PropHitInstance, Ref->PropHitInstance, St.WallSoundCount,
									Ref->WallSoundCount, *St.LastImpactNormal.ToString(), *Ref->LastImpactNormal.ToString())
									+ FString::Printf(TEXT("f%u racer %d (%s): in game %s / %s motion %d control %d, headless %s / %s motion %d control %d"), F, I, *Diff,
									*St.Position.ToString(), *St.Velocity.ToString(), int32(St.Motion), int32(St.Control), *Ref->Position.ToString(), *Ref->Velocity.ToString(),
									int32(Ref->Motion), int32(Ref->Control));
							}
							++Mismatches;
							if (!MismatchFrames.Contains(F)) { MismatchFrames.Add(F); }
						}
					}
				};
			}
			if (!bHooked && !FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_PORT_DUMP_INGAME")).IsEmpty())
			{
				bHooked = true;
				TFunction<void(ASSXRaceManager&)> Prev = Race->TestOnFrame;
				Race->TestOnFrame = [this, Prev](ASSXRaceManager& M)
				{
					if (Prev) { Prev(M); }
					const uint32 F = M.GetReplay().GetRecordFrame();
					if (!M.GetReplay().IsRebuilding() || F >= DumpTo() || !M.GetSession()) return;
					for (int32 I = 0; I < M.GetSession()->NumRacers(); ++I)
					{
						if (const FRiderSim* Sim = M.GetSession()->GetRacer(I).Sim)
						{
							const FRiderState& St = Sim->State;
							FString Row = FString::Printf(TEXT("%u,%d"), F, I);
							auto V3 = [&Row](int32 Off, const FVector& X) { Row += FString::Printf(TEXT(",%X=%.9g,%X=%.9g,%X=%.9g"), Off, X.X, Off + 4, X.Y, Off + 8, X.Z); };
							V3(0x140, St.Position); V3(0x150, St.Velocity); V3(0x4AA0, St.RenderRow0); V3(0x4AB0, St.RenderRow1); V3(0x4AC0, St.RenderRow2); V3(0x4AD0, St.RenderRow3);
							Row += FString::Printf(TEXT(",170=%.9g,174=%.9g,178=%.9g,17C=%.9g,12C=%.9g,424=%d,428=%d"), St.Orientation.W, St.Orientation.X, St.Orientation.Y, St.Orientation.Z,
								St.TimeScale, int32(St.Motion), int32(St.Control));
							Dump.Add(Row);
						}
					}
				};
			}
			if (Race->GetReplay().IsRebuilding()) { return ++Frames > 60 * 600 ? Finish(TEXT("rebuild did not end")) : false; }
			if (bHooked)
			{
				FFileHelper::SaveStringArrayToFile(Dump, *FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_PORT_DUMP_INGAME")));
				bHooked = false;
			}
			Race->TestOnFrame = nullptr;
			if (Headless.Num())
			{
				FString Fs;
				for (int32 K = 0; K < MismatchFrames.Num() && K < 12; ++K) { Fs += FString::Printf(TEXT(" %u"), MismatchFrames[K]); }
				Test->AddInfo(FString::Printf(TEXT("in-game rebuild vs the headless re-run: %d racer frames compared (to f%u: %s), %d differ%s%s (frames%s)"), Compared,
					RollerFrame ? RollerFrame : HeadlessLast, RollerFrame ? TEXT("the first rigid-body prop node, not in the headless harness") : TEXT("the rebuild's end"),
					Mismatches, FirstMismatch.IsEmpty() ? TEXT("") : TEXT("; first "), *FirstMismatch, *Fs));
				Test->TestTrue(TEXT("in-game rebuild frames compared"), Compared > 0);
				Test->TestEqual(TEXT("in-game rebuild = headless re-run, every racer every frame"), Mismatches, 0);
			}
			for (const SSXReplayFile::FSnapshotCheck& C : Race->GetSnapshotChecks())
			{
				FString Per;
				for (const double E : C.Errors) Per += FString::Printf(TEXT(" %.1f"), E);
				Test->AddInfo(FString::Printf(TEXT("in game: max position error %.2f (racer %d), per racer:%s; stream B %s (port - file calls %lld)"),
					C.MaxError, C.WorstRacer, *Per, C.bStreamBEqual ? TEXT("equal") : TEXT("differs"), C.StreamBCalls));
			}
			Test->TestEqual(TEXT("a check per keyframe"), Race->GetSnapshotChecks().Num(), Race->GetReplay().KeyframeFrames().Num());
			Test->TestTrue(TEXT("the grid of the file (keyframe 0 within 1 unit)"), Race->GetSnapshotChecks().Num() > 0
				&& Race->GetSnapshotChecks()[0].MaxError >= 0.0 && Race->GetSnapshotChecks()[0].MaxError < 1.0);
			return Finish(FString());
		}
	private:
		FAutomationTestBase* Test;
		TArray<uint8> File;
		bool bPrevFixed = false;
		double PrevFixedDt = 0.0;
		double WallStart = 0.0;
		int32 Stage = 0;
		int32 Frames = 0;
		bool bHooked = false, bCompareHooked = false;
		TArray<FString> Dump;
		TMap<int64, FRiderState> Headless;
		int32 Compared = 0, Mismatches = 0;
		uint32 HeadlessLast = 0, RollerFrame = 0;
		FString FirstMismatch;
		TArray<uint32> MismatchFrames;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayUserFileInGameTest, "SSX.Replay.Format.UserFileInGame", SSXReplayInGame::Flags)
bool FSSXReplayUserFileInGameTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayInGame;
	const FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ORIGINAL_REPLAY"));
	TArray<uint8> File;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToArray(File, *Path) || !SSXReplayFile::Validate(File))
	{
		SSXTest::Skip(*this, TEXT("SSX_ORIGINAL_REPLAY not set or not a valid replay file: skipped"));
		return true;
	}
	const bool bPrevFixed = FApp::UseFixedTimeStep();
	const double PrevFixedDt = FApp::GetFixedDeltaTime();
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / 60.0);
	AutomationOpenMap(TEXT("/Game/Maps/FrontEnd"), true);
	ADD_LATENT_AUTOMATION_COMMAND(FUserFileCommand(this, MoveTemp(File), bPrevFixed, PrevFixedDt));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayLoadedInGameTest, "SSX.Replay.LoadedInGame", SSXReplayInGame::Flags)
bool FSSXReplayLoadedInGameTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayInGame;
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
	Run->PrevMemCard = FSSXMemCard::RootOverride();
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardLoadedReplay"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	Run->bPrevFixed = FApp::UseFixedTimeStep();
	Run->PrevFixedDt = FApp::GetFixedDeltaTime();
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / 60.0);
	AutomationOpenMap(TEXT("/Game/Maps/FrontEnd"), true);
	ADD_LATENT_AUTOMATION_COMMAND(FCommand(this, Run));
	return true;
}

#endif
