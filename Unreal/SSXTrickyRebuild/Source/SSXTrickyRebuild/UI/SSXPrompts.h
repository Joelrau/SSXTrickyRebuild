#pragma once

#include "CoreMinimal.h"

struct FSSXSprite;
class UTexture;

/** Which key map a prompt describes: the menus / race overlays (USSXFeWidget::PadBitForKey), racing (SSXInputSetup.h),
 *  or the original PS2 glyphs whatever the device (the lessons). */
enum class ESSXPromptContext : uint8 { Menu, Race, Original };

enum class ESSXPromptDevice : uint8 { PS2, Keyboard, Xbox, PS4, PS5 };

/** Button prompts for the device in use (deliberate difference from the original, cvar ssx.UI.Prompts): the HUD button
 *  sprites 0x62..0x75 and the front end's face buttons 79..82 are drawn with the images in <Project>/Prompts
 *  (tools/import_prompts.py), fitted into the original sprite's box. */
namespace SSXPrompts
{
	/** The prompt set to draw: ssx.UI.Prompts 0 = PS2, 1 = the last device used (keyboard until a pad is used or attached),
	 *  2.. = forced. */
	SSXTRICKYREBUILD_API ESSXPromptDevice Device();
	/** The last input came from a gamepad (true) or the keyboard / mouse. */
	SSXTRICKYREBUILD_API void NoteInput(bool bGamepad);
	/** <set>/<name> under <Project>/Prompts for a sprite, empty to keep the original. */
	SSXTRICKYREBUILD_API FString ImageFor(int32 Id, bool bHud, ESSXPromptContext Ctx, ESSXPromptDevice Dev);
	/** Replaces Sprite / Page with the prompt image (same W / H, FitAspect set) when one applies. */
	SSXTRICKYREBUILD_API bool Substitute(int32 Id, bool bHud, ESSXPromptContext Ctx, FSSXSprite& Sprite, UTexture*& Page);
	SSXTRICKYREBUILD_API FString Dir();
}
