#include "Audio/SSXPropSounds.h"

#include "Audio/SSXBoardSoundComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 Magic = 0x50585353;
	constexpr uint32 Version = 2;
	constexpr int32 HeaderBytes = 48, EntryBytes = 28;
	constexpr int32 HitListMax = 200;      // 0x22E2D0

	bool Fail(FString* E, const FString& Msg)
	{
		if (E) { *E = Msg; }
		return false;
	}
}

FString FSSXPropSounds::DefaultPath(const FString& TrackStem)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TrackStem.ToLower() + TEXT(".propsnd.bin"));
}

void FSSXPropSounds::Reset()
{
	NumRecords = NumAttached = NumInstances = 0;
	Entries.Reset();
	ByInstance.Reset();
	HitList.Reset();
	LastOwner = INDEX_NONE;
	GridMinX = GridMinY = GridCell = 0.f;
	GridNx = GridNy = 0;
	AmbientRecords.Reset();
}

bool FSSXPropSounds::Load(const FString& Path, FString* OutError)
{
	Reset();
	TArray<uint8> B;
	if (!FFileHelper::LoadFileToArray(B, *Path, FILEREAD_Silent))
	{
		return Fail(OutError, FString::Printf(TEXT("%s missing (run tools/export_prop_sounds.py)"), *Path));
	}
	auto U32 = [&B](int32 Off) { uint32 V; FMemory::Memcpy(&V, B.GetData() + Off, 4); return V; };
	auto F32 = [&B](int32 Off) { float V; FMemory::Memcpy(&V, B.GetData() + Off, 4); return V; };
	if (B.Num() < HeaderBytes || U32(0) != Magic || U32(4) != Version)
	{
		return Fail(OutError, FString::Printf(TEXT("%s: bad header (re-run tools/export_prop_sounds.py)"), *Path));
	}
	NumRecords = int32(U32(8));
	NumAttached = int32(U32(12));
	const int32 N = int32(U32(16));
	NumInstances = int32(U32(20));
	GridMinX = F32(24);
	GridMinY = F32(28);
	GridCell = F32(32);
	GridNx = int32(U32(36));
	GridNy = int32(U32(40));
	const int32 NumAmbient = int32(U32(44));
	if (B.Num() < HeaderBytes + N * EntryBytes)
	{
		return Fail(OutError, FString::Printf(TEXT("%s: size %d < %d entries"), *Path, B.Num(), N));
	}
	Entries.SetNum(N);
	for (int32 K = 0; K < N; ++K)
	{
		const int32 O = HeaderBytes + K * EntryBytes;
		FSSXPropSoundEntry& E = Entries[K];
		E.Instance = int32(U32(O));
		E.HitId = U32(O + 4);
		E.NumShapes = int32(U32(O + 8));
		for (int32 S = 0; S < 4; ++S) { E.ShapeIds[S] = int32(U32(O + 12 + 4 * S)); }
		ByInstance.Add(E.Instance, K);
	}
	int32 O = HeaderBytes + N * EntryBytes;
	AmbientRecords.SetNum(NumAmbient);
	for (FSSXAmbientRecord& R : AmbientRecords)
	{
		if (O + 24 > B.Num())
		{
			Reset();
			return Fail(OutError, FString::Printf(TEXT("%s: emitter records truncated"), *Path));
		}
		R.Instance = int32(U32(O));
		R.Position = FVector3f(F32(O + 4), F32(O + 8), F32(O + 12));
		const int32 NumShapes = int32(U32(O + 16)), NumWords = int32(U32(O + 20));
		O += 24;
		const int32 End = O + 4 * NumWords;
		for (int32 S = 0; S < NumShapes && O < End; ++S)
		{
			FSSXAmbientShape& Sh = R.Shapes.AddDefaulted_GetRef();
			const int32 W = FSSXAmbientShape::NumWords(int32(U32(O)));
			if (W == 0 || O + 4 * W > End)
			{
				Reset();
				return Fail(OutError, FString::Printf(TEXT("%s: bad emitter shape"), *Path));
			}
			for (int32 K = 0; K < W; ++K) { Sh.W[K] = U32(O + 4 * K); }
			O += 4 * W;
		}
		O = End;
	}
	if (O != B.Num())
	{
		Reset();
		return Fail(OutError, FString::Printf(TEXT("%s: size %d != %d"), *Path, B.Num(), O));
	}
	return true;
}

const FSSXPropSoundEntry* FSSXPropSounds::Find(int32 Instance) const
{
	const int32* K = ByInstance.Find(Instance);
	return K ? &Entries[*K] : nullptr;
}

bool FSSXPropSounds::IsSwapId(uint32 Id)
{
	// 0x22E388 / 0x22E3F0
	return (Id >= 0x4F && Id <= 0x60) || (Id >= 0x67 && Id <= 0x93) || (Id >= 0x9F && Id <= 0xB2) || (Id >= 0xB7 && Id <= 0xB9)
		|| Id == 0x66;
}

bool FSSXPropSounds::Resolve(uint32 Id, int32& OutBank, int32& OutIndex)
{
	// 0x22D8A8
	if (IsSwapId(Id))
	{
		OutBank = 6;
		OutIndex = 0;
		return true;
	}
	OutBank = 2;
	switch (Id)
	{
	case 0x61: OutBank = 3; OutIndex = 0; return true;
	case 0x62: OutBank = 3; OutIndex = 1; return true;
	case 0x63: OutBank = 3; OutIndex = 2; return true;
	case 0x3A: OutIndex = 0; return true;
	default: break;
	}
	static const uint8 Table[0x4F] = {
		0xFF, 0xFF, 0x22, 0x02, 0x30, 0x01, 0x20, 0x32, 0x29, 0x30, 0x31, 0x21, 0x33, 0x35, 0x36, 0x33,
		0x23, 0x25, 0x13, 0x28, 0x35, 0x35, 0x34, 0x16, 0x18, 0x12, 0x53, 0x46, 0x18, 0x51, 0x03, 0x10,
		0x52, 0x32, 0x19, 0x3A, 0x4F, 0x32, 0x61, 0x11, 0x17, 0x60, 0x61, 0x33, 0x15, 0x14, 0x16, 0x2D,
		0x30, 0x31, 0x2A, 0x38, 0x33, 0x36, 0x60, 0x13, 0x47, 0x24, 0xFF, 0x2B, 0x21, 0x23, 0x21, 0x40,
		0x17, 0x37, 0x41, 0x70, 0x26, 0x60, 0x01, 0x22, 0x31, 0x31, 0x34, 0x34, 0x39, 0x36, 0x35 };
	if (Id < 0x4F && Table[Id] != 0xFF)
	{
		OutIndex = Table[Id];
		return true;
	}
	switch (Id)
	{
	case 0x94: OutIndex = 6; return true;
	case 0x95: OutIndex = 7; return true;
	case 0x96: OutIndex = 8; return true;
	case 0x97: OutIndex = 9; return true;
	case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: OutIndex = 0x73 + int32(Id - 0x98); return true;
	case 0xB3: case 0xB4: case 0xB5: case 0xB6: OutIndex = 10 + int32(Id - 0xB3); return true;
	default: return false;
	}
}

FString FSSXPropSounds::ManifestId(uint32 Id, const FString& TrackBank)
{
	int32 Bank = 0, Index = 0;
	if (!Resolve(Id, Bank, Index))
	{
		return FString();   // the original plays bank 2 with an uninitialised sound index (not reproduced)
	}
	if (Bank == 2)
	{
		return TrackBank.IsEmpty() ? FString() : FString::Printf(TEXT("sfx/%s/%02d"), *TrackBank, Index);
	}
	if (Bank == 3)
	{
		return FString::Printf(TEXT("sfx/Crowd/%02d"), Index);
	}
	return FString();
}

static const FSSXPropSounds* GBoundTable = nullptr;

void FSSXPropSounds::BindToBoardSounds(const FString& TrackBank)
{
	GBoundTable = this;
	USSXBoardSoundComponent::PropSoundRing.Reset();
	USSXBoardSoundComponent::PropSoundLookup = [this, TrackBank](int32 Instance, FString& OutId)
	{
		const FSSXPropSoundEntry* E = Find(Instance);
		OutId = E ? ManifestId(E->HitId, TrackBank) : FString();
		return E != nullptr;
	};
	USSXBoardSoundComponent::PropSoundOwner = [this](int32 Instance) { NotePropSoundOwner(Instance); };
}

void FSSXPropSounds::UnbindBoardSounds(const FSSXPropSounds* Only)
{
	if (Only && Only != GBoundTable)
	{
		return;
	}
	GBoundTable = nullptr;
	USSXBoardSoundComponent::PropSoundLookup = nullptr;
	USSXBoardSoundComponent::PropSoundOwner = nullptr;
	USSXBoardSoundComponent::PropSoundRing.Reset();
}

void FSSXPropSounds::NotePropSoundOwner(int32 Instance)
{
	if (Instance == INDEX_NONE || Instance == LastOwner)
	{
		LastOwner = Instance == INDEX_NONE ? LastOwner : Instance;
		return;
	}
	if (const FSSXPropSoundEntry* E = Find(Instance))
	{
		bool bActivated = false;
		for (const int32 S : E->ShapeIds) { bActivated |= S == 0x1C || S == 0x10 || S == 0x39; }   // 0x22E240
		if (bActivated && !HitList.Contains(Instance) && HitList.Num() < HitListMax)
		{
			HitList.Add(Instance);
		}
	}
	LastOwner = Instance;
}
