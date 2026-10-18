// 0x100908, 0x155E00, 0x155E68, 0x157198, 0x157268
#include "Misc/AutomationTest.h"
#include "Sim/Air/TrickName.h"
#include "Sim/Air/TrickScore.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags FlipTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Ramp(SSXAir::FTrickScore& S, double SpinTo, double FlipTo)
	{
		const int32 Steps = FMath::Max(1, int32(FMath::Max(FMath::Abs(SpinTo), FMath::Abs(FlipTo)) / 7.0) + 1);
		for (int32 I = 1; I <= Steps; ++I)
		{
			const double A = double(I) / Steps;
			S.UpdateAngles(SpinTo * A, FlipTo * A);
		}
	}

	uint32 FlipName(const SSXAir::FTrickRecord& R) { return (R.W1 >> 8) & 15; }
	uint32 FlipCount(const SSXAir::FTrickRecord& R) { return (R.W1 >> 24) & 7; }
	uint32 Prefix(const SSXAir::FTrickRecord& R) { return (R.W1 >> 16) & 15; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrickFlipScoreTest, "SSX.Trick.FlipScore", FlipTestFlags)

bool FSSXTrickFlipScoreTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	TestEqual(TEXT("V per flip = the 0x155E68 literal"), float(TrickValueFlipStep), 0.2499898f);
	TestEqual(TEXT("V per 180 = the 0x155E00 literal"), float(TrickValueSpinStep), 0.06995432f);

	struct FCase { double Flip; int32 Flips; const TCHAR* What; };
	const FCase Cases[] = {
		{ 269.9, 0, TEXT("under 270") }, { 270.0, 0, TEXT("270 rounds down (remainder 90 toward zero)") },
		{ 270.9, 0, TEXT("270.9 is (int) 270") }, { 271.0, 1, TEXT("271 rounds to 360") }, { 300.0, 1, TEXT("300") },
		{ 359.0, 1, TEXT("359") }, { 360.0, 1, TEXT("360") }, { 450.0, 1, TEXT("450 (round180 360)") },
		{ 540.0, 1, TEXT("540 is not a full turn") }, { 630.0, 1, TEXT("630 rounds to 540") },
		{ 631.0, 2, TEXT("631 rounds to 720") }, { 720.0, 2, TEXT("720") }, { 991.0, 3, TEXT("991 rounds to 1080") },
	};
	for (const bool bSwitch : { false, true })
	{
		for (const double Dir : { -1.0, 1.0 })   // back (negative flip angle: stick Down) / front
		{
			for (const FCase& C : Cases)
			{
				const FString L = FString::Printf(TEXT("%s %s %.1f deg (%s): "), bSwitch ? TEXT("switch") : TEXT("regular"),
					Dir < 0 ? TEXT("back") : TEXT("front"), C.Flip, C.What);
				FTrickScore S;
				S.StartSegment(bSwitch, 0, false);
				Ramp(S, 0.0, Dir * C.Flip);
				const double V = S.Value;
				TestTrue(L + TEXT("V = flips x 0.2499898"), FMath::IsNearlyEqual(V, C.Flips * TrickValueFlipStep, 1e-9));
				S.Land(false, 0);
				TestEqual(L + TEXT("summary flips (signed)"), S.LastSummary.Flips, C.Flips * int32(Dir));
				TestEqual(L + TEXT("points"), S.LastPoints, FTrickScore::PointsFor(C.Flips * TrickValueFlipStep, 1.0));
				const int32 NamedFlips = FMath::Abs(SSXTrick::Round180(int32(Dir * C.Flip))) / 360;
				TestEqual(L + TEXT("record flip count (w1 24..26)"), int32(FlipCount(S.LastRecord)), NamedFlips);
				TestEqual(L + TEXT("record flip name"), int32(FlipName(S.LastRecord)), NamedFlips == 0 ? 0 : (Dir < 0 ? 2 : 1));
				if (NamedFlips > 0)
				{
					TestEqual(L + TEXT("record prefix"), int32(Prefix(S.LastRecord)), bSwitch ? 1 : 0);
				}
			}
		}
	}

	{
		FTrickScore S;
		S.StartSegment(false, 0, false);
		Ramp(S, 360.0, -360.0);
		TestTrue(TEXT("360 + back flip: V = 2 x 0.06995432 + 0.2499898"), FMath::IsNearlyEqual(S.Value, 2 * TrickValueSpinStep + TrickValueFlipStep, 1e-9));
		S.Land(false, 0);
		TestEqual(TEXT("360 + back flip: Back Flip"), int32(FlipName(S.LastRecord)), 2);
		TestEqual(TEXT("360 + back flip: points"), S.LastPoints, FTrickScore::PointsFor(2 * TrickValueSpinStep + TrickValueFlipStep, 1.0));
	}
	{
		FTrickScore S;
		S.StartSegment(false, 0, false);
		Ramp(S, 540.0, -360.0);
		S.Land(false, 0);
		TestEqual(TEXT("540 + back flip: Rodeo"), int32(FlipName(S.LastRecord)), 3);
		TestEqual(TEXT("540 + back flip: points"), S.LastPoints, FTrickScore::PointsFor(3 * TrickValueSpinStep + TrickValueFlipStep, 1.0));
	}
	{
		FTrickScore S;
		S.StartSegment(false, 0, false);
		Ramp(S, 0.0, -400.0);
		S.UpdateAngles(0.0, -350.0);
		TestTrue(TEXT("overshoot then settle back: one flip"), FMath::IsNearlyEqual(S.Value, TrickValueFlipStep, 1e-9));
	}
	{
		FTrickScore S;
		int32 P[3];
		for (int32 I = 0; I < 3; ++I)
		{
			S.StartSegment(false, 0, false);
			Ramp(S, 0.0, -360.0);
			S.Land(false, 0);
			P[I] = S.LastPoints;
		}
		const int32 Base = FTrickScore::PointsFor(TrickValueFlipStep, 1.0);
		TestEqual(TEXT("first back flip"), P[0], Base);
		TestEqual(TEXT("second back flip: P / 2"), P[1], Base / 2);
		TestEqual(TEXT("third back flip: P / 3"), P[2], Base / 3);
		S.StartSegment(false, 0, false);
		Ramp(S, 0.0, -720.0);
		S.Land(false, 0);
		TestEqual(TEXT("a double back flip is a different record: full points"), S.LastPoints, FTrickScore::PointsFor(2 * TrickValueFlipStep, 1.0));
	}
	return !HasAnyErrors();
}

#endif
