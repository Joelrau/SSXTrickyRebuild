#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UTexture2D;
class UWorld;

struct SSXTRICKYREBUILD_API FSSXLoadPending
{
	int32 Packages = 0;
	int32 Sublevels = 0;
	int32 Streaming = 0;
	int32 Textures = 0;
	int32 Shaders = 0;
	int32 Psos = 0;
	int32 PoolLimited = 0;
	FString StreamingDetail;

	bool Any() const { return Packages + Sublevels + Streaming + Textures + Shaders + Psos > 0; }
	void KeepMax(const FSSXLoadPending& O);
	FString Describe() const;
};

namespace SSXLoadReady
{
	constexpr double TimeoutSeconds = 20.0;
	constexpr float PostReleaseResidentSeconds = 5.f;

	using FProbe = TFunction<FSSXLoadPending(const UWorld*)>;
	SSXTRICKYREBUILD_API void SetProbeOverride(FProbe Probe);

	/** One streamed texture's wait: Done (resident mips reached the full level), PoolLimited (below full, nothing in flight, and the streamer reports the pool over budget / a global mip bias: done too) or Pending. A texture with a stream request in flight is Pending whatever the pool says. */
	enum class EMipWait : uint8 { Done, PoolLimited, Pending };
	SSXTRICKYREBUILD_API EMipWait ClassifyMips(int32 ResidentMips, int32 FullMips, bool bInFlight, bool bPoolLimited);

	using FTextureList = TArray<TWeakObjectPtr<UTexture2D>>;
	/** The streamable 2D textures used by World's registered primitives (its own materials; not the editor's, other
	 *  worlds' or UI textures). Empty when texture streaming is off (-nullrhi). */
	SSXTRICKYREBUILD_API FTextureList CollectWorldTextures(const UWorld* World);
	SSXTRICKYREBUILD_API int32 ForceTexturesResident(const FTextureList& Textures, float Seconds);
	/** The pending work for World; Streaming counts only Watched (the world's forced textures) that need mips.
	 *  Render-dependent parts are skipped when the app cannot render. */
	SSXTRICKYREBUILD_API FSSXLoadPending Probe(const UWorld* World, const FTextureList& Watched);
}

struct SSXTRICKYREBUILD_API FSSXLoadGate
{
	double Timeout = SSXLoadReady::TimeoutSeconds;
	double StartTime = -1.0;
	double ReleaseTime = -1.0;
	bool bReleased = false;
	bool bTimedOut = false;
	FSSXLoadPending Waited;
	FSSXLoadPending Last;

	void Reset() { *this = FSSXLoadGate(); }
	bool Update(const FSSXLoadPending& Pending, double Now);
	double HeldSeconds() const { return bReleased ? ReleaseTime - StartTime : 0.0; }
};
