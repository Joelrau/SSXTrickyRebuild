// FSSXSettings (0x32C9E8..0x32CA50) consumers: volumes / preset / arcade / speaker -> SSXGameAudio::ApplySettings; Units -> HUD speed (0x1A4F70) and race results (0x1B1278); Widescreen -> WidescreenView (0x1DD450, read by 0x1DC3E0 and 0x1E89C0); Brightness -> ApplyBrightness (0x17B980 / 0x1E4288); MaxBoarders -> RacerCount (0x2657A8); Controller[] -> PadLayout (the tutorial always loads layout 0); Camera1P / Camera2P / HudDetail -> race camera view at load (0x180030), HUD masks (0x17F158); Volume3 -> announcer class-gate level (0x235D58).
#pragma once

#include "CoreMinimal.h"
#include "Progression/SSXProgression.h"

class UWorld;

namespace SSXSettings
{
	/** cPS2GraphicsMan vfunc_36 (graphics +0x17D4 mode): +0x17E0 x-projection scale, +0x17E4 y-projection scale, +0x17D8 lowest viewport top, +0x17DC largest viewport height (fractions of the 480-line screen). Off 1 / 1 / 0 / 1; 16:9 0.75 / 0.75 / 0.125 / 0.75 (letterbox); Animorphic 0.75 / 1 / 0 / 1. Other modes behave as Off (vfunc_21 applies the factors only for modes 0..2). */
	struct FWidescreenView
	{
		float XScale = 1.f;
		float YScale = 1.f;
		float ViewTop = 0.f;
		float ViewHeight = 1.f;
	};
	SSXTRICKYREBUILD_API FWidescreenView WidescreenView(int32 Mode);

	/** Shape of the 3D image the mode produces on the TV: 640 pixels across ViewHeight x 480 lines, x tangent scaled by 1 / XScale and y tangent by 1 / YScale: aspect = 4/3 x YScale / (XScale x ViewHeight) = 4:3 (Off) or 16:9 (16:9 and Animorphic). */
	SSXTRICKYREBUILD_API float ImageAspect(int32 Mode);

	/** Full horizontal FOV (radians) for the camera's half-FOV (the original's FOV is horizontal, m00 = cot(half) x XScale): 2 atan(tan(half) / XScale). The widescreen modes keep the Off vertical FOV (Hor+). */
	SSXTRICKYREBUILD_API double HorizontalFov(double HalfFovRad, int32 Mode);

	/** Racer count RS+0 = DAT_0032DB6C for a launch (0x2657A8, then the World Circuit set-ups): trick tutorial (6) 1; Freeride (1) and timed race (7) the number of players; World Circuit race (4) 6 (heat set-up 0x16D300); World Circuit showoff (5) 1 (0x16D230); every other mode the Max Boarders option 0x32CA48. */
	SSXTRICKYREBUILD_API int32 RacerCount(int32 GameMode, int32 Players, int32 MaxBoarders);

	/** DAT_0032EBAC for a next race chosen in a race's results: Race of the next-event menu (mode 2 / 4) the Max Boarders option (0x1BC8A0 case 0), Showoff / Time Challenge (0x1BC8A0 cases 1 / 2) and Freeride (0x1B32C0) the number of humans (0x164288); the World Circuit set-ups still replace it (6 / 1). */
	SSXTRICKYREBUILD_API int32 NextEventRacerCount(int32 NextMode, int32 Humans, int32 MaxBoarders);
	/** Racer count of a launch: the next-event count when one was chosen (>= 0), else RacerCount; the World Circuit
	 *  heat set-ups and the tutorial override both. */
	SSXTRICKYREBUILD_API int32 LaunchRacerCount(int32 GameMode, int32 Players, int32 MaxBoarders, int32 NextEventRacers);

	/** BTNMAP layout of a port's rider: the configuration 0x32CA1C + 4 * port (Default 0 / Pro 1), except the trick tutorial which always loads layout 0 (0x14AE74). */
	SSXTRICKYREBUILD_API int32 PadLayout(const FSSXSettings& S, int32 Port, int32 GameMode);

	/** Announcer class-gate level 0x235D58: min(snd+0x6E68, int(snd+0x383C)). Cap = snd+0x6E68. */
	SSXTRICKYREBUILD_API int32 AnnouncerLevel(float Volume3, int32 Cap);

	/** Push the brightness scale into MPC_SSXWorld "Brightness", which PP_SSXAuthoredColour multiplies the displayed frame by (GS final pass modulate 0x80 x scale, clamped at 255). No effect without the collection. */
	SSXTRICKYREBUILD_API void ApplyBrightness(UWorld* World, float Scale);
}
