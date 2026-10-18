// 0x199D10 / 0x199DB8 / 0x199EB0 / 0x199F20
#include "Effects/TrackEffects.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "World/PropNodes.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace SSXPropNodes;

namespace SSXAnimDeltaComboTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	struct FTrackRun
	{
		SSXEffects::FEffectData Data;
		FPropAnimData Anim;
		SSXEffects::FEffectWorld W;
		TMap<int32, FAnimObject> Nodes;
		FRiderSim Sim;
		SSXEffects::FEffectRider Rider;
		int32 Messages = 0;

		bool Load(FAutomationTestBase* T, const TCHAR* Stem)
		{
			FString Err;
			if (!Data.Load(SSXEffects::FEffectData::DefaultPath(Stem), &Err) || !Anim.Load(FPropAnimData::DefaultPath(Stem), &Err))
			{
				SSXTest::Skip(*T, FString::Printf(TEXT("%s: %s; skipped"), Stem, *Err));
				return false;
			}
			W.Init(&Data, 0, SSXEffects::TrackIdForStem(Stem));
			Rider.Sim = &Sim;
			Rider.Slot = 0;
			Rider.bHuman = true;
			W.OnOutcome = [this](const SSXEffects::FOutcome& O)
			{
				if (O.Kind == SSXEffects::EOutcome::Node && O.A >= SSXEffects::TypeAnimObject && O.A <= SSXEffects::TypeAnimTexFlip)
				{
					FAnimObjectRecord Rec;
					if (FAnimObjectRecord::Parse(Data.RecordBytes(Data.Records[O.Record]), Data.Records[O.Record].Size, Rec))
					{
						FAnimObject A;
						const FPropModelInfo* M = Anim.ModelOfInstance(O.Instance);
						A.Init(Rec, M ? M->EndFrame : 0.0, [](double Lo, double) { return Lo; }, false);
						Nodes.Add(O.Instance, A);
					}
				}
				else if (O.Kind == SSXEffects::EOutcome::NodeDestroyed)
				{
					Nodes.Remove(O.Instance);
				}
				else if (O.Kind == SSXEffects::EOutcome::Message)
				{
					if (FAnimObject* A = Nodes.Find(O.Instance)) { A->Message(O.B, O.V); ++Messages; }
				}
			};
			return true;
		}

		void Step(int32 Frames)
		{
			for (int32 F = 0; F < Frames; ++F)
			{
				W.Frame();
				TArray<int32> Done;
				for (TPair<int32, FAnimObject>& P : Nodes) { if (P.Value.Tick()) { Done.Add(P.Key); } }
				for (int32 I : Done) { W.NodeDone(I); }
			}
		}

		double T(int32 Inst) const { const FAnimObject* A = Nodes.Find(Inst); return A ? A->T : -1.0; }

		double Channel(int32 Inst, int32 K) const
		{
			const FAnimObject* A = Nodes.Find(Inst);
			const FPropModelInfo* M = Anim.ModelOfInstance(Inst);
			for (int32 O = 0; A && M && O < M->NumObjects; ++O)
			{
				const FPropObjectInfo& Ob = Anim.Objects[M->FirstObject + O];
				if (Anim.Anims.IsValidIndex(Ob.Anim)) { return Anim.Anims[Ob.Anim].Channel(K, A->T); }
			}
			return 0.0;
		}
	};

	FAnimObjectRecord KickerRecord()
	{
		FAnimObjectRecord R;
		R.Subtype = KindAnimDelta;
		R.Mode = AnimPingPong;
		R.Start = 0.0;
		R.End = 30.0;
		R.Speed = 30.0;
		R.Cooldown = 1.0;
		R.Direction = 3;
		return R;
	}
}

using namespace SSXAnimDeltaComboTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimDeltaTest, "SSX.World.AnimDelta", SSXAnimDeltaComboTests::Flags)
bool FSSXAnimDeltaTest::RunTest(const FString&)
{
	FAnimObject A;
	A.Init(KickerRecord(), 30.0, [](double Lo, double) { return Lo; }, false);
	TestFalse(TEXT("AnimDelta starts stopped"), A.bRunning);
	for (int32 F = 0; F < 30; ++F) { A.Tick(); }
	TestEqual(TEXT("no budget: time stands still"), A.T, 0.0);
	A.Message(2, 30.0);
	TestEqual(TEXT("message 2: budget += v / 30"), A.Budget, 1.0f);
	A.Tick();
	TestEqual(TEXT("first budget frame: no advance (previous running flag), now running"), A.T, 0.0);
	TestTrue(TEXT("running after the first budget frame"), A.bRunning);
	int32 Advances = 0;
	double Prev = A.T;
	for (int32 F = 0; F < 120; ++F)
	{
		A.Tick();
		Advances += A.T != Prev;
		Prev = A.T;
	}
	TestEqual(TEXT("1 s budget = 60 advances of 1/60 s"), Advances, 60);
	TestTrue(TEXT("kicker at the clip end (frame 30)"), FMath::Abs(A.T - 1.0) < 1e-6);
	TestFalse(TEXT("budget spent: stopped"), A.bRunning);
	TestEqual(TEXT("budget clamped at 0"), A.Budget, 0.0f);
	A.Message(2, 30.0);
	for (int32 F = 0; F < 120; ++F) { A.Tick(); }
	TestTrue(TEXT("next message: ping-pong back to frame 0"), FMath::Abs(A.T) < 1e-6);
	A.Message(2, -30.0);
	A.Message(1, 0.5);
	const double TFrozen = A.T;
	for (int32 F = 0; F < 10; ++F) { A.Tick(); }
	TestEqual(TEXT("negative budget: frozen"), A.T, TFrozen);
	TestEqual(TEXT("negative budget: delay 30 - 10"), A.Delay, 20);
	TestFalse(TEXT("negative budget: not evaluated"), A.bEvaluated);

	{
		FTrackRun R;
		if (R.Load(this, TEXT("elysium")))
		{
			const int32 Kickers[3] = { 153, 154, 179 };
			for (int32 I : Kickers) { R.W.Activate(I); }
			TestEqual(TEXT("three AnimDelta nodes"), R.Nodes.Num(), 3);
			R.Step(90);
			TestTrue(TEXT("153 (slot 0 sends message 2 v 30): up"), FMath::Abs(R.T(153) - 1.0) < 1e-6);
			TestTrue(TEXT("153 rotated ry 26.98 deg"), FMath::Abs(R.Channel(153, 4) - 26.98) < 0.01);
			TestEqual(TEXT("154 without a message: down"), R.T(154), 0.0);
			TestEqual(TEXT("179 without a message: down"), R.T(179), 0.0);
			R.W.Touch(68, R.Rider);
			R.Step(90);
			TestTrue(TEXT("landing trigger: 153 back down"), FMath::Abs(R.T(153)) < 1e-6);
			TestTrue(TEXT("landing trigger: 154 up"), FMath::Abs(R.T(154) - 1.0) < 1e-6);
			TestTrue(TEXT("landing trigger: 179 up"), FMath::Abs(R.T(179) - 1.0) < 1e-6);
			TSharedPtr<FPatchWorld> Pw = MakeShared<FPatchWorld>();
			FString Err;
			if (Pw->LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("elysium")), &Err))
			{
				const int32 K = Pw->FindInstance(153);
				TestTrue(TEXT("kicker 153 has collision nodes (they follow the keyframes)"), K != INDEX_NONE && Pw->GetInstance(K).NumNodes > 0);
			}
		}
	}
	{
		FTrackRun R;
		if (R.Load(this, TEXT("merquer")))
		{
			R.W.Touch(3622, R.Rider);
			TestTrue(TEXT("both gates got a node"), R.Nodes.Contains(3623) && R.Nodes.Contains(3624));
			R.Step(200);
			TestTrue(TEXT("gate L stops at frame 51 (clip 52)"), FMath::Abs(R.T(3623) - 51.0 / 30.0) < 1e-4);
			TestTrue(TEXT("gate R stops at frame 51 (clip 53)"), FMath::Abs(R.T(3624) - 51.0 / 30.0) < 1e-4);
			TestTrue(TEXT("gate L open about -149 deg"), FMath::Abs(R.Channel(3623, 5) + 149.0) < 5.0);
		}
	}
	{
		FTrackRun R;
		if (R.Load(this, TEXT("mesa")))
		{
			R.W.Touch(2168, R.Rider);
			R.Step(400);
			TestTrue(TEXT("TreeTrunk_Fall_0 stops at frame 84"), FMath::Abs(R.T(2238) - 84.0 / 30.0) < 1e-4);
			TestTrue(TEXT("TreeTrunk_Fall_1 stops at frame 107"), FMath::Abs(R.T(2239) - 107.0 / 30.0) < 1e-4);
			TestTrue(TEXT("TreeLeaves_Fall_0 stops at frame 84"), FMath::Abs(R.T(2236) - 84.0 / 30.0) < 1e-4);
			TestTrue(TEXT("TreeLeaves_Fall_1 stops at frame 107"), FMath::Abs(R.T(2237) - 107.0 / 30.0) < 1e-4);
			const FAnimObject* Tree = R.Nodes.Find(2239);
			TestTrue(TEXT("play-once tree never reaches its end: not finished, the node (and the fallen pose) stays"), Tree && !Tree->bFinished);
			TestTrue(TEXT("rails 87 / 88 enabled"), R.W.IsRailEnabled(87) && R.W.IsRailEnabled(88));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimComboTest, "SSX.World.AnimCombo", SSXAnimDeltaComboTests::Flags)
bool FSSXAnimComboTest::RunTest(const FString&)
{
	FTrackRun R;
	if (!R.Load(this, TEXT("aloha")))
	{
		return true;
	}
	const int32 Inst = 686;
	R.W.Activate(Inst);
	FAnimObject* A = R.Nodes.Find(Inst);
	if (!TestNotNull(TEXT("AnimCombo node"), A))
	{
		return false;
	}
	TestTrue(TEXT("combo clip 61..100 at 30 fps"), FMath::Abs(A->ComboStart - 61.0 / 30.0) < 1e-9 && FMath::Abs(A->ComboEnd - 100.0 / 30.0) < 1e-9
		&& FMath::Abs(A->ComboRate - 1.0 / 60.0) < 1e-9);
	TestFalse(TEXT("word 12 = 0: no hold"), A->bComboHoldAtEnd);
	R.Step(30);
	const double TTouch = R.T(Inst);
	TestTrue(TEXT("side to side: 0.5 s"), FMath::Abs(TTouch - 0.5) < 1e-9);
	R.W.Touch(Inst, R.Rider);
	A = R.Nodes.Find(Inst);
	TestTrue(TEXT("touch -> slot 1 -> message 3: combo running"), A && A->bCombo && FMath::Abs(A->SnapshotT - TTouch) < 1e-12);
	const SSXEffects::FController* C = R.W.FindController(Inst);
	TestTrue(TEXT("touch cooldown reloaded to 60 frames"), C && C->TouchCooldown == 60);
	R.Step(40);
	A = R.Nodes.Find(Inst);
	C = R.W.FindController(Inst);
	TestTrue(TEXT("touch cooldown counts down (0x199030): 20 left"), C && C->TouchCooldown == 20);
	TestTrue(TEXT("during the combo the side-to-side time stands still"), A && A->T == TTouch);
	TestTrue(TEXT("combo time 61 + 40 x 0.5 frames"), A && FMath::Abs(A->ComboT - (61.0 + 20.0) / 30.0) < 1e-6);
	A->Message(3, 0.0);
	TestTrue(TEXT("message 3 during the combo is ignored"), FMath::Abs(A->ComboT - 81.0 / 30.0) < 1e-6 && A->SnapshotT == TTouch);
	TArray<FMatrix> Combo, AtTouch;
	R.Anim.ComboWorldMatrices(Inst, FMatrix::Identity, A->ComboT, A->SnapshotT, Combo);
	R.Anim.ObjectWorldMatrices(Inst, FMatrix::Identity, TTouch, true, AtTouch);
	if (TestTrue(TEXT("two objects"), Combo.Num() >= 2 && AtTouch.Num() >= 2))
	{
		TestTrue(TEXT("tipped barrier stays where it was touched"), FVector::Dist(Combo[1].GetOrigin(), AtTouch[1].GetOrigin()) < 1.0);
		const double Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Combo[1].GetUnitAxis(EAxis::Z), AtTouch[1].GetUnitAxis(EAxis::Z)), -1.0, 1.0)));
		TestTrue(TEXT("tipped about 90 deg"), FMath::Abs(Ang - 90.0) < 1.0);
	}
	int32 Frames = 40;
	while (A && A->bCombo && Frames < 200)
	{
		R.Step(1);
		++Frames;
		A = R.Nodes.Find(Inst);
	}
	TestTrue(TEXT("combo over after 78 frames, back to the side-to-side pose"), A && (Frames == 78 || Frames == 79) && !A->ComboPose() && A->T == TTouch);
	R.Step(10);
	A = R.Nodes.Find(Inst);
	TestTrue(TEXT("side to side resumes from the touch time"), A && FMath::Abs(A->T - (TTouch + 10.0 / 60.0)) < 1e-9);
	return true;
}

#endif
