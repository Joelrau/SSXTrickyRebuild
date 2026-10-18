#include "Characters/SSXRiderChrome.h"

#include "Materials/MaterialInstance.h"
#include "Misc/Paths.h"
#include "UI/SSXFrontEndData.h"

namespace SSXRiderChrome
{
	bool IsEnvPassName(const FString& Name)
	{
		return Name.Contains(TEXT("_env_"), ESearchCase::CaseSensitive);
	}

	bool IsEnvPassMaterial(const UMaterialInterface* M)
	{
		for (int32 Depth = 0; M && Depth < 8; ++Depth)
		{
			if (IsEnvPassName(M->GetName()))
			{
				return true;
			}
			const UMaterialInstance* I = Cast<UMaterialInstance>(M);
			M = I ? I->Parent.Get() : nullptr;
		}
		return false;
	}

	FString EnvTexturePath()
	{
		return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("characters"), TEXT("envr.png"));
	}

	const TCHAR* EnvUvHlsl()
	{
		return TEXT("return float2(N.x * R.x - N.y * U.x + N.z * F.x, -N.x * R.y + N.y * U.y - N.z * F.y);");
	}

	const TCHAR* ShadeHlsl()
	{
		return TEXT(
			"float3 t = (lerp(1.055 * pow(max(Tex, 0.0), 1.0 / 2.4) - 0.055, Tex * 12.92, step(Tex, 0.0031308)));\n"
			"float3 e = (lerp(1.055 * pow(max(Env, 0.0), 1.0 / 2.4) - 0.055, Env * 12.92, step(Env, 0.0031308)));\n"
			"float3 cb = saturate(t * V);\n"
			"float3 ce = saturate(e * V);\n"
			"return saturate(cb + (ce - cb) * (33.0 / 128.0));\n");
	}

	FVector2D EnvUv(const FVector& N, const FVector& R, const FVector& U, const FVector& F)
	{
		return FVector2D(N.X * R.X - N.Y * U.X + N.Z * F.X, -N.X * R.Y + N.Y * U.Y - N.Z * F.Y);
	}

	FVector2D EnvStVu(const FMatrix& View, const FMatrix& BoneRows, const FVector& LocalN)
	{
		// FUN_001C5C88
		FVector T[3];
		for (int32 K = 0; K < 3; ++K)
		{
			T[K] = FVector(View.M[0][K], View.M[1][K], View.M[2][K]);
		}
		FVector E[3];
		for (int32 J = 0; J < 3; ++J)
		{
			E[J] = T[0] * BoneRows.M[J][0] + T[1] * BoneRows.M[J][1] + T[2] * BoneRows.M[J][2];
		}
		const FVector St = E[0] * LocalN.X + E[1] * LocalN.Y + E[2] * LocalN.Z;
		return FVector2D(St.X, St.Y);
	}

	int32 GsPixel(int32 Tb, int32 Te, int32 Cv)
	{
		const int32 Cb = FMath::Min(255, (Tb * Cv) >> 7);
		const int32 Ce = FMath::Min(255, (Te * Cv) >> 7);
		const int32 Out = Cb + FMath::FloorToInt((Ce - Cb) * EnvFix / 128.0);
		return FMath::Clamp(Out, 0, 255);
	}

	double ShadePixel(double T, double E, double V)
	{
		const double Cb = FMath::Clamp(T * V, 0.0, 1.0);
		const double Ce = FMath::Clamp(E * V, 0.0, 1.0);
		return FMath::Clamp(Cb + (Ce - Cb) * (33.0 / 128.0), 0.0, 1.0);
	}
}
