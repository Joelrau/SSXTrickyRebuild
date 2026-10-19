#include "Effects/SSXTrackEffects.h"

#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXCrowdVoices.h"
#include "Audio/SSXGameAudio.h"
#include "Effects/BoostVolumes.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "Race/GameRng.h"
#include "Race/RaceTrackData.h"
#include "SSXTrickyRebuild.h"
#include "SSXShowOffDraw.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/SimCoords.h"
#include "World/SSXPropAnimation.h"
#include "World/SSXWorldLighting.h"
#include "Particles/SSXParticleWorld.h"
#include "Camera/RaceCamera.h"
#include "SnowboarderPawn.h"

using namespace SSXEffects;

USSXTrackEffects* USSXTrackEffects::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	return W ? W->GetSubsystem<USSXTrackEffects>() : nullptr;
}

namespace
{
	/** BANKS.INF: the section whose name appears in the track file name gives BANK; the rebuild's stems are shortened (merquer, megaple, untrack), so a section name starting with the stem also counts. */
	FString TrackBankFor(const FString& Stem)
	{
		FSSXInfFile Inf;
		if (!Inf.Load(SSXAudio::ConfigPath(TEXT("BANKS.INF"))))
		{
			return FString();
		}
		const FString Up = Stem.ToUpper();
		for (const FSSXInfFile::FSection& S : Inf.Sections)
		{
			const FString Name = S.Name.ToUpper();
			if (Name.IsEmpty() || Name == TEXT("GLOBAL") || Name == TEXT("FE") || !(Up.Contains(Name) || Name.StartsWith(Up)))
			{
				continue;
			}
			FString Bank = S.Get(TEXT("BANK")).TrimQuotes();
			return FPaths::GetBaseFilename(Bank).ToLower();
		}
		return FString();
	}
}

bool USSXTrackEffects::BeginTrack(const FString& Track, int32 TrackId, int32 GameMode, const TSharedPtr<FPatchWorld>& InSimWorld, bool bFrontEndLoad)
{
	for (int32 I = 0; I < Data.NumInstances(); ++I)
	{
		if (World.IsScriptHidden(I)) { DirtyDraw.Add(I); }
	}
	bActive = false;
	bCountdownEnded = false;
	bStageResetPending = false;
	StageResets = 0;
	GameModeId = GameMode;
	Riders.Reset();
	World.Racers.Reset();
	LapsLeftOf = nullptr;
	if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this)) { Pw->ClearTrackEmitters(); }
	SimWorld = InSimWorld;
	TrackStem = Track.ToLower();
	bShowOffMode = IsShowOffMode(GameMode);
	USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this);
	if (PropAnim)
	{
		PropAnim->GetControllers().Reset();
		PropAnim->GetControllers().EffectTouch = nullptr;
		PropAnim->GetControllers().RunEffect = nullptr;
	}
	FString Err;
	if (!Data.Load(FEffectData::DefaultPath(TrackStem), &Err))
	{
		UE_LOG(LogSSX, Warning, TEXT("[effects] track %s: %s; track effects off"), *TrackStem, *Err);
		World.Init(nullptr, GameMode, TrackId);
		return false;
	}
	const int32 Track_ = TrackId >= 0 ? TrackId : (Data.TrackId >= 0 ? Data.TrackId : TrackIdForStem(TrackStem));
	World.Init(&Data, GameMode, Track_);
	bHaveContact = false;
	bInTouch = false;
	bPreRace = false;
	if (PropAnim)
	{
		PropAnim->BeginTrack(TrackStem, &Data, &World, InSimWorld);
	}
	World.Rng = []() { return double(FGameRng::StreamB().Uniform(0.f, 1.f)); };
	SSXEffects::BindBoostQueries(World, InSimWorld);
	World.LapsLeftOf = [this](const FEffectRider& R) { return LapsLeftOf ? LapsLeftOf(R.Slot) : R.LapsLeft; };
	World.OnOutcome = [this](const FOutcome& O) { OnOutcome(O); };
	World.OnFlagsChanged = [this](int32 Inst, uint16 Old, uint16 New)
	{
		if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin()) { W->SetInstanceRunFlags(Inst, New); }
		if (((Old ^ New) & FlDisplay) != 0 && SplinePathInstances.Contains(Inst)) { DirtyDraw.Add(Inst); }
	};
	ClearSplinePaths();
	{
		TArray<FSplinePathSegment> Segs;
		TArray<FIntPoint> Spl;
		if (const TSharedPtr<FPatchWorld> W = InSimWorld)
		{
			Segs.SetNum(W->NumRailSegments());
			for (int32 K = 0; K < Segs.Num(); ++K)
			{
				const FSimRailSegment& G = W->GetRailSegment(K);
				FSplinePathSegment& O = Segs[K];
				for (int32 R = 0; R < 4; ++R) { O.Coef[R] = FVector3f(G.Coef[R]); O.Poly[R] = G.DistPoly[R]; }
				O.Prev = G.Prev;
				O.Next = G.Next;
				O.Length = float(G.Length);
				O.Start = float(G.StartDist);
			}
			Spl.SetNum(W->NumRailSplines());
			for (int32 K = 0; K < Spl.Num(); ++K) { Spl[K] = FIntPoint(W->GetRailSpline(K).FirstSegment, W->GetRailSpline(K).SegmentCount); }
		}
		World.SetSplines(MoveTemp(Segs), MoveTemp(Spl));
	}
	Cells.Init(&Data, bShowOffMode);
	if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin())
	{
		W->ResetRailsToInitial();
		W->ResetInstanceRunFlags();
	}
	if (PropAnim)
	{
		PropAnim->GetControllers().EffectTouch = [this](FRiderSim& S, const SSXGround::FPropContactEvent& E) { Touch(S, E); };
		PropAnim->GetControllers().RunEffect = [this](int32 Inst, int32 Slot, const SSXGround::FPropContactEvent*)
		{
			World.RunEffect(Data.EffectFor(Inst, Slot), Inst, nullptr);
		};
	}
	SoundBank = TrackBankFor(TrackStem);
	// 0x25EDF0 / 0x22A518
	StopAmbientVoices();
	if (bFrontEndLoad)
	{
		Ambient.Reset();
		FSSXPropSounds::UnbindBoardSounds(&PropSounds);
		PropSounds.Reset();
	}
	else if (PropSounds.Load(FSSXPropSounds::DefaultPath(TrackStem), &Err))
	{
		PropSounds.BindToBoardSounds(SoundBank);
		Ambient.Build(&PropSounds);   // 0x22A518 / 0x22A6D8
		if (const USSXAudioSubsystem* Snd = USSXAudioSubsystem::Get(this)) { Ambient.RestoreEnabled(Snd->bAmbientEnableCarry); }
		BindAmbientVoices();
	}
	else
	{
		Ambient.Reset();
		FSSXPropSounds::UnbindBoardSounds(&PropSounds);
		UE_LOG(LogSSX, Warning, TEXT("[effects] track %s: %s; prop hits silent"), *TrackStem, *Err);
	}
	bActive = true;
	UE_LOG(LogSSX, Display, TEXT("[effects] track %s (id %d), mode %d: %d effects, %d functions, %d instances, bank '%s'"),
		*TrackStem, Track_, GameMode, Data.Effects.Num(), Data.Functions.Num(), Data.NumInstances(), *SoundBank);
	return true;
}

void USSXTrackEffects::AddRider(FRiderSim* Sim, bool bHuman, int32 Slot)
{
	if (!Sim)
	{
		return;
	}
	FEffectRider* R = RiderFor(Sim);
	R->bHuman = bHuman;
	R->Slot = Slot;
}

FEffectRider* USSXTrackEffects::RiderFor(FRiderSim* Sim)
{
	for (const TUniquePtr<FEffectRider>& R : Riders)
	{
		if (R->Sim == Sim) { return R.Get(); }
	}
	TUniquePtr<FEffectRider> R = MakeUnique<FEffectRider>();
	R->Sim = Sim;
	R->Slot = Riders.Num();
	R->bHuman = true;
	Riders.Add(MoveTemp(R));
	World.Racers.Add(Riders.Last().Get());
	return Riders.Last().Get();
}

void USSXTrackEffects::RunFlow(EFlowPhase Phase)
{
	if (bActive)
	{
		World.RunFlow(Phase);
	}
}

void USSXTrackEffects::OnRaceFrame(ERacePhase Prev, ERacePhase Now, double Clock)
{
	if (!bActive)
	{
		return;
	}
	bPreRace = Now == ERacePhase::PreRace;
	if (Prev != Now)
	{
		// 0x1126F0
		if (Prev == ERacePhase::StartRace)
		{
			World.RunFlow(EFlowPhase::StartRace);   // 0x1134D0
		}
		switch (Now)
		{
		case ERacePhase::PreRace: World.RunFlow(EFlowPhase::PreRace); bCountdownEnded = false; break;
		case ERacePhase::StartRace:
			// 0x113478 / 0x191298 / 0x17E148
			bStageResetPending = GameModeId != 6;
			break;
		case ERacePhase::Countdown: World.RunFlow(EFlowPhase::CountdownEnter); bCountdownEnded = false; break;
		default: break;
		}
	}
	// 0x113600
	if (Now == ERacePhase::Countdown && Clock >= 0.0 && !bCountdownEnded)
	{
		bCountdownEnded = true;
		World.RunFlow(EFlowPhase::CountdownEnd);
	}
}

void USSXTrackEffects::BeginFrame()
{
	if (bActive && bStageResetPending)
	{
		bStageResetPending = false;
		ResetStage();
	}
}

void USSXTrackEffects::StepCells(ERacePhase RequestedPhase, const FVector* Camera)
{
	if (!bActive || Riders.Num() == 0)
	{
		return;
	}
	// 0x115E28
	TArray<FEffectRider*, TInlineAllocator<8>> List;
	for (const TUniquePtr<FEffectRider>& R : Riders) { List.Add(R.Get()); }
	Cells.StepRacers(World, List, uint8(RequestedPhase) < uint8(ERacePhase::StartRace), Camera);
}

void USSXTrackEffects::OnRacersPlaced()
{
	for (const TUniquePtr<FEffectRider>& R : Riders)
	{
		R->Cell = -1;
		R->PrevCell = -1;
	}
}

void USSXTrackEffects::PostRiders()
{
	for (const TUniquePtr<FEffectRider>& R : Riders)
	{
		R->PrevCell = R->Cell;
	}
}

void USSXTrackEffects::StepFrame()
{
	if (bActive)
	{
		World.Frame();
		if (USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this))
		{
			// 0x2213F8
			USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
			const FSSXCrowdSystem* Crowd = Audio && Ambient.Crowd ? &Audio->GetCrowdVoices().Crowd : nullptr;
			TArray<TPair<FVector, int32>> Humans;
			for (const TUniquePtr<FEffectRider>& R : Riders)
			{
				if (R->bHuman && R->Sim) { Humans.Add({ R->Sim->State.Position, Crowd ? Crowd->Level(R->Slot) : 0 }); }
			}
			PropAnim->SetCrowdInput(bPreRace, MoveTemp(Humans));
		}
	}
}

void USSXTrackEffects::TouchInstance(FRiderSim& Sim, int32 Inst)
{
	if (bActive)
	{
		World.Touch(Inst, *RiderFor(&Sim));
	}
}

void USSXTrackEffects::Touch(FRiderSim& Sim, const SSXGround::FPropContactEvent& E)
{
	if (!bActive)
	{
		return;
	}
	const int32 Inst = E.Hit.InstanceIndex;
	const FController* C = World.FindController(Inst);
	const bool bFence = C && C->Type == TypeFence;
	LastContact = FContactRec{ E.Contact.Point, E.Contact.Direction, E.Contact.Normal, E.Contact.Speed };
	bHaveContact = true;
	bInTouch = true;
	World.Touch(Inst, *RiderFor(&Sim), true, FVector::DotProduct(E.Contact.Direction, E.Contact.Normal), E.Contact.Speed);
	bInTouch = false;
	if (bFence)
	{
		if (USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this))
		{
			PropAnim->GetControllers().OnFenceContact(E);
			SSXPropNodes::FContactRecord Rec;
			Rec.C = LastContact.C; Rec.VHat = LastContact.VHat; Rec.N = LastContact.N; Rec.A = LastContact.A;
			PropAnim->OnFenceTouch(Inst, Rec);   // 0x144428
		}
	}
}

void USSXTrackEffects::OnOutcome(const FOutcome& O)
{
	USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this);
	switch (O.Kind)
	{
	case EOutcome::Rail:
		if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin()) { W->SetRailEnabled(O.A, (O.B & 1) != 0); }
		break;
	case EOutcome::Hide:
		DirtyDraw.Add(O.Instance);
		break;
	case EOutcome::NodeDestroyed:
		if (O.A == TypeDead || O.A == TypeRestore) { DirtyDraw.Add(O.Instance); }
		if (PropAnim)
		{
			PropAnim->GetControllers().Remove(O.Instance);
			PropAnim->OnNodeDestroyed(O.Instance, O.A);
		}
		break;
	case EOutcome::NodeRestart:
		if (PropAnim) { PropAnim->OnNodeRestart(O.Instance); }
		break;
	case EOutcome::RollerStop:
		if (PropAnim) { PropAnim->OnRollerStop(O.Instance); }
		break;
	case EOutcome::Node:
		if (PropAnim && Data.Records.IsValidIndex(O.Record))
		{
			SSXPropNodes::FContactRecord Contact;
			if (bHaveContact)
			{
				Contact.C = LastContact.C; Contact.VHat = LastContact.VHat; Contact.N = LastContact.N; Contact.A = LastContact.A;
			}
			const bool bNodeContact = bInTouch || (bHaveContact && O.A == TypeMeshAnim);
			PropAnim->OnNodeCreated(O.Instance, O.A, O.Record, bNodeContact ? &Contact : nullptr, bInTouch);
		}
		if (PropAnim && Data.Records.IsValidIndex(O.Record))
		{
			const FRecord& R = Data.Records[O.Record];
			SSXProps::FPropController N;
			N.Instance = O.Instance;
			bool bOk = false;
			if (O.A == TypeUVScroll)
			{
				N.Type = SSXProps::EControllerType::UVScroll;
				bOk = SSXProps::ParseUVScrollRecord(Data.RecordBytes(R), R.Size, N.Scroll);
			}
			else if (O.A == TypeTexFlip)
			{
				N.Type = SSXProps::EControllerType::TexFlip;
				bOk = SSXProps::ParseTexFlipRecord(Data.RecordBytes(R), R.Size, N.Flip);
				if (bOk)
				{
					// 0x142E90 / 0x142AC8 / 0x142AF0
					const TArray<int32>* Fb = PropAnim->GetData().FirstFlipbookOfInstance(O.Instance);
					N.Flip.Create(Fb ? *Fb : TArray<int32>());
				}
			}
			else if (O.A == TypeFence)
			{
				N.Type = SSXProps::EControllerType::Fence;
				bOk = true;
			}
			if (bOk) { PropAnim->GetControllers().Add(N); }
		}
		break;
	case EOutcome::Message:
		if (PropAnim && O.A == TypeTexFlip) { PropAnim->GetControllers().Message(O.Instance, O.B, O.V); }
		if (PropAnim) { PropAnim->OnNodeMessage(O.Instance, O.A, O.B, O.V); }
		break;
	case EOutcome::Sound:
		PlayTrackSound(O.Instance, O.B, O.Extra != 0, O.Rider && O.Rider->Sim ? O.Rider->Sim->State.Frame : INDEX_NONE);
		break;
	case EOutcome::BoostSound:
	case EOutcome::LaunchSound:
		// FUN_002340B8 / FUN_00234308
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
		{
			SSXGameAudio::OnTrackBoostOp(*Audio, O.Kind == EOutcome::BoostSound ? 17 : 18);
		}
		break;
	case EOutcome::TimeSound:
		// 0x11E8A0 / 0x219FC0
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
		{
			Audio->PlayZbxsfx(SSXBoardAudio::TimeBonusSound(O.V));
		}
		break;
	case EOutcome::MultiplierSound:
		// 0x11E660 / 0x219D70
		if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
		{
			SSXGameAudio::OnTrackMultiplierOp(*Audio, O.V);
		}
		break;
	case EOutcome::Emitter:
		if (O.A == 1)
		{
			// 0x1FA108
			World.ApplyNodeFlags(O.Instance, FlDisplay | FlNodeDrawn, 0);
			SplinePathInstances.Add(O.Instance);
			DirtyDraw.Add(O.Instance);
			break;
		}
		UE_LOG(LogSSX, Verbose, TEXT("[effects] emitter kind %d at instance %d"), O.A, O.Instance);
		if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this))
		{
			Pw->AddTrackEmitter(Data, O.Instance, O.A, O.Record, O.Rider ? O.Rider->Sim : nullptr);
		}
		break;
	case EOutcome::EmitterDied:
		if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this)) { Pw->RemoveTrackEmitter(O.Instance, O.A, O.Record); }
		break;
	case EOutcome::Teleport:
		// 0x11A098 / 0x177DB0
		for (TActorIterator<ASnowboarderPawn> It(GetWorld()); It; ++It)
		{
			if (O.Rider && It->HasSim() && It->GetSim() == O.Rider->Sim && It->IsPlayerControlled())
			{
				if (FSSXRaceCamera* Cam = It->GetRaceCamera()) { Cam->MarkCut(); }
			}
		}
		break;
	case EOutcome::LaunchBoost:
		if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this); Pw && O.Rider) { Pw->OnLaunchBoost(O.Rider->Sim); }
		break;
	default:
		break;
	}
}

void USSXTrackEffects::PlayTrackSound(int32 Inst, int32 Id, bool b2D, int32 RaceFrame)
{
	// FUN_00226DD8 / 0x2165C8 / 0x2154E0 / 0x215998
	USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
	if (!Audio || SoundBank.IsEmpty() || !Data.InstMatrix.IsValidIndex(Inst) || !GetWorld())
	{
		return;
	}
	if (RaceFrame != INDEX_NONE && USSXBoardSoundComponent::IsInstanceSoundHeld(Inst, RaceFrame))
	{
		return;
	}
	const FMatrix& M = Data.InstMatrix[Inst];
	const FVector At = SSXSim::GameToUnreal(FVector(M.M[3][0], M.M[3][1], M.M[3][2]));
	// 0x22E338
	TrackSounds.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& C) { return !C.IsValid() || !C->IsPlaying(); });
	if (UAudioComponent* C = Audio->PlayGameSound(TEXT("tracksound"), FString::Printf(TEXT("sfx/%s/%02d"), *SoundBank, Id),
		Audio->GetSfxGain(), b2D ? nullptr : &At, SSXAudioFalloff::TrackSoundRadiusM))
	{
		TrackSounds.Add(C);
		if (RaceFrame != INDEX_NONE)
		{
			USSXBoardSoundComponent::HoldInstanceSound(Inst, RaceFrame);
		}
	}
}

int32 USSXTrackEffects::NumPlayingTrackSounds() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<UAudioComponent>& C : TrackSounds)
	{
		N += C.IsValid() && C->IsPlaying() ? 1 : 0;
	}
	return N;
}

void USSXTrackEffects::BindAmbientVoices()
{
	// 0x22BF60
	Ambient.Start = [this](const FSSXAmbientStart& P) -> int32
	{
		USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
		UWorld* W = GetWorld();
		FString Id;
		if (P.Bank == 6 && P.bChant) { Id = FString::Printf(TEXT("speech/%s/00"), *P.SwapBank); }
		else if (P.Bank == 6) { Id = P.SwapBank.IsEmpty() ? FString() : FString::Printf(TEXT("ambience/%s/00"), *P.SwapBank); }
		else if (P.Bank == 3) { Id = FString::Printf(TEXT("sfx/Crowd/%02d"), P.Index); }
		else if (P.Index >= 0 && !SoundBank.IsEmpty()) { Id = FString::Printf(TEXT("sfx/%s/%02d"), *SoundBank, P.Index); }
		USoundWave* Wave = Audio && W && !Id.IsEmpty() ? Cast<USoundWave>(Audio->LoadSound(Id)) : nullptr;
		if (!Wave)
		{
			SSXAudioDiag::Note(TEXT("ambient"), Id, P.b3D, P.Volume127 / 127.f, 30.0,
				!Audio || Id.IsEmpty() ? SSXAudioDiag::EResult::Gated : (!W ? SSXAudioDiag::EResult::NoWorld : Audio->MissReason(Id)));
			return INDEX_NONE;
		}
		const FSSXAudioEntry* E = Audio->GetManifest().Find(Id);
		Wave->bLooping = E && E->LoopStart >= 0;
		if (!AmbientSpatial)
		{
			AmbientSpatial = NewObject<USoundAttenuation>(this, NAME_None, RF_Transient);
			AmbientSpatial->Attenuation.bAttenuate = false;
			AmbientSpatial->Attenuation.bSpatialize = Audio->GetSpeaker() != 0;
		}
		const float Gain0 = FMath::Clamp(P.Volume127 / 127.f * Audio->GetSfxGain(), 0.f, 1.f);
		UAudioComponent* C = P.b3D
			? UGameplayStatics::SpawnSoundAtLocation(W, Wave, SSXSim::GameToUnreal(FVector(P.Position)), FRotator::ZeroRotator, Gain0, 1.f, 0.f,
				AmbientSpatial, nullptr, false)
			: UGameplayStatics::CreateSound2D(W, Wave, Gain0, 1.f, 0.f, nullptr, false, false);
		if (C && !P.b3D)
		{
			USSXAudioSubsystem::MakeGameSound(C);   // 0x20BFE8
			C->Play();
		}
		SSXAudioDiag::Note(TEXT("ambient"), Id, P.b3D, Gain0, 30.0, SSXAudioDiag::SpawnResult(C, false), Wave->GetPathName());
		SSXAudioDiag::NoteLoop(TEXT("ambient"), Id, C, P.b3D);
		if (!C)
		{
			return INDEX_NONE;
		}
		const int32 H = NextAmbientHandle++;
		AmbientVoices.Add(H, { C, P.b3D, P.Position, P.Volume127 });
		return H;
	};
	Ambient.IsPlaying = [this](int32 H)
	{
		const FAmbientVoice* V = AmbientVoices.Find(H);
		return V && V->Comp.IsValid() && V->Comp->IsPlaying();
	};
	Ambient.SetVolume = [this](int32 H, int32 Vol)
	{
		if (FAmbientVoice* V = AmbientVoices.Find(H)) { V->Volume127 = Vol; }
	};
	Ambient.Stop = [this](int32 H, float Fade)
	{
		FAmbientVoice V;
		if (AmbientVoices.RemoveAndCopyValue(H, V) && V.Comp.IsValid())
		{
			V.Comp->FadeOut(Fade, 0.f);
			V.Comp->bAutoDestroy = true;
		}
	};
	if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
	{
		Audio->GetZoneAmbience().Start = Ambient.Start;
		Audio->GetZoneAmbience().Stop = Ambient.Stop;
	}
	Ambient.SwapReady = nullptr;   // TODO: the swap bank load time (0x21A640 / 0x21A548);
	Ambient.Crowd = nullptr;
	Ambient.RiderAmbienceBank = nullptr;
	if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
	{
		FSSXCrowdVoices& CV = Audio->GetCrowdVoices();
		CV.Begin(Audio, GetWorld());
		Ambient.Crowd = &CV.Crowd;
		Ambient.RiderAmbienceBank = [Audio](int32 Listener)
		{
			return Audio->GetCrowdVoices().PickChantBank(Listener, []() { return FGameRng::StreamA().Next(); });   // 0x343050
		};
	}
}

void USSXTrackEffects::StopAmbientVoices()
{
	for (TPair<int32, FAmbientVoice>& V : AmbientVoices)
	{
		if (V.Value.Comp.IsValid())
		{
			V.Value.Comp->Stop();
			V.Value.Comp->bAutoDestroy = true;
		}
	}
	AmbientVoices.Reset();
}

void USSXTrackEffects::StopAudio()
{
	Ambient.RaceRestart();   // 0x22E338 / 0x22D408 / 0x22E360
	PropSounds.ResetHitList();
	StopAmbientVoices();
}

void USSXTrackEffects::StepAmbient(const FVector* Listener, const FVector& Ear)
{
	if (!Ambient.IsBuilt())
	{
		return;
	}
	Ambient.AdvanceMs(1000.0 / 60.0);
	if (Listener)
	{
		Ambient.Query(FVector3f(*Listener), 0);   // 0x1166F0
	}
	Ambient.Update();
	// 0x20B700
	USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this);
	if (Audio && Ambient.Crowd)
	{
		Audio->GetCrowdVoices().Step(Ear, 1000.0 / 60.0);   // 0x220968
	}
	const float Level = Audio ? Audio->GetSfxGain() : 1.f;
	for (TPair<int32, FAmbientVoice>& It : AmbientVoices)
	{
		FAmbientVoice& V = It.Value;
		if (!V.Comp.IsValid())
		{
			continue;
		}
		const int32 Vol = V.b3D ? SSXBoardAudio::Falloff(FMath::Clamp(V.Volume127, 0, 127), FVector::Dist(FVector(V.Position), Ear) * 0.01,
			SSXAmbient::FalloffRadiusM) : FMath::Clamp(V.Volume127, 0, 127);
		V.Comp->SetVolumeMultiplier(FMath::Clamp(Vol / 127.f * Level, 0.f, 1.f));
	}
}

int32 USSXTrackEffects::NumPlayingAmbientVoices() const
{
	int32 N = 0;
	for (const TPair<int32, FAmbientVoice>& V : AmbientVoices)
	{
		N += V.Value.Comp.IsValid() && V.Value.Comp->IsPlaying() ? 1 : 0;
	}
	return N;
}

void USSXTrackEffects::Deinitialize()
{
	if (USSXAudioSubsystem* Audio = USSXAudioSubsystem::Get(this))
	{
		Audio->GetZoneAmbience().Start = nullptr;
		Audio->GetZoneAmbience().Stop = nullptr;
	}
	FSSXPropSounds::UnbindBoardSounds(&PropSounds);
	StopAmbientVoices();
	if (Ambient.IsBuilt())
	{
		if (USSXAudioSubsystem* Snd = USSXAudioSubsystem::Get(this)) { Snd->bAmbientEnableCarry = Ambient.CarryEnabled(); }
	}
	Ambient.Reset();
	PropSounds.Reset();
	Super::Deinitialize();
}

void USSXTrackEffects::RestartTrack()
{
	if (!bActive)
	{
		return;
	}
	// 0x180A10 / 0x115DC0 / 0x115DF0 / 0x18EBD0
	Cells.Init(&Data, bShowOffMode);
	bStageResetPending = false;
	for (const TUniquePtr<FEffectRider>& R : Riders)
	{
		// 0x173A98 / 0x175EB8
		R->CollideEmitters = 0;
		R->PointsMult = 1.0;
		R->bResetRequested = false;
	}
	for (const TWeakObjectPtr<UAudioComponent>& C : TrackSounds)
	{
		if (C.IsValid()) { C->FadeOut(0.25f, 0.f); }
	}
	TrackSounds.Reset();
	Ambient.RaceRestart();   // 0x22E338 / 0x22E360
	bCountdownEnded = false;
}

void USSXTrackEffects::ResetStage()
{
	// 0x191298 / 0x17DA50 / 0x115DF0 / 0x115FE8
	++StageResets;
	World.RestartReset();
	Cells.Init(&Data, bShowOffMode);
	for (const TUniquePtr<FEffectRider>& R : Riders)
	{
		R->PrevCell = -1;
	}
	if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin())
	{
		W->ResetRailsToInitial();
		W->ResetInstanceRunFlags();
		for (int32 I = 0; I < Data.NumInstances(); ++I)
		{
			W->SetInstanceRunFlags(I, World.GetFlags(I));
		}
	}
	if (USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this))
	{
		PropAnim->RestartTrack();
	}
	if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this))
	{
		Pw->ClearTrackEmitters();
	}
	for (int32 I = 0; I < Data.NumInstances(); ++I)
	{
		DirtyDraw.Add(I);
	}
	bHaveContact = false;
	bInTouch = false;
}

void USSXTrackEffects::SaveReplay(FReplayKeyframe& Out) const
{
	Out.bValid = bActive;
	if (!bActive)
	{
		return;
	}
	World.SaveKeyframe(Out.World);
	Out.Cells = Cells;
	Out.Riders.Reset();
	for (const TUniquePtr<FEffectRider>& R : Riders) { Out.Riders.Add(*R); }
	Out.bCountdownEnded = bCountdownEnded;
	Out.bPreRace = bPreRace;
}

void USSXTrackEffects::LoadReplay(const FReplayKeyframe& In)
{
	if (!bActive || !In.bValid)
	{
		return;
	}
	World.RestartReset();
	World.LoadKeyframe(In.World);
	Cells = In.Cells;
	for (int32 I = 0; I < Riders.Num() && I < In.Riders.Num(); ++I)
	{
		FRiderSim* const Sim = Riders[I]->Sim;
		*Riders[I] = In.Riders[I];
		Riders[I]->Sim = Sim;
	}
	bCountdownEnded = In.bCountdownEnded;
	bPreRace = In.bPreRace;
	bStageResetPending = false;
	if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin())
	{
		W->ResetRailsToInitial();
		for (int32 I = 0; I < World.NumRails(); ++I) { W->SetRailEnabled(I, World.IsRailEnabled(I)); }
		for (int32 I = 0; I < Data.NumInstances(); ++I) { W->SetInstanceRunFlags(I, World.GetFlags(I)); }
	}
	if (USSXPropAnimation* PropAnim = USSXPropAnimation::Get(this))
	{
		PropAnim->RestartTrack();
	}
	if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this))
	{
		Pw->ClearTrackEmitters();
	}
	for (int32 I = 0; I < Data.NumInstances(); ++I)
	{
		DirtyDraw.Add(I);
	}
	bHaveContact = false;
	bInTouch = false;
}

void USSXTrackEffects::RollSnowfall(int32 Humans, int32 Racers)
{
	// 0x17E148 / FUN_0023D4A8 / 0x1C8DB8 / 0x1C8EC8
	bSnowfall = false;
	double Draw = -1.0, Chance = 0.0;
	if (SnowfallNeedsDraw(World.GetGameMode(), Humans, Racers))
	{
		TArray<FSSXTrackPresentation> Table;
		if (FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table))
		{
			for (const FSSXTrackPresentation& T : Table)
			{
				if (T.Id == FRaceTrackData::GameTrackOf(World.GetTrack())) { Chance = T.SnowChance; }
			}
		}
		Draw = FGameRng::StreamA().Uniform(0.f, 1.f);
		bSnowfall = SnowfallEnabled(World.GetGameMode(), Humans, Racers, Draw, Chance);
	}
	TArray<double> Seeds;
	for (int32 I = 0; I < SSXParticles::FSnowfall::Slots * 4; ++I) { Seeds.Add(FGameRng::StreamA().Uniform(0.f, 1.f)); }
	const double X = FGameRng::StreamA().Uniform(0.f, 1.f);
	const double R1 = FGameRng::StreamA().Uniform(0.f, 1.f);
	const double R2 = FGameRng::StreamA().Uniform(0.f, 1.f);
	UE_LOG(LogSSX, Display, TEXT("[effects] snowfall %s (chance %.2f, draw %.3f)"), bSnowfall ? TEXT("on") : TEXT("off"), Chance, Draw);
	if (USSXParticleWorld* Pw = USSXParticleWorld::Get(this))
	{
		if (bSnowfall) { Pw->StartSnowfall(X, R1, R2, &Seeds); }
		else { Pw->StopSnowfall(); }
	}
}

void USSXTrackEffects::BuildActorMap()
{
	bActorsBuilt = true;
	ActorsByInstance.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const int32 Inst = SSXShowOff::InstanceOfActor(*It);
		if (Inst != INDEX_NONE) { ActorsByInstance.FindOrAdd(Inst).Add(*It); }
	}
}

void USSXTrackEffects::ApplyRender()
{
	SyncSplinePaths();
	if (DirtyDraw.Num() == 0 || !GetWorld())
	{
		return;
	}
	if (!bActorsBuilt)
	{
		BuildActorMap();
	}
	const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
	for (int32 Inst : DirtyDraw)
	{
		const TArray<TWeakObjectPtr<AActor>>* Actors = ActorsByInstance.Find(Inst);
		if (!Actors)
		{
			continue;
		}
		const bool bDrawn = (!W || W->NumDrawRecords() == 0 || W->IsInstanceDrawn(Inst, bShowOffMode)) && !World.IsScriptHidden(Inst)
			&& !(SplinePathInstances.Contains(Inst) && (World.GetFlags(Inst) & FlDisplay) == 0);
		for (const TWeakObjectPtr<AActor>& A : *Actors)
		{
			if (A.IsValid()) { A->SetActorHiddenInGame(!bDrawn); }
		}
	}
	DirtyDraw.Reset();
}
