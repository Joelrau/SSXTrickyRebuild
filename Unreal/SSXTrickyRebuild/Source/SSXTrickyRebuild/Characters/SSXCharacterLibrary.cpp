#include "Characters/SSXCharacterLibrary.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UI/SSXFrontEndData.h"

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<FString> CVarCharacter(
		TEXT("ssx.Character"), TEXT(""),
		TEXT("Rider model to show: id (eddie kaori luther mac moby zoe jp elise psymon seeiah brodi marisol mmm) ")
		TEXT("or character index 0..12. Empty = the pawn's CharacterId / Character. Takes effect immediately."),
		ECVF_Default);
#endif

	template <typename T>
	T* FindByName(const FString& Id, const FString& AssetName)
	{
		IAssetRegistry* RegistryPtr = IAssetRegistry::Get();
		if (!RegistryPtr)
		{
			return nullptr;
		}
		IAssetRegistry& Registry = *RegistryPtr;
		const FString Folder = FString::Printf(TEXT("/Game/Characters/%s"), *Id);
		if (Registry.IsLoadingAssets())
		{
			Registry.ScanPathsSynchronous({ Folder }, false);
		}
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(T::StaticClass()->GetClassPathName());
		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);
		for (const FAssetData& Data : Found)
		{
			if (Data.AssetName.ToString().Equals(AssetName, ESearchCase::IgnoreCase))
			{
				return Cast<T>(Data.GetAsset());
			}
		}
		return nullptr;
	}
}

namespace SSXCharacters
{
	const TArray<FString>& AllIds()
	{
		static const TArray<FString> Ids = { TEXT("eddie"), TEXT("kaori"), TEXT("luther"), TEXT("mac"), TEXT("moby"),
			TEXT("zoe"), TEXT("jp"), TEXT("elise"), TEXT("psymon"), TEXT("seeiah"), TEXT("brodi"), TEXT("marisol"),
			TEXT("zz_mmm") };
		return Ids;
	}

	FString IdForIndex(int32 Index)
	{
		return AllIds().IsValidIndex(Index) ? AllIds()[Index] : FString();
	}

	FString NormalizeId(const FString& NameOrIndex)
	{
		const FString S = NameOrIndex.TrimStartAndEnd().ToLower();
		if (S.IsEmpty())
		{
			return FString();
		}
		if (S.IsNumeric())
		{
			return IdForIndex(FCString::Atoi(*S));
		}
		if (S == TEXT("mmm") || S == TEXT("mix master mike"))
		{
			return TEXT("zz_mmm");
		}
		return AllIds().Contains(S) ? S : FString();
	}

	FString ConsoleOverride()
	{
#if !UE_BUILD_SHIPPING
		return CVarCharacter.GetValueOnGameThread();
#else
		return FString();
#endif
	}

	FString ResolveId(const FString& PawnCharacterId, int32 CharacterIndex)
	{
		FString Id = NormalizeId(ConsoleOverride());
		if (Id.IsEmpty())
		{
			Id = NormalizeId(PawnCharacterId);
		}
		if (Id.IsEmpty())
		{
			Id = IdForIndex(CharacterIndex);
		}
		return Id;
	}

	const TCHAR* BoardSuffix(int32 BoardClass)
	{
		switch (BoardClass)
		{
		case 0: return TEXT("bx");
		case 1: return TEXT("fr");
		default: return TEXT("al");
		}
	}

	USkeletalMesh* LoadRiderMesh(const FString& Id)
	{
		return Id.IsEmpty() ? nullptr : FindByName<USkeletalMesh>(Id, TEXT("SK_") + Id);
	}

	UStaticMesh* LoadBoardMesh(const FString& Id, int32 BoardClass, bool bGoofyModel)
	{
		return Id.IsEmpty() ? nullptr
			: FindByName<UStaticMesh>(Id, FString::Printf(TEXT("SM_%s_board_%s%s"), *Id, BoardSuffix(BoardClass),
				bGoofyModel ? TEXT("_goofy") : TEXT("")));
	}

	int32 BoardTextureNumber(int32 Board)
	{
		return Board >= 0 && Board < 12 ? Board + 1 : INDEX_NONE;
	}

	int32 OutfitTextureNumber(int32 Outfit)
	{
		// 0x3CD0E0
		return Outfit >= 0 && Outfit <= 6 ? FMath::Min(Outfit, 5) + 1 : INDEX_NONE;
	}

	FString TexturePrefix(const FString& Id)
	{
		return Id == TEXT("zz_mmm") ? FString(TEXT("mmm")) : Id;
	}

	FString OutfitTexturePath(const FString& Id, const TCHAR* Slot, int32 Number)
	{
		if (Id.IsEmpty() || Number == INDEX_NONE)
		{
			return FString();
		}
		return NamedTexturePath(FString::Printf(TEXT("%s%d_%s"), *TexturePrefix(Id), Number, Slot), Slot);
	}

	FString NamedTexturePath(const FString& Name, const TCHAR* Slot)
	{
		return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("textures"), TEXT("CHAR"), TEXT("TEXPS2"),
			TEXT("data"), TEXT("char"), Name, FString::Printf(TEXT("000_%s.png"), Slot));
	}

	const TCHAR* MalloraTextureName(const TCHAR* Slot)
	{
		// 0x341A38, 0x341A3C, 0x341D50
		if (FCString::Strcmp(Slot, TEXT("suit")) == 0) { return TEXT("mallora_suit"); }
		if (FCString::Strcmp(Slot, TEXT("boot")) == 0) { return TEXT("elise1_boot"); }
		if (FCString::Strcmp(Slot, TEXT("bord")) == 0) { return TEXT("mallora_bord"); }
		return nullptr;
	}
}
