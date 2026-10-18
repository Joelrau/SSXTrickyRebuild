#include "Race/AIInput.h"

#include "Input/PadMapper.h"
#include "Race/RaceRandom.h"
#include "Race/RaceTrackData.h"

namespace SSXRaceAI
{
	namespace
	{
		using namespace SSXRace;

		enum : int32
		{
			CBump = 2, CCruise = 3, CChangeFakie = 4, CFakieCruise = 5, CGateToBase = 7, CGateAnticipate = 8,
			CGateLaunch = 9, CJump = 10, CLandHard = 11, CLandNormal = 12, CNaturalAir = 13, CPrewind = 14,
			CRailSlide = 15, CSpin = 16, CSitAndWait = 17, CGetupFromSit = 18, CWipeOut = 19, CWipeOutRecover = 20,
		};
		enum : int32 { MAir = 1, MRail = 3, MWipeOut = 5, MRecover = 6 };

		bool IsGroundLayout(int32 C)
		{
			return C == CBump || C == CCruise || C == CChangeFakie || C == CGateToBase || C == CLandHard || C == CLandNormal
				|| C == CGetupFromSit;
		}

		double Scaled(double Value, double Skill, double Divisor)
		{
			return Skill == 1.0 ? Value : Value * (Skill / Divisor);
		}

		uint32 Q6(double V) { return uint32(int32(V * 31.0) & 63); }
		uint32 S2(double V) { return V < -0.5 ? 3u : (V > 0.5 ? 1u : 0u); }

		uint32 SetField(uint32 W, int32 Lo, int32 Bits, uint32 Value)
		{
			const uint32 Mask = ((1u << Bits) - 1u) << Lo;
			return (W & ~Mask) | ((Value << Lo) & Mask);
		}

		double Speed(const FVector& V) { return V.Size(); }

		double PitchToward(const FAIRiderView& R, const FVector& Normal, double Skill)
		{
			FVector V = Normal - FVector::DotProduct(Normal, R.Side) * R.Side;
			const double N = V.Size();
			if (N == 0.0)
			{
				return 0.0;
			}
			return Scaled(FVector::DotProduct(V / N, R.Fwd), Skill, SkillPitch);
		}

		double TrickTime(int32 StatAir)
		{
			return TrickTimeA / (StatAir / 255.0 * TrickTimeB + TrickTimeC);
		}

		int32 RandomGrab(const FAIRiderView& R, FRaceRandom& Rng)
		{
			// 0x11D368, 0x333098
			return FPadMapper::GrabSlot(R.Character, R.ProfileBoardClass, Rng.Mod(15));
		}

		bool LeadOverRival(const FAIRiderView& R, double& OutLead)
		{
			if (R.CatchupRival < 0 || R.CatchupRival >= MaxRacers)
			{
				return false;
			}
			OutLead = CatchupLead(R.RivalDist[R.CatchupRival], R.RivalBearing[R.CatchupRival], R.PathHeading);
			return true;
		}

		struct FZone { bool bInside; bool bSpin; bool bFlip; };

		bool JumpZone(FAIState& St, const FAIRiderView& R, FZone& Out)
		{
			const FRacePathEvent* E = R.Path ? R.Path->FindEvent(R.PathParamPrev, R.PathParam + JumpWindow, SSXRaceEvent::AIJump) : nullptr;
			if (!E)
			{
				return false;
			}
			St.TargetSpeed = (E->Param >> 2) * KmhToCms;
			Out = { E->Start <= R.PathParam, ((E->Param >> 1) & 1u) != 0, (E->Param & 1u) != 0 };
			return true;
		}

		bool SpeedRequest(const FAIRiderView& R, double& OutSpeed, bool& bOutInside)
		{
			const FRacePathEvent* E = R.Path ? R.Path->FindEvent(R.PathParamPrev, R.PathParam + SpeedWindow, SSXRaceEvent::AISpeed) : nullptr;
			if (!E)
			{
				return false;
			}
			OutSpeed = E->Param * KmhToCms;
			bOutInside = E->Start <= R.PathParam;
			return true;
		}

		uint32 Tuck(const FAIState& St, const FAIRiderView& R, uint32 W, bool bBoost = false)
		{
			double C = Scaled(1.0, St.Skill, SkillTuck);
			if (Speed(R.Vel) < TuckSlow)
			{
				C = 1.0;
			}
			W = SetField(W, 11, 6, Q6(C));
			return bBoost ? (W | 8u) : W;
		}

		uint32 Brake(uint32 W) { return SetField(W, 17, 6, 31u); }
		uint32 SetTurn(uint32 W, double Steer) { return SetField(W, 5, 6, Q6(Steer)); }

		// 0x139D58
		void ChooseTrick(FAIState& St, const FAIRiderView& R, bool bSpin, bool bFlip, FRaceRandom& Rng)
		{
			St.Trick = NoTrick;
			St.bSpinFlipOn = (bSpin || bFlip) && (Rng.Mod(100) - TrickStatOffset < R.StatTrick);
			const int32 Roll = Rng.Mod(100);
			const double Hold = St.Skill == 1.0 ? 0.0 : 1.0 - St.Skill / SkillHold;
			St.bShortHold = ShortHoldLimit < Roll * Hold;
			St.bTrickAllowed = false;
			St.Trick = (Rng.Mod(100) - TrickStatOffset < R.StatTrick) ? RandomGrab(R, Rng) : NoTrick;
			if (St.bSpinFlipOn)
			{
				St.SpinDir = bSpin ? 1.0 : 0.0;
				St.FlipDir = bFlip ? 1.0 : 0.0;
				if (Rng.Next() & 1u) { St.SpinDir = -St.SpinDir; }
				if (Rng.Next() & 1u) { St.FlipDir = -St.FlipDir; }
			}
		}

		bool ResetRequest(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race)
		{
			// 0x139AB8
			const float X = float(R.PathHeading - R.Yaw);
			const double Err = double(X - FMath::FloorToFloat(X * 0.15915494f + 0.5f) * 6.2831855f);
			if (St.Target < 0)
			{
				St.WrongWayFrames = FMath::Abs(Err) > FMath::DegreesToRadians(60.0) ? St.WrongWayFrames + 1 : 0;
			}
			else
			{
				St.WrongWayFrames = 0;
			}
			bool bWrongWay = St.WrongWayFrames > WrongWayFrames;
			if (R.Motion == MWipeOut)
			{
				St.OffPathFrames += 1;
				bWrongWay = false;
			}
			else
			{
				if (R.Motion == MRail && St.WrongWayFrames > 0)
				{
					return true;
				}
				if (Race.Mode == ERaceEventMode::PracticeKnockdown)
				{
					St.OffPathFrames = FMath::Abs(R.PathLateral) >= OffPathLateralKnockdown ? St.OffPathFrames + 1 : 0;
				}
				else if (R.Motion == MAir || R.Motion == MRecover)
				{
					bWrongWay = false;
					St.OffPathFrames = 0;
					if (R.Motion == MAir && R.AirElapsed > AirTimeout)
					{
						return true;
					}
				}
				else if (FMath::Abs(R.PathLateral) >= OffPathLateral)
				{
					St.OffPathFrames += 1;
				}
				else
				{
					bWrongWay = false;
					St.OffPathFrames = 0;
				}
			}
			const float Count = float(St.OffPathFrames);
			const float Stuck = St.Skill == 1.0 ? Count : Count * (float(St.Skill) / float(SkillStuck));
			const bool bWant = bWrongWay || Stuck > float(OffPathFrames);
			if (R.Control == CWipeOut || R.Control == CWipeOutRecover)
			{
				double Lead = 0.0;
				const bool bHasLead = LeadOverRival(R, Lead);
				const bool bFarBehind = bHasLead && Lead < R.CatchupNear;
				if (!bWant && !bFarBehind)
				{
					return false;
				}
				return !(bHasLead && Lead > R.CatchupFar);
			}
			return bWant && St.Target < 0;
		}

		uint32 ShoveSide(const FAIRiderView& R, const FAIRaceView& Race)
		{
			if (R.Channel1Anim == ShoveBlockAnim || Race.Frame % ShovePeriod)
			{
				return 0;
			}
			const SSXRace::FGrudge& G = R.Grudge;
			for (const FAIOtherRider& O : Race.Riders)
			{
				const int32 J = O.Index;
				if (J == R.Index || J < 0 || J >= MaxRacers || R.RivalDist[J] >= ShoveRange)
				{
					continue;
				}
				const double Diff = WrapAngle(R.Yaw - R.RivalBearing[J]);
				if (FMath::Abs(Diff) > FMath::DegreesToRadians(30.0) && FMath::Abs(Diff) < FMath::DegreesToRadians(150.0)
					&& G.bActive && G.Rival == J && (G.Kind == 1 || G.Kind == 2))
				{
					return Diff > 0.0 ? 3u : 1u;
				}
			}
			return 0;
		}

		bool SelectTarget(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, int32& OutAttack)
		{
			OutAttack = -1;
			if (St.Target >= 0)
			{
				if (R.RivalDist[St.Target] > TargetDrop || FMath::Abs(R.PathLateral) > TargetLateralDrop)
				{
					St.Target = -1;
				}
				else
				{
					return true;
				}
			}
			if (FMath::Abs(R.PathLateral) < TargetLateralMax && Race.Frame % TargetPeriod == 0)
			{
				double Best = 1e10;
				for (const FAIOtherRider& O : Race.Riders)
				{
					const int32 J = O.Index;
					if (J == R.Index || J < 0 || J >= MaxRacers || R.RivalDist[J] > TargetRange)
					{
						continue;
					}
					const bool bK = GrudgeWantsAttack(R.Grudge, J, Race.Mode);
					const double C = FMath::Cos(WrapAngle(R.RivalBearing[J] - R.Yaw));
					const double Score = R.RivalDist[J] * (bK ? (1.0 + FMath::Abs(C)) : (1.0 - C));
					if (Score <= Best)
					{
						Best = Score;
						OutAttack = bK ? 1 : 0;
						St.Target = J;
					}
				}
			}
			return St.Target >= 0;
		}

		uint32 BehaviourCruise(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, uint32 W)
		{
			W = SetTurn(W, SteerToward(R, R.Lookahead, St.Skill));
			double Req = 0.0;
			bool bInside = false;
			const double V = Speed(R.Vel);
			if (SpeedRequest(R, Req, bInside))
			{
				St.TargetSpeed = Req;
				if (V > St.TargetSpeed + SpeedBand)
				{
					if (bInside) { W = Brake(W); }
				}
				else if (V < Scaled(St.TargetSpeed - SpeedBand, St.Skill, SkillSpeed))
				{
					W = Tuck(St, R, W, bInside);
				}
			}
			else
			{
				const bool bBoost = Scaled(R.BoostMeter, St.Skill, SkillSpeed) >= 1.0;
				W = Tuck(St, R, W, bBoost);
			}
			FZone Z;
			if (JumpZone(St, R, Z))
			{
				St.Target = -1;
				St.Behaviour = Jump;
				return W;
			}
			int32 Attack = -1;
			const bool bHas = SelectTarget(St, R, Race, Attack);
			if (bHas && Attack >= 0)
			{
				St.Behaviour = Attack ? SSXRaceAI::Attack : Pass;
			}
			return W;
		}

		uint32 BehaviourJump(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, uint32 W, FRaceRandom& Rng)
		{
			FZone Z;
			if (!JumpZone(St, R, Z))
			{
				St.Behaviour = Cruise;
				return BehaviourCruise(St, R, Race, W);
			}
			const double Steer = SteerToward(R, R.Lookahead, St.Skill);
			W = SetTurn(W, Steer);
			const double V = Speed(R.Vel);
			if (Z.bInside && FMath::Abs(R.PathLateral) < JumpMaxLateral && FMath::Abs(Steer) < JumpMaxSteer)
			{
				ChooseTrick(St, R, Z.bSpin, Z.bFlip, Rng);
				return W | 2u;
			}
			if (V < St.TargetSpeed - SpeedBand)
			{
				return Tuck(St, R, W, true);
			}
			if (V > St.TargetSpeed + SpeedBand)
			{
				return Brake(W);
			}
			return W;
		}

		uint32 BehaviourAttack(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, uint32 W)
		{
			const FAIOtherRider& O = Race.Riders[St.Target];
			const double Rel = WrapAngle(R.RivalBearing[St.Target] - R.PathHeading);
			const double Mine = Speed(R.Vel), Theirs = Speed(O.Vel);
			double Steer;
			if (FMath::Abs(Rel) > UE_DOUBLE_HALF_PI)
			{
				Steer = SteerToward(R, R.Lookahead, St.Skill);
				if (R.RivalDist[St.Target] > AttackWait && Mine > Theirs)
				{
					W = Brake(W);
				}
			}
			else
			{
				const double Sp = Speed(O.Vel);
				const FVector Aim = O.Pos + (Sp > 0.0 ? O.Vel / Sp * AttackLead : FVector::ZeroVector);
				if (FMath::Abs(Rel) < FMath::DegreesToRadians(50.0))
				{
					W = Tuck(St, R, W, FMath::Abs(Rel) < FMath::DegreesToRadians(15.0));
				}
				else if (Mine < Theirs)
				{
					W = Tuck(St, R, W);
				}
				else if (Mine > Theirs)
				{
					W = Brake(W);
				}
				Steer = SteerToward(R, Aim, St.Skill);
			}
			W = SetTurn(W, Steer);
			FZone Z;
			if (JumpZone(St, R, Z))
			{
				St.Target = -1;
				St.Behaviour = Jump;
				return W;
			}
			int32 Attack = -1;
			const bool bHas = SelectTarget(St, R, Race, Attack);
			if (!bHas)
			{
				St.Behaviour = Cruise;
			}
			else if (Attack == 0)
			{
				St.Behaviour = Pass;
			}
			return W;
		}

		uint32 BehaviourPass(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, uint32 W)
		{
			const FAIOtherRider& O = Race.Riders[St.Target];
			const double Rel = WrapAngle(R.RivalBearing[St.Target] - R.PathHeading);
			const double Mine = Speed(R.Vel), Theirs = Speed(O.Vel);
			double Steer;
			if (FMath::Abs(Rel) > UE_DOUBLE_HALF_PI)
			{
				Steer = SteerToward(R, R.Lookahead, St.Skill);
				W = Tuck(St, R, W, Mine < Theirs);
			}
			else
			{
				const double Dist = R.RivalDist[St.Target];
				double Cone = 0.0;
				if (Dist != 0.0)
				{
					Cone = FMath::Atan(PassClearance / Dist) + (Dist < 0.0 ? UE_DOUBLE_PI : 0.0);
				}
				if (Cone < FMath::Abs(Rel) || Mine <= Theirs)
				{
					Steer = SteerToward(R, R.Lookahead, St.Skill);
					W = Tuck(St, R, W);
				}
				else
				{
					const double Ang = Rel > 0.0 ? Rel - Cone : Cone - Rel;
					const double S = FMath::Sin(Ang), C = FMath::Cos(Ang);
					const double Dx = R.Lookahead.X - R.Pos.X, Dy = R.Lookahead.Y - R.Pos.Y;
					const FVector Aim(R.Pos.X + Dx * C - Dy * S, R.Pos.Y + Dx * S + Dy * C, R.Lookahead.Z);
					Steer = SteerToward(R, Aim, St.Skill);
					W = Tuck(St, R, W, true);
				}
			}
			W = SetTurn(W, Steer);
			FZone Z;
			if (JumpZone(St, R, Z))
			{
				St.Target = -1;
				St.Behaviour = Jump;
				return W;
			}
			int32 Attack = -1;
			const bool bHas = SelectTarget(St, R, Race, Attack);
			if (!bHas)
			{
				St.Behaviour = Cruise;
			}
			else if (Attack == 1)
			{
				St.Behaviour = SSXRaceAI::Attack;
			}
			return W;
		}

		uint32 GroundWord(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, FRaceRandom& Rng)
		{
			uint32 W = ResetRequest(St, R, Race) ? 1u : 0u;
			if (const uint32 Side = ShoveSide(R, Race))
			{
				W = SetField(W, 23, 2, Side);
			}
			switch (St.Behaviour)
			{
			case Jump: return BehaviourJump(St, R, Race, W, Rng);
			case Pass: return BehaviourPass(St, R, Race, W);
			case SSXRaceAI::Attack: return BehaviourAttack(St, R, Race, W);
			default: return BehaviourCruise(St, R, Race, W);
			}
		}

		uint32 GateWord(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race)
		{
			const double ToGo = -Race.Clock;
			if (ToGo < Scaled(GatePushTime, St.Skill, SkillGate))
			{
				return 31u;
			}
			if (ToGo < Scaled(GateBackTime, St.Skill, SkillGate))
			{
				return 33u;
			}
			if (R.GateCur == R.GateTarget || St.TargetSpeed == 0.0)
			{
				St.TargetSpeed = R.GateCur > 0.0 ? -1.0 : 1.0;
			}
			return Q6(St.TargetSpeed);
		}

		uint32 PrewindWord(FAIState& St, const FAIRiderView& R)
		{
			uint32 W = 0;
			bool bEdge = false;
			const FRacePathEvent* E = R.Path ? R.Path->FindEvent(R.PathParamPrev, R.PathParam + PrewindWindow, SSXRaceEvent::AIJump, &bEdge) : nullptr;
			if (E && bEdge)
			{
				if (Speed(R.Vel) < St.TargetSpeed - SpeedBand)
				{
					W |= 8u;
				}
				W |= 2u;
			}
			W = SetField(W, 17, 6, Q6(SteerToward(R, R.Lookahead, St.Skill)));
			W = SetField(W, 5, 6, Q6(St.SpinDir));
			return SetField(W, 11, 6, Q6(St.FlipDir));
		}

		uint32 NaturalAirWord(FAIState& St, const FAIRiderView& R)
		{
			St.bTrickAllowed = true;
			St.bSpinFlipOn = false;
			St.bShortHold = false;
			uint32 W = 0;
			if (R.PredTimeLeft < 0.5)
			{
				W = SetField(W, 8, 6, Q6(-SteerToward(R, R.Lookahead, St.Skill)));
				St.Trick = NoTrick;
			}
			return W;
		}

		uint32 RailWord(FAIState& St, const FAIRiderView& R, FRaceRandom& Rng)
		{
			uint32 W = 0;
			FZone Z;
			const bool bZone = JumpZone(St, R, Z);
			if (!bZone || !Z.bInside)
			{
				const double Bal = FMath::Clamp(-2.0 * R.Side.Z, -1.0, 1.0);
				if (FMath::Abs(Bal) > 0.1)
				{
					return SetTurn(W, Bal);
				}
				if (R.StatTrick <= Rng.Mod(100) - TrickStatOffset)   // 0x138BB8
				{
					return W;
				}
				W |= 8u;
				if (St.SpinDir == 0.0)
				{
					St.SpinDir = Bal;
				}
				return SetTurn(W, St.SpinDir < 0.0 ? 1.0 : -1.0);
			}
			ChooseTrick(St, R, Z.bSpin, Z.bFlip, Rng);
			return W | 2u;
		}

		uint32 SpinWord(FAIState& St, const FAIRiderView& R, FRaceRandom& Rng)
		{
			const double TAir = TrickTime(R.StatAir);
			const double Pred = R.PredTimeLeft;
			if (St.bSpinFlipOn)
			{
				if (St.FlipDir != 0.0)
				{
					const int32 A = FMath::Abs(int32(FMath::RadiansToDegrees(R.FlipAngle))) % 360;
					if (!(A >= 5 && A < 341) && Pred < UE_DOUBLE_TWO_PI / FMath::Max(FMath::Abs(R.FlipRate), 1e-9))
					{
						St.FlipDir = 0.0;
					}
				}
				if (St.SpinDir != 0.0)
				{
					const int32 Step = R.BoardClass == 2 ? 360 : 180;
					const int32 A = FMath::Abs(int32(FMath::RadiansToDegrees(R.SpinAngle))) % Step;
					if ((A > Step - 20 || A < 5) && Pred < FMath::DegreesToRadians(double(Step)) / FMath::Max(FMath::Abs(R.SpinRate), 1e-9))
					{
						St.SpinDir = 0.0;
					}
				}
			}
			if (St.Trick == NoTrick)
			{
				if (St.bTrickAllowed && R.AirElapsed > TAir && Pred > TAir)
				{
					St.Trick = RandomGrab(R, Rng);
					St.bTrickAllowed = false;
				}
			}
			else if (Pred < (St.bShortHold ? TrickShortRelease : TAir))
			{
				St.Trick = NoTrick;
			}
			uint32 W = SetField(0, 12, 2, S2(St.SpinDir));
			W = SetField(W, 14, 2, S2(St.FlipDir));
			W = SetField(W, 0, 8, uint32(St.Trick));
			if (St.Trick == NoTrick && St.SpinDir == 0.0 && St.FlipDir == 0.0)
			{
				W = SetField(W, 20, 2, S2(-SteerToward(R, R.Lookahead, St.Skill)));
				W = SetField(W, 22, 2, S2(-PitchToward(R, R.PredLandNormal, St.Skill)));
			}
			return W;
		}
	}

	double SteerToward(const FAIRiderView& R, const FVector& Target, double Skill)
	{
		FVector D = Target - R.Pos;
		double N = D.Size();
		if (N == 0.0)
		{
			return 0.0;
		}
		D /= N;
		D -= FVector::DotProduct(D, R.Up) * R.Up;
		N = D.Size();
		if (N == 0.0)
		{
			return 0.0;
		}
		D /= N;
		const double Ang = -FMath::Atan2(FVector::DotProduct(D, R.Side), FVector::DotProduct(D, R.Fwd));
		const double Mag = Scaled(FMath::Abs(Ang) * SteerGain, Skill, SkillSteer);
		if (Mag < SteerDead)
		{
			return 0.0;
		}
		return FMath::Min(Mag, SteerMax) * (Ang < 0.0 ? -1.0 : 1.0);
	}

	bool ResetDecision(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race)
	{
		return ResetRequest(St, R, Race);
	}

	void ResetAI(FAIState& St, double SkillAhead, double SkillBehind)
	{
		St.bSpinFlipOn = St.bShortHold = St.bTrickAllowed = false;
		St.SpinDir = St.FlipDir = 0.0;
		St.TargetSpeed = 0.0;
		St.OffPathFrames = St.WrongWayFrames = 0;
		St.Trick = NoTrick;
		St.Target = -1;
		St.SkillAhead = SkillAhead;
		St.SkillBehind = SkillBehind;
		St.Skill = SkillBehind;
		St.Behaviour = Cruise;
	}

	uint32 AIWord(FAIState& St, const FAIRiderView& R, const FAIRaceView& Race, FRaceRandom& Rng)
	{
		double Lead = 0.0;
		const bool bHasLead = LeadOverRival(R, Lead);
		St.TimeScale = StepTimeScale(St.TimeScale, CatchupFactorAI(bHasLead, Lead, R.CatchupNear, R.CatchupFar));
		const int32 C = R.Control;
		uint32 W = 0;
		if (IsGroundLayout(C))
		{
			W = GroundWord(St, R, Race, Rng);
		}
		else if (C == CFakieCruise)
		{
			W = SetTurn(0, 1.0);
		}
		else if (C == CGateAnticipate)
		{
			W = GateWord(St, R, Race);
		}
		else if (C == CGateLaunch || C == CSitAndWait)
		{
			W = 31u;
		}
		else if (C == CNaturalAir)
		{
			W = NaturalAirWord(St, R);
		}
		else if (C == CPrewind)
		{
			W = PrewindWord(St, R);
		}
		else if (C == CRailSlide)
		{
			W = RailWord(St, R, Rng);
		}
		else if (C == CSpin)
		{
			W = SpinWord(St, R, Rng);
		}
		else if (C == CWipeOut || C == CWipeOutRecover)
		{
			W = ResetRequest(St, R, Race) ? 1u : 0u;
		}
		St.Skill = SelectSkill(St.Skill, St.SkillAhead, St.SkillBehind, Race.Frame, R.CatchupRival >= 0, R.Rank, Race.HumanRank);
		return W;
	}
}
