// 0x10C198, 0x10DAD8, 0x10C2F0, 0x10DCA8, 0x10E190, 0x10FD58, 0x10F9A0, 0x10F178, 0x10D680, 0x110260, 0x1102C8, 0x110290, 0x108140, 0x108198, 0x108280, 0x123B10, 0x123EC8, 0x123F00, 0x15FDD0, 0x15EEC0
#include "Sim/RiderPlaceholderStates.h"
#include "Sim/SimCoords.h"

#include "Sim/Air/LandingPredictor.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/Ground/WallCollision.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/SurfaceTable.h"
#include "Sim/EEFloat.h"

namespace SSXSimPlaceholders
{
namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;
	using SSXWipeOut::RoundF;
	const FVector Z = FVector::UpVector;

	namespace WO
	{
		constexpr int32 EntrySurface = 10;
		constexpr float FilterRate = 2.0f;
		constexpr float LiftBase = 10.0f;
		constexpr float AirPhase = 0.1f;
		constexpr float SelfRightTime = 0.1f, SelfRightStick = 0.5f, SelfRightUp = 0.9f, SelfRightSpin = 4.0f;
		constexpr float SelfRightGain = 250951.34f;
		constexpr float SelfRightYawGain = 125475.67f;
		constexpr float InputDamping = 3.3475056f;
		constexpr float InputTurnGain = 75285.4f;
		constexpr float InputTiltGain = -125475.67f;
		constexpr float SettleRoll = 400000.0f;
		constexpr float QuickTotal = 0.5f, QuickPhase = 0.1f, QuickSpin = 7.0f, QuickLook = 0.8f, QuickUp = 0.85f;
		constexpr float PhaseTimeout = 3.0f, TotalTimeout = 7.0f;
		constexpr float StuckTimeout = 1.0e6f;
		constexpr float FixedStep = 0.016666668f;
		constexpr float SlideDamping = 0.98333335f;
		constexpr float SnapLimit = 8.333333f;
		constexpr float SlideBreakNz = 0.3f;
		constexpr float AlignEps = 1e-5f, AlignClamp = 0.99999f, AlignRate = 0.083333336f;
		constexpr float LyingSin = 0.1f, LyingFaceDot = 0.9f;
		constexpr float GetUpDirCos = 0.70710677f;
		constexpr float RecoverSlerp = 0.083333336f;
		constexpr float GroundCheckHalf = 400.0f;
		constexpr float GroundCheckTRef = 0.5f;
		constexpr float HalfPi = 1.5707964f;
		constexpr float Pi = 3.1415927f;

		bool Bail(int32 S) { return S == 6 || S == 10; }
	}

	FORCEINLINE FVector Unit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 0.0 ? V / L : Fallback;
	}

	int32 AnimFrames(const FRiderSim& Sim, const FWipeOutContext& X, int32 AnimId)
	{
		int32 N = X.Anim ? X.Anim->Frames(Sim, AnimId) : 0;
		if (N <= 0)
		{
			if (const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared())
			{
				if (const SSXAnim::FAflClip* Clip = Lib->Clip(Lib->Resolve(AnimId, Sim.State.BoardClass, Sim.Profile.GetTrickCharacter())))
				{
					N = Clip->NFrames;
				}
			}
		}
		return N;
	}

	int32 ChainOf(int32 AnimId)
	{
		if (AnimId == 0x2E0) { return 0x2DE; }
		if (AnimId >= 0x2E1 && AnimId <= 0x2ED) { return 0x2DF; }
		return -1;
	}

	int32 CategoryOf(int32 AnimId)
	{
		if (AnimId == 0x2DF || (AnimId >= 0x2E0 && AnimId <= 0x2E6)) { return 17; }
		if ((AnimId >= 0x2DC && AnimId <= 0x2DE) || (AnimId >= 0x2E7 && AnimId <= 0x2ED)) { return 18; }
		if (const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared())
		{
			if (const SSXAnim::FAnimProps* P = Lib->Props(AnimId)) { return P->Category; }
		}
		return 8;
	}

	// 0x15FCF8
	void PlayAnim(FRiderSim& Sim, FWipeOutContext& X, int32 AnimId)
	{
		X.AnimId = AnimId;
		X.AnimPlayFrame = Sim.State.Frame;
		X.AnimFrames = AnimFrames(Sim, X, AnimId);
		if (X.Anim) { X.Anim->RequestPlay(AnimId); }
	}

	int32 CurrentAnim(const FRiderSim& Sim, FWipeOutContext& X)
	{
		const int32 Next = ChainOf(X.AnimId);
		if (Next >= 0 && X.AnimFrames > 0 && Sim.State.Frame - X.AnimPlayFrame >= SSXAnim::ControlFrames(X.AnimFrames))
		{
			X.AnimPlayFrame += SSXAnim::ControlFrames(X.AnimFrames);
			X.AnimId = Next;
			X.AnimFrames = AnimFrames(Sim, X, Next);
		}
		return X.AnimId;
	}

	// 0x218328
	void PlayTumbleAnim(FRiderSim& Sim, FWipeOutContext& X)
	{
		FRiderState& St = Sim.State;
		const bool bFirst = CategoryOf(CurrentAnim(Sim, X)) == 17;
		const FVector& N = St.GroundNormal;
		const float Un = float(FVector::DotProduct(St.BasisUp, N));
		++St.CrashSoundCount;
		int32 Id;
		if (Un < -0.7f) { Id = bFirst ? 0x2E4 : 0x2EB; }
		else if (0.7f < Un) { Id = bFirst ? 0x2E3 : 0x2EA; }
		else
		{
			const float Sn = float(FVector::DotProduct(St.BasisSide, N));
			if (0.7f < Sn) { Id = bFirst ? 0x2E5 : 0x2EC; }
			else if (Sn < -0.7f) { Id = bFirst ? 0x2E6 : 0x2ED; }
			else { Id = 0.0f < float(FVector::DotProduct(St.BasisForward, N)) ? (bFirst ? 0x2E1 : 0x2E8) : (bFirst ? 0x2E2 : 0x2E9); }
		}
		PlayAnim(Sim, X, Id);
	}

	FVector PivotOffset(const FRiderSim& Sim)
	{
		const FRiderState& St = Sim.State;
		FVector Pivot = FVector::ZeroVector;
		if (St.bPosedSpheresValid) { Pivot = St.PosedPivot; }
		else { SSXRider::BodyPivot(Sim.Profile, St.bStanceGoofy, Pivot); }
		return Pivot;
	}

	// 0x128A18
	FVector FallLineOf(const FVector& N, const FVector& Fallback)
	{
		const FVector A = FVector::CrossProduct(N, Z);
		const double La = A.Size();
		return float(La) > 0.0001f ? FVector::CrossProduct(N, A / La) : Fallback;
	}

	bool TumbleGroundCheck(FRiderSim& Sim)
	{
		FRiderState& St = Sim.State;
		const IRiderWorld* World = Sim.GetWorld();
		const FVector N = St.GroundNormal;
		const FVector Rp = St.Orientation.RotateVector(PivotOffset(Sim));
		const FVector A = St.Position + Rp - FVector::DotProduct(Rp, N) * N;
		FRiderWorldHit Hit;
		if (World && World->LineQuery(A - WO::GroundCheckHalf * N, A + WO::GroundCheckHalf * N, ERiderLineQuery::Fine, WO::GroundCheckTRef, Hit))
		{
			St.ContactKind = Hit.Kind;
			St.SurfaceId = Hit.Surface;
			St.GroundNormal = RoundF(Hit.Normal);
			const FVector Up = St.BasisUp;
			St.ContactForward = Unit(Up - FVector::DotProduct(Up, St.GroundNormal) * St.GroundNormal, St.ContactForward);
			St.GroundHeight = RoundF(FVector::DotProduct(St.Position - Hit.Point, St.GroundNormal));
			St.FallLine = FallLineOf(St.GroundNormal, Up);
			St.ContactPoint = Hit.Point;
			return true;
		}
		St.GroundNormal = Z;
		const FVector Up = St.BasisUp;
		St.ContactForward = Unit(Up - FVector::DotProduct(Up, Z) * Z, St.ContactForward);
		St.FallLine = Up;
		St.ContactKind = 3;
		return false;
	}

	// 0x123AB8
	void RefreshPrediction(FRiderSim& Sim)
	{
		FRiderState& St = Sim.State;
		const SSXAir::FLandingPredictorResult Pred = SSXAir::PredictLanding(Sim.GetWorld(), St.Position, St.Velocity, false);
		FLandingPrediction& LP = St.Prediction;
		LP.AirElapsed = 0.0;
		LP.ApexTime = Pred.ApexTime;
		if (Pred.bHit)
		{
			LP.FlightTime = LP.TimeLeft = RoundF(Pred.HitTime);
			LP.ApexLeft = Pred.ApexTime;
			LP.LandPoint = Pred.Point;
			LP.LandVelocity = Pred.Velocity;
			LP.LandNormal = Pred.Normal;
			LP.LandSurface = Pred.Surface;
		}
		else
		{
			LP.FlightTime = LP.TimeLeft = 0.0;
			LP.ApexLeft = 0.0;
			LP.LandPoint = St.Position;
			LP.LandVelocity = St.Velocity;
			LP.LandNormal = St.GroundNormal;
			LP.LandSurface = -1;
		}
	}

	void TickPrediction(FRiderState& St)
	{
		FLandingPrediction& LP = St.Prediction;
		LP.AirElapsed = RoundF(float(LP.AirElapsed) + WO::FixedStep);
		LP.TimeLeft = RoundF(float(LP.FlightTime) - float(LP.AirElapsed));
		LP.ApexLeft = RoundF(float(LP.ApexTime) - float(LP.AirElapsed));
	}

	void CurrentSpheres(const FRiderSim& Sim, const FWipeOutContext& X, FVector Out[SSXRider::NumShapeSpheres])
	{
		const FRiderState& St = Sim.State;
		const FVector P0 = St.bSphereFrameValid ? St.SphereFrameP : St.Position;
		const FQuat Q0 = St.bSphereFrameValid ? St.SphereFrameQ : St.Orientation;
		const FVector* BodyFrame = St.bPosedSpheresValid ? St.PosedSpheres : X.BindCentres;
		for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I)
		{
			Out[I] = RoundF(P0 + Q0.RotateVector(BodyFrame[I]));
		}
	}

	// 0x128E90, 0x129008
	void RotateAbout(FRiderSim& Sim, const FVector& Axis, float Angle)
	{
		Sim.RotateBody(Axis, double(Angle));
	}

	// 0x15FA40
	bool AnimRootOffset(const FRiderSim& Sim, int32 AnimId, FVector& Out)
	{
		const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
		const SSXAnim::FAflClip* Clip = Lib ? Lib->Clip(Lib->Resolve(AnimId, Sim.State.BoardClass, Sim.Profile.GetTrickCharacter())) : nullptr;
		const SSXAnim::FAflBlock* Block = Clip ? Clip->Block(0) : nullptr;
		if (!Block || Block->Tracks.Num() < 3)
		{
			return false;
		}
		const double S = Sim.Profile.SizeScale;
		Out = FVector(Lib->EvalTrack(Block->Tracks[0], 0.0), Lib->EvalTrack(Block->Tracks[1], 0.0), Lib->EvalTrack(Block->Tracks[2], 0.0)) * S;
		return true;
	}
}

TSharedRef<FWipeOutContext> InstallWipeOutStates(FRiderSim& Sim)
{
	TSharedRef<FWipeOutContext> Ctx = MakeShared<FWipeOutContext>();
	Ctx->Anim = SSXAnim::AcquireChannel(Sim);
	TSharedRef<FWipeOutMotion> Motion = MakeShared<FWipeOutMotion>(Ctx);
	Sim.SetMotionState(M::WipeOut, Motion);
	Sim.SetMotionState(M::WipeOutRecover, Motion);
	Sim.SetControlState(C::WipeOut, MakeShared<FWipeOutControl>(Ctx));
	Sim.SetControlState(C::WipeOutRecover, MakeShared<FWipeOutRecoverControl>(Ctx));
	return Ctx;
}

FWipeOutContext* FindWipeOutContext(const FRiderSim& Sim)
{
	const FRiderSim* Owner = Sim.GetRegistrationSim() ? Sim.GetRegistrationSim() : &Sim;
	IRiderMotionState* Motion = Owner->GetMotionState(M::WipeOut);
	return Motion ? &static_cast<FWipeOutMotion*>(Motion)->GetContext() : nullptr;
}

void FWipeOutControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x108140
	Ctx->AirTurn = In.AirTurn;
	Ctx->AirTilt = In.AirTilt;
}

void FWipeOutMotion::EnterBody(FRiderSim& Sim)
{
	// 0x10DAD8
	FRiderState& St = Sim.State;
	FWipeOutContext& X = *Ctx;
	if (!X.bTumblePoseSimulated)
	{
		return;
	}
	FVector Spheres[SSXRider::NumShapeSpheres];
	CurrentSpheres(Sim, X, Spheres);
	X.Body.InitFromSpheres(Spheres, St.Orientation, St.Velocity);
	X.Info.Normal = RoundF(-X.Info.Normal);
	St.LastImpactNormal = X.Info.Normal;
	const FVector R = X.Info.Point - X.Body.X;
	if (FVector::DotProduct(X.Body.PointVelocity(R), X.Info.Normal) < 0.0)
	{
		X.Body.ContactImpulse(R, X.Info.Normal, SSXWipeOut::EntryRestitution, 0.0);
	}
	X.Spin = X.Body.W.Size();
}

void FWipeOutMotion::Enter(FRiderSim& Sim, ERiderMotion Previous)
{
	FRiderState& St = Sim.State;
	FWipeOutContext& X = *Ctx;
	if (St.Motion == M::WipeOutRecover)
	{
		// 0x110260
		Sim.RunGroundProbe();
		return;
	}
	// 0x10C198
	{
		const TSharedPtr<SSXAnim::FRiderAnimChannel> Anim = X.Anim;
		const int32 AnimId = X.AnimId, PlayFrame = X.AnimPlayFrame, Frames = X.AnimFrames;
		X = FWipeOutContext();
		X.Anim = Anim;
		if (Previous == M::WipeOutRecover)
		{
			X.AnimId = AnimId; X.AnimPlayFrame = PlayFrame; X.AnimFrames = Frames;
		}
	}
	X.Info = St.LastImpact;
	X.bTumblePoseSimulated = St.bPosedSpheresValid || SSXRider::BodyCentres(Sim.Profile, St.bStanceGoofy, X.BindCentres);
	EnterBody(Sim);
	if (!X.bTumblePoseSimulated)
	{
		const FVector N = -Unit(St.LastImpact.Normal, Z);
		St.LastImpactNormal = N;
		const double Vn = FVector::DotProduct(St.Velocity, N);
		if (Vn < 0.0) { St.Velocity -= (1.0 + SSXWipeOut::EntryRestitution) * Vn * N; }
		X.Spin = 9.0;
		X.LyingAngle = 0.78539816;
	}
	const bool bGoofy = Sim.Profile.Stance != 0 && St.BoardClass == 2;
	St.bStanceGoofy = bGoofy;
	Sim.LogNote(FString::Printf(TEXT("wipe-out (0x10C198): +0x1B4 = %d (profile +0x40 %d, board class %d)%s"), bGoofy ? 1 : 0,
		Sim.Profile.Stance, St.BoardClass, (bGoofy != (Sim.Profile.Stance != 0)) ? TEXT(": riding switch from here") : TEXT("")));
	St.AnimYaw = bGoofy ? double(WO::Pi) : 0.0;
	St.bAnimMirror = bGoofy;
	St.bRailAnimPose = false;
	// 0x15F050, 0x10F9A0
	if (X.Anim.IsValid())
	{
		X.Anim->FadeChannel1(Sim);
	}
	if (!St.bPosedSpheresValid && X.bTumblePoseSimulated)
	{
		SSXRider::BodyCentres(Sim.Profile, St.bStanceGoofy, X.BindCentres);
	}
	X.AirTime = X.TotalTime = X.PhaseTime = 0.0;
	St.SurfaceId = WO::EntrySurface;
	St.GrindType = 0;
	PlayTumbleAnim(Sim, X);
	for (FApproachFilter* F : { &St.Crouch, &St.Brake, &St.Steer, &St.SteerFollow, &St.Lean, &St.Ramp1F0 })
	{
		F->Target = 0.0;
		F->Rate = WO::FilterRate;
	}
	// 0x218318
}

void FWipeOutMotion::Exit(FRiderSim& Sim, ERiderMotion Next)
{
	if (Sim.State.Motion == M::WipeOutRecover)
	{
		// 0x110290, 0x218B68
		Sim.RunGroundProbe();
	}
	// 0x10D680, 0x218B20
	Sim.State.Highlight(19);
}

FString FWipeOutMotion::DebugString() const
{
	const FWipeOutContext& X = *Ctx;
	const SSXWipeOut::FTumbleBody& B = X.Body;
	return FString::Printf(TEXT("wipe-out anim 0x%X total %.3f phase %.3f air %.3f spin %.2f contacts %d rigid %d body x (%.1f %.1f %.1f) v (%.1f %.1f %.1f) |L| %.1f settle f%d get-up 0x%X"),
		X.AnimId, X.TotalTime, X.PhaseTime, X.AirTime, X.Spin, X.Contacts, X.bTumblePoseSimulated ? 1 : 0, B.X.X, B.X.Y, B.X.Z,
		B.V.X, B.V.Y, B.V.Z, B.L.Size(), X.SettleFrame, X.GetUpAnim);
}

void FWipeOutMotion::Update(FRiderSim& Sim)
{
	FWipeOutContext& X = *Ctx;
	if (Sim.State.Motion == M::WipeOutRecover)
	{
		Recover(Sim);
		return;
	}
	// 0x10C2F0
	const float Dt = float(Sim.State.TimeScale) * WO::FixedStep;
	X.TotalTime = SSXEE::FpuAdd(float(X.TotalTime), Dt);   // 0x10C344
	const int32 Anim = CurrentAnim(Sim, X);
	if (Anim == 0x2DC || Anim == 0x2DD)
	{
		Slide(Sim, Anim);
	}
	else if (X.bTumblePoseSimulated)
	{
		TumbleRigid(Sim, Anim);
	}
	else
	{
		TumbleReduced(Sim, Anim);
	}
}

void FWipeOutMotion::TumbleRigid(FRiderSim& Sim, int32 Anim)
{
	FRiderState& St = Sim.State;
	FWipeOutContext& X = *Ctx;
	SSXWipeOut::FTumbleBody& B = X.Body;
	const float Dt = float(St.TimeScale) * WO::FixedStep;
	auto MassF = [&B]() { return float(B.InvMass) == 0.0f ? -1.0f : 1.0f / float(B.InvMass); };

	// 0x1267A0, 0x10DCA8
	const FVector N0 = St.GroundNormal;
	const float LiftK = SSXEE::FpuAdd(float(St.SurfaceP7), WO::LiftBase) * 0.5f;
	const float Lift = float(N0.Z) < 0.0f ? LiftK * 0.0f : LiftK * float(N0.Z);
	FVector Spheres[SSXRider::NumShapeSpheres];
	CurrentSpheres(Sim, X, Spheres);
	const FVector3f LiftV = SSXEE::Scale(Lift, SSXEE::F(N0));   // 0x102F50
	if (Lift > 0.0f)
	{
		for (FVector& S : Spheres) { S = SSXEE::D(SSXEE::Add(SSXEE::F(S), LiftV)); }   // 0x236448
		Sim.NotifySphereSetMoved(SSXEE::D(LiftV));
	}

	const FVector Centroid = SSXWipeOut::MassFromSpheres(B, Spheres);   // 0x10D6F8
	FVector F, Tau;
	{
		using namespace SSXEE;
		const float Im = float(B.InvMass);
		F = D(Sub(FVector3f(0.0f, 0.0f, -SSXWipeOut::TumbleGravity / Im), Scale(SSXWipeOut::TumbleLinearDrag, SSXEE::F(B.P))));
		Tau = D(Sub(FVector3f::ZeroVector, Scale(SSXWipeOut::TumbleAngularDrag, SSXEE::F(B.L))));
		if (float(X.AirTime) < SSXWipeOut::HomingWindow && !St.PathPoint.IsZero())
		{
			const FVector3f N = SSXEE::F(St.GroundNormal);
			FVector3f T = Sub(SSXEE::F(St.PathPoint), SSXEE::F(Centroid));
			T = Sub(T, Scale(Dot(T, N), N));
			const float L2 = Dot(T, T);
			if (L2 > 0.0f)
			{
				F = D(Add(SSXEE::F(F), Scale(SSXWipeOut::HomingAccel / Im, Scale(Rsqrt(L2), T))));
			}
		}
	}
	B.Integrate(F, Tau, Dt);   // 0x153C50
	B.Refresh();               // 0x153AD8
	const SSXWipeOut::FTumbleContact Contact = SSXWipeOut::TumbleContactPass(Sim, B, Spheres, St.GroundNormal);   // 0x10E190
	X.Contacts += Contact.Impulses;
	X.LastContact = Contact;
	// 0x129008
	{
		using SSXEE::FpuAdd;
		using SSXEE::FpuSub;
		auto CrossFpu = [](float Ux, float Uy, float Uz, const FVector3f& V)
		{
			return FVector3f(FpuSub(Uy * V.Z, Uz * V.Y), FpuSub(Uz * V.X, Ux * V.Z), FpuSub(Ux * V.Y, Uy * V.X));
		};
		const FVector3f A = SSXEE::Sub(SSXEE::F(Centroid), SSXEE::F(St.Position));
		float Qw = float(St.Orientation.W), Qx = float(St.Orientation.X), Qy = float(St.Orientation.Y), Qz = float(St.Orientation.Z);
		FVector3f T = CrossFpu(Qx, Qy, Qz, A);
		FVector3f U = CrossFpu(Qx, Qy, Qz, T);
		const FVector3f Wt = SSXEE::Scale(FpuAdd(Qw, Qw), T);
		const FVector3f Dl(FpuAdd(FpuSub(A.X, Wt.X), 2.0f * U.X), FpuAdd(FpuSub(A.Y, Wt.Y), 2.0f * U.Y), FpuAdd(FpuSub(A.Z, Wt.Z), 2.0f * U.Z));
		St.Orientation = B.Q;
		Qw = float(B.Q.W); Qx = float(B.Q.X); Qy = float(B.Q.Y); Qz = float(B.Q.Z);
		T = CrossFpu(Qx, Qy, Qz, Dl);
		U = CrossFpu(Qx, Qy, Qz, T);
		const FVector3f Wt2 = SSXEE::Scale(FpuAdd(Qw, Qw), T);
		const FVector3f Dw(FpuAdd(FpuAdd(Dl.X, Wt2.X), 2.0f * U.X), FpuAdd(FpuAdd(Dl.Y, Wt2.Y), 2.0f * U.Y), FpuAdd(FpuAdd(Dl.Z, Wt2.Z), 2.0f * U.Z));
		St.Position = SSXEE::D(SSXEE::Sub(SSXEE::F(B.X), Dw));
	}
	St.Velocity = B.V;
	Sim.UpdateBasis();
	if (Lift > 0.0f)
	{
		Sim.NotifySphereSetMoved(SSXEE::D(SSXEE::Scale(-1.0f, LiftV)));
	}
	const bool bCheck = TumbleGroundCheck(Sim);   // 0x10FD58
	const int32 Kind = Contact.bImpulse ? Contact.Hit.Kind : 3;
	St.ContactKind = Kind;

	bool bDamp = true;
	if (Kind == 3)
	{
		float Air = float(X.AirTime);
		if (WO::AirPhase < Air)
		{
			if (Anim == 0x2DF)
			{
				PlayAnim(Sim, X, 0x2E0);
				RefreshPrediction(Sim);   // 0x123B10
			}
			else
			{
				TickPrediction(St);       // 0x123EC8
			}
			const FLandingPrediction& LP = St.Prediction;
			if (WO::SelfRightTime < float(LP.TimeLeft) && !WO::Bail(LP.LandSurface))
			{
				using namespace SSXEE;
				const float Cu = Dot(SSXEE::F(St.BasisUp), SSXEE::F(LP.LandNormal));
				if (FMath::Abs(float(X.AirTurn)) < WO::SelfRightStick && FMath::Abs(float(X.AirTilt)) < WO::SelfRightStick)
				{
					const FVector3f NL = SSXEE::F(LP.LandNormal), Vl = SSXEE::F(LP.LandVelocity);
					FVector3f T = Sub(Vl, Scale(Dot(Vl, NL), NL));
					const float T2 = Dot(T, T);
					T = T2 > 0.0f ? Scale(Rsqrt(T2), T) : T;
					const float Ft = Dot(SSXEE::F(St.BasisForward), T);
					const FVector3f Dl1 = Scale(Dt * WO::SelfRightGain * MassF(), Cross(SSXEE::F(St.BasisUp), NL));
					const float Yaw = 0.0f < Ft ? WO::SelfRightYawGain : -WO::SelfRightYawGain;
					const FVector3f Dl2 = Scale(Dt * Yaw * MassF(), Cross(SSXEE::F(St.BasisForward), T));
					bDamp = false;
					B.L = D(Add(Scale(FpuSub(1.0f, Dt * WO::InputDamping), SSXEE::F(B.L)), Add(Dl1, Dl2)));
					B.SpinFromL();
					if (WO::SelfRightUp < Cu && float(B.W.Size()) < WO::SelfRightSpin)
					{
						St.Ramp1F0.Cur = St.Ramp1F0.Target = St.Ramp1F0.Rate = 0.0;
						PlayAnim(Sim, X, 0x224);
						X.Spin = B.W.Size();
						Sim.ApplyTransition(TEXT("T61"));
						return;
					}
				}
			}
			X.PhaseTime = 0.0;
			Air = float(X.AirTime);
		}
		else if (!bCheck)
		{
			X.PhaseTime = 0.0;
		}
		else
		{
			St.ContactKind = 0;
			X.PhaseTime = SSXEE::FpuAdd(float(X.PhaseTime), Dt);
		}
		X.AirTime = SSXEE::FpuAdd(Air, Dt);
	}
	else
	{
		const FRiderWorldHit& H = Contact.Hit;
		if (!bCheck || float(St.GroundNormal.Z) < float(H.Normal.Z))
		{
			St.GroundNormal = H.Normal;
		}
		St.SurfaceId = (Kind == 0 || Kind == 2) ? H.Surface : WO::EntrySurface;
		if ((float(X.PhaseTime) == 0.0f && Anim == 0x2DF) || Anim == 0x2DE || Anim == 0x2E0)
		{
			PlayTumbleAnim(Sim, X);          // 0x10F9A0
			St.Prediction.FlightTime = 0.0;  // 0x123F00
		}
		X.AirTime = 0.0;
		X.PhaseTime = SSXEE::FpuAdd(float(X.PhaseTime), Dt);
	}

	if (bCheck && SSXWipeOut::SettlePhaseTime < float(X.PhaseTime) && !WO::Bail(St.SurfaceId))
	{
		const FVector N = St.GroundNormal;
		const float Cf = float(FVector::DotProduct(St.BasisForward, N));
		++St.SlideSoundCount;   // 0x218700
		if (SSXWipeOut::SettleSteepNz < float(N.Z))
		{
			if (SSXWipeOut::SettleFaceDot < Cf || Cf < -SSXWipeOut::SettleFaceDot)
			{
				PlayAnim(Sim, X, SSXWipeOut::SettleFaceDot < Cf ? 0x2DC : 0x2DD);
				X.SettleFrame = St.Frame;
			}
			else
			{
				const float Sign = Cf < 0.0f ? -WO::SettleRoll : WO::SettleRoll;
				B.L = SSXEE::D(SSXEE::Add(SSXEE::F(B.L), SSXEE::Scale(Dt * Sign * MassF(), SSXEE::Cross(SSXEE::F(St.BasisForward), SSXEE::F(N)))));
				B.SpinFromL();
			}
		}
	}

	if (WO::QuickTotal < float(X.TotalTime) && WO::QuickPhase < float(X.PhaseTime) && !WO::Bail(St.SurfaceId)
		&& float(B.W.Size()) < WO::QuickSpin && SSXWipeOut::SettleSteepNz < float(St.GroundNormal.Z))
	{
		const FVector Dl = SSXSim::RsqrtNormal((St.PathLookahead - St.Position));
		if (WO::QuickLook < FMath::Abs(float(FVector::DotProduct(Dl, St.BasisForward)))
			&& WO::QuickUp < float(FVector::DotProduct(St.BasisUp, St.GroundNormal)))
		{
			if (Sim.RunGroundProbe() && !WO::Bail(St.SurfaceId))   // 0x128AE8
			{
				PlayAnim(Sim, X, CategoryOf(X.AnimId) == 17 ? 0x250 : 0x251);
				X.Spin = B.W.Size();
				Sim.ApplyTransition(TEXT("T62"));
				return;
			}
		}
	}

	const FVector3f Dl = SSXEE::Add(SSXEE::Scale(Dt * WO::InputTurnGain * MassF() * float(X.AirTurn), SSXEE::F(St.BasisSide)),
	                                SSXEE::Scale(Dt * WO::InputTiltGain * MassF() * float(X.AirTilt), SSXEE::F(St.BasisForward)));
	B.L = SSXEE::D(SSXEE::Add(bDamp ? SSXEE::Scale(SSXEE::FpuSub(1.0f, Dt * WO::InputDamping), SSXEE::F(B.L)) : SSXEE::F(B.L), Dl));
	B.SpinFromL();
	X.Spin = B.W.Size();

	if (WO::PhaseTimeout <= float(X.PhaseTime) || WO::TotalTimeout < float(X.TotalTime))
	{
		if (St.FinishResult < 0)
		{
			St.StuckAccumA = WO::StuckTimeout;
			return;
		}
		PlayAnim(Sim, X, 0.0f < float(FVector::DotProduct(St.BasisForward, St.GroundNormal)) ? 0x2DC : 0x2DD);
		BeginRecover(Sim);
	}
}

void FWipeOutMotion::TumbleReduced(FRiderSim& Sim, int32 Anim)
{
	FRiderState& St = Sim.State;
	FWipeOutContext& X = *Ctx;
	const float Dt = float(St.TimeScale) * WO::FixedStep;
	const FVector N0 = St.GroundNormal;
	FVector V = St.Velocity;
	FVector A = FVector(0, 0, -SSXWipeOut::TumbleGravity) - SSXWipeOut::TumbleLinearDrag * V;
	if (float(X.AirTime) < SSXWipeOut::HomingWindow && !St.PathPoint.IsZero())
	{
		FVector T = St.PathPoint - St.Position;
		T -= FVector::DotProduct(T, N0) * N0;
		if (T.SizeSquared() > 0.0) { A += SSXWipeOut::HomingAccel * T.GetUnsafeNormal(); }
	}
	V += A * Dt;
	St.Position += V * Dt;
	X.Spin *= (1.0 - SSXWipeOut::TumbleAngularDrag * Dt) * (1.0 - WO::InputDamping * Dt);
	const bool bCheck = TumbleGroundCheck(Sim);
	const bool bContact = bCheck && St.GroundHeight <= 0.0;
	if (bContact)
	{
		St.Position -= St.GroundHeight * St.GroundNormal;
		St.GroundHeight = 0.0;
		const double Vn = FVector::DotProduct(V, St.GroundNormal);
		if (Vn < 0.0) { V -= (1.0 + SSXWipeOut::EntryRestitution) * Vn * St.GroundNormal; }
		if ((float(X.PhaseTime) == 0.0f && Anim == 0x2DF) || Anim == 0x2DE || Anim == 0x2E0)
		{
			PlayTumbleAnim(Sim, X);
		}
	}
	St.Velocity = V;
	SSXWipeOut::TumbleBookkeeping(X.PhaseTime, X.AirTime, bContact, bCheck, Dt);
	if (bCheck && SSXWipeOut::SettlePhaseTime < float(X.PhaseTime) && !WO::Bail(St.SurfaceId) && SSXWipeOut::SettleSteepNz < float(St.GroundNormal.Z))
	{
		++St.SlideSoundCount;
		PlayAnim(Sim, X, FVector::DotProduct(St.BasisForward, St.GroundNormal) < 0.0 ? 0x2DD : 0x2DC);
		X.SettleFrame = St.Frame;
	}
	if (WO::PhaseTimeout <= float(X.PhaseTime) || WO::TotalTimeout < float(X.TotalTime))
	{
		if (St.FinishResult < 0)
		{
			St.StuckAccumA = WO::StuckTimeout;
			return;
		}
		PlayAnim(Sim, X, FVector::DotProduct(St.BasisForward, St.GroundNormal) > 0.0 ? 0x2DC : 0x2DD);
		BeginRecover(Sim);
	}
}

void FWipeOutMotion::Slide(FRiderSim& Sim, int32 Anim)
{
	// 0x10C2F0
	FRiderState& St = Sim.State;
	FWipeOutContext& X = *Ctx;
	const float Dt = float(St.TimeScale) * WO::FixedStep;
	X.PhaseTime = SSXEE::FpuAdd(float(X.PhaseTime), Dt);
	const FVector N = St.GroundNormal;
	const float S0 = float(Sim.Surface(St.SurfaceId).Gravity());
	const float G = float(N.Z) * S0;
	const FVector Acc(float(N.X) * G, float(N.Y) * G, G - S0);
	St.Position = RoundF(St.Position + St.Velocity * double(WO::FixedStep));
	St.Velocity = RoundF(St.Velocity + Acc * double(WO::FixedStep));
	St.Velocity = RoundF(St.Velocity * double(WO::SlideDamping));
	const float E = FMath::Clamp(float(St.GroundHeight) + float(St.SurfaceP7), -WO::SnapLimit, WO::SnapLimit);
	St.Position = RoundF(St.Position - double(E) * N);
	St.GroundHeight = RoundF(float(St.GroundHeight) - E);
	St.Velocity = RoundF(St.Velocity - FVector::DotProduct(St.Velocity, N) * N);
	const bool bCheck = TumbleGroundCheck(Sim);   // 0x10FD58
	if (!bCheck || float(St.GroundNormal.Z) < WO::SlideBreakNz || WO::Bail(St.SurfaceId))
	{
		// 0x10DAD8
		PlayAnim(Sim, X, 0x2DE);
		X.Info.Point = St.Position;
		X.Info.Direction = St.GroundNormal;
		X.Info.Normal = St.GroundNormal;
		X.Info.Speed = St.Velocity.Size();
		EnterBody(Sim);
	}
	// body alignment: a = n x forward (negated face down); rotate by -asin(min(|a|, 0.99999)) / 12 about a / |a|
	FVector A = FVector::CrossProduct(St.GroundNormal, St.BasisForward);
	if (Anim == 0x2DD)
	{
		A = -A;
	}
	const float La = float(A.Size());
	if (WO::AlignEps < La)
	{
		const float Ang = FMath::Asin(FMath::Min(La, WO::AlignClamp));
		RotateAbout(Sim, A / double(La), -Ang * WO::AlignRate);
	}
	if (!X.bTumblePoseSimulated)
	{
		X.LyingAngle -= FMath::Asin(FMath::Min(FMath::Sin(X.LyingAngle), 0.99999)) / 12.0;
	}
	const float Vup = FMath::Min(float(X.PhaseTime) * 694.4444f + 833.3333f, 2222.2222f);
	if (La < WO::LyingSin && float(St.Velocity.Size()) < Vup
		&& WO::LyingFaceDot < FMath::Abs(float(FVector::DotProduct(St.BasisForward, St.GroundNormal))))
	{
		if (Sim.RunGroundProbe())   // 0x128AE8
		{
			BeginRecover(Sim);
			return;
		}
	}
	// 0x125088, 0x126250
	SSXGround::PropCollision(Sim, M::WipeOut, X.Anim.Get());
	SSXGround::WallCollision(Sim, SSXRider::MaskWipeOut, X.Anim.Get());
}

void FWipeOutMotion::BeginRecover(FRiderSim& Sim)
{
	// 0x10F178
	FRiderState& St = Sim.State;
	FWipeOutContext& X = *Ctx;
	const FVector OldRoot = St.Orientation.RotateVector(PivotOffset(Sim));
	const FVector D = SSXSim::RsqrtNormal((St.PathLookahead - St.Position));
	const float Du = float(FVector::DotProduct(St.BasisUp, D));
	const float Ds = float(FVector::DotProduct(St.BasisSide, D));
	const bool bFaceDown = CurrentAnim(Sim, X) == 0x2DD;
	const bool bRegularTurn = Sim.Profile.Stance == 0 || St.BoardClass != 2;
	auto TurnUp = [&](float Angle) { RotateAbout(Sim, St.BasisUp, Angle); };
	auto SideStance = [&]()
	{
		if (bRegularTurn) { TurnUp(-WO::HalfPi); St.bStanceGoofy = true; St.AnimYaw = -double(WO::Pi); }
		else { TurnUp(WO::HalfPi); St.bStanceGoofy = false; St.AnimYaw = 0.0; }
		Sim.LogNote(FString::Printf(TEXT("get-up along the side (0x10F178): +0x1B4 = %d"), St.bStanceGoofy ? 1 : 0));
	};
	int32 Id;
	RotateAbout(Sim, St.BasisSide, bFaceDown ? WO::HalfPi : -WO::HalfPi);
	if (bFaceDown)
	{
		if (WO::GetUpDirCos < Du) { Id = 0x24E; }
		else if (WO::GetUpDirCos < Ds)
		{
			SideStance();
			if (St.BoardClass == 2) { Id = 0x253; }
			else { St.bAnimMirror = true; Id = 0x24F; }
		}
		else if (Du < -WO::GetUpDirCos) { TurnUp(WO::Pi); Id = 0x24D; }
		else { TurnUp(bRegularTurn ? WO::HalfPi : -WO::HalfPi); Id = 0x24F; }
	}
	else
	{
		if (WO::GetUpDirCos < Du) { TurnUp(WO::Pi); Id = 0x24A; }
		else if (WO::GetUpDirCos < Ds)
		{
			SideStance();
			if (St.BoardClass == 2) { Id = 0x252; }
			else { St.bAnimMirror = true; Id = 0x24C; }
		}
		else if (Du < -WO::GetUpDirCos) { Id = 0x24B; }
		else { TurnUp(bRegularTurn ? WO::HalfPi : -WO::HalfPi); Id = 0x24C; }
	}
	St.bRailAnimPose = false;
	X.GetUpAnim = Id;
	if (X.Anim) { X.Anim->GetUpAnim = Id; }
	PlayAnim(Sim, X, Id);
	FVector NewRoot;
	if (AnimRootOffset(Sim, Id, NewRoot))
	{
		St.Position = RoundF(St.Position + (OldRoot - St.Orientation.RotateVector(NewRoot)));
	}
	Sim.ApplyTransition(TEXT("T63"));
}

void FWipeOutMotion::Recover(FRiderSim& Sim)
{
	// 0x1102C8
	FRiderState& St = Sim.State;
	const FVector N0 = St.GroundNormal;
	const float S0 = float(Sim.Surface(St.SurfaceId).Gravity());
	const float G = float(N0.Z) * S0;
	St.Position = RoundF(St.Position + St.Velocity * double(WO::FixedStep));
	St.Velocity = RoundF(St.Velocity + FVector(float(N0.X) * G, float(N0.Y) * G, G - S0) * double(WO::FixedStep));
	St.Velocity = RoundF(St.Velocity * double(WO::SlideDamping));
	Sim.RunGroundProbe();   // 0x128AE8
	const float E = FMath::Clamp(float(St.GroundHeight) + float(St.SurfaceP7), -WO::SnapLimit, WO::SnapLimit);
	St.Position = RoundF(St.Position - double(E) * St.GroundNormal);
	St.GroundHeight = RoundF(float(St.GroundHeight) - E);
	const FVector N = St.GroundNormal;
	St.Velocity = RoundF(St.Velocity - FVector::DotProduct(St.Velocity, N) * N);
	// 0x2508B8
	auto Arc = [](const FVector& Av, const FVector& Bv, FQuat& InOut)
	{
		const FVector Ax = FVector::CrossProduct(Av, Bv);
		const float S = float(Ax.Size());
		if (S == 0.0f) { return; }
		float Ang = FMath::Asin(FMath::Min(S, 0.99999f));
		if (FVector::DotProduct(Av, Bv) < 0.0) { Ang = WO::Pi - Ang; }
		float Sn, Cs;
		FMath::SinCos(&Sn, &Cs, Ang * 0.5f);
		const FVector U = Ax / double(S) * double(Sn);
		InOut = FQuat(U.X, U.Y, U.Z, Cs) * InOut;   // 0x102E80
	};
	FQuat Target = FQuat::Identity;
	Arc(Z, N, Target);
	FVector Dl = St.PathLookahead - St.Position;
	Dl -= FVector::DotProduct(Dl, N) * N;
	Dl = Dl.SizeSquared() > 0.0 ? Dl.GetUnsafeNormal() : Dl;
	Arc(Target.RotateVector(FVector(0, 1, 0)), Dl, Target);
	// 0x250A48
	FQuat Qa = St.Orientation, Qb = Target;
	float Dot = float(Qa.X * Qb.X + Qa.Y * Qb.Y + Qa.Z * Qb.Z + Qa.W * Qb.W);
	if (Dot < 0.0f) { Qb = FQuat(-Qb.X, -Qb.Y, -Qb.Z, -Qb.W); Dot = -Dot; }
	float Wa, Wb;
	if (0.001f < 1.0f - Dot)
	{
		const float Om = WO::HalfPi - FMath::Asin(Dot);
		const float So = FMath::Sin(Om);
		Wa = FMath::Sin((1.0f - WO::RecoverSlerp) * Om) / So;
		Wb = FMath::Sin(WO::RecoverSlerp * Om) / So;
	}
	else
	{
		Wa = 1.0f - WO::RecoverSlerp;
		Wb = WO::RecoverSlerp;
	}
	St.Orientation = FQuat(Wa * Qa.X + Wb * Qb.X, Wa * Qa.Y + Wb * Qb.Y, Wa * Qa.Z + Wb * Qb.Z, Wa * Qa.W + Wb * Qb.W);
	St.Orientation = RoundF(St.Orientation.GetNormalized());   // 0x129008
	Sim.UpdateBasis();
	// 0x125088, 0x126250
	SSXGround::PropCollision(Sim, M::WipeOutRecover, Ctx->Anim.Get());
	SSXGround::WallCollision(Sim, SSXRider::MaskWipeOut, Ctx->Anim.Get());
}

void FWipeOutRecoverControl::Update(FRiderSim& Sim, const FRiderInputWord& In)
{
	// 0x108198
	FRiderState& St = Sim.State;
	FWipeOutContext& X = *Ctx;
	const int32 N = X.AnimFrames > 0 ? X.AnimFrames : AnimFrames(Sim, X, X.GetUpAnim);
	const int32 Need = SSXAnim::ControlFrames(N > 0 ? N : 35);
	if (St.Frame - X.AnimPlayFrame >= Need)
	{
		const bool bFakie = X.GetUpAnim == 0x252 || X.GetUpAnim == 0x253;
		Sim.ApplyTransition(bFakie ? TEXT("T65") : TEXT("T64"));
	}
}
}
