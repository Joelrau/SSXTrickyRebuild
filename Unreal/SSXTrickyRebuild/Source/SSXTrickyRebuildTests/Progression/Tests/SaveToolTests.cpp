#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "Progression/SSXProgression.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXMemCard.h"
#include "Race/SSXReplay.h"
#include "Race/SSXReplayFile.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWriteMaxRankSaveTest, "SSX.Tools.WriteMaxRankSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FSSXWriteMaxRankSaveTest::RunTest(const FString& Parameters)
{
	if (FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WRITE_MAX_RANK_SAVE")) != TEXT("1"))
	{
		return true;
	}
	using namespace SSXProg;
	FSSXProfile P = FSSXProfile::MakeDefault();
	FSSXCareer& Eddie = P.Career[0];
	const int32 Top = RankThreshold(0);
	Eddie.Points[0] = Top / 2;
	Eddie.Points[1] = Top - Top / 2;
	for (int32 B = 0; B < 12; ++B) { if (BoardCost(B) <= Top) { Eddie.Boards |= 1u << B; } }
	Eddie.Outfits |= 1u << ChromeOutfit;                      // 0x16C420

	const FString Path = FSSXMemCard::FilePath(FSSXMemCard::SettingsName());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	TestTrue(TEXT("save written"), FFileHelper::SaveArrayToFile(P.SaveBlob(), *Path));

	TArray<uint8> Blob;
	FSSXProfile L = FSSXProfile::MakeDefault();
	TestTrue(TEXT("file read"), FFileHelper::LoadFileToArray(Blob, *Path));
	TestTrue(TEXT("boot load (checksum ok)"), L.BootLoadBlob(Blob));
	TestEqual(TEXT("Eddie rank points"), L.Career[0].RankPoints(), Top);
	TestEqual(TEXT("Eddie rank = Master"), RankFromPoints(L.Career[0].RankPoints()), 0);
	TestTrue(TEXT("settings are defaults"), L.Settings == FSSXProfile::MakeDefault().Settings);
	for (int32 C = 1; C < NumRiders; ++C) { TestEqual(*FString::Printf(TEXT("rider %d rank points"), C), L.Career[C].RankPoints(), 0); }
	AddInfo(FString::Printf(TEXT("WriteMaxRankSave: %s, %d bytes, Eddie rank points %d rank index %d"), *Path, Blob.Num(), L.Career[0].RankPoints(), RankFromPoints(L.Career[0].RankPoints())));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXInspectSaveTest, "SSX.Tools.InspectSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FSSXInspectSaveTest::RunTest(const FString& Parameters)
{
	using namespace SSXProg;
	const FString SavePath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_INSPECT_SAVE"));
	if (!SavePath.IsEmpty())
	{
		TArray<uint8> Blob;
		FSSXProfile P = FSSXProfile::MakeDefault();
		TestTrue(TEXT("file read"), FFileHelper::LoadFileToArray(Blob, *SavePath));
		TestEqual(TEXT("blob size"), Blob.Num(), int32(FSSXProfile::SettingsBlobSize));
		const bool bBoot = P.BootLoadBlob(Blob);
		FSSXProfile Q = FSSXProfile::MakeDefault();
		TestTrue(TEXT("load (checksum ok)"), Q.LoadBlob(Blob));
		AddInfo(FString::Printf(TEXT("Inspect: %s %d bytes, boot load %d, autoload %d"), *SavePath, Blob.Num(), bBoot ? 1 : 0, Q.Settings.Autoload));
		AddInfo(FString::Printf(TEXT("Inspect: venues 0x%08X riders 0x%08X"), Q.VenueUnlocks, Q.RiderUnlocks));
		for (int32 C = 0; C < NumRiders; ++C)
		{
			const FSSXCareer& K = Q.Career[C];
			FString Medals;
			for (int32 T = 0; T < NumTracks; ++T) { Medals += FString::Printf(TEXT("%d%d "), K.Tracks[T].RaceMedal, K.Tracks[T].ShowoffMedal); }
			int32 Chapters = 0;
			for (int32 Ch = 0; Ch < 6; ++Ch) { Chapters += Q.ChapterComplete(C, Ch) ? 1 : 0; }
			AddInfo(FString::Printf(TEXT("Inspect: rider %2d %-8s unlocked %d race %d showoff %d rank pts %d rank %d raw %d stats %d/%d/%d/%d boards 0x%03X outfits 0x%02X tricks 0x%08X chapters %d medals(race,showoff) %s"),
				C, SSXFeTables::RiderName(C), Q.IsRiderUnlocked(C, false) ? 1 : 0, K.Points[0], K.Points[1], K.RankPoints(), RankFromPoints(K.RankPoints()), K.RawPoints,
				K.StatPoints[0], K.StatPoints[1], K.StatPoints[2], K.StatPoints[3], K.Boards, K.Outfits, K.TrickBits(), Chapters, *Medals));
		}
		const FSSXSettings& S = Q.Settings;
		AddInfo(FString::Printf(TEXT("Inspect: settings master %.3f music %.3f mc %.3f v3 %.3f char %.3f sfx %.3f speaker %d arcadeSfx %d vib %d/%d units %d wide %d split %d ctrl %d/%d cam %d/%d hud %d bright %.3f maxBoarders %d replay %d lang %d"),
			S.Master, S.Music, S.McSpeech, S.Volume3, S.CharSpeech, S.Sfx, S.Speaker, S.ArcadeSfx, S.Vibration[0], S.Vibration[1], S.Units, S.Widescreen,
			S.SplitScreen, S.Controller[0], S.Controller[1], S.Camera1P, S.Camera2P, S.HudDetail, S.Brightness, S.MaxBoarders, S.Replay, S.Language));
		FString Names;
		for (int32 N = 0; N < NumNames; ++N)
		{
			FString One;
			for (int32 I = 0; I < 10 && Q.Names[N][I]; ++I) { One.AppendChar(TCHAR(Q.Names[N][I])); }
			Names += FString::Printf(TEXT("[%s] "), *One);
		}
		AddInfo(FString::Printf(TEXT("Inspect: names %s"), *Names));
		for (int32 T = 0; T < NumTracks; ++T)
		{
			for (int32 Tb = 0; Tb < 3; ++Tb)
			{
				const FSSXRecordRow& R0 = Q.Records[T].Tables[Tb].Rows[0];
				AddInfo(FString::Printf(TEXT("Inspect: record track %d table %d top: char %d value %d"), T, Tb, R0.Char, R0.Value));
			}
		}
		TestTrue(TEXT("re-save is byte identical"), Q.SaveBlob() == Blob);
	}
	const FString RepPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_INSPECT_REPLAY"));
	if (!RepPath.IsEmpty())
	{
		TArray<uint8> In;
		TestTrue(TEXT("replay read"), FFileHelper::LoadFileToArray(In, *RepPath));
		const bool bValid = SSXReplayFile::Validate(In);
		SSXReplayFile::FSetup Setup;
		const bool bSetup = SSXReplayFile::ReadSetup(In, Setup);
		FSSXReplay R;
		R.Init(6, 1, true, nullptr);
		R.AttachHuman(0);
		const bool bReplay = SSXReplayFile::ReadReplay(In, R);
		AddInfo(FString::Printf(TEXT("Inspect replay: %s %d bytes valid %d setup %d (mode %d venue %d racers %d humans %d seed %08x) replay object %d"),
			*RepPath, In.Num(), bValid ? 1 : 0, bSetup ? 1 : 0, Setup.GameMode, Setup.Venue, Setup.NumRacers, Setup.NumHumans(), Setup.Seed2B00,
			bReplay ? 1 : 0));
	}
	return true;
}

#endif
