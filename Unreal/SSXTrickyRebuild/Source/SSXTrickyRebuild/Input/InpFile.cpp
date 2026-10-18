#include "Input/InpFile.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/SSXFrontEndData.h"

bool FInpFile::Load(const FString& Path, TArray<FInpFrame>& OutFrames, FString* OutError)
{
	auto Fail = [OutError](const FString& Msg) { if (OutError) { *OutError = Msg; } return false; };
	OutFrames.Reset();
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return Fail(FString::Printf(TEXT("cannot read %s"), *Path));
	}
	if (Bytes.Num() < HeaderSize)
	{
		return Fail(TEXT(".inp shorter than its header"));
	}
	auto U32 = [&Bytes](int32 Off) { uint32 V; FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); return V; };
	auto F32 = [&Bytes](int32 Off) { float V; FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); return V; };
	const int32 Count = int32(U32(0));
	if (Count < 0 || Count > FrameCapacity || HeaderSize + Count * FrameSize > Bytes.Num())
	{
		return Fail(FString::Printf(TEXT("bad frame count %d"), Count));
	}
	OutFrames.SetNum(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		const int32 O = HeaderSize + I * FrameSize;
		FInpFrame& F = OutFrames[I];
		F.RecordedWord = U32(O + 0);
		F.RecordedBools = U32(O + 4);
		F.RecordedGrabs = U32(O + 8);
		F.Pad.Buttons = U32(O + 12);
		F.Pad.ButtonsFiltered = U32(O + 16);
		F.Pad.PadX = F32(O + 20);
		F.Pad.PadY = F32(O + 24);
		F.Pad.LeftX = F32(O + 28);
		F.Pad.LeftY = F32(O + 32);
		F.Pad.RightX = F32(O + 36);
		F.Pad.RightY = F32(O + 40);
	}
	return true;
}

FString FInpFile::UnpackedDir()
{
	return FPaths::Combine(SSXFrontEnd::RepoRoot(), TEXT("unpacked"));
}

FString FInpFile::TutorialPath(const FString& Name)
{
	FString Base = FPaths::GetBaseFilename(Name).ToLower();
	FString Prefix = Base;
	Prefix.LeftChopInline(2);
	return FPaths::Combine(UnpackedDir(), TEXT("TUTORIAL"), Prefix.ToUpper(), TEXT("data"), TEXT("tutorial"), Base + TEXT(".inp"));
}
