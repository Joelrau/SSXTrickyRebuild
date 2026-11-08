#include "World/SSXRenderLook.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"

#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SSXRenderLook)

#ifndef SSX_CLASSIC_ONLY
#define SSX_CLASSIC_ONLY 0
#endif

namespace
{
#if !UE_BUILD_SHIPPING
	bool& ClassicOnlyFlag()
	{
		static bool bClassicOnly = FParse::Param(FCommandLine::Get(), TEXT("classiconly"));
		return bClassicOnly;
	}
#endif

	ESSXRenderLook FromInt(int32 V) { return V > 0 ? ESSXRenderLook::Remastered : ESSXRenderLook::Classic; }

	ESSXRenderLook GApplied = SSXRenderLook::Default;
	TMap<FString, FString> GClassicValues;

	void OnLookCvar(IConsoleVariable* Var)
	{
		const ESSXRenderLook Look = SSXRenderLook::Get();
		if (Look == GApplied) { return; }
		GApplied = Look;
		SSXRenderLook::ApplyCvars(SSXRenderLook::RemasteredCvars(), Look);
		SSXRenderLook::OnChanged().Broadcast(Look);
	}

	void OnQualityCvar(IConsoleVariable* Var)
	{
		if (GApplied == ESSXRenderLook::Remastered)
		{
			SSXRenderLook::ApplyCvars(SSXRenderLook::RemasteredCvars(), ESSXRenderLook::Remastered);
		}
	}

	TAutoConsoleVariable<int32> CVarQuality(TEXT("ssx.Render.Quality"), SSXRenderLook::DefaultQuality,
		TEXT("Enhanced look quality: 0 = Low (no Lumen, 67 % resolution), 1 = Medium, 2 = High (default), 3 = Epic. ")
		TEXT("No effect on the Classic look."),
		FConsoleVariableDelegate::CreateStatic(&OnQualityCvar));

	// PC-only deviations (user request), like the Remastered look itself.
	TAutoConsoleVariable<int32> CVarUpscaler(TEXT("ssx.Render.Upscaler"), SSXRenderLook::DefaultUpscaler,
		TEXT("Enhanced look upscaler (PC-only, user request): 0 = off (the full resolution, Unreal TSR anti-aliasing), ")
		TEXT("1 = Unreal TSR (default), 2 = AMD FSR (with the optional FSR plugin; TSR without it). No effect on the Classic look."),
		FConsoleVariableDelegate::CreateStatic(&OnQualityCvar));

	TAutoConsoleVariable<int32> CVarUpscaleQuality(TEXT("ssx.Render.UpscaleQuality"), 0,
		TEXT("Enhanced look render resolution with an upscaler on (PC-only, user request): 0 = Auto (the quality preset's), 1 = Native, ")
		TEXT("2 = Quality (67 %), 3 = Balanced (59 %), 4 = Performance (50 %), 5 = Ultra Performance (33 %). ")
		TEXT("No effect on the Classic look."),
		FConsoleVariableDelegate::CreateStatic(&OnQualityCvar));

	// PC-only deviation (user request): Remastered is not part of the original game.
	TAutoConsoleVariable<int32> CVarLook(TEXT("ssx.Render.Look"), int32(SSXRenderLook::Default),
		TEXT("Render look: 0 = Classic, the PS2-faithful presentation (default); 1 = Enhanced (PC-only deviation: ")
		TEXT("modern lighting, materials and post; gameplay, audio, HUD and replays unchanged)."),
		FConsoleVariableDelegate::CreateStatic(&OnLookCvar));
}

namespace SSXRenderLook
{
	bool IsClassicOnly()
	{
#if SSX_CLASSIC_ONLY
		return true;
#elif !UE_BUILD_SHIPPING
		return ClassicOnlyFlag();
#else
		return false;
#endif
	}

#if !UE_BUILD_SHIPPING
	void SetClassicOnlyForTest(bool bClassicOnly) { ClassicOnlyFlag() = bClassicOnly; }
#endif

	ESSXRenderLook Get() { return IsClassicOnly() ? ESSXRenderLook::Classic : FromInt(CVarLook.GetValueOnGameThread()); }

	void Set(ESSXRenderLook Look)
	{
		if (!IsClassicOnly()) { CVarLook->Set(int32(Look), ECVF_SetByCode); }
	}

	FOnChanged& OnChanged()
	{
		static FOnChanged Delegate;
		return Delegate;
	}

	int32 GetQuality() { return FMath::Clamp(CVarQuality.GetValueOnGameThread(), 0, NumQualities - 1); }

	void SetQuality(int32 Quality) { CVarQuality->Set(FMath::Clamp(Quality, 0, NumQualities - 1), ECVF_SetByCode); }

	bool QualityUsesLumen(int32 Quality) { return Quality > 0; }

	int32 GetUpscaler() { return FMath::Clamp(CVarUpscaler.GetValueOnGameThread(), 0, NumUpscalers - 1); }

	void SetUpscaler(int32 Upscaler) { CVarUpscaler->Set(FMath::Clamp(Upscaler, 0, NumUpscalers - 1), ECVF_SetByCode); }

	int32 GetUpscaleQuality() { return FMath::Clamp(CVarUpscaleQuality.GetValueOnGameThread(), 0, NumUpscaleQualities - 1); }

	void SetUpscaleQuality(int32 UpscaleQuality)
	{
		CVarUpscaleQuality->Set(FMath::Clamp(UpscaleQuality, 0, NumUpscaleQualities - 1), ECVF_SetByCode);
	}

	float UpscalePercent(int32 UpscaleQuality, int32 Quality)
	{
		// the upscalers' common modes: per axis 1, 1 / 1.5, 1 / 1.7, 1 / 2 and 1 / 3 of the output resolution
		static const float Modes[NumUpscaleQualities] = { 0.f, 100.f, 66.7f, 58.8f, 50.f, 33.3f };
		static const float Auto[NumQualities] = { 67.f, 75.f, 100.f, 100.f };   // Low .. Epic
		const int32 U = FMath::Clamp(UpscaleQuality, 0, NumUpscaleQualities - 1);
		return U == 0 ? Auto[FMath::Clamp(Quality, 0, NumQualities - 1)] : Modes[U];
	}

	bool FsrLoaded()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FSR"));
		return !IsClassicOnly() && Plugin.IsValid() && Plugin->IsEnabled();
	}

	TConstArrayView<FCvarValue> RemasteredCvars()
	{
		// GI 1 / reflections 1 = Lumen, reflections 2 = screen space. Epic: Lumen on hardware ray tracing (needs
		// r.RayTracing in DefaultEngine.ini and an RT GPU; Lumen falls back to software tracing otherwise). Nanite: the track
		// meshes (Scripts/enable_nanite.py); Classic keeps r.Nanite 0 (DefaultEngine.ini), their full-detail fallback.
		// r.Nanite.VSMInvalidateOnLODDelta: a cached shadow page drawn before its Nanite detail streamed in is drawn again
		// once it has (the hidden terrain shadow twins of Alaska and Aloha stream only for shadow passes).
		// Shadows: Epic takes the engine's Epic tier like High (the Cinematic tier doubles the soft-shadow rays and samples and
		// quarters the volumetric fog cells) with the Cinematic page pool, as every one of its lamps casts a shadow.
		static const FCvarValue Cvars[NumQualities][11] = {
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("0") }, { TEXT("r.ReflectionMethod"), TEXT("2") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("1") }, { TEXT("sg.ReflectionQuality"), TEXT("1") },
			  { TEXT("sg.ShadowQuality"), TEXT("1") }, { TEXT("r.VolumetricFog"), TEXT("0") },
			  { TEXT("sg.EffectsQuality"), TEXT("1") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("0") }, { TEXT("r.Nanite"), TEXT("1") },
			  { TEXT("r.Nanite.VSMInvalidateOnLODDelta"), TEXT("1") },
			  { TEXT("r.Shadow.Virtual.MaxPhysicalPages"), TEXT("512") } },
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1") }, { TEXT("r.ReflectionMethod"), TEXT("1") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("1") }, { TEXT("sg.ReflectionQuality"), TEXT("1") },
			  { TEXT("sg.ShadowQuality"), TEXT("2") }, { TEXT("r.VolumetricFog"), TEXT("0") },
			  { TEXT("sg.EffectsQuality"), TEXT("2") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("0") }, { TEXT("r.Nanite"), TEXT("1") },
			  { TEXT("r.Nanite.VSMInvalidateOnLODDelta"), TEXT("1") },
			  { TEXT("r.Shadow.Virtual.MaxPhysicalPages"), TEXT("2048") } },
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1") }, { TEXT("r.ReflectionMethod"), TEXT("1") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("2") }, { TEXT("sg.ReflectionQuality"), TEXT("2") },
			  { TEXT("sg.ShadowQuality"), TEXT("3") }, { TEXT("r.VolumetricFog"), TEXT("1") },
			  { TEXT("sg.EffectsQuality"), TEXT("3") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("0") }, { TEXT("r.Nanite"), TEXT("1") },
			  { TEXT("r.Nanite.VSMInvalidateOnLODDelta"), TEXT("1") },
			  { TEXT("r.Shadow.Virtual.MaxPhysicalPages"), TEXT("4096") } },
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1") }, { TEXT("r.ReflectionMethod"), TEXT("1") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("3") }, { TEXT("sg.ReflectionQuality"), TEXT("3") },
			  { TEXT("sg.ShadowQuality"), TEXT("3") }, { TEXT("r.VolumetricFog"), TEXT("1") },
			  { TEXT("sg.EffectsQuality"), TEXT("4") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("1") }, { TEXT("r.Nanite"), TEXT("1") },
			  { TEXT("r.Nanite.VSMInvalidateOnLODDelta"), TEXT("1") },
			  { TEXT("r.Shadow.Virtual.MaxPhysicalPages"), TEXT("8192") } },
		};
		// Then the upscaler and the render resolution, r.ScreenPercentage last: the FSR plugin sets it itself whenever FSR is
		// switched or changes mode, and the look's value must win.
		static TArray<FCvarValue> Out;
		static FString FsrMode, FsrOn, Percent;
		const int32 Quality = GetQuality();
		const float Pct = GetUpscaler() == 0 ? 100.f : UpscalePercent(GetUpscaleQuality(), Quality);
		Out.Reset();
		Out.Append(Cvars[Quality], UE_ARRAY_COUNT(Cvars[Quality]));
		if (FsrLoaded())
		{
			// FSR modes 0 Native AA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance
			FsrMode = FString::FromInt(GetUpscaleQuality() > 0 ? GetUpscaleQuality() - 1 : (Pct >= 100.f ? 0 : 1));
			FsrOn = GetUpscaler() == 2 ? TEXT("1") : TEXT("0");
			Out.Add({ TEXT("r.FidelityFX.FSR.QualityMode"), *FsrMode });
			Out.Add({ TEXT("r.FidelityFX.FSR.Enabled"), *FsrOn });
		}
		Percent = FString::SanitizeFloat(Pct);
		Out.Add({ TEXT("r.ScreenPercentage"), *Percent });
		return Out;
	}

	void ApplyCvars(TConstArrayView<FCvarValue> Cvars, ESSXRenderLook Look)
	{
		for (const FCvarValue& C : Cvars)
		{
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(C.Name);
			if (!Var) { continue; }
			if (Look == ESSXRenderLook::Remastered)
			{
				if (!GClassicValues.Contains(C.Name)) { GClassicValues.Add(C.Name, Var->GetString()); }
				Var->Set(C.Value, ECVF_SetByCode);
			}
			else if (const FString* Old = GClassicValues.Find(C.Name))
			{
				Var->Set(**Old, ECVF_SetByCode);
				GClassicValues.Remove(C.Name);
			}
		}
	}
}

namespace SSXRenderLook
{
	namespace
	{
		IConsoleVariable* FrameGenerationCvar() { return IConsoleManager::Get().FindConsoleVariable(TEXT("r.FidelityFX.FI.Enabled")); }
	}

	bool FrameGenerationAvailable() { return FsrLoaded() && FrameGenerationCvar() != nullptr; }

	FString FrameGenerationName()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FSR"));
		FString Major;
		if (Plugin.IsValid())
		{
			Plugin->GetDescriptor().VersionName.Split(TEXT("."), &Major, nullptr);
			if (Major.IsEmpty()) { Major = Plugin->GetDescriptor().VersionName; }
		}
		return Major.IsEmpty() ? FString(TEXT("AMD FSR")) : FString::Printf(TEXT("AMD FSR %s"), *Major);
	}

	bool GetFrameGeneration()
	{
		const IConsoleVariable* V = FrameGenerationCvar();
		return V && V->GetInt() != 0;
	}

	void SetFrameGeneration(bool bOn)
	{
		// by code: above the off default in DefaultEngine.ini [SystemSettings], which would win over a game setting
		if (IConsoleVariable* V = FrameGenerationCvar()) { V->Set(bOn ? 1 : 0, ECVF_SetByCode); }
	}
}

void USSXVideoSettings::Save()
{
	USSXVideoSettings* S = GetMutableDefault<USSXVideoSettings>();
	S->Look = int32(SSXRenderLook::Get());
	S->Quality = SSXRenderLook::GetQuality();
	S->FrameGeneration = SSXRenderLook::GetFrameGeneration() ? 1 : 0;
	S->Upscaler = SSXRenderLook::GetUpscaler();
	S->UpscaleQuality = SSXRenderLook::GetUpscaleQuality();
	S->SaveConfig();
}

void USSXRenderLookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (SSXRenderLook::IsClassicOnly() && !GIsEditor && !GIsAutomationTesting && GEngine)
	{
		// Deliberate (PC): no VIDEO page to choose a mode on, so always borderless at the desktop size. Runs before the window exists.
		UGameUserSettings* G = GEngine->GetGameUserSettings();
		G->SetScreenResolution(G->GetDesktopResolution());
		G->SetFullscreenMode(EWindowMode::WindowedFullscreen);
		G->ApplyResolutionSettings(true);
		IConsoleManager::Get().CallAllConsoleVariableSinks();
	}
	if (GIsAutomationTesting || SSXRenderLook::IsClassicOnly())
	{
		return;   // tests run the Classic default unless they set the look themselves, whatever the user saved
	}
	SSXRenderLook::SetQuality(GetDefault<USSXVideoSettings>()->Quality);
	SSXRenderLook::SetUpscaler(GetDefault<USSXVideoSettings>()->Upscaler);
	SSXRenderLook::SetUpscaleQuality(GetDefault<USSXVideoSettings>()->UpscaleQuality);
	SSXRenderLook::Set(FromInt(GetDefault<USSXVideoSettings>()->Look));
	SSXRenderLook::SetFrameGeneration(GetDefault<USSXVideoSettings>()->FrameGeneration != 0);
}
