#include "Input/SSXPadVibration.h"

namespace SSXVibration
{
	int32 SmallMotorFrames(float X)
	{
		const float T = X * SmallTimeK;
		float S = SmallTimeMin;
		if (T >= SmallTimeMin)
		{
			S = T > SmallTimeMax ? SmallTimeMax : T;
		}
		return int32(S * 60.0f);
	}

	void FPadActuators::SetSmall(uint8 On, int32 Frames)
	{
		if (bEnabled)
		{
			Small = On;
			bDirty = true;
			SmallFrames = Frames;
		}
	}

	void FPadActuators::SetBig(float Level, int32 Frames)
	{
		if (bEnabled)
		{
			bDirty = true;
			BigFramesLeft = Frames;
			Big = uint8(int32(Level * 255.0f));
		}
	}

	void FPadActuators::SetEnabled(bool bOn)
	{
		if (bEnabled != bOn)
		{
			bEnabled = bOn;
			bDirty = true;
			Small = 0;
			Big = 0;
		}
	}

	bool FPadActuators::Poll(bool bStable)
	{
		if (!bStable)
		{
			Big = 0;
			SmallFrames = -1;
			Small = 0;
			BigFramesLeft = -1;
		}
		else
		{
			if (SmallFrames >= 1)
			{
				--SmallFrames;
			}
			else if (SmallFrames == 0)
			{
				SmallFrames = -1;
				bDirty = true;
				Small = 0;
			}
			if (BigFramesLeft >= 1)
			{
				--BigFramesLeft;
			}
			else if (BigFramesLeft == 0)
			{
				BigFramesLeft = -1;
				bDirty = true;
				Big = 0;
			}
		}
		if (!bDirty)
		{
			return false;
		}
		// 0x2E7C78
		SentSmall = Small;
		SentBig = Big;
		++SendCount;
		bDirty = false;
		return true;
	}

	void FPlayerRumble::Impact(FPadActuators& Pad, const FVector& Velocity, const FVector& Normal, bool bReplayPlaying)
	{
		if (bReplayPlaying)
		{
			return;   // 0x1532C0, 0x150A80
		}
		const float D = float(Velocity.X) * float(Normal.X) + float(Velocity.Y) * float(Normal.Y) + float(Velocity.Z) * float(Normal.Z);
		const float A = D * ImpactK;
		float X = 0.f;
		if (A >= 0.f)
		{
			X = A > ImpactMax ? ImpactMax : A;
		}
		if (X > ImpactThreshold)
		{
			Pad.SetSmall(1, SmallMotorFrames(X));
		}
		if (Strength < X)
		{
			Strength = X;
		}
	}

	void FPlayerRumble::Feedback(FPadActuators& Pad, float X, bool bReplayPlaying)
	{
		if (bReplayPlaying)
		{
			return;
		}
		if (X > FeedbackThreshold)
		{
			Pad.SetSmall(1, SmallMotorFrames(X));
		}
		float S = Strength;
		const float P = X + FeedbackFloor;
		if (S <= P)
		{
			S = P > StrengthMax ? StrengthMax : P;
		}
		Strength = S;
	}

	void FPlayerRumble::Update(FPadActuators& Pad, bool bReplayPlaying)
	{
		if (bReplayPlaying)
		{
			return;   // 0x153390, 0x150D18
		}
		if (Strength > 0.f)
		{
			Pad.SetBig(Strength > 1.f ? 1.f : Strength, BigFrames);
			Strength = Strength * DecayMul - DecaySub;
			if (Strength <= 0.f)
			{
				Pad.SetBig(0.f, -1);
			}
		}
		else
		{
			Pad.SetBig(0.f, -1);
		}
	}

	void FPlayerRumble::Apply(FPadActuators& Pad, const FRiderPadFeedback& Call, bool bReplayPlaying)
	{
		if (Call.bImpact)
		{
			Impact(Pad, Call.Velocity, Call.Normal, bReplayPlaying);
		}
		else
		{
			Feedback(Pad, float(Call.Amount), bReplayPlaying);
		}
	}

	void RiderFrame(FPadActuators& Pad, FPlayerRumble& Player, TConstArrayView<FRiderPadFeedback> Calls, bool bReplayPlaying)
	{
		Pad.Poll();
		for (const FRiderPadFeedback& C : Calls)
		{
			Player.Apply(Pad, C, bReplayPlaying);
		}
		Player.Update(Pad, bReplayPlaying);
	}

	void ApplyRaceLoadSetting(FPadActuators& Pad, int32 VibrationSetting)
	{
		Pad.SetEnabled(VibrationSetting != 0);
	}

	void FrontEndOneShot(FPadActuators& Pad)
	{
		Pad.SetEnabled(true);
		Pad.SetSmall(1, FrontEndFrames);
	}
}
