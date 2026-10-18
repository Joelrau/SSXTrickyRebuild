#include "Audio/SSXAudioConfig.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/SSXFrontEndData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString FSSXInfFile::FSection::Get(const FString& Key, const FString& Default) const
{
	for (const TPair<FString, FString>& KV : Values)
	{
		if (KV.Key.Equals(Key, ESearchCase::IgnoreCase))
		{
			return KV.Value;
		}
	}
	return Default;
}

TArray<FString> FSSXInfFile::FSection::GetAll(const FString& Key) const
{
	TArray<FString> Out;
	for (const TPair<FString, FString>& KV : Values)
	{
		if (KV.Key.Equals(Key, ESearchCase::IgnoreCase))
		{
			Out.Add(KV.Value);
		}
	}
	return Out;
}

bool FSSXInfFile::Parse(const FString& Text)
{
	Sections.Reset();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (FString Line : Lines)
	{
		int32 Hash;
		if (Line.FindChar(TEXT('#'), Hash)) { Line.LeftInline(Hash); }
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty()) { continue; }
		if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
		{
			FSection& S = Sections.AddDefaulted_GetRef();
			S.Name = Line.Mid(1, Line.Len() - 2).TrimStartAndEnd();
			continue;
		}
		if (Sections.Num() == 0) { continue; }
		FString Key, Value;
		if (Line.Split(TEXT("="), &Key, &Value))
		{
			Value.TrimStartAndEndInline();
			Value.TrimQuotesInline();
			Sections.Last().Values.Emplace(Key.TrimStartAndEnd(), Value);
		}
		else
		{
			Sections.Last().Values.Emplace(Line, FString());
		}
	}
	return Sections.Num() > 0;
}

bool FSSXInfFile::Load(const FString& Path)
{
	FString Text;
	return FFileHelper::LoadFileToString(Text, *Path) && Parse(Text);
}

const FSSXInfFile::FSection* FSSXInfFile::Find(const FString& Name) const
{
	for (const FSection& S : Sections)
	{
		if (S.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return &S;
		}
	}
	return nullptr;
}

FString SSXAudio::ConvertedDir()
{
	return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("audio"));
}

FString SSXAudio::ManifestPath()
{
	return FPaths::Combine(ConvertedDir(), TEXT("manifest.json"));
}

FString SSXAudio::ConfigPath(const FString& InfName)
{
	const FString Converted = FPaths::Combine(ConvertedDir(), TEXT("config"), InfName);
	if (FPaths::FileExists(Converted))
	{
		return Converted;
	}
	return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("extracted"), TEXT("DATA"), TEXT("CONFIG"), InfName);
}

FString SSXAudio::SanitizeAssetName(const FString& Name)
{
	FString Out = Name;
	for (TCHAR& C : Out)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_'))
		{
			C = TEXT('_');
		}
	}
	return Out;
}

FString SSXAudio::AssetPathForFile(const FString& File)
{
	const FString Dir = FPaths::GetPath(File);
	const FString Name = SanitizeAssetName(FPaths::GetBaseFilename(File));
	return FString::Printf(TEXT("/Game/Audio/%s/%s.%s"), *Dir, *Name, *Name);
}

bool FSSXAudioManifest::Load(const FString& Path, FString* OutError)
{
	Entries.Reset();
	Pathfinder.Reset();
	SampleCountMismatches = 0;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s (run tools/convert_audio.py)"), *Path); }
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		if (OutError) { *OutError = TEXT("manifest is not valid JSON"); }
		return false;
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("entries")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FSSXAudioEntry E;
		E.Id = O->GetStringField(TEXT("id"));
		E.File = O->GetStringField(TEXT("file"));
		E.Category = O->GetStringField(TEXT("category"));
		E.Rate = int32(O->GetNumberField(TEXT("rate")));
		E.Channels = int32(O->GetNumberField(TEXT("channels")));
		E.Duration = O->GetNumberField(TEXT("duration"));
		E.bImport = O->GetBoolField(TEXT("import"));
		double L = 0.0;
		if (O->TryGetNumberField(TEXT("loop_start"), L)) { E.LoopStart = int32(L); }
		if (O->TryGetNumberField(TEXT("loop_end"), L)) { E.LoopEnd = int32(L); }
		double HeaderSamples = 0.0, Decoded = 0.0;
		if (O->TryGetNumberField(TEXT("header_samples"), HeaderSamples))
		{
			if (!O->TryGetNumberField(TEXT("decoded_samples"), Decoded))
			{
				Decoded = O->GetNumberField(TEXT("samples"));
			}
			SampleCountMismatches += int32(int64(HeaderSamples) != int64(Decoded));
		}
		Entries.Add(E.Id, MoveTemp(E));
	}
	const TSharedPtr<FJsonObject>* Songs = nullptr;
	if (Root->TryGetObjectField(TEXT("pathfinder"), Songs))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*Songs)->Values)
		{
			Pathfinder.Add(KV.Key, KV.Value->AsObject()->GetStringField(TEXT("json")));
		}
	}
	return Entries.Num() > 0;
}
