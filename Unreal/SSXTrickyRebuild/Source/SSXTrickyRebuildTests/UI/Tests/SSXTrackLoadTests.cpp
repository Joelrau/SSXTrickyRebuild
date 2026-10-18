// 0x1F12D0 / FUN_002459B0 / FUN_00246700
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXLoadReady.h"
#include "UI/SSXTrackLoad.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TlFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FSSXFeLaunch Launch(int32 Mode, int32 Track, int32 Char)
	{
		FSSXFeLaunch L;
		L.GameMode = Mode;
		L.Track = Track;
		L.Slots[0].Char = Char;
		return L;
	}

	int32 RunIntro(FSSXTrackLoader& T, int32 Max = 5000)
	{
		int32 N = 0;
		while (!T.bIntroDone() && N < Max) { T.Step(false, 3); ++N; }
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrackLoadQueueTest, "SSX.UI.TrackLoad.Queue", TlFlags)
bool FSSXTrackLoadQueueTest::RunTest(const FString& Parameters)
{
	FSSXLoadSequencer Q;
	Q.Reset();
	TestTrue(TEXT("reset: done"), Q.bDone);
	Q.Add(1, 1, 2, 0, 0, 4);
	Q.Add(2, 1, 0x46, 3, 0, 1000);
	Q.Add(3, 2, 2, 0, 0, 2);
	TestFalse(TEXT("an add clears done"), Q.bDone);
	TestEqual(TEXT("max step"), Q.MaxStep, 2);
	Q.Tick();
	TestEqual(TEXT("first tick enters step 1"), Q.Cur, 1);
	TestEqual(TEXT("one mode-0 channel counted"), Q.Active, 1);
	TestEqual(TEXT("nothing typed yet"), Q.Count(1), 0);
	Q.Tick(); TestEqual(TEXT("frame 1: 0"), Q.Count(1), 0);
	Q.Tick(); TestEqual(TEXT("frame 2: 1 (every 2nd frame)"), Q.Count(1), 1);
	TestTrue(TEXT("ramp after 2 ticks = 2/70"), FMath::IsNearlyEqual(Q.Value(2), 2.f / 70.f, 1e-5f));
	for (int32 N = 0; N < 6; ++N) Q.Tick();
	TestEqual(TEXT("typed to the end"), Q.Count(1), 4);
	TestEqual(TEXT("next step once done"), Q.Cur, 2);
	for (int32 N = 0; N < 4; ++N) Q.Tick();
	TestEqual(TEXT("step 2 typed"), Q.Count(3), 2);
	TestEqual(TEXT("past the last step"), Q.Cur, 3);
	TestTrue(TEXT("done past the last step"), Q.bDone);
	TestTrue(TEXT("the ramp keeps running (mode 3 from its step on)"), FMath::IsNearlyEqual(Q.Value(2), 12.f / 70.f, 1e-5f));

	FSSXLoadSequencer R;
	R.Reset();
	R.Add(1, 1, 1, 0, 0, 3);
	R.Add(2, 1, 1, 0, 0, 100);
	R.Tick();
	for (int32 N = 0; N < 4; ++N) R.Tick();
	TestTrue(TEXT("the step ends on the repeated done of the short channel"), R.Cur == 2 && R.Count(2) == 4);

	FSSXLoadSequencer S;
	S.Reset();
	S.Add(7, 1, 0x32, 2, 400, 1000);
	TestEqual(TEXT("pulse 0 before it starts"), S.Value(7), 0.f);
	float Lo = 10.f, Hi = -10.f;
	for (int32 N = 0; N < 200; ++N) { S.Tick(); if (S.Count(7) > 0) { Lo = FMath::Min(Lo, S.Value(7)); Hi = FMath::Max(Hi, S.Value(7)); } }
	TestTrue(TEXT("pulse within 0.4 .. 1.0"), Lo >= 0.4f - 1e-3f && Hi <= 1.f + 1e-3f && Lo < 0.45f && Hi > 0.95f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrackLoadStateTest, "SSX.UI.TrackLoad.StateMachine", TlFlags)
bool FSSXTrackLoadStateTest::RunTest(const FString& Parameters)
{
	using EPhase = FSSXTrackLoader::EPhase;
	const FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXTrackLoader T;
	T.Begin(Launch(2, 0, 3), &Profile, nullptr);
	TestEqual(TEXT("ctor: Prepare"), int32(T.Phase), int32(EPhase::Prepare));
	TestEqual(TEXT("ctor: opaque"), T.Fade, 1.f);
	T.Step(false, 0);
	TestEqual(TEXT("no update before the start"), T.Fade, 1.f);
	T.Start();
	TestEqual(TEXT("start: Run"), int32(T.Phase), int32(EPhase::Run));
	TestEqual(TEXT("start: loadingscreen.bnk #0"), T.MusicStarts, 1);
	TestEqual(TEXT("three lines"), T.Lines.Num, 3);
	T.Step(false, 0);
	TestTrue(TEXT("fade -1/60 per frame"), FMath::IsNearlyEqual(T.Fade, 1.f - 1.f / 60.f, 1e-6f));
	for (int32 N = 0; N < 70; ++N) T.Step(false, 0);
	TestEqual(TEXT("fade clamped at 0"), T.Fade, 0.f);
	TestFalse(TEXT("skip refused while loading"), T.RequestSkip(false));
	const int32 Frames = RunIntro(T);
	TestTrue(TEXT("intro ends"), T.bIntroDone() && Frames < 5000);
	for (int32 N = 0; N < 120; ++N) T.Step(false, 5);
	TestEqual(TEXT("PC: stays up until loaded"), int32(T.Phase), int32(EPhase::Run));
	TestEqual(TEXT("music still on at progress 5"), T.MusicStops, 0);
	T.Step(false, 8);
	TestEqual(TEXT("app+8 > 7 stops the bank-13 voice"), T.MusicStops, 1);
	T.Step(false, 9);
	TestEqual(TEXT("stopped once"), T.MusicStops, 1);
	T.Step(true, 10);
	TestEqual(TEXT("loaded + intro done: Finish"), int32(T.Phase), int32(EPhase::Finish));
	TestTrue(TEXT("closing fade +1/30"), T.bClosing && FMath::IsNearlyEqual(T.Fade, 1.f / 30.f, 1e-6f));
	int32 N = 0;
	while (T.Phase == EPhase::Finish && N < 100) { T.Step(true, 10); ++N; }
	TestEqual(TEXT("Done"), int32(T.Phase), int32(EPhase::Done));
	TestTrue(TEXT("back to black in 30 frames (then the vfunc_04 check)"), N >= 29 && N <= 31 && T.Fade == 1.f);

	FSSXTrackLoader U;
	U.Begin(Launch(2, 1, 0), &Profile, nullptr);
	U.Start();
	U.Step(true, 10);
	TestEqual(TEXT("loaded but the intro runs: Run"), int32(U.Phase), int32(EPhase::Run));
	int32 M = 0;
	while (U.Phase == EPhase::Run && M < 5000) { U.Step(true, 10); ++M; }
	TestTrue(TEXT("finishes when the intro has played"), U.bIntroDone() && U.Phase != EPhase::Run);
	FSSXTrackLoader V;
	V.Begin(Launch(2, 1, 0), &Profile, nullptr);
	V.Start();
	for (int32 K = 0; K < 10; ++K) V.Step(false, 4);
	TestFalse(TEXT("skip before the load: refused"), V.RequestSkip(false));
	V.Step(false, 4);
	TestEqual(TEXT("still running"), int32(V.Phase), int32(EPhase::Run));
	TestTrue(TEXT("skip after the load: accepted"), V.RequestSkip(true));
	V.Step(true, 10);
	TestTrue(TEXT("skip: Finish with the intro unfinished"), V.Phase == EPhase::Finish && !V.bIntroDone());
	return true;
}

// FUN_00214910 / 0x248A60 / 0x248AD0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrackLoadVoiceTest, "SSX.UI.TrackLoad.Voice", TlFlags)
bool FSSXTrackLoadVoiceTest::RunTest(const FString& Parameters)
{
	using SSXTrackLoad::EKind;
	FSSXTrackLoader K;
	const EKind Track[3] = { EKind::OneP, EKind::TwoP, EKind::Tutorial };
	for (EKind E : Track)
	{
		K.Kind = E;
		TestEqual(FString::Printf(TEXT("kind %d: bank 13 #0"), int32(E)), K.PageMusicSound(), 0);
		TestEqual(FString::Printf(TEXT("kind %d: 8 s stop"), int32(E)), K.MusicStopFade(), 8.f);
		TestTrue(FString::Printf(TEXT("kind %d: voice"), int32(E)), K.HasPageMusic());
	}
	const EKind Return[4] = { EKind::FromQuit, EKind::FEOneP, EKind::FETwoP, EKind::RiderRanking };
	for (EKind E : Return)
	{
		K.Kind = E;
		TestEqual(FString::Printf(TEXT("kind %d: bank 13 #1"), int32(E)), K.PageMusicSound(), 1);
		TestEqual(FString::Printf(TEXT("kind %d: 3 s stop"), int32(E)), K.MusicStopFade(), 3.f);
		TestTrue(FString::Printf(TEXT("kind %d: voice"), int32(E)), K.HasPageMusic() && K.TicksPageQueue());
	}
	K.Kind = EKind::TwoP;
	TestFalse(TEXT("c2PTrackLoad ticks no page queue"), K.TicksPageQueue());

	const FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXFeLaunch Two = Launch(2, 0, 0);
	Two.Players = 2;
	FSSXTrackLoader T;
	T.Begin(Two, &Profile, nullptr);
	TestEqual(TEXT("2 players: c2PTrackLoad"), int32(T.Kind), int32(EKind::TwoP));
	T.Start();
	TestEqual(TEXT("c2PTrackLoad start: the voice"), T.MusicStarts, 1);
	T.Step(false, 7);
	TestEqual(TEXT("on at app+8 7"), T.MusicStops, 0);
	T.Step(false, 8);
	TestEqual(TEXT("app+8 > 7 stops it"), T.MusicStops, 1);
	T.Step(true, 10);
	TestEqual(TEXT("loaded: Finish"), int32(T.Phase), int32(FSSXTrackLoader::EPhase::Finish));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrackLoadSelectionTest, "SSX.UI.TrackLoad.Selection", TlFlags)
bool FSSXTrackLoadSelectionTest::RunTest(const FString& Parameters)
{
	using namespace SSXTrackLoad;
	TestEqual(TEXT("mode 6 -> cTrickTutorialLoad"), int32(KindFor(Launch(6, 10, 0))), int32(EKind::Tutorial));
	FSSXFeLaunch Two = Launch(2, 0, 0);
	Two.Players = 2;
	TestEqual(TEXT("2 players -> c2PTrackLoad"), int32(KindFor(Two)), int32(EKind::TwoP));
	TestEqual(TEXT("else c1PTrackLoad"), int32(KindFor(Launch(4, 0, 0))), int32(EKind::OneP));
	TestEqual(TEXT("race picture (2 / 4)"), ModePicture(4), FString(TEXT("ldmoderace")));
	TestEqual(TEXT("showoff picture (3 / 5)"), ModePicture(3), FString(TEXT("ldmodeshowoff")));
	TestEqual(TEXT("freeride picture"), ModePicture(1), FString(TEXT("ldmodefreeride")));
	TestEqual(TEXT("time challenge picture"), ModePicture(7), FString(TEXT("ldmodetimec")));
	TestTrue(TEXT("no mode picture for 0 / 6"), ModePicture(0).IsEmpty() && ModePicture(6).IsEmpty());
	TestEqual(TEXT("Garibaldi"), TrackPicture(0), FString(TEXT("ldtrackgari")));
	TestEqual(TEXT("Tokyo Megaplex (8)"), TrackPicture(8), FString(TEXT("ldtracktoky")));
	TestEqual(TEXT("Alaska (11)"), TrackPicture(11), FString(TEXT("ldtrackalas")));
	TestTrue(TEXT("no picture for tracks 9 / 10"), TrackPicture(9).IsEmpty() && TrackPicture(10).IsEmpty());
	TestEqual(TEXT("Mac (3)"), RiderPicture(3), FString(TEXT("ldridermac")));
	TestEqual(TEXT("Marisol (11)"), RiderPicture(11), FString(TEXT("ldridermarisol")));
	TestEqual(TEXT("stat 1 Garibaldi LOC 0xEF"), StatLoc(0, 0), 0xEF);
	TestEqual(TEXT("stat 4 Alaska LOC 0xC94"), StatLoc(3, 11), 0xC94);
	TestEqual(TEXT("track 9 uses track 6's stats"), StatLoc(1, 9), StatLoc(1, 6));
	TestEqual(TEXT("tutorial track: default stat"), StatLoc(2, 10), 0x11A);
	TestTrue(TEXT("marker Snowdream (0.92, 0.38)"), MapMarker(1).Equals(FVector2f(0.92f, 0.38f)));
	TestEqual(TEXT("title: SE race 0xE71"), ModeTitleLoc(2), 0xE71);
	TestEqual(TEXT("title: WC 0xE2F"), ModeTitleLoc(4), 0xE2F);
	TestEqual(TEXT("sub-title: showoff 0xE2D"), SubTitleLoc(5), 0xE2D);
	TestEqual(TEXT("medal none -> LOC 306"), MedalLoc(3), 306);

	const FSSXProfile Profile = FSSXProfile::MakeDefault();
	const FSSXTrackLoadPage P = FSSXTrackLoadPage::Build(Launch(2, 11, 3), &Profile, nullptr);
	TestTrue(TEXT("page pictures by launch"), P.Pictures[0] == TEXT("ldmoderace") && P.Pictures[1] == TEXT("ldtrackalas")
		&& P.Pictures[2] == TEXT("ldridermac"));
	TestEqual(TEXT("rider name from the character table"), P.Values[4], FString(SSXFeTables::RiderName(3)));
	TestEqual(TEXT("chapter 1 on a new save"), P.Values[6], FString(TEXT("1")));
	FSSXTrackLoader T;
	T.Begin(Launch(2, 11, 3), &Profile, nullptr);
	TestEqual(TEXT("17 intro channels (FUN_00246700)"), T.Seq.Num, 17);
	TestEqual(TEXT("intro to step 9"), T.Seq.MaxStep, 9);
	FSSXTrackLoader Demo;
	Demo.Begin(Launch(8, 0, 0), &Profile, nullptr);
	TestTrue(TEXT("mode 8: no texts, no intro"), !Demo.Page.bTexts && Demo.bIntroDone() && Demo.Seq.Num == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrackLoadDrawTest, "SSX.UI.TrackLoad.Draw", TlFlags)
bool FSSXTrackLoadDrawTest::RunTest(const FString& Parameters)
{
	if (!FPaths::FileExists(SSXTrackLoad::JsonPath()) || !FPaths::FileExists(FPaths::Combine(SSXFrontEnd::FontDir(), TEXT("MENU.json"))))
	{
		SSXTest::Skip(*this, TEXT("loading screen art / fonts not converted (tools/export_loading_screen.py); draw checks skipped"));
		return true;
	}
	static UTexture2D* Dummy = nullptr;
	if (!Dummy) { Dummy = NewObject<UTexture2D>(GetTransientPackage()); Dummy->AddToRoot(); }
	FSSXFeAssets Fe;
	for (const TCHAR* Name : { TEXT("TITLE"), TEXT("MENU") })
	{
		const FString Base = FPaths::Combine(SSXFrontEnd::FontDir(), Name);
		TSharedPtr<FSSXBitmapFont> F = FSSXBitmapFont::Load(Base + TEXT(".json"), Dummy, FPaths::Combine(SSXFrontEnd::SfnDir(), FString(Name) + TEXT(".SFN")));
		(FString(Name) == TEXT("TITLE") ? Fe.Title : Fe.Menu) = F;
	}
	FSSXTrackLoadAssets Art;
	TestTrue(TEXT("loader art loads"), Art.Load(Fe, [](const FString&) { return Dummy; }));
	TestTrue(TEXT("28 records"), Art.Sprites.Sprites.Num() == 28);
	TestTrue(TEXT("loader fonts x 1.25 / y 1.0"), Art.Assets.Menu.IsValid() && Art.Assets.Menu->ScaleX == 1.25f && Art.Assets.Menu->ScaleY == 1.f);
	if (const FSSXSprite* R1 = Art.Sprites.Find(1); TestNotNull(TEXT("record 1 (map plate)"), R1))
	{
		TestTrue(TEXT("record 1 = 159 x 63 on loa1"), R1->W == 159.f && R1->H == 63.f && R1->Page == FName(TEXT("loa1")));
	}

	FSSXLocStrings Loc;
	Loc.Load(SSXFrontEnd::LangDir());
	const FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXTrackLoader T;
	T.Begin(Launch(2, 11, 3), &Profile, &Loc);
	T.Start();
	for (int32 N = 0; N < 3000 && T.Seq.Cur < 8; ++N) T.Step(false, 4);
	FSSXFeDrawList D(&Art.Assets);
	T.Draw(D);
	auto CountId = [&D](int32 Id) { int32 N = 0; for (const FSSXFeDrawCmd& C : D.Cmds) N += C.SpriteId == Id; return N; };
	TestEqual(TEXT("10 progress pieces"), [&]() { int32 N = 0; for (int32 I = 6; I < 16; ++I) N += CountId(I); return N; }(), 10);
	int32 Lit = 0;
	for (const FSSXFeDrawCmd& C : D.Cmds) { if (C.SpriteId >= 6 && C.SpriteId < 16 && C.Color.A == 1.f) ++Lit; }
	TestEqual(TEXT("app+8 = 4 pieces lit"), Lit, 4);
	TestEqual(TEXT("band notches (2 bands x 2 passes x 2)"), CountId(24), 8);
	TestTrue(TEXT("map plate and marker"), CountId(1) == 1 && CountId(2) == 1);
	int32 Images = 0;
	for (const FSSXFeDrawCmd& C : D.Cmds) { Images += C.Kind == FSSXFeDrawCmd::EKind::Image && C.Size == FVector2f(254.f, 79.f); }
	TestEqual(TEXT("3 pictures + their shadows"), Images, 6);
	const FSSXFeTextRec* Loading = nullptr;
	for (const FSSXFeTextRec& R : D.TextRecs) { if (R.Str == Loc.ByIndex(373)) Loading = &R; }
	if (TestNotNull(TEXT("loading..."), Loading)) { TestEqual(TEXT("loading... at (28, 430)"), Loading->Pos, FVector2f(28.f, 430.f)); }
	TestTrue(TEXT("rider name typed in"), D.Texts.Contains(T.Page.Values[4]));
	const FSSXFeDrawCmd* Fade = nullptr;
	for (const FSSXFeDrawCmd& C : D.Cmds) { if (C.bGsUntextured && C.Size == FVector2f(640.f, 480.f) && C.Color.R == 0.f && C.Color.A < 1.f) Fade = &C; }
	TestNull(TEXT("no fade quad once faded in"), Fade);
	FSSXTrackLoader I;
	I.Begin(Launch(2, 0, 4), &Profile, &Loc);
	I.Start();
	int32 Total = 0;
	while (!I.bIntroDone() && Total < 5000) { I.Step(false, 4); ++Total; }
	AddInfo(FString::Printf(TEXT("SE race at Garibaldi with Moby: intro %d frames"), Total));
	TestTrue(TEXT("the intro takes seconds"), Total > 60 && Total < 3000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrackLoadWaitForStreamingTest, "SSX.UI.TrackLoad.WaitForStreaming", TlFlags)
bool FSSXTrackLoadWaitForStreamingTest::RunTest(const FString& Parameters)
{
	using EPhase = FSSXTrackLoader::EPhase;
	const SSXLoadReady::FTextureList NoTextures;
	bool bPending = true;
	SSXLoadReady::SetProbeOverride([&bPending](const UWorld*)
	{
		FSSXLoadPending P;
		P.Streaming = bPending ? 12 : 0;
		P.Textures = bPending ? 3 : 0;
		return P;
	});
	const FSSXLoadPending Fake = SSXLoadReady::Probe(nullptr, NoTextures);
	TestTrue(TEXT("override probe used"), Fake.Streaming == 12 && Fake.Textures == 3 && Fake.Any());

	const FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXTrackLoader T;
	T.Begin(Launch(2, 0, 3), &Profile, nullptr);
	T.Start();
	FSSXTrackLoader Ref;
	Ref.Begin(Launch(2, 0, 3), &Profile, nullptr);
	Ref.Start();
	FSSXLoadGate Gate;
	Gate.Timeout = 1.0e6;
	double Now = 100.0;
	int32 Frames = 0;
	while (!T.bIntroDone() && Frames < 5000)
	{
		const bool bReady = Gate.Update(SSXLoadReady::Probe(nullptr, NoTextures), Now);
		T.Step(bReady, bReady ? 10 : 9);
		Ref.Step(false, 9);
		TestFalse(TEXT("not released while pending"), bReady);
		Now += 1.0 / 60.0;
		++Frames;
	}
	TestTrue(TEXT("same intro as without the hold"), Ref.bIntroDone() && T.Fade == Ref.Fade && T.MusicStops == Ref.MusicStops);
	for (int32 N = 0; N < 120; ++N)
	{
		const bool bReady = Gate.Update(SSXLoadReady::Probe(nullptr, NoTextures), Now);
		T.Step(bReady, bReady ? 10 : 9);
		Now += 1.0 / 60.0;
	}
	TestEqual(TEXT("intro done, still pending: the loader stays up"), int32(T.Phase), int32(EPhase::Run));
	TestFalse(TEXT("skip refused while held"), T.RequestSkip(Gate.bReleased));
	bPending = false;
	const bool bReady = Gate.Update(SSXLoadReady::Probe(nullptr, NoTextures), Now);
	TestTrue(TEXT("cleared: released"), bReady && Gate.bReleased && !Gate.bTimedOut);
	TestTrue(TEXT("held time measured"), Gate.HeldSeconds() > 0.0 && Gate.HeldSeconds() < Gate.Timeout);
	TestTrue(TEXT("waited-for kinds recorded"), Gate.Waited.Streaming == 12 && Gate.Waited.Textures == 3 && !Gate.Last.Any());
	T.Step(true, 10);
	TestEqual(TEXT("released: Finish"), int32(T.Phase), int32(EPhase::Finish));
	bPending = true;
	TestTrue(TEXT("latched once released"), Gate.Update(SSXLoadReady::Probe(nullptr, NoTextures), Now + 1.0));

	FSSXLoadGate G2;
	TestFalse(TEXT("first check starts the clock"), G2.Update(SSXLoadReady::Probe(nullptr, NoTextures), 50.0));
	TestFalse(TEXT("held before the timeout"), G2.Update(SSXLoadReady::Probe(nullptr, NoTextures), 50.0 + SSXLoadReady::TimeoutSeconds - 0.01));
	TestTrue(TEXT("released at the timeout"), G2.Update(SSXLoadReady::Probe(nullptr, NoTextures), 50.0 + SSXLoadReady::TimeoutSeconds));
	TestTrue(TEXT("flagged as timed out"), G2.bTimedOut && G2.Last.Any());

	SSXLoadReady::SetProbeOverride(SSXLoadReady::FProbe());
	FSSXLoadGate G3;
	TestTrue(TEXT("idle: immediate"), G3.Update(FSSXLoadPending(), 1.0) && !G3.bTimedOut && G3.HeldSeconds() == 0.0);

	using EMipWait = SSXLoadReady::EMipWait;
	TestTrue(TEXT("mips: full is done"), SSXLoadReady::ClassifyMips(12, 12, false, false) == EMipWait::Done);
	TestTrue(TEXT("mips: above full (optional mips unmounted) is done"), SSXLoadReady::ClassifyMips(12, 11, false, true) == EMipWait::Done);
	TestTrue(TEXT("mips: below full, idle, pool has room: pending"), SSXLoadReady::ClassifyMips(9, 12, false, false) == EMipWait::Pending);
	TestTrue(TEXT("mips: below full, in flight: pending"), SSXLoadReady::ClassifyMips(9, 12, true, false) == EMipWait::Pending);
	TestTrue(TEXT("mips: in flight under a full pool: still pending"), SSXLoadReady::ClassifyMips(9, 12, true, true) == EMipWait::Pending);
	TestTrue(TEXT("mips: below full, idle, pool full: limited"), SSXLoadReady::ClassifyMips(9, 12, false, true) == EMipWait::PoolLimited);

	FSSXLoadPending Limited;
	Limited.PoolLimited = 4;
	TestFalse(TEXT("pool-limited is not pending"), Limited.Any());
	FSSXLoadGate G4;
	TestTrue(TEXT("pool-limited: released at once, not timed out"), G4.Update(Limited, 1.0) && !G4.bTimedOut && G4.Waited.PoolLimited == 4);
	FSSXLoadPending OneLeft = Limited;
	OneLeft.Streaming = 1;
	FSSXLoadGate G5;
	TestFalse(TEXT("one texture still below full: held"), G5.Update(OneLeft, 1.0));
	TestTrue(TEXT("it reaches full: released"), G5.Update(Limited, 1.5) && !G5.bTimedOut);

	if (!FApp::CanEverRender())
	{
		TestEqual(TEXT("nullrhi: no world textures"), SSXLoadReady::CollectWorldTextures(nullptr).Num(), 0);
		TestEqual(TEXT("nullrhi: probe streaming 0"), SSXLoadReady::Probe(nullptr, NoTextures).Streaming, 0);
	}
	return true;
}

#endif
