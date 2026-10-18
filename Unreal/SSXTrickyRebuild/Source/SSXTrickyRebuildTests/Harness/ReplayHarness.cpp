#include "Harness/ReplayHarness.h"

#include "Harness/RiderSetup.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Ground/GroundStates.h"
#include "Input/InpFile.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Misc/CommandLine.h"
#include "Containers/Ticker.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sim/PatchWorld.h"
#include "SSXTrickyRebuild.h"

namespace
{
	void Say(const FString& Line)
	{
		UE_LOG(LogTemp, Display, TEXT("[SSX.Replay] %s"), *Line);
	}

	FString StateKey(const FReplayRow& R) { return R.Motion + TEXT("/") + R.Control; }

	FString OwnerOf(const FString& Motion, const FString& Control)
	{
		static const TSet<FString> GroundControls = { TEXT("CRUISE"), TEXT("PREWIND"), TEXT("JUMP"), TEXT("CHANGE_FAKIE"),
			TEXT("FAKIE_CRUISE"), TEXT("BUMP"), TEXT("LAND_NORMAL"), TEXT("LAND_HARD"), TEXT("GATE_TO_BASE"),
			TEXT("RAIL_SLIDE"), TEXT("GATE_ANTICIPATE"), TEXT("GATE_LAUNCH") };
		static const TSet<FString> AirControls = { TEXT("NATURAL_AIR"), TEXT("SPIN") };
		if (Motion == TEXT("AIR") || AirControls.Contains(Control))
		{
			return TEXT("air / tricks / landing (P3, Sim/Air)");
		}
		if (Motion.StartsWith(TEXT("WIPE_OUT")) || Control.StartsWith(TEXT("WIPE_OUT")))
		{
			return TEXT("wipe-out (Sim/RiderPlaceholderStates)");
		}
		if (Motion == TEXT("GROUND") || Motion == TEXT("RAIL_SLIDE") || GroundControls.Contains(Control))
		{
			return TEXT("ground / rail (P2, Sim/Ground)");
		}
		return TEXT("core frame / reset / lesson (P1 Sim core, P4 harness)");
	}

	FString RowText(const FReplayRow& R)
	{
		return FString::Printf(TEXT("f%04d pos (%.3f %.3f %.3f) vel (%.3f %.3f %.3f) %s/%s steer %.4f crouch %.4f surf %d word %u"),
			R.Frame, R.Pos.X, R.Pos.Y, R.Pos.Z, R.Vel.X, R.Vel.Y, R.Vel.Z, *R.Motion, *R.Control, R.Steer, R.Crouch, R.Surface, R.WordModel);
	}
}

namespace SSXReplay
{
	const TCHAR* PyName(ERiderMotion M)
	{
		static const TCHAR* Names[] = { TEXT("NONE"), TEXT("AIR"), TEXT("GROUND"), TEXT("RAIL_SLIDE"), TEXT("STATIC"),
			TEXT("WIPE_OUT"), TEXT("WIPE_OUT_RECOVER") };
		const int32 I = int32(M);
		return (I >= 0 && I < UE_ARRAY_COUNT(Names)) ? Names[I] : TEXT("?");
	}

	const TCHAR* PyName(ERiderControl C)
	{
		static const TCHAR* Names[] = { TEXT("NONE"), TEXT("VOID"), TEXT("BUMP"), TEXT("CRUISE"), TEXT("CHANGE_FAKIE"),
			TEXT("FAKIE_CRUISE"), TEXT("FINISH_LINE"), TEXT("GATE_TO_BASE"), TEXT("GATE_ANTICIPATE"), TEXT("GATE_LAUNCH"),
			TEXT("JUMP"), TEXT("LAND_HARD"), TEXT("LAND_NORMAL"), TEXT("NATURAL_AIR"), TEXT("PREWIND"), TEXT("RAIL_SLIDE"),
			TEXT("SPIN"), TEXT("SIT_AND_WAIT"), TEXT("GETUP_FROM_SIT"), TEXT("WIPE_OUT"), TEXT("WIPE_OUT_RECOVER"),
			TEXT("LESSON_WAIT"), TEXT("RESET_WAIT") };
		const int32 I = int32(C);
		return (I >= 0 && I < UE_ARRAY_COUNT(Names)) ? Names[I] : TEXT("?");
	}

	FString OutputPath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Replay"), Name + TEXT(".csv")));
	}

	FString ReferencePath(const FString& Name)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("replay"), Name + TEXT(".csv")));
	}

	FString RunName(const FReplayOptions& Options)
	{
		return FPaths::GetBaseFilename(Options.InpPath).ToLower() + TEXT("_") + Options.World.ToLower();
	}

	bool Run(const FReplayOptions& Options, FReplayResult& Out)
	{
		Out = FReplayResult();
		FString InpPath = Options.InpPath;
		if (!FPaths::FileExists(InpPath))
		{
			InpPath = FInpFile::TutorialPath(InpPath);
		}
		TArray<FInpFrame> Frames;
		if (!FInpFile::Load(InpPath, Frames, &Out.Error))
		{
			return false;
		}

		int32 Character = 0, BoardClass = 0;
		const bool bTutorialRider = FPadMapper::TutorialRider(InpPath, Character, BoardClass);
		FPadMapper Mapper(bTutorialRider ? Character : INDEX_NONE, BoardClass);

		const FString WorldName = Options.World.ToLower();
		FPatchWorld World;
		const bool bSlope = WorldName.StartsWith(TEXT("slope"));
		double SlopeDeg = 15.0;
		if (bSlope)
		{
			const FString Deg = WorldName.RightChop(5);
			if (!Deg.IsEmpty()) { SlopeDeg = FCString::Atod(*Deg); }
			SSXRiderSetup::BuildSlopeWorld(World, SlopeDeg, 1);
		}
		else if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(WorldName), &Out.Error))
		{
			return false;
		}

		FRiderSim Sim;
		FRiderInputDriver Driver;
		Driver.Install(Sim, [](FRiderSim& Registry)
		{
			FGroundConfig Cfg;
			Cfg.StatOverride = 0.5;
			Cfg.bUsePathHeading = true;
			RegisterGroundStates(Registry, Cfg);
			RegisterAirStates(Registry);
		});
		Out.States = SSXRiderSetup::DescribeStates() + TEXT(" (ground stats 0.5, path heading on)");
		Sim.SetWorld(&World);
		Sim.Profile.Character = Character;
		Sim.Profile.TrickCharacter = Character;
		Sim.Profile.BoardClass = BoardClass;
		Sim.Reset();

		FVector Pos = FVector::ZeroVector, Heading = FVector(0, 1, 0);
		double Speed = 0.0;
		if (bSlope)
		{
			Out.SpawnInfo = FString::Printf(TEXT("slope %.1f deg, race-line origin, at rest"), SlopeDeg);
		}
		else if (Options.bHaveSpawn)
		{
			Pos = Options.SpawnPos;
			Heading = Options.SpawnHeading;
			Speed = Options.SpawnSpeed;
			Out.SpawnInfo = FString::Printf(TEXT("given spawn (%.1f %.1f %.1f)"), Pos.X, Pos.Y, Pos.Z);
		}
		else
		{
			FVector Marker, MarkerFwd;
			int32 Tried = 0;
			if (!World.GetStartMarker(Marker, MarkerFwd) || !SSXRiderSetup::FindGentleStart(World, Marker, MarkerFwd, Pos, Heading, &Tried))
			{
				Out.Error = TEXT("no spawn: pass -SSXSpawn=x,y,z (game space)");
				return false;
			}
			Out.SpawnInfo = FString::Printf(TEXT("P1 start rule, candidate %d, (%.1f %.1f %.1f)"), Tried, Pos.X, Pos.Y, Pos.Z);
		}
		const FVector H = FVector(Heading.X, Heading.Y, 0.0).GetSafeNormal();
		Sim.Spawn(Pos, H, H * Speed, 0.0, false);
		SSXRiderSetup::RestInSnow(Sim);
		if (Speed > 0.0)
		{
			Sim.State.Velocity = Sim.State.BasisForward * Speed;
		}
		Sim.SetControl(ERiderControl::LessonWait, TEXT("T08 lesson placement"));

		const double SlopeTan = FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
		const int32 N = Options.MaxFrames > 0 ? FMath::Min(Options.MaxFrames, Frames.Num()) : Frames.Num();
		Out.Rows.Reserve(N);
		for (int32 I = 0; I < N; ++I)
		{
			if (bSlope)
			{
				const FVector U = FVector(0.0, 1.0, -SlopeTan).GetSafeNormal();
				Sim.State.PathPoint = FVector::DotProduct(Sim.State.Position, U) * U;
				Sim.State.PathHeading = UE_DOUBLE_HALF_PI;
			}
			const FPadActions Actions = Mapper.Step(Frames[I], /*bUseRecordedMasks=*/false);
			Driver.Step(Sim, Actions);

			const FRiderState& S = Sim.State;
			FReplayRow& R = Out.Rows.AddDefaulted_GetRef();
			R.Frame = I;
			R.T = I * FRiderSim::Dt;
			R.Pos = S.Position;
			R.Vel = S.Velocity;
			R.Speed = S.Velocity.Size();
			R.Kmh = R.Speed * 0.036;
			R.Motion = PyName(S.Motion);
			R.Control = PyName(S.Control);
			R.BoostMeter = S.BoostMeter;
			R.TrickyTimer = S.TrickyTimer;
			R.Steer = S.Steer.Cur;
			R.Crouch = S.Crouch.Cur;
			R.Surface = S.SurfaceId;
			R.WordModel = Driver.GetLastWord();
			R.WordRecorded = Frames[I].RecordedWord;
			R.bWordMatch = R.WordModel == R.WordRecorded;
		}
		Out.Events = Sim.GetEvents();
		Out.bOk = true;
		return true;
	}

	bool WriteCsv(const FString& Path, const TArray<FReplayRow>& Rows)
	{
		TArray<FString> Lines;
		Lines.Reserve(Rows.Num() + 1);
		Lines.Add(TEXT("frame,t,x,y,z,vx,vy,vz,speed,kmh,motion,control,boost_meter,tricky_timer,steer,crouch,surface,word_model,word_recorded,word_match"));
		for (const FReplayRow& R : Rows)
		{
			Lines.Add(FString::Printf(TEXT("%d,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%s,%s,%.5f,%.3f,%.4f,%.4f,%d,%u,%u,%d"),
				R.Frame, R.T, R.Pos.X, R.Pos.Y, R.Pos.Z, R.Vel.X, R.Vel.Y, R.Vel.Z, R.Speed, R.Kmh, *R.Motion, *R.Control,
				R.BoostMeter, R.TrickyTimer, R.Steer, R.Crouch, R.Surface, R.WordModel, R.WordRecorded, R.bWordMatch ? 1 : 0));
		}
		return FFileHelper::SaveStringArrayToFile(Lines, *Path);
	}

	bool LoadCsv(const FString& Path, TArray<FReplayRow>& Rows, FString* OutError)
	{
		Rows.Reset();
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 1)
		{
			if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s (run tools/make_replay_reference.py)"), *Path); }
			return false;
		}
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		auto Col = [&Header](const TCHAR* Name) { return Header.IndexOfByKey(FString(Name)); };
		const int32 CFrame = Col(TEXT("frame")), CT = Col(TEXT("t")), CX = Col(TEXT("x")), CVX = Col(TEXT("vx")),
			CSpeed = Col(TEXT("speed")), CKmh = Col(TEXT("kmh")), CMotion = Col(TEXT("motion")), CControl = Col(TEXT("control")),
			CBoost = Col(TEXT("boost_meter")), CTricky = Col(TEXT("tricky_timer")), CSteer = Col(TEXT("steer")),
			CCrouch = Col(TEXT("crouch")), CSurf = Col(TEXT("surface")), CWm = Col(TEXT("word_model")),
			CWr = Col(TEXT("word_recorded")), CMatch = Col(TEXT("word_match"));
		if (CFrame < 0 || CX < 0 || CVX < 0 || CMotion < 0 || CControl < 0)
		{
			if (OutError) { *OutError = TEXT("CSV header lacks the models/boarder.py LOG_FIELDS columns"); }
			return false;
		}
		for (int32 L = 1; L < Lines.Num(); ++L)
		{
			TArray<FString> V;
			Lines[L].ParseIntoArray(V, TEXT(","), false);
			if (V.Num() < Header.Num())
			{
				continue;
			}
			auto D = [&V](int32 I) { return I >= 0 ? FCString::Atod(*V[I]) : 0.0; };
			FReplayRow R;
			R.Frame = int32(D(CFrame));
			R.T = D(CT);
			R.Pos = FVector(D(CX), D(CX + 1), D(CX + 2));
			R.Vel = FVector(D(CVX), D(CVX + 1), D(CVX + 2));
			R.Speed = D(CSpeed);
			R.Kmh = D(CKmh);
			R.Motion = V[CMotion];
			R.Control = V[CControl];
			R.BoostMeter = D(CBoost);
			R.TrickyTimer = D(CTricky);
			R.Steer = D(CSteer);
			R.Crouch = D(CCrouch);
			R.Surface = int32(D(CSurf));
			R.WordModel = CWm >= 0 ? uint32(FCString::Strtoui64(*V[CWm], nullptr, 10)) : 0u;
			R.WordRecorded = CWr >= 0 ? uint32(FCString::Strtoui64(*V[CWr], nullptr, 10)) : 0u;
			R.bWordMatch = CMatch >= 0 && D(CMatch) != 0.0;
			Rows.Add(R);
		}
		return Rows.Num() > 0;
	}

	FReplayDiff Compare(const TArray<FReplayRow>& Ours, const TArray<FReplayRow>& Model, double PosTol, double VelTol)
	{
		FReplayDiff D;
		D.Frames = FMath::Min(Ours.Num(), Model.Num());
		TArray<FString>& Rep = D.Report;
		if (Ours.Num() != Model.Num())
		{
			Rep.Add(FString::Printf(TEXT("frame count differs: C++ %d, Python %d (comparing %d)"), Ours.Num(), Model.Num(), D.Frames));
		}
		bool bInMismatch = false;
		int32 SpanStart = 0;
		TArray<FString> Spans;
		for (int32 I = 0; I < D.Frames; ++I)
		{
			const FReplayRow& A = Ours[I];
			const FReplayRow& B = Model[I];
			const double Dp = FVector::Dist(A.Pos, B.Pos);
			const double Dv = FVector::Dist(A.Vel, B.Vel);
			const bool bStateDiff = StateKey(A) != StateKey(B);
			if (Dp > D.MaxPosErr) { D.MaxPosErr = Dp; D.MaxPosFrame = I; }
			if (Dv > D.MaxVelErr) { D.MaxVelErr = Dv; D.MaxVelFrame = I; }
			if (D.FirstDrift == INDEX_NONE && Dp > 0.01) { D.FirstDrift = I; }
			if (bStateDiff)
			{
				++D.StateMismatchFrames;
				if (D.FirstStateMismatch == INDEX_NONE) { D.FirstStateMismatch = I; }
			}
			if (D.FirstDivergence == INDEX_NONE && (Dp > PosTol || Dv > VelTol || bStateDiff))
			{
				D.FirstDivergence = I;
			}
			if (A.WordModel == B.WordModel) { ++D.WordModelMatches; }
			if (bStateDiff && !bInMismatch)
			{
				bInMismatch = true;
				SpanStart = I;
			}
			if (bInMismatch && (!bStateDiff || I == D.Frames - 1))
			{
				const int32 End = bStateDiff ? I : I - 1;
				Spans.Add(FString::Printf(TEXT("  frames %d-%d: C++ %s vs Python %s"), SpanStart, End,
				                          *StateKey(Ours[SpanStart]), *StateKey(Model[SpanStart])));
				bInMismatch = false;
			}
		}

		if (D.FirstDivergence != INDEX_NONE)
		{
			const int32 F = D.FirstDivergence;
			const FReplayRow& A = Ours[F];
			const FReplayRow& B = Model[F];
			const FReplayRow& BPrev = Model[FMath::Max(0, F - 1)];
			if (StateKey(A) != StateKey(B))
			{
				const FReplayRow& APrev = Ours[FMath::Max(0, F - 1)];
				const bool bSamePrev = StateKey(APrev) == StateKey(BPrev);
				const FString Owner = bSamePrev ? OwnerOf(BPrev.Motion, BPrev.Control) : OwnerOf(B.Motion, B.Control);
				D.Subsystem = FString::Printf(TEXT("state transition: Python %s -> %s, C++ %s -> %s; owner %s"),
					*StateKey(BPrev), *StateKey(B), *StateKey(APrev), *StateKey(A), *Owner);
			}
			else
			{
				const double Dp = FVector::Dist(A.Pos, B.Pos);
				const double Dv = FVector::Dist(A.Vel, B.Vel);
				D.Subsystem = FString::Printf(TEXT("kinematics in %s (|dpos| %.3f cm, |dvel| %.3f cm/s); owner %s"),
					*StateKey(B), Dp, Dv, *OwnerOf(B.Motion, B.Control));
			}
		}

		Rep.Add(FString::Printf(TEXT("frames compared %d, tolerance |dpos| <= %.2f cm, |dvel| <= %.2f cm/s, same motion/control"), D.Frames, PosTol, VelTol));
		Rep.Add(D.FirstDivergence == INDEX_NONE ? FString(TEXT("NO DIVERGENCE"))
			: FString::Printf(TEXT("FIRST DIVERGENCE at frame %d (t = %.3f s): %s"), D.FirstDivergence, D.FirstDivergence / 60.0, *D.Subsystem));
		Rep.Add(FString::Printf(TEXT("first |dpos| > 0.01 cm at frame %d%s; first state mismatch at frame %d (%d frames mismatched)"),
			D.FirstDrift, D.FirstDrift == INDEX_NONE ? TEXT("") : *FString::Printf(TEXT(" in %s (%s)"), *StateKey(Model[D.FirstDrift]),
				*OwnerOf(Model[D.FirstDrift].Motion, Model[D.FirstDrift].Control)),
			D.FirstStateMismatch, D.StateMismatchFrames));
		if (D.FirstDrift != INDEX_NONE)
		{
			FString Growth;
			for (int32 I : { D.FirstDrift, D.FirstDrift + 60, D.FirstDrift + 240, D.FirstDivergence - 1 })
			{
				if (I >= 0 && I < D.Frames)
				{
					Growth += FString::Printf(TEXT(" f%d %.3f/%.3f"), I, FVector::Dist(Ours[I].Pos, Model[I].Pos), FVector::Dist(Ours[I].Vel, Model[I].Vel));
				}
			}
			Rep.Add(TEXT("drift |dpos| cm / |dvel| cm/s:") + Growth);
		}
		Rep.Add(FString::Printf(TEXT("max |dpos| %.3f cm at frame %d; max |dvel| %.3f cm/s at frame %d"), D.MaxPosErr, D.MaxPosFrame, D.MaxVelErr, D.MaxVelFrame));
		Rep.Add(FString::Printf(TEXT("control word (word_model) equal to Python on %d / %d frames"), D.WordModelMatches, D.Frames));
		if (D.FirstDivergence != INDEX_NONE)
		{
			Rep.Add(TEXT("context around the first divergence (C++ / Python):"));
			for (int32 I = FMath::Max(0, D.FirstDivergence - 3); I < FMath::Min(D.Frames, D.FirstDivergence + 3); ++I)
			{
				Rep.Add(TEXT("  C++ ") + RowText(Ours[I]));
				Rep.Add(TEXT("  Py  ") + RowText(Model[I]));
			}
		}
		auto Timeline = [](const TArray<FReplayRow>& Rows, int32 N)
		{
			FString S;
			int32 Count = 0;
			for (int32 I = 0; I < N; ++I)
			{
				if (I == 0 || StateKey(Rows[I]) != StateKey(Rows[I - 1]))
				{
					if (++Count > 24) { S += TEXT(" ..."); break; }
					S += FString::Printf(TEXT(" %d:%s"), I, *StateKey(Rows[I]));
				}
			}
			return S;
		};
		Rep.Add(TEXT("timeline C++   :") + Timeline(Ours, D.Frames));
		Rep.Add(TEXT("timeline Python:") + Timeline(Model, D.Frames));
		if (Spans.Num() > 0)
		{
			Rep.Add(FString::Printf(TEXT("state-timeline mismatches (%d spans):"), Spans.Num()));
			for (int32 I = 0; I < FMath::Min(Spans.Num(), 20); ++I) { Rep.Add(Spans[I]); }
			if (Spans.Num() > 20) { Rep.Add(TEXT("  ...")); }
		}
		return D;
	}
}

namespace
{
	bool ParseVector(const TCHAR* Cmd, const TCHAR* Key, FVector& Out)
	{
		FString S;
		if (!FParse::Value(Cmd, Key, S, /*bShouldStopOnSeparator=*/false))
		{
			return false;
		}
		TArray<FString> P;
		S.ParseIntoArray(P, TEXT(","));
		if (P.Num() != 3) { return false; }
		Out = FVector(FCString::Atod(*P[0]), FCString::Atod(*P[1]), FCString::Atod(*P[2]));
		return true;
	}

	void RunReplayFromCommandLine()
	{
		const TCHAR* Cmd = FCommandLine::Get();
		FReplayOptions Opt;
		if (!FParse::Value(Cmd, TEXT("-SSXReplay="), Opt.InpPath))
		{
			return;
		}
		FParse::Value(Cmd, TEXT("-SSXReplayWorld="), Opt.World);
		Opt.bHaveSpawn = ParseVector(Cmd, TEXT("-SSXSpawn="), Opt.SpawnPos);
		ParseVector(Cmd, TEXT("-SSXHeading="), Opt.SpawnHeading);
		FParse::Value(Cmd, TEXT("-SSXSpawnSpeed="), Opt.SpawnSpeed);
		FParse::Value(Cmd, TEXT("-SSXFrames="), Opt.MaxFrames);
		FString ComparePath;
		FParse::Value(Cmd, TEXT("-SSXCompare="), ComparePath);

		FReplayResult Result;
		if (!SSXReplay::Run(Opt, Result))
		{
			UE_LOG(LogTemp, Error, TEXT("[SSX.Replay] replay failed: %s"), *Result.Error);
		}
		else
		{
			const FString Out = SSXReplay::OutputPath(SSXReplay::RunName(Opt));
			SSXReplay::WriteCsv(Out, Result.Rows);
			Say(FString::Printf(TEXT("replayed %s: %d frames, %s; %s -> %s"), *Opt.InpPath, Result.Rows.Num(), *Result.SpawnInfo, *Result.States, *Out));
			if (!ComparePath.IsEmpty())
			{
				TArray<FReplayRow> Model;
				FString Err;
				if (!SSXReplay::LoadCsv(ComparePath, Model, &Err))
				{
					UE_LOG(LogTemp, Error, TEXT("[SSX.Replay] %s"), *Err);
				}
				else
				{
					const FReplayDiff Diff = SSXReplay::Compare(Result.Rows, Model);
					for (const FString& Line : Diff.Report) { Say(Line); }
				}
			}
		}
		RequestEngineExit(TEXT("SSXReplay finished"));
	}

	FDelayedAutoRegisterHelper GSSXReplayHook(EDelayedRegisterRunPhase::EndOfEngineInit, []()
	{
		if (FCString::Strifind(FCommandLine::Get(), TEXT("-SSXReplay=")))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				RunReplayFromCommandLine();
				return false;
			}));
		}
	});
}
