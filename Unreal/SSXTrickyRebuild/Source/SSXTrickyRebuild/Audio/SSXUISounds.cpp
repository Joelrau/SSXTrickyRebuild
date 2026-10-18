#include "Audio/SSXUISounds.h"

namespace SSXUISound
{
FFeSound FeSoundForId(int32 Id, int32 FeMusicState)
{
	FFeSound S;
	switch (Id)
	{
	case 0: S.Sound = 16; S.Voice = 1; break;
	case 1: case 5: case 9: case 15:
		S.Sound = FeMusicState >= 4 ? 50 : -1;
		break;
	case 2: case 6: case 10: S.Sound = 48; break;
	case 3: case 7: case 11: S.Sound = 49; break;
	case 4: case 8: case 20: case 23:
	case 17: case 18: case 22:
		S.Sound = 17; S.Voice = 2; break;
	case 12: S.Sound = 18; break;
	case 13: S.Sound = 50; break;
	case 19: S.Sound = 64; S.LevelClass = 2; break;
	case 21: S.Sound = 64; S.LevelClass = 3; break;
	default: break;
	}
	return S;
}

FString FeId(int32 Sound)
{
	return FString::Printf(TEXT("sfx/zbxfe/%02d"), Sound);
}

FRaceSound RaceSound(ERace Kind)
{
	FRaceSound S;
	switch (Kind)
	{
	case ERace::Accept: S.Sound = 112; S.FrameByte1 = 0x5C; break;
	case ERace::Navigate: S.Sound = 112; break;
	case ERace::Back: S.Sound = 112; S.FrameByte1 = 0x18; break;
	case ERace::PointChange: S.Sound = 124; break;
	case ERace::Denied: S.Sound = 113; S.bStopShared = true; break;
	case ERace::ReplayHelp: S.Sound = 0x7D; break;     // 0x214428 / 0x213D60
	case ERace::ReplayCamera: S.Sound = 0x7F; break;   // 0x214650
	case ERace::PauseOpen: S.Sound = 0x7E; break;      // 0x215458 / 0x1AC410
	case ERace::PreviewMaster: case ERace::PreviewMusic: case ERace::PreviewSpeech: case ERace::PreviewChar: case ERace::PreviewSfx:
		S.Sound = 0x70;   // TODO: the queue params 100 / 0x5A / 0x32 are not interpreted
		break;
	}
	return S;
}

FString RaceId(int32 Sound)
{
	return FString::Printf(TEXT("sfx/zBxsfx/%02d"), Sound);
}
}
