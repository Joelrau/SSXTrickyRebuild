#include "Sim/Anim/RiderAnim.h"
#include "Sim/EEFloat.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/GameRng.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/RiderSim.h"

namespace SSXAnim
{
namespace
{
	FAnimData MakeAnim(int32 N, int32 M0, int32 M1, int32 M2, int32 M3, int32 Category)
	{
		FAnimData D;
		D.NFrames = N;
		D.Markers[0] = M0;
		D.Markers[1] = M1;
		D.Markers[2] = M2;
		D.Markers[3] = M3;
		D.Category = Category;
		return D;
	}

	const FAnimData DefaultGrab = MakeAnim(30, 10, 14, 20, 25, CategoryGrab);
	const FAnimData DefaultTweak = MakeAnim(25, 5, 9, 20, 18, CategoryTweak);
	const FAnimData DefaultUber = MakeAnim(61, 10, -1, 50, 50, CategoryUber);

	struct FDefaultOneShot { int32 Id; int32 N[3]; };
	const FDefaultOneShot DefaultOneShots[] = {
		{ GateToBase, { 21, 21, 14 } }, { SitUp, { 56, 56, 61 } },
		{ SwitchFakie, { 21, 15, 20 } }, { UnSwitch, { 0, 0, 20 } },
		{ TurnAroundHS, { 0, 0, 20 } }, { TurnAroundTS, { 0, 0, 20 } },
		{ SkatePush, { 36, 36, 30 } }, { AirIdle, { 16, 16, 17 } }, { AirAuto180, { 10, 10, 0 } },
		{ LandAtomic, { 31, 31, 31 } }, { LandAtomicBwd, { 35, 31, 0 } }, { LandFakie, { 0, 0, 25 } },
		{ 0x241, { 16, 19, 9 } }, { LandTail, { 26, 26, 25 } }, { LandTailBwd, { 26, 26, 0 } },
		{ 0x244, { 30, 16, 20 } }, { 0x245, { 28, 16, 20 } }, { 0x246, { 27, 17, 25 } },
		{ 0x247, { 25, 19, 25 } }, { 0x248, { 36, 24, 40 } }, { 0x249, { 36, 23, 40 } },
		{ 0x24A, { 51, 57, 56 } }, { 0x24B, { 35, 35, 30 } }, { 0x24C, { 35, 35, 33 } },
		{ 0x24D, { 41, 31, 33 } }, { 0x24E, { 30, 35, 33 } }, { 0x24F, { 35, 35, 33 } },
		{ 0x250, { 21, 21, 10 } }, { 0x251, { 21, 21, 10 } }, { 0x252, { 0, 0, 33 } }, { 0x253, { 0, 0, 33 } },
		{ 0x258, { 15, 16, 11 } }, { 0x259, { 15, 16, 11 } }, { 0x25A, { 15, 16, 11 } }, { 0x25B, { 15, 16, 11 } },
		{ 0x25C, { 15, 16, 12 } }, { 0x25D, { 15, 16, 11 } }, { 0x25E, { 15, 16, 11 } }, { 0x25F, { 15, 16, 11 } },
		{ 0x205, { 21, 23, 20 } }, { 0x206, { 23, 24, 20 } }, { 0x207, { 21, 21, 21 } }, { 0x208, { 23, 23, 23 } },
		// 0x106668
		{ 0x2EE, { 20, 15, 15 } }, { 0x2EF, { 50, 50, 50 } }, { 0x214, { 41, 53, 44 } }, { 0x215, { 41, 45, 43 } },
		{ 0x226, { 16, 15, 6 } }, { 0x227, { 15, 31, 6 } }, { 0x228, { 16, 16, 15 } }, { 0x229, { 16, 21, 14 } },
		{ 0x22A, { 16, 16, 10 } }, { 0x22B, { 16, 16, 14 } }, { 0x22C, { 16, 17, 12 } }, { 0x22D, { 16, 20, 12 } },
		{ 0x236, { 16, 30, 16 } }, { 0x237, { 16, 15, 16 } }, { 0x238, { 16, 16, 16 } }, { 0x239, { 15, 16, 16 } },
		{ 0x23A, { 16, 16, 16 } }, { 0x23B, { 16, 16, 16 } }, { 0x23C, { 16, 18, 17 } }, { 0x23D, { 17, 17, 17 } },
		{ 0x26E, { 10, 11, 11 } }, { 0x26F, { 10, 11, 11 } },
		{ 0x270, { 15, 15, 11 } }, { 0x271, { 15, 15, 11 } }, { 0x272, { 15, 15, 11 } }, { 0x273, { 15, 15, 11 } },
		{ 0x274, { 15, 15, 11 } }, { 0x275, { 15, 15, 11 } }, { 0x276, { 15, 15, 11 } }, { 0x277, { 15, 11, 11 } },
	};

	struct FChannelRegistry
	{
		TMap<const FRiderSim*, TWeakPtr<FRiderAnimChannel>> Map;
	};

	FChannelRegistry& Registry()
	{
		static FChannelRegistry R;
		return R;
	}

	FCriticalSection& TableLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}
}

double GrabSpeed(double Stat14)
{
	// 0x1275E0, 0x127774
	const float Byte = float(FMath::RoundToInt32(Stat14 * 255.0));
	return SSXEE::FpuAdd(Byte * 0.8057834f * 0.003921569f, 0.78212035f);
}

double ClockStep(double Frame, double Speed)
{
	return SSXEE::FpuAdd(float(Frame), (float(Speed) * 30.0f) / 60.0f);
}

int32 FramesInControl(const FRiderState& State)
{
	return FMath::RoundToInt32(State.ControlTime / FRiderSim::Dt);
}

void FAnimInstance::Start(const FAnimData& InData, double InSpeed, double InHold)
{
	*this = FAnimInstance();
	Data = InData;
	Speed = InSpeed;
	Hold = InHold;
}

void FAnimInstance::Advance()
{
	if (!IsActive())
	{
		return;
	}
	double New = ClockStep(Frame, Speed);
	Flags = 0;
	Edges = 0;
	for (int32 K = 0; K < 4; ++K)
	{
		if (Data.Markers[K] >= 0 && double(Data.Markers[K]) < New)
		{
			Flags |= uint8(1u << K);
			if (double(Data.Markers[K]) >= Frame)
			{
				Edges |= uint8(1u << K);
			}
		}
	}
	if (Hold >= 0.0 && New > Hold)
	{
		New = Hold;
	}
	const double Last = double(Data.NFrames - 1);
	bEnded = Last < New && Last <= Frame;   // 0x15A030
	Frame = FMath::Min(New, Last);
}

FString FAnimTable::DefaultPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("anim_table.bin")));
}

const FAnimTable& FAnimTable::Shared()
{
	static FAnimTable* Table = nullptr;
	FScopeLock Lock(&TableLock());
	if (!Table)
	{
		Table = new FAnimTable();
		const FString Path = DefaultPath();
		if (FPaths::FileExists(Path))
		{
			FString Err;
			if (!Table->Load(Path, &Err))
			{
				UE_LOG(LogTemp, Warning, TEXT("[SSX.Anim] %s; using the notes' typical animation timings"), *Err);
			}
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[SSX.Anim] %s not found (run tools/export_anim_table.py); using the notes' typical animation timings"), *Path);
		}
	}
	return *Table;
}

int32 DefaultOneShotFrames(int32 AnimId, int32 BoardClass)
{
	const int32 Bc = FMath::Clamp(BoardClass, 0, 2);
	for (const FDefaultOneShot& D : DefaultOneShots)
	{
		if (D.Id == AnimId)
		{
			return D.N[Bc];
		}
	}
	return 0;
}

void FAnimTable::SetDefaults()
{
	Rows.Reset();
	OneShots.Reset();
	for (const FDefaultOneShot& D : DefaultOneShots)
	{
		FOneShot& O = OneShots.AddDefaulted_GetRef();
		O.AnimId = D.Id;
		for (int32 Bc = 0; Bc < NumBoardClasses; ++Bc)
		{
			for (int32 Ch = 0; Ch < NumCharacters; ++Ch)
			{
				O.N[Bc][Ch][0] = uint16(D.N[Bc]);
			}
		}
	}
	bFromDisc = false;
}

bool FAnimTable::Load(const FString& Path, FString* OutError)
{
	TArray<uint8> Bytes;
	auto Fail = [OutError, &Path](const TCHAR* Why)
	{
		if (OutError) { *OutError = FString::Printf(TEXT("%s: %s"), *Path, Why); }
		return false;
	};
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return Fail(TEXT("cannot read"));
	}
	int64 Pos = 0;
	auto Read = [&Bytes, &Pos](void* Dst, int64 Size)
	{
		if (Pos + Size > Bytes.Num()) { return false; }
		FMemory::Memcpy(Dst, Bytes.GetData() + Pos, Size);
		Pos += Size;
		return true;
	};
	char Magic[4];
	uint32 Hdr[5];
	if (!Read(Magic, 4) || !Read(Hdr, sizeof(Hdr)) || FMemory::Memcmp(Magic, "SSXA", 4) != 0 || Hdr[0] != 1)
	{
		return Fail(TEXT("not an SSXA version 1 file"));
	}
	if (Hdr[1] != NumBoardClasses || Hdr[2] != NumCharacters || Hdr[3] != NumSlots)
	{
		return Fail(TEXT("unexpected table dimensions"));
	}
	TArray<FTrickRow> NewRows;
	NewRows.SetNum(NumBoardClasses * NumCharacters * NumSlots);
	for (FTrickRow& R : NewRows)
	{
		int32 GrabId = 0;
		if (!Read(&GrabId, 4)) { return Fail(TEXT("truncated trick rows")); }
		R.GrabId = GrabId;
		for (FAnimData* D : { &R.Grab, &R.Tweak, &R.Uber })
		{
			int32 Id;
			uint16 N, Cat, M[4];
			if (!Read(&Id, 4) || !Read(&N, 2) || !Read(&Cat, 2) || !Read(M, 8)) { return Fail(TEXT("truncated trick rows")); }
			*D = FAnimData();
			if (Id >= 0 && N > 0)
			{
				D->AnimId = Id;
				D->NFrames = N;
				D->Category = Cat;
				for (int32 K = 0; K < 4; ++K) { D->Markers[K] = M[K] == 0xFFFF ? -1 : int32(M[K]); }
			}
		}
	}
	TArray<FOneShot> NewShots;
	NewShots.SetNum(int32(Hdr[4]));
	for (FOneShot& O : NewShots)
	{
		if (!Read(&O.AnimId, 4) || !Read(O.N, sizeof(O.N))) { return Fail(TEXT("truncated one-shot entries")); }
	}
	Rows = MoveTemp(NewRows);
	OneShots = MoveTemp(NewShots);
	bFromDisc = true;
	return true;
}

FTrickRow FAnimTable::TrickRow(int32 BoardClass, int32 Character, int32 GrabId) const
{
	if (Rows.Num() > 0 && BoardClass >= 0 && BoardClass < NumBoardClasses && Character >= 0 && Character < NumCharacters)
	{
		const int32 Base = (BoardClass * NumCharacters + Character) * NumSlots;
		for (int32 S = 0; S < NumSlots; ++S)
		{
			if (Rows[Base + S].GrabId == GrabId && GrabId != NoTrickId)
			{
				return Rows[Base + S];
			}
		}
	}
	FTrickRow R;
	R.GrabId = GrabId;
	R.Grab = DefaultGrab;
	R.Tweak = DefaultTweak;
	R.Uber = DefaultUber;
	return R;
}

int32 FAnimTable::Frames(int32 AnimId, int32 BoardClass, int32 Character) const
{
	BoardClass = FMath::Clamp(BoardClass, 0, NumBoardClasses - 1);
	Character = FMath::Clamp(Character, 0, NumCharacters - 1);
	for (const FOneShot& O : OneShots)
	{
		if (O.AnimId == AnimId)
		{
			return O.N[BoardClass][Character][0];
		}
	}
	return bFromDisc ? DefaultOneShotFrames(AnimId, BoardClass) : 0;
}

int32 FRiderAnimChannel::Frames(const FRiderSim& Sim, int32 AnimId) const
{
	return GetTable().Frames(AnimId, Sim.State.BoardClass, Sim.Profile.GetTrickCharacter());
}

int32 FRiderAnimChannel::ControlFramesFor(const FRiderSim& Sim, int32 AnimId, int32 Fallback) const
{
	const int32 N = Frames(Sim, AnimId);
	return N > 0 ? ControlFrames(N) : Fallback;
}

int32 RandomVariantCount(int32 AnimId)
{
	if (const FAflLibrary* Lib = FAflLibrary::Shared())
	{
		const FAnimProps* P = Lib->Props(AnimId);
		return (P && P->Lookup == 3) ? Lib->VariantCount(AnimId) : 0;
	}
	// 0x3BF180, 0x325FAC
	switch (AnimId)
	{
	case 0x205: case 0x206: case 0x23E: return 3;
	case 0x207: case 0x208: return 1;
	case 0x209: case 0x20A: return 2;
	case 0x211: case 0x212: case 0x213: return 5;
	case 0x214: return 10;
	case 0x215: return 9;
	default: return 0;
	}
}

int32 FRiderAnimChannel::DrawVariant(int32 AnimId)
{
	const int32 N = RandomVariantCount(AnimId);
	if (N <= 0)
	{
		return 0;
	}
	const uint32 R = FGameRng::StreamB().Next();   // 0x15ADA8
	if (PreDrawn.Num() >= 8) { PreDrawn.RemoveAt(0); }
	PreDrawn.Emplace(AnimId, R);
	return int32(R % uint32(N));
}

bool FRiderAnimChannel::TakeDraw(int32 AnimId, uint32& OutR)
{
	for (int32 K = 0; K < PreDrawn.Num(); ++K)
	{
		if (PreDrawn[K].Key == AnimId)
		{
			OutR = PreDrawn[K].Value;
			PreDrawn.RemoveAt(K);
			return true;
		}
	}
	return false;
}

int32 FRiderAnimChannel::PlayOverlay(FRiderSim& Sim, int32 AnimId)
{
	const int32 Variant = RequestPlay(AnimId);
	FRiderState& S = Sim.State;
	int32 N = 0;
	if (const FAflLibrary* Lib = FAflLibrary::Shared())
	{
		if (const FAflClip* C = Lib->Clip(Lib->Resolve(AnimId, S.BoardClass, Sim.Profile.GetTrickCharacter(), Variant)))
		{
			N = C->NFrames;
		}
	}
	if (N <= 0)
	{
		N = Frames(Sim, AnimId);
	}
	S.Ch1Anim = AnimId;
	S.Ch1EndFrame = S.Frame + ControlFrames(N);
	return Variant;
}

void FRiderAnimChannel::FadeChannel1(FRiderSim& Sim)
{
	if (PlayRequestSpeeds.Num() != PlayRequests.Num()) { PlayRequestSpeeds.Init(-1.0, PlayRequests.Num()); }
	if (PlayRequests.Num() >= 8) { PlayRequests.RemoveAt(0); PlayRequestSpeeds.RemoveAt(0); }
	PlayRequests.Add(FadeChannel1Request);
	PlayRequestSpeeds.Add(-1.0);
	FRiderState& S = Sim.State;
	if (S.Frame < S.Ch1EndFrame)
	{
		S.Ch1EndFrame = FMath::Min(S.Ch1EndFrame, S.Frame + 20);
	}
}

void FRiderAnimChannel::LandBobble(FRiderSim& Sim)
{
	FRiderState& S = Sim.State;
	if (S.Frame < S.Ch1EndFrame)
	{
		return;   // 0x15EEC0
	}
	PlayOverlay(Sim, LandBobbleAnim);
	S.Ch1EndFrame = MAX_int32;
}

TSharedRef<FRiderAnimChannel> AcquireChannel(const FRiderSim& RegistrationSim)
{
	FChannelRegistry& R = Registry();
	for (auto It = R.Map.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	if (const TWeakPtr<FRiderAnimChannel>* Found = R.Map.Find(&RegistrationSim))
	{
		if (TSharedPtr<FRiderAnimChannel> Pinned = Found->Pin())
		{
			return Pinned.ToSharedRef();
		}
	}
	TSharedRef<FRiderAnimChannel> New = MakeShared<FRiderAnimChannel>();
	R.Map.Add(&RegistrationSim, New);
	return New;
}

FRiderAnimChannel* FindChannel(const FRiderSim& RegistrationSim)
{
	const TWeakPtr<FRiderAnimChannel>* Found = Registry().Map.Find(&RegistrationSim);
	FRiderAnimChannel* Ch = Found ? Found->Pin().Get() : nullptr;
	if (!Ch && RegistrationSim.GetRegistrationSim())
	{
		Found = Registry().Map.Find(RegistrationSim.GetRegistrationSim());
		Ch = Found ? Found->Pin().Get() : nullptr;
	}
	return Ch;
}
}
