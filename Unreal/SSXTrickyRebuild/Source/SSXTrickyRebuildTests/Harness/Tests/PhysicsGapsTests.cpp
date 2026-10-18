#include "Characters/SSXCharacterLibrary.h"
#include "Harness/RiderSetup.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Air/TrickScore.h"
#include "Sim/Ground/WallCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderPlaceholderStates.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/SurfaceTable.h"
#include "Sim/WipeOut/TumbleBody.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags GapTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	bool NeedTable(FAutomationTestBase* T)
	{
		if (!SSXRider::FRiderTable::Shared().IsLoaded())
		{
			T->AddError(TEXT("SimData/riders.bin missing: run tools/export_rider_profiles.py"));
			return false;
		}
		return true;
	}

	struct FProfileCase { int32 C = 0, K = 0; uint8 P0[16], Pk[16], A0[16], A240[16], Alt[16]; int32 ClassK = 0; };
	struct FProfileReference { TArray<FProfileCase> Cases; double Scale[12]; int32 Stance[12]; int32 Mass[12]; };

	bool LoadProfileReference(FProfileReference& Out)
	{
		FString Text;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("rider_profile_reference.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path)) { return false; }
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) { return false; }
		auto Bytes = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key, uint8* Dst)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = O->GetArrayField(Key);
			if (A.Num() != 16) { return false; }
			for (int32 i = 0; i < 16; ++i) { Dst[i] = (uint8)A[i]->AsNumber(); }
			return true;
		};
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cases")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FProfileCase C;
			C.C = (int32)O->GetNumberField(TEXT("rider"));
			C.K = (int32)O->GetNumberField(TEXT("board"));
			C.ClassK = (int32)O->GetNumberField(TEXT("class_k"));
			if (!Bytes(O, TEXT("p0"), C.P0) || !Bytes(O, TEXT("pk"), C.Pk) || !Bytes(O, TEXT("a0"), C.A0)
				|| !Bytes(O, TEXT("a240"), C.A240) || !Bytes(O, TEXT("alt"), C.Alt)) { return false; }
			Out.Cases.Add(C);
		}
		const TArray<TSharedPtr<FJsonValue>>& S = Root->GetArrayField(TEXT("scale"));
		const TArray<TSharedPtr<FJsonValue>>& St = Root->GetArrayField(TEXT("stance"));
		const TArray<TSharedPtr<FJsonValue>>& M = Root->GetArrayField(TEXT("mass"));
		if (Out.Cases.Num() != 12 || S.Num() != 12 || St.Num() != 12 || M.Num() != 12) { return false; }
		for (int32 i = 0; i < 12; ++i)
		{
			Out.Scale[i] = S[i]->AsNumber();
			Out.Stance[i] = (int32)St[i]->AsNumber();
			Out.Mass[i] = (int32)M[i]->AsNumber();
		}
		return true;
	}

	bool SameBytes(const uint8* A, const uint8* B) { return FMemory::Memcmp(A, B, 16) == 0; }

	class FScriptedWallWorld : public IRiderWorld
	{
	public:
		FRiderWorldHit WallHit;
		bool bWall = false;
		int32 Queries = 0;
		FSurfaceTable Surfaces;
		virtual bool ProbeGround(const FGroundProbeInput& In, FGroundProbeResult& Out) const override
		{
			Out = FGroundProbeResult();
			Out.bFound = true;
			Out.Surface = 1;
			Out.GroundNormal = FVector::UpVector;
			Out.ContactPoint = FVector(In.Position.X, In.Position.Y, 0.0);
			Out.Height = In.Position.Z;
			Out.ContactForward = FVector(In.Forward.X, In.Forward.Y, 0.0).GetSafeNormal();
			Out.ContactLeft = FVector::CrossProduct(FVector::UpVector, Out.ContactForward);
			Out.ContactKind = 0;
			return true;
		}
		virtual bool LineQuery(const FVector& A, const FVector& B, ERiderLineQuery Type, double TRef, FRiderWorldHit& Out) const override
		{
			if ((A.Z > 0.0) == (B.Z > 0.0)) { return false; }
			const double T = A.Z / (A.Z - B.Z);
			Out = FRiderWorldHit();
			Out.Point = A + T * (B - A);
			Out.Normal = FVector::UpVector;
			Out.Surface = 1;
			Out.T = T;
			return true;
		}
		virtual bool QueryWalls(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir, FRiderWorldHit& Out) const override
		{
			++const_cast<FScriptedWallWorld*>(this)->Queries;
			if (!bWall) { return false; }
			Out = WallHit;
			return true;
		}
		virtual const FSurfaceParams& SurfaceParams(int32 SurfaceId) const override { return Surfaces.Get(SurfaceId); }
		virtual double FloorZ() const override { return -1e6; }
	};

	void SetupBrodi(FRiderSim& Sim)
	{
		SSXRider::FPlayerOptions O;
		SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(SSXRider::FRiderTable::Shared(), 10, O), Sim.Profile);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRiderProfilesTest, "SSX.Rider.Profiles", GapTestFlags)

bool FSSXRiderProfilesTest::RunTest(const FString& Parameters)
{
	if (!NeedTable(this)) { return false; }
	FProfileReference Ref;
	if (!LoadProfileReference(Ref))
	{
		AddError(TEXT("SimData/rider_profile_reference.json missing or malformed"));
		return false;
	}
	const SSXRider::FRiderTable& T = SSXRider::FRiderTable::Shared();
	int32 Bad = 0;
	for (const FProfileCase& Case : Ref.Cases)
	{
		using namespace SSXRider;
		FPlayerOptions O;
		const FBuiltProfile P0 = BuildPlayerProfile(T, Case.C, O);
		O.Board = Case.K;
		O.bHaveCareer = true;
		O.Career[0] = 85; O.Career[1] = 85; O.Career[2] = 90; O.Career[3] = 90;
		const FBuiltProfile Pk = BuildPlayerProfile(T, Case.C, O);
		FPlayerOptions Alt;
		Alt.bCheatAlt = true;
		const FBuiltProfile Pa = BuildPlayerProfile(T, Case.C, Alt);
		const FBuiltProfile A0 = BuildComputerProfile(T, Case.C, true, 0, Case.K);
		const FBuiltProfile A240 = BuildComputerProfile(T, Case.C, true, 240, Case.K);
		const FBuiltProfile ANull = BuildComputerProfile(T, Case.C, false, 0, 0);
		const bool bOk = SameBytes(P0.Stats, Case.P0) && SameBytes(Pk.Stats, Case.Pk) && SameBytes(Pa.Stats, Case.Alt)
			&& SameBytes(A0.Stats, Case.A0) && SameBytes(A240.Stats, Case.A240) && SameBytes(ANull.Stats, Case.A0)
			&& Pk.BoardClass == Case.ClassK && A0.BoardClass == Case.ClassK
			&& FMath::IsNearlyEqual(P0.Scale, Ref.Scale[Case.C], 1e-6) && P0.Stance == Ref.Stance[Case.C]
			&& A0.Stance == Ref.Stance[Case.C] && P0.TrickSet == Case.C && P0.MassByte == Ref.Mass[Case.C]
			&& !P0.bComputer && A0.bComputer;
		if (!bOk && ++Bad <= 12)
		{
			AddError(FString::Printf(TEXT("rider %d (%s): profile differs from models/rider_profile.py"), Case.C, *T.Rider(Case.C).Name));
		}
	}
	{
		SSXRider::FPlayerOptions O;
		O.bHaveCareer = true;
		O.Career[0] = 85; O.Career[1] = 85; O.Career[2] = 90; O.Career[3] = 90;
		const SSXRider::FBuiltProfile P = SSXRider::BuildPlayerProfile(T, 4, O);
		TestEqual(TEXT("Moby live board class (Plasmatic, class 0)"), P.BoardClass, 0);
	}
	// 0x3352B8
	{
		SSXRider::FPlayerOptions O;
		O.bMixMasterMike = true;
		const SSXRider::FBuiltProfile M = SSXRider::BuildPlayerProfile(T, 0, O);
		TestTrue(TEXT("MMM outside mode 6"), M.bMixMasterMike && M.Byte64 == 12 && M.Byte80 == 0 && M.Char3C == 1
			&& FMath::IsNearlyEqual(M.Scale, 0.8, 1e-9));
		O.bMode6 = true;
		const SSXRider::FBuiltProfile M6 = SSXRider::BuildPlayerProfile(T, 0, O);
		TestTrue(TEXT("MMM in mode 6 keeps +0x3C and the scale"), M6.Byte64 == 12 && M6.Char3C == 0 && FMath::IsNearlyEqual(M6.Scale, 0.85, 1e-6));
	}
	// 0x16A9D8, 0x3352AC
	{
		using namespace SSXRider;
		auto Build = [&T](int32 C, bool bAlt, bool bMmm, bool bMallora, bool bMode6)
		{
			FPlayerOptions O;
			O.bCheatAlt = bAlt; O.bMixMasterMike = bMmm; O.bMallora = bMallora; O.bMode6 = bMode6;
			return BuildPlayerProfile(T, C, O);
		};
		const FBuiltProfile EliseAlt = Build(MalloraCharacter, true, false, false, false);
		const FBuiltProfile ElisePlain = Build(MalloraCharacter, false, false, false, false);
		const FBuiltProfile Mal = Build(MalloraCharacter, false, false, true, false);
		const FBuiltProfile Mal6 = Build(MalloraCharacter, false, false, true, true);
		const FBuiltProfile MalMoby = Build(4, false, false, true, false);
		TestTrue(TEXT("Mallora: Elise only, any mode"), Mal.bMallora && Mal6.bMallora && !MalMoby.bMallora && !ElisePlain.bMallora);
		TestTrue(TEXT("Mallora Elise: alt stats outside mode 6"), SameBytes(Mal.Stats, EliseAlt.Stats));
		TestTrue(TEXT("Mallora Elise in mode 6: normal stats"), SameBytes(Mal6.Stats, ElisePlain.Stats));
		TestTrue(TEXT("Mallora on another rider: no effect"), SameBytes(MalMoby.Stats, Build(4, false, false, false, false).Stats));
		TestTrue(TEXT("MMM: alt stats outside mode 6"), SameBytes(Build(0, false, true, false, false).Stats, Build(0, true, false, false, false).Stats));
		TestTrue(TEXT("MMM in mode 6: normal stats"), SameBytes(Build(0, false, true, false, true).Stats, Build(0, false, false, false, false).Stats));
		TestEqual(TEXT("Mallora suit"), FString(SSXCharacters::MalloraTextureName(TEXT("suit"))), FString(TEXT("mallora_suit")));
		TestEqual(TEXT("Mallora boot"), FString(SSXCharacters::MalloraTextureName(TEXT("boot"))), FString(TEXT("elise1_boot")));
		TestEqual(TEXT("Mallora board"), FString(SSXCharacters::MalloraTextureName(TEXT("bord"))), FString(TEXT("mallora_bord")));
	}
	FRiderProfile SimP;
	TestTrue(TEXT("new-save player profile applied"), SSXRider::ApplyNewSavePlayerProfile(SimP, 10, 2));
	TestTrue(TEXT("Brodi board 0 (Now and Zen, class 2), stat 0x0E = 0x5B"), SimP.BoardClass == 2 && SimP.StatBytes[0] == 0x5B
		&& FMath::IsNearlyEqual(SimP.SizeScale, 0.98, 1e-6) && SimP.MassByte == 93 && SimP.bProfileLoaded);
	AddInfo(FString::Printf(TEXT("12 riders x 6 builds checked, %d mismatches"), Bad));
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrickValuesTest, "SSX.Air.TrickValues", GapTestFlags)

bool FSSXTrickValuesTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	auto Near = [this](const TCHAR* What, double A, double B) { TestTrue(FString::Printf(TEXT("%s: %.12f vs %.12f"), What, A, B), FMath::Abs(A - B) < 1e-9); };
	FTrickScore S;
	S.GrabReachesHold(0x27A);
	for (int32 I = 0; I < 60; ++I) { S.TickHold(); }
	S.FinishTrick(0x27A, 0.0);
	Near(TEXT("grab 0x27A held 60 frames"), S.Value, 0.132505193600);
	TestTrue(TEXT("grab name 3, grab count 1, hold -1"), S.GrabNames.Num() == 1 && S.GrabNames[0] == 3 && S.GrabCount == 1 && S.HoldFrames == -1);
	S.GrabReachesHold(0x282);
	for (int32 I = 0; I < 30; ++I) { S.TickHold(); }
	S.FinishTrick(0x282, 0.0);
	Near(TEXT("second grab (k = 1) 0x282, 30 frames"), S.Value, 0.240002134500);
	// 0x156148
	Near(TEXT("hold seconds total"), S.HoldSecondsTotal, double(float(60) * 0.016666668f + float(30) * 0.016666668f));
	Near(TEXT("hold seconds best"), S.HoldSecondsBest, double(float(60) * 0.016666668f));
	S.GrabReachesHold(0x278);
	for (int32 I = 0; I < 10; ++I) { S.TickHold(); }
	S.TrickUpgraded(0x2AC);
	for (int32 I = 0; I < 70; ++I) { S.TickHold(); }
	S.FinishTrick(0x2AC, 70.0 / 30.0);
	Near(TEXT("uber 0x2AC payout at flag 1 (70 frames)"), S.Value, 3.217737902900);
	TestTrue(TEXT("names 3 12 53, grabs 2, ubers 1"), S.GrabNames.Num() == 3 && S.GrabNames[2] == 53 && S.GrabCount == 2 && S.PendingUbers == 1);
	S.FinishTrick(0x27A, 0.0);
	Near(TEXT("finish without a counted hold adds nothing"), S.Value, 3.217737902900);

	FTrickScore T;
	T.GrabReachesHold(0x27E);
	for (int32 I = 0; I < 5; ++I) { T.TickHold(); }
	T.TrickUpgraded(0x295);
	for (int32 I = 0; I < 20; ++I) { T.TickHold(); }
	T.FinishTrick(0x295, 0.0);
	Near(TEXT("tweak 0x295 (rate 5)"), T.Value, 0.121254233150);
	TestTrue(TEXT("tweak name 30"), T.GrabNames.Num() == 1 && T.GrabNames[0] == 30);
	const FTrickRecord R = T.BuildRecord();
	T.History.Add(R);
	T.History.Add(R);
	const double Meter = T.Land(false, false, false);
	Near(TEXT("landing meter with 2 repeats (0.678655 V / 3)"), Meter, 0.027429910324);
	TestEqual(TEXT("landing points with 2 repeats"), T.LastPoints, 273);
	TestEqual(TEXT("repeat count"), T.LastRepeatCount, 2);

	{
		FTrickScore H;
		FTrickRecord A, B, C;
		A.bValid = B.bValid = C.bValid = true;
		A.W0 = 2u << 24; B.W0 = 4u << 24; C.W0 = 0x65u << 16;   // byte 2 'e' never counts
		const FTrickRecord* Seq[] = { &A, &A, &B, &A, &A, &A, &A, &C, &A };
		const int32 Expect[] = { 0, 1, 0, 2, 3, 4, 4, 0, 4 };
		for (int32 I = 0; I < 9; ++I)
		{
			TestEqual(FString::Printf(TEXT("repeat count %d"), I), H.RepeatCount(*Seq[I]), Expect[I]);
		}
	}
	TestTrue(TEXT("rates"), TrickHoldRate(0x278) == 2.0 && TrickHoldRate(0x27C) == 1.25 && TrickHoldRate(0x295) == 5.0
		&& TrickHoldRate(0x2CF) == 18.0 && TrickHoldRate(0x2D0) == 20.0 && TrickHoldRate(0x200) == 0.0);
	TestTrue(TEXT("names"), TrickNameIndex(0x291) == 5 && TrickNameIndex(0x2AB) == 31 && TrickNameIndex(0x2AD) == 54);
	{
		FTrickScore Rc;
		Rc.SpinDeg = 365.0;
		FTrickRecord X = Rc.BuildRecord();
		TestTrue(TEXT("record 360: BS 360"), X.bValid && !X.IsUnnameable() && ((X.W1 >> 27) & 7) == 2 && ((X.W0 >> 24) & 15) == 2);
		Rc.SpinDeg = -2160.0;
		X = Rc.BuildRecord();
		TestTrue(TEXT("record 2160 unnameable"), X.bValid && X.IsUnnameable() && X.W0 == 0x2Cu && X.W1 == 0u);
		TestEqual(TEXT("round 90 toward zero"), Round180Signed(90.0), 0);
		TestEqual(TEXT("round -90 toward zero"), Round180Signed(-90.0), 0);
	}
	{
		FTrickScore L;
		L.UpdateAngles(200.0, 0.0);
		const double V = L.Value;
		TestEqual(TEXT("late trick adds 0"), L.LateTrick(false), 0.0);
		TestTrue(TEXT("late trick clears the bests, keeps V, marks late"), L.BestSpin == 0 && L.Value == V && L.bSegmentLate && L.History.Num() == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXSphereSetTest, "SSX.Collision.SphereSet", GapTestFlags)

bool FSSXSphereSetTest::RunTest(const FString& Parameters)
{
	if (!NeedTable(this)) { return false; }
	FRiderProfile P;
	SSXRider::ApplyNewSavePlayerProfile(P, 10, 2);
	FRiderSphereSet S;
	const FVector Pos(100, 200, 300);
	TestTrue(TEXT("sphere set built"), SSXRider::BuildSphereSet(P, false, Pos, FQuat::Identity, SSXRider::MaskGroundWall, S));
	TestEqual(TEXT("18 spheres"), S.Count, 18);
	TestTrue(TEXT("bound radius 100 * scale"), FMath::IsNearlyEqual(S.BoundRadius, 98.0, 1e-4));
	TestTrue(TEXT("bound centre = midpoint of 8 / 9"), S.BoundCentre.Equals(0.5 * (S.Centres[8] + S.Centres[9]), 1e-9));
	bool bRadii = true;
	for (int32 I = 0; I < 18; ++I) { bRadii &= S.Radii[I] == SSXRider::SphereRadius[I]; }
	TestTrue(TEXT("fixed radii (25 20 15 15 15 15 10 10 25 25 20 20 30 30 20 20 20 20)"), bRadii);
	const FVector M = 0.5 * (S.Centres[12] + S.Centres[13]);
	const FVector D = (S.Centres[12] - S.Centres[13]).GetSafeNormal();
	TestTrue(TEXT("board spheres M +- 70 d, M +- 45 d"), S.Centres[14].Equals(M + 70.0 * D, 1e-6) && S.Centres[15].Equals(M - 70.0 * D, 1e-6)
		&& S.Centres[16].Equals(M + 45.0 * D, 1e-6) && S.Centres[17].Equals(M - 45.0 * D, 1e-6));
	TestTrue(TEXT("head above chest above hips (bind pose)"), S.Centres[1].Z > S.Centres[0].Z && S.Centres[0].Z > S.Centres[8].Z && S.Centres[8].Z > S.Centres[12].Z);
	TestTrue(TEXT("feet along the board nose axis (body forward)"), FMath::Abs(D.Y) > 0.9);
	FRiderSphereSet G;
	SSXRider::BuildSphereSet(P, true, Pos, FQuat::Identity, -1, G);
	TestTrue(TEXT("goofy turns the set by pi about up"), FMath::IsNearlyEqual(G.Centres[2].X - Pos.X, -(S.Centres[2].X - Pos.X), 1e-9));
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);
	FRiderSphereSet On;
	SSXRider::BuildSphereSet(P, false, FVector(0, 0, 0), FQuat::Identity, SSXRider::MaskTumble, On);
	FRiderWorldHit Hit;
	TestTrue(TEXT("tumble mask touches the ground"), World.QueryWalls(On, true, nullptr, Hit) && Hit.T > 0.0 && Hit.Normal.Z > 0.99);
	const FVector Up = FVector::UpVector;
	TestFalse(TEXT("ground filter rejects the floor"), World.QueryWalls(On, false, &Up, Hit));
	SSXRider::BuildSphereSet(P, false, FVector(0, 0, 0), FQuat::Identity, SSXRider::MaskGroundWall, On);
	TestFalse(TEXT("chest + head clear of the floor"), World.QueryWalls(On, false, nullptr, Hit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWallTestTest, "SSX.Collision.WallTest", GapTestFlags)

bool FSSXWallTestTest::RunTest(const FString& Parameters)
{
	if (!NeedTable(this)) { return false; }
	FScriptedWallWorld World;
	World.bWall = true;
	World.WallHit.Normal = FVector(0, -1, 0);
	World.WallHit.T = 5.0;
	World.WallHit.Point = FVector(0, 30, 130);
	World.WallHit.Kind = 0;
	World.WallHit.Surface = 1;
	auto Make = [&World](FRiderSim& Sim, double Speed)
	{
		SetupBrodi(Sim);
		Sim.SetWorld(&World);
		Sim.Reset();
		Sim.SetOrientationFromAxes(FVector(0, 1, 0), FVector::UpVector);
		Sim.State.Motion = ERiderMotion::Ground;
		Sim.State.Control = ERiderControl::Cruise;
		Sim.State.Velocity = FVector(0, Speed, 0);
	};
	{
		FRiderSim Sim;
		Make(Sim, 1000.0);
		SSXAnim::FRiderAnimChannel Ch;
		const bool bHit = SSXGround::WallCollision(Sim, SSXRider::MaskGroundWall, &Ch);
		const FRiderState& St = Sim.State;
		TestTrue(TEXT("wall hit"), bHit);
		TestTrue(TEXT("push-out 1.1 t m"), St.Position.Equals(FVector(0, -5.5, 0), 1e-9));
		TestTrue(TEXT("bounce max(0.5 a, 55.56): v.m = 500"), St.Velocity.Equals(FVector(0, -500, 0), 1e-9));
		const double Turn = FMath::RadiansToDegrees(FMath::Atan2(-St.BasisForward.X, St.BasisForward.Y));
		TestTrue(FString::Printf(TEXT("heading turned away by 20 deg (%.3f)"), Turn), FMath::IsNearlyEqual(FMath::Abs(Turn), 20.0, 1e-4));   // 0x128E90
		TestTrue(TEXT("stuck_a fed (+1) and +0x2F0 = m"), FMath::IsNearlyEqual(St.StuckAccumA, 1.0) && St.LastImpactNormal.Equals(FVector(0, -1, 0)));
		TestTrue(TEXT("bump: control Bump, front bump animation 0x245"), St.Control == ERiderControl::Bump && Ch.BumpAnim == 0x245);
		TestTrue(TEXT("still on the ground"), St.Motion == ERiderMotion::Ground);
		FRiderSim Slow;
		Make(Slow, 50.0);
		SSXGround::WallCollision(Slow, SSXRider::MaskGroundWall, &Ch);
		TestTrue(TEXT("minimum bounce 55.5556"), FMath::IsNearlyEqual(-Slow.State.Velocity.Y, 55.5556, 1e-9));
		FRiderSim Away;
		Make(Away, -300.0);
		TestFalse(TEXT("separating: no bounce"), SSXGround::WallCollision(Away, SSXRider::MaskGroundWall, &Ch));
		TestTrue(TEXT("separating: velocity kept"), Away.State.Velocity.Equals(FVector(0, -300, 0)));
	}
	{
		FRiderSim Sim;
		Make(Sim, 5000.0);
		SSXAnim::FRiderAnimChannel Ch;
		SSXGround::WallCollision(Sim, SSXRider::MaskGroundWall, &Ch);
		const FRiderState& St = Sim.State;
		TestTrue(TEXT("wall bounce wipe-out (T60)"), St.Motion == ERiderMotion::WipeOut && St.Control == ERiderControl::WipeOut);
		// 0x10DAD8
		const auto* WM = static_cast<SSXSimPlaceholders::FWipeOutMotion*>(Sim.GetMotionState(ERiderMotion::WipeOut));
		const double BodyVy = WM && WM->GetContext().Body.bValid ? WM->GetContext().Body.V.Y : St.Velocity.Y;
		TestTrue(FString::Printf(TEXT("entry impulse applied to the body after the bounce (body v.y %.1f)"), BodyVy), BodyVy > -2500.0 + 1.0);
		TestTrue(TEXT("+0x2F0 = -m (into the obstacle)"), St.LastImpactNormal.Equals(FVector(0, 1, 0), 1e-9));
	}
	{
		FRiderSim Sim;
		SetupBrodi(Sim);
		World.bWall = false;
		Sim.SetWorld(&World);
		Sim.Reset();
		Sim.SetOrientationFromAxes(FVector(0, 1, 0), FVector::UpVector);
		Sim.State.Position = FVector(0, 0, 0);
		Sim.State.Velocity = FVector(0, 1500, -800);
		FRiderImpact Impact;
		Impact.Point = FVector(0, 0, 0);
		Impact.Normal = FVector::UpVector;
		Impact.Speed = -800;
		Sim.StartWipeOut(Impact, TEXT("test landing bail"));
		TestTrue(TEXT("no entry impulse at a landing bail"), Sim.State.Velocity.Equals(FVector(0, 1500, -800), 1e-6));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAuto180Test, "SSX.Air.Auto180", GapTestFlags)

bool FSSXAuto180Test::RunTest(const FString& Parameters)
{
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);
	auto Run = [&World](int32 BoardClass, const FVector& Vel, int32& OutCount, bool& OutGoofy, FVector& OutFwd)
	{
		FRiderSim Sim;
		RegisterAirStates(Sim);
		Sim.Profile.Character = 10;
		Sim.Profile.BoardClass = BoardClass;
		Sim.SetWorld(&World);
		Sim.Reset();
		Sim.Spawn(FVector(0, 0, 400), FVector(0, 1, 0), Vel, 0.0, true);
		Sim.State.Velocity = Vel;
		Sim.SetMotion(ERiderMotion::Air, TEXT("test"));
		Sim.SetControl(ERiderControl::Spin, TEXT("test"));
		Sim.Step(FRiderInputWord());
		const SSXAir::FAirContext* Ctx = SSXAir::GetAirContext(Sim);
		OutCount = Ctx ? Ctx->Spin.Auto180Count : -1;
		OutGoofy = Sim.State.bStanceGoofy;
		OutFwd = SSXAir::GetRenderedOrientation(Sim.State).RotateVector(FVector(0, 1, 0));
	};
	int32 Count = 0;
	bool bGoofy = false;
	FVector Fwd;
	Run(0, FVector(0, -100, 800), Count, bGoofy, Fwd);
	TestEqual(TEXT("auto-180 fires (board class 0)"), Count, 1);
	TestTrue(TEXT("stance toggled"), bGoofy);
	TestTrue(FString::Printf(TEXT("rendered forward turned to -Y (%.3f)"), Fwd.Y), Fwd.Y < -0.9);
	Run(2, FVector(0, -100, 800), Count, bGoofy, Fwd);
	TestEqual(TEXT("no auto-180 on board class 2"), Count, 0);
	Run(0, FVector(0, 1500, 800), Count, bGoofy, Fwd);
	TestEqual(TEXT("no auto-180 when moving forward fast"), Count, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTumbleTest, "SSX.WipeOut.Tumble", GapTestFlags)

bool FSSXTumbleTest::RunTest(const FString& Parameters)
{
	using namespace SSXWipeOut;
	FVector C[18];
	for (int32 I = 0; I < 18; ++I) { C[I] = FVector(double((I * 7) % 41) - 20.0, double((I * 11) % 37) - 18.0, I * 10.0 - 50.0); }
	FMatrix InvI;
	const FVector Centre = MassProperties(C, InvI);
	TestTrue(TEXT("centroid vs models/wipeout.py"), Centre.Equals(FVector(-1.608695652173913, -1.5, 37.391304347826086), 1e-4));
	const double Ref[3][3] = { { 5.600483132233465e-06, 2.2025787670041667e-07, 6.854991269060324e-06 },
	                           { 2.2025787670041667e-07, 4.894762692537015e-06, 1.84177605446755e-06 },
	                           { 6.854991269060323e-06, 1.8417760544675497e-06, 6.418261871134107e-05 } };
	double MaxRel = 0.0;
	for (int32 R = 0; R < 3; ++R) for (int32 Col = 0; Col < 3; ++Col) { MaxRel = FMath::Max(MaxRel, FMath::Abs(InvI.M[R][Col] - Ref[R][Col]) / FMath::Abs(Ref[R][Col])); }
	TestTrue(FString::Printf(TEXT("I^-1 = 0.8 inverse(1.2 I) vs models/wipeout.py (rel %.2e, single precision)"), MaxRel), MaxRel < 1e-5);

	FTumbleBody B;
	B.bValid = true;
	B.InvI = InvI;
	const FVector V(300, -200, -500), W(1, -2, 0.5), R(10, -20, -60), N(0, 0.6, 0.8);
	FMatrix I3 = FMatrix::Identity;
	for (int32 Rr = 0; Rr < 3; ++Rr) for (int32 Col = 0; Col < 3; ++Col) { I3.M[Rr][Col] = InvI.M[Rr][Col]; }
	const FMatrix Inertia = I3.Inverse();
	B.P = BodyMass * V;
	B.L = FVector(Inertia.M[0][0] * W.X + Inertia.M[0][1] * W.Y + Inertia.M[0][2] * W.Z,
	              Inertia.M[1][0] * W.X + Inertia.M[1][1] * W.Y + Inertia.M[1][2] * W.Z,
	              Inertia.M[2][0] * W.X + Inertia.M[2][1] * W.Y + Inertia.M[2][2] * W.Z);
	B.Refresh();
	const FVector P0 = B.P, L0 = B.L;
	B.ContactImpulse(R, N, 0.36, 0.12);
	TestTrue(TEXT("impulse dP vs models/wipeout.py"), (B.P - P0).Equals(FVector(-729.4390509498032, 14125.982786040975, 19377.482078218825), 0.1));
	TestTrue(TEXT("impulse dL vs models/wipeout.py"), (B.L - L0).Equals(FVector(460009.3255980819, -150008.47772520006, 126671.04684141368), 2.0));

	B.P = BodyMass * V;
	B.L = FVector::ZeroVector;
	B.Refresh();
	const double Vn0 = FVector::DotProduct(B.PointVelocity(R), N);
	B.ContactImpulse(R, N, 0.15, 0.0);
	TestTrue(TEXT("restitution at the contact point"), FMath::IsNearlyEqual(FVector::DotProduct(B.PointVelocity(R), N), -0.15 * Vn0, 1e-2));
	B.L = FVector(1e9, 0, 0);
	B.Refresh();
	TestTrue(TEXT("spin cap"), FMath::IsNearlyEqual(B.W.Size(), double(MaxSpinRate), 1e-4));
	B.L = FVector::ZeroVector; B.P = BodyMass * V; B.Refresh();
	const FVector X0 = B.X;
	B.Integrate(FVector::ZeroVector, FVector::ZeroVector, 1.0 / 60.0);
	TestTrue(TEXT("x += v dt"), B.X.Equals(X0 + V / 60.0, 1e-3) && FMath::IsNearlyEqual(B.Q.Size(), 1.0, 1e-6));

	if (!NeedTable(this)) { return false; }
	FRiderProfile Prof;
	SSXRider::ApplyNewSavePlayerProfile(Prof, 10, 2);
	FTumbleBody Bb;
	const FVector Pos(10, 20, 30);
	const FQuat Q = FQuat(FVector(0.3, 0.2, 0.9).GetSafeNormal(), 0.7);
	TestTrue(TEXT("body from the bind pose"), Bb.Init(Prof, false, Pos, Q, FVector(0, 1000, 0)));
	TestTrue(TEXT("rider position round trip"), Bb.RiderPosition().Equals(Pos, 1e-3));
	TestTrue(TEXT("I^-1 symmetric"), FMath::IsNearlyEqual(Bb.InvI.M[0][1], Bb.InvI.M[1][0], 1e-12) && Bb.InvI.M[0][0] > 0 && Bb.InvI.M[1][1] > 0 && Bb.InvI.M[2][2] > 0);

	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 10.0, 1);
	FRiderSim Sim;
	SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, 10, 2);
	Sim.SetWorld(&World);
	Sim.Reset();
	Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
	Sim.State.Velocity = FVector(200, 1800, -300);
	FRiderImpact Impact;
	Impact.Point = Sim.State.Position;
	Impact.Normal = Sim.State.GroundNormal;
	Impact.Speed = -300;
	Sim.StartWipeOut(Impact, TEXT("test"));
	SSXSimPlaceholders::FWipeOutMotion* Motion = static_cast<SSXSimPlaceholders::FWipeOutMotion*>(Sim.GetMotionState(ERiderMotion::WipeOut));
	TestNotNull(TEXT("wipe-out motion"), Motion);
	double MaxW = 0.0, MinH = 1e30;
	int32 Frames = 0;
	bool bFinite = true;
	TSet<FString> Seen;
	for (; Frames < 60 * 8 && (Sim.State.Motion == ERiderMotion::WipeOut || Sim.State.Motion == ERiderMotion::WipeOutRecover); ++Frames)
	{
		Sim.Step(FRiderInputWord());
		const FRiderState& St = Sim.State;
		bFinite &= !St.Position.ContainsNaN() && !St.Velocity.ContainsNaN() && !St.Orientation.ContainsNaN();
		FRiderWorldHit G;
		if (World.LineQuery(St.Position + FVector(0, 0, 1000), St.Position - FVector(0, 0, 5000), ERiderLineQuery::Fine, -1.0, G))
		{
			MinH = FMath::Min(MinH, FVector::DotProduct(St.Position - G.Point, G.Normal));
		}
	}
	for (const FRiderSimEvent& E : Sim.GetEvents()) { Seen.Add(E.Reason); }
	AddInfo(FString::Printf(TEXT("tumble run: %d frames, lowest rider point %.1f cm, events: %s"), Frames, MinH, *FString::Join(Seen.Array(), TEXT(", "))));
	TestTrue(TEXT("finite"), bFinite);
	TestTrue(TEXT("the body does not sink through the slope (> -60 cm)"), MinH > -60.0);
	TestTrue(TEXT("the wipe-out ends (slide / get-up / quick recovery / reset)"), Sim.State.Motion != ERiderMotion::WipeOut || Frames < 60 * 8);
	return true;
}

#endif
