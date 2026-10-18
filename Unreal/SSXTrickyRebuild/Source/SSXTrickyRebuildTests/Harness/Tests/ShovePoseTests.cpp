// 0x179940, 0x127918, 0x201138
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Input/SSXInputSetup.h"
#include "Characters/SSXCharacterAppearance.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FShovePose
	{
		int32 Ch1 = -1;
		FString Clip;
		double MaxNose = -1e9;
		double MinNose = 1e9;
		double BoardNoseDotFwd = 0.0;
		double BoardRow0DotFwd = 0.0;
		double DrawnNoseDotFwd = 0.0;
	};

	FShovePose RunShove(const SSXAnim::FAflLibrary* Lib, const TCHAR* RiderId, int32 Character, bool bGoofy, bool bUp, FString& Log)
	{
		FShovePose R;
		FPatchWorld World;
		SSXRiderSetup::BuildSlopeWorld(World, 0.0, 1);
		FRiderSim Sim;
		FRiderInputDriver Input;
		SSXRiderSetup::InstallStates(Sim, Input, false);
		Sim.SetWorld(&World);
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, Character, 0);
		Sim.Profile.Stance = bGoofy ? 1 : 0;
		Sim.Reset();
		Sim.Spawn(FVector::ZeroVector, FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
		Sim.State.Velocity = 800.0 * Sim.State.ContactForward;
		Sim.State.bStanceGoofy = bGoofy;
		SSXAnim::FRiderAnimDriver Anim;
		Anim.Init(Lib, RiderId, 0, Character);
		FPadMapper Mapper(Character, 0);
		Mapper.SetLayout(0);
		FSSXPadDevice Pad;
		const SSXAnim::FAflSkeleton* Sk = Anim.GetSkeleton();
		auto BoneIndex = [Sk](const TCHAR* Name)
		{
			for (int32 I = 0; Sk && I < Sk->Bones.Num(); ++I)
			{
				if (Sk->Bones[I].Name == Name) { return I; }
			}
			return int32(INDEX_NONE);
		};
		const int32 Hips = BoneIndex(TEXT("hips")), LHand = BoneIndex(TEXT("l_hand")), RHand = BoneIndex(TEXT("r_hand"));
		bool bStarted = false;
		for (int32 F = 0; F < 90; ++F)
		{
			const FVector2D Right = (F >= 20 && F < 24) ? FVector2D(0.0, bUp ? 1.0 : -1.0) : FVector2D::ZeroVector;
			const FPadActions A = Mapper.Step(Pad.Update(SSXInput::HardwareFromUE(0, FVector2D::ZeroVector, Right)));
			Input.Step(Sim, A);
			Anim.Step(Sim);
			const int32 Ch1 = Anim.GetController().CurrentId(1);
			if (Ch1 >= SSXAnim::ShovePushTS && Ch1 <= SSXAnim::ShoveCombatBwd)
			{
				if (!bStarted)
				{
					bStarted = true;
					R.Ch1 = Ch1;
					if (const SSXAnim::FPlayInstance* I = Anim.GetController().Newest(1))
					{
						const SSXAnim::FAflClip* C = Lib->Clip(I->Subs[0].Clip);
						R.Clip = C ? C->Name : FString();
					}
				}
				SSXAnim::FRiderPalette P;
				Anim.ComputePalette(Sim, P);
				if (P.bValid && Hips >= 0 && LHand >= 0 && RHand >= 0)
				{
					const FVector Nose = Sim.State.RenderRow1.GetSafeNormal();
					R.BoardNoseDotFwd = FVector::DotProduct(Nose, Sim.State.BasisForward);
					R.BoardRow0DotFwd = FVector::DotProduct(FVector(P.Board.Row(0)).GetSafeNormal(), Nose);
					FMatrix MeshToGame;
					if (USSXCharacterAppearance::BoardMeshToGame(*Lib, 0, P.Board, Sim.Profile.SizeScale, MeshToGame, Sim.Profile.Stance != 0))
					{
						R.DrawnNoseDotFwd = FVector::DotProduct(MeshToGame.TransformVector(FVector(1, 0, 0)).GetSafeNormal(), Nose);
					}
					const FVector H(P.Body[Hips].Row(3));
					for (const int32 B : { LHand, RHand })
					{
						const double D = FVector::DotProduct(FVector(P.Body[B].Row(3)) - H, Nose);
						R.MaxNose = FMath::Max(R.MaxNose, D);
						R.MinNose = FMath::Min(R.MinNose, D);
					}
				}
			}
		}
		Log += FString::Printf(TEXT("%s %s stick %s: ch1 %#x %s, hands along the nose [%.1f, %.1f], body +Y . fwd %.3f, board row 0 . +Y %.3f, drawn mesh +X . +Y %.3f\n"),
			RiderId, bGoofy ? TEXT("goofy") : TEXT("regular"), bUp ? TEXT("up") : TEXT("down"), R.Ch1, *R.Clip, R.MinNose, R.MaxNose,
			R.BoardNoseDotFwd, R.BoardRow0DotFwd, R.DrawnNoseDotFwd);
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimShovePoseTest, "SSX.Anim.ShovePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAnimShovePoseTest::RunTest(const FString& Parameters)
{
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	if (!Lib)
	{
		SSXTest::Skip(*this, TEXT("no exported animation library: skipped"));
		return true;
	}
	FString Log;
	struct FCase { const TCHAR* Id; int32 Character; };
	for (const FCase& C : { FCase{ TEXT("eddie"), 0 }, FCase{ TEXT("brodi"), 10 } })
	{
		for (int32 Goofy = 0; Goofy < 2; ++Goofy)
		{
			for (int32 Up = 1; Up >= 0; --Up)
			{
				const FShovePose R = RunShove(Lib, C.Id, C.Character, Goofy != 0, Up != 0, Log);
				const FString Tag = FString::Printf(TEXT("%s %s stick %s"), C.Id, Goofy ? TEXT("goofy") : TEXT("regular"), Up ? TEXT("up") : TEXT("down"));
				TestEqual(Tag + TEXT(": channel-1 shove id"), R.Ch1, Up ? SSXAnim::ShoveCombatFwd : SSXAnim::ShoveCombatBwd);
				TestTrue(Tag + TEXT(": clip ") + R.Clip, R.Clip.EndsWith(Up ? TEXT("R_COMBATFWD1") : TEXT("R_COMBATBWD1")));
				TestTrue(Tag + TEXT(": body +Y = travel direction"), R.BoardNoseDotFwd > 0.9);
				// 0x201138
				TestTrue(Tag + TEXT(": drawn board nose ahead (goofy: the Goofy model under the yawed board matrix)"), R.DrawnNoseDotFwd > 0.9);
				if (Up)
				{
					TestTrue(Tag + TEXT(": a hand reaches > 50 cm toward the nose"), R.MaxNose > 50.0);
					TestTrue(Tag + TEXT(": no hand reaches > 50 cm toward the tail"), R.MinNose > -50.0);
				}
				else
				{
					TestTrue(Tag + TEXT(": a hand reaches > 50 cm toward the tail"), R.MinNose < -50.0);
				}
			}
		}
	}
	AddInfo(Log);
	return true;
}

#endif
