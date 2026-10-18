#include "Effects/SSXSplineLineComponent.h"

#include "DynamicMeshBuilder.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "SceneInterface.h"
#include "SceneView.h"

namespace
{
	class FSSXSplineLineSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		explicit FSSXSplineLineSceneProxy(const USSXSplineLineComponent* C, const TArray<FVector3f>& InPoints, FColor InColour, float InFar)
			: FPrimitiveSceneProxy(C)
			, Material(C->GetLineMaterial() ? C->GetLineMaterial()->GetRenderProxy() : nullptr)
			, MaterialRelevance(C->GetMaterialRelevance(GetScene().GetShaderPlatform()))
			, Points(InPoints)
			, Colour(InColour)
			, Far(InFar)
		{
		}

		virtual SIZE_T GetTypeHash() const override { static size_t Unique; return reinterpret_cast<size_t>(&Unique); }

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
			uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			constexpr int32 PPS = USSXSplineLineComponent::PointsPerStrip;
			if (!Material || Points.Num() < PPS)
			{
				return;
			}
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1u << ViewIndex)))
				{
					continue;
				}
				const FSceneView* View = Views[ViewIndex];
				const FVector3f Eye = FVector3f(View->ViewMatrices.GetViewOrigin());
				const FVector3f Dir = FVector3f(View->GetViewDirection());
				const bool bPersp = View->IsPerspectiveProjection();
				const float P11 = float(View->ViewMatrices.GetViewToClip().M[1][1]);
				if (P11 <= 0.f)
				{
					continue;
				}
				const float HalfPerDepth = 1.f / (P11 * USSXSplineLineComponent::Ps2ViewLines);
				const float Near = bPersp ? FMath::Max(View->NearClippingDistance, 0.01f) : -UE_BIG_NUMBER;
				FDynamicMeshBuilder MB(View->GetFeatureLevel());
				const int32 NStrips = Points.Num() / PPS;
				MB.ReserveVertices(NStrips * (PPS - 1) * 4);
				MB.ReserveTriangles(NStrips * (PPS - 1) * 2);
				for (int32 S = 0; S < NStrips; ++S)
				{
					for (int32 I = 0; I + 1 < PPS; ++I)
					{
						FVector3f A = Points[S * PPS + I], B = Points[S * PPS + I + 1];
						float Da = FVector3f::DotProduct(A - Eye, Dir), Db = FVector3f::DotProduct(B - Eye, Dir);
						if ((Da < Near && Db < Near) || (Da > Far && Db > Far))
						{
							continue;
						}
						auto ClipTo = [](FVector3f& P, float& D, const FVector3f& Q, float Dq, float Plane)
						{
							const float T = (Plane - D) / (Dq - D);
							P = P + (Q - P) * T;
							D = Plane;
						};
						if (Da < Near) { ClipTo(A, Da, B, Db, Near); }
						else if (Db < Near) { ClipTo(B, Db, A, Da, Near); }
						if (Da > Far) { ClipTo(A, Da, B, Db, Far); }
						else if (Db > Far) { ClipTo(B, Db, A, Da, Far); }
						const FVector3f ToEye = bPersp ? (Eye - (A + B) * 0.5f) : -Dir;
						const FVector3f Side = FVector3f::CrossProduct(B - A, ToEye).GetSafeNormal();
						if (Side.IsNearlyZero())
						{
							continue;
						}
						const float Ha = HalfPerDepth * (bPersp ? Da : 1.f), Hb = HalfPerDepth * (bPersp ? Db : 1.f);
						const FVector3f N = ToEye.GetSafeNormal();
						const int32 V0 = MB.AddVertex(FDynamicMeshVertex(A - Side * Ha, Side, N, FVector2f(0, 0), Colour));
						MB.AddVertex(FDynamicMeshVertex(A + Side * Ha, Side, N, FVector2f(1, 0), Colour));
						MB.AddVertex(FDynamicMeshVertex(B + Side * Hb, Side, N, FVector2f(1, 1), Colour));
						MB.AddVertex(FDynamicMeshVertex(B - Side * Hb, Side, N, FVector2f(0, 1), Colour));
						MB.AddTriangle(V0, V0 + 1, V0 + 2);
						MB.AddTriangle(V0, V0 + 2, V0 + 3);
					}
				}
				MB.GetMesh(FMatrix::Identity, Material, SDPG_World, true, false, ViewIndex, Collector);
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bRenderInMainPass = true;
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			return Result;
		}

		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize() + Points.GetAllocatedSize(); }

	private:
		const FMaterialRenderProxy* Material;
		FMaterialRelevance MaterialRelevance;
		TArray<FVector3f> Points;
		FColor Colour;
		float Far;
	};
}

USSXSplineLineComponent::USSXSplineLineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bReceivesDecals = false;
	bUseAsOccluder = false;
	SetMobility(EComponentMobility::Movable);
	// 0x251748
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Line(MaterialPath);
	LineMaterial = Line.Object;
}

void USSXSplineLineComponent::SetStrips(TArray<FVector3f>&& InPoints, FColor InColour)
{
	Points = MoveTemp(InPoints);
	Colour = InColour;
	UpdateBounds();
	MarkRenderStateDirty();
}

void USSXSplineLineComponent::SetFarDistance(float InFar)
{
	if (InFar != FarDistance)
	{
		FarDistance = InFar;
		MarkRenderStateDirty();
	}
}

FPrimitiveSceneProxy* USSXSplineLineComponent::CreateSceneProxy()
{
	if (!LineMaterial || Points.Num() < PointsPerStrip)
	{
		return nullptr;
	}
	return new FSSXSplineLineSceneProxy(this, Points, Colour, FarDistance);
}

FBoxSphereBounds USSXSplineLineComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (Points.Num() == 0)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
	}
	FBox Box(ForceInit);
	for (const FVector3f& P : Points) { Box += FVector(P); }
	return FBoxSphereBounds(Box.ExpandBy(100.0));
}

void USSXSplineLineComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool) const
{
	if (LineMaterial) { OutMaterials.Add(LineMaterial); }
}
