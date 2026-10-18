#pragma once

#include "CoreMinimal.h"

struct SSXTRICKYREBUILD_API FSSXInfFile
{
	struct FSection
	{
		FString Name;
		TArray<TPair<FString, FString>> Values;
		SSXTRICKYREBUILD_API FString Get(const FString& Key, const FString& Default = FString()) const;
		SSXTRICKYREBUILD_API TArray<FString> GetAll(const FString& Key) const;
	};
	TArray<FSection> Sections;

	bool Parse(const FString& Text);
	bool Load(const FString& Path);
	const FSection* Find(const FString& Name) const;
};

struct FSSXAudioEntry
{
	FString Id;
	FString File;
	FString Category;
	int32 Rate = 0;
	int32 Channels = 0;
	double Duration = 0.0;
	int32 LoopStart = -1;
	int32 LoopEnd = -1;
	bool bImport = false;
};

namespace SSXAudio
{
	SSXTRICKYREBUILD_API FString ConvertedDir();
	SSXTRICKYREBUILD_API FString ManifestPath();
	SSXTRICKYREBUILD_API FString ConfigPath(const FString& InfName);
	SSXTRICKYREBUILD_API FString AssetPathForFile(const FString& File);
	SSXTRICKYREBUILD_API FString SanitizeAssetName(const FString& Name);
}

class SSXTRICKYREBUILD_API FSSXAudioManifest
{
public:
	bool Load(const FString& Path, FString* OutError = nullptr);
	bool IsLoaded() const { return Entries.Num() > 0; }
	const FSSXAudioEntry* Find(const FString& Id) const { return Entries.Find(Id); }
	int32 Num() const { return Entries.Num(); }
	const TMap<FString, FSSXAudioEntry>& GetEntries() const { return Entries; }
	const TMap<FString, FString>& GetPathfinderSongs() const { return Pathfinder; }
	int32 SampleCountMismatches = 0;

private:
	TMap<FString, FSSXAudioEntry> Entries;
	TMap<FString, FString> Pathfinder;
};
