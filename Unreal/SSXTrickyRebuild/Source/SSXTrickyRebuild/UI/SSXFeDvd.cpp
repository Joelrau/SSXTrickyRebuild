#include "UI/SSXFeDvd.h"

#include "UI/SSXFeDraw.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndData.h"

using namespace SSXFe;

int32 SSXFeDvdTables::MainLoc(int32 Item)
{
	static const int32 T[9] = { 3323, 3319, 3318, 3321, 3320, 3322, 3317, 3324, 3325 };   // 0x3B2420
	return Item >= 0 && Item < 9 ? T[Item] : -1;
}

FString SSXFeDvdTables::MainMovie(int32 Item)
{
	switch (Item)   // 0x293C20 / 0x3B2138
	{
	case 0: return TEXT("EVOLVE");
	case 1: return TEXT("TRICKS");
	case 2: return TEXT("TRACKS");
	case 3: return TEXT("CHARACTR");
	case 5: return TEXT("MUSIC");
	case 6: return TEXT("AUDIO");
	default: return FString();
	}
}

bool SSXFeDvdTables::TalentRange(int32 Char, int32& OutFirst, int32& OutLast)
{
	// FUN_00296870 / 0x3B2530
	static const int32 T[12][2] = { { 0xD33, 0xD3F }, { -1, -1 }, { 0xD40, 0xD49 }, { 0xD4A, 0xD52 }, { 0xD53, 0xD5C }, { 0xD5D, 0xD68 },
		{ -1, -1 }, { 0xD69, 0xD74 }, { 0xD75, 0xD7F }, { 0xD80, 0xD89 }, { 0xD8A, 0xD94 }, { 0xD95, 0xDA2 } };
	if (Char < 0 || Char >= 12 || T[Char][0] < 0)
	{
		return false;
	}
	OutFirst = T[Char][0];
	OutLast = T[Char][1];
	return true;
}

int32 SSXFeDvdTables::MainSpeech(int32 Item)
{
	static const int32 T[9] = { 0x3A, 0x3C, 0x3B, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42 };
	return Item >= 0 && Item < 9 ? T[Item] : -1;
}

int32 SSXFeDvdTables::VoiceSpeech(int32 Char)
{
	static const int32 T[12] = { 0x47, -1, 0x46, 0x43, 0x44, 0x4B, -1, 0x4A, 0x4C, 0x4D, 0x4E, 0x49 };
	return Char >= 0 && Char < 12 ? T[Char] : -1;
}

int32 SSXFeDvdTables::MainTile(int32 Item)
{
	static const int32 T[9] = { 0x68, 0x6A, 0x69, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70 };
	return Item >= 0 && Item < 9 ? T[Item] : -5;
}

int32 SSXFeDvdTables::VoicePortrait(int32 Char)
{
	static const int32 T[12] = { 0x71, -5, 0x72, 0x73, 0x74, 0x75, -5, 0x76, 0x77, 0x78, 0x79, 0x7A };
	return Char >= 0 && Char < 12 ? T[Char] : -5;
}

FString SSXFeDvdTables::FolderOf(int32 Sprite)
{
	return Sprite >= 0x71 ? TEXT("DVD_CHAR") : TEXT("DVD_MAIN");
}

FString SSXFeDvdTables::ImageOf(int32 Sprite)
{
	// FUN_001F9660
	static const TCHAR* Main[9] = { TEXT("dssx"), TEXT("dtra"), TEXT("dtrk"), TEXT("dcha"), TEXT("dvoi"), TEXT("dmus"), TEXT("daud"), TEXT("djuk"), TEXT("ddem") };
	static const TCHAR* Chr[10] = { TEXT("dved"), TEXT("dvlu"), TEXT("dvmc"), TEXT("dvmo"), TEXT("dvzo"), TEXT("dvel"), TEXT("dvps"), TEXT("dvse"), TEXT("dvbr"), TEXT("dvma") };
	if (Sprite >= 0x68 && Sprite < 0x71) { return Main[Sprite - 0x68]; }
	if (Sprite >= 0x71 && Sprite < 0x7B) { return Chr[Sprite - 0x71]; }
	return FString();
}

FName SSXFeDvdTables::PageOf(int32 Sprite)
{
	return FName(*(FolderOf(Sprite) + TEXT("/") + ImageOf(Sprite)));
}

float FSSXFeDvd::ItemH(const FSSXFeDrawList& D, const FItem& I)
{
	return I.bIcon ? 10.f : D.FontHeight(I.Font, I.Scale) + (I.bShadow ? ShadowOffset : 0.f);
}

float FSSXFeDvd::FItem::YAt(const FSSXFeDrawList& D) const
{
	return bAbsY ? Y : BaseY + Row * (FSSXFeDvd::ItemH(D, *this) + FSSXFeDvd::Spacing);
}

void FSSXFeDvd::Build(const FSSXLocStrings* Loc)
{
	auto L = [Loc](int32 Id) { return Loc ? Loc->ByIndex(Id) : FString(); };
	auto Column = [](FList& List, float X0, float Y0, float Dx, int32 Frames)
	{
		int32 Row = 0;
		for (FItem& I : List.Items)
		{
			I.X0 = X0;
			I.BaseY = Y0;
			I.Row = Row++;
			I.Slide.Setup(0.f, Dx, Frames, 0);
		}
	};
	*this = FSSXFeDvd();
	// FUN_00274108
	for (int32 K = 0; K < 9; ++K)
	{
		FItem I;
		I.Id = K;
		I.Text = L(SSXFeDvdTables::MainLoc(K));
		Lists[Main].Items.Add(I);
	}
	Column(Lists[Main], ListX0, MainY, ListSlide, MainFrames);
	// FUN_00297FA0 / 0x3B2918 / 0x348098
	const TArray<int32>& Order = SSXFeTables::IntTable(TEXT("dvd_char_order"));
	const TArray<FString>& Names = SSXFeTables::StringTable(TEXT("dvd_char_names"));
	for (const int32 C : Order)
	{
		if (Names.IsValidIndex(C) && !Names[C].IsEmpty())
		{
			FItem I;
			I.Id = C;
			I.Text = Names[C];
			Lists[Voices].Items.Add(I);
		}
	}
	Column(Lists[Voices], ListX0, MainY, ListSlide, SubFrames);
	// FUN_00297178
	const TArray<FString>& Titles = SSXFeTables::StringTable(TEXT("dvd_jukebox.title"));
	{
		FList Col0, Col1;
		for (int32 S = 0; S < Titles.Num(); ++S)
		{
			FItem I;
			I.Id = S;
			I.Text = Titles[S];
			I.Scale = 0.8f;
			I.Column = S < 11 ? 0 : 1;
			(S < 11 ? Col0 : Col1).Items.Add(I);
		}
		Column(Col0, JukeX0, JukeY, JukeSlide, SubFrames);
		Column(Col1, JukeX1, JukeY, -JukeSlide, SubFrames);
		Lists[Jukebox].Items = Col0.Items;
		Lists[Jukebox].Items.Append(Col1.Items);
	}
	// FUN_00298368 / 0x3B2948
	for (int32 K = 0; K < 2; ++K)
	{
		FItem I;
		I.Id = K;
		I.Text = L(3547 + K);
		Lists[Previews].Items.Add(I);
	}
	Column(Lists[Previews], ListX0, PreviewsY, ListSlide, SubFrames);
	// FUN_002994C8
	TitleSlide.Setup(0.f, TitleSlideBy, TitleFrames, 0);
	VoiceSlide.Setup(0.f, VoiceSlideBy, VoiceFrames, 0);
	TileFade.Setup(0.f, 1.f, 15, 0);
	PortraitFade.Setup(0.f, 1.f, 15, 0);
	for (int32 K = 0; K < 4; ++K)
	{
		Pulse[K].Setup(K % 2 == 0 ? 1.f : 0.5f, K % 2 == 0 ? 0.5f : 1.f, 120, 2);
		Pulse[K].Activate(false);
	}
	for (int32 K = 0; K < 5; ++K)
	{
		Chevron[K].Setup(0.f, 1.f, 60, 1);
		Chevron[K].bInit = true;
		Chevron[K].N = 60 - int32((K / 5.f) * 60.f);
		Chevron[K].Activate(false);
	}
	Arrow.Setup(0.f, 1.f, 35, 2);
	Arrow.bInit = true;
	Arrow.Activate(false);
	Phase = 1;
	ChainList = Main;
	ChainItem = 0;
	Lists[Main].Items[0].Slide.Activate(false);
	Focus = None;
	LegendMask = 10;
}

void FSSXFeDvd::BuildTalent(int32 C, const FSSXLocStrings* Loc)
{
	// FUN_00296870
	FList& T = Lists[Talent];
	T.Items.Reset();
	T.Sel = 0;
	Char = C;
	int32 First = 0, Last = -1;
	SSXFeDvdTables::TalentRange(C, First, Last);
	float Y = TalentY0;
	int32 Prev = -1;
	for (int32 K = First; K <= Last; ++K)
	{
		FString Str = Loc ? Loc->ByIndex(K) : FString();
		FItem I;
		I.Id = K;
		I.bSelectable = false;
		if (Str.StartsWith(TEXT("~")))
		{
			if (Prev == 0) { Y += 25.f; } else if (Prev == 1) { Y += 20.f; }
			Prev = 1;
			I.Text = Str.RightChop(1);
			I.Font = EFont::Title;
			I.Scale = 0.6f;
			I.Color = White;
			I.bShadow = true;
		}
		else
		{
			if (Prev == 1) { Y += 20.f; } else if (Prev == 0) { Y += 15.f; }
			Prev = 0;
			I.Text = Str;
			I.Scale = 0.7f;
			I.Color = Black;
			I.bShadow = false;
		}
		I.X0 = TalentX - 640.f;
		I.Y = Y;
		I.bAbsY = true;
		I.Slide.Setup(0.f, 640.f, TalentFrames, 0);
		T.Items.Add(I);
	}
	FItem Play;
	Play.Id = C;
	Play.bIcon = true;
	Play.bShadow = false;
	Play.Color = FLinearColor(0.f, 0.f, 0.f, 0.f);
	Play.X0 = 100.f - 640.f;
	Play.Y = 400.f;
	Play.bAbsY = true;
	Play.Slide.Setup(0.f, 640.f, TalentFrames, 0);
	T.Items.Add(Play);
	T.Sel = T.Items.Num() - 1;
	const TArray<FString>& Full = SSXFeTables::StringTable(TEXT("dvd_talents_full"));
	TalentName = Full.IsValidIndex(C) ? Full[C] : FString();
}

const FSSXFeDvd::FItem* FSSXFeDvd::Selected() const
{
	if (Focus == None) { return nullptr; }
	const FList& L = Lists[Focus];
	return L.Items.IsValidIndex(L.Sel) ? &L.Items[L.Sel] : nullptr;
}

void FSSXFeDvd::SetFocus(EList L, TArray<FString>& OutHooks, const FSSXLocStrings* Loc)
{
	Focus = L;
	if (L != None)
	{
		Highlight(OutHooks, Loc);
	}
}

void FSSXFeDvd::Highlight(TArray<FString>& OutHooks, const FSSXLocStrings* Loc)
{
	auto LocS = [Loc](int32 Id) { return Loc ? Loc->ByIndex(Id) : FString(); };
	const FItem* I = Selected();
	if (!I) { return; }
	auto Swap = [](FSSXFeTween& Fade, bool bShown)
	{
		if (bShown)
		{
			Fade.bReverse = true;
			if (Fade.Frames - 1 <= Fade.N) { Fade.N = Fade.Frames - 2; }
		}
		Fade.Activate(Fade.bReverse);
	};
	if (Focus == Voices)
	{
		PortraitSprite = SSXFeDvdTables::VoicePortrait(I->Id);
		const int32 Sp = SSXFeDvdTables::VoiceSpeech(I->Id);
		if (Sp >= 0) { OutHooks.Add(FString::Printf(TEXT("speech%d"), Sp)); }
		Swap(PortraitFade, bPortraitShown);
		const TArray<FString>& Short = SSXFeTables::StringTable(TEXT("dvd_talents"));
		VoiceLabel = LocS(3536);
		VoiceValue = Short.IsValidIndex(I->Id) ? Short[I->Id] : FString();
	}
	else
	{
		bPortraitShown = false;
	}
	if (Focus == Talent)
	{
		const TArray<FString>& Short = SSXFeTables::StringTable(TEXT("dvd_talents"));
		VoiceLabel = Short.IsValidIndex(Char) ? Short[Char] : FString();
		VoiceValue = SSXFeTables::RiderName(Char);
	}
	if (Focus != Main)
	{
		bTileShown = false;
	}
	else
	{
		TileSprite = SSXFeDvdTables::MainTile(I->Id);
		const int32 Sp = SSXFeDvdTables::MainSpeech(I->Id);
		if (Sp >= 0) { OutHooks.Add(FString::Printf(TEXT("speech%d"), Sp)); }
		Swap(TileFade, bTileShown);
	}
	if (Focus == Jukebox)
	{
		const TArray<FString>& Artist = SSXFeTables::StringTable(TEXT("dvd_jukebox.artist"));
		VoiceLabel = LocS(3540);
		VoiceValue = Artist.IsValidIndex(I->Id) ? Artist[I->Id] : FString();
	}
}

void FSSXFeDvd::PlaySong(int32 S, TArray<FString>& OutHooks)
{
	// FUN_00299338
	OutHooks.Add(FString::Printf(TEXT("jukebox%d"), S));
	Song = S;
	for (int32 K = 0; K < Lists[Jukebox].Items.Num(); ++K)
	{
		if (Lists[Jukebox].Items[K].Id == S)
		{
			PlayIconItem = K;
			PlayIconX = Lists[Jukebox].Items[K].X() - 5.f;
		}
	}
}

FString FSSXFeDvd::Move(int32 DY, int32 DX, TArray<FString>& OutHooks, const FSSXLocStrings* Loc)
{
	if (Focus == None) { return FString(); }
	FList& L = Lists[Focus];
	const int32 N = L.Items.Num();
	if (N == 0) { return FString(); }
	if (Focus == Jukebox)
	{
		const int32 Col = L.Items[L.Sel].Column;
		TArray<int32> C0, C1;
		for (int32 K = 0; K < N; ++K) { (L.Items[K].Column == 0 ? C0 : C1).Add(K); }
		const TArray<int32>& Cur = Col == 0 ? C0 : C1;
		const int32 Row = Cur.IndexOfByKey(L.Sel);
		if (DY != 0)
		{
			L.Sel = Cur[(Row + DY + Cur.Num()) % Cur.Num()];
		}
		else if (DX != 0)
		{
			const TArray<int32>& Other = Col == 0 ? C1 : C0;
			L.Sel = Other[FMath::Min(Row, Other.Num() - 1)];
		}
		Highlight(OutHooks, Loc);
		return TEXT("move");
	}
	if (DY == 0) { return FString(); }
	// FUN_0026D3B8
	TArray<int32> Stops;
	for (int32 K = 0; K < N; ++K) { if (L.Items[K].bSelectable) { Stops.Add(K); } }
	if (Stops.Num() <= 1) { return FString(); }
	const int32 At = FMath::Max(Stops.IndexOfByKey(L.Sel), 0);
	int32 Next = At + DY;
	if (Focus == Main)
	{
		if (Next < 0 || Next >= Stops.Num()) { return TEXT("refused"); }
	}
	else
	{
		Next = (Next + Stops.Num()) % Stops.Num();
	}
	L.Sel = Stops[Next];
	Highlight(OutHooks, Loc);
	return TEXT("move");
}

void FSSXFeDvd::StartChain(EList L, bool bReverse)
{
	ChainList = L;
	ChainItem = 0;
	if (Lists[L].Items.Num() > 0)
	{
		Lists[L].Items[0].Slide.Activate(bReverse);
	}
	if (L == Jukebox)
	{
		for (FItem& I : Lists[Jukebox].Items) { if (I.Column == 1) { I.Slide.Activate(bReverse); break; } }
	}
}

void FSSXFeDvd::Tick(TArray<FString>& OutHooks, const FSSXLocStrings* Loc)
{
	auto TitleIn = [this](int32 TitleId, const FString& Text)
	{
		// FUN_002994C8 / FUN_002995A8
		if (!TitleSlide.bActive)
		{
			TitleLoc = TitleId;
			TitleText = Text;
			TitleSlide.N = 0;
			TitleSlide.Activate(false);
		}
	};
	auto TitleOut = [this]() { TitleSlide.Activate(true); };   // FUN_00299688
	auto Col0Items = [this](EList L) { TArray<int32> Out; for (int32 K = 0; K < Lists[L].Items.Num(); ++K) { if (L != Jukebox || Lists[L].Items[K].Column == 0) Out.Add(K); } return Out; };

	if (Phase == 0)
	{
		// FUN_001F9D28
		auto Picture = [](FSSXFeTween& Fade, bool& bShown, int32 Sprite)
		{
			if (!bShown)
			{
				bShown = true;
				Fade.bInit = true;
				Fade.N = 0;
				Fade.Activate(false);
			}
			else if (Fade.bReverse && Fade.N >= Fade.Frames)
			{
				Fade.N = 0;
				Fade.Activate(false);
			}
		};
		if (Focus == Voices) { Picture(PortraitFade, bPortraitShown, PortraitSprite); }
		if (Focus == Main) { Picture(TileFade, bTileShown, TileSprite); }
		if (Focus == Jukebox && bSongEnded)
		{
			// 0x2986C0 / FUN_002AC0C0 / FUN_00299338
			bSongEnded = false;
			const int32 Next = (Song + 1) % NumSongs;
			FList& J = Lists[Jukebox];
			int32 Found = INDEX_NONE;
			for (int32 Col = 0; Col < 2 && Found == INDEX_NONE; ++Col)
			{
				for (int32 K = 0; K < J.Items.Num(); ++K)
				{
					if (J.Items[K].Column == Col && J.Items[K].Id == Next) { Found = K; break; }
				}
			}
			if (Found != INDEX_NONE)
			{
				J.Sel = Found;
			}
			PlaySong(Next, OutHooks);
		}
	}
	else
	{
		FList& CL = Lists[ChainList];
		const TArray<int32> Chain = Col0Items(ChainList);
		const int32 Pos = Chain.IndexOfByKey(ChainItem);
		FItem* Cur = CL.Items.IsValidIndex(ChainItem) ? &CL.Items[ChainItem] : nullptr;
		const bool bWait = Cur && Cur->Slide.bActive && Cur->Slide.Progress() < 0.5f;
		if (!bWait && Cur && Chain.IsValidIndex(Pos + 1))
		{
			ChainItem = Chain[Pos + 1];
			CL.Items[ChainItem].Slide.Activate(Cur->Slide.bReverse);
			if (Phase == 8 || Phase == 9)
			{
				int32 Row = 0;
				for (FItem& I : Lists[Jukebox].Items)
				{
					if (I.Column == 1 && Row++ == Pos + 1) { I.Slide.Activate(Cur->Slide.bReverse); }
				}
			}
		}
		else if (!bWait)
		{
			switch (Phase)
			{
			case 1:
				Phase = 0;
				SetFocus(Main, OutHooks, Loc);
				TitleIn(3315, FString());
				bPulseShown = true;
				break;
			case 2:
			{
				const int32 Id = Lists[Main].Items.IsValidIndex(Lists[Main].Sel) ? Lists[Main].Items[Lists[Main].Sel].Id : 0;
				if (Id == 4) { Phase = 3; OutHooks.Add(TEXT("speech62")); StartChain(Voices, false); }
				else if (Id == 7) { Phase = 8; OutHooks.Add(TEXT("speech65")); StartChain(Jukebox, false); }
				else if (Id == 8) { Phase = 10; OutHooks.Add(TEXT("speech66")); StartChain(Previews, false); }
				bPulseShown = false;
				break;
			}
			case 3:
				Phase = 0;
				SetFocus(Voices, OutHooks, Loc);
				TitleIn(3320, FString());
				VoiceSlide.Activate(false);
				break;
			case 4:
				Phase = 6;
				StartChain(Talent, false);
				bTalentNameShown = true;
				TitleOut();
				VoiceSlide.Activate(true);
				break;
			case 5: case 9: case 11:
				StartChain(Main, false);
				TitleOut();
				if (Phase != 11) { VoiceSlide.Activate(true); }
				Phase = 1;
				bJukeboxInfo = false;
				break;
			case 6:
			{
				Phase = 0;
				SetFocus(Talent, OutHooks, Loc);
				const TArray<FString>& Names = SSXFeTables::StringTable(TEXT("dvd_char_names"));
				TitleIn(-1, Names.IsValidIndex(Char) ? Names[Char] : FString());
				VoiceSlide.Activate(false);
				break;
			}
			case 7:
				Phase = 3;
				StartChain(Voices, false);
				TitleOut();
				VoiceSlide.Activate(true);
				break;
			case 8:
				Phase = 0;
				SetFocus(Jukebox, OutHooks, Loc);
				bJukeboxInfo = true;
				OutHooks.Add(TEXT("jukeboxStart"));
				if (const FItem* I = Selected()) { PlaySong(I->Id, OutHooks); }
				TitleIn(3324, FString());
				VoiceSlide.Activate(false);
				break;
			case 10:
				Phase = 0;
				SetFocus(Previews, OutHooks, Loc);
				TitleIn(3325, FString());
				break;
			default:
				break;
			}
		}
	}
	for (FList& L : Lists) { for (FItem& I : L.Items) { I.Slide.Tick(); } }
	TitleSlide.Tick();
	VoiceSlide.Tick();
	TileFade.Tick();
	PortraitFade.Tick();
	for (FSSXFeTween& P : Pulse) { P.Tick(); }
	for (FSSXFeTween& C : Chevron) { C.Tick(); }
	Arrow.Tick();
}

void FSSXFeDvd::Draw(FSSXFeDrawList& D, const FSSXLocStrings* Loc) const
{
	auto L = [Loc](int32 Id) { return Loc ? Loc->ByIndex(Id) : FString(); };
	// background "f3dDVDBkgrnd": sprite 47 over 640 x 480, white (its colour tween is never activated), layer 0
	D.Sprite(SprBackground, 0.f, 0.f, ScreenW, ScreenH, White, SpriteLayer(0));
	D.Sprite(SprLogo, 15.f, 10.f, 145.f, 85.f, White, SpriteLayer(3));
	{
		const FString T = TitleText.IsEmpty() ? L(TitleLoc) : TitleText;
		const float W = D.TextBoxWidth(EFont::Title, T, 1.f);
		const float X = TitleX0 + TitleSlide.Cur;
		D.Text(EFont::Title, X - W, TitleY, 1.f, White, T, ESSXAlign::Left, false, TextLayer(0));
		D.Text(EFont::Title, X + TitleShadowDX - W, TitleY, 1.f, FeColor(0.2f, 1.f, 1.f, 1.f), T, ESSXAlign::Left, false, TextLayer(0));
	}
	if (bPulseShown)
	{
		for (int32 K = 0; K < 4; ++K)
		{
			const FSSXSprite* S = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(49 + K) : nullptr;
			const float W = S ? S->W : 84.f, H = S ? S->H : 72.f;
			const float Sc = Pulse[K].Cur;
			D.Icon(49 + K, 80.f + K * W * 0.7f, 425.f - (K % 2) * 0.3f * H, W * Sc, H * Sc, 0x60, White, SpriteLayer(2));
		}
	}
	for (int32 Li = 0; Li < NumLists; ++Li)
	{
		for (const FItem& I : Lists[Li].Items)
		{
			if (I.bIcon)
			{
				D.Rect(I.X(), I.YAt(D), 10.f, 10.f, I.Color, SpriteLayer(2));
				continue;
			}
			D.Text(I.Font, I.X(), I.YAt(D), I.Scale, I.Color, I.Text, ESSXAlign::Left, I.bShadow, TextLayer(2));
		}
	}
	if (bTileShown && TileSprite >= 0)
	{
		D.PageSprite(SSXFeDvdTables::PageOf(TileSprite), TileSprite, TileX, TileY, TileW, TileH, FeColor(TileFade.Cur, 1.f, 1.f, 1.f), SpriteLayer(2));
	}
	if (bPortraitShown && PortraitSprite >= 0)
	{
		D.PageSprite(SSXFeDvdTables::PageOf(PortraitSprite), PortraitSprite, PortraitX, PortraitY, PortraitW, PortraitH,
			FeColor(PortraitFade.Cur, 1.f, 1.f, 1.f), SpriteLayer(2));
	}
	if (bTalentNameShown)
	{
		D.Text(EFont::Title, TalentNameX, TalentNameY, 0.9f, White, TalentName, ESSXAlign::Left, true, TextLayer(2));
	}
	{
		const FLinearColor C = FeColor(1.f, 1.f, 0.533f, 0.f);
		const float X = VoiceX0 + VoiceSlide.Cur;
		const float Lw = D.TextBoxWidth(EFont::Menu, VoiceLabel, VoiceScale);
		D.Text(EFont::Menu, X, VoiceY - D.FontHeight(EFont::Menu, VoiceScale) * 0.5f, VoiceScale, C, VoiceLabel, ESSXAlign::Left, false, TextLayer(3));
		D.Text(EFont::Title, X + Lw + 15.f, VoiceY - D.FontHeight(EFont::Title, VoiceScale) * 0.5f, VoiceScale, C, VoiceValue, ESSXAlign::Left, false, TextLayer(3));
	}
	if (bJukeboxInfo)
	{
		const TArray<FString>& Titles = SSXFeTables::StringTable(TEXT("dvd_jukebox.title"));
		const TArray<FString>& Artist = SSXFeTables::StringTable(TEXT("dvd_jukebox.artist"));
		const float H = D.FontHeight(EFont::Menu, InfoScale);
		const FString Rows[2][2] = { { L(3537), Titles.IsValidIndex(Song) ? Titles[Song] : FString() },
			{ L(3538), Artist.IsValidIndex(Song) ? Artist[Song] : FString() } };
		for (int32 R = 0; R < 2; ++R)
		{
			const float Y = (R == 0 ? InfoY0 : InfoY1) - H;
			D.Text(EFont::Menu, InfoX - D.TextBoxWidth(EFont::Menu, Rows[R][0], InfoScale), Y, InfoScale, Black, Rows[R][0], ESSXAlign::Left, false, TextLayer(0));
			D.Text(EFont::Menu, InfoX + 15.f, Y, InfoScale, Black, Rows[R][1], ESSXAlign::Left, false, TextLayer(0));
		}
	}
	if (const FItem* I = Selected())
	{
		const float Cy = I->YAt(D) + ItemH(D, *I) * 0.5f;
		if (Focus == Jukebox || Focus == Talent)
		{
			D.Icon(SprChevron, I->X() - 5.f, Cy, -1.f, -1.f, 0x48, FMath::Lerp(FeColor(1.f, 1.f, 0.f, 0.f), White, Arrow.Cur), SpriteLayer(4));
			float Px = 0.f, Py = 0.f;
			if (Lists[Jukebox].Items.IsValidIndex(PlayIconItem))
			{
				const FItem& P = Lists[Jukebox].Items[PlayIconItem];
				Px = PlayIconX;
				Py = P.YAt(D) + ItemH(D, P) * 0.5f;
			}
			D.Icon(SprPlay, Px, Py, -1.f, -1.f, 0x49, White, SpriteLayer(3));
		}
		else
		{
			const FSSXSprite* S = D.Assets && D.Assets->FeSprites ? D.Assets->FeSprites->Find(SprChevron) : nullptr;
			const float W = S ? S->W : 17.f;
			for (int32 K = 0; K < 5; ++K)
			{
				D.Icon(SprChevron, I->X() - 80.f + K * W * 0.8f, Cy, -1.f, -1.f, 0x40, FMath::Lerp(FeColor(1.f, 1.f, 0.f, 0.f), White, Chevron[K].Cur),
					SpriteLayer(3));
			}
		}
	}
	// FUN_002719C0
	{
		int32 Row = 0;
		for (const FHelpEntry& E : HelpBarTable())
		{
			if (!(LegendMask & E.Mask)) { continue; }
			const float SlotY = LegendY + Row * HelpRowPitch;
			const FString Label = L(E.Loc);
			const float W = D.TextWidth(EFont::Menu, Label, HelpLabelScale);
			const float H = D.FontHeight(EFont::Menu, HelpLabelScale);
			D.Text(EFont::Menu, LegendX - W, SlotY + (HelpSlotH - H) * 0.5f, HelpLabelScale, White, Label, ESSXAlign::Left, false, TextLayer(2));
			D.Sprite(E.Icon, LegendX + HelpIconGap, SlotY + (HelpSlotH - HelpIconSize) * 0.5f, HelpIconSize, HelpIconSize, White, SpriteLayer(2));
			++Row;
		}
	}
}
