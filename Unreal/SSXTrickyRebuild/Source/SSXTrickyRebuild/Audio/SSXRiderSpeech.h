// 0x235090 / 0x235398 / 0x234D00 / 0x2347C8
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXSpeech.h"

namespace SSXRiderSpeech
{
	struct FRacer
	{
		int32 Index = -1;
		bool bFinished = false;
		bool bHuman = false;
		int32 Character = 0;           // 0x167E00 / 0x234C38
		int32 Voice = 0;               // profile +0x64 voice byte (= +0x38, but 12 for Mix Master Mike): speaker / target
		                               // 0x234698 / 0x234730 / 0x234BB8 / 0x228BD8
		FVector GamePos = FVector::ZeroVector;   // +0x140 (game cm, the 40 m test)
		FVector SoundPos = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		double PathHeading = 0.0;
		bool bGrudge = false;
		int32 GrudgeRival = -1;
		int32 Level[6] = {};           // +0x50 + 0x20 j (rivalry level toward racer j, signed byte)
		int32 Score[6] = {};
	};

	constexpr int32 TagRiderLine = 0xD;     // 0x235090 / 0x235398
	constexpr int32 TagVictimLine = 0xC;    // 0x234D00
	constexpr int32 OvertakeQuietFrames = 600;    // 0x2347C8
	constexpr double OvertakeMinSpeed = 670.56;   // cm/s along the passee's path heading (15 mph)
	constexpr double OvertakeMaxSpeed = 1788.16;
	constexpr double LevelThreshold = 60.0;

	/** 0x234698 / 0x234730: speaker / target bit of a voice byte (Eddie 0x40, Kaori 0x10, Luther 0x20, Mac 1, Moby 2, Zoe 8, JP 0x400, Elise 4, Psymon 0x100, Seeiah 0x800, Brodi 0x80, Marisol 0x200; else 0, so Mix Master Mike's 12 matches no CHAR sample). */
	SSXTRICKYREBUILD_API uint32 SpeakerMask(int32 Voice);

	class SSXTRICKYREBUILD_API FHelpers
	{
	public:
		using FRand = TFunctionRef<uint32()>;   // 0x23D4A8
		// 0x228958
		TFunction<bool()> IsChannel1Free;
		// 0x234BB8
		TFunction<void(int32 Tag)> StopIfTag;
		// 0x20F9B0
		TFunction<int32(const FVector& GamePos)> Zone;
		// 0x167E00
		TFunction<int32(int32 CharA, int32 CharB)> Relation;
		/** Post + service channel 1 at the speaker (0x228BD8, position = the speaker's +0x140) with the voice tag. */
		TFunction<void(const SSXSpeech::FRequest& R, const FVector& SoundPos, int32 Tag, int32 Racer)> Post;
		// 0x20ED20
		int32 Focus = -1;
		TArray<uint32> Posted;

		/** 0x234C38: 1 when A's relation kind toward B is 2, or kind 0 / 1 and (A holds a grudge on B, or A's level toward
		 *  B > 60); else 0 (also for a negative kind). */
		int32 RelationCheck(const FRacer& A, const FRacer& B) const;
		// 0x235090
		void Wipeout(const FRacer& R, FRand Rand);
		// 0x235398 / 0x124DD0
		void Bump(const FRacer& R, bool bHard, FRand Rand);
		/** 0x234D00 (rider hit 0x124920): bNotShoving = the attacker's channel 1 is not a shove (category 0x15),
		 *  bKnockdown = the knock-down branch. */
		void Hit(const FRacer& Attacker, const FRacer& Victim, bool bNotShoving, bool bKnockdown, FRand Rand);
		// 0x2347C8 / 0x219A48
		void Overtake(const FRacer& Passer, const FRacer& Passee, int32 Frame, int32 GoFrame, FRand Rand);
		/** 0x234F68 (Cruise taunt 0x103660): both unfinished, channel 1 free, a human or the focus player among the two,
		 *  both within 40 m of the same listener -> CHAR 0x600F {speaker, target} at the speaker. No random draw. */
		void Taunt(const FRacer& Speaker, const FRacer& Rival);
		/** 0x235298 (Cruise win taunt 0x128150): unfinished, human or focus, channel 1 free, within 40 m of a listener ->
		 *  CHAR 0x6028 (strength >= 0.65) or 0x6027 {speaker} at the rider. */
		void WinTaunt(const FRacer& R, double Strength);

	private:
		void Send(std::initializer_list<uint32> Words, const FVector& Pos, int32 Tag, int32 Racer);
		bool Free() const { return !IsChannel1Free || IsChannel1Free(); }
		int32 ZoneOf(const FVector& P) const { return Zone ? Zone(P) : 0; }
	};
}
