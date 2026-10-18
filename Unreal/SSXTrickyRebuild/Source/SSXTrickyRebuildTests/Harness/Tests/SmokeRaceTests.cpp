// SSX.Smoke.Race: headless end-to-end race (integration guard, -NullRHI compatible).
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/SSXCharacterAppearance.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Effects/SSXTrackEffects.h"
#include "Effects/TrackEffects.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "Input/SSXInputSetup.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Particles/SSXParticleWorld.h"
#include "World/SSXPropAnimation.h"
#include "Race/GameRng.h"
#include "Race/SSXRaceManager.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Anim/AnimController.h"
#include "Sim/RiderSim.h"
#include "SnowboarderPawn.h"
#include "SSXHUD.h"
#include "SSXTrickyRebuild.h"
#include "Tests/AutomationCommon.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFeRiderActor.h"
#include "UI/SSXFeWidget.h"
#include "UI/SSXFrontEndGameMode.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXMemCard.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

namespace SSXSmoke
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	class FErrorCapture : public FOutputDevice
	{
	public:
		FErrorCapture() { GLog->AddOutputDevice(this); }
		virtual ~FErrorCapture() override { GLog->RemoveOutputDevice(this); }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if ((Verbosity & ELogVerbosity::VerbosityMask) <= ELogVerbosity::Error && Category.ToString().StartsWith(TEXT("LogSSX")))
			{
				FScopeLock Lock(&Mutex);
				Lines.Add(FString::Printf(TEXT("%s: %s"), *Category.ToString(), V));
			}
		}
		TArray<FString> Get() { FScopeLock Lock(&Mutex); return Lines; }
	private:
		FCriticalSection Mutex;
		TArray<FString> Lines;
	};

	struct FPadInjector
	{
		uint32 Held = 0;
		void Send(APlayerController* PC, uint32 Buttons, const FVector2D& Stick)
		{
			using namespace SSXPadButton;
			const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
			struct FBit { uint32 Bit; FKey Key; };
			const FBit Keys[] = { { Cross, EKeys::Gamepad_FaceButton_Bottom }, { L1, EKeys::Gamepad_LeftShoulder },
				{ R1, EKeys::Gamepad_RightShoulder }, { L2, EKeys::Gamepad_LeftTrigger }, { R2, EKeys::Gamepad_RightTrigger } };
			for (const FBit& K : Keys)
			{
				const bool bNow = (Buttons & K.Bit) != 0, bWas = (Held & K.Bit) != 0;
				if (bNow != bWas)
				{
					PC->InputKey(FInputKeyEventArgs::CreateSimulated(K.Key, bNow ? IE_Pressed : IE_Released, bNow ? 1.f : 0.f, -1, Device));
				}
			}
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftX, IE_Axis, float(Stick.X), 1, Device));
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Gamepad_LeftY, IE_Axis, float(Stick.Y), 1, Device));
			Held = Buttons;
		}
	};

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

	int32 PosedBones(const UPoseableMeshComponent* Mesh)
	{
		const USkinnedAsset* Asset = Mesh ? Mesh->GetSkinnedAsset() : nullptr;
		if (!Asset)
		{
			return 0;
		}
		const TArray<FTransform>& Ref = Asset->GetRefSkeleton().GetRefBonePose();
		int32 N = 0;
		for (int32 B = 0; B < FMath::Min(Ref.Num(), Mesh->BoneSpaceTransforms.Num()); ++B)
		{
			const FTransform& T = Mesh->BoneSpaceTransforms[B];
			if (!T.GetRotation().Equals(Ref[B].GetRotation(), 0.02f) || !T.GetTranslation().Equals(Ref[B].GetTranslation(), 0.5))
			{
				++N;
			}
		}
		return N;
	}

	int32 VisiblePlaceholderParts(const ASnowboarderPawn* Pawn)
	{
		TArray<UStaticMeshComponent*> Parts;
		Pawn->GetComponents(Parts);
		int32 N = 0;
		for (const UStaticMeshComponent* P : Parts)
		{
			for (const FName& Tag : P->ComponentTags)
			{
				if (Tag.ToString().StartsWith(TEXT("Color=")) && P->IsVisible()) { ++N; break; }
			}
		}
		return N;
	}

	int32 DeadInstances(const SSXEffects::FEffectWorld& W)
	{
		TArray<TPair<int32, const SSXEffects::FController*>> C;
		W.GetControllers(C);
		int32 N = 0;
		for (const TPair<int32, const SSXEffects::FController*>& P : C)
		{
			N += P.Value && P.Value->Type == SSXEffects::TypeDead ? 1 : 0;
		}
		return N;
	}

	constexpr int32 SnowResetStart = 1087;
	// 0x182858
	constexpr uint32 TestRngSeed = 0x00055358u;

	struct FRaceRun
	{
		FString Map;
		int32 TrackId = 0;
		double RaceSeconds = 36.0;
		bool bExpectDead = true;
		int32 Stage = 0;
		double WallStart = 0.0, StageStart = 0.0;
		bool bTimedOut = false;
		FString Failure;
		TUniquePtr<FErrorCapture> Errors;
		FPadInjector Pad;
		bool bPrevFixed = false;
		double PrevFixedDt = 0.0;
		FString PrevMemCard;

		bool bSawCountdown = false, bSawRace = false, bClockMonotonic = true;
		double LastClock = -1.0, MaxClock = 0.0;
		int32 RaceFrames = 0, Takeoffs = 0, Landings = 0, ScoredLandings = 0, WipeOuts = 0, RailFrames = 0, Grabs = 0;
		int32 WipeStartFrame = -1;
		FString WipeLog;
		int32 RailExits = 0, RailScoredExits = 0, RailExitPoints = 0;
		bool bOverlapPlaced = false;
		int32 OverlapRacer = -1, OverlapFrames = 0, ContactsBefore = 0, ContactsAfter = 0, ImpulsesTotal = 0, ContactsTotal = 0, RiderKnockdowns = 0, ContactsAtGo = -1, PlayerContacts = 0;
		ERiderMotion PrevMotion = ERiderMotion::None;
		double RideTime = 0.0, WindUpLeft = 0.0, AirStartClock = 0.0, LastScriptClock = 0.0;
		int32 PreAirAnim = -1, AirStartFrame = -1, PreGrabAnim = -1;
		bool bWasGrab = false, bAnimOnJump = false, bAnimOnGrab = false;
		int32 LastLandingFrame = -1, HudWindow = 0;
		double ScoreBefore = 0.0;
		bool bHudTrickText = false, bHudPopup = false, bHudDrawnText = false, bScoreRose = false;
		FString HudTrickText;
		int32 MaxPosedBones = 0, Bones = 0;
		float MaxBoardVolume = 0.f;
		int32 MaxLoops = 0;
		bool bAudioDevice = false, bSongAtGo = false, bFlowGo = false;
		FString Song;
		int32 SprayAtCarveStart = -1, SprayCarve = 0;
		bool bShadow = false;
		double MaxCamDist = 0.0, MinFov = 1e9, MaxFov = 0.0;
		int32 CamSamples = 0;
		bool bRaceCamera = false;
		int32 MaxCells = 0, MaxDead = 0;
		int32 MaxCrowdNodes = 0, CrowdSteps = 0, MaxAnimNodes = 0, Rollers = 0;
		double RollerTravel = 0.0;
		TArray<FVector> AIStart;
		TArray<double> AIMoved;
		TArray<double> AIRemainingStart, AIRemainingEnd;
		int32 RankChanges = 0, LastRank = -1;
		int32 PreRaceFrames = 0, MaxIntroStep = -1, IntroScripts = 0, SpeechPosts = 0, SpeechLines = 0;
		bool bIntroDone = false, bSpeechLoaded = false, bIntroMusicEnd = false;
		FString IntroSlots;
		int32 Racers = 0, ShownRiders = 0, AnimatedRiders = 0, PlaceholderParts = 0;
		TArray<FString> Missing;
		double LastWall = 0.0, SumMs = 0.0, MaxMs = 0.0;
		int32 TimedFrames = 0;
		// player wipe-out within the first seconds of the race (no scripted cause before 30 s)
		double FirstPlayerWipeClock = -1.0;
		int32 ContactsSeenPrev = 0, LastContactFrame = -1000, RiderHitWipes = 0, PendingWipeFrame = -1, FirstWipeFrame = -1; TArray<int32> ContactFrames;
		FString FirstPlayerWipe;

		bool bStartOnly = false;
		bool bSkipIntro = true;
		int32 Difficulty = 0;
		ERaceEventMode GameMode = ERaceEventMode::CircuitRace;
		double StartWindow = 5.0;       // seconds after GO in which every rider must ride normally
		TArray<int32> LastEventFrame;
		TArray<FVector> GoPos;
		TArray<FString> StartBad;
		int32 CountdownFrames = 0, CountdownBadStates = 0, StartRacers = 0, RidingAtEnd = 0;
		bool bStartEndChecked = false;
		int32 RestartsLeft = 0, RestartsDone = 0;
		int32 PlacementsBefore = 0;
		int32 CardFrames = 0;
		int32 CardHoldFrames = 30;
		int32 MaxCardFrames = 0;
		int32 StageResetsAtGo = -1;
		int32 ResetStartDebounceAtGo = -1;
		int32 AIBails = 0;
		FString AIBailLog;
		bool bDropCheck = false;
		TArray<bool> DropLanded, Excused;
		int32 DropClean = 0, DropCleanUnscaled = 0, ScaledGrabBails = 0;
		FString DropLog;

		bool bStageCheck = false;
		struct FStageSnap
		{
			bool bTaken = false;
			TArray<uint16> Flags;
			TMap<int32, int32> Ctls;
			int32 Rollers = 0, MeshAnims = 0, Anims = 0, Emitters = 0, Scripts = 0, Rails = 0, TrackSounds = 0;
		};
		FStageSnap Snap[2];
		bool bStageActed = false;
		int32 StagePane = INDEX_NONE, StageRoller = INDEX_NONE, StagePad = INDEX_NONE, StageActFrames = 0;
		FString StageActed;
		int32 StageFlagDiffs = 0;
		bool bPaneHidden = false, bRollerNode = false;
	};

	void TakeStageSnap(FRaceRun::FStageSnap& S, UWorld* World)
	{
		USSXTrackEffects* Fx = USSXTrackEffects::Get(World);
		const USSXPropAnimation* Props = USSXPropAnimation::Get(World);
		if (!Fx || !Fx->IsActive())
		{
			return;
		}
		const SSXEffects::FEffectWorld& W = Fx->GetEffectWorld();
		S.bTaken = true;
		S.Flags.SetNum(Fx->GetData().NumInstances());
		for (int32 I = 0; I < S.Flags.Num(); ++I) { S.Flags[I] = W.GetFlags(I); }
		TArray<TPair<int32, const SSXEffects::FController*>> C;
		W.GetControllers(C);
		for (const TPair<int32, const SSXEffects::FController*>& P : C) { S.Ctls.Add(P.Key, P.Value ? P.Value->Type : -1); }
		S.Rollers = Props ? Props->NumNodes(SSXEffects::TypeRoller) : 0;
		S.MeshAnims = Props ? Props->NumNodes(SSXEffects::TypeMeshAnim) : 0;
		S.Anims = Props ? Props->NumNodes(0x100) + Props->NumNodes(0x101) + Props->NumNodes(0x102) + Props->NumNodes(0x103) : 0;
		S.Emitters = W.NumEmitters();
		S.Scripts = W.NumScripts();
		for (int32 K = 0; K < W.NumRails(); ++K) { S.Rails += W.IsRailEnabled(K) ? 1 : 0; }
		S.TrackSounds = Fx->NumPlayingTrackSounds();
	}

	int32 FindTouchInstance(const SSXEffects::FEffectData& Data, int32 Mode, int32 Track, FRiderSim* Sim,
	                        TFunctionRef<bool(const SSXEffects::FOutcome&, int32)> Pred)
	{
		for (int32 I = 0; I < Data.NumInstances(); ++I)
		{
			SSXEffects::FEffectWorld W;
			W.Init(&Data, Mode, Track);
			W.bKeepLog = true;
			SSXEffects::FEffectRider Rider;
			Rider.Sim = nullptr;
			W.Touch(I, Rider, true, -1.0, 300.0);
			W.Frame();
			for (const SSXEffects::FOutcome& O : W.Log)
			{
				if (Pred(O, I)) { return I; }
			}
		}
		return INDEX_NONE;
	}

	void ObserveStage(FRaceRun& R, UWorld* World, ASSXRaceManager* Race, const FRaceSession* S)
	{
		const ERacePhase Phase = S->GetPhase();
		USSXTrackEffects* Fx = USSXTrackEffects::Get(World);
		if (!Fx || !Fx->IsActive())
		{
			return;
		}
		FRaceRun::FStageSnap& Cur = R.Snap[FMath::Min(R.RestartsDone, 1)];
		if (Phase == ERacePhase::Countdown && !Cur.bTaken)
		{
			TakeStageSnap(Cur, World);
		}
		if (R.RestartsDone == 0 && Phase == ERacePhase::Race && S->GetClock() >= 1.0 && !R.bStageActed)
		{
			R.bStageActed = true;
			const SSXEffects::FEffectData& Data = Fx->GetData();
			SSXEffects::FEffectWorld& W = Fx->GetEffectWorld();
			const int32 Mode = W.GetGameMode(), Track = W.GetTrack();
			ASnowboarderPawn* Player = Race->GetPlayerPawn();
			FRiderSim* Sim = Player ? Player->GetSim() : nullptr;
			R.StagePane = FindTouchInstance(Data, Mode, Track, nullptr, [](const SSXEffects::FOutcome& O, int32 I)
				{ return O.Kind == SSXEffects::EOutcome::Node && O.Instance == I && O.A == SSXEffects::TypeCracked; });
			R.StageRoller = FindTouchInstance(Data, Mode, Track, nullptr, [](const SSXEffects::FOutcome& O, int32 I)
				{ return O.Kind == SSXEffects::EOutcome::Node && O.Instance == I && O.A == SSXEffects::TypeRoller; });
			R.StagePad = FindTouchInstance(Data, Mode, Track, nullptr, [](const SSXEffects::FOutcome& O, int32 I)
				{ return O.Kind == SSXEffects::EOutcome::BoostBurst; });
			SSXEffects::FEffectRider Rider;
			Rider.Sim = Sim;
			for (int32 I : { R.StagePane, R.StageRoller, R.StagePad })
			{
				if (I != INDEX_NONE) { W.Touch(I, Rider, true, -1.0, 300.0); }
			}
		}
		if (R.bStageActed && R.RestartsDone == 0 && R.StageActFrames < 40 && ++R.StageActFrames == 40)
		{
			FRaceRun::FStageSnap Now;
			TakeStageSnap(Now, World);
			const SSXEffects::FEffectWorld& W = Fx->GetEffectWorld();
			int32 FlagDiffs = 0;
			for (int32 I = 0; I < FMath::Min(Now.Flags.Num(), R.Snap[0].Flags.Num()); ++I) { FlagDiffs += Now.Flags[I] != R.Snap[0].Flags[I] ? 1 : 0; }
			R.StageFlagDiffs = FlagDiffs;
			R.StageActed = FString::Printf(TEXT("pane %d hidden %d, roller %d (rollers %d -> %d), pad %d (controller %d); controllers %d -> %d, debris %d -> %d, %d instance flag words changed"),
				R.StagePane, R.StagePane != INDEX_NONE && W.IsScriptHidden(R.StagePane), R.StageRoller, R.Snap[0].Rollers, Now.Rollers, R.StagePad,
				R.StagePad != INDEX_NONE && W.FindController(R.StagePad) ? W.FindController(R.StagePad)->Type : -1, R.Snap[0].Ctls.Num(), Now.Ctls.Num(),
				R.Snap[0].MeshAnims, Now.MeshAnims, FlagDiffs);
			R.bPaneHidden = R.StagePane != INDEX_NONE && W.IsScriptHidden(R.StagePane);
			R.bRollerNode = Now.Rollers > R.Snap[0].Rollers;
		}
	}

	bool IsStartWaitState(const FRiderState& St)
	{
		return St.Motion == ERiderMotion::Static
			&& (St.Control == ERiderControl::GateAnticipate || St.Control == ERiderControl::SitAndWait || St.Control == ERiderControl::Void);
	}

	void ObserveStart(FRaceRun& R, ASSXRaceManager* Race, const FRaceSession* S)
	{
		const ERacePhase Phase = S->GetPhase();
		const int32 N = S->NumRacers();
		if (R.LastEventFrame.Num() != N)
		{
			R.LastEventFrame.Init(-1, N);
			R.GoPos.Init(FVector::ZeroVector, N);
			R.DropLanded.Init(false, N);
			R.Excused.Init(false, N);
		}
		R.StartRacers = N;
		const bool bPlaced = S->GetPlacements() > R.PlacementsBefore;
		const double Clock = S->GetClock();
		const bool bWindow = (Phase == ERacePhase::PreRace && bPlaced) || Phase == ERacePhase::StartRace || Phase == ERacePhase::Countdown
			|| (Phase == ERacePhase::Race && Clock <= R.StartWindow);
		for (int32 I = 0; I < N; ++I)
		{
			const ASnowboarderPawn* P = Race->GetPawn(I);
			const FRiderSim* Sim = P ? P->GetSim() : nullptr;
			if (!Sim)
			{
				continue;
			}
			for (const FRiderSimEvent& E : Sim->GetEvents())
			{
				if (E.Frame <= R.LastEventFrame[I])
				{
					continue;
				}
				const bool bWipe = E.bMotion && (ERiderMotion(E.To) == ERiderMotion::WipeOut
					|| (ERiderMotion(E.To) == ERiderMotion::WipeOutRecover && FCString::Strcmp(E.Reason, TEXT("T40")) != 0));
				const bool bReset = !E.bMotion && ERiderControl(E.To) == ERiderControl::ResetWait;
				const bool bLanding = E.bMotion && ERiderMotion(E.From) == ERiderMotion::Air;
				if (R.bDropCheck && bWindow && Phase == ERacePhase::Race && bLanding && !R.DropLanded[I] && !R.Excused[I]
					&& Sim->State.Position.Z < R.GoPos[I].Z - 1000.0)
				{
					const SSXAir::FAirContext* AirX = SSXAir::GetAirContext(*Sim);
					const bool bClean = ERiderMotion(E.To) == ERiderMotion::Ground;
					const double Ts = Sim->State.TimeScale;
					R.DropLanded[I] = true;
					R.DropLog += FString::Printf(TEXT(" racer %d%s f%d %s (time scale %.3f%s);"), I, P->bAIRider ? TEXT("") : TEXT(" (player)"), E.Frame,
						bClean ? TEXT("landed") : TEXT("bailed"), Ts, AirX && AirX->LastLanding.bValid && AirX->LastLanding.Result.bBailGrab ? TEXT(", grab held") : TEXT(""));
					R.DropClean += bClean ? 1 : 0;
					R.DropCleanUnscaled += bClean && FMath::IsNearlyEqual(Ts, 1.0) ? 1 : 0;
					if (!bClean && P->bAIRider && AirX && AirX->LastLanding.bValid && AirX->LastLanding.Result.bBailGrab && Ts > 1.0 + 1e-6)
					{
						// 0x108378, 0x12B340, 0x123EC8
						R.Excused[I] = true;
						++R.ScaledGrabBails;
					}
				}
				if (bWindow && bWipe && P->bAIRider)
				{
					++R.AIBails;
					R.AIBailLog += FString::Printf(TEXT(" racer %d f%d (%s);"), I, E.Frame, E.Reason ? E.Reason : TEXT(""));
				}
				if (bWindow && (bWipe || bReset) && !R.Excused[I] && R.StartBad.Num() < 24)
				{
					R.StartBad.Add(FString::Printf(TEXT("racer %d f%d %s %s -> %s (%s) in phase %d clock %.2f: %s"), I, E.Frame,
						E.bMotion ? TEXT("motion") : TEXT("control"),
						E.bMotion ? LexToString(ERiderMotion(E.From)) : LexToString(ERiderControl(E.From)),
						E.bMotion ? LexToString(ERiderMotion(E.To)) : LexToString(ERiderControl(E.To)),
						E.Reason ? E.Reason : TEXT(""), int32(Phase), Clock, *Sim->DumpState()));
					UE_LOG(LogSSX, Warning, TEXT("[smoke-start] %s"), *R.StartBad.Last());
				}
			}
			for (const FRiderSimEvent& E : Sim->GetEvents())
			{
				R.LastEventFrame[I] = FMath::Max(R.LastEventFrame[I], E.Frame);
			}
			const FRiderState& St = Sim->State;
			if (Phase == ERacePhase::Countdown)
			{
				R.GoPos[I] = St.Position;
				if (!IsStartWaitState(St))
				{
					++R.CountdownBadStates;
					if (R.StartBad.Num() < 24)
					{
						R.StartBad.Add(FString::Printf(TEXT("racer %d in the countdown: %s"), I, *Sim->DumpState()));
					}
				}
			}
			if (Phase == ERacePhase::Race && Clock >= R.StartWindow && !R.bStartEndChecked)
			{
				const bool bRiding = St.Motion == ERiderMotion::Ground || St.Motion == ERiderMotion::Air || St.Motion == ERiderMotion::RailSlide;
				const double Moved = FVector::Dist(St.Position, R.GoPos[I]);
				if (R.bDropCheck && !R.DropLanded[I] && !R.Excused[I])
				{
					R.DropLog += FString::Printf(TEXT(" racer %d%s not at the bottom at %.1f s (%.0f cm below GO): %s;"), I, P->bAIRider ? TEXT("") : TEXT(" (player)"),
						R.StartWindow, R.GoPos[I].Z - St.Position.Z, *Sim->DumpState());
				}
				if ((bRiding && Moved > 1000.0) || R.Excused[I])
				{
					++R.RidingAtEnd;
				}
				else if (R.StartBad.Num() < 24)
				{
					R.StartBad.Add(FString::Printf(TEXT("racer %d not riding %.1f s after GO (moved %.0f cm): %s"), I, R.StartWindow, Moved, *Sim->DumpState()));
				}
			}
		}
		if (Phase == ERacePhase::Race && R.StageResetsAtGo < 0)
		{
			if (USSXTrackEffects* Fx = USSXTrackEffects::Get(Race))
			{
				R.StageResetsAtGo = Fx->NumStageResets();
				if (R.Map == TEXT("Snow") && Fx->IsActive() && Fx->GetData().NumInstances() > SnowResetStart)
				{
					const SSXEffects::FController* C = Fx->GetEffectWorld().FindController(SnowResetStart);
					R.ResetStartDebounceAtGo = C ? C->Type : -1;
				}
			}
		}
		R.CountdownFrames += Phase == ERacePhase::Countdown ? 1 : 0;
		if (Phase == ERacePhase::Race && Clock >= R.StartWindow)
		{
			R.bStartEndChecked = true;
		}
	}

	static double TrickT0Value() { double V = 7.0; FParse::Value(FCommandLine::Get(), TEXT("SSXTrickT0="), V); return V; }

	/** Pad script on the race clock (seconds since GO). */
	void Script(double T, FRaceRun& R, const FRiderState& St, uint32& Buttons, FVector2D& Stick)
	{
		using namespace SSXPadButton;
		Buttons = 0;
		Stick = FVector2D::ZeroVector;
		static const uint32 GrabCombos[] = { R1, L1, R2, L2, R1 | R2 };
		static const double TrickT0 = TrickT0Value();
		const double Dt = FMath::Clamp(T - R.LastScriptClock, 0.0, 0.25);
		R.LastScriptClock = T;
		if (T >= 4.0 && T < 5.0) { Stick = FVector2D(-1.0, 0.0); }
		else if (T >= 5.0 && T < 6.5) { Stick = FVector2D(1.0, 0.0); }
		else if (T >= 6.5 && T < 7.0) { Stick = FVector2D(-1.0, 0.0); }
		else if (T >= TrickT0 && T < 28.0)
		{
			if (St.Motion == ERiderMotion::Air)
			{
				R.RideTime = 0.0;
				R.WindUpLeft = 0.0;
				const double AirT = T - R.AirStartClock;
				if (AirT >= 0.15 && AirT < 0.75 && St.Prediction.TimeLeft > 0.5) { Buttons = GrabCombos[R.Takeoffs % UE_ARRAY_COUNT(GrabCombos)]; }
			}
			else if (St.Motion == ERiderMotion::Ground)
			{
				if (R.WindUpLeft > 0.0)
				{
					R.WindUpLeft -= Dt;
					Buttons = Cross;
				}
				else
				{
					R.RideTime += Dt;
					if (R.RideTime >= 1.5) { R.RideTime = 0.0; R.WindUpLeft = 0.75; }
				}
			}
		}
		else if (T >= 30.0 && T < 30.75) { Buttons = Cross; Stick = FVector2D(1.0, 0.0); }
		else if (T >= 30.75 && T < 35.0) { Buttons = R1; Stick = FVector2D(1.0, 0.0); }
	}

	class FRaceCommand : public IAutomationLatentCommand
	{
	public:
		FRaceCommand(FAutomationTestBase* InTest, TSharedRef<FRaceRun> InRun) : Test(InTest), Run(InRun) {}
		virtual bool Update() override;
	private:
		void Observe(UWorld* World, ASSXRaceManager* Race, ASnowboarderPawn* Player, APlayerController* PC);
		void Report();
		FAutomationTestBase* Test;
		TSharedRef<FRaceRun> Run;
	};

	bool FRaceCommand::Update()
	{
		FRaceRun& R = *Run;
		const double Now = FPlatformTime::Seconds();
		if (Now - R.WallStart > 900.0)
		{
			R.bTimedOut = true;
			R.Failure = FString::Printf(TEXT("timed out in stage %d"), R.Stage);
			Report();
			return true;
		}
		UWorld* World = GameWorld();
		if (R.Stage == 0)
		{
			USSXFrontEndSubsystem* FE = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
			if (!FE || MapOf(World) != TEXT("FrontEnd") || !World->HasBegunPlay())
			{
				return false;
			}
			FSSXFeLaunch L;
			L.GameMode = int32(R.GameMode);
			L.Track = R.TrackId;
			L.Difficulty = R.Difficulty;
			L.Slots[0].Char = 4;
			L.Slots[0].Board = 0;
			const ESSXLoadResult Res = FE->RequestLaunch(L);
			if (Res != ESSXLoadResult::Loading)
			{
				R.Failure = FString::Printf(TEXT("RequestLaunch(track %d) = %d"), R.TrackId, int32(Res));
				Report();
				return true;
			}
			R.Stage = 1;
			R.StageStart = Now;
			return false;
		}
		if (!World || MapOf(World) != R.Map)
		{
			return false;
		}
		ASSXRaceManager* Race = ASSXRaceManager::Find(World);
		APlayerController* PC = World->GetFirstPlayerController();
		ASnowboarderPawn* Player = Race ? Race->GetPlayerPawn() : nullptr;
		if (R.Stage == 1)
		{
			if (!Race || !Race->GetSession() || !Player || !PC)
			{
				return false;
			}
			R.Stage = 2;
			R.StageStart = Now;
			R.LastWall = Now;
		}
		Observe(World, Race, Player, PC);
		if (R.Stage == 3)
		{
			R.Pad.Send(PC, 0, FVector2D::ZeroVector);
			Report();
			return true;
		}
		return false;
	}

	void FRaceCommand::Observe(UWorld* World, ASSXRaceManager* Race, ASnowboarderPawn* Player, APlayerController* PC)
	{
		FRaceRun& R = *Run;
		const FRaceSession* S = Race->GetSession();
		const FRiderSim* Sim = Player->GetSim();
		if (!S || !Sim)
		{
			return;
		}
		const ERacePhase Phase = S->GetPhase();
		R.bSawCountdown |= Phase == ERacePhase::Countdown;
		if (Phase == ERacePhase::PreRace)
		{
			// 0x110AB8
			++R.PreRaceFrames;
			const bool bCard = Race->GetUIHost().GetIntroCards() > 0;
			R.CardFrames = bCard ? R.CardFrames + 1 : 0;
			R.MaxCardFrames = FMath::Max(R.MaxCardFrames, R.CardFrames);
			if ((R.bSkipIntro && R.PreRaceFrames > 90) || (!R.bSkipIntro && bCard && R.CardFrames % R.CardHoldFrames == 0))
			{
				Race->GetUIHost().PressIntroSkip();
			}
		}
		ObserveStart(R, Race, S);
		if (R.bStageCheck)
		{
			ObserveStage(R, World, Race, S);
		}
		if (R.bStartOnly)
		{
			R.Pad.Send(PC, 0, FVector2D::ZeroVector);
			R.bSawRace |= Phase == ERacePhase::Race;
			R.MaxClock = Phase == ERacePhase::Race ? FMath::Max(R.MaxClock, S->GetClock()) : R.MaxClock;
			if (R.bStartEndChecked && R.RestartsLeft > 0)
			{
				// 0x180A10
				--R.RestartsLeft;
				++R.RestartsDone;
				R.bStartEndChecked = false;
				R.RidingAtEnd = 0;
				R.PreRaceFrames = 0;
				R.PlacementsBefore = S->GetPlacements();
				Race->RestartRace();
				return;
			}
			if (R.bStartEndChecked || (R.bSawRace && Phase != ERacePhase::Race && R.RestartsDone == 0))
			{
				R.Stage = 3;
			}
			return;
		}
		R.MaxIntroStep = FMath::Max(R.MaxIntroStep, S->GetIntroStep());
		R.bIntroDone |= S->GetIntroStep() == SSXIntro::Done;
		R.bIntroMusicEnd |= S->GetIntroMusicEnds() > 0;
		R.IntroScripts = 0;
		for (const FString& A : S->IntroLog)
		{
			R.IntroScripts += A.StartsWith(TEXT("director_play:")) && !A.EndsWith(TEXT("PreLoad")) ? 1 : 0;
		}
		R.IntroSlots = FString::Printf(TEXT("%s / %s / %s"), *SSXIntro::ScriptName(S->IntroSlots[0].Type), *SSXIntro::ScriptName(S->IntroSlots[1].Type),
			*SSXIntro::ScriptName(S->IntroSlots[2].Type));
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(World))
		{
			R.bSpeechLoaded = Audio->GetSpeech().GetSet() == FSSXSpeechSystem::ESet::Race;
			R.SpeechPosts = 0;
			R.SpeechLines = 0;
			for (const FSSXSpeechSystem::FLog& L : Audio->GetSpeech().Log)
			{
				R.SpeechPosts += L.bPosted ? 1 : 0;
				R.SpeechLines += L.Lines;
			}
		}
		const double Now = FPlatformTime::Seconds();
		if (Phase != ERacePhase::Race)
		{
			R.Pad.Send(PC, 0, FVector2D::ZeroVector);
			if (R.bSawRace)
			{
				R.Stage = 3;
			}
			R.LastWall = Now;
			return;
		}

		if (R.bSawRace)
		{
			const double Ms = (Now - R.LastWall) * 1000.0;
			R.SumMs += Ms; R.MaxMs = FMath::Max(R.MaxMs, Ms);
			++R.TimedFrames;
		}
		R.LastWall = Now;

		const double T = S->GetClock();
		if (!R.bSawRace)
		{
			R.bSawRace = true;
			R.Racers = S->NumRacers();
			for (int32 I = 0; I < R.Racers; ++I)
			{
				const ASnowboarderPawn* P = Race->GetPawn(I);
				if (!P) { R.Missing.Add(FString::Printf(TEXT("racer %d pawn"), I)); continue; }
				const USSXCharacterAppearance* App = P->GetAppearance();
				R.ShownRiders += App && !App->GetShownId().IsEmpty() ? 1 : 0;
				R.AnimatedRiders += App && App->IsAnimated() ? 1 : 0;
				R.PlaceholderParts += VisiblePlaceholderParts(P);
				R.AIStart.Add(P->GetActorLocation());
				R.AIMoved.Add(0.0);
			}
			TArray<FRaceStanding> Rows;
			S->GetStandings(Rows);
			R.AIRemainingStart.SetNumZeroed(R.Racers);
			for (const FRaceStanding& Row : Rows) { if (R.AIRemainingStart.IsValidIndex(Row.Racer)) { R.AIRemainingStart[Row.Racer] = Row.Remaining; } }
			if (const USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(World))
			{
				R.Song = Audio->GetCurrentSongName();
			}
		}
		R.bClockMonotonic &= T + 1e-6 >= R.LastClock;
		R.LastClock = T;
		R.MaxClock = FMath::Max(R.MaxClock, T);
		++R.RaceFrames;
		if (T >= R.RaceSeconds)
		{
			R.Stage = 3;
		}

		uint32 Buttons = 0;
		FVector2D Stick;
		const FRiderState& St = Sim->State;
		Script(T, R, St, Buttons, Stick);
		R.Pad.Send(PC, Buttons, Stick);

		const USSXCharacterAppearance* App = Player->GetAppearance();
		const SSXAnim::FRiderAnimDriver* Anim = App ? App->GetAnimator() : nullptr;
		const int32 AnimId = Anim ? Anim->GetController().CurrentId(0) : -1;
		const SSXAir::FAirContext* Air = SSXAir::GetAirContext(*Sim);
		if (St.Motion == ERiderMotion::Air && R.PrevMotion != ERiderMotion::Air)
		{
			++R.Takeoffs;
			R.AirStartClock = T;
			R.AirStartFrame = St.Frame;
			R.ScoreBefore = Air ? Air->Score.Score : 0.0;
		}
		if (St.Motion == ERiderMotion::Air)
		{
			if (St.Frame - R.AirStartFrame <= 15 && AnimId != R.PreAirAnim && R.PreAirAnim >= 0) { R.bAnimOnJump = true; }
			const bool bGrab = St.TrickId != FRiderInputWord::NoTrick;
			if (bGrab && !R.bWasGrab) { ++R.Grabs; }
			if (bGrab && AnimId != R.PreGrabAnim && R.PreGrabAnim >= 0) { R.bAnimOnGrab = true; }
			if (!bGrab) { R.PreGrabAnim = AnimId; }
			R.bWasGrab = bGrab;
			R.MaxPosedBones = FMath::Max(R.MaxPosedBones, PosedBones(App ? App->GetRiderMesh() : nullptr));
		}
		else
		{
			R.PreAirAnim = AnimId;
			R.PreGrabAnim = AnimId;
			R.bWasGrab = false;
		}
		if (const UPoseableMeshComponent* Mesh = App ? App->GetRiderMesh() : nullptr)
		{
			R.Bones = Mesh->BoneSpaceTransforms.Num();
		}
		{
			const int32 CNow = S->GetRacer(Race->GetRacerIndex(Player)).RiderContacts + S->GetRiderKnockdowns();
			if (CNow > R.ContactsSeenPrev) { R.LastContactFrame = R.RaceFrames; R.ContactFrames.Add(R.RaceFrames); }
			R.ContactsSeenPrev = CNow;
		}
		if (St.Motion == ERiderMotion::WipeOut && R.PrevMotion != ERiderMotion::WipeOut)
		{
			++R.WipeOuts;
			R.PendingWipeFrame = R.RaceFrames;
			if (R.FirstPlayerWipeClock < 0.0)
			{
				R.FirstPlayerWipeClock = T;
				R.FirstPlayerWipe = Sim->DumpState();
				R.FirstWipeFrame = R.RaceFrames;
			}
		}
		{
			const bool bIn = St.Motion == ERiderMotion::WipeOut || St.Motion == ERiderMotion::WipeOutRecover;
			const bool bWas = R.PrevMotion == ERiderMotion::WipeOut || R.PrevMotion == ERiderMotion::WipeOutRecover;
			if (bIn && !bWas) { R.WipeStartFrame = R.RaceFrames; }
			if (!bIn && bWas && R.WipeStartFrame >= 0)
			{
				R.WipeLog += FString::Printf(TEXT(" f%d: %d frames -> motion %d;"), R.WipeStartFrame, R.RaceFrames - R.WipeStartFrame, int32(St.Motion));
				R.WipeStartFrame = -1;
			}
		}
		R.RailFrames += St.Motion == ERiderMotion::RailSlide ? 1 : 0;
		if (R.PrevMotion == ERiderMotion::RailSlide && St.Motion == ERiderMotion::Air && Air)
		{
			// 0x156618
			++R.RailExits;
			R.RailExitPoints += Air->Score.LastPoints;
			R.RailScoredExits += Air->Score.LastPoints > 0 ? 1 : 0;
		}
		{
			int32 Contacts = 0, Impulses = 0;
			for (int32 I = 0; I < S->NumRacers(); ++I)
			{
				Contacts += S->GetRacer(I).RiderContacts;
				Impulses += S->GetRacer(I).RiderImpulses;
			}
			R.ContactsTotal = Contacts;
			if (R.ContactsAtGo < 0) { R.ContactsAtGo = Contacts; }
			R.PlayerContacts = S->GetRacer(Race->GetRacerIndex(Player)).RiderContacts;
			R.ImpulsesTotal = Impulses;
			R.RiderKnockdowns = S->GetRiderKnockdowns();
			if (!R.bOverlapPlaced && T >= 35.0)
			{
				const int32 Me = Race->GetRacerIndex(Player);
				for (int32 I = 0; I < S->NumRacers() && R.OverlapRacer < 0; ++I)
				{
					ASnowboarderPawn* P = Race->GetPawn(I);
					FRiderSim* O = (I != Me && P) ? P->GetSim() : nullptr;
					if (O && (O->State.Motion == ERiderMotion::Ground || O->State.Motion == ERiderMotion::Air))
					{
						O->State.Position = St.Position + 45.0 * St.BasisSide;
						O->State.Velocity = St.Velocity;
						O->State.Orientation = St.Orientation;
						O->UpdateBasis();
						R.OverlapRacer = I;
					}
				}
				R.bOverlapPlaced = R.OverlapRacer >= 0;
				R.ContactsBefore = Contacts;
			}
			else if (R.bOverlapPlaced && R.OverlapFrames < 30)
			{
				++R.OverlapFrames;
				R.ContactsAfter = Contacts;
			}
		}
		if (Air && Air->LastLanding.bValid && Air->LastLanding.Frame != R.LastLandingFrame)
		{
			R.LastLandingFrame = Air->LastLanding.Frame;
			++R.Landings;
			if (Air->LastLanding.Verdict != SSXAir::ELandingVerdict::WipeOut && Air->LastLanding.Points > 0)
			{
				++R.ScoredLandings;
				R.HudWindow = 90;
			}
		}
		if (const ASSXHUD* Hud = Cast<ASSXHUD>(PC->GetHUD()); Hud && (St.Motion == ERiderMotion::Air || R.HudWindow > 0))
		{
			FSSXHudInput In;
			Hud->BuildInput(In);
			if (In.Players.Num() > 0)
			{
				const FSSXHudPlayerInput& Pl = In.Players[0];
				if (R.HudWindow > 0)
				{
					if (!Pl.TrickText.IsEmpty()) { R.bHudTrickText = true; R.HudTrickText = Pl.TrickText; }
					for (const FSSXHudMsg& M : Pl.Messages) { R.bHudPopup |= M.Type != 0; }
					R.bScoreRose |= Pl.Score > R.ScoreBefore;
					for (const FString& Drawn : Hud->GetDrawList().Texts)
					{
						R.bHudDrawnText |= !Pl.TrickText.IsEmpty() && Drawn.Contains(Pl.TrickText.Left(8));
					}
				}
			}
		}
		R.HudWindow = FMath::Max(0, R.HudWindow - 1);
		R.PrevMotion = St.Motion;
		if (R.RaceFrames % 30 == 1)
		{
			UE_LOG(LogSSX, Display, TEXT("[smoke] t=%5.2f f%d pad %04x stick %+.0f %s/%s v %.0f pos (%.0f %.0f %.0f) rank %d anim 0x%X"),
				T, St.Frame, Buttons, Stick.X, LexToString(St.Motion), LexToString(St.Control), St.Velocity.Size(),
				St.Position.X, St.Position.Y, St.Position.Z, S->GetRacer(Race->GetRacerIndex(Player)).Rank, AnimId);
		}

		R.bAudioDevice = World->GetAudioDeviceRaw() != nullptr;
		if (const USSXBoardSoundComponent* Board = Player->GetBoardSound(); Board && St.Motion == ERiderMotion::Ground)
		{
			R.MaxBoardVolume = FMath::Max(R.MaxBoardVolume, Board->LastVolume);
			R.MaxLoops = FMath::Max(R.MaxLoops, Board->NumPlayingLoops());
		}
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(World))
		{
			R.bFlowGo |= Audio->GetRaceFlow().bGo;
			if (T < 3.0)
			{
				R.Song = Audio->GetCurrentSongName();
				R.bSongAtGo |= !R.Song.IsEmpty() && (!R.bAudioDevice || Audio->IsPathfinderPlaying());
			}
		}

		if (const USSXParticleWorld* Fx = USSXParticleWorld::Get(World))
		{
			const int32 Bursts = Fx->GetSprayBurstCount(Player);
			if (T >= 4.0 && T < 7.0)
			{
				if (R.SprayAtCarveStart < 0) { R.SprayAtCarveStart = Bursts; }
				R.SprayCarve = Bursts - R.SprayAtCarveStart;
			}
			R.bShadow |= Fx->IsShadowActive(Player);
		}

		if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
		{
			R.MaxCamDist = FMath::Max(R.MaxCamDist, FVector::Dist(Cam->GetCameraLocation(), Player->GetActorLocation()));
			R.MinFov = FMath::Min(R.MinFov, double(Cam->GetFOVAngle()));
			R.MaxFov = FMath::Max(R.MaxFov, double(Cam->GetFOVAngle()));
			++R.CamSamples;
		}
		R.bRaceCamera = Player->GetRaceCamera() != nullptr;

		if (USSXTrackEffects* Fx = USSXTrackEffects::Get(World))
		{
			R.MaxCells = FMath::Max(R.MaxCells, Fx->NumActiveCells());
			R.MaxDead = FMath::Max(R.MaxDead, DeadInstances(Fx->GetEffectWorld()));
		}
		if (const USSXPropAnimation* Props = USSXPropAnimation::Get(World))
		{
			R.MaxCrowdNodes = FMath::Max(R.MaxCrowdNodes, Props->NumNodes(SSXEffects::TypeCrowdBox));
			R.MaxAnimNodes = FMath::Max(R.MaxAnimNodes, Props->NumNodes(SSXEffects::TypeAnimObject));
			R.CrowdSteps = Props->GetCrowdSteps();
			R.Rollers = Props->GetRollersKnocked();
			R.RollerTravel = Props->GetMaxRollerTravel();
		}

		for (int32 I = 0; I < R.AIStart.Num(); ++I)
		{
			if (const ASnowboarderPawn* P = Race->GetPawn(I))
			{
				R.AIMoved[I] = FMath::Max(R.AIMoved[I], FVector::Dist(P->GetActorLocation(), R.AIStart[I]));
			}
		}
		const int32 Rank = S->GetRacer(Race->GetRacerIndex(Player)).Rank;
		R.RankChanges += R.LastRank >= 0 && Rank != R.LastRank ? 1 : 0;
		R.LastRank = Rank;
		if (R.Stage == 3)
		{
			TArray<FRaceStanding> Rows;
			S->GetStandings(Rows);
			R.AIRemainingEnd.SetNumZeroed(R.Racers);
			for (const FRaceStanding& Row : Rows) { if (R.AIRemainingEnd.IsValidIndex(Row.Racer)) { R.AIRemainingEnd[Row.Racer] = Row.bFinished ? 0.0 : Row.Remaining; } }
		}
	}

	void FRaceCommand::Report()
	{
		FRaceRun& R = *Run;
		FAutomationTestBase& T = *Test;
		FApp::SetUseFixedTimeStep(R.bPrevFixed);
		FApp::SetFixedDeltaTime(R.PrevFixedDt);
		FSSXMemCard::RootOverride() = R.PrevMemCard;
		const TArray<FString> Errors = R.Errors ? R.Errors->Get() : TArray<FString>();
		R.Errors.Reset();
		if (!R.Failure.IsEmpty())
		{
			T.AddError(FString::Printf(TEXT("[%s] %s"), *R.Map, *R.Failure));
		}
		const TCHAR* M = *R.Map;
		auto Check = [&](const FString& What, bool bOk) { T.TestTrue(FString::Printf(TEXT("[%s] %s"), M, *What), bOk); };
		if (R.bStartOnly)
		{
			if (!R.Failure.IsEmpty())
			{
				return;
			}
			T.AddInfo(FString::Printf(TEXT("[%s] race start (intro %s, %d restarts): %d racers, %d countdown frames, %d riding %.0f s after the last GO, race clock %.2f"),
				M, R.bSkipIntro ? TEXT("skipped") : TEXT("played"), R.RestartsDone, R.StartRacers, R.CountdownFrames, R.RidingAtEnd, R.StartWindow, R.MaxClock));
			Check(TEXT("every restart reached a new start"), R.RestartsLeft == 0);
			T.AddInfo(FString::Printf(TEXT("[%s] longest pre-race card hold %d frames; stage resets by GO %d; AI wipe-outs in the start window %d%s"),
				M, R.MaxCardFrames, R.StageResetsAtGo, R.AIBails, *R.AIBailLog));
			if (!R.bSkipIntro && R.CardHoldFrames > 30)
			{
				Check(FString::Printf(TEXT("a pre-race card was held %d frames"), R.CardHoldFrames), R.MaxCardFrames >= R.CardHoldFrames);
			}
			// 0x113478, 0x191298
			Check(TEXT("the stage was reset at StartRace (replay state 0xB)"), R.StageResetsAtGo >= 1);
			if (R.bDropCheck)
			{
				T.AddInfo(FString::Printf(TEXT("[%s] start drop landings:%s"), M, *R.DropLog));
				int32 Landed = 0;
				for (bool B : R.DropLanded) { Landed += B ? 1 : 0; }
				Check(TEXT("every racer reached the bottom of the start drop"), Landed == R.StartRacers);
				Check(TEXT("racers land the start drop cleanly (an AI at time scale 1.0 always does)"), R.DropClean > 0 && R.DropCleanUnscaled > 0);
				T.AddInfo(FString::Printf(TEXT("[%s] %d AI drop bails with the grab held at a catch-up time scale > 1 (the original's unscaled predictor clock, 0x123EC8)"),
					M, R.ScaledGrabBails));
			}
			if (R.Map == TEXT("Snow"))
			{
				Check(TEXT("Snowdream start volume (Mdl_Trigger_ResetStart_1000) held by its Debounce at GO"), R.ResetStartDebounceAtGo == SSXEffects::TypeDebounce);
			}
			if (R.bStageCheck)
			{
				const FRaceRun::FStageSnap& A = R.Snap[0];
				const FRaceRun::FStageSnap& B = R.Snap[1];
				T.AddInfo(FString::Printf(TEXT("[%s] stage actions: %s"), M, *R.StageActed));
				Check(TEXT("both countdowns snapshotted"), A.bTaken && B.bTaken);
				Check(TEXT("the actions changed the stage (pane broken, cone node, flags)"), R.bPaneHidden && R.bRollerNode && R.StageFlagDiffs > 0);
				int32 FlagDiffs = 0, DisplayOnly = 0;
				FString First;
				for (int32 I = 0; I < FMath::Min(A.Flags.Num(), B.Flags.Num()); ++I)
				{
					if (A.Flags[I] == B.Flags[I]) { continue; }
					if ((A.Flags[I] ^ B.Flags[I]) == SSXEffects::FlDisplay) { ++DisplayOnly; continue; }
					if (FlagDiffs++ < 6) { First += FString::Printf(TEXT(" %d: 0x%04X -> 0x%04X;"), I, A.Flags[I], B.Flags[I]); }
				}
				int32 CtlDiffs = 0;
				for (const TPair<int32, int32>& P : A.Ctls) { const int32* V = B.Ctls.Find(P.Key); CtlDiffs += (!V || *V != P.Value) ? 1 : 0; }
				for (const TPair<int32, int32>& P : B.Ctls) { CtlDiffs += A.Ctls.Contains(P.Key) ? 0 : 1; }
				T.AddInfo(FString::Printf(TEXT("[%s] countdown 1 vs after the restart: %d / %d controllers, rollers %d / %d, debris %d / %d, keyframed %d / %d, emitters %d / %d, scripts %d / %d, rails on %d / %d, track sounds %d / %d; %d flag words differ (%d by the display bit only)%s"),
					M, A.Ctls.Num(), B.Ctls.Num(), A.Rollers, B.Rollers, A.MeshAnims, B.MeshAnims, A.Anims, B.Anims, A.Emitters, B.Emitters,
					A.Scripts, B.Scripts, A.Rails, B.Rails, A.TrackSounds, B.TrackSounds, FlagDiffs, DisplayOnly, *First));
				Check(TEXT("restart: every instance's flags as at the first countdown"), FlagDiffs == 0);
				Check(TEXT("restart: the same controller nodes as at the first countdown"), CtlDiffs == 0);
				Check(TEXT("restart: no knocked cone, debris or keyframed node left"), A.Rollers == B.Rollers && A.MeshAnims == B.MeshAnims && A.Anims == B.Anims);
				Check(TEXT("restart: emitters, scripts and rails as at the first countdown"), A.Emitters == B.Emitters && A.Scripts == B.Scripts && A.Rails == B.Rails);
				Check(TEXT("restart: no track sound of the old race playing"), B.TrackSounds <= A.TrackSounds);
			}
			for (const FString& B : R.StartBad)
			{
				T.AddError(FString::Printf(TEXT("[%s] %s"), M, *B));
			}
			Check(TEXT("countdown seen, then GO"), R.CountdownFrames > 0 && R.bSawRace);
			Check(TEXT("every racer in Static + gate / sit wait through the countdown"), R.CountdownBadStates == 0);
			Check(FString::Printf(TEXT("no wipe-out / reset wait from the start placement to %.0f s after GO"), R.StartWindow), R.StartBad.Num() == 0);
			Check(FString::Printf(TEXT("every racer riding %.0f s after GO"), R.StartWindow), R.bStartEndChecked && R.RidingAtEnd == R.StartRacers && R.StartRacers == 6);
			for (const FString& E : Errors)
			{
				T.AddError(FString::Printf(TEXT("[%s] error log: %s"), M, *E));
			}
			return;
		}
		T.AddInfo(FString::Printf(TEXT("[%s] %d racers, race clock %.1f s over %d frames; %d take-offs, %d grabs, %d landings (%d scored), %d wipe-outs, %d rail frames"),
			M, R.Racers, R.MaxClock, R.RaceFrames, R.Takeoffs, R.Grabs, R.Landings, R.ScoredLandings, R.WipeOuts, R.RailFrames));
		T.AddInfo(FString::Printf(TEXT("[%s] HUD trick text '%s' popup %d drawn %d score-rise %d; anim jump %d grab %d, posed bones %d/%d"),
			M, *R.HudTrickText, R.bHudPopup, R.bHudDrawnText, R.bScoreRose, R.bAnimOnJump, R.bAnimOnGrab, R.MaxPosedBones, R.Bones));
		T.AddInfo(FString::Printf(TEXT("[%s] audio device %d, board volume %.2f, loops %d, song '%s' at GO %d (flow GO %d); spray bursts while carving %d, shadow %d"),
			M, R.bAudioDevice, R.MaxBoardVolume, R.MaxLoops, *R.Song, R.bSongAtGo, R.bFlowGo, R.SprayCarve, R.bShadow));
		T.AddInfo(FString::Printf(TEXT("[%s] camera max dist %.0f cm, FOV %.1f..%.1f; cells %d, dead instances %d; riders shown %d animated %d, placeholder parts visible %d"),
			M, R.MaxCamDist, R.MinFov, R.MaxFov, R.MaxCells, R.MaxDead, R.ShownRiders, R.AnimatedRiders, R.PlaceholderParts));
		FString Ai;
		for (int32 I = 0; I < R.AIMoved.Num(); ++I)
		{
			Ai += FString::Printf(TEXT(" %d: moved %.0f m, remaining %.0f -> %.0f m;"), I, R.AIMoved[I] / 100.0,
				R.AIRemainingStart.IsValidIndex(I) ? R.AIRemainingStart[I] / 100.0 : -1.0, R.AIRemainingEnd.IsValidIndex(I) ? R.AIRemainingEnd[I] / 100.0 : -1.0);
		}
		T.AddInfo(FString::Printf(TEXT("[%s] racers:%s player rank changes %d"), M, *Ai, R.RankChanges));
		T.AddInfo(FString::Printf(TEXT("[%s] player wipe-outs:%s"), M, R.WipeLog.IsEmpty() ? TEXT(" none") : *R.WipeLog));
		const FString Budget = FString::Printf(TEXT("[%s] game-thread frame time over %d race frames: avg %.2f ms, max %.2f ms (whole engine frame, NullRHI, fixed 60 Hz step; readout only)"),
			M, R.TimedFrames, R.TimedFrames ? R.SumMs / R.TimedFrames : 0.0, R.MaxMs);
		UE_LOG(LogSSX, Display, TEXT("[smoke] %s"), *Budget);
		T.AddInfo(Budget);
		if (!R.Failure.IsEmpty())
		{
			return;
		}

		T.AddInfo(FString::Printf(TEXT("[%s] pre-race intro: slots %s, %d slot scripts played, max step %d, done %d, intro music end %d; speech loaded %d: %d requests posted, %d lines"),
			M, *R.IntroSlots, R.IntroScripts, R.MaxIntroStep, R.bIntroDone, R.bIntroMusicEnd, R.bSpeechLoaded, R.SpeechPosts, R.SpeechLines));
		Check(TEXT("pre-race intro ran to step 0x15"), R.bIntroDone && R.MaxIntroStep == SSXIntro::Done);
		Check(TEXT("pre-race step 0x12 ended the intro music"), R.bIntroMusicEnd);
		if (R.bSpeechLoaded)
		{
			Check(TEXT("announcer requests posted (narration / GO lines)"), R.SpeechPosts > 0 && R.SpeechLines > 0);
		}
		else
		{
			SSXTest::Skip(T, TEXT("speech databases not converted (tools/convert_speech.py): announcer requests not checked"));
		}
		Check(TEXT("countdown seen, then GO (race phase)"), R.bSawCountdown && R.bSawRace);
		Check(TEXT("race clock advances monotonically past 30 s"), R.bClockMonotonic && R.MaxClock >= 30.0);
		Check(TEXT("rider took off and grabbed"), R.Takeoffs > 0 && R.Grabs > 0);
		static int32 SessionRaces = 0, SessionScored = 0;
		++SessionRaces;
		SessionScored += R.ScoredLandings;
		if (R.ScoredLandings > 0)
		{
			Check(TEXT("HUD trick-name text after a landed trick"), R.bHudTrickText);
			Check(TEXT("HUD popup message after a landed trick"), R.bHudPopup);
			Check(TEXT("HUD draws the trick text"), R.bHudDrawnText);
			Check(TEXT("score increased"), R.bScoreRose);
		}
		else
		{
			T.AddInfo(FString::Printf(TEXT("[%s] no scored landing in this scripted run (chaotic): scoring/HUD checks deferred to the combined smoke races"), M));
		}
		if (SessionRaces >= 2 || R.ScoredLandings > 0)
		{
			Check(TEXT("a trick landed and scored (across the smoke races run so far)"), SessionScored > 0);
		}
		Check(TEXT("animation clip id changes on the jump"), R.bAnimOnJump);
		Check(TEXT("animation clip id changes on the grab"), R.bAnimOnGrab);
		Check(TEXT("rider mesh posed away from the bind pose"), R.MaxPosedBones > 5);
		Check(TEXT("board sound volume > 0 while riding"), R.MaxBoardVolume > 0.f);
		if (R.bAudioDevice)
		{
			Check(TEXT("board audio loops playing"), R.MaxLoops > 0);
		}
		else
		{
			T.AddInfo(TEXT("no audio device: loop playback not checked"));
		}
		Check(TEXT("race flow reached GO"), R.bFlowGo);
		bool bRiderHit = false;
		for (const int32 CF : R.ContactFrames) { bRiderHit |= CF >= R.FirstWipeFrame - 30 && CF <= R.FirstWipeFrame + 10; }
		if (R.FirstPlayerWipeClock >= 0.0 && R.FirstPlayerWipeClock < 5.0 && bRiderHit)
		{
			T.AddInfo(FString::Printf(TEXT("[%s] player knocked down by another rider %.2f s after GO (excused)"), M, R.FirstPlayerWipeClock));
		}
		else if (R.FirstPlayerWipeClock >= 0.0 && R.FirstPlayerWipeClock < 5.0)
		{
			T.AddError(FString::Printf(TEXT("[%s] player wiped out %.2f s after GO without a scripted cause: %s"), M, R.FirstPlayerWipeClock, *R.FirstPlayerWipe));
		}
		for (const FString& B : R.StartBad) { T.AddInfo(FString::Printf(TEXT("[%s] start: %s"), M, *B)); }
		T.AddInfo(FString::Printf(TEXT("[%s] rails: %d rail frames, %d exits (%d scored, %d points); rider collision: scripted overlap racer %d, contacts %d -> %d in 30 frames; race totals %d contacts (%d before GO, player's own pass %d), %d impulses / shove hits, %d knock-downs"),
			M, R.RailFrames, R.RailExits, R.RailScoredExits, R.RailExitPoints, R.OverlapRacer, R.ContactsBefore, R.ContactsAfter, R.ContactsTotal, R.ContactsAtGo, R.PlayerContacts,
			R.ImpulsesTotal, R.RiderKnockdowns));
		if (R.RailExits > 0)
		{
			Check(TEXT("every rail exit scores"), R.RailScoredExits == R.RailExits);
		}
		else
		{
			T.AddInfo(FString::Printf(TEXT("[%s] no rail exit reached by the scripted run: rail score covered by SSX.Rail.Score"), M));
		}
		Check(TEXT("scripted overlap: the riders collide"), R.bOverlapPlaced && R.ContactsAfter > R.ContactsBefore);
		Check(TEXT("track song starts at GO"), R.bSongAtGo);
		Check(TEXT("spray bursts while carving"), R.SprayCarve > 0);
		Check(TEXT("rider shadow active"), R.bShadow);
		Check(TEXT("race camera present"), R.bRaceCamera);
		Check(TEXT("camera near the rider"), R.CamSamples > 0 && R.MaxCamDist < 3000.0);
		Check(TEXT("camera FOV in range"), R.CamSamples > 0 && R.MinFov > 20.0 && R.MaxFov < 130.0);
		Check(TEXT("track cells activated"), R.MaxCells > 0);
		T.AddInfo(FString::Printf(TEXT("[%s] prop nodes: crowd boxes %d (steps %d), keyframed %d, rollers knocked %d (max travel %.0f cm)"),
			M, R.MaxCrowdNodes, R.CrowdSteps, R.MaxAnimNodes, R.Rollers, R.RollerTravel));
		Check(TEXT("crowd frames advance (CrowdBox steps)"), R.MaxCrowdNodes == 0 || R.CrowdSteps > 0);
		if (R.Rollers > 0)
		{
			Check(TEXT("a touched cone / dynamic prop is knocked away"), R.RollerTravel > 10.0);
		}
		if (R.bExpectDead)
		{
			Check(TEXT("mode script ran (dead instances)"), R.MaxDead > 0);
		}
		Check(TEXT("six racers"), R.Racers == 6);
		int32 Moving = 0, Closing = 0;
		for (int32 I = 0; I < R.AIMoved.Num(); ++I)
		{
			Moving += R.AIMoved[I] > 5000.0 ? 1 : 0;
			Closing += R.AIRemainingStart.IsValidIndex(I) && R.AIRemainingEnd.IsValidIndex(I) && R.AIRemainingEnd[I] < R.AIRemainingStart[I] - 5000.0 ? 1 : 0;
		}
		Check(TEXT("every racer moved > 50 m"), Moving == R.Racers);
		Check(TEXT("standings distances update"), Closing == R.Racers);
		Check(TEXT("every rider model loaded (no placeholder)"), R.ShownRiders == R.Racers && R.PlaceholderParts == 0 && R.Missing.Num() == 0);
		Check(TEXT("every rider animated"), R.AnimatedRiders == R.Racers);
		for (const FString& E : Errors)
		{
			T.AddError(FString::Printf(TEXT("[%s] error log: %s"), M, *E));
		}
	}

	bool Prerequisites(const FString& Track, const FString& Map, FString& OutMissing)
	{
		const FString Sim = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"));
		for (const FString& F : { Track + TEXT(".patches.bin"), Track + TEXT(".race.bin"), Track + TEXT(".effects.bin"), FString(TEXT("anim_clips.bin")) })
		{
			if (!FPaths::FileExists(FPaths::Combine(Sim, F))) { OutMissing += TEXT(" SimData/") + F; }
		}
		for (const FString& Name : { Map, FString(TEXT("FrontEnd")) })
		{
			if (!FPaths::FileExists(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Maps"), Name + TEXT(".umap")))) { OutMissing += TEXT(" Maps/") + Name; }
		}
		if (!FPaths::DirectoryExists(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Characters"), TEXT("moby"))))
		{
			OutMissing += TEXT(" Content/Characters/moby");
		}
		return OutMissing.IsEmpty();
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FEndPieIfRunning, int32, Unused);
	bool FEndPieIfRunning::Update()
	{
		if (GEditor && GEditor->PlayWorld)
		{
			GEditor->RequestEndPlayMap();
		}
		return true;
	}

	void QueueRace(FAutomationTestBase* Test, const FString& Track, const FString& Map, int32 TrackId, bool bExpectDead,
	               bool bStartOnly = false, bool bSkipIntro = true, int32 Difficulty = 0, ERaceEventMode Mode = ERaceEventMode::CircuitRace,
	               bool bStageCheck = false, int32 CardHoldFrames = 30, int32 Restarts = -1)
	{
		FString Missing;
		if (!Prerequisites(Track, Map, Missing))
		{
			SSXTest::Skip(*Test, FString::Printf(TEXT("SKIPPED %s: missing%s (tools/export_*.py, Scripts/build_track_map.py, Scripts/import_characters.py)"), *Map, *Missing));
			return;
		}
		TSharedRef<FRaceRun> Run = MakeShared<FRaceRun>();
		Run->Map = Map;
		Run->TrackId = TrackId;
		Run->bExpectDead = bExpectDead;
		Run->bStartOnly = bStartOnly;
		Run->bSkipIntro = bSkipIntro;
		Run->Difficulty = Difficulty;
		Run->GameMode = Mode;
		Run->RestartsLeft = Restarts >= 0 ? Restarts : (bStageCheck ? 1 : (bStartOnly ? 2 : 0));
		Run->CardHoldFrames = FMath::Max(CardHoldFrames, 1);
		if (bStartOnly && Map == TEXT("Alaska"))
		{
			// 0x106228, 0x1062B0
			Run->bDropCheck = true;
			Run->StartWindow = 3.5;
		}
		Run->bStageCheck = bStageCheck;
		Run->WallStart = FPlatformTime::Seconds();
		Run->Errors = MakeUnique<FErrorCapture>();
		{
			uint32 Seed = TestRngSeed;
			FParse::Value(FCommandLine::Get(), TEXT("SSXTestSeed="), Seed);
			FGameRng::StreamA() = FGameRng(Seed);
			FGameRng::StreamB() = FGameRng();
			Test->AddInfo(FString::Printf(TEXT("[%s] PRNG stream A boot seed 0x%08X"), *Map, Seed));
		}
		Run->PrevMemCard = FSSXMemCard::RootOverride();
		FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardSmoke"));
		IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
		Run->bPrevFixed = FApp::UseFixedTimeStep();
		Run->PrevFixedDt = FApp::GetFixedDeltaTime();
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(1.0 / 60.0);
		AutomationOpenMap(TEXT("/Game/Maps/FrontEnd"), true);
		ADD_LATENT_AUTOMATION_COMMAND(FRaceCommand(Test, Run));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPieIfRunning(0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceTest, "SSX.Smoke.Race", SSXSmoke::Flags)

bool FSSXSmokeRaceTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("gari"), TEXT("Gari"), 0, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartSnowTest, "SSX.Smoke.RaceStart.Snow", SSXSmoke::Flags)
bool FSSXSmokeRaceStartSnowTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("snow"), TEXT("Snow"), 1, false, true, false, 1, ERaceEventMode::PracticeRace);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartSnowCircuitTest, "SSX.Smoke.RaceStart.SnowCircuit", SSXSmoke::Flags)
bool FSSXSmokeRaceStartSnowCircuitTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("snow"), TEXT("Snow"), 1, false, true, true, 0, ERaceEventMode::CircuitRace);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartGariTest, "SSX.Smoke.RaceStart.Gari", SSXSmoke::Flags)
bool FSSXSmokeRaceStartGariTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("gari"), TEXT("Gari"), 0, false, true, false, 1, ERaceEventMode::CircuitRace);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartElysiumTest, "SSX.Smoke.RaceStart.Elysium", SSXSmoke::Flags)
bool FSSXSmokeRaceStartElysiumTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("elysium"), TEXT("Elysium"), 2, false, true, true, 0, ERaceEventMode::PracticeRace);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartSnowCard5Test, "SSX.Smoke.RaceStart.SnowCard5", SSXSmoke::Flags)
bool FSSXSmokeRaceStartSnowCard5Test::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("snow"), TEXT("Snow"), 1, false, true, false, 1, ERaceEventMode::PracticeRace, false, 5 * 60, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartSnowCard15Test, "SSX.Smoke.RaceStart.SnowCard15", SSXSmoke::Flags)
bool FSSXSmokeRaceStartSnowCard15Test::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("snow"), TEXT("Snow"), 1, false, true, false, 0, ERaceEventMode::CircuitRace, false, 15 * 60, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartSnowFirstTest, "SSX.Smoke.RaceStart.SnowFirst", SSXSmoke::Flags)
bool FSSXSmokeRaceStartSnowFirstTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("snow"), TEXT("Snow"), 1, false, true, false, 0, ERaceEventMode::CircuitRace, false, 30, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartMegapleTest, "SSX.Smoke.RaceStart.Megaple", SSXSmoke::Flags)
bool FSSXSmokeRaceStartMegapleTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("megaple"), TEXT("Megaple"), 8, false, true, false, 0, ERaceEventMode::CircuitRace, false, 30, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartMerquerTest, "SSX.Smoke.RaceStart.Merquer", SSXSmoke::Flags)
bool FSSXSmokeRaceStartMerquerTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("merquer"), TEXT("Merquer"), 4, false, true, false, 0, ERaceEventMode::CircuitRace, false, 30, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceStartAlaskaTest, "SSX.Smoke.RaceStart.Alaska", SSXSmoke::Flags)
bool FSSXSmokeRaceStartAlaskaTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("alaska"), TEXT("Alaska"), 11, false, true, true, 0, ERaceEventMode::CircuitRace, false, 30, 0);
	return true;
}

// 0x180A10
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceRestartResetsStageTest, "SSX.Race.RestartResetsStage", SSXSmoke::Flags)
bool FSSXRaceRestartResetsStageTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("megaple"), TEXT("Megaple"), 8, false, true, true, 0, ERaceEventMode::PracticeRace, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeRaceAlaskaTest, "SSX.Smoke.RaceAlaska", SSXSmoke::Flags)

bool FSSXSmokeRaceAlaskaTest::RunTest(const FString& Parameters)
{
	SSXSmoke::QueueRace(this, TEXT("alaska"), TEXT("Alaska"), 11, false);
	return true;
}

namespace SSXSmoke
{
	struct FFeRun
	{
		double WallStart = 0.0;
		int32 Frame = 0, LastPress = -1000, PressFrame = -1;
		FKey HeldKey;
		TArray<FString> Screens;
		TSet<FString> Pressed;
		int32 MoviesSkipped = 0;
		bool bManualTick = false;
		int32 FeRiders = 0, PosedFeRiders = 0, MaxPosedBones = 0;
		bool bLaunched = false, bReachedRace = false, bTimedOut = false;
		FString Unexpected;
		FString PrevMemCard;
		TUniquePtr<FErrorCapture> Errors;
		bool bPrevFixed = false;
		double PrevFixedDt = 0.0;
	};

	void PadKey(USSXFeWidget* Menu, const FKey& Key, bool bDown)
	{
		const FKeyEvent E(Key, FModifierKeysState(), 0, false, 0, 0);
		const FGeometry G = FGeometry::MakeRoot(FVector2D(640.0, 480.0), FSlateLayoutTransform());
		const TSharedRef<SWidget> W = Menu->TakeWidget();
		if (bDown) { W->OnKeyDown(G, E); }
		else { W->OnKeyUp(G, E); }
	}

	class FFrontEndCommand : public IAutomationLatentCommand
	{
	public:
		FFrontEndCommand(FAutomationTestBase* InTest, TSharedRef<FFeRun> InRun) : Test(InTest), Run(InRun) {}
		virtual bool Update() override;
	private:
		void Report();
		FAutomationTestBase* Test;
		TSharedRef<FFeRun> Run;
	};

	bool FFrontEndCommand::Update()
	{
		FFeRun& R = *Run;
		if (FPlatformTime::Seconds() - R.WallStart > 600.0)
		{
			R.bTimedOut = true;
			Report();
			return true;
		}
		UWorld* World = GameWorld();
		if (!World || !World->HasBegunPlay())
		{
			return false;
		}
		if (MapOf(World) == TEXT("Gari"))
		{
			const ASSXRaceManager* Race = ASSXRaceManager::Find(World);
			if (Race && Race->GetSession() && Race->GetPlayerPawn())
			{
				R.bReachedRace = true;
				Report();
				return true;
			}
			return false;
		}
		const ASSXFrontEndPlayerController* PC = Cast<ASSXFrontEndPlayerController>(World->GetFirstPlayerController());
		USSXFeWidget* Menu = PC ? PC->GetMenu() : nullptr;
		if (!Menu)
		{
			return false;
		}
		++R.Frame;
		if (R.HeldKey.IsValid() && R.Frame - R.PressFrame >= 2)
		{
			PadKey(Menu, R.HeldKey, false);
			R.HeldKey = FKey();
		}
		const FSSXFrontEnd& FE = Menu->GetFrontEnd();
		if (R.Frame % 300 == 0)
		{
			UE_LOG(LogSSX, Display, TEXT("[smoke-fe] frame %d: boot %d (frame %d) movie '%s' screen %s"), R.Frame, int32(FE.Boot),
				FE.BootFrame, *FE.MovieRequest, LexToString(FE.Current()));
		}
		if (R.Frame == 120 && FE.Boot == ESSXFeBoot::StartScreen && FE.BootFrame == 0)
		{
			R.bManualTick = true;
		}
		if (R.bManualTick)
		{
			Menu->TakeWidget()->Tick(FGeometry::MakeRoot(FVector2D(640.0, 480.0), FSlateLayoutTransform()), FPlatformTime::Seconds(), 1.f / 60.f);
		}
		if (FE.Boot != ESSXFeBoot::FrontEnd)
		{
			if (!FE.MovieRequest.IsEmpty() && FE.bMovieSkippable && R.Frame - R.LastPress > 30)
			{
				PadKey(Menu, EKeys::Gamepad_Special_Right, true);
				R.HeldKey = EKeys::Gamepad_Special_Right;
				R.PressFrame = R.LastPress = R.Frame;
				++R.MoviesSkipped;
			}
			return false;
		}
		const FString Screen = LexToString(FE.Current());
		if (R.Screens.Num() == 0 || R.Screens.Last() != Screen)
		{
			R.Screens.Add(Screen);
			R.LastPress = R.Frame;
		}
		if (Screen == TEXT("CharacterSelect") && R.Frame - R.LastPress == 60)
		{
			for (TActorIterator<ASSXFeRiderActor> It(World); It; ++It)
			{
				++R.FeRiders;
				TArray<UPoseableMeshComponent*> Meshes;
				It->GetComponents(Meshes);
				int32 Best = 0;
				for (const UPoseableMeshComponent* M : Meshes) { Best = FMath::Max(Best, PosedBones(M)); }
				R.MaxPosedBones = FMath::Max(R.MaxPosedBones, Best);
				R.PosedFeRiders += Best > 5 ? 1 : 0;
			}
		}
		static const TSet<FString> Path = { TEXT("Title"), TEXT("ModeSelect"), TEXT("CharacterSelect"), TEXT("CharacterAccept"),
			TEXT("CharacterModify"), TEXT("EventSel"), TEXT("DifficultSel"), TEXT("VenueSel") };
		if (FE.bLaunchPending)
		{
			R.bLaunched = true;
			return false;
		}
		if (!Path.Contains(Screen))
		{
			if (R.Unexpected.IsEmpty()) { R.Unexpected = Screen; }
			return false;
		}
		if (R.Frame - R.LastPress >= 90 && !R.HeldKey.IsValid())
		{
			PadKey(Menu, EKeys::Gamepad_FaceButton_Bottom, true);
			R.HeldKey = EKeys::Gamepad_FaceButton_Bottom;
			R.PressFrame = R.LastPress = R.Frame;
			R.Pressed.Add(Screen);
		}
		return false;
	}

	void FFrontEndCommand::Report()
	{
		FFeRun& R = *Run;
		FAutomationTestBase& T = *Test;
		FApp::SetUseFixedTimeStep(R.bPrevFixed);
		FApp::SetFixedDeltaTime(R.PrevFixedDt);
		FSSXMemCard::RootOverride() = R.PrevMemCard;
		const TArray<FString> Errors = R.Errors ? R.Errors->Get() : TArray<FString>();
		R.Errors.Reset();
		T.AddInfo(FString::Printf(TEXT("[fe] widget ticked by the test (headless Slate): %d"), R.bManualTick));
		T.AddInfo(FString::Printf(TEXT("[fe] screens: %s; intro skips %d; FE riders %d (posed %d, max posed bones %d); launch %d, race %d%s"),
			*FString::Join(R.Screens, TEXT(" > ")), R.MoviesSkipped, R.FeRiders, R.PosedFeRiders, R.MaxPosedBones, R.bLaunched, R.bReachedRace,
			R.Unexpected.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", stopped on unexpected screen %s"), *R.Unexpected)));
		T.TestFalse(TEXT("front end finished in time"), R.bTimedOut);
		for (const TCHAR* S : { TEXT("Title"), TEXT("ModeSelect"), TEXT("CharacterSelect"), TEXT("CharacterAccept"), TEXT("CharacterModify"),
			TEXT("EventSel"), TEXT("DifficultSel"), TEXT("VenueSel") })
		{
			T.TestTrue(FString::Printf(TEXT("screen %s reached"), S), R.Screens.Contains(S));
		}
		T.TestTrue(TEXT("FE carousel riders spawned"), R.FeRiders > 0);
		T.TestTrue(TEXT("FE riders animated (not the bind pose)"), R.FeRiders > 0 && R.PosedFeRiders == R.FeRiders);
		T.TestTrue(TEXT("venue accept launches the race"), R.bLaunched);
		T.TestTrue(TEXT("the race map loads with the race running"), R.bReachedRace);
		for (const FString& E : Errors)
		{
			T.AddError(FString::Printf(TEXT("[fe] error log: %s"), *E));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSmokeFrontEndTest, "SSX.Smoke.FrontEnd", SSXSmoke::Flags)

bool FSSXSmokeFrontEndTest::RunTest(const FString& Parameters)
{
	using namespace SSXSmoke;
	FString Missing;
	if (!Prerequisites(TEXT("gari"), TEXT("Gari"), Missing))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: missing%s"), *Missing));
		return true;
	}
	TSharedRef<FFeRun> Run = MakeShared<FFeRun>();
	Run->WallStart = FPlatformTime::Seconds();
	Run->Errors = MakeUnique<FErrorCapture>();
	Run->PrevMemCard = FSSXMemCard::RootOverride();
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardSmoke"));
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	Run->bPrevFixed = FApp::UseFixedTimeStep();
	Run->PrevFixedDt = FApp::GetFixedDeltaTime();
	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / 60.0);
	AutomationOpenMap(TEXT("/Game/Maps/FrontEnd"), true);
	ADD_LATENT_AUTOMATION_COMMAND(FFrontEndCommand(this, Run));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPieIfRunning(0));
	return true;
}

#endif
