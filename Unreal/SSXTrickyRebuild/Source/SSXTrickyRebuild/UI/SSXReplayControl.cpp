#include "UI/SSXReplayControl.h"
#include "Race/SSXReplay.h"
#include "UI/SSXFeTypes.h"

using namespace SSXReplayCtl;
using namespace SSXFe;
using namespace SSXReplay;

void FSSXReplayControl::Activate(FSSXReplay& R, int32 InPadRacer, bool bAdvanced)
{
	PadRacer = InPadRacer;
	bEditor = bAdvanced;
	if (!R.IsLoaded()) R.SetFocus(PadRacer);
	R.ApplyFocusCamera();
	PadRacer = R.GetFocus();
	R.SetCommand(CmdToStart);
	if (!bEditor) { HelpMax = 1; PanelA = PanelB = 360.f; HelpLevel = 0; }
	else { PanelA = 360.f; PanelB = 105.f; HelpMax = 2; HelpLevel = 1; }
	bBusy = false;
	MarkFrame = 0;
	Slide = 0;
	RepeatMask = 0;
	RepeatCount = 0;
	PanelY = HelpLevel == 2 ? PanelB : (HelpLevel == 1 ? PanelA : 480.f);
}

void FSSXReplayControl::Update(FSSXReplay& R, uint32 InHeld, uint32 Prev, const FEnv& Env)
{
	LastCommand = 0;
	SlidePanel();   // 0x1EC728
	if (Fade > 0.f)
	{
		if (FadeDelay < 1) { Fade -= 0.016f; if (Fade < 0.f) Fade = 0.f; }
		else --FadeDelay;
	}
	if (Slide < 0) { Slide = FMath::Min(0, Slide + 5); }
	else if (Slide > 0) { Slide = FMath::Max(0, Slide - 5); }
	if (bBusy) return;
	if (++Blink > 60) Blink = 0;
	if (R.GetState() == Started) return;
	if (R.GetState() == Paused) bMarking = false;
	if (R.GetState() == ClipPlay) return;

	const auto CamMode = [&]() { return Env.CameraMode ? Env.CameraMode() : 0; };
	const auto Nudge = [&](int32 Op) { if (Env.CameraNudge) Env.CameraNudge(1.f, Op); };
	// 0x1EDED0
	const auto Msg = [&](int32 Loc) { MessageLoc = Loc; Fade = 1.f; FadeDelay = 60; if (Env.Message) Env.Message(Loc); };
	const auto ClearList = [&]() { R.SetPlaylist(nullptr); };   // 0x191B18

	int32 Cmd = 0;
	int32 Old = RepeatCount;
	uint32 Held = InHeld;
	uint32 Pressed = InHeld & ~Prev;
	const uint32 Mask = RepeatMask;
	RepeatCount = 0;
	if (Mask != 0)
	{
		Held &= (Mask | 0x1E00200);
		Pressed &= (Mask | 0x1E00200);
	}
	RepeatMask = 0;
	if ((Held & Mask) == 0) Old = 0;

	if (Pressed & PadUp)
	{
		if (HelpLevel < HelpMax) { Add(EEvent::HelpSound); bHelpChanged = true; ++HelpLevel; }
	}
	else if ((Pressed & PadDown) && HelpLevel > 0)
	{
		Add(EEvent::HelpSound); bHelpChanged = true; --HelpLevel;
	}

	const bool bL1 = (Held & PadL1) != 0;
	const int32 St = R.GetState();
	const uint32 Play = R.GetPlayFrame();
	enum class EEnd : uint8 { Done, Pause7, Pause7Clear, Clear };
	EEnd End = EEnd::Done;
	bool bList = false;

	auto StickOp = [&](int32 Op) { if (CamMode() == 8) Nudge(Op); };
	if (Held & RStickA) StickOp(bL1 ? 6 : 2);
	else if (Held & RStickB) StickOp(bL1 ? 7 : 3);
	else if (Held & RStickC) StickOp(bL1 ? 8 : 0);
	else if (Held & RStickD) StickOp(bL1 ? 9 : 1);
	else
	{
		const bool bL2Pressed = (Pressed & PadL2) != 0;
		bool bGeneric = true;
		if (bL1)
		{
			if (bL2Pressed)
			{
				bGeneric = false;
				Add(EEvent::SpeechStop);
				ClearList();
				if (St == Paused) { if (Play != 0) { Cmd = CmdToStart; Add(EEvent::Navigate); } }
				else End = EEnd::Pause7;
			}
			else if (Pressed & PadR2)
			{
				bGeneric = false;
				Add(EEvent::SpeechStop);
				ClearList();
				if (St == Paused) { if (Play != R.GetRecordedLength() && Play != R.GetRecordFrame()) { Cmd = CmdToEnd; Add(EEvent::Navigate); } }
				else End = EEnd::Pause7;
			}
			else if (!(Held & PadR2))
			{
				bGeneric = false;
			}
		}
		if (bGeneric)
		{
			if (!bL2Pressed && !(Held & PadL2))
			{
				if (!(Pressed & PadR2) && !(Held & PadR2))
				{
					if ((Pressed & PadR1) || (Held & PadR1))
					{
						if (bEditor)
						{
							const bool bAtEnd = R.IsPlaying() && Play == R.GetRecordedLength();
							if (!bAtEnd)
							{
								if (Old == 0 || Old > 14)
								{
									if (Pressed & PadR1) Add(EEvent::Navigate);
									Cmd = CmdStep;
								}
								RepeatMask = PadR1;
								RepeatCount = Old + 1;
							}
						}
					}
					else if (Pressed & PadTriangle)
					{
						if (bEditor)
						{
							ClearList();
							if (St == Paused)
							{
								if (Play != R.GetRecordedLength())
								{
									const int32 Ok = R.ClipMarkAllowed();   // 0x1921E0
									if (Ok == 1)
									{
										Cmd = CmdClip; Add(EEvent::Navigate); bMarking = true; MarkFrame = Play;
									}
									else if (Ok == 0) { Add(EEvent::Navigate); Msg(0x16D); }
								}
							}
							else End = EEnd::Pause7;
						}
					}
					else if (Pressed & PadCircle)
					{
						if (bEditor)
						{
							ClearList();
							if (St == Paused)
							{
								if (Play == 0) { Add(EEvent::Navigate); Msg(0x15B); }
								else if (R.DeleteCamera(Play)) { Add(EEvent::Navigate); Msg(0x14F); }
							}
							else End = EEnd::Pause7;
						}
					}
					else if (Pressed & PadSquare)
					{
						if (bEditor)
						{
							ClearList();
							if (St == Paused) { if (R.DeleteClipAtFrame()) { Add(EEvent::Navigate); Msg(0x16C); } }
							else End = EEnd::Pause7;
						}
					}
					else if (Pressed & PadCross)
					{
						if (St != Paused) End = EEnd::Pause7Clear;
						else if (Play != R.GetRecordedLength()) { Cmd = CmdPlayToggle; End = EEnd::Clear; Add(EEvent::Navigate); }
					}
					else if (Pressed & PadStart)
					{
						// 0x1EFDB0
					}
					else if (Pressed & PadSelect)
					{
						Add(EEvent::Navigate);
						Add(EEvent::CameraSound);
						if (Env.NextCamera) Env.NextCamera();
						if (!bEditor && CamMode() == 8 && Env.NextCamera) Env.NextCamera();
						R.StoreCamera();
					}
					else if (Pressed & (PadRight | PadLeft))
					{
						if (Env.Racers >= 2)
						{
							Add(EEvent::Navigate);
							R.ApplyFocusCamera();
							Nudge((Pressed & PadRight) ? 4 : 5);
							Add(EEvent::BoardAudio);
							R.StoreCamera();
						}
					}
					else if (Pressed & (PadLJoyLeft | PadLJoyRight | PadLJoyUp))
					{
						if (bEditor)
						{
							if (St != Paused) End = EEnd::Pause7Clear;
							else
							{
								// 0x195690 / 0x1955B8 / 0x191D88
								const TArray<int32>& Clips = Timeline.Clips;
								const auto ClipStart = [&](int32 C) { const int32 K = R.GetClip(C).Key; return K >= 0 ? R.GetKey(K).Frame : 0u; };
								List430.Reset();
								List430Cur = 0;
								const auto Add430 = [&](int32 C) { if (List430.Num() < MaxPlaylist) List430.Add(C); };
								if (Pressed & PadLJoyLeft)
								{
									for (const int32 C : Clips) Add430(C);
									if (List430.Num() < 1) End = EEnd::Clear;   // 0x191B18
									else
									{
										// 0x195510
										bool bFound = false;
										for (int32 I = List430.Num() - 1; I >= 1 && !bFound; --I)
										{
											const uint32 S = ClipStart(List430[I]);
											if (S == Play) { List430Cur = I - 1; bFound = true; }
											else if (S < Play) { List430Cur = I; bFound = true; }
										}
										if (!bFound && ClipStart(List430[0]) < Play) { List430Cur = 0; bFound = true; }
										if (bFound) { Cmd = CmdPrevClip; Add(EEvent::Navigate); bList = true; End = EEnd::Clear; }
									}
								}
								else if (Pressed & PadLJoyRight)
								{
									for (const int32 C : Clips) Add430(C);
									if (List430.Num() < 1) End = EEnd::Clear;
									else
									{
										// 0x1954B8
										bool bFound = false;
										for (int32 I = 0; I < List430.Num() && !bFound; ++I)
										{
											if (Play < ClipStart(List430[I])) { List430Cur = I; bFound = true; }
										}
										if (bFound) { Cmd = CmdNextClip; Add(EEvent::Navigate); bList = true; End = EEnd::Clear; }
									}
								}
								else
								{
									for (const int32 C : Clips) { if (Play <= ClipStart(C) + uint32(R.GetClip(C).Length)) Add430(C); }
									if (List430.Num() > 0) { bList = true; Cmd = CmdPlayToggle; Add(EEvent::Navigate); End = EEnd::Clear; }   // 0x1955E0
								}
							}
						}
					}
					else if (Pressed & PadLJoyDown)
					{
						if (bEditor && List430.Num() > 0)   // 0x191B18
						{
							ClearList();
							if (St != Paused) End = EEnd::Pause7;
						}
					}
					else if (Pressed & PadL3)
					{
						if (bEditor)
						{
							if (St == Paused)
							{
								// 0x190C88 / 0x190C40
								if (!R.CanInsertCameraCut()) { Add(EEvent::Navigate); Msg(0x153); }
								else if (R.InsertCameraCut(Play)) { Add(EEvent::Navigate); Msg(0x149); }
							}
							else Cmd = CmdPause;
						}
					}
				}
				else
				{
					RepeatMask = PadR2;
					int32 Next = 0;
					if (Old != 0 && Old <= 14) { Next = Old + 1; RepeatCount = Next; }
					if (Next == 0)
					{
						if (St == Paused)
						{
							if (Play != R.GetRecordFrame())
							{
								Add(EEvent::SpeechStop);
								if (Pressed & PadR2) Add(EEvent::Navigate);
								Cmd = CmdNextKey;
								RepeatCount = 1;
							}
						}
						else { End = EEnd::Pause7; RepeatMask = 0; }
					}
				}
			}
			else
			{
				RepeatMask = PadL2;
				int32 Next = 0;
				if (Old != 0 && Old <= 14) { Next = Old + 1; RepeatCount = Next; }
				if (Next == 0)
				{
					if (St == Paused)
					{
						if (Play != 0)
						{
							Add(EEvent::SpeechStop);
							if (bL2Pressed) Add(EEvent::Navigate);
							Cmd = CmdPrevKey;
							RepeatCount = 1;
						}
					}
					else { End = EEnd::Pause7; RepeatMask = 0; }
				}
			}
		}
	}

	switch (End)
	{
	case EEnd::Pause7: Cmd = CmdPause; Add(EEvent::Navigate); break;
	case EEnd::Pause7Clear: Cmd = CmdPause; Add(EEvent::Navigate); ClearList(); break;
	case EEnd::Clear: R.SetPlaylist(bList ? &List430 : nullptr, List430Cur); break;   // 0x191B18
	default: break;
	}

	if ((Held & 0x1E00000) && bEditor && CamMode() == 8) R.StoreCamera();
	// A play command still pending: the replay has not stepped since (the UI and the race keep separate 60 Hz clocks).
	if (Cmd == 0) { if (R.GetState() == Paused && R.GetCommand() != CmdPlayToggle) Add(EEvent::AudioPause); }
	else if (Cmd == CmdPlayToggle) Add(EEvent::AudioResume);
	else Add(EEvent::AudioPause);
	if (Cmd != 0) R.SetCommand(Cmd);
	LastCommand = Cmd;
	// 0x191D88
	R.GetTimeline(Timeline);
	if (Timeline.Length - 1u < 0x707u) { WinLen = int32(Timeline.Length); WinStep = 1; }
	else { WinLen = 0x708; WinStep = 0xB4; }
}

void FSSXReplayControl::SlidePanel()
{
	// 0x1EC728
	bHelpChanged = false;
	const float To = HelpLevel == 2 ? PanelB : (HelpLevel == 1 ? PanelA : 480.f);
	const float Y = PanelY;
	if (Y != To)
	{
		bool bPast;
		if (Y < To) { bPast = To < Y + 6.f; PanelY = Y + 6.f; }
		else { bPast = Y - 6.f < To; PanelY = Y - 6.f; }
		if (bPast) PanelY = To;
		bHelpChanged = true;
	}
}

int32 FSSXReplayControl::ScrollWindow(int32 Play) const
{
	// 0x1ED490
	int32 Start = WinStart;
	if (Start + WinLen < Play)
	{
		Start = Play - (WinLen - WinStep);
		Slide = 1 - WinStep;
		WinStart = Start;
	}
	else if (Play < Start)
	{
		Start = Play - WinStep;
		Slide = WinStep - 1;
		WinStart = Start;
	}
	const int32 Max = int32(Timeline.Length) - WinLen;
	if (Max < Start) { WinStart = Start = Max; Slide = 0; }
	if (Start < 0) { WinStart = Start = 0; Slide = 0; }
	return Start + Slide;
}
