// 0x39AFB8
#include "Characters/SSXCharacterLibrary.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Math/RandomStream.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/Anim/AflClips.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Anim/RiderPose.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Harness/RiderSetup.h"
#include "StaticMeshAttributes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace SSXBoardSnow
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FRenderTerrain
	{
		TArray<FVector> V;
		TArray<FIntVector> T;
		TMap<FIntPoint, TArray<int32>> Cells;
		static constexpr double Cell = 1000.0;

		bool Load(const FString& Stem)
		{
			FString Folder = Stem;
			Folder[0] = FChar::ToUpper(Folder[0]);
			const FString Path = FString::Printf(TEXT("/Game/Tracks/%s/%s/StaticMeshes/%s_terrain.%s_terrain"), *Folder, *Stem, *Stem, *Stem);
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
			const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
			if (!MD)
			{
				return false;
			}
			FStaticMeshConstAttributes Attr(*MD);
			TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
			TMap<FVertexID, int32> Index;
			for (const FVertexID Id : MD->Vertices().GetElementIDs())
			{
				const FVector3f P = Pos[Id];
				Index.Add(Id, V.Add(FVector(P.X, -P.Y, P.Z)));
			}
			for (const FTriangleID Tri : MD->Triangles().GetElementIDs())
			{
				TArrayView<const FVertexID> Ids = MD->GetTriangleVertices(Tri);
				const int32 K = T.Add(FIntVector(Index[Ids[0]], Index[Ids[1]], Index[Ids[2]]));
				FBox2D B(ForceInit);
				for (int32 J = 0; J < 3; ++J) { B += FVector2D(V[T[K][J]]); }
				for (int32 X = FMath::FloorToInt(B.Min.X / Cell); X <= FMath::FloorToInt(B.Max.X / Cell); ++X)
				{
					for (int32 Y = FMath::FloorToInt(B.Min.Y / Cell); Y <= FMath::FloorToInt(B.Max.Y / Cell); ++Y)
					{
						Cells.FindOrAdd(FIntPoint(X, Y)).Add(K);
					}
				}
			}
			return T.Num() > 0;
		}

		/** Board orientation terms in degrees. */
		bool HeightAt(const FVector& P, double RefZ, double& OutZ) const
		{
			const TArray<int32>* List = Cells.Find(FIntPoint(FMath::FloorToInt(P.X / Cell), FMath::FloorToInt(P.Y / Cell)));
			if (!List)
			{
				return false;
			}
			double Best = 60.0;
			bool bFound = false;
			for (const int32 K : *List)
			{
				const FVector& A = V[T[K].X];
				const FVector& B = V[T[K].Y];
				const FVector& C = V[T[K].Z];
				const double D = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
				if (FMath::Abs(D) < 1e-9)
				{
					continue;
				}
				const double L0 = ((B.Y - C.Y) * (P.X - C.X) + (C.X - B.X) * (P.Y - C.Y)) / D;
				const double L1 = ((C.Y - A.Y) * (P.X - C.X) + (A.X - C.X) * (P.Y - C.Y)) / D;
				const double L2 = 1.0 - L0 - L1;
				if (L0 < -1e-6 || L1 < -1e-6 || L2 < -1e-6)
				{
					continue;
				}
				const double Z = L0 * A.Z + L1 * B.Z + L2 * C.Z;
				if (FMath::Abs(Z - RefZ) < Best)
				{
					Best = FMath::Abs(Z - RefZ);
					OutZ = Z;
					bFound = true;
				}
			}
			return bFound;
		}
	};

	double Percentile(TArray<double> A, double Q)
	{
		if (A.Num() == 0)
		{
			return 0.0;
		}
		A.Sort();
		return A[FMath::Clamp(int32(Q * (A.Num() - 1) + 0.5), 0, A.Num() - 1)];
	}

	/** Board orientation terms in degrees. */
	struct FOrient
	{
		int32 Stance = 1;
		double Align = 0;     // 0x12AE18
		double LeanRes = 0;   // 0x129628
		double BodyRoll = 0, BodyPitch = 0;
		double Yaw = 0;
		double AnimRoll = 0;

		/** Q body rows, R root rows (lean x Q), B board rows (long, up, across), N unit smoothed up +0x2C0. */
		static FOrient Make(const FVector Q[3], const FVector R[3], const FVector B[3], const FVector& N, double Steer, int32 Class)
		{
			FOrient O;
			O.Stance = FVector::DotProduct(B[0], R[1]) >= 0.0 ? 1 : -1;
			const double S2 = FVector::DotProduct(B[2], R[0]) >= 0.0 ? 1.0 : -1.0;
			O.Align = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(N, B[0]), -1.0, 1.0)));
			O.LeanRes = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(R[0], Q[2]), FVector::DotProduct(R[0], Q[0]))
				- Steer * (Class == 2 ? 1.134464 : 0.87266463));
			O.BodyRoll = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(N, Q[0]), FVector::DotProduct(N, Q[2])));
			O.BodyPitch = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(N, Q[1]), FVector::DotProduct(N, Q[2])));
			const FVector F = (R[1] - FVector::DotProduct(R[1], N) * N).GetSafeNormal();
			const FVector B0 = B[0] * double(O.Stance);
			O.Yaw = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(N, FVector::CrossProduct(F, B0)), FVector::DotProduct(F, B0)));
			const double Roll = FMath::Atan2(FVector::DotProduct(N, B[2]), FVector::DotProduct(N, B[1]));
			O.AnimRoll = S2 * FMath::RadiansToDegrees(Roll - FMath::Atan2(S2 * FVector::DotProduct(N, R[0]), FVector::DotProduct(N, R[2])));
			return O;
		}

		static int32 Key(int32 Class, double Crouch, double SteerFollow, double Steer, int32 Stance)
		{
			const double Sg = SteerFollow != 0.0 ? SteerFollow : Steer;
			const int32 Side = (Sg >= 0.0 ? 1 : -1) * Stance;
			return Class * 100000 + FMath::RoundToInt(Crouch * 20.0) * 1000 + FMath::RoundToInt(FMath::Abs(SteerFollow) * 20.0) * 10 + (Side > 0 ? 1 : 0);
		}
	};

	struct FLiveHeights
	{
		TArray<FVector> P;
		TArray<double> H;
		TArray<double> Pose[3];
		TArray<double> Terms[3][2];
		TMap<FIntVector, TArray<double>> State;
		TMap<int32, TArray<double>> OriState[2];
		TArray<double> Ori[3][5];                 // per class (not stale): |align|, |lean residual|, body roll, body pitch, body up vs tilt target
		TMap<int32, TArray<double>> TiltBySlope;
		TMap<FIntPoint, TArray<int32>> Cells;
		static constexpr double Cell = 200.0;

		static FIntVector StateKey(int32 Class, double Crouch, double SteerFollow)
		{
			return FIntVector(Class, FMath::RoundToInt(Crouch * 20.0), FMath::RoundToInt(FMath::Abs(SteerFollow) * 20.0));
		}

		bool Load()
		{
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("gari.ground_h_live.csv"))))
			{
				return false;
			}
			for (int32 L = 1; L < Lines.Num(); ++L)
			{
				TArray<FString> C;
				Lines[L].ParseIntoArray(C, TEXT(","));
				if (C.Num() < 9)
				{
					continue;
				}
				const int32 Class = FCString::Atoi(*C[2]);
				const FVector Pos(FCString::Atod(*C[4]), FCString::Atod(*C[5]), FCString::Atod(*C[6]));
				const int32 K = P.Add(Pos);
				H.Add(FCString::Atod(*C[7]));
				if (Class >= 0 && Class < 3)
				{
					Pose[Class].Add(FCString::Atod(*C[8]));
					if (C.Num() >= 17)
					{
						const double Scale = FCString::Atod(*C[9]), Anim = FCString::Atod(*C[13]) / FMath::Max(Scale, 0.1);
						Terms[Class][0].Add(FCString::Atod(*C[10]));
						Terms[Class][1].Add(Anim);
						if (FCString::Atod(*C[16]) == 0.0)
						{
							State.FindOrAdd(StateKey(Class, FCString::Atod(*C[14]), FCString::Atod(*C[15]))).Add(Anim);
						}
					}
					if (C.Num() >= 29 && FCString::Atoi(*C[28]) == 0)
					{
						Ori[Class][0].Add(FMath::Abs(FCString::Atod(*C[19])));
						Ori[Class][1].Add(FMath::Abs(FCString::Atod(*C[21])));
						Ori[Class][2].Add(FCString::Atod(*C[22]));
						Ori[Class][3].Add(FCString::Atod(*C[23]));
						if (C.Num() >= 32)
						{
							Ori[Class][4].Add(FCString::Atod(*C[31]));
							TiltBySlope.FindOrAdd(FMath::RoundToInt(FCString::Atod(*C[29]))).Add(FCString::Atod(*C[30]));
						}
						if (FCString::Atod(*C[16]) == 0.0)
						{
							const int32 Key = FOrient::Key(Class, FCString::Atod(*C[14]), FCString::Atod(*C[15]), FCString::Atod(*C[17]), FCString::Atoi(*C[18]));
							OriState[0].FindOrAdd(Key).Add(FCString::Atod(*C[27]));
							OriState[1].FindOrAdd(Key).Add(FCString::Atod(*C[25]));
						}
					}
				}
				Cells.FindOrAdd(FIntPoint(FMath::FloorToInt(Pos.X / Cell), FMath::FloorToInt(Pos.Y / Cell))).Add(K);
			}
			return P.Num() > 0;
		}

		bool LocalMedian(const FVector& Pos, double& Out) const
		{
			TArray<double> Near;
			const int32 X0 = FMath::FloorToInt(Pos.X / Cell), Y0 = FMath::FloorToInt(Pos.Y / Cell);
			for (int32 X = X0 - 1; X <= X0 + 1; ++X)
			{
				for (int32 Y = Y0 - 1; Y <= Y0 + 1; ++Y)
				{
					if (const TArray<int32>* List = Cells.Find(FIntPoint(X, Y)))
					{
						for (const int32 K : *List)
						{
							if (FVector2D::Distance(FVector2D(P[K]), FVector2D(Pos)) < 200.0 && FMath::Abs(P[K].Z - Pos.Z) < 100.0)
							{
								Near.Add(H[K]);
							}
						}
					}
				}
			}
			if (Near.Num() < 3)
			{
				return false;
			}
			Out = Percentile(Near, 0.5);
			return true;
		}
	};

	FString HeightRunLine(const TArray<double> (&H)[4])
	{
		static const TCHAR* Names[] = { TEXT("<=10"), TEXT("11-30"), TEXT("31-120"), TEXT(">120") };
		FString Line;
		for (int32 B = 0; B < 4; ++B)
		{
			int32 Clamped = 0;
			for (const double V : H[B]) { Clamped += V < -2.4 ? 1 : 0; }
			Line += FString::Printf(TEXT("| %s n %d p25 %.3f p50 %.3f p75 %.3f h<-2.4 %.3f "), Names[B], H[B].Num(), Percentile(H[B], 0.25),
				Percentile(H[B], 0.5), Percentile(H[B], 0.75), H[B].Num() ? double(Clamped) / H[B].Num() : 0.0);
		}
		return Line;
	}

	FMatrix ToFMatrix(const SSXAnim::FRowMat& R)
	{
		FMatrix M;
		for (int32 I = 0; I < 4; ++I) { for (int32 J = 0; J < 4; ++J) { M.M[I][J] = R.M[I][J]; } }
		return M;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoardOnSnowTest, "SSX.World.BoardOnSnow", SSXBoardSnow::Flags)

bool FSSXBoardOnSnowTest::RunTest(const FString& Parameters)
{
	using namespace SSXBoardSnow;
	static const TCHAR* Tracks[] = { TEXT("snow"), TEXT("gari"), TEXT("elysium"), TEXT("mesa"), TEXT("aloha"), TEXT("merquer") };
	TArray<double> OriginAbove[3], BottomVsColl[3], BottomVsRender[3], RenderVsColl, BottomBelowOrigin[3];
	TArray<double> Heights[3], BoardPose[3];
	TArray<double> PortTerms[3][2], StateDiff[3];
	TArray<double> PortOri[3][5], OriDiff[3][3];
	TArray<double> HeightRun[4];
	TArray<double> MatchedPort, MatchedLive;
	FLiveHeights Live;
	const bool bLive = Live.Load();
	if (!bLive)
	{
		SSXTest::Skip(*this, TEXT("SimData/gari.ground_h_live.csv missing (tools/make_ground_height_reference.py): like-for-like checks skipped"));
	}
	double MinZ[3] = { 0, 0, 0 };
	int32 TracksRun = 0;
	for (const TCHAR* Stem : Tracks)
	{
		FRaceTrackData Data;
		FPatchWorld World;
		FRenderTerrain Render;
		FString Err;
		if (!Data.Load(FRaceTrackData::DefaultPath(Stem), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(Stem), &Err) || !Render.Load(Stem))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s skipped: %s"), Stem, Err.IsEmpty() ? TEXT("no terrain mesh") : *Err));
			continue;
		}
		++TracksRun;
		TArray<double> TrackRvC;
		for (const FRacePath& Line : Data.Race.AIPaths)
		{
			const double L = Line.Length();
			for (int32 K = 0; K < 200; ++K)
			{
				const FVector P = Line.PointAt(L * (K + 0.5) / 200.0);
				FRiderWorldHit Hit;
				double Rz;
				if (World.LineQuery(P + FVector(0, 0, 300.0), P - FVector(0, 0, 300.0), ERiderLineQuery::FineRefined, 0.5, Hit)
					&& Hit.Kind == 0 && Hit.Normal.Z > 0.5 && Render.HeightAt(Hit.Point, Hit.Point.Z, Rz))
				{
					TrackRvC.Add(Rz - Hit.Point.Z);
				}
			}
		}
		RenderVsColl.Append(TrackRvC);
		FRaceSession Session;
		FRaceConfig Cfg;
		Cfg.Mode = ERaceEventMode::PracticeRace;
		Session.Init(Data, Cfg);
		TArray<TUniquePtr<FRiderSim>> Sims;
		TArray<TUniquePtr<SSXAnim::FRiderAnimDriver>> Drivers;
		for (int32 Class = 0; Class < 3; ++Class)
		{
			TUniquePtr<FRiderSim>& Sim = Sims.Add_GetRef(MakeUnique<FRiderSim>());
			SSXRiderSetup::RegisterStates(*Sim, true);
			SSXRider::ApplyNewSavePlayerProfile(Sim->Profile, 10, 0);
			Sim->Profile.BoardClass = Class;
			Sim->SetWorld(&World);
			TUniquePtr<SSXAnim::FRiderAnimDriver>& D = Drivers.Add_GetRef(MakeUnique<SSXAnim::FRiderAnimDriver>());
			if (!D->Init(SSXAnim::FAflLibrary::Shared(), TEXT("brodi"), Class, 10))
			{
				SSXTest::Skip(*this, TEXT("animation library missing (anim_clips.bin): skipped"));
				return true;
			}
			Session.AddRacer(Sim.Get(), false);
		}
		Session.Start();
		Session.Tick();
		Session.SetPhase(ERacePhase::Race);
		Session.Tick();
		{
			// on the ground on the race line, 10 % down the course, side by side (as SSX.WipeOut.NoStuck places racers)
			const FRacePath& Line = Data.Race.AIPaths[Session.GetRacer(0).Race.Path];
			const double At = Line.Length() * 0.1;
			const FVector Tan = Line.TangentAt(At).GetSafeNormal2D(1e-6, FVector(0, 1, 0));
			const FVector Side = FVector::CrossProduct(Tan, FVector::UpVector);
			for (int32 I = 0; I < 3; ++I)
			{
				FRiderSim& Sim = *Sims[I];
				const FVector Pos = Line.PointAt(At) + Side * ((I - 1) * 150.0) + FVector(0, 0, 40.0);
				Sim.ApplyTransition(TEXT("T03"));
				Sim.State.Position = Pos;
				Sim.State.Velocity = Tan * 1500.0;
				Sim.SetOrientationFromAxes(Tan, FVector::UpVector);
				Sim.RunGroundProbe();
				FRacerRecord& Rec = Session.GetRacerMutable(I);
				Rec.Race.Reproject(Data.Race, Pos);
				Rec.Line.Cache = FRacePathCache();
				Session.TrackScan(I);
			}
		}
		TArray<double> TrackBvC[3], TrackBvR[3];
		TArray<double> TrackH[4];
		int32 GroundRun[3] = { 0, 0, 0 };
		int32 GroundFrames = 0;
		const bool bGari = FCString::Strcmp(Stem, TEXT("gari")) == 0;
		for (int32 F = 0; F < 60 * 40; ++F)
		{
			Session.Tick();
			for (int32 I = 0; I < 3; ++I)
			{
				FRiderSim& Sim = *Sims[I];
				Sim.Step(FRiderInputWord());
				Drivers[I]->Step(Sim);
				const FRiderState& S = Sim.State;
				GroundRun[I] = S.Motion == ERiderMotion::Ground ? GroundRun[I] + 1 : 0;
				if (S.Motion == ERiderMotion::Ground && S.SurfaceId == 1 && S.ContactKind == 0)
				{
					const int32 Bin = GroundRun[I] <= 10 ? 0 : GroundRun[I] <= 30 ? 1 : GroundRun[I] <= 120 ? 2 : 3;
					TrackH[Bin].Add(S.GroundHeight);
					HeightRun[Bin].Add(S.GroundHeight);
					double LocalH;
					if (bGari && bLive && Live.LocalMedian(S.Position, LocalH))
					{
						MatchedPort.Add(S.GroundHeight);
						MatchedLive.Add(LocalH);
					}
				}
				if (S.Motion != ERiderMotion::Ground || S.SurfaceId != 1 || F % 4 != 0)
				{
					continue;
				}
				++GroundFrames;
				SSXAnim::FRiderPalette Pal;
				Drivers[I]->ComputePalette(Sim, Pal);
				const SSXAnim::FAflLibrary* Lib = Drivers[I]->GetLibrary();
				const SSXAnim::FAflSkeleton* BoardSkel = Lib ? Lib->BoardSkeleton() : nullptr;
				const SSXAnim::FAflBoardModel* Model = Lib ? Lib->BoardModel(I, S.bStanceGoofy) : nullptr;
				if (!Pal.bValid || !BoardSkel || BoardSkel->Bones.Num() == 0 || !Model)
				{
					continue;
				}
				const SSXAnim::FAflBone& B = BoardSkel->Bones[0];
				const FMatrix ModelToWorld = ToFMatrix(SSXAnim::LocalRows(B.Euler, B.Translation)).Inverse() * FScaleMatrix(FVector(Sim.Profile.SizeScale)) * ToFMatrix(Pal.Board);
				const FVector Origin = ModelToWorld.TransformPosition(FVector::ZeroVector);
				const FVector Bottom = ModelToWorld.TransformPosition(FVector(0, 0, Model->MinZ));
				OriginAbove[I].Add(S.GroundHeight + FVector::DotProduct(Origin - S.Position, S.SmoothedUp));
				BottomBelowOrigin[I].Add(FVector::DotProduct(Bottom - Origin, S.SmoothedUp));
				Heights[I].Add(S.GroundHeight);
				{
					// 0x129628, 0x12AE18
					const FVector U = S.SmoothedUp.GetSafeNormal();
					const double Pose = FVector::DotProduct(FVector(Pal.Board.M[3][0], Pal.Board.M[3][1], Pal.Board.M[3][2]) - S.Position, U);
					const double Scale = Sim.Profile.SizeScale;
					const double T308 = S.SurfaceP10 * FVector::DotProduct(S.SmoothedUp, U);
					const double TLean = Scale * S.Lean.Cur * FVector::DotProduct(S.ContactLeft, U);
					double TChat = 0.0;
					if (S.BoardChatter > 0.0)
					{
						const FVector N = S.GroundNormal * S.BoardChatter;
						for (int32 J = 0; J < 3; ++J)
						{
							TChat += N[J] * FVector::DotProduct(FVector(Pal.Board.M[J][0], Pal.Board.M[J][1], Pal.Board.M[J][2]), U);
						}
					}
					const double Anim = (Pose - T308 - TLean - TChat) / FMath::Max(Scale, 0.1);
					BoardPose[I].Add(Pose);
					PortTerms[I][0].Add(T308);
					PortTerms[I][1].Add(Anim);
					const TArray<double>* Same = bLive && S.Brake.Cur == 0.0 ? Live.State.Find(FLiveHeights::StateKey(I, S.Crouch.Cur, S.SteerFollow.Cur)) : nullptr;
					if (Same && Same->Num() >= 10)
					{
						StateDiff[I].Add(Anim - Percentile(*Same, 0.5));
					}
				}
				{
					SSXAnim::FRiderFrameInput In = SSXAnim::FRiderAnimDriver::FrameInputs(S, Sim.Profile.SizeScale);
					const SSXAnim::FRowMat RootM = SSXAnim::RootFrame(In, 0.0, false);
					In.Steer = 0.0;
					const SSXAnim::FRowMat BodyM = SSXAnim::RootFrame(In, 0.0, false);
					FVector Q[3], R[3], Bm[3];
					for (int32 J = 0; J < 3; ++J)
					{
						Q[J] = FVector(BodyM.Row(J));
						R[J] = FVector(RootM.Row(J));
						Bm[J] = FVector(Pal.Board.Row(J));
					}
					const FOrient O = FOrient::Make(Q, R, Bm, S.SmoothedUp.GetSafeNormal(), S.Steer.Cur, I);
					PortOri[I][0].Add(FMath::Abs(O.Align));
					PortOri[I][1].Add(FMath::Abs(O.LeanRes));
					PortOri[I][2].Add(O.BodyRoll);
					PortOri[I][3].Add(O.BodyPitch);
					{
						const FVector U = S.SmoothedUp.GetSafeNormal();
						const FVector G = S.GroundNormal;
						const double Tilt = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Q[2].GetSafeNormal(), U), -1.0, 1.0)));
						if (bGari)
						{
							PortOri[I][4].Add(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Q[2].GetSafeNormal(),
								FVector(G.X, G.Y, 1.5 * G.Z).GetSafeNormal()), -1.0, 1.0))));
						}
						const TArray<double>* Same = bLive ? Live.TiltBySlope.Find(FMath::RoundToInt(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(U.Z, -1.0, 1.0))))) : nullptr;
						if (Same && Same->Num() >= 20)
						{
							OriDiff[I][2].Add(Tilt - Percentile(*Same, 0.5));
						}
					}
					if (bLive && S.Brake.Cur == 0.0)
					{
						const int32 Key = FOrient::Key(I, S.Crouch.Cur, S.SteerFollow.Cur, S.Steer.Cur, O.Stance);
						const TArray<double>* Roll = Live.OriState[0].Find(Key);
						const TArray<double>* Yaw = Live.OriState[1].Find(Key);
						if (Roll && Roll->Num() >= 10)
						{
							OriDiff[I][0].Add(O.AnimRoll - Percentile(*Roll, 0.5));
							OriDiff[I][1].Add(O.Yaw - Percentile(*Yaw, 0.5));
						}
					}
				}
				MinZ[I] = Model->MinZ;
				FRiderWorldHit Hit;
				double Rz;
				if (World.LineQuery(Bottom + FVector(0, 0, 100.0), Bottom - FVector(0, 0, 100.0), ERiderLineQuery::FineRefined, 0.5, Hit) && Hit.Kind == 0
					&& Render.HeightAt(Bottom, Hit.Point.Z, Rz))
				{
					TrackBvC[I].Add(Bottom.Z - Hit.Point.Z);
					TrackBvR[I].Add(Bottom.Z - Rz);
				}
			}
		}
		FString Line = FString::Printf(TEXT("%s: render - collision below the race line p5 %.2f p50 %.2f p95 %.2f cm (%d); %d ground samples"), Stem,
			Percentile(TrackRvC, 0.05), Percentile(TrackRvC, 0.5), Percentile(TrackRvC, 0.95), TrackRvC.Num(), GroundFrames);
		for (int32 I = 0; I < 3; ++I)
		{
			Line += FString::Printf(TEXT(" | class %d board bottom - collision p50 %.2f, - render p5 %.2f p50 %.2f (%d)"), I,
				Percentile(TrackBvC[I], 0.5), Percentile(TrackBvR[I], 0.05), Percentile(TrackBvR[I], 0.5), TrackBvR[I].Num());
			BottomVsColl[I].Append(TrackBvC[I]);
			BottomVsRender[I].Append(TrackBvR[I]);
		}
		AddInfo(Line);
		AddInfo(FString::Printf(TEXT("%s: h by frames on the ground %s"), Stem, *HeightRunLine(TrackH)));
	}
	AddInfo(FString::Printf(TEXT("all tracks: h by frames on the ground %s"), *HeightRunLine(HeightRun)));
	if (TracksRun == 0)
	{
		SSXTest::Skip(*this, TEXT("skipped: no track data / terrain meshes"));
		return true;
	}
	for (int32 I = 0; I < 3; ++I)
	{
		AddInfo(FString::Printf(TEXT("class %d: min z %.2f, (bottom - origin).up p5 %.2f p50 %.2f p95 %.2f; origin above collision p5 %.2f p25 %.2f p75 %.2f; h p50 %.2f"), I, MinZ[I],
			Percentile(BottomBelowOrigin[I], 0.05), Percentile(BottomBelowOrigin[I], 0.5), Percentile(BottomBelowOrigin[I], 0.95),
			Percentile(OriginAbove[I], 0.05), Percentile(OriginAbove[I], 0.25), Percentile(OriginAbove[I], 0.75), Percentile(Heights[I], 0.5)));
		AddInfo(FString::Printf(TEXT("class %d: board origin above the collision surface p50 %.2f cm (original %s); bottom - collision p5 %.2f p50 %.2f; bottom - render p5 %.2f p50 %.2f p95 %.2f (%d)"),
			I, Percentile(OriginAbove[I], 0.5), I == 0 ? TEXT("-1.2 .. -1.8") : TEXT("+2.9 .. +3.6"),
			Percentile(BottomVsColl[I], 0.05), Percentile(BottomVsColl[I], 0.5),
			Percentile(BottomVsRender[I], 0.05), Percentile(BottomVsRender[I], 0.5), Percentile(BottomVsRender[I], 0.95), BottomVsRender[I].Num()));
	}
	AddInfo(FString::Printf(TEXT("all tracks: render - collision p5 %.2f p50 %.2f p95 %.2f cm (%d)"),
		Percentile(RenderVsColl, 0.05), Percentile(RenderVsColl, 0.5), Percentile(RenderVsColl, 0.95), RenderVsColl.Num()));
	const double RvC = Percentile(RenderVsColl, 0.5);
	TestTrue(TEXT("render mesh vs collision surface: median within [-1, 2] cm"), RvC > -1.0 && RvC < 2.0);
	if (bLive)
	{
		const double PortH = Percentile(MatchedPort, 0.5), LiveH = Percentile(MatchedLive, 0.5);
		AddInfo(FString::Printf(TEXT("Garibaldi, same places as the trace (%d samples): h p25 %.3f p50 %.3f p75 %.3f, trace p25 %.3f p50 %.3f p75 %.3f"),
			MatchedPort.Num(), Percentile(MatchedPort, 0.25), PortH, Percentile(MatchedPort, 0.75), Percentile(MatchedLive, 0.25), LiveH,
			Percentile(MatchedLive, 0.75)));
		if (MatchedPort.Num() >= 200)
		{
			TestTrue(TEXT("h at the trace's places matches the original within 0.25 cm"), FMath::Abs(PortH - LiveH) < 0.25);
		}
		else
		{
			AddWarning(FString::Printf(TEXT("too few Garibaldi samples at traced places (%d)"), MatchedPort.Num()));
		}
		for (int32 I = 0; I < 3; ++I)
		{
			const double PortPose = Percentile(BoardPose[I], 0.5), LivePose = Percentile(Live.Pose[I], 0.5);
			AddInfo(FString::Printf(TEXT("class %d: board matrix origin above the rider origin p50 %.2f cm, original %.2f (%d)"), I, PortPose, LivePose, Live.Pose[I].Num()));
			AddInfo(FString::Printf(TEXT("class %d: terms p50 port / original: +0x308 %.3f / %.3f, animation per unit scale %.3f / %.3f; at equal riding state (crouch, |steer follow|, no brake) port - original p25 %.3f p50 %.3f p75 %.3f (%d)"),
				I, Percentile(PortTerms[I][0], 0.5), Percentile(Live.Terms[I][0], 0.5), Percentile(PortTerms[I][1], 0.5), Percentile(Live.Terms[I][1], 0.5),
				Percentile(StateDiff[I], 0.25), Percentile(StateDiff[I], 0.5), Percentile(StateDiff[I], 0.75), StateDiff[I].Num()));
			if (StateDiff[I].Num() >= 200)
			{
				TestTrue(FString::Printf(TEXT("class %d: board pose animation term matches the original at equal riding state within 0.1 cm"), I),
					FMath::Abs(Percentile(StateDiff[I], 0.5)) < 0.1);
				TestTrue(FString::Printf(TEXT("class %d: board pose +0x308 term matches the original within 0.05 cm"), I),
					FMath::Abs(Percentile(PortTerms[I][0], 0.5) - Percentile(Live.Terms[I][0], 0.5)) < 0.05);
			}
			else
			{
				SSXTest::Skip(*this, FString::Printf(TEXT("class %d: too few samples at traced riding states (%d; rerun tools/make_ground_height_reference.py)"), I, StateDiff[I].Num()));
			}
			if (BoardPose[I].Num() >= 100 && Live.Pose[I].Num() >= 100)
			{
				TestTrue(FString::Printf(TEXT("class %d: board pose median within 1.5 cm of the original (riding-mix bound)"), I), FMath::Abs(PortPose - LivePose) < 1.5);
			}
		}
	}
	for (int32 I = 0; I < 3 && bLive; ++I)
	{
		AddInfo(FString::Printf(TEXT("class %d orientation, port / original p50 (p95): |align| %.4f (%.4f) / %.4f (%.4f) deg, |lean - steer x k| %.4f (%.4f) / %.4f (%.4f) deg, body roll vs n %.2f / %.2f, body pitch vs n %.2f / %.2f (p25 %.2f / %.2f, p75 %.2f / %.2f)"), I,
			Percentile(PortOri[I][0], 0.5), Percentile(PortOri[I][0], 0.95), Percentile(Live.Ori[I][0], 0.5), Percentile(Live.Ori[I][0], 0.95),
			Percentile(PortOri[I][1], 0.5), Percentile(PortOri[I][1], 0.95), Percentile(Live.Ori[I][1], 0.5), Percentile(Live.Ori[I][1], 0.95),
			Percentile(PortOri[I][2], 0.5), Percentile(Live.Ori[I][2], 0.5), Percentile(PortOri[I][3], 0.5), Percentile(Live.Ori[I][3], 0.5),
			Percentile(PortOri[I][3], 0.25), Percentile(Live.Ori[I][3], 0.25), Percentile(PortOri[I][3], 0.75), Percentile(Live.Ori[I][3], 0.75)));
		AddInfo(FString::Printf(TEXT("class %d orientation at equal riding state (crouch, |steer follow|, toe / heel side, no brake), port - original: animation roll p25 %.3f p50 %.3f p75 %.3f deg, board yaw p25 %.3f p50 %.3f p75 %.3f deg (%d)"), I,
			Percentile(OriDiff[I][0], 0.25), Percentile(OriDiff[I][0], 0.5), Percentile(OriDiff[I][0], 0.75),
			Percentile(OriDiff[I][1], 0.25), Percentile(OriDiff[I][1], 0.5), Percentile(OriDiff[I][1], 0.75), OriDiff[I][0].Num()));
		AddInfo(FString::Printf(TEXT("class %d body tilt (body up vs n) at equal slope, port - original p25 %.3f p50 %.3f p75 %.3f deg (%d); body up vs tilt target (Garibaldi) p50 %.3f / %.3f deg"), I,
			Percentile(OriDiff[I][2], 0.25), Percentile(OriDiff[I][2], 0.5), Percentile(OriDiff[I][2], 0.75), OriDiff[I][2].Num(),
			Percentile(PortOri[I][4], 0.5), Percentile(Live.Ori[I][4], 0.5)));
		if (PortOri[I][0].Num() >= 100 && Live.Ori[I][0].Num() >= 100)
		{
			TestTrue(FString::Printf(TEXT("class %d: board long axis lies in the ground plane (|asin(n . b0)| p95 < 0.01 deg; original p50 < 0.01)"), I),
				Percentile(PortOri[I][0], 0.95) < 0.01 && Percentile(Live.Ori[I][0], 0.5) < 0.01);
			TestTrue(FString::Printf(TEXT("class %d: lean = steer x 50 deg (ex 65) about the forward axis (port p95 < 0.001 deg; original p50 < 0.05)"), I),
				Percentile(PortOri[I][1], 0.95) < 0.001 && Percentile(Live.Ori[I][1], 0.5) < 0.05);
		}
		if (OriDiff[I][0].Num() >= 200)
		{
			TestTrue(FString::Printf(TEXT("class %d: board animation roll matches the original at equal riding state within 0.1 deg"), I), FMath::Abs(Percentile(OriDiff[I][0], 0.5)) < 0.1);
			TestTrue(FString::Printf(TEXT("class %d: board yaw vs the root forward matches the original at equal riding state within 0.02 deg"), I), FMath::Abs(Percentile(OriDiff[I][1], 0.5)) < 0.02);
		}
		else
		{
			AddWarning(FString::Printf(TEXT("class %d: too few samples at traced riding states for the orientation (%d)"), I, OriDiff[I][0].Num()));
		}
		if (OriDiff[I][2].Num() >= 200)
		{
			TestTrue(FString::Printf(TEXT("class %d: body tilt against n matches the original at equal slope within 0.15 deg"), I), FMath::Abs(Percentile(OriDiff[I][2], 0.5)) < 0.15);
		}
		if (PortOri[I][4].Num() >= 50 && Live.Ori[I][4].Num() >= 100)
		{
			TestTrue(FString::Printf(TEXT("class %d: body up vs the tilt target on Garibaldi within 0.5 deg of the original (ride-dependent bound)"), I),
				FMath::Abs(Percentile(PortOri[I][4], 0.5) - Percentile(Live.Ori[I][4], 0.5)) < 0.5);
		}
	}
	static const double Original[3] = { -1.38, 3.32, 3.00 };
	for (int32 I = 0; I < 3; ++I)
	{
		if (OriginAbove[I].Num() < 100)
		{
			AddWarning(FString::Printf(TEXT("class %d: too few ground samples (%d)"), I, OriginAbove[I].Num()));
			continue;
		}
		if (!bLive)
		{
			TestTrue(FString::Printf(TEXT("class %d: board origin height matches the original within 1 cm"), I),
				FMath::Abs(Percentile(OriginAbove[I], 0.5) - Original[I]) < 1.0);
		}
		TestTrue(FString::Printf(TEXT("class %d: drawn board bottom = board origin + model min z along the board up"), I),
			FMath::Abs(Percentile(BottomBelowOrigin[I], 0.5) - MinZ[I]) < 0.15);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXBoardClearanceTest, "SSX.World.BoardClearance", SSXBoardSnow::Flags)

bool FSSXBoardClearanceTest::RunTest(const FString& Parameters)
{
	using namespace SSXBoardSnow;
	static const TCHAR* Tracks[] = { TEXT("snow"), TEXT("gari"), TEXT("elysium"), TEXT("mesa"), TEXT("aloha"), TEXT("merquer") };
	static const TCHAR* Bins[3] = { TEXT("tail"), TEXT("middle"), TEXT("nose") };
	TArray<double> All[2][3][3];
	int32 TracksRun = 0;
	for (const TCHAR* Stem : Tracks)
	{
		FRaceTrackData Data;
		FPatchWorld World;
		FRenderTerrain Render;
		FString Err;
		if (!Data.Load(FRaceTrackData::DefaultPath(Stem), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(Stem), &Err) || !Render.Load(Stem))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s skipped: %s"), Stem, Err.IsEmpty() ? TEXT("no terrain mesh") : *Err));
			continue;
		}
		FRaceSession Session;
		FRaceConfig Cfg;
		Cfg.Mode = ERaceEventMode::PracticeRace;
		Session.Init(Data, Cfg);
		TArray<TUniquePtr<FRiderSim>> Sims;
		TArray<TUniquePtr<SSXAnim::FRiderAnimDriver>> Drivers;
		TArray<FVector> Verts[3];
		for (int32 Class = 0; Class < 3; ++Class)
		{
			UStaticMesh* Mesh = SSXCharacters::LoadBoardMesh(TEXT("brodi"), Class);
			const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
			if (!MD)
			{
				SSXTest::Skip(*this, TEXT("board meshes not imported: skipped"));
				return true;
			}
			FStaticMeshConstAttributes Attr(*MD);
			TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
			for (const FVertexID Id : MD->Vertices().GetElementIDs()) { Verts[Class].Add(FVector(Pos[Id])); }
			TUniquePtr<FRiderSim>& Sim = Sims.Add_GetRef(MakeUnique<FRiderSim>());
			SSXRiderSetup::RegisterStates(*Sim, true);
			SSXRider::ApplyNewSavePlayerProfile(Sim->Profile, 10, 0);
			Sim->Profile.BoardClass = Class;
			Sim->SetWorld(&World);
			TUniquePtr<SSXAnim::FRiderAnimDriver>& D = Drivers.Add_GetRef(MakeUnique<SSXAnim::FRiderAnimDriver>());
			if (!D->Init(SSXAnim::FAflLibrary::Shared(), TEXT("brodi"), Class, 10))
			{
				SSXTest::Skip(*this, TEXT("animation library missing (anim_clips.bin): skipped"));
				return true;
			}
			Session.AddRacer(Sim.Get(), false);
		}
		++TracksRun;
		Session.Start();
		Session.Tick();
		TArray<double> Track[2][3][3];
		const int32 StartFrames = 90, RaceFrames = 60 * 10;
		for (int32 F = 0; F < StartFrames + RaceFrames; ++F)
		{
			if (F == StartFrames)
			{
				Session.SetPhase(ERacePhase::Race);
			}
			Session.Tick();
			const int32 Part = F < StartFrames ? 0 : 1;
			for (int32 I = 0; I < 3; ++I)
			{
				FRiderSim& Sim = *Sims[I];
				Sim.Step(FRiderInputWord());
				Drivers[I]->Step(Sim);
				const FRiderState& S = Sim.State;
				if (F % 3 != 0 || (Part == 1 && (S.Motion != ERiderMotion::Ground || S.SurfaceId != 1)))
				{
					continue;
				}
				SSXAnim::FRiderPalette Pal;
				Drivers[I]->ComputePalette(Sim, Pal);
				const SSXAnim::FAflLibrary* Lib = Drivers[I]->GetLibrary();
				const SSXAnim::FAflSkeleton* BoardSkel = Lib ? Lib->BoardSkeleton() : nullptr;
				const SSXAnim::FAflBoardModel* Model = Lib ? Lib->BoardModel(I, false) : nullptr;
				if (!Pal.bValid || !BoardSkel || BoardSkel->Bones.Num() == 0 || !Model)
				{
					continue;
				}
				const SSXAnim::FAflBone& B = BoardSkel->Bones[0];
				const FMatrix MirrorY(FPlane(1, 0, 0, 0), FPlane(0, -1, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, 0, 0, 1));
				const FMatrix MeshToWorld = MirrorY * FTranslationMatrix(FVector(0, 0, Model->MinZ))
					* ToFMatrix(SSXAnim::LocalRows(B.Euler, B.Translation)).Inverse() * ToFMatrix(Pal.Board);
				const FVector Center = MeshToWorld.TransformPosition(FVector::ZeroVector);
				const FVector Fwd = S.BasisForward.GetSafeNormal();
				double HalfLen = 1.0;
				TArray<FVector> W;
				for (const FVector& V : Verts[I])
				{
					W.Add(MeshToWorld.TransformPosition(V));
					HalfLen = FMath::Max(HalfLen, FMath::Abs(FVector::DotProduct(W.Last() - Center, Fwd)));
				}
				double Lowest[3] = { 1e9, 1e9, 1e9 };
				for (const FVector& P : W)
				{
					const double T = FVector::DotProduct(P - Center, Fwd) / HalfLen;
					const int32 Bin = T < -1.0 / 3.0 ? 0 : T > 1.0 / 3.0 ? 2 : 1;
					double Rz;
					if (Render.HeightAt(P, P.Z, Rz))
					{
						Lowest[Bin] = FMath::Min(Lowest[Bin], P.Z - Rz);
					}
				}
				for (int32 K = 0; K < 3; ++K)
				{
					if (Lowest[K] < 1e8)
					{
						Track[Part][I][K].Add(Lowest[K]);
					}
				}
			}
		}
		for (int32 Part = 0; Part < 2; ++Part)
		{
			FString Line = FString::Printf(TEXT("%s %s:"), Stem, Part == 0 ? TEXT("start slots (Static)") : TEXT("riding (ground, surface 1)"));
			for (int32 I = 0; I < 3; ++I)
			{
				Line += FString::Printf(TEXT(" | class %d"), I);
				for (int32 K = 0; K < 3; ++K)
				{
					Line += FString::Printf(TEXT(" %s p5 %.2f p50 %.2f"), Bins[K], Percentile(Track[Part][I][K], 0.05), Percentile(Track[Part][I][K], 0.5));
					All[Part][I][K].Append(Track[Part][I][K]);
				}
			}
			AddInfo(Line);
		}
	}
	if (TracksRun == 0)
	{
		SSXTest::Skip(*this, TEXT("skipped: no track data / terrain meshes"));
		return true;
	}
	for (int32 Part = 0; Part < 2; ++Part)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			AddInfo(FString::Printf(TEXT("all tracks, %s, class %d: lowest vertex above the render mesh, tail p5 %.2f p50 %.2f, middle p5 %.2f p50 %.2f, nose p5 %.2f p50 %.2f cm (%d)"),
				Part == 0 ? TEXT("start") : TEXT("riding"), I,
				Percentile(All[Part][I][0], 0.05), Percentile(All[Part][I][0], 0.5), Percentile(All[Part][I][1], 0.05), Percentile(All[Part][I][1], 0.5),
				Percentile(All[Part][I][2], 0.05), Percentile(All[Part][I][2], 0.5), All[Part][I][0].Num()));
		}
	}
	static const TCHAR* Parents[] = {
		TEXT("/Game/World/M_SSXRiderLit.M_SSXRiderLit"), TEXT("/Game/World/M_SSXRiderLit_Masked.M_SSXRiderLit_Masked"),
		TEXT("/Game/World/M_SSXRiderLitChrome.M_SSXRiderLitChrome"), TEXT("/Game/World/M_SSXRiderLitChrome_Masked.M_SSXRiderLitChrome_Masked"),
		TEXT("/Game/World/M_SSXTerrain.M_SSXTerrain"), TEXT("/Game/World/M_SSXTerrain_Masked.M_SSXTerrain_Masked"),
		TEXT("/Game/World/M_SSXTerrainOpaque.M_SSXTerrainOpaque") };
	for (const TCHAR* Path : Parents)
	{
		UMaterial* M = LoadObject<UMaterial>(nullptr, Path);
		if (!M)
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("%s missing (Scripts/make_world_materials.py)"), Path));
			continue;
		}
		const FString N = M->GetName();
		TestTrue(N + TEXT(": opaque or masked (depth-tested, depth-writing)"), M->GetBlendMode() == BLEND_Opaque || M->GetBlendMode() == BLEND_Masked);
		TestFalse(N + TEXT(": depth test on"), M->bDisableDepthTest);
		TestFalse(N + TEXT(": no pixel depth offset"), M->GetEditorOnlyData()->PixelDepthOffset.IsConnected());
		const bool bRider = N.StartsWith(TEXT("M_SSXRiderLit"));
		TestEqual(N + (bRider ? TEXT(": world position offset = the rider depth pull") : TEXT(": no world position offset")),
			M->GetEditorOnlyData()->WorldPositionOffset.IsConnected(), bRider);
	}
	return true;
}

#endif
