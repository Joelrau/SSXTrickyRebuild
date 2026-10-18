#include "Camera/RaceCamera.h"
#include "Race/GameRng.h"

#include "Sim/EEFloat.h"
#include "Sim/RiderState.h"
#include "Sim/Rider/RiderShape.h"

#include <cmath>

namespace
{
	constexpr double Pi = UE_DOUBLE_PI;
	constexpr double TwoPi = 2.0 * UE_DOUBLE_PI;
	constexpr double DirKeep = 0.95, DirNew = 0.05;            // FUN_00173270
	constexpr double DirKeepWipe = 0.99, DirNewWipe = 0.01;
	constexpr double DirSlow = 10.0;                           // cm/s
	constexpr double PosKeep = 0.8, PosNew = 0.2;              // FUN_00174238
	constexpr double RollSmoothDiv = 25.0;
	constexpr double BlendRise = 0.029166667, BlendCap = 0.98, BlendDecay = 0.003;
	constexpr double DipWindow = 0.083333336, DipLead = 0.033333335;
	constexpr double DipMin = 1000.0, DipRange = 3000.0;
	constexpr double CeilProbe = 150.0, CeilPush = 20.0;       // FUN_0016fe60
	constexpr double CutClock = 500.0;
	constexpr double CtorFov = 1.6, CtorNear = 100.0, ResetFov = 1.5;   // FUN_00173820, FUN_00173a98
	constexpr double HalfFovMin = 0.054831136, HalfFovMax = 1.3962634; // FUN_0017f390
	constexpr double AnchorFallbackUp = 72.0;   // anchor ~72 cm above the position without bones
	constexpr int32 MAir = 1, MGround = 2, MRail = 3, MWipe = 5, MWipeRecover = 6;
	constexpr int32 CGetupFromSit = 0x12;
	const FVector ZUp(0, 0, 1);

	FVector Norm(const FVector& A)
	{
		const double L = A.Size();
		return L == 0.0 ? FVector::ZeroVector : A / L;
	}

	double SlopeRoll(const FVector& N, const FVector& W)
	{
		const FVector Q = Norm(N - W * FVector::DotProduct(N, W));
		const FVector R = Norm(ZUp - W * FVector::DotProduct(ZUp, W));
		double Cv = FVector::DotProduct(R, Q);
		if (FMath::Abs(Cv) > 1.0)
		{
			Cv = 1.0;
		}
		double Ang = FMath::Acos(Cv);
		if (FVector::DotProduct(FVector::CrossProduct(W, R), Q) < 0.0)
		{
			Ang = -Ang;
		}
		return Ang;
	}
}

double SSXCamera::Wrap(double A)
{
	A = std::fmod(A, TwoPi);
	if (A <= -Pi)
	{
		A += TwoPi;
	}
	else if (A > Pi)
	{
		A -= TwoPi;
	}
	return A;
}

double SSXCamera::Ease(int32 Kind, double U)
{
	const double U2 = U * U, U3 = U2 * U;
	const double H10 = U3 - 2 * U2 + U, H01 = -2 * U3 + 3 * U2, H11 = U3 - U2;
	switch (Kind)
	{
	case 1: return H01 + H11;
	case 2: return H01;
	case 3: return H10 + H01;
	default: return U;
	}
}

const TCHAR* SSXCamera::ModeDefinition(int32 Mode)
{
	switch (Mode)
	{
	case 1: return TEXT("chase reverse");
	case 2: return TEXT("chase board");
	case 3: return TEXT("chase eyes");
	case 4: return TEXT("chase near");
	case 5: return TEXT("chase far");
	case 6: return TEXT("chase over");
	case 8: return TEXT("replay cam");
	default: return TEXT("chase far");
	}
}

const TCHAR* SSXCamera::ReplayModeDefinition(int32 Mode)
{
	switch (Mode)
	{
	case 1: case 2: case 3: case 4: case 5: case 6: case 8: return TEXT("replay cam");   // FUN_00174c70
	// 0x177564, FUN_00173c18, 0x337774, 0x1786D0
	case 10: return TEXT("manual target");
	default: return TEXT("chase far");      // 0x177600
	}
}

float SSXCamera::Hermite(float P0, float P1, float M0, float M1, float T)
{
	using SSXEE::FpuAdd;
	using SSXEE::FpuSub;
	const float T2 = T * T;
	const float T3 = T2 * T;
	const float H00 = FpuSub(FpuAdd(T3, T3), T2 * 3.f);
	const float H01 = FpuAdd(T3 * -2.f, T2 * 3.f);
	const float H10 = FpuAdd(FpuSub(T3, FpuAdd(T2, T2)), T);
	const float H11 = FpuSub(T3, T2);
	float A = FpuAdd(H00 * P0, P0);
	A = FpuAdd(A, H01 * P1);
	A = FpuAdd(A, H10 * M0);
	return FpuAdd(A, H11 * M1);
}

float SSXCamera::FFlash::Update()
{
	float C8 = 0.f;
	if (bActive)
	{
		C8 = Hermite(0.7f, 0.f, 1.6f, 0.f, T / 0.6f);
		if (0.99f < C8)
		{
			C8 = 0.99f;
		}
		T = SSXEE::FpuAdd(T, 0.016666668f);
		if (0.6f <= T)
		{
			bActive = false;
		}
	}
	if (Hold > 0)
	{
		--Hold;
		C8 = 1.f;
	}
	return C8;
}

float SSXCamera::ProjectionFar(float DrawDist1P, float DrawDist2P, int32 Viewports, int32 RacePhase, int32 ReplayState)
{
	if (ReplayState >= 1 && ReplayState <= 9)
	{
		return FMath::Max(DrawDist1P, 30000.f);
	}
	if (RacePhase == 1)
	{
		return FMath::Max(DrawDist1P, 30000.f);
	}
	return Viewports < 2 ? DrawDist1P : DrawDist2P;
}

SSXCamera::FFlash& SSXCamera::SharedFlash()
{
	static FFlash Flash;
	return Flash;
}

FSSXCameraRiderInput FSSXCameraRiderInput::FromState(const FRiderState& S, const FVector& Anchor)
{
	FSSXCameraRiderInput R;
	R.Velocity = S.Velocity;
	R.Forward = S.BasisForward;
	R.Yaw = S.Yaw;
	R.Anchor = Anchor;
	R.Contact = S.ContactPoint;
	R.BoostStrength = S.BoostStrength;
	R.Motion = int32(S.Motion);
	R.Control = int32(S.Control);
	R.GroundNormal = S.GroundNormal;
	R.Surface = S.SurfaceId;
	R.PredTimeLeft = S.Prediction.TimeLeft;
	R.PredLandVelocity = S.Prediction.LandVelocity;
	R.PredLandNormal = S.Prediction.LandNormal;
	R.PredLandPoint = S.Prediction.LandPoint;
	R.FinishResult = S.FinishResult;
	R.bGoofy = S.bStanceGoofy;
	return R;
}

FVector SSXCamera::RiderAnchor(const FRiderState& S, const FRiderProfile& Profile, const FQuat& Rendered)
{
	const double K = Profile.SizeScale;
	FVector T = S.Position + S.SmoothedUp * S.SurfaceP10 + S.ContactLeft * (K * S.Lean.Cur);
	if (S.Motion == ERiderMotion::Ground || S.Motion == ERiderMotion::Static)
	{
		T += Rendered.RotateVector(S.SmoothedUp * (S.SuspensionOffset * K));
	}
	FVector Hips;
	if (!SSXRider::BodyPivot(Profile, S.bStanceGoofy, Hips))
	{
		Hips = FVector(0, 0, AnchorFallbackUp);   // TODO: no bones exported
	}
	return T + Rendered.RotateVector(Hips);
}

void FSSXRaceCamera::Reset()
{
	for (int32 I = 0; I < 22; ++I)
	{
		C[I] = V[I] = 0.0;
	}
	if (!bBlendScratch)
	{
		for (int32 I = 0; I < 22; ++I)
		{
			BlendF[I] = BlendT[I] = 0.0;   // 0x174D28
		}
	}
	C[6] = ResetFov;
	C[7] = CtorNear;
	V[6] = CtorFov;
	V[7] = CtorNear;
	ViewIndex = 1;
	Mode = 4;
	SavedMode = 4;   // FUN_00173a98
	bCut = true;
	bKeepCut = false;
	StaticCount = 0;
	// 0x173A98
	bBlendDone = false;
	Blend = 0.0;
	Name.Reset();
	bPushed = false;
	bRollForced = false;
	S = FVector::ZeroVector;
	for (int32 K = 0; K < NumSlots; ++K)
	{
		Entries[K] = FEntry();
		EvalEntries[K] = FEntry();
		PrevEntries[K] = FEntry();
	}
	Inputs.Reset();
	LastLookX = 1.0;
	LastLookY = 0.0;
	Script = nullptr;
	ScriptClock = 0.0;
	ScriptEvent = 0;
	bHold = bReturned = false;
}

void FSSXCameraReverseLatch::Step(FSSXRaceCamera& Cam, bool bHeld, bool bPressed, bool bReplayFlag)
{
	if (!bLatch || !bHeld)
	{
		bLatch = bPressed;   // 0x1797A8
		Cam.SetReverseHold(false, bReplayFlag);
	}
	else
	{
		Cam.SetReverseHold(true, bReplayFlag);
	}
}

void FSSXRaceCamera::SetReverseHold(bool bPress, bool bReplayFlag)
{
	// FUN_001776F0
	if (IsDirectorOn() || bReplayFlag)
	{
		return;
	}
	if (bPress && Mode != 3)
	{
		SavedMode = Mode;
		Blend = 0.0;
		SetMode(3);         // FUN_001776D0
	}
	else if (!bPress && Mode == 3)
	{
		SetMode(SavedMode);
	}
}

void FSSXRaceCamera::SetViewIndex(int32 Index)
{
	static const int32 ViewMode[SSXCamera::ViewCount] = { 2, 4, 5, 6 };
	ViewIndex = ((Index % SSXCamera::ViewCount) + SSXCamera::ViewCount) % SSXCamera::ViewCount;
	SetMode(ViewMode[ViewIndex]);
}

bool FSSXRaceCamera::PlayScript(const FString& ScriptName)
{
	const TArray<FSSXCameraScriptEvent>* Found = Defs ? Defs->FindScript(ScriptName) : nullptr;
	if (!Found)
	{
		return false;
	}
	Script = Found;
	CurScript = ScriptName;
	Stack.Reset();
	ScriptClock = 0.0;
	ScriptEvent = 0;
	bReturned = false;
	return true;
}

SSXCamera::FProjection SSXCamera::Projection(float HalfFov, float Near, float Far)
{
	FProjection P;
	const float Cot = 1.f / FMath::Tan(HalfFov);
	P.M00 = Cot;
	P.M11 = Cot * (4.f / 3.f);
	P.M22 = (Far + Near) / (Far - Near);
	P.M23 = 1.f;
	P.M32 = (-2.f * Far * Near) / (Far - Near);
	return P;
}

void FSSXRaceCamera::ViewAxes(double Yaw, double Pitch, double Roll, FVector& OutForward, FVector& OutUp)
{
	const double Cp = FMath::Cos(Pitch);
	OutForward = FVector(Cp * FMath::Cos(Yaw), Cp * FMath::Sin(Yaw), FMath::Sin(Pitch));
	FVector R = Norm(ZUp - OutForward * FVector::DotProduct(ZUp, OutForward));
	if (R.IsZero())
	{
		R = FVector(-FMath::Cos(Yaw), -FMath::Sin(Yaw), 0.0);
	}
	OutUp = R * FMath::Cos(Roll) + FVector::CrossProduct(OutForward, R) * FMath::Sin(Roll);
}

void FSSXRaceCamera::GetViewAxes(FVector& OutForward, FVector& OutUp) const
{
	ViewAxes(C[3], C[4], C[5], OutForward, OutUp);
}

// FUN_00173270
void FSSXRaceCamera::UpdateEntry(FEntry& E, const FSSXCameraRiderInput& R) const
{
	FVector D = E.Dir;
	const FVector& Vel = R.Velocity;
	const double Vl = Vel.Size();
	const bool bReset = bCut || D.Size() < 0.5;
	const bool bWipe = (R.Motion == MWipe || R.Motion == MWipeRecover) && R.Control != CGetupFromSit;
	if (!bWipe)
	{
		E.Yaw = R.Yaw;
		if (bReset)
		{
			D = Vl < DirSlow ? R.Forward : Vel;
		}
		else
		{
			D = Norm(D) * DirKeep + (Vl < DirSlow ? R.Forward : Norm(Vel)) * DirNew;
		}
	}
	else if (R.Motion == MWipeRecover)
	{
		D = bReset ? Vel : Norm(D) * (DirKeep * Vl) + Vel * DirNew;
	}
	else
	{
		D = bReset ? Vel : Norm(D) * (DirKeepWipe * Vl) + Vel * DirNewWipe;
	}
	E.Dir = Norm(D);
	E.Anchor = R.Anchor;
}

// FUN_00173c50, FUN_00174010, FUN_001731f8
void FSSXRaceCamera::UpdateTable(const FSSXCameraRiderInput& R)
{
	const int32 N = (NumRacers && RacerInput) ? FMath::Min(NumRacers(), 10) : 0;
	const int32 Own = (N > 0 && OwnRacer) ? OwnRacer() : -1;
	for (int32 K = 0; K < NumSlots; ++K) { PrevEntries[K] = Entries[K]; }
	Inputs.Reset();
	if (N <= 0 || Own < 0 || Own >= N)
	{
		Inputs.Add(R);
		Entries[0].Racer = 0;
		for (int32 K = 1; K < NumSlots; ++K) { Entries[K].Racer = -1; }
		UpdateEntry(Entries[0], R);
	}
	else
	{
		for (int32 I = 0; I < N; ++I)
		{
			FSSXCameraRiderInput& In = Inputs.AddDefaulted_GetRef();
			if (I == Own) { In = R; }
			else if (!RacerInput(I, In)) { In = FSSXCameraRiderInput(); }
		}
		for (int32 K = 0; K < NumSlots; ++K) { Entries[K].Racer = -1; }
		Entries[0].Racer = Own;
		int32 Next = 1;
		for (int32 I = 0; I < N; ++I)
		{
			if (I == Own)
			{
				if (I > 0) { Entries[11].Racer = I - 1; }
				if (I < N - 1) { Entries[12].Racer = I + 1; }
			}
			else
			{
				Entries[Next++].Racer = I;
			}
			const int32 Ch = Inputs[I].Character;
			if (Ch >= 0 && Ch < 12) { Entries[14 + Ch].Racer = I; }
		}
		// FUN_00173270
		for (int32 K = 0; K < N; ++K)
		{
			if (Entries[K].Racer >= 0) { UpdateEntry(Entries[K], Inputs[Entries[K].Racer]); }
		}
		for (int32 K = 10; K < NumSlots; ++K)
		{
			if (Entries[K].Racer >= 0)
			{
				const FSSXCameraRiderInput& In = Inputs[Entries[K].Racer];
				Entries[K].Yaw = In.Yaw;
				Entries[K].Dir = FVector::ZeroVector;
				Entries[K].Anchor = In.Anchor;
			}
		}
	}
	if (StaticCount < 3)
	{
		for (int32 K = 0; K < NumSlots; ++K) { EvalEntries[K] = Entries[K]; }
	}
}

const FSSXRaceCamera::FEntry& FSSXRaceCamera::SlotEntry(int32 Slot) const
{
	return EvalEntries[FMath::Clamp(Slot, 0, NumSlots - 1)];
}

const FSSXCameraRiderInput* FSSXRaceCamera::SlotRider(int32 Slot) const
{
	const int32 I = SlotEntry(Slot).Racer;
	return Inputs.IsValidIndex(I) ? &Inputs[I] : nullptr;
}

bool FSSXRaceCamera::SlotBone(int32 Slot, int32 Bone, FVector& Out) const
{
	const FSSXCameraRiderInput* In = SlotRider(Slot);
	if (!In)
	{
		return false;
	}
	if (Bone == -1)
	{
		Out = In->BoardPos;
		return In->bHasBoard;
	}
	if (Bone >= 0 && Bone < In->Bones.Num())
	{
		Out = In->Bones[Bone];
		return true;
	}
	return false;
}

// FUN_0016fb08, FUN_0016fe60
FVector FSSXRaceCamera::Clearance(FVector P, const FVector& A, double Radius) const
{
	if (!LineQuery)
	{
		return P;
	}
	const FVector W = Norm(A - P);
	FVector U = Norm(ZUp - W * FVector::DotProduct(ZUp, W)) * Radius;
	FVector Pt, N;
	bool bHit = Line(P + U, P - U, Pt, N);
	if (!bHit)
	{
		U = FVector::CrossProduct(U, W);
		bHit = Line(P + U, P - U, Pt, N);
	}
	if (bHit)
	{
		P = Pt + N * Radius;
	}
	return P;
}

FVector FSSXRaceCamera::Collide(double OrbitDist, FVector P, const FVector& A, bool& bHit) const
{
	bHit = false;
	if (!LineQuery)
	{
		return P;
	}
	FVector Ap = A, Pt, N;
	if (Line(A, A + FVector(0, 0, CeilProbe), Pt, N) && N.Z > 0.0)
	{
		Ap = Pt + N * CeilPush;
	}
	if (Line(Ap, P, Pt, N))
	{
		const FVector P1 = P - N * FVector::DotProduct(P - Pt, N);
		FVector Pt2, N2;
		// 0x16FE60
		const bool bHit2 = Line(Ap, P1, Pt2, N2);
		P = bHit2 ? Pt2 : P1;
		P += (bHit2 ? N2 : N) * (OrbitDist > 500.0 ? 60.0 : 40.0);
		bHit = true;
		return P;
	}
	return Clearance(P, A, OrbitDist > 500.0 ? 100.0 : 40.0);   // 0x16FB08
}

// FUN_0016e900
FVector FSSXRaceCamera::EvalPosition(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, bool& bHit, double* W, bool bCutFlag)
{
	const FEntry& Ent = SlotEntry(D.PosRiderSlot);
	const FSSXCameraRiderInput* Rider = D.PosAttachRider ? SlotRider(D.PosRiderSlot) : nullptr;
	const FVector& Dir = Ent.Dir;
	const bool bHasDir = Dir.Size() != 0.0;
	FVector Offset = FVector::ZeroVector;
	W[12] = 0.0;
	if (D.OrbitEnable)
	{
		const double Dist = D.OrbitDist + (Rider ? D.OrbitDistBoost * Rider->BoostStrength : 0.0);
		double E = D.ElevBase;
		if (D.ElevFromVelocity && Rider && bHasDir)
		{
			const double Slope = Pi / 2 - FMath::Atan2(FMath::Sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y), Dir.Z);
			E += Slope / (Dir.Z < 0.0 ? D.ElevDivDescending : D.ElevDivAscending);
			W[12] = 1.0;
			W[9] = Dir.X;
			W[10] = Dir.Y;
			W[11] = Dir.Z;
		}
		if (D.ElevClampEnable)
		{
			E = FMath::Min(FMath::Max(E, D.ElevMin), D.ElevMax);
		}
		const bool bLag = D.OrbitLagEnable && !bCutFlag && D.OrbitLagDiv != 0.0;
		if (bLag)
		{
			E = W[20] + (E - W[20]) / D.OrbitLagDiv;
		}
		W[20] = E;
		double Y = D.YawOffset;
		if (D.YawGoofyFlip && SlotRider(D.PosRiderSlot) && SlotRider(D.PosRiderSlot)->bGoofy)
		{
			Y = SSXCamera::Wrap(Pi - Y) + Pi;
		}
		if (D.YawFromHeading && Rider)
		{
			// 0x16E900, FUN_001731f8
			Y += (Dir.X != 0.0 || Dir.Y != 0.0) ? -FMath::Atan2(Dir.X, Dir.Y) : Ent.Yaw;
			if (Y < 0.0)
			{
				Y += TwoPi;
			}
		}
		if (bLag)
		{
			Y = W[19] + SSXCamera::Wrap(Y - W[19]) / D.OrbitLagDiv;
		}
		W[19] = Y;
		const double Ce = FMath::Cos(E);
		Offset = FVector(Dist * FMath::Sin(Y) * Ce, -Dist * FMath::Cos(Y) * Ce, -Dist * FMath::Sin(E));
	}
	// 0x16F77C, 0x16F998
	FVector A = FVector::ZeroVector;
	if (D.PosAttachRider)
	{
		A = D.BoneIndex == -2 && Rider ? Rider->Contact : Ent.Anchor;
	}
	FVector PosAnchor = A;
	if (D.PosAttachRider && D.AnchorUseBone && Rider)
	{
		FVector Bone;
		if (SlotBone(D.PosRiderSlot, D.BoneIndex, Bone))
		{
			PosAnchor += Bone - Ent.Anchor;
		}
	}
	// FUN_001777D0
	FVector Base = D.Base;
	StageYaw = 0.0;
	if (D.StageArea != 0 && Defs)
	{
		double Yaw = 0.0;
		FVector B = Base;
		if (Defs->StageAreaTransform(D.StageArea, B, Yaw))
		{
			Base = B;
			StageYaw = Yaw;
		}
	}
	FVector P = PosAnchor + Base + Offset;
	W[14] = D.PosSmoothEnable;
	W[15] = D.PosSmoothParam;
	W[16] = D.UnkView40Enable;
	W[17] = D.UnkView44Value;
	bHit = false;
	if (D.OrbitEnable)
	{
		P = Collide(D.OrbitDist, P, A, bHit);
	}
	return P;   // TODO: shake noise table
}

// FUN_001722e8
void FSSXRaceCamera::EvalTarget(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, bool bLandingDip, double AddYaw, double* W)
{
	const FVector P(W[0], W[1], W[2]);
	double Yaw, Pitch, Roll;
	FVector L = P;
	if (!D.TgtAttachRider)
	{
		Yaw = D.TgtYaw;
		Pitch = D.TgtPitch;
		Roll = D.TgtRoll;
	}
	else
	{
		L = SlotEntry(D.TgtRiderSlot).Anchor + D.TgtOff;
		if (D.TgtUseBone && D.TgtBoneIndex >= 0)
		{
			FVector Bone;
			if (SlotBone(D.TgtRiderSlot, D.TgtBoneIndex, Bone))
			{
				L = Bone + D.TgtOff;
			}
		}
		double Dx = L.X - P.X, Dy = L.Y - P.Y;
		if (FMath::Sqrt(Dx * Dx + Dy * Dy) < 0.01)
		{
			Dx = LastLookX;
			Dy = LastLookY;
		}
		else
		{
			LastLookX = Dx;
			LastLookY = Dy;
		}
		Yaw = FMath::Atan2(Dy, Dx) + D.TgtYaw;
		if (bLandingDip)
		{
			const double Vn = FVector::DotProduct(-R.PredLandVelocity, Norm(R.PredLandNormal));
			if (R.Motion == MAir && R.PredTimeLeft < DipLead && Vn > DipMin)
			{
				Clock = 0.0;
			}
			if (R.Motion == MGround && Clock < DipWindow)
			{
				// 0x1722E8
				const double K = FMath::Min((Vn - DipMin) / DipRange, 1.0);
				const int32 Sf = R.Surface;
				const double Depth = Sf == 3 ? 166.0 : (Sf == 4 || Sf > 14) ? 125.0 : (Sf >= 12 && Sf <= 14) ? 250.0 : 200.0;
				L.Z -= K * Depth;
			}
		}
		Pitch = FMath::Atan2(L.Z - P.Z, FMath::Sqrt(FMath::Square(L.X - P.X) + FMath::Square(L.Y - P.Y))) + D.TgtPitch;
		Roll = 0.0;
		if ((D.RollFollowSlope || bRollForced) && R.Motion != MAir && R.Motion != MRail && R.Motion != MWipe
			&& R.Motion != MWipeRecover)
		{
			Roll = SlopeRoll(R.GroundNormal, Norm(L - P)) / (bRollForced ? 1.0 : D.RollDiv);
		}
		Roll += D.TgtRoll;
	}
	if (D.TgtReverse)
	{
		Pitch = -Pitch;
		Yaw += Pi;
	}
	const double Fov = (D.AutoFov && D.TgtAttachRider)
		? FMath::Min(FMath::Max(D.AutoFovRef / ((L - P).Size() * D.Fov), 0.25), 2.0)
		: D.Fov;
	W[3] = Yaw + AddYaw;
	W[4] = Pitch;
	W[5] = Roll;
	W[6] = Fov;
	W[18] = FMath::Max(D.AngleSmoothDiv, 1.0);
}

// FUN_00174238
void FSSXRaceCamera::FlipAndClear()
{
	if (V[12] != 0.0 && SSXCamera::Wrap(V[3] - V[19]) < 0.0)   // 0x174238
	{
		V[3] = SSXCamera::Wrap(V[3] + Pi);
		V[4] = SSXCamera::Wrap(Pi - V[4]);
	}
	if (V[12] != 0.0 && Blend > 0.0)
	{
		const FVector P = Clearance(FVector(C[0], C[1], C[2]), SlotEntry(0).Anchor, 50.0);
		C[0] = P.X;
		C[1] = P.Y;
		C[2] = P.Z;
	}
}

void FSSXRaceCamera::Smooth()
{
	if (bCut)
	{
		FMemory::Memcpy(C, V, sizeof(C));
		S = FVector(V[0], V[1], V[2]);
		Clock = CutClock;
		FlipAndClear();
		if (!bKeepCut)
		{
			bCut = false;
		}
		bBlendDone = false;
		bKeepCut = false;
		return;
	}
	if (V[21] != 0.0)
	{
		return;
	}
	if (StaticCount >= 3)
	{
		FMemory::Memcpy(V, C, sizeof(V));
	}
	if (!bBlendDone || (V[14] != 0.0 && V[15] > 0.0))
	{
		S = S * PosKeep + FVector(V[0], V[1], V[2]) * PosNew;
	}
	else
	{
		S = FVector(V[0], V[1], V[2]);
	}
	if (StaticCount < 3)
	{
		C[0] = S.X;
		C[1] = S.Y;
		C[2] = S.Z;
	}
	double Div = V[18];
	if (Blend > 0.0 && Div < 50.0)
	{
		Div = Blend * 20.0 + 10.0;
	}
	C[3] += SSXCamera::Wrap(V[3] - C[3]) / Div;
	C[4] += SSXCamera::Wrap(V[4] - C[4]) / Div;
	C[5] += SSXCamera::Wrap(V[5] - C[5]) / RollSmoothDiv;
	C[6] += (V[6] - C[6]) / Div;
	FlipAndClear();
	bBlendDone = false;
}

// FUN_00174888, FUN_00174c98
void FSSXRaceCamera::Evaluate(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, bool bSmooth)
{
	Name = D.Name;
	StaticCount = D.StaticCut ? FMath::Min(StaticCount + 1, 3) : 0;
	const FVector Old(V[0], V[1], V[2]);
	bool bHit = false;
	FVector P = EvalPosition(D, R, bHit, V, bCut);
	if (!bCut)
	{
		P = Old + (P - Old) * (1.0 - Blend);
	}
	V[0] = P.X;
	V[1] = P.Y;
	V[2] = P.Z;
	bPushed = bHit;
	EvalTarget(D, R, Mode >= 2 && Mode <= 6, 0.0, V);   // 0x174888
	if (bSmooth)
	{
		Smooth();
	}
	C[7] = D.NearClip;   // FUN_00174c58
	UpdateFlash();
}

// FUN_00174d28
void FSSXRaceCamera::EvalScratch(const FSSXCameraDef& D, const FSSXCameraRiderInput& R, double* W, bool bCutFlag)
{
	bool bHit = false;
	const FVector P = EvalPosition(D, R, bHit, W, bCutFlag);
	W[0] = P.X;
	W[1] = P.Y;
	W[2] = P.Z;
	EvalTarget(D, R, false, StageYaw, W);   // 0x1722E8
}

bool FSSXRaceCamera::BlendTo(const FSSXCameraDef& To, const FSSXCameraRiderInput& R, double U)
{
	if (!bBlendScratch)
	{
		FMemory::Memcpy(BlendF, V, sizeof(V));
		FMemory::Memcpy(BlendT, V, sizeof(V));
		bBlendScratch = true;
	}
	const bool bStart = U <= 0.0;
	if (bStart)
	{
		FMemory::Memcpy(BlendF, V, sizeof(V));
		FMemory::Memcpy(BlendT, V, sizeof(V));
	}
	const FSSXCameraDef* From = Defs ? Defs->Find(Name) : nullptr;
	if (From)
	{
		EvalScratch(*From, R, BlendF, bCut);
	}
	// FUN_0016dd68, 0x16E518
	EvalScratch(To, R, BlendT, bStart);
	V[12] = 0.0;
	if (U >= 1.0)
	{
		FMemory::Memcpy(V, BlendT, sizeof(V));
		V[12] = 0.0;
		Smooth();
		C[7] = To.NearClip;   // FUN_00174c58
		UpdateFlash();
		bBlendDone = true;
		Name = To.Name;       // FUN_00175a08
		return true;
	}
	for (int32 I = 0; I < 3; ++I)
	{
		V[I] = BlendF[I] + (BlendT[I] - BlendF[I]) * U;
	}
	for (int32 I = 3; I <= 5; ++I)
	{
		V[I] = BlendF[I] + SSXCamera::Wrap(BlendT[I] - BlendF[I]) * U;
	}
	V[6] = BlendF[6] + (BlendT[6] - BlendF[6]) * U;
	Smooth();
	if (From)
	{
		C[7] = From->NearClip;
	}
	UpdateFlash();               // 0x1753F8
	return false;
}

// FUN_00175f50
bool FSSXRaceCamera::StepScript(const FSSXCameraRiderInput& R, double PrevClock)
{
	bool bEvaluated = false;
	bool bEntered = false;   // 0x175F50
	while (Script)
	{
		if (ScriptEvent >= Script->Num())
		{
			// FUN_00175eb8
			if (Stack.Num() > 0)
			{
				const FScriptFrame F = Stack.Pop();
				Script = F.Script;
				ScriptEvent = F.Event;
				ScriptClock = F.Clock;
				continue;
			}
			DirectorEndCut();
			Script = nullptr;
			break;
		}
		const FSSXCameraScriptEvent& E = (*Script)[ScriptEvent];
		const int32 Op = E.Op;
		if (ScriptClock < E.T0)
		{
			break;
		}
		const FSSXCameraDef* D = Defs ? Defs->Find(E.Name) : nullptr;
		if (Op == 0)
		{
			if (D)
			{
				// FUN_00177db0
				if ((bEntered || (ScriptEvent == 0 && PrevClock == 0.0)) && Name != E.Name)
				{
					MarkCut();
				}
				Evaluate(*D, R, true);   // FUN_00174c98
				bEvaluated = true;
			}
		}
		else if (Op == 1 || Op == 0x1C)   // FUN_00175a08, FUN_00174d28
		{
			if (D)
			{
				// 0x175A08
				double U = 1.0;
				if (E.T1 != E.T0)
				{
					const double Tc = ScriptClock < E.T0 ? E.T0 : (ScriptClock > E.T1 ? E.T1 : ScriptClock);
					U = (Tc - E.T0) / (E.T1 - E.T0);
				}
				if (U > 0.0 && U < 1.0)
				{
					U = SSXCamera::Ease(E.A, U);
				}
				BlendTo(*D, R, FMath::Min(U, 1.0));
				bEvaluated = true;
			}
		}
		else if (Op == 5 || Op == 6)
		{
			SetMode(Op == 5 ? 4 : 5);
		}
		else if (Op == 7)
		{
			bReturned = true;
			MarkCut();
		}
		else if (Op == 8 || Op == 9)
		{
			bHold = Op == 8;
		}
		else if (Op == 13)
		{
			MarkCut();
		}
		else if (Op == 0x17)
		{
			StartFlash();   // FUN_00174c30
		}
		else if (Op == 0x18 || Op == 0x19)
		{
			bRollForced = Op == 0x18;
		}
		else if (Op == 14 || Op == 15)
		{
			// FUN_00176850, FUN_00176F38
			if (OnSceneOp)
			{
				OnSceneOp(Op, E.Name);
			}
		}
		else if (Op == 0x21 || Op == 0x22 || Op == 0x29 || Op == 0x2A || Op == 0x2B)
		{
			if (OnScriptOp)
			{
				OnScriptOp(Op, E.Arg);
			}
		}
		else if (Op == 0x20)
		{
			Script = nullptr;
			Stack.Reset();
			break;
		}
		else if (Op == 10 || Op == 11)
		{
			const TArray<FSSXCameraScriptEvent>* Target = Defs ? Defs->FindScript(E.Name) : nullptr;
			const bool bTake = Op == 11 || int32(FGameRng::StreamA().Next() % 100u) < FMath::Abs(E.C);
			if (Target && bTake && (Op == 10 || Stack.Num() < 16))
			{
				if (Op == 11)
				{
					Stack.Add({ Script, ScriptEvent + 1, ScriptClock });
				}
				Script = Target;
				ScriptEvent = 0;
				ScriptClock = 0.0;
				bEntered = true;
				continue;
			}
		}
		if (ScriptClock < E.T1 && (Op == 0 || Op == 1 || Op == 0x1C))
		{
			break;
		}
		if (StaticCount == 3)
		{
			StaticCount = 0;
		}
		bEntered = true;
		++ScriptEvent;
	}
	return bEvaluated;
}

// 0x177078
void FSSXRaceCamera::Step(const FSSXCameraRiderInput& R)
{
	UpdateTable(R);                     // FUN_00174010
	StepRegions();
	Clock += SSXCamera::Dt;
	// 0x1773C0
	const bool bFrozen = !bCut && ReplayFrozen && ReplayFrozen();
	if (bFrozen)
	{
		UpdateFlash();
	}
	if (Script)
	{
		if (!bFrozen)
		{
			const double PrevClock = ScriptClock;
			ScriptClock += SSXCamera::Dt;
			if (StepScript(R, PrevClock))
			{
				return;
			}
		}
		if (Script)
		{
			return;                     // FUN_00174c70
		}
	}
	if (!bReplay && Mode != 1 && R.Control != CGetupFromSit)
	{
		const bool bWipe = R.Motion == MWipe || R.Motion == MWipeRecover;
		Blend = (bWipe && !bPushed) ? FMath::Min(Blend + BlendRise, BlendCap) : FMath::Max(Blend - BlendDecay, 0.0);
		if (!bWipe)
		{
			bPushed = false;
		}
	}
	const TCHAR* DefName = bReplay ? SSXCamera::ReplayModeDefinition(Mode) : SSXCamera::ModeDefinition(Mode);
	if (const FSSXCameraDef* D = Defs ? Defs->Find(DefName) : nullptr)
	{
		Evaluate(*D, R, true);
	}
}

bool FSSXRaceCamera::RegionInside(const FSSXCameraRegion& G, bool bPrevTable) const
{
	// FUN_00171610, FUN_001777D0
	const FEntry& E = (bPrevTable ? PrevEntries : Entries)[FMath::Clamp(G.RiderSlot, 0, NumSlots - 1)];
	FVector Centre = G.Centre;
	double AreaYaw = 0.0;
	if (G.StageArea != 0 && Defs)
	{
		FVector P = G.Centre;
		double Yaw = 0.0;
		if (Defs->StageAreaTransform(G.StageArea, P, Yaw)) { Centre = P; AreaYaw = Yaw; }
	}
	float Dx = float(Centre.X) - float(E.Anchor.X);
	float Dy = float(Centre.Y) - float(E.Anchor.Y);
	float Dz = float(Centre.Z) - float(E.Anchor.Z);
	const float Radius = float(G.Radius);
	if (!G.Capsule)
	{
		if (Radius < FMath::Abs(Dx) || Radius < FMath::Abs(Dy) || Radius < FMath::Abs(Dz)) return false;
	}
	else
	{
		const float Half = float(G.CapsuleLen) * 0.5f;
		const float Box = Radius <= Half ? Half : Radius;
		if (Box < FMath::Abs(Dx) || Box < FMath::Abs(Dy) || Box < FMath::Abs(Dz)) return false;
		float Yaw = float(G.CapsuleYaw);
		if (G.StageArea != 0) Yaw = float(SSXCamera::Wrap(double(Yaw + float(AreaYaw))));
		// FUN_00250C10, 0x346410, 0x251238, 0x346440
		const float Cy = FMath::Cos(Yaw), Sy = FMath::Sin(Yaw);
		const float Cp = FMath::Cos(float(G.CapsulePitch)), Sp = FMath::Sin(float(G.CapsulePitch));
		const float Hx = Cp * Cy * Half, Hy = -(Cp * Sy) * Half, Hz = Sp * Half;
		const float T = (Dx * Hx + Dy * Hy + Dz * Hz) / (Half * Half);
		if (!(FMath::Abs(T) <= 1.f)) return false;
		Dx -= Hx * T; Dy -= Hy * T; Dz -= Hz * T;   // FUN_00102F80
	}
	if (Radius <= FMath::Sqrt(Dx * Dx + Dy * Dy + Dz * Dz)) return false;
	// FUN_00171500
	const FSSXCameraRiderInput* Rd = Inputs.IsValidIndex(E.Racer) ? &Inputs[E.Racer] : nullptr;
	if (G.NeedBigAir)
	{
		if (!Rd || !(0.33f < float(Rd->PredTimeLeft))) return false;
		const FVector3f L = FVector3f(Rd->PredLandPoint) - FVector3f(E.Anchor);
		return float(G.MinAirDist) <= FMath::Sqrt(L.X * L.X + L.Y * L.Y + L.Z * L.Z);
	}
	if (!G.NeedWipeout) return true;
	return Rd && (Rd->Motion == MWipe || Rd->Motion == MWipeRecover);
}

bool FSSXRaceCamera::RegionEvent(const FSSXCameraRegion& G, int32 K) const
{
	switch (K)
	{
	case 0: return RegionInside(G, false) && !RegionInside(G, true);
	case 1: return RegionInside(G, true) && !RegionInside(G, false);
	case 2: return RegionInside(G, false);
	default: return false;
	}
}

void FSSXRaceCamera::StepRegions()
{
	if (!Defs || bRegionsOff)
	{
		return;
	}
	for (const TArray<FSSXCameraRegion>& File : Defs->GetRegionFiles())
	{
		for (const FSSXCameraRegion& G : File)
		{
			if (!((!bReplay && G.ActiveInRace) || (bReplay && Mode == 7 && G.ActiveInReplay)))
			{
				continue;
			}
			bool bSkip = false;
			const int32 Racer = Entries[FMath::Clamp(G.RiderSlot, 0, NumSlots - 1)].Racer;
			if (const FSSXCameraRiderInput* Rd = Inputs.IsValidIndex(Racer) ? &Inputs[Racer] : nullptr)
			{
				if (G.OnlyFinished && Rd->FinishResult < 0) bSkip = true;
				// FUN_0011F488, FUN_0015FE70
			}
			if (bSkip)
			{
				continue;
			}
			if (G.OnEnter && RegionEvent(G, 0)) { PlayScript(G.ScriptOnEnter); break; }
			if (G.OnExit && RegionEvent(G, 1)) { PlayScript(G.ScriptOnExit); break; }
			// FUN_00175E50
			if (!G.WhileInside || IsScriptRunning() || !RegionEvent(G, 2))
			{
				continue;
			}
			if (!IsDirectorOn() || G.ScriptWhileInside != CurScript) { PlayScript(G.ScriptWhileInside); }
			break;
		}
	}
}
