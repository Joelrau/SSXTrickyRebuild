#include "Audio/SSXRiderSpeech.h"

namespace SSXRiderSpeech
{
namespace
{
	uint32 Char(int32 Event) { return SSXSpeech::RequestId(SSXSpeech::DbChar, Event); }
	int32 LevelToward(const FRacer& A, const FRacer& B) { return B.Index >= 0 && B.Index < 6 ? A.Level[B.Index] : 0; }
	int32 ScoreToward(const FRacer& A, const FRacer& B) { return B.Index >= 0 && B.Index < 6 ? A.Score[B.Index] : 0; }
}

uint32 SpeakerMask(int32 Voice)
{
	static const uint32 Masks[12] = { 0x40, 0x10, 0x20, 1, 2, 8, 0x400, 4, 0x100, 0x800, 0x80, 0x200 };
	return Voice >= 0 && Voice < 12 ? Masks[Voice] : 0u;
}

void FHelpers::Send(std::initializer_list<uint32> Words, const FVector& Pos, int32 Tag, int32 Racer)
{
	const SSXSpeech::FRequest R = SSXSpeech::MakeRequest(Words);
	Posted.Add(R[0]);
	if (Post)
	{
		Post(R, Pos, Tag, Racer);
	}
}

int32 FHelpers::RelationCheck(const FRacer& A, const FRacer& B) const
{
	const int32 Kind = Relation ? Relation(A.Character, B.Character) : 0;
	if (Kind < 0)
	{
		return 0;
	}
	if (Kind < 2)
	{
		const bool bGrudgeOnB = A.bGrudge && A.GrudgeRival == B.Index;
		return (!bGrudgeOnB && double(LevelToward(A, B)) <= LevelThreshold) ? 0 : 1;
	}
	return Kind == 2 ? 1 : 0;
}

void FHelpers::Wipeout(const FRacer& R, FRand Rand)
{
	if (R.bFinished)
	{
		return;
	}
	if (StopIfTag)
	{
		StopIfTag(R.Voice);
		StopIfTag(TagVictimLine);
	}
	if (!Free() || ZoneOf(R.GamePos) < 0)
	{
		return;
	}
	const uint32 Mask = SpeakerMask(R.Voice);
	const int32 Event = (Rand() & 1) == 0 ? 0x6024 : 0x6023;
	Send({ Char(Event), Mask, 0x40 }, R.SoundPos, TagRiderLine, R.Index);
}

void FHelpers::Bump(const FRacer& R, bool bHard, FRand Rand)
{
	if (R.bFinished)
	{
		return;
	}
	if (StopIfTag) StopIfTag(R.Voice);
	if (!Free() || ZoneOf(R.GamePos) < 0)
	{
		return;
	}
	if (!bHard)
	{
		Wipeout(R, Rand);
		return;
	}
	Send({ Char(0x6022), SpeakerMask(R.Voice) }, R.SoundPos, TagRiderLine, R.Index);
}

void FHelpers::Hit(const FRacer& Attacker, const FRacer& Victim, bool bNotShoving, bool bKnockdown, FRand Rand)
{
	if (Attacker.bFinished || Victim.bFinished)
	{
		return;
	}
	if (StopIfTag) StopIfTag(Victim.Voice);
	if (!Free())
	{
		return;
	}
	const int32 ZV = ZoneOf(Victim.GamePos);
	const int32 ZA = ZoneOf(Attacker.GamePos);
	if (ZV < 0 && ZA < 0)
	{
		return;
	}
	if (bKnockdown && ZV >= 0)
	{
		Wipeout(Victim, Rand);
		return;
	}
	uint32 VictimSpeaks = 0;
	if (ZV >= 0 && ZA >= 0)
	{
		VictimSpeaks = Rand() & 1;
	}
	const bool bInvolved = Attacker.bHuman || Victim.bHuman || Attacker.Index == Focus || Victim.Index == Focus;
	if (!bInvolved)
	{
		Wipeout(VictimSpeaks == 0 ? Attacker : Victim, Rand);
		return;
	}
	uint32 Kind = 0x40;
	if (VictimSpeaks == 0)
	{
		const int32 Rel = RelationCheck(Attacker, Victim);
		Kind |= Rel == 0 ? (bNotShoving ? 2u : 0x10u) : (bNotShoving ? 1u : 0x20u);
		Send({ Char(0x6036), SpeakerMask(Attacker.Voice), Kind }, Attacker.SoundPos, Attacker.Voice, Attacker.Index);
	}
	else
	{
		const int32 Rel = RelationCheck(Victim, Attacker);
		Kind |= Rel == 0 ? 8u : 4u;
		Send({ Char(0x6036), SpeakerMask(Victim.Voice), Kind }, Victim.SoundPos, TagVictimLine, Victim.Index);
	}
}

void FHelpers::Taunt(const FRacer& Speaker, const FRacer& Rival)
{
	if (Speaker.bFinished || Rival.bFinished || !Free())
	{
		return;
	}
	if (!(Speaker.bHuman || Rival.bHuman || Speaker.Index == Focus || Rival.Index == Focus))
	{
		return;
	}
	const int32 Z = ZoneOf(Speaker.GamePos);
	if (Z < 0 || ZoneOf(Rival.GamePos) != Z)
	{
		return;
	}
	Send({ Char(0x600F), SpeakerMask(Speaker.Voice), SpeakerMask(Rival.Voice) }, Speaker.SoundPos, Speaker.Voice, Speaker.Index);
}

void FHelpers::WinTaunt(const FRacer& R, double Strength)
{
	if (R.bFinished || !(R.bHuman || R.Index == Focus) || !Free() || ZoneOf(R.GamePos) < 0)
	{
		return;
	}
	Send({ Char(float(Strength) >= 0.65f ? 0x6028 : 0x6027), SpeakerMask(R.Voice) }, R.SoundPos, R.Voice, R.Index);
}

void FHelpers::Overtake(const FRacer& Passer, const FRacer& Passee, int32 Frame, int32 GoFrame, FRand Rand)
{
	if (Frame - GoFrame < OvertakeQuietFrames || Passer.bFinished || Passee.bFinished)
	{
		return;
	}
	const bool bInvolved = Passer.bHuman || Passee.bHuman || Passer.Index == Focus || Passee.Index == Focus;
	if (!bInvolved || !Free())
	{
		return;
	}
	const int32 Z = ZoneOf(Passer.GamePos);
	if (Z < 0 || ZoneOf(Passee.GamePos) != Z)
	{
		return;
	}
	auto Kind = [this](const FRacer& A, const FRacer& B) { return Relation ? Relation(A.Character, B.Character) : 0; };
	const bool bPasserQualifies = Passer.bHuman || Passer.Index == Focus || Kind(Passer, Passee) == 1
		|| (double(LevelToward(Passer, Passee)) > LevelThreshold && ScoreToward(Passer, Passee) > 0);
	bool bPasseeQualifies = true;
	if (!Passee.bHuman && Passee.Index != Focus)
	{
		bPasseeQualifies = Kind(Passee, Passer) == 1 || double(LevelToward(Passee, Passer)) > LevelThreshold;
	}
	if (!bPasserQualifies && !bPasseeQualifies)
	{
		return;
	}
	uint32 PasserSpeaks = 1;
	if (bPasseeQualifies)
	{
		PasserSpeaks = 0;
		if (bPasserQualifies) PasserSpeaks = Rand() & 1;
	}
	// 0x250C10
	const FVector Dir(FMath::Cos(Passee.PathHeading), FMath::Sin(Passee.PathHeading), 0.0);
	const double Dv = FVector::DotProduct(Dir, Passer.Velocity) - FVector::DotProduct(Dir, Passee.Velocity);
	if (!(OvertakeMinSpeed < Dv && Dv < OvertakeMaxSpeed))
	{
		return;
	}
	if (PasserSpeaks == 0)
	{
		const int32 Rel = RelationCheck(Passee, Passer);
		Send({ Char(Rel == 0 ? 0x601C : 0x601B), SpeakerMask(Passee.Voice), SpeakerMask(Passer.Voice) }, Passee.SoundPos, Passee.Voice, Passee.Index);
	}
	else
	{
		const int32 Rel = RelationCheck(Passer, Passee);
		Send({ Char(Rel == 0 ? 0x601A : 0x6019), SpeakerMask(Passer.Voice), SpeakerMask(Passee.Voice) }, Passer.SoundPos, Passer.Voice, Passer.Index);
	}
}
}
