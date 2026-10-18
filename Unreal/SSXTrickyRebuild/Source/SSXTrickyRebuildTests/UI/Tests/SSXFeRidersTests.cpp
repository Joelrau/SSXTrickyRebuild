#include "Audio/SSXAudioConfig.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Rider/RiderProfiles.h"
#include "UI/SSXFeCamera.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeRiders.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFrontEndData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace SSXFeRiders;

	TSharedPtr<FJsonObject> LoadFeReference(FAutomationTestBase& T)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *FData::DefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root))
		{
			SSXTest::Skip(T, TEXT("SimData/fe_riders.json missing (tools/export_fe_riders.py); data checks skipped"));
			return nullptr;
		}
		return Root;
	}

	double RowsDiff(const SSXAnim::FRowMat& M, const TArray<TSharedPtr<FJsonValue>>& Rows, double& OutTransDiff)
	{
		double Rot = 0.0;
		OutTransDiff = 0.0;
		for (int32 R = 0; R < 4; ++R)
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = Rows[R]->AsArray();
			for (int32 C = 0; C < 3; ++C)
			{
				const double D = FMath::Abs(double(M.M[R][C]) - Row[C]->AsNumber());
				(R == 3 ? OutTransDiff : Rot) = FMath::Max(R == 3 ? OutTransDiff : Rot, D);
			}
		}
		return Rot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXFeRidersTest, "SSX.UI.FeRiders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXFeRidersTest::RunTest(const FString& Parameters)
{
	{
		FSession S;
		TestEqual(TEXT("first FE load -> pair 1 (feselect1)"), S.OnFeLoad(), 1);
		TArray<int32> K;
		for (int32 I = 0; I < 4; ++I) { K.Add(S.NextTrans()); }
		TestEqual(TEXT("load 1: TRANS3 / TRANS4 alternate"), K, TArray<int32>({ 2, 3, 2, 3 }));
		TestEqual(TEXT("second FE load -> pair 0 (feselect0)"), S.OnFeLoad(), 0);
		K.Reset();
		for (int32 I = 0; I < 4; ++I) { K.Add(S.NextTrans()); }
		TestEqual(TEXT("load 2: TRANS1 / TRANS2 alternate"), K, TArray<int32>({ 0, 1, 0, 1 }));

		FSession S2;
		S2.OnFeLoad();
		FRider R;
		R.Character = 3;
		TestEqual(TEXT("not loaded: nothing starts"), R.Update(S2, false), -1);
		R.bLoaded = true;
		R.Requested = Idle;
		TestEqual(TEXT("state 0 -> FEINIT 0x2F9"), R.Update(S2, false), AnimInit);
		TestEqual(TEXT("no change, nothing new"), R.Update(S2, true), -1);
		R.Requested = Picked;
		TestEqual(TEXT("state 1 -> TRANS3 (pair 1, flip 0)"), R.Update(S2, false), AnimTrans1 + 2);
		TestEqual(TEXT("TRANS running"), R.Update(S2, false), -1);
		TestEqual(TEXT("TRANS done -> CYCLE 0x2FE"), R.Update(S2, true), AnimCycle);
		R.Requested = 7;
		TestEqual(TEXT("other states only copy"), R.Update(S2, false), -1);
		TestEqual(TEXT("current copied"), R.Current, 7);
	}

	{
		FLightBlend B;
		B.Blend = 0.f;
		B.bSelected = true;
		int32 N = 0;
		while (B.Step() && N < 100) { ++N; }
		TestEqual(TEXT("blend 0 -> 1 in 15 FE frames"), N, 15);
		TestEqual(TEXT("blend reaches 1"), B.Blend, 1.f);
		FLightBlend Start;
		TestTrue(TEXT("initial blend 1, flag 0"), Start.Blend == 1.f && !Start.bSelected);
	}

	{
		static bool bTables = SSXFeTables::LoadTablesJson(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_tables.json")));
		(void)bTables;
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FE.Init(&Profile, &Cheats, nullptr, false);
		TestTrue(TEXT("FE load: every rider loaded and asked for FEINIT"), FE.Riders.Riders[5].bLoaded && FE.Riders.Riders[5].Requested == Idle);
		FE.Riders.RequestAll(None);
		auto Press = [&FE](ESSXFeKey K) { for (int32 N = 0; N < 3000 && !FE.IsReady(); ++N) { FE.Tick(); } FE.Key(0, K); for (int32 N = 0; N < 3000 && (N == 0 || !FE.IsReady()); ++N) { FE.Tick(); } };
		Press(ESSXFeKey::Cross);
		Press(ESSXFeKey::Cross);
		TestEqual(TEXT("on CharacterSelect"), FString(LexToString(FE.Current())), FString(TEXT("CharacterSelect")));
		bool bAllIdle = true;
		for (const FRider& R : FE.Riders.Riders) { bAllIdle &= R.Requested == Idle; }
		TestTrue(TEXT("CharacterSelect vfunc_15: all riders -> 0"), bAllIdle);
		TestTrue(TEXT("anim-event sounds on"), FE.bAnimEventSounds);
		const int32 P0 = FE.ViewSlot();
		TestEqual(TEXT("no rider chosen: carousel slot 4 (Eddie, cFE3D::vfunc_02 default player)"), P0, 4);
		FE.Key(0, ESSXFeKey::Cross); FE.Tick();
		TestEqual(TEXT("X: the selected rider -> 1 (TRANS)"), FE.Riders.Riders[P0].Requested, int32(Picked));
		TestEqual(TEXT("others stay 0"), FE.Riders.Riders[0].Requested, int32(Idle));
		for (int32 N = 0; N < 3000 && !FE.IsReady(); ++N) { FE.Tick(); }
		Press(ESSXFeKey::Triangle);
		TestEqual(TEXT("back: all -> 0 (FEINIT hard cut)"), FE.Riders.Riders[P0].Requested, int32(Idle));
		TestEqual(TEXT("back: camera on select<Name> frame 0 (FUN_0026B760)"), FE.Camera.Current, FString(TEXT("charSelect_selectEddie")));
	}

	// FUN_0026B760 / FUN_00261B38
	{
		FSSXFeCameraSet Set;
		const bool bCams = Set.Load(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json")));
		FSSXProfile Profile = FSSXProfile::MakeDefault();
		FSSXCheats Cheats;
		FSSXFrontEnd FE;
		FE.Init(&Profile, &Cheats, nullptr, false);
		if (bCams) { FE.Camera.CameraSet = &Set; }
		auto Press = [&FE](ESSXFeKey K) { for (int32 N = 0; N < 3000 && !FE.IsReady(); ++N) { FE.Tick(); } FE.Key(0, K); for (int32 N = 0; N < 3000 && (N == 0 || !FE.IsReady()); ++N) { FE.Tick(); } };
		Press(ESSXFeKey::Cross);
		Press(ESSXFeKey::Cross);
		const int32 P = FE.ViewSlot();
		for (int32 F = 0; F < 60 * 3 && FE.Camera.Current != TEXT("charSelect_selectEddie"); ++F) { FE.Tick(); }
		TestEqual(TEXT("entry: camera ends on the selected slot's select camera"), FE.Camera.Current,
			TEXT("charSelect_select") + SSXFeCams::RiderCamName(SSXFeTables::CarouselRider(P)));
		int32 NDrawn = 0;
		for (int32 K = 0; K < NumRiders; ++K) { NDrawn += IsSlotDrawn(K, P) ? 1 : 0; }
		TestTrue(TEXT("entry: selected slot drawn, 5 drawn (p-2..p+2)"), IsSlotDrawn(P, P) && NDrawn == 5 && !IsSlotDrawn((P + 3) % NumRiders, P));
		const FData* D = FData::Shared();
		FSSXFeCamPose Pose;
		if (bCams && D && FE.Camera.GetPose(Set, Pose))
		{
			// 0x26AC98
			FVector Fw, Up;
			Pose.Axes(Fw, Up);
			const FVector Rt = FVector::CrossProduct(Fw, Up);
			const double CotH = 1.0 / FMath::Tan(FMath::DegreesToRadians(double(Pose.HorizontalFovDeg()) * 0.5));
			auto Ndc = [&](int32 Slot, double Zoff) {
				const SSXAnim::FRowMat M = RiderRoot(Slot, D->RingRadius, D->RingZ);
				const FVector V = FVector(M.M[3][0], M.M[3][1], M.M[3][2] + Zoff) - Pose.Pos;
				const double Zc = FVector::DotProduct(V, Fw);
				return FVector2D(FVector::DotProduct(V, Rt) / Zc * CotH, FVector::DotProduct(V, Up) / Zc * CotH * 4.0 / 3.0);
			};
			const FVector2D C0 = Ndc(P, 90.0), Nb = Ndc((P + 1) % NumRiders, 90.0), Nb2 = Ndc((P + 2) % NumRiders, 90.0);
			const FVector2D Feet = Ndc(P, 0.0), Head = Ndc(P, 180.0);
			AddInfo(FString::Printf(TEXT("FE framing: focal %.2f, horizontal FOV %.2f deg; selected x %.3f, feet y %.3f, 180 cm y %.3f; neighbour x %.3f; 2nd neighbour x %.3f"),
				Pose.Focal, Pose.HorizontalFovDeg(), C0.X, Feet.Y, Head.Y, Nb.X, Nb2.X));
			TestTrue(TEXT("select camera: horizontal FOV 2 x 2 atan(8.997951/25) = 79.2 deg"), FMath::IsNearlyEqual(Pose.HorizontalFovDeg(), 79.2f, 0.1f));
			TestTrue(TEXT("selected rider centred (|x| < 0.02)"), FMath::Abs(C0.X) < 0.02);
			TestTrue(TEXT("selected rider fully in frame (feet and 180 cm inside |y| < 1)"), FMath::Abs(Feet.Y) < 1.0 && FMath::Abs(Head.Y) < 1.0);
			TestTrue(TEXT("first neighbours in frame"), FMath::Abs(Nb.X) < 1.0);
		}
	}

	TSharedPtr<FJsonObject> Ref = LoadFeReference(*this);
	const SSXAnim::FAflLibrary* Lib = SSXAnim::FAflLibrary::Shared();
	if (!Ref.IsValid())
	{
		return true;
	}
	FData Data;
	FString Err;
	if (!TestTrue(TEXT("fe_riders.json loads"), Data.Load(FData::DefaultPath(), &Err)))
	{
		AddError(Err);
		return false;
	}

	{
		double MaxRot = 0.0, MaxTrans = 0.0, MaxPlat = 0.0, MinFacing = 1.0;
		int32 WorstPlat = -1;
		for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("placement")))
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			const int32 Slot = int32(P->GetNumberField(TEXT("slot")));
			const SSXAnim::FRowMat M = RiderRoot(Slot, Data.RingRadius, Data.RingZ);
			double T = 0.0;
			MaxRot = FMath::Max(MaxRot, RowsDiff(M, P->GetArrayField(TEXT("rows")), T));
			MaxTrans = FMath::Max(MaxTrans, T);
			const TArray<TSharedPtr<FJsonValue>>& Pl = P->GetArrayField(TEXT("platform"));
			const double D = FMath::Sqrt(FMath::Square(M.M[3][0] - Pl[0]->AsNumber()) + FMath::Square(M.M[3][1] - Pl[1]->AsNumber()) + FMath::Square(M.M[3][2] - Pl[2]->AsNumber()));
			if (D > MaxPlat) { MaxPlat = D; WorstPlat = Slot; }
			const FVector2d Radial = FVector2d(M.M[3][0], M.M[3][1]).GetSafeNormal();
			MinFacing = FMath::Min(MinFacing, Radial.X * M.M[0][0] + Radial.Y * M.M[0][1]);
			TestEqual(*FString::Printf(TEXT("slot %d character (carousel)"), Slot), SSXFeTables::CarouselRider(Slot), int32(P->GetNumberField(TEXT("character"))));
		}
		AddInfo(FString::Printf(TEXT("placement: 12 slots, radius %.3f; rows max diff %.2e, position max diff %.2e cm vs fe_riders.py; max distance to the ssxfe.pbd platform %.2f cm (slot %d); min radial facing %.6f"),
			Data.RingRadius, MaxRot, MaxTrans, MaxPlat, WorstPlat, MinFacing));
		TestTrue(TEXT("root rows = fe_riders.rider_matrix"), MaxRot < 1e-5 && MaxTrans < 1e-3);
		TestTrue(TEXT("every rider on its platform (< 3 cm; platform 02 is authored 2.1 cm off)"), MaxPlat < 3.0);
		TestTrue(TEXT("local +x points out of the ring"), MinFacing > 0.99999);
	}

	{
		const TSharedPtr<FJsonObject> B = Ref->GetObjectField(TEXT("blend"));
		const TSharedPtr<FJsonObject> Mid = B->GetObjectField(TEXT("mid"));
		const FLightSet L = FLightSet::Lerp(Data.Unselected, Data.Selected, float(B->GetNumberField(TEXT("mid_t"))));
		double MaxD = 0.0;
		const TArray<TSharedPtr<FJsonValue>>& Amb = Mid->GetArrayField(TEXT("ambient"));
		const TArray<TSharedPtr<FJsonValue>>& Col = Mid->GetArrayField(TEXT("colour"));
		const TArray<TSharedPtr<FJsonValue>>& Dir = Mid->GetArrayField(TEXT("direction"));
		for (int32 I = 0; I < 4; ++I) { MaxD = FMath::Max(MaxD, FMath::Abs(L.Ambient[I] - Amb[I]->AsNumber())); }
		for (int32 I = 0; I < 12; ++I) { MaxD = FMath::Max(MaxD, FMath::Abs(L.Colour[I / 4][I % 4] - Col[I]->AsNumber())); }
		for (int32 I = 0; I < 3; ++I)
		{
			const TArray<TSharedPtr<FJsonValue>>& D = Dir[I]->AsArray();
			for (int32 C = 0; C < 3; ++C) { MaxD = FMath::Max(MaxD, FMath::Abs(L.Direction[I][C] - D[C]->AsNumber())); }
		}
		AddInfo(FString::Printf(TEXT("light set at t = 7/15: max diff %.2e vs fe_riders.light_set (python blend 0->1: %d frames in double)"), MaxD,
			int32(B->GetNumberField(TEXT("frames_0_to_1")))));
		TestTrue(TEXT("mid-blend light set"), MaxD < 1e-5);
	}

	if (!Lib)
	{
		SSXTest::Skip(*this, TEXT("SimData/anim_clips.bin missing (tools/export_anim_clips.py); pose checks skipped"));
		return true;
	}

	{
		int32 NClips = 0, NEvents = 0, NInBank = 0;
		FSSXAudioManifest Manifest;
		const bool bManifest = Manifest.Load(SSXAudio::ManifestPath());
		for (int32 Id = AnimInit; Id <= AnimCycle; ++Id)
		{
			for (int32 Ch = 0; Ch < 12; ++Ch)
			{
				const SSXAnim::FAflClip* C = Lib->Clip(Lib->Resolve(Id, 0, Ch));
				NClips += C && C->Name.StartsWith(TEXT("fe")) && C->Block(0) && C->Block(1);
			}
		}
		TestEqual(TEXT("72 feanim.afl clips with body and board blocks"), NClips, 72);
		for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("speech_events")))
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			const int32 Ch = int32(E->GetNumberField(TEXT("character"))), T = int32(E->GetNumberField(TEXT("trans")));
			const SSXAnim::FAflClip* C = Lib->Clip(Lib->Resolve(AnimTrans1 + T, 0, Ch));
			const TArray<TSharedPtr<FJsonValue>>& Ev = E->GetArrayField(TEXT("events"));
			if (!C || C->Events.Num() != Ev.Num() || Ev.Num() != 1) { AddError(FString::Printf(TEXT("char %d TRANS%d events"), Ch, T + 1)); continue; }
			const TArray<TSharedPtr<FJsonValue>>& FV = Ev[0]->AsArray();
			NEvents += C->Events[0].X == int32(FV[0]->AsNumber()) && C->Events[0].Y == int32(FV[1]->AsNumber());
			NInBank += bManifest && Manifest.Find(FString::Printf(TEXT("speech/feselect%d/%02d"), T / 2, C->Events[0].Y)) != nullptr;
		}
		AddInfo(FString::Printf(TEXT("speech: %d / 48 TRANS events equal the afl.py records, %d / 48 in feselect<k/2> of the converted audio"), NEvents, NInBank));
		TestEqual(TEXT("48 TRANS speech events exported"), NEvents, 48);
		if (bManifest) { TestEqual(TEXT("48 speech lines exist"), NInBank, 48); }
	}

	{
		double MaxRot = 0.0, MaxTrans = 0.0;
		int32 Cases = 0;
		for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("palette")))
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			const int32 Ch = int32(P->GetNumberField(TEXT("character")));
			const SSXAnim::FAflSkeleton* Body = Lib->Skeleton(P->GetStringField(TEXT("rider")));
			FPoser Poser;
			if (!TestTrue(TEXT("poser ready"), Body && Poser.Init(Lib, Body, Ch))) { continue; }
			Poser.GetController().Play(int32(P->GetNumberField(TEXT("anim"))), 0.0, 5);
			SSXAnim::FPlayInstance* I = Poser.GetController().Newest(0);
			if (!I) { AddError(TEXT("clip did not start")); continue; }
			TestEqual(TEXT("clip"), Lib->Clip(I->Subs[0].Clip)->Name, P->GetStringField(TEXT("clip")));
			I->Subs[0].Frame = P->GetNumberField(TEXT("frame"));
			I->Weight = 1.0;
			TArray<SSXAnim::FRowMat> Pal;
			SSXAnim::FRowMat Board;
			Poser.ComputePalette(RiderRoot(int32(P->GetNumberField(TEXT("slot"))), Data.RingRadius, Data.RingZ), Pal, Board);
			const TArray<TSharedPtr<FJsonValue>>& RB = P->GetArrayField(TEXT("body"));
			if (!TestEqual(TEXT("bone count"), Pal.Num(), RB.Num())) { continue; }
			for (int32 J = 0; J < Pal.Num(); ++J)
			{
				double T = 0.0;
				MaxRot = FMath::Max(MaxRot, RowsDiff(Pal[J], RB[J]->AsArray(), T));
				MaxTrans = FMath::Max(MaxTrans, T);
			}
			double CaseRot = 0.0, CaseTrans = 0.0;
			for (int32 J = 0; J < Pal.Num(); ++J)
			{
				double T = 0.0;
				CaseRot = FMath::Max(CaseRot, RowsDiff(Pal[J], RB[J]->AsArray(), T));
				CaseTrans = FMath::Max(CaseTrans, T);
			}
			double T = 0.0, BoardRot = RowsDiff(Board, P->GetArrayField(TEXT("board")), T);
			MaxRot = FMath::Max(MaxRot, BoardRot);
			MaxTrans = FMath::Max(MaxTrans, T);
			AddInfo(FString::Printf(TEXT("  %s frame %.1f slot %d: body rot %.2e trans %.2e cm, board rot %.2e trans %.2e cm"), *P->GetStringField(TEXT("clip")),
				P->GetNumberField(TEXT("frame")), int32(P->GetNumberField(TEXT("slot"))), CaseRot, CaseTrans, BoardRot, T));
			++Cases;
		}
		AddInfo(FString::Printf(TEXT("FK palettes: %d clip/frame cases x (19 body bones + board) vs afl_curves: rotation max diff %.2e, translation max diff %.2e cm"), Cases, MaxRot, MaxTrans));
		TestTrue(TEXT("palettes match afl_curves"), Cases == 6 && MaxRot < 1e-4 && MaxTrans < 0.01);
	}

	{
		FSSXFeCameraSet Set;
		const FData* D = FData::Shared();
		const SSXRider::FRiderTable& Table = SSXRider::FRiderTable::Shared();
		const FSSXFeCamera* Sel = Set.Load(FPaths::Combine(SSXFrontEnd::FrontEndDir(), TEXT("fe_cameras.json"))) ? Set.Find(TEXT("charSelect_selectMac")) : nullptr;
		if (Sel && D && Table.IsLoaded())
		{
			TestTrue(TEXT("size scale Mac 0.75 (table +0x24)"), FMath::IsNearlyEqual(SizeScale(3), 0.75, 1e-4));
			TestTrue(TEXT("size scale Kaori 0.70"), FMath::IsNearlyEqual(SizeScale(1), 0.70, 1e-4));
			const float ShotT = 0.f;
			const FSSXFeCamPose Pose = Sel->Sample(ShotT);
			FVector Fw, Up;
			Pose.Axes(Fw, Up);
			const FVector Rt = FVector::CrossProduct(Fw, Up);
			const double CotH = 1.0 / FMath::Tan(FMath::DegreesToRadians(double(Pose.HorizontalFovDeg()) * 0.5));
			auto Screen = [&](const FVector3f& P) {
				const FVector V = FVector(P) - Pose.Pos;
				const double Zc = FVector::DotProduct(V, Fw);
				return FVector2D(0.5 + 0.5 * FVector::DotProduct(V, Rt) / Zc * CotH, 0.5 - 0.5 * FVector::DotProduct(V, Up) / Zc * CotH * 4.0 / 3.0);
			};
			struct FCase { int32 Char; const TCHAR* Skel; FVector2D Orig; FVector2D Port; };
			const FCase Cases[] = { { 3, TEXT("mac"), FVector2D(0.353, 0.569), FVector2D(0.310, 0.519) }, { 1, TEXT("kaori"), FVector2D(0.806, 0.442), FVector2D(0.755, 0.337) } };
			for (const FCase& C : Cases)
			{
				int32 Slot = INDEX_NONE;
				for (int32 K = 0; K < NumRiders; ++K) { if (SSXFeTables::CarouselRider(K) == C.Char) { Slot = K; } }
				const SSXAnim::FAflSkeleton* Skel = Lib->Skeleton(C.Skel);
				FPoser Poser;
				if (Slot == INDEX_NONE || !Skel || !Poser.Init(Lib, Skel, C.Char)) { AddError(FString::Printf(TEXT("%s poser"), C.Skel)); continue; }
				FSession S;
				S.OnFeLoad();
				FRider R;
				R.Character = C.Char;
				R.bLoaded = true;
				R.Requested = Idle;
				Poser.Step(R, S);
				int32 Head = INDEX_NONE;
				for (int32 B = 0; B < Skel->Bones.Num(); ++B) { if (Skel->Bones[B].Name == TEXT("head")) { Head = B; } }
				const SSXAnim::FRowMat Root = RiderRoot(Slot, D->RingRadius, D->RingZ);
				TArray<SSXAnim::FRowMat> Body;
				SSXAnim::FRowMat Board;
				Poser.ComputePalette(Root, Body, Board);
				if (!Body.IsValidIndex(Head)) { AddError(TEXT("no head bone")); continue; }
				const SSXAnim::FRowMat Scaled = ScaleAboutRoot(Body[Head], Root, SizeScale(C.Char));
				const FVector2D U = Screen(Body[Head].Row(3)), Sc = Screen(Scaled.Row(3)), Ft = Screen(Root.Row(3));
				const double DU = FVector2D::Distance(U, C.Orig), DS = FVector2D::Distance(Sc, C.Orig);
				AddInfo(FString::Printf(TEXT("%s: unscaled head vs the old port shot (%.3f, %.3f): error %.3f"), C.Skel, C.Port.X, C.Port.Y, FVector2D::Distance(U, C.Port)));
				AddInfo(FString::Printf(TEXT("%s (slot %d, size %.2f) head on screen: unscaled (%.3f, %.3f) -> scaled (%.3f, %.3f), original (%.3f, %.3f); ring point (%.3f, %.3f); head-to-ring height %.3f -> %.3f; error %.3f -> %.3f"),
					C.Skel, Slot, SizeScale(C.Char), U.X, U.Y, Sc.X, Sc.Y, C.Orig.X, C.Orig.Y, Ft.X, Ft.Y, Ft.Y - U.Y, Ft.Y - Sc.Y, DU, DS));
				TestTrue(*FString::Printf(TEXT("%s: the scaled head lands nearer the original"), C.Skel), DS < DU);
				TestTrue(*FString::Printf(TEXT("%s: scaled head within 0.035 of the original"), C.Skel), DS < 0.035);
				TestTrue(TEXT("ScaleAboutRoot keeps the ring point"), (ScaleAboutRoot(Root, Root, 0.5).Row(3) - Root.Row(3)).Size() < 1e-3f);
				TestTrue(TEXT("ScaleAboutRoot scales the axes"), FMath::IsNearlyEqual(Scaled.Row(0).Size(), Body[Head].Row(0).Size() * float(SizeScale(C.Char)), 1e-4f));
			}
		}
		else
		{
			SSXTest::Skip(*this, TEXT("fe_cameras.json / fe_riders.json / riders.bin missing: FE size projection skipped"));
		}
	}

	{
		FSession S;
		S.OnFeLoad();
		FRider R;
		R.Character = 3;
		R.bLoaded = true;
		FPoser Poser;
		TestTrue(TEXT("Mac poser"), Poser.Init(Lib, Lib->Skeleton(TEXT("mac")), 3));
		TArray<FString> Log;
		auto Name = [&]() { const SSXAnim::FPlayInstance* I = Poser.GetController().Newest(0); return I ? Lib->Clip(I->Subs[0].Clip)->Name : FString(); };
		R.Requested = Idle;
		Poser.Step(R, S);
		Log.Add(Name());
		TestEqual(TEXT("FEINIT hard cut: one instance at weight 1"), Poser.GetController().Instances(0).Num() * 10 + int32(Poser.GetController().Newest(0)->Weight), 11);
		for (int32 K = 0; K < 30; ++K) { Poser.Step(R, S); }
		R.Requested = Picked;
		TArray<int32> Events;
		Poser.Step(R, S, &Events);
		Log.Add(Name());
		const int32 NTrans = Lib->Clip(Poser.GetController().Newest(0)->Subs[0].Clip)->NFrames;
		int32 FadeFrames = 1;
		// 0x203A40
		while (Poser.GetController().Newest(0)->Weight < 1.0 && FadeFrames < 100) { Poser.Step(R, S, &Events); ++FadeFrames; }
		TestEqual(TEXT("TRANS cross-fade: 7 anim frames = 15 FE frames (float weights)"), FadeFrames, 15);
		TestTrue(TEXT("old FEINIT faded out"), Poser.GetController().Instances(0).Num() == 1 || Poser.GetController().Instances(0)[0].Weight < 1e-9);
		int32 Frames = FadeFrames;
		while (R.Current == Picked && Frames < 2000) { Poser.Step(R, S, &Events); ++Frames; }
		Log.Add(Name());
		TestEqual(TEXT("CYCLE after fe_riders.game_frames_until_done(n) frames"), Frames - 1, 2 * (NTrans - 1));
		const SSXAnim::FPlayInstance& Held = Poser.GetController().Instances(0)[0];
		TestEqual(TEXT("TRANS held at its last frame while fading out"), Held.Subs[0].Frame, double(NTrans - 1));
		TestEqual(TEXT("one speech event from TRANS"), Events.Num(), 1);
		if (Events.Num() == 1) { TestEqual(TEXT("Mac TRANS3 speech = 8 * 4 + 2"), Events[0], 34); }
		TArray<FString> Expect;
		for (const TSharedPtr<FJsonValue>& V : Ref->GetObjectField(TEXT("trans"))->GetArrayField(TEXT("mac_log"))) { Expect.Add(V->AsString()); }
		TestEqual(TEXT("clip log = fe_riders.FeRider (FEINIT, TRANS3, CYCLE)"), Log, Expect);
		if (Log.Num() == 3) AddInfo(FString::Printf(TEXT("clip sequence: %s / %s / %s; TRANS n = %d -> CYCLE %d FE frames after the TRANS start"), *Log[0], *Log[1], *Log[2], NTrans, Frames - 1));
	}
	return true;
}

#endif
