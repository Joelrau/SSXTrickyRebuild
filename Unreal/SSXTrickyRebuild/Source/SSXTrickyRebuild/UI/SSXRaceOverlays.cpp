#include "UI/SSXRaceOverlays.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SSXTrickyRebuild.h"
#include "Race/SSXReplay.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "UI/SSXMemCard.h"
#include "UI/SSXRaceUIHost.h"

using namespace SSXFe;

namespace
{
	TMap<TWeakObjectPtr<const UWorld>, ISSXRaceUIHost*>& Hosts()
	{
		static TMap<TWeakObjectPtr<const UWorld>, ISSXRaceUIHost*> M;
		return M;
	}

	constexpr int32 PhPreRace = 1, PhCountdown = 3, PhRace = 4, PhEndRace = 5, PhLoadLesson = 6;

	// 0x38E328
	enum { ActContinue = 1, ActRestart = 4, ActOptions = 5, ActTrickBook = 6, ActQuit = 7 };
	enum { DlgRestart = 1, DlgQuit = 2, DlgQuitSave = 3, DlgSetupChar = 4, DlgPoints = 5, DlgReplayExit = 6 };

}

void SSXRaceUI::SetHost(const UWorld* World, ISSXRaceUIHost* Host)
{
	if (Host) { Hosts().Add(World, Host); } else { Hosts().Remove(World); }
}

ISSXRaceUIHost* SSXRaceUI::GetHost(const UWorld* World)
{
	ISSXRaceUIHost** H = Hosts().Find(World);
	return H ? *H : nullptr;
}

const TCHAR* LexToString(ESSXOverlay O)
{
	static const TCHAR* Names[] = { TEXT("HUD"), TEXT("RaceResults"), TEXT("HiScoreRecord"), TEXT("NextRace"), TEXT("Countdown"),
		TEXT("ReplayControl"), TEXT("PauseMenu"), TEXT("PopFinish"), TEXT("PopAsyncLoad"), TEXT("Replay"), TEXT("Lesson"),
		TEXT("ViewHiScores"), TEXT("BlackFade"), TEXT("Unlocked"), TEXT("LessonController"), TEXT("PointDistribution"),
		TEXT("SelectNextVenue"), TEXT("SelectNextEvent"), TEXT("HelpRace") };
	return O >= 0 && O < OvCount ? Names[O] : TEXT("?");
}

void FSSXOvDialog::Show(int32 InContext, int32 InLine1, int32 InX, int32 InCircle, int32 InTriangle, int32 InLine2)
{
	bActive = true;
	Context = InContext;
	Line1 = InLine1;
	Line2 = InLine2;
	LocX = InX;
	LocCircle = InCircle;
	LocTriangle = InTriangle;
}

int32 FSSXOvDialog::Result(uint32 Bits) const
{
	// 0x1AAC08
	if (!bActive) return 0;
	if ((Bits & PadCircle) && LocCircle >= 0) return 3;
	if ((Bits & (PadTriangle | PadSelect)) && LocTriangle >= 0) return 2;
	if ((Bits & PadCross) && LocX >= 0) return 1;
	return 0;
}

void FSSXOverlayManager::Init(FSSXProfile* InProfile, FSSXCheats* InCheats, const FSSXLocStrings* InLoc)
{
	Profile = InProfile;
	Cheats = InCheats;
	Loc = InLoc;
	FMemory::Memzero(Active);
	PauseDepth = -1;
	// 0x1AB9F8
	if (Profile)
	{
		Profile->Session.bQuit = false;
		Profile->Session.bNextVenue = false;
		Profile->Session.bRankUp = false;
	}
	Active[OvHUD] = true;
}

FString FSSXOverlayManager::L(int32 Id) const { return Loc ? Loc->ByIndex(Id) : FString(); }
int32 FSSXOverlayManager::Mode() const { return Host ? Host->GetEventMode() : 1; }

void FSSXOverlayManager::Activate(int32 Idx, int32 Player)
{
	if (Idx < 0 || Idx >= OvCount) return;
	const bool bFirst = !Active[Idx];
	Active[Idx] = true;
	if (!bFirst) return;
	ActiveFrame[Idx] = 0;
	Blink[Idx] = 0;
	// 0x1ABC00
	Backdrop[Idx].Open();
	switch (Idx)
	{
	case OvRaceResults:
		ResultsSel = 0;
		if (Host) Host->PostRaceNarration(0, 0);   // 0x1AF720 / 0x231C50
		break;
	case OvSelectNextEvent:
	{
		const int32 M = Mode();
		NextEventSel = M == 7 ? 2 : ((M == 3 || M == 5) ? 1 : 0);
		break;
	}
	case OvSelectNextVenue:
		NextVenueSel = 0;
		break;
	case OvPointDistribution:
		PointSel = 0;   // 0x166A58
		// 0x1BA208 / 0x166580 / 0x164840
		if (Host && Profile && Profile->Session.bRankUp) Host->PostRaceNarration(3, Profile->Session.RankAfter);
		break;
	case OvUnlocked:
		BuildUnlockRows();   // 0x1B5EF8
		if (Host) Host->PostRaceNarration(2, 0);   // 0x1B5EF8 / 0x232330
		break;
	case OvPopAsyncLoad:
		AsyncFrames = 0;
		break;
	case OvReplay:
		// 0x1EF990 / 0x1EC548
		ReplaySel = 0;
		ReplayState = 1;
		Active[OvReplayControl] = true;
		ActiveFrame[OvReplayControl] = 0;
		if (FSSXReplay* R = Host ? Host->GetReplay() : nullptr)
		{
			ReplayCtl = FSSXReplayControl();
			ReplayCtl.Activate(*R, Host->GetReplayPadRacer(), R->IsAdvanced());
			ReplayPrevHeld = PadHeld[0];
		}
		break;
	default:
		break;
	}
}

void FSSXOverlayManager::Deactivate(int32 Idx)
{
	if (Idx < 0 || Idx >= OvCount) return;
	if (Idx == OvPointDistribution && Active[Idx] && Host) Host->PostRaceNarration(4, 0);   // 0x1BA2F0
	Active[Idx] = false;
	Backdrop[Idx].bOpen = false;
}

const TArray<FSSXOverlayManager::FPauseItem>& FSSXOverlayManager::PauseItems()
{
	static const TArray<FPauseItem> Items = {
		{ 475, ActContinue }, { 480, ActRestart }, { 477, ActOptions }, { 478, ActQuit }, { 495, ActTrickBook },
	};
	return Items;
}

TArray<int32> FSSXOverlayManager::PauseOptionRows() const
{
	// 0x38E380
	const int32 Players = Host ? Host->GetNumHumanPlayers() : 1;
	TArray<int32> R = { 483, 484 };
	if (Players >= 1) R.Add(481);
	if (Players >= 2) R.Add(482);
	R.Append({ 489, 486, 490, 488, 487, 3225 });
	return R;
}

int32 FSSXOverlayManager::OptionValue(int32 RowLoc) const
{
	// 0x1AD198 / 0x1ADD28
	const FSSXSettings* S = Profile ? &Profile->Settings : nullptr;
	auto Trunc50 = [](float Level) { return FMath::Clamp(FMath::TruncToInt(Level * 50.f + 1e-4f), 0, 50); };
	switch (RowLoc)
	{
	case 483: return S ? FMath::Clamp(FMath::RoundToInt(S->Brightness * 50.f), 0, 50) : 25;
	case 484: return GetHudDetail();
	case 481: case 482: return Host ? Host->GetCameraView(RowLoc == 481 ? 0 : 1) : -1;
	case 486: return S ? Trunc50(S->Master) : 50;
	case 490: return S ? Trunc50(S->McSpeech) : 50;
	case 488: return S ? Trunc50(S->Sfx) : 50;
	case 487: return S ? Trunc50(S->Music) : 50;
	case 3225: return S ? Trunc50(S->CharSpeech) : 50;
	default: return 0;
	}
}

void FSSXOverlayManager::OptionsPad(uint32 Bits)
{
	// 0x1ADD28 / 0x1A87A0
	const TArray<int32> Rows = PauseOptionRows();
	const bool bUp = (Bits & (PadUp | 0x20000)) != 0, bDown = (Bits & (PadDown | 0x40000)) != 0;
	const bool bLeft = (Bits & (PadLeft | 0x100000)) != 0, bRight = (Bits & (PadRight | 0x80000)) != 0;
	if (!bLeft && !bRight)
	{
		if (bUp) { OptionsSel = (OptionsSel + Rows.Num() - 1) % Rows.Num(); Sounds.Add(SSXUISound::ERace::Navigate); }
		else if (bDown) { OptionsSel = (OptionsSel + 1) % Rows.Num(); Sounds.Add(SSXUISound::ERace::Navigate); }
		return;
	}
	if (!Rows.IsValidIndex(OptionsSel))
	{
		return;
	}
	const int32 D = bRight ? 1 : -1;
	const int32 Row = Rows[OptionsSel];
	FSSXSettings* S = Profile ? &Profile->Settings : nullptr;
	// 0x211DB0 / 0x212E98
	auto Volume = [&](float& Level, int32 Class)
	{
		const int32 Old = FMath::Clamp(FMath::TruncToInt(Level * 50.f + 1e-4f), 0, 50);
		// TODO: the setter's exact clamp bounds;
		Level = FMath::Clamp(Level + D / 11.f, 0.f, 1.f);
		const int32 New = FMath::Clamp(FMath::TruncToInt(Level * 50.f + 1e-4f), 0, 50);
		if (New != Old)
		{
			Sounds.Add(SSXUISound::ERace(int32(SSXUISound::ERace::PreviewMaster) + Class));
		}
		bAudioLevelsChanged = true;
		if (Profile) { Profile->bSettingsDirty = true; }
	};
	switch (Row)
	{
	case 483:
		if (S)
		{
			S->Brightness = FMath::Clamp(OptionValue(483) + D, 0, 50) / 50.f;
			Profile->bSettingsDirty = true;
		}
		Sounds.Add(SSXUISound::ERace::Accept);
		break;
	case 484:   // 0x17F168 / 0x32CA40
		SetHudDetail((GetHudDetail() + 3 + D) % 3);
		Sounds.Add(SSXUISound::ERace::Accept);
		break;
	case 481: case 482:   // 0x173E70
		if (Host)
		{
			Host->CycleCameraView(Row == 481 ? 0 : 1, D);
			// 0x173E08 / 0x32CA38 / 0x32CA3C / 0x180030
			if (Profile)
			{
				(Row == 481 ? Profile->Settings.Camera1P : Profile->Settings.Camera2P) = Host->GetCameraView(Row == 481 ? 0 : 1);
			}
		}
		Sounds.Add(SSXUISound::ERace::Accept);
		break;
	case 489:   // 0x225618 / 0x225660 / 0x215220
		if (Host) Host->StepSong(D);
		Sounds.Add(SSXUISound::ERace::Navigate);
		break;
	case 486: if (S) { Volume(S->Master, 0); } break;
	case 487: if (S) { Volume(S->Music, 1); } break;
	case 490: if (S) { Volume(S->McSpeech, 2); } break;
	case 3225: if (S) { Volume(S->CharSpeech, 3); } break;
	case 488: if (S) { Volume(S->Sfx, 4); } break;
	default: break;
	}
}

uint32 FSSXOverlayManager::RepeatWord(uint32 Held, bool& bChanged)
{
	// 0x1A87A0 / 0x1A8688
	bChanged = false;
	if (Held == 0)
	{
		RepeatLast = 0;
		RepeatCount = 0;
		return 0;
	}
	if (Held == RepeatLast)
	{
		++RepeatCount;
		if (RepeatCount < 0x3D)
		{
			return RepeatCount % 12 == 0 ? Held : 0;
		}
		return (RepeatCount & 1) == 0 ? Held : 0;
	}
	RepeatLast = Held;
	RepeatCount = 0;
	bChanged = true;
	return Held;
}

void FSSXOverlayManager::TickPauseRepeat()
{
	if (!Active[OvPauseMenu] || PauseDepth != 1 || bTrickBook || Dialog.bActive)
	{
		RepeatLast = 0;
		RepeatCount = 0;
		return;
	}
	bool bChanged = false;
	const uint32 R = RepeatWord(PadHeld[FMath::Clamp(PausePlayer, 0, 1)], bChanged);
	const uint32 LeftRight = R & (PadLeft | PadRight | 0x100000 | 0x80000);
	if (!bChanged && LeftRight)
	{
		OptionsPad(LeftRight);
	}
}

int32 FSSXOverlayManager::GetHudDetail() const
{
	return Profile ? Profile->Settings.HudDetail : LocalHudDetail;
}

void FSSXOverlayManager::SetHudDetail(int32 V)
{
	if (!Profile)
	{
		LocalHudDetail = V;
		return;
	}
	if (V != Profile->Settings.HudDetail)
	{
		Profile->Settings.HudDetail = V;
		Profile->bSettingsDirty = true;
	}
}

FString FSSXOverlayManager::PauseOptionValue(int32 RowLoc) const
{
	switch (RowLoc)
	{
	case 484:
	{
		static const int32 Detail[] = { 472, 474, 473 };   // 0x17F158
		return L(Detail[FMath::Clamp(GetHudDetail(), 0, 2)]);
	}
	case 481: case 482:
	{
		static const int32 Cam[] = { 330, 333, 332, 334 };
		const int32 V = OptionValue(RowLoc);
		return L(V >= 0 && V < 4 ? Cam[V] : 333);
	}
	case 489:
		return Host ? Host->GetSongName() : FString();
	default:
		return FString();
	}
}

float FSSXOverlayManager::PauseOptionSlider(int32 RowLoc) const
{
	if (!Profile)
	{
		return -1.f;
	}
	const FSSXSettings& S = Profile->Settings;
	switch (RowLoc)
	{
	case 483: return S.Brightness;
	case 486: return S.Master;
	case 490: return S.McSpeech;
	case 488: return S.Sfx;
	case 487: return S.Music;
	case 3225: return S.CharSpeech;
	default: return -1.f;
	}
}

bool FSSXOverlayManager::CanPause() const
{
	if (AnyModal()) return false;
	if (!Host) return true;
	const int32 Ph = Host->GetRacePhase();
	if (Ph == PhPreRace || Ph == PhLoadLesson) return false;
	if (Ph == PhEndRace) { const int32 Sub = Host->GetGameSubState(); return Sub >= 1 && Sub <= 9; }
	return true;
}

void FSSXOverlayManager::Pad(int32 Player, uint32 Bits)
{
	if (Bits == 0) return;
	// 0x110AB8
	if (Host && Host->GetIntroCards() >= 0 && !Active[OvPauseMenu] && (Bits & (PadStart | PadCross))
		&& (Host->GetRacePhase() == PhPreRace || (Host->GetRacePhase() == PhEndRace && !bPostRaceStarted)))
	{
		Host->PressIntroSkip();
		return;
	}
	if (Active[OvPauseMenu])
	{
		if (Player == PausePlayer) PausePad(Bits);
		return;
	}
	if (Active[OvReplay])
	{
		if (ReplayState == 1)
		{
			// 0x181638 / 0x1EFDB0 / 0x1EF990
			if (Bits & PadStart)
			{
				Sounds.Add(SSXUISound::ERace::Accept);
				ReplayState = 2;
				ReplaySel = 1;
				if (Host) Host->SetReplayMenu(true);
			}
			return;
		}
		if (ReplayState == 0)
		{
			// 0x1CEEE0
			uint32 Card = 0;
			if (Bits & PadCross) Card |= FSSXMemCard::InAccept;
			if (Bits & PadTriangle) Card |= FSSXMemCard::InBack;
			if (Bits & PadCircle) Card |= FSSXMemCard::InCircle;
			if (Bits & PadStart) Card |= FSSXMemCard::InStart;
			if (Bits & (PadUp | 0x20000)) Card |= FSSXMemCard::InUp;
			if (Bits & (PadDown | 0x40000)) Card |= FSSXMemCard::InDown;
			ReplayCard.Input(Card);
			return;
		}
		if (ReplayState == 4)
		{
			// 0x1F00C8 / 0x1AAC08
			const int32 R = Dialog.Result(Bits);
			if (R != 1 && R != 2) return;
			Sounds.Add(R == 1 ? SSXUISound::ERace::Accept : SSXUISound::ERace::Back);
			Dialog.bActive = false;
			if (R == 1) { ReplayExit(); } else { ReplayState = 2; }
			return;
		}
		ReplayPad(Bits);
		return;
	}
	// 0x17A840
	if ((Bits & PadStart) && CanPause() && !Active[OvRaceResults] && !Active[OvSelectNextEvent] && !Active[OvSelectNextVenue]
		&& !Active[OvPointDistribution] && !Active[OvHelpRace])
	{
		PausePlayer = Player;
		PauseDepth = 0;
		PauseSel = 0;
		bTrickBook = false;
		Dialog.bActive = false;
		Activate(OvPauseMenu, Player);
		// 0x1ABC00 / 0x1641F8
		Backdrop[OvPauseMenu].OpenAnimated(0.88f);
		TrickPage = ChapterFirstPage(TrickBookChapter());
		if (Profile)
		{
			const int32 Char = FMath::Clamp(Profile->Slot[PausePlayer].Char, 0, SSXProg::NumRiders - 1);
			const uint32 Bits = Profile->Career[Char].TrickBits();
			int32 First = -1;
			for (int32 K = 0; K < 30 && First < 0; ++K) { if (!((Bits >> K) & 1)) First = K; }
			TrickPage = (First < 0 || First > 0x1D) ? ChapterFirstPage(5) : First;
		}
		if (Host) Host->SetPaused(true); else PendingAction = EAction::Pause;
		// 0x1AC410 / 0x2153F8 / 0x215458
		if (Host) Host->PauseMenuAudio(true);
		Sounds.Add(SSXUISound::ERace::PauseOpen);
		RepeatLast = 0;
		RepeatCount = 0;
		return;
	}
	if (Host && Host->GetLesson())
	{
		// FUN_0014D6E0 / 0x1AAC08
		Host->LessonPad(Bits);
		return;
	}
	// 0x110AB8
	if (Active[OvHelpRace] && (Bits & (PadCross | PadStart)))
	{
		Deactivate(OvHelpRace);
		if (Mode() != 1) Activate(OvNextRace);
		return;
	}
	if (Active[OvNextRace] && (Bits & (PadCross | PadStart)))
	{
		Deactivate(OvNextRace);
		return;
	}
	if (Dialog.bActive && !Active[OvPauseMenu])
	{
		const int32 R = Dialog.Result(Bits);
		if (R == 0) return;
		// 0x1AAC08
		Sounds.Add(R == 1 ? SSXUISound::ERace::Accept : SSXUISound::ERace::Back);
		const int32 Ctx = Dialog.Context;
		Dialog.bActive = false;
		// 0x1B32C0 / 0x1BC8A0 / 0x1BBB30
		if (Ctx == DlgQuit && R == 1) Quit();
		else if (Ctx == DlgQuitSave) { if (R == 1) { bQuitAfterSave = true; SaveSettingsInRace(); } else if (R == 3) Quit(); }
		else if (Ctx == DlgSetupChar && R == 1)
		{
			// 0x1BC8A0 / 0x161A98 / 0x113BB0 / 0x163AB0
			if (Profile)
			{
				Profile->ReloadSettings(Cheats && Cheats->bMaxAttributes);
				Profile->Session.Heat = 0;
			}
			Exit(0x10);
		}
		else if (Ctx == DlgPoints)
		{
			if (R == 1) { AfterSaveExit = -1; SaveSettingsInRace(); }
			if (R != 2) { AfterPoints(); }
		}
		return;
	}
	if (RankCardFrames > 0) return;
	if (Active[OvHiScoreRecord]) { HiScorePad(Bits); return; }
	if (Active[OvSelectNextVenue]) { NextVenuePad(Bits); return; }
	if (Active[OvSelectNextEvent]) { NextEventPad(Bits); return; }
	if (Active[OvPointDistribution]) { PointsPad(Bits); return; }
	if (Active[OvViewHiScores])
	{
		// 0x1B3EC8 / 0x1A8668
		if (Bits & PadTriangle) { Sounds.Add(SSXUISound::ERace::Accept); Deactivate(OvViewHiScores); Activate(OvRaceResults); }
		return;
	}
	if (Active[OvUnlocked])
	{
		// 0x1B6AC0
		if (Bits & PadCross) { Sounds.Add(SSXUISound::ERace::Accept); Deactivate(OvUnlocked); Activate(OvRaceResults); }
		return;
	}
	if (Active[OvRaceResults]) { ResultsPad(Bits); return; }
}

void FSSXOverlayManager::ClosePause(bool bResume)
{
	// 0x1AC580 / 0x225240
	if (PauseDepth > 0 && Host) Host->PauseSong();
	Deactivate(OvPauseMenu);
	PauseDepth = -1;
	bTrickBook = false;
	Dialog.bActive = false;
	// 0x1AC580 / 0x215488
	if (!bResume) return;
	if (Host) Host->SetPaused(false); else if (PendingAction == EAction::None) PendingAction = EAction::Resume;
	if (Host) Host->PauseMenuAudio(false);
}

void FSSXOverlayManager::QuitPrompt(int32 From)
{
	// 0x161928
	QuitFrom = From;
	if (Profile && Profile->bSettingsDirty) { Dialog.Show(DlgQuitSave, 444, 470, 392, 340); }
	else { Dialog.Show(DlgQuit, 326, 470, -1, 392); }
}

void FSSXOverlayManager::Quit()
{
	const bool bMax = Cheats && Cheats->bMaxAttributes;
	if (Profile)
	{
		switch (QuitFrom)
		{
		case QuitFromPause:
			// 0x1AC130 / 0x161A98 / 0x161840
			Profile->Session.bQuit = true;
			Profile->ReloadSettings(bMax);
			Profile->ResetEventState();
			break;
		case QuitFromResults:
			// 0x1B32C0 / 0x161A98 / 0x161840
			Profile->ReloadSettings(bMax);
			Profile->ResetEventState();
			break;
		default:
			// 0x1BC8A0 / 0x1BBB30 / 0x161A98 / 0x113BB0
			Profile->ReloadSettings(bMax);
			Profile->Session.Heat = 0;
			break;
		}
	}
	if (QuitFrom == QuitFromPause)
	{
		ClosePause(false);   // 0x1AC580
	}
	Exit(2);
}

void FSSXOverlayManager::PausePad(uint32 Bits)
{
	if (Dialog.bActive)
	{
		const int32 R = Dialog.Result(Bits);
		if (R) Sounds.Add(R == 1 ? SSXUISound::ERace::Accept : SSXUISound::ERace::Back);   // 0x1AAC08
		if (R) PauseDialogResult(R);
		return;
	}
	const bool bUp = (Bits & (PadUp | 0x20000)) != 0, bDown = (Bits & (PadDown | 0x40000)) != 0;
	if (PauseDepth == 1 && !bTrickBook && (Bits & (PadTriangle | PadSelect)))
	{
		Sounds.Add(SSXUISound::ERace::Back);   // 0x1A8658
		if (Bits & PadTriangle)
		{
			PauseDepth = 0;
			if (Host) Host->PauseSong();   // 0x1AC580 / 0x225240
		}
		else { ClosePause(); }
		return;
	}
	if (Bits & (PadTriangle | PadSelect))
	{
		// 0x1ACE00 / 0x1A8658
		if (PauseDepth <= 0) Sounds.Add(SSXUISound::ERace::Back);
		if (bTrickBook) { bTrickBook = false; }
		else if (PauseDepth > 0) { PauseDepth = 0; if (Host) Host->PauseSong(); }
		else { ClosePause(); }
		return;
	}
	if (bTrickBook)
	{
		// 0x1ACE00 / 0x100008 / FUN_00164110 / 0x1AC7E0
		const int32 Ch = TrickBookChapter();
		if (Bits & (PadLeft | 0x100000))
		{
			if (TrickPage > ChapterFirstPage(Ch)) { Sounds.Add(SSXUISound::ERace::Navigate); --TrickPage; }
		}
		else if (Bits & (PadRight | 0x80000))
		{
			if (TrickPage < ChapterFirstPage(Ch + 1) - 1) { Sounds.Add(SSXUISound::ERace::Navigate); ++TrickPage; }
		}
		return;
	}
	if (PauseDepth == 1)
	{
		OptionsPad(Bits);
		return;
	}
	// 0x1ACE00
	const TArray<FPauseItem>& Items = PauseItems();
	if (bUp) { PauseSel = (PauseSel + Items.Num() - 1) % Items.Num(); Sounds.Add(SSXUISound::ERace::Navigate); }
	else if (bDown) { PauseSel = (PauseSel + 1) % Items.Num(); Sounds.Add(SSXUISound::ERace::Navigate); }
	else if (Bits & PadCross)
	{
		Sounds.Add(SSXUISound::ERace::Accept);
		switch (Items[PauseSel].Action)
		{
		case ActContinue: ClosePause(); break;
		case ActRestart: Dialog.Show(DlgRestart, 326, 470, -1, 392); break;
		case ActOptions: PauseDepth = 1; OptionsSel = 0; RepeatLast = 0; RepeatCount = 0; break;
		case ActTrickBook: bTrickBook = true; break;
		case ActQuit: QuitPrompt(QuitFromPause); break;
		}
	}
}

void FSSXOverlayManager::PauseDialogResult(int32 R)
{
	const int32 Ctx = Dialog.Context;
	Dialog.bActive = false;
	switch (Ctx)
	{
	case DlgRestart:
		if (R == 1)
		{
			// 0x1AC130 / 0x165400 / 0x180A10 / 0x14B638
			if (Profile) { for (int32 S = 0; S < 2; ++S) Profile->DropEventTricks(S); }
			ClosePause();
			if (Host) Host->RequestRestart(); else PendingAction = EAction::Restart;
		}
		break;
	case DlgQuit:
		if (R == 1) Quit();
		break;
	case DlgQuitSave:
		// 0x165400 / 0x1D21C0
		if (R == 1)
		{
			if (Profile) { for (int32 S = 0; S < 2; ++S) Profile->DropEventTricks(S); }
			ClosePause(false);
			bQuitAfterSave = true;
			SaveSettingsInRace();
		}
		else if (R == 3) Quit();
		break;
	default:
		break;
	}
}

void FSSXOverlayManager::Exit(int32 Code)
{
	if (Host) Host->RequestExit(Code); else PendingAction = EAction::QuitToFrontEnd;
}

void FSSXOverlayManager::SaveSettingsInRace()
{
	// 0x1CEE68 / 0x1CE8B0 / 0x1CE778
	if (Profile)
	{
		const FString Path = FSSXMemCard::FilePath(FSSXMemCard::SettingsName());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveArrayToFile(Profile->SaveBlob(), *Path);
		Profile->bSettingsDirty = false;
	}
	SaveFrames = 120;
	SaveWarnAlpha = 1.f;   // 0x1CE778
	SaveWarnDir = -1.f;
}

void FSSXOverlayManager::StartPostRace()
{
	// 0x1141E0
	bPostRaceStarted = true;
	// 0x164FC0 / 0x113E10
	if (Host && Host->HasPendingHiScore() && Profile && Profile->Session.PendingRecords.Num() > 0)
	{
		HiScorePending = 0;
		OpenHiScoreEntry();
		Activate(OvHiScoreRecord);
		return;
	}
	AfterHiScores();
}

void FSSXOverlayManager::AfterHiScores()
{
	// 0x1B7D98 / 0x1B6AC0
	TArray<FSSXUnlock> Unlocks;
	if (Host) Host->GetUnlocks(Unlocks);
	const bool bUnlock = Unlocks.ContainsByPredicate([](const FSSXUnlock& U) { return U.Type != SSXProg::UMedal; });
	Activate(bUnlock ? OvUnlocked : OvRaceResults);
}

void FSSXOverlayManager::OpenHiScoreEntry()
{
	// 0x164968
	HiGridX = HiGridY = 0;
	HiName.Reset();
	bHiReplace = true;   // 0x1B6D60
	if (Profile && Profile->Session.PendingRecords.IsValidIndex(HiScorePending))
	{
		const FSSXPendingRecord& E = Profile->Session.PendingRecords[HiScorePending];
		HiName = Profile->Records[E.Track].Tables[E.Kind].Rows[E.Row].GetName();
		if (Host) Host->PostRaceNarration(1, E.Row);   // 0x1B81B8 / 0x232240
	}
}

void FSSXOverlayManager::HiScorePad(uint32 Bits)
{
	// 0x38E550 / 0x3B7F58
	using namespace SSXFeUserName;
	// 0x1B7D98 / 0x1B6D60
	if (Bits & (PadUp | 0x20000)) { Sounds.Add(SSXUISound::ERace::Navigate); HiGridY = (HiGridY + Rows - 1) % Rows; }
	if (Bits & (PadDown | 0x40000)) { Sounds.Add(SSXUISound::ERace::Navigate); HiGridY = (HiGridY + 1) % Rows; }
	if (Bits & (PadLeft | 0x100000)) { Sounds.Add(SSXUISound::ERace::Navigate); HiGridX = (HiGridX + Cols - 1) % Cols; }
	if (Bits & (PadRight | 0x80000)) { Sounds.Add(SSXUISound::ERace::Navigate); HiGridX = (HiGridX + 1) % Cols; }
	if (!(Bits & PadCross)) return;
	Sounds.Add(SSXUISound::ERace::Accept);
	if (!Profile || !Profile->Session.PendingRecords.IsValidIndex(HiScorePending)) return;
	const FSSXPendingRecord& E = Profile->Session.PendingRecords[HiScorePending];
	FSSXRecordRow& Row = Profile->Records[E.Track].Tables[E.Kind].Rows[E.Row];
	const TCHAR C = Cell(HiGridX, HiGridY);
	if (C == TEXT('d'))
	{
		Sounds.Add(SSXUISound::ERace::Back);
		bHiReplace = false;
		HiName.LeftChopInline(1);
	}
	else if (C == TEXT('e'))
	{
		bHiReplace = true;
		Row.SetName(HiName);
		Profile->bSettingsDirty = true;
		if (++HiScorePending < Profile->Session.PendingRecords.Num()) { OpenHiScoreEntry(); return; }
		Deactivate(OvHiScoreRecord);
		AfterHiScores();
		return;
	}
	else
	{
		if (bHiReplace) { bHiReplace = false; HiName.Reset(); }
		if (HiName.Len() < SSXProg::MaxNameChars) HiName.AppendChar(C);
		else HiName[HiName.Len() - 1] = C;
	}
	Row.SetName(HiName);
}

FSSXPointSession* FSSXOverlayManager::PointSession() const
{
	return Profile ? &Profile->Session.Points[0] : nullptr;
}

void FSSXOverlayManager::AfterPoints()
{
	Deactivate(OvPointDistribution);
	if (Profile && Profile->Session.bRankUp)
	{
		RankCardFrames = 300;
		return;
	}
	Activate(OvSelectNextEvent);
}

TArray<int32> FSSXOverlayManager::ResultsItems() const
{
	switch (Mode())
	{
	case 1: return { 0, 2, 1, 3 };
	case 2: case 9: return { 0, 2, 1, 4, 3 };
	case 7: return { 0, 2, 1, 3 };
	case 6: return { 2, 3 };
	case 4:
	{
		// 0x1AF950 / 0x1635B0 / 0x164368
		const bool bPending = PointSession() && PointSession()->Left > 0;
		TArray<int32> R;
		if (Profile && Profile->Session.Heat >= 2)
		{
			R.Add(0);
			if (!bPending) R.Add(2);
			R.Add(1);
			R.Add(4);
			if (!bPending) R.Add(3);
			return R;
		}
		if (!(Host && Host->IsEliminated())) R.Add(0);
		R.Append({ 2, 1, 4, 3 });
		return R;
	}
	default:
	{
		// 0x1B0BB8 / 0x1618E0
		if (Host && Host->IsCircuitBeaten()) return { 0, 1, 4 };
		if (Mode() == 3) return { 0, 2, 1, 4, 3 };
		const bool bPending = PointSession() && PointSession()->Left > 0;
		return bPending ? TArray<int32>{ 0, 1, 4 } : TArray<int32>{ 0, 2, 1, 4 };
	}
	}
}

void FSSXOverlayManager::ResultsPad(uint32 Bits)
{
	if (Host && Host->IsResultsMenuHidden()) return;
	const TArray<int32> Items = ResultsItems();
	if (Items.Num() == 0) return;
	// 0x1B32C0
	if (!(Bits & PadCross))
	{
		if (Bits & (PadUp | 0x20000)) { Sounds.Add(SSXUISound::ERace::Navigate); ResultsSel = (ResultsSel + Items.Num() - 1) % Items.Num(); }
		else if (Bits & (PadDown | 0x40000)) { Sounds.Add(SSXUISound::ERace::Navigate); ResultsSel = (ResultsSel + 1) % Items.Num(); }
		return;
	}
	Sounds.Add(SSXUISound::ERace::Accept);
	const int32 M = Mode();
	switch (Items[ResultsSel])
	{
	case 1:   // 0x1EF990
		if (Host && Host->RequestReplay())
		{
			Deactivate(OvRaceResults);
			ReplayReturn = OvRaceResults;
			Activate(OvReplay);
		}
		break;
	case 2:   // 0x180A10
		Deactivate(OvRaceResults);
		if (Host) Host->RequestRestart(); else PendingAction = EAction::Restart;
		break;
	case 3:
		// 0x1B32C0
		QuitPrompt(QuitFromResults);
		ResultsSel = 0;
		break;
	case 4:
		Deactivate(OvRaceResults);
		Activate(OvViewHiScores);
		break;
	case 0:
		Deactivate(OvRaceResults);
		if (M == 1)
		{
			NextVenueMode = 1;
			bNextVenueBackToResults = true;
			Activate(OvSelectNextVenue);
		}
		else if (M == 2 || M == 3 || M == 7)
		{
			bNextEventBack = true;
			bNextVenueBackToResults = false;
			Activate(OvSelectNextEvent);
		}
		else if (M == 4 && Profile && Profile->Session.Heat < 2)
		{
			if (Host) Host->RequestNextHeat();
		}
		else
		{
			const bool bPoints = PointSession() && PointSession()->Left > 0;   // 0x164368
			bNextEventBack = !bPoints;
			Activate(bPoints ? OvPointDistribution : OvSelectNextEvent);
		}
		break;
	}
}

void FSSXOverlayManager::NextEventPad(uint32 Bits)
{
	// 0x1BC8A0
	const bool bWC = Mode() == 4 || Mode() == 5;
	if (Bits & (PadUp | 0x20000)) { Sounds.Add(SSXUISound::ERace::Navigate); NextEventSel = (NextEventSel + 4) % 5; return; }
	if (Bits & (PadDown | 0x40000)) { Sounds.Add(SSXUISound::ERace::Navigate); NextEventSel = (NextEventSel + 1) % 5; return; }
	if (!(Bits & PadCross))
	{
		if ((Bits & PadTriangle) && bNextEventBack)
		{
			Sounds.Add(SSXUISound::ERace::Back);
			Deactivate(OvSelectNextEvent);
			Activate(OvRaceResults);
		}
		return;
	}
	if (NextEventSel == 2 && bWC) return;
	Sounds.Add(SSXUISound::ERace::Accept);
	switch (NextEventSel)
	{
	case 0: NextVenueMode = bWC ? 4 : 2; break;
	case 1: NextVenueMode = bWC ? 5 : 3; break;
	case 2: NextVenueMode = 7; break;
	case 3: Dialog.Show(DlgSetupChar, 361, 470, -1, 392); return;
	case 4: QuitPrompt(QuitFromNext); return;
	}
	bNextVenueBackToResults = false;
	Deactivate(OvSelectNextEvent);
	Activate(OvSelectNextVenue);
}

TArray<int32> FSSXOverlayManager::NextVenueList() const
{
	// 0x3352F0 / 0x335370 / 0x37EE18
	TArray<int32> L = { 0, 1, 2, 3, 4, 8, 5, 11 };
	if (NextVenueMode == 3) L.Add(6);
	if (NextVenueMode == 1) { L.Add(7); L.Add(6); }
	L.Add(-1);
	L.Add(-2);
	return L;
}

void FSSXOverlayManager::NextVenuePad(uint32 Bits)
{
	const TArray<int32> List = NextVenueList();
	auto Step = [&List, this](int32 D)
	{
		do { NextVenueSel = (NextVenueSel + D + List.Num()) % List.Num(); } while (List[NextVenueSel] == -1);
	};
	// 0x1BBB30
	if (Bits & (PadUp | 0x20000)) { Sounds.Add(SSXUISound::ERace::Navigate); Step(-1); }
	if (Bits & (PadDown | 0x40000)) { Sounds.Add(SSXUISound::ERace::Navigate); Step(1); }
	if (Bits & PadCross)
	{
		const int32 T = List[NextVenueSel];
		if (T == -2) { QuitPrompt(QuitFromNext); return; }
		const bool bLocked = Profile && !Profile->IsVenueUnlocked(NextVenueMode, T, Cheats && Cheats->bUnlockAll);   // 0x163BB8
		if (!bLocked)
		{
			if (Host) Host->RequestNextVenue(NextVenueMode, T);
			return;
		}
	}
	if (Bits & PadTriangle)
	{
		Deactivate(OvSelectNextVenue);
		Activate(bNextVenueBackToResults ? OvRaceResults : OvSelectNextEvent);
	}
}

void FSSXOverlayManager::PointsPad(uint32 Bits)
{
	// 0x164398 / 0x1644E8 / 0x1BA330 / 0x1A8638
	if (Bits & (PadUp | 0x20000)) { Sounds.Add(SSXUISound::ERace::PointChange); PointSel = (PointSel + 3) % 4; }
	if (Bits & (PadDown | 0x40000)) { Sounds.Add(SSXUISound::ERace::PointChange); PointSel = (PointSel + 1) % 4; }
	FSSXPointSession* PS = PointSession();
	if (Bits & (PadLeft | 0x100000))
	{
		const bool bOk = PS && SSXProg::PointRemove(*Profile, *PS, PointSel);
		if (bOk) Profile->bSettingsDirty = true;
		Sounds.Add(bOk ? SSXUISound::ERace::PointChange : SSXUISound::ERace::Denied);
	}
	if (Bits & (PadRight | 0x80000))
	{
		const bool bOk = PS && SSXProg::PointAdd(*Profile, *PS, PointSel);
		if (bOk) Profile->bSettingsDirty = true;
		Sounds.Add(bOk ? SSXUISound::ERace::PointChange : SSXUISound::ERace::Denied);
	}
	if (Bits & PadCross)
	{
		// 0x1BA788
		if (!PS || PS->Left == 0) { Dialog.Show(DlgPoints, 449, 470, 392, 340); }
	}
}

void FSSXOverlayManager::Tick()
{
	++Frame;
	for (int32 I = 0; I < OvCount; ++I)
	{
		if (!Active[I]) continue;
		++ActiveFrame[I];
		Backdrop[I].Tick();
		// 0x1AB148
		if (++Blink[I] > 0x36) Blink[I] = 0;
	}
	if (Active[OvPopAsyncLoad]) ++AsyncFrames;
	TickPauseRepeat();
	if (Active[OvReplayControl]) TickReplayControl();
	if (Host && !Active[OvReplay] && Host->TakeLoadedReplayStart() && Host->RequestReplay())
	{
		// 0x180D08 / 0x1A7848 / 0x1EF990 / 0x1EF7F0
		ReplayReturn = -1;
		// 0x11A7D0
		Deactivate(OvHUD);
		Activate(OvReplay);
	}
	if (Active[OvReplay] && ReplayState == 0)
	{
		// 0x1EF750 / 0x1CEEE0 / 0x1D21A0
		ReplayCard.Tick();
		Sounds.Append(ReplayCard.Sounds);
		ReplayCard.Sounds.Reset();
		if (!ReplayCard.IsActive()) { ReplayState = 2; ReplayCtl.bHidden = false; }   // 0x1EF990
	}
	if (Active[OvUnlocked])
	{
		// 0x1F12D0
		bool bAllDone = true;
		for (int32 I = 0; I < UnlockValue.Num(); ++I)
		{
			if (I + 1 != UnlockStep) continue;
			if (UnlockValue[I] < UnlockTarget[I]) ++UnlockValue[I];
			bAllDone &= UnlockValue[I] >= UnlockTarget[I];
		}
		if (bAllDone && UnlockStep <= UnlockValue.Num()) ++UnlockStep;
	}
	if (RankCardFrames > 0 && --RankCardFrames == 0)
	{
		Exit(0x30);
	}
	if (SaveFrames > 0)
	{
		// 0x1CEEE0
		SaveWarnAlpha += SaveWarnDir / 60.f;
		if (SaveWarnAlpha <= 0.2f) { SaveWarnAlpha = 0.2f; SaveWarnDir = 1.f; }
		if (SaveWarnAlpha >= 1.f) { SaveWarnAlpha = 1.f; SaveWarnDir = -1.f; }
	}
	if (SaveFrames > 0 && --SaveFrames == 0)
	{
		if (bQuitAfterSave)
		{
			bQuitAfterSave = false;
			Quit();   // 0x1D21C0
		}
		else if (AfterSaveExit > 0)
		{
			Exit(AfterSaveExit);
			AfterSaveExit = 0;
		}
	}
	if (!Host) return;
	const int32 Ph = Host->GetRacePhase();
	if (Ph == PhPreRace && LastPhase != PhPreRace && !Active[OvReplay])
	{
		// 0x112B88 / 0x1A7758 / 0x1A7E70 / 0x180A10
		bPostRaceStarted = false;
		GoFrames = -1;
		for (int32 I = OvHUD; I < OvCount; ++I)
		{
			if (I != OvPauseMenu) Deactivate(I);   // 0x1AC130
		}
		for (FFinish& F : Finish) F.bOn = false;
	}
	// 0x1AE3B8 / 0x113520
	const int32 Opens = Host->GetCountdownOpens();
	bool bCountdownOpen = Host->GetCountdownTimer() < 0.0;
	if (Opens >= 0)
	{
		bCountdownOpen = Opens > CountdownOpensSeen;
		CountdownOpensSeen = Opens;
	}
	if (Ph == PhCountdown && (Active[OvCountdown] || bCountdownOpen))
	{
		if (!Active[OvCountdown]) { Activate(OvCountdown); GoFrames = 90; }
		const double X = Host->GetCountdownTimer() / 60.0;
		const double Tr = FMath::TruncToDouble(X);
		CountdownDigit = 1 - (int32)Tr;
		CountdownScale = (float)(1.0 - X + Tr);
	}
	else if (Active[OvCountdown] || GoFrames >= 0)
	{
		if (Active[OvCountdown]) { Deactivate(OvCountdown); }
		--GoFrames;
		CountdownDigit = 0;
		CountdownScale = GoFrames >= 0 ? 1.f - GoFrames / 90.f : 0.f;
	}
	if (const int32 Cards = Host->GetIntroCards(); Cards >= 0)
	{
		if ((Cards & 1) && !Active[OvHelpRace]) Activate(OvHelpRace);
		if (!(Cards & 1) && Active[OvHelpRace]) Deactivate(OvHelpRace);
		if ((Cards & 2) && !Active[OvNextRace]) Activate(OvNextRace);
		if (!(Cards & 2) && Active[OvNextRace]) Deactivate(OvNextRace);
	}
	else if (Ph == PhPreRace && Host->WantsHelpRace() && !Active[OvHelpRace] && !Active[OvNextRace] && LastPhase != PhPreRace)
	{
		Activate(OvHelpRace);
	}
	if (Ph == PhEndRace && LastPhase != PhEndRace)
	{
		// 0x113E10 / 0x1A7860
		Deactivate(OvHUD);
		Deactivate(OvPopFinish);
		for (FFinish& F : Finish) F.bOn = false;
	}
	// 0x113520 / 0x11A7D0 / 0x190878
	const int32 HudOpens = Host->GetHudOpens();
	const bool bHudOpen = HudOpens >= 0 ? HudOpens > HudOpensSeen : (Ph == PhCountdown && LastPhase != PhCountdown && !Active[OvReplay]);
	if (HudOpens >= 0) { HudOpensSeen = HudOpens; }
	if (bHudOpen && !Active[OvHUD])
	{
		Activate(OvHUD);
	}
	const bool bEndRace = Ph == PhEndRace;
	LastPhase = Ph;
	if (!bPostRaceStarted && !bEndRace && !Host->IsPostRaceReady() && Profile && Host->GetTrackId() >= 0 && Host->GetTrackId() < SSXProg::NumTracks)
	{
		TimeToBeatCs = Profile->Records[Host->GetTrackId()].Tables[2].Rows[0].Value;
	}
	// 0x1AE7A8 / 0x1AE7F0
	int32 P = 0, R = 0;
	FString V;
	while (Host->ConsumeFinish(P, R, V))
	{
		if (Host->IsLesson()) continue;
		FFinish& F = Finish[FMath::Clamp(P, 0, 1)];
		F = FFinish();
		F.bOn = true;
		F.Delay = 8;
		const int32 M = Mode();
		F.bScored = R >= 0;
		switch (M)
		{
		case 2: case 4:
			// 0x16BB58
			F.bPlace = M == 2 || (Profile && Profile->Session.Heat == 2);
			F.PlaceChar = TCHAR(TEXT('1') + FMath::Max(R, 0));
			F.Icon = R == 0 ? HudMedalGold : (R == 1 ? HudMedalSilver : (R == 2 ? HudMedalBronze : -1));
			F.Value = V;
			break;
		case 3: case 5:
			if (F.bScored)
			{
				F.Value = V;
				if (NumHumans() < 2)
				{
					const int32 Medal = SSXProg::ShowoffMedal(FCString::Atoi(*V), Host->GetTrackId());   // 0x16BAA8
					if (Medal >= 0 && Medal <= 2)
					{
						F.PlaceChar = TCHAR(TEXT('1') + Medal);
						F.Icon = Medal == 0 ? HudMedalGold : (Medal == 1 ? HudMedalSilver : HudMedalBronze);
						F.bPlace = true;
					}
				}
			}
			break;
		case 7:
		{
			F.bTC = true;
			TArray<FSSXRacerResult> Rows;
			Host->GetResults(Rows);
			for (const FSSXRacerResult& Row : Rows)
			{
				if (Row.Player == P) { F.TimeCs = Row.TimeSec >= 0.0 ? (int32)(Row.TimeSec * 100.0 + 0.5) : 0; }
			}
			break;
		}
		case 9:
			F.bPlace = true;
			F.Value = V;
			break;
		default:
			break;
		}
		Activate(OvPopFinish);
	}
	for (FFinish& F : Finish)
	{
		if (!F.bOn) continue;
		if (F.Delay > 0) --F.Delay; else F.Alpha = FMath::Min(1.f, F.Alpha + 0.05f);
	}
	if (Host->IsAsyncLoading()) Activate(OvPopAsyncLoad); else Deactivate(OvPopAsyncLoad);
	if (Host->GetRiderFade(0) > 0.f) Activate(OvBlackFade); else Deactivate(OvBlackFade);
	if (!bPostRaceStarted && Host->IsPostRaceReady())
	{
		// 0x1FF4C8
		Deactivate(OvPopFinish);
		for (FFinish& F : Finish) F.bOn = false;
		StartPostRace();
	}
}


int32 FSSXOverlayManager::NumHumans() const
{
	return Host ? FMath::Max(1, Host->GetNumHumanPlayers()) : 1;   // FUN_00164288
}

int32 FSSXOverlayManager::TrickBookChapter() const
{
	// FUN_00164060 / FUN_00164020
	if (!Profile) return 0;
	const int32 Char = FMath::Clamp(Profile->Slot[PausePlayer].Char, 0, SSXProg::NumRiders - 1);
	int32 N = 0;
	while (N < SSXProg::TrickBookChapters && Profile->Career[Char].ChapterDone(N)) ++N;
	return FMath::Min(N, 5);
}

void FSSXOverlayManager::BuildUnlockRows()
{
	// 0x1B5EF8 / FUN_001B5BF8 / 0x338268 / 0x338240
	UnlockRows.Reset();
	UnlockPlayers.Reset();
	TArray<FSSXUnlock> U;
	if (Host) Host->GetUnlocks(U);
	static const int32 Order[] = { 5, 9, 8, 4, 3, 2, 1, 0, 6 };
	static const int32 Sing[] = { 571, 573, 567, 565, 563, 569, 3507, 3614, 554, 575 };
	static const int32 Plur[] = { 570, 572, 566, 564, 562, 568, 3508, 3615, 554, 574 };
	static const int32 Icons[] = { 0x58, 0x59, -1, 0x5A, 0x5B, 0x5C, 0x5D, -1, 0x5F, 0x5E };
	auto Fmt = [](FString F, const FString& A, const FString& B)
	{
		const int32 At = F.Find(TEXT("%d"));
		if (At != INDEX_NONE) F = F.Left(At) + A + F.Mid(At + 2);
		int32 S = F.Find(TEXT("%s"));
		if (S == INDEX_NONE) S = F.Find(TEXT("%S"));
		if (S != INDEX_NONE) F = F.Left(S) + B + F.Mid(S + 2);
		return F;
	};
	int32 RowBase = 0;
	for (int32 P = 0; P < 4; ++P)
	{
		TArray<FSSXUnlock> Mine = U.FilterByPredicate([P](const FSSXUnlock& X) { return X.Player == P; });
		if (Mine.Num() == 0) continue;
		UnlockPlayers.Add(P);
		while (UnlockRows.Num() < RowBase) UnlockRows.AddDefaulted();
		for (int32 T : Order)
		{
			int32 Count = 0, Value = 0;
			for (const FSSXUnlock& X : Mine)
			{
				if (X.Type != T) continue;
				if (T == 9) { Count = X.Value; } else { ++Count; }
				Value = X.Value;
			}
			if (Count < 1) continue;
			FUnlockRow Row;
			Row.Icon = Icons[T];
			const FString N = FString::FromInt(Count);
			if (Count >= 2)
			{
				Row.Text = Fmt(L(Plur[T]), N, FString());
			}
			else if (T == 0 || T == 1)
			{
				Row.Text = Fmt(L(Sing[T]), N, L(SSXFeTables::TrackNameLoc(Value)));
			}
			else if (T == 2)
			{
				if (Value != SSXProg::TrackPipedream && Value != SSXProg::TrackUntracked) continue;
				Row.Text = Fmt(L(Sing[T]), N, L(SSXFeTables::TrackNameLoc(Value)));
				Row.Icon = Value == SSXProg::TrackPipedream ? 0x58 : -1;
			}
			else if (T == 3)
			{
				Row.Text = Fmt(L(Sing[T]), N, SSXFeTables::RiderName(Value));
			}
			else if (T == 5)
			{
				int32 Ch = 0;
				if (Profile)
				{
					const int32 Char = FMath::Clamp(Profile->Slot[P].Char, 0, SSXProg::NumRiders - 1);
					while (Ch < SSXProg::TrickBookChapters && Profile->Career[Char].ChapterDone(Ch)) ++Ch;
				}
				Row.Text = Fmt(Fmt(L(Sing[T]), N, FString()), FString::FromInt(Ch), FString());
			}
			else
			{
				Row.Text = Fmt(L(Sing[T]), N, FString());
			}
			UnlockRows.Add(Row);
		}
		RowBase = 7;
	}
	UnlockStep = 0;
	UnlockValue.Init(0, UnlockRows.Num());
	UnlockTarget.SetNum(UnlockRows.Num());
	for (int32 I = 0; I < UnlockRows.Num(); ++I)
	{
		UnlockTarget[I] = UnlockRows[I].Text.Len() + (UnlockRows[I].Text.IsEmpty() ? 0 : 80);
	}
}

int32 FSSXOverlayManager::UnlockReveal(int32 Row) const
{
	return UnlockValue.IsValidIndex(Row) ? UnlockValue[Row] : 0;
}

void FSSXOverlayManager::ReplayPad(uint32 Bits)
{
	// FUN_001F01E8 / 0x1EFEF0 / 0x1EF990 / 0x1AA090
	auto Continue = [this]()
	{
		ReplayState = 1;
		ReplayCtl.bHidden = false;   // 0x1EF990
		bReplayMenuClosedThisFrame = true;
		if (Host) Host->SetReplayMenu(false);
	};
	if (Bits & (PadStart | PadTriangle))
	{
		Sounds.Add(SSXUISound::ERace::Back);
		Continue();
		return;
	}
	if (Bits & PadCross)
	{
		if (ReplaySel == 2)
		{
			Sounds.Add(SSXUISound::ERace::Back);
			Continue();
		}
		else
		{
			Sounds.Add(SSXUISound::ERace::Accept);
			const FSSXReplay* R = Host ? Host->GetReplay() : nullptr;
			if (ReplaySel == 1)
			{
				if (R && R->IsLoaded())
				{
					ReplayState = 4;   // 0x1EF990
					Dialog.Show(DlgReplayExit, 0x161, 0x1D6, -1, 0x188, 0x146);
				}
				else
				{
					ReplayExit();
				}
			}
			else if (ReplaySel == 0)
			{
				// 0x1EF990
				ReplayState = 0;
				ReplayCtl.bHidden = true;
				ReplayCard.Begin(FSSXMemCard::EMode::Replay, true, Profile);
				ReplayCard.bInRace = true;
				ReplayCard.ReplayFile.Reset();
				if (Host) { Host->BuildReplayFile(ReplayCard.ReplayFile); }
			}
		}
		return;
	}
	if (Bits & (PadUp | 0x20000))
	{
		if (ReplaySel > 0) { --ReplaySel; Sounds.Add(SSXUISound::ERace::Navigate); }
	}
	else if (Bits & (PadDown | 0x40000))
	{
		if (ReplaySel < 2) { ++ReplaySel; Sounds.Add(SSXUISound::ERace::Navigate); }
	}
}

void FSSXOverlayManager::ReplayExit()
{
	Deactivate(OvReplay);
	Active[OvReplayControl] = false;
	ReplayState = 1;
	if (Host) Host->StopReplay();
	if (ReplayReturn >= 0)
	{
		Activate(ReplayReturn);
	}
	else
	{
		// 0x2917B0 / 0x167060
		Exit(2);
	}
}

void FSSXOverlayManager::TickReplayControl()
{
	// 0x1EB6D0
	FSSXReplay* R = Host ? Host->GetReplay() : nullptr;
	if (!R)
	{
		return;
	}
	ReplayCtl.bBusy = (Active[OvReplay] && ReplayState != 1) || bReplayMenuClosedThisFrame;
	bReplayMenuClosedThisFrame = false;
	SSXReplayCtl::FEnv Env;
	ISSXRaceUIHost* H = Host;
	Env.CameraMode = [H]() { return H->GetReplayCameraMode(); };
	Env.CameraNudge = [H](float Amount, int32 Op) { H->ReplayCameraNudge(Amount, Op); };
	Env.NextCamera = [H]() { H->ReplayNextCamera(); };
	Env.Racers = Host->GetRacerCount();
	ReplayCtl.Update(*R, PadHeld[0], ReplayPrevHeld, Env);
	ReplayPrevHeld = PadHeld[0];
	for (const SSXReplayCtl::EEvent E : ReplayCtl.Events)
	{
		switch (E)
		{
		case SSXReplayCtl::EEvent::Navigate: Sounds.Add(SSXUISound::ERace::Navigate); break;
		case SSXReplayCtl::EEvent::HelpSound: Sounds.Add(SSXUISound::ERace::ReplayHelp); break;
		case SSXReplayCtl::EEvent::CameraSound: Sounds.Add(SSXUISound::ERace::ReplayCamera); break;
		case SSXReplayCtl::EEvent::SpeechStop: Host->ReplaySpeechStop(); break;
		case SSXReplayCtl::EEvent::AudioPause: Host->ReplayAudio(true); break;
		case SSXReplayCtl::EEvent::AudioResume: Host->ReplayAudio(false); break;
		case SSXReplayCtl::EEvent::BoardAudio: Host->ReplayTargetAudio(); break;   // 0x22D370 / 0x215370
		default: break;
		}
	}
	ReplayCtl.Events.Reset();
}

bool FSSXRaceUIHostStub::RequestReplay()
{
	if (bCanReplay && ReplayObj) { ReplayObj->StartFullReplay(); }
	return bCanReplay;
}
