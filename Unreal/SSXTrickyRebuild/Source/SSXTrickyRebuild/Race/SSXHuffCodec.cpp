#include "Race/SSXHuffCodec.h"

namespace
{
	constexpr uint32 NoRun = 32000;
	constexpr int32 RunWindow = 0x7531;
	constexpr int32 LengthLimit = 15;       // 0x2426F8
	constexpr int32 NotUsed = 99;           // 0x241FF8

	// 0x242578
	void SmallBucket(uint32 V, int32& Bits, uint32& Base)
	{
		if (V < 4) { Bits = 0; Base = 0; }
		else if (V < 0xC) { Bits = 1; Base = 4; }
		else if (V < 0x1C) { Bits = 2; Base = 0xC; }
		else if (V < 0x3C) { Bits = 3; Base = 0x1C; }
		else if (V < 0x7C) { Bits = 4; Base = 0x3C; }
		else { Bits = 5; Base = 0x7C; }
	}

	// 0x241E88
	struct FHuffBitWriter
	{
		TArray<uint8>& Out;
		uint32 Bits = 0, Acc = 0;
		explicit FHuffBitWriter(TArray<uint8>& InOut) : Out(InOut) {}
		void Put(uint32 V, uint32 N)
		{
			while (N > 16)
			{
				Put(V >> 16, N - 16);
				N = 16;
			}
			Bits += N;
			const uint32 Mask = N >= 32 ? 0xFFFFFFFFu : ((1u << N) - 1u);
			Acc += (V & Mask) << (24 - Bits);
			while (Bits > 7)
			{
				Out.Add(uint8(Acc >> 16));
				Acc <<= 8;
				Bits -= 8;
			}
		}
		// 0x242320
		void Num(uint32 V)
		{
			int32 B = 0;
			uint32 Base = 0;
			if (V < 0xFC) { SmallBucket(V, B, Base); }
			else if (V < 0x1FC) { B = 6; Base = 0xFC; }
			else if (V < 0x3FC) { B = 7; Base = 0x1FC; }
			else if (V < 0x7FC) { B = 8; Base = 0x3FC; }
			else if (V < 0xFFC) { B = 9; Base = 0x7FC; }
			else if (V < 0x1FFC) { B = 10; Base = 0xFFC; }
			else if (V < 0x3FFC) { B = 11; Base = 0x1FFC; }
			else if (V < 0x7FFC) { B = 12; Base = 0x3FFC; }
			else if (V < 0xFFFC) { B = 13; Base = 0x7FFC; }
			else if (V < 0x1FFFC) { B = 14; Base = 0xFFFC; }
			else if (V < 0x3FFFC) { B = 15; Base = 0x1FFFC; }
			else if (V < 0x80000) { B = 16; Base = 0x3FFFC; }
			else if (V < 0x100000) { B = 17; Base = 0x80000; }
			else { B = 18; Base = 0x100000; }
			Put(1, B + 1);
			Put(V - Base, B + 2);
		}
	};

	// 0x241F78
	void Depths(const int32* C0, const int32* C1, int32 N, int32 D, int32* Lens)
	{
		while (N >= 0x100)
		{
			++D;
			Depths(C0, C1, C0[N], D, Lens);
			N = C1[N];
		}
		Lens[N] = D;
	}

	int32 BuildLengths(const uint32* Cnt, int32* Lens)
	{
		uint32 W[260];
		int32 Id[260];
		W[0] = 0;
		Id[0] = 0;
		int32 N = 1;
		for (int32 S = 0; S < 256; ++S)
		{
			Lens[S] = NotUsed;
			if (Cnt[S] != 0) { W[N] = Cnt[S]; Id[N] = S; ++N; }
		}
		int32 M = N - 1;
		const int32 Symbols = M;
		if (N < 3)
		{
			Depths(nullptr, nullptr, Id[M], 1, Lens);
			return Symbols;
		}
		int32 C0[512], C1[512];
		int32 Node = 0x100;
		while (true)
		{
			int32 Lo, Hi;
			uint32 LoV, HiV;
			if (W[M] < W[M - 1]) { Lo = M; LoV = W[M]; Hi = M - 1; HiV = W[M - 1]; }
			else { Lo = M - 1; LoV = W[M - 1]; Hi = M; HiV = W[M]; }
			int32 J = M - 1;
			if (J != 0)
			{
				while (true)
				{
					--J;
					while (HiV < W[J]) { --J; }
					if (J == 0) break;
					const uint32 V = W[J];
					if (V <= LoV) { Hi = Lo; HiV = LoV; Lo = J; LoV = V; }
					else { Hi = J; HiV = V; }
				}
			}
			const int32 IdH = Id[Hi], IdL = Id[Lo];
			W[Hi] = HiV + LoV;
			Id[Hi] = Node;
			C0[Node] = IdH;
			C1[Node] = IdL;
			const uint32 T = W[M];
			W[Lo] = T;
			Id[Lo] = Id[M];
			if (M < 3) break;
			--M;
			++Node;
		}
		Depths(C0, C1, Node, 0, Lens);
		return Symbols;
	}

	// 0x243368
	void ScanRun(const uint8* D, int32 P, int32 End, uint32 Prev, uint32& N, uint32& Last, int32& Next)
	{
		const int32 Limit = FMath::Min(P + RunWindow, End);
		N = 0;
		Last = D[P];
		int32 Q = P + 1;
		if (Q < Limit)
		{
			while (true)
			{
				Last = D[Q];
				++N;
				++Q;
				if (Last != Prev || Q >= Limit) break;
			}
		}
		Next = Q;
	}

	uint32 RunCost(bool bClueUsed, const int32* Lens, uint32 Clue, uint32 N)
	{
		if (!bClueUsed) return NoRun;
		if (N >= 0xFC) return 0x14;
		int32 B = 0;
		uint32 Base = 0;
		SmallBucket(N, B, Base);
		return uint32(Lens[Clue]) + uint32(B) * 2 + 3;
	}

	struct FHuffBitReader
	{
		const uint8* D;
		int32 N;
		int64 Pos;
		bool bOk = true;
		uint32 Bit()
		{
			if ((Pos >> 3) >= N) { bOk = false; return 0; }
			const uint32 B = (D[Pos >> 3] >> (7 - (Pos & 7))) & 1;
			++Pos;
			return B;
		}
		uint32 Bits(int32 K)
		{
			uint32 V = 0;
			for (int32 I = 0; I < K; ++I) { V = (V << 1) | Bit(); }
			return V;
		}
		// 0x240EF8
		uint32 Num()
		{
			if (Bit()) return 4 + Bits(2);
			int32 Z = 1;
			while (bOk && Bit() == 0) { if (++Z > 30) { bOk = false; return 0; } }
			const int32 K = Z + 2;
			return Bits(K) + (1u << K);
		}
	};
}

namespace SSXHuff
{
	void Encode(const uint8* D, int32 End, TArray<uint8>& Out)
	{
		Out.Reset();
		// 0x2426F8
		uint32 Cnt[0x300] = {};
		uint32 Prev = 0x100;
		for (int32 P = 0; P < End;)
		{
			const uint32 B = D[P];
			if (B == Prev)
			{
				uint32 N, Last;
				int32 Next;
				ScanRun(D, P, End, Prev, N, Last, Next);
				if (N < 0xFF) { ++Cnt[0x200 + N]; }
				else { ++Cnt[0x200]; }
				++Cnt[Last];
				++Cnt[0x100 + ((Last - B) & 0xFF)];
				Prev = Last;
				P = Next;
			}
			else
			{
				++Cnt[B];
				++Cnt[0x100 + ((B - Prev) & 0xFF)];
				Prev = B;
				++P;
			}
		}
		if (Cnt[0x200] == 0) Cnt[0x200] = 1;
		uint32 ClueLen = 0, Second = 0, Clue = 0, SecondAt = 0, Least = 0;
		uint32 Gap = 0;
		for (uint32 U = 0;;)
		{
			Gap = 0;
			if (Cnt[U] < Cnt[Least]) Least = U;
			uint32 K = U;
			if (Cnt[U] == 0)
			{
				while (true)
				{
					++K;
					++Gap;
					if (Cnt[K] != 0 || K >= 0x100) break;
				}
			}
			if (!(Gap < Second))
			{
				SecondAt = U;
				Second = Gap;
				if (ClueLen <= Gap)
				{
					Second = ClueLen;
					SecondAt = Clue;
					Clue = U;
					ClueLen = Gap;
				}
			}
			U = K + 1;
			if (U > 0xFF) break;
		}
		(void)SecondAt;
		if (ClueLen == 0) { Clue = Least; ClueLen = 1; }
		if (ClueLen > 1) ClueLen = 1;
		Cnt[Clue] = Cnt[0x200];
		int32 Lens[256];
		BuildLengths(Cnt, Lens);
		uint32 Old[256];
		FMemory::Memcpy(Old, Cnt, sizeof(Old));
		FMemory::Memzero(Cnt, sizeof(Cnt));
		uint32 RunN = Gap;
		Prev = 0x100;
		for (int32 P = 0; P < End;)
		{
			const uint32 B = D[P];
			if (B == Prev)
			{
				uint32 N, Last;
				int32 Next;
				ScanRun(D, P, End, Prev, N, Last, Next);
				RunN = N;
				const uint32 Lit = N * uint32(Lens[B]);
				const uint32 Run = RunCost(Old[Clue] != 0, Lens, Clue, N);
				if (Run < Lit || NoRun < Lit)
				{
					if (Run < NoRun) ++Cnt[Clue];
				}
				else
				{
					Cnt[B] += N;
				}
				++Cnt[Last];
				Prev = Last;
				P = Next;
			}
			else
			{
				++Cnt[B];
				Prev = B;
				++P;
			}
		}
		++Cnt[Clue];
		const int32 Symbols = BuildLengths(Cnt, Lens);
		{
			uint32 Holder = RunN, PrevHolder = Least;
			while (true)
			{
				uint32 Cur = 0, P2 = PrevHolder;
				for (uint32 S = 0; S < 256; ++S)
				{
					if (Cnt[S] != 0 && !(uint32(Lens[S]) < Cur))
					{
						Cur = uint32(Lens[S]);
						P2 = Holder;
						Holder = S;
					}
				}
				PrevHolder = P2;
				if (Cur <= uint32(LengthLimit)) break;
				uint32 U = 0;
				if (Cnt[0] == 0 || uint32(LengthLimit) <= uint32(Lens[0]))
				{
					do { ++U; } while (U <= 0xFF && (Cnt[U] == 0 || uint32(LengthLimit) <= uint32(Lens[U])));
				}
				if (U > 0xFF) break;
				uint32 Chosen = U;
				for (uint32 V = U; V < 256; ++V)
				{
					if (Cnt[V] != 0 && uint32(Lens[V]) < uint32(LengthLimit) && uint32(Lens[Chosen]) < uint32(Lens[V])) { Chosen = V; }
				}
				const int32 L = Lens[Chosen] + 1;
				Lens[Chosen] = L;
				Lens[Holder] = L;
				Lens[PrevHolder] -= 1;
			}
		}
		int32 PerLen[17] = {};
		for (int32 S = 0; S < 256; ++S) { if (Lens[S] < 17) ++PerLen[Lens[S]]; }
		TArray<int32, TInlineAllocator<256>> Order;
		int32 MaxLen = 0;
		for (int32 L = 1; L < 17; ++L)
		{
			if (PerLen[L] == 0) continue;
			MaxLen = L;
			for (int32 S = 0; S < 256; ++S) { if (Lens[S] == L) Order.Add(S); }
		}
		uint32 Codes[256] = {};
		{
			uint32 Code = 0;
			int32 CurLen = 0;
			for (int32 I = 0; I < Symbols && I < Order.Num(); ++I)
			{
				const int32 S = Order[I];
				while (CurLen < Lens[S]) { ++CurLen; Code <<= 1; }
				Codes[S] = Code++;
			}
		}
		// 0x2438C0, 0x243368
		FHuffBitWriter Wr(Out);
		Wr.Put(SSXHuff::TypeHuff, 16);
		Wr.Put(uint32(End), 24);
		Wr.Put(Clue, 8);
		for (int32 L = 1; L <= MaxLen; ++L) { Wr.Num(uint32(PerLen[L])); }
		{
			uint8 Listed[256] = {};
			uint32 U = 0xFF;
			for (int32 I = 0; I < Symbols && I < Order.Num(); ++I)
			{
				int32 Skip = -1;
				do
				{
					U = (U + 1) & 0xFF;
					if (!Listed[U]) ++Skip;
				} while (uint32(Order[I]) != U);
				Listed[U] = 1;
				Wr.Num(uint32(Skip));
			}
		}
		auto Emit = [&](uint32 S)   // 0x242548, 0x2424D0
		{
			Wr.Put(Codes[S], uint32(Lens[S]));
			if (S == Clue)
			{
				Wr.Num(0);
				Wr.Put(S, 9);
			}
		};
		Prev = 0x100;
		for (int32 P = 0; P < End;)
		{
			const uint32 B = D[P];
			if (B == Prev)
			{
				uint32 N, Last;
				int32 Next;
				ScanRun(D, P, End, Prev, N, Last, Next);
				const uint32 Lit = N * uint32(Lens[B]);
				const uint32 Run = RunCost(Cnt[Clue] != 0, Lens, Clue, N);
				if (Run < Lit || NoRun < Lit)
				{
					if (Run < NoRun)
					{
						Wr.Put(Codes[Clue], uint32(Lens[Clue]));
						Wr.Num(N);
					}
				}
				else
				{
					for (uint32 I = 0; I < N; ++I) { Emit(B); }
				}
				Emit(Last);
				Prev = Last;
				P = Next;
			}
			else
			{
				Emit(B);
				Prev = B;
				++P;
			}
		}
		Wr.Put(Codes[Clue], uint32(Lens[Clue]));
		Wr.Num(0);
		Wr.Put(2, 2);
		Wr.Put(0, 7);
	}

	int32 DecodedSize(const uint8* In, int32 InNum)
	{
		if (InNum < 5 || In[1] != 0xFB) return -1;
		int32 P = 2;
		if (In[0] & 1) P += 3;
		if (InNum < P + 3) return -1;
		return (int32(In[P]) << 16) | (int32(In[P + 1]) << 8) | int32(In[P + 2]);
	}

	bool Decode(const uint8* In, int32 InNum, TArray<uint8>& Out, int32* OutConsumed)
	{
		Out.Reset();
		const int32 Size = DecodedSize(In, InNum);
		if (Size < 0) return false;
		if (Size == 0)
		{
			if (OutConsumed) { *OutConsumed = InNum; }
			return true;
		}
		const uint16 Type = uint16(((In[0] << 8) | In[1]) & 0xFEFF);
		FHuffBitReader R{ In, InNum, int64((In[0] & 1) ? 8 : 5) * 8 };
		const uint32 Clue = R.Bits(8);
		int32 Count[33] = {};
		int32 MaxLen = 0, Total = 0;
		{
			uint32 Code = 0;
			for (int32 K = 1;; ++K)
			{
				if (K > 32 || !R.bOk) return false;
				const int32 C = int32(R.Num()) - 4;
				if (C < 0) return false;
				Count[K] = C;
				Total += C;
				Code = Code * 2 + uint32(C);
				MaxLen = K;
				if (C != 0 && ((Code << (16 - FMath::Min(K, 16))) & 0xFFFF) == 0) break;
			}
		}
		if (Total <= 0 || Total > 256) return false;
		uint8 Syms[256];
		{
			uint8 Used[256] = {};
			uint32 U = 0xFF;
			for (int32 I = 0; I < Total; ++I)
			{
				int32 Skip = int32(R.Num()) - 3;
				if (!R.bOk || Skip < 1) return false;
				int32 Guard = 0;
				while (true)
				{
					U = (U + 1) & 0xFF;
					if (!Used[U]) --Skip;
					if (Skip == 0) break;
					if (++Guard > 512) return false;
				}
				Used[U] = 1;
				Syms[I] = uint8(U);
			}
		}
		uint32 First[34] = {};
		int32 Base[34] = {};
		{
			uint32 C = 0;
			int32 Idx = 0;
			for (int32 K = 1; K <= MaxLen; ++K)
			{
				First[K] = C;
				Base[K] = Idx;
				C = (C + uint32(Count[K])) << 1;
				Idx += Count[K];
			}
		}
		Out.Reserve(Size);
		while (true)
		{
			uint32 V = 0;
			int32 K = 0;
			int32 Sym = -1;
			while (K < MaxLen)
			{
				V = (V << 1) | R.Bit();
				++K;
				if (V - First[K] < uint32(Count[K])) { Sym = Syms[Base[K] + int32(V - First[K])]; break; }
			}
			if (!R.bOk || Sym < 0) return false;
			if (uint32(Sym) != Clue)
			{
				Out.Add(uint8(Sym));
				continue;
			}
			const uint32 N = R.Num();
			if (!R.bOk) return false;
			if (N > 4)
			{
				if (Out.Num() == 0 || Out.Num() + int64(N - 4) > Size) return false;
				const uint8 B = Out.Last();
				for (uint32 I = 0; I < N - 4; ++I) { Out.Add(B); }
				continue;
			}
			if (R.Bit()) break;
			Out.Add(uint8(R.Bits(8)));
			if (!R.bOk || Out.Num() > Size) return false;
		}
		if (Type == 0x32FB)
		{
			uint8 A = 0;
			for (uint8& B : Out) { A = uint8(A + B); B = A; }
		}
		else if (Type == 0x34FB)
		{
			uint8 A = 0, C = 0;
			for (uint8& B : Out) { A = uint8(A + B); C = uint8(C + A); B = C; }
		}
		if (OutConsumed) { *OutConsumed = int32((R.Pos + 7) >> 3); }
		return Out.Num() == Size;
	}
}
