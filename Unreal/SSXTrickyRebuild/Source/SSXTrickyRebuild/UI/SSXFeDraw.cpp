#include "UI/SSXFeDraw.h"

#include "Algo/StableSort.h"
#include "Styling/CoreStyle.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "World/SSXRenderLook.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ESSXFeKey SSXFe::KeyForPadBit(uint32 Bit)
{
	switch (Bit)   // 0x3857C0
	{
	case PadUp: return ESSXFeKey::Up;
	case PadDown: return ESSXFeKey::Down;
	case PadRight: return ESSXFeKey::Right;
	case PadLeft: return ESSXFeKey::Left;
	case PadSquare: return ESSXFeKey::Square;
	case PadTriangle: return ESSXFeKey::Triangle;
	case PadCircle: return ESSXFeKey::Circle;
	case PadStart: return ESSXFeKey::Start;
	case PadSelect: return ESSXFeKey::Select;
	case PadL1: return ESSXFeKey::L1;
	case PadR1: return ESSXFeKey::R1;
	case PadCross: return ESSXFeKey::Cross;
	case PadL2: return ESSXFeKey::L2;
	case PadR2: return ESSXFeKey::R2;
	default: return ESSXFeKey::None;
	}
}

uint32 SSXFe::StickBits(float X, float Y)
{
	uint32 B = 0;
	if (X < -0.5f) { B |= PadLJoyLeft; } else if (X > 0.5f) { B |= PadLJoyRight; }
	if (Y < -0.5f) { B |= PadLJoyUp; } else if (Y > 0.5f) { B |= PadLJoyDown; }
	return B;
}

int32 SSXFe::FeKeyForWord(uint32 Word)
{
	struct FRow { uint32 Mask; int32 Key, Shoulder; };
	static const FRow Rows[] = {   // 0x3857C0
		{ 0x100, 0x1B, 0x1B }, { 0x80, 0x0D, 0x0D }, { 0x10, 'a', 'A' }, { 0x20, 'b', 'B' }, { 0x40, 'c', 'C' }, { 0x800, 'd', 'D' },
		{ 0x100008, 0x4B00, 0x4700 }, { 0x80004, 0x4D00, 0x4F00 }, { 0x40002, 0x5000, 0x5100 }, { 0x20001, 0x4800, 0x4900 },
		{ 0x400, 'r', 'r' }, { 0x2000, 'R', 'R' }, { 0x200, 'l', 'l' }, { 0x1000, 'L', 'L' } };
	for (const FRow& R : Rows)
	{
		if (Word & R.Mask)
		{
			return (Word & 0x3600) == 0 ? R.Key : R.Shoulder;
		}
	}
	return 0;
}

int32 SSXFe::FFeKeyEmitter::Step(uint32 Word)
{
	const int32 Key = FeKeyForWord(Word);
	if (Key == 0 || Key != Last)
	{
		Counter = -7;
		Last = Key;
		return Key;
	}
	if (++Counter > 7 && bRepeat)
	{
		Counter = 0;
		return Key;
	}
	return 0;
}

const TArray<SSXFe::FHelpEntry>& SSXFe::HelpBarTable()
{
	// 0x347F20
	static const TArray<FHelpEntry> T = {
		{ 0x1, IconCross, 535 }, { 0x2, IconCross, 541 }, { 0x4, IconCross, 3326 }, { 0x8, IconTriangle, 538 },
		{ 0x10, IconCross, 543 }, { 0x20, IconCross, 536 }, { 0x40, IconCross, 531 }, { 0x80, IconCross, 539 },
		{ 0x100, IconTriangle, 530 }, { 0x200, IconCross, 540 }, { 0x400, IconCross, 534 }, { 0x800, IconCircle, 532 },
		{ 0x1000, IconTriangle, 533 }, { 0x2000, IconSquare, 537 }, { 0x4000, IconCross, 529 }, { 0x8000, IconCircle, 542 },
	};
	return T;
}

TSharedPtr<FSSXBitmapFont> FSSXBitmapFont::Load(const FString& JsonPath, UTexture2D* InAtlas, const FString& SfnPath)
{
	FString JsonText;
	if (!InAtlas || !FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		return nullptr;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
	{
		return nullptr;
	}
	TSharedPtr<FSSXBitmapFont> F = MakeShared<FSSXBitmapFont>();
	F->Atlas = InAtlas;
	F->AtlasSize = FVector2f((float)Root->GetNumberField(TEXT("width")), (float)Root->GetNumberField(TEXT("height")));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("glyphs")))
	{
		const TSharedPtr<FJsonObject>& G = V->AsObject();
		FSSXBitmapGlyph Glyph;
		Glyph.AtlasPos = FVector2f((float)G->GetNumberField(TEXT("x")), (float)G->GetNumberField(TEXT("y")));
		Glyph.Size = FVector2f((float)G->GetNumberField(TEXT("w")), (float)G->GetNumberField(TEXT("h")));
		Glyph.Offset = FVector2f((float)G->GetNumberField(TEXT("xoffset")), (float)G->GetNumberField(TEXT("yoffset")));
		Glyph.Advance = (float)G->GetNumberField(TEXT("advance"));
		F->Glyphs.Add((TCHAR)G->GetIntegerField(TEXT("code")), Glyph);
	}
	TArray<uint8> Head;
	if (!SfnPath.IsEmpty() && FFileHelper::LoadFileToArray(Head, *SfnPath) && Head.Num() >= 0x20)
	{
		F->OriginX = (float)(int8)Head[0x10];
		F->OriginY = (float)(int8)Head[0x11];
	}
	F->RecomputeLineHeight();
	return F;
}

void FSSXBitmapFont::RecomputeLineHeight()
{
	float H = 0.f;
	for (const TPair<TCHAR, FSSXBitmapGlyph>& G : Glyphs)
	{
		H = FMath::Max(H, G.Value.Size.Y + G.Value.Offset.Y);
	}
	LineHeight = H;
}

float FSSXBitmapFont::Width(const FString& Line, float Scale) const
{
	float W = 0.f;
	for (TCHAR C : Line)
	{
		if (const FSSXBitmapGlyph* G = Find(C))
		{
			W += G->Advance;
		}
	}
	return W * Scale * ScaleX;
}

void FSSXBitmapFont::InkExtent(const FString& Line, float Scale, float& OutMin, float& OutMax) const
{
	const float Sx = Scale * ScaleX;
	float Pen = 0.f;
	OutMin = TNumericLimits<float>::Max();
	OutMax = -TNumericLimits<float>::Max();
	for (TCHAR C : Line)
	{
		if (const FSSXBitmapGlyph* G = Find(C))
		{
			if (G->Size.X > 0.f)
			{
				OutMin = FMath::Min(OutMin, Pen + G->Offset.X * Sx);
				OutMax = FMath::Max(OutMax, Pen + (G->Offset.X + G->Size.X) * Sx);
			}
			Pen += G->Advance * Sx;
		}
	}
	if (OutMax < OutMin)
	{
		OutMin = OutMax = 0.f;
	}
}

float FSSXBitmapFont::BoxWidth(const FString& Line, float Scale) const
{
	const double Sx = FMath::RoundToDouble(double(Scale) * double(ScaleX) * 1.0e6) / 1.0e6;
	double Pen = OriginX * Sx;
	double Lo = 0.0, Hi = 0.0;
	for (TCHAR C : Line)
	{
		if (const FSSXBitmapGlyph* G = Find(C))
		{
			Lo = FMath::Min(Lo, FMath::FloorToDouble(Pen + G->Offset.X * Sx));
			Hi = FMath::Max(Hi, FMath::CeilToDouble(Pen + (G->Offset.X + G->Size.X) * Sx));
			Pen += G->Advance * Sx;
		}
	}
	return float(Hi - Lo);
}

float FSSXBitmapFont::InkWidth(const FString& Line, float Scale) const
{
	float Lo = 0.f, Hi = 0.f;
	InkExtent(Line, Scale, Lo, Hi);
	return Hi - Lo;
}

bool FSSXSpriteTable::LoadJson(const FString& Path)
{
	Sprites.Reset();
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("sprites")))
	{
		const TSharedPtr<FJsonObject>& O = V->AsObject();
		FSSXSprite S;
		S.Page = FName(*O->GetStringField(TEXT("page")));
		S.W = (float)O->GetNumberField(TEXT("w"));
		S.H = (float)O->GetNumberField(TEXT("h"));
		S.U0 = (float)O->GetNumberField(TEXT("u0"));
		S.V0 = (float)O->GetNumberField(TEXT("v0"));
		S.U1 = (float)O->GetNumberField(TEXT("u1"));
		S.V1 = (float)O->GetNumberField(TEXT("v1"));
		Sprites.Add(O->GetIntegerField(TEXT("id")), S);
	}
	return Sprites.Num() > 0;
}

const FSSXBitmapFont* FSSXFeAssets::Font(SSXFe::EFont F) const
{
	switch (F)
	{
	case SSXFe::EFont::Title: return Title.Get();
	case SSXFe::EFont::Menu: return Menu.Get();
	default: return Small.Get();
	}
}

void FSSXFeDrawList::Rect(float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer)
{
	FSSXFeDrawCmd& C = Cmds.AddDefaulted_GetRef();
	C.Kind = FSSXFeDrawCmd::EKind::Quad;
	C.Layer = Layer;
	C.Pos = FVector2f(X, Y);
	C.Size = FVector2f(W, H);
	C.Color = Color;
	C.bGsUntextured = true;
}

FLinearColor FSSXFeDrawList::ScreenColor(const FSSXFeDrawCmd& C)
{
	FLinearColor Lc = C.Color;
	if (C.bGsUntextured && !C.Texture && C.Kind == FSSXFeDrawCmd::EKind::Quad)
	{
		constexpr float Gs = 128.f / 255.f;
		Lc.R *= Gs;
		Lc.G *= Gs;
		Lc.B *= Gs;
	}
	return Lc;
}

void FSSXFeDrawList::PageSprite(FName Page, int32 Id, float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer)
{
	UTexture* const* T = Assets ? Assets->Pages.Find(Page) : nullptr;
	FSSXFeDrawCmd& C = Cmds.AddDefaulted_GetRef();
	C.Kind = FSSXFeDrawCmd::EKind::Image;
	C.Layer = Layer;
	C.Pos = FVector2f(X, Y);
	C.Size = FVector2f(W, H);
	C.UV0 = FVector2f(0.f, 0.f);
	C.UV1 = FVector2f(1.f, 1.f);
	C.ImageObject = T ? *T : nullptr;
	C.Color = Color;
	C.SpriteId = Id;
}

void FSSXFeDrawList::RectScreen(float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer)
{
	Rect(X, Y, W, H, Color, Layer);
	Cmds.Last().bGsUntextured = false;
}

namespace
{
	const FSSXSpriteTable* GHudSheet = nullptr;
	const TMap<FName, UTexture*>* GHudPages = nullptr;
}

namespace
{
	float GImageAspect = 4.f / 3.f;

	TAutoConsoleVariable<int32> CVarUiWidescreen(TEXT("ssx.UI.Widescreen"), -1,
		TEXT("UI with a 16:9 image (Options > Widescreen): 0 = the original's 640 x 480 frame stretched to the image width (as on ")
		TEXT("a 16:9 TV), 1 = unstretched with the HUD anchored to the screen edges (deliberate difference), -1 = by look ")
		TEXT("(Classic 0, Remastered 1)."));
}

void SSXFeWide::SetImageAspect(float Aspect)
{
	GImageAspect = Aspect;
}

float SSXFeWide::VisibleExtra(float ImageAspect, float WindowAspect)
{
	if (ImageAspect <= SSXFe::ScreenW / SSXFe::ScreenH + 1e-3f || WindowAspect <= 0.f)
	{
		return 0.f;
	}
	return FMath::Max(0.f, FMath::Min(ImageAspect, WindowAspect) * SSXFe::ScreenH * 0.5f - SSXFe::ScreenW * 0.5f);
}

bool SSXFeWide::Anchored()
{
	const int32 Mode = CVarUiWidescreen.GetValueOnAnyThread();
	return Mode < 0 ? SSXRenderLook::Get() == ESSXRenderLook::Remastered : Mode != 0;
}

static float CurrentExtra()
{
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
	{
		return 0.f;
	}
	const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
	return Size.Y > 0 ? SSXFeWide::VisibleExtra(GImageAspect, float(Size.X) / float(Size.Y)) : 0.f;
}

float SSXFeWide::ExtraX()
{
	return Anchored() ? CurrentExtra() : 0.f;
}

float SSXFeWide::StretchX()
{
	return Anchored() ? 1.f : 1.f + 2.f * CurrentExtra() / SSXFe::ScreenW;
}

float SSXFeWide::AnchorX(float X, float Extra)
{
	return X < SSXFe::ScreenW / 3.f ? X - Extra : (X > SSXFe::ScreenW * 2.f / 3.f ? X + Extra : X);
}

void SSXFeDrawHud::SetSheet(const FSSXSpriteTable* Sprites, const TMap<FName, UTexture*>* Pages)
{
	GHudSheet = Sprites;
	GHudPages = Pages;
}

const FSSXSpriteTable* SSXFeDrawHud::GetSheet() { return GHudSheet; }

static const FSSXSprite* ResolveSprite(const FSSXFeAssets* Assets, int32 Id, bool bHud, UTexture*& OutPage,
	ESSXPromptContext Prompts = ESSXPromptContext::Original)
{
	OutPage = nullptr;
	const FSSXSpriteTable* Table = Assets ? (bHud ? Assets->HudSprites : Assets->FeSprites) : nullptr;
	if (!Table && bHud)
	{
		Table = GHudSheet;
	}
	const FSSXSprite* S = Table ? Table->Find(Id) : nullptr;
	if (!S)
	{
		return nullptr;
	}
	UTexture* const* Page = Assets ? Assets->Pages.Find(S->Page) : nullptr;
	if ((!Page || !*Page) && bHud && GHudPages)
	{
		Page = GHudPages->Find(S->Page);
	}
	OutPage = Page ? *Page : nullptr;
	if (OutPage && Prompts != ESSXPromptContext::Original)
	{
		static FSSXSprite Prompt;
		Prompt = *S;
		if (SSXPrompts::Substitute(Id, bHud, Prompts, Prompt, OutPage)) { return &Prompt; }
	}
	return OutPage ? S : nullptr;
}

bool FSSXFeDrawList::Sprite(int32 Id, float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer, bool bHud)
{
	return SpriteEx(Id, X, Y, W, H, Color, Layer, bHud, false, false);
}

bool FSSXFeDrawList::SpriteEx(int32 Id, float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer, bool bHud,
	bool bFlipX, bool bFlipY)
{
	UTexture* Page = nullptr;
	const FSSXSprite* S = ResolveSprite(Assets, Id, bHud, Page, PromptContext);
	if (!S)
	{
		return false;
	}
	FSSXFeDrawCmd& C = Cmds.AddDefaulted_GetRef();
	C.Kind = FSSXFeDrawCmd::EKind::Quad;
	C.Layer = Layer;
	C.Pos = FVector2f(X, Y);
	C.Size = FVector2f(W < 0.f ? S->W : W, H < 0.f ? S->H : H);
	if (S->FitAspect > 0.f && C.Size.Y > 0.f)
	{
		const FVector2f Fit = S->FitAspect > C.Size.X / C.Size.Y ? FVector2f(C.Size.X, C.Size.X / S->FitAspect)
			: FVector2f(C.Size.Y * S->FitAspect, C.Size.Y);
		C.Pos += (C.Size - Fit) * 0.5f;
		C.Size = Fit;
		bFlipX = bFlipY = false;
	}
	C.UV0 = FVector2f(bFlipX ? S->U1 : S->U0, bFlipY ? S->V1 : S->V0);
	C.UV1 = FVector2f(bFlipX ? S->U0 : S->U1, bFlipY ? S->V0 : S->V1);
	C.Texture = Page;
	C.Color = Color;
	C.SpriteId = Id;
	return true;
}

FVector2f FSSXFeDrawList::IconOrigin(float X, float Y, float W, float H, uint32 Flags)
{
	// FUN_001FC178
	const bool bRot = (Flags & 0x10) != 0;
	const float Sw = bRot ? H : W, Sh = bRot ? W : H;
	if (Flags & 0x4) { Y -= Sh; }
	if (Flags & 0x8) { X -= Sw; }
	if (Flags & 0x20) { X -= Sw * 0.5f; }
	if (Flags & 0x40) { Y -= Sh * 0.5f; }
	return FVector2f(X, Y);
}

void FSSXFeDrawList::CornerUVs(const FVector2f& U0, const FVector2f& U1, bool bRot90, FVector2f Out[4])
{
	// FUN_0019BDF8
	Out[0] = bRot90 ? FVector2f(U1.X, U0.Y) : FVector2f(U0.X, U0.Y);
	Out[1] = bRot90 ? FVector2f(U1.X, U1.Y) : FVector2f(U1.X, U0.Y);
	Out[2] = bRot90 ? FVector2f(U0.X, U1.Y) : FVector2f(U1.X, U1.Y);
	Out[3] = bRot90 ? FVector2f(U0.X, U0.Y) : FVector2f(U0.X, U1.Y);
}

bool FSSXFeDrawList::ClipToScreen(FVector2f& P0, FVector2f& P1, FVector2f Uv[4])
{
	// 0x1A9BB8 (widescreen: the visible width past the frame too)
	const float E = SSXFeWide::ExtraX();
	const FVector2f A = P0, B = P1;
	const FVector2f N0(FMath::Clamp(A.X, -E, SSXFe::ScreenW + E), FMath::Clamp(A.Y, 0.f, SSXFe::ScreenH));
	const FVector2f N1(FMath::Clamp(B.X, -E, SSXFe::ScreenW + E), FMath::Clamp(B.Y, 0.f, SSXFe::ScreenH));
	if (N0.X == N1.X || N0.Y == N1.Y)
	{
		return false;
	}
	if (N0 == A && N1 == B)
	{
		return true;
	}
	const FVector2f In[4] = { Uv[0], Uv[1], Uv[2], Uv[3] };
	auto At = [&](float X, float Y)
	{
		const float Tx = B.X != A.X ? (X - A.X) / (B.X - A.X) : 0.f;
		const float Ty = B.Y != A.Y ? (Y - A.Y) / (B.Y - A.Y) : 0.f;
		const FVector2f Top = FMath::Lerp(In[0], In[1], Tx), Bot = FMath::Lerp(In[3], In[2], Tx);
		return FMath::Lerp(Top, Bot, Ty);
	};
	Uv[0] = At(N0.X, N0.Y);
	Uv[1] = At(N1.X, N0.Y);
	Uv[2] = At(N1.X, N1.Y);
	Uv[3] = At(N0.X, N1.Y);
	P0 = N0;
	P1 = N1;
	return true;
}

bool FSSXFeDrawList::Icon(int32 Id, float X, float Y, float W, float H, uint32 Flags, const FLinearColor& Color, int32 Layer)
{
	UTexture* Page = nullptr;
	const FSSXSprite* S = ResolveSprite(Assets, Id, false, Page);
	if (!S)
	{
		return false;
	}
	W = W < 0.f ? S->W : W;
	H = H < 0.f ? S->H : H;
	const FVector2f P = IconOrigin(X, Y, W, H, Flags);   // 0x1FC178
	const bool bRot = (Flags & 0x10) != 0;
	if (!SpriteEx(Id, P.X, P.Y, bRot ? H : W, bRot ? W : H, Color, Layer, false, (Flags & 0x1) != 0, (Flags & 0x2) != 0))
	{
		return false;
	}
	Cmds.Last().bRot90 = bRot;
	return true;
}

bool FSSXFeDrawList::SpriteCropU(int32 Id, float X, float Y, float W, float H, float FracU, const FLinearColor& Color, int32 Layer, bool bHud)
{
	if (!SpriteEx(Id, X, Y, W, H, Color, Layer, bHud, false, false))
	{
		return false;
	}
	FSSXFeDrawCmd& C = Cmds.Last();
	C.UV1.X = C.UV0.X + (C.UV1.X - C.UV0.X) * FMath::Clamp(FracU, 0.f, 1.f);
	return true;
}

bool FSSXFeDrawList::SpriteTiled(int32 Id, float X, float Y, float W, float H, float RepU, float RepV, const FLinearColor& Color, int32 Layer)
{
	UTexture* Page = nullptr;
	const FSSXSprite* S = ResolveSprite(Assets, Id, false, Page);
	if (!S || RepU <= 0.f || RepV <= 0.f || W <= 0.f || H <= 0.f)
	{
		return false;
	}
	const float Cw = W / RepU, Ch = H / RepV;
	const int32 Nu = FMath::CeilToInt(RepU - 1e-4f), Nv = FMath::CeilToInt(RepV - 1e-4f);
	for (int32 J = 0; J < Nv; ++J)
	{
		const float Fv = FMath::Min(1.f, RepV - J);
		for (int32 I = 0; I < Nu; ++I)
		{
			const float Fu = FMath::Min(1.f, RepU - I);
			FSSXFeDrawCmd& C = Cmds.AddDefaulted_GetRef();
			C.Kind = FSSXFeDrawCmd::EKind::Quad;
			C.Layer = Layer;
			C.Pos = FVector2f(X + I * Cw, Y + J * Ch);
			C.Size = FVector2f(Cw * Fu, Ch * Fv);
			C.UV0 = FVector2f(S->U0, S->V0);
			C.UV1 = FVector2f(S->U0 + (S->U1 - S->U0) * Fu, S->V0 + (S->V1 - S->V0) * Fv);
			C.Texture = Page;
			C.Color = Color;
			C.SpriteId = Id;
		}
	}
	return true;
}

void FSSXFeDrawList::Image(UObject* Resource, float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer)
{
	if (!Resource)
	{
		return;
	}
	FSSXFeDrawCmd& C = Cmds.AddDefaulted_GetRef();
	C.Kind = FSSXFeDrawCmd::EKind::Image;
	C.Layer = Layer;
	C.Pos = FVector2f(X, Y);
	C.Size = FVector2f(W, H);
	C.ImageObject = Resource;
	C.Color = Color;
}

float FSSXFeDrawList::TextWidth(SSXFe::EFont Font, const FString& Str, float Scale) const
{
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	if (!F)
	{
		return 0.f;
	}
	float W = 0.f;
	TArray<FString> Lines;
	Str.ParseIntoArray(Lines, TEXT("\n"), false);
	for (const FString& L : Lines)
	{
		W = FMath::Max(W, F->Width(L, Scale));
	}
	return W;
}

float FSSXFeDrawList::TextHeight(SSXFe::EFont Font, float Scale) const
{
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	return F ? F->Height(Scale) : 0.f;
}

float FSSXFeDrawList::TextInkWidth(SSXFe::EFont Font, const FString& Str, float Scale) const
{
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	if (!F)
	{
		return 0.f;
	}
	float W = 0.f;
	TArray<FString> Lines;
	Str.ParseIntoArray(Lines, TEXT("\n"), false);
	for (const FString& L : Lines)
	{
		W = FMath::Max(W, F->InkWidth(L, Scale));
	}
	return W;
}

float FSSXFeDrawList::TextBoxWidth(SSXFe::EFont Font, const FString& Str, float Scale) const
{
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	if (!F)
	{
		return 0.f;
	}
	float W = 0.f;
	TArray<FString> Lines;
	Str.ParseIntoArray(Lines, TEXT("\n"), false);
	for (const FString& L : Lines)
	{
		W = FMath::Max(W, F->BoxWidth(L, Scale));
	}
	return W;
}

float FSSXFeDrawList::FontHeight(SSXFe::EFont Font, float Scale) const
{
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	return F ? F->LineHeight * Scale : 0.f;
}

bool FSSXFeDrawList::GlyphInk(SSXFe::EFont Font, TCHAR Ch, float Scale, float& OutXoff, float& OutW) const
{
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	const FSSXBitmapGlyph* G = F ? F->Find(Ch) : nullptr;
	OutXoff = OutW = 0.f;
	if (!G)
	{
		return false;
	}
	OutXoff = G->Offset.X * Scale * F->ScaleX;
	OutW = G->Size.X * Scale * F->ScaleX;
	return true;
}

void FSSXFeDrawList::Glyphs(const FSSXBitmapFont& F, float X, float Y, float Scale, const FLinearColor& Color, const FString& Line, int32 Layer)
{
	// FUN_0019cf70
	const float Sx = Scale * F.ScaleX, Sy = Scale * F.ScaleY;
	float Pen = X + F.OriginX * Sx;
	const float Top = Y + F.OriginY * Sy;
	for (TCHAR Ch : Line)
	{
		const FSSXBitmapGlyph* G = F.Find(Ch);
		if (!G)
		{
			continue;
		}
		if (G->Size.X > 0.f && G->Size.Y > 0.f && F.Atlas)
		{
			FSSXFeDrawCmd& C = Cmds.AddDefaulted_GetRef();
			C.Kind = FSSXFeDrawCmd::EKind::Glyphs;
			C.Layer = Layer;
			C.Pos = FVector2f(Pen + G->Offset.X * Sx, Top + G->Offset.Y * Sy);
			C.Size = FVector2f(G->Size.X * Sx, G->Size.Y * Sy);
			C.UV0 = G->AtlasPos / F.AtlasSize;
			C.UV1 = (G->AtlasPos + G->Size) / F.AtlasSize;
			C.Texture = F.Atlas;
			C.Color = Color;
		}
		Pen += G->Advance * Sx;
	}
}

FVector2f FSSXFeDrawList::Text(SSXFe::EFont Font, float X, float Y, float Scale, const FLinearColor& Color, const FString& Str,
	ESSXAlign Align, bool bShadow, int32 Layer)
{
	Texts.Add(Str);
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	if (!F || Str.IsEmpty())
	{
		return FVector2f::ZeroVector;
	}
	TArray<FString> Lines;
	Str.ParseIntoArray(Lines, TEXT("\n"), false);
	float MaxW = 0.f;
	const float LineH = F->Height(Scale);
	for (int32 I = 0; I < Lines.Num(); ++I)
	{
		const float W = F->Width(Lines[I], Scale);
		MaxW = FMath::Max(MaxW, W);
		const float Lx = Align == ESSXAlign::Center ? X - W * 0.5f : (Align == ESSXAlign::Right ? X - W : X);
		const float Ly = Y + I * LineH;
		if (I == 0)
		{
			FSSXFeTextRec& R = TextRecs.AddDefaulted_GetRef();
			R.Str = Str;
			R.Pos = FVector2f(Lx, Ly);
			R.Layer = Layer;
			R.Color = Color;
			R.bShadow = bShadow;
		}
		if (bShadow)
		{
			FLinearColor S = SSXFe::Black;
			S.A = Color.A;   // FUN_0019d3f0
			Glyphs(*F, Lx + SSXFe::ShadowOffset, Ly + SSXFe::ShadowOffset, Scale, S, Lines[I], Layer - 1);
		}
		Glyphs(*F, Lx, Ly, Scale, Color, Lines[I], Layer);
	}
	const float Pad = bShadow ? SSXFe::ShadowOffset : 0.f;
	if (TextRecs.Num() > 0)
	{
		TextRecs.Last().Size = FVector2f(MaxW, LineH * Lines.Num());
	}
	return FVector2f(MaxW + Pad, LineH * Lines.Num() + Pad);
}

float FSSXFeDrawList::TextBox(SSXFe::EFont Font, float X, float Y, float Width, float Scale, const FLinearColor& Color, const FString& Str,
	ESSXAlign Align, bool bShadow, int32 Layer)
{
	const FSSXBitmapFont* F = Assets ? Assets->Font(Font) : nullptr;
	if (!F)
	{
		Texts.Add(Str);
		return 0.f;
	}
	TArray<FString> Out;
	TArray<FString> Paragraphs;
	Str.ParseIntoArray(Paragraphs, TEXT("\n"), false);
	for (const FString& P : Paragraphs)
	{
		TArray<FString> Words;
		P.ParseIntoArrayWS(Words);
		FString Line;
		for (const FString& W : Words)
		{
			const FString Try = Line.IsEmpty() ? W : Line + TEXT(" ") + W;
			if (!Line.IsEmpty() && F->Width(Try, Scale) > Width)
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
	const float Ax = Align == ESSXAlign::Center ? X + Width * 0.5f : (Align == ESSXAlign::Right ? X + Width : X);
	Text(Font, Ax, Y, Scale, Color, FString::Join(Out, TEXT("\n")), Align, bShadow, Layer);
	return F->Height(Scale) * Out.Num();
}

void SSSXFeCanvas::Construct(const FArguments& InArgs)
{
	DrawList = InArgs._DrawList;
	bFillBlack = InArgs._FillBlack;
	SetCanTick(false);
}

const FSlateBrush* SSSXFeCanvas::BrushFor(UObject* Resource) const
{
	TSharedPtr<FSlateBrush>& B = Brushes.FindOrAdd(Resource);
	if (!B.IsValid())
	{
		B = MakeShared<FSlateBrush>();
		B->SetResourceObject(Resource);
		B->ImageSize = FVector2D(1, 1);
		B->DrawAs = ESlateBrushDrawType::Image;
	}
	return B.Get();
}

int32 SSSXFeCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Local = FVector2f(Geo.GetLocalSize());
	const float Scale = FMath::Min(Local.X / SSXFe::ScreenW, Local.Y / SSXFe::ScreenH);
	const FVector2f Origin = (Local - FVector2f(SSXFe::ScreenW, SSXFe::ScreenH) * Scale) * 0.5f;
	const FSlateRenderTransform& RT = Geo.GetAccumulatedRenderTransform();

	FSlateRenderer* Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
	if (!Renderer)
	{
		return LayerId;
	}
	if (bFillBlack)
	{
		FSlateDrawElement::MakeBox(Out, LayerId, Geo.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor::Black);
	}
	if (!DrawList || DrawList->Cmds.Num() == 0)
	{
		return LayerId + 1;
	}

	TArray<int32> Order;
	Order.Reserve(DrawList->Cmds.Num());
	for (int32 I = 0; I < DrawList->Cmds.Num(); ++I)
	{
		Order.Add(I);
	}
	Algo::StableSort(Order, [this](int32 A, int32 B) { return DrawList->Cmds[A].Layer < DrawList->Cmds[B].Layer; });
	const int32 MinLayer = DrawList->Cmds[Order[0]].Layer;

	UTexture* WhiteTex = DrawList->Assets ? DrawList->Assets->White : nullptr;
	TArray<FSlateVertex> Verts;
	TArray<SlateIndex> Idx;
	UObject* BatchRes = nullptr;
	int32 BatchLayer = 0;
	int32 MaxLayerId = LayerId + 1;
	const float Opacity = InWidgetStyle.GetColorAndOpacityTint().A;
	const float Extra = SSXFeWide::ExtraX();
	const float Stretch = SSXFeWide::StretchX();
	const float Mid = SSXFe::ScreenW * 0.5f;

	auto Flush = [&]()
	{
		if (Verts.Num() > 0 && BatchRes)
		{
			const FSlateResourceHandle H = Renderer->GetResourceHandle(*BrushFor(BatchRes));
			FSlateDrawElement::MakeCustomVerts(Out, BatchLayer, H, Verts, Idx, nullptr, 0, 0);
		}
		Verts.Reset();
		Idx.Reset();
	};

	for (int32 I : Order)
	{
		const FSSXFeDrawCmd& C = DrawList->Cmds[I];
		UObject* Res = C.Kind == FSSXFeDrawCmd::EKind::Image ? C.ImageObject : (C.Texture ? (UObject*)C.Texture : (UObject*)WhiteTex);
		if (!Res)
		{
			continue;
		}
		// FUN_0019BDF8
		const FVector2f U0 = (C.Texture || C.Kind == FSSXFeDrawCmd::EKind::Image) ? C.UV0 : FVector2f(0, 0);
		const FVector2f U1 = (C.Texture || C.Kind == FSSXFeDrawCmd::EKind::Image) ? C.UV1 : FVector2f(1, 1);
		FVector2f Uv[4];
		FSSXFeDrawList::CornerUVs(U0, U1, C.bRot90, Uv);
		FVector2f Q0 = C.Pos, Q1 = C.Pos + C.Size;
		if (Extra > 0.f && !C.Texture && C.Kind == FSSXFeDrawCmd::EKind::Quad && Q0.X <= 0.f && Q1.X >= SSXFe::ScreenW
			&& Q0.Y <= 0.f && Q1.Y >= SSXFe::ScreenH)
		{
			Q0.X = -Extra;   // full-screen fades and dims cover the widescreen sides too
			Q1.X = SSXFe::ScreenW + Extra;
		}
		if (!FSSXFeDrawList::ClipToScreen(Q0, Q1, Uv))
		{
			continue;
		}
		const int32 Layer = LayerId + 1 + (C.Layer - MinLayer);
		if (Res != BatchRes || Layer != BatchLayer)
		{
			Flush();
			BatchRes = Res;
			BatchLayer = Layer;
		}
		MaxLayerId = FMath::Max(MaxLayerId, Layer);
		// PS2 colours are gamma-space values: write them straight into the vertex bytes.
		FLinearColor Lc = FSSXFeDrawList::ScreenColor(C);
		Lc.A *= Opacity;
		const FColor Col = Lc.ToFColor(false);
		const FVector2f P0 = Origin + FVector2f(Mid + (Q0.X - Mid) * Stretch, Q0.Y) * Scale;
		const FVector2f P1 = Origin + FVector2f(Mid + (Q1.X - Mid) * Stretch, Q1.Y) * Scale;
		const SlateIndex Base = (SlateIndex)Verts.Num();
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, FVector2f(P0.X, P0.Y), Uv[0], Col));
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, FVector2f(P1.X, P0.Y), Uv[1], Col));
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, FVector2f(P1.X, P1.Y), Uv[2], Col));
		Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, FVector2f(P0.X, P1.Y), Uv[3], Col));
		Idx.Append({ Base, (SlateIndex)(Base + 1), (SlateIndex)(Base + 2), Base, (SlateIndex)(Base + 2), (SlateIndex)(Base + 3) });
	}
	Flush();
	return MaxLayerId;
}
