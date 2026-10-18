#include "Harness/FixedStepClock.h"
#include "Harness/ReplayHarness.h"
#include "Input/InpFile.h"
#include "Input/PadMapper.h"
#include "Input/SSXInputSetup.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags PawnTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	void Report(FAutomationTestBase* Test, const TCHAR* Tag, const FString& Msg)
	{
		Test->AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[%s] %s"), Tag, *Msg);
	}

	bool SameWord(const FRiderInputWord& A, const FRiderInputWord& B, FString& Why)
	{
		Why.Reset();
#define SSX_CMP(F) if (A.F != B.F) { Why += TEXT(" ") TEXT(#F); }
		SSX_CMP(bResetRequest) SSX_CMP(bJumpPressed) SSX_CMP(bJumpHeld) SSX_CMP(bJumpAbort) SSX_CMP(bBoostHeld)
		SSX_CMP(bBoostPressed) SSX_CMP(bTweak) SSX_CMP(bTrickPressed) SSX_CMP(bLessonContinue) SSX_CMP(TrickId)
		SSX_CMP(Turn) SSX_CMP(Crouch) SSX_CMP(Brake) SSX_CMP(RailSpin) SSX_CMP(PrewindTurn) SSX_CMP(PrewindSpin)
		SSX_CMP(PrewindFlip) SSX_CMP(GateRock) SSX_CMP(Spin) SSX_CMP(Flip) SSX_CMP(LateSpin) SSX_CMP(LateFlip)
		SSX_CMP(AirTurn) SSX_CMP(AirTilt) SSX_CMP(ShoveSide) SSX_CMP(ShoveFwd)
#undef SSX_CMP
		return Why.IsEmpty();
	}

	const ERiderControl AllControls[] = {
		ERiderControl::Void, ERiderControl::Bump, ERiderControl::Cruise, ERiderControl::ChangeFakie, ERiderControl::FakieCruise,
		ERiderControl::FinishLine, ERiderControl::GateToBase, ERiderControl::GateAnticipate, ERiderControl::GateLaunch,
		ERiderControl::Jump, ERiderControl::LandHard, ERiderControl::LandNormal, ERiderControl::NaturalAir,
		ERiderControl::Prewind, ERiderControl::RailSlide, ERiderControl::Spin, ERiderControl::SitAndWait,
		ERiderControl::GetupFromSit, ERiderControl::WipeOut, ERiderControl::WipeOutRecover, ERiderControl::LessonWait,
		ERiderControl::ResetWait };

	const TCHAR* const MapperFiles[] = { TEXT("brod01"), TEXT("eddi05"), TEXT("kaor12"), TEXT("mari26"),
		TEXT("zoe30"), TEXT("jp17"), TEXT("mac09"), TEXT("psym21") };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnInputMapperTest, "SSX.Pawn.InputMapper", PawnTestFlags)

bool FSSXPawnInputMapperTest::RunTest(const FString& Parameters)
{
	int64 Frames = 0, WordOk = 0, BoolOk = 0, GrabOk = 0, Files = 0;
	TMap<FString, int32> LayoutUse;
	for (const TCHAR* Name : MapperFiles)
	{
		const FString Path = FInpFile::TutorialPath(Name);
		TArray<FInpFrame> InpFrames;
		FString Err;
		if (!FInpFile::Load(Path, InpFrames, &Err))
		{
			AddError(FString::Printf(TEXT("%s: %s (tutorial files are read from the local unpacked/ folder)"), Name, *Err));
			continue;
		}
		int32 Character = 0, BoardClass = 0;
		if (!FPadMapper::TutorialRider(Path, Character, BoardClass))
		{
			AddError(FString::Printf(TEXT("%s: unknown tutorial rider"), Name));
			continue;
		}
		++Files;
		FPadMapper Mapper(Character, BoardClass);
		int32 FileBad = 0;
		TArray<ERiderControl> Hits;
		for (int32 I = 0; I < InpFrames.Num(); ++I)
		{
			const FInpFrame& F = InpFrames[I];
			const FPadActions A = Mapper.Step(F, /*bUseRecordedMasks=*/false);
			++Frames;
			const uint32 Bools = FPadMapper::BoolMask(F.Pad.ButtonsFiltered);
			const uint32 Grabs = FPadMapper::GrabMask(F.Pad.ButtonsFiltered, Character, BoardClass);
			BoolOk += Bools == F.RecordedBools;
			GrabOk += Grabs == F.RecordedGrabs;
			FPadMapper::InferControls(F.RecordedWord, A, Hits);
			WordOk += Hits.Num() > 0;
			FString Key;
			for (int32 K = 0; K < FMath::Min(3, Hits.Num()); ++K) { Key += FString(LexToString(Hits[K])) + TEXT(" "); }
			LayoutUse.FindOrAdd(Key.IsEmpty() ? TEXT("(none)") : Key)++;
			if ((Hits.Num() == 0 || Bools != F.RecordedBools || Grabs != F.RecordedGrabs) && ++FileBad <= 5)
			{
				AddError(FString::Printf(TEXT("%s frame %d: word 0x%08X layouts %d, bools 0x%X vs 0x%X, grabs 0x%X vs 0x%X"),
					Name, I, F.RecordedWord, Hits.Num(), Bools, F.RecordedBools, Grabs, F.RecordedGrabs));
			}
		}
	}
	const double Pct = Frames ? 100.0 * double(WordOk) / double(Frames) : 0.0;
	Report(this, TEXT("SSX.Pawn"), FString::Printf(TEXT("input mapper: %lld files, %lld frames; control word rebuilt from the raw pad by at least one layout: %lld (%.2f%%); bool mask %lld, grab mask %lld"),
		Files, Frames, WordOk, Pct, BoolOk, GrabOk));
	LayoutUse.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 Shown = 0;
	for (const TPair<FString, int32>& P : LayoutUse)
	{
		if (++Shown > 6) { break; }
		Report(this, TEXT("SSX.Pawn"), FString::Printf(TEXT("  %6d frames: %s"), P.Value, *P.Key));
	}
	TestEqual(TEXT("files loaded"), Files, int64(UE_ARRAY_COUNT(MapperFiles)));
	TestEqual(TEXT("control words rebuilt"), WordOk, Frames);
	TestEqual(TEXT("bool masks"), BoolOk, Frames);
	TestEqual(TEXT("grab masks"), GrabOk, Frames);
	return Frames > 0 && WordOk == Frames && BoolOk == Frames && GrabOk == Frames;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnInputWordTest, "SSX.Pawn.InputWord", PawnTestFlags)

bool FSSXPawnInputWordTest::RunTest(const FString& Parameters)
{
	int32 Checked = 0, Bad = 0;
	for (const TCHAR* Name : { TEXT("brod01"), TEXT("mari26") })
	{
		TArray<FInpFrame> InpFrames;
		const FString Path = FInpFile::TutorialPath(Name);
		if (!FInpFile::Load(Path, InpFrames))
		{
			AddError(FString::Printf(TEXT("cannot read %s"), *Path));
			return false;
		}
		int32 Character = 0, BoardClass = 0;
		FPadMapper::TutorialRider(Path, Character, BoardClass);
		FPadMapper Mapper(Character, BoardClass);
		for (const FInpFrame& F : InpFrames)
		{
			const FPadActions A = Mapper.Step(F, false);
			const FRiderInputWord Full = FPadMapper::FullWord(A);
			for (ERiderControl C : AllControls)
			{
				FRiderInputWord Exact = FPadMapper::DecodeWord(C, FPadMapper::EncodeWord(C, A));
				FRiderInputWord Masked = Full.MaskedFor(C);
				if (C == ERiderControl::WipeOut)
				{
					Masked.AirTurn = Exact.AirTurn;
					Masked.AirTilt = Exact.AirTilt;
				}
				FString Why;
				++Checked;
				if (!SameWord(Exact, Masked, Why) && ++Bad <= 10)
				{
					AddError(FString::Printf(TEXT("%s %s:%s"), Name, LexToString(C), *Why));
				}
			}
		}
	}
	Report(this, TEXT("SSX.Pawn"), FString::Printf(TEXT("input word: %d (frame, layout) pairs, %d mismatches"), Checked, Bad));
	return Bad == 0 && Checked > 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnVirtualPadTest, "SSX.Pawn.VirtualPad", PawnTestFlags)

bool FSSXPawnVirtualPadTest::RunTest(const FString& Parameters)
{
	using namespace SSXPadButton;
	auto Axes = [](uint32 Buttons, FVector2D L = FVector2D::ZeroVector, int32 Layout = 0)
	{
		FPadMapper M(10, 2);
		M.SetLayout(Layout);
		return M.Step(SSXInput::MakePad(Buttons, L, FVector2D::ZeroVector));
	};
	TestEqual(TEXT("Up = crouch"), Axes(Up).Axes[FPadMapper::ACrouch], 1.0);
	TestEqual(TEXT("Down = brake"), Axes(Down).Axes[FPadMapper::ABrake], 1.0);
	TestEqual(TEXT("Left = turn +1"), Axes(Left).Axes[FPadMapper::ATurn], 1.0);
	TestEqual(TEXT("Left = prewind spin -1"), Axes(Left).Axes[FPadMapper::APrewindSpin], -1.0);
	TestEqual(TEXT("Right = turn -1"), Axes(Right).Axes[FPadMapper::ATurn], -1.0);
	// sticks: UE +X right, +Y up; through the pad byte; dead zone 0.38
	TestTrue(TEXT("stick left turns left"), Axes(0, FVector2D(-1, 0)).Axes[FPadMapper::ATurn] > 0.99);
	TestTrue(TEXT("stick up crouches"), Axes(0, FVector2D(0, 1)).Axes[FPadMapper::ACrouch] > 0.99);
	TestEqual(TEXT("stick drift ignored"), Axes(0, FVector2D(0.3, -0.3)).Axes[FPadMapper::ATurn], 0.0);
	TestTrue(TEXT("layout 1: stick spins"), Axes(0, FVector2D(-1, 0), 1).Axes[FPadMapper::ASpin] < -0.99);
	TestTrue(TEXT("layout 1: stick up flips as d-pad Up (-1)"), Axes(0, FVector2D(0, 1), 1).Axes[FPadMapper::AFlip] < -0.99);
	TestEqual(TEXT("layout 0: stick does not spin"), Axes(0, FVector2D(-1, 0), 0).Axes[FPadMapper::ASpin], 0.0);
	TestEqual(TEXT("layout 1: prewind turn null"), Axes(Left, FVector2D(-1, 0), 1).Axes[FPadMapper::APrewindTurn], 0.0);
	TestTrue(TEXT("layout 0: prewind turn from stick"), Axes(0, FVector2D(-1, 0), 0).Axes[FPadMapper::APrewindTurn] > 0.99);
	TestTrue(TEXT("layout 1: triangle = camera reverse"), Axes(Triangle, FVector2D::ZeroVector, 1).IsHeld(FPadMapper::ActCameraReverse)
		&& !Axes(Triangle, FVector2D::ZeroVector, 0).IsHeld(FPadMapper::ActCameraReverse));
	TestTrue(TEXT("layout 1: no L3 jump abort"), !Axes(L3, FVector2D::ZeroVector, 1).IsHeld(FPadMapper::ActJumpAbort)
		&& Axes(L3, FVector2D::ZeroVector, 0).IsHeld(FPadMapper::ActJumpAbort));
	const FPadActions J = Axes(Cross);
	TestTrue(TEXT("cross = jump"), J.IsHeld(FPadMapper::ActJump) && J.WasPressed(FPadMapper::ActJump));
	TestTrue(TEXT("square = boost"), Axes(Square).IsHeld(FPadMapper::ActBoost) && Axes(Square).IsHeld(FPadMapper::ActTweak));
	TestTrue(TEXT("select = reset"), Axes(Select).WasPressed(FPadMapper::ActReset));
	TestTrue(TEXT("L1 grab slot"), Axes(L1).IsHeld(12));
	{
		FPadMapper SM(10, 2);
		const FRiderInputWord SW = FPadMapper::DecodeWord(ERiderControl::Spin,
			FPadMapper::EncodeWord(ERiderControl::Spin, SM.Step(SSXInput::MakePad(Left | Up, FVector2D::ZeroVector, FVector2D::ZeroVector))));
		TestEqual(TEXT("spin from Left"), SW.Spin, -1);
		TestEqual(TEXT("late spin from Left"), SW.LateSpin, -1);
		TestEqual(TEXT("flip from Up"), SW.Flip, -1);
		TestEqual(TEXT("late flip from Up"), SW.LateFlip, -1);
		TestEqual(TEXT("full word carries late spin"), FPadMapper::FullWord(SM.Step(SSXInput::MakePad(Right, FVector2D::ZeroVector, FVector2D::ZeroVector))).MaskedFor(ERiderControl::Spin).LateSpin, 1);
	}
	FPadMapper M(10, 2);
	const uint32 W = FPadMapper::EncodeWord(ERiderControl::Cruise, M.Step(SSXInput::MakePad(Left | Cross, FVector2D::ZeroVector, FVector2D::ZeroVector)));
	TestEqual(TEXT("ground word"), W, (31u << 5) | 2u | 4u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXPawnFixedStepTest, "SSX.Pawn.FixedStep", PawnTestFlags)

bool FSSXPawnFixedStepTest::RunTest(const FString& Parameters)
{
	FFixedStepClock C;
	int32 Total = 0;
	for (int32 I = 0; I < 144; ++I) { Total += C.Advance(1.0 / 144.0); }
	TestTrue(TEXT("60 steps per second at 144 fps"), Total == 60 || Total == 59);
	C.Reset();
	TestEqual(TEXT("cap 4"), C.Advance(0.5), 4);
	TestTrue(TEXT("backlog dropped"), C.Alpha() < 1.0);
	C.Reset();
	TestEqual(TEXT("no step"), C.Advance(0.25 / 60.0), 0);
	TestTrue(TEXT("alpha quarter"), FMath::IsNearlyEqual(C.Alpha(), 0.25, 1e-9));
	TestEqual(TEXT("one step"), C.Advance(1.0 / 60.0), 1);
	TestTrue(TEXT("alpha kept"), FMath::IsNearlyEqual(C.Alpha(), 0.25, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayBrod01SlopeTest, "SSX.Replay.Brod01Slope", PawnTestFlags)

bool FSSXReplayBrod01SlopeTest::RunTest(const FString& Parameters)
{
	FReplayOptions Opt;
	Opt.InpPath = FInpFile::TutorialPath(TEXT("brod01"));
	Opt.World = TEXT("slope15");
	FReplayResult Result;
	if (!SSXReplay::Run(Opt, Result))
	{
		AddError(FString::Printf(TEXT("replay failed: %s"), *Result.Error));
		return false;
	}
	const FString Name = SSXReplay::RunName(Opt);
	const FString Out = SSXReplay::OutputPath(Name);
	SSXReplay::WriteCsv(Out, Result.Rows);
	Report(this, TEXT("SSX.Replay"), FString::Printf(TEXT("brod01 on slope15: %d frames, %s; %s -> %s"),
		Result.Rows.Num(), *Result.SpawnInfo, *Result.States, *Out));

	TArray<FReplayRow> Model;
	FString Err;
	const FString Ref = SSXReplay::ReferencePath(Name);
	if (!SSXReplay::LoadCsv(Ref, Model, &Err))
	{
		AddError(Err);
		return false;
	}
	int32 WipeFrame = Model.Num();
	for (int32 I = 0; I < Model.Num(); ++I)
	{
		if (Model[I].Motion == TEXT("WIPE_OUT")) { WipeFrame = I; break; }
	}
	const int32 Window = FMath::Min(WipeFrame, Result.Rows.Num());
	TArray<FReplayRow> OursW(Result.Rows.GetData(), Window), ModelW(Model.GetData(), FMath::Min(Window, Model.Num()));
	const FReplayDiff Diff = SSXReplay::Compare(OursW, ModelW, 0.05, 0.05);
	Report(this, TEXT("SSX.Replay"), FString::Printf(TEXT("strict window: frames 0-%d (before the model's wipe-out)"), Window - 1));
	for (const FString& Line : Diff.Report)
	{
		Report(this, TEXT("SSX.Replay"), Line);
	}
	bool bEntryOk = true;
	if (WipeFrame < Model.Num())
	{
		bEntryOk = WipeFrame < Result.Rows.Num() && Result.Rows[WipeFrame].Motion == Model[WipeFrame].Motion
			&& Result.Rows[WipeFrame].Control == Model[WipeFrame].Control
			&& FVector::Dist(Result.Rows[WipeFrame].Pos, Model[WipeFrame].Pos) <= 0.05;
		Report(this, TEXT("SSX.Replay"), FString::Printf(TEXT("wipe-out entry at frame %d: %s"), WipeFrame, bEntryOk ? TEXT("same frame, state and position") : TEXT("MISMATCH")));
		const FReplayDiff Full = SSXReplay::Compare(Result.Rows, Model);
		Report(this, TEXT("SSX.Replay"), TEXT("full run (information; the wipe-out models differ):"));
		for (const FString& Line : Full.Report)
		{
			Report(this, TEXT("SSX.Replay"), TEXT("  ") + Line);
		}
	}
	if (!Diff.Passed())
	{
		AddError(FString::Printf(TEXT("C++ diverges from models/boarder.py at frame %d: %s"), Diff.FirstDivergence, *Diff.Subsystem));
	}
	if (!bEntryOk)
	{
		AddError(TEXT("wipe-out entry differs from models/boarder.py"));
	}
	return Diff.Passed() && bEntryOk;
}

#endif
