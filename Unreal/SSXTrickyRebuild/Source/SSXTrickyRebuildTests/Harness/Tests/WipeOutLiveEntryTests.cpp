// 0x10DAD8, 0x10C2F0, 0x10DCA8, 0x10E190
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Race/SSXReplayFile.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderPlaceholderStates.h"
#include "Sim/RiderSim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/WipeOut/TumbleBody.h"
#include "Sim/EEFloat.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FSample
	{
		int32 Case = 0, Rider = 0, Frame = 0, Char = 0, Stance = 0, BoardClass = 0;
		bool bHuman = false;
		double Scale = 1.0;
		TArray<uint8> B, A, P, H;

		float BF(int32 Off) const { float V; FMemory::Memcpy(&V, B.GetData() + Off, 4); return V; }
		int32 BI(int32 Off) const { int32 V; FMemory::Memcpy(&V, B.GetData() + Off, 4); return V; }
		FVector BV(int32 Off) const { return FVector(BF(Off), BF(Off + 4), BF(Off + 8)); }
		FQuat BQ(int32 Off) const { return FQuat(BF(Off + 4), BF(Off + 8), BF(Off + 12), BF(Off)); }
		float HF(int32 Off) const { float V; FMemory::Memcpy(&V, H.GetData() + Off - 0x4F0, 4); return V; }
		FVector HV(int32 Off) const { return FVector(HF(Off), HF(Off + 4), HF(Off + 8)); }
		FQuat HQ(int32 Off) const { return FQuat(HF(Off + 4), HF(Off + 8), HF(Off + 12), HF(Off)); }
		float PF(int32 Off) const { float V; FMemory::Memcpy(&V, P.GetData() + Off - 0x57B0, 4); return V; }
		int32 PInt(int32 Off) const { int32 V; FMemory::Memcpy(&V, P.GetData() + Off - 0x57B0, 4); return V; }
		int32 Anim() const { int32 V; FMemory::Memcpy(&V, A.GetData() + 0x46C8 - 0x4600, 4); return V; }
		int32 Motion() const { return BI(0x424); }
		void Spheres(FVector Out[SSXRider::NumShapeSpheres]) const
		{
			for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I) { Out[I] = BV(0x4A0 + 0x20 * I); }
		}
		float Total() const { return HF(0x4FC); }
	};

	bool Hex(const FString& S, TArray<uint8>& Out)
	{
		Out.SetNumUninitialized(S.Len() / 2);
		return HexToBytes(S, Out.GetData()) == Out.Num();
	}

	bool LoadSamples(TArray<FSample>& Out)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("snow.wipeout_entry.csv"))))
		{
			return false;
		}
		for (int32 L = 1; L < Lines.Num(); ++L)
		{
			TArray<FString> C;
			Lines[L].ParseIntoArray(C, TEXT(","), false);
			if (C.Num() < 12) { continue; }
			FSample S;
			S.Case = FCString::Atoi(*C[0]);
			S.Rider = FCString::Atoi(*C[1]);
			S.Frame = FCString::Atoi(*C[2]);
			S.bHuman = FCString::Atoi(*C[3]) != 0;
			S.Char = FCString::Atoi(*C[4]);
			S.Stance = FCString::Atoi(*C[5]);
			S.BoardClass = FCString::Atoi(*C[6]);
			S.Scale = FCString::Atod(*C[7]);
			if (!Hex(C[8], S.B) || !Hex(C[9], S.A) || !Hex(C[10], S.P) || !Hex(C[11], S.H) || S.B.Num() < 0xB58) { return false; }
			Out.Add(MoveTemp(S));
		}
		return Out.Num() > 0;
	}

	struct FStat
	{
		int32 N = 0, Exact = 0, Over = 0;
		double Worst = 0.0;
		FString WorstAt;
		void Add(double Err, double Tol, const FString& At)
		{
			++N;
			Exact += Err == 0.0;
			Over += Err > Tol;
			if (Err > Worst) { Worst = Err; WorstAt = At; }
		}
	};

	double VErr(const FVector& A, const FVector& B) { return FMath::Max3(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y), FMath::Abs(A.Z - B.Z)); }
	double QErr(const FQuat& A, const FQuat& B)
	{
		return FMath::Max(FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y)), FMath::Max(FMath::Abs(A.Z - B.Z), FMath::Abs(A.W - B.W)));
	}
	double RelErr(const FVector& A, const FVector& B) { return VErr(A, B) / FMath::Max(1.0, B.GetAbsMax()); }

	double InvIErr(const FMatrix& M, const FSample& S)
	{
		double E = 0.0;
		for (int32 K = 0; K < 9; ++K)
		{
			const double L = S.HF(0x56C + 4 * K);
			E = FMath::Max(E, FMath::Abs(M.M[K / 3][K % 3] - L) / FMath::Max(1e-12, FMath::Abs(L)));
		}
		return E;
	}

	// 0x236448, 0x153C50
	void StartSet(const FSample& A, const FSample& B, const FSample& Src, FVector Out[SSXRider::NumShapeSpheres])
	{
		Src.Spheres(Out);
		if (&Src != &B) { return; }
		const float Dt = A.BF(0x12C) * 0.016666668f;
		const FVector3f Xa = SSXEE::F(A.HV(0x590)), Va = SSXEE::F(A.HV(0x5D0));
		const FVector3f Push = SSXEE::Sub(SSXEE::F(B.HV(0x590)), SSXEE::Add(Xa, SSXEE::Scale(Dt, Va)));
		for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I) { Out[I] = SSXEE::D(SSXEE::Sub(SSXEE::F(Out[I]), Push)); }
	}

	SSXSimPlaceholders::FWipeOutMotion* MakeSim(FRiderSim& Sim, const FPatchWorld& World, const FSample& S)
	{
		SSXRiderSetup::RegisterStates(Sim);
		SSXRider::ApplyNewSavePlayerProfile(Sim.Profile, S.Char, S.BoardClass);
		Sim.Profile.SizeScale = S.Scale;
		Sim.Profile.Stance = S.Stance;
		Sim.Profile.BoardClass = S.BoardClass;
		Sim.SetWorld(&World);
		Sim.Reset();
		Sim.Spawn(S.BV(0x140), FVector(0, 1, 0), FVector::ZeroVector, 0.0, true);
		Sim.State.BoardClass = S.BoardClass;
		return static_cast<SSXSimPlaceholders::FWipeOutMotion*>(Sim.GetMotionState(ERiderMotion::WipeOut));
	}

	void ForceRider(FRiderSim& Sim, const FSample& S, const FSample& SetFrom, const FVector* StartSpheres = nullptr)
	{
		FRiderState& St = Sim.State;
		SSXReplayFile::RestoreBoarderRecord(S.B.GetData() + 0x10, St);
		St.SurfaceId = S.BI(0x290);
		St.ContactKind = S.BI(0x2E0);
		St.bStanceGoofy = S.BI(0x1B4) != 0;
		St.PathPoint = S.BV(0x350);
		St.PathLookahead = S.BV(0x360);
		St.BoardClass = S.BoardClass;
		FLandingPrediction& LP = St.Prediction;
		LP.TimeLeft = S.PF(0x57B8);
		LP.LandPoint = FVector(S.PF(0x57C0), S.PF(0x57C4), S.PF(0x57C8));
		LP.LandVelocity = FVector(S.PF(0x57D0), S.PF(0x57D4), S.PF(0x57D8));
		LP.LandNormal = FVector(S.PF(0x57E0), S.PF(0x57E4), S.PF(0x57E8));
		LP.LandSurface = S.PInt(0x57F0);
		LP.FlightTime = S.PF(0x57F4);
		LP.ApexTime = S.PF(0x57F8);
		LP.AirElapsed = S.PF(0x57FC);
		if (StartSpheres) { FMemory::Memcpy(St.PosedSpheres, StartSpheres, sizeof(St.PosedSpheres)); }
		else { SetFrom.Spheres(St.PosedSpheres); }
		St.bPosedSpheresValid = true;
		St.PosedPivot = SetFrom.BV(0x8C8);
		St.SphereFrameP = FVector::ZeroVector;
		St.SphereFrameQ = FQuat::Identity;
		St.bSphereFrameValid = true;
	}

	void ForceBody(SSXWipeOut::FTumbleBody& Bd, const FSample& S)
	{
		Bd.bValid = true;
		Bd.InvMass = S.HF(0x568);
		Bd.InvI = FMatrix::Identity;
		for (int32 K = 0; K < 9; ++K) { Bd.InvI.M[K / 3][K % 3] = S.HF(0x56C + 4 * K); }
		Bd.X = S.HV(0x590);
		Bd.Q = S.HQ(0x5A0);
		Bd.P = S.HV(0x5B0);
		Bd.L = S.HV(0x5C0);
		Bd.V = S.HV(0x5D0);
		Bd.W = S.HV(0x5E0);
	}

	struct FStats
	{
		FStat EnterX, EnterInvI, EnterP, EnterAnim;
		FStat Px, Pv, Pq, Pm, Pn, Bx, Bq, BP, BL, Bv, Bw, BInvI, Tim, Kind, Anim;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWipeOutLiveEntryTest, "SSX.WipeOut.LiveEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXWipeOutLiveEntryTest::RunTest(const FString& Parameters)
{
	TArray<FSample> All;
	if (!LoadSamples(All))
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: SimData/snow.wipeout_entry.csv missing (tools/make_wipeout_entry_reference.py)"));
		return true;
	}
	FPatchWorld World;
	FString Err;
	if (!World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("snow")), &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: %s"), *Err));
		return true;
	}
	const int32 TraceCase = FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOENTRY_TRACE")));
	TMap<int32, TArray<const FSample*>> Cases;
	TArray<int32> Order;
	for (const FSample& S : All)
	{
		if (!Cases.Contains(S.Case)) { Order.Add(S.Case); }
		Cases.FindOrAdd(S.Case).Add(&S);
	}
	FStats St;
	constexpr double Tol = 0.01;
	int32 Entries = 0, Steps = 0, Unsampled = 0, Torn = 0;
	for (const int32 Case : Order)
	{
		const TArray<const FSample*>& Rows = Cases[Case];
		int32 E = -1;
		for (int32 I = 1; I < Rows.Num() && E < 0; ++I)
		{
			if (Rows[I]->Motion() == 5 && Rows[I - 1]->Motion() == 1) { E = I; }
		}
		if (E < 1) { continue; }
		const bool bTrace = Case == TraceCase || TraceCase == -1;
		{
			const FSample& Se = *Rows[E];
			const FVector Xl = Se.HV(0x590);
			int32 SetAt = -1;
			double Best = 1e30;
			for (int32 K = E; K >= FMath::Max(0, E - 3); --K)
			{
				FVector C[SSXRider::NumShapeSpheres];
				Rows[K]->Spheres(C);
				FMatrix M;
				const double D = VErr(SSXWipeOut::MassProperties(C, M), Xl);
				if (D < Best) { Best = D; SetAt = K; }
			}
			if (Best < 0.05)
			{
				++Entries;
				FRiderSim Sim;
				SSXSimPlaceholders::FWipeOutMotion* Motion = MakeSim(Sim, World, Se);
				ForceRider(Sim, Se, *Rows[SetAt]);
				Sim.State.Velocity = Se.HV(0x5D0);
				Sim.State.Orientation = Se.HQ(0x5A0);
				Sim.UpdateBasis();
				FRiderImpact Impact;
				Impact.Point = Se.HV(0x510);
				Impact.Direction = Se.HV(0x520);
				Impact.Normal = -Se.HV(0x530);   // Enter negates it into +0x40 / +0x2F0
				Impact.Speed = Se.HF(0x540);
				Sim.StartWipeOut(Impact, TEXT("live entry"));
				const SSXWipeOut::FTumbleBody& Bd = Motion->GetContext().Body;
				const FString At = FString::Printf(TEXT("case %d f%d"), Case, Se.Frame);
				St.EnterX.Add(VErr(Bd.X, Xl), Tol, At);
				St.EnterInvI.Add(InvIErr(Bd.InvI, Se), 1e-5, At);
				St.EnterP.Add(RelErr(Bd.P, Se.HV(0x5B0)), 1e-6, At);
				int32 LiveAnim = Se.Anim();
				for (int32 K = E; K < FMath::Min(E + 3, Rows.Num()) && !(LiveAnim >= 0x2E1 && LiveAnim <= 0x2ED); ++K) { LiveAnim = Rows[K]->Anim(); }
				St.EnterAnim.Add(Motion->GetContext().AnimId != LiveAnim ? 1.0 : 0.0, 0.5, At);
				if (bTrace)
				{
					AddInfo(FString::Printf(TEXT("c%d entry f%d (set of f%d): x port %s live %s | P port %s live %s | anim port %X live %X"), Case, Se.Frame, Rows[SetAt]->Frame,
						*Bd.X.ToString(), *Xl.ToString(), *Bd.P.ToString(), *Se.HV(0x5B0).ToString(), Motion->GetContext().AnimId, LiveAnim));
				}
			}
			else
			{
				AddInfo(FString::Printf(TEXT("case %d: entry set not sampled (closest centroid %.2f cm off the body x)"), Case, Best));
			}
		}
		int32 CaseSteps = 0;
		for (int32 I = E; I + 1 < Rows.Num() && CaseSteps < 30; ++I)
		{
			const FSample& A = *Rows[I];
			const FSample& B = *Rows[I + 1];
			if (A.Motion() != 5 || B.Motion() != 5) { break; }
			const float Dt = A.BF(0x12C) * 0.016666668f;
			if (B.Total() != A.Total() + Dt) { continue; }
			if (A.Anim() == 0x2DC || A.Anim() == 0x2DD) { break; }
			auto Complete = [](const FSample& P, const FSample& C)
			{
				const float D = C.BF(0x12C) * 0.016666668f;
				return C.HF(0x4F8) == P.HF(0x4F8) + D || (C.HF(0x4F8) == 0.0f && C.HF(0x500) == P.HF(0x500) + D);
			};
			if (!Complete(A, B) || (I > E && Rows[I - 1]->Motion() == 5 && A.Total() == Rows[I - 1]->Total() + Dt && !Complete(*Rows[I - 1], A)))
			{
				++Torn;
				continue;
			}
			// 0x10DCA8
			const FSample* SetFrom = nullptr;
			{
				double BestRes = 1e30;
				for (const FSample* Cand : { &A, &B })
				{
					FVector C[SSXRider::NumShapeSpheres];
					StartSet(A, B, *Cand, C);
					FMatrix M;
					const FVector Cen = SSXWipeOut::MassProperties(C, M);
					const FVector Na = A.BV(0x2A0);
					const FVector Lift = 0.5 * (B.BF(0x294) + 10.0) * FMath::Max(Na.Z, 0.0) * Na;
					const FVector Pb = B.HV(0x590) - B.HQ(0x5A0).RotateVector(A.BQ(0x170).Inverse().RotateVector(Cen + Lift - A.BV(0x140)));
					const double Res = VErr(Pb, B.BV(0x140));
					if (InvIErr(M, B) < 1e-4 && Res < BestRes) { BestRes = Res; SetFrom = Cand; }
				}
				if (!SetFrom || BestRes > 1.0)
				{
					++Unsampled;
					continue;
				}
			}
			FRiderSim Sim;
			SSXSimPlaceholders::FWipeOutMotion* Motion = MakeSim(Sim, World, A);
			if (!Motion) { break; }
			{
				FRiderImpact Impact;
				Impact.Normal = FVector(0, 0, -1);
				Sim.StartWipeOut(Impact, TEXT("live tumble"));
			}
			FVector Start[SSXRider::NumShapeSpheres];
			StartSet(A, B, *SetFrom, Start);
			ForceRider(Sim, A, *SetFrom, Start);
			Sim.UpdateBasis();
			SSXSimPlaceholders::FWipeOutContext& X = Motion->GetContext();
			X.AirTime = A.HF(0x4F8);
			X.TotalTime = A.HF(0x4FC);
			X.PhaseTime = A.HF(0x500);
			X.AirTurn = A.HF(0x504);
			X.AirTilt = A.HF(0x508);
			X.Info.Point = A.HV(0x510);
			X.Info.Direction = A.HV(0x520);
			X.Info.Normal = A.HV(0x530);
			X.Info.Speed = A.HF(0x540);
			X.bTumblePoseSimulated = true;
			X.AnimId = (A.Anim() >= 0x2DC && A.Anim() <= 0x2ED) ? A.Anim() : B.Anim();
			X.AnimPlayFrame = Sim.State.Frame;
			X.AnimFrames = 0;
			ForceBody(X.Body, A);
			FRiderInputWord W;
			W.AirTurn = B.HF(0x504);
			W.AirTilt = B.HF(0x508);
			Sim.Step(W);
			++Steps;
			++CaseSteps;
			const FRiderState& R = Sim.State;
			const SSXWipeOut::FTumbleBody& Bd = X.Body;
			const FString At = FString::Printf(TEXT("case %d f%d"), Case, B.Frame);
			St.Px.Add(VErr(R.Position, B.BV(0x140)), Tol, At);
			St.Pv.Add(VErr(R.Velocity, B.BV(0x150)), Tol, At);
			St.Pq.Add(QErr(R.Orientation, B.BQ(0x170)), 1e-5, At);
			St.Pm.Add(VErr(R.LastImpactNormal, B.BV(0x2F0)), 1e-5, At);
			St.Pn.Add(VErr(R.GroundNormal, B.BV(0x2A0)), 1e-4, At);
			St.Bx.Add(VErr(Bd.X, B.HV(0x590)), Tol, At);
			St.Bq.Add(QErr(Bd.Q, B.HQ(0x5A0)), 1e-5, At);
			St.BP.Add(RelErr(Bd.P, B.HV(0x5B0)), 1e-5, At);
			St.BL.Add(RelErr(Bd.L, B.HV(0x5C0)), 1e-4, At);
			St.Bv.Add(VErr(Bd.V, B.HV(0x5D0)), Tol, At);
			St.Bw.Add(VErr(Bd.W, B.HV(0x5E0)), 1e-3, At);
			St.BInvI.Add(InvIErr(Bd.InvI, B), 1e-5, At);
			St.Tim.Add(FMath::Max3(FMath::Abs(X.AirTime - B.HF(0x4F8)), FMath::Abs(X.TotalTime - B.HF(0x4FC)), FMath::Abs(X.PhaseTime - B.HF(0x500))), 1e-6, At);
			St.Kind.Add(R.ContactKind != B.BI(0x2E0) ? 1.0 : 0.0, 0.5, At);
			St.Anim.Add(X.AnimId != B.Anim() ? 1.0 : 0.0, 0.5, At);
			if (bTrace)
			{
				AddInfo(FString::Printf(TEXT("c%d f%d->f%d (set f%d) anim %X->%X/%X kind %d/%d | p err %.4f v %.4f | x %.4f P %.2e L %.2e w %.2e | port x %s live %s | port L %s live %s | contact t1 %.2f t2 %.2f t3 %.2f imp %d"),
					Case, A.Frame, B.Frame, SetFrom->Frame, A.Anim(), X.AnimId, B.Anim(), R.ContactKind, B.BI(0x2E0), VErr(R.Position, B.BV(0x140)), VErr(R.Velocity, B.BV(0x150)),
					VErr(Bd.X, B.HV(0x590)), RelErr(Bd.P, B.HV(0x5B0)), RelErr(Bd.L, B.HV(0x5C0)), VErr(Bd.W, B.HV(0x5E0)),
					*Bd.X.ToString(), *B.HV(0x590).ToString(), *Bd.L.ToString(), *B.HV(0x5C0).ToString(),
					X.LastContact.T1, X.LastContact.T2, X.LastContact.T3, X.LastContact.Impulses));
				if (VErr(R.Position, B.BV(0x140)) > 0.0 || RelErr(Bd.L, B.HV(0x5C0)) > 1e-6 || RelErr(Bd.P, B.HV(0x5B0)) > 1e-6)
				{
					AddInfo(FString::Printf(TEXT("   detail: P port %s live %s | L port %s live %s | t port %.6f %.6f %.6f live %.6f %.6f %.6f | n port %s live %s | p port %s live %s | turn/tilt %.4f %.4f pred %.4f"),
						*Bd.P.ToString(), *B.HV(0x5B0).ToString(), *Bd.L.ToString(), *B.HV(0x5C0).ToString(), X.AirTime, X.TotalTime, X.PhaseTime,
						B.HF(0x4F8), B.HF(0x4FC), B.HF(0x500), *R.GroundNormal.ToString(), *B.BV(0x2A0).ToString(), *R.Position.ToString(), *B.BV(0x140).ToString(),
						A.HF(0x504), A.HF(0x508), A.PF(0x57B8)));
				}
			}
		}
	}
	auto Report = [this](const TCHAR* Name, const FStat& S)
	{
		AddInfo(FString::Printf(TEXT("%-10s n %3d exact %3d over-tol %3d worst %.3g (%s)"), Name, S.N, S.Exact, S.Over, S.Worst, *S.WorstAt));
	};
	AddInfo(FString::Printf(TEXT("%d entries, %d one-step tumble updates (skipped: %d without a sampled start set, %d with a sample inside the update)"), Entries, Steps, Unsampled, Torn));
	Report(TEXT("enter x"), St.EnterX); Report(TEXT("enter I^-1"), St.EnterInvI); Report(TEXT("enter P"), St.EnterP); Report(TEXT("enter anim"), St.EnterAnim);
	Report(TEXT("p"), St.Px); Report(TEXT("v"), St.Pv); Report(TEXT("q"), St.Pq); Report(TEXT("+0x2F0"), St.Pm); Report(TEXT("n"), St.Pn);
	Report(TEXT("body x"), St.Bx); Report(TEXT("body q"), St.Bq); Report(TEXT("body P"), St.BP); Report(TEXT("body L"), St.BL);
	Report(TEXT("body v"), St.Bv); Report(TEXT("body w"), St.Bw); Report(TEXT("body I^-1"), St.BInvI); Report(TEXT("timers"), St.Tim);
	Report(TEXT("kind"), St.Kind); Report(TEXT("anim"), St.Anim);
	TestTrue(TEXT("entries present"), Entries >= 10);
	TestTrue(TEXT("entry body x, I^-1, P bit-exact (0x10DAD8 / 0x10D6F8 / 0x236530)"),
		St.EnterX.Exact == Entries && St.EnterInvI.Exact == Entries && St.EnterP.Exact == Entries);
	TestTrue(TEXT("entry TumbleAnim pick (0x10F9A0)"), St.EnterAnim.Over == 0);
	TestTrue(TEXT("tumble updates present"), Steps >= 60);
	const int32 Few = FMath::Max(1, Steps / 20);
	TestTrue(TEXT("tumble body x within 0.01 cm (0x153C50, 0x10E190 pushes)"), St.Bx.Over == 0);
	TestTrue(TEXT("tumble contact kind"), St.Kind.Over == 0);
	TestTrue(FString::Printf(TEXT("tumble rider p within 0.01 cm (0x10DCA8 rebuild) but %d"), Few), St.Px.Over <= Few);
	TestTrue(FString::Printf(TEXT("tumble rider q within 1e-5 but %d"), Few), St.Pq.Over <= Few);
	TestTrue(FString::Printf(TEXT("tumble body P / L within 1e-5 / 1e-4 relative but %d"), Few), St.BP.Over <= Few && St.BL.Over <= Few);
	return true;
}

#endif
