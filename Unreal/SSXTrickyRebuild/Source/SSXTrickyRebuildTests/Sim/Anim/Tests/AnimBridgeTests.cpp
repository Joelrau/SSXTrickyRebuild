#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/AnimController.h"
#include "Sim/Anim/RiderAnimDriver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimBridgeTest, "SSX.Anim.Bridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAnimBridgeTest::RunTest(const FString& Parameters)
{
	using namespace SSXAnim;
	FString Text;
	TSharedPtr<FJsonObject> Ref;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("anim_bridge_reference.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ref) || !Ref.IsValid())
	{
		AddError(TEXT("SimData/anim_bridge_reference.json missing (run tools/make_anim_bridge_reference.py)"));
		return false;
	}
	auto Num = [](const TSharedPtr<FJsonValue>& V) { return V->AsNumber(); };
	auto Sg = [](double X) { return FMath::Abs(X) < 1e-3 ? 0 : (X > 0.0 ? 1 : -1); };

	int32 TkOk = 0, TkN = 0, TbOk = 0, TbN = 0, QrOk = 0, QrN = 0, LtOk = 0, LtN = 0;
	for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("takeoff")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
		const int32 Clip = TakeoffClip(TakeoffRow(Num(A[0]), Num(A[1]), Num(A[2]) != 0.0), int32(Num(A[3])));
		++TkN;
		TkOk += (Clip == int32(Num(A[4])) || Clip == int32(Num(A[5]))) ? 1 : 0;
	}
	for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("tumble")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
		const int32 Cat = int32(Num(A[2])), Id = int32(Num(A[3]));
		bool bHit = false;
		for (int32 K = 0; K < 2; ++K)
		{
			const TArray<TSharedPtr<FJsonValue>>& D = A[K]->AsArray();
			bHit |= TumbleId(Num(D[0]), Num(D[1]), Num(D[2]), Cat) == Id;
		}
		++TbN;
		TbOk += bHit ? 1 : 0;
	}
	for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("recovery")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
		++QrN;
		QrOk += QuickRecoveryId(int32(Num(A[0]))) == int32(Num(A[1])) ? 1 : 0;
	}
	for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("late")))
	{
		// 0x102158
		const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
		const bool bGoofy = Num(A[2]) != 0.0;
		const int32 SpinIn = Sg(Num(A[0])), FlipDir = Sg(Num(A[1]));
		++LtN;
		LtOk += (SpinIn != 0 || FlipDir != 0) && DirectionalTrickId(SpinIn, bGoofy ? FlipDir : -FlipDir, bGoofy) == int32(Num(A[3])) ? 1 : 0;
	}
	const FString Msg = FString::Printf(TEXT("live traces vs C++ rules: take-off clip %d/%d, directional trick id %d/%d, tumble %d/%d, quick recovery %d/%d"),
		TkOk, TkN, LtOk, LtN, TbOk, TbN, QrOk, QrN);
	AddInfo(Msg);
	UE_LOG(LogTemp, Display, TEXT("[animbridge] %s"), *Msg);
	TestTrue(TEXT("take-offs 77/77"), TkN == 77 && TkOk == 77);
	TestTrue(TEXT("directional tricks 33/35 (the Python model's score)"), LtN == 35 && LtOk >= 33);
	TestTrue(TEXT("tumbles 25/26"), TbN == 26 && TbOk >= 25);
	TestTrue(TEXT("quick recoveries 2/2"), QrN == 2 && QrOk == 2);

	TestEqual(TEXT("regular: spin right = INTOSPINBS"), DirectionalTrickId(1, 0, false), 0x22C);
	TestEqual(TEXT("regular: flip -1 = INTOFLIPFWD"), DirectionalTrickId(0, -1, false), 0x227);
	TestEqual(TEXT("goofy mirrors the sector"), DirectionalTrickId(-1, 0, true), 0x22C);
	TestEqual(TEXT("rail: type 3 -> 0x256"), RailStartId(3, 0, false), 0x256);
	TestEqual(TEXT("rail: board class 2 + stance swaps 1 / 2"), RailStartId(1, 2, true), 0x255);
	TestEqual(TEXT("LandNormal arms: forward -> L_ARMS"), LandArmsId(FVector(0, 1000, -50), FVector::UpVector, FVector(0, 1, 0), FVector(1, 0, 0), false), 0x20C);
	TestEqual(TEXT("LandNormal arms: sideways + -> L_TS, goofy -> L_HS"),
		LandArmsId(FVector(1000, 300, 0), FVector::UpVector, FVector(0, 1, 0), FVector(1, 0, 0), false) * 1000
		+ LandArmsId(FVector(1000, 300, 0), FVector::UpVector, FVector(0, 1, 0), FVector(1, 0, 0), true), 0x20D * 1000 + 0x20E);
	TestEqual(TEXT("mask: arms bits 1..12"), OverlayMask(0x20C, 0), 0x1FFEu);
	TestEqual(TEXT("mask: head checks 3,4,5,9"), OverlayMask(0x210, 0), 0x238u);
	TestEqual(TEXT("mask: land bobble 6,7,8,10,11,12"), OverlayMask(0x216, 0), 0x1DC0u);
	TestEqual(TEXT("mask: shift"), OverlayMask(0x20C, 19), 0x1FFEu << 19);
	TestEqual(TEXT("mask: others full"), OverlayMask(0x1FF, 0), 0xFFFFFFFFu);
	TestEqual(TEXT("sit idle: characters 2 / 11"), SitIdleId(2) * 1000 + SitIdleId(4), 0x2F4 * 1000 + 0x2F0);

	const FAflLibrary* Lib = FAflLibrary::Shared();
	if (!Lib || !Lib->IsLoaded())
	{
		AddError(TEXT("SimData/anim_clips.bin missing (run tools/export_anim_clips.py)"));
		return false;
	}
	TestEqual(TEXT("anim_clips.bin version 3 (10 variants)"), Lib->GetNumVariants(), 10);
	const int32 Counts[][2] = { { 0x205, 3 }, { 0x206, 3 }, { 0x207, 1 }, { 0x208, 1 }, { 0x209, 2 }, { 0x20A, 2 }, { 0x211, 5 },
		{ 0x212, 5 }, { 0x213, 5 }, { 0x214, 10 }, { 0x215, 9 }, { 0x23E, 3 } };
	int32 CountOk = 0;
	for (const auto& C : Counts) { CountOk += Lib->VariantCount(C[0]) == C[1] ? 1 : 0; }
	TestEqual(TEXT("variant counts of the 12 random ids (RAM property table)"), CountOk, 12);
	const FAnimProps* P0 = Lib->Props(0);
	TestTrue(TEXT("id 0 cmSG_FREERIDEAMBIENT exported (cycle on channel 0)"), P0 && P0->LoopMode == 3 && P0->Channel == 0 && Lib->Resolve(0, 0, 10) >= 0);

	FAnimController Ctl;
	Ctl.Init(Lib, 0, 10);
	TArray<uint32> Draws = { 7u, 0xFFFFFFFFu };
	int32 DrawIdx = 0;
	Ctl.RandomB = [&Draws, &DrawIdx]() { return Draws[DrawIdx++ % Draws.Num()]; };
	Ctl.Play(0x214);
	TestEqual(TEXT("0x214: draw 7 % 10 -> variant 7"), Ctl.LastVariant, 7);
	TestEqual(TEXT("0x214: variant 7's clip"), Ctl.Newest(Lib->Props(0x214)->Channel)->OwnClip, Lib->Resolve(0x214, 0, 10, 7));
	Ctl.Play(0x215);
	TestEqual(TEXT("0x215: unsigned 0xFFFFFFFF % 9 = 3"), Ctl.LastVariant, int32(0xFFFFFFFFu % 9u));
	TestEqual(TEXT("one draw per Play of a random id"), DrawIdx, 2);
	Ctl.Play(0x1FF);
	TestEqual(TEXT("no draw for a plain id"), DrawIdx, 2);

	int32 Action1Id = INDEX_NONE;
	for (int32 Id = 0x1F0; Id < 0x300 && Action1Id == INDEX_NONE; ++Id)
	{
		const FAnimProps* P = Lib->Props(Id);
		if (P && P->OnDone == 1 && P->Channel == 0 && P->LoopMode == 0 && Lib->Resolve(Id, 0, 10) >= 0) { Action1Id = Id; }
	}
	if (TestTrue(TEXT("an on-done action 1 one-shot exists"), Action1Id != INDEX_NONE))
	{
		FAnimController C1;
		C1.Init(Lib, 0, 10);
		C1.Play(Action1Id);
		FAnimDriveInputs In;
		for (int32 F = 0; F < 2000 && C1.ChainAction1 == 0; ++F) { C1.Update(In); }
		TestTrue(*FString::Printf(TEXT("0x%X done -> chain action 1, then 0x1FF"), Action1Id), C1.ChainAction1 == 1 && C1.CurrentId(0) == 0x1FF);
	}
	return !HasAnyErrors();
}

#endif
