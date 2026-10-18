#include "Audio/SSXCrowdSequencer.h"

namespace SSXCrowdSeq
{
	namespace
	{
		bool HasSecondByte(uint8 Status)
		{
			const uint8 Hi = Status & 0xF0;
			return Hi == 0x80 || Hi == 0x90 || Hi == 0xB0 || Hi == 0xE0;
		}
		bool IsEnd(uint8 Status, uint8 D1) { return (Status & 0xF0) == 0xF0 && D1 == '/'; }
	}

	bool FSequence::Parse(TConstArrayView<uint8> File, FString* OutError)
	{
		Data.Reset();
		Events.Reset();
		auto Fail = [OutError](const TCHAR* Msg) { if (OutError) { *OutError = Msg; } return false; };
		// 0x2D4440
		if (File.Num() < 4 || File[0] != 'M' || File[1] != 'I' || File[2] != 'D' || File[3] != 'x')
		{
			return Fail(TEXT("no MIDx tag"));
		}
		Data.Append(File.GetData() + 4, File.Num() - 4);
		int32 P = 0;
		while (true)
		{
			if (P + 3 > Data.Num())
			{
				return Fail(TEXT("no end token"));
			}
			FEvent E;
			E.Offset = P;
			E.Wait = Data[P];
			E.Status = Data[P + 1];
			E.Data1 = Data[P + 2];
			P += 3;
			if (IsEnd(E.Status, E.Data1))
			{
				Events.Add(E);
				return true;
			}
			if (HasSecondByte(E.Status))
			{
				if (P >= Data.Num())
				{
					return Fail(TEXT("truncated event"));
				}
				E.Data2 = Data[P++];
			}
			Events.Add(E);
		}
	}

	int32 FSequence::LengthTicks() const
	{
		int32 T = 0;
		for (int32 I = 0; I + 1 < Events.Num(); ++I)
		{
			T += Events[I].Wait;
		}
		return T;
	}

	void FPatch::SetTag(int32 Tag, int32 Value)
	{
		bValid = true;
		switch (Tag)
		{
		case 0x01: VelLo = Value; break;
		case 0x02: VelHi = Value; break;
		case 0x03: KeyLo = Value; break;
		case 0x04: KeyHi = Value; break;
		case 0x06: Priority = Value; break;
		case 0x07: Root = Value; break;
		case 0x0A: BendRange = Value; break;
		case 0x0C: Pan = Value; break;
		case 0x0D: PanRand = Value; break;
		case 0x0E: Volume = Value; break;
		case 0x0F: VolRand = Value; break;
		case 0x10: Fine = int16(Value); break;
		case 0x11: PitchRand = int16(Value); break;
		default: break;
		}
	}

	uint32 FLibRandom::Next()
	{
		// 0x2D6928
		const uint32 A = W[5] + W[4];
		const uint32 C0 = (A < W[5] || A < W[4]) ? 1u : 0u;
		const uint32 B = A + W[3] + C0;
		const uint32 C = B + W[2] + (B < W[3] ? 1u : 0u);
		W[5] = W[5] + 1;
		const uint32 D = C + W[1] + (C < W[2] ? 1u : 0u);
		W[0] = D + W[0] + (D < W[1] ? 1u : 0u);
		W[1] = D;
		W[2] = C;
		W[3] = B;
		W[4] = A;
		if (W[5] == 0 && (W[4] = A + 1) == 0 && (W[3] = B + 1) == 0 && (W[2] = C + 1) == 0 && (W[1] = D + 1) == 0)
		{
			W[0] = W[0] + 1;
		}
		return W[0];
	}

	int32 FLibRandom::Spread(int32 Range)
	{
		Range = FMath::Clamp(Range, 0, 0x10000);
		return int32(((int32(Next() & 0x7FFF) - 0x4000) * Range)) >> 14;
	}

	bool ResolveNote(const FPatch& P, int32 Key, int32 Velocity, FLibRandom& Rng, FNote& Out)
	{
		// 0x2D5E18
		if (!(P.VelLo <= Velocity && Velocity <= P.VelHi && P.KeyLo <= Key && Key <= P.KeyHi))
		{
			return false;
		}
		// 0x2D59D8
		int32 Pan = P.Pan;
		if (P.PanRand != 0)
		{
			Pan = FMath::Clamp(Pan + Rng.Spread(P.PanRand), 0, 0x7F);
		}
		Out.PanMidi = Pan;
		int32 Cents = P.Fine + (Key - P.Root) * 100;
		if (P.PitchRand != 0)
		{
			Cents += Rng.Spread(P.PitchRand);
		}
		Out.Cents = int16(Cents);
		int32 Vol = P.Volume;
		if (P.VolRand != 0)
		{
			Vol += Rng.Spread(P.VolRand);
		}
		Vol = FMath::Clamp(Vol, 0, 0x7F);
		Out.Volume127 = (Vol * Velocity) / 0x7F;
		Out.BendRangeCents = P.BendRange * 100;
		return true;
	}

	void FPlayer::Start(const FSequence& InSeq, int32 InVolume, int32 InTempo)
	{
		// 0x2D4440
		Seq = &InSeq;
		Ptr = 0;
		LoopPtr = 0;
		LoopCount = 0xFF;
		Counter = 1;
		State = 3;
		Frame = 0;
		Steps = 0;
		Tempo = InTempo;
		Volume = InVolume;
		for (FChannel& C : Channels) { C = FChannel(); }
		for (FVoiceRec& V : Voices) { V = FVoiceRec(); }
		EventLog.Reset();
	}

	void FPlayer::Stop()
	{
		// 0x2D4368 / 0x2D3260
		for (FVoiceRec& V : Voices)
		{
			if (V.Voice >= 0 && StopVoice && IsVoicePlaying && IsVoicePlaying(V.Voice)) { StopVoice(V.Voice); }
			V = FVoiceRec();
		}
		State = 2;
	}

	void FPlayer::Tick()
	{
		// 0x2D4108
		if (State != 3)
		{
			return;
		}
		const uint32 Target = uint32((int64(Frame) * Tempo) / 100);
		++Frame;
		if (Steps <= Target)
		{
			while (State == 3)
			{
				Step();
				++Steps;
				if (Target < Steps)
				{
					break;
				}
			}
		}
	}

	bool FPlayer::NoVoices() const
	{
		// 0x2D3938
		for (const FVoiceRec& V : Voices)
		{
			if (V.Voice >= 0 && IsVoicePlaying && IsVoicePlaying(V.Voice))
			{
				return false;
			}
		}
		return true;
	}

	void FPlayer::Step()
	{
		// 0x2D3F00
		--Counter;
		if (IsEnd(Byte(Ptr + 1), Byte(Ptr + 2)))
		{
			if (NoVoices())
			{
				Stop();
			}
			return;
		}
		if (Counter != 0)
		{
			return;
		}
		while (true)
		{
			uint8 Wait = Byte(Ptr);
			const uint8 Status = Byte(Ptr + 1);
			const uint8 D1 = Byte(Ptr + 2);
			Ptr += 3;
			if (bLogEvents)
			{
				EventLog.Add(FString::Printf(TEXT("%u:%02X:%02X"), Steps, Status, D1));
			}
			if (IsEnd(Status, D1))
			{
				if (!NoVoices())
				{
					Wait = 1;
					Ptr -= 3;
				}
				else
				{
					Stop();
				}
			}
			else
			{
				Dispatch(Status, D1);
			}
			if (Wait != 0)
			{
				Counter = Wait;
				return;
			}
			if (State != 3)
			{
				break;
			}
		}
		Counter = 0;
	}

	void FPlayer::Dispatch(uint8 Status, uint8 D1)
	{
		const int32 Ch = Status & 0x0F;
		FChannel& C = Channels[Ch];
		switch (Status & 0xF0)
		{
		case 0x80:
		{
			// 0x2D3B90
			++Ptr;
			for (FVoiceRec& V : Voices)
			{
				if (V.Voice >= 0 && V.Channel == Ch && V.Key == D1 && V.Velocity >= 0)
				{
					if (NoteOff) { NoteOff(V.Voice); }
					V.Velocity = -1;
					break;
				}
			}
			break;
		}
		case 0x90:
		{
			// 0x2D39C8
			int32 Free = -1;
			for (int32 K = 1; K <= NumVoices; ++K)
			{
				const int32 I = (NextVoice + K) % NumVoices;
				if (Voices[I].Voice < 0 || !(IsVoicePlaying && IsVoicePlaying(Voices[I].Voice)))
				{
					Free = I;
					NextVoice = I;
					break;
				}
			}
			const uint8 Velocity = Byte(Ptr);
			++Ptr;
			if (Free < 0)
			{
				break;
			}
			FNoteOn N;
			N.Channel = Ch;
			N.Program = C.Program == 0xFF ? -1 : C.Program;
			N.Key = D1;
			N.Velocity = Velocity;
			N.Level = (C.Volume * Volume) >> 7;
			N.PanMidi = C.PanMidi;
			N.Bend = C.Bend;
			FVoiceRec& V = Voices[Free];
			V.Voice = NoteOn ? NoteOn(N) : -1;
			V.Velocity = int8(Velocity);
			V.Channel = Ch;
			V.Key = D1;
			break;
		}
		case 0xB0:
		{
			// 0x2D3C30
			const uint8 Value = Byte(Ptr);
			if (D1 == 0x14)
			{
				LoopPtr = Ptr + 1;
				Ptr = LoopPtr;
				break;
			}
			if (D1 == 0x1E)
			{
				if (LoopCount >= 0x7F)
				{
					LoopCount = Value;
				}
				if (LoopCount != 0)
				{
					--LoopCount;
					Ptr = LoopPtr;
					break;
				}
				++Ptr;
				break;
			}
			++Ptr;
			if (D1 == 7)
			{
				C.Volume = Value & 0x7F;
				for (const FVoiceRec& V : Voices)
				{
					if (V.Voice >= 0 && V.Channel == Ch && SetVoiceLevel && IsVoicePlaying && IsVoicePlaying(V.Voice)) { SetVoiceLevel(V.Voice, (C.Volume * Volume) >> 7); }
				}
			}
			else if (D1 == 10)
			{
				// 0x2D3480 / 0x351998
				C.PanMidi = Value;
			}
			break;
		}
		case 0xC0:
			C.Program = D1;
			break;
		case 0xE0:
		{
			// 0x2D3E18 / 0x2D6A18
			C.Bend = ((int32(Byte(Ptr)) * 0x80 + D1) >> 7) & 0x7F;
			++Ptr;
			break;
		}
		default:
			break;   // 0xA0 / 0xD0 / 0xF0 class: the 3 bytes only
		}
	}

	void FPlayer::SetVolume(int32 InVolume)
	{
		Volume = InVolume;
		for (const FVoiceRec& V : Voices)
		{
			if (V.Voice >= 0 && SetVoiceLevel && IsVoicePlaying && IsVoicePlaying(V.Voice)) { SetVoiceLevel(V.Voice, (Channels[V.Channel].Volume * Volume) >> 7); }
		}
	}
}
