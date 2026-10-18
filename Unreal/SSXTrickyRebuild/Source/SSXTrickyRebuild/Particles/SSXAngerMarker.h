// 0x134288 / 0x1343D0 / 0x134830 / 0x1364D0
#pragma once

#include "CoreMinimal.h"

namespace SSXParticles
{
	struct FAngerMarkerInput
	{
		bool bShown = false;
		int32 Level = 0;            // pair byte 0 toward the human (rider +0x50 + 0x20 h)
		int32 Anger = 0;            // pair byte 3 (rider +0x53 + 0x20 h)
		bool bGrudge = false;
		int32 GrudgeRival = -1;
		int32 GrudgeKind = 0;   // +0x124: 0 white, 1 yellow, 2 red
		int32 Human = 0;            // 0x166598
		FVector Bone4 = FVector::ZeroVector;   // palette bone 4 translation of the previous pose
		FVector Up = FVector(0, 0, 1);   // rendered up row +0x4AC0 of the previous frame (FPrevRow)
	};

	/** A cPostAI-written row as the boarder update reads it: the value before the last sim step's PostAI. FRiderState::RenderRow2 (+0x4AC0) is rewritten at the end of every FRiderSim::Step, so this keeps the previous sim frame's row; read once per sim frame (this frame's row if none). */
	struct SSXTRICKYREBUILD_API FPrevRow
	{
		FVector Read(int32 SimFrame, const FVector& Row);
		void Reset() { bSeen = false; bPrev = false; }

	private:
		FVector Seen = FVector(0, 0, 1);
		FVector Prev = FVector(0, 0, 1);
		int32 Frame = 0;
		bool bSeen = false;
		bool bPrev = false;
	};

	struct FAngerSprite
	{
		FVector Pos = FVector::ZeroVector;
		double HalfX = 0.0, HalfY = 0.0;
		FVector4 Argb = FVector4(0, 0, 0, 0);
	};

	class SSXTRICKYREBUILD_API FAngerMarker
	{
	public:
		static constexpr int32 Image = 37;
		static constexpr double Lift = 95.90865325927734;
		static constexpr double MaxDepth = 3700.0784;
		static constexpr double FadeDepth = 2777.401;
		static constexpr double FadeSpan = 922.6775;
		static constexpr double Size = 33.20431;
		static constexpr double NearDepth = 500.0;
		static constexpr double FarDepth = 3000.0;
		// 0x134288
		static FVector4 Key(int32 K);
		// 0x1343D0
		static double MoodOf(int32 Level);
		// 0x134830
		static FVector4 Curve(double F);

		// 0x1343D0
		void Frame(const FAngerMarkerInput& In);
		// 0x134830
		bool Sprite(double ViewZ, FAngerSprite& Out);

		const FVector& GetPos() const { return Pos; }
		bool IsActive() const { return bActive; }
		bool IsTargeting() const { return bTarget; }
		bool IsPopping() const { return bPop; }
		bool IsFlashing() const { return bFlash; }

	private:
		bool bActive = false;
		float Mood = 0.f;
		FVector Pos = FVector::ZeroVector;
		int32 LastKind = 3;
		bool bPop = false;
		float PopT = 0.f;
		int32 LastAnger = 0;
		bool bFlash = false;
		float FlashT = 0.f;
		bool bPingUp = true;
		float Ping = 0.f;
		bool bTarget = false;
		int32 Kind = 0;
	};
}
