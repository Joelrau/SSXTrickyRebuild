#include "UI/SSXFeRiders.h"

#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SSXTrickyRebuild.h"
#include "Sim/Rider/RiderProfiles.h"

namespace SSXFeRiders
{
FSession& FSession::Global()
{
	static FSession S;
	return S;
}

int32 FRider::Update(FSession& Session, bool bClipDone)
{
	if (!bLoaded)
	{
		return -1;
	}
	if (bClipDone && Current == Picked)
	{
		Requested = PickedLoop;
	}
	int32 Start = -1;
	if (Requested != Current)
	{
		switch (Requested)
		{
		case Idle: Start = AnimInit; break;
		case Picked: Start = AnimTrans1 + Session.NextTrans(); break;
		case PickedLoop: Start = AnimCycle; break;
		default: break;
		}
		Current = Requested;
	}
	if (Start >= 0)
	{
		Playing = Start;
	}
	return Start;
}

void FLightSet::UpdateDirections()
{
	for (int32 I = 0; I < 3; ++I)
	{
		const double A = FMath::DegreesToRadians(double(Azimuth[I]));
		const double E = FMath::DegreesToRadians(double(Elevation[I]));
		// FUN_002670E8 / 0x250E98 / 0x250D60
		const FVector3d V(FMath::Cos(A) * FMath::Cos(E), FMath::Sin(E), FMath::Sin(A) * FMath::Cos(E));
		Direction[I] = FVector3f(V.GetSafeNormal());
	}
}

FLightSet FLightSet::Lerp(const FLightSet& U, const FLightSet& S, float T)
{
	auto L = [T](float A, float B) { return B * T + A * (1.f - T); };
	FLightSet O;
	for (int32 I = 0; I < 4; ++I) { O.Ambient[I] = L(U.Ambient[I], S.Ambient[I]); }
	for (int32 I = 0; I < 3; ++I)
	{
		O.Azimuth[I] = L(U.Azimuth[I], S.Azimuth[I]);
		O.Elevation[I] = L(U.Elevation[I], S.Elevation[I]);
		for (int32 K = 0; K < 4; ++K) { O.Colour[I][K] = L(U.Colour[I][K], S.Colour[I][K]); }
	}
	O.UpdateDirections();
	return O;
}

bool FLightBlend::Step()
{
	// FUN_002670E8
	if (bSelected)
	{
		if (Blend >= 1.f) { return false; }
		Blend += 1.f / 15.f;
	}
	else
	{
		if (Blend <= 0.f) { return false; }
		Blend -= 1.f / 15.f;
	}
	Blend = FMath::Clamp(Blend, 0.f, 1.f);
	return true;
}

FString FData::DefaultPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("fe_riders.json"));
}

namespace
{
	bool ReadFloats(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float* Out, int32 N)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!O->TryGetArrayField(Key, A) || A->Num() != N) { return false; }
		for (int32 I = 0; I < N; ++I) { Out[I] = float((*A)[I]->AsNumber()); }
		return true;
	}

	bool ReadLightSet(const TSharedPtr<FJsonObject>& O, FLightSet& Out)
	{
		if (!O.IsValid()) { return false; }
		float Col[12];
		if (!ReadFloats(O, TEXT("ambient"), Out.Ambient, 4) || !ReadFloats(O, TEXT("azimuth"), Out.Azimuth, 3)
			|| !ReadFloats(O, TEXT("elevation"), Out.Elevation, 3) || !ReadFloats(O, TEXT("colour"), Col, 12))
		{
			return false;
		}
		for (int32 I = 0; I < 12; ++I) { Out.Colour[I / 4][I % 4] = Col[I]; }
		Out.UpdateDirections();
		return true;
	}
}

bool FData::Load(const FString& Path, FString* OutError)
{
	*this = FData();
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		if (OutError) { *OutError = FString::Printf(TEXT("%s: missing or not JSON (tools/export_fe_riders.py)"), *Path); }
		return false;
	}
	const TSharedPtr<FJsonObject>* Ring = nullptr;
	const TSharedPtr<FJsonObject>* Lights = nullptr;
	if (!Root->TryGetObjectField(TEXT("ring"), Ring) || !Root->TryGetObjectField(TEXT("lights"), Lights)
		|| !ReadLightSet((*Lights)->GetObjectField(TEXT("unselected")), Unselected)
		|| !ReadLightSet((*Lights)->GetObjectField(TEXT("selected")), Selected))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("%s: ring / lights missing"), *Path); }
		return false;
	}
	RingRadius = (*Ring)->GetNumberField(TEXT("radius"));
	RingZ = (*Ring)->GetNumberField(TEXT("z"));
	bLoaded = RingRadius > 0.0;
	return bLoaded;
}

const FData* FData::Shared()
{
	static FCriticalSection Lock;
	static FData* Data = nullptr;
	static bool bTried = false;
	FScopeLock L(&Lock);
	if (!bTried)
	{
		bTried = true;
		FData* D = new FData();
		FString Err;
		if (D->Load(DefaultPath(), &Err))
		{
			Data = D;
		}
		else
		{
			UE_LOG(LogSSX, Warning, TEXT("[fe riders] %s"), *Err);
			delete D;
		}
	}
	return Data;
}

SSXAnim::FRowMat RiderRoot(int32 Slot, double Radius, double Z)
{
	using SSXAnim::FRowMat;
	const double Theta = FMath::DegreesToRadians(-30.0 * Slot - 90.0);
	const float C = float(FMath::Cos(Theta)), S = float(FMath::Sin(Theta));
	FRowMat M = FRowMat::Identity();
	M.SetRow(0, FVector3f(C, -S, 0.f), 0.f);
	M.SetRow(1, FVector3f(S, C, 0.f), 0.f);
	M.SetRow(2, FVector3f(0.f, 0.f, 1.f), 0.f);
	M.SetRow(3, FVector3f(float(Radius) * C, -float(Radius) * S, float(Z)), 1.f);
	return M;
}

void FRing::Init(const int32 CharacterBySlot[NumRiders], FSession* InSession)
{
	Session = InSession;
	SpeechVariant = GetSession().OnFeLoad();
	for (int32 K = 0; K < NumRiders; ++K)
	{
		FRider& R = Riders[K];
		R = FRider();
		R.Character = CharacterBySlot[K];
		R.Slot = K;
		R.bLoaded = true;
		Lights[K] = FLightBlend();
	}
	RequestAll(Idle);   // 0x2637A0
}

void FRing::RequestAll(int32 State)
{
	for (FRider& R : Riders)
	{
		R.Requested = State;
	}
}

void FRing::Request(int32 Slot, int32 State)
{
	if (Slot >= 0 && Slot < NumRiders)
	{
		Riders[Slot].Requested = State;
	}
}

void FRing::Select(int32 Slot)
{
	for (int32 K = 0; K < NumRiders; ++K)
	{
		Lights[K].bSelected = K == Slot;
	}
}

void FRing::TickLights()
{
	for (FLightBlend& L : Lights)
	{
		L.Step();
	}
}

bool FPoser::Init(const SSXAnim::FAflLibrary* InLib, const SSXAnim::FAflSkeleton* InBody, int32 Character)
{
	Lib = InLib;
	Body = InBody;
	Board = Lib ? Lib->BoardSkeleton() : nullptr;
	Parts = (Lib && Body) ? Lib->Parts(Body->Name) : nullptr;
	// FUN_00266208
	Controller.Init(Lib, 0, FMath::Clamp(Character, 0, 11));
	Controller.bMirror = false;
	Controller.Yaw = 0.0;
	Controller.Speed = 1.0;
	return IsReady();
}

bool FPoser::IsClipDone() const
{
	const SSXAnim::FPlayInstance* I = Controller.Newest(0);
	const SSXAnim::FAflClip* C = (I && Lib) ? Lib->Clip(I->Subs[0].Clip) : nullptr;
	return C && I->Subs[0].Frame >= C->NFrames - 1;
}

void FPoser::Step(FRider& Rider, FSession& Session, TArray<int32>* OutEvents)
{
	if (!IsReady())
	{
		return;
	}
	const int32 Start = Rider.Update(Session, IsClipDone());
	if (Start >= 0)
	{
		Controller.Play(Start, 0.0, 5);
	}
	const SSXAnim::FPlayInstance* Before = Controller.Newest(0);
	const int32 ClipBefore = Before ? Before->Subs[0].Clip : INDEX_NONE;
	const double FrameBefore = Before ? (Start >= 0 ? 0.0 : Before->Subs[0].Frame) : 0.0;
	Controller.Update(SSXAnim::FAnimDriveInputs());
	const SSXAnim::FPlayInstance* After = Controller.Newest(0);
	if (OutEvents && After && After->Subs[0].Clip == ClipBefore)
	{
		// 0x15A030 / 0x15F300
		const SSXAnim::FAflClip* C = Lib->Clip(ClipBefore);
		const double Now = After->Subs[0].Frame;
		const double Lo = FMath::Min(FrameBefore, Now), Hi = FMath::Max(FrameBefore, Now);
		for (const FIntPoint& E : C ? C->Events : TArray<FIntPoint>())
		{
			if (Lo <= double(E.X) && double(E.X) < Hi)
			{
				OutEvents->Add(E.Y);
			}
		}
	}
}

void FPoser::ComputePalette(const SSXAnim::FRowMat& Root, TArray<SSXAnim::FRowMat>& OutBody, SSXAnim::FRowMat& OutBoard) const
{
	OutBody.Reset();
	OutBoard = Root;
	if (!IsReady())
	{
		return;
	}
	// TODO: where the game multiplies it (FK 0x2057A0 or the render model) is not recovered.
	TArray<SSXAnim::FPoseLayer> Layers;
	TArray<SSXAnim::FBonePose> BodyPose, BoardPose;
	Controller.GatherLayers(0, Layers);
	SSXAnim::BlendLayers(*Lib, *Body, Layers, 1.0, 1.0, 0, BodyPose);
	Controller.GatherLayers(1, Layers);
	SSXAnim::BlendLayers(*Lib, *Board, Layers, 1.0, 1.0, 0, BoardPose);
	// 0x2083E8 / 0x2083D8 / 0x2057A0
	SSXAnim::BuildPaletteFK(*Body, Root, BodyPose, 1.0, OutBody);
	TArray<SSXAnim::FRowMat> BoardPal;
	SSXAnim::BuildPaletteFK(*Board, Root, BoardPose, 1.0, BoardPal);
	if (BoardPal.Num() > 0)
	{
		OutBoard = BoardPal[0];
	}
}

void FPoser::ComputeParts(TConstArrayView<SSXAnim::FRowMat> InBody, SSXAnim::FRiderPartsPose& Out) const
{
	Out = SSXAnim::FRiderPartsPose();
	if (!IsReady() || !Parts)
	{
		return;
	}
	// 0x208400
	SSXAnim::BuildPartsPose(*Lib, *Parts, Controller, InBody, 1.0, Out);
}

SSXAnim::FRowMat ScaleAboutRoot(const SSXAnim::FRowMat& M, const SSXAnim::FRowMat& Root, double Scale)
{
	SSXAnim::FRowMat Out = M;
	const FVector3f O = Root.Row(3);
	for (int32 R = 0; R < 3; ++R)
	{
		Out.SetRow(R, M.Row(R) * float(Scale), M.M[R][3]);
	}
	Out.SetRow(3, O + (M.Row(3) - O) * float(Scale), M.M[3][3]);
	return Out;
}

double SizeScale(int32 Character)
{
	const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
	return Table.IsLoaded() ? (double)Table.Rider(Character).Scale : 1.0;
}
}
