// FUN_00176850, FUN_00172100, FUN_001777D0, FUN_0011CAE8, FUN_0016DA10, FUN_0016DAA8, FUN_00169BE8, FUN_0015FCF8, 0x116AB8, 0x15EE30
#include "Camera/CameraDefs.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/IntroDialogue.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/AnimController.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags SceneTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	namespace ID = SSXIntroDialogue;

	TArray<ID::FSceneRacer> SixRacers()
	{
		TArray<ID::FSceneRacer> R;
		for (int32 I = 0; I < 6; ++I) R.Add({ I == 0 ? 0 : -1, (I + 5) % 6 });
		return R;
	}

	TMap<int32, TPair<FVector, double>> StartPlacements(const TArray<FSSXSceneActor>& Actors, const FSSXCameraDefSet& Defs)
	{
		TMap<int32, TPair<FVector, double>> Out;
		ID::FScenePlayer P;
		P.OnPlace = [&Out](int32 Racer, const FVector& Pos, double Yaw) { Out.Add(Racer, TPair<FVector, double>(Pos, Yaw)); };
		ID::FObject Obj;
		P.Start(Actors, Defs, SixRacers(), Obj, ID::FData::Get());
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXIntroSceneStageAreaTest, "SSX.Race.IntroScene.StageArea", SceneTestFlags)
bool FSSXIntroSceneStageAreaTest::RunTest(const FString&)
{
	// FUN_001777D0
	{
		FSSXCameraDefSet Defs;
		FMatrix M = FMatrix::Identity;
		M.M[0][0] = 0.0; M.M[0][1] = -1.0; M.M[1][0] = 1.0; M.M[1][1] = 0.0;   // X row (0, -1): -90 deg
		M.M[3][0] = 100.0; M.M[3][1] = 200.0; M.M[3][2] = 300.0;
		Defs.SetStageArea(1, M);
		FVector P(10.0, 20.0, 30.0);
		double Yaw = 0.0;
		TestTrue(TEXT("area 1 resolves"), Defs.StageAreaTransform(1, P, Yaw));
		TestTrue(TEXT("point through the matrix (row vectors)"), P.Equals(FVector(100.0 + 20.0, 200.0 - 10.0, 330.0), 1e-9));
		TestTrue(TEXT("yaw = -pi/2"), FMath::IsNearlyEqual(Yaw, -UE_DOUBLE_HALF_PI, 1e-12));
		FVector Q(1, 2, 3);
		TestFalse(TEXT("area 2 missing: no transform"), Defs.StageAreaTransform(2, Q, Yaw));
		TestFalse(TEXT("area 0: world"), Defs.StageAreaTransform(0, Q, Yaw));
		TestTrue(TEXT("FUN_001741D0 wraps to [-pi, pi)"), FMath::IsNearlyEqual(ID::WrapYaw(3.5), 3.5 - UE_DOUBLE_TWO_PI, 1e-12)
			&& FMath::IsNearlyEqual(ID::WrapYaw(-1.0), -1.0, 1e-12));
	}

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("intro_scene_reference.csv")))
		|| Lines.Num() < 2)
	{
		AddError(TEXT("SimData/intro_scene_reference.csv missing (tools/make_intro_scene_reference.py)"));
		return false;
	}
	TMap<FString, TUniquePtr<FSSXCameraDefSet>> DefsByTrack;
	TMap<FString, TUniquePtr<FPatchWorld>> Worlds;
	TMap<FString, TArray<TPair<FVector, double>>> PlacedBySlot;
	int32 Rows = 0, PosBad = 0, OnProp = 0, OnPatch = 0, Raised = 0, Off = 0;
	TMap<FString, int32> OffBy;
	double MaxDpos = 0.0, MaxDyaw = 0.0, MaxDzProp = 0.0, MaxDzPatch = 0.0;
	for (int32 L = 1; L < Lines.Num(); ++L)
	{
		TArray<FString> C;
		Lines[L].ParseIntoArray(C, TEXT(","), false);
		if (C.Num() < 10) continue;
		const FString& Track = C[0];
		const FString& Scene = C[1];
		const int32 Slot = FCString::Atoi(*C[2]);
		if (!DefsByTrack.Contains(Track))
		{
			TUniquePtr<FSSXCameraDefSet> D = MakeUnique<FSSXCameraDefSet>();
			TUniquePtr<FPatchWorld> W = MakeUnique<FPatchWorld>();
			if (!D->LoadForTrack(Track) || !W->LoadTrack(FPatchWorld::DefaultTrackPath(Track)))
			{
				AddError(FString::Printf(TEXT("%s: camera data / patches missing"), *Track));
				return false;
			}
			DefsByTrack.Add(Track, MoveTemp(D));
			Worlds.Add(Track, MoveTemp(W));
		}
		const FSSXCameraDefSet& Defs = *DefsByTrack[Track];
		const FString Key = Track + TEXT("/") + Scene;
		if (!PlacedBySlot.Contains(Key))
		{
			TArray<TPair<FVector, double>>& Out = PlacedBySlot.Add(Key);
			const TArray<FSSXSceneActor>* Actors = Defs.FindScene(Scene);
			Out.Init(TPair<FVector, double>(FVector(1e20), 0.0), 8);
			if (Actors)
			{
				const TArray<int32> Who = ID::AssignActors(*Actors, SixRacers());
				const TMap<int32, TPair<FVector, double>> ByRacer = StartPlacements(*Actors, Defs);
				for (int32 K = 0; K < Who.Num() && K < 8; ++K)
				{
					if (Who[K] >= 0 && ByRacer.Contains(Who[K])) Out[K] = ByRacer[Who[K]];
				}
			}
		}
		const TPair<FVector, double>& Got = PlacedBySlot[Key][FMath::Clamp(Slot, 0, 7)];
		const FVector Want(FCString::Atod(*C[4]), FCString::Atod(*C[5]), FCString::Atod(*C[6]));
		const double WantYaw = FCString::Atod(*C[7]);
		const double Dpos = (Got.Key - Want).Size(), Dyaw = FMath::Abs(ID::WrapYaw(Got.Value - WantYaw));
		++Rows;
		if (Dpos > 0.05 || Dyaw > 1e-4)
		{
			if (++PosBad <= 5) AddInfo(FString::Printf(TEXT("%s actor %d: placed (%s) yaw %.5f, reference (%s) yaw %.5f"), *Key, Slot,
				*Got.Key.ToString(), Got.Value, *Want.ToString(), WantYaw));
		}
		MaxDpos = FMath::Max(MaxDpos, Dpos);
		MaxDyaw = FMath::Max(MaxDyaw, Dyaw);
		double Dz = 1e9;
		bool bProp = !C[8].IsEmpty();
		if (bProp)
		{
			Dz = Got.Key.Z - FCString::Atod(*C[8]);
		}
		else
		{
			FRiderWorldHit Hit;
			if (Worlds[Track]->LineQuery(Got.Key + FVector(0, 0, 100.0), Got.Key - FVector(0, 0, 100.0), ERiderLineQuery::FineRefined, 0.5, Hit))
			{
				Dz = Got.Key.Z - Hit.Point.Z;
			}
		}
		// S_COM1 actor 4 and S_COM4 actor 1 are posed off the floor (72 cm above it / in the bench, Mdl_Prepbench)
		const bool bRaisedSlot = (Scene == TEXT("S_COM1") && Slot == 4) || (Scene == TEXT("S_COM4") && Slot == 1);
		if (FMath::Abs(Dz) <= 5.0)
		{
			(bProp ? OnProp : OnPatch)++;
			(bProp ? MaxDzProp : MaxDzPatch) = FMath::Max(bProp ? MaxDzProp : MaxDzPatch, FMath::Abs(Dz));
		}
		else if (bRaisedSlot && bProp)
		{
			++Raised;
		}
		else
		{
			++Off;
			OffBy.FindOrAdd(Key)++;
			if (Off <= 10 || Scene == TEXT("PIPE") || Scene == TEXT("UNTR")) AddInfo(FString::Printf(TEXT("%s actor %d at (%.1f %.1f %.1f): floor %s"), *Key, Slot, Got.Key.X, Got.Key.Y,
				Got.Key.Z, Dz > 1e8 ? TEXT("none within 1 m") : *FString::Printf(TEXT("dz %.1f (%s)"), Dz, bProp ? *C[9] : TEXT("patch"))));
		}
	}
	AddInfo(FString::Printf(TEXT("scene actors %d: max |dpos| %.4f cm, max |dyaw| %.2g rad vs the reference; on a prop floor %d (max |dz| %.2f), "
		"on the patches %d (max |dz| %.2f), posed off the floor %d, elsewhere %d"), Rows, MaxDpos, MaxDyaw, OnProp, MaxDzProp, OnPatch,
		MaxDzPatch, Raised, Off));
	FString OffList;
	for (const auto& It : OffBy) OffList += FString::Printf(TEXT(" %s:%d"), *It.Key, It.Value);
	AddInfo(TEXT("no floor within 5 cm:") + OffList);
	TestTrue(TEXT("reference rows"), Rows > 700);
	TestEqual(TEXT("placements = reference (world = (x, y, z, 1) x stage matrix, yaw + X-row heading, wrapped)"), PosBad, 0);
	TestTrue(TEXT("the start / finish stage actors of the eight stage tracks stand on the platform floor (|dz| <= 5 cm)"), OnProp >= 8 * 70);

	{
		FSSXCameraDefSet Defs;
		if (Defs.LoadForTrack(TEXT("aloha")))
		{
			const TArray<FSSXSceneActor>* A = Defs.FindScene(TEXT("S_COM3"));
			const TMap<int32, TPair<FVector, double>> Placed = A ? StartPlacements(*A, Defs) : TMap<int32, TPair<FVector, double>>();
			const TArray<int32> Who = A ? ID::AssignActors(*A, SixRacers()) : TArray<int32>();
			if (TestTrue(TEXT("Aloha S_COM3 placed"), A && Who.Num() > 0 && Who[0] >= 0 && Placed.Contains(Who[0])))
			{
				const double Got = Placed[Who[0]].Value, Want = ID::WrapYaw((*A)[0].Yaw - UE_DOUBLE_HALF_PI);
				AddInfo(FString::Printf(TEXT("Aloha S_COM3 actor 1: record yaw %.6f, placed %.6f, expected %.6f"), (*A)[0].Yaw, Got, Want));
				TestTrue(TEXT("Aloha S_COM3 actor 1 yaw = wrap(yaw - pi/2)"), FMath::Abs(ID::WrapYaw(Got - Want)) < 1e-5);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXIntroSceneClipsTest, "SSX.Race.IntroScene.Clips", SceneTestFlags)
bool FSSXIntroSceneClipsTest::RunTest(const FString&)
{
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	const ID::FData& D = ID::FData::Get();
	if (!Lib || !D.bLoaded)
	{
		AddError(TEXT("SimData/anim_clips.bin / intro_dialogue.json missing (tools/export_anim_clips.py, export_intro_dialogue.py)"));
		return false;
	}
	int32 Ids = 0, Bad = 0;
	for (const auto& It : D.Clips)
	{
		const int32 Id = It.Key;
		const SSXAnim::FAnimProps* P = Lib->Props(Id);
		const int32 Clip = Lib->Resolve(Id, 0, 0);
		const SSXAnim::FAflClip* C = Lib->Clip(Clip);
		++Ids;
		const bool bOk = P && P->Category == 13 && P->Lookup == 2 && P->LoopMode == 2 && P->Channel == 0 && P->Blend == 0
			&& C && C->Name == It.Value.Name && C->NFrames == It.Value.Frames && C->Block(0) != nullptr
			&& Lib->Resolve(Id, 2, 11) == Clip;
		if (!bOk)
		{
			if (++Bad <= 5)
			{
				AddInfo(FString::Printf(TEXT("line id %d (%s): props %d clip %d %s"), Id, *It.Value.Name, P ? P->Category : -1, Clip,
					C ? *C->Name : TEXT("-")));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("line ids %d, unresolved / wrong %d"), Ids, Bad));
	TestTrue(TEXT("all intro line ids checked"), Ids > 400);
	TestEqual(TEXT("every line id plays its scene clip (category 13, lookup 2, hold last, channel 0, hard cut)"), Bad, 0);

	// 0x169BE8
	SSXAnim::FAnimController Ctl;
	Ctl.Init(Lib, 0, 4);
	Ctl.Play(0x1FF);
	Ctl.bMirror = false;
	Ctl.Speed = 1.0;
	Ctl.Yaw = 0.0;
	const int32 Line = 220;   // GAT_FO_1 USER line of session 1 (SSX.Race.IntroDialogue.Scene)
	TestEqual(TEXT("Play(line 220) returns the id"), Ctl.Play(Line, 0.0, 5), Line);
	const SSXAnim::FPlayInstance* I = Ctl.Newest(0);
	const ID::FLineClip* LC = D.Clip(Line);
	TestTrue(TEXT("channel 0 newest = the scene clip at frame 0, weight 1 (hard cut), not mirrored"),
		I && LC && Lib->Clip(I->OwnClip) && Lib->Clip(I->OwnClip)->Name == LC->Name && I->Subs[0].Frame == 0.0 && !I->bMirror
		&& Ctl.CurrentId(0) == Line);
	for (int32 F = 0; F < 2000; ++F) Ctl.Update(SSXAnim::FAnimDriveInputs());
	I = Ctl.Newest(0);
	TestTrue(TEXT("loop mode 2: the clip holds its last frame (no auto-chain)"), I && I->AnimId == Line && Ctl.CurrentId(0) == Line);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXIntroSceneHandOffTest, "SSX.Race.IntroScene.HandOff", SceneTestFlags)
bool FSSXIntroSceneHandOffTest::RunTest(const FString&)
{
	FSSXCameraDefSet Defs;
	if (!Defs.LoadForTrack(TEXT("gari")) || !Defs.FindScene(TEXT("S_COM3")))
	{
		AddError(TEXT("SimData/cameras missing (tools/export_cameras.py)"));
		return false;
	}
	// 0x177020, 0x16DAA8
	ID::FScenePlayer P;
	int32 Places = 0;
	P.OnPlace = [&Places](int32, const FVector&, double) { ++Places; };
	ID::FObject Obj;
	P.Start(*Defs.FindScene(TEXT("S_COM3")), Defs, SixRacers(), Obj, ID::FData::Get());
	TestEqual(TEXT("S_COM3: six actors placed at the start"), Places, 6);
	for (int32 F = 0; F < 10; ++F) P.Step(Obj, ID::FData::Get());
	TestEqual(TEXT("re-placed on each of 10 updates"), Places, 66);
	P.Stop();
	P.Step(Obj, ID::FData::Get());
	TestEqual(TEXT("not after STOP / director reset"), Places, 66);
	Places = 0;
	P.Start(*Defs.FindScene(TEXT("FOE_1")), Defs, SixRacers(), Obj, ID::FData::Get());
	P.Step(Obj, ID::FData::Get());
	TestEqual(TEXT("FOE_1: no placement (place 0)"), Places, 0);

	// 0x116AB8, 0x15EE30
	FRaceTrackData Data;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("gari race data: %s"), *Err));
		return false;
	}
	TArray<TUniquePtr<FRiderSim>> Sims;
	FRaceSession Session;
	FRaceConfig Cfg;
	TestTrue(TEXT("session init"), Session.Init(Data, Cfg));
	for (int32 I = 0; I < 6; ++I)
	{
		Sims.Add(MakeUnique<FRiderSim>());
		SSXRiderSetup::RegisterStates(*Sims[I], true);
		Session.AddRacer(Sims[I].Get(), I == 0);
		if (SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(*Sims[I])) { Ch->RequestScenePlay(220); }
	}
	Session.Start();
	Session.Tick();
	bool bAll = true;
	for (int32 I = 0; I < 6; ++I)
	{
		const SSXAnim::FRiderAnimChannel* Ch = SSXAnim::FindChannel(*Sims[I]);
		bAll &= Ch && Ch->bResetAnim && Ch->ScenePlays.Num() == 0;
	}
	TestTrue(TEXT("start placement: animation reset requested, pending scene clips dropped"), bAll);
	return true;
}

#endif
