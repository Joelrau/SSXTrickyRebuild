// 0x347208 / FUN_0026C370
#pragma once

#include "CoreMinimal.h"

// One sample of a camera: game space (cm, Z up), rotation in degrees, focal length in mm.
struct SSXTRICKYREBUILD_API FSSXFeCamPose
{
	FVector Pos = FVector::ZeroVector;
	FVector RotDeg = FVector::ZeroVector;
	float Focal = 25.f;

	// 0x26AC98
	float FovDeg() const { return FMath::RadiansToDegrees(2.f * FMath::Atan(8.997951f / FMath::Max(Focal, 0.01f))); }
	/** Horizontal FOV of the rendered frame, degrees. cFE3DCameraScripted::vfunc_11 0x26AC98 passes FovDeg (radians) to SetProjection (vtable +0xAC), whose argument is the HALF angle: m00 = cot(arg), m11 = cot(arg) * 4/3. So the full horizontal FOV is 2 * FovDeg, at 4:3. */
	float HorizontalFovDeg() const { return 2.f * FovDeg(); }
	// 0x26A4B8
	static constexpr float NearClip = 20.f;   // cm
	static constexpr float FarClip = 20000.f;   // cm
	/** View direction and up in game space: R = Rz(rotZ) * Rx(rotX) applied to the Maya camera axes (look -Z, up +Y); rotX - 90 deg converts Maya Y-up to game Z-up. rotY is 0 in all FE cameras. */
	void Axes(FVector& OutForward, FVector& OutUp) const;
	void ToUnreal(FVector& OutLoc, FRotator& OutRot) const;
};

struct SSXTRICKYREBUILD_API FSSXFeCamera
{
	FString Name;
	float Duration = 0.f;
	TMap<FName, TArray<TArray<float, TFixedAllocator<6>>>> Tracks;
	FVector RestPos = FVector::ZeroVector;
	FVector RestRotRad = FVector::ZeroVector;
	float RestFocal = 25.f;

	FSSXFeCamPose Sample(float T) const;
};

class SSXTRICKYREBUILD_API FSSXFeCameraSet
{
public:
	bool Load(const FString& JsonPath);
	const FSSXFeCamera* Find(const FString& Name) const;
	bool IsLoaded() const { return Cameras.Num() > 0; }
	TArray<FSSXFeCamera> Cameras;
};

/** Plays camera scripts: a camera name and a time range (seconds; the script table counts 30 fps frames, e.g. cycleRight segments are 16 frames per slot). Reverse playback = end < start. */
class SSXTRICKYREBUILD_API FSSXFeCameraDirector
{
public:
	void Play(const FString& Camera, float StartSec, float EndSec);
	void PlayFull(const FString& Camera, bool bReverse = false);
	void Hold(const FString& Camera, float AtSec) { Play(Camera, AtSec, AtSec); }
	/** Starts the script when the running one has ended (now when nothing runs). A new Play drops it. Used for the state opened behind a closed-wait-camera phase: the next state's enter runs after the swing. */
	void Then(const FString& Camera, float StartSec, float EndSec);
	void Tick(float Dt);
	bool IsPlaying() const { return Remaining() > 0.f; }
	float Remaining() const { return FMath::Abs(End - Time); }
	bool GetPose(const FSSXFeCameraSet& Set, FSSXFeCamPose& Out) const;

	FString Current;
	float Start = 0.f, End = 0.f, Time = 0.f;
	/** Script speed (script +0x10, 1 at every start). cFE3DState::vfunc_18 multiplies it by 100 when X / Start is pressed during a skippable script (FUN_0026C430, state +0x28). */
	float Speed = 1.f;
	FString Queued;
	float QueuedStart = 0.f, QueuedEnd = 0.f;
	const FSSXFeCameraSet* CameraSet = nullptr;
	bool IsRunning() const { return CameraSet != nullptr && CameraSet->Find(Current) != nullptr && IsPlaying(); }
	TArray<FString> History;
};

namespace SSXFeCams
{
	constexpr float Fps = 30.f;
	constexpr int32 CycleFramesPerSlot = 16;
	constexpr int32 VenueFramesPerSlot = 24;
	SSXTRICKYREBUILD_API FString RiderCamName(int32 Char);
	/** Venue ring slot of a track id = its VenueSel list index (.data 0x3A8AF0 order: Gari, Snow, Elysium, Mesa, Merqury,
	 *  Tokyo, Aloha, Alaska, Untracked, Pipedream); screens, medals and cycle / choose scripts are all indexed by it. */
	SSXTRICKYREBUILD_API int32 VenueRingSlot(int32 TrackId);
	SSXTRICKYREBUILD_API const TCHAR* VenueChooseName(int32 Index);
	// 0x347180
	SSXTRICKYREBUILD_API const TCHAR* VenueEventName(int32 Index);
	/** The ssxfe.map instance mdl_medal_NN standing at a venue: NN = (10 - ring slot) % 10 (the chooseVenue cameras end
	 *  at the venue's screen: Gari +Y = mdl_medal_00, Snowdream 54 deg = _09, Elysium 18 deg = _08 ...). */
	SSXTRICKYREBUILD_API int32 VenueMedalModel(int32 TrackId);
}
