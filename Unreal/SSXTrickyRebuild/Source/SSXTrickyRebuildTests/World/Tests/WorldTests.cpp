#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SSXWorldLighting.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags WorldTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	bool Near(double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol * FMath::Max(1.0, FMath::Abs(B)); }
	bool NearV(const FVector& A, const FVector& B, double Tol) { return Near(A.X, B.X, Tol) && Near(A.Y, B.Y, Tol) && Near(A.Z, B.Z, Tol); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldTrackTableTest, "SSX.World.TrackTable", WorldTestFlags)
bool FSSXWorldTrackTableTest::RunTest(const FString&)
{
	TestEqual(TEXT("gari id"), FSSXTrackPresentation::IdForStem(TEXT("Gari")), 0);
	TestEqual(TEXT("alaska id"), FSSXTrackPresentation::IdForStem(TEXT("alaska")), 10);
	TestEqual(TEXT("front end id"), FSSXTrackPresentation::IdForStem(TEXT("FrontEnd")), 11);
	TestEqual(TEXT("unknown id"), FSSXTrackPresentation::IdForStem(TEXT("nowhere")), -1);

	TArray<FSSXTrackPresentation> T;
	FString Err;
	if (!FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), T, &Err))
	{
		AddError(Err);
		return false;
	}
	TestEqual(TEXT("13 records"), T.Num(), 13);
	if (T.Num() < 12) { return false; }
	TestTrue(TEXT("Gari draw 30000/20000"), Near(T[0].DrawDist1P, 30000.0, 1e-6) && Near(T[0].DrawDist2P, 20000.0, 1e-6));
	TestTrue(TEXT("Gari mip D 27571"), Near(T[0].MipDist1P, 27570.64844, 1e-6) && T[0].bMipLodOn && T[0].MipShift1P == 3);
	TestTrue(FString::Printf(TEXT("Gari TEX1 (LCM, L, K) = %s"), *T[0].MipLodParams1P().ToString()), T[0].MipLodParams1P() == FIntVector(0, 3, -1888));
	TestTrue(TEXT("Gari clear (52,115,195)"), T[0].ClearRGB == FIntVector(52, 115, 195));
	TestTrue(TEXT("Snowdream snow 1.0, scale 1.705"), Near(T[1].SnowChance, 1.0, 1e-9) && Near(T[1].RiderLightScale, 1.70482, 1e-6));
	TestTrue(TEXT("Merqury snow 0.25"), Near(T[4].SnowChance, 0.25005, 1e-6));
	TestTrue(TEXT("Aloha draw 17438 / mip D 14542"), Near(T[5].DrawDist1P, 17437.76172, 1e-6) && Near(T[5].MipDist1P, 14542.35449, 1e-6));
	TestTrue(TEXT("Pipedream draw 49549"), Near(T[6].DrawDist1P, 49548.51953, 1e-6));
	TestTrue(TEXT("Alaska draw 40000 / mip D 37002"), Near(T[10].DrawDist1P, 40000.0, 1e-6) && Near(T[10].MipDist1P, 37001.95703, 1e-6));
	TestTrue(TEXT("Gari rider f / C / scale"), Near(T[0].RiderAmbBlend, 0.40715, 1e-6) && T[0].RiderAmbTarget.IsNearlyZero() && Near(T[0].RiderLightScale, 1.991, 1e-6));
	TestTrue(TEXT("Elysium rider C"), NearV(T[2].RiderAmbTarget, FVector(0.3836, 0.2395, 0.3828), 1e-6) && Near(T[2].RiderAmbBlend, 1.0, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldLightMathTest, "SSX.World.LightMath", WorldTestFlags)
bool FSSXWorldLightMathTest::RunTest(const FString&)
{
	TestTrue(TEXT("falloff 1"), Near(FSSXLightWorld::FalloffFactor(1, 400.0), 0.25, 1e-12));
	TestTrue(TEXT("falloff 2"), Near(FSSXLightWorld::FalloffFactor(2, 200.0), 0.25, 1e-12));
	TestTrue(TEXT("falloff 3"), Near(FSSXLightWorld::FalloffFactor(3, 1000.0), 1e-3, 1e-12));
	TestTrue(TEXT("falloff knee"), Near(FSSXLightWorld::FalloffFactor(2, 50.0), 1.0, 1e-12));

	FSSXLightWorld W;
	TArray<FSSXLight> L;
	FSSXLight Amb; Amb.Type = FSSXLight::Ambient; Amb.Colour = FVector(0.5); L.Add(Amb);
	FSSXLight AmbN = Amb; AmbN.Flags = FSSXLight::FlagNotModulated; AmbN.Colour = FVector(0.1); L.Add(AmbN);
	FSSXLight Sun; Sun.Type = FSSXLight::Directional; Sun.Colour = FVector(2.0); Sun.Direction = FVector(0, 0, -1); L.Add(Sun);
	FSSXLight Neg; Neg.Type = FSSXLight::Point; Neg.Flags = FSSXLight::FlagModulator; Neg.Colour = FVector(-50.0, 0.0, -400.0);
	Neg.Position = FVector(1000, 0, 0); Neg.Falloff = 1; L.Add(Neg);
	FSSXLight Spot; Spot.Type = FSSXLight::Spot; Spot.Colour = FVector(100.0); Spot.Position = FVector(0, 0, 1000);
	Spot.Direction = FVector(0, 0, -1); Spot.CosCone = 0.5; Spot.Falloff = 1; Spot.Exponent = 2.0; L.Add(Spot);
	W.SetLights(MoveTemp(L));
	const TArray<int32> All = { 0, 1, 2, 3, 4 };
	const FSSXRiderLightAdjust Unit{ 0.0, FVector::ZeroVector, 1.0 };

	const FSSXRiderLight R = W.ProbeList(All, FVector::ZeroVector, Unit);
	TestTrue(TEXT("modulator"), NearV(R.Modulator, FVector(0.0, 1.0, 0.0), 1e-12));
	TestTrue(TEXT("ambient = 0.5*M + 0.1"), NearV(R.Ambient, FVector(0.1, 0.6, 0.1), 1e-12));
	TestTrue(TEXT("slot 2 = spot"), NearV(R.Colour[2], FVector(0.0, 10.0, 0.0), 1e-12) && NearV(R.Direction[2], FVector(0, 0, 1), 1e-12));
	TestTrue(TEXT("slot 1 = sun"), NearV(R.Colour[1], FVector(0.0, 2.0, 0.0), 1e-12) && NearV(R.Direction[1], FVector(0, 0, 1), 1e-12));
	TestTrue(TEXT("slot 0 empty"), NearV(R.Colour[0], FVector::ZeroVector, 1e-12) && NearV(R.Direction[0], FVector(1, 0, 0), 1e-12));
	TestTrue(TEXT("total"), NearV(R.Total, R.Ambient + R.Colour[1] + R.Colour[2], 1e-12));
	const FSSXRiderLight R2 = W.ProbeList(All, FVector(5000, 0, 0), Unit);
	TestTrue(TEXT("outside cone: sun in slot 2"), NearV(R2.Colour[2], FVector(0.0, 2.0, 0.0), 1e-12) && NearV(R2.Colour[1], FVector::ZeroVector, 1e-12));
	const FSSXRiderLight R3 = W.ProbeList({ 0, 2 }, FVector(5000, 0, 0), { 0.5, FVector(1.0, 0.0, 0.0), 2.0 });
	TestTrue(TEXT("adjust ambient"), NearV(R3.Ambient, FVector(1.5, 0.5, 0.5), 1e-12));
	TestTrue(TEXT("adjust light"), NearV(R3.Colour[2], FVector(4.0), 1e-12));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldRiderProbeTest, "SSX.World.RiderProbe", WorldTestFlags)
bool FSSXWorldRiderProbeTest::RunTest(const FString&)
{
	TArray<FSSXTrackPresentation> Table;
	FString Err;
	if (!FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table, &Err))
	{
		AddError(Err);
		return false;
	}
	TArray<FString> Lines;
	const FString Csv = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("world_probe_reference.csv"));
	if (!FFileHelper::LoadFileToStringArray(Lines, *Csv) || Lines.Num() < 2)
	{
		AddError(TEXT("cannot read SimData/world_probe_reference.csv (run tools/make_world_reference.py)"));
		return false;
	}
	TMap<FString, FSSXLightWorld> Worlds;
	int32 Pass = 0, Fail = 0;
	for (int32 I = 1; I < Lines.Num(); ++I)
	{
		TArray<FString> C;
		Lines[I].ParseIntoArray(C, TEXT(","));
		if (C.Num() < 31) { continue; }
		const FString& Stem = C[0];
		FSSXLightWorld* W = Worlds.Find(Stem);
		if (!W)
		{
			W = &Worlds.Add(Stem);
			if (!W->Load(FSSXLightWorld::DefaultPath(Stem), &Err))
			{
				AddError(Err);
				return false;
			}
		}
		auto V = [&](int32 K) { return FVector(FCString::Atod(*C[K]), FCString::Atod(*C[K + 1]), FCString::Atod(*C[K + 2])); };
		const int32 Id = FSSXTrackPresentation::IdForStem(Stem);
		const FSSXRiderLight R = W->Probe(V(1), FSSXRiderLightAdjust::From(Table[Id]));
		const double Tol = 2e-4;
		bool bOk = NearV(R.Ambient, V(4), Tol) && NearV(R.Modulator, V(25), Tol) && NearV(R.Total, V(28), Tol);
		for (int32 S = 0; S < FSSXRiderLight::Slots; ++S)
		{
			bOk &= NearV(R.Colour[S], V(7 + 6 * S), Tol) && NearV(R.Direction[S], V(10 + 6 * S), Tol);
		}
		if (bOk) { ++Pass; }
		else if (++Fail <= 5)
		{
			AddError(FString::Printf(TEXT("%s P=(%s): ambient %s vs %s, c0 %s vs %s, c1 %s vs %s, c2 %s vs %s"), *Stem, *V(1).ToString(),
				*R.Ambient.ToString(), *V(4).ToString(), *R.Colour[0].ToString(), *V(7).ToString(), *R.Colour[1].ToString(), *V(13).ToString(),
				*R.Colour[2].ToString(), *V(19).ToString()));
		}
	}
	AddInfo(FString::Printf(TEXT("rider probe vs models/rider_light.py: %d/%d cases match"), Pass, Pass + Fail));
	return Fail == 0 && Pass > 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldRiderProbeRamTest, "SSX.World.RiderProbeRam", WorldTestFlags)
bool FSSXWorldRiderProbeRamTest::RunTest(const FString&)
{
	TArray<FSSXTrackPresentation> Table;
	FSSXLightWorld W;
	FString Err;
	if (!FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table, &Err) || !W.Load(FSSXLightWorld::DefaultPath(TEXT("gari")), &Err))
	{
		AddError(Err);
		return false;
	}
	const FVector Positions[] = { {5240.753, -8613.688, 839.335}, {5058.678, -8557.438, 801.264}, {6684.472, -8677.104, 993.155}, {6688.588, -8537.922, 993.289} };
	const FVector RamAmbient(0.56500, 0.65453, 0.80029), RamColour(4.91777, 4.46042, 4.06208), RamDir(0.55572, -0.00578, 0.83135);
	for (const FVector& P : Positions)
	{
		const FSSXRiderLight R = W.Probe(P, FSSXRiderLightAdjust::From(Table[0]));
		TestTrue(FString::Printf(TEXT("ambient at %s: %s"), *P.ToString(), *R.Ambient.ToString()), NearV(R.Ambient, RamAmbient, 1e-4));
		TestTrue(FString::Printf(TEXT("slot 2 at %s: %s"), *P.ToString(), *R.Colour[2].ToString()), NearV(R.Colour[2], RamColour, 1e-4));
		TestTrue(FString::Printf(TEXT("dir 2 at %s: %s"), *P.ToString(), *R.Direction[2].ToString()), NearV(R.Direction[2], RamDir, 1e-4));
		TestTrue(TEXT("slots 0/1 empty"), R.Colour[0].IsNearlyZero() && R.Colour[1].IsNearlyZero()
			&& NearV(R.Direction[0], FVector(1, 0, 0), 1e-12) && NearV(R.Direction[1], FVector(1, 0, 0), 1e-12));
	}
	return true;
}

#endif
