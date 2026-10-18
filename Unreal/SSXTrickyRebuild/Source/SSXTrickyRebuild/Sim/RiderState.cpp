#include "Sim/RiderState.h"

const TCHAR* LexToString(ERiderMotion M)
{
	switch (M)
	{
	case ERiderMotion::None: return TEXT("None");
	case ERiderMotion::Air: return TEXT("Air");
	case ERiderMotion::Ground: return TEXT("Ground");
	case ERiderMotion::RailSlide: return TEXT("RailSlide");
	case ERiderMotion::Static: return TEXT("Static");
	case ERiderMotion::WipeOut: return TEXT("WipeOut");
	case ERiderMotion::WipeOutRecover: return TEXT("WipeOutRecover");
	default: return TEXT("?");
	}
}

const TCHAR* LexToString(ERiderControl C)
{
	static const TCHAR* Names[] = {
		TEXT("None"), TEXT("Void"), TEXT("Bump"), TEXT("Cruise"), TEXT("ChangeFakie"), TEXT("FakieCruise"),
		TEXT("FinishLine"), TEXT("GateToBase"), TEXT("GateAnticipate"), TEXT("GateLaunch"), TEXT("Jump"),
		TEXT("LandHard"), TEXT("LandNormal"), TEXT("NaturalAir"), TEXT("Prewind"), TEXT("RailSlide"), TEXT("Spin"),
		TEXT("SitAndWait"), TEXT("GetupFromSit"), TEXT("WipeOut"), TEXT("WipeOutRecover"), TEXT("LessonWait"),
		TEXT("ResetWait") };
	const int32 I = int32(C);
	return (I >= 0 && I < UE_ARRAY_COUNT(Names)) ? Names[I] : TEXT("?");
}

FRiderInputWord FRiderInputWord::MaskedFor(ERiderControl Control) const
{
	FRiderInputWord W;
	switch (Control)
	{
	case ERiderControl::Bump:
	case ERiderControl::Cruise:
	case ERiderControl::ChangeFakie:
	case ERiderControl::FakieCruise:
	case ERiderControl::GateToBase:
	case ERiderControl::LandHard:
	case ERiderControl::LandNormal:
	case ERiderControl::GetupFromSit:
		W.bResetRequest = bResetRequest; W.bJumpPressed = bJumpPressed; W.bJumpHeld = bJumpHeld;
		W.bBoostHeld = bBoostHeld; W.bBoostPressed = bBoostPressed;
		W.Turn = Turn; W.Crouch = Crouch; W.Brake = Brake;
		W.ShoveSide = ShoveSide; W.ShoveFwd = ShoveFwd;
		break;
	case ERiderControl::GateAnticipate:
	case ERiderControl::GateLaunch:
	case ERiderControl::SitAndWait:
		W.GateRock = GateRock;
		break;
	case ERiderControl::Jump:
		W.bResetRequest = bResetRequest; W.bJumpHeld = bJumpHeld;
		break;
	case ERiderControl::NaturalAir:
		W.bTrickPressed = bTrickPressed; W.bResetRequest = bResetRequest;
		W.Crouch = Crouch; W.Turn = Turn; W.PrewindSpin = PrewindSpin; W.PrewindFlip = PrewindFlip;
		W.ShoveSide = ShoveSide; W.ShoveFwd = ShoveFwd;
		break;
	case ERiderControl::Prewind:
		W.bResetRequest = bResetRequest; W.bJumpHeld = bJumpHeld; W.bJumpAbort = bJumpAbort; W.bBoostHeld = bBoostHeld;
		W.PrewindSpin = PrewindSpin; W.PrewindFlip = PrewindFlip; W.PrewindTurn = PrewindTurn;
		break;
	case ERiderControl::RailSlide:
		W.bResetRequest = bResetRequest; W.bJumpPressed = bJumpPressed; W.bJumpHeld = bJumpHeld; W.bBoostHeld = bBoostHeld;
		W.Turn = Turn; W.Crouch = Crouch; W.RailSpin = RailSpin; W.bRailSpinMode = bRailSpinMode;
		break;
	case ERiderControl::Spin:
		W.TrickId = TrickId; W.bResetRequest = bResetRequest; W.bTweak = bTweak;
		W.bTrickPressed = TrickId != NoTrick;
		W.Spin = Spin; W.Flip = Flip; W.LateSpin = LateSpin; W.LateFlip = LateFlip;
		W.AirTurn = AirTurn; W.AirTilt = AirTilt; W.ShoveSide = ShoveSide; W.ShoveFwd = ShoveFwd;
		break;
	case ERiderControl::WipeOut:
		W.bResetRequest = bResetRequest; W.AirTurn = AirTurn; W.AirTilt = AirTilt;
		break;
	case ERiderControl::WipeOutRecover:
		W.bResetRequest = bResetRequest;
		break;
	case ERiderControl::LessonWait:
		W.bLessonContinue = bLessonContinue;
		break;
	default:
		break;
	}
	return W;
}
