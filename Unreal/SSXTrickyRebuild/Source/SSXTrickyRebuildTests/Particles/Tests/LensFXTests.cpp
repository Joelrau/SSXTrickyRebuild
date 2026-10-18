#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Particles/SSXLensFX.h"
#include "Particles/SSXParticleWorld.h"
#include "Sim/SimCoords.h"
#include "World/SSXWorldLighting.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "MaterialShared.h"
#include "RHIGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXLensFX;

namespace
{
	constexpr EAutomationTestFlags LensTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FCamera AxisCamera()
	{
		FCamera C;
		C.CotH = 1.0;
		C.CotV = 4.0 / 3.0;
		C.Far = 30000.0;
		return C;
	}

	FSSXLight HaloLight(const FVector& GamePos, uint32 Flags, const FVector& Rgb = FVector(255, 200, 100), double Intensity = 1.5)
	{
		FSSXLight L;
		L.Type = FSSXLight::Point;
		L.Flags = Flags;
		L.Position = GamePos;
		L.Colour = Rgb;
		L.Intensity = Intensity;
		return L;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLensHaloDataTest, "SSX.Particles.LensHaloData", LensTestFlags)

bool FSSXLensHaloDataTest::RunTest(const FString& Parameters)
{
	// 0x2006E8
	struct FWant { const TCHAR* Stem; int32 Halos; };
	const FWant Want[] = { { TEXT("alaska"), 0 }, { TEXT("aloha"), 42 }, { TEXT("elysium"), 33 }, { TEXT("gari"), 0 },
		{ TEXT("megaple"), 60 }, { TEXT("merquer"), 182 }, { TEXT("mesa"), 113 }, { TEXT("pipe"), 0 }, { TEXT("snow"), 122 },
		{ TEXT("ssxfe"), 0 }, { TEXT("trick"), 15 }, { TEXT("untrack"), 9 } };
	int32 Loaded = 0;
	for (const FWant& W : Want)
	{
		FSSXLightWorld LW;
		FString Err;
		if (!LW.Load(FSSXLightWorld::DefaultPath(W.Stem), &Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s: %s (tools/export_world_lighting.py)"), W.Stem, *Err));
			continue;
		}
		++Loaded;
		const TArray<int32>& Seq = LW.GetHaloSequence();
		TSet<int32> Unique(Seq);
		int32 Flagged = 0, BadClass = 0;
		for (int32 I = 0; I < LW.GetLights().Num(); ++I)
		{
			const uint32 F = LW.GetLights()[I].Flags & 0x70;
			if (F)
			{
				++Flagged;
				FHaloClass C;
				BadClass += HaloClass(F, C) ? 0 : 1;
				TestTrue(FString::Printf(TEXT("%s: halo light %d listed"), W.Stem, I), Unique.Contains(I));
			}
		}
		TestEqual(FString::Printf(TEXT("%s: halo list entries"), W.Stem), Seq.Num(), W.Halos);
		TestEqual(FString::Printf(TEXT("%s: listed once each"), W.Stem), Unique.Num(), Seq.Num());
		TestEqual(FString::Printf(TEXT("%s: = lights with flags & 0x70"), W.Stem), Flagged, Seq.Num());
		TestEqual(FString::Printf(TEXT("%s: size classes 0x10 / 0x20 / 0x40 only"), W.Stem), BadClass, 0);
	}
	if (Loaded == 0)
	{
		SSXTest::Skip(*this, TEXT("no lights.bin: data checks skipped"));
	}
	TArray<FSSXTrackPresentation> Table;
	if (FSSXTrackPresentation::LoadTable(FSSXTrackPresentation::DefaultTablePath(), Table) && Table.Num() >= 11)
	{
		const FSSXTrackPresentation& Snow = Table[1];
		TestFalse(TEXT("Snowdream: no sun flare (0x00)"), Snow.bFlareOn);
		TestEqual(TEXT("Snowdream halo range 0x5C"), Snow.HaloRange, 20000.0, 1e-3);
		TestEqual(TEXT("Snowdream ring base 0x34"), Snow.HaloRingBase, 3.00973, 1e-5);
		TestEqual(TEXT("Snowdream streak base 0x38"), Snow.HaloStreakBase, 0.56724, 1e-5);
		TestEqual(TEXT("Snowdream streak alpha 0x44"), Snow.HaloAlpha.Z, 0.81881, 1e-5);
		int32 SunTracks = 0;
		for (const FSSXTrackPresentation& T : Table) { SunTracks += T.bFlareOn ? 1 : 0; }
		TestEqual(TEXT("sun flare on Elysium, Mesa, Aloha, Pipedream only"), SunTracks, 4);
		TestTrue(TEXT("Mesa sun on"), Table[3].bFlareOn);
		TestEqual(TEXT("Mesa sun colour 0x18"), Table[3].SunRGB, FIntVector(153, 110, 10));
		TestEqual(TEXT("Mesa glare colour 0x04"), Table[3].GlareRGB, FIntVector(248, 125, 23));
	}
	else
	{
		SSXTest::Skip(*this, TEXT("world_track_table.json missing: table checks skipped"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLensHaloWalkTest, "SSX.Particles.LensHaloWalk", LensTestFlags)

bool FSSXLensHaloWalkTest::RunTest(const FString& Parameters)
{
	// 0x2006E8 / FUN_0025ECB8
	FSSXLightWorld LW;
	SSXFog::FFogData Fog;
	FString Err;
	if (!LW.Load(FSSXLightWorld::DefaultPath(TEXT("merquer")), &Err) || !Fog.Load(SSXFog::FFogData::DefaultPath(TEXT("merquer")), &Err)
		|| !Fog.HasGrid())
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("merquer lights / fog grid: %s: skipped"), *Err));
		return true;
	}
	TArray<TPair<int32, uint16>> All;
	for (const SSXFog::FCell& C : Fog.Cells) { All.Add({ C.Index, uint16(0xFFFF) }); }
	All.Sort([](const TPair<int32, uint16>& A, const TPair<int32, uint16>& B) { return A.Key < B.Key; });
	TArray<int32> Seq;
	LW.VisibleHaloSequence(All, Seq);
	TestEqual(TEXT("all cells visible: the whole halo list"), Seq, LW.GetHaloSequence());
	SSXFog::FClipView Cv;
	const SSXFog::FCell& Mid = Fog.Cells[Fog.Cells.Num() / 2];
	Cv.Pos = (Mid.Lo + Mid.Hi) * 0.5;
	Cv.Far = 30000.0;
	TArray<SSXFog::FFogData::FVisible> Vis;
	Fog.VisibleCells(Cv, 3, Vis);
	TArray<TPair<int32, uint16>> Cells;
	for (const SSXFog::FFogData::FVisible& V : Vis) { Cells.Add({ Fog.Cells[V.Cell].Index, V.Vis }); }
	LW.VisibleHaloSequence(Cells, Seq);
	const TSet<int32> Whole(LW.GetHaloSequence());
	int32 Outside = 0;
	for (int32 L : Seq) { Outside += Whole.Contains(L) ? 0 : 1; }
	AddInfo(FString::Printf(TEXT("visible cells %d, halos walked %d of %d"), Vis.Num(), Seq.Num(), LW.GetHaloSequence().Num()));
	TestEqual(TEXT("walked halos come from the lists"), Outside, 0);
	TestTrue(TEXT("a partial view walks fewer halos"), Seq.Num() < LW.GetHaloSequence().Num());
	for (TPair<int32, uint16>& C : Cells) { C.Value = 0; }
	LW.VisibleHaloSequence(Cells, Seq);
	TestEqual(TEXT("masks 0: nothing walked"), Seq.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLensFormulasTest, "SSX.Particles.LensFormulas", LensTestFlags)

bool FSSXLensFormulasTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("k4 at ndc (0.3, -0.4)"), CentreWeight(FVector2D(0.3, -0.4)), 0.1746364691879534, 1e-9);
	const FVector S = HaloSizes(200.07841, FVector2D(0.3, -0.4), 3.00973, 0.56724);
	TestEqual(TEXT("core = radius"), S.X, 200.07841, 1e-9);
	TestEqual(TEXT("ring"), S.Y, 637.4577600919794, 1e-6);
	TestEqual(TEXT("streak"), S.Z, 157.24748559770356, 1e-6);
	TestEqual(TEXT("streak angle -x pi/2"), StreakAngle(0.5), -UE_DOUBLE_PI * 0.25, 1e-12);
	TestEqual(TEXT("fade 1 up to range / 2"), DistanceFade(9000.0, 20000.0), 1.0, 1e-12);
	TestEqual(TEXT("fade at 3/4 range"), DistanceFade(15000.0, 20000.0), 0.5, 1e-12);
	TestEqual(TEXT("fade at range"), DistanceFade(20000.0, 20000.0), 0.0, 1e-12);
	const FVector C = HaloColour(FVector(255, 200, 100), 1.5);
	TestEqual(TEXT("colour normalised"), C.X, 0.7518722555859098, 1e-9);
	TestEqual(TEXT("colour g"), C.Y, 0.5897037298713017, 1e-9);
	TestEqual(TEXT("zero colour unchanged"), HaloColour(FVector::ZeroVector, 1.0), FVector::ZeroVector);
	const FVector2D Mesa = SunDistanceAndSize(31363.26562, 9022.33496, 30000.0);
	TestEqual(TEXT("sun at far - 1"), Mesa.X, 29999.0, 1e-9);
	TestEqual(TEXT("sun radius x (far - 1) / D"), Mesa.Y, 8629.873870418729, 1e-6);
	const FVector2D Ely = SunDistanceAndSize(27121.60742, 3390.86987, 30000.0);
	TestEqual(TEXT("sun nearer than far: unchanged"), Ely.Y, 3390.86987, 1e-9);
	const FVector D = SunDirectionGame(63.46582, 13.01621);
	TestEqual(TEXT("sun dir x"), D.X, 0.43525345910718, 1e-9);
	TestEqual(TEXT("sun dir y"), D.Y, 0.8716807641090917, 1e-9);
	TestEqual(TEXT("sun dir z"), D.Z, 0.22522671204238692, 1e-9);
	FHaloClass K;
	TestTrue(TEXT("class 0x10"), HaloClass(0x110, K) && K.Radius == 100.73963 && K.PullIn == 300.0);
	TestTrue(TEXT("class 0x20"), HaloClass(0x20, K) && K.Radius == 200.07841 && K.PullIn == 500.0);
	TestTrue(TEXT("class 0x40"), HaloClass(0x40, K) && K.Radius == 500.0 && K.PullIn == 800.0);
	TestFalse(TEXT("class 0x30 undefined"), HaloClass(0x30, K));
	TestEqual(TEXT("core quadrant TL"), QuadrantUV(0), FVector4f(0.f, 0.f, 127.f / 256.f, 127.f / 256.f));
	TestEqual(TEXT("ring / sun quadrant TR"), QuadrantUV(1), FVector4f(0.5f, 0.f, 255.f / 256.f, 127.f / 256.f));
	TestEqual(TEXT("streak quadrant BL"), QuadrantUV(2), FVector4f(0.f, 0.5f, 127.f / 256.f, 255.f / 256.f));
	TestEqual(TEXT("GS byte truncates"), int32(GsByte(0.81881)), 104);
	TestEqual(TEXT("GS byte 1.0 = 128"), int32(GsByte(1.0)), 128);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLensOcclusionTest, "SSX.Particles.LensOcclusion", LensTestFlags)

bool FSSXLensOcclusionTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("all visible"), Visibility(256, 256, 256), 1.0, 1e-12);
	TestEqual(TEXT("half covered"), Visibility(128, 256, 256), 0.5, 1e-12);
	TestEqual(TEXT("halo block quarter"), Visibility(32, 128, 128), 0.25, 1e-12);
	TestEqual(TEXT("partly outside"), Visibility(200, 200, 256), 0.5625, 1e-12);
	TestEqual(TEXT("partly outside, clamped"), Visibility(100, 150, 256), 0.0, 1e-12);
	TestEqual(TEXT("sun block centred"), BlockOrigin(FVector2D(256.0, 224.0), SunBlockW, SunBlockH), FIntPoint(248, 216));
	TestEqual(TEXT("clamped at 0"), BlockOrigin(FVector2D(3.0, 2.0), SunBlockW, SunBlockH), FIntPoint(0, 0));
	TestEqual(TEXT("halo clamped at the far edges"), BlockOrigin(FVector2D(510.0, 447.0), HaloBlockW, HaloBlockH), FIntPoint(496, 440));
	TestEqual(TEXT("truncation"), BlockOrigin(FVector2D(8.9, 100.0), HaloBlockW, HaloBlockH), FIntPoint(0, 96));
	TestEqual(TEXT("ndc -> PS2 pixel"), Ps2Pixel(FVector2D(0.0, 0.0)), FVector2D(256.0, 224.0));
	// 0x1CC088
	TArray<FHaloPick> Slots;
	for (int32 I = 0; I < 10; ++I) { InsertHalo(Slots, I, 100.0 * (I + 1)); }
	InsertHalo(Slots, 10, 2000.0);
	TestEqual(TEXT("farther light ignored when full"), Slots[9].Light, 9);
	InsertHalo(Slots, 11, 50.0);
	TestEqual(TEXT("nearer light replaces the farthest"), Slots[9].Light, 11);
	TestEqual(TEXT("still 10"), Slots.Num(), MaxHalos);
	Slots.Reset();
	for (int32 I = 0; I < 10; ++I) { InsertHalo(Slots, I, I < 2 ? 900.0 : 100.0); }
	InsertHalo(Slots, 20, 500.0);
	TestEqual(TEXT("first of equal maxima replaced"), Slots[0].Light, 20);
	TestEqual(TEXT("second kept"), Slots[1].Light, 1);
	// 0x2006E8
	const FCamera Cam = AxisCamera();
	TArray<FSSXLight> Lights = { HaloLight(FVector(1000, 0, 0), 0x20), HaloLight(FVector(-1000, 0, 0), 0x20),
		HaloLight(FVector(20000, 0, 0), 0x20), HaloLight(FVector(1000, 0, 0), 0x100), HaloLight(FVector(1000, 1500, 0), 0x10),
		HaloLight(FVector(19999, 0, 0), 0x40) };
	TArray<FHaloPick> Out;
	CollectHalos({ 0, 1, 2, 3, 4, 5 }, Lights, Cam, 20000.0, Out);
	TestEqual(TEXT("collected: in front, in frustum, depth < range, flagged"), Out.Num(), 2);
	if (Out.Num() == 2)
	{
		TestEqual(TEXT("first"), Out[0].Light, 0);
		TestEqual(TEXT("depth just inside the range"), Out[1].Light, 5);
		TestEqual(TEXT("view depth"), Out[1].Depth, 19999.0, 1e-9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLensGlareFanTest, "SSX.Particles.LensGlareFan", LensTestFlags)

bool FSSXLensGlareFanTest::RunTest(const FString& Parameters)
{
	TArray<FFanPoint> P;
	GlareFan(FVector2D(0.0, 0.0), P);
	TestEqual(TEXT("points (sun at the centre)"), P.Num(), 42);
	if (P.Num() == 42)
	{
		TestEqual(TEXT("ray 0 hits the right edge"), P[0].Ndc, FVector2D(1.0, 0.0));
		TestEqual(TEXT("ray 0 weight"), P[0].Weight, 0.2857, 1e-12);
		TestEqual(TEXT("ray 0 falloff (3 - 1) / 3"), P[0].Falloff, 2.0 / 3.0, 1e-12);
		TestTrue(TEXT("corner (1, 1) after ray 4"), P[5].Ndc.Equals(FVector2D(1.0, 1.0), 1e-9));
		TestEqual(TEXT("corner weight = preceding ray"), P[5].Weight, 0.2857, 1e-12);
		TestEqual(TEXT("corner falloff (3 - 2) / 3"), P[5].Falloff, 1.0 / 3.0, 1e-9);
		TestEqual(TEXT("closing point = first"), P.Last().Ndc, P[0].Ndc);
		bool bEdge = true;
		for (const FFanPoint& F : P) { bEdge &= FMath::IsNearlyEqual(FMath::Max(FMath::Abs(F.Ndc.X), FMath::Abs(F.Ndc.Y)), 1.0, 1e-9); }
		TestTrue(TEXT("every point on the screen edge"), bEdge);
	}
	GlareFan(FVector2D(-0.9, -0.9), P);
	bool bClamp = false;
	for (const FFanPoint& F : P) { bClamp |= FVector2D::DistSquared(F.Ndc, FVector2D(-0.9, -0.9)) >= 3.0 && F.Falloff == 0.0; }
	TestTrue(TEXT("falloff clamped to 0 beyond d^2 = 3"), bClamp);
	TestEqual(TEXT("points (off centre): 37 rays + 3 corners + closing"), P.Num(), 41);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLensFrameTest, "SSX.Particles.LensFrame", LensTestFlags)

bool FSSXLensFrameTest::RunTest(const FString& Parameters)
{
	FParams P;
	P.RingBase = 3.00973;
	P.StreakBase = 0.56724;
	P.HaloAlpha = FVector(0.04599, 0.25928, 0.81881);
	P.HaloRange = 20000.0;
	const FCamera Cam = AxisCamera();
	const TArray<FSSXLight> Lights = { HaloLight(FVector(1000, 0, 0), 0x20), HaloLight(FVector(15000, 0, 0), 0x10) };
	FLensFrame F;
	BuildFrame(P, Lights, { 0, 1 }, Cam, F);
	TestEqual(TEXT("halos"), F.Halos.Num(), 2);
	TestEqual(TEXT("core + ring per halo"), F.Sprites.Num(), 4);
	TestEqual(TEXT("one streak per halo"), F.Streaks.Num(), 2);
	TestFalse(TEXT("no sun when 0x00 = 0"), F.bSun);
	if (F.Sprites.Num() == 4 && F.Streaks.Num() == 2)
	{
		const double K4 = CentreWeight(FVector2D(0, 0));
		TestEqual(TEXT("core radius"), F.Sprites[0].Half, 200.07841, 1e-9);
		TestEqual(TEXT("ring radius"), F.Sprites[1].Half, 200.07841 * (3.00973 + K4 * 1.0095813), 1e-6);
		TestEqual(TEXT("colour = normalised x 128 (truncated)"), F.Sprites[0].Colour, FColor(96, 75, 37, 5));
		TestEqual(TEXT("ring alpha 0x40"), int32(F.Sprites[1].Colour.A), 33);
		TestEqual(TEXT("streak alpha 0x44"), int32(F.Streaks[0].Colour.A), 104);
		TestEqual(TEXT("occlusion point = the light"), F.Sprites[0].Occ.Point, FVector(1000, 0, 0));
		TestEqual(TEXT("pull-in of class 0x20"), F.Sprites[0].Occ.PullIn, 500.0);
		TestEqual(TEXT("halo block 16 x 8"), FIntPoint(F.Sprites[0].Occ.BlockW, F.Sprites[0].Occ.BlockH), FIntPoint(16, 8));
		const double S = 200.07841 * (0.56724 + K4 * 1.2522545);
		TestTrue(TEXT("streak corner 0"), F.Streaks[0].Corner[0].Equals(FVector(1000.0, -S, S), 1e-6));
		TestTrue(TEXT("streak corner 3"), F.Streaks[0].Corner[3].Equals(FVector(1000.0, S, -S), 1e-6));
		TestEqual(TEXT("streak quadrant BL"), F.Streaks[0].UV, QuadrantUV(2));
		TestEqual(TEXT("distance fade in the alpha"), int32(F.Streaks[1].Colour.A), 52);
		TestEqual(TEXT("class 0x10 core"), F.Sprites[2].Half, 100.73963, 1e-9);
	}
	BuildFrame(P, { HaloLight(FVector(1000, 500, 0), 0x20) }, { 0 }, Cam, F);
	if (F.Streaks.Num() == 1)
	{
		const FVector L = SSXSim::GameToUnreal(FVector(1000, 500, 0));
		const FVector D = (F.Streaks[0].Corner[1] - F.Streaks[0].Corner[0]).GetSafeNormal();
		TestTrue(TEXT("streak A axis rotated by -x pi/2"), D.Equals((Cam.Right + Cam.Up).GetSafeNormal(), 1e-9));
		TestTrue(TEXT("streak centred on the light"), ((F.Streaks[0].Corner[0] + F.Streaks[0].Corner[3]) * 0.5).Equals(L, 1e-6));
	}
	FParams M;
	M.bSunOn = true;
	M.GlareRGB = FIntVector(248, 125, 23);
	M.GlareAlpha = 0.70545;
	M.SunRadius = 9022.33496;
	M.SunRGB = FIntVector(153, 110, 10);
	M.SunAlpha = 0.72135;
	M.AzimuthDeg = 25.07705;
	M.ElevationDeg = 2.48734;
	M.SunDist = 31363.26562;
	FCamera SC = AxisCamera();
	SC.Fwd = SSXSim::GameToUnreal(SunDirectionGame(M.AzimuthDeg, M.ElevationDeg));
	SC.Right = FVector::CrossProduct(FVector::UpVector, SC.Fwd).GetSafeNormal();
	SC.Up = FVector::CrossProduct(SC.Fwd, SC.Right);
	BuildFrame(M, {}, {}, SC, F);
	TestTrue(TEXT("sun on screen"), F.bSun);
	TestEqual(TEXT("one sun sprite"), F.Sprites.Num(), 1);
	TestEqual(TEXT("fan triangles"), F.Fan.Num(), 41);
	if (F.Sprites.Num() == 1 && F.Fan.Num() == 41)
	{
		TestEqual(TEXT("sun radius scaled to far - 1"), F.Sprites[0].Half, 8629.873870418729, 1e-6);
		TestEqual(TEXT("sun colour / alpha bytes"), F.Sprites[0].Colour, FColor(76, 55, 5, 92));
		TestEqual(TEXT("sun block 16 x 16, no pull-in"), FIntPoint(F.Sprites[0].Occ.BlockW, F.Sprites[0].Occ.BlockH), FIntPoint(16, 16));
		TestEqual(TEXT("fan colour"), FColor(F.Fan[0].C[0].R, F.Fan[0].C[0].G, F.Fan[0].C[0].B), FColor(124, 62, 11));
		TestEqual(TEXT("fan centre alpha int(0x10 x w0 x 128)"), int32(F.Fan[0].C[0].A), int32(0.70545 * 0.2857 * 128.0));
		TestEqual(TEXT("fan edge alpha x falloff"), int32(F.Fan[0].C[1].A), int32(0.70545 * 0.2857 * 128.0 * (2.0 / 3.0)));
	}
	SC.Fwd = -SC.Fwd;
	SC.Right = -SC.Right;
	BuildFrame(M, {}, {}, SC, F);
	TestFalse(TEXT("sun behind the camera: no sprite, no fan"), F.bSun || F.Fan.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXLensMaterialsTest, "SSX.Particles.LensMaterials", LensTestFlags)

bool FSSXLensMaterialsTest::RunTest(const FString& Parameters)
{
#if WITH_EDITORONLY_DATA
	for (const TCHAR* Path : { USSXParticleWorld::LensSpriteMaterialPath, USSXParticleWorld::LensFanMaterialPath })
	{
		UMaterial* M = LoadObject<UMaterial>(nullptr, Path);
		if (!M)
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s missing (Scripts/make_particle_materials.py): skipped"), Path));
			continue;
		}
		const FString N = M->GetName();
		TestEqual(N + TEXT(": additive"), int32(M->GetBlendMode()), int32(BLEND_Additive));
		TestTrue(N + TEXT(": no depth test"), M->bDisableDepthTest);
		TestTrue(N + TEXT(": unlit"), M->GetShadingModels().IsUnlit());
		bool bOcc = false, bGs = false;
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			if (const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E))
			{
				bOcc |= C->Code.Contains(TEXT("CalcSceneDepth")) && C->Code.Contains(TEXT("512.0")) && C->Code.Contains(TEXT("448.0"))
					&& C->Code.Contains(TEXT("VP.z - Pull"));
				bGs |= C->Code.Contains(TEXT("* V"));
			}
		}
		TestTrue(N + TEXT(": z-buffer block occlusion node"), bOcc);
		TestTrue(N + TEXT(": colour x V"), bGs);
		M->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		if (const FMaterialResource* R = M->GetMaterialResource(GMaxRHIShaderPlatform))
		{
			for (const FString& E : R->GetCompileErrors()) { AddError(N + TEXT(": ") + E); }
		}
		else
		{
			AddWarning(N + TEXT(": no material resource for the shader platform: compile not checked"));
		}
	}
#endif
	return true;
}

#endif
