// TODO: render states 56 / 57 / 61 around the particle draw (probably depth write off and an alpha test 0.08) are not 
#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "SSXSpriteRenderer.generated.h"

class UMaterialInterface;

struct FSSXSpriteVertexData
{
	FVector3f Pos = FVector3f::ZeroVector;   // Unreal world space centre
	float Half = 0.f;                        // half-extent (cm)
	FColor Color = FColor::White;            // GS bytes (128 = 1.0), alpha GS (128 = 1.0)
	FVector4f UV = FVector4f(0, 0, 1, 1);
	FVector4f Occ = FVector4f(0, 0, 0, 0);    // occlusion batches only: point (Unreal world), pull-in (cm)
	FVector2f Block = FVector2f(0, 0);
	float HalfY = -1.f;
};

struct FSSXStripVertex
{
	FVector3f Pos = FVector3f::ZeroVector;   // Unreal world space
	FColor Color = FColor::White;            // GS bytes (128 = 1.0)
	FVector2f UV = FVector2f::ZeroVector;
	FVector4f Occ = FVector4f(0, 0, 0, 0);
	FVector2f Block = FVector2f(0, 0);
};

struct FSSXSpriteBatch
{
	UMaterialInterface* Material = nullptr;
	TArray<FSSXSpriteVertexData> Sprites;
	TArray<TArray<FSSXStripVertex>> Strips;
	/** Lens FX batches: every vertex also carries texcoord 1 = (Occ.x, Occ.y), 2 = (Occ.z, Occ.w), 3 = Block, read by the lens materials' occlusion node. */
	bool bOcclusion = false;
};

UCLASS(ClassGroup = Rendering)
class SSXTRICKYREBUILD_API USSXSpriteRenderComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	USSXSpriteRenderComponent();

	void SetBatches(TArray<FSSXSpriteBatch>&& InBatches);
	void SetBaseMaterials(const TArray<UMaterialInterface*>& InBases);
	const TArray<FSSXSpriteBatch>& GetBatches() const { return Batches; }

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return Bases.Num(); }
	virtual UMaterialInterface* GetMaterial(int32 Index) const override { return Bases.IsValidIndex(Index) ? Bases[Index] : nullptr; }
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual void SendRenderDynamicData_Concurrent() override;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Bases;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Used;
	TArray<FSSXSpriteBatch> Batches;
};
