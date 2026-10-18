// The headless re-run of a memory-card replay file (FFileRace) and the cBoarder record helpers, shared by
// SSX.Replay.Format.UserFile / LiveTrace / SSX.Rail.LiveOneStep (ReplayFormatTests.cpp) and the in-game comparison of
// SSX.Replay.Format.UserFileInGame (ReplayInGameTests.cpp). Test-only; included inside WITH_DEV_AUTOMATION_TESTS.
#pragma once

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Progression/SSXProgression.h"
#include "Race/GameRng.h"
#include "Effects/TrackEffects.h"
#include "Sim/Ground/PropCollision.h"
#include "World/PropNodes.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXHuffCodec.h"
#include "Race/SSXReplay.h"
#include "Race/SSXReplayFile.h"
#include "Sim/EEFloat.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"

// --- the user's original file ------------------------------------------------------------------------------------------

namespace
{
	/** env SSX_DUMP_TO: the last record frame (exclusive) of the per-frame dumps (default 2400). */
	uint32 DumpTo()
	{
		const int32 V = FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_DUMP_TO")));
		return V > 0 ? uint32(V) : 2400u;
	}

	const TCHAR* VenueStem(int32 Venue)
	{
		static const TCHAR* Names[] = { TEXT("gari"), TEXT("snow"), TEXT("elysium"), TEXT("mesa"), TEXT("merquer"), TEXT("aloha"),
			TEXT("pipe"), TEXT("untrack"), TEXT("megaple"), TEXT("trick"), TEXT("alaska") };
		return Venue >= 0 && Venue < UE_ARRAY_COUNT(Names) ? Names[Venue] : TEXT("");
	}

	/** Keyframe-0 field comparison: a port rider state against the file's cBoarder record (field F at Rec + F - 0x10).
	 *  Returns the mismatch lines; Mapped collects the offsets compared (the rest of the record is listed separately). */
	TArray<FString> CompareBoarderRecord(const uint8* Rec, const FRiderState& S, TSet<int32>& Mapped)
	{
		TArray<FString> Out;
		auto F = [Rec](int32 Off) { float V; FMemory::Memcpy(&V, Rec + Off - 0x10, 4); return double(V); };
		auto I = [Rec](int32 Off) { int32 V; FMemory::Memcpy(&V, Rec + Off - 0x10, 4); return V; };
		auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol + 1e-4 * FMath::Abs(B); };
		auto Scalar = [&](const TCHAR* Name, int32 Off, double Port, double Tol)
		{
			Mapped.Add(Off);
			if (!Near(Port, F(Off), Tol)) Out.Add(FString::Printf(TEXT("+0x%X %s: port %.5f file %.5f"), Off, Name, Port, F(Off)));
		};
		auto Int = [&](const TCHAR* Name, int32 Off, int32 Port)
		{
			Mapped.Add(Off);
			if (Port != I(Off)) Out.Add(FString::Printf(TEXT("+0x%X %s: port %d file %d (0x%08x)"), Off, Name, Port, I(Off), uint32(I(Off))));
		};
		auto Vec = [&](const TCHAR* Name, int32 Off, const FVector& Port, double Tol)
		{
			for (int32 K = 0; K < 3; ++K) Mapped.Add(Off + 4 * K);
			const FVector File(F(Off), F(Off + 4), F(Off + 8));
			if (!Near(Port.X, File.X, Tol) || !Near(Port.Y, File.Y, Tol) || !Near(Port.Z, File.Z, Tol))
				Out.Add(FString::Printf(TEXT("+0x%X %s: port (%.4f %.4f %.4f) file (%.4f %.4f %.4f)"), Off, Name, Port.X, Port.Y, Port.Z, File.X, File.Y, File.Z));
		};
		auto Filt = [&](const TCHAR* Name, int32 Off, const FApproachFilter& P)
		{
			for (int32 K = 0; K < 3; ++K) Mapped.Add(Off + 4 * K);
			if (!Near(P.Cur, F(Off), 1e-3) || !Near(P.Rate, F(Off + 4), 1e-3) || !Near(P.Target, F(Off + 8), 1e-3))
				Out.Add(FString::Printf(TEXT("+0x%X filter %s: port (%.4f %.4f %.4f) file (%.4f %.4f %.4f)"), Off, Name, P.Cur, P.Rate, P.Target, F(Off), F(Off + 4), F(Off + 8)));
		};
		Scalar(TEXT("boost meter"), 0x1C, S.BoostMeter, 1e-4);
		Scalar(TEXT("tricky timer"), 0x24, S.TrickyTimer, 1e-4);
		Int(TEXT("uber hint"), 0x28, S.UberHintIndex);
		Scalar(TEXT("time scale"), 0x12C, S.TimeScale, 1e-4);
		Scalar(TEXT("boost strength"), 0x130, S.BoostStrength, 1e-4);
		Scalar(TEXT("boost burst timer"), 0x134, S.BoostBurstTimer, 1e-4);
		Scalar(TEXT("launch boost timer"), 0x138, S.LaunchBoostTimer, 1e-4);
		Vec(TEXT("position"), 0x140, S.Position, 0.01);
		Vec(TEXT("velocity"), 0x150, S.Velocity, 0.05);
		Scalar(TEXT("ground drag"), 0x160, S.GroundDragAccel, 1e-3);
		Scalar(TEXT("forward distance"), 0x164, S.ForwardDistance, 1e-2);
		Scalar(TEXT("ref dir angle"), 0x168, S.RefDirAngle, 1e-4);
		for (int32 K = 0; K < 4; ++K) Mapped.Add(0x170 + 4 * K);
		{
			const FQuat& Q = S.Orientation;
			const double FW = F(0x170), FX = F(0x174), FY = F(0x178), FZ = F(0x17C);   // stored (w, x, y, z)
			const bool bSame = (Near(Q.X, FX, 1e-3) && Near(Q.Y, FY, 1e-3) && Near(Q.Z, FZ, 1e-3) && Near(Q.W, FW, 1e-3))
				|| (Near(-Q.X, FX, 1e-3) && Near(-Q.Y, FY, 1e-3) && Near(-Q.Z, FZ, 1e-3) && Near(-Q.W, FW, 1e-3));
			if (!bSame) Out.Add(FString::Printf(TEXT("+0x170 orientation (w x y z): port (%.5f %.5f %.5f %.5f) file (%.5f %.5f %.5f %.5f)"), Q.W, Q.X, Q.Y, Q.Z, FW, FX, FY, FZ));
		}
		Vec(TEXT("basis forward"), 0x180, S.BasisForward, 1e-3);
		Vec(TEXT("basis side"), 0x190, S.BasisSide, 1e-3);
		Vec(TEXT("basis up"), 0x1A0, S.BasisUp, 1e-3);
		Scalar(TEXT("yaw"), 0x1B0, S.Yaw, 1e-4);
		Int(TEXT("stance goofy"), 0x1B4, S.bStanceGoofy ? 1 : 0);
		Scalar(TEXT("ground height"), 0x1BC, S.GroundHeight, 1e-2);
		Scalar(TEXT("suspension"), 0x1C0, S.SuspensionOffset, 1e-3);
		Scalar(TEXT("speed cap"), 0x1C4, S.SpeedCap, 1e-2);
		Scalar(TEXT("launch charge"), 0x1C8, S.LaunchCharge, 1e-4);
		Scalar(TEXT("board chatter"), 0x1CC, S.BoardChatter, 1e-4);
		Scalar(TEXT("spin angle"), 0x1D0, S.SpinAngle, 1e-4);
		Scalar(TEXT("flip angle"), 0x1D4, S.FlipAngle, 1e-4);
		Scalar(TEXT("edge angle"), 0x1D8, S.EdgeAngle, 1e-4);
		Int(TEXT("grind type"), 0x1DC, S.GrindType);
		Scalar(TEXT("shove angle"), 0x1E0, S.ShoveAngle, 1e-4);
		Int(TEXT("trick id"), 0x1E4, S.TrickId);
		Int(TEXT("trick phase"), 0x1E8, S.TrickPhase);
		Int(TEXT("trick hold"), 0x1EC, S.TrickHoldFrames);
		Filt(TEXT("ramp 1F0"), 0x1F0, S.Ramp1F0);
		Filt(TEXT("brake"), 0x1FC, S.Brake);
		Filt(TEXT("crouch"), 0x208, S.Crouch);
		Filt(TEXT("steer"), 0x214, S.Steer);
		Filt(TEXT("prewind a"), 0x220, S.PrewindA);
		Filt(TEXT("prewind b"), 0x22C, S.PrewindB);
		Filt(TEXT("gate launch"), 0x238, S.GateLaunch);
		Filt(TEXT("lean"), 0x244, S.Lean);
		Filt(TEXT("boost pose"), 0x250, S.BoostPose);
		Filt(TEXT("boost flash"), 0x25C, S.BoostFlash);
		Filt(TEXT("steer follow"), 0x268, S.SteerFollow);
		Scalar(TEXT("air turn offset"), 0x274, S.AirTurnOffset, 1e-4);
		Scalar(TEXT("air tilt offset"), 0x278, S.AirTiltOffset, 1e-4);
		Scalar(TEXT("air turn smoothed"), 0x27C, S.AirTurnSmoothed, 1e-4);
		Scalar(TEXT("air tilt smoothed"), 0x280, S.AirTiltSmoothed, 1e-4);
		Int(TEXT("surface id"), 0x290, S.SurfaceId);
		Scalar(TEXT("surface p7"), 0x294, S.SurfaceP7, 1e-4);
		Scalar(TEXT("surface p8"), 0x298, S.SurfaceP8, 1e-4);
		Vec(TEXT("ground normal"), 0x2A0, S.GroundNormal, 1e-3);
		Vec(TEXT("fall line"), 0x2B0, S.FallLine, 1e-3);
		Vec(TEXT("smoothed up"), 0x2C0, S.SmoothedUp, 1e-3);
		Vec(TEXT("contact point"), 0x2D0, S.ContactPoint, 0.5);
		Int(TEXT("contact kind"), 0x2E0, S.ContactKind);
		Scalar(TEXT("stuck a"), 0x2E4, S.StuckAccumA, 1e-4);
		Vec(TEXT("last impact normal"), 0x2F0, S.LastImpactNormal, 1e-3);
		Scalar(TEXT("stuck b"), 0x300, S.StuckAccumB, 1e-4);
		Int(TEXT("auto reset"), 0x304, S.bAutoReset ? 1 : 0);
		Scalar(TEXT("surface p10"), 0x308, S.SurfaceP10, 1e-4);
		Scalar(TEXT("ground blend"), 0x30C, S.GroundBlend, 1e-4);
		Int(TEXT("surface before wipe-out"), 0x310, S.SurfaceBeforeWipeout);
		Vec(TEXT("contact forward"), 0x320, S.ContactForward, 1e-3);
		Vec(TEXT("contact left"), 0x330, S.ContactLeft, 1e-3);
		Scalar(TEXT("path param"), 0x344, S.PathParam, 1e-2);
		Scalar(TEXT("path lateral"), 0x348, S.PathLateral, 1e-2);
		Vec(TEXT("path point"), 0x350, S.PathPoint, 0.5);
		Vec(TEXT("path lookahead"), 0x360, S.PathLookahead, 0.5);
		Scalar(TEXT("path heading"), 0x370, S.PathHeading, 1e-3);
		Scalar(TEXT("track dist to go"), 0x374, S.TrackDistToGo, 1.0);
		Int(TEXT("finish result"), 0x418, S.FinishResult);
		Int(TEXT("board class"), 0x420, S.BoardClass);
		Int(TEXT("motion"), 0x424, int32(S.Motion));
		Int(TEXT("control"), 0x428, int32(S.Control));
		Scalar(TEXT("render alpha"), 0x448, S.RenderAlpha, 1e-4);
		Scalar(TEXT("reset fade"), 0x44C, S.ResetFade, 1e-4);
		Vec(TEXT("spawn position"), 0x450, S.SpawnPosition, 0.01);
		return Out;
	}

	/** One rider's state at the cBoarder offsets a live trace records (pos, vel, quat w x y z, ground terms, the filters
	 *  +0x1F0..+0x26C, ground normal, motion / control, the landing predictor +0x57B0..), as "offset=value" pairs. */
	FString BoarderDumpRow(uint32 Frame, int32 Racer, const FRiderState& S)
	{
		FString R = FString::Printf(TEXT("%u,%d"), Frame, Racer);
		auto V = [&R](int32 Off, double X) { R += FString::Printf(TEXT(",%X=%.9g"), Off, X); };
		auto V3 = [&V](int32 Off, const FVector& X) { V(Off, X.X); V(Off + 4, X.Y); V(Off + 8, X.Z); };
		auto Fl = [&V](int32 Off, const FApproachFilter& P) { V(Off, P.Cur); V(Off + 4, P.Rate); V(Off + 8, P.Target); };
		V3(0x140, S.Position); V3(0x150, S.Velocity); V(0x12C, S.TimeScale); V(0x1E4, S.TrickId); V3(0x4AA0, S.RenderRow0); V3(0x4AB0, S.RenderRow1); V3(0x4AC0, S.RenderRow2); V3(0x4AD0, S.RenderRow3); V3(0x8C8, S.PoseRoot);
		V3(0x4A60, S.BoardRow0); V3(0x4A70, S.BoardRow1); V3(0x4A80, S.BoardRow2); V3(0x4A90, S.BoardOrigin);
		V(0x160, S.GroundDragAccel); V(0x164, S.ForwardDistance);
		V(0x170, S.Orientation.W); V(0x174, S.Orientation.X); V(0x178, S.Orientation.Y); V(0x17C, S.Orientation.Z);
		V(0x1B0, S.Yaw); V(0x1BC, S.GroundHeight); V(0x1C0, S.SuspensionOffset); V(0x1C4, S.SpeedCap); V(0x1C8, S.LaunchCharge);
		V(0x1D0, S.SpinAngle); V(0x1D4, S.FlipAngle); V(0x1D8, S.EdgeAngle);
		Fl(0x1F0, S.Ramp1F0); Fl(0x1FC, S.Brake); Fl(0x208, S.Crouch); Fl(0x214, S.Steer); Fl(0x220, S.PrewindA); Fl(0x22C, S.PrewindB);
		Fl(0x238, S.GateLaunch); Fl(0x244, S.Lean); Fl(0x250, S.BoostPose); Fl(0x25C, S.BoostFlash); Fl(0x268, S.SteerFollow);
		V(0x1CC, S.BoardChatter); V(0x290, S.SurfaceId); V(0x294, S.SurfaceP7); V(0x298, S.SurfaceP8);
		V3(0x2A0, S.GroundNormal); V3(0x2B0, S.FallLine); V3(0x2C0, S.SmoothedUp); V3(0x2D0, S.ContactPoint); V(0x2E0, S.ContactKind);
		V3(0x320, S.ContactForward); V3(0x330, S.ContactLeft);
		V(0x424, int32(S.Motion)); V(0x428, int32(S.Control));
		const FLandingPrediction& P = S.Prediction;
		V(0x57B8, P.TimeLeft); V3(0x57C0, P.LandPoint); V3(0x57D0, P.LandVelocity); V3(0x57E0, P.LandNormal);
		V(0x57F4, P.FlightTime); V(0x57F8, P.ApexTime); V(0x57FC, P.AirElapsed); V(0x5800, P.ApexLeft);
		return R;
	}

	/** The race of a file's setup block in the sim harness (as SSX.Replay.LiveRace): racers from the profiles, stream B
	 *  from the race seed, the human playing the recorded words; each rebuilt keyframe against the file's snapshot. */
	struct FFileRace : public SSXReplay::IReplayWorld
	{
		FRaceTrackData Data;
		FPatchWorld World;
		FRaceSession Session;
		virtual void Periodic60() override { Session.ReplayPairUpkeep(true); }    // 0x1147F8
		virtual void Periodic10() override { Session.ReplayPairUpkeep(false); }   // 0x14FE18 + 0x150428
		TArray<TUniquePtr<FRiderSim>> Sims;
		TArray<TUniquePtr<SSXAnim::FRiderAnimDriver>> Poses;   // the pose pass after every boarder update (cPostAI 0x114730)
		FRiderInputDriver Driver;
		FSSXReplay Replay;
		int32 Human = 0;
		struct FCheck { uint32 Frame; SSXReplayFile::FSnapshotCheck C; TArray<FVector> Port; SSXReplayFile::FSnapshot File; int32 PortFrame = 0, PortPhase = 0; double PortClock = 0.0; TArray<FRiderState> States; TArray<uint8> Raw; };
		TArray<FCheck> Checks;
		TArray<FString> Trace;
		TArray<FString> WordTrace;          // the human's decoded words / transitions to frame 760 (one line per change)
		FString FirstLayoutMismatch;        // the first word that does not fit the port's layout (the first provable divergence)
		uint32 LastWord = 0;
		int32 LastWc = -1;
		TArray<int32> DrawsBy;              // stream-B draws during each racer's boarder update this frame (dump F010)
		bool bDump = false;                 // env SSX_PORT_DUMP: every racer's BoarderDumpRow per frame to 2400 (vs a live trace)
		// one-step forcing from a live trace (SSX.Rail.LiveOneStep): (racer << 32 | frame) -> the trace record (0xB50 bytes:
		// cBoarder 0x0000..0x0600, 0x4600..0x4B00, 0x57B0..0x5800). Before the update of frame f + 1 the racer is set to the
		// sample of frame f: motion / control (when they differ), the block +0x10..+0x460, the grind type / stance / surface,
		// the rail box filter +0x284 and the board matrix +0x4A60 (the pose the next update's rail code reads)
		TMap<int64, const uint8*> Force;
		// the board matrix for the update after frame f (the cPostAI pose of update f): the sampler reads the pose block
		// either before the update's cPostAI (the pose of the previous update) or after it (tools/make_rail_live_reference.py)
		TMap<int64, const uint8*> ForceBoard;
		// the cPostAI sphere set the next update's rider contacts read (cBoarder +0x470..+0x6E0, chosen as the board)
		TMap<int64, const uint8*> ForceSet;
		TFunction<void()> OnFrameEnd;       // after every frame's boarder updates and pose pass
		int32 ForcedPreControl = 0;
		bool bForceControl = false;         // Force also sets a differing control (SSX.WipeOut.LiveSequence's run-up)
		TSet<int64> ForceControlAt;         // the same for single forced samples (SSX.Rail.LiveOneStep's rider-contact windows)         // forced rail samples read before their update's control (steer rate kept)
		TArray<FString> Dump;
		FString DumpAnim(int32 I) const   // dump: controller channel ids, every instance (id:frame:weight:rate) and the sim's trick clock
		{
			if (!Poses.IsValidIndex(I) || !Poses[I]) return FString();
			const SSXAnim::FAnimController& C = Poses[I]->GetController();
			FString R = FString::Printf(TEXT(",A0=%x,A1=%x,A2=%x"), C.CurrentId(0), C.CurrentId(1), C.CurrentId(2));
			if (const SSXAnim::FRiderAnimChannel* Sc = SSXAnim::FindChannel(*Sims[I])) { R += FString::Printf(TEXT(",TR=%d:%.4f:%.4f:%d"), Sc->Trick.Data.AnimId, Sc->Trick.Frame, Sc->Trick.Hold, Sc->Trick.IsActive() ? 1 : 0); }
			for (int32 Ch = 0; Ch < 3; ++Ch)
			{
				R += FString::Printf(TEXT(",I%d="), Ch);
				for (const SSXAnim::FPlayInstance& P : C.Instances(Ch)) { R += FString::Printf(TEXT("%x:%.4f:%.4f:%.4f:y%.3f:m%d;"), P.AnimId, P.Subs[0].Frame, P.Weight, P.Rate, P.Yaw, P.bMirror ? 1 : 0); }
			}
			return R;
		}
		// The track effects as the race manager runs them (USSXTrackEffects): the .ssf scripts, controllers and
		// cells, the riders' prop touches, the flow scripts, the stage reset; without visuals / sounds. Their stream-B draws
		// (op 5 kind 1, the AnimObject-family ctor 0x198810) belong to the race: REP0001's first is the AnimObject that the
		// cell update of f364 creates on Snowdream.
		SSXEffects::FEffectData FxData;
		SSXEffects::FEffectWorld Fx;
		SSXEffects::FCellTracker FxCells;
		SSXPropNodes::FPropAnimData FxAnim;
		TArray<TUniquePtr<SSXEffects::FEffectRider>> FxRiders;
		struct FFxAnimNode { SSXPropNodes::FAnimObject Anim; bool bDone = false; };
		TMap<int32, FFxAnimNode> FxAnimNodes;   // AnimObject / AnimDelta / AnimCombo / AnimTexFlip play state (NodeDone, messages)
		bool bFx = false, bFxInTouch = false, bFxCountdownEnded = false, bShowOff = false;
		int32 FxPlacements = 0;

		bool Build(const SSXReplayFile::FSetup& S, FString& Err)
		{
			const FString Stem = VenueStem(S.Venue);
			if (!Data.Load(FRaceTrackData::DefaultPath(Stem), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(Stem), &Err)) return false;
			Human = FMath::Max(0, S.RacerOfHuman(0));
			const int32 N = FMath::Clamp(S.NumRacers, 1, SSXReplayFile::MaxRacers);
			for (int32 I = 0; I < N; ++I)
			{
				TUniquePtr<FRiderSim>& Sim = Sims.Add_GetRef(MakeUnique<FRiderSim>());
				if (I == Human) { SSXRiderSetup::InstallStates(*Sim, Driver, true); }
				else { SSXRiderSetup::RegisterStates(*Sim, true); }
				Sim->SetWorld(&World);
				SSXRider::ApplyProfile(SSXReplayFile::RacerProfile(S.Racers[I]), Sim->Profile);
				Sim->Reset();
				// the rider's animation controller and pose (cBoarder+0x46C0 / +0x7C0): the pivot the air code reads
				TUniquePtr<SSXAnim::FRiderAnimDriver>& Pose = Poses.Add_GetRef(MakeUnique<SSXAnim::FRiderAnimDriver>());
				if (!Pose->Init(SSXAnim::FAflLibrary::Shared(), SSXCharacters::IdForIndex(S.Racers[I].Char38), FMath::Clamp(Sim->Profile.BoardClass, 0, 2),
					FMath::Clamp(Sim->Profile.GetTrickCharacter(), 0, 11)))
				{
					Pose.Reset();
				}
			}
			FRaceConfig Cfg;
			Cfg.Mode = ERaceEventMode(FMath::Clamp(S.GameMode, 0, 9));
			Cfg.Difficulty = S.Difficulty;
			if (!Session.Init(Data, Cfg)) { Err = TEXT("race data has no paths for this mode"); return false; }
			for (int32 I = 0; I < N; ++I) { Session.AddRacer(Sims[I].Get(), I == Human); }
			for (int32 I = 0; I < N; ++I)
			{
				for (int32 J = 0; J < N; ++J)
				{
					FRacerRecord& Rec = Session.GetRacerMutable(I);
					const uint8* B = S.Racers[I].Rival[J];
					Rec.RivalSetup[J].Level = B[0]; Rec.RivalSetup[J].LevelMax = B[1]; Rec.RivalSetup[J].AngerLimit = B[2]; Rec.RivalSetup[J].Kind = B[3];
					SSXRace::ResetRivalry(Rec.Rivals[J], Rec.RivalSetup[J]);
				}
			}
			if (FxData.Load(SSXEffects::FEffectData::DefaultPath(Stem)))
			{
				// USSXTrackEffects::BeginTrack: a fresh effect state, the rails and instance flags at their initial halves
				bFx = true;
				bShowOff = SSXEffects::IsShowOffMode(S.GameMode);
				FxAnim.Load(SSXPropNodes::FPropAnimData::DefaultPath(Stem));
				Fx.Init(&FxData, S.GameMode, FxData.TrackId >= 0 ? FxData.TrackId : SSXEffects::TrackIdForStem(Stem));
				Fx.Rng = []() { return double(FGameRng::StreamB().Uniform(0.f, 1.f)); };   // op 5 kind 1: stream B
				Fx.OnOutcome = [this](const SSXEffects::FOutcome& O) { OnFxOutcome(O); };
				Fx.OnFlagsChanged = [this](int32 Inst, uint16, uint16 New) { World.SetInstanceRunFlags(Inst, New); };
				FxCells.Init(&FxData, bShowOff);
				World.ResetRailsToInitial();
				World.ResetInstanceRunFlags();
				for (int32 I = 0; I < N; ++I)
				{
					TUniquePtr<SSXEffects::FEffectRider>& R = FxRiders.Add_GetRef(MakeUnique<SSXEffects::FEffectRider>());
					R->Sim = Sims[I].Get();
					R->Slot = I;
					R->bHuman = I == Human;
					Fx.Racers.Add(R.Get());
					SSXEffects::FEffectRider* RP = R.Get();
					// the touch handler 0x13BD40 from PropCollision (USSXTrackEffects::Touch; a node it creates is made by touch)
					Sims[I]->OnPropContact = [this, RP](FRiderSim&, const SSXGround::FPropContactEvent& E)
					{
						bFxInTouch = true;
						Fx.Touch(E.Hit.InstanceIndex, *RP, true, FVector::DotProduct(E.Contact.Direction, E.Contact.Normal), E.Contact.Speed);
						bFxInTouch = false;
					};
				}
			}
			Replay.Init(N, S.NumHumans(), false, this);
			for (int32 P = 0; P < S.NumHumans(); ++P) { Replay.AttachHuman(P); }
			FGameRng::StreamB().Seed(S.Seed2B00);   // cAI vfunc_15: mgr+0x2B00
			Session.Start();
			return true;
		}
		virtual void RebuiltKeyframe(int32 Key, uint32 Frame, const TArray<uint8>& Original) override
		{
			FCheck& C = Checks.AddDefaulted_GetRef();
			C.Frame = Frame;
			for (const TUniquePtr<FRiderSim>& S : Sims) { C.Port.Add(S->State.Position); }
			if (Frame == 0) { for (const TUniquePtr<FRiderSim>& S : Sims) { C.States.Add(S->State); } C.Raw = Original; }
			SSXReplayFile::CheckSnapshot(Original, C.Port, FGameRng::StreamB().S, C.C);
			SSXReplayFile::ParseSnapshot(Original, C.File);
			if (Frame == 0 && !SSXReplayFile::IsPortSnapshot(Original))
			{
				// 0x18F6C0 at the playback's keyframe-0 restore: every cBoarder block back as the file holds it (the in-game
				// FSSXRaceReplayWorld::RebuiltKeyframe does the same)
				for (int32 I = 0; I < Sims.Num(); ++I)
				{
					const int32 R = Sims.Num() - 1 - I;
					if (C.File.RecordAt.IsValidIndex(R) && C.File.RecordAt[R] + 0x450 <= Original.Num())
					{
						SSXReplayFile::RestoreBoarderRecord(Original.GetData() + C.File.RecordAt[R], Sims[I]->State);
					}
				}
			}
			C.PortFrame = Session.GetFrame();
			C.PortPhase = int32(Session.GetPhase());
			C.PortClock = Session.GetClock();
			if (Frame == 0 && C.File.AiFrame > 0)
			{
				// a loaded replay's playback restores keyframe 0 (0x190878 -> 0x18F6C0 -> cAI vfunc_09 0x1127E0 rereads +0x18 frame,
				// +0x1C / +0x20 phases): the race frame counter (+0x18, the every-6th-frame ranking / catch-up 0x115100 and the
				// 60-frame skill pick) continues from the recording's value, not from the port's shortened pre-race
				FRaceSessionKeyframe K;
				Session.SaveReplay(K);
				K.Frame = C.File.AiFrame;
				Session.LoadReplay(K);
			}
		}
		virtual void StageReset() override
		{
			// 0x191298 case 0xB (USSXTrackEffects::ResetStage): script / controller nodes deleted, cell slots freed, instance
			// flags and rails back to their initial halves, every racer's +0x42C = -1
			if (!bFx) return;
			Fx.RestartReset();
			FxCells.Init(&FxData, bShowOff);
			for (const TUniquePtr<SSXEffects::FEffectRider>& R : FxRiders) { R->PrevCell = -1; }
			World.ResetRailsToInitial();
			World.ResetInstanceRunFlags();
			for (int32 I = 0; I < FxData.NumInstances(); ++I) { World.SetInstanceRunFlags(I, Fx.GetFlags(I)); }
			FxAnimNodes.Reset();
		}
		void OnFxOutcome(const SSXEffects::FOutcome& O)
		{
			using namespace SSXEffects;
			const bool bAnim = O.A >= TypeAnimObject && O.A <= TypeAnimTexFlip;
			switch (O.Kind)
			{
			case EOutcome::Rail: World.SetRailEnabled(O.A, (O.B & 1) != 0); break;
			case EOutcome::Node:
				if (bAnim && FxData.Records.IsValidIndex(O.Record))
				{
					// AnimObject-family ctor 0x198810 (as USSXPropAnimation::OnNodeCreated): its stream-B draws
					const FRecord& R = FxData.Records[O.Record];
					SSXPropNodes::FAnimObjectRecord Rec;
					if (SSXPropNodes::FAnimObjectRecord::Parse(FxData.RecordBytes(R), R.Size, Rec))
					{
						const SSXPropNodes::FPropModelInfo* M = FxAnim.ModelOfInstance(O.Instance);
						FFxAnimNode Node;
						Node.Anim.InitGame(Rec, M ? M->EndFrame : 0.0, bFxInTouch);
						Fx.ApplyNodeFlags(O.Instance, FlDisplay, FlNodeDrawn);
						FxAnimNodes.Add(O.Instance, MoveTemp(Node));
					}
				}
				break;
			case EOutcome::NodeDestroyed:
				if (bAnim) { FxAnimNodes.Remove(O.Instance); }
				break;
			case EOutcome::Message:
				if (FFxAnimNode* Node = FxAnimNodes.Find(O.Instance); Node && bAnim) { Node->Anim.Message(O.B, O.V); }
				break;
			default: break;   // visuals, sounds, emitters; the rider ops act inside the interpreter
			}
		}
		void StepFxAnimNodes()
		{
			// as USSXPropAnimation::StepFrame: a play-once end reports done(1) (slot-3 effect, else restore + destroy)
			TArray<int32> Done;
			for (TPair<int32, FFxAnimNode>& P : FxAnimNodes)
			{
				if (P.Value.Anim.Tick()) { Done.Add(P.Key); }
			}
			for (const int32 Inst : Done)
			{
				FFxAnimNode* Node = FxAnimNodes.Find(Inst);
				if (!Node || Node->bDone) continue;
				Node->bDone = true;
				Fx.NodeDone(Inst);
			}
		}
		void FxRaceFrame(ERacePhase Prev, ERacePhase Now, double Clock)
		{
			// as USSXTrackEffects::OnRaceFrame: the race-flow scripts
			using namespace SSXEffects;
			if (Prev != Now)
			{
				if (Prev == ERacePhase::StartRace) { Fx.RunFlow(EFlowPhase::StartRace); }
				if (Now == ERacePhase::PreRace) { Fx.RunFlow(EFlowPhase::PreRace); bFxCountdownEnded = false; }
				if (Now == ERacePhase::Countdown) { Fx.RunFlow(EFlowPhase::CountdownEnter); bFxCountdownEnded = false; }
			}
			if (Now == ERacePhase::Countdown && Clock >= 0.0 && !bFxCountdownEnded)
			{
				bFxCountdownEnded = true;
				Fx.RunFlow(EFlowPhase::CountdownEnd);
			}
		}
		void Frame()
		{
			Replay.Update();   // 0x191298 first (state 0xB: StageReset)
			if (bFx)
			{
				// scene groups 2 (cAIWorld cells; no camera point headless) and 3 (scripts, controllers, the prop nodes)
				TArray<SSXEffects::FEffectRider*, TInlineAllocator<8>> List;
				for (const TUniquePtr<SSXEffects::FEffectRider>& R : FxRiders) { List.Add(R.Get()); }
				FxCells.StepRacers(Fx, List, uint8(Session.GetRequestedPhase()) < uint8(ERacePhase::StartRace), nullptr);
				Fx.Frame();
				StepFxAnimNodes();
			}
			const ERacePhase Prev = Session.GetPhase();
			Session.Tick();
			if (Prev != ERacePhase::StartRace && Session.GetPhase() == ERacePhase::StartRace) { Replay.OnStartRace(); }
			if (bFx)
			{
				FxRaceFrame(Prev, Session.GetPhase(), Session.GetClock());
				if (Session.GetPlacements() != FxPlacements)
				{
					for (const TUniquePtr<SSXEffects::FEffectRider>& R : FxRiders) { R->Cell = R->PrevCell = -1; }   // rider reset 0x116AB8
				}
			}
			FxPlacements = Session.GetPlacements();
			{
				// env SSX_TEACH = a live trace frames.bin (SSX_TEACH_FROM = first frame): before every boarder update each racer's
				// cBoarder block +0x10..+0x460 is set from the live sample of the previous frame (SSXReplayFile::RestoreBoarderRecord),
				// so the SSX_PORT_DUMP rows are one-step results from the original's state
				static TArray<uint8> TeachBin;
				static bool bTeachLoaded = false;
				static TMap<int32, int32> TeachAt;
				static int32 TeachN = 0;
				if (!bTeachLoaded)
				{
					bTeachLoaded = true;
					const FString TP = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_TEACH"));
					if (!TP.IsEmpty() && FFileHelper::LoadFileToArray(TeachBin, *TP))
					{
						FMemory::Memcpy(&TeachN, TeachBin.GetData() + 4, 4);
						const int32 Size = 8 + TeachN * 0xB50;
						for (int32 Off = 0; Off + Size <= TeachBin.Num(); Off += Size) { int32 C; FMemory::Memcpy(&C, TeachBin.GetData() + Off, 4); TeachAt.Add(C - 127, Off); }
					}
				}
				const int32 TeachFrom = FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_TEACH_FROM")));
				const int32 T = int32(Replay.GetRecordFrame()) - 1;
				if (TeachAt.Num() && T >= TeachFrom && TeachAt.Contains(T))
				{
					for (int32 I = 0; I < Sims.Num() && I < TeachN; ++I)
					{
						SSXReplayFile::RestoreBoarderRecord(TeachBin.GetData() + TeachAt[T] + 8 + I * 0xB50 + 0x10, Sims[I]->State);
					}
				}
			}
			if (Force.Num())
			{
				const int32 T = int32(Replay.GetRecordFrame()) - 1;
				for (int32 I = 0; I < Sims.Num(); ++I)
				{
					const uint8* const* Rp = Force.Find((int64(I) << 32) | uint32(T));
					if (!Rp) continue;
					const uint8* Rec = *Rp;
					auto Fl = [Rec](int32 Off) { float V; FMemory::Memcpy(&V, Rec + Off, 4); return double(V); };
					auto In = [Rec](int32 Off) { int32 V; FMemory::Memcpy(&V, Rec + Off, 4); return V; };
					FRiderSim& Sim = *Sims[I];
					// motion and control only when the motion differs (the control state machine is otherwise the port's own)
					const bool bCtl = bForceControl || ForceControlAt.Contains((int64(I) << 32) | uint32(T));
					if (int32(Sim.State.Motion) != In(0x424) || (bCtl && int32(Sim.State.Control) != In(0x428)))
					{
						Sim.SetMotion(ERiderMotion(In(0x424)), TEXT("one-step force"));
						Sim.SetControl(ERiderControl(In(0x428)), TEXT("one-step force"));
					}
					FRiderState& St = Sim.State;
					// a sample read before its update's control ran holds the previous control's steer rate / target: on a rail
					// (control 15) the rate must be clamp(|target - cur| x 0x40E08E35, 0.1, 0x41004B28); when it is not, the
					// port's own control values (computed at the end of the previous update) are kept
					const FApproachFilter KeepSteer = St.Steer, KeepCrouch = St.Crouch;
					SSXReplayFile::RestoreBoarderRecord(Rec + 0x10, St);
					if (In(0x428) == int32(ERiderControl::RailSlide) && Sim.State.Control == ERiderControl::RailSlide)
					{
						const float Cur = float(Fl(0x214)), Rate = float(Fl(0x218)), Tgt = float(Fl(0x21C));
						const float Want = FMath::Clamp(FMath::Abs(SSXEE::FpuSub(Tgt, Cur)) * 7.0173593f, 0.1f, 8.0183487f);
						if (Rate != Want)
						{
							St.Steer.Rate = KeepSteer.Rate; St.Steer.Target = KeepSteer.Target;
							St.Crouch.Rate = KeepCrouch.Rate; St.Crouch.Target = KeepCrouch.Target;
							++ForcedPreControl;
						}
					}
					// the race path block +0x340..+0x374 (the port keeps it in the race record; the AI steers by it)
					FRaceLineFollow& Path = Session.GetRacerMutable(I).Race;
					Path.ParamPrev = Fl(0x340); Path.Param = Fl(0x344); Path.Lateral = Fl(0x348);
					Path.Point = FVector(Fl(0x350), Fl(0x354), Fl(0x358)); Path.Look = FVector(Fl(0x360), Fl(0x364), Fl(0x368)); Path.Heading = Fl(0x370);
					St.GrindType = In(0x1DC);
					St.bStanceGoofy = In(0x1B4) != 0;
					St.SurfaceId = In(0x290);
					St.RailBox.Cur = Fl(0x284); St.RailBox.Rate = Fl(0x288); St.RailBox.Target = Fl(0x28C);
					const uint8* const* Bp = ForceBoard.Find((int64(I) << 32) | uint32(T));
					if (Bp && *Bp)
					{
						const uint8* B = *Bp;
						auto BRow = [B](int32 Off) { float V[3]; FMemory::Memcpy(V, B + 0x600 + Off - 0x4600, 12); return FVector(V[0], V[1], V[2]); };
						St.BoardRow0 = BRow(0x4A60); St.BoardRow1 = BRow(0x4A70); St.BoardRow2 = BRow(0x4A80); St.BoardOrigin = BRow(0x4A90);
						St.bBoardValid = true;
					}
				}
			}
			for (const TUniquePtr<FRiderSim>& S : Sims) { S->bDeferPostAI = true; }   // cPostAI after the whole boarder loop (below)
			if (ForceSet.Num())
			{
				const int32 T = int32(Replay.GetRecordFrame()) - 1;
				for (int32 I = 0; I < Sims.Num(); ++I)
				{
					if (const uint8* const* Sp = ForceSet.Find((int64(I) << 32) | uint32(T)))
					{
						// the cPostAI sphere set as the trace holds it: world centres (frame P = 0, Q = 1)
						FRiderState& St = Sims[I]->State;
						St.SphereFrameP = FVector::ZeroVector;
						St.SphereFrameQ = FQuat::Identity;
						for (int32 K = 0; K < 18; ++K)
						{
							float C[3];
							FMemory::Memcpy(C, *Sp + 0x30 + 0x20 * K, 12);
							St.PosedSpheres[K] = FVector(C[0], C[1], C[2]);
						}
						St.bPosedSpheresValid = St.bSphereFrameValid = true;
						Session.RiderSphereSetRebuilt(I, true);   // the original's proxy AABB is this set's (0x126748)
					}
				}
			}
			const uint32 DrawsStart = FGameRng::StreamB().S[5];   // stream B's call counter (s5): draws per racer for the dump
			uint32 DrawsAt = DrawsStart;
			DrawsBy.Init(0, Sims.Num());
			// last racer first, as the original's boarder loop (live trace: a sample inside the loop shows the low indices on the
			// previous frame); the stream-B order of the AI trick plans and the Cruise taunt rolls depends on it
			for (int32 I = Sims.Num() - 1; I >= 0; --I)
			{
				ON_SCOPE_EXIT { DrawsBy[I] = int32(FGameRng::StreamB().S[5] - DrawsAt); DrawsAt = FGameRng::StreamB().S[5]; };
				if (I != Human) { Sims[I]->Step(FRiderInputWord()); continue; }
				uint32 W = 0;
				const int32 C0 = int32(Sims[I]->State.Control);
				const bool bPlay = Replay.IsPlayingInput() && Replay.RecordOrPlayInput(0, W);
				if (bPlay) { Driver.StepWord(*Sims[I], W); }
				else { Driver.Step(*Sims[I], FPadActions()); }
				if (!bPlay) continue;
				const FRiderState& St = Sims[I]->State;
				const ERiderControl Wc = Driver.GetWordControl();   // the layout the word is decoded with (control after the motion phase)
				const uint32 Rf = Replay.GetRecordFrame();
				// the words and transitions of the start (gate rocking, the first jump and landing), one line per change
				if (Rf < 760 && (W != LastWord || int32(St.Control) != C0 || int32(Wc) != LastWc))
				{
					WordTrace.Add(FString::Printf(TEXT("f%u w %08x layout %d control %d->%d motion %d pos (%.1f %.1f %.1f) v (%.1f %.1f %.1f) spin %.3f trick %d"), Rf, W, int32(Wc), C0,
						int32(St.Control), int32(St.Motion), St.Position.X, St.Position.Y, St.Position.Z, St.Velocity.X, St.Velocity.Y, St.Velocity.Z, St.SpinAngle, St.TrickId));
				}
				// the first word the original can not have packed with the port's layout: a Spin word is the trick byte (0x1A =
				// none) over its fields, so an all-zero word after a no-trick word is a ground layout's idle word (the original
				// had landed); a ground layout's bit 1 is a press edge (0x1797A8), never set on two frames in a row
				if (FirstLayoutMismatch.IsEmpty())
				{
					const bool bSpinZero = Wc == ERiderControl::Spin && W == 0u && (LastWord & 0xFFu) == uint32(FRiderInputWord::NoTrick);
					const bool bGround = Wc == ERiderControl::Cruise || Wc == ERiderControl::Bump || Wc == ERiderControl::ChangeFakie || Wc == ERiderControl::FakieCruise
						|| Wc == ERiderControl::GateToBase || Wc == ERiderControl::LandHard || Wc == ERiderControl::LandNormal || Wc == ERiderControl::GetupFromSit;
					const bool bEdgeHeld = bGround && LastWc == int32(Wc) && (W & 2u) && (LastWord & 2u);
					if (bSpinZero || bEdgeHeld)
					{
						FirstLayoutMismatch = FString::Printf(TEXT("f%u word %08x (previous %08x) with the port's layout %d (%s): the original was in another control; port motion %d, predicted touchdown in %.4f s"),
							Rf, W, LastWord, int32(Wc), bSpinZero ? TEXT("an idle ground word in Spin") : TEXT("a press edge held"), int32(St.Motion), St.Prediction.TimeLeft);
					}
				}
				LastWord = W;
				LastWc = int32(Wc);
			}
			// cPostAI after every boarder update: the render rows / sphere-set frame, then the pose pass
			for (int32 I = 0; I < Sims.Num(); ++I) { Sims[I]->PostAI(); }
			for (int32 I = 0; I < Sims.Num(); ++I) { if (Poses.IsValidIndex(I) && Poses[I]) { Poses[I]->Step(*Sims[I]); Poses[I]->FeedPose(*Sims[I], true); } }
			for (const TUniquePtr<SSXEffects::FEffectRider>& R : FxRiders) { R->PrevCell = R->Cell; }   // cPostAI: +0x42C = +0x16C
			Session.ClearCues();
			for (const TUniquePtr<FRiderSim>& S : Sims) { S->State.TakeHighlightCalls(); }
			if (OnFrameEnd) { OnFrameEnd(); }
			if (bDump && Replay.GetRecordFrame() < DumpTo())
			{
				for (int32 I = 0; I < Sims.Num(); ++I)
				{
					const FRacerRecord& Rr = Session.GetRacer(I);
					FRiderSphereSet Bs;
					const FVector Bc = SSXRider::BuildPostAISphereSet(Sims[I]->Profile, Sims[I]->State, SSXRider::MaskAll, Bs) ? Bs.BoundCentre : FVector::ZeroVector;
					Dump.Add(BoarderDumpRow(Replay.GetRecordFrame(), I, Sims[I]->State) + FString::Printf(TEXT(",340=%.9g,344=%.9g,348=%.9g,360=%.9g,364=%.9g,368=%.9g,370=%.9g"),
						Rr.Race.ParamPrev, Rr.Race.Param, Rr.Race.Lateral, Rr.Race.Look.X, Rr.Race.Look.Y, Rr.Race.Look.Z, Rr.Race.Heading)
						+ FString::Printf(TEXT(",480=%.9g,484=%.9g,488=%.9g,F010=%d,F011=%u"), Bc.X, Bc.Y, Bc.Z, DrawsBy.IsValidIndex(I) ? DrawsBy[I] : -1, FGameRng::StreamB().S[5])
						+ FString::Printf(TEXT(",F001=%d,F002=%.9g,F003=%.9g,F004=%d,F005=%.9g,F006=%.9g"), Rr.CatchupRival, Rr.CatchupNear, Rr.CatchupFar, Rr.Rank,
							Rr.CatchupRival >= 0 ? Rr.RivalDist[Rr.CatchupRival] : 0.0, Rr.CatchupRival >= 0 ? Rr.RivalBearing[Rr.CatchupRival] : 0.0)
						+ DumpAnim(I) + [&Bs]() { FString C; for (int32 K = 0; K < Bs.Count && K < 18; ++K) { C += FString::Printf(TEXT(",%X=%.9g,%X=%.9g,%X=%.9g"), 0x4A0 + 0x20 * K, Bs.Centres[K].X, 0x4A4 + 0x20 * K, Bs.Centres[K].Y, 0x4A8 + 0x20 * K, Bs.Centres[K].Z); } return C; }());
				}
			}
			if (Replay.IsRebuilding() && Replay.GetRecordFrame() % 120 == 0 && Trace.Num() < 40)
			{
				Trace.Add(FString::Printf(TEXT("f%u %s control %d"), Replay.GetRecordFrame(), *Sims[Human]->State.Position.ToString(), int32(Sims[Human]->State.Control)));
			}
		}
	};
}
