#include "Progression/SSXSettingsEffects.h"

#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Race/RaceRules.h"
#include "World/SSXWorldPresentation.h"

namespace SSXSettings
{
	FWidescreenView WidescreenView(int32 Mode)
	{
		FWidescreenView V;
		switch (Mode)
		{
		case 1:   // 16:9: letterbox, both projection axes x 0.75
			V.XScale = 0.75f;
			V.YScale = 0.75f;
			V.ViewTop = 0.125f;
			V.ViewHeight = 0.75f;
			break;
		case 2:   // Animorphic: x 0.75 only, the full frame (the TV stretches it)
			V.XScale = 0.75f;
			V.YScale = 1.f;
			V.ViewTop = 0.f;
			V.ViewHeight = 1.f;
			break;
		default:  // Off, and modes vfunc_21 does not scale
			break;
		}
		return V;
	}

	float ImageAspect(int32 Mode)
	{
		const FWidescreenView V = WidescreenView(Mode);
		return (4.f / 3.f) * V.YScale / (V.XScale * V.ViewHeight);
	}

	double HorizontalFov(double HalfFovRad, int32 Mode)
	{
		return 2.0 * FMath::Atan(FMath::Tan(HalfFovRad) / double(WidescreenView(Mode).XScale));
	}

	int32 RacerCount(int32 GameMode, int32 Players, int32 MaxBoarders)
	{
		int32 N;
		switch (GameMode)
		{
		// 0x2657A8 stores max boarders (humans in 1 / 7, 1 in 6), then 0x163980 -> 0x165558 rebuilds RS+0 by mode:
		// 0 -> 6; 2 keeps max boarders; 3, 1, 7 and any other mode -> 0x1664B0 = the humans (pads with a rider);
		// 4 / 5 -> the WC heat set-up; 6 -> 0x165778.
		case 6: N = 1; break;                   // trick tutorial
		case 0: N = 6; break;                   // knockdown (unreachable from the menus)
		case 2: N = MaxBoarders; break;         // single event race
		case 4: N = 6; break;                   // World Circuit race: heat set-up RS+0 = 6
		case 5: N = 1; break;                   // World Circuit showoff: 0x16D230 RS+0 = 1
		default: N = Players; break;            // Freeride, single event showoff, timed race: the humans only
		}
		return FMath::Clamp(N, 1, SSXRace::MaxRacers);
	}

	int32 NextEventRacerCount(int32 NextMode, int32 Humans, int32 MaxBoarders)
	{
		return (NextMode == 2 || NextMode == 4) ? MaxBoarders : Humans;
	}

	int32 LaunchRacerCount(int32 GameMode, int32 Players, int32 MaxBoarders, int32 NextEventRacers)
	{
		if (NextEventRacers < 0 || GameMode == 4 || GameMode == 5 || GameMode == 6)
		{
			return RacerCount(GameMode, Players, MaxBoarders);
		}
		return FMath::Clamp(NextEventRacers, 1, SSXRace::MaxRacers);
	}

	int32 PadLayout(const FSSXSettings& S, int32 Port, int32 GameMode)
	{
		if (GameMode == 6 || Port < 0 || Port >= 4)
		{
			return 0;
		}
		return S.Controller[Port] != 0 ? 1 : 0;
	}

	int32 AnnouncerLevel(float Volume3, int32 Cap)
	{
		return FMath::Min(Cap, int32(Volume3));   // C float -> int conversion truncates
	}

	void ApplyBrightness(UWorld* World, float Scale)
	{
		if (!World)
		{
			return;
		}
		static TWeakObjectPtr<UMaterialParameterCollection> Cached;
		UMaterialParameterCollection* Mpc = Cached.Get();
		if (!Mpc)
		{
			Mpc = LoadObject<UMaterialParameterCollection>(nullptr, USSXWorldPresentation::CollectionPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
			Cached = Mpc;
		}
		if (!Mpc || !Mpc->GetScalarParameterByName(TEXT("Brightness")))
		{
			return;
		}
		if (UMaterialParameterCollectionInstance* Inst = World->GetParameterCollectionInstance(Mpc))
		{
			Inst->SetScalarParameterValue(TEXT("Brightness"), Scale);
		}
	}
}
