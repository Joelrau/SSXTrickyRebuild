#include "UI/SSXHudOverlay.h"

#include "Input/PadMapper.h"
#include "Progression/SSXProgression.h"
#include "Race/RaceTrackData.h"
#include "Sim/Air/TrickName.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFrontEndData.h"

using namespace SSXFe;

void FSSXHudInput::ApplySettings(const FSSXSettings& S)
{
	bMph = S.Units != 0;
	HudDetail = S.HudDetail;
}

namespace
{
	constexpr float SegStep = 1.f / 14.f;
	constexpr float TrickToMeter = 0.6786545f;        // FUN_00155410
	constexpr float BoostSlew = 0.005f;
	constexpr float PopStep = 1.f / 120.f;
	constexpr float StripSlew = 1.f / 36.f;
	constexpr float StripRange = 20000.f;             // cm -> +-150 px
	constexpr float StripPx = 150.f;
	constexpr float SpeedPopAt = 3319.4443f;          // cm/s
	constexpr float KmhPerCms = 0.036f, MphPerCms = 0.022369362f;
	constexpr double MarkerMinGap = 500.0;            // FUN_001a1e50

	const FLinearColor HudGold = FeColor(1.f, 0.953f, 0.729f, 0.106f);
	const FLinearColor LapColor = FeColor(1.f, 1.f, 0.878f, 0.137f);
	const FLinearColor TrickColor = FeColor(1.f, 1.f, 0.867f, 0.f);
	const FLinearColor ClockRed = FeColor(1.f, 0.902f, 0.094f, 0.094f);
	const FLinearColor Cyan = FeColor(1.f, 0.f, 1.f, 1.f);
	const FLinearColor PopGreen = FeColor(1.f, 0.f, 1.f, 0.f);
	const FLinearColor BeatGreen = FeColor(1.f, 0.f, 0.85f, 0.f);
	const FLinearColor BeatRed = FeColor(1.f, 0.9f, 0.1f, 0.1f);
	const FLinearColor DeltaRed = FeColor(1.f, 1.f, 0.f, 0.f);
	const FLinearColor ReplayFull = FeColor(1.f, 1.f, 0.776f, 0.f);

	FLinearColor WithA(FLinearColor C, float A) { C.A *= A; return C; }
	constexpr float OrbRise = 0.7f, OrbSink = 19.3f, OrbBounce = 40.f, OrbSpin = 0.15f;
	constexpr float PlateCapW = 25.f;
	constexpr float PlatePad = 14.f, PlateRaise = 7.f, PlateCapK = 0.37f;

	float Approach(float V, float T, float Step) { return T + Step < V ? V - Step : (V < T - Step ? V + Step : T); }
}

int32 FSSXHudOverlay::Round10(float X)
{
	if (X < 0.f) return 0;
	const int32 I = (int32)(X + 5.f);
	return I - I % 10;
}

float FSSXHudOverlay::OrbFold(float Ph)
{
	if (Ph <= -3.f) return 1.f - FMath::Abs(Ph + 3.f);
	if (Ph <= -2.f) return FMath::Abs(Ph + 2.f);
	if (Ph < -1.f) return -(Ph + 2.f);
	return Ph;
}

float FSSXHudOverlay::TierPop(float E, float Sx)
{
	if (E > 36.f) return 0.f;
	return E < 18.f ? Sx * 0.8f * E / 18.f : Sx * 0.8f * (1.f - (E - 18.f) / 36.f);   // up to 0.8 sx over 18 frames, 0.4 sx at frame 36, then snaps to 0 (the original's quirk)
}

bool FSSXHudOverlay::MedalThresholds(int32 Mode, int32 Track, int32& G, int32& S, int32& B)
{
	// FUN_0019e888 / FUN_002ccf70 / FUN_002cc808 / 0x1A6FB8
	auto Set = [&](int32 Gv, int32 Sv, int32 Bv, bool bWritten) { G = Gv; S = Sv; B = Bv; return bWritten; };
	switch (Track)
	{
	case 0: return Set(55000, 40000, 25000, true);
	case 1: return Set(95000, 65000, 35000, true);
	case 2: case 3: return Set(225000, 150000, 75000, true);
	case 4: return Set(275000, 175000, 125000, true);
	case 5: return Set(175000, 115000, 75000, true);
	case 6: if (Mode == 3) return Set(800000, 500000, 250000, true); break;
	case 8: return Set(350000, 225000, 100000, true);
	case 11: return Set(500000, 300000, 150000, true);
	default: break;
	}
	return Set(UnwrittenThreshold, UnwrittenThreshold, UnwrittenThreshold, false);
}

void FSSXHudOverlay::BuildMasks(int32 Mode, int32 Track, int32 Racers, int32 ShowoffSum, uint32& Min, uint32& Full, uint32& None)
{
	// 0x1A1738 / 0x1A1920
	Min = 0xB000; Full = 0x4002B9C0; None = 0;
	if (Track == 8) { Min = 0xB400; Full = 0x4002BDC0; }
	if (Racers >= 2) { Min |= 0x200; Full |= 0x300; }
	if (Mode == 6) { Min &= ~0x40000000u; Full &= ~0x40000000u; }
	switch (Mode)
	{
	case 1: case 6: Min &= ~0x300u; Full &= ~0x300u; None &= ~0x300u; Min |= 0x10000000; Full |= 0x10000000; break;
	case 2: case 4: case 9: Min |= 0x1C410009; Full |= 0x1C410009; break;
	case 3: case 5: Min |= 0x1CD10009; Full |= ShowoffSum == 2 ? 0x1CD10009u : 0x1DD10009u; break;
	case 7: Min |= 0x30000000; Full |= 0x30000000; Min &= ~0x100u; Full &= ~0x100u; break;
	case 8: Min = Full = None = 0x80000; break;
	default: break;
	}
	Min |= 0x2000000; Full |= 0x2000000; None |= 0x2000000;
}

int32 FSSXHudOverlay::LayoutFor(int32 Player, int32 NumPlayers)
{
	// TODO: with 2 racers and a vertical split (DAT_0032ca18) the value is left unchanged.
	return NumPlayers < 2 ? 0 : (Player == 0 ? 1 : 2);
}

float FSSXHudOverlay::Pop(float P, float K)
{
	const float H = (1.f - P) * 0.5f;
	return (P <= 0.25f && H >= 0.05f) ? FMath::Sin(4.f * PI * P) * H * K : 0.f;
}

FString FSSXHudOverlay::FormatTime(int32 Cs)
{
	Cs = FMath::Clamp(Cs, 0, 359999);
	return FString::Printf(TEXT("%d:%02d.%02d"), Cs / 6000, (Cs / 100) % 60, Cs % 100);
}

FString FSSXHudOverlay::TimeText(int32 Cs) const
{
	if (Cs == 50000) return L(0x15D);
	if (Cs < 0) return L(0x18C);
	return FormatTime(Cs);
}

FString FSSXHudOverlay::FormatGap(int32 Cs)
{
	if (Cs > 359999) return TEXT("59:59.99");   // 0x38E280
	if (Cs < 0)
	{
		const int32 Sec = -Cs / 100;
		return FString::Printf(TEXT("-%d:%02d.%02d"), Sec / 60, Sec % 60, -Cs % 100);   // 0x38E2A0
	}
	return FString::Printf(TEXT("%d:%02d.%02d"), Cs / 100 / 60, (Cs / 100) % 60, Cs % 100);   // 0x38E290
}

void FSSXHudOverlay::BuildMarkers(const FRacePathSet& Paths, int32 Track, double LapLength, TArray<FSSXHudMarker>& Out)
{
	// TODO: the trigger list DAT_00337FC4 order is not recovered;
	Out.Reset();
	Out.Add({ 1, 0.0 });
	auto Add = [&Out](int32 Kind, double Dist)
	{
		if (Out.Num() >= MaxMarkers) return;
		for (const FSSXHudMarker& M : Out)
		{
			if (M.Kind == Kind && FMath::Abs(M.Dist - Dist) < MarkerMinGap) return;
		}
		Out.Add({ Kind, Dist });
	};
	const int32 Laps = Track == 8 ? 3 : 1;
	for (const FRacePath& L : Paths.LinePaths)
	{
		for (const FRacePathEvent& E : L.Events)
		{
			if (E.Type != SSXRaceEvent::Checkpoint && E.Type != SSXRaceEvent::Lap) continue;
			const double D = L.DistToGoAtStart - E.Start;
			for (int32 K = 0; K < Laps; ++K)
			{
				Add(E.Type == SSXRaceEvent::Checkpoint ? 0 : 2, D + K * LapLength);
			}
		}
	}
}

FString FSSXHudOverlay::L(int32 Id) const { return Loc ? Loc->ByIndex(Id) : FString(); }

void FSSXHudOverlay::Activate(int32 Mode, int32 Track, int32 Racers, int32 ShowoffSum, const FRacePathSet* Paths, double LapLength)
{
	BuildMasks(Mode, Track, Racers, ShowoffSum, MaskMinimal, MaskFull, MaskNone);
	Markers.Reset();
	if (Paths) BuildMarkers(*Paths, Track, LapLength, Markers);
	for (FPlayerState& S : P) S = FPlayerState();
	MedalThresholds(Mode, Track, MedalGold, MedalSilver, MedalBronze);
	Frame = 0;
	Ticks = 0;
	ShowoffBeeps = 0;
	ClockWhole = 0;
}

void FSSXHudOverlay::Update(const FSSXHudInput& In)
{
	// 0x1A1F18 / FUN_001A77F8
	if (In.bModal)
	{
		return;
	}
	++Frame;
	const uint32 Mask = MaskFor(In.HudDetail);
	// 0x1A1FB4 / 0x1A20DC / 0x38DA10 / FUN_00216930
	{
		const int32 Now = (int32)In.Clock;
		if ((In.Mode == 3 || In.Mode == 5) && Now >= 1 && Now <= 10 && Now == ClockWhole - 1)
		{
			for (int32 I = 0; I < In.Players.Num() && I < MaxPlayers; ++I)
			{
				if (In.Players[I].FinishResult < 0) { ++ShowoffBeeps; break; }
			}
		}
		ClockWhole = Now;
	}
	for (int32 I = 0; I < In.Players.Num() && I < MaxPlayers; ++I)
	{
		const FSSXHudPlayerInput& Pl = In.Players[I];
		FPlayerState& S = P[I];
		const bool bRacing = In.Phase == 4;

		const float Base = 10000.f * TrickToMeter * (float)Pl.TrickValue;
		const int32 Tgt = Round10(Base * (float)Pl.PointsMultiplier);
		if (Tgt < 1 || Tgt < (int32)S.PointsShown)
		{
			S.PointsShown = 0.f;
			S.PointsPending = 0.f;
		}
		else
		{
			const int32 Old = (int32)S.PointsShown;
			const int32 New = Old > Tgt + 50 ? Old - 50 : (Old < Tgt - 50 ? Old + 50 : Tgt);
			S.PointsShown = (float)New;
			S.PointsPending = (float)Round10(Base);
			if ((Mask & SSXHudBit::Points) && Old % 100 > New % 100) ++Ticks;
		}

		S.BoostShown = Approach(S.BoostShown, (float)Pl.BoostMeter, BoostSlew);
		S.PendingShown = Approach(S.PendingShown, FMath::Max(0.f, TrickToMeter * (float)Pl.TrickValue), BoostSlew);
		float Rm = S.BoostShown;
		for (int32 K = 0; K < 14; ++K)
		{
			S.SegLit[K] = Rm > 0.f;
			const float T = S.SegLit[K] ? Rm : S.PendingShown + Rm;
			const FSSXHudBoostSeg Seg = Data && Data->Boost.IsValidIndex(K) ? Data->Boost[K] : FSSXHudBoostSeg();
			const float U = FMath::Clamp(T / SegStep, 0.f, 1.f);
			S.SegScale[K] = U * Seg.Hi + (1.f - U) * Seg.Lo;
			Rm -= SegStep;
		}
		if (In.LessonState == 5 && Pl.TrickyTimer >= 20.0)
		{
			for (int32 K = 0; K < 14; ++K)
			{
				const FSSXHudBoostSeg Seg = Data && Data->Boost.IsValidIndex(K) ? Data->Boost[K] : FSSXHudBoostSeg();
				S.SegLit[K] = true;
				S.SegScale[K] = Seg.Hi;
			}
		}
		S.Pulse = 1.f;
		S.ChainProgress = 0.f;
		for (const FSSXHudMsg& M : Pl.Messages)
		{
			if (M.IsLive() && (M.Type == SSXHudMsg::Points2 || M.Type == SSXHudMsg::Points3))
			{
				S.Pulse = 1.f + Pop(M.Progress(), 1.4f);
				S.ChainProgress = M.Progress();
			}
		}

		if (S.PointsPending < 1.f)
		{
			S.PointsTier = -1;
		}
		else
		{
			const FSSXHudRec& R30 = R(LayoutFor(I, In.Players.Num()), 30);
			const int32 Tier = SSXTrick::FTrickTables::Get().PointsTier(S.PointsPending);
			if (Tier == S.PointsTier)
			{
				S.PointsPop = TierPop((float)Frame - S.PopStart, R30.Sx);
			}
			else
			{
				S.PopStart = S.PointsPop == 0.f ? (float)Frame : (float)Frame - S.PointsPop / 0.8f * 18.f;
				S.PointsTier = Tier;
			}
		}

		if (Mask & SSXHudBit::Orb)
		{
			if (Pl.TrickyTimer > 0.0)
			{
				const float T = (float)Pl.TrickyTimer;
				const float F = (20.f - T) < OrbRise ? (20.f - T) / OrbRise : T / OrbSink;
				const float Old = S.Phase;
				S.Bounce = OrbBounce * F;
				if (bRacing) S.Phase = Old - (1.f - (F * OrbBounce) / OrbBounce) * OrbSpin;
				S.OrbW = OrbFold(Old);
				if (Old <= -3.f) S.Phase = S.OrbW;
			}
			else
			{
				S.Phase = 1.f;
				S.Bounce = 0.f;
				S.OrbW = 1.f;
			}
		}
		S.bSwitch = Pl.bSwitch;

		// 4 speed pop: starts when the speed first reaches 3319.4443 cm/s, +1/120 per frame
		if (!S.bSpeedPopDone && Pl.Speed >= SpeedPopAt) { S.bSpeedPopDone = true; S.SpeedPop = 0.f; }
		else S.SpeedPop = FMath::Min(1.f, S.SpeedPop + PopStep);

		if (In.Mode == 3 || In.Mode == 5)
		{
			if (S.MedalPop >= 1.f)
			{
				const int32 Next = S.MedalTier == 0 && Pl.Score >= (double)MedalBronze ? 1
					: (S.MedalTier == 1 && Pl.Score >= (double)MedalSilver ? 2 : (S.MedalTier == 2 && Pl.Score >= (double)MedalGold ? 3 : S.MedalTier));
				if (Next != S.MedalTier) { S.MedalTier = Next; S.MedalPop = 0.f; }
			}
			else
			{
				S.MedalPop = FMath::Min(1.f, S.MedalPop + PopStep);
			}
		}

		const double Self = In.RacerDist.IsValidIndex(Pl.Racer) ? In.RacerDist[Pl.Racer] : 0.0;
		for (int32 K = 0; K < 6 && K < In.RacerDist.Num(); ++K)
		{
			S.RivalD[K] += ((float)(In.RacerDist[K] - Self) - S.RivalD[K]) * StripSlew;
		}
		for (int32 K = 0; K < Markers.Num(); ++K)
		{
			S.MarkerD[K] += ((float)(Markers[K].Dist - Self) - S.MarkerD[K]) * StripSlew;
		}
	}
}

const FSSXHudOverlay::FUberIcon* FSSXHudOverlay::UberIcons()
{
	namespace PB = SSXPadButton;
	// 0x154E20 / 0x154D60 / 0x31E090 / 0x31E0F0
	static const FUberIcon T[12] = {
		{ '@', PB::L1, 4, 1, 32, 110 }, { '!', PB::R1, 4, 1, 32, 112 }, { '$', PB::L2, 4, -8, 32, 111 }, { '#', PB::R2, 4, -8, 32, 113 },
		{ '%', PB::Square, 3, 0, 26, 117 }, { '^', PB::Triangle, 3, 0, 26, 115 }, { '&', PB::Circle, 3, 0, 26, 114 }, { '*', PB::Cross, 3, 0, 26, 116 },
		{ 'a', PB::Left, 5, 5, 22, 98 }, { 'd', PB::Right, 5, 5, 22, 99 }, { 'w', PB::Up, 5, 4, 18, 100 }, { 'x', PB::Down, 5, 4, 18, 101 } };
	return T;
}

const FSSXHudOverlay::FUberIcon* FSSXHudOverlay::FindUberIcon(TCHAR Ch)
{
	for (int32 I = 0; I < 12; ++I)
	{
		if (UberIcons()[I].Ch == Ch) return &UberIcons()[I];
	}
	return nullptr;
}

bool FSSXHudOverlay::UberHintActive(const FSSXHudInput& In, const FSSXHudPlayerInput& Pl)
{
	return !In.bLessonObject && Pl.TrickyTimer > 0.0 && Pl.bBeginner && Pl.FinishResult < 0;
}

float FSSXHudOverlay::UberHintAlpha(const FSSXHudPlayerInput& Pl)
{
	return Pl.UberCount > 5 ? 1.f : FMath::Min((float)Pl.TrickyTimer / 5.f, 1.f);
}

FString FSSXHudOverlay::UberComboString(const SSXFeTrickBook::FEntry& E, TFunctionRef<uint32(int32)> ActionMask, bool bFlipSpin)
{
	auto Buttons = [](uint32 Mask, FString& Out)
	{
		for (int32 I = 0; I < 12; ++I)
		{
			if (Mask & UberIcons()[I].Mask) Out.AppendChar(UberIcons()[I].Ch);
		}
	};
	auto Tweak = [&](FString& Out)
	{
		Out += TEXT("+");
		const uint32 Mask = ActionMask(FPadMapper::ActTweak);
		int32 N = 0;
		for (int32 I = 0; I < 12; ++I)
		{
			if (!(Mask & UberIcons()[I].Mask)) continue;
			if (++N == 2) Out.InsertAt(Out.Len() - 1, TEXT("("));
			if (N != 1) Out += TEXT("OR");
			Out.AppendChar(UberIcons()[I].Ch);
		}
		if (N >= 2) Out += TEXT(")");
	};

	FString S;
	int32 Spin = bFlipSpin ? -E.Spin : E.Spin;
	if (Spin != 0)
	{
		S.AppendChar(Spin < 0 ? 'a' : 'd');
		S += FString::FromInt(FMath::Abs(Spin) * 180);
	}
	if (E.Flip != 0)
	{
		if (E.Spin != 0) S += TEXT("+");
		S.AppendChar(E.Flip < 0 ? 'x' : 'w');
		S += FString::FromInt(FMath::Abs((int32)E.Flip) * 360);
	}
	if (E.Rail != 0)
	{
		S += TEXT(" RAIL");
		if (E.W1 & 0xF0u) S += FString::Printf(TEXT(" %d"), int32((E.W1 >> 4) & 0xF) * 180);
	}
	S += TEXT(" ");
	if (E.ActA != 0x1A)
	{
		const uint32 M = ActionMask(E.ActA);
		if (M != 0xFFFFFFFFu) Buttons(M, S);
	}
	if (E.Tweak1 != 0) Tweak(S);
	if (E.ActB != 0x1A)
	{
		S += TEXT(" TO ");
		const uint32 M = ActionMask(E.ActB);
		if (M != 0xFFFFFFFFu) Buttons(M, S);
	}
	if (E.Tweak2 != 0) Tweak(S);
	return S;
}

void FSSXHudOverlay::Spr(FSSXFeDrawList& D, int32 Id, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C) const
{
	const FSSXSpriteTable* T = SSXFeDrawHud::GetSheet();
	const FSSXSprite* S = (D.Assets && D.Assets->HudSprites) ? D.Assets->HudSprites->Find(Id) : (T ? T->Find(Id) : nullptr);
	if (!S) return;
	const float W = S->W * Sx, H = S->H * Sy;
	// FUN_001FC178
	if (Flags & 4) Y -= H;
	if (Flags & 8) X -= W;
	if (Flags & 0x20) X -= W * 0.5f;
	if (Flags & 0x40) Y -= H * 0.5f;
	const bool bMirror = (W < 0.f) != ((Flags & 1) != 0);
	if (W < 0.f) D.SpriteEx(Id, X + W, Y, -W, H, C, 0, true, bMirror, (Flags & 2) != 0);
	else if (Flags & 3) D.SpriteEx(Id, X, Y, W, H, C, 0, true, bMirror, (Flags & 2) != 0);
	else D.Sprite(Id, X, Y, W, H, C, 0, true);
}

void FSSXHudOverlay::DrawPlate(FSSXFeDrawList& D, float X, float Y, float S, float W, const FLinearColor& C) const
{
	const float H = D.FontHeight(EFont::Title, 1.f) * S + PlatePad * S;
	const float Top = Y - PlateRaise * S;
	const float Cw = PlateCapW + PlateCapK * H;
	D.Sprite(0x3A, X - W * 0.5f, Top, W, H, C, 0, true);
	D.Sprite(0x38, X - W * 0.5f - Cw, Top, Cw, H, C, 0, true);
	D.Sprite(0x39, X + W * 0.5f, Top, Cw, H, C, 0, true);
}

float FSSXHudOverlay::Txt(FSSXFeDrawList& D, bool bMenuFont, float X, float Y, float Scale, uint32 Flags, const FLinearColor& C, const FString& S) const
{
	// FUN_0019d3f0 / FUN_0019d568
	const EFont F = bMenuFont ? EFont::Menu : EFont::Title;
	const float H = D.FontHeight(F, Scale);
	if (Flags & 0x40) Y -= H * 0.5f; else if (Flags & 4) Y -= H;
	if (Flags & 0x80)
	{
		float Cell = 0.f;
		for (TCHAR Ch : S)
		{
			float Xo = 0.f, Wd = 0.f;
			D.GlyphInk(F, Ch, Scale, Xo, Wd);
			Cell = FMath::Max(Cell, Wd);
		}
		const float W = Cell * S.Len();
		float X0 = (Flags & 0x20) ? X - W * 0.5f : ((Flags & 8) ? X - W : X);
		const int32 FirstText = D.Texts.Num();
		for (int32 I = 0; I < S.Len(); ++I)
		{
			float Xo = 0.f, Wd = 0.f;
			D.GlyphInk(F, S[I], Scale, Xo, Wd);
			D.Text(F, X0 + (Cell - Wd) * 0.5f - Xo, Y, Scale, C, S.Mid(I, 1), ESSXAlign::Left, true, 2);
			X0 += Cell;
		}
		D.Texts.SetNum(FirstText);
		D.Texts.Add(S);
		return W;
	}
	const float W = D.TextInkWidth(F, S, Scale);
	const float X0 = (Flags & 0x20) ? X - W * 0.5f : ((Flags & 8) ? X - W : X);
	D.Text(F, X0, Y, Scale, C, S, ESSXAlign::Left, true, 2);
	return W;
}

void FSSXHudOverlay::Draw(FSSXFeDrawList& D, const FSSXHudInput& In) const
{
	if (IsHidden(In)) return;
	if (In.LessonState >= 0 && In.LessonState != 5 && In.LessonState != 7 && In.LessonState != 8) return;
	const FSSXPromptScope Prompts(D, In.LessonState >= 0 ? ESSXPromptContext::Original : ESSXPromptContext::Race);
	const uint32 Mask = MaskFor(In.HudDetail);
	for (int32 I = 0; I < In.Players.Num() && I < MaxPlayers; ++I)
	{
		const bool bDim = In.bDimOverlay || In.Players[I].FinishResult >= 0;   // 0x1A2C40 / FUN_001A77A8
		DrawPass1(D, In, I, Mask, bDim ? 0.5f : 1.f);
	}
	for (int32 I = 0; I < In.Players.Num() && I < MaxPlayers; ++I)
	{
		const bool bDim = In.bDimOverlay || In.Players[I].FinishResult >= 0;
		DrawPass2(D, In, I, Mask, bDim ? 0.35f : 1.f);
	}
}

void FSSXHudOverlay::DrawPass1(FSSXFeDrawList& D, const FSSXHudInput& In, int32 I, uint32 Mask, float A) const
{
	const FSSXHudPlayerInput& Pl = In.Players[I];
	const FPlayerState& S = P[I];
	const int32 Lay = LayoutFor(I, In.Players.Num());
	const float Vx = Pl.VpX, Vy = Pl.VpY;
	const bool b2P = In.Players.Num() >= 2;
	const FSSXHudRec& B = R(Lay, 14);

	float MeterA = A;
	if ((Mask & SSXHudBit::Boost) && Data && Data->Boost.Num() == 14)
	{
		MeterA = S.SegScale[0] == Data->Boost[0].Lo ? 0.5f : A;
		const FSSXHudRec& Sp = R(Lay, 15);
		Spr(D, 0x17, Vx + B.X + Sp.X, Vy + B.Y + Sp.Y, Sp.Sx, Sp.Sy, Sp.Flags, WithA(White, MeterA));
		for (int32 K = 0; K < 14; ++K)
		{
			const FSSXHudBoostSeg& Seg = Data->Boost[K];
			const bool bLit = S.SegLit[K];
			const float Sx = B.Sx * (bLit ? S.Pulse : 1.f) * S.SegScale[K];
			Spr(D, bLit ? Seg.Sprite : 0x15, Vx + B.X + Seg.Dx, Vy + B.Y + Seg.Dy, Sx, S.SegScale[K], 0x60, WithA(White, MeterA));
		}
	}
	const FSSXHudRec& Ob = R(Lay, 22);
	const bool bOrbActive = Pl.TrickyTimer > 0.0;
	if (Mask & SSXHudBit::Orb)
	{
		if (bOrbActive) Spr(D, 0x25, Vx + B.X + Ob.X, Vy + B.Y + Ob.Y - S.Bounce, Ob.Sx * S.OrbW, Ob.Sy, Ob.Flags, WithA(White, MeterA));
		else Spr(D, 0x26, Vx + B.X + Ob.X, Vy + B.Y + Ob.Y, Ob.Sx, Ob.Sy, Ob.Flags, WithA(White, MeterA * 0.7f));
	}
	if ((Mask & SSXHudBit::Letters) && Data)
	{
		const FSSXHudRec& Lr = R(Lay, 23);
		const float Ox = Vx + B.X + Lr.X + Ob.X, Oy = Vy + B.Y + Lr.Y + Ob.Y - S.Bounce;
		for (int32 K = 0; K < Data->Letters.Num(); ++K)
		{
			const FSSXHudLetter& Le = Data->Letters[K];
			const FSSXHudRec& Pr = R(Lay, Le.Rec);
			Spr(D, K < Pl.UberCount ? Le.Lit : Le.Dark, Ox + Pr.X, Oy + Pr.Y, Pr.Sx, Pr.Sy, Pr.Flags, WithA(White, A));
		}
	}
	if ((Mask & SSXHudBit::Switch) && S.bSwitch)
	{
		const FSSXHudRec& Sw = R(Lay, 12);
		Spr(D, 0x18, Vx + Sw.X, Vy + Sw.Y, Sw.Sx, Sw.Sy, Sw.Flags, WithA(White, A));
	}
	if (Mask & SSXHudBit::Strip)
	{
		const FSSXHudRec& O = R(Lay, 7);
		const FSSXHudRec& R8 = R(Lay, 8);
		const FSSXHudRec& R9 = R(Lay, 9);
		if (R9.Sx > 0.f)
		{
			const FSSXHudRec& R10 = R(Lay, 10);
			const FSSXHudRec& R11 = R(Lay, 11);
			Spr(D, 9, Vx + O.X + R9.X, Vy + O.Y + R9.Y, R9.Sx, R9.Sy, R9.Flags, WithA(White, A));
			Spr(D, 0x0A, Vx + O.X + R10.X, Vy + O.Y + R10.Y, R10.Sx, R10.Sy, R10.Flags, WithA(White, A));
			Spr(D, 9, Vx + O.X + R11.X, Vy + O.Y + R11.Y, R11.Sx, R11.Sy, R11.Flags, WithA(White, A));
		}
		// 0x1A2C40
		const float Mx = Vx + O.X + R8.X, My = Vy + O.Y + R8.Y;
		for (int32 K = 0; K < Markers.Num(); ++K)
		{
			const float U = S.MarkerD[K] / StripRange, Au = FMath::Abs(U);
			if (U <= -1.f || U >= 1.f) continue;
			float Dy = U * StripPx;
			const float F = Dy > 0.f ? (Au - 0.25f) * 4.f : (Au - 0.5f) + (Au - 0.5f);
			const float Fa = F < 0.f ? 1.f : (F > 1.f ? 0.f : 1.f - F);
			if (Markers[K].Kind == 1 && Dy > 0.f) Dy = 0.f;
			const int32 Id = Markers[K].Kind == 1 ? 7 : 8;
			const FLinearColor Mc = WithA(White, A * Fa);
			if (Lay == 0) { Spr(D, Id, Mx, My + Dy, O.Sx, O.Sy, 8, Mc); Spr(D, Id, Mx, My + Dy, O.Sx, O.Sy, 1, Mc); }
			else if (Lay == 1) { Spr(D, Id, Mx, My + Dy, O.Sx, O.Sy, 8, Mc); }
			else { Spr(D, Id, Mx, My + Dy, O.Sx, O.Sy, 1, Mc); }
		}
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 K = 0; K < 6 && K < In.RacerDist.Num(); ++K)
			{
				if (K == Pl.Racer) continue;
				const bool bCpu = In.RacerComputer.IsValidIndex(K) && In.RacerComputer[K];
				if (bCpu != (Pass == 0)) continue;
				const float U = S.RivalD[K] / StripRange, Au = FMath::Abs(U);
				if (Au >= 1.f) continue;
				const float Fa = Au > 0.5f ? (1.f - Au) / 0.5f : 1.f;
				const int32 Id = bCpu ? (Lay == 1 ? 6 : 5) : (Lay == 2 ? 1 : (Lay == 1 ? 4 : -1));
				if (Id >= 0) Spr(D, Id, Mx, My + U * StripPx, O.Sx, O.Sy, Lay == 1 ? 0x48 : 0x40, WithA(White, A * Fa));
			}
		}
		if (Lay < 2) Spr(D, 3, Mx, My, R8.Sx, O.Sy, 0x48, WithA(White, A));
		else Spr(D, 2, Mx, My, O.Sx, O.Sy, 0x40, WithA(White, A));
	}
	const FSSXHudRec& R30 = R(Lay, 30);
	const SSXTrick::FTrickTables& Tt = SSXTrick::FTrickTables::Get();
	auto TierColor = [&Tt](int32 Tier, float Alpha)
	{
		const SSXTrick::FPointsTier& Tc = Tt.Tiers[FMath::Clamp(Tier, 0, SSXTrick::NumTiers - 1)];
		return Tt.bGrids ? FeColor(Tc.A * Alpha, Tc.R, Tc.G, Tc.B) : WithA(White, Alpha);
	};
	if ((Mask & SSXHudBit::Points) && S.PointsPending >= 1.f)
	{
		const FString Num = FString::FromInt((int32)S.PointsPending);
		const float Sc = R30.Sx + S.PointsPop;
		const float W = Txt(D, false, Vx + R30.X, Vy + R30.Y, Sc, R30.Flags, WithA(White, A), Num);
		if (Mask & SSXHudBit::PlateStyle)
		{
			DrawPlate(D, Vx + R30.X, Vy + R30.Y, Sc, W, TierColor(S.PointsTier, A));
		}
	}
	const bool bRacing = Pl.FinishResult < 0;
	const FSSXHudRec& R4 = R(Lay, 4);
	bool bComboShown = false, bOther = false;
	for (const FSSXHudMsg& M : Pl.Messages)
	{
		if (!M.IsLive()) continue;
		const float T = M.Progress();
		const float Am = A * (1.f - T);
		if (M.Type == SSXHudMsg::ComboBonus && (Mask & SSXHudBit::ComboBonus)) bComboShown = true;
		if (bRacing && (Mask & SSXHudBit::Points))
		{
			if (M.Type == SSXHudMsg::Points2 || M.Type == SSXHudMsg::Points3)
			{
				const float K = T * T + T;
				const float X = Vx + R30.X + (R4.X - R30.X) * K, Y = Vy + R30.Y + (R4.Y - R30.Y) * K;
				const FLinearColor C = M.Type == SSXHudMsg::Points3 ? FeColor(Am, 1.f, 0.5f, 0.5f) : WithA(White, Am);
				const float Sc = (1.f - T * 0.5f) * S.Pulse * R30.Sy * R30.Sx;
				const float W = Txt(D, false, X, Y, Sc, R30.Flags, C, FString::FromInt(M.Value));
				if (Mask & SSXHudBit::PlateStyle) DrawPlate(D, X, Y, Sc, W, TierColor(Tt.PointsTier((float)M.Extra), Am));
			}
			else if (M.Type == SSXHudMsg::Points1)
			{
				Txt(D, false, Vx + R30.X, Vy + R30.Y, R30.Sx, R30.Flags, WithA(White, Am), FString::FromInt(M.Value));
			}
			else if (M.Type == SSXHudMsg::Combo)
			{
				const FSSXHudRec& R41 = R(Lay, 41);
				Txt(D, false, Vx + R41.X, Vy + R41.Y, R41.Sx + 0.75f * T, R41.Flags, WithA(White, Am), L(0xE41));
			}
		}
	}
	const float Cx = Vx + Pl.VpW * 0.5f;
	for (const FSSXHudMsg& M : Pl.Messages)
	{
		if (!M.IsLive()) continue;
		const float T = M.Progress();
		const FLinearColor G = WithA(HudGold, A * (1.f - T));
		if (M.Type == SSXHudMsg::ComboBonus && (Mask & SSXHudBit::ComboBonus))
		{
			const int32 Id = 0xEB3 + FMath::Clamp(M.Value / 4000, 1, 4) - 1;
			const float Sc = b2P ? 0.6f : 0.94f;
			Txt(D, false, Cx, Vy + 175.f, Sc, 0x20, G, L(Id));
			Txt(D, false, Cx, Vy + 225.f - 15.f, Sc, 0x20, G, FString::Printf(TEXT("+%d"), M.Value));
			bOther = true;
		}
		else if (M.Type == SSXHudMsg::BigAir && (Mask & SSXHudBit::BigAir))
		{
			const float Off = bComboShown ? 100.f : 0.f;
			const float Sc = b2P ? 0.6f : 0.94f;
			Txt(D, false, Cx, Vy + 175.f + Off, Sc, 0x20, G, L(0xE94));
			Txt(D, false, Cx, Vy + 225.f + Off - 15.f, Sc, 0x20, G, FString::Printf(TEXT("+%d"), M.Value));
			bOther = true;
		}
		else if (M.Type == SSXHudMsg::Camera && (Mask & SSXHudBit::CameraIcon))
		{
			const FSSXHudRec& R52 = R(Lay, 52);
			Spr(D, 0x35, Vx + R52.X, Vy + R52.Y, R52.Sx, R52.Sy, R52.Flags, WithA(White, A));
		}
		else if ((M.Type == SSXHudMsg::TimeBonus || M.Type == SSXHudMsg::CheckpointDelta) && (Mask & SSXHudBit::TimePopup) && T < 0.5f)
		{
			const FSSXHudRec& R13 = R(Lay, 13);
			const bool bBonus = M.Type == SSXHudMsg::TimeBonus;
			Txt(D, false, Vx + R13.X, Vy + R13.Y, R13.Sx, R13.Flags, WithA(bBonus ? Cyan : White, A * (1.f - T)), L(bBonus ? 0xE59 : 0xE40));
		}
	}
	for (const FSSXHudMsg& M : Pl.Messages)
	{
		if (M.Type == SSXHudMsg::Knockdown && (Mask & SSXHudBit::Knockdown) && M.IsLive())
		{
			Txt(D, false, Cx, Vy + (bOther ? 300.f : 210.f), b2P ? 0.7f : 1.f, 0x20, WithA(HudGold, A * (1.f - M.Progress())), L(0xE93));
		}
	}
	if ((Mask & SSXHudBit::FinalSeconds) && In.Clock > 1.0 && In.Clock < 11.0 && bRacing)
	{
		const FSSXHudRec& R40 = R(Lay, 40);
		const float Fr = (float)FMath::Frac(In.Clock);
		// TODO: only sx = 2 + (1 - frac) is given;
		Txt(D, false, Vx + R40.X, Vy + R40.Y, 2.f + (1.f - Fr), R40.Flags, WithA(ClockRed, A * Fr), FString::FromInt((int32)In.Clock));
	}
	if ((Mask & SSXHudBit::Medal) && S.MedalTier != 0)   // 0x1A2C40
	{
		const FSSXHudRec& R36 = R(Lay, 36);
		const float Sc = R36.Sx + Pop(S.MedalPop, 0.7f);
		Spr(D, 0x26 + S.MedalTier, Vx + R36.X, Vy + R36.Y, Sc, Sc, R36.Flags, WithA(White, A));
	}
}

void FSSXHudOverlay::DrawPass2(FSSXFeDrawList& D, const FSSXHudInput& In, int32 I, uint32 Mask, float A) const
{
	const FSSXHudPlayerInput& Pl = In.Players[I];
	const FPlayerState& S = P[I];
	const int32 Lay = LayoutFor(I, In.Players.Num());
	const float Vx = Pl.VpX, Vy = Pl.VpY;
	const FLinearColor W = WithA(White, A);

	if (Mask & SSXHudBit::Rank)
	{
		const FSSXHudRec& R1 = R(Lay, 1);
		const FSSXHudRec& R2 = R(Lay, 2);
		const int32 Place = Pl.Rank + 1;
		Txt(D, false, Vx + R1.X, Vy + R1.Y, R1.Sx, R1.Flags, Place == 1 ? WithA(HudGold, A) : W, FString::FromInt(Place));
		static const int32 Suffix[] = { 0xE44, 0xE54, 0xE58, 0xE45 };
		Txt(D, false, Vx + R2.X, Vy + R2.Y, R2.Sx, R2.Flags, W, L(Suffix[FMath::Clamp(Place, 1, 4) - 1]));
		if (!(Mask & SSXHudBit::Medal))
		{
			const FSSXHudRec& R3 = R(Lay, 3);
			Txt(D, false, Vx + R3.X, Vy + R3.Y, R3.Sx, R3.Flags, W, FString::Printf(TEXT("/%d"), In.RacerCount));
		}
	}
	if ((Mask & SSXHudBit::RaceTime) && In.Phase != 3)
	{
		const FSSXHudRec& R5 = R(Lay, 5);
		const float Sc = R5.Sx;
		const float Y = Vy + R5.Y - ((R5.Flags & 0x40) ? 0.f : D.FontHeight(EFont::Title, Sc));
		Txt(D, false, Vx + R5.X, Y, Sc, R5.Flags, W, TimeText((int32)(In.RaceTimeSec * 100.0)));
	}
	if ((Mask & SSXHudBit::TimeToBeat) && Lay <= 1 && R(Lay, 51).Sx > 0.f)
	{
		// 0x1A53A0 / 0x1A565C / FUN_0011ea28
		const FSSXHudRec& R51 = R(Lay, 51);
		if (!(Lay == 0 && Pl.FinishResult >= 0))
		{
			const int32 Diff = In.TimeToBeatCs - (int32)(In.RaceTimeSec * 100.0);
			const float DiffS = (float)Diff / 100.f;
			float Sc = R51.Sx;
			if (DiffS > 0.f && DiffS < 10.f)
			{
				const float Fr = DiffS - (float)(int32)DiffS;
				Sc += 0.7f * (Fr < 0.5f ? Fr + Fr : (1.f - Fr) + (1.f - Fr));
			}
			const float At = Pl.FinishResult < 0 ? 1.f : A;
			const FLinearColor C = WithA(DiffS > 0.f ? BeatGreen : BeatRed, At);
			if ((R51.Flags & 0x60) == 0)
			{
				const float Grow = Sc - R51.Sx;
				Txt(D, false, Vx + R51.X - Grow * 65.f, Vy + R51.Y - Grow * 10.f - Sc, Sc, R51.Flags, C, FormatGap(Diff));
			}
			else
			{
				Txt(D, false, Vx + R51.X, Vy + R51.Y, Sc, R51.Flags, C, FormatGap(Diff));
			}
		}
	}
	if (Mask & SSXHudBit::Speed)
	{
		const FSSXHudRec& R6 = R(Lay, 6);
		const float Pp = Pop(S.SpeedPop, 1.5f);
		const int32 V = (int32)((float)Pl.Speed * (In.bMph ? MphPerCms : KmhPerCms));
		Txt(D, false, Vx + R6.X, Vy + R6.Y, R6.Sx + Pp, 8 | 4, W, FString::FromInt(V));
		Txt(D, false, Vx + R6.X, Vy + R6.Y, R6.Sy + Pp, 4, W, Data ? (In.bMph ? Data->UnitMph : Data->UnitKmh) : FString());
	}
	if (Mask & SSXHudBit::Score)
	{
		const FSSXHudRec& R4 = R(Lay, 4);
		Txt(D, false, Vx + R4.X, Vy + R4.Y, 1.1f * S.Pulse, R4.Flags, S.Pulse == 1.f ? W : WithA(PopGreen, A), FString::FromInt((int32)Pl.Score));
	}
	bool bTrickText = false;
	bool bMultiplierDrawn = false;   // 0x1A4F70
	for (const FSSXHudMsg& M : Pl.Messages)
	{
		if (!M.IsLive()) continue;
		const float T = M.Progress();
		if (M.Type == SSXHudMsg::Laps && (Mask & SSXHudBit::LapMsg))
		{
			const FSSXHudRec& R35 = R(Lay, 35);
			const FString Txt35 = M.Value == 2 ? L(0x115) : L(0x116).Replace(TEXT("%d"), *FString::FromInt(M.Value - 1));
			Txt(D, false, Vx + R35.X, Vy + R35.Y, R35.Sy + R35.Sx * T, R35.Flags, WithA(LapColor, A * (1.f - T)), Txt35);
		}
		else if (M.Type == SSXHudMsg::Multiplier && (Mask & SSXHudBit::Multiplier))
		{
			// 0x157008 / 0x157308
			if (bMultiplierDrawn) continue;
			bMultiplierDrawn = true;
			const FSSXHudRec& R31 = R(Lay, 31);
			const int32 N = M.Value;
			const FLinearColor C = N == 2 ? LapColor : (N == 3 ? FeColor(1.f, 1.f, 0.471f, 0.039f) : (N == 5 ? FeColor(1.f, 0.992f, 0.f, 0.f) : White));
			Txt(D, false, Vx + R31.X, Vy + R31.Y, R31.Sx, R31.Flags, WithA(C, A), FString::Printf(TEXT("X%c"), TCHAR('0' + N)));
		}
		else if ((M.Type == SSXHudMsg::TrickAir || M.Type == SSXHudMsg::TrickLanded) && (Mask & SSXHudBit::TrickName))
		{
			bTrickText = true;
		}
		else if ((M.Type == SSXHudMsg::TimeBonus || M.Type == SSXHudMsg::CheckpointDelta) && (Mask & SSXHudBit::TimePopup) && T >= 0.5f)
		{
			const FSSXHudRec& R13 = R(Lay, 13);
			FString Str;
			FLinearColor C = BeatGreen;
			if (M.Type == SSXHudMsg::TimeBonus) { Str = L(0xE29).Replace(TEXT("%d"), *FString::FromInt(M.Value)); C = PopGreen; }
			else
			{
				// 0x1A4F70 / FUN_001A6BA0
				Str = (M.Value >= 0 ? TEXT("+") : TEXT("-")) + TimeText(FMath::Abs(M.Value));
				C = M.Value >= 0 ? PopGreen : DeltaRed;
			}
			Txt(D, false, Vx + R13.X, Vy + R13.Y, R13.Sy, R13.Flags, WithA(C, A * (1.f - T)), Str);
		}
	}
	if (bTrickText && !Pl.TrickText.IsEmpty())
	{
		// FUN_001a6d38
		const FSSXHudRec& R32 = R(Lay, 32);
		const float Sc = 1.f;
		const float Wmax = Pl.VpW * 0.4f;
		TArray<FString> Words, Lines;
		Pl.TrickText.ParseIntoArray(Words, TEXT(" "));
		FString Cur;
		for (const FString& Wd : Words)
		{
			const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
			if (!Cur.IsEmpty() && D.TextInkWidth(EFont::Menu, Try, Sc) > Wmax) { Lines.Add(Cur); Cur = Wd; }
			else Cur = Try;
		}
		if (!Cur.IsEmpty()) Lines.Add(Cur);
		const float Step = D.FontHeight(EFont::Menu, Sc) + 4.f;
		float Y = Vy + Pl.VpH - R32.Y;
		for (int32 K = Lines.Num() - 1; K >= 0; --K)
		{
			Txt(D, true, Vx + R32.X, Y, Sc, 0x20 | 4, WithA(TrickColor, A), Lines[K]);
			Y -= Step;
		}
	}
	// FUN_00164840
	if ((Mask & SSXHudBit::TrickName) && !bTrickText && UberHintActive(In, Pl))
	{
		const bool b2P = In.Players.Num() >= 2;
		const float Ah = A * UberHintAlpha(Pl);
		const FSSXHudRec& R32 = R(Lay, 32);
		if (UberLabelOn(In.RaceFlags))
		{
			Txt(D, true, Vx + R32.X, Vy + Pl.VpH * (1.f - UberLabelFrac(b2P)), R32.Sx, 0x20, WithA(FeColor(1.f, 1.f, 0.f, 0.f), Ah), L(0xDF2));
		}
		// 0x154B18
		if (const SSXFeTrickBook::FEntry* Rec = SSXFeTrickBook::FTable::Get().Find(Pl.Character, UberHintRecord(Pl.UberHintIndex)))
		{
			const FString Str = UberComboString(*Rec, [&Pl](int32 Act) { return FPadMapper::ActionMask(Act, Pl.PadLayout, Pl.Character, Pl.BoardClass); }, Pl.bFlipSpin);
			auto Adv = [&D](TCHAR Ch) -> float
			{
				if (const FUberIcon* Ic = FindUberIcon(Ch)) return Ic->Adv;
				return Ch == ' ' ? 8.f : D.TextWidth(EFont::Menu, FString(1, &Ch), 1.f) + 1.f;
			};
			float Width = 0.f;
			for (TCHAR Ch : Str) Width += Adv(Ch);
			float X = Vx + Pl.VpW * 0.5f - Width * 0.5f;
			const float Y = Vy + Pl.VpH * (1.f - UberComboFrac(b2P));
			const FLinearColor C = WithA(White, Ah);
			for (TCHAR Ch : Str)
			{
				if (const FUberIcon* Ic = FindUberIcon(Ch))
				{
					Spr(D, Ic->Sprite, X + Ic->Xo, Y + Ic->Yo, 1.f, 1.f, 0, C);
				}
				else if (Ch != ' ')
				{
					const float Dy = (Ch == '(' || Ch == ')') ? -1.f : (Ch == '+' ? -2.f : 0.f);
					Txt(D, true, X, Y + Dy, 1.f, 0, C, FString(1, &Ch));
				}
				X += Adv(Ch);
			}
		}
	}
	if ((Mask & SSXHudBit::Strip) && Loc)
	{
		const FSSXHudRec& O = R(Lay, 7);
		const FSSXHudRec& R8 = R(Lay, 8);
		static const int32 Label[] = { 0xE22, 0xE23, 0xE27 };
		for (int32 K = 0; K < Markers.Num(); ++K)
		{
			// 0x1A4F70 / FUN_0019D568
			const float U = S.MarkerD[K] / StripRange, Au = FMath::Abs(U);
			if (U <= -1.f || U >= 1.f) continue;
			float Dy = U * StripPx;
			const float F = Dy > 0.f ? (Au - 0.25f) * 4.f : (Au - 0.5f) + (Au - 0.5f);
			const float Fa = F < 0.f ? 1.f : (F > 1.f ? 0.f : 1.f - F);
			if (Markers[K].Kind == 1 && Dy > 0.f) Dy = 0.f;
			const FString Str = L(Label[Markers[K].Kind]);
			float X = Vx + O.X + R8.X;
			if (Lay == 0) X -= 20.f;
			else if (Lay == 1) X -= D.TextInkWidth(EFont::Title, Str, 0.5f) + 3.f;
			else X += 3.f;
			Txt(D, false, X, Vy + O.Y + R8.Y - D.FontHeight(EFont::Title, 1.f) * 0.5f + Dy, 0.5f, 0, WithA(White, A * Fa), Str);
		}
	}
	// 0x1A4F70 / FUN_0019d568
	if ((Mask & SSXHudBit::Medal) && In.RacerCount == 1)
	{
		if (S.MedalPop >= 1.f)
		{
			if (S.MedalTier < 3)
			{
				static const int32 Label[] = { 0xE32, 0xE26, 0xE25 };
				const int32 Target = S.MedalTier == 0 ? MedalBronze : (S.MedalTier == 1 ? MedalSilver : MedalGold);
				const FSSXHudRec& Rm = R(Lay, S.MedalTier == 0 ? 38 : 39);
				Txt(D, false, Vx + Rm.X, Vy + Rm.Y - D.FontHeight(EFont::Title, Rm.Sx), Rm.Sx, 0, W, L(Label[S.MedalTier]));
				Txt(D, false, Vx + Rm.X, Vy + Rm.Y, Rm.Sy, 0, W, FString::FromInt(Target));
			}
		}
		else if (S.MedalTier >= 1)
		{
			// FUN_001a68f8
			static const int32 Name[] = { 0xE32, 0xE3B, 0xE35 };
			const FSSXHudRec& R37 = R(Lay, 37);
			const float Pp = Pop(S.MedalPop, 0.7f);
			const float S1 = R37.Sx + Pp;
			Txt(D, false, Vx + R37.X, Vy + R37.Y - D.FontHeight(EFont::Title, S1), S1, 0, W, L(Name[FMath::Min(S.MedalTier, 3) - 1]));
			Txt(D, false, Vx + R37.X, Vy + R37.Y, R37.Sy + Pp, 0, W, L(0xE28));
		}
	}
	if (In.bReplayFull)
	{
		Txt(D, true, Vx + Pl.VpW * 0.5f, Vy + Pl.VpH - 30.f, 1.f, 0x20, WithA(ReplayFull, A), L(0x1B8));
	}
}
