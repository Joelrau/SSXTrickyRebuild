// 0x127298, 0x126FC0, 0x102800, 0x100908
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Input/SSXInputSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TrickAnimFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FAnimFrame
	{
		int32 F = 0;
		bool bAir = false, bSpin = false;
		int32 Ch0 = -1, Mode = -1, SimSpinAnim = -1, TrickId = 26;
		double Frame = 0.0, Speed = 1.0, Hold = -1.0, SimSpinFrame = 0.0, SimSpinSpeed = 1.0;
		int32 SimSpinN = 0;
		double SpinDeg = 0.0, Offset = 0.0;
		double RootErr = 0.0;
	};

	struct FAnimRun
	{
		TArray<FAnimFrame> Frames;
		int32 TakeOff = -1, Land = -1;
		double SpinGain = 1.0;
		bool bGoofy = false;
		FString Log;
	};

	FAnimRun Ride(const SSXAnim::FAflLibrary* Lib, FPatchWorld& World, int32 BoardClass, bool bGoofy, double Boost,
		TFunctionRef<uint32(int32 F, int32 TakeOff)> Script, int32 NumFrames)
	{
		FAnimRun R;
		FRiderSim Sim;
		FRiderInputDriver Input;
		SSXRiderSetup::InstallStates(Sim, Input, false);
		Sim.SetWorld(&World);
		Sim.Profile.Character = 10;
		Sim.Profile.TrickCharacter = 10;
		Sim.Profile.BoardClass = BoardClass;
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, 10, BoardClass);
		Sim.Reset();
		Sim.Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = 1500.0 * Sim.State.ContactForward;
		Sim.State.bStanceGoofy = bGoofy;
		R.bGoofy = bGoofy;
		R.SpinGain = 0.5449139 + 0.6741781 * Sim.Profile.Stat(0x1A);
		SSXAnim::FRiderAnimDriver Anim;
		Anim.Init(Lib, TEXT("brodi"), BoardClass, 10);
		FPadMapper Mapper(10, BoardClass);
		Mapper.SetLayout(0);
		FSSXPadDevice Pad;
		for (int32 F = 0; F < NumFrames; ++F)
		{
			const uint32 Buttons = Script(F, R.TakeOff);
			const FPadActions A = Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(Buttons, FVector2D::ZeroVector, FVector2D::ZeroVector)));
			const bool bWasAir = Sim.State.Motion == ERiderMotion::Air;
			Input.Step(Sim, A);
			FRiderState& S = Sim.State;
			const bool bAir = S.Motion == ERiderMotion::Air;
			if (bAir && !bWasAir && R.TakeOff < 0)
			{
				R.TakeOff = F;
				S.Velocity.Z += Boost;   // longer flight for the trick (the rules under test do not depend on the height)
			}
			if (!bAir && bWasAir && R.TakeOff >= 0 && R.Land < 0)
			{
				R.Land = F;
			}
			Anim.Step(Sim);
			FAnimFrame Fr;
			Fr.F = F;
			Fr.bAir = bAir;
			Fr.bSpin = S.Control == ERiderControl::Spin;
			Fr.TrickId = S.TrickId;
			Fr.SpinDeg = FMath::RadiansToDegrees(S.SpinAngle);
			const SSXAnim::FAnimController& C = Anim.GetController();
			Fr.Ch0 = C.CurrentId(0);
			if (const SSXAnim::FPlayInstance* I = C.Newest(0))
			{
				Fr.Mode = I->Props.LoopMode;
				Fr.Frame = I->Subs[0].Frame;
				Fr.Speed = I->Speed;
				Fr.Hold = I->Hold;
			}
			if (const SSXAir::FAirContext* X = SSXAir::GetAirContext(Sim))
			{
				Fr.SimSpinAnim = X->SpinAnim.Id;
				Fr.SimSpinFrame = X->SpinAnim.Frame;
				Fr.SimSpinSpeed = X->SpinAnim.Speed;
				Fr.SimSpinN = X->SpinAnim.NFrames;
				Fr.Offset = X->Spin.SettleOffset;
			}
			if (bAir)
			{
				SSXAnim::FRiderFrameInput In = SSXAnim::FRiderAnimDriver::FrameInputs(S, 1.0);
				In.Steer = 0.0;
				const SSXAnim::FRowMat M = SSXAnim::RootFrame(In, 0.0, false);
				const FVector Y = SSXAir::GetRenderedOrientation(S).RotateVector(FVector(0, 1, 0));
				Fr.RootErr = (FVector(M.M[1][0], M.M[1][1], M.M[1][2]) - Y).Size();
			}
			if (bAir || (R.TakeOff >= 0 && R.Land < 0))
			{
				R.Log += FString::Printf(TEXT("  f%3d %s ch0 %#x m%d fr %5.1f sp %.3f | sim %#x %5.1f | spin %+7.1f off %+5.2f trick %d\n"),
					F, LexToString(S.Control), Fr.Ch0, Fr.Mode, Fr.Frame, Fr.Speed, Fr.SimSpinAnim, Fr.SimSpinFrame, Fr.SpinDeg, Fr.Offset, Fr.TrickId);
			}
			R.Frames.Add(Fr);
		}
		return R;
	}

	bool ModelMatches(const FAnimFrame& Fr)
	{
		const int32 Last = Fr.SimSpinN - 1;
		const double Next = Fr.SimSpinFrame + SSXAnim::AdvancePerFrame * Fr.SimSpinSpeed;
		if (SSXAnim::IsSpinCycle(Fr.SimSpinAnim))
		{
			return Fr.Ch0 == Fr.SimSpinAnim;
		}
		if (Next <= Last || Fr.SimSpinFrame < Last)
		{
			// 0x15A030
			return Fr.Ch0 == Fr.SimSpinAnim && FMath::Abs(Fr.Frame - FMath::Min(Next, double(Last))) < 1e-6;
		}
		const int32 Chain = SSXAnim::IsSpinInto(Fr.SimSpinAnim) ? SSXAnim::TrickPhaseChain(Fr.SimSpinAnim) : SSXAnim::AirIdle;
		return Fr.Ch0 == Chain;
	}

	bool IsTakeoffClip(int32 Id) { return (Id >= 0x218 && Id <= 0x220) || (Id >= 0x260 && Id <= 0x26D); }

	/** Invariants every run must hold; returns the ordered list of distinct channel-0 ids while airborne. */
	TArray<int32> CheckRun(FAutomationTestBase& T, const FString& Name, const FAnimRun& R)
	{
		TArray<int32> Seq;
		int32 Still = 0, TakeoffFrames = 0, ModelMismatch = 0, RootBad = 0;
		double PrevFrame = -1.0;
		int32 PrevId = -2;
		for (const FAnimFrame& Fr : R.Frames)
		{
			if (!Fr.bAir || Fr.F <= R.TakeOff)
			{
				PrevId = -2;
				continue;
			}
			if (Seq.Num() == 0 || Seq.Last() != Fr.Ch0) { Seq.Add(Fr.Ch0); }
			TakeoffFrames += IsTakeoffClip(Fr.Ch0) ? 1 : 0;
			if (Fr.bSpin && Fr.SimSpinAnim >= 0 && !ModelMatches(Fr))
			{
				++ModelMismatch;
			}
			const bool bClocked = Fr.Mode >= 0 && Fr.Mode <= 2;
			const bool bHeld = Fr.Hold >= 0.0 && Fr.TrickId != 26;
			Still = (bClocked && !bHeld && Fr.Ch0 == PrevId && Fr.Frame == PrevFrame) ? Still + 1 : 0;
			T.TestTrue(Name + FString::Printf(TEXT(": channel 0 not stalled (f%d %#x frame %.1f)"), Fr.F, Fr.Ch0, Fr.Frame), Still < 3);
			if (Still >= 3) { break; }
			RootBad += Fr.RootErr > 1e-3 ? 1 : 0;
			PrevId = Fr.Ch0;
			PrevFrame = Fr.Frame;
		}
		T.TestTrue(Name + TEXT(": took off"), R.TakeOff >= 0);
		T.TestEqual(Name + TEXT(": no take-off clip left on channel 0 in the air"), TakeoffFrames, 0);
		T.TestEqual(Name + TEXT(": the Spin control's channel-0 model equals the controller"), ModelMismatch, 0);
		T.TestEqual(Name + TEXT(": rendered frame = base x trick angles, folded once"), RootBad, 0);
		return Seq;
	}

	FString SeqText(const TArray<int32>& Seq)
	{
		FString S;
		for (const int32 Id : Seq) { S += FString::Printf(TEXT("%s%#x"), S.IsEmpty() ? TEXT("") : TEXT(" > "), Id); }
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimTrickPipelineTest, "SSX.Anim.TrickPipeline", TrickAnimFlags)

bool FSSXAnimTrickPipelineTest::RunTest(const FString& Parameters)
{
	using namespace SSXPadButton;
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	if (!Lib)
	{
		SSXTest::Skip(*this, TEXT("SimData/anim_clips.bin missing (tools/export_anim_clips.py); skipped"));
		return true;
	}
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	if (!World.GetSurfaces().IsLoaded())
	{
		AddError(TEXT("SimData/surfaces.bin not available"));
		return false;
	}
	constexpr int32 HoldFrom = 30, ReleaseAt = 90;

	// 0x126E30, 0x127298
	for (const int32 Bc : { 0, 2 })
	{
		const FAnimRun R = Ride(Lib, World, Bc, false, 600.0, [](int32 F, int32) -> uint32 { return (F >= HoldFrom && F < ReleaseAt) ? Cross : 0u; }, 220);
		const FString N = FString::Printf(TEXT("ollie class %d"), Bc);
		const TArray<int32> Seq = CheckRun(*this, N, R);
		AddInfo(N + TEXT(": ") + SeqText(Seq));
		TestTrue(N + TEXT(": the air idle 0x223 plays from the first air frame"), Seq.Num() > 0 && Seq[0] == SSXAnim::AirIdle);
	}

	int32 HalfSpinRuns = 0;
	for (const int32 Bc : { 0, 2 })
	{
		for (const bool bGoofy : { false, true })
		{
			for (const uint32 Dir : { Right, Left })
			{
				for (const int32 Keep : { 0, 12, 30, 50, 70 })
				{
					auto Script = [Dir, Keep](int32 F, int32 TakeOff) -> uint32
					{
						if (F >= HoldFrom && F < ReleaseAt) { return Cross | Dir; }
						if (F >= ReleaseAt && (TakeOff < 0 || F < TakeOff + Keep)) { return Keep > 0 ? Dir : 0u; }
						return 0u;
					};
					const FAnimRun R = Ride(Lib, World, Bc, bGoofy, 900.0, Script, 330);
					const FString N = FString::Printf(TEXT("class %d %s %s keep %d"), Bc, bGoofy ? TEXT("goofy") : TEXT("regular"),
						Dir == Right ? TEXT("right") : TEXT("left"), Keep);
					const TArray<int32> Seq = CheckRun(*this, N, R);
					const bool bBs = (Dir == Right) != bGoofy;
					const int32 Into = bBs ? 0x22C : 0x22D, Cycle = Into + 8, Out = Into + 0x10;
					const int32 IInto = Seq.Find(Into), IOut = Seq.Find(Out), IIdle = Seq.Find(SSXAnim::AirIdle);
					AddInfo(N + FString::Printf(TEXT(": %s (take-off f%d, landing f%d)"), *SeqText(Seq), R.TakeOff, R.Land));
					if (Keep == 0)
					{
						TestEqual(N + TEXT(": the play-out replaces the into clip at the take-off"), IOut, 0);
					}
					else
					{
						TestEqual(N + TEXT(": the into clip by the take-off clip (0x126FC0) starts the flight"), IInto, 0);
						TestTrue(N + TEXT(": the play-out follows (0x102800)"), IOut > IInto);
					}
					TestTrue(N + TEXT(": the air idle follows the play-out (chain 2)"), IIdle > IOut);
					if (Keep >= 50)
					{
						TestTrue(N + TEXT(": the cycle runs between into and play-out (chain 4)"), Seq.Find(Cycle) == 1);
					}
					double OffStart = 0.0, OffEnd = 0.0, SpinAtStart = 0.0, SpinAtEnd = 0.0;
					bool bInOut = false, bSawOut = false, bSpeedOk = true;
					for (const FAnimFrame& Fr : R.Frames)
					{
						if (!Fr.bAir || Fr.F <= R.TakeOff) { continue; }
						if (Fr.Ch0 == Out)
						{
							bSpeedOk &= FMath::Abs(Fr.Speed - R.SpinGain) < 1e-6;
							if (!bInOut) { OffStart = Fr.Offset; SpinAtStart = Fr.SpinDeg; bSawOut = true; }
							bInOut = true;
							OffEnd = Fr.Offset;
							SpinAtEnd = Fr.SpinDeg;
						}
						else if (bInOut)
						{
							break;
						}
					}
					TestTrue(N + TEXT(": play-out speed = spin gain"), bSawOut && bSpeedOk);
					const double Want = PI * (bGoofy ? -1.0 : 1.0) * (Out == 0x23D ? -1.0 : 1.0);
					TestTrue(N + FString::Printf(TEXT(": half-spin offset starts at %+.2f (got %+.2f) and is taken back (end %+.2f)"), Want, OffStart, OffEnd),
						FMath::Abs(FMath::Abs(OffStart) - PI) < 0.2 && FMath::Sign(OffStart) == FMath::Sign(Want) && FMath::Abs(OffEnd) < 0.1);
					++HalfSpinRuns;
					if (Bc == 0 && !bGoofy && Dir == Right && Keep == 30)
					{
						UE_LOG(LogTemp, Display, TEXT("[trickanim] %s\n%s"), *N, *R.Log);
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d prewind spin runs"), HalfSpinRuns));

	for (const uint32 Dir : { Up, Down })
	{
		auto Script = [Dir](int32 F, int32 TakeOff) -> uint32
		{
			if (F >= HoldFrom && F < ReleaseAt) { return Cross | Dir; }
			if (F >= ReleaseAt && (TakeOff < 0 || F < TakeOff + 40)) { return Dir; }
			return 0u;
		};
		const FAnimRun R = Ride(Lib, World, 0, false, 900.0, Script, 330);
		const FString N = FString::Printf(TEXT("flip %s"), Dir == Up ? TEXT("up") : TEXT("down"));
		const TArray<int32> Seq = CheckRun(*this, N, R);
		AddInfo(N + TEXT(": ") + SeqText(Seq));
		const int32 Into = Seq.Num() ? Seq[0] : -1;
		TestTrue(N + TEXT(": a flip into clip"), Into == 0x226 || Into == 0x227);
		TestTrue(N + TEXT(": its play-out"), Into >= 0 && Seq.Contains(Into + 0x10));
	}

	{
		auto Script = [](int32 F, int32 TakeOff) -> uint32
		{
			if (F >= HoldFrom && F < ReleaseAt) { return Cross; }
			if (TakeOff >= 0 && ((F >= TakeOff + 6 && F < TakeOff + 20) || (F >= TakeOff + 100 && F < TakeOff + 150))) { return Right; }
			return 0u;
		};
		const FAnimRun R = Ride(Lib, World, 0, false, 1800.0, Script, 460);
		const TArray<int32> Seq = CheckRun(*this, TEXT("late spins"), R);
		AddInfo(TEXT("late spins: ") + SeqText(Seq));
		int32 Intos = 0;
		for (const int32 Id : Seq) { Intos += SSXAnim::IsSpinInto(Id) ? 1 : 0; }
		if (!TestEqual(TEXT("late spins: two into clips"), Intos, 2))
		{
			UE_LOG(LogTemp, Display, TEXT("[trickanim] late spins\n%s"), *R.Log);
		}
	}

	{
		auto Script = [](int32 F, int32 TakeOff) -> uint32
		{
			if (F >= HoldFrom && F < ReleaseAt) { return Cross; }
			if (TakeOff < 0) { return 0u; }
			const int32 T = F - TakeOff;
			if (T >= 4 && T < 7) { return R1; }
			if (T >= 30 && T < 60) { return R1; }
			if (T >= 76 && T < 100) { return R1; }
			return 0u;
		};
		const FAnimRun R = Ride(Lib, World, 0, false, 1200.0, Script, 360);
		const TArray<int32> Seq = CheckRun(*this, TEXT("grabs"), R);
		AddInfo(TEXT("grabs: ") + SeqText(Seq));
		bool bTapToIdle = false, bRestart = false;
		for (int32 K = 1; K < R.Frames.Num(); ++K)
		{
			const FAnimFrame& A = R.Frames[K - 1];
			const FAnimFrame& B = R.Frames[K];
			if (R.TakeOff >= 0 && B.F == R.TakeOff + 10)
			{
				bTapToIdle = B.Ch0 == SSXAnim::AirIdle;
			}
			if (R.TakeOff >= 0 && B.F > R.TakeOff + 70 && A.Ch0 == B.Ch0 && A.Mode == 1 && B.Frame < A.Frame - 1.0)
			{
				bRestart = true;
			}
		}
		TestTrue(TEXT("grabs: the tapped grab gives channel 0 back to the air idle (0x1272E8 plays 0x223)"), bTapToIdle);
		TestTrue(TEXT("grabs: the same grab pressed again restarts its clip"), bRestart);
	}
	return !HasAnyErrors();
}

#endif
