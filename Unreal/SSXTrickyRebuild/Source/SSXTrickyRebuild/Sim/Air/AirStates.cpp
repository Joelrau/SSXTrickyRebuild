#include "Sim/Air/AirStates.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderAnimDriver.h"

#include "Sim/Ground/GroundControls.h"
#include "Sim/Ground/PropCollision.h"

#include "Sim/RiderSim.h"
#include "Sim/Ground/GroundMotion.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/SurfaceTable.h"

namespace SSXAir
{
namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;

	FORCEINLINE FVector AirUnit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}

	FORCEINLINE FQuat AirAxisAngle(const FVector& Axis, double Angle)
	{
		const double S = FMath::Sin(Angle * 0.5);
		return FQuat(Axis.X * S, Axis.Y * S, Axis.Z * S, FMath::Cos(Angle * 0.5));
	}

	// 0x1029D0, 0x108378
	FORCEINLINE FVector PivotFoldShift(const FQuat& QOld, const FQuat& QNew, double Pz)
	{
		const FVector V(0.0, 0.0, Pz);
		return QOld.RotateVector(V) - QNew.RotateVector(V);
	}

	FORCEINLINE int32 Dir2(double V) { return V > 0.5 ? 1 : (V < -0.5 ? -1 : 0); }

	bool RidingFakie(const FRiderSim& Sim) { return Sim.State.bStanceGoofy != (Sim.Profile.Stance != 0); }

	void EnvelopeFromSeed(double P, double& Hi, double& Lo, double& Floor)
	{
		if (P > 0.0)      { Hi = P; Lo = LoFraction * P; Floor = FMath::Min(P, RateFloorMax); }
		else if (P < 0.0) { Hi = LoFraction * P; Lo = P; Floor = FMath::Min(-P, RateFloorMax); }
		else              { Hi = Lo = Floor = 0.0; }
	}

	void EnvelopeStep(double K, double& Hi, double& Lo, double Floor, double& Rate)
	{
		Hi *= K;
		Lo *= K;
		if (Lo < 0.0)
		{
			if (-Floor < Hi)
			{
				Hi = -Floor;
				if (-Floor < Lo)
				{
					Lo = -Floor;
				}
			}
		}
		else if (Lo < Floor)
		{
			Lo = Floor;
			if (Hi < Floor)
			{
				Hi = Floor;
			}
		}
		Rate = Rate < Lo ? Lo : (Rate > Hi ? Hi : Rate);
	}

	void BailFromAir(FRiderSim& Sim, FAirContext& Ctx, const FRiderImpact& Impact, const TCHAR* Reason)
	{
		const bool bPending = Ctx.Score.Value > 0.0;
		Sim.State.CrowdCueX = FMath::Max(0.0, Ctx.Score.Value * 0.6786545);   // 0x2181E8
		Sim.State.bCrowdCueBail = true;
		++Sim.State.CrowdCueCount;
		const double Reward = Ctx.Score.Bail();
		Sim.StartWipeOut(Impact, Reason, bPending);
		BoostAdd(Sim.State, Reward, BoostGain(Sim.Profile));
		Sim.State.TrickId = NoTrick;
		Sim.State.TrickPhase = 0;
		Sim.State.TrickHoldFrames = 0;
		Sim.State.SphereMask = -1;
		Ctx.Anim->Trick.Stop();
	}

	TMap<const FRiderSim*, TWeakPtr<FAirContext>>& Registry()
	{
		static TMap<const FRiderSim*, TWeakPtr<FAirContext>> Map;
		return Map;
	}

	class FAirMotion final : public IRiderMotionState
	{
	public:
		explicit FAirMotion(const TSharedRef<FAirContext>& InCtx) : Ctx(InCtx) {}

		virtual void Enter(FRiderSim& Sim, ERiderMotion Previous) override
		{
			FRiderState& St = Sim.State;
			const FLandingPredictorResult Pred = PredictLanding(Sim.GetWorld(), St.Position, St.Velocity, Ctx->Config.bRefinePredictorHit,
			                                                   Ctx->Config.PredictorQueryType);
			Ctx->LastPrediction = Pred;
			FLandingPrediction& LP = St.Prediction;
			LP.AirElapsed = 0.0;
			LP.FlightTime = Pred.bHit ? Pred.HitTime : 0.0;
			LP.TimeLeft = LP.FlightTime;
			// 0x123B10
			LP.LandPoint = Pred.bHit ? Pred.Point : St.Position;
			LP.LandVelocity = Pred.Velocity;
			LP.LandNormal = Pred.bHit ? Pred.Normal : St.GroundNormal;
			LP.LandSurface = Pred.bHit ? Pred.Surface : -1;
			LP.ApexTime = Pred.ApexTime;
			LP.ApexLeft = Pred.bHit ? Pred.ApexTime : 0.0;
			St.TrickId = NoTrick;
			St.TrickPhase = 0;
			St.TrickHoldFrames = 0;
			Ctx->Anim->Trick.Stop();
			Ctx->Anim->Auto180FramesLeft = 0;
			Ctx->bTrickRegistered = false;
			Ctx->bChannel0AirIdle = false;
			Ctx->SpinAnim = FSpinAnimModel();
			Ctx->bTookOffFromRail = Previous == M::RailSlide;
			// FUN_00156618, 0x11B018, 0x1284E0, 0x10B0D0
			const double RailReward = Ctx->Score.StartSegment(RidingFakie(Sim), St.GrindType, false, St.bLaunchWall);
			Sim.LogNote(FString::Printf(TEXT("take-off: switch %d (+0x1B4 %d, profile +0x40 %d, board class %d)"),
				RidingFakie(Sim) ? 1 : 0, St.bStanceGoofy ? 1 : 0, Sim.Profile.Stance, St.BoardClass));
			St.bLaunchWall = false;
			if (RailReward > 0.0)
			{
				BoostAdd(St, RailReward, BoostGain(Sim.Profile));
			}
			Ctx->AirFrames = 0;
			Ctx->ApexZ = St.Position.Z;
			bRisingSeen = St.Velocity.Z > 0.0;
		}

		virtual void Update(FRiderSim& Sim) override
		{
			FRiderState& St = Sim.State;
			const IRiderWorld* World = Sim.GetWorld();

			BallisticStep(St.Position, St.Velocity, Sim.MotionDt());
			St.SpeedCap = AirSpeedCap;
			FLandingPrediction& LP = St.Prediction;
			// 0x123EC8
			LP.AirElapsed = SSXEE::FpuAdd(float(LP.AirElapsed), 0.016666668f);
			LP.TimeLeft = SSXEE::FpuSub(float(LP.FlightTime), float(LP.AirElapsed));
			LP.ApexLeft = SSXEE::FpuSub(float(LP.ApexTime), float(LP.AirElapsed));
			if (LP.LandSurface != -1)
			{
				St.SurfaceId = LP.LandSurface;
			}
			++Ctx->AirFrames;
			Ctx->ApexZ = FMath::Max(Ctx->ApexZ, St.Position.Z);
			bRisingSeen |= St.Velocity.Z > 0.0;
			if (!World)
			{
				return;
			}

			const FVector3f Rx = SSXEE::F(St.RenderRow0), Ry = SSXEE::F(St.RenderRow1), Rz = SSXEE::F(St.RenderRow2), Rt = SSXEE::F(St.RenderRow3);
			const FVector3f Pv = SSXEE::F(St.PoseRoot);
			const FVector3f BoardPoint(((Rx.X * -Pv.X + Ry.X * -Pv.Y) + Rz.X * -Pv.Z) + Rt.X, ((Rx.Y * -Pv.X + Ry.Y * -Pv.Y) + Rz.Y * -Pv.Z) + Rt.Y,
				((Rx.Z * -Pv.X + Ry.Z * -Pv.Y) + Rz.Z * -Pv.Z) + Rt.Z);
			FRiderWorldHit RayHit;
			const bool bRay = World->LineQuery(SSXEE::D(SSXEE::Add(BoardPoint, SSXEE::Scale(-100.0f, Rz))), SSXEE::D(SSXEE::Add(BoardPoint, SSXEE::Scale(200.0f, Rz))),
				ERiderLineQuery::Fine, 0.5, RayHit);
			if (bRay)
			{
				St.GroundNormal = RayHit.Normal;
			}
			const float Vn = SSXEE::Dot(SSXEE::F(St.GroundNormal), SSXEE::F(St.Velocity));

			// 0x25AFF8
			if (bRay && Vn < 0.0f)
			{
				// 0x128AE8
				const FVector FoldShift = PivotFoldShift(St.Orientation, FoldTrick(St.Orientation, St.SpinAngle, St.FlipAngle), St.PoseRoot.Z);
				const FVector Keep = St.Position;
				St.Position = SSXEE::D(SSXEE::Add(SSXEE::F(Keep), SSXEE::F(FoldShift)));
				FGroundProbeResult R;
				const bool bHit = Sim.RunGroundProbe(&R);
				St.Position = Keep;
				if (bHit && float(St.GroundHeight) < float(Sim.Surface(St.SurfaceId).StickHeight()) * 0.5f)
				{
					Touchdown(Sim, R, FoldShift);
					return;
				}
			}

			// 0x125CB8
			SSXGround::TryRailAttach(Sim);
			const ERiderMotion AfterCatch = St.Motion;
			// 0x125088, 0x257810
			SSXGround::PropCollision(Sim, St.Motion, Ctx->Anim.Get(), nullptr, St.SphereMask);
			if (St.Motion != AfterCatch)
			{
				return;
			}
			St.SphereMask = 0xCF;
			BodyContact(Sim);
		}

		bool bRisingSeen = false;

		struct FKeyframe { FAirContext Ctx; bool bRisingSeen = false; };
		virtual TSharedPtr<FRiderStateBlob> SaveState() const override { return SSXRiderBlob::Save(FKeyframe{ *Ctx, bRisingSeen }); }
		virtual void LoadState(const FRiderStateBlob& B) override
		{
			FKeyframe K;
			SSXRiderBlob::Load(B, K);
			const TSharedPtr<SSXAnim::FRiderAnimChannel> Keep = Ctx->Anim;
			*Ctx = K.Ctx;
			Ctx->Anim = Keep;
			bRisingSeen = K.bRisingSeen;
		}

	private:
		void Touchdown(FRiderSim& Sim, const FGroundProbeResult& R, const FVector& FoldShift)
		{
			FRiderState& St = Sim.State;
			FAirContext& X = *Ctx;
			if (St.Ch1Anim == SSXAnim::LandBobbleAnim && St.Frame < St.Ch1EndFrame && X.Anim)
			{
				X.Anim->FadeChannel1(Sim);
			}

			if (R.Surface != St.SurfaceId)
			{
				St.SurfaceChanged = int32(0x80000001u);
			}
			else if (St.SurfaceChanged >= 0)
			{
				St.SurfaceChanged = 0;
			}
			St.SurfaceId = R.Surface;
			St.ContactPoint = R.ContactPoint;
			St.GroundHeight = R.Height;
			St.GroundNormal = R.GroundNormal;
			St.ContactForward = R.ContactForward;
			St.ContactLeft = R.ContactLeft;
			St.FallLine = R.FallLine;
			St.ContactKind = R.ContactKind;
			Sim.LastProbe = R;
			if (St.GroundHeight < 0.0)
			{
				St.Position = R.ContactPoint - FoldShift;
				St.GroundHeight = 0.0;
			}
			const FVector N = St.GroundNormal;

			FLandingInput In;
			In.BasisQ = St.Orientation;
			In.PendingSpin = St.SpinAngle;
			In.PendingFlip = St.FlipAngle;
			In.Velocity = St.Velocity;
			In.Normal = N;
			In.KLand = LandScale(Sim.Profile);
			In.BoardClass = St.BoardClass;
			In.bRidingFakie = RidingFakie(Sim);
			In.Surface = St.SurfaceId;
			const SSXAnim::FAnimInstance& Trick = X.Anim->Trick;
			const int32 Cat = Trick.Category();
			In.bGrabActive = Cat == SSXAnim::CategoryGrab || Cat == SSXAnim::CategoryTweak || Cat == SSXAnim::CategoryUber;
			In.bGrabLandable = Trick.Flag(4);
			In.bGrabCategory = Cat == SSXAnim::CategoryGrab;
			const FLandingResult Res = EvaluateLanding(In);
			// 0x108E8C, 0x12BA78
			St.AddPadFeedback(double(float(Res.Vn) * -0.00072000007f));

			FAirLandingRecord& Rec = X.LastLanding;
			Rec = FAirLandingRecord();
			Rec.bValid = true;
			Rec.Frame = St.Frame;
			Rec.Verdict = Res.Verdict;
			Rec.Result = Res;
			Rec.Normal = N;
			Rec.VelocityBefore = St.Velocity;
			Rec.Surface = St.SurfaceId;
			Rec.AirFrames = X.AirFrames;
			Rec.AnimCategory = Cat;
			Rec.AnimFrame = Trick.Frame;

			FRiderImpact Impact;
			Impact.Point = St.ContactPoint;
			Impact.Direction = AirUnit(St.Velocity, St.BasisForward);
			Impact.Normal = N;
			Impact.Speed = Res.Vn;
			St.LastImpact = Impact;
			St.Prediction.FlightTime = 0.0;

			if (Res.Verdict == ELandingVerdict::WipeOut)
			{
				St.AddPadImpact(St.Velocity, N);   // 0x108DFC
				St.AddFxImpact(Impact.Point, St.Velocity, N);
				BailFromAir(Sim, X, Impact, TEXT("T60 landing rejected"));
				Rec.Reward = X.Score.LastReward;
				Rec.TotalSpinDeg = X.Score.TotalSpinDeg;
				Rec.TotalFlipDeg = X.Score.TotalFlipDeg;
				St.Highlight(19);
				return;
			}

			Sim.SetControl(C::Void, TEXT("T54"));
			if (St.SpinAngle != 0.0 || St.FlipAngle != 0.0)
			{
				St.Orientation = FoldTrick(St.Orientation, St.SpinAngle, St.FlipAngle);
				St.SpinAngle = St.FlipAngle = 0.0;
				Sim.UpdateBasis();
			}
			const FVector F = St.BasisForward;
			St.ContactForward = AirUnit(F - FVector::DotProduct(F, N) * N, St.ContactForward);
			St.ContactLeft = FVector::CrossProduct(N, St.ContactForward);
			if (Res.bStanceFlip)
			{
				St.bStanceGoofy = !St.bStanceGoofy;
				Sim.RotateBody(St.BasisUp, PI);
				St.ContactForward = -St.ContactForward;
				St.ContactLeft = -St.ContactLeft;
			}
			if (In.bGrabCategory && Res.Verdict == ELandingVerdict::LandNormal)
			{
				St.Crouch.Cur = St.Crouch.Target = 1.0;
				St.Brake.Reset();
				St.Steer.Reset();
			}
			St.SphereMask = -1;
			St.TrickId = NoTrick;
			St.TrickPhase = 0;
			St.TrickHoldFrames = 0;
			X.Anim->Trick.Stop();
			const bool bNormal = Res.Verdict == ELandingVerdict::LandNormal;
			if (!bNormal)
			{
				X.Anim->LandHardAnim = LandHardAnimation(Res, St.BoardClass, In.bRidingFakie);
				Rec.LandHardAnim = X.Anim->LandHardAnim;
				X.Anim->DrawVariant(X.Anim->LandHardAnim);
			}
			Sim.SetMotion(M::Ground, bNormal ? TEXT("T52") : TEXT("T53"));
			Sim.SetControl(bNormal ? C::LandNormal : C::LandHard, bNormal ? TEXT("T52") : TEXT("T53"));

			St.Velocity *= Res.SpeedFactor;
			X.Score.AirFrames = X.AirFrames;
			const int32 UbersBefore = X.Score.UberCount;   // 0x1569B0
			const double Reward = X.Score.Land(X.bTookOffFromRail, false, In.bRidingFakie != (Res.Psi > HALF_PI), St.FinishResult >= 0,
				St.GroundNormal.Z < 0.7);
			Sim.LogNote(FString::Printf(TEXT("landed '%s' (w %08x %08x%s): take-off switch %d, landing +0x1B4 %d, psi %.0f deg%s"),
				X.Score.LastRecord.bValid ? *X.Score.TrickText : TEXT(""), X.Score.LastRecord.W0, X.Score.LastRecord.W1,
				X.Score.LastRecord.bValid ? TEXT("") : TEXT(", not valid"), X.Score.bSegmentFakie ? 1 : 0, St.bStanceGoofy ? 1 : 0,
				FMath::RadiansToDegrees(Res.Psi), Res.bStanceFlip ? TEXT(", stance flipped (class 2 backwards)") : TEXT("")));
			BoostAdd(St, Reward, BoostGain(Sim.Profile));
			if (X.Score.UberCount > InfiniteBoostUbers)
			{
				St.bInfiniteBoost = true;
			}
			if (UbersBefore < X.Score.UberCount)
			{
				// 0x233448
				St.Highlight(16, 2.f);
				++St.UberLandCount;
				St.PositiveReactionFrame = St.Frame;
				St.PositiveReactionStrength = UberReactionStrength;
				if (St.bInfiniteBoost && UbersBefore <= InfiniteBoostUbers)
				{
					++St.InfiniteBoostCallCount;
				}
				St.UberHintIndex = (St.UberHintIndex + 1) % 4;
			}
			else if (LandReactionThreshold[FMath::Clamp(Sim.Profile.Level, 0, 9)] < Reward)
			{
				St.PositiveReactionFrame = St.Frame;
				St.PositiveReactionStrength = Reward;
			}
			St.CrowdCueX = Reward;   // 0x217D50
			St.bCrowdCueBail = false;
			++St.CrowdCueCount;
			Rec.Reward = Reward;
			Rec.Points = X.Score.LastPoints;
			Rec.Summary = X.Score.LastSummary;
			Rec.TotalSpinDeg = X.Score.TotalSpinDeg;
			Rec.TotalFlipDeg = X.Score.TotalFlipDeg;
			St.Highlight(19);
		}

		void BodyContact(FRiderSim& Sim)
		{
			FRiderState& St = Sim.State;
			const FQuat Rendered = GetRenderedOrientation(St);
			const FVector UpR = Rendered.RotateVector(FVector(0, 0, 1));
			FRiderSphereSet Shape;
			if (!SSXRider::BuildPostAISphereSet(Sim.Profile, St, SSXRider::MaskAir, Shape))
			{
				return;
			}
			FRiderWorldHit Hit;
			if (!Sim.GetWorld()->QueryWalls(Shape, true, nullptr, Hit) || Hit.T <= 0.0)
			{
				return;
			}
			const FVector N = Hit.Normal;
			St.Position += WallPushOut * Hit.T * N;
			St.SphereFrameP += WallPushOut * Hit.T * N;   // 0x1267A0
			Sim.NotifySphereSetMoved(WallPushOut * Hit.T * N);
			const double Vn = FVector::DotProduct(N, St.Velocity);
			if (Vn >= 0.0)
			{
				return;
			}
			const double F = WallAlignBase + WallAlignExtra * FMath::Min(1.0, -WallAlignK * Vn);
			if (FVector::DotProduct(N, UpR) > F)
			{
				const FVector Cr = FVector::CrossProduct(UpR, N);
				const double S = Cr.Size();
				if (S > 1e-5)
				{
					Sim.RotateBody(Cr / S, FMath::Min(FMath::Asin(FMath::Min(S, 1.0)), WallAlignRate));
				}
				St.Velocity -= Vn * N;
				return;
			}
			FRiderImpact Impact;
			Impact.Point = Hit.Point;
			Impact.Direction = AirUnit(St.Velocity, St.BasisForward);
			Impact.Normal = N;
			Impact.Speed = Vn;
			St.LastImpact = Impact;
			Ctx->LastLanding = FAirLandingRecord();
			Ctx->LastLanding.bValid = true;
			Ctx->LastLanding.bBodyContactBail = true;
			Ctx->LastLanding.Verdict = ELandingVerdict::WipeOut;
			Ctx->LastLanding.Frame = St.Frame;
			St.AddPadImpact(St.Velocity, N);   // 0x109278
			St.AddFxImpact(Impact.Point, St.Velocity, N);
			BailFromAir(Sim, *Ctx, Impact, TEXT("T60 air body contact"));
		}

		TSharedRef<FAirContext> Ctx;
	};

	class FNaturalAirControl final : public IRiderControlState
	{
	public:
		explicit FNaturalAirControl(const TSharedRef<FAirContext>& InCtx) : Ctx(InCtx) {}
		virtual TSharedPtr<FRiderStateBlob> SaveState() const override { return SSXRiderBlob::Save(EntrySteer); }
		virtual void LoadState(const FRiderStateBlob& B) override { SSXRiderBlob::Load(B, EntrySteer); }

		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override
		{
			// 0x1000B8
			FRiderState& St = Sim.State;
			St.Brake.Target = 0.0;
			St.Brake.Rate = NaturalAirReleaseRate;
			EntrySteer = St.Steer.Cur;
		}

		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override
		{
			// 0x1000E0, 0x15FDD0
			FRiderState& St = Sim.State;
			const bool bFallRecover = St.Ch0Anim == 0x224;
			St.bLandBobbleCall = true;   // 0x127D08, 0x127918
			if (bFallRecover)
			{
				St.Crouch.Cur = 1.0;
				St.Crouch.Rate = 0.0;
				St.Crouch.Target = 1.0;
				St.Ramp1F0.Cur = St.Ramp1F0.Rate = St.Ramp1F0.Target = 0.0;
				return;
			}
			SSXGround::SetCrouchTarget(Sim, In.Crouch);   // 0x126C00
			if (St.Brake.Cur != 0.0 || St.Steer.Cur != 0.0 || St.SteerFollow.Cur != 0.0 || St.PrewindA.Cur != 0.0 || St.PrewindB.Cur != 0.0)
			{
				// leftover ground filters: released toward 0 (never zeroed at once); no Spin this frame
				auto Release = [](FApproachFilter& F, double Rate) { F.Target = 0.0; F.Rate = Rate; };
				if (In.bTrickPressed)
				{
					Release(St.Steer, NaturalAirReleaseRate);
					Release(St.SteerFollow, NaturalAirReleaseRate);
					Release(St.PrewindA, NaturalAirReleaseRate);
					Release(St.PrewindB, NaturalAirReleaseRate);
				}
				else if (St.Steer.Target == 0.0)
				{
					Release(St.Steer, NaturalAirReleaseRate);
					Release(St.SteerFollow, NaturalAirReleaseRate);
					if (St.PrewindA.Cur != 0.0 && In.PrewindSpin == 0.0)
					{
						Release(St.PrewindA, NaturalAirReleaseRate);
					}
					if (St.PrewindB.Cur != 0.0 && In.PrewindFlip == 0.0)
					{
						Release(St.PrewindB, NaturalAirReleaseRate);
					}
				}
				else
				{
					// 0x126948
					SSXGround::SetSteerTarget(Sim, In.Turn);
					if (FMath::Abs(EntrySteer) <= FMath::Abs(St.Steer.Cur))
					{
						St.Steer.Cur = EntrySteer;
					}
					else
					{
						Release(St.Steer, NaturalAirReleaseRate);
					}
					if (St.Ch0Anim == 0x1FF && (SSXGround::ControlInputsMask() & 32))
					{
						St.SteerFollow.Target = St.Steer.Cur;
						St.SteerFollow.Rate = SteerFollowReleaseRate;
					}
					else
					{
						Release(St.SteerFollow, SteerFollowReleaseRate);
					}
				}
				return;
			}
			if (In.Turn != 0.0 || St.Crouch.Cur < 0.5 || In.bTrickPressed)
			{
				St.Ramp1F0.Cur = St.Ramp1F0.Target = 1.0 - St.Crouch.Cur;
				St.Ramp1F0.Rate = 0.0;
				Ctx->bSeedsPending = true;
				Ctx->PendingSeedSpin = Ctx->PendingSeedFlip = 0.0;
				Ctx->bPendingLate = false;
				Sim.SetControl(C::Spin, TEXT("T50"));
			}
		}

		static constexpr double NaturalAirReleaseRate = 4.0;
		static constexpr double SteerFollowReleaseRate = 2.0;
		double EntrySteer = 0.0;
		TSharedRef<FAirContext> Ctx;
	};

	class FSpinControl final : public IRiderControlState
	{
	public:
		explicit FSpinControl(const TSharedRef<FAirContext>& InCtx, const TSharedRef<FAirMotion>& InMotion)
			: Ctx(InCtx), Motion(InMotion) {}

		virtual void Enter(FRiderSim& Sim, ERiderControl Previous) override
		{
			FRiderState& St = Sim.State;
			FAirContext& X = *Ctx;
			const double G = SpinGain(Sim.Profile);
			const bool bFakie = RidingFakie(Sim);
			double SeedSpin = 0.0, SeedFlip = 0.0;
			bool bSpinCapable = false;
			if (X.bSeedsPending)
			{
				SeedSpin = X.PendingSeedSpin;
				SeedFlip = X.PendingSeedFlip;
				bSpinCapable = X.bPendingLate;
				if (X.bPendingLate)
				{
					X.bChannel0AirIdle = false;
				}
				else if (St.Prediction.FlightTime > 0.35)
				{
					// 0x127298
					const int32 Idle = St.GrindType == 3 ? 0x26E : St.GrindType == 4 ? 0x26F : -1;
					X.bChannel0AirIdle = Idle < 0;
					X.SpinAnim = FSpinAnimModel();
					if (Idle >= 0)
					{
						StartSpinAnim(Sim, X, Idle, 1.0, false);
					}
				}
				else
				{
					X.SpinAnim = FSpinAnimModel();
				}
			}
			else if (Previous == C::Jump || Previous == C::Prewind)
			{
				SeedSpin = SeedPrewind * G * St.PrewindA.Cur;
				SeedFlip = SeedPrewind * G * SeedPrewindFlipScale * St.PrewindB.Cur;
				if (St.BoardClass == 2)
				{
					SeedSpin *= 2.0 / 3.0;
				}
				// 0x126E30, 0x105A60
				if (St.bStanceGoofy)
				{
					SeedFlip = -SeedFlip;
				}
				// 0x126FC0
				bSpinCapable = St.PrewindA.Cur != 0.0 || St.PrewindB.Cur != 0.0;
				X.bChannel0AirIdle = !bSpinCapable;
				X.SpinAnim = FSpinAnimModel();
				if (!bSpinCapable)
				{
					if (St.GrindType == 3 || St.GrindType == 4)
					{
						StartSpinAnim(Sim, X, St.GrindType == 3 ? 0x26E : 0x26F, 1.0, true);
					}
					else
					{
						X.Anim->RequestPlay(SSXAnim::AirIdle);
						St.Ramp1F0.Cur = St.Ramp1F0.Target = 1.0 - St.Crouch.Cur;
						St.Ramp1F0.Rate = 0.0;
					}
				}
				else
				{
					// 0x1055F0
					const int32 Into = SSXAnim::IntoFromTakeoff(SSXAnim::TakeoffClip(
						SSXAnim::TakeoffRow(St.PrewindA.Cur, St.PrewindB.Cur, St.bStanceGoofy), St.GrindType));
					if (Into >= 0)
					{
						StartSpinAnim(Sim, X, Into, 1.0, true);
					}
				}
				St.PrewindA.Reset();
				St.PrewindB.Reset();
			}
			if (St.LaunchBoostTimer > 0.0)
			{
				SeedSpin *= LaunchBoostMult;
				SeedFlip *= LaunchBoostMult;
			}
			if (bFakie)
			{
				SeedSpin *= FakieMult;
				SeedFlip *= FakieMult;
			}
			FSpinControlState S;
			S.bLateMode = X.bSeedsPending && X.bPendingLate;
			EnvelopeFromSeed(SeedSpin, S.SpinHi, S.SpinLo, S.SpinFloor);
			EnvelopeFromSeed(SeedFlip, S.FlipHi, S.FlipLo, S.FlipFloor);
			S.SpinRate = SeedSpin;
			S.FlipRate = SeedFlip;
			S.Phase = bSpinCapable ? 0 : 2;
			S.CapSpin = S.CapFlip = 0.0;   // 0x100598
			St.Steer.Target = 0.0;
			St.Steer.Rate = 4.0;
			St.SteerFollow.Reset();
			// 6: auto-align toward the predicted landing frame
			AutoAlignSolve(St.Orientation, St.Prediction.LandNormal, St.BoardClass, St.GrindType, S.AlignAngle, S.AlignAxis);
			if (St.Prediction.LandSurface == 6 || St.Prediction.LandSurface == 10)
			{
				S.AlignAngle = 0.0;
			}
			St.AirTurnOffset = St.AirTiltOffset = 0.0;
			X.Spin = S;
			X.bSeedsPending = false;
			X.bPendingLate = false;
			X.PendingSeedSpin = X.PendingSeedFlip = 0.0;
		}

		virtual void Exit(FRiderSim& Sim, ERiderControl Next) override
		{
			FRiderState& St = Sim.State;
			if (St.SpinAngle == 0.0 && St.FlipAngle == 0.0)
			{
				return;
			}
			const FQuat QOld = St.Orientation;
			St.Orientation = FoldTrick(St.Orientation, St.SpinAngle, St.FlipAngle);
			St.Position += PivotFoldShift(QOld, St.Orientation, St.PoseRoot.Z);
			St.SpinAngle = St.FlipAngle = 0.0;
			Sim.UpdateBasis();
		}

		virtual void Update(FRiderSim& Sim, const FRiderInputWord& In) override
		{
			FRiderState& St = Sim.State;
			FAirContext& X = *Ctx;
			FSpinControlState& S = X.Spin;
			const double Dt = Sim.MotionDt();
			const double G = SpinGain(Sim.Profile);
			StepSpinAnim(Sim, X);

			// 0x100908, 0x250C10, 0x102E80, 0x129008
			const float DtF = float(St.TimeScale) * 0.016666668f;
			const float TLand = float(St.Prediction.TimeLeft);
			const float Angle = float(S.AlignAngle);
			if (0.0f < TLand && Angle != 0.0f)
			{
				const float Lo = TLand * -1.3962634f, Hi = TLand * 1.3962634f;
				const float Cl = Angle < Lo ? Lo : (Hi < Angle ? Hi : Angle);
				const float Delta = Cl * (TLand < DtF ? DtF / DtF : DtF / TLand);
				S.AlignAngle = SSXEE::FpuSub(Angle, Delta);
				float Sn, Cs;
				SSXAnim::SinCosEE(Delta * 0.5f, Sn, Cs);
				const FVector3f Ax = SSXEE::F(S.AlignAxis);
				const float Bw = Cs, Bx = Sn * Ax.X, By = Sn * Ax.Y, Bz = Sn * Ax.Z;
				const float Aw = float(St.Orientation.W), Axq = float(St.Orientation.X), Ay = float(St.Orientation.Y), Az = float(St.Orientation.Z);
				using SSXEE::FpuAdd; using SSXEE::FpuSub;
				const float Qw = FpuSub(FpuSub(FpuSub(Aw * Bw, Axq * Bx), Ay * By), Az * Bz);
				const float Qx = FpuSub(FpuAdd(FpuAdd(Aw * Bx, Axq * Bw), Ay * Bz), Az * By);
				const float Qy = FpuSub(FpuAdd(FpuAdd(Aw * By, Ay * Bw), Az * Bx), Axq * Bz);
				const float Qz = FpuSub(FpuAdd(FpuAdd(Aw * Bz, Az * Bw), Axq * By), Ay * Bx);
				St.Orientation = FQuat(Qx, Qy, Qz, Qw);
				Sim.UpdateBasis();
			}

			// integrate the trick angles and report degrees to scoring: angle + rate x dt (add.s)
			St.SpinAngle = SSXEE::FpuAdd(float(St.SpinAngle), float(S.SpinRate) * DtF);
			St.FlipAngle = SSXEE::FpuAdd(float(St.FlipAngle), float(S.FlipRate) * DtF);
			const double SpinDeg = FMath::RadiansToDegrees(Sim.Profile.Stance != 0 ? -St.SpinAngle : St.SpinAngle);
			X.Score.UpdateAngles(SpinDeg, FMath::RadiansToDegrees(St.FlipAngle));
			X.Score.TickHold();   // 0x156E80

			const int32 SIn = S.bLateMode ? In.LateSpin : In.Spin;
			const int32 FIn = S.bLateMode ? In.LateFlip : In.Flip;
			if ((SIn == 0 && FIn == 0) || S.Phase == 2)
			{
				if (S.Phase == 2)
				{
					// 0x15FE70
					if (SSXAnim::IsHalfSpinPlayOut(X.SpinAnim.Id))
					{
						const double P = X.SpinAnim.NFrames > 1 ? X.SpinAnim.Frame / double(X.SpinAnim.NFrames - 1) : 1.0;
						double Dp = (P - S.PlayOutProgress) * PI;
						S.PlayOutProgress = P;
						if (St.bStanceGoofy) { Dp = -Dp; }
						if (X.SpinAnim.Id == 0x23D) { Dp = -Dp; }
						St.SpinAngle += Dp;
						S.SettleOffset -= Dp;
					}
				}
				else
				{
					// 0x102800
					const int32 PlayOut = SSXAnim::SpinPlayOutId(X.SpinAnim.Id);
					if (PlayOut >= 0)
					{
						StartSpinAnim(Sim, X, PlayOut, G, true);
					}
					S.PlayOutProgress = 0.0;
					S.SettleOffset = 0.0;
					if (SSXAnim::IsHalfSpinPlayOut(X.SpinAnim.Id))
					{
						S.SettleOffset = St.bStanceGoofy ? -PI : PI;
						if (X.SpinAnim.Id == 0x23D) { S.SettleOffset = -S.SettleOffset; }
					}
					S.Phase = 2;
					S.bAirTurnMode = false;
					St.Ramp1F0.Reset();
					X.bChannel0AirIdle = false;
					if (S.SpinHi < 0.0) { S.SpinHi = 0.0; } else { S.SpinLo = 0.0; }
					S.SpinFloor = 0.0;
					if (S.FlipHi < 0.0) { S.FlipHi = 0.0; } else { S.FlipLo = 0.0; }
					S.FlipFloor = 0.0;
					S.CapSpin = S.CapFlip = 2.0 * PI;
				}
				const double A = WrapPi(St.SpinAngle + S.SettleOffset);
				const double B = FMath::Abs(WrapPi(St.SpinAngle + S.SettleOffset - St.AirTurnOffset * AirTurnRange));
				double Es;
				if (B < BandNear)        { Es = -A; }
				else if (B > BandFar)    { Es = -WrapPi(A - PI); }
				else if (S.SpinHi > 0.0) { Es = A < 0.0 ? A + PI : A; }
				else                     { Es = A > 0.0 ? A - PI : A; }
				const double Cf = WrapPi(St.FlipAngle);
				const double D = FMath::Abs(WrapPi(St.FlipAngle - St.AirTiltOffset * AirTurnRange));
				double Ef;
				if (D < BandNear)          { Ef = -Cf; }
				else if (S.FlipHi <= 0.0)  { Ef = Cf > 0.0 ? Cf - 2.0 * PI : Cf; }
				else                       { Ef = Cf < 0.0 ? Cf + 2.0 * PI : Cf; }
				S.CapSpin = FMath::Min(S.CapSpin, FMath::Abs(Es));
				S.CapFlip = FMath::Min(S.CapFlip, FMath::Abs(Ef));
				const double Gain = G * SettleGain * Dt;
				S.SpinRate = Gain * FMath::Clamp(Es, -S.CapSpin, S.CapSpin);
				S.FlipRate = Gain * FMath::Clamp(Ef, -S.CapFlip, S.CapFlip);
			}
			else
			{
				const double Step = Slew * Dt;
				const double TgtS = SIn * FMath::Abs(S.SpinLo + S.SpinHi);
				const double TgtF = -FIn * FMath::Abs(S.FlipLo + S.FlipHi);
				S.SpinRate = FMath::Clamp(TgtS, S.SpinRate - Step, S.SpinRate + Step);
				S.FlipRate = FMath::Clamp(TgtF, S.FlipRate - Step, S.FlipRate + Step);
			}

			const double K = SpinDecay(Sim.Profile);
			EnvelopeStep(K, S.SpinHi, S.SpinLo, S.SpinFloor, S.SpinRate);
			EnvelopeStep(K, S.FlipHi, S.FlipLo, S.FlipFloor, S.FlipRate);

			// 0x1272E8, 0x11CAE8
			bool bBlocked = false;
			if (St.TrickPhase == 3)
			{
				bBlocked = St.Velocity.Z <= 0.0;
				if (!bBlocked)
				{
					St.TrickPhase = 0;
				}
			}
			if (!bBlocked)
			{
				GrabUpdate(Sim, X, In, false);
			}
			SSXAnim::FAnimInstance& Trick = X.Anim->Trick;
			const int32 TrickCat = Trick.Category();
			const bool bGrabbing = !bBlocked && (St.TrickId != NoTrick || (Trick.IsActive()
				&& (TrickCat == SSXAnim::CategoryGrab || TrickCat == SSXAnim::CategoryTweak || TrickCat == SSXAnim::CategoryUber)));
			if (bGrabbing)
			{
				X.bChannel0AirIdle = false;
			}
			Trick.Advance();
			if (Trick.IsActive() && Trick.bEnded && Trick.Hold < 0.0)
			{
				// 0x15A030
				Trick.Stop();
				X.bChannel0AirIdle = true;
			}
			int32& Auto180Left = X.Anim->Auto180FramesLeft;
			if (Trick.IsActive())
			{
				Auto180Left = 0;
			}
			else if (Auto180Left > 0)
			{
				if (--Auto180Left == 0)
				{
					X.bChannel0AirIdle = true;
				}
			}

			if (S.Phase == 0)
			{
				if (bGrabbing || SSXAnim::IsSpinCycle(X.SpinAnim.Id))
				{
					S.Phase = 1;
				}
			}
			else if (bGrabbing)
			{
				St.Ramp1F0.Target = 0.0;
				St.Ramp1F0.Rate = RampGrabRate;
				if (S.Phase == 1)
				{
					St.AirTurnOffset = St.AirTiltOffset = 0.0;
				}
				else
				{
					S.bAirTurnMode = false;
				}
			}
			if (S.Phase == 2 && !bGrabbing && !SSXAnim::IsHalfSpinPlayOut(X.SpinAnim.Id))
			{
				const bool bSlow = FMath::Abs(S.SpinRate) < LateRateMax && FMath::Abs(S.FlipRate) < LateRateMax;
				if (Auto180Left == 0 && bSlow)
				{
					// 0x102158
					int32 Ls = In.LateSpin, Lf = In.LateFlip;
					if (Ls != 0 || Lf != 0)
					{
						const double Theta = FMath::Atan2(double(FMath::Abs(Lf)), double(FMath::Abs(Ls)));
						if (Theta > LateAxisFlipOnly)
						{
							Ls = 0;
						}
						else if (Theta < LateAxisSpinOnly)
						{
							Lf = 0;
						}
						// 0x156568
						BoostAdd(St, X.Score.LateTrick(St.bStanceGoofy), BoostGain(Sim.Profile));
						X.bSeedsPending = true;
						X.bPendingLate = true;
						X.PendingSeedSpin = G * SeedLate * Ls;
						X.PendingSeedFlip = -G * SeedLate * Lf;
						StartSpinAnim(Sim, X, SSXAnim::DirectionalTrickId(Ls, St.bStanceGoofy ? -Lf : Lf, St.bStanceGoofy), 1.0, true);
						Sim.SetControl(C::Void, TEXT("T51"));
						Sim.SetControl(C::Spin, TEXT("T51"));
						return;
					}
					if (!S.bAirTurnMode && St.Ramp1F0.Cur == 0.0 && St.BoardClass != 2 && Auto180Wanted(St))
					{
						Auto180(Sim, X);
						return;
					}
					if (!S.bAirTurnMode)
					{
						St.bLandBobbleCall = true;
					}
					if (X.bChannel0AirIdle && !S.bAirTurnMode)
					{
						if (St.Velocity.Z > 0.0)
						{
							St.Ramp1F0.Target = 0.0;
							St.Ramp1F0.Rate = RampRiseRate;
						}
						else
						{
							const double T = St.Prediction.TimeLeft - RampLandLead;
							St.Ramp1F0.Rate = RampGrabRate;
							St.Ramp1F0.Target = T > 0.0 ? 1.0 - T / (T + FMath::Abs(St.Prediction.ApexLeft)) : 1.0;
						}
					}
				}
				if (S.bAirTurnMode)
				{
					St.bLandBobbleCall = true;
					const double R = AirTurnRate(Sim.Profile) * Dt;
					const int32 Turn = Dir2(In.AirTurn), Tilt = Dir2(In.AirTilt);
					if (Turn != 0)
					{
						const double Old = St.AirTurnOffset;
						St.AirTurnOffset = FMath::Clamp(double(Turn), Old - R, Old + R);
						St.SpinAngle += (St.AirTurnOffset - Old) * AirTurnRange;
					}
					if (Tilt != 0)
					{
						const int32 T = S.bAirTiltInvert ? Tilt : -Tilt;
						const double Old = St.AirTiltOffset;
						St.AirTiltOffset = FMath::Clamp(double(T), Old - R, Old + R);
						St.FlipAngle += (St.AirTiltOffset - Old) * AirTurnRange;
					}
				}
				else if (Auto180Left == 0 && (Dir2(In.AirTurn) != 0 || Dir2(In.AirTilt) != 0))
				{
					S.bAirTurnMode = true;
					X.Anim->RequestPlay(0x222);
					St.bLandBobbleCall = true;
					X.bChannel0AirIdle = false;
					X.SpinAnim = FSpinAnimModel();
					St.AirTurnOffset = St.AirTiltOffset = 0.0;
					S.bAirTiltInvert = (bSlow && FMath::Abs(WrapPi(St.SpinAngle)) > HALF_PI) != S.bAuto180Toggle;
				}
			}
		}

		static void StartSpinAnim(FRiderSim& Sim, FAirContext& X, int32 Id, double Speed, bool bRequest)
		{
			FSpinAnimModel& A = X.SpinAnim;
			A = FSpinAnimModel();
			A.NFrames = X.Anim->Frames(Sim, Id);
			if (bRequest)
			{
				X.Anim->RequestPlay(Id, Speed == 1.0 ? -1.0 : Speed);
			}
			X.bChannel0AirIdle = false;
			if (A.NFrames <= 0)
			{
				return;   // 0x15FCF8
			}
			A.Id = Id;
			A.Speed = Speed;
			A.StartFrame = Sim.State.Frame;
		}

		static void StepSpinAnim(const FRiderSim& Sim, FAirContext& X)
		{
			FSpinAnimModel& A = X.SpinAnim;
			if (A.Id < 0 || A.StartFrame == Sim.State.Frame || SSXAnim::IsSpinCycle(A.Id))
			{
				return;
			}
			const double Last = double(A.NFrames - 1);
			const double Old = A.Frame;
			const double New = SSXAnim::ClockStep(Old, A.Speed);
			A.Frame = FMath::Min(New, Last);
			if (!(Last < New && Last <= Old))
			{
				return;
			}
			if (SSXAnim::IsSpinInto(A.Id))
			{
				A.Id = SSXAnim::TrickPhaseChain(A.Id);
				A.Frame = 0.0;
				A.Speed = 1.0;
			}
			else
			{
				A = FSpinAnimModel();
				X.bChannel0AirIdle = true;
			}
		}

		static void OnTrickStarted(FAirContext& X)
		{
			++X.Anim->TrickStarts;
			X.SpinAnim = FSpinAnimModel();
			X.bChannel0AirIdle = false;
		}

		static constexpr double RampGrabRate = 3.3333333;
		static constexpr double RampRiseRate = 2.1666667;
		static constexpr double RampLandLead = 0.25;

		static bool Auto180Wanted(const FRiderState& St)
		{
			const FVector Y = GetRenderedOrientation(St).RotateVector(FVector(0, 1, 0));
			const FVector NLand = St.Prediction.LandNormal;
			const FVector Yp = AirUnit(Y - FVector::DotProduct(Y, NLand) * NLand, FVector::ZeroVector);
			return FVector::DotProduct(Yp, St.Prediction.LandVelocity) < 0.0 && FVector::DotProduct(Y, St.Velocity) < Auto180MaxSpeed;
		}

		static void Auto180(FRiderSim& Sim, FAirContext& X)
		{
			FRiderState& St = Sim.State;
			FSpinControlState& S = X.Spin;
			const double Sp = St.SpinAngle, Fl = St.FlipAngle;
			const FVector Zb(0, 0, 1), Xb(1, 0, 0);
			const FQuat Qt = AirAxisAngle(Zb, -Sp) * AirAxisAngle(Xb, -Fl) * AirAxisAngle(Zb, PI) * AirAxisAngle(Xb, Fl) * AirAxisAngle(Zb, Sp);
			FVector PivotOld = FVector::ZeroVector, PivotNew = FVector::ZeroVector;
			const bool bPivot = SSXRider::BodyPivot(Sim.Profile, St.bStanceGoofy, PivotOld);
			const FVector PivotWorldOld = GetRenderedOrientation(St).RotateVector(PivotOld);
			St.Orientation = (St.Orientation * Qt).GetNormalized();
			Sim.UpdateBasis();
			St.bStanceGoofy = !St.bStanceGoofy;
			Sim.LogNote(FString::Printf(TEXT("auto-180 in the air (0x100908): +0x1B4 -> %d"), St.bStanceGoofy ? 1 : 0));
			if (bPivot && SSXRider::BodyPivot(Sim.Profile, St.bStanceGoofy, PivotNew))
			{
				St.Position += PivotWorldOld - GetRenderedOrientation(St).RotateVector(PivotNew);
			}
			// 0x15EEF8
			X.Anim->LayerYawAdd += PI;
			X.Anim->Auto180FramesLeft = SSXAnim::ControlFrames(X.Anim->Frames(Sim, SSXAnim::AirAuto180));
			X.bChannel0AirIdle = false;
			X.SpinAnim = FSpinAnimModel();
			S.bAuto180Toggle = !S.bAuto180Toggle;
			++S.Auto180Count;
			if (S.AlignAngle != 0.0)
			{
				// 0x11DB00
				AutoAlignSolve(St.Orientation, St.Prediction.LandNormal, St.BoardClass, St.GrindType, S.AlignAngle, S.AlignAxis);
				if (St.Prediction.LandSurface == 6 || St.Prediction.LandSurface == 10)
				{
					S.AlignAngle = 0.0;
				}
			}
		}

		static constexpr double Auto180MaxSpeed = 277.78;

		static void GrabUpdate(FRiderSim& Sim, FAirContext& X, const FRiderInputWord& In, bool bBlocked)
		{
			using namespace SSXAnim;
			FRiderState& St = Sim.State;
			FAnimInstance& A = X.Anim->Trick;
			const int32 Cat = A.Category();
			const double GSpeed = GrabAnimSpeed(Sim.Profile);
			const FAnimTable& Table = X.Anim->GetTable();
			const int32 InGrab = In.TrickId;
			if (A.IsActive())
			{
				const bool bNoGrab = St.TrickId == NoTrick;
				if (Cat == CategoryGrab && A.Flag(2) && !X.bTrickRegistered)
				{
					X.Score.GrabReachesHold(A.Data.AnimId);
					X.bTrickRegistered = true;
				}
				if ((Cat == CategoryGrab || Cat == CategoryTweak) && A.Edge(2) && bNoGrab)
				{
					X.Score.FinishTrick(A.Data.AnimId, 0.0);
				}
				else if (Cat == CategoryUber && A.Edge(1))
				{
					X.Score.FinishTrick(A.Data.AnimId, A.Data.NFrames / (AnimFps * A.Speed));   // 0x15FEC8
				}
			}
			if (St.TrickId == NoTrick)
			{
				if (InGrab == NoTrick || bBlocked)
				{
					return;
				}
				bool bFree = true;
				if (Cat == CategoryGrab || Cat == CategoryTweak)
				{
					bFree = A.Flag(3) || !A.Flag(1);
				}
				else if (Cat == CategoryUber)
				{
					bFree = A.Flag(3);
				}
				if (!bFree)
				{
					return;
				}
				X.Anim->FadeChannel1(Sim);   // 0x15F050
				const FTrickRow Row = Table.TrickRow(St.BoardClass, Sim.Profile.GetTrickCharacter(), InGrab);
				if (Row.Grab.IsValid())
				{
					A.Start(Row.Grab, GSpeed, Row.Grab.Markers[1]);
					OnTrickStarted(X);
					X.bTrickRegistered = false;
				}
				St.TrickId = InGrab;
				St.TrickPhase = 0;
				St.TrickHoldFrames = 0;
				return;
			}
			if (A.IsActive() && A.Flag(2))
			{
				++St.TrickHoldFrames;
			}
			if (St.TrickPhase == 4 && Cat == CategoryUber && !A.Flag(3))
			{
				return;
			}
			const bool bTweakNow = In.bTweak;
			if (InGrab == St.TrickId && bTweakNow == (St.TrickPhase != 0))
			{
				return;
			}
			if (InGrab == St.TrickId && St.TrickPhase == 0)
			{
				if (Cat == CategoryGrab && A.Flag(2))
				{
					const FTrickRow Row = Table.TrickRow(St.BoardClass, Sim.Profile.GetTrickCharacter(), St.TrickId);
					const bool bHasUber = Row.Uber.IsValid() && (!X.Config.HasUber || X.Config.HasUber(Sim, St.TrickId));
					if (St.TrickyTimer > 0.0 && bHasUber)
					{
						A.Start(Row.Uber, 1.0, -1.0);
						OnTrickStarted(X);
						St.TrickPhase = 4;
						X.Score.TrickUpgraded(Row.Uber.AnimId);    // 0x156050
					}
					else if (Row.Tweak.IsValid())
					{
						A.Start(Row.Tweak, GSpeed, Row.Tweak.Markers[1]);
						OnTrickStarted(X);
						St.TrickPhase = 1;
						X.Score.TrickUpgraded(Row.Tweak.AnimId);
						++St.TweakStartCount;   // 0x218B78
					}
				}
				return;
			}
			if (Cat == CategoryTweak)
			{
				A.Hold = -1.0;
			}
			else if (Cat == CategoryGrab)
			{
				if (!A.Flag(2) && !A.Flag(1))
				{
					A.Stop();
					X.bChannel0AirIdle = true;
					X.Anim->RequestPlay(SSXAnim::AirIdle);
				}
				else if (!A.Flag(2) && !X.bTrickRegistered)
				{
					// 0x155EE8
					X.Score.GrabReachesHold(A.Data.AnimId);
					X.bTrickRegistered = true;
				}
				if (A.IsActive())
				{
					A.Hold = -1.0;
				}
			}
			St.TrickId = NoTrick;
			St.TrickPhase = 0;
			St.TrickHoldFrames = 0;
			X.Anim->FadeChannel1(Sim);
		}

		TSharedRef<FAirContext> Ctx;
		TSharedRef<FAirMotion> Motion;
	};
}

FQuat GetRenderedOrientation(const FRiderState& State)
{
	return FoldTrick(State.Orientation, State.SpinAngle, State.FlipAngle);
}

FAirContext* GetAirContext(const FRiderSim& Sim)
{
	const TWeakPtr<FAirContext>* Found = Registry().Find(&Sim);
	FAirContext* Ctx = Found ? Found->Pin().Get() : nullptr;
	if (!Ctx && Sim.GetRegistrationSim())
	{
		Found = Registry().Find(Sim.GetRegistrationSim());
		Ctx = Found ? Found->Pin().Get() : nullptr;
	}
	return Ctx;
}
}

void RegisterAirStates(FRiderSim& Sim)
{
	using namespace SSXAir;
	const TSharedRef<FAirContext> Ctx = MakeShared<FAirContext>();
	Ctx->Anim = SSXAnim::AcquireChannel(Sim);
	{
		const TWeakPtr<FAirContext> Weak = Ctx;
		Ctx->Anim->CrashScoring = [Weak](bool& bPending) -> double
		{
			const TSharedPtr<FAirContext> C = Weak.Pin();
			bPending = C && C->Score.Value > 0.0;
			return C ? C->Score.Bail() : BailMeter;
		};
	}
	const TSharedRef<FAirMotion> Motion = MakeShared<FAirMotion>(Ctx);
	Sim.SetMotionState(ERiderMotion::Air, Motion);
	Sim.SetControlState(ERiderControl::NaturalAir, MakeShared<FNaturalAirControl>(Ctx));
	Sim.SetControlState(ERiderControl::Spin, MakeShared<FSpinControl>(Ctx, Motion));
	for (auto It = Registry().CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	Registry().Add(&Sim, Ctx);
}
