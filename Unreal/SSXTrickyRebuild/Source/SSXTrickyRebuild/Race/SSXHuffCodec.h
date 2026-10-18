// 0x243A60, 0x2438C0, 0x242578, 0x2426F8, 0x243368, 0x241E28, 0x240EF8, 0x1942F0, 0x194550, 0x242320
#pragma once

#include "CoreMinimal.h"

// EA HUFF (0x30FB + 24-bit size). Bit stream MSB first: clue byte; per code length the number of codes; the symbols as
// skip counts over the unlisted byte values; then the data: a canonical code, or clue + number n (n > 0: the previous
// byte repeated n times; n = 0 then bit 0 + 8 bits: a literal byte; n = 0 then bit 1: the end).
namespace SSXHuff
{
	constexpr uint16 TypeHuff = 0x30FB;

	SSXTRICKYREBUILD_API void Encode(const uint8* Src, int32 Size, TArray<uint8>& Out);
	SSXTRICKYREBUILD_API bool Decode(const uint8* In, int32 InNum, TArray<uint8>& Out, int32* OutConsumed = nullptr);
	SSXTRICKYREBUILD_API int32 DecodedSize(const uint8* In, int32 InNum);
}
