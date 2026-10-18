#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXTrackLoad.h"
#include "Sim/Rider/RiderProfiles.h"
#include "SSXFrontEndSubsystem.generated.h"

class UTexture2D;
class UWorld;

enum class ESSXLoadResult : uint8 { Loading, MapMissing, NoTrack };

/**
 * Game-instance side of the front end: the profile (career, unlocks, settings), cheats, the launch
 * selection handed to gameplay, and the locally loaded resources (LOC strings, FE textures, SFN fonts,
 * sprite table, FE cameras). Survives the level change between /Game/Maps/FrontEnd and a track.
 */
UCLASS()
class SSXTRICKYREBUILD_API USSXFrontEndSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static USSXFrontEndSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// 0x32B390 / 0x3352A0
	FSSXProfile Profile = FSSXProfile::MakeDefault();
	FSSXCheats Cheats;

	FSSXFeLaunch Launch;
	int32 TrackIndex = INDEX_NONE;
	int32 Character = 10;
	int32 BoardClass = 2;
	int32 BoardIndex = 0;
	int32 Outfit = 0;
	bool bHasSelection = false;
	/** Mix Master Mike cheat: every loaded rider becomes model 12, outfit texture 84, board 0x91, scale 0.8 except in mode 6 (FUN_0016A9D8); applied by the rider/character code. */
	bool IsMixMasterMike() const { return Cheats.bMixMasterMike && Launch.GameMode != 6; }

	/** Player 1's rider-profile options for the selection (0x16A9D8): chosen board, cheat flags 0x3352AC / 0x3352B0 / 0x3352B8, mode 6 and the career attributes rec+0x0C of the rider (base on a new save, raised by the PointDistribution screen). */
	SSXRider::FPlayerOptions MakePlayerOptions() const
	{
		SSXRider::FPlayerOptions O;
		O.Board = BoardIndex;
		if (Character >= 0 && Character < SSXFeTables::NumRiders && SSXRider::FRiderTable::Shared().IsLoaded())
		{
			O.bHaveCareer = true;
			for (int32 A = 0; A < SSXRider::NumAttributes; ++A) { O.Career[A] = Profile.Career[Character].Attr[A]; }
		}
		if (Character >= 0 && Character < SSXFeTables::NumRiders)
		{
			// FUN_00164840 / 0x32B390
			O.Rank = SSXFeTables::RankFromPoints(Profile.Career[Character].RankPoints());
		}
		O.bCheatAlt = Cheats.bMaxAttributes;
		O.bMixMasterMike = Cheats.bMixMasterMike;
		O.bMallora = Cheats.bMallora;
		O.bMode6 = Launch.GameMode == 6;
		return O;
	}

	ESSXLoadResult RequestLaunch(const FSSXFeLaunch& InLaunch);
	/** A memory-card replay loaded by the FE (Options item 2; 0x166800's 0x80000 buffer, +0x3710 set by 0x166870): the race
	 *  set up next takes it (0x180C70 -> 0x1668A0 -> 0x194910) and empties it (0x166840). Race/SSXReplayFile.h. */
	TArray<uint8> PendingReplay;
	FString LastRequestedMap;
	int32 LoadRequests = 0;
	bool bSuppressTravel = false;
	TFunction<bool(const FString&)> MapExistsHook;

	void ReturnToFrontEnd(int32 ExitCode = 2);
	bool bColdBootDone = false;
	int32 LastExitCode = 2;
	// 0x180E00
	SSXTrackLoad::FReturnPick LastReturn;

	/** Push the live settings into the engine every frame (front end and race): the sound state through
	 *  SSXGameAudio::ApplySettings and the brightness into MPC_SSXWorld (World may be null). */
	void ApplySettings(UWorld* World = nullptr) const;

	const FSSXLocStrings& Strings();
	UTexture2D* LoadPng(const FString& Path);
	const FSSXFeAssets& Assets();
	const FSSXFeCameraSet& Cameras();

	bool bMenuShots = false;
	int32 MenuShotStage = 0;

private:
	TOptional<FSSXLocStrings> LocStrings;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> WhiteTexture;
	TSet<FString> MissingTextures;
	TOptional<FSSXFeAssets> AssetCache;
	FSSXSpriteTable FeSprites;
	TOptional<FSSXFeCameraSet> CameraSet;
};
