#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UStaticMesh;

namespace SSXCharacters
{
	SSXTRICKYREBUILD_API const TArray<FString>& AllIds();

	SSXTRICKYREBUILD_API FString IdForIndex(int32 Index);

	SSXTRICKYREBUILD_API FString NormalizeId(const FString& NameOrIndex);

	SSXTRICKYREBUILD_API FString ResolveId(const FString& PawnCharacterId, int32 CharacterIndex);

	SSXTRICKYREBUILD_API const TCHAR* BoardSuffix(int32 BoardClass);

	SSXTRICKYREBUILD_API USkeletalMesh* LoadRiderMesh(const FString& Id);
	// FUN_00201138, 0x341998
	SSXTRICKYREBUILD_API UStaticMesh* LoadBoardMesh(const FString& Id, int32 BoardClass, bool bGoofyModel = false);

	// 0x3CD620, 0x3CD0E0
	SSXTRICKYREBUILD_API int32 BoardTextureNumber(int32 Board);
	SSXTRICKYREBUILD_API int32 OutfitTextureNumber(int32 Outfit);
	SSXTRICKYREBUILD_API FString TexturePrefix(const FString& Id);
	SSXTRICKYREBUILD_API FString OutfitTexturePath(const FString& Id, const TCHAR* Slot, int32 Number);
	SSXTRICKYREBUILD_API FString NamedTexturePath(const FString& Name, const TCHAR* Slot);
	// 0x3352B0
	SSXTRICKYREBUILD_API const TCHAR* MalloraTextureName(const TCHAR* Slot);

	SSXTRICKYREBUILD_API FString ConsoleOverride();
}
