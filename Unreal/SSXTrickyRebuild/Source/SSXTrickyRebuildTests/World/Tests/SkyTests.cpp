// 0x3880C0 / FUN_00185B18 / 0x337CB8 / 0x1C7900
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR   // material expressions are editor-only data

namespace
{
	void TestCollectionParameters(FAutomationTestBase& Test, const UMaterial& M)
	{
		for (const TObjectPtr<UMaterialExpression>& E : M.GetExpressions())
		{
			if (const UMaterialExpressionCollectionParameter* C = Cast<UMaterialExpressionCollectionParameter>(E.Get()))
			{
				const FString Coll = C->Collection ? C->Collection->GetName() : FString(TEXT("(none)"));
				Test.TestTrue(FString::Printf(TEXT("%s: collection parameter node bound to a parameter of %s"), *M.GetName(), *Coll),
					C->Collection && !C->ParameterName.IsNone());
			}
		}
	}

	constexpr EAutomationTestFlags SkyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldSkyTest, "SSX.World.Sky", SkyTestFlags)
bool FSSXWorldSkyTest::RunTest(const FString&)
{
	const UMaterial* Sky = LoadObject<UMaterial>(nullptr, TEXT("/Game/World/M_SSXSky.M_SSXSky"));
	if (!Sky)
	{
		AddError(TEXT("/Game/World/M_SSXSky missing (Scripts/make_world_materials.py)"));
		return false;
	}
	int32 Samplers = 0;
	for (const TObjectPtr<UMaterialExpression>& E : Sky->GetExpressions())
	{
		if (const UMaterialExpressionTextureSampleParameter2D* T = Cast<UMaterialExpressionTextureSampleParameter2D>(E.Get()))
		{
			++Samplers;
			TestEqual(FString::Printf(TEXT("M_SSXSky %s: CLAMP sampler (slot 63 = 1)"), *T->ParameterName.ToString()),
				int32(T->SamplerSource), int32(SSM_Clamp_WorldGroupSettings));
			TestEqual(FString::Printf(TEXT("M_SSXSky %s: explicit mip level"), *T->ParameterName.ToString()),
				int32(T->MipValueMode), int32(TMVM_MipLevel));
		}
	}
	TestEqual(TEXT("M_SSXSky texture samplers"), Samplers, 1);
	TestCollectionParameters(*this, *Sky);

	// 0x3880C0
	IAssetRegistry* AR = IAssetRegistry::Get();
	if (!AR)
	{
		AddError(TEXT("no asset registry"));
		return false;
	}
	AR->SearchAllAssets(true);
	const TPair<const TCHAR*, const TCHAR*> Tracks[] = { { TEXT("Gari"), TEXT("gari") }, { TEXT("Snow"), TEXT("snow") },
		{ TEXT("Elysium"), TEXT("elysium") }, { TEXT("Mesa"), TEXT("mesa") }, { TEXT("Merquer"), TEXT("merquer") },
		{ TEXT("Aloha"), TEXT("aloha") }, { TEXT("Pipe"), TEXT("pipe") }, { TEXT("Untrack"), TEXT("untrack") },
		{ TEXT("Megaple"), TEXT("megaple") }, { TEXT("Trick"), TEXT("trick") }, { TEXT("Alaska"), TEXT("alaska") } };
	int32 Found = 0;
	for (const TPair<const TCHAR*, const TCHAR*>& T : Tracks)
	{
		FARFilter Filter;
		Filter.PackagePaths = { FName(*FString::Printf(TEXT("/Game/Tracks/%s/Sky"), T.Key)) };
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		TArray<FAssetData> Meshes;
		AR->GetAssets(Filter, Meshes);
		if (Meshes.Num() == 0)
		{
			AddWarning(FString::Printf(TEXT("%s: no sky mesh (track map not built)"), T.Key));
			continue;
		}
		++Found;
		const FString Model = FString::Printf(TEXT("%s_sky_"), T.Value);
		for (const FAssetData& A : Meshes)
		{
			TestTrue(FString::Printf(TEXT("%s: sky mesh %s from model %ssky"), T.Key, *A.AssetName.ToString(), T.Value),
				A.AssetName.ToString().StartsWith(Model));
			const UStaticMesh* Mesh = Cast<UStaticMesh>(A.GetAsset());
			if (!Mesh)
			{
				continue;
			}
			const FStaticMeshRenderData* RD = Mesh->GetRenderData();
			if (RD && RD->LODResources.Num() > 0 && RD->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetTexCoordData())
			{
				const FStaticMeshLODResources& LOD = RD->LODResources[0];
				const FIndexArrayView Idx = LOD.IndexBuffer.GetArrayView();
				for (int32 Sec = 0; Sec < LOD.Sections.Num(); ++Sec)
				{
					const FStaticMeshSection& S = LOD.Sections[Sec];
					FVector2f Lo(1e9f), Hi(-1e9f);
					for (uint32 I = 0; I < S.NumTriangles * 3; ++I)
					{
						const FVector2f UV = LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Idx[S.FirstIndex + I], 0);
						Lo = FVector2f::Min(Lo, UV);
						Hi = FVector2f::Max(Hi, UV);
					}
					constexpr float Eps = 1e-3f;
					TestTrue(FString::Printf(TEXT("%s: %s section %d UV (%.3f..%.3f, %.3f..%.3f) inside 0..1 (CLAMP sampler)"), T.Key,
						*A.AssetName.ToString(), Sec, Lo.X, Hi.X, Lo.Y, Hi.Y),
						S.NumTriangles > 0 && Lo.X >= -Eps && Lo.Y >= -Eps && Hi.X <= 1 + Eps && Hi.Y <= 1 + Eps);
				}
			}
			else
			{
				SSXTest::Skip(*this, FString::Printf(TEXT("%s: %s has no CPU vertex data, UV range not checked"), T.Key, *A.AssetName.ToString()));
			}
			for (const FStaticMaterial& S : Mesh->GetStaticMaterials())
			{
				UMaterialInterface* M = S.MaterialInterface;
				TestTrue(FString::Printf(TEXT("%s: %s slot %s parent M_SSXSky"), T.Key, *A.AssetName.ToString(), *S.MaterialSlotName.ToString()),
					M && M->GetMaterial() == Sky);
			}
		}
	}
	TestTrue(TEXT("sky meshes found"), Found > 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWorldMaterialCollectionsTest, "SSX.World.MaterialCollections", SkyTestFlags)
bool FSSXWorldMaterialCollectionsTest::RunTest(const FString&)
{
	IAssetRegistry* AR = IAssetRegistry::Get();
	if (!AR)
	{
		AddError(TEXT("no asset registry"));
		return false;
	}
	AR->SearchAllAssets(true);
	FARFilter Filter;
	Filter.PackagePaths = { FName(TEXT("/Game/World")) };
	Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
	TArray<FAssetData> Materials;
	AR->GetAssets(Filter, Materials);
	TestTrue(TEXT("world materials found"), Materials.Num() > 0);
	for (const FAssetData& A : Materials)
	{
		if (const UMaterial* M = Cast<UMaterial>(A.GetAsset()))
		{
			TestCollectionParameters(*this, *M);
		}
	}
	return !HasAnyErrors();
}

#endif
