// 0x2361B0, 0x138B60
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/SSXReplayFile.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderPlaceholderStates.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/WipeOut/TumbleBody.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* const GRiderIds[] = { TEXT("eddie"), TEXT("kaori"), TEXT("luther"), TEXT("mac"), TEXT("moby"), TEXT("zoe"), TEXT("jp"),
	                                   TEXT("elise"), TEXT("psymon"), TEXT("seeiah"), TEXT("brodi"), TEXT("marisol") };

	struct FRow
	{
		int32 Frame = 0, Motion = 0, Kind = 3, Anim = 0, Char = 0, Stance = 0, BoardClass = 0;
		bool bHuman = false;
		FVector P, V, M, N, Path, Look;
		FQuat Q;
		FLandingPrediction Pred;
		double Stuck = 0.0, TimeScale = 1.0, P7 = 0.5, Scale = 1.0;
		bool bAnimMirror = false;
		double AnimYaw = 0.0;
		bool bAnimInputs = false;
		double Crouch = 0.0, Brake = 0.0, Steer = 0.0, SteerFollow = 0.0, AirTurnS = 0.0, AirTiltS = 0.0, Ramp = 0.0;
		int32 Grind = 0, Goofy = 0;
		TArray<uint8> Rec;   // cBoarder +0x10..+0x460 (column "rec"), empty in old reference files
	};

	bool ParseRow(const TArray<FString>& C, FRow& R)
	{
		if (C.Num() < 66) { return false; }
		auto D = [&C](int32 I) { return FCString::Atod(*C[I]); };
		auto I = [&C](int32 K) { return FCString::Atoi(*C[K]); };
		R.Frame = I(1);
		R.P = FVector(D(16), D(17), D(18));
		R.V = FVector(D(19), D(20), D(21));
		R.Q = FQuat(D(23), D(24), D(25), D(22));
		R.M = FVector(D(26), D(27), D(28));
		R.Stuck = D(29);
		R.N = FVector(D(30), D(31), D(32));
		R.Path = FVector(D(34), D(35), D(36));
		R.Kind = I(37);
		R.Anim = I(38);
		R.Motion = I(39);
		R.TimeScale = D(40);
		R.Look = FVector(D(41), D(42), D(43));
		R.P7 = D(44);
		R.Pred.TimeLeft = D(47);
		R.Pred.LandPoint = FVector(D(48), D(49), D(50));
		R.Pred.LandVelocity = FVector(D(51), D(52), D(53));
		R.Pred.LandNormal = FVector(D(54), D(55), D(56));
		R.Pred.LandSurface = I(57);
		R.Pred.FlightTime = D(58);
		R.Pred.ApexTime = D(59);
		R.Pred.AirElapsed = D(60);
		R.Char = I(61);
		R.Stance = I(62);
		R.BoardClass = I(63);
		R.Scale = D(64);
		R.bHuman = I(65) != 0;
		if (C.Num() >= 76 && !C[66].IsEmpty())
		{
			R.bAnimMirror = I(66) != 0;
			R.AnimYaw = D(67);
			R.bAnimInputs = true;
			R.Crouch = D(68); R.Brake = D(69); R.Steer = D(70); R.SteerFollow = D(71);
			R.AirTurnS = D(72); R.AirTiltS = D(73); R.Ramp = D(74); R.Grind = I(75);
			R.Goofy = I(46);
		}
		if (C.Num() >= 77 && !C[76].IsEmpty())
		{
			R.Rec.SetNumUninitialized(C[76].Len() / 2);
			if (HexToBytes(C[76], R.Rec.GetData()) != R.Rec.Num() || R.Rec.Num() < 0x450) { R.Rec.Reset(); }
		}
		return true;
	}

	FVector SpinOf(const FQuat& Qa, const FQuat& Qb)
	{
		FQuat D = Qb * Qa.Inverse();
		if (D.W < 0.0) { D = FQuat(-D.X, -D.Y, -D.Z, -D.W); }
		FVector Axis; double Angle;
		D.ToAxisAndAngle(Axis, Angle);
		return Axis * (Angle * 60.0);
	}

	// 0x129500, 0x129628: the pose-root inputs of a traced sample (the live rider's lean, up, +0x308, contact left,
	// suspension, chatter, spin / flip and the input filters); the rows give p / v / q, these come from the record.
	void CopyPoseInputs(const FRiderState& L, FRiderState& St)
	{
		St.Ramp1F0 = L.Ramp1F0; St.Brake = L.Brake; St.Crouch = L.Crouch; St.Steer = L.Steer; St.SteerFollow = L.SteerFollow;
		St.PrewindA = L.PrewindA; St.PrewindB = L.PrewindB; St.GateLaunch = L.GateLaunch; St.Lean = L.Lean;
		St.AirTurnSmoothed = L.AirTurnSmoothed; St.AirTiltSmoothed = L.AirTiltSmoothed;
		St.SmoothedUp = L.SmoothedUp; St.SurfaceP10 = L.SurfaceP10; St.ContactLeft = L.ContactLeft; St.ContactForward = L.ContactForward;
		St.SuspensionOffset = L.SuspensionOffset; St.BoardChatter = L.BoardChatter; St.SpinAngle = L.SpinAngle; St.FlipAngle = L.FlipAngle;
	}

	bool SeedPoseInputs(const FRow& R, FRiderState& St)
	{
		if (R.Rec.Num() < 0x450)
		{
			return false;
		}
		FRiderState L = St;
		SSXReplayFile::RestoreBoarderRecord(R.Rec.GetData(), L);
		CopyPoseInputs(L, St);
		return true;
	}

	double AngleDeg(const FQuat& A, const FQuat& B) { return FMath::RadiansToDegrees(A.AngularDistance(B)); }

	struct FLiveSpan
	{
		int32 Entry = -1;
		int32 SlideAt = -1;
		int32 RecoverAt = -1;
		int32 End = -1;
		int32 SlideAnim = 0, EndMotion = 0;
	};

	FLiveSpan SpanOf(const TArray<FRow>& Rows)
	{
		FLiveSpan S;
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			const FRow& R = Rows[I];
			if (S.Entry < 0 && R.Motion == 5) { S.Entry = I; }
			if (S.Entry >= 0 && S.SlideAt < 0 && R.Motion == 5 && (R.Anim == 0x2DC || R.Anim == 0x2DD)) { S.SlideAt = I; S.SlideAnim = R.Anim; }
			if (S.Entry >= 0 && S.RecoverAt < 0 && R.Motion == 6) { S.RecoverAt = I; }
			if (S.Entry >= 0 && S.End < 0 && R.Motion != 5 && R.Motion != 6) { S.End = I; S.EndMotion = R.Motion; }
		}
		return S;
	}

	struct FRuleResult
	{
		bool bEntryAnim = false;
		int32 SettlePred = -1, SettlePredAnim = 0, SettleLive = -1, SettleLiveAnim = 0;
		int32 GetupPred = -1, RecoverFrame = -1;
		int32 KindOk = 0, KindN = 0;
	};

	FRuleResult RuleChecks(const TArray<FRow>& Rows, const FLiveSpan& Sp)
	{
		constexpr double Dt = 1.0 / 60.0;
		FRuleResult Res;
		if (Sp.Entry < 1 || Sp.RecoverAt < 0) { return Res; }
		Res.RecoverFrame = Rows[Sp.RecoverAt].Frame;
		auto Held = [&Rows](int32 F) { return Rows[F].P == Rows[F - 1].P; };
		auto Updates = [&](int32 F, int32& Nu, bool& bContact)
		{
			const double A = Rows[F - 1].M.Size(), B = Rows[F].M.Size();
			const double S0 = Rows[F - 1].Stuck, S1 = Rows[F].Stuck;
			bContact = false;
			Nu = 0;
			if (Held(F) && S0 > 0.0 && S0 == S1 && FMath::Abs(A - B) < 1e-6) { return; }
			if (B > A + 1e-4 || FMath::Abs(B - 0.98) < 2e-3)
			{
				bContact = true;
				Nu = FMath::Max(0, (int32)FMath::RoundHalfToEven(FMath::Loge(B) / FMath::Loge(0.98)));
				return;
			}
			Nu = A > 0.0 && B > 0.0 ? FMath::Max(0, (int32)FMath::RoundHalfToEven(FMath::Loge(B / A) / FMath::Loge(0.98))) : 0;
		};
		const int32 E = Sp.Entry;
		const int32 Fa = (Rows[E].Anim >= 0x2E1 && Rows[E].Anim <= 0x2ED) ? E : E + 1;
		{
			const FRow& R = Rows[Fa];
			const FVector N = R.N;
			Res.bEntryAnim = SSXAnim::TumbleId(FVector::DotProduct(R.Q.RotateVector(FVector(0, 0, 1)), N), FVector::DotProduct(R.Q.RotateVector(FVector(1, 0, 0)), N),
				FVector::DotProduct(R.Q.RotateVector(FVector(0, 1, 0)), N), 0) == R.Anim;
		}
		double Phase = 0.0, Air = 0.0;
		if (FVector::DotProduct(Rows[E].M, Rows[E].N) > 0.0)
		{
			Phase = Dt;
		}
		else
		{
			const int32 Dec = (int32)FMath::RoundHalfToEven(FMath::Loge(FMath::Max(Rows[E].M.Size(), 1e-6)) / FMath::Loge(0.98));
			Air = FMath::Max(0, Dec - 1) * Dt;
		}
		bool bSlide = false;
		double SlidePhase = 0.0;
		for (int32 F = E + 1; F <= Sp.RecoverAt; ++F)
		{
			int32 Nu;
			bool bContact;
			Updates(F, Nu, bContact);
			const FRow& R = Rows[F];
			const FVector Fwd = R.Q.RotateVector(FVector(0, 1, 0));
			const FVector N = R.N;
			const bool bGroundHit = N != FVector(0, 0, 1);
			const double C = FVector::DotProduct(Fwd, N);
			if (!bSlide)
			{
				const int32 Nb = bContact ? FMath::Max(Nu, 1) : Nu;
				bool bGrounded = false;
				for (int32 J = 0; J < Nb; ++J)
				{
					bGrounded = SSXWipeOut::TumbleBookkeeping(Phase, Air, bContact && J == Nb - 1, bGroundHit, Dt);
				}
				if (Nb)
				{
					++Res.KindN;
					Res.KindOk += bGrounded == (R.Kind != 3);
				}
				if (Res.SettlePred < 0)
				{
					const int32 Dec = SSXWipeOut::SettleDecision(Phase, bGroundHit, 1, N.Z, C);
					if (Dec == 0x2DC || Dec == 0x2DD) { Res.SettlePred = R.Frame; Res.SettlePredAnim = Dec; }
				}
				if (R.Anim == 0x2DC || R.Anim == 0x2DD)
				{
					bSlide = true;
					SlidePhase = Phase;
					Res.SettleLive = R.Frame;
					Res.SettleLiveAnim = R.Anim;
				}
			}
			else
			{
				SlidePhase += Nu * Dt;
				if (Res.GetupPred < 0 && FVector::CrossProduct(N, Fwd).Size() < 0.1 && FMath::Abs(C) > 0.9
					&& R.V.Size() < SSXWipeOut::GetupSpeedLimit(SlidePhase + 2.0 * Dt))
				{
					Res.GetupPred = R.Frame;
				}
			}
		}
		return Res;
	}

	struct FReplay
	{
		bool bRan = false, bFinite = true, bDriver = false, bWallEntry = false, bContactInEntry = false;
		int32 SeedUpdates = 0;
		int32 TumbleUpdates = 0, SlideUpdates = 0, RecoverUpdates = 0, EndMotion = 0, SlideAnim = 0, GetUpAnim = 0;
		double PosErr[4] = { -1, -1, -1, -1 };    // aligned, after 5, 10, 20 samples and at the live tumble end
		double PosErrStrict[4] = { -1, -1, -1, -1 };
		double AngErr[4] = { -1, -1, -1, -1 };    // degrees (aligned update)
		double SpinErr[4] = { -1, -1, -1, -1 };   // | |w_sim| - |w_live| | rad/s from quaternion steps (aligned update)
		double MeanPosErrTumble = 0.0, MaxPosErrTumble = 0.0;
		FString Anims;
	};

	FReplay Replay(const FPatchWorld& World, const TArray<FRow>& Rows, const FLiveSpan& Sp, TArray<FString>* Trace = nullptr)
	{
		FReplay Out;
		if (Sp.Entry < 1 || Sp.End < 0)
		{
			return Out;
		}
		const FRow& R0 = Rows[Sp.Entry];
		const FRow& Rm = Rows[Sp.Entry - 1];
		const double Dt = R0.TimeScale / 60.0;
		const double M0 = R0.M.Size();
		const int32 Decays = M0 < 0.999 && M0 > 0.5 ? FMath::Max(0, (int32)FMath::RoundHalfToEven(FMath::Loge(M0) / FMath::Loge(0.98))) : 0;
		Out.bContactInEntry = R0.Stuck > Rm.Stuck + 0.5;
		Out.SeedUpdates = Out.bContactInEntry ? FMath::Max(1, Decays) : FMath::Max(0, Decays - 1);
		// 0x12BA78, 0x1267A0
		const FVector Mh = M0 > 0.5 ? R0.M / M0 : -R0.N;
		Out.bWallEntry = Out.bContactInEntry || FVector::DotProduct(R0.N, -Mh) < 0.99999;
		FRiderSim Sim;
		SSXRiderSetup::RegisterStates(Sim);
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, R0.Char, R0.BoardClass);
		Sim.Profile.SizeScale = R0.Scale;
		Sim.Profile.Stance = R0.Stance;
		Sim.Profile.BoardClass = R0.BoardClass;
		Sim.SetWorld(&World);
		Sim.Reset();
		Sim.Spawn(R0.P, R0.V.GetSafeNormal2D(), R0.V, 0.0, true);
		FRiderState& St = Sim.State;
		St.BoardClass = R0.BoardClass;
		St.TimeScale = R0.TimeScale;
		SSXAnim::FRiderAnimDriver Driver;
		Out.bDriver = Driver.Init(SSXAnim::FAflLibrary::Shared(), GRiderIds[FMath::Clamp(R0.Char, 0, 11)], R0.BoardClass, R0.Char);
		auto Feed = [&]()
		{
			St.bPosedSpheresValid = false;
			if (!Out.bDriver) { return; }
			SSXAnim::FRiderPalette Pal;
			Driver.ComputePalette(Sim, Pal);
			TArray<FVector> Bones;
			for (const SSXAnim::FRowMat& Mx : Pal.Body) { Bones.Add(FVector(Mx.Row(3))); }
			St.bPosedSpheresValid = Pal.bValid && SSXRider::PosedBodyCentres(Bones.GetData(), Bones.Num(), St.Position, St.Orientation, St.PosedSpheres);
			if (St.bPosedSpheresValid) { St.PosedPivot = St.Orientation.Inverse().RotateVector(Bones[SSXRider::RootBone] - St.Position); }
		};
		auto External = [&](const FRow& R)
		{
			St.PathPoint = R.Path;
			St.PathLookahead = R.Look;
			St.SurfaceP7 = R.P7;
		};
		const bool bRmHeld = Sp.Entry >= 2 && Rows[Sp.Entry - 2].P == Rm.P;
		const FVector PrevP = bRmHeld ? R0.P - 0.5 * (Rm.V + R0.V) * Dt : Rm.P;
		St.Position = Rm.P;
		St.Orientation = Rm.Q;
		Sim.UpdateBasis();
		Feed();
		if (Out.SeedUpdates > 0)
		{
			St.SphereFrameP = R0.P;
			St.SphereFrameQ = R0.Q;
		}
		else
		{
			St.SphereFrameP = Out.bWallEntry ? R0.P - 0.5 * (Rm.V + R0.V) * Dt : PrevP;
			St.SphereFrameQ = Rm.Q;
		}
		St.bSphereFrameValid = true;
		St.Position = R0.P;
		St.Velocity = R0.V;
		St.Orientation = R0.Q;
		St.GroundNormal = R0.N;
		St.SurfaceP7 = R0.P7;
		St.Prediction = R0.Pred;
		Sim.UpdateBasis();
		External(Rm);
		FRiderImpact Impact;
		Impact.Point = R0.P;
		Impact.Normal = -Mh;
		Impact.Direction = R0.V.GetSafeNormal();
		Impact.Speed = FVector::DotProduct(R0.V, Impact.Normal);
		if (Out.bDriver && Rm.Anim > 0)
		{
			const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
			const SSXAnim::FAflClip* Clip = Lib ? Lib->Clip(Lib->Resolve(Rm.Anim, R0.BoardClass, Sim.Profile.GetTrickCharacter())) : nullptr;
			const int32 PrePlay = Clip ? FMath::Min(SSXAnim::ControlFrames(Clip->NFrames) + 14, 200) : 0;
			St.bAnimMirror = Rm.bAnimMirror;
			St.AnimYaw = Rm.AnimYaw;
			const FRiderState Keep = St;
			if (SeedPoseInputs(Rm, St))
			{
				St.GrindType = Rm.Grind;
				St.bStanceGoofy = Rm.Goofy != 0;
			}
			else if (Rm.bAnimInputs)
			{
				St.Crouch.Cur = Rm.Crouch; St.Brake.Cur = Rm.Brake; St.Steer.Cur = Rm.Steer; St.SteerFollow.Cur = Rm.SteerFollow;
				St.AirTurnSmoothed = Rm.AirTurnS; St.AirTiltSmoothed = Rm.AirTiltS; St.Ramp1F0.Cur = Rm.Ramp; St.GrindType = Rm.Grind;
				St.bStanceGoofy = Rm.Goofy != 0;
			}
			if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim)) { Ch->RequestPlay(Rm.Anim); }
			for (int32 K = 0; K < PrePlay; ++K) { Driver.Step(Sim); }
			CopyPoseInputs(Keep, St);
			St.GrindType = Keep.GrindType;
			St.bStanceGoofy = Keep.bStanceGoofy;
			St.Position = Rm.P;
			St.Orientation = Rm.Q;
			Sim.UpdateBasis();
			Feed();
			if (Out.SeedUpdates == 0)
			{
				St.SphereFrameP = Out.bWallEntry ? R0.P - 0.5 * (Rm.V + R0.V) * Dt : PrevP;
				St.SphereFrameQ = Rm.Q;
			}
			else
			{
				St.SphereFrameP = R0.P;
				St.SphereFrameQ = R0.Q;
			}
			St.Position = R0.P;
			St.Orientation = R0.Q;
			Sim.UpdateBasis();
		}
		Sim.StartWipeOut(Impact, TEXT("live replay"));
		SeedPoseInputs(R0, St);
		auto* Motion = static_cast<SSXSimPlaceholders::FWipeOutMotion*>(Sim.GetMotionState(ERiderMotion::WipeOut));
		if (!Motion)
		{
			return Out;
		}
		if (Out.SeedUpdates > 0)
		{
			SSXSimPlaceholders::FWipeOutContext& X = Motion->GetContext();
			const double T = SSXWipeOut::RoundF(Out.SeedUpdates * float(R0.TimeScale) * 0.016666668f);
			X.TotalTime = X.PhaseTime = T;
			X.AirTime = Out.bContactInEntry ? 0.0 : T;
			if (Out.bContactInEntry && X.Body.bValid)
			{
				for (int32 K = Sp.Entry + 1; K < Sp.End; ++K)
				{
					if (Rows[K].Q != R0.Q)
					{
						const FVector W = SpinOf(R0.Q, Rows[K].Q) / double(FMath::Max(1, Rows[K].Frame - R0.Frame));
						const FMatrix I = X.Body.InvI.Inverse();
						X.Body.L = FVector(I.M[0][0] * W.X + I.M[1][0] * W.Y + I.M[2][0] * W.Z,
						                   I.M[0][1] * W.X + I.M[1][1] * W.Y + I.M[2][1] * W.Z,
						                   I.M[0][2] * W.X + I.M[1][2] * W.Y + I.M[2][2] * W.Z);
						X.Body.Refresh();
						break;
					}
				}
			}
		}
		Out.bRan = true;
		if (Out.bDriver) { Driver.Step(Sim); }
		Feed();
		St.SphereFrameP = St.Position;
		St.SphereFrameQ = St.Orientation;
		TArray<FVector> SimP;
		TArray<FQuat> SimQ;
		SimP.Add(St.Position);
		SimQ.Add(St.Orientation);
		int32 LastAnim = -1;
		const int32 F0 = R0.Frame;
		for (int32 K = 1; K < 60 * 14; ++K)
		{
			const int32 Li = FMath::Min(Sp.Entry + K, Rows.Num() - 1);
			External(Rows[Li - 1]);
			Sim.Step(FRiderInputWord());
			if (Out.bDriver) { Driver.Step(Sim); }
			Feed();
			Out.bFinite &= !St.Position.ContainsNaN() && !St.Orientation.ContainsNaN();
			SimP.Add(St.Position);
			SimQ.Add(St.Orientation);
			const SSXSimPlaceholders::FWipeOutContext& X = Motion->GetContext();
			if (St.Motion == ERiderMotion::WipeOut)
			{
				if (X.AnimId == 0x2DC || X.AnimId == 0x2DD) { ++Out.SlideUpdates; Out.SlideAnim = X.AnimId; }
				else { ++Out.TumbleUpdates; }
			}
			else if (St.Motion == ERiderMotion::WipeOutRecover)
			{
				++Out.RecoverUpdates;
				Out.GetUpAnim = X.GetUpAnim;
			}
			if (X.AnimId != LastAnim)
			{
				Out.Anims += FString::Printf(TEXT("%s%X@%d"), Out.Anims.IsEmpty() ? TEXT("") : TEXT(" "), X.AnimId, K);
				LastAnim = X.AnimId;
			}
			if (Trace && K <= 70)
			{
				const FRow* L = Rows.IsValidIndex(Sp.Entry + K) ? &Rows[Sp.Entry + K] : nullptr;
				Trace->Add(FString::Printf(TEXT("k%d f%d sim p(%.0f %.0f %.0f) v(%.0f %.0f %.0f) kind %d n(%.2f %.2f %.2f) anim %X phase %.3f air %.3f |w| %.1f h %.1f | live p(%.0f %.0f %.0f) v(%.0f %.0f %.0f) kind %d n(%.2f %.2f %.2f) anim %X |m| %.4f || pass1 t %.1f n(%.2f %.2f %.2f) c%d pass2 t %.1f c%d pass3 t %.1f c%d"),
					K, L ? L->Frame : -1, St.Position.X, St.Position.Y, St.Position.Z, St.Velocity.X, St.Velocity.Y, St.Velocity.Z, St.ContactKind,
					St.GroundNormal.X, St.GroundNormal.Y, St.GroundNormal.Z, X.AnimId, X.PhaseTime, X.AirTime, X.Body.W.Size(), St.GroundHeight,
					L ? L->P.X : 0, L ? L->P.Y : 0, L ? L->P.Z : 0, L ? L->V.X : 0, L ? L->V.Y : 0, L ? L->V.Z : 0, L ? L->Kind : -1,
					L ? L->N.X : 0, L ? L->N.Y : 0, L ? L->N.Z : 0, L ? L->Anim : 0, L ? L->M.Size() : 0.0,
					X.LastContact.T1, X.LastContact.N1.X, X.LastContact.N1.Y, X.LastContact.N1.Z, X.LastContact.bClose1 ? 1 : 0,
					X.LastContact.T2, X.LastContact.bClose2 ? 1 : 0, X.LastContact.T3, X.LastContact.bClose3 ? 1 : 0));
			}
			if (St.Motion != ERiderMotion::WipeOut && St.Motion != ERiderMotion::WipeOutRecover)
			{
				Out.EndMotion = int32(St.Motion);
				break;
			}
		}
		const int32 TumbleEndIdx = Sp.SlideAt >= 0 ? Sp.SlideAt : (Sp.RecoverAt >= 0 ? Sp.RecoverAt : Sp.End);
		int32 TumbleN = 0;
		for (int32 Li = Sp.Entry + 1; Li < Sp.End; ++Li)
		{
			const FRow& L = Rows[Li];
			const int32 Kb = L.Frame - F0;
			int32 Best = -1;
			double BestE = 1e30;
			for (int32 K = Kb - 1; K <= Kb + 1; ++K)
			{
				if (SimP.IsValidIndex(K))
				{
					const double E = FVector::Dist(SimP[K], L.P);
					if (E < BestE) { BestE = E; Best = K; }
				}
			}
			if (Best < 1) { continue; }
			const int32 N = Li - Sp.Entry;
			const int32 Slot = N == 5 ? 0 : N == 10 ? 1 : N == 20 ? 2 : (Li == TumbleEndIdx ? 3 : -1);
			if (Slot >= 0)
			{
				Out.PosErr[Slot] = BestE;
				Out.PosErrStrict[Slot] = SimP.IsValidIndex(Kb) ? FVector::Dist(SimP[Kb], L.P) : -1.0;
				Out.AngErr[Slot] = AngleDeg(SimQ[Best], L.Q);
				Out.SpinErr[Slot] = FMath::Abs(SpinOf(SimQ[Best - 1], SimQ[Best]).Size() - SpinOf(Rows[Li - 1].Q, L.Q).Size());
			}
			if (Li <= TumbleEndIdx)
			{
				Out.MeanPosErrTumble += BestE;
				Out.MaxPosErrTumble = FMath::Max(Out.MaxPosErrTumble, BestE);
				++TumbleN;
			}
		}
		Out.MeanPosErrTumble = TumbleN ? Out.MeanPosErrTumble / TumbleN : 0.0;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWipeOutLiveTest, "SSX.WipeOut.Live",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXWipeOutLiveTest::RunTest(const FString& Parameters)
{
	TArray<FString> Lines;
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("gari.wipeout_live.csv"));
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 3)
	{
		SSXTest::Skip(*this, TEXT("skipped: SimData/gari.wipeout_live.csv missing (tools/make_wipeout_live_reference.py)"));
		return true;
	}
	FPatchWorld World;
	FString Err;
	if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("skipped: %s"), *Err));
		return true;
	}
	TMap<int32, TArray<FRow>> Cases;
	TArray<int32> Order;
	for (int32 L = 1; L < Lines.Num(); ++L)
	{
		TArray<FString> C;
		Lines[L].ParseIntoArray(C, TEXT(","), false);
		FRow R;
		if (!ParseRow(C, R))
		{
			SSXTest::Skip(*this, TEXT("skipped: old gari.wipeout_live.csv layout, re-run tools/make_wipeout_live_reference.py"));
			return true;
		}
		if (R.Rec.IsEmpty() && L == 1)
		{
			SSXTest::Skip(*this, TEXT("gari.wipeout_live.csv has no boarder records (column rec): the pose inputs are not seeded, re-run tools/make_wipeout_live_reference.py"));
		}
		const int32 Case = FCString::Atoi(*C[0]);
		if (!Cases.Contains(Case)) { Order.Add(Case); }
		Cases.FindOrAdd(Case).Add(R);
	}
	int32 RuleCases = 0, EntryOk = 0, SettleOk = 0, GetupOk = 0, KindOk = 0, KindN = 0;
	int32 AiCases = 0, AiEarlyOk = 0, AiSplitOk = 0, AiSlideOk = 0, AiEndOk = 0;
	for (const int32 Case : Order)
	{
		const TArray<FRow>& Rows = Cases[Case];
		const FLiveSpan Sp = SpanOf(Rows);
		if (Sp.Entry < 1 || Sp.End < 0)
		{
			continue;
		}
		const bool bHuman = Rows[Sp.Entry].bHuman;
		const int32 LiveTumble = (Sp.SlideAt >= 0 ? Sp.SlideAt : (Sp.RecoverAt >= 0 ? Sp.RecoverAt : Sp.End)) - Sp.Entry;
		const int32 LiveSlide = Sp.SlideAt >= 0 ? (Sp.RecoverAt >= 0 ? Sp.RecoverAt : Sp.End) - Sp.SlideAt : 0;
		const int32 LiveRecover = Sp.RecoverAt >= 0 ? Sp.End - Sp.RecoverAt : 0;
		if (Sp.SlideAt >= 0 && Sp.RecoverAt >= 0)
		{
			const FRuleResult R = RuleChecks(Rows, Sp);
			const bool bSettle = R.SettlePred >= 0 && R.SettleLive >= 0 && FMath::Abs(R.SettlePred - R.SettleLive) <= 2 && R.SettlePredAnim == R.SettleLiveAnim;
			const bool bGetup = R.GetupPred >= 0 && R.RecoverFrame - R.GetupPred >= 0 && R.RecoverFrame - R.GetupPred <= 2;
			static const int32 R24Set[] = { 1401304, 1403810, 1404160, 1405563, 1407051 };
			bool bR24 = false;
			for (const int32 C : R24Set) { bR24 |= C == Case; }
			if (bR24)
			{
				++RuleCases;
				EntryOk += R.bEntryAnim;
				SettleOk += bSettle;
				GetupOk += bGetup;
				KindOk += R.KindOk;
				KindN += R.KindN;
			}
			AddInfo(FString::Printf(TEXT("rules %d: entry anim %d, settle predicted f%d %#x / live f%d %#x, get-up test f%d / live f%d, kind %d/%d"),
				Case, R.bEntryAnim, R.SettlePred, R.SettlePredAnim, R.SettleLive, R.SettleLiveAnim, R.GetupPred, R.RecoverFrame, R.KindOk, R.KindN));
		}
		TArray<FString> Trace;
		const FString Want = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WIPEOUT_TRACE"));
		const bool bTrace = !Want.IsEmpty() && FCString::Atoi(*Want) == Case;
		const FReplay P = Replay(World, Rows, Sp, bTrace ? &Trace : nullptr);
		for (const FString& T : Trace) { AddInfo(T); }
		if (!P.bRan)
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("replay %d (%s, char %d, ts %.2f, %s entry, seed +%d): live tumble/slide/recover %d/%d/%d -> %s; port %d/%d/%d -> motion %d (slide %#x, get-up %#x) | pos err 5/10/20/tumble-end aligned %.0f/%.0f/%.0f/%.0f strict %.0f/%.0f/%.0f/%.0f cm (tumble mean %.0f max %.0f) | ang err %.0f/%.0f/%.0f/%.0f deg | spin err %.1f/%.1f/%.1f/%.1f rad/s | anims %s"),
			Case, bHuman ? TEXT("player, input not traced") : TEXT("AI, input 0"), Rows[Sp.Entry].Char, Rows[Sp.Entry].TimeScale,
			P.bContactInEntry ? TEXT("contact") : (P.bWallEntry ? TEXT("wall") : TEXT("landing")), P.SeedUpdates,
			LiveTumble, LiveSlide, LiveRecover, Sp.EndMotion == 1 ? TEXT("Air (T61)") : TEXT("Ground"),
			P.TumbleUpdates, P.SlideUpdates, P.RecoverUpdates, P.EndMotion, P.SlideAnim, P.GetUpAnim,
			P.PosErr[0], P.PosErr[1], P.PosErr[2], P.PosErr[3], P.PosErrStrict[0], P.PosErrStrict[1], P.PosErrStrict[2], P.PosErrStrict[3], P.MeanPosErrTumble, P.MaxPosErrTumble,
			P.AngErr[0], P.AngErr[1], P.AngErr[2], P.AngErr[3], P.SpinErr[0], P.SpinErr[1], P.SpinErr[2], P.SpinErr[3], *P.Anims));
		TestTrue(FString::Printf(TEXT("case %d finite and terminating"), Case), P.bFinite && P.EndMotion != 0);
		if (!bHuman && P.bDriver)
		{
			++AiCases;
			const bool bEarly = P.PosErr[1] >= 0.0 && P.PosErr[1] < 100.0;
			const bool bMean = P.MeanPosErrTumble < 130.0;
			const bool bEnd = (Sp.EndMotion == 1) == (P.EndMotion == int32(ERiderMotion::Air));
			AiEarlyOk += bEarly;
			AiSplitOk += FMath::Abs(P.TumbleUpdates - LiveTumble) <= 6;
			AiSlideOk += (Sp.SlideAt < 0) == (P.SlideAnim == 0) && (Sp.SlideAt < 0 || P.SlideAnim == Sp.SlideAnim);
			AiEndOk += bEnd;
			TestTrue(FString::Printf(TEXT("AI case %d: same end state (T61 air / get-up ground)"), Case), bEnd);
			TestTrue(FString::Printf(TEXT("AI case %d: position within 100 cm after 10 samples (%.0f)"), Case, P.PosErr[1]), bEarly);
			TestTrue(FString::Printf(TEXT("AI case %d: mean tumble position error < 130 cm (%.0f)"), Case, P.MeanPosErrTumble), bMean);
		}
	}
	AddInfo(FString::Printf(TEXT("rules vs %d live wipe-outs: entry anim %d, settle %d, get-up %d, kind %d/%d samples"), RuleCases, EntryOk, SettleOk, GetupOk, KindOk, KindN));
	AddInfo(FString::Printf(TEXT("trajectory vs %d AI wipe-outs (exact input): 10-sample pos < 100 cm %d, tumble/slide split +-6 %d, slide anim %d, end state %d"),
		AiCases, AiEarlyOk, AiSplitOk, AiSlideOk, AiEndOk));
	TestTrue(TEXT("R24 rule checks: entry TumbleAnim"), RuleCases > 0 && EntryOk == RuleCases);
	TestTrue(TEXT("R24 rule checks: settle frame + anim"), SettleOk == RuleCases);
	TestTrue(TEXT("R24 rule checks: get-up within 2 samples"), GetupOk == RuleCases);
	TestTrue(TEXT("R24 rule checks: contact-kind bookkeeping (all but one sample)"), KindOk >= KindN - 1);
	TestTrue(TEXT("AI trajectory cases present"), AiCases >= 6);
	return true;
}

#endif
