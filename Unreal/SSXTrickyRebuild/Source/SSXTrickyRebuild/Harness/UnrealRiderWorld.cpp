#include "Harness/UnrealRiderWorld.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Sim/SimCoords.h"

FUnrealRiderWorld::FUnrealRiderWorld(const IRiderWorld* InInner, UWorld* InWorld, AActor* InIgnore)
	: Inner(InInner), World(InWorld), Ignore(InIgnore)
{
	check(Inner);
}

bool FUnrealRiderWorld::QueryWalls(const FRiderSphereSet& Shape, bool bFine, const FVector* FilterDir, FRiderWorldHit& Out) const
{
	++TotalWallQueries;
	LastWallContacts = 0;
	UWorld* W = World.Get();
	if (!W)
	{
		return Inner->QueryWalls(Shape, bFine, FilterDir, Out);
	}
	const double TRef = bFine ? 2.0 * Shape.BoundRadius : -1.0;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SSXRiderWalls), /*bTraceComplex=*/true);
	if (AActor* Ign = Ignore.Get())
	{
		Params.AddIgnoredActor(Ign);
	}
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);

	bool bHave = false;
	double BestScore = TNumericLimits<double>::Max();
	for (int32 I = 0; I < FMath::Min(Shape.Count, FRiderSphereSet::MaxSpheres); ++I)
	{
		if (Shape.Mask != -1 && !(Shape.Mask & (1 << I)))
		{
			continue;
		}
		const double R = Shape.Radii[I];
		if (R <= 0.0)
		{
			continue;
		}
		const FVector CentreUE = SSXSim::GameToUnreal(Shape.Centres[I]);
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(float(R));
		TArray<FOverlapResult> Overlaps;
		if (!W->OverlapMultiByObjectType(Overlaps, CentreUE, FQuat::Identity, Objects, Sphere, Params))
		{
			continue;
		}
		for (const FOverlapResult& O : Overlaps)
		{
			UPrimitiveComponent* Comp = O.GetComponent();
			if (!Comp)
			{
				continue;
			}
			FMTDResult Mtd;
			if (!Comp->ComputePenetration(Mtd, Sphere, CentreUE, FQuat::Identity) || Mtd.Distance <= 0.f)
			{
				continue;
			}
			++LastWallContacts;
			const FVector NormalUE = FVector(Mtd.Direction).GetSafeNormal();
			const FVector Normal = SSXSim::UnrealToGame(NormalUE);
			if (FilterDir && FVector::DotProduct(Normal, *FilterDir) > FilterDot)
			{
				continue;
			}
			FRiderWorldHit Hit;
			Hit.T = Mtd.Distance;
			Hit.Normal = Normal;
			Hit.Point = SSXSim::UnrealToGame(CentreUE - NormalUE * (R - Mtd.Distance));
			Hit.Kind = 1;
			Hit.Surface = -1;
			const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(Comp);
			if (Smc && Smc->GetStaticMesh() && Smc->GetStaticMesh()->GetName().Contains(TEXT("terrain")))
			{
				Hit.Kind = 0;
				FRiderWorldHit PatchHit;
				if (Inner->LineQuery(Hit.Point + 50.0 * Normal, Hit.Point - 50.0 * Normal, ERiderLineQuery::Fine, 0.5, PatchHit))
				{
					Hit.Surface = PatchHit.Surface;
					Hit.PatchIndex = PatchHit.PatchIndex;
				}
			}
			if (TRef < 0.0)
			{
				Out = Hit;
				return true;
			}
			const double Score = FMath::Abs(Hit.T - TRef);
			if (Score < BestScore)
			{
				BestScore = Score;
				Out = Hit;
				bHave = true;
			}
		}
	}
	return bHave;
}
