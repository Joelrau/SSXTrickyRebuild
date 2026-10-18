// 0x251748 / 0x1FABC8 / 0x1C72E8 / 0x1E3260
#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "SSXSplineLineComponent.generated.h"

class UMaterialInterface;

UCLASS(ClassGroup = Rendering)
class SSXTRICKYREBUILD_API USSXSplineLineComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	// 0x251748
	static constexpr int32 PointsPerStrip = 10;
	static constexpr float Ps2ViewLines = 448.f;
	static constexpr const TCHAR* MaterialPath = TEXT("/Game/World/M_SSXLine.M_SSXLine");

	USSXSplineLineComponent();

	void SetStrips(TArray<FVector3f>&& InPoints, FColor InColour);
	void SetFarDistance(float InFar);
	int32 NumPoints() const { return Points.Num(); }
	FColor GetColour() const { return Colour; }
	UMaterialInterface* GetLineMaterial() const { return LineMaterial; }

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterialInterface* GetMaterial(int32 Index) const override { return Index == 0 ? LineMaterial.Get() : nullptr; }
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

private:
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LineMaterial;
	TArray<FVector3f> Points;
	FColor Colour = FColor::Black;
	float FarDistance = 1.0e9f;
};
