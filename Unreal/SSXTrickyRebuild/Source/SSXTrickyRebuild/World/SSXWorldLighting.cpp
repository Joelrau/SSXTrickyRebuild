#include "World/SSXWorldLighting.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/RaceTrackData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

int32 FSSXTrackPresentation::IdForStem(const FString& Stem)
{
	static const TCHAR* Stems[] = { TEXT("gari"), TEXT("snow"), TEXT("elysium"), TEXT("mesa"), TEXT("merquer"), TEXT("aloha"),
		TEXT("pipe"), TEXT("untrack"), TEXT("megaple"), TEXT("trick"), TEXT("alaska"), TEXT("ssxfe") };
	const FString S = Stem.ToLower();
	if (S == TEXT("frontend"))
	{
		return 11;
	}
	for (int32 I = 0; I < UE_ARRAY_COUNT(Stems); ++I)
	{
		if (S == Stems[I])
		{
			return I;
		}
	}
	return -1;
}

int32 FSSXTrackPresentation::RowForStem(const FString& Stem)
{
	const int32 Id = IdForStem(Stem);
	return Id == 11 ? Id : FRaceTrackData::GameTrackOf(Id);
}

FString FSSXTrackPresentation::DefaultTablePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("world_track_table.json")));
}

bool FSSXTrackPresentation::LoadTable(const FString& Path, TArray<FSSXTrackPresentation>& Out, FString* OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s (run tools/export_world_lighting.py)"), *Path); }
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> Records;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Records))
	{
		if (OutError) { *OutError = TEXT("bad JSON"); }
		return false;
	}
	auto Vec = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		FVector V = FVector::ZeroVector;
		if (O->TryGetArrayField(Key, A) && A->Num() >= 3)
		{
			V = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		}
		return V;
	};
	Out.Reset();
	for (const TSharedPtr<FJsonValue>& R : Records)
	{
		const TSharedPtr<FJsonObject> O = R->AsObject();
		if (!O) { continue; }
		FSSXTrackPresentation T;
		T.Id = O->GetIntegerField(TEXT("id"));
		T.Name = O->GetStringField(TEXT("track"));
		T.DrawDist1P = O->GetNumberField(TEXT("draw_dist_1p"));
		T.DrawDist2P = O->GetNumberField(TEXT("draw_dist_2p"));
		T.bMipLodOn = O->GetIntegerField(TEXT("fog_on")) != 0;
		T.MipDist1P = O->GetNumberField(TEXT("fog_dist_1p"));
		T.MipShift1P = O->GetIntegerField(TEXT("fog_shift_1p"));
		T.MipDist2P = O->GetNumberField(TEXT("fog_dist_2p"));
		T.MipShift2P = O->GetIntegerField(TEXT("fog_shift_2p"));
		T.SnowChance = O->GetNumberField(TEXT("snow_chance"));
		T.RiderAmbBlend = O->GetNumberField(TEXT("rider_amb_blend"));
		T.RiderAmbTarget = Vec(O, TEXT("rider_amb_target"));
		T.RiderLightScale = O->GetNumberField(TEXT("rider_light_scale"));
		const FVector C = Vec(O, TEXT("colour_rgb"));
		T.ClearRGB = FIntVector(FMath::RoundToInt(C.X), FMath::RoundToInt(C.Y), FMath::RoundToInt(C.Z));
		auto Rgb = [](const FVector& V) { return FIntVector(FMath::RoundToInt(V.X), FMath::RoundToInt(V.Y), FMath::RoundToInt(V.Z)); };
		auto Num = [&O](const TCHAR* Key) { double V = 0.0; O->TryGetNumberField(Key, V); return V; };
		T.bFlareOn = Num(TEXT("flare_on")) != 0.0;
		T.GlareRGB = Rgb(Vec(O, TEXT("flare_rgb1")));
		T.GlareAlpha = Num(TEXT("flare_k_a"));
		T.SunRadius = Num(TEXT("flare_size"));
		T.SunRGB = Rgb(Vec(O, TEXT("flare_rgb2")));
		T.SunAlpha = Num(TEXT("flare_k_b"));
		T.SunAzimuthDeg = Num(TEXT("sun_azimuth_deg"));
		T.SunElevationDeg = Num(TEXT("sun_elevation_deg"));
		T.SunDistance = Num(TEXT("sun_distance"));
		T.HaloRingBase = Num(TEXT("f34"));
		T.HaloStreakBase = Num(TEXT("f38"));
		T.HaloAlpha = Vec(O, TEXT("flare_k_c"));
		T.HaloRange = Num(TEXT("f5c"));
		Out.Add(MoveTemp(T));
	}
	Out.Sort([](const FSSXTrackPresentation& A, const FSSXTrackPresentation& B) { return A.Id < B.Id; });
	return Out.Num() > 0;
}

FColor FSSXTrackPresentation::ClearColour() const
{
	// 0x2F79A8
	auto RoundTrip = [](int32 B) { const float F = float(B) / 255.0f; return uint8(FMath::Clamp(FMath::RoundToInt(F * 255.0f), 0, 255)); };
	return FColor(RoundTrip(ClearRGB.X), RoundTrip(ClearRGB.Y), RoundTrip(ClearRGB.Z), 0x80);
}

FIntVector FSSXTrackPresentation::MipLodParams1P() const
{
	// FUN_001C0DA8
	if (!bMipLodOn || MipDist1P <= 0.0)
	{
		return FIntVector(1, 0, 0);
	}
	const double K = -((FMath::Loge(MipDist1P) / 0.6931472) * double(1 << MipShift1P)) * 16.0;
	return FIntVector(0, MipShift1P, int32(K));
}

int32 FSSXLightSlots::Insert(const FVector& InColour, const FVector& InDirection)
{
	const double F = FMath::Abs(InColour.X + InColour.Y + InColour.Z);
	if (!(Score[0] < F))
	{
		return -1;
	}
	int32 At = 0;
	while (At + 1 < FSSXRiderLight::Slots && Score[At + 1] < F)
	{
		Score[At] = Score[At + 1];
		Colour[At] = Colour[At + 1];
		Direction[At] = Direction[At + 1];
		++At;
	}
	Score[At] = F;
	Colour[At] = InColour;
	Direction[At] = InDirection;
	return At;
}

FString FSSXLightWorld::DefaultPath(const FString& Stem)
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Stem.ToLower() + TEXT(".lights.bin")));
}

bool FSSXLightWorld::Load(const FString& Path, FString* OutError)
{
	bLoaded = false;
	TArray<uint8> B;
	if (!FFileHelper::LoadFileToArray(B, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s (run tools/export_world_lighting.py)"), *Path); }
		return false;
	}
	int64 Pos = 0;
	bool bOk = true;
	auto Need = [&](int64 N) { if (Pos + N > B.Num()) { bOk = false; } return bOk; };
	auto U32 = [&]() -> uint32 { if (!Need(4)) return 0; uint32 V; FMemory::Memcpy(&V, B.GetData() + Pos, 4); Pos += 4; return V; };
	auto I32 = [&]() -> int32 { return static_cast<int32>(U32()); };
	auto F32 = [&]() -> double { const uint32 U = U32(); float F; FMemory::Memcpy(&F, &U, 4); return F; };
	auto RecF = [&](int64 Base, int32 Off) { float F; FMemory::Memcpy(&F, B.GetData() + Base + Off, 4); return static_cast<double>(F); };
	auto RecV = [&](int64 Base, int32 Off) { return FVector(RecF(Base, Off), RecF(Base, Off + 4), RecF(Base, Off + 8)); };
	auto RecI = [&](int64 Base, int32 Off) { int32 V; FMemory::Memcpy(&V, B.GetData() + Base + Off, 4); return V; };

	if (!Need(4) || FMemory::Memcmp(B.GetData(), "SSXL", 4) != 0)
	{
		if (OutError) { *OutError = TEXT("not an SSXL file"); }
		return false;
	}
	Pos = 4;
	const uint32 Version = U32();
	const uint32 Count = U32();
	if ((Version != 1 && Version != 2) || !Need(int64(Count) * 92))
	{
		if (OutError) { *OutError = TEXT("bad header"); }
		return false;
	}
	Lights.Reset(Count);
	for (uint32 K = 0; K < Count; ++K)
	{
		const int64 R = Pos + int64(K) * 92;
		FSSXLight L;
		L.Type = RecI(R, 0x00);
		L.Flags = static_cast<uint32>(RecI(R, 0x04));
		L.Intensity = RecF(R, 0x08);
		L.Colour = RecV(R, 0x10);
		L.Direction = RecV(R, 0x1C);
		L.Position = RecV(R, 0x28);
		L.CosCone = RecF(R, 0x4C);
		L.Falloff = RecI(R, 0x50);
		L.Exponent = RecF(R, 0x54);
		Lights.Add(L);
	}
	Pos += int64(Count) * 92;
	MinX = F32(); MinY = F32(); Cell = F32(); Sub = F32();
	NX = I32(); NY = I32();
	if (!bOk || NX < 0 || NY < 0 || NX * NY > 1000000)
	{
		if (OutError) { *OutError = TEXT("bad grid header"); }
		return false;
	}
	CellBase.Init(-1, NX * NY);
	SubLists.Reset();
	HaloSequence.Reset();
	HaloSubLists.Reset();
	for (int32 C = 0; C < NX * NY && bOk; ++C)
	{
		if (!Need(1)) { break; }
		const bool bPresent = B[Pos++] != 0;
		if (!bPresent) { continue; }
		CellBase[C] = SubLists.Num();
		for (int32 S = 0; S < SubSide * SubSide; ++S)
		{
			const uint32 N = U32();
			TArray<int32>& List = SubLists.AddDefaulted_GetRef();
			if (!Need(int64(N) * 4)) { break; }
			List.Reserve(N);
			for (uint32 I = 0; I < N; ++I)
			{
				List.Add(I32());
			}
		}
		for (int32 S = 0; S < SubSide * SubSide; ++S)
		{
			TArray<int32>& Halo = HaloSubLists.AddDefaulted_GetRef();
			if (Version < 2 || !bOk) { continue; }
			const uint32 N = U32();
			if (!Need(int64(N) * 4)) { continue; }
			for (uint32 I = 0; I < N; ++I)
			{
				const int32 Li = I32();
				if (Lights.IsValidIndex(Li)) { HaloSequence.Add(Li); Halo.Add(Li); }
			}
		}
	}
	if (!bOk)
	{
		if (OutError) { *OutError = TEXT("truncated grid"); }
		return false;
	}
	bLoaded = true;
	return true;
}

const TArray<int32>& FSSXLightWorld::SubCellLights(const FVector& P) const
{
	if (NX <= 0 || NY <= 0 || Cell <= 0.0 || Sub <= 0.0)
	{
		return Empty;
	}
	const int32 IX = FMath::FloorToInt((P.X - MinX) / Cell);
	const int32 IY = FMath::FloorToInt((P.Y - MinY) / Cell);
	if (IX < 0 || IX >= NX || IY < 0 || IY >= NY)
	{
		return Empty;
	}
	const int32 Base = CellBase[IY * NX + IX];
	if (Base < 0)
	{
		return Empty;
	}
	const int32 SX = FMath::Clamp(FMath::FloorToInt((P.X - MinX - IX * Cell) / Sub), 0, SubSide - 1);
	const int32 SY = FMath::Clamp(FMath::FloorToInt((P.Y - MinY - IY * Cell) / Sub), 0, SubSide - 1);
	return SubLists[Base + SY * SubSide + SX];
}

double FSSXLightWorld::FalloffFactor(int32 Kind, double D)
{
	if (Kind == 0 || D < FalloffKnee) { return 1.0; }
	switch (Kind)
	{
	case 1: return 100.0 / D;
	case 2: return 10000.0 / (D * D);
	case 3: return 1e6 / (D * D * D);
	default: return 1.0;
	}
}

bool FSSXLightWorld::Attenuation(const FSSXLight& L, const FVector& P, double& OutAtten, FVector& OutToLight) const
{
	const FVector V = L.Position - P;
	const double D = V.Size();
	OutToLight = D > 0.0 ? V / D : FVector(0, 0, 1);
	OutAtten = FalloffFactor(L.Falloff, D);
	if (L.Type == FSSXLight::Spot)
	{
		const double C = -FVector::DotProduct(OutToLight, L.Direction);
		if (C <= L.CosCone)
		{
			return false;
		}
		const int32 N = static_cast<int32>(L.Exponent);
		double Pw = 1.0;
		for (int32 I = 0; I < FMath::Abs(N); ++I) { Pw *= C; }
		OutAtten *= N < 0 ? (Pw != 0.0 ? 1.0 / Pw : 0.0) : Pw;
	}
	return true;
}

FSSXRiderLight FSSXLightWorld::Probe(const FVector& P, const FSSXRiderLightAdjust& Adjust, const FVector& Flash) const
{
	return ProbeList(SubCellLights(P), P, Adjust, Flash);
}

FSSXRiderLight FSSXLightWorld::ProbeList(const TArray<int32>& Indices, const FVector& P, const FSSXRiderLightAdjust& Adjust,
	const FVector& Flash) const
{
	FVector M(1.0);
	for (const int32 I : Indices)
	{
		if (!Lights.IsValidIndex(I)) { continue; }
		const FSSXLight& L = Lights[I];
		if (!(L.Flags & FSSXLight::FlagModulator) || (L.Type != FSSXLight::Spot && L.Type != FSSXLight::Point)) { continue; }
		double A; FVector ToL;
		if (Attenuation(L, P, A, ToL))
		{
			M += L.Colour * A;
		}
	}
	M = FVector(FMath::Clamp(M.X, 0.0, 1.0), FMath::Clamp(M.Y, 0.0, 1.0), FMath::Clamp(M.Z, 0.0, 1.0));

	FSSXLightSlots Slots;
	FVector Amb = FVector::ZeroVector;
	for (const int32 I : Indices)
	{
		if (!Lights.IsValidIndex(I)) { continue; }
		const FSSXLight& L = Lights[I];
		if (L.Flags & FSSXLight::FlagModulator) { continue; }
		const FVector Mod = (L.Flags & FSSXLight::FlagNotModulated) ? FVector(1.0) : M;
		switch (L.Type)
		{
		case FSSXLight::Ambient:
			Amb += L.Colour * Mod;
			break;
		case FSSXLight::Directional:
			Slots.Insert(L.Colour * Mod, -L.Direction);
			break;
		case FSSXLight::Spot:
		case FSSXLight::Point:
		{
			double A; FVector ToL;
			if (Attenuation(L, P, A, ToL))
			{
				Slots.Insert(L.Colour * A * Mod, ToL);
			}
			break;
		}
		default:
			break;
		}
	}

	// 0x135EB8
	FSSXRiderLight Out;
	Out.Modulator = M;
	Out.Ambient = (Amb + (Adjust.Target - Amb) * Adjust.Blend) * Adjust.Scale + Flash;
	Out.Total = Out.Ambient;
	for (int32 S = 0; S < FSSXRiderLight::Slots; ++S)
	{
		Out.Colour[S] = Slots.Colour[S] * Adjust.Scale;
		Out.Direction[S] = Slots.Direction[S];
		Out.Total += Out.Colour[S];
	}
	return Out;
}

FVector3d SSXTerrainLook::Blend(const FVector3d& Base, const FVector4d& Lm)
{
	auto Ch = [&Lm](double C, double L) { return FMath::Clamp((C - L) * Lm.W / 128.0, 0.0, 255.0); };
	return FVector3d(Ch(Base.X, Lm.X), Ch(Base.Y, Lm.Y), Ch(Base.Z, Lm.Z));
}

FIntVector4 SSXTerrainLook::RegionClamp(const FVector4d& Rect)
{
	const int32 MinU = int32(Rect.X * 128.0), MinV = int32(Rect.Y * 128.0);
	const int32 Span = int32(Rect.Z * 128.0) - 1;
	return FIntVector4(MinU, MinU + Span, MinV, MinV + Span);
}

FVector2D SSXTerrainLook::AtlasUV(const FVector2D& Local, const FVector2D& Origin, bool bSwapUV)
{
	FVector2D L = bSwapUV ? FVector2D(Local.Y, Local.X) : Local;
	const FVector2D Texel(FMath::Clamp(L.X * TileTexels, 0.5, TileTexels - 0.5), FMath::Clamp(L.Y * TileTexels, 0.5, TileTexels - 0.5));
	return Origin + Texel / double(TileTexels) * FVector2D(1.0 / (AtlasCols * 16.0), 1.0 / (AtlasRows * 16.0));
}


void FSSXLightWorld::VisibleHaloSequence(TConstArrayView<TPair<int32, uint16>> Cells, TArray<int32>& Out) const
{
	// 0x2006E8
	Out.Reset();
	for (const TPair<int32, uint16>& C : Cells)
	{
		if (!CellBase.IsValidIndex(C.Key) || CellBase[C.Key] < 0)
		{
			continue;
		}
		for (int32 K = 0; K < SubSide * SubSide; ++K)
		{
			const int32 Si = CellBase[C.Key] + K;
			if ((C.Value >> K) & 1 && HaloSubLists.IsValidIndex(Si))
			{
				Out.Append(HaloSubLists[Si]);
			}
		}
	}
}
