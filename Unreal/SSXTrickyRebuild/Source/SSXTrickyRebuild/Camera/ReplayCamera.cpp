#include "Camera/ReplayCamera.h"

namespace
{
	const TCHAR* const ReplayDef = TEXT("replay cam");   // 0x383920

	// 0x336608, 0x1786D0
	const TCHAR* SourceDefinition(int32 Mode)
	{
		switch (Mode)
		{
		case 1: return TEXT("chase reverse");
		case 2: return TEXT("chase board");
		case 3: return TEXT("chase eyes");
		case 4: return TEXT("chase near");
		case 5: return TEXT("chase far");
		case 6: return TEXT("chase over");
		default: return TEXT("replay start");
		}
	}
}

FSSXReplayCamera::FSSXReplayCamera(FSSXCameraDefSet* InDefs)
	: Defs(InDefs), Cam(InDefs)
{
	Cam.OwnRacer = [this]() { return Target; };   // FUN_00173C50, 0x3368B0
}

void FSSXReplayCamera::Activate(bool bReplay, int32 Rider)
{
	bActive = bReplay;
	Cam.bReplay = bReplay;
	Cam.Reset();   // 0x173A88, 0x173A98
	Target = 0;
	if (!bReplay)
	{
		return;
	}
	const int32 N = NumRacers ? NumRacers() : 1;
	if (N <= Rider)
	{
		Rider = 0;
	}
	Set(0.f, 0.f, 10.f, 1.f, Rider, false);
	SetMode(7);
}

void FSSXReplayCamera::SetMode(int32 InMode)
{
	Mode = (uint32(InMode - 1) < 8u) ? InMode : 7;
	Cam.SetMode(Mode);       // 0x1776D0
	// 0x175EB8, 0x231318
	if (Cam.ResetDirector() && OnNarrationStop)
	{
		OnNarrationStop();
	}
	if (Defs)
	{
		const FSSXCameraDef* Src = Defs->Find(SourceDefinition(Mode));
		FSSXCameraDef* Dst = Defs->FindMutable(ReplayDef);
		if (Src && Dst && Src != Dst)
		{
			const FString Name = Dst->Name;
			const int32 StaticCut = Dst->StaticCut;
			*Dst = *Src;
			Dst->Name = Name;
			Dst->StaticCut = StaticCut;
		}
	}
	Cam.MarkCut();           // FUN_00177DB0
}

void FSSXReplayCamera::NextMode()
{
	int32 M = Mode + 1;
	if (8 < M) M = 1;
	if (M == 3) M = 4;
	SetMode(M);
}

void FSSXReplayCamera::Get(float& A, float& B, float& C, float& D, int32& OutTarget) const
{
	A = B = C = D = 0.f;
	if (const FSSXCameraDef* R = Defs ? Defs->Find(ReplayDef) : nullptr)
	{
		A = float(R->YawOffset);
		B = float(R->ElevBase);
		C = float(R->OrbitDist);
		D = float(R->Fov);
	}
	OutTarget = Target;
}

void FSSXReplayCamera::Set(float A, float B, float C, float D, int32 InTarget, bool bCut)
{
	float F = 1.f;
	if (1.f < B || (F = -1.f, B < -1.f)) B = F;
	F = 200.f;
	if (200.f <= C && (F = C, 1200.f < C)) F = 1200.f;
	if (D * F < 300.f) D = 300.f / F;
	float G = 0.5f;
	if (0.5f <= D && (G = D, 1.9f < D)) G = 1.9f;
	if (FSSXCameraDef* R = Defs ? Defs->FindMutable(ReplayDef) : nullptr)
	{
		R->OrbitDist = F;
		R->YawOffset = A;
		R->ElevBase = B;
		R->Fov = G;
	}
	Target = InTarget;   // FUN_001814A8, FUN_00173C50
	if (bCut)
	{
		Cam.MarkCut();
	}
}

void FSSXReplayCamera::Nudge(float Amount, int32 Op)
{
	const float Fov = Amount * 0.0055555557f;
	const float Ang = Amount * 0.034906585f;
	float A, B, C, D;
	int32 T;
	Get(A, B, C, D, T);
	const int32 N = NumRacers ? NumRacers() : 1;
	switch (Op)
	{
	case 0: A -= Ang; break;
	case 1: A += Ang; break;
	case 2: B -= Ang; break;
	case 3: B += Ang; break;
	case 4:
		if (N <= ++T) T = 0;
		Cam.MarkCut();
		if (CutPlayerCamera) CutPlayerCamera();
		break;
	case 5:
		if (--T < 0) T = N - 1;
		Cam.MarkCut();
		if (CutPlayerCamera) CutPlayerCamera();
		break;
	case 6: D -= Fov; break;
	case 7: D += Fov; break;
	case 8: C -= Amount * 5.f; break;
	case 9: C += Amount * 5.f; break;
	default: break;
	}
	// FUN_001741D0
	Set(A, B, C, D, T, false);
}
