#pragma once

#include "CoreMinimal.h"

class UAudioComponent;

namespace SSXAudioDiag
{
	enum class EResult : uint8
	{
		Played,
		VoiceCap,
		NotInManifest,
		AssetMissing,
		NoWorld,
		Volume0,
		Gated,
		OutOfRange,
		SpawnFailed,
		Num
	};

	SSXTRICKYREBUILD_API const TCHAR* ResultName(EResult R);

	struct FRecord
	{
		FString System;
		FString Id;
		bool b3D = false;
		bool bUI = false;
		float Volume = 0.f;
		double RadiusM = 0.0;
		EResult Result = EResult::Played;
	};

	SSXTRICKYREBUILD_API int32 LogLevel();
	SSXTRICKYREBUILD_API void Note(const TCHAR* System, const FString& Id, bool b3D, float Volume, double RadiusM, EResult Result,
		const FString& Asset = FString(), bool bUI = false);
	/** A song / stream state change, logged as "[audiolog] song <what> gain=.." with ssx.Audio.Log 1. */
	SSXTRICKYREBUILD_API void Song(const FString& What, float Gain);
	SSXTRICKYREBUILD_API EResult SpawnResult(const UAudioComponent* C, bool b3D);

	SSXTRICKYREBUILD_API int32 Count(EResult R);
	SSXTRICKYREBUILD_API int32 Count(const FString& System, EResult R);
	SSXTRICKYREBUILD_API const TArray<FRecord>& Records();
	SSXTRICKYREBUILD_API void Reset();
	SSXTRICKYREBUILD_API TArray<FString> Summary();
	SSXTRICKYREBUILD_API int32 PeakActiveSources();
	SSXTRICKYREBUILD_API int32 MaxSources();

	SSXTRICKYREBUILD_API void NoteLoop(const TCHAR* System, const FString& Id, UAudioComponent* C, bool b3D);
	SSXTRICKYREBUILD_API void NoteStop(const UAudioComponent* C, const TCHAR* Why);
	struct FLiveLoop
	{
		FString System;
		FString Id;
		bool b3D = false;
		float Volume = 0.f;
		double AgeS = 0.0;
	};
	SSXTRICKYREBUILD_API TArray<FLiveLoop> LiveLoops();
	SSXTRICKYREBUILD_API FString LoopsLine();
}
