#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Particles/SSXRiderFx.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXParticles;

namespace
{
	TArray<double> TrNums(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<double> Out;
		for (const TSharedPtr<FJsonValue>& V : A) { Out.Add(V->AsNumber()); }
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesTrailsTest, "SSX.Particles.Trails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXParticlesTrailsTest::RunTest(const FString&)
{
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("prop_anim_reference.json"));
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		SSXTest::Skip(*this, TEXT("skipped: no SimData/prop_anim_reference.json (run tools/make_prop_anim_reference.py)"));
		return true;
	}
	const TSharedPtr<FJsonObject> T = Root->GetObjectField(TEXT("trails"));
	int32 Riders = 0, ActiveOk = 0, Entries = 0, WidthOk = 0, PairOk = 0, Ramp = 0;
	double MinW = 1e9, MaxW = 0.0;
	for (const TSharedPtr<FJsonValue>& RV : T->GetArrayField(TEXT("riders")))
	{
		const TSharedPtr<FJsonObject> R = RV->AsObject();
		const int32 Surf = int32(R->GetNumberField(TEXT("surface")));
		++Riders;
		ActiveOk += TrailActive(Surf, int32(R->GetNumberField(TEXT("motion"))), int32(R->GetNumberField(TEXT("kind")))) == (R->GetNumberField(TEXT("active")) != 0.0);
		const double Scale = TrailSurfaceScale(Surf);
		for (const TSharedPtr<FJsonValue>& EV : R->GetArrayField(TEXT("entries")))
		{
			const TArray<double> E = TrNums(EV->AsArray());
			const FVector A(E[0], E[1], E[2]), B(E[3], E[4], E[5]);
			const double K0 = E[6], K1 = E[7], Al = E[8], K0a = E[9], K1a = E[10], S = E[11];
			++Entries;
			const double W = FVector::Dist(A, B);
			MinW = FMath::Min(MinW, W);
			MaxW = FMath::Max(MaxW, W);
			WidthOk += Scale >= 0.0 && FMath::Abs(S - W * Scale) < 2e-3;
			PairOk += FMath::Abs(K0a - K0 * Al) < 1e-5 && FMath::Abs(K1a - K1 * Al) < 1e-5 && K0 * K1 <= 1e-9 && FMath::Abs(K0) <= 1.0 + 1e-6 && FMath::Abs(K1) <= 1.0 + 1e-6;
			Ramp += Al < 1.0;
		}
	}
	TestEqual(TEXT("trail active rule = the FIFO flag"), ActiveOk, Riders);
	TestEqual(TEXT("S = width x surface scale"), WidthOk, Entries);
	TestEqual(TEXT("side weights: cached k alpha, opposite signs"), PairOk, Entries);
	const TSharedPtr<FJsonObject> Rules = T->GetObjectField(TEXT("rules"));
	int32 RuleN = 0, RuleOk = 0;
	for (const TSharedPtr<FJsonValue>& V : Rules->GetArrayField(TEXT("weights")))
	{
		const TArray<double> W = TrNums(V->AsArray());
		double KA, KB;
		TrailSideWeights(W[0], W[1], KA, KB);
		++RuleN;
		RuleOk += FMath::Abs(KA - W[2]) < 1e-6 && FMath::Abs(KB - W[3]) < 1e-6;
	}
	for (const TSharedPtr<FJsonValue>& V : Rules->GetArrayField(TEXT("push")))
	{
		const TArray<TSharedPtr<FJsonValue>>& P = V->AsArray();
		++RuleN;
		RuleOk += TrailShouldPush(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber(), P[3]->AsNumber(), P[4]->AsBool()) == P[5]->AsBool();
	}
	for (const TSharedPtr<FJsonValue>& V : Rules->GetArrayField(TEXT("scale")))
	{
		const TArray<double> S = TrNums(V->AsArray());
		++RuleN;
		RuleOk += FMath::Abs(TrailSurfaceScale(int32(S[0])) - S[1]) < 1e-7;
	}
	const TArray<TSharedPtr<FJsonValue>>& Ra = Rules->GetArrayField(TEXT("ribbon_alpha"));
	for (int32 N = 2; N < 8; ++N)
	{
		const TArray<double> A = TrNums(Ra[N - 2]->AsArray());
		bool bOk = A.Num() == N + 1;
		for (int32 K = 0; K <= N && bOk; ++K) { bOk &= FMath::Abs(FBoostRibbons::OuterAlpha(N, K) - A[K]) < 1e-6; }
		++RuleN;
		RuleOk += bOk;
	}
	TestEqual(TEXT("trail / ribbon rules match models/particles.py"), RuleOk, RuleN);
	// 0x132648
	FGroundTrail G;
	FRiderFxInput In;
	In.Motion = 2; In.ContactKind = 0; In.Surface = 1; In.Vel = FVector(0, 1200, 0);
	In.BoardLong = FVector(0, 1, 0); In.BoardAcross = FVector(1, 0, 0); In.GroundNormal = FVector(0, 0, 1);
	In.BoardClass = 0; In.SizeScale = 0.95;
	const double H0 = 16.22651, Len0 = 96.09368 + 98.02858;
	for (int32 F = 0; F < 120; ++F)
	{
		In.Contact = In.BoardOrigin = FVector(0, 20.0 * F, 0);
		G.Frame(In, 1.0, 0.0);
	}
	bool bSpaced = G.Num() > 3;
	for (int32 J = 1; J + 1 < G.Num() && bSpaced; ++J)
	{
		const FGroundTrail::FEntry& A = G.Get(J);
		const FGroundTrail::FEntry& B = G.Get(J + 1);
		bSpaced &= FVector::Dist((A.A + A.B) * 0.5, (B.A + B.B) * 0.5) > FGroundTrail::PushDist;
	}
	TestTrue(TEXT("stored entries more than 110 cm apart at speed"), bSpaced);
	TestTrue(TEXT("D = velocity x normal: A on +x"), G.Get(1).A.X > G.Get(1).B.X);
	TestTrue(TEXT("lit side: k_A < 0 when +D is lit (darkened wall on the other side)"), G.Get(1).KA < 0.0 && G.Get(1).KB > 0.0);
	const double W0 = 2.0 * H0 * 0.95;
	TestTrue(TEXT("board along the motion: stored width = 2 h s, S = width x 0.02856"),
		FMath::IsNearlyEqual(FVector::Dist(G.Get(1).A, G.Get(1).B), W0, 1e-4) && FMath::Abs(G.Get(1).S - W0 * 0.02855919) < 1e-6);
	TestTrue(TEXT("live head: the across edge, same width"), FMath::IsNearlyEqual(FVector::Dist(G.Get(0).A, G.Get(0).B), W0, 1e-4));
	{
		const int32 C = G.Num();
		TestTrue(TEXT("run ramp 0 / 0.3 / 0.6 / 0.9 / 1, break on the run's first entry"), C > 6 && G.Get(C).bBreak && G.Get(C).Alpha == 0.0
			&& FMath::IsNearlyEqual(G.Get(C - 1).Alpha, 0.3, 1e-6) && FMath::IsNearlyEqual(G.Get(C - 2).Alpha, 0.6, 1e-6)
			&& FMath::IsNearlyEqual(G.Get(C - 3).Alpha, 0.9, 1e-6) && G.Get(C - 4).Alpha == 1.0 && !G.Get(C - 1).bBreak);
	}
	for (int32 Cls = 0; Cls < 3; ++Cls)
	{
		static const double Ext[3][3] = { { 96.09368, 98.02858, 16.22651 }, { 84.922966, 84.07263, 16.232784 }, { 114.15547, 124.23839, 15.998993 } };
		FGroundTrail Gy;
		FRiderFxInput Iy = In;
		Iy.BoardClass = Cls;
		Iy.SizeScale = 0.9;
		const double Yaw = FMath::DegreesToRadians(30.0);
		Iy.BoardLong = FVector(FMath::Sin(Yaw), FMath::Cos(Yaw), 0);
		Iy.BoardAcross = FVector(FMath::Cos(Yaw), -FMath::Sin(Yaw), 0);
		Iy.Contact = Iy.BoardOrigin = FVector::ZeroVector;
		Gy.Frame(Iy, 1.0, 0.0);
		const double L = (Ext[Cls][0] + Ext[Cls][1]) * 0.9, Wd = 2.0 * Ext[Cls][2] * 0.9;
		const double Push = L * 0.5 + Wd * FMath::Cos(Yaw);
		TestTrue(FString::Printf(TEXT("class %d: stored width = max projected diagonal"), Cls), FMath::IsNearlyEqual(FVector::Dist(Gy.Get(1).A, Gy.Get(1).B), Push, 1e-3));
		TestTrue(FString::Printf(TEXT("class %d: live width = (front + back) s"), Cls), FMath::IsNearlyEqual(FVector::Dist(Gy.Get(0).A, Gy.Get(0).B), L, 1e-3)
			&& FMath::IsNearlyEqual(Gy.Get(0).S, L * 0.02855919, 1e-5));
	}
	{
		FGroundTrail Gb;
		FRiderFxInput Ib = In;
		Ib.Steer = 0.5;
		Ib.Brake = 0.0;
		Gb.Frame(Ib, 1.0, 0.0);
		const double WNarrow = FVector::Dist(Gb.Get(1).A, Gb.Get(1).B);
		const double WLive = FVector::Dist(Gb.Get(0).A, Gb.Get(0).B);
		Gb.Reset();
		Ib.Brake = 0.01;
		Gb.Frame(Ib, 1.0, 0.0);
		const double WFull = FVector::Dist(Gb.Get(1).A, Gb.Get(1).B);
		TestTrue(TEXT("brake == 0: x (1 - 0.85 |steer|); any brake: full width"), FMath::IsNearlyEqual(WNarrow, W0 * (1.0 - 0.425), 1e-4)
			&& FMath::IsNearlyEqual(WFull, W0, 1e-4) && FMath::IsNearlyEqual(WLive, W0, 1e-4));
	}
	{
		FGroundTrail Gc;
		FRiderFxInput Ic = In;
		const double Yaw = FMath::DegreesToRadians(30.0);
		Ic.BoardLong = FVector(FMath::Sin(Yaw), FMath::Cos(Yaw), 0);
		Ic.BoardAcross = FVector(FMath::Cos(Yaw), -FMath::Sin(Yaw), 0);
		for (const double Y : { 0.0, 200.0, 250.0 })   // the third frame is 50 cm on, straight: no push
		{
			Ic.Contact = Ic.BoardOrigin = FVector(0, Y, 0);
			Gc.Frame(Ic, 1.0, 0.0);
		}
		TestTrue(TEXT("live head: back vertex clamped to the newest entry, front vertex free"), Gc.Num() == 2 && Gc.Get(0).B.Equals(Gc.Get(1).B)
			&& Gc.Get(0).A.Y > 250.0);
	}
	In.Surface = 0;
	G.Frame(In, 1.0, 0.0);
	TestTrue(TEXT("trail end: newest entries tapered 0 / 0.3 / 0.6 / 0.9"), !G.IsActive() && FMath::Abs(G.Get(1).Alpha) < 1e-9
		&& FMath::Abs(G.Get(2).Alpha - 0.3) < 1e-6 && FMath::Abs(G.Get(4).Alpha - 0.9) < 1e-6 && G.Get(5).Alpha == 1.0 && G.GetRunAlpha() == 0.f);
	FBoostRibbons Rb;
	const FRibbonSample S = FBoostRibbons::MakeSample(FVector(0, 0, 100), FVector(0, 0, 80), FVector(0, 0, 50), FVector(10, 0, 100), FVector(10, 0, 80), FVector(10, 0, 50), FVector4());
	for (int32 F = 0; F < 30; ++F) { Rb.Frame(F, 1.0, 0.0, S); }
	TestTrue(TEXT("ribbon ring full (7) and drawn"), Rb.GetCount() == 7 && Rb.IsDrawn());
	TestTrue(TEXT("outer point 15 cm past the hand"), FMath::Abs(S.Outer[0].Z - 115.001) < 1e-6 && S.Inner[0].Z == 50.0);
	TestTrue(TEXT("tint by tier (high: red)"), FMath::Abs(Rb.Get(0).Tint.Z - 0.0758) < 1e-6);
	for (int32 F = 30; F < 48; ++F) { Rb.Frame(F, 0.0, 0.0, S); }
	TestEqual(TEXT("ribbon count falls 1 per 3 frames"), Rb.GetCount(), 1);
	AddInfo(FString::Printf(TEXT("RAM: %d trail FIFOs, %d entries (widths %.1f..%.1f cm; %d tapered); S rule %d / %d, k pairs %d / %d; rules %d / %d"),
		Riders, Entries, MinW, MaxW, Ramp, WidthOk, Entries, PairOk, Entries, RuleOk, RuleN));
	return true;
}

// 0x1333C0 / 0x411870
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesTrailDrawOffsetTest, "SSX.Particles.TrailDrawOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXParticlesTrailDrawOffsetTest::RunTest(const FString&)
{
	const FVector Right(0.99998939, 0.00459695, -0.00025119);
	const FVector Up(-0.00130453, 0.33525920, 0.94212502);
	const FVector Fwd(-0.00441512, 0.94211477, -0.33526167);
	const FVector Row2(Right.Z, Up.Z, Fwd.Z);
	const FVector O = FGroundTrail::DrawOffset(Right, Up, Fwd);
	TestTrue(TEXT("offset = -10 x row 2 of the dump's world->view matrix"), O.Equals(FVector(0.0025119, -9.4212502, 3.3526167), 1e-6));
	TestTrue(TEXT("dump camera (looking along +Y): 10.00 cm toward the camera along the view axis"), FMath::Abs(O.Dot(Fwd) + 10.0) < 1e-3);
	const FQuat Yaw(FVector::UpVector, PI);
	const FVector O2 = FGroundTrail::DrawOffset(Yaw.RotateVector(Right), Yaw.RotateVector(Up), Yaw.RotateVector(Fwd));
	TestTrue(TEXT("yaw-independent (not the view axis)"), O2.Equals(O, 1e-6));
	TestTrue(TEXT("camera looking along -Y: 7.75 cm farther along the view axis"), FMath::Abs(O2.Dot(Yaw.RotateVector(Fwd)) - 7.7519) < 1e-3);
	const FRotationMatrix M(FRotationMatrix::MakeFromXZ(FVector(Fwd.X, -Fwd.Y, Fwd.Z), FVector(Up.X, -Up.Y, Up.Z)).Rotator());
	const FVector O3 = FGroundTrail::DrawOffset(M.GetUnitAxis(EAxis::Y), M.GetUnitAxis(EAxis::Z), M.GetUnitAxis(EAxis::X));
	TestTrue(TEXT("from the Unreal camera rotation"), O3.Equals(O, 1e-3));
	AddInfo(FString::Printf(TEXT("offset (%.4f, %.4f, %.4f); along the view axis %.3f (dump camera), %.3f (turned 180)"),
		O.X, O.Y, O.Z, O.Dot(Fwd), O2.Dot(Yaw.RotateVector(Fwd))));
	return true;
}

// 0x1333C0 / 0x132648 / 0x129628
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXParticlesTrailStripsTest, "SSX.Particles.TrailStrips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXParticlesTrailStripsTest::RunTest(const FString&)
{
	auto Entry = [](double Y, double KA, double KB, bool bBreak = false)
	{
		FGroundTrail::FEntry E;
		E.A = FVector(10, Y, 0);
		E.B = FVector(-10, Y, 0);
		E.KA = KA;
		E.KB = KB;
		E.Alpha = 1.0;
		E.S = 0.8;
		E.bBreak = bBreak;
		return E;
	};
	const FGroundTrail::FEntry Head = Entry(0, -0.5, 0.5);
	const TArray<FGroundTrail::FEntry> Stored = { Entry(-100, -0.5, 0.5), Entry(-200, 0.5, -0.5), Entry(-300, 0.5, -0.5, true),
		Entry(-400, -0.5, 0.5), Entry(-500, -0.5, 0.5) };
	const FVector Off(0, 0, 1);
	TArray<TArray<FTrailVertex>> P1, P2;
	TrailStrips(&Head, Stored, false, Off, P1);
	TrailStrips(&Head, Stored, true, Off, P2);
	if (!TestTrue(TEXT("two strips (8 + 4 vertices) per pass"), P1.Num() == 2 && P2.Num() == 2 && P1[0].Num() == 8 && P1[1].Num() == 4 && P2[0].Num() == 8))
	{
		return false;
	}
	TestTrue(TEXT("offset applied"), P1[0][0].Pos.Equals(FVector(10, 0, 1)));
	TestTrue(TEXT("head pass 1: alpha A 32 / B 0, U A 0 / B S"), P1[0][0].Alpha == 32 && P1[0][1].Alpha == 0 && P1[0][0].U == 0.f && P1[0][1].U == 0.8f);
	TestTrue(TEXT("head pass 2: alpha A 0 / B 64, U A S / B 0"), P2[0][0].Alpha == 0 && P2[0][1].Alpha == 64 && P2[0][0].U == 0.8f && P2[0][1].U == 0.f);
	TestTrue(TEXT("T: head 0, then 1 / 0 / 1 / 0"), P1[0][0].T == 0.f && P1[0][2].T == 1.f && P1[0][4].T == 0.f && P1[0][6].T == 1.f && P1[1][0].T == 0.f && P1[1][2].T == 1.f);
	TestTrue(TEXT("swap: the previous pair and this pair alpha 0"), P1[0][2].Alpha == 0 && P1[0][3].Alpha == 0 && P1[0][4].Alpha == 0 && P1[0][5].Alpha == 0);
	TestTrue(TEXT("no swap: alpha kept"), P1[0][6].Alpha == 0 && P1[0][7].Alpha == 32 && P1[0][6].U == 0.8f);
	TestTrue(TEXT("swap across a break: gap on the new strip's first pair only"), P1[1][0].Alpha == 0 && P1[1][1].Alpha == 0 && P1[1][2].Alpha == 32
		&& P1[0][6].Alpha == 0 && P1[0][7].Alpha == 32);
	{
		const TArray<FGroundTrail::FEntry> Zero = { Entry(0, 0.0, 0.0), Entry(-100, 0.0, 0.0) };
		TArray<TArray<FTrailVertex>> Z;
		TrailStrips(nullptr, Zero, true, FVector::ZeroVector, Z);
		TestTrue(TEXT("tie: U = 0 on B; no head: T 0 / 1"), Z.Num() == 1 && Z[0].Num() == 4 && Z[0][0].U == 0.8f && Z[0][1].U == 0.f && Z[0][0].T == 0.f && Z[0][2].T == 1.f);
	}
	{
		FGroundTrail G;
		FRiderFxInput In;
		In.Motion = 2; In.ContactKind = 0; In.Surface = 1; In.Vel = FVector(0, 1200, 0); In.GroundNormal = FVector(0, 0, 1);
		In.BoardClass = 0; In.SizeScale = 0.95;
		const double Yaw = FMath::DegreesToRadians(30.0);
		In.LiveBoardLong = FVector(FMath::Sin(Yaw), FMath::Cos(Yaw), 0);
		In.LiveBoardAcross = FVector(FMath::Cos(Yaw), -FMath::Sin(Yaw), 0);
		In.BoardLong = FVector(0, 1, 0);
		In.BoardAcross = FVector(1, 0, 0);
		G.Frame(In, 1.0, 0.0);
		const double W0 = 2.0 * 16.22651 * 0.95;
		TestTrue(TEXT("previous-pose corners: live head = the across edge (2 h s), stored width 2 h s"), FMath::IsNearlyEqual(FVector::Dist(G.Get(0).A, G.Get(0).B), W0, 1e-4)
			&& FMath::IsNearlyEqual(FVector::Dist(G.Get(1).A, G.Get(1).B), W0, 1e-4));
	}
	return true;
}

#endif
