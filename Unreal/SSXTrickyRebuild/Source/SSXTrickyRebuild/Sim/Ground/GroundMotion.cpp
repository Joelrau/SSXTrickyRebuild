#include "Sim/Ground/GroundMotion.h"

#include "HAL/IConsoleManager.h"
#include "SSXTrickyRebuild.h"

#include "Sim/Ground/RailCore.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/Ground/WallCollision.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Air/AirMath.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/SurfaceTable.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderPose.h"

namespace SSXGround
{
namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;
	const FVector Z = FVector::UpVector;

	FORCEINLINE FVector Unit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}

	SSXRail::FRailBoard BoardOf(const FRiderState& St)
	{
		SSXRail::FRailBoard B;
		if (St.bBoardValid)
		{
			B.R0 = St.BoardRow0;
			B.R1 = St.BoardRow1;
			B.R2 = St.BoardRow2;
			B.Origin = St.BoardOrigin;
		}
		else
		{
			B.R0 = St.BasisForward;
			B.R1 = St.BasisUp;
			B.R2 = St.BasisSide;
			B.Origin = St.Position;
		}
		return B;
	}

	SSXRail::FRailRider RailRiderOf(const FRiderState& St)
	{
		SSXRail::FRailRider R;
		R.Pos = St.Position;
		R.Vel = St.Velocity;
		R.Board = BoardOf(St);
		R.Up = St.BasisUp;
		R.Side = St.BasisSide;
		R.Steer = St.Steer.Cur;
		R.BoostStrength = St.BoostStrength;
		R.TimeScale = St.TimeScale;
		R.BoxFilter = St.RailBox;
		R.GrindType = St.GrindType;
		R.Surface = St.SurfaceId;
		R.RailPoint = St.ContactPoint;
		return R;
	}

	void SetSurface(FRiderState& St, int32 Surface)
	{
		if (St.SurfaceId != Surface)
		{
			St.SurfaceChanged = int32(0x80000001u);
		}
		St.SurfaceId = Surface;
	}
}

FGroundRider MakeGroundRider(const FRiderSim& Sim, const FGroundContext& Ctx)
{
	FGroundRider R;
	const double O = Ctx.Config.StatOverride;
	auto Stat = [&Sim, O](int32 Offset) { return O >= 0.0 ? O : Sim.Profile.Stat(Offset); };
	R.StatSpeed = Stat(0x0E);
	R.StatBrake = Stat(0x11);
	R.StatGrip = Stat(0x13);
	R.StatGate = Stat(0x15);
	R.StatLanding = Stat(0x16);
	R.StatJump = Stat(0x17);
	R.StatAero = Stat(0x19);
	auto Byte = [&Stat](int32 Offset) { return float(Stat(Offset) * 255.0); };
	R.Byte0E = Byte(0x0E);
	R.Byte11 = Byte(0x11);
	R.Byte13 = Byte(0x13);
	R.Byte19 = Byte(0x19);
	R.BoardClass = FMath::Clamp(Sim.State.BoardClass, 0, 2);
	R.bRidingSwitch = Sim.State.bStanceGoofy != (Sim.Profile.Stance != 0);
	return R;
}

void NaturalTakeOff(FRiderSim& Sim, FGroundContext& Ctx)
{
	FRiderState& St = Sim.State;
	const bool bWasJump = St.Control == C::Jump;
	if (!bWasJump)
	{
		St.LaunchCharge = St.Crouch.Cur;
	}
	Ctx.LastLaunchImpulse = ApplyLaunch(Sim, 0.0, MakeGroundRider(Sim, Ctx).StatJump);
	if (bWasJump)
	{
		Sim.SetMotion(M::Air, TEXT("T28"));
		Sim.ApplyTransition(TEXT("T26"));
	}
	else
	{
		Sim.ApplyTransition(TEXT("T28"));
	}
}

#if !UE_BUILD_SHIPPING   // diagnostic log, compiled out of the release build
static TAutoConsoleVariable<int32> CVarRailLog(TEXT("ssx.Rail.Log"), 1,
	TEXT("Rail catch diagnostics (LogSSX \"[rail]\" lines, players with the transition log only): 1 = every Air frame near a ")
	TEXT("rail (|R1.c| < 150, |R2.c| < 150, |R0.c| < 300; at most 24 lines per second), each catch, and a prop wall bounce in ")
	TEXT("Air / RailSlide; 2 = also every Ground frame near a rail; 0 = off."));

namespace
{
	FString RailCatchLine(const FRiderSim& Sim, const SSXRail::FCatchDiag& D, const SSXAnim::FRiderAnimChannel* Ch)
	{
		const FRiderState& St = Sim.State;
		FString Clip = TEXT("ch0 none");
		if (Ch && Ch->Trick.IsActive())
		{
			Clip = FString::Printf(TEXT("ch0 0x%X cat %d fl4 %d frame %.1f/%d hold %.1f"), Ch->Trick.Data.AnimId, Ch->Trick.Category(),
				Ch->Trick.Flag(4) ? 1 : 0, Ch->Trick.Frame, Ch->Trick.Data.NFrames, Ch->Trick.Hold);
		}
		const uint32 Slots = uint32(Sim.DebugHeldActions & 0x3FFFFFFull);
		return FString::Printf(TEXT("[rail] %s f%d %s/%s rail %d c (%+.1f %+.1f %+.1f) | < (%.1f %.1f %.1f) c.v %+.2f box %.3f trick %d %s ")
			TEXT("pad %08x slots %07x tweak %d -> %s"),
			*Sim.DebugName, St.Frame, LexToString(St.Motion), LexToString(St.Control), D.Spline, D.C0, D.C1, D.C2, D.L0, D.L1, D.L2, D.Cv,
			D.Box, St.TrickId, *Clip, Sim.DebugPadButtons, Slots, int32((Sim.DebugHeldActions >> 32) & 1ull), SSXRail::LexCatchVerdict(D.Verdict));
	}

	void LogRailCatch(FRiderSim& Sim, const SSXRail::FCatchDiag& D, const SSXAnim::FRiderAnimChannel* Ch)
	{
		const int32 Mode = CVarRailLog.GetValueOnAnyThread();
		const FRiderState& St = Sim.State;
		if (Mode <= 0 || !Sim.bLogTransitions || D.Spline == INDEX_NONE || (St.Motion != M::Air && Mode < 2))
		{
			return;
		}
		const bool bCaught = D.Verdict == SSXRail::ECatchVerdict::Caught;
		const bool bNear = FMath::Abs(D.C1) < 150.f && FMath::Abs(D.C2) < 150.f && FMath::Abs(D.C0) < 300.f;
		if (!bCaught && !bNear)
		{
			return;
		}
		if (St.Frame - Sim.RailLogWindowFrame >= 60 || St.Frame < Sim.RailLogWindowFrame)
		{
			Sim.RailLogWindowFrame = St.Frame;
			Sim.RailLogLines = 0;
		}
		Sim.RailLogCatch = RailCatchLine(Sim, D, Ch);
		Sim.RailLogCatchFrame = St.Frame;
		if (bCaught || Sim.RailLogLines < 24)
		{
			++Sim.RailLogLines;
			UE_LOG(LogSSX, Log, TEXT("%s"), *Sim.RailLogCatch);
		}
	}
}

void LogRailPropBounce(FRiderSim& Sim, ERiderMotion Before, int32 Instance, const FVector& Normal, double Dv, double Approach)
{
	if (CVarRailLog.GetValueOnAnyThread() <= 0 || !Sim.bLogTransitions || (Before != M::Air && Before != M::RailSlide))
	{
		return;
	}
	const FRiderState& St = Sim.State;
	UE_LOG(LogSSX, Log, TEXT("[rail] %s f%d prop wall bounce from %s: instance %d n (%.2f %.2f %.2f) dv %.0f approach %.0f v (%.0f %.0f %.0f); this update's catch: %s"),
		*Sim.DebugName, St.Frame, LexToString(Before), Instance, Normal.X, Normal.Y, Normal.Z, Dv, Approach, St.Velocity.X, St.Velocity.Y,
		St.Velocity.Z, Sim.RailLogCatchFrame == St.Frame ? *Sim.RailLogCatch : TEXT("(no rail within range)"));
}
#else
namespace
{
	void LogRailCatch(FRiderSim&, const SSXRail::FCatchDiag&, const SSXAnim::FRiderAnimChannel*) {}
}

void LogRailPropBounce(FRiderSim&, ERiderMotion, int32, const FVector&, double, double) {}
#endif

bool TryRailAttach(FRiderSim& Sim)
{
	// 0x125CB8
	FRiderState& St = Sim.State;
	const IRiderWorld* World = Sim.GetWorld();
	if (!World)
	{
		return false;
	}
	bool bTrickBlocks = false;
	if (const SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim))
	{
		const int32 Cat = Ch->Trick.Category();
		bTrickBlocks = Ch->Trick.IsActive() && (Cat == SSXAnim::CategoryGrab || Cat == SSXAnim::CategoryTweak
			|| Cat == SSXAnim::CategoryUber) && !Ch->Trick.Flag(4);
	}
	SSXRail::FRailRider R = RailRiderOf(St);
	SSXRail::FCatchDiag Diag;
	const bool bCaught = SSXRail::CatchTest(R, *World, bTrickBlocks, &Diag);
	LogRailCatch(Sim, Diag, SSXAnim::FindChannel(Sim));
	if (!bCaught)
	{
		return false;
	}
	St.Velocity = R.Vel;
	St.GrindType = R.GrindType;
	SetSurface(St, R.Surface);
	// 0x343050, 0x1261F0, 0x21E670, 0x1569B0, 0x11B018
	if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		const bool bFromAir = St.Motion == M::Air;
		if (bFromAir)
		{
			Air->Score.AirFrames = Air->AirFrames;
		}
		// 0x108378
		const double Reward = Air->Score.Land(Air->Score.SegmentType != 0, St.GrindType, St.bStanceGoofy, St.FinishResult >= 0);
		SSXAir::BoostAdd(St, Reward, SSXAir::BoostGain(Sim.Profile));
		if (Air->Score.UberCount > SSXAir::InfiniteBoostUbers)
		{
			St.bInfiniteBoost = true;
		}
		Air->Score.RailFrame(St.Velocity.Size());   // 0x156E80
	}
	Sim.ApplyTransition(TEXT("T29"));
	return true;
}

namespace
{
	struct FGroundKeyframe
	{
		FGroundContext Ctx;
		bool bAnim = false;
		SSXAnim::FRiderAnimChannel Anim;
	};
}

TSharedPtr<FRiderStateBlob> FGroundMotion::SaveState() const
{
	FGroundKeyframe K;
	K.Ctx = *Ctx;
	K.bAnim = Ctx->Anim.IsValid();
	if (K.bAnim) { K.Anim = *Ctx->Anim; }
	return SSXRiderBlob::Save(K);
}

void FGroundMotion::LoadState(const FRiderStateBlob& Blob)
{
	FGroundKeyframe K;
	SSXRiderBlob::Load(Blob, K);
	const TSharedPtr<SSXAnim::FRiderAnimChannel> Keep = Ctx->Anim;
	*Ctx = K.Ctx;
	Ctx->Anim = Keep;
	if (Keep && K.bAnim) { *Keep = K.Anim; }
}

void FGroundMotion::Enter(FRiderSim& Sim, ERiderMotion Previous)
{
	// 0x109858
	Sim.State.GrindType = 0;
}

void FGroundMotion::Update(FRiderSim& Sim)
{
	FRiderState& St = Sim.State;
	FGroundContext& X = *Ctx;

	// 0x10A0D8
	const FSurfaceParams& S = Sim.Surface();
	const FVector N = St.GroundNormal, F = St.ContactForward, L = St.ContactLeft;
	const FVector3f Nf = SSXEE::F(N), Ff = SSXEE::F(F), Lf = SSXEE::F(L);
	FVector3f Pf = SSXEE::F(St.Position), Vf3 = SSXEE::F(St.Velocity);
	const float VnF = SSXEE::Dot(Vf3, Nf);
	const float VfF = SSXEE::Dot(Vf3, Ff);
	const float VsF = SSXEE::Dot(Vf3, Lf);
	const float DtF = float(St.TimeScale) * 0.016666668f;
	const float SteerF = float(St.Steer.Cur);
	const double Steer = St.Steer.Cur, Crouch = St.Crouch.Cur, Brake = St.Brake.Cur;
	const FGroundRider R = MakeGroundRider(Sim, X);
	const float G = S.F[0];

	// 0x109878, 0x109950, 0x109CB8, 0x109EF8
	float Support = NormalSupport(float(St.GroundHeight), VnF, S, float(St.SurfaceP7), float(St.SurfaceP8));
	const float W = X.Config.bUsePathHeading ? PathAlignment(float(St.Yaw), float(St.PathHeading)) : 1.0f;
	const float Prop = Propulsion(R, SSXEE::Length(Vf3), SteerF, float(Brake), float(St.BoostStrength),
	                              float(St.BoostBurstTimer), W, Ff.Z, X.bPushAnim, S);
	const float Drag = LongitudinalDrag(R, VfF, (float(K::DragLoadK) * Support) / G, float(Crouch), float(Brake),
	                                    float(St.BoostStrength), St.SurfaceId, float(St.GroundHeight), float(St.SurfaceP8), S);
	const float Grip = LateralGrip(R, VsF, VfF, SteerF, float(St.BoostStrength), S);
	if (Nf.Z < 0.0f && Support < 0.0f)
	{
		Support = 0.0f;
	}
	St.GroundDragAccel = Drag;

	const float Theta = (S.F[5] * 0.017453292f) * SteerF;
	St.EdgeAngle = Theta;

	if (Brake == 0.0 && VfF < -111.11111f
		&& (St.Control == C::Cruise || St.Control == C::FakieCruise || St.Control == C::LandNormal))
	{
		Sim.SetControl(C::ChangeFakie, TEXT("T27"));
	}

	// 0x250C10, 0x102F50
	float SinT, CosT;
	SSXAnim::SinCosEE(Theta, SinT, CosT);
	const float G2 = G + G;
	const float MOverC = (Support <= G2 ? Support : G2) / CosT;
	const FVector3f Tilted = SSXEE::Add(SSXEE::Scale(CosT, Nf), SSXEE::Scale(SinT, Lf));
	FVector3f Af = SSXEE::Add(SSXEE::Scale(SSXEE::FpuSub(Support, MOverC), Nf), SSXEE::Scale(MOverC, Tilted));
	Af = SSXEE::Add(Af, SSXEE::Scale(SSXEE::FpuAdd(Drag, Prop), Ff));
	Af = SSXEE::Add(Af, SSXEE::Scale(Grip, Lf));
	Af = SSXEE::Add(Af, FVector3f(0.0f, 0.0f, -G));

	const float D = FMath::Max(SSXEE::FpuAdd(float(St.SurfaceP8), float(St.GroundHeight)), float(K::DepthFloor));
	if (D < 0.0f)
	{
		Pf = SSXEE::Sub(Pf, SSXEE::Scale(D, Nf));
		St.GroundHeight = SSXEE::FpuSub(float(St.GroundHeight), D);
		Vf3 = SSXEE::Sub(Vf3, SSXEE::Scale(VnF, Nf));
		const float An = SSXEE::Dot(Af, Nf);
		if (An < 0.0f)
		{
			Af = SSXEE::Sub(Af, SSXEE::Scale(An, Nf));
		}
	}
	const FVector OldPos = SSXEE::D(Pf);
	St.Position = SSXEE::D(SSXEE::Add(Pf, SSXEE::Scale(DtF, Vf3)));
	St.Velocity = SSXEE::D(SSXEE::Add(Vf3, SSXEE::Scale(DtF, Af)));

	{
		const FVector3f Uf = SSXEE::F(St.SmoothedUp);
		const float Before = SSXEE::Dot(Pf, Uf);
		const float After = SSXEE::Dot(SSXEE::F(St.Position), Uf);
		const float Sum = SSXEE::FpuAdd(float(St.SuspensionOffset), SSXEE::FpuSub(Before, After)) * SSXEE::FpuSub(1.0f, DtF * 4.6840196f);
		const float Cr = float(St.Crouch.Cur);
		const float Lo = SSXEE::FpuSub(1.0f, Cr) * (R.BoardClass == 2 ? -10.0f : -20.0f);
		const float Hi = Cr * (R.BoardClass == 2 ? 15.0f : 10.0f);
		float Target = Sum < Lo ? Lo : (Hi < Sum ? Hi : Sum);
		if (St.Control == C::LandHard || St.Control == C::LandNormal)
		{
			Target = 0.0f;
		}
		const float Slew = 1.6666666f;
		St.SuspensionOffset = (SSXEE::FpuAdd(Target, Slew) < Sum) ? SSXEE::FpuSub(Sum, Slew) : (Sum < SSXEE::FpuSub(Target, Slew) ? SSXEE::FpuAdd(Sum, Slew) : Target);
	}

	if (!(St.Control == C::Prewind && Steer == 0.0))
	{
		const float DYaw = YawIncrement(SSXEE::F(St.Velocity), Nf, Ff, SSXEE::F(St.FallLine), SteerF, float(Crouch), R.BoardClass, DtF);
		Sim.RotateBodyEE(Nf, DYaw);   // 0x128E90
	}

	St.bWallHitThisFrame = WallCollision(Sim, SSXRider::MaskGroundWall, X.Anim.Get());
	if (St.Motion != M::Ground)
	{
		return;
	}

	const bool bOnGround = Sim.RunGroundProbe();

	if (!IsDeepSnow(St.SurfaceId))
	{
		const FVector3f Nn = SSXEE::F(St.GroundNormal);
		FVector3f V = SSXEE::F(St.Velocity);
		const float Vn = SSXEE::Dot(V, Nn);
		const float Speed = SSXEE::Length(V);
		V = SSXEE::Sub(V, SSXEE::Scale(Vn * 0.4f, Nn));
		const float Speed2 = SSXEE::Length(V);
		if (Speed2 != 0.0f)
		{
			V = SSXEE::Scale(Speed / Speed2, V);
		}
		St.Velocity = SSXEE::D(V);
	}

	if (!St.bWallHitThisFrame && (!bOnGround || St.GroundHeight > S.StickHeight()))
	{
		NaturalTakeOff(Sim, X);
		return;
	}

	if (IsBailSurface(St.SurfaceId))
	{
		FRiderImpact Impact;
		Impact.Point = St.ContactPoint;
		Impact.Direction = Unit(St.Velocity, St.BasisForward);
		Impact.Normal = St.GroundNormal;
		Impact.Speed = FVector::DotProduct(St.Velocity, St.GroundNormal);
		St.AddPadImpact(St.Velocity, St.GroundNormal);   // 0x10AC28
		St.AddFxImpact(St.Position, St.Velocity, St.GroundNormal);
		Sim.StartWipeOut(Impact, TEXT("T60 bail surface"));
		return;
	}
	if (St.SurfaceId != FRiderSim::OutOfBoundsSurface)
	{
		St.LastGroundPosition = St.Position;
		St.LastGroundHeading = St.ContactForward;
	}

	// 0x250FD0
	{
		const FVector3f Nn = SSXEE::F(St.GroundNormal);
		const FVector3f T = SSXEE::Normalize(SSXEE::Add(Nn, FVector3f(0.0f, 0.0f, Nn.Z * 0.5f)));
		const FVector3f Cr = SSXEE::Cross(T, SSXEE::F(St.BasisUp));
		float Sc = SSXEE::Length(Cr);
		if (1e-05f < Sc)
		{
			const FVector3f Axis = SSXEE::Scale(1.0f / Sc, Cr);
			if (0.99999f < Sc)
			{
				Sc = 0.99999f;
			}
			Sim.RotateBodyEE(Axis, -SSXAnim::AsinEE(Sc) * 0.23333861f);
		}
	}
	{
		const double Kc = FMath::Min(St.Velocity.Size() * K::ChatterSpeed, 1.0) * (FMath::Abs(St.Steer.Cur) + FMath::Abs(St.Brake.Cur) + 0.1);
		X.ChatterPhase = FMath::Fmod(X.ChatterPhase + K::ChatterPhase * Kc, 2.0 * UE_DOUBLE_PI);
		St.BoardChatter = K::ChatterAmp * Kc * FMath::Cos(X.ChatterPhase);
	}
	// 0x125088
	TryRailAttach(Sim);
	PropCollision(Sim, St.Motion, X.Anim.Get(), nullptr, PropMaskForMotion(M::Ground));
}

// 0x10B068, 0x10B090, 0x10B0D0
void FRailSlideMotion::Enter(FRiderSim& Sim, ERiderMotion Previous)
{
	Ctx->RailTime = 0.0;
	Ctx->RailRoll = 0.0;
	Sim.State.RailBox.Target = 0.0;
	Sim.State.RailBox.Rate = SSXRail::FilterRate;
}

void FRailSlideMotion::Exit(FRiderSim& Sim, ERiderMotion Next)
{
	Sim.State.RailBox.Cur = SSXRail::FilterExitValue;
	Sim.State.RailBox.Target = 1.0;
	Sim.State.RailBox.Rate = SSXRail::FilterRate;
}

void FRailSlideMotion::Leave(FRiderSim& Sim)
{
	// 0x156618
	Sim.State.Steer.Reset();
	if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		Air->bSeedsPending = true;
		Air->PendingSeedSpin = Air->PendingSeedFlip = 0.0;
		Air->bPendingLate = false;
	}
	Sim.ApplyTransition(TEXT("T31"));
}

void FRailSlideMotion::Update(FRiderSim& Sim)
{
	FRiderState& St = Sim.State;
	FGroundContext& X = *Ctx;
	const IRiderWorld* World = Sim.GetWorld();
	if (!World)
	{
		Leave(Sim);
		return;
	}
	SSXRail::FRailRider R = RailRiderOf(St);
	R.RailTime = X.RailTime;
	R.Roll = X.RailRoll;
	const SSXRail::ESlideResult Res = SSXRail::SlideStep(R, *World);
	St.Position = R.Pos;
	St.Velocity = R.Vel;
	St.RailBox.Cur = R.BoxFilter.Cur;
	X.RailTime = R.RailTime;
	X.RailRoll = R.Roll;
	X.RailLateral = R.Lateral;
	if (Res == SSXRail::ESlideResult::NoHit)
	{
		Leave(Sim);
		return;
	}
	SetSurface(St, R.Surface);
	St.ContactPoint = R.RailPoint;
	if (Res == SSXRail::ESlideResult::FellOff)
	{
		Leave(Sim);
		return;
	}

	FVector RailUp;
	const FQuat Target = SSXRail::RailFrame(R.Tangent, St.BasisUp, R.Roll, RailUp);
	St.GroundNormal = RailUp;
	St.Orientation = SSXRail::GameSlerp(St.Orientation, Target, SSXRail::OrientBlend);   // 0x129008
	Sim.UpdateBasis();
	St.LastGroundPosition = St.Position;
	// 0x156E80
	if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim))
	{
		Air->Score.RailFrame(St.Velocity.Size());
	}
	// 0x10B0D0, 0x125088, 0x126250
	PropCollision(Sim, M::RailSlide, X.Anim.Get());
	St.bWallHitThisFrame = WallCollision(Sim, SSXRider::MaskGroundWall, X.Anim.Get());
}
}
