#include "Audio/SSXBoardAudio.h"

#include "Sim/RiderState.h"

namespace SSXBoardAudio
{
namespace
{
	int32 F2I(float X)
	{
		if (X != X) { return 0; }
		return int32(FMath::Clamp(double(X), -2147483648.0, 2147483647.0));
	}

	int32 TDiv(int32 A, int32 B) { return B ? int32(int64(A) / int64(B)) : 0; }

	// 0x343098
	const int16 Curves[8][8] = {
		{ 100, 300, 800, 1200, 22, 60, 100, 127 },
		{ 100, 300, 800, 2000, 11, 30, 60, 120 },
		{ 200, 400, 550, 800, 33, 70, 100, 127 },
		{ 100, 1000, 2000, 4000, 20, 70, 100, 127 },
		{ 500, 1000, 1500, 2000, 0, 10, 20, 40 },
		{ 10, 60, 200, 300, 0, 127, 100, 0 },
		{ 10, 30, 500, 900, 0, 30, 80, 127 },
		{ 100, 800, 1200, 1600, 80, 90, 105, 127 },
	};
}

FSSXSnowInputs Inputs(const FVector3f& Cf, const FVector3f& V, float GroundDrag, float SteerCur)
{
	const double Sum = double(Cf.X * V.X) + double(Cf.Y * V.Y) + double(Cf.Z * V.Z);
	FSSXSnowInputs In;
	In.Slip = FMath::Abs(F2I(float(Sum)));
	In.Dig = FMath::Abs(F2I(GroundDrag));
	In.Lean = FMath::Abs(F2I(SteerCur * 127.f));
	In.Board = (In.Slip >> 2) + In.Dig * 2;
	In.Bend = In.Board + In.Slip;
	return In;
}

FSSXSnowInputs Inputs(const FRiderState& S)
{
	return Inputs(FVector3f(S.ContactForward), FVector3f(S.Velocity), float(S.GroundDragAccel), float(S.Steer.Cur));
}

ESSXSnowSurface SnowIndex(int32 SurfaceId, int32 Motion, int32 RailSurface)
{
	switch (Motion == MotionRail ? RailSurface : SurfaceId)
	{
	case 0: case 3: case 4: return ESSXSnowSurface::Powder;
	case 2: return ESSXSnowSurface::Loose;
	case 5: case 6: return ESSXSnowSurface::Ice;
	case 13: return ESSXSnowSurface::Metal;
	case 12: return ESSXSnowSurface::Wood;
	case 9: case 10: case 11: return ESSXSnowSurface::Rock;
	case 14: return ESSXSnowSurface::Glass;
	case 18: case 19: return ESSXSnowSurface::Chute;
	default: return ESSXSnowSurface::Pack;
	}
}

int32 Curve(int32 Index, int32 X)
{
	const int16* C = Curves[FMath::Clamp(Index, 0, 7)];
	for (int32 K = 0; K < 3; ++K)
	{
		if (C[K] <= X && X < C[K + 1])
		{
			return C[4 + K] + TDiv((X - C[K]) * (C[5 + K] - C[4 + K]), C[K + 1] - C[K]);
		}
	}
	if (X < C[0]) { return C[4]; }
	if (X <= C[3]) { return 0; }   // exactly x3 -> 0 (quirk)
	return C[7];
}

FSSXSnowResult Callback(EVoice Voice, const FSSXSnowPrograms& Programs, int32 Motion, ESSXSnowSurface Index, const FSSXSnowInputs& In)
{
	FSSXSnowResult Off;
	switch (Voice)
	{
	case EVoice::Speed:   // 0x21B440
		if (Motion != MotionGround) { return Off; }
		Off.Volume = Curve(5, In.Slip);
		Off.Bend = Curve(6, In.Slip);
		return Off;
	case EVoice::Glide:   // 0x21B508
		return Motion != MotionAir ? Programs.Evaluate(Index, ESSXSnowMode::Glide, In) : Off;
	case EVoice::Carve:   // 0x21B718
		return Motion != MotionAir ? Programs.Evaluate(Index, ESSXSnowMode::Carve, In) : Off;
	case EVoice::AIGlide: // 0x21B628
		return (Motion == MotionGround || Motion == MotionRail) ? Programs.Evaluate(Index, ESSXSnowMode::AIGlide, In) : Off;
	}
	return Off;
}

int32 VoiceSound(EVoice Voice, ESSXSnowSurface Index)
{
	const int32 I = int32(Index) * 8;
	switch (Voice)
	{
	case EVoice::Speed: return 0;
	case EVoice::Glide: return I + 4;
	case EVoice::Carve: return I + 3;
	case EVoice::AIGlide: return I + 6;
	}
	return 0;
}

bool BigAir(double TLand, double TElapsed, double TApex, bool bGameMode7, int32& OutMusicByte, double& OutWindLevel)
{
	if (TLand <= 1.5)
	{
		OutMusicByte = 127;
		OutWindLevel = 0.0;
		return false;
	}
	// TODO: the EE's float rounding of these terms)
	const double F = bGameMode7 ? 50.0 : 16.0;
	const double X = FMath::Min(127.0, FMath::Max(F, (1.0 - (1.0 - F / 127.0) * TLand * 0.5) * 127.0));
	const double Total = TApex * 0.5 + TElapsed;
	int32 Duck;
	if (Total <= TElapsed)
	{
		Duck = int32(X);
	}
	else
	{
		const double R = TElapsed / Total;
		Duck = int32(FMath::Min(127.0, FMath::Max(X, 127.0 - (127.0 - X) * R)));
	}
	OutMusicByte = Duck;
	OutWindLevel = (127 - Duck) / 127.0;
	return true;
}

int32 Falloff(int32 Vol, double DistM, double RadiusM)
{
	const double R = RadiusM != 0.0 ? RadiusM : 30.0;
	const double D = FMath::Max(DistM - 0.5, 0.0);
	if (!(D < R && R > 0.0))
	{
		return 0;
	}
	const double K = (R - D) / R;
	return int32(K * K * Vol);
}

double Doppler(double PrevDistM, double DistM, double A)
{
	const double V = FMath::Clamp((PrevDistM - DistM) / A, -894.07996, 894.07996);
	return (V + 8000.0) / 8000.0;
}

FString ZboardId(int32 Sound) { return FString::Printf(TEXT("sfx/zboard/%02d"), Sound); }
FString ZbxsfxId(int32 Sound) { return FString::Printf(TEXT("sfx/zBxsfx/%d"), Sound); }
}
