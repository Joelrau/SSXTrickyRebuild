// 0x1FABC8 / 0x251748 / 0x1E1F70 / 0x1CE278
#include "Effects/SSXTrackEffects.h"

#include "Effects/SSXSplineLineComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "World/SSXWorldPresentation.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Rendering/ColorVertexBuffer.h"
#include "StaticMeshComponentLODInfo.h"
#include "StaticMeshResources.h"

using namespace SSXEffects;

namespace
{
	FMatrix GameMatrixToUnreal(const FMatrix& M)
	{
		static const double Sg[4] = { 1.0, -1.0, 1.0, 1.0 };
		FMatrix Out;
		for (int32 I = 0; I < 4; ++I)
		{
			for (int32 J = 0; J < 4; ++J)
			{
				Out.M[I][J] = M.M[I][J] * Sg[I] * Sg[J];
			}
		}
		return Out;
	}

	// Vertex colour 128 on every vertex of LOD 0 (FColor = the GS byte, as the imported COLOR_0).
	void OverrideVertexColour128(UStaticMeshComponent* C)
	{
		const UStaticMesh* Mesh = C->GetStaticMesh();
		const FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!RD || RD->LODResources.Num() == 0)
		{
			return;
		}
		C->SetLODDataCount(1, C->LODData.Num());
		FStaticMeshComponentLODInfo& L = C->LODData[0];
		if (L.OverrideVertexColors)
		{
			return;
		}
		L.OverrideVertexColors = new FColorVertexBuffer;
		L.OverrideVertexColors->InitFromSingleColor(FColor(128, 128, 128, 128), RD->LODResources[0].GetNumVertices());
		BeginInitResource(L.OverrideVertexColors);
		C->MarkRenderStateDirty();
	}
}

void USSXTrackEffects::ClearSplinePaths()
{
	for (TPair<int32, FSplinePathDraw>& P : SplinePathDraws)
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& C : P.Value.Comps)
		{
			if (C.IsValid()) { C->DestroyComponent(); }
		}
		if (P.Value.Line.IsValid()) { P.Value.Line->DestroyComponent(); }
	}
	SplinePathDraws.Reset();
	SplinePathInstances.Reset();
}

void USSXTrackEffects::BuildSplinePathDraw(const FEmitter& E, FSplinePathDraw& Out)
{
	UWorld* W = GetWorld();
	if (!W || !Data.InstMatrix.IsValidIndex(E.Inst))
	{
		return;
	}
	if (!bActorsBuilt)
	{
		BuildActorMap();
	}
	if (!SplinePathActor)
	{
		FActorSpawnParameters P;
		P.ObjectFlags |= RF_Transient;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SplinePathActor = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
		if (!SplinePathActor)
		{
			return;
		}
	}
	const FMatrix InstInv = GameMatrixToUnreal(Data.InstMatrix[E.Inst]).Inverse();
	TArray<UStaticMeshComponent*> Sources;
	if (const TArray<TWeakObjectPtr<AActor>>* Actors = ActorsByInstance.Find(E.Inst))
	{
		for (const TWeakObjectPtr<AActor>& A : *Actors)
		{
			if (!A.IsValid()) { continue; }
			TArray<UStaticMeshComponent*> Cs;
			A->GetComponents<UStaticMeshComponent>(Cs);
			for (UStaticMeshComponent* C : Cs)
			{
				if (C && C->GetStaticMesh()) { Sources.Add(C); }
			}
		}
	}
	for (UStaticMeshComponent* Src : Sources)
	{
		Out.ObjectLocal.Add(Src->GetComponentTransform().ToMatrixWithScale() * InstInv);
	}
	if (E.Path.Config.bDrawSpline)
	{
		// 0x251748 / 0x251A98
		TArray<FVector3f> Pts;
		const TConstArrayView<FSplinePathSegment> Segs = World.GetPathSegments();
		for (int32 K = E.Path.FirstSegment, N = E.Path.SegmentCount; N > 0 && Segs.IsValidIndex(K); --N, K = Segs[K].Next)
		{
			FVector3f P[USSXSplineLineComponent::PointsPerStrip];
			SplinePath::SegmentLinePoints(Segs[K], P);
			for (const FVector3f& Q : P) { Pts.Add(FVector3f(Q.X, -Q.Y, Q.Z)); }
		}
		USSXSplineLineComponent* L = NewObject<USSXSplineLineComponent>(SplinePathActor);
		L->SetStrips(MoveTemp(Pts), SplinePath::LineColour(E.Path.Config));
		L->RegisterComponent();
		Out.Line = L;
	}
	for (int32 Copy = 0; Copy < E.Path.Points.Num(); ++Copy)
	{
		for (UStaticMeshComponent* Src : Sources)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(SplinePathActor);
			C->SetMobility(EComponentMobility::Movable);
			C->SetStaticMesh(Src->GetStaticMesh());
			for (int32 M = 0; M < Src->GetNumMaterials(); ++M)
			{
				C->SetMaterial(M, Src->GetMaterial(M));
			}
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(Src->CastShadow);
			C->SetVisibility(false);
			C->RegisterComponent();
			OverrideVertexColour128(C);
			Out.Comps.Add(C);
		}
	}
}

void USSXTrackEffects::SyncSplinePaths()
{
	UWorld* W = GetWorld();
	if (!W || (!SplinePathDraws.Num() && !World.NumEmitters()))
	{
		return;
	}
	TSet<int32> Live;
	// 0x1C72E8
	float Far = 1.0e9f;
	static TWeakObjectPtr<UMaterialParameterCollection> MpcRef;
	if (!MpcRef.IsValid())
	{
		MpcRef = LoadObject<UMaterialParameterCollection>(nullptr, USSXWorldPresentation::CollectionPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	}
	if (const UMaterialParameterCollection* Mpc = MpcRef.Get())
	{
		if (const UMaterialParameterCollectionInstance* Inst = W->GetParameterCollectionInstance(Mpc))
		{
			Inst->GetScalarParameterValue(TEXT("DrawDistance"), Far);
		}
	}
	for (const FEmitter& E : World.GetEmitters())
	{
		if (E.Kind != 1 || !E.Path.bValid)
		{
			continue;
		}
		Live.Add(E.Serial);
		if (!SplinePathInstances.Contains(E.Inst))
		{
			SplinePathInstances.Add(E.Inst);
			DirtyDraw.Add(E.Inst);
		}
		FSplinePathDraw* D = SplinePathDraws.Find(E.Serial);
		if (!D)
		{
			D = &SplinePathDraws.Add(E.Serial);
			BuildSplinePathDraw(E, *D);
		}
		const int32 NObj = D->ObjectLocal.Num();
		if (E.Path.bPlaced)
		{
			for (int32 Copy = 0; Copy < E.Path.Points.Num(); ++Copy)
			{
				const FMatrix PathM = GameMatrixToUnreal(FMatrix(SplinePath::PointMatrix(E.Path.Points[Copy])));
				for (int32 K = 0; K < NObj; ++K)
				{
					const int32 Idx = Copy * NObj + K;
					UStaticMeshComponent* C = D->Comps.IsValidIndex(Idx) ? D->Comps[Idx].Get() : nullptr;
					if (!C) { continue; }
					C->SetWorldTransform(FTransform(D->ObjectLocal[K] * PathM));
					C->SetVisibility(true);
				}
			}
		}
		if (USSXSplineLineComponent* L = D->Line.Get())
		{
			L->SetFarDistance(Far);
		}
	}
	for (auto It = SplinePathDraws.CreateIterator(); It; ++It)
	{
		if (Live.Contains(It.Key())) { continue; }
		for (const TWeakObjectPtr<UStaticMeshComponent>& C : It.Value().Comps)
		{
			if (C.IsValid()) { C->DestroyComponent(); }
		}
		if (It.Value().Line.IsValid()) { It.Value().Line->DestroyComponent(); }
		It.RemoveCurrent();
	}
}

TArray<USSXSplineLineComponent*> USSXTrackEffects::GetSplineLineComponents() const
{
	TArray<USSXSplineLineComponent*> Out;
	for (const TPair<int32, FSplinePathDraw>& P : SplinePathDraws)
	{
		if (USSXSplineLineComponent* L = P.Value.Line.Get()) { Out.Add(L); }
	}
	return Out;
}
