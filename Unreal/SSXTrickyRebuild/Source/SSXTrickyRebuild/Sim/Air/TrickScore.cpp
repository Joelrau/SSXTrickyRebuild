#include "Sim/Air/TrickScore.h"

#include "Sim/RiderState.h"
#include "Sim/Air/TrickName.h"
#include "Sim/EEFloat.h"

namespace
{
	/** frames x 0.016666668 (0x3C888889) in single precision: the seconds of the race_stats totals. */
	float TrickStatSeconds(int32 Frames) { return float(Frames) * 0.016666668f; }
}

namespace SSXAir
{
namespace
{
	constexpr int32 GrabNameTable[26] = { 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 23, 24, 22, 25, 26, 5 };
	constexpr int32 TweakNameTable[26] = { 27, 28, 29, 30, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 49, 50, 48, 51, 52, 31 };
	constexpr int32 FirstGrab = 0x278, FirstTweak = 0x292, FirstUber = 0x2AC, LastUber = 0x2DB;

	struct FRateGroup { double Rate; int32 Ids[16]; };
}

int32 Round180Signed(double Deg)
{
	// 0x157198
	const double Q = FMath::TruncToDouble(Deg / 180.0);
	const double Rem = Deg - 180.0 * Q;
	double R = Q;
	if (Rem > 90.0)
	{
		R += 1.0;
	}
	else if (Rem < -90.0)
	{
		R -= 1.0;
	}
	return int32(180.0 * R);
}

int32 Round180(double Deg)
{
	return FMath::Abs(Round180Signed(FMath::Abs(Deg)));
}

double TrickHoldRate(int32 AnimId)
{
	// 0x155EE8, 0x156050
	static const FRateGroup Groups[] = {
		{ 1.0, { 0x27E, 0x282, 0x283, 0x28C } },
		{ 1.25, { 0x27C, 0x28E } },
		{ 1.5, { 0x27F, 0x280, 0x285, 0x286, 0x288 } },
		{ 1.75, { 0x284, 0x290 } },
		{ 2.0, { 0x278, 0x279, 0x27A, 0x27D, 0x281, 0x287, 0x289, 0x28A, 0x28B, 0x28D, 0x28F, 0x298, 0x29C, 0x29D, 0x2A6 } },
		{ 2.5, { 0x27B, 0x291, 0x296, 0x2A8 } },
		{ 3.0, { 0x299, 0x29A, 0x29F, 0x2A0, 0x2A2 } },
		{ 3.5, { 0x29E, 0x2AA } },
		{ 4.0, { 0x292, 0x293, 0x294, 0x297, 0x29B, 0x2A1, 0x2A3, 0x2A4, 0x2A5, 0x2A7, 0x2A9 } },
		{ 5.0, { 0x295, 0x2AB } },
	};
	if (AnimId >= FirstUber && AnimId < 0x2D0) { return 18.0; }
	if (AnimId >= 0x2D0 && AnimId <= LastUber) { return 20.0; }
	for (const FRateGroup& G : Groups)
	{
		for (int32 Id : G.Ids)
		{
			if (Id == 0) { break; }
			if (Id == AnimId) { return G.Rate; }
		}
	}
	return 0.0;
}

int32 TrickNameIndex(int32 AnimId)
{
	if (AnimId >= FirstGrab && AnimId < FirstTweak) { return GrabNameTable[AnimId - FirstGrab]; }
	if (AnimId >= FirstTweak && AnimId < FirstUber) { return TweakNameTable[AnimId - FirstTweak]; }
	if (AnimId >= FirstUber && AnimId <= LastUber) { return 53 + (AnimId - FirstUber); }
	return 0;
}

bool IsUberAnim(int32 AnimId) { return AnimId >= FirstUber && AnimId <= LastUber; }

void FTrickScore::UpdateAngles(double SpinDegrees, double FlipDegrees)
{
	// 0x100908
	const int32 SpinI = int32(SpinDegrees);
	const int32 FlipI = int32(FlipDegrees);
	SpinDeg = SpinI;
	FlipDeg = FlipI;
	// 0x155E00
	int32 R = Round180(SpinI);
	if (R > BestSpin)
	{
		BestSpin = R;
		Value += TrickValueSpinStep;
	}
	// 0x155E68, 0x157198
	R = Round180(FlipI);
	if (R > BestFlip && R % 360 == 0)
	{
		BestFlip = R;
		Value += TrickValueFlipStep;
	}
	++AirFrames;
}

void FTrickScore::GrabReachesHold(int32 AnimId)
{
	Value += GrabRegisterValue * (FMath::Min(GrabNames.Num(), MaxGrabNames) + 1);
	HoldFrames = 0;
	const double R = TrickHoldRate(AnimId);
	if (R > 0.0)
	{
		HoldRate = R;
	}
}

void FTrickScore::TrickUpgraded(int32 AnimId)
{
	const double R = TrickHoldRate(AnimId);
	if (R > 0.0)
	{
		HoldRate = R;
	}
}

void FTrickScore::TickHold()
{
	if (HoldFrames >= 0)
	{
		++HoldFrames;
		Value += HoldRate * GrabHoldValue;
	}
}

void FTrickScore::FinishTrick(int32 AnimId, double FinishValue)
{
	if (HoldFrames < 0)
	{
		return;
	}
	const int32 Name = TrickNameIndex(AnimId);
	if (GrabNames.Num() == 0 || (GrabNames.Last() != Name && GrabNames.Num() < MaxGrabNames))
	{
		GrabNames.Add(Name);
	}
	if (IsUberAnim(AnimId))
	{
		++PendingUbers;
	}
	else
	{
		++GrabCount;
	}
	// 0x156148
	const float Held = TrickStatSeconds(HoldFrames);
	HoldSecondsTotal = SSXEE::FpuAdd(float(HoldSecondsTotal), Held);
	HoldSecondsBest = FMath::Max(HoldSecondsBest, double(Held));
	HoldFrames = -1;
	Value += FinishValue * TrickFinishValue * HoldRate;
}

void FTrickScore::RailSpin(int32 Degrees)
{
	// 0x155E00
	SpinDeg = Degrees;
	const int32 R = Round180(Degrees);
	if (R > BestSpin)
	{
		BestSpin = R;
		Value += TrickValueRailSpin;
	}
}

void FTrickScore::RailFrame(double Speed)
{
	if (RailFrames < 0)
	{
		return;   // 0x156E80
	}
	++RailFrames;
	// g = 1 at >= 800 cm/s, else s / (3200 - 3 s), 0 below 50 cm/s
	const double G = Speed >= 800.0 ? 1.0 : (Speed < 50.0 ? 0.0 : Speed / (3200.0 - 3.0 * Speed));
	Value += TrickValueRailK * G;
}

FTrickSummary FTrickScore::Recognise() const
{
	FTrickSummary S;
	S.Spins180 = BestSpin ? (SpinDeg < 0.0 ? -1 : 1) * (BestSpin / 180) : 0;
	S.Flips = BestFlip ? (FlipDeg < 0.0 ? -1 : 1) * (BestFlip / 360) : 0;
	S.Value = Value;
	return S;
}

void FTrickScore::BankSegment()
{
	TotalSpinDeg += FMath::Abs(SpinDeg);
	TotalFlipDeg += FMath::Abs(FlipDeg);
}

FTrickRecord FTrickScore::BuildRecord(bool bToRail, bool bLandSwitch, bool bSteep) const
{
	// 0x157490
	SSXTrick::FRecordInput In;
	In.SpinDeg = int32(SpinDeg);
	In.FlipDeg = int32(FlipDeg);
	In.bSwitch = bSegmentFakie;
	In.SegType = SegmentType;
	In.bLate = bSegmentLate;
	In.bAltFlag = bAltFlag;
	for (int32 I = 0; I < GrabNames.Num() && I < 3; ++I)
	{
		In.Grabs[I] = GrabNames[I];
	}
	In.bToRail = bToRail;
	In.bLandSwitch = bLandSwitch;
	In.bAltSelect = bSteep;   // 0x108378
	const SSXTrick::FRecord Rec = SSXTrick::BuildTrickRecord(In);
	FTrickRecord R;
	R.W0 = Rec.W0;
	R.W1 = Rec.W1;
	R.bValid = Rec.bValid;
	return R;
}

int32 FTrickScore::PointsFor(double V, double M)
{
	const double Raw = MeterScale * V * M;
	if (Raw <= 0.0)
	{
		return 0;
	}
	const int32 P = int32(Raw * PointsScale + 5.0);
	return P - P % 10;
}

void FTrickScore::Announce(const FTrickRecord& R, int32 N)
{
	// FUN_00157088, FUN_00157c50
	Announced = R;
	TrickText = SSXTrick::FTrickTables::Get().Compose(R.W0, R.W1);
	Messages.Push(N > 0 ? SSXMsg::TrickRepeat : SSXMsg::TrickNew, 0, SSXMsg::LifeTrickName());
}

void FTrickScore::SampleSpeed(float Speed)
{
	// 0x155DC8
	SpeedKmhSum = SSXEE::FpuAdd(SpeedKmhSum, Speed * 0.036000002f);
	SpeedFrames = SSXEE::FpuAdd(SpeedFrames, 1.0f);
}

void FTrickScore::ResetRaceStats()
{
	TFunction<bool(uint32, uint32)> Book = MoveTemp(BookCheck);
	TFunction<void(int32, int32)> Push = MoveTemp(Messages.OnPush);
	*this = FTrickScore();
	BookCheck = MoveTemp(Book);
	Messages.OnPush = MoveTemp(Push);
}

void FTrickScore::CheckBook(const FTrickRecord& R)
{
	// FUN_001579E0, 0x157330
	if (BookCheck && BookCheck(R.W0, R.W1))
	{
		Messages.Push(SSXMsg::Camera, 0, SSXMsg::LifeCamera);
	}
}

double FTrickScore::StartSegment(bool bSwitch, int32 Type, bool bLate, bool bAlt)
{
	// FUN_00156618
	++SegmentStarts;
	double Reward = 0.0;
	if (SegmentType != 0)
	{
		const double RailSec = FMath::Max(RailFrames, 0) / 60.0;
		// 0x156618
		const float RailStat = TrickStatSeconds(RailFrames);
		++RailsRace;
		RailSecondsTotal = SSXEE::FpuAdd(float(RailSecondsTotal), RailStat);
		RailSecondsBest = FMath::Max(RailSecondsBest, double(RailStat));
		if (RailSec > RailComboSeconds)
		{
			++Combo;
		}
		FTrickRecord R;
		const SSXTrick::FRecord Rail = SSXTrick::RailTrickRecord(Type != 0 ? Type : SegmentType, Round180Signed(SpinDeg));
		R.W0 = Rail.W0;
		R.W1 = Rail.W1;
		R.bValid = Rail.bValid;
		LastRecord = R;
		Announce(R, 0);
		CheckBook(R);   // FUN_001579E0
		Reward = FMath::Max(0.0, MeterScale * Value);
		const int32 P = PointsFor(Value, PointsMult);
		LastPoints = P;
		if (P > 0)
		{
			Messages.Push(SSXMsg::Points, P, SSXMsg::LifePoints, PointsFor(Value, 1.0));
			Score += P;
			if (P > BestTrickPoints)
			{
				BestTrickPoints = P;
				BestTrick = R;
			}
		}
		// 0x157008
		Value = 0.0;
		SpinDeg = FlipDeg = 0.0;
		BestSpin = BestFlip = 0;
		RailFrames = -1;
		HoldSecondsTotal = HoldSecondsBest = 0.0;
		GrabCount = 0;
		PendingUbers = 0;
		PointsMult = bSwitch ? RailStartPointsMult : 1.0;
		Messages.RemoveType(SSXMsg::Multiplier);   // 0x157308
		LastReward = Reward;
	}
	AirFrames = FMath::Max(AirFrames, 0);
	bSegmentFakie = bSwitch;
	bAltFlag = bAlt;
	SegmentType = Type;
	bSegmentLate = bLate;
	return Reward;
}

int32 FTrickScore::RepeatCount(const FTrickRecord& R)
{
	// 0x157268
	int32 N = 0;
	if (!R.IsUnrepeatable())
	{
		for (const FTrickRecord& H : History)
		{
			N += H == R ? 1 : 0;
		}
	}
	History.Add(R);
	if (History.Num() > RepeatHistory)
	{
		History.RemoveAt(0, History.Num() - RepeatHistory);
	}
	return N;
}

void FTrickScore::ClearSegment()
{
	SpinDeg = FlipDeg = 0.0;
	BestSpin = BestFlip = 0;
	SegmentType = 0;
	HoldFrames = -1;
	RailFrames = -1;
	GrabNames.Reset();
}

double FTrickScore::LateTrick(bool bGoofy)
{
	// 0x156568, 0x157490
	const FTrickRecord R = BuildRecord(false, bGoofy);
	if (R.bValid)
	{
		LastRepeatCount = RepeatCount(R);
		LastRecord = R;
		Announce(R, LastRepeatCount);
	}
	BankSegment();
	ClearSegment();
	StartSegment(bGoofy, 0, true);   // 0x156618
	return 0.0;
}

double FTrickScore::Land(bool bPrevWasRail, int32 ToRailKind, bool bLandSwitch, bool bFinished, bool bSteep)
{
	const bool bToRail = ToRailKind != 0;
	const int32 RailFramesBefore = RailFrames;
	// 0x1569B0
	const FTrickRecord Rec = BuildRecord(bToRail, bLandSwitch, bSteep);
	int32 N = 0;
	if (Rec.bValid)
	{
		++Combo;
		if (bPrevWasRail)
		{
			Value += TrickValuePrevRail;
		}
		if (bToRail)
		{
			Value += TrickValueFromRail;
		}
		if (!bSegmentLate)
		{
			N = RepeatCount(Rec);
		}
		Announce(Rec, N);
		CheckBook(Rec);   // FUN_001579E0
	}
	LastSummary = Recognise();
	LastRepeatCount = N;
	LastRecord = Rec;
	double Meter = 0.0;
	int32 Pts = 0;
	if (!bFinished)
	{
		Meter = FMath::Max(0.0, MeterScale * Value) / (N + 1);
		SpinDegRace += FMath::Abs(int32(SpinDeg));   // 0x157008
		FlipDegRace += FMath::Abs(int32(FlipDeg));
		GrabsRace += GrabCount;
		UberCount += PendingUbers;
		HoldSecondsRace = SSXEE::FpuAdd(float(HoldSecondsRace), float(HoldSecondsTotal));
		GrabSecondsBest = FMath::Max(GrabSecondsBest, HoldSecondsBest);
		Pts = PointsFor(Value, PointsMult) / (N + 1);
		const int32 Unmultiplied = PointsFor(Value, 1.0);
		if (!bToRail)
		{
			if (Combo >= 2)
			{
				const int32 Bonus = ComboBonusStep * FMath::Min(Combo - 1, ComboBonusMaxSteps);
				Pts += Bonus;
				Messages.Push(SSXMsg::ComboBonus, Bonus, SSXMsg::LifeBonus);
			}
			Combo = 0;
		}
		const double AirSec = AirFrames / 60.0;
		const float AirStat = TrickStatSeconds(AirFrames);
		if (0.0f < AirStat)
		{
			AirSecondsTotal = SSXEE::FpuAdd(float(AirSecondsTotal), AirStat);
			AirSecondsBest = FMath::Max(AirSecondsBest, double(AirStat));
		}
		const int32 BigAir = AirSec < BigAirMinSeconds ? 0 : int32(AirSec - BigAirMinSeconds + 1.0) * BigAirBonusStep;
		if (BigAir > 0)
		{
			Pts += BigAir;
			Messages.Push(SSXMsg::BigAir, BigAir, SSXMsg::LifeBonus);
		}
		if (Pts > 0)
		{
			Messages.Push(N > 0 ? SSXMsg::PointsRepeat : SSXMsg::Points, Pts, SSXMsg::LifePoints, Unmultiplied);
			Score += Pts;
			++TierCounts[SSXTrick::FTrickTables::Get().PointsTier(float(Pts))];
			++ScoredTricks;
		}
		if (Rec.bValid && Pts > BestTrickPoints)
		{
			BestTrickPoints = Pts;
			BestTrick = Rec;
		}
	}
	LastPoints = Pts;
	LastReward = Meter;
	BankSegment();
	PendingUbers = 0;
	GrabCount = 0;
	HoldSecondsTotal = HoldSecondsBest = 0.0;
	Value = 0.0;
	AirFrames = 0;
	ClearSegment();
	bSegmentLate = false;
	SegmentType = ToRailKind;
	RailFrames = bToRail ? RailFramesBefore + 1 : -1;
	PointsMult = 1.0;
	Messages.RemoveType(SSXMsg::Multiplier);   // 0x157308
	return Meter;
}

double FTrickScore::Bail(bool bCrash)
{
	// FUN_00156de0
	const int32 Lost = PointsFor(Value, PointsMult);
	if (Lost > 0)
	{
		Messages.Push(SSXMsg::PointsLost, Lost, SSXMsg::LifePointsLost);
	}
	if (bCrash)
	{
		++CrashCount;
		Messages.Push(SSXMsg::Crash, 0, SSXMsg::LifeCrash);
	}
	else
	{
		++WipeOutCount;
	}
	Value = 0.0;
	ClearSegment();
	bSegmentLate = false;
	PendingUbers = 0;
	GrabCount = 0;   // 0x157008
	HoldSecondsTotal = HoldSecondsBest = 0.0;
	PointsMult = 1.0;
	Messages.RemoveType(SSXMsg::Multiplier);   // 0x157308
	Combo = 0;
	LastReward = BailMeter;
	LastPoints = 0;
	return BailMeter;
}

double FTrickScore::Respawn()
{
	// FUN_00155aa8
	const int32 Lost = PointsFor(Value, PointsMult);
	if (Lost > 0)
	{
		Messages.Push(SSXMsg::PointsLost, Lost, SSXMsg::LifePointsLost);
	}
	Value = 0.0;
	ClearSegment();
	bSegmentLate = false;
	PendingUbers = 0;
	GrabCount = 0;   // 0x157008
	HoldSecondsTotal = HoldSecondsBest = 0.0;
	PointsMult = 1.0;
	Messages.RemoveType(SSXMsg::Multiplier);   // 0x157308
	Combo = 0;
	++RespawnCount;
	return RespawnMeter;
}

double BoostAdd(FRiderState& State, double Reward, double Gain)
{
	const double Before = State.BoostMeter;
	const double Delta = Reward * Gain;
	State.Highlight(16, float(Delta));   // 0x11B018
	if (Before < MeterNearFullFrom && Before + Delta >= 1.0)
	{
		++State.MeterNearFullCount;   // 0x2191C0
	}
	State.BoostMeter = FMath::Clamp(Before + Delta, 0.0, 1.0);
	if (State.BoostMeter == 1.0 && Reward > 0.0)
	{
		// FUN_0021DCE0, FUN_0021C610
		State.TrickyTimerBeforeFull = State.TrickyTimer;
		if (State.TrickyTimer <= 0.0)
		{
			State.UberHintIndex = (State.UberHintIndex + 1) % 4;
			++State.TrickyFreshCount;
		}
		State.TrickyTimer = TrickyWindowSeconds;
		++State.TrickyFullCount;
	}
	return State.BoostMeter - Before;
}

bool FTrickScore::ScriptMultiplier(float M)
{
	const int32 N = int32(M);
	const int32 K = N == 2 ? 0 : (N == 3 ? 1 : (N == 5 ? 2 : -1));
	if (K >= 0)
	{
		++MultiplierCounts[K];   // 0x155CE8, 0x155D44
	}
	if (!(float(PointsMult) < M))
	{
		return false;
	}
	Messages.Push(SSXMsg::Multiplier, N, SSXMsg::LifeNever);
	PointsMult = double(M);
	return true;
}
}
