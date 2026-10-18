// 0x125CB8, 0x108378, 0x15EEC0, 0x15FFB8
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
	constexpr EAutomationTestFlags RailTrickFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FRailTrickRun
	{
		int32 TakeOff = -1, Catch = -1, Land = -1, Wipe = -1;
		int32 TrickAtCatch = -1;
		double SpinAtCatch = 0.0, FlipAtCatch = 0.0, BoxAtCatch = 0.0, VzAtCatch = 0.0;
		ERiderControl ControlAtCatch = ERiderControl::None;
		FString Log;
		FString RailLine;   // ssx.Rail.Log: the catch line of the catch frame
	};

	FRailTrickRun RideOntoRail(const SSXAnim::FAflLibrary* Lib, double SlopeDeg, double RailH, double Boost, bool bFromRail,
		TFunctionRef<uint32(int32 F, int32 Rail1, int32 TakeOff)> Script, int32 NumFrames)
	{
		using namespace SSXPadButton;
		FRailTrickRun R;
		FPatchWorld World;
		SSXRiderSetup::BuildSlopeWorld(World, SlopeDeg, 1);
		const double Ta = FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
		const FVector N(0.0, FMath::Sin(FMath::DegreesToRadians(SlopeDeg)), FMath::Cos(FMath::DegreesToRadians(SlopeDeg)));
		FRiderSim Sim;
		FRiderInputDriver Input;
		SSXRiderSetup::InstallStates(Sim, Input, false);
		Sim.SetWorld(&World);
		Sim.Profile.Character = 10;
		Sim.Profile.TrickCharacter = 10;
		Sim.Profile.BoardClass = 0;
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, 10, 0);
		Sim.Reset();
		Sim.Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = 1500.0 * Sim.State.ContactForward;
		SSXAnim::FRiderAnimDriver Anim;
		Anim.Init(Lib, TEXT("brodi"), 0, 10);
		FPadMapper Mapper(10, 0);
		Mapper.SetLayout(0);
		FSSXPadDevice Pad;
		auto LayRail = [&World, Ta, N, RailH](double X, double Y0, double Y1)
		{
			FSimRailSegment Seg;
			const FVector P0 = FVector(X, Y0, -Y0 * Ta) + RailH * N, P1 = FVector(X, Y1, -Y1 * Ta) + RailH * N;
			Seg.Coef[0] = Seg.Coef[1] = FVector::ZeroVector;
			Seg.Coef[2] = P1 - P0;
			Seg.Coef[3] = P0;
			Seg.Bounds = FBox(P0.ComponentMin(P1), P0.ComponentMax(P1));
			Seg.Spline = 0;
			Seg.Length = (P1 - P0).Size();
			TArray<FSimRailSegment> Segs = { Seg };
			TArray<FSimRailSpline> Spl;
			Spl.AddDefaulted();
			Spl[0].Surface = 13;
			Spl[0].SegmentCount = 1;
			World.SetRails(MoveTemp(Segs), MoveTemp(Spl));
		};
		if (bFromRail)
		{
			LayRail(0.0, 600.0, 20000.0);
		}
		int32 Rail1 = -1;
		for (int32 F = 0; F < NumFrames && R.Land < 0; ++F)
		{
			const FPadActions A = Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(Script(F, Rail1, R.TakeOff), FVector2D::ZeroVector, FVector2D::ZeroVector)));
			const bool bWasAir = Sim.State.Motion == ERiderMotion::Air;
			const int32 TrickBefore = Sim.State.TrickId;
			const double SpinBefore = Sim.State.SpinAngle, FlipBefore = Sim.State.FlipAngle, VzBefore = Sim.State.Velocity.Z;
			const ERiderControl ControlBefore = Sim.State.Control;
			Input.Step(Sim, A);
			FRiderState& S = Sim.State;
			if (bFromRail && Rail1 < 0 && S.Motion == ERiderMotion::RailSlide)
			{
				Rail1 = F;
			}
			if (S.Motion == ERiderMotion::Air && !bWasAir && R.TakeOff < 0 && (!bFromRail || Rail1 >= 0))
			{
				R.TakeOff = F;
				S.Velocity.Z += Boost;
				if (!bFromRail)
				{
					LayRail(S.Position.X, S.Position.Y + 800.0, S.Position.Y + 8800.0);
				}
			}
			if (bFromRail && Rail1 < 0 && F > 200)
			{
				break;   // never got on the first rail
			}
			if (R.TakeOff >= 0 && R.Catch < 0 && S.Motion == ERiderMotion::RailSlide)
			{
				R.Catch = F;
				R.TrickAtCatch = TrickBefore;
				R.SpinAtCatch = SpinBefore;
				R.FlipAtCatch = FlipBefore;
				R.VzAtCatch = VzBefore;
				R.ControlAtCatch = ControlBefore;
				R.BoxAtCatch = S.RailBox.Cur;
				break;
			}
			if (R.TakeOff >= 0 && bWasAir && S.Motion == ERiderMotion::Ground)
			{
				R.Land = F;
			}
			if (R.TakeOff >= 0 && S.Motion == ERiderMotion::WipeOut)
			{
				R.Wipe = F;
				break;
			}
			if (R.TakeOff >= 0 && R.Catch < 0)
			{
				FRiderRailQuery Q;
				Q.Origin = S.BoardOrigin;
				FRiderRailHit H;
				FString C = FString::Printf(TEXT("box %+.3f"), S.RailBox.Cur);
				if (World.NumRailSplines() > 0 && World.QueryRail(Q, H))
				{
					const FVector Cv = H.Point - S.BoardOrigin - 30.0 * S.BoardRow1;
					C += FString::Printf(TEXT(" c (%+6.1f %+6.1f %+6.1f) cv %+5.2f"), Cv.Dot(S.BoardRow0), Cv.Dot(S.BoardRow1), Cv.Dot(S.BoardRow2),
						Cv.Dot(S.Velocity.GetSafeNormal()));
				}
				const SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim);
				R.Log += FString::Printf(TEXT("  f%3d %s/%s trick %d ch0 cat %d fl4 %d board %d z-above %.1f %s\n"), F, LexToString(S.Motion), LexToString(S.Control),
					S.TrickId, Ch ? Ch->Trick.Category() : -1, Ch ? int32(Ch->Trick.Flag(4)) : -1, S.bBoardValid ? 1 : 0,
					S.Position.Z + S.Position.Y * Ta, *C);
			}
			Anim.Step(Sim);
			Anim.FeedPose(Sim, true);
		}
		return R;
	}

	FRailTrickRun RideElysium62(const SSXAnim::FAflLibrary* Lib, FPatchWorld& World, double Speed, double Boost, int32 PrewindAt, int32 JumpAt,
		TFunctionRef<uint32(int32 T)> Air, int32 NumFrames)
	{
		using namespace SSXPadButton;
		constexpr int32 Rail62 = 62;
		FRailTrickRun R;
		const FSimRailSpline& S = World.GetRailSpline(Rail62);
		const FSimRailSegment& Seg = World.GetRailSegment(S.FirstSegment);
		const FVector P = Seg.Evaluate(0.3);
		FVector T = Seg.Derivative(0.3).GetSafeNormal();
		if (T.Z > 0.0)
		{
			T = -T;
		}
		FRiderSim Sim;
		FRiderInputDriver Input;
		SSXRiderSetup::InstallStates(Sim, Input, false);
		Sim.SetWorld(&World);
		Sim.Profile.Character = 10;
		Sim.Profile.TrickCharacter = 10;
		Sim.Profile.BoardClass = 0;
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, 10, 0);
		Sim.Reset();
		Sim.bLogTransitions = true;   // the player's transition log: ssx.Rail.Log writes its lines (and keeps the last)
		Sim.Spawn(P, FVector(T.X, T.Y, 0.0).GetSafeNormal(), Speed * T, 25.0, false);
		Sim.State.Position = P + FVector(0, 0, 25.0);
		Sim.State.Velocity = Speed * T;
		Sim.SetOrientationFromAxes(T, (FVector::UpVector - T.Z * T).GetSafeNormal());
		SSXAnim::FRiderAnimDriver Anim;
		Anim.Init(Lib, TEXT("brodi"), 0, 10);
		FPadMapper Mapper(10, 0);
		Mapper.SetLayout(0);
		FSSXPadDevice Pad;
		int32 Rail1 = -1;
		for (int32 F = 0; F < NumFrames && R.Land < 0; ++F)
		{
			uint32 Buttons = 0u;
			if (R.TakeOff >= 0)
			{
				Buttons = Air(F - R.TakeOff);
			}
			else if (Rail1 >= 0 && F >= Rail1 + PrewindAt && F < Rail1 + JumpAt)
			{
				Buttons = Cross;
			}
			const FPadActions A = Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(Buttons, FVector2D::ZeroVector, FVector2D::ZeroVector)));
			const bool bWasAir = Sim.State.Motion == ERiderMotion::Air;
			const ERiderControl ControlBefore = Sim.State.Control;
			const int32 TrickBefore = Sim.State.TrickId;
			Input.Step(Sim, A);
			FRiderState& St = Sim.State;
			if (Rail1 < 0 && St.Motion == ERiderMotion::RailSlide)
			{
				Rail1 = F;
			}
			if (Rail1 < 0 && F > 120)
			{
				R.Log += TEXT("  never caught rail 62 at the start\n");
				break;
			}
			if (St.Control != ControlBefore)
			{
				R.Log += FString::Printf(TEXT("  f%d %s -> %s (%s) p (%.0f %.0f %.0f) v (%.0f %.0f %.0f)\n"), F, LexToString(ControlBefore), LexToString(St.Control),
					LexToString(St.Motion), St.Position.X, St.Position.Y, St.Position.Z, St.Velocity.X, St.Velocity.Y, St.Velocity.Z);
			}
			if (Rail1 >= 0 && R.TakeOff < 0 && St.Motion == ERiderMotion::Air)
			{
				R.TakeOff = F;
				St.Velocity.Z += Boost;
			}
			if (R.TakeOff >= 0 && R.Catch < 0 && St.Motion == ERiderMotion::RailSlide)
			{
				R.Catch = F;
				R.ControlAtCatch = ControlBefore;
				R.TrickAtCatch = TrickBefore;
				R.BoxAtCatch = St.RailBox.Cur;
				R.RailLine = Sim.RailLogCatch;
				break;
			}
			if (R.TakeOff >= 0 && bWasAir && St.Motion == ERiderMotion::Ground)
			{
				R.Land = F;
			}
			if (R.TakeOff >= 0 && St.Motion == ERiderMotion::WipeOut)
			{
				R.Wipe = F;
				R.Log += FString::Printf(TEXT("  f%d wipe-out (prop hits %d, last instance %d)\n"), F, St.PropHitCount, St.PropHitInstance);
				break;
			}
			if (R.TakeOff >= 0)
			{
				FRiderRailQuery Q;
				Q.Origin = St.BoardOrigin;
				FRiderRailHit H;
				if (World.QueryRail(Q, H) && (H.Point - St.BoardOrigin).Size() < 300.0)
				{
					const FVector Cv = H.Point - St.BoardOrigin - 30.0 * St.BoardRow1;
					if (FMath::Abs(Cv.Dot(St.BoardRow1)) < 150.0)
					R.Log += FString::Printf(TEXT("  f%d %s/%s spline %d c (%+6.1f %+6.1f %+6.1f) cv %+6.1f v (%.0f %.0f %.0f) props %d\n"), F,
						LexToString(St.Motion), LexToString(St.Control), H.Spline, Cv.Dot(St.BoardRow0), Cv.Dot(St.BoardRow1), Cv.Dot(St.BoardRow2),
						Cv.Dot(St.Velocity.GetSafeNormal()), St.Velocity.X, St.Velocity.Y, St.Velocity.Z, St.PropHitCount);
				}
			}
			Anim.Step(Sim);
			Anim.FeedPose(Sim, true);
		}
		return R;
	}
}

namespace
{
	FRailTrickRun DropOntoElysium62(const SSXAnim::FAflLibrary* Lib, FPatchWorld& World, double Frac, double Height, int32 DropAt, int32 NumFrames)
	{
		using namespace SSXPadButton;
		constexpr int32 Rail62 = 62;
		FRailTrickRun R;
		const FSimRailSpline& S = World.GetRailSpline(Rail62);
		const FSimRailSegment& Seg = World.GetRailSegment(S.FirstSegment);
		const FVector P = Seg.Evaluate(0.3);
		FVector T = Seg.Derivative(0.3).GetSafeNormal();
		if (T.Z > 0.0)
		{
			T = -T;
		}
		FRiderSim Sim;
		FRiderInputDriver Input;
		SSXRiderSetup::InstallStates(Sim, Input, false);
		Sim.SetWorld(&World);
		Sim.Profile.Character = 10;
		Sim.Profile.TrickCharacter = 10;
		Sim.Profile.BoardClass = 0;
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, 10, 0);
		Sim.Reset();
		Sim.bLogTransitions = true;
		Sim.Spawn(P, FVector(T.X, T.Y, 0.0).GetSafeNormal(), 1700.0 * T, 25.0, false);
		Sim.State.Position = P + FVector(0, 0, 25.0);
		Sim.State.Velocity = 1700.0 * T;
		Sim.SetOrientationFromAxes(T, (FVector::UpVector - T.Z * T).GetSafeNormal());
		SSXAnim::FRiderAnimDriver Anim;
		Anim.Init(Lib, TEXT("brodi"), 0, 10);
		FPadMapper Mapper(10, 0);
		Mapper.SetLayout(0);
		FSSXPadDevice Pad;
		int32 Rail1 = -1;
		bool bDropped = false;
		for (int32 F = 0; F < NumFrames; ++F)
		{
			const uint32 Buttons = (R.TakeOff < 0 && Rail1 >= 0 && F >= Rail1 + 14 && F < Rail1 + 67) ? Cross : 0u;
			const FPadActions A = Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(Buttons, FVector2D::ZeroVector, FVector2D::ZeroVector)));
			const ERiderControl ControlBefore = Sim.State.Control;
			const int32 TrickBefore = Sim.State.TrickId;
			Input.Step(Sim, A);
			FRiderState& St = Sim.State;
			if (Rail1 < 0 && St.Motion == ERiderMotion::RailSlide)
			{
				Rail1 = F;
			}
			if (Rail1 < 0 && F > 120)
			{
				R.Log += TEXT("  never caught rail 62 at the start\n");
				break;
			}
			if (Rail1 >= 0 && R.TakeOff < 0 && St.Motion == ERiderMotion::Air)
			{
				R.TakeOff = F;
			}
			if (R.TakeOff >= 0 && !bDropped && F == R.TakeOff + DropAt)
			{
				if (St.Motion != ERiderMotion::Air)
				{
					R.Log += TEXT("  not in the air at the drop\n");
					break;
				}
				const int32 K = S.FirstSegment + FMath::Clamp(int32(Frac * S.SegmentCount), 0, S.SegmentCount - 1);
				const FSimRailSegment& G = World.GetRailSegment(K);
				const FVector Q = G.Evaluate(0.5);
				FVector Td = G.Derivative(0.5).GetSafeNormal();
				if (Td.Z > 0.0)
				{
					Td = -Td;
				}
				const FVector Side = FVector::CrossProduct(Td, FVector::UpVector).GetSafeNormal();
				FVector Nu = FVector::CrossProduct(Side, Td).GetSafeNormal();
				if (Nu.Z < 0.0)
				{
					Nu = -Nu;
				}
				Sim.SetOrientationFromAxes(FVector(Td.X, Td.Y, 0.0).GetSafeNormal(), FVector::UpVector);
				St.Position = Q + Height * Nu;
				St.SphereFrameP = St.Position;   // 0x1267A0
				St.SphereFrameQ = St.Orientation;
				St.Velocity = 1880.0 * Td - 2760.0 * Nu;
				St.RailBox.Cur = St.RailBox.Target;
				bDropped = true;
				R.Log += FString::Printf(TEXT("  f%d drop: segment %d q (%.0f %.0f %.0f) n (%.2f %.2f %.2f) v (%.0f %.0f %.0f) %s\n"), F, K, Q.X, Q.Y, Q.Z,
					Nu.X, Nu.Y, Nu.Z, St.Velocity.X, St.Velocity.Y, St.Velocity.Z, LexToString(St.Control));
			}
			if (bDropped && R.Catch < 0 && St.Motion == ERiderMotion::RailSlide)
			{
				R.Catch = F;
				R.ControlAtCatch = ControlBefore;
				R.TrickAtCatch = TrickBefore;
				R.RailLine = Sim.RailLogCatch;
				break;
			}
			if (bDropped && St.Motion == ERiderMotion::WipeOut)
			{
				R.Wipe = F;
				R.Log += FString::Printf(TEXT("  f%d wipe-out (prop hits %d, last instance %d)\n"), F, St.PropHitCount, St.PropHitInstance);
				break;
			}
			if (bDropped && St.Motion == ERiderMotion::Ground)
			{
				R.Land = F;
				break;
			}
			if (bDropped)
			{
				R.Log += FString::Printf(TEXT("  f%d %s/%s props %d (instance %d) | %s\n"), F, LexToString(St.Motion), LexToString(St.Control),
					St.PropHitCount, St.PropHitInstance, *Sim.RailLogCatch);
			}
			Anim.Step(Sim);
			Anim.FeedPose(Sim, true);
		}
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailFromTrickTest, "SSX.Rail.FromTrick", RailTrickFlags)

bool FSSXRailFromTrickTest::RunTest(const FString& Parameters)
{
	using namespace SSXPadButton;
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	if (!Lib)
	{
		SSXTest::Skip(*this, TEXT("SimData/anim_clips.bin missing (tools/export_anim_clips.py); skipped"));
		return true;
	}
	auto Prewind = [](int32 F, int32 Rail1, bool bFromRail)
	{
		return bFromRail ? (Rail1 >= 0 && F >= Rail1 + 15 && F < Rail1 + 45) : (F >= 30 && F < 90);
	};
	struct FTrick { const TCHAR* Name; TFunction<uint32(int32 T)> Air; bool bCatch; };
	const TArray<FTrick> Tricks = {
		{ TEXT("ollie"), [](int32) -> uint32 { return 0u; }, true },
		{ TEXT("spin held to the rail"), [](int32 T) -> uint32 { return T >= 6 ? Right : 0u; }, true },
		{ TEXT("flip"), [](int32 T) -> uint32 { return T < 40 ? Up : 0u; }, true },
		{ TEXT("spin then grab, released"), [](int32 T) -> uint32 { return T < 20 ? Left : ((T >= 24 && T < 40) ? R1 : 0u); }, true },
		// 0x125CB8
		{ TEXT("grab held to the rail"), [](int32 T) -> uint32 { return T >= 4 ? R1 : 0u; }, false },
	};
	for (const bool bFromRail : { false, true })
	{
		for (const FTrick& K : Tricks)
		{
			auto Script = [&](int32 F, int32 Rail1, int32 TakeOff) -> uint32
			{
				if (TakeOff >= 0) { return K.Air(F - TakeOff); }
				return Prewind(F, Rail1, bFromRail) ? Cross : 0u;
			};
			const FRailTrickRun R = RideOntoRail(Lib, 15.0, 60.0, 600.0, bFromRail, Script, 500);
			const FString Name = FString::Printf(TEXT("%s, %s"), bFromRail ? TEXT("rail -> jump -> rail") : TEXT("ground -> rail"), K.Name);
			AddInfo(FString::Printf(TEXT("%s: take-off f%d, catch f%d (%s, spin %.2f, flip %.2f, trick %d, vz %.0f, box %.2f), landing f%d, wipe-out f%d"), *Name,
				R.TakeOff, R.Catch, LexToString(R.ControlAtCatch), R.SpinAtCatch, R.FlipAtCatch, R.TrickAtCatch, R.VzAtCatch, R.BoxAtCatch, R.Land, R.Wipe));
			TestTrue(Name + TEXT(": took off"), R.TakeOff >= 0);
			bool bOk = TestEqual(Name + TEXT(": caught the rail from the air"), R.Catch >= 0, K.bCatch);
			if (K.bCatch && R.Catch >= 0)
			{
				bOk &= TestEqual(Name + TEXT(": caught from the Spin control (trick in progress)"), R.ControlAtCatch, ERiderControl::Spin);
				bOk &= TestTrue(Name + TEXT(": caught on the way down"), R.VzAtCatch < 0.0);
			}
			if (!bOk)
			{
				UE_LOG(LogTemp, Display, TEXT("[railtrick] %s\n%s"), *Name, *R.Log);
			}
		}
	}
	{
		FPatchWorld World;
		FString Err;
		if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("elysium")), &Err) || World.NumRailSplines() <= 62)
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("elysium skipped: %s"), *Err));
		}
		else
		{
			World.bAlternateSetMode = true;
			World.SetRailEnabled(62, true);
			struct FCase { double Boost; const TCHAR* Name; TFunction<uint32(int32 T)> Air; };
			const TArray<FCase> Cases = {
				{ 0.0, TEXT("ollie"), [](int32) -> uint32 { return 0u; } },
				{ 1000.0, TEXT("~2 s flight, ollie"), [](int32) -> uint32 { return 0u; } },
				{ 1000.0, TEXT("~2 s flight, flip then spin"), [](int32 T) -> uint32 { return T < 30 ? Up : ((T >= 34 && T < 60) ? Right : 0u); } },
			};
			for (const FCase& K : Cases)
			{
				const FRailTrickRun R = RideElysium62(Lib, World, 1700.0, K.Boost, 14, 67, K.Air, 900);
				const FString Name = FString::Printf(TEXT("Elysium rail 62 -> jump -> rail 62, %s"), K.Name);
				AddInfo(FString::Printf(TEXT("%s: take-off f%d, catch f%d (%s), landing f%d, wipe-out f%d; %s"), *Name, R.TakeOff, R.Catch,
					LexToString(R.ControlAtCatch), R.Land, R.Wipe, *R.RailLine));
				bool bOk = TestTrue(Name + TEXT(": took off the rail"), R.TakeOff >= 0);
				bOk &= TestTrue(Name + TEXT(": caught rail 62 again from the air"), R.Catch >= 0);
				if (R.Catch >= 0)
				{
					bOk &= TestEqual(Name + TEXT(": caught from the Spin control"), R.ControlAtCatch, ERiderControl::Spin);
					bOk &= TestTrue(Name + TEXT(": ssx.Rail.Log wrote the catch line (rail 62, caught)"),
						R.RailLine.Contains(TEXT(" rail 62 ")) && R.RailLine.EndsWith(TEXT("-> caught")));
				}
				if (!bOk)
				{
					UE_LOG(LogTemp, Display, TEXT("[railtrick] %s\n%s"), *Name, *R.Log);
				}
			}
			// 0x125088
			for (const double Frac : { 0.35, 0.5, 0.65 })
			{
				const FRailTrickRun R = DropOntoElysium62(Lib, World, Frac, 215.0, 20, 400);
				const FString Name = FString::Printf(TEXT("Elysium rail 62, falling onto it in Spin (live f1803, spline %.2f)"), Frac);
				AddInfo(FString::Printf(TEXT("%s: catch f%d (%s), wipe-out f%d, landing f%d; %s"), *Name, R.Catch, LexToString(R.ControlAtCatch), R.Wipe,
					R.Land, *R.RailLine));
				bool bOk = TestTrue(Name + TEXT(": caught rail 62"), R.Catch >= 0);
				bOk &= TestTrue(Name + TEXT(": no prop wipe-out"), R.Wipe < 0);
				if (R.Catch >= 0)
				{
					bOk &= TestEqual(Name + TEXT(": caught from the Spin control"), R.ControlAtCatch, ERiderControl::Spin);
				}
				if (!bOk)
				{
					UE_LOG(LogTemp, Display, TEXT("[railtrick] %s\n%s"), *Name, *R.Log);
				}
			}
		}
	}
	return !HasAnyErrors();
}

#endif
