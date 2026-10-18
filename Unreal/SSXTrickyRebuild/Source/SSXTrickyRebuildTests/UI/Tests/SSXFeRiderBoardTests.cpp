// FUN_00266208 / 0x116A68 / 0x2021F8 / FUN_00201138
#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Sim/Rider/RiderProfiles.h"
#include "UI/SSXFeRiderActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXFeRiderBoardModelTest, "SSX.UI.FeRiderBoardModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXFeRiderBoardModelTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("FE descriptor stance +0x40 = 0 (FUN_00266208)"), ASSXFeRiderActor::FeProfileGoofy);
	const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
	if (!Table.IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: SimData/riders.bin missing (tools/export_rider_profiles.py)"));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SSXFeRiderBoardTestWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	int32 Checked = 0;
	FString Goofies;
	for (int32 Character = 0; Character < SSXRider::NumRiders; ++Character)
	{
		if (Table.Rider(Character).Stance == 0)
		{
			continue;
		}
		const FString Id = SSXCharacters::IdForIndex(Character);
		Goofies += Id + TEXT(" ");
		for (int32 Board = 0; Board < SSXRider::NumBoards; ++Board)
		{
			const int32 Class = Table.Board(Character, Board).Class;
			UStaticMesh* Regular = SSXCharacters::LoadBoardMesh(Id, Class);
			UStaticMesh* Goofy = SSXCharacters::LoadBoardMesh(Id, Class, true);
			if (!Regular || !Goofy)
			{
				continue;
			}
			ASSXFeRiderActor* Actor = World->SpawnActor<ASSXFeRiderActor>();
			if (!TestNotNull(TEXT("FE rider spawned"), Actor) || !Actor->Setup(Character % 12, Character, Board, 0))
			{
				continue;
			}
			Actor->ApplyPose();
			const UStaticMeshComponent* Comp = Actor->GetAppearance()->GetBoardMesh();
			TestTrue(*FString::Printf(TEXT("%s board %d (class %d): FE draws the regular model"), *Id, Board, Class),
				Comp && Comp->GetStaticMesh().Get() == Regular && !Actor->GetAppearance()->IsBoardMeshGoofy());
			Actor->Destroy();
			++Checked;
		}
	}
	AddInfo(FString::Printf(TEXT("goofy riders (record stance): %s; %d rider / board cases"), *Goofies, Checked));
	if (Checked == 0)
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: no goofy rider with imported regular + Goofy board meshes"));
	}
	return true;
}

#endif
