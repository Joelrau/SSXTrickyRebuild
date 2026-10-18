// 0x157ED8, 0x158030, 0x202B00, 0x202520
#pragma once

#include "CoreMinimal.h"

namespace SSXAnim
{
	// 0x325FA8
	struct FAnimProps
	{
		int32 Category = 0;
		int32 Lookup = 0;
		int32 LoopMode = 0;
		int32 OnDone = 0;
		int32 Channel = 0;
		int32 Blend = 0;   // cross-fade frames (0 = hard cut)
		int32 FadeOut = 0;   // auto fade-out frames
	};

	struct FAflBlock
	{
		int32 Channel = 0;
		TArray<int32> Tracks;
	};

	struct FAflClip
	{
		FString Name;
		int32 NFrames = 0;
		int32 Markers[4] = { -1, -1, -1, -1 };
		TArray<FAflBlock> Blocks;
		TArray<FIntPoint> Events;

		SSXTRICKYREBUILD_API const FAflBlock* Block(int32 Channel) const;
	};

	// Absolute local transforms: Euler (x, y, z) rad with rotation Rz(-z) Ry(-y) Rx(-x); translations cm (Z up), only
	// for bones with Flags bit 0. Clip time = authored frames (30 fps).
	struct FAflBone
	{
		FString Name;
		int32 Parent = -1;   // -1 = root
		int32 ParentFile = -1;
		int32 Flags = 0;   // bit 0: 3 translation tracks, bit 1: 3 rotation tracks
		int32 MirrorBone = 0;   // the partner read when mirrored
		FVector3d Translation = FVector3d::ZeroVector;
		FVector3d Euler = FVector3d::ZeroVector;
		double Extra[9] = {};   // mirror: euler = extra[0..2] + extra[3..5] * euler, translation *= extra[6..8]
	};

	struct FAflSkeleton
	{
		FString Name;
		TArray<FAflBone> Bones;
		// 0x159910
		TArray<FIntPoint> TrackIndex;
		int32 Find(const FString& BoneName) const;
	};

	struct FAflPart
	{
		int32 FileId = 0;
		int32 NumMorphKeys = 0;
		int32 PaletteShift = 0;
		FAflSkeleton Skeleton;
	};

	struct FAflRiderParts
	{
		FString Rider;
		TArray<FAflPart> Parts;
		SSXTRICKYREBUILD_API const FAflPart* Find(int32 FileId) const;
	};

	struct FAflBoardModel
	{
		FString Name;
		FVector3d IK[2];
		double MinZ = 0.0;
	};

	struct FBonePose
	{
		FVector3d Euler = FVector3d::ZeroVector;
		FVector3d Trans = FVector3d::ZeroVector;
	};

	// 0x202930
	struct FPoseLayer
	{
		const FAflBlock* Block = nullptr;
		double Frame = 0.0;
		double Weight = 1.0;
		bool bMirror = false;
		double Yaw = 0.0;
		uint32 Mask = 0xFFFFFFFFu;
	};

	class SSXTRICKYREBUILD_API FAflLibrary
	{
	public:
		static constexpr int32 NumClasses = 3;
		static constexpr int32 NumCharacters = 12;
		static constexpr int32 MaxVariants = 10;

		static FString DefaultPath();
		static const FAflLibrary* Shared();

		bool Load(const FString& Path, FString* OutError = nullptr);
		bool IsLoaded() const { return Clips.Num() > 0; }

		double EvalTrack(int32 Track, double T) const;

		int32 NumClips() const { return Clips.Num(); }
		const FAflClip* Clip(int32 Index) const { return Clips.IsValidIndex(Index) ? &Clips[Index] : nullptr; }
		int32 FindClip(const FString& Name) const;

		const FAnimProps* Props(int32 AnimId) const;
		// 0x15ADA8
		int32 Resolve(int32 AnimId, int32 BoardClass, int32 Character, int32 Variant = 0) const;
		int32 VariantCount(int32 AnimId) const;
		int32 GetNumVariants() const { return NVariants; }

		int32 Riding(int32 C, int32 K) const { return Tab(RidingTab, C * 10 + K); }
		int32 AirDir(int32 C, int32 K) const { return Tab(AirDirTab, C * 5 + K); }
		int32 AirIdle(int32 C) const { return Tab(AirIdleTab, C); }
		int32 Switch(int32 C, int32 K) const { return Tab(SwitchTab, C * 2 + K); }
		int32 Prewind(int32 C, int32 Row, int32 Grind) const { return Tab(PrewindTab, (C * 10 + Row) * 5 + Grind); }
		int32 Rail(int32 C, int32 Row, int32 Grind) const { return Tab(RailTab, (C * 6 + Row) * 5 + Grind); }

		const FAflSkeleton* Skeleton(const FString& RiderId) const;
		const FAflSkeleton* BoardSkeleton() const { return Skeleton(TEXT("board")); }
		const FAflBoardModel* BoardModel(int32 BoardClass, bool bGoofy) const;
		const FAflRiderParts* Parts(const FString& RiderId) const;
		int32 GetVersion() const { return Version; }

	private:
		struct FSegment { uint8 Type = 0; int32 N = 0; int32 Data = 0; };
		int32 Tab(const TArray<int16>& T, int32 I) const { return T.IsValidIndex(I) ? int32(T[I]) : -1; }
		double EvalSegment(const FSegment& S, double T) const;

		int32 FirstId = 0;
		int32 Version = 0;
		int32 NVariants = 3;
		TArray<FAnimProps> PropTable;
		TArray<int16> Lookup;
		TArray<int16> Counts;
		struct FExtraId { FAnimProps Props; int32 Count = 1; TArray<int16> Lookup; };
		TMap<int32, FExtraId> Extras;
		TArray<int16> RidingTab, AirDirTab, AirIdleTab, SwitchTab, PrewindTab, RailTab;
		TArray<FIntPoint> TrackSegs;
		TArray<FSegment> Segments;
		TArray<float> Floats;
		TArray<uint16> Samples;
		TArray<FAflClip> Clips;
		TMap<FString, int32> ClipByName;
		TArray<FAflSkeleton> Skeletons;
		TArray<FAflBoardModel> Boards;
		TArray<FAflRiderParts> RiderParts;
	};

	SSXTRICKYREBUILD_API void SampleBlock(const FAflLibrary& Lib, const FAflBlock& Block, const FAflSkeleton& Skel,
		double Frame, bool bMirror, double Yaw, double TransScale, double BindScale, TArray<FBonePose>& Out);

	SSXTRICKYREBUILD_API FVector3d EulerBlend(const FVector3d& A, const FVector3d& B, double W);

	SSXTRICKYREBUILD_API void BlendLayers(const FAflLibrary& Lib, const FAflSkeleton& Skel, TConstArrayView<FPoseLayer> Layers,
		double TransScale, double BindScale, int32 MaskShift, TArray<FBonePose>& InOutPose);
}
