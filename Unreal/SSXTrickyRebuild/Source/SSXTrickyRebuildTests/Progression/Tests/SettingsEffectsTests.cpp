// 0x2657A8, 0x1BC8A0, 0x1B32C0
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/Paths.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXIntroMusic.h"
#include "Input/PadMapper.h"
#include "Progression/SSXProgression.h"
#include "Progression/SSXSettingsEffects.h"
#include "UI/SSXFeOptions.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXHudData.h"
#include "UI/SSXHudOverlay.h"
#include "UI/SSXMemCard.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"
#include "World/SSXWorldPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SetTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
	constexpr float On = 10.f / 11.f;

	USSXAudioSubsystem* NewAudio()
	{
		return NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	}

	const FSSXLocStrings& SetLoc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	struct FSetFe
	{
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FSetFe()
		{
			SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
			FE.Init(&Profile, &Cheats, &SetLoc(), false);
		}
		void Settle() { for (int32 N = 0; !FE.IsReady() && FE.Boot == ESSXFeBoot::FrontEnd && N < 3000; ++N) FE.Tick(); }
		void Press(ESSXFeKey K) { Settle(); FE.Key(0, K); FE.Tick(); Settle(); }
		bool OpenPage(int32 P)
		{
			Press(ESSXFeKey::Square);
			for (int32 I = 0; I < P; ++I) Press(ESSXFeKey::Down);
			Press(ESSXFeKey::Cross);
			const FSSXFeState* S = FE.Top();
			return S && S->Screen == ESSXFeScreen::Options && S->Page == P;
		}
		void Row(int32 R) { for (int32 I = 0; I < R; ++I) Press(ESSXFeKey::Down); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsDefaultsTest, "SSX.Settings.Defaults", SetTestFlags)
bool FSSXSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	const FSSXSettings S;
	TestEqual(TEXT("arcade 1 (sound ctor / presets set snd+0x3844 = 1, copied back by 0x161928)"), S.ArcadeSfx, 1);
	TestEqual(TEXT("speaker 2"), S.Speaker, 2);
	TestEqual(TEXT("units 1 (mph)"), S.Units, 1);
	TestEqual(TEXT("widescreen off"), S.Widescreen, 0);
	TestEqual(TEXT("split 0"), S.SplitScreen, 0);
	TestEqual(TEXT("1P camera 1 (LOC 333)"), S.Camera1P, 1);
	TestEqual(TEXT("2P camera 1"), S.Camera2P, 1);
	TestEqual(TEXT("HUD full"), S.HudDetail, 0);
	TestEqual(TEXT("brightness scale 1.0 at 0.5"), SSXProg::BrightnessScale(S), 1.f);
	TestEqual(TEXT("max boarders 6"), S.MaxBoarders, 6);
	TestEqual(TEXT("replay advanced"), S.Replay, 1);
	TestEqual(TEXT("autoload on"), S.Autoload, 1);
	TestEqual(TEXT("vibration on, both ports"), S.Vibration[0] + S.Vibration[1], 2);
	TestTrue(TEXT("a new save carries the start-up settings"), FSSXSaveData::NewSave().Settings == S);
	FSSXSaveData D = FSSXSaveData::NewSave();
	D.Settings.Vibration[0] = 0;
	D.Settings.Vibration[1] = 1;
	D.Settings.Camera1P = 3;
	D.Settings.HudDetail = 2;
	D.Settings.Widescreen = 2;
	D.Settings.CharSpeech = 3.f / 11.f;
	FSSXSaveData R;
	TestTrue(TEXT("blob round trip"), R.FromBlob(D.ToBlob()));
	TestEqual(TEXT("vibration P1 saved"), R.Settings.Vibration[0], 0);
	TestEqual(TEXT("vibration P2 saved"), R.Settings.Vibration[1], 1);
	TestEqual(TEXT("1P camera saved"), R.Settings.Camera1P, 3);
	TestEqual(TEXT("HUD detail saved"), R.Settings.HudDetail, 2);
	TestEqual(TEXT("widescreen saved"), R.Settings.Widescreen, 2);
	TestEqual(TEXT("character speech saved"), R.Settings.CharSpeech, 3.f / 11.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsAudioLevelsTest, "SSX.Settings.AudioLevels", SetTestFlags)
bool FSSXSettingsAudioLevelsTest::RunTest(const FString& Parameters)
{
	USSXAudioSubsystem* A = NewAudio();
	FSSXSettings S;
	S.Master = 6.f / 11.f;
	S.Music = 2.f / 11.f;
	S.McSpeech = 4.f / 11.f;
	S.CharSpeech = 9.f / 11.f;
	S.Sfx = 5.f / 11.f;
	S.ArcadeSfx = 0;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("class 0 master"), A->GetMasterVolume(), 6.f / 11.f);
	TestEqual(TEXT("class 1 music"), A->GetMusicVolume(), 2.f / 11.f);
	TestEqual(TEXT("class 2 PA / MC speech"), A->GetSpeechVolume(), 4.f / 11.f);
	TestEqual(TEXT("class 3 character speech"), A->GetCharSpeechVolume(), 9.f / 11.f);
	TestEqual(TEXT("class 4 SFX"), A->GetSfxVolume(), 5.f / 11.f);
	TestEqual(TEXT("arcade flag snd+0x3844"), A->ArcadeSfx, 0);
	const float G0 = A->GetSpeechGain(0), G1 = A->GetSpeechGain(1);
	S.CharSpeech = 4.5f / 11.f;
	SSXGameAudio::ApplySettings(*A, S);
	TestTrue(TEXT("halving character speech halves channel 1"), FMath::IsNearlyEqual(A->GetSpeechGain(1), G1 * 0.5f, 1e-5f));
	TestTrue(TEXT("channel 0 (PA) unchanged"), FMath::IsNearlyEqual(A->GetSpeechGain(0), G0, 1e-6f));
	S.CharSpeech = 0.f;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("character speech 0 silences channel 1"), A->GetSpeechGain(1), 0.f);
	const float Sfx1 = A->GetSfxGain();
	S.Master = 3.f / 11.f;
	SSXGameAudio::ApplySettings(*A, S);
	// 0x2D2F90
	TestTrue(TEXT("master scales the SFX gain"), FMath::IsNearlyEqual(A->GetSfxGain(), Sfx1 * 34.f / 69.f, 1e-5f));
	// 0x211DB0
	S.Music = 1.7f;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("level clamped to 1"), A->GetMusicVolume(), 1.f);
	// 0x235D58
	S.Volume3 = On;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("gate level 0 at 10/11"), A->GetAnnouncer().GateLevel, 0);
	S.Volume3 = 1.f;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("gate level 1 at 1.0"), A->GetAnnouncer().GateLevel, 1);
	TestEqual(TEXT("AnnouncerLevel caps at snd+0x6E68"), SSXSettings::AnnouncerLevel(1.f, 0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsMasterSilenceTest, "SSX.Settings.MasterSilence", SetTestFlags)
bool FSSXSettingsMasterSilenceTest::RunTest(const FString& Parameters)
{
	// 0x211DB0, 0x2D2F90, 0x2D5148
	USSXAudioSubsystem* A = NewAudio();
	FSSXSettings S;
	S.Master = 1.f; S.Music = 1.f; S.McSpeech = 1.f; S.CharSpeech = 1.f; S.Sfx = 1.f;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("master 1 -> byte 127"), A->GetMasterGain(), 1.f);
	TestTrue(TEXT("full levels are audible"), A->GetSfxGain() > 0.f && A->MusicClassLevel(true) > 0.f && A->GetSpeechGain(0) > 0.f);
	S.Master = On;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("master 10/11 -> byte 115"), A->GetMasterGain(), 115.f / 127.f);
	S.Master = 3.f / 11.f;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("master 3/11 -> byte 34 (the RAM snapshot's lib +0x1D9)"), A->GetMasterGain(), 34.f / 127.f);
	S.Master = 0.f;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("master byte 0"), A->GetMasterGain(), 0.f);
	TestEqual(TEXT("SFX (bank voices, board, crowd, ambient, loading-screen type sound)"), A->GetSfxGain(), 0.f);
	TestEqual(TEXT("music stream (pathfinder)"), A->GetMusicGain(), 0.f);
	TestEqual(TEXT("music class, FE table (movies, loading-screen voice)"), A->MusicClassLevel(true), 0.f);
	TestEqual(TEXT("music class, race table"), A->MusicClassLevel(false), 0.f);
	TestEqual(TEXT("PA / MC speech"), A->GetSpeechGain(0), 0.f);
	TestEqual(TEXT("character speech"), A->GetSpeechGain(1), 0.f);
	TestEqual(TEXT("jukebox"), A->GetJukeboxGain(), 0.f);
	TestEqual(TEXT("race intro stream"), A->GetIntroGain(), 0.f);
	USSXSegmentQueueWave* Q = NewObject<USSXSegmentQueueWave>(GetTransientPackage());
	Q->Init(MakeShared<FSSXSegmentQueuePlayer>(36000, 2));
	TestTrue(TEXT("speech / intro stream plays when silent"), Q->VirtualizationMode == EVirtualizationMode::PlayWhenSilent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsOptionsLiveTest, "SSX.Settings.OptionsLive", SetTestFlags)
bool FSSXSettingsOptionsLiveTest::RunTest(const FString& Parameters)
{
	FSetFe F;
	if (!F.OpenPage(0))
	{
		SSXTest::Skip(*this, TEXT("FE tables not converted (fe_tables.json): the Options flow is skipped"));
		return true;
	}
	USSXAudioSubsystem* A = NewAudio();
	F.Row(2);
	F.Press(ESSXFeKey::Left);
	TestTrue(TEXT("Left on master: settings -1/11"), FMath::IsNearlyEqual(F.Profile.Settings.Master, 9.f / 11.f, 1e-5f));
	SSXGameAudio::ApplySettings(*A, F.Profile.Settings);
	TestTrue(TEXT("master level live in the sound system"), FMath::IsNearlyEqual(A->GetMasterVolume(), 9.f / 11.f, 1e-5f));
	F.Row(2);
	F.Press(ESSXFeKey::Left);
	F.Press(ESSXFeKey::Left);
	TestTrue(TEXT("Left x2 on character speech: -2/11"), FMath::IsNearlyEqual(F.Profile.Settings.CharSpeech, 8.f / 11.f, 1e-5f));
	SSXGameAudio::ApplySettings(*A, F.Profile.Settings);
	TestTrue(TEXT("character speech live (class 3)"), FMath::IsNearlyEqual(A->GetCharSpeechVolume(), 8.f / 11.f, 1e-5f));
	F.Row(1);
	for (int32 I = 0; I < 11; ++I) F.Press(ESSXFeKey::Left);
	SSXGameAudio::ApplySettings(*A, F.Profile.Settings);
	TestEqual(TEXT("music bar at 0: music class 0"), A->GetMusicVolume(), 0.f);
	for (int32 I = 0; I < 4; ++I) F.Press(ESSXFeKey::Up);
	F.Press(ESSXFeKey::Left);
	TestEqual(TEXT("preset No Sound"), F.Profile.Settings.Preset, 5);
	SSXGameAudio::ApplySettings(*A, F.Profile.Settings);
	TestEqual(TEXT("No Sound: SFX 0"), A->GetSfxVolume(), 0.f);
	TestEqual(TEXT("No Sound: PA speech 0"), A->GetSpeechVolume(), 0.f);
	TestEqual(TEXT("No Sound: character speech 0"), A->GetCharSpeechVolume(), 0.f);
	TestEqual(TEXT("No Sound: arcade 0 (0x212270(snd, 0))"), A->ArcadeSfx, 0);
	TestTrue(TEXT("master untouched by a preset"), FMath::IsNearlyEqual(A->GetMasterVolume(), 9.f / 11.f, 1e-5f));
	F.Press(ESSXFeKey::Right);
	SSXGameAudio::ApplySettings(*A, F.Profile.Settings);
	TestTrue(TEXT("Tasty Mix: music 10/11"), FMath::IsNearlyEqual(A->GetMusicVolume(), On, 1e-5f));
	TestEqual(TEXT("Tasty Mix: arcade 1"), A->ArcadeSfx, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsPauseOptionsTest, "SSX.Settings.PauseOptions", SetTestFlags)
bool FSSXSettingsPauseOptionsTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCheats C;
	FSSXRaceUIHostStub H;
	H.Phase = 4;
	FSSXOverlayManager O;
	O.Init(&P, &C, &SetLoc());
	O.SetHost(&H);
	O.Pad(0, PadStart);
	O.Pad(0, PadDown); O.Pad(0, PadDown);
	O.Pad(0, PadCross);
	if (!TestEqual(TEXT("pause Options page"), O.PauseDepth, 1)) return false;
	O.Pad(0, PadDown);
	O.Pad(0, PadRight);
	TestEqual(TEXT("HUD detail minimal stored in 0x32CA40"), P.Settings.HudDetail, 1);
	FSSXHudInput In;
	In.ApplySettings(P.Settings);
	TestEqual(TEXT("the HUD reads it"), In.HudDetail, 1);
	O.Pad(0, PadDown);
	O.Pad(0, PadRight);
	TestEqual(TEXT("1P camera index stored in 0x32CA38"), P.Settings.Camera1P, 2);
	O.Pad(0, PadLeft); O.Pad(0, PadLeft);
	TestEqual(TEXT("1P camera Left x2"), P.Settings.Camera1P, 0);
	FSSXSaveData R;
	TestTrue(TEXT("saved"), R.FromBlob(P.SaveBlob()) && R.Settings.Camera1P == 0 && R.Settings.HudDetail == 1);
	O.Pad(0, PadDown); O.Pad(0, PadDown);
	USSXAudioSubsystem* A = NewAudio();
	O.Pad(0, PadLeft);
	TestTrue(TEXT("master -1/11"), FMath::IsNearlyEqual(P.Settings.Master, 9.f / 11.f, 1e-5f));
	SSXGameAudio::TickRaceOverlays(*A, O);
	TestTrue(TEXT("master live"), FMath::IsNearlyEqual(A->GetMasterVolume(), 9.f / 11.f, 1e-5f));
	for (int32 I = 0; I < 4; ++I) O.Pad(0, PadDown);
	O.Pad(0, PadLeft);
	SSXGameAudio::TickRaceOverlays(*A, O);
	TestTrue(TEXT("character speech -1/11, live (class 3)"), FMath::IsNearlyEqual(A->GetCharSpeechVolume(), 9.f / 11.f, 1e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsSpeakerTest, "SSX.Settings.Speaker", SetTestFlags)
bool FSSXSettingsSpeakerTest::RunTest(const FString& Parameters)
{
	USSXAudioSubsystem* A = NewAudio();
	FSSXSettings S;
	S.Speaker = 2;
	SSXGameAudio::ApplySettings(*A, S);
	const int32 R0 = A->GetSpeakerRestarts();
	TestEqual(TEXT("speaker 2 in the sound system"), A->GetSpeaker(), 2);
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("same speaker every frame: no rebuild"), A->GetSpeakerRestarts(), R0);
	S.Speaker = 0;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("mono: one rebuild"), A->GetSpeakerRestarts(), R0 + 1);
	TestEqual(TEXT("mono in the sound system (3D angles zeroed by the board sounds)"), A->GetSpeaker(), 0);
	S.Speaker = 3;
	SSXGameAudio::ApplySettings(*A, S);
	TestEqual(TEXT("DTS: QuadMenu speaker"), A->GetSpeaker(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsWidescreenTest, "SSX.Settings.Widescreen", SetTestFlags)
bool FSSXSettingsWidescreenTest::RunTest(const FString& Parameters)
{
	using namespace SSXSettings;
	const FWidescreenView Off = WidescreenView(0), W = WidescreenView(1), An = WidescreenView(2), Bad = WidescreenView(7);
	TestTrue(TEXT("Off 1 / 1 / 0 / 1"), Off.XScale == 1.f && Off.YScale == 1.f && Off.ViewTop == 0.f && Off.ViewHeight == 1.f);
	TestTrue(TEXT("16:9 0.75 / 0.75 / 0.125 / 0.75"), W.XScale == 0.75f && W.YScale == 0.75f && W.ViewTop == 0.125f && W.ViewHeight == 0.75f);
	TestTrue(TEXT("Animorphic 0.75 / 1 / 0 / 1"), An.XScale == 0.75f && An.YScale == 1.f && An.ViewTop == 0.f && An.ViewHeight == 1.f);
	TestTrue(TEXT("other modes: not scaled"), Bad.XScale == 1.f && Bad.YScale == 1.f);
	TestEqual(TEXT("Off: 4:3 image"), ImageAspect(0), 4.f / 3.f, 1e-5f);
	TestEqual(TEXT("16:9: 16:9 image"), ImageAspect(1), 16.f / 9.f, 1e-5f);
	TestEqual(TEXT("Animorphic: 16:9 image"), ImageAspect(2), 16.f / 9.f, 1e-5f);
	const double Half = 0.825;
	TestEqual(TEXT("Off: horizontal FOV = 2 x half"), HorizontalFov(Half, 0), 2.0 * Half, 1e-9);
	TestEqual(TEXT("16:9: x tangent / 0.75"), FMath::Tan(HorizontalFov(Half, 1) * 0.5), FMath::Tan(Half) / 0.75, 1e-9);
	const double VOff = FMath::Tan(HorizontalFov(Half, 0) * 0.5) / ImageAspect(0);
	const double VWide = FMath::Tan(HorizontalFov(Half, 1) * 0.5) / ImageAspect(1);
	TestEqual(TEXT("16:9 keeps the vertical FOV (Hor+)"), VWide, VOff, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsBrightnessTest, "SSX.Settings.Brightness", SetTestFlags)
bool FSSXSettingsBrightnessTest::RunTest(const FString& Parameters)
{
	FSSXSettings S;
	S.Brightness = 0.f;
	TestEqual(TEXT("bar 0 -> x0.5"), SSXProg::BrightnessScale(S), 0.5f);
	S.Brightness = 1.f;
	TestEqual(TEXT("bar 1 -> x1.5"), SSXProg::BrightnessScale(S), 1.5f);
	S.Brightness = 2.f;
	TestEqual(TEXT("clamped at 2"), SSXProg::BrightnessScale(S), 2.f);
	UMaterialParameterCollection* Mpc = LoadObject<UMaterialParameterCollection>(nullptr, USSXWorldPresentation::CollectionPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!Mpc || !Mpc->GetScalarParameterByName(TEXT("Brightness")))
	{
		SSXTest::Skip(*this, TEXT("MPC_SSXWorld has no Brightness (run Scripts/make_world_materials.py): display check skipped"));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXSettingsBrightnessWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	S.Brightness = 0.8f;
	SSXSettings::ApplyBrightness(World, SSXProg::BrightnessScale(S));
	float V = 0.f;
	UMaterialParameterCollectionInstance* Inst = World->GetParameterCollectionInstance(Mpc);
	TestTrue(TEXT("MPC Brightness set"), Inst && Inst->GetScalarParameterValue(TEXT("Brightness"), V));
	TestEqual(TEXT("display factor 1.3"), V, 1.3f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsRacerCountTest, "SSX.Settings.RacerCount", SetTestFlags)
bool FSSXSettingsRacerCountTest::RunTest(const FString& Parameters)
{
	using namespace SSXSettings;
	for (int32 M = 2; M <= 6; ++M)
	{
		TestEqual(*FString::Printf(TEXT("single event race, max boarders %d"), M), RacerCount(2, 1, M), M);
	}
	TestEqual(TEXT("single event showoff: the humans (0x165558 case 3 -> 0x1664B0)"), RacerCount(3, 1, 4), 1);
	TestEqual(TEXT("single event showoff 2P: both humans"), RacerCount(3, 2, 6), 2);
	TestEqual(TEXT("knockdown: 6 (0x165558 case 0)"), RacerCount(0, 1, 3), 6);
	TestEqual(TEXT("freeride: humans"), RacerCount(1, 1, 6), 1);
	TestEqual(TEXT("timed race: humans"), RacerCount(7, 1, 6), 1);
	TestEqual(TEXT("tutorial: 1"), RacerCount(6, 1, 6), 1);
	TestEqual(TEXT("WC race: the heat grid (6)"), RacerCount(4, 1, 2), 6);
	TestEqual(TEXT("WC showoff: 1 (0x16D230)"), RacerCount(5, 1, 6), 1);
	TestEqual(TEXT("next Race: max boarders"), NextEventRacerCount(2, 1, 3), 3);
	TestEqual(TEXT("next Showoff: humans"), NextEventRacerCount(3, 1, 3), 1);
	TestEqual(TEXT("next TC: humans"), NextEventRacerCount(7, 1, 3), 1);
	TestEqual(TEXT("launch with a next-event count"), LaunchRacerCount(3, 1, 5, 1), 1);
	TestEqual(TEXT("FE launch of a race (no next-event count)"), LaunchRacerCount(2, 1, 5, -1), 5);
	TestEqual(TEXT("FE launch of a showoff (no next-event count)"), LaunchRacerCount(3, 1, 5, -1), 1);
	TestEqual(TEXT("WC heat grid overrides a next-event count"), LaunchRacerCount(4, 1, 3, 3), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsPadLayoutTest, "SSX.Settings.PadLayout", SetTestFlags)
bool FSSXSettingsPadLayoutTest::RunTest(const FString& Parameters)
{
	FSSXSettings S;
	TestEqual(TEXT("Default -> BTNMAP0"), SSXSettings::PadLayout(S, 0, 2), 0);
	S.Controller[0] = 1;
	TestEqual(TEXT("Pro -> BTNMAP1"), SSXSettings::PadLayout(S, 0, 2), 1);
	TestEqual(TEXT("tutorial always BTNMAP0"), SSXSettings::PadLayout(S, 0, 6), 0);
	TestEqual(TEXT("port 1 uses its own word"), SSXSettings::PadLayout(S, 1, 2), 0);
	FPadMapper M;
	M.SetLayout(SSXSettings::PadLayout(S, 0, 2));
	TestEqual(TEXT("mapper takes the layout"), M.GetLayout(), 1);
	const uint32 Abort = 1u << (FPadMapper::ActJumpAbort - FPadMapper::ActJump);
	TestTrue(TEXT("BTNMAP0: L3 holds AnticAbort"), (FPadMapper::BoolMask(0x4000, 0) & Abort) != 0);
	TestTrue(TEXT("BTNMAP1: L3 does not"), (FPadMapper::BoolMask(0x4000, 1) & Abort) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsUnitsTest, "SSX.Settings.Units", SetTestFlags)
bool FSSXSettingsUnitsTest::RunTest(const FString& Parameters)
{
	static FSSXHudData D;
	static bool bTried = false;
	if (!bTried) { bTried = true; D.Load(); }
	FSSXSettings S;
	FSSXHudInput In;
	S.Units = 0;
	In.ApplySettings(S);
	TestFalse(TEXT("Units 0: km/h"), In.bMph);
	S.Units = 1;
	In.ApplySettings(S);
	TestTrue(TEXT("Units 1: mph"), In.bMph);
	if (!D.IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("no HUD data (tools/export_hud_data.py): the speed text check is skipped"));
		return true;
	}
	FSSXHudOverlay H;
	H.Init(&D, &SetLoc());
	H.Activate(4, 0, 6, 0, nullptr, 0.0);
	In.Mode = 4;
	In.RacerCount = 6;
	In.Players.AddDefaulted();
	In.Players[0].Speed = 1000.0;
	S.Units = 0;
	In.ApplySettings(S);
	H.Update(In);
	FSSXFeDrawList Kmh;
	H.Draw(Kmh, In);
	TestTrue(TEXT("km/h: v x 0.036 (single precision)"), Kmh.Texts.Contains(FString::FromInt((int32)(1000.f * 0.036f))) && Kmh.Texts.Contains(D.UnitKmh));
	S.Units = 1;
	In.ApplySettings(S);
	FSSXFeDrawList Mph;
	H.Draw(Mph, In);
	TestTrue(TEXT("mph: 22 mph (v x 0.0223694)"), Mph.Texts.Contains(TEXT("22")) && Mph.Texts.Contains(D.UnitMph));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsAutoloadTest, "SSX.Settings.Autoload", SetTestFlags)
bool FSSXSettingsAutoloadTest::RunTest(const FString& Parameters)
{
	FSSXProfile Saved = FSSXProfile::MakeDefault();
	Saved.Settings.Units = 0;
	Saved.Settings.MaxBoarders = 3;
	Saved.Settings.Autoload = 1;
	{
		FSSXProfile P = FSSXProfile::MakeDefault();
		TestTrue(TEXT("auto load on: the file is loaded"), P.BootLoadBlob(Saved.SaveBlob()));
		TestEqual(TEXT("loaded units"), P.Settings.Units, 0);
		TestEqual(TEXT("loaded max boarders"), P.Settings.MaxBoarders, 3);
	}
	Saved.Settings.Autoload = 0;
	{
		FSSXProfile P = FSSXProfile::MakeDefault();
		TestFalse(TEXT("auto load off: not loaded (0x1845E0)"), P.BootLoadBlob(Saved.SaveBlob()));
		TestEqual(TEXT("only auto load = 0 kept"), P.Settings.Autoload, 0);
		TestEqual(TEXT("units stay the start-up value"), P.Settings.Units, 1);
		TestEqual(TEXT("max boarders stay 6"), P.Settings.MaxBoarders, 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSettingsArcadePersistTest, "SSX.Settings.ArcadePersist", SetTestFlags)
bool FSSXSettingsArcadePersistTest::RunTest(const FString& Parameters)
{
	// 0x29F178, 0x32CA04, 0x161928, 0x2912E8, 0x161A98
	FSSXMemCard::RootOverride() = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("MemCardArcade"));
	const FString Path = FSSXMemCard::FilePath(FSSXMemCard::SettingsName());
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FSSXProfile Old = FSSXProfile::MakeDefault();
	Old.Settings.ArcadeSfx = 0;
	TestTrue(TEXT("card written with arcade off"), FFileHelper::SaveArrayToFile(Old.SaveBlob(), *Path));
	auto BootArcade = [&Path]()
	{
		TArray<uint8> Blob;
		FSSXProfile P = FSSXProfile::MakeDefault();
		return FFileHelper::LoadFileToArray(Blob, *Path) && P.BootLoadBlob(Blob) ? P.Settings.ArcadeSfx : -1;
	};
	TestEqual(TEXT("boot auto-load: arcade 0 from the card"), BootArcade(), 0);
	FSetFe F;
	F.Profile.Settings.ArcadeSfx = 0;
	if (!F.OpenPage(0))
	{
		SSXTest::Skip(*this, TEXT("FE tables not converted (fe_tables.json): the Options flow is skipped"));
		FSSXMemCard::RootOverride().Reset();
		return true;
	}
	F.Row(7);
	for (int32 I = 0; I < 2 && F.Profile.Settings.ArcadeSfx == 0; ++I) { F.Press(ESSXFeKey::Left); }
	TestEqual(TEXT("Arcade SFX On: live at once (vslot 0x1BC)"), F.Profile.Settings.ArcadeSfx, 1);
	F.Press(ESSXFeKey::Down);
	F.Press(ESSXFeKey::Cross);
	TestEqual(TEXT("DONE keeps arcade 1"), F.Profile.Settings.ArcadeSfx, 1);
	TestFalse(TEXT("no dirty flag (0x161928: word == snd+0x3844)"), F.Profile.bSettingsDirty);
	TestEqual(TEXT("no autosave: the card still holds 0, so the next boot is Off again"), BootArcade(), 0);
	// 0x28F260
	FSSXMemCard M;
	M.Begin(FSSXMemCard::EMode::Game, true, &F.Profile);
	for (int32 N = 0; N < 400 && M.State != FSSXMemCard::SaveList; ++N) { M.Tick(); }
	M.Input(FSSXMemCard::InAccept);
	M.Input(FSSXMemCard::InAccept);
	for (int32 N = 0; N < 400 && M.State != FSSXMemCard::SaveDone && M.State != FSSXMemCard::SaveFailed; ++N) { M.Tick(); }
	TestEqual(TEXT("save done"), int32(M.State), int32(FSSXMemCard::SaveDone));
	TestEqual(TEXT("after a memory-card save the boot loads arcade 1"), BootArcade(), 1);
	IFileManager::Get().DeleteDirectory(*FSSXMemCard::RootDir(), false, true);
	FSSXMemCard::RootOverride().Reset();
	return true;
}

#endif
