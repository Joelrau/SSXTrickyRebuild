#include "UI/SSXFrontEndData.h"

#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SSXTrickyRebuild.h"

const TCHAR* SSXFrontEnd::FrontEndMap = TEXT("/Game/Maps/FrontEnd");

const TArray<FSSXTrackInfo>& SSXFrontEnd::Tracks()
{
	static const TArray<FSSXTrackInfo> List = {
		{ TEXT("gari"), TEXT("Gari") }, { TEXT("snow"), TEXT("Snow") }, { TEXT("elysium"), TEXT("Elysium") },
		{ TEXT("mesa"), TEXT("Mesa") }, { TEXT("merquer"), TEXT("Merquer") }, { TEXT("megaple"), TEXT("Megaple") },
		{ TEXT("aloha"), TEXT("Aloha") }, { TEXT("alaska"), TEXT("Alaska") }, { TEXT("pipe"), TEXT("Pipe") },
		{ TEXT("untrack"), TEXT("Untrack") }, { TEXT("trick"), TEXT("Trick") },
	};
	return List;
}

FString SSXFrontEnd::RepoRoot()
{
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("SSXDataRoot="), Override))
	{
		return FPaths::ConvertRelativePathToFull(Override);
	}
	// Packaged game: the data is staged next to the content as loose files, <Package>/SSXTrickyRebuild/GameData/ (converted/, extracted/, unpacked/ mirrored). Editor: the repo root, two up.
	FString Root = FPlatformProperties::RequiresCookedData()
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("GameData")))
		: FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("..")));
	FPaths::CollapseRelativeDirectories(Root);
	return Root;
}

FString SSXFrontEnd::LangDir() { return FPaths::Combine(RepoRoot(), TEXT("extracted"), TEXT("DATA"), TEXT("LANG")); }
FString SSXFrontEnd::SfnDir() { return FPaths::Combine(RepoRoot(), TEXT("extracted"), TEXT("DATA"), TEXT("FONTS")); }
FString SSXFrontEnd::TextureDir() { return FPaths::Combine(RepoRoot(), TEXT("converted"), TEXT("textures"), TEXT("TEXTURES")); }
FString SSXFrontEnd::FrontEndDir() { return FPaths::Combine(RepoRoot(), TEXT("converted"), TEXT("frontend")); }
FString SSXFrontEnd::FontDir() { return FPaths::Combine(FrontEndDir(), TEXT("fonts")); }
FString SSXFrontEnd::VideoDir() { return FPaths::Combine(FrontEndDir(), TEXT("video")); }

double SSXFrontEnd::WavSeconds(const FString& WavPath)
{
	TArray<uint8> D;
	if (!FFileHelper::LoadFileToArray(D, *WavPath) || D.Num() < 44 || FMemory::Memcmp(D.GetData(), "RIFF", 4) != 0)
	{
		return 0.0;
	}
	auto U32 = [&D](int32 P) { return (uint32)D[P] | ((uint32)D[P + 1] << 8) | ((uint32)D[P + 2] << 16) | ((uint32)D[P + 3] << 24); };
	uint32 ByteRate = 0;
	for (int32 P = 12; P + 8 <= D.Num();)
	{
		const uint32 Len = U32(P + 4);
		if (FMemory::Memcmp(D.GetData() + P, "fmt ", 4) == 0 && P + 20 <= D.Num())
		{
			ByteRate = U32(P + 16);
		}
		else if (FMemory::Memcmp(D.GetData() + P, "data", 4) == 0)
		{
			const int64 Bytes = FMath::Min<int64>(Len, D.Num() - (P + 8));
			return ByteRate ? double(Bytes) / double(ByteRate) : 0.0;
		}
		P += 8 + int32(Len) + int32(Len & 1);
	}
	return 0.0;
}

bool SSXFrontEnd::MovieDone(bool bSkipped, bool bVideoEnded, double AudioSeconds, double AudioElapsed)
{
	return bSkipped || (bVideoEnded && AudioElapsed >= AudioSeconds);
}

FBox2f SSXFrontEnd::MovieRect(FIntPoint Picture)
{
	constexpr int32 FrameW = 512, FrameH = 448;
	const int32 W = FMath::Min(FMath::Max(Picture.X, 0) >> 4, FrameW >> 4) << 4;
	const int32 H = FMath::Clamp(Picture.Y, 0, FrameH);
	const int32 X = FMath::Max(0, (FrameW - FMath::Max(Picture.X, 0)) / 2);
	const float Sx = 640.f / FrameW, Sy = 480.f / FrameH;
	return FBox2f(FVector2f(X * Sx, 0.f), FVector2f((X + W) * Sx, H * Sy));
}

FString SSXFrontEnd::MapPackage(const FString& MapName) { return FString::Printf(TEXT("/Game/Maps/%s"), *MapName); }
bool SSXFrontEnd::MapExists(const FString& MapName) { return FPackageName::DoesPackageExist(MapPackage(MapName)); }

bool FSSXLocStrings::ReadLocFile(const FString& Path, TArray<FString>& Out)
{
	Out.Reset();
	TArray<uint8> D;
	if (!FFileHelper::LoadFileToArray(D, *Path) || D.Num() < 0x24)
	{
		return false;
	}
	auto U32 = [&D](int32 O) -> uint32
	{
		return O + 4 <= D.Num() ? (uint32)D[O] | ((uint32)D[O + 1] << 8) | ((uint32)D[O + 2] << 16) | ((uint32)D[O + 3] << 24) : 0u;
	};
	if (FMemory::Memcmp(D.GetData(), "LOCH", 4) != 0)
	{
		return false;
	}
	const int32 Base = (int32)U32(16);
	if (Base + 16 > D.Num() || FMemory::Memcmp(D.GetData() + Base, "LOCL", 4) != 0)
	{
		return false;
	}
	const int32 Count = (int32)U32(Base + 12);
	if (Count <= 0 || Base + 16 + Count * 4 > D.Num())
	{
		return false;
	}
	Out.Reserve(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		int32 P = Base + (int32)U32(Base + 16 + I * 4);
		FString S;
		while (P + 1 < D.Num())
		{
			const TCHAR C = (TCHAR)(D[P] | (D[P + 1] << 8));
			if (C == 0)
			{
				break;
			}
			S.AppendChar(C);
			P += 2;
		}
		Out.Add(MoveTemp(S));
	}
	return true;
}

bool FSSXLocStrings::Load(const FString& Dir, const FString& Language)
{
	TArray<FString> K, T;
	if (!ReadLocFile(FPaths::Combine(Dir, TEXT("CONSTANT.LOC")), K) ||
		!ReadLocFile(FPaths::Combine(Dir, Language + TEXT(".LOC")), T) || K.Num() != T.Num())
	{
		UE_LOG(LogSSX, Warning, TEXT("[fe] no LOC strings in %s: menus draw no text until the disc is extracted"), *Dir);
		return false;
	}
	Texts.Reset(T.Num());
	KeyNames.Reset(K.Num());
	Keys.Reset();
	for (int32 I = 0; I < K.Num(); ++I)
	{
		Texts.Add(T[I].Replace(TEXT("\\"), TEXT("\n")));
		KeyNames.Add(K[I].TrimStartAndEnd());
		Keys.Add(KeyNames.Last(), I);
	}
	UE_LOG(LogSSX, Log, TEXT("[fe] %d LOC strings from %s"), Texts.Num(), *Dir);
	return true;
}
