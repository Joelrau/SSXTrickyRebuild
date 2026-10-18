// 0x148A70 / 0x148B28 / 0x148CC8 / 0x148D28
#include "Effects/TrackEffects.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "World/PropNodes.h"
#include "World/SSXPropAnimation.h"
#include "World/SSXPropMovie.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SSXPropMovieTests
{
	using namespace SSXPropMovie;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FFound { int32 Inst; int32 Slot; int32 Rec; };
	void Scan(const SSXEffects::FEffectData& D, TArray<FFound>& Out)
	{
		TSet<TPair<int32, int32>> Seen;
		TFunction<void(int32, int32, int32)> Walk = [&](int32 E, int32 Inst, int32 Slot)
		{
			if (E < 0 || E >= D.Effects.Num() || Seen.Contains({ E, Inst }))
			{
				return;
			}
			Seen.Add({ E, Inst });
			for (int32 K = D.Effects[E].X; K < D.Effects[E].X + D.Effects[E].Y; ++K)
			{
				const SSXEffects::FRecord& R = D.Records[K];
				if (R.Type == 0 && D.I(R, 0) == SSXEffects::TypeMovie) { Out.Add({ Inst, Slot, K }); }
				else if (R.Type == 7) { Walk(D.I(R, 1), D.I(R, 0), Slot); }
			}
		};
		for (int32 Inst = 0; Inst < D.NumInstances(); ++Inst)
		{
			for (int32 Slot = 0; Slot < SSXEffects::NumSlots; ++Slot)
			{
				Seen.Reset();
				Walk(D.EffectFor(Inst, Slot), Inst, Slot);
			}
		}
	}

	FColor Px(const TArray<FColor>& Img, int32 W, int32 X, int32 Y) { return Img[Y * W + X]; }
}

using namespace SSXPropMovieTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXMovieDecodeTest, "SSX.World.Movie.Decode", SSXPropMovieTests::Flags)
bool FSSXMovieDecodeTest::RunTest(const FString&)
{
	const uint8 A[8] = { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x1B, 0x00, 0xFF };
	uint16 T[16];
	DecodeBlock(A, T);
	TestEqual(TEXT("c0 red -> 5551 R31 + alpha"), T[0], uint16(0x8000 | 31));
	TestEqual(TEXT("c1 blue -> 5551 B31 + alpha"), T[1], uint16(0x8000 | 31 << 10));
	TestEqual(TEXT("c2 = R ((0xF800 x 5) >> 3) & 0xF800 = 19, B (31 x 3) >> 3 = 11"), T[2], uint16(0x8000 | 11 << 10 | 19));
	TestEqual(TEXT("c3 = R 11, B (31 x 5) >> 3 = 19"), T[3], uint16(0x8000 | 19 << 10 | 11));
	TestTrue(TEXT("row 1 reversed"), T[4] == T[3] && T[5] == T[2] && T[6] == T[1] && T[7] == T[0]);
	TestTrue(TEXT("row 2 all c0, row 3 all c3"), T[8] == T[0] && T[11] == T[0] && T[12] == T[3] && T[15] == T[3]);
	TestTrue(TEXT("GS colour of c2: c5 << 3"), TexelColour(T[2]) == FColor(152, 0, 88, 255));

	const uint8 B[8] = { 0x00, 0x00, 0xE0, 0x07, 0xE4, 0x00, 0x00, 0x00 };
	DecodeBlock(B, T);
	TestTrue(TEXT("c0 0 -> 1 -> blue 1"), TexelColour(T[0]) == FColor(0, 0, 8, 255));
	TestTrue(TEXT("c1 green: top 5 of 6 bits"), TexelColour(T[1]) == FColor(0, 248, 0, 255));
	TestTrue(TEXT("c2 = (c0 + c1) / 2 masked: green6 31 -> green5 15"), TexelColour(T[2]) == FColor(0, 120, 0, 255));
	TestEqual(TEXT("c3 = 0: transparent black"), T[3], uint16(0));
	TestTrue(TEXT("transparent texel colour"), TexelColour(T[3]) == FColor(0, 0, 0, 0));

	uint8 Frame[16];
	FMemory::Memcpy(Frame, A, 8);
	FMemory::Memcpy(Frame + 8, B, 8);
	TArray<FColor> Img;
	DecodeFrame(Frame, 8, 4, Img);
	TestEqual(TEXT("frame size"), Img.Num(), 32);
	TestTrue(TEXT("(0,0) = A c0"), Px(Img, 8, 0, 0) == FColor(248, 0, 0, 255));
	TestTrue(TEXT("(3,1) = A row 1 texel 3 = c0"), Px(Img, 8, 3, 1) == FColor(248, 0, 0, 255));
	TestTrue(TEXT("(5,0) = B c1"), Px(Img, 8, 5, 0) == FColor(0, 248, 0, 255));
	TestTrue(TEXT("(7,0) = B c3"), Px(Img, 8, 7, 0) == FColor(0, 0, 0, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXMovieTimingTest, "SSX.World.Movie.Timing", SSXPropMovieTests::Flags)
bool FSSXMovieTimingTest::RunTest(const FString&)
{
	// 0x148B28
	static const TCHAR* Files[13] = { TEXT("jgari"), TEXT("jsnow"), TEXT("jelysium"), TEXT("jmesa"), TEXT("jmerquer"), TEXT("jaloha"),
		TEXT("jpipe"), TEXT("jumbo"), TEXT("jmegaple"), TEXT("jumbo"), TEXT("jumbo"), TEXT("jalaska"), TEXT("jumbo") };
	for (int32 K = 0; K < 13; ++K) { TestEqual(FString::Printf(TEXT("track id %d"), K), FileForTrackId(K), FString(Files[K])); }
	TestEqual(TEXT("ids clamp to 0..12"), FileForTrackId(-3), FString(TEXT("jgari")));
	TestTrue(TEXT("gari -> JGARI.SSS"), DefaultPath(TEXT("gari")).EndsWith(TEXT("VIDEO/JGARI.SSS")));
	TestTrue(TEXT("unknown stem -> no file"), DefaultPath(TEXT("nowhere")).IsEmpty());
	TestEqual(TEXT("60 / 15 fps = a frame every 4 frames"), FramesPerVideoFrame, 4);

	FStream S;
	TestFalse(TEXT("closed stream does not step"), S.Step(3));
	S.Acquire();
	TArray<int32> At;   // 60 Hz frame of each decode
	TArray<int32> Shown;
	for (int32 F = 1; F <= 16; ++F)
	{
		if (S.Step(3)) { At.Add(F); Shown.Add(S.Shown); }
		if (F == 3) { TestEqual(TEXT("blank texture before the first decode"), S.Shown, int32(INDEX_NONE)); }
	}
	TestTrue(TEXT("decodes at frames 4, 8, 12, 16"), At == TArray<int32>({ 4, 8, 12, 16 }));
	TestTrue(TEXT("3-frame file loops 0 1 2 0"), Shown == TArray<int32>({ 0, 1, 2, 0 }));

	S.Acquire();
	TestTrue(TEXT("shared: 2 users, position kept"), S.Refs == 2 && S.Next == 1 && S.Counter == 16);
	S.Release();
	TestTrue(TEXT("one user left: still open"), S.bOpen);
	S.Release();
	TestFalse(TEXT("last user: closed"), S.bOpen);
	TestFalse(TEXT("closed: no step"), S.Step(3));
	TestEqual(TEXT("closed: texture keeps its frame"), S.Shown, 0);
	S.Release();
	TestEqual(TEXT("extra release ignored"), S.Refs, 0);

	for (int32 K = 0; K < 2; ++K) { S.Step(3); }
	S.Acquire();
	int32 First = INDEX_NONE;
	for (int32 F = 1; F <= 4; ++F)
	{
		if (S.Step(3)) { First = F; }
		if (F < 4) { TestEqual(TEXT("reopen: old frame until the first decode"), S.Shown, 0); }
	}
	TestTrue(TEXT("reopen: frame 0 at the 4th frame"), First == 4 && S.Shown == 0 && S.Next == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXMovieTrackDataTest, "SSX.World.Movie.TrackData", SSXPropMovieTests::Flags)
bool FSSXMovieTrackDataTest::RunTest(const FString&)
{
	struct FExpect { const TCHAR* Stem; int32 Movies; int32 Frames; };
	static const FExpect Expect[] = {
		{ TEXT("gari"), 8, 264 }, { TEXT("snow"), 1, 254 }, { TEXT("elysium"), 1, 255 }, { TEXT("mesa"), 1, 261 },
		{ TEXT("merquer"), 1, 254 }, { TEXT("aloha"), 1, 253 }, { TEXT("megaple"), 1, 255 }, { TEXT("pipe"), 0, 255 },
		{ TEXT("alaska"), 0, -1 }, { TEXT("untrack"), 0, -1 }, { TEXT("trick"), 0, -1 }, { TEXT("ssxfe"), 0, -1 } };
	int32 Total = 0;
	for (const FExpect& E : Expect)
	{
		SSXEffects::FEffectData D;
		if (!D.Load(SSXEffects::FEffectData::DefaultPath(E.Stem)))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s.effects.bin missing"), E.Stem));
			continue;
		}
		TArray<FFound> F;
		Scan(D, F);
		TSet<int32> Insts;
		int32 NotActivate = 0;
		for (const FFound& X : F) { Insts.Add(X.Inst); NotActivate += X.Slot != SSXEffects::SlotActivate; }
		TestEqual(FString::Printf(TEXT("%s Movie instances"), E.Stem), Insts.Num(), E.Movies);
		TestEqual(FString::Printf(TEXT("%s every Movie node made by the cell activation (slot 0)"), E.Stem), NotActivate, 0);
		Total += Insts.Num();

		SSXPropNodes::FPropAnimData A;
		if (Insts.Num() && A.Load(SSXPropNodes::FPropAnimData::DefaultPath(E.Stem)))
		{
			for (int32 I : Insts)
			{
				const int32 M = A.InstanceModel.IsValidIndex(I) ? A.InstanceModel[I] : INDEX_NONE;
				const int32 Mats = A.ModelMaterials.IsValidIndex(M) ? A.ModelMaterials[M].Num() : -1;
				AddInfo(FString::Printf(TEXT("%s movie instance %d: model %d, %d meshes"), E.Stem, I, M, Mats));
				TestTrue(FString::Printf(TEXT("%s instance %d: 1..4 meshes (the 4-entry material table covers them)"), E.Stem, I), Mats >= 1 && Mats <= 4);
			}
		}
		else if (Insts.Num())
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s.propanim.bin missing: mesh counts not checked"), E.Stem));
		}

		if (E.Frames < 0)
		{
			continue;
		}
		FSssFile File;
		FString Err;
		if (!File.Load(DefaultPath(E.Stem), &Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s video: %s (extracted/ missing?)"), E.Stem, *Err));
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s frames"), E.Stem), File.NumFrames(), E.Frames);
		int32 Bad = 0;
		for (int32 K = 0; K < File.NumFrames(); ++K) { Bad += File.FrameWidth(K) != Width || File.FrameHeight(K) != Height; }
		TestEqual(FString::Printf(TEXT("%s all frames 128 x 64"), E.Stem), Bad, 0);
		TArray<FColor> Img;
		File.Decode(0, Img);
		int32 Opaque = 0;
		for (const FColor& C : Img) { Opaque += C.A == 255; }
		AddInfo(FString::Printf(TEXT("%s: %d frames = %.1f s loop; frame 0 opaque texels %d / %d"), E.Stem, File.NumFrames(),
			File.NumFrames() / double(Fps), Opaque, Img.Num()));
		TestTrue(FString::Printf(TEXT("%s frame 0 mostly opaque"), E.Stem), Opaque > Img.Num() / 2);
	}
	TestEqual(TEXT("14 Movie nodes over the racing tracks"), Total, 14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXMovieNodeTest, "SSX.World.Movie.Node", SSXPropMovieTests::Flags)
bool FSSXMovieNodeTest::RunTest(const FString&)
{
	SSXEffects::FEffectData Fx;
	if (!Fx.Load(SSXEffects::FEffectData::DefaultPath(TEXT("gari"))))
	{
		SSXTest::Skip(*this, TEXT("gari.effects.bin missing; skipped"));
		return true;
	}
	TArray<FFound> F;
	Scan(Fx, F);
	if (!TestTrue(TEXT("gari has two Movie records"), F.Num() >= 2 && F[0].Inst != F.Last().Inst))
	{
		return false;
	}
	const FFound M0 = F[0], M1 = F.Last();

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXMovieTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* PropMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/World/M_SSXProp.M_SSXProp"));
	AStaticMeshActor* Screen = World->SpawnActor<AStaticMeshActor>();
	if (!TestNotNull(TEXT("plane"), Plane) || !TestNotNull(TEXT("M_SSXProp"), PropMat) || !TestNotNull(TEXT("actor"), Screen))
	{
		return false;
	}
	Screen->GetStaticMeshComponent()->SetStaticMesh(Plane);
	Screen->GetStaticMeshComponent()->SetMaterial(0, PropMat);
	Screen->Tags.Add(*FString::Printf(TEXT("SSXInst%d"), M0.Inst));
	UTexture* Own = nullptr;
	PropMat->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseTex")), Own);

	USSXPropAnimation* Props = World->GetSubsystem<USSXPropAnimation>();
	if (!TestNotNull(TEXT("prop subsystem"), Props))
	{
		return false;
	}
	Props->BeginTrack(TEXT("gari"), &Fx, nullptr, nullptr);
	Props->OnNodeCreated(M0.Inst, SSXEffects::TypeMovie, M0.Rec, nullptr, false);
	Props->OnNodeCreated(M1.Inst, SSXEffects::TypeMovie, M1.Rec, nullptr, false);
	TestEqual(TEXT("two screens share one stream"), Props->GetMovieStream().Refs, 2);
	const bool bFile = Props->GetMovieFile().NumFrames() > 0;
	if (!bFile) { SSXTest::Skip(*this, TEXT("JGARI.SSS missing: frame checks skipped")); }

	Props->ApplyRender();
	UTexture* Shown = nullptr;
	Screen->GetStaticMeshComponent()->GetMaterial(0)->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseTex")), Shown);
	TestNotNull(TEXT("movie texture made"), Props->GetMovieTexture());
	TestTrue(TEXT("the screen shows the movie texture"), Shown != nullptr && Shown == Props->GetMovieTexture());
	TestEqual(TEXT("one slot swapped"), Props->NumMovieSlots(M0.Inst), 1);
	// 0x148D28 / 0x200390
	TestFalse(TEXT("in-track screen keeps its baked instance lighting"), USSXPropAnimation::HasFlatVertexColour(Screen->GetStaticMeshComponent()));
	TestEqual(TEXT("blank before the first decode"), Props->GetMovieFrameUploaded(), int32(INDEX_NONE));
	if (bFile)
	{
		for (int32 K = 0; K < 4; ++K) { Props->StepFrame(); }
		Props->ApplyRender();
		TestEqual(TEXT("frame 0 after 4 frames"), Props->GetMovieFrameUploaded(), 0);
		for (int32 K = 0; K < 4 * 264; ++K) { Props->StepFrame(); }
		Props->ApplyRender();
		TestEqual(TEXT("264 frames later: frame 0 again (loop)"), Props->GetMovieFrameUploaded(), 0);
	}

	Props->OnNodeDestroyed(M1.Inst, SSXEffects::TypeMovie);
	TestTrue(TEXT("one screen left: stream open"), Props->GetMovieStream().bOpen && Props->GetMovieStream().Refs == 1);
	Props->OnNodeDestroyed(M0.Inst, SSXEffects::TypeMovie);
	TestFalse(TEXT("no screen: stream closed"), Props->GetMovieStream().bOpen);
	Props->ApplyRender();
	Screen->GetStaticMeshComponent()->GetMaterial(0)->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseTex")), Shown);
	TestTrue(TEXT("node gone: own texture back"), Shown == Own);
	TestEqual(TEXT("node gone: no slot"), Props->NumMovieSlots(M0.Inst), 0);
	return true;
}

#endif
