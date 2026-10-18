#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Compression.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Race/Tests/ReplayFileRace.inl"
#include "Sim/RiderPlaceholderStates.h"

namespace SSXWipeOutSequence
{
	constexpr int32 RecSize = 0xB50, SetSize = 0x270, HandlerSize = 0x110, CrashSize = 0x558;

	struct FCrash
	{
		int32 Index = 0, Trace = 0, Racer = 0, Entry = 0, ForceAt = 0, Last = 0;
		struct FForce { uint32 Flags; const uint8* Rec; const uint8* Next; const uint8* Set; };
		TArray<FForce> Forced;
		struct FRow { int32 Frame; uint32 Flags; const uint8* Rec; const uint8* H; const uint8* Set; const uint8* Crash; };
		TArray<FRow> Rows;
		int32 Sync001 = 0, Sync1 = 0;   // frames within 0.01 cm / 1 cm
		TArray<FVector> PortP;
		TArray<FString> PortWhat;
	};

	FVector V3(const uint8* P, int32 Off) { float V[3]; FMemory::Memcpy(V, P + Off, 12); return FVector(V[0], V[1], V[2]); }
	int32 I32(const uint8* P, int32 Off) { int32 V; FMemory::Memcpy(&V, P + Off, 4); return V; }

	constexpr int32 OtherSize = 4 + 0x460 + 0x40 + SetSize;

	bool Parse(const TArray<uint8>& File, TArray<uint8>& B, TArray<FCrash>& Out, TMap<int64, const uint8*>& Others, int32& OtherRiders)
	{
		if (File.Num() < 8 || FMemory::Memcmp(File.GetData(), "WSQ2", 4) != 0) return false;
		B.SetNumUninitialized(I32(File.GetData(), 4));
		if (!FCompression::UncompressMemory(NAME_Zlib, B.GetData(), B.Num(), File.GetData() + 8, File.Num() - 8)) return false;
		int32 N = I32(B.GetData(), 0), At = 4;
		OtherRiders = 0;
		for (int32 K = 0; K < N; ++K)
		{
			if (At + 24 > B.Num()) return false;
			FCrash& C = Out.AddDefaulted_GetRef();
			const uint8* P = B.GetData() + At;
			C.Index = K; C.Trace = I32(P, 0); C.Racer = I32(P, 4); C.Entry = I32(P, 8); C.ForceAt = I32(P, 12); C.Last = I32(P, 16);
			const int32 Riders = I32(P, 20);
			OtherRiders = Riders;
			At += 24;
			for (int32 J = 0; J < Riders; ++J)
			{
				const uint8* Q = B.GetData() + At;
				C.Forced.Add({ uint32(I32(Q, 0)), Q + 4, Q + 4 + RecSize, Q + 4 + 2 * RecSize });
				At += 4 + 2 * RecSize + SetSize;
			}
			for (int32 F = C.ForceAt + 1; F <= C.Last; ++F)
			{
				const uint8* Q = B.GetData() + At;
				C.Rows.Add({ I32(Q, 0), uint32(I32(Q, 4)), Q + 8, Q + 8 + RecSize, Q + 8 + RecSize + HandlerSize, Q + 8 + RecSize + HandlerSize + SetSize });
				At += 8 + RecSize + HandlerSize + SetSize + CrashSize;
			}
			if (At > B.Num()) return false;
		}
		const int32 M = I32(B.GetData(), At);
		At += 4;
		for (int32 K = 0; K < M; ++K)
		{
			if (At + 8 + OtherRiders * OtherSize > B.Num()) return false;
			const uint8* Q = B.GetData() + At;
			Others.Add((int64(I32(Q, 0)) << 32) | uint32(I32(Q, 4)), Q + 8);
			At += 8 + OtherRiders * OtherSize;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXWipeOutLiveSequenceTest, "SSX.WipeOut.LiveSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXWipeOutLiveSequenceTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	using namespace SSXWipeOutSequence;
	const FString RefPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("replay_rep0001.wipeout_seq.bin"));
	FString ReplayPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ORIGINAL_REPLAY"));
	if (ReplayPath.IsEmpty())
	{
		ReplayPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MemoryCard"), TEXT("BASLUS-20326-REP0001"), TEXT("BASLUS-20326-REP0001"));
	}
	TArray<uint8> Ref, In, Raw;
	TArray<FCrash> Crashes;
	TMap<int64, const uint8*> Others;
	int32 OtherRiders = 0;
	if (!FFileHelper::LoadFileToArray(Ref, *RefPath) || !FFileHelper::LoadFileToArray(In, *ReplayPath) || !Validate(In))
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: SimData/replay_rep0001.wipeout_seq.bin (tools/make_wipeout_sequence_reference.py) or the REP0001 replay file missing"));
		return true;
	}
	if (!TestTrue(TEXT("reference parsed"), Parse(Ref, Raw, Crashes, Others, OtherRiders))) return false;
	FSetup S;
	if (!TestTrue(TEXT("setup read"), ReadSetup(In, S))) return false;
	const FString TraceEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOSEQ_TRACE"));
	const int32 TraceCrash = TraceEnv.IsEmpty() ? -2 : FCString::Atoi(*TraceEnv);
	const bool bNoForce = !FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOSEQ_NOFORCE")).IsEmpty();
	const bool bSpheres = !FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOSEQ_SPHERES")).IsEmpty();
	const bool bTeachOthers = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOSEQ_OTHERS")) != TEXT("0");
	const bool bTeachSet = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOSEQ_TEACHSET")) != TEXT("0");
	const bool bTeachBoard = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOSEQ_TEACHBOARD")) != TEXT("0");
	const FString OnlyEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_WOSEQ_ONLY"));

	TArray<TArray<FCrash*>> Runs;
	for (FCrash& C : Crashes) { if (OnlyEnv.IsEmpty() || FCString::Atoi(*OnlyEnv) == C.Index) { Runs.AddDefaulted_GetRef().Add(&C); } }
	for (TArray<FCrash*>& Run : Runs)
	{
		TUniquePtr<FFileRace> Race = MakeUnique<FFileRace>();
		FString Err;
		if (!Race->Build(S, Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: race / sim data not available (%s)"), *Err));
			return true;
		}
		if (!TestTrue(TEXT("replay read into the race"), ReadReplay(In, Race->Replay))) return false;
		int32 LastFrame = 0;
		Race->bForceControl = true;
		TArray<TArray<uint8>> OtherBufs;
		OtherBufs.Reserve(16384);
		for (FCrash* C : Run)
		{
			LastFrame = FMath::Max(LastFrame, C->Last);
			for (int32 J = 0; J < C->Forced.Num() && J < Race->Sims.Num(); ++J)
			{
				const FCrash::FForce& Fo = C->Forced[J];
				if (!(Fo.Flags & 1u) || bNoForce) continue;
				const int64 Key = (int64(J) << 32) | uint32(C->ForceAt - 1);
				Race->Force.Add(Key, Fo.Rec);
				if (Fo.Flags & 4u) { Race->ForceBoard.Add(Key, Fo.Next); }
				else if (!(Fo.Flags & 2u)) { Race->ForceBoard.Add(Key, Fo.Rec); }
				if ((Fo.Flags & 8u) && !(Fo.Flags & 16u)) { Race->ForceSet.Add(Key, Fo.Set); }
			}
			for (int32 F = C->ForceAt + 1; F <= C->Last && bTeachOthers && !bNoForce; ++F)
			{
				const uint8* const* Op = Others.Find((int64(C->Trace) << 32) | uint32(F));
				if (!Op) continue;
				for (int32 J = 0; J < OtherRiders && J < Race->Sims.Num(); ++J)
				{
					if (J == C->Racer) continue;
					const uint8* O = *Op + J * OtherSize;
					const uint32 Fl = uint32(I32(O, 0));
					if (!(Fl & 1u)) continue;
					TArray<uint8>& Buf = OtherBufs.AddDefaulted_GetRef();
					Buf.SetNumZeroed(RecSize);
					FMemory::Memcpy(Buf.GetData(), O + 4, 0x460);
					FMemory::Memcpy(Buf.GetData() + 0x660, O + 4 + 0x460, 0x40);
					const int64 Key = (int64(J) << 32) | uint32(F - 1);
					Race->Force.Add(Key, Buf.GetData());
					if ((Fl & 4u) || !(Fl & 2u)) { Race->ForceBoard.Add(Key, Buf.GetData()); }
					if ((Fl & 8u) && !(Fl & 16u)) { Race->ForceSet.Add(Key, O + 4 + 0x460 + 0x40); }
				}
			}
			for (int32 K = 0; K < C->Rows.Num() && C->Rows[K].Frame < C->Entry && !bNoForce; ++K)
			{
				const FCrash::FRow& Rw = C->Rows[K];
				if (!(Rw.Flags & 1u)) continue;
				const int64 Key = (int64(C->Racer) << 32) | uint32(Rw.Frame - 1);
				Race->Force.Add(Key, Rw.Rec);
				if (bTeachBoard && (Rw.Flags & 4u) && K + 1 < C->Rows.Num()) { Race->ForceBoard.Add(Key, C->Rows[K + 1].Rec); }
				else if (bTeachBoard && !(Rw.Flags & 2u)) { Race->ForceBoard.Add(Key, Rw.Rec); }
				if (bTeachSet && (Rw.Flags & 8u) && !(Rw.Flags & 16u)) { Race->ForceSet.Add(Key, Rw.Set); }
			}
		}
		for (FCrash* C : Run) { C->PortP.Add(V3(C->Forced[C->Racer].Rec, 0x140)); C->PortWhat.Add(FString()); }
		Race->OnFrameEnd = [&]()
		{
			const int32 F = int32(Race->Replay.GetRecordFrame());
			for (FCrash* C : Run)
			{
				if (F < C->ForceAt || F >= C->Last) continue;
				const FCrash::FRow& Row = C->Rows[F - C->ForceAt];
				FRiderSim& Sim = *Race->Sims[C->Racer];
				const FRiderState& St = Sim.State;
				C->PortP.Add(St.Position);
				FString What = FString::Printf(TEXT("port %d / %d v err %.3f"), int32(St.Motion), int32(St.Control), FVector::Dist(St.Velocity, V3(Row.Rec, 0x150)));
				if (TraceCrash == C->Index && FMath::Abs(F - C->Entry) <= 2)
				{
					What += FString::Printf(TEXT(" V port %s live %s"), *St.Velocity.ToString(), *V3(Row.Rec, 0x150).ToString());
					if (const SSXSimPlaceholders::FWipeOutContext* Xw = SSXSimPlaceholders::FindWipeOutContext(Sim))
					{
						What += FString::Printf(TEXT(" BV port %s live %s info n port %s live %s speed %.3f/%.3f"), *Xw->Body.V.ToString(), *V3(Row.H, 0x5D0 - 0x4F0).ToString(),
							*Xw->Info.Normal.ToString(), *V3(Row.H, 0x530 - 0x4F0).ToString(), Xw->Info.Speed, [&Row]() { float V; FMemory::Memcpy(&V, Row.H + 0x540 - 0x4F0, 4); return V; }());
					}
				}
				if (TraceCrash == C->Index)
				{
					What += FString::Printf(TEXT(" p port %s live %s surf %d/%d kind %d/%d n port %s live %s"), *St.Position.ToString(), *V3(Row.Rec, 0x140).ToString(),
						St.SurfaceId, I32(Row.Rec, 0x290), St.ContactKind, I32(Row.Rec, 0x2E0), *St.GroundNormal.ToString(), *V3(Row.Rec, 0x2A0).ToString());
					float Sa, Sb;
					FMemory::Memcpy(&Sa, Row.Rec + 0x2E4, 4);
					FMemory::Memcpy(&Sb, Row.Rec + 0x300, 4);
					What += FString::Printf(TEXT(" stuck port %.4f %.4f live %.4f %.4f"), St.StuckAccumA, St.StuckAccumB, Sa, Sb);
					const TArray<FRiderSimEvent>& Ev = Sim.GetEvents();
					if (Ev.Num()) { What += FString::Printf(TEXT(" event %d@%d %s"), Ev.Last().To, Ev.Last().Frame - St.Frame, Ev.Last().Reason); }
				}
				if (C->Trace == 1 && St.Motion == ERiderMotion::WipeOut)
				{
					if (const SSXSimPlaceholders::FWipeOutContext* X = SSXSimPlaceholders::FindWipeOutContext(Sim))
					{
						What += FString::Printf(TEXT(" body x err %.4f anim %X"), FVector::Dist(X->Body.X, V3(Row.H, 0x590 - 0x4F0)), X->AnimId);
						if (TraceCrash == C->Index)
						{
							auto HF = [&Row](int32 Off) { float V; FMemory::Memcpy(&V, Row.H + Off - 0x4F0, 4); return double(V); };
							What += FString::Printf(TEXT(" | bv err %.4f L err %.3f t port %.5f/%.5f/%.5f live %.5f/%.5f/%.5f contact t %.2f %.2f %.2f imp %d kind %d/%d"),
								FVector::Dist(X->Body.V, V3(Row.H, 0x5D0 - 0x4F0)), FVector::Dist(X->Body.L, V3(Row.H, 0x5C0 - 0x4F0)),
								X->AirTime, X->TotalTime, X->PhaseTime, HF(0x4F8), HF(0x4FC), HF(0x500),
								X->LastContact.T1, X->LastContact.T2, X->LastContact.T3, X->LastContact.Impulses, St.ContactKind, I32(Row.Rec, 0x2E0));
							What += FString::Printf(TEXT(" path port %s live %s bv port %s live %s"), *St.PathPoint.ToString(), *V3(Row.Rec, 0x350).ToString(),
								*X->Body.V.ToString(), *V3(Row.H, 0x5D0 - 0x4F0).ToString());
						}
					}
				}
				if (TraceCrash == C->Index && C->Trace == 1)
				{
					FRiderSphereSet Bs;
					if (SSXRider::BuildPostAISphereSet(Sim.Profile, St, SSXRider::MaskAll, Bs))
					{
						double Best = 1e30;
						FString Per;
						for (int32 Dk = -1; Dk <= 1 && F - C->ForceAt + Dk < C->Rows.Num(); ++Dk)
						{
							if (F - C->ForceAt + Dk < 0) continue;
							const uint8* Ls = C->Rows[F - C->ForceAt + Dk].Set;
							double Mx = 0.0;
							FString P;
							for (int32 K = 0; K < Bs.Count && K < 18; ++K)
							{
								const double Ek = FVector::Dist(Bs.Centres[K], V3(Ls, 0x30 + 0x20 * K));
								Mx = FMath::Max(Mx, Ek);
								P += FString::Printf(TEXT(" %.4f"), Ek);
							}
							if (Mx < Best) { Best = Mx; Per = P; }
						}
						What += FString::Printf(TEXT(" set err %.4f"), Best);
						if (bSpheres) { What += TEXT(" [") + Per + TEXT("]"); }
					}
				}
				if (TraceCrash == C->Index && C->Trace == 1 && bSpheres && Race->Poses.IsValidIndex(C->Racer) && Race->Poses[C->Racer])
				{
					const SSXAnim::FRiderAnimDriver& Dv = *Race->Poses[C->Racer];
					TArray<SSXAnim::FPoseLayer> Layers;
					TArray<SSXAnim::FBonePose> Body;
					Dv.GetController().GatherLayers(0, Layers);
					SSXAnim::BlendLayers(*Dv.GetLibrary(), *Dv.GetSkeleton(), Layers, Sim.Profile.SizeScale, Sim.Profile.SizeScale, 0, Body);
					double BestE = 1e30, BestT = 1e30;
					int32 WorstBone = -1;
					for (int32 Dk = -1; Dk <= 1 && F - C->ForceAt + Dk < C->Rows.Num(); ++Dk)
					{
						if (F - C->ForceAt + Dk < 0) continue;
						const uint8* Pc = C->Rows[F - C->ForceAt + Dk].Crash;
						double Ee = 0.0, Et = 0.0;
						int32 Wb = -1;
						for (int32 Bn = 0; Bn < Body.Num() && Bn < 22; ++Bn)
						{
							const FVector Le = V3(Pc, 0x1C0 + 12 * Bn), Lt = V3(Pc, 0x2C8 + 12 * Bn);
							const double De = (FVector(Body[Bn].Euler) - Le).GetAbsMax(), Dt2 = (FVector(Body[Bn].Trans) - Lt).GetAbsMax();
							if (De > Ee) { Ee = De; Wb = Bn; }
							Et = FMath::Max(Et, Dt2);
						}
						if (Ee + Et < BestE + BestT) { BestE = Ee; BestT = Et; WorstBone = Wb; }
					}
					What += FString::Printf(TEXT(" pose euler err %.5f (bone %d) trans err %.4f layers %d"), BestE, WorstBone, BestT, Layers.Num());
					double Br = 1e30;
					FString Rs;
					for (int32 Dk = -1; Dk <= 1 && F - C->ForceAt + Dk < C->Rows.Num(); ++Dk)
					{
						if (F - C->ForceAt + Dk < 0) continue;
						const uint8* Rc = C->Rows[F - C->ForceAt + Dk].Rec + 0x600 - 0x4600;
						const double Ro = FVector::Dist(St.RenderRow3, V3(Rc, 0x4AD0)), Rr = FMath::Max3(FVector::Dist(St.RenderRow0, V3(Rc, 0x4AA0)), FVector::Dist(St.RenderRow1, V3(Rc, 0x4AB0)), FVector::Dist(St.RenderRow2, V3(Rc, 0x4AC0)));
						const double Bo = FVector::Dist(St.BoardOrigin, V3(Rc, 0x4A90)), Bro = FMath::Max3(FVector::Dist(St.BoardRow0, V3(Rc, 0x4A60)), FVector::Dist(St.BoardRow1, V3(Rc, 0x4A70)), FVector::Dist(St.BoardRow2, V3(Rc, 0x4A80)));
						if (Ro + Rr + Bo + Bro < Br)
						{
							Br = Ro + Rr + Bo + Bro;
							Rs = FString::Printf(TEXT(" root o %.4f rows %.5f board o %.4f rows %.5f"), Ro, Rr, Bo, Bro);

						}
					}
					What += Rs;
				}
				if (TraceCrash == C->Index) { What += Race->DumpAnim(C->Racer).Left(160) + FString::Printf(TEXT(" trick %d ph %d"), St.TrickId, St.TrickPhase); }
				C->PortWhat.Add(What);
			}
		};
		Race->Replay.BeginRebuild();
		for (int32 F = 0; Race->Replay.IsRebuilding() && int32(Race->Replay.GetRecordFrame()) < LastFrame - 1; ++F) { Race->Frame(); }
	}
	int32 Done = 0;
	for (FCrash& C : Crashes)
	{
		if (!OnlyEnv.IsEmpty() && FCString::Atoi(*OnlyEnv) != C.Index) continue;
		++Done;
		const int32 N = FMath::Min(C.Rows.Num(), C.PortP.Num() - 1);
		TArray<double> E;
		for (int32 K = 0; K < N; ++K)
		{
			const FVector Lp = V3(C.Rows[K].Rec, 0x140);
			double X = 1e30;
			for (int32 J = K; J <= K + 2 && J < C.PortP.Num(); ++J) { X = FMath::Min(X, FVector::Dist(C.PortP[J], Lp)); }
			E.Add(X);
		}
		const int32 K0 = FMath::Clamp(C.Entry - C.ForceAt - 1, 0, N);
		auto First = [&E, N, K0](double Tol) { for (int32 K = K0; K < N; ++K) { if (E[K] > Tol && (K + 1 >= N || E[K + 1] > Tol) && (K + 2 >= N || E[K + 2] > Tol)) return K; } return N; };
		C.Sync001 = First(0.01) - K0;
		C.Sync1 = First(1.0) - K0;
		double RunUp = 0.0;
		for (int32 K = 0; K < K0; ++K) { RunUp = FMath::Max(RunUp, E[K]); }
		auto Where = [&C, N](int32 K)
		{
			if (K >= N) return FString(TEXT("-"));
			const uint8* R = C.Rows[K].Rec;
			return FString::Printf(TEXT("f%d (live %d / %d anim %X, %s)"), C.ForceAt + K, I32(R, 0x424), I32(R, 0x428), I32(R, 0x600 + 0x46C8 - 0x4600), *C.PortWhat[K + 1]);
		};
		if (TraceCrash == C.Index || TraceCrash == -1)
		{
			for (int32 K = 0; K < N && K < 400; ++K) { AddInfo(FString::Printf(TEXT("c%d %s err %.4f"), C.Index, *Where(K), E[K])); }
		}
		AddInfo(FString::Printf(TEXT("crash %2d trace %d racer %d entry f%d (run-up from f%d, max %.3f cm): %3d / %3d frames within 0.01 cm, %3d within 1 cm; first over 0.01 cm %s; over 1 cm %s"),
			C.Index, C.Trace, C.Racer, C.Entry, C.ForceAt, RunUp, C.Sync001, N - K0, C.Sync1, *Where(C.Sync001 + K0), *Where(C.Sync1 + K0)));
	}
	TestTrue(TEXT("crashes run"), Done > 0);
	if (OnlyEnv.IsEmpty() && !bNoForce && bTeachOthers)
	{
		int32 Sum = 0;
		for (const FCrash& C : Crashes) { Sum += C.Sync001; }
		AddInfo(FString::Printf(TEXT("frames within 0.01 cm after the entries, all crashes: %d"), Sum));
		TestEqual(TEXT("crashes in the reference"), Crashes.Num(), 25);
		// 0x2361B0, 0x10C2F0, 0x12AE18
		TestTrue(TEXT("crash 0: the tumble within 0.01 cm to the reset (>= 27 frames)"), Crashes[0].Sync001 >= 27);
		TestTrue(TEXT("crash 17: the reset wait and respawn within 0.01 cm (>= 100 frames)"), Crashes[17].Sync001 >= 100);
		// 0x10C198
		TestTrue(TEXT("frames within 0.01 cm, all crashes (>= 240)"), Sum >= 240);
	}
	return !HasAnyErrors();
}

#endif
