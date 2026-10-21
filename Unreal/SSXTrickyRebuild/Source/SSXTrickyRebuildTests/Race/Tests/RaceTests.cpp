#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Race/AIInput.h"
#include "Race/RacePathTracker.h"
#include "Race/RaceRandom.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/SurfaceTable.h"
#include "Sim/Anim/RiderAnim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags RaceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Report(FAutomationTestBase* Test, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Race] %s"), *Msg);
	}

	FString SimDataFile(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Name);
	}

	bool LoadGari(FAutomationTestBase& Test, FRaceTrackData& Data)
	{
		FString Err;
		if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err))
		{
			Test.AddError(FString::Printf(TEXT("gari race data not available: %s"), *Err));
			return false;
		}
		return true;
	}

	bool LoadCsv(FAutomationTestBase& Test, const TCHAR* Name, TArray<FString>& Lines)
	{
		if (!FFileHelper::LoadFileToStringArray(Lines, *SimDataFile(Name)) || Lines.Num() < 2)
		{
			Test.AddError(FString::Printf(TEXT("cannot read SimData/%s (run tools/make_race_reference.py)"), Name));
			return false;
		}
		return true;
	}

	struct FCols
	{
		TArray<FString> C;
		explicit FCols(const FString& Line) { Line.ParseIntoArray(C, TEXT(","), false); }
		double D(int32 I) const { return FCString::Atod(*C[I]); }
		int32 I(int32 K) const { return FCString::Atoi(*C[K]); }
		FVector V(int32 K) const { return FVector(D(K), D(K + 1), D(K + 2)); }
		int32 Num() const { return C.Num(); }
	};

	struct FRacers
	{
		TArray<TUniquePtr<FRiderSim>> Sims;
		void Make(int32 N, const IRiderWorld* World)
		{
			for (int32 I = 0; I < N; ++I)
			{
				TUniquePtr<FRiderSim> S = MakeUnique<FRiderSim>();
				SSXRiderSetup::RegisterStates(*S, true);
				S->SetWorld(World);
				Sims.Add(MoveTemp(S));
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceSpawnTest, "SSX.Race.Spawn", RaceTestFlags)

bool FSSXRaceSpawnTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	TArray<FString> Lines;
	if (!LoadGari(*this, Data) || !LoadCsv(*this, TEXT("gari.race_spawn.csv"), Lines))
	{
		return false;
	}
	TestEqual(TEXT("Garibaldi track id"), Data.TrackId, 0);
	TestTrue(TEXT("world floor from .ltg"), Data.bHasWorldFloor && FMath::IsNearlyEqual(Data.WorldFloorZ, -271772.0));
	TestTrue(TEXT("race length 460747 cm"), FMath::Abs(Data.Race.RaceLength() - 460747.0) < 1.0);

	double MaxErr = 0.0, MaxHErr = 0.0;
	int32 Rows = 0;
	TArray<double> Sizes6;
	TArray<FVector> Expect6;
	for (int32 L = 1; L < Lines.Num(); ++L)
	{
		const FCols C(Lines[L]);
		if (C.Num() < 8) { continue; }
		const int32 Racer = C.I(1), Count = C.I(2);
		const double Size = C.D(3);
		FVector P;
		double H;
		const int32 Path = SSXRace::StartPose(Data.Race, Racer, Count, Size, P, H);
		TestTrue(TEXT("start path found"), Path >= 0);
		MaxErr = FMath::Max(MaxErr, (P - C.V(4)).Size());
		MaxHErr = FMath::Max(MaxHErr, FMath::Abs(SSXRace::WrapAngle(H - C.D(7))));
		if (Count == 6) { Sizes6.Add(Size); Expect6.Add(C.V(4)); }
		++Rows;
	}
	FRacers R;
	R.Make(6, nullptr);
	FRaceSession Session;
	FRaceConfig Cfg;
	TestTrue(TEXT("session init"), Session.Init(Data, Cfg));
	for (int32 I = 0; I < 6; ++I)
	{
		R.Sims[I]->Profile.SizeScale = Sizes6.IsValidIndex(I) ? Sizes6[I] : 1.0;
		Session.AddRacer(R.Sims[I].Get(), I == 4);
	}
	Session.Start();
	Session.Tick();
	double MaxSimErr = 0.0;
	for (int32 I = 0; I < 6 && I < Expect6.Num(); ++I)
	{
		const FRiderState& S = R.Sims[I]->State;
		MaxSimErr = FMath::Max(MaxSimErr, (S.Position - Expect6[I]).Size());
		TestTrue(TEXT("Static after placement"), S.Motion == ERiderMotion::Static);
		TestTrue(TEXT("Void after placement"), S.Control == ERiderControl::Void);
		TestTrue(TEXT("velocity 0"), S.Velocity.IsZero());
		TestTrue(TEXT("path heading written"), FMath::Abs(SSXRace::WrapAngle(S.PathHeading - S.Yaw)) < 0.2);
		TestTrue(TEXT("distance to go near the race length"), S.TrackDistToGo > 455000.0);
	}
	Report(this, FString::Printf(TEXT("start poses %d: max |dpos| %.3g cm, max |dheading| %.3g rad; session placement max |dpos| %.3g cm"),
		Rows, MaxErr, MaxHErr, MaxSimErr));
	TestTrue(TEXT("pose within 1 cm"), MaxErr < 1.0 && MaxSimErr < 1.0);
	TestTrue(TEXT("heading within 1e-6 rad"), MaxHErr < 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRacePathTrackingTest, "SSX.Race.PathTracking", RaceTestFlags)

bool FSSXRacePathTrackingTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	TArray<FString> Lines;
	if (!LoadGari(*this, Data) || !LoadCsv(*this, TEXT("gari.race_track.csv"), Lines))
	{
		return false;
	}
	struct FTrack { FRaceLineFollow Race; FRaceLineProgress Line; };
	TMap<int32, FTrack> Riders;
	int32 Frames = 0, ParamOk = 0, HeadOk = 0, PathOk = 0, ToGoOk = 0, MaskOk = 0, LineOk = 0;
	double MaxParam = 0.0, MaxHead = 0.0, MaxToGo = 0.0;
	for (int32 L = 1; L < Lines.Num(); ++L)
	{
		const FCols C(Lines[L]);
		if (C.C[0] == TEXT("#init"))
		{
			FTrack& T = Riders.Add(C.I(1));
			T.Race.SetPath(C.I(2));
			T.Line.SetLine(C.I(3));
			T.Line.Best = C.D(4);
			continue;
		}
		if (C.Num() < 24) { continue; }
		FTrack* T = Riders.Find(C.I(0));
		if (!T) { continue; }
		const int32 Frame = C.I(1);
		const FVector Pos = C.V(2), Vel = C.V(5);
		const bool bFinished = C.I(8) != 0;
		T->Race.Follow(Data.Race, Pos, Vel, Frame, bFinished);
		TArray<FRaceTriggerEvent> Events;
		T->Line.Update(Data.Race, Pos, Vel, Frame, bFinished, 0.0, Events);
		++Frames;
		const double DP = FMath::Abs(T->Race.Param - C.D(10));
		const double DH = FMath::Abs(SSXRace::WrapAngle(T->Race.Heading - C.D(13)));
		const double DT = FMath::Abs(T->Line.DistToGo - C.D(21));
		PathOk += T->Race.Path == C.I(9);
		ParamOk += DP < 0.01;
		HeadOk += DH < 3e-5;   // 0x2510F8
		LineOk += T->Line.Line == C.I(20);
		ToGoOk += DT < 0.01;
		MaskOk += T->Line.Mask == uint32(FCString::Atoi64(*C.C[23]));
		MaxParam = FMath::Max(MaxParam, DP);
		MaxHead = FMath::Max(MaxHead, DH);
		MaxToGo = FMath::Max(MaxToGo, DT);
	}
	Report(this, FString::Printf(TEXT("%d frames, %d riders: AI line %d, param (<0.01 cm) %d, heading (<3e-5) %d, line path %d, "
		"distance to go (<0.01 cm) %d, trigger mask %d; max |dparam| %.3g cm, |dheading| %.3g rad, |dtogo| %.3g cm"),
		Frames, Riders.Num(), PathOk, ParamOk, HeadOk, LineOk, ToGoOk, MaskOk, MaxParam, MaxHead, MaxToGo));
	TestTrue(TEXT("enough frames"), Frames > 10000);
	TestEqual(TEXT("AI line every frame"), PathOk, Frames);
	TestEqual(TEXT("path param every frame"), ParamOk, Frames);
	TestEqual(TEXT("path heading every frame"), HeadOk, Frames);
	TestEqual(TEXT("line path every frame"), LineOk, Frames);
	TestEqual(TEXT("distance to go every frame"), ToGoOk, Frames);
	TestEqual(TEXT("trigger mask every frame"), MaskOk, Frames);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceAIInputTest, "SSX.Race.AIInput", RaceTestFlags)

bool FSSXRaceAIInputTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	TArray<FString> Lines;
	if (!LoadGari(*this, Data) || !LoadCsv(*this, TEXT("gari.race_ai.csv"), Lines) || Lines.Num() < 3)
	{
		return false;
	}
	const FCols Head(Lines[0]);
	FAIState St;
	SSXRaceAI::ResetAI(St, Head.D(9), Head.D(11));
	St.TimeScale = Head.D(13);
	FRaceRandom Rng(uint32(FCString::Atoi64(*Head.C[15])));
	const int32 Self = Head.I(3);
	constexpr int32 N = 6;
	TArray<FAIOtherRider> Others;
	Others.SetNum(N);
	int32 Rows = 0, WordExact = 0, CrouchOk = 0, BoostOk = 0, JumpOk = 0, SteerOk = 0, OtherOk = 0, Ground = 0;
	int32 BehaviourOk = 0, TimeScaleOk = 0;
	for (int32 L = 2; L < Lines.Num(); ++L)
	{
		const FCols C(Lines[L]);
		if (C.Num() < 118) { continue; }
		int32 K = 0;
		auto Nd = [&]() { return C.D(K++); };
		auto Ni = [&]() { return C.I(K++); };
		auto Nv = [&]() { const FVector V = C.V(K); K += 3; return V; };
		FAIRiderView V;
		FAIRaceView RV;
		V.Index = Self;
		RV.Frame = Ni();
		RV.Mode = ERaceEventMode(Ni());
		RV.HumanRank = Ni();
		const int32 Path = Ni();
		V.Path = &Data.Race.AIPaths[Path];
		V.Pos = Nv(); V.Vel = Nv(); V.Side = Nv(); V.Fwd = Nv(); V.Up = Nv();
		V.Yaw = Nd(); V.Motion = Ni(); V.Control = Ni();
		V.PathParam = Nd(); V.PathParamPrev = Nd(); V.PathLateral = Nd(); V.Lookahead = Nv(); V.PathHeading = Nd();
		V.BoostMeter = Nd(); V.Rank = Ni(); V.BoardClass = Ni(); V.ProfileBoardClass = Ni(); V.Character = Ni();
		V.StatAir = Ni(); V.StatTrick = Ni(); V.GateCur = Nd(); V.GateTarget = Nd(); V.PredTimeLeft = Nd();
		V.AirElapsed = Nd(); V.PredLandNormal = Nv(); V.SpinAngle = Nd(); V.FlipAngle = Nd();
		V.CatchupRival = Ni(); V.CatchupNear = Nd(); V.CatchupFar = Nd();
		V.Grudge.bActive = Ni() != 0; V.Grudge.Frame = Ni(); V.Grudge.Rival = Ni(); V.Grudge.Kind = Ni(); V.Grudge.bCooled = Ni() != 0;
		for (int32 J = 0; J < N; ++J) { V.RivalDist[J] = Nd(); }
		for (int32 J = 0; J < N; ++J) { V.RivalBearing[J] = Nd(); }
		for (int32 J = 0; J < N; ++J)
		{
			Others[J].Index = J;
			Others[J].Pos = Nv();
			Others[J].Vel = Nv();
			Others[J].Rank = Ni();
			Others[J].bHuman = Ni() != 0;
		}
		const uint32 Expect = uint32(FCString::Atoi64(*C.C[K++]));
		const int32 ExpBehaviour = Ni();
		Ni();
		Nd();
		const double ExpTimeScale = Nd();
		RV.Clock = 0.0;
		RV.Riders = Others;

		const uint32 Word = SSXRaceAI::AIWord(St, V, RV, Rng);
		++Rows;
		WordExact += Word == Expect;
		BehaviourOk += St.Behaviour == ExpBehaviour;
		TimeScaleOk += FMath::Abs(St.TimeScale - ExpTimeScale) < 1e-9;
		const ERiderControl Ctl = ERiderControl(V.Control);
		const FRiderInputWord A = FPadMapper::DecodeWord(Ctl, Word);
		const FRiderInputWord B = FPadMapper::DecodeWord(Ctl, Expect);
		if (Ctl == ERiderControl::Cruise || Ctl == ERiderControl::LandNormal || Ctl == ERiderControl::LandHard
			|| Ctl == ERiderControl::GateToBase || Ctl == ERiderControl::Bump || Ctl == ERiderControl::ChangeFakie)
		{
			++Ground;
			CrouchOk += A.Crouch == B.Crouch && A.Brake == B.Brake;
			BoostOk += A.bBoostHeld == B.bBoostHeld;
			JumpOk += A.bJumpPressed == B.bJumpPressed && A.bJumpHeld == B.bJumpHeld && A.bResetRequest == B.bResetRequest;
			SteerOk += FMath::Abs(A.Turn - B.Turn) <= 1.0 / 31.0 + 1e-9;
		}
		else
		{
			OtherOk += Word == Expect;
		}
	}
	Report(this, FString::Printf(TEXT("%d AI frames (%d ground): word exact %d, crouch/brake %d, boost %d, jump/reset %d, steer within one step %d, "
		"non-ground words exact %d/%d, behaviour %d, time_scale %d"), Rows, Ground, WordExact, CrouchOk, BoostOk, JumpOk, SteerOk,
		OtherOk, Rows - Ground, BehaviourOk, TimeScaleOk));
	TestTrue(TEXT("enough frames"), Rows > 5000);
	TestEqual(TEXT("crouch / brake exact"), CrouchOk, Ground);
	TestEqual(TEXT("boost exact"), BoostOk, Ground);
	TestEqual(TEXT("jump / reset exact"), JumpOk, Ground);
	TestEqual(TEXT("steer within one step"), SteerOk, Ground);
	TestEqual(TEXT("non-ground words exact"), OtherOk, Rows - Ground);
	TestEqual(TEXT("catch-up time_scale"), TimeScaleOk, Rows);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceCountdownTest, "SSX.Race.CountdownGate", RaceTestFlags)

bool FSSXRaceCountdownTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	if (!LoadGari(*this, Data))
	{
		return false;
	}
	FPatchWorld World;
	FString Err;
	if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("gari patches not available: %s"), *Err));
		return false;
	}
	for (const ERaceEventMode Mode : { ERaceEventMode::CircuitRace, ERaceEventMode::PracticeFreeride })
	{
		const bool bGates = Mode == ERaceEventMode::CircuitRace;
		FRacers R;
		R.Make(6, &World);
		FRaceSession Session;
		FRaceConfig Cfg;
		Cfg.Mode = Mode;
		Session.Init(Data, Cfg);
		for (int32 I = 0; I < 6; ++I)
		{
			Session.AddRacer(R.Sims[I].Get(), false);
		}
		TestEqual(TEXT("gate mode"), Session.UsesGates(), bGates);
		Session.Start();
		// 0x111108
		for (int32 I = 0; I < 6; ++I)
		{
			const FRiderState& St0 = R.Sims[I]->State;
			TestTrue(TEXT("race start: placed (Static + Void, at rest, off the origin)"), St0.Motion == ERiderMotion::Static
				&& St0.Control == ERiderControl::Void && St0.Velocity.IsZero() && St0.Position.Size() > 1000.0);
		}
		TestEqual(TEXT("race start: clock 0"), Session.GetClock(), 0.0);
		auto StepAll = [&]() { Session.Tick(); for (auto& S : R.Sims) { S->Step(FRiderInputWord()); } };
		StepAll();
		TestTrue(TEXT("frame 1: PreRace"), Session.GetPhase() == ERacePhase::PreRace);
		StepAll();
		TestTrue(TEXT("frame 2: StartRace"), Session.GetPhase() == ERacePhase::StartRace);
		StepAll();
		TestTrue(TEXT("frame 3: Countdown"), Session.GetPhase() == ERacePhase::Countdown);
		// 0x113520
		TestEqual(TEXT("countdown clock after the first update"), Session.GetClock(), bGates ? -298.0 : 2.0);
		for (auto& S : R.Sims)
		{
			TestTrue(TEXT("countdown control"), S->State.Control == (bGates ? ERiderControl::GateAnticipate : ERiderControl::SitAndWait));
			TestTrue(TEXT("still Static"), S->State.Motion == ERiderMotion::Static);
		}
		int32 CountdownFrames = 1;
		while (Session.GetPhase() == ERacePhase::Countdown && CountdownFrames < 400)
		{
			Session.Tick();
			if (Session.GetPhase() != ERacePhase::Countdown)
			{
				break;
			}
			++CountdownFrames;
			TestEqual(TEXT("clock rate 2 per frame"), Session.GetClock(), (bGates ? -300.0 : 0.0) + 2.0 * CountdownFrames);
			for (auto& S : R.Sims) { S->Step(FRiderInputWord()); }
		}
		TestEqual(TEXT("150 countdown frames (2.5 s), 1 without gates"), CountdownFrames, bGates ? 150 : 1);
		TestTrue(TEXT("GO: Race phase"), Session.GetPhase() == ERacePhase::Race);
		for (auto& S : R.Sims)
		{
			TestTrue(TEXT("race enter control"), S->State.Control == (bGates ? ERiderControl::GateLaunch : ERiderControl::SitAndWait));
			TestTrue(TEXT("racing flag"), S->State.bRacing);
		}
		int32 Moving = 0, Cruising = 0;
		for (int32 F = 0; F < 300; ++F)
		{
			for (auto& S : R.Sims) { S->Step(FRiderInputWord()); }
			Session.Tick();
		}
		for (auto& S : R.Sims)
		{
			Moving += S->State.Velocity.Size() > 100.0;
			Cruising += S->State.Motion != ERiderMotion::Static && S->State.Control != ERiderControl::GateLaunch
				&& S->State.Control != ERiderControl::SitAndWait;
		}
		Report(this, FString::Printf(TEXT("%s: countdown %d frames, after 5 s of race %d/6 racers moving, %d/6 past the start control; clock %.2f s"),
			LexToString(Mode), CountdownFrames, Moving, Cruising, Session.GetClock()));
		TestEqual(TEXT("everyone left the start"), Cruising, 6);
		TestEqual(TEXT("everyone moving"), Moving, 6);
		TestTrue(TEXT("race clock ~5 s"), FMath::Abs(Session.GetClock() - 300.0 / 60.0) < 0.05);
		TArray<FRaceStanding> Standings;
		Session.GetStandings(Standings);
		TestEqual(TEXT("six standings"), Standings.Num(), 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceRespawnTest, "SSX.Race.Respawn", RaceTestFlags)

bool FSSXRaceRespawnTest::RunTest(const FString& Parameters)
{
	FRaceTrackData Data;
	TArray<FString> Lines;
	if (!LoadGari(*this, Data) || !LoadCsv(*this, TEXT("gari.race_respawn.csv"), Lines))
	{
		return false;
	}
	int32 Cases = 0, CaseOk = 0;
	for (const FString& Line : Lines)
	{
		const FCols C(Line);
		if (C.Num() < 12) { continue; }
		const FRacePath& Path = Data.Race.AIPaths[C.I(1)];
		const double Param = C.D(2);
		const int32 NO = C.I(3);
		TArray<SSXRace::FCrowdRacer> Others;
		int32 K = 4;
		for (int32 J = 0; J < NO; ++J, K += 6)
		{
			Others.Add({ C.V(K), C.V(K + 3) });
		}
		const bool bExpOk = C.I(K) != 0;
		FVector P, V;
		double H;
		const bool bOk = SSXRace::RespawnPoint(Path, Param, Others, P, H, V, [](const FVector&) { return true; });
		bool bMatch = bOk == bExpOk;
		if (bOk && bExpOk)
		{
			bMatch = (P - C.V(K + 1)).Size() < 1e-6 && FMath::Abs(SSXRace::WrapAngle(H - C.D(K + 4))) < 1e-9 && (V - C.V(K + 5)).Size() < 1e-6;
		}
		++Cases;
		CaseOk += bMatch;
		if (!bMatch)
		{
			AddError(FString::Printf(TEXT("respawn case %s: ok %d/%d pos (%.2f %.2f %.2f)"), *C.C[0], bOk, bExpOk, P.X, P.Y, P.Z));
		}
	}
	Report(this, FString::Printf(TEXT("respawn placement cases %d/%d match models/race.py"), CaseOk, Cases));
	TestTrue(TEXT("respawn cases present"), Cases >= 5);

	FPatchWorld World;
	FString Err;
	if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("gari patches not available: %s"), *Err));
		return false;
	}
	FRacers R;
	R.Make(2, &World);
	FRaceSession Session;
	FRaceConfig Cfg;
	Session.Init(Data, Cfg);
	Session.AddRacer(R.Sims[0].Get(), false);
	Session.AddRacer(R.Sims[1].Get(), false);
	Session.Start();
	Session.Tick();
	Session.SetPhase(ERacePhase::Race);
	Session.Tick();
	TestTrue(TEXT("race phase"), Session.GetPhase() == ERacePhase::Race);
	TestEqual(TEXT("world floor from .ltg"), R.Sims[0]->GetWorldFloor(), -271772.0);

	FRacerRecord& Rec = Session.GetRacerMutable(0);
	const FRacePath& Line = Data.Race.AIPaths[Rec.Race.Path];
	const double Param = 30000.0;
	FRiderSim& Sim = *R.Sims[0];
	Sim.ApplyTransition(TEXT("T03"));
	Sim.State.Position = Line.PointAt(Param) + FVector(0, 0, 50.0);
	Rec.Race.Reproject(Data.Race, Sim.State.Position);
	Rec.Line.Cache = FRacePathCache();
	Session.TrackScan(0);
	TestTrue(TEXT("path param follows the rider"), FMath::Abs(Sim.State.PathParam - Param) < 1.0);
	const uint32 Serial = Sim.State.RespawnSerial;
	Sim.ForcedReset(false, TEXT("test"));
	TestTrue(TEXT("ResetWait"), Sim.State.Control == ERiderControl::ResetWait && Sim.State.Motion == ERiderMotion::Static);
	int32 Frames = 0, AlphaOk = 0, AlphaN = 0, FirstZero = -1;
	while (Sim.State.Control == ERiderControl::ResetWait && Frames < 120)
	{
		Sim.Step(FRiderInputWord());
		++Frames;
		if (Sim.State.Control == ERiderControl::ResetWait)
		{
			// 0x106A90, 0x1364D0
			++AlphaN;
			AlphaOk += FMath::Abs(Sim.State.RenderAlpha - FMath::Clamp(2.0 * (0.5 - Frames / 60.0), 0.0, 1.0)) < 1e-5;
			if (FirstZero < 0 && Sim.State.RenderAlpha <= 0.0) { FirstZero = Frames; }
		}
	}
	TestEqual(TEXT("ResetWait render alpha = clamp(2 (0.5 - t), 0, 1)"), AlphaOk, AlphaN);
	TestTrue(TEXT("rider hidden (alpha 0) from frame 30 (t = 0.5 s) until the respawn"), FirstZero == 30 || FirstZero == 31);
	TestTrue(TEXT("respawn restores alpha 1"), Sim.State.RenderAlpha == 1.0);
	const double PlacedParam = Sim.State.PathParam;
	FVector ExpP, ExpV;
	double ExpH;
	SSXRace::RespawnPoint(Data.Race.AIPaths[Rec.Race.Path], PlacedParam, {}, ExpP, ExpH, ExpV, [](const FVector&) { return true; });
	Report(this, FString::Printf(TEXT("forced reset: respawn after %d frames at param %.1f (AI advance 0), pos err %.3g cm, speed %.2f cm/s, %s/%s"),
		Frames, PlacedParam, (Sim.State.Position - (ExpP + FVector(0, 0, SSXRace::RespawnDrop))).Size(), Sim.State.Velocity.Size(),
		LexToString(Sim.State.Motion), LexToString(Sim.State.Control)));
	TestTrue(TEXT("respawned after 0.8 s"), Frames >= 48 && Frames <= 50);
	// 0x119220 -> 0x173A60 / 0x177DB0: the respawn cuts the human camera (SnowboarderPawn watches the serial)
	TestEqual(TEXT("one respawn, one camera cut"), Sim.State.RespawnSerial, Serial + 1);
	TestTrue(TEXT("Air + NaturalAir"), Sim.State.Motion == ERiderMotion::Air && Sim.State.Control == ERiderControl::NaturalAir);
	TestTrue(TEXT("param unchanged for an AI (advance 0)"), FMath::Abs(PlacedParam - Param) < 1.0);
	// 0x11CAE8
	FRiderWorldHit RespawnHit;
	const bool bRespawnHit = World.LineQuery(ExpP - FVector(0, 0, SSXRace::RespawnDrop + 200.0), ExpP + FVector(0, 0, 200.0),
		ERiderLineQuery::FineRefined, 0.0, RespawnHit);
	TestTrue(TEXT("101.46 cm above the ground under the race-line point"), bRespawnHit
		&& (Sim.State.Position - (RespawnHit.Point + FVector(0, 0, SSXRace::RespawnDrop))).Size() < 1e-6);
	TestTrue(TEXT("respawn tilted onto the ground normal"), bRespawnHit && FVector::DotProduct(Sim.State.BasisUp, RespawnHit.Normal) > 1.0 - 1e-8);
	TestTrue(TEXT("833.33 cm/s along the line"), (Sim.State.Velocity - ExpV).Size() < 1e-6
		&& FMath::Abs(FVector2D(ExpV.X, ExpV.Y).Size() - SSXRace::RespawnSpeed) < 50.0);

	FRaceTrackData Zoned = Data;
	FRacePathEvent Zone;
	Zone.Type = SSXRaceEvent::AINoReset;
	Zone.Start = Sim.State.PathParam - 100.0;
	Zone.End = Sim.State.PathParam + 1000.0;
	Zoned.Race.AIPaths[Rec.Race.Path].Events.Add(Zone);
	FRacers R2;
	R2.Make(1, &World);
	FRaceSession S2;
	S2.Init(Zoned, Cfg);
	S2.AddRacer(R2.Sims[0].Get(), false);
	S2.Start();
	S2.Tick();
	S2.GetRacerMutable(0).Race.SetPath(Rec.Race.Path);
	R2.Sims[0]->ApplyTransition(TEXT("T03"));
	R2.Sims[0]->State.PathParam = Sim.State.PathParam;
	R2.Sims[0]->ForcedReset(false, TEXT("test"));
	TestTrue(TEXT("no-reset zone refuses the forced reset"), R2.Sims[0]->State.Control != ERiderControl::ResetWait);

	FRiderSim& S1 = *R.Sims[1];
	S1.ApplyTransition(TEXT("T02"));
	S1.State.Position.Z = -271772.0 - 10001.0;
	S1.Step(FRiderInputWord());
	TestTrue(TEXT("world-floor reset"), S1.State.Control == ERiderControl::ResetWait);
	return CaseOk == Cases;
}

// 0x11CAE8, 0x116AB8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXResetSpawnTest, "SSX.Race.ResetSpawn", RaceTestFlags)

bool FSSXResetSpawnTest::RunTest(const FString& Parameters)
{
	constexpr double SlopeDeg = 15.0;
	FPatchWorld Slope;
	SSXRiderSetup::BuildSlopeWorld(Slope, SlopeDeg, 3);
	const double A = FMath::DegreesToRadians(SlopeDeg);
	const FVector N(0.0, FMath::Sin(A), FMath::Cos(A));
	auto PlaneZ = [&](double X, double Y) { return -Y * FMath::Tan(A); };
	auto MakeSim = [&](TUniquePtr<FRiderSim>& S)
	{
		S = MakeUnique<FRiderSim>();
		SSXRiderSetup::RegisterStates(*S, true);
		S->SetWorld(&Slope);
		S->Reset();
	};

	{
		TUniquePtr<FRiderSim> S;
		MakeSim(S);
		TSharedRef<SSXAnim::FRiderAnimChannel> Ch = SSXAnim::AcquireChannel(*S);
		FRiderState& St = S->State;
		St.SpinAngle = 1.0; St.FlipAngle = 2.0; St.GrindType = 3; St.SpeedCap = 5000.0;
		St.AirTurnOffset = St.AirTiltOffset = St.AirTurnSmoothed = St.AirTiltSmoothed = 0.5;
		St.Lean.Cur = 10.0; St.Steer.Cur = 0.3;
		const FVector P(120.0, 300.0, PlaneZ(120.0, 300.0) + 150.0);
		const FVector H = FVector(FMath::Cos(0.6), FMath::Sin(0.6), 0.0);
		S->Spawn(P, H, FVector::ZeroVector, 0.0, false);
		FRiderWorldHit Snap;
		const bool bSnap = Slope.LineQuery(FVector(P.X, P.Y, P.Z - 200.0), FVector(P.X, P.Y, P.Z + 200.0), ERiderLineQuery::FineRefined, 0.0, Snap);
		TestTrue(TEXT("slope: snapped onto the ground below (ray hit point)"), bSnap && FMath::Abs(St.Position.Z - PlaneZ(P.X, P.Y)) < 0.05
			&& St.Position == Snap.Point);
		TestTrue(TEXT("slope: body up tilted onto the ground normal"), FVector::DotProduct(St.BasisUp, N) > 1.0 - 1e-6);
		const FVector WantF = FQuat::FindBetweenNormals(FVector::UpVector, N).RotateVector(H);
		// 0x2510F8
		TestTrue(TEXT("slope: forward = shortest rotation of the heading (0x128E90 by -asin|n x up|)"), (St.BasisForward - WantF).Size() < 5e-5);
		const FVector Projected = (H - FVector::DotProduct(H, N) * N).GetSafeNormal();
		TestTrue(TEXT("slope: not the projected heading"), (St.BasisForward - Projected).Size() > 1e-3);
		TestTrue(TEXT("slope: smoothed up +0x2C0 = n"), (St.SmoothedUp - N).Size() < 1e-6);
		TestTrue(TEXT("slope: fields +0x1C4 / +0x1D0 / +0x1D4 / +0x1DC / +0x1F0..+0x280 zeroed"), St.SpinAngle == 0.0 && St.FlipAngle == 0.0
			&& St.GrindType == 0 && St.Lean.Cur == 0.0 && St.Steer.Cur == 0.0 && St.AirTurnOffset == 0.0 && St.AirTiltOffset == 0.0
			&& St.AirTurnSmoothed == 0.0 && St.AirTiltSmoothed == 0.0 && St.SpeedCap == 0.0 && St.Velocity.IsZero());
		TestTrue(TEXT("slope: ground probe ran (surface of the slope), Ground + Cruise"), St.SurfaceId == 3
			&& St.Motion == ERiderMotion::Ground && St.Control == ERiderControl::Cruise);
		TestTrue(TEXT("slope: animation reset 0x15EE30 requested"), Ch->bResetAnim);
	}
	{
		struct FCase { double Above; bool bHit; };
		for (const FCase C : { FCase{ 199.0, true }, FCase{ 201.0, false }, FCase{ -199.0, true }, FCase{ -201.0, false } })
		{
			TUniquePtr<FRiderSim> S;
			MakeSim(S);
			const FVector P(0.0, 0.0, C.Above);
			S->Spawn(P, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
			const bool bSnapped = FMath::Abs(S->State.Position.Z) < 0.05;
			const bool bTilted = FVector::DotProduct(S->State.BasisUp, N) > 1.0 - 1e-6;
			TestTrue(*FString::Printf(TEXT("ray limit: %.0f cm above the ground -> %s"), C.Above, C.bHit ? TEXT("hit") : TEXT("miss")),
				C.bHit ? (bSnapped && bTilted) : (S->State.Position == P && S->State.BasisUp.Equals(FVector::UpVector, 1e-6)));
		}
		// 0x11CDDC
		TUniquePtr<FRiderSim> S;
		MakeSim(S);
		S->Spawn(FVector(0.0, 0.0, 250.0), FVector(0, 1, 0), FVector::ZeroVector, 101.46, false);
		TestTrue(TEXT("drop 101.46: ray reaches 250 cm down, z = hit + drop, Air"), FMath::Abs(S->State.Position.Z - 101.46) < 0.05
			&& S->State.Motion == ERiderMotion::Air && FVector::DotProduct(S->State.BasisUp, N) > 1.0 - 1e-6);
		TUniquePtr<FRiderSim> S2;
		MakeSim(S2);
		TSharedRef<SSXAnim::FRiderAnimChannel> Ch = SSXAnim::AcquireChannel(*S2);
		S2->Spawn(FVector(0.0, 0.0, 50.0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, true);
		TestTrue(TEXT("initial: placed as given, upright, Static + Void, no animation reset"), S2->State.Position == FVector(0.0, 0.0, 50.0)
			&& S2->State.BasisUp.Equals(FVector::UpVector, 1e-9) && S2->State.Motion == ERiderMotion::Static && !Ch->bResetAnim);
	}

	FRaceTrackData Data;
	if (!LoadGari(*this, Data))
	{
		return false;
	}
	FPatchWorld World;
	FString Err;
	if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("gari patches not available: %s"), *Err));
		return false;
	}
	FRacers R;
	R.Make(6, &World);
	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::CircuitRace;
	Session.Init(Data, Cfg);
	for (int32 I = 0; I < 6; ++I)
	{
		Session.AddRacer(R.Sims[I].Get(), false);
	}
	Session.Start();
	double MaxTilt = 0.0;
	for (int32 I = 0; I < 6; ++I)
	{
		const FRiderSim& Sim = *R.Sims[I];
		const FRiderState& St = Sim.State;
		FRiderWorldHit Hit;
		const bool bHit = World.LineQuery(St.Position - FVector(0, 0, 200.0), St.Position + FVector(0, 0, 200.0), ERiderLineQuery::FineRefined, 0.0, Hit);
		const double Tilt = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(St.BasisUp.Z, -1.0, 1.0)));
		MaxTilt = FMath::Max(MaxTilt, Tilt);
		Report(this, FString::Printf(TEXT("start slot %d: z %.3f, tilt %.3f deg, surface %d"), I, St.Position.Z, Tilt, St.SurfaceId));
		TestTrue(TEXT("start: on the ground (the ray hit point)"), bHit && (Hit.Point - St.Position).Size() < 0.05);
		// 0x129008
		TestTrue(TEXT("start: body up = ground normal"), bHit && FVector::DotProduct(St.BasisUp.GetSafeNormal(), Hit.Normal.GetSafeNormal()) > 1.0 - 1e-8);
		TestTrue(TEXT("start: Static + Void, at rest"), St.Motion == ERiderMotion::Static && St.Control == ERiderControl::Void && St.Velocity.IsZero());
		TestTrue(TEXT("start: surface 1 forced with its [7] / [8]"), St.SurfaceId == 1 && St.SurfaceP7 == Sim.Surface(1).SinkDepth()
			&& St.SurfaceP8 == Sim.Surface(1).SinkMax());
		TestTrue(TEXT("start: +0x2D0 = +0x450 = position"), St.ContactPoint == St.Position && St.SpawnPosition == St.Position);
	}
	TestTrue(TEXT("start slots are sloped (tilt not zero)"), MaxTilt > 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceRepickMarkerTest, "SSX.Race.RepickMarker", RaceTestFlags)
bool FSSXRaceRepickMarkerTest::RunTest(const FString& Parameters)
{
	// 0x1969D8, 0x197B90
	FRacePath P;
	P.DistToGoAtStart = 1000.0;
	FRacePathEvent E;
	E.Type = SSXRaceEvent::Repick;
	E.Start = 100.0;
	E.End = 200.0;
	P.Events.Add(E);
	TestTrue(TEXT("inside, progressing (best = distance)"), FRaceLineProgress::RepickMarker(P, 850.0, 850.0));
	TestTrue(TEXT("inside, best ahead of the rider"), FRaceLineProgress::RepickMarker(P, 820.0, 880.0));
	TestFalse(TEXT("before the marker"), FRaceLineProgress::RepickMarker(P, 950.0, 950.0));
	TestFalse(TEXT("best already past its end"), FRaceLineProgress::RepickMarker(P, 750.0, 850.0));
	P.Events[0].Type = SSXRaceEvent::Repick - 1;
	TestFalse(TEXT("another type"), FRaceLineProgress::RepickMarker(P, 850.0, 850.0));
	return !HasAnyErrors();
}

#endif
