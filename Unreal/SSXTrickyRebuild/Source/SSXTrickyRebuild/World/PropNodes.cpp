#include "World/PropNodes.h"
#include "Sim/SimCoords.h"
#include "Race/GameRng.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SSXPropNodes
{
namespace
{
	constexpr double Deg = 0.017453292;
	constexpr uint32 PropMagic = 0x50585353;

	int32 RecI(const uint8* R, int32 K) { int32 X; FMemory::Memcpy(&X, R + 8 + 4 * K, 4); return X; }
	double RecF(const uint8* R, int32 K) { float X; FMemory::Memcpy(&X, R + 8 + 4 * K, 4); return double(X); }

	FQuat4d QMul(const FQuat4d& A, const FQuat4d& B)
	{
		FQuat4d R;
		R.W = A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z;
		R.X = A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y;
		R.Y = A.W * B.Y + A.Y * B.W + A.Z * B.X - A.X * B.Z;
		R.Z = A.W * B.Z + A.Z * B.W + A.X * B.Y - A.Y * B.X;
		return R;
	}
}

double FNodeAnim::Channel(int32 K, double T, int32* Cache) const
{
	if (K < 0 || K >= 6 || Tracks[K].Num() == 0)
	{
		return (K >= 0 && K < 6) ? Base[K] : 0.0;
	}
	const TArray<FKeySegment>& Segs = Tracks[K];
	int32 I = Cache ? *Cache : 0;
	const FKeySegment* S = Segs.IsValidIndex(I) ? &Segs[I] : &Segs[0];
	if (!(S->T0 <= T && T < S->T1))
	{
		I = I + 1;
		if (Segs.IsValidIndex(I) && Segs[I].T0 <= T && T < Segs[I].T1)
		{
			S = &Segs[I];
		}
		else
		{
			I = 0;
			while (I < Segs.Num() - 1 && !(T < Segs[I].T1))
			{
				++I;
			}
			S = &Segs[I];
		}
		if (Cache)
		{
			*Cache = I;
		}
	}
	const double Tt = T < S->T0 ? S->T0 : (T > S->T1 ? S->T1 : T);
	return S->Value(Tt);
}

FMatrix FNodeAnim::Matrix(double T, int32* Cache) const
{
	double Ch[6];
	for (int32 K = 0; K < 6; ++K)
	{
		Ch[K] = Channel(K, T, Cache ? Cache + K : nullptr);
	}
	const double Sa = FMath::Sin(-Ch[3] * Deg), Ca = FMath::Cos(-Ch[3] * Deg);
	const double Sb = FMath::Sin(-Ch[4] * Deg), Cb = FMath::Cos(-Ch[4] * Deg);
	const double Sg = FMath::Sin(-Ch[5] * Deg), Cg = FMath::Cos(-Ch[5] * Deg);
	FMatrix M;
	M.M[0][0] = Cb * Cg; M.M[0][1] = -Cb * Sg; M.M[0][2] = Sb; M.M[0][3] = 0.0;
	M.M[1][0] = Sa * Sb * Cg + Ca * Sg; M.M[1][1] = Ca * Cg - Sa * Sb * Sg; M.M[1][2] = -Sa * Cb; M.M[1][3] = 0.0;
	M.M[2][0] = Sa * Sg - Ca * Sb * Cg; M.M[2][1] = Ca * Sg * Sb + Sa * Cg; M.M[2][2] = Ca * Cb; M.M[2][3] = 0.0;
	M.M[3][0] = Ch[0]; M.M[3][1] = Ch[1]; M.M[3][2] = Ch[2]; M.M[3][3] = 1.0;
	return M;
}

FString FPropAnimData::DefaultPath(const FString& Track)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Track.ToLower() + TEXT(".propanim.bin"));
}

const TArray<int32>* FPropAnimData::FirstFlipbookOfInstance(int32 Inst) const
{
	const int32 M = InstanceModel.IsValidIndex(Inst) ? InstanceModel[Inst] : INDEX_NONE;
	if (!ModelMaterials.IsValidIndex(M))
	{
		return nullptr;
	}
	for (const FMaterialRef& R : ModelMaterials[M])
	{
		if (Flipbooks.IsValidIndex(R.Flipbook)) { return &Flipbooks[R.Flipbook]; }
	}
	return nullptr;
}

int32 FPropAnimData::FlipTextureFor(int32 Inst, int32 BaseTexture, int32 FlipFrame) const
{
	const int32 M = InstanceModel.IsValidIndex(Inst) ? InstanceModel[Inst] : INDEX_NONE;
	if (!ModelMaterials.IsValidIndex(M) || FlipFrame < 0)
	{
		return INDEX_NONE;
	}
	for (const FMaterialRef& R : ModelMaterials[M])
	{
		if (R.Texture == BaseTexture && Flipbooks.IsValidIndex(R.Flipbook) && Flipbooks[R.Flipbook].Num())
		{
			const TArray<int32>& Fb = Flipbooks[R.Flipbook];
			return Fb[FMath::Min(FlipFrame, Fb.Num() - 1)];
		}
	}
	return INDEX_NONE;
}

bool FPropAnimData::Load(const FString& Path, FString* OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s (run tools/export_prop_anim.py)"), *Path); }
		return false;
	}
	return LoadFromMemory(Bytes, OutError);
}

bool FPropAnimData::LoadFromMemory(TConstArrayView<uint8> Bytes, FString* OutError)
{
	bLoaded = false;
	auto Fail = [OutError](const TCHAR* Msg) { if (OutError) { *OutError = Msg; } return false; };
	const uint8* D = Bytes.GetData();
	const int64 Size = Bytes.Num();
	auto U32 = [D](int64 O) { uint32 X; FMemory::Memcpy(&X, D + O, 4); return X; };
	auto I32 = [D](int64 O) { int32 X; FMemory::Memcpy(&X, D + O, 4); return X; };
	auto F32 = [D](int64 O) { float X; FMemory::Memcpy(&X, D + O, 4); return double(X); };
	const uint32 Version = Size >= 8 ? U32(4) : 0;
	if (Size < 32 || U32(0) != PropMagic || (Version != 2 && Version != 3))
	{
		return Fail(TEXT("propanim: bad header (re-run tools/export_prop_anim.py)"));
	}
	const int32 NM = I32(8), NO = I32(12), NA = I32(16), NS = I32(20), NI = I32(24);
	const int64 Need = 32 + int64(NM) * 16 + int64(NO) * 80 + int64(NA) * 76 + int64(NS) * 24 + int64(NI) * 4;
	if (NM < 0 || NO < 0 || NA < 0 || NS < 0 || NI < 0 || Need + 4 > Size)
	{
		return Fail(TEXT("propanim: size mismatch"));
	}
	int64 P = 32;
	Models.SetNum(NM);
	for (FPropModelInfo& M : Models)
	{
		M.EndFrame = F32(P); M.Flags = U32(P + 4); M.FirstObject = I32(P + 8); M.NumObjects = I32(P + 12);
		P += 16;
	}
	Objects.SetNum(NO);
	for (FPropObjectInfo& O : Objects)
	{
		O.Parent = I32(P); O.Anim = I32(P + 4); O.Meshes = I32(P + 8);
		for (int32 R = 0; R < 4; ++R)
		{
			for (int32 C = 0; C < 4; ++C) { O.Matrix.M[R][C] = F32(P + 16 + 4 * (4 * R + C)); }
		}
		P += 80;
	}
	struct FRaw { int32 First[6]; int32 Count[6]; };
	TArray<FRaw> Raw;
	Raw.SetNum(NA);
	Anims.SetNum(NA);
	for (int32 A = 0; A < NA; ++A)
	{
		for (int32 K = 0; K < 6; ++K) { Anims[A].Base[K] = F32(P + 4 * K); }
		Anims[A].Mask = U32(P + 24);
		for (int32 K = 0; K < 6; ++K) { Raw[A].First[K] = I32(P + 28 + 4 * K); Raw[A].Count[K] = I32(P + 52 + 4 * K); }
		P += 76;
	}
	const int64 SegBase = P;
	for (int32 A = 0; A < NA; ++A)
	{
		for (int32 K = 0; K < 6; ++K)
		{
			if (Raw[A].First[K] < 0 || Raw[A].First[K] + Raw[A].Count[K] > NS)
			{
				return Fail(TEXT("propanim: segment range"));
			}
			for (int32 S = 0; S < Raw[A].Count[K]; ++S)
			{
				const int64 O = SegBase + 24 * int64(Raw[A].First[K] + S);
				FKeySegment G;
				G.A = F32(O); G.B = F32(O + 4); G.C = F32(O + 8); G.D = F32(O + 12); G.T0 = F32(O + 16); G.T1 = F32(O + 20);
				Anims[A].Tracks[K].Add(G);
			}
		}
	}
	P = SegBase + 24 * int64(NS);
	InstanceModel.SetNum(NI);
	for (int32 I = 0; I < NI; ++I) { InstanceModel[I] = I32(P + 4 * I); }
	P += 4 * int64(NI);
	const int32 NF = I32(P);
	P += 4;
	Flipbooks.Reset();
	ModelMaterials.Reset();
	for (int32 F = 0; F < NF; ++F)
	{
		if (P + 4 > Size) { return Fail(TEXT("propanim: flipbooks truncated")); }
		const int32 N = I32(P);
		if (N < 0 || P + 4 + 4 * int64(N) > Size) { return Fail(TEXT("propanim: flipbooks truncated")); }
		TArray<int32>& Fb = Flipbooks.AddDefaulted_GetRef();
		for (int32 K = 0; K < N; ++K) { Fb.Add(I32(P + 4 + 4 * K)); }
		P += 4 + 4 * int64(N);
	}
	ModelMaterials.SetNum(NM);
	for (TArray<FMaterialRef>& MM : ModelMaterials)
	{
		if (P + 4 > Size) { return Fail(TEXT("propanim: materials truncated")); }
		const int32 N = I32(P);
		if (N < 0 || P + 4 + 8 * int64(N) > Size) { return Fail(TEXT("propanim: materials truncated")); }
		for (int32 K = 0; K < N; ++K)
		{
			FMaterialRef R;
			R.Texture = I32(P + 4 + 8 * K);
			R.Flipbook = I32(P + 8 + 8 * K);
			if (R.Flipbook >= NF) { return Fail(TEXT("propanim: flipbook index")); }
			MM.Add(R);
		}
		P += 4 + 8 * int64(N);
	}
	// version 3: Flag / Fence strip sources, 256 bytes each
	Strips.Reset();
	if (Version >= 3)
	{
		if (P + 4 > Size) { return Fail(TEXT("propanim: strips truncated")); }
		const int32 NStrip = I32(P);
		P += 4;
		if (NStrip < 0 || P + 256 * int64(NStrip) > Size) { return Fail(TEXT("propanim: strips truncated")); }
		for (int32 K = 0; K < NStrip; ++K, P += 256)
		{
			FStripSource S;
			S.Inst = I32(P);
			auto V3 = [&](int64 O) { return FVector(F32(O), F32(O + 4), F32(O + 8)); };
			S.Scale = V3(P + 4);
			for (int32 C = 0; C < 4; ++C)
			{
				S.Pos[C] = V3(P + 16 + 12 * C);
				S.Nrm[C] = V3(P + 64 + 12 * C);
				S.UV[C] = FVector2D(F32(P + 112 + 8 * C), F32(P + 116 + 8 * C));
			}
			for (int32 L = 0; L < 3; ++L)
			{
				S.LightDir[L] = V3(P + 144 + 12 * L);
				S.LightColour[L] = FVector4(F32(P + 180 + 16 * L), F32(P + 184 + 16 * L), F32(P + 188 + 16 * L), F32(P + 192 + 16 * L));
			}
			S.Ambient = FVector4(F32(P + 228), F32(P + 232), F32(P + 236), F32(P + 240));
			S.NeighbourC4 = I32(P + 244);
			S.NeighbourC8 = I32(P + 248);
			S.InstFlags = U32(P + 252);
			if (S.Inst < 0 || S.Inst >= NI) { return Fail(TEXT("propanim: strip instance")); }
			Strips.Add(S.Inst, S);
		}
	}
	if (P != Size)
	{
		return Fail(TEXT("propanim: size mismatch"));
	}
	bLoaded = true;
	return true;
}

const FPropModelInfo* FPropAnimData::ModelOfInstance(int32 Inst) const
{
	return InstanceModel.IsValidIndex(Inst) && Models.IsValidIndex(InstanceModel[Inst]) ? &Models[InstanceModel[Inst]] : nullptr;
}

void FPropAnimData::ObjectWorldMatrices(int32 Inst, const FMatrix& InstMatrix, double T, bool bAnimated, TArray<FMatrix>& Out) const
{
	WorldMatrices(Inst, InstMatrix, T, bAnimated, nullptr, Out);
}

void FPropAnimData::ComboWorldMatrices(int32 Inst, const FMatrix& InstMatrix, double T, double SnapshotT, TArray<FMatrix>& Out) const
{
	WorldMatrices(Inst, InstMatrix, T, true, &SnapshotT, Out);
}

void FPropAnimData::WorldMatrices(int32 Inst, const FMatrix& InstMatrix, double T, bool bAnimated, const double* SnapshotT, TArray<FMatrix>& Out) const
{
	Out.Reset();
	const FPropModelInfo* M = ModelOfInstance(Inst);
	if (!M)
	{
		return;
	}
	const int32 N = M->NumObjects;
	Out.SetNum(N);
	TArray<uint8> Done;
	Done.Init(0, N);
	TFunction<void(int32, int32)> Solve = [&](int32 K, int32 Depth)
	{
		if (Done[K])
		{
			return;
		}
		const FPropObjectInfo& O = Objects[M->FirstObject + K];
		FMatrix Local = (bAnimated && Anims.IsValidIndex(O.Anim)) ? Anims[O.Anim].Matrix(T) : O.Matrix;
		if (SnapshotT && Anims.IsValidIndex(O.Anim))
		{
			// 0x1CB658
			Local = Local * Anims[O.Anim].Matrix(*SnapshotT);
		}
		if (O.Parent >= 0 && O.Parent < N && O.Parent != K && Depth < N)
		{
			Solve(O.Parent, Depth + 1);
			Out[K] = Local * Out[O.Parent];
		}
		else
		{
			Out[K] = Local * InstMatrix;
		}
		Done[K] = 1;
	};
	for (int32 K = 0; K < N; ++K)
	{
		Solve(K, 0);
	}
}

bool FAnimObjectRecord::Parse(const uint8* R, int32 Size, FAnimObjectRecord& Out)
{
	if (!R || Size < 44)
	{
		return false;
	}
	Out.Subtype = RecI(R, 0);
	Out.Mode = int32(int16(RecI(R, 1) & 0xFFFF));
	Out.Start = RecF(R, 2);
	Out.End = RecF(R, 3);
	Out.Speed = RecF(R, 4);
	Out.SpeedRand = RecF(R, 5);
	Out.Cooldown = RecF(R, 6);
	Out.RandomStart = RecI(R, 7);
	Out.Direction = RecI(R, 8);
	if (Out.Subtype == KindAnimCombo)
	{
		if (Size < 60)
		{
			return false;
		}
		Out.ComboStart = RecF(R, 9);
		Out.ComboEnd = RecF(R, 10);
		Out.ComboSpeed = RecF(R, 11);
		Out.ComboHold = RecI(R, 12);
	}
	return true;
}

void FAnimObject::InitGame(const FAnimObjectRecord& R, double ModelEndFrame, bool bCreatedByTouch)
{
	Init(R, ModelEndFrame, [](double Lo, double Hi) { return double(FGameRng::StreamB().Uniform(float(Lo), float(Hi))); }, bCreatedByTouch);
}

void FAnimObject::Init(const FAnimObjectRecord& R, double ModelEndFrame, TFunctionRef<double(double, double)> RandRange, bool bCreatedByTouch)
{
	Mode = R.Mode;
	TStart = FMath::Max(R.Start, 0.0) / KeyRate;
	TEnd = (R.End >= 0.0 ? R.End : ModelEndFrame) / KeyRate;
	T = TStart;
	if (R.RandomStart)
	{
		T = RandRange(TStart, TEnd);
	}
	double Spd = R.Speed / KeyRate;
	if (R.SpeedRand != 0.0)
	{
		Spd = Spd + (R.SpeedRand / KeyRate) * RandRange(-1.0, 1.0);
	}
	Rate = Spd * Frame;
	if (R.Direction == 4)
	{
		Rate = -Rate;
	}
	CooldownFrames = int32(R.Cooldown * 60.0);
	Cooldown = bCreatedByTouch ? CooldownFrames : 0;
	bRunning = true;
	bFinished = false;
	Delay = 0;
	Kind = R.Subtype;
	Budget = 0.0f;
	bCombo = bComboHeld = false;
	ComboT = 0.0;
	if (Kind == KindAnimDelta)
	{
		bRunning = false;   // 0x199D10
	}
	else if (Kind == KindAnimCombo)
	{
		// 0x199F20
		ComboStart = (R.ComboStart < 0.0 ? R.End : R.ComboStart) / KeyRate;
		ComboEnd = (R.ComboEnd < 0.0 ? ModelEndFrame : R.ComboEnd) / KeyRate;
		ComboRate = (R.ComboSpeed / KeyRate) * Frame;
		bComboHoldAtEnd = R.ComboHold != 0;
		bComboHeldPose = R.ComboHold < 0;
	}
	EvalT = T;
	SnapshotT = T;
	bEvaluated = true;
}

void FAnimObject::Message(int32 Msg, double V)
{
	if (Msg == 1)
	{
		Delay = int32(V * 60.0);
	}
	else if (Msg == 4)
	{
		const double Tm = V / KeyRate;
		if (TStart <= Tm && Tm <= TEnd)
		{
			T = Tm;
		}
	}
	else if (Msg == 2 && Kind == KindAnimDelta)
	{
		Budget = Budget + float(V) / 30.0f;   // 0x199EB0
	}
	else if (Msg == 3 && Kind == KindAnimCombo && !bCombo && !bComboHeld)
	{
		// 0x19A2D0
		SnapshotT = EvalT;
		bCombo = true;
		ComboT = ComboStart;
	}
}

void FAnimObject::TickCooldown()
{
	if (CooldownFrames >= 0 && Cooldown > 0)
	{
		--Cooldown;
	}
}

void FAnimObject::Evaluated()
{
	bEvaluated = true;
	if (!ComboPose())
	{
		EvalT = T;
	}
}

bool FAnimObject::Tick()
{
	bEvaluated = false;
	if (Kind == KindAnimDelta)
	{
		// 0x199DB8
		if (bFinished)
		{
			TickCooldown();
			return false;
		}
		if (Budget >= 0.0f)
		{
			if (Budget > 0.0f)
			{
				Budget = Budget - FMath::Abs(float(Rate));
				if (Budget < 0.0f) { Budget = 0.0f; }
			}
			const bool bDone = TickObject();
			bRunning = Budget > 0.0f;
			return bDone;
		}
		TickCooldown();
		if (Delay > 0) { --Delay; }
		return false;
	}
	if (Kind == KindAnimCombo)
	{
		// 0x19A218
		if (bComboHeld)
		{
			TickCooldown();
			return false;
		}
		if (!bCombo)
		{
			return TickObject();
		}
		TickCooldown();
		ComboT += ComboRate;
		if (ComboEnd <= ComboT)
		{
			ComboT = ComboEnd;
			bComboHeld = bComboHoldAtEnd;
			bCombo = false;
		}
		Evaluated();
		return false;
	}
	return TickObject();
}

bool FAnimObject::TickObject()
{
	TickCooldown();
	if (bFinished)
	{
		return false;
	}
	if (bRunning)
	{
		if (Delay <= 0)
		{
			T += Rate;
			if (Rate >= 0.0)
			{
				if (T >= TEnd)
				{
					if (Mode == AnimPingPong) { T = TEnd - (T - TEnd); Rate = -Rate; }
					else if (Mode == AnimLoop) { T = TStart + (T - TEnd); }
					else { T = TEnd; bFinished = Mode == AnimOnce; }
				}
			}
			else if (T <= TStart)
			{
				if (Mode == AnimPingPong) { T = TStart + (TStart - T); Rate = -Rate; }
				else if (Mode == AnimLoop) { T = TEnd - (TStart - T); }
				else { T = TStart; bFinished = Mode == AnimOnce; }
			}
		}
		if (Delay > 0)
		{
			--Delay;
		}
	}
	Evaluated();
	return bFinished;
}

const int32 FCrowdBox::Seq[4][16] = {
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 0, 1, 2, 3, 2, 1, 0, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1 },
};
const int32 FCrowdBox::PauseChance[7] = { 256, 0, 50, 100, 200, 100, 50 };

int32 FCrowdBox::FrameOf(int32 Counter, int32 InAnim, int32 Person)
{
	const int32* S = Seq[InAnim & 3];
	return Counter < 0 ? S[0] : S[(Counter + Person) & 0xF];
}

void FCrowdBox::Init(int32 InRows, int32 InCols, TFunctionRef<uint32()> Rand)
{
	Rows = InRows;
	Cols = InCols;
	State = 0;
	for (int32 K = 0; K < Slots; ++K)
	{
		Counter[K] = int32(Rand() & 0xF);
		Anim[K] = int32(Rand() & 3);
	}
	Step(Rand);
}

void FCrowdBox::Update(int32 RaceFrame, bool bPreRace, TConstArrayView<TPair<double, int32>> Humans, TFunctionRef<uint32()> Rand)
{
	if (RaceFrame % Tick)
	{
		return;
	}
	if (bPreRace)
	{
		State = 7;
	}
	else
	{
		State = 0;
		for (const TPair<double, int32>& H : Humans)
		{
			if (H.Key <= Range)
			{
				State = (H.Value + 4) & 7;
			}
		}
	}
	Step(Rand);
}

void FCrowdBox::Step(TFunctionRef<uint32()> Rand)
{
	if (State != 0)
	{
		for (int32 K = 0; K < Slots; ++K)
		{
			const int32 C = ++Counter[K];
			if (C > 15 && (C & 0xF) == 0)
			{
				const uint32 R = Rand();
				Anim[K] = int32(R & 3);
				if (State < 7 && int32(R & 0xFF) < PauseChance[State])
				{
					Counter[K] = -int32(R & 0x3F);
				}
			}
		}
	}
	const int32 N = FMath::Min(Rows * Cols, Slots);
	for (int32 I = 0; I < N; ++I)
	{
		Frames[I] = FrameOf(Counter[I], Anim[I], I);
	}
}

void FFlagStrip::Init(int32 Dir, double Hz, double Amp, double Seconds, double ModelScale, TFunctionRef<double()> Rand01)
{
	Direction = Dir;
	Rate = Hz * Frame;
	if (Amp == -1.0)
	{
		Amp = (Rand01() * 45.0 + 5.0) * Frame;
	}
	if (Rate == -1.0)
	{
		Rate = (Rand01() * 1.5 + 0.5) * Frame;
	}
	Amplitude = Amp / (FMath::Max(ModelScale, 1.0) * StripModelScaleK);
	Phase = Rand01();
	Timer = int32(Seconds * 60.0);
}

bool FFlagStrip::Tick()
{
	if (Timer > 0 && --Timer == 0)
	{
		return true;
	}
	Phase += Rate;
	if (Phase >= 1.0)
	{
		Phase -= 1.0;
	}
	return false;
}

void FFlagStrip::Offsets(double Out[StripCols]) const
{
	for (int32 K = 0; K < StripCols; ++K) { Out[K] = 0.0; }
	for (int32 K = 1; K < StripCols; ++K)
	{
		const double V = Amplitude * (K / 8.0) * FMath::Sin(-2.0 * PI * Phase + 2.0 * PI * K / 8.0);
		// 0x144EC8
		Out[Direction == 0 ? K : StripCols - 1 - K] = V;
	}
}

void FFenceStrip::Hit(double Amount)
{
	Amp = FMath::Min(Amp + FMath::Abs(Amount), 1.0);
	const bool bNeg = Rate == 0.0 ? Amount < 0.0 : Rate < 0.0;
	Rate = Amp * (bNeg ? -0.1 : 0.1);
}

void FFenceStrip::Touch(double Amount)
{
	if (Amp < 1.0)
	{
		Hit(Amount);
		if (Timer40 == 0) { Timer40 = 4; }
		if (Timer44 == 0) { Timer44 = 4; }
	}
}

bool FFenceStrip::Tick(TFunctionRef<void(int32 Side, double Amount)> Fire)
{
	if (Timer40 > 0 && --Timer40 == 0) { Fire(1, NeighbourAmount()); }
	if (Timer44 > 0 && --Timer44 == 0) { Fire(0, NeighbourAmount()); }
	Theta += Rate;
	if (Amp < 0.01 && FMath::Abs(Theta) < 0.01)
	{
		return true;
	}
	if (Rate >= 0.0)
	{
		if (Theta >= Amp) { Theta = Amp; Amp *= 0.5; Rate = -Amp * 0.1; }
	}
	else if (Theta <= -Amp)
	{
		Theta = -Amp; Amp *= 0.5; Rate = Amp * 0.1;
	}
	return false;
}

void FFenceStrip::Offsets(double Out[StripCols]) const
{
	static const double Profile[StripCols] = { 0.0, 0.3827, 0.7071, 0.9239, 0.9239, 0.7071, 0.3827, 0.0 };
	for (int32 K = 0; K < StripCols; ++K) { Out[K] = Theta * Scale * 200.0 * Profile[K]; }
}

void StripLocal(const FStripSource& S, const double Offsets[StripCols], bool bBothRows, FVector OutPos[StripVerts], FVector2D OutUV[StripVerts])
{
	FVector C[4];
	for (int32 I = 0; I < 4; ++I) { C[I] = FVector(S.Pos[I].X * StripCornerXScale, S.Pos[I].Y, S.Pos[I].Z); }
	for (int32 K = 0; K < StripCols; ++K)
	{
		const double T = K * (1.0 / 7.0);
		FVector Top = C[0] + (C[1] - C[0]) * T;
		FVector Bot = C[2] + (C[3] - C[2]) * T;
		Top.X += Offsets[K];
		if (bBothRows) { Bot.X += Offsets[K]; }
		OutPos[2 * K] = Top;
		OutPos[2 * K + 1] = Bot;
		OutUV[2 * K] = S.UV[0] + (S.UV[1] - S.UV[0]) * T;
		OutUV[2 * K + 1] = S.UV[2] + (S.UV[3] - S.UV[2]) * T;
	}
}

FMatrix StripDrawMatrix(const FStripSource& S, const FMatrix& InstMatrix)
{
	const FVector D = StripDrawScale(S.Scale);
	FMatrix M = InstMatrix;
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 Col = 0; Col < 4; ++Col) { M.M[R][Col] *= D[R]; }
	}
	return M;
}

FColor StripColour(const FStripSource& S, const FVector& Normal)
{
	double C[4] = { S.Ambient.X, S.Ambient.Y, S.Ambient.Z, S.Ambient.W };
	for (int32 L = 0; L < 3; ++L)
	{
		const double D = FMath::Max(0.0, FVector::DotProduct(Normal, S.LightDir[L]));
		for (int32 K = 0; K < 4; ++K) { C[K] += S.LightColour[L][K] * D; }
	}
	uint8 B[4];
	for (int32 K = 0; K < 4; ++K) { B[K] = uint8(FMath::Clamp(FMath::TruncToDouble(FMath::Min(C[K], 255.0)), 0.0, 255.0)); }
	return FColor(B[0], B[1], B[2], B[3]);
}

FVector FMat3::MulVec(const FVector& V) const
{
	return FVector(M[0][0] * V.X + M[0][1] * V.Y + M[0][2] * V.Z,
	               M[1][0] * V.X + M[1][1] * V.Y + M[1][2] * V.Z,
	               M[2][0] * V.X + M[2][1] * V.Y + M[2][2] * V.Z);
}

FMat3 FMat3::operator*(const FMat3& B) const
{
	FMat3 R;
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = 0; J < 3; ++J) { R.M[I][J] = M[I][0] * B.M[0][J] + M[I][1] * B.M[1][J] + M[I][2] * B.M[2][J]; }
	}
	return R;
}

FMat3 FMat3::Transposed() const
{
	FMat3 R;
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = 0; J < 3; ++J) { R.M[I][J] = M[J][I]; }
	}
	return R;
}

FMat3 FMat3::Inverse() const
{
	const double A = M[0][0], B = M[0][1], C = M[0][2], D = M[1][0], E = M[1][1], F = M[1][2], G = M[2][0], H = M[2][1], I = M[2][2];
	const double Ca = E * I - F * H, Cb = -(D * I - F * G), Cc = D * H - E * G;
	const double Det = A * Ca + B * Cb + C * Cc;
	FMat3 R;
	if (Det == 0.0)
	{
		return R;
	}
	const double Inv[3][3] = { { Ca, -(B * I - C * H), B * F - C * E }, { Cb, A * I - C * G, -(A * F - C * D) }, { Cc, -(A * H - B * G), A * E - B * D } };
	for (int32 X = 0; X < 3; ++X)
	{
		for (int32 Y = 0; Y < 3; ++Y) { R.M[X][Y] = Inv[X][Y] / Det; }
	}
	return R;
}

FMat3 FMat3::Scaled(double S) const
{
	FMat3 R = *this;
	for (int32 I = 0; I < 3; ++I)
	{
		for (int32 J = 0; J < 3; ++J) { R.M[I][J] *= S; }
	}
	return R;
}

FMat3 QuatToMatrix(const FQuat4d& Q)
{
	const double W = Q.W, X = Q.X, Y = Q.Y, Z = Q.Z;
	FMat3 R;
	R.M[0][0] = 1 - 2 * (Y * Y + Z * Z); R.M[0][1] = 2 * (X * Y + W * Z); R.M[0][2] = 2 * (X * Z - W * Y);
	R.M[1][0] = 2 * (X * Y - W * Z); R.M[1][1] = 1 - 2 * (X * X + Z * Z); R.M[1][2] = 2 * (Y * Z + W * X);
	R.M[2][0] = 2 * (X * Z + W * Y); R.M[2][1] = 2 * (Y * Z - W * X); R.M[2][2] = 1 - 2 * (X * X + Y * Y);
	return R;
}

void FRigidBody::Init(double Mass, const FMat3& InvInertiaBody, const FVector& InCom, const FVector& Pos, const FQuat4d& InQ)
{
	InvMass = 1.0 / Mass;
	InvIBody = InvInertiaBody.Scaled(InertiaScale);
	ComOffset = InCom;
	X = Pos;
	Q = InQ;
	P = L = V = W = FVector::ZeroVector;
	UpdateInertia();
}

void FRigidBody::UpdateInertia()
{
	// 0x13E140
	const FMat3 R = QuatToMatrix(Q);
	InvI = (R.Transposed() * InvIBody) * R;
}

void FRigidBody::Derive()
{
	V = P * InvMass;
	W = InvI.MulVec(L);
	const double N = W.Size();
	if (N > OmegaMax)
	{
		const double S = OmegaMax / N;
		W *= S;
		L *= S;
	}
}

void FRigidBody::SetOmega(const FVector& InW)
{
	W = InW;
	L = InvI.Inverse().MulVec(InW);
}

void FRigidBody::Integrate(const FVector& F, const FVector& T, double Dt)
{
	X += V * Dt;
	const double W0 = Q.W, X0 = Q.X, Y0 = Q.Y, Z0 = Q.Z;
	const double Wx = W.X, Wy = W.Y, Wz = W.Z;
	const double Nw = W0 + (-Wx * X0 - Wy * Y0 - Wz * Z0) * 0.5 * Dt;
	const double Nx = X0 + (Wx * W0 + Wy * Z0 - Wz * Y0) * 0.5 * Dt;
	const double Ny = Y0 + (Wy * W0 + Wz * X0 - Wx * Z0) * 0.5 * Dt;
	const double Nz = Z0 + (Wz * W0 + Wx * Y0 - Wy * X0) * 0.5 * Dt;
	const double N = FMath::Sqrt(Nw * Nw + Nx * Nx + Ny * Ny + Nz * Nz);
	if (N == 0.0) { Q = FQuat4d(Nx, Ny, Nz, 1.0); }
	else { Q = FQuat4d(Nx / N, Ny / N, Nz / N, Nw / N); }
	P += F * Dt;
	L += T * Dt;
}

FVector FRigidBody::PointVelocity(const FVector& C) const
{
	return V + FVector::CrossProduct(W, C - X);
}

void FRigidBody::ApplyImpulse(const FVector& R, const FVector& J)
{
	P += J;
	L += FVector::CrossProduct(R, J);
	Derive();
}

double FRigidBody::K(const FVector& R, const FVector& D) const
{
	return InvMass + FVector::DotProduct(D, FVector::CrossProduct(InvI.MulVec(FVector::CrossProduct(R, D)), R));
}

void FRigidBody::ContactImpulse(double E, double Mu, const FVector& C, const FVector& N)
{
	const FVector R = C - X;
	FVector U = PointVelocity(C);
	double Un = FVector::DotProduct(U, N);
	if (Mu != 0.0)
	{
		const FVector Tv = U - N * Un;
		const double Tl = Tv.Size();
		if (Tl > 0.0)
		{
			const FVector Th = Tv / Tl;
			ApplyImpulse(R, Th * (-Mu * Tl / K(R, Th)));
		}
		U = PointVelocity(C);
		Un = FVector::DotProduct(U, N);
		if (Un > 0.0)
		{
			return;
		}
	}
	ApplyImpulse(R, N * (-(E + 1.0) * Un / K(R, N)));
}

bool FRollerRecord::Parse(const uint8* R, int32 Size, FRollerRecord& Out)
{
	if (!R || Size < 36)
	{
		return false;
	}
	Out.Mass = RecF(R, 1);
	Out.W2 = RecF(R, 2);
	Out.W3 = RecF(R, 3);
	Out.Velocity = FVector(RecF(R, 4), RecF(R, 5), RecF(R, 6));
	return Out.Mass > 0.0;
}

void FRoller::Init(const FRollerRecord& R, const FRigidBody& InBody, const FContactRecord* Contact)
{
	Body = InBody;
	Mass = R.Mass;
	T = ContactTime = Energy = 0.0;
	bAsleep = false;
	if (!R.Velocity.IsZero())
	{
		Body.P = R.Velocity * R.Mass;
		Body.V = R.Velocity;
		Body.SetOmega(SSXSim::RsqrtNormal((-R.Velocity)) * SpinKick);
	}
	else
	{
		const double Up = FMath::Min(5000.0 / R.Mass + 100.0, KickUpMax);
		const FVector V0 = Contact ? Contact->VHat * (Contact->A * 0.5) + FVector(0, 0, Up) : FVector(0, 0, Up);
		Body.V = V0;
		Body.P = V0 * R.Mass;
		Body.SetOmega(Contact ? Contact->N * SpinKick : FVector::ZeroVector);
	}
	Energy = FVector::DotProduct(Body.V, Body.P) + FVector::DotProduct(Body.W, Body.L);
}

void FRoller::Wake()
{
	if (bAsleep)
	{
		T = 0.0;
		ContactTime = 0.0;
		Energy = FVector::DotProduct(Body.V, Body.P) + FVector::DotProduct(Body.W, Body.L);
		bAsleep = false;
	}
}

bool FRoller::Tick(TFunctionRef<bool(const FRigidBody&, FBodyHit&)> TerrainQuery, TFunctionRef<bool(const FRigidBody&, FBodyHit&)> PropQuery)
{
	if (bAsleep)
	{
		return false;
	}
	FRigidBody& B = Body;
	T += Frame;
	Energy = Energy * 0.95 + (FVector::DotProduct(B.V, B.P) + FVector::DotProduct(B.W, B.L)) * 0.05 * B.InvMass;
	if (Energy < RestEnergy || T >= RestTime)
	{
		T = 0.0;
		B.P = B.L = B.V = B.W = FVector::ZeroVector;
		bAsleep = true;
		return true;
	}
	ContactTime *= 0.8333333;
	const double D = FMath::Clamp(ContactTime * 10.0, 0.0, 2.0);
	const double Kd = (2.0 * T) * D + D;
	const FVector F = FVector(0, 0, -Gravity * Mass) - B.P * Kd;
	const FVector Tq = B.L * -Kd;
	B.Integrate(F, Tq);
	B.UpdateInertia();
	B.Derive();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bTerrain = Pass == 0;
		FBodyHit H;
		if (!(bTerrain ? TerrainQuery(B, H) : PropQuery(B, H)))
		{
			continue;
		}
		const double Depth = FMath::Max(H.Depth, MinPush);
		B.X += H.N * Depth;
		B.L *= ContactDamp;
		B.W = B.InvI.MulVec(B.L);
		B.V *= ContactDamp;
		B.P = B.V * Mass;
		if (FVector::DotProduct(B.PointVelocity(H.C), H.N) < 0.0)
		{
			const double E = !(bTerrain && B.P.Size() < SlowSpeed) ? Restitution : 0.0;
			B.ContactImpulse(E, Friction, H.C, H.N);
			ContactTime += Frame;
			if (bTerrain && T > GroundRestTime)
			{
				T = RestTime;
			}
		}
	}
	return false;
}

bool RiderHitsBody(FRigidBody& Body, const FVector& RiderV, double RiderMass, const FVector& C, const FVector& N, FVector* OutJ)
{
	const FVector R = C - Body.X;
	const double Vn = FVector::DotProduct(N, RiderV - Body.PointVelocity(C));
	if (Vn >= 0.0)
	{
		return false;
	}
	const double J = 1.3 * Vn / (FVector::DotProduct(N, FVector::CrossProduct(Body.InvI.MulVec(FVector::CrossProduct(R, N)), R))
		+ Body.InvMass + 1.0 / RiderMass);
	Body.ApplyImpulse(R, N * J);
	if (OutJ) { *OutJ = N * J; }
	return true;
}

bool FMeshAnimRecord::Parse(const uint8* R, int32 Size, FMeshAnimRecord& Out)
{
	if (!R || Size < 52)
	{
		return false;
	}
	Out.Keep = RecI(R, 1);
	Out.Gravity = RecF(R, 2);
	Out.Seconds = RecF(R, 3);
	Out.Velocity = FVector(RecF(R, 4), RecF(R, 5), RecF(R, 6));
	Out.Spread = FVector(RecF(R, 7), RecF(R, 8), RecF(R, 9));
	Out.VelScale = RecF(R, 10);
	return true;
}

void FMeshAnim::Init(const FMeshAnimRecord& R, TConstArrayView<TPair<FVector, FQuat4d>> Start, const FMatrix& InstRot,
                     const FContactRecord* Contact, TFunctionRef<double()> Rand01, TFunctionRef<int32()> RandBit)
{
	FVector V0 = FVector::ZeroVector;
	if (!R.Velocity.IsZero())
	{
		V0 = InstRot.TransformVector(R.Velocity);
	}
	else if (Contact)
	{
		V0 = Contact->VHat * FMath::Abs(Contact->A);
	}
	V0 *= R.VelScale;
	Timer = int32(R.Seconds * 60.0);
	Keep = R.Keep;
	Gravity = R.Gravity;
	bActive = true;
	Pieces.Reset();
	for (int32 I = 0; I < Start.Num() && I < MaxPieces; ++I)
	{
		FPiece P;
		P.Pos = Start[I].Key;
		P.Q = Start[I].Value;
		P.Spin = (Rand01() * 8.0 + 2.0) * Frame;
		const double Ax = Rand01() * 2 - 1, Ay = Rand01() * 2 - 1, Az = Rand01() * 2 - 1;
		P.Axis = SSXSim::RsqrtNormal(FVector(Ax, Ay, Az));
		if (P.Axis.IsZero()) { P.Axis = FVector(Ax, Ay, Az); }
		Pieces.Add(P);
	}
	for (FPiece& P : Pieces)
	{
		double Rx = R.Spread.X * Rand01() * Frame;
		if (RandBit()) { Rx = -Rx; }
		double Ry = R.Spread.Y * Rand01() * Frame;
		if (RandBit()) { Ry = -Ry; }
		const double Rz = R.Spread.Z * Rand01() * Frame;
		P.Vel = V0 * Frame + FVector(Rx, Ry, Rz);
	}
}

FMeshAnim::EResult FMeshAnim::Tick()
{
	if (Timer < 1 || Timer - 1 < 1)
	{
		if (Timer >= 1) { --Timer; }
		bActive = false;
		return Keep == 1 ? EResult::Stop : EResult::Done;
	}
	--Timer;
	if (!bActive)
	{
		return EResult::None;
	}
	for (FPiece& P : Pieces)
	{
		const double H = P.Spin * 0.5;
		const double S = FMath::Sin(H), C = FMath::Cos(H);
		FQuat4d Dq; Dq.W = C; Dq.X = P.Axis.X * S; Dq.Y = P.Axis.Y * S; Dq.Z = P.Axis.Z * S;
		FQuat4d Q = QMul(P.Q, Dq);
		const double N = FMath::Sqrt(Q.W * Q.W + Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z);
		P.Q = N == 0.0 ? FQuat4d::Identity : FQuat4d(Q.X / N, Q.Y / N, Q.Z / N, Q.W / N);
	}
	for (FPiece& P : Pieces)
	{
		P.Pos += P.Vel;
		P.Vel.Z -= Gravity * DebrisGravity;
	}
	return EResult::None;
}

int32 FMovingObjects::Add(int32 Owner, EKind Kind, const FBox& Box)
{
	int32 Id = INDEX_NONE;
	for (int32 I = 0; I < Proxies.Num(); ++I)
	{
		if (!Proxies[I].bUsed) { Id = I; break; }
	}
	if (Id == INDEX_NONE)
	{
		if (Proxies.Num() >= Capacity)
		{
			return INDEX_NONE;
		}
		Id = Proxies.AddDefaulted();
	}
	Proxies[Id] = FProxy{ Owner, Kind, Box, true };
	return Id;
}

void FMovingObjects::Update(int32 Id, const FBox& Box)
{
	if (Proxies.IsValidIndex(Id)) { Proxies[Id].Box = Box; }
}

void FMovingObjects::Remove(int32 Id)
{
	if (Proxies.IsValidIndex(Id)) { Proxies[Id] = FProxy(); }
}

void FMovingObjects::Pairs(int32 Id, TArray<int32>& Out) const
{
	Out.Reset();
	if (!Proxies.IsValidIndex(Id) || !Proxies[Id].bUsed)
	{
		return;
	}
	const FBox& A = Proxies[Id].Box;
	for (int32 I = 0; I < Proxies.Num(); ++I)
	{
		const FProxy& P = Proxies[I];
		if (I == Id || !P.bUsed)
		{
			continue;
		}
		const FBox& B = P.Box;
		if (A.Min.X <= B.Max.X && B.Min.X <= A.Max.X && A.Min.Y <= B.Max.Y && B.Min.Y <= A.Max.Y && A.Min.Z <= B.Max.Z && B.Min.Z <= A.Max.Z)
		{
			Out.Add(I);
		}
	}
}

int32 FMovingObjects::Num() const
{
	int32 N = 0;
	for (const FProxy& P : Proxies) { N += P.bUsed; }
	return N;
}
}
