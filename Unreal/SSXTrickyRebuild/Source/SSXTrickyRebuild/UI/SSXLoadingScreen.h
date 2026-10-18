#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXLoadReady.h"
#include "UI/SSXTrackLoad.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "SSXLoadingScreen.generated.h"

class UAudioComponent;
class UGameViewportClient;
class UMediaPlayer;
class UMediaTexture;
class USoundWaveProcedural;
class UPackage;
class IInputProcessor;

UCLASS()
class SSXTRICKYREBUILD_API USSXLoadingScreen : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static USSXLoadingScreen* Get(const UObject* WorldContext);
	static bool HoldsGame(const UObject* WorldContext);

	virtual void Deinitialize() override;

	void Begin(const FSSXFeLaunch& Launch, const FString& MapPackage);
	/** Build a race -> FE loader (cGame::vfunc_06: cFELoadFromQuit, c1PFELoad, ...) and travel to the FE map; the same PC
	 *  rule applies (up until the FE is loaded, the page intro plays to its end, X / Start skips only once loaded). With
	 *  Pick.bRankVideo the rank video (FSSXRankVideo, data/video/rr_<rider>) plays first, then the loader is built
	 *  (cVideoModule::vfunc_06). L = the event that just ended. */
	void BeginReturn(const SSXTrackLoad::FReturnPick& Pick, const FSSXFeLaunch& L, const FString& MapPackage);
	bool IsActive() const
	{
		return RankVideo.IsPlaying() || (Loader.Phase != FSSXTrackLoader::EPhase::Off && Loader.Phase != FSSXTrackLoader::EPhase::Done);
	}
	const FSSXRankVideo& GetRankVideo() const { return RankVideo; }
	bool PressSkip();
	const FSSXTrackLoader& GetLoader() const { return Loader; }
	const FSSXFeDrawList& GetDrawList() const { return DrawList; }
	bool IsLoaded() const { return bLoaded; }

private:
	void StartLoad(const FString& MapPackage);
	bool TickLoader(float DeltaTime);
	void StepOnce();
	void UpdateLoadState();
	void Travel();
	void End();
	class UGameViewportClient* ViewportClient() const;
	void ShowOverlay();
	void HideOverlay();
	void OnPackageLoaded(const FName& Name, UPackage* Package, EAsyncLoadingResult::Type Result);
	void PlaySideEffects();
	void StartRankMovie();
	void TickRankMovie(float DeltaTime);
	void StopRankMovie();
	void BuildPendingReturn();
	/** cGame::vfunc_01 (the race module deleted before the next module and its loader voice, or before the rank video):
	 *  SSXGameAudio::OnRaceUnload on the race world, once per load; nothing when the running world is the front end. */
	void UnloadRaceModule();
	bool bModuleUnloaded = false;

	FSSXTrackLoader Loader;
	FSSXRankVideo RankVideo;
	SSXTrackLoad::FReturnPick PendingPick;
	FSSXFeLaunch PendingLaunch;
	FString PendingMap;
	FString PlayingMovieName;
	float MovieClock = 0.f;
	bool bMovieStarted = false;
	bool bMovieVideoEnded = false;
	double MovieAudioSeconds = 0.0;
	double MovieAudioClock = 0.0;
	UPROPERTY(Transient) TObjectPtr<UMediaPlayer> MediaPlayer;
	UPROPERTY(Transient) TObjectPtr<UMediaTexture> MediaTexture;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> MovieAudio;
	UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> MovieWave;
	FSSXTrackLoadAssets Art;
	FSSXFeDrawList DrawList;
	TSharedPtr<SSSXFeCanvas> Canvas;
	TWeakObjectPtr<UGameViewportClient> OverlayHost;
	TWeakObjectPtr<UGameViewportClient> ViewportOverride;
	friend class FSSXQuitRankVideoOverlayTest;
	TSharedPtr<IInputProcessor> Input;
	FTSTicker::FDelegateHandle TickHandle;
	float Accum = 0.f;

	FString Target;
	bool bPreloading = false;
	bool bPackageReady = false;
	bool bTravelled = false;
	int32 WorldFrames = 0;
	TStrongObjectPtr<UPackage> Preloaded;
	/** The preloaded package's world, held too: a package does not keep its objects alive, and in a cooked game the GC in LoadMap collects the unreferenced UWorld while the package stays, so LoadMap fails ("Failed to load package") and drops back to the front end. */
	TStrongObjectPtr<UWorld> PreloadedWorld;
	bool bLoaded = false;
	int32 Progress = 0;
	/** PC addition: hold until nothing streams / compiles; the new world's own streamed textures (forced resident, watched by the hold). */
	FSSXLoadGate ReadyGate;
	SSXLoadReady::FTextureList WorldTextures;

	UPROPERTY(Transient) TObjectPtr<UAudioComponent> Music;
	int32 SeenMusicStarts = 0, SeenMusicStops = 0, SeenTypeSounds = 0;
};
