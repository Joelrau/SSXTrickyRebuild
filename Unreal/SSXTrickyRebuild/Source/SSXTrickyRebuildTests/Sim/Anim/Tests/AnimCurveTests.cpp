#include "Characters/SSXCharacterLibrary.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/AnimController.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Anim/RiderPose.h"
#include "Sim/Ground/GroundStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXAnim;

	constexpr EAutomationTestFlags AnimTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	constexpr double TolSampleDeg = 1e-4, TolSampleCm = 1e-4;
	constexpr double TolPaletteDeg = 0.01, TolPaletteCm = 0.01;
	constexpr double TolFrame = 1e-3, TolWeight = 1e-5, TolFoot = 1e-5;

	TSharedPtr<FJsonObject> LoadReference(FAutomationTestBase& T)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("anim_reference.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			SSXTest::Skip(T, FString::Printf(TEXT("%s missing: run tools/export_anim_clips.py and tools/make_anim_reference.py; skipped"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			T.AddError(TEXT("anim_reference.json does not parse"));
			return nullptr;
		}
		return Root;
	}

	const FAflLibrary* Library(FAutomationTestBase& T)
	{
		const FAflLibrary* Lib = FAflLibrary::Shared();
		if (!Lib)
		{
			SSXTest::Skip(T, TEXT("SimData/anim_clips.bin missing (tools/export_anim_clips.py); skipped"));
		}
		return Lib;
	}

	FVector3d Vec(const TSharedPtr<FJsonValue>& V)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		return FVector3d(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
	}

	TArray<FVector3d> Vecs(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		TArray<FVector3d> Out;
		for (const TSharedPtr<FJsonValue>& V : A) { Out.Add(Vec(V)); }
		return Out;
	}

	void Mat44(const TSharedPtr<FJsonValue>& V, double M[4][4])
	{
		const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
		for (int32 I = 0; I < 4; ++I)
		{
			const TArray<TSharedPtr<FJsonValue>>& C = R[I]->AsArray();
			for (int32 J = 0; J < 4; ++J) { M[I][J] = C[J]->AsNumber(); }
		}
	}

	FMatrix EulerMatrix(const FVector3d& E)
	{
		const double X = -E.X, Y = -E.Y, Z = -E.Z;
		const double Cx = FMath::Cos(X), Sx = FMath::Sin(X), Cy = FMath::Cos(Y), Sy = FMath::Sin(Y), Cz = FMath::Cos(Z), Sz = FMath::Sin(Z);
		const double Rx[3][3] = { { 1, 0, 0 }, { 0, Cx, -Sx }, { 0, Sx, Cx } };
		const double Ry[3][3] = { { Cy, 0, Sy }, { 0, 1, 0 }, { -Sy, 0, Cy } };
		const double Rz[3][3] = { { Cz, -Sz, 0 }, { Sz, Cz, 0 }, { 0, 0, 1 } };
		double T[3][3], R[3][3];
		for (int32 I = 0; I < 3; ++I) { for (int32 J = 0; J < 3; ++J) { T[I][J] = 0; for (int32 K = 0; K < 3; ++K) { T[I][J] += Ry[I][K] * Rx[K][J]; } } }
		for (int32 I = 0; I < 3; ++I) { for (int32 J = 0; J < 3; ++J) { R[I][J] = 0; for (int32 K = 0; K < 3; ++K) { R[I][J] += Rz[I][K] * T[K][J]; } } }
		FMatrix M = FMatrix::Identity;
		for (int32 I = 0; I < 3; ++I) { for (int32 J = 0; J < 3; ++J) { M.M[I][J] = R[I][J]; } }
		return M;
	}

	/** Chord metric 2 asin(|A - B|_F / (2 sqrt 2)) in degrees over the upper 3x3. */
	double RotDiffDeg(const double A[4][4], const double B[4][4])
	{
		double S = 0.0;
		for (int32 I = 0; I < 3; ++I) { for (int32 J = 0; J < 3; ++J) { S += FMath::Square(A[I][J] - B[I][J]); } }
		return FMath::RadiansToDegrees(2.0 * FMath::Asin(FMath::Min(1.0, FMath::Sqrt(S) / (2.0 * UE_DOUBLE_SQRT_2))));
	}

	double EulerDiffDeg(const FVector3d& A, const FVector3d& B)
	{
		const FMatrix Ma = EulerMatrix(A), Mb = EulerMatrix(B);
		double X[4][4], Y[4][4];
		for (int32 I = 0; I < 4; ++I) { for (int32 J = 0; J < 4; ++J) { X[I][J] = Ma.M[I][J]; Y[I][J] = Mb.M[I][J]; } }
		return RotDiffDeg(X, Y);
	}

	struct FMaxErr
	{
		double Rot = 0.0, Pos = 0.0;
		void Add(double R, double P) { Rot = FMath::Max(Rot, R); Pos = FMath::Max(Pos, P); }
	};

	void ComparePose(const TArray<FBonePose>& Got, const TArray<FVector3d>& E, const TArray<FVector3d>& T, FMaxErr& Err)
	{
		for (int32 I = 0; I < Got.Num() && I < E.Num(); ++I)
		{
			Err.Add(EulerDiffDeg(Got[I].Euler, E[I]), (Got[I].Trans - T[I]).Size());
		}
	}

	const FAflSkeleton* Skel(FAutomationTestBase& T, const FAflLibrary& Lib, const FString& Rider)
	{
		const FAflSkeleton* S = Lib.Skeleton(Rider);
		T.TestNotNull(*FString::Printf(TEXT("skeleton %s"), *Rider), S);
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimCurvesTest, "SSX.Anim.Curves", AnimTestFlags)
bool FSSXAnimCurvesTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	const FAflLibrary* Lib = Ref ? Library(*this) : nullptr;
	if (!Ref || !Lib)
	{
		return true;
	}
	FMaxErr Err;
	int32 Cases = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("sample")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		const FString Rider = C->GetStringField(TEXT("rider"));
		const FString Name = C->GetStringField(TEXT("clip"));
		const FAflClip* Clip = Lib->Clip(Lib->FindClip(Name));
		const FAflSkeleton* Body = Skel(*this, *Lib, Rider);
		if (!TestNotNull(*FString::Printf(TEXT("clip %s"), *Name), Clip) || !Body)
		{
			continue;
		}
		const double Frame = C->GetNumberField(TEXT("frame")), Yaw = C->GetNumberField(TEXT("yaw")), Ts = C->GetNumberField(TEXT("trans_scale"));
		const bool bMirror = C->GetBoolField(TEXT("mirror"));
		for (int32 Part = 0; Part < 2; ++Part)
		{
			const TSharedPtr<FJsonObject>* Exp = nullptr;
			if (!C->TryGetObjectField(Part == 0 ? TEXT("body") : TEXT("board"), Exp) || !Clip->Block(Part))
			{
				continue;
			}
			TArray<FBonePose> Got;
			SampleBlock(*Lib, *Clip->Block(Part), Part == 0 ? *Body : *Lib->BoardSkeleton(), Frame, bMirror, Yaw, Ts, 1.0, Got);
			FMaxErr E1;
			ComparePose(Got, Vecs((*Exp)->GetArrayField(TEXT("euler"))), Vecs((*Exp)->GetArrayField(TEXT("trans"))), E1);
			if (E1.Rot > TolSampleDeg || E1.Pos > TolSampleCm)
			{
				AddError(FString::Printf(TEXT("sample %s %s @%.3f part %d: %.6f deg / %.6f cm"), *Rider, *Name, Frame, Part, E1.Rot, E1.Pos));
			}
			Err.Add(E1.Rot, E1.Pos);
			++Cases;
		}
	}
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("blend")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		const FAflSkeleton* Body = Skel(*this, *Lib, C->GetStringField(TEXT("rider")));
		if (!Body)
		{
			continue;
		}
		TArray<FPoseLayer> Layers;
		for (const TSharedPtr<FJsonValue>& L : C->GetArrayField(TEXT("layers")))
		{
			const TSharedPtr<FJsonObject> O = L->AsObject();
			const FAflClip* Clip = Lib->Clip(Lib->FindClip(O->GetStringField(TEXT("clip"))));
			if (!TestNotNull(TEXT("blend clip"), Clip))
			{
				continue;
			}
			FPoseLayer& P = Layers.AddDefaulted_GetRef();
			P.Block = Clip->Block(0);
			P.Frame = O->GetNumberField(TEXT("frame"));
			P.Weight = O->GetNumberField(TEXT("weight"));
			P.bMirror = O->GetBoolField(TEXT("mirror"));
			P.Yaw = O->GetNumberField(TEXT("yaw"));
		}
		TArray<FBonePose> Got;
		BlendLayers(*Lib, *Body, Layers, 1.0, 1.0, 0, Got);
		FMaxErr E1;
		ComparePose(Got, Vecs(C->GetArrayField(TEXT("euler"))), Vecs(C->GetArrayField(TEXT("trans"))), E1);
		if (E1.Rot > TolSampleDeg || E1.Pos > TolSampleCm)
		{
			AddError(FString::Printf(TEXT("blend %s: %.6f deg / %.6f cm"), *C->GetStringField(TEXT("rider")), E1.Rot, E1.Pos));
		}
		Err.Add(E1.Rot, E1.Pos);
		++Cases;
	}
	TestTrue(TEXT("cases compared"), Cases >= 10);
	AddInfo(FString::Printf(TEXT("curves: %d cases, max %.2e deg / %.2e cm"), Cases, Err.Rot, Err.Pos));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimProceduralTest, "SSX.Anim.Procedural", AnimTestFlags)
bool FSSXAnimProceduralTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	const FAflLibrary* Lib = Ref ? Library(*this) : nullptr;
	if (!Ref || !Lib)
	{
		return true;
	}
	int32 Cases = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("procedural")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		const TSharedPtr<FJsonObject> St = C->GetObjectField(TEXT("st"));
		FAnimDriveInputs In;
		In.SteerFollow = St->GetNumberField(TEXT("steer_follow"));
		In.Brake = St->GetNumberField(TEXT("brake"));
		In.Crouch = St->GetNumberField(TEXT("crouch"));
		In.bGoofy = St->GetBoolField(TEXT("goofy"));
		In.AirTurn = St->GetNumberField(TEXT("air_turn"));
		In.AirTilt = St->GetNumberField(TEXT("air_tilt"));
		In.Ramp1F0 = St->GetNumberField(TEXT("ramp_1f0"));
		In.Steer = St->GetNumberField(TEXT("steer"));
		In.GrindType = int32(St->GetNumberField(TEXT("grind_type")));
		In.PrewindA = St->GetNumberField(TEXT("pw_x"));
		In.PrewindB = St->GetNumberField(TEXT("pw_y"));
		const int32 Mode = int32(C->GetNumberField(TEXT("mode")));
		const int32 Bc = int32(C->GetNumberField(TEXT("board_class")));
		const FProceduralSubs R = EvalProcedural(*Lib, Mode, In, Bc, INDEX_NONE);
		const TArray<TSharedPtr<FJsonValue>>& Subs = C->GetArrayField(TEXT("subs"));
		const FString Tag = FString::Printf(TEXT("mode %d class %d case %d"), Mode, Bc, Cases);
		TestEqual(*(Tag + TEXT(" subs")), R.Num, Subs.Num());
		TestEqual(*(Tag + TEXT(" clock")), R.bClock, C->GetBoolField(TEXT("clock")));
		TestEqual(*(Tag + TEXT(" cycle")), R.bCycle, C->GetBoolField(TEXT("cycle")));
		for (int32 K = 0; K < Subs.Num() && K < R.Num; ++K)
		{
			const TSharedPtr<FJsonObject> S = Subs[K]->AsObject();
			const FAflClip* Clip = Lib->Clip(R.Subs[K].Clip);
			TestEqual(*FString::Printf(TEXT("%s sub %d clip"), *Tag, K), Clip ? Clip->Name : FString(TEXT("<none>")), S->GetStringField(TEXT("clip")));
			TestTrue(*FString::Printf(TEXT("%s sub %d weight"), *Tag, K), FMath::Abs(R.Subs[K].Weight - S->GetNumberField(TEXT("weight"))) <= TolWeight);
			double F = 0.0;
			if (S->TryGetNumberField(TEXT("frame"), F))
			{
				TestTrue(*FString::Printf(TEXT("%s sub %d frame %.5f vs %.5f"), *Tag, K, R.Subs[K].Frame, F), FMath::Abs(R.Subs[K].Frame - F) <= TolFrame);
			}
			else
			{
				TestTrue(*FString::Printf(TEXT("%s sub %d on the clock"), *Tag, K), R.Subs[K].Frame < 0.0);
			}
		}
		++Cases;
	}
	TestTrue(TEXT("cases compared"), Cases >= 30);
	AddInfo(FString::Printf(TEXT("procedural: %d cases"), Cases));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimPaletteTest, "SSX.Anim.Palette", AnimTestFlags)
bool FSSXAnimPaletteTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	const FAflLibrary* Lib = Ref ? Library(*this) : nullptr;
	if (!Ref || !Lib)
	{
		return true;
	}
	double FootErr = 0.0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("foot")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		const FAflSkeleton* Body = Skel(*this, *Lib, C->GetStringField(TEXT("rider")));
		double L[3][3], R[3][3];
		if (!Body || !TestTrue(TEXT("foot rotations"), FootRotations(*Lib, *Body, int32(C->GetNumberField(TEXT("board_class"))), C->GetBoolField(TEXT("mirror")), L, R)))
		{
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>& El = C->GetArrayField(TEXT("l"));
		const TArray<TSharedPtr<FJsonValue>>& Er = C->GetArrayField(TEXT("r"));
		for (int32 I = 0; I < 3; ++I)
		{
			const FVector3d A = Vec(El[I]), B = Vec(Er[I]);
			for (int32 J = 0; J < 3; ++J)
			{
				FootErr = FMath::Max(FootErr, FMath::Max(FMath::Abs(L[I][J] - A[J]), FMath::Abs(R[I][J] - B[J])));
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("foot rotation constants (max %.2e)"), FootErr), FootErr <= TolFoot);

	FMaxErr Err, PoseErr;
	int32 Cases = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("palette")))
	{
		const TSharedPtr<FJsonObject> C = V->AsObject();
		const FString Rider = C->GetStringField(TEXT("rider"));
		const int32 Bc = int32(C->GetNumberField(TEXT("board_class")));
		const bool bMirror = C->GetBoolField(TEXT("mirror"));
		const double Scale = C->GetNumberField(TEXT("scale"));
		const FAflSkeleton* Body = Skel(*this, *Lib, Rider);
		FRiderSkeletonSet Set;
		if (!Body || !TestTrue(TEXT("skeleton set"), Set.Init(*Lib, Body, Bc)))
		{
			continue;
		}
		TArray<FPoseLayer> BodyLayers, BoardLayers;
		for (const TSharedPtr<FJsonValue>& L : C->GetArrayField(TEXT("layers")))
		{
			const TSharedPtr<FJsonObject> O = L->AsObject();
			const FAflClip* Clip = Lib->Clip(Lib->FindClip(O->GetStringField(TEXT("clip"))));
			if (!TestNotNull(TEXT("palette clip"), Clip))
			{
				continue;
			}
			for (int32 Part = 0; Part < 2; ++Part)
			{
				FPoseLayer& P = (Part == 0 ? BodyLayers : BoardLayers).AddDefaulted_GetRef();
				P.Block = Clip->Block(Part);
				P.Frame = O->GetNumberField(TEXT("frame"));
				P.Weight = O->GetNumberField(TEXT("weight"));
				P.bMirror = bMirror;
				P.Yaw = bMirror ? -UE_DOUBLE_PI : 0.0;
			}
		}
		TArray<FBonePose> Pose, Board;
		BlendLayers(*Lib, *Body, BodyLayers, Scale, Scale, 0, Pose);
		BlendLayers(*Lib, *Lib->BoardSkeleton(), BoardLayers, Scale, 1.0, 0, Board);
		ComparePose(Pose, Vecs(C->GetArrayField(TEXT("euler"))), Vecs(C->GetArrayField(TEXT("trans"))), PoseErr);
		TArray<FBonePose> BoardExp;
		BoardExp.SetNum(1);
		BoardExp[0].Euler = Vec(C->GetField<EJson::Array>(TEXT("board_euler")));
		BoardExp[0].Trans = Vec(C->GetField<EJson::Array>(TEXT("board_trans")));
		PoseErr.Add(EulerDiffDeg(Board[0].Euler, BoardExp[0].Euler), (Board[0].Trans - BoardExp[0].Trans).Size());

		const TSharedPtr<FJsonObject> St = C->GetObjectField(TEXT("state"));
		FRiderFrameInput In;
		const TArray<TSharedPtr<FJsonValue>>& Q = St->GetArrayField(TEXT("quat"));
		In.Orientation = FQuat(Q[1]->AsNumber(), Q[2]->AsNumber(), Q[3]->AsNumber(), Q[0]->AsNumber());
		In.Position = Vec(St->GetField<EJson::Array>(TEXT("pos")));
		In.Steer = St->GetNumberField(TEXT("steer"));
		In.Motion = int32(St->GetNumberField(TEXT("motion")));
		In.BoardClass = Bc;
		In.Up = Vec(St->GetField<EJson::Array>(TEXT("v2c0")));
		In.S308 = St->GetNumberField(TEXT("s308"));
		In.Left = Vec(St->GetField<EJson::Array>(TEXT("v330")));
		In.Scale = Scale;
		In.Lean = St->GetNumberField(TEXT("lean"));
		In.Spin = St->GetNumberField(TEXT("spin"));
		In.Flip = St->GetNumberField(TEXT("flip"));
		In.Suspension = St->GetNumberField(TEXT("susp"));
		In.BoardChatter = St->GetNumberField(TEXT("s1cc"));
		In.GroundNormal = Vec(St->GetField<EJson::Array>(TEXT("v2a0")));
		TArray<FBonePose> RefPose;
		{
			const TArray<FVector3d> E = Vecs(C->GetArrayField(TEXT("euler"))), T = Vecs(C->GetArrayField(TEXT("trans")));
			RefPose.SetNum(E.Num());
			for (int32 I = 0; I < E.Num(); ++I) { RefPose[I].Euler = E[I]; RefPose[I].Trans = T[I]; }
		}
		const FRowMat Root0 = RootFrame(In, RefPose[0].Trans.Z, false);
		const FRowMat BoardW = BoardFrame(Root0, BoardExp[0], In);
		const FRowMat Root = RootFrame(In, RefPose[0].Trans.Z, true);
		TArray<FRowMat> Pal;
		BuildPalette(*Lib, Set, Root, RefPose, BoardW, bMirror, Scale, Pal);

		auto CompareMat = [&](const FRowMat& Got, const TSharedPtr<FJsonValue>& Exp, const TCHAR* What, int32 Bone)
		{
			double E[4][4], G[4][4];
			Mat44(Exp, E);
			for (int32 I = 0; I < 4; ++I) { for (int32 J = 0; J < 4; ++J) { G[I][J] = Got.M[I][J]; } }
			const double R = RotDiffDeg(G, E);
			const double P = FVector3d(G[3][0] - E[3][0], G[3][1] - E[3][1], G[3][2] - E[3][2]).Size();
			if (R > TolPaletteDeg || P > TolPaletteCm)
			{
				AddError(FString::Printf(TEXT("palette %s %s %d: %.5f deg / %.5f cm"), *Rider, What, Bone, R, P));
			}
			Err.Add(R, P);
		};
		CompareMat(Root, C->GetField<EJson::Array>(TEXT("root")), TEXT("root"), 0);
		CompareMat(BoardW, C->GetField<EJson::Array>(TEXT("board")), TEXT("board"), 0);
		const TArray<TSharedPtr<FJsonValue>>& BodyExp = C->GetArrayField(TEXT("body"));
		TestEqual(TEXT("palette bones"), Pal.Num(), BodyExp.Num());
		for (int32 I = 0; I < Pal.Num() && I < BodyExp.Num(); ++I)
		{
			CompareMat(Pal[I], BodyExp[I], TEXT("bone"), I);
		}
		++Cases;
	}
	TestTrue(TEXT("palette sample pose"), PoseErr.Rot <= TolSampleDeg && PoseErr.Pos <= TolSampleCm);
	TestTrue(TEXT("cases compared"), Cases >= 4);
	AddInfo(FString::Printf(TEXT("palette: %d cases, max %.2e deg / %.2e cm (pose %.2e deg / %.2e cm), feet %.2e"),
		Cases, Err.Rot, Err.Pos, PoseErr.Rot, PoseErr.Pos, FootErr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimControllerTest, "SSX.Anim.Controller", AnimTestFlags)
bool FSSXAnimControllerTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Ref = LoadReference(*this);
	const FAflLibrary* Lib = Ref ? Library(*this) : nullptr;
	if (!Ref || !Lib)
	{
		return true;
	}
	FAnimController Ctl;
	Ctl.Init(Lib, 0, 10);
	FAnimDriveInputs In;
	const FAnimProps* Ride = Lib->Props(0x1FF);
	if (!TestNotNull(TEXT("props 0x1FF"), Ride))
	{
		return true;
	}
	TestEqual(TEXT("0x1FF loop mode"), Ride->LoopMode, 4);
	TestEqual(TEXT("0x1FF play"), Ctl.Play(0x1FF), 0x1FF);
	// 0x15F060
	TestEqual(TEXT("empty channel: weight 1 at once"), Ctl.Newest(0)->Weight, 1.0);
	TestEqual(TEXT("empty channel: no fade rate"), Ctl.Newest(0)->Rate, 0.0);
	TestEqual(TEXT("0x1FF play again"), Ctl.Play(0x1FF), 0x1FF);
	const TSharedPtr<FJsonObject> Fade = Ref->GetObjectField(TEXT("fade"));
	TestTrue(TEXT("fade-in rate (0x159E38)"), FMath::Abs(Ctl.Newest(0)->Rate - Fade->GetNumberField(TEXT("fade_in"))) < 1e-9);
	// 0x203A40
	for (int32 F = 0; F < 14; ++F)
	{
		Ctl.Update(In);
	}
	TestTrue(TEXT("not yet faded in after 2 B game frames (add.s sum 0.99999976)"), Ctl.Newest(0)->Weight < 1.0);
	Ctl.Update(In);
	TestTrue(TEXT("faded in after 2 B + 1 game frames"), FMath::IsNearlyEqual(Ctl.Newest(0)->Weight, 1.0, 1e-9));
	TestTrue(TEXT("straight riding cycles on the clock"), Ctl.Newest(0)->bCycle && Ctl.Newest(0)->Subs[0].Frame == 7.5);
	const FAnimProps* Land = Lib->Props(0x242);
	const FAflClip* LandClip = Lib->Clip(Lib->Resolve(0x242, 0, 10));
	if (TestNotNull(TEXT("0x242"), Land) && TestNotNull(TEXT("L_TAIL clip"), LandClip))
	{
		Ctl.Play(0x242);
		const FPlayInstance& Old = Ctl.Instances(0)[0];
		TestTrue(TEXT("fade-out rate (0x159D88)"), FMath::Abs(Old.Rate - Fade->GetNumberField(TEXT("fade_out"))) < 1e-9);
		TestFalse(TEXT("replaced instance loses auto-chain"), Old.bAutoChain);
		int32 Frames = 0;
		while (Ctl.CurrentId(0) == 0x242 && Frames < 500)
		{
			Ctl.Update(In);
			++Frames;
		}
		TestEqual(TEXT("one-shot done one clock after reaching n - 1 (0x15A030): 2 (n - 1) + 1 frames, then 0x1FF"), Frames, 2 * (LandClip->NFrames - 1) + 1);
		TestEqual(TEXT("chained to riding"), Ctl.CurrentId(0), 0x1FF);
		for (int32 F = 0; F < 20; ++F)
		{
			Ctl.Update(In);
		}
		TestEqual(TEXT("faded-out instances removed"), Ctl.Instances(0).Num(), 1);
	}
	for (int32 Id = 0x24A; Id <= 0x24F; ++Id)
	{
		if (const FAnimProps* P = Lib->Props(Id); P && P->Blend == 0 && Lib->Resolve(Id, 0, 10) >= 0)
		{
			Ctl.Play(Id);
			TestEqual(TEXT("hard cut leaves one instance"), Ctl.Instances(0).Num(), 1);
			TestEqual(TEXT("hard cut weight"), Ctl.Newest(0)->Weight, 1.0);
			break;
		}
	}
	const int32 GrabClip = Lib->Resolve(0x278, 0, 10);
	if (const FAflClip* G = Lib->Clip(GrabClip); G && G->Markers[1] >= 0)
	{
		Ctl.Play(0x278, 0.0, 5, 1.0);
		for (int32 F = 0; F < 200; ++F)
		{
			Ctl.Update(In);
		}
		TestEqual(TEXT("grab held at marker 1"), Ctl.Newest(0)->Subs[0].Frame, double(G->Markers[1]));
		Ctl.ReleaseHold(Lib->Props(0x278)->Category);
		Ctl.Update(In);
		TestTrue(TEXT("released grab plays on"), Ctl.Newest(0)->Subs[0].Frame > G->Markers[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimDriverTest, "SSX.Anim.Driver", AnimTestFlags)
bool FSSXAnimDriverTest::RunTest(const FString& Parameters)
{
	const FAflLibrary* Lib = Library(*this);
	if (!Lib)
	{
		return true;
	}
	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	FRiderSim Sim;
	Sim.SetWorld(&World);
	Sim.Profile.BoardClass = 0;
	RegisterGroundStates(Sim);
	Sim.Reset();
	Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
	FRiderAnimDriver Driver;
	if (!TestTrue(TEXT("driver init"), Driver.Init(Lib, TEXT("brodi"), 0, 10)))
	{
		return true;
	}
	FRiderInputWord In;
	FRiderPalette Pal;
	bool bFinite = true;
	for (int32 F = 0; F < 240; ++F)
	{
		In.Turn = F > 60 && F < 150 ? 1.0 : 0.0;
		In.Crouch = F > 160 ? 1.0 : 0.0;
		Sim.Step(In);
		Driver.Step(Sim);
		Driver.ComputePalette(Sim, Pal);
		for (const FRowMat& M : Pal.Body)
		{
			for (int32 I = 0; I < 4; ++I) { for (int32 J = 0; J < 4; ++J) { bFinite &= FMath::IsFinite(M.M[I][J]); } }
		}
	}
	TestTrue(TEXT("palette valid"), Pal.bValid);
	TestTrue(TEXT("palette finite"), bFinite);
	const int32 Cur = Driver.GetController().CurrentId(0);
	TestTrue(TEXT("riding loop (or the skating push) on channel 0"), Cur == 0x1FF || Cur == 0x221);
	if (Pal.bValid && Pal.Body.Num() >= 19)
	{
		const FAflBoardModel* Model = Lib->BoardModel(0, Sim.State.bStanceGoofy);
		const FVector3f Target = Pal.Board.TransformPoint(FVector3f(Model->IK[1]));
		TestTrue(TEXT("right foot on the board"), (Pal.Body[18].Row(3) - Target).Size() < 5.0f);
	}
	int32 Meshes = 0;
	double MaxDev = 0.0;
	for (const FString& Id : SSXCharacters::AllIds())
	{
		const USkeletalMesh* Mesh = SSXCharacters::LoadRiderMesh(Id);
		const FAflSkeleton* Body = Lib->Skeleton(Id);
		if (!Mesh || !Body)
		{
			continue;
		}
		++Meshes;
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		TArray<FTransform> Comp;
		Comp.SetNum(Ref.GetNum());
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			const int32 P = Ref.GetParentIndex(I);
			Comp[I] = P == INDEX_NONE ? Ref.GetRefBonePose()[I] : Ref.GetRefBonePose()[I] * Comp[P];
		}
		TArray<FRowMat> BindW;
		BindW.SetNum(Body->Bones.Num());
		double Lift = 0.0;
		for (int32 J = 0; J < Body->Bones.Num(); ++J)
		{
			const FAflBone& B = Body->Bones[J];
			const FRowMat L = LocalRows(B.Euler, B.Translation);
			BindW[J] = B.Parent < 0 ? L : L * BindW[B.Parent];
			const int32 I = Ref.FindBoneIndex(FName(*B.Name));
			if (!TestTrue(*FString::Printf(TEXT("%s bone %s in the mesh"), *Id, *B.Name), I != INDEX_NONE))
			{
				continue;
			}
			const FVector3f G = BindW[J].Row(3);
			if (J == 0)
			{
				Lift = Comp[I].GetLocation().Z - G.Z;
			}
			const FVector Expect(G.X, -G.Y, G.Z + Lift);
			MaxDev = FMath::Max(MaxDev, (Comp[I].GetLocation() - Expect).Size());
		}
	}
	if (Meshes > 0)
	{
		TestTrue(*FString::Printf(TEXT("mesh bind positions = game bind (max %.4f cm over %d riders)"), MaxDev, Meshes), MaxDev < 0.05);
		AddInfo(FString::Printf(TEXT("mesh bind check: %d riders, max %.4f cm"), Meshes, MaxDev));
	}
	else
	{
		SSXTest::Skip(*this, TEXT("no imported rider mesh: bind check skipped"));
	}
	return true;
}

#endif
