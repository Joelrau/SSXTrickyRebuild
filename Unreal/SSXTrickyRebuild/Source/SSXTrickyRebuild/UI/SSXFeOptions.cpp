#include "UI/SSXFeOptions.h"

#include "Kismet/KismetSystemLibrary.h"

const TArray<FIntPoint>& SSXFeOptions::Resolutions()
{
	static const TArray<FIntPoint> All = []()
	{
		TArray<FIntPoint> R;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(R);
		if (R.Num() == 0)
		{
			R = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
		}
		R.RemoveAll([](const FIntPoint& P) { return P.X < 640 || P.Y < 480; });
		R.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		TArray<FIntPoint> Unique;
		for (const FIntPoint& P : R) { Unique.AddUnique(P); }
		return Unique;
	}();
	return All;
}

int32 SSXFeOptions::ResolutionIndex(const FIntPoint& Res)
{
	const TArray<FIntPoint>& All = Resolutions();
	int32 Best = All.Num() - 1;
	int64 BestD = MAX_int64;
	for (int32 I = 0; I < All.Num(); ++I)
	{
		const int64 D = FMath::Abs(int64(All[I].X) * All[I].Y - int64(Res.X) * Res.Y) + FMath::Abs(All[I].X - Res.X);
		if (D < BestD) { BestD = D; Best = I; }
	}
	return FMath::Max(Best, 0);
}

const TArray<SSXFeOptions::FRow>& SSXFeOptions::PageRows(int32 Page)
{
	auto List = [](int32 Id, int32 Loc, TArray<FValue> V) { FRow R; R.Id = Id; R.Loc = Loc; R.Kind = EKind::List; R.Values = MoveTemp(V); return R; };
	auto Bar = [](int32 Id, int32 Loc, float W, float Step) { FRow R; R.Id = Id; R.Loc = Loc; R.Kind = EKind::Bar; R.BarW = W; R.BarStep = Step; return R; };
	auto Done = []() { FRow R; R.Id = DoneId; R.Loc = 3533; R.Kind = EKind::Done; return R; };
	auto Item = [](int32 Id, int32 Loc) { FRow R; R.Id = Id; R.Loc = Loc; R.Kind = EKind::Item; return R; };
	// FUN_0029FB70
	constexpr float VolStep = 1.f / 11.f;
	// FUN_0029FB70
	static const TArray<FRow> Sound = {
		List(0, 226, { { 8, 206 }, { 9, 228 }, { 10, 229 }, { 11, 184 } }),
		List(1, 217, { { 0xC, 230 }, { 0xD, 212 }, { 0xE, 209 }, { 0xF, 224 }, { 0x10, 208 }, { 0x11, 211 } }),
		Bar(2, 200, 150.f, VolStep), Bar(3, 202, 150.f, VolStep), Bar(4, 130, 150.f, VolStep),
		Bar(5, 207, 150.f, VolStep), Bar(6, 223, 150.f, VolStep),
		List(7, 123, { { 0x12, 215 }, { 0x13, 213 } }),
		Done(),
	};
	// FUN_002A36E8
	static const TArray<FRow> Controller = {
		List(0, 235, { { 4, 213 }, { 5, 215 } }), List(1, 236, { { 4, 213 }, { 5, 215 } }),
		List(2, 178, { { 6, 183 }, { 7, 218 } }), List(3, 179, { { 6, 183 }, { 7, 218 } }),
		Done(),
	};
	// FUN_002A53D0
	static const TArray<FRow> Game = {
		List(0, 227, { { 7, 188 }, { 8, 205 } }),
		List(1, 237, { { 9, 213 }, { 10, 3552 }, { 11, 3553 } }),
		List(2, 124, { { 0xC, 215 }, { 0xD, 213 } }),
		List(3, 220, { { 0xE, 210 }, { 0xF, 120 } }),
		List(4, 201, { { 0x10, -1, TEXT("2") }, { 0x11, -1, TEXT("3") }, { 0x12, -1, TEXT("4") }, { 0x13, -1, TEXT("5") }, { 0x14, -1, TEXT("6") } }),
		Bar(5, 128, 100.f, 0.02f),
		Done(),
	};
	// FUN_002A7FD8
	static const TArray<FRow> SaveLoad = { Item(0, 222), Item(1, 198), Item(2, 199) };
	static const TArray<FRow> Video = [&]()
	{
		FRow Look = List(0, -1, { { 0, -1, TEXT("CLASSIC") }, { 1, -1, TEXT("REMASTERED") } });
		Look.Text = TEXT("LOOK");
		FRow Quality = List(1, -1, { { 0, -1, TEXT("LOW") }, { 1, -1, TEXT("MEDIUM") }, { 2, -1, TEXT("HIGH") }, { 3, -1, TEXT("EPIC") } });
		Quality.Text = TEXT("QUALITY");
		static TArray<FString> ResText;
		for (const FIntPoint& R : Resolutions()) { ResText.Add(FString::Printf(TEXT("%dx%d"), R.X, R.Y)); }
		TArray<FValue> ResValues;
		for (int32 I = 0; I < ResText.Num(); ++I) { ResValues.Add({ I, -1, *ResText[I] }); }
		FRow Res = List(2, -1, MoveTemp(ResValues));
		Res.Text = TEXT("RESOLUTION");
		FRow Mode = List(3, -1, { { 0, -1, TEXT("FULLSCREEN") }, { 1, -1, TEXT("BORDERLESS") }, { 2, -1, TEXT("WINDOWED") } });
		Mode.Text = TEXT("DISPLAY");
		return TArray<FRow>{ Look, Quality, Res, Mode, Done() };
	}();
	static const TArray<FRow> None;
	switch (Page)
	{
	case 0: return Sound;
	case 1: return Controller;
	case 2: return Game;
	case 3: return SaveLoad;
	case VideoPage: return Video;
	default: return None;
	}
}

int32 SSXFeOptions::PageTitleLoc(int32 Page)
{
	static const int32 Locs[NumPages] = { 3608, 176, 186, 204, 180, -1 };
	return Page >= 0 && Page < NumPages ? Locs[Page] : -1;
}

const TCHAR* SSXFeOptions::PageTitleText(int32 Page)
{
	return Page == VideoPage ? TEXT("VIDEO") : nullptr;
}

const TCHAR* SSXFeOptions::HelpText(int32 Page, int32 ItemId)
{
	if (Page == -1 && ItemId == VideoPage) { return TEXT("Change the PC display settings."); }
	if (Page == VideoPage && ItemId == 0) { return TEXT("Classic: the original look. Remastered: modern lighting and materials."); }
	if (Page == VideoPage && ItemId == 1) { return TEXT("Detail of the Remastered look. Low has no global illumination."); }
	if (Page == VideoPage && ItemId == 2) { return TEXT("Screen resolution, applied with DONE. Borderless always uses the desktop resolution."); }
	if (Page == VideoPage && ItemId == 3) { return TEXT("Fullscreen, borderless window over the desktop, or a window. Applied with DONE."); }
	return nullptr;
}

int32 SSXFeOptions::HelpLoc(int32 Page, int32 ItemId)
{
	if (ItemId == DoneId)
	{
		return 3534;
	}
	static const TArray<int32> Top = { 3287, 3297, 3302, 3310, 3314 };                                  // 0x3481D0
	static const TArray<int32> Snd = { 3288, 3289, 3290, 3291, 3293, 3294, 3295, 3296, 3288, 3288, 3288, 3288,
		3289, 3289, 3289, 3289, 3289, 3289, 3296, 3296 };                                                    // 0x3481E8
	static const TArray<int32> Ctl = { 3298, 3299, 3300, 3301, 3298, 3298, 3300, 3300 };                // 0x348238
	static const TArray<int32> Gam = { 3303, 3304, 3305, 3306, 3307, 3308, 3309, 3303, 3303, 3304, 3304, 3304,
		3305, 3305, 3306, 3306, 3307, 3307, 3307, 3307, 3307 };                                              // 0x3483F8
	static const TArray<int32> Mem = { 3311, 3312, 3313 };                                              // 0x348450
	const TArray<int32>* T = nullptr;
	switch (Page)
	{
	case -1: T = &Top; break;
	case 0: T = &Snd; break;
	case 1: T = &Ctl; break;
	case 2: T = &Gam; break;
	case 3: T = &Mem; break;
	default: return -1;   // FUN_002B0FB8
	}
	return T->IsValidIndex(ItemId) ? (*T)[ItemId] : -1;
}

float SSXFeOptions::MenuTopY(int32 Page, int32 Rows, float CellH)
{
	if (Page == 1) { return CtrlMenuY; }
	if (Page == 4) { return CreditsY; }
	return (TopGroupBoxH - 70.f) + ((450.f - BarEdge) - Rows * CellH) * 0.5f;
}

const TArray<SSXFeOptions::FCtrlIcon>& SSXFeOptions::CtrlIcons()
{
	static const TArray<FCtrlIcon> T = { { 0.f, 0.f, 61, 256.f, 177.f }, { -25.f, 122.f, 38, 48.f, 38.f }, { -35.f, 80.f, 39, 38.f, 48.f } };
	return T;
}

const TArray<SSXFeOptions::FCtrlLabel>& SSXFeOptions::CtrlLabels(int32 Group)
{
	static const TArray<FCtrlLabel> G0 = {   // 0x3482C8
		{ 210.f, 24.f, 0x0, 3554 }, { -10.f, 10.f, 0x8, 3196 }, { -10.f, 26.f, 0x8, 3197 }, { -10.f, 48.f, 0x8, 3198 },
		{ -10.f, 64.f, 0x8, 3199 }, { -45.f, 91.f, 0x8, 3208 }, { -36.f, 127.f, 0x8, 3209 } };
	static const TArray<FCtrlLabel> G1 = {   // 0x348338
		{ 210.f, 24.f, 0x0, 3214 }, { -10.f, 10.f, 0x8, 3196 }, { -10.f, 26.f, 0x8, 3197 }, { -10.f, 48.f, 0x8, 3198 },
		{ -10.f, 64.f, 0x8, 3199 }, { -45.f, 85.f, 0x8, 3200 }, { -45.f, 101.f, 0x8, 3201 }, { -36.f, 120.f, 0x8, 3202 },
		{ -36.f, 136.f, 0x8, 3203 } };
	static const TArray<FCtrlLabel> G2 = {   // 0x348258
		{ 118.f, 125.f, 0x0, 3212 }, { 75.f, 145.f, 0x20, 3211 }, { 40.f, -20.f, 0x8, 3213 }, { 160.f, -20.f, 0x0, 3213 },
		{ 210.f, 64.f, 0x0, 3216 }, { 210.f, 88.f, 0x0, 3217 }, { 210.f, 107.f, 0x0, 3210 } };
	static const TArray<FCtrlLabel> None;
	return Group == 0 ? G0 : (Group == 1 ? G1 : (Group == 2 ? G2 : None));
}

int32 SSXFeRiderProfile::PortraitSprite(int32 Page)
{
	static const int32 T[5] = { 0x7C, 0x7D, 0x7D, 0x7E, 0x7F };
	return Page >= 0 && Page < 5 ? T[Page] : 0x7C;
}

const TCHAR* SSXFeRiderProfile::PortraitFolder(int32 Char)
{
	// 0x28BF08 / 0x33F3B8
	static const TCHAR* T[12] = { TEXT("RP_EDDI"), TEXT("RP_KAOR"), TEXT("RP_LUTH"), TEXT("RP_MAC"), TEXT("RP_MOBY"), TEXT("RP_ZOE"),
		TEXT("RP_JP"), TEXT("RP_ELIS"), TEXT("RP_PSYM"), TEXT("RP_SEEI"), TEXT("RP_BROD"), TEXT("RP_MARI") };
	return T[FMath::Clamp(Char, 0, 11)];
}

int32 SSXFeRiderProfile::VenueNameLoc(int32 Track)
{
	// FUN_0016B650 / 0x37EBF0
	static const int32 T[12] = { 3613, 3688, 3576, 3646, 3645, 3573, 3685, 3692, 3689, 3685, 3610, 3693 };
	return Track >= 0 && Track < 12 ? T[Track] : 3613;
}

FName SSXFeRiderProfile::PortraitPage(int32 Char, int32 Sprite)
{
	// FUN_001F9660 / 0x39A550
	return FName(*FString::Printf(TEXT("%s/rpb%d"), PortraitFolder(Char), FMath::Clamp(Sprite - 0x7C, 0, 3) + 1));
}

int32 SSXFeRiderProfile::BioValueLoc(int32 Char, int32 Row)
{
	return 0xBAC + 15 * FMath::Clamp(Char, 0, 11) + Row;
}

void SSXFeRiderProfile::BackStoryRange(int32 Char, int32& OutFirst, int32& OutLast)
{
	static const int32 T[12][2] = { { 0x259, 0x267 }, { 0x268, 0x27E }, { 0x27F, 0x291 }, { 0x292, 0x2A8 }, { 0x2A9, 700 },
		{ 0x2BD, 0x2D4 }, { 0x2D5, 0x2E9 }, { 0x2EA, 0x2FF }, { 0x300, 0x313 }, { 0x314, 0x322 }, { 0x323, 0x33C }, { 0x33D, 0x350 } };
	const int32 C = FMath::Clamp(Char, 0, 11);
	OutFirst = T[C][0] + 1;
	OutLast = T[C][1];
}

void SSXFeRiderProfile::InterviewRange(int32 Char, int32& OutFirst, int32& OutLast)
{
	static const int32 T[12][2] = { { 849, 931 }, { 938, 1079 }, { 1092, 1185 }, { 1194, 1295 }, { 1298, 1393 }, { 1400, 1498 },
		{ 1505, 1606 }, { 1610, 1709 }, { 1717, 1850 }, { 1857, 1939 }, { 1945, 2050 }, { 2058, 2137 } };
	const int32 C = FMath::Clamp(Char, 0, 11);
	OutFirst = T[C][0] + 1;
	OutLast = T[C][1];
}

void FSSXFeScrollText::Reset(float BoxH)
{
	Cur = First;
	const int32 N = FMath::Max(0, int32(BoxH / Pitch()));
	Slots.Init(-1, N);
}

void FSSXFeScrollText::Tick()
{
	const int32 N = Slots.Num();
	Offset = Dir == 0 ? Offset + Speed : Offset - Speed;
	const float P = Pitch();
	if (P < FMath::Abs(Offset))
	{
		// FUN_002B1DE8 / FUN_002B1E58
		if (Dir == 0)
		{
			for (int32 I = 1; I < N; ++I) { Slots[I - 1] = Slots[I]; }
			if (N > 0) { Slots[N - 1] = -1; }
		}
		else
		{
			for (int32 I = N - 1; I > 0; --I) { Slots[I] = Slots[I - 1]; }
			if (N > 0) { Slots[0] = -1; }
		}
		Offset = Offset > 0.f ? Offset - P : Offset + P;
		// FUN_002B1EE8 / FUN_002B1DA0
		auto Count = [this]() { int32 C = 0; for (int32 V : Slots) { C += V != -1; } return C; };
		if (Count() < N && Cur >= First && Cur <= Last)
		{
			const int32 Loc = Cur;
			Cur = Dir == 0 ? Cur + 1 : Cur - 1;
			if (N > 0 && Slots[N - 1] == -1) { Slots[N - 1] = Loc; }
		}
		if (Count() == 0)
		{
			Cur = Dir == 0 ? First : Last;
			for (int32& V : Slots) { V = -1; }
		}
	}
}

void FSSXFeScrollText::Key(bool bUp)
{
	// FUN_002B1FF0
	const bool bFaster = bUp ? Dir == 0 : Dir == 1;
	const float V = Speed + (bFaster ? 0.1f : -0.1f);
	const float Hi = LineH * 0.35f;
	Speed = V < 0.1f ? 0.1f : (Hi < V ? Hi : V);
}

float FSSXFeScrollText::SlotAlpha(int32 I) const
{
	float A = 1.f;
	if (I == 0) { A = 1.f - FMath::Abs(Offset) / Pitch(); }
	else if (I == Slots.Num() - 1) { A = FMath::Abs(Offset) / LineH; }
	A = A < 0.f ? 0.f : (1.f <= A ? 1.f : A);
	return Dir == 1 ? 1.f - A : A;
}
