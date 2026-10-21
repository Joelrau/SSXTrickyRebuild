// FUN_0019DC78 / FUN_0019D568 / FUN_0019CF70
#include "UI/SSXRaceOverlays.h"

#include "SSXTrickyRebuild.h"
#include "Race/SSXReplay.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"
#include "Input/PadMapper.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXHudOverlay.h"
#include "UI/SSXRaceUIHost.h"

using namespace SSXFe;

namespace
{
	const FLinearColor VenueBlue = FeColor(1.f, 0.537f, 0.773f, 0.827f);   // 0x1AAEC0
	const FLinearColor Orange = FeColor(1.f, 1.f, 0.5f, 0.f);
	const FLinearColor ListGrey = FeColor(1.f, 0.4f, 0.4f, 0.4f);
	const FLinearColor Yellow = FeColor(1.f, 1.f, 0.471f, 0.039f);
	const FLinearColor GreenTxt = FeColor(1.f, 0.f, 1.f, 0.f);
	const FLinearColor RedTxt = FeColor(1.f, 1.f, 0.f, 0.f);
	const FLinearColor Completed = FeColor(1.f, 0.98f, 0.784f, 0.169f);
	const FLinearColor CountGreen = FeColor(1.f, 0.2f, 1.f, 0.2f);
	const FLinearColor BoxGrey = FeColor(1.f, 0.1f, 0.1f, 0.1f);

	FLinearColor WithA(FLinearColor C, float A) { C.A *= A; return C; }

	const FSSXSprite* HudRec(const FSSXFeDrawList& D, int32 Id)
	{
		const FSSXSpriteTable* T = D.Assets && D.Assets->HudSprites ? D.Assets->HudSprites : SSXFeDrawHud::GetSheet();
		return T ? T->Find(Id) : nullptr;
	}
	float HudW(const FSSXFeDrawList& D, int32 Id, float Fallback) { const FSSXSprite* S = HudRec(D, Id); return S ? S->W : Fallback; }
	float HudH(const FSSXFeDrawList& D, int32 Id, float Fallback) { const FSSXSprite* S = HudRec(D, Id); return S ? S->H : Fallback; }
	float H37(const FSSXFeDrawList& D) { return HudH(D, HudBoxCorner, 33.f); }

	float LineH(const FSSXFeDrawList& D, EFont F)
	{
		const float H = D.FontHeight(F, 1.f);
		return H > 0.f ? H : (F == EFont::Menu ? 15.f : 24.f);
	}
	// FUN_0019DC78
	float Ink(const FSSXFeDrawList& D, EFont F, const FString& S, float Scale) { return FMath::TruncToFloat(D.TextInkWidth(F, S, Scale)); }

	// FUN_0019D568
	void Txt(FSSXFeDrawList& D, EFont F, float X, float Y, float S, const FLinearColor& C, const FString& Str, int32 Layer)
	{
		D.Text(F, X, Y, S, C, Str, ESSXAlign::Left, true, Layer);
	}
	// FUN_0019D770
	void TxtC(FSSXFeDrawList& D, EFont F, float X, float Y, float S, const FLinearColor& C, const FString& Str, int32 Layer)
	{
		D.Text(F, X - Ink(D, F, Str, S) * 0.5f, Y, S, C, Str, ESSXAlign::Left, true, Layer);
	}
	// FUN_0019D868
	void TxtR(FSSXFeDrawList& D, EFont F, float X, float Y, float S, const FLinearColor& C, const FString& Str, int32 Layer)
	{
		D.Text(F, X - Ink(D, F, Str, S), Y, S, C, Str, ESSXAlign::Left, true, Layer);
	}
	// FUN_0019CF70
	void TxtN(FSSXFeDrawList& D, EFont F, float X, float Y, float S, const FLinearColor& C, const FString& Str, int32 Layer)
	{
		D.Text(F, X, Y, S, C, Str, ESSXAlign::Left, false, Layer);
	}

	/** FUN_001C0068(x, y, sx, sy, ovl, id, flags): HUD sprite, flags 0x20 centre x, 0x40 centre y, 8 right, 4 bottom,
	 *  1 mirror u, 2 flip v. */
	void Icon(FSSXFeDrawList& D, int32 Id, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C, int32 Layer)
	{
		const FSSXSprite* S = HudRec(D, Id);
		if (!S || Id < 0) return;
		const float W = S->W * Sx, H = S->H * Sy;
		if (Flags & 0x20) X -= W * 0.5f; else if (Flags & 8) X -= W;
		if (Flags & 0x40) Y -= H * 0.5f; else if (Flags & 4) Y -= H;
		D.SpriteEx(Id, X, Y, W, H, C, Layer, true, (Flags & 1) != 0, (Flags & 2) != 0);
	}

	/** FUN_001C0068 with all its flags, including 0x10 (FUN_001FC178 -> FUN_0019BDF8: the quad is h wide and w high,
	 *  the texture turned). Id -5 = untextured: the size is the scale itself (an untextured GS quad). */
	void IconFlags(FSSXFeDrawList& D, int32 Id, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C, int32 Layer)
	{
		// 0x1FC178
		const bool bRot = (Flags & 0x10) != 0;
		if (Id == -5)
		{
			const FVector2f P = FSSXFeDrawList::IconOrigin(X, Y, Sx, Sy, Flags);
			D.Rect(P.X, P.Y, bRot ? Sy : Sx, bRot ? Sx : Sy, C, Layer);
			return;
		}
		const FSSXSprite* S = HudRec(D, Id);
		if (!S || Id < 0) return;
		const float W = S->W * Sx, H = S->H * Sy;
		const FVector2f P = FSSXFeDrawList::IconOrigin(X, Y, W, H, Flags);
		if (D.SpriteEx(Id, P.X, P.Y, bRot ? H : W, bRot ? W : H, C, Layer, true, (Flags & 1) != 0, (Flags & 2) != 0) && bRot)
		{
			D.Cmds.Last().bRot90 = true;
		}
	}

	// FUN_001A6BA0
	FString TimeCs(const FSSXLocStrings* Loc, int32 Cs)
	{
		if (Cs == 50000) return Loc ? Loc->ByIndex(349) : FString();
		if (Cs < 0) return Loc ? Loc->ByIndex(396) : FString();
		if (Cs >= 360000) return TEXT("59:59.99");
		return FString::Printf(TEXT("%d:%02d.%02d"), Cs / 100 / 60, Cs / 100 % 60, Cs % 100);
	}
	// FUN_001A6C90
	FString DiffCs(int32 Cs)
	{
		if (Cs > 359999) return TEXT("59:59.99");
		if (Cs < 0) { const int32 S = -Cs / 100; return FString::Printf(TEXT("-%d:%02d.%02d"), S / 60, S % 60, -Cs % 100); }
		return FString::Printf(TEXT("%d:%02d.%02d"), Cs / 100 / 60, Cs / 100 % 60, Cs % 100);
	}
	int32 SecToCs(double Sec) { return Sec < 0.0 ? -1 : (int32)(Sec * 100.0 + 0.5); }

	FString PlaceSuffix(const FSSXLocStrings* Loc, int32 Place)
	{
		const int32 Id = Place == 1 ? 3652 : (Place == 2 ? 3668 : (Place == 3 ? 3672 : 3653));
		return Loc ? Loc->ByIndex(Id) : FString();
	}

	/**
	 * Text box FUN_001957B0 family: box (x, y, w) FUN_00196070, scale FUN_001961F0 (line pitch (font+0xC + 3) x scale,
	 * +0x14), word wrap FUN_00196218 on the ink width (a '\' breaks the line), draw FUN_001957D8: lines left (+0x20 = 0),
	 * centred (+0x20) or right-aligned (+0x24) in the box, colour +0x3C with alpha x +0x38, shadow +0x6C (default on).
	 */
	struct FOvBox
	{
		EFont Font = EFont::Menu;
		float X = 0.f, Y = 0.f, W = 100.f;
		float Scale = 1.f;
		FLinearColor Color = White;
		int32 Align = 0;
		float PitchAdd = 0.f;     // FUN_001961E8
		bool bShadow = true;
	};
	TArray<FString> WrapLines(const FSSXFeDrawList& D, const FOvBox& B, const FString& Str)
	{
		TArray<FString> Out;
		TArray<FString> Paras;
		Str.ParseIntoArray(Paras, TEXT("\\"), false);
		for (const FString& P : Paras)
		{
			TArray<FString> Words;
			P.ParseIntoArray(Words, TEXT(" "), true);
			FString Line;
			for (const FString& W : Words)
			{
				const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
				if (!Line.IsEmpty() && Ink(D, B.Font, Try, B.Scale) > FMath::TruncToFloat(B.W))
				{
					Out.Add(Line);
					Line = W;
				}
				else
				{
					Line = Try;
				}
			}
			Out.Add(Line);
		}
		return Out;
	}
	void TextBox(FSSXFeDrawList& D, const FOvBox& B, const FString& Str, int32 Layer)
	{
		const float Pitch = (LineH(D, B.Font) + 3.f) * B.Scale + B.PitchAdd;
		float Y = B.Y;
		for (const FString& Line : WrapLines(D, B, Str))
		{
			if (!Line.IsEmpty())
			{
				const float W = Ink(D, B.Font, Line, B.Scale);
				const float X = B.Align == 1 ? B.X + B.W * 0.5f - W * 0.5f : (B.Align == 2 ? B.X + B.W - W : B.X);
				D.Text(B.Font, X, Y, B.Scale, B.Color, Line, ESSXAlign::Left, B.bShadow, Layer);
			}
			Y += Pitch;
		}
	}

	/** In-race progress bar (cFE3DProgressBar with the HUD pieces of 0x1BA788): track 0x44 (1 x 11) tiled over
	 *  (x, y, w, 10) colour (1, .2, .2, .2), end 0x43 at (x + w, y) size (6, 10), pips 0x42 (9 x 11, one per point)
	 *  red (1, 1, 0, 0) to Value2 and white to Value; the bar is centred on its y. */
	void HudCells(FSSXFeDrawList& D, int32 Id, float X, float Y, float W, float H, float Rep, const FLinearColor& C, int32 Layer)
	{
		if (Rep <= 0.f || W <= 0.f) return;
		const float Cw = W / Rep;
		const int32 N = FMath::CeilToInt(Rep - 1e-4f);
		for (int32 I = 0; I < N; ++I)
		{
			const float F = FMath::Min(1.f, Rep - I);
			D.SpriteCropU(Id, X + I * Cw, Y, Cw * F, H, F, C, Layer, true);
		}
	}
	void ProgressBar(FSSXFeDrawList& D, float X, float Y, float W, float Max, float Value, float Value2, int32 Layer)
	{
		const float Top = Y - 5.f;
		HudCells(D, 0x44, X, Top, W, 10.f, 1.f, FeColor(1.f, 0.2f, 0.2f, 0.2f), Layer);
		D.Sprite(0x43, X + W, Top, 6.f, 10.f, FeColor(1.f, 0.2f, 0.2f, 0.2f), Layer, true);
		if (Max > 0.f && Value2 > Value) HudCells(D, 0x42, X, Top, Value2 / Max * W, 10.f, Value2, RedTxt, Layer + 1);
		if (Max > 0.f && Value > 0.f) HudCells(D, 0x42, X, Top, Value / Max * W, 10.f, Value, White, Layer + 2);
	}

	/** Aggression label FUN_001A8FA0 / the label part of FUN_001A91C8: LOC 4 "Friend" green (1, .1, 1, .1) at <= 20,
	 *  3551 "Neutral" white at <= 60, orange (1, 1, .471, .039) at <= 80, else 8 "Enemy" red (1, .9, .1, .1). */
	void AggressionLabel(int32 V, int32& OutLoc, FLinearColor& OutC)
	{
		if (V <= 20) { OutLoc = 4; OutC = FeColor(1.f, 0.1f, 1.f, 0.1f); }
		else if (V <= 60) { OutLoc = 3551; OutC = White; }
		else if (V <= 80) { OutLoc = 3551; OutC = Yellow; }
		else { OutLoc = 8; OutC = FeColor(1.f, 0.9f, 0.1f, 0.1f); }
	}
}

void FSSXBackdrop::Open()
{
	// 0x1A8900
	bOpen = true;
	bAnimated = false;
	Alpha = AlphaCap = 0.65f;
	TextAlpha = 1.f;
	TabX = 0.f;
	Height = 60.f;
	Target = 500.f;
}

void FSSXBackdrop::OpenAnimated(float InAlphaCap)
{
	// 0x1A89B0 / 0x1A8AD8 / 0x1A8B70
	bOpen = true;
	bAnimated = true;
	Height = 60.f;
	Target = 500.f;
	Alpha = 0.f;
	AlphaCap = InAlphaCap;
	TextAlpha = 0.f;
	TabX = -850.f;
}

void FSSXBackdrop::Tick()
{
	if (!bOpen || !bAnimated) return;
	Alpha = FMath::Min(Alpha + 0.034f, AlphaCap);               // 0x1A8C50
	Height = FMath::Min(Height + 13.f, Target);
	TextAlpha = FMath::Min(TextAlpha + 0.034f, 1.f);            // 0x1A8E10
	TabX = TabX + 25.f > 0.f ? 0.f : TabX + 25.f;               // 0x1A8F28
}

void FSSXBackdrop::Box(FSSXFeDrawList& D, float H, int32 Layer)
{
	// 0x1A9BB8
	if (bAnimated) { Target = H; } else { Height = H; }
	const float Hc = Height;
	const float Ch = H37(D);
	const float S = 340.f / HudW(D, HudBoxCorner, 256.f);
	const float Wd = 340.f;
	const float X0 = -20.f;
	const float Top = 240.f - Hc * 0.5f;
	const float Bottom = Hc * 0.5f + 240.f - Ch;
	const FLinearColor C = FeColor(Alpha, 0.1f, 0.1f, 0.1f);
	D.Rect(X0, Top + Ch - Ch * 0.21f, 680.f, Hc - Ch - Ch + Ch * 0.21f + Ch * 0.21f, C, Layer);
	const float Inner = Wd * 0.455f;
	const float Iw = (Wd - Inner) + (Wd - Inner);
	const float Ih = (Ch - Ch * 0.21f) - Ch * 0.26f;
	D.Rect(X0 + Inner, Top + Ch * 0.26f, Iw, Ih, C, Layer);
	D.Rect(X0 + Inner, Bottom + Ch * 0.21f, Iw, Ih, C, Layer);
	Icon(D, HudBoxCorner, X0, Top, S, 1.f, 2, White, Layer + 1);
	Icon(D, HudBoxCorner, 320.f, Top, S, 1.f, 3, White, Layer + 1);
	Icon(D, HudBoxCorner, X0, Bottom, S, 1.f, 0, White, Layer + 1);
	Icon(D, HudBoxCorner, 320.f, Bottom, S, 1.f, 1, White, Layer + 1);
}

void FSSXOvDialog::Draw(FSSXFeDrawList& D, const FSSXLocStrings& Loc, FSSXBackdrop& Bd) const
{
	if (!bActive) return;
	// 0x1AA090
	auto W = [&D, &Loc](int32 Id) { return Id >= 0 ? Ink(D, EFont::Menu, Loc.ByIndex(Id), 1.f) : 0.f; };
	float Buttons = 0.f;
	if (LocX >= 0) Buttons = W(LocX) + 40.f;
	if (LocCircle >= 0) Buttons = Buttons + 40.f + W(LocCircle);
	if (LocTriangle >= 0) Buttons = Buttons + 40.f + W(LocTriangle);
	const float Bw = FMath::Max3(W(Line1), W(Line2), Buttons);
	float H = 70.f;
	if (Line1 >= 0) H = 100.f;
	if (Line2 >= 0) H += 30.f;
	const float Offset = (Bw - Buttons) * 0.5f;
	// 0x1AA268
	Bd.Box(D, 165.f, 0x11);
	float Y = (240.f - H * 0.5f) + 20.f;
	float X = 320.f - Bw * 0.5f;
	for (int32 Id : { Line1, Line2 })
	{
		if (Id < 0) break;
		const FString S = Loc.ByIndex(Id);
		Txt(D, EFont::Menu, X + (Bw - Ink(D, EFont::Menu, S, 1.f)) * 0.5f, Y, 1.f, White, S, 0x13);
		Y += 30.f;
	}
	X += Offset;
	const struct { int32 Loc; int32 Icon; } Btn[] = { { LocX, HudCross }, { LocCircle, HudCircle }, { LocTriangle, HudTriangle } };
	for (const auto& B : Btn)
	{
		if (B.Loc < 0) continue;
		Icon(D, B.Icon, X, Y, 1.f, 1.f, 0, White, 0x13);
		const FString S = Loc.ByIndex(B.Loc);
		Txt(D, EFont::Menu, X + 23.f, Y + 2.f, 1.f, White, S, 0x13);
		X = X + 23.f + ((Ink(D, EFont::Menu, S, 1.f) + 40.f) - 23.f);
	}
}

void FSSXOverlayManager::DrawList(FSSXFeDrawList& D, float X, float Y, float Scale, uint32 Align, const TArray<FString>& Strs, int32 Sel,
	const TArray<bool>* Enabled, int32 IconId, float A, int32 Layer, bool bPulse) const
{
	// 0x1AA630
	const bool bSelOn = Sel < 0 || !Enabled || !Enabled->IsValidIndex(Sel) || (*Enabled)[Sel];
	if (bPulse && bSelOn) bPulseDrawn = true;
	const float Lh = LineH(D, EFont::Menu);
	const float Pitch = Lh * Scale + 4.f;
	float Y0 = Y;
	if (Align & 10)
	{
		const float Bh = Strs.Num() * Pitch;
		Y0 = (Align & 2) ? Y - Bh : Y - Bh * 0.5f;
	}
	float X0 = X;
	if (Align & 5)
	{
		float Mw = 0.f;
		for (const FString& S : Strs) Mw = FMath::Max(Mw, Ink(D, EFont::Menu, S, Scale));
		if (Align & 1) X0 = X - Mw; else if (Align & 4) X0 = X - Mw * 0.5f;
	}
	if (IconId >= 0)
	{
		const float Iy = Sel < 0 ? Y0 + Lh * Scale * 0.5f : Y0 + Pitch * Sel + Lh * Scale * 0.5f;
		Icon(D, IconId, X0 - 16.f, Iy, 0.9f, 0.9f, 0x60, WithA(White, A), Layer);
	}
	float Ry = Y0;
	for (int32 I = 0; I < Strs.Num(); ++I)
	{
		const bool bOn = !Enabled || !Enabled->IsValidIndex(I) || (*Enabled)[I];
		if (I == Sel)
		{
			const float Base = Lh * Scale, Big = Base * Pulse;
			Txt(D, EFont::Menu, X0, Ry - (Big - Base) * 0.5f, Scale * Pulse, WithA(bOn ? Gold : ListGrey, A), Strs[I], Layer);
		}
		else
		{
			Txt(D, EFont::Menu, X0, Ry, Scale, WithA(bOn ? White : ListGrey, A), Strs[I], Layer);
		}
		Ry += Pitch;
	}
}

void FSSXOverlayManager::DrawTab(FSSXFeDrawList& D, float Y, const FSSXBackdrop& Bd, const FString& Str, int32 Layer) const
{
	// 0x1A9928
	const float Tw = Ink(D, EFont::Title, Str, 0.79f);
	const float Mid = Tw - 2.f + 10.f;
	const float X = Bd.TabX;
	const float Lw = HudW(D, HudTabL, 82.f) * 0.9f;
	Icon(D, HudTabL, X, Y, 0.9f, 0.867f, 0, White, Layer);
	Icon(D, HudTabM, X + Lw, Y, Mid, 0.867f, 0, White, Layer);
	Icon(D, HudTabR, X + Lw + Mid, Y, 0.9f, 0.867f, 0, White, Layer);
	Txt(D, EFont::Title, X + Lw - 2.f, Y + 7.f, 0.79f, White, Str, Layer + 2);
}

void FSSXOverlayManager::DrawVenueTitleBar(FSSXFeDrawList& D, FSSXBackdrop& Bd, float H) const
{
	// 0x1AAEC0
	Bd.Box(D, H, 5);
	const int32 Track = Host ? Host->GetTrackId() : 0;
	Txt(D, EFont::Title, 116.f, (480.f - H) * 0.5f + H37(D) + 7.f, 0.78f, WithA(VenueBlue, Bd.TextAlpha), L(SSXFeTables::TrackNameLoc(Track)), 7);
}

/** The cResultsOverlay table (0x1AB1A0 data: columns DAT_00338598, alignment 0x3384D0, header y 0x3385BC, rows 0x338550,
 *  row / column tints 0x338510 / 0x3384F0, headers 0x33C5C0, cells 0x3385C0, title 0x33C9C0, items +0x10). */
struct FSSXOverlayManager::FTable
{
	int32 Cols = 0;
	float X[8] = {};
	int32 Align[8] = {};
	int32 ColTint[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	FString Head[8];
	float HeaderY = 0.f;
	int32 NumRows = 0;
	float Rows[16] = {};
	int32 RowTint[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	FString Cell[8][16];
	FString Title;
	bool bSmallTitle = false;
	bool bCol0Right = false;
	bool bCol2Small = false;
	TArray<FString> Menu;
	int32 MenuSel = 0;
	// FUN_001AAE10
	void SetRows(const FSSXFeDrawList& D, float InHeaderY, float Gap)
	{
		HeaderY = InHeaderY;
		const float R = LineH(D, EFont::Menu) * 0.85f;
		Rows[0] = InHeaderY + R + 12.f;
		for (int32 I = 1; I < 16; ++I) Rows[I] = Rows[I - 1] + R + Gap;
	}
};

void FSSXOverlayManager::DrawTable(FSSXFeDrawList& D, const FSSXBackdrop& Bd, const FTable& T, int32 M) const
{
	const float A = Bd.TextAlpha;
	Txt(D, EFont::Title, 116.f, (480.f - Bd.Height) * 0.5f + H37(D) + 7.f + 23.f, T.bSmallTitle ? 0.8f : 1.f, WithA(White, A), T.Title, 7);
	for (int32 C = 0; C < T.Cols; ++C)
	{
		if (T.Head[C].IsEmpty()) continue;
		float X = T.X[C];
		if (T.Align[C] == 2 || (C == 0 && T.bCol0Right)) X = T.X[C + 1] - Ink(D, EFont::Title, T.Head[C], 0.7f);
		else if (T.Align[C] == 1) X = (T.X[C + 1] + T.X[C] - Ink(D, EFont::Title, T.Head[C], 0.7f)) * 0.5f;
		const bool bSmall = C == 2 && T.bCol2Small;
		Txt(D, EFont::Title, X, T.HeaderY - 8.f + (bSmall ? 2.f : 0.f), bSmall ? 0.6f : 0.7f, WithA(Gold, A), T.Head[C], 7);
	}
	const float S = (M == 1 && NumHumans() == 2) ? 0.8f : 0.85f;
	for (int32 R = 0; R < T.NumRows; ++R)
	{
		for (int32 C = 0; C < T.Cols; ++C)
		{
			const FString& Str = T.Cell[C][R];
			if (Str.IsEmpty()) continue;
			FLinearColor Col = WithA(White, A);
			if (T.RowTint[R] != -1 || T.ColTint[C] != -1)
			{
				const int32 Tint = FMath::Max(T.RowTint[R], T.ColTint[C]);
				if (Tint == 1) Col = FeColor(A, 1.f, 0.5f, 0.f);
				else if (Tint == 0) Col = WithA(Gold, A);
			}
			float X = T.X[C];
			if (T.Align[C] == 1) X = (T.X[C + 1] + T.X[C] - Ink(D, EFont::Menu, Str, S)) * 0.5f;
			else if (T.Align[C] == 2) X = T.X[C + 1] - Ink(D, EFont::Menu, Str, S);
			Txt(D, EFont::Menu, X, T.Rows[R], S, Col, Str, 7);
		}
	}
	if (T.Menu.Num() > 0)
	{
		DrawList(D, 600.f, (Bd.Height + 480.f) * 0.5f - 40.f, 0.73f, 3, T.Menu, T.MenuSel, nullptr, HudCross, A, 7);
	}
}

void FSSXOverlayManager::DrawPause(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvPauseMenu];
	// 0x1ABE70
	if (Dialog.bActive) { Dialog.Draw(D, *Loc, B); return; }
	if (SaveFrames > 0) { DrawSaveBox(D, B); return; }
	const int32 N = PauseDepth == 1 ? PauseOptionRows().Num() : PauseItems().Num();
	float Hp = (PauseDepth == 1 ? 100.f : 95.f) + N * (LineH(D, EFont::Menu) * 0.92f + 4.f);
	if (bTrickBook) Hp += 120.f;
	B.Box(D, Hp, 0xE);
	const float A = B.TextAlpha;
	const float Top = (480.f - B.Height) * 0.5f;
	if (PauseDepth == 1)
	{
		DrawPauseOptions(D);
	}
	else
	{
		// 0x1AC7E0
		TArray<FString> Strs;
		for (const FPauseItem& I : PauseItems()) Strs.Add(L(I.Loc));
		if (!bTrickBook)
		{
			DrawList(D, 233.f, 240.f, 0.92f, 8, Strs, PauseSel, nullptr, HudCross, A, 0x10);
			DrawTab(D, Top - 8.f, B, L(3683), 0x10);
		}
		else
		{
			DrawList(D, 233.f, Top + 95.f, 0.92f, 8, Strs, PauseSel, nullptr, -1, A, 0x10);
			DrawTab(D, Top - 8.f, B, L(3683), 0x10);
			DrawPauseTrickBook(D, Top);
		}
	}
	if (B.IsGrown())
	{
		DrawList(D, 600.f, (Hp + 480.f) * 0.5f - 40.f, 0.73f, 3, { L(471) }, -1, nullptr, HudTriangle, A, 0x10);
	}
}

void FSSXOverlayManager::DrawPauseTrickBook(FSSXFeDrawList& D, float Top) const
{
	// 0x1AC7E0
	(void)Top;
	const int32 Char = Profile ? FMath::Clamp(Profile->Slot[PausePlayer].Char, 0, SSXProg::NumRiders - 1) : 0;
	const int32 Ch = TrickBookChapter();
	Txt(D, EFont::Title, 40.f, 242.f, 0.9f, Gold, SSXFeTables::RiderName(Char), 0x10);
	TxtC(D, EFont::Title, 320.f, 242.f, 0.9f, Gold, L(3684).Replace(TEXT("%d"), *FString::FromInt(Ch + 1)), 0x10);
	TxtC(D, EFont::Title, 550.f, 245.f, 0.76f, Gold, L(3751).Replace(TEXT("%d"), *FString::FromInt(TrickPage + 1)), 0x10);
	// FUN_00154638 / FUN_001551C0 / 0x31E210 / FUN_00154580
	if (const SSXFeTrickBook::FEntry* E = SSXFeTrickBook::FTable::Get().Find(Char, TrickPage))
	{
		const FString Name = SSXFeTrickBook::FTable::Get().TrickName(Char, TrickPage);
		const bool bLong = Name.Len() >= 50;
		TxtC(D, EFont::Menu, 320.f, bLong ? 275.f : 273.f, bLong ? 0.696f : 0.8f, White, Name, 0x10);
		// 0x154B18 / 0x31E0F0
		int32 Pad = 0, Board = 0;
		bool bFlip = false;
		if (Host) Host->GetTrickInput(PausePlayer, Pad, Board, bFlip);
		const FString Str = FSSXHudOverlay::UberComboString(*E, [Pad, Char, Board](int32 Act) { return FPadMapper::ActionMask(Act, Pad, Char, Board); }, bFlip);
		auto Adv = [&D](TCHAR G) -> float
		{
			if (const FSSXHudOverlay::FUberIcon* Ic = FSSXHudOverlay::FindUberIcon(G)) return Ic->Adv;
			return G == ' ' ? 8.f : D.TextWidth(EFont::Menu, FString(1, &G), 1.f) + 1.f;
		};
		float Width = 0.f;
		for (TCHAR G : Str) Width += Adv(G);
		float X = 320.f - Width * 0.5f;
		for (TCHAR G : Str)
		{
			if (const FSSXHudOverlay::FUberIcon* Ic = FSSXHudOverlay::FindUberIcon(G))
			{
				Icon(D, Ic->Sprite, X + Ic->Xo, 303.f + Ic->Yo, 1.f, 1.f, 0, White, 0x10);
			}
			else if (G != ' ')
			{
				Txt(D, EFont::Menu, X, 303.f + ((G == '(' || G == ')') ? -1.f : (G == '+' ? -2.f : 0.f)), 1.f, White, FString(1, &G), 0x10);
			}
			X += Adv(G);
		}
	}
	if (TrickPage > ChapterFirstPage(Ch)) Icon(D, HudArrowL, 30.f, 282.f, 1.f, 1.f, 0, Gold, 0x10);
	if (TrickPage < ChapterFirstPage(Ch + 1) - 1) Icon(D, HudArrowR, 598.f, 282.f, 1.f, 1.f, 0, Gold, 0x10);
	const bool bDone = Profile && Profile->Career[Char].TrickDone(TrickPage);   // FUN_00164258
	if (bDone) TxtC(D, EFont::Menu, 320.f, 330.f, 0.8f, Completed, L(3219), 0x10);
}

void FSSXOverlayManager::DrawPauseOptions(FSSXFeDrawList& D) const
{
	// 0x1AD628
	const FSSXBackdrop& B = Backdrop[OvPauseMenu];
	constexpr int32 Layer = 0x10;
	DrawTab(D, (480.f - B.Height) * 0.5f - 8.f, B, L(3769), Layer);
	const TArray<int32> Rows = PauseOptionRows();
	const float RowPitch = LineH(D, EFont::Menu) * 0.8f + 6.f;
	const float Y0 = 240.f - RowPitch * Rows.Num() * 0.5f;
	for (int32 I = 0; I < Rows.Num(); ++I)
	{
		const float Y = Y0 + I * RowPitch;
		const FLinearColor C = I == OptionsSel ? Gold : White;
		TxtR(D, EFont::Menu, 264.f, Y, 0.8f, C, L(Rows[I]), Layer);
		Txt(D, EFont::Menu, 270.f, Y, 0.8f, C, TEXT(":"), Layer);
		float ValueRight = 305.f;
		const FString Value = PauseOptionValue(Rows[I]);
		if (PauseOptionSlider(Rows[I]) >= 0.f)
		{
			const float Frac = FMath::Clamp(OptionValue(Rows[I]) / 50.f, 0.f, 1.f);
			const float Hs = HudH(D, HudSlider, 0.f) * 0.55f;
			const float Hf = HudH(D, HudSliderFill, 0.f) * 0.55f;
			if (HudRec(D, HudSlider))
			{
				D.Sprite(HudSlider, 305.f, Y + 3.f, 135.f, Hs, White, Layer, true);
				if (Frac > 0.f) D.SpriteCropU(HudSliderFill, 305.f, Y + 3.f, 135.f * Frac, Hf, Frac, White, Layer + 1, true);
			}
			else
			{
				Txt(D, EFont::Menu, 305.f, Y, 0.8f, White, FString::FromInt(OptionValue(Rows[I])), Layer);
			}
			ValueRight = 440.f;
		}
		else if (!Value.IsEmpty())
		{
			Txt(D, EFont::Menu, 305.f, Y, 0.8f, White, Value, Layer);
			ValueRight = 305.f + Ink(D, EFont::Menu, Value, 0.8f);
		}
		if (I == OptionsSel)
		{
			Icon(D, HudArrowL, 282.f, Y + 1.f, 0.8f, 0.8f, 0, White, Layer);
			Icon(D, HudArrowR, ValueRight + 5.f, Y + 1.f, 0.8f, 0.8f, 0, White, Layer);
		}
	}
}

void FSSXOverlayManager::DrawCountdown(FSSXFeDrawList& D) const
{
	// 0x1AE4B0
	if (AnyModal() || GetHudDetail() == 2) return;
	if (Active[OvCountdown] && CountdownDigit != 0)
	{
		// FUN_001A82E8 / FUN_0019D3F0
		const FString S = FString::FromInt(CountdownDigit);
		const float W = Ink(D, EFont::Title, S, CountdownScale);
		const float H = LineH(D, EFont::Title) * CountdownScale;
		Txt(D, EFont::Title, 320.f - W * 0.5f, 215.f - H * 0.5f, CountdownScale, CountGreen, S, 0x13);
	}
	else if (!Active[OvCountdown] && GoFrames >= 0)
	{
		// FUN_0019D770
		const float F = CountdownScale;
		FLinearColor C = CountGreen;
		C.A = F > 0.5f ? 2.f - (F + F) : 1.f;
		TxtC(D, EFont::Title, 320.f, 215.f - (F - 1.f) * 13.f, F * 3.5f, C, L(3759), 0x13);
	}
}

void FSSXOverlayManager::DrawPopFinish(FSSXFeDrawList& D) const
{
	if (AnyModal()) return;
	for (int32 P = 0; P < 2; ++P)
	{
		const FFinish& F = Finish[P];
		if (!F.bOn || F.Delay > 0) continue;
		const float Al = F.Alpha;
		// 0x1AEB88
		if (F.bPlace && F.Icon >= 0) Icon(D, F.Icon, 72.f, 217.f, 1.3f, 1.3f, 0, WithA(White, Al), 0xD);
		// 0x1AED50
		if (Mode() == 7 && F.bTC)
		{
			TxtC(D, EFont::Title, 320.f, 158.f, 2.1f, WithA(Gold, Al), L(3651), 7);
			TxtR(D, EFont::Title, 305.f, 220.f, 1.05f, WithA(White, Al), DiffCs(F.TimeCs), 7);
			const int32 Diff = F.TimeCs - TimeToBeatCs;
			const FString S = DiffCs(Diff);
			Txt(D, EFont::Title, 335.f, 223.f, 0.9f, WithA(Diff < 0 ? GreenTxt : RedTxt, Al), Diff < 0 ? S : TEXT("+") + S, 7);
			continue;
		}
		if (!F.bPlace)
		{
			TxtC(D, EFont::Title, 320.f, 225.f, 2.1f, WithA(Gold, Al), L(F.bScored ? 3651 : 3675), 0xD);
		}
		else if (F.PlaceChar != 0)
		{
			const int32 Place = F.PlaceChar - TEXT('0');
			const FString S = FString::Printf(TEXT("%c%s %s"), F.PlaceChar, *PlaceSuffix(Loc, Place), *L(3663));
			Txt(D, EFont::Title, 175.f, 225.f, 2.f, WithA(Gold, Al), S, 0xD);
		}
		if (!F.Value.IsEmpty()) TxtC(D, EFont::Title, 320.f, 185.f, 1.f, WithA(Gold, Al), F.Value, 0xD);
	}
}

void FSSXOverlayManager::DrawHelpRace(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvHelpRace];
	// 0x1BD358
	const float Saved = B.Alpha;
	B.Alpha = 0.8f;
	B.Box(D, 420.f, 0xE);
	B.Alpha = Saved;
	// 0x1BD3B8
	const float A = B.TextAlpha;
	const float X = 160.f;
	FLinearColor Ta = Gold; Ta.A = A;
	TxtC(D, EFont::Title, 320.f, 105.f, 0.91f, Ta, L(3773), 0x11);
	FLinearColor W = White; W.A = A;
	Txt(D, EFont::Menu, X, 170.f, 0.9f, W, L(3565), 0x11);
	Txt(D, EFont::Menu, X, 220.f, 0.9f, W, L(3566), 0x11);
	Txt(D, EFont::Menu, X, 270.f, 0.9f, W, L(3567), 0x11);
	const float W3 = Ink(D, EFont::Menu, L(3567), 0.9f);
	Txt(D, EFont::Menu, X + W3 + 44.f, 270.f, 0.9f, W, L(3568), 0x11);
	Txt(D, EFont::Menu, X, 320.f, 0.9f, W, L(3569), 0x11);
	const float W5 = Ink(D, EFont::Menu, L(3569), 0.9f);
	for (const float By : { 175.f, 225.f, 275.f, 325.f }) D.Rect(X - 18.f, By, 7.f, 7.f, W, 0x11);
	DrawList(D, 600.f, (B.Height + 480.f) * 0.5f - 40.f, 0.73f, 3, { L(341) }, 0, nullptr, HudCross, A, 0x11);
	Icon(D, HudR2, X + 8.f + W3, 262.f, 1.f, 1.f, 0, W, 0x11);
	Icon(D, 0x75, X + 10.f + W5, 320.f, 1.f, 1.f, 0, W, 0x11);
}

void FSSXOverlayManager::DrawNextRace(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvNextRace];
	const int32 M = Mode();
	const int32 Track = Host ? Host->GetTrackId() : 0;
	const int32 Heat = Profile ? FMath::Clamp(Profile->Session.Heat, 0, 2) : 0;
	const int32 Humans = NumHumans();
	// 0x1B4120 / 0x1B4250 / 0x1B44C0 / 0x1B4890
	FTable T;
	FString TextStr;
	FString Message;
	bool bTargets = false;
	float DiscX = 50.f;
	TArray<FSSXRacerResult> Racers;
	TArray<int32> AiRows;
	if (M == 2 || M == 4 || M == 9)
	{
		if (Humans == 1)
		{
			T.SetRows(D, 137.f, 23.f);
			T.X[0] = 131.f; T.X[1] = 259.f; T.X[2] = 434.f; T.Cols = 2;
		}
		else
		{
			T.SetRows(D, 142.f, 29.f);
			T.X[0] = 116.f; T.X[1] = 244.f; T.X[2] = 419.f; T.X[3] = 594.f; T.Cols = 3;
		}
		static const int32 WcTitle[] = { 3659, 3660, 3658 };   // 0x1B3F50
		static const int32 WcText[] = { 466, 467, 465 };       // 0x390838
		T.Title = L(M == 4 ? WcTitle[Heat] : 519);
		TextStr = L(M == 4 ? WcText[Heat] : 453);
		if (Host) Host->GetEntrants(Racers);
		for (int32 I = 0; I < Racers.Num(); ++I)
		{
			if (Racers[I].Player >= 0) continue;
			T.Cell[0][T.NumRows] = Racers[I].Name;
			AiRows.Add(I);
			++T.NumRows;
		}
	}
	else if (M == 3 || M == 5)
	{
		T.X[0] = 131.f; T.X[1] = 410.f; T.Cols = 1;
		const int32 Best = Profile && Track >= 0 && Track < SSXProg::NumTracks ? Profile->Records[Track].Tables[1].Rows[0].Value : 0;
		Message = L(3647).Replace(TEXT("%d"), *FString::FromInt(Best));
		bTargets = true;
		if (M == 3)
		{
			T.Title = L(3687);
			if (Humans != 1) { TextStr = L(455); DiscX = 148.f; bTargets = false; }
			else { TextStr = L(454); }
		}
		else
		{
			T.Title = L(3694);
			TextStr = L(468);
		}
	}
	else if (M == 7)
	{
		T.Title = L(3734);
		TextStr = L(3189);
	}
	if (M == 4 || M == 5) T.bSmallTitle = true;
	const float A = B.TextAlpha;

	// 0x1B4950
	const float Saved = B.Alpha;
	B.Alpha = 0.8f;
	DrawVenueTitleBar(D, B, M == 7 ? 410.f : 460.f);
	if (M == 3 || M == 5)
	{
		Icon(D, 0x2E, DiscX, 150.f, 2.f, 2.f, 0, WithA(Gold, A), 7);
		Icon(D, 0x2E, DiscX, 215.f, 2.f, 2.f, 0, WithA(Yellow, A), 7);
		Icon(D, 0x2E, DiscX, 280.f, 2.f, 2.f, 0, WithA(RedTxt, A), 7);
	}
	if (M == 2 || M == 4)
	{
		// FUN_001A8FA0
		for (int32 R = 0; R < AiRows.Num(); ++R)
		{
			const FSSXRacerResult& Rr = Racers[AiRows[R]];
			for (int32 H = 0; H < FMath::Min(Humans, 2); ++H)
			{
				int32 Lc = 0; FLinearColor C;
				AggressionLabel(H == 0 ? Rr.Aggression : Rr.Aggression2, Lc, C);
				Txt(D, EFont::Menu, T.X[1 + H], T.Rows[R] + 3.f, 0.9f, C, L(Lc), 7);
			}
		}
	}
	if (bTargets)
	{
		// 0x390848
		static const int32 Medals[] = { HudMedalGold, HudMedalSilver, HudMedalBronze };
		for (int32 I = 0; I < 3; ++I) Icon(D, Medals[I], 426.f, 189.f + 40.f * I, 0.6f, 0.6f, 0, WithA(White, A), 7);
	}
	B.Alpha = Saved;

	// 0x1B4D98
	const float Top = (480.f - B.Height) * 0.5f + H37(D);
	if (M == 7)
	{
		FLinearColor W = White; W.A = A;
		for (int32 H = 0; H < FMath::Min(Humans, 2); ++H)
		{
			const float X = H == 0 ? 117.f : 330.f;
			if (Profile)
			{
				Txt(D, EFont::Title, X, Top + 90.f, 0.8f, W, Profile->Slot[H].UserName, 7);
				Txt(D, EFont::Title, X, Top + 120.f, 0.85f, W, SSXFeTables::RiderName(FMath::Max(Profile->Slot[H].Char, 0)), 7);
			}
		}
		FLinearColor G = Gold; G.A = A;
		Txt(D, EFont::Title, 117.f, Top + 170.f, 0.75f, G, L(3746), 7);
		Txt(D, EFont::Title, 345.f, Top + 170.f, 0.75f, G, L(3747), 7);
		Txt(D, EFont::Title, 480.f, Top + 170.f, 0.75f, G, L(3748), 7);
		// FUN_00163468
		if (Profile && Track >= 0 && Track < SSXProg::NumTracks)
		{
			const FSSXRecordRow& R = Profile->Records[Track].Tables[2].Rows[0];
			Txt(D, EFont::Menu, 117.f, Top + 200.f, 0.92f, W, TimeCs(Loc, R.Value), 7);
			Txt(D, EFont::Menu, 345.f, Top + 200.f, 0.92f, W, R.GetName(), 7);
			Txt(D, EFont::Menu, 480.f, Top + 200.f, 0.92f, W, SSXFeTables::RiderName(R.Char), 7);
		}
	}
	if (M == 2 || M == 4)
	{
		Txt(D, EFont::Title, T.X[0], T.Rows[0] - 48.f, 0.76f, WithA(Gold, A), L(3680), 7);
		Txt(D, EFont::Title, Humans == 1 ? T.X[1] : T.X[1] + 65.f, T.Rows[0] - 48.f, 0.76f, WithA(Gold, A), L(3676), 7);
		for (int32 H = 0; H < FMath::Min(Humans, 2); ++H)
		{
			const FString Name = Profile ? Profile->Slot[H].UserName : FString();
			Txt(D, EFont::Title, T.X[1 + H], T.Rows[0] - 23.f, 0.47f, WithA(Gold, A), L(3767).Replace(TEXT("%S"), *Name), 7);
		}
	}
	DrawTable(D, B, T, M);
	if (!Message.IsEmpty() && M != 7) Txt(D, EFont::Title, 116.f, 118.f, 0.7f, WithA(Gold, A), Message, 7);
	FOvBox Box;
	Box.X = 116.f; Box.Y = (M == 3 || M == 5) ? 346.f : (M == 7 ? 320.f : 352.f); Box.W = 380.f;
	Box.Scale = 0.88f; Box.Color = WithA(Gold, A);
	TextBox(D, Box, TextStr, 7);
	if (M == 3 || M == 5)
	{
		for (int32 I = 0; I < 3; ++I) Txt(D, EFont::Menu, DiscX + 60.f, 170.f + 65.f * I, 0.92f, WithA(White, A), L(516 + I), 7);
	}
	if (bTargets)
	{
		int32 Th[3] = { 0, 0, 0 };
		if (SSXProg::ShowoffThresholds(Track, Th))
		{
			for (int32 I = 0; I < 3; ++I)
			{
				Txt(D, EFont::Menu, 486.f, 194.f + 40.f * I, 0.92f, WithA(White, A), L(389).Replace(TEXT("%S"), *FString::FromInt(Th[I])), 7);
			}
		}
	}
}

void FSSXOverlayManager::DrawResults(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvRaceResults];
	if (Dialog.bActive) { Dialog.Draw(D, *Loc, B); return; }   // 0x1B1CF0 / 0x1B2428
	if (SaveFrames > 0) { DrawSaveBox(D, B); return; }
	const int32 M = Mode();
	const int32 Humans = NumHumans();
	const float Ch = H37(D);
	const float A = B.TextAlpha;
	const bool bShowoff = M == 3 || M == 5;
	FTable T;
	for (int32 Act : ResultsItems())
	{
		int32 Id = 398;
		switch (Act)
		{
		case 0: Id = M == 1 ? 3510 : (M == 4 ? (Profile && Profile->Session.Heat < 2 ? 3511 : 3509) : 3509); break;
		case 1: Id = 400; break;
		case 2: Id = 401; break;
		case 3: Id = 398; break;
		case 4: Id = 403; break;
		}
		const bool bPending = PointSession() && PointSession()->Left > 0;
		if (Act == 0 && M == 4 && Profile && Profile->Session.Heat >= 2 && bPending) Id = 397;
		if (Act == 0 && bShowoff && ((M == 5 && bPending) || (Host && Host->IsCircuitBeaten()))) Id = 397;
		T.Menu.Add(L(Id).TrimEnd());
	}
	T.MenuSel = ResultsSel;
	if (Host && Host->IsResultsMenuHidden()) T.Menu.Reset();
	TArray<FSSXRacerResult> Rows;
	if (Host) Host->GetResults(Rows);
	bool bMedalRows = false;
	int32 HumanPlace = -1;
	FString Message;
	if (M == 2 || M == 4 || M == 9)
	{
		static const int32 WcTitle[] = { 3666, 3669, 3650 };   // 0x1AF470
		const int32 Heat = Profile ? FMath::Clamp(Profile->Session.Heat, 0, 2) : 0;
		T.Title = L(M == 4 ? WcTitle[Heat] : 3671);
		if (Humans == 1 || M == 4)
		{
			// 0x1B0188 / 0x1AF950
			T.SetRows(D, Ch + 79.f, 18.f);
			const float X[] = { 81.f, 164.f, 292.f, 492.f, 582.f };
			for (int32 C = 0; C < 5; ++C) T.X[C] = X[C];
			T.Cols = 4;
			T.Align[3] = 2;
			T.Head[0] = L(3701); T.Head[1] = L(3677); T.Head[2] = L(3676); T.Head[3] = L(3681);
		}
		else
		{
			T.SetRows(D, Ch + 87.f, 16.f);
			const float X[] = { 50.f, 133.f, 261.f, 381.f, 491.f, 581.f };
			for (int32 C = 0; C < 6; ++C) T.X[C] = X[C];
			T.Cols = 5;
			T.Align[4] = 2;
			T.Head[0] = L(3701); T.Head[1] = L(3677); T.Head[4] = L(3681);
		}
		if (M == 4 && Heat == 2)
		{
			bMedalRows = true;
			for (int32 I = 1; I < 16; ++I) T.Rows[I] = T.Rows[I - 1] + 37.f;
		}
		else if (M == 4)
		{
			Message = L((Host && Host->IsEliminated()) ? 412 : (Heat == 0 ? 415 : 414));
		}
		const int32 TimeCol = T.Cols - 1;
		for (int32 R = 0; R < Rows.Num() && R < 16; ++R)
		{
			const FSSXRacerResult& X = Rows[R];
			T.Cell[0][R] = FString::Printf(TEXT("%d%s"), R + 1, *PlaceSuffix(Loc, R + 1));
			T.Cell[1][R] = X.Name;
			T.Cell[TimeCol][R] = TimeCs(Loc, SecToCs(X.TimeSec));
			T.RowTint[R] = X.Player >= 0 ? 0 : -1;
			if (bMedalRows && X.Player >= 0 && HumanPlace < 0) HumanPlace = R;
		}
		T.NumRows = FMath::Min(Rows.Num(), 16);
	}
	else if (bShowoff)
	{
		// 0x1B0BB8 / FUN_001B0998
		T.Title = L(3670);
		T.SetRows(D, 140.f, 3.f);
		if (M == 5 && Humans == 1 && Profile && Host)
		{
			// 0x1B0BB8 / 0x1B1BA8 / 0x16BAA8 / 0x163E28
			FSSXShowoffStats S0;
			Host->GetShowoffStats(0, S0);
			const int32 Run = SSXProg::ShowoffMedal(S0.Score, Host->GetTrackId());
			const int32 Stored = Profile->VenueMedal(5, Host->GetTrackId());
			Message = L(Stored == SSXProg::NoMedal ? 585 : (Stored < Run ? 584 : (Run == 0 ? 583 : 582)));
		}
		const float W1 = Humans == 1 ? 160.f : 140.f, W2 = Humans == 1 ? 90.f : 113.f;
		T.X[0] = Humans == 1 ? 191.f : 20.f;
		T.X[1] = T.X[0] + W1; T.X[2] = T.X[1] + W2; T.X[3] = T.X[2] + 32.f; T.X[4] = T.X[3] + W1; T.X[5] = T.X[4] + W2; T.X[6] = T.X[5] + 32.f;
		const int32 Al[] = { 0, 2, 2, 0, 2, 2 };
		for (int32 C = 0; C < 6; ++C) T.Align[C] = Al[C];
		T.Cols = 3 * FMath::Min(Humans, 2);
		T.RowTint[0] = 0;
		static const int32 Labels[] = { 3524, 502, 507, 508, 510, 506 };
		int32 BestScore = -1, Winners = 0;
		int32 Score[2] = { 0, 0 };
		for (int32 H = 0; H < FMath::Min(Humans, 2); ++H)
		{
			FSSXShowoffStats S;
			if (Host) Host->GetShowoffStats(H, S);
			Score[H] = S.Score;
			const int32 C0 = H * 3;
			for (int32 I = 0; I < 6; ++I) T.Cell[C0][I] = L(Labels[I]);
			T.Cell[C0 + 1][0] = FString::FromInt(S.Score);
			T.Cell[C0 + 1][1] = FString::FromInt(S.SpinDeg / 180);
			T.Cell[C0 + 1][2] = FString::FromInt(S.FlipDeg / 360);
			T.Cell[C0 + 1][3] = FString::FromInt(S.Grabs);
			T.Cell[C0 + 1][4] = FString::FromInt(S.Rails);
			T.Cell[C0 + 1][5] = TimeCs(Loc, (int32)(float(S.BiggestAir) * 100.f));
		}
		T.NumRows = 6;
		if (Humans >= 2)
		{
			BestScore = FMath::Max(Score[0], Score[1]);
			Winners = (Score[0] == BestScore) + (Score[1] == BestScore);
			if (Winners == 1) { T.ColTint[Score[0] == BestScore ? 1 : 4] = 0; }
		}
	}
	else if (M == 1)
	{
		// 0x1B16F0 / FUN_001B1278
		T.Title = L(3654);
		T.SetRows(D, Ch + 79.f, 3.f);
		const bool b1 = Humans == 1;
		T.X[0] = b1 ? 192.f : 70.f;
		T.X[1] = T.X[0] + (b1 ? 160.f : 151.f); T.X[2] = T.X[1] + (b1 ? 90.f : 80.f); T.X[3] = T.X[2] + (b1 ? 40.f : 30.f);
		T.X[4] = T.X[3] + (b1 ? 160.f : 151.f); T.X[5] = T.X[4] + (b1 ? 90.f : 80.f);
		const int32 Al[] = { 0, 2, 1, 0, 2, 0 };
		for (int32 C = 0; C < 6; ++C) T.Align[C] = Al[C];
		T.Cols = b1 ? 2 : 5;
		static const int32 Labels[] = { 3558, 504, 514, 515, 506, 503, 509, 511, -1, 513 };
		for (int32 H = 0; H < FMath::Min(Humans, 2); ++H)
		{
			FSSXShowoffStats S;
			if (Host) Host->GetShowoffStats(H, S);
			const int32 C0 = H * 3;
			for (int32 I = 0; I < 10; ++I) T.Cell[C0][I + 1] = Labels[I] >= 0 ? L(Labels[I]) : FString();
			const bool bMph = Profile && Profile->Settings.Units != 0;
			const int32 Speed = bMph ? (int32)(float(S.AvgSpeed) * 0.621f) : (int32)float(S.AvgSpeed);
			T.Cell[C0 + 1][1] = FString::FromInt(S.StylePoints);
			T.Cell[C0 + 1][2] = FString::Printf(TEXT("%d%s"), Speed, bMph ? TEXT(" mph") : TEXT(" km/h"));
			T.Cell[C0 + 1][3] = FString::FromInt(S.TricksLanded);
			T.Cell[C0 + 1][4] = FString::FromInt(S.Wipeouts);
			T.Cell[C0 + 1][5] = TimeCs(Loc, (int32)(float(S.BiggestAir) * 100.f));
			T.Cell[C0 + 1][6] = TimeCs(Loc, (int32)(float(S.AirTime) * 100.f));
			T.Cell[C0 + 1][7] = TimeCs(Loc, (int32)(float(S.GrabTime) * 100.f));
			T.Cell[C0 + 1][8] = TimeCs(Loc, (int32)(float(S.RailTime) * 100.f));
			T.Cell[C0 + 1][10] = TimeCs(Loc, SecToCs(S.TotalTime));
		}
		T.NumRows = 11;
	}
	else if (M == 7)
	{
		T.Title = L(3740);
		T.SetRows(D, Ch + 119.f, 3.f);
	}
	else if (M == 6)
	{
		T.Title = L(3655);
		T.SetRows(D, 142.f, 3.f);
		T.X[0] = 131.f; T.X[1] = 400.f;
	}

	// 0x1B1CF0
	DrawVenueTitleBar(D, B, M == 7 ? 380.f : 460.f);
	const bool bBlink = BlinkOn(OvRaceResults);
	static const int32 Medals[] = { HudMedalGold, HudMedalSilver, HudMedalBronze };   // 0x390828
	if (!bShowoff)
	{
		if (bMedalRows && HumanPlace >= 0 && HumanPlace < 3)
		{
			Icon(D, Medals[HumanPlace], T.X[0] - 5.f, 130.f + 37.f * HumanPlace, 0.55f, 0.55f, 8, WithA(White, A * (bBlink ? 0.7f : 1.f)), 7);
		}
		else if (M == 1 && Humans == 2)
		{
			D.Rect(316.5f, 120.f, 3.f, 214.f, White, 5);   // FUN_001AB028
		}
	}
	else
	{
		if (Humans == 2) D.Rect(286.5f, 155.f, 3.f, 260.f, White, 5);   // FUN_001AB028
		// 0x16BAA8
		if (Humans == 1)
		{
			FSSXShowoffStats S;
			if (Host) Host->GetShowoffStats(0, S);
			const int32 Medal = SSXProg::ShowoffMedal(S.Score, Host ? Host->GetTrackId() : 0);
			if (Medal >= 0 && Medal <= 2) Icon(D, Medals[Medal], 166.f, 112.f, 0.67f, 0.67f, 0, WithA(White, bBlink ? A : A * 0.3f), 7);
		}
	}

	// 0x1B2428
	DrawTable(D, B, T, M);
	const FLinearColor G = WithA(Gold, A);
	if (bShowoff)
	{
		auto StatsBox = [&](float X, float Y, float W, int32 Player, int32 Align)
		{
			// FUN_001B2158
			FSSXShowoffStats S;
			if (Host) Host->GetShowoffStats(Player, S);
			const FString Lb = L(505);
			float Lx = Align == 2 ? (X + W) - Ink(D, EFont::Title, Lb, 0.66f) : X;
			if (Humans == 1) Lx -= 25.f;
			Txt(D, EFont::Title, Lx, Y, 0.66f, G, Lb, 7);
			FOvBox Bx;
			Bx.X = X; Bx.Y = Y + LineH(D, EFont::Title) * 0.66f + 6.f; Bx.W = W; Bx.Scale = 0.83f; Bx.Align = Align;
			TextBox(D, Bx, S.BestTrick, 7);
		};
		if (Humans == 1)
		{
			if (Profile) Txt(D, EFont::Title, 226.f, 122.f, 0.7f, G, Profile->Slot[0].UserName, 7);
			StatsBox(191.f, 293.f, 322.f, 0, 0);
		}
		else
		{
			if (Profile)
			{
				Txt(D, EFont::Title, 78.f, 122.f, 0.64f, G, Profile->Slot[0].UserName, 7);
				Txt(D, EFont::Title, 363.f, 122.f, 0.64f, G, Profile->Slot[1].UserName, 7);
			}
			StatsBox(108.f, 273.f, 165.f, 0, 2);
			StatsBox(303.f, 273.f, 165.f, 1, 0);
		}
	}
	if (M == 1 && Profile)
	{
		if (Humans == 1) { Txt(D, EFont::Title, 192.f, 122.f, 0.7f, G, Profile->Slot[0].UserName, 7); }
		else
		{
			Txt(D, EFont::Title, 70.f, 122.f, 0.7f, G, Profile->Slot[0].UserName, 7);
			Txt(D, EFont::Title, 330.f, 122.f, 0.7f, G, Profile->Slot[1].UserName, 7);
		}
	}
	if (M == 2 || M == 4)
	{
		if (Humans == 2)
		{
			Txt(D, EFont::Title, T.X[2] + 35.f, T.Rows[0] - 45.f, 0.65f, G, L(3676), 7);
			for (int32 H = 0; H < 2; ++H)
			{
				const FString Name = Profile ? Profile->Slot[H].UserName : FString();
				Txt(D, EFont::Menu, T.X[2 + H], T.Rows[0] - 23.f, 0.6f, G, L(3550).Replace(TEXT("%S"), *Name), 7);
			}
		}
		// FUN_001A91C8
		bAggressionDrawn = true;
		const float Anim = AggressionAnim / 2.5f;
		for (int32 R = 0; R < T.NumRows; ++R)
		{
			if (Rows[R].Player >= 0) continue;
			for (int32 H = 0; H < FMath::Min(Humans, 2); ++H)
			{
				const float Cur = (float)(H == 0 ? Rows[R].Aggression : Rows[R].Aggression2);
				const float To = (float)(H == 0 ? Rows[R].AggressionTo : Rows[R].AggressionTo2);
				const float X = T.X[2 + H], Y = T.Rows[R] + 3.f;
				const float Dlt = To - Cur;
				D.Rect(X, Y, 70.f, 13.f, Black, 0x10);
				const float Wb = ((Dlt < 0.f ? To : Cur) * 70.f) / 100.f;
				D.Rect(X, Y, Wb, 13.f, FeColor(1.f, 0.f, 0.f, 0.8f), 0x11);
				const float Wd = (Dlt * 70.f) / 100.f;
				if (Wd > 0.f) D.Rect(X + Wb, Y, Wd, 13.f, FeColor(1.f, 0.7f, 0.1f, 0.1f), 0x11);
				else if (Wd < 0.f) D.Rect(X + Wb + Wd, Y, -Wd, 13.f, FeColor(1.f, 0.1f, 0.7f, 0.1f), 0x11);
				if (Humans == 1)
				{
					const FString Ds = Dlt > 0.f ? FString::Printf(TEXT("+%2.f ="), Dlt) : (Dlt < 0.f ? FString::Printf(TEXT("-%2.f ="), Dlt) : FString(TEXT(" =")));
					TxtR(D, EFont::Menu, X + 150.f, Y, 0.65f, White, Ds, 0x11);
				}
				Txt(D, EFont::Menu, X + (Humans == 1 ? 155.f : 77.f), Y, 0.65f, White, FString::Printf(TEXT("%2.f%%"), To), 0x11);
				int32 Lc = 0; FLinearColor C;
				AggressionLabel((int32)To, Lc, C);
				TxtC(D, EFont::Menu, X + 35.f, Y - 13.f, 0.65f, C, L(Lc), 0x11);
				if (Humans == 1)
				{
					if (Dlt < 0.f) Icon(D, HudArrowL, (X - 20.f) - Anim, Y, 0.6f, 0.6f, 0, GreenTxt, 0x11);
					else if (Dlt > 0.f) Icon(D, HudArrowR, X + 76.f + Anim, Y, 0.6f, 0.6f, 0, RedTxt, 0x11);
				}
			}
		}
	}
	if (M == 7)
	{
		Txt(D, EFont::Title, 40.f, 175.f, 0.76f, G, L(3677), 7);
		Txt(D, EFont::Title, 200.f, 175.f, 0.76f, G, L(3758), 7);
		Txt(D, EFont::Title, 320.f, 175.f, 0.76f, G, L(3681), 7);
		Txt(D, EFont::Title, 430.f, 175.f, 0.76f, G, L(3739), 7);
		int32 FirstDiff = 0;
		int32 HumanIdx = 0;
		for (const FSSXRacerResult& X : Rows)
		{
			if (X.Player < 0 || HumanIdx >= Humans) continue;
			const float Y = HumanIdx * 25.f + 200.f;
			const FLinearColor W = WithA(White, A);
			const int32 Slot = FMath::Clamp(X.Player, 0, 1);
			if (Profile)
			{
				Txt(D, EFont::Menu, 40.f, Y, 0.92f, W, Profile->Slot[Slot].UserName, 7);
				Txt(D, EFont::Menu, 200.f, Y, 0.92f, W, SSXFeTables::RiderName(FMath::Max(Profile->Slot[Slot].Char, 0)), 7);
			}
			const int32 Cs = SecToCs(X.TimeSec);
			Txt(D, EFont::Menu, 320.f, Y, 0.92f, W, DiffCs(Cs), 7);
			const int32 Diff = Cs - TimeToBeatCs;
			if (HumanIdx == 0) FirstDiff = Diff;
			Txt(D, EFont::Menu, 430.f, Y, 0.92f, WithA(Diff < 0 ? GreenTxt : RedTxt, A), DiffCs(Diff), 7);
			++HumanIdx;
		}
		FOvBox Bx;
		Bx.X = 115.f; Bx.Y = 300.f; Bx.W = 340.f; Bx.Scale = 0.8f; Bx.Color = G;
		TextBox(D, Bx, L(FirstDiff < 0 ? 3190 : 3191), 7);
	}
	if (!Message.IsEmpty())
	{
		FOvBox Bx;
		Bx.X = 70.f; Bx.Y = (bMedalRows || bShowoff) ? 375.f : 360.f; Bx.W = 340.f; Bx.Scale = 0.8f; Bx.Color = G;
		TextBox(D, Bx, Message, 7);
	}
}

void FSSXOverlayManager::BuildRecordTable(FTable& T, int32 Track, int32 Kind, int32 Highlight) const
{
	// 0x1B3A00 / 0x1B6D60
	T.X[0] = 131.f; T.X[1] = 214.f; T.X[2] = 342.f; T.X[3] = 415.f; T.X[4] = 505.f;
	T.Align[3] = 2;
	T.Cols = 4;
	if (!Profile || Track < 0 || Track >= SSXProg::NumTracks || Kind < 0 || Kind > 2) return;
	const FSSXRecordTable& R = Profile->Records[Track].Tables[Kind];
	const bool bTime = R.IsTime != 0;
	if (Mode() != 7)
	{
		T.Head[0] = L(3679); T.Head[1] = L(3677); T.Head[2] = L(461); T.Head[3] = L(bTime ? 3681 : 3678);
	}
	const int32 N = FMath::Clamp(R.Count, 0, 5);
	T.Title = L(bTime ? 3614 : 3615).Replace(TEXT("%d"), *FString::FromInt(N));   // 0x1AACB0
	if (Kind == 2) T.Title = L(3745);
	for (int32 I = 0; I < N; ++I)
	{
		const FSSXRecordRow& Row = R.Rows[I];
		T.Cell[0][I] = FString::FromInt(I + 1);
		T.Cell[1][I] = I == Highlight ? FString() : Row.GetName();
		T.Cell[2][I] = SSXFeTables::RiderName(Row.Char);
		T.Cell[3][I] = bTime ? TimeCs(Loc, Row.Value) : FString::FromInt(Row.Value);
		T.RowTint[I] = I == Highlight ? 2 : -1;
		if (Mode() == 7) { for (int32 C = 0; C < 4; ++C) T.Cell[C][I].Reset(); }
	}
	T.NumRows = N;
}

void FSSXOverlayManager::DrawViewHiScores(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvViewHiScores];
	// 0x1B3E30
	DrawVenueTitleBar(D, B, 460.f);
	DrawList(D, 600.f, 430.f, 0.73f, 3, { L(471) }, -1, nullptr, HudTriangle, B.TextAlpha, 7);
	const int32 M = Mode();
	FTable T;
	T.SetRows(D, H37(D) + 79.f, 3.f);
	BuildRecordTable(T, Host ? Host->GetTrackId() : 0, (M == 3 || M == 5) ? 1 : 0, -1);
	DrawTable(D, B, T, M);
}

void FSSXOverlayManager::DrawHiScoreRecord(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvHiScoreRecord];
	const int32 M = Mode();
	const bool bTC = M == 7;
	const float A = B.TextAlpha;
	// 0x1B7078 / 0x1BD898
	DrawVenueTitleBar(D, B, bTC ? 410.f : 460.f);
	const float X0 = 166.f, Gy = 316.f, Px = 23.f, Py = 22.f;
	const bool bBlink = BlinkOn(OvHiScoreRecord);
	Icon(D, 0x36, X0 + 1.f + HiGridX * Px, (Gy + HiGridY * Py) - 1.f, 1.12f, 1.1f, 0x20, WithA(White, A * (bBlink ? 0.5f : 0.2f)), 7);
	if (!Profile || !Profile->Session.PendingRecords.IsValidIndex(HiScorePending)) return;
	const FSSXPendingRecord& E = Profile->Session.PendingRecords[HiScorePending];
	FTable T;
	T.SetRows(D, H37(D) + 79.f, 3.f);
	BuildRecordTable(T, E.Track, E.Kind, E.Row);
	const float RowY = T.Rows[FMath::Clamp(E.Row, 0, 15)];
	if (bTC)
	{
		D.Rect(213.f, 287.f, 200.f, 25.f, FeColor(0.3f, 1.f, 1.f, 1.f), 7);
	}
	else
	{
		Icon(D, 0x32, T.X[1] - 15.f, RowY + 2.f, 1.2f, 1.2f, 0, FeColor(bBlink ? A : A * 0.4f, 1.f, 1.f, 0.f), 7);
	}
	// 0x1B73E0 / 0x3382A0
	DrawTable(D, B, T, M);
	using namespace SSXFeUserName;
	const FLinearColor W = WithA(White, A);
	for (int32 Gr = 0; Gr < Rows; ++Gr)
	{
		for (int32 Gc = 0; Gc < Cols; ++Gc)
		{
			const TCHAR C = Cell(Gc, Gr);
			const float Cx = X0 + Gc * Px, Cy = Gy + Gr * Py;
			if (C == TEXT('e')) { TxtN(D, EFont::Menu, Cx - Ink(D, EFont::Menu, L(274), 0.55f) * 0.5f, Cy + 4.f, 0.55f, W, L(274), 7); }
			else if (C == TEXT('d')) { Icon(D, 0x32, Cx - 5.f, Cy + 3.f, 1.23f, 1.23f, 1, W, 7); }
			else if (C != 0)
			{
				const FString S(1, &C);
				TxtN(D, EFont::Menu, Cx - Ink(D, EFont::Menu, S, 1.f) * 0.5f, Cy, 1.f, W, S, 7);
			}
		}
	}
	const FLinearColor G = WithA(Gold, A);
	if (bTC)
	{
		FOvBox Bx; Bx.Font = EFont::Title; Bx.X = 132.f; Bx.Y = 135.f; Bx.W = 370.f; Bx.Scale = 0.7f; Bx.Color = G; Bx.Align = 1;
		TextBox(D, Bx, L(3744), 7);
		FLinearColor Yl = Yellow; Yl.A = A;
		TxtR(D, EFont::Title, 300.f, 190.f, 0.65f, Yl, L(3742), 7);
		TxtR(D, EFont::Title, 300.f, 210.f, 0.65f, Yl, L(3741), 7);
		TxtR(D, EFont::Title, 300.f, 230.f, 0.65f, Yl, L(3743), 7);
		FLinearColor Wv = White; Wv.A = A;
		const int32 New = Profile->Records[E.Track].Tables[2].Rows[0].Value;
		TxtR(D, EFont::Menu, 400.f, 190.f, 0.9f, Wv, TimeCs(Loc, New), 7);
		TxtR(D, EFont::Menu, 400.f, 210.f, 0.9f, Wv, TimeCs(Loc, TimeToBeatCs), 7);
		FLinearColor Gn = GreenTxt; Gn.A = A;
		TxtR(D, EFont::Menu, 400.f, 230.f, 0.9f, Gn, DiffCs(New - TimeToBeatCs), 7);
		FLinearColor Gd = Gold; Gd.A = A;
		TxtC(D, EFont::Title, 320.f, 260.f, 0.7f, Gd, L(3617), 7);
		if (!HiName.IsEmpty()) TxtC(D, EFont::Menu, 320.f, 290.f, 1.f, G, HiName, 0x13);
	}
	else if (!HiName.IsEmpty())
	{
		Txt(D, EFont::Menu, T.X[1], RowY, 0.85f, G, HiName, 7);
	}
	if (!(Host && Host->IsResultsMenuHidden()))
	{
		const bool bEnd = HiGridY == 2 && HiGridX == 13;
		DrawList(D, 600.f, bTC ? 405.f : 430.f, 0.73f, 3, { L(bEnd ? 397 : 275) }, -1, nullptr, HudCross, A, 7);
	}
	if (!bTC)
	{
		TxtC(D, EFont::Title, 320.f, 256.f, 0.7f, G, L(3616), 7);
		TxtC(D, EFont::Title, 320.f, 280.f, 0.7f, G, L(3617), 7);
	}
}

void FSSXOverlayManager::DrawUnlocked(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvUnlocked];
	const float A = B.TextAlpha;
	// 0x1B6468
	DrawVenueTitleBar(D, B, 460.f);
	if (NumHumans() != 2 && UnlockRows.Num() > 0 && UnlockStep != 0)
	{
		const int32 Row = UnlockStep >= 1 && UnlockStep <= UnlockRows.Num() ? UnlockStep - 1 : UnlockRows.Num() - 1;
		if (UnlockRows[Row].Icon > 0) Icon(D, UnlockRows[Row].Icon, 44.f, 160.f, 1.f, 1.f, 0, White, 7);
	}
	// 0x1B65F0
	const float Ch = H37(D);
	float Y = Ch + 10.f + 7.f + 23.f + 5.f;
	const FLinearColor G = WithA(Gold, A);
	for (int32 P : UnlockPlayers)
	{
		const FString Name = Profile ? Profile->Slot[FMath::Clamp(P, 0, 1)].UserName : FString();
		Txt(D, EFont::Title, 116.f, Y, 0.656f, G, L(3574) + Name, 7);
		Txt(D, EFont::Title, 116.f, Y + 28.f, 0.8f, G, L(3575), 7);
		Y += 148.f;
	}
	// FUN_0019DF90
	FTable T;
	T.SetRows(D, Ch + 10.f + 7.f + 23.f + 51.f, NumHumans() == 2 ? 10.f : 13.f);
	for (int32 I = 0; I < UnlockRows.Num() && I < 16; ++I)
	{
		const FString& S = UnlockRows[I].Text;
		if (S.IsEmpty()) continue;
		const int32 N = UnlockReveal(I);
		if (N >= S.Len())
		{
			Txt(D, EFont::Menu, 156.f, T.Rows[I], 0.85f, White, S, 7);
			continue;
		}
		const FString Head = S.Left(N);
		if (!Head.IsEmpty()) Txt(D, EFont::Menu, 156.f, T.Rows[I], 0.85f, White, Head, 7);
		float Xc = 156.f + Ink(D, EFont::Menu, Head, 0.85f);
		if (N > 0 && S[N - 1] == TEXT(' ')) Xc += 0.85f * 6.f;
		Txt(D, EFont::Menu, Xc, T.Rows[I], 0.85f, FeColor(0.5f, 1.f, 1.f, 1.f), S.Mid(N, 1), 7);
	}
	if (Profile && (Profile->Session.bRaceCircuitBeaten || Profile->Session.bShowoffCircuitBeaten))
	{
		FOvBox Bx; Bx.X = 65.f; Bx.Y = 330.f; Bx.W = 450.f; Bx.Scale = 0.85f;
		TextBox(D, Bx, L(Profile->Session.bRaceCircuitBeaten ? 3228 : 3229), 7);
	}
	DrawList(D, 600.f, (B.Height + 480.f) * 0.5f - 40.f, 0.73f, 3, { L(397) }, 0, nullptr, HudCross, A, 7);
}

void FSSXOverlayManager::DrawNextEvent(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvSelectNextEvent];
	// 0x1BCD28
	if (Dialog.bActive) { Dialog.Draw(D, *Loc, B); return; }
	if (SaveFrames > 0) { DrawSaveBox(D, B); return; }
	const float A = B.TextAlpha;
	B.Box(D, 300.f, 0xE);
	const float Ty = H37(D) + 90.f + 7.f;
	Txt(D, EFont::Title, 116.f, Ty, 1.f, WithA(White, A), L(3657), 0x10);
	const int32 M = Mode();
	const int32 Which = (M == 4 || M == 5) ? 3705 : (M == 1 ? 3707 : 3706);
	FOvBox Bx; Bx.Font = EFont::Title; Bx.X = 134.f; Bx.Y = Ty + 32.f; Bx.W = 390.f; Bx.Scale = 0.62f; Bx.Color = WithA(Gold, A);
	TextBox(D, Bx, FString::Printf(TEXT("%s %s %s"), *L(3702), *L(Which), *L(3704)), 0x10);
	if (bNextEventBack)
	{
		const float W = Ink(D, EFont::Menu, L(471), 0.73f);
		DrawList(D, 600.f - W, 350.f, 0.73f, 2, { L(471) }, -1, nullptr, HudTriangle, A, 0x10);
	}
	const bool bWC = M == 4 || M == 5;
	const TArray<FString> Strs = { L(411), L(457), L(3194), L(456), L(399) };
	const TArray<bool> En = { true, true, !bWC, true, true };
	DrawList(D, 320.f, 240.f, 0.92f, 4, Strs, NextEventSel, &En, HudCross, A, 0x10);
}

void FSSXOverlayManager::DrawNextVenue(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvSelectNextVenue];
	if (Dialog.bActive) { Dialog.Draw(D, *Loc, B); return; }
	if (SaveFrames > 0) { DrawSaveBox(D, B); return; }
	const float A = B.TextAlpha;
	// 0x1BBF20
	B.Box(D, 460.f, 0xE);
	const float Ty = H37(D) + 10.f + 7.f;
	Txt(D, EFont::Title, 116.f, Ty, 1.f, WithA(White, A), L(3662), 0x10);
	const int32 Mv = NextVenueMode;
	const int32 Which = Mv == 4 ? 3753 : Mv == 5 ? 3754 : Mv == 1 ? 3707 : Mv == 2 ? 3755 : Mv == 3 ? 3756 : 3757;
	FOvBox Bx; Bx.Font = EFont::Title; Bx.X = 134.f; Bx.Y = Ty + 40.f; Bx.W = 390.f; Bx.Scale = 0.62f; Bx.Color = WithA(Gold, A);
	TextBox(D, Bx, FString::Printf(TEXT("%s %s %s"), *L(3702), *L(Which), *L(3703)), 0x10);
	const TArray<int32> List = NextVenueList();
	TArray<FString> Strs;
	TArray<bool> En;
	for (int32 Tk : List)
	{
		Strs.Add(Tk == -2 ? L(399) : (Tk == -1 ? FString() : L(SSXFeTables::TrackNameLoc(Tk))));
		En.Add(Tk < 0 || !Profile || Profile->IsVenueUnlocked(Mv, Tk, Cheats && Cheats->bUnlockAll));
	}
	DrawList(D, 320.f, 170.f, 0.92f, 4, Strs, NextVenueSel, &En, HudCross, A, 0x10);
	if (List.IsValidIndex(NextVenueSel) && List[NextVenueSel] >= 0 && En[NextVenueSel])
	{
		Icon(D, 0x45 + List[NextVenueSel], 116.f, 170.f, 1.f, 1.f, 0, WithA(White, A), 0x10);
	}
	const float Wb = Ink(D, EFont::Menu, L(471), 0.73f);
	DrawList(D, 600.f - Wb, 430.f, 0.73f, 2, { L(471) }, -1, nullptr, HudTriangle, A, 0x10);
	if (Mv == 4 || Mv == 5)
	{
		// FUN_00163E28
		Txt(D, EFont::Menu, 450.f, 148.f, 0.9f, Gold, L(342), 0x10);
		static const int32 MedalLoc[] = { 303, 309, 300, 306 };
		const float Pitch = LineH(D, EFont::Menu) * 0.92f + 4.f;
		for (int32 I = 0; I < List.Num(); ++I)
		{
			if (List[I] < 0) break;
			const int32 Medal = Profile ? Profile->VenueMedal(Mv, List[I]) : SSXProg::NoMedal;
			Txt(D, EFont::Menu, 450.f, Pitch * I + 170.f, 0.92f, En[I] ? White : ListGrey, L(MedalLoc[FMath::Clamp(Medal, 0, 3)]), 0x10);
		}
	}
}

void FSSXOverlayManager::DrawPoints(FSSXFeDrawList& D) const
{
	FSSXBackdrop& B = Backdrop[OvPointDistribution];
	if (Dialog.bActive) { Dialog.Draw(D, *Loc, B); return; }
	if (SaveFrames > 0) { DrawSaveBox(D, B); return; }
	if (RankCardFrames > 0) { DrawRankCard(D); return; }
	const float A = B.TextAlpha;
	const int32 Char = Profile ? FMath::Clamp(Profile->Slot[0].Char, 0, SSXProg::NumRiders - 1) : 0;
	const FSSXPointSession* PS = PointSession();
	// 0x1BA788
	B.Box(D, 460.f, 0xC);
	Txt(D, EFont::Title, 115.f, 48.f, 0.77f, White, L(3750), 0xF);
	const int32 Left = PS ? PS->Left : 0;
	const int32 Initial = PS ? PS->Initial : 0;
	ProgressBar(D, 335.f, 244.f, Initial * 6.f, (float)Initial, (float)Left, -9999.f, 0xF);
	TxtR(D, EFont::Menu, 328.f, 240.f, 0.88f, White, L(3195), 0xF);
	static const int32 Labels[] = { 37, 39, 40, 41 };
	const SSXFeTables::FBoard& Bd = SSXFeTables::Board(Char, Profile ? Profile->Slot[0].Board : 0);
	for (int32 I = 0; I < 4; ++I)
	{
		const float Y = 270.f + 19.f * I;
		const bool bSel = I == PointSel;
		const FLinearColor C = bSel ? Gold : White;
		if (bSel)
		{
			Icon(D, HudArrowL, 182.f, Y - 3.f, 0.68f, 0.68f, 0, C, 0xF);
			Icon(D, HudArrowR, 455.f, Y - 3.f, 0.68f, 0.68f, 0, C, 0xF);
		}
		TxtR(D, EFont::Menu, 177.f, Y - 5.f, 0.88f, C, L(Labels[I]), 0xF);
		const int32 Bonus = (int32)(Bd.Stats[I] * 0.1f);
		const int32 Base = PS ? PS->Cur[I] : 0;
		const int32 Max = (PS ? PS->Max[I] : 0) + Bonus;
		ProgressBar(D, 200.f, Y, Max * 5.f, (float)Max, (float)Base, (float)(Base + Bonus), 0xF);
		Txt(D, EFont::Menu, 470.f, Y - 5.f, 0.88f, C, FString::Printf(TEXT("%d/%d"), Base + Bonus, Max), 0xF);
	}
	TxtC(D, EFont::Title, 320.f, 113.f, 1.3f, Yellow, SSXFeTables::RiderName(Char), 0xF);
	TxtC(D, EFont::Title, 320.f, 75.f, 0.88f, Gold, L(3664), 0xF);
	FOvBox Bx; Bx.X = 130.f; Bx.Y = 165.f; Bx.W = 400.f; Bx.Scale = 0.9f; Bx.Color = WithA(White, A);
	TextBox(D, Bx, L(3192), 0xF);
	if (Left == 0)
	{
		DrawList(D, 600.f, 430.f, 0.73f, 3, { L(275) }, 0, nullptr, HudCross, A, 0xF);
	}
	else
	{
		FOvBox Bw; Bw.X = 115.f; Bw.Y = 370.f; Bw.W = 400.f; Bw.Scale = 0.8f; Bw.Color = WithA(White, A);
		TextBox(D, Bw, L(3193), 0xF);
	}
}

void FSSXOverlayManager::DrawRankCard(FSSXFeDrawList& D) const
{
	// 0x1BA788 / FUN_00164730
	FSSXBackdrop& B = Backdrop[OvPointDistribution];
	B.Box(D, 300.f, 0xC);
	const int32 NewLoc = 23 + (Profile ? FMath::Clamp(Profile->Session.RankAfter, 0, 9) : 9);
	TxtC(D, EFont::Menu, 320.f, 165.f, 0.9f, White, L(3365), 0xF);
	TxtC(D, EFont::Menu, 320.f, 200.f, 0.9f, White, L(3366), 0xF);
	TxtC(D, EFont::Menu, 320.f, 230.f, 1.2f, Gold, L(NewLoc + 1), 0xF);
	TxtC(D, EFont::Menu, 320.f, 265.f, 0.9f, White, L(3367), 0xF);
	TxtC(D, EFont::Menu, 320.f, 295.f, 1.2f, Gold, L(NewLoc), 0xF);
	TxtR(D, EFont::Menu, 600.f, 323.f, 0.87f, White, L(373), 0xF);
}

void FSSXOverlayManager::DrawReplayMenu(FSSXFeDrawList& D) const
{
	// FUN_001F01E8 / FUN_001A9928
	FSSXBackdrop& B = Backdrop[OvReplay];
	B.Box(D, 165.f, 0xF);
	DrawList(D, 320.f, 240.f, 0.92f, 0xC, { L(391), L(353), L(475) }, ReplaySel, nullptr, HudCross, B.TextAlpha, 0xF);
	DrawTab(D, 155.f, B, L(3667), 0x11);
}

void FSSXOverlayManager::DrawReplayControl(FSSXFeDrawList& D) const
{
	// 0x1EC808
	if (Host && Host->GetLesson()) return;
	if (ReplayCtl.bHidden) return;
	const FSSXReplayControl& C = ReplayCtl;
	const FSSXReplay* R = Host ? Host->GetReplay() : nullptr;
	const bool bBusy = C.bBusy;
	const float Y = C.PanelY;
	constexpr float Left = 0.f, Right = 640.f, Width = 640.f;
	const FLinearColor Arrow = bBusy ? FeColor(0.25f, 0.4f, 0.4f, 0.4f) : FeColor(0.43f, 1.f, 1.f, 1.f);
	const FLinearColor Wht = bBusy ? FeColor(0.5f, 0.4f, 0.4f, 0.4f) : White;
	const bool bPanel = C.bHelpChanged || C.HelpLevel != 0;
	if (bPanel)
	{
		const float Sw = HudW(D, HudBoxCorner, 128.f), Sh = H37(D);
		const float Sx = 340.f / Sw;
		const float Top = Y - 26.f;
		const float Lx = 340.f * 0.455f;
		if (!bBusy)
		{
			const FLinearColor Panel = FeColor(0.88f, 0.1f, 0.1f, 0.1f);
			D.Rect(Left, Y, Width, 480.f - Y, Panel, 10);
			const float Iw = 340.f - Lx;
			D.Rect(Lx - 20.f, Top + Sh * 0.26f, Iw + Iw, (Sh - Sh * 0.21f) - Sh * 0.26f, Panel, 10);
		}
		else
		{
			D.Rect(0.f, 0.f, 640.f, 480.f, FeColor(0.8f, 0.1f, 0.1f, 0.1f), 10);
		}
		const FLinearColor EdgeC = bBusy ? FeColor(0.5f, 0.5f, 0.5f, 0.5f) : White;
		Icon(D, HudBoxCorner, -20.f, Top, Sx, 1.f, 2, EdgeC, 12);
		Icon(D, HudBoxCorner, 320.f, Top, Sx, 1.f, 3, EdgeC, 12);
	}
	if (!C.bHelpChanged)
	{
		const FString Hint = L(C.HelpLevel == C.HelpMax ? 0x16A : 0x16B);
		const float Tw = Ink(D, EFont::Menu, Hint, 1.f);
		Icon(D, 0x61, (Right - Tw) - 42.f, Y - 43.f, 1.f, 1.f, 0x60, Arrow, 14);
		if (C.HelpLevel == C.HelpMax) { Icon(D, 0x65, (Right - Tw) - 42.f, Y - 33.f, 1.f, 1.f, 0x60, Wht, 15); }
		else { Icon(D, 0x64, (Right - Tw) - 41.f, Y - 52.f, 1.f, 1.f, 0x60, Wht, 15); }
	}

	// 0x1EDEF0 / 0x1ED490 / FUN_001EE660 / FUN_001EE6E8
	if (C.bEditor && bPanel && R && C.Timeline.Length > 0)
	{
		constexpr float SX = 640.f / 512.f, SY = 480.f / 448.f;
		const float H2a = HudH(D, 0x2A, 31.f), W2a = HudW(D, 0x2A, 1.f);
		// FUN_001A9F80
		const auto Fill = [&](float X, float Yy, float Wd, float Ht, const FLinearColor& Cl, int32 Ly)
		{
			Icon(D, 0x2A, X * SX, Yy * SY, Wd * SX / W2a, (Ht - 0.5f) * SY / H2a, 0, Cl, Ly);
		};
		// FUN_0019C1E8 / FUN_0019B108
		const auto Line = [&](float X, float Y0, float Y1, const FLinearColor& Cl, int32 Ly) { D.Rect(X * SX, Y0 * SY, SX, (Y1 - Y0) * SY, Cl, Ly); };
		const auto Box = [&](float X, float Yy, float Wd, float Ht, const FLinearColor& Cl, int32 Ly) { D.Rect(X * SX, Yy * SY, Wd * SX, Ht * SY, Cl, Ly); };
		const auto Cursor = [&](float X, float Yy, float Wd, float Sy, const FLinearColor& Cl, int32 Ly) { Icon(D, 0x2A, X * SX, Yy * SY, Wd * SX / W2a, Sy * SY, 0, Cl, Ly); };
		const SSXReplay::FCameraTrack* Track = C.Timeline.Track;
		const bool bPaused = R->GetState() == SSXReplay::Paused;
		const int32 Play = int32(R->GetPlayFrame());
		const auto ClipStart = [&](int32 Cl) { const int32 K = R->GetClip(Cl).Key; return K >= 0 ? int32(R->GetKey(K).Frame) : 0; };
		const FLinearColor TlWhite = bBusy ? FeColor(1.f, 0.4f, 0.4f, 0.4f) : FeColor(1.f, 1.f, 1.f, 1.f);
		const FLinearColor TlYellow = bBusy ? FeColor(0.7f, 0.4f, 0.4f, 0.f) : FeColor(0.7f, 1.f, 1.f, 0.f);
		const FLinearColor TlGold = bBusy ? FeColor(1.f, 0.4f, 0.4f, 0.f) : FeColor(1.f, 1.f, 0.87843f, 0.13725f);
		const FLinearColor TlMark = bBusy ? FeColor(0.5f, 0.4f, 0.f, 0.f) : FeColor(0.5f, 1.f, 0.f, 0.f);
		const FLinearColor CutRed = FeColor(0.8f, 1.f, 0.f, 0.f);   // 0x337FE8

		// 0x1EDEF0
		{
			const FLinearColor Yel = FeColor(0.7f, 1.f, 1.f, 0.f);
			const FLinearColor Red8 = bBusy ? FeColor(1.f, 0.4f, 0.f, 0.f) : FeColor(0.8f, 1.f, 0.f, 0.f);
			const float Len = float(C.Timeline.Length);
			const float X0 = 320.f * 0.8f - 204.8f;
			const float Y0 = Y * 0.93333334f + 26.04651f;
			const float Fp = float(Play);
			Fill(X0, Y0, 409.6f, 9.f, TlWhite, 0xD);
			if (Track && Track->Count > 1)
			{
				for (int32 I = 1; I < Track->Count; ++I)   // 0x1953A0
				{
					const int32 F = int32(Track->Cuts[I].Frame);
					const FLinearColor& Cl = (bPaused && F == Play) ? Yel : CutRed;
					const float X = X0 + (float(F) / Len) * 409.6f;
					Line(X, Y0 - 6.f, Y0, Cl, 0xD);
					Line(X + 1.f, Y0 - 6.f, Y0, Cl, 0xD);
				}
			}
			bool bOnClip = false;
			for (const int32 Cl : C.Timeline.Clips)
			{
				const float S = float(ClipStart(Cl));
				float Lf = float(R->GetClip(Cl).Length);
				if (Len < S + Lf) Lf = Len - S;
				Fill(X0 + (S / Len) * 409.6f, Y0, (Lf / Len) * 409.6f, 9.f, TlGold, 0xD);
				if (S == Fp) bOnClip = true;
			}
			if (C.bMarking)
			{
				Fill(X0 + (float(int32(C.MarkFrame)) / Len) * 409.6f, Y0, (float(Play - int32(C.MarkFrame)) / Len) * 409.6f, 9.f, TlMark, 0xD);
			}
			Cursor((X0 + (Fp / Len) * 409.6f) - 2.f, Y0 - 4.f, 5.f, 0.6f, bOnClip ? Yel : Red8, 0xE);
		}

		// 0x1ED490
		{
			const FLinearColor Red7 = bBusy ? FeColor(0.7f, 0.4f, 0.f, 0.f) : FeColor(0.7f, 1.f, 0.f, 0.f);
			const FLinearColor KeyGrey = FeColor(1.f, 0.2f, 0.2f, 0.2f);
			const int32 Start = C.ScrollWindow(Play);
			const int32 Wl = C.WinLen;
			const float Fw = float(Wl);
			const float Ht = H2a * 0.85f, Hh = Ht * 0.5f, Hm1 = Hh - 1.f;
			const float Yz = Y * 0.93333334f + 68.92308f;
			const float Xi = (320.f * 0.8f + 15.f) - 179.2f;
			Fill(320.f * 0.8f - 179.2f, Yz - Hh, 358.4f, Ht, TlWhite, 0xE);
			bool bOnClip = false;
			for (const int32 Cl : C.Timeline.Clips)
			{
				int32 S = ClipStart(Cl);
				const int32 E = S + R->GetClip(Cl).Length;
				int32 We = Start + Wl;
				if (Start < E && S < We)
				{
					if (S <= Start) S = Start;
					if (E < We) We = E;
					Fill(Xi + (float(S - Start) / Fw) * 328.4f, Yz - Ht * 0.5f, (float(We - S) / Fw) * 328.4f, Ht, TlGold, 0xE);
				}
				if (ClipStart(Cl) == Play) bOnClip = true;
			}
			if (C.bMarking)
			{
				const int32 M = int32(C.MarkFrame) <= Start ? Start : int32(C.MarkFrame);
				const int32 E = Play < Start + Wl ? Play : Start + Wl;
				Fill(Xi + (float(M - Start) / Fw) * 328.4f, Yz - Ht * 0.5f, (float(E - M) / Fw) * 328.4f, Ht, TlMark, 0xE);
			}
			if (Track)
			{
				for (int32 I = 0; I < Track->Count; ++I)
				{
					const int32 F = int32(Track->Cuts[I].Frame);
					if (Start + Wl < F) break;
					if (Start <= F)
					{
						const FLinearColor& Cl = (bPaused && F == Play) ? TlYellow : CutRed;
						const float X = FMath::TruncToFloat(Xi + (float(F - Start) / Fw) * 328.4f);
						Line(X, Yz - (Hm1 + 6.f), Yz - Hm1, Cl, 0xE);
						Line(X + 1.f, Yz - (Hm1 + 6.f), Yz - Hm1, Cl, 0xE);
						Box(X - 1.f, Yz - ((Hm1 + 6.f) + 4.f), 4.f, 4.f, Cl, 0xE);
					}
				}
			}
			for (int32 K = 0; K < C.Timeline.KeyCount && K < 16; ++K)
			{
				const int32 F = int32(C.Timeline.Keys[K]);
				if (Start + Wl < F) break;
				if (Start <= F)
				{
					const float X = Xi + (float(F - Start) / Fw) * 328.4f;
					Line(X, Yz - Hm1, Yz + Hm1, KeyGrey, 0xE);
					Line(X + 1.f, Yz - Hm1, Yz + Hm1, KeyGrey, 0xE);
				}
			}
			if (!(Play < Start) && Play <= Start + Wl)
			{
				Cursor(((Xi + ((float(Play) - float(Start)) / Fw) * 328.4f) - 2.f) - 1.f, (Yz - (Hm1 + 2.f)) - 4.f, 7.f, 1.2f, bOnClip ? TlYellow : Red7, 0xF);
			}
		}
	}

	// 0x1ECEA0
	{
		const FLinearColor CamGold = bBusy ? FeColor(0.5f, 0.4f, 0.4f, 0.f) : FeColor(1.f, 0.953f, 0.729f, 0.106f);
		const FLinearColor Red = bBusy ? FeColor(0.5f, 0.4f, 0.f, 0.f) : FeColor(1.f, 1.f, 0.f, 0.f);
		const bool bAtEnd = R && R->GetState() != 0 && R->GetState() < 10 && R->GetPlayFrame() == R->GetRecordedLength();
		DrawTab(D, 18.f, Backdrop[OvReplayControl], L(bAtEnd ? 0xEBA : 0xEB8), 6);
		if (bPanel)
		{
			const int32 Mode = Host ? Host->GetReplayCameraMode() : 0;
			int32 CamLoc = 0x14D;
			switch (Mode)
			{
			case 1: CamLoc = 0x152; break;
			case 2: CamLoc = 0x14A; break;
			case 3: CamLoc = 0x14B; break;
			case 5: CamLoc = 0x14C; break;
			case 6: CamLoc = 0x14E; break;
			case 7: CamLoc = 0x150; break;
			case 8: CamLoc = 0x151; break;
			case 10: CamLoc = -1; break;
			default: break;
			}
			const float Ty = Y - 47.f;
			Txt(D, EFont::Title, 20.f, Ty, 0.63f, CamGold, CamLoc < 0 ? FString(TEXT("Artist")) : L(CamLoc), 0x11);
			TxtC(D, EFont::Title, 320.f, Ty, 0.63f, CamGold, Host ? Host->GetReplayTargetName() : FString(), 0x11);
		}
		if (C.bMarking)
		{
			Txt(D, EFont::Menu, (Left + Width) - 71.f, Y - 28.f, 0.9f, Red, L(0xDEC), 0x11);
		}
		if (C.Fade > 0.f)
		{
			const FString M = L(C.MessageLoc);
			Txt(D, EFont::Menu, (Left + (Right - Left) * 0.5f) - Ink(D, EFont::Menu, M, 1.f) * 0.5f, Y - 2.f, 1.f, FeColor(C.Fade, 1.f, 1.f, 1.f), M, 0x11);
		}
		if (!C.bHelpChanged)
		{
			TxtR(D, EFont::Menu, Right - 20.f, Y - 49.f, 1.f, CamGold, L(C.HelpLevel == C.HelpMax ? 0x16A : 0x16B), 0x11);
		}
	}
	if (C.bEditor)
	{
		// 0x1EEE00 / 0x33F0B0 / 0x33F150
		if (Y < C.PanelA)
		{
			struct FIc { float X, Y; int32 Id; };
			static const FIc Icons[] = {
				{ 18.f, 155.f, 107 }, { -18.f, 155.f, 106 }, { 28.f, 185.f, 108 }, { -28.f, 185.f, 108 }, { -68.f, 155.f, 97 },
				{ 56.f, 155.f, 117 }, { 94.f, 155.f, 114 }, { 75.f, 136.f, 115 }, { 75.f, 175.f, 116 }, { -64.f, 42.f, 111 },
				{ -64.f, 72.f, 110 }, { 75.f, 42.f, 113 }, { 75.f, 72.f, 112 } };
			struct FLb { float X, Y; int32 Loc, Kind; float Len, Seg; int32 Icon; uint32 Flags; };
			static const FLb Labels[] = {
				{ -108.f, 64.f, 0x1A5, 2, 20.f, 0.f, -1, 0 }, { 118.f, 64.f, 0x1B0, 1, 21.f, 0.f, -1, 0 },
				{ -108.f, 36.f, 0x1B1, 2, 20.f, 0.f, -1, 0 }, { 118.f, 36.f, 0x1B2, 1, 21.f, 0.f, -1, 0 },
				{ -108.f, 92.f, 0x1B3, 2, 85.f, 48.f, -1, 0 }, { 16.f, 101.f, 0x1B4, 0, 26.f, 0.f, -1, 0 },
				{ -108.f, 120.f, 0x1B5, 2, 35.f, 7.f, -1, 0 }, { -108.f, 176.f, 0x1B6, 2, 38.f, -7.f, -1, 0 },
				{ -108.f, 148.f, 0x1B7, 2, 13.f, 0.f, -1, 0 }, { -108.f, 232.f, 0x1A6, 2, 77.f, -34.f, 47, 16 },
				{ 118.f, 187.f, 0x1A7, 1, 62.f, -29.f, -1, 0 }, { 118.f, 114.f, 0x1AA, 1, 42.f, 4.f, -1, 0 },
				{ 118.f, 139.f, 0x1A8, 1, 11.f, 0.f, -1, 0 }, { 118.f, 163.f, 0x1A9, 1, 29.f, 0.f, -1, 0 },
				{ 55.f, 211.f, 0x1AB, 1, 28.f, -16.f, -1, 0 }, { -108.f, 204.f, 0x1AD, 2, 20.f, 28.f, 47, 0 },
				{ 55.f, 235.f, 0x1AE, 1, 28.f, -22.f, 47, 16 }, { 55.f, 259.f, 0x1AF, 1, 28.f, -21.f, 47, 0 } };
			const FLinearColor Lw = bBusy ? FeColor(0.2f, 0.8f, 0.8f, 0.8f) : White;
			constexpr float Sc = 0.9f;
			const float Base = Y + 70.f;
			for (const FIc& I : Icons)
			{
				Icon(D, I.Id, I.X + 320.f, Base + I.Y, 1.f, 1.f, 0x60, Lw, 0xE);
			}
			for (const FLb& B : Labels)
			{
				const FString S = L(B.Loc);
				float W = Ink(D, EFont::Menu, S, Sc);
				W = B.Kind == 0 ? W * 0.5f : (B.Kind == 1 ? 0.f : W);
				Txt(D, EFont::Menu, (B.X + 320.f) - W, Base + B.Y, Sc, Lw, S, 0xD);
			}
			const float Fh = LineH(D, EFont::Menu);
			for (const FLb& B : Labels)
			{
				if (!(0.f < B.Len)) continue;
				const float Bx = B.X + 320.f;
				float End = 0.f, Ly = 0.f;
				const auto SmallIcon = [&](bool bRightOfText)
				{
					if (B.Icon == -1) return;
					const float Tw = Ink(D, EFont::Menu, L(B.Loc), Sc);
					const float Iw = HudW(D, B.Icon, 1.f), Ih = HudH(D, B.Icon, 1.f);
					float Ix, Iy;
					if (bRightOfText)
					{
						Ix = Bx + Tw + 7.f;
						Iy = B.Flags == 0 ? Base + B.Y + 2.f : (Base + B.Y + Fh * 0.5f) - Ih * 0.5f;
					}
					else
					{
						Ix = Bx - (Tw + (B.Flags == 0 ? Iw : Ih) * 0.4f + 10.f);
						Iy = B.Flags == 0 ? Base + B.Y + 4.f : (Base + B.Y + Fh * 0.5f) - Ih * 0.5f;
					}
					IconFlags(D, B.Icon, Ix, Iy, 0.65f, 0.65f, B.Flags, Lw, 0xD);
				};
				if (B.Kind == 0)
				{
					End = Bx + B.Len;
					Ly = Base + B.Y + Fh * Sc + 5.f;
					IconFlags(D, -5, Bx, Ly, 3.f, B.Len, 0, Lw, 0xD);
				}
				else if (B.Kind == 1)
				{
					const float X12 = Bx - 2.f;
					End = X12 - B.Len;
					Ly = Base + B.Y + Fh * Sc * 0.4f;
					IconFlags(D, -5, X12, Ly, B.Len, 3.f, 8, Lw, 0xD);
					SmallIcon(true);
				}
				else
				{
					const float X10 = Bx + 4.f;
					End = X10 + B.Len;
					Ly = Base + B.Y + Fh * Sc * 0.4f;
					IconFlags(D, -5, X10, Ly, B.Len, 3.f, 0, Lw, 0xD);
					SmallIcon(false);
				}
				if (B.Seg != 0.f)
				{
					const float A = FMath::Abs(B.Seg);
					if (B.Seg < 0.f)
					{
						if (B.Kind == 2) IconFlags(D, -5, End - 3.f, Ly + B.Seg, A, 3.f, 0x11, Lw, 0xD);
						else IconFlags(D, -5, End, Ly + B.Seg, A, 3.f, 0x10, Lw, 0xD);
					}
					else
					{
						IconFlags(D, -5, End, Ly, A, 3.f, 0x10, Lw, 0xD);
					}
				}
			}
		}
		return;
	}
	// 0x1EE720
	{
		const FLinearColor Dim = bBusy ? FeColor(0.2f, 0.8f, 0.8f, 0.8f) : FeColor(0.35f, 1.f, 1.f, 1.f);
		const FLinearColor W = bBusy ? FeColor(0.2f, 0.8f, 0.8f, 0.8f) : White;
		// TODO: app+0x58 == 4 (190 / 450 / 150 / 375 / 570 and the half font height - 1) is not identified
		const float X1 = 340.f, X2 = 568.f, Xa = 154.f, Xb = 381.f, Xc = 593.f;
		const float Fh = LineH(D, EFont::Menu) * 0.5f;
		const float Y5 = Y + 35.f, Y4 = Y + 83.f;
		Icon(D, 0x74, Xa, Y5, 1.f, 1.f, 0x60, W, 14);
		Icon(D, 0x6B, Xa, Y4, 1.f, 1.f, 0x60, W, 14);
		Icon(D, 0x6A, Xb, Y5 - 1.f, 1.f, 1.f, 0x60, W, 14);
		Icon(D, 0x62, Xb - 10.f, Y4, 1.f, 1.f, 0x60, W, 14);
		Icon(D, 0x63, Xb + 10.f, Y4, 1.f, 1.f, 0x60, W, 14);
		Icon(D, 0x71, Xc, Y5 + 1.f, 1.f, 1.f, 0x60, W, 14);
		Icon(D, 0x6F, Xc, Y4 + 1.f, 1.f, 1.f, 0x60, W, 14);
		Icon(D, 0x61, Xb, Y4, 1.f, 1.f, 0x60, Dim, 14);
		const float Ta = Y5 - Fh, Tb = Y4 - Fh;
		TxtR(D, EFont::Menu, 125.f, Ta, 0.85f, W, L(0x1A9), 13);
		TxtR(D, EFont::Menu, 125.f, Tb, 0.85f, W, L(0x1B4), 13);
		TxtR(D, EFont::Menu, X1, Ta, 0.85f, W, L(0x1B3), 13);
		TxtR(D, EFont::Menu, X1, Tb, 0.85f, W, L(0x1B7), 13);
		TxtR(D, EFont::Menu, X2, Ta, 0.85f, W, L(0x1B2), 13);
		TxtR(D, EFont::Menu, X2, Tb, 0.85f, W, L(0x1B1), 13);
		Txt(D, EFont::Menu, Xa - D.TextInkWidth(EFont::Menu, TEXT("START"), 0.7f) * 0.5f, Tb + 20.f, 0.7f, W, TEXT("START"), 13);
		Txt(D, EFont::Menu, Xb - D.TextInkWidth(EFont::Menu, TEXT("SELECT"), 0.8f) * 0.5f, Ta + 20.f, 0.7f, W, TEXT("SELECT"), 13);
	}
}

void FSSXOverlayManager::DrawSaveBox(FSSXFeDrawList& D, FSSXBackdrop& B) const
{
	// 0x1B8718 / 0x1B8BB8
	DrawSaveProgress(D, B, FMath::Clamp(1.f - SaveFrames / 120.f, 0.f, 1.f), L(451).Replace(TEXT("%s"), *L(459)));
	FOvBox Bx; Bx.X = 128.f; Bx.Y = 312.f; Bx.W = 384.f; Bx.Align = 1; Bx.Color = FeColor(SaveWarnAlpha, 1.f, 1.f, 1.f);
	TextBox(D, Bx, L(350), 0x13);
}

void FSSXOverlayManager::DrawSaveProgress(FSSXFeDrawList& D, FSSXBackdrop& B, float P, const FString& S) const
{
	// 0x1B8718
	B.Box(D, 290.f, 0xE);
	const float H = HudH(D, 0x2A, 31.f) * 0.8f;
	const float Y = 240.f - H * 0.5f;
	const float Sh = (H - 0.5f) / HudH(D, 0x2A, 31.f);
	Icon(D, 0x2A, 104.f, Y, 412.f / HudW(D, 0x2A, 1.f), Sh, 0, White, 0xF);
	float Lx = 131.46667f;
	for (int32 K = 0; K < 14; ++K)
	{
		D.Rect(Lx, Y + 0.5f, 1.f, (Y + H - 1.f) - (Y + 0.5f), Black, 0x10);
		Lx += 27.466667f;
	}
	if (P > 0.f) Icon(D, 0x2A, 103.f, Y, (P * 384.53333f + 27.466667f) / HudW(D, 0x2A, 1.f), Sh, 0, FeColor(0.8f, 0.f, 0.9f, 0.f), 0x11);
	float W = Ink(D, EFont::Menu, S, 1.f);
	if (W < 200.f) W = 200.f; else if (W > 384.f) W = W > 512.f ? 384.f : 320.f;
	FOvBox Bx; Bx.X = 320.f - (W + 10.f) * 0.5f; Bx.Y = 144.f; Bx.W = W + 10.f; Bx.Align = 1;
	TextBox(D, Bx, S, 0x12);
}

void FSSXOverlayManager::DrawCardRace(FSSXFeDrawList& D, FSSXBackdrop& B, const FSSXMemCard& Card) const
{
	const FSSXMemCard::FRaceView V = Card.RaceView();
	const float Fh = LineH(D, EFont::Menu);
	auto Warning = [&]()
	{
		// 0x1B8BB8
		FOvBox Bx; Bx.X = 128.f; Bx.Y = 312.f; Bx.W = 384.f; Bx.Align = 1; Bx.Color = FeColor(Card.WarnAlpha, 1.f, 1.f, 1.f);
		TextBox(D, Bx, L(350), 0x13);
	};
	switch (V.Kind)
	{
	case FSSXMemCard::FRaceView::Status:
	{
		// 0x1B9968
		B.Box(D, 300.f, 0x10);
		const FString S = Card.MessageText(*Loc);
		float W = S.IsEmpty() ? 0.f : Ink(D, EFont::Menu, S, 1.f);
		if (W < 200.f) W = 200.f; else if (W > 384.f) W = W > 512.f ? 384.f : 320.f;
		FOvBox Bx; Bx.X = 320.f - (W + 10.f) * 0.5f; Bx.Y = FMath::TruncToFloat(240.f - Fh * 0.5f); Bx.W = W + 10.f;
		Bx.Align = 1; Bx.PitchAdd = 4.f;
		TextBox(D, Bx, S, 0x12);
		if (V.bWarning) Warning();
		break;
	}
	case FSSXMemCard::FRaceView::Dialog:
	{
		// 0x1B9530
		B.Box(D, 300.f, 0x10);
		const FString S = V.TextLoc >= 0 ? L(V.TextLoc) : Card.MessageText(*Loc);
		FOvBox Bx; Bx.X = 0.f; Bx.Y = 204.f; Bx.W = 640.f; Bx.Align = 1; Bx.PitchAdd = 4.f;
		TextBox(D, Bx, S, 0x13);
		const int32 Icon1 = V.bXFirst ? 0x74 : 0x73, Icon2 = V.bXFirst ? 0x73 : 0x74;
		if (V.Button1 >= 0 && V.Button2 >= 0)
		{
			const FString L1 = L(V.Button1), L2 = L(V.Button2);
			const float C = ((320.f - ((24.f + 403.2f + Ink(D, EFont::Menu, L2, 1.f)) - 147.2f) * 0.5f) - 147.2f) + 320.f;
			const float X1 = C - 172.8f, X2 = C + 83.2f;
			Icon(D, Icon1, X1, 295.f, 1.f, 1.f, 0, White, 0x13);
			Txt(D, EFont::Menu, X1 + 24.f, 296.f, 1.f, White, L1, 0x13);
			Icon(D, Icon2, X2, 295.f, 1.f, 1.f, 0, White, 0x13);
			Txt(D, EFont::Menu, X2 + 24.f, 296.f, 1.f, White, L2, 0x13);
		}
		else if (V.Button1 >= 0 || V.Button2 >= 0)
		{
			const FString L1 = L(V.Button1 >= 0 ? V.Button1 : V.Button2);
			const float X = 320.f - (Ink(D, EFont::Menu, L1, 1.f) + 24.f) * 0.5f;
			Icon(D, Icon1, X, 295.f, 1.f, 1.f, 0, White, 0x13);
			Txt(D, EFont::Menu, X + 24.f, 296.f, 1.f, White, L1, 0x13);
		}
		break;
	}
	case FSSXMemCard::FRaceView::Saving:
		DrawSaveProgress(D, B, Card.SaveProgress(), Card.MessageText(*Loc));
		Warning();
		break;
	case FSSXMemCard::FRaceView::List:
	{
		// 0x1B8D08 / FUN_001A9928 / 0x1B91B0 / 0x1B91C4
		const bool bList = Card.State == FSSXMemCard::SaveList || Card.State == FSSXMemCard::LoadList;
		const bool bDim = Card.State != FSSXMemCard::SaveList;
		const FLinearColor C0 = bDim ? FeColor(1.f, 0.4f, 0.4f, 0.4f) : White;
		const FLinearColor C1 = FeColor(0.5f, 1.f, 1.f, 1.f);
		const FLinearColor Bar = FeColor(bDim ? 0.4f : 0.7f, 0.15625f, 0.29296875f, 0.78125f);
		B.Box(D, 420.f, 5);
		DrawTab(D, 25.f, B, L(Card.RaceTitleLoc()), 0xE);
		const FString PortS = L(3761);
		Txt(D, EFont::Menu, 320.f - Ink(D, EFont::Menu, PortS, 1.f) * 0.5f, 86.4f, 1.f, C0, PortS, 0xD);
		const FString Det = L(312).Replace(TEXT("%d"), *FString::FromInt(Card.FreeKb()));
		const float Dw = Ink(D, EFont::Menu, Det, 1.f);
		FOvBox Bx; Bx.X = 320.f - Dw * 0.5f; Bx.Y = Fh + 86.4f + 7.f; Bx.W = Dw; Bx.Align = 1;
		TextBox(D, Bx, Det, 0xD);
		float Y = 168.f;
		for (int32 I = 0; I < Card.MaxFiles(); ++I)
		{
			const FString Name = Card.FileTitle(*Loc, I);
			if (I == Card.Selected && !Name.IsEmpty())
			{
				D.Rect(192.f, Y - 10.56f, 371.2f, 36.f, Bar, 0xA);
			}
			Txt(D, EFont::Menu, 204.8f, Y, 1.f, C0, FString::FromInt(I + 1), 0xE);
			Txt(D, EFont::Menu, 236.8f, Y, 1.f, C0, Name, 0xE);
			if (Card.Mode == FSSXMemCard::EMode::Game) break;
			Y += 36.f;
		}
		// 0x1CF040
		const bool bSave = bList, bCancel = bList, bDelete = bList && Card.FileExists(Card.Selected);
		const float Step = Fh + 10.f;
		const float Ys = Step + Step + 288.f, Yc = Step * 3.f + 288.f, Yd = Step * 4.f + 288.f;
		Txt(D, EFont::Menu, 448.f, Ys, 1.f, bSave ? C0 : C1, L(443), 0xE);
		Txt(D, EFont::Menu, 448.f, Yc, 1.f, bCancel ? C0 : C1, L(340), 0xE);
		Txt(D, EFont::Menu, 448.f, Yd, 1.f, bDelete ? C0 : C1, L(343), 0xE);
		Icon(D, 0x74, 420.f, Ys - 2.f, 1.f, 1.f, 0, bSave ? C0 : C1, 0xE);
		Icon(D, 0x73, 420.f, Yc - 2.f, 1.f, 1.f, 0, C0, 0xE);
		Icon(D, 0x72, 420.f, Yd - 2.f, 1.f, 1.f, 0, bDelete ? C0 : C1, 0xE);
		if (bDim)
		{
			D.Rect(0.f, 0.f, 640.f, 480.f, FeColor(0.8f, 0.f, 0.f, 0.f), 0xE);
		}
		break;
	}
	default:
		break;
	}
}

void FSSXOverlayManager::Draw(FSSXFeDrawList& D) const
{
	if (!Loc) return;
	// Render 0x1A78C0: 640x480 ortho, pass 1 then pass 2, in slot order. Slot 0 (HUD) is ASSXHUD.
	if (Active[OvBlackFade] && Host)
	{
		// TODO: the end-race sub-state 1..9 test FUN_00193CE0.
		const float Fade = Host->GetRiderFade(0);
		if (Fade > 0.f)
		{
			D.Rect(0.f, 0.f, ScreenW, ScreenH, FeColor(Fade, 1.f, 1.f, 1.f), 0xB);
		}
	}
	DrawCountdown(D);
	if (Active[OvPopFinish]) DrawPopFinish(D);
	if (Active[OvPopAsyncLoad])
	{
		// 0x1AE660 / FUN_00250D60
		const FString S = L(373);
		Txt(D, EFont::Menu, 600.f - Ink(D, EFont::Menu, S, 1.f), 440.f, 1.f, White, S, 0x13);
	}
	DrawLesson(D);
	if (Active[OvHelpRace]) DrawHelpRace(D);
	if (Active[OvNextRace]) DrawNextRace(D);
	if (Active[OvRaceResults]) DrawResults(D);
	if (Active[OvViewHiScores]) DrawViewHiScores(D);
	if (Active[OvHiScoreRecord]) DrawHiScoreRecord(D);
	if (Active[OvUnlocked]) DrawUnlocked(D);
	if (Active[OvPointDistribution] || RankCardFrames > 0) DrawPoints(D);
	if (Active[OvSelectNextVenue]) DrawNextVenue(D);
	if (Active[OvSelectNextEvent]) DrawNextEvent(D);
	if (Active[OvPauseMenu]) DrawPause(D);
	if (Active[OvReplayControl]) DrawReplayControl(D);   // 0x1EC808
	if (IsReplayMenuOpen()) DrawReplayMenu(D);
	if (Active[OvReplay] && ReplayState == 0 && Loc)
	{
		// 0x1EFE38 / 0x1CF628 / 0x1CF6E8
		DrawCardRace(D, Backdrop[OvReplay], ReplayCard);
	}
	if (Active[OvReplay] && ReplayState == 4 && Loc)
	{
		// 0x1A9BB8 / 0x1AA268
		FSSXBackdrop Bd = Backdrop[OvReplay];
		Dialog.Draw(D, *Loc, Bd);
	}
	if (SaveFrames > 0 && !Active[OvPauseMenu] && !Active[OvRaceResults] && !Active[OvSelectNextEvent] && !Active[OvSelectNextVenue]
		&& !Active[OvPointDistribution])
	{
		FSSXBackdrop Bd = Backdrop[OvPauseMenu];
		Bd.bAnimated = false;
		Bd.Alpha = Bd.AlphaCap;
		DrawSaveBox(D, Bd);
	}
}
