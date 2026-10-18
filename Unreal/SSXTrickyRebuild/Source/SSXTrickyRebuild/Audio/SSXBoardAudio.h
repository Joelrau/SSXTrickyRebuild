#pragma once

#include "CoreMinimal.h"
#include "Audio/SSXAudioTypes.h"
#include "Audio/SSXSnowSound.h"

struct FRiderState;

namespace SSXBoardAudio
{
	constexpr int32 MotionAir = 1, MotionGround = 2, MotionRail = 3;

	enum class EVoice : uint8 { Speed, Glide, Carve, AIGlide };

	/** 0x21A6A8..0x21A788, float32: Slip = |trunc(dot4(contact_forward, velocity))|, Dig = |trunc(ground drag)|, Lean = |trunc(steer_cur * 127)|, Board = (Slip >> 2) + 2 Dig, Bend = Board + Slip. */
	SSXTRICKYREBUILD_API FSSXSnowInputs Inputs(const FVector3f& ContactForward, const FVector3f& Velocity, float GroundDrag, float SteerCur);
	SSXTRICKYREBUILD_API FSSXSnowInputs Inputs(const FRiderState& S);

	// 0x20F908
	SSXTRICKYREBUILD_API ESSXSnowSurface SnowIndex(int32 SurfaceId, int32 Motion, int32 RailSurface);

	// 0x20D928 / 0x343098
	SSXTRICKYREBUILD_API int32 Curve(int32 Index, int32 X);

	// 0x39EB38
	SSXTRICKYREBUILD_API FSSXSnowResult Callback(EVoice Voice, const FSSXSnowPrograms& Programs, int32 Motion, ESSXSnowSurface Index,
		const FSSXSnowInputs& In);
	SSXTRICKYREBUILD_API int32 VoiceSound(EVoice Voice, ESSXSnowSurface Index);
	constexpr double PlayerVoiceRadius = 40.0, AIVoiceRadius = 50.0, TrackSoundRadius = 30.0, AIOneShotRange = 40.0;
	/** Push positioned at the rider (record +0x28 = rider +0x140) without 0x226DD8 distance parameters: default 30 m falloff (+0x88 = 0, 0x20AAB8), no cull; landing 0x217D50, crash 0x218328, slide 0x218700. */
	constexpr double DefaultPushRadius = 30.0;

	// 0x21B828
	/** TLand = +0x57F4 while motion is Air (else 0), TElapsed = +0x57FC, TApex = +0x5800. False = inactive (music byte 127, wind stopped); else music byte and wind level factor (x SFX level) of zbxsfx #32 (2D). */
	SSXTRICKYREBUILD_API bool BigAir(double TLand, double TElapsed, double TApex, bool bGameMode7, int32& OutMusicByte, double& OutWindLevel);
	constexpr int32 WindSound = 32;

	// 0x20AAB8
	SSXTRICKYREBUILD_API int32 Falloff(int32 Vol, double DistM, double RadiusM);
	/** 0x20AB48: (clamp((prev - dist) / A, +-894.08) + 8000) / 8000. TODO: the unit of A (the tick argument) is unknown; seconds are used. */
	SSXTRICKYREBUILD_API double Doppler(double PrevDistM, double DistM, double A);
	// 0x20E730
	constexpr double EarForwardCm = 100.0, EarUpCm = 25.0;

	// 0x217D50
	inline int32 LandSound(ESSXSnowSurface Index, bool bPlayer) { return int32(Index) * 8 + (bPlayer ? 1 : 5); }
	inline int32 LandVolume(int32 ImpactSpeed, bool bPlayer) { return Curve(bPlayer ? 0 : 1, ImpactSpeed); }
	// 0x218328
	inline int32 CrashSound(int32 Rand15, int32 SurfaceBeforeWipeout)
	{
		return (SurfaceBeforeWipeout == 3 || SurfaceBeforeWipeout == 4) ? 64 : (Rand15 * 3) / 0x7FFF + 48;
	}
	/** Track-script multiplier cue (0x11E660 -> 0x219D70; voice set-up as #119: 100 / 0x5A / 0x32, level class 4): zbxsfx #116 below 3, #117 from 3, #118 from 5. */
	inline int32 MultiplierSound(double M) { return float(M) >= 5.f ? 118 : (float(M) >= 3.f ? 117 : 116); }
	// 0x11E8A0 / 0x219FC0
	inline int32 TimeBonusSound(double Seconds) { return float(Seconds) >= 10.f ? 0x73 : (float(Seconds) >= 5.f ? 0x72 : 0x71); }
	constexpr int32 SlideSound = 51, WallSound = 49, TweakSound = 119, ResetSound = 123, LeadSound = 109, BookSound = 95;
	constexpr double SlideStopFade = 0.75;
	// 0x216960
	inline int32 BumpSound(int32 Rand15) { return (Rand15 * 2) / 0x7FFF + 90; }
	constexpr int32 BumpCooldownFrames = 30;
	constexpr int32 WallSoundHoldFrames = 3;   // ring hold of 50 ms (snd+0x80 counts ms)
	// 0x2192A8
	inline int32 BoostSound(double Meter) { return Meter > 0.666 ? 120 : (Meter > 0.3336 ? 121 : 122); }
	inline int32 BoostBurstSound(double Meter) { return BoostSound(Meter) - 48; }
	// 0x2197D8
	constexpr int32 NoBoostSound = 108;
	// 0x216C50
	inline int32 PropHitVolume(double SpeedCmS) { return FMath::Clamp(Curve(2, int32(SpeedCmS)), 0, 127); }
	constexpr int32 PropHitHold = 3;   // the same 50 ms ring hold
	// 0x1579E0 / 0x2190F0
	constexpr double BookDelaysMs[3] = { 250.0, 500.0, 750.0 };
	// 0x217D50 / 0x2181E8
	inline int32 CrowdLevel(double X) { return X <= 0.0 ? -1 : (X < 0.25 ? 0 : (X < 0.65 ? 1 : 2)); }

	SSXTRICKYREBUILD_API FString ZboardId(int32 Sound);
	SSXTRICKYREBUILD_API FString ZbxsfxId(int32 Sound);
}
