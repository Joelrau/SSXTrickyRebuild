// 0x11D4E8, 0x2029D8, 0x1DF478
#include "Animation/MorphTarget.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Characters/SSXRiderPartsMesh.h"
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
#include "Sim/Anim/RiderParts.h"
#include "Sim/Anim/RiderPose.h"
#include "Sim/Ground/GroundStates.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXAnim;

	constexpr EAutomationTestFlags FaceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	const TCHAR* const FsRiderIds[12] = { TEXT("eddie"), TEXT("kaori"), TEXT("luther"), TEXT("mac"), TEXT("moby"), TEXT("zoe"),
		TEXT("jp"), TEXT("elise"), TEXT("psymon"), TEXT("seeiah"), TEXT("brodi"), TEXT("marisol") };

	TSharedPtr<FJsonObject> FsLoadReference(FAutomationTestBase& T)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("face_secondary_reference.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			SSXTest::Skip(T, FString::Printf(TEXT("%s missing: run tools/make_face_secondary_reference.py; skipped"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			T.AddError(TEXT("face_secondary_reference.json does not parse"));
			return nullptr;
		}
		return Root;
	}

	const FAflLibrary* FsLibrary(FAutomationTestBase& T)
	{
		const FAflLibrary* Lib = FAflLibrary::Shared();
		if (!Lib || Lib->GetVersion() < 4)
		{
			SSXTest::Skip(T, TEXT("SimData/anim_clips.bin missing or older than version 4 (tools/export_anim_clips.py); skipped"));
			return nullptr;
		}
		return Lib;
	}

	void FsInfo(FAutomationTestBase& T, const FString& Msg)
	{
		T.AddInfo(Msg);
		UE_LOG(LogTemp, Display, TEXT("[SSX.Anim.FaceSecondary] %s"), *Msg);
	}

	FVector4f FsVec4(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = O->GetArrayField(Field);
		return FVector4f(float(A[0]->AsNumber()), float(A[1]->AsNumber()), float(A[2]->AsNumber()), float(A[3]->AsNumber()));
	}

	FRowMat FsRowMat(const TSharedPtr<FJsonValue>& V)
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		FRowMat M;
		for (int32 K = 0; K < 16; ++K)
		{
			M.M[K / 4][K % 4] = float(A[K]->AsNumber());
		}
		return M;
	}

	double FsRotDiffDeg(const FRowMat& A, const FRowMat& B)
	{
		double S = 0.0;
		for (int32 I = 0; I < 3; ++I) { for (int32 J = 0; J < 3; ++J) { S += FMath::Square(double(A.M[I][J]) - double(B.M[I][J])); } }
		return FMath::RadiansToDegrees(2.0 * FMath::Asin(FMath::Min(1.0, FMath::Sqrt(S) / (2.0 * UE_DOUBLE_SQRT_2))));
	}

	double FsEulerDiffDeg(const FVector3d& A, const FVector3d& B)
	{
		return FsRotDiffDeg(LocalRows(A, FVector3d::ZeroVector), LocalRows(B, FVector3d::ZeroVector));
	}

	int32 FsPartTracks(const FAflPart& P)
	{
		if (P.Skeleton.Bones.Num() == 0)
		{
			return P.NumMorphKeys;
		}
		int32 N = 0;
		for (const FAflBone& B : P.Skeleton.Bones)
		{
			N += 3 * ((B.Flags & 1) + ((B.Flags >> 1) & 1));
		}
		return N;
	}

	int32 FsIdleIdForClip(const FAflLibrary& Lib, int32 Character, int32 Clip)
	{
		for (int32 Id = IdleStill; Id <= IdleUp; ++Id)
		{
			if (Lib.Resolve(Id, 0, Character) == Clip)
			{
				return Id;
			}
		}
		return NoAnim;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimSecondaryTest, "SSX.Anim.Secondary", FaceTestFlags)
bool FSSXAnimSecondaryTest::RunTest(const FString& Parameters)
{
	const FAflLibrary* Lib = FsLibrary(*this);
	if (!Lib)
	{
		return true;
	}
	FString Table;
	for (const FString& Id : SSXCharacters::AllIds())
	{
		const FAflRiderParts* Parts = Lib->Parts(Id);
		if (!TestNotNull(*FString::Printf(TEXT("parts of %s"), *Id), Parts))
		{
			continue;
		}
		const FAflPart* Eyes = Parts->Find(5);
		const FAflPart* Face = Parts->Find(FaceFileId);
		TestTrue(*FString::Printf(TEXT("%s eyes: 2 bones"), *Id), Eyes && Eyes->Skeleton.Bones.Num() == 2 && Eyes->PaletteShift == 20);
		TestTrue(*FString::Printf(TEXT("%s face part"), *Id), Id == TEXT("zz_mmm") ? Face == nullptr
			: (Face && Face->Skeleton.Bones.Num() == 0 && Face->NumMorphKeys == 23));
		int32 NBones = 0;
		FString Names;
		for (const FAflPart& P : Parts->Parts)
		{
			for (const FAflBone& B : P.Skeleton.Bones)
			{
				TestTrue(*FString::Printf(TEXT("%s %s hangs on a body bone or its own part"), *Id, *B.Name),
					(B.ParentFile == 0 && B.Parent < 19) || (B.ParentFile == P.FileId && B.Parent >= 0));
				Names += FString::Printf(TEXT(" %s(%d)"), *B.Name, P.FileId);
				++NBones;
			}
		}
		Table += FString::Printf(TEXT("\n  %-8s %d bones = root + 19 + %d:%s"), *Id, 20 + NBones, NBones, *Names);
		if (const USkeletalMesh* Mesh = SSXCharacters::LoadRiderMesh(Id))
		{
			TestEqual(*FString::Printf(TEXT("%s mesh bones = root + body + parts"), *Id), Mesh->GetRefSkeleton().GetNum(), 20 + NBones);
			for (const FAflPart& P : Parts->Parts)
			{
				for (const FAflBone& B : P.Skeleton.Bones)
				{
					TestTrue(*FString::Printf(TEXT("%s mesh has %s"), *Id, *B.Name), Mesh->GetRefSkeleton().FindBoneIndex(FName(*B.Name)) != INDEX_NONE);
				}
			}
		}
	}
	FsInfo(*this, TEXT("rider bones (all part bones are driven by the clips' part blocks):") + Table);

	int32 BlocksChecked = 0, BlockErrors = 0;
	for (int32 Ch = 0; Ch < 12; ++Ch)
	{
		const FAflRiderParts* Parts = Lib->Parts(FsRiderIds[Ch]);
		for (int32 Id = IdleStill; Id <= 0x2FE && Parts; ++Id)
		{
			const FAflClip* C = Lib->Clip(Lib->Resolve(Id, 0, Ch));
			for (int32 B = 0; C && B < C->Blocks.Num(); ++B)
			{
				const FAflBlock& Blk = C->Blocks[B];
				if (Blk.Channel < 2)
				{
					continue;
				}
				const FAflPart* P = Parts->Find(Blk.Channel);
				++BlocksChecked;
				if (!P || Blk.Tracks.Num() != FsPartTracks(*P))
				{
					++BlockErrors;
					AddError(FString::Printf(TEXT("%s: block of part %d has %d tracks (part %s)"), *C->Name, Blk.Channel,
						Blk.Tracks.Num(), P ? *FString::FromInt(FsPartTracks(*P)) : TEXT("missing")));
				}
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("part blocks match the part skeletons (%d blocks)"), BlocksChecked), BlocksChecked > 300 && BlockErrors == 0);

	const TSharedPtr<FJsonObject> Ref = FsLoadReference(*this);
	if (!Ref)
	{
		return true;
	}
	// 0x11D4E8
	int32 Cases = 0, Wrong = 0, LiveN = 0, LiveOk = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("selector")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const int32 Got = HairIdleId(FsVec4(O, TEXT("v")), FsVec4(O, TEXT("x")), FsVec4(O, TEXT("y")), FsVec4(O, TEXT("z")),
			int32(O->GetNumberField(TEXT("character"))), int32(O->GetNumberField(TEXT("ch0"))));
		++Cases;
		if (Got != int32(O->GetNumberField(TEXT("id"))))
		{
			if (++Wrong < 5)
			{
				AddError(FString::Printf(TEXT("selector case %d: %#x, model %#x"), Cases, Got, int32(O->GetNumberField(TEXT("id")))));
			}
		}
		if (O->HasField(TEXT("live")))
		{
			++LiveN;
			LiveOk += Got == int32(O->GetNumberField(TEXT("live")));
		}
	}
	TestTrue(*FString::Printf(TEXT("selector = model on %d cases (%d differ)"), Cases, Wrong), Cases > 1000 && Wrong == 0);
	TestTrue(*FString::Printf(TEXT("selector predicts %d of %d live channel-2 changes"), LiveOk, LiveN), LiveN > 500 && LiveOk >= LiveN - 15);
	FsInfo(*this, FString::Printf(TEXT("idle selector: %d cases, live changes %d/%d"), Cases, LiveOk, LiveN));
	TestTrue(TEXT("gate: race frame % 18, not Void / ResetWait"), HairIdleDue(36, 3) && !HairIdleDue(37, 3) && !HairIdleDue(36, 1) && !HairIdleDue(36, 22));

	double PoseRot = 0.0, PoseCm = 0.0, PalRot = 0.0, PalCm = 0.0;
	int32 PartsN = 0, Animated = 0, PalBones = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("ram_parts")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const FString Rider = O->GetStringField(TEXT("rider"));
		const int32 Character = int32(O->GetNumberField(TEXT("character")));
		const double Scale = O->GetNumberField(TEXT("scale"));
		const FAflRiderParts* Parts = Lib->Parts(Rider);
		if (!TestNotNull(*FString::Printf(TEXT("parts %s"), *Rider), Parts))
		{
			continue;
		}
		TArray<FRowMat> Body;
		for (const TSharedPtr<FJsonValue>& M : O->GetArrayField(TEXT("body"))) { Body.Add(FsRowMat(M)); }
		FAnimController Ctl;
		Ctl.Init(Lib, 0, Character);
		for (const TSharedPtr<FJsonValue>& PV : O->GetArrayField(TEXT("parts")))
		{
			const TSharedPtr<FJsonObject> P = PV->AsObject();
			const int32 File = int32(P->GetNumberField(TEXT("file")));
			const FAflPart* Part = Parts->Find(File);
			if (!TestNotNull(*FString::Printf(TEXT("%s part %d"), *Rider, File), Part))
			{
				continue;
			}
			TestEqual(*FString::Printf(TEXT("%s part %d palette offset (pose +0x390)"), *Rider, File), Part->PaletteShift, int32(P->GetNumberField(TEXT("shift"))));
			TArray<FPoseLayer> Layers;
			for (const TSharedPtr<FJsonValue>& LV : P->GetArrayField(TEXT("layers")))
			{
				const TSharedPtr<FJsonObject> L = LV->AsObject();
				const int32 Clip = Lib->FindClip(L->GetStringField(TEXT("clip")));
				const FAflClip* C = Lib->Clip(Clip);
				if (!TestNotNull(*FString::Printf(TEXT("clip %s"), *L->GetStringField(TEXT("clip"))), C))
				{
					continue;
				}
				FPoseLayer& Ly = Layers.AddDefaulted_GetRef();
				Ly.Block = C->Block(File);
				Ly.Frame = L->GetNumberField(TEXT("frame"));
				Ly.Weight = L->GetNumberField(TEXT("weight"));
				Ly.bMirror = L->GetBoolField(TEXT("mirror"));
				Ly.Mask = uint32(L->GetNumberField(TEXT("mask")));
				const int32 Id = FsIdleIdForClip(*Lib, Character, Clip);
				if (TestTrue(*FString::Printf(TEXT("%s: idle id of %s"), *Rider, *C->Name), Id != NoAnim))
				{
					Ctl.bMirror = Ly.bMirror;
					Ctl.Play(Id);
					if (FPlayInstance* I = Ctl.Newest(2))
					{
						I->Subs[0].Frame = Ly.Frame;
						I->Weight = Ly.Weight;
					}
				}
			}
			Animated += Layers.Num() > 0;
			TArray<FBonePose> Pose;
			BlendLayers(*Lib, Part->Skeleton, Layers, Scale, Scale, Part->PaletteShift, Pose);
			const TArray<TSharedPtr<FJsonValue>>& E = P->GetArrayField(TEXT("euler"));
			const TArray<TSharedPtr<FJsonValue>>& T = P->GetArrayField(TEXT("trans"));
			for (int32 I = 0; I < Pose.Num(); ++I)
			{
				const FVector3d Er(E[3 * I]->AsNumber(), E[3 * I + 1]->AsNumber(), E[3 * I + 2]->AsNumber());
				const FVector3d Tr(T[3 * I]->AsNumber(), T[3 * I + 1]->AsNumber(), T[3 * I + 2]->AsNumber());
				PoseRot = FMath::Max(PoseRot, FsEulerDiffDeg(Pose[I].Euler, Er));
				PoseCm = FMath::Max(PoseCm, (Pose[I].Trans - Tr).Size());
			}
			++PartsN;
		}
		if (O->GetBoolField(TEXT("drawn")))
		{
			FRiderPartsPose Out;
			BuildPartsPose(*Lib, *Parts, Ctl, Body, Scale, Out);
			for (const TSharedPtr<FJsonValue>& PV : O->GetArrayField(TEXT("parts")))
			{
				const TSharedPtr<FJsonObject> P = PV->AsObject();
				const FPartPalette* Got = Out.Find(int32(P->GetNumberField(TEXT("file"))));
				const TArray<TSharedPtr<FJsonValue>>& Pal = P->GetArrayField(TEXT("palette"));
				if (!TestTrue(TEXT("part palette built"), Got && Got->Bones.Num() == Pal.Num()))
				{
					continue;
				}
				for (int32 I = 0; I < Pal.Num(); ++I)
				{
					const FRowMat R = FsRowMat(Pal[I]);
					PalRot = FMath::Max(PalRot, FsRotDiffDeg(Got->Bones[I], R));
					PalCm = FMath::Max(PalCm, double((Got->Bones[I].Row(3) - R.Row(3)).Size()));
					++PalBones;
				}
			}
			TestFalse(*FString::Printf(TEXT("%s: no face layer in the race (+0x288 = 0)"), *Rider), Out.bFaceSampled);
		}
	}
	TestTrue(*FString::Printf(TEXT("RAM part poses: %d parts (%d animated), max %.2e deg / %.2e cm"), PartsN, Animated, PoseRot, PoseCm),
		PartsN > 30 && Animated >= 10 && PoseRot < 1e-3 && PoseCm < 1e-3);
	TestTrue(*FString::Printf(TEXT("RAM part palettes: %d bones, max %.2e deg / %.2e cm"), PalBones, PalRot, PalCm),
		PalBones > 30 && PalRot < 0.01 && PalCm < 0.01);
	FsInfo(*this, FString::Printf(TEXT("RAM: %d head parts (%d animated) pose max %.2e deg %.2e cm; %d palette bones max %.2e deg %.2e cm"),
		PartsN, Animated, PoseRot, PoseCm, PalBones, PalRot, PalCm));

	FPatchWorld World;
	SSXRiderSetup::BuildSlopeWorld(World, 15.0, 1);
	FRiderSim Sim;
	Sim.SetWorld(&World);
	Sim.Profile.BoardClass = 0;
	RegisterGroundStates(Sim);
	Sim.Reset();
	Sim.Spawn(FVector(0, 0, 0), FVector(0, 1, 0), FVector::ZeroVector, 0.0, false);
	FRiderAnimDriver Driver;
	if (!TestTrue(TEXT("driver init (eddie)"), Driver.Init(Lib, TEXT("eddie"), 0, 0)) || !TestNotNull(TEXT("eddie parts"), Driver.GetParts()))
	{
		return true;
	}
	FRiderInputWord In;
	FRiderPalette Pal;
	int32 Changes = 0, OffGate = 0, Last = NoAnim;
	double HairMove = 0.0;
	bool bFinite = true;
	TOptional<FVector3f> FirstRel;
	for (int32 F = 0; F < 600; ++F)
	{
		In.Turn = F > 200 && F < 320 ? 1.0 : 0.0;
		Sim.Step(In);
		Driver.Step(Sim);
		const int32 Cur = Driver.GetController().CurrentId(2);
		if (Cur != Last)
		{
			++Changes;
			OffGate += Sim.State.Frame % IdleSelectPeriod != 0;
			Last = Cur;
		}
		Driver.ComputePalette(Sim, Pal);
		const FPartPalette* Hair = Pal.Parts.Find(8);
		if (Hair && Pal.Body.Num() == 19)
		{
			for (const FRowMat& M : Hair->Bones)
			{
				for (int32 I = 0; I < 4; ++I) { for (int32 J = 0; J < 4; ++J) { bFinite &= FMath::IsFinite(M.M[I][J]); } }
			}
			const FRowMat& H = Pal.Body[4];
			const FVector3f D = Hair->Bones[0].Row(3) - H.Row(3);
			const FVector3f Rel(D | H.Row(0), D | H.Row(1), D | H.Row(2));
			if (!FirstRel.IsSet())
			{
				FirstRel = Rel;
			}
			HairMove = FMath::Max(HairMove, double((Rel - FirstRel.GetValue()).Size()));
		}
	}
	TestTrue(*FString::Printf(TEXT("channel-2 idle plays (%d changes, %d off the 18-frame gate)"), Changes, OffGate), Changes >= 1 && OffGate == 0);
	TestTrue(TEXT("hair palette finite"), bFinite);
	TestTrue(*FString::Printf(TEXT("hair moves relative to the head (%.2f cm)"), HairMove), HairMove > 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAnimFaceTest, "SSX.Anim.Face", FaceTestFlags)
bool FSSXAnimFaceTest::RunTest(const FString& Parameters)
{
	const FAflLibrary* Lib = FsLibrary(*this);
	if (!Lib)
	{
		return true;
	}
	int32 FeFace = 0;
	for (int32 Ch = 0; Ch < 12; ++Ch)
	{
		for (int32 Id = 0x2F9; Id <= 0x2FE; ++Id)
		{
			const FAflClip* C = Lib->Clip(Lib->Resolve(Id, 0, Ch));
			const FAflBlock* B = C ? C->Block(FaceFileId) : nullptr;
			FeFace += B && B->Tracks.Num() == 23;
		}
	}
	TestEqual(TEXT("FE clips with a 23-key face block"), FeFace, 72);
	const FAflClip* Ride = Lib->Clip(Lib->Riding(0, 0));
	TestTrue(TEXT("riding clips have no face block"), Ride && !Ride->Block(FaceFileId));

	const TSharedPtr<FJsonObject> Ref = FsLoadReference(*this);
	if (!Ref)
	{
		return true;
	}
	// 0x2029D8
	double MaxErr = 0.0, MaxDrawn = 0.0;
	int32 N = 0;
	for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("face")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const FAflClip* C = Lib->Clip(Lib->FindClip(O->GetStringField(TEXT("clip"))));
		const FAflBlock* B = C ? C->Block(FaceFileId) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("face block %s"), *O->GetStringField(TEXT("clip"))), B))
		{
			continue;
		}
		const double Fr = O->GetNumberField(TEXT("frame"));
		FPoseLayer L1; L1.Block = B; L1.Frame = Fr; L1.Weight = 1.0;
		FPoseLayer La; La.Block = B; La.Frame = Fr; La.Weight = 0.25;
		FPoseLayer Lb; Lb.Block = B; Lb.Frame = FMath::Max(0.0, Fr - 3.0); Lb.Weight = 0.75;
		TArray<double> W, W2;
		SampleFaceWeights(*Lib, { L1 }, 23, W);
		SampleFaceWeights(*Lib, { La, Lb }, 23, W2);
		const TArray<TSharedPtr<FJsonValue>>& E = O->GetArrayField(TEXT("weights"));
		const TArray<TSharedPtr<FJsonValue>>& E2 = O->GetArrayField(TEXT("blend"));
		const TArray<TSharedPtr<FJsonValue>>& D = O->GetArrayField(TEXT("drawn"));
		FRiderPartsPose Pose;
		Pose.FaceWeights = W;
		Pose.bFaceSampled = true;
		TArray<double> Drawn;
		SSXCharacters::FaceMorphWeights(Pose, 1.0, Drawn);
		for (int32 K = 0; K < 23; ++K)
		{
			MaxErr = FMath::Max(MaxErr, FMath::Abs(W[K] - E[K]->AsNumber()));
			MaxErr = FMath::Max(MaxErr, FMath::Abs(W2[K] - E2[K]->AsNumber()));
			MaxDrawn = FMath::Max(MaxDrawn, FMath::Abs(Drawn[K] - D[K]->AsNumber()));
		}
		++N;
	}
	TestTrue(*FString::Printf(TEXT("face weights = model (%d cases, max %.2e)"), N, MaxErr), N >= 288 && MaxErr < 1e-5);
	TestTrue(*FString::Printf(TEXT("drawn weights (key 0 always, others > 0.01) = model (max %.2e)"), MaxDrawn), MaxDrawn < 1e-5);
	FsInfo(*this, FString::Printf(TEXT("face weights: %d FE cases, max %.2e vs model; drawn max %.2e"), N, MaxErr, MaxDrawn));
	TestEqual(TEXT("key 0 drawn at any weight"), FaceKeyDrawWeight(0, -0.3), -0.3);
	TestEqual(TEXT("key 5 at 0.01 skipped"), FaceKeyDrawWeight(5, 0.01), 0.0);
	TestEqual(TEXT("key 5 negative skipped"), FaceKeyDrawWeight(5, -0.5), 0.0);
	TestTrue(TEXT("2560 / 32768 cm per delta step"), FMath::IsNearlyEqual(FaceMorphCmPerUnit,
		Ref->GetObjectField(TEXT("morph"))->GetNumberField(TEXT("cm_per_unit")), 1e-12));
	{
		FRiderPartsPose Pose;
		Pose.FaceWeights.Init(0.5, 23);
		Pose.bFaceSampled = false;
		TArray<double> Drawn;
		SSXCharacters::FaceMorphWeights(Pose, 1.1, Drawn);
		bool bZero = true;
		for (double X : Drawn) { bZero &= X == 0.0; }
		TestTrue(TEXT("no face layer this frame: base mesh (all weights 0)"), bZero);
		Pose.bFaceSampled = true;
		SSXCharacters::FaceMorphWeights(Pose, 1.1, Drawn);
		TestTrue(TEXT("weights x rider scale"), FMath::IsNearlyEqual(Drawn[3], 0.55, 1e-12));
	}
	if (const FAflRiderParts* Parts = Lib->Parts(TEXT("eddie")))
	{
		FAnimController Ctl;
		Ctl.Init(Lib, 0, 0);
		Ctl.Play(0x2F9);
		for (int32 F = 0; F < 30; ++F) { Ctl.Update(FAnimDriveInputs()); }
		TArray<FRowMat> Body;
		Body.Init(FRowMat::Identity(), 19);
		FRiderPartsPose Out;
		BuildPartsPose(*Lib, *Parts, Ctl, Body, 1.0, Out);
		TestTrue(TEXT("FE: face sampled"), Out.bFaceSampled && Out.FaceWeights.Num() == 23);
		const FPlayInstance* I = Ctl.Newest(0);
		const FAflClip* C = I ? Lib->Clip(I->Subs[0].Clip) : nullptr;
		if (C && Out.FaceWeights.Num() == 23)
		{
			FPoseLayer L; L.Block = C->Block(FaceFileId); L.Frame = I->Subs[0].Frame; L.Weight = I->Weight;
			TArray<double> W;
			SampleFaceWeights(*Lib, { L }, 23, W);
			double D = 0.0;
			for (int32 K = 0; K < 23; ++K) { D = FMath::Max(D, FMath::Abs(W[K] - Out.FaceWeights[K])); }
			TestTrue(TEXT("FE: controller path = direct sample"), D < 1e-12);
		}
		TestTrue(TEXT("FE: eyes / goggles / hair palettes"), Out.Find(5) && Out.Find(6) && Out.Find(8));
	}
	FString NonFe;
	int32 NonFeN = 0;
	for (int32 C = 0; C < Lib->NumClips(); ++C)
	{
		const FAflClip* Clip = Lib->Clip(C);
		if (Clip->Block(FaceFileId) && !Clip->Name.StartsWith(TEXT("fe")) && !Clip->Name.StartsWith(TEXT("scr")))
		{
			NonFe += TEXT(" ") + Clip->Name;
			++NonFeN;
		}
	}
	TestTrue(*FString::Printf(TEXT("non-FE clips with a face block:%s"), *NonFe), NonFeN == 1 && NonFe.Contains(TEXT("bxUT_SIGZOE")));
	FsInfo(*this, FString::Printf(TEXT("non-FE clips with a face block (%d):%s"), NonFeN, *NonFe));

	int32 WithTargets = 0, Meshes = 0;
	for (const FString& Id : SSXCharacters::AllIds())
	{
		const USkeletalMesh* Mesh = SSXCharacters::LoadRiderMesh(Id);
		const FAflRiderParts* Parts = Lib->Parts(Id);
		const FAflPart* Face = Parts ? Parts->Find(FaceFileId) : nullptr;
		if (!Mesh || !Face)
		{
			continue;
		}
		++Meshes;
		int32 Found = 0;
		for (int32 K = 0; K < Face->NumMorphKeys; ++K)
		{
			int32 Index = INDEX_NONE;
			Found += Mesh->FindMorphTargetAndIndex(SSXCharacters::FaceMorphName(K), Index) != nullptr;
		}
		WithTargets += Found == Face->NumMorphKeys && Face->NumMorphKeys > 0;
	}
	if (WithTargets > 0)
	{
		TestEqual(TEXT("imported riders with all face morph targets"), WithTargets, Meshes);
	}
	FsInfo(*this, FString::Printf(TEXT("imported meshes %d, with the 23 face morph targets %d (re-import after convert_characters.py)"), Meshes, WithTargets));
	return true;
}

#endif
