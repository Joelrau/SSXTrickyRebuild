#include "UI/SSXFrontEndSubsystem.h"

#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXGameAudio.h"
#include "Progression/SSXSettingsEffects.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "UI/SSXFeOptions.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXFeDvd.h"
#include "UI/SSXLoadingScreen.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SSXTrickyRebuild.h"

USSXFrontEndSubsystem* USSXFrontEndSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<USSXFrontEndSubsystem>() : nullptr;
}

void USSXFrontEndSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	bMenuShots = FParse::Param(FCommandLine::Get(), TEXT("SSXMenuShots"));
#endif
	Strings();
	if (!SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json"))))
	{
		SSXFeTables::LoadBoardsCsv(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_boards.csv")));
	}
	SSXProg::LoadRecordDefaults(SSXProg::RecordDefaultsPath());
	Profile = FSSXProfile::MakeDefault();
}

ESSXLoadResult USSXFrontEndSubsystem::RequestLaunch(const FSSXFeLaunch& InLaunch)
{
	Launch = InLaunch;
	TrackIndex = SSXFeTables::TrackCatalogIndex(Launch.Track);
	const FSSXPlayerSlot& P1 = Launch.Slots[0];
	Character = FMath::Clamp(P1.Char, 0, SSXFeTables::NumRiders - 1);
	BoardIndex = P1.Board;
	Outfit = P1.Outfit;
	const int32 Type = SSXFeTables::Board(Character, BoardIndex).Type;
	if (Type >= 0)
	{
		BoardClass = Type;
	}
	if (!SSXFrontEnd::Tracks().IsValidIndex(TrackIndex))
	{
		return ESSXLoadResult::NoTrack;
	}
	const FSSXTrackInfo& Track = SSXFrontEnd::Tracks()[TrackIndex];
	const bool bExists = MapExistsHook ? MapExistsHook(Track.MapName) : SSXFrontEnd::MapExists(Track.MapName);
	if (!bExists)
	{
		UE_LOG(LogSSX, Warning, TEXT("[fe] %s is not built (Scripts/build_track_map.py %s converted/tracks/%s/%s.gltf)"),
			*Track.MapName, *Track.MapName, *Track.Id, *Track.Id);
		return ESSXLoadResult::MapMissing;
	}
	// 0x16D230 / 0x166A58 / 0x163E70
	Profile.Session.Heat = Launch.GameMode == SSXProg::WCShowoff ? 2 : 0;
	SSXProg::BuildPointSession(Profile, P1.Char, Cheats.bMaxAttributes, Profile.Session.Points[0]);
	LastRequestedMap = SSXFrontEnd::MapPackage(Track.MapName);
	++LoadRequests;
	bHasSelection = true;
	if (!bSuppressTravel)
	{
		if (USSXLoadingScreen* Loading = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXLoadingScreen>() : nullptr)
		{
			Loading->Begin(Launch, LastRequestedMap);
		}
		else if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		{
			UGameplayStatics::OpenLevel(World, FName(*LastRequestedMap));
		}
	}
	return ESSXLoadResult::Loading;
}

void USSXFrontEndSubsystem::ReturnToFrontEnd(int32 ExitCode)
{
	LastExitCode = ExitCode;
	bColdBootDone = true;
	// 0x180E00
	LastReturn = SSXTrackLoad::PickAfterRace(ExitCode == 0x30, false, Profile.Session.bQuit, Launch.GameMode, Profile.Session.bRankUp,
		Profile.Session.bNextVenue, FMath::Max(1, Launch.Players));
	UE_LOG(LogSSX, Log, TEXT("[fe] leave the race: exit code 0x%X, mode %d, quit %d -> loader kind %d%s"), ExitCode, Launch.GameMode,
		Profile.Session.bQuit ? 1 : 0, int32(LastReturn.Kind), LastReturn.bRankVideo ? TEXT(" after the rank video") : TEXT(""));
	if (!bSuppressTravel)
	{
		// 0x263B30
		if (USSXLoadingScreen* Loading = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXLoadingScreen>() : nullptr)
		{
			Loading->BeginReturn(LastReturn, Launch, SSXFrontEnd::FrontEndMap);
		}
		else if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		{
			UGameplayStatics::OpenLevel(World, FName(SSXFrontEnd::FrontEndMap));
		}
	}
}

void USSXFrontEndSubsystem::ApplySettings(UWorld* World) const
{
	// 0x17B980
	if (USSXAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<USSXAudioSubsystem>() : nullptr)
	{
		SSXGameAudio::ApplySettings(*Audio, Profile.Settings);
	}
	SSXSettings::ApplyBrightness(World, SSXProg::BrightnessScale(Profile.Settings));
}

const FSSXLocStrings& USSXFrontEndSubsystem::Strings()
{
	if (!LocStrings.IsSet())
	{
		LocStrings.Emplace();
		LocStrings->Load(SSXFrontEnd::LangDir());
	}
	return LocStrings.GetValue();
}

UTexture2D* USSXFrontEndSubsystem::LoadPng(const FString& Path)
{
	if (Path.IsEmpty() || MissingTextures.Contains(Path))
	{
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Found = Textures.Find(Path))
	{
		return *Found;
	}
	UTexture2D* Tex = FPaths::FileExists(Path) ? FImageUtils::ImportFileAsTexture2D(Path) : nullptr;
	if (!Tex)
	{
		MissingTextures.Add(Path);
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->UpdateResource();
	Textures.Add(Path, Tex);
	return Tex;
}

const FSSXFeAssets& USSXFrontEndSubsystem::Assets()
{
	if (AssetCache.IsSet())
	{
		return AssetCache.GetValue();
	}
	FSSXFeAssets A;
	auto Font = [this](const TCHAR* Name, float Sx, float Sy) -> TSharedPtr<FSSXBitmapFont>
	{
		const FString Base = FPaths::Combine(SSXFrontEnd::FontDir(), Name);
		TSharedPtr<FSSXBitmapFont> F = FSSXBitmapFont::Load(Base + TEXT(".json"), LoadPng(Base + TEXT(".png")),
			FPaths::Combine(SSXFrontEnd::SfnDir(), FString(Name) + TEXT(".SFN")));
		if (F.IsValid())
		{
			F->ScaleX = Sx;
			F->ScaleY = Sy;
		}
		return F;
	};
	A.Title = Font(TEXT("TITLE"), SSXFe::TitleScaleX, SSXFe::TitleScaleY);
	A.Menu = Font(TEXT("MENU"), SSXFe::MenuScaleX, SSXFe::MenuScaleY);
	// TODO: the FE scale of SMLFONT is not written by cFE3D::vfunc_02;
	A.Small = Font(TEXT("SMLFONT"), 1.f, 1.f);

	const FString SpriteJson = FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_sprites.json"));
	if (FeSprites.LoadJson(SpriteJson))
	{
		A.FeSprites = &FeSprites;
	}
	else
	{
		UE_LOG(LogSSX, Warning, TEXT("[fe] no FE sprite table at %s (run tools/export_fe_sprites.py)"), *SpriteJson);
	}
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (FFileHelper::LoadFileToString(Text, *SpriteJson) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Root->GetObjectField(TEXT("pages"))->Values)
		{
			if (UTexture2D* T = LoadPng(FPaths::Combine(SSXFrontEnd::TextureDir(), P.Value->AsString())))
			{
				A.Pages.Add(FName(*P.Key), T);
			}
		}
	}
	// FUN_001F9660
	for (int32 Char = 0; Char < 12; ++Char)
	{
		const FString Dir = FPaths::Combine(SSXFrontEnd::TextureDir(), SSXFeRiderProfile::PortraitFolder(Char));
		for (int32 N = 1; N <= 4; ++N)
		{
			TArray<FString> Found;
			IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, FString::Printf(TEXT("*_rpb%d.png"), N)), true, false);
			if (Found.Num() > 0)
			{
				if (UTexture2D* T = LoadPng(FPaths::Combine(Dir, Found[0])))
				{
					A.Pages.Add(SSXFeRiderProfile::PortraitPage(Char, 0x7C + N - 1), T);
				}
			}
		}
	}
	// FUN_0027A288
	for (int32 Char = 0; Char < 12; ++Char)
	{
		auto Bind = [this, &A](const FString& Folder, const FString& Pattern, FName Key)
		{
			TArray<FString> Found;
			const FString Dir = FPaths::Combine(SSXFrontEnd::TextureDir(), Folder);
			IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, Pattern), true, false);
			if (Found.Num() > 0)
			{
				if (UTexture2D* T = LoadPng(FPaths::Combine(Dir, Found[0])))
				{
					A.Pages.Add(Key, T);
				}
			}
		};
		Bind(SSXFeTrickBook::CoverFolder(Char), TEXT("*_tkb1.png"), SSXFeTrickBook::CoverPage(Char));
		for (int32 Ch = 0; Ch < SSXFeTrickBook::Chapters; ++Ch)
		{
			for (int32 T = 0; T < SSXFeTrickBook::TricksPerChapter; ++T)
			{
				Bind(SSXFeTrickBook::TrickFolder(Char, Ch), SSXFeTrickBook::TrickImagePattern(T), SSXFeTrickBook::TrickPage(Char, Ch, T));
			}
		}
	}
	for (int32 Spr = 0x68; Spr <= 0x7A; ++Spr)
	{
		TArray<FString> Found;
		const FString Dir = FPaths::Combine(SSXFrontEnd::TextureDir(), SSXFeDvdTables::FolderOf(Spr));
		IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, FString::Printf(TEXT("*_%s.png"), *SSXFeDvdTables::ImageOf(Spr))), true, false);
		if (Found.Num() > 0)
		{
			if (UTexture2D* T = LoadPng(FPaths::Combine(Dir, Found[0])))
			{
				A.Pages.Add(SSXFeDvdTables::PageOf(Spr), T);
			}
		}
	}
	if (!WhiteTexture)
	{
		WhiteTexture = UTexture2D::CreateTransient(2, 2, PF_B8G8R8A8);
		if (WhiteTexture)
		{
			FTexture2DMipMap& Mip = WhiteTexture->GetPlatformData()->Mips[0];
			void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memset(Data, 0xFF, 2 * 2 * 4);
			Mip.BulkData.Unlock();
			WhiteTexture->SRGB = true;
			WhiteTexture->UpdateResource();
		}
	}
	A.White = WhiteTexture;
	AssetCache.Emplace(MoveTemp(A));
	return AssetCache.GetValue();
}

const FSSXFeCameraSet& USSXFrontEndSubsystem::Cameras()
{
	if (!CameraSet.IsSet())
	{
		CameraSet.Emplace();
		CameraSet->Load(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json")));
	}
	return CameraSet.GetValue();
}
