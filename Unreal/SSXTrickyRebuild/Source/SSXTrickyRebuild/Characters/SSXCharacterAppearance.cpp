#include "Characters/SSXCharacterAppearance.h"

#include "Characters/SSXCharacterLibrary.h"
#include "Characters/SSXRiderChrome.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SSXTrickyRebuild.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/RiderSim.h"
#include "UI/SSXFrontEndSubsystem.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "World/SSXWorldPresentation.h"

namespace
{
	// Stand-in deck height when the board is missing (cm).
	constexpr double DefaultBoardTop = 7.0;
}

USSXCharacterAppearance::USSXCharacterAppearance()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
}

USSXCharacterAppearance::~USSXCharacterAppearance() = default;

bool USSXCharacterAppearance::Apply(USceneComponent* BoardParent, USceneComponent* BodyParent, const FString& CharacterId,
	int32 CharacterIndex, int32 BoardClass, int32 Board, int32 Outfit)
{
	bPoseCurValid = false;
	BoardParentRef = BoardParent;
	BodyParentRef = BodyParent;
	RequestedId = CharacterId;
	RequestedIndex = CharacterIndex;
	RequestedBoardClass = BoardClass;
	RequestedBoard = Board;
	RequestedOutfit = Outfit;
	LastConsoleValue = SSXCharacters::ConsoleOverride();
	AppliedTextures.Reset();

	AActor* Owner = GetOwner();
	const FString Id = SSXCharacters::ResolveId(CharacterId, CharacterIndex);
	USkeletalMesh* Skel = SSXCharacters::LoadRiderMesh(Id);
	UStaticMesh* BoardModel = Skel ? SSXCharacters::LoadBoardMesh(Id, BoardClass) : nullptr;
	BoardMeshClass = BoardClass;
	RegularBoardModel = BoardModel;
	GoofyBoardModel = Skel ? SSXCharacters::LoadBoardMesh(Id, BoardClass, true) : nullptr;
	bBoardMeshGoofy = false;
	if (!Owner || !BodyParent || !BoardParent || !Skel)
	{
		if (RiderMesh) { RiderMesh->SetVisibility(false); }
		if (BoardMesh) { BoardMesh->SetVisibility(false); }
		SetPlaceholderVisible(true);
		ShownId.Reset();
		UE_LOG(LogSSX, Display, TEXT("[character] '%s' not imported (run tools/convert_characters.py + Scripts/import_characters.py); using the placeholder"), *Id);
		return false;
	}

	if (!RiderMesh)
	{
		RiderMesh = NewObject<UPoseableMeshComponent>(Owner, TEXT("CharacterMesh"));
		RiderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RiderMesh->SetGenerateOverlapEvents(false);
		ConfigureShadowFlags(*RiderMesh);
		RiderMesh->SetUsingAbsoluteScale(true);
		RiderMesh->SetupAttachment(BodyParent);
		RiderMesh->RegisterComponent();
		Owner->AddInstanceComponent(RiderMesh);
	}
	if (!BoardMesh)
	{
		BoardMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("CharacterBoard"));
		BoardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoardMesh->SetGenerateOverlapEvents(false);
		ConfigureShadowFlags(*BoardMesh);
		BoardMesh->SetupAttachment(BoardParent);
		BoardMesh->RegisterComponent();
		Owner->AddInstanceComponent(BoardMesh);
	}

	RiderMesh->SetSkinnedAssetAndUpdate(Skel);
	RiderMesh->SetVisibility(true);
	Animator.Reset();
	AnimBoardClass = AnimCharacter = -1;
	if (bAnimated)
	{
		bAnimated = false;
		RiderMesh->AttachToComponent(BodyParent, FAttachmentTransformRules::KeepRelativeTransform);
	}
	BoardMesh->SetStaticMesh(BoardModel);
	BoardMesh->SetVisibility(BoardModel != nullptr);
	BoardTop = BoardModel ? BoardModel->GetBoundingBox().Max.Z : DefaultBoardTop;
	ApplySelectionTextures(Id, RequestedBoard, RequestedOutfit);
	UpdateStanceTransform();
	SetPlaceholderVisible(false);
	ShownId = Id;
	UE_LOG(LogSSX, Display, TEXT("[character] showing %s (%d bones), board %s"), *Id,
		Skel->GetRefSkeleton().GetNum(), BoardModel ? *BoardModel->GetName() : TEXT("none"));
	return true;
}

void USSXCharacterAppearance::ConfigureShadowFlags(UPrimitiveComponent& Part)
{
	Part.SetCastShadow(false);
	Part.bCastDynamicShadow = false;
	Part.bCastStaticShadow = false;
	Part.bReceivesDecals = false;
	Part.SetRenderCustomDepth(true);
}

UTexture2D* USSXCharacterAppearance::LoadTexture(const FString& Path)
{
	if (Path.IsEmpty() || !FPaths::FileExists(Path))
	{
		return nullptr;
	}
	if (USSXFrontEndSubsystem* FrontEnd = USSXFrontEndSubsystem::Get(GetOwner()))
	{
		return FrontEnd->LoadPng(Path);
	}
	UTexture2D* Tex = FImageUtils::ImportFileAsTexture2D(Path);
	if (Tex)
	{
		Tex->SRGB = true;
		Tex->UpdateResource();
	}
	return Tex;
}

void USSXCharacterAppearance::ApplySelectionTextures(const FString& Id, int32 Board, int32 Outfit)
{
	SelectionRefs.Reset();
	if (RiderMesh) { RiderMesh->EmptyOverrideMaterials(); }
	if (BoardMesh) { BoardMesh->EmptyOverrideMaterials(); }
	struct FSlot { const TCHAR* Material; const TCHAR* File; int32 Number; UMeshComponent* Mesh; };
	const FSlot Slots[] = {
		{ TEXT("_board"), TEXT("bord"), SSXCharacters::BoardTextureNumber(Board), BoardMesh },
		{ TEXT("_suit"), TEXT("suit"), SSXCharacters::OutfitTextureNumber(Outfit), RiderMesh },
		{ TEXT("_boot"), TEXT("boot"), SSXCharacters::OutfitTextureNumber(Outfit), RiderMesh },
	};
	for (const FSlot& S : Slots)
	{
		if (!S.Mesh || (!bMalloraLook && S.Number == INDEX_NONE))
		{
			continue;
		}
		const FString Path = bMalloraLook ? SSXCharacters::NamedTexturePath(SSXCharacters::MalloraTextureName(S.File), S.File)
			: SSXCharacters::OutfitTexturePath(Id, S.File, S.Number);
		UTexture2D* Tex = LoadTexture(Path);
		if (!Tex)
		{
			UE_LOG(LogSSX, Verbose, TEXT("[character] %s missing; imported texture kept"), *Path);
			continue;
		}
		for (int32 M = 0; M < S.Mesh->GetNumMaterials(); ++M)
		{
			UMaterialInterface* Mat = S.Mesh->GetMaterial(M);
			if (!Mat || !Mat->GetName().EndsWith(S.Material))
			{
				continue;
			}
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Mat, this);
			TArray<FMaterialParameterInfo> Params;
			TArray<FGuid> Ids;
			Mat->GetAllTextureParameterInfo(Params, Ids);
			for (const FMaterialParameterInfo& P : Params)
			{
				UTexture* Cur = nullptr;
				if (Mat->GetTextureParameterValue(P, Cur) && Cur && Cur->GetName().EndsWith(S.Material))
				{
					Mid->SetTextureParameterValue(P.Name, Tex);
				}
			}
			S.Mesh->SetMaterial(M, Mid);
			SelectionRefs.Add(Mid);
		}
		SelectionRefs.Add(Tex);
		AppliedTextures.Add(Path);
	}
	if (Outfit == SSXRiderChrome::ChromeOutfit)
	{
		ApplyChrome();
	}
}

void USSXCharacterAppearance::ApplyChrome()
{
	UMaterialInterface* Chrome = LoadObject<UMaterialInterface>(nullptr, SSXRiderChrome::MaterialPath);
	UMaterialInterface* ChromeMasked = LoadObject<UMaterialInterface>(nullptr, SSXRiderChrome::MaskedMaterialPath);
	UTexture2D* Env = LoadTexture(SSXRiderChrome::EnvTexturePath());
	if (!Chrome || !Env)
	{
		UE_LOG(LogSSX, Warning, TEXT("[character] Chrome outfit drawn without its reflection: %s%s"),
			Chrome ? TEXT("") : TEXT("M_SSXRiderLitChrome missing (Scripts/make_world_materials.py) "),
			Env ? TEXT("") : *FString::Printf(TEXT("%s missing (tools/convert_characters.py)"), *SSXRiderChrome::EnvTexturePath()));
		return;
	}
	SelectionRefs.Add(Env);
	for (UMeshComponent* Mesh : { static_cast<UMeshComponent*>(RiderMesh.Get()), static_cast<UMeshComponent*>(BoardMesh.Get()) })
	{
		for (int32 M = 0; Mesh && M < Mesh->GetNumMaterials(); ++M)
		{
			UMaterialInterface* Mat = Mesh->GetMaterial(M);
			if (!Mat || !SSXRiderChrome::IsEnvPassMaterial(Mat))
			{
				continue;
			}
			UMaterialInterface* Parent = (Mat->GetBlendMode() == BLEND_Masked && ChromeMasked) ? ChromeMasked : Chrome;
			if (UMaterialInstanceDynamic* Mid = USSXWorldPresentation::MakeRiderMid(*Mat, Parent, this))
			{
				Mid->SetTextureParameterValue(TEXT("EnvTex"), Env);
				Mesh->SetMaterial(M, Mid);
				SelectionRefs.Add(Mid);
			}
		}
	}
}

UStaticMesh* USSXCharacterAppearance::LoadDrawnBoardMesh(const FString& Id, int32 BoardClass, bool bGoofyProfile, bool& bOutGoofyMesh)
{
	UStaticMesh* Goofy = bGoofyProfile ? SSXCharacters::LoadBoardMesh(Id, BoardClass, true) : nullptr;
	bOutGoofyMesh = Goofy != nullptr;
	return Goofy ? Goofy : SSXCharacters::LoadBoardMesh(Id, BoardClass);
}

void USSXCharacterAppearance::SelectBoardModel(bool bGoofyProfile)
{
	const bool bGoofyMesh = bGoofyProfile && GoofyBoardModel != nullptr;
	UStaticMesh* Want = bGoofyMesh ? GoofyBoardModel.Get() : RegularBoardModel.Get();
	if (!BoardMesh || !Want || (bGoofyMesh == bBoardMeshGoofy && BoardMesh->GetStaticMesh() == Want))
	{
		return;
	}
	BoardMesh->SetStaticMesh(Want);
	bBoardMeshGoofy = bGoofyMesh;
	BoardTop = Want->GetBoundingBox().Max.Z;
}

void USSXCharacterAppearance::SetGoofy(bool bGoofy)
{
	if (bGoofy != bIsGoofy)
	{
		bIsGoofy = bGoofy;
		UpdateStanceTransform();
	}
}

void USSXCharacterAppearance::UpdateStanceTransform()
{
	if (bAnimated)
	{
		return;
	}
	const FRotator Yaw(0.f, bIsGoofy ? 180.f : 0.f, 0.f);
	if (RiderMesh)
	{
		RiderMesh->SetRelativeLocationAndRotation(FVector(0, 0, BoardTop), Yaw);
	}
	if (BoardMesh)
	{
		BoardMesh->SetRelativeLocationAndRotation(FVector::ZeroVector, Yaw);
	}
}

void USSXCharacterAppearance::SetPlaceholderVisible(bool bVisible)
{
	TArray<UStaticMeshComponent*> Parts;
	if (AActor* Owner = GetOwner())
	{
		Owner->GetComponents(Parts);
	}
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part == BoardMesh)
		{
			continue;
		}
		for (const FName& Tag : Part->ComponentTags)
		{
			if (Tag.ToString().StartsWith(TEXT("Color=")))
			{
				Part->SetVisibility(bVisible);
				break;
			}
		}
	}
}

void USSXCharacterAppearance::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const FString Console = SSXCharacters::ConsoleOverride();
	if (Console != LastConsoleValue && BodyParentRef && BoardParentRef)
	{
		Apply(BoardParentRef, BodyParentRef, RequestedId, RequestedIndex, RequestedBoardClass, RequestedBoard, RequestedOutfit);
	}
}
