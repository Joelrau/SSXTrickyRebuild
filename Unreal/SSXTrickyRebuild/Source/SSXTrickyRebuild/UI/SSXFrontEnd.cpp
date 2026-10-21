#include "UI/SSXFrontEnd.h"
#include "UI/SSXFeTrickBook.h"
#include "Sim/Air/TrickName.h"

#include "SSXTrickyRebuild.h"
#include "Race/GameRng.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFrontEndData.h"
#include "World/SSXRenderLook.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/App.h"

namespace
{
	// FUN_00267B18 / 0x264188 / 0x163D30 / 0x163D98
	// PORT DEVIATION (user request): every FE rider wears its own saved board / outfit; the original gives only the pad's
	// own rider its saved gear and every other FE rider board 0 / outfit 0 (0 = the original).
	TAutoConsoleVariable<int32> CVarFeAllRidersSavedGear(TEXT("ssx.FE.AllRidersSavedGear"), 1,
		TEXT("1: every FE rider wears its own saved board / outfit (user-requested port difference). 0: the original's rule ")
		TEXT("(only the pad's rider, the others board 0 / outfit 0)."));

	// PORT DEVIATION (user request): accepting a board / outfit on the Customize screens writes it to the rider's saved
	// record at once; the original writes the records only at the character accept and the race start (0x1637F8), so a
	// choice made after the accept is lost when the rider is changed or the menus are left without racing.
	TAutoConsoleVariable<int32> CVarFeSaveGearOnAccept(TEXT("ssx.FE.SaveGearOnAccept"), 1,
		TEXT("1: accepting a board / outfit on the Customize screens saves it to the rider's record (user-requested port ")
		TEXT("difference). 0: the original (saved at the character accept and the race start only)."));
}

using namespace SSXFe;

namespace
{
	constexpr float HBar = SSXFe::HBarH;

	// 0x3B2AB8 / 0x3B2AD0
	const int32 EventPool[] = { 3717, 3588, 3589, 3590, 3591 };
	enum { EvTC = 0, EvFreeride = 1, EvRace = 2, EvShowoff = 3, EvTutorial = 4 };

	const TCHAR* GroupName(int32 G) { return G == 1 ? TEXT("PR") : (G == 2 ? TEXT("WC") : TEXT("SE")); }

	// 0x278F60
	constexpr int32 RankingFrames = 120, AttributesFrames = 270;
}

const TCHAR* LexToString(ESSXFeScreen S)
{
	switch (S)
	{
	case ESSXFeScreen::Title: return TEXT("Title");
	case ESSXFeScreen::ModeSelect: return TEXT("ModeSelect");
	case ESSXFeScreen::EventSel: return TEXT("EventSel");
	case ESSXFeScreen::NumPlayers: return TEXT("NumPlayers");
	case ESSXFeScreen::CharacterSelect: return TEXT("CharacterSelect");
	case ESSXFeScreen::CharacterAccept: return TEXT("CharacterAccept");
	case ESSXFeScreen::CharacterModify: return TEXT("CharacterModify");
	case ESSXFeScreen::TrickBook: return TEXT("TrickBook");
	case ESSXFeScreen::VenueSel: return TEXT("VenueSel");
	case ESSXFeScreen::DifficultSel: return TEXT("DifficultSel");
	case ESSXFeScreen::UserName: return TEXT("UserName");
	case ESSXFeScreen::DVDContent: return TEXT("DVDContent");
	case ESSXFeScreen::Video: return TEXT("Video");
	case ESSXFeScreen::CustomBoard: return TEXT("CustomBoard");
	case ESSXFeScreen::CustomBody: return TEXT("CustomBody");
	case ESSXFeScreen::RiderProfile: return TEXT("RiderProfile");
	case ESSXFeScreen::Options: return TEXT("Options");
	case ESSXFeScreen::MemCard: return TEXT("MemCard");
	}
	return TEXT("?");
}

int32 SSXFeTypeId(ESSXFeScreen S)
{
	switch (S)
	{
	case ESSXFeScreen::Title: return 0;
	case ESSXFeScreen::ModeSelect: return 1;
	case ESSXFeScreen::EventSel: return 2;
	case ESSXFeScreen::NumPlayers: return 3;
	case ESSXFeScreen::CharacterSelect: return 4;
	case ESSXFeScreen::CharacterAccept: return 5;
	case ESSXFeScreen::CharacterModify: return 6;
	case ESSXFeScreen::TrickBook: return 7;
	case ESSXFeScreen::VenueSel: return 8;
	case ESSXFeScreen::DifficultSel: return 9;
	case ESSXFeScreen::UserName: return 10;
	case ESSXFeScreen::DVDContent: return 11;
	case ESSXFeScreen::Video: return 12;
	default: return -1;
	}
}

int32 SSXFeUserName::StepLeft(int32 Idx)
{
	if (Idx % Cols == 0) { const int32 N = Idx - 1 + Cols; return N < NumCells ? N : NumCells - 1; }
	return Idx - 1;
}

int32 SSXFeUserName::StepRight(int32 Idx)
{
	if (Idx % Cols == Cols - 1) { return FMath::Max(0, Idx + 1 - Cols); }
	return Idx == NumCells - 1 ? (NumCells - 1) - (NumCells - 1) % Cols : Idx + 1;
}

int32 SSXFeUserName::StepUp(int32 Idx)
{
	if (Idx / Cols == 0)
	{
		const int32 LastCol = (NumCells - 1) % Cols;
		return Idx % Cols >= LastCol ? NumCells - 1 : (NumCells - (LastCol + 1)) + Idx % Cols;
	}
	return Idx - Cols;
}

int32 SSXFeUserName::StepDown(int32 Idx)
{
	const int32 RowsN = (Cols + NumCells - 1) / Cols;
	if (Idx / Cols < RowsN - 1) { return FMath::Min(Idx + Cols, NumCells - 1); }
	return Idx % Cols;
}

TCHAR SSXFeUserName::Cell(int32 X, int32 Y)
{
	// 0x3B7F58
	const FString& Grid = SSXFeTables::NameGrid();
	const int32 I = Y * Cols + X;
	return X >= 0 && X < Cols && I >= 0 && I < Grid.Len() ? Grid[I] : TEXT(' ');
}

FString FSSXFrontEnd::L(int32 Id) const
{
	return Loc ? Loc->ByIndex(Id) : FString();
}

void FSSXFrontEnd::Init(FSSXProfile* InProfile, FSSXCheats* InCheats, const FSSXLocStrings* InLoc, bool bColdBoot, int32 InExitFlag)
{
	// FUN_00263280
	ExitFlag = bColdBoot ? 1 : InExitFlag;
	Profile = InProfile;
	Cheats = InCheats;
	Loc = InLoc;
	Stack.Reset();
	CamMode = 0;
	bLaunchRequested = false;
	bLaunchPending = false;
	bHasPendingOpen = false;
	MovieRequest.Reset();
	BootFrame = 0;
	BootState = 0;
	BootTimer = 0;
	CarouselWindow = INDEX_NONE;
	FMemory::Memzero(CarouselSignFrame);
	FMemory::Memzero(CarouselPlatformFrame);
	++CarouselSerial;
	if (bColdBoot)
	{
		Boot = ESSXFeBoot::StartScreen;
	}
	else
	{
		// 0x263B30 / FUN_001FF648
		Boot = ESSXFeBoot::FrontEnd;
		if (ExitFlag == 0x10)
		{
			Fader.Start(0.f, 1.f, 0.5f, FLinearColor::Black);
		}
		Push(Make(ESSXFeScreen::Title));
	}
	for (int32 P = 0; P < 2; ++P)
	{
		const int32 Char = Profile ? Profile->Slot[P].Char : -1;
		// 0x263B30 / FUN_001635B8 / FUN_00265788
		CarouselSlot[P] = Char >= 0 ? SSXFeTables::CarouselSlot(Char) : DefaultCarouselSlot;
	}
	int32 CharBySlot[SSXFeRiders::NumRiders];
	for (int32 K = 0; K < SSXFeRiders::NumRiders; ++K)
	{
		CharBySlot[K] = SSXFeTables::CarouselRider(K);
	}
	Riders.Init(CharBySlot);
	FMemory::Memzero(RiderModelBoard);
	FMemory::Memzero(RiderModelOutfit);
	if (!bColdBoot)
	{
		BuildRiderGear();
	}
}

void FSSXFrontEnd::Sound(const TCHAR* Hook)
{
	const ESSXFeScreen S = Current();
	const bool bAny = Stack.Num() > 0;
	const bool bAlt = bAny && (S == ESSXFeScreen::ModeSelect || S == ESSXFeScreen::CharacterSelect || S == ESSXFeScreen::VenueSel);
	int32 Refused = 7, Move = 4, Ok = 5, BackId = 6, CircleOk = 5, SquareOk = 5, CircleNo = 7;
	if (bAlt) { Refused = 3; Move = 0; Ok = 1; BackId = 2; CircleOk = 1; SquareOk = 1; CircleNo = 3; }
	else if (bAny && S == ESSXFeScreen::TrickBook) { Ok = 4; CircleOk = 4; CircleNo = 4; }
	else if (bAny && S == ESSXFeScreen::DVDContent) { Ok = 0xFF; CircleOk = 0xFF; SquareOk = 0xFF; }
	else if (bAny && S == ESSXFeScreen::Options) { CircleOk = 0xFF; SquareOk = 0xFF; }
	const FString H(Hook);
	int32 Id = -1;
	if (H == TEXT("accept")) { Id = Ok; }
	else if (H == TEXT("refused")) { Id = Refused; }
	else if (H == TEXT("back")) { Id = BackId; }
	else if (H == TEXT("move")) { Id = Move; }
	else if (H == TEXT("circle")) { Id = CircleOk; }
	else if (H == TEXT("circleRefused")) { Id = CircleNo; }
	else if (H == TEXT("square")) { Id = SquareOk; }
	else { SoundHooks.Add(H); return; }
	if (Id != 0xFF) { PlayId(Id); }
}

void FSSXFrontEnd::MovieFinished()
{
	// 0x2AB918 / FUN_00275B90 / FUN_00275B10
	MovieRequest.Reset();
	bMovieSkippable = false;
	switch (Boot)
	{
	case ESSXFeBoot::EaLogo:
		// 0x2637A0
		Boot = ESSXFeBoot::Intro;
		MovieRequest = TEXT("SSXINTRO");
		bMovieSkippable = true;   // 0x2BA9F8
		break;
	case ESSXFeBoot::Intro:
		Boot = ESSXFeBoot::FrontEnd;
		IdleFrames = 0;
		if (Stack.Num() == 0)
		{
			Push(Make(ESSXFeScreen::Title));
		}
		break;
	case ESSXFeBoot::Attract:
		// 0x265398 / FUN_00275B90 / 0x293938 / 0x2ABA30
		Boot = ESSXFeBoot::FrontEnd;
		IdleFrames = 0;
		if (Stack.Num() == 0)
		{
			Push(Make(ESSXFeScreen::Title));
		}
		else if (FSSXFeState* T = TopMut())
		{
			OnReshow(*T);
		}
		break;
	default:
		break;
	}
}

FSSXFeState FSSXFrontEnd::Make(ESSXFeScreen Screen, int32 Player) const
{
	FSSXFeState S;
	S.Screen = Screen;
	S.Player = Player;
	auto Item = [&S](int32 Id, int32 LocId, int32 Help = -1) { FSSXFeItem I; I.Id = Id; I.Loc = LocId; I.HelpLoc = Help; S.Items.Add(I); };
	const bool bUnlockAll = Cheats && Cheats->bUnlockAll;
	switch (Screen)
	{
	case ESSXFeScreen::Title:
		Item(0, 3696);
		Item(1, 3315);
		S.TitleLoc = -1;
		S.bBack = false;
		S.HelpMask = 0x2;
		break;
	case ESSXFeScreen::ModeSelect:
		S.TitleLoc = 3499;
		S.BarH = HBar;
		S.HelpMask = 0x200A;
		Item(0, 3594, 3235);
		Item(1, 3595, 3233);
		Item(2, 3596, 3234);
		S.Sel = 1;
		S.bSelectedOnly = true;
		S.bVertical = false;
		S.bCamSkip = false;    // 0x29A330
		break;
	case ESSXFeScreen::NumPlayers:
		S.TitleLoc = 3500;
		S.BarH = 194.f;
		S.HelpMask = 0x200A;
		Item(0, 3597, 3238);   // 0x29AC80
		Item(1, 3611, 3239);
		S.InFade = 0;          // 0x29A928
		break;
	case ESSXFeScreen::EventSel:
	{
		S.TitleLoc = 3498;
		TArray<int32> Ids;
		if (Group == 1 || Group == 3 || Group == 4) { Ids = { EvFreeride, EvTutorial }; }
		else if (Group == 2) { Ids = { EvRace, EvShowoff }; }
		else { Ids = { EvRace, EvShowoff, EvTC }; }
		// FUN_0029B5D0
		static const int32 EventHelp[] = { 3276, 3236, 3271, 3275, 3237 };
		for (int32 Id : Ids) { Item(Id, EventPool[Id], EventHelp[Id]); }
		S.BarH = Ids.Num() == 3 ? 224.f : 194.f;
		S.HelpMask = 0x200A;
		break;
	}
	case ESSXFeScreen::DifficultSel:
		S.TitleLoc = 3497;
		S.BarH = 224.f;
		S.HelpMask = 0x200A;
		Item(0, 3587, 3272);   // FUN_0029B9F8
		Item(1, 3593, 3273);
		Item(2, 3592, 3274);
		S.InFade = 0;          // 0x29B6A8
		break;
	case ESSXFeScreen::CharacterSelect:
		S.TitleLoc = 3495 + Player;
		S.BarH = HBar;
		S.HelpMask = 0x200A;
		S.bVertical = false;
		S.bSelectedOnly = true;
		S.bWrap = true;   // 0x276E28
		for (int32 Slot = 0; Slot < SSXFeTables::NumRiders; ++Slot)
		{
			const int32 Char = SSXFeTables::CarouselRider(Slot);
			FSSXFeItem I;
			I.Id = Slot;
			I.Text = SSXFeTables::RiderName(Char);
			I.bEnabled = Profile ? Profile->IsRiderUnlocked(Char, bUnlockAll) : true;
			const int32 K = SSXFeTables::UnlockRank(Char);
			I.HelpLoc = K >= 4 ? 3525 + (K - 4) : -1;
			S.Items.Add(I);
		}
		S.Sel = CarouselSlot[Player];
		S.bCamSkip = false;   // 0x278598
		break;
	case ESSXFeScreen::CharacterAccept:
		S.TitleLoc = 3502 + Player;
		S.BarH = HBar;
		S.HelpMask = 0x200A;
		S.bSelectedOnly = true;
		Item(0, 3585);
		S.InFade = 1;   // 0x279EC0
		break;
	case ESSXFeScreen::CharacterModify:   // 0x3AF688
		S.TitleLoc = 3502 + Player;
		S.BarH = 314.f;
		S.HelpMask = 0x200A;
		Item(0, 3585, 3240);
		Item(1, 3603, 3242);
		Item(2, 3578, 3250);
		Item(3, 3606, 3262);
		Item(4, 3610, 3268);
		Item(5, 3612, 3270);
		S.InFade = 0;   // 0x279908
		break;
	case ESSXFeScreen::CustomBoard:
	{
		S.TitleLoc = 181;
		S.BarH = HBar;
		S.HelpMask = 0x200A;
		S.bSelectedOnly = true;
		const int32 Char = Profile ? Profile->Slot[Player].Char : 0;
		for (int32 B = 0; B < SSXFeTables::BoardsPerRider; ++B)
		{
			FSSXFeItem I;
			I.Id = B;
			I.Text = SSXFeTables::Board(Char, B).Name;   // 0x332148
			I.bEnabled = Profile ? Profile->IsBoardUnlocked(Char, B, bUnlockAll) : true;
			I.HelpLoc = B < 2 ? -1 : (B == 11 ? 3261 : 3252 + (B - 2));   // 0x3AFB78
			S.Items.Add(I);
		}
		S.Sel = Profile ? Profile->Slot[Player].Board : 0;
		S.SavedValue = S.Sel;
		S.InFade = 0;   // 0x28C718
		break;
	}
	case ESSXFeScreen::CustomBody:
	{
		S.TitleLoc = 182;
		S.BarH = HBar;
		S.HelpMask = 0x200A;
		S.bSelectedOnly = true;
		const int32 Char = Profile ? Profile->Slot[Player].Char : 0;
		const int32 Other = Profile && NumPlayers > 1 ? Profile->Slot[1 - Player].Char : -1;
		for (int32 O = 0; O < SSXFeTables::OutfitsPerRider; ++O)
		{
			FSSXFeItem I;
			I.Id = O;
			I.Text = SSXFeTables::OutfitName(Char, O);   // 0x332C88
			I.bEnabled = Profile ? Profile->IsOutfitUnlocked(Char, O, bUnlockAll) : true;
			if (Other == Char && Profile && Profile->Slot[1 - Player].Outfit == O)
			{
				I.bEnabled = false;   // FUN_00267CC0
			}
			I.HelpLoc = O >= 1 && O <= 5 ? 3245 + (O - 1) : (O == 6 ? 3535 : -1);   // 0x3AFBA0
			S.Items.Add(I);
		}
		S.Sel = Profile ? Profile->Slot[Player].Outfit : 0;
		S.SavedValue = S.Sel;
		S.InFade = 0;   // 0x28D9C0
		break;
	}
	case ESSXFeScreen::RiderProfile:   // 0x28B968 / 0x3AFAC0
	{
		S.TitleLoc = 3494;
		S.BarH = HBar;
		S.bSelectedOnly = true;
		S.HelpMask = 0x8;   // FUN_002718F0
		S.HelpW = 420.f;
		S.bSquare = false;
		for (int32 P = 0; P < 5; ++P)
		{
			static const int32 Locs[5] = { 3604, 3708, 3607, 3718, 3605 };
			Item(P, Locs[P]);
			S.Items.Last().HelpLoc = 3263 + P;
		}
		// FUN_00284F60 / FUN_00284C70
		const int32 Char = Profile ? FMath::Clamp(Profile->Slot[Player].Char, 0, 11) : 0;
		for (int32 K = 0; K < 2; ++K)
		{
			FSSXFeScrollText& T = S.ProfileText[K];
			(K == 0 ? SSXFeRiderProfile::BackStoryRange : SSXFeRiderProfile::InterviewRange)(Char, T.First, T.Last);
			T.Scale = SSXFeRiderProfile::TextScale;
			T.Extra = SSXFeRiderProfile::TextExtra;
			T.Reset(SSXFeRiderProfile::TextH);
		}
		break;
	}
	case ESSXFeScreen::TrickBook:
	{
		// 0x27A5B8 / FUN_00274208 / FUN_002718F0 / 0x27E008
		S.TitleLoc = 3504;
		S.BarH = HBar;
		S.HelpMask = SSXFeTrickBook::ChapterLegend;
		S.bSelectedOnly = true;
		S.bSquare = false;
		const int32 Char = Profile ? Profile->Slot[Player].Char : 0;
		for (int32 C = 0; C < SSXFeTrickBook::Chapters; ++C)
		{
			bool bVisible = true;
			for (int32 E = 0; E < C; ++E) { bVisible &= Profile ? Profile->ChapterComplete(Char, E) : true; }
			if (bVisible) { Item(C, 3579 + C); }
		}
		break;
	}
	case ESSXFeScreen::VenueSel:
	{
		S.TitleLoc = 3501;
		S.BarH = HBar;
		S.HelpMask = 0x200A;
		S.bSelectedOnly = true;
		S.bWrap = true;
		// 0x163BB8 / 0x379FD0
		const uint32 ModeMask = SSXFeTables::VenueModeMask(GameMode);
		for (int32 T : SSXFeTables::VenueOrder())
		{
			FSSXFeItem I;
			I.Id = T;
			I.Loc = SSXFeTables::TrackNameLoc(T);
			I.bEnabled = Profile ? Profile->IsVenueUnlocked(GameMode, T, bUnlockAll) : (ModeMask & (1u << T)) != 0;
			S.Items.Add(I);
		}
		S.Sel = FMath::Clamp(VenueCamSlot, 0, S.Items.Num() - 1);
		S.bCamSkip = false;   // 0x29BB90
		break;
	}
	case ESSXFeScreen::UserName:
		S.TitleLoc = 3505;
		S.BarH = 300.f;
		S.HelpMask = 0x200A;
		S.bSelectedOnly = true;
		Item(0, 3612);
		S.Name = Profile ? Profile->Slot[Player].UserName : FString();
		// 0x29C370 / FUN_002B0428 / FUN_00275468
		S.GridX = (SSXFeUserName::NumCells - 1) % SSXFeUserName::Cols;
		S.GridY = (SSXFeUserName::NumCells - 1) / SSXFeUserName::Cols;
		S.bListFocused = false;
		S.bNameFresh = true;
		break;
	case ESSXFeScreen::DVDContent:   // 0x293E10
		S.TitleLoc = -1;
		S.bSquare = false;
		S.InFade = 2;
		for (int32 K = 0; K < 9; ++K) { Item(K, SSXFeDvdTables::MainLoc(K)); }
		S.Dvd.Build(Loc);
		break;
	case ESSXFeScreen::Options:
		S.TitleLoc = 3493;
		S.BarH = 450.f;
		S.HelpMask = 0xA;
		S.bSquare = false;
		Item(0, 3608);
		Item(1, 176);
		Item(2, 186);
		Item(3, 204);
		Item(4, 180);
		{
			FSSXFeItem Video;   // PC-only Video page (user request)
			Video.Id = SSXFeOptions::VideoPage;
			Video.Text = SSXFeOptions::PageTitleText(SSXFeOptions::VideoPage);
			S.Items.Add(Video);
		}
		for (int32 I = 0; I < S.Items.Num(); ++I) { S.Items[I].HelpLoc = SSXFeOptions::HelpLoc(-1, I); }   // 0x3481D0
		S.InFade = 0;   // 0x2A8A30
		// FUN_002A8380
		S.Opt.Credits.Reset(300.f);
		break;
	case ESSXFeScreen::MemCard:
		S.BarH = 450.f;
		S.bSquare = false;
		S.bBack = false;
		break;
	case ESSXFeScreen::Video:
		break;
	}
	// FUN_00274208 / FUN_00274000 / FUN_00274278 / FUN_00274108
	switch (Screen)
	{
	case ESSXFeScreen::ModeSelect: case ESSXFeScreen::CharacterSelect: case ESSXFeScreen::UserName:
	case ESSXFeScreen::CharacterAccept: case ESSXFeScreen::CustomBody: case ESSXFeScreen::CustomBoard:
	case ESSXFeScreen::RiderProfile: case ESSXFeScreen::TrickBook: case ESSXFeScreen::VenueSel:
		S.bVertical = false;
		break;
	default:
		S.bVertical = true;
		break;
	}
	S.bWrap = Screen == ESSXFeScreen::CharacterSelect || Screen == ESSXFeScreen::VenueSel;
	return S;
}

void FSSXFrontEnd::Push(FSSXFeState S)
{
	// FUN_00275B10 / 0x29BE58 / FUN_00263000
	if (Stack.Num() && Stack.Last().Screen == ESSXFeScreen::VenueSel) { SetVenueVideo(-1, false); }
	Stack.Add(MoveTemp(S));
	OnOpen(Stack.Last());
	UE_LOG(LogSSX, Verbose, TEXT("[fe] push %s (depth %d)"), LexToString(Stack.Last().Screen), Stack.Num());
}

void FSSXFrontEnd::Pop()
{
	if (Stack.Num() > 1)
	{
		// FUN_00275B90
		if (Stack.Last().Screen == ESSXFeScreen::VenueSel) { SetVenueVideo(-1, false); }
		Stack.Pop();
	}
}

void FSSXFrontEnd::OnOpen(FSSXFeState& S)
{
	// FUN_00275B10
	const FSSXFeState* Prev = Stack.Num() > 1 ? &Stack[Stack.Num() - 2] : nullptr;
	S.OutFade = 0;
	if (S.TitleLoc >= 0)
	{
		Sound(TEXT("whooshB"));   // 0x2747B0
	}
	ShowSounds(S);
	switch (S.Screen)
	{
	case ESSXFeScreen::DVDContent:
		MusicEvent(4);
		Speech(2);   // 0x299270 / FUN_00262508
		break;
	default: break;
	}
	switch (S.Screen)
	{
	case ESSXFeScreen::Title:
		TitleShow();
		TitleAuto(S);
		break;
	case ESSXFeScreen::ModeSelect:
		// 0x29A4E0
		ModeSelectShow(S);
		break;
	case ESSXFeScreen::NumPlayers:
		RequestModeSelectCamera();   // 0x29AB48
		S.OutFade = 1;
		NumPlayersAuto(S);
		break;
	case ESSXFeScreen::UserName:
		UserNameAuto(S);
		break;
	case ESSXFeScreen::CharacterAccept:
		// 0x27A000
		if (ExitFlag == 0x10) { ExitFlag = 2; }
		break;
	case ESSXFeScreen::EventSel:
		// 0x29B2C8
		if (Prev && Prev->Screen == ESSXFeScreen::ModeSelect) { S.InFade = 0; }
		EventSelCamera(S);
		break;
	case ESSXFeScreen::CharacterSelect:
		if (IsAutoAdvance())
		{
			CharacterSelectAuto(S);
			break;
		}
		// 0x278598 / FUN_00265AF8
		Riders.RequestAll(SSXFeRiders::Idle);
		bAnimEventSounds = true;
		SetCarouselWindow(S.Player, CarouselSlot[S.Player]);   // FUN_00261B38
		CharacterRefresh(S);
		// FUN_00267B18 / FUN_00278840 / 0x1635B8
		if (Prev && Prev->Screen == ESSXFeScreen::CharacterModify && Prev->Player < S.Player)
		{
			const FSSXFeItem* I = S.Selected();
			if (Profile && I && I->bEnabled)
			{
				const int32 Char = SSXFeTables::CarouselRider(I->Id);
				int32 Rec[5];
				FMemory::Memcpy(Rec, Profile->RiderConfig[Char], sizeof(Rec));
				LatchRiderGear(S.Player, Char, Rec[4], Rec[3]);
			}
			else
			{
				LatchRiderGear(S.Player);
			}
		}
		// FUN_0026B760
		CamMode = 8;
		Camera.Hold(TEXT("charSelect_select") + SSXFeCams::RiderCamName(SSXFeTables::CarouselRider(CarouselSlot[S.Player])), 0.f);
		break;
	case ESSXFeScreen::CharacterModify:
		CharacterModifyCamera(S);
		break;
	case ESSXFeScreen::CustomBoard:
		CamMode = -1;
		if (Profile) { Camera.PlayFull(TEXT("charSelect_modifyBoard") + SSXFeCams::RiderCamName(Profile->Slot[S.Player].Char)); }
		break;
	case ESSXFeScreen::TrickBook:
		// 0x27E008 / FUN_0027A288
		S.bBookTricks = false;
		S.bListFocused = true;
		TrickBookLoad(S, 0);
		break;
	case ESSXFeScreen::CustomBody:
		CamMode = -1;
		if (Profile) { Camera.PlayFull(TEXT("charSelect_modifyOutfit") + SSXFeCams::RiderCamName(Profile->Slot[S.Player].Char)); }
		break;
	case ESSXFeScreen::VenueSel:
		// 0x29BD88
		S.InFade = Prev && Prev->Screen == ESSXFeScreen::CharacterModify ? 1 : 0;
		VenueScreens(S);
		break;
	case ESSXFeScreen::Options:
		// 0x2A9258 / FUN_0029FB40 / FUN_00262608 / FUN_002A9350
		OptionsSync(S);
		Speech(36);
		MusicEvent(5);
		OptionsDiagram(S);
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::TitleShow()
{
	// 0x293938 / FUN_00262438 / FUN_0026BE38
	RequestTitleCamera();
	MusicEvent(0);
}

void FSSXFrontEnd::ShowSounds(FSSXFeState& S)
{
	switch (S.Screen)
	{
	case ESSXFeScreen::CharacterSelect:
		// 0x278598 / FUN_002621A0 / FUN_00265AF8
		bAnimEventSounds = true;
		if (!IsAutoAdvance())
		{
			Speech(0x10);
			SoundHooks.Add(TEXT("stopAnimVoice"));
			MusicEvent(2);
		}
		break;
	case ESSXFeScreen::CharacterModify:
		// 0x279A80 / FUN_00262260
		Speech(0x1D);
		MusicEvent(2);
		break;
	case ESSXFeScreen::CharacterAccept: case ESSXFeScreen::CustomBoard: case ESSXFeScreen::CustomBody:
	case ESSXFeScreen::TrickBook: case ESSXFeScreen::RiderProfile:
		// 0x27A000 / 0x28D458 / 0x28DBB8 / FUN_00262228
		MusicEvent(2);
		break;
	case ESSXFeScreen::EventSel:
		// 0x29B2C8 / FUN_00262388 / FUN_00262F48
		Speech(7);
		MusicEvent(3);
		break;
	case ESSXFeScreen::DifficultSel:
		// 0x29B8C8 / FUN_00262338
		Speech(0x2B);
		MusicEvent(3);
		break;
	case ESSXFeScreen::VenueSel:
		// 0x29BD88 / FUN_00263000 / FUN_002624B8
		VenueSpeech(S);
		VenueVideoForSelection(S);
		Speech(0x2F);
		MusicEvent(3);
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::VenueSpeech(const FSSXFeState& S)
{
	const int32 T = S.SelectedId();
	static const int32 Ids[9] = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x36, 0x39, 0x38, 0x35 };
	if (T >= 0 && T < 9) { Speech(Ids[T]); }
	else if (T == 11) { Speech(0x37); }
}

void FSSXFrontEnd::AutoAccept(FSSXFeState& S, ESSXFeScreen Next, int32 NextPlayer)
{
	// 0x274B48
	PendingOpen = Next;
	PendingPlayer = NextPlayer;
	bHasPendingOpen = true;
	S.Phase = 5;
}

ESSXFeScreen FSSXFrontEnd::ModeSelectNext(int32 Id)
{
	// 0x29A258 / FUN_00265A58 / FUN_00265870 / FUN_00263670
	if (Id == 0) { return ESSXFeScreen::EventSel; }
	if (Id == 1 && PadsConnected >= 2) { return ESSXFeScreen::NumPlayers; }
	NumPlayers = 1;
	return ESSXFeScreen::CharacterSelect;
}

void FSSXFrontEnd::TitleAuto(FSSXFeState& S)
{
	// 0x293938 / FUN_00265AF8 / 0x2933D0
	if (IsAutoAdvance())
	{
		S.Sel = 0;
		AutoAccept(S, ESSXFeScreen::ModeSelect);
	}
}

void FSSXFrontEnd::ModeSelectShow(FSSXFeState& S)
{
	// 0x29A4E0 / FUN_002623D8 / FUN_00265AF8 / FUN_0029A7A8
	RequestModeSelectCamera();
	const bool bAuto = IsAutoAdvance();
	if (bAuto)
	{
		switch (GameMode)
		{
		case 1: case 6: S.Sel = 0; break;
		case 2: case 3: case 7: S.Sel = 1; break;
		case 4: case 5: S.Sel = 2; break;
		default: break;
		}
	}
	Group = S.SelectedId() == 0 ? 1 : (S.SelectedId() == 2 ? 2 : 0);
	CamMode = Group == 1 ? 4 : (Group == 2 ? 3 : 5);
	SceneEvents.Add(FString::Printf(TEXT("on%sOpen"), GroupName(Group)));
	Speech(ModeSelectSpeechId(Group));   // FUN_002627F0 / FUN_00262840 / FUN_00262890
	if (bAuto)
	{
		AutoAccept(S, ModeSelectNext(S.SelectedId()));
	}
}

void FSSXFrontEnd::NumPlayersAuto(FSSXFeState& S)
{
	// 0x29AB48 / FUN_00265AF8 / FUN_00164288 / 0x29A8C8
	if (!IsAutoAdvance())
	{
		return;
	}
	const int32 Old = S.Sel;
	S.Sel = NumPlayers == 1 ? 0 : 1;
	OnHighlight(S, Old);
	NumPlayers = S.SelectedId() + 1;
	AutoAccept(S, ESSXFeScreen::CharacterSelect, 0);
}

void FSSXFrontEnd::CharacterSelectAuto(FSSXFeState& S)
{
	// 0x278598 / FUN_00265AF8 / FUN_002658E8 / FUN_00164330
	const int32 Char = Profile ? Profile->Slot[S.Player].Char : -1;
	int32 Slot = 0;
	for (int32 K = 0; K < SSXFeRiders::NumRiders; ++K)
	{
		if (SSXFeTables::CarouselRider(K) == Char) { Slot = K; break; }
	}
	S.Sel = Slot;
	S.bCamSkip = false;
	SetCarouselWindow(S.Player, Slot);
	CharacterRefresh(S);
	CamMode = 9;
	Camera.PlayFull(TEXT("charSelect_select") + SSXFeCams::RiderCamName(SSXFeTables::CarouselRider(Slot)));
	Riders.RequestAll(SSXFeRiders::Idle);
	Riders.Request(Slot, SSXFeRiders::Picked);
	bAnimEventSounds = true;
	AutoAccept(S, Group == 2 ? ESSXFeScreen::UserName : ESSXFeScreen::CharacterAccept, S.Player);
}

void FSSXFrontEnd::UserNameAuto(FSSXFeState& S)
{
	// 0x29D2D0 / FUN_00265AF8 / 0x29C2D8
	if (IsAutoAdvance())
	{
		AutoAccept(S, ESSXFeScreen::CharacterAccept, S.Player);
	}
}

void FSSXFrontEnd::RequestTitleCamera()
{
	// FUN_0026BE38 / FUN_0026C370
	if (CamMode == 1)
	{
		return;
	}
	Camera.Hold(TEXT("modeSelectIntro"), 0.f);
	CamMode = 1;
}

void FSSXFrontEnd::CarouselSwingReset()
{
	// 0x29A630 / 0x29AC68 / FUN_00262AD0 / FUN_00263270
	CarouselSlot[0] = CarouselSlot[1] = DefaultCarouselSlot;
	SetCarouselWindow(0, DefaultCarouselSlot);
}

void FSSXFrontEnd::RequestCharSelectCamera()
{
	// TODO: the JP variants 12-14 are used when app+0x58 == 4 (meaning not recovered);
	if (CamMode == 6)
	{
		return;
	}
	CamMode = 6;
	const int32 G = Group == 2 ? 2 : (Group == 0 ? 0 : 1);
	Camera.PlayFull(FString::Printf(TEXT("modeSelect%s_charSelect"), GroupName(G)));
}

void FSSXFrontEnd::RequestModeSelectCamera()
{
	// FUN_0026BE38
	if (CamMode == 2)
	{
		return;
	}
	if (CamMode == 1)
	{
		Camera.PlayFull(TEXT("modeSelectIntro"));
		CamMode = 2;
		return;
	}
	if (Group == 2)
	{
		Camera.Hold(TEXT("modeSelectWC_SE"), 0.f);
		CamMode = 3;
	}
	else if (Group == 0)
	{
		Camera.Hold(TEXT("modeSelectSE_WC"), 0.f);
		CamMode = 5;
	}
	else
	{
		Camera.Hold(TEXT("modeSelectPR_SE"), 0.f);
		CamMode = 4;
	}
}

void FSSXFrontEnd::EventSelCamera(const FSSXFeState& S)
{
	// 0x29B2C8
	const int32 Id = S.SelectedId();
	if (Id == EvTutorial || (Id == EvFreeride && PadsConnected < 2))
	{
		RequestModeSelectCamera();
	}
}

void FSSXFrontEnd::VenueScreens(const FSSXFeState& S)
{
	// FUN_0029BAA0 / 0x3A8AF0 / FUN_00163BB8 / FUN_00269BE8
	for (int32 I = 0; I < 10; ++I)
	{
		const int32 V = SSXFeTables::VenueOrder().IsValidIndex(I) ? SSXFeTables::VenueOrder()[I] : -1;
		const FSSXFeItem* It = S.Items.FindByPredicate([V](const FSSXFeItem& X) { return X.Id == V; });
		VenueScreenFrame[I] = It && It->bEnabled ? 1 : 0;
	}
	++VenueScreenSerial;
}

void FSSXFrontEnd::SetVenueVideo(int32 I, bool bEnabled)
{
	// FUN_00263000 / FUN_00269CC8 / FUN_00269C70
	VenueVideo = I >= 0 && I < 10 && bEnabled ? I : -1;
	++VenueVideoSerial;
}

void FSSXFrontEnd::VenueVideoForSelection(const FSSXFeState& S)
{
	// 0x3A8AF0
	const FSSXFeItem* I = S.Selected();
	SetVenueVideo(I ? S.Sel : -1, I && I->bEnabled);
}

FString SSXFeVenueVideo::FileName(int32 I)
{
	static const TCHAR* const Files[10] = { TEXT("GARI"), TEXT("SNOW"), TEXT("ELYSIUM"), TEXT("MESA"), TEXT("MERQUER"),
		TEXT("MEGAPLE"), TEXT("ALOHA"), TEXT("ALASKA"), TEXT("UNTRACK"), TEXT("PIPE") };   // 0x3471D0
	return I >= 0 && I < 10 ? FString(Files[I]) + TEXT(".SSS") : FString();
}

FString SSXFeVenueVideo::Path(int32 I)
{
	const FString F = FileName(I);
	return F.IsEmpty() ? F : FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("extracted"), TEXT("DATA"), TEXT("VIDEO"), F);
}

bool SSXFeVenueVideo::FVideo::Step(int32 I, int32 Serial)
{
	if (I != Cur || Serial != CurSerial)
	{
		// 0x200098 / 0x1FFF30
		if (Cur >= 0) { Stream.Release(); }
		Stream = SSXPropMovie::FStream();
		if (I != Cur)
		{
			File = SSXPropMovie::FSssFile();
			bFile = I >= 0 && File.Load(Path(I));
		}
		Cur = I;
		CurSerial = Serial;
		if (Cur >= 0 && bFile) { Stream.Acquire(); }
	}
	if (Cur < 0 || !bFile || !Stream.Step(File.NumFrames()))
	{
		return false;
	}
	W = File.FrameWidth(Stream.Shown);
	H = File.FrameHeight(Stream.Shown);
	File.Decode(Stream.Shown, Pixels);
	return true;
}

void FSSXFrontEnd::CharacterModifyCamera(const FSSXFeState& S)
{
	// 0x279A80 / FUN_0026B9F0
	SetCarouselWindow(S.Player, CarouselSlot[S.Player]);   // FUN_00261B38
	CamMode = 10;
	const FString Cam = TEXT("charSelect_select") + SSXFeCams::RiderCamName(SSXFeTables::CarouselRider(CarouselSlot[S.Player]));
	const FSSXFeCamera* C = Camera.CameraSet ? Camera.CameraSet->Find(Cam) : nullptr;
	Camera.Hold(Cam, C ? C->Duration : 1000.f);
}

void FSSXFrontEnd::OnReshow(FSSXFeState& S)
{
	// FUN_00275B90 / 0x274DF0
	S.OpenFrame = 0;
	S.CloseFrame = -1;
	S.Phase = 0;
	S.OutFade = 0;
	if (S.TitleLoc >= 0)
	{
		Sound(TEXT("whooshB"));
	}
	ShowSounds(S);
	switch (S.Screen)
	{
	case ESSXFeScreen::Title:
		TitleShow();
		TitleAuto(S);
		break;
	case ESSXFeScreen::ModeSelect:
		// 0x29A4E0
		ModeSelectShow(S);
		break;
	case ESSXFeScreen::NumPlayers:
		RequestModeSelectCamera();
		S.OutFade = 1;
		NumPlayersAuto(S);
		break;
	case ESSXFeScreen::EventSel:
		EventSelCamera(S);
		break;
	case ESSXFeScreen::CharacterAccept:
		if (ExitFlag == 0x10) { ExitFlag = 2; }   // 0x27A000
		break;
	case ESSXFeScreen::CharacterSelect:
		if (IsAutoAdvance())
		{
			CharacterSelectAuto(S);
			break;
		}
		CamMode = 8;
		Riders.RequestAll(SSXFeRiders::Idle);
		SetCarouselWindow(S.Player, CarouselSlot[S.Player]);   // FUN_00261B38
		CharacterRefresh(S);
		S.bCamSkip = false;
		// FUN_0026B760
		Camera.Hold(TEXT("charSelect_select") + SSXFeCams::RiderCamName(SSXFeTables::CarouselRider(S.Sel)), 0.f);
		break;
	case ESSXFeScreen::VenueSel:
		VenueScreens(S);   // FUN_0029BAA0 / FUN_00263000
		break;
	case ESSXFeScreen::UserName:
		// 0x29D2D0
		S.Name = Profile ? Profile->Slot[S.Player].UserName : FString();
		S.bNameFresh = true;
		UserNameAuto(S);
		break;
	case ESSXFeScreen::CharacterModify:
		CharacterModifyCamera(S);
		break;
	default:
		// 0x29B8C8 / 0x29BD88 / 0x27A000
		break;
	}
}

void FSSXFrontEnd::OnOpened(FSSXFeState& S)
{
	switch (S.Screen)
	{
	case ESSXFeScreen::Title:
		Speech(0);   // 0x293AD8
		break;
	case ESSXFeScreen::ModeSelect:
		// 0x29A618
		SceneEvents.Add(TEXT("onTitleStop"));
		MusicEvent(1);
		Speech(3);
		break;
	case ESSXFeScreen::NumPlayers:
		// 0x29AC50 / FUN_00262CD8
		SceneEvents.Add(TEXT("onTitleStop"));
		MusicEvent(1);
		Speech(3);
		Speech(0xD);
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::CheatInput(int32 Pad, ESSXFeKey K)
{
	// FUN_0026CF60
	if ((Held[Pad] & (PadL1 | PadR1)) != (PadL1 | PadR1))
	{
		CheatBuffer.Reset();
		return;
	}
	uint32 Bit = 0;
	switch (K)
	{
	case ESSXFeKey::Cross: Bit = PadCross; break; case ESSXFeKey::Triangle: Bit = PadTriangle; break;
	case ESSXFeKey::Circle: Bit = PadCircle; break; case ESSXFeKey::Square: Bit = PadSquare; break;
	case ESSXFeKey::Up: Bit = PadUp; break; case ESSXFeKey::Down: Bit = PadDown; break;
	case ESSXFeKey::Left: Bit = PadLeft; break; case ESSXFeKey::Right: Bit = PadRight; break;
	default: return;
	}
	CheatBuffer.Add(Bit);
	const FSSXCheats::ECode C = FSSXCheats::Match(CheatBuffer);
	if (C != FSSXCheats::ECode::None && Cheats)
	{
		Cheats->Apply(C);
		PlayId(3);
		CheatBuffer.Reset();
		if (FSSXFeState* T = TopMut())
		{
			const int32 Sel = T->Sel;
			const int32 Player = T->Player;
			const ESSXFeScreen Scr = T->Screen;
			if (Scr == ESSXFeScreen::CharacterSelect || Scr == ESSXFeScreen::VenueSel || Scr == ESSXFeScreen::CustomBoard)
			{
				*T = Make(Scr, Player);
				T->Sel = Sel;
				T->Phase = 2;
				T->OpenFrame = SlideFrames + 1;
			}
		}
	}
	if (CheatBuffer.Num() > 12)
	{
		CheatBuffer.RemoveAt(0);
	}
}

void FSSXFrontEnd::Key(int32 Pad, ESSXFeKey K)
{
	const bool bMoviePad = Pad == 0 || Pad == 1;
	Pad = FMath::Clamp(Pad, 0, 1);
	IdleFrames = 0;
	if (!MovieRequest.IsEmpty())
	{
		// 0x2BA9F8
		if (bMovieSkippable && bMoviePad && (K == ESSXFeKey::Start || K == ESSXFeKey::Cross))
		{
			PlayId(6);   // 0x2AB918
			MovieFinished();
		}
		return;
	}
	if (Boot != ESSXFeBoot::FrontEnd)
	{
		return;
	}
	CheatInput(Pad, K);
	if ((Held[Pad] & (PadL1 | PadR1)) == (PadL1 | PadR1) && K != ESSXFeKey::L1 && K != ESSXFeKey::R1)
	{
		return;
	}
	FSSXFeState* S = TopMut();
	if (!S)
	{
		return;
	}
	// 0x275148 / FUN_0026CE08 / FUN_0026C430 / FUN_001FF648
	if (Camera.IsRunning())
	{
		if (S->bCamSkip && IsAccept(K))
		{
			Camera.Speed *= 100.f;
			Fader.Start(0.f, 0.f, 0.5f, Black);
		}
		return;
	}
	if (S->Phase != 2 || bLaunchPending || bHasPendingOpen)
	{
		return;
	}
	const bool bPlayerScreen = S->Screen == ESSXFeScreen::CharacterSelect || S->Screen == ESSXFeScreen::CharacterAccept
		|| S->Screen == ESSXFeScreen::CharacterModify || S->Screen == ESSXFeScreen::UserName;
	if (bPlayerScreen && NumPlayers > 1 && Pad != S->Player)
	{
		return;
	}
	if (S->Screen == ESSXFeScreen::MemCard)
	{
		uint32 Bits = 0;
		if (IsAccept(K)) Bits |= FSSXMemCard::InAccept;
		if (IsBack(K)) Bits |= FSSXMemCard::InBack;
		if (K == ESSXFeKey::Circle) Bits |= FSSXMemCard::InCircle;
		if (K == ESSXFeKey::Up) Bits |= FSSXMemCard::InUp;
		if (K == ESSXFeKey::Down) Bits |= FSSXMemCard::InDown;
		if (MemCard.ListFocused())
		{
			// 0x275148 / FUN_00275468 / 0x28F3E0 / 0x2715C8
			if (K == ESSXFeKey::Up) { Sound(MemCard.Selected > 0 ? TEXT("move") : TEXT("refused")); }
			else if (K == ESSXFeKey::Down) { Sound(MemCard.Selected < MemCard.MaxFiles() - 1 ? TEXT("move") : TEXT("refused")); }
			else if (IsAccept(K)) { Sound(TEXT("accept")); }
			else if (K == ESSXFeKey::Circle) { Sound(TEXT("circle")); }
			else if (IsBack(K)) { Sound(TEXT("back")); }
		}
		MemCard.Input(Bits);
		return;
	}
	if (K == ESSXFeKey::Square && S->bSquare)
	{
		S->OutFade = 0;
		BeginClose(*S);
		Push(Make(ESSXFeScreen::Options, Pad));
		return;
	}
	if (S->Screen == ESSXFeScreen::Options)
	{
		OptionsKey(*S, K);
		return;
	}
	if (S->Screen == ESSXFeScreen::UserName)
	{
		UserNameKey(*S, K);
		return;
	}
	if (S->Screen == ESSXFeScreen::TrickBook)
	{
		TrickBookKey(*S, K);
		return;
	}
	if (S->Screen == ESSXFeScreen::DVDContent)
	{
		DvdKey(*S, K);
		return;
	}
	ScreenKey(*S, K);
}

void FSSXFrontEnd::ScreenKey(FSSXFeState& S, ESSXFeKey K)
{
	if (IsAccept(K))
	{
		const FSSXFeItem* I = S.Selected();
		if (I && !I->bEnabled)
		{
			Sound(TEXT("refused"));
			return;
		}
		Sound(TEXT("accept"));
		Accept(S);
		return;
	}
	if (IsBack(K))
	{
		if (S.bBack)
		{
			Sound(TEXT("back"));
			Back(S);
		}
		return;
	}
	if (K == ESSXFeKey::Circle || K == ESSXFeKey::Square)
	{
		const FSSXFeItem* I = S.Selected();
		if (I)
		{
			const bool bOk = I->bEnabled;
			Sound(K == ESSXFeKey::Circle ? (bOk ? TEXT("circle") : TEXT("circleRefused")) : (bOk ? TEXT("square") : TEXT("refused")));
		}
		return;
	}
	const bool bVerticalKey = K == ESSXFeKey::Up || K == ESSXFeKey::Down;
	const bool bHorizontalKey = K == ESSXFeKey::Left || K == ESSXFeKey::Right;
	if ((bVerticalKey && S.bVertical) || (bHorizontalKey && !S.bVertical))
	{
		Move(S, (K == ESSXFeKey::Up || K == ESSXFeKey::Left) ? -1 : 1, bVerticalKey);
	}
}

void FSSXFrontEnd::Move(FSSXFeState& S, int32 Dir, bool bVerticalKey)
{
	const int32 N = S.Items.Num();
	if (N == 0)
	{
		return;
	}
	const int32 Old = S.Sel;
	// FUN_0026E538 / FUN_0026E5C8
	S.Sel = S.bWrap ? (S.Sel + Dir + N) % N : FMath::Clamp(S.Sel + Dir, 0, N - 1);
	if (S.Sel == Old)
	{
		Sound(TEXT("refused"));
		return;
	}
	if (S.Sel != Old)
	{
		S.ArrowFlashAt[Dir > 0 ? 1 : 0] = S.Frame;
		Sound(TEXT("move"));
		OnHighlight(S, Old);
	}
}

void FSSXFrontEnd::OnHighlight(FSSXFeState& S, int32 OldSel)
{
	switch (S.Screen)
	{
	case ESSXFeScreen::ModeSelect:
	{
		const int32 OldG = S.Items[OldSel].Id == 0 ? 1 : (S.Items[OldSel].Id == 2 ? 2 : 0);
		const int32 NewG = S.SelectedId() == 0 ? 1 : (S.SelectedId() == 2 ? 2 : 0);
		Group = NewG;
		CamMode = NewG == 1 ? 4 : (NewG == 2 ? 3 : 5);
		SceneEvents.Add(FString::Printf(TEXT("on%sClose"), GroupName(OldG)));
		SceneEvents.Add(FString::Printf(TEXT("on%sOpen"), GroupName(NewG)));
		Camera.PlayFull(FString::Printf(TEXT("modeSelect%s_%s"), GroupName(OldG), GroupName(NewG)));
		Speech(ModeSelectSpeechId(NewG));   // 0x29A6B0 / FUN_002627F0 / FUN_00262840 / FUN_00262890
		break;
	}
	case ESSXFeScreen::EventSel:
		// 0x29B458
		{
			static const int32 EventSpeech[] = { 0xC, 0xA, 8, 9, 0xB };
			const int32 Id = S.SelectedId();
			if (Id >= 0 && Id < 5) { Speech(EventSpeech[Id]); }
		}
		break;
	case ESSXFeScreen::DifficultSel:
		// 0x29B920 / FUN_002631B0 / FUN_002631F0 / FUN_00263230
		if (S.SelectedId() >= 0 && S.SelectedId() <= 2) { Speech(0x2C + S.SelectedId()); }
		break;
	case ESSXFeScreen::CharacterModify:
		// 0x279CA8 / FUN_00261F20 / FUN_00262020
		{
			static const int32 ModifySpeech[] = { -1, 0x1E, 0x1F, 0x20, 0x22, 0x23 };
			const int32 Id = S.SelectedId();
			if (Id == 0) { StopSpeechRequest(); }
			else if (Id > 0 && Id < 6) { Speech(ModifySpeech[Id]); }
		}
		break;
	case ESSXFeScreen::CharacterSelect:
	{
		const int32 New = S.Sel;
		SetCarouselWindow(S.Player, New);   // 0x2791F0 / FUN_00261B38
		CharacterRefresh(S);
		const bool bRight = New == (OldSel + 1) % SSXFeTables::NumRiders;
		const float Seg = SSXFeCams::CycleFramesPerSlot / SSXFeCams::Fps;
		if (bRight)
		{
			Camera.Play(TEXT("charSelect_cycleRight"), OldSel * Seg, (OldSel + 1) * Seg);
		}
		else
		{
			const int32 J = (SSXFeTables::NumRiders - OldSel) % SSXFeTables::NumRiders;
			Camera.Play(TEXT("charSelect_cycleLeft"), J * Seg, (J + 1) * Seg);
		}
		Sound(TEXT("riderName"));
		S.PanelFrame = 0;
		S.bRankingView = true;
		break;
	}
	case ESSXFeScreen::VenueSel:
	{
		// 0x29BF40 / 0x3A8AF0 / FUN_0026C1A8 / FUN_0026C090
		const int32 Old = OldSel, New = S.Sel;
		const bool bRight = (Old < New && Old == New - 1) || (New == 0 && Old == 9);
		const float Seg = SSXFeCams::VenueFramesPerSlot / SSXFeCams::Fps;
		const int32 K = bRight ? (New + 9) % 10 : (9 - New + 10) % 10;
		Camera.Play(bRight ? TEXT("venueSelect_cycleRight") : TEXT("venueSelect_cycleLeft"), K * Seg, (K + 1) * Seg);
		VenueCamSlot = New;
		VenueSpeech(S);   // FUN_00263000
		VenueVideoForSelection(S);
		break;
	}
	case ESSXFeScreen::CustomBoard:
		if (Profile) { Profile->Slot[S.Player].Board = S.SelectedId(); }
		break;
	case ESSXFeScreen::RiderProfile:
		// 0x28C1B8
		S.PageSel = S.SelectedId();
		S.HelpMask = S.PageSel == 2 ? 0xC : 0x8;
		break;
	case ESSXFeScreen::CustomBody:
		if (Profile) { Profile->Slot[S.Player].Outfit = S.SelectedId(); }  // FUN_00267C48
		break;
	case ESSXFeScreen::TrickBook:
		TrickBookHighlight(S);
		break;
	default:
		break;
	}
}

bool FSSXFrontEnd::TrickBookDone(const FSSXFeState& S, int32 Trick) const
{
	// FUN_00164258
	const int32 Char = Profile ? FMath::Clamp(Profile->Slot[S.Player].Char, 0, 11) : 0;
	return Profile && S.Chapter >= 0 && Profile->Career[Char].TrickDone(S.Chapter * SSXFeTrickBook::TricksPerChapter + Trick);
}

void FSSXFrontEnd::TrickBookLoad(FSSXFeState& S, int32 C)
{
	// FUN_0027A288 / FUN_00275468 / FUN_0019E840 / FUN_0027A4A8
	if (S.Chapter == C)
	{
		return;
	}
	S.Chapter = C;
	S.BookVideo = -1;
	S.BookTrick = 0;
	S.BookPicSprite = 0x7C;
	S.BookPicColor = TrickBookDone(S, 0) ? White : SSXFeTrickBook::IconDisabled;
	S.bBookTricks = false;
	S.bListFocused = true;
}

void FSSXFrontEnd::TrickBookHighlight(FSSXFeState& S)
{
	using namespace SSXFeTrickBook;
	S.BookVideo = -1;   // 0x200098
	if (!S.bBookTricks)
	{
		// FUN_0027A288
		S.HelpMask = ChapterLegend;
		if (S.Selected())
		{
			TrickBookLoad(S, S.SelectedId());
		}
		return;
	}
	// FUN_00154638 / FUN_001551C0 / FUN_0019E840
	const int32 Char = Profile ? FMath::Clamp(Profile->Slot[S.Player].Char, 0, 11) : 0;
	const bool bDone = TrickBookDone(S, S.BookTrick);
	S.BookPicSprite = 0x7C + S.BookTrick;
	S.BookPicColor = bDone ? White : IconDisabled;
	S.BookTrickName = FTable::Get().TrickName(Char, FMath::Max(S.Chapter, 0) * TricksPerChapter + S.BookTrick);
	S.HelpMask = TrickLegend;
}

void FSSXFrontEnd::DvdKey(FSSXFeState& S, ESSXFeKey K)
{
	FSSXFeDvd& V = S.Dvd;
	using EL = FSSXFeDvd::EList;
	if (V.Focus == EL::None)
	{
		return;   // FUN_00275468
	}
	auto Byte = [&V](int32 Default) { return V.Focus == EL::Talent ? 0xFF : Default; };
	if (IsAccept(K))
	{
		const FSSXFeDvd::FItem* I = V.Selected();
		if (!I || V.Phase != 0)
		{
			return;
		}
		if (V.Focus != EL::Main && Byte(5) != 0xFF) { PlayId(Byte(5)); }
		auto Movie = [this](const FString& Name)
		{
			// 0x293C20
			MovieRequest = Name;
			bMovieSkippable = true;
		};
		switch (V.Focus)
		{
		case EL::Main:
			if (I->Id == 4 || I->Id == 7 || I->Id == 8)
			{
				V.Phase = 2;
				V.StartChain(EL::Main, true);
				V.Focus = EL::None;
				V.TitleSlide.Activate(true);
				V.TileFade.Activate(true);
				PlayId(1);
			}
			else
			{
				Movie(SSXFeDvdTables::MainMovie(I->Id));
			}
			break;
		case EL::Voices:
			// FUN_00296870
			V.Phase = 4;
			V.StartChain(EL::Voices, true);
			V.Focus = EL::None;
			V.BuildTalent(I->Id, Loc);
			V.LegendMask = 0xC;
			V.PortraitFade.Activate(true);
			break;
		case EL::Jukebox:
			V.PlaySong(I->Id, SoundHooks);   // FUN_00299338
			break;
		case EL::Talent:
			Movie(FString::Printf(TEXT("CV_%s"), *FString(SSXFeTables::RiderShort(V.Char)).ToUpper()));
			break;
		case EL::Previews:
			Movie(I->Id == 0 ? TEXT("SLED2PR") : TEXT("NBAPROMO"));
			break;
		default:
			break;
		}
		return;
	}
	if (IsBack(K))
	{
		PlayId(6);
		switch (V.Focus)
		{
		case EL::Main:
			Back(S);
			return;
		case EL::Voices:
			V.Phase = 5;
			V.StartChain(EL::Voices, true);
			V.PortraitFade.Activate(true);
			break;
		case EL::Talent:
			V.Phase = 7;
			V.StartChain(EL::Talent, true);
			V.bTalentNameShown = false;
			V.LegendMask = 10;
			break;
		case EL::Jukebox:
			SoundHooks.Add(TEXT("jukeboxStop"));
			V.Phase = 9;
			V.StartChain(EL::Jukebox, true);
			break;
		case EL::Previews:
			V.Phase = 11;
			V.StartChain(EL::Previews, true);
			break;
		default:
			break;
		}
		V.Focus = EL::None;
		return;
	}
	const int32 Dy = K == ESSXFeKey::Up ? -1 : (K == ESSXFeKey::Down ? 1 : 0);
	const int32 Dx = K == ESSXFeKey::Left ? -1 : (K == ESSXFeKey::Right ? 1 : 0);
	if (Dy != 0 || Dx != 0)
	{
		const FString Hook = V.Move(Dy, Dx, SoundHooks, Loc);
		if (Hook == TEXT("move") && Byte(4) != 0xFF) { PlayId(Byte(4)); }
		else if (Hook == TEXT("refused") && Byte(7) != 0xFF) { PlayId(Byte(7)); }
	}
}

void FSSXFrontEnd::TrickBookKey(FSSXFeState& S, ESSXFeKey K)
{
	using namespace SSXFeTrickBook;
	if (K == ESSXFeKey::Square)
	{
		const bool bOk = S.bBookTricks ? TrickBookDone(S, S.BookTrick) : (S.Selected() && S.Selected()->bEnabled);
		Sound(bOk ? TEXT("square") : TEXT("refused"));
		return;
	}
	if (K == ESSXFeKey::Circle)
	{
		// 0x27E7F8 / FUN_001637D0 / FUN_001635B8
		const bool bOk = S.bBookTricks ? TrickBookDone(S, S.BookTrick) : (S.Selected() && S.Selected()->bEnabled);
		Sound(bOk ? TEXT("circle") : TEXT("circleRefused"));
		if (S.Player == 1 && Profile)
		{
			Profile->Slot[0] = Profile->Slot[1];
		}
		NumPlayers = 1;
		StopSpeechRequest();
		GameMode = 6;
		Group = 3;
		Track = SSXFeTables::TrickTutorialTrack;
		BeginLaunch();
		return;
	}
	const bool bVerticalKey = K == ESSXFeKey::Up || K == ESSXFeKey::Down;
	if (!S.bBookTricks)
	{
		if (IsAccept(K))
		{
			const FSSXFeItem* I = S.Selected();
			if (!I || !I->bEnabled)
			{
				Sound(TEXT("refused"));
				return;
			}
			Sound(TEXT("accept"));
			// FUN_0027A288
			TrickBookLoad(S, I->Id);
			S.bBookTricks = true;
			S.bListFocused = false;
			TrickBookHighlight(S);
			return;
		}
		if (IsBack(K))
		{
			Sound(TEXT("back"));
			Back(S);
			return;
		}
		if (K == ESSXFeKey::Left || K == ESSXFeKey::Right)
		{
			Move(S, K == ESSXFeKey::Left ? -1 : 1, bVerticalKey);
		}
		return;
	}
	// the trick list (cFE3DWList style 3, axis 0, no wrap; menu bytes as the chapter list)
	if (IsAccept(K))
	{
		if (!TrickBookDone(S, S.BookTrick))
		{
			Sound(TEXT("refused"));
			return;
		}
		Sound(TEXT("accept"));
		// 0x27E3C0
		if (S.BookVideo < 0)
		{
			S.BookVideo = FMath::Max(S.Chapter, 0) * TricksPerChapter + S.BookTrick;
			S.BookPicSprite = SprVideo;
		}
		return;
	}
	if (IsBack(K))
	{
		Sound(TEXT("back"));
		S.bBookTricks = false;
		S.bListFocused = true;
		S.BookPicSprite = 0x7C + S.BookTrick;
		S.BookTrickName.Reset();
		TrickBookHighlight(S);
		return;
	}
	if (K == ESSXFeKey::Left || K == ESSXFeKey::Right)
	{
		const int32 Dir = K == ESSXFeKey::Left ? -1 : 1;
		const int32 New = FMath::Clamp(S.BookTrick + Dir, 0, TricksPerChapter - 1);
		if (New == S.BookTrick)
		{
			Sound(TEXT("refused"));
			return;
		}
		S.BookTrick = New;
		S.BookArrowFlashAt[Dir > 0 ? 1 : 0] = S.Frame;
		Sound(TEXT("move"));
		TrickBookHighlight(S);
	}
}

void FSSXFrontEnd::Accept(FSSXFeState& S)
{
	const int32 Id = S.SelectedId();
	const int32 P = S.Player;
	switch (S.Screen)
	{
	case ESSXFeScreen::Title:
		// 0x293B80 / 0x2933D0
		S.OutFade = Id == 1 ? 2 : 0;
		Leave(S, Id == 0 ? ESSXFeScreen::ModeSelect : ESSXFeScreen::DVDContent);
		break;
	case ESSXFeScreen::ModeSelect:
		// 0x29A800
		if (Id == 0) { S.OutFade = 0; }
		else if (Id == 1) { S.OutFade = PadsConnected < 2 ? 1 : 0; S.bCamSkip = PadsConnected < 2; }
		else { S.OutFade = 1; S.bCamSkip = true; }
		Group = Id == 0 ? 1 : (Id == 2 ? 2 : 0);
		Leave(S, ModeSelectNext(Id));
		break;
	case ESSXFeScreen::NumPlayers:
		NumPlayers = Id + 1;
		Leave(S, ESSXFeScreen::CharacterSelect, 0);
		break;
	case ESSXFeScreen::EventSel:
		switch (Id)
		{
		case EvFreeride: GameMode = 1; Group = 4; break;
		case EvTutorial: GameMode = 6; Group = 3; Track = SSXFeTables::TrickTutorialTrack; break;
		case EvRace: GameMode = Group == 2 ? 4 : 2; break;
		case EvShowoff: GameMode = Group == 2 ? 5 : 3; break;
		case EvTC: GameMode = 7; break;
		}
		// 0x29B550
		S.OutFade = (Id == EvTutorial || (Id == EvFreeride && PadsConnected < 2)) ? 1 : 0;
		if (Id == EvFreeride) { NumPlayers = 1; Leave(S, PadsConnected >= 2 ? ESSXFeScreen::NumPlayers : ESSXFeScreen::CharacterSelect); }
		else if (Id == EvTutorial) { NumPlayers = 1; Leave(S, ESSXFeScreen::CharacterSelect); }
		else if (Id == EvRace && Group != 2) { Leave(S, ESSXFeScreen::DifficultSel); }
		else { Leave(S, ESSXFeScreen::VenueSel); }
		break;
	case ESSXFeScreen::DifficultSel:
		Difficulty = Id;
		Leave(S, ESSXFeScreen::VenueSel);
		break;
	case ESSXFeScreen::CharacterSelect:
	{
		// 0x279318 / FUN_002661E8 / FUN_0026B8A8
		const int32 Char = SSXFeTables::CarouselRider(Id);
		StopSpeechRequest();
		Riders.Request(Id, SSXFeRiders::Picked);
		AcceptedSlot[P] = Id;
		CarouselSlot[P] = Id;
		if (Profile)
		{
			// 0x278840 / 0x1635B8
			Profile->Slot[P].Char = Char;
			Profile->LoadSlotConfig(P, Char);
		}
		S.bCamSkip = true;
		Camera.PlayFull(TEXT("charSelect_select") + SSXFeCams::RiderCamName(Char));
		Leave(S, Group == 2 ? ESSXFeScreen::UserName : ESSXFeScreen::CharacterAccept, P);
		break;
	}
	case ESSXFeScreen::CharacterAccept:
		// 0x27A1A8 / 0x1637F8 / 0x161A98
		if (Profile && P == NumPlayers - 1)
		{
			Profile->CommitSlotConfigs(NumPlayers);
			Profile->ReloadSettings(Cheats && Cheats->bMaxAttributes);
		}
		Leave(S, ESSXFeScreen::CharacterModify, P);
		break;
	case ESSXFeScreen::CharacterModify:
		// 0x279E18
		S.OutFade = Id == 0 ? 1 : 0;
		switch (Id)
		{
		case 0: AfterModifyContinue(P); break;
		case 1: Leave(S, ESSXFeScreen::CustomBody, P); break;
		case 2: Leave(S, ESSXFeScreen::CustomBoard, P); break;
		case 3: Leave(S, ESSXFeScreen::RiderProfile, P); break;
		case 4: Leave(S, ESSXFeScreen::TrickBook, P); break;
		case 5: Leave(S, ESSXFeScreen::UserName, P); break;
		}
		break;
	case ESSXFeScreen::CustomBoard:
	case ESSXFeScreen::CustomBody:
		// 0x28D5D0 / 0x28DDF8
		if (Profile && CVarFeSaveGearOnAccept.GetValueOnAnyThread() != 0) { Profile->CommitSlotConfigs(NumPlayers); }
		BeginBack(S, true);
		break;
	case ESSXFeScreen::RiderProfile:
		// 0x28C128 / 0x28BE20 / 0x3AFAF8 / 0x331EB0
		if (S.PageSel == 2 && (LegendDisabledMask(S) & 0x4u) == 0 && Profile)
		{
			const int32 Char = FMath::Clamp(Profile->Slot[S.Player].Char, 0, SSXFeTables::NumRiders - 1);
			MovieRequest = FString(TEXT("RR_")) + FString(SSXFeTables::RiderShort(Char)).ToUpper();
			bMovieSkippable = true;
		}
		break;
	case ESSXFeScreen::VenueSel:
		Track = Id;
		SoundHooks.Add(TEXT("stinger1"));
		// 0x29C238 / FUN_0026C2C0 / FUN_00263150 / 0x347180
		SceneEvents.Add(FString::Printf(TEXT("FE%s1"), SSXFeCams::VenueEventName(S.Sel)));
		Camera.PlayFull(FString(TEXT("venueSelect_choose")) + SSXFeCams::VenueChooseName(VenueCamSlot));
		// FUN_001FF648
		Fader.Start(1.f, 10.f, 0.f, Black);
		BeginLaunch();
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::Leave(FSSXFeState& S, ESSXFeScreen Next, int32 NextPlayer)
{
	PendingOpen = Next;
	PendingPlayer = NextPlayer;
	bHasPendingOpen = true;
	BeginClose(S);
}

void FSSXFrontEnd::AfterModifyContinue(int32 Player)
{
	FSSXFeState* S = TopMut();
	if (!S)
	{
		return;
	}
	if (Player + 1 < NumPlayers)
	{
		Leave(*S, ESSXFeScreen::CharacterSelect, Player + 1);
		return;
	}
	if (GameMode == 6 && Group == 3) { Track = SSXFeTables::TrickTutorialTrack; BeginLaunch(); return; }
	Leave(*S, (GameMode == 1 && Group == 4) ? ESSXFeScreen::VenueSel : ESSXFeScreen::EventSel, 0);
}

void FSSXFrontEnd::BeginClose(FSSXFeState& S)
{
	// 0x2749F0 / FUN_001FF648
	Fader.StartKind(S.OutFade, Black);
	S.Phase = 3;
	S.CloseFrame = 0;
	Sound(TEXT("whooshA"));
}

void FSSXFrontEnd::BeginBack(FSSXFeState& S, bool bHook)
{
	// 0x274878
	Fader.StartKind(S.InFade, White);
	if (bHook)
	{
		BackHook(S);
	}
	S.Phase = 1;
	S.CloseFrame = 0;
	Sound(TEXT("whooshA"));
}

void FSSXFrontEnd::OnClosed(FSSXFeState& S)
{
	// 0x274C78
	switch (S.Screen)
	{
	case ESSXFeScreen::Title:
		// 0x293B18 / FUN_00262A68
		if (S.SelectedId() == 0)
		{
			RequestModeSelectCamera();
			SceneEvents.Add(TEXT("onTitleOpen"));
			SoundHooks.Add(TEXT("stinger0"));
		}
		break;
	case ESSXFeScreen::ModeSelect:
		// 0x29A630 / FUN_00261B38
		if (S.SelectedId() == 2 || (S.SelectedId() == 1 && PadsConnected < 2))
		{
			CarouselSwingReset();
			RequestCharSelectCamera();
			SoundHooks.Add(TEXT("stinger1"));
		}
		break;
	case ESSXFeScreen::NumPlayers:
		// 0x29AC68 / FUN_00263270 / FUN_00261B38
		CarouselSwingReset();
		RequestCharSelectCamera();
		SoundHooks.Add(TEXT("stinger1"));
		break;
	case ESSXFeScreen::EventSel:
		// 0x29B3D8 / FUN_00262AD0
		if (S.SelectedId() == EvTutorial || (S.SelectedId() == EvFreeride && PadsConnected < 2))
		{
			CarouselSwingReset();
			RequestCharSelectCamera();
			SoundHooks.Add(TEXT("stinger1"));
		}
		break;
	case ESSXFeScreen::CharacterModify:
		if (S.SelectedId() == 0 && S.Player + 1 >= NumPlayers && !(GameMode == 6 && Group == 3))
		{
			// 0x279C10
			bAnimEventSounds = false;
			if (CamMode != 0xB)   // FUN_0026BE38
			{
				CamMode = 0xB;
				VenueCamSlot = 0;   // FUN_0026BE38
				Camera.PlayFull(TEXT("venueSelect_intro"));
			}
			SoundHooks.Add(TEXT("stinger2"));
			SoundHooks.Add(TEXT("stopAnimVoice"));
		}
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::BackHook(FSSXFeState& S)
{
	switch (S.Screen)
	{
	case ESSXFeScreen::CustomBoard:
	case ESSXFeScreen::CustomBody:
		// 0x28D4C0 / 0x28DCA0
		if (Profile)
		{
			const FString Cam = (S.Screen == ESSXFeScreen::CustomBoard ? TEXT("charSelect_modifyBoard") : TEXT("charSelect_modifyOutfit"))
				+ SSXFeCams::RiderCamName(Profile->Slot[S.Player].Char);
			Camera.PlayFull(Cam, true);
		}
		break;
	case ESSXFeScreen::ModeSelect:
		// 0x29A600 / 0x270790 / FUN_00262960
		StopSpeechRequest();
		SceneEvents.Add(FString::Printf(TEXT("on%sClose"), GroupName(Group)));
		break;
	case ESSXFeScreen::EventSel:
		// 0x29B3A8
		if (Group == 0 || Group == 2) { SoundHooks.Add(TEXT("stopSpeech")); }
		break;
	case ESSXFeScreen::VenueSel:
		// 0x29BEE0
		if (Group == 4) { SoundHooks.Add(TEXT("stopSpeech")); }
		break;
	case ESSXFeScreen::CharacterSelect:
		// 0x2791C8
		if (S.Player == 0) { SoundHooks.Add(TEXT("stopSpeech")); SoundHooks.Add(TEXT("stopAnimVoice")); }
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::RiderGear(int32 Pad, int32& OutBoard, int32& OutOutfit) const
{
	OutBoard = OutOutfit = 0;
	if (!Profile || Pad < 0 || Pad > 1 || Profile->Slot[Pad].Char < 0 || Profile->Slot[Pad].Char >= SSXFeTables::NumRiders)
	{
		return;
	}
	const FSSXPlayerSlot& Sl = Profile->Slot[Pad];
	const int32 Board = FMath::Max(Sl.Board, 0), Outfit = FMath::Max(Sl.Outfit, 0);
	Profile->ShownGear(Sl.Char, Board, Outfit, Cheats && Cheats->bUnlockAll, OutBoard, OutOutfit);
	// 0x28D4D8 / 0x28DCB8 / FUN_00266190
	const FSSXFeState* T = Top();
	if (T && T->Player == Pad && T->Screen == ESSXFeScreen::CustomBoard) { OutBoard = Board; }
	if (T && T->Player == Pad && T->Screen == ESSXFeScreen::CustomBody) { OutOutfit = Outfit; }
}

void FSSXFrontEnd::RefreshRiderGear()
{
	// A save loaded from the memory card screen replaces the riders' records and the pads' gear; the models were latched
	// at the boot load (0x263B30), so they show the loaded gear only from here.
	BuildRiderGear();
}

void FSSXFrontEnd::BuildRiderGear()
{
	// 0x263B30 / FUN_00164288 / FUN_001635B8 / FUN_00265788
	if (Profile && (Profile->Slot[0].Char < 0 || Profile->Slot[0].Char >= SSXFeTables::NumRiders))
	{
		const int32 Char = SSXFeTables::CarouselRider(DefaultCarouselSlot);
		int32 Rec[5];
		FMemory::Memcpy(Rec, Profile->RiderConfig[Char], sizeof(Rec));
		LatchRiderGear(0, Char, Rec[4], Rec[3]);
		return;
	}
	LatchRiderGear(0);
}

void FSSXFrontEnd::LatchRiderGear(int32 Pad, int32 Char, int32 Board, int32 Outfit)
{
	// TODO: FUN_002679A0's outfit change for a pad > 0 whose rider another pad also rides (0x267910).
	if (Char < 0 && Profile && Pad >= 0 && Pad < 2)
	{
		Char = Profile->Slot[Pad].Char;
		Board = Profile->Slot[Pad].Board;
		Outfit = Profile->Slot[Pad].Outfit;
	}
	for (int32 K = 0; K < SSXFeRiders::NumRiders; ++K)
	{
		int32 B = 0, O = 0;
		const int32 KChar = SSXFeTables::CarouselRider(K);
		if (Profile && Char >= 0 && Char < SSXFeTables::NumRiders && KChar == Char)
		{
			Profile->ShownGear(Char, FMath::Max(Board, 0), FMath::Max(Outfit, 0), Cheats && Cheats->bUnlockAll, B, O);
		}
		// PORT DEVIATION (ssx.FE.AllRidersSavedGear): the rider's own saved record (+0x10 board, +0xC outfit), gated
		else if (Profile && KChar >= 0 && KChar < SSXFeTables::NumRiders && CVarFeAllRidersSavedGear.GetValueOnAnyThread() != 0)
		{
			// 0x1637F8
			int32 Rec[5];
			FMemory::Memcpy(Rec, Profile->RiderConfig[KChar], sizeof(Rec));
			Profile->ShownGear(KChar, FMath::Max(Rec[4], 0), FMath::Max(Rec[3], 0), Cheats && Cheats->bUnlockAll, B, O);
		}
		RiderModelBoard[K] = B;
		RiderModelOutfit[K] = O;
	}
}

void FSSXFrontEnd::SyncCustomGear(const FSSXFeState& S)
{
	// FUN_0028D668 / 0x28D4D8 / 0x267C20 / 0x28DD50
	if (!Profile || S.Player < 0 || S.Player > 1) { return; }
	const int32 K = ((CarouselSlot[S.Player] % SSXFeRiders::NumRiders) + SSXFeRiders::NumRiders) % SSXFeRiders::NumRiders;
	if (S.Screen == ESSXFeScreen::CustomBoard) { RiderModelBoard[K] = FMath::Max(Profile->Slot[S.Player].Board, 0); }
	if (S.Screen == ESSXFeScreen::CustomBody) { RiderModelOutfit[K] = FMath::Max(Profile->Slot[S.Player].Outfit, 0); }
}

void FSSXFrontEnd::ModelGear(int32 Slot, int32& OutBoard, int32& OutOutfit) const
{
	OutBoard = OutOutfit = 0;
	if (Slot >= 0 && Slot < SSXFeRiders::NumRiders)
	{
		OutBoard = RiderModelBoard[Slot];
		OutOutfit = RiderModelOutfit[Slot];
	}
}

void FSSXFrontEnd::Back(FSSXFeState& S)
{
	if (S.Screen == ESSXFeScreen::CustomBoard || S.Screen == ESSXFeScreen::CustomBody)
	{
		if (Profile)
		{
			(S.Screen == ESSXFeScreen::CustomBoard ? Profile->Slot[S.Player].Board : Profile->Slot[S.Player].Outfit) = S.SavedValue;
		}
		SyncCustomGear(S);
	}
	BeginBack(S, true);
}

namespace
{
	/** The sound system's preset routine (cFE3DSound vfunc_41 0x270FC0 -> jump table 0x39F0B0): levels of classes 1 music,
	 *  4 sfx, 2 mc/PA speech, 3 character speech (0x211DB0), sound +0x383C (0x212210) = settings Volume3 and the arcade
	 *  flag snd+0x3844 (0x212270: 1 for presets 0..4, 0 for No Sound); the master level is not touched. 0x3F68BA2F = 10/11. */
	void ApplySoundPreset(FSSXSettings& S, int32 Preset)
	{
		constexpr float On = 0.90909094f;
		struct FP { float Music, Sfx, Mc, Char, V3; };
		static const FP T[6] = {
			{ On, On, On, On, On },
			{ On, On, 0.f, 0.f, 0.f },
			{ 0.f, On, On, On, On },
			{ 0.f, On, 0.f, 0.f, 0.f },
			{ On, 0.f, 0.f, 0.f, 0.f },
			{ 0.f, 0.f, 0.f, 0.f, 0.f },
		};
		S.Preset = Preset;
		if (Preset < 0 || Preset >= 6)
		{
			return;
		}
		S.Music = T[Preset].Music;
		S.Sfx = T[Preset].Sfx;
		S.McSpeech = T[Preset].Mc;
		S.CharSpeech = T[Preset].Char;
		S.Volume3 = T[Preset].V3;
		S.ArcadeSfx = Preset == 5 ? 0 : 1;
	}
}

int32 FSSXFrontEnd::OptionsSelectedId(const FSSXFeState& S) const
{
	using namespace SSXFeOptions;
	if (S.Page < 0)
	{
		return S.SelectedId();
	}
	const TArray<FRow>& Rows = PageRows(S.Page);
	return Rows.IsValidIndex(S.Opt.RowSel[S.Page]) ? Rows[S.Opt.RowSel[S.Page]].Id : -1;
}

void FSSXFrontEnd::OptionsSync(FSSXFeState& S)
{
	// FUN_0029FB40 / 0x32C9E8
	FSSXFeOptionsUI& O = S.Opt;
	const FSSXSettings St = Profile ? Profile->Settings : FSSXSettings();
	auto Idx = [](int32 V, int32 N) { return FMath::Clamp(V, 0, N - 1); };
	// FUN_0029EF50
	O.List[0][0] = Idx(St.Speaker, 4);
	O.List[0][1] = Idx(St.Preset, 6);
	O.Bar[0][2] = St.Master;
	O.Bar[0][3] = St.McSpeech;
	O.Bar[0][4] = St.CharSpeech;
	O.Bar[0][5] = St.Music;
	O.Bar[0][6] = St.Sfx;
	O.List[0][7] = St.ArcadeSfx == 0 ? 1 : 0;
	// FUN_0029F430 / 0x32CA08 / 0x32CA0C / 0x32CA1C
	O.List[1][0] = Idx(St.Vibration[0], 2);
	O.List[1][1] = Idx(St.Vibration[1], 2);
	O.List[1][2] = Idx(St.Controller[0], 2);
	O.List[1][3] = Idx(St.Controller[1], 2);
	// FUN_0029F5F0 / FUN_0017B970
	O.List[2][0] = Idx(St.Units, 2);
	O.List[2][1] = Idx(St.Widescreen, 3);
	O.List[2][2] = St.Autoload == 0 ? 1 : 0;
	O.List[2][3] = Idx(St.Replay, 2);
	O.List[2][4] = Idx(St.MaxBoarders - 2, 5);
	O.Bar[2][5] = St.Brightness;
	O.List[SSXFeOptions::VideoPage][0] = int32(SSXRenderLook::Get());
	O.List[SSXFeOptions::VideoPage][1] = SSXRenderLook::GetQuality();
	if (const UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		O.List[SSXFeOptions::VideoPage][2] = SSXFeOptions::ResolutionIndex(G->GetScreenResolution());
		O.List[SSXFeOptions::VideoPage][3] = FMath::Clamp(int32(G->GetFullscreenMode()), 0, 2);
	}
	if (const int32 Fg = SSXFeOptions::RowIndex(SSXFeOptions::VideoPage, SSXFeOptions::FrameGenerationId); Fg != INDEX_NONE)
	{
		O.List[SSXFeOptions::VideoPage][Fg] = SSXRenderLook::GetFrameGeneration() ? 1 : 0;
	}
}

void FSSXFrontEnd::OptionsDiagram(FSSXFeState& S)
{
	// FUN_002A9350
	const int32 Id = OptionsSelectedId(S);
	if (Id < 0 || Id > 3)
	{
		return;
	}
	const int32 CfgRow = (Id == 1 || Id == 3) ? 3 : 2;
	S.Opt.CtrlDiagram = S.Opt.List[1][CfgRow] == 0 ? 0 : 1;
}

void FSSXFrontEnd::OptionsEnterPage(FSSXFeState& S, int32 P)
{
	using namespace SSXFeOptions;
	if (P < 0 || P >= NumPages)
	{
		return;
	}
	FSSXFeOptionsUI& O = S.Opt;
	// 0x2AA1F8 / FUN_00275468 / 0x3B4290
	S.Page = P;
	auto Snapshot = [&O](int32 Pg)
	{
		FMemory::Memcpy(O.SnapList[Pg], O.List[Pg], sizeof(O.List[Pg]));
		FMemory::Memcpy(O.SnapBar[Pg], O.Bar[Pg], sizeof(O.Bar[Pg]));
	};
	switch (P)
	{
	case 0:
		Speech(38);   // FUN_002627B0
		Snapshot(0);                       // FUN_0029E988
		break;
	case 1:
		Speech(39);   // FUN_00262670
		Snapshot(1);                       // FUN_0029EBC0
		if (OptionsSelectedId(S) == DoneId)
		{
			O.CtrlDiagram = O.List[1][2] == 0 ? 0 : 1;   // FUN_002A9350
		}
		else
		{
			OptionsDiagram(S);
		}
		break;
	case 2:
		Speech(37);   // FUN_002626F0
		Snapshot(2);                       // FUN_0029ED50
		break;
	case 3:
		SoundHooks.Add(TEXT("stopSpeech")); // FUN_00262770
		break;
	case 4:
		O.bCreditsActive = true;
		Speech(41);   // FUN_002626B0
		break;
	case VideoPage:
		Snapshot(VideoPage);
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::OptionsCommit(FSSXFeState& S, int32 P)
{
	if (P == SSXFeOptions::VideoPage)
	{
		SSXRenderLook::Set(ESSXRenderLook(S.Opt.List[P][0]));
		SSXRenderLook::SetQuality(S.Opt.List[P][1]);
		if (const int32 Fg = SSXFeOptions::RowIndex(P, SSXFeOptions::FrameGenerationId); Fg != INDEX_NONE)
		{
			SSXRenderLook::SetFrameGeneration(S.Opt.List[P][Fg] != 0);
		}
		SoundHooks.Add(TEXT("saveVideo"));
		UGameUserSettings* G = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		const TArray<FIntPoint>& Res = SSXFeOptions::Resolutions();
		if (G && Res.IsValidIndex(S.Opt.List[P][2]) && FApp::CanEverRender() && !GIsAutomationTesting)
		{
			const FIntPoint R = Res[S.Opt.List[P][2]];
			const EWindowMode::Type Mode = EWindowMode::ConvertIntToWindowMode(FMath::Clamp(S.Opt.List[P][3], 0, 2));
			if (G->GetScreenResolution() != R || G->GetFullscreenMode() != Mode)
			{
				G->SetScreenResolution(R);
				G->SetFullscreenMode(Mode);
				G->ApplyResolutionSettings(false);
				G->SaveSettings();
			}
		}
		return;
	}
	if (!Profile)
	{
		return;
	}
	FSSXSettings& St = Profile->Settings;
	const FSSXFeOptionsUI& O = S.Opt;
	switch (P)
	{
	case 0:
		// FUN_0029F178 / 0x32CA00 / 0x32C9E8 / 0x32CA04
		St.Speaker = O.List[0][0];
		ApplySoundPreset(St, O.List[0][1]);
		St.Master = O.Bar[0][2];
		St.McSpeech = O.Bar[0][3];
		St.CharSpeech = O.Bar[0][4];
		St.Music = O.Bar[0][5];
		St.Sfx = O.Bar[0][6];
		St.ArcadeSfx = O.List[0][7] == 0 ? 1 : 0;
		break;
	case 1:
		// FUN_0029F508
		St.Vibration[0] = O.List[1][0] == 1 ? 1 : 0;
		St.Vibration[1] = O.List[1][1] == 1 ? 1 : 0;
		St.Controller[0] = O.List[1][2] == 1 ? 1 : 0;
		St.Controller[1] = O.List[1][3] == 1 ? 1 : 0;
		break;
	case 2:
		// FUN_0029F720 / 0x32CA10 / 0x32CA18 / FUN_0017B980
		St.Units = O.List[2][0] == 1 ? 1 : 0;
		St.Widescreen = O.List[2][1];
		St.SplitScreen = 0;
		St.Autoload = O.List[2][2] == 0 ? 1 : 0;
		St.Replay = O.List[2][3] == 1 ? 1 : 0;
		St.MaxBoarders = O.List[2][4] + 2;
		St.Brightness = O.Bar[2][5];
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::OptionsLeavePage(FSSXFeState& S)
{
	// 0x2AA5C0
	S.Page = -1;
}

void FSSXFrontEnd::OptionsApplyList(FSSXFeState& S, int32 Row)
{
	using namespace SSXFeOptions;
	const int32 P = S.Page;
	const TArray<FRow>& Rows = PageRows(P);
	if (P == VideoPage && Rows.IsValidIndex(Row))
	{
		if (Rows[Row].Id == 0) { SSXRenderLook::Set(ESSXRenderLook(S.Opt.List[P][Row])); }
		if (Rows[Row].Id == 1) { SSXRenderLook::SetQuality(S.Opt.List[P][Row]); }
		return;
	}
	if (!Rows.IsValidIndex(Row) || !Profile)
	{
		return;
	}
	FSSXFeOptionsUI& O = S.Opt;
	FSSXSettings& St = Profile->Settings;
	const int32 Index = O.List[P][Row];
	const int32 Id = Rows[Row].Values.IsValidIndex(Index) ? Rows[Row].Values[Index].Id : -1;
	switch (P)
	{
	case 0:
		// FUN_002A9720
		if (Id >= 8 && Id <= 0xB)
		{
			St.Speaker = Index;
			PlayId(0x10);
		}
		else if (Id >= 0xC && Id <= 0x11)
		{
			ApplySoundPreset(St, Index);
			OptionsSync(S);                // FUN_0029EF50
			PlayId(0x11);
		}
		else if (Id == 0x12 || Id == 0x13)
		{
			St.ArcadeSfx = Id == 0x12 ? 1 : 0;
			PlayId(0x17);
		}
		break;
	case 1:
		// FUN_002A9848 / FUN_0026D1C0 / FUN_0026D1C8
		if (Id == 5) { SoundHooks.Add(FString::Printf(TEXT("vibrate%d"), Rows[Row].Id == 0 ? 0 : 1)); }
		else if (Id == 6) { O.CtrlDiagram = 0; }
		else if (Id == 7) { O.CtrlDiagram = 1; }
		break;
	case 2:
		// FUN_002A99A0 / 0x32CA10 / FUN_0029F720 / FUN_0029F5F0
		switch (Id)
		{
		case 7: St.Units = 1; break;
		case 8: St.Units = 0; break;
		case 9: case 10: case 11: St.Widescreen = Id - 9; break;
		case 0xC: St.Autoload = 1; break;
		case 0xD: St.Autoload = 0; break;
		case 0xE: St.Replay = 0; break;
		case 0xF: St.Replay = 1; break;
		case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: St.MaxBoarders = Id - 0xE; break;
		default: break;
		}
		break;
	default:
		break;
	}
}

void FSSXFrontEnd::OptionsApplyBar(FSSXFeState& S, int32 Row)
{
	if (!Profile)
	{
		return;
	}
	FSSXSettings& St = Profile->Settings;
	const float V = S.Opt.Bar[S.Page][Row];
	// 0x2AA098 / FUN_0017B980
	if (S.Page == 0)
	{
		switch (Row)
		{
		case 2: St.Master = V; PlayId(0x12); break;
		case 3: St.McSpeech = V; PlayId(0x13); break;
		case 4: St.CharSpeech = V; PlayId(0x15); break;
		case 5: St.Music = V; break;
		case 6: St.Sfx = V; PlayId(0x16); break;
		default: break;
		}
	}
	else if (S.Page == 2 && Row == 5)
	{
		St.Brightness = V;
	}
}

void FSSXFrontEnd::OptionsKey(FSSXFeState& S, ESSXFeKey K)
{
	using namespace SSXFeOptions;
	FSSXFeOptionsUI& O = S.Opt;
	const bool bUpDown = K == ESSXFeKey::Up || K == ESSXFeKey::Down;
	const bool bLeftRight = K == ESSXFeKey::Left || K == ESSXFeKey::Right;
	if (K == ESSXFeKey::Circle || K == ESSXFeKey::Square)
	{
		// FUN_0029FB70 / FUN_002A36E8 / FUN_002A53D0 / FUN_002A7FD8
		if (S.Page >= 0 && (K == ESSXFeKey::Circle || S.Page >= 3))
		{
			PlayId(5);
		}
		return;
	}
	if (S.Page < 0)
	{
		// FUN_00274278
		if (IsAccept(K))
		{
			Sound(TEXT("accept"));
			OptionsEnterPage(S, S.SelectedId());
		}
		else if (IsBack(K))
		{
			Sound(TEXT("back"));
			BeginBack(S, true);
		}
		else if (bUpDown)
		{
			Move(S, K == ESSXFeKey::Up ? -1 : 1, true);
		}
		return;
	}
	const int32 P = S.Page;
	if (P == 4)
	{
		if (bUpDown)
		{
			O.Credits.Key(K == ESSXFeKey::Up);
		}
		else if (IsBack(K))
		{
			Sound(TEXT("back"));
			O.bCreditsActive = false;
			OptionsLeavePage(S);
			Speech(36);
		}
		return;
	}
	const TArray<FRow>& Rows = PageRows(P);
	if (Rows.Num() == 0)
	{
		return;
	}
	int32& Sel = O.RowSel[P];
	Sel = FMath::Clamp(Sel, 0, Rows.Num() - 1);
	const FRow& R = Rows[Sel];
	const bool bRight = K == ESSXFeKey::Right;
	if (R.Kind == EKind::List && bLeftRight)
	{
		const int32 N = R.Values.Num();
		int32& V = O.List[P][Sel];
		V = (V + (bRight ? 1 : N - 1)) % N;
		O.ArrowFlashFrame[bRight ? 1 : 0] = S.Frame;
		Sound(TEXT("move"));
		OptionsApplyList(S, Sel);
		return;
	}
	if (R.Kind == EKind::Bar && bLeftRight)
	{
		// FUN_002AE760 / FUN_002AE7F0
		float& V = O.Bar[P][Sel];
		if (bRight) { V += R.BarStep; if (1.f < V) { V = 1.f; } }
		else { V -= R.BarStep; if (V < 0.f) { V = 0.f; } }
		O.ArrowFlashFrame[bRight ? 1 : 0] = S.Frame;
		OptionsApplyBar(S, Sel);
		return;
	}
	if (IsAccept(K) && (R.Kind == EKind::List || R.Kind == EKind::Bar))
	{
		return;
	}
	if (IsBack(K))
	{
		Sound(TEXT("back"));
		if (P <= 2 || P == VideoPage)
		{
			FMemory::Memcpy(O.List[P], O.SnapList[P], sizeof(O.List[P]));
			FMemory::Memcpy(O.Bar[P], O.SnapBar[P], sizeof(O.Bar[P]));
			OptionsCommit(S, P);
		}
		OptionsLeavePage(S);
		Speech(36);
		return;
	}
	if (bUpDown)
	{
		Sel = (Sel + (K == ESSXFeKey::Up ? -1 : 1) + Rows.Num()) % Rows.Num();
		Sound(TEXT("move"));
		if (P == 1)
		{
			OptionsDiagram(S);
		}
		return;
	}
	if (IsAccept(K))
	{
		Sound(TEXT("accept"));
		if (P == 3)
		{
			MemCard.Begin(R.Id == 2 ? FSSXMemCard::EMode::Replay : FSSXMemCard::EMode::Game, R.Id == 0, Profile);
			Push(Make(ESSXFeScreen::MemCard, S.Player));
			return;
		}
		if (R.Id == DoneId)
		{
			OptionsCommit(S, P);
			OptionsLeavePage(S);
		}
	}
}

void FSSXFrontEnd::UserNameKey(FSSXFeState& S, ESSXFeKey K)
{
	using namespace SSXFeUserName;
	// 0x2B0850
	int32 Idx = S.GridY * Cols + S.GridX;
	auto SetIdx = [&S](int32 I) { S.GridX = I % Cols; S.GridY = I / Cols; };
	switch (K)
	{
	case ESSXFeKey::Left: SetIdx(StepLeft(Idx)); PlayId(12); return;
	case ESSXFeKey::Right: SetIdx(StepRight(Idx)); PlayId(12); return;
	case ESSXFeKey::Up: SetIdx(StepUp(Idx)); PlayId(12); return;
	case ESSXFeKey::Down: SetIdx(StepDown(Idx)); PlayId(12); return;
	default: break;
	}
	if (IsBack(K))
	{
		// 0x29D3E8
		S.Name = Profile ? Profile->Slot[S.Player].UserName : FString();
		BeginBack(S, true);
		PlayId(13);
		return;
	}
	if (!IsAccept(K))
	{
		return;
	}
	// 0x29D4C8
	const TCHAR C = Cell(S.GridX, S.GridY);
	if (C == TEXT('d'))
	{
		S.Name.LeftChopInline(1);
		S.bNameFresh = false;
	}
	else if (C == TEXT('e'))
	{
		// FUN_00164968
		if (Profile) { Profile->Slot[S.Player].UserName = S.Name; }
		const int32 P = S.Player;
		const bool bFromModify = Stack.Num() >= 2 && Stack[Stack.Num() - 2].Screen == ESSXFeScreen::CharacterModify;
		if (bFromModify) { BeginBack(S, true); }
		else { Leave(S, ESSXFeScreen::CharacterAccept, P); }
	}
	else
	{
		const int32 Len = S.bNameFresh ? 0 : S.Name.Len();
		S.bNameFresh = false;
		if (Len < MaxChars)
		{
			S.Name = S.Name.Left(Len);
			S.Name.AppendChar(C);
		}
	}
	PlayId(13);
}

void FSSXFrontEnd::BeginLaunch()
{
	bLaunchPending = true;
	if (FSSXFeState* S = TopMut())
	{
		BeginClose(*S);
	}
}

void FSSXFrontEnd::LaunchFailed()
{
	bLaunchPending = false;
	bLaunchRequested = false;
	Boot = ESSXFeBoot::FrontEnd;
	BootFrame = 0;
	if (FSSXFeState* S = TopMut())
	{
		S->Phase = 2;
		S->CloseFrame = -1;
	}
}

void FSSXFrontEnd::RequestLaunch()
{
	Launch.GameMode = GameMode;
	Launch.Track = Track;
	Launch.Difficulty = Difficulty;
	Launch.Players = NumPlayers;
	// 0x2657A8 / 0x163980 / 0x1637F8
	if (Profile && !bReplayLaunch) { Profile->CommitSlotConfigs(NumPlayers); }
	for (int32 P = 0; P < 2; ++P) { Launch.Slots[P] = Profile ? Profile->Slot[P] : FSSXPlayerSlot(); }
	if (bReplayLaunch)
	{
		// TODO: )
		for (int32 P = 0; P < 2; ++P)
		{
			const int32 R = ReplayLaunchSetup.RacerOfHuman(P);
			if (R < 0) continue;
			const SSXReplayFile::FRacer& H = ReplayLaunchSetup.Racers[R];
			const SSXRider::FBuiltProfile B = SSXReplayFile::RacerProfile(H);
			Launch.Slots[P].Char = H.Char38;
			Launch.Slots[P].Board = B.Board;
			Launch.Slots[P].Outfit = 0;
			Launch.Slots[P].UserName = H.NameString();
		}
		Launch.Racers = ReplayLaunchSetup.NumRacers;
		bReplayLaunch = false;
	}
	bLaunchRequested = true;
	UE_LOG(LogSSX, Log, TEXT("[fe] launch: mode %d track %d difficulty %d players %d rider %d board %d outfit %d"),
		GameMode, Track, Difficulty, NumPlayers, Launch.Slots[0].Char, Launch.Slots[0].Board, Launch.Slots[0].Outfit);
}

int32 FSSXFrontEnd::ViewSlot() const
{
	const FSSXFeState* S = Top();
	const int32 P = S ? S->Player : 0;
	return CarouselSlot[P];
}

void FSSXFrontEnd::SetCarouselWindow(int32 Pad, int32 Slot)
{
	// FUN_00261B38 / FUN_002670E0 / FUN_00269BE8
	Pad = FMath::Clamp(Pad, 0, 1);
	Slot = ((Slot % SSXFeRiders::NumRiders) + SSXFeRiders::NumRiders) % SSXFeRiders::NumRiders;
	const int32 Old = CarouselSlot[Pad];
	if (Old >= 0 && Old < SSXFeRiders::NumRiders) { CarouselSignFrame[Old] = 0; }
	CarouselSignFrame[Slot] = 1;
	for (int32 K = 0; K < SSXFeRiders::NumRiders; ++K) { CarouselPlatformFrame[K] = K == Slot ? 1 : 0; }
	Riders.Select(Slot);
	CarouselSlot[Pad] = Slot;
	CarouselWindow = Slot;
	++CarouselSerial;
}

void FSSXFrontEnd::CharacterRefresh(const FSSXFeState& S)
{
	// FUN_00278840 / FUN_001635B8 / 0x16BBA0
	const FSSXFeItem* I = S.Selected();
	if (Profile && I && I->bEnabled) { Profile->Session.ClearCircuitContext(); }
}

bool FSSXFrontEnd::IsCarouselRiderDrawn(int32 Slot) const
{
	return CarouselWindow == INDEX_NONE || SSXFeRiders::IsSlotDrawn(Slot, CarouselWindow);
}

void FSSXFrontEnd::GoTo(ESSXFeScreen Target, int32 Player)
{
	Boot = ESSXFeBoot::FrontEnd;
	MovieRequest.Reset();
	Stack.Reset();
	bHasPendingOpen = false;
	bLaunchPending = false;
	CamMode = 0;
	Push(Make(ESSXFeScreen::Title));
	auto PushS = [this](ESSXFeScreen S, int32 P = 0) { Push(Make(S, P)); };
	switch (Target)
	{
	case ESSXFeScreen::Title: break;
	case ESSXFeScreen::ModeSelect: PushS(Target); break;
	case ESSXFeScreen::DVDContent: PushS(Target); break;
	case ESSXFeScreen::NumPlayers: Group = 0; PushS(ESSXFeScreen::ModeSelect); PushS(Target); break;
	case ESSXFeScreen::CharacterSelect: case ESSXFeScreen::CharacterAccept: case ESSXFeScreen::CharacterModify:
	case ESSXFeScreen::CustomBoard: case ESSXFeScreen::CustomBody: case ESSXFeScreen::RiderProfile: case ESSXFeScreen::TrickBook:
	{
		Group = 0;
		PushS(ESSXFeScreen::ModeSelect);
		PushS(ESSXFeScreen::CharacterSelect, Player);
		if (Profile && Profile->Slot[Player].Char < 0) { Profile->Slot[Player].Char = SSXFeTables::CarouselRider(CarouselSlot[Player]); }
		if (Target != ESSXFeScreen::CharacterSelect)
		{
			Camera.PlayFull(TEXT("charSelect_select") + SSXFeCams::RiderCamName(Profile ? Profile->Slot[Player].Char : 3));
			Camera.Time = 1000.f;
			PushS(ESSXFeScreen::CharacterAccept, Player);
		}
		if (Target != ESSXFeScreen::CharacterSelect && Target != ESSXFeScreen::CharacterAccept) { PushS(ESSXFeScreen::CharacterModify, Player); }
		if (Target == ESSXFeScreen::CustomBoard || Target == ESSXFeScreen::CustomBody || Target == ESSXFeScreen::RiderProfile || Target == ESSXFeScreen::TrickBook)
		{
			PushS(Target, Player);
		}
		break;
	}
	case ESSXFeScreen::UserName:
		Group = 2;
		PushS(ESSXFeScreen::ModeSelect);
		PushS(ESSXFeScreen::CharacterSelect);
		PushS(Target);
		break;
	case ESSXFeScreen::EventSel: case ESSXFeScreen::DifficultSel: case ESSXFeScreen::VenueSel:
		Group = 0;
		GameMode = 2;
		PushS(ESSXFeScreen::ModeSelect);
		PushS(ESSXFeScreen::CharacterSelect);
		if (Profile && Profile->Slot[0].Char < 0) { Profile->Slot[0].Char = SSXFeTables::CarouselRider(CarouselSlot[0]); }
		Camera.PlayFull(TEXT("charSelect_select") + SSXFeCams::RiderCamName(Profile ? Profile->Slot[0].Char : 3));
		Camera.Time = 1000.f;
		PushS(ESSXFeScreen::CharacterAccept);
		PushS(ESSXFeScreen::CharacterModify);
		CamMode = 0xB;
		Camera.PlayFull(TEXT("venueSelect_intro"));
		Camera.Time = Camera.End;
		PushS(ESSXFeScreen::EventSel);
		if (Target != ESSXFeScreen::EventSel) { PushS(ESSXFeScreen::DifficultSel); }
		if (Target == ESSXFeScreen::VenueSel) { PushS(ESSXFeScreen::VenueSel); }
		break;
	case ESSXFeScreen::Options:
		PushS(Target);
		break;
	default:
		PushS(Target, Player);
		break;
	}
}

void FSSXFrontEnd::Tick()
{
	++Frame;
	Camera.Tick(1.f / FrameRate);
	Fader.Tick();   // 0x265398 / FUN_001FECE8
	switch (Boot)
	{
	case ESSXFeBoot::StartScreen:
	{
		// 0x1845E0 / 0x32CA34 / 0x1850E8
		++BootFrame;
		bool bDone = false;
		switch (BootState)
		{
		case 0: BootState = 1; BootTimer = 75; break;
		case 1:
			if (BootTimer > 0) { --BootTimer; }
			if (BootTimer == 0) { BootState = 3; }
			break;
		case 3:
			BootState = FPaths::FileExists(FSSXMemCard::FilePath(FSSXMemCard::SettingsName())) ? 5 : 9;
			if (BootState == 9) { BootTimer = 75; }
			break;
		case 5: BootState = 6; break;
		case 6:
		{
			TArray<uint8> Blob;
			bStartScreenLoaded = Profile && FFileHelper::LoadFileToArray(Blob, *FSSXMemCard::FilePath(FSSXMemCard::SettingsName())) && Profile->BootLoadBlob(Blob);
			BootState = 7;
			break;
		}
		case 7: BootState = 8; BootTimer = 120; break;
		case 8:
			if (BootTimer > 0) { --BootTimer; }
			if (BootTimer == 0) { BootState = 9; BootTimer = 75; }
			break;
		default:
			if (BootTimer > 0) { --BootTimer; }
			bDone = BootTimer == 0;
			break;
		}
		if (bDone)
		{
			// TODO: the FirstLoad overlay is not described;
			Boot = ESSXFeBoot::EaLogo;
			BuildRiderGear();
			// 0x2637A0
			if (ExitFlag == 1) { ExitFlag = 2; }
			BootFrame = 0;
			MovieRequest = TEXT("EABIG");
			bMovieSkippable = false;
		}
		return;
	}
	case ESSXFeBoot::Loading:
		// FUN_002657A8
		if (++BootFrame == 2)
		{
			RequestLaunch();
		}
		return;
	case ESSXFeBoot::FrontEnd:
		break;
	default:
		return;
	}
	FSSXFeState* S = TopMut();
	if (!S)
	{
		return;
	}
	SyncCustomGear(*S);
	++S->Frame;
	++S->OpenFrame;
	if (S->CloseFrame >= 0)
	{
		++S->CloseFrame;
	}
	// 0x274C78
	switch (S->Phase)
	{
	case 0:
		if (S->IsSlidIn())
		{
			S->Phase = 2;
			OnOpened(*S);
		}
		break;
	case 1:
		if (S->IsSlidOut())
		{
			const int32 Before = Stack.Num();
			const ESSXFeScreen PoppedScreen = S->Screen;
			const int32 PoppedPlayer = S->Player;
			Pop();
			if (FSSXFeState* T = TopMut(); T && Stack.Num() < Before)
			{
				// 0x279A80 / FUN_00267B18
				if (T->Screen == ESSXFeScreen::CharacterModify && PoppedScreen == ESSXFeScreen::CharacterSelect && PoppedPlayer != T->Player)
				{
					LatchRiderGear(T->Player);
				}
				OnReshow(*T);
			}
		}
		return;
	case 3:
		if (S->IsSlidOut())
		{
			OnClosed(*S);
			S->Phase = 4;
		}
		return;
	case 5:
		// FUN_00275B10
		if (bHasPendingOpen)
		{
			bHasPendingOpen = false;
			Push(Make(PendingOpen, PendingPlayer));
			return;
		}
		break;
	case 4:
		if (Camera.IsRunning())
		{
			return;
		}
		if (bLaunchPending)
		{
			bLaunchPending = false;
			Boot = ESSXFeBoot::Loading;
			BootFrame = 0;
			do { HintIndex = int32(FGameRng::StreamA().RandInt(19)); } while (HintIndex == 17);
		}
		else if (bHasPendingOpen)
		{
			bHasPendingOpen = false;
			S->Phase = 5;
			Push(Make(PendingOpen, PendingPlayer));
		}
		return;
	default:
		break;
	}
	switch (S->Screen)
	{
	case ESSXFeScreen::Title:
		// 0x265398
		if (++IdleFrames >= AttractFrames)
		{
			IdleFrames = 0;
			Boot = ESSXFeBoot::Attract;
			MovieRequest = TEXT("SSXINTRO");
			bMovieSkippable = true;
		}
		break;
	case ESSXFeScreen::CharacterSelect:
		++S->PanelFrame;
		if (S->PanelFrame >= (S->bRankingView ? RankingFrames : AttributesFrames))
		{
			S->PanelFrame = 0;
			S->bRankingView = !S->bRankingView;
		}
		break;
	case ESSXFeScreen::DVDContent:
		S->Dvd.bSongEnded = bJukeboxSongEnded;
		S->Dvd.Tick(SoundHooks, Loc);   // 0x2986C0
		break;
	case ESSXFeScreen::RiderProfile:
		if (S->PageSel == 1 || S->PageSel == 4)
		{
			S->ProfileText[S->PageSel == 1 ? 0 : 1].Tick();
		}
		break;
	case ESSXFeScreen::Options:
		if (S->Opt.bCreditsActive)
		{
			S->Opt.Credits.Tick();
		}
		break;
	case ESSXFeScreen::MemCard:
		MemCard.Tick();
		if (!MemCard.IsActive() && MemCard.bLoadedReplay && SSXReplayFile::ReadSetup(MemCard.LoadedReplay, ReplayLaunchSetup))
		{
			// 0x2912E8 / 0x166870 / 0x2917B0 / 0x167060
			Pop();
			PendingReplay = MoveTemp(MemCard.LoadedReplay);
			MemCard.bLoadedReplay = false;
			bReplayLaunch = true;
			GameMode = ReplayLaunchSetup.GameMode;
			Track = ReplayLaunchSetup.Venue;
			Difficulty = ReplayLaunchSetup.Difficulty;
			NumPlayers = FMath::Max(1, ReplayLaunchSetup.NumHumans());
			BeginLaunch();
			break;
		}
		if (!MemCard.IsActive())
		{
			// 0x2917B0 / 0x2912E8 / 0x161A98
			if (MemCard.bLoadedSettings)
			{
				MemCard.bLoadedSettings = false;
				RefreshRiderGear();
			}
			Pop();
			if (FSSXFeState* T = TopMut(); T && T->Screen == ESSXFeScreen::Options)
			{
				// FUN_0029FB40 / FUN_002A9350
				OptionsSync(*T);
				OptionsDiagram(*T);
			}
		}
		break;
	default:
		break;
	}
}

float FSSXFeState::SlideT() const
{
	// FUN_002742E8
	if (CloseFrame >= 0)
	{
		return 1.f - FMath::Clamp(CloseFrame / (float)SlideFrames, 0.f, 1.f);
	}
	return FMath::Clamp(OpenFrame / (float)SlideFrames, 0.f, 1.f);
}

bool FSSXFeState::IsSlidOut() const
{
	// 0x2AD380
	return CloseFrame > (HasGroups() ? SlideFrames : 0);
}

bool FSSXFeState::IsSlidIn() const
{
	return OpenFrame > (HasGroups() ? SlideFrames : 0);
}

bool FSSXFeFader::Start(float InUp, float InHold, float InDown, const FLinearColor& InColor)
{
	// FUN_001FF648
	if (bActive)
	{
		return false;
	}
	bActive = true;
	Stage = 0;
	T = 0.f;
	Up = InUp;
	Hold = InHold;
	Down = InDown;
	Alpha = 0.f;
	Color = InColor;
	return true;
}

bool FSSXFeFader::StartKind(int32 Kind, const FLinearColor& InColor)
{
	if (Kind == 1) { return Start(0.5f, 0.f, 0.5f, InColor); }
	if (Kind == 2) { return Start(0.f, 0.f, 1.5f, InColor); }
	return false;
}

void FSSXFeFader::Tick()
{
	// FUN_001FECE8
	if (!bActive)
	{
		return;
	}
	T += 1.f / 60.f;
	switch (Stage)
	{
	case 0:
		if (Up <= T) { Stage = 1; T -= Up; Alpha = 1.f; }
		else { Alpha = T / Up; }
		break;
	case 1:
		if (Hold <= T) { Stage = 2; T -= Hold; }
		Alpha = 1.f;
		break;
	default:
		if (Down <= T) { bActive = false; T -= Down; Alpha = 0.f; Stage = 3; }
		else { Alpha = 1.f - T / Down; }
		break;
	}
}

bool FSSXFrontEnd::IsReady() const
{
	const FSSXFeState* S = Top();
	return Boot == ESSXFeBoot::FrontEnd && MovieRequest.IsEmpty() && S && S->Phase == 2 && !bLaunchPending && !bHasPendingOpen
		&& !Camera.IsRunning();
}

namespace
{
	float SlideT(const FSSXFeState& S) { return S.SlideT(); }
	float BottomY(const FSSXFeState& S) { return BottomGroupY + BottomGroupH * (1.f - SlideT(S)); }
	float BarY(const FSSXFeState& S) { return S.BarH > 0.f ? -S.BarH * (1.f - SlideT(S)) : 0.f; }

	FLinearColor PulseColor(int32 Frame)
	{
		// FUN_002742e8
		const int32 P = Frame % (2 * SelectPulseFrames);
		const float T = P < SelectPulseFrames ? P / (float)SelectPulseFrames : 2.f - P / (float)SelectPulseFrames;
		return FMath::Lerp(White, Gold, T);
	}

	const FLinearColor BarTrack = FeColor(1.f, 0.2f, 0.2f, 0.2f);
	const FLinearColor PipRed = FeColor(1.f, 1.f, 0.f, 0.f);
	const FLinearColor PipYellow = FeColor(1.f, 1.f, 1.f, 0.f);
	const FLinearColor RankRed = FeColor(1.f, 1.f, 0.f, 0.f);
	// 0x276E28
	constexpr int32 InfoLayer = FrameLayer;
}

FString FSSXFrontEnd::HelpLine(const FSSXFeState& S) const
{
	if (S.Screen == ESSXFeScreen::Options)
	{
		// FUN_002AA998
		const int32 Id = OptionsSelectedId(S);
		const int32 HelpId = SSXFeOptions::HelpLoc(S.Page, Id);
		const TCHAR* Text = HelpId < 0 ? SSXFeOptions::HelpText(S.Page, Id) : nullptr;
		return HelpId >= 0 ? L(HelpId) : Text ? FString(Text) : FString();
	}
	if (S.Screen == ESSXFeScreen::TrickBook)
	{
		// FUN_002B0F68
		return L(S.bBookTricks ? SSXFeTrickBook::TrickHelpLoc : SSXFeTrickBook::ChapterHelpLoc);
	}
	const FSSXFeItem* I = S.Selected();
	if (I && S.Screen == ESSXFeScreen::VenueSel)
	{
		// FUN_0029C010 / 0x37ECB0 / 0x37EC80
		const int32 T = I->Id;
		if (I->bEnabled)
		{
			static const TMap<int32, int32> Desc = { { 0, 3277 }, { 1, 3278 }, { 2, 3279 }, { 3, 3280 }, { 4, 3281 }, { 5, 3282 },
				{ 6, 3283 }, { 9, 3283 }, { 7, 3286 }, { 8, 3284 }, { 11, 3285 }, { 10, 576 } };
			const int32* D = Desc.Find(T);
			return D ? L(*D) : FString();
		}
		const bool bUnlockAll = Cheats && Cheats->bUnlockAll;
		auto Open1 = [&](int32 Track) { return Profile ? Profile->IsVenueUnlocked(1, Track, bUnlockAll) : false; };
		if (T == 6) { return L(Open1(6) ? 3559 : 498); }
		if (T == 7) { return L(Open1(7) ? 3560 : 578); }
		static const TMap<int32, int32> HowTo = { { 0, 240 }, { 1, 525 }, { 2, 44 }, { 3, 322 }, { 4, 316 }, { 5, 14 }, { 6, 498 }, { 9, 498 },
			{ 7, 578 }, { 10, 578 }, { 8, 549 }, { 11, 3376 } };
		const int32 Sub = (GameMode == 1 || GameMode == 7) ? 3377 : ((GameMode == 2 || GameMode == 4) ? 49 : ((GameMode == 3 || GameMode == 5) ? 50 : -1));
		const int32* H = HowTo.Find(T);
		if (Sub < 0 || !H)
		{
			return FString();
		}
		return L(*H).Replace(TEXT("%s"), *L(Sub));
	}
	if (I && S.Screen == ESSXFeScreen::CustomBody)
	{
		// FUN_0028DE90 / 0x3AFBA0
		return L(!I->bEnabled && I->HelpLoc >= 0 ? I->HelpLoc : 3243);
	}
	if (!I || I->HelpLoc < 0)
	{
		return FString();
	}
	// TODO: the CustomBoard / CustomBody handlers are not tabulated;
	const bool bLockText = S.Screen == ESSXFeScreen::CharacterSelect || S.Screen == ESSXFeScreen::CustomBoard;
	return bLockText && I->bEnabled ? FString() : L(I->HelpLoc);
}

uint32 FSSXFrontEnd::LegendDisabledMask(const FSSXFeState& S) const
{
	if (S.Screen == ESSXFeScreen::RiderProfile && S.PageSel == 2)
	{
		// FUN_00271848
		const int32 Char = Profile ? FMath::Clamp(Profile->Slot[S.Player].Char, 0, 11) : 0;
		const int32 Rank = SSXFeTables::RankFromPoints(Profile ? Profile->Career[Char].RankPoints() : 0);
		return Rank == 0 ? 0u : 0x4u;
	}
	if (S.Screen == ESSXFeScreen::MemCard)
	{
		return MemCard.LegendDisabledMask();   // FUN_00292968 / FUN_002924d0
	}
	if (S.Screen == ESSXFeScreen::TrickBook && S.bBookTricks)
	{
		// FUN_00271848
		return TrickBookDone(S, S.BookTrick) ? 0u : 0x4u;
	}
	if (S.Screen == ESSXFeScreen::Options && S.Page >= 0)
	{
		// FUN_00271848
		const bool bEnabled = S.Page == 3 || (S.Page != 4 && OptionsSelectedId(S) == SSXFeOptions::DoneId);
		return bEnabled ? 0u : 0x2u;
	}
	return 0u;
}

void FSSXFrontEnd::DrawHelpBar(FSSXFeDrawList& D, uint32 Mask, float X, float Y, float LabelScale, int32 Layer, uint32 DisabledMask) const
{
	// FUN_002719C0 / FUN_002AE230 / 0x347F20
	int32 Row = 0;
	for (const FHelpEntry& E : HelpBarTable())
	{
		if (!(Mask & E.Mask))
		{
			continue;
		}
		const float SlotY = Y + Row * HelpRowPitch;
		const FString Label = L(E.Loc);
		const float W = D.TextWidth(EFont::Menu, Label, LabelScale) + ShadowOffset;
		const float H = D.FontHeight(EFont::Menu, LabelScale) + ShadowOffset;
		// FUN_002AD988
		const FLinearColor C = (DisabledMask & E.Mask) ? Grey : White;
		D.Text(EFont::Menu, X - W, SlotY + (HelpSlotH - H) * 0.5f, LabelScale, C, Label, ESSXAlign::Left, true, TextLayer(Layer));
		D.Sprite(E.Icon, X + HelpIconGap, SlotY + (HelpSlotH - HelpIconSize) * 0.5f, HelpIconSize, HelpIconSize, C, SpriteLayer(Layer));
		++Row;
	}
}

void FSSXFrontEnd::DrawTitleBar(FSSXFeDrawList& D, float H, float Y) const
{
	// FUN_00268308
	const int32 Lr = SpriteLayer(TitleBarLayer);
	D.Rect(0.f, Y, ScreenW, H - BarNotchY, OptionsPanel, Lr);
	D.Rect(BarNotchX, Y + H - BarNotchY, BarNotchW, BarNotchH, OptionsPanel, Lr);
	D.Sprite(SprBarEdgeL, 0.f, Y + H - BarEdgeH, ScreenW * 0.5f, BarEdgeH, White, SpriteLayer(TitleBarLayer + 1));
	D.Sprite(SprBarEdgeR, ScreenW * 0.5f, Y + H - BarEdgeH, ScreenW * 0.5f, BarEdgeH, White, SpriteLayer(TitleBarLayer + 1));
}

void FSSXFrontEnd::DrawFrame(FSSXFeDrawList& D, const FSSXFeState& S, int32 TabLoc, uint32 HelpMask) const
{
	// FUN_002720F8
	const float Ty = -TopGroupH * (1.f - SlideT(S));
	const float By = BottomY(S);
	const FString Tab = TabLoc >= 0 ? L(TabLoc) : FString();
	const float Tw = D.TextBoxWidth(EFont::Menu, Tab, 1.f) + ShadowOffset;
	const float Th = D.FontHeight(EFont::Menu, 1.f) + ShadowOffset;
	D.Text(EFont::Menu, TabTextX, Ty + TabTextY, 1.f, White, Tab, ESSXAlign::Left, true, TextLayer(FrameLayer + 5));
	D.Sprite(SprTabBody, 0.f, Ty, Tw + TabPadW, Th + TabPadH, FrameGrey, SpriteLayer(FrameLayer + 2));
	D.Sprite(SprTabCap, Tw + TabPadW, Ty, TabCapW, Th + TabPadH, FrameGrey, SpriteLayer(FrameLayer + 2));
	D.SpriteTiled(SprPanelL, 0.f, Ty, ScreenW * 0.5f, PanelH, 1.f, 1.f, White, SpriteLayer(FrameLayer));
	D.SpriteTiled(SprPanelR, ScreenW * 0.5f, Ty, ScreenW * 0.5f, PanelH, 1.f, 1.f, White, SpriteLayer(FrameLayer));
	D.Sprite(SprStripL, 0.f, Ty + PanelH, ScreenW * 0.5f, StripH, White, SpriteLayer(FrameLayer));
	D.Sprite(SprStripR, ScreenW * 0.5f, Ty + PanelH, ScreenW * 0.5f, StripH, White, SpriteLayer(FrameLayer));
	D.SpriteTiled(SprFillL, 0.f, By + BottomFillY, ScreenW * 0.5f, BottomFillH, 1.f, BottomFillRepV, FrameGrey, SpriteLayer(FrameLayer));
	D.SpriteTiled(SprFillR, ScreenW * 0.5f, By + BottomFillY, ScreenW * 0.5f, BottomFillH, 1.f, BottomFillRepV, FrameGrey, SpriteLayer(FrameLayer));
	D.Sprite(SprBStripL, 0.f, By, ScreenW * 0.5f, StripH, FrameGrey, SpriteLayer(FrameLayer));
	D.Sprite(SprBStripR, ScreenW * 0.5f, By, ScreenW * 0.5f, StripH, FrameGrey, SpriteLayer(FrameLayer));
	if (HelpMask != 0)
	{
		DrawHelpBar(D, HelpMask, HelpBarX, By + HelpBarY, HelpLabelScale, FrameLayer + 3, LegendDisabledMask(S));
	}
	const FString Help = HelpLine(S);
	if (!Help.IsEmpty())
	{
		D.TextBox(EFont::Menu, HelpLineX, By + HelpLineY, S.HelpW, HelpLineScale, Gold, Help, ESSXAlign::Left, true, TextLayer(FrameLayer + 3));
	}
}

void FSSXFrontEnd::DrawTitleScreen(FSSXFeDrawList& D, const FSSXFeState& S) const
{
	// 0x293528 / FUN_00274108
	const float Pitch = D.FontHeight(EFont::Title, 1.f) + ShadowOffset + ListSpacing;
	for (int32 I = 0; I < S.Items.Num(); ++I)
	{
		const FSSXFeItem& It = S.Items[I];
		const FLinearColor C = !It.bEnabled ? Grey : (I == S.Sel ? PulseColor(S.Frame) : White);
		D.Text(EFont::Title, TitleListX, TitleListY + I * Pitch, 1.f, C, It.Loc >= 0 ? L(It.Loc) : It.Text, ESSXAlign::Left, true, TextLayer(1));
	}
	D.Text(EFont::Menu, 38.f, 410.f, TitleLineScale, White, L(107), ESSXAlign::Left, true, TextLayer(1));
	D.Text(EFont::Menu, 20.f, 428.f, TitleLineScale, White, L(3544), ESSXAlign::Left, true, TextLayer(1));
	DrawHelpBar(D, S.HelpMask, HelpBarX, TitleHelpY, TitleLineScale, 2);
}

void FSSXFrontEnd::DrawStandard(FSSXFeDrawList& D, const FSSXFeState& S) const
{
	if (S.Screen == ESSXFeScreen::Title)
	{
		DrawTitleScreen(D, S);
		return;
	}
	auto ItemText = [this](const FSSXFeItem& I) { return I.Loc >= 0 ? L(I.Loc) : I.Text; };
	const float Y0 = BarY(S);
	if (S.BarH > 0.f)
	{
		DrawTitleBar(D, S.BarH, Y0);
	}
	const bool bOptionsPage = S.Screen == ESSXFeScreen::Options && S.Page >= 0;
	const int32 ListLayer = S.BarH > 0.f ? TitleBarLayer + 1 : 1;
	if (S.bSelectedOnly)
	{
		if (const FSSXFeItem* I = S.Selected())
		{
			const FString Str = ItemText(*I);
			const float W = D.TextWidth(S.ItemFont, Str, 1.f) + ShadowOffset;
			const float H = D.FontHeight(S.ItemFont, 1.f) + ShadowOffset;
			// FUN_002AD988
			D.Text(S.ItemFont, ItemCX - W * 0.5f, Y0 + ItemCY - H * 0.5f, 1.f, !I->bEnabled ? Grey : (S.bListFocused ? Gold : White), Str,
				ESSXAlign::Left, true, TextLayer(ListLayer));
			auto ArrowColor = [&S](int32 Side)
			{
				const int32 F = S.Frame - S.ArrowFlashAt[Side];
				return F >= 0 && F < ArrowFlashFrames ? FMath::Lerp(ArrowFlash, White, F / (float)ArrowFlashFrames) : White;
			};
			if (S.bWrap || S.Sel > 0)
			{
				D.Sprite(SprArrowL, ArrowLX, Y0 + ArrowY, ArrowSize, ArrowSize, ArrowColor(0), SpriteLayer(ListLayer + 1));
			}
			if (S.bWrap || S.Sel < S.Items.Num() - 1)
			{
				D.Sprite(SprArrowR, ArrowRX, Y0 + ArrowY, ArrowSize, ArrowSize, ArrowColor(1), SpriteLayer(ListLayer + 1));
			}
		}
	}
	else if (S.Items.Num() > 0 && !bOptionsPage)
	{
		// FUN_00274108 / 0x26F2A0 / FUN_002742E8
		const float ItemH = D.FontHeight(S.ItemFont, 1.f) + ShadowOffset;
		const float Pitch = ItemH + ListSpacing;
		for (int32 I = 0; I < S.Items.Num(); ++I)
		{
			const FSSXFeItem& It = S.Items[I];
			const FLinearColor C = !It.bEnabled ? Grey : (I == S.Sel ? PulseColor(S.Frame) : White);
			D.Text(S.ItemFont, ListX, Y0 + ListY93 - ItemH * 0.5f + I * Pitch, 1.f, C, ItemText(It), ESSXAlign::Center, true, TextLayer(ListLayer));
		}
	}
	if (S.TitleLoc >= 0)
	{
		DrawFrame(D, S, S.TitleLoc, S.HelpMask);
	}
}

void FSSXFrontEnd::DrawStatRows(FSSXFeDrawList& D, int32 Char, int32 Board, float GroupY) const
{
	// FUN_001646B0 / FUN_00165468
	static const int32 Labels[] = { 37, 39, 40, 41 };
	const SSXFeTables::FBoard& B = SSXFeTables::Board(Char, Board);
	const bool bMaxAttr = Cheats && Cheats->bMaxAttributes;
	for (int32 I = 0; I < 4; ++I)
	{
		const float RowY = GroupY + 45.f + 15.f * I;
		const int32 SlotMax = SSXProg::StatBarMax(Char, I);
		const int32 Bonus = (int32)(B.Stats[I] * 0.1f);
		const int32 Max = SlotMax + Bonus;
		const int32 Base = bMaxAttr ? SlotMax : (Profile ? Profile->Career[Char].StatPoints[I] : 0);
		const int32 Value = Base + Bonus;
		const FString Label = L(Labels[I]);
		const float Lw = D.TextWidth(EFont::Menu, Label, 0.7f) + ShadowOffset;
		const float Lh = D.FontHeight(EFont::Menu, 0.7f) + ShadowOffset;
		D.Text(EFont::Menu, 170.f - Lw, RowY - Lh * 0.5f, 0.7f, White, Label, ESSXAlign::Left, true, TextLayer(InfoLayer + 3));
		const float X = 178.f, Y = RowY - 5.f, W = Max * 5.f;
		D.SpriteTiled(SprBarTrack, X, Y, W, 10.f, 1.f, 1.f, BarTrack, SpriteLayer(InfoLayer + 3));
		// 0x2AE880 / FUN_0014EEE8 / FUN_001FC728 / FUN_001FC178
		D.Sprite(SprBarCap, X + W, Y, 6.f, 10.f, BarTrack, SpriteLayer(InfoLayer + 3));
		// FUN_001FCCC8 / 0x19B4D8 / 0x19BF90
		if (Max > 0 && Value > Base)
		{
			D.SpriteTiled(SprPip, X, Y, (float)Value / Max * W, 10.f, (float)Value, 1.f, PipRed, SpriteLayer(InfoLayer + 4));
		}
		if (Max > 0 && Base > 0)
		{
			D.SpriteTiled(SprPip, X, Y, (float)Base / Max * W, 10.f, (float)Base, 1.f, PipYellow, SpriteLayer(InfoLayer + 5));
		}
		D.Text(EFont::Menu, 450.f, RowY - D.FontHeight(EFont::Menu, 0.7f) * 0.5f, 0.7f, White, FString::Printf(TEXT("%d/%d"), Value, Max),
			ESSXAlign::Left, true, TextLayer(InfoLayer + 3));
	}
}

void FSSXFrontEnd::DrawCharPanel(FSSXFeDrawList& D, const FSSXFeState& S, int32 Char, float GroupY) const
{
	const int32 TypeLoc = SSXFeTables::BoardTypeLoc(SSXFeTables::Board(Char, 0).Type);
	if (TypeLoc >= 0)
	{
		D.Text(EFont::Menu, 20.f, GroupY + 18.f, 0.8f, White, L(TypeLoc), ESSXAlign::Left, true, TextLayer(FrameLayer + 8));
	}
	const FSSXFeItem* Sel = S.Selected();
	if (Sel && !Sel->bEnabled)
	{
		return;
	}
	const int32 Board = Profile && Profile->Slot[S.Player].Char == Char ? Profile->Slot[S.Player].Board : 0;
	if (!S.bRankingView)
	{
		DrawStatRows(D, Char, Board, GroupY);
		return;
	}
	const int32 Rank = SSXFeTables::RankFromPoints(Profile ? Profile->Career[Char].RankPoints() : 0);
	const FSSXSprite* S28 = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(IconRankInactive) : nullptr;
	const float W28 = S28 ? S28->W : 0.f, H28 = S28 ? S28->H : 0.f;
	for (int32 I = 0; I < 10; ++I)
	{
		const bool bOn = I <= 9 - Rank;
		const int32 Id = bOn ? IconRankActive : IconRankInactive;
		const FSSXSprite* R = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(Id) : nullptr;
		const float W = R ? R->W : 0.f, H = R ? R->H : 0.f;
		const float Cx = 250.f + I * (W28 + 6.f);
		D.Sprite(Id, Cx - W * 0.5f, GroupY + 75.f - H * 0.5f, W, H, bOn ? FMath::Lerp(Gold, RankRed, I / 10.f) : White, SpriteLayer(InfoLayer + 4));
	}
	const float CurX = 250.f + (9 - Rank) * (W28 + 6.f);
	// 0x3D2060
	D.Rect(CurX - 1.5f, GroupY + 73.f - 11.5f, 3.f, 23.f, Black, SpriteLayer(InfoLayer + 3));
	const float Th = D.FontHeight(EFont::Menu, 0.8f) + ShadowOffset;
	// 0x276E28
	const FString Head = L(221);
	D.Text(EFont::Menu, 250.f - W28 - 6.f - (D.TextWidth(EFont::Menu, Head, 0.8f) + ShadowOffset), GroupY + 75.f - Th * 0.5f, 0.8f, White,
		Head, ESSXAlign::Left, true, TextLayer(InfoLayer + 3));
	D.Text(EFont::Menu, CurX, GroupY + 75.f - H28 - Th - 5.f, 0.8f, White, L(23 + Rank), ESSXAlign::Center, true, TextLayer(InfoLayer + 3));
}

void FSSXFrontEnd::DrawUserName(FSSXFeDrawList& D, const FSSXFeState& S) const
{
	using namespace SSXFeUserName;
	DrawStandard(D, S);
	const float By = BarY(S);
	const int32 Lr = TitleBarLayer + 1;
	// 0x29C370
	static const FLinearColor BoxCol = FeColor(0.7f, 0.478f, 0.478f, 0.478f);
	static const FLinearColor CapCol = FeColor(0.7f, 0.3f, 0.3f, 0.3f);
	D.Rect(BoxX - BoxW * 0.5f, By + BoxY, BoxW, BoxH, BoxCol, SpriteLayer(Lr));
	const FSSXSprite* Cap = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(SprCap) : nullptr;
	const float CapW = Cap ? Cap->W : 6.f;
	D.Sprite(SprCap, CapLX - CapW, By + BoxY, CapW, BoxH, CapCol, SpriteLayer(Lr));
	D.SpriteEx(SprCap, CapRX, By + BoxY, CapW, BoxH, CapCol, SpriteLayer(Lr), false, true, false);
	const float NameW = D.TextBoxWidth(EFont::Title, S.Name, NameScale);
	D.Text(EFont::Title, (ScreenW - NameW) * 0.5f, By + NameY, NameScale, Gold, S.Name, ESSXAlign::Left, false, TextLayer(Lr + 1));
	// 0x2B0528 / FUN_0019D6E0 / FUN_0019D770 / FUN_002AC348
	const float X0 = GridX0, Y0 = By + GridY0;
	const int32 Sel = S.GridY * Cols + S.GridX;
	const float LineH = D.FontHeight(EFont::Menu, 1.f);
	for (int32 I = 0; I < NumCells; ++I)
	{
		const TCHAR C = Cell(I % Cols, I / Cols);
		const float Cx = X0 + (I % Cols) * CellW, Cy = Y0 + (I / Cols) * CellH;
		if (I == Sel)
		{
			const int32 F = S.Frame % (2 * SelectPulseFrames);
			const float T = F < SelectPulseFrames ? F / (float)SelectPulseFrames : 2.f - F / (float)SelectPulseFrames;
			const FLinearColor CurCol = FMath::Lerp(FeColor(0.88f, 0.1f, 0.1f, 0.1f), White, T);
			const FSSXSprite* Cur = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(SprCursor) : nullptr;
			const float W = (Cur ? Cur->W : 25.f) * CursorScale, H = (Cur ? Cur->H : 20.f) * CursorScale;
			D.Sprite(SprCursor, Cx + 1.f - W * 0.5f, Cy + LineH * 0.5f + 2.f - H * 0.5f, W, H, CurCol, SpriteLayer(Lr + 1));
		}
		if (C == TEXT('e'))
		{
			const FString End = L(274);
			const float W = float(int32(D.TextInkWidth(EFont::Menu, End, EndScale)));
			D.Text(EFont::Menu, Cx - W * 0.5f, Cy + LineH * (1.f - EndScale) * 0.5f, EndScale, White, End, ESSXAlign::Left, true, TextLayer(Lr + 1));
		}
		else if (C == TEXT('d'))
		{
			const FSSXSprite* Del = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(SprDelete) : nullptr;
			const float W = Del ? Del->W : 7.f, H = Del ? Del->H : 11.f;
			D.Sprite(SprDelete, Cx - W * 0.5f, Cy + LineH - H, W, H, White, SpriteLayer(Lr + 1));
		}
		else
		{
			const FString Str(1, &C);
			const float W = float(int32(D.TextWidth(EFont::Menu, Str, 1.f)));
			D.Text(EFont::Menu, Cx - W * 0.5f, Cy, 1.f, White, Str, ESSXAlign::Left, true, TextLayer(Lr + 1));
		}
	}
}

FString FSSXFrontEnd::FeTime(int32 Cs) const
{
	if (Cs == 50000) { return L(349); }
	if (Cs < 0) { return L(396); }
	if (Cs >= 360000) { return TEXT("59:59.99"); }
	return FString::Printf(TEXT("%d:%02d.%02d"), Cs / 100 / 60, Cs / 100 % 60, Cs % 100);
}

void FSSXFrontEnd::DrawRiderProfile(FSSXFeDrawList& D, const FSSXFeState& S) const
{
	using namespace SSXFeRiderProfile;
	const int32 Char = Profile ? FMath::Clamp(Profile->Slot[S.Player].Char, 0, 11) : 0;
	const int32 Page = FMath::Clamp(S.PageSel, 0, 4);
	// FUN_0027E918
	D.Rect(PanelX, 0.f, PanelW, ProfilePanelH, RiderProfilePanel, 0);
	const int32 Spr = PortraitSprite(Page);
	D.PageSprite(PortraitPage(Char, Spr), Spr, PortraitX, PortraitY, PortraitW, PortraitH, Page == 2 ? Grey : White, 0);
	D.SpriteTiled(SprEdge, PanelX - EdgeW, PortraitY, EdgeW, PortraitH, 1.f, ProfilePanelH / EdgeTileH, FeColor(1.f, 0.3912f, 0.5265f, 0.5191f), 0);
	D.Rect(0.f, 0.f, ScreenW, BarH, Black, 0);
	D.Rect(0.f, ScreenH - BarH, ScreenW, BarH, Black, 0);
	DrawStandard(D, S);
	const int32 Tl = TextLayer(1);
	switch (Page)
	{
	case 0:
	{
		// FUN_0027F600
		const float CellH = D.FontHeight(EFont::Menu, BioScale);
		for (int32 K = 0; K < 15; ++K)
		{
			const float Y = BioY + K * (CellH + BioSpacing);
			const FString Label = L(586 + K);
			D.Text(EFont::Menu, BioX - D.TextBoxWidth(EFont::Menu, Label, BioScale), Y, BioScale, Black, Label, ESSXAlign::Left, false, Tl);
			D.Text(EFont::Menu, BioX + SSXFeOptions::EntryGap, Y, BioScale, Black, L(BioValueLoc(Char, K)), ESSXAlign::Left, false, Tl);
		}
		break;
	}
	case 1: case 4:
	{
		const FSSXFeScrollText& T = S.ProfileText[Page == 1 ? 0 : 1];
		float Yl = TextY - T.Offset;
		for (int32 I = 0; I < T.Slots.Num(); ++I, Yl += T.Pitch())
		{
			if (T.Slots[I] < 0)
			{
				continue;
			}
			FString Str = L(T.Slots[I]);
			if (Str.IsEmpty())
			{
				continue;
			}
			const bool bHead = Str[0] == TEXT('~');
			if (bHead)
			{
				Str.RightChopInline(1);
			}
			FLinearColor C = bHead ? White : Black;
			C.A = T.SlotAlpha(I);
			D.Text(EFont::Menu, TextX, Yl, T.Scale, C, Str, ESSXAlign::Left, bHead, Tl);
		}
		break;
	}
	case 2:
	{
		// FUN_00282D30
		const FSSXCareer& Cr = Profile ? Profile->Career[Char] : FSSXCareer();
		const bool bUnlockAll = Cheats && Cheats->bUnlockAll;
		const float CellH = D.FontHeight(EFont::Menu, WcScale);
		const float RowH = FMath::Max(CellH, WcMedalH);
		const float Pitch = RowH + WcSpacing;
		auto Medal = [&D](int32 M, float X, float Y, int32 Lr)
		{
			const int32 Spr = M == SSXProg::Gold ? SprMedalGold : (M == SSXProg::Silver ? SprMedalSilver : SprMedalBronze);
			if (M >= SSXProg::Gold && M <= SSXProg::Bronze)
			{
				D.Sprite(Spr, X, Y, WcMedalW, WcMedalH, White, SpriteLayer(Lr));
			}
		};
		auto CellText = [&](float X, float Y, const FString& Str, const FLinearColor& C)
		{
			D.Text(EFont::Menu, X, Y, WcScale, C, Str, ESSXAlign::Left, false, Tl);
		};
		int32 Row = 0, ShowRow = 0;
		for (const int32 V : SSXFeTables::VenueOrder())
		{
			const float Y = WcListY + Row * Pitch;
			const bool bFree = V == SSXProg::TrackPipedream || V == SSXProg::TrackUntracked;
			bool bEnabled = true;
			if (bFree && Profile)
			{
				bEnabled = Profile->IsVenueUnlocked(V == SSXProg::TrackPipedream ? 3 : 1, V, bUnlockAll);   // FUN_00163BB8
			}
			const FString Name = L(VenueNameLoc(V));
			CellText(WcRaceX - D.TextBoxWidth(EFont::Menu, Name, WcScale), Y, Name, bEnabled ? Black : White);
			if (bFree)
			{
				CellText(WcRaceX + WcGap, Y, bEnabled ? L(3549) : FString(TEXT("---")), Black);
			}
			else
			{
				const int32 Cs = Cr.BestTime[V];
				CellText(WcRaceX + WcGap, Y, Cs < 1 ? FString(TEXT("---")) : FeTime(Cs), Black);
				Medal(Cr.Tracks[V].RaceMedal, WcRaceX + WcGap + WcCell + WcGap, Y, 1);
				const float Ys = WcListY + ShowRow * Pitch;
				const int32 Pts = Cr.BestScore[V];
				CellText(WcShowX, Ys, Pts < 1 ? FString(TEXT("---")) : FString::FromInt(Pts), Black);
				Medal(Cr.Tracks[V].ShowoffMedal, WcShowX + WcCell + WcGap, Ys, 1);
				++ShowRow;
			}
			++Row;
		}
		struct FHead { int32 Loc; float X, Y; };
		static const FHead Heads[] = { { 3184, 320.f, 110.f }, { 3186, 320.f, 125.f }, { 3187, 390.f, 125.f }, { 3185, 470.f, 110.f },
			{ 3188, 470.f, 125.f }, { 3187, 540.f, 125.f } };
		for (const FHead& H : Heads) { D.Text(EFont::Menu, H.X, H.Y, WcScale, Black, L(H.Loc), ESSXAlign::Left, false, Tl); }
		const int32 Rank = FMath::Clamp(SSXFeTables::RankFromPoints(Cr.RankPoints()), 0, 9);
		const float Rh = D.FontHeight(EFont::Title, RankScale);
		for (int32 K = 0; K < 10; ++K)
		{
			const FString Str = L(23 + K);
			const float W = D.TextBoxWidth(EFont::Title, Str, RankScale);
			const float Yc = RankY + K * (RankCellH + RankSpacing);
			D.Text(EFont::Title, RankX + RankCellW - W, Yc + (RankCellH - Rh) * 0.5f, RankScale, K == Rank ? Black : Grey, Str, ESSXAlign::Left, false, TextLayer(1));
		}
		D.Rect(0.f, RankY + Rank * (RankCellH + RankSpacing) - 2.f, RankCursorW, RankCursorH, RankCursor, SpriteLayer(0));
		break;
	}
	case 3:
	{
		// FUN_00285230
		const FSSXCareer& Cr = Profile ? Profile->Career[Char] : FSSXCareer();
		const float CellH = D.FontHeight(EFont::Menu, WcScale);
		const float Pitch = CellH + BestSpacing;
		auto Venue = [](int32 T) { return T >= 0 && T < 13 ? SSXFeTables::TrackTableName(T) : FString(); };
		auto RowAt = [&](int32 K, int32 Loc, const FString& Value, const FString& Where)
		{
			const float Y = BestY + K * Pitch;
			const FString Label = L(Loc);
			D.Text(EFont::Menu, BestX - D.TextBoxWidth(EFont::Menu, Label, WcScale), Y, WcScale, Black, Label, ESSXAlign::Left, false, Tl);
			D.Text(EFont::Menu, BestX + WcGap, Y, WcScale, Black, Value, ESSXAlign::Left, false, Tl);
			if (!Where.IsEmpty())
			{
				D.Text(EFont::Menu, BestX + WcGap + BestCell + WcGap, Y, WcScale, Black, Where, ESSXAlign::Left, false, Tl);
			}
		};
		auto IntRow = [&](int32 K, int32 Loc, const FSSXCareerBest& B)
		{
			const int32 V = int32(B.Value);
			RowAt(K, Loc, V < 1 ? FString(TEXT("-")) : FString::FromInt(V), V < 1 ? FString(TEXT("---")) : Venue(B.Track));
		};
		auto TimeRow = [&](int32 K, int32 Loc, const FSSXCareerBest& B)
		{
			float F = 0.f;
			FMemory::Memcpy(&F, &B.Value, sizeof(F));
			RowAt(K, Loc, F <= 0.f ? FString(TEXT("-")) : FeTime(int32(F * 100.f)), F <= 0.f ? FString(TEXT("---")) : Venue(B.Track));
		};
		IntRow(0, 3168, Cr.Bests[0]);
		IntRow(1, 3169, Cr.Bests[1]);
		IntRow(2, 3170, Cr.Bests[2]);
		static const int32 ColourLoc[6] = { 3176, 3177, 3178, 3179, 3180, 3506 };
		for (int32 C = 0; C < 6; ++C) { IntRow(3 + C, ColourLoc[C], Cr.TrickColour[C]); }
		IntRow(9, 3171, Cr.Bests[3]);
		for (int32 B = 4; B < 7; ++B) { TimeRow(6 + B, 3168 + B, Cr.Bests[B]); }
		int32 Best = 0, Fav = SSXProg::NumTracks;
		for (int32 T = 0; T < SSXProg::NumTracks; ++T) { if (Best < Cr.EventCount[T]) { Best = Cr.EventCount[T]; Fav = T; } }
		RowAt(13, 3175, Fav == SSXProg::NumTracks ? FString(TEXT("---")) : FString::Printf(TEXT("%s (%d)"), *Venue(Fav), Best), FString());
		D.Text(EFont::Menu, 375.f, 110.f, WcScale, Black, L(3181), ESSXAlign::Left, false, Tl);
		D.Text(EFont::Menu, 500.f, 110.f, WcScale, Black, L(3182), ESSXAlign::Left, false, Tl);
		break;
	}
	default:
		break;
	}
}

void FSSXFrontEnd::DrawTrickBook(FSSXFeDrawList& D, const FSSXFeState& S) const
{
	using namespace SSXFeTrickBook;
	// 0x27A5B8 / 0x27DC58
	const int32 Char = Profile ? FMath::Clamp(Profile->Slot[S.Player].Char, 0, 11) : 0;
	const int32 Chapter = FMath::Clamp(S.Chapter >= 0 ? S.Chapter : FMath::Max(S.SelectedId(), 0), 0, Chapters - 1);
	// background: untextured 640 x 480 (1, 1.1137, .8627, 0), layer 0 (the GS shows x128: (143, 110, 0))
	D.Rect(0.f, 0.f, ScreenW, ScreenH, Background, SpriteLayer(0));
	D.SpriteTiled(SprBand, 0.f, BandY - BandH, ScreenW, BandH, BandRepU, 1.f, BandColor, SpriteLayer(1));
	D.PageSprite(CoverPage(Char), SprCover, CoverX, CoverY, CoverSize, CoverSize, White, SpriteLayer(2));
	{
		const FString Name = SSXFeTables::RiderName(Char);
		const float W = D.TextBoxWidth(EFont::Title, Name, 1.f) + ShadowOffset;
		D.Text(EFont::Title, CoverX + (CoverSize - W) * 0.5f, NameY, 1.f, White, Name, ESSXAlign::Left, true, TextLayer(3));
	}
	// FUN_0019E840 / 0x338124
	{
		const SSXTrick::FPointsTier& T = SSXTrick::FTrickTables::Get().Tiers[FMath::Clamp(Chapter, 0, SSXTrick::NumTiers - 1)];
		D.Icon(SprChapter, ChapterX, ChapterY, -1.f, -1.f, 0, FeColor(T.A, T.R, T.G, T.B), SpriteLayer(2));
	}
	if (!S.BookTrickName.IsEmpty())
	{
		D.TextBox(EFont::Menu, TrickNameX, TrickNameY, TrickNameW, TrickNameScale, Black, S.BookTrickName, ESSXAlign::Left, false, TextLayer(2));
	}
	{
		const FVector2f P = FSSXFeDrawList::IconOrigin(PicX, PicY, PicW, PicH, 0x60);
		if (S.BookPicSprite == SprVideo)
		{
			if (BookVideoTexture) { D.Image(BookVideoTexture, P.X, P.Y, PicW, PicH, S.BookPicColor, SpriteLayer(4)); }
		}
		else if (S.BookPicSprite >= 0x7C && S.BookPicSprite < 0x7C + TricksPerChapter)
		{
			D.PageSprite(TrickPage(Char, Chapter, S.BookPicSprite - 0x7C), S.BookPicSprite, P.X, P.Y, PicW, PicH, S.BookPicColor, SpriteLayer(4));
		}
		else
		{
			D.Rect(P.X, P.Y, PicW, PicH, S.BookPicColor, SpriteLayer(4));
		}
	}
	{
		const FSSXSprite* F = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(SprFrame) : nullptr;
		const float H = (F ? F->H : 14.f) * 0.5f;
		for (int32 K = 0; K < 8; ++K)
		{
			const FFramePiece& Pc = FramePieces()[K];
			D.Icon(SprFrame, Pc.X, Pc.Y, Pc.W, H, Pc.Flags, Black, SpriteLayer(5));
		}
	}
	{
		const FSSXSprite* F = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(SprFrame) : nullptr;
		const float H41 = F ? F->H : 14.f;
		const int32 Gl = 3;
		D.Icon(SprFrame, 0.f, GroupY, ScreenW * 0.5f, H41, 0x1, Black, SpriteLayer(Gl));
		D.Icon(SprFrame, ScreenW * 0.5f, GroupY, ScreenW * 0.5f, H41, 0x0, Black, SpriteLayer(Gl));
		D.Rect(0.f, GroupY + H41, ScreenW, TricksPanelH, Black, SpriteLayer(Gl));
		const int32 Ll = Gl + 1;
		const float Ly = GroupY + TrickListY;
		for (int32 T = 0; T < TricksPerChapter; ++T)
		{
			const float X = TrickListX + T * (IconW + IconSpacing);
			D.PageSprite(TrickPage(Char, Chapter, T), 0x7C + T, X, Ly, IconW, IconH, TrickBookDone(S, T) ? White : IconDisabled, SpriteLayer(Ll));
		}
		// FUN_0026D8C8
		auto ArrowColor = [&S](int32 Side)
		{
			const int32 F = S.Frame - S.BookArrowFlashAt[Side];
			return F >= 0 && F < ArrowFlashFrames ? FMath::Lerp(ArrowFlash, White, F / (float)ArrowFlashFrames) : White;
		};
		if (S.BookTrick > 0)
		{
			D.Sprite(SprArrowL, TrickListX - TrickArrowW - IconSpacing, GroupY + TrickArrowY, TrickArrowW, TrickArrowH, ArrowColor(0), SpriteLayer(Ll + 1));
		}
		if (S.BookTrick < TricksPerChapter - 1)
		{
			D.Sprite(SprArrowR, TrickListX + TricksPerChapter * (IconW + IconSpacing), GroupY + TrickArrowY, TrickArrowW, TrickArrowH, ArrowColor(1), SpriteLayer(Ll + 1));
		}
		if (S.bBookTricks)
		{
			const int32 Pf = S.Frame % (2 * SelectPulseFrames);
			const float A = Pf < SelectPulseFrames ? Pf / (float)SelectPulseFrames : 2.f - Pf / (float)SelectPulseFrames;
			const float X = TrickListX + S.BookTrick * (IconW + IconSpacing);
			D.Sprite(SprSlider, X - SliderInset, Ly - SliderInset, SliderW, SliderH, FMath::Lerp(White, Gold, A), SpriteLayer(Ll + 2));
		}
	}
	DrawStandard(D, S);
	// FUN_00164178 / FUN_0027A4A8
	{
		int32 Done = 0;
		for (int32 T = 0; T < TricksPerChapter; ++T) { Done += TrickBookDone(S, T) ? 1 : 0; }
		FString Counter = L(CounterLoc);
		for (const int32 V : { Done, TricksPerChapter })
		{
			const int32 At = Counter.Find(TEXT("%d"));
			if (At != INDEX_NONE) { Counter = Counter.Left(At) + FString::FromInt(V) + Counter.Mid(At + 2); }
		}
		const float W = D.TextBoxWidth(EFont::Title, Counter, CounterScale) + ShadowOffset;
		D.Text(EFont::Title, (ScreenW - W) * 0.5f, BottomY(S) + CounterY, CounterScale, Gold, Counter, ESSXAlign::Left, true, TextLayer(FrameLayer + 4));
	}
}

void FSSXFrontEnd::DrawOptions(FSSXFeDrawList& D, const FSSXFeState& S) const
{
	using namespace SSXFeOptions;
	// 0x2A8A30
	DrawStandard(D, S);
	if (S.Page < 0 || S.Page >= NumPages)
	{
		return;
	}
	const FSSXFeOptionsUI& O = S.Opt;
	const float Y0 = BarY(S);
	const int32 Tl = TextLayer(TitleBarLayer + 1);
	auto MenuW = [&D](const FString& Str, float Scale) { return D.TextBoxWidth(EFont::Menu, Str, Scale) + ShadowOffset; };
	const TCHAR* TitleText = PageTitleText(S.Page);
	const FString Title = TitleText ? FString(TitleText) : L(PageTitleLoc(S.Page));
	const float TitleW = D.TextBoxWidth(EFont::Title, Title, 1.f) + ShadowOffset;
	D.Text(EFont::Title, (ScreenW - TitleW) * 0.5f, Y0 + TitleY, 1.f, White, Title, ESSXAlign::Left, true, Tl);

	if (S.Page == 4)
	{
		// FUN_0019DC78
		const FSSXFeScrollText& T = O.Credits;
		float Yl = Y0 + CreditsY - T.Offset;
		for (int32 I = 0; I < T.Slots.Num(); ++I, Yl += T.Pitch())
		{
			if (T.Slots[I] < 0)
			{
				continue;
			}
			FString Str = L(T.Slots[I]);
			if (Str.IsEmpty())
			{
				continue;
			}
			const bool bHead = Str[0] == TEXT('~');
			if (bHead)
			{
				Str.RightChopInline(1);
			}
			FLinearColor C = bHead ? Gold : White;
			C.A = T.SlotAlpha(I);
			const float W = float(int32(D.TextInkWidth(EFont::Menu, Str, T.Scale)));
			D.Text(EFont::Menu, ListX - W * 0.5f, Yl, T.Scale, C, Str, ESSXAlign::Left, bHead, Tl);
		}
		return;
	}

	const TArray<FRow>& Rows = PageRows(S.Page);
	if (Rows.Num() == 0)
	{
		return;
	}
	auto ValueStr = [this](const FValue& V) { return V.Text ? FString(V.Text) : L(V.Loc); };
	const float CellH = D.FontHeight(EFont::Menu, 1.f) + ShadowOffset;
	float X = ListX;
	float CellW = 0.f;
	if (S.Page == 1)
	{
		for (const FRow& R : Rows) { CellW = FMath::Max(CellW, EntryGap + (R.Values.Num() ? MenuW(ValueStr(R.Values[0]), 1.f) : 0.f)); }
		X = ListX - CellW * 0.5f;
	}
	else if (S.Page == 3)
	{
		for (const FRow& R : Rows) { CellW = FMath::Max(CellW, MenuW(L(R.Loc), 1.f)); }
		X = ListX - CellW * 0.5f;
	}
	const float Pitch = CellH + (S.Page == 1 ? CtrlSpacing : PageSpacing);
	const float Top = Y0 + MenuTopY(S.Page, Rows.Num(), CellH);
	const int32 Sel = O.RowSel[S.Page];

	if (S.Page == 1)
	{
		// FUN_002A36E8 / FUN_002A9488 / FUN_002A9350
		const float Cx = (ScreenW - CtrlBodyW * CtrlIconScale) * 0.5f;
		const float Cy = Y0 + CtrlOriginY;
		for (const FCtrlIcon& Ic : CtrlIcons())
		{
			D.Sprite(Ic.Sprite, Cx + Ic.X, Cy + Ic.Y, Ic.W * CtrlIconScale, Ic.H * CtrlIconScale, White, SpriteLayer(TitleBarLayer + 1));
		}
		for (int32 G = 0; G < 3; ++G)
		{
			if (G < 2 && G != O.CtrlDiagram)
			{
				continue;
			}
			for (const FCtrlLabel& Lb : CtrlLabels(G))
			{
				const FString Str = L(Lb.Loc);
				const float W = MenuW(Str, CtrlLabelScale);
				const float H = D.FontHeight(EFont::Menu, CtrlLabelScale) + ShadowOffset;
				float Lx = Cx + Lb.X, Ly = Cy + Lb.Y;
				if (Lb.Flags & 0x8) { Lx -= W; } else if (Lb.Flags & 0x20) { Lx -= W * 0.5f; }
				if (Lb.Flags & 0x4) { Ly -= H; } else if (Lb.Flags & 0x40) { Ly -= H * 0.5f; }
				D.Text(EFont::Menu, Lx, Ly, CtrlLabelScale, White, Str, ESSXAlign::Left, true, Tl);
			}
		}
	}

	// FUN_002ADE40
	auto ArrowColor = [&S, &O](int32 Side)
	{
		const int32 F = S.Frame - O.ArrowFlashFrame[Side];
		return F >= 0 && F < ArrowFlashFrames ? FMath::Lerp(ArrowFlash, White, F / (float)ArrowFlashFrames) : White;
	};
	const int32 ArrowLayer = SpriteLayer(TitleBarLayer + 4);
	for (int32 I = 0; I < Rows.Num(); ++I)
	{
		const FRow& R = Rows[I];
		const float Y = Top + I * Pitch;
		const bool bHi = I == Sel;
		const FLinearColor C = bHi ? Gold : White;
		if (R.Kind == EKind::Item)
		{
			const FString Str = L(R.Loc);
			D.Text(EFont::Menu, X + (CellW - MenuW(Str, 1.f)) * 0.5f, Y, 1.f, C, Str, ESSXAlign::Left, true, Tl);
			continue;
		}
		const FString Label = R.Text ? FString(R.Text) : L(R.Loc);
		D.Text(EFont::Menu, X - MenuW(Label, 1.f), Y, 1.f, C, Label, ESSXAlign::Left, true, Tl);
		const float Vx = X + EntryGap;
		if (R.Kind == EKind::List)
		{
			const int32 Idx = FMath::Clamp(O.List[S.Page][I], 0, R.Values.Num() - 1);
			const FString Val = ValueStr(R.Values[Idx]);
			const float Vw = MenuW(Val, 1.f);
			D.Text(EFont::Menu, Vx, Y, 1.f, C, Val, ESSXAlign::Left, true, Tl);
			if (bHi)
			{
				// FUN_0026D8C8
				const float Ay = Y + CellH * 0.5f - OptArrowSize * 0.5f;
				D.Sprite(SprArrowL, Vx - OptArrowSize - OptArrowGap, Ay, OptArrowSize, OptArrowSize, ArrowColor(0), ArrowLayer);
				D.Sprite(SprArrowR, Vx + Vw + OptArrowGap, Ay, OptArrowSize, OptArrowSize, ArrowColor(1), ArrowLayer);
			}
		}
		else if (R.Kind == EKind::Bar)
		{
			const float V = O.Bar[S.Page][I];
			const float Frac = FMath::Clamp(V, 0.f, 1.f);
			D.Sprite(SprSliderTrack, Vx, Y, R.BarW, BarH, White, SpriteLayer(TitleBarLayer + 1));
			if (Frac > 0.f)
			{
				D.SpriteCropU(SprSliderFill, Vx, Y, R.BarW * Frac, BarH, Frac, White, SpriteLayer(TitleBarLayer + 3), false);
			}
			if (bHi)
			{
				// FUN_002AE3C0
				const float Ay = Y + BarH * 0.5f - OptArrowSize * 0.5f;
				if (0.f < V) { D.Sprite(SprArrowL, Vx - (OptArrowSize + OptArrowGap), Ay, OptArrowSize, OptArrowSize, ArrowColor(0), ArrowLayer); }
				if (V < 1.f) { D.Sprite(SprArrowR, Vx + R.BarW + OptArrowGap, Ay, OptArrowSize, OptArrowSize, ArrowColor(1), ArrowLayer); }
			}
		}
	}
}

void FSSXFrontEnd::DrawStartBox(FSSXFeDrawList& D, const FString& Msg, uint32 Buttons, bool bCentred) const
{
	// FUN_00183538 / FUN_00196070 / FUN_00196088 / FUN_001961E8
	constexpr float BoxX = 112.f, BoxY = 96.f, BoxW = 416.f;
	const float Pitch = D.FontHeight(EFont::Menu, 1.f) + 3.f + 4.f;
	TArray<FString> Lines;
	{
		TArray<FString> Paras;
		Msg.Replace(TEXT("\\"), TEXT("\n")).ParseIntoArray(Paras, TEXT("\n"), false);
		for (const FString& P : Paras)
		{
			TArray<FString> Words;
			P.ParseIntoArray(Words, TEXT(" "), true);
			FString Line;
			for (const FString& W : Words)
			{
				const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
				if (!Line.IsEmpty() && D.TextInkWidth(EFont::Menu, Try, 1.f) > BoxW)
				{
					Lines.Add(Line);
					Line = W;
				}
				else
				{
					Line = Try;
				}
			}
			Lines.Add(Line);
		}
	}
	float Y = BoxY;
	for (const FString& Line : Lines)
	{
		if (!Line.IsEmpty())
		{
			const float W = D.TextInkWidth(EFont::Menu, Line, 1.f);
			D.Text(EFont::Menu, BoxX + (BoxW - W) * 0.5f, Y, 1.f, White, Line, ESSXAlign::Left, false, TextLayer(1));
		}
		Y += Pitch;
	}
	if (Buttons == 0)
	{
		return;
	}
	// FUN_00183C20 / FUN_00183AE8
	auto Icon = [](uint32 Bit) { return (Bit == 1 || Bit == 2) ? IconCross : (Bit == 0x20 ? IconCircle : (Bit == 0x40 ? IconSquare : IconTriangle)); };
	auto Btn = [&](float X, float Yb, uint32 Bit, int32 LocId) -> float
	{
		const FString Label = L(LocId);
		D.Sprite(Icon(Bit), X, Yb, -1.f, -1.f, White, SpriteLayer(1));
		D.Text(EFont::Menu, X + 35.f, Yb, 1.f, White, Label, ESSXAlign::Left, false, TextLayer(1));
		return float(int32(D.TextInkWidth(EFont::Menu, Label, 1.f))) + 60.f;
	};
	auto InkI = [&](int32 LocId) { return float(int32(D.TextInkWidth(EFont::Menu, L(LocId), 1.f))); };
	const int32 N = ((Buttons & 3) ? 1 : 0) + ((Buttons & 0x58) ? 1 : 0) + ((Buttons & 0x24) ? 1 : 0);
	// FUN_00183788
	const int32 FirstLoc = (Buttons & 1) ? 341 : 3226;
	const int32 LastLoc = (Buttons & 0x10) ? 340 : ((Buttons & 8) ? 3227 : 354);
	float X = 160.f, Yr = 340.f;
	if (bCentred)
	{
		float Wsum = 0.f;
		if (N == 3) { Wsum = InkI(FirstLoc) + InkI(442) + InkI(LastLoc); }
		else if (N == 2) { Wsum = InkI(FirstLoc) + InkI((Buttons & 0x24) ? 442 : LastLoc); }
		else if (N == 1)
		{
			const int32 OneLoc = (Buttons & 2) ? 3226 : ((Buttons & 1) ? 341 : ((Buttons & 8) ? 3227 : ((Buttons & 0x10) ? 340 : ((Buttons & 4) ? 442 : 354))));
			Wsum = InkI(OneLoc);
		}
		X = 320.f - (Wsum + N * 35.f + (N - 1) * 25.f) * 0.5f;
	}
	if (N == 3)
	{
		const float S1 = Btn(X, 340.f, (Buttons & 1) ? 1 : 2, FirstLoc);
		if (bCentred) { X += S1; } else { Yr = 370.f; }
		const float S2 = Btn(X, Yr, (Buttons & 4) ? 4 : 0x20, 442);
		if (bCentred) { X += S1 + S2; } else { Yr += 30.f; }
		Btn(X, Yr, (Buttons & 0x10) ? 0x10 : ((Buttons & 8) ? 8 : 0x40), LastLoc);
	}
	else if (N == 2)
	{
		const float S1 = Btn(X, 340.f, 1, FirstLoc);
		if (bCentred) { X += S1; } else { Yr = 370.f; }
		if (Buttons & 4) { Btn(X, Yr, 4, 442); }
		else if (Buttons & 0x20) { Btn(X, Yr, 0x20, 442); }
		else if (Buttons & 0x10) { Btn(X, Yr, 0x10, 340); }
		else if (Buttons & 8) { Btn(X, Yr, 8, 3227); }
		else { Btn(X, Yr, 0x40, 354); }
	}
	else if (N == 1)
	{
		if (Buttons & 1) { Btn(X, 340.f, 1, 341); }
		if (Buttons & 2) { Btn(X, 340.f, 2, 3226); }
		else if (Buttons & 8) { Btn(X, 340.f, 8, 3227); }
		else if (Buttons & 0x10) { Btn(X, 340.f, 0x10, 340); }
		else if (Buttons & 4) { Btn(X, 340.f, 4, 442); }
		else { Btn(X, 340.f, 0x40, 354); }
	}
}

void FSSXFrontEnd::DrawBoot(FSSXFeDrawList& D) const
{
	if (Boot == ESSXFeBoot::StartScreen)
	{
		// 0x185430
		if (BootState != 1)
		{
			DrawStartBox(D, L(BootState >= 6 && BootState <= 8 ? 327 : 376), 0, true);
		}
	}
}

void FSSXFrontEnd::Draw(FSSXFeDrawList& D) const
{
	if (!MovieRequest.IsEmpty())
	{
		// 0x264FD8
		return;
	}
	DrawScreens(D);
	// 0x264FD8
	if (Fader.bActive && (Boot == ESSXFeBoot::FrontEnd || Boot == ESSXFeBoot::Loading))
	{
		// FUN_001FF000
		FLinearColor C = Fader.Color;
		C.A = Fader.Alpha;
		D.Rect(0.f, 0.f, ScreenW, ScreenH, C, SpriteLayer(FadeLayer));   // FUN_0018DF60 / FUN_0014F5F0
	}
}

void FSSXFrontEnd::DrawScreens(FSSXFeDrawList& D) const
{
	if (Boot == ESSXFeBoot::Loading)
	{
		D.RectScreen(0.f, 0.f, ScreenW, ScreenH, Black, 0);
		return;
	}
	if (Boot != ESSXFeBoot::FrontEnd)
	{
		DrawBoot(D);
		return;
	}
	const FSSXFeState* S = Top();
	if (!S)
	{
		return;
	}
	if (S->CloseFrame >= 0 && S->IsSlidOut())
	{
		return;
	}
	switch (S->Screen)
	{
	case ESSXFeScreen::Title:
		DrawStandard(D, *S);
		break;
	case ESSXFeScreen::CharacterSelect:
		DrawStandard(D, *S);
		DrawCharPanel(D, *S, SSXFeTables::CarouselRider(S->Sel), BottomY(*S));
		break;
	case ESSXFeScreen::CustomBoard:
		DrawStandard(D, *S);
		if (Profile && Profile->Slot[S->Player].Char >= 0)
		{
			// TODO: its flags 8 and the per-board bar width update FUN_002755E8.
			const int32 Char = Profile->Slot[S->Player].Char;
			const FSSXFeItem* Sel = S->Selected();
			if (!Sel || Sel->bEnabled)
			{
				DrawStatRows(D, Char, S->SelectedId(), BottomY(*S));
			}
			const int32 TypeLoc = SSXFeTables::BoardTypeLoc(SSXFeTables::Board(Char, FMath::Max(S->SelectedId(), 0)).Type);
			if (TypeLoc >= 0)
			{
				D.Text(EFont::Menu, 20.f, BottomY(*S) + 18.f, 0.8f, White, L(TypeLoc), ESSXAlign::Left, true, TextLayer(FrameLayer + 8));
			}
		}
		break;
	case ESSXFeScreen::UserName:
		DrawUserName(D, *S);
		break;
	case ESSXFeScreen::RiderProfile:
		DrawRiderProfile(D, *S);
		break;
	case ESSXFeScreen::TrickBook:
		DrawTrickBook(D, *S);
		break;
	case ESSXFeScreen::DVDContent:
		S->Dvd.Draw(D, Loc);
		break;
	case ESSXFeScreen::Options:
		DrawOptions(D, *S);
		break;
	case ESSXFeScreen::MemCard:
		// FUN_002718F0
		DrawTitleBar(D, 450.f, BarY(*S));
		DrawFrame(D, *S, MemCard.FeTitleLoc(), MemCard.HelpMask());
		if (Loc) { MemCard.DrawFe(D, *Loc, S->Frame); }
		break;
	default:
		DrawStandard(D, *S);
		break;
	}
}
