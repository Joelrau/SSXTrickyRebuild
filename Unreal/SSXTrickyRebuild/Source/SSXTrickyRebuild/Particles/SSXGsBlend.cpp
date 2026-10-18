#include "Particles/SSXGsBlend.h"

namespace SSXGs
{
	const TCHAR* SpriteHlsl()
	{
		return TEXT(
			"float3 ct = floor(Tex.rgb * 255.0 + 0.001);\n"
			"float at = min(floor((floor(Tex.a * 255.0 + 0.001) + 1.0) * 0.5), 128.0);\n"
			"float3 cv = floor(VC.rgb * 255.0 + 0.5);\n"
			"float av = floor(VC.a * 255.0 + 0.5);\n"
			"float3 cs = min(floor(ct * cv / 128.0), 255.0);\n"
			"float sa = min(floor(at * av / 128.0), 255.0);\n"
			"return float4(cs / 255.0, sa / 128.0);\n");
	}

	FGsSource GsModulate(const FIntVector& Ct, int32 At, const FIntVector& Cv, int32 Av)
	{
		FGsSource S;
		S.R = FMath::Min((Ct.X * Cv.X) >> 7, 255);
		S.G = FMath::Min((Ct.Y * Cv.Y) >> 7, 255);
		S.B = FMath::Min((Ct.Z * Cv.Z) >> 7, 255);
		S.A = FMath::Min((At * Av) >> 7, 255);
		return S;
	}

	int32 GsBlend(int32 Mode, int32 Cs, int32 As, int32 Cd)
	{
		int32 V = Cd;
		switch (Mode)
		{
		case 3: V = (((Cs - Cd) * As) >> 7) + Cd; break;
		case 5: V = ((Cs * As) >> 7) + Cd; break;
		case 4: V = (((0 - Cd) * As) >> 7) + Cd; break;
		case 8: V = ((Cd * As) >> 7) + Cd; break;
		default: break;
		}
		return FMath::Clamp(V, 0, 255);
	}

	FSpriteNode SpriteNode(const FLinearColor& Tex, const FColor& Vc)
	{
		auto Floor = [](float X) { return FMath::FloorToFloat(X); };
		const FVector3f Ct(Floor(Tex.R * 255.f + 0.001f), Floor(Tex.G * 255.f + 0.001f), Floor(Tex.B * 255.f + 0.001f));
		const float At = FMath::Min(Floor((Floor(Tex.A * 255.f + 0.001f) + 1.f) * 0.5f), 128.f);
		const FVector3f Cv(Floor(Vc.R / 255.f * 255.f + 0.5f), Floor(Vc.G / 255.f * 255.f + 0.5f), Floor(Vc.B / 255.f * 255.f + 0.5f));
		const float Av = Floor(Vc.A / 255.f * 255.f + 0.5f);
		FSpriteNode N;
		N.Cs = FVector3f(FMath::Min(Floor(Ct.X * Cv.X / 128.f), 255.f), FMath::Min(Floor(Ct.Y * Cv.Y / 128.f), 255.f),
			FMath::Min(Floor(Ct.Z * Cv.Z / 128.f), 255.f)) / 255.f;
		N.A = FMath::Min(Floor(At * Av / 128.f), 255.f) / 128.f;
		return N;
	}

	const TCHAR* PropAlphaHlsl()
	{
		// At from the PNG byte (min(2 At, 255)), Av the vertex byte (the baked block alpha, 128), MODULATE >> 7
		return TEXT(
			"float at = min(floor((floor(A * 255.0 + 0.001) + 1.0) * 0.5), 128.0);\n"
			"float av = floor(VA * 255.0 + 0.5);\n"
			"return min(floor(at * av / 128.0), 255.0);\n");
	}

	const TCHAR* SoftDepthHlsl()
	{
		return TEXT("return (PD <= CD.r * 1.0005 + 0.5) ? 1.0 : 0.0;");
	}

	const TCHAR* SoftDepthSpriteHlsl()
	{
		return TEXT("return float4(S.rgb, (PD <= CD.r * 1.0005 + 0.5) ? S.a : 0.0);");
	}

	bool SoftDepthFront(float PixelDepth, float CustomDepth)
	{
		return PixelDepth <= CustomDepth * 1.0005f + 0.5f;
	}

	float PropAlphaNode(float TexA, float VertexA)
	{
		const float At = FMath::Min(FMath::FloorToFloat((FMath::FloorToFloat(TexA * 255.f + 0.001f) + 1.f) * 0.5f), 128.f);
		const float Av = FMath::FloorToFloat(VertexA * 255.f + 0.5f);
		return FMath::Min(FMath::FloorToFloat(At * Av / 128.f), 255.f);
	}

	float PropMaterialPixel(float Cs, float As, float Cd)
	{
		if (As >= 128.f) { return Cs; }
		const float Op = (As > float(PropAlphaRef) && As < 128.f) ? As / 128.f : 0.f;
		return MaterialBlend(3, Cs, Op, Cd);
	}

	int32 GsPropPixel(int32 Cs, int32 As, int32 Cd)
	{
		return As > PropAlphaRef ? GsBlend(3, Cs, As, Cd) : Cd;
	}

	float MaterialBlend(int32 Mode, float Cs, float A, float Cd)
	{
		switch (Mode)
		{
		case 3: { const float Op = FMath::Clamp(A, 0.f, 1.f); return Cs * Op + Cd * (1.f - Op); }
		case 5: return Cd + Cs * A;
		case 4: return Cd * FMath::Max(1.f - A, 0.f);
		case 8: return Cd * (1.f + A);
		default: return Cd;
		}
	}
}
