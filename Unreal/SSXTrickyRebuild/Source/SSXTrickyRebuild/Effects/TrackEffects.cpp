#include "Effects/TrackEffects.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/TriggerEffects.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Anim/RiderPose.h"
#include "Sim/RiderPlaceholderStates.h"
#include "Sim/RiderSim.h"
#include "SSXTrickyRebuild.h"

namespace SSXEffects
{
namespace
{
	constexpr uint32 Magic = 0x45585353;
	constexpr int32 HeaderSize = 64;
	constexpr float FrameDt = 1.0f / 60.0f;

	bool IsAnim(int32 T) { return T >= TypeAnimObject && T <= TypeAnimTexFlip; }
	bool IsTouchForward(int32 T) { return T == TypeUVScroll || T == TypeTexFlip || T == TypeUVScrollTexFlip; }
	int32 SecondsToFrames(float S) { return int32(S * 60.0f); }   // int(F32(s) * F32(60)): float multiply, truncate

	int32 NodeSize(int32 Sub)
	{
		switch (Sub)
		{
		case 0: return 36; case 2: return 16; case 5: return 16; case 6: return 20; case 7: return 40; case 8: return 20;
		case 9: return 24; case 10: return 36; case 11: return 32; case 12: return 20; case 13: return 28; case 14: return 20;
		case 15: return 32; case 16: return 52; case 17: return 24; case 18: return 40; case 20: return 52; case 21: return 24;
		case 23: return 12; case 24: return 88; case 0x100: return 44; case 0x101: return 44; case 0x102: return 60;
		default: return -1;
		}
	}
}

bool IsShowOffMode(int32 M) { return M == 3 || M == 5; }

const TCHAR* ModeScript(int32 M)
{
	switch (M)
	{
	case 0: case 2: case 4: case 7: case 9: return TEXT("RaceMode");
	case 3: case 5: return TEXT("ShowoffMode");
	default: return TEXT("FreerideMode");
	}
}

bool HasCountdown(int32 M, int32 Track)
{
	if (Track == 7)
	{
		return false;
	}
	return M == 0 || M == 2 || M == 3 || M == 4 || M == 5 || M == 7 || M == 9;
}

TArray<FString> FlowScripts(EFlowPhase Phase, int32 M, int32 Track)
{
	const bool bCd = HasCountdown(M, Track);
	TArray<FString> Out;
	switch (Phase)
	{
	case EFlowPhase::PreRace:
		if (!bCd) { Out.Add(TEXT("NoCountdown")); }
		Out.Add(ModeScript(M));
		break;
	case EFlowPhase::StartRace:
		if (!bCd) { Out.Add(TEXT("NoCountdown")); Out.Add(ModeScript(M)); }
		break;
	case EFlowPhase::CountdownEnter:
		if (bCd) { Out.Add(TEXT("StartCountdown")); Out.Add(ModeScript(M)); }
		break;
	case EFlowPhase::CountdownEnd:
		if (bCd) { Out.Add(TEXT("EndCountdown")); Out.Add(ModeScript(M)); }
		break;
	}
	return Out;
}

int32 TrackIdForStem(const FString& Stem)
{
	static const TCHAR* Names[] = { TEXT("gari"), TEXT("snow"), TEXT("elysium"), TEXT("mesa"), TEXT("merquer"), TEXT("aloha"),
		TEXT("pipe"), TEXT("untrack"), TEXT("megaple"), TEXT("trick"), TEXT("alaska"), TEXT("ssxfe") };
	for (int32 K = 0; K < UE_ARRAY_COUNT(Names); ++K)
	{
		if (Stem.Equals(Names[K], ESearchCase::IgnoreCase))
		{
			return K;
		}
	}
	return -1;
}

bool SnowfallNeedsDraw(int32 M, int32 Humans, int32 Racers)
{
	return M != 2 && M != 4 && Humans <= 1 && Racers < 3;
}

bool SnowfallEnabled(int32 M, int32 Humans, int32 Racers, double Draw, double SnowChance)
{
	return SnowfallNeedsDraw(M, Humans, Racers) && Draw < SnowChance;
}

FString FEffectData::DefaultPath(const FString& Track)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Track.ToLower() + TEXT(".effects.bin"));
}

bool FEffectData::Load(const FString& Path, FString* OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s (run tools/export_effects.py)"), *Path); }
		return false;
	}
	return LoadFromMemory(Bytes, OutError);
}

bool FEffectData::LoadFromMemory(TConstArrayView<uint8> Bytes, FString* OutError)
{
	auto Fail = [OutError](const TCHAR* Msg) { if (OutError) { *OutError = Msg; } return false; };
	bLoaded = false;
	const int64 N = Bytes.Num();
	const uint8* D = Bytes.GetData();
	if (N < HeaderSize)
	{
		return Fail(TEXT("effects file too short"));
	}
	auto U32 = [D](int64 O) { uint32 X; FMemory::Memcpy(&X, D + O, 4); return X; };
	auto I32 = [D](int64 O) { int32 X; FMemory::Memcpy(&X, D + O, 4); return X; };
	auto F32 = [D](int64 O) { float X; FMemory::Memcpy(&X, D + O, 4); return X; };
	if (U32(0) != Magic || U32(4) != 1)
	{
		return Fail(TEXT("effects file: bad magic or version"));
	}
	TrackId = I32(8);
	const int32 NInst = I32(12), NEvent = I32(16), NEffect = I32(20), NFunc = I32(24), NRec = I32(28), NSpl = I32(32);
	const int32 BlobSize = I32(36);
	Nx = I32(40);
	Ny = I32(44);
	const int32 NRefs = I32(48);
	MinX = F32(52);
	MinY = F32(56);
	CellSize = F32(60);
	const int64 Need = HeaderSize + 72LL * NInst + 28LL * NEvent + 8LL * NEffect + 24LL * NFunc + 12LL * NRec + 4LL * NSpl
		+ 16LL * Nx * Ny + 4LL * NRefs + BlobSize;
	if (NInst < 0 || NEvent < 0 || NEffect < 0 || NFunc < 0 || NRec < 0 || NSpl < 0 || BlobSize < 0 || Nx < 0 || Ny < 0
		|| NRefs < 0 || N < Need)
	{
		return Fail(TEXT("effects file: truncated"));
	}
	int64 O = HeaderSize;
	InstEvent.SetNum(NInst);
	InstFlags.SetNum(NInst);
	InstMatrix.SetNum(NInst);
	for (int32 K = 0; K < NInst; ++K, O += 72)
	{
		InstEvent[K] = I32(O);
		InstFlags[K] = U32(O + 4);
		FMatrix M;
		for (int32 R = 0; R < 4; ++R)
		{
			for (int32 C = 0; C < 4; ++C)
			{
				M.M[R][C] = F32(O + 8 + 4 * (4 * R + C));
			}
		}
		InstMatrix[K] = M;
	}
	Events.SetNum(NEvent * NumSlots);
	for (int32 K = 0; K < NEvent * NumSlots; ++K, O += 4)
	{
		Events[K] = I32(O);
	}
	Effects.SetNum(NEffect);
	for (int32 K = 0; K < NEffect; ++K, O += 8)
	{
		Effects[K] = FIntPoint(I32(O), I32(O + 4));
	}
	Functions.SetNum(NFunc);
	for (int32 K = 0; K < NFunc; ++K, O += 24)
	{
		Functions[K].First = I32(O);
		Functions[K].Count = I32(O + 4);
		ANSICHAR Name[17] = {};
		FMemory::Memcpy(Name, D + O + 8, 16);
		Functions[K].Name = ANSI_TO_TCHAR(Name);
	}
	Records.SetNum(NRec);
	for (int32 K = 0; K < NRec; ++K, O += 12)
	{
		Records[K] = FRecord{ I32(O), I32(O + 4), I32(O + 8) };
	}
	Splines.SetNum(NSpl);
	for (int32 K = 0; K < NSpl; ++K, O += 4)
	{
		Splines[K] = U32(O);
	}
	Cells.SetNum(Nx * Ny);
	for (int32 K = 0; K < Nx * Ny; ++K, O += 16)
	{
		Cells[K] = FCell{ I32(O), I32(O + 4), I32(O + 8), I32(O + 12) };
	}
	CellRefs.SetNum(NRefs);
	for (int32 K = 0; K < NRefs; ++K, O += 4)
	{
		CellRefs[K] = I32(O);
	}
	Blob = TArray<uint8>(D + O, BlobSize);
	for (const FRecord& R : Records)
	{
		if (R.Offset < 0 || R.Size < 8 || int64(R.Offset) + R.Size > BlobSize)
		{
			return Fail(TEXT("effects file: record outside the blob"));
		}
	}
	for (const FIntPoint& E : Effects)
	{
		if (E.X < 0 || E.Y < 0 || E.X + E.Y > NRec) { return Fail(TEXT("effects file: bad effect list")); }
	}
	for (const FFunction& F : Functions)
	{
		if (F.First < 0 || F.Count < 0 || F.First + F.Count > NRec) { return Fail(TEXT("effects file: bad function list")); }
	}
	bLoaded = true;
	return true;
}

int32 FEffectData::EffectFor(int32 Inst, int32 Slot) const
{
	if (!InstEvent.IsValidIndex(Inst) || Slot < 0 || Slot >= NumSlots)
	{
		return -1;
	}
	const int32 E = InstEvent[Inst];
	return (E < 0 || (E + 1) * NumSlots > Events.Num()) ? -1 : Events[E * NumSlots + Slot];
}

int32 FEffectData::FunctionIndex(const FString& Name) const
{
	for (int32 K = 0; K < Functions.Num(); ++K)
	{
		if (Functions[K].Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return K;
		}
	}
	return -1;
}

int32 FEffectData::I(const FRecord& R, int32 K) const
{
	const int32 O = R.Offset + 8 + 4 * K;
	if (O + 4 > R.Offset + R.Size) { return 0; }
	int32 X; FMemory::Memcpy(&X, Blob.GetData() + O, 4); return X;
}

float FEffectData::F(const FRecord& R, int32 K) const
{
	const int32 O = R.Offset + 8 + 4 * K;
	if (O + 4 > R.Offset + R.Size) { return 0.f; }
	float X; FMemory::Memcpy(&X, Blob.GetData() + O, 4); return X;
}

int16 FEffectData::S16(const FRecord& R, int32 ByteOff) const
{
	const int32 O = R.Offset + ByteOff;
	if (O + 2 > R.Offset + R.Size) { return 0; }
	int16 X; FMemory::Memcpy(&X, Blob.GetData() + O, 2); return X;
}

int32 FEffectData::ExpectedSize(int32 Type, int32 Sub)
{
	switch (Type)
	{
	case 0: return NodeSize(Sub);
	case 2: return (Sub == 0 || Sub == 2) ? 216 : (Sub == 1 ? 56 : -1);
	case 3: case 7: case 9: case 23: case 25: return 16;
	case 4: case 6: case 8: case 10: case 11: case 13: case 14: case 15: case 16: case 17: case 18: case 21: case 24: case 26:
		return 12;
	case 5: return 20;
	default: return -1;
	}
}

TArray<FString> FEffectData::Validate() const
{
	TArray<FString> Errs;
	const int32 NInst = NumInstances(), NEffect = Effects.Num(), NFunc = Functions.Num(), NSpl = Splines.Num();
	TArray<FIntPoint> Spans;
	auto List = [&](int32 First, int32 Count)
	{
		if (Count > 0)
		{
			Spans.AddUnique(FIntPoint(Records[First].Offset, Records[First + Count - 1].Offset + Records[First + Count - 1].Size));
		}
		for (int32 K = First; K < First + Count; ++K)
		{
			const FRecord& R = Records[K];
			if (K > First && R.Offset != Records[K - 1].Offset + Records[K - 1].Size)
			{
				Errs.AddUnique(TEXT("records of a list not consecutive"));
			}
			const int32 Exp = ExpectedSize(R.Type, (R.Type == 0 || R.Type == 2) ? I(R, 0) : 0);
			if (Exp < 0 || Exp != R.Size)
			{
				Errs.AddUnique(FString::Printf(TEXT("size type %d"), R.Type));
			}
			if (R.Type == 7 && !(I(R, 0) >= 0 && I(R, 0) < NInst && I(R, 1) >= 0 && I(R, 1) < NEffect)) { Errs.AddUnique(TEXT("op7 ref")); }
			if ((R.Type == 21 || R.Type == 26) && !(I(R, 0) >= 0 && I(R, 0) < NFunc)) { Errs.AddUnique(TEXT("op21 ref")); }
			if (R.Type == 24 && !(I(R, 0) >= 0 && I(R, 0) < NInst)) { Errs.AddUnique(TEXT("op24 ref")); }
			if (R.Type == 25 && !(I(R, 0) >= 0 && I(R, 0) < NSpl && (I(R, 1) == 0 || I(R, 1) == 1))) { Errs.AddUnique(TEXT("op25 ref")); }
		}
	};
	for (const FIntPoint& E : Effects) { List(E.X, E.Y); }
	for (const FFunction& F : Functions) { List(F.First, F.Count); }
	Spans.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	int32 Pos = 0;
	for (const FIntPoint& S : Spans)
	{
		if (S.X != Pos) { Errs.AddUnique(TEXT("bodies do not tile the data block")); break; }
		Pos = S.Y;
	}
	if (Pos != Blob.Num()) { Errs.AddUnique(TEXT("data block tail")); }
	for (int32 E : Events)
	{
		if (E < -1 || E >= NEffect) { Errs.AddUnique(TEXT("event slot ref")); }
	}
	for (int32 E : InstEvent)
	{
		if (E < -1 || E * NumSlots >= Events.Num()) { Errs.AddUnique(TEXT("inst event ref")); }
	}
	return Errs;
}

int32 FEffectRider::GetMotion() const { return Sim ? int32(Sim->State.Motion) : Motion; }
double FEffectRider::GetSpeed() const { return Sim ? Sim->State.Velocity.Size() : Speed; }

void FEffectWorld::Init(const FEffectData* InData, int32 InGameMode, int32 InTrack)
{
	Data = InData;
	GameMode = InGameMode;
	Track = InTrack;
	bShowOff = IsShowOffMode(GameMode);
	Controllers.Reset();
	CtlOrder.Reset();
	Scripts.Reset();
	Emitters.Reset();
	Log.Reset();
	const int32 N = Data ? Data->NumInstances() : 0;
	Flags.SetNum(N);
	Initial.SetNum(N);
	for (int32 K = 0; K < N; ++K)
	{
		Initial[K] = Data->InitialFlags(K);
		Flags[K] = Initial[K];
	}
	RailEnabled.SetNum(Data ? Data->Splines.Num() : 0);
	for (int32 K = 0; K < RailEnabled.Num(); ++K)
	{
		RailEnabled[K] = uint8((Data->Splines[K] >> 16) & 1);
	}
}

void FEffectWorld::LoadFrontEndFlags()
{
	if (!Data)
	{
		return;
	}
	// 0x25EDF0 / 0x263B30 / 0x147E60
	for (const FEffectData::FCell& C : Data->Cells)
	{
		if (C.NumNormal < 0)
		{
			continue;
		}
		for (int32 K = 0; K < C.NumAlt; ++K)
		{
			const int32 Inst = Data->CellRefs.IsValidIndex(C.FirstAlt + K) ? Data->CellRefs[C.FirstAlt + K] : INDEX_NONE;
			if (!Initial.IsValidIndex(Inst))
			{
				continue;
			}
			Initial[Inst] = uint16(Initial[Inst] | FlCellActive | FlDisplay);
			SetFlags(Inst, uint16(Initial[Inst] | FlDisplay));
		}
	}
}

void FEffectWorld::RestartReset()
{
	if (!Data)
	{
		return;
	}
	// 0x115FE8
	const TArray<TPair<int32, TSharedPtr<FController>>> Ctls = CtlOrder;
	for (const TPair<int32, TSharedPtr<FController>>& P : Ctls)
	{
		RemoveController(P.Key);
	}
	for (const FEmitter& E : Emitters)
	{
		if (E.Kind == 2 && E.Rider) { E.Rider->CollideEmitters = FMath::Max(E.Rider->CollideEmitters - 1, 0); }
		FOutcome O; O.Kind = EOutcome::EmitterDied; O.Instance = E.Inst; O.A = E.Kind; O.Record = E.Record;
		Out(O);
	}
	Emitters.Reset();
	// 0x149A88 / 0x31D938
	for (const TSharedPtr<FScript>& S : Scripts) { S->bAlive = false; }
	Scripts.Reset();
	for (int32 K = 0; K < Flags.Num(); ++K)
	{
		SetFlags(K, uint16(Initial[K] | FlDisplay));
	}
	for (int32 K = 0; K < RailEnabled.Num(); ++K)
	{
		RailEnabled[K] = uint8((Data->Splines[K] >> 16) & 1);
	}
}

void FEffectWorld::Out(const FOutcome& O)
{
	if (bKeepLog) { Log.Add(O); }
	if (OnOutcome) { OnOutcome(O); }
}

void FEffectWorld::SaveKeyframe(FEffectKeyframe& Out) const
{
	Out.Flags = Flags;
	Out.RailEnabled = RailEnabled;
	Out.Controllers.Reset();
	for (const TPair<int32, TSharedPtr<FController>>& P : CtlOrder)
	{
		if (P.Value) { Out.Controllers.Add({ P.Key, *P.Value }); }
	}
	Out.Scripts.Reset();
	for (const TSharedPtr<FScript>& S : Scripts)
	{
		if (S) { Out.Scripts.Add(*S); }
	}
	Out.Emitters = Emitters;
	Out.TubeEndSide = TArray<int32>(TubeEndSide, BoostMaxRacers);
}

void FEffectWorld::LoadKeyframe(const FEffectKeyframe& In)
{
	if (In.Flags.Num() == Flags.Num()) { Flags = In.Flags; }
	if (In.RailEnabled.Num() == RailEnabled.Num()) { RailEnabled = In.RailEnabled; }
	Controllers.Reset();
	CtlOrder.Reset();
	for (const TPair<int32, FController>& P : In.Controllers)
	{
		const TSharedPtr<FController> C = MakeShared<FController>(P.Value);
		Controllers.Add(P.Key, C);
		CtlOrder.Add({ P.Key, C });
	}
	Scripts.Reset();
	for (const FScript& S : In.Scripts) { Scripts.Add(MakeShared<FScript>(S)); }
	Emitters = In.Emitters;
	for (int32 I = 0; I < BoostMaxRacers && I < In.TubeEndSide.Num(); ++I) { TubeEndSide[I] = In.TubeEndSide[I]; }
}

void FEffectWorld::SetFlags(int32 Inst, uint16 New)
{
	const uint16 Old = Flags[Inst];
	Flags[Inst] = New;
	if (Old != New && OnFlagsChanged) { OnFlagsChanged(Inst, Old, New); }
}

FController* FEffectWorld::Ctl(int32 Inst) const
{
	const TSharedPtr<FController>* P = Controllers.Find(Inst);
	return P ? P->Get() : nullptr;
}

const FController* FEffectWorld::FindController(int32 Inst) const { return Ctl(Inst); }

void FEffectWorld::GetControllers(TArray<TPair<int32, const FController*>>& Out) const
{
	Out.Reset();
	for (const TPair<int32, TSharedPtr<FController>>& P : CtlOrder) { Out.Emplace(P.Key, P.Value.Get()); }
}

bool FEffectWorld::IsScriptHidden(int32 Inst) const
{
	const FController* C = Ctl(Inst);
	return C && (C->Type == TypeDead || C->Type == TypeRestore);
}

TSharedPtr<FController> FEffectWorld::AddController(int32 Inst, const FController& C)
{
	RemoveController(Inst);
	TSharedPtr<FController> P = MakeShared<FController>(C);
	Controllers.Add(Inst, P);
	CtlOrder.Emplace(Inst, P);
	return P;
}

void FEffectWorld::RemoveController(int32 Inst)
{
	TSharedPtr<FController> P;
	if (!Controllers.RemoveAndCopyValue(Inst, P))
	{
		return;
	}
	CtlOrder.RemoveAll([Inst](const TPair<int32, TSharedPtr<FController>>& E) { return E.Key == Inst; });
	FOutcome O; O.Kind = EOutcome::NodeDestroyed; O.Instance = Inst; O.A = P->Type;
	Out(O);
}

void FEffectWorld::StartScript(const TSharedPtr<FScript>& S)
{
	UpdateScript(*S);
	if (S->bAlive) { Scripts.Add(S); }
}

void FEffectWorld::RunEffect(int32 Effect, int32 Inst, FEffectRider* Rider)
{
	if (!Data || Effect < 0 || Effect >= Data->Effects.Num())
	{
		return;
	}
	TSharedPtr<FScript> S = MakeShared<FScript>();
	S->First = Data->Effects[Effect].X;
	S->Count = Data->Effects[Effect].Y;
	S->Inst = Inst;
	S->Rider = Rider;
	StartScript(S);
}

bool FEffectWorld::CallFunction(const FString& Name)
{
	const int32 K = Data ? Data->FunctionIndex(Name) : -1;
	if (K < 0)
	{
		return false;
	}
	for (const TSharedPtr<FScript>& S : Scripts)
	{
		if (S->Function == K) { S->bAlive = false; }   // 0x13BAD8
	}
	Scripts.RemoveAll([K](const TSharedPtr<FScript>& S) { return S->Function == K; });
	TSharedPtr<FScript> S = MakeShared<FScript>();
	S->First = Data->Functions[K].First;
	S->Count = Data->Functions[K].Count;
	S->Function = K;
	StartScript(S);
	return true;
}

void FEffectWorld::CallFunctionIndex(int32 K, FEffectRider* Rider)
{
	if (!Data || K < 0 || K >= Data->Functions.Num())
	{
		return;
	}
	TSharedPtr<FScript> S = MakeShared<FScript>();
	S->First = Data->Functions[K].First;
	S->Count = Data->Functions[K].Count;
	S->Function = K;
	S->Rider = Rider;
	StartScript(S);
}

void FEffectWorld::RunFlow(EFlowPhase Phase)
{
	for (const FString& Name : FlowScripts(Phase, GameMode, Track))
	{
		CallFunction(Name);
	}
}

void FEffectWorld::Frame()
{
	const TArray<TSharedPtr<FScript>> Snapshot = Scripts;
	for (const TSharedPtr<FScript>& S : Snapshot)
	{
		if (S->bAlive) { UpdateScript(*S); }
	}
	Scripts.RemoveAll([](const TSharedPtr<FScript>& S) { return !S->bAlive; });
	const TArray<TPair<int32, TSharedPtr<FController>>> Ctls = CtlOrder;
	for (const TPair<int32, TSharedPtr<FController>>& P : Ctls)
	{
		ControllerFrame(P.Key, *P.Value);
	}
	for (int32 K = 0; K < Emitters.Num(); ++K)
	{
		FEmitter& E = Emitters[K];
		bool bDies;
		if (E.Kind == 1)
		{
			// 0x1FA6A0
			if (E.Path.bDone)
			{
				continue;
			}
			bDies = E.Inst == INDEX_NONE || !Ctl(E.Inst) || (GetFlags(E.Inst) & FlHeld) != 0;
			if (!bDies)
			{
				FVector3f Leader;
				const bool bLeader = E.Path.Config.EndMode == 3 && LeaderPosition && LeaderPosition(Leader);
				SplinePath::Step(E.Path, PathSegments, (GetFlags(E.Inst) & FlReacts) != 0, bLeader ? &Leader : nullptr);
			}
		}
		else if (E.Inst == INDEX_NONE || (GetFlags(E.Inst) & (FlCellActive | FlHeld)) != FlCellActive)
		{
			// 0x147E60 / 0x148948
			bDies = true;
		}
		else if (E.bContinuous)
		{
			bDies = false;
		}
		else
		{
			E.Life = FMath::Max(E.Life - 1.0 / 60.0, 0.0);
			bDies = E.Life <= 0.0;
		}
		if (bDies)
		{
			if (E.Kind == 2 && E.Rider) { E.Rider->CollideEmitters = FMath::Max(E.Rider->CollideEmitters - 1, 0); }
			FOutcome O; O.Kind = EOutcome::EmitterDied; O.Instance = E.Inst; O.A = E.Kind; O.Record = E.Record;
			Out(O);
			Emitters.RemoveAt(K--);
		}
	}
}

bool FEffectWorld::UpdateScript(FScript& S)
{
	if (S.Delay > 0.f)
	{
		S.Delay = S.Delay - FrameDt;
		if (S.Delay >= 0.f)
		{
			return true;
		}
		S.Delay = 0.f;
	}
	while (S.Index < S.Count)
	{
		const int32 RecIndex = S.First + S.Index;
		const int32 R = Exec(S, Data->Records[RecIndex]);
		if (R == -1)
		{
			S.bAlive = false;
			return false;
		}
		++S.Index;
		if (R == 0)
		{
			if (S.Index < S.Count)
			{
				return true;
			}
			break;
		}
	}
	S.bAlive = false;
	return false;
}

int32 FEffectWorld::Exec(FScript& S, const FRecord& R)
{
	const FEffectData& D = *Data;
	const int32 RecIndex = int32(&R - D.Records.GetData());
	FEffectRider* Rider = S.Rider;
	const int32 Inst = S.Inst;
	switch (R.Type)
	{
	case 0:
		Factory(S, R, RecIndex);
		break;
	case 1:
	{
		FOutcome O; O.Kind = EOutcome::Camera; O.Instance = Inst; O.Record = RecIndex;
		Out(O);
		break;
	}
	case 2:
		Emitter(S, R, RecIndex);
		break;
	case 3:
	case 9:
		if (R.Type == 3 && Inst == INDEX_NONE)
		{
			return 1;
		}
		if (FController* C = Inst != INDEX_NONE ? Ctl(Inst) : nullptr)
		{
			Message(Inst, *C, D.I(R, 0), D.F(R, 1));
		}
		break;
	case 4:
		S.Delay = D.F(R, 0);
		return 0;
	case 5:
		return Condition(S, R);
	case 6:
		if (bShowOff && Rider)
		{
			FOutcome O; O.Kind = EOutcome::ShowOffPoints; O.Rider = Rider; O.A = D.I(R, 0);
			Out(O);
			if (Rider->Sim)
			{
				if (SSXAir::FAirContext* Air = SSXAir::GetAirContext(*Rider->Sim)) { Air->Score.Messages.Push(6, D.I(R, 0), 2.5f); }
			}
			FOutcome M; M.Kind = EOutcome::MeterReward; M.Rider = Rider; M.V = ShowOffPickupReward;
			Out(M);
		}
		break;
	case 7:
		RunEffect(D.I(R, 1), D.I(R, 0), Rider);
		break;
	case 8:
	{
		FOutcome O; O.Kind = EOutcome::Sound; O.Instance = Inst; O.Rider = Rider; O.A = 2; O.B = D.I(R, 0);
		O.Extra = (Track == 4 && D.I(R, 0) == 0x7A) ? 1 : 0;
		Out(O);
		break;
	}
	case 10:
		if (FController* C = Inst != INDEX_NONE ? Ctl(Inst) : nullptr)
		{
			if (D.I(R, 0) == 0 || D.I(R, 0) == 1) { SetEnabled(Inst, *C, D.I(R, 0)); }
		}
		break;
	case 11:
		if (FController* C = Inst != INDEX_NONE ? Ctl(Inst) : nullptr)
		{
			if (D.I(R, 0) == 0) { DeactivateController(Inst, *C); }
		}
		break;
	case 13:
		if (Rider)
		{
			UE_LOG(LogSSX, Verbose, TEXT("[effects] op 13 reset: instance %d (flags 0x%04X), rider slot %d"), Inst, Inst != INDEX_NONE ? GetFlags(Inst) : 0, Rider->Slot);
			Rider->bResetRequested = true;
			if (Rider->Sim) { SSXTriggers::ApplyForcedReset(*Rider->Sim); }
			FOutcome O; O.Kind = EOutcome::Reset; O.Rider = Rider; O.V = SSXTriggers::ResetDrop;
			Out(O);
		}
		break;
	case 14:
		if (Rider)
		{
			// 0x11E660 / FUN_00155cd0 / 0x219D70
			const float MF = D.F(R, 0);
			const int32 M = int32(MF);
			const int32 Motion = Rider->GetMotion();
			if (bShowOff && (Motion == 1 || Motion == 3))
			{
				SSXAir::FAirContext* Air = Rider->Sim ? SSXAir::GetAirContext(*Rider->Sim) : nullptr;
				const bool bTaken = Air ? Air->Score.ScriptMultiplier(MF) : float(Rider->PointsMult) < MF;
				if (bTaken)
				{
					Rider->PointsMult = double(MF);
					FOutcome O; O.Kind = EOutcome::Multiplier; O.Rider = Rider; O.A = M;
					Out(O);
				}
			}
			if (Rider->bHuman)
			{
				FOutcome O; O.Kind = EOutcome::MultiplierSound; O.Rider = Rider; O.V = MF;
				Out(O);
			}
		}
		break;
	case 15:
		if (bShowOff && Rider)
		{
			FOutcome O; O.Kind = EOutcome::MeterReward; O.Rider = Rider; O.V = D.F(R, 0);
			Out(O);
		}
		break;
	case 16:
		if (Rider)
		{
			if (bShowOff)
			{
				FOutcome O; O.Kind = EOutcome::AddClock; O.Rider = Rider; O.V = D.F(R, 0);
				Out(O);
			}
			FOutcome T; T.Kind = EOutcome::TimeSound; T.Rider = Rider; T.V = D.F(R, 0);
			Out(T);
		}
		break;
	case 17:
		if (Rider)
		{
			const double Sec = D.F(R, 0);
			Rider->BurstTimer = FMath::Max(Rider->BurstTimer, Sec);
			if (Rider->Sim) { SSXTriggers::ApplyBoostBurst(*Rider->Sim, Sec); }
			FOutcome O; O.Kind = EOutcome::BoostBurst; O.Rider = Rider; O.V = Sec;
			Out(O);
			if (Rider->bHuman) { FOutcome B; B.Kind = EOutcome::BoostSound; B.Rider = Rider; Out(B); }
		}
		break;
	case 18:
		if (Rider)
		{
			const double Sec = D.F(R, 0);
			Rider->LaunchTimer = FMath::Max(Rider->LaunchTimer, Sec);
			Rider->LaunchFlag = 1;
			if (Rider->Sim) { SSXTriggers::ApplyLaunchBoost(*Rider->Sim, Sec); }
			FOutcome O; O.Kind = EOutcome::LaunchBoost; O.Rider = Rider; O.V = Sec;
			Out(O);
			if (Rider->bHuman) { FOutcome B; B.Kind = EOutcome::LaunchSound; B.Rider = Rider; Out(B); }
		}
		break;
	case 21:
	case 26:
		if (D.I(R, 0) >= 0)
		{
			CallFunctionIndex(D.I(R, 0), R.Type == 21 ? Rider : nullptr);
		}
		break;
	case 23:
		if (Rider)
		{
			FOutcome O; O.Kind = EOutcome::CameraShake; O.Rider = Rider; O.V = D.F(R, 0); O.A = D.I(R, 1);
			Out(O);
		}
		break;
	case 24:
		if (Rider && D.I(R, 0) >= 0 && D.I(R, 0) < Flags.Num())
		{
			Teleport(*Rider, D.I(R, 0));
		}
		break;
	case 25:
		SetRail(D.I(R, 0), D.I(R, 1));
		break;
	default:
		break;
	}
	return 1;
}

int32 FEffectWorld::Condition(const FScript& S, const FRecord& R)
{
	const int32 Kind = Data->I(R, 0), Sub = Data->I(R, 1);
	const float Value = Data->F(R, 2);
	switch (Kind)
	{
	case 0:
		if (!S.Rider) { return 1; }
		if (Sub == 0) { return S.Rider->GetSpeed() > Value ? -1 : 1; }
		if (Sub == 1) { return S.Rider->GetSpeed() < Value * KmhToCms ? -1 : 1; }
		return 1;
	case 1:
	{
		if (Sub != 0 && Sub != 1) { return 1; }
		const double Rn = Rng ? Rng() : 0.0;
		if (Sub == 0) { return Rn > Value ? -1 : 1; }
		return Rn < Value ? -1 : 1;
	}
	case 2:
		return (S.Rider && S.Rider->bHuman) ? 1 : -1;
	case 3:
		return (S.Inst != INDEX_NONE && Ctl(S.Inst)) ? -1 : 1;
	default:
		return 1;
	}
}

void FEffectWorld::Factory(const FScript& S, const FRecord& R, int32 RecIndex)
{
	const int32 Inst = S.Inst, Sub = Data->I(R, 0);
	if (Inst == INDEX_NONE || !Flags.IsValidIndex(Inst))
	{
		return;
	}
	if (FController* C = Ctl(Inst))
	{
		if (C->Type == TypeDead)
		{
			return;
		}
		const bool bLive = C->Type == TypeRoller && C->bRollerLive;
		if (C->Type == Sub)
		{
			if (bLive)
			{
				FOutcome O; O.Kind = EOutcome::NodeRestart; O.Instance = Inst; O.A = Sub;
				Out(O);
			}
			return;
		}
		Destroy(Inst);
	}
	if (Sub == TypeState)
	{
		SetState(Inst, Data->I(R, 1));
		return;
	}
	if (Sub == TypeTrickTrigger && !S.Rider)
	{
		return;
	}
	FController N;
	N.Type = Sub;
	N.Record = RecIndex;
	switch (Sub)
	{
	case TypeDebounce:
		N.Timer = SecondsToFrames(Data->F(R, 1));
		break;
	case TypeTimer:
		N.Timer = SecondsToFrames(Data->F(R, 1));
		N.Count = SecondsToFrames(Data->F(R, 2));
		break;
	case TypeCounter:
		N.Count = Data->S16(R, 0x0C);
		N.Timer = SecondsToFrames(Data->F(R, 2));
		break;
	case TypeCracked:
		N.Timer = SecondsToFrames(Data->F(R, 1));
		N.Health = Data->F(R, 2);
		N.TouchCooldown = CrackedCooldownFrames;
		break;
	case TypeBoost:
	case TypeTubeEndBoost:
	{
		// 0x140498 / 0x141688
		FBoostState& B = N.Boost;
		B.Mode = Data->S16(R, 0x0C);
		B.Frames = SecondsToFrames(Data->F(R, 2));
		B.Active = B.Mode == 1 ? B.Frames : 0;
		B.Gain = Data->F(R, 3);
		B.Target = Data->F(R, 4) * 100.0f;
		B.Dir = FVector3f(Data->F(R, 5), Data->F(R, 6), Data->F(R, 7));
		if (Sub == TypeTubeEndBoost)
		{
			for (int32 K = 0; K < 3; ++K)
			{
				B.Exit[K] = FVector3f(Data->F(R, 8 + 3 * K), Data->F(R, 9 + 3 * K), Data->F(R, 10 + 3 * K));
				B.ExitTarget[K] = Data->F(R, 17 + K) * 100.0f;
			}
			for (FVector3f& D : B.ExitDir) { D = FVector3f(TubeEndUnset); }
		}
		break;
	}
	case TypeLapBoost:
	{
		// 0x140948
		FBoostState& B = N.Boost;
		B.Gain = Data->F(R, 1);
		B.Target = Data->F(R, 2) * 100.0f;
		B.Dir = FVector3f(Data->F(R, 3), Data->F(R, 4), Data->F(R, 5));
		for (int32 K = 0; K < Racers.Num() && K < BoostMaxRacers; ++K)
		{
			B.LapCopy[K] = Racers[K] ? int16(LapsLeft(*Racers[K])) : 0;
		}
		break;
	}
	case TypeZBoost:
		// 0x141AF0
		N.Boost.Gain = Data->F(R, 1);
		N.Boost.Target = Data->F(R, 2) * 100.0f;
		N.Boost.Dir = FVector3f(Data->F(R, 3), Data->F(R, 4), Data->F(R, 5));
		N.Boost.ZTop = Data->F(R, 6);
		N.Boost.ZRange = Data->F(R, 7);
		break;
	default:
		if (IsAnim(Sub))
		{
			N.TouchCooldownReset = SecondsToFrames(Data->F(R, 6));
			N.TouchCooldown = S.Rider ? N.TouchCooldownReset : 0;
		}
		break;
	}
	if (Sub == TypeUVScroll || Sub == TypeTexFlip || Sub == TypeCracked)
	{
		N.TouchCooldown = 30;
	}
	AddController(Inst, N);
	if (Sub == TypeUVScroll || Sub == TypeTexFlip || Sub == TypeCrowdBox || Sub == TypeMovie || Sub == TypeCracked
		|| Sub == TypeUVScrollTexFlip)
	{
		SetFlags(Inst, uint16((Flags[Inst] & ~FlDisplay) | FlNodeDrawn));
	}
	FOutcome O; O.Kind = EOutcome::Node; O.Instance = Inst; O.A = Sub; O.Record = RecIndex;
	Out(O);
}

void FEffectWorld::SetState(int32 Inst, int32 K)
{
	switch (K)
	{
	case 0:
		RestoreFlags(Inst, true);
		break;
	case 1:
		if (FController* C = Ctl(Inst)) { SetEnabled(Inst, *C, 1); }
		break;
	case 2:
	case 3:
	{
		uint16 F = uint16((Flags[Inst] & 0xFF0D) | FlNodeDrawn);
		if (K == 3) { F |= FlHeld; }
		SetFlags(Inst, F);
		FController C; C.Type = TypeDead;
		AddController(Inst, C);
		FOutcome O; O.Kind = EOutcome::Hide; O.Instance = Inst; O.A = K;
		Out(O);
		break;
	}
	case 4:
	{
		if (const FController* C = Ctl(Inst); C && C->Type == TypeRestore)
		{
			return;
		}
		SetFlags(Inst, uint16((Flags[Inst] & 0xFF0D) | FlNodeDrawn));
		FController C; C.Type = TypeRestore;
		AddController(Inst, C);
		FOutcome O; O.Kind = EOutcome::Hide; O.Instance = Inst; O.A = 4;
		Out(O);
		break;
	}
	default:
		break;
	}
}

void FEffectWorld::RestoreFlags(int32 Inst, bool bKeepCellBit)
{
	const uint16 Keep = bKeepCellBit ? uint16(Flags[Inst] & FlCellActive) : 0;
	SetFlags(Inst, uint16(Initial[Inst] | Keep | FlDisplay));
}

void FEffectWorld::Destroy(int32 Inst)
{
	const FController* C = Ctl(Inst);
	const bool bDead = C && C->Type == TypeDead;
	RemoveController(Inst);
	if (bDead)
	{
		SetFlags(Inst, uint16((Flags[Inst] & 0xFF0D) | FlNodeDrawn));
	}
}

void FEffectWorld::Message(int32 Inst, FController& C, int32 Msg, float Value)
{
	FOutcome O; O.Kind = EOutcome::Message; O.Instance = Inst; O.A = C.Type; O.B = Msg; O.V = Value;
	Out(O);
	switch (C.Type)
	{
	case TypeDead: case TypeRestore: case TypeCrowdBox: case TypeMovie: case TypeFlag: case TypeFence: case TypeMeshAnim:
		return;
	case TypeCounter:
		CounterMessage(C, Msg, Value);
		return;
	default:
		break;
	}
	if (Msg == 7)
	{
		SetFlags(Inst, uint16(Flags[Inst] & ~FlHeld));
	}
	else if (Msg == 8)
	{
		SetFlags(Inst, uint16(Flags[Inst] | FlHeld));
	}
	else if (Msg == 1 && C.Type == TypeTimer)
	{
		C.Count = SecondsToFrames(Value);
	}
	else if (Msg == 1 && (C.Type == TypeBoost || C.Type == TypeTubeEndBoost))
	{
		C.Boost.Active = C.Boost.Frames;   // 0x1408D0
	}
	else if (Msg == 1 && C.Type == TypeCracked)
	{
		C.Timer = SecondsToFrames(Value);
	}
	else if (Msg == 2 && C.Type == TypeCracked)
	{
		C.Health = Value;
	}
}

void FEffectWorld::CounterMessage(FController& C, int32 Msg, float Value)
{
	uint32 Bit = 0;
	for (int32 K = 1; K <= 10; ++K)
	{
		if (Value == float(K)) { Bit = 1u << (K - 1); }
	}
	bool bHit = false;
	if (Msg == 1)
	{
		if (!(C.Mask & Bit)) { C.Mask |= Bit; bHit = true; }
	}
	else if (Msg == 2)
	{
		if (!(C.Mask & Bit) && (Value == 1.0f || (C.Mask & (Bit >> 1))))
		{
			C.Mask |= (Value != 1.0f) ? Bit : 1u;
			bHit = true;
		}
	}
	else if (Msg == 3)
	{
		bHit = true;
	}
	if (bHit && C.Count > 0)
	{
		--C.Count;
	}
}

void FEffectWorld::SetEnabled(int32 Inst, FController& C, int32 On)
{
	if (C.Type == TypeDead || C.Type == TypeRestore)
	{
		return;
	}
	if (C.Type == TypeLapBoost)
	{
		// 0x141198
		if (On) { RunSlot(Inst, SlotDone, nullptr); }
		return;
	}
	if (On && RunSlot(Inst, SlotDone, nullptr))
	{
		return;
	}
	if (C.Type == TypeRoller)
	{
		FOutcome O; O.Kind = EOutcome::RollerStop; O.Instance = Inst;   // 0x149E18
		Out(O);
		return;
	}
	if ((C.Type == TypeDebounce || C.Type == TypeTimer || C.Type == TypeCracked) && C.Timer < 0)
	{
		return;
	}
	if (C.Type != TypeDebounce && C.Type != TypeTimer)
	{
		RestoreFlags(Inst, true);
	}
	RemoveController(Inst);
}

void FEffectWorld::DeactivateController(int32 Inst, FController& C)
{
	if (C.Type == TypeDead || C.Type == TypeLapBoost)
	{
		return;   // 0x141190
	}
	if ((C.Type == TypeDebounce || C.Type == TypeTimer || C.Type == TypeCracked) && C.Timer < 0)
	{
		return;
	}
	if (C.Type != TypeDebounce && C.Type != TypeTimer)
	{
		RestoreFlags(Inst, false);
	}
	RemoveController(Inst);
}

void FEffectWorld::ControllerFrame(int32 Inst, FController& C)
{
	// 0x199030
	if (C.TouchCooldown > 0 && (!IsAnim(C.Type) || C.TouchCooldownReset >= 0))
	{
		--C.TouchCooldown;
	}
	switch (C.Type)
	{
	case TypeDebounce:
		if (C.Timer > 0 && --C.Timer == 0)
		{
			SetEnabled(Inst, C, 1);
		}
		break;
	case TypeTimer:
		--C.Count;
		if (C.Count < 1)
		{
			RunSlot(Inst, SlotExpired, nullptr);
			SetEnabled(Inst, C, 0);
			if (!Ctl(Inst))
			{
				return;
			}
		}
		if (C.Timer > 0 && --C.Timer == 0)
		{
			SetEnabled(Inst, C, 1);
		}
		break;
	case TypeCounter:
		if (C.Count < 1)
		{
			RunSlot(Inst, SlotExpired, nullptr);
			SetEnabled(Inst, C, 0);
		}
		else if (C.Timer > 0 && --C.Timer == 0)
		{
			SetEnabled(Inst, C, 1);
		}
		break;
	case TypeCracked:
		if (C.Timer > 0 && --C.Timer == 0)
		{
			SetEnabled(Inst, C, 1);
			return;
		}
		if (C.Health <= 0.0 && Ctl(Inst) == &C)
		{
			if (!RunSlot(Inst, SlotExpired, nullptr))
			{
				Destroy(Inst);
				SetState(Inst, 2);
			}
		}
		break;
	case TypeBoost:
	case TypeTubeEndBoost:
		BoostFrame(Inst, C);
		break;
	case TypeLapBoost:
		LapBoostFrame(Inst, C);
		break;
	case TypeZBoost:
		ZBoostFrame(Inst, C);
		break;
	default:
		break;
	}
}

void FEffectWorld::BoostFrame(int32 Inst, FController& C)
{
	if (Ctl(Inst) != &C)
	{
		return;
	}
	FBoostState& B = C.Boost;
	const bool bTube = C.Type == TypeTubeEndBoost;
	// 0x140688
	bool bAlive = B.Mode == 0;
	if (B.Active > 0 && --B.Active > 0)
	{
		bAlive = true;
	}
	if (bTube)
	{
		FMemory::Memzero(B.Flag, sizeof(B.Flag));   // 0x1419F8
	}
	if (B.Mode == 1 || B.Active <= 0)
	{
		for (FEffectRider* R : Racers)
		{
			if (!R || !R->Sim || !BoostQueries.Pairs || !BoostQueries.Inside || !BoostQueries.Pairs(Inst, *R) || !BoostQueries.Inside(Inst, *R))
			{
				continue;
			}
			bAlive = true;
			if (bTube)
			{
				TubeEndLaw(C, *R);
			}
			else
			{
				SSXTriggers::BoostNodeVelocity(R->Sim->State.Velocity, B.Dir, B.Target, B.Gain);   // 0x140838
			}
		}
	}
	if (bTube && B.Active < 1)
	{
		// 0x141A08
		for (int32 K = 0; K < Racers.Num() && K < BoostMaxRacers; ++K)
		{
			if (B.Flag[K] == 0) { B.ExitDir[K].X = TubeEndUnset; }
		}
	}
	if (!bAlive)
	{
		SetEnabled(Inst, C, 1);   // 0x13A980
	}
}

void FEffectWorld::TubeEndLaw(FController& C, FEffectRider& R)
{
	const int32 S = R.Slot;
	if (S < 0 || S >= BoostMaxRacers)
	{
		return;
	}
	FBoostState& B = C.Boost;
	const int32 Side = FMath::Clamp(TubeEndSide[S], 0, 2);
	FVector3f& D = B.ExitDir[S];
	if (D.X == TubeEndUnset)
	{
		const FVector3f E = B.Exit[Side];
		const float L2 = E.X * E.X + E.Y * E.Y + E.Z * E.Z;
		D = L2 > 0.0f ? E * (1.0f / FMath::Sqrt(L2)) : E;
	}
	SSXTriggers::BoostNodeVelocity(R.Sim->State.Velocity, D, B.ExitTarget[Side], B.Gain);
	B.Flag[S] = 1;
}

void FEffectWorld::LapBoostFrame(int32 Inst, FController& C)
{
	if (Ctl(Inst) != &C)
	{
		return;
	}
	FBoostState& B = C.Boost;
	FBox Box(ForceInit);
	if (!BoostQueries.Pairs || !BoostQueries.Bounds || !BoostQueries.Bounds(Inst, Box))
	{
		return;
	}
	bool bSeen[BoostMaxRacers] = {};
	const float MinX = float(Box.Min.X), MinY = float(Box.Min.Y), MinZ = float(Box.Min.Z);
	const float MaxX = float(Box.Max.X), MaxY = float(Box.Max.Y), MaxZ = float(Box.Max.Z);
	for (FEffectRider* R : Racers)
	{
		if (!R || !R->Sim || R->Slot < 0 || R->Slot >= BoostMaxRacers || !BoostQueries.Pairs(Inst, *R))
		{
			continue;
		}
		const int32 S = R->Slot;
		bSeen[S] = true;
		int16& Copy = B.LapCopy[S];
		if (LapsLeft(*R) <= Copy)
		{
			--Copy;
		}
		if (Copy < 0)
		{
			continue;
		}
		FRiderState& St = R->Sim->State;
		if (B.LapFlag[S] == 0)
		{
			int32 Side = 0;
			if (float(St.Position.Z) - MinZ > LapBoostSideRise)
			{
				Side = 2;
				const FMatrix& M = Data->InstMatrix[Inst];
				const float Cx = MinX + (MaxX - MinX) * 0.5f, Cy = MinY + (MaxY - MinY) * 0.5f;
				const float Along = (float(St.Position.X) - Cx) * float(M.M[0][0]) + (float(St.Position.Y) - Cy) * float(M.M[0][1]);
				if (Along < 0.0f)
				{
					Side = 1;
				}
			}
			TubeEndSide[S] = Side;
		}
		if (St.Motion == ERiderMotion::WipeOutRecover)
		{
			St.Velocity = FVector(FVector3f(St.Velocity) * LapBoostWipeDamp);
			continue;
		}
		if (St.Motion == ERiderMotion::WipeOut)
		{
			St.Velocity.X = double(float(St.Velocity.X) * LapBoostWipeDamp);
			St.Velocity.Y = double(float(St.Velocity.Y) * LapBoostWipeDamp);
			if (SSXSimPlaceholders::FWipeOutContext* X = SSXSimPlaceholders::FindWipeOutContext(*R->Sim); X && X->Body.bValid && X->Body.InvMass != 0.0)
			{
				const FVector3f V(St.Velocity);
				X->Body.P = FVector(V * float(1.0 / X->Body.InvMass));
				X->Body.V = FVector(V);
			}
		}
		const float Dx = (MaxX + MinX) * 0.5f - float(St.Position.X);
		const float Dy = (MaxY + MinY) * 0.5f - float(St.Position.Y);
		const float Dist = FMath::Sqrt(Dx * Dx + Dy * Dy);
		if (Dist <= LapBoostCatchRadius || B.LapFlag[S] != 0)
		{
			if (B.LapFlag[S] == 0 && Dist > 0.0f)
			{
				const float K = 1.0f / Dist;
				St.Position += FVector(Dx * K * LapBoostPullXY, Dy * K * LapBoostPullXY, LapBoostPullZ);
			}
			B.LapFlag[S] = 1;
			St.Velocity.X = double(float(St.Velocity.X) * LapBoostDamp);
			St.Velocity.Y = double(float(St.Velocity.Y) * LapBoostDamp);
			SSXTriggers::BoostNodeVelocity(St.Velocity, B.Dir, B.Target, B.Gain);
		}
	}
	for (int32 K = 0; K < Racers.Num() && K < BoostMaxRacers; ++K)
	{
		if (!bSeen[K]) { B.LapFlag[K] = 0; }
	}
}

void FEffectWorld::ZBoostFrame(int32 Inst, FController& C)
{
	if (Ctl(Inst) != &C)
	{
		return;
	}
	FBoostState& B = C.Boost;
	for (FEffectRider* R : Racers)
	{
		if (R && R->Sim && R->Slot >= 0 && R->Slot < BoostMaxRacers && BoostQueries.Pairs && BoostQueries.Inside
			&& BoostQueries.Pairs(Inst, *R) && BoostQueries.Inside(Inst, *R))
		{
			B.Flag[R->Slot] = 1;
		}
	}
	bool bAlive = false;
	for (FEffectRider* R : Racers)
	{
		if (!R || !R->Sim || R->Slot < 0 || R->Slot >= BoostMaxRacers || B.Flag[R->Slot] == 0)
		{
			continue;
		}
		FRiderState& St = R->Sim->State;
		const float Z = float(St.Position.Z);
		if (!(Z < B.ZTop))
		{
			B.Flag[R->Slot] = 0;
			continue;
		}
		if (B.ZTop - Z < B.ZRange)
		{
			St.Position.Z = B.ZTop;
		}
		else
		{
			if (St.Motion == ERiderMotion::WipeOut)
			{
				// TODO: the placement takes the yaw (atan2 of the forward);
				const FVector Fwd = FVector(St.BasisForward.X, St.BasisForward.Y, 0.0).GetSafeNormal();
				R->Sim->Spawn(St.Position, Fwd.IsNearlyZero() ? FVector(0, 1, 0) : Fwd, St.Velocity, 0.0, false);
			}
			SSXTriggers::BoostNodeVelocity(St.Velocity, B.Dir, B.Target, B.Gain);
			St.Velocity.X = 0.0;
			St.Velocity.Y = 0.0;
		}
		bAlive = true;
	}
	if (!bAlive)
	{
		SetEnabled(Inst, C, 1);
	}
}

bool FEffectWorld::RunSlot(int32 Inst, int32 Slot, FEffectRider* Rider)
{
	const int32 E = Data->EffectFor(Inst, Slot);
	if (E < 0)
	{
		return false;
	}
	RunEffect(E, Inst, Rider);
	return true;
}

void FEffectWorld::Emitter(const FScript& S, const FRecord& R, int32 RecIndex)
{
	const int32 Kind = Data->I(R, 0);
	if (const FController* C = S.Inst != INDEX_NONE ? Ctl(S.Inst) : nullptr; C && C->Type == TypeDead)
	{
		return;
	}
	FEmitter E;
	E.Inst = S.Inst;
	E.Kind = Kind;
	E.Record = RecIndex;
	FOutcome O; O.Kind = EOutcome::Emitter; O.Instance = S.Inst; O.A = Kind; O.Record = RecIndex;
	if (Kind == 0 || Kind == 2)
	{
		if (Kind == 2)
		{
			if (!S.Rider)
			{
				return;
			}
			if (bCollideEmitterQuota && S.Rider->CollideEmitters >= (S.Rider->bHuman ? HumanCollideEmitters : AICollideEmitters))
			{
				return;   // 0x148900 / 0x31D498
			}
			++S.Rider->CollideEmitters;
			E.Rider = S.Rider;
			O.Rider = S.Rider;
		}
		const float Emission = Data->F(R, 3), Life = Data->F(R, 6), Range = Data->F(R, 8);
		E.bContinuous = Emission < 0.f;
		E.Life = double(Emission) + double(Life) + double(Range) / 2.0;
	}
	else if (Kind == 1)
	{
		if (Data->I(R, 1) == -1)
		{
			return;
		}
		// 0x1FA108
		O.B = Data->I(R, 1);
		const FSplinePathConfig C = SplinePath::ConfigFromWords([&](int32 K) { return Data->I(R, K); },
			[&](int32 K) { return Data->F(R, K); });
		const FIntPoint Spl = PathSplines.IsValidIndex(C.Spline) ? PathSplines[C.Spline] : FIntPoint(INDEX_NONE, 0);
		SplinePath::Init(E.Path, C, PathSegments, Spl.X, Spl.Y);
	}
	else
	{
		return;
	}
	E.Serial = NextEmitterSerial++;
	O.Extra = Kind == 1 ? E.Serial : 0;
	Emitters.Add(E);
	Out(O);
}

void FEffectWorld::SetRail(int32 Spline, int32 Enable)
{
	if (RailEnabled.IsValidIndex(Spline))
	{
		RailEnabled[Spline] = uint8(Enable & 1);
	}
	FOutcome O; O.Kind = EOutcome::Rail; O.A = Spline; O.B = Enable;
	Out(O);
}

void FEffectWorld::Teleport(FEffectRider& Rider, int32 Inst)
{
	static const FVector2D SlotOffset[5] = { { -1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
	const FVector2D Off = (Rider.Slot >= 0 && Rider.Slot < 5) ? SlotOffset[Rider.Slot] : FVector2D::ZeroVector;
	const FMatrix& M = Data->InstMatrix[Inst];
	const FVector Row0(M.M[0][0], M.M[0][1], M.M[0][2]), Row1(M.M[1][0], M.M[1][1], M.M[1][2]), Origin(M.M[3][0], M.M[3][1], M.M[3][2]);
	FOutcome O; O.Kind = EOutcome::Teleport; O.Rider = &Rider; O.Instance = Inst;
	O.P = Origin + Row0 * (Off.X * TeleportSpacing) + Row1 * (Off.Y * TeleportSpacing);
	// 0x11A098 / 0x2510F8 / 0x11CAE8 / 0x177DB0
	const float Yaw = SSXAnim::Atan2EE(float(Row1.Y), float(Row1.X));
	O.V = Yaw;
	if (Rider.Sim)
	{
		Rider.Sim->SpawnYaw(O.P, Yaw, FVector::ZeroVector, 0.0, false);
	}
	Out(O);
}

void FEffectWorld::Touch(int32 Inst, FEffectRider& Rider, bool bImpact, double VN, double A)
{
	if (!Data || !Flags.IsValidIndex(Inst))
	{
		return;
	}
	FController* C = Ctl(Inst);
	if (!C || (C->Type == TypeRoller && C->bRollerLive))
	{
		RunSlot(Inst, SlotTouch, &Rider);
		// 0x145458
		FController* N = Ctl(Inst);
		if (bCrackedCreateDamage && !C && N && N->Type == TypeCracked && bImpact && N->Health > 0.0)
		{
			N->TouchCooldown = CrackedCooldownFrames;
			N->Health -= FMath::Abs(VN * A) * CrackDamage;
		}
		return;
	}
	if (IsTouchForward(C->Type) || IsAnim(C->Type))
	{
		const bool bReady = IsTouchForward(C->Type) ? C->TouchCooldown < 1 : C->TouchCooldown == 0;
		if (bReady)
		{
			const TSharedPtr<FController> Keep = Controllers.FindRef(Inst);
			RunSlot(Inst, SlotTouch, &Rider);
			C->TouchCooldown = IsAnim(C->Type) ? C->TouchCooldownReset : TouchForwardFrames;
		}
	}
	else if (C->Type == TypeCracked && bImpact && C->TouchCooldown == 0 && C->Health > 0.0)
	{
		C->TouchCooldown = CrackedCooldownFrames;
		C->Health -= FMath::Abs(VN * A) * CrackDamage;
	}
	else if (C->Type == TypeFence)
	{
		FOutcome O; O.Kind = EOutcome::FenceWobble; O.Instance = Inst; O.Rider = &Rider;
		Out(O);
	}
}

void FEffectWorld::MessageInstance(int32 Inst, int32 Msg, float Value)
{
	if (FController* C = Flags.IsValidIndex(Inst) ? Ctl(Inst) : nullptr)
	{
		Message(Inst, *C, Msg, Value);
	}
}

void FEffectWorld::NodeDone(int32 Inst)
{
	if (FController* C = Ctl(Inst))
	{
		const TSharedPtr<FController> Keep = Controllers.FindRef(Inst);
		SetEnabled(Inst, *C, 1);
	}
}

void FEffectWorld::SetRollerAsleep(int32 Inst, bool bAsleep)
{
	if (FController* C = Ctl(Inst); C && C->Type == TypeRoller)
	{
		C->bRollerLive = bAsleep;
	}
}

bool FEffectWorld::CreateCodeNode(int32 Inst, int32 Type, int32 Record)
{
	if (!Flags.IsValidIndex(Inst) || Ctl(Inst))
	{
		return false;
	}
	FController N;
	N.Type = Type;
	N.Record = Record;
	AddController(Inst, N);
	return true;
}

void FEffectWorld::ApplyNodeFlags(int32 Inst, uint16 Clear, uint16 Set)
{
	if (Flags.IsValidIndex(Inst))
	{
		SetFlags(Inst, uint16((Flags[Inst] & ~Clear) | Set));
	}
}

void FEffectWorld::Activate(int32 Inst)
{
	if (!Flags.IsValidIndex(Inst))
	{
		return;
	}
	SetFlags(Inst, uint16(Flags[Inst] | FlCellActive));
	const FController* C = Ctl(Inst);
	if (!C)
	{
		RunSlot(Inst, SlotActivate, nullptr);
	}
	else if (C->Type == TypeRoller && C->bRollerLive)
	{
		FOutcome O; O.Kind = EOutcome::NodeRestart; O.Instance = Inst; O.A = C->Type;
		Out(O);
	}
}

void FEffectWorld::Deactivate(int32 Inst)
{
	if (!Flags.IsValidIndex(Inst))
	{
		return;
	}
	SetFlags(Inst, uint16(Flags[Inst] & ~FlCellActive));
	FController* C = Ctl(Inst);
	if (C && C->Type == TypeDead)
	{
		return;
	}
	const TSharedPtr<FController> Keep = Controllers.FindRef(Inst);
	if (!RunSlot(Inst, SlotDeactivate, nullptr) && C)
	{
		DeactivateController(Inst, *C);
	}
}

void FCellTracker::Init(const FEffectData* InData, bool bShowOffMode)
{
	// 0x115DF0
	Data = InData;
	bShowOff = bShowOffMode;
	Slots.Init(FSlot(), CellSlots);
}

int32 FCellTracker::NumActive() const
{
	int32 N = 0;
	for (const FSlot& S : Slots) { N += S.Cell >= 0 ? 1 : 0; }
	return N;
}

void FCellTracker::CellsAround(double X, double Y, TArray<int32>& Out) const
{
	if (!Data || Data->Nx <= 0 || Data->Ny <= 0 || Data->CellSize <= 0.0)
	{
		return;
	}
	// 0x1160F8
	const float Cell = float(Data->CellSize);
	const int32 I = int32(((float(X) - float(Data->MinX)) + Cell) / Cell);
	const int32 J = int32(((float(Y) - float(Data->MinY)) + Cell) / Cell);
	for (int32 Ix = FMath::Max(I - CellRadius, 0); Ix <= FMath::Min(FMath::Max(I, 0), Data->Nx - 1); ++Ix)
	{
		for (int32 Iy = FMath::Max(J - CellRadius, 0); Iy <= FMath::Min(FMath::Max(J, 0), Data->Ny - 1); ++Iy)
		{
			Out.Add(Iy * Data->Nx + Ix);
		}
	}
}

int32 FCellTracker::CellOf(double X, double Y) const
{
	if (!Data || Data->Nx <= 0 || Data->Ny <= 0 || Data->CellSize <= 0.0)
	{
		return -1;
	}
	const float Cell = float(Data->CellSize);
	const int32 I = FMath::Clamp(int32(((float(X) - float(Data->MinX)) + Cell) / Cell) - 1, 0, Data->Nx - 1);
	const int32 J = FMath::Clamp(int32(((float(Y) - float(Data->MinY)) + Cell) / Cell) - 1, 0, Data->Ny - 1);
	return J * Data->Nx + I;
}

void FCellTracker::Mark(int32 Cell)
{
	// 0x116420
	if (Data->Cells[Cell].NumNormal < 0)
	{
		return;
	}
	int32 Free = INDEX_NONE;
	for (int32 K = 0; K < Slots.Num(); ++K)
	{
		if (Slots[K].Cell == Cell)
		{
			Slots[K].Count = 1;
			return;
		}
		if (Free == INDEX_NONE && Slots[K].Cell < 0)
		{
			Free = K;
		}
	}
	if (Free != INDEX_NONE)
	{
		Slots[Free].Cell = Cell;
		Slots[Free].Count = 1;
		Slots[Free].bNew = true;
	}
}

void FCellTracker::ForInstances(int32 Cell, TFunctionRef<void(int32)> Fn) const
{
	const FEffectData::FCell& C = Data->Cells[Cell];
	const int32 N = bShowOff ? C.NumAlt : C.NumNormal;
	const int32 First = bShowOff ? C.FirstAlt : C.FirstNormal;
	for (int32 K = 0; K < N; ++K)
	{
		Fn(Data->CellRefs[First + K]);
	}
}

void FCellTracker::StepRacers(FEffectWorld& World, TConstArrayView<FEffectRider*> Riders, bool bBeforeStartRace, const FVector* Camera)
{
	if (Riders.Num() == 0)
	{
		return;
	}
	for (FEffectRider* R : Riders)
	{
		R->Cell = R->Sim ? CellOf(R->Sim->State.Position.X, R->Sim->State.Position.Y) : -1;
	}
	bool bCamera = false;
	if (Riders[0]->PrevCell == Riders[0]->Cell)
	{
		if (bBeforeStartRace)
		{
			bCamera = true;
		}
		else
		{
			bool bChanged = false;
			for (const FEffectRider* R : Riders)
			{
				bChanged |= R->PrevCell != R->Cell;
			}
			if (!bChanged)
			{
				return;
			}
		}
	}
	TArray<FVector2D, TInlineAllocator<8>> Points;
	for (const FEffectRider* R : Riders)
	{
		if (R->Sim) { Points.Add(FVector2D(R->Sim->State.Position.X, R->Sim->State.Position.Y)); }
	}
	if (bCamera && Camera)
	{
		Points.Add(FVector2D(Camera->X, Camera->Y));   // 0x173C18
	}
	Step(World, Points);
}

void FCellTracker::Step(FEffectWorld& World, TConstArrayView<FVector2D> Points)
{
	if (!Data)
	{
		return;
	}
	if (Slots.Num() != CellSlots)
	{
		Slots.Init(FSlot(), CellSlots);
	}
	for (FSlot& S : Slots)   // 0x1162C0
	{
		if (S.Cell >= 0) { --S.Count; }
	}
	TArray<int32> Around;
	for (const FVector2D& P : Points)   // 0x1160F8
	{
		Around.Reset();
		CellsAround(P.X, P.Y, Around);
		for (int32 C : Around) { Mark(C); }
	}
	for (FSlot& S : Slots)   // 0x1162F8
	{
		if (S.Cell < 0)
		{
			continue;
		}
		if (S.bNew)
		{
			ForInstances(S.Cell, [&World](int32 I) { World.Activate(I); });   // 0x25FED0
			S.bNew = false;
		}
		else if (S.Count < 1)
		{
			ForInstances(S.Cell, [&World](int32 I) { World.Deactivate(I); });   // 0x260108
			S = FSlot();
		}
	}
}
}
