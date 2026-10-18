#include "Audio/SSXAnnouncer.h"

#include "Audio/SSXAudioDiag.h"
#include "Audio/SSXSpeechSystem.h"
#include "Race/LessonMan.h"
#include "Race/RaceSession.h"
#include "Sim/Air/RiderMessages.h"


namespace
{
	constexpr int32 MeterHintDelayFrames = 180;   // 0x2191C0 / 0x219258
	constexpr double MeterHintLevel = 0.875;
	constexpr int32 ShowoffWarnSeconds = 10;     // 0x1A1F18
	constexpr int32 LeadSoundHoldFrames = 12;    // 0x219A48

	uint32 Mc(int32 Event) { return SSXSpeech::RequestId(SSXSpeech::DbMC, Event); }
}

void FSSXAnnouncer::Reset(int32 InMode, int32 InVenue, int32 InHeatWord)
{
	Mode = InMode;
	Venue = InVenue;
	HeatWord = InHeatWord;
	FMemory::Memzero(Checkpoints, sizeof(Checkpoints));
	MeterHintFrames = -1;
	LastNearFullCount = -1;
	LastInfiniteCalls.Reset();
	LastClock = 0.0;
	bTimeOverPosted = false;
	LastPhase = -1;
	LeadSoundFrame = -1000000;
	TrickGate = 1;
	TrickNamed = 0;
	Posted.Reset();
}

bool FSSXAnnouncer::LeadTaken(const FRaceSession& Session, const FRaceCue& C)
{
	// 0x219A48
	if (C.Kind != ERaceCueKind::RankChange || C.Param != 0 || C.Racer < 0 || C.Racer >= Session.NumRacers()
		|| !Session.GetRacer(C.Racer).bHuman || C.Frame - LeadSoundFrame < LeadSoundHoldFrames)
	{
		return false;
	}
	LeadSoundFrame = C.Frame;
	return true;
}

bool FSSXAnnouncer::Gate(int32 Class, FRandA RandA) const
{
	// 0x235D58
	const bool bPass = FSSXSpeechSystem::ClassGate(Class, GateLevel, RandA());
	if (!bPass)
	{
		SSXAudioDiag::Note(TEXT("mc"), FString::Printf(TEXT("class %d"), Class), false, 0.f, 0.0, SSXAudioDiag::EResult::Gated,
			TEXT("class gate 0x235D58"));
	}
	return bPass;
}

bool FSSXAnnouncer::PostWords(FSSXSpeechSystem& Speech, const uint32* Words, int32 N)
{
	SSXSpeech::FRequest R = SSXSpeech::MakeRequest({});
	for (int32 K = 0; K < N && K < SSXSpeech::RequestWords; ++K) R[K] = Words[K];
	Posted.Add(R[0]);
	const bool bTook = Speech.Post(R);
	SSXAudioDiag::Note(TEXT("mc"), FString::Printf(TEXT("request 0x%08X"), R[0]), false, 0.f, 0.0,
		bTook ? SSXAudioDiag::EResult::Played : SSXAudioDiag::EResult::Gated, bTook ? TEXT("posted") : TEXT("no slot / chance roll"));
	return bTook;
}

bool FSSXAnnouncer::Post(FSSXSpeechSystem& Speech, std::initializer_list<uint32> Words)
{
	TArray<uint32, TInlineAllocator<SSXSpeech::RequestWords>> W(Words);
	return PostWords(Speech, W.GetData(), W.Num());
}

void FSSXAnnouncer::Frame(FSSXSpeechSystem& Speech, const FRaceSession& Session, int32 Human, FRandA RandA)
{
	const int32 Phase = int32(Session.GetPhase());
	const bool bRace = Session.GetPhase() == ERacePhase::Race;
	// 0x108378 / 0x233448 / 0x21BFD0 / 0x22F0A0
	while (LastInfiniteCalls.Num() < Session.NumRacers()) { LastInfiniteCalls.Add(-1); }
	for (int32 R = 0; R < Session.NumRacers(); ++R)
	{
		const FRacerRecord& Rc = Session.GetRacer(R);
		const int32 Calls = Rc.Sim ? Rc.Sim->State.InfiniteBoostCallCount : 0;
		if (LastInfiniteCalls[R] >= 0 && Calls > LastInfiniteCalls[R] && Rc.Sim->State.FinishResult < 0 && Gate(23, RandA) && Mode != 6 && !bReplay)
		{
			Post(Speech, { Mc(0x204D) });
		}
		LastInfiniteCalls[R] = Calls;
	}
	if (Human >= 0 && Human < Session.NumRacers() && Session.GetRacer(Human).Sim && bRace)
	{
		const FRiderState& S = Session.GetRacer(Human).Sim->State;
		const bool bFinished = S.FinishResult >= 0;
		// 0x11B018 / 0x2191C0 / 0x219258
		if (LastNearFullCount >= 0 && S.MeterNearFullCount != LastNearFullCount && !bReplay)
		{
			MeterHintFrames = S.BoostStrength <= 0.0 ? MeterHintDelayFrames : -1;
		}
		LastNearFullCount = S.MeterNearFullCount;
		if (MeterHintFrames > 0 && --MeterHintFrames == 0)
		{
			MeterHintFrames = -1;
			if (S.BoostStrength <= 0.0 && S.BoostMeter > MeterHintLevel && !bFinished && Gate(9, RandA) && Mode != 6 && !bReplay)
			{
				Post(Speech, { Mc(0x2046), 1 });
			}
		}
	}
	// 0x233FF0 / 0x234058
	if (bRace && SSXRace::IsScoreMode(Session.GetMode()) && Human >= 0)
	{
		const double Clock = Session.GetClock();
		const bool bFinished = Human < Session.NumRacers() && Session.GetRacer(Human).Sim && Session.GetRacer(Human).Sim->State.FinishResult >= 0;
		if (LastPhase == Phase && (int32)LastClock == ShowoffWarnSeconds + 1 && (int32)Clock == ShowoffWarnSeconds && !bFinished)
		{
			if (Gate(12, RandA) && Mode != 6 && !bReplay) Post(Speech, { Mc(0x2036) });
		}
		if (LastPhase == Phase && !bTimeOverPosted && LastClock > 0.0 && Clock <= 0.0)
		{
			bTimeOverPosted = true;
			if (Gate(13, RandA) && Mode != 6 && !bReplay) Post(Speech, { Mc(0x2037) });
		}
		LastClock = Clock;
	}
	LastPhase = Phase;
}

void FSSXAnnouncer::Cue(FSSXSpeechSystem& Speech, const FRaceSession& Session, const FRaceCue& C, FRandA RandA)
{
	if (C.Kind != ERaceCueKind::TriggerEnter || C.Racer < 0 || C.Racer >= Session.NumRacers())
	{
		return;
	}
	const FRacerRecord& R = Session.GetRacer(C.Racer);
	if (!R.Sim || R.Sim->State.FinishResult >= 0)
	{
		return;
	}
	// 0x20ED20
	int32 Focus = -1, Slot = -1, Humans = 0;
	for (int32 I = 0; I < Session.NumRacers(); ++I)
	{
		if (!Session.GetRacer(I).bHuman) continue;
		if (Focus < 0) Focus = I;
		if (I == C.Racer) Slot = Humans;
		++Humans;
	}
	const bool bFocus = C.Racer == Focus;
	switch (C.Type)
	{
	case SSXRaceEvent::Checkpoint:
	{
		// 0x233EE0
		if (!bReplay && R.bHuman && Mode != 6 && Slot >= 0 && Slot < 4)
		{
			const int32 Count = ++Checkpoints[Slot];
			if (bFocus && Gate(11, RandA) && Mode > 1 && Mode < 6)
			{
				Post(Speech, { Mc(0x2034), 1, (1u << ((Count + 1) & 31)) | 1u });
			}
		}
		// 0x233BA0
		if (Gate(6, RandA) && Mode != 6 && !bReplay && Session.NumRacers() > 1 && bFocus)
		{
			const uint32 Who = Slot == 0 ? 1u : (Slot == 1 ? 2u : 0u);
			Post(Speech, { Mc(0x2049), Who, RankChangeWord(R.PrevRank, R.Rank), RankWord(PlayerRankIndex) });
		}
		break;
	}
	case 19: case 20:   // 0x234558
		if (!bReplay && R.bHuman && Mode != 6 && Gate(14, RandA)) { Post(Speech, { Mc(0x2051), uint32(C.Type == 19 ? 1 : 2) }); }
		break;
	case 21:            // 0x234610
		if (!bReplay && R.bHuman && Mode != 6 && Gate(15, RandA)) { Post(Speech, { Mc(0x2053) }); }
		break;
	case 22:            // 0x233740
		if (!bReplay && Mode != 6 && Gate(18, RandA) && bFocus) { Post(Speech, { Mc(0x2056) }); }
		break;
	default:
		break;
	}
}

uint32 FSSXAnnouncer::GoRankWord(int32 InHeatWord, int32 InVenue, int32 InMode)
{
	// 0x233138 / 0x1635B0
	uint32 W = 0;
	switch (InMode)
	{
	case 2: case 3: case 7:
		W = 2;
		break;
	case 4:
		W = InHeatWord == 1 ? 4u : (InHeatWord == 0 ? 2u : (InHeatWord == 2 ? (InVenue == 5 ? 0x10u : 8u) : 0u));
		W |= 0x200;
		break;
	case 5:
		W = InVenue == 6 ? 0x40u : 0x20u;
		W |= 0x400;
		break;
	default:
		W = 0;
		break;
	}
	return W | 1;
}

TArray<TArray<uint32>> FSSXAnnouncer::GoRequests(int32 InMode, int32 InVenue, int32 InHeatWord)
{
	// 0x2334B0
	uint32 ModeWord = 0, RaceType = 0;
	switch (InMode)
	{
	case 2: ModeWord = 2; RaceType = 1; break;
	case 3: ModeWord = 2; RaceType = 2; break;
	case 4: ModeWord = 4; RaceType = 1; break;
	case 5: ModeWord = 4; RaceType = 2; break;
	case 7: ModeWord = 2; RaceType = 1; break;
	default: break;
	}
	ModeWord |= 1;
	uint32 VenueBit = 0;
	switch (InVenue)
	{
	case 0: VenueBit = 0x400; break;
	case 1: VenueBit = 1; break;
	case 2: VenueBit = 2; break;
	case 3: VenueBit = 4; break;
	case 4: VenueBit = 8; break;
	case 5: VenueBit = 0x20; break;
	case 6: VenueBit = 0x40; break;
	case 7: VenueBit = 0x80; break;
	case 8: VenueBit = 0x10; break;
	case 11: VenueBit = 0x800; break;
	default: VenueBit = 0; break;
	}
	const uint32 Rank = GoRankWord(InHeatWord, InVenue, InMode);
	TArray<TArray<uint32>> Out;
	Out.Add({ Mc(0x204A), ModeWord, VenueBit, RaceType });
	if ((Rank & 2) == 0)
	{
		Out.Add({ Mc(0x201F), ModeWord, RaceType, Rank });
	}
	else
	{
		Out.Add({ Mc(0x2020), ModeWord, VenueBit, RaceType });
	}
	return Out;
}

void FSSXAnnouncer::Go(FSSXSpeechSystem& Speech, FRandA RandA)
{
	// 0x2334B0
	if (bReplay || !Gate(5, RandA) || Mode == 6)
	{
		return;
	}
	for (const TArray<uint32>& W : GoRequests(Mode, Venue, HeatWord))
	{
		PostWords(Speech, W.GetData(), W.Num());
	}
}

void FSSXAnnouncer::TrackBoostOp(FSSXSpeechSystem& Speech, int32 Op)
{
	// 0x2340B8 / 0x234308 / 0x21BFD0
	if (bReplay)
	{
		return;
	}
	if (Op == 17)
	{
		Post(Speech, { Mc(0x2047), 2 });
	}
	else if (Op == 18)
	{
		Post(Speech, { Mc(0x2048), 1 });
	}
}

void FSSXAnnouncer::TrickyActivation(FSSXSpeechSystem& Speech, FRandA RandA)
{
	if (Gate(19, RandA) && Mode != 6 && !bReplay)
	{
		Post(Speech, { Mc(0x204C) });
	}
}

uint32 FSSXAnnouncer::MessageBit(int32 Type, int32 Value)
{
	// 0x233220
	switch (Type)
	{
	case SSXMsg::Knockdown: return 0x40;
	case SSXMsg::TimeBonus: return 4;
	case SSXMsg::Multiplier: return Value == 2 ? 8u : (Value == 3 ? 0x10u : (Value == 5 ? 0x20u : 0u));
	case SSXMsg::BigAir: return 1;
	default: return 0;
	}
}

uint32 FSSXAnnouncer::ComboWord(int32 Value)
{
	switch (Value)
	{
	case 0: return 0;
	case 1: return 1;
	case 2: return 2;
	case 3: return 4;
	default: return 8;
	}
}

void FSSXAnnouncer::Message(FSSXSpeechSystem& Speech, int32 Type, int32 Value, bool bFinished, FRandA RandA)
{
	// 0x233220
	if (bFinished || Mode == 6 || Mode == 1 || bReplay || !Gate(3, RandA))
	{
		return;
	}
	const uint32 Bit = MessageBit(Type, Value);
	if (Bit)
	{
		Post(Speech, { Mc(0x204E), Bit });
	}
	if (Type == SSXMsg::Combo || Type == SSXMsg::ComboBonus)
	{
		Post(Speech, { Mc(0x2031), ComboWord(Value) });
	}
}

void FSSXAnnouncer::SegmentStart()
{
	TrickGate = 1;
	TrickNamed = 0;
}

int32 FSSXAnnouncer::TrickClass(int32 Points)
{
	// 0x232498
	static const int32 Table[3] = { 0, 4000, 10000 };
	int32 C = 2;
	while (C >= 0 && Points < Table[C]) --C;
	return C;
}

int32 FSSXAnnouncer::GrabPage(uint8 B)
{
	// 0x232498
	static const int16 Low[30] = { -1, 0xF, 0x15, 0x10, 0x2C, 0x19, 7, 0x11, 4, 10, 0x12, 0x13, 5, 3, 1, 0x16, 0x17, 0xB, 9, 0x68, 0xC,
		0x18, 8, 6, 0x14, 0xE, 2, 0x6F, 0x70, 0x23 };
	if (B < 30) return Low[B];
	if (B <= 100) return 0x71 + (B - 30);
	if (B == 101) return 0x1E;
	return -1;
}

int32 FSSXAnnouncer::FlipPage(uint8 B)
{
	// 0x232498
	static const int16 Tab[45] = { -1, 0x67, 0x69, 0x6A, 0x6B, 0x6C, 0x48, 0x44, 0x45, 0x3A, 0x46, 0x49, 0x24, 0x3B, 0x25, 0x34, 0x26, 0x28,
		0x3C, 0x2F, 0x41, 0x35, 0x36, 0x42, 0x40, 0x47, 0x4E, 0x4D, 0x4C, 0x4B, 0x2A, 0x27, 0x4F, 0x37, 0x38, 0x31, 0x2E, 0x30, 0x3D,
		0x3F, 0x3E, 0x4A, 0x43, 0x29, 0x3D };
	return B < 45 ? Tab[B] : -1;
}

bool FSSXAnnouncer::TrickWords(uint32 W0, uint32 W1, uint32 Out[8], int32& OutFollowPage)
{
	OutFollowPage = -1;
	for (int32 K = 0; K < 8; ++K) Out[K] = 0;
	uint32& Body = Out[0];
	uint32& Kind = Out[1];
	uint32& Spin = Out[2];
	uint32& Flip = Out[3];
	uint32& Rail = Out[4];
	uint32& Late = Out[7];
	const uint32 K16 = (W1 >> 16) & 0xF;
	if (K16 == 0 || K16 == 1) Kind = 2;
	else if (K16 == 2 || K16 == 3) Kind = 1;
	if ((W1 & 0xF0F0) == 0 && (W0 & 0x0F000000) == 0 && (W1 & 0xF00) == 0 && (W0 & 0xFFFF) == 0)
	{
		return false;
	}
	const uint32 L27 = (W1 >> 27) & 7;
	if (L27 == 1) Late = 1;
	else if (L27 == 2) Late = 2;
	switch ((W1 >> 12) & 0xF)
	{
	case 0: Kind = 2; break;
	case 1: Kind = 4; Late |= 4; break;
	case 2: case 3: case 4: Kind = 1; Late |= 4; break;
	default: break;
	}
	const uint32 B24 = (W0 >> 24) & 0xF;
	if (B24 <= 10) Body = 1u << B24;
	const uint32 S24 = (W1 >> 24) & 7;
	Rail = S24 == 0 ? 1u : (S24 == 1 ? 2u : (S24 == 2 ? 4u : (S24 == 3 ? 8u : 0u)));
	switch ((W1 >> 8) & 0xF)
	{
	case 1: Spin = 1; break;
	case 2: Spin = 2; break;
	case 3: Flip = 1; break;
	case 4: Flip = 2; break;
	default: break;
	}
	Out[5] = uint32(GrabPage(uint8(W0 >> 8)));
	const uint8 B0 = uint8(W0);
	Out[6] = uint32(B0 == 0x2C ? 0x3D : FlipPage(B0));
	if ((W0 & 0xF0000000) != 0)
	{
		const uint32 B28 = W0 >> 28;
		if (B28 <= 10) Body = 1u << B28;
	}
	const uint8 B2 = uint8(W0 >> 16);
	if (B2 != 0) OutFollowPage = GrabPage(B2);
	return true;
}

void FSSXAnnouncer::TrickAnnounce(FSSXSpeechSystem& Speech, uint32 W0, uint32 W1, int32 Points, bool bRepeat, bool bFinished, FRandA RandA)
{
	if (bRepeat && Mode != 6)
	{
		// 0x2333E0
		if (!bFinished && Gate(4, RandA) && Mode != 6 && !bReplay) Post(Speech, { Mc(0x201A) });
		return;
	}
	// 0x232498
	if (bReplay || TrickGate == 0 || bFinished)
	{
		return;
	}
	{
		if (Gate(TrickClass(Points), RandA))
		{
			Post(Speech, { Mc(0x201C), 4, 2, 2, uint32(Points < 4000 ? 1 : 2) });
			uint32 Words[8];
			int32 Follow = -1;
			if (TrickNamed == 0 && TrickWords(W0, W1, Words, Follow))
			{
				uint32 Full[9] = { Mc(0x200F) };
				for (int32 K = 0; K < 8; ++K) Full[K + 1] = Words[K];
				const bool bPosted = PostWords(Speech, Full, 9);
				TrickGate = bPosted ? 1 : 0;
				if (bPosted)
				{
					++TrickNamed;
					if (uint8(W0 >> 16) != 0)
					{
						Post(Speech, { Mc(0x2010), uint32(Follow) });
					}
				}
			}
		}
	}
	if (Gate(16, RandA) && (Mode == 5 || Mode == 3))
	{
		Post(Speech, { Mc(0x2054), 1 });
	}
}

void FSSXAnnouncer::Bail(FSSXSpeechSystem& Speech, bool bFinished, int32 RankIndex, FRandA RandA)
{
	if (bFinished || bReplay || Mode == 6)
	{
		return;
	}
	if (Gate(8, RandA))
	{
		Post(Speech, { Mc(0x202E), RankWord(RankIndex) });
	}
	if (Gate(16, RandA) && (Mode == 5 || Mode == 3))
	{
		Post(Speech, { Mc(0x2054), 2 });
	}
}

void FSSXAnnouncer::ForcedReset(FSSXSpeechSystem& Speech, bool bFinished, bool bFocus, int32 MusicZone, FRandA RandA)
{
	if (!bFinished && !bReplay && Mode != 6 && bFocus && MusicZone != 0 && Gate(22, RandA))
	{
		Post(Speech, { Mc(0x2052) });
	}
}

void FSSXAnnouncer::BoostEmpty(FSSXSpeechSystem& Speech, bool bFinished, FRandA RandA)
{
	if (!bFinished && Gate(10, RandA) && Mode != 6 && !bReplay)
	{
		Post(Speech, { Mc(0x2033), 2 });
	}
}

void FSSXAnnouncer::KnockedDown(FSSXSpeechSystem& Speech, bool bAttackerFinished, int32 RelationKind, FRandA RandA)
{
	if (bAttackerFinished || bReplay || Mode == 6 || !Gate(20, RandA))
	{
		return;
	}
	const uint32 Bit = RelationKind == 1 ? 4u : (RelationKind == 0 ? 1u : (RelationKind == 2 ? 2u : 0u));
	Post(Speech, { Mc(0x2050), Bit });
}

void FSSXAnnouncer::ShoveHit(FSSXSpeechSystem& Speech, bool bFinished, FRandA RandA)
{
	if (!bFinished && Gate(7, RandA) && Mode != 6 && !bReplay)
	{
		Post(Speech, { Mc(0x202C) });
	}
}

void FSSXAnnouncer::Grudge(FSSXSpeechSystem& Speech, bool bAIFinished, FRandA RandA)
{
	if (!bAIFinished && !bReplay && Mode != 6 && Gate(17, RandA))
	{
		Post(Speech, { Mc(0x204F) });
	}
}

uint32 FSSXAnnouncer::FieldWord(int32 Strong, int32 Weak)
{
	uint32 W = 0;
	if (Strong >= 2)
	{
		W = (Weak > 1 ? 0x10u : 0u) | 4u;
	}
	else if (Strong == 0)
	{
		W = 2;
	}
	if (Weak >= 2)
	{
		W |= 1;
	}
	else if (Weak == 0)
	{
		W |= 8;
	}
	return W;
}

int32 FSSXAnnouncer::PreRaceCard(FSSXSpeechSystem& Speech, bool bChannel0Free, int32 Strong, int32 Weak, int32 CareerPlayers)
{
	Speech.UpdateChannel(0, !bChannel0Free);
	if ((Mode != 4 && Mode != 2) || CareerPlayers > 1)
	{
		return -1;
	}
	auto Narr = [this, &Speech](std::initializer_list<uint32> Words)
	{
		const SSXSpeech::FRequest R = SSXSpeech::MakeRequest(Words);
		Posted.Add(R[0]);
		Speech.Post(R);
		Speech.UpdateChannel(0, false);   // 0x228BD8
	};
	if (!bChannel0Free)
	{
		Narr({ SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8015), 2 });
	}
	const uint32 Field = FieldWord(Strong, Weak);
	Narr({ SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8017), Field, SSXSpeech::LanguageMask(Language) });
	return int32(Field);
}

uint32 FSSXAnnouncer::NarrVenueBit(int32 InVenue)
{
	switch (InVenue)
	{
	case 0: return 0x10;
	case 1: return 2;
	case 2: return 8;
	case 3: return 4;
	case 4: return 1;
	case 5: return 0x40;
	case 6: return 0x80;
	case 7: return 0x200;
	case 8: return 0x20;
	case 11: return 0x100;
	default: return 0x10;
	}
}

uint32 FSSXAnnouncer::NarrCharacterBit(int32 Character)
{
	static const uint32 Bits[12] = { 0x10, 0x100, 0x40, 1, 0x800, 4, 0x20, 2, 0x400, 8, 0x80, 0x200 };
	return Character >= 0 && Character < 12 ? Bits[Character] : 0u;
}

TArray<uint32> FSSXAnnouncer::LessonNarration(FSSXSpeechSystem& Speech, int32 Step, bool bReplayState)
{
	TArray<uint32> Out;
	if (bReplayState)
	{
		return Out;
	}
	const uint32 Word = SSXLesson::NarrationWord(Step, EndFlags);
	// 0x230388 / 0x2309A8
	Out = { SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x800F), Word, SSXSpeech::LanguageMask(Language), 1u };
	SSXSpeech::FRequest R = SSXSpeech::MakeRequest({});
	for (int32 K = 0; K < Out.Num(); ++K) R[K] = Out[K];
	Posted.Add(R[0]);
	Speech.Request(R);   // 0x228BD8
	return Out;
}

TArray<uint32> FSSXAnnouncer::RankUpNarration(FSSXSpeechSystem& Speech, int32 Rank)
{
	// 0x2314E0 / 0x230400 / 0x3A2750 / 0x2309A8
	const uint32 Bit = (Rank >= 0 && Rank < 10) ? (1u << Rank) : 0u;
	TArray<uint32> Out = { SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8010), Bit, SSXSpeech::LanguageMask(Language) };
	SSXSpeech::FRequest R = SSXSpeech::MakeRequest({});
	for (int32 K = 0; K < Out.Num(); ++K) R[K] = Out[K];
	Posted.Add(R[0]);
	Speech.Request(R);   // 0x228BD8
	return Out;
}

TArray<uint32> FSSXAnnouncer::StagingNarration(FSSXSpeechSystem& Speech, int32 Kind, TFunctionRef<int32(int32, int32)> Relation)
{
	TArray<uint32> Out;
	if (Mode != 4 || HeatWord != 0 || PlayerCharacter < 0)
	{
		return Out;
	}
	uint32 Set = 0;
	if (Kind == 0)
	{
		Set = 0x2000;
	}
	else if (Kind == 1 || Kind == 2)
	{
		for (int32 R = 0; R < 12; ++R)
		{
			if (Relation(PlayerCharacter, R) == (Kind == 1 ? 2 : 1)) Set |= NarrCharacterBit(R);
		}
	}
	const uint32 Lang = SSXSpeech::LanguageMask(Language);
	Out = { SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8006), NarrVenueBit(Venue) | 0x400u, Set, NarrCharacterBit(PlayerVoice >= 0 ? PlayerVoice : PlayerCharacter), Lang };
	SSXSpeech::FRequest R = SSXSpeech::MakeRequest({});
	for (int32 K = 0; K < Out.Num(); ++K) R[K] = Out[K];
	Posted.Add(R[0]);
	Speech.Request(R);   // 0x228BD8
	return Out;
}

uint32 FSSXAnnouncer::RankChangeWord(int32 OldRank, int32 NewRank)
{
	switch (NewRank)
	{
	case 0: return (OldRank == 5 ? 0x101u : 1u) | 0x400000u;
	case 1: return (OldRank == 0 ? 0x22u : 2u) | 0x400000u;
	case 2: return 0x400004u;
	case 3: return 8u;
	case 4: return 0x2000u;
	case 5: return OldRank == 0 ? 0x4090u : 0x4010u;
	default: return 0u;
	}
}

uint32 FSSXAnnouncer::RankWord(int32 ProfileRank)
{
	return ProfileRank < 5 ? (ProfileRank > 1 ? 10u : 12u) : 9u;
}

// 0x231548 / 0x231800 / 0x231AF8 / 0x2315E0
namespace
{
	uint32 Narr(int32 Event) { return SSXSpeech::RequestId(SSXSpeech::DbNarr, Event); }

	// 0x2315E0
	void PerfThresholds(int32 Venue, int32& Fast, int32& Slow, int32& Resp, int32& Wipe)
	{
		Fast = Slow = Resp = Wipe = 0;
		switch (Venue)
		{
		case 0: Fast = 30000; Slow = 90000; Resp = 5; Wipe = 10; break;
		case 1: Fast = 25000; Slow = 75000; Resp = 4; Wipe = 8; break;
		case 2: Fast = 0x9664; Slow = 0x1C32C; Resp = 6; Wipe = 12; break;
		case 3: Fast = 27000; Slow = 81000; Resp = 4; Wipe = 9; break;
		case 4: case 8: Fast = 32000; Slow = 96000; Resp = 5; Wipe = 10; break;
		case 5: Fast = 21000; Slow = 63000; Resp = 3; Wipe = 7; break;
		case 6: case 7: case 9: Fast = 0x1E23; Slow = 0x5A69; Resp = 1; Wipe = 2; break;
		case 11: Fast = 0x86C4; Slow = 0x1944C; Resp = 5; Wipe = 11; break;
		default: break;
		}
	}
}

uint32 FSSXAnnouncer::PerformanceMask(const FRaceEndInfo& Info, int32 Slot) const
{
	uint32 M = 0;
	for (const FRaceEndInfo::FRacer& R : Info.Racers)
	{
		if (R.Slot != Slot)
		{
			continue;
		}
		int32 Fast, Slow, Resp, Wipe;
		PerfThresholds(Venue, Fast, Slow, Resp, Wipe);
		if (float(R.Frames) <= float(Fast)) M |= 0x10C;
		if (float(Slow) <= float(R.Frames)) M |= 0x230;
		if (Resp <= R.Respawns) M |= 0x240;
		if (Wipe <= R.WipeOuts) M |= 0x280;
		if (R.Knockdowns == 0) M |= 0x101;
		if (R.Knockdowns > 1) M |= 0x202;
		if (M == 0) M = 0x300;
	}
	return M;
}

bool FSSXAnnouncer::RaceEndNarration(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info)
{
	if ((EndFlags & 1) || Info.bReplayState)
	{
		return false;
	}
	switch (Mode)
	{
	case 2: case 4: return RaceEndRace(Speech, Info);
	case 3: case 5: return RaceEndShowoff(Speech, Info);
	default: return false;
	}
}

bool FSSXAnnouncer::RaceEndRace(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info)
{
	if ((EndFlags & 1) || Info.bReplayState)
	{
		return false;
	}
	const int32 N = Info.Order.Num();
	int32 Slot = -1, Place = -1;
	for (int32 K = 0; K < N; ++K)
	{
		const int32 R = Info.Order[K];
		const int32 S = Info.Racers.IsValidIndex(R) ? Info.Racers[R].Slot : -1;
		if (S >= 0) { Slot = S; Place = K; break; }
	}
	uint32 PlaceWord = 0;
	if (Place == 0 || Place != N - 1)
	{
		static const uint32 Words[5] = { 1, 2, 4, 8, 0x10 };
		PlaceWord = Place >= 0 && Place < 5 ? Words[Place] : 0;
	}
	else
	{
		PlaceWord = 0x20;
	}
	const uint32 RecordWord = (Info.PendingSlots.Contains(Slot) ? 2u : 1u) | 4u;
	const uint32 Lang = SSXSpeech::LanguageMask(Language);
	const uint32 Perf = PerformanceMask(Info, Slot);
	auto Post = [this, &Speech](std::initializer_list<uint32> Words)
	{
		const SSXSpeech::FRequest Rq = SSXSpeech::MakeRequest(Words);
		Posted.Add(Rq[0]);
		Speech.Post(Rq);
	};
	Post({ Narr(0x8007), PlaceWord, RecordWord, Lang, Perf, 1 });
	if (Mode == 4 && Info.HeatWord == 2)
	{
		const uint32 MedalWord = Place == 0 ? 8u : (Place == 1 ? 4u : (Place == 2 ? 2u : 1u));
		Post({ Narr(0x8009), MedalWord, 2, Lang, Perf, 1 });
	}
	Speech.UpdateChannel(0, false);   // 0x228BD8
	EndFlags |= 1;
	return true;
}

bool FSSXAnnouncer::RaceEndShowoff(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info)
{
	if ((EndFlags & 1) || Info.bReplayState || Info.Racers.Num() >= 2)
	{
		return false;
	}
	const int32 M = Info.ShowoffMedal0;
	const uint32 MedalWord = M == 0 ? 8u : (M == 1 ? 4u : (M == 2 ? 2u : 1u));
	const int32 Slot0 = Info.Racers.Num() > 0 ? Info.Racers[0].Slot : -1;
	const uint32 Perf = PerformanceMask(Info, Slot0) & 0x3C0u;
	// 0x228D10 / 0x2286E8
	Speech.Stop(0);
	const SSXSpeech::FRequest Rq = SSXSpeech::MakeRequest({ Narr(0x8013), MedalWord, 3, SSXSpeech::LanguageMask(Language), Perf, 0x11, 1 });
	Posted.Add(Rq[0]);
	Speech.Post(Rq);
	Speech.UpdateChannel(0, false);
	EndFlags |= 1;
	return true;
}

void FSSXAnnouncer::ResultsOpen(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info, bool bChannel0Free)
{
	if (EndFlags & 0x10)
	{
		return;
	}
	EndFlags |= 0x10;
	auto Post = [this, &Speech](std::initializer_list<uint32> Words)
	{
		const SSXSpeech::FRequest Rq = SSXSpeech::MakeRequest(Words);
		Posted.Add(Rq[0]);
		Speech.Post(Rq);
		Speech.UpdateChannel(0, false);
	};
	if (RaceEndNarration(Speech, Info))
	{
		Post({ Narr(0x8015), 2 });
	}
	const uint32 Lang = SSXSpeech::LanguageMask(Language);
	const int32 N = Info.Racers.Num();
	if (Mode == 4)
	{
		int32 FirstSlot = -1;
		bool bTopHalf = false;
		for (int32 K = 0; K < Info.Order.Num(); ++K)
		{
			const int32 R = Info.Order[K];
			const int32 S = Info.Racers.IsValidIndex(R) ? Info.Racers[R].Slot : -1;
			if (S >= 0)
			{
				if (FirstSlot < 0) FirstSlot = S;
				if (K < N / 2) { bTopHalf = true; break; }
			}
		}
		if (bTopHalf)
		{
			const uint32 HeatBits = Info.HeatWord == 1 ? 9u : (Info.HeatWord == 0 ? 3u : (Info.HeatWord == 2 ? 4u : 1u));
			Post({ Narr(0x800C), 0xB, HeatBits, Lang, 2 });
			int32 Best = 2;
			uint32 RiderBit = 0;
			const int32 Upto = Info.HeatWord == 2 ? N : N / 2;
			for (int32 K = 0; K < Upto && K < Info.Order.Num(); ++K)
			{
				const int32 R = Info.Order[K];
				if (!Info.Racers.IsValidIndex(R)) continue;
				if (Best <= Info.Racers[R].TowardHuman)
				{
					RiderBit = NarrCharacterBit(Info.Racers[R].Character);
					Best = Info.Racers[R].TowardHuman;
				}
			}
			if (RiderBit != 0)
			{
				if (!bChannel0Free)
				{
					Post({ Narr(0x8015), 2 });
				}
				Post({ Narr(0x800A), RiderBit, Lang });
			}
		}
	}
	else if (Mode == 5 && Info.ShowoffMedal0 != 3)
	{
		Post({ Narr(0x800C), 0x11, 4, Lang, 2 });
	}
	Speech.UpdateChannel(0, false);
}

void FSSXAnnouncer::HiScoreRow(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info, int32 Row)
{
	if (RaceEndNarration(Speech, Info))
	{
		return;
	}
	uint32 ModeWord = 1, RowWord = 1;
	switch (Mode)
	{
	case 3: case 5: ModeWord = 0x11; if (Row < 10) RowWord = 5; break;
	case 4: ModeWord = 3 | 8; if (Row < 5) RowWord = 3; break;
	case 2: ModeWord = 1 | 8; if (Row < 5) RowWord = 3; break;
	default: break;
	}
	const SSXSpeech::FRequest Rq = SSXSpeech::MakeRequest({ Narr(0x800B), ModeWord, RowWord, SSXSpeech::LanguageMask(Language) });
	Posted.Add(Rq[0]);
	Speech.Post(Rq);
	Speech.UpdateChannel(0, false);
}

void FSSXAnnouncer::UnlockedOpen(FSSXSpeechSystem& Speech, const FRaceEndInfo& Info, TConstArrayView<TPair<int32, int32>> Unlocks)
{
	if (RaceEndNarration(Speech, Info))
	{
		const SSXSpeech::FRequest Rq = SSXSpeech::MakeRequest({ Narr(0x8015), 2 });
		Posted.Add(Rq[0]);
		Speech.Post(Rq);
		Speech.UpdateChannel(0, false);
	}
	const uint32 Lang = SSXSpeech::LanguageMask(Language);
	for (const TPair<int32, int32>& U : Unlocks)
	{
		if (U.Key < 0 || U.Key >= 3) continue;
		uint32 Bit = 0;
		switch (U.Value)
		{
		case 0: Bit = 0x10; break; case 1: Bit = 2; break; case 2: Bit = 8; break; case 3: Bit = 4; break;
		case 4: Bit = 1; break; case 5: Bit = 0x40; break; case 6: Bit = 0x80; break; case 7: Bit = 0x200; break;
		case 8: Bit = 0x20; break; case 11: Bit = 0x100; break; default: break;
		}
		const SSXSpeech::FRequest Rq = SSXSpeech::MakeRequest({ Narr(0x800D), Bit, Lang });
		Posted.Add(Rq[0]);
		Speech.Post(Rq);
	}
	Speech.UpdateChannel(0, false);
}