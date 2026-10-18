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
	/** Engine cvars the Remastered look sets; Classic puts back the values they had before (the project settings). */
	SSXTRICKYREBUILD_API TConstArrayView<FCvarValue> RemasteredCvars();
	/** Remastered: remember each cvar's current value (once) and set the new one. Classic: restore every remembered value.
	 *  Unknown cvars are skipped. */
	SSXTRICKYREBUILD_API void ApplyCvars(TConstArrayView<FCvarValue> Cvars, ESSXRenderLook Look);
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

	/** Store the current ssx.Render.Look and write the ini. */
	static void SaveLook();
};

/** Applies the saved look when the game starts. */
UCLASS()
class SSXTRICKYREBUILD_API USSXRenderLookSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
