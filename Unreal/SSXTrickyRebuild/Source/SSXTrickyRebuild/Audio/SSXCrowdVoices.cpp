#include "Audio/SSXCrowdVoices.h"

#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardAudio.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/SimCoords.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

namespace
{
	FString CrowdDir() { return FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("crowd")); }
}

FSSXCrowdVoices& USSXAudioSubsystem::GetCrowdVoices()
{
	if (!CrowdVoices.IsValid())
	{
		CrowdVoices = MakeShared<FSSXCrowdVoices>();
	}
	return *CrowdVoices;
}

bool FSSXCrowdVoices::Begin(USSXAudioSubsystem* Audio, UWorld* World, const FString& TrackSection)
{
	End();
	AudioRef = Audio;
	WorldRef = World;
	for (SSXCrowdSeq::FPatch& P : Patches) { P = SSXCrowdSeq::FPatch(); }
	FString Json;
	if (FFileHelper::LoadFileToString(Json, *FPaths::Combine(CrowdDir(), TEXT("crowd_patches.json"))))
	{
		TSharedPtr<FJsonObject> Root;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid())
		{
			const TSharedPtr<FJsonObject>* Ps = nullptr;
			if (Root->TryGetObjectField(TEXT("patches"), Ps))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*Ps)->Values)
				{
					const int32 Slot = FCString::Atoi(*KV.Key);
					if (Slot < 0 || Slot >= UE_ARRAY_COUNT(Patches) || !KV.Value->AsObject().IsValid())
					{
						continue;
					}
					Patches[Slot].bValid = true;
					for (const TPair<FString, TSharedPtr<FJsonValue>>& T : KV.Value->AsObject()->Values)
					{
						Patches[Slot].SetTag(FParse::HexNumber(*T.Key), int32(T.Value->AsNumber()));
					}
				}
			}
		}
	}
	Crowd.Begin(1);
	FSSXInfFile Inf;
	if (Inf.Load(SSXAudio::ConfigPath(TEXT("CROWD.INF"))))
	{
		Crowd.LoadConfig(Inf, TrackSection, [](const FString& Path, TArray<uint8>& Out)
		{
			const FString Name = FPaths::GetCleanFilename(Path.Replace(TEXT("\\"), TEXT("/")));
			return FFileHelper::LoadFileToArray(Out, *FPaths::Combine(CrowdDir(), Name), FILEREAD_Silent);
		});
	}
	FSSXInfFile Chant;
	if (Chant.Load(SSXAudio::ConfigPath(TEXT("CHANT.INF"))))
	{
		Chants.Load(Chant);
	}
	Crowd.Start = [this](const FSSXCrowdStart& S) { return StartVoice(S); };
	Crowd.IsPlaying = [this](int32 H)
	{
		const FVoice* V = Voices.Find(H);
		if (!V) { return false; }
		return V->Player.IsValid() ? V->Player->IsPlaying() : (V->Comp.IsValid() && V->Comp->IsPlaying());
	};
	Crowd.SetVolume = [this](int32 H, int32 Vol) { if (FVoice* V = Voices.Find(H)) { V->Volume = Vol; } };
	Crowd.SetPosition = [this](int32 H, const FVector3f& P)
	{
		if (FVoice* V = Voices.Find(H))
		{
			V->Position = P;
			const FVector At = SSXSim::GameToUnreal(FVector(P));
			if (V->Comp.IsValid()) { V->Comp->SetWorldLocation(At); }
			for (TPair<int32, FNoteVoice>& N : Notes)
			{
				if (N.Value.Owner == H && N.Value.Comp.IsValid()) { N.Value.Comp->SetWorldLocation(At); }
			}
		}
	};
	Crowd.Stop = [this](int32 H, float Fade)
	{
		FVoice V;
		if (!Voices.RemoveAndCopyValue(H, V))
		{
			return;
		}
		if (V.Player.IsValid())
		{
			V.Player->Stop();   // 0x20C2A0 / 0x2D4368
		}
		if (V.Comp.IsValid())
		{
			if (Fade > 0.f) { V.Comp->FadeOut(Fade, 0.f); } else { V.Comp->Stop(); }
			V.Comp->bAutoDestroy = true;
		}
	};
	Crowd.Rand = []() { return FGameRng::StreamA().Next(); };   // 0x21ED98
	Crowd.Anticipation = [this](int32) { return FSSXCrowdSystem::AnticipationLevel(ListenerTimeLeft, ListenerReward); };   // 0x20EA38
	bBegun = true;
	return Crowd.GetSequences().Num() > 0;
}

void FSSXCrowdVoices::End()
{
	if (bBegun)
	{
		Crowd.End();
	}
	for (TPair<int32, FVoice>& V : Voices)
	{
		if (V.Value.Comp.IsValid()) { V.Value.Comp->Stop(); V.Value.Comp->bAutoDestroy = true; }
	}
	for (TPair<int32, FNoteVoice>& N : Notes)
	{
		if (N.Value.Comp.IsValid()) { N.Value.Comp->Stop(); N.Value.Comp->bAutoDestroy = true; }
	}
	Voices.Reset();
	Notes.Reset();
	if (Spatial.IsValid())
	{
		Spatial->RemoveFromRoot();
		Spatial.Reset();
	}
	TickAccMs = 0.0;
	bBegun = false;
}

UAudioComponent* FSSXCrowdVoices::Spawn(const FString& Id, const FVector3f& Pos, bool bLoopFromManifest, float Pitch)
{
	USSXAudioSubsystem* Audio = AudioRef.Get();
	UWorld* World = WorldRef.Get();
	USoundWave* Wave = Audio && World ? Cast<USoundWave>(Audio->LoadSound(Id)) : nullptr;
	if (!Wave)
	{
		SSXAudioDiag::Note(TEXT("crowd"), Id, true, 0.f, 30.0, Audio && World ? Audio->MissReason(Id) : SSXAudioDiag::EResult::NoWorld);
		return nullptr;
	}
	const FSSXAudioEntry* E = Audio->GetManifest().Find(Id);
	Wave->bLooping = bLoopFromManifest && E && E->LoopStart >= 0;
	if (!Spatial.IsValid())
	{
		USoundAttenuation* A = NewObject<USoundAttenuation>(GetTransientPackage(), NAME_None, RF_Transient);
		A->AddToRoot();
		A->Attenuation.bAttenuate = false;
		A->Attenuation.bSpatialize = Audio->GetSpeaker() != 0;
		Spatial = A;
	}
	UAudioComponent* C = UGameplayStatics::SpawnSoundAtLocation(World, Wave, SSXSim::GameToUnreal(FVector(Pos)), FRotator::ZeroRotator, 0.f,
		Pitch, 0.f, Spatial.Get(), nullptr, false);
	SSXAudioDiag::Note(TEXT("crowd"), Id, true, 0.f, 30.0, SSXAudioDiag::SpawnResult(C, false), Wave->GetPathName());
	SSXAudioDiag::NoteLoop(TEXT("crowd"), Id, C, true);
	return C;
}

int32 FSSXCrowdVoices::StartVoice(const FSSXCrowdStart& S)
{
	const int32 H = NextHandle++;
	FVoice V;
	V.Position = S.Position;
	V.Volume = S.Volume;
	if (S.Sequence)
	{
		// 0x222160 / 0x222358 / 0x2D4440
		TSharedPtr<SSXCrowdSeq::FPlayer> P = MakeShared<SSXCrowdSeq::FPlayer>();
		P->NoteOn = [this, H](const SSXCrowdSeq::FNoteOn& N) { return StartNote(H, N); };
		P->NoteOff = [this](int32 Voice)
		{
			// 0x2DFDA0 / 0x2DC340 / 0x351388
			FNoteVoice N;
			if (Notes.RemoveAndCopyValue(Voice, N) && N.Comp.IsValid()) { N.Comp->Stop(); N.Comp->bAutoDestroy = true; }
		};
		P->StopVoice = P->NoteOff;
		P->IsVoicePlaying = [this](int32 Voice)
		{
			const FNoteVoice* N = Notes.Find(Voice);
			return N && N->Comp.IsValid() && N->Comp->IsPlaying();
		};
		P->SetVoiceLevel = [this](int32 Voice, int32 Level) { if (FNoteVoice* N = Notes.Find(Voice)) { N->Level = Level; } };
		P->Start(*S.Sequence, 0);
		V.Player = P;
		Log.Add(FString::Printf(TEXT("seq:%s"), *S.Sequence->Name));
	}
	else
	{
		UAudioComponent* C = Spawn(FString::Printf(TEXT("sfx/Crowd/%02d"), S.SoundIndex), S.Position, true, 1.f);
		if (!C)
		{
			return -1;
		}
		V.Comp = C;
		Log.Add(FString::Printf(TEXT("stream:%d"), S.SoundIndex));
	}
	Voices.Add(H, V);
	return H;
}

int32 FSSXCrowdVoices::StartNote(int32 Owner, const SSXCrowdSeq::FNoteOn& N)
{
	// 0x2D1718 / 0x2D5E18 / 0x2D59D8
	if (N.Program < 0 || N.Program >= UE_ARRAY_COUNT(Patches) || !Patches[N.Program].bValid)
	{
		return -1;
	}
	SSXCrowdSeq::FNote R;
	if (!SSXCrowdSeq::ResolveNote(Patches[N.Program], N.Key, N.Velocity, LibRng, R))
	{
		return -1;
	}
	const FVoice* V = Voices.Find(Owner);
	const float Pitch = FMath::Pow(2.f, R.Cents / 1200.f);
	UAudioComponent* C = Spawn(FString::Printf(TEXT("sfx/Crowd/%02d"), N.Program), V ? V->Position : FVector3f::ZeroVector, true, Pitch);
	if (!C)
	{
		return -1;
	}
	const int32 Id = NextNote++;
	Notes.Add(Id, { C, Owner, R.Volume127, N.Level });
	Log.Add(FString::Printf(TEXT("note:%d:%d:%d"), N.Program, N.Key, R.Cents));
	return Id;
}

void FSSXCrowdVoices::Step(const FVector& Ear, double DeltaMs)
{
	if (!bBegun)
	{
		return;
	}
	Crowd.Frame();
	TickAccMs += DeltaMs;
	while (TickAccMs >= SSXCrowdSeq::TickMs)
	{
		TickAccMs -= SSXCrowdSeq::TickMs;
		for (TPair<int32, FVoice>& V : Voices)
		{
			if (V.Value.Player.IsValid()) { V.Value.Player->Tick(); }
		}
	}
	const USSXAudioSubsystem* Audio = AudioRef.Get();
	const float Level = Audio ? Audio->GetSfxGain() : 1.f;
	for (TPair<int32, FVoice>& It : Voices)
	{
		FVoice& V = It.Value;
		const int32 Vol = SSXBoardAudio::Falloff(FMath::Clamp(V.Volume, 0, 127), FVector::Dist(FVector(V.Position), Ear) * 0.01,
			SSXAmbient::FalloffRadiusM);
		if (V.Player.IsValid())
		{
			if (V.Player->GetVolume() != Vol) { V.Player->SetVolume(Vol); }
		}
		else if (V.Comp.IsValid())
		{
			V.Comp->SetVolumeMultiplier(FMath::Clamp(Vol / 127.f * Level, 0.f, 1.f));
		}
	}
	for (auto It = Notes.CreateIterator(); It; ++It)
	{
		FNoteVoice& N = It.Value();
		if (!N.Comp.IsValid() || !N.Comp->IsPlaying())
		{
			continue;
		}
		// 0x2D5148
		N.Comp->SetVolumeMultiplier(FMath::Clamp(SSXCrowdSeq::VoiceLevel(N.Volume127, N.Level) / 127.f * Level, 0.f, 1.f));
	}
	for (auto It = Voices.CreateIterator(); It; ++It)
	{
		if (!It.Value().Player.IsValid() || It.Value().Player->IsPlaying())
		{
			continue;
		}
		bool bTracked = false;
		for (int32 L = 0; L < Crowd.NumCrowds(); ++L)
		{
			bTracked |= Crowd.GetCrowd(L)->Reaction == It.Key();
		}
		if (!bTracked)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = Notes.CreateIterator(); It; ++It)
	{
		if (!Voices.Contains(It.Value().Owner) && !(It.Value().Comp.IsValid() && It.Value().Comp->IsPlaying()))
		{
			It.RemoveCurrent();
		}
	}
}

FString FSSXCrowdVoices::PickChantBank(int32 Listener, TFunctionRef<uint32()> Rand) const
{
	return Listener == 0 ? Chants.Pick(ListenerCharacter, ListenerRank, Rand) : FString();
}
