#include "UI/SSXTrackLoad.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFrontEndData.h"

using namespace SSXFe;

namespace
{
	float Hf(uint32 Bits) { float F; FMemory::Memcpy(&F, &Bits, sizeof(F)); return F; }

	constexpr int32 Ly(int32 L) { return L * 10; }
	constexpr int32 LyText(int32 L) { return L * 10 + 1; }

	FLinearColor WithA(FLinearColor C, float A) { C.A *= A; return C; }
}

void FSSXLoadSequencer::Reset()
{
	// 0x1F1258
	*this = FSSXLoadSequencer();
}

void FSSXLoadSequencer::Add(int32 Id, int32 Step, int32 Speed, int32 Mode, int32 From, int32 To)
{
	// 0x1F11C0
	bDone = false;
	if (Num >= MaxChannels)
	{
		return;
	}
	FChannel& C = Ch[Num];
	C.Id = Id;
	C.Step = Step;
	C.From = From;
	C.To = To;
	C.Speed = float(Speed);
	C.Mode = Mode;
	MaxStep = FMath::Max(MaxStep, Step);
	++Num;
}

bool FSSXLoadSequencer::StepChannel(FChannel& C)
{
	// 0x1F0E80
	int32 Inc = 0;
	++C.Frames;
	if (C.Speed > 1.f)
	{
		Inc = (C.Frames % int32(C.Speed)) == 0 ? 1 : 0;
	}
	else if (C.Speed > 0.f)
	{
		Inc = int32(1.f / C.Speed);
	}
	if (uint32(C.Mode - 2) < 2u)
	{
		Inc = 1;
	}
	C.Value += FMath::Max(Inc, 0);
	return C.To <= C.Value;
}

float FSSXLoadSequencer::Eval(const FChannel& C)
{
	// 0x1F0F48
	switch (C.Mode)
	{
	case 0: return float(C.From + C.Value);
	case 1: return float(C.From - C.Value);
	case 2:
	{
		// FUN_00250D60
		const float Hi = float(C.To) / 1000.f;
		const float Mid = (Hi + float(C.From) / 1000.f) * 0.5f;
		if (C.Value == 0)
		{
			return 0.f;
		}
		return Mid + FMath::Sin((float(C.Value) / C.Speed) * 6.28318f) * (Hi - Mid);
	}
	case 3:
	{
		const float A = float(C.From) / 1000.f;
		const float T = C.Speed < float(C.Value) ? 1.f : float(C.Value) / C.Speed;
		return A + (float(C.To) / 1000.f - A) * T;
	}
	default: return 0.f;
	}
}

void FSSXLoadSequencer::Tick()
{
	// 0x1F12D0
	for (int32 I = 0; I < Num; ++I)
	{
		FChannel& C = Ch[I];
		if (uint32(C.Mode - 2) < 2u)
		{
			if (C.Step <= Cur)
			{
				StepChannel(C);
			}
		}
		else if (C.Step == Cur)
		{
			if (StepChannel(C))
			{
				--Active;
			}
		}
	}
	if (Active == 0 && !bDone)
	{
		++Cur;
		int32 N = 0;
		for (int32 I = 0; I < MaxChannels; ++I)
		{
			if (Ch[I].Step == Cur && uint32(Ch[I].Mode - 2) > 1u)
			{
				Active = ++N;
			}
		}
		if (N == 0 && MaxStep < Cur)
		{
			bDone = true;
		}
	}
}

int32 FSSXLoadSequencer::Count(int32 Id) const
{
	for (int32 I = 0; I < MaxChannels; ++I)
	{
		if (Ch[I].Id == Id) { return Ch[I].Value; }
	}
	return 0;
}

float FSSXLoadSequencer::Value(int32 Id) const
{
	for (int32 I = 0; I < MaxChannels; ++I)
	{
		if (Ch[I].Id == Id) { return Eval(Ch[I]); }
	}
	return 0.f;
}

namespace SSXTrackLoad
{
	EKind KindFor(const FSSXFeLaunch& L)
	{
		if (L.GameMode == 6) { return EKind::Tutorial; }
		return L.Players == 2 ? EKind::TwoP : EKind::OneP;
	}

	FString ModePicture(int32 Mode)
	{
		// FUN_002459B0 / 0x3A4CD0
		switch (Mode)
		{
		case 1: return TEXT("ldmodefreeride");
		case 2: case 4: return TEXT("ldmoderace");
		case 3: case 5: return TEXT("ldmodeshowoff");
		case 7: return TEXT("ldmodetimec");
		default: return FString();
		}
	}

	FString TrackPicture(int32 Track)
	{
		// FUN_00245A48 / 0x3A4D40
		static const TCHAR* const N[12] = { TEXT("gari"), TEXT("snow"), TEXT("elys"), TEXT("mesa"), TEXT("merq"), TEXT("aloh"),
			TEXT("pipe"), TEXT("untr"), TEXT("toky"), nullptr, nullptr, TEXT("alas") };
		return Track >= 0 && Track < 12 && N[Track] ? FString(TEXT("ldtrack")) + N[Track] : FString();
	}

	FString RiderPicture(int32 Char)
	{
		// FUN_00245B68 / 0x3A4E10
		static const TCHAR* const N[12] = { TEXT("eddie"), TEXT("kaori"), TEXT("luther"), TEXT("mac"), TEXT("moby"), TEXT("zoe"),
			TEXT("jp"), TEXT("elise"), TEXT("psymon"), TEXT("seeiah"), TEXT("brodi"), TEXT("marisol") };
		return Char >= 0 && Char < 12 ? FString(TEXT("ldrider")) + N[Char] : FString();
	}

	int32 ModeTitleLoc(int32 Mode)
	{
		switch (Mode)
		{
		case 1: case 6: return 0xE2B;
		case 2: case 3: case 7: return 0xE71;
		case 8: case 9: return -1;
		default: return 0xE2F;
		}
	}

	int32 SubTitleLoc(int32 Mode)
	{
		switch (Mode)
		{
		case 2: case 4: return 0xE2C;
		case 3: case 5: return 0xE2D;
		case 6: return 0xE2E;
		case 7: return 0xE72;
		default: return 0xE2A;
		}
	}

	int32 StatLoc(int32 K, int32 Track)
	{
		// FUN_0016B330
		static const int32 T[4][10] = {
			{ 0xEF, 0x20C, 0x2B, 0x141, 0x13B, 0xD, 0x1F1, 0x241, 0x224, 0xC96 },
			{ 0xF2, 0x20F, 0x2E, 0x144, 0x13E, 0x10, 500, 0x244, 0x227, 0xC98 },
			{ 0xF3, 0x210, 0x2F, 0x145, 0x13F, 0x11, 0x1F5, 0x245, 0x228, 0xC97 },
			{ 0xF1, 0x20E, 0x2D, 0x143, 0x13D, 0xF, 499, 0x243, 0x226, 0xC94 },
		};
		static const int32 Default[4] = { 0x117, 0x119, 0x11A, 0x118 };
		K = FMath::Clamp(K, 0, 3);
		if (Track >= 0 && Track <= 8) { return T[K][Track]; }
		if (Track == 9) { return T[K][6]; }
		if (Track == 11) { return T[K][9]; }
		return Default[K];
	}

	FVector2f MapMarker(int32 Track)
	{
		// 0x37EC20 / 0x37EC50
		static const float X[12] = { 0.092f, 0.92f, 0.47f, 0.12f, 0.22f, 0.035f, 0.46f, 0.08f, 0.92f, 0.46f, 0.05f, 0.08f };
		static const float Y[12] = { 0.25f, 0.38f, 0.29f, 0.38f, 0.34f, 0.37f, 0.29f, 0.2f, 0.38f, 0.29f, 0.05f, 0.2f };
		return Track >= 0 && Track < 12 ? FVector2f(X[Track], Y[Track]) : FVector2f(0.05f, 0.05f);
	}

	int32 MedalLoc(int32 Medal)
	{
		switch (Medal)
		{
		case 0: return 0x12F;
		case 1: return 0x135;
		case 2: return 300;
		default: return 0x132;
		}
	}

	FReturnPick PickAfterRace(bool bRankVideo, bool bEAA0, bool bQuit, int32 Mode, bool bRankUp, bool bNextVenue, int32 Humans)
	{
		// 0x180E00 / FUN_00164288
		FReturnPick P;
		const bool bOneP = Humans == 1;
		if (bRankVideo)
		{
			P.bRankVideo = true;
			if (bQuit || Mode == 6 || Mode == 1) { P.Kind = EKind::FromQuit; }
			else if (bRankUp) { P.Kind = EKind::RiderRanking; }
			else { P.Kind = bOneP ? EKind::FEOneP : EKind::FETwoP; }
			return P;
		}
		if (bEAA0) { P.Kind = EKind::FromQuit; P.bHintAnyRank = true; }
		else if (bQuit || Mode == 6) { P.Kind = EKind::FromQuit; }
		else if (bRankUp) { P.Kind = EKind::RiderRanking; }
		else if (bNextVenue) { P.Kind = bOneP ? EKind::OneP : EKind::TwoP; }
		else if (Mode == 1) { P.Kind = EKind::FromQuit; }
		else { P.Kind = bOneP ? EKind::FEOneP : EKind::FETwoP; }
		return P;
	}

	int32 PickHint(bool bAnyRank, int32 RankPoints, TFunctionRef<uint32()> Rand)
	{
		// FUN_0024D1A0 / FUN_00164730
		const bool bLowRank = RankPoints >= 0 && RankPoints < 20;
		if (!bAnyRank && bLowRank)
		{
			for (;;)
			{
				const int32 N = int32(Rand() % uint32(NumHints));
				if (uint32(N - 3) < 2u || N == 6 || N == 15) { return N; }
			}
		}
		for (;;)
		{
			const int32 N = int32(Rand() % uint32(NumHints));
			if (N != 17) { return N; }
		}
	}

	TArray<FString> WrapBox(const FSSXFeDrawList& D, SSXFe::EFont Font, const FString& Str, float Scale, float Width)
	{
		TArray<FString> Out;
		TArray<FString> Paras;
		Str.ParseIntoArray(Paras, TEXT("\\"), false);
		const float Limit = FMath::TruncToFloat(Width);
		for (const FString& Para : Paras)
		{
			TArray<FString> Words;
			Para.ParseIntoArray(Words, TEXT(" "), true);
			FString Line;
			for (const FString& W : Words)
			{
				const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
				if (!Line.IsEmpty() && FMath::TruncToFloat(D.TextInkWidth(Font, Try, Scale)) > Limit)
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

	int32 FeSummaryLoc(int32 Mode, bool bTwoP)
	{
		switch (Mode)
		{
		case 1: return 0xE7D;
		case 2: case 3: case 7: return 0xE7E;
		case 4: case 5: return bTwoP ? -1 : 0xE7F;
		default: return -1;
		}
	}

	FString StatTime(float Seconds, const FSSXLocStrings* Loc)
	{
		// FUN_00243B88
		const int32 Cs = int32(Seconds * 100.f);
		if (Cs == 50000) { return Loc ? Loc->ByIndex(0x15D) : FString(); }
		if (Cs < 0) { return Loc ? Loc->ByIndex(0x18C) : FString(); }
		if (Cs < 360000) { return FString::Printf(TEXT("%d:%02d.%02d"), (Cs / 100) / 60, (Cs / 100) % 60, Cs % 100); }
		return TEXT("59:59.99");
	}

	int32 RankBoardTypeLoc(int32 Type)
	{
		switch (Type)
		{
		case 0: return 0x81;
		case 1: return 0xB9;
		case 2: return 0x79;
		default: return -1;
		}
	}

	int32 RankUpBoard(const TArray<FSSXUnlock>& Unlocks)
	{
		for (const FSSXUnlock& U : Unlocks)
		{
			if (U.Type == SSXProg::UBoard) { return U.Value; }
		}
		return 0;
	}

	FString RankVideoName(int32 Char)
	{
		const TCHAR* Short = SSXFeTables::RiderShort(FMath::Clamp(Char, 0, SSXFeTables::NumRiders - 1));
		return FString(TEXT("RR_")) + FString(Short ? Short : TEXT("")).ToUpper();
	}

	FString DataDir() { return FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("loading")); }
	FString JsonPath() { return FPaths::Combine(DataDir(), TEXT("loading_screen.json")); }
}

bool FSSXTrackLoadAssets::Load(const FSSXFeAssets& Fe, TFunctionRef<UTexture2D*(const FString&)> LoadPng)
{
	auto Rescaled = [](const TSharedPtr<FSSXBitmapFont>& Src, float Sx, float Sy) -> TSharedPtr<FSSXBitmapFont>
	{
		if (!Src.IsValid()) { return nullptr; }
		TSharedPtr<FSSXBitmapFont> F = MakeShared<FSSXBitmapFont>(*Src);
		F->ScaleX = Sx;
		F->ScaleY = Sy;
		return F;
	};
	Assets = FSSXFeAssets();
	Assets.Title = Rescaled(Fe.Title, 1.25f, 1.f);
	Assets.Menu = Rescaled(Fe.Menu, 1.25f, 1.f);
	Assets.Small = Rescaled(Fe.Title, Hf(0x3FA28F5C), Hf(0x3FAB851F));
	Assets.White = Fe.White;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	// 0x249948
	bLoaded = Sprites.LoadJson(SSXTrackLoad::JsonPath())
		&& FFileHelper::LoadFileToString(Text, *SSXTrackLoad::JsonPath())
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid();
	Assets.FeSprites = &Sprites;
	if (!bLoaded)
	{
		return false;
	}
	for (const TCHAR* Field : { TEXT("pages"), TEXT("pictures") })
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (Root->TryGetObjectField(Field, Obj) && Obj && Obj->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Obj)->Values)
			{
				if (UTexture2D* T = LoadPng(FPaths::Combine(SSXTrackLoad::DataDir(), P.Value->AsString())))
				{
					Assets.Pages.Add(FName(*P.Key), T);
				}
			}
		}
	}
	return true;
}

FSSXTrackLoadPage FSSXTrackLoadPage::Build(const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc)
{
	using namespace SSXTrackLoad;
	FSSXTrackLoadPage P;
	auto Lc = [Loc](int32 Id) { return Loc && Id >= 0 ? Loc->ByIndex(Id) : FString(); };
	P.Mode = L.GameMode;
	P.Track = L.Track;
	// FUN_00164330 / FUN_00166580
	P.Char = FMath::Clamp(L.Slots[0].Char, 0, SSXFeTables::NumRiders - 1);
	P.Pictures[0] = ModePicture(P.Mode);
	P.Pictures[1] = TrackPicture(P.Track);
	P.Pictures[2] = RiderPicture(P.Char);
	P.Loading = Lc(373);
	const int32 TitleLoc = ModeTitleLoc(P.Mode);
	if (TitleLoc < 0)
	{
		return P;   // FUN_00246700
	}
	P.bTexts = true;
	P.ModeTitle = Lc(TitleLoc);
	P.SubTitle = Lc(SubTitleLoc(P.Mode));
	P.VenueName = Lc(SSXFeRiderProfile::VenueNameLoc(P.Track));
	static const int32 LabelLoc[8] = { 0x11D, 0x11F, 0x124, 0x11E, 0x121, 0x120, 0x123, 0x11C };
	for (int32 I = 0; I < 8; ++I) { P.Labels[I] = Lc(LabelLoc[I]); }
	for (int32 K = 0; K < 4; ++K) { P.Values[K] = Lc(StatLoc(K, P.Track)); }
	P.Values[4] = SSXFeTables::RiderName(P.Char);
	if (Profile)
	{
		const FSSXCareer& Car = Profile->Career[P.Char];
		// FUN_00164808 / FUN_00164730
		P.Values[5] = Lc(23 + SSXFeTables::RankFromPoints(Car.RankPoints()));
		// FUN_00164060 / FUN_00163F18
		int32 N = 0;
		while (N < SSXProg::TrickBookChapters && Car.ChapterDone(N)) { ++N; }
		P.Values[6] = N == 6 ? Lc(0xC9E) : FString::Printf(TEXT("%d"), N + 1);
		P.Medal = Lc(MedalLoc(Profile->VenueMedal(P.Mode, P.Track)));
	}
	else
	{
		P.Values[5] = Lc(23 + 9);
		P.Values[6] = TEXT("1");
		P.Medal = Lc(MedalLoc(3));
	}
	P.WcTitle[0] = Lc(0xE73);
	P.WcTitle[1] = Lc(0xE74);
	return P;
}

FSSXTrackLoadPage FSSXTrackLoadPage::BuildTutorial(const FSSXProfile* Profile, const FSSXLocStrings* Loc)
{
	FSSXTrackLoadPage P;
	auto Lc = [Loc](int32 Id) { return Loc && Id >= 0 ? Loc->ByIndex(Id) : FString(); };
	P.Mode = 6;
	P.Loading = Lc(373);
	P.TutorialTitle = Lc(0xE80);
	// 0x24E3A8
	P.bTutorialPro = Profile && Profile->Settings.Controller[0] != 0;
	auto Add = [&P, &Lc](int32 Id, float X, float Y, float S, int32 Align)
	{
		FSSXTrackLoadPage::FTutorialText T;
		T.Loc = Id;
		T.Str = Lc(Id);
		T.X = X; T.Y = Y; T.Scale = S; T.Align = Align;
		P.TutorialTexts.Add(T);
	};
	constexpr int32 Left = 0, Right = 1, Centre = 2;
	const float S85 = 0.85f;
	Add(P.bTutorialPro ? 0xD29 : 0xD28, 320.f, 374.f, 1.f, Centre);
	if (!P.bTutorialPro)
	{
		Add(0xC7C, 160.f, 153.f, 1.f, Right);
		Add(0xC7D, 160.f, 167.f, 1.f, Right);
		Add(0xC7E, 160.f, 200.f, 1.f, Right);
		Add(0xC7F, 160.f, 214.f, 1.f, Right);
		Add(0xC80, 130.f, 245.f, S85, Right);
		Add(0xC82, 130.f, 295.f, 1.f, Right);
		Add(0xDE2, 470.f, 159.f, 1.f, Left);
	}
	else
	{
		Add(0xC84, 160.f, 153.f, 1.f, Right);
		Add(0xC85, 160.f, 167.f, 1.f, Right);
		Add(0xC86, 160.f, 200.f, 1.f, Right);
		Add(0xC87, 160.f, 214.f, 1.f, Right);
		Add(0xC88, 130.f, 245.f, S85, Right);
		Add(0xC81, 130.f, 259.f, S85, Right);
		Add(0xC89, 130.f, 295.f, 1.f, Right);
		Add(0xC83, 130.f, 309.f, 1.f, Right);
		Add(0xC8E, 470.f, 159.f, 1.f, Left);
	}
	Add(0xC8D, 190.f, 125.f, 1.f, Left);
	Add(0xC8D, 360.f, 125.f, 1.f, Left);
	Add(0xC8B, 310.f, 320.f, 1.f, Right);
	Add(0xC8C, 330.f, 320.f, 1.f, Left);
	Add(0xC90, 470.f, 228.f, 1.f, Left);
	Add(0xC91, 470.f, 253.f, S85, Left);
	Add(0xC8A, 470.f, 290.f, 1.f, Left);
	return P;
}

FSSXTrackLoadPage FSSXTrackLoadPage::BuildQuit(int32 N, const FSSXLocStrings* Loc)
{
	using namespace SSXTrackLoad;
	FSSXTrackLoadPage P;
	auto Lc = [Loc](int32 Id) { return Loc && Id >= 0 ? Loc->ByIndex(Id) : FString(); };
	P.Loading = Lc(373);
	P.Hint = N;
	P.QuitTitle = Lc(QuitTitleLoc);
	if (N >= 0)
	{
		P.HintTitle = Lc(HintTitleLoc + 2 * N);
		P.HintBody = Lc(HintTitleLoc + 2 * N + 1);
	}
	return P;
}

namespace
{
	/** Rank / trick-chapter strings of a rider (FUN_00164808: LOC 23 + rank; FUN_00164060(FUN_00163F18): the leading
	 *  complete trick-book chapters, 6 -> LOC 0xC9E, else "%d" n + 1). */
	void RankAndChapter(const FSSXProfile* Profile, int32 Char, const FSSXLocStrings* Loc, FString& OutRank, FString& OutChapter)
	{
		auto Lc = [Loc](int32 Id) { return Loc && Id >= 0 ? Loc->ByIndex(Id) : FString(); };
		if (!Profile)
		{
			OutRank = Lc(23 + 9);
			OutChapter = TEXT("1");
			return;
		}
		const FSSXCareer& Car = Profile->Career[FMath::Clamp(Char, 0, SSXFeTables::NumRiders - 1)];
		OutRank = Lc(23 + SSXFeTables::RankFromPoints(Car.RankPoints()));
		int32 N = 0;
		while (N < SSXProg::TrickBookChapters && Car.ChapterDone(N)) { ++N; }
		OutChapter = N == 6 ? Lc(0xC9E) : FString::Printf(TEXT("%d"), N + 1);
	}
}

FSSXTrackLoadPage FSSXTrackLoadPage::BuildFe(bool bTwoP, const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc)
{
	using namespace SSXTrackLoad;
	FSSXTrackLoadPage P;
	auto Lc = [Loc](int32 Id) { return Loc && Id >= 0 ? Loc->ByIndex(Id) : FString(); };
	P.Mode = L.GameMode;
	P.Track = L.Track;
	P.Char = FMath::Clamp(L.Slots[0].Char, 0, SSXFeTables::NumRiders - 1);
	P.Loading = Lc(373);
	// FUN_0024A188 / FUN_0024BBD0 / FUN_0016B650
	P.FeTitle = Lc(FeSummaryLoc(P.Mode, bTwoP));
	P.FeSubTitle = Lc(bTwoP && P.Mode == 7 ? 0xE2C : SubTitleLoc(P.Mode));
	P.FeVenue = Lc(SSXFeRiderProfile::VenueNameLoc(P.Track));
	static const int32 LabelLoc[FeRows] = { 0x121, 0x123, 0x120, 0x125, 0x126, 0x127, 0x128, 0x129, 0x12A, 299 };
	for (int32 I = 0; I < FeRows; ++I) { P.FeLabels[I] = Lc(LabelLoc[I]); }
	const int32 NumPlayers = bTwoP ? 2 : 1;
	for (int32 S = 0; S < NumPlayers; ++S)
	{
		// FUN_00166580 / FUN_00164330 / FUN_00166598
		const int32 Char = FMath::Clamp(L.Slots[S].Char, 0, SSXFeTables::NumRiders - 1);
		FString* V = P.FeValues[S];
		V[0] = SSXFeTables::RiderName(Char);
		RankAndChapter(Profile, Char, Loc, V[2], V[1]);
		const FSSXRaceStatBlock B = Profile ? Profile->Session.RaceStats[S] : FSSXRaceStatBlock();
		V[3] = FString::Printf(TEXT("%d"), B.Knockdowns);   // 0x32E66C
		V[4] = FString::Printf(TEXT("%d"), B.Wipeouts);     // 0x32E640
		V[5] = FString::Printf(TEXT("%d"), B.Tricks);       // 0x32E634
		V[6] = FString::Printf(TEXT("%d"), B.Ubers);        // 0x32E638
		V[7] = StatTime(B.AirBest, Loc);                    // 0x32E680
		V[8] = StatTime(B.GrabBest, Loc);                   // 0x32E684
		V[9] = StatTime(B.RailBest, Loc);                   // 0x32E688
		P.FePictures[S] = RiderPicture(Char);
	}
	return P;
}

FSSXTrackLoadPage FSSXTrackLoadPage::BuildRanking(const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc)
{
	using namespace SSXTrackLoad;
	FSSXTrackLoadPage P;
	auto Lc = [Loc](int32 Id) { return Loc && Id >= 0 ? Loc->ByIndex(Id) : FString(); };
	P.Mode = L.GameMode;
	P.Track = L.Track;
	P.Char = FMath::Clamp(L.Slots[0].Char, 0, SSXFeTables::NumRiders - 1);
	P.Loading = Lc(373);
	P.WcTitle[0] = Lc(0xE73);
	P.WcTitle[1] = Lc(0xE74);
	// FUN_0024F520
	P.RankTitle = Lc(0xD2A);
	P.RankBody = Lc(0xD2B);
	P.RankLabels[0] = Lc(0xD2C);
	P.RankLabels[1] = Lc(0xD2D);
	P.RankLabels[2] = Lc(0xD2E);
	FString Chapter;
	RankAndChapter(Profile, P.Char, Loc, P.RankValues[0], Chapter);
	// 0x332148
	const SSXFeTables::FBoard& Board = SSXFeTables::Board(P.Char, Profile ? RankUpBoard(Profile->Session.Unlocks) : 0);
	P.RankValues[1] = Board.Name;
	P.RankValues[2] = Lc(RankBoardTypeLoc(Board.Type));
	P.RankPicture = RiderPicture(P.Char);
	return P;
}

bool FSSXRankVideo::Begin(const SSXTrackLoad::FReturnPick& Pick, int32 Char)
{
	*this = FSSXRankVideo();
	if (!Pick.bRankVideo)
	{
		return false;
	}
	// 0x32EAA4 / 0x1BA330
	Name = SSXTrackLoad::RankVideoName(Char);
	bSkippable = true;
	Phase = EPhase::Playing;
	return true;
}

bool FSSXRankVideo::PressSkip()
{
	if (Phase != EPhase::Playing || !bSkippable)
	{
		return false;
	}
	bSkipped = true;
	return true;
}

bool FSSXRankVideo::Finish()
{
	if (Phase != EPhase::Playing)
	{
		return false;
	}
	Phase = EPhase::Done;
	return true;
}

void FSSXTrackLoader::BeginReturn(SSXTrackLoad::EKind InKind, bool bHintAnyRank, const FSSXFeLaunch& L, const FSSXProfile* Profile,
	const FSSXLocStrings* Loc, TFunctionRef<uint32()> Rand)
{
	// 0x243C78
	using SSXTrackLoad::EKind;
	*this = FSSXTrackLoader();
	Kind = InKind;
	Phase = EPhase::Prepare;
	Seq.Reset();
	Lines.Reset();
	auto Len = [](const FString& S) { return S.Len(); };   // FUN_0017CC48
	if (Kind == EKind::FEOneP)
	{
		// 0x249758 / FUN_0024A188
		Page = FSSXTrackLoadPage::BuildFe(false, L, Profile, Loc);
		Seq.Add(1, 1, 1, 0, 0, Len(Page.FeTitle));
		Seq.Add(2, 2, 1, 0, 0, Len(Page.FeSubTitle));
		Seq.Add(3, 3, 1, 0, 0, Len(Page.FeVenue));
		Seq.Add(4, 4, 0x46, 3, 0, 1000);
		for (int32 R = 0; R < FSSXTrackLoadPage::FeRows; ++R)
		{
			Seq.Add(FeRowId(R), FeRowStep(R), 1, 0, 0, Len(Page.FeValues[0][R]) + (R == FSSXTrackLoadPage::FeRows - 1 ? 0x3C : 8));
		}
		return;
	}
	if (Kind == EKind::FETwoP)
	{
		// 0x24B028 / FUN_0024BBD0
		Page = FSSXTrackLoadPage::BuildFe(true, L, Profile, Loc);
		return;
	}
	if (Kind == EKind::RiderRanking)
	{
		// 0x24EA20 / FUN_0024F520
		Page = FSSXTrackLoadPage::BuildRanking(L, Profile, Loc);
		Seq.Add(1, 1, 2, 0, 0, Len(Page.RankTitle));
		Seq.Add(2, 2, 1, 0, 0, Len(Page.RankBody));
		Seq.Add(3, 3, 2, 0, 0, Len(Page.RankValues[0]) + 4);
		Seq.Add(4, 4, 2, 0, 0, Len(Page.RankValues[1]) + 4);
		Seq.Add(5, 5, 2, 0, 0, Len(Page.RankValues[2]) + 4);
		return;
	}
	if (Kind != EKind::FromQuit)
	{
		Page = FSSXTrackLoadPage::BuildQuit(-1, Loc);
		return;
	}
	// FUN_00166580 / FUN_00164730
	const FSSXCareer* Car = Profile ? Profile->SlotCareer(0) : nullptr;
	const int32 N = SSXTrackLoad::PickHint(bHintAnyRank, Car ? Car->RankPoints() : 0, Rand);
	Page = FSSXTrackLoadPage::BuildQuit(N, Loc);
	// FUN_001F11C0 / FUN_0017CC48
	Seq.Add(1, 1, 2, 0, 0, Page.QuitTitle.Len());
	Seq.Add(2, 2, 1, 0, 0, Page.HintTitle.Len());
	Seq.Add(3, 3, 1, 0, 0, Page.HintBody.Len());
}

void FSSXTrackLoader::Begin(const FSSXFeLaunch& L, const FSSXProfile* Profile, const FSSXLocStrings* Loc)
{
	// 0x243C78 / 0x1F10C0 / 0x245840 / FUN_00246700
	*this = FSSXTrackLoader();
	Kind = SSXTrackLoad::KindFor(L);
	Phase = EPhase::Prepare;
	Page = FSSXTrackLoadPage::Build(L, Profile, Loc);
	Seq.Reset();
	Lines.Reset();
	if (Kind == SSXTrackLoad::EKind::Tutorial)
	{
		// FUN_0024D7F8 / FUN_0024E228
		Page = FSSXTrackLoadPage::BuildTutorial(Profile, Loc);
		return;
	}
	if (Kind != SSXTrackLoad::EKind::OneP)
	{
		// TODO: the c2PTrackLoad (vfunc_08 0x248B18) page and queue are not ported: only the cFlakeSpinnerLoad base (fade, "lo
		return;
	}
	if (!Page.bTexts)
	{
		return;
	}
	auto Len = [](const FString& S) { return S.Len(); };   // FUN_0017CC48
	Seq.Add(1, 1, 2, 0, 0, Len(Page.ModeTitle));
	Seq.Add(2, 1, 0x46, 3, 0, 1000);
	Seq.Add(0x13, 1, 1, 0, 0, 0x82);
	Seq.Add(4, 1, 0x46, 3, 0, 1000);
	Seq.Add(0x11, 1, 2, 0, 0, Len(Page.Medal) + 0x3C);
	Seq.Add(5, 2, 2, 0, 0, Len(Page.VenueName));
	Seq.Add(6, 2, 0x3C, 3, 0, 1000);
	Seq.Add(7, 3, 0x32, 2, 400, 1000);
	Seq.Add(8, 3, 0x46, 3, 0, 1000);
	Seq.Add(9, 3, 2, 0, 0, Len(Page.Values[0]) + 0x14);
	Seq.Add(10, 4, 2, 0, 0, Len(Page.Values[1]) + 0x14);
	Seq.Add(0xB, 5, 2, 0, 0, Len(Page.Values[2]) + 0x14);
	Seq.Add(0xC, 6, 2, 0, 0, Len(Page.Values[3]) + 0x14);
	Seq.Add(0xD, 7, 0x32, 3, 0, 1000);
	Seq.Add(0xE, 7, 2, 0, 0, Len(Page.Values[4]) + 0x14);
	Seq.Add(0xF, 8, 2, 0, 0, Len(Page.Values[5]) + 0x14);
	Seq.Add(0x10, 9, 2, 0, 0, Len(Page.Values[6]) + 0x14);
}

void FSSXTrackLoader::Start()
{
	// 0x17C998 / 0x246F18 / FUN_00214910 / 0x2443B0
	if (Phase != EPhase::Prepare)
	{
		return;
	}
	// 0x248A60 / 0x24E1F0 / FUN_00214910 / 0x24D168
	if (HasPageMusic())
	{
		++MusicStarts;
		bMusicOn = true;
	}
	// 0x2498C8 / 0x24BB60 / 0x24F4E8 / FUN_00214910
	Lines.Add(1, 1, 100, 2, 5000, 70000);
	Lines.Add(2, 1, 0xFA, 2, 5000, 70000);
	Lines.Add(3, 1, 400, 2, 5000, 70000);
	Phase = EPhase::Run;
}

bool FSSXTrackLoader::RequestSkip(bool bLoaded)
{
	if (Phase != EPhase::Run || !bLoaded)
	{
		return false;
	}
	bSkipped = true;
	return true;
}

void FSSXTrackLoader::Update()
{
	// 0x2444C0 / 0x246F88
	Timer += SSXTrackLoad::FrameStep;
	if (!bClosing)
	{
		Fade -= SSXTrackLoad::FrameStep;
		if (Fade < 0.f) { Fade = 0.f; }
	}
	else
	{
		Fade += SSXTrackLoad::CloseStep;
		if (Fade > 1.f) { Fade = 1.f; }
	}
	Lines.Tick();
	// 0x24D390 / 0x24A930 / 0x24C278 / 0x24F8C0
	if (HasPageMusic())
	{
		if (TicksPageQueue())
		{
			Seq.Tick();
		}
		if (MusicStopDue(Progress) && bMusicOn)
		{
			bMusicOn = false;
			++MusicStops;
		}
	}
}

void FSSXTrackLoader::DrawTimeEffects()
{
	// 0x2473D8 / FUN_0019E460 / FUN_00214DD0
	if (Kind == SSXTrackLoad::EKind::FEOneP)
	{
		// 0x24AA70 / FUN_0019E460 / FUN_0019DF90
		for (int32 Row = 0; Row < FSSXTrackLoadPage::FeRows; ++Row)
		{
			if (--BlinkCount < 0)
			{
				BlinkCount = SSXTrackLoad::BlinkReset;
				bBlink = !bBlink;
			}
			RowBlink[Row] = bBlink;
			const int32 N = Seq.Count(FeRowId(Row));
			if (N > 0 && N <= Page.FeValues[0][Row].Len())
			{
				++TypeSounds;
			}
		}
		return;
	}
	if (Kind != SSXTrackLoad::EKind::OneP || !Page.bTexts)
	{
		return;
	}
	auto Call = [this](int32 Row)
	{
		if (--BlinkCount < 0)
		{
			BlinkCount = SSXTrackLoad::BlinkReset;
			bBlink = !bBlink;
		}
		RowBlink[Row] = bBlink;
		const int32 N = Seq.Count(RowIds[Row]);
		if (N > 0 && N <= RowText(Row).Len())
		{
			++TypeSounds;
		}
	};
	if (Page.IsWorldCircuit())
	{
		Call(7);
	}
	for (int32 Row = 0; Row < 7; ++Row)
	{
		Call(Row);
	}
}

void FSSXTrackLoader::Step(bool bLoaded, int32 InProgress)
{
	Progress = FMath::Clamp(InProgress, 0, 10);
	switch (Phase)
	{
	case EPhase::Run:
		if (!bLoaded || !(Seq.bDone || bSkipped))
		{
			Update();
			DrawTimeEffects();
			return;
		}
		Phase = EPhase::Finish;
		[[fallthrough]];
	case EPhase::Finish:
		// 0x17C9D8 / 0x244490
		bClosing = true;
		if (Fade == 1.f)
		{
			Phase = EPhase::Done;
			bMusicOn = false;   // FUN_00214D78
			return;
		}
		Update();
		DrawTimeEffects();
		return;
	default:
		return;
	}
}

void FSSXTrackLoader::Draw(FSSXFeDrawList& D) const
{
	if (Phase != EPhase::Run && Phase != EPhase::Finish)
	{
		return;
	}
	// 0x244B20 / FUN_0019D568
	D.RectScreen(0.f, 0.f, ScreenW, ScreenH, Black, Ly(0));
	if (Kind == SSXTrackLoad::EKind::OneP)
	{
		DrawPage(D);
		if (Page.bTexts)
		{
			DrawTexts(D);
		}
	}
	else if (Kind == SSXTrackLoad::EKind::FromQuit)
	{
		DrawQuitPage(D);
		DrawQuitTexts(D);
	}
	else if (Kind == SSXTrackLoad::EKind::FEOneP)
	{
		DrawQuitPage(D);    // 0x24A988
		DrawFeTexts(D);
	}
	else if (Kind == SSXTrackLoad::EKind::FETwoP)
	{
		DrawFe2Page(D);
		DrawFe2Texts(D);
	}
	else if (Kind == SSXTrackLoad::EKind::RiderRanking)
	{
		DrawRankPage(D);
		DrawRankTexts(D);
	}
	else if (Kind == SSXTrackLoad::EKind::Tutorial)
	{
		DrawTutorialPage(D);
		DrawTutorialTexts(D);
	}
	D.Text(EFont::Menu, 28.f, 430.f, 1.f, White, Page.Loading, ESSXAlign::Left, true, LyText(9));
	if (Fade > 0.f)
	{
		D.Rect(0.f, 0.f, ScreenW, ScreenH, FeColor(Fade, 0.f, 0.f, 0.f), Ly(0x13));
	}
}

void FSSXTrackLoader::DrawPage(FSSXFeDrawList& D) const
{
	const FSSXSpriteTable* Sheet = D.Assets ? D.Assets->FeSprites : nullptr;
	auto Rec = [Sheet](int32 K) { return Sheet ? Sheet->Find(K) : nullptr; };
	auto Spr = [&D, &Rec](int32 K, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C, int32 L)
	{
		// FUN_0014EED0 / FUN_001FC178
		if (const FSSXSprite* S = Rec(K))
		{
			D.Icon(K, X, Y, S->W * Sx, S->H * Sy, Flags, C, L);
		}
	};
	DrawBase(D);
	// FUN_002447A8
	static const float PicY[3] = { 100.f, 199.f, 300.f };
	static const int32 PicCh[3] = { 4, 8, 0xD };
	for (int32 I = 0; I < 3; ++I)
	{
		UTexture* const* T = Page.Pictures[I].IsEmpty() || !D.Assets ? nullptr : D.Assets->Pages.Find(FName(*Page.Pictures[I]));
		if (!T || !*T)
		{
			continue;
		}
		const float A = Seq.Value(PicCh[I]);
		const float X = 23.f, Y = PicY[I];
		D.Image(*T, X + 3.f, Y + 3.f, 254.f, 79.f, FeColor(A, 0.f, 0.f, 0.f), Ly(7));
		D.Image(*T, X, Y, 254.f, 79.f, FeColor(A, 1.f, 1.f, 1.f), Ly(8));
		if (I == 0 && !Page.SubTitle.IsEmpty())
		{
			const float W = FMath::TruncToFloat(D.TextInkWidth(EFont::Small, Page.SubTitle, Hf(0x3F19999A)));
			D.Text(EFont::Small, (X + 240.f) - W, Y + 52.f + 5.f, Hf(0x3F19999A), FeColor(A, 0.f, 0.f, 0.f), Page.SubTitle,
				ESSXAlign::Left, false, LyText(10));
		}
	}
	const FSSXSprite* R1 = Rec(1);
	const float PlateW = R1 ? R1->W : 159.f;
	const float PlateSy = R1 && R1->H > 0.f ? 80.f / R1->H : 1.f;
	Spr(1, 320.f, 0.f, Hf(0x3FE66666), PlateSy, 0, FeColor(Seq.Value(6), Hf(0x3EE66666), Hf(0x3EE66666), Hf(0x3EE66666)), Ly(8));
	if (Page.Track != 7)
	{
		const FVector2f M = SSXTrackLoad::MapMarker(Page.Track);
		const float S = Seq.Value(7);
		Spr(2, PlateW * 1.8f * M.X + 320.f, (R1 ? R1->H : 63.f) * PlateSy * M.Y + 0.f, S, S, 0x60, White, Ly(9));
	}
	if (Page.IsWorldCircuit())
	{
		Spr(0, 10.f, 0.f, Hf(0x3F666666), Hf(0x3F666666), 0, FeColor(Seq.Value(2), 1.f, 1.f, 1.f), Ly(8));
	}
	DrawProgress(D);
}

void FSSXTrackLoader::DrawQuitPage(FSSXFeDrawList& D) const
{
	// 0x24D3E8 / FUN_00244FC8 / FUN_002449F8 / FUN_00244540
	DrawBase(D);
	const FSSXSpriteTable* Sheet = D.Assets ? D.Assets->FeSprites : nullptr;
	if (const FSSXSprite* S = Sheet ? Sheet->Find(16) : nullptr)
	{
		D.Icon(16, 28.f, 235.f, S->W * Hf(0x3F933333), S->H * Hf(0x3F933333), 0x40, White, Ly(7));
	}
	DrawProgress(D);
}

void FSSXTrackLoader::DrawQuitTexts(FSSXFeDrawList& D) const
{
	// 0x24D4D0
	const int32 L = LyText(0xC);
	const float S = Hf(0x3F99999A);
	// FUN_0019D568
	D.Text(EFont::Title, 30.f, 27.f, S, FeColor(1.f, Hf(0x3F73F7CF), Hf(0x3F3A9FBE), Hf(0x3DD91687)), Page.QuitTitle,
		ESSXAlign::Left, true, L);
	// FUN_0019D0C0
	const float W = FMath::TruncToFloat(D.TextInkWidth(EFont::Menu, Page.HintTitle, S));
	const float X = (620.f - W < 320.f) ? 620.f - W : 320.f;
	D.Text(EFont::Menu, X, 127.f, S, Black, Page.HintTitle, ESSXAlign::Left, false, L);
	// FUN_00196070 / FUN_001961F0 / FUN_001957D8
	const float Bs = Hf(0x3F88F5C3);
	const TArray<FString> BodyLines = SSXTrackLoad::WrapBox(D, EFont::Menu, Page.HintBody, Bs, 280.f);
	const float Pitch = (D.FontHeight(EFont::Menu, 1.f) + 3.f) * Bs;
	for (int32 I = 0; I < BodyLines.Num(); ++I)
	{
		D.Text(EFont::Menu, 320.f, 205.f + Pitch * I, Bs, Black, BodyLines[I], ESSXAlign::Left, false, L);
	}
}

void FSSXTrackLoader::DrawBase(FSSXFeDrawList& D) const
{
	const FSSXSpriteTable* Sheet = D.Assets ? D.Assets->FeSprites : nullptr;
	auto Rec = [Sheet](int32 K) { return Sheet ? Sheet->Find(K) : nullptr; };
	auto Spr = [&D, &Rec](int32 K, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C, int32 L)
	{
		if (const FSSXSprite* S = Rec(K))
		{
			D.Icon(K, X, Y, S->W * Sx, S->H * Sy, Flags, C, L);
		}
	};
	auto Tiled = [&D](int32 K, float X, float Y, float W, float H, float Ru, float Rv, bool bFlipV, int32 L)
	{
		// FUN_001FC728
		const int32 First = D.Cmds.Num();
		D.SpriteTiled(K, X, Y, W, H, Ru, Rv, White, L);
		if (bFlipV)
		{
			for (int32 I = First; I < D.Cmds.Num(); ++I) { Swap(D.Cmds[I].UV0.Y, D.Cmds[I].UV1.Y); }
		}
	};

	// FUN_00244FC8
	D.Rect(0.f, 0.f, ScreenW, ScreenH, FeColor(1.f, Hf(0x3FC26E98), Hf(0x3F94BC6A), Hf(0x3E2D0E56)), Ly(4));
	const FSSXSprite* R24 = Rec(24);
	const float Nw = R24 ? R24->W : 24.f, Nh = R24 ? R24->H : 24.f;
	const float Right = ScreenW - Nw;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FLinearColor C = Pass == 0 ? FeColor(1.f, 0.f, 0.f, 0.f) : FeColor(Hf(0x3ECCCCCD), 0.f, 0.f, 0.f);
		const float X = 8.f * Pass, Y = 8.f * Pass;
		D.Rect(X, Y, ScreenW, 80.f - Nh, C, Ly(6));
		const float Ny = (Y + 80.f) - Nh;
		D.Rect(X, Ny, 120.f, Nh, C, Ly(6));
		Spr(24, X + 120.f, Ny, 1.f, 1.f, 2, C, Ly(6));
		D.Rect(X + 120.f + Nw, Ny, 400.f - Nw, Nh, C, Ly(6));
		Spr(24, X + 520.f, Ny, 1.f, 1.f, 2, C, Ly(6));
		D.Rect(X + 520.f + Nw, Ny, (Right - 520.f) - Nw, Nh, C, Ly(6));
		Spr(23, X + Right, Ny, 1.f, 1.f, 2, C, Ly(6));
	}
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FLinearColor C = Pass == 0 ? FeColor(1.f, 0.f, 0.f, 0.f) : FeColor(Hf(0x3ECCCCCD), 0.f, 0.f, 0.f);
		const float X = 8.f * Pass, Y = 400.f - 8.f * Pass;
		D.Rect(X, Y, 120.f, Nh, C, Ly(6));
		Spr(24, X + 120.f, Y, 1.f, 1.f, 0, C, Ly(6));
		D.Rect(X + 120.f + Nw, Y, 400.f - Nw, Nh, C, Ly(6));
		Spr(24, X + 520.f, Y, 1.f, 1.f, 0, C, Ly(6));
		D.Rect(X + 520.f + Nw, Y, (Right - 520.f) - Nw, Nh, C, Ly(6));
		Spr(23, X + Right, Y, 1.f, 1.f, 0, C, Ly(6));
		D.Rect(X, Y + Nh, ScreenW, 80.f - Nh, C, Ly(6));
	}
	for (int32 I = 1; I <= 3; ++I)
	{
		D.Rect(0.f, Lines.Value(I), ScreenW, 1.f, FeColor(Hf(0x3E99999A), 1.f, 1.f, 1.f), Ly(7));
	}
	// FUN_002449F8
	if (const FSSXSprite* R3 = Rec(3))
	{
		Tiled(3, 0.f, ((480.f - R3->H) - 80.f) + 18.f, 700.f, R3->H + R3->H, 6.f, 1.f, false, Ly(5));
		Tiled(3, -57.f, -76.f, 700.f, R3->H + R3->H, 6.f, 1.f, true, Ly(5));
	}
}

void FSSXTrackLoader::DrawProgress(FSSXFeDrawList& D) const
{
	const FSSXSpriteTable* Sheet = D.Assets ? D.Assets->FeSprites : nullptr;
	auto Spr = [&D, Sheet](int32 K, float X, float Y, float Sx, float Sy, uint32 Flags, const FLinearColor& C, int32 L)
	{
		if (const FSSXSprite* S = Sheet ? Sheet->Find(K) : nullptr)
		{
			D.Icon(K, X, Y, S->W * Sx, S->H * Sy, Flags, C, L);
		}
	};
	// FUN_00244540 / 0x3A4C30 / 0x3A4C58 / 0x3A4C80
	static const float Dx[10] = { 0.f, 50.f, 45.f, 50.f, 54.f, 43.f, 43.f, 52.f, 52.f, 57.f };
	static const float Py[10] = { 430.f, 422.f, 419.f, 415.f, 406.f, 410.f, 413.f, 418.f, 426.f, 430.f };
	static const float Sy[10] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.9f, 0.8f, 0.8f, 0.9f };
	float Px = 120.f;
	for (int32 I = 0; I < 10; ++I)
	{
		Px += Dx[I];
		const FLinearColor C = I < FMath::Min(Progress, 10) ? FeColor(1.f, Hf(0x3F73F3F4), Hf(0x3F58D8D9), Hf(0x3F0E8E8F))
			: FeColor(Hf(0x3EC28F5C), 1.f, 1.f, 1.f);
		Spr(6 + I, Px, Py[I], 1.f, Sy[I], 0, C, Ly(8));
	}
}

namespace
{
	/** FUN_0019DF90 (align 0, no shadow): the first n characters, the n-th at half alpha after them (+6 x scale after a
	 *  space). */
	void Typed(FSSXFeDrawList& D, EFont F, float Scale, float X, float Y, const FString& Str, int32 N, const FLinearColor& C, int32 L,
		bool bShadow = false)
	{
		// FUN_0019D568
		const int32 Len = Str.Len();
		const FString Prefix = Str.Left(FMath::Clamp(N, 0, Len));
		D.Text(F, X, Y, Scale, C, Prefix, ESSXAlign::Left, bShadow, L);
		if (N > 0 && N < Len)
		{
			float W = FMath::TruncToFloat(D.TextInkWidth(F, Prefix, Scale));
			if (Prefix[N - 1] == TEXT(' ')) { W += Scale * 6.f; }
			D.Text(F, X + W, Y, Scale, WithA(C, 0.5f), Str.Mid(N, 1), ESSXAlign::Left, bShadow, L);
		}
	}

	/** FUN_0019E460 drawn part: Typed, then the cursor (10 x font height, +3 after the text) while bCursor (the row's
	 *  step is current) and the blink flag are set. */
	void TypedCursor(FSSXFeDrawList& D, float Scale, float X, float Y, const FString& Str, int32 N, bool bCursor, int32 TextL, int32 CursorL)
	{
		Typed(D, EFont::Menu, Scale, X, Y, Str, N, Black, TextL);
		if (!bCursor)
		{
			return;
		}
		const FSSXBitmapFont* Menu = D.Assets ? D.Assets->Font(EFont::Menu) : nullptr;
		const FString Prefix = Str.Left(FMath::Clamp(N, 0, Str.Len()));
		float W = Menu ? FMath::TruncToFloat(Menu->Width(Prefix, Scale)) : 0.f;
		if (N > 0 && N < Str.Len() && Prefix[N - 1] == TEXT(' ')) { W += Scale * 6.f; }
		D.Rect(X + W + 3.f, Y, 10.f, D.FontHeight(EFont::Menu, Scale), Black, CursorL);
	}

	// FUN_0019D370
	void RightText(FSSXFeDrawList& D, EFont F, float X, float Y, float Scale, const FLinearColor& C, const FString& Str, int32 L)
	{
		const float W = FMath::TruncToFloat(D.TextInkWidth(F, Str, Scale));
		D.Text(F, X - W, Y, Scale, C, Str, ESSXAlign::Left, false, L);
	}

	// FUN_0019D278 / FUN_0019D770
	void CentredText(FSSXFeDrawList& D, EFont F, float X, float Y, float Scale, const FLinearColor& C, const FString& Str, int32 L, bool bShadow)
	{
		// FUN_0019DC78 / FUN_0019D370
		const float W = FMath::TruncToFloat(D.TextInkWidth(F, Str, Scale));
		D.Text(F, X - W * 0.5f, Y, Scale, C, Str, ESSXAlign::Left, bShadow, L);
	}
}

void FSSXTrackLoader::DrawTexts(FSSXFeDrawList& D) const
{
	// 0x2473D8
	const FLinearColor TitleGold = FeColor(1.f, Hf(0x3F73F7CF), Hf(0x3F3A9FBE), Hf(0x3DD91687));
	if (Page.IsWorldCircuit())
	{
		// FUN_0019D568
		D.Text(EFont::Title, 15.f, 25.f, Hf(0x3F8CCCCD), TitleGold, Page.WcTitle[0], ESSXAlign::Left, true, LyText(10));
		D.Text(EFont::Title, 46.f, 54.f, Hf(0x3F8CCCCD), TitleGold, Page.WcTitle[1], ESSXAlign::Left, true, LyText(10));
	}
	else
	{
		Typed(D, EFont::Title, Hf(0x3F99999A), 30.f, 30.f, Page.ModeTitle, Seq.Count(1), TitleGold, LyText(10));
	}
	Typed(D, EFont::Title, Hf(0x3F4CCCCD), 395.f, 30.f, Page.VenueName, Seq.Count(5),
		FeColor(1.f, Hf(0x3F0978D5), Hf(0x3F45E354), Hf(0x3F53B646)), LyText(10));
	// FUN_0019D370
	const float S = Hf(0x3F947AE1);
	static const float RowY[8] = { 186.f, 212.f, 238.f, 264.f, 303.f, 329.f, 355.f, 131.f };
	const int32 NumLabels = Page.IsWorldCircuit() ? 8 : 7;
	for (int32 I = 0; I < NumLabels; ++I)
	{
		const float W = FMath::TruncToFloat(D.TextInkWidth(EFont::Menu, Page.Labels[I], S));
		D.Text(EFont::Menu, 460.f - W, RowY[I], S, Black, Page.Labels[I], ESSXAlign::Left, false, LyText(10));
	}
	// FUN_0019E460
	const FSSXBitmapFont* Menu = D.Assets ? D.Assets->Font(EFont::Menu) : nullptr;
	for (int32 Row = 0; Row < 8; ++Row)
	{
		if (Row == 7 && !Page.IsWorldCircuit())
		{
			break;
		}
		const FString Str = RowText(Row);
		const int32 N = Seq.Count(RowIds[Row]);
		Typed(D, EFont::Menu, S, 475.f, RowY[Row], Str, N, Black, LyText(10));
		if (RowBlink[Row] && Seq.Cur == RowSteps[Row])
		{
			const FString Prefix = Str.Left(FMath::Clamp(N, 0, Str.Len()));
			float W = Menu ? FMath::TruncToFloat(Menu->Width(Prefix, S)) : 0.f;
			if (N > 0 && N < Str.Len() && Prefix[N - 1] == TEXT(' ')) { W += S * 6.f; }
			D.Rect(475.f + W + 3.f, RowY[Row], 10.f, D.FontHeight(EFont::Menu, S), Black, Ly(10));
		}
	}
}

void FSSXTrackLoader::DrawPicture(FSSXFeDrawList& D, const FString& Name, float X, float Y, float Alpha) const
{
	// FUN_002447A8
	UTexture* const* T = Name.IsEmpty() || !D.Assets ? nullptr : D.Assets->Pages.Find(FName(*Name));
	if (!T || !*T)
	{
		return;
	}
	D.Image(*T, X + 3.f, Y + 3.f, 254.f, 79.f, FeColor(Alpha, 0.f, 0.f, 0.f), Ly(7));
	D.Image(*T, X, Y, 254.f, 79.f, FeColor(Alpha, 1.f, 1.f, 1.f), Ly(8));
}

void FSSXTrackLoader::DrawFeTexts(FSSXFeDrawList& D) const
{
	// 0x24AA70 / FUN_0019E460
	const int32 L = LyText(0xC);
	const FLinearColor TitleGold = FeColor(1.f, Hf(0x3F73F7CF), Hf(0x3F3A9FBE), Hf(0x3DD91687));
	Typed(D, EFont::Title, Hf(0x3F99999A), 30.f, 30.f, Page.FeTitle, Seq.Count(1), TitleGold, L);
	const float S = Hf(0x3F947AE1);
	for (int32 Row = 0; Row < FSSXTrackLoadPage::FeRows; ++Row)
	{
		RightText(D, EFont::Menu, 460.f, FeRowY[Row], S, Black, Page.FeLabels[Row], L);
	}
	for (int32 Row = 0; Row < FSSXTrackLoadPage::FeRows; ++Row)
	{
		TypedCursor(D, S, 475.f, FeRowY[Row], Page.FeValues[0][Row], Seq.Count(FeRowId(Row)),
			RowBlink[Row] && Seq.Cur == FeRowStep(Row), L, Ly(0xC));
	}
}

void FSSXTrackLoader::DrawFe2Page(FSSXFeDrawList& D) const
{
	// 0x24C2D0 / FUN_00244FC8 / FUN_002449F8 / FUN_002447A8
	DrawBase(D);
	DrawPicture(D, Page.FePictures[0], 23.f, 100.f, 1.f);
	DrawPicture(D, Page.FePictures[1], 360.f, 100.f, 1.f);
	DrawProgress(D);
}

void FSSXTrackLoader::DrawFe2Texts(FSSXFeDrawList& D) const
{
	// 0x24C428 / FUN_0019D770 / FUN_0019D278
	const int32 L = LyText(0xC);
	const FLinearColor TitleGold = FeColor(1.f, Hf(0x3F73F7CF), Hf(0x3F3A9FBE), Hf(0x3DD91687));
	CentredText(D, EFont::Title, 320.f, 30.f, Hf(0x3F99999A), TitleGold, Page.FeTitle, L, true);
	const float S = Hf(0x3F947AE1);
	static const int32 Rows[6] = { 2, 1, 3, 4, 5, 6 };
	static const float Y[6] = { 202.f, 228.f, 268.f, 294.f, 320.f, 346.f };
	for (int32 I = 0; I < 6; ++I)
	{
		CentredText(D, EFont::Menu, 320.f, Y[I], S, Black, Page.FeLabels[Rows[I]], L, false);
		RightText(D, EFont::Menu, 210.f, Y[I], S, Black, Page.FeValues[0][Rows[I]], L);
		D.Text(EFont::Menu, 430.f, Y[I], S, Black, Page.FeValues[1][Rows[I]], ESSXAlign::Left, false, L);
	}
}

void FSSXTrackLoader::DrawRankPage(FSSXFeDrawList& D) const
{
	// 0x24F918 / FUN_00244FC8 / FUN_002449F8 / FUN_002447A8
	DrawBase(D);
	const FSSXSpriteTable* Sheet = D.Assets ? D.Assets->FeSprites : nullptr;
	if (const FSSXSprite* R0 = Sheet ? Sheet->Find(0) : nullptr)
	{
		D.Icon(0, 10.f, 0.f, R0->W * Hf(0x3F666666), R0->H * Hf(0x3F666666), 0, White, Ly(8));
	}
	DrawPicture(D, Page.RankPicture, 23.f, 150.f, 1.f);
	DrawProgress(D);
}

void FSSXTrackLoader::DrawRankTexts(FSSXFeDrawList& D) const
{
	// 0x24FA48
	const int32 L = LyText(10);
	const FLinearColor TitleGold = FeColor(1.f, Hf(0x3F73F7CF), Hf(0x3F3A9FBE), Hf(0x3DD91687));
	D.Text(EFont::Title, 15.f, 25.f, Hf(0x3F8CCCCD), TitleGold, Page.WcTitle[0], ESSXAlign::Left, true, L);
	D.Text(EFont::Title, 46.f, 54.f, Hf(0x3F8CCCCD), TitleGold, Page.WcTitle[1], ESSXAlign::Left, true, L);
	const float S = Hf(0x3F99999A);
	Typed(D, EFont::Menu, S, 320.f, 110.f, Page.RankTitle, Seq.Count(1), Black, L);
	// FUN_00196070 / FUN_001961F0 / FUN_001957D8
	const float Bs = Hf(0x3F87AE14);
	const TArray<FString> BodyLines = SSXTrackLoad::WrapBox(D, EFont::Menu, Page.RankBody, Bs, 270.f);
	const float Pitch = (D.FontHeight(EFont::Menu, 1.f) + 3.f) * Bs;
	int32 Left = Seq.Count(2);
	for (int32 I = 0; I < BodyLines.Num(); ++I)
	{
		const int32 N = FMath::Min(Left, BodyLines[I].Len());
		Left -= N;
		Typed(D, EFont::Menu, Bs, 320.f, 152.f + Pitch * I, BodyLines[I], N, Black, L);
	}
	static const float Y[3] = { 290.f, 323.f, 356.f };
	for (int32 I = 0; I < 3; ++I)
	{
		RightText(D, EFont::Menu, 380.f, Y[I], S, Black, Page.RankLabels[I], L);
		Typed(D, EFont::Menu, S, 410.f, Y[I], Page.RankValues[I], Seq.Count(3 + I), White, L, true);
	}
}

void FSSXTrackLoader::DrawTutorialPage(FSSXFeDrawList& D) const
{
	// 0x24E2C0 / FUN_00244FC8 / FUN_002449F8 / FUN_00244540
	DrawBase(D);
	const FSSXSpriteTable* Sheet = D.Assets ? D.Assets->FeSprites : nullptr;
	if (const FSSXSprite* S = Sheet ? Sheet->Find(20) : nullptr)
	{
		D.Icon(20, 320.f, 240.f, S->W * Hf(0x3F8CCCCD), S->H * Hf(0x3F8CCCCD), 0x60, White, Ly(7));
	}
	DrawProgress(D);
}

void FSSXTrackLoader::DrawTutorialTexts(FSSXFeDrawList& D) const
{
	// 0x24E3A8 / FUN_0019D770
	const int32 L = LyText(0xC);
	const FLinearColor TitleGold = FeColor(1.f, Hf(0x3F73F7CF), Hf(0x3F3A9FBE), Hf(0x3DD91687));
	CentredText(D, EFont::Title, 320.f, 25.f, Hf(0x3FB33333), TitleGold, Page.TutorialTitle, L, true);
	for (const FSSXTrackLoadPage::FTutorialText& T : Page.TutorialTexts)
	{
		if (T.Align == 1) { RightText(D, EFont::Menu, T.X, T.Y, T.Scale, Black, T.Str, L); }
		else if (T.Align == 2) { CentredText(D, EFont::Menu, T.X, T.Y, T.Scale, Black, T.Str, L, false); }
		else { D.Text(EFont::Menu, T.X, T.Y, T.Scale, Black, T.Str, ESSXAlign::Left, false, L); }
	}
	const FSSXSpriteTable* Sheet = D.Assets ? D.Assets->FeSprites : nullptr;
	const float Rs = Hf(0x3F3851EC);
	for (int32 K = 0; K < 2; ++K)
	{
		if (const FSSXSprite* S = Sheet ? Sheet->Find(21 + K) : nullptr)
		{
			D.Icon(21 + K, 140.f, K == 0 ? 240.f : 290.f, S->W * Rs, S->H * Rs, 0, White, L);
		}
	}
}
