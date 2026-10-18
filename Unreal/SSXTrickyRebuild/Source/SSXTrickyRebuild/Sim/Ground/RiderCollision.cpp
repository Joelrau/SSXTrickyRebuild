#include "Sim/Ground/RiderCollision.h"

#include "Race/GameRng.h"
#include "Sim/Air/AirMath.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/WallCollision.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderPose.h"

namespace SSXRiderHit
{
namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;

	FVector Unit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}

	FVector Horizontal(const FVector& V)
	{
		return Unit(FVector(V.X, V.Y, 0.0), FVector::ZeroVector);
	}

	SSXAnim::FRiderAnimChannel* ChannelOf(const FRiderSim& Sim)
	{
		return SSXAnim::FindChannel(Sim);
	}
}

double PairMass(const FRiderSim& Sim)
{
	// 0x123FC8, 0x11D7B0
	return IsImmovable(Sim) ? double(1e30f) : SSXGround::RiderCollisionMass(Sim.Profile, Sim.State.BoostStrength);
}

bool IsImmovable(const FRiderSim& Sim)
{
	// 0x11D998, 0x1065C8
	return Sim.State.FinishResult >= 0 && Sim.State.Control == C::FinishLine && Sim.State.bFinishLineDone;
}

bool RiderShape(const FRiderSim& Sim, FRiderSphereSet& Out)
{
	const FRiderState& St = Sim.State;
	return SSXRider::BuildSphereSet(Sim.Profile, St.bStanceGoofy, St.Position, SSXAir::GetRenderedOrientation(St), SSXRider::MaskAll, Out);
}

bool ContactShape(const FRiderSim& Sim, FRiderSphereSet& Out)
{
	// 0x123FC8
	return SSXRider::BuildPostAISphereSet(Sim.Profile, Sim.State, SSXRider::MaskAll, Out);
}

bool SphereContact(const FRiderSphereSet& A, const FRiderSphereSet& B, FVector& OutD, FVector& OutPoint)
{
	// 0x236A00
	using namespace SSXEE;
	if (A.Count == 0 || B.Count == 0)
	{
		return false;
	}
	const FVector3f Db = Sub(F(B.BoundCentre), F(A.BoundCentre));
	const float Rb = FpuAdd(float(B.BoundRadius), float(A.BoundRadius));
	if (!(Dot(Db, Db) < Rb * Rb))
	{
		return false;
	}
	for (int32 I = 0; I < A.Count; ++I)
	{
		const FVector3f Ca = F(A.Centres[I]);
		for (int32 J = 0; J < B.Count; ++J)
		{
			const FVector3f Dv = Sub(F(B.Centres[J]), Ca);
			const float R = FpuAdd(float(B.Radii[J]), float(A.Radii[I]));
			const float D2 = Dot(Dv, Dv);
			if (D2 < R * R)
			{
				const FVector3f U = Normalize(Dv);
				const float E = FpuSub(R, float(FMath::Sqrt(double(D2))));
				OutD = D(Scale(E, U));
				OutPoint = D(Sub(Ca, Scale(FpuSub(float(A.Radii[I]), E), U)));
				return true;
			}
		}
	}
	return false;
}

bool HeadingWithin(double Yaw, double Bearing)
{
	// 0x124920
	using SSXEE::FpuAdd; using SSXEE::FpuSub;
	const float Diff = FpuSub(float(Yaw), float(Bearing));
	const float T = FpuAdd(Diff * 0.15915494f, 0.5f);
	float K = float(int32(T));
	if (T < K) { K = FpuSub(K, 1.0f); }
	return FMath::Abs(FpuSub(Diff, K * 6.2831855f)) < 0.7853982f;
}

FVector HitPoint(const FRiderState& Target, const FVector& U, double RivalDist)
{
	// 0x124920
	return SSXEE::D(SSXEE::Add(SSXEE::F(Target.Position), SSXEE::Scale(float(RivalDist), SSXEE::F(U))));
}

EHit ApplyRiderHit(FRiderSim& Target, double J, const FVector& InU, const FVector& ContactPoint, bool bMode0, bool bHeadingOk)
{
	// 0x124920
	using namespace SSXEE;
	FRiderState& St = Target.State;
	const float Jf = float(J);
	float Jc = Jf;
	if (Jf < 0.0f) { if (!(-500.0f < Jf)) { Jc = -500.0f; } }
	else if (!(Jf < 500.0f)) { Jc = 500.0f; }
	const FVector3f U0 = F(InU), N = F(St.GroundNormal);
	const float Along = Dot(U0, N) * Jc;
	if (1000.0f < FMath::Abs(Jf) && St.Motion == M::Ground && bHeadingOk && 0.0f < Along)
	{
		St.LaunchCharge = 1.0;
		Target.ApplyTransition(TEXT("T66"));
		return EHit::Launch;
	}
	// 0x124BA8
	FVector3f U = U0;
	if (St.Motion != M::Air)
	{
		U = Normalize(Sub(U0, Scale(Dot(U0, N), N)));
	}
	St.Velocity = D(Add(F(St.Velocity), Scale(Jc, U)));
	St.AddPadFeedback(double(Jc * 0.0012f));
	if (St.Motion == M::WipeOut || St.Motion == M::WipeOutRecover)
	{
		return Jc != 0.0f ? EHit::Push : EHit::None;
	}
	const float Mag = (St.Motion == M::Air && 0.0f < Along) ? 0.0f : FMath::Abs(Jf);
	const float Ko = bMode0 ? 530.50287f : 733.9661f;
	if (Ko < Mag || St.Motion == M::RailSlide)
	{
		// 0x11D830
		FRiderImpact Info;
		Info.Point = ContactPoint;
		Info.Direction = D(Normalize(F(St.Velocity)));
		Info.Normal = InU;
		Info.Speed = Jc;
		const SSXAir::FAirContext* Air = SSXAir::GetAirContext(Target);
		const bool bPending = Air && Air->Score.Value > 0.0;
		// 0x11E988, 0x156DE0
		Target.StartWipeOut(Info, TEXT("T60 rider hit"), bPending);
		return EHit::Knockdown;
	}
	if (167.31738f < Mag)
	{
		// 0x124DD0
		SSXGround::BumpReaction(Target, ChannelOf(Target), InU);
		return EHit::Bump;
	}
	return Jc != 0.0f ? EHit::Push : EHit::None;
}

FPairResult BodyContact(FRiderSim& A, FRiderSim& B, bool bImpulseAllowed, bool bMode0, bool bHeadingOkA, bool bHeadingOkB,
                        double DistA, double DistB)
{
	// 0x123FC8, 0x236A00
	using namespace SSXEE;
	FPairResult R;
	FRiderSphereSet Sa, Sb;
	if (!ContactShape(A, Sa) || !ContactShape(B, Sb) || !SphereContact(Sa, Sb, R.D, R.Point))
	{
		return R;
	}
	R.bContact = true;
	const bool bFixA = IsImmovable(A), bFixB = IsImmovable(B);
	FVector3f Dv = F(R.D);
	// 0x1267A0
	if (!bFixA && !bFixB)
	{
		Dv = Scale(0.55f, Dv);
	}
	if (!bFixA)
	{
		A.State.Position = D(Sub(F(A.State.Position), Dv));
		A.State.SphereFrameP -= D(Dv);
		A.NotifySphereSetMoved(D(Scale(-1.0f, Dv)));   // 0x102F50
	}
	if (!bFixB)
	{
		B.State.Position = D(Add(F(B.State.Position), Dv));
		B.State.SphereFrameP += D(Dv);
		B.NotifySphereSetMoved(D(Dv));
	}
	R.D = D(Dv);
	if (bImpulseAllowed)
	{
		Exchange(A, B, D(Normalize(Dv)), R.Point, bMode0, bHeadingOkA, bHeadingOkB, R, DistA, DistB);
	}
	return R;
}

void Exchange(FRiderSim& A, FRiderSim& B, const FVector& InU, const FVector& Point, bool bMode0, bool bHeadingOkA, bool bHeadingOkB,
              FPairResult& R, double DistA, double DistB)
{
	// 0x124920
	using namespace SSXEE;
	const bool bFixA = IsImmovable(A), bFixB = IsImmovable(B);
	const float Ma = float(PairMass(A)), Mb = float(PairMass(B));
	if (!(FpuAdd(Ma, Mb) > 0.0f))
	{
		return;
	}
	const FVector3f U = F(InU);
	const float K = FpuSub(Dot(F(B.State.Velocity), U), Dot(F(A.State.Velocity), U)) / FpuAdd(Ma, Mb);
	R.bImpulse = true;
	R.JA = Mb * K;
	R.JB = -Ma * K;
	R.HitA = bFixA ? EHit::None : ApplyRiderHit(A, R.JA, InU, DistA >= 0.0 ? HitPoint(A.State, InU, DistA) : Point, bMode0, bHeadingOkA);
	R.HitB = bFixB ? EHit::None : ApplyRiderHit(B, R.JB, InU, DistB >= 0.0 ? HitPoint(B.State, InU, DistB) : Point, bMode0, bHeadingOkB);
}

bool ShoveInRange(const FRiderState& A, const FRiderState& B)
{
	// 0x123FC8, 0x2510F8
	using namespace SSXEE;
	// without a cPostAI yet (a headless rider never stepped): the positions and the rendered orientation's axes
	auto Rows = [](const FRiderState& S, FVector3f Out[4])
	{
		if (S.bRenderRowsValid)
		{
			Out[0] = F(S.RenderRow0); Out[1] = F(S.RenderRow1); Out[2] = F(S.RenderRow2); Out[3] = F(S.RenderRow3);
			return;
		}
		const FQuat Q = SSXAir::GetRenderedOrientation(S);
		Out[0] = F(Q.RotateVector(FVector(1, 0, 0))); Out[1] = F(Q.RotateVector(FVector(0, 1, 0))); Out[2] = F(Q.RotateVector(FVector(0, 0, 1)));
		Out[3] = F(S.Position);
	};
	FVector3f Ra[4], Rb[4];
	Rows(A, Ra);
	Rows(B, Rb);
	const FVector3f Dv = Sub(Rb[3], Ra[3]);
	const FVector3f Up = Ra[2];
	const float Dz = Dot(Dv, Up);
	if (150.0f < FMath::Abs(Dz) || 180.0f < Length(Sub(Dv, Scale(Dz, Up))))
	{
		return false;
	}
	const float X = Dot(Dv, Ra[0]), Y = Dot(Dv, Ra[1]);
	float Bang;
	if (Y == 0.0f)
	{
		Bang = X == 0.0f ? -X : (0.0f <= X ? -1.5707964f : 1.5707964f);
	}
	else
	{
		const float At = SSXAnim::AtanEE(X / Y);
		Bang = Y < 0.0f ? -(0.0f < X ? FpuAdd(At, 3.1415927f) : FpuSub(At, 3.1415927f)) : -At;
	}
	const float Diff = FpuSub(Bang, float(A.ShoveAngle));
	const float T = FpuAdd(Diff * 0.15915494f, 0.5f);
	float K = float(int32(T));
	if (T < K) { K = FpuSub(K, 1.0f); }
	return FMath::Abs(FpuSub(Diff, K * 6.2831855f)) < 0.9599311f;
}

double ShoveImpulse(const FRiderSim& A, const FRiderSim& B, bool bMode0, FVector& OutU)
{
	using namespace SSXEE;
	const FVector3f U = Normalize(Sub(F(B.State.Position), F(A.State.Position)));
	OutU = D(U);
	// 0x11D7B0
	const float Ma = float(SSXGround::RiderCollisionMass(A.Profile, A.State.BoostStrength)), Mb = float(SSXGround::RiderCollisionMass(B.Profile, B.State.BoostStrength));
	const float Rel = Dot(Scale(6.60164976f, U), Sub(F(B.State.Velocity), F(A.State.Velocity)));
	const float J = FpuAdd(bMode0 ? 250.59537f : 108.58082f, (Rel * Ma) / FpuAdd(Ma, Mb));
	const float Scl = FpuAdd((float(A.Profile.StatBytes[0x12 - 0x0E]) * 0.739932f) * 0.003921569f, 0.54663557f);
	return double(J * Scl);
}

EHit ShoveHit(FRiderSim& A, FRiderSim& B, bool bMode0, bool bHeadingOk, double* OutJ, double DistB)
{
	if (OutJ) { *OutJ = 0.0; }
	if (!A.State.bShoveActive || !ShoveInRange(A.State, B.State))
	{
		return EHit::None;
	}
	FVector U;
	const double J = ShoveImpulse(A, B, bMode0, U);
	if (OutJ) { *OutJ = J; }
	if (!(J > 0.0))
	{
		return EHit::None;
	}
	const EHit H = ApplyRiderHit(B, J, U, DistB >= 0.0 ? HitPoint(B.State, U, DistB) : B.State.Position, bMode0, bHeadingOk);
	return H == EHit::None ? EHit::Push : H;
}

void ShoveWindow(int32 Anim, int32 BoardClass, int32& OutOpen, int32& OutClose)
{
	static const int32 Table[4][3][2] = {
		{ { 11, 29 }, { 13, 27 }, { 13, 29 } },
		{ { 13, 29 }, { 13, 29 }, { 13, 29 } },
		{ { 15, 25 }, { 13, 29 }, { 13, 27 } },
		{ { 13, 29 }, { 13, INDEX_NONE }, { 13, 29 } },    // 0x208 R_COMBATBWD1 (fr: flag 2 never sets)
	};
	const int32 K = FMath::Clamp(Anim - SSXAnim::ShovePushTS, 0, 3);
	const int32 C = FMath::Clamp(BoardClass, 0, 2);
	OutOpen = Table[K][C][0];
	OutClose = Table[K][C][1];
}

void ShoveWindowOfClip(const SSXAnim::FAflClip& Clip, int32& OutOpen, int32& OutClose)
{
	// 0x15A030
	auto FlagUpdate = [](int32 M) { return M >= 0 ? 2 * M + 1 : INDEX_NONE; };
	OutOpen = FlagUpdate(Clip.Markers[0]);
	OutClose = FlagUpdate(Clip.Markers[1]);
	if (OutOpen == INDEX_NONE)
	{
		OutOpen = MAX_int32;   // never opens
	}
}

void TickShove(FRiderSim& Sim)
{
	FRiderState& St = Sim.State;
	if (St.ShoveAnim == 0)
	{
		return;
	}
	if (--St.ShoveFramesLeft <= 0)
	{
		St.ShoveAnim = 0;
		St.ShoveFramesLeft = 0;
		St.bShoveActive = false;
		return;
	}
	++St.ShoveAge;
	St.bShoveActive = St.ShoveAge >= St.ShoveOpen && (St.ShoveClose == INDEX_NONE || St.ShoveAge < St.ShoveClose);
}

bool ShoveInput(FRiderSim& Sim, int32 Side, int32 Fwd)
{
	FRiderState& St = Sim.State;
	if ((Side == 0 && Fwd == 0) || St.ShoveAnim != 0)
	{
		return false;
	}
	const double S = Side == 0 ? 0.0 : ((Side == 3 || Side == -1) ? -1.0 : 1.0);
	const double F = Fwd == 0 ? 0.0 : ((Fwd == 3 || Fwd == -1) ? -1.0 : 1.0);
	const double A = FMath::Atan2(S, F);
	int32 Anim;
	if (FMath::Abs(A) < FMath::DegreesToRadians(30.0))
	{
		Anim = SSXAnim::ShoveCombatFwd;
	}
	else if (FMath::Abs(A) > FMath::DegreesToRadians(150.0))
	{
		Anim = SSXAnim::ShoveCombatBwd;
	}
	else
	{
		Anim = ((S < 0.0) != St.bStanceGoofy) ? SSXAnim::ShovePushTS : SSXAnim::ShoveElbowHS;
	}
	const FVector Axis = Horizontal(-FMath::Sin(A) * St.BasisSide + FMath::Cos(A) * St.BasisForward);
	// 0x15ADA8
	SSXAnim::FRiderAnimChannel* Ch = ChannelOf(Sim);
	int32 Variant = 0;
	if (Ch)
	{
		Variant = Ch->RequestPlay(Anim);
	}
	else if (const int32 Count = SSXAnim::RandomVariantCount(Anim); Count > 0)
	{
		Variant = int32(FGameRng::StreamB().Next() % uint32(Count));
	}
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	const SSXAnim::FAflClip* Clip = Lib ? Lib->Clip(Lib->Resolve(Anim, St.BoardClass, Sim.Profile.GetTrickCharacter(), Variant)) : nullptr;
	int32 N = Clip ? Clip->NFrames : (Ch ? Ch->Frames(Sim, Anim) : 0);
	if (N <= 0)
	{
		N = SSXAnim::DefaultOneShotFrames(Anim, St.BoardClass);
	}
	St.ShoveAnim = Anim;
	St.ShoveFramesLeft = FMath::Max(1, SSXAnim::OneShotFrames(N));
	St.ShoveAngle = A;
	St.ShoveAge = 0;
	if (Clip)
	{
		ShoveWindowOfClip(*Clip, St.ShoveOpen, St.ShoveClose);
	}
	else
	{
		ShoveWindow(Anim, St.BoardClass, St.ShoveOpen, St.ShoveClose);
	}
	St.bShoveActive = false;
	St.ShoveAxis = Axis;
	++St.ShoveCount;
	St.Ch1Anim = Anim;
	St.Ch1EndFrame = St.Frame + SSXAnim::ControlFrames(N);
	return true;
}
}
