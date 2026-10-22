// Render look: Classic = the PS2-faithful presentation (default), Remastered = modern lighting, materials and post
// (PC-only deviation, user request). Render-only: nothing behind the switch writes sim, audio, HUD or replay state or
// draws from FGameRng (Remastered effects use their own random numbers).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SSXRenderLook.generated.h"

UENUM()
enum class ESSXRenderLook : uint8
{
	Classic = 0,
	Remastered = 1,
};

namespace SSXRenderLook
{
	constexpr ESSXRenderLook Default = ESSXRenderLook::Classic;

	/** ssx.Render.Look: 0 Classic, anything above 0 Remastered. */
	SSXTRICKYREBUILD_API ESSXRenderLook Get();
	/** Set ssx.Render.Look; OnChanged fires when the look changes. */
	SSXTRICKYREBUILD_API void Set(ESSXRenderLook Look);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnChanged, ESSXRenderLook);
	SSXTRICKYREBUILD_API FOnChanged& OnChanged();

	struct FCvarValue
	{
		const TCHAR* Name;
		const TCHAR* Value;
	};
	/** Remastered quality preset (ssx.Render.Quality): 0 Low, 1 Medium, 2 High (default), 3 Epic. Classic ignores it. */
	constexpr int32 NumQualities = 4;
	constexpr int32 DefaultQuality = 2;
	SSXTRICKYREBUILD_API int32 GetQuality();
	SSXTRICKYREBUILD_API void SetQuality(int32 Quality);
	/** Lumen GI and reflections at this preset (Low: none, screen-space reflections). */
	SSXTRICKYREBUILD_API bool QualityUsesLumen(int32 Quality);

	/** Engine cvars the Remastered look sets at the current quality (the same names at every quality); Classic puts
	 *  back the values they had before (the project settings). */
	SSXTRICKYREBUILD_API TConstArrayView<FCvarValue> RemasteredCvars();
	/** Remastered: remember each cvar's current value (once) and set the new one. Classic: restore every remembered value.
	 *  Unknown cvars are skipped. */
	SSXTRICKYREBUILD_API void ApplyCvars(TConstArrayView<FCvarValue> Cvars, ESSXRenderLook Look);

	/** Frame generation (PC only, user request): AMD FSR Frame Generation through the optional FSR plugin
	 *  (r.FidelityFX.FI.Enabled). Unavailable when the plugin is not installed. */
	SSXTRICKYREBUILD_API bool FrameGenerationAvailable();
	/** "AMD FSR <major version of the installed FSR plugin>". */
	SSXTRICKYREBUILD_API FString FrameGenerationName();
	SSXTRICKYREBUILD_API bool GetFrameGeneration();
	SSXTRICKYREBUILD_API void SetFrameGeneration(bool bOn);
}

/** PC video options (Video page), saved in GameUserSettings.ini, never on the memory card. */
UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class SSXTRICKYREBUILD_API USSXVideoSettings : public UObject
{
	GENERATED_BODY()

public:
	/** ESSXRenderLook. */
	UPROPERTY(config)
	int32 Look = int32(SSXRenderLook::Default);

	/** ssx.Render.Quality. */
	UPROPERTY(config)
	int32 Quality = SSXRenderLook::DefaultQuality;

	/** SSXRenderLook::SetFrameGeneration (0 off, 1 on). */
	UPROPERTY(config)
	int32 FrameGeneration = 0;

	/** Store the current ssx.Render.Look / ssx.Render.Quality and write the ini. */
	static void Save();
};

/** Applies the saved look when the game starts. */
UCLASS()
class SSXTRICKYREBUILD_API USSXRenderLookSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
