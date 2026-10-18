#include "UI/SSXMemCard.h"
#include "Race/SSXReplayFile.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SSXTrickyRebuild.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndData.h"

FString& FSSXMemCard::RootOverride()
{
	static FString Override;
	return Override;
}

FString FSSXMemCard::RootDir()
{
	return RootOverride().IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MemoryCard")) : RootOverride();
}

bool FSSXMemCard::FileExists(int32 I) const
{
	return FPaths::FileExists(FilePath(FileName(I)));
}

void FSSXMemCard::Begin(EMode InMode, bool bInSave, FSSXProfile* InProfile)
{
	Mode = InMode;
	bSave = bInSave;
	Profile = InProfile;
	Selected = 0;
	bLoadedSettings = false;
	bLoadedReplay = false;
	LoadedReplay.Reset();
	// 0x1CEF78 / FUN_00292D80
	bPortMenu = false;
	Port = 0;
	WarnAlpha = 1.f;
	WarnDir = -1.f;
	Go(Checking, 180);
}

bool FSSXMemCard::WriteSettings()
{
	if (!Profile)
	{
		return false;
	}
	const FString Path = FilePath(SettingsName());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveArrayToFile(Profile->SaveBlob(), *Path);
}

bool FSSXMemCard::ReadSettings()
{
	TArray<uint8> Blob;
	return Profile && FFileHelper::LoadFileToArray(Blob, *FilePath(SettingsName())) && Profile->LoadBlob(Blob);
}

bool FSSXMemCard::WriteReplay()
{
	if (ReplayFile.Num() != SSXReplayFile::FileBytes)
	{
		return false;
	}
	const FString Path = FilePath(ReplayName(Selected));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveArrayToFile(ReplayFile, *Path);
}

bool FSSXMemCard::ReadReplay()
{
	// 0x291178 / 0x291240 / 0x2CE04C / 0x2912E8
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath(ReplayName(Selected))) || Bytes.Num() != SSXReplayFile::FileBytes
		|| !SSXReplayFile::SetupCrcValid(Bytes.GetData()) || !SSXReplayFile::Validate(Bytes))
	{
		return false;
	}
	LoadedReplay = MoveTemp(Bytes);
	bLoadedReplay = true;   // 0x166870
	return true;
}

int32 FSSXMemCard::SaveBytes() const
{
	return Mode == EMode::Replay ? SSXReplayFile::FileBytes : (Profile ? Profile->SaveBlob().Num() : 0);
}

void FSSXMemCard::Tick()
{
	// 0x1CEEE0
	WarnAlpha += WarnDir / 60.f;
	if (WarnAlpha <= 0.2f) { WarnAlpha = 0.2f; WarnDir = 1.f; }
	if (WarnAlpha >= 1.f) { WarnAlpha = 1.f; WarnDir = -1.f; }
	// 0x1D10B8
	if (Timer > 0 && !(State >= Loading && State <= Loading4))
	{
		--Timer;
	}
	const bool bFresh = bPendingFresh;
	bPendingFresh = false;
	switch (State)
	{
	case Checking:
		if (Timer == 0) { Go(CheckDone); }
		break;
	case CheckDone:
		Go(Reading, 90);
		break;
	case Reading:
		if (Timer == 0)
		{
			Go(bSave ? SaveList : LoadList);
		}
		break;
	case SaveTitle: Go(SaveDeleteOld); break;
	case SaveDeleteOld: Go(SaveMkdir); break;
	case SaveMkdir: Go(SaveStream); break;
	case SaveStream:
		Go((Mode == EMode::Replay ? WriteReplay() : WriteSettings()) ? SaveWait : SaveFailed, 120);
		break;
	case SaveWait:
		if (Timer == 0) { Go(SaveDone); }
		break;
	case Deleting:
		Go(Deleting2);
		break;
	case Deleting2:
	{
		const bool bOk = IFileManager::Get().DeleteDirectory(*(RootDir() / FileName(Selected)), false, true);
		Go(bOk ? DeleteDone : DeleteFailed);
		break;
	}
	case LoadList:
		// 0x1D1498 / 0x1CF040
		if (Pending != EPending::None && !bFresh)
		{
			const EPending P = Pending;
			Pending = EPending::None;
			if (P == EPending::X && FileValid(Selected)) { BeginLoad(); }
			else if (P == EPending::Triangle) { Go(Idle); }
		}
		break;
	case Loading:   // 0x1D1268
		Go(Loading2, 120);
		break;
	case Loading2:  // 0x1D12F0
	{
		bool bOk = false;
		if (Mode == EMode::Game)
		{
			bOk = ReadSettings();
			bLoadedSettings = bOk;
		}
		else
		{
			bOk = ReadReplay();      // 0x166870
		}
		if (bOk) { Go(Loading3, 120); } else { Go(LoadFailed); }
		break;
	}
	case Loading3:  // 0x1D1720
		Go(Loading4, Timer);
		break;
	case Loading4:  // 0x1D10B8
		if (Timer >= 1) { --Timer; } else { Go(LoadDone); }
		break;
	case LoadDone:  // 0x290FD0
		if (Mode == EMode::Replay) { Go(Idle); }
		break;
	case LoadFailed:   // 0x1D0FF0
		if (Pending == EPending::FailX && !bFresh)
		{
			Pending = EPending::None;
			Go(Reading, 90);
		}
		break;
	default:
		break;
	}
}

void FSSXMemCard::BeginLoad()
{
	Go(Loading, 120);
	WarnAlpha = 1.f;
	WarnDir = -1.f;
}

void FSSXMemCard::MoveFile(bool bDown)
{
	const int32 N = MaxFiles();
	if (bDown)
	{
		if (Selected < N - 1) { if (bInRace) Sounds.Add(SSXUISound::ERace::Navigate); ++Selected; }
		else if (bWrap) { Selected = 0; }
	}
	else
	{
		if (Selected > 0) { if (bInRace) Sounds.Add(SSXUISound::ERace::Navigate); --Selected; }
		else if (bWrap) { Selected = N - 1; }
	}
}

void FSSXMemCard::MovePort(bool bDown)
{
	const int32 Ports = 2;
	if (bDown)
	{
		if (Port < Ports - 1 && bInRace) Sounds.Add(SSXUISound::ERace::Navigate);
		if (Port < Ports - 1) { ++Port; } else if (bWrap) { Port = 0; }
	}
	else
	{
		if (Port > 0 && bInRace) Sounds.Add(SSXUISound::ERace::Navigate);
		if (Port > 0) { --Port; } else if (bWrap) { Port = Ports - 1; }
	}
}

void FSSXMemCard::AcceptPort()
{
	if (bInRace) Sounds.Add(SSXUISound::ERace::Accept);   // FUN_002138F0
}

void FSSXMemCard::Input(uint32 Bits)
{
	// 0x1CFAA8 / 0x1D0150 / 0x1D02E8 / 0x1D0358
	const bool bX = (Bits & InAccept) != 0, bTri = (Bits & InBack) != 0, bCi = (Bits & InCircle) != 0;
	const bool bUp = (Bits & InUp) != 0, bDown = (Bits & InDown) != 0;
	const bool bXStart = (Bits & (InAccept | InStart)) != 0;
	switch (State)
	{
	case NoCard: case NoCard2:
		if (bTri) { SndBack(); Go(Idle); } else if (bX) { SndAccept(); Go(Checking, 180); }
		break;
	case SaveList:
		if (bTri) { SndBack(); Go(Idle); }
		else if (bX)
		{
			// X: slot has a file -> 9, not enough space -> 0x15, else -> 0xD (KB required = ceil(bytes / 1024) + 131)
			SndAccept();
			if (FileExists(Selected)) { Go(Overwrite); }
			else if (FreeKb() < KbRequired(SaveBytes())) { Go(TooBig); }
			else { Go(SaveTitle); }
		}
		else if (bCi) { SndAccept(); if (FileExists(Selected)) { Go(DeleteAsk); } }
		else if (bUp) { MoveFile(false); }
		else if (bDown) { MoveFile(true); }
		break;
	case Overwrite:   // 0x1D1C30
		if (bTri) { SndAccept(); Go(SaveList); } else if (bX) { SndBack(); Go(SaveTitle); }
		break;
	case Unformatted:   // 0x1D0150
		if (bX) { SndBack(); Go(SaveList); } else if (bTri) { SndAccept(); Go(Formatting, 120); }
		break;
	case LoadFailed:   // 0x1D0FF0
		if (Pending == EPending::None && bXStart) { SndAccept(); Pending = EPending::FailX; bPendingFresh = true; }
		break;
	case FormatFailed: case SaveFailed: case DeleteDone: case DeleteFailed:
		if (bXStart) { SndAccept(); Go(State == FormatFailed ? Checking : Reading, State == FormatFailed ? 180 : 90); }
		break;
	case SaveDone:
		if (bXStart) { SndAccept(); Go(Idle); }
		break;
	case TooBig:
		if (bXStart) { SndAccept(); Go(SaveList); }
		break;
	case TooBig2:
		if (bX) { SndAccept(); Go(Checking, 180); } else if (bTri) { SndBack(); Go(Idle); }
		break;
	case DeleteAsk:
		if (bTri) { SndBack(); Go(bSave ? SaveList : LoadList); } else if (bX) { SndAccept(); Go(Deleting); }
		break;
	case LoadList:
		// 0x1D1498
		if (Pending != EPending::None) { break; }
		if (bTri) { SndBack(); Pending = EPending::Triangle; bPendingFresh = true; }
		else if (bX) { SndAccept(); Pending = EPending::X; bPendingFresh = true; }
		else if (bCi) { SndAccept(); Pending = EPending::Circle; bPendingFresh = true; }
		else if (bUp) { MoveFile(false); }
		else if (bDown) { MoveFile(true); }
		break;
	case LoadDone:   // 0x290FD0
		if (Mode == EMode::Game && bXStart) { SndAccept(); Go(Idle); }
		break;
	default:
		break;
	}
}

int32 FSSXMemCard::MessageLoc() const
{
	switch (State)
	{
	case Checking: case CheckDone: return 64;
	case NoCard: case NoCard2: return 394;
	case Reading: return 416;
	case Overwrite: return 404;
	case Unformatted: return 355;
	case Formatting: return 360;
	case FormatFailed: return 358;
	case SaveTitle: case SaveDeleteOld: case SaveMkdir: case SaveStream: case SaveWait: return 451;
	case SaveDone: return 447;
	case SaveFailed: return 448;
	case TooBig: case TooBig2: return 462;
	case DeleteAsk: return 346;
	case Deleting: case Deleting2: return 348;
	case DeleteDone: return 344;
	case DeleteFailed: return 345;
	case Loading: case Loading2: case Loading3: case Loading4: return 380;
	case LoadDone: return Mode == EMode::Game ? 371 : -1;
	case LoadFailed: return 372;
	default: return -1;
	}
}

uint32 FSSXMemCard::DialogMask() const
{
	switch (State)
	{
	case NoCard: case NoCard2: return 0x03;
	case Overwrite: return 0xC0;
	case Unformatted: return 0x22;
	case FormatFailed: case SaveFailed: case DeleteDone: case DeleteFailed: case LoadDone: case LoadFailed: return 0x10;
	case SaveDone: case TooBig: return 0x04;
	case TooBig2: return 0x03;
	case DeleteAsk: return 0x0A;
	default: return 0;
	}
}

FSSXMemCard::FRaceView FSSXMemCard::RaceView() const
{
	// 0x1CF6E8 / 0x1D22C8 / 0x1D2C78
	FRaceView V;
	auto Status = [&V](bool bWarn) { V.Kind = FRaceView::Status; V.bWarning = bWarn; };
	auto Box = [&V](int32 B1, int32 B2, bool bXFirst, int32 Text = -1)
	{
		V.Kind = FRaceView::Dialog; V.Button1 = B1; V.Button2 = B2; V.bXFirst = bXFirst; V.TextLoc = Text;
	};
	switch (State)
	{
	case Checking: case CheckDone: case Reading: Status(false); break;
	case NoCard: case NoCard2: Box(442, 340, true); break;
	case SaveList: V.Kind = FRaceView::List; break;                         // 0x1B8D08
	case Overwrite: Box(470, 392, true); break;
	case Unformatted: Box(354, 340, false); break;
	case Formatting: Status(true); break;
	case FormatFailed: Box(341, -1, true); break;
	case SaveTitle: case SaveDeleteOld: case SaveMkdir: case SaveStream: case SaveWait:
		V.Kind = FRaceView::Saving; V.bWarning = true; break;               // 0x1B8718
	case SaveDone: Box(341, -1, true, 447); break;
	case SaveFailed: case TooBig: Box(341, -1, true); break;
	case TooBig2: Box(442, 340, true); break;
	case DeleteAsk: Box(470, 392, true); break;
	case Deleting: case Deleting2: Status(true); break;
	case DeleteDone: Box(341, -1, true, 344); break;
	case DeleteFailed: Box(341, -1, true); break;
	case Loading: case Loading2: case Loading3: case Loading4: Status(true); break;
	case LoadDone: Box(341, -1, true, 371); break;
	case LoadFailed: Box(341, -1, true); break;
	default: break;
	}
	return V;
}

FString FSSXMemCard::MessageText(const FSSXLocStrings& Loc) const
{
	const int32 Id = MessageLoc();
	if (Id < 0)
	{
		return FString();
	}
	FString S = Loc.ByIndex(Id);
	S.ReplaceInline(TEXT("%d"), TEXT("1"));
	// 0x1CE8F8
	FString Name;
	if (State >= SaveTitle && State <= SaveWait)
	{
		const int32 Venue = ReplayFile.Num() >= 0x320 ? int32(ReplayFile[0x31C] | (ReplayFile[0x31D] << 8) | (ReplayFile[0x31E] << 16)
			| (uint32(ReplayFile[0x31F]) << 24)) : -1;
		Name = IconTitle(Loc, Selected, Venue);
	}
	else
	{
		Name = FileTitle(Loc, Selected);
	}
	S.ReplaceInline(TEXT("%s"), *Name);
	return S;
}

FString FSSXMemCard::TrackCode(int32 Venue)
{
	// 0x332F28 / 0x1CE8F8
	static const TCHAR* Codes[] = { TEXT("gari"), TEXT("snow"), TEXT("elys"), TEXT("mesa"), TEXT("merq"), TEXT("aloh"),
		TEXT("pipe"), TEXT("untr"), TEXT("mega"), TEXT("biga"), TEXT("tric"), TEXT("alas"), TEXT("ssxf") };
	return Venue >= 0 && Venue < int32(UE_ARRAY_COUNT(Codes)) ? FString(Codes[Venue]) : FString();
}

int32 FSSXMemCard::FileVenue(int32 I)
{
	TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*FilePath(ReplayName(I))));
	if (!Ar || Ar->TotalSize() < 0x320)
	{
		return -1;
	}
	uint8 B[4] = {};
	Ar->Seek(0x31C);
	Ar->Serialize(B, 4);
	return int32(B[0] | (B[1] << 8) | (B[2] << 16) | (uint32(B[3]) << 24));
}

FString FSSXMemCard::IconTitle(const FSSXLocStrings& Loc, int32 Slot, int32 Venue) const
{
	if (Mode == EMode::Game)
	{
		return Loc.ByIndex(459);
	}
	return Loc.ByIndex(460).Replace(TEXT("%d"), *FString::FromInt(Slot + 1)).Replace(TEXT("%S"), *TrackCode(Venue));
}

FString FSSXMemCard::FileTitle(const FSSXLocStrings& Loc, int32 I) const
{
	// 0x1CE8F8
	if (!FileExists(I))
	{
		return Loc.ByIndex(351);
	}
	return IconTitle(Loc, I, Mode == EMode::Replay ? FileVenue(I) : -1);
}

int32 FSSXMemCard::FreeKb() const
{
	// 0x23FF18
	int32 Kb = 8192;
	auto Take = [&Kb](const FString& Name)
	{
		const int64 Size = IFileManager::Get().FileSize(*FilePath(Name));
		if (Size > 0) { Kb -= KbRequired(int32(Size)); }
	};
	Take(SettingsName());
	for (int32 I = 0; I < 4; ++I) { Take(ReplayName(I)); }
	return FMath::Max(Kb, 0);
}

uint32 FSSXMemCard::HelpMask() const
{
	// 0x2914D0 / 0x292968 / FUN_002718F0
	if (bPortMenu)
	{
		return 0x140;
	}
	return State == Idle ? 0u : (bSave ? 0xB00u : 0x500u);
}

uint32 FSSXMemCard::LegendDisabledMask() const
{
	if (bPortMenu)
	{
		return PortValid(Port) ? 0u : 0x40u;   // FUN_00271848
	}
	const uint32 All = bSave ? 0xB00u : 0x500u;
	switch (State)
	{
	case SaveList:   // FUN_00292968
		return FileExists(Selected) ? 0u : 0x800u;
	case LoadList:   // FUN_00292968 / FUN_002923b8
		return FileValid(Selected) ? 0u : 0x400u;
	// FUN_00292810 / FUN_002924d0
	case Checking: case CheckDone: case Reading: case Formatting: case Deleting: case Deleting2:
	case SaveTitle: case SaveDeleteOld: case SaveMkdir: case SaveStream: case SaveWait:
	case Loading: case Loading2: case Loading3: case Loading4:
		return All;
	default:         // FUN_002923b8 / FUN_002924d0
		return 0u;
	}
}

void FSSXMemCard::DrawFe(FSSXFeDrawList& D, const FSSXLocStrings& Loc, int32 Frame) const
{
	using namespace SSXFe;
	auto Fmt = [&Loc, this](int32) { return MessageText(Loc); };

	// 0x28F3E0 / FUN_00292E70
	if (bPortMenu)
	{
		const float Row = D.FontHeight(EFont::Menu, 1.f) + ShadowOffset;
		for (int32 P = 0; P < 2; ++P)
		{
			const bool bOk = PortValid(P);
			const bool bHi = P == Port;
			const FLinearColor C = bOk ? (bHi ? Gold : White) : (bHi ? FeColor(0.7f, 0.953f, 0.729f, 0.106f) : Grey);
			const FString Str = TEXT(" ");   // 0x3B1160
			const float W = D.TextBoxWidth(EFont::Menu, Str, 1.f) + ShadowOffset;
			D.Text(EFont::Menu, (ScreenW - W) * 0.5f, 140.f + P * Row, 1.f, C, Str, ESSXAlign::Left, true, TextLayer(3));
		}
	}

	// 0x28F3E0 / FUN_00275468 / FUN_002923b8 / FUN_00292428
	const bool bPreList = State == Checking || State == CheckDone || State == Reading;
	if (!bPortMenu && State != Idle)
	{
		const FString Title = Loc.ByIndex(3761);
		const float Tw = D.TextBoxWidth(EFont::Title, Title, 1.f) + ShadowOffset;
		D.Text(EFont::Title, (ScreenW - Tw) * 0.5f, 60.f, 1.f, White, Title, ESSXAlign::Left, true, TextLayer(3));
		const float Th = D.FontHeight(EFont::Title, 1.f) + ShadowOffset;
		const FString Free = Loc.ByIndex(312).Replace(TEXT("%d"), *FString::FromInt(FreeKb()));
		const float Fw = D.TextBoxWidth(EFont::Menu, Free, 1.f) + ShadowOffset;
		if (!bPreList)
		{
			D.Text(EFont::Menu, (ScreenW - Fw) * 0.5f, 60.f + Th + 7.f, 1.f, White, Free, ESSXAlign::Left, true, TextLayer(3));
		}

		const float Row = D.TextHeight(EFont::Menu, 1.f) + ListSpacing;
		for (int32 I = 0; I < MaxFiles(); ++I)
		{
			const FLinearColor C = ListFocused() && I == Selected ? Gold : White;
			const float Y = 190.f + I * Row;
			D.Text(EFont::Menu, 185.f, Y, 1.f, C, FString::FromInt(I + 1), ESSXAlign::Left, true, TextLayer(3));
			D.Text(EFont::Menu, 215.f, Y, 1.f, C, bPreList ? Loc.ByIndex(351) : FileTitle(Loc, I), ESSXAlign::Left, true, TextLayer(3));
		}
	}

	const int32 Msg = MessageLoc();
	if (Msg >= 0)
	{
		const uint32 Mask = DialogMask();
		if (Mask != 0)
		{
			// 0x291E00 / 0x2AE098 / 0x2AFB30 / 0x2AF8C0
			const float Dy = (ScreenH - 225.f) * 0.5f;
			D.TextBox(EFont::Menu, 0.f, Dy, 640.f, 1.f, White, Fmt(Msg), ESSXAlign::Center, true, TextLayer(4));
			struct FBtn { int32 Icon = -1; int32 Loc = -1; };
			FBtn B1, B2;
			if (Mask & 0x10) B1 = { IconCross, 341 };
			if (Mask & 0x01) B1 = { IconCross, 442 };
			if (Mask & 0x04) B1 = { IconCross, 341 };
			if (Mask & 0x08) B1 = { IconCross, 343 };
			if (Mask & 0x20) B1 = { IconTriangle, 354 };
			if (Mask & 0x40) B1 = { IconCross, 470 };
			if (Mask & 0x02)
			{
				if (B1.Loc < 0) B1 = { IconTriangle, 340 };
				else B2 = { State == Unformatted ? IconCross : IconTriangle, 340 };
			}
			if (Mask & 0x80) B2 = { IconTriangle, 392 };
			const float By = Dy + 225.f - 60.f;
			constexpr float BtnIcon = 17.f, BtnGap = 4.f, BtnLabelScale = 0.7f;
			const float Lh = D.FontHeight(EFont::Menu, BtnLabelScale) + ShadowOffset;
			auto Width = [&](const FBtn& B) { return BtnGap + D.TextWidth(EFont::Menu, Loc.ByIndex(B.Loc), BtnLabelScale) + ShadowOffset; };
			auto DrawBtn = [&](const FBtn& B, float X)
			{
				if (B.Loc < 0) return;
				D.Sprite(B.Icon, X - BtnIcon, By, BtnIcon, BtnIcon, White, SpriteLayer(4));
				D.Text(EFont::Menu, X + BtnGap, By + (BtnIcon - Lh) * 0.5f, BtnLabelScale, White, Loc.ByIndex(B.Loc), ESSXAlign::Left, true,
					TextLayer(4));
			};
			if (B2.Loc < 0)
			{
				DrawBtn(B1, 320.f - Width(B1) * 0.5f);
			}
			else
			{
				DrawBtn(B1, 320.f - (Width(B1) + 50.f));
				DrawBtn(B2, 370.f);
			}
		}
		else
		{
			const float Sy = bSave ? 255.f : 280.f;
			const float H = D.TextBox(EFont::Menu, 20.f, Sy, 600.f, 1.f, White, Fmt(Msg), ESSXAlign::Center, true, TextLayer(4));
			// FUN_00292638
			const bool bWarn = State == Formatting || (State >= SaveTitle && State <= SaveWait) || State == Deleting
				|| State == Deleting2 || (State >= Loading && State <= Loading4);
			if (bWarn)
			{
				FLinearColor C = White;
				C.A = WarnAlpha;
				D.TextBox(EFont::Menu, 20.f, Sy + FMath::Max(H, 50.f) + 5.f, 600.f, 1.f, C, Loc.ByIndex(350), ESSXAlign::Center, true, TextLayer(4));
			}
		}
	}
}
