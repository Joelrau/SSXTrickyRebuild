#include "Audio/SSXRaceAudioFlow.h"

#include "Audio/SSXAudioConfig.h"

namespace SSXRaceAudio
{
TArray<FString> SongList(const FSSXInfFile& MusicInf)
{
	TArray<FString> Out;
	for (const FSSXInfFile::FSection& S : MusicInf.Sections)
	{
		if (!S.Name.Equals(TEXT("GLOBAL"), ESearchCase::IgnoreCase))
		{
			Out.Add(S.Name);
		}
	}
	return Out;
}

uint32 MusicMapMask(const FSSXInfFile& MusicMap, const TArray<FString>& Songs, const FString& TrackName)
{
	for (const FSSXInfFile::FSection& S : MusicMap.Sections)
	{
		if (!TrackName.Contains(S.Name, ESearchCase::IgnoreCase))
		{
			continue;
		}
		uint32 Mask = 0;
		int32 Count = 0;
		for (const TPair<FString, FString>& KV : S.Values)
		{
			if (!KV.Key.Equals(TEXT("SONG"), ESearchCase::IgnoreCase) || Count > 3)
			{
				continue;
			}
			for (int32 I = 0; I < Songs.Num() && I < 32; ++I)
			{
				if (Songs[I].Contains(KV.Value, ESearchCase::CaseSensitive))
				{
					Mask |= 1u << I;
					++Count;
					break;
				}
			}
		}
		return Mask ? Mask : 1u;
	}
	return 1u;
}

int32 NextAllowed(int32 Index, uint32 Mask, int32 NumSongs)
{
	if (NumSongs <= 0 || Mask == 0)
	{
		return 0;
	}
	for (int32 Guard = 0; Guard <= NumSongs; ++Guard)
	{
		Index = Index + 1 >= NumSongs ? 0 : Index + 1;
		if ((Mask >> Index) & 1u)
		{
			return Index;
		}
	}
	return 0;
}

int32 PickSong(uint32 Mask, int32 NumSongs, FRand15 Rand15)
{
	int32 Index = NextAllowed(-1, Mask, NumSongs);
	const int32 Steps = ((Rand15() & 0x7FFF) * 21) / 0x7FFF;
	for (int32 K = 0; K < Steps; ++K)
	{
		Index = NextAllowed(Index, Mask, NumSongs);
	}
	return Index;
}

bool IsSequentialTrack(int32 Track)
{
	return Track == 3 || Track == 7 || Track == 8 || Track == 11;
}

TArray<int32> InitialIntroQueue(int32 Track, bool bRestart, FRand15 Rand15)
{
	TArray<int32> Q;
	if (IsSequentialTrack(Track))
	{
		return Q;
	}
	if (bRestart)
	{
		Q.Add(4 + ((Rand15() & 0x7FFF) * 4) / 0x7FFF);
		return Q;
	}
	Q.Add(0);
	Q.Add(1 + ((Rand15() & 0x7FFF) * 3) / 0x7FFF);
	Q.Add(4 + ((Rand15() & 0x7FFF) * 4) / 0x7FFF);
	Q.Add(4 + ((Rand15() & 0x7FFF) * 4) / 0x7FFF);
	return Q;
}

int32 Intensity(int32 Racers, int32 Rank, double Boost, int32 Track)
{
	if (Racers < 2)
	{
		if (Track == 10)
		{
			return -1;
		}
		return int32(FMath::Clamp(Boost * 1.5, 0.0, 1.0) * 99.0);
	}
	const int32 V = 99 - ((Rank - 1) * 99) / (Racers - 1);
	return FMath::Clamp(V, 0, 99);
}

int32 TriggerEvent(int32 InternalType, int32 LapsLeft)
{
	if (InternalType == 1) return 0;
	if (InternalType == 9) return 9;
	if ((InternalType == 10 || InternalType == 18) && LapsLeft == 0) return 10;
	return -1;
}

void FMusicZones::Enter(int32 Kind)
{
	const int32 Z = (Kind - 2) / 2 + 1;
	if (Kind % 2 == 0)
	{
		InEnter = Z;
		if (Zone == 0)
		{
			Zone = Z;
			Posted.Add(2 * Z - 1);
		}
	}
	else
	{
		InExit = Z;
		if (Zone == Z)
		{
			ExitZone = Zone;
		}
	}
}

void FMusicZones::Leave(int32 Kind, double Delay)
{
	const int32 Z = (Kind - 2) / 2 + 1;
	if (Kind % 2 == 0)
	{
		if (Zone != Z) return;
		InEnter = 0;
	}
	else
	{
		if (ExitZone != Z) return;
		InExit = 0;
	}
	Pending.Add({ Delay, Kind });
}

void FMusicZones::Tick(double Dt)
{
	TArray<int32> Due;
	for (int32 I = 0; I < Pending.Num();)
	{
		Pending[I].TimeLeft -= Dt;
		if (Pending[I].TimeLeft <= 0.0)
		{
			Due.Add(Pending[I].Kind);
			Pending.RemoveAt(I);
		}
		else
		{
			++I;
		}
	}
	for (const int32 Kind : Due)
	{
		const int32 Z = (Kind - 2) / 2 + 1;
		if (Kind % 2 == 0)
		{
			if (InEnter != Z && ExitZone == 0)
			{
				Zone = 0;
				Posted.Add(2 * Z);
			}
		}
		else if (InExit != Z)
		{
			Zone = 0;
			ExitZone = 0;
			Posted.Add(2 * Z);
		}
	}
}

void FMusicZones::Reset()
{
	if (Zone > 0)
	{
		Posted.Add(2 * Zone);
	}
	Zone = ExitZone = 0;
}

void FFlow::Load(int32 InTrack, bool bRestart, bool bCircuit, bool bTutorial, double IntroDuck, FRand15 Rand15)
{
	Commands.Reset();
	Track = InTrack;
	IntroDuckLevel = IntroDuck;
	bTutorialMode = bTutorial;
	DuckMode = 4;
	DuckLevel = 1.0;
	bGo = false;
	SeqCounter = 0;
	Zones = FMusicZones();
	TriggerMask = 0;
	Control = SongStartControl;
	bEvent10Armed = true;
	if (bTutorial)
	{
		Control = (Rand15() & 0x7FFF) % 100;
		Emit(FCommand::SetControl, Control);
		Emit(FCommand::UnpauseSong);
		State = 6;
		bIntroQueued = false;
		return;
	}
	if (bRestart && !bCircuit)
	{
		State = 5;
		bIntroQueued = false;
		return;
	}
	const TArray<int32> Q = InitialIntroQueue(Track, bRestart, Rand15);
	for (const int32 Seg : Q)
	{
		Emit(FCommand::QueueIntro, Seg);
	}
	bIntroQueued = Q.Num() > 0 || IsSequentialTrack(Track);
	State = 2;
}

void FFlow::Frame(int32 IntroQueueLen, int32 Racers, int32 PlayerRank, double PlayerBoost, FRand15 Rand15)
{
	switch (State)
	{
	case 2:
		DuckMode = 0;
		DuckLevel = IntroDuckLevel;
		if (IntroQueueLen < 4)
		{
			State = 3;
		}
		break;
	case 3:
	case 4:
		if (IntroQueueLen < 2)
		{
			int32 Seg = 8 + ((Rand15() & 0x7FFF) << 3) / 0x7FFF;
			if (IsSequentialTrack(Track))
			{
				Seg = SeqCounter++;
				if (SeqCounter > 15)
				{
					SeqCounter = 8;
				}
			}
			Emit(FCommand::QueueIntro, Seg);
		}
		break;
	case 6:
	{
		const int32 V = Intensity(Racers, PlayerRank, PlayerBoost, Track);
		if (V >= 0 && V != Control)
		{
			Control = V;
			Emit(FCommand::SetControl, V);
		}
		break;
	}
	default:
		break;
	}
}

void FFlow::Countdown(double Clock, FRand15 Rand15)
{
	if (bGo)
	{
		return;
	}
	if (Clock < 0.0)
	{
		if (int32(Clock) % 60 == 0)
		{
			Emit(FCommand::Beep, 78);
		}
		return;
	}
	Go(Rand15);
}

void FFlow::IntroEnd()
{
	if (bIntroQueued)
	{
		Emit(FCommand::ClearIntro);
		Emit(FCommand::QueueIntro, EndSegment);
	}
	if (State != 6)
	{
		State = 5;
	}
}

void FFlow::Go(FRand15 Rand15)
{
	bGo = true;
	DuckMode = 4;
	State = 6;
	Emit(FCommand::StopIntro);
	if (bTutorialMode)
	{
		Control = (Rand15() & 0x7FFF) % 100;
		Emit(FCommand::SetControl, Control);
		return;
	}
	Emit(FCommand::UnpauseSong);
}

void FFlow::ReplayGo()
{
	bGo = true;
	DuckMode = 4;
	DuckLevel = 1.0;
	State = 6;
	Emit(FCommand::StopIntro);
	Control = SongStartControl;
	Emit(FCommand::PrepareSong);   // 0x215220 / 0x225260
	Emit(FCommand::UnpauseSong);
}

void FFlow::LoadReplay(int32 InTrack)
{
	Commands.Reset();
	Track = InTrack;
	DuckMode = 4;
	DuckLevel = 1.0;
	bGo = false;
	SeqCounter = 0;
	Zones = FMusicZones();
	TriggerMask = 0;
	Control = SongStartControl;
	bEvent10Armed = true;
	bTutorialMode = false;
	bIntroQueued = false;
	State = 6;
}

void FFlow::Trigger(int32 InternalType, bool bEnter, int32 LapsLeft)
{
	const uint32 Bit = InternalType >= 0 && InternalType < 32 ? (1u << InternalType) : 0u;
	if (bEnter)
	{
		if (TriggerMask & Bit)
		{
			return;
		}
		TriggerMask |= Bit;
	}
	else
	{
		TriggerMask &= ~Bit;
	}
	if (FMusicZones::IsZoneKind(InternalType))
	{
		if (bEnter) Zones.Enter(InternalType); else Zones.Leave(InternalType);
		FlushZones();
		return;
	}
	if (!bEnter)
	{
		return;
	}
	const int32 E = TriggerEvent(InternalType, LapsLeft);
	if (E == 10)
	{
		// TODO: the setter of snd+0x3700 is not recovered;
		if (!bEvent10Armed)
		{
			return;
		}
		bEvent10Armed = false;
	}
	if (E >= 0)
	{
		// TODO: type 18 also calls FUN_0021E528 (not recovered)
		Emit(FCommand::PostEvent, E);
	}
}

void FFlow::TickZones(double Dt)
{
	Zones.Tick(Dt);
	FlushZones();
}

void FFlow::ForcedReset()
{
	Zones.Reset();
	FlushZones();
}

void FFlow::FlushZones()
{
	for (const int32 E : Zones.Posted)
	{
		Emit(FCommand::PostEvent, E);
	}
	Zones.Posted.Reset();
}
}
