#include "Audio/SSXCrowdSystem.h"

#include "Audio/SSXAudioConfig.h"

void FSSXCrowdSystem::LoadConfig(const FSSXInfFile& Inf, const FString& TrackSection,
	TFunctionRef<bool(const FString& Path, TArray<uint8>& Out)> LoadFile)
{
	// 0x220CE8
	static const TCHAR* const ListKeys[3] = { TEXT("TRICK"), TEXT("FALL"), TEXT("ANTICIPATE") };
	static const TCHAR* const FloatKeys[3] = { TEXT("ATTACK"), TEXT("DECAY"), TEXT("MAXBEND") };
	auto Section = [&](const FString& Name)
	{
		const FSSXInfFile::FSection* S = Inf.Find(Name);
		if (!S)
		{
			return;
		}
		for (const TPair<FString, FString>& KV : S->Values)
		{
			bool bDone = false;
			for (int32 L = 0; L < SSXCrowd::Levels && !bDone; ++L)
			{
				for (int32 K = 0; K < 3 && !bDone; ++K)
				{
					if (KV.Key == FString(ListKeys[K]) + FString::FromInt(L + 1))
					{
						bDone = true;
						TArray<int32>& List = Records[L].Lists[K];
						TArray<uint8> Bytes;
						SSXCrowdSeq::FSequence Seq;
						if (List.Num() < SSXCrowd::MaxPerList && LoadFile(KV.Value, Bytes) && Seq.Parse(Bytes))
						{
							Seq.Name = FPaths::GetBaseFilename(KV.Value.Replace(TEXT("\\"), TEXT("/")));
							List.Add(Sequences.Add(MoveTemp(Seq)));
						}
					}
					else if (KV.Key == FString(FloatKeys[K]) + FString::FromInt(L + 1))
					{
						bDone = true;
						const float V = FCString::Atof(*KV.Value);
						(K == 0 ? Records[L].Attack : K == 1 ? Records[L].Decay : Records[L].MaxBend) = V;
					}
				}
			}
		}
	};
	Section(TEXT("GLOBAL"));
	if (!TrackSection.IsEmpty())
	{
		Section(TrackSection);   // 0x211098
	}
}

void FSSXCrowdSystem::Begin(int32 NumCrowds)
{
	// 0x2209C8 / 0x21EE70
	End();
	Crowds.SetNum(FMath::Max(NumCrowds, 0));
}

void FSSXCrowdSystem::End()
{
	// 0x220BB8 / 0x221330
	RemoveAllEntries();
	Crowds.Reset();
	for (FRecord& R : Records) { R = FRecord(); }
	Sequences.Reset();
}

void FSSXCrowdSystem::RemoveAllEntries()
{
	// 0x221330 / 0x220338 / 0x2201F8
	for (FCrowd& C : Crowds)
	{
		for (int32 E = 0; E < SSXCrowd::MaxEntries; ++E)
		{
			if (C.Entries[E].bUsed)
			{
				RemoveEntry(C, E, SSXCrowd::NoFade);
			}
		}
	}
}

int32 FSSXCrowdSystem::AddEmitter(int32 Listener, const FVector3f& Pos, int32 Weight, int32 SoundIndex, bool bPositioned)
{
	// 0x2212A0 / 0x21FC98
	if (!Crowds.IsValidIndex(Listener))
	{
		return -1;   // the original indexes past the crowd array (its own out-of-range read; not reproduced)
	}
	FCrowd& C = Crowds[Listener];
	if (C.Count >= SSXCrowd::MaxEntries || !bPositioned)
	{
		return -1;
	}
	int32 E = 0;
	while (E < SSXCrowd::MaxEntries && C.Entries[E].bUsed) { ++E; }
	++C.Count;
	FEntry& En = C.Entries[E];
	En.bUsed = true;
	En.Position = Pos;
	En.Weight = Weight;
	En.Record = -1;
	for (int32 R = 0; R < SSXCrowd::MaxRecords; ++R)
	{
		if (C.Records[R].Index == SoundIndex)
		{
			En.Record = R;
			++C.Records[R].Refs;
			break;
		}
	}
	if (C.Count == 1)
	{
		C.Listener = Listener;
		C.Position = Pos;
	}
	if (En.Record < 0)
	{
		for (int32 R = 0; R < SSXCrowd::MaxRecords; ++R)
		{
			if (C.Records[R].Refs == 0)
			{
				FVoiceRec& V = C.Records[R];
				V.Index = SoundIndex;
				V.Refs = 1;
				++C.Voices;
				En.Record = R;
				V.Position = Pos;
				FSSXCrowdStart S;   // 0x20C948
				S.Listener = C.Listener;
				S.SoundIndex = SoundIndex;
				S.Volume = Weight;
				S.Position = V.Position;
				V.Handle = Start ? Start(S) : -1;
				break;
			}
		}
	}
	return (E << 8) | Listener;
}

void FSSXCrowdSystem::SetEmitterWeight(int32 Handle, int32 Weight)
{
	// 0x221000 / 0x21FC68
	if (Handle < 0 || !Crowds.IsValidIndex(Handle & 0xFF))
	{
		return;
	}
	FCrowd& C = Crowds[Handle & 0xFF];
	const int32 E = Handle >> 8;
	if (E >= 0 && E < SSXCrowd::MaxEntries && C.Entries[E].bUsed)
	{
		C.Entries[E].Weight = Weight;
	}
}

void FSSXCrowdSystem::RemoveEmitter(int32 Handle, float Fade)
{
	// 0x220F98
	if (Handle < 0 || !Crowds.IsValidIndex(Handle & 0xFF))
	{
		return;
	}
	RemoveEntry(Crowds[Handle & 0xFF], Handle >> 8, Fade);
}

void FSSXCrowdSystem::RemoveEntry(FCrowd& C, int32 E, float Fade)
{
	// 0x2201F8
	if (E < 0 || E >= SSXCrowd::MaxEntries || !C.Entries[E].bUsed)
	{
		return;
	}
	FEntry& En = C.Entries[E];
	En.bUsed = false;
	--C.Count;
	const int32 R = En.Record;
	En.Record = -1;
	if (R >= 0)
	{
		FVoiceRec& V = C.Records[R];
		if (--V.Refs == 0)
		{
			if (V.Handle >= 0 && Stop) { Stop(V.Handle, Fade); }
			V.Index = -1;
			V.Handle = -1;
			C.State = 0;
			--C.Voices;
		}
	}
	if (C.Count == 0)
	{
		C.Hold = FMath::Min(C.Hold, 0.25f);
		if (C.Reaction >= 0)
		{
			if (Fade == 0.f && Stop)
			{
				Stop(C.Reaction, Fade);
			}
			C.Reaction = -1;
			if (C.ArmState == 2) { C.ArmState = 1; }
		}
		C.Volume = 0;
		C.Listener = -1;
	}
}

void FSSXCrowdSystem::Frame()
{
	for (FCrowd& C : Crowds)   // 0x220968
	{
		CrowdFrame(C);
	}
}

void FSSXCrowdSystem::CrowdFrame(FCrowd& C)
{
	// 0x21EFB0
	if (C.Count == 0)
	{
		return;
	}
	FVector3f GPos[SSXCrowd::MaxRecords] = { FVector3f::ZeroVector, FVector3f::ZeroVector, FVector3f::ZeroVector };
	int32 GMax[SSXCrowd::MaxRecords] = { 0, 0, 0 };
	int32 GSum[SSXCrowd::MaxRecords] = { 0, 0, 0 };
	int32 E = 0;
	while (E < SSXCrowd::MaxEntries && !C.Entries[E].bUsed) { ++E; }
	if (E >= SSXCrowd::MaxEntries)
	{
		return;
	}
	auto Lerp = [](FVector3f& P, const FVector3f& Q, int32 W, int32 Other)
	{
		// t = w / (other + w); 0 / 0 on the EE FPU (two silent entries) is not reproduced (t = 0 here)
		const float Den = float(Other + W);
		const float T = Den != 0.f ? float(W) / Den : 0.f;
		P.X = P.X + (Q.X - P.X) * T;
		P.Y = P.Y + (Q.Y - P.Y) * T;
		P.Z = P.Z + (Q.Z - P.Z) * T;
	};
	FVector3f Pos = C.Entries[E].Position;
	int32 Last = C.Entries[E].Record;
	if (Last >= 0)
	{
		GPos[Last] = Pos;
		GMax[Last] = C.Entries[E].Weight;
	}
	int32 MaxW = C.Entries[E].Weight;
	int32 SumQ = 0;
	const bool bGroups = C.Voices > 1;
	for (int32 K = 1; K < C.Count; ++K)
	{
		do { ++E; } while (E < SSXCrowd::MaxEntries && !C.Entries[E].bUsed);
		if (E >= SSXCrowd::MaxEntries)
		{
			break;
		}
		const FEntry& En = C.Entries[E];
		const int32 W = En.Weight;
		Lerp(Pos, En.Position, W, MaxW);
		int32 Q = W >> 2;
		if (MaxW < W)
		{
			Q = MaxW >> 2;
			MaxW = W;
		}
		SumQ += Q;
		Last = En.Record;
		if (bGroups && Last >= 0)
		{
			Lerp(GPos[Last], En.Position, W, GMax[Last]);
			if (GMax[Last] < W)
			{
				GSum[Last] += GMax[Last] >> 2;
				GMax[Last] = W;
			}
			else
			{
				GSum[Last] += W >> 2;
			}
		}
	}
	const int32 Total = MaxW + SumQ;
	auto Drive = [this, &C](FVoiceRec& V, const FVector3f& P, int32 Vol)
	{
		V.Position = P;
		if (IsPlaying && IsPlaying(V.Handle))
		{
			if (SetVolume) { SetVolume(V.Handle, Vol); }
			if (SetPosition) { SetPosition(V.Handle, V.Position); }
			return;
		}
		FSSXCrowdStart S;   // 0x20C948
		S.Listener = C.Listener;
		S.SoundIndex = V.Index;
		S.Volume = Vol;
		S.Position = V.Position;
		V.Handle = Start ? Start(S) : -1;
	};
	if (!bGroups)
	{
		if (Last >= 0 && C.Records[Last].Handle >= 0)
		{
			Drive(C.Records[Last], Pos, Total);
		}
	}
	else
	{
		for (int32 R = 0; R < SSXCrowd::MaxRecords; ++R)
		{
			FVoiceRec& V = C.Records[R];
			if (V.Refs != 0 && V.Handle >= 0)
			{
				Drive(V, GPos[R], GMax[R] + GSum[R]);
			}
		}
	}
	// 0x21FA1C
	if (C.ArmState != 0)
	{
		const int32 Q = Anticipation ? Anticipation(C.Listener) : 3;
		if (Q == 1 || Q == 2)
		{
			C.State = Q;
		}
		if (C.ArmState == 1 && Q != 3)
		{
			StopReaction(C);   // 0x220720
			const int32 Seq = Pick(Records[Q].Lists[2]);
			if (Seq >= 0)
			{
				FSSXCrowdStart S;   // 0x222160 / 0x222358
				S.Listener = C.Listener;
				S.Sequence = &Sequences[Seq];
				S.Volume = Total;
				S.Position = Pos;
				C.Reaction = Start ? Start(S) : -1;
				if (C.Reaction >= 0)
				{
					C.ArmState = 2;
				}
			}
		}
	}
	if (C.Reaction >= 0)
	{
		if (!(IsPlaying && IsPlaying(C.Reaction)))
		{
			StopReaction(C);
			C.State = 0;
		}
		else
		{
			if (SetVolume) { SetVolume(C.Reaction, Total); }
			if (SetPosition) { SetPosition(C.Reaction, Pos); }
		}
	}
	C.Volume = Total;
	C.Position = Pos;
}

void FSSXCrowdSystem::StopReaction(FCrowd& C)
{
	// 0x220720
	C.Hold = FMath::Min(C.Hold, 0.25f);
	C.ArmState = 0;
	if (C.Reaction >= 0)
	{
		if (Stop) { Stop(C.Reaction, 0.f); }
		C.Reaction = -1;
	}
}

int32 FSSXCrowdSystem::Pick(const TArray<int32>& List)
{
	// 0x21ED98
	if (List.Num() == 0)
	{
		return -1;
	}
	const uint32 R = Rand ? Rand() : 0u;
	return List[int32(R % uint32(List.Num()))];
}

void FSSXCrowdSystem::Reaction(FCrowd& C, int32 L, int32 List, bool bGroan)
{
	// 0x2203A8 / 0x220538
	StopReaction(C);
	if (C.Count == 0 || L < 0 || L >= SSXCrowd::Levels)
	{
		return;
	}
	C.LastLevel = L;
	C.Hold = Records[L].Attack + Records[L].Decay;
	const int32 Seq = Pick(Records[L].Lists[List]);
	if (Seq < 0)
	{
		return;
	}
	FSSXCrowdStart S;   // 0x222160 / 0x222358
	S.Listener = C.Listener;
	S.Sequence = &Sequences[Seq];
	S.Volume = C.Volume;
	S.Position = C.Position;
	C.Reaction = Start ? Start(S) : -1;
	C.State = bGroan ? ~L : L + 1;
}

void FSSXCrowdSystem::Cheer(int32 Listener, int32 L)
{
	if (Crowds.IsValidIndex(Listener)) { Reaction(Crowds[Listener], L, 0, false); }   // 0x221068
}

void FSSXCrowdSystem::Groan(int32 Listener, int32 L)
{
	if (Crowds.IsValidIndex(Listener)) { Reaction(Crowds[Listener], L, 1, true); }    // 0x221088
}

void FSSXCrowdSystem::Neutral(int32 Listener)
{
	// 0x2210C8 / 0x2206E0
	if (!Crowds.IsValidIndex(Listener))
	{
		return;
	}
	FCrowd& C = Crowds[Listener];
	if (C.ArmState == 2)
	{
		StopReaction(C);
	}
	C.ArmState = 0;
}

void FSSXCrowdSystem::Arm(int32 Listener)
{
	// 0x2210A8 / 0x2206C8
	if (Crowds.IsValidIndex(Listener) && Crowds[Listener].ArmState == 0)
	{
		Crowds[Listener].ArmState = 1;
	}
}

int32 FSSXCrowdSystem::AnticipationLevel(float TimeLeft, float PendingReward)
{
	// 0x20EA38
	if (!(TimeLeft <= 2.f) || !(0.f < PendingReward))
	{
		return 3;
	}
	return PendingReward < 0.25f ? 0 : (PendingReward < 0.5f ? 1 : 2);
}

void FSSXCrowdSystem::Cue(int32 Listener, double X, bool bBail)
{
	const int32 L = SSXCrowd::CueLevel(X);
	if (L < 0) { Neutral(Listener); }
	else if (bBail) { Groan(Listener, L); }
	else { Cheer(Listener, L); }
}

int32 FSSXCrowdSystem::Level(int32 Listener) const
{
	return Crowds.IsValidIndex(Listener) ? Crowds[Listener].State : 0;   // 0x2213F8
}
