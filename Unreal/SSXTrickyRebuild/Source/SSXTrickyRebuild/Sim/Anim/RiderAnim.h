#pragma once

#include "CoreMinimal.h"

class FRiderSim;
struct FRiderState;

namespace SSXAnim
{
	constexpr int32 GateToBase = 0x1FD;
	constexpr int32 SitUp = 0x1FE;
	constexpr int32 SwitchFakie = 0x200;
	constexpr int32 UnSwitch = 0x201;
	constexpr int32 TurnAroundHS = 0x203;
	constexpr int32 TurnAroundTS = 0x204;
	constexpr int32 SkatePush = 0x221;
	constexpr int32 AirIdle = 0x223;
	constexpr int32 AirAuto180 = 0x225;
	constexpr int32 LandAtomic = 0x23E;
	constexpr int32 LandAtomicBwd = 0x23F;
	constexpr int32 LandFakie = 0x240;
	constexpr int32 LandTail = 0x242;
	constexpr int32 LandTailBwd = 0x243;
	constexpr int32 BumpBwd = 0x244;
	constexpr int32 BumpFwd = 0x245;
	constexpr int32 GetUpRollBwd = 0x24A;
	constexpr int32 GetUpDefault = 0x24C;

	constexpr int32 CategoryGrab = 14;
	constexpr int32 CategoryTweak = 15;
	constexpr int32 CategoryUber = 16;

	constexpr double AdvancePerFrame = 0.5;   // animation frames per game frame at speed 1 (30 fps data)
	constexpr double GrabSpeedMin = 0.78212035;
	constexpr double GrabSpeedMax = 0.78212035 + 0.8057834;
	constexpr int32 NoTrickId = 26;

	constexpr int32 OneShotFrames(int32 NFrames) { return NFrames > 0 ? 2 * (NFrames - 1) : 0; }
	// a one-shot of n frames is done after 2 (n - 1) game frames; a control waiting for it lasts 2 (n - 1) + 1
	constexpr int32 ControlFrames(int32 NFrames) { return NFrames > 0 ? OneShotFrames(NFrames) + 1 : 0; }

	constexpr int32 RailSwitchFirst = 0x258;
	constexpr int32 RailSwitchLast = 0x25F;
	constexpr int32 ShovePushTS = 0x205;
	constexpr int32 ShoveElbowHS = 0x206;
	constexpr int32 ShoveCombatFwd = 0x207;
	constexpr int32 ShoveCombatBwd = 0x208;
	constexpr int32 CategoryShove = 0x15;

	// 0x161400
	SSXTRICKYREBUILD_API int32 TrickPhaseChain(int32 IntoId);
	// 0x102800
	SSXTRICKYREBUILD_API int32 SpinPlayOutId(int32 Channel0Id);
	// 0x126FC0
	SSXTRICKYREBUILD_API int32 IntoFromTakeoff(int32 TakeoffId);
	constexpr bool IsSpinInto(int32 Id) { return (Id >= 0x226 && Id <= 0x22D) || (Id >= 0x270 && Id <= 0x277); }
	constexpr bool IsSpinCycle(int32 Id) { return Id >= 0x22E && Id <= 0x235; }
	constexpr bool IsHalfSpinPlayOut(int32 Id) { return Id == 0x23C || Id == 0x23D; }

	SSXTRICKYREBUILD_API int32 DefaultOneShotFrames(int32 AnimId, int32 BoardClass);

	// 0x15ADA8, 0x3BF180
	SSXTRICKYREBUILD_API int32 RandomVariantCount(int32 AnimId);
	constexpr int32 LandBobbleAnim = 0x216;   // 0x127D08

	SSXTRICKYREBUILD_API double GrabSpeed(double Stat14);
	// 0x15A030
	SSXTRICKYREBUILD_API double ClockStep(double Frame, double Speed);

	SSXTRICKYREBUILD_API int32 FramesInControl(const FRiderState& State);

	struct FAnimData
	{
		int32 AnimId = -1;
		int32 NFrames = 0;
		int32 Markers[4] = { -1, -1, -1, -1 };   // flag markers 1..4, -1 = none
		int32 Category = 0;

		bool IsValid() const { return NFrames > 0; }
	};

	struct FTrickRow
	{
		int32 GrabId = NoTrickId;
		FAnimData Grab, Tweak, Uber;
	};

	struct SSXTRICKYREBUILD_API FAnimInstance
	{
		FAnimData Data;
		double Speed = 1.0;
		double Frame = 0.0;
		double Hold = -1.0;   // hold frame, < 0 = none
		uint8 Flags = 0;   // bit k-1 = flag k as of the last Advance
		uint8 Edges = 0;   // bit k-1 = marker k crossed by the last Advance

		bool IsActive() const { return Data.IsValid(); }
		int32 Category() const { return Data.IsValid() ? Data.Category : 0; }
		bool Flag(int32 K) const { return ((Flags >> (K - 1)) & 1) != 0; }
		bool Edge(int32 K) const { return ((Edges >> (K - 1)) & 1) != 0; }
		bool IsDone() const { return Frame >= Data.NFrames - 1; }
		bool bEnded = false;

		void Start(const FAnimData& InData, double InSpeed, double InHold);
		void Stop() { *this = FAnimInstance(); }
		void Advance();
	};

	class SSXTRICKYREBUILD_API FAnimTable
	{
	public:
		static constexpr int32 NumBoardClasses = 3;
		static constexpr int32 NumCharacters = 12;
		static constexpr int32 NumSlots = 15;
		static constexpr int32 NumVariants = 3;

		FAnimTable() { SetDefaults(); }

		static FString DefaultPath();
		static const FAnimTable& Shared();

		bool Load(const FString& Path, FString* OutError = nullptr);
		void SetDefaults();
		bool IsFromDisc() const { return bFromDisc; }

		FTrickRow TrickRow(int32 BoardClass, int32 Character, int32 GrabId) const;
		int32 Frames(int32 AnimId, int32 BoardClass, int32 Character = 0) const;

	private:
		struct FOneShot
		{
			int32 AnimId = 0;
			uint16 N[NumBoardClasses][NumCharacters][NumVariants] = {};
		};
		TArray<FTrickRow> Rows;
		TArray<FOneShot> OneShots;
		bool bFromDisc = false;
	};

	struct SSXTRICKYREBUILD_API FRiderAnimChannel
	{
		FAnimInstance Trick;
		int32 LandHardAnim = LandTail;
		int32 BumpAnim = BumpFwd;
		int32 GetUpAnim = GetUpDefault;
		int32 Auto180FramesLeft = 0;
		// 0x156DE0
		TFunction<double(bool& bPending)> CrashScoring;
		const FAnimTable* Table = nullptr;

		TArray<int32, TInlineAllocator<8>> PlayRequests;
		TArray<double, TInlineAllocator<8>> PlayRequestSpeeds;
		// 0x15EEF8
		double LayerYawAdd = 0.0;
		// 0x15EF48
		bool bClearLayerMirrors = false;
		uint32 TrickStarts = 0;
		int32 RequestPlay(int32 AnimId, double Speed = -1.0)
		{
			const int32 Variant = DrawVariant(AnimId);
			if (PlayRequestSpeeds.Num() != PlayRequests.Num()) { PlayRequestSpeeds.Init(-1.0, PlayRequests.Num()); }
			if (PlayRequests.Num() >= 8) { PlayRequests.RemoveAt(0); PlayRequestSpeeds.RemoveAt(0); }
			PlayRequests.Add(AnimId);
			PlayRequestSpeeds.Add(Speed);
			return Variant;
		}
		// 0x15EE30, 0x11A840
		static constexpr int32 ResetRequest = -1;
		void RequestReset()
		{
			ClearPlayRequests();
			PlayRequests.Add(ResetRequest);
			PlayRequestSpeeds.Add(-1.0);
		}
		// 0x15F050, 0x15A600
		static constexpr int32 FadeChannel1Request = -2;
		// 0x1272E8, 0x108378
		void FadeChannel1(FRiderSim& Sim);
		void LandBobble(FRiderSim& Sim);
		TArray<TPair<int32, uint32>, TInlineAllocator<4>> PreDrawn;
		int32 DrawVariant(int32 AnimId);
		bool TakeDraw(int32 AnimId, uint32& OutR);
		int32 PlayOverlay(FRiderSim& Sim, int32 AnimId);
		double PlayRequestSpeed(int32 Index) const
		{
			return PlayRequestSpeeds.Num() == PlayRequests.Num() && PlayRequestSpeeds.IsValidIndex(Index) ? PlayRequestSpeeds[Index] : -1.0;
		}
		void ClearPlayRequests() { PlayRequests.Reset(); PlayRequestSpeeds.Reset(); PreDrawn.Reset(); }
		// 0x11CAE8, 0x116AB8
		bool bResetAnim = false;
		// 0x169BE8
		TArray<int32, TInlineAllocator<2>> ScenePlays;
		void RequestScenePlay(int32 LineId)
		{
			if (ScenePlays.Num() >= 4) { ScenePlays.RemoveAt(0); }
			ScenePlays.Add(LineId);
		}

		const FAnimTable& GetTable() const { return Table ? *Table : FAnimTable::Shared(); }
		int32 Frames(const FRiderSim& Sim, int32 AnimId) const;
		int32 ControlFramesFor(const FRiderSim& Sim, int32 AnimId, int32 Fallback) const;
	};

	SSXTRICKYREBUILD_API TSharedRef<FRiderAnimChannel> AcquireChannel(const FRiderSim& RegistrationSim);
	SSXTRICKYREBUILD_API FRiderAnimChannel* FindChannel(const FRiderSim& RegistrationSim);
}
