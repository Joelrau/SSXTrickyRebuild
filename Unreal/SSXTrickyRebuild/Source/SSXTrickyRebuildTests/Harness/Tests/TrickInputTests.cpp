#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Input/SSXInputSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Air/TrickName.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "SnowboarderPawn.h"
#include "UI/SSXFeTrickBook.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TrickTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FScriptFrame
	{
		uint32 Buttons = 0;
		FVector2D Left = FVector2D::ZeroVector;   // UE convention: +X right, +Y up
	};

	struct FTrickRun
	{
		int32 TakeOffFrame = -1, LandFrame = -1, AirFrames = 0;
		int32 MaxPrewindFrame = -1;
		double MaxPrewindA = 0.0, MaxSpinDeg = 0.0, MaxFlipDeg = 0.0, MaxSpinRateDeg = 0.0, SpinStat = 0.0;
		bool bSawSpinControl = false, bSawGrab = false, bWipeOut = false, bGrabAnim = false;
		bool bSteppedContext = false;
		int32 Spins180 = 0;
		FString HudText;
		int32 GrabId = FRiderInputWord::NoTrick;
		int32 Points = 0, Score = 0;
		double TotalSpinDeg = 0.0;
		FString Verdict;
		FString Log;
		uint32 W0 = 0, W1 = 0;
		bool bRidingSwitchAtTakeOff = false;
	};

	FTrickRun RunScript(FAutomationTestBase* Test, FPatchWorld& World, const FVector& Pos, const FVector& Heading,
		double Speed, int32 Layout, int32 Character, int32 BoardClass, TFunctionRef<FScriptFrame(int32)> Script, int32 Frames,
		TFunction<void(FRiderSim&)> BeforeSpawn = nullptr, double Drop = 0.0)
	{
		FTrickRun R;
		FRiderSim Sim;
		FRiderInputDriver Driver;
		SSXRiderSetup::InstallStates(Sim, Driver, false);
		Sim.SetWorld(&World);
		Sim.Profile.Character = Character;
		Sim.Profile.TrickCharacter = Character;
		Sim.Profile.BoardClass = BoardClass;
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, Character, BoardClass);
		R.SpinStat = Sim.Profile.Stat(0x1A);
		Sim.Reset();
		if (BeforeSpawn) { BeforeSpawn(Sim); }
		Sim.Spawn(Pos, Heading, FVector::ZeroVector, Drop, false);   // 0x156618
		// 0x11CAE8
		if (Drop > 0.0) { Sim.State.TrickPhase = 0; }
		Sim.State.Velocity = Speed * Sim.State.ContactForward;
		FPadMapper Mapper(Character, BoardClass);
		Mapper.SetLayout(Layout);
		FSSXPadDevice Pad;
		ERiderMotion PrevMotion = Sim.State.Motion;
		double PrevSpin = 0.0;
		for (int32 F = 0; F < Frames; ++F)
		{
			const FScriptFrame In = Script(F);
			const FPadActions A = Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(In.Buttons, In.Left, FVector2D::ZeroVector)));
			Driver.Step(Sim, A);
			const FRiderState& S = Sim.State;
			if (FMath::Abs(S.PrewindA.Cur) > R.MaxPrewindA) { R.MaxPrewindA = FMath::Abs(S.PrewindA.Cur); R.MaxPrewindFrame = F; }
			if (S.Motion == ERiderMotion::Air)
			{
				if (PrevMotion != ERiderMotion::Air && R.TakeOffFrame < 0) { R.TakeOffFrame = F; }
				++R.AirFrames;
				R.MaxSpinDeg = FMath::Max(R.MaxSpinDeg, FMath::Abs(FMath::RadiansToDegrees(S.SpinAngle)));
				if (PrevMotion == ERiderMotion::Air)
				{
					R.MaxSpinRateDeg = FMath::Max(R.MaxSpinRateDeg, FMath::Abs(FMath::RadiansToDegrees(S.SpinAngle - PrevSpin)) * 60.0);
				}
				PrevSpin = S.SpinAngle;
				R.MaxFlipDeg = FMath::Max(R.MaxFlipDeg, FMath::Abs(FMath::RadiansToDegrees(S.FlipAngle)));
				R.bSawSpinControl |= S.Control == ERiderControl::Spin;
				if (S.TrickId != FRiderInputWord::NoTrick) { R.bSawGrab = true; R.GrabId = S.TrickId; }
				if (const SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(Sim))
				{
					R.bGrabAnim |= Ch->Trick.IsActive() && Ch->Trick.Category() == SSXAnim::CategoryGrab;
				}
			}
			else if (PrevMotion == ERiderMotion::Air && R.LandFrame < 0)
			{
				R.LandFrame = F;
				R.bWipeOut = S.Motion == ERiderMotion::WipeOut;
			}
			if (F % 6 == 0 || S.Motion != PrevMotion)
			{
				R.Log += FString::Printf(TEXT("  f%4d btn %04x stick (%+.1f %+.1f) word %08x %s/%s prewind %+.2f spin %+7.1f flip %+6.1f trick %d v %.0f\n"),
					F, In.Buttons, In.Left.X, In.Left.Y, Driver.GetLastWord(), LexToString(S.Motion), LexToString(S.Control),
					S.PrewindA.Cur, FMath::RadiansToDegrees(S.SpinAngle), FMath::RadiansToDegrees(S.FlipAngle), S.TrickId, S.Velocity.Size());
			}
			PrevMotion = S.Motion;
		}
		R.bSteppedContext = SSXAir::GetAirContext(Sim) != nullptr && SSXAnim::FindChannel(Sim) != nullptr;
		if (const SSXAir::FAirContext* Ctx = SSXAir::GetAirContext(Sim))
		{
			R.Score = Ctx->Score.Score;
			R.HudText = Ctx->Score.TrickText.TrimEnd();
			R.W0 = Ctx->Score.Announced.W0;
			R.W1 = Ctx->Score.Announced.W1;
			R.bRidingSwitchAtTakeOff = Ctx->Score.bSegmentFakie;
			R.Spins180 = Ctx->LastLanding.Summary.Spins180;
			if (Ctx->LastLanding.bValid)
			{
				R.Points = Ctx->LastLanding.Points;
				R.TotalSpinDeg = Ctx->LastLanding.TotalSpinDeg;
				R.Verdict = Ctx->LastLanding.Verdict == SSXAir::ELandingVerdict::WipeOut ? TEXT("wipeout")
					: (Ctx->LastLanding.Verdict == SSXAir::ELandingVerdict::LandHard ? TEXT("hard") : TEXT("normal"));
			}
		}
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnTrickInputTest, "SSX.Pawn.TrickInput", TrickTestFlags)

bool FSSXPawnTrickInputTest::RunTest(const FString& Parameters)
{
	using namespace SSXPadButton;
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	if (!World.GetSurfaces().IsLoaded())
	{
		AddError(TEXT("SimData/surfaces.bin not available"));
		return false;
	}
	const ASnowboarderPawn* Defaults = GetDefault<ASnowboarderPawn>();
	const int32 Character = Defaults->Character, BoardClass = Defaults->BoardClass;
	TestEqual(TEXT("the pawn defaults to controller configuration 0 (BTNMAP0)"), Defaults->PadLayout, 0);

	// 0x105A60
	constexpr int32 HoldFrom = 30, ReleaseAt = 105;
	for (const int32 Layout : { 0, 1 })
	{
		const uint32 DRight = Layout == 0 ? Right : 0u;
		const FVector2D SRight = Layout == 1 ? FVector2D(1.0, 0.0) : FVector2D::ZeroVector;
		auto Script = [DRight, SRight](int32 F)
		{
			FScriptFrame In;
			if (F >= HoldFrom && F < ReleaseAt) { In.Buttons = Cross | DRight; In.Left = SRight; }
			else if (F >= ReleaseAt && F < ReleaseAt + 12) { In.Buttons = R1 | DRight; In.Left = SRight; }
			else if (F >= ReleaseAt + 12 && F < ReleaseAt + 30) { In.Buttons = DRight; In.Left = SRight; }
			return In;
		};
		const FTrickRun R = RunScript(this, World, FVector::ZeroVector, FVector(0, 1, 0), 1500.0, Layout, Character, BoardClass, Script, 400);
		UE_LOG(LogTemp, Display, TEXT("[trickinput] layout %d char %d class %d\n%s"), Layout, Character, BoardClass, *R.Log);
		const FString L = FString::Printf(TEXT("layout %d: "), Layout);
		AddInfo(L + FString::Printf(TEXT("take-off f%d, %d air frames, landing f%d (%s), max prewind %.2f, max spin %.0f deg (%d x 180), grab %d (anim %d), points %d, score %d, HUD text '%s'"),
			R.TakeOffFrame, R.AirFrames, R.LandFrame, *R.Verdict, R.MaxPrewindA, R.MaxSpinDeg, R.Spins180, R.GrabId, R.bGrabAnim, R.Points, R.Score, *R.HudText));
		TestTrue(L + TEXT("the stepped sim finds its air context and animation channel"), R.bSteppedContext);
		TestTrue(L + TEXT("Cross + right winds up (Prewind)"), R.MaxPrewindA > 0.5);
		TestTrue(L + TEXT("release jumps"), R.TakeOffFrame > 0);
		TestTrue(L + TEXT("trick control entered"), R.bSawSpinControl);
		TestTrue(L + TEXT("grab registered"), R.bSawGrab);
		TestTrue(L + TEXT("grab animation plays on the stepped sim's channel"), R.bGrabAnim);
		TestTrue(L + TEXT("spin recognised as a 180"), R.MaxSpinDeg >= 135.0 && R.Spins180 != 0);
		TestTrue(L + TEXT("landed"), R.LandFrame > 0 && !R.bWipeOut);
		TestTrue(L + TEXT("landing scored"), R.Points > 0 && R.Score > 0);
		TestFalse(L + TEXT("trick name text for the HUD"), R.HudText.IsEmpty());

		if (SSXRider::FRiderTable::Shared().IsLoaded())
		{
			const FTrickRun M = RunScript(this, World, FVector::ZeroVector, FVector(0, 1, 0), 1500.0, Layout, 4, 0, Script, 400);
			const double G = FMath::Lerp(0.5449, 1.2191, M.SpinStat);
			const double Expected = FMath::RadiansToDegrees(11.5172 * G * M.MaxPrewindA);
			AddInfo(L + FString::Printf(TEXT("Moby / class 0: %d air frames, max spin rate %.0f deg/s (K_PRE G_spin prewind_a = %.0f for stat %.0f, prewind %.3f), %s landing, %d pts, '%s'"),
				M.AirFrames, M.MaxSpinRateDeg, Expected, M.SpinStat * 255.0, M.MaxPrewindA, *M.Verdict, M.Points, *M.HudText));
			TestTrue(L + TEXT("class 0: prewind spin rate = K_PRE G_spin prewind_a (5 %)"), FMath::Abs(M.MaxSpinRateDeg - Expected) < 0.05 * Expected);
			TestEqual(L + TEXT("class 0: the 180 lands normally"), M.Verdict, FString(TEXT("normal")));
		}
	}

	{
		auto StickOnly = [](int32 F)
		{
			FScriptFrame In;
			if (F >= HoldFrom && F < ReleaseAt) { In.Buttons = Cross; In.Left = FVector2D(1.0, 0.0); }
			return In;
		};
		const FTrickRun R = RunScript(this, World, FVector::ZeroVector, FVector(0, 1, 0), 1500.0, 0, Character, BoardClass, StickOnly, 140);
		TestEqual(TEXT("layout 0: the stick does not wind up a prewind spin"), R.MaxPrewindA, 0.0);
	}
	return !HasAnyErrors();
}

// 0x105A60, 0x126E30, 0x100908, 0x102158
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnFlipDirectionTest, "SSX.Pawn.FlipDirection", TrickTestFlags)

bool FSSXPawnFlipDirectionTest::RunTest(const FString& Parameters)
{
	using namespace SSXPadButton;
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	if (!World.GetSurfaces().IsLoaded())
	{
		AddError(TEXT("SimData/surfaces.bin not available"));
		return false;
	}
	const ASnowboarderPawn* Defaults = GetDefault<ASnowboarderPawn>();
	const int32 Character = Defaults->Character, BoardClass = Defaults->BoardClass;
	constexpr int32 HoldFrom = 60, ReleaseAt = 105;
	struct FFlipRun { int32 FirstSign = 0; double MaxAbsDeg = 0.0; bool bSpin = false; FString Stance; };
	auto Run = [&](int32 Layout, int32 ProfileStance, bool bSwitch, int32 FlipIn, bool bLate)
	{
		FFlipRun Out;
		FRiderSim Sim;
		FRiderInputDriver Driver;
		SSXRiderSetup::InstallStates(Sim, Driver, false);
		Sim.SetWorld(&World);
		Sim.Profile.Character = Character;
		Sim.Profile.TrickCharacter = Character;
		Sim.Profile.BoardClass = BoardClass;
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, Character, BoardClass);
		Sim.Profile.Stance = ProfileStance;
		Sim.Reset();
		Sim.Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = 1500.0 * Sim.State.ContactForward;
		Sim.State.bStanceGoofy = (ProfileStance != 0) != bSwitch;
		Out.Stance = FString::Printf(TEXT("profile %s, %s, +0x1B4 %d"), ProfileStance ? TEXT("goofy") : TEXT("regular"),
			bSwitch ? TEXT("switch") : TEXT("normal"), Sim.State.bStanceGoofy ? 1 : 0);
		FPadMapper Mapper(Character, BoardClass);
		Mapper.SetLayout(Layout);
		FSSXPadDevice Pad;
		// "Up" / "Down" are the d-pad in BTNMAP0 and the d-pad or the left stick in BTNMAP1 (UE stick +Y = up)
		const uint32 DPad = FlipIn < 0 ? Up : Down;
		const FVector2D Stick = Layout == 1 ? FVector2D(0.0, FlipIn < 0 ? 1.0 : -1.0) : FVector2D::ZeroVector;
		const uint32 DBtn = Layout == 1 ? 0u : DPad;
		for (int32 F = 0; F < 360; ++F)
		{
			uint32 Buttons = 0;
			FVector2D Left = FVector2D::ZeroVector;
			if (F >= HoldFrom && F < ReleaseAt)
			{
				Buttons = Cross | (bLate ? 0u : DBtn);
				Left = bLate ? FVector2D::ZeroVector : Stick;
			}
			else if (bLate && F >= ReleaseAt + 6 && F < ReleaseAt + 16)
			{
				Buttons = DPad;
			}
			else if (!bLate && F >= ReleaseAt && F < ReleaseAt + 30)
			{
				Buttons = DBtn;
				Left = Stick;
			}
			const FPadActions A = Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(Buttons, Left, FVector2D::ZeroVector)));
			Driver.Step(Sim, A);
			const FRiderState& S = Sim.State;
			if (S.Motion == ERiderMotion::Air)
			{
				Out.bSpin |= S.Control == ERiderControl::Spin;
				if (Out.FirstSign == 0 && FMath::Abs(S.FlipAngle) > 1e-6)
				{
					Out.FirstSign = S.FlipAngle > 0.0 ? 1 : -1;
				}
				Out.MaxAbsDeg = FMath::Max(Out.MaxAbsDeg, FMath::Abs(FMath::RadiansToDegrees(S.FlipAngle)));
			}
			else if (Out.FirstSign != 0)
			{
				break;
			}
		}
		return Out;
	};
	int32 Cases = 0, Good = 0;
	for (const int32 Layout : { 0, 1 })
	{
		for (const int32 ProfileStance : { 0, 1 })
		{
			for (const bool bSwitch : { false, true })
			{
				for (const int32 FlipIn : { -1, 1 })
				{
					const int32 Want = -FlipIn;   // Up: + flip angle (front flip, nose down), Down: - (back flip)
					for (const bool bLate : { false, true })
					{
						const FFlipRun R = Run(Layout, ProfileStance, bSwitch, FlipIn, bLate);
						++Cases;
						const bool bOk = R.bSpin && R.FirstSign == Want && R.MaxAbsDeg > 10.0;
						Good += bOk;
						const FString Msg = FString::Printf(TEXT("layout %d, %s, %s %s: flip angle sign %+d (want %+d), max |flip| %.0f deg"),
							Layout, *R.Stance, bLate ? TEXT("late") : TEXT("prewind"), FlipIn < 0 ? TEXT("Up") : TEXT("Down"),
							R.FirstSign, Want, R.MaxAbsDeg);
						AddInfo(Msg);
						TestTrue(Msg, bOk);
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("flip direction: %d / %d cases match the original"), Good, Cases));
	return !HasAnyErrors();
}

// 0x1284E0, 0x10B0D0, FUN_00156618, 0x116AB8, FUN_00157C18, 0x103EB8, 0x10C198
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnBookMuteAirTest, "SSX.Pawn.BookMuteAir", TrickTestFlags)

bool FSSXPawnBookMuteAirTest::RunTest(const FString& Parameters)
{
	using namespace SSXPadButton;
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	if (!World.GetSurfaces().IsLoaded() || !SSXRider::FRiderTable::Shared().IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("SimData (surfaces.bin / riders.bin) not available: skipped"));
		return true;
	}
	const SSXFeTrickBook::FTable& Book = SSXFeTrickBook::FTable::Get();
	constexpr int32 Elise = 7;
	auto Ollie = [](int32 F)
	{
		FScriptFrame In;
		if (F >= 30 && F < 75) { In.Buttons = Cross; }
		return In;
	};
	// 0x11CAE8, 0x156618
	auto Grab = [](int32 F)
	{
		FScriptFrame In;
		if (F >= 4 && F < 44) { In.Buttons = R1; }
		return In;
	};
	for (const int32 Character : { Elise, 1 /* Kaori: regular */ })
	{
		for (const int32 BoardClass : { 0, 1, 2 })
		{
			for (const int32 Layout : { 0, 1 })
			{
				for (const bool bSwitch : { false, true })
				{
					auto Setup = [bSwitch](FRiderSim& Sim) { if (bSwitch) { Sim.State.bStanceGoofy = !Sim.State.bStanceGoofy; } };
					const FString L = FString::Printf(TEXT("char %d class %d layout %d %s: "), Character, BoardClass, Layout,
						bSwitch ? TEXT("riding switch") : TEXT("natural stance"));
					const FTrickRun O = RunScript(this, World, FVector::ZeroVector, FVector(0, 1, 0), 1500.0, Layout, Character, BoardClass,
						Ollie, 160, Setup);
					TestTrue(L + TEXT("ollie take-off"), O.TakeOffFrame > 0);
					TestEqual(L + TEXT("ollie: switch flag +0x10 = (+0x1B4 != profile +0x40)"), O.bRidingSwitchAtTakeOff, bSwitch);
					const FTrickRun R = RunScript(this, World, FVector::ZeroVector, FVector(0, 1, 0), 1500.0, Layout, Character, BoardClass,
						Grab, 240, Setup, 900.0);
					AddInfo(L + FString::Printf(TEXT("switch flag %d, %d air frames, grab %d, landing %s, record %08x %08x '%s'"),
						R.bRidingSwitchAtTakeOff ? 1 : 0, R.AirFrames, R.GrabId, *R.Verdict, R.W0, R.W1, *R.HudText));
					TestEqual(L + TEXT("switch flag +0x10"), R.bRidingSwitchAtTakeOff, bSwitch);
					TestTrue(L + TEXT("grab named in the record (w0 bits 8..15)"), ((R.W0 >> 8) & 0xFF) != 0);
					TestEqual(L + TEXT("prefix (w1 bits 16..19): 1 Switch only when riding switch"), int32((R.W1 >> 16) & 0xF), bSwitch ? 1 : 0);
					if (Character == Elise)
					{
						if (SSXTrick::FTrickTables::Get().IsLoaded())
						{
							TestEqual(L + TEXT("text"), R.HudText, FString(bSwitch ? TEXT("Switch Mute Air") : TEXT("Mute Air")));
						}
						if (Book.IsLoaded())
						{
							const SSXFeTrickBook::FEntry* E = Book.Find(Elise, 0);
							TestTrue(L + TEXT("Elise's book entry 0 (Mute Air) matches: FUN_00157C18 ignores the Switch prefix"),
								E && SSXFeTrickBook::Matches(*E, R.W0, R.W1));
						}
					}
				}
			}
		}
	}
	return !HasAnyErrors();
}

// 0x155E68
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnBackFlipScoreTest, "SSX.Pawn.BackFlipScore", TrickTestFlags)

bool FSSXPawnBackFlipScoreTest::RunTest(const FString& Parameters)
{
	using namespace SSXPadButton;
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	if (!World.GetSurfaces().IsLoaded())
	{
		AddError(TEXT("SimData/surfaces.bin not available"));
		return false;
	}
	const ASnowboarderPawn* Defaults = GetDefault<ASnowboarderPawn>();
	const int32 Character = Defaults->Character, BoardClass = Defaults->BoardClass;
	constexpr int32 HoldFrom = 30, ReleaseAt = 105;
	constexpr double Lift = 1000.0;
	int32 Landed = 0, FlipsScored[2] = { 0, 0 }, ShortScored = 0;
	for (const int32 FlipIn : { 1, -1 })   // Down (back flip) / Up (front flip)
	for (const int32 Stance : { 0, 1 })
	for (const bool bSpin : { false, true })
	for (const int32 Extra : { 0, 10, 20, 30, 45, 60, 90 })
	{
		FRiderSim Sim;
		FRiderInputDriver Driver;
		SSXRiderSetup::InstallStates(Sim, Driver, false);
		Sim.SetWorld(&World);
		Sim.Profile.Character = Character;
		Sim.Profile.TrickCharacter = Character;
		Sim.Profile.BoardClass = BoardClass;
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, Character, BoardClass);
		Sim.Profile.Stance = Stance;
		Sim.Reset();
		Sim.Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = 1500.0 * Sim.State.ContactForward;
		FPadMapper Mapper(Character, BoardClass);
		Mapper.SetLayout(0);
		FSSXPadDevice Pad;
		const uint32 Dir = (FlipIn > 0 ? Down : Up) | (bSpin ? Right : 0u);
		double MaxFlipDeg = 0.0, LastFlipDeg = 0.0;
		bool bAir = false, bLanded = false;
		for (int32 F = 0; F < 600 && !bLanded; ++F)
		{
			uint32 Buttons = 0;
			if (F >= HoldFrom && F < ReleaseAt) { Buttons = Cross | Dir; }
			else if (F >= ReleaseAt && F < ReleaseAt + Extra) { Buttons = Dir; }
			Driver.Step(Sim, Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(Buttons, FVector2D::ZeroVector, FVector2D::ZeroVector))));
			FRiderState& S = Sim.State;
			if (S.Motion == ERiderMotion::Air)
			{
				if (!bAir) { S.Velocity.Z += Lift; }
				bAir = true;
				LastFlipDeg = FMath::RadiansToDegrees(S.FlipAngle);
				MaxFlipDeg = FMath::Max(MaxFlipDeg, FMath::Abs(LastFlipDeg));
			}
			else if (bAir)
			{
				bLanded = true;
			}
		}
		const FString L = FString::Printf(TEXT("%s stance %d%s +%d frames: "), FlipIn > 0 ? TEXT("back") : TEXT("front"), Stance,
			bSpin ? TEXT(" spin") : TEXT(""), Extra);
		const SSXAir::FAirContext* Ctx = SSXAir::GetAirContext(Sim);
		if (!bLanded || !Ctx || !Ctx->LastLanding.bValid)
		{
			AddError(L + TEXT("no landing"));
			continue;
		}
		++Landed;
		const bool bWipeOut = Ctx->LastLanding.Verdict == SSXAir::ELandingVerdict::WipeOut;
		const int32 Best = SSXTrick::Round180(int32(MaxFlipDeg));
		const int32 Expected = -FlipIn * (Best - Best % 360) / 360;
		const int32 Flips = Ctx->LastLanding.Summary.Flips;
		const uint32 W1 = Ctx->Score.Announced.W1;
		AddInfo(L + FString::Printf(TEXT("max |flip| %.1f, last %.1f deg, %s, flips %d (rule %d), %d pts, w1 %08x"), MaxFlipDeg,
			LastFlipDeg, bWipeOut ? TEXT("wipeout") : TEXT("landed"), Flips, Expected, Ctx->LastLanding.Points, W1));
		if (bWipeOut)
		{
			TestEqual(L + TEXT("a wipe-out scores nothing (0x156DE0)"), Ctx->LastLanding.Points, 0);
			continue;
		}
		TestEqual(L + TEXT("summary flips = the 0x155E68 rule on the reported angles"), Flips, Expected);
		if (Flips != 0)
		{
			++FlipsScored[FlipIn > 0 ? 0 : 1];
			ShortScored += MaxFlipDeg < 359.5 ? 1 : 0;
			const uint32 Name = (W1 >> 8) & 15;
			TestTrue(L + TEXT("named as a flip (Front / Back Flip, Rodeo / Misty, or a named flip)"),
				Name != 0 || (Ctx->Score.Announced.W0 & 0xFF) != 0);
			TestTrue(L + TEXT("each flip pays 0.2499898 of V"),
				Ctx->LastLanding.Points >= SSXAir::FTrickScore::PointsFor(FMath::Abs(Flips) * SSXAir::TrickValueFlipStep, 1.0));
		}
	}
	AddInfo(FString::Printf(TEXT("%d landings, scored flips: %d back, %d front"), Landed, FlipsScored[0], FlipsScored[1]));
	TestTrue(TEXT("scripted back flips land and score"), FlipsScored[0] > 0);
	TestTrue(TEXT("flips landed short of 360 (271..359 deg) score, as 0x155E68 rounds to 180"), ShortScored > 0);
	return !HasAnyErrors();
}

#endif