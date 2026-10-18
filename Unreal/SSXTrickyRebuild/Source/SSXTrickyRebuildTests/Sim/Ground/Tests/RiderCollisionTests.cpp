// 0x14FFC8, 0x150248
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/SSXInputSetup.h"
#include "Sim/Anim/AflClips.h"
#include "Race/AIInput.h"
#include "Race/RaceRandom.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Ground/RiderCollision.h"
#include "Sim/Ground/RiderPairs.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CollisionTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Report(FAutomationTestBase* Test, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Collision] %s"), *Msg);
	}

	void MakeRider(FRiderSim& Sim, const FPatchWorld& World, const FVector& Pos, const FVector& Vel)
	{
		Sim.SetWorld(&World);
		SSXRiderSetup::RegisterStates(Sim);
		Sim.Profile.MassByte = 128;
		Sim.Reset();
		Sim.Spawn(Pos, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = Vel;
		Sim.State.GroundNormal = FVector::UpVector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCollisionRidersTest, "SSX.Collision.Riders", CollisionTestFlags)

bool FSSXCollisionRidersTest::RunTest(const FString& Parameters)
{
	using namespace SSXRiderHit;
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);
	const FVector U(0, 1, 0);

	{
		FRiderSim A;
		MakeRider(A, World, FVector(0, 0, 0), FVector::ZeroVector);
		TestTrue(TEXT("mass 239.75512"), FMath::IsNearlyEqual(PairMass(A), 239.75512445490196, 1e-4));   // 0x11D7B0
		A.State.BoostStrength = 1.0;
		TestTrue(TEXT("boosting x 8.999995"), FMath::IsNearlyEqual(PairMass(A), 239.75512445490196 * 8.999995, 1e-3));
	}
	// head-on 1500 cm/s vs at rest: J -750 / +750, both knocked down (model: knockdown, knockdown, none rewarded)
	{
		FRiderSim A, B;
		MakeRider(A, World, FVector(0, 0, 0), FVector(0, 1500, 0));
		MakeRider(B, World, FVector(0, 60, 0), FVector::ZeroVector);
		FPairResult R;
		Exchange(A, B, U, FVector(0, 30, 50), false, false, false, R);
		Report(this, FString::Printf(TEXT("head-on 1500: JA %.4f JB %.4f -> %d / %d (model -750 / 750, knockdown / knockdown)"),
			R.JA, R.JB, int32(R.HitA), int32(R.HitB)));
		TestTrue(TEXT("JA -750"), FMath::IsNearlyEqual(R.JA, -750.0, 1e-3));   // 0x123FC8
		TestTrue(TEXT("JB +750"), FMath::IsNearlyEqual(R.JB, 750.0, 1e-3));
		TestTrue(TEXT("A knocked down"), R.HitA == EHit::Knockdown && A.State.Motion == ERiderMotion::WipeOut);
		TestTrue(TEXT("B knocked down"), R.HitB == EHit::Knockdown && B.State.Motion == ERiderMotion::WipeOut);
		TestTrue(TEXT("impact normal u, speed Jc clamped to -500"), A.State.LastImpact.Normal.Equals(U) && A.State.LastImpact.Speed == -500.0);
	}
	// 300 cm/s touch: J -/+150, push only, velocities 150 / 150 (model)
	{
		FRiderSim A, B;
		MakeRider(A, World, FVector(0, 0, 0), FVector(0, 300, 0));
		MakeRider(B, World, FVector(0, 60, 0), FVector::ZeroVector);
		FPairResult R;
		Exchange(A, B, U, FVector::ZeroVector, false, false, false, R);
		TestTrue(TEXT("touch: push / push"), R.HitA == EHit::Push && R.HitB == EHit::Push);
		TestTrue(TEXT("touch: vA 150"), A.State.Velocity.Equals(FVector(0, 150, 0), 1e-3));
		TestTrue(TEXT("touch: vB 150"), B.State.Velocity.Equals(FVector(0, 150, 0), 1e-3));
		TestTrue(TEXT("touch: controls unchanged"), A.State.Control == ERiderControl::Cruise && B.State.Control == ERiderControl::Cruise);
	}
	{
		FRiderSim A, B;
		MakeRider(A, World, FVector(0, 0, 0), FVector(0, 700, 0));
		MakeRider(B, World, FVector(0, 60, 0), FVector::ZeroVector);
		A.State.BoostMeter = 0.5;
		B.State.BoostStrength = 1.0;
		FPairResult R;
		Exchange(A, B, U, FVector::ZeroVector, false, false, false, R);
		Report(this, FString::Printf(TEXT("700 vs boosting: JA %.6f JB %.6f vA %.4f (model -629.99996 / 70.000035, vA 200)"), R.JA, R.JB, A.State.Velocity.Y));
		TestTrue(TEXT("JA -629.99996"), FMath::IsNearlyEqual(R.JA, -629.9999649999826, 1e-3));
		TestTrue(TEXT("JB 70.000035"), FMath::IsNearlyEqual(R.JB, 70.00003500001752, 1e-3));
		TestTrue(TEXT("A bumps"), R.HitA == EHit::Bump && A.State.Control == ERiderControl::Bump);
		TestTrue(TEXT("bump boost -0.02 x gain"), FMath::IsNearlyEqual(A.State.BoostMeter, 0.5 - 0.02 * (0.9787758 + 0.35966223 * 128.0 / 255.0), 1e-6));
		TestTrue(TEXT("vA 200"), FMath::IsNearlyEqual(A.State.Velocity.Y, 200.0, 1e-3));
		TestTrue(TEXT("B pushed"), R.HitB == EHit::Push);
	}
	{
		FRiderSim A, B;
		MakeRider(A, World, FVector(0, 0, 0), FVector(0, 300, 0));
		MakeRider(B, World, FVector(0, 60, 0), FVector::ZeroVector);
		B.State.Motion = ERiderMotion::RailSlide;
		FPairResult R;
		Exchange(A, B, U, FVector::ZeroVector, false, false, false, R);
		TestTrue(TEXT("rail: B knocked down at |J| 150"), R.HitA == EHit::Push && R.HitB == EHit::Knockdown);
	}
	{
		FRiderSim T;
		MakeRider(T, World, FVector(0, 0, 0), FVector::ZeroVector);
		TestTrue(TEXT("mode 0: 600 knocks down"), ApplyRiderHit(T, 600.0, U, FVector::ZeroVector, true, false) == EHit::Knockdown);
		FRiderSim T2;
		MakeRider(T2, World, FVector(0, 0, 0), FVector::ZeroVector);
		TestTrue(TEXT("normal: 600 bumps"), ApplyRiderHit(T2, 600.0, U, FVector::ZeroVector, false, false) == EHit::Bump);
		TestTrue(TEXT("feedback virtual (slot 13, 0x124BA8): 0.0012 Jc with Jc = clamp(600, +-500)"), T2.State.PadFeedback.Num() == 1
			&& !T2.State.PadFeedback[0].bImpact && FMath::IsNearlyEqual(T2.State.PadFeedback[0].Amount, double(500.f * 0.0012f), 1e-9));
		FRiderSim L;
		MakeRider(L, World, FVector(0, 0, 0), FVector::ZeroVector);
		TestTrue(TEXT("launch: |J| > 1000, grounded, heading ok, along > 0"),
			ApplyRiderHit(L, 1200.0, FVector(0, 0.6, 0.8), FVector::ZeroVector, false, true) == EHit::Launch);
		TestTrue(TEXT("launch -> Air + NaturalAir, flag 1"), L.State.Motion == ERiderMotion::Air && L.State.Control == ERiderControl::NaturalAir
			&& L.State.LaunchCharge == 1.0);
		FRiderSim Air;
		MakeRider(Air, World, FVector(0, 0, 0), FVector::ZeroVector);
		Air.State.Motion = ERiderMotion::Air;
		TestTrue(TEXT("airborne pushed away from the ground: |J| treated as 0"),
			ApplyRiderHit(Air, 900.0, FVector(0, 0.6, 0.8), FVector::ZeroVector, false, false) == EHit::Push);
		TestTrue(TEXT("airborne: u not made tangent"), Air.State.Velocity.Equals(FVector(0, 300, 400), 1e-3));
		FRiderSim W;
		MakeRider(W, World, FVector(0, 0, 0), FVector::ZeroVector);
		W.State.Motion = ERiderMotion::WipeOut;
		TestTrue(TEXT("wiping out: push only"), ApplyRiderHit(W, 900.0, U, FVector::ZeroVector, false, false) == EHit::Push);
		FRiderSim G;
		MakeRider(G, World, FVector(0, 0, 0), FVector::ZeroVector);
		ApplyRiderHit(G, 100.0, FVector(0, 0.6, 0.8), FVector::ZeroVector, false, false);
		TestTrue(TEXT("grounded: u tangent to the ground"), G.State.Velocity.Equals(FVector(0, 100, 0), 1e-3));
	}
	{
		FRiderSim A, B;
		MakeRider(A, World, FVector(0, 0, 0), FVector::ZeroVector);
		MakeRider(B, World, FVector(0, 100, 0), FVector::ZeroVector);
		FRiderSphereSet Sa;
		if (!RiderShape(A, Sa))
		{
			SSXTest::Skip(*this, TEXT("sphere part skipped: no bind-pose bones (SimData/riders.bin)"));
			return true;
		}
		const FVector Pa = A.State.Position, Pb = B.State.Position;
		const FPairResult R = BodyContact(A, B, false, false, false, false);
		Report(this, FString::Printf(TEXT("spheres 100 cm nose to tail: contact %d, |d| %.3f cm"), R.bContact, R.D.Size()));
		TestTrue(TEXT("riders 100 cm apart along the board axis touch"), R.bContact);
		TestTrue(TEXT("A moves -0.55 d"), (A.State.Position - (Pa - R.D)).IsNearlyZero(1e-3));
		TestTrue(TEXT("B moves +0.55 d"), (B.State.Position - (Pb + R.D)).IsNearlyZero(1e-3));
		TestFalse(TEXT("no impulse while the pair stamp is younger than 3 frames"), R.bImpulse);

		FRiderSim C, D;
		MakeRider(C, World, FVector(0, 0, 0), FVector::ZeroVector);
		MakeRider(D, World, FVector(0, 100, 0), FVector::ZeroVector);
		D.State.FinishResult = 100;
		D.SetControl(ERiderControl::FinishLine);
		D.State.bFinishLineDone = true;   // 0x1065C8
		const FVector Pc = C.State.Position, Pd = D.State.Position;
		const FPairResult R2 = BodyContact(C, D, true, false, false, false);
		TestTrue(TEXT("immovable: the other moves the full d"), R2.bContact && (C.State.Position - (Pc - R2.D)).IsNearlyZero(1e-3)
			&& D.State.Position.Equals(Pd));
		TestTrue(TEXT("immovable: no hit on it"), R2.HitB == EHit::None);

		FRiderSim E, F;
		MakeRider(E, World, FVector(0, 0, 0), FVector::ZeroVector);
		MakeRider(F, World, FVector(400, 0, 0), FVector::ZeroVector);
		TestFalse(TEXT("4 m apart: no contact"), BodyContact(E, F, true, false, false, false).bContact);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCollisionShoveTest, "SSX.Collision.Shove", CollisionTestFlags)

bool FSSXCollisionShoveTest::RunTest(const FString& Parameters)
{
	using namespace SSXRiderHit;
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);

	{
		FRiderSim A;
		MakeRider(A, World, FVector(0, 0, 0), FVector::ZeroVector);
		TestTrue(TEXT("side 3 starts a shove"), ShoveInput(A, 3, 0));
		TestEqual(TEXT("regular, right (toe side): R_PUSHTS 0x205"), A.State.ShoveAnim, SSXAnim::ShovePushTS);
		TestTrue(TEXT("axis = body right"), A.State.ShoveAxis.Equals(FVector(1, 0, 0), 1e-9));
		TestFalse(TEXT("hit window closed at the Play (opens after 11 updates, bx R_PUSHTS)"), A.State.bShoveActive);
		{
			int32 Open = -1, Close = -1;
			ShoveWindow(SSXAnim::ShovePushTS, 0, Open, Close);
			TestTrue(TEXT("bx R_PUSHTS window 11-29"), Open == 11 && Close == 29);
		}
		int32 Expect = SSXAnim::OneShotFrames(SSXAnim::DefaultOneShotFrames(SSXAnim::ShovePushTS, 0));
		if (const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared())
		{
			bool bAny = false;
			for (int32 V = 0; V < 3; ++V)
			{
				const SSXAnim::FAflClip* C = Lib->Clip(Lib->Resolve(SSXAnim::ShovePushTS, 0, A.Profile.GetTrickCharacter(), V));
				bAny |= C && SSXAnim::OneShotFrames(C->NFrames) == A.State.ShoveFramesLeft;
			}
			TestTrue(TEXT("plays 2 (n - 1) frames of the drawn variant"), bAny);
			Expect = A.State.ShoveFramesLeft;
		}
		TestEqual(TEXT("plays 2 (n - 1) frames"), A.State.ShoveFramesLeft, Expect);
		{
			SSXAnim::FAflClip C;
			C.Markers[0] = 7;
			C.Markers[1] = 14;
			int32 Open = -1, Close = -1;
			ShoveWindowOfClip(C, Open, Close);
			TestTrue(TEXT("window of markers (7, 14) = 15..29"), Open == 15 && Close == 29);
			C.Markers[1] = -1;
			ShoveWindowOfClip(C, Open, Close);
			TestTrue(TEXT("no flag 2: the window lasts to the clip end"), Close == INDEX_NONE);
		}
		TestFalse(TEXT("no new shove while one plays"), ShoveInput(A, 1, 0));
		for (int32 F = 0; F < Expect; ++F) { TickShove(A); }
		TestEqual(TEXT("done after its length"), A.State.ShoveAnim, 0);
		TestTrue(TEXT("side 1 (left, heel side): R_ELBOWHS 0x206"), ShoveInput(A, 1, 0) && A.State.ShoveAnim == SSXAnim::ShoveElbowHS);
		A.State.ShoveAnim = 0;
		TestTrue(TEXT("fwd +1 (stick up): R_COMBATFWD 0x207 along the nose"), ShoveInput(A, 0, 1)
			&& A.State.ShoveAnim == SSXAnim::ShoveCombatFwd && A.State.ShoveAxis.Equals(FVector(0, 1, 0), 1e-9));
		A.State.ShoveAnim = 0;
		TestTrue(TEXT("fwd -1 (stick down): R_COMBATBWD 0x208 along the tail"), ShoveInput(A, 0, -1)
			&& A.State.ShoveAnim == SSXAnim::ShoveCombatBwd && A.State.ShoveAxis.Equals(FVector(0, -1, 0), 1e-9));
		A.State.ShoveAnim = 0;
		TestTrue(TEXT("diagonal up-right (a = -45 deg): side clip 0x205 (regular), axis 45 deg"), ShoveInput(A, -1, 1)
			&& A.State.ShoveAnim == SSXAnim::ShovePushTS && FMath::IsNearlyEqual(A.State.ShoveAngle, -UE_DOUBLE_PI / 4.0, 1e-9));
		{
			// pad -> word -> decode: right stick up gives ShoveFwd +1 (the analog rule value = -raw, no negate)
			FSSXPadFrame P;
			P.RightY = -1.f;   // PS2 byte axis: up = -1
			TestEqual(TEXT("right stick up: ShoveFwd axis +1"), FPadMapper::Analog(P, FPadMapper::AShoveFwd, 0), 1.0);
		}
		FRiderSim G;
		MakeRider(G, World, FVector(0, 0, 0), FVector::ZeroVector);
		G.State.bStanceGoofy = true;
		TestTrue(TEXT("goofy, right = heel side: 0x206"), ShoveInput(G, 3, 0) && G.State.ShoveAnim == SSXAnim::ShoveElbowHS);
	}
	// 0x127918
	{
		FRiderSim A;
		MakeRider(A, World, FVector(0, 0, 0), FVector(0, 500, 0));
		FRiderInputWord W;
		W.ShoveSide = -1;   // decoded raw 3 (right)
		A.Step(W);
		TestEqual(TEXT("sim step: shove started from the ground word"), A.State.ShoveCount, 1);
		const SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(A);
		TestTrue(TEXT("overlay requested for the pose"), Ch && Ch->PlayRequests.Contains(SSXAnim::ShovePushTS));
	}
	// geometry and impulse (model: in range, J 99.6825 -> push, mode 0 J 230.0648; 200 cm and 60 deg out of range)
	{
		FRiderSim A, B;
		MakeRider(A, World, FVector(0, 0, 0), FVector::ZeroVector);
		MakeRider(B, World, FVector(100, 0, 0), FVector::ZeroVector);
		ShoveInput(A, 3, 0);
		TestTrue(TEXT("100 cm to the right: in range"), ShoveInRange(A.State, B.State));
		FVector U;
		const double J = ShoveImpulse(A, B, false, U);
		const double J0 = ShoveImpulse(A, B, true, U);
		Report(this, FString::Printf(TEXT("shove at rest: J %.6f (notes 99.682884), mode 0 %.6f (notes 230.059685)"), J, J0));
		TestTrue(TEXT("J 99.6829 (original constants)"), FMath::IsNearlyEqual(J, 99.68288424722905, 1e-3));
		TestTrue(TEXT("mode 0 J 230.0597"), FMath::IsNearlyEqual(J0, 230.0596851322502, 1e-3));
		TestTrue(TEXT("hit -> push"), ApplyRiderHit(B, J, U, B.State.Position, false, false) == EHit::Push);
		TestTrue(TEXT("vB along u"), B.State.Velocity.Equals(FVector(J, 0, 0), 1e-3));
		B.State.Position = FVector(200, 0, 0);
		TestFalse(TEXT("200 cm: out of range"), ShoveInRange(A.State, B.State));
		B.State.Position = FVector(50, 86.6, 0);
		TestFalse(TEXT("60 deg off the axis: out of range"), ShoveInRange(A.State, B.State));
		B.State.Position = FVector(-80, 0, 0);
		TestFalse(TEXT("behind the axis: out of range"), ShoveInRange(A.State, B.State));
	}
	{
		FRiderSim A, B;
		MakeRider(A, World, FVector(0, 0, 0), FVector(0, 1000, 0));
		MakeRider(B, World, FVector(0, 80, 0), FVector(0, 1000, 0));
		ShoveInput(A, 0, 1);   // forward (right stick up)
		TArray<double> Js;
		EHit Last = EHit::None;
		int32 Stamp = -100;
		for (int32 F = 0; F < 60 && A.State.ShoveAnim != 0; ++F)
		{
			const FRiderInputWord W;
			A.Step(W);
			B.Step(W);
			if (F - Stamp > ShovePairGap)
			{
				double J = 0.0;
				const EHit H = ShoveHit(A, B, false, false, &J);
				if (H != EHit::None)
				{
					Stamp = F;
					Js.Add(J);
					Last = H;
					if (H == EHit::Knockdown) { break; }
				}
			}
		}
		FString L;
		for (double J : Js) { L += FString::Printf(TEXT(" %.1f"), J); }
		Report(this, FString::Printf(TEXT("forward shove, both 1000 cm/s, 80 cm apart: %d hits, J:%s, last %d"), Js.Num(), *L, int32(Last)));
		TestTrue(TEXT("one shove delivers more than one hit"), Js.Num() > 1);
		TestTrue(TEXT("J grows from hit to hit"), Js.Num() > 1 && Js[1] > Js[0]);
		TestTrue(TEXT("repeat hits knock the target down (6.6 x relative velocity)"), Last == EHit::Knockdown);
	}
	{
		FAIState St;
		SSXRaceAI::ResetAI(St, 1.0, 1.0);
		FAIRiderView V;
		V.Index = 0;
		V.Control = int32(ERiderControl::Cruise);
		V.Motion = int32(ERiderMotion::Ground);
		V.Yaw = 0.0;
		V.RivalDist[1] = 150.0;
		V.RivalBearing[1] = -UE_DOUBLE_HALF_PI;
		V.Grudge.bActive = true;
		V.Grudge.Rival = 1;
		V.Grudge.Kind = 1;
		TArray<FAIOtherRider> Others;
		Others.SetNum(2);
		Others[0].Index = 0;
		Others[1].Index = 1;
		FAIRaceView RV;
		RV.Frame = 12;
		RV.Riders = Others;
		FRaceRandom Rng;
		auto Side = [&](const FAIRiderView& View, int32 Frame)
		{
			RV.Frame = Frame;
			FAIState S2 = St;
			return FPadMapper::DecodeWord(ERiderControl::Cruise, SSXRaceAI::AIWord(S2, View, RV, Rng)).ShoveSide;
		};
		TestEqual(TEXT("AI: grudge rival alongside -> side 3 (decodes to -1)"), Side(V, 12), -1);
		TestEqual(TEXT("AI: not on frames that are not a multiple of 6"), Side(V, 13), 0);
		FAIRiderView Blocked = V;
		Blocked.Channel1Anim = SSXAnim::CategoryShove;
		TestEqual(TEXT("AI: blocked while channel 1 plays a shove"), Side(Blocked, 12), 0);
		FAIRiderView Far = V;
		Far.RivalDist[1] = 250.0;
		TestEqual(TEXT("AI: 250 cm is too far"), Side(Far, 12), 0);
		FAIRiderView Kind0 = V;
		Kind0.Grudge.Kind = 0;
		TestEqual(TEXT("AI: grudge kind 0 does not shove"), Side(Kind0, 12), 0);
		FAIRiderView Left = V;
		Left.RivalBearing[1] = UE_DOUBLE_HALF_PI;
		TestEqual(TEXT("AI: on the left -> side 1"), Side(Left, 12), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCollisionShoveStickTest, "SSX.Collision.ShoveStick", CollisionTestFlags)

bool FSSXCollisionShoveStickTest::RunTest(const FString& Parameters)
{
	// 0x179940, 0x127918, 0x123FC8
	using namespace SSXRiderHit;
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	for (int32 Goofy = 0; Goofy < 2; ++Goofy)
	{
		for (int32 Dir = 0; Dir < 2; ++Dir)
		{
			const bool bUp = Dir == 0;
			const FString Tag = FString::Printf(TEXT("%s, stick %s"), Goofy ? TEXT("goofy") : TEXT("regular"), bUp ? TEXT("up") : TEXT("down"));
			FPadMapper Mapper;
			const FSSXPadFrame Pad = SSXInput::MakePad(0, FVector2D::ZeroVector, FVector2D(0.0, bUp ? 1.0 : -1.0));
			const FPadActions Act = Mapper.Step(Pad);
			const FRiderInputWord W = FPadMapper::DecodeWord(ERiderControl::Cruise, FPadMapper::EncodeWord(ERiderControl::Cruise, Act));
			TestEqual(Tag + TEXT(": word ShoveFwd"), W.ShoveFwd, bUp ? 1 : -1);
			TestEqual(Tag + TEXT(": word ShoveSide"), W.ShoveSide, 0);

			FRiderSim A, Ahead, Behind;
			MakeRider(A, World, FVector(0, 0, 0), FVector::ZeroVector);
			MakeRider(Ahead, World, FVector(0, 120, 0), FVector::ZeroVector);
			MakeRider(Behind, World, FVector(0, -120, 0), FVector::ZeroVector);
			A.State.bStanceGoofy = Goofy != 0;
			TestTrue(Tag + TEXT(": shove starts"), ShoveInput(A, W.ShoveSide, W.ShoveFwd));
			TestEqual(Tag + TEXT(": clip id"), A.State.ShoveAnim, bUp ? SSXAnim::ShoveCombatFwd : SSXAnim::ShoveCombatBwd);
			TestTrue(Tag + TEXT(": axis along the nose / tail"), A.State.ShoveAxis.Equals(FVector(0, bUp ? 1 : -1, 0), 1e-9));
			TestEqual(Tag + TEXT(": hits the rider ahead"), ShoveInRange(A.State, Ahead.State), bUp);
			TestEqual(Tag + TEXT(": hits the rider behind"), ShoveInRange(A.State, Behind.State), !bUp);
			if (Lib)
			{
				const SSXAnim::FAflClip* C = Lib->Clip(Lib->Resolve(A.State.ShoveAnim, 0, 0, 0));
				TestTrue(Tag + TEXT(": resolves to bxR_COMBAT") + (bUp ? TEXT("FWD1") : TEXT("BWD1")),
					C && C->Name.EndsWith(bUp ? TEXT("R_COMBATFWD1") : TEXT("R_COMBATBWD1")));
			}
		}
	}
	return true;
}

// 0x11D998, 0x1065C8
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCollisionImmovableTest, "SSX.Collision.Immovable", CollisionTestFlags)

bool FSSXCollisionImmovableTest::RunTest(const FString& Parameters)
{
	using namespace SSXRiderHit;
	FRiderSim Sim;
	Sim.State.FinishResult = 2;
	Sim.State.Control = ERiderControl::FinishLine;
	Sim.State.bFinishLineDone = false;
	TestFalse(TEXT("FinishLine before the done test: movable"), IsImmovable(Sim));
	Sim.State.bFinishLineDone = true;
	TestTrue(TEXT("FinishLine past the done test: immovable"), IsImmovable(Sim));
	TestTrue(TEXT("immovable mass 1e30"), PairMass(Sim) == double(1e30f));
	Sim.State.FinishResult = -1;
	TestFalse(TEXT("not finished: movable"), IsImmovable(Sim));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXCollisionRiderPairsTest, "SSX.Collision.RiderPairs", CollisionTestFlags)
bool FSSXCollisionRiderPairsTest::RunTest(const FString& Parameters)
{
	using SSXRiderHit::FRiderPairs;
	FRiderPairs P;
	P.Reset();
	constexpr int32 N = 6;
	for (int32 I = 0; I < N; ++I) { TestEqual(TEXT("proxy id"), P.AddProxy(), I); }
	FVector3f Mn[N], Mx[N];
	FRandomStream R(1234);
	int32 Mismatch = 0, WalkMismatch = 0, Pairs = 0;
	for (int32 Step = 0; Step < 4000; ++Step)
	{
		const int32 I = R.RandRange(0, N - 1);
		if (Step < N || R.FRand() < 0.5f)
		{
			const FVector3f C(R.FRandRange(-300, 300), R.FRandRange(-300, 300), R.FRandRange(-100, 100));
			const FVector3f H(R.FRandRange(20, 90), R.FRandRange(20, 90), R.FRandRange(40, 100));
			Mn[I] = C - H; Mx[I] = C + H;
			P.SetBox(I, Mn[I], Mx[I]);
		}
		else
		{
			const FVector3f D(R.FRandRange(-40, 40), R.FRandRange(-40, 40), R.FRandRange(-10, 10));
			Mn[I] += D; Mx[I] += D;
			P.Move(I, D);
		}
		for (int32 A = 0; A < N; ++A)
		{
			TArray<int32> Seen;
			P.Walk(A, [&Seen](int32 B) { Seen.Add(B); });
			for (int32 B = 0; B < N; ++B)
			{
				if (A == B) continue;
				const bool bOverlap = Mn[A].X < Mx[B].X && Mn[B].X < Mx[A].X && Mn[A].Y < Mx[B].Y && Mn[B].Y < Mx[A].Y && Mn[A].Z < Mx[B].Z && Mn[B].Z < Mx[A].Z;
				Mismatch += P.IsPaired(A, B) != bOverlap;
				WalkMismatch += Seen.Contains(B) != bOverlap;
				Pairs += bOverlap;
			}
		}
	}
	Report(this, FString::Printf(TEXT("random boxes: %d paired samples, %d pair-bit / %d walk mismatches"), Pairs, Mismatch, WalkMismatch));
	TestTrue(TEXT("paired exactly while the AABBs overlap"), Mismatch == 0 && WalkMismatch == 0 && Pairs > 1000);

	FRiderPairs Q;
	Q.Reset();
	for (int32 I = 0; I < 3; ++I) { Q.AddProxy(); }
	Q.SetBox(0, FVector3f(0, 0, 0), FVector3f(10, 10, 10));
	Q.SetBox(1, FVector3f(100, 0, 0), FVector3f(110, 10, 10));
	Q.SetBox(2, FVector3f(5, 5, 5), FVector3f(15, 15, 15));
	Q.Move(1, FVector3f(-95, 0, 0));
	TArray<int32> Order;
	Q.Walk(0, [&Order](int32 B) { Order.Add(B); });
	TestTrue(TEXT("walk order = pair creation order (2 then 1)"), Order.Num() == 2 && Order[0] == 2 && Order[1] == 1);
	Q.Move(2, FVector3f(100, 0, 0));
	Q.Move(2, FVector3f(-100, 0, 0));
	Order.Reset();
	Q.Walk(0, [&Order](int32 B) { Order.Add(B); });
	TestTrue(TEXT("a re-made pair goes to the tail (1 then 2)"), Order.Num() == 2 && Order[0] == 1 && Order[1] == 2);
	Q.SetBox(1, FVector3f(200, 0, 0), FVector3f(210, 10, 10));
	Q.SetBox(1, FVector3f(10, 0, 0), FVector3f(20, 10, 10));
	TestFalse(TEXT("touching from apart: no pair"), Q.IsPaired(0, 1));
	Q.SetBox(1, FVector3f(5, 0, 0), FVector3f(20, 10, 10));
	Q.SetBox(1, FVector3f(10, 0, 0), FVector3f(20, 10, 10));
	TestTrue(TEXT("touching from an overlap: still a pair"), Q.IsPaired(0, 1));
	return !HasAnyErrors();
}

#endif
