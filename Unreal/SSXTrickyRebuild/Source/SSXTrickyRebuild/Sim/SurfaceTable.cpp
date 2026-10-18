#include "Sim/SurfaceTable.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint32 SimSurfaceMagic = 'S' | ('S' << 8) | ('X' << 16) | ('S' << 24);
}

FSurfaceParams FSurfaceTable::MakeFallback()
{
	FSurfaceParams P;
	const float Defaults[FSurfaceParams::NumFields] = {
		980.f, 0.01f, 0.f, 0.025f, 1.f, 58.3f, 2.74204f, 0.5f, 2.50417f, 5.f, 1.74f, 52.5f, 2.2f,
		1.f, 2.f, 0.4f, 0.6f, 13.f, 0.7f, 0.1f, 0.2f, 3.f, 0.6f, 0.4f, 2.f };
	FMemory::Memcpy(P.F, Defaults, sizeof(Defaults));
	return P;
}

FString FSurfaceTable::DefaultPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("surfaces.bin"));
}

bool FSurfaceTable::LoadFromFile(const FString& Path, FString* OutError)
{
	TArray<uint8> Bytes;
	auto Fail = [OutError](const FString& Msg) { if (OutError) { *OutError = Msg; } return false; };
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return Fail(FString::Printf(TEXT("cannot read %s (run tools/export_sim_data.py)"), *Path));
	}
	if (Bytes.Num() < 16)
	{
		return Fail(TEXT("surfaces.bin too short"));
	}
	uint32 Header[4];
	FMemory::Memcpy(Header, Bytes.GetData(), sizeof(Header));
	const uint32 Count = Header[2];
	const uint32 Fields = Header[3];
	if (Header[0] != SimSurfaceMagic || Header[1] != 1 || Fields != FSurfaceParams::NumFields)
	{
		return Fail(TEXT("surfaces.bin: bad magic/version/field count"));
	}
	if (Bytes.Num() < int64(16 + Count * Fields * 4))
	{
		return Fail(TEXT("surfaces.bin truncated"));
	}
	Records.SetNum(Count);
	for (uint32 I = 0; I < Count; ++I)
	{
		FMemory::Memcpy(Records[I].F, Bytes.GetData() + 16 + I * Fields * 4, Fields * 4);
	}
	return true;
}

const FSurfaceParams& FSurfaceTable::Get(int32 SurfaceId) const
{
	if (Records.IsValidIndex(SurfaceId))
	{
		return Records[SurfaceId];
	}
	return Records.IsValidIndex(1) ? Records[1] : Fallback;
}
