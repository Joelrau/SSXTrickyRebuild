#include "Effects/TrackEffects.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Particles/SSXFogVolumes.h"
#include "World/PropControllers.h"
#include "World/PropNodes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags R25TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
	const TCHAR* const Stems[] = { TEXT("gari"), TEXT("snow"), TEXT("elysium"), TEXT("mesa"), TEXT("merquer"), TEXT("aloha"),
		TEXT("pipe"), TEXT("untrack"), TEXT("megaple"), TEXT("trick"), TEXT("alaska"), TEXT("ssxfe") };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFogVolumesTest, "SSX.World.FogVolumes", R25TestFlags)
bool FSSXWorldFogVolumesTest::RunTest(const FString&)
{
	using namespace SSXFog;
	TestTrue(TEXT("point alpha 128 at d 1000, w 3000"), FMath::IsNearlyEqual(128.0 * FarFactor(1000.0) * NearFactor(3000.0), 128.0));
	TestTrue(TEXT("not drawn at d 30000"), FarFactor(30000.0) < 0.0);
	TestTrue(TEXT("near factor 0.25 at w 1551"), FMath::IsNearlyEqual(NearFactor(1551.0), 0.25, 1e-3));
	TestTrue(TEXT("point skipped at w 400"), NearFactor(400.0) < 0.0);
	TestTrue(TEXT("far factor 1 below 12500.37, 0.5 halfway"), FarFactor(12500.0) == 1.0 && FMath::IsNearlyEqual(FarFactor(16250.278), 0.5, 1e-4));
	TestTrue(TEXT("near factor 1 from 2601.95"), NearFactor(2602.0) == 1.0);

	const TMap<FString, int32> Expect = { { TEXT("alaska"), 27 }, { TEXT("aloha"), 11 }, { TEXT("elysium"), 59 }, { TEXT("gari"), 10 },
		{ TEXT("merquer"), 9 }, { TEXT("untrack"), 4 }, { TEXT("ssxfe"), 59 } };
	int32 Files = 0;
	for (const TCHAR* Stem : Stems)
	{
		FFogData D;
		FString Err;
		if (!D.Load(FFogData::DefaultPath(Stem), &Err))
		{
			AddWarning(Err);
			continue;
		}
		++Files;
		const int32* E = Expect.Find(Stem);
		TestEqual(FString::Printf(TEXT("%s fog instances"), Stem), D.Instances.Num(), E ? *E : 0);
	}
	if (Files == 0)
	{
		SSXTest::Skip(*this, TEXT("no .fog.bin (tools/export_fog_volumes.py): data checks skipped"));
		return true;
	}
	FFogData Fe;
	if (Fe.Load(FFogData::DefaultPath(TEXT("ssxfe"))))
	{
		TestEqual(TEXT("FE: 26 fog models"), Fe.Models.Num(), 26);
		int32 Char = 0, Venue = 0, Mode = 0, Amb = 0;
		double RMin = 1e9, RMax = 0.0;
		for (const FInstance& I : Fe.Instances)
		{
			Char += FMath::Abs(I.T.Y) < 10000.0;
			Venue += FMath::Abs(I.T.Y + 50000.0) < 10000.0;
			Mode += FMath::Abs(I.T.Y - 30000.0) < 10000.0;
			Amb += I.Model >= 16;
			for (const FPoint& P : Fe.Models[I.Model]) { RMin = FMath::Min(RMin, P.Radius); RMax = FMath::Max(RMax, P.Radius); }
		}
		TestEqual(TEXT("FE character room: 31 instances"), Char, 31);
		TestEqual(TEXT("FE venue room: 20 instances (ceiling haze)"), Venue, 20);
		TestEqual(TEXT("FE mode-select room: 8 instances"), Mode, 8);
		TestEqual(TEXT("FE fog_amb models 16-25: 28 instances"), Amb, 28);
		TestTrue(TEXT("FE radii 260..4235 cm"), FMath::IsNearlyEqual(RMin, 260.0, 1.0) && FMath::IsNearlyEqual(RMax, 4235.4, 1.0));
		TArray<FPuff> Out;
		Fe.Gather(FVector(0.03, -50000.2, 288.0), FVector(0, 0, 1), Out);
		TestTrue(TEXT("venue room: haze puffs drawn"), Out.Num() >= 20);
		for (const FPuff& P : Out)
		{
			TestTrue(TEXT("alpha in (0, 128]"), P.Rgba.W > 0.0 && P.Rgba.W <= 128.0);
		}
		Out.Reset();
		Fe.Gather(FVector(0.03, -50000.2, 288.0), FVector(0, 0, -1), Out);
		int32 HazeDown = 0;
		for (const FPuff& P : Out) { HazeDown += P.Pos.Z > 5000.0; }
		TestEqual(TEXT("haze behind the camera skipped (w <= 500)"), HazeDown, 0);
	}
	FFogData T;
	T.Models.AddDefaulted();
	for (double Y : { 1000.0, 5000.0, 3000.0, 400.0 })
	{
		FPoint P; P.Pos = FVector(0, Y, 0); P.Radius = 100.0; P.Rgb = FVector(0.5, 0.5, 0.5);
		T.Models[0].Add(P);
	}
	T.Instances.AddDefaulted();
	TArray<FPuff> Out;
	T.Gather(FVector::ZeroVector, FVector(0, 1, 0), Out);
	TestEqual(TEXT("w 400 skipped: 3 puffs"), Out.Num(), 3);
	if (Out.Num() == 3)
	{
		TestTrue(TEXT("back to front"), Out[0].Pos.Y == 5000.0 && Out[1].Pos.Y == 3000.0 && Out[2].Pos.Y == 1000.0);
		TestTrue(TEXT("near fade at w 1000: trunc(128 ((500 / 2101.95)^2)) = 7"), Out[2].Rgba.W == 7.0);
		TestTrue(TEXT("colour = rgb x 128"), FMath::IsNearlyEqual(Out[0].Rgba.X, 64.0));
		TestTrue(TEXT("half-size = radius"), Out[0].Half == 100.0);
	}
	Out.Reset();
	T.Gather(FVector(0, -16250.278 + 0.0, 0), FVector(0, 1, 0), Out);
	TestTrue(TEXT("far fade halfway: alpha 64"), Out.Num() == 4 && FMath::Abs(Out[3].Rgba.W - 64.0) <= 1.0);

	// 0x1C9D58
	{
		FFogData C = T;
		C.Instances[0].BoxMin = FVector(-1000, 14000, -10);
		C.Instances[0].BoxMax = FVector(1000, 16000, 10);
		Out.Reset();
		C.Gather(FVector(0, -1250.278, 0), FVector(0, 1, 0), Out);
		TestTrue(TEXT("far fade from the bbox centre"), Out.Num() == 4 && FMath::Abs(Out[0].Rgba.W - 64.0) <= 1.0);
		Out.Reset();
		C.Gather(FVector(0, -6000.0, 0), FVector(0, 1, 0), Out);
		TestEqual(TEXT("bbox centre beyond 20000.18: instance not drawn"), Out.Num(), 0);
	}
	// 0x1CA5A0
	{
		FFogData Q;
		Q.Models.AddDefaulted();
		for (double Y : { 500.9, 501.5, 3000.0 })
		{
			FPoint P; P.Pos = FVector(0, Y, 0); P.Radius = 50.0; P.Rgb = FVector(0.49, 0.79, 0.92);
			Q.Models[0].Add(P);
		}
		Q.Instances.AddDefaulted();
		Out.Reset();
		Q.Gather(FVector::ZeroVector, FVector(0, 1, 0), Out);
		TestEqual(TEXT("trunc(w) 500 skipped, 501 kept (alpha trunc(128 x (1 / 2101.95)^2) = 0: no draw)"), Out.Num(), 1);
		TestTrue(TEXT("RGBAQ = trunc(x 128): (62, 101, 117, 128)"), Out.Num() == 1 && Out[0].Rgba == FVector4(62, 101, 117, 128));
	}
	// 0x2FB5C8 / 0x1CAD28
	{
		auto Order = [](std::initializer_list<int32> Ws)
		{
			TArray<FSortKey> K;
			for (int32 W : Ws) { K.Add({ K.Num(), W }); }
			EeQsortFogKeys(K);
			TArray<int32> R;
			for (const FSortKey& E : K) { R.Add(E.W); }
			return R;
		};
		TestTrue(TEXT("qsort < 7: insertion sort, far first"), Order({ 1000, 5000, 3000 }) == TArray<int32>({ 5000, 3000, 1000 }));
		TestTrue(TEXT("qsort 10 keys: library order"), Order({ 100, 900, 300, 700, 500, 200, 800, 600, 400, 1000 })
			== TArray<int32>({ 200, 900, 300, 700, 500, 1000, 800, 600, 400, 100 }));
		TestTrue(TEXT("qsort 8 keys: library order"), Order({ 5000, 4000, 3000, 2000, 1000, 600, 700, 800 })
			== TArray<int32>({ 800, 4000, 3000, 2000, 1000, 5000, 700, 600 }));
		TArray<int32> Asc;
		for (int32 W = 1000; W <= 20000; W += 1000) { Asc.Add(W); }
		TArray<FSortKey> K;
		for (int32 W : Asc) { K.Add({ K.Num(), W }); }
		EeQsortFogKeys(K);
		TArray<int32> R;
		for (const FSortKey& E : K) { R.Add(E.W); }
		TestTrue(TEXT("qsort 20 ascending keys: library order"), R == TArray<int32>({ 11000, 12000, 13000, 14000, 15000, 16000, 17000, 18000,
			19000, 20000, 6000, 7000, 8000, 9000, 10000, 5000, 4000, 3000, 2000, 1000 }));
	}
	{
		FFogData I;
		I.Models.AddDefaulted();
		FPoint P; P.Radius = 10.0;
		I.Models[0].Add(P);
		for (double Y : { 3000.0, 9000.0, 3010.0 })
		{
			FInstance& In = I.Instances.AddDefaulted_GetRef();
			In.T = FVector(0, Y, 0);
			In.BoxMin = In.BoxMax = In.T;
		}
		Out.Reset();
		I.Gather(FVector::ZeroVector, FVector(0, 1, 0), Out);
		TestTrue(TEXT("depth keys 76 / 230 / 76"), DepthKey(3000.0) == 76 && DepthKey(9000.0) == 230 && DepthKey(3010.0) == 76 && DepthKey(1.0e6) == 1023);
		TestTrue(TEXT("instances drawn far to near, ties in order"), Out.Num() == 3 && Out[0].Instance == 1 && Out[1].Instance == 0 && Out[2].Instance == 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldFlipbookBindingTest, "SSX.World.FlipbookBinding", R25TestFlags)
bool FSSXWorldFlipbookBindingTest::RunTest(const FString&)
{
	using namespace SSXProps;
	{
		FTexFlip F;
		F.Fps = 3.5;
		F.Create({ 10, 11, 12 });
		TestEqual(TEXT("start frame 0"), F.Frame, 0);
		int32 Steps = 0, Prev = F.Frame;
		TArray<int32> Seq;
		for (int32 K = 0; K < 600; ++K)
		{
			F.Tick();
			if (F.Frame != Prev) { ++Steps; Seq.Add(F.Frame); Prev = F.Frame; }
		}
		TestTrue(TEXT("3.5 fps: 35 (+-1) steps in 10 s"), FMath::Abs(Steps - 35) <= 1);
		TestTrue(TEXT("forward loop 1, 2, 0"), Seq.Num() >= 3 && Seq[0] == 1 && Seq[1] == 2 && Seq[2] == 0);
		TestEqual(TEXT("texture shown = flipbook[frame]"), F.TextureIndex(), 10 + F.Frame);
	}
	for (int32 N : { 2, 3, 5 })
	{
		for (double R : { 0.0, 0.5, 0.999 })
		{
			FTexFlip B;
			B.FrameCount = N;
			B.Rng = [R]() { return R; };
			B.SetFrame(-1);
			TestTrue(FString::Printf(TEXT("crack frame in 1..%d"), N - 1), B.Frame >= 1 && B.Frame <= N - 1);
		}
	}
	int32 Books = 0, TexFlips = 0, Bound = 0, At35 = 0, Cracked = 0, CrackedBound = 0, Loaded = 0, Match = 0, Pbds = 0;
	for (const TCHAR* Stem : Stems)
	{
		SSXPropNodes::FPropAnimData A;
		SSXEffects::FEffectData E;
		FString Err;
		if (!A.Load(SSXPropNodes::FPropAnimData::DefaultPath(Stem), &Err) || !E.Load(SSXEffects::FEffectData::DefaultPath(Stem), &Err))
		{
			AddWarning(Err);
			continue;
		}
		++Loaded;
		Books += A.Flipbooks.Num();
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *FPaths::Combine(FPaths::ProjectDir(), TEXT("../../unpacked/MODELS")), *(FString(Stem) + TEXT(".pbd")), true, false);
		TArray<uint8> Bytes;
		TArray<TArray<int32>> Parsed;
		if (Found.Num() && FFileHelper::LoadFileToArray(Bytes, *Found[0]) && ParsePbdFlipbooks(Bytes, Parsed))
		{
			++Pbds;
			bool bSame = Parsed.Num() == A.Flipbooks.Num();
			for (int32 K = 0; bSame && K < Parsed.Num(); ++K)
			{
				TArray<int32> Low;
				for (int32 V : Parsed[K]) { Low.Add(V & 0xFFFF); }
				bSame = Low == A.Flipbooks[K];
			}
			Match += bSame;
			TestTrue(FString::Printf(TEXT("%s: exported flipbooks = .pbd parser"), Stem), bSame);
		}
		for (int32 I = 0; I < E.NumInstances(); ++I)
		{
			for (int32 Slot = 0; Slot < 7; ++Slot)
			{
				const int32 Fx = E.EffectFor(I, Slot);
				if (!E.Effects.IsValidIndex(Fx))
				{
					continue;
				}
				for (int32 K = 0; K < E.Effects[Fx].Y; ++K)
				{
					const SSXEffects::FRecord& R = E.Records[E.Effects[Fx].X + K];
					if (R.Type != 0)
					{
						continue;
					}
					const int32 Sub = E.I(R, 0);
					const TArray<int32>* Fb = A.FirstFlipbookOfInstance(I);
					if (Sub == SSXEffects::TypeTexFlip && Slot == 0)
					{
						++TexFlips;
						FTexFlip F;
						if (ParseTexFlipRecord(E.RecordBytes(R), R.Size, F))
						{
							At35 += FMath::IsNearlyEqual(F.Fps, 3.5, 1e-4);
						}
						Bound += Fb != nullptr;
					}
					if (Sub == SSXEffects::TypeCracked)
					{
						++Cracked;
						CrackedBound += Fb != nullptr;
					}
					if (Fb && Fb->Num())
					{
						const int32 M = A.InstanceModel[I];
						for (const SSXPropNodes::FPropAnimData::FMaterialRef& Mr : A.ModelMaterials[M])
						{
							if (A.Flipbooks.IsValidIndex(Mr.Flipbook) && Fb == &A.Flipbooks[Mr.Flipbook])
							{
								TestEqual(TEXT("FlipTextureFor = flipbook[last]"), A.FlipTextureFor(I, Mr.Texture, Fb->Num() - 1), Fb->Last());
								break;
							}
						}
					}
				}
			}
		}
	}
	if (Loaded == 0)
	{
		SSXTest::Skip(*this, TEXT("no SimData propanim / effects (tools/export_prop_anim.py, export_effects.py): data checks skipped"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("flipbooks %d; TexFlip nodes %d (3.5 fps %d), bound %d; Cracked %d, bound %d; .pbd match %d / %d"),
		Books, TexFlips, At35, Bound, Cracked, CrackedBound, Match, Pbds));
	if (Loaded == UE_ARRAY_COUNT(Stems))
	{
		TestEqual(TEXT("118 flipbooks over the 12 files"), Books, 118);
		TestEqual(TEXT("580 slot-0 TexFlip nodes"), TexFlips, 580);
		TestEqual(TEXT("440 of them at 3.5 fps (signs, checkpoint tops, barriers)"), At35, 440);
		TestEqual(TEXT("20 Megaplex glass panes (Cracked)"), Cracked, 20);
	}
	TestEqual(TEXT("every TexFlip node's instance has a flipbook material (material +0x44)"), Bound, TexFlips);
	TestEqual(TEXT("every Cracked pane has a flipbook (crack texture)"), CrackedBound, Cracked);
	return true;
}

#endif
