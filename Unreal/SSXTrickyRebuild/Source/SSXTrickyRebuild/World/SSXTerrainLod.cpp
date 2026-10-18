#include "World/SSXTerrainLod.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SSXTerrainLod
{
	namespace
	{
		const uint8 LevelTable[4] = { 4, 4, 6, 8 };   // 0x39AFB8
		const int32 Edges[4][2] = { { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 } };

		struct FLattice
		{
			TArray<FIntPoint> UV;
			TMap<FIntPoint, int32> Index;
			FLattice()
			{
				for (int32 I = 0; I < 8; ++I)
				{
					for (int32 J = 0; J < 8; ++J) { Add(FIntPoint(I * 15, J * 15)); }
				}
				for (const int32 N : { 6, 4 })
				{
					const int32 Step = Den / (N - 1);
					for (int32 I = 0; I < N; ++I)
					{
						for (int32 J = 0; J < N; ++J)
						{
							if ((I == 0 || I == N - 1) && (J == 0 || J == N - 1)) { continue; }
							Add(FIntPoint(I * Step, J * Step));
						}
					}
				}
				check(UV.Num() == VertsPerPatch);
			}
			void Add(const FIntPoint& P) { Index.Add(P, UV.Num()); UV.Add(P); }
		};

		const FLattice& Lattice()
		{
			static const FLattice L;
			return L;
		}

		FIntPoint EdgePoint(int32 E, int32 T, bool bInner)
		{
			const int32 Off = bInner ? 15 : 0;
			switch (E)
			{
			case 0: return FIntPoint(T, Off);
			case 1: return FIntPoint(T, Den - Off);
			case 2: return FIntPoint(Off, T);
			default: return FIntPoint(Den - Off, T);
			}
		}

		void GridStrips(const TArray<int32>& Ks, TArray<TArray<FIntPoint>>& Out)
		{
			for (int32 S = 0; S + 1 < Ks.Num(); ++S)
			{
				TArray<FIntPoint>& Strip = Out.AddDefaulted_GetRef();
				for (const int32 U : Ks)
				{
					Strip.Add(FIntPoint(U, Ks[S]));
					Strip.Add(FIntPoint(U, Ks[S + 1]));
				}
			}
		}

		void StitchStrip(int32 E, int32 N, TArray<FIntPoint>& S)
		{
			auto O = [&](int32 K) { return EdgePoint(E, K * (Den / (N - 1)), false); };
			auto I = [&](int32 K) { return EdgePoint(E, K * 15, true); };
			if (N == 8)
			{
				S.Add(O(0));
				for (int32 K = 1; K <= 6; ++K) { S.Add(I(K)); S.Add(O(K)); }
				S.Add(O(7));
			}
			else if (N == 6)
			{
				S.Add(O(0));
				for (int32 K = 1; K <= 5; ++K) { S.Add(I(K)); S.Add(O(K)); }
				S.Add(I(6));
			}
			else
			{
				S.Add(I(1));
				for (int32 K = 0; K <= 3; ++K) { S.Add(O(K)); S.Add(I(K + 2)); }
				S.Add(I(6));
			}
		}

		void StripsUV(const uint8 L[4], TArray<TArray<FIntPoint>>& Out)
		{
			TArray<int32> Ks;
			if (L[0] == L[1] && L[1] == L[2] && L[2] == L[3])
			{
				const int32 N = L[0];
				for (int32 K = 0; K < N; ++K) { Ks.Add(K * (Den / (N - 1))); }
				GridStrips(Ks, Out);
				return;
			}
			for (int32 K = 1; K <= 6; ++K) { Ks.Add(K * 15); }
			GridStrips(Ks, Out);
			for (int32 E = 0; E < 4; ++E) { StitchStrip(E, L[E], Out.AddDefaulted_GetRef()); }
		}
	}

	void EdgeLevels(const FVector3f C[4], const FVector3f& Eye, uint8 Out[4])
	{
		const float A2 = RadiusA * RadiusA + RadiusA * RadiusA;
		const float K = -2.0f / ((RadiusB * RadiusB + RadiusB * RadiusB) - A2);
		const float C0 = 3.0f - K * A2;
		float D[4];
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector3f V = Eye - C[I];
			D[I] = V.X * V.X + V.Y * V.Y + V.Z * V.Z;
		}
		for (int32 E = 0; E < 4; ++E)
		{
			const float L = FMath::Clamp(C0 + K * (D[Edges[E][0]] + D[Edges[E][1]]), 1.0f, 3.0f);
			Out[E] = LevelTable[int32(L)];
		}
	}

	int32 SignatureIndex(const uint8 Levels[4])
	{
		int32 S = 0;
		for (int32 E = 3; E >= 0; --E) { S = S * 3 + (Levels[E] - 4) / 2; }
		return S;
	}

	void SignatureLevels(int32 Signature, uint8 Out[4])
	{
		for (int32 E = 0; E < 4; ++E) { Out[E] = uint8(4 + 2 * (Signature % 3)); Signature /= 3; }
	}

	FIntPoint LatticeUV(int32 Index) { return Lattice().UV[Index]; }

	int32 LatticeIndex(const FIntPoint& UV105)
	{
		const int32* I = Lattice().Index.Find(UV105);
		return I ? *I : INDEX_NONE;
	}

	void PatchStrips(const uint8 Levels[4], TArray<TArray<int32>>& Out)
	{
		TArray<TArray<FIntPoint>> UV;
		StripsUV(Levels, UV);
		Out.Reset();
		for (const TArray<FIntPoint>& S : UV)
		{
			TArray<int32>& Dst = Out.AddDefaulted_GetRef();
			for (const FIntPoint& P : S) { Dst.Add(LatticeIndex(P)); }
		}
	}

	const TArray<uint32>& Template(int32 Signature)
	{
		static const TArray<TArray<uint32>> All = []()
		{
			TArray<TArray<uint32>> R;
			for (int32 S = 0; S < NumSignatures; ++S)
			{
				uint8 L[4];
				SignatureLevels(S, L);
				TArray<TArray<FIntPoint>> Strips;
				StripsUV(L, Strips);
				TArray<uint32>& T = R.AddDefaulted_GetRef();
				for (const TArray<FIntPoint>& St : Strips)
				{
					for (int32 J = 0; J + 2 < St.Num(); ++J)
					{
						FIntPoint A = St[J], B = St[J + 1], C = St[J + 2];
						const int64 Cross = int64(B.X - A.X) * (C.Y - A.Y) - int64(B.Y - A.Y) * (C.X - A.X);
						if (Cross == 0) { continue; }
						if (Cross < 0) { Swap(B, C); }
						T.Add(uint32(LatticeIndex(A)));
						T.Add(uint32(LatticeIndex(B)));
						T.Add(uint32(LatticeIndex(C)));
					}
				}
				check(T.Num() <= MaxTris * 3);
			}
			return R;
		}();
		return All[Signature];
	}
}

FString FSSXTerrainLodData::DefaultPath(const FString& Stem)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), Stem + TEXT(".terrainlod.bin"));
}

bool FSSXTerrainLodData::Load(const FString& Path, FString* OutError)
{
	auto Fail = [&](const FString& Msg) { if (OutError) { *OutError = Msg; } Patches.Reset(); Vertices.Reset(); return false; };
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
	{
		return Fail(FString::Printf(TEXT("%s missing (tools/export_terrain_lod.py)"), *Path));
	}
	constexpr int32 HeaderSize = 32, PatchSize = 68, VertexSize = 32;
	if (Bytes.Num() < HeaderSize || FMemory::Memcmp(Bytes.GetData(), "SSXL", 4) != 0)
	{
		return Fail(FString::Printf(TEXT("%s: not a terrain LOD file"), *Path));
	}
	auto U32 = [&](int32 O) { uint32 V; FMemory::Memcpy(&V, &Bytes[O], 4); return V; };
	auto F32 = [&](int32 O) { float V; FMemory::Memcpy(&V, &Bytes[O], 4); return V; };
	const uint32 Version = U32(4), Count = U32(8), Per = U32(12);
	if (Version != 1 || Per != uint32(SSXTerrainLod::VertsPerPatch)
		|| int64(Bytes.Num()) != HeaderSize + int64(Count) * (PatchSize + int64(Per) * VertexSize))
	{
		return Fail(FString::Printf(TEXT("%s: version %u / %u vertices per patch / size mismatch (re-run tools/export_terrain_lod.py)"), *Path, Version, Per));
	}
	Patches.SetNum(Count);
	for (uint32 P = 0; P < Count; ++P)
	{
		const int32 O = HeaderSize + P * PatchSize;
		FPatch& Out = Patches[P];
		Out.PbdIndex = int32(U32(O));
		int16 Tex;
		uint16 Flags;
		FMemory::Memcpy(&Tex, &Bytes[O + 4], 2);
		FMemory::Memcpy(&Flags, &Bytes[O + 6], 2);
		Out.Texture = Tex;
		Out.bShowOff = (Flags & 1) != 0;
		for (int32 C = 0; C < 4; ++C)
		{
			Out.Corners[C] = FVector3f(F32(O + 8 + 12 * C), F32(O + 12 + 12 * C), F32(O + 16 + 12 * C));
		}
		Out.LightmapOrigin = FVector2f(F32(O + 56), F32(O + 60));
	}
	Vertices.SetNum(int32(Count) * SSXTerrainLod::VertsPerPatch);
	const int32 V0 = HeaderSize + Count * PatchSize;
	for (int32 V = 0; V < Vertices.Num(); ++V)
	{
		const int32 O = V0 + V * VertexSize;
		FVertex& Out = Vertices[V];
		Out.Position = FVector3f(F32(O), F32(O + 4), F32(O + 8));
		FMemory::Memcpy(Out.Normal, &Bytes[O + 12], 4);
		Out.Uv0 = FVector2f(F32(O + 16), F32(O + 20));
		Out.Uv1 = FVector2f(F32(O + 24), F32(O + 28));
	}
	return true;
}
