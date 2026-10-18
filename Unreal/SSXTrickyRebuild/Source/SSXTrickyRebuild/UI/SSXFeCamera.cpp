#include "UI/SSXFeCamera.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFeProfile.h"

void FSSXFeCamPose::Axes(FVector& OutForward, FVector& OutUp) const
{
	const FQuat Rz(FVector::UpVector, FMath::DegreesToRadians(RotDeg.Z));
	const FQuat Rx(FVector::ForwardVector, FMath::DegreesToRadians(RotDeg.X));
	const FQuat Ry(FVector::RightVector, FMath::DegreesToRadians(RotDeg.Y));
	const FQuat R = Rz * Rx * Ry;
	OutForward = R.RotateVector(FVector(0, 0, -1));
	OutUp = R.RotateVector(FVector(0, 1, 0));
}

void FSSXFeCamPose::ToUnreal(FVector& OutLoc, FRotator& OutRot) const
{
	FVector F, U;
	Axes(F, U);
	auto Mirror = [](const FVector& V) { return FVector(V.X, -V.Y, V.Z); };
	OutLoc = Mirror(Pos);
	OutRot = FRotationMatrix::MakeFromXZ(Mirror(F), Mirror(U)).Rotator();
}

FSSXFeCamPose FSSXFeCamera::Sample(float T) const
{
	auto Eval = [this, T](const TCHAR* Ch, float Rest) -> float
	{
		const auto* Segs = Tracks.Find(FName(Ch));
		if (!Segs || Segs->Num() == 0)
		{
			return Rest;
		}
		for (int32 I = 0; I < Segs->Num(); ++I)
		{
			const auto& S = (*Segs)[I];
			if (T <= S[5] || I == Segs->Num() - 1)
			{
				return ((S[0] * T + S[1]) * T + S[2]) * T + S[3];
			}
		}
		return Rest;
	};
	FSSXFeCamPose P;
	P.Pos = FVector(Eval(TEXT("posX"), RestPos.X), Eval(TEXT("posY"), RestPos.Y), Eval(TEXT("posZ"), RestPos.Z));
	P.RotDeg = FVector(Eval(TEXT("rotX"), FMath::RadiansToDegrees(RestRotRad.X)), Eval(TEXT("rotY"), FMath::RadiansToDegrees(RestRotRad.Y)),
		Eval(TEXT("rotZ"), FMath::RadiansToDegrees(RestRotRad.Z)));
	P.Focal = Eval(TEXT("focal"), RestFocal);
	return P;
}

bool FSSXFeCameraSet::Load(const FString& JsonPath)
{
	Cameras.Reset();
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *JsonPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogSSX, Warning, TEXT("[fe] no FE cameras at %s (run tools/export_fe_cameras.py)"), *JsonPath);
		return false;
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cameras")))
	{
		const TSharedPtr<FJsonObject>& O = V->AsObject();
		FSSXFeCamera& C = Cameras.AddDefaulted_GetRef();
		C.Name = O->GetStringField(TEXT("name"));
		C.Duration = (float)O->GetNumberField(TEXT("duration"));
		C.RestFocal = (float)O->GetNumberField(TEXT("focal"));
		const TArray<TSharedPtr<FJsonValue>>& Pos = O->GetArrayField(TEXT("pos"));
		const TArray<TSharedPtr<FJsonValue>>& Rot = O->GetArrayField(TEXT("rot"));
		if (Pos.Num() == 3) { C.RestPos = FVector(Pos[0]->AsNumber(), Pos[1]->AsNumber(), Pos[2]->AsNumber()); }
		if (Rot.Num() == 3) { C.RestRotRad = FVector(Rot[0]->AsNumber(), Rot[1]->AsNumber(), Rot[2]->AsNumber()); }
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Tr : O->GetObjectField(TEXT("tracks"))->Values)
		{
			auto& Segs = C.Tracks.Add(FName(*Tr.Key));
			for (const TSharedPtr<FJsonValue>& S : Tr.Value->AsArray())
			{
				const TArray<TSharedPtr<FJsonValue>>& A = S->AsArray();
				if (A.Num() == 6)
				{
					auto& Seg = Segs.AddDefaulted_GetRef();
					for (int32 I = 0; I < 6; ++I) { Seg.Add((float)A[I]->AsNumber()); }
				}
			}
		}
	}
	UE_LOG(LogSSX, Log, TEXT("[fe] %d FE cameras from %s"), Cameras.Num(), *JsonPath);
	return Cameras.Num() > 0;
}

const FSSXFeCamera* FSSXFeCameraSet::Find(const FString& Name) const
{
	return Cameras.FindByPredicate([&Name](const FSSXFeCamera& C) { return C.Name.Equals(Name, ESearchCase::IgnoreCase); });
}

void FSSXFeCameraDirector::Play(const FString& Camera, float StartSec, float EndSec)
{
	Current = Camera;
	Start = StartSec;
	End = EndSec;
	Time = StartSec;
	Speed = 1.f;
	Queued.Reset();
	History.Add(Camera);
}

void FSSXFeCameraDirector::Then(const FString& Camera, float StartSec, float EndSec)
{
	if (!IsPlaying())
	{
		Play(Camera, StartSec, EndSec);
		return;
	}
	Queued = Camera;
	QueuedStart = StartSec;
	QueuedEnd = EndSec;
}

void FSSXFeCameraDirector::PlayFull(const FString& Camera, bool bReverse)
{
	const FSSXFeCamera* C = CameraSet ? CameraSet->Find(Camera) : nullptr;
	const float Dur = C ? C->Duration : 1000.f;
	Play(Camera, bReverse ? Dur : 0.f, bReverse ? 0.f : Dur);
}

void FSSXFeCameraDirector::Tick(float Dt)
{
	if (End >= Start)
	{
		Time = FMath::Min(Time + Dt * Speed, End);
	}
	else
	{
		Time = FMath::Max(Time - Dt * Speed, End);
	}
	if (!Queued.IsEmpty() && !IsPlaying())
	{
		const FString Next = Queued;
		Play(Next, QueuedStart, QueuedEnd);
	}
}

bool FSSXFeCameraDirector::GetPose(const FSSXFeCameraSet& Set, FSSXFeCamPose& Out) const
{
	const FSSXFeCamera* C = Set.Find(Current);
	if (!C)
	{
		return false;
	}
	Out = C->Sample(FMath::Clamp(Time, 0.f, C->Duration));
	return true;
}

int32 SSXFeCams::VenueMedalModel(int32 TrackId)
{
	return (10 - VenueRingSlot(TrackId)) % 10;
}

FString SSXFeCams::RiderCamName(int32 Char)
{
	return SSXFeTables::RiderName(Char);
}

int32 SSXFeCams::VenueRingSlot(int32 TrackId)
{
	// FUN_0029BAA0 / FUN_00262D80 / 0x347130 / 0x347158
	const int32 I = SSXFeTables::VenueOrder().Find(TrackId);
	return I == INDEX_NONE ? 0 : I;
}

const TCHAR* SSXFeCams::VenueChooseName(int32 Index)
{
	static const TCHAR* Names[10] = { TEXT("Gari"), TEXT("Snowdream"), TEXT("Alysium"), TEXT("MesaBlanca"), TEXT("Mercury"),
		TEXT("Aloha"), TEXT("Pipedream"), TEXT("Untracked"), TEXT("Tokyo"), TEXT("Alaska") };
	return Names[FMath::Clamp(Index, 0, 9)];
}

const TCHAR* SSXFeCams::VenueEventName(int32 Index)
{
	static const TCHAR* Names[10] = { TEXT("Gari"), TEXT("Snow"), TEXT("Elys"), TEXT("Mesa"), TEXT("Merq"), TEXT("Mega"),
		TEXT("Aloha"), TEXT("Alas"), TEXT("Untr"), TEXT("Pipe") };   // 0x347180
	return Names[FMath::Clamp(Index, 0, 9)];
}
