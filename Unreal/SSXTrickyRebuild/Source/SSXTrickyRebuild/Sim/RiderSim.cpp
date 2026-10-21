#include "Sim/RiderSim.h"
#include "Sim/EEFloat.h"
#include "Sim/Ground/RiderCollision.h"
#include "Sim/Ground/GroundControls.h"

#include "Sim/RiderPlaceholderStates.h"
#include "Sim/Air/AirMath.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/SurfaceTable.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Anim/RiderPose.h"
#include "HAL/IConsoleManager.h"
#include "SSXTrickyRebuild.h"

namespace
{
	using M = ERiderMotion;
	using C = ERiderControl;
	constexpr M NoM = M::None;
	constexpr C NoC = C::None;

	const FRiderTransition GTransitions[] = {
		{ TEXT("T01"), TEXT("reset (slot 9)"), true, M::Static, true, C::Void, TEXT("boarder reset: race-path start, heading = tangent, v = 0, frozen"), TEXT("A"), false },
		{ TEXT("T02"), TEXT("spawn routine"), true, M::Air, true, C::NaturalAir, TEXT("spawn with drop height > 0 (respawn 101.46 cm above the path)"), TEXT("A"), false },
		{ TEXT("T03"), TEXT("spawn routine"), true, M::Ground, true, C::Cruise, TEXT("spawn with drop height <= 0: snap with the ground probe"), TEXT("A"), false },
		{ TEXT("T04"), TEXT("spawn routine"), true, M::Static, true, C::Void, TEXT("initial placement flag set"), TEXT("A"), false },
		{ TEXT("T05"), TEXT("forced reset routine"), true, M::Static, true, C::ResetWait, TEXT("not finished; reset bit / surface 0 in a reset-rule control, stuck_a > 4.492, stuck_b > 12.0, z < floor - 10000, wipe-out timeout"), TEXT("A"), false },
		{ TEXT("T06"), TEXT("RESET_WAIT update"), false, NoM, false, NoC, TEXT("after 0.8 s respawn at the path point, v = tangent * 833.33, drop 101.46 -> T02"), TEXT("A"), false },
		{ TEXT("T07"), TEXT("tutorial lesson start"), true, M::Ground, true, C::Cruise, TEXT("LessonWait at frame 0, ground words from frame 1 (code not exported)"), TEXT("A"), true },
		{ TEXT("T08"), TEXT("lesson placement"), false, NoM, true, C::LessonWait, TEXT("SetControl(LessonWait) then pre-race placement"), TEXT("A"), false },
		{ TEXT("T10"), TEXT("CRUISE"), false, NoM, true, C::FinishLine, TEXT("finish_result >= 0 and push animation not playing"), TEXT("B"), false },
		{ TEXT("T11"), TEXT("CRUISE"), false, NoM, true, C::Prewind, TEXT("jump latch (press edge), prewind lock clear, brake == 0"), TEXT("B"), false },
		{ TEXT("T12"), TEXT("CHANGE_FAKIE"), false, NoM, true, C::FakieCruise, TEXT("animation reaches the fakie loop"), TEXT("B"), false },
		{ TEXT("T13"), TEXT("CHANGE_FAKIE"), false, NoM, true, C::Cruise, TEXT("riding or push loop playing and jump not held"), TEXT("B"), false },
		{ TEXT("T14"), TEXT("CHANGE_FAKIE"), false, NoM, true, C::Prewind, TEXT("riding or push loop playing and jump held"), TEXT("B"), false },
		{ TEXT("T15"), TEXT("BUMP"), false, NoM, true, C::Prewind, TEXT("bump animation layer changed and jump held"), TEXT("B"), false },
		{ TEXT("T16"), TEXT("BUMP"), false, NoM, true, C::Cruise, TEXT("bump animation layer changed"), TEXT("B"), false },
		{ TEXT("T17"), TEXT("FAKIE_CRUISE"), false, NoM, true, C::Cruise, TEXT("riding or push loop playing again"), TEXT("B"), false },
		{ TEXT("T18"), TEXT("LAND_HARD"), false, NoM, true, C::Cruise, TEXT("riding loop playing, jump not held"), TEXT("B"), false },
		{ TEXT("T19"), TEXT("LAND_HARD"), false, NoM, true, C::Prewind, TEXT("riding loop playing, jump held"), TEXT("B"), false },
		{ TEXT("T20"), TEXT("LAND_HARD"), false, NoM, true, C::FakieCruise, TEXT("fakie loop playing"), TEXT("B"), false },
		{ TEXT("T21"), TEXT("LAND_NORMAL"), false, NoM, true, C::Cruise, TEXT("crouch >= absorb = clamp(-0.00045 vn, 0.25, 1), jump not held"), TEXT("B"), false },
		{ TEXT("T22"), TEXT("LAND_NORMAL"), false, NoM, true, C::Prewind, TEXT("crouch >= absorb and jump held"), TEXT("B"), false },
		{ TEXT("T23"), TEXT("PREWIND"), false, NoM, true, C::Cruise, TEXT("jump not held and both prewind charges at rest"), TEXT("B"), false },
		{ TEXT("T24"), TEXT("PREWIND"), false, NoM, true, C::Jump, TEXT("jump released: launch_charge = crouch, crouch target 0 at 13.33/s"), TEXT("B"), false },
		{ TEXT("T25"), TEXT("JUMP"), true, M::Air, false, NoC, TEXT("crouch unwound: J = max(charge^2 (0.0985 + 0.787 S17) f_v(|v|), 630.88) along normalize(n + 0.2 f)"), TEXT("B"), false },
		{ TEXT("T26"), TEXT("after launch"), false, NoM, true, C::Spin, TEXT("take-off helper seeds spin/flip from the prewind filters"), TEXT("C"), false },
		{ TEXT("T27"), TEXT("GROUND motion"), false, NoM, true, C::ChangeFakie, TEXT("brake == 0 and v.contact_forward < -111.11 in CRUISE/FAKIE_CRUISE/LAND_NORMAL"), TEXT("B"), false },
		{ TEXT("T28"), TEXT("GROUND motion"), true, M::Air, true, C::NaturalAir, TEXT("no wall hit and (probe miss or ground_height > surface[6]); JUMP stays and enters SPIN"), TEXT("B"), false },
		{ TEXT("T29"), TEXT("GROUND/AIR motion"), true, M::RailSlide, true, C::RailSlide, TEXT("rail query hit inside the attach box, rail point not behind"), TEXT("B"), false },
		{ TEXT("T30"), TEXT("RAIL_SLIDE control"), false, NoM, true, C::Prewind, TEXT("jump latch set while grinding"), TEXT("B"), false },
		{ TEXT("T31"), TEXT("RAIL_SLIDE motion"), true, M::Air, true, C::Spin, TEXT("rail query miss or rail point outside the box; +277.78 cm/s away"), TEXT("B"), false },
		{ TEXT("T32"), TEXT("rail exit helper"), false, NoM, true, C::Void, TEXT("rail ends while control is RAIL_SLIDE"), TEXT("B"), false },
		{ TEXT("T40"), TEXT("SIT_AND_WAIT"), true, M::WipeOutRecover, true, C::GetupFromSit, TEXT("race phase == 4; sit-up animation 0x1FE"), TEXT("D"), false },
		{ TEXT("T41"), TEXT("GETUP_FROM_SIT"), true, M::Ground, true, C::Cruise, TEXT("sit-up done; v += 416.67 forward"), TEXT("D"), false },
		{ TEXT("T42"), TEXT("GATE_LAUNCH"), true, M::Ground, false, NoC, TEXT("speed == 0 and gate value >= push phase: v = (0.4876 + 0.7181 S15) gate_push forward"), TEXT("B"), false },
		{ TEXT("T43"), TEXT("GATE_LAUNCH"), true, M::Ground, true, C::GateToBase, TEXT("gate value reaches 1"), TEXT("B"), false },
		{ TEXT("T44"), TEXT("GATE_TO_BASE"), true, M::Ground, true, C::Cruise, TEXT("reset clear, surface != 0, push animation finished"), TEXT("B"), false },
		{ TEXT("T45"), TEXT("FINISH_LINE"), true, M::Static, false, NoC, TEXT("stopped after the finish; speed rule not decoded"), TEXT("A"), true },
		{ TEXT("T46"), TEXT("race handler enter (GO)"), false, NoM, true, C::GateLaunch, TEXT("every racer in GATE_ANTICIPATE switches at race start"), TEXT("A"), false },
		{ TEXT("T50"), TEXT("NATURAL_AIR"), false, NoM, true, C::Spin, TEXT("filters settled and (turn or crouch < 0.5 or trick pressed)"), TEXT("C"), false },
		{ TEXT("T51"), TEXT("SPIN late trick"), false, NoM, true, C::Spin, TEXT("phase 2, no grab, slow rates, stick pressed: VOID then SPIN"), TEXT("C"), false },
		{ TEXT("T52"), TEXT("AIR touchdown"), true, M::Ground, true, C::LandNormal, TEXT("landing passes and (landable grab or phi >= -45 deg and n.v >= -2777.1)"), TEXT("C"), false },
		{ TEXT("T53"), TEXT("AIR touchdown"), true, M::Ground, true, C::LandHard, TEXT("landing passes but phi < -45 deg (nose up) or n.v < -2777.1"), TEXT("C"), false },
		{ TEXT("T54"), TEXT("landing test"), false, NoM, true, C::Void, TEXT("touchdown on a surface other than 0/6/10: SetControl(Void) folds trick angles"), TEXT("C"), false },
		{ TEXT("T60"), TEXT("wipe-out entry"), true, M::WipeOut, true, C::WipeOut, TEXT("landing fails, air wall hit not a glance, grounded on 6/10, rider hit > knock-down, wall bounce too hard"), TEXT("D"), false },
		{ TEXT("T61"), TEXT("WIPE_OUT tumble (airborne)"), true, M::Air, true, C::NaturalAir, TEXT("air_time > 0.1, predicted > 0.1, landing surface not 6/10, upright, |w| < 4"), TEXT("D"), false },
		{ TEXT("T62"), TEXT("WIPE_OUT tumble (grounded)"), true, M::Ground, true, C::Cruise, TEXT("total > 0.5 s, upright, slow spin, probe hits a non-bail surface"), TEXT("D"), false },
		{ TEXT("T63"), TEXT("WIPE_OUT slide / timeout"), true, M::WipeOutRecover, true, C::WipeOutRecover, TEXT("lying flat and |v| < min(833.33 + 694.44 t, 2222.22), or timeout after finish"), TEXT("D"), false },
		{ TEXT("T64"), TEXT("WIPE_OUT_RECOVER control"), true, M::Ground, true, C::Cruise, TEXT("riding loop 0x1FF or stand loop 0x221 reached"), TEXT("D"), false },
		{ TEXT("T65"), TEXT("WIPE_OUT_RECOVER control"), true, M::Ground, true, C::FakieCruise, TEXT("fakie loop 0x202 reached"), TEXT("D"), false },
		{ TEXT("T66"), TEXT("rider collision"), true, M::Air, true, C::NaturalAir, TEXT("grounded target, |J| > 1000, heading ok, away from ground: knock-up"), TEXT("D"), false },
		{ TEXT("T67"), TEXT("rider collision"), false, NoM, true, C::Bump, TEXT("167.32 < |J| <= knock-down, target in CRUISE/PREWIND; boost -0.02"), TEXT("D"), false },
	};

	FORCEINLINE FVector SafeUnit(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}
}

FRiderSim::FRiderSim()
{
	InstallPlaceholders();
	Events.Reserve(256);
}

FRiderSim::~FRiderSim() = default;

void FRiderSim::InstallPlaceholders()
{
	using namespace SSXSimPlaceholders;
	MotionStates[int32(M::Air)] = MakeShared<FPlaceholderAirMotion>();
	MotionStates[int32(M::Ground)] = MakeShared<FPlaceholderGroundMotion>();
	MotionStates[int32(M::RailSlide)] = MakeShared<FPlaceholderRailMotion>();
	const TSharedPtr<IRiderControlState> Ground = MakeShared<FPlaceholderGroundControl>();
	for (C Ctl : { C::Bump, C::Cruise, C::ChangeFakie, C::FakieCruise, C::GateToBase, C::LandHard, C::LandNormal,
	               C::Prewind, C::Jump, C::GetupFromSit })
	{
		ControlStates[int32(Ctl)] = Ground;
	}
	ControlStates[int32(C::LessonWait)] = MakeShared<FPlaceholderLessonWaitControl>();
	ControlStates[int32(C::ResetWait)] = MakeShared<FPlaceholderResetWaitControl>();
	InstallWipeOutStates(*this);
}

void FRiderSim::SetMotionState(ERiderMotion Id, TSharedPtr<IRiderMotionState> Handler)
{
	MotionStates[int32(Id)] = MoveTemp(Handler);
}

void FRiderSim::SetControlState(ERiderControl Id, TSharedPtr<IRiderControlState> Handler)
{
	ControlStates[int32(Id)] = MoveTemp(Handler);
}

IRiderMotionState* FRiderSim::GetMotionState(ERiderMotion Id) const
{
	return MotionStates[int32(Id)].Get();
}

IRiderControlState* FRiderSim::GetControlState(ERiderControl Id) const
{
	return ControlStates[int32(Id)].Get();
}

void FRiderSim::SaveReplay(FRiderSimKeyframe& Out) const
{
	Out.State = State;
	Out.TakeOffCount = TakeOffCount;
	TSet<const void*> Seen;
	Out.Motions.SetNum(int32(ERiderMotion::Count));
	for (int32 I = 0; I < int32(ERiderMotion::Count); ++I)
	{
		const IRiderMotionState* H = MotionStates[I].Get();
		bool bDup = false;
		Seen.Add(H, &bDup);
		Out.Motions[I] = H && !bDup ? H->SaveState() : nullptr;
	}
	Out.Controls.SetNum(int32(ERiderControl::Count));
	for (int32 I = 0; I < int32(ERiderControl::Count); ++I)
	{
		const IRiderControlState* H = ControlStates[I].Get();
		bool bDup = false;
		Seen.Add(H, &bDup);
		Out.Controls[I] = H && !bDup ? H->SaveState() : nullptr;
	}
}

void FRiderSim::LoadReplay(const FRiderSimKeyframe& In)
{
	State = In.State;
	TakeOffCount = In.TakeOffCount;
	for (int32 I = 0; I < In.Motions.Num() && I < int32(ERiderMotion::Count); ++I)
	{
		if (In.Motions[I] && MotionStates[I]) { MotionStates[I]->LoadState(*In.Motions[I]); }
	}
	for (int32 I = 0; I < In.Controls.Num() && I < int32(ERiderControl::Count); ++I)
	{
		if (In.Controls[I] && ControlStates[I]) { ControlStates[I]->LoadState(*In.Controls[I]); }
	}
}

TConstArrayView<FRiderTransition> FRiderSim::GetTransitionTable()
{
	return MakeArrayView(GTransitions, UE_ARRAY_COUNT(GTransitions));
}

const FRiderTransition* FRiderSim::FindTransition(const TCHAR* Id)
{
	for (const FRiderTransition& T : GTransitions)
	{
		if (FCString::Strcmp(T.Id, Id) == 0)
		{
			return &T;
		}
	}
	return nullptr;
}

bool FRiderSim::IsResetRuleControl(ERiderControl Ctl)
{
	switch (Ctl)
	{
	case C::Bump: case C::Cruise: case C::ChangeFakie: case C::FakieCruise: case C::GateToBase: case C::Jump:
	case C::LandHard: case C::LandNormal: case C::NaturalAir: case C::Prewind: case C::RailSlide: case C::Spin:
	case C::WipeOut: case C::WipeOutRecover:
		return true;
	default:
		return false;
	}
}

bool FRiderSim::ApplyTransition(const TCHAR* Id)
{
	const FRiderTransition* T = FindTransition(Id);
	if (!T)
	{
		return false;
	}
	if (T->bSetsMotion)
	{
		SetMotion(T->Motion, T->Id);
	}
	if (T->bSetsControl)
	{
		SetControl(T->Control, T->Id);
	}
	return true;
}

#if !UE_BUILD_SHIPPING
namespace
{
	TAutoConsoleVariable<int32> CVarRiderStateLog(TEXT("ssx.Rider.StateLog"), 1,
		TEXT("Rider motion / control transition log (one line per transition with its cause, never per frame; a transition ")
		TEXT("repeated within a second is counted and summarised on the next line it logs). 0: off (Verbose only), ")
		TEXT("1 (default): the player's rider at Log, 2: every rider."));
}
#endif

void FRiderSim::LogEvent(bool bMotion, uint8 From, uint8 To, const TCHAR* Reason)
{
#if !UE_BUILD_SHIPPING
	const int32 Mode = CVarRiderStateLog.GetValueOnAnyThread();
	const bool bLog = Mode >= 2 || (Mode == 1 && bLogTransitions);
	if (bLog || UE_LOG_ACTIVE(LogSSX, Verbose))
	{
		const TCHAR* Kind = bMotion ? TEXT("motion") : TEXT("control");
		const TCHAR* A = bMotion ? LexToString(ERiderMotion(From)) : LexToString(ERiderControl(From));
		const TCHAR* B = bMotion ? LexToString(ERiderMotion(To)) : LexToString(ERiderControl(To));
		const TCHAR* Why = Reason ? Reason : TEXT("");
		// rate limit: the same transition (kind, from, to, cause) at most once per second, with the repeats counted
		const uint64 Key = (uint64(bMotion) << 56) | (uint64(From) << 48) | (uint64(To) << 40) | (uint64(GetTypeHash(FStringView(Why))) & 0xFFFFFFFFFFull);
		FLogRepeat& Rep = LogRepeats.FindOrAdd(Key);
		if (State.Frame - Rep.LastFrame < 60 && State.Frame >= Rep.LastFrame)
		{
			++Rep.Suppressed;
		}
		else
		{
			const FString More = Rep.Suppressed > 0 ? FString::Printf(TEXT(" [+%d repeats since f%d]"), Rep.Suppressed, Rep.LastFrame) : FString();
			if (bLog)
			{
				UE_LOG(LogSSX, Log, TEXT("[rider] %s f%d %s %s -> %s (%s)%s"), *DebugName, State.Frame, Kind, A, B, Why, *More);
			}
			else
			{
				UE_LOG(LogSSX, Verbose, TEXT("[rider] %s f%d %s %s -> %s (%s)%s"), *DebugName, State.Frame, Kind, A, B, Why, *More);
			}
			Rep.LastFrame = State.Frame;
			Rep.Suppressed = 0;
		}
	}
#endif
	if (Events.Num() >= 4096)
	{
		Events.RemoveAt(0, 1024, EAllowShrinking::No);
	}
	FRiderSimEvent& E = Events.AddDefaulted_GetRef();
	E.Frame = State.Frame;
	E.bMotion = bMotion;
	E.From = From;
	E.To = To;
	E.Reason = Reason;
}

void FRiderSim::LogNote(const FString& Text)
{
#if !UE_BUILD_SHIPPING
	const int32 Mode = CVarRiderStateLog.GetValueOnAnyThread();
	if (Mode >= 2 || (Mode == 1 && bLogTransitions))
	{
		UE_LOG(LogSSX, Log, TEXT("[rider] %s f%d %s"), *DebugName, State.Frame, *Text);
	}
	else
	{
		UE_LOG(LogSSX, Verbose, TEXT("[rider] %s f%d %s"), *DebugName, State.Frame, *Text);
	}
#endif
	if (Notes.Num() >= 64)
	{
		Notes.RemoveAt(0, 16, EAllowShrinking::No);
	}
	Notes.Add(Text);
}

void FRiderSim::SetMotion(ERiderMotion NewMotion, const TCHAR* Reason)
{
	const ERiderMotion Old = State.Motion;
	if (NewMotion == Old)
	{
		return;
	}
	if (IRiderMotionState* H = MotionStates[int32(Old)].Get())
	{
		H->Exit(*this, NewMotion);
	}
	State.Motion = NewMotion;
	State.MotionTime = 0.0;
	if (NewMotion == M::Air && (Old == M::Ground || Old == M::RailSlide))
	{
		++TakeOffCount;
	}
	LogEvent(true, uint8(Old), uint8(NewMotion), Reason);
	if (NewMotion == M::Ground || NewMotion == M::RailSlide)
	{
		ResetsWithoutRiding = 0;
		bWatchdogResetLoop = false;
	}
	if (IRiderMotionState* H = MotionStates[int32(NewMotion)].Get())
	{
		H->Enter(*this, Old);
	}
}

void FRiderSim::SetControl(ERiderControl NewControl, const TCHAR* Reason)
{
	const ERiderControl Old = State.Control;
	if (NewControl == Old)
	{
		return;
	}
	if (IRiderControlState* H = ControlStates[int32(Old)].Get())
	{
		H->Exit(*this, NewControl);
	}
	State.Control = NewControl;
	State.ControlTime = 0.0;
	LogEvent(false, uint8(Old), uint8(NewControl), Reason);
	WatchdogResetWait(Old, NewControl);
	if (IRiderControlState* H = ControlStates[int32(NewControl)].Get())
	{
		H->Enter(*this, Old);
	}
}

const FSurfaceParams& FRiderSim::Surface() const
{
	return Surface(State.SurfaceId);
}

const FSurfaceParams& FRiderSim::Surface(int32 SurfaceId) const
{
	static const FSurfaceTable Empty;
	return World ? World->SurfaceParams(SurfaceId) : Empty.Get(SurfaceId);
}

bool FRiderSim::RunGroundProbe(FGroundProbeResult* OutResult)
{
	FGroundProbeResult R;
	FGroundProbeInput In;
	In.Position = State.Position;
	In.GroundNormal = State.GroundNormal;
	In.Forward = State.BasisForward;
	In.ContactLeft = State.ContactLeft;
	In.LeanDeg = State.Lean.Cur;
	In.RiderScale = Profile.SizeScale;
	const bool bHit = World && World->ProbeGround(In, R);
	if (!World)
	{
		R.GroundNormal = FVector::UpVector;
		R.ContactForward = SafeUnit(FVector(State.BasisForward.X, State.BasisForward.Y, 0.0), FVector(0, 1, 0));
		R.ContactLeft = FVector::CrossProduct(FVector::UpVector, R.ContactForward);
		R.FallLine = State.BasisForward;
		R.ContactKind = 3;
	}
	if (bHit)
	{
		if (R.Surface != State.SurfaceId)
		{
			State.SurfaceChanged = int32(0x80000001u);
		}
		else if (State.SurfaceChanged >= 0)
		{
			State.SurfaceChanged = 0;
		}
		State.SurfaceId = R.Surface;
		State.ContactPoint = R.ContactPoint;
		State.GroundHeight = R.Height;
	}
	State.GroundNormal = R.GroundNormal;
	State.ContactForward = R.ContactForward;
	State.ContactLeft = R.ContactLeft;
	State.FallLine = R.FallLine;
	State.ContactKind = R.ContactKind;
	LastProbe = R;
	if (OutResult)
	{
		*OutResult = R;
	}
	return bHit;
}

void FRiderSim::StartWipeOut(const FRiderImpact& Impact, const TCHAR* Reason, bool bTrickPending)
{
	// 0x11D830
	if (bTrickPending && State.TrickyTimer > 0.0 && !State.bInfiniteBoost)
	{
		State.BoostMeter = FMath::Min(State.BoostMeter, double(0.66599226f));
		State.TrickyTimer = 0.0;
	}
	State.LastImpact = Impact;
	State.SurfaceBeforeWipeout = State.SurfaceId;
	SetControl(C::WipeOut, Reason);
	SetMotion(M::WipeOut, Reason);
	State.Highlight(15, 2.f);
	State.Highlight(17, float(Impact.Normal.Size()));
}

void FRiderSim::ForcedReset(bool bAuto, const TCHAR* Reason)
{
	// 0x118F10
	State.bAutoReset = bAuto;
	if (State.FinishResult >= 0)
	{
		return;
	}
	if (RaceHooks && !RaceHooks->OnForcedReset(*this))
	{
		return;
	}
	++State.ForcedResetCount;   // 0x218EB0
	State.ResetDropHeight = RespawnDrop;
	// 0x106A90
	if (State.Control != C::ResetWait)
	{
		State.ResetWaitTime = 0.0;
	}
	// 0x117198
	SetMotion(M::Static, Reason);
	SetControl(C::ResetWait, Reason);
}

double FRiderSim::GetWorldFloor() const
{
	if (bHasWorldFloor)
	{
		return WorldFloorZ;
	}
	return World ? World->FloorZ() : -TNumericLimits<double>::Max();
}

void FRiderSim::Respawn()
{
	// 0x118C08, 0x119220, 0x119D30
	++State.RespawnSerial;
	FVector HookPos, HookHeading, HookVel;
	if (RaceHooks && RaceHooks->PlaceRespawn(*this, HookPos, HookHeading, HookVel))
	{
		if (!State.bInfiniteBoost)
		{
			State.TrickyTimer = 0.0;
		}
		Spawn(HookPos, HookHeading, HookVel, State.ResetDropHeight, false);
		PayRespawn();
		return;
	}
	const FVector Heading = SafeUnit(FVector(State.LastGroundHeading.X, State.LastGroundHeading.Y, 0.0), FVector(0, 1, 0));
	if (State.TrickyTimer > 0.0 && !State.bInfiniteBoost)
	{
		State.TrickyTimer = 0.0;
	}
	Spawn(State.LastGroundPosition, Heading, Heading * RespawnSpeed, State.ResetDropHeight, false);
	PayRespawn();
}

void FRiderSim::PayRespawn()
{
	// 0x11CAE8, 0x155AA8
	if (!State.bAutoReset)
	{
		SSXAir::BoostAdd(State, SSXAir::RespawnMeter, SSXAir::BoostGain(Profile));
	}
	State.bAutoReset = false;
	State.PositiveReactionFrame = -1;   // 0x11F3B8
}

void FRiderSim::SetOrientationFromAxes(const FVector& Forward, const FVector& Up)
{
	const FVector F = SafeUnit(Forward, FVector(0, 1, 0));
	FVector U = Up - FVector::DotProduct(Up, F) * F;
	U = SafeUnit(U, FVector::UpVector);
	const FVector Side = FVector::CrossProduct(F, U);   // right axis: X = Y x Z
	const FMatrix Mtx(Side, F, U, FVector::ZeroVector);
	State.Orientation = FQuat(Mtx).GetNormalized();
	UpdateBasis();
}

void FRiderSim::RotateBody(const FVector& Axis, double Angle)
{
	const FVector A = SafeUnit(Axis, FVector::UpVector);
	RotateBodyEE(FVector3f(float(A.X), float(A.Y), float(A.Z)), float(Angle));
}

// 0x128E90, 0x129008, 0x250C10, 0x2510F8
void FRiderSim::RotateBodyEE(const FVector3f& Axis, float Angle)
{
	float S, Cs;
	SSXAnim::SinCosEE(Angle * 0.5f, S, Cs);
	const float Ax = S * Axis.X, Ay = S * Axis.Y, Az = S * Axis.Z;
	const float W = float(State.Orientation.W), X = float(State.Orientation.X), Y = float(State.Orientation.Y), Z = float(State.Orientation.Z);
	using SSXEE::FpuAdd; using SSXEE::FpuSub;
	const float Nx = FpuSub(FpuAdd(FpuAdd(Cs * X, Ax * W), Ay * Z), Az * Y);
	const float Ny = FpuSub(FpuAdd(FpuAdd(Cs * Y, Ay * W), Az * X), Ax * Z);
	const float Nz = FpuSub(FpuAdd(FpuAdd(Cs * Z, Az * W), Ax * Y), Ay * X);
	const float Nw = FpuSub(FpuSub(FpuSub(Cs * W, Ax * X), Ay * Y), Az * Z);
	State.Orientation = FQuat(Nx, Ny, Nz, Nw);
	UpdateBasis();
}

void FRiderSim::UpdateBasis()
{
	float W = float(State.Orientation.W), X = float(State.Orientation.X), Y = float(State.Orientation.Y), Z = float(State.Orientation.Z);
	using SSXEE::FpuAdd; using SSXEE::FpuSub;   // 0x129008
	const float Len = float(FMath::Sqrt(double(FpuAdd(FpuAdd(FpuAdd(X * X, Y * Y), Z * Z), W * W))));
	if (Len == 0.0f)
	{
		W = 1.0f;
	}
	else
	{
		const float Inv = 1.0f / Len;
		W = W * Inv; X = X * Inv; Y = Y * Inv; Z = Z * Inv;
	}
	State.Orientation = FQuat(X, Y, Z, W);
	// 0x102F80
	const float W2 = FpuAdd(W, W);
	auto Rotate = [&](float Ex, float Ey, float Ez)
	{
		const float Cx = FpuSub(Y * Ez, Z * Ey), Cy = FpuSub(Z * Ex, X * Ez), Cz = FpuSub(X * Ey, Y * Ex);
		const float Dx = FpuSub(Y * Cz, Z * Cy), Dy = FpuSub(Z * Cx, X * Cz), Dz = FpuSub(X * Cy, Y * Cx);
		const float Tx = FpuAdd(Ex, W2 * Cx), Ty = FpuAdd(Ey, W2 * Cy), Tz = FpuAdd(Ez, W2 * Cz);
		return FVector(FpuAdd(Tx, 2.0f * Dx), FpuAdd(Ty, 2.0f * Dy), FpuAdd(Tz, 2.0f * Dz));
	};
	State.BasisSide = Rotate(1.0f, 0.0f, 0.0f);
	State.BasisForward = Rotate(0.0f, 1.0f, 0.0f);
	State.BasisUp = Rotate(0.0f, 0.0f, 1.0f);
	State.Yaw = SSXAnim::Atan2EE(float(State.BasisForward.Y), float(State.BasisForward.X));
}

void FRiderSim::Reset()
{
	const int32 KeepFrame = State.Frame;
	State = FRiderState();
	State.Frame = KeepFrame;
	State.Motion = M::None;
	State.Control = C::None;
	State.BoardClass = Profile.BoardClass;
	State.bStanceGoofy = Profile.Stance != 0;
	State.BoostMeter = Profile.InitialBoost;
	State.SurfaceId = 1;
	State.SurfaceP7 = Surface(1).SinkDepth();
	State.SurfaceP8 = Surface(1).SinkMax();
	State.TrickId = 0x1A;
	State.FinishResult = -1;
	State.RenderAlpha = 1.0;
	State.TimeScale = 1.0;
	ResetsWithoutRiding = 0;
	bWatchdogResetLoop = false;
	UpdateBasis();
	SetMotion(M::Static, TEXT("T01"));
	SetControl(C::Void, TEXT("T01"));
}

void FRiderSim::Spawn(const FVector& Position, const FVector& Heading, const FVector& Velocity, double DropHeight, bool bInitial)
{
	SpawnYaw(Position, SSXAnim::Atan2EE(float(Heading.Y), float(Heading.X)), Velocity, DropHeight, bInitial);
}

void FRiderSim::SpawnYaw(const FVector& Position, float Yaw, const FVector& Velocity, double DropHeight, bool bInitial)
{
	// 0x11CAE8, 0x11F3B8, 0x11F3F8
	State.Position = Position;
	State.Velocity = Velocity;
	State.SpeedCap = 0.0;
	// 0x128E20, 0x128E90, 0x129008
	State.Orientation = FQuat::Identity;
	RotateBodyEE(FVector3f(0.0f, 0.0f, 1.0f), -1.5707964f);
	RotateBodyEE(FVector3f(0.0f, 0.0f, 1.0f), Yaw);
	const FVector H = SafeUnit(FVector(State.BasisForward.X, State.BasisForward.Y, 0.0), FVector(0, 1, 0));
	State.SpinAngle = 0.0;
	State.FlipAngle = 0.0;
	State.GrindType = 0;
	for (FApproachFilter* F : { &State.Ramp1F0, &State.Brake, &State.Crouch, &State.Steer, &State.SteerFollow,
	                            &State.PrewindA, &State.PrewindB, &State.Lean, &State.BoostPose, &State.BoostFlash,
	                            &State.GateLaunch })
	{
		F->Reset();
	}
	State.AirTurnOffset = State.AirTiltOffset = State.AirTurnSmoothed = State.AirTiltSmoothed = 0.0;
	State.GroundNormal = FVector::UpVector;
	// 0x116AB8
	State.LastGroundPosition = Position;
	State.LastGroundHeading = H;
	State.RenderAlpha = 1.0;
	State.ResetFade = 0.0;

	if (!bInitial && World)
	{
		// 0x11CC8C, 0x11CDC4, 0x25AFF8
		FRiderWorldHit Hit;
		const float Zf = float(Position.Z);
		const FVector Bottom(float(Position.X), float(Position.Y), Zf - (float(DropHeight) + 200.0f));
		const FVector Top(float(Position.X), float(Position.Y), Zf + 200.0f);
		if (World->LineQuery(Bottom, Top, ERiderLineQuery::FineRefined, 0.0, Hit))
		{
			State.Position = Hit.Point;
			State.GroundNormal = Hit.Normal;
			// 0x250FD0
			const FVector3f Nf(Hit.Normal), Uf(State.BasisUp);
			const FVector3f C(Nf.Y * Uf.Z - Nf.Z * Uf.Y, Nf.Z * Uf.X - Nf.X * Uf.Z, Nf.X * Uf.Y - Nf.Y * Uf.X);
			const float L = float(FMath::Sqrt(double((C.X * C.X + C.Y * C.Y) + C.Z * C.Z)));
			if (L > 1e-5f)
			{
				const float Q = 1.0f / L;
				RotateBodyEE(FVector3f(C.X * Q, C.Y * Q, C.Z * Q), -SSXAnim::AsinEE(FMath::Min(L, 0.99999f)));
			}
		}
	}
	else if (bInitial)
	{
		// 0x11CC6C, 0x118298, 0x119D30
	}
	// 0x11CDC8
	State.Position.Z += DropHeight;
	State.ContactLeft = FVector::ZeroVector;
	State.SmoothedUp = State.GroundNormal;
	State.SpawnPosition = State.Position;
	State.ContactPoint = State.Position;
	if (!bInitial)
	{
		// 0x15EE30
		if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(*this))
		{
			Ch->bResetAnim = true;
			Ch->ClearPlayRequests();
			Ch->ScenePlays.Reset();
		}
		State.Ch1Anim = 0x300;
		State.Ch1EndFrame = 0;
		State.Ch0Anim = 0x300;
	}

	if (bInitial)
	{
		SetMotion(M::Static, TEXT("T04"));
		SetControl(C::Void, TEXT("T04"));
	}
	else if (DropHeight > 0.0)
	{
		SetMotion(M::Air, TEXT("T02"));
		SetControl(C::NaturalAir, TEXT("T02"));
		// 0x1272E8
		State.TrickPhase = 3;
	}
	else
	{
		RunGroundProbe();
		SetMotion(M::Ground, TEXT("T03"));
		SetControl(C::Cruise, TEXT("T03"));
	}
	// 0x126748
	if (RaceHooks)
	{
		RaceHooks->SphereSetRebuilt(*this, true);
	}
	State.Highlight(19);
}

void FRiderSim::TickFilters()
{
	FRiderState& S = State;
	S.ForwardDistance = SSXEE::FpuAdd(float(S.ForwardDistance), SSXEE::Dot(SSXEE::F(S.Velocity), SSXEE::F(S.BasisForward)) * (float(S.TimeScale) / 60.0f));

	for (FApproachFilter* F : { &S.Ramp1F0, &S.Brake, &S.Crouch, &S.Steer, &S.SteerFollow, &S.PrewindA, &S.PrewindB,
	                            &S.Lean, &S.BoostPose, &S.BoostFlash, &S.RailBox })   // 0x117198
	{
		F->Tick(Dt);
	}
	auto Approach = [](double& Cur, double Target, double Rate)
	{
		const float C = float(Cur), T = float(Target), S = float(Rate * Dt);
		if (SSXEE::FpuAdd(T, S) < C) { Cur = SSXEE::FpuSub(C, S); }
		else if (C < SSXEE::FpuSub(T, S)) { Cur = SSXEE::FpuAdd(C, S); }
		else { Cur = Target; }
	};
	Approach(S.AirTiltSmoothed, S.AirTiltOffset, 5.0);
	Approach(S.AirTurnSmoothed, S.AirTurnOffset, 5.0);
	Approach(S.SurfaceP10, S.Motion == M::Ground ? Surface().P10() : 0.0, 100.0);

	Approach(S.SurfaceP7, Surface().SinkDepth(), 100.0);
	Approach(S.SurfaceP8, Surface().SinkMax(), 100.0);
	Approach(S.GroundBlend, S.Motion == M::Ground ? 1.0 : 0.0, 0.1);

	{
		const float B = SSXEE::FpuSub(float(S.BoostBurstTimer), 0.016666668f), L = SSXEE::FpuSub(float(S.LaunchBoostTimer), 0.016666668f);
		S.BoostBurstTimer = B <= 0.0f ? 0.0 : B;
		S.LaunchBoostTimer = L <= 0.0f ? 0.0 : L;
	}
}

void FRiderSim::ApplySpeedCap()
{
	FRiderState& S = State;
	// 0x117968
	auto Bits = [](uint32 U) { float F; FMemory::Memcpy(&F, &U, 4); return F; };
	const float Speed = SSXEE::Length(SSXEE::F(S.Velocity));
	float Tier = Bits(0x452E4D6Eu);
	if (0.0f < float(S.BoostBurstTimer))
	{
		Tier = Bits(0x4551338Eu);
	}
	else if (0.0f < float(S.BoostStrength))
	{
		const float Meter = float(S.BoostMeter);
		if (Bits(0x3F2A7E78u) < Meter) { Tier = Bits(0x4551338Eu); }
		else if (Bits(0x3EAAD18Bu) < Meter) { Tier = Bits(0x45400245u); }
		else if (0.0f < Meter) { Tier = Bits(0x453746E1u); }
	}
	const float Dropped = SSXEE::FpuSub(float(S.SpeedCap), Bits(0x405E38E3u));
	const float Cap = Tier < Dropped ? Dropped : Tier;
	S.SpeedCap = Cap;
	if (Cap < Speed)
	{
		S.Velocity = SSXEE::D(SSXEE::Scale(Cap / Speed, SSXEE::F(S.Velocity)));
	}
}

double FRiderSim::TrickyTimerStep(double T, bool bAir, double Step)
{
	return (bAir && T > 0.0 && T <= Step) ? Step : FMath::Max(0.0, T - Step);
}

void FRiderSim::RaceHandler()
{
	FRiderState& S = State;
	S.TrickyTimer = TrickyTimerStep(S.TrickyTimer, S.Motion == M::Air, Dt);
	if (S.bInfiniteBoost)
	{
		S.TrickyTimer = 1.0;
		S.BoostMeter = 1.0;
	}
	else if (S.bRacing && S.FinishResult < 0 && S.BoostMeter != 0.0)   // 0x11B1F8
	{
		const double Stat = Profile.Stat(0x1B);
		S.BoostMeter = FMath::Max(0.0, S.BoostMeter - (PassiveDrainA + PassiveDrainB * Stat) * PassiveDrainK);
	}
}

void FRiderSim::LeanAndUp()
{
	FRiderState& S = State;
	// 0x129500
	if (S.Motion == M::RailSlide)
	{
		S.Lean.Target = 0.0;
	}
	else
	{
		const float F = SSXEE::Length(SSXEE::F(S.Velocity)) * 0.69444442f;
		float T = float(S.Steer.Cur) * float(LeanPerSteer);
		if (F < 1.0f)
		{
			T = T * F;
		}
		S.Lean.Target = T;
	}
	S.Lean.Rate = LeanRate;
	S.SmoothedUp = SSXEE::D(SSXEE::Normalize(SSXEE::Add(SSXEE::F(S.SmoothedUp), SSXEE::Scale(0.5f, SSXEE::F(S.GroundNormal)))));
}

void FRiderSim::ApplyResetRule(ERiderControl Ran, ERiderControl After, const FRiderInputWord& Word)
{
	// 0x106A90, 0x102FC8, 0x1041B0, 0x104030, 0x1043F8, 0x105DC0, 0x1046B0, 0x104800, 0x104E68, 0x1050E8, 0x108140, 0x108198, 0x118F10, 0x1000E0, 0x1073E0, 0x100908
	if (!IsResetRuleControl(Ran))
	{
		return;
	}
	if ((Ran == C::Cruise && (After == C::FinishLine || After == C::Prewind))
		|| (Ran == C::Prewind && (After == C::Cruise || After == C::Jump)))
	{
		return;
	}
	const bool bAirLayout = Ran == C::NaturalAir || Ran == C::RailSlide || Ran == C::Spin;
	if (bAirLayout)
	{
		if (Word.bResetRequest)
		{
			ForcedReset(State.bAutoReset, TEXT("T05 reset rule (Select)"));
		}
		return;
	}
	if (Word.bResetRequest || State.SurfaceId == OutOfBoundsSurface)
	{
		ForcedReset(!Word.bResetRequest, Word.bResetRequest ? TEXT("T05 reset rule (Select)") : TEXT("T05 reset rule (surface 0)"));
	}
}

void FRiderSim::Step(const FRiderInputWord& Input)
{
	FRiderState& S = State;

	TickFilters();

	S.bWallHitThisFrame = false;
	if (IRiderMotionState* Motion = MotionStates[int32(S.Motion)].Get())
	{
		Motion->Update(*this);
	}

	if (S.Motion != M::Air)
	{
		ApplySpeedCap();
	}

	if (RaceHooks)
	{
		RaceHooks->TrackScan(*this);
	}

	FRiderInputWord Source = Input;
	if (RaceHooks)
	{
		RaceHooks->BuildInput(*this, Source);
	}
	const FRiderInputWord Word = Source.MaskedFor(S.Control);
	const ERiderControl Ran = S.Control;   // 0x11C9A0
	if (IRiderControlState* Control = ControlStates[int32(Ran)].Get())
	{
		Control->Update(*this, Word);
	}
	// 0x127918
	SSXRiderHit::TickShove(*this);
	SSXRiderHit::ShoveInput(*this, Word.ShoveSide, Word.ShoveFwd);
	if (S.bLandBobbleCall)
	{
		// 0x127D08, 0x1000E0, 0x100908
		S.bLandBobbleCall = false;
		if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(*this))
		{
			Ch->LandBobble(*this);
		}
	}
	if (Ran == C::Cruise && S.Control == C::Cruise)
	{
		SSXGround::CruiseOverlays(*this);   // 0x102FC8
	}
	ApplyResetRule(Ran, S.Control, Word);

	// 0x117198, 0x118F10
	S.LastImpactNormal = SSXEE::D(SSXEE::Scale(float(ImpactNormalDecay), SSXEE::F(S.LastImpactNormal)));
	S.StuckAccumA = double(float(S.StuckAccumA) * float(StuckADecay));
	if (float(StuckALimit) < float(S.StuckAccumA))
	{
		S.bAutoReset = true;
		ForcedReset(true, TEXT("T05 stuck_a"));
		S.StuckAccumA = 0.0;
	}
	S.StuckAccumB = double(float(S.StuckAccumB) * float(StuckBDecay));
	if (float(StuckBLimit) < float(S.StuckAccumB))
	{
		S.bAutoReset = true;
		ForcedReset(true, TEXT("T05 stuck_b"));
		S.StuckAccumB = 0.0;
	}
	if ((bHasWorldFloor || World) && S.Position.Z < GetWorldFloor() - FloorMargin)
	{
		S.bAutoReset = true;
		ForcedReset(true, TEXT("T05 floor"));
	}

	RaceHandler();

	if (RaceHooks)
	{
		RaceHooks->RiderCollision(*this);
	}

	LeanAndUp();

	S.MotionTime += Dt;
	S.ControlTime += Dt;
	++S.Frame;
	if (!bDeferPostAI)
	{
		PostAI();
	}
	Watchdog();
}

void FRiderSim::PostAI()
{
	FRiderState& S = State;
	{
		// 0x114730, 0x129628, 0x1372F8
		const SSXAnim::FRowMat M = SSXAnim::RootFrame(SSXAnim::FRiderAnimDriver::FrameInputs(S, Profile.SizeScale), S.PoseRoot.Z, false);
		S.RenderRow0 = FVector(M.Row(0));
		S.RenderRow1 = FVector(M.Row(1));
		S.RenderRow2 = FVector(M.Row(2));
		S.RenderRow3 = FVector(M.TransformPoint(FVector3f(S.PoseRoot)));
		S.bRenderRowsValid = true;
	}
	// 0x126748
	S.SphereFrameP = S.Position;
	S.SphereFrameQ = S.Orientation;
	S.bSphereFrameValid = true;
	if (RaceHooks)
	{
		RaceHooks->SphereSetRebuilt(*this, false);   // 0x236758, 0x14FE68
	}
}

FString FRiderSim::DumpState() const
{
	const FRiderState& S = State;
	FString Out = FString::Printf(TEXT("%s f%d motion %s (%.2f s) control %s (%.2f s) wipe-out %.2f s finish %d racing %d p (%.1f %.1f %.1f) v (%.1f %.1f %.1f) q (%.4f %.4f %.4f %.4f) n (%.3f %.3f %.3f) surface %d stuck_a %.3f stuck_b %.3f path %.1f reset_wait %.2f alpha %.2f fade %.2f auto_reset %d resets_in_a_row %d"),
		*DebugName, S.Frame, LexToString(S.Motion), S.MotionTime, LexToString(S.Control), S.ControlTime, WipeOutSeconds, S.FinishResult,
		S.bRacing ? 1 : 0, S.Position.X, S.Position.Y, S.Position.Z, S.Velocity.X, S.Velocity.Y, S.Velocity.Z,
		S.Orientation.X, S.Orientation.Y, S.Orientation.Z, S.Orientation.W, S.GroundNormal.X, S.GroundNormal.Y, S.GroundNormal.Z,
		S.SurfaceId, S.StuckAccumA, S.StuckAccumB, S.PathParam, S.ResetWaitTime, S.RenderAlpha, S.ResetFade, S.bAutoReset ? 1 : 0,
		ResetsWithoutRiding);
	if (const IRiderMotionState* H = MotionStates[int32(S.Motion)].Get())
	{
		const FString Extra = H->DebugString();
		if (!Extra.IsEmpty())
		{
			Out += TEXT(" | ") + Extra;
		}
	}
	return Out;
}

void FRiderSim::WatchdogResetWait(ERiderControl Old, ERiderControl New)
{
	if (Old == C::ResetWait)
	{
		WatchdogRespawnFrame = State.Frame;
	}
	if (New != C::ResetWait)
	{
		return;
	}
	ResetsWithoutRiding = (State.Frame - WatchdogRespawnFrame <= 30) ? ResetsWithoutRiding + 1 : 1;
	if (ResetsWithoutRiding >= WatchdogResetLoop && !bWatchdogResetLoop)
	{
		bWatchdogResetLoop = true;
		++WatchdogReports;
		UE_LOG(LogSSX, Warning, TEXT("[watchdog] reset loop: %d forced resets, each within 0.5 s of the respawn: %s"), ResetsWithoutRiding, *DumpState());
	}
}

void FRiderSim::Watchdog()
{
	const FRiderState& S = State;
	const bool bWipe = S.Motion == M::WipeOut || S.Motion == M::WipeOutRecover;
	WipeOutSeconds = bWipe ? WipeOutSeconds + Dt : 0.0;
	if (!bWipe) { bWatchdogWipeOut = false; }
	if (bWipe && WipeOutSeconds > WatchdogWipeOutLimit && !bWatchdogWipeOut)
	{
		bWatchdogWipeOut = true;
		++WatchdogReports;
		UE_LOG(LogSSX, Warning, TEXT("[watchdog] stuck wipe-out: %s"), *DumpState());
	}
	const bool bWait = S.Control == C::ResetWait;
	if (!bWait) { bWatchdogStatic = false; }
	if (bWait && S.ControlTime > WatchdogStaticLimit && !bWatchdogStatic)
	{
		bWatchdogStatic = true;
		++WatchdogReports;
		UE_LOG(LogSSX, Warning, TEXT("[watchdog] reset wait does not end: %s"), *DumpState());
	}
	const bool bLive = S.bRacing && S.FinishResult < 0 && !(S.Motion == M::Static && S.Control != C::ResetWait);
	if (!bLive || S.Motion != WatchdogMotion || S.Control != WatchdogControl || FVector::DistSquared(S.Position, WatchdogAnchor) > 1.0)
	{
		WatchdogAnchor = S.Position;
		WatchdogMotion = S.Motion;
		WatchdogControl = S.Control;
		WatchdogStillSeconds = 0.0;
		bWatchdogNoProgress = false;
	}
	else
	{
		WatchdogStillSeconds += Dt;
		if (WatchdogStillSeconds > WatchdogNoProgressLimit && !bWatchdogNoProgress)
		{
			bWatchdogNoProgress = true;
			++WatchdogReports;
			UE_LOG(LogSSX, Warning, TEXT("[watchdog] no progress for %.1f s: %s"), WatchdogStillSeconds, *DumpState());
		}
	}
	WatchdogInvisibleSeconds = S.bRacing && S.RenderAlpha <= 0.0 ? WatchdogInvisibleSeconds + Dt : 0.0;
	if (WatchdogInvisibleSeconds == 0.0) { bWatchdogInvisible = false; }
	if (WatchdogInvisibleSeconds > WatchdogNoProgressLimit && !bWatchdogInvisible)
	{
		bWatchdogInvisible = true;
		++WatchdogReports;
		UE_LOG(LogSSX, Warning, TEXT("[watchdog] render alpha 0 for %.1f s: %s"), WatchdogInvisibleSeconds, *DumpState());
	}
	const bool bFinite = !S.Position.ContainsNaN() && !S.Velocity.ContainsNaN() && !S.Orientation.ContainsNaN()
		&& FMath::IsFinite(S.Position.X) && FMath::IsFinite(S.Position.Y) && FMath::IsFinite(S.Position.Z);
	if (!bFinite && !bWatchdogNonFinite)
	{
		bWatchdogNonFinite = true;
		++WatchdogReports;
		UE_LOG(LogSSX, Error, TEXT("[watchdog] non-finite rider state: %s"), *DumpState());
	}
	if (bFinite) { bWatchdogNonFinite = false; }
}
