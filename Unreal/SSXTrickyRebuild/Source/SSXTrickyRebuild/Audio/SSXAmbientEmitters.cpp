#include "Audio/SSXAmbientEmitters.h"

#include "Audio/SSXPropSounds.h"
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXCrowdSystem.h"
#include "Algo/Reverse.h"
#include "Sim/Anim/RiderPose.h"

namespace
{
	constexpr float HalfPi = 1.5707964f;

	float Dot(const FVector3f& A, const FVector3f& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }   // 0x137140

	/** 0x22ACC0 / 0x22B370 ellipsoid basis: A = stored axis (not normalised), B = normalise(up x A) with up = (0, 0, 1), C = A x B. */
	void EllipsoidBasis(const FSSXAmbientShape& S, FVector3f& A, FVector3f& B, FVector3f& C)
	{
		A = FVector3f(S.F(8), S.F(9), S.F(10));
		B = FVector3f(A.Z * 0.f - 1.f * A.Y, 1.f * A.X - A.Z * 0.f, A.Y * 0.f - A.X * 0.f);
		const float K = 1.f / FMath::Sqrt(Dot(B, B));
		B *= K;
		C = FVector3f(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X);
	}

	const TArray<int32> NoRecords;
}

bool SSXAmbient::Curve(int32 Kind, float X, float& Out)
{
	// 0x22BC40
	switch (Kind)
	{
	case 0: Out = 1.f - X * X; return true;
	case 1: Out = 1.f - X / (1.5f - X * 0.5f); return true;
	case 2: Out = 1.f - X; return true;
	case 3: Out = (1.f - X) / (1.5f - (1.f - X) * 0.5f); return true;
	case 4: Out = (1.f - X) * (1.f - X); return true;
	case 5: Out = X <= 0.7f ? 1.f : (1.f - X) / 0.3f; return true;
	default: Out = 0.f; return false;   // the original returns $f3 unset (its own undefined value; not reproduced)
	}
}

FString SSXAmbient::SwapBankName(int32 Id, bool bTraffic4440, bool bTraffic4444)
{
	// 0x22C648 / 0x21A640
	static const TCHAR* const A[] = {
		TEXT("Dynkicker1"), TEXT("Dynkicker2"), TEXT("GoGo1"), TEXT("GoGo2"), TEXT("Gravelpit"), TEXT("Helicopter2"),
		TEXT("Megaplexfan"), TEXT("Movingramp"), TEXT("Octocar1"), TEXT("River"), TEXT("RiverSmall"), TEXT("Snowmachine"),
		TEXT("Snowmachinemist"), TEXT("Snowcat"), TEXT("Steam1"), TEXT("Steam2"), nullptr /* 0x5F traffic */, TEXT("Truckidle") };
	static const TCHAR* const B[] = {
		TEXT("Birds1"), TEXT("Cowbell"), TEXT("CowbellsWoo"), TEXT("Dog1"), TEXT("Tunnel1"), TEXT("Woo1"), TEXT("Woo2"),
		TEXT("Bird_Crows"), TEXT("Bird_Eagle"), TEXT("Bird_Flap"), TEXT("Bird_Owl"), TEXT("Bird_Woodpecker"), TEXT("Coyote"),
		TEXT("Wind1"), TEXT("Wind2"), TEXT("Wolf"), TEXT("Plane_Far"), TEXT("Birds_Summer"), TEXT("Birds_Vulture"),
		TEXT("Birds_Tropical"), TEXT("Boat"), TEXT("TV1"), TEXT("TV2"), TEXT("TV3"), TEXT("TV4"), TEXT("Dog2"), TEXT("Parkade"),
		TEXT("MineShaft"), TEXT("RattleSnake"), TEXT("Seagulls1"), TEXT("Seagulls2"), nullptr /* 0x86 traffic */,
		TEXT("Penguins"), TEXT("Birds_Nightingale"), TEXT("Dog3"), TEXT("Birds_Hawk"), TEXT("Birds_Lark"), TEXT("Birds_Pigeon"),
		TEXT("Birds_Falcon"), TEXT("Birds_Woodpecker2"), TEXT("Birds_Sparrow"), TEXT("Bird_Crows2"), TEXT("Bird_Crows3"),
		TEXT("Bird_Eagle2"), TEXT("Bird_Eagle3") };
	static const TCHAR* const C[] = {
		TEXT("SkyTrain_Outdoors"), TEXT("SkyTrainStation"), TEXT("Mall"), TEXT("Sewer"), TEXT("Office"), TEXT("Port"),
		TEXT("Alleyway_1"), TEXT("Alleyway_2"), TEXT("Alleyway_3"), TEXT("Alleyway_4"), TEXT("Alleyway_5"), TEXT("Alleyway_6"),
		TEXT("Alleyway_7"), TEXT("Alleyway_8"), TEXT("ElectricRoof"), TEXT("ElectricRoof2"), TEXT("Magnetic_Turbo_Donut"),
		TEXT("Super_Conducting_Collider"), TEXT("CopCar"), TEXT("Waterfall") };
	static const TCHAR* const D[] = { TEXT("Construction1"), TEXT("Construction2"), TEXT("UFO") };
	static_assert(UE_ARRAY_COUNT(A) == 0x12 && UE_ARRAY_COUNT(B) == 0x2D && UE_ARRAY_COUNT(C) == 0x14, "swap table");
	if (Id == 0x5F) { return bTraffic4440 ? TEXT("Trafficloop") : TEXT("Traffic_Loop2"); }
	if (Id == 0x86) { return bTraffic4444 ? TEXT("Trafficloop") : TEXT("Traffic_Loop2"); }
	if (Id >= 0x4F && Id <= 0x60) { return A[Id - 0x4F]; }
	if (Id >= 0x67 && Id <= 0x93) { return B[Id - 0x67]; }
	if (Id >= 0x9F && Id <= 0xB2) { return C[Id - 0x9F]; }
	if (Id >= 0xB7 && Id <= 0xB9) { return D[Id - 0xB7]; }
	return FString();
}

void FSSXChantBanks::Load(const FSSXInfFile& Inf)
{
	// 0x22DCE0
	static const TCHAR* const Keys[13] = { TEXT("EDDIE"), TEXT("KAORI"), TEXT("LUTHER"), TEXT("MAC"), TEXT("MOBY"), TEXT("ZOE"),
		TEXT("JP"), TEXT("ELISE"), TEXT("PSYMON"), TEXT("SEEIAH"), TEXT("BRODI"), TEXT("MARISOL"), TEXT("GENERAL") };
	for (TArray<FString>& L : Character) { L.Reset(); }
	General.Reset();
	CharCount = GenCount = -1;   // 0x22DCE0
	if (const FSSXInfFile::FSection* C = Inf.Find(TEXT("COUNT")))
	{
		const FString A = C->Get(TEXT("CHARCHANTBANKS")), B = C->Get(TEXT("GENCHANTBANKS"));
		if (!A.IsEmpty()) { CharCount = FCString::Atoi(*A); }
		if (!B.IsEmpty()) { GenCount = FCString::Atoi(*B); }
	}
	const FSSXInfFile::FSection* G = Inf.Find(TEXT("GLOBAL"));
	if (!G)
	{
		return;
	}
	for (int32 K = 0; K < 13; ++K)
	{
		TArray<FString>& L = K < 12 ? Character[K] : General;
		for (int32 N = 1; N <= 20; ++N)
		{
			const FString V = G->Get(FString::Printf(TEXT("%s%d"), Keys[K], N));
			if (V.IsEmpty())
			{
				continue;
			}
			L.SetNum(FMath::Max(L.Num(), N));
			L[N - 1] = FPaths::GetBaseFilename(V.Replace(TEXT("\\"), TEXT("/")));
		}
	}
}

FString FSSXChantBanks::Pick(int32 CharacterIndex, int32 Rank, TFunctionRef<uint32()> Rand) const
{
	auto Index = [](uint32 R, int32 Count) { return int32((uint32(R & 0x7FFF) * uint32(FMath::Max(Count, 0))) / 0x7FFFu); };
	auto Own = [&]() -> FString
	{
		const int32 I = Index(Rand(), CharCount);
		if (CharCount <= I || CharacterIndex < 0 || CharacterIndex >= 12)
		{
			return FString();   // 0x22C648
		}
		return Character[CharacterIndex].IsValidIndex(I) ? Character[CharacterIndex][I] : FString();
	};
	auto Generic = [&]() -> FString
	{
		const int32 I = Index(Rand(), GenCount);
		return General.IsValidIndex(I) ? General[I] : FString();
	};
	if (Rank < 2)
	{
		return Own();
	}
	if (Rank < 4)
	{
		return (int32((Rand() & 0x7FFF) * 0x65u / 0x7FFFu) < 0x32) ? Own() : Generic();
	}
	return Generic();
}

void FSSXAmbientEmitters::ShapeBounds(const FSSXAmbientShape& S, FVector3f& Lo, FVector3f& Hi)
{
	// 0x22ACC0
	const FVector3f O = S.Offset();
	if (S.Type() == 1)
	{
		FVector3f A, B, C;
		EllipsoidBasis(S, A, B, C);
		const FVector3f E = A * S.F(5) + B * S.F(6) + C * S.F(7);
		Lo = O - E;
		Hi = O + E;
		return;
	}
	const float R = S.F(5);
	Lo = O - FVector3f(R);
	Hi = O + FVector3f(R);
}

void FSSXAmbientEmitters::RecordBounds(const FSSXAmbientRecord& R, FVector3f& Lo, FVector3f& Hi)
{
	// 0x22AAA0
	Lo = FVector3f(10000.f);
	Hi = FVector3f(-10000.f);
	for (const FSSXAmbientShape& S : R.Shapes)
	{
		FVector3f A, B;
		ShapeBounds(S, A, B);
		Lo = FVector3f(FMath::Min(Lo.X, A.X), FMath::Min(Lo.Y, A.Y), FMath::Min(Lo.Z, A.Z));
		Hi = FVector3f(FMath::Max(Hi.X, B.X), FMath::Max(Hi.Y, B.Y), FMath::Max(Hi.Z, B.Z));
	}
	Lo += R.Position;
	Hi += R.Position;
}

void FSSXAmbientEmitters::Reset()
{
	Table = nullptr;
	Cells.Reset();
	for (FSlot& S : Slots) { S = FSlot(); }
	bEnabled = true;
	NumEnableCalls = 0;
	ClockMs = 0.0;
	PendingEnables.Reset();
}

void FSSXAmbientEmitters::Build(FSSXPropSounds* InTable)
{
	Reset();
	Table = InTable;
	if (!Table || Table->GridNx <= 0 || Table->GridNy <= 0 || Table->GridCell <= 0.f)
	{
		return;
	}
	const int32 Nx = Table->GridNx, Ny = Table->GridNy;
	const float Cell = Table->GridCell;
	Cells.SetNum(Nx * Ny);
	for (int32 RI = 0; RI < Table->AmbientRecords.Num(); ++RI)
	{
		const FSSXAmbientRecord& R = Table->AmbientRecords[RI];
		if (R.Shapes.Num() == 0)
		{
			continue;
		}
		// 0x22B008
		FVector3f Lo, Hi;
		RecordBounds(R, Lo, Hi);
		int32 C[4] = {
			int32(((Lo.X - Table->GridMinX) + Cell) / Cell) - 1, int32(((Lo.Y - Table->GridMinY) + Cell) / Cell) - 1,
			int32(((Hi.X - Table->GridMinX) + Cell) / Cell) - 1, int32(((Hi.Y - Table->GridMinY) + Cell) / Cell) - 1 };
		for (int32& V : C) { V = FMath::Max(V, 0); }
		C[0] = FMath::Min(C[0], Nx - 1);
		C[2] = FMath::Min(C[2], Nx - 1);
		C[1] = FMath::Min(C[1], Ny - 1);
		C[3] = FMath::Min(C[3], Ny - 1);
		for (int32 X = C[0]; X <= C[2]; ++X)
		{
			for (int32 Y = C[1]; Y <= C[3]; ++Y)
			{
				Cells[Nx * Y + X].Add(RI);
			}
		}
	}
	for (TArray<int32>& L : Cells) { Algo::Reverse(L); }
}

int32 FSSXAmbientEmitters::CellIndex(float X, float Y) const
{
	// 0x22B370
	if (!Table || Cells.Num() == 0)
	{
		return -1;
	}
	const float Cell = Table->GridCell;
	const int32 I = Table->GridNx * (int32(((Y - Table->GridMinY) + Cell) / Cell) - 1) + int32(((X - Table->GridMinX) + Cell) / Cell) - 1;
	return I >= 0 && I < Cells.Num() ? I : -1;
}

const TArray<int32>& FSSXAmbientEmitters::CellRecords(int32 Cell) const
{
	return Cells.IsValidIndex(Cell) ? Cells[Cell] : NoRecords;
}

void FSSXAmbientEmitters::Query(const FVector3f& P, int32 Listener)
{
	const int32 Cell = CellIndex(P.X, P.Y);
	if (Cell < 0)
	{
		return;
	}
	for (const int32 RI : Cells[Cell])
	{
		const FSSXAmbientRecord& R = Table->AmbientRecords[RI];
		for (int32 SI = 0; SI < R.Shapes.Num(); ++SI)
		{
			const FSSXAmbientShape& S = R.Shapes[SI];
			const FVector3f Centre = R.Position + S.Offset();
			const FVector3f D = P - Centre;
			float Vol = 0.f;
			switch (S.Type())
			{
			case 0:
			{
				const float Dist = FMath::Abs(FMath::Sqrt(Dot(D, D)));
				if (Dist < S.F(5) && SSXAmbient::Curve(int32(S.F(6)), Dist / S.F(5), Vol))
				{
					Register(Vol, R.Instance, SI, Listener, S.SoundId(), true, Centre);
				}
				break;
			}
			case 1:
			{
				FVector3f A, B, C;
				EllipsoidBasis(S, A, B, C);
				auto Term = [](float Proj, float Axis) { return Axis == 0.f ? 0.f : (Proj * Proj) / (Axis * Axis); };
				const float Q = Term(Dot(A, D), S.F(5)) + Term(Dot(B, D), S.F(6)) + Term(Dot(C, D), S.F(7));
				if (Q < 1.f && SSXAmbient::Curve(int32(S.F(11)), Q, Vol))
				{
					Register(Vol, R.Instance, SI, Listener, S.SoundId(), true, Centre);
				}
				break;
			}
			case 2:
			{
				const float Dist = FMath::Abs(FMath::Sqrt(Dot(D, D)));
				if (Dist < S.F(5))
				{
					const FVector3f U = D * (1.f / Dist);
					const float Cos = Dot(U, FVector3f(S.F(6), S.F(7), S.F(8)));
					float V1 = 0.f, V2 = 0.f;
					if (S.F(10) < Cos && SSXAmbient::Curve(int32(S.F(9)), Dist / S.F(5), V1)
						&& SSXAmbient::Curve(int32(S.F(11)), (HalfPi - SSXAnim::AsinEE(Cos)) / (HalfPi - SSXAnim::AsinEE(S.F(10))), V2))
					{
						Register(V1 * V2, R.Instance, SI, Listener, S.SoundId(), true, Centre);
					}
				}
				break;
			}
			case 3:
			{
				if (FMath::Abs(FMath::Sqrt(Dot(D, D))) < S.F(5))
				{
					Register(1.f, R.Instance, SI, Listener, S.SoundId(), false, Centre);
				}
				break;
			}
			default: break;
			}
		}
	}
}

void FSSXAmbientEmitters::Register(float Vol, int32 Instance, int32 Shape, int32 Listener, int32 SoundId, bool b3D, const FVector3f& Pos)
{
	// 0x22D260
	if (!bEnabled)
	{
		return;
	}
	FSlot* Found = nullptr;
	for (FSlot& S : Slots)   // 0x22D1B8 / 0x22BF30
	{
		if (S.bActive && S.Instance == Instance && S.Shape == Shape && S.Listener == Listener) { Found = &S; break; }
	}
	if (Found)
	{
		Found->Volume = Vol;
	}
	else
	{
		for (FSlot& S : Slots)   // 0x22D188
		{
			if (!S.bActive) { Found = &S; break; }
		}
		if (!Found)
		{
			return;   // with all 40 slots active the original writes through a null slot (its own fault; not reproduced)
		}
		Found->bActive = true;
		Found->Instance = Instance;
		Found->Shape = Shape;
		Found->Listener = Listener;
		Found->SoundId = SoundId;
		Found->Volume = Vol;
		Found->b3D = b3D;
		Found->Position = Pos;
	}
	Found->bTouched = true;
}

void FSSXAmbientEmitters::ClearSlot(FSlot& S)
{
	// 0x22D080
	S.bActive = false;
	S.Swap = INDEX_NONE;
	S.SwapBank.Reset();
	S.bTouched = false;
	S.Handle = INDEX_NONE;
}

void FSSXAmbientEmitters::StopSlot(FSlot& S)
{
	// 0x22C5A0 / 0x20C2A0 / 0x220F98
	if (S.Handle != INDEX_NONE)
	{
		if (S.bStream && Crowd)
		{
			Crowd->RemoveEmitter(S.Handle, SSXAmbient::StopFade);
		}
		else if (Stop)
		{
			Stop(S.Handle, SSXAmbient::StopFade);
		}
	}
	S.Swap = INDEX_NONE;
	S.SwapBank.Reset();
	S.Handle = INDEX_NONE;
}

void FSSXAmbientEmitters::Update()
{
	// 0x22D370
	for (int32 I = 0; I < SSXAmbient::MaxSlots; ++I)
	{
		FSlot& S = Slots[I];
		if (!S.bActive)
		{
			continue;
		}
		if (!S.bTouched)
		{
			S.bActive = false;
			StopSlot(S);
		}
		else
		{
			UpdateSlot(I);
		}
		S.bTouched = false;
	}
}

void FSSXAmbientEmitters::UpdateSlot(int32 I)
{
	// 0x22BF60
	FSlot& S = Slots[I];
	const int32 Vol = int32(S.Volume * 127.f);
	if (S.Handle != INDEX_NONE)
	{
		// 0x221000 / 0x20C200
		if (S.bStream && Crowd)
		{
			Crowd->SetEmitterWeight(S.Handle, Vol);
		}
		else if (SetVolume && (S.bStream || (IsPlaying && IsPlaying(S.Handle))))
		{
			SetVolume(S.Handle, Vol);
		}
		return;
	}
	FSSXAmbientStart P;
	P.Slot = I;
	P.Instance = S.Instance;
	P.SoundId = S.SoundId;
	P.Volume127 = FMath::Clamp(Vol, 0, 127);
	P.b3D = S.b3D;
	P.Position = S.Position;
	P.Listener = S.Listener;
	// 0x22C610 / 0x22D8A8
	if (!FSSXPropSounds::Resolve(uint32(S.SoundId), P.Bank, P.Index))
	{
		P.Index = INDEX_NONE;
	}
	if (SSXAmbient::IsStreamId(S.SoundId))
	{
		S.bStream = true;   // 0x2210E8 / 0x2212A0
		P.bStream = true;
		if (Crowd)
		{
			// 0x2212A0
			S.Handle = Crowd->AddEmitter(S.Listener, S.Position, P.Volume127, P.Index, S.b3D);
		}
		else
		{
			S.Handle = Start ? Start(P) : INDEX_NONE;
		}
		return;
	}
	for (int32 K = 0; K < SSXAmbient::MaxSlots; ++K)   // 0x22DC88
	{
		const FSlot& O = Slots[K];
		if (K != I && O.bActive && O.Instance == S.Instance && O.Shape == S.Shape)
		{
			ClearSlot(S);
			return;
		}
	}
	S.bStream = false;
	bool bReady = true;
	if (P.Bank == 6)
	{
		// 0x20ED20
		for (const FSlot& O : Slots)   // 0x22DC38
		{
			if (O.bActive && O.Swap != INDEX_NONE && O.SoundId != S.SoundId)
			{
				ClearSlot(S);
				return;
			}
		}
		P.bChant = S.SoundId == SSXAmbient::RiderAmbienceId;
		if (S.Swap == INDEX_NONE)
		{
			// 0x22C648
			const FString Bank = P.bChant ? (RiderAmbienceBank ? RiderAmbienceBank(S.Listener) : FString())
				: SSXAmbient::SwapBankName(S.SoundId);
			if (Bank.IsEmpty())
			{
				ClearSlot(S);   // 0x22C648
				return;
			}
			S.Swap = 1;   // 0x21A640
			S.SwapBank = Bank;
		}
		P.SwapBank = S.SwapBank;
		bReady = !SwapReady || SwapReady(P.SwapBank);   // 0x21A548
	}
	if (SSXAmbient::IsHitActivated(S.SoundId) && !(Table && Table->IsInHitList(S.Instance)))
	{
		ClearSlot(S);
		return;
	}
	if (P.Bank == 6 && !bReady)
	{
		return;
	}
	S.Handle = Start ? Start(P) : INDEX_NONE;   // 0x20C948
}

void FSSXAmbientEmitters::StopAll()
{
	for (FSlot& S : Slots)   // 0x22D408
	{
		if (S.bActive)
		{
			S.bActive = false;
			StopSlot(S);
		}
	}
}

void FSSXAmbientEmitters::RaceRestart()
{
	StopAll();
	if (Table)
	{
		Table->ResetHitList();   // 0x22E360
	}
}

void FSSXAmbientEmitters::SetEnabled(bool bOn, bool bDeferred)
{
	++NumEnableCalls;
	if (bOn && bDeferred)
	{
		PendingEnables.Add(ClockMs + SSXAmbient::DeferredEnableMs);
		return;
	}
	bEnabled = bOn;
}

void FSSXAmbientEmitters::AdvanceMs(double Ms)
{
	ClockMs += Ms;
	for (int32 K = PendingEnables.Num() - 1; K >= 0; --K)
	{
		if (ClockMs >= PendingEnables[K])
		{
			PendingEnables.RemoveAt(K);
			bEnabled = true;   // 0x21E498 / 0x22E400
		}
	}
}

int32 FSSXAmbientEmitters::NumActive() const
{
	int32 N = 0;
	for (const FSlot& S : Slots) { N += S.bActive ? 1 : 0; }
	return N;
}
