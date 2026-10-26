#include "UI/SSXPrompts.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Input/PadMapper.h"
#include "Input/SSXDualSense.h"
#include "Input/SSXKeyBinds.h"
#include "Misc/Paths.h"
#include "UI/SSXFeDraw.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPrompts(TEXT("ssx.UI.Prompts"), 1,
		TEXT("Button prompts. Deliberate difference from the original (PS2 glyphs only): 0 = the original PS2 glyphs, 1 = the device in use ")
		TEXT("(keyboard / Xbox / PlayStation), 2 = keyboard, 3 = Xbox, 4 = PS4, 5 = PS5. The lessons always show the PS2 glyphs."));

	enum class EButton : uint8 { None, Cross, Circle, Square, Triangle, L1, R1, L2, R2, Start, Select, Up, Down, Left, Right, LStick, RStick };

	ESSXPromptDevice GLast = ESSXPromptDevice::Keyboard;
	bool GSeenInput = false;

	EButton ButtonOf(int32 Id, bool bHud)
	{
		if (!bHud)
		{
			switch (Id)   // SSXFeTypes.h IconCircle..IconSquare
			{
			case 79: return EButton::Circle;
			case 80: return EButton::Triangle;
			case 81: return EButton::Cross;
			case 82: return EButton::Square;
			default: return EButton::None;
			}
		}
		switch (Id)   // hudgame / hudtrick sheet (0x61 the whole d-pad and 0x66..0x69 its diagonals keep the original)
		{
		case 0x62: return EButton::Left;
		case 0x63: return EButton::Right;
		case 0x64: return EButton::Up;
		case 0x65: return EButton::Down;
		case 0x6A: return EButton::Select;
		case 0x6B: return EButton::Start;
		case 0x6C: return EButton::LStick;
		case 0x6D: return EButton::RStick;
		case 0x6E: return EButton::L1;
		case 0x6F: return EButton::L2;
		case 0x70: return EButton::R1;
		case 0x71: return EButton::R2;
		case 0x72: return EButton::Circle;
		case 0x73: return EButton::Triangle;
		case 0x74: return EButton::Cross;
		case 0x75: return EButton::Square;
		default: return EButton::None;
		}
	}

	const TCHAR* PadName(EButton B)
	{
		static const TCHAR* const Names[] = { TEXT(""), TEXT("cross"), TEXT("circle"), TEXT("square"), TEXT("triangle"), TEXT("l1"),
			TEXT("r1"), TEXT("l2"), TEXT("r2"), TEXT("start"), TEXT("select"), TEXT("up"), TEXT("down"), TEXT("left"), TEXT("right"),
			TEXT("lstick"), TEXT("rstick") };
		return Names[uint8(B)];
	}

	uint32 PadBit(EButton B)
	{
		using namespace SSXPadButton;
		switch (B)
		{
		case EButton::Cross: return Cross;
		case EButton::Circle: return Circle;
		case EButton::Square: return Square;
		case EButton::Triangle: return Triangle;
		case EButton::L1: return L1;
		case EButton::R1: return R1;
		case EButton::L2: return L2;
		case EButton::R2: return R2;
		case EButton::Start: return Start;
		case EButton::Select: return Select;
		case EButton::Up: return Up;
		case EButton::Down: return Down;
		case EButton::Left: return Left;
		case EButton::Right: return Right;
		default: return 0;
		}
	}

	/** Image name of a keyboard key in <Project>/Prompts/key, empty if the pack has none. */
	FString KeyImage(const FKey& K)
	{
		static const TMap<FKey, const TCHAR*> Special = {
			{ EKeys::SpaceBar, TEXT("space") }, { EKeys::Enter, TEXT("enter") }, { EKeys::Tab, TEXT("tab") },
			{ EKeys::Escape, TEXT("esc") }, { EKeys::BackSpace, TEXT("backspace") }, { EKeys::LeftShift, TEXT("shift") },
			{ EKeys::RightShift, TEXT("shift") }, { EKeys::LeftControl, TEXT("ctrl") }, { EKeys::RightControl, TEXT("ctrl") },
			{ EKeys::LeftAlt, TEXT("alt") }, { EKeys::RightAlt, TEXT("alt") }, { EKeys::CapsLock, TEXT("capslock") },
			{ EKeys::Up, TEXT("up") }, { EKeys::Down, TEXT("down") }, { EKeys::Left, TEXT("left") }, { EKeys::Right, TEXT("right") },
			{ EKeys::Home, TEXT("home") }, { EKeys::End, TEXT("end") }, { EKeys::PageUp, TEXT("pageup") },
			{ EKeys::PageDown, TEXT("pagedown") }, { EKeys::Insert, TEXT("ins") }, { EKeys::Delete, TEXT("del") },
			{ EKeys::Hyphen, TEXT("minus") }, { EKeys::Subtract, TEXT("minus") }, { EKeys::Add, TEXT("plus") },
			{ EKeys::Equals, TEXT("plus") }, { EKeys::Multiply, TEXT("asterisk") }, { EKeys::Asterix, TEXT("asterisk") },
			{ EKeys::Slash, TEXT("slash") }, { EKeys::Divide, TEXT("slash") }, { EKeys::Semicolon, TEXT("semicolon") },
			{ EKeys::Apostrophe, TEXT("quotation") }, { EKeys::Tilde, TEXT("tilde") }, { EKeys::LeftBracket, TEXT("brackets_l") },
			{ EKeys::RightBracket, TEXT("brackets_r") }, { EKeys::NumLock, TEXT("numlock") },
			{ EKeys::Zero, TEXT("0") }, { EKeys::One, TEXT("1") }, { EKeys::Two, TEXT("2") }, { EKeys::Three, TEXT("3") },
			{ EKeys::Four, TEXT("4") }, { EKeys::Five, TEXT("5") }, { EKeys::Six, TEXT("6") }, { EKeys::Seven, TEXT("7") }, { EKeys::Eight, TEXT("8") },
			{ EKeys::Nine, TEXT("9") } };
		if (const TCHAR* const* Name = Special.Find(K))
		{
			return *Name;
		}
		const FString N = K.GetFName().ToString();
		if (N.Len() == 1 && FChar::IsAlpha(N[0]))
		{
			return N.ToLower();
		}
		if (N.Len() >= 2 && N.Len() <= 3 && N[0] == TEXT('F') && FChar::IsDigit(N[1]))
		{
			return N.ToLower();
		}
		return FString();
	}

	/** The key on a prompt: the button's KEYBINDS key (primary, else alternate); in the menus "previous" (Triangle /
	 *  Select) shows Esc, which always goes back there. */
	FString KeyName(EButton B, ESSXPromptContext Ctx)
	{
		if (Ctx == ESSXPromptContext::Menu && (B == EButton::Triangle || B == EButton::Select))
		{
			return TEXT("esc");
		}
		const uint32 Bit = PadBit(B);
		const SSXKeyBinds::FSet& Kb = SSXKeyBinds::Current();
		for (int32 Row = 0; Bit && Row < SSXKeyBinds::NumButtons; ++Row)
		{
			if (SSXKeyBinds::Button(Row).Bit != Bit)
			{
				continue;
			}
			for (const FKey& K : Kb.Keys[Row])
			{
				const FString Img = K.IsValid() ? KeyImage(K) : FString();
				if (!Img.IsEmpty()) { return Img; }
			}
		}
		return FString();
	}

	ESSXPromptDevice PadDevice()
	{
		SSXPad::FSonyPadState State;
		uint16 Pid = 0;
		bool bBt = false;
		if (SSXPad::GetSonyPadDebug(State, Pid, bBt))
		{
			return Pid == 0x0CE6 || Pid == 0x0DF2 ? ESSXPromptDevice::PS5 : ESSXPromptDevice::PS4;
		}
		return ESSXPromptDevice::Xbox;
	}

	class FPromptInput : public IInputProcessor
	{
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
		{
			SSXPrompts::NoteInput(E.GetKey().IsGamepadKey());
			return false;
		}
		virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& E) override
		{
			if (E.GetKey().IsGamepadKey() && FMath::Abs(E.GetAnalogValue()) > 0.5f) { SSXPrompts::NoteInput(true); }
			return false;
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("SSXPrompts"); }
	};

	void EnsureTracking()
	{
		static bool bStarted = false;
		if (bStarted || !FSlateApplication::IsInitialized())
		{
			return;
		}
		bStarted = true;
		FSlateApplication::Get().RegisterInputPreProcessor(MakeShared<FPromptInput>());
		if (!GSeenInput && FSlateApplication::Get().IsGamepadAttached())
		{
			GLast = PadDevice();
		}
	}

	UTexture2D* LoadPrompt(const FString& Rel)
	{
		static TMap<FString, TStrongObjectPtr<UTexture2D>> Cache;
		if (const TStrongObjectPtr<UTexture2D>* Found = Cache.Find(Rel))
		{
			return Found->Get();
		}
		const FString Path = FPaths::Combine(SSXPrompts::Dir(), Rel + TEXT(".png"));
		UTexture2D* T = FPaths::FileExists(Path) ? FImageUtils::ImportFileAsTexture2D(Path) : nullptr;
		if (T)
		{
			T->SRGB = true;
			T->Filter = TF_Bilinear;
			T->AddressX = TA_Clamp;
			T->AddressY = TA_Clamp;
			T->UpdateResource();
		}
		Cache.Add(Rel, TStrongObjectPtr<UTexture2D>(T));
		return T;
	}
}

FString SSXPrompts::Dir()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Prompts"));
}

void SSXPrompts::NoteInput(bool bGamepad)
{
	GSeenInput = true;
	GLast = bGamepad ? PadDevice() : ESSXPromptDevice::Keyboard;
}

ESSXPromptDevice SSXPrompts::Device()
{
	switch (CVarPrompts.GetValueOnGameThread())
	{
	case 0: return ESSXPromptDevice::PS2;
	case 2: return ESSXPromptDevice::Keyboard;
	case 3: return ESSXPromptDevice::Xbox;
	case 4: return ESSXPromptDevice::PS4;
	case 5: return ESSXPromptDevice::PS5;
	default:
		if (GIsAutomationTesting) { return ESSXPromptDevice::PS2; }   // draw-list tests compare against the original
		EnsureTracking();
		return GLast;
	}
}

FString SSXPrompts::ImageFor(int32 Id, bool bHud, ESSXPromptContext Ctx, ESSXPromptDevice Dev)
{
	const EButton B = ButtonOf(Id, bHud);
	if (B == EButton::None || Ctx == ESSXPromptContext::Original || Dev == ESSXPromptDevice::PS2)
	{
		return FString();
	}
	if (Dev == ESSXPromptDevice::Keyboard)
	{
		const FString K = KeyName(B, Ctx);
		return K.IsEmpty() ? FString() : FString(TEXT("key/")) + K;
	}
	const TCHAR* Set = Dev == ESSXPromptDevice::Xbox ? TEXT("xbox/") : Dev == ESSXPromptDevice::PS4 ? TEXT("ps4/") : TEXT("ps5/");
	return FString(Set) + PadName(B);
}

bool SSXPrompts::Substitute(int32 Id, bool bHud, ESSXPromptContext Ctx, FSSXSprite& Sprite, UTexture*& Page)
{
	const FString Rel = ImageFor(Id, bHud, Ctx, Device());
	UTexture2D* T = Rel.IsEmpty() ? nullptr : LoadPrompt(Rel);
	if (!T || T->GetSizeY() <= 0)
	{
		return false;
	}
	Sprite.U0 = Sprite.V0 = 0.f;
	Sprite.U1 = Sprite.V1 = 1.f;
	Sprite.FitAspect = float(T->GetSizeX()) / float(T->GetSizeY());
	Page = T;
	return true;
}
