#include "Harness/RiderSetup.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/TriggerEffects.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderPlaceholderStates.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SSXStuck
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FRider
	{
		TUniquePtr<FRiderSim> Sim;
		TUniquePtr<SSXAnim::FRiderAnimDriver> Driver;
		bool bDriver = false;
		void Feed()
		{
			FRiderState& St = Sim->State;
			St.bPosedSpheresValid = false;
			if (!bDriver) { return; }
			SSXAnim::FRiderPalette Pal;
			Driver->ComputePalette(*Sim, Pal);
			TArray<FVector> Bones;
			for (const SSXAnim::FRowMat& M : Pal.Body) { Bones.Add(FVector(M.Row(3))); }
			St.bPosedSpheresValid = Pal.bValid && SSXRider::PosedBodyCentres(Bones.GetData(), Bones.Num(), St.Position, St.Orientation, St.PosedSpheres);
			if (St.bPosedSpheresValid) { St.PosedPivot = St.Orientation.Inverse().RotateVector(Bones[SSXRider::RootBone] - St.Position); }
		}
	};

	struct FRace
	{
		FRaceTrackData Data;
		FPatchWorld World;
		TArray<FRider> Riders;
		FRaceSession Session;

		bool Load(const FString& Stem, FString& Err)
		{
			return Data.Load(FRaceTrackData::DefaultPath(Stem), &Err) && World.LoadTrack(FPatchWorld::DefaultTrackPath(Stem), &Err);
		}
		void Make(int32 N, int32 Humans, ERaceEventMode Mode = ERaceEventMode::PracticeRace, bool bFullStart = false)
		{
			static const TCHAR* Names[] = { TEXT("moby"), TEXT("brodi") };
			static const int32 Chars[] = { 4, 10 };
			FRaceConfig Cfg;
			Cfg.Mode = Mode;
			Session.Init(Data, Cfg);
			for (int32 I = 0; I < N; ++I)
			{
				FRider& R = Riders.AddDefaulted_GetRef();
				R.Sim = MakeUnique<FRiderSim>();
				SSXRiderSetup::RegisterStates(*R.Sim, true);
				SSXRider::ApplyNewSavePlayerProfile(R.Sim->Profile, Chars[I & 1], 0);
				R.Sim->SetWorld(&World);
				R.Driver = MakeUnique<SSXAnim::FRiderAnimDriver>();
				R.bDriver = R.Driver->Init(SSXAnim::FAflLibrary::Shared(), Names[I & 1], R.Sim->Profile.BoardClass, Chars[I & 1]);
				Session.AddRacer(R.Sim.Get(), I < Humans);
			}
			Session.Start();
			if (bFullStart)
			{
				for (int32 F = 0; F < 60 * 60 && Session.GetPhase() != ERacePhase::Race; ++F) { Step(); }
				return;
			}
			Session.Tick();
			Session.SetPhase(ERacePhase::Race);
			Session.Tick();
		}
		void Step(const FRiderInputWord& HumanWord = FRiderInputWord())
		{
			Session.Tick();
			for (int32 I = 0; I < Riders.Num(); ++I)
			{
				FRider& R = Riders[I];
				R.Sim->Step(I == 0 ? HumanWord : FRiderInputWord());
				if (R.bDriver) { R.Driver->Step(*R.Sim); }
				R.Feed();
			}
		}
		void Place(int32 I, const FVector& Pos, const FVector& Vel)
		{
			FRiderSim& Sim = *Riders[I].Sim;
			Sim.ApplyTransition(TEXT("T03"));
			Sim.State.Position = Pos;
			Sim.State.Velocity = Vel;
			Sim.SetOrientationFromAxes(Vel.GetSafeNormal2D(), FVector::UpVector);
			Sim.RunGroundProbe();
			FRacerRecord& Rec = Session.GetRacerMutable(I);
			Rec.Race.Reproject(Data.Race, Pos);
			Rec.Line.Cache = FRacePathCache();
			Session.TrackScan(I);
		}
	};

	TArray<TPair<FVector, FVector>> OutOfBoundsSpots(const FPatchWorld& W)
	{
		TArray<TPair<FVector, FVector>> Out;
		for (int32 I = 0; I < W.NumPatches(); ++I)
		{
			const FSimPatch& Pa = W.GetPatch(I);
			if (Pa.Surface != FRiderSim::OutOfBoundsSurface) { continue; }
			FVector P, Du, Dv;
			Pa.Evaluate(0.5, 0.5, P, Du, Dv);
			FVector N = FVector::CrossProduct(Du, Dv).GetSafeNormal();
			if (N.Z < 0.0) { N = -N; }
			if (N.Z < 0.8) { continue; }
			FRiderWorldHit Hit;
			if (W.LineQuery(P + N * 300.0, P + N * 5.0, ERiderLineQuery::FineRefined, 0.5, Hit)) { continue; }
			Out.Add({ P, N });
		}
		return Out;
	}

	template <typename FRaceT>
	bool RideOutOfBounds(FRaceT& Race, int32 I, const TArray<TPair<FVector, FVector>>& Spots, int32 First)
	{
		FRiderSim& Sim = *Race.Riders[I].Sim;
		for (int32 K = 0; K < FMath::Min(Spots.Num(), 12); ++K)
		{
			const TPair<FVector, FVector>& Sp = Spots[(First + K) % Spots.Num()];
			const FVector Along = FVector::CrossProduct(Sp.Value, FVector(1, 0, 0)).GetSafeNormal();
			Race.Place(I, Sp.Key + Sp.Value * 5.0, Along * 300.0);
			for (int32 F = 0; F < 3 && Sim.State.Control != ERiderControl::ResetWait; ++F) { Race.Step(); }
			if (Sim.State.Control == ERiderControl::ResetWait && Sim.State.SurfaceId == FRiderSim::OutOfBoundsSurface) { return true; }
		}
		return false;
	}

	FString LastEvents(const FRiderSim& Sim, int32 N = 10)
	{
		const TArray<FRiderSimEvent>& Ev = Sim.GetEvents();
		FString Out;
		for (int32 K = FMath::Max(0, Ev.Num() - N); K < Ev.Num(); ++K)
		{
			const FRiderSimEvent& E = Ev[K];
			Out += FString::Printf(TEXT(" | f%d %s %s->%s (%s)"), E.Frame, E.bMotion ? TEXT("M") : TEXT("C"),
				E.bMotion ? LexToString(ERiderMotion(E.From)) : LexToString(ERiderControl(E.From)),
				E.bMotion ? LexToString(ERiderMotion(E.To)) : LexToString(ERiderControl(E.To)), E.Reason);
		}
		return Out;
	}

	struct FStats
	{
		int32 WipeOuts = 0, Stuck = 0, Episodes = 0;
		int32 Getup = 0, SelfRight = 0, Quick = 0, Reset = 0, Other = 0, Open = 0;
		int64 Frames = 0;
		int32 MaxFrames = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWipeOutNoStuckTest, "SSX.WipeOut.NoStuck", SSXStuck::Flags)

bool FSSXWipeOutNoStuckTest::RunTest(const FString& Parameters)
{
	using namespace SSXStuck;
	static const TCHAR* Tracks[] = { TEXT("elysium"), TEXT("gari"), TEXT("snow"), TEXT("mesa"), TEXT("merquer"), TEXT("aloha") };
	FStats All;
	FRandomStream Rng(0x5538);
	int32 TracksRun = 0;
	for (const TCHAR* Stem : Tracks)
	{
		FRace Race;
		FString Err;
		if (!Race.Load(Stem, Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s skipped: %s"), Stem, *Err));
			continue;
		}
		++TracksRun;
		Race.Make(6, 0);
		FStats T;
		const bool bEnd = FCString::Stricmp(Stem, TEXT("elysium")) == 0;
		const int32 Episodes = bEnd ? 24 : 10;
		for (int32 E = 0; E < Episodes; ++E)
		{
			++T.Episodes;
			const FRacerRecord& Rec0 = Race.Session.GetRacer(0);
			const int32 PathIdx = Race.Data.Race.AIPaths.IsValidIndex(Rec0.Race.Path) ? Rec0.Race.Path : 0;
			const FRacePath& Line = Race.Data.Race.AIPaths[PathIdx];
			const double L = Line.Length();
			const double Param = L * (bEnd ? Rng.FRandRange(0.75, 0.97) : Rng.FRandRange(0.05, 0.95));
			const FVector P = Line.PointAt(Param);
			const FVector Tan = Line.TangentAt(Param).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
			const FVector Side = FVector::CrossProduct(Tan, FVector::UpVector);
			const bool bDirect = (E & 1) != 0;
			for (int32 I = 0; I < 4; ++I)
			{
				const FVector Pos = P + Side * ((I - 1.5) * 110.0) + Tan * Rng.FRandRange(-60.0, 60.0) + FVector(0, 0, 40.0);
				const FVector Vel = Tan * Rng.FRandRange(1500.0, 2800.0) + Side * Rng.FRandRange(-1800.0, 1800.0);
				Race.Place(I, Pos, Vel);
				if (bDirect)
				{
					FRiderImpact Impact;
					Impact.Point = Pos;
					Impact.Normal = -Vel.GetSafeNormal();
					Impact.Direction = Vel.GetSafeNormal();
					Impact.Speed = Vel.Size();
					Race.Riders[I].Sim->StartWipeOut(Impact, TEXT("fuzz"));
				}
			}
			TArray<int32> Run, Before;
			Run.Init(0, 6);
			for (FRider& R : Race.Riders) { Before.Add(R.Sim->WatchdogReports); }
			for (int32 F = 0; F < 60 * 12; ++F)
			{
				TArray<ERiderMotion> Prev;
				for (FRider& R : Race.Riders) { Prev.Add(R.Sim->State.Motion); }
				Race.Step();
				for (int32 I = 0; I < Race.Riders.Num(); ++I)
				{
					const FRiderState& S = Race.Riders[I].Sim->State;
					if (S.Motion == ERiderMotion::WipeOut)
					{
						++Run[I];
					}
					else if (Prev[I] == ERiderMotion::WipeOut)
					{
						++T.WipeOuts;
						T.Frames += Run[I];
						T.MaxFrames = FMath::Max(T.MaxFrames, Run[I]);
						if (S.Motion == ERiderMotion::WipeOutRecover) { ++T.Getup; }
						else if (S.Motion == ERiderMotion::Air && S.Control == ERiderControl::NaturalAir) { ++T.SelfRight; }
						else if (S.Motion == ERiderMotion::Ground) { ++T.Quick; }
						else if (S.Control == ERiderControl::ResetWait) { ++T.Reset; }
						else { ++T.Other; }
						Run[I] = 0;
					}
				}
			}
			for (int32 I = 0; I < Race.Riders.Num(); ++I)
			{
				FRiderSim& Sim = *Race.Riders[I].Sim;
				if (Sim.State.Motion == ERiderMotion::WipeOut) { ++T.Open; }
				const int32 New = Sim.WatchdogReports - Before[I];
				if (New > 0 && Sim.State.Motion == ERiderMotion::WipeOut)
				{
					// 0x10C2F0, 0x139AB8
					AddInfo(FString::Printf(TEXT("%s episode %d: still sliding in WipeOut (no timeout in the original): %s"), Stem, E, *Sim.DumpState()));
				}
				else if (New > 0)
				{
					T.Stuck += New;
					AddError(FString::Printf(TEXT("%s episode %d: watchdog: %s"), Stem, E, *Sim.DumpState()));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d episodes, %d wipe-outs ended (get-up %d, self-right %d, quick %d, forced reset %d, other %d), still in WipeOut at 12 s %d, mean %.1f f, max %d f, stuck %d"),
			Stem, T.Episodes, T.WipeOuts, T.Getup, T.SelfRight, T.Quick, T.Reset, T.Other, T.Open,
			T.WipeOuts ? double(T.Frames) / T.WipeOuts : 0.0, T.MaxFrames, T.Stuck));
		All.WipeOuts += T.WipeOuts; All.Stuck += T.Stuck; All.Episodes += T.Episodes; All.Frames += T.Frames;
		All.MaxFrames = FMath::Max(All.MaxFrames, T.MaxFrames); All.Reset += T.Reset; All.Getup += T.Getup;
	}
	if (TracksRun == 0)
	{
		SSXTest::Skip(*this, TEXT("skipped: no race data"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("all: %d episodes, %d wipe-outs, mean %.1f f, max %d f (7 s timeout = 420 f), get-up %d, forced reset %d, stuck riders %d"),
		All.Episodes, All.WipeOuts, All.WipeOuts ? double(All.Frames) / All.WipeOuts : 0.0, All.MaxFrames, All.Getup, All.Reset, All.Stuck));
	TestTrue(TEXT("wipe-outs happened"), All.WipeOuts > 20);
	TestEqual(TEXT("no stuck riders"), All.Stuck, 0);
	TestTrue(TEXT("no wipe-out longer than the 7 s timeout + slide (11 s)"), All.MaxFrames <= int32(FRiderSim::WatchdogWipeOutLimit * 60.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWipeOutNoStuckHeatsTest, "SSX.WipeOut.NoStuckHeats", SSXStuck::Flags)

bool FSSXWipeOutNoStuckHeatsTest::RunTest(const FString& Parameters)
{
	using namespace SSXStuck;
	static const TCHAR* Tracks[] = { TEXT("gari"), TEXT("elysium"), TEXT("snow") };
	static const int32 Seeds[] = { 0x5401, 0x5402, 0x5403 };
	int32 TracksRun = 0, Stuck = 0, Episodes = 0, Resets = 0, WipeOuts = 0, LongWaits = 0, Heats = 0, CountdownWipes = 0, OobResets = 0;
	for (const TCHAR* Stem : Tracks)
	{
		const bool bGari = FCString::Stricmp(Stem, TEXT("gari")) == 0;
		const int32 NumSeeds = bGari ? 3 : 1;
		bool bLoaded = true;
		for (int32 Si = 0; Si < NumSeeds && bLoaded; ++Si)
		{
			FRandomStream Rng(Seeds[Si]);
			for (int32 Heat = 0; Heat < 3; ++Heat)
			{
				FRace Race;
				FString Err;
				if (!Race.Load(Stem, Err))
				{
					SSXTest::Skip(*this, FString::Printf(TEXT("%s skipped: %s"), Stem, *Err));
					bLoaded = false;
					break;
				}
				if (Heat == 0 && Si == 0) { ++TracksRun; }
				++Heats;
				Race.Make(6, 1, ERaceEventMode::CircuitRace, false);
				Race.Session.Start();
				Race.Step();
				for (int32 F = 0; F < 60 * 60 && Race.Session.GetPhase() != ERacePhase::Race; ++F)
				{
					if (Race.Session.GetPhase() == ERacePhase::Countdown && Rng.FRand() < 0.01f)
					{
						FRiderSim& V = *Race.Riders[Rng.RandHelper(6)].Sim;
						FRiderImpact Impact;
						Impact.Point = V.State.Position;
						Impact.Normal = FVector(Rng.FRandRange(-1.0, 1.0), Rng.FRandRange(-1.0, 1.0), 0.3).GetSafeNormal();
						Impact.Speed = 800.0;
						V.StartWipeOut(Impact, TEXT("fuzz countdown"));
						++CountdownWipes;
					}
					TArray<int32> R0;
					for (FRider& R : Race.Riders) { R0.Add(R.Sim->WatchdogReports); }
					Race.Step();
					for (int32 I = 0; I < Race.Riders.Num(); ++I)
					{
						if (Race.Riders[I].Sim->WatchdogReports != R0[I])
						{
							AddInfo(FString::Printf(TEXT("%s seed %d heat %d start phase %d: watchdog: %s%s"), Stem, Si, Heat + 1, int32(Race.Session.GetPhase()),
								*Race.Riders[I].Sim->DumpState(), *LastEvents(*Race.Riders[I].Sim, 30)));
						}
					}
				}
				if (!TestTrue(FString::Printf(TEXT("%s seed %d heat %d: GO"), Stem, Si, Heat + 1), Race.Session.GetPhase() == ERacePhase::Race))
				{
					continue;
				}
				const TArray<TPair<FVector, FVector>> Oob = OutOfBoundsSpots(Race.World);
				TArray<int32> Before, Wait, Seen, Excused;
				for (FRider& R : Race.Riders) { Before.Add(R.Sim->WatchdogReports); }
				Seen = Before;
				Excused.Init(0, Race.Riders.Num());
				Wait.Init(0, Race.Riders.Num());
				const int32 NumEpisodes = 8;
				for (int32 E = 0; E < NumEpisodes; ++E)
				{
					++Episodes;
					const int32 Kind = (E + Heat + Si) % 8;
					const FRacePath& Line = Race.Data.Race.AIPaths[Race.Session.GetRacer(0).Race.Path];
					const double Param = Line.Length() * Rng.FRandRange(0.05, 0.95);
					const FVector P = Line.PointAt(Param);
					const FVector Tan = Line.TangentAt(Param).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
					const FVector Side = FVector::CrossProduct(Tan, FVector::UpVector);
					const int32 Group = Kind == 4 ? 2 : 4;
					for (int32 I = 0; I < Group; ++I)
					{
						const FVector Pos = P + Side * ((I - 1.5) * 110.0) + Tan * Rng.FRandRange(-60.0, 60.0) + FVector(0, 0, 40.0);
						const FVector Vel = Tan * Rng.FRandRange(1500.0, 2800.0) + Side * Rng.FRandRange(-1800.0, 1800.0);
						Race.Place(I, Pos, Vel);
						FRiderSim& Sim = *Race.Riders[I].Sim;
						if (Kind == 1 || Kind == 3)
						{
							FRiderImpact Impact;
							Impact.Point = Pos;
							Impact.Normal = -Vel.GetSafeNormal();
							Impact.Direction = Vel.GetSafeNormal();
							Impact.Speed = Vel.Size();
							Sim.StartWipeOut(Impact, TEXT("fuzz"));
						}
						else if (Kind == 4)
						{
							Sim.Spawn(Pos, Tan, Vel, Rng.FRandRange(800.0, 2000.0), false);
						}
						else if (Kind == 2 && I == 0)
						{
							OobResets += RideOutOfBounds(Race, 0, Oob, Rng.RandHelper(1000));
						}
						else if (Kind == 6 && I == 0)
						{
							Sim.State.Position.Z = Sim.GetWorldFloor() - FRiderSim::FloorMargin - 100.0;
						}
					}
					const FBox Zone(P - FVector(500.0), P + FVector(500.0));
					for (int32 F = 0; F < 60 * 10; ++F)
					{
						if (Kind == 3 && F < 45)
						{
							for (FRider& R : Race.Riders)
							{
								if (Zone.IsInside(R.Sim->State.Position)) { SSXTriggers::ApplyForcedReset(*R.Sim); }
							}
						}
						FRiderInputWord W;
						W.bResetRequest = (Kind == 5 || Kind == 7) && F % 97 == 13;
						TArray<ERiderControl> Prev;
						TArray<ERiderMotion> PrevM;
						for (FRider& R : Race.Riders) { Prev.Add(R.Sim->State.Control); PrevM.Add(R.Sim->State.Motion); }
						Race.Step(W);
						for (int32 I = 0; I < Race.Riders.Num(); ++I)
						{
							const FRiderState& S = Race.Riders[I].Sim->State;
							const bool bWait = S.Control == ERiderControl::ResetWait;
							Resets += bWait && Prev[I] != ERiderControl::ResetWait;
							WipeOuts += S.Motion == ERiderMotion::WipeOut && PrevM[I] != ERiderMotion::WipeOut;
							if (Race.Riders[I].Sim->WatchdogReports != Seen[I])
							{
								Seen[I] = Race.Riders[I].Sim->WatchdogReports;
								// 0x11B348, 0x114730, 0x126748, 0x117198, 0x123FC8, 0x11D4E8, 0x236A00, 0x124920, 0x106A48
								const FString Ev = LastEvents(*Race.Riders[I].Sim, 12);
								if (Ev.Contains(TEXT("ResetWait->WipeOut (T60 rider hit)")))
								{
									++Excused[I];
									AddInfo(FString::Printf(TEXT("%s seed %d heat %d episode %d: reset loop through rider hits on a waiting rider: %s%s"),
										Stem, Si, Heat + 1, E, *Race.Riders[I].Sim->DumpState(), *Ev));
								}
							}
							Wait[I] = bWait && S.ControlTime > 0.82 ? Wait[I] + 1 : 0;
							if (Wait[I] == 1) { ++LongWaits; AddError(FString::Printf(TEXT("%s seed %d heat %d episode %d: ResetWait > 0.8 s: %s%s"), Stem, Si, Heat + 1, E, *Race.Riders[I].Sim->DumpState(), *LastEvents(*Race.Riders[I].Sim))); }
						}
					}
				}
				for (int32 I = 0; I < Race.Riders.Num(); ++I)
				{
					FRiderSim& Sim = *Race.Riders[I].Sim;
					const int32 New = Sim.WatchdogReports - Before[I] - Excused[I];
					if (New > 0)
					{
						Stuck += New;
						AddError(FString::Printf(TEXT("%s seed %d heat %d: %d watchdog report(s): %s%s"), Stem, Si, Heat + 1, New, *Sim.DumpState(), *LastEvents(Sim, 24)));
					}
				}
			}
		}
	}
	if (TracksRun == 0)
	{
		SSXTest::Skip(*this, TEXT("skipped: no race data"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d heats, %d episodes: %d wipe-outs (%d during countdowns), %d forced resets (%d from out-of-bounds patches), %d ResetWaits over 0.8 s, %d watchdog reports"),
		Heats, Episodes, WipeOuts, CountdownWipes, Resets, OobResets, LongWaits, Stuck));
	TestTrue(TEXT("out-of-bounds resets happened"), OobResets > 3);
	TestTrue(TEXT("wipe-outs and resets happened"), WipeOuts > 50 && Resets > 20);
	TestEqual(TEXT("no stuck rider (watchdog)"), Stuck, 0);
	TestEqual(TEXT("every ResetWait ends after 0.8 s"), LongWaits, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRaceResetTest, "SSX.Race.Reset", SSXStuck::Flags)

bool FSSXRaceResetTest::RunTest(const FString& Parameters)
{
	using namespace SSXStuck;
	FRace Race;
	FString Err;
	if (!Race.Load(TEXT("elysium"), Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s"), *Err));
		return true;
	}
	TestTrue(TEXT("Elysium track id 2"), Race.Data.TrackId == 2);
	Race.Make(2, 1);
	FRiderSim& Sim = *Race.Riders[0].Sim;
	const FRacerRecord& Rec = Race.Session.GetRacer(0);
	const FRacePath& Line = Race.Data.Race.AIPaths[Rec.Race.Path];
	auto Crash = [&](double Param)
	{
		const FVector P = Line.PointAt(Param) + FVector(0, 0, 40.0);
		const FVector Tan = Line.TangentAt(Param).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
		Race.Place(0, P, Tan * 2000.0);
		FRiderImpact Impact;
		Impact.Point = P;
		Impact.Normal = -Tan;
		Impact.Speed = 2000.0;
		Sim.StartWipeOut(Impact, TEXT("test"));
	};
	auto RunUntilRespawn = [&](int32 MaxFrames, const FRiderInputWord& W) -> int32
	{
		int32 F = 0;
		while (Sim.State.Control != ERiderControl::NaturalAir && F < MaxFrames) { Race.Step(W); ++F; }
		return F;
	};
	FRiderInputWord Select;
	Select.bResetRequest = true;
	const double Param = Line.Length() * 0.9;
	Crash(Param);
	Race.Step();
	TestTrue(TEXT("tumbling"), Sim.State.Motion == ERiderMotion::WipeOut && Sim.State.Control == ERiderControl::WipeOut);
	Race.Step(Select);
	TestTrue(TEXT("Select in WipeOut -> Static + ResetWait"), Sim.State.Motion == ERiderMotion::Static && Sim.State.Control == ERiderControl::ResetWait);
	const int32 Fa = RunUntilRespawn(120, FRiderInputWord());
	const double Err0 = FVector::Dist2D(Sim.State.Position, Line.PointAt(Sim.State.PathParam));
	AddInfo(FString::Printf(TEXT("Select in WipeOut: respawn after %d frames, %s/%s, %.1f cm from the race line, path %.0f of %.0f"),
		Fa, LexToString(Sim.State.Motion), LexToString(Sim.State.Control), Err0, Sim.State.PathParam, Line.Length()));
	TestTrue(TEXT("respawn after the 0.8 s ResetWait"), Fa >= 47 && Fa <= 50);
	TestTrue(TEXT("respawned on the race line"), Err0 < 900.0 && Sim.State.Motion == ERiderMotion::Air);
	Crash(Param);
	int32 F = 0;
	while (Sim.State.Motion != ERiderMotion::WipeOutRecover && F < 600) { Race.Step(); ++F; }
	if (Sim.State.Motion == ERiderMotion::WipeOutRecover)
	{
		Race.Step(Select);
		TestTrue(TEXT("Select in WipeOutRecover -> ResetWait"), Sim.State.Control == ERiderControl::ResetWait);
	}
	else
	{
		AddInfo(FString::Printf(TEXT("(b) wipe-out ended without a get-up: %s"), *Sim.DumpState()));
	}
	RunUntilRespawn(120, FRiderInputWord());
	Crash(Param);
	Race.Step();
	if (auto* Motion = static_cast<SSXSimPlaceholders::FWipeOutMotion*>(Sim.GetMotionState(ERiderMotion::WipeOut)))
	{
		Motion->GetContext().TotalTime = 7.01;
	}
	F = 0;
	while (Sim.State.Control != ERiderControl::ResetWait && F < 3) { Race.Step(); ++F; }
	TestTrue(TEXT("timeout -> forced reset within two frames"), Sim.State.Control == ERiderControl::ResetWait && Sim.State.bAutoReset);
	const int32 Fc = RunUntilRespawn(120, FRiderInputWord());
	TestTrue(TEXT("timeout reset respawns"), Sim.State.Control == ERiderControl::NaturalAir && Fc <= 50);
	{
		Crash(Param);
		FRiderSim& Ai = *Race.Riders[1].Sim;
		const FVector Tan = Line.TangentAt(Param).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
		Race.Place(1, Sim.State.Position + FVector::CrossProduct(Tan, FVector::UpVector) * 150.0, Tan * 1800.0);
		FRiderImpact Impact;
		Impact.Point = Ai.State.Position;
		Impact.Normal = -Tan;
		Ai.StartWipeOut(Impact, TEXT("test"));
		Race.Step();
		const FBox Zone(Sim.State.Position - FVector(600.0), Sim.State.Position + FVector(600.0));
		int32 Resets = 0, LongestWait = 0, PlayerBack = -1;
		TArray<int32> Wait;
		Wait.Init(0, Race.Riders.Num());
		for (int32 Fd = 0; Fd < 180; ++Fd)
		{
			for (FRider& R : Race.Riders)
			{
				if (Zone.IsInside(R.Sim->State.Position)) { SSXTriggers::ApplyForcedReset(*R.Sim); }
			}
			Race.Step();
			for (int32 I = 0; I < Race.Riders.Num(); ++I)
			{
				const bool bWait = Race.Riders[I].Sim->State.Control == ERiderControl::ResetWait;
				Resets += bWait && Wait[I] == 0;
				Wait[I] = bWait ? Wait[I] + 1 : 0;
				LongestWait = FMath::Max(LongestWait, Wait[I]);
			}
			if (PlayerBack < 0 && Sim.State.Control == ERiderControl::NaturalAir) { PlayerBack = Fd + 1; }
		}
		AddInfo(FString::Printf(TEXT("reset zone: %d forced resets, longest ResetWait %d frames, player respawned after %d frames (AI now %s/%s)"),
			Resets, LongestWait, PlayerBack, LexToString(Ai.State.Motion), LexToString(Ai.State.Control)));
		TestTrue(TEXT("reset zone: no endless ResetWait (0.8 s each)"), Resets >= 1 && LongestWait <= 49);
		TestTrue(TEXT("reset zone: the player respawns after 0.8 s"), PlayerBack > 0 && PlayerBack <= 50);
	}
	{
		const TArray<TPair<FVector, FVector>> Spots = OutOfBoundsSpots(Race.World);
		const bool bOob = RideOutOfBounds(Race, 0, Spots, 0);
		AddInfo(FString::Printf(TEXT("elysium: %d out-of-bounds spots"), Spots.Num()));
		TestTrue(TEXT("riding onto surface 0 -> ResetWait (auto reset)"), bOob && Sim.State.bAutoReset);
		int32 Resets = 0, AirFrames = 0;
		ERiderControl Prev = Sim.State.Control;
		for (int32 F2 = 0; F2 < 60 * 6; ++F2)
		{
			Race.Step();
			Resets += Sim.State.Control == ERiderControl::ResetWait && Prev != ERiderControl::ResetWait;
			AirFrames += Sim.State.Motion == ERiderMotion::Air;
			Prev = Sim.State.Control;
		}
		AddInfo(FString::Printf(TEXT("out-of-bounds reset: %d further resets in 6 s, %d air frames, now %s/%s surface %d"),
			Resets, AirFrames, LexToString(Sim.State.Motion), LexToString(Sim.State.Control), Sim.State.SurfaceId));
		TestEqual(TEXT("out-of-bounds reset: the respawn does not reset again (no reset loop)"), Resets, 0);
		TestTrue(TEXT("out-of-bounds reset: the rider respawns and lands"), AirFrames > 0 && Sim.State.Control != ERiderControl::ResetWait);
	}
	TestEqual(TEXT("no watchdog report"), Sim.WatchdogReports, 0);

	// 0x106A90
	for (const TCHAR* Stem : { TEXT("gari"), TEXT("elysium") })
	{
		for (int32 Heat = 0; Heat < 3; ++Heat)
		{
			FRace H;
			if (!H.Load(Stem, Err)) { SSXTest::Skip(*this, FString::Printf(TEXT("%s skipped: %s"), Stem, *Err)); break; }
			H.Make(6, 1, ERaceEventMode::CircuitRace, true);
			FRiderSim& P = *H.Riders[0].Sim;
			if (!TestTrue(FString::Printf(TEXT("%s heat %d: GO reached"), Stem, Heat + 1), H.Session.GetPhase() == ERacePhase::Race)) { continue; }
			const FRacePath& L = H.Data.Race.AIPaths[H.Session.GetRacer(0).Race.Path];
			const double At = L.Length() * (0.3 + 0.2 * Heat);
			const FVector Tan = L.TangentAt(At).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
			auto Respawned = [&]() { int32 F2 = 0; while (P.State.Control != ERiderControl::NaturalAir && F2 < 120) { H.Step(); ++F2; } return F2; };
			int32 Ok = 0, Cases = 0;
			FString Fail;
			auto Expect = [&](const TCHAR* What, bool bCond) { ++Cases; Ok += bCond; if (!bCond) { Fail += FString::Printf(TEXT(" %s (%s/%s)"), What, LexToString(P.State.Motion), LexToString(P.State.Control)); } };
			H.Place(0, L.PointAt(At) + FVector(0, 0, 40.0), Tan * 1500.0);
			H.Step();
			const ERiderControl Ground = P.State.Control;
			H.Step(Select);
			Expect(TEXT("Select on the ground"), P.State.Control == ERiderControl::ResetWait && !P.State.bAutoReset);
			int32 F2 = 0;
			while (P.State.Control == ERiderControl::ResetWait && F2 < 120) { H.Step(Select); ++F2; }
			Expect(TEXT("Select held in ResetWait: respawn after 0.8 s"), F2 >= 47 && F2 <= 50 && P.State.Control == ERiderControl::NaturalAir);
			H.Step();
			H.Step(Select);
			Expect(TEXT("Select in NaturalAir after the respawn"), P.State.Control == ERiderControl::ResetWait);
			Respawned();
			H.Place(0, L.PointAt(At) + FVector(0, 0, 40.0), Tan * 1500.0);
			P.Spawn(L.PointAt(At), Tan, Tan * 1500.0, 800.0, false);
			H.Step();
			H.Step(Select);
			Expect(TEXT("Select in a drop"), P.State.Control == ERiderControl::ResetWait);
			Respawned();
			H.Place(0, L.PointAt(At) + FVector(0, 0, 40.0), Tan * 2000.0);
			FRiderImpact Impact;
			Impact.Point = P.State.Position;
			Impact.Normal = -Tan;
			Impact.Speed = 2000.0;
			P.StartWipeOut(Impact, TEXT("test"));
			H.Step();
			H.Step(Select);
			Expect(TEXT("Select in WipeOut"), P.State.Control == ERiderControl::ResetWait);
			Respawned();
			Expect(TEXT("surface 0 resets"), RideOutOfBounds(H, 0, OutOfBoundsSpots(H.World), Heat * 5));
			Respawned();
			int32 Again = 0;
			for (int32 F3 = 0; F3 < 120; ++F3) { H.Step(); Again += P.State.Control == ERiderControl::ResetWait; }
			Expect(TEXT("no reset after the out-of-bounds respawn"), Again == 0);
			AddInfo(FString::Printf(TEXT("%s heat %d (ground control %s): Select %d of %d cases%s; watchdog %d"), Stem, Heat + 1,
				LexToString(Ground), Ok, Cases, *Fail, P.WatchdogReports));
			TestEqual(FString::Printf(TEXT("%s heat %d: Select / reset cases"), Stem, Heat + 1), Ok, Cases);
			TestEqual(FString::Printf(TEXT("%s heat %d: no watchdog report"), Stem, Heat + 1), P.WatchdogReports, 0);
		}
	}
	{
		FRace H;
		if (H.Load(TEXT("gari"), Err))
		{
			H.Make(6, 1, ERaceEventMode::CircuitRace);
			H.Session.Start();
			H.Step();
			int32 Resets = 0, F2 = 0;
			for (; F2 < 60 * 60 && H.Session.GetPhase() != ERacePhase::Race; ++F2)
			{
				H.Step(Select);
				Resets += H.Riders[0].Sim->State.Control == ERiderControl::ResetWait;
			}
			AddInfo(FString::Printf(TEXT("Select held through the start (%d frames): %d ResetWait frames"), F2, Resets));
			TestEqual(TEXT("Select does nothing before GO"), Resets, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCollisionShoveKnockdownTest, "SSX.Collision.ShoveKnockdown", SSXStuck::Flags)

bool FSSXCollisionShoveKnockdownTest::RunTest(const FString& Parameters)
{
	using namespace SSXStuck;
	FRace Race;
	FString Err;
	if (!Race.Load(TEXT("gari"), Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s"), *Err));
		return true;
	}
	for (const bool bAiTarget : { false, true })
	{
		FRace R;
		R.Load(TEXT("gari"), Err);
		R.Make(2, bAiTarget ? 1 : 2);   // target racer 1: AI pad, or a second human with a neutral pad
		FRiderSim& T = *R.Riders[1].Sim;
		const FRacerRecord& Rec = R.Session.GetRacer(0);
		const FRacePath& Line = R.Data.Race.AIPaths[Rec.Race.Path];
		int32 Down = 0, Launched = 0, Cases = 0, MultiHit = 0;
		FString All;
		for (int32 Case = 0; Case < 4; ++Case)
		{
			const double Param = Line.Length() * (0.2 + 0.15 * Case);
			const FVector At = Line.PointAt(Param) + FVector(0, 0, 30.0);
			const FVector Tan = Line.TangentAt(Param).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
			const FVector Right = FVector::CrossProduct(Tan, FVector::UpVector);   // body +X for forward = Tan, up = Z
			R.Place(0, At, Tan * 1500.0);
			R.Place(1, At + Right * 80.0, Tan * 1500.0);
			T.State.BoostStrength = 0.0;
			R.Step();
			FRacerRecord& R0 = R.Session.GetRacerMutable(0);
			R0.ShoveHitJ.Reset();
			FRiderInputWord Shove;
			Shove.ShoveSide = -1;   // right stick right (raw 3)
			R.Step(Shove);
			bool bDown = false, bLaunch = false;
			for (int32 F = 0; F < 50 && !bDown && !bLaunch; ++F)
			{
				R.Step();
				bDown = T.State.Motion == ERiderMotion::WipeOut;
				bLaunch = T.State.Motion == ERiderMotion::Air && T.State.LaunchCharge > 0.0;
			}
			++Cases;
			Down += bDown;
			Launched += bLaunch;
			MultiHit += R0.ShoveHitJ.Num() > 1 && R0.ShoveHitJ.Last() > R0.ShoveHitJ[0];
			FString Js;
			for (double J : R0.ShoveHitJ) { Js += FString::Printf(TEXT(" %.0f"), J); }
			All += FString::Printf(TEXT(" | case %d: J%s -> %s"), Case, *Js, bDown ? TEXT("KNOCKDOWN") : (bLaunch ? TEXT("launched (|J| > 1000, 8.2)") : TEXT("standing")));
			R.Place(1, At + Right * 3000.0, Tan * 1500.0);
			for (int32 F = 0; F < 60; ++F) { R.Step(); }
		}
		AddInfo(FString::Printf(TEXT("shove vs %s, both 1500 cm/s, 80 cm apart (threshold 733.97):%s; knockdowns %d, launches %d of %d"),
			bAiTarget ? TEXT("AI (own pad, info only)") : TEXT("neutral rider"), *All, Down, Launched, Cases));
		if (!bAiTarget)
		{
			TestTrue(TEXT("repeat hits grow J (6.6 x relative velocity)"), MultiHit >= 3);
			// 0x123FC8
			TestTrue(TEXT("shoves knock a neutral rider down (or launch it, 8.2)"), Down + Launched >= 1);
		}
	}
	return true;
}

#endif
