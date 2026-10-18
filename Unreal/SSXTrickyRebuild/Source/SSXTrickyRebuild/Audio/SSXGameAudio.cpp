#include "Audio/SSXGameAudio.h"

#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXCrowdVoices.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Effects/SSXTrackEffects.h"
#include "UObject/UObjectIterator.h"
#include "Input/SSXRumbleSubsystem.h"
#include "Misc/FileHelper.h"
#include "Progression/SSXHeats.h"
#include "Progression/SSXSettingsEffects.h"
#include "SSXTrickyRebuild.h"
#include "Race/GameRng.h"
#include "Race/RaceSession.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/RiderSim.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/AnimController.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXRaceOverlays.h"

bool SSXGameAudio::WantsMenuMusic(const FSSXFrontEnd& FE)
{
	// 0x265398
	return FE.Boot == ESSXFeBoot::FrontEnd || FE.Boot == ESSXFeBoot::Attract || FE.Boot == ESSXFeBoot::Loading;
}

void SSXGameAudio::TickFrontEnd(USSXAudioSubsystem& Audio, FSSXFrontEnd& FE)
{
	// 0x20F430 / 0x20F530
	Audio.GetAnnouncer().EndFlags = 0;
	Audio.UpdateFrontEndMusic(WantsMenuMusic(FE));
	for (const FString& Hook : FE.SoundHooks)
	{
		if (Hook == TEXT("riderName"))
		{
			// 0x2791F0 / FUN_00265788
			const int32 Pad = FE.Top() ? FMath::Clamp(FE.Top()->Player, 0, 1) : 0;
			FeSpeech(Audio, FSSXSpeechSystem::RiderNameId(SSXFeTables::CarouselRider(FE.CarouselSlot[Pad])));
			continue;
		}
		if (Hook == TEXT("stopAnimVoice"))
		{
			// 0x270780
			Audio.StopSpeech(1);
			continue;
		}
		if (Hook == TEXT("vibrate0") || Hook == TEXT("vibrate1"))
		{
			// 0x26D1C8
			if (USSXRumbleSubsystem* Rumble = USSXRumbleSubsystem::Get(&Audio))
			{
				Rumble->FrontEndOneShot(Hook == TEXT("vibrate1") ? 1 : 0);
			}
			continue;
		}
		if (Hook == TEXT("whooshA") || Hook == TEXT("whooshB"))
		{
			Audio.PlayFeWhoosh(Hook == TEXT("whooshB"));
			continue;
		}
		if (Hook.StartsWith(TEXT("speech")) && Hook.Len() > 6 && FCString::IsNumeric(*Hook + 6))
		{
			FeSpeech(Audio, FCString::Atoi(*Hook + 6));
			continue;
		}
		if (Hook == TEXT("jukeboxStart"))
		{
			Audio.JukeboxOpen();   // cFE3DSound entry 28 (DVD jukebox page in): the menu song fades to byte 0
			continue;
		}
		if (Hook == TEXT("jukeboxStop"))
		{
			Audio.JukeboxStop();
			continue;
		}
		if (Hook.StartsWith(TEXT("jukebox")) && Hook.Len() > 7 && FCString::IsNumeric(*Hook + 7))
		{
			Audio.JukeboxPlay(FCString::Atoi(*Hook + 7));   // FUN_00299338
			continue;
		}
		if (Hook == TEXT("stinger0"))
		{
			Audio.GetSpeech().Stop(0);
			Audio.PlaySound2D(SSXUISound::FeId(SSXUISound::FeWhooshA));
			Audio.PlayFeStinger(0);
			continue;
		}
		if (Hook == TEXT("stopSpeech") || Hook == TEXT("stinger1") || Hook == TEXT("stinger2"))
		{
			Audio.GetSpeech().Stop(0);
			if (Hook != TEXT("stopSpeech"))
			{
				Audio.PlayFeStinger(Hook == TEXT("stinger1") ? 1 : 2);
			}
			continue;
		}
		Audio.PlayUISound(FName(*Hook));
	}
	FE.SoundHooks.Reset();
	// 0x21D028 / 0x2986C0
	FE.bJukeboxSongEnded = Audio.JukeboxSongEnded();
	Audio.TickMusicFade(1.f / 60.f);
	Audio.UpdateSpeech();                 // 0x228998
	Audio.TickDuck(4, 1.0, 1.f / 60.f);
}

bool SSXGameAudio::FeSpeech(USSXAudioSubsystem& Audio, int32 Id)
{
	FSSXSpeechSystem& Sp = Audio.GetSpeech();
	if (Sp.GetSet() != FSSXSpeechSystem::ESet::FrontEnd && !Audio.LoadSpeech(FSSXSpeechSystem::ESet::FrontEnd))
	{
		return false;
	}
	return Sp.FeRequest(Id);
}

int32 SSXGameAudio::OnRaceUnload(USSXAudioSubsystem& Audio, UWorld* RaceWorld)
{
	// 0x17ECF8 / 0x22E338 / 0x211960 / 0x211B18
	if (RaceWorld)
	{
		if (USSXTrackEffects* Fx = USSXTrackEffects::Get(RaceWorld)) { Fx->StopAudio(); }
	}
	// 0x211960 / 0x2151C8 / 0x225280 / 0x229D40
	Audio.GetRaceFlow().State = 0;
	SSXRaceAudio::FCommand StopIntro;
	StopIntro.Kind = SSXRaceAudio::FCommand::StopIntro;
	Audio.GetRaceFlow().Commands.Add(StopIntro);
	Audio.ExecuteRaceCommands();
	Audio.StopMusic(0.f);
	Audio.StopSpeech(0);
	Audio.StopSpeech(1);   // 0x211B18 / 0x2356A0
	Audio.GetCrowdVoices().End();   // 0x211B18 / 0x220BB8
	// 0x210B50 / 0x222DB0
	int32 Stopped = 0;
	if (RaceWorld)
	{
		FAudioDevice* Device = RaceWorld->GetAudioDeviceRaw();
		if (!Device && GEngine)
		{
			Device = GEngine->GetMainAudioDeviceRaw();
		}
		for (TObjectIterator<UAudioComponent> It; It; ++It)
		{
			UAudioComponent* C = *It;
			if (!C || C->bIsUISound || !C->IsPlaying())
			{
				continue;
			}
			const UWorld* W = C->GetWorld();
			if (W == RaceWorld || (!W && Device && C->GetAudioDevice() == Device))
			{
				C->Stop();
				++Stopped;
			}
		}
	}
	return Stopped;
}

void SSXGameAudio::OnRaceSetup(USSXAudioSubsystem& Audio, const FString& Track, int32 TrackId, int32 GameMode, bool bLoadedReplay)
{
	Audio.StopMusic(1.f);
	Audio.GetZoneAmbience().Reset();   // 0x20DB18
	// 0x211098 / 0x228830 / 0x17E148 / 0x2116D8
	if (Audio.LoadSpeech(FSSXSpeechSystem::ESet::Race))
	{
		FString Nascript;
		if (FFileHelper::LoadFileToString(Nascript, *SSXAudio::ConfigPath(TEXT("NASCRIPT.INF"))))
		{
			// TODO: the venue name the game passes (and app+0x58 language) are not traced;
			Audio.GetSpeech().LoadNarration(Nascript, Track, 0);
		}
	}
	if (bLoadedReplay)
	{
		Audio.BeginLoadedReplayRace(Track, TrackId);   // 0x2116D8
	}
	else
	{
		Audio.BeginRace(Track, TrackId, GameMode, false);
	}
	if (Audio.ArcadeSfx == 0)
	{
		UE_LOG(LogSSX, Display, TEXT("[audio] race load: Arcade SFX is Off (Options > Sound): Tricky plays only zbxsfx #107"));
	}
	Audio.GetAnnouncer().Reset(GameMode, TrackId);
	Audio.SeenIntroMusicEnds = 0;
	Audio.GetTricky().Reset();
	Audio.SeenTrickyFull = 0;
	Audio.SeenTrickyFresh = 0;
	Audio.SpeechWatch = USSXAudioSubsystem::FSpeechWatch();
	Audio.bAnimEventSounds = true;
	BindRiderSpeech(Audio);
}

void SSXGameAudio::BindRiderSpeech(USSXAudioSubsystem& Audio)
{
	SSXRiderSpeech::FHelpers& H = Audio.GetRiderSpeech();
	USSXAudioSubsystem* A = &Audio;
	H.IsChannel1Free = [A]() { return A->IsChannelFree(1); };
	H.StopIfTag = [A](int32 Tag) { A->StopChannel1IfTag(Tag); };
	H.Zone = [A](const FVector& P) { return A->ListenerZone ? A->ListenerZone(P) : 0; };
	H.Relation = [](int32 CA, int32 CB)
	{
		// 0x167E00
		const SSXHeats::FData& D = SSXHeats::FData::Get();
		return D.bLoaded && CA >= 0 && CA < SSXHeats::NumRiders && CB >= 0 && CB < SSXHeats::NumRiders ? D.Agg[CA][CB][3] : 0;
	};
	H.Post = [A](const SSXSpeech::FRequest& R, const FVector& Pos, int32 Tag, int32 Racer) { A->CharRequest(R, Pos, Tag, Racer); };
}

namespace
{
	SSXRiderSpeech::FRacer SpeechRacer(const USSXAudioSubsystem& Audio, const FRaceSession& Session, int32 I)
	{
		SSXRiderSpeech::FRacer R;
		const FRacerRecord& Rec = Session.GetRacer(I);
		R.Index = I;
		R.bHuman = Rec.bHuman;
		if (const FRiderSim* Sim = Rec.Sim)
		{
			R.bFinished = Sim->State.FinishResult >= 0;
			R.Character = Sim->Profile.Character;
			R.Voice = Sim->Profile.GetVoice();   // +0x64: the speaker masks read the voice byte, not +0x38
			R.GamePos = Sim->State.Position;
			R.Velocity = Sim->State.Velocity;
			R.PathHeading = Sim->State.PathHeading;
		}
		R.SoundPos = Audio.RacerLocation ? Audio.RacerLocation(I) : R.GamePos;
		R.bGrudge = Rec.Grudge.bActive;
		R.GrudgeRival = Rec.Grudge.Rival;
		for (int32 J = 0; J < 6; ++J)
		{
			R.Level[J] = Rec.Rivals[J].Level;
			R.Score[J] = Rec.Rivals[J].Score;
		}
		return R;
	}

	// 0x20ED20 / 0x193CE0
	int32 FocusRacer(const USSXAudioSubsystem& Audio, const FRaceSession& Session)
	{
		if (Audio.IsReplayPlayback() && Audio.GetReplayTargetRacer() >= 0 && Audio.GetReplayTargetRacer() < Session.NumRacers())
		{
			return Audio.GetReplayTargetRacer();
		}
		for (int32 I = 0; I < Session.NumRacers(); ++I)
		{
			if (Session.GetRacer(I).bHuman) return I;
		}
		return -1;
	}

	// 0x156618
	void SyncSegment(USSXAudioSubsystem& Audio, const FRiderSim& Sim)
	{
		if (const SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
		{
			if (Air->Score.SegmentStarts != Audio.SpeechWatch.Segment)
			{
				Audio.SpeechWatch.Segment = Air->Score.SegmentStarts;
				if (Sim.State.FinishResult < 0)
				{
					Audio.GetAnnouncer().SegmentStart();   // FUN_00157C50
				}
			}
		}
	}
}

SSXGameAudio::FAnimSnapshot SSXGameAudio::SnapshotAnim(const SSXAnim::FAnimController& C)
{
	FAnimSnapshot S;
	for (int32 Ch = 0; Ch < 3; ++Ch)
	{
		S.Count[Ch] = C.Instances(Ch).Num();
		if (const SSXAnim::FPlayInstance* N = C.Newest(Ch))
		{
			S.Clip[Ch] = N->Subs[0].Clip;
			S.Frame[Ch] = N->Subs[0].Frame;
		}
	}
	return S;
}

void SSXGameAudio::CrossedAnimEvents(const FAnimSnapshot& Before, const FAnimSnapshot& After, const SSXAnim::FAflLibrary& Lib, TArray<int32>& Out)
{
	for (int32 Ch = 0; Ch < 3; ++Ch)
	{
		const SSXAnim::FAflClip* C = After.Clip[Ch] != INDEX_NONE ? Lib.Clip(After.Clip[Ch]) : nullptr;
		if (!C || C->Events.Num() == 0)
		{
			continue;
		}
		const bool bSame = Before.Clip[Ch] == After.Clip[Ch] && After.Count[Ch] <= Before.Count[Ch] && After.Frame[Ch] >= Before.Frame[Ch];
		const double Old = bSame ? Before.Frame[Ch] : 0.0;
		double New = After.Frame[Ch];
		if (New >= double(C->NFrames - 1) && Old < double(C->NFrames - 1))
		{
			New = double(C->NFrames);
		}
		for (const FIntPoint& E : C->Events)
		{
			if (Old <= double(E.X) && double(E.X) < New)
			{
				Out.Add(E.Y);
			}
		}
	}
}

void SSXGameAudio::RiderSpeechFrame(USSXAudioSubsystem& Audio, const FRaceSession& Session, int32 PlayerRacer)
{
	auto RandA = []() { return FGameRng::StreamA().Next(); };
	USSXAudioSubsystem::FSpeechWatch& W = Audio.SpeechWatch;
	SSXRiderSpeech::FHelpers& H = Audio.GetRiderSpeech();
	H.Focus = FocusRacer(Audio, Session);
	if (H.Focus >= 0 && Session.GetRacer(H.Focus).Sim)
	{
		Audio.GetAnnouncer().PlayerVoice = Session.GetRacer(H.Focus).Sim->Profile.GetVoice();
	}
	const int32 N = FMath::Min(Session.NumRacers(), 6);
	if (!W.bInit)
	{
		W.bInit = true;
		for (int32 I = 0; I < N; ++I)
		{
			const FRiderSim* Sim = Session.GetRacer(I).Sim;
			W.Crash[I] = Sim ? Sim->State.CrashSoundCount : 0;
			W.Bump[I] = Sim ? Sim->State.BumpSpeechCount : 0;
			W.Grudge[I] = Session.GetRacer(I).Grudge.Kind2Starts;
		}
		if (PlayerRacer >= 0 && PlayerRacer < N && Session.GetRacer(PlayerRacer).Sim)
		{
			const FRiderState& S = Session.GetRacer(PlayerRacer).Sim->State;
			W.CrowdCue = S.CrowdCueCount;
			W.BoostEmpty = S.BoostEmptyPressCount;
			W.ForcedReset = S.ForcedResetCount;
		}
		return;
	}
	for (int32 I = 0; I < N; ++I)
	{
		const FRacerRecord& Rec = Session.GetRacer(I);
		if (!Rec.Sim)
		{
			continue;
		}
		const FRiderState& S = Rec.Sim->State;
		if (S.CrashSoundCount != W.Crash[I])
		{
			// 0x218328 / 0x235090
			W.Crash[I] = S.CrashSoundCount;
			H.Wipeout(SpeechRacer(Audio, Session, I), RandA);
		}
		if (S.BumpSpeechCount != W.Bump[I])
		{
			W.Bump[I] = S.BumpSpeechCount;
			H.Bump(SpeechRacer(Audio, Session, I), S.bBumpSpeechHard, RandA);   // 0x124DD0 / 0x235398
		}
		if (Rec.Grudge.Kind2Starts != W.Grudge[I])
		{
			W.Grudge[I] = Rec.Grudge.Kind2Starts;
			// 0x11ECF8 / 0x2336B8
			const int32 Rival = Rec.Grudge.Rival;
			if (Rival >= 0 && Rival < Session.NumRacers() && Session.GetRacer(Rival).bHuman)
			{
				Audio.GetAnnouncer().Grudge(Audio.GetSpeech(), S.FinishResult >= 0, RandA);
			}
		}
	}
	if (PlayerRacer >= 0 && PlayerRacer < N && Session.GetRacer(PlayerRacer).Sim)
	{
		const FRiderSim& Sim = *Session.GetRacer(PlayerRacer).Sim;
		const FRiderState& S = Sim.State;
		const bool bFinished = S.FinishResult >= 0;
		SyncSegment(Audio, Sim);
		if (S.CrowdCueCount != W.CrowdCue)
		{
			W.CrowdCue = S.CrowdCueCount;
			if (S.bCrowdCueBail && PlayerRacer == H.Focus)
			{
				// 0x2181E8 / 0x233D38
				FSSXAnnouncer& An = Audio.GetAnnouncer();
				An.Bail(Audio.GetSpeech(), bFinished, An.PlayerRankIndex, RandA);
			}
		}
		if (S.BoostEmptyPressCount != W.BoostEmpty)
		{
			W.BoostEmpty = S.BoostEmptyPressCount;
			if (!Audio.IsReplayPlayback())   // 0x11D038 / 0x2197D8 / 0x21BFD0
			{
				Audio.GetAnnouncer().BoostEmpty(Audio.GetSpeech(), bFinished, RandA);   // 0x2197D8 / 0x233E78
			}
		}
		if (S.ForcedResetCount != W.ForcedReset)
		{
			// 0x118F10 / 0x218EB0 / 0x233A18 / 0x229FA0
			W.ForcedReset = S.ForcedResetCount;
			if (Session.GetRacer(PlayerRacer).bHuman)
			{
				SSXRaceAudio::FFlow& F = Audio.GetRaceFlow();
				Audio.GetAnnouncer().ForcedReset(Audio.GetSpeech(), bFinished, PlayerRacer == H.Focus, F.Zones.Zone, RandA);
				Audio.StopSpeech(1);
				F.ForcedReset();
				Audio.GetZoneAmbience().StopVoice();   // 0x21D5C8
			}
		}
	}
}

void SSXGameAudio::OnRaceRestart(USSXAudioSubsystem& Audio, const FString& Track, int32 TrackId, int32 GameMode)
{
	SSXRaceAudio::FFlow& F = Audio.GetRaceFlow();
	Audio.GetZoneAmbience().Restart();   // 0x211B58 / 0x21D5C8
	F.Zones.Reset();
	for (const int32 E : F.Zones.Posted)
	{
		Audio.PostMusicEvent(E);
	}
	F.Zones.Posted.Reset();
	Audio.StopSpeech(0);
	Audio.StopSpeech(1);
	Audio.BeginRace(Track, TrackId, GameMode, true);
	Audio.GetAnnouncer().Reset(GameMode, TrackId, Audio.GetAnnouncer().HeatWord);
	Audio.SpeechWatch = USSXAudioSubsystem::FSpeechWatch();
	Audio.GetTricky().Reset();
	Audio.SeenTrickyFull = 0;
	Audio.SeenTrickyFresh = 0;
}

void SSXGameAudio::TrickyFrame(USSXAudioSubsystem& Audio, const FRaceSession& Session, int32 PlayerRacer, bool bReplayPlaying)
{
	// FUN_0020ED20 / FUN_0021BFD0
	const FRiderSim* Sim = PlayerRacer >= 0 && PlayerRacer < Session.NumRacers() ? Session.GetRacer(PlayerRacer).Sim : nullptr;
	TrickyFrame(Audio, Session.GetPhase() == ERacePhase::Race && !bReplayPlaying ? Sim : nullptr, int32(Session.GetMode()));
}

void SSXGameAudio::TrickyFrame(USSXAudioSubsystem& Audio, const FRiderSim* Sim, int32 Mode)
{
	FSSXTrickyAudio& T = Audio.GetTricky();
	T.bArcadeSfx = Audio.ArcadeSfx != 0;
	if (Sim)
	{
		const FRiderState& St = Sim->State;
		const SSXAir::FAirContext* Air = SSXAir::GetAirContext(*Sim);
		int32 Ubers = Air ? Air->Score.UberCount : 0;
		if (St.bInfiniteBoost) Ubers = FMath::Max(Ubers, SSXAir::InfiniteBoostUbers + 1);
		if (St.TrickyFullCount != Audio.SeenTrickyFull)
		{
			// 0x11B018 / FUN_0021DCE0 / FUN_0021C610
			const int32 Adds = FMath::Clamp(St.TrickyFullCount - Audio.SeenTrickyFull, 1, SSXTrickyAudio::DeferredSlots);
			const bool bFresh = St.TrickyFreshCount != Audio.SeenTrickyFresh;
			Audio.SeenTrickyFull = St.TrickyFullCount;
			Audio.SeenTrickyFresh = St.TrickyFreshCount;
			if (St.FinishResult < 0)
			{
				const FSSXTrickyPhraseContext Ctx = Audio.TrickyPhraseContext(Mode);
				for (int32 K = 0; K < Adds; ++K)
				{
					const double Before = (K == 0 && bFresh) ? 0.0
						: (Adds == 1 ? St.TrickyTimerBeforeFull : SSXAir::TrickyWindowSeconds);
					if (K == 0 && Before <= 0.0 && (!T.bArcadeSfx || !Ctx.bSongLoaded || Ctx.bSongEndPending))
					{
						SSXAudioDiag::Note(TEXT("tricky"), TEXT("activation phrase+MC0x204C"), false, 0.f, 0.0, SSXAudioDiag::EResult::Gated,
							!T.bArcadeSfx ? TEXT("Arcade SFX off (Options > Sound): only zbxsfx #107")
							: (!Ctx.bSongLoaded ? TEXT("no race song loaded") : TEXT("song ending posted (snd+0x3700 == 0)")));
					}
					T.MeterFull(Before, St.TrickyTimer, Ubers, Ctx);
				}
			}
		}
		T.Frame(St.TrickyTimer, Ubers);
	}
	T.Advance(1000.0 / 60.0);
	Audio.ExecuteTrickyCommands([]() { return FGameRng::StreamA().Next(); });
}

void SSXGameAudio::CountdownFrame(USSXAudioSubsystem& Audio, ERacePhase Phase, ERacePhase PrevPhase, double Clock, int32 Frame, bool bReplayPlaying)
{
	// 0x113600 / 0x216930 / 0x215288
	if (Phase != ERacePhase::Countdown || bReplayPlaying)
	{
		return;
	}
	SSXRaceAudio::FFlow& F = Audio.GetRaceFlow();
	if (PrevPhase != ERacePhase::Countdown)
	{
		// 0x113600
		F.bGo = false;
	}
	const bool bWasGo = F.bGo;
	F.Countdown(Clock, [&Audio]() { return Audio.Rand15(); });
	if (F.bGo && !bWasGo)
	{
		// FUN_00215288 / 0x229D40 / 0x2151C8 / 0x228B88
		Audio.GetAnnouncer().Go(Audio.GetSpeech(), []() { return FGameRng::StreamA().Next(); });
		Audio.SpeechWatch.GoFrame = Frame;
	}
}

void SSXGameAudio::RaceFrame(USSXAudioSubsystem& Audio, const FRaceSession& Session, int32 PlayerRacer, ERacePhase PrevPhase, bool bReplayPlaying)
{
	SSXRaceAudio::FFlow& F = Audio.GetRaceFlow();
	auto Rand = [&Audio]() { return Audio.Rand15(); };
	auto RandA = []() { return FGameRng::StreamA().Next(); };
	const ERacePhase Phase = Session.GetPhase();
	if (Session.GetIntroMusicEnds() != Audio.SeenIntroMusicEnds)
	{
		// FUN_00214E08
		Audio.SeenIntroMusicEnds = Session.GetIntroMusicEnds();
		F.IntroEnd();
	}
	Audio.GetAnnouncer().bReplay = bReplayPlaying;   // 0x21BFD0
	CountdownFrame(Audio, Phase, PrevPhase, Session.GetClock(), Session.GetFrame(), bReplayPlaying);
	if ((Phase == ERacePhase::EndRace || Phase == ERacePhase::ReplayEndRace) && PrevPhase != Phase)
	{
		// 0x113820
		F.DuckMode = 0;
		F.DuckLevel = 0.65;
	}
	Audio.GetAnnouncer().Frame(Audio.GetSpeech(), Session, PlayerRacer, RandA);
	if (Phase == ERacePhase::Race)
	{
		RiderSpeechFrame(Audio, Session, PlayerRacer);
	}
	int32 Rank = 0;
	double Boost = 0.0;
	// 0x20EE58 / 0x20ED20
	const int32 Listener = bReplayPlaying ? FocusRacer(Audio, Session) : PlayerRacer;
	if (Listener >= 0 && Listener < Session.NumRacers())
	{
		const FRacerRecord& R = Session.GetRacer(Listener);
		Rank = R.Rank;
		Boost = R.Sim ? R.Sim->State.BoostMeter : 0.0;
		if (R.Sim)
		{
			// 0x20ED20
			Audio.GetCrowdVoices().SetListenerRider(R.Sim->Profile.Character, R.Rank);
		}
	}
	F.Frame(Audio.GetIntroQueueLength(), Session.NumRacers(), Rank, Boost, Rand);
	TrickyFrame(Audio, Session, PlayerRacer, bReplayPlaying);   // 0x21BFD0
	F.TickZones(SSXRaceAudio::ZoneReturnDelay / 60.0);
	Audio.ExecuteRaceCommands();
	Audio.UpdateSpeech();   // 0x228998
	Audio.FollowSpeakers();   // 0x228BD8
	Audio.TickDuck(F.DuckMode, F.DuckLevel, 1.f / 60.f);
}

void SSXGameAudio::HandleSpeechCue(USSXAudioSubsystem& Audio, const FRaceSession& Session, const FRaceCue& Cue)
{
	auto RandA = []() { return FGameRng::StreamA().Next(); };
	if (Cue.Racer < 0 || Cue.Racer >= Session.NumRacers())
	{
		return;
	}
	SSXRiderSpeech::FHelpers& H = Audio.GetRiderSpeech();
	H.Focus = FocusRacer(Audio, Session);
	if (Cue.Kind == ERaceCueKind::RankChange)
	{
		// 0x115100 / 0x219A48
		const bool bWho = Session.GetRacer(Cue.Racer).bHuman || Cue.Racer == H.Focus;
		if (bWho && Audio.GetAnnouncer().LeadTaken(Session, Cue))
		{
			Audio.PlayZbxsfx(109);
		}
		if (bWho)
		{
			const int32 NewRank = Cue.Param, OldRank = Cue.Extra;
			int32 Behind = -1, Ahead = -1;
			for (int32 I = 0; I < Session.NumRacers(); ++I)
			{
				if (Session.GetRacer(I).Rank == NewRank + 1) Behind = I;
				if (Session.GetRacer(I).Rank == NewRank - 1) Ahead = I;
			}
			const int32 Passer = NewRank < OldRank ? Cue.Racer : Ahead;
			const int32 Passee = NewRank < OldRank ? Behind : Cue.Racer;
			if (Passer >= 0 && Passee >= 0)
			{
				H.Overtake(SpeechRacer(Audio, Session, Passer), SpeechRacer(Audio, Session, Passee), Session.GetFrame(),
					Audio.SpeechWatch.GoFrame, RandA);
			}
		}
	}
	else if (Cue.Kind == ERaceCueKind::RiderHit && Cue.Param >= 0 && Cue.Param < Session.NumRacers())
	{
		// 0x124920 / 0x234D00
		const SSXRiderSpeech::FRacer Vic = SpeechRacer(Audio, Session, Cue.Racer);
		const SSXRiderSpeech::FRacer Att = SpeechRacer(Audio, Session, Cue.Param);
		H.Hit(Att, Vic, Cue.Extra != 0, Cue.Type == 2, RandA);
		if (Cue.Type == 2 && Cue.Racer == H.Focus && H.Relation)
		{
			Audio.GetAnnouncer().KnockedDown(Audio.GetSpeech(), Att.bFinished, H.Relation(Vic.Character, Att.Character), RandA);
		}
	}
	else if (Cue.Kind == ERaceCueKind::Taunt && Cue.Param >= 0 && Cue.Param < Session.NumRacers())
	{
		// 0x103660 / 0x234F68
		H.Taunt(SpeechRacer(Audio, Session, Cue.Racer), SpeechRacer(Audio, Session, Cue.Param));
	}
	else if (Cue.Kind == ERaceCueKind::WinTaunt)
	{
		// 0x128150 / 0x235298
		H.WinTaunt(SpeechRacer(Audio, Session, Cue.Racer), Cue.Value);
	}
	else if (Cue.Kind == ERaceCueKind::Shove && Cue.Param >= 0 && Cue.Param < Session.NumRacers())
	{
		// 0x123FC8 / 0x216960 / 0x233CB8
		const FRacerRecord& A = Session.GetRacer(Cue.Racer);
		const FRacerRecord& B = Session.GetRacer(Cue.Param);
		if ((A.bHuman || B.bHuman) && Cue.Frame - Audio.SpeechWatch.LastBumpFrame > 30)
		{
			Audio.SpeechWatch.LastBumpFrame = Cue.Frame;
			const int32 Snd = SSXBoardAudio::BumpSound(Audio.Rand15());
			Audio.PlayZbxsfxAt(Snd, Audio.RacerLocation ? Audio.RacerLocation(Cue.Param) : FVector::ZeroVector);
		}
		if (Cue.Racer == H.Focus)
		{
			Audio.GetAnnouncer().ShoveHit(Audio.GetSpeech(), A.Sim && A.Sim->State.FinishResult >= 0, RandA);
		}
	}
	Audio.GetAnnouncer().Cue(Audio.GetSpeech(), Session, Cue, RandA);
}

void SSXGameAudio::OnTrackBoostOp(USSXAudioSubsystem& Audio, int32 Op)
{
	// FUN_002340B8 / FUN_00234308 / 0x21BFD0
	if (Audio.IsReplayPlayback())
	{
		return;
	}
	Audio.PlayZbxsfx(Op == 17 ? 0x73 : 0x72);
	Audio.GetAnnouncer().TrackBoostOp(Audio.GetSpeech(), Op);
}

void SSXGameAudio::OnTrackMultiplierOp(USSXAudioSubsystem& Audio, double Value)
{
	Audio.PlayZbxsfx(SSXBoardAudio::MultiplierSound(Value));
}

void SSXGameAudio::OnRiderMessage(USSXAudioSubsystem& Audio, const FRiderSim& Sim, int32 Type, int32 Value)
{
	auto RandA = []() { return FGameRng::StreamA().Next(); };
	const bool bFinished = Sim.State.FinishResult >= 0;
	FSSXAnnouncer& An = Audio.GetAnnouncer();
	if (Type == SSXMsg::TrickNew || Type == SSXMsg::TrickRepeat)
	{
		// 0x157088 / FUN_00157C50 / 0x232498 / 0x155440
		if (const SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
		{
			SyncSegment(Audio, Sim);   // 0x156618
			if (!bFinished)
			{
				const SSXAir::FTrickScore& Sc = Air->Score;
				An.TrickAnnounce(Audio.GetSpeech(), Sc.Announced.W0, Sc.Announced.W1, SSXAir::FTrickScore::PointsFor(Sc.Value, Sc.PointsMult),
					Type == SSXMsg::TrickRepeat, bFinished, RandA);
			}
		}
	}
	// 0x157330 / 0x233220
	An.Message(Audio.GetSpeech(), Type, Value, bFinished, RandA);
}

void SSXGameAudio::OnCameraOp(USSXAudioSubsystem& Audio, int32 Op, int32 Arg)
{
	// 0x2310C8
	FSSXSpeechSystem& Sp = Audio.GetSpeech();
	if (Op == 0x21)
	{
		Sp.Narrate(Arg, Audio.IsSpeechPlaying(0));
	}
	else if (Op == 0x22)
	{
		Audio.StopSpeech(0);
		Sp.StopNarration();
	}
	else if (Op == 0x29 || Op == 0x2A || Op == 0x2B)
	{
		const int32 Kind = Op == 0x29 ? 0 : (Op == 0x2A ? 2 : 1);
		Audio.GetAnnouncer().StagingNarration(Sp, Kind, [](int32 CA, int32 CB)
		{
			const SSXHeats::FData& D = SSXHeats::FData::Get();
			return D.bLoaded && CA >= 0 && CA < SSXHeats::NumRiders && CB >= 0 && CB < SSXHeats::NumRiders ? D.Agg[CA][CB][3] : 0;
		});
	}
}

bool SSXGameAudio::HandleRaceCue(USSXAudioSubsystem& Audio, const FRaceCue& Cue, bool bHumanRacer)
{
	// FUN_0017F1F0
	if (!bHumanRacer || (Cue.Kind != ERaceCueKind::TriggerEnter && Cue.Kind != ERaceCueKind::TriggerLeave))
	{
		return false;
	}
	SSXRaceAudio::FFlow& F = Audio.GetRaceFlow();
	F.Trigger(Cue.Type, Cue.Kind == ERaceCueKind::TriggerEnter, Cue.Extra);
	const bool bPosted = F.Commands.ContainsByPredicate([](const SSXRaceAudio::FCommand& C) { return C.Kind == SSXRaceAudio::FCommand::PostEvent; });
	Audio.ExecuteRaceCommands();
	return bPosted;
}

void SSXGameAudio::ApplySettings(USSXAudioSubsystem& Audio, const FSSXSettings& S)
{
	Audio.SetSpeaker(S.Speaker);
	Audio.SetMasterVolume(S.Master);
	Audio.SetMusicVolume(S.Music);
	Audio.SetSpeechVolume(S.McSpeech);
	Audio.SetCharSpeechVolume(S.CharSpeech);
	Audio.SetSfxVolume(S.Sfx);
	Audio.ArcadeSfx = S.ArcadeSfx;
	// TODO: the front end's 10 and several humans.
	Audio.GetAnnouncer().GateLevel = SSXSettings::AnnouncerLevel(S.Volume3, 9 - Audio.GetAnnouncer().PlayerRankIndex);
}

void SSXGameAudio::TickRaceOverlays(USSXAudioSubsystem& Audio, FSSXOverlayManager& Overlays)
{
	if (Overlays.bAudioLevelsChanged)
	{
		// 0x211DB0
		Overlays.bAudioLevelsChanged = false;
		if (const FSSXProfile* P = Overlays.GetProfile())
		{
			ApplySettings(Audio, P->Settings);
		}
	}
	for (const SSXUISound::ERace K : Overlays.Sounds)
	{
		Audio.PlayRaceUISound(K);
	}
	Overlays.Sounds.Reset();
}
