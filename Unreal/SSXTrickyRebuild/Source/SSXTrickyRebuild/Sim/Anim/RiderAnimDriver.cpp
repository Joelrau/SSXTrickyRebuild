#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Ground/GroundControls.h"

#include "Misc/ScopeExit.h"

#include "Sim/Air/AirStates.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/GroundPhysics.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderShape.h"

namespace SSXAnim
{
namespace
{
	using C = ERiderControl;

	constexpr int32 Riding = 0x1FF, FakieLoop = 0x202, StandLoop = 0x221, GateWait = 0x1FC, GatePush = 0x1FD,
		SitUpAnim = 0x1FE, SwitchAnim = 0x200, Prewind = 0x217, AirIdleAnim = 0x223, FallRecover = 0x224,
		Auto180Anim = 0x225, SitLoop = 0;

	bool Is(const TCHAR* Reason, const TCHAR* Id)
	{
		return Reason && FCString::Strcmp(Reason, Id) == 0;
	}
}

int32 TakeoffRow(double A, double B, bool bGoofy)
{
	A = FMath::Abs(A) < 0.2 ? 0.0 : A;
	B = FMath::Abs(B) < 0.2 ? 0.0 : B;
	if (A != 0.0 && B != 0.0)
	{
		const double Phi = FMath::Atan2(FMath::Abs(B), FMath::Abs(A));
		if (Phi > 1.3963) { A = 0.0; }
		else if (Phi < 0.1745) { B = 0.0; }
	}
	if (A == 0.0 && B == 0.0)
	{
		return 0;
	}
	const double T = bGoofy ? FMath::Atan2(-B, -A) : FMath::Atan2(B, A);
	if (FMath::Abs(T) > 2.7489) { return 1; }
	if (T > 1.9635) { return 2; }
	if (T > 1.1781) { return 3; }
	if (T > 0.3927) { return 4; }
	if (T < -1.9635) { return 5; }
	if (T < -1.1781) { return 6; }
	if (T < -0.3927) { return 7; }
	return 8;
}

int32 TakeoffClip(int32 Row, int32 TakeoffType)
{
	// 0x35FED0
	static const int32 Table[9][5] = {
		{ 0x218, 0x218, 0x218, 0x261, 0x260 },
		{ 0x220, 0x262, 0x262, 0x268, 0x26C },
		{ 0x21C, 0x21C, 0x21C, 0x268, 0x26C },
		{ 0x21A, 0x264, 0x264, 0x266, 0x26B },
		{ 0x21B, 0x21B, 0x21B, 0x269, 0x26D },
		{ 0x21E, 0x21E, 0x21E, 0x268, 0x26C },
		{ 0x219, 0x265, 0x265, 0x267, 0x26A },
		{ 0x21D, 0x21D, 0x21D, 0x269, 0x26D },
		{ 0x21F, 0x263, 0x263, 0x269, 0x26D },
	};
	return Table[FMath::Clamp(Row, 0, 8)][FMath::Clamp(TakeoffType, 0, 4)];
}

int32 DirectionalTrickId(int32 SpinIn, int32 FlipIn, bool bGoofy)
{
	const double T = FMath::RadiansToDegrees(bGoofy ? FMath::Atan2(double(FlipIn), double(-SpinIn)) : FMath::Atan2(double(-FlipIn), double(SpinIn)));
	if (FMath::Abs(T) > 157.5) { return 0x22D; }
	if (T > 112.5) { return 0x229; }
	if (T > 67.5) { return 0x227; }
	if (T > 22.5) { return 0x228; }
	if (T < -112.5) { return 0x22B; }
	if (T < -67.5) { return 0x226; }
	if (T < -22.5) { return 0x22A; }
	return 0x22C;
}

int32 TumbleId(double UpN, double SideN, double FwdN, int32 CurrentCategory)
{
	const bool bFirst = CurrentCategory == 17;
	if (UpN < -0.7) { return bFirst ? 0x2E4 : 0x2EB; }
	if (UpN > 0.7) { return bFirst ? 0x2E3 : 0x2EA; }
	if (SideN > 0.7) { return bFirst ? 0x2E5 : 0x2EC; }
	if (SideN < -0.7) { return bFirst ? 0x2E6 : 0x2ED; }
	return FwdN > 0.0 ? (bFirst ? 0x2E1 : 0x2E8) : (bFirst ? 0x2E2 : 0x2E9);
}

int32 RailStartId(int32 Type, int32 BoardClass, bool bProfileStance)
{
	if (BoardClass == 2 && bProfileStance)
	{
		Type = Type == 1 ? 2 : (Type == 2 ? 1 : Type);
	}
	return Type == 4 ? 0x257 : Type == 3 ? 0x256 : Type == 2 ? 0x255 : 0x254;
}

int32 LandArmsId(const FVector& V, const FVector& Up, const FVector& Forward, const FVector& Side, bool bGoofy)
{
	const FVector E = (V - FVector::DotProduct(V, Up) * Up).GetSafeNormal();
	if (FMath::Abs(FVector::DotProduct(Forward, E)) > 0.866)
	{
		return 0x20C;
	}
	const double S = FVector::DotProduct(Side, E) * (bGoofy ? -1.0 : 1.0);
	return S > 0.0 ? 0x20D : 0x20E;
}

uint32 OverlayMask(int32 AnimId, int32 Shift)
{
	auto Range = [Shift](int32 Lo, int32 Hi)
	{
		uint32 M = 0;
		for (int32 B = Lo; B <= Hi; ++B) { M |= 1u << ((Shift + B) & 31); }
		return M;
	};
	auto Bit = [Shift](int32 B) { return 1u << ((Shift + B) & 31); };
	switch (AnimId)
	{
	case 0x20F: case 0x210: return Bit(3) | Bit(4) | Bit(5) | Bit(9);        // 0x1033D0
	case 0x209: case 0x20A: case 0x20B: return Range(5, 12);                  // 0x103B88
	case 0x211: case 0x212: case 0x213:                                       // 0x103660
	case 0x20C: case 0x20D: case 0x20E: return Range(1, 12);                  // 0x1049F8
	case 0x214: case 0x215: return Range(2, 12);                              // 0x128150, 0x127E80
	case 0x205: case 0x206: case 0x207: case 0x208: return Range(1, 12);      // 0x127918
	case 0x216: return Range(6, 8) | Range(10, 12);                           // 0x127D08
	default: return 0xFFFFFFFFu;
	}
}

bool FRiderAnimDriver::Init(const FAflLibrary* InLib, const FString& RiderId, int32 BoardClass, int32 InCharacter)
{
	bReady = false;
	bStarted = false;
	EventsSeen = 0;
	LastTrickId = -1;
	bTrickWasActive = false;
	LastTrickStarts = 0;
	LastAuto180 = 0;
	RailType = -1;
	Character = InCharacter;
	Lib = InLib;
	Parts = nullptr;
	if (!Lib || !Lib->IsLoaded())
	{
		return false;
	}
	const FAflSkeleton* Body = Lib->Skeleton(RiderId);
	if (!Body || !Set.Init(*Lib, Body, BoardClass))
	{
		return false;
	}
	Controller.Init(Lib, BoardClass, InCharacter);
	Parts = Lib->Parts(RiderId);
	bReady = true;
	return true;
}

FAnimDriveInputs FRiderAnimDriver::DriveInputs(const FRiderState& S)
{
	FAnimDriveInputs In;
	In.SteerFollow = S.SteerFollow.Cur;
	In.Brake = S.Brake.Cur;
	In.Crouch = S.Crouch.Cur;
	In.Steer = S.Steer.Cur;
	In.AirTurn = S.AirTurnSmoothed;
	In.AirTilt = S.AirTiltSmoothed;
	In.Ramp1F0 = S.Ramp1F0.Cur;
	In.PrewindA = S.PrewindA.Cur;
	In.PrewindB = S.PrewindB.Cur;
	In.Gate = S.GateLaunch.Cur;
	In.GrindType = S.GrindType;
	In.bGoofy = S.bStanceGoofy;
	return In;
}

FRiderFrameInput FRiderAnimDriver::FrameInputs(const FRiderState& S, double Scale)
{
	FRiderFrameInput F;
	F.Orientation = S.Orientation;
	F.Position = S.Position;
	F.Steer = S.Steer.Cur;
	F.Motion = int32(S.Motion);
	F.BoardClass = S.BoardClass;
	F.Up = S.SmoothedUp;
	F.S308 = S.SurfaceP10;
	F.Left = S.ContactLeft;
	F.Scale = Scale;
	F.Lean = S.Lean.Cur;
	F.Spin = S.SpinAngle;
	F.Flip = S.FlipAngle;
	F.Suspension = S.SuspensionOffset;
	F.BoardChatter = S.BoardChatter;
	F.GroundNormal = S.GroundNormal;
	return F;
}

void FRiderAnimDriver::Play(int32 AnimId, double Speed)
{
	Controller.Mask = OverlayMask(AnimId, MaskShift);
	uint32 R = 0;
	if (StepChannel && StepChannel->TakeDraw(AnimId, R))
	{
		TFunction<uint32()> Saved = MoveTemp(Controller.RandomB);
		Controller.RandomB = [R]() { return R; };
		Controller.Play(AnimId, 0.0, 5, Speed);
		Controller.RandomB = MoveTemp(Saved);
	}
	else
	{
		Controller.Play(AnimId, 0.0, 5, Speed);
	}
	Controller.Mask = 0xFFFFFFFFu;
}

int32 FRiderAnimDriver::CurrentCategory() const
{
	// 0x15EEC0
	const int32 Id = Controller.CurrentId(0);
	const FAnimProps* P = (Id != NoAnim && Lib) ? Lib->Props(Id) : nullptr;
	return P ? P->Category : 8;
}

int32 FRiderAnimDriver::TumbleAnim(const FRiderState& S) const
{
	// 0x10F9A0
	const FVector& N = S.GroundNormal;
	return TumbleId(FVector::DotProduct(S.BasisUp, N), FVector::DotProduct(S.BasisSide, N), FVector::DotProduct(S.BasisForward, N), CurrentCategory());
}

void FRiderAnimDriver::OnControl(const FRiderSim& Sim, uint8 From, uint8 To, const TCHAR* Reason)
{
	const FRiderState& S = Sim.State;
	const FRiderAnimChannel* Ch = FindChannel(Sim);
	const int32 Cur = Controller.CurrentId(0);
	if (C(From) == C::RailSlide && C(To) != C::RailSlide)
	{
		RailType = -1;
	}
	switch (C(To))
	{
	case C::Cruise:
		if (Is(Reason, TEXT("T41")))
		{
			Play(StandLoop);
		}
		else if (Is(Reason, TEXT("T62")))
		{
			Play(QuickRecoveryId(CurrentCategory()));   // 0x10D274
		}
		else if (Cur != Riding && Cur != StandLoop)
		{
			Play(Riding);
		}
		break;
	case C::ChangeFakie:
		Play(SSXGround::ChangeFakieSkipsSwitch(Sim) ? Riding : SwitchAnim);
		break;
	case C::FakieCruise:
		if (Cur != FakieLoop)
		{
			Play(FakieLoop);
		}
		break;
	case C::FinishLine:
		break;                               // 0x106668
	case C::GateToBase:
		Play(GatePush);
		break;
	case C::GateAnticipate:
		if (Cur != GateWait)
		{
			Play(GateWait);                  // 0x11A840
		}
		break;
	case C::SitAndWait:
		if (Cur != SitLoop)
		{
			Play(SitLoop);
			Play(SitIdleId(Character));
		}
		break;
	case C::Jump:
		// 0x1050E8, 0x1055F0
		Play(TakeoffClip(TakeoffRow(S.PrewindA.Cur, S.PrewindB.Cur, S.bStanceGoofy), S.GrindType));
		break;
	case C::LandHard:
		if (Ch)
		{
			Play(Ch->LandHardAnim);
		}
		break;
	case C::LandNormal:
		// 0x109308, 0x1049F8
		if (Is(Reason, TEXT("T52")))
		{
			Play(Riding);
		}
		break;
	case C::NaturalAir:
		if (Is(Reason, TEXT("T61")))
		{
			Play(FallRecover);
		}
		else if (Is(Reason, TEXT("T02")))
		{
			Play(AirIdleAnim);               // 0x11CAE8
		}
		// 0x10A0D8, 0x1000B8
		break;
	case C::Prewind:
		Play(Prewind);
		break;
	case C::RailSlide:
		// 0x106DC8
		RailType = S.GrindType;
		break;
	case C::Spin:
		if (Is(Reason, TEXT("T50")) && S.Prediction.FlightTime > 0.35)
		{
			Play(S.GrindType == 3 ? 0x26E : S.GrindType == 4 ? 0x26F : AirIdleAnim);
		}
		// 0x126FC0, 0x102158, 0x102800
		break;
	case C::GetupFromSit:
		Play(SitUpAnim);
		break;
	case C::WipeOut:
		Play(TumbleAnim(S));
		break;
	case C::WipeOutRecover:
		if (Ch)
		{
			Play(Ch->GetUpAnim);
		}
		break;
	case C::Bump:
		if (Ch)
		{
			Play(Ch->BumpAnim);
		}
		break;
	default:
		// 0x106228, 0x106500, 0x11AFE8, 0x15EE30
		break;
	}
}

void FRiderAnimDriver::StepHairIdle(const FRiderSim& Sim)
{
	// 0x11D4E8
	const FRiderState& S = Sim.State;
	IRiderRaceHooks::FRacerTable Table;
	const IRiderRaceHooks* Hooks = Sim.GetRaceHooks();
	const int32 RaceFrame = Hooks && Hooks->RacerTable(Sim, Table) && Table.RaceFrame >= 0 ? Table.RaceFrame : S.Frame;
	if (!HairIdleDue(RaceFrame, int32(S.Control)))
	{
		return;
	}
	const FRowMat R = RootFrame(FrameInputs(S, Sim.Profile.SizeScale), 0.0, true);
	auto Row = [&R](int32 K) { return FVector4f(R.M[K][0], R.M[K][1], R.M[K][2], R.M[K][3]); };
	const FVector4f V(float(S.Velocity.X), float(S.Velocity.Y), float(S.Velocity.Z), 0.0f);
	const int32 Want = HairIdleId(V, Row(0), Row(1), Row(2), Character, Controller.CurrentId(0));
	if (Want != Controller.CurrentId(2))
	{
		Play(Want);
	}
}

void FRiderAnimDriver::SaveEventsForReplay(const FRiderSim& Sim)
{
	const TArray<FRiderSimEvent>& Events = Sim.GetEvents();
	for (int32 I = FMath::Clamp(EventsSeen, 0, Events.Num()); I < Events.Num(); ++I)
	{
		KeyframePending.Add(Events[I]);
	}
}

void FRiderAnimDriver::RestoreEventsForReplay(const FRiderSim& Sim)
{
	EventsSeen = Sim.GetEvents().Num();
}

void FRiderAnimDriver::Step(FRiderSim& Sim)
{
	if (!bReady)
	{
		return;
	}
	const FRiderState& S = Sim.State;
	FRiderAnimChannel* SimCh = FindChannel(Sim);
	StepChannel = SimCh;
	ON_SCOPE_EXIT { StepChannel = nullptr; };
	if (SimCh && SimCh->bResetAnim)
	{
		Controller.Reset();   // 0x15EE30, 0x11CAE8, 0x116AB8
		SimCh->bResetAnim = false;
	}
	if (SimCh)
	{
		// 0x15EEF8, 0x15EF48
		if (SimCh->LayerYawAdd != 0.0)
		{
			Controller.AddLayerYaw(SimCh->LayerYawAdd);
			SimCh->LayerYawAdd = 0.0;
		}
		if (SimCh->bClearLayerMirrors)
		{
			Controller.ClearLayerMirrors();
			SimCh->bClearLayerMirrors = false;
		}
	}
	if (S.bRailAnimPose && (S.Motion == ERiderMotion::RailSlide || S.Control == C::RailSlide))
	{
		Controller.bMirror = S.bAnimMirror;
		Controller.Yaw = S.AnimYaw;
	}
	else
	{
		Controller.bMirror = S.bStanceGoofy;
		Controller.Yaw = S.bStanceGoofy ? -UE_DOUBLE_PI : 0.0;
	}

	for (const FRiderSimEvent& E : KeyframePending)
	{
		if (!E.bMotion)
		{
			OnControl(Sim, E.From, E.To, E.Reason);
		}
	}
	KeyframePending.Reset();
	const TArray<FRiderSimEvent>& Events = Sim.GetEvents();
	if (EventsSeen > Events.Num())
	{
		EventsSeen = FMath::Max(0, EventsSeen - 1024);
	}
	for (int32 I = EventsSeen; I < Events.Num(); ++I)
	{
		const FRiderSimEvent& E = Events[I];
		if (!E.bMotion)
		{
			OnControl(Sim, E.From, E.To, E.Reason);
		}
	}
	EventsSeen = Events.Num();
	if (S.Control == C::RailSlide)
	{
		RailType = S.GrindType;
	}

	if (FRiderAnimChannel* Ch = FindChannel(Sim))
	{
		for (int32 K = 0; K < Ch->PlayRequests.Num(); ++K)
		{
			// 0x128150, 0x127E80, 0x102800
			const int32 Id = Ch->PlayRequests[K];
			if (Id == FRiderAnimChannel::ResetRequest)
			{
				// 0x11A840
				Controller.Reset();
				Controller.Speed = 1.0;
				Controller.Mask = 0xFFFFFFFFu;
				continue;
			}
			if (Id == FRiderAnimChannel::FadeChannel1Request)
			{
				Controller.FadeChannel(1, 10);   // 0x15F050
				continue;
			}
			Play(Id, (Id == 0x214 || Id == 0x215) ? 0.5 : Ch->PlayRequestSpeed(K));
		}
		Ch->ClearPlayRequests();
		// 0x169BE8
		for (const int32 Id : Ch->ScenePlays)
		{
			Controller.bMirror = false;
			Controller.Speed = 1.0;
			Controller.Yaw = 0.0;
			Controller.Play(Id, 0.0, 5);
		}
		Ch->ScenePlays.Reset();
		const FAnimInstance& Trick = Ch->Trick;
		const bool bNewStart = Ch->TrickStarts != LastTrickStarts;
		LastTrickStarts = Ch->TrickStarts;
		if (Trick.IsActive() && Trick.Data.AnimId >= 0 && (bNewStart || !bTrickWasActive || Trick.Data.AnimId != LastTrickId))
		{
			const bool bGrabSpeed = Trick.Data.Category == CategoryGrab || Trick.Data.Category == CategoryTweak;
			Play(Trick.Data.AnimId, bGrabSpeed ? Trick.Speed : -1.0);
		}
		if (Trick.IsActive())
		{
			// 0x15EF98
			if (FPlayInstance* I = Controller.Newest(0); I && I->AnimId == Trick.Data.AnimId && Trick.Hold < 0.0)
			{
				I->Hold = -1.0;
			}
		}
		bTrickWasActive = Trick.IsActive();
		LastTrickId = Trick.IsActive() ? Trick.Data.AnimId : -1;
		if (Ch->Auto180FramesLeft > LastAuto180)
		{
			Play(Auto180Anim);
		}
		LastAuto180 = Ch->Auto180FramesLeft;
	}
	if (!bStarted)
	{
		bStarted = true;
		if (Controller.Newest(0) == nullptr)
		{
			if (S.Control == C::SitAndWait)
			{
				Play(SitLoop);
				Play(SitIdleId(Character));
			}
			else if (S.Control == C::GateAnticipate || S.Control == C::GateLaunch)
			{
				Play(GateWait);
			}
			else
			{
				Play(S.Motion == ERiderMotion::Air ? AirIdleAnim : Riding);
			}
		}
	}
	StepHairIdle(Sim);
	Controller.Update(DriveInputs(S));
	{
		// 0x15EEC0
		const int32 C1 = Controller.CurrentId(1);
		Sim.State.Ch1Anim = C1;
		Sim.State.Ch1EndFrame = C1 == NoAnim ? 0 : MAX_int32;
		Sim.State.Ch0Anim = Controller.CurrentId(0);   // 0x15FDD0, 0x1000E0
	}
	{
		// 0x15F390, 0x15F7C8, 0x202990, 0x1029D0, 0x108378
		TArray<FPoseLayer> Layers;
		TArray<FBonePose> Body;
		Controller.GatherLayers(0, Layers);
		const double Scale = Sim.Profile.SizeScale;
		BlendLayers(*Lib, *Set.Body, Layers, Scale, Scale, MaskShift, Body);
		Sim.State.PoseRoot = Body.Num() > 0 ? FVector(Body[0].Trans) : FVector::ZeroVector;
		Sim.State.bPoseRootValid = Body.Num() > 0;
		// 0x129628
		const FRowMat M = RootFrame(FrameInputs(Sim.State, Scale), Sim.State.PoseRoot.Z, false);
		Sim.State.RenderRow3 = FVector(M.TransformPoint(FVector3f(Sim.State.PoseRoot)));
	}
	if (Controller.ChainAction1 > 0)
	{
		// 0x1613A0, 0x15F700
		Controller.ChainAction1 = 0;
		for (FApproachFilter* F : { &Sim.State.Brake, &Sim.State.Crouch })
		{
			F->Cur = F->Rate = F->Target = 0.0;
		}
	}
}

void FRiderAnimDriver::ComputePalette(const FRiderSim& Sim, FRiderPalette& Out) const
{
	Out.bValid = false;
	if (!bReady)
	{
		return;
	}
	const FRiderState& S = Sim.State;
	const double Scale = Sim.Profile.SizeScale;
	TArray<FPoseLayer> Layers;
	TArray<FBonePose> Body, Board;
	Controller.GatherLayers(0, Layers);
	BlendLayers(*Lib, *Set.Body, Layers, Scale, Scale, MaskShift, Body);
	Controller.GatherLayers(1, Layers);
	BlendLayers(*Lib, *Set.Board, Layers, Scale, 1.0, MaskShift, Board);

	const FRiderFrameInput In = FrameInputs(S, Scale);
	const double HipsZ = Body[0].Trans.Z;
	const FRowMat Root0 = RootFrame(In, HipsZ, false);
	Out.Board = BoardFrame(Root0, Board[0], In);
	// 0x129628, 0x15FDD0, 0x15EEC0
	const int32 Id = Controller.PoseCurrentId();
	const bool bPlainFK = (S.Control == ERiderControl::Void && Id != GateWait) || Id == 0x291 || Id == 0x2AB
		|| Controller.PoseCurrentCategory() == CategoryUber;
	if (bPlainFK)
	{
		BuildPaletteFK(*Set.Body, Root0, Body, Scale, Out.Body);
	}
	else
	{
		BuildPalette(*Lib, Set, RootFrame(In, HipsZ, true), Body, Out.Board, S.bStanceGoofy, Scale, Out.Body);
	}
	// 0x208400, 0x206818
	if (Parts)
	{
		BuildPartsPose(*Lib, *Parts, Controller, Out.Body, Scale, Out.Parts);
	}
	else
	{
		Out.Parts = FRiderPartsPose();
	}
	Out.bValid = true;
}

void FRiderAnimDriver::FeedPose(FRiderSim& Sim, bool bPosedSpheres) const
{
	FRiderPalette Pal;
	ComputePalette(Sim, Pal);
	FRiderState& St = Sim.State;
	St.FxBoardRow0 = St.BoardRow0;
	St.FxBoardRow1 = St.BoardRow1;
	St.FxBoardRow2 = St.BoardRow2;
	St.FxBoardOrigin = St.BoardOrigin;
	St.bFxBoardValid = St.bBoardValid;
	St.bBoardValid = Pal.bValid;
	if (Pal.bValid)
	{
		St.BoardRow0 = FVector(Pal.Board.Row(0));
		St.BoardRow1 = FVector(Pal.Board.Row(1));
		St.BoardRow2 = FVector(Pal.Board.Row(2));
		St.BoardOrigin = FVector(Pal.Board.Row(3));
	}
	// 0x2361B0
	TArray<FVector, TInlineAllocator<32>> Bones;
	for (const FRowMat& M : Pal.Body)
	{
		Bones.Add(FVector(M.Row(3)));
	}
	for (int32 K = 0; K < FRiderState::FxPoseBoneCount; ++K) { St.FxPoseBones[K] = St.PoseBones[K]; }
	St.bFxPoseBonesValid = St.bPoseBonesValid;
	St.bPoseBonesValid = Pal.bValid && Bones.Num() >= FRiderState::FxPoseBoneCount;
	if (St.bPoseBonesValid)
	{
		for (int32 K = 0; K < FRiderState::FxPoseBoneCount; ++K) { St.PoseBones[K] = Bones[K]; }
	}
	St.bPosedSpheresValid = Pal.bValid && bPosedSpheres
		&& SSXRider::PosedBodyCentres(Bones.GetData(), Bones.Num(), St.Position, St.Orientation, St.PosedSpheres);
	if (St.bPosedSpheresValid)
	{
		St.PosedPivot = St.Orientation.GetNormalized().Inverse().RotateVector(Bones[SSXRider::RootBone] - St.Position);
	}
	if (IRiderRaceHooks* H = Sim.GetRaceHooks())
	{
		H->PoseFed(Sim);   // 0x126748
	}
}
}
