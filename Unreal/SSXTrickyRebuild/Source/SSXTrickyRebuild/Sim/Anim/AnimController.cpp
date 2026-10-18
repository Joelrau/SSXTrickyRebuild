#include "Sim/Anim/AnimController.h"

#include "Race/GameRng.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/EEFloat.h"

namespace SSXAnim
{
namespace
{
	int32 ClipFrames(const FAflLibrary& Lib, int32 Clip)
	{
		const FAflClip* C = Lib.Clip(Clip);
		return C ? C->NFrames : 0;
	}

	float FramesOf(const FAflLibrary& Lib, int32 Clip)
	{
		return float(ClipFrames(Lib, Clip) - 1);
	}

	// 0x1601D0, 0x160B20
	float Atan2EE(float Y, float X)
	{
		if (X == 0.0f)
		{
			return Y < 0.0f ? -1.5707964f : 1.5707964f;
		}
		float A = float(FMath::Atan(double(Y / X)));
		if (X < 0.0f)
		{
			A = Y > 0.0f ? A + 3.1415927f : A - 3.1415927f;
		}
		return A;
	}

	void Set(FProceduralSubs& R, int32 K, int32 Clip, double Frame, double Weight)
	{
		R.Subs[K].Clip = Clip;
		R.Subs[K].Frame = Frame;
		R.Subs[K].Weight = Weight;
		R.Num = FMath::Max(R.Num, K + 1);
	}
}

// 0x161400, 0x379620
int32 TrickPhaseChain(int32 Id)
{
	if (Id == 0x226) { return 0x22E; }
	if (Id == 0x227) { return 0x22F; }
	if (Id >= 0x228 && Id <= 0x22D) { return 0x230 + (Id - 0x228); }
	if (Id == 0x270 || Id == 0x274) { return 0x235; }
	if (Id == 0x271 || Id == 0x275) { return 0x234; }
	if (Id == 0x272 || Id == 0x276 || Id == 0x277) { return 0x22F; }
	if (Id == 0x273) { return 0x22E; }
	return 0x22F;
}

int32 SpinPlayOutId(int32 Id)
{
	// 0x102800
	switch (Id)
	{
	case 0x226: case 0x22E: case 0x273: case 0x277: return 0x236;
	case 0x227: case 0x22F: case 0x272: case 0x276: return 0x237;
	case 0x228: case 0x230: return 0x238;
	case 0x229: case 0x231: return 0x239;
	case 0x22A: case 0x232: return 0x23A;
	case 0x22B: case 0x233: return 0x23B;
	case 0x22C: case 0x234: case 0x271: case 0x275: return 0x23C;
	case 0x22D: case 0x235: case 0x270: case 0x274: return 0x23D;
	default: return -1;
	}
}

int32 IntoFromTakeoff(int32 Id)
{
	// 0x126FC0
	switch (Id)
	{
	case 0x219: case 0x265: return 0x226;
	case 0x21A: case 0x264: return 0x227;
	case 0x21B: return 0x228;
	case 0x21C: return 0x229;
	case 0x21D: return 0x22A;
	case 0x21E: return 0x22B;
	case 0x21F: case 0x263: return 0x22C;
	case 0x220: case 0x262: return 0x22D;
	case 0x266: return 0x272;
	case 0x267: return 0x273;
	case 0x268: return 0x270;
	case 0x269: return 0x271;
	case 0x26A: return 0x276;
	case 0x26B: return 0x277;
	case 0x26C: return 0x274;
	case 0x26D: return 0x275;
	default: return -1;
	}
}

FProceduralSubs EvalProcedural(const FAflLibrary& Lib, int32 Mode, const FAnimDriveInputs& In, int32 BoardClass, int32 OwnClip)
{
	FProceduralSubs R;
	const int32 C = FMath::Clamp(BoardClass, 0, 2);
	const float Cr = float(In.Crouch);
	constexpr double Clk = FProceduralSubs::OnClock;
	switch (Mode)
	{
	case 4:   // 0x160680
	{
		float T = float(In.SteerFollow), B = float(In.Brake);
		const float At = FMath::Abs(T), Ab = FMath::Abs(B);
		if (In.bGoofy)
		{
			T = -T;
			B = -B;
		}
		auto Rd = [&](int32 K) { return Lib.Riding(C, K); };
		R.bHandled = true;
		if (T == 0.0f && Ab == 0.0f && (C != 2 || Cr == 0.0f || Cr == 1.0f))
		{
			Set(R, 0, Rd(4), Clk, 1.0f - Cr);
			Set(R, 1, Rd(5), Clk, Cr);
			R.bClock = true;
			R.bCycle = true;
			return R;
		}
		if (Ab == 0.0f)
		{
			int32 S0, S1;
			float W0, W1;
			if (C == 2)
			{
				if (Cr < 0.5f)
				{
					W0 = 1.0f - (Cr + Cr); W1 = Cr + Cr;
					S0 = T > 0.0f ? Rd(1) : Rd(0); S1 = T > 0.0f ? Rd(9) : Rd(8);
				}
				else
				{
					W0 = 2.0f - (Cr + Cr); W1 = (Cr + Cr) - 1.0f;
					S0 = T > 0.0f ? Rd(9) : Rd(8); S1 = T > 0.0f ? Rd(3) : Rd(2);
				}
			}
			else
			{
				W0 = 1.0f - Cr; W1 = Cr;
				S0 = T > 0.0f ? Rd(1) : Rd(0); S1 = T > 0.0f ? Rd(3) : Rd(2);
			}
			Set(R, 0, S0, At * FramesOf(Lib, S0), W0);
			Set(R, 1, S1, At * FramesOf(Lib, S1), W1);
			return R;
		}
		const int32 S0 = T > 0.0f ? Rd(1) : Rd(0);
		const int32 S1 = B > 0.0f ? Rd(6) : Rd(7);
		Set(R, 0, S0, At * FramesOf(Lib, S0), 1.0f - Ab);
		Set(R, 1, S1, Ab * FramesOf(Lib, S1), Ab);
		return R;
	}
	case 5:   // 0x1605A0
		R.bHandled = true;
		Set(R, 0, Lib.Switch(C, 0), Clk, 1.0f - Cr);
		Set(R, 1, Lib.Switch(C, 1), FProceduralSubs::Sync, Cr);
		R.bClock = true;
		return R;
	case 6:   // 0x161300
		R.bHandled = true;
		Set(R, 0, OwnClip, (1.0f - Cr) * FramesOf(Lib, OwnClip), 1.0f);
		return R;
	case 7:   // 0x161118
	{
		R.bHandled = true;
		const float S = float(In.Steer);
		const int32 G = FMath::Clamp(In.GrindType, 0, 4);
		if (FMath::Abs(S) == 0.0f || G == 1 || G == 2)
		{
			Set(R, 0, Lib.Rail(C, 4, G), Clk, 1.0f - Cr);
			Set(R, 1, Lib.Rail(C, 5, G), Clk, Cr);
			R.bClock = true;
			R.bCycle = true;
			return R;
		}
		const int32 S0 = S > 0.0f ? Lib.Rail(C, 0, G) : Lib.Rail(C, 2, G);
		const int32 S1 = S > 0.0f ? Lib.Rail(C, 1, G) : Lib.Rail(C, 3, G);
		Set(R, 0, S0, FMath::Abs(S) * FramesOf(Lib, S0), 1.0f - Cr);
		Set(R, 1, S1, FMath::Abs(S) * FramesOf(Lib, S1), Cr);
		return R;
	}
	case 8:   // 0x160140
	{
		R.bHandled = true;
		const int32 A = Lib.AirIdle(C);
		Set(R, 0, A, float(In.Ramp1F0) * FramesOf(Lib, A), 1.0f);
		return R;
	}
	case 9:   // 0x1601D0
	{
		R.bHandled = true;
		float X = float(In.AirTurn), Y = float(In.AirTilt);
		if (X == 0.0f && Y == 0.0f)
		{
			Set(R, 0, Lib.AirDir(C, 4), 0.0, 1.0f);
			return R;
		}
		if (In.bGoofy)
		{
			X = -X;
			Y = -Y;
		}
		const float Th = Atan2EE(Y, X);
		const float H = 1.5707964f;
		float F;
		int32 S0, S1;
		if (Th >= H) { F = Th - H; S0 = Lib.AirDir(C, 0); S1 = Lib.AirDir(C, 2); }
		else if (Th >= 0.0f) { F = Th; S0 = Lib.AirDir(C, 2); S1 = Lib.AirDir(C, 1); }
		else if (Th <= -H) { F = Th + H; S0 = Lib.AirDir(C, 0); S1 = Lib.AirDir(C, 3); }
		else { F = Th; S0 = Lib.AirDir(C, 3); S1 = Lib.AirDir(C, 1); }
		const float Mag = FMath::Min(FMath::Max(float(FMath::Sqrt(double(X * X + Y * Y))), 0.0f), 1.0f);
		const float W0 = FMath::Abs(F) * 0.63661975f;
		Set(R, 0, S0, Mag * FramesOf(Lib, S0), W0);
		Set(R, 1, S1, Mag * FramesOf(Lib, S1), 1.0f - W0);
		return R;
	}
	case 10:  // 0x160B20
	{
		R.bHandled = true;
		float X = float(In.PrewindA), Y = float(In.PrewindB);
		const int32 G = FMath::Clamp(In.GrindType, 0, 4);
		auto P = [&](int32 Row) { return Lib.Prewind(C, Row, G); };
		if (X == 0.0f && Y == 0.0f)
		{
			Set(R, 0, P(0), 0.0, 1.0f - Cr);
			Set(R, 1, P(1), Cr * FramesOf(Lib, P(1)), Cr);
			return R;
		}
		if (In.bGoofy)
		{
			X = -X;
			Y = -Y;
		}
		const float Th = Atan2EE(Y, X);
		const float Q = 0.7853982f;
		float F = 0.0f;
		int32 S0 = INDEX_NONE, S1 = INDEX_NONE;
		bool bFound = false;
		const struct { float Lo; int32 A, B; } Up[4] = { { 3.0f * Q, 2, 3 }, { 2.0f * Q, 3, 4 }, { Q, 4, 5 }, { 0.0f, 5, 6 } };
		for (const auto& Sct : Up)
		{
			if (Th >= Sct.Lo) { F = Th - Sct.Lo; S0 = P(Sct.A); S1 = P(Sct.B); bFound = true; break; }
		}
		if (!bFound)
		{
			const struct { float Hi; int32 A, B; } Dn[3] = { { -3.0f * Q, 2, 9 }, { -2.0f * Q, 9, 8 }, { -Q, 8, 7 } };
			for (const auto& Sct : Dn)
			{
				if (Th <= Sct.Hi) { F = Th - Sct.Hi; S0 = P(Sct.A); S1 = P(Sct.B); bFound = true; break; }
			}
		}
		if (!bFound)
		{
			F = Th; S0 = P(7); S1 = P(6);
		}
		const float Mag = FMath::Min(FMath::Max(float(FMath::Sqrt(double(X * X + Y * Y))), 0.0f), 1.0f);
		const float Wa = FMath::Abs(F) * 1.2732395f;
		Set(R, 0, S0, Mag * FramesOf(Lib, S0), Cr * Wa);
		Set(R, 1, S1, Mag * FramesOf(Lib, S1), Cr * (1.0f - Wa));
		Set(R, 2, P(0), 0.0, 1.0f - Cr);
		return R;
	}
	case 11:  // 0x160AF0
		R.bHandled = true;
		Set(R, 0, OwnClip, float(In.Gate) * FramesOf(Lib, OwnClip), 1.0f);
		return R;
	default:  // 0x15A030
		return R;
	}
}

void FAnimController::Init(const FAflLibrary* InLib, int32 InBoardClass, int32 InCharacter)
{
	Lib = InLib;
	BoardClass = FMath::Clamp(InBoardClass, 0, 2);
	Character = InCharacter;
	Reset();
}

void FAnimController::Reset()
{
	bCaptured = false;
	for (int32 C = 0; C < NumChannels; ++C)
	{
		Channels[C].Reset();
		Current[C] = NoAnim;
	}
}

int32 FAnimController::Play(int32 AnimId, double StartOffset, int32 StartMode, double SpeedOverride)
{
	if (!Lib)
	{
		return NoAnim;
	}
	Invalidate();
	const FAnimProps* P = Lib->Props(AnimId);
	// 0x15ADA8, 0x3BFD80
	int32 Variant = 0;
	if (P && P->Lookup == 3)
	{
		const uint32 R = RandomB ? RandomB() : FGameRng::StreamB().Next();
		Variant = int32(R % uint32(Lib->VariantCount(AnimId)));
		LastVariant = Variant;
	}
	const int32 Clip = Lib->Resolve(AnimId, BoardClass, Character, Variant);
	const FAflClip* C = Lib->Clip(Clip);
	if (!P)
	{
		return NoAnim;
	}
	const int32 Ch = FMath::Clamp(P->Channel, 0, NumChannels - 1);
	if (!C)
	{
		// 0x15F060, 0x15FCF8
		Current[Ch] = NoAnim;
		return NoAnim;
	}
	const double Spd = SpeedOverride > 0.0 ? SpeedOverride : Speed;
	const int32 N = C->NFrames;

	FPlayInstance I;
	I.AnimId = AnimId;
	I.Props = *P;
	I.OwnClip = Clip;
	I.NumSubs = 1;
	I.bMirror = bMirror;
	I.Yaw = Yaw;
	I.Mask = Mask;
	I.Speed = Spd;
	I.Weight = 1.0;
	double Start = 0.0;
	if (StartMode >= 1 && StartMode <= 4 && C->Markers[StartMode - 1] >= 0)
	{
		Start = C->Markers[StartMode - 1];
	}
	else if (StartMode == 6)
	{
		Start = N - 1;
	}
	Start = FMath::Max(Start + StartOffset, 0.0);
	I.Subs[0].Clip = Clip;
	I.Subs[0].Frame = Start;
	I.Subs[0].Weight = 1.0;
	if (P->LoopMode == 1 && C->Markers[1] >= 0)
	{
		I.Hold = C->Markers[1];
	}
	else if (P->LoopMode == 2)
	{
		I.Hold = N - 1;
	}
	I.bCycle = P->LoopMode == 3;
	// 0x15A330
	if (P->FadeOut > 0)
	{
		I.AutoFadeStart = N - (P->FadeOut + 1);
		I.AutoFadeRate = (FMath::Abs(float(Spd)) * -30.0f) / (float(P->FadeOut) * 60.0f);
	}
	// 0x159D88, 0x159E38
	TArray<FPlayInstance>& List = Channels[Ch];
	if (P->Blend == 0)
	{
		List.Reset();
	}
	else if (Ch == 1 || List.Num() > 0)
	{
		for (FPlayInstance& E : List)
		{
			// 0x15A760, 0x15A600
			E.bAutoChain = false;
			if (!E.bCycle)
			{
				E.Hold = E.Speed < 0.0 ? 0.0 : double(FMath::Max(ClipFrames(*Lib, E.Subs[0].Clip) - 1, 0));
			}
			E.Rate = (FMath::Abs(float(E.Speed)) * -30.0f) / float(P->Blend * 60);
		}
		I.Weight = 0.0;
		I.Rate = (1.0f - float(I.Weight)) * ((FMath::Abs(float(Spd)) * 30.0f) / float(P->Blend * 60));
	}
	List.Add(I);
	Current[Ch] = AnimId;
	return AnimId;
}

void FAnimController::Clock(FPlayInstance& I) const
{
	// 0x15A030
	const int32 N0 = ClipFrames(*Lib, I.Subs[0].Clip);
	if (I.bCycle && double(N0 - 1) <= I.Subs[0].Frame)
	{
		for (int32 K = 0; K < I.NumSubs; ++K)
		{
			I.Subs[K].Frame = 0.0;
		}
		return;
	}
	const float Adv = (float(I.Speed) * 30.0f) / 60.0f;
	for (int32 K = 0; K < I.NumSubs; ++K)
	{
		FAnimSub& S = I.Subs[K];
		const double Last = double(FMath::Max(ClipFrames(*Lib, S.Clip), 1) - 1);
		const double Old = S.Frame;
		double New = SSXEE::FpuAdd(float(Old), Adv);
		bool bAtEnd = false;
		if (Adv > 0.0)
		{
			if (I.Hold >= 0.0 && I.Hold < New)
			{
				New = I.Hold;
			}
			if (Last < New)
			{
				bAtEnd = Last <= Old;
				New = Last;
			}
		}
		else
		{
			if (I.Hold >= 0.0 && New < I.Hold)
			{
				New = I.Hold;
			}
			if (New < 0.0)
			{
				bAtEnd = Old <= 0.0;
				New = 0.0;
			}
		}
		S.Frame = New;
		if (bAtEnd && !I.bCycle)
		{
			I.bDone = true;
		}
	}
}

void FAnimController::FadeChannel(int32 Channel, int32 B)
{
	Invalidate();
	if (!Lib || Channel < 0 || Channel >= NumChannels)
	{
		return;
	}
	for (FPlayInstance& I : Channels[Channel])
	{
		const int32 N = ClipFrames(*Lib, I.Subs[0].Clip);
		if (!I.bCycle)
		{
			I.Hold = I.Speed < 0.0 ? 0.0 : double(FMath::Max(N - 1, 0));
		}
		int32 Frames = B;
		if (Frames < 0)
		{
			Frames = FMath::Max(N - (int32(I.Subs[0].Frame) + 1), 1);
		}
		I.Rate = (FMath::Abs(float(I.Speed)) * -30.0f) / float(Frames * 60);   // 0x159D88
	}
}

int32 FAnimController::ChainTarget(const FPlayInstance& I) const
{
	switch (I.Props.OnDone)
	{
	case 1: return 0x1FF;
	case 2: return 0x223;
	case 3: return 0x202;
	case 4: return TrickPhaseChain(I.AnimId);
	case 5: return 0x2DF;
	case 6: return 0x2DE;
	case 7: return BoardClass == 2 ? 0x202 : 0x1FF;
	default: return NoAnim;
	}
}

void FAnimController::Update(const FAnimDriveInputs& In)
{
	if (!Lib)
	{
		return;
	}
	for (int32 Ch = 0; Ch < NumChannels; ++Ch)
	{
		TArray<FPlayInstance>& List = Channels[Ch];
		for (FPlayInstance& I : List)
		{
			const FProceduralSubs R = EvalProcedural(*Lib, I.Props.LoopMode, In, BoardClass, I.OwnClip);
			if (R.bHandled)
			{
				I.NumSubs = R.Num;
				for (int32 K = 0; K < R.Num; ++K)
				{
					I.Subs[K].Clip = R.Subs[K].Clip;
					I.Subs[K].Weight = R.Subs[K].Weight;
					if (R.Subs[K].Frame >= 0.0)
					{
						I.Subs[K].Frame = R.Subs[K].Frame;
					}
				}
				I.bCycle = R.bCycle || I.Props.LoopMode == 3;
				if (R.bClock)
				{
					Clock(I);
				}
				for (int32 K = 0; K < R.Num; ++K)
				{
					if (R.Subs[K].Frame == FProceduralSubs::Sync)
					{
						const double N0 = ClipFrames(*Lib, I.Subs[0].Clip) - 1;
						const double N1 = ClipFrames(*Lib, I.Subs[K].Clip) - 1;
						I.Subs[K].Frame = N0 > 0.0 ? I.Subs[0].Frame / N0 * N1 : 0.0;   // 0x159E80
					}
				}
			}
			else
			{
				Clock(I);
			}
			// 0x203A40
			if (int32(I.Subs[0].Frame) >= I.AutoFadeStart)
			{
				I.Rate = I.AutoFadeRate;
			}
			I.Weight = SSXEE::FpuAdd(float(I.Weight), float(I.Rate));
			if (I.Weight >= 1.0)
			{
				I.Weight = 1.0;
				I.Rate = 0.0;
			}
			else if (I.Weight <= 0.0)
			{
				I.Weight = 0.0;
				I.bDone = true;
				I.Rate = 0.0;
			}
		}
	}
	// 0x15F390, 0x15F7C8, 0x129628
	bCaptured = false;
	GatherLive(0, CapturedLayers[0]);
	GatherLive(1, CapturedLayers[1]);
	CapturedId0 = CurrentId(0);
	CapturedCategory0 = CurrentCategory(0);
	bCaptured = true;
	// 0x117190
	SweepDone();
}

void FAnimController::SweepDone()
{
	if (!Lib)
	{
		return;
	}
	// 0x15F700, 0x15F5B8
	TArray<TPair<int32, int32>, TInlineAllocator<12>> Done;
	for (int32 Ch = 0; Ch < NumChannels; ++Ch)
	{
		for (int32 K = Channels[Ch].Num() - 1; K >= 0; --K)
		{
			FPlayInstance& I = Channels[Ch][K];
			if (I.bDone)
			{
				I.Serial = ++SerialCounter;
				Done.Emplace(Ch, I.Serial);
			}
		}
	}
	bInSweep = true;
	for (const TPair<int32, int32>& D : Done)
	{
		TArray<FPlayInstance>& List = Channels[D.Key];
		const int32 K = List.IndexOfByPredicate([&D](const FPlayInstance& I) { return I.Serial == D.Value; });
		if (K == INDEX_NONE)
		{
			continue;
		}
		const FPlayInstance I = List[K];
		List.RemoveAt(K);   // 0x203BD8
		if (!I.bAutoChain)
		{
			continue;
		}
		const int32 PropCh = FMath::Clamp(I.Props.Channel, 0, NumChannels - 1);
		if (I.Props.OnDone == 0)
		{
			Current[PropCh] = NoAnim;   // 0x161640
			continue;
		}
		const int32 Next = ChainTarget(I);
		if (Next == NoAnim)
		{
			continue;
		}
		if (I.Props.OnDone == 1)
		{
			++ChainAction1;   // 0x1613A0
		}
		Current[PropCh] = Play(Next);
	}
	bInSweep = false;
}

void FAnimController::GatherLayers(int32 Part, TArray<FPoseLayer>& Out) const
{
	if (bCaptured && (Part == 0 || Part == 1))
	{
		Out = CapturedLayers[Part];
		return;
	}
	GatherLive(Part, Out);
}

void FAnimController::GatherLive(int32 Part, TArray<FPoseLayer>& Out) const
{
	Out.Reset();
	if (!Lib)
	{
		return;
	}
	for (int32 Ch = 0; Ch < NumChannels; ++Ch)
	{
		// 0x15F7C8, 0x203AE0, 0x202B00
		for (int32 Idx = Channels[Ch].Num() - 1; Idx >= 0; --Idx)
		{
			const FPlayInstance& I = Channels[Ch][Idx];
			if (!(I.Weight > 0.0))
			{
				continue;
			}
			for (int32 K = 0; K < I.NumSubs; ++K)
			{
				const FAnimSub& S = I.Subs[K];
				const FAflClip* C = Lib->Clip(S.Clip);
				if (S.Weight == 0.0 || !C)
				{
					continue;
				}
				if (const FAflBlock* B = C->Block(Part))
				{
					FPoseLayer& L = Out.AddDefaulted_GetRef();
					L.Block = B;
					L.Frame = S.Frame;
					L.Weight = S.Weight * I.Weight;
					L.bMirror = I.bMirror;
					L.Yaw = I.Yaw;
					L.Mask = I.Mask;
				}
			}
		}
	}
}

int32 FAnimController::CurrentCategory(int32 Channel) const
{
	// 0x15EEC0
	const int32 Id = CurrentId(Channel);
	if (Id == NoAnim)
	{
		return 8;
	}
	const FAnimProps* P = Lib ? Lib->Props(Id) : nullptr;
	if (P)
	{
		return P->Category;
	}
	const FPlayInstance* I = Newest(Channel);
	return I ? I->Props.Category : 0;
}

const FPlayInstance* FAnimController::Newest(int32 Channel) const
{
	const TArray<FPlayInstance>& L = Channels[FMath::Clamp(Channel, 0, NumChannels - 1)];
	return L.Num() ? &L.Last() : nullptr;
}

FPlayInstance* FAnimController::Newest(int32 Channel)
{
	TArray<FPlayInstance>& L = Channels[FMath::Clamp(Channel, 0, NumChannels - 1)];
	return L.Num() ? &L.Last() : nullptr;
}

void FAnimController::AddLayerYaw(double A)
{
	Invalidate();
	for (int32 Ch = 0; Ch < NumChannels; ++Ch)
	{
		for (FPlayInstance& I : Channels[Ch])
		{
			I.Yaw += A;
		}
	}
}

void FAnimController::ClearLayerMirrors()
{
	Invalidate();
	for (int32 Ch = 0; Ch < NumChannels; ++Ch)
	{
		for (FPlayInstance& I : Channels[Ch])
		{
			I.bMirror = false;
		}
	}
}

void FAnimController::ReleaseHold(int32 Category)
{
	for (int32 Ch = 0; Ch < NumChannels; ++Ch)
	{
		for (FPlayInstance& I : Channels[Ch])
		{
			if (I.Props.Category == Category)
			{
				I.Hold = -1.0;
			}
		}
	}
}
}
