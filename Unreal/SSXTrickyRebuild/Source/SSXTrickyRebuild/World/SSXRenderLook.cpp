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

	TConstArrayView<FCvarValue> RemasteredCvars()
	{
		return {};
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

void USSXVideoSettings::SaveLook()
{
	USSXVideoSettings* S = GetMutableDefault<USSXVideoSettings>();
	S->Look = int32(SSXRenderLook::Get());
	S->SaveConfig();
}

void USSXRenderLookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SSXRenderLook::Set(FromInt(GetDefault<USSXVideoSettings>()->Look));
}
