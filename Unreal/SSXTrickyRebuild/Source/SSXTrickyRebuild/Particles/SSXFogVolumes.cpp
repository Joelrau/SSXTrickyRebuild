#include "Particles/SSXFogVolumes.h"

#include "Algo/StableSort.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SSXFog
{
	double FarFactor(double D)
	{
		const float Df = float(D);
		if (!(Df < 20000.182f))
		{
			return -1.0;
		}
		return Df < 12500.374f ? 1.0 : double((20000.182f - Df) / 7499.8076f);
	}

	double NearFactor(double W)
	{
		const float Wf = float(W);
		if (!(500.0f < Wf))
		{
			return -1.0;
		}
		if (Wf < 2601.9497f)
		{
			const float T = (Wf - 500.0f) / 2101.9497f;
			return double(T * T);
		}
		return 1.0;
	}

	int32 DepthKey(double W)
	{
		return FMath::Clamp(int32(FMath::Max(W, 0.0) * 1023.0 / DepthKeyFar), 0, 1023);
	}

	namespace
	{
		// 0x2FB5C8 / 0x1CAD28
		int32 Cmp(const FSortKey& A, const FSortKey& B) { return A.W < B.W ? 1 : 0; }

		FSortKey* Med3(FSortKey* A, FSortKey* B, FSortKey* C)
		{
			return Cmp(*A, *B) < 0 ? (Cmp(*B, *C) < 0 ? B : (Cmp(*A, *C) < 0 ? C : A))
			                       : (Cmp(*B, *C) > 0 ? B : (Cmp(*A, *C) < 0 ? A : C));
		}

		void VecSwap(FSortKey* A, FSortKey* B, int32 N)
		{
			for (int32 K = 0; K < N; ++K) { Swap(A[K], B[K]); }
		}

		void InsertionSort(FSortKey* A, int32 N)
		{
			for (FSortKey* Pm = A + 1; Pm < A + N; ++Pm)
			{
				for (FSortKey* Pl = Pm; Pl > A && Cmp(Pl[-1], *Pl) > 0; --Pl)
				{
					Swap(Pl[0], Pl[-1]);
				}
			}
		}

		void Qsort(FSortKey* A, int32 N)
		{
			for (;;)
			{
				if (N < 7)
				{
					InsertionSort(A, N);
					return;
				}
				FSortKey* Pm = A + N / 2;
				if (N > 7)
				{
					FSortKey* Pl = A;
					FSortKey* Pn = A + N - 1;
					if (N > 40)
					{
						const int32 D = N / 8;
						Pl = Med3(Pl, Pl + D, Pl + 2 * D);
						Pm = Med3(Pm - D, Pm, Pm + D);
						Pn = Med3(Pn - 2 * D, Pn - D, Pn);
					}
					Pm = Med3(Pl, Pm, Pn);
				}
				Swap(*A, *Pm);
				bool bSwapped = false;
				FSortKey* Pa = A + 1;
				FSortKey* Pb = Pa;
				FSortKey* Pc = A + N - 1;
				FSortKey* Pd = Pc;
				for (;;)
				{
					int32 R;
					while (Pb <= Pc && (R = Cmp(*Pb, *A)) <= 0)
					{
						if (R == 0) { bSwapped = true; Swap(*Pa, *Pb); ++Pa; }
						++Pb;
					}
					while (Pb <= Pc && (R = Cmp(*Pc, *A)) >= 0)
					{
						if (R == 0) { bSwapped = true; Swap(*Pc, *Pd); --Pd; }
						--Pc;
					}
					if (Pb > Pc)
					{
						break;
					}
					Swap(*Pb, *Pc);
					bSwapped = true;
					++Pb;
					--Pc;
				}
				if (!bSwapped)
				{
					InsertionSort(A, N);
					return;
				}
				FSortKey* const End = A + N;
				int32 R = int32(FMath::Min(Pa - A, Pb - Pa));
				VecSwap(A, Pb - R, R);
				R = int32(FMath::Min(Pd - Pc, End - Pd - 1));
				VecSwap(Pb, End - R, R);
				if ((R = int32(Pb - Pa)) > 1)
				{
					Qsort(A, R);
				}
				if ((R = int32(Pd - Pc)) > 1)
				{
					A = End - R;
					N = R;
					continue;
				}
				return;
			}
		}
	}

	void EeQsortFogKeys(TArrayView<FSortKey> Keys)
	{
		if (Keys.Num() > 1)
		{
			Qsort(Keys.GetData(), Keys.Num());
		}
	}

	FString FFogData::DefaultPath(const FString& Stem)
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Stem.ToLower() + TEXT(".fog.bin"));
	}

	int32 FClipView::ClipFlags(const FVector& P) const
	{
		const FVector3f D = FVector3f(P - Pos);
		const float W = FVector3f::DotProduct(D, FVector3f(Fwd));
		const float X = FVector3f::DotProduct(D, FVector3f(Right)) * float(CotH);
		const float Y = FVector3f::DotProduct(D, FVector3f(Up)) * float(CotV);
		const float F = float(Far), N = float(Near);
		const float Z = W * ((F + N) / (F - N)) + (-2.f * F * N / (F - N));
		const float Aw = FMath::Abs(W);
		int32 Fl = 0;
		const float C[3] = { X, Y, Z };
		for (int32 K = 0; K < 3; ++K)
		{
			if (C[K] > Aw) { Fl |= 1 << (2 * K); }
			if (C[K] < -Aw) { Fl |= 1 << (2 * K + 1); }
		}
		return Fl;
	}

	int32 FClipView::BoxTest(const FVector& Lo, const FVector& Hi) const
	{
		int32 Or = 0, And = 63;
		for (int32 I = 0; I < 8; ++I)
		{
			const int32 Fl = ClipFlags(FVector(I & 4 ? Hi.X : Lo.X, I & 2 ? Hi.Y : Lo.Y, I & 1 ? Hi.Z : Lo.Z));
			Or |= Fl;
			And &= Fl;
		}
		return And ? 1 : (Or ? 2 : 0);
	}

	void FFogData::VisibleCells(const FClipView& View, int32 Radius, TArray<FVisible>& Out) const
	{
		Out.Reset();
		if (!HasGrid())
		{
			return;
		}
		// FUN_00260278
		const float Cs = float(CellSize);
		const int32 Cx = int32((float(View.Pos.X) - float(GridMinX) + Cs) / Cs) - 1;
		const int32 Cy = int32((float(View.Pos.Y) - float(GridMinY) + Cs) / Cs) - 1;
		const int32 X0 = FMath::Max(Cx - Radius, 0), X1 = FMath::Min(FMath::Max(Cx + Radius, 0), NX - 1);
		const int32 Y0 = FMath::Max(Cy - Radius, 0), Y1 = FMath::Min(FMath::Max(Cy + Radius, 0), NY - 1);
		for (int32 Ix = X0; Ix <= X1; ++Ix)
		{
			for (int32 Iy = Y0; Iy <= Y1 && Out.Num() < 162; ++Iy)
			{
				const int32* Slot = CellOfIndex.Find(Iy * NX + Ix);
				if (!Slot)
				{
					continue;
				}
				// FUN_00260458
				const FCell& C = Cells[*Slot];
				FVisible& V = Out.AddDefaulted_GetRef();
				V.Cell = *Slot;
				const int32 R = View.BoxTest(C.Lo, C.Hi);
				if (R == 0)
				{
					V.Vis = V.In = 0xFFFF;
				}
				else if (R == 2)
				{
					for (int32 K = 0; K < 16; ++K)
					{
						if (C.Subs[K].bEmpty) { continue; }
						const int32 S = View.BoxTest(C.Subs[K].Lo, C.Subs[K].Hi);
						if (S == 0) { V.Vis |= 1 << K; V.In |= 1 << K; }
						else if (S == 2) { V.Vis |= 1 << K; }
					}
				}
			}
		}
	}

	void FFogData::SubmissionOrder(const FClipView& View, int32 Radius, TArray<int32>& Out) const
	{
		Out.Reset();
		TArray<FVisible> Vis;
		VisibleCells(View, Radius, Vis);
		for (const FVisible& V : Vis)
		{
			const FCell& C = Cells[V.Cell];
			for (int32 K = 0; K < 16; ++K)
			{
				if (!((V.Vis >> K) & 1))
				{
					continue;
				}
				for (const int32 Ii : C.Subs[K].Instances)
				{
					if (!Instances.IsValidIndex(Ii))
					{
						continue;
					}
					if (!((V.In >> K) & 1) && View.BoxTest(Instances[Ii].BoxMin, Instances[Ii].BoxMax) == 1)
					{
						continue;
					}
					Out.Add(Ii);
				}
			}
		}
	}

	bool FFogData::Load(const FString& Path, FString* OutError)
	{
		Models.Reset();
		Instances.Reset();
		Cells.Reset();
		CellOfIndex.Reset();
		NX = NY = 0;
		TArray<uint8> B;
		auto Fail = [&](const TCHAR* Why)
		{
			if (OutError) { *OutError = FString::Printf(TEXT("%s: %s"), *Path, Why); }
			Models.Reset(); Instances.Reset(); Cells.Reset(); CellOfIndex.Reset(); NX = NY = 0;
			return false;
		};
		if (!FFileHelper::LoadFileToArray(B, *Path, FILEREAD_Silent))
		{
			return Fail(TEXT("not found (tools/export_fog_volumes.py)"));
		}
		int64 O = 0;
		auto Need = [&](int64 N) { return O + N <= B.Num(); };
		auto U32 = [&]() { uint32 V; FMemory::Memcpy(&V, B.GetData() + O, 4); O += 4; return V; };
		auto F32 = [&]() { float V; FMemory::Memcpy(&V, B.GetData() + O, 4); O += 4; return double(V); };
		auto Vec = [&]() { const double X = F32(); const double Y = F32(); const double Z = F32(); return FVector(X, Y, Z); };
		if (!Need(16) || FMemory::Memcmp(B.GetData(), "SSXG", 4) != 0)
		{
			return Fail(TEXT("bad magic"));
		}
		O = 4;
		const uint32 Version = U32();
		if (Version != 1 && Version != 2)
		{
			return Fail(TEXT("version"));
		}
		const uint32 NM = U32(), NI = U32();
		for (uint32 M = 0; M < NM; ++M)
		{
			if (!Need(4)) { return Fail(TEXT("truncated")); }
			const uint32 NP = U32();
			if (!Need(int64(NP) * 28)) { return Fail(TEXT("truncated")); }
			TArray<FPoint>& Pts = Models.AddDefaulted_GetRef();
			for (uint32 K = 0; K < NP; ++K)
			{
				FPoint& P = Pts.AddDefaulted_GetRef();
				P.Pos = Vec();
				P.Rgb = Vec();
				P.Radius = F32();
			}
		}
		if (!Need(int64(NI) * 92))
		{
			return Fail(TEXT("truncated"));
		}
		for (uint32 I = 0; I < NI; ++I)
		{
			FInstance& In = Instances.AddDefaulted_GetRef();
			In.X = Vec(); F32();
			In.Y = Vec(); F32();
			In.Z = Vec(); F32();
			In.T = Vec(); F32();
			In.Model = int32(U32());
			In.BoxMin = Vec();
			In.BoxMax = Vec();
			if (!Models.IsValidIndex(In.Model))
			{
				return Fail(TEXT("model index"));
			}
		}
		if (Version >= 2)
		{
			if (!Need(24)) { return Fail(TEXT("truncated grid")); }
			GridMinX = F32();
			GridMinY = F32();
			CellSize = F32();
			NX = int32(U32());
			NY = int32(U32());
			const uint32 NC = U32();
			for (uint32 C = 0; C < NC; ++C)
			{
				if (!Need(28)) { return Fail(TEXT("truncated cell")); }
				FCell& Cell = Cells.AddDefaulted_GetRef();
				Cell.Index = int32(U32());
				Cell.Lo = Vec();
				Cell.Hi = Vec();
				for (int32 K = 0; K < 16; ++K)
				{
					if (!Need(27)) { return Fail(TEXT("truncated sub-cell")); }
					FSubCell& S = Cell.Subs[K];
					S.Lo = Vec();
					S.Hi = Vec();
					S.bEmpty = B[O] != 0;
					uint16 N;
					FMemory::Memcpy(&N, B.GetData() + O + 1, 2);
					O += 3;
					if (!Need(int64(N) * 4)) { return Fail(TEXT("truncated list")); }
					for (uint32 I = 0; I < N; ++I) { S.Instances.Add(int32(U32())); }
				}
				CellOfIndex.Add(Cell.Index, Cells.Num() - 1);
			}
		}
		return O == B.Num() ? true : Fail(TEXT("trailing bytes"));
	}

	int32 FFogData::NumPuffs() const
	{
		int32 N = 0;
		for (const FInstance& I : Instances) { N += Models[I.Model].Num(); }
		return N;
	}

	void FFogData::Gather(const FVector& CamPos, const FVector& CamForward, TArray<FPuff>& Out) const
	{
		TArray<int32> Order;
		for (int32 Ii = 0; Ii < Instances.Num(); ++Ii) { Order.Add(Ii); }
		GatherOrder(CamPos, CamForward, Order, Out);
	}

	void FFogData::Gather(const FClipView& View, int32 Radius, TArray<FPuff>& Out) const
	{
		if (!HasGrid())
		{
			Gather(View.Pos, View.Fwd, Out);
			return;
		}
		TArray<int32> Order;
		SubmissionOrder(View, Radius, Order);
		GatherOrder(View.Pos, View.Fwd, Order, Out);
	}

	void FFogData::GatherOrder(const FVector& CamPos, const FVector& CamForward, const TArray<int32>& Order, TArray<FPuff>& Out) const
	{
		struct FBatch { int32 Key = 0; int32 First = 0; int32 Num = 0; };
		TArray<FBatch> Batches;
		TArray<FPuff> Puffs;
		TArray<FSortKey> Keys;
		for (const int32 Ii : Order)
		{
			const FInstance& I = Instances[Ii];
			const double F = FarFactor(FVector::Dist(CamPos, I.BoxCentre()));
			if (F < 0.0)
			{
				continue;
			}
			const TArray<FPoint>& Pts = Models[I.Model];
			Keys.Reset();
			for (int32 K = 0; K < Pts.Num(); ++K)
			{
				const double W = FVector::DotProduct(I.Point(Pts[K].Pos) - CamPos, CamForward);
				Keys.Add({ K & 0xFFFF, int32(float(W)) });
			}
			EeQsortFogKeys(Keys);
			FBatch& B = Batches.AddDefaulted_GetRef();
			B.Key = DepthKey(FVector::DotProduct(I.T - CamPos, CamForward));
			B.First = Puffs.Num();
			for (const FSortKey& Sk : Keys)
			{
				const double N = NearFactor(double(Sk.W));
				if (N < 0.0)
				{
					continue;
				}
				const FPoint& P = Pts[Sk.Index];
				// RGBAQ bytes: (int) of colour x 128 and of 128 x t x t x f (single precision, in the packet's order)
				const float Wf = float(Sk.W);
				const float Ff = float(F);
				float Af = 128.0f * Ff;
				if (Wf < 2601.9497f)
				{
					const float T = (Wf - 500.0f) / 2101.9497f;
					Af = 128.0f * T * T * Ff;
				}
				const int32 A = int32(Af);
				if (A <= 0)
				{
					continue;
				}
				FPuff& Pf = Puffs.AddDefaulted_GetRef();
				Pf.Pos = I.Point(P.Pos);
				Pf.Half = P.Radius;
				Pf.Rgba = FVector4(int32(float(P.Rgb.X) * 128.0f), int32(float(P.Rgb.Y) * 128.0f), int32(float(P.Rgb.Z) * 128.0f), A);
				Pf.Instance = Ii;
			}
			B.Num = Puffs.Num() - B.First;
		}
		Algo::StableSort(Batches, [](const FBatch& A, const FBatch& B) { return A.Key > B.Key; });
		for (const FBatch& B : Batches)
		{
			Out.Append(Puffs.GetData() + B.First, B.Num);
		}
	}
}
