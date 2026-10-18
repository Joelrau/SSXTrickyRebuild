// 0x134288 / 0x1343D0 / 0x134830
#include "Particles/SSXAngerMarker.h"

namespace SSXParticles
{
	FVector FPrevRow::Read(int32 SimFrame, const FVector& Row)
	{
		if (!bSeen || SimFrame != Frame)
		{
			bPrev = bSeen && SimFrame == Frame + 1;
			Prev = Seen;
			Seen = Row;
			Frame = SimFrame;
			bSeen = true;
		}
		return bPrev ? Prev : Seen;
	}

	namespace
	{
		constexpr float KeyA = 0.33327361941337585f, KeyHi = 1.99f;
		constexpr float PeakA = 0.77786f;
		constexpr float PopRate = 0.016386366f, FlashRate = 0.016652344f, PingRate = 0.066181876f;
	}

	FVector4 FAngerMarker::Key(int32 K)
	{
		switch (K)
		{
		case 1: return FVector4(KeyA, KeyHi, KeyHi, 0.f);
		case 2: return FVector4(KeyA, KeyHi, 1.f, 0.f);
		case 3: return FVector4(KeyA, KeyHi, 0.f, 0.f);
		default: return FVector4(KeyA, 1.f, 1.f, 1.f);
		}
	}

	double FAngerMarker::MoodOf(int32 Level)
	{
		float V = float(Level);
		if (V < 60.f)
		{
			return V / 60.f;
		}
		if (V < 80.98926f)
		{
			return (V - 60.f) / 20.989258f + 1.f;
		}
		return (V - 80.98926f) / 40.f + 2.f;
	}

	FVector4 FAngerMarker::Curve(double F)
	{
		const float T = float(F);
		auto Lerp = [](const FVector4& A, const FVector4& B, float S)
		{
			return FVector4(float(A.X) + float(B.X - A.X) * S, float(A.Y) + float(B.Y - A.Y) * S, float(A.Z) + float(B.Z - A.Z) * S,
				float(A.W) + float(B.W - A.W) * S);
		};
		if (T < 1.f) { return Lerp(Key(0), Key(1), T); }
		if (T < 2.f) { return Lerp(Key(1), Key(2), T - 1.f); }
		return Lerp(Key(2), Key(3), T - 2.f);
	}

	void FAngerMarker::Frame(const FAngerMarkerInput& In)
	{
		bActive = false;
		if (!In.bShown)
		{
			return;
		}
		bActive = true;
		Mood = float(MoodOf(In.Level));
		if (bPop)
		{
			float T = PopT;
			if (T >= 1.f) { PopT = 0.f; bPop = false; T = 0.f; }
			PopT = FMath::Min(T + PopRate, 1.f);
		}
		else if (bFlash)
		{
			float T = FlashT;
			if (T >= 1.f) { FlashT = 0.f; bFlash = false; T = 0.f; }
			FlashT = FMath::Min(T + FlashRate, 1.f);
		}
		if (!bPingUp)
		{
			Ping -= PingRate;
			if (Ping <= 0.f) { Ping = -Ping; bPingUp = true; }
		}
		else
		{
			Ping += PingRate;
			if (Ping >= 1.f) { bPingUp = false; Ping = 1.f - (Ping - 1.f); }
		}
		bTarget = false;
		if (In.bGrudge && In.GrudgeRival == In.Human)
		{
			bTarget = true;
			Kind = In.GrudgeKind;
			if (Kind != LastKind)
			{
				LastKind = Kind;
				bPop = true;
				PopT = 0.f;
				if (bFlash) { FlashT = 0.f; bFlash = false; }
			}
		}
		if (!bPop && LastAnger < In.Anger)
		{
			FlashT = 0.f;
			bFlash = true;
		}
		LastAnger = In.Anger;
		const float L = float(Lift);
		Pos = FVector(float(In.Bone4.X) + float(In.Up.X) * L, float(In.Bone4.Y) + float(In.Up.Y) * L, float(In.Bone4.Z) + float(In.Up.Z) * L);
	}

	bool FAngerMarker::Sprite(double ViewZ, FAngerSprite& Out)
	{
		if (!bActive || !(bTarget || bFlash))
		{
			return false;
		}
		const float Z = float(ViewZ);
		if (!(Z > 0.f && Z < float(MaxDepth)))
		{
			return false;
		}
		auto KindKey = [](int32 K) { return K == 1 ? Key(1) : K == 2 ? Key(3) : Key(0); };
		float Sx = 1.f, Sy = 1.f;
		FVector4 C;
		if (!bFlash)
		{
			C = KindKey(Kind);
			if (!bPop)
			{
				Sx = Sy = Ping * 0.34085047f + 1.f;
				C.X = float(C.X) + Ping * (PeakA - float(C.X));
			}
			else
			{
				const float T = PopT;
				if (T < 0.33333334f)
				{
					Sy = T * 3.f * 1.1018491f + 0.3f;
					Sx = T * 3.f * 0.f + 0.2f;
				}
				else if (T < 0.6666667f)
				{
					const float U = (T - 0.33333334f) * 3.f;
					Sx = U * 1.3982834f + 0.2f;
					Sy = U * -0.9675763f + 1.401849f;
				}
				else
				{
					const float U = (T - 0.6666667f) * 3.f;
					Sx = U * -0.5982834f + 1.5982834f;
					Sy = U * 0.5657273f + 0.4342727f;
				}
				Ping = 0.f;
			}
		}
		else
		{
			const float T = FlashT;
			const float Fade = T < 0.1f ? T / 0.1f : 1.f - (T - 0.1f) / 0.9f;
			const FVector4 M = Curve(Mood);
			if (!bTarget)
			{
				C = M;
				C.X = Fade * PeakA;
			}
			else
			{
				const FVector4 K = KindKey(Kind);
				C = FVector4(float(K.X) + float(M.X - K.X) * Fade, float(K.Y) + float(M.Y - K.Y) * Fade, float(K.Z) + float(M.Z - K.Z) * Fade,
					float(K.W) + float(M.W - K.W) * Fade);
				Sx = Sy = Fade * 0.29999995f + 1.f;
				C.X = float(C.X) + Fade * (PeakA - float(C.X));
			}
		}
		float S = float(Size);
		if (Z <= float(NearDepth)) { S = Z / float(NearDepth) * float(Size); }
		else if (float(FarDepth) <= Z) { S = Z / float(FarDepth) * float(Size); }
		if (float(FadeDepth) < Z)
		{
			C.X = float(C.X) * (1.f - (Z - float(FadeDepth)) / float(FadeSpan));
		}
		Out.Pos = Pos;
		Out.HalfX = S * Sx;
		Out.HalfY = S * Sy;
		Out.Argb = C;
		return true;
	}
}
