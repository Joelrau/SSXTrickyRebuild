// 0x1364D0 / 0x207F18
#include "Misc/AutomationTest.h"
#include "Particles/SSXRiderFx.h"
#include "Particles/SSXSnowFx.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXShadowGateTest, "SSX.Particles.ShadowGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXShadowGateTest::RunTest(const FString& Parameters)
{
	using namespace SSXParticles;
	struct FCase { int32 Motion; double H; bool bBoard; };
	static const FCase Cases[] = { { 2, -0.4965, false }, { 2, 0.0, true }, { 2, 1.0, true }, { 3, -2.0, true },
		{ 2, -5.0, false }, { 2, -7.0, false }, { 2, -2.4e-7, true }, { 2, -1e-6, false } };
	for (const FCase& C : Cases)
	{
		TestEqual(FString::Printf(TEXT("board in silhouette, motion %d h %g"), C.Motion, C.H), ShadowBoardInSilhouette(C.Motion, C.H), C.bBoard);
	}
	TestEqual(TEXT("subtract 32 / 255"), ShadowSubtract, 32.0 / 255.0);
	auto Out = [](int32 Cd, int32 Cs) { return FMath::Max(Cd - ((Cs * 128) >> 7), 0); };
	TestEqual(TEXT("Cd 128"), Out(128, 32), 96);
	TestEqual(TEXT("Cd 20 clamps"), Out(20, 32), 0);
	TestEqual(TEXT("depth range"), ShadowDepthBelow + ShadowDepthAbove, 3100.0);
	return !HasAnyErrors();
}

// 0x1C7E90 / 0x129628
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXShadowExtentsTest, "SSX.Particles.ShadowExtents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSSXShadowExtentsTest::RunTest(const FString& Parameters)
{
	using namespace SSXParticles;
	// class 2 board (front 114.155, back 124.238, half-width 15.999), scale 0.9, along +y, origin 50 cm under the eye
	FRiderFxInput In;
	In.BoardClass = 2;
	In.SizeScale = 0.9;
	In.BoardLong = FVector(0, 1, 0);
	In.BoardAcross = FVector(1, 0, 0);
	In.BoardOrigin = FVector(0, 0, -50);
	FVector P[4];
	BoardCorners(In, P);
	TArray<FVector> Pts(P, 4);
	Pts.Add(FVector(5, 3, 0));
	const FBox2D E = ShadowExtents(FVector::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), Pts);
	TestTrue(TEXT("x: +-(h s + 20)"), FMath::IsNearlyEqual(E.Min.X, -(15.998993 * 0.9 + 20.0), 1e-4) && FMath::IsNearlyEqual(E.Max.X, 15.998993 * 0.9 + 20.0, 1e-4));
	TestTrue(TEXT("y: -(back s + 20) .. front s + 20 (a rectangle, not a square)"), FMath::IsNearlyEqual(E.Min.Y, -(124.23839 * 0.9 + 20.0), 1e-4)
		&& FMath::IsNearlyEqual(E.Max.Y, 114.15547 * 0.9 + 20.0, 1e-4));
	In.ProfileStance = 1;
	BoardCorners(In, P);
	const FBox2D G = ShadowExtents(FVector::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), TArray<FVector>(P, 4));
	TestTrue(TEXT("profile +0x40 != 0: front / back swapped"), FMath::IsNearlyEqual(G.Max.Y, 124.23839 * 0.9 + 20.0, 1e-4));
	return !HasAnyErrors();
}

#endif
