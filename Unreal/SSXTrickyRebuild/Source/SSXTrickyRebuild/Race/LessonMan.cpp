#include "Race/LessonMan.h"

#include "UI/SSXFeTrickBook.h"

namespace SSXLesson
{
	const TCHAR* LexToString(EState S)
	{
		static const TCHAR* Names[] = { TEXT("None"), TEXT("Init"), TEXT("Intro"), TEXT("Start"), TEXT("Wait"), TEXT("Preview"),
			TEXT("PreviewEnd"), TEXT("Perform"), TEXT("Practice"), TEXT("PracticeEnd"), TEXT("End"), TEXT("Exit") };
		const int32 I = int32(S);
		return I >= 0 && I < UE_ARRAY_COUNT(Names) ? Names[I] : TEXT("?");
	}

	int32 FirstLesson(int32 Chapter)
	{
		// FUN_00164110
		return Chapter >= 0 && Chapter <= 6 ? Chapter * LessonsPerChapter : 0;
	}

	uint32 NarrationWord(int32 Step, uint32& InOutFlags)
	{
		// FUN_00231398, 0x14E6A8, 0x230388, 0x228BD8
		switch (Step)
		{
		case 0: if (InOutFlags & 2u) return 0u; InOutFlags |= 2u; return 1u;
		case 1: if (InOutFlags & 4u) return 0u; InOutFlags |= 4u; return 2u;
		case 2: if (InOutFlags & 8u) return 0u; InOutFlags |= 8u; return 2u;
		case 3: return 4u;
		case 4: return 8u;
		case 5: return 0x10u;
		case 6: return 0x20u;
		case 7: return 0x40u;
		default: return 0u;
		}
	}

	const FGlyph* DiagramGlyphs()
	{
		// 0x31DF88
		static const FGlyph G[NumDiagramGlyphs] = {
			{ 29.f, 9.f, 0x7F, 0x200 }, { 192.f, 9.f, 0x81, 0x400 }, { 34.f, 0.f, 0x80, 0x1000 }, { 185.f, 0.f, 0x82, 0x2000 },
			{ 22.f, 66.f, 0x77, 0x8 }, { 51.f, 66.f, 0x78, 0x4 }, { 38.f, 76.f, 0x7A, 0x2 }, { 36.f, 50.f, 0x79, 0x1 },
			{ 177.f, 65.f, 0x86, 0x10 }, { 198.f, 46.f, 0x84, 0x20 }, { 216.f, 64.f, 0x83, 0x40 }, { 194.f, 82.f, 0x85, 0x800 },
			{ 94.f, 62.f, 0x7B, 0x100 }, { 142.f, 62.f, 0x7C, 0x80 },
		};
		return G;
	}

	uint32 DiagramMask(const FInpFrame& F, TFunctionRef<uint32(int32 Action)> ActionMask)
	{
		// FUN_0014BD98
		uint32 M = 0;
		for (int32 K = 0; K < FPadMapper::NumBoolActions; ++K)
		{
			if ((F.RecordedBools >> K) & 1u)
			{
				M |= ActionMask(FPadMapper::FirstBoolAction + K);
			}
		}
		for (int32 K = 0; K < FPadMapper::GrabCount; ++K)
		{
			const uint32 A = ActionMask(K);
			if (((F.RecordedGrabs >> K) & 1u) && A != 0xFFFFFFFFu)
			{
				M |= A;
			}
		}
		M |= F.Pad.ButtonsFiltered & 0xFu;
		const uint32 T = M & ActionMask(FPadMapper::ActTweak);
		int32 N = 0, First = -1;
		for (int32 B = 0; B < 32; ++B)
		{
			if ((T >> B) & 1u)
			{
				++N;
				if (First < 0) First = B;
			}
		}
		if (N > 1 && First >= 0)
		{
			M = (M & ~T) | (1u << First);
		}
		return M;
	}

	bool LessonBookCheck(FLessonMan& M, int32 BookChar, uint32 W0, uint32 W1)
	{
		const SSXFeTrickBook::FEntry* E = SSXFeTrickBook::FTable::Get().Find(BookChar, M.Lesson);
		if (!E || !SSXFeTrickBook::Matches(*E, W0, W1))
		{
			return false;
		}
		M.MarkPassed();
		return true;
	}

	void FLessonMan::ResetFields(ILessonEnv& Env)
	{
		// FUN_0014B5A8
		Prev = EState::None;
		State = EState::None;
		Requested = EState::None;
		bEnd = false;
		MenuSel = MenuCount = MenuSaved = 0;
		bPassed = false;
		FMemory::Memzero(MenuItems);
		Request(Env, EState::Init);
	}

	void FLessonMan::Construct(int32 InRider)
	{
		// FUN_0014AAA0, 0x17E148
		*this = FLessonMan();
		ILessonEnv Null;
		ResetFields(Null);
		Lesson = 0;
		Chapter = 0;
		MenuLevel = Column = Scroll = 0;
		DiagramPosX = DiagramX;
		DiagramPosY = DiagramY;
		Rider = InRider;
	}

	void FLessonMan::Request(ILessonEnv& Env, EState S)
	{
		// FUN_0014BAA8, FUN_00174C48
		if (S == EState::Exit)
		{
			Env.CameraFlash();
			OpenOverlay(OvLesson, false);
		}
		Requested = S;
	}

	void FLessonMan::Update(ILessonEnv& Env)
	{
		// 0x14B9B8, 0x337E08
		if (Env.IsBlocked())
		{
			return;
		}
		++Frames;
		EState PendingExit = EState::None;
		if (Requested != EState::None)
		{
			// FUN_0014BB08
			if (Requested != State)
			{
				Prev = State;
				PendingExit = State;
				State = Requested;
			}
			Requested = EState::None;
		}
		if (Prev != State)
		{
			if (PendingExit != EState::None)
			{
				Exit(Env, PendingExit);
			}
			Frames = 0;
			Enter(Env, State);
			Prev = State;
		}
		Tick(Env, State);
	}

	void FLessonMan::Setup(ILessonEnv& Env)
	{
		// FUN_0014B768
		bEnd = false;
		bPassed = false;
		PracticeLoc = 0;
		if (Chapter == 5)
		{
			Env.SetBoost(1.0, 20.0);
		}
		else
		{
			Env.SetBoost(0.0, 0.0);
		}
	}

	uint32 FLessonMan::ReadPad(ILessonEnv& Env)
	{
		// FUN_0014D6E0
		const uint32 Bits = PadEdges;
		PadEdges = 0;
		if (MenuLevel == 2)
		{
			if (Bits & PadUp)
			{
				if (MenuSel >= 1)
				{
					Env.Sound(ESound::Move);
					--MenuSel;
				}
			}
			else if (Bits & PadDown)
			{
				if (MenuSel < MenuCount - 1)
				{
					Env.Sound(ESound::Move);
					++MenuSel;
				}
			}
		}
		return Bits;
	}

	void FLessonMan::WaitMenu()
	{
		// FUN_0014DAA8
		if (MenuLevel == 0 || MenuLevel == 1)
		{
			MenuSel = 0;
			MenuCount = 1;
			MenuItems[0] = LocSelect;
		}
		else if (MenuLevel == 2)
		{
			MenuCount = 4;
			MenuSel = MenuSaved;
			FMemory::Memzero(MenuItems);
			MenuItems[0] = LocShowMe;
			MenuItems[1] = LocPerform;
			MenuItems[2] = LocFreeJump;
			MenuItems[3] = LocQuit;
		}
	}

	void FLessonMan::EndMenu(bool bPreview)
	{
		MenuSel = 0;
		FMemory::Memzero(MenuItems);
		if (bPreview)
		{
			// FUN_0014D948
			MenuCount = 4;
			MenuItems[0] = LocNewTrick;
			MenuItems[1] = LocPerform;
			MenuItems[2] = LocRestart;
			MenuItems[3] = LocQuit;
		}
		else
		{
			// FUN_0014D9C8, FUN_0014DA38
			MenuCount = 3;
			MenuItems[0] = LocNewTrick;
			MenuItems[1] = LocRestart;
			MenuItems[2] = LocQuit;
		}
	}

	void FLessonMan::LoadLesson(ILessonEnv& Env)
	{
		// FUN_002C8BC8
		Inp.Reset();
		bInpMissing = !Env.LoadInp(Rider, Lesson, Inp);
		InpFrames = Inp.Num();
		InpIndex = 0;
	}

	void FLessonMan::NewTrick(ILessonEnv& Env)
	{
		Env.ResetWorld();                              // FUN_0014D7D0
		MenuLevel = 1;
		Env.SetRacePhase(ERacePhase::RestartLesson);
	}

	void FLessonMan::Restart(ILessonEnv& Env)
	{
		// FUN_0014B638
		Env.ResetWorld();
		InpIndex = 0;
		OpenOverlay(OvLesson, false);
		OpenOverlay(OvController, true);
		Env.SetRacePhase(ERacePhase::Countdown);
		switch (State)
		{
		case EState::Preview:
		case EState::Perform:
		case EState::Practice:
			Setup(Env);
			return;
		case EState::PreviewEnd: Request(Env, EState::Preview); return;
		case EState::PracticeEnd: Request(Env, EState::Practice); return;
		case EState::End: Request(Env, EState::Perform); return;
		default: Request(Env, EState::Preview); return;
		}
	}

	bool FLessonMan::InjectPad(FSSXPadFrame& InOut) const
	{
		// 0x14B7C8
		if (State != EState::Preview)
		{
			return false;
		}
		InOut = Inp.IsValidIndex(InpIndex) ? Inp[InpIndex].Pad : FSSXPadFrame();
		return true;
	}

	void FLessonMan::AfterPad()
	{
		// 0x14B8A8
		if ((State == EState::Preview || State == EState::Perform) && InpIndex < InpFrames)
		{
			++InpIndex;
		}
	}

	void FLessonMan::Enter(ILessonEnv& Env, EState S)
	{
		switch (S)
		{
		case EState::Intro:
			// 0x14DBA8, FUN_00175DE0
			Env.StopSpeech();
			Env.Narration(NarrIntro);
			OpenOverlay(OvLesson, true);
			break;
		case EState::Wait:
			// 0x14DC88
			WaitMenu();
			PanelH = WaitPanelH;
			break;
		case EState::Preview:
			// 0x14E138
			Env.UseTutorialMapping(true);
			bTutorialMapping = true;
			Setup(Env);
			Env.StopSpeech();
			Env.Narration(NarrPreview);
			Env.Sound(ESound::Accept);
			LoadLesson(Env);
			OpenOverlay(OvLesson, false);
			OpenOverlay(OvController, true);
			Env.SetRacePhase(ERacePhase::Countdown);
			break;
		case EState::PreviewEnd:
			// 0x14E2E0
			OpenOverlay(OvLesson, true);
			OpenOverlay(OvController, false);
			Env.SetRacePhase(ERacePhase::EndLesson);   // 0x1117F0
			Env.ClearFeKey();
			Env.StampFinish();                          // 0x11DA40
			EndMenu(true);
			PanelH = ShortPanelH;
			break;
		case EState::Perform:
			// 0x14E558
			Setup(Env);
			Env.Sound(ESound::Accept);
			Env.StopSpeech();
			Env.Narration(NarrPerform);
			LoadLesson(Env);
			OpenOverlay(OvLesson, false);
			OpenOverlay(OvController, true);
			Env.SetRacePhase(ERacePhase::Countdown);
			break;
		case EState::Practice:
			// 0x14E6D0
			Setup(Env);
			PracticeLoc = PracticeLocs[((Env.Rand() % 3) + 3) % 3];
			Env.ClearTrickText();
			Env.StopSpeech();
			OpenOverlay(OvLesson, false);
			OpenOverlay(OvController, true);
			Env.SetRacePhase(ERacePhase::Countdown);
			break;
		case EState::PracticeEnd:
			// 0x14E838
			OpenOverlay(OvLesson, true);
			OpenOverlay(OvController, false);
			Env.SetRacePhase(ERacePhase::EndLesson);
			Env.ClearFeKey();
			Env.StampFinish();
			EndMenu(false);
			PanelH = ShortPanelH;
			break;
		case EState::End:
			// 0x14EA20
			OpenOverlay(OvLesson, true);
			OpenOverlay(OvController, false);
			Env.SetRacePhase(ERacePhase::EndLesson);
			Env.ClearFeKey();
			Env.StampFinish();
			EndMenu(false);
			PanelH = EndPanelH;
			break;
		default:
			break;   // 0x14DBA0, 0x14DC30, 0x14EBE8
		}
	}

	void FLessonMan::Exit(ILessonEnv& Env, EState S)
	{
		switch (S)
		{
		case EState::Preview:
			// 0x14E2B0
			Env.UseTutorialMapping(false);
			bTutorialMapping = false;
			break;
		case EState::Perform:
			Env.Narration(bPassed ? NarrPassed : NarrFailed);
			break;
		default:
			break;   // 0x14F348, 0x14E830
		}
	}

	void FLessonMan::Tick(ILessonEnv& Env, EState S)
	{
		switch (S)
		{
		case EState::Intro:
			Requested = EState::Start;   // 0x14DC20
			break;
		case EState::Start:
			if (Env.CameraIdle())
			{
				Request(Env, EState::Wait);   // 0x14DC38
			}
			break;
		case EState::Wait:
		{
			// 0x14DCC0
			if (bQuitDialog)
			{
				// 0x1AAC08
				const uint32 Bits = PadEdges;
				PadEdges = 0;
				int32 R = 0;
				if (Bits & (PadTriangle | PadSelect)) R = 2;
				else if (Bits & PadCross) R = 1;
				if (R == 0)
				{
					return;
				}
				bQuitDialog = false;
				if (R == 1)
				{
					Env.Sound(ESound::Accept);
					Request(Env, EState::Exit);
				}
				else
				{
					Env.Sound(ESound::Back);   // 0x1AAC60
				}
				return;
			}
			const uint32 Bits = ReadPad(Env);
			if (MenuLevel == 1)
			{
				if (Bits & PadLeft)
				{
					if (Lesson <= FirstLesson(Chapter)) return;
					Env.Sound(ESound::Move);
					--Lesson;
					if (Column > 0) --Column; else --Scroll;
					return;
				}
				if (Bits & PadRight)
				{
					if (FirstLesson(Chapter + 1) - 1 <= Lesson) return;
					Env.Sound(ESound::Move);
					++Lesson;
					if (Column < 4) ++Column; else ++Scroll;
					return;
				}
				if (Bits & PadCross)
				{
					Env.Sound(ESound::Move);
					MenuLevel = 2;
					WaitMenu();
					return;
				}
				if (!(Bits & PadTriangle)) return;
				Env.Sound(ESound::Back);
				MenuLevel = 0;
				WaitMenu();
				return;
			}
			if (MenuLevel == 0)
			{
				if (Bits & PadLeft)
				{
					if (Chapter < 1) return;
					Env.Sound(ESound::Move);
					--Chapter;
				}
				else if (Bits & PadRight)
				{
					if (Chapter > 4) return;
					Env.Sound(ESound::Move);
					++Chapter;
				}
				else
				{
					if (!(Bits & PadCross)) return;
					Env.Sound(ESound::Move);
					MenuLevel = 1;
					WaitMenu();
					return;
				}
				const int32 First = FirstLesson(Chapter);
				Column = 0;
				Scroll = First;
				Lesson = First;
				return;
			}
			if (MenuLevel != 2) return;
			if (Bits & PadTriangle)
			{
				Env.Sound(ESound::Back);
				MenuLevel = 1;
				WaitMenu();
				return;
			}
			if (!(Bits & PadCross)) return;
			switch (MenuSel)
			{
			case 0: Env.Sound(ESound::Accept); Request(Env, EState::Preview); break;
			case 1: Env.Sound(ESound::Accept); Request(Env, EState::Perform); break;
			case 2: Env.Sound(ESound::Accept); Request(Env, EState::Practice); break;
			case 3: Env.Sound(ESound::Accept); bQuitDialog = true; break;   // 0x1AA090
			default: break;
			}
			MenuSaved = MenuSel;
			return;
		}
		case EState::Preview:
			// 0x14E270
			if (InpIndex >= InpFrames)
			{
				bEnd = true;
			}
			if (bEnd)
			{
				Requested = EState::PreviewEnd;
			}
			break;
		case EState::PreviewEnd:
		{
			// 0x14E388
			const uint32 Bits = ReadPad(Env);
			if (!(Bits & PadCross)) return;
			switch (MenuSel)
			{
			case 0:
				Env.Sound(ESound::Accept);
				NewTrick(Env);
				break;
			case 1:
				Env.Sound(ESound::Accept);
				Env.ResetWorld();
				InpIndex = 0;
				OpenOverlay(OvLesson, false);
				OpenOverlay(OvController, true);
				Env.SetRacePhase(ERacePhase::Countdown);
				Request(Env, EState::Perform);
				break;
			case 2:
				Env.Sound(ESound::Accept);
				Restart(Env);
				break;
			case 3:
				Env.Sound(ESound::Accept);
				Request(Env, EState::Exit);
				break;
			default:
				break;
			}
			return;
		}
		case EState::Perform:
			if (bEnd)
			{
				Requested = EState::End;   // 0x14E688
			}
			break;
		case EState::Practice:
			// 0x14E7C0
			if (bEnd)
			{
				Request(Env, EState::PracticeEnd);
				return;
			}
			if (Env.HasTrickText())
			{
				bPassed = true;
			}
			break;
		case EState::PracticeEnd:
		case EState::End:
		{
			// 0x14E900, 0x14EAC8
			const uint32 Bits = ReadPad(Env);
			if (!(Bits & PadCross)) return;
			switch (MenuSel)
			{
			case 0:
				Env.Sound(ESound::Accept);
				NewTrick(Env);
				break;
			case 1:
				Env.Sound(ESound::Accept);
				Restart(Env);
				break;
			case 2:
				Env.Sound(ESound::Accept);
				Request(Env, EState::Exit);
				break;
			default:
				break;
			}
			return;
		}
		case EState::Exit:
			// 0x14EBF0
			Env.QuitRace();
			OpenOverlay(OvLesson, false);
			OpenOverlay(OvController, false);
			break;
		default:
			break;
		}
	}
}
