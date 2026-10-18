#include "Audio/SSXSnowSound.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/SSXFrontEndData.h"

namespace
{
	// 0x210C38
	const TCHAR* SurfaceNames[] = { TEXT("PACK"), TEXT("POWDER"), TEXT("LOOSE"), TEXT("ICE"), TEXT("METAL"),
		TEXT("WOOD"), TEXT("RAIL"), TEXT("ROCK"), TEXT("GLASS"), TEXT("CHUTE") };
	const TCHAR* ModeNames[] = { TEXT("GLIDE"), TEXT("AIGLIDE"), TEXT("CARVE") };
	constexpr int32 NumModes = int32(ESSXSnowMode::Count);
	static_assert(UE_ARRAY_COUNT(SurfaceNames) == int32(ESSXSnowSurface::Count), "surface table");

	bool IsWs(TCHAR C) { return C == TEXT(' ') || C == TEXT('\t') || C == TEXT('\r') || C == TEXT('\n'); }

	struct FReader
	{
		const FString& T;
		int32 P = 0;
		explicit FReader(const FString& In) : T(In) {}

		bool SeekSection(const TCHAR* Name)
		{
			for (;;)
			{
				const int32 I = T.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, P);
				if (I == INDEX_NONE) { P = T.Len(); return false; }
				const int32 J = T.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I);
				if (J == INDEX_NONE) { P = T.Len(); return false; }
				P = J + 1;
				if (T.Mid(I + 1, J - I - 1).TrimStartAndEnd().Equals(Name, ESearchCase::IgnoreCase)) { return true; }
			}
		}
		void Skip()
		{
			while (P < T.Len() && IsWs(T[P])) { ++P; }
			while (P < T.Len() && T[P] == TEXT('#'))
			{
				const int32 E = T.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, P);
				P = E == INDEX_NONE ? T.Len() : E;
				while (P < T.Len() && IsWs(T[P])) { ++P; }
			}
		}
		bool AtEnd() { Skip(); return P >= T.Len() || T[P] == TEXT('['); }
		FString Token()
		{
			Skip();
			const int32 S = P;
			while (P < T.Len() && !IsWs(T[P])) { ++P; }
			return T.Mid(S, P - S);
		}
		int32 Int()
		{
			const FString Tok = Token();
			int64 V = 0;
			int32 K = 0, Digits = 0;
			bool bNeg = false;
			if (K < Tok.Len() && (Tok[K] == TEXT('+') || Tok[K] == TEXT('-'))) { bNeg = Tok[K] == TEXT('-'); ++K; }
			for (; K < Tok.Len() && FChar::IsDigit(Tok[K]); ++K, ++Digits) { V = V * 10 + (Tok[K] - TEXT('0')); }
			return Digits ? int32(bNeg ? -V : V) : 0;
		}
	};

	int32 Wrap(int64 V) { return int32(uint32(uint64(V))); }
	int32 TDiv(int32 A, int32 B)
	{
		if (B == 0) { return 0; }   // TODO: division by zero is not specified;
		return Wrap(int64(A) / int64(B));
	}
}

const TCHAR* FSSXSnowPrograms::SurfaceName(ESSXSnowSurface S)
{
	return int32(S) < int32(ESSXSnowSurface::Count) ? SurfaceNames[int32(S)] : TEXT("?");
}

const TCHAR* FSSXSnowPrograms::ModeName(ESSXSnowMode M)
{
	return int32(M) < NumModes ? ModeNames[int32(M)] : TEXT("?");
}

FString FSSXSnowPrograms::DefaultPath()
{
	const FString Repo = SSXFrontEnd::RepoRoot();
	const FString Converted = FPaths::Combine(Repo, TEXT("converted"), TEXT("audio"), TEXT("config"), TEXT("SNOW.INF"));
	if (FPaths::FileExists(Converted))
	{
		return Converted;
	}
	return FPaths::Combine(Repo, TEXT("extracted"), TEXT("DATA"), TEXT("CONFIG"), TEXT("SNOW.INF"));
}

bool FSSXSnowPrograms::LoadDefault(FString* OutError)
{
	FString Text;
	const FString Path = DefaultPath();
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s"), *Path); }
		bLoaded = false;
		return false;
	}
	return Parse(Text, OutError);
}

FSSXSnowPrograms::FProgram FSSXSnowPrograms::Compile(const FString& Text, ESSXSnowSurface Surface, ESSXSnowMode Mode)
{
	static const TCHAR* LoadSrc[] = { TEXT("SLIP"), TEXT("DIG"), TEXT("LEAN"), TEXT("BOARD"), TEXT("BEND") };
	static const struct { const TCHAR* Name; int32 Op; } IntOps[] = {
		{ TEXT("LOADCONST"), OpLoadConst }, { TEXT("MAP"), OpMap }, { TEXT("ADD"), OpAdd }, { TEXT("SUB"), OpSub },
		{ TEXT("MUL"), OpMul }, { TEXT("DIV"), OpDiv } };
	static const struct { const TCHAR* Name; int32 Op; } BareOps[] = {
		{ TEXT("SQUARE"), OpSquare }, { TEXT("SQRT"), OpSqrt }, { TEXT("LOG"), OpLog }, { TEXT("NEG"), OpNeg },
		{ TEXT("ABS"), OpAbs }, { TEXT("PUSH"), OpPush }, { TEXT("POP"), OpPop }, { TEXT("SADD"), OpSAdd },
		{ TEXT("SSUB"), OpSSub }, { TEXT("SMUL"), OpSMul }, { TEXT("SDIV"), OpSDiv }, { TEXT("ASSIGNVOL"), OpAssignVol },
		{ TEXT("ASSIGNBEND"), OpAssignBend } };
	FReader R(Text);
	R.SeekSection(SurfaceName(Surface));
	R.SeekSection(ModeName(Mode));
	FProgram Code;
	while (!R.AtEnd())
	{
		const FString Tok = R.Token().ToUpper();
		if (Tok == TEXT("LOAD"))
		{
			Code.Add(OpLoad);
			const FString Src = R.Token().ToUpper();
			for (int32 K = 0; K < 5; ++K)
			{
				if (Src == LoadSrc[K]) { Code.Add(K); break; }   // no selector byte for anything else (compiler quirk)
			}
			continue;
		}
		bool bDone = false;
		for (const auto& O : IntOps)
		{
			if (Tok == O.Name) { Code.Add(O.Op); Code.Add(R.Int()); bDone = true; break; }
		}
		if (bDone) { continue; }
		if (Tok == TEXT("BOUND"))
		{
			Code.Add(OpBound);
			const int32 Lo = R.Int();
			const int32 Hi = R.Int();
			Code.Add(Lo);
			Code.Add(Hi);
			continue;
		}
		for (const auto& O : BareOps)
		{
			if (Tok == O.Name) { Code.Add(O.Op); break; }
		}
	}
	Code.Add(OpEnd);
	return Code;
}

bool FSSXSnowPrograms::Parse(const FString& Text, FString* OutError)
{
	for (int32 S = 0; S < int32(ESSXSnowSurface::Count); ++S)
	{
		for (int32 M = 0; M < NumModes; ++M)
		{
			Programs[S * NumModes + M] = Compile(Text, ESSXSnowSurface(S), ESSXSnowMode(M));
		}
	}
	bLoaded = true;
	return true;
}

const FSSXSnowPrograms::FProgram& FSSXSnowPrograms::Program(ESSXSnowSurface Surface, ESSXSnowMode Mode) const
{
	static const FProgram Empty = { OpEnd };
	const int32 I = int32(Surface) * NumModes + int32(Mode);
	return (I >= 0 && I < UE_ARRAY_COUNT(Programs) && Programs[I].Num()) ? Programs[I] : Empty;
}

FSSXSnowResult FSSXSnowPrograms::Evaluate(ESSXSnowSurface Surface, ESSXSnowMode Mode, const FSSXSnowInputs& In) const
{
	return Run(Program(Surface, Mode), In);
}

FSSXSnowResult FSSXSnowPrograms::Run(const FProgram& Code, const FSSXSnowInputs& In, FSSXSnowResult R)
{
	int32 Acc = 0;
	TArray<int32, TInlineAllocator<16>> Stack;
	auto PopV = [&Stack]() { return Stack.Num() ? Stack.Pop(EAllowShrinking::No) : 0; };
	int32 Pc = 0;
	auto Arg = [&Code, &Pc]() { return Code.IsValidIndex(Pc) ? Code[Pc++] : 0; };
	while (Code.IsValidIndex(Pc))
	{
		const int32 Op = Code[Pc++];
		switch (Op)
		{
		case OpEnd: return R;
		case OpLoad:
		{
			const int32 Sel = Arg();
			const int32 Src[5] = { In.Slip, In.Dig, In.Lean, In.Board, In.Bend };
			if (Sel >= 0 && Sel <= 4) { Acc = Src[Sel]; }
			break;
		}
		case OpLoadConst: Acc = Arg(); break;
		case OpMap: { const int32 N = Arg(); Acc = FMath::Clamp(TDiv(Wrap(int64(Acc) * 127), N), 0, 127); break; }
		case OpSquare: Acc = Wrap(int64(Acc) * Acc); break;
		case OpSqrt:
		{
			const float F = float(FMath::Sqrt(FMath::Abs(double(Acc))));
			Acc = int32(FMath::Clamp(double(F), -2147483648.0, 2147483647.0));
			break;
		}
		case OpLog: break;
		case OpAdd: Acc = Wrap(int64(Acc) + Arg()); break;
		case OpSub: Acc = Wrap(int64(Acc) - Arg()); break;
		case OpMul: Acc = Wrap(int64(Acc) * Arg()); break;
		case OpDiv: Acc = TDiv(Acc, Arg()); break;
		case OpNeg: Acc = Wrap(-int64(Acc)); break;
		case OpAbs: Acc = Wrap(FMath::Abs(int64(Acc))); break;
		case OpBound: { const int32 Lo = Arg(); const int32 Hi = Arg(); Acc = FMath::Max(Lo, FMath::Min(Hi, Acc)); break; }
		case OpPush: Stack.Add(Acc); break;
		case OpPop: Acc = PopV(); break;
		case OpSAdd: case OpSSub: case OpSMul: case OpSDiv:
		{
			const int32 Top = PopV();
			const int32 Second = PopV();
			Stack.Add(Op == OpSAdd ? Wrap(int64(Top) + Second) : Op == OpSSub ? Wrap(int64(Top) - Second)
				: Op == OpSMul ? Wrap(int64(Top) * Second) : TDiv(Top, Second));
			break;
		}
		case OpAssignVol: R.Volume = Acc; break;
		case OpAssignBend: R.Bend = Acc; break;
		default: break;
		}
	}
	return R;
}
