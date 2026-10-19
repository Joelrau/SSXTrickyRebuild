#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UI/SSXFeTypes.h"
#include "UI/SSXPrompts.h"
#include "Widgets/SLeafWidget.h"

class UTexture;
class UTexture2D;

struct SSXTRICKYREBUILD_API FSSXBitmapGlyph
{
	FVector2f AtlasPos = FVector2f::ZeroVector;
	FVector2f Size = FVector2f::ZeroVector;
	FVector2f Offset = FVector2f::ZeroVector;
	float Advance = 0.f;
};

/** One SFN font as the game's loader FUN_0019c910 sees it: atlas + metrics come from converted/frontend/fonts/<NAME>.png/.json; header origin offsets (s8 0x10 / 0x11) are read from DATA/FONTS/<NAME>.SFN. The kerning table (file 0x18) and the 0x13 line-height byte are NOT read by the game, so they are ignored; the line height is max over glyphs of (h + yoffset). */
struct SSXTRICKYREBUILD_API FSSXBitmapFont
{
	UTexture2D* Atlas = nullptr;
	FVector2f AtlasSize = FVector2f(1, 1);
	float LineHeight = 0.f;
	float OriginX = 0.f, OriginY = 0.f;
	float ScaleX = 1.f, ScaleY = 1.f;
	TMap<TCHAR, FSSXBitmapGlyph> Glyphs;

	// Parse the font JSON; SfnPath (optional) supplies the header origin bytes.
	static TSharedPtr<FSSXBitmapFont> Load(const FString& JsonPath, UTexture2D* InAtlas, const FString& SfnPath = FString());
	void RecomputeLineHeight();

	// FUN_0019cc10
	const FSSXBitmapGlyph* Find(TCHAR C) const { return Glyphs.Find(C); }
	// FUN_0019da30
	float Width(const FString& Line, float Scale) const;
	float Height(float Scale) const { return LineHeight * Scale * ScaleY; }
	/** HUD text measure FUN_0019da30 / FUN_0019dc78: the ink bounding box max(pen + xoff + w) - min(pen + xoff) in scaled pixels (sx = scale * ScaleX), not the sum of advances. */
	float InkWidth(const FString& Line, float Scale) const;
	/** Text-widget width: the integer inked box from 0, pen start originX * sx; per glyph floor(pen + xoff sx) .. ceil(pen + (xoff + w) sx), pen += advance sx. */
	float BoxWidth(const FString& Line, float Scale) const;
	void InkExtent(const FString& Line, float Scale, float& OutMin, float& OutMax) const;
};

struct SSXTRICKYREBUILD_API FSSXSprite
{
	FName Page;
	float W = 0.f, H = 0.f;
	float U0 = 0.f, V0 = 0.f, U1 = 1.f, V1 = 1.f;
	/** A button prompt (SSXPrompts): the image's w / h, drawn centred in the box at that aspect, never mirrored. 0 = fill the box. */
	float FitAspect = 0.f;
};

class SSXTRICKYREBUILD_API FSSXSpriteTable
{
public:
	bool LoadJson(const FString& Path);
	const FSSXSprite* Find(int32 Id) const { return Sprites.Find(Id); }
	bool IsLoaded() const { return Sprites.Num() > 0; }
	TMap<int32, FSSXSprite> Sprites;
};

struct SSXTRICKYREBUILD_API FSSXFeAssets
{
	TSharedPtr<FSSXBitmapFont> Title, Menu, Small;
	const FSSXSpriteTable* FeSprites = nullptr;
	/** HUD-sheet sprite records (hudgame / hudtrick): ids 0..0x89 {w, h, u0, v0, u1, v1}, built by 0x1F4440 from the overlay-manager ctor 0x1A6FB8 (exported to hud_sprites.json). In-race icons resolve here. */
	const FSSXSpriteTable* HudSprites = nullptr;
	TMap<FName, UTexture*> Pages;
	UTexture* White = nullptr;

	const FSSXBitmapFont* Font(SSXFe::EFont F) const;
};

/** HUD sheet (hudgame / hudtrick) registered by the in-race HUD; draw lists whose assets have no HudSprites resolve HUD ids and pages against it. Pointers must outlive their use. */
namespace SSXFeDrawHud
{
	SSXTRICKYREBUILD_API void SetSheet(const FSSXSpriteTable* Sprites, const TMap<FName, UTexture*>* Pages);
	SSXTRICKYREBUILD_API const FSSXSpriteTable* GetSheet();
}

enum class ESSXAlign : uint8 { Left, Center, Right };

struct SSXTRICKYREBUILD_API FSSXFeDrawCmd
{
	enum class EKind : uint8 { Quad, Glyphs, Image };
	EKind Kind = EKind::Quad;
	int32 Layer = 0;
	FVector2f Pos = FVector2f::ZeroVector;
	FVector2f Size = FVector2f::ZeroVector;
	FVector2f UV0 = FVector2f(0, 0), UV1 = FVector2f(1, 1);
	UTexture* Texture = nullptr;
	UObject* ImageObject = nullptr;
	FLinearColor Color = FLinearColor::White;
	int32 SpriteId = -1;
	/** An untextured GS primitive with a widget colour: the game writes the float colour x 128 (FUN_0019C1E8 / FUN_0014F5F0) and the GS outputs an untextured vertex colour as is, so 1.0 = 128 = half intensity on screen (textured quads use MODULATE, where 128 = the texel). The canvas applies the 128 / 255 scale; the command keeps the game's colour. */
	bool bGsUntextured = false;
	/** Icon flag 0x10 (FUN_001FC178 -> FUN_0019BDF8): the texture is turned 90 degrees in the quad: top-left (u1, v0),
	 *  top-right (u1, v1), bottom-right (u0, v1), bottom-left (u0, v0) (UV0 / UV1 as stored). */
	bool bRot90 = false;
};

struct SSXTRICKYREBUILD_API FSSXFeTextRec
{
	FString Str;
	FVector2f Pos = FVector2f::ZeroVector;
	FVector2f Size = FVector2f::ZeroVector;
	int32 Layer = 0;
	FLinearColor Color = FLinearColor::White;
	bool bShadow = false;
};

class SSXTRICKYREBUILD_API FSSXFeDrawList
{
public:
	/** The colour a command shows on screen: untextured GS primitives (bGsUntextured, no texture) RGB x 128 / 255 (alpha unchanged, nothing clamped before the scale); everything else as stored. */
	static FLinearColor ScreenColor(const FSSXFeDrawCmd& C);
	explicit FSSXFeDrawList(const FSSXFeAssets* InAssets = nullptr) : Assets(InAssets) {}

	/** The key map button prompts describe (SSXPrompts); FSSXPromptScope sets it for a block of draws. */
	ESSXPromptContext PromptContext = ESSXPromptContext::Menu;

	void Reset() { Cmds.Reset(); Texts.Reset(); TextRecs.Reset(); }

	void Rect(float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer = 0);
	void RectScreen(float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer = 0);
	bool Sprite(int32 Id, float X, float Y, float W = -1.f, float H = -1.f, const FLinearColor& Color = FLinearColor::White,
		int32 Layer = 0, bool bHud = false);
	bool SpriteCropU(int32 Id, float X, float Y, float W, float H, float FracU, const FLinearColor& Color, int32 Layer, bool bHud);
	bool SpriteEx(int32 Id, float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer, bool bHud, bool bFlipX, bool bFlipY);
	/** Tiled icon (repeat u x v): the rectangle is split into cells W/RepU x H/RepV that each show the whole sprite; a fractional last cell shows the matching part of the sprite. */
	bool SpriteTiled(int32 Id, float X, float Y, float W, float H, float RepU, float RepV, const FLinearColor& Color, int32 Layer);
	/** cFE3DIcon draw FUN_001FC178 flags: 1 mirror u, 2 flip v, 4 the y position is the bottom edge, 8 the x position is the right edge, 0x10 rotate 90 (the quad is H wide and W high, bRot90), 0x20 centre x, 0x40 centre y. W / H < 0 = the record's size. False if not resolvable. */
	bool Icon(int32 Id, float X, float Y, float W, float H, uint32 Flags, const FLinearColor& Color, int32 Layer);
	// FUN_001FC178
	static FVector2f IconOrigin(float X, float Y, float W, float H, uint32 Flags);
	// FUN_0019BDF8
	static void CornerUVs(const FVector2f& U0, const FVector2f& U1, bool bRot90, FVector2f Out[4]);
	/** The frame-buffer scissor: clips the quad P0..P1 (corner uvs TL, TR, BR, BL) to the 640 x 480 screen, re-sampling
	 *  the uvs; false when nothing is left. */
	static bool ClipToScreen(FVector2f& P0, FVector2f& P1, FVector2f Uv[4]);
	/** A runtime-bound FE sprite (records 124+ = {w 128, h 128, uv 0..1}, FUN_001F1580, bound by FUN_001F9660): the whole
	 *  texture Assets->Pages[Page] in the rectangle. Recorded (SpriteId) even when the page is not loaded; drawn only when
	 *  it is. Textured: the colour modulates (1.0 = the texel). */
	void PageSprite(FName Page, int32 Id, float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer);
	void Image(UObject* Resource, float X, float Y, float W, float H, const FLinearColor& Color = FLinearColor::White, int32 Layer = 0);

	/** FUN_0019cf70 text draw: one line per '\n'. Align Center/Right subtracts width/2 or width (FUN_0019d6e0). Shadow: the same string at (+2,+2) in black keeping alpha, one layer below. Returns the drawn extent (shadow adds 2 px). */
	FVector2f Text(SSXFe::EFont Font, float X, float Y, float Scale, const FLinearColor& Color, const FString& Str,
		ESSXAlign Align = ESSXAlign::Left, bool bShadow = false, int32 Layer = 1);
	float TextWidth(SSXFe::EFont Font, const FString& Str, float Scale) const;
	float TextHeight(SSXFe::EFont Font, float Scale) const;
	float TextInkWidth(SSXFe::EFont Font, const FString& Str, float Scale) const;
	float FontHeight(SSXFe::EFont Font, float Scale) const;
	float TextBoxWidth(SSXFe::EFont Font, const FString& Str, float Scale) const;
	bool GlyphInk(SSXFe::EFont Font, TCHAR Ch, float Scale, float& OutXoff, float& OutW) const;
	/** Text box: greedy word wrap to Width, lines at the font height. TODO: the multi-line text box widget's own line pitch / wrap rule is not recovered; lines are stacked at lineHeight*scale*ScaleY with no extra spacing. */
	float TextBox(SSXFe::EFont Font, float X, float Y, float Width, float Scale, const FLinearColor& Color, const FString& Str,
		ESSXAlign Align = ESSXAlign::Left, bool bShadow = false, int32 Layer = 1);

	TArray<FSSXFeDrawCmd> Cmds;
	TArray<FString> Texts;
	TArray<FSSXFeTextRec> TextRecs;
	const FSSXFeAssets* Assets = nullptr;

private:
	void Glyphs(const FSSXBitmapFont& F, float X, float Y, float Scale, const FLinearColor& Color, const FString& Line, int32 Layer);
};

// Paints an FSSXFeDrawList in a 640x480 frame centred in its geometry (aspect kept).
class SSXTRICKYREBUILD_API SSSXFeCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSSXFeCanvas) {}
		SLATE_ARGUMENT(const FSSXFeDrawList*, DrawList)
		SLATE_ARGUMENT(bool, FillBlack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(SSXFe::ScreenW, SSXFe::ScreenH); }

private:
	const FSlateBrush* BrushFor(UObject* Resource) const;
	const FSSXFeDrawList* DrawList = nullptr;
	bool bFillBlack = false;
	mutable TMap<TWeakObjectPtr<UObject>, TSharedPtr<FSlateBrush>> Brushes;
};

struct FSSXPromptScope
{
	FSSXFeDrawList& D;
	ESSXPromptContext Old;
	FSSXPromptScope(FSSXFeDrawList& InD, ESSXPromptContext Ctx) : D(InD), Old(InD.PromptContext) { D.PromptContext = Ctx; }
	~FSSXPromptScope() { D.PromptContext = Old; }
};
