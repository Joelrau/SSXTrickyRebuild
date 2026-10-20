#include "World/SSXRenderLook.h"

#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SSXRenderLook)

namespace
{
	ESSXRenderLook FromInt(int32 V) { return V > 0 ? ESSXRenderLook::Remastered : ESSXRenderLook::Classic; }

	ESSXRenderLook GApplied = SSXRenderLook::Default;
	TMap<FString, FString> GClassicValues;

	void OnLookCvar(IConsoleVariable* Var)
	{
		const ESSXRenderLook Look = FromInt(Var->GetInt());
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
		TEXT("Remastered look quality: 0 = Low (no Lumen, 67 % resolution), 1 = Medium, 2 = High (default), 3 = Epic. ")
		TEXT("No effect on the Classic look."),
		FConsoleVariableDelegate::CreateStatic(&OnQualityCvar));

	// PC-only deviation (user request): Remastered is not part of the original game.
	TAutoConsoleVariable<int32> CVarLook(TEXT("ssx.Render.Look"), int32(SSXRenderLook::Default),
		TEXT("Render look: 0 = Classic, the PS2-faithful presentation (default); 1 = Remastered (PC-only deviation: ")
		TEXT("modern lighting, materials and post; gameplay, audio, HUD and replays unchanged)."),
		FConsoleVariableDelegate::CreateStatic(&OnLookCvar));
}

namespace SSXRenderLook
{
	ESSXRenderLook Get() { return FromInt(CVarLook.GetValueOnGameThread()); }

	void Set(ESSXRenderLook Look) { CVarLook->Set(int32(Look), ECVF_SetByCode); }

	FOnChanged& OnChanged()
	{
		static FOnChanged Delegate;
		return Delegate;
	}

	int32 GetQuality() { return FMath::Clamp(CVarQuality.GetValueOnGameThread(), 0, NumQualities - 1); }

	void SetQuality(int32 Quality) { CVarQuality->Set(FMath::Clamp(Quality, 0, NumQualities - 1), ECVF_SetByCode); }

	bool QualityUsesLumen(int32 Quality) { return Quality > 0; }

	TConstArrayView<FCvarValue> RemasteredCvars()
	{
		// GI 1 / reflections 1 = Lumen, reflections 2 = screen space. Epic: Lumen on hardware ray tracing (needs
		// r.RayTracing in DefaultEngine.ini and an RT GPU; Lumen falls back to software tracing otherwise). Nanite: the track
		// meshes (Scripts/enable_nanite.py); Classic keeps r.Nanite 0 (DefaultEngine.ini), their full-detail fallback.
		static const FCvarValue Cvars[NumQualities][10] = {
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("0") }, { TEXT("r.ReflectionMethod"), TEXT("2") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("1") }, { TEXT("sg.ReflectionQuality"), TEXT("1") },
			  { TEXT("sg.ShadowQuality"), TEXT("1") }, { TEXT("r.VolumetricFog"), TEXT("0") },
			  { TEXT("r.ScreenPercentage"), TEXT("67") }, { TEXT("sg.EffectsQuality"), TEXT("1") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("0") }, { TEXT("r.Nanite"), TEXT("1") } },
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1") }, { TEXT("r.ReflectionMethod"), TEXT("1") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("1") }, { TEXT("sg.ReflectionQuality"), TEXT("1") },
			  { TEXT("sg.ShadowQuality"), TEXT("2") }, { TEXT("r.VolumetricFog"), TEXT("0") },
			  { TEXT("r.ScreenPercentage"), TEXT("75") }, { TEXT("sg.EffectsQuality"), TEXT("2") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("0") }, { TEXT("r.Nanite"), TEXT("1") } },
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1") }, { TEXT("r.ReflectionMethod"), TEXT("1") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("2") }, { TEXT("sg.ReflectionQuality"), TEXT("2") },
			  { TEXT("sg.ShadowQuality"), TEXT("3") }, { TEXT("r.VolumetricFog"), TEXT("1") },
			  { TEXT("r.ScreenPercentage"), TEXT("100") }, { TEXT("sg.EffectsQuality"), TEXT("3") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("0") }, { TEXT("r.Nanite"), TEXT("1") } },
			{ { TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("1") }, { TEXT("r.ReflectionMethod"), TEXT("1") },
			  { TEXT("sg.GlobalIlluminationQuality"), TEXT("3") }, { TEXT("sg.ReflectionQuality"), TEXT("3") },
			  { TEXT("sg.ShadowQuality"), TEXT("4") }, { TEXT("r.VolumetricFog"), TEXT("1") },
			  { TEXT("r.ScreenPercentage"), TEXT("100") }, { TEXT("sg.EffectsQuality"), TEXT("4") },
			  { TEXT("r.Lumen.HardwareRayTracing"), TEXT("1") }, { TEXT("r.Nanite"), TEXT("1") } },
		};
		return Cvars[GetQuality()];
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

void USSXVideoSettings::Save()
{
	USSXVideoSettings* S = GetMutableDefault<USSXVideoSettings>();
	S->Look = int32(SSXRenderLook::Get());
	S->Quality = SSXRenderLook::GetQuality();
	S->SaveConfig();
}

void USSXRenderLookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GIsAutomationTesting)
	{
		return;   // tests run the Classic default unless they set the look themselves, whatever the user saved
	}
	SSXRenderLook::SetQuality(GetDefault<USSXVideoSettings>()->Quality);
	SSXRenderLook::Set(FromInt(GetDefault<USSXVideoSettings>()->Look));
}
