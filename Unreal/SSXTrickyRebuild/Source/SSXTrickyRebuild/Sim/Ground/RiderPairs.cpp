#include "Sim/Ground/RiderPairs.h"

namespace SSXRiderHit
{
namespace
{
	constexpr int32 MaxProxies = 16;
}

void FRiderPairs::Reset()
{
	Proxies.Reset();
	Ends.Reset();
	Pairs.Reset();
	Bits.Init(0, MaxProxies * MaxProxies);
	for (int32 A = 0; A < 3; ++A)
	{
		AxisHead[A] = AxisTail[A] = None;
	}
	FreeHead = None;
}

int32& FRiderPairs::BitsOf(int32 PA, int32 PB)
{
	// 0x1503E0, 0x1503B0
	return Bits[FMath::Min(PA, PB) * MaxProxies + FMath::Max(PA, PB)];
}

int32 FRiderPairs::AddProxy()
{
	if (Bits.Num() != MaxProxies * MaxProxies)
	{
		Reset();
	}
	if (Proxies.Num() >= MaxProxies)
	{
		return None;
	}
	const int32 Id = Proxies.AddDefaulted();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		for (int32 K = 0; K < 2; ++K)
		{
			const int32 E = Ends.AddDefaulted();
			Ends[E].Proxy = Id;
			Ends[E].Axis = Axis;
			Ends[E].bMax = uint8(K);
			Proxies[Id].End[K][Axis] = E;
			Append(Axis, E);
		}
	}
	Update(Id);
	return Id;
}

void FRiderPairs::SetBox(int32 Proxy, const FVector3f& Min, const FVector3f& Max)
{
	if (!Proxies.IsValidIndex(Proxy))
	{
		return;
	}
	FProxy& P = Proxies[Proxy];
	for (int32 A = 0; A < 3; ++A)
	{
		P.Min[A] = Min[A];
		P.Max[A] = Max[A];
	}
	Update(Proxy);
}

void FRiderPairs::Move(int32 Proxy, const FVector3f& D)
{
	if (!Proxies.IsValidIndex(Proxy))
	{
		return;
	}
	FProxy& P = Proxies[Proxy];
	for (int32 A = 0; A < 3; ++A)
	{
		P.Min[A] += D[A];
		P.Max[A] += D[A];
	}
	Update(Proxy);
}

bool FRiderPairs::IsPaired(int32 A, int32 B) const
{
	if (!Proxies.IsValidIndex(A) || !Proxies.IsValidIndex(B) || A == B)
	{
		return false;
	}
	return (Bits[FMath::Min(A, B) * MaxProxies + FMath::Max(A, B)] & 7) == 7;
}

float FRiderPairs::Value(int32 E) const
{
	const FEnd& End = Ends[E];
	const FProxy& P = Proxies[End.Proxy];
	return End.bMax ? P.Max[End.Axis] : P.Min[End.Axis];
}

void FRiderPairs::Append(int32 Axis, int32 E)
{
	Ends[E].Prev = AxisTail[Axis];
	Ends[E].Next = None;
	if (AxisTail[Axis] == None)
	{
		AxisHead[Axis] = E;
	}
	else
	{
		Ends[AxisTail[Axis]].Next = E;
	}
	AxisTail[Axis] = E;
}

void FRiderPairs::SwapForward(int32 Axis, int32 E)
{
	const int32 N = Ends[E].Next;
	const int32 NN = Ends[N].Next;
	const int32 P = Ends[E].Prev;
	if (NN == None) { AxisTail[Axis] = E; } else { Ends[NN].Prev = E; }
	if (P == None) { AxisHead[Axis] = N; } else { Ends[P].Next = N; }
	Ends[E].Next = NN;
	Ends[N].Prev = P;
	Ends[E].Prev = N;
	Ends[N].Next = E;
}

void FRiderPairs::Update(int32 Proxy)
{
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		UpdateAxis(Proxy, Axis);
	}
}

void FRiderPairs::UpdateAxis(int32 Proxy, int32 Axis)
{
	// 0x14FFC8
	const int32 Lo = Proxies[Proxy].End[0][Axis];
	const int32 Hi = Proxies[Proxy].End[1][Axis];
	auto Forward = [&](int32 E)
	{
		while (Ends[E].Next != None && Value(Ends[E].Next) < Value(E))
		{
			const int32 N = Ends[E].Next;
			if (Ends[E].bMax != Ends[N].bMax)
			{
				Toggle(Axis, Ends[E].Proxy, Ends[N].Proxy);
			}
			SwapForward(Axis, E);
		}
	};
	auto Back = [&](int32 E)
	{
		while (Ends[E].Prev != None && Value(E) < Value(Ends[E].Prev))
		{
			const int32 P = Ends[E].Prev;
			if (Ends[E].bMax != Ends[P].bMax)
			{
				Toggle(Axis, Ends[P].Proxy, Ends[E].Proxy);
			}
			SwapForward(Axis, P);
		}
	};
	Forward(Hi);
	Back(Lo);
	Back(Hi);
	Forward(Lo);
}

void FRiderPairs::Toggle(int32 Axis, int32 PA, int32 PB)
{
	// 0x150248
	int32& W = BitsOf(PA, PB);
	const int32 Old = W;
	const int32 NewBits = ((1 << Axis) ^ Old) & 7;
	W = (Old & ~7) | NewBits;
	if ((Old & 7) == 7)
	{
		const int32 P = Old >> 3;
		Unlink(Pairs[P].A, P);
		Unlink(Pairs[P].B, P);
		Pairs[P].FreeNext = FreeHead;   // 0x14FD80
		FreeHead = P;
	}
	else if (NewBits == 7)
	{
		int32 P = FreeHead;   // 0x14FDB8, 0x14F720
		if (P == None)
		{
			P = Pairs.AddDefaulted();
		}
		else
		{
			FreeHead = Pairs[P].FreeNext;
		}
		W = (W & 7) | (P << 3);
		Pairs[P].A = PA;
		Pairs[P].B = PB;
		Link(PA, P);
		Link(PB, P);
	}
}

void FRiderPairs::Link(int32 Proxy, int32 P)
{
	FProxy& X = Proxies[Proxy];
	const int32 S = Pairs[P].B == Proxy ? 1 : 0;
	Pairs[P].Prev[S] = X.Tail;
	Pairs[P].Next[S] = None;
	if (X.Tail == None)
	{
		X.Head = P;
	}
	else
	{
		Pairs[X.Tail].Next[Pairs[X.Tail].B == Proxy ? 1 : 0] = P;
	}
	X.Tail = P;
}

void FRiderPairs::Unlink(int32 Proxy, int32 P)
{
	// 0x123FC8
	FProxy& X = Proxies[Proxy];
	const int32 S = Pairs[P].B == Proxy ? 1 : 0;
	const int32 Prev = Pairs[P].Prev[S];
	const int32 Next = Pairs[P].Next[S];
	if (Prev == None)
	{
		X.Head = Next;
	}
	else
	{
		Pairs[Prev].Next[Pairs[Prev].B == Proxy ? 1 : 0] = Next;
	}
	if (Next == None)
	{
		X.Tail = Prev;
	}
	else
	{
		Pairs[Next].Prev[Pairs[Next].B == Proxy ? 1 : 0] = Prev;
	}
}

void FRiderPairs::SortLists()
{
	for (int32 X = 0; X < Proxies.Num(); ++X)
	{
		TArray<int32, TInlineAllocator<16>> List;
		for (int32 P = Proxies[X].Head; P != None; P = Pairs[P].Next[Pairs[P].B == X ? 1 : 0])
		{
			List.Add(P);
		}
		auto Other = [this, X](int32 P) { return Pairs[P].B == X ? Pairs[P].A : Pairs[P].B; };
		List.StableSort([&Other](int32 L, int32 R) { return Other(L) < Other(R); });
		Proxies[X].Head = Proxies[X].Tail = None;
		for (const int32 P : List)
		{
			Link(X, P);
		}
	}
}
}
