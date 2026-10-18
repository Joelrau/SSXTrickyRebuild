#include "Characters/SSXRiderGloss.h"

#include "Materials/MaterialInstance.h"
#include "Misc/Paths.h"
#include "UI/SSXFrontEndData.h"

namespace SSXRiderGloss
{
	namespace
	{
		const TCHAR* const Stems[NumSpecmaps] = { TEXT("gari"), TEXT("snow"), TEXT("elys"), TEXT("mesa"), TEXT("merq"), TEXT("aloh"),
			TEXT("pipe"), TEXT("untr"), TEXT("toky"), TEXT("tric"), TEXT("alas"), TEXT("fend") };

		FString SpecmapDir()
		{
			return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("converted"), TEXT("characters"), TEXT("specmap"));
		}
	}

	const TCHAR* SpecmapStem(int32 Index)
	{
		return Index >= 0 && Index < NumSpecmaps ? Stems[Index] : TEXT("");
	}

	FString SpecmapName(int32 Index, bool bEnv)
	{
		return FString(SpecmapStem(Index)) + (bEnv ? TEXT("e") : TEXT("s"));
	}

	int32 SpecmapForGameTrack(int32 GameTrack, bool bFrontEnd)
	{
		if (bFrontEnd)
		{
			return FrontEndSpecmap;   // FUN_0025EDF0
		}
		if (GameTrack >= 0 && GameTrack <= 8)
		{
			return GameTrack;
		}
		return GameTrack == 10 ? 9 : GameTrack == 11 ? 10 : FrontEndSpecmap;
	}

	int32 SpecmapForTrackId(int32 TrackId)
	{
		return TrackId >= 0 && TrackId < NumSpecmaps ? TrackId : INDEX_NONE;
	}

	FString SpecmapPath(int32 Index, bool bEnv)
	{
		return FPaths::Combine(SpecmapDir(), SpecmapName(Index, bEnv) + TEXT(".png"));
	}

	FString FallbackPath(bool bEnv)
	{
		return FPaths::Combine(SpecmapDir(), bEnv ? TEXT("envr.png") : TEXT("spec.png"));
	}

	int32 GlossOfMpfMaterial(float Float0, bool bSlot3, bool bSlot4)
	{
		// FUN_00205670
		const float F = bSlot3 ? Float0 : (bSlot4 ? -Float0 : 0.0f);
		return int32(F * 127.0f);
	}

	int32 GlossFromName(const FString& Name)
	{
		const int32 At = Name.Find(TEXT("_env_"), ESearchCase::CaseSensitive);
		if (At == INDEX_NONE || At + 6 >= Name.Len())
		{
			return 0;
		}
		const TCHAR Sign = Name[At + 5];
		if (Sign != TEXT('s') && Sign != TEXT('e'))
		{
			return 0;
		}
		int32 I = At + 6, Value = 0, Digits = 0;
		while (I < Name.Len() && FChar::IsDigit(Name[I]) && Digits < 3)
		{
			Value = Value * 10 + (Name[I] - TEXT('0'));
			++I;
			++Digits;
		}
		if (Digits == 0 || I >= Name.Len() || Name[I] != TEXT('_'))
		{
			return 0;
		}
		return Sign == TEXT('s') ? Value : -Value;
	}

	int32 GlossOfMaterial(const UMaterialInterface* M)
	{
		for (int32 Depth = 0; M && Depth < 8; ++Depth)
		{
			if (const int32 G = GlossFromName(M->GetName()))
			{
				return G;
			}
			const UMaterialInstance* I = Cast<UMaterialInstance>(M);
			M = I ? I->Parent.Get() : nullptr;
		}
		return 0;
	}

	FString MaterialKey(const FString& Slot, int32 Gloss)
	{
		const int32 F = Fix(Gloss);
		return F == 0 ? FString::Printf(TEXT("env_%s"), *Slot)
			: FString::Printf(TEXT("env_%s%d_%s"), Gloss > 0 ? TEXT("s") : TEXT("e"), F, *Slot);
	}

	const TCHAR* UvHlsl()
	{
		return TEXT("return saturate(float2(N.x * R.x - N.y * U.x + N.z * F.x, -N.x * R.y + N.y * U.y - N.z * F.y));");
	}

	const TCHAR* ShadeHlsl()
	{
		return TEXT(
			"float3 t = (lerp(1.055 * pow(max(Tex, 0.0), 1.0 / 2.4) - 0.055, Tex * 12.92, step(Tex, 0.0031308)));\n"
			"float3 g = (lerp(1.055 * pow(max(Gloss, 0.0), 1.0 / 2.4) - 0.055, Gloss * 12.92, step(Gloss, 0.0031308)));\n"
			"float3 cb = saturate(t * V);\n"
			"float3 cg = saturate(g * V);\n"
			"return saturate(cb + cg * Fix);\n");
	}

	int32 GsPixel(int32 Tb, int32 Tg, int32 Cv, int32 FixValue)
	{
		const int32 Cb = FMath::Min(255, (Tb * Cv) >> 7);
		const int32 Cg = FMath::Min(255, (Tg * Cv) >> 7);
		return FMath::Min(255, Cb + ((Cg * FixValue) >> 7));
	}

	double ShadePixel(double T, double G, double V, double FixF)
	{
		const double Cb = FMath::Clamp(T * V, 0.0, 1.0);
		const double Cg = FMath::Clamp(G * V, 0.0, 1.0);
		return FMath::Clamp(Cb + Cg * FixF, 0.0, 1.0);
	}

	FVector2D Uv(const FVector& N, const FVector& R, const FVector& U, const FVector& F)
	{
		const FVector2D St(N.X * R.X - N.Y * U.X + N.Z * F.X, -N.X * R.Y + N.Y * U.Y - N.Z * F.Y);
		return FVector2D(FMath::Clamp(St.X, 0.0, 1.0), FMath::Clamp(St.Y, 0.0, 1.0));
	}
}
