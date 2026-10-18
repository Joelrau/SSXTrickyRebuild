// 0x25EDF0
#include "Audio/SSXAnnouncer.h"
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXCrowdVoices.h"
#include "Audio/SSXRaceAudioFlow.h"
#include "Audio/SSXSpeechSystem.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXPathfinder.h"
#include "Effects/TrackEffects.h"
#include "Effects/SSXTrackEffects.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Race/RaceSession.h"
#include "Sim/PatchWorld.h"
#include "SnowboarderPawn.h"
#include "Sound/SoundWave.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AudioTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FString PackageOf(const FString& ObjectPath)
	{
		FString Package = ObjectPath;
		int32 Dot;
		if (Package.FindChar(TEXT('.'), Dot)) { Package.LeftInline(Dot); }
		return Package;
	}

	USSXAudioSubsystem* MakeAudio()
	{
		return NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioAssetsResolveTest, "SSX.Audio.AssetsResolve", AudioTestFlags)

bool FSSXAudioAssetsResolveTest::RunTest(const FString& Parameters)
{
	FSSXAudioManifest Manifest;
	FString Err;
	if (!Manifest.Load(SSXAudio::ManifestPath(), &Err))
	{
		AddError(FString::Printf(TEXT("manifest: %s"), *Err));
		return false;
	}
	int32 Imported = 0, Missing = 0, Loops = 0, LoopsWrong = 0;
	for (const TPair<FString, FSSXAudioEntry>& KV : Manifest.GetEntries())
	{
		const FSSXAudioEntry& E = KV.Value;
		if (!E.bImport)
		{
			continue;
		}
		const FString Path = SSXAudio::AssetPathForFile(E.File);
		if (!FPackageName::DoesPackageExist(PackageOf(Path)))
		{
			if (++Missing <= 10) { AddError(FString::Printf(TEXT("%s: no asset %s (run Scripts/import_audio.py)"), *E.Id, *Path)); }
			continue;
		}
		++Imported;
		if (E.LoopStart >= 0)
		{
			++Loops;
			const USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Wave || !Wave->bLooping)
			{
				if (++LoopsWrong <= 10) { AddError(FString::Printf(TEXT("%s: manifest loop but %s is not a looping SoundWave"), *E.Id, *Path)); }
			}
		}
	}
	TestEqual(TEXT("imported assets missing"), Missing, 0);
	TestEqual(TEXT("looping assets wrong"), LoopsWrong, 0);
	TestTrue(TEXT("some assets"), Imported > 0);
	AddInfo(FString::Printf(TEXT("%d assets resolve, %d looping"), Imported, Loops));

	USSXAudioSubsystem* Audio = MakeAudio();
	int32 NotInBank = 0;
	for (const FString& Id : USSXBoardSoundComponent::AllSoundIds())
	{
		if (!Manifest.Find(Id))
		{
			++NotInBank;
			AddInfo(FString::Printf(TEXT("%s is not in the bank"), *Id));
			continue;
		}
		TestNotNull(*FString::Printf(TEXT("sound %s loads"), *Id), Audio->LoadSound(Id));
	}
	TestEqual(TEXT("board ids missing from the banks (zboard #69, #70)"), NotInBank, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioFrontEndMusicTest, "SSX.Audio.FrontEndMusic", AudioTestFlags)

bool FSSXAudioFrontEndMusicTest::RunTest(const FString& Parameters)
{
	USSXAudioSubsystem* Audio = MakeAudio();
	FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXCheats Cheats;
	FSSXFrontEnd FE;
	FE.Init(&Profile, &Cheats, nullptr, true);

	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestFalse(TEXT("no menu song on the card check"), Audio->IsMenuMusicActive());
	FE.Boot = ESSXFeBoot::EaLogo;
	FE.MovieRequest = TEXT("EABIG");
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestFalse(TEXT("no menu song during eabig"), Audio->IsMenuMusicActive());
	FE.MovieFinished();
	TestEqual(TEXT("then ssxintro"), FE.MovieRequest, FString(TEXT("SSXINTRO")));
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestFalse(TEXT("no menu song during ssxintro"), Audio->IsMenuMusicActive());
	FE.MovieFinished();
	TestTrue(TEXT("menus up"), FE.Boot == ESSXFeBoot::FrontEnd && FE.MovieRequest.IsEmpty());

	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestTrue(TEXT("menu song at Title"), Audio->IsMenuMusicActive());
	TestEqual(TEXT("menu song"), Audio->GetCurrentSongName(), FString(TEXT("ssxmenu")));
	TestTrue(TEXT("pathfinder player built"), Audio->GetPathfinderPlayer().IsValid());
	const TSharedPtr<FSSXPathfinderPlayer> Player = Audio->GetPathfinderPlayer();
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestTrue(TEXT("not restarted every frame"), Audio->GetPathfinderPlayer() == Player);

	FE.SoundHooks.Add(TEXT("ui7"));
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestEqual(TEXT("sound hooks drained"), FE.SoundHooks.Num(), 0);

	FE.MovieRequest = TEXT("PREVIEW");
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestTrue(TEXT("a menu movie does not end the song"), Audio->IsMenuMusicActive() && Audio->GetPathfinderPlayer() == Player);
	Audio->PauseForMovie();
	Audio->PauseForMovie();
	TestTrue(TEXT("paused once (snd+0x3830)"), Audio->IsMoviePaused() && Audio->MoviePauses == 1 && Player->IsPaused());
	Audio->ResumeAfterMovie();
	Audio->ResumeAfterMovie();
	TestTrue(TEXT("resumed once only when set"), !Audio->IsMoviePaused() && Audio->MovieResumes == 1 && !Player->IsPaused());
	FE.MovieRequest.Reset();

	// 0x265398
	FE.Boot = ESSXFeBoot::Attract;
	FE.MovieRequest = TEXT("SSXINTRO");
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestTrue(TEXT("attract movie does not end the song"), Audio->IsMenuMusicActive() && Audio->GetPathfinderPlayer() == Player);
	FE.MovieFinished();
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestTrue(TEXT("same song after the attract"), Audio->IsMenuMusicActive() && Audio->GetPathfinderPlayer() == Player);

	USSXAudioSubsystem* Audio2 = MakeAudio();
	FSSXFrontEnd Warm;
	Warm.Init(&Profile, &Cheats, nullptr, false);
	SSXGameAudio::TickFrontEnd(*Audio2, Warm);
	TestTrue(TEXT("menu song on return from a race"), Audio2->IsMenuMusicActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioRaceMusicTest, "SSX.Audio.RaceMusic", AudioTestFlags)

bool FSSXAudioRaceMusicTest::RunTest(const FString& Parameters)
{
	USSXAudioSubsystem* Audio = MakeAudio();
	Audio->PlayMenuMusic();
	TestTrue(TEXT("menu song first"), Audio->IsMenuMusicActive());

	FRaceCue Tick;
	Tick.Kind = ERaceCueKind::CountdownTick;
	TestFalse(TEXT("countdown cue posts no music event"), SSXGameAudio::HandleRaceCue(*Audio, Tick, true));
	SSXGameAudio::OnRaceSetup(*Audio, TEXT("gari"), 0, 2);
	TestFalse(TEXT("race setup stops the menu song"), Audio->IsMenuMusicActive());

	FSSXInfFile MusicMap, Music;
	TestTrue(TEXT("MUSICMAP.INF"), MusicMap.Load(SSXAudio::ConfigPath(TEXT("MUSICMAP.INF"))));
	TestTrue(TEXT("MUSIC.INF"), Music.Load(SSXAudio::ConfigPath(TEXT("MUSIC.INF"))));
	const FSSXInfFile::FSection* Gari = MusicMap.Find(TEXT("GARI"));
	if (!TestNotNull(TEXT("[GARI]"), Gari)) return false;
	TSet<FString> Allowed;
	for (const FString& Key : Gari->GetAll(TEXT("SONG")))
	{
		for (const FSSXInfFile::FSection& S : Music.Sections)
		{
			const FString Path = S.Get(TEXT("PATHDATA"));
			if (!Path.IsEmpty() && S.Name.StartsWith(Key, ESearchCase::IgnoreCase))
			{
				Allowed.Add(FPaths::GetBaseFilename(Path).ToLower());
				break;
			}
		}
	}
	TestTrue(TEXT("[GARI] songs resolve"), Allowed.Num() > 0);

	TestTrue(TEXT("race load prepares the song, paused"), Audio->GetPathfinderPlayer().IsValid() && Audio->GetPathfinderPlayer()->IsPaused());
	Audio->GetRaceFlow().Go([Audio]() { return Audio->Rand15(); });
	Audio->ExecuteRaceCommands();
	TestFalse(TEXT("GO unpauses the track song"), Audio->GetPathfinderPlayer()->IsPaused());
	TestFalse(TEXT("not the menu song"), Audio->IsMenuMusicActive());
	TestTrue(*FString::Printf(TEXT("song '%s' is a [GARI] song"), *Audio->GetCurrentSongName()), Allowed.Contains(Audio->GetCurrentSongName()));
	TestTrue(TEXT("pathfinder player built"), Audio->GetPathfinderPlayer().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioLessonGoNarrationTest, "SSX.Audio.LessonGoNarration", AudioTestFlags)

bool FSSXAudioLessonGoNarrationTest::RunTest(const FString& Parameters)
{
	// 0x228D10 / 0x231398 / FUN_00215288 / 0x2151C8
	USSXAudioSubsystem* Audio = MakeAudio();
	if (!Audio->LoadSpeech(FSSXSpeechSystem::ESet::Race))
	{
		SSXTest::Skip(*this, TEXT("race speech databases missing (tools/convert_speech.py): ordering check skipped"));
		return true;
	}
	int32 Stops0 = 0;
	Audio->GetSpeech().OnStop = [&Stops0](int32 C) { Stops0 += C == 0 ? 1 : 0; };
	Audio->GetRaceFlow().Load(0, false, false, true, 1.0, [Audio]() { return Audio->Rand15(); });
	Audio->GetAnnouncer().Reset(6, 0, 0);
	ERacePhase Before = ERacePhase::StartRace;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		Audio->StopSpeech(0);   // 0x228D10
		const int32 StopsAfterEnter = Stops0;
		const TArray<uint32> Narr = Audio->GetAnnouncer().LessonNarration(Audio->GetSpeech(), Attempt == 0 ? 1 : 2);
		const int32 LogAfterNarr = Audio->GetSpeech().Log.Num();
		Audio->GetRaceFlow().Commands.Reset();
		SSXGameAudio::CountdownFrame(*Audio, ERacePhase::Countdown, Before, 2.0, 1000 + Attempt);
		const FString A = FString::Printf(TEXT("attempt %d: "), Attempt + 1);
		TestTrue(A + TEXT("GO ran (song control re-drawn)"), Audio->GetRaceFlow().bGo
			&& Audio->GetRaceFlow().Commands.ContainsByPredicate([](const SSXRaceAudio::FCommand& C) { return C.Kind == SSXRaceAudio::FCommand::SetControl; }));
		TestEqual(A + TEXT("GO frame snd+0x7DDC"), Audio->SpeechWatch.GoFrame, 1000 + Attempt);
		TestEqual(A + TEXT("GO does not stop channel 0 after the narration"), Stops0, StopsAfterEnter);
		TestEqual(A + TEXT("no MC line at GO in mode 6 (0x2334B0)"), Audio->GetSpeech().Log.Num(), LogAfterNarr);
		TestTrue(A + TEXT("the narration is the last request (NARR 0x800F)"), Narr.Num() > 0 && Audio->GetSpeech().Log.Num() > 0
			&& Audio->GetSpeech().Log.Last().Req[0] == Narr[0]);
		Audio->GetRaceFlow().Commands.Reset();
		SSXGameAudio::CountdownFrame(*Audio, ERacePhase::Race, ERacePhase::Countdown, 2.0, 1001 + Attempt);
		TestEqual(A + TEXT("one GO per countdown"), Audio->GetRaceFlow().Commands.Num(), 0);
		Before = ERacePhase::Race;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioRaceUnloadTest, "SSX.Audio.RaceUnload", AudioTestFlags)

bool FSSXAudioRaceUnloadTest::RunTest(const FString& Parameters)
{
	// 0x22E338 / 0x211960 / 0x211B18
	USSXAudioSubsystem* Audio = MakeAudio();
	SSXGameAudio::OnRaceSetup(*Audio, TEXT("gari"), 0, 2);
	const bool bSong = !Audio->GetCurrentSongName().IsEmpty();
	int32 Stops[2] = { 0, 0 };
	const bool bSpeech = Audio->GetSpeech().IsLoaded();
	Audio->GetSpeech().OnStop = [&Stops](int32 C) { if (C == 0 || C == 1) ++Stops[C]; };
	UPackage* Package = CreatePackage(TEXT("/Temp/SSXAudioTest/gari_unload"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("gari_unload"), Package);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	USoundBase* Wave = Audio->LoadSound(TEXT("sfx/zbxsfx/78"));
	UAudioComponent* Game = Wave ? UGameplayStatics::CreateSound2D(World, Wave, 1.f, 1.f, 0.f, nullptr, false, false) : nullptr;
	UAudioComponent* Ui = Wave ? UGameplayStatics::CreateSound2D(World, Wave, 1.f, 1.f, 0.f, nullptr, false, false) : nullptr;
	USSXAudioSubsystem::MakeGameSound(Game);
	if (Ui) { Ui->bIsUISound = true; }
	if (Game) Game->Play();
	if (Ui) Ui->Play();
	const bool bVoices = Game && Ui && Game->IsPlaying() && Ui->IsPlaying();

	const int32 Stopped = SSXGameAudio::OnRaceUnload(*Audio, World);
	if (bSong) TestTrue(TEXT("race song stopped at once"), Audio->GetCurrentSongName().IsEmpty());
	else SSXTest::Skip(*this, TEXT("no race song (audio data missing): song check skipped"));
	TestEqual(TEXT("audio state 0"), Audio->GetRaceFlow().State, 0);
	TestTrue(TEXT("stream 2 (intro) stopped"), Audio->GetRaceCommandLog().Num() > 0
		&& Audio->GetRaceCommandLog().Last().Kind == SSXRaceAudio::FCommand::StopIntro);
	if (bSpeech) TestTrue(TEXT("both speech channels stopped"), Stops[0] >= 1 && Stops[1] >= 1);
	TestFalse(TEXT("crowd ended"), Audio->GetCrowdVoices().IsBegun());
	if (bVoices)
	{
		TestFalse(TEXT("the race world's game voice stopped"), Game->IsPlaying());
		TestTrue(TEXT("a UI voice keeps playing"), Ui->IsPlaying());
		TestEqual(TEXT("one world voice stopped"), Stopped, 1);
	}
	else
	{
		AddInfo(TEXT("no audio device (NullRHI / -nosound): world voice check skipped"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioFrontEndNoAdlTest, "SSX.Audio.FrontEndNoAdl", AudioTestFlags)

bool FSSXAudioFrontEndNoAdlTest::RunTest(const FString& Parameters)
{
	// 0x25EDF0 / 0x22A470 / 0x22A518
	FString Err;
	SSXEffects::FEffectData Probe;
	if (!Probe.Load(SSXEffects::FEffectData::DefaultPath(TEXT("ssxfe")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("SimData/ssxfe.effects.bin: %s (skipped)"), *Err));
		return true;
	}
	struct FWarnCatcher : public FOutputDevice
	{
		TArray<FString> Lines;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity <= ELogVerbosity::Warning && FString(V).Contains(TEXT("propsnd"))) { Lines.Add(V); }
		}
	};
	UPackage* Package = CreatePackage(TEXT("/Temp/SSXAudioTest/ssxfe"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ssxfe"), Package);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	FWarnCatcher Catcher;
	GLog->AddOutputDevice(&Catcher);
	ON_SCOPE_EXIT
	{
		GLog->RemoveOutputDevice(&Catcher);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	USSXTrackEffects* Fx = USSXTrackEffects::Get(World);
	if (!TestNotNull(TEXT("track effects subsystem"), Fx)) return false;
	TestTrue(TEXT("FE load begins the ssxfe effects"), Fx->BeginTrack(TEXT("ssxfe"), 11, 0, nullptr, true));
	TestEqual(TEXT("no prop sound records in the FE"), Fx->GetPropSounds().NumRecords, 0);
	TestEqual(TEXT("no 'propsnd missing' warning for the FE load"), Catcher.Lines.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioPawnBoardSoundTest, "SSX.Audio.PawnBoardSound", AudioTestFlags)

bool FSSXAudioPawnBoardSoundTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FPatchWorld> Patches = MakeShared<FPatchWorld>();
	FString Err;
	if (!Patches->LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("SimData/gari.patches.bin: %s"), *Err));
		return false;
	}
	UPackage* Package = CreatePackage(TEXT("/Temp/SSXAudioTest/gari"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("gari"), Package);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	constexpr int32 Steps = 30;
	for (const bool bAI : { false, true })
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASnowboarderPawn* Pawn = World->SpawnActor<ASnowboarderPawn>(ASnowboarderPawn::StaticClass(), FTransform::Identity, Params);
		if (!TestNotNull(TEXT("pawn"), Pawn)) return false;
		Pawn->bAIRider = bAI;
		Pawn->bRaceDriven = true;
		Pawn->bStartRestarts = false;
		Pawn->SetSharedWorld(Patches);
		Pawn->FinishSpawning(FTransform::Identity);
		Pawn->DispatchBeginPlay();
		if (!TestTrue(TEXT("race pawn has a sim"), Pawn->HasSim())) return false;

		USSXBoardSoundComponent* Board = Pawn->GetBoardSound();
		if (!TestNotNull(TEXT("board sound component"), Board)) return false;
		TestEqual(TEXT("board sound attached to the pawn"), Board->GetOwner(), static_cast<AActor*>(Pawn));
		TestEqual(TEXT("AI flag follows the rider"), Board->bIsAI, bAI);
		TestTrue(TEXT("SNOW.INF programs loaded"), Board->GetPrograms().IsLoaded());
		const int32 Before = Board->UpdateCount;
		for (int32 I = 0; I < Steps; ++I)
		{
			Pawn->RaceStepFrame();
		}
		TestEqual(*FString::Printf(TEXT("%s: driven once per sim step"), bAI ? TEXT("AI") : TEXT("player")), Board->UpdateCount - Before, Steps);
		// 0x20FC60
		const int32 N0 = Board->OneShotLog.Num();
		Board->PlayOneShotSound(SSXBoardAudio::ResetSound, 127, false);
		if (TestEqual(TEXT("reset one-shot logged"), Board->OneShotLog.Num(), N0 + 1)
			&& TestEqual(TEXT("3D log follows the id log"), Board->OneShot3DLog.Num(), Board->OneShotLog.Num()))
		{
			TestEqual(*FString::Printf(TEXT("%s reset #123 positional"), bAI ? TEXT("AI") : TEXT("player")), Board->OneShot3DLog.Last(), bAI);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioJukeboxTest, "SSX.Audio.Jukebox", AudioTestFlags)

bool FSSXAudioJukeboxTest::RunTest(const FString& Parameters)
{
	// FUN_00225468 / 0x2254F8
	{
		SSXMusicFade::FFade F;
		F.Start(127, 0, 0.5f);
		TestTrue(TEXT("127 -> 0 over 0.5 s: step -10"), F.bActive && F.Step == -10.f);
		int32 B = 127;
		F.Tick(0.039f, B);
		TestEqual(TEXT("nothing before 40 ms"), B, 127);
		F.Tick(0.001f, B);
		TestEqual(TEXT("first call: 117"), B, 117);
		for (int32 K = 0; K < 11; ++K) { F.Tick(0.04f, B); }
		TestTrue(TEXT("12 calls: 7, still running"), B == 7 && F.bActive);
		F.Tick(0.04f, B);
		TestTrue(TEXT("13th call passes the target: 0, done"), B == 0 && !F.bActive);
		F.Start(0, 127, 0.5f);
		for (int32 K = 0; K < 12; ++K) { F.Tick(0.04f, B); }
		TestTrue(TEXT("0 -> 127: 120 after 12 calls"), B == 120 && F.bActive);
		F.Tick(0.04f, B);
		TestTrue(TEXT("then 127"), B == 127 && !F.bActive);
		F.Start(127, 115, 0.5f);
		TestEqual(TEXT("|delta| < 25: step 0"), F.Step, 0.f);
		F.Tick(0.04f, B);
		TestTrue(TEXT("step 0: the target at the first call"), B == 115 && !F.bActive);
		SSXMusicFade::FFade G;
		G.Start(127, 127, 0.5f);
		TestFalse(TEXT("already the target: no fade"), G.bActive);
	}

	USSXAudioSubsystem* Audio = MakeAudio();
	FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXCheats Cheats;
	FSSXFrontEnd FE;
	FE.Init(&Profile, &Cheats, nullptr, true);
	TestEqual(TEXT("JUKEBOX.INF: 21 songs"), Audio->GetJukeboxCount(), 21);
	TestTrue(TEXT("class 5 gain = [FELEVELSCALING] JUKEBOXLEVEL 100 (music volume not applied)"), FMath::IsNearlyEqual(Audio->GetJukeboxGain(), 1.f));
	Audio->SetMusicVolume(0.5f);
	TestTrue(TEXT("the music slider does not scale the jukebox"), FMath::IsNearlyEqual(Audio->GetJukeboxGain(), 1.f));
	Audio->SetMusicVolume(1.f);

	// entry 28 + 29 on the page's idle: the menu song's volume byte fades to 0 over 0.5 s, song 0 requested
	FE.SoundHooks = { TEXT("jukeboxStart"), TEXT("jukebox0") };
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestTrue(TEXT("jukebox open, song 0"), Audio->IsJukeboxOpen() && Audio->GetJukeboxSong() == 0);
	TestFalse(TEXT("no song started without a world: entry 31 stays false"), FE.bJukeboxSongEnded);
	for (int32 N = 0; N < 28; ++N) { SSXGameAudio::TickFrontEnd(*Audio, FE); }
	TestEqual(TEXT("29 frames (483 ms): 12 calls -> byte 7"), Audio->GetBigAirMusicByte(), 7);
	for (int32 N = 0; N < 3; ++N) { SSXGameAudio::TickFrontEnd(*Audio, FE); }
	TestEqual(TEXT("32 frames (533 ms): the 13th call -> byte 0"), Audio->GetBigAirMusicByte(), 0);
	FE.SoundHooks = { TEXT("jukebox20") };
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestEqual(TEXT("entry 29: song 20"), Audio->GetJukeboxSong(), 20);
	TestFalse(TEXT("index out of the list: nothing plays"), Audio->JukeboxPlay(21));
	// entry 32: the stream stopped, the byte back to 127 over 0.5 s
	FE.SoundHooks = { TEXT("jukeboxStop") };
	SSXGameAudio::TickFrontEnd(*Audio, FE);
	TestFalse(TEXT("jukebox closed"), Audio->IsJukeboxOpen());
	for (int32 N = 0; N < 40; ++N) { SSXGameAudio::TickFrontEnd(*Audio, FE); }
	TestEqual(TEXT("menu song volume byte back to 127"), Audio->GetBigAirMusicByte(), 127);
	return true;
}

#endif
