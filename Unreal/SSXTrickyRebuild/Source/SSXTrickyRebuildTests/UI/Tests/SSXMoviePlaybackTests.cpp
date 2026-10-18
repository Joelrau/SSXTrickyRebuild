#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "IMediaTextureSample.h"
#include "MediaPlayer.h"
#include "MediaPlayerFacade.h"
#include "MediaSampleSink.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/SSXFrontEndData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FSSXPresentedPicture
	{
		double Wall = 0.0;
		double Time = 0.0;
		int32 Format = 0;
		double Mean = -1.0;
	};

	double SampleBrightness(IMediaTextureSample& S)
	{
		const uint8* Buf = static_cast<const uint8*>(S.GetBuffer());
		const FIntPoint Dim = S.GetDim();
		const uint32 Stride = S.GetStride();
		if (!Buf || Dim.X <= 0 || Dim.Y <= 0 || Stride == 0)
		{
			return -1.0;
		}
		const EMediaTextureSampleFormat F = S.GetFormat();
		const FIntPoint Out = S.GetOutputDim();
		const int32 Rows = FMath::Clamp(Out.Y > 0 ? Out.Y : Dim.Y, 1, Dim.Y);
		double Sum = 0.0;
		int64 N = 0;
		for (int32 Y = 0; Y < Rows; Y += 4)
		{
			const uint8* Row = Buf + (SIZE_T)Y * Stride;
			switch (F)
			{
			case EMediaTextureSampleFormat::CharNV12:
			case EMediaTextureSampleFormat::CharNV21:
				for (int32 X = 0; X < (int32)FMath::Min<uint32>(Stride, (uint32)FMath::Max(Out.X, 1)); X += 4) { Sum += (Row[X] - 16) / 219.0; ++N; }
				break;
			case EMediaTextureSampleFormat::CharYUY2:
			case EMediaTextureSampleFormat::CharYVYU:
				for (uint32 X = 0; X + 1 < Stride; X += 8) { Sum += (Row[X] - 16) / 219.0; ++N; }
				break;
			case EMediaTextureSampleFormat::CharUYVY:
				for (uint32 X = 1; X < Stride; X += 8) { Sum += (Row[X] - 16) / 219.0; ++N; }
				break;
			default:   // 4 bytes per pixel RGB-ish
				for (uint32 X = 0; X + 3 < Stride; X += 16) { Sum += (Row[X] + Row[X + 1] + Row[X + 2]) / 765.0; ++N; }
				break;
			}
		}
		return N ? Sum / N : -1.0;
	}

	class FSSXPictureProbe : public FMediaTextureSampleSink
	{
	public:
		FCriticalSection Lock;
		TArray<FSSXPresentedPicture> Pictures;
		double Start = 0.0;
		uint32 Flushes = 0;

		virtual bool Enqueue(const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>& Sample) override
		{
			FSSXPresentedPicture P;
			P.Wall = FPlatformTime::Seconds() - Start;
			P.Time = Sample->GetTime().Time.GetTotalSeconds();
			P.Format = (int32)Sample->GetFormat();
			P.Mean = SampleBrightness(*Sample);
			FScopeLock L(&Lock);
			Pictures.Add(P);
			return true;
		}
		virtual int32 Num() const override { return 0; }
		virtual void RequestFlush() override { ++Flushes; }
		virtual uint32 GetFlushCount() const override { return Flushes; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIEabigPlaybackTest, "SSX.UI.EabigPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXUIEabigPlaybackTest::RunTest(const FString&)
{
	const FString Path = FPaths::Combine(SSXFrontEnd::VideoDir(), TEXT("EABIG.m2ts"));
	if (!FPaths::FileExists(Path))
	{
		SSXTest::Skip(*this, TEXT("converted/frontend/video/EABIG.m2ts missing (tools/mpc.py): skipped"));
		return true;
	}
	struct FState
	{
		TStrongObjectPtr<UMediaPlayer> Player;
		TSharedPtr<FSSXPictureProbe, ESPMode::ThreadSafe> Probe;
		double Begin = 0.0;
		double Ended = -1.0;
		bool bStarted = false;
	};
	TSharedRef<FState> S = MakeShared<FState>();
	S->Player.Reset(NewObject<UMediaPlayer>(GetTransientPackage()));
	S->Player->PlayOnOpen = true;
	S->Player->SetLooping(false);
	S->Probe = MakeShared<FSSXPictureProbe, ESPMode::ThreadSafe>();
	S->Player->GetPlayerFacade()->AddVideoSampleSink(S->Probe.ToSharedRef());
	if (!S->Player->OpenFile(Path))
	{
		AddError(TEXT("media player refused EABIG.m2ts"));
		return true;
	}
	S->Begin = FPlatformTime::Seconds();

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]() -> bool
	{
		const double Now = FPlatformTime::Seconds();
		if (!S->bStarted && S->Player->IsPlaying())
		{
			S->bStarted = true;
			S->Probe->Start = Now;
		}
		if (S->bStarted && S->Ended < 0.0 && !S->Player->IsPlaying() && !S->Player->IsPreparing())
		{
			S->Ended = Now;
		}
		const bool bDone = (S->Ended > 0.0 && Now - S->Ended > 1.5) || Now - S->Begin > 10.0;
		if (!bDone)
		{
			return false;
		}
		TArray<FSSXPresentedPicture> P;
		{
			FScopeLock L(&S->Probe->Lock);
			P = S->Probe->Pictures;
		}
		S->Player->Close();
		const double Dur = S->Player->GetDuration().GetTotalSeconds();
		AddInfo(FString::Printf(TEXT("EABIG: %d pictures presented, duration %.3f, playback ended after %.3f s"), P.Num(), Dur,
			S->Ended > 0.0 ? S->Ended - S->Probe->Start : -1.0));
		for (int32 I = FMath::Max(0, P.Num() - 6); I < P.Num(); ++I)
		{
			AddInfo(FString::Printf(TEXT("  #%d wall %.3f time %.3f fmt %d bright %.3f"), I, P[I].Wall, P[I].Time, P[I].Format, P[I].Mean));
		}
		if (!TestTrue(TEXT("pictures presented"), P.Num() > 0))
		{
			return true;
		}
		if (P.Last().Mean < 0.0)
		{
			AddWarning(TEXT("GPU-only samples: brightness not measurable"));
			return true;
		}
		TestTrue(TEXT("first picture is the logo on white"), P[0].Mean > 0.5);
		TestTrue(TEXT("last picture presented is black (pictures 75-76)"), P.Last().Mean < 0.1);
		int32 FirstBlack = -1;
		for (int32 I = P.Num() - 1; I >= 0 && P[I].Mean < 0.1; --I) FirstBlack = I;
		if (FirstBlack >= 0)
		{
			TestTrue(TEXT("black from picture 75 (2.50 s after picture 0)"),
				FMath::Abs(P[FirstBlack].Time - P[0].Time - 75.0 * 1001.0 / 30000.0) < 0.02);
			TestTrue(TEXT("pictures 0..74 all presented before the black"), FirstBlack >= 75);
		}
		return true;
	}));
	return true;
}

// FUN_002BA290
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXUIMovieRectTest, "SSX.UI.MovieRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSSXUIMovieRectTest::RunTest(const FString& Parameters)
{
	const FBox2f Full = SSXFrontEnd::MovieRect(FIntPoint(512, 448));
	TestEqual(TEXT("512x448 x0"), Full.Min.X, 0.f);
	TestEqual(TEXT("512x448 y0"), Full.Min.Y, 0.f);
	TestEqual(TEXT("512x448 x1"), Full.Max.X, 640.f);
	TestEqual(TEXT("512x448 y1"), Full.Max.Y, 480.f);
	const FBox2f Small = SSXFrontEnd::MovieRect(FIntPoint(320, 224));
	TestEqual(TEXT("320 centred x0"), Small.Min.X, 96.f * 1.25f);
	TestEqual(TEXT("320 top y0"), Small.Min.Y, 0.f);
	TestEqual(TEXT("320 wide"), Small.Max.X - Small.Min.X, 320.f * 1.25f);
	TestEqual(TEXT("224 high"), Small.Max.Y, 240.f);
	const FBox2f Big = SSXFrontEnd::MovieRect(FIntPoint(720, 480));
	TestEqual(TEXT("wide clipped x0"), Big.Min.X, 0.f);
	TestEqual(TEXT("wide clipped x1"), Big.Max.X, 640.f);
	TestEqual(TEXT("tall clipped y1"), Big.Max.Y, 480.f);
	return true;
}

#endif
