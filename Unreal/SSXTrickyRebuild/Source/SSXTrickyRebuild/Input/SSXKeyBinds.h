// PC keyboard bindings of the 16 PS2 pad buttons (KEYBINDS options page, user request; not in the original).
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "SSXKeyBinds.generated.h"

namespace SSXKeyBinds
{
	constexpr int32 NumButtons = 16;
	constexpr int32 NumSlots = 2;

	/** Rows in page order: UP DOWN LEFT RIGHT CROSS SQUARE CIRCLE TRIANGLE L1 R1 L2 R2 L3 R3 SELECT START. */
	struct FButton { uint32 Bit; const TCHAR* Name; };
	SSXTRICKYREBUILD_API const FButton& Button(int32 Row);

	/** Keys[row][slot]; slot 0 primary, 1 alternate; EKeys::Invalid = unbound. */
	struct FSet
	{
		FKey Keys[NumButtons][NumSlots];
	};

	SSXTRICKYREBUILD_API const FSet& Defaults();
	/** The bindings in use (loaded from GameUserSettings.ini on first use). */
	SSXTRICKYREBUILD_API const FSet& Current();
	SSXTRICKYREBUILD_API void SetCurrent(const FSet& In);

	/** Keys the page cannot bind: Escape (cancel / back / pause), Delete (clears a slot), BackSpace (restart),
	 *  I J K L (keyboard right stick), and every gamepad or mouse key. */
	SSXTRICKYREBUILD_API bool IsReserved(const FKey& Key);
	/** Bind Key to [Row][Slot]; a slot that already holds Key takes this slot's old key, so no key is lost. */
	SSXTRICKYREBUILD_API void Assign(FSet& Set, int32 Row, int32 Slot, const FKey& Key);
	/** SSXPadButton bits of a keyboard key under the current bindings (0 if unbound). */
	SSXTRICKYREBUILD_API uint32 PadBits(const FKey& Key);
	/** Upper-case name shown on the page; "-" when unbound. */
	SSXTRICKYREBUILD_API FString KeyText(const FKey& Key);
}

/** KEYBINDS page bindings, saved in GameUserSettings.ini, never on the memory card. */
UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class SSXTRICKYREBUILD_API USSXKeyBindSettings : public UObject
{
	GENERATED_BODY()

public:
	/** 32 key names, row-major (row 0 primary, row 0 alternate, row 1 ...); "None" = unbound. */
	UPROPERTY(config)
	TArray<FString> Keys;

	/** Store SSXKeyBinds::Current() and write the ini. */
	static void Save();
};
