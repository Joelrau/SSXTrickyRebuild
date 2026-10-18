#include "Race/RaceRules.h"
#include "Sim/EEFloat.h"

#include "Race/RaceTrackData.h"
#include "Sim/Anim/RiderPose.h"

const TCHAR* LexToString(ERacePhase Phase)
{
	static const TCHAR* Names[] = { TEXT("None"), TEXT("PreRace"), TEXT("StartRace"), TEXT("Countdown"), TEXT("Race"),
		TEXT("EndRace"), TEXT("LoadLesson"), TEXT("PreLesson"), TEXT("RestartLesson"), TEXT("SelectLesson"),
		TEXT("EndLesson"), TEXT("ReplayEndRace") };
	const int32 I = int32(Phase);
	return I >= 0 && I < UE_ARRAY_COUNT(Names) ? Names[I] : TEXT("?");
}

const TCHAR* LexToString(ERaceEventMode Mode)
{
	static const TCHAR* Names[] = { TEXT("PRACTICE_KNOCKDOWN"), TEXT("PRACTICE_FREERIDE"), TEXT("PRACTICE_RACE"),
		TEXT("PRACTICE_SHOWOFF"), TEXT("CIRCUIT_RACE"), TEXT("CIRCUIT_SHOWOFF"), TEXT("LESSON"), TEXT("PRACTICE_TIMEDRACE"),
		TEXT("DEMO"), TEXT("TEST") };
	const int32 I = int32(Mode);
	return I >= 0 && I < UE_ARRAY_COUNT(Names) ? Names[I] : TEXT("?");
}

namespace SSXRace
{
	// 0x365060, 0x365078, 0x365090, 0x3650A8
	const double CatchupNearRace[6] = { -800.169677734375, -1101.80322265625, -1415.9346923828125, -1809.329833984375, -3506.75390625, -3911.97998046875 };
	const double CatchupFarRace[6] = { 6030.36328125, 5509.6865234375, 5008.2197265625, 5000.6826171875, 4017.697998046875, 1508.1605224609375 };
	const double CatchupNearCircuit[6] = { -1869.0435791015625, -2147.75439453125, -2427.93994140625, -2937.53173828125, -3560.376708984375, -4086.20751953125 };
	const double CatchupFarCircuit[6] = { 5014.83935546875, 4519.08154296875, 4034.012939453125, 4097.28466796875, 3029.933837890625, 2022.4232177734375 };

	double WrapAngle(double A)
	{
		return A - FMath::FloorToDouble(A / UE_DOUBLE_TWO_PI + 0.5) * UE_DOUBLE_TWO_PI;
	}

	bool IsScoreMode(ERaceEventMode M)
	{
		return M == ERaceEventMode::PracticeShowoff || M == ERaceEventMode::CircuitShowoff;
	}

	bool IsGapMode(ERaceEventMode M)
	{
		return M == ERaceEventMode::PracticeRace || M == ERaceEventMode::CircuitRace;
	}

	bool UsesGateStart(ERaceEventMode M, int32 TrackId)
	{
		if (TrackId == 7)
		{
			return false;
		}
		switch (M)
		{
		case ERaceEventMode::PracticeKnockdown: case ERaceEventMode::PracticeRace: case ERaceEventMode::PracticeShowoff:
		case ERaceEventMode::CircuitRace: case ERaceEventMode::CircuitShowoff: case ERaceEventMode::PracticeTimedRace:
		case ERaceEventMode::Test:
			return true;
		default:
			return false;
		}
	}

	double ShowoffTimeLimit(int32 TrackId)
	{
		// 0x332F34
		switch (TrackId)
		{
		case 0: return 120.0;
		case 1: case 2: case 3: case 4: case 5: case 6: case 8: case 9: return 90.0;
		case 11: return 135.0;
		default: return 0.0;
		}
	}

	bool CatchupTrack(int32 TrackId, double& OutLate, double& OutOffset)
	{
		switch (TrackId)
		{
		case 0: OutLate = 140005.72f; OutOffset = 1532.3651f; return true;
		case 1: OutLate = 125085.09f; OutOffset = 907.67194f; return true;
		case 2: OutLate = 159083.5f; OutOffset = 421.18005f; return true;
		case 3: OutLate = 138040.55f; OutOffset = -504.2323f; return true;
		case 4: OutLate = 142028.88f; OutOffset = 503.4722f; return true;
		case 5: OutLate = 131061.625f; OutOffset = 46.695477f; return true;
		case 8: OutLate = 40011.445f; OutOffset = 0.0f; return true;
		case 11: OutLate = 160036.53f; OutOffset = 0.0f; return true;
		default: OutLate = 0.0; OutOffset = 0.0; return false;
		}
	}

	void CatchupDifficulty(bool bCircuit, int32 Difficulty, double& OutKeep, double& OutK)
	{
		static const double Race[3][2] = { { 1.0, 0.600729 }, { 0.5, 0.4006506 }, { 0.0, 0.3501341 } };
		static const double Circuit[3][2] = { { 1.0, 0.60011744 }, { 0.5, 0.39999962 }, { 0.0, 0.35052386 } };
		if (Difficulty < 0 || Difficulty > 2)
		{
			OutKeep = 1.0; OutK = 1.0;
			return;
		}
		const double* Row = bCircuit ? Circuit[Difficulty] : Race[Difficulty];
		OutKeep = float(Row[0]);   // 0x1157C0, 0x115884
		OutK = float(Row[1]);
	}

	int32 StartSlot(int32 RacerIndex, int32 RacerCount)
	{
		return RacerIndex + (StartSlots - RacerCount) / 2;
	}

	int32 StartPose(const FRacePathSet& Paths, int32 RacerIndex, int32 RacerCount, double RiderSize, FVector& OutPos, double& OutHeading)
	{
		const int32 Slot = StartSlot(RacerIndex, RacerCount);
		if (!Paths.StartPaths.IsValidIndex(Slot) || !Paths.AIPaths.IsValidIndex(Paths.StartPaths[Slot]))
		{
			return -1;
		}
		const int32 PathIndex = Paths.StartPaths[Slot];
		const FRacePath& Path = Paths.AIPaths[PathIndex];
		// 0x116AB8, 0x197738, 0x11EA00, 0x102F50
		const FVector T = Path.Dirs.Num() > 0 ? Path.Dirs[0] : FVector(1, 0, 0);
		const float S = float(RiderSize) * float(StartSizeSpacing) - float(StartSizeSpacing);
		OutPos = FVector(float(Path.Start.X) + S * float(T.X), float(Path.Start.Y) + S * float(T.Y), float(Path.Start.Z) + S * float(T.Z));
		OutHeading = SSXAnim::Atan2EE(float(T.Y), float(T.X));   // 0x2510F8
		return PathIndex;
	}

	double CatchupLead(double RivalDist, double RivalBearing, double PathHeading)
	{
		// 0x13A0F0, 0x250E98
		return -float(RivalDist) * SSXAnim::CosEE(SSXEE::FpuSub(float(RivalBearing), float(PathHeading)));
	}

	double CatchupFactorAI(bool bHasLead, double Lead, double Near, double Far)
	{
		if (!bHasLead)
		{
			return 1.0;
		}
		const float L = float(Lead), Nr = float(Near), Fr = float(Far);
		float F = 1.0f;
		if (L < Nr)
		{
			F = SSXEE::FpuAdd(SSXEE::FpuSub(L, Nr) / Nr, 1.0f) * float(AICatchupGain);
		}
		else if (Fr < L)
		{
			F = (1.0f / SSXEE::FpuSub(L, Fr)) * float(AICatchupFarGain);
		}
		if (F < float(AITimeScaleMin))
		{
			F = float(AITimeScaleMin);
		}
		else if (float(AITimeScaleMax) < F)
		{
			F = float(AITimeScaleMax);
		}
		return F;
	}

	double CatchupFactorPlayer(bool bHasLead, double Lead, double Near)
	{
		if (!bHasLead || Lead >= Near)
		{
			return 1.0;
		}
		const double F = ((Lead - Near) / Near + 1.0) * PlayerCatchupGain;
		return FMath::Clamp(F, 1.0, PlayerTimeScaleMax);
	}

	double StepTimeScale(double Current, double Target)
	{
		// 0x11CFF0
		const float Cur = float(Current), T = float(Target), Step = float(TimeScaleStep);
		if (SSXEE::FpuAdd(T, Step) < Cur)
		{
			return SSXEE::FpuSub(Cur, Step);
		}
		if (Cur < SSXEE::FpuSub(T, Step))
		{
			return SSXEE::FpuAdd(Cur, Step);
		}
		return T;
	}

	void SkillLevels(ERaceEventMode Mode, int32 Difficulty, double& OutAhead, double& OutBehind)
	{
		static const double Circuit[3][2] = { { 0.404814, 0.754853 }, { 0.708402, 0.909216 }, { 0.951308, 1.0 } };
		static const double Race[3][2] = { { 0.274136, 0.606922 }, { 0.656647, 0.872325 }, { 0.949089, 1.0 } };
		const int32 D = FMath::Clamp(Difficulty, 0, 2);
		if (Mode == ERaceEventMode::CircuitRace)
		{
			OutAhead = Circuit[D][0]; OutBehind = Circuit[D][1];
		}
		else if (Mode == ERaceEventMode::PracticeRace)
		{
			OutAhead = Race[D][0]; OutBehind = Race[D][1];
		}
		else
		{
			OutAhead = 0.50875; OutBehind = 1.0;
		}
		OutAhead = FMath::Min(OutAhead, 1.0);
		OutBehind = FMath::Min(OutBehind, 1.0);
	}

	double SelectSkill(double Current, double Ahead, double Behind, int32 Frame, bool bHasRival, int32 MyRank, int32 HumanRank)
	{
		if (Frame % SkillPeriod)
		{
			return Current;
		}
		if (!bHasRival || HumanRank < MyRank)
		{
			return Behind;
		}
		return Ahead;
	}

	bool RespawnPoint(const FRacePath& Path, double Param, TConstArrayView<FCrowdRacer> Others, FVector& OutPos,
	                  double& OutHeading, FVector& OutVel, TFunctionRef<bool(const FVector&)> SphereFree)
	{
		FVector P = Path.PointAt(Param);
		FVector T = Path.TangentAt(Param);
		double Heading = FMath::Atan2(T.Y, T.X);
		// 0x119220
		FVector Raw = Path.Dirs.Num() > 0 ? Path.Dirs[0] : FVector(1, 0, 0);
		{
			float Rest = float(Param);
			for (int32 Seg = 0; Seg < Path.Dirs.Num(); ++Seg)
			{
				Raw = Path.Dirs[Seg];
				if (Seg == Path.Dirs.Num() - 1 || Rest <= float(Path.Lengths[Seg]))
				{
					break;
				}
				Rest -= float(Path.Lengths[Seg]);
			}
		}
		bool bRawVelocity = true;
		struct FFrame { FVector Pos, Dir, Side; bool bDone; };
		TArray<FFrame, TInlineAllocator<8>> Frames;
		for (const FCrowdRacer& O : Others)
		{
			if ((O.Pos - P).Size() >= RespawnCrowdRange)
			{
				continue;
			}
			const double Sp = O.Vel.Size();
			const FVector D = Sp > 0.0 ? O.Vel / Sp : T;
			FVector Side = FVector::CrossProduct(FVector(0, 0, 1), D);
			Side = Side / FMath::Max(Side.Size(), 1e-9);
			Frames.Add({ O.Pos, D, Side, false });
		}
		if (Frames.Num())
		{
			FVector Shift = FVector::ZeroVector;
			bool bMoved = false;
			int32 Last = -1;
			while (true)
			{
				int32 Pick = -1;
				double Best = RespawnStep, CurLat = 0.0;
				for (int32 K = 0; K < Frames.Num(); ++K)
				{
					if (Frames[K].bDone) { continue; }
					const double Lat = FVector::DotProduct(P + Shift - Frames[K].Pos, Frames[K].Side);
					if (FMath::Abs(Lat) < RespawnCrowdLateral && FMath::Abs(Lat) < Best)
					{
						Pick = K; Best = FMath::Abs(Lat); CurLat = Lat;
					}
				}
				if (Pick < 0)
				{
					break;
				}
				const double Dir = CurLat >= 0.0 ? 1.0 : -1.0;
				Shift += Frames[Pick].Side * (Dir * RespawnStep - CurLat);
				Frames[Pick].bDone = true;
				bMoved = true;
				Last = Pick;
			}
			if (bMoved)
			{
				if (Shift.Size() <= RespawnMaxShift && SphereFree(P + Shift))
				{
					const FVector D = Frames[Last].Dir;
					if (FVector::DotProduct(D, T) > RespawnAlignCos)
					{
						T = D;
						bRawVelocity = false;
						Heading = FMath::Atan2(D.Y, D.X);
					}
					P += Shift;
				}
				else
				{
					return false;
				}
			}
		}
		OutPos = P;
		OutHeading = Heading;
		OutVel = bRawVelocity ? FVector(float(Raw.X) * float(RespawnSpeed), float(Raw.Y) * float(RespawnSpeed), 0.0) : FVector(T.X, T.Y, 0.0) * RespawnSpeed;
		return true;
	}

	void RelieveGrudge(FGrudge& G, FRivalry* Rivals, int32 How)
	{
		if (!G.bActive || G.Rival < 0)
		{
			return;
		}
		// 0x11EF00
		FRivalry& R = Rivals[G.Rival];
		if (How == 0)
		{
			G.bCooled = true;
			R.Anger -= 15;
		}
		else if (How == 1)
		{
			R.Anger -= 45;
		}
		else if (How == 2)
		{
			R.Anger -= FMath::Max(45, int32(R.Anger / 2));
		}
		R.Anger = FMath::Max(R.Anger, 0);
	}

	void StartGrudge(FGrudge& G, FRivalry* Rivals, int32 Target, int32 Frame)
	{
		if (G.bActive && G.Rival != Target)
		{
			RelieveGrudge(G, Rivals, 0);
		}
		G.bActive = true;
		G.bCooled = false;
		G.Frame = Frame;
		G.Rival = Target;
		FRivalry& R = Rivals[Target];
		if (R.Level >= GrudgeKind2Level) { G.Kind = 2; R.Score += 3; ++G.Kind2Starts; }   // 0x2336B8
		else if (R.Level >= GrudgeKind1Level) { G.Kind = 1; R.Score += 2; }
		else { G.Kind = 0; R.Score += 1; }
		if (R.bFriend) { G.Kind = 0; }
		R.Level = FMath::Min(R.Level + GrudgeLevelStep, R.LevelMax);
		R.Anger = 0;
	}

	void GrudgeCooldown(FGrudge& G, FRivalry* Rivals, int32 Frame)
	{
		if (G.bActive && G.Kind == 1 && !G.bCooled && float(G.Frame) + GrudgeCoolFrames < float(Frame))
		{
			RelieveGrudge(G, Rivals, 0);
		}
	}

	bool GrudgeWantsAttack(const FGrudge& G, int32 Other, ERaceEventMode Mode)
	{
		if (Mode == ERaceEventMode::PracticeKnockdown)
		{
			return true;
		}
		if (!G.bActive || G.Rival != Other)
		{
			return false;
		}
		return G.Kind == 2 || (G.Kind == 1 && !G.bCooled);
	}

	void ResetRivalry(FRivalry& R, const FRivalry& Setup)
	{
		R = FRivalry();
		R.Level = R.StartLevel = Setup.Level;
		R.LevelMax = Setup.LevelMax;
		R.AngerLimit = Setup.AngerLimit;
		R.Kind = Setup.Kind;
		R.bFriend = Setup.Kind == 1 || Setup.bFriend;
	}

	void BlendLevel(FRivalry& R)
	{
		const float Start = float(R.StartLevel);
		const float B = float(uint16(R.Bumps)), S = float(uint16(R.Shoves)), K = float(uint16(R.Hits));
		const float Sum = B + S + K;
		if (Sum == 0.f)
		{
			R.Level = int32(int8(int32(Start)));
			return;
		}
		const float W = (B + S + S + K * 3.f) / (Sum * 3.f);
		R.Level = int32(int8(int32(Start + W * (float(R.Level) - Start))));
	}

	void OnBump(FGrudge& VictimGrudge, FRivalry* VictimRivals, FGrudge& AttackerGrudge, FRivalry* AttackerRivals,
	            int32 VictimIndex, int32 AttackerIndex, bool bVictimAI, bool bAttackerAI, int32 Frame, bool bShove)
	{
		FRivalry& Rec = VictimRivals[AttackerIndex];
		if (bShove) { Rec.Shoves += 1; } else { Rec.Bumps += 1; }
		if (bShove && bAttackerAI && !bVictimAI)
		{
			if (AttackerGrudge.bActive && AttackerGrudge.Rival == VictimIndex)
			{
				RelieveGrudge(AttackerGrudge, AttackerRivals, 1);
			}
			AttackerRivals[VictimIndex].Score += 1;
		}
		else if (bVictimAI && !bAttackerAI)
		{
			Rec.Anger = FMath::Min(Rec.Anger + (bShove ? 15 : 9), Rec.AngerLimit);
			if (Rec.Anger == Rec.AngerLimit)
			{
				StartGrudge(VictimGrudge, VictimRivals, AttackerIndex, Frame);
			}
		}
	}

	void OnKnockdown(FGrudge& VictimGrudge, FRivalry* VictimRivals, FGrudge& AttackerGrudge, FRivalry* AttackerRivals,
	                 int32 VictimIndex, int32 AttackerIndex, bool bVictimAI, bool bAttackerAI, int32 Frame)
	{
		FRivalry& Rec = VictimRivals[AttackerIndex];
		Rec.Hits += 1;
		if (bAttackerAI && !bVictimAI)
		{
			if (AttackerGrudge.bActive && AttackerGrudge.Rival == VictimIndex)
			{
				RelieveGrudge(AttackerGrudge, AttackerRivals, 2);
			}
			AttackerRivals[VictimIndex].Score += 2;
		}
		else if (bVictimAI && !bAttackerAI)
		{
			Rec.Anger = FMath::Min(Rec.Anger + GrudgeAngerPerHit, Rec.AngerLimit);
			if (Rec.Anger == Rec.AngerLimit)
			{
				StartGrudge(VictimGrudge, VictimRivals, AttackerIndex, Frame);
			}
		}
	}
}
