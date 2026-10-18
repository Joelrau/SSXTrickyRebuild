// 0x36E570, 0x17E148, FUN_0014AAA0, 0x14B9B8, FUN_0014BAA8, FUN_0014BB08, FUN_001579E0, FUN_0014C2A8
#pragma once

#include "CoreMinimal.h"
#include "Input/PadMapper.h"
#include "Race/RaceRules.h"

namespace SSXLesson
{
	enum class EState : int32
	{
		None = 0, Init = 1, Intro = 2, Start = 3, Wait = 4, Preview = 5, PreviewEnd = 6, Perform = 7, Practice = 8,
		PracticeEnd = 9, End = 10, Exit = 11,
	};
	SSXTRICKYREBUILD_API const TCHAR* LexToString(EState S);

	constexpr int32 Chapters = 6, LessonsPerChapter = 5, Lessons = 30;
	// FUN_00164110
	SSXTRICKYREBUILD_API int32 FirstLesson(int32 Chapter);

	// FUN_0014D6E0
	constexpr uint32 PadLeft = 0x100008, PadRight = 0x80004, PadUp = 0x20001, PadDown = 0x40002;
	constexpr uint32 PadCross = 0x800, PadTriangle = 0x20, PadCircle = 0x40, PadSelect = 0x100;

	// FUN_0014DAA8, 0x14D948, 0x14D9C8, 0x14DA38, 0x14DCC0
	constexpr int32 LocSelect = 0x22D, LocShowMe = 0x199, LocPerform = 0x195, LocFreeJump = 0x198, LocQuit = 0x18E;
	constexpr int32 LocNewTrick = 0x22C, LocRestart = 0x191;
	constexpr int32 LocAreYouSure = 0x146, LocYes = 0x1D6, LocNo = 0x188;
	// 0x36E560, 0x23D4A8
	constexpr int32 PracticeLocs[3] = { 0x241, 0x243, 0x132 };
	constexpr float WaitPanelH = 435.f, ShortPanelH = 240.f, EndPanelH = 330.f;

	// 0x1A7848, 0x1A7860
	constexpr int32 OvLesson = 10, OvController = 14;

	// 0x1A8648, 0x1A8658, 0x1A8668, FUN_002138F0
	enum class ESound : uint8 { Move, Back, Accept };

	// FUN_00231398
	constexpr int32 NarrIntro = 0, NarrPreview = 1, NarrPerform = 2, NarrPassed = 4, NarrFailed = 5;
	SSXTRICKYREBUILD_API uint32 NarrationWord(int32 Step, uint32& InOutFlags);

	class SSXTRICKYREBUILD_API ILessonEnv
	{
	public:
		virtual ~ILessonEnv() = default;
		// 0x337E08
		virtual bool IsBlocked() const { return false; }
		// 0x1125C8
		virtual void SetRacePhase(ERacePhase P) {}
		// FUN_00228D10
		virtual void StopSpeech() {}
		virtual void Narration(int32 Step) {}
		virtual void Sound(ESound S) {}
		// FUN_00175E50
		virtual bool CameraIdle() const { return true; }
		// FUN_0014D7D0, FUN_0014D898
		virtual void ResetWorld() {}
		// FUN_0014B768
		virtual void SetBoost(double Meter, double TrickyTimer) {}
		// FUN_0017A2F8
		virtual void UseTutorialMapping(bool bTutorial) {}
		// FUN_002C8BC8
		virtual bool LoadInp(int32 Rider, int32 Lesson, TArray<FInpFrame>& Out) { return false; }
		// FUN_0011DA40
		virtual void StampFinish() {}
		virtual void ClearFeKey() {}
		virtual void QuitRace() {}
		// FUN_00174C48
		virtual void CameraFlash() {}
		virtual int32 Rand() { return 0; }
		virtual bool HasTrickText() const { return false; }
		virtual void ClearTrickText() {}
	};

	// 0x31DF88
	struct FGlyph { float X, Y; int32 Sprite; uint32 Mask; };
	SSXTRICKYREBUILD_API const FGlyph* DiagramGlyphs();
	constexpr int32 NumDiagramGlyphs = 14;
	constexpr int32 DiagramBaseSprite = 0x60;
	constexpr float DiagramX = 20.f, DiagramY = 20.f;
	constexpr int32 DiagramFirstFrame = 121;
	// FUN_0014BD98
	SSXTRICKYREBUILD_API uint32 DiagramMask(const FInpFrame& F, TFunctionRef<uint32(int32 Action)> ActionMask);

	class FLessonMan;
	// FUN_00154638, FUN_00157C18
	SSXTRICKYREBUILD_API bool LessonBookCheck(FLessonMan& M, int32 BookChar, uint32 W0, uint32 W1);

	class SSXTRICKYREBUILD_API FLessonMan
	{
	public:
		void Construct(int32 InRider);
		void Update(ILessonEnv& Env);
		void Request(ILessonEnv& Env, EState S);
		// FUN_0014B638
		void Restart(ILessonEnv& Env);
		// FUN_0014D930
		void EndTrigger() { if (!bEnd) bEnd = true; }
		void MarkPassed() { bPassed = true; }
		void AddPad(uint32 Bits) { PadEdges |= Bits; }

		// 0x14B7C8
		bool InjectPad(FSSXPadFrame& InOut) const;
		// 0x14B8A8
		void AfterPad();

		EState GetState() const { return State; }
		EState State = EState::None;
		EState Requested = EState::None;
		EState Prev = EState::None;
		int32 Frames = 0;
		bool bEnd = false;
		int32 MenuSel = 0;
		int32 MenuCount = 0;
		int32 MenuSaved = 0;
		bool bPassed = false;
		int32 MenuItems[7] = {};
		float DiagramPosX = DiagramX, DiagramPosY = DiagramY;
		int32 PracticeLoc = 0;
		int32 MenuLevel = 0;
		int32 Column = 0;
		int32 Scroll = 0;
		TArray<FInpFrame> Inp;
		int32 InpIndex = 0;
		int32 InpFrames = 0;
		int32 Lesson = 0;
		int32 Chapter = 0;
		int32 Rider = 0;
		float PanelH = WaitPanelH;
		bool bQuitDialog = false;
		bool bOvLesson = false, bOvController = false;
		bool bInpMissing = false;
		uint32 PadEdges = 0;
		bool bTutorialMapping = false;
		FString RiderTrickText;

	private:
		void Enter(ILessonEnv& Env, EState S);
		void Exit(ILessonEnv& Env, EState S);
		void Tick(ILessonEnv& Env, EState S);
		// FUN_0014B5A8
		void ResetFields(ILessonEnv& Env);
		void Setup(ILessonEnv& Env);
		uint32 ReadPad(ILessonEnv& Env);
		void WaitMenu();
		// FUN_0014D948
		void EndMenu(bool bPreview);
		void LoadLesson(ILessonEnv& Env);
		void NewTrick(ILessonEnv& Env);
		void OpenOverlay(int32 Ov, bool bOn) { (Ov == OvLesson ? bOvLesson : bOvController) = bOn; }
	};
}
