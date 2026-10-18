// 0x16AE68
#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Race/GameRng.h"
#include "Race/SSXRaceManager.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/RiderSim.h"
#include "SnowboarderPawn.h"
#include "SSXGameMode.h"
#include "SSXHUD.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "UI/SSXHudOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags WiringTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	bool SameProfile(const FRiderProfile& A, const SSXRider::FBuiltProfile& B)
	{
		for (int32 I = 0; I < SSXRider::NumStats; ++I)
		{
			if (A.StatBytes[I] != B.Stats[I]) return false;
		}
		return A.BoardClass == B.BoardClass && A.Board == B.Board && A.Stance == B.Stance && A.bComputer == B.bComputer;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWiringBoardSelectionTest, "SSX.Wiring.BoardSelection", WiringTestFlags)

bool FSSXWiringBoardSelectionTest::RunTest(const FString& Parameters)
{
	{
		FGameRng A(1234u), B(1234u);
		int32 Board = -1, Outfit = -1;
		ASSXRaceManager::DrawComputerBoard(A, Board, Outfit);
		const int32 Want0 = int32(B.Next() % 5u);
		const int32 Want1 = int32(B.Next() % 12u);
		TestEqual(TEXT("AI outfit = rand() % 5"), Outfit, Want0);
		TestEqual(TEXT("AI board = rand() % 12"), Board, Want1);
	}
	TestEqual(TEXT("board 0 -> bord1"), SSXCharacters::BoardTextureNumber(0), 1);
	TestEqual(TEXT("board 11 -> bord12"), SSXCharacters::BoardTextureNumber(11), 12);
	TestEqual(TEXT("outfit 5 -> 6"), SSXCharacters::OutfitTextureNumber(5), 6);
	TestEqual(TEXT("outfit 6 (Chrome) -> outfit 5's files"), SSXCharacters::OutfitTextureNumber(6), 6);
	TestEqual(TEXT("outfit 7 -> none"), SSXCharacters::OutfitTextureNumber(7), (int32)INDEX_NONE);
	TestTrue(TEXT("mmm prefix"), SSXCharacters::OutfitTexturePath(TEXT("zz_mmm"), TEXT("suit"), 1).Contains(TEXT("mmm1_suit")));

	const SSXRider::FRiderTable& T = SSXRider::FRiderTable::Shared();
	if (!T.IsLoaded())
	{
		AddError(TEXT("SimData/riders.bin missing (tools/export_rider_profiles.py)"));
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXWiringTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	USSXFrontEndSubsystem* FrontEnd = NewObject<USSXFrontEndSubsystem>(NewObject<UGameInstance>());
	int32 Checked = 0;
	for (int32 Char = 0; Char < SSXRider::NumRiders; ++Char)
	{
		int32 Slot = SSXRider::NumBoards - 1;
		for (int32 K = 1; K < SSXRider::NumBoards; ++K)
		{
			if (T.Board(Char, K).Class != T.Board(Char, 0).Class) { Slot = K; break; }
		}
		FrontEnd->Character = Char;
		FrontEnd->BoardIndex = Slot;
		FrontEnd->BoardClass = T.Board(Char, Slot).Class;
		FrontEnd->Outfit = 2;
		FrontEnd->Cheats.bMaxAttributes = (Char % 2) == 1;   // 0x3352AC
		FrontEnd->bHasSelection = true;

		ASnowboarderPawn* Pawn = World->SpawnActor<ASnowboarderPawn>();
		if (!TestNotNull(TEXT("pawn"), Pawn)) return false;
		ASSXGameMode::ApplySelection(*FrontEnd, *Pawn);
		TestEqual(TEXT("pawn character"), Pawn->Character, Char);
		TestEqual(TEXT("pawn board slot"), Pawn->Board, Slot);
		TestEqual(TEXT("pawn outfit"), Pawn->Outfit, 2);
		TestTrue(TEXT("player options set"), Pawn->PlayerOptions.IsSet() && Pawn->PlayerOptions->Board == Slot
			&& Pawn->PlayerOptions->bCheatAlt == FrontEnd->Cheats.bMaxAttributes);
		Pawn->BoardClass = 2;
		Pawn->ResolveBoardSelection();
		TestEqual(TEXT("board class from the board table"), Pawn->BoardClass, T.Board(Char, Slot).Class);

		FRiderSim Sim;
		Pawn->ApplyRiderProfile(Sim);
		SSXRider::FPlayerOptions O;
		O.Board = Slot;
		O.bCheatAlt = FrontEnd->Cheats.bMaxAttributes;
		const SSXRider::FBuiltProfile Want = SSXRider::BuildPlayerProfile(T, Char, O);
		TestTrue(FString::Printf(TEXT("rider %d: profile = BuildPlayerProfile(board %d)"), Char, Slot), SameProfile(Sim.Profile, Want));
		TestEqual(TEXT("profile board class"), Sim.Profile.BoardClass, T.Board(Char, Slot).Class);

		ASnowboarderPawn* AI = World->SpawnActor<ASnowboarderPawn>();
		AI->bAIRider = true;
		AI->Character = Char;
		AI->Board = Slot;
		AI->ResolveBoardSelection();
		FRiderSim AiSim;
		AI->ApplyRiderProfile(AiSim);
		TestTrue(TEXT("AI profile = BuildComputerProfile"), SameProfile(AiSim.Profile, SSXRider::BuildComputerProfile(T, Char, true, 0, Slot)));
		TestEqual(TEXT("AI board class"), AI->BoardClass, T.Board(Char, Slot).Class);
		++Checked;
	}
	TestEqual(TEXT("riders checked"), Checked, SSXRider::NumRiders);

	for (int32 Char = 0; Char < SSXRider::NumRiders; ++Char)
	{
		const FString Id = SSXCharacters::IdForIndex(Char);
		if (!SSXCharacters::LoadRiderMesh(Id))
		{
			continue;
		}
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ssx.Character"));
		const FString Saved = CVar ? CVar->GetString() : FString();
		if (CVar) CVar->Set(TEXT(""));
		ASnowboarderPawn* Pawn = World->SpawnActor<ASnowboarderPawn>();
		USceneComponent* RiderRoot = nullptr;
		USceneComponent* BodyRoot = nullptr;
		TArray<USceneComponent*> Scene;
		Pawn->GetComponents(Scene);
		for (USceneComponent* C : Scene)
		{
			if (C->GetFName() == TEXT("RiderRoot")) RiderRoot = C;
			if (C->GetFName() == TEXT("BodyRoot")) BodyRoot = C;
		}
		const int32 Board = 3, Outfit = 1;
		TestTrue(TEXT("apply with board / outfit"), Pawn->GetAppearance()->Apply(RiderRoot, BodyRoot, FString(), Char, T.Board(Char, Board).Class, Board, Outfit));
		const TArray<FString>& Applied = Pawn->GetAppearance()->GetAppliedTextures();
		const FString WantBoard = SSXCharacters::OutfitTexturePath(Id, TEXT("bord"), Board + 1);
		const FString WantSuit = SSXCharacters::OutfitTexturePath(Id, TEXT("suit"), Outfit + 1);
		if (FPaths::FileExists(WantBoard)) TestTrue(TEXT("board graphic applied"), Applied.Contains(WantBoard));
		if (FPaths::FileExists(WantSuit)) TestTrue(TEXT("outfit suit applied"), Applied.Contains(WantSuit));
		AddInfo(FString::Printf(TEXT("%s: %d selection textures applied"), *Id, Applied.Num()));
		if (CVar) CVar->Set(*Saved);
		break;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWiringHudFeedTest, "SSX.Wiring.HudFeed", WiringTestFlags)

bool FSSXWiringHudFeedTest::RunTest(const FString& Parameters)
{
	FRiderSim Sim;
	RegisterAirStates(Sim);
	Sim.Reset();
	SSXAir::FAirContext* Air = SSXAir::GetAirContext(Sim);
	if (!TestNotNull(TEXT("air context"), Air)) return false;
	SSXAir::FTrickScore& Score = Air->Score;

	for (int32 F = 1; F <= 36; ++F)
	{
		Score.UpdateAngles(10.0 * F, 0.0);
	}
	FSSXHudPlayerInput Pl;
	ASSXHUD::FillRider(Sim, Pl);
	TestEqual(TEXT("trick value V"), Pl.TrickValue, Score.Value);
	TestTrue(TEXT("V = 0.14"), FMath::IsNearlyEqual(Pl.TrickValue, 2.0 * SSXAir::TrickValueSpinStep, 1e-9));
	TestEqual(TEXT("M_pts"), Pl.PointsMultiplier, 1.0);

	FSSXHudOverlay Hud;
	Hud.Activate(1, 0, 1, 0, nullptr, 0.0);
	FSSXHudInput In;
	In.Players.Add(Pl);
	Hud.Update(In);
	const float Base = 10000.f * (float)SSXAir::MeterScale * (float)Pl.TrickValue;
	TestEqual(TEXT("pending points S[2]"), Hud.GetPlayer(0).PointsPending, FMath::RoundToFloat(Base / 10.f) * 10.f);
	TestTrue(TEXT("points roll toward the value"), Hud.GetPlayer(0).PointsShown > 0.f);
	TestTrue(TEXT("boost preview moves"), Hud.GetPlayer(0).PendingShown > 0.f);

	Score.Land(false, false);
	ASSXHUD::FillRider(Sim, Pl);
	TestEqual(TEXT("combo count after a ground landing"), Pl.ComboCount, 0);
	TestEqual(TEXT("V cleared"), Pl.TrickValue, 0.0);

	Score.UpdateAngles(180.0, 0.0);
	Score.PendingUbers = 3;
	Score.Land(false, true);
	ASSXHUD::FillRider(Sim, Pl);
	TestEqual(TEXT("uber count -> letters"), Pl.UberCount, 3);
	TestEqual(TEXT("combo 1 after a to-rail landing"), Pl.ComboCount, 1);

	Score.Bail();
	ASSXHUD::FillRider(Sim, Pl);
	TestEqual(TEXT("combo cleared by a bail"), Pl.ComboCount, 0);

	Sim.State.BoostMeter = 0.5;
	Sim.State.TrickyTimer = 12.0;
	ASSXHUD::FillRider(Sim, Pl);
	TestEqual(TEXT("boost meter"), Pl.BoostMeter, 0.5);
	TestEqual(TEXT("tricky timer"), Pl.TrickyTimer, 12.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWiringTrackLaunchTest, "SSX.Wiring.TrackLaunch", WiringTestFlags)

bool FSSXWiringTrackLaunchTest::RunTest(const FString& Parameters)
{
	int32 Reached = 0;
	for (int32 TrackId = 0; TrackId < 16; ++TrackId)
	{
		const int32 Index = SSXFeTables::TrackCatalogIndex(TrackId);
		if (Index == INDEX_NONE)
		{
			continue;
		}
		const FSSXTrackInfo& Info = SSXFrontEnd::Tracks()[Index];
		TestTrue(FString::Printf(TEXT("map %s built"), *Info.MapName), SSXFrontEnd::MapExists(Info.MapName));
		TestTrue(FString::Printf(TEXT("SimData for %s"), *Info.Id),
			FPaths::FileExists(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Info.Id + TEXT(".patches.bin")))
			&& FPaths::FileExists(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Info.Id + TEXT(".race.bin"))));
		USSXFrontEndSubsystem* FrontEnd = NewObject<USSXFrontEndSubsystem>(NewObject<UGameInstance>());
		FrontEnd->bSuppressTravel = true;
		FSSXFeLaunch L;
		L.Track = TrackId;
		const ESSXLoadResult R = FrontEnd->RequestLaunch(L);
		if (TestEqual(FString::Printf(TEXT("launch of track %d"), TrackId), (int32)R, (int32)ESSXLoadResult::Loading))
		{
			TestEqual(TEXT("travel target"), FrontEnd->LastRequestedMap, SSXFrontEnd::MapPackage(Info.MapName));
			++Reached;
		}
	}
	TestEqual(TEXT("every catalogue track reached"), Reached, SSXFrontEnd::Tracks().Num());
	return true;
}

#endif
