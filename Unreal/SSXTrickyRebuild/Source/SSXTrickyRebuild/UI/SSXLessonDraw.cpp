// 0x14BBE8 / FUN_0014BD98 / 0x1B8548 / FUN_0014C2D0
#include "UI/SSXRaceOverlays.h"

#include "Input/PadMapper.h"
#include "Race/LessonMan.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXHudOverlay.h"
#include "UI/SSXRaceUIHost.h"

using namespace SSXFe;

namespace
{
	const FLinearColor LessonGold = FeColor(1.f, 0.953f, 0.729f, 0.106f);
	const FLinearColor LockedGrey = FeColor(1.f, 0.5f, 0.5f, 0.5f);
	const FLinearColor CompletedGold = FeColor(1.f, 0.9804f, 0.7843f, 0.1686f);
	const FLinearColor DialogTint = FeColor(0.8f, 0.1f, 0.1f, 0.1f);

	float Ink(const FSSXFeDrawList& D, EFont F, const FString& S, float Scale) { return FMath::TruncToFloat(D.TextInkWidth(F, S, Scale)); }
	float LineH(const FSSXFeDrawList& D, EFont F)
	{
		const float H = D.FontHeight(F, 1.f);
		return H > 0.f ? H : (F == EFont::Menu ? 15.f : 24.f);
	}
	// FUN_0019D568
	void Txt(FSSXFeDrawList& D, EFont F, float X, float Y, float S, const FLinearColor& C, const FString& Str, int32 L)
	{
		D.Text(F, X, Y, S, C, Str, ESSXAlign::Left, true, L);
	}
	// FUN_0019D770
	void TxtC(FSSXFeDrawList& D, EFont F, float X, float Y, float S, const FLinearColor& C, const FString& Str, int32 L)
	{
		D.Text(F, X - Ink(D, F, Str, S) * 0.5f, Y, S, C, Str, ESSXAlign::Left, true, L);
	}
	const FSSXSprite* HudRec(const FSSXFeDrawList& D, int32 Id)
	{
		const FSSXSpriteTable* T = D.Assets && D.Assets->HudSprites ? D.Assets->HudSprites : SSXFeDrawHud::GetSheet();
		return T ? T->Find(Id) : nullptr;
	}
	/** FUN_001C0068 / FUN_0014EED0: HUD-sheet sprite, flags 0x20 / 0x40 centre x / y. The id is recorded even when the
	 *  sheet is not loaded (tests). */
	void Icon(FSSXFeDrawList& D, int32 Id, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C, int32 L)
	{
		const FSSXSprite* S = HudRec(D, Id);
		const float W = (S ? S->W : 16.f) * Sx, H = (S ? S->H : 16.f) * Sy;
		if (Flags & 0x20) X -= W * 0.5f;
		if (Flags & 0x40) Y -= H * 0.5f;
		if (S)
		{
			D.SpriteEx(Id, X, Y, W, H, C, L, true, false, false);
			return;
		}
		FSSXFeDrawCmd Cmd;
		Cmd.Layer = L;
		Cmd.Pos = FVector2f(X, Y);
		Cmd.Size = FVector2f(W, H);
		Cmd.Color = FLinearColor(C.R, C.G, C.B, 0.f);
		Cmd.SpriteId = Id;
		D.Cmds.Add(Cmd);
	}

	/** FUN_001A6D38(x, y, max width, scale, str, colour, flags 1): MENU; one line centred on x when it fits, else words
	 *  broken where the line passes the width, every line centred on x, pitch font height x scale + 4, from y down. */
	void CentredWrap(FSSXFeDrawList& D, float X, float Y, float MaxW, float Scale, const FString& Str, const FLinearColor& C, int32 L)
	{
		if (Ink(D, EFont::Menu, Str, Scale) <= MaxW)
		{
			Txt(D, EFont::Menu, X - Ink(D, EFont::Menu, Str, Scale) * 0.5f, Y, Scale, C, Str, L);
			return;
		}
		TArray<FString> Words;
		Str.ParseIntoArray(Words, TEXT(" "), false);
		TArray<FString> Lines;
		FString Line;
		for (const FString& W : Words)
		{
			const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
			if (!Line.IsEmpty() && Ink(D, EFont::Menu, Try, Scale) > MaxW)
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
		const float Pitch = LineH(D, EFont::Menu) * Scale + 4.f;
		for (const FString& S : Lines)
		{
			Txt(D, EFont::Menu, X - Ink(D, EFont::Menu, S, Scale) * 0.5f, Y, Scale, C, S, L);
			Y += Pitch;
		}
	}

	/** The FUN_001957B0 text box of the End page: MENU, box (130, 235) width 360, scale 0.82, pitch (font height + 3)
	 *  x 0.82 + 3, lines centred in the box (+0x20 = 1), white, shadow on. Layout FUN_00196218: the string is split at
	 *  every single ' ' (no run collapsing, so LOC 0x22E / 0x22F keep their two spaces after a sentence), a line keeps
	 *  a word while int(ink width) <= int(width), '\' ends a line. */
	void EndTextBox(FSSXFeDrawList& D, const FString& Str, int32 L)
	{
		constexpr float X = 130.f, Y = 235.f, W = 360.f, S = 0.82f;
		TArray<FString> Lines;
		TArray<FString> Paras;
		Str.ParseIntoArray(Paras, TEXT("\\"), false);
		for (const FString& P : Paras)
		{
			TArray<FString> Words;
			P.ParseIntoArray(Words, TEXT(" "), false);
			FString Line;
			for (const FString& Wd : Words)
			{
				const FString Try = Line.IsEmpty() ? Wd : Line + TEXT(" ") + Wd;
				if (!Line.IsEmpty() && Ink(D, EFont::Menu, Try, S) > W) { Lines.Add(Line); Line = Wd; }
				else { Line = Try; }
			}
			Lines.Add(Line);
		}
		const float Pitch = (LineH(D, EFont::Menu) + 3.f) * S + 3.f;
		float Ly = Y;
		for (const FString& Line : Lines)
		{
			if (!Line.IsEmpty())
			{
				Txt(D, EFont::Menu, X + W * 0.5f - Ink(D, EFont::Menu, Line, S) * 0.5f, Ly, S, White, Line, L);
			}
			Ly += Pitch;
		}
	}
}

void FSSXOverlayManager::DrawLesson(FSSXFeDrawList& D) const
{
	const FSSXPromptScope Prompts(D, ESSXPromptContext::Original);   // the lessons keep the PS2 pad
	const SSXLesson::FLessonMan* Ls = Host ? Host->GetLesson() : nullptr;
	if (!Ls || !Loc)
	{
		return;
	}
	using SSXLesson::EState;
	const SSXLesson::FLessonMan& M = *Ls;
	const SSXFeTrickBook::FTable& Book = SSXFeTrickBook::FTable::Get();
	const int32 Rider = FMath::Clamp(M.Rider, 0, SSXProg::NumRiders - 1);
	int32 Layout = 0, Board = 0;
	bool bFlip = false;
	Host->GetTrickInput(0, Layout, Board, bFlip);
	auto Mask = [Layout, Rider, Board](int32 Act) { return FPadMapper::ActionMask(Act, Layout, Rider, Board); };

	// 0x14BBE8 / FUN_0014BD98
	if (M.State == EState::Preview || M.State == EState::Perform)
	{
		Icon(D, SSXLesson::DiagramBaseSprite, M.DiagramPosX, M.DiagramPosY, 1.f, 1.f, 0, White, 8);
		if (M.InpIndex >= SSXLesson::DiagramFirstFrame && M.Inp.IsValidIndex(M.InpIndex - 1))
		{
			const uint32 Lit = SSXLesson::DiagramMask(M.Inp[M.InpIndex - 1], Mask);
			const SSXLesson::FGlyph* G = SSXLesson::DiagramGlyphs();
			for (int32 K = 0; K < SSXLesson::NumDiagramGlyphs; ++K)
			{
				if (Lit & G[K].Mask)
				{
					Icon(D, G[K].Sprite, M.DiagramPosX + G[K].X, M.DiagramPosY + G[K].Y, 1.f, 1.f, 0, White, 9);
				}
			}
		}
	}

	// 0x1B8548 / 0x337E08
	if (!M.bOvLesson || Active[OvPauseMenu])
	{
		return;
	}
	FSSXBackdrop& Bd = Backdrop[OvLesson];
	if (!Bd.bOpen) { Bd.Open(); }
	const FSSXProfile* P = Profile;
	const FSSXCareer* Car = P ? &P->Career[FMath::Clamp(P->Slot[0].Char, 0, SSXProg::NumRiders - 1)] : nullptr;   // FUN_00163F18
	auto Done = [Car](int32 K) { return Car && Car->TrickDone(K); };                                                 // FUN_00164258
	int32 CurChapter = 0;                                                                                             // FUN_00164060
	while (Car && CurChapter < SSXLesson::Chapters && Car->ChapterDone(CurChapter)) { ++CurChapter; }
	const FString TrickName = Book.TrickName(Rider, M.Lesson);   // FUN_00154638 / FUN_001551C0
	const FString TrickNo = L(0xEA7).Replace(TEXT("%d"), *FString::FromInt(M.Lesson + 1));

	// FUN_0014D5E0 / 0x1AA630
	auto Menu = [&]()
	{
		if (M.bQuitDialog)
		{
			return;
		}
		TArray<FString> Items;
		for (int32 I = 0; I < M.MenuCount && I < UE_ARRAY_COUNT(M.MenuItems); ++I) { Items.Add(L(M.MenuItems[I])); }
		DrawList(D, 600.f, (M.PanelH + 480.f) * 0.5f - 40.f, 0.73f, 3, Items, M.MenuSel, nullptr, HudCross, 1.f, 0x10);
	};

	switch (M.State)
	{
	case EState::Wait:
	{
		// FUN_0014C2D0
		Bd.Box(D, M.PanelH, 4);
		const int32 L6 = 6;
		TxtC(D, EFont::Title, 320.f, 65.f, 1.04f, White, L(0xEA8).Replace(TEXT("%S"), SSXFeTables::RiderName(Rider)), L6);
		const FString ChapterStr = L(0xE6A).Replace(TEXT("%d"), *FString::FromInt(M.Chapter + 1));
		const float ChapterW = Ink(D, EFont::Title, ChapterStr, 1.12f);
		Txt(D, EFont::Title, 212.f, 110.f, 1.12f, CurChapter < M.Chapter ? LockedGrey : LessonGold, ChapterStr, L6);
		TxtC(D, EFont::Title, 318.f, 155.f, 0.61f, LessonGold, TrickNo, L6);
		// 0x36E508 / 0x36E520 / 0x36E538
		if (LessonPulse > 1.05f || LessonPulse < 0.95f) { LessonPulseStep = -LessonPulseStep; }
		LessonPulse += LessonPulseStep;
		const float Pl = LessonPulse;
		static const float Fx[5] = { 0.166f, 0.333f, 0.5f, 0.666f, 0.833f };
		float Tw[5] = { 90.f, 90.f, 90.f, 90.f, 90.f }, Th[5] = { 70.f, 70.f, 70.f, 70.f, 70.f };
		float SelY = 215.f;
		const int32 Col = FMath::Clamp(M.Column, 0, 4);
		if (M.MenuLevel == 1)
		{
			const float S = Pl * 0.97f;
			Tw[Col] *= S;
			Th[Col] *= S;
			SelY = (1.f - S) * 45.f + 215.f;
		}
		for (int32 I = 0; I < 5; ++I)
		{
			const int32 K = M.Scroll + I;
			const FLinearColor C = Done(K) ? White : FeColor(1.f, 0.5f, 0.5f, 0.5f);
			const int32 Ch = FMath::Clamp(K / SSXLesson::LessonsPerChapter, 0, SSXLesson::Chapters - 1);
			const int32 Tr = ((K % SSXLesson::LessonsPerChapter) + SSXLesson::LessonsPerChapter) % SSXLesson::LessonsPerChapter;
			D.PageSprite(SSXFeTrickBook::TrickPage(Rider, Ch, Tr), 0x7C + Tr, Fx[I] * 640.f - Tw[I] * 0.5f, I == Col ? SelY : 215.f,
				Tw[I], Th[I], C, L6);
		}
		if (M.MenuLevel != 0)
		{
			const float X0 = (Fx[Col] * 640.f - Tw[Col] * 0.5f) - 2.f, Y0 = SelY - 2.f;
			const float Right = Fx[Col] * 640.f + Tw[Col] * 0.5f;
			D.Rect(X0, Y0, Tw[Col] + 4.f, 4.f, LessonGold, L6);
			D.Rect(X0, Y0, 4.f, Th[Col] + 4.f, LessonGold, L6);
			D.Rect(Right - 2.f, Y0, 4.f, Th[Col] + 4.f, LessonGold, L6);
			D.Rect(X0, (SelY + Th[Col]) - 2.f, Tw[Col] + 4.f, 4.f, LessonGold, L6);
		}
		const float As = Pl * 1.2f;
		if (M.MenuLevel == 0)
		{
			if (M.Chapter > 0) Icon(D, HudArrowL, 192.f, 126.f, As, As, 0x60, White, L6);
			if (M.Chapter < 5) Icon(D, HudArrowR, ChapterW + 212.f + 25.f, 126.f, As, As, 0x60, White, L6);
		}
		else if (M.MenuLevel == 1)
		{
			if (M.Column > 0) Icon(D, HudArrowL, 31.f, 249.f, As, As, 0x60, White, L6);
			if (M.Column < 4) Icon(D, HudArrowR, 604.f, 249.f, As, As, 0x60, White, L6);
		}
		// FUN_0019D6E0
		const bool bLong = TrickName.Len() >= 0x35;
		TxtC(D, EFont::Menu, 314.f, bLong ? 189.f : 187.f, bLong ? 0.765f : 0.9f, White, TrickName, L6);
		if (Done(M.Lesson))
		{
			TxtC(D, EFont::Menu, 320.f, 345.f, 0.8f, CompletedGold, L(0xC93), L6);
		}
		// FUN_0014CF38
		if (const SSXFeTrickBook::FEntry* E = Book.Find(Rider, M.Lesson))
		{
			const FString Str = FSSXHudOverlay::UberComboString(*E, Mask, bFlip);
			auto Adv = [&D](TCHAR G) -> float
			{
				if (const FSSXHudOverlay::FUberIcon* Ic = FSSXHudOverlay::FindUberIcon(G)) return Ic->Adv;
				return G == ' ' ? 8.f : D.TextWidth(EFont::Menu, FString(1, &G), 1.f) + 1.f;
			};
			float Width = 0.f;
			for (TCHAR G : Str) Width += Adv(G);
			float X = 320.f - Width * 0.5f;
			const float Y = 307.2f;
			for (TCHAR G : Str)
			{
				if (const FSSXHudOverlay::FUberIcon* Ic = FSSXHudOverlay::FindUberIcon(G))
				{
					Icon(D, Ic->Sprite, X + Ic->Xo, Y + Ic->Yo, 1.f, 1.f, 0, White, L6);
				}
				else if (G != ' ')
				{
					Txt(D, EFont::Menu, X, Y + ((G == '(' || G == ')') ? -1.f : (G == '+' ? -2.f : 0.f)), 1.f, White, FString(1, &G), L6);
				}
				X += Adv(G);
			}
		}
		Menu();
		const int32 L10 = M.bQuitDialog ? L6 : 0x10;
		if (M.MenuLevel != 0)
		{
			const float By = ((M.PanelH + 480.f) * 0.5f - 40.f) - 23.f;
			Icon(D, HudTriangle, 40.f, By, 1.f, 1.f, 0, White, L10);
			Txt(D, EFont::Menu, 64.f, By + 4.f, 0.73f, LessonGold, L(0x1D7), L10);
		}
		if (M.MenuLevel == 0) TxtC(D, EFont::Menu, 320.f, 399.f, 0.73f, White, L(0xDD9), L10);
		else if (M.MenuLevel == 1) TxtC(D, EFont::Menu, 320.f, 399.f, 0.73f, White, L(0xDDA), L10);
		break;
	}
	case EState::PreviewEnd:
		// FUN_0014CFB0
		Bd.Box(D, M.PanelH, 4);
		TxtC(D, EFont::Title, 320.f, 155.f, 1.f, LessonGold, TrickNo, 6);
		CentredWrap(D, 320.f, 200.f, 480.f, 0.85f, TrickName, White, 6);
		Menu();
		break;
	case EState::PracticeEnd:
		// FUN_0014D138
		Bd.Box(D, M.PanelH, 4);
		TxtC(D, EFont::Title, 320.f, 165.f, 0.8f, LessonGold, L(0xEBB), 6);
		if (M.RiderTrickText.IsEmpty())
		{
			TxtC(D, EFont::Menu, 320.f, 215.f, 0.9f, White, L(M.PracticeLoc), 6);
		}
		else
		{
			CentredWrap(D, 320.f, 215.f, 480.f, 0.85f, M.RiderTrickText, White, 6);
		}
		Menu();
		break;
	case EState::End:
		// FUN_0014D320
		Bd.Box(D, M.PanelH, 4);
		TxtC(D, EFont::Title, 320.f, 135.f, 1.f, LessonGold, TrickNo, 6);
		CentredWrap(D, 320.f, 180.f, 480.f, 0.85f, TrickName, White, 6);
		EndTextBox(D, L(M.bPassed ? 0x22F : 0x22E), 6);
		Menu();
		break;
	default:
		break;   // FUN_0014C2C0 / 0x14C2C8
	}

	if (M.bQuitDialog)
	{
		// 0x1AA268
		D.Rect(0.f, 0.f, ScreenW, ScreenH, DialogTint, 8);
		FSSXOvDialog Box;
		Box.Show(0, SSXLesson::LocAreYouSure, SSXLesson::LocYes, -1, SSXLesson::LocNo);
		Box.Draw(D, *Loc, Bd);
	}
}
