#include "Sim/Anim/AflClips.h"
#include "Sim/EEFloat.h"
#include "Sim/Anim/RiderPose.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace SSXAnim
{
namespace
{
	class FReader
	{
	public:
		explicit FReader(const TArray<uint8>& InData) : Data(InData) {}
		bool Ok() const { return !bFail; }
		template <typename T> T Get()
		{
			T V{};
			if (Pos + int64(sizeof(T)) > Data.Num())
			{
				bFail = true;
				return V;
			}
			FMemory::Memcpy(&V, Data.GetData() + Pos, sizeof(T));
			Pos += sizeof(T);
			return V;
		}
		FString Str()
		{
			const int32 N = Get<uint8>();
			if (Pos + N > Data.Num())
			{
				bFail = true;
				return FString();
			}
			FString S;
			S.Reserve(N);
			for (int32 I = 0; I < N; ++I)
			{
				S.AppendChar(TCHAR(Data[Pos + I]));
			}
			Pos += N;
			return S;
		}
	private:
		const TArray<uint8>& Data;
		int64 Pos = 0;
		bool bFail = false;
	};

	FCriticalSection& LibLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}

	const TCHAR* const BoardModelNames[6] = { TEXT("Bx"), TEXT("BxGoofy"), TEXT("Fr"), TEXT("FrGoofy"), TEXT("Al"), TEXT("AlGoofy") };

	double WrapPi(double D)
	{
		return D - UE_DOUBLE_TWO_PI * FMath::FloorToDouble(D / UE_DOUBLE_TWO_PI + 0.5);
	}
}

const FAflBlock* FAflClip::Block(int32 Channel) const
{
	for (const FAflBlock& B : Blocks)
	{
		if (B.Channel == Channel)
		{
			return &B;
		}
	}
	return nullptr;
}

const FAflPart* FAflRiderParts::Find(int32 FileId) const
{
	for (const FAflPart& P : Parts)
	{
		if (P.FileId == FileId)
		{
			return &P;
		}
	}
	return nullptr;
}

int32 FAflSkeleton::Find(const FString& BoneName) const
{
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		if (Bones[I].Name == BoneName)
		{
			return I;
		}
	}
	return INDEX_NONE;
}

FString FAflLibrary::DefaultPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("anim_clips.bin")));
}

const FAflLibrary* FAflLibrary::Shared()
{
	static FAflLibrary* Lib = nullptr;
	static bool bTried = false;
	FScopeLock Lock(&LibLock());
	if (!bTried)
	{
		bTried = true;
		const FString Path = DefaultPath();
		if (FPaths::FileExists(Path))
		{
			FAflLibrary* L = new FAflLibrary();
			FString Err;
			if (L->Load(Path, &Err))
			{
				Lib = L;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[SSX.Anim] %s; riders keep their bind pose"), *Err);
				delete L;
			}
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("[SSX.Anim] %s not found (run tools/export_anim_clips.py); riders keep their bind pose"), *Path);
		}
	}
	return Lib;
}

bool FAflLibrary::Load(const FString& Path, FString* OutError)
{
	auto Fail = [&](const FString& Msg)
	{
		if (OutError) { *OutError = FString::Printf(TEXT("%s: %s"), *Path, *Msg); }
		*this = FAflLibrary();
		return false;
	};
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return Fail(TEXT("cannot read"));
	}
	FReader R(Bytes);
	const uint32 Magic = R.Get<uint32>();
	if (Magic != 0x43585353u)
	{
		return Fail(TEXT("bad magic"));
	}
	Version = int32(R.Get<uint32>());
	if (Version < 1 || Version > 4)
	{
		return Fail(TEXT("unsupported version"));
	}
	FirstId = int32(R.Get<uint32>());
	const int32 NIds = int32(R.Get<uint32>());
	const int32 NClips = int32(R.Get<uint32>());
	const int32 NTracks = int32(R.Get<uint32>());
	const int32 NSkel = int32(R.Get<uint32>());
	const int32 NBoards = int32(R.Get<uint32>());
	if (!R.Ok() || NIds <= 0 || NIds > 4096 || NClips <= 0 || NTracks <= 0)
	{
		return Fail(TEXT("bad header"));
	}
	PropTable.SetNum(NIds);
	for (FAnimProps& P : PropTable)
	{
		P.Category = R.Get<int32>(); P.Lookup = R.Get<int32>(); P.LoopMode = R.Get<int32>(); P.OnDone = R.Get<int32>();
		P.Channel = R.Get<int32>(); P.Blend = R.Get<int32>(); P.FadeOut = R.Get<int32>();
	}
	NVariants = Version >= 3 ? MaxVariants : 3;
	Lookup.SetNum(NIds * NumClasses * NumCharacters * NVariants);
	for (int16& V : Lookup) { V = R.Get<int16>(); }
	Counts.Reset();
	Extras.Reset();
	if (Version >= 3)
	{
		Counts.SetNum(NIds);
		for (int16& V : Counts) { V = R.Get<int16>(); }
		const uint32 NExtra = R.Get<uint32>();
		for (uint32 K = 0; K < NExtra && R.Ok(); ++K)
		{
			const int32 Id = R.Get<int32>();
			FExtraId E;
			FAnimProps& P = E.Props;
			P.Category = R.Get<int32>(); P.Lookup = R.Get<int32>(); P.LoopMode = R.Get<int32>(); P.OnDone = R.Get<int32>();
			P.Channel = R.Get<int32>(); P.Blend = R.Get<int32>(); P.FadeOut = R.Get<int32>();
			E.Count = R.Get<int16>();
			E.Lookup.SetNum(NumClasses * NumCharacters * NVariants);
			for (int16& V : E.Lookup) { V = R.Get<int16>(); }
			Extras.Add(Id, MoveTemp(E));
		}
	}
	auto ReadTab = [&](TArray<int16>& T, int32 N) { T.SetNum(N); for (int16& V : T) { V = R.Get<int16>(); } };
	ReadTab(RidingTab, 3 * 10);
	ReadTab(AirDirTab, 3 * 5);
	ReadTab(AirIdleTab, 3);
	ReadTab(SwitchTab, 3 * 2);
	ReadTab(PrewindTab, 3 * 10 * 5);
	ReadTab(RailTab, 3 * 6 * 5);

	TrackSegs.SetNum(NTracks);
	for (int32 T = 0; T < NTracks && R.Ok(); ++T)
	{
		const int32 NSeg = R.Get<uint16>();
		TrackSegs[T] = FIntPoint(Segments.Num(), NSeg);
		for (int32 K = 0; K < NSeg && R.Ok(); ++K)
		{
			FSegment S;
			S.Type = R.Get<uint8>();
			S.N = R.Get<uint16>();
			if (S.Type <= 3)
			{
				S.Data = Floats.Num();
				for (int32 I = 0; I <= S.Type; ++I) { Floats.Add(R.Get<float>()); }
			}
			else if (S.Type == 4)
			{
				S.Data = Floats.Num();
				for (int32 I = 0; I < S.N; ++I) { Floats.Add(R.Get<float>()); }
			}
			else if (S.Type == 6 || S.Type == 7)
			{
				S.Data = Floats.Num();
				Floats.Add(R.Get<float>());
				Floats.Add(R.Get<float>());
				Floats.Add(float(Samples.Num()));
				for (int32 I = 0; I < S.N; ++I) { Samples.Add(R.Get<uint16>()); }
			}
			else
			{
				return Fail(FString::Printf(TEXT("unknown segment type %d"), S.Type));
			}
			Segments.Add(S);
		}
	}
	Clips.SetNum(NClips);
	for (int32 C = 0; C < NClips && R.Ok(); ++C)
	{
		FAflClip& Clip = Clips[C];
		Clip.Name = R.Str();
		Clip.NFrames = R.Get<uint16>();
		for (int32 K = 0; K < 4; ++K) { Clip.Markers[K] = R.Get<int16>(); }
		const int32 NB = R.Get<uint8>();
		Clip.Blocks.SetNum(NB);
		for (FAflBlock& B : Clip.Blocks)
		{
			B.Channel = R.Get<uint16>();
			const int32 N = R.Get<uint16>();
			B.Tracks.SetNum(N);
			for (int32& T : B.Tracks)
			{
				T = int32(R.Get<uint32>());
				if (T < 0 || T >= NTracks) { return Fail(TEXT("track index out of range")); }
			}
		}
		if (Version >= 2)
		{
			const int32 NE = R.Get<uint8>();
			Clip.Events.SetNum(NE);
			for (FIntPoint& E : Clip.Events)
			{
				E.X = R.Get<uint16>();
				E.Y = R.Get<uint16>();
			}
		}
		if (!ClipByName.Contains(Clip.Name)) { ClipByName.Add(Clip.Name, C); }
	}
	Skeletons.SetNum(NSkel);
	for (FAflSkeleton& S : Skeletons)
	{
		S.Name = R.Str();
		const int32 NB = R.Get<uint8>();
		S.Bones.SetNum(NB);
		int32 K = 0;
		S.TrackIndex.SetNum(NB);
		for (int32 I = 0; I < NB; ++I)
		{
			FAflBone& B = S.Bones[I];
			B.Name = R.Str();
			B.Parent = R.Get<int8>();
			B.Flags = R.Get<uint8>();
			B.MirrorBone = R.Get<int16>();
			B.Translation.X = R.Get<float>(); B.Translation.Y = R.Get<float>(); B.Translation.Z = R.Get<float>();
			B.Euler.X = R.Get<float>(); B.Euler.Y = R.Get<float>(); B.Euler.Z = R.Get<float>();
			for (double& E : B.Extra) { E = R.Get<float>(); }
			// 0x159910
			FIntPoint Idx(-1, -1);
			if (B.Flags & 1) { Idx.X = K; K += 3; }
			if (B.Flags & 2) { Idx.Y = K; K += 3; }
			S.TrackIndex[I] = Idx;
		}
		for (const FAflBone& B : S.Bones)
		{
			if (B.Parent >= NB || B.MirrorBone < 0 || B.MirrorBone >= NB) { return Fail(TEXT("bad skeleton")); }
		}
	}
	Boards.SetNum(NBoards);
	for (FAflBoardModel& B : Boards)
	{
		B.Name = R.Str();
		for (int32 K = 0; K < 2; ++K)
		{
			B.IK[K].X = R.Get<float>(); B.IK[K].Y = R.Get<float>(); B.IK[K].Z = R.Get<float>();
		}
		B.MinZ = R.Get<float>();
	}
	RiderParts.Reset();
	if (Version >= 4)
	{
		const uint32 NRiders = R.Get<uint32>();
		for (uint32 K = 0; K < NRiders && R.Ok(); ++K)
		{
			FAflRiderParts& RP = RiderParts.AddDefaulted_GetRef();
			RP.Rider = R.Str();
			const int32 NP = R.Get<uint8>();
			int32 Shift = 20;
			for (int32 P = 0; P < NP && R.Ok(); ++P)
			{
				FAflPart& Part = RP.Parts.AddDefaulted_GetRef();
				Part.FileId = R.Get<uint8>();
				Part.NumMorphKeys = R.Get<uint8>();
				const int32 NB = R.Get<uint8>();
				Part.Skeleton.Name = FString::Printf(TEXT("%s_part%d"), *RP.Rider, Part.FileId);
				// 0x207198
				Part.PaletteShift = NB > 0 ? Shift : 0;
				Shift += NB;
				Part.Skeleton.Bones.SetNum(NB);
				Part.Skeleton.TrackIndex.SetNum(NB);
				int32 T = 0;
				for (int32 I = 0; I < NB; ++I)
				{
					FAflBone& B = Part.Skeleton.Bones[I];
					B.Name = R.Str();
					B.ParentFile = R.Get<int8>();
					B.Parent = R.Get<int8>();
					B.Flags = R.Get<uint8>();
					B.MirrorBone = R.Get<int16>();
					B.Translation.X = R.Get<float>(); B.Translation.Y = R.Get<float>(); B.Translation.Z = R.Get<float>();
					B.Euler.X = R.Get<float>(); B.Euler.Y = R.Get<float>(); B.Euler.Z = R.Get<float>();
					for (double& E : B.Extra) { E = R.Get<float>(); }
					FIntPoint Idx(-1, -1);
					if (B.Flags & 1) { Idx.X = T; T += 3; }
					if (B.Flags & 2) { Idx.Y = T; T += 3; }
					Part.Skeleton.TrackIndex[I] = Idx;
					if (B.MirrorBone < 0 || B.MirrorBone >= NB || B.Parent < 0 || B.ParentFile < 0)
					{
						return Fail(TEXT("bad part skeleton"));
					}
				}
			}
		}
	}
	if (!R.Ok())
	{
		return Fail(TEXT("truncated"));
	}
	return true;
}

double FAflLibrary::EvalSegment(const FSegment& S, double InT) const
{
	// 0x158030
	using SSXEE::FpuAdd; using SSXEE::FpuSub;
	const float T = float(InT);
	if (S.Type <= 3)
	{
		float V = Floats[S.Data];
		for (int32 K = 1; K <= S.Type; ++K)
		{
			V = FpuAdd(V * T, Floats[S.Data + K]);
		}
		return V;
	}
	const int32 Last = FMath::Max(S.N - 1, 0);
	const int32 I = FMath::Clamp(int32(T), 0, Last);
	const float Fr = FpuSub(T, float(I));
	const int32 J = FMath::Min(I + 1, Last);
	if (S.Type == 4)
	{
		const float A = Floats[S.Data + I];
		return Fr == 0.0f ? A : FpuAdd(FpuSub(Floats[S.Data + J], A) * Fr, A);
	}
	const float Base = Floats[S.Data], Scale = Floats[S.Data + 1];
	const int32 Q0 = int32(Floats[S.Data + 2]);
	float A = float(Samples[Q0 + I]);
	if (Fr != 0.0f)
	{
		A = FpuAdd(FpuSub(float(Samples[Q0 + J]), A) * Fr, A);
	}
	return FpuAdd(A * Scale, Base);
}

double FAflLibrary::EvalTrack(int32 Track, double InT) const
{
	// 0x157ED8
	const FIntPoint TS = TrackSegs[Track];
	float T = float(InT);
	if (TS.Y == 1)
	{
		return EvalSegment(Segments[TS.X], T);
	}
	for (int32 K = 0; K < TS.Y; ++K)
	{
		const FSegment& S = Segments[TS.X + K];
		if (T <= float(S.N - 1))
		{
			return EvalSegment(S, T);
		}
		T = SSXEE::FpuSub(T, float(S.N - 1));
	}
	return 0.0;
}

int32 FAflLibrary::FindClip(const FString& Name) const
{
	const int32* I = ClipByName.Find(Name);
	return I ? *I : INDEX_NONE;
}

const FAnimProps* FAflLibrary::Props(int32 AnimId) const
{
	const int32 I = AnimId - FirstId;
	if (PropTable.IsValidIndex(I))
	{
		return &PropTable[I];
	}
	const FExtraId* E = Extras.Find(AnimId);
	return E ? &E->Props : nullptr;
}

int32 FAflLibrary::VariantCount(int32 AnimId) const
{
	const int32 I = AnimId - FirstId;
	if (PropTable.IsValidIndex(I))
	{
		return Counts.IsValidIndex(I) ? FMath::Max(1, int32(Counts[I])) : 1;
	}
	const FExtraId* E = Extras.Find(AnimId);
	return E ? FMath::Max(1, E->Count) : 1;
}

int32 FAflLibrary::Resolve(int32 AnimId, int32 BoardClass, int32 Character, int32 Variant) const
{
	if (BoardClass < 0 || BoardClass >= NumClasses || Variant < 0 || Variant >= NVariants)
	{
		return INDEX_NONE;
	}
	const int32 Ch = FMath::Clamp(Character, 0, NumCharacters - 1);
	const int32 Cell = (BoardClass * NumCharacters + Ch) * NVariants + Variant;
	const int32 I = AnimId - FirstId;
	if (PropTable.IsValidIndex(I))
	{
		return Lookup[I * NumClasses * NumCharacters * NVariants + Cell];
	}
	const FExtraId* E = Extras.Find(AnimId);
	return E && E->Lookup.IsValidIndex(Cell) ? int32(E->Lookup[Cell]) : INDEX_NONE;
}

const FAflSkeleton* FAflLibrary::Skeleton(const FString& RiderId) const
{
	for (const FAflSkeleton& S : Skeletons)
	{
		if (S.Name == RiderId)
		{
			return &S;
		}
	}
	return nullptr;
}

const FAflRiderParts* FAflLibrary::Parts(const FString& RiderId) const
{
	for (const FAflRiderParts& P : RiderParts)
	{
		if (P.Rider == RiderId)
		{
			return &P;
		}
	}
	return nullptr;
}

const FAflBoardModel* FAflLibrary::BoardModel(int32 BoardClass, bool bGoofy) const
{
	const TCHAR* Name = BoardModelNames[FMath::Clamp(BoardClass, 0, 2) * 2 + (bGoofy ? 1 : 0)];
	for (const FAflBoardModel& B : Boards)
	{
		if (B.Name == Name)
		{
			return &B;
		}
	}
	return nullptr;
}

// 0x202B00
void SampleBlock(const FAflLibrary& Lib, const FAflBlock& Block, const FAflSkeleton& Skel,
	double Frame, bool bMirror, double Yaw, double TransScale, double BindScale, TArray<FBonePose>& Out)
{
	const int32 N = Skel.Bones.Num();
	Out.SetNum(N);
	const int32 NT = Block.Tracks.Num();
	auto Track = [&](int32 K) { return K >= 0 && K < NT ? Lib.EvalTrack(Block.Tracks[K], Frame) : 0.0; };
	for (int32 I = 0; I < N; ++I)
	{
		const FAflBone& B = Skel.Bones[I];
		const int32 Src = bMirror ? B.MirrorBone : I;
		const FIntPoint Idx = Skel.TrackIndex[Src];
		FBonePose& P = Out[I];
		if (B.Flags & 1)
		{
			// 0x250C10, 0x129628
			using SSXEE::FpuAdd; using SSXEE::FpuSub;
			const float Ts = float(TransScale);
			float X = float(Track(Idx.X)) * Ts, Y = float(Track(Idx.X + 1)) * Ts, Z = float(Track(Idx.X + 2)) * Ts;
			if (B.Parent < 0)
			{
				float Sn, Cs;
				SSXAnim::SinCosEE(float(Yaw), Sn, Cs);
				const float Zz = FpuAdd(FpuSub(1.0f, Cs), Cs);
				const float Nx = FpuAdd(FpuAdd(Cs * X, Sn * Y), 0.0f * Z);
				const float Ny = FpuAdd(FpuAdd(-Sn * X, Cs * Y), 0.0f * Z);
				const float Nz = FpuAdd(FpuAdd(0.0f * X, 0.0f * Y), Zz * Z);
				X = Nx; Y = Ny; Z = Nz;
			}
			if (bMirror)
			{
				X = X * float(B.Extra[6]); Y = Y * float(B.Extra[7]); Z = Z * float(B.Extra[8]);
			}
			P.Trans = FVector3d(X, Y, Z);
		}
		else
		{
			P.Trans = B.Translation * BindScale;
		}
		if (B.Flags & 2)
		{
			using SSXEE::FpuAdd;
			float Ex = float(Track(Idx.Y)), Ey = float(Track(Idx.Y + 1)), Ez = float(Track(Idx.Y + 2));
			if (B.Parent < 0)
			{
				Ez = FpuAdd(Ez, float(Yaw));
			}
			if (bMirror)
			{
				Ex = FpuAdd(float(B.Extra[0]), Ex * float(B.Extra[3]));
				Ey = FpuAdd(float(B.Extra[1]), Ey * float(B.Extra[4]));
				Ez = FpuAdd(float(B.Extra[2]), Ez * float(B.Extra[5]));
			}
			P.Euler = FVector3d(Ex, Ey, Ez);
		}
		else
		{
			P.Euler = B.Euler;
		}
	}
}

FVector3d EulerBlend(const FVector3d& A, const FVector3d& B, double W)
{
	// 0x202520, 0x202758
	const float InvTwoPi = 0.15915494f, Pi = 3.14159274f, TwoPi = 6.28318548f;
	using SSXEE::FpuAdd; using SSXEE::FpuSub;
	auto Wrap = [=](float D)
	{
		const float T = FpuAdd(D * InvTwoPi, 0.5f);
		float K = float(int32(T));
		if (T < K) { K = FpuSub(K, 1.0f); }
		return FpuSub(D, K * TwoPi);
	};
	const float Ax = float(A.X), Ay = float(A.Y), Az = float(A.Z), Bx = float(B.X), By = float(B.Y), Bz = float(B.Z);
	const float Dx = FpuSub(Bx, Ax), Dy = FpuSub(By, Ay), Dz = FpuSub(Bz, Az);
	const float D1x = Wrap(Dx), D1y = Wrap(Dy), D1z = Wrap(Dz);
	const float D2x = Wrap(FpuAdd(Dx, Pi)), D2y = Wrap(FpuAdd(FpuSub(-By, Ay), Pi)), D2z = Wrap(FpuAdd(Dz, Pi));
	const float S1 = FpuAdd(FpuAdd(FMath::Abs(D1x), FMath::Abs(D1y)), FMath::Abs(D1z));
	const float S2 = FpuAdd(FpuAdd(FMath::Abs(D2x), FMath::Abs(D2y)), FMath::Abs(D2z));
	const float Wf = float(W);
	const bool bDirect = S1 < S2;
	return FVector3d(FpuAdd(Ax, Wf * (bDirect ? D1x : D2x)), FpuAdd(Ay, Wf * (bDirect ? D1y : D2y)), FpuAdd(Az, Wf * (bDirect ? D1z : D2z)));
}

void BlendLayers(const FAflLibrary& Lib, const FAflSkeleton& Skel, TConstArrayView<FPoseLayer> Layers,
	double TransScale, double BindScale, int32 MaskShift, TArray<FBonePose>& InOutPose)
{
	const int32 N = Skel.Bones.Num();
	if (InOutPose.Num() != N)
	{
		InOutPose.SetNum(N);
		for (int32 I = 0; I < N; ++I)
		{
			InOutPose[I].Euler = Skel.Bones[I].Euler;
			InOutPose[I].Trans = Skel.Bones[I].Translation * BindScale;
		}
	}
	TArray<float> W[2];
	TArray<FBonePose> Acc[2];
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		W[Pass].Init(0.0f, N);
		Acc[Pass].SetNum(N);
	}
	TArray<FBonePose> S;
	// 0x202B00
	for (int32 Pass = 0; Pass < 2; ++Pass)
	for (const FPoseLayer& L : Layers)
	{
		if (!L.Block || (L.Mask != 0xFFFFFFFFu ? 0 : 1) != Pass)
		{
			continue;
		}
		SampleBlock(Lib, *L.Block, Skel, L.Frame, L.bMirror, L.Yaw, TransScale, BindScale, S);
		for (int32 I = 0; I < N; ++I)
		{
			const int32 Bit = I + MaskShift;
			if (Bit < 0 || Bit > 31 || ((L.Mask >> Bit) & 1u) == 0)
			{
				continue;
			}
			float& Wi = W[Pass][I];
			FBonePose& A = Acc[Pass][I];
			if (W[0][I] == 1.0f)
			{
				continue;
			}
			const float Wl = float(L.Weight);
			if (Wi == 0.0f)
			{
				Wi = Wl;
				A = S[I];
				continue;
			}
			const float Tot = SSXEE::FpuAdd(Wi, Wl);
			const float Inv = 1.0f / Tot;
			const float Fo = Wi * Inv, Fn = Wl * Inv;
			Wi = Tot;
			A.Trans = FVector3d(SSXEE::FpuAdd(float(A.Trans.X) * Fo, float(S[I].Trans.X) * Fn), SSXEE::FpuAdd(float(A.Trans.Y) * Fo, float(S[I].Trans.Y) * Fn),
				SSXEE::FpuAdd(float(A.Trans.Z) * Fo, float(S[I].Trans.Z) * Fn));
			A.Euler = EulerBlend(A.Euler, S[I].Euler, Fn);
		}
	}
	for (int32 I = 0; I < N; ++I)
	{
		const double W0 = W[0][I];
		if (W0 == 0.0)
		{
			if (W[1][I] != 0.0f)
			{
				InOutPose[I] = Acc[1][I];
			}
		}
		else if (W0 == 1.0 || W[1][I] == 0.0f)
		{
			InOutPose[I] = Acc[0][I];
		}
		else
		{
			// 0x102F80
			const float W0f = float(W0), W1f = SSXEE::FpuSub(1.0f, W0f);
			const FVector3d& T0 = Acc[0][I].Trans; const FVector3d& T1 = Acc[1][I].Trans;
			InOutPose[I].Trans = FVector3d(SSXEE::FpuAdd(float(T0.X) * W0f, float(T1.X) * W1f), SSXEE::FpuAdd(float(T0.Y) * W0f, float(T1.Y) * W1f),
				SSXEE::FpuAdd(float(T0.Z) * W0f, float(T1.Z) * W1f));
			InOutPose[I].Euler = EulerBlend(Acc[1][I].Euler, Acc[0][I].Euler, W0);
		}
	}
}
}
