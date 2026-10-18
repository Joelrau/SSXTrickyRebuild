// FUN_00260278 / FUN_00260458
#include "Misc/AutomationTest.h"
#include "Particles/SSXFogVolumes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags FogVisFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXFogVisibilityTest, "SSX.Fog.Visibility", FogVisFlags)
bool FSSXFogVisibilityTest::RunTest(const FString& Parameters)
{
	using namespace SSXFog;
	FClipView V;
	V.Pos = FVector::ZeroVector;
	V.Fwd = FVector(0, 1, 0);
	V.Right = FVector(1, 0, 0);
	V.Up = FVector(0, 0, 1);
	V.CotH = 1.0;
	V.CotV = 4.0 / 3.0;
	V.Near = 15.0;
	V.Far = 30000.0;
	TestEqual(TEXT("box ahead inside"), V.BoxTest(FVector(-10, 1000, -10), FVector(10, 1100, 10)), 0);
	TestEqual(TEXT("box behind outside"), V.BoxTest(FVector(-10, -1100, -10), FVector(10, -1000, 10)), 1);
	TestEqual(TEXT("box beyond far outside"), V.BoxTest(FVector(-10, 31000, -10), FVector(10, 32000, 10)), 1);
	TestEqual(TEXT("box across the left plane"), V.BoxTest(FVector(-1500, 1000, -10), FVector(0, 1100, 10)), 2);
	TestEqual(TEXT("box straddling near"), V.BoxTest(FVector(-1, -5, -1), FVector(1, 100, 1)), 2);

	// grid 4 x 12 of 10000 cm from (-20000, -10000): every cell present; the camera at (42.4, 29538.1): cell (2, 3)
	FFogData D;
	D.GridMinX = -20000.0;
	D.GridMinY = -10000.0;
	D.CellSize = 10000.0;
	D.NX = 4;
	D.NY = 12;
	for (int32 Iy = 0; Iy < 12; ++Iy)
	{
		for (int32 Ix = 0; Ix < 4; ++Ix)
		{
			if (Ix == 1 && Iy == 5) { continue; }
			FCell& C = D.Cells.AddDefaulted_GetRef();
			C.Index = Iy * 4 + Ix;
			C.Lo = FVector(-20000.0 + 10000.0 * Ix, -10000.0 + 10000.0 * Iy, -100.0);
			C.Hi = C.Lo + FVector(10000.0, 10000.0, 200.0);
			for (int32 K = 0; K < 16; ++K)
			{
				C.Subs[K].Lo = C.Lo + FVector(2500.0 * (K % 4), 2500.0 * (K / 4), 0.0);
				C.Subs[K].Hi = C.Subs[K].Lo + FVector(2500.0, 2500.0, 200.0);
				C.Subs[K].bEmpty = false;
			}
			D.CellOfIndex.Add(C.Index, D.Cells.Num() - 1);
		}
	}
	FClipView Cam = V;
	Cam.Pos = FVector(42.4, 29538.1, 0.0);
	Cam.Far = 49548.52;
	TArray<FFogData::FVisible> Vis;
	D.VisibleCells(Cam, 5, Vis);
	TArray<int32> Want;
	for (int32 Ix = 0; Ix <= 3; ++Ix)
	{
		for (int32 Iy = 0; Iy <= 8; ++Iy)
		{
			if (!(Ix == 1 && Iy == 5)) { Want.Add(Iy * 4 + Ix); }
		}
	}
	TArray<int32> Got;
	for (const FFogData::FVisible& E : Vis) { Got.Add(D.Cells[E.Cell].Index); }
	TestEqual(TEXT("candidates: ix outer, iy inner, radius 5, null skipped"), Got, Want);
	int32 Behind = 0, Ahead = 0;
	for (const FFogData::FVisible& E : Vis)
	{
		const FCell& C = D.Cells[E.Cell];
		if (C.Hi.Y <= 20000.0) { Behind += E.Vis == 0 ? 1 : 0; }
		if (C.Lo.Y >= 40000.0 && C.Lo.X >= -10000.0 && C.Hi.X <= 10000.0) { Ahead += E.Vis != 0 ? 1 : 0; }
	}
	TestEqual(TEXT("cells behind the camera hidden"), Behind, 12);
	TestTrue(TEXT("cells ahead visible"), Ahead > 0);

	D.Models.AddDefaulted();
	D.Models[0].Add(FPoint());
	auto AddInst = [&D](const FVector& P, double Half)
	{
		FInstance& I = D.Instances.AddDefaulted_GetRef();
		I.T = P;
		I.BoxMin = P - FVector(Half);
		I.BoxMax = P + FVector(Half);
		return D.Instances.Num() - 1;
	};
	const int32 IAhead = AddInst(FVector(100.0, 35000.0, 0.0), 50.0);
	const int32 ISide = AddInst(FVector(9900.0, 31000.0, 0.0), 50.0);
	const int32 IBehind = AddInst(FVector(0.0, 25000.0, 0.0), 50.0);
	auto SubOf = [&D](const FVector& P) -> FSubCell&
	{
		const int32 Ix = int32((P.X + 20000.0) / 10000.0), Iy = int32((P.Y + 10000.0) / 10000.0);
		FCell& C = D.Cells[D.CellOfIndex[Iy * 4 + Ix]];
		const int32 K = int32((P.Y - C.Lo.Y) / 2500.0) * 4 + int32((P.X - C.Lo.X) / 2500.0);
		return C.Subs[K];
	};
	SubOf(D.Instances[IAhead].T).Instances.Add(IAhead);
	SubOf(D.Instances[ISide].T).Instances.Add(ISide);
	SubOf(D.Instances[IBehind].T).Instances.Add(IBehind);
	TArray<int32> Order;
	D.SubmissionOrder(Cam, 5, Order);
	TestTrue(TEXT("ahead drawn"), Order.Contains(IAhead));
	TestFalse(TEXT("off to the side culled"), Order.Contains(ISide));
	TestFalse(TEXT("behind culled"), Order.Contains(IBehind));
	TArray<int32> Narrow;
	D.SubmissionOrder(Cam, 0, Narrow);
	TestFalse(TEXT("radius 0: only the camera's cell"), Narrow.Contains(IAhead));
	return !HasAnyErrors();
}

#endif
