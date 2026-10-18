// 0x21A7C8 / 0x21AE90
#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXAudioTypes.h"

struct FSSXSnowInputs
{
	int32 Slip = 0;    // |trunc(dot4(contact_forward, velocity))|: cm/s along the board's forward axis
	int32 Dig = 0;     // |trunc(ground drag +0x160)|: cm/s^2
	int32 Lean = 0;   // |trunc(steer_cur * 127)|: 0..127
	int32 Board = 0;
	int32 Bend = 0;
};

struct FSSXSnowResult
{
	int32 Volume = 0;
	int32 Bend = 0x40;   // pitch bend, 0x40 = none
};

class SSXTRICKYREBUILD_API FSSXSnowPrograms
{
public:
	enum EOp : int32
	{
		OpLoad = 0, OpLoadConst, OpMap, OpSquare, OpSqrt, OpLog, OpAdd, OpSub, OpMul, OpDiv, OpNeg, OpAbs, OpBound,
		OpPush, OpPop, OpSAdd, OpSSub, OpSMul, OpSDiv, OpAssignVol, OpAssignBend, OpEnd = 0x7F,
	};
	using FProgram = TArray<int32>;

	bool Parse(const FString& Text, FString* OutError = nullptr);
	bool LoadDefault(FString* OutError = nullptr);
	static FString DefaultPath();
	// 0x21A7C8
	static FProgram Compile(const FString& Text, ESSXSnowSurface Surface, ESSXSnowMode Mode);

	bool IsLoaded() const { return bLoaded; }
	const FProgram& Program(ESSXSnowSurface Surface, ESSXSnowMode Mode) const;
	FSSXSnowResult Evaluate(ESSXSnowSurface Surface, ESSXSnowMode Mode, const FSSXSnowInputs& In) const;
	static FSSXSnowResult Run(const FProgram& Program, const FSSXSnowInputs& In, FSSXSnowResult Defaults = FSSXSnowResult());

	static const TCHAR* SurfaceName(ESSXSnowSurface S);
	static const TCHAR* ModeName(ESSXSnowMode M);

private:
	FProgram Programs[int32(ESSXSnowSurface::Count) * int32(ESSXSnowMode::Count)];
	bool bLoaded = false;
};
