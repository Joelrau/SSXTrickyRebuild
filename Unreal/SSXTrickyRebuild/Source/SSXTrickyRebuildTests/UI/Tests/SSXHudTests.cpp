#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Engine/Texture2D.h"
#include "Camera/CameraDefs.h"
#include "Camera/RaceCamera.h"
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Progression/SSXProgression.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXHudData.h"
#include "UI/SSXHudOverlay.h"
#include "UI/SSXRaceOverlays.h"
#include "UI/SSXRaceUIHost.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HudTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	const FSSXHudData* HudData(FAutomationTestBase& T)
	{
		static FSSXHudData D;
		static bool bTried = false;
		if (!bTried) { bTried = true; D.Load(); }
		if (!D.IsLoaded()) T.AddError(FString::Printf(TEXT("no HUD data in %s (run tools/export_hud_data.py)"), *FSSXHudData::DefaultDir()));
		return D.IsLoaded() ? &D : nullptr;
	}

	const FSSXLocStrings& Loc()
	{
		static FSSXLocStrings L;
		static bool bOnce = false;
		if (!bOnce) { bOnce = true; L.Load(SSXFrontEnd::LangDir()); }
		return L;
	}

	struct FHudAssets
	{
		FSSXHudSheet Sheet;
		FSSXFeAssets Assets;
		bool Make()
		{
			if (!Sheet.Load(FPaths::Combine(FSSXHudData::DefaultDir(), TEXT("hud_sprites.json")), false)) return false;
			Assets.HudSprites = &Sheet.Sprites;
			for (const TPair<FName, FString>& P : Sheet.PagePaths)
			{
				Assets.Pages.Add(P.Key, UTexture2D::CreateTransient(1, 1));
			}
			return true;
		}
	};

	FSSXHudInput OnePlayer(int32 Mode = 4)
	{
		FSSXHudInput In;
		In.Mode = Mode;
		In.RacerCount = 6;
		In.Players.AddDefaulted();
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudMaskTest, "SSX.HUD.Masks", HudTestFlags)
bool FSSXHudMaskTest::RunTest(const FString& Parameters)
{
	uint32 Min, Full, None;
	FSSXHudOverlay::BuildMasks(4, 0, 6, 0, Min, Full, None);
	TestEqual(TEXT("full = RAM 0x5E43BBC9"), Full, 0x5E43BBC9u);
	TestEqual(TEXT("minimal = RAM 0x1E41B209"), Min, 0x1E41B209u);
	TestEqual(TEXT("none = RAM 0x2000000"), None, 0x2000000u);
	FSSXHudOverlay::BuildMasks(8, 0, 1, 0, Min, Full, None);
	TestEqual(TEXT("demo: all 0x80000 | 0x2000000"), Full, 0x2080000u);
	FSSXHudOverlay::BuildMasks(1, 8, 1, 0, Min, Full, None);
	TestEqual(TEXT("freeride lapped: no strip/rank, knockdown on"), Full & 0x10000300u, 0x10000000u);
	FSSXHudOverlay::BuildMasks(7, 0, 6, 0, Min, Full, None);
	TestEqual(TEXT("timed race: no strip"), Full & 0x100u, 0u);
	TestEqual(TEXT("timed race: time to beat"), Full & 0x20000000u, 0x20000000u);
	FSSXHudOverlay::BuildMasks(6, 0, 1, 0, Min, Full, None);
	TestEqual(TEXT("lesson: no camera icon"), Full & 0x40000000u, 0u);
	TestEqual(TEXT("layout 1P"), FSSXHudOverlay::LayoutFor(0, 1), 0);
	TestEqual(TEXT("layout 2P bottom"), FSSXHudOverlay::LayoutFor(1, 2), 2);
	TestEqual(TEXT("pop at p=1/8"), FSSXHudOverlay::Pop(0.125f, 1.4f), FMath::Sin(PI * 0.5f) * 0.4375f * 1.4f, 1e-5f);
	TestEqual(TEXT("pop after 0.25"), FSSXHudOverlay::Pop(0.3f, 1.4f), 0.f);
	TestEqual(TEXT("time format"), FSSXHudOverlay::FormatTime(9512), FString(TEXT("1:35.12")));
	TestEqual(TEXT("time cap"), FSSXHudOverlay::FormatTime(400000), FString(TEXT("59:59.99")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudDataTest, "SSX.HUD.Data", HudTestFlags)
bool FSSXHudDataTest::RunTest(const FString& Parameters)
{
	const FSSXHudData* D = HudData(*this);
	if (!D) return false;
	TestEqual(TEXT("L[14] x"), D->L(0, 14).X, 570.f);
	TestEqual(TEXT("L[14] y"), D->L(0, 14).Y, 140.f);
	TestEqual(TEXT("L[30] flags"), D->L(0, 30).Flags, 0xA0u);
	TestEqual(TEXT("L[1] scale"), D->L(0, 1).Sx, 1.9f);
	TestEqual(TEXT("boost seg 0 y"), D->Boost[0].Dy, 288.f);
	TestEqual(TEXT("boost seg 0 sprite"), D->Boost[0].Sprite, 0x14);
	TestEqual(TEXT("boost seg 13 sprite"), D->Boost[13].Sprite, 0x12);
	TestEqual(TEXT("boost lo"), D->Boost[5].Lo, 0.2f);
	TestEqual(TEXT("letter 0 rec / dark / lit"), D->Letters[0].Rec * 10000 + D->Letters[0].Dark * 100 + D->Letters[0].Lit, 24 * 10000 + 0x19 * 100 + 0x1F);
	TestFalse(TEXT("unit strings"), D->UnitKmh.IsEmpty() || D->UnitMph.IsEmpty());
	// Field 7 (w1>>8 & 15) then field 8 (w0 byte 0), in sprintf order.
	const FString Name = D->ComposeTrickName(3u, 2u << 8);
	TestEqual(TEXT("flip + named trick"), Name, D->Slot(0x3209D8 + 8) + D->Slot(0x3209F0 + 12));
	TestFalse(TEXT("named trick non-empty"), D->Slot(0x3209F0 + 12).IsEmpty());
	TestEqual(TEXT("field 2 overflow"), D->ComposeTrickName(0u, 13u << 4), D->Slot(0x320B50 + 4));
	TestEqual(TEXT("empty record"), D->ComposeTrickName(0u, 0u), FString());
	FHudAssets A;
	TestTrue(TEXT("sprite sheet"), A.Make());
	const FSSXSprite* Corner = A.Sheet.Sprites.Find(0x37);
	TestTrue(TEXT("corner 0x37 is 256x33"), Corner && Corner->W == 256.f && Corner->H == 33.f);
	TestEqual(TEXT("three pages"), A.Sheet.PagePaths.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudBoostTest, "SSX.HUD.Boost", HudTestFlags)
bool FSSXHudBoostTest::RunTest(const FString& Parameters)
{
	const FSSXHudData* D = HudData(*this);
	FHudAssets A;
	if (!D || !A.Make()) return false;
	FSSXHudOverlay H;
	H.Init(D, &Loc());
	H.Activate(4, 0, 6, 0, nullptr, 0.0);
	FSSXHudInput In = OnePlayer();
	In.Players[0].BoostMeter = 0.5;
	H.Update(In);
	TestEqual(TEXT("slew 0.005 per frame"), H.GetPlayer(0).BoostShown, 0.005f, 1e-6f);
	for (int32 I = 0; I < 200; ++I) H.Update(In);
	const FSSXHudOverlay::FPlayerState& S = H.GetPlayer(0);
	TestEqual(TEXT("reached"), S.BoostShown, 0.5f, 1e-5f);
	int32 Lit = 0;
	for (int32 K = 0; K < 14; ++K) Lit += S.SegLit[K] ? 1 : 0;
	TestEqual(TEXT("8 of 14 lit (7 full + the float remainder)"), Lit, 8);
	TestEqual(TEXT("lit scale 1"), S.SegScale[0], 1.f, 1e-5f);
	TestEqual(TEXT("unlit scale 0.2"), S.SegScale[13], 0.2f, 1e-5f);
	FSSXFeDrawList DL(&A.Assets);
	In.HudDetail = 1;
	H.Draw(DL, In);
	const FSSXSprite* Seg = A.Sheet.Sprites.Find(0x14);
	bool bFound = false;
	for (const FSSXFeDrawCmd& C : DL.Cmds)
	{
		if (Seg && FMath::IsNearlyEqual(C.Size.X, Seg->W) && FMath::IsNearlyEqual(C.Pos.Y + C.Size.Y * 0.5f, 428.f, 0.01f)
			&& FMath::IsNearlyEqual(C.Pos.X + C.Size.X * 0.5f, 570.f, 0.01f)) bFound = true;
	}
	TestTrue(TEXT("segment 0 at (570, 428)"), bFound);
	// 0x1A27D0 / 0x1A2A00 / 0x1A77A8
	FSSXFeDrawList Dimmed(&A.Assets);
	In.bModal = true;
	In.bDimOverlay = true;
	H.Draw(Dimmed, In);
	TestEqual(TEXT("drawn (dimmed) under the pause menu"), Dimmed.Cmds.Num() + Dimmed.Texts.Num(), DL.Cmds.Num() + DL.Texts.Num());
	bool bHalf = false;
	for (const FSSXFeDrawCmd& C : Dimmed.Cmds) { bHalf |= FMath::IsNearlyEqual(C.Color.A, 0.5f, 0.01f); }
	TestTrue(TEXT("0x1A77A8 (+0x10 overlay up): pass 1 at alpha 0.5"), bHalf);
	In.bModal = false;
	In.bDimOverlay = false;
	FSSXFeDrawList Hidden(&A.Assets);
	In.bReplayActive = true;
	H.Draw(Hidden, In);
	TestEqual(TEXT("hidden during replay playback"), Hidden.Cmds.Num() + Hidden.Texts.Num(), 0);
	In.bReplayActive = false;
	FSSXFeDrawList Saving(&A.Assets);
	In.bCardSave = true;
	H.Draw(Saving, In);
	TestEqual(TEXT("hidden during the quit prompt's card save"), Saving.Cmds.Num() + Saving.Texts.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudTextTest, "SSX.HUD.Text", HudTestFlags)
bool FSSXHudTextTest::RunTest(const FString& Parameters)
{
	const FSSXHudData* D = HudData(*this);
	if (!D) return false;
	FSSXHudOverlay H;
	H.Init(D, &Loc());
	H.Activate(4, 0, 6, 0, nullptr, 0.0);
	FSSXHudInput In = OnePlayer();
	In.RaceTimeSec = 95.12;
	In.Players[0].Rank = 1;
	In.Players[0].Speed = 1000.0;
	H.Update(In);
	FSSXFeDrawList DL;
	H.Draw(DL, In);
	TestTrue(TEXT("rank 2"), DL.Texts.Contains(TEXT("2")));
	TestTrue(TEXT("suffix nd (LOC 0xE54)"), DL.Texts.Contains(Loc().ByIndex(0xE54)));
	TestTrue(TEXT("/N (medal bit 0x1000000 is off in race modes)"), DL.Texts.Contains(TEXT("/6")));
	TestTrue(TEXT("speed km/h"), DL.Texts.Contains(FString::FromInt((int32)(1000.f * 0.036f))) && DL.Texts.Contains(D->UnitKmh));
	TestTrue(TEXT("race time"), DL.Texts.Contains(TEXT("1:35.12")));
	In.bMph = true;
	FSSXFeDrawList Mph;
	H.Draw(Mph, In);
	TestTrue(TEXT("speed 22 mph"), Mph.Texts.Contains(TEXT("22")) && Mph.Texts.Contains(D->UnitMph));

	// 0x100000
	H.Activate(5, 8, 6, 0, nullptr, 0.0);
	In.Mode = 5;
	In.Track = 8;
	In.Players[0].Messages = { { SSXHudMsg::Laps, 2, 100, 10, 0 }, { SSXHudMsg::Multiplier, 3, 100, 10, 0 }, { SSXHudMsg::TrickLanded, 0, 100, 10, 0 } };
	In.Players[0].TrickText = D->ComposeTrickName(3u, 2u << 8).TrimEnd();
	FSSXFeDrawList Q;
	H.Draw(Q, In);
	TestTrue(TEXT("FINAL LAP (LOC 0x115)"), Q.Texts.Contains(Loc().ByIndex(0x115)));
	TestTrue(TEXT("X3"), Q.Texts.Contains(TEXT("X3")));
	TestTrue(TEXT("trick name drawn"), Q.Texts.ContainsByPredicate([&](const FString& T) { return !T.IsEmpty() && In.Players[0].TrickText.Contains(T); }));
	In.Players[0].Messages = { { SSXHudMsg::Laps, 3, 100, 10, 0 } };
	FSSXFeDrawList Q2;
	H.Draw(Q2, In);
	TestTrue(TEXT("2 LAPS TO GO (LOC 0x116)"), Q2.Texts.Contains(Loc().ByIndex(0x116).Replace(TEXT("%d"), TEXT("2"))));
	return true;
}

// 0x1A4F70
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudMedalTargetTest, "SSX.HUD.MedalTarget", HudTestFlags)
bool FSSXHudMedalTargetTest::RunTest(const FString& Parameters)
{
	const FSSXHudData* D = HudData(*this);
	if (!D) return false;
	const FSSXLocStrings& L = Loc();
	FSSXHudOverlay H;
	H.Init(D, &L);
	H.Activate(3, 6, 1, 0, nullptr, 0.0);
	FSSXHudInput In = OnePlayer(3);
	In.Track = 6;
	In.RacerCount = 1;
	auto DrawTexts = [&]() { FSSXFeDrawList DL; H.Draw(DL, In); return DL.Texts; };
	auto Run = [&](int32 Frames) { for (int32 F = 0; F < Frames; ++F) H.Update(In); };

	Run(1);
	TArray<FString> T = DrawTexts();
	TestTrue(TEXT("tier 0: BRONZE (0xE32)"), T.Contains(L.ByIndex(0xE32)));
	TestTrue(TEXT("tier 0: bronze target 250000"), T.Contains(TEXT("250000")));
	TestFalse(TEXT("no rank with one racer"), T.Contains(L.ByIndex(0xE44)));

	In.Players[0].Score = 260000.0;
	Run(1);
	T = DrawTexts();
	TestTrue(TEXT("pop: BRONZE"), T.Contains(L.ByIndex(0xE32)));
	TestTrue(TEXT("pop: MEDAL (0xE28)"), T.Contains(L.ByIndex(0xE28)));
	TestFalse(TEXT("pop: no target number"), T.Contains(TEXT("500000")));
	Run(130);
	T = DrawTexts();
	TestTrue(TEXT("tier 1: FOR SILVER (0xE26)"), T.Contains(L.ByIndex(0xE26)));
	TestTrue(TEXT("tier 1: silver target"), T.Contains(TEXT("500000")));

	In.Players[0].Score = 900000.0;
	Run(1);
	T = DrawTexts();
	TestTrue(TEXT("pop: SILVER (0xE3B)"), T.Contains(L.ByIndex(0xE3B)));
	Run(130);
	T = DrawTexts();
	TestTrue(TEXT("pop: GOLD (0xE35)"), T.Contains(L.ByIndex(0xE35)));
	Run(130);
	T = DrawTexts();
	TestFalse(TEXT("tier 3 at rest: no target text"), T.Contains(L.ByIndex(0xE25)) || T.Contains(L.ByIndex(0xE35)) || T.Contains(TEXT("800000")));

	FSSXHudOverlay H2;
	H2.Init(D, &L);
	H2.Activate(3, 6, 2, 0, nullptr, 0.0);
	FSSXHudInput In2 = OnePlayer(3);
	In2.Track = 6;
	In2.RacerCount = 2;
	H2.Update(In2);
	FSSXFeDrawList DL2;
	H2.Draw(DL2, In2);
	TestFalse(TEXT("2 racers: no medal target"), DL2.Texts.Contains(TEXT("250000")));
	return true;
}

// 5. Progress-strip course markers on Garibaldi vs the session-1 RAM (finish 0; 345272, 205616, 65771 cm).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudMarkerTest, "SSX.HUD.Markers", HudTestFlags)
bool FSSXHudMarkerTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("gari race data not available: %s"), *Err));
		return false;
	}
	TArray<FSSXHudMarker> M;
	FSSXHudOverlay::BuildMarkers(Data.PathsFor(false), Data.TrackId, 0.0, M);
	FString List;
	for (const FSSXHudMarker& K : M) List += FString::Printf(TEXT("(%d %.0f) "), K.Kind, K.Dist);
	AddInfo(List);
	TestTrue(TEXT("first = finish at 0"), M.Num() > 0 && M[0].Kind == 1 && M[0].Dist == 0.0);
	for (const double Want : { 345272.0, 205616.0, 65771.0 })
	{
		TestTrue(FString::Printf(TEXT("checkpoint near %.0f"), Want),
			M.ContainsByPredicate([Want](const FSSXHudMarker& K) { return K.Kind == 0 && FMath::Abs(K.Dist - Want) < 100.0; }));
	}
	TestEqual(TEXT("4 markers as in RAM"), M.Num(), 4);
	return true;
}

// 0x1A2C40 / 0x1A4F70 / FUN_001FC178 / FUN_001A77F8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudStripTest, "SSX.HUD.Strip", HudTestFlags)
bool FSSXHudStripTest::RunTest(const FString& Parameters)
{
	const FSSXHudData* D = HudData(*this);
	FHudAssets A;
	FRaceTrackData Data;
	FString Err;
	if (!D || !A.Make() || !Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("HUD / gari data not available %s"), *Err));
		return false;
	}
	FSSXHudOverlay H;
	H.Init(D, &Loc());
	H.Activate(4, Data.TrackId, 6, 0, &Data.PathsFor(false), 0.0);
	FSSXHudInput In = OnePlayer();
	In.Players[0].Racer = 4;
	In.Players[0].FinishResult = -1;
	// the player 0.3 x 20000 cm behind the checkpoint at ~65771 (the marker 90 px below the centre), the others far away
	In.RacerDist = { 900000.0, 900000.0, 900000.0, 900000.0, 59771.0, 900000.0 };
	In.RacerComputer = { true, true, true, true, false, true };
	for (int32 I = 0; I < 900; ++I) H.Update(In);
	int32 Cp = INDEX_NONE;
	for (int32 K = 0; K < H.GetMarkers().Num(); ++K)
	{
		if (H.GetMarkers()[K].Kind == 0 && FMath::Abs(H.GetMarkers()[K].Dist - 65771.0) < 100.0) Cp = K;
	}
	if (!TestTrue(TEXT("checkpoint ~65771"), Cp != INDEX_NONE)) return false;
	const float U = H.GetPlayer(0).MarkerD[Cp] / 20000.f;
	TestEqual(TEXT("slewed to the offset"), U, float((H.GetMarkers()[Cp].Dist - 59771.0) / 20000.0), 1e-3f);

	FSSXFeDrawList DL(&A.Assets);
	H.Draw(DL, In);
	int32 Cap = 0, CapTurned = 0, Mk = 0, MkMirror = 0;
	float MkAlpha = -1.f;
	for (const FSSXFeDrawCmd& C : DL.Cmds)
	{
		if (C.SpriteId == 9) { ++Cap; CapTurned += (C.UV0.X > C.UV1.X && C.UV0.Y > C.UV1.Y) ? 1 : 0; }
		if (C.SpriteId == 8) { ++Mk; MkMirror += C.UV0.X > C.UV1.X ? 1 : 0; MkAlpha = C.Color.A; }
	}
	TestEqual(TEXT("strip caps: two sprite-9 draws"), Cap, 2);
	TestEqual(TEXT("the lower cap (L[11] flags 0x23) mirrored in x and flipped in y"), CapTurned, 1);
	TestEqual(TEXT("layout 0: the checkpoint marker drawn twice (flags 8 and 1)"), Mk, 2);
	TestEqual(TEXT("one of the two mirrored (flag 1)"), MkMirror, 1);
	const float Au = FMath::Abs(U);
	TestEqual(TEXT("below the centre: alpha 1 - (|u| - 0.25) x 4"), MkAlpha, 1.f - (Au - 0.25f) * 4.f, 0.02f);

	// FUN_001A77F8
	const float Before = H.GetPlayer(0).MarkerD[Cp];
	In.RacerDist[4] = 50000.0;
	In.bModal = true;
	for (int32 I = 0; I < 10; ++I) H.Update(In);
	TestEqual(TEXT("modal: the strip slew stands still"), H.GetPlayer(0).MarkerD[Cp], Before);
	In.bModal = false;
	H.Update(In);
	TestNotEqual(TEXT("and runs again after it"), H.GetPlayer(0).MarkerD[Cp], Before);

	TestEqual(TEXT("FUN_001A6BA0(50000) = LOC 0x15D"), H.TimeText(50000), Loc().ByIndex(0x15D));
	TestEqual(TEXT("FUN_001A6BA0(< 0) = LOC 0x18C"), H.TimeText(-1), Loc().ByIndex(0x18C));
	TestEqual(TEXT("FUN_001A6BA0(9512)"), H.TimeText(9512), FString(TEXT("1:35.12")));
	return true;
}

// FUN_00173e70
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudPauseCameraTest, "SSX.HUD.PauseCamera", HudTestFlags)
bool FSSXHudPauseCameraTest::RunTest(const FString& Parameters)
{
	using namespace SSXFe;
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCheats C;
	FSSXOverlayManager O;
	O.Init(&P, &C, &Loc());
	FSSXRaceUIHostStub Stub;
	O.SetHost(&Stub);
	O.Pad(0, PadStart);
	O.Pad(0, PadDown); O.Pad(0, PadDown); O.Pad(0, PadCross);
	TestEqual(TEXT("options open"), O.PauseDepth, 1);
	const int32 Row = O.PauseOptionRows().IndexOfByKey(481);
	TestEqual(TEXT("1P camera row index"), Row, 2);
	for (int32 I = 0; I < Row; ++I) O.Pad(0, PadDown);
	O.Pad(0, PadRight);
	O.Pad(0, PadLeft);
	O.Pad(0, PadLeft);
	TestEqual(TEXT("three camera calls"), Stub.CameraSteps.Num(), 3);
	TestTrue(TEXT("+1, -1, -1 on player 0"), Stub.CameraSteps.Num() == 3 && Stub.CameraSteps[0] == 1 && Stub.CameraSteps[1] == -1 && Stub.CameraSteps[2] == -1);
	TestEqual(TEXT("view wraps 1 -> 2 -> 1 -> 0"), Stub.GetCameraView(0), 0);
	TestFalse(TEXT("camera does not dirty the audio settings"), P.bSettingsDirty);
	FSSXRaceCamera Cam;
	Cam.SetViewIndex(0);
	Cam.CycleView(-1);
	TestEqual(TEXT("camera wraps to 3"), Cam.GetViewIndex(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudCameraScriptTest, "SSX.HUD.CameraScripts", HudTestFlags)
bool FSSXHudCameraScriptTest::RunTest(const FString& Parameters)
{
	FSSXCameraDefSet Aloha;
	if (!Aloha.LoadForTrack(TEXT("aloha")))
	{
		AddError(TEXT("no camera data (run tools/export_cameras.py)"));
		return false;
	}
	FSSXRaceCamera Cam(&Aloha);
	FSSXCameraRiderInput R;
	TestTrue(TEXT("PreLoad exists"), Cam.PlayScript(TEXT("PreLoad")));
	int32 Frames = 0;
	while (Cam.IsScriptRunning() && Frames < 2000) { Cam.Step(R); ++Frames; }
	AddInfo(FString::Printf(TEXT("aloha PreLoad ran %d frames"), Frames));
	TestTrue(TEXT("PreLoad ends at 8 s"), Frames >= 470 && Frames <= 490);
	TestTrue(TEXT("RaceStarted2 exists"), Cam.PlayScript(TEXT("RaceStarted2")));
	Cam.Step(R);
	TestEqual(TEXT("RaceStarted2: chase near (mode 4)"), Cam.GetMode(), 4);

	FSSXCameraDefSet Mixed;
	TestTrue(TEXT("commonob + alaska"), Mixed.LoadFiles({ TEXT("commonob"), TEXT("alaska") }));
	FSSXRaceCamera Cam2(&Mixed);
	TestTrue(TEXT("commonob PreLoad"), Cam2.PlayScript(TEXT("PreLoad")));
	Cam2.Step(R);
	TestEqual(TEXT("RANDOM_GOTO -> RaceInBlocks1 -> chase near"), Cam2.GetEvaluatedName(), FString(SSXCamera::ModeDefinition(4)));
	TestFalse(TEXT("script finished after RETURN"), Cam2.IsScriptRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudRaceScriptTest, "SSX.HUD.RaceFlowScripts", HudTestFlags)
bool FSSXHudRaceScriptTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("gari data not available: %s"), *Err));
		return false;
	}
	TArray<TUniquePtr<FRiderSim>> Sims;
	for (int32 I = 0; I < 2; ++I)
	{
		TUniquePtr<FRiderSim> S = MakeUnique<FRiderSim>();
		SSXRiderSetup::RegisterStates(*S, true);
		S->SetWorld(&World);
		Sims.Add(MoveTemp(S));
	}
	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::PracticeFreeride;
	TestTrue(TEXT("init"), Session.Init(Data, Cfg));
	Session.AddRacer(Sims[0].Get(), true);
	Session.AddRacer(Sims[1].Get(), false);
	TArray<FString> Played;
	int32 Busy = 30;
	Session.OnCameraScript = [&Played](int32 Racer, const TCHAR* Script) { Played.Add(FString::Printf(TEXT("%d:%s"), Racer, Script)); };
	Session.IsIntroBusy = [&Busy]() { return Busy > 0; };
	Session.Start();
	auto Step = [&]() { Session.Tick(); for (auto& S : Sims) S->Step(FRiderInputWord()); --Busy; };
	Step();
	TestEqual(TEXT("PreLoad for the human only"), Played, TArray<FString>{ TEXT("0:PreLoad") });
	TestEqual(TEXT("Null intro: placed at steps 0xE and 0x14 on the first update"), Session.GetPlacements(), 2);
	TestEqual(TEXT("intro done (step 0x15)"), Session.GetIntroStep(), SSXIntro::Done);
	int32 N = 0;
	while (Session.GetPhase() != ERacePhase::Race && N++ < 600) Step();
	TestEqual(TEXT("no further placement"), Session.GetPlacements(), 2);
	Step();
	TestTrue(TEXT("RaceStarted2 at phase 4"), Played.Contains(TEXT("0:RaceStarted2")));
	TestFalse(TEXT("not for the AI racer"), Played.Contains(TEXT("1:RaceStarted2")));
	Session.SetPhase(ERacePhase::EndRace);
	Step();
	TestEqual(TEXT("EndRace PreLoad"), Played.Last(), FString(TEXT("0:PreLoad")));
	for (int32 I = 0; I < 2; ++I) Session.DetachRacer(I);
	return true;
}

// 0x1A53A0 / 0x1A565C / FUN_001A6C90
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudTimeToBeatTest, "SSX.HUD.TimeToBeat", HudTestFlags)
bool FSSXHudTimeToBeatTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("gap format"), FSSXHudOverlay::FormatGap(19430), FString(TEXT("3:14.30")));
	TestEqual(TEXT("gap format, behind"), FSSXHudOverlay::FormatGap(-501), FString(TEXT("-0:05.01")));
	TestEqual(TEXT("gap format, cap"), FSSXHudOverlay::FormatGap(360000), FString(TEXT("59:59.99")));
	TestEqual(TEXT("gap format, 50000 is a plain time"), FSSXHudOverlay::FormatGap(50000), FString(TEXT("8:20.00")));
	const FSSXHudData* D = HudData(*this);
	if (!D) return false;
	FSSXFeAssets Assets;
	UTexture2D* Dummy = UTexture2D::CreateTransient(1, 1);
	const FString Base = FPaths::Combine(SSXFrontEnd::FontDir(), TEXT("TITLE"));
	Assets.Title = FSSXBitmapFont::Load(Base + TEXT(".json"), Dummy, FPaths::Combine(SSXFrontEnd::SfnDir(), TEXT("TITLE.SFN")));
	if (Assets.Title.IsValid()) { Assets.Title->ScaleX = SSXFe::TitleScaleX; Assets.Title->ScaleY = SSXFe::TitleScaleY; }
	if (!Assets.Title.IsValid()) { AddError(TEXT("no TITLE font")); return false; }

	FSSXHudOverlay H;
	H.Init(D, nullptr);
	H.Activate(7, 0, 1, 0, nullptr, 0.0);
	FSSXHudInput In = OnePlayer(7);
	In.RacerCount = 1;
	In.TimeToBeatCs = 24000;
	auto Find = [](const FSSXFeDrawList& DL, const FString& S) -> const FSSXFeTextRec*
	{
		for (const FSSXFeTextRec& R : DL.TextRecs) { if (R.Str == S && !R.bShadow) return &R; }
		for (const FSSXFeTextRec& R : DL.TextRecs) { if (R.Str == S) return &R; }
		return nullptr;
	};
	auto Draw = [&](FSSXFeDrawList& DL) { H.Update(In); H.Draw(DL, In); };

	In.RaceTimeSec = 45.7;
	{
		FSSXFeDrawList DL(&Assets);
		Draw(DL);
		const FSSXFeTextRec* R = Find(DL, TEXT("3:14.30"));
		TestTrue(TEXT("counts down: 4:00.00 - 0:45.70"), R != nullptr);
		if (R)
		{
			TestTrue(TEXT("green (0, 0.85, 0)"), R->Color.R == 0.f && FMath::IsNearlyEqual(R->Color.G, 0.85f, 1e-4f) && R->Color.B == 0.f);
			TestTrue(TEXT("at L[51] x 247"), FMath::IsNearlyEqual(R->Pos.X, 247.f, 1e-3f));
			TestTrue(TEXT("at L[51] y 120 - scale"), FMath::IsNearlyEqual(R->Pos.Y, 120.f - 1.2f, 1e-3f));
		}
		TestFalse(TEXT("mode 7 mask has no race time (bit 0x1)"), DL.Texts.Contains(TEXT("0:45.70")));
	}
	In.RaceTimeSec = 234.75;
	{
		FSSXFeDrawList DL(&Assets);
		Draw(DL);
		const FSSXFeTextRec* R = Find(DL, TEXT("0:05.25"));
		TestTrue(TEXT("last 10 s"), R != nullptr);
		if (R)
		{
			TestTrue(TEXT("pulse moves it left 65 x grow"), FMath::IsNearlyEqual(R->Pos.X, 247.f - 0.35f * 65.f, 1e-2f));
			TestTrue(TEXT("pulse moves it up 10 x grow + scale"), FMath::IsNearlyEqual(R->Pos.Y, 120.f - 3.5f - 1.55f, 1e-2f));
		}
	}
	In.RaceTimeSec = 240.0;
	{
		FSSXFeDrawList DL(&Assets);
		Draw(DL);
		const FSSXFeTextRec* R = Find(DL, TEXT("0:00.00"));
		TestTrue(TEXT("exactly 0: red (0.9, 0.1, 0.1)"), R && FMath::IsNearlyEqual(R->Color.R, 0.9f, 1e-4f) && FMath::IsNearlyEqual(R->Color.G, 0.1f, 1e-4f));
	}
	In.RaceTimeSec = 245.015;
	{
		FSSXFeDrawList DL(&Assets);
		Draw(DL);
		TestTrue(TEXT("behind: -0:05.01, the race goes on"), Find(DL, TEXT("-0:05.01")) != nullptr);
	}
	In.Players[0].FinishResult = 23000;
	In.RaceTimeSec = 230.0;
	{
		FSSXFeDrawList DL(&Assets);
		Draw(DL);
		TestFalse(TEXT("layout 0: gone after the finish"), DL.Texts.Contains(TEXT("0:10.00")));
	}
	return true;
}

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudUberHintTest, "SSX.HUD.UberHint", HudTestFlags)
bool FSSXHudUberHintTest::RunTest(const FString&)
{
	FSSXHudInput In;
	FSSXHudPlayerInput Pl;
	Pl.TrickyTimer = 12.0; Pl.bBeginner = true; Pl.FinishResult = -1;
	TestTrue(TEXT("shown"), FSSXHudOverlay::UberHintActive(In, Pl));
	Pl.bBeginner = false;
	TestFalse(TEXT("not a beginner"), FSSXHudOverlay::UberHintActive(In, Pl));
	Pl.bBeginner = true; Pl.TrickyTimer = 0.0;
	TestFalse(TEXT("Tricky over"), FSSXHudOverlay::UberHintActive(In, Pl));
	Pl.TrickyTimer = 3.0; Pl.FinishResult = 1;
	TestFalse(TEXT("finished"), FSSXHudOverlay::UberHintActive(In, Pl));
	Pl.FinishResult = -1; In.bLessonObject = true;
	TestFalse(TEXT("lesson object"), FSSXHudOverlay::UberHintActive(In, Pl));
	TestEqual(TEXT("fade over the last 5 s"), FSSXHudOverlay::UberHintAlpha(Pl), 0.6f);
	Pl.UberCount = 6;
	TestEqual(TEXT("infinite boost"), FSSXHudOverlay::UberHintAlpha(Pl), 1.f);
	TestTrue(TEXT("label frames 32..63"), FSSXHudOverlay::UberLabelOn(32) && FSSXHudOverlay::UberLabelOn(63));
	TestFalse(TEXT("label frames 0..31 / 64"), FSSXHudOverlay::UberLabelOn(0) || FSSXHudOverlay::UberLabelOn(31) || FSSXHudOverlay::UberLabelOn(64));
	TestEqual(TEXT("record = 0x19 + hint"), FSSXHudOverlay::UberHintRecord(3), 0x1C);

	// FUN_00164840 / 0x32B390
	TestTrue(TEXT("19 points = Rookie (8): hint"), SSXProg::RankFromPoints(19) >= 8);
	TestFalse(TEXT("20 points = Contender (7): no hint"), SSXProg::RankFromPoints(20) >= 8);
	TestEqual(TEXT("60 points = Star (5)"), SSXProg::RankFromPoints(60), 5);
	// 0x16A9D8 / 0x16AE68
	{
		const SSXRider::FRiderTable& Tab = SSXRider::FRiderTable::Shared();
		SSXRider::FPlayerOptions O;
		TestEqual(TEXT("new save: rank 9"), O.Rank, 9);
		O.Rank = SSXProg::RankFromPoints(60);
		FRiderProfile P;
		if (!Tab.IsLoaded())
		{
			SSXTest::Skip(*this, TEXT("no SimData/riders.bin: the profile rank check is skipped"));
		}
		else
		{
			SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(Tab, 0, O), P);
			TestEqual(TEXT("player profile +0x74 = career rank"), P.Level, 5);
			SSXRider::ApplyProfile(SSXRider::BuildComputerProfile(Tab, 1, true, 0, 0), P);
			TestEqual(TEXT("computer profile +0x74 = 0"), P.Level, 0);
		}
	}
	SSXFeTrickBook::FEntry E;
	E.Tweak1 = 1; E.ActA = 11;
	auto Mask = [](int32 Layout, int32 Ch, int32 Cls) { return [=](int32 A) { return FPadMapper::ActionMask(A, Layout, Ch, Cls); }; };
	TestEqual(TEXT("Eddie slot 11 = R2 + Square"), FSSXHudOverlay::UberComboString(E, Mask(0, 0, 0), false), FString(TEXT(" #+%")));
	E.ActA = 22;
	TestEqual(TEXT("slot 22 = L2"), FSSXHudOverlay::UberComboString(E, Mask(0, 0, 0), false), FString(TEXT(" $+%")));
	E.ActA = 25;
	TestEqual(TEXT("two shoulders"), FSSXHudOverlay::UberComboString(E, Mask(1, 0, 0), false), FString(TEXT(" @$+%")));
	E.ActA = 3;
	TestEqual(TEXT("unbound slot"), FPadMapper::ActionMask(3, 0, 0, 0), 0xFFFFFFFFu);
	TestEqual(TEXT("unbound slot string"), FSSXHudOverlay::UberComboString(E, Mask(0, 0, 0), false), FString(TEXT(" +%")));
	TestEqual(TEXT("tweak is Square in both layouts"), FPadMapper::ActionMask(FPadMapper::ActTweak, 1, 0, 0), (uint32)SSXPadButton::Square);

	E = SSXFeTrickBook::FEntry();
	E.Spin = 3; E.Flip = -1; E.ActA = 1; E.ActB = 2; E.Tweak2 = 1;
	auto Fake = [](int32 A) -> uint32 { return A == FPadMapper::ActTweak ? (SSXPadButton::Square | SSXPadButton::Triangle) : (A == 1 ? SSXPadButton::R1 : SSXPadButton::Cross); };
	TestEqual(TEXT("full string"), FSSXHudOverlay::UberComboString(E, Fake, false), FString(TEXT("d540+x360 ! TO *+(%OR^)")));
	TestEqual(TEXT("goofy spin"), FSSXHudOverlay::UberComboString(E, Fake, true).Left(4), FString(TEXT("a540")));
	const FSSXHudOverlay::FUberIcon* Ic = FSSXHudOverlay::FindUberIcon('#');
	TestTrue(TEXT("R2 icon"), Ic && Ic->Sprite == 113 && Ic->Mask == SSXPadButton::R2 && Ic->Adv == 32.f);
	return true;
}
