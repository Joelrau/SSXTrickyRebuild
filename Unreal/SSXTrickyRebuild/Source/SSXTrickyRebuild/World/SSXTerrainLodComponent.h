#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "World/SSXTerrainLod.h"
#include "SSXTerrainLodComponent.generated.h"

class UStaticMeshComponent;

struct FSSXTerrainLodRenderData
{
	struct FSection
	{
		int32 FirstPatch = 0;
		int32 NumPatches = 0;
	};
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	TArray<FVector2f> Uv0, Uv1, Uv2;
	TArray<FVector3f> Corners;
	TArray<FSection> Sections;
	FBox Bounds = FBox(ForceInit);
	int32 NumPatches() const { return Corners.Num() / 4; }
};

UCLASS(ClassGroup = Rendering)
class SSXTRICKYREBUILD_API USSXTerrainLodComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	USSXTerrainLodComponent();

	/** Builds the draw data from Data's patches of one kind (ShowOff or not) with one material per texture, vertices in the
	 *  space of LocalToWorld (the replaced static mesh component's); false (nothing set) when a texture has no material. */
	bool Init(const FSSXTerrainLodData& Data, bool bShowOff, const TMap<int32, UMaterialInterface*>& MaterialForTexture,
		const FTransform& LocalToWorld);

	/** A LOD component (not registered) drawing what the terrain static mesh component C draws ("<stem>_terrain" or
	 *  "<stem>_showoff_terrain", materials by the slots' tex_NNN names), or null on a materials / data mismatch. */
	static USSXTerrainLodComponent* CreateFor(UStaticMeshComponent& C, const FSSXTerrainLodData& Data, UObject* Outer);

	/** On every static mesh component in World whose mesh is "<stem>_terrain" / "<stem>_showoff_terrain" with
	 *  SimData/<stem>.terrainlod.bin present: add the LOD component and hide the static mesh (or the reverse when
	 *  ssx.TerrainLod is 0). Returns the number of terrain meshes switched. */
	static int32 AttachToTerrain(UWorld& World, TArray<TWeakObjectPtr<USSXTerrainLodComponent>>& OutAttached);
	/** Applies ssx.TerrainLod (1 = this component, 0 = the static k/7 mesh) to the attached components; cheap, called each
	 *  frame by USSXWorldPresentation. */
	static void ApplyCVar(TConstArrayView<TWeakObjectPtr<USSXTerrainLodComponent>> Attached);

	const TSharedPtr<const FSSXTerrainLodRenderData>& GetRenderData() const { return RenderData; }

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Replaced;

private:
	TSharedPtr<const FSSXTerrainLodRenderData> RenderData;
};
