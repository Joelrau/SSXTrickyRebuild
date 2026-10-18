// 0x243A60, 0x194910
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Characters/SSXCharacterAppearance.h"
#include "Characters/SSXCharacterLibrary.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Harness/RiderSetup.h"
#include "Input/PadMapper.h"
#include "Input/RiderInputDriver.h"
#include "Progression/SSXHeats.h"
#include "Progression/SSXProgression.h"
#include "Race/GameRng.h"
#include "Effects/TrackEffects.h"
#include "Sim/Ground/PropCollision.h"
#include "World/PropNodes.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Race/SSXHuffCodec.h"
#include "Race/SSXReplay.h"
#include "Race/SSXReplayFile.h"
#include "Sim/EEFloat.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/Anim/RiderAnimDriver.h"
#include "Sim/Anim/RiderAnim.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/Rider/RiderShape.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TArray<uint8> FromHex(const TCHAR* Hex)
	{
		TArray<uint8> Out;
		const int32 N = FCString::Strlen(Hex) / 2;
		Out.SetNumUninitialized(N);
		HexToBytes(FString(Hex), Out.GetData());
		return Out;
	}

	struct FBytes
	{
		TArray<uint8> B;
		void U32(uint32 V) { for (int32 I = 0; I < 4; ++I) B.Add(uint8(V >> (8 * I))); }
		void F32(float V) { uint32 U; FMemory::Memcpy(&U, &V, 4); U32(U); }
		void Huff(const TArray<uint8>& Raw) { TArray<uint8> C; SSXHuff::Encode(Raw.GetData(), Raw.Num(), C); U32(C.Num()); B.Append(C); }
	};

	void Put32(TArray<uint8>& B, int32 At, uint32 V) { FMemory::Memcpy(B.GetData() + At, &V, 4); }
	uint32 Get32(const TArray<uint8>& B, int32 At) { uint32 V = 0; FMemory::Memcpy(&V, B.GetData() + At, 4); return V; }

	TArray<uint8> MakeSnapshot(uint32 Seed, TConstArrayView<FVector> Pos)
	{
		FBytes S;
		const FGameRng B(Seed);
		for (int32 I = 0; I < 6; ++I) S.U32(B.S[I]);
		S.U32(0x18); S.U32(0xDEADBEF1); for (int32 I = 0; I < 4; ++I) S.U32(I);
		for (const FVector& P : Pos)
		{
			const int32 At = S.B.Num();
			S.B.AddZeroed(0x450);
			Put32(S.B, At, 0x450);
			Put32(S.B, At + 4, 0xDEADBEF8);
			float F[3] = { float(P.X), float(P.Y), float(P.Z) };
			FMemory::Memcpy(S.B.GetData() + At + 0x130, F, 12);
			S.U32(1); S.U32(0);
		}
		return S.B;
	}

	// 0x194820, 0x2912E8
	void Seal(TArray<uint8>& F)
	{
		uint32 Sum = SSXReplayFile::ChecksumSeed;
		for (int32 I = 0; I < SSXReplayFile::StreamMax; ++I) { Sum += uint32(int32(int8(F[SSXReplayFile::SetupBytes + I]))); }
		Put32(F, SSXReplayFile::LengthAt, SSXReplayFile::StreamMax);
		Put32(F, SSXReplayFile::ChecksumAt, Sum);
	}

	int32 FirstDiff(const TArray<uint8>& A, const TArray<uint8>& B)
	{
		for (int32 I = 0; I < FMath::Min(A.Num(), B.Num()); ++I) { if (A[I] != B[I]) return I; }
		return A.Num() == B.Num() ? -1 : FMath::Min(A.Num(), B.Num());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayFormatStartMeterTest, "SSX.Replay.Format.StartMeter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayFormatStartMeterTest::RunTest(const FString& Parameters)
{
	// 0x16CFD8
	SSXReplayFile::FSetup S;
	S.NumRacers = 6;
	SSXReplayFile::SetStartMeter(S.Racers[2], SSXHeats::HeatFactor(0));
	TArray<uint8> B;
	B.SetNumZeroed(SSXReplayFile::SetupBytes);
	SSXReplayFile::EncodeSetup(S, B.GetData());
	uint32 W = 0;
	FMemory::Memcpy(&W, B.GetData() + 4 + 2 * SSXReplayFile::ProfileBytes + 0x70, 4);
	TestEqual(TEXT("entry 2 +0x70 = 0x3F553F3C"), W, 0x3F553F3Cu);
	SSXReplayFile::FSetup R;
	SSXReplayFile::DecodeSetup(B.GetData(), R);
	TestEqual(TEXT("read back"), SSXReplayFile::StartMeter(R.Racers[2]), SSXHeats::HeatFactor(0));
	TestEqual(TEXT("others 0"), SSXReplayFile::StartMeter(R.Racers[0]), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayFormatHuffTest, "SSX.Replay.Format.Huff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayFormatHuffTest::RunTest(const FString& Parameters)
{
	const char* Text = "SSX Tricky replay HUFF codec 0x30FB";
	TArray<uint8> V1((const uint8*)Text, FCStringAnsi::Strlen(Text));
	TArray<uint8> V2;
	V2.AddZeroed(40);
	for (int32 I = 0; I < 5; ++I) { V2.Add(1); V2.Add(2); V2.Add(3); }
	for (int32 I = 0; I < 300; ++I) { V2.Add(0x7F); }
	V2.Add('e'); V2.Add('n'); V2.Add('d');
	struct FCase { const TArray<uint8>* In; const TCHAR* Hex; };
	const FCase Cases[] = {
		{ &V1, TEXT("30fb0000237a92a8a09052409d20a05ce24874ccbecd150cec28d94d3865eeeee2752209cc2a087a30297e40") },
		{ &V2, TEXT("30fb000166809e645903fc0418648ce68ac6186186cd025feff764") },
	};
	for (const FCase& C : Cases)
	{
		TArray<uint8> Enc, Dec;
		SSXHuff::Encode(C.In->GetData(), C.In->Num(), Enc);
		TestEqual(TEXT("encoder output (0x243A60)"), Enc, FromHex(C.Hex));
		TestTrue(TEXT("decoder (0x240EF8)"), SSXHuff::Decode(Enc.GetData(), Enc.Num(), Dec) && Dec == *C.In);
	}
	TArray<TArray<uint8>> More;
	{
		TArray<uint8>& A = More.AddDefaulted_GetRef();
		for (int32 R = 0; R < 2; ++R) for (int32 I = 0; I < 256; ++I) A.Add(uint8(I));
		for (int32 I = 0; I < 20; ++I) A.Add(5);
		for (int32 I = 0; I < 256; I += 3) A.Add(uint8(I));
		TArray<uint8> Enc;
		SSXHuff::Encode(A.GetData(), A.Num(), Enc);
		TestEqual(TEXT("all byte values: 714 bytes (as the original's encoder)"), Enc.Num(), 714);
	}
	{
		TArray<uint8>& A = More.AddDefaulted_GetRef();
		A.SetNumUninitialized(0x6664);
		for (int32 I = 0; I < 0x6664 / 4; ++I) { const uint32 W = I < 300 ? uint32(I * 37 % 11) << 11 : 0xDEADC0DEu; FMemory::Memcpy(&A[I * 4], &W, 4); }
	}
	{
		TArray<uint8>& A = More.AddDefaulted_GetRef();
		uint32 F0 = 1, F1 = 1;
		for (int32 S = 0; S < 24; ++S)
		{
			for (uint32 K = 0; K < F0; ++K) A.Add(uint8(S * 7 + 1));
			const uint32 T = F0 + F1; F0 = F1; F1 = FMath::Min(T, 40000u);
		}
		for (int32 I = 0; I < A.Num(); I += 2) { Swap(A[I], A[(I * 7919) % A.Num()]); }
	}
	for (const TArray<uint8>& A : More)
	{
		TArray<uint8> Enc, Dec;
		SSXHuff::Encode(A.GetData(), A.Num(), Enc);
		TestTrue(FString::Printf(TEXT("round trip of %d bytes"), A.Num()), SSXHuff::Decode(Enc.GetData(), Enc.Num(), Dec) && Dec == A);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayFormatSyntheticTest, "SSX.Replay.Format.Synthetic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayFormatSyntheticTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	TArray<uint8> F;
	F.SetNumZeroed(FileBytes);
	// 0x32DB6C
	Put32(F, 0, 2);
	{
		uint8* P = F.GetData() + 4;
		Put32(F, 4, 0);
		FMemory::Memcpy(P + 4, "CPU", 3);
		for (int32 I = 0; I < 16; ++I) P[0x0E + I] = uint8(0x40 + I);
		P[0x1E] = 54;
		P[0x20] = 50; P[0x21] = 100; P[0x22] = 66; P[0x23] = 0;
		Put32(F, 4 + 0x38, 11); Put32(F, 4 + 0x3C, 11); Put32(F, 4 + 0x44, 1);
		Put32(F, 4 + 0x48, 11 * 12 + 1); Put32(F, 4 + 0x4C, 11 * 7 + 3);
		Put32(F, 4 + 0x50, 11); Put32(F, 4 + 0x54, 11); Put32(F, 4 + 0x58, 11); Put32(F, 4 + 0x5C, 11);
		const float Sc = 0.85f; FMemory::Memcpy(P + 0x60, &Sc, 4);
		P[0x64] = 11; P[0x65] = 0xFF; P[0x66] = 0xFF; P[0x67] = 0xFF;
		Put32(F, 4 + 0x68, 0xFF); Put32(F, 4 + 0x6C, 0xFFFFFFFFu); Put32(F, 4 + 0x80, 2);
	}
	{
		uint8* P = F.GetData() + 4 + ProfileBytes;
		Put32(F, 4 + ProfileBytes, 1);
		FMemory::Memcpy(P + 4, "JOE", 3);
		for (int32 I = 0; I < 16; ++I) P[0x0E + I] = uint8(0x80 - I);
		P[0x1E] = 80;
		Put32(F, 4 + ProfileBytes + 0x40, 1); Put32(F, 4 + ProfileBytes + 0x44, 1);
		const float Sc = 0.85f; FMemory::Memcpy(P + 0x60, &Sc, 4);
		P[0x65] = 0; P[0x66] = 0xFF; P[0x67] = 0xFF;
		Put32(F, 4 + ProfileBytes + 0x74, 9); Put32(F, 4 + ProfileBytes + 0x80, 2);
	}
	Put32(F, 0x31C, 1); Put32(F, 0x320, 2); Put32(F, 0x324, 0xAEE0F2A5u); Put32(F, 0x328, 0xDD835CB7u); Put32(F, 0x330, 3);
	for (int32 K = 0; K < 15; ++K)
	{
		Put32(F, 0x33C + 100 * K, K == 0 ? 0 : 0x39);
		for (int32 J = 0; J < 6; ++J)
		{
			Put32(F, 0x340 + 100 * K + 16 * J, 13); Put32(F, 0x344 + 100 * K + 16 * J, 0x300);
			Put32(F, 0x348 + 100 * K + 16 * J, 0x1BF); Put32(F, 0x34C + 100 * K + 16 * J, 0xFFFFFFFFu);
		}
	}
	Put32(F, 0x918, 1);
	Put32(F, SetupCrcAt, SSXProg::Crc16(F.GetData(), SetupBytes));
	// 0x193F70
	const TArray<FVector> P0 = { FVector(-65090.0, 6067.7, 1.0), FVector(-65066.3, 6067.9, 1.0) };
	const TArray<FVector> P1 = { FVector(-4669.6, -28857.3, 12.5), FVector(-3354.2, -32823.9, 7.25) };
	const TArray<uint8> Snap0 = MakeSnapshot(0xAEE0F2A5u, P0), Snap1 = MakeSnapshot(0x1234u, P1);
	FBytes S;
	S.U32(HeaderMagic); S.U32(2); S.U32(0x960); S.U32(0); S.U32(0x80); S.U32(0x80); S.U32(1);
	S.U32(1); S.U32(0); S.U32(0); S.U32(0); S.U32(0);
	for (int32 I = 0; I < 5; ++I) S.U32(StaleChunkWord);
	S.U32(1); S.U32(0);
	S.F32(1.57f); S.F32(-0.7f); S.F32(300.f); S.F32(1.65f); S.U32(4); S.U32(7); S.U32(0);
	S.F32(0.f); S.F32(-0.5f); S.F32(180.f); S.F32(1.65f); S.U32(1); S.U32(4); S.U32(0);
	S.B.AddZeroed(0x1C * 127);
	S.U32(KeyTag); S.U32(Snap0.Num()); S.U32(0); S.U32(0); S.U32(1); S.U32(KeyTail); S.Huff(Snap0);
	S.U32(KeyTag); S.U32(Snap1.Num()); S.U32(0x3C); S.U32(1); S.U32(0); S.U32(KeyTail); S.Huff(Snap1);
	auto Clip = [&S]() { S.U32(ClipTag); S.U32(1); S.U32(0x3C); S.U32(0x50); S.F32(2.f); S.F32(0.f); S.U32(1); S.U32(0); };
	Clip();
	{
		// input recorder: 3 runs over the 0xDEADC0DE buffer
		TArray<uint8> Words, Runs;
		Words.SetNumUninitialized(RecorderWordBytes);
		for (int32 I = 0; I < RecorderWordBytes / 4; ++I) { const uint32 W = I == 0 ? 0u : I == 1 ? 0x34u : I == 2 ? 0x6800u : RecorderFill; FMemory::Memcpy(&Words[I * 4], &W, 4); }
		Runs.SetNumUninitialized(RecorderRunBytes);
		const uint8 Fill[4] = { 0xDE, 0xC0, 0xAD, 0xDE };
		for (int32 I = 0; I < RecorderRunBytes; ++I) Runs[I] = I == 0 ? 42 : I == 1 ? 1 : I == 2 ? 85 : Fill[(0x6664 + I) & 3];
		S.U32(InputTag); S.U32(3); S.Huff(Words); S.Huff(Runs);
	}
	S.U32(HighlightTag); S.U32(1); S.U32(0x3C); S.U32(0); S.U32(0); S.U32(0);
	Clip();
	FMemory::Memcpy(F.GetData() + SetupBytes, S.B.GetData(), S.B.Num());
	Seal(F);

	TestTrue(TEXT("valid (length, 0xDEADBEEF + signed bytes)"), Validate(F));
	TestTrue(TEXT("setup CRC"), SetupCrcValid(F.GetData()));
	FSetup Setup;
	TestTrue(TEXT("setup read"), ReadSetup(F, Setup));
	TestTrue(TEXT("RS fields"), Setup.NumRacers == 2 && Setup.Venue == 1 && Setup.GameMode == 2 && Setup.Seed2B00 == 0xAEE0F2A5u
		&& Setup.HeatWord == 3 && Setup.Focus == 1 && Setup.NumHumans() == 1 && Setup.RacerOfHuman(0) == 1);
	TestTrue(TEXT("computer racer: board / outfit from the asset ids"), Setup.Racers[0].Board() == 1 && Setup.Racers[0].Outfit() == 3
		&& Setup.Racers[0].Char38 == 11 && Setup.Racers[0].NameString() == TEXT("CPU") && Setup.Racers[0].Rival[0][2] == 66);
	TestTrue(TEXT("human racer"), Setup.Racers[1].IsHuman() && Setup.Racers[1].NameString() == TEXT("JOE") && Setup.Racers[1].Rank == 9
		&& Setup.Racers[1].Stats[3] == 0x7D);
	FSSXReplay R;
	R.Init(2, 1, false, nullptr);
	R.AttachHuman(0);
	if (!TestTrue(TEXT("replay read (0x194910)"), ReadReplay(F, R))) return false;
	TestTrue(TEXT("header: lock, frames, loaded"), R.GetLockInterval() == 0x960u && R.GetRecordFrame() == 0x80u && R.GetRecordedLength() == 0x80u && R.IsLoaded());
	TestEqual(TEXT("keyframes"), R.KeyframeFrames(), TArray<uint32>({ 0u, 0x3Cu }));
	TestTrue(TEXT("camera track"), R.GetTrack(0).Count == 1 && R.GetTrack(0).Single.Mode == 7 && R.GetTrack(0).Cuts[0].Cam.C == 180.f);
	TestTrue(TEXT("clip by key list index"), R.GetRiderClips(0).Num() == 1 && R.GetKey(R.GetClip(R.GetRiderClips(0)[0]).Key).Frame == 0x3Cu
		&& R.GetClip(R.GetRiderClips(0)[0]).Value == 2.f);
	TestTrue(TEXT("highlight slot by key frame"), R.GetHighlights(0).Slots[0] == R.GetRiderClips(0)[0] && R.GetHighlights(0).Slots[1] < 0);
	TestTrue(TEXT("input runs"), R.GetInput(0).NumRuns() == 3 && R.GetInput(0).Words[2] == 0x6800u && R.GetInput(0).Runs[2] == 85);
	FSnapshot Sn;
	const int32 K1 = R.GetRiderClips(0).Num() > 0 ? R.GetClip(R.GetRiderClips(0)[0]).Key : -1;
	TestTrue(TEXT("snapshot: stream B and the rider positions (+0x140)"), K1 >= 0 && ParseSnapshot(R.GetKey(K1).Snapshot, Sn)
		&& Sn.Positions.Num() == 2 && Sn.Positions[1].Equals(P1[1], 0.01) && Sn.StreamB[0] == FGameRng(0x1234u).S[0]);
	TArray<uint8> Out;
	TestTrue(TEXT("written"), Write(R, Setup, Out));
	const int32 D = FirstDiff(Out, F);
	TestEqual(TEXT("written back byte-identical (first difference)"), D, -1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayFormatPortWrittenTest, "SSX.Replay.Format.PortWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayFormatPortWrittenTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	struct FWorld : public SSXReplay::IReplayWorld
	{
		uint32 Frame = 0;
		virtual void CaptureKeyframe(int32 Key, uint32 InFrame, bool) override { Frame = InFrame; }
		virtual void FileSnapshot(int32 Key, TArray<uint8>& Out) const override { const FGameRng B(Frame + 7); PortSnapshot(B.S, Out); }
	} W;
	FSSXReplay R;
	R.Init(4, 1, false, &W);
	R.AttachHuman(0);
	R.OnStartRace();
	for (int32 I = 0; I < 3000; ++I)
	{
		R.Update();
		uint32 Word = uint32(I / 25) * 3 + 1;
		R.RecordOrPlayInput(0, Word);
		if (I == 700) { R.HighlightStart(0, 1); }
		if (I == 760) { R.HighlightValue(0, 2.f); R.HighlightCommit(0); }
	}
	R.EndRecording();
	R.Update();
	R.StartFullReplay();
	FSetup Setup;
	Setup.NumRacers = 4;
	Setup.Racers[2].Port = 0;
	Setup.Seed2B00 = 99;
	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("written"), Write(R, Setup, Bytes))) return false;
	// 0x194910
	int32 At = SetupBytes;
	auto U = [&]() { const uint32 V = Get32(Bytes, At); At += 4; return V; };
	TestEqual(TEXT("magic"), U(), HeaderMagic);
	const int32 Keys = int32(U());
	At += 4 * 4;
	TestEqual(TEXT("humans (+0x18 = cAI+0x7C)"), int32(U()), 1);
	const int32 Clips0 = int32(U());
	At += 4 * 4;
	TestEqual(TEXT("stale header words"), U(), StaleChunkWord);
	At = SetupBytes + HeaderBytes + TrackBytes;
	TArray<uint32> Frames;
	for (int32 K = 0; K < Keys; ++K)
	{
		TestEqual(TEXT("keyframe tag"), U(), KeyTag);
		const int32 Size = int32(U());
		Frames.Add(U());
		At += 8;
		TestEqual(TEXT("keyframe tail tag"), U(), KeyTail);
		const int32 Len = int32(U());
		TArray<uint8> Snap;
		TestTrue(TEXT("snapshot HUFF block"), SSXHuff::Decode(Bytes.GetData() + At, Len, Snap) && Snap.Num() == Size && Size == 0x18);
		At += Len;
	}
	TArray<uint32> Expect = R.KeyframeFrames();
	Expect.RemoveAt(Expect.Num() - 1);   // 0x191B60
	TestEqual(TEXT("the keyframes but the end one"), Frames, Expect);
	TestTrue(TEXT("a highlight clip"), Clips0 >= 1);
	for (int32 C = 0; C < Clips0; ++C) { TestEqual(TEXT("clip tag"), U(), ClipTag); At += 0x1C; }
	TestEqual(TEXT("input tag"), U(), InputTag);
	TestEqual(TEXT("runs"), int32(U()), R.GetInput(0).NumRuns());
	for (int32 Bl = 0; Bl < 2; ++Bl)
	{
		const int32 Len = int32(U());
		TArray<uint8> Buf;
		TestTrue(TEXT("recorder buffer"), SSXHuff::Decode(Bytes.GetData() + At, Len, Buf) && Buf.Num() == (Bl == 0 ? RecorderWordBytes : RecorderRunBytes));
		if (Bl == 0) TestEqual(TEXT("fill past the runs"), Get32(Buf, R.GetInput(0).NumRuns() * 4), RecorderFill);
		At += Len;
	}
	TestEqual(TEXT("highlight tag"), U(), HighlightTag);
	FSSXReplay L;
	L.Init(4, 1, false, nullptr);
	L.AttachHuman(0);
	TestTrue(TEXT("read back"), ReadReplay(Bytes, L));
	FSetup Back;
	TestTrue(TEXT("setup back"), ReadSetup(Bytes, Back) && Back.Seed2B00 == 99u && Back.RacerOfHuman(0) == 2);
	TArray<uint8> Again;
	TestTrue(TEXT("rewritten"), Write(L, Back, Again));
	TestEqual(TEXT("rewritten byte-identical (first difference)"), FirstDiff(Again, Bytes), -1);
	return !HasAnyErrors();
}

#include "Race/Tests/ReplayFileRace.inl"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayFormatUserFileTest, "SSX.Replay.Format.UserFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayFormatUserFileTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	const FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ORIGINAL_REPLAY"));
	if (Path.IsEmpty())
	{
		SSXTest::Skip(*this, TEXT("SSX_ORIGINAL_REPLAY not set (a BASLUS-20326-REP%04d file from a memory card): skipped"));
		return true;
	}
	TArray<uint8> In;
	if (!TestTrue(TEXT("file read"), FFileHelper::LoadFileToArray(In, *Path))) return false;
	TestTrue(TEXT("valid (0x2912E8)"), Validate(In));
	TestTrue(TEXT("setup CRC (+0x334)"), In.Num() >= SetupBytes && SetupCrcValid(In.GetData()));
	FSetup S;
	if (!TestTrue(TEXT("setup read"), ReadSetup(In, S))) return false;
	AddInfo(FString::Printf(TEXT("RS: racers %d, venue %d (%s), mode %d, seeds %08x / %08x, time %d, heat word %d, difficulty %d, focus %d, +0x91C %d"),
		S.NumRacers, S.Venue, VenueStem(S.Venue), S.GameMode, S.Seed2B00, S.Seed2B04, S.TimeToBeat, S.HeatWord, S.Difficulty, S.Focus, S.Word91C));
	for (int32 I = 0; I < FMath::Min(S.NumRacers, MaxRacers); ++I)
	{
		const FRacer& R = S.Racers[I];
		FString Stats, Riv;
		for (const uint8 B : R.Stats) Stats += FString::Printf(TEXT("%02x"), B);
		for (int32 J = 0; J < MaxRacers; ++J) Riv += FString::Printf(TEXT(" %d/%d/%d/%d"), R.Rival[J][0], R.Rival[J][1], R.Rival[J][2], R.Rival[J][3]);
		AddInfo(FString::Printf(TEXT("racer %d '%s' %s char %d board %d outfit %d class %d stance %d scale %.2f mass %d rank %d stats %s rivals%s"),
			I, *R.NameString(), R.IsHuman() ? *FString::Printf(TEXT("human %d"), R.Port) : TEXT("computer"), R.Char38, R.Board(), R.Outfit(),
			R.BoardClass, R.Stance, R.Scale, R.Mass, R.Rank, *Stats, *Riv));
	}
	for (int32 K = 0; K < SSXIntroDialogue::NumSlots; ++K)
	{
		AddInfo(FString::Printf(TEXT("intro slot %d type %d line0 %d/%d/%d/%d"), K, S.Slots[K].Type, S.Slots[K].Lines[0].Speaker, S.Slots[K].Lines[0].LineId,
			S.Slots[K].Lines[0].Aux, S.Slots[K].Lines[0].Value));
	}
	FSSXReplay R;
	R.Init(FMath::Clamp(S.NumRacers, 1, MaxRacers), S.NumHumans(), false, nullptr);
	for (int32 P = 0; P < S.NumHumans(); ++P) { R.AttachHuman(P); }
	if (!TestTrue(TEXT("replay object read (0x194910)"), ReadReplay(In, R))) return false;
	AddInfo(FString::Printf(TEXT("replay: length %u, record frame %u, lock %u, full %d, focus %d, runs %d, clips %d, camera cuts %d"),
		R.GetRecordedLength(), R.GetRecordFrame(), R.GetLockInterval(), R.IsFull() ? 1 : 0, R.GetFocus(), R.GetInput(0).NumRuns(),
		R.GetRiderClips(0).Num(), R.GetTrack(0).Count));
	for (int32 K = 0; K < R.GetKeyframePool(); ++K)
	{
		const SSXReplay::FKeyframe& Key = R.GetKey(K);
		if (!Key.bInUse) continue;
		FSnapshot Sn;
		ParseSnapshot(Key.Snapshot, Sn);
		FString Pos;
		for (const FVector& P : Sn.Positions) Pos += FString::Printf(TEXT(" (%.1f %.1f %.1f)"), P.X, P.Y, P.Z);
		const uint32 Calls = Sn.StreamB[5] - FGameRng(S.Seed2B00).S[5];
		AddInfo(FString::Printf(TEXT("keyframe frame %u refs %d lock %d: snapshot %d bytes, stream B calls %u, %d riders:%s"), Key.Frame, Key.Refs, Key.Lock,
			Key.Snapshot.Num(), Calls, Sn.Positions.Num(), *Pos));
		FGameRng G(S.Seed2B00);
		for (uint32 N = 0; N < Calls && N < 1000000u; ++N) { G.Next(); }
		TestTrue(FString::Printf(TEXT("keyframe %u: stream B = seed advanced %u calls (all six words)"), Key.Frame, Calls),
			FMemory::Memcmp(G.S, Sn.StreamB, sizeof(G.S)) == 0);
	}
	for (const int32 C : R.GetRiderClips(0))
	{
		const SSXReplay::FClip& Cl = R.GetClip(C);
		AddInfo(FString::Printf(TEXT("clip key frame %u type %d length %d value %.3f impact %.3f"), R.GetKey(Cl.Key).Frame, Cl.Type, Cl.Length, Cl.Value, Cl.Impact));
	}
	TArray<uint8> Out;
	TestTrue(TEXT("written"), Write(R, S, Out));
	const int32 D = FirstDiff(Out, In);
	if (D >= 0) { AddError(FString::Printf(TEXT("round trip differs first at 0x%X (file %02x, port %02x)"), D, In.IsValidIndex(D) ? In[D] : 0, Out.IsValidIndex(D) ? Out[D] : 0)); }
	else { AddInfo(TEXT("round trip: load -> save byte-identical (0x80000 bytes)")); }

	TUniquePtr<FFileRace> Race = MakeUnique<FFileRace>();
	FString Err;
	if (!Race->Build(S, Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("%s race / sim data not available (%s): the re-run is skipped"), VenueStem(S.Venue), *Err));
		return !HasAnyErrors();
	}
	if (!TestTrue(TEXT("replay read into the race"), ReadReplay(In, Race->Replay))) return false;
	const FString DumpPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_PORT_DUMP"));
	Race->bDump = !DumpPath.IsEmpty();
	Race->Replay.BeginRebuild();
	const int32 Guard = int32(R.GetRecordedLength()) + 60 * 180;
	for (int32 F = 0; F < Guard && Race->Replay.IsRebuilding(); ++F) { Race->Frame(); }
	TestFalse(TEXT("every keyframe rebuilt"), Race->Replay.IsRebuilding());
	if (Race->bDump) { FFileHelper::SaveStringArrayToFile(Race->Dump, *DumpPath); }
	for (int32 I = 0; I < Race->Sims.Num(); ++I)
	{
		const FRiderProfile& Pf = Race->Sims[I]->Profile;
		FString G;
		for (int32 K = 0; K < 15; ++K) { G += FString::Printf(TEXT(" %d"), FPadMapper::GrabSlot(Pf.GetTrickCharacter(), Pf.BoardClass, K)); }
		AddInfo(FString::Printf(TEXT("racer %d: trick stat %d, grab slots%s"), I, int32(Pf.StatBytes[0x1D - 0x0E]), *G));
	}
	for (const FFileRace::FCheck& C : Race->Checks)
	{
		FString Per;
		double MaxAi = 0.0;
		for (int32 I = 0; I < C.C.Errors.Num(); ++I) { Per += FString::Printf(TEXT(" %.1f"), C.C.Errors[I]); if (I != Race->Human) MaxAi = FMath::Max(MaxAi, double(C.C.Errors[I])); }
		AddInfo(FString::Printf(TEXT("snapshot frame %u: max position error %.2f (racer %d), per racer:%s; human %.2f, computer max %.2f; stream B %s (port - file calls %lld)"),
			C.Frame, C.C.MaxError, C.C.WorstRacer, *Per, C.C.Errors.IsValidIndex(Race->Human) ? C.C.Errors[Race->Human] : -1.0, MaxAi,
			C.C.bStreamBEqual ? TEXT("equal") : TEXT("differs"), C.C.StreamBCalls));
		AddInfo(FString::Printf(TEXT("  race state: port frame %d phase %d clock %.3f; file cAI frame %d phase %d / %d, +0x14 %.3f"), C.PortFrame, C.PortPhase, C.PortClock,
			C.File.AiFrame, C.File.AiPhase, C.File.AiOldPhase, C.File.AiValue14));
		if (C.Frame != 0 && C.Port.IsValidIndex(Race->Human))
		{
			const int32 FH = C.Port.Num() - 1 - Race->Human;
			AddInfo(FString::Printf(TEXT("  human port %s file %s (file motion %d control %d)"), *C.Port[Race->Human].ToString(),
				C.File.Positions.IsValidIndex(FH) ? *C.File.Positions[FH].ToString() : TEXT("-"), C.File.Motions.IsValidIndex(FH) ? C.File.Motions[FH] : -1,
				C.File.Controls.IsValidIndex(FH) ? C.File.Controls[FH] : -1));
		}
		if (C.Frame == 0)
		{
			for (int32 I = 0; I < C.Port.Num(); ++I)
			{
				AddInfo(FString::Printf(TEXT("  frame 0 racer %d port %s file %s"), I, *C.Port[I].ToString(), C.File.Positions.IsValidIndex(C.Port.Num() - 1 - I) ? *C.File.Positions[C.Port.Num() - 1 - I].ToString() : TEXT("-")));
			}
			for (int32 I = 0; I < C.States.Num(); ++I)
			{
				const int32 Rec = C.File.RecordAt.IsValidIndex(C.States.Num() - 1 - I) ? C.File.RecordAt[C.States.Num() - 1 - I] : -1;
				if (Rec < 0 || Rec + 0x450 > C.Raw.Num()) continue;
				TSet<int32> Mapped;
				const TArray<FString> Diffs = CompareBoarderRecord(C.Raw.GetData() + Rec, C.States[I], Mapped);
				AddInfo(FString::Printf(TEXT("  keyframe 0 racer %d%s: %d mismatching fields"), I, I == Race->Human ? TEXT(" (human)") : TEXT(""), Diffs.Num()));
				for (const FString& Df : Diffs) AddInfo(FString::Printf(TEXT("    racer %d %s"), I, *Df));
				if (I == Race->Human)
				{
					FString Other;
					for (int32 Off = 0x18; Off < 0x460; Off += 4)
					{
						if (Mapped.Contains(Off)) continue;
						uint32 U; FMemory::Memcpy(&U, C.Raw.GetData() + Rec + Off - 0x10, 4);
						if (U == 0 || U == 0xDEADC0EDu) continue;
						float Fl; FMemory::Memcpy(&Fl, &U, 4);
						Other += FString::Printf(TEXT(" %X=%08x(%g)"), Off, U, Fl);
					}
					AddInfo(TEXT("    human unmapped words:") + Other);
				}
			}
		}
	}
	{
		int64 PrevFile = 0, PrevPort = 0;
		uint32 PrevFrame = 0;
		for (const FFileRace::FCheck& C : Race->Checks)
		{
			const int64 File = int64(C.File.StreamB[5] - FGameRng(S.Seed2B00).S[5]);
			const int64 Port = File + C.C.StreamBCalls;
			if (C.Frame != 0)
			{
				AddInfo(FString::Printf(TEXT("stream B draws frames %u..%u: port %lld, file %lld"), PrevFrame, C.Frame, Port - PrevPort, File - PrevFile));
			}
			PrevFile = File; PrevPort = Port; PrevFrame = C.Frame;
		}
	}
	for (const FString& T : Race->Trace) { AddInfo(TEXT("  trace human ") + T); }
	for (const FString& T : Race->WordTrace) { AddInfo(TEXT("  word human ") + T); }
	AddInfo(Race->FirstLayoutMismatch.IsEmpty() ? FString(TEXT("every played word fits the port's layout"))
		: TEXT("first word that does not fit the port's layout: ") + Race->FirstLayoutMismatch);
	{
		int32 RunFrames = 0;
		for (int32 K = 0; K < R.GetInput(0).NumRuns(); ++K) { RunFrames += FMath::Max(1, int32(R.GetInput(0).Runs[K])); }
		AddInfo(FString::Printf(TEXT("input recorder: %d runs = %d frames (recorded length %u)"), R.GetInput(0).NumRuns(), RunFrames, R.GetRecordedLength()));
	}
	TestEqual(TEXT("a check per keyframe"), Race->Checks.Num(), R.KeyframeFrames().Num());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayLiveTraceTest, "SSX.Replay.LiveTrace.Racers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayLiveTraceTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	const FString RefPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("replay_rep0001.live.csv"));
	FString ReplayPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ORIGINAL_REPLAY"));
	if (ReplayPath.IsEmpty())
	{
		ReplayPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MemoryCard"), TEXT("BASLUS-20326-REP0001"), TEXT("BASLUS-20326-REP0001"));
	}
	TArray<FString> Lines;
	TArray<uint8> In;
	if (!FFileHelper::LoadFileToStringArray(Lines, *RefPath) || !FFileHelper::LoadFileToArray(In, *ReplayPath) || !Validate(In))
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: SimData/replay_rep0001.live.csv (tools/make_replay_live_reference.py) or the REP0001 replay file missing"));
		return true;
	}
	struct FRow { int32 Racer, Frame, Motion, Control; FVector P, V; };
	TArray<FRow> Rows;
	int32 LastFrame = 0;
	for (int32 I = 1; I < Lines.Num(); ++I)
	{
		TArray<FString> C;
		Lines[I].ParseIntoArray(C, TEXT(","));
		if (C.Num() < 10) continue;
		FRow& Row = Rows.AddDefaulted_GetRef();
		Row.Racer = FCString::Atoi(*C[0]); Row.Frame = FCString::Atoi(*C[1]);
		Row.P = FVector(FCString::Atod(*C[2]), FCString::Atod(*C[3]), FCString::Atod(*C[4]));
		Row.V = FVector(FCString::Atod(*C[5]), FCString::Atod(*C[6]), FCString::Atod(*C[7]));
		Row.Motion = FCString::Atoi(*C[8]); Row.Control = FCString::Atoi(*C[9]);
		LastFrame = FMath::Max(LastFrame, Row.Frame);
	}
	FSetup S;
	if (!TestTrue(TEXT("setup read"), ReadSetup(In, S))) return false;
	TUniquePtr<FFileRace> Race = MakeUnique<FFileRace>();
	FString Err;
	if (!Race->Build(S, Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: race / sim data not available (%s)"), *Err));
		return true;
	}
	if (!TestTrue(TEXT("replay read into the race"), ReadReplay(In, Race->Replay))) return false;
	Race->Replay.BeginRebuild();
	TMap<int64, FRiderState> Port;
	for (int32 F = 0; F < LastFrame + 600 && Race->Replay.IsRebuilding() && int32(Race->Replay.GetRecordFrame()) <= LastFrame; ++F)
	{
		Race->Frame();
		for (int32 I = 0; I < Race->Sims.Num(); ++I) { Port.Add((int64(I) << 32) | Race->Replay.GetRecordFrame(), Race->Sims[I]->State); }
	}
	TMap<int32, int32> Samples, VelOut, StateOut;
	TMap<int32, double> MaxP, MaxV;
	for (const FRow& Row : Rows)
	{
		const FRiderState* Best = nullptr;
		double BestE = 1e30;
		for (const int32 T : { Row.Frame, Row.Frame - 1 })
		{
			if (const FRiderState* St = Port.Find((int64(Row.Racer) << 32) | uint32(T)))
			{
				const double E = FVector::Dist(St->Position, Row.P) + FVector::Dist(St->Velocity, Row.V) / 60.0;
				if (E < BestE) { BestE = E; Best = St; }
			}
		}
		if (!TestNotNull(*FString::Printf(TEXT("racer %d frame %d re-run"), Row.Racer, Row.Frame), Best)) continue;
		const double BestP = FVector::Dist(Best->Position, Row.P);
		const double EV = FVector::Dist(Best->Velocity, Row.V);
		Samples.FindOrAdd(Row.Racer)++;
		MaxP.FindOrAdd(Row.Racer) = FMath::Max(MaxP.FindOrAdd(Row.Racer), BestP);
		MaxV.FindOrAdd(Row.Racer) = FMath::Max(MaxV.FindOrAdd(Row.Racer), EV);
		if (BestP > 0.5) { AddError(FString::Printf(TEXT("racer %d frame %d: position %s vs original %s (%.3f cm)"), Row.Racer, Row.Frame, *Best->Position.ToString(), *Row.P.ToString(), BestP)); }
		if (EV > 1.0) { VelOut.FindOrAdd(Row.Racer)++; AddInfo(FString::Printf(TEXT("racer %d frame %d: velocity off by %.3f cm/s"), Row.Racer, Row.Frame, EV)); }
		if (int32(Best->Motion) != Row.Motion || int32(Best->Control) != Row.Control)
		{
			StateOut.FindOrAdd(Row.Racer)++;
			AddInfo(FString::Printf(TEXT("racer %d frame %d: motion / control %d / %d vs original %d / %d"), Row.Racer, Row.Frame, int32(Best->Motion), int32(Best->Control), Row.Motion, Row.Control));
		}
	}
	for (const TPair<int32, int32>& P : Samples)
	{
		const int32 Allowed = FMath::Max(1, P.Value / 50);
		AddInfo(FString::Printf(TEXT("racer %d: %d live samples, max position error %.3f cm, max velocity error %.3f cm/s, velocity outliers %d, state outliers %d"),
			P.Key, P.Value, MaxP.FindRef(P.Key), MaxV.FindRef(P.Key), VelOut.FindRef(P.Key), StateOut.FindRef(P.Key)));
		TestTrue(FString::Printf(TEXT("racer %d: velocity outliers <= %d"), P.Key, Allowed), VelOut.FindRef(P.Key) <= Allowed);
		TestTrue(FString::Printf(TEXT("racer %d: motion / control outliers <= %d"), P.Key, Allowed), StateOut.FindRef(P.Key) <= Allowed);
	}
	TestTrue(TEXT("racer 4 (the human) sampled"), Samples.Contains(4));
	return !HasAnyErrors();
}

// 0x11A840, 0x15EE30, 0x160AF0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXReplayLiveTraceGateAnimTest, "SSX.Replay.LiveTrace.GateAnim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXReplayLiveTraceGateAnimTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	const FString RefPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("replay_rep0001.anim.csv"));
	FString ReplayPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ORIGINAL_REPLAY"));
	if (ReplayPath.IsEmpty())
	{
		ReplayPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MemoryCard"), TEXT("BASLUS-20326-REP0001"), TEXT("BASLUS-20326-REP0001"));
	}
	TArray<FString> Lines;
	TArray<uint8> In;
	if (!FFileHelper::LoadFileToStringArray(Lines, *RefPath) || !FFileHelper::LoadFileToArray(In, *ReplayPath) || !Validate(In))
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: SimData/replay_rep0001.anim.csv (tools/make_replay_live_reference.py) or the REP0001 replay file missing"));
		return true;
	}
	FSetup S;
	if (!TestTrue(TEXT("setup read"), ReadSetup(In, S))) return false;
	TUniquePtr<FFileRace> Race = MakeUnique<FFileRace>();
	FString Err;
	if (!Race->Build(S, Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: race / sim data not available (%s)"), *Err));
		return true;
	}
	if (!TestTrue(TEXT("replay read into the race"), ReadReplay(In, Race->Replay))) return false;
	struct FPortAnim { int32 Control = -1; double Gate = 0.0; int32 Ch0 = SSXAnim::NoAnim, Ch1 = SSXAnim::NoAnim, Ch2 = SSXAnim::NoAnim, NumInst = 0, NumSubs = 0; double Frame = -1.0, Weight = 0.0, Last = 0.0; bool bMirror = false; double Yaw = 0.0; SSXAnim::FRowMat Board, Root, Hips; bool bPose = false; };
	TMap<int64, FPortAnim> Port;
	TOptional<SSXAnim::FRiderAnimDriver> Saved;
	Race->Replay.BeginRebuild();
	for (int32 F = 0; F < 2000 && Race->Replay.IsRebuilding() && Race->Replay.GetRecordFrame() <= 245u; ++F)
	{
		Race->Frame();
		for (int32 I = 0; I < Race->Sims.Num(); ++I)
		{
			if (!Race->Poses.IsValidIndex(I) || !Race->Poses[I]) continue;
			const SSXAnim::FAnimController& Ctl = Race->Poses[I]->GetController();
			FPortAnim A;
			A.Control = int32(Race->Sims[I]->State.Control);
			A.Gate = Race->Sims[I]->State.GateLaunch.Cur;
			A.Ch0 = Ctl.CurrentId(0);
			A.Ch1 = Ctl.CurrentId(1);
			A.Ch2 = Ctl.CurrentId(2);
			A.NumInst = Ctl.Instances(0).Num();
			A.bMirror = Ctl.bMirror;
			A.Yaw = Ctl.Yaw;
			if (const SSXAnim::FPlayInstance* P = Ctl.Newest(0))
			{
				A.NumSubs = P->NumSubs;
				A.Frame = P->Subs[0].Frame;
				A.Weight = P->Weight;
				const SSXAnim::FAflClip* C = Ctl.GetLibrary() ? Ctl.GetLibrary()->Clip(P->Subs[0].Clip) : nullptr;
				A.Last = C ? double(C->NFrames - 1) : 0.0;
			}
			// 0x129628
			SSXAnim::FRiderPalette Pal;
			Race->Poses[I]->ComputePalette(*Race->Sims[I], Pal);
			if (Pal.bValid && Pal.Body.Num() > 0)
			{
				TArray<SSXAnim::FPoseLayer> Layers;
				TArray<SSXAnim::FBonePose> Body;
				Ctl.GatherLayers(0, Layers);
				const double Scale = Race->Sims[I]->Profile.SizeScale;
				SSXAnim::BlendLayers(*Ctl.GetLibrary(), *Race->Poses[I]->GetSkeleton(), Layers, Scale, Scale, 0, Body);
				A.bPose = true;
				A.Board = Pal.Board;
				A.Hips = Pal.Body[0];
				A.Root = SSXAnim::RootFrame(SSXAnim::FRiderAnimDriver::FrameInputs(Race->Sims[I]->State, Scale), Body.Num() ? Body[0].Trans.Z : 0.0, true);
				A.Root.SetRow(3, Pal.Body[0].Row(3), 1.0f);
			}
			Port.Add((int64(I) << 32) | Race->Replay.GetRecordFrame(), A);
			if (I == Race->Human && Race->Replay.GetRecordFrame() == 0u)
			{
				Saved = *Race->Poses[I];
			}
		}
	}
	if (TestTrue(TEXT("driver saved at the replay's frame 0"), Saved.IsSet()) && Race->Poses.IsValidIndex(Race->Human) && Race->Poses[Race->Human])
	{
		*Race->Poses[Race->Human] = Saved.GetValue();
		const SSXAnim::FAnimController& Ctl = Race->Poses[Race->Human]->GetController();
		const FPortAnim* A0 = Port.Find((int64(Race->Human) << 32) | 0u);
		const SSXAnim::FPlayInstance* P = Ctl.Newest(0);
		TestTrue(TEXT("restored keyframe 0: one 0x1FC instance at weight 1, the frame of frame 0"), A0 && P && Ctl.CurrentId(0) == 0x1FC
			&& Ctl.Instances(0).Num() == 1 && P->Weight == 1.0 && P->Subs[0].Frame == A0->Frame && Ctl.CurrentId(1) == SSXAnim::NoAnim);
	}
	TMap<int32, int32> Samples, IdOut, PoseOut, Ch2Out, MatOut, BaseOut, Torn;
	TSet<int32> Reported;
	for (int32 L = 1; L < Lines.Num(); ++L)
	{
		TArray<FString> C;
		Lines[L].ParseIntoArray(C, TEXT(","));
		if (C.Num() < 10) continue;
		const int32 Racer = FCString::Atoi(*C[0]), Frame = FCString::Atoi(*C[1]), Control = FCString::Atoi(*C[3]);
		const double Gate = FCString::Atod(*C[4]);
		const int32 Ch0 = FCString::Atoi(*C[5]), Ch1 = FCString::Atoi(*C[6]), Ch2 = FCString::Atoi(*C[7]);
		const bool bMirror = FCString::Atoi(*C[8]) != 0;
		const double Yaw = FCString::Atod(*C[9]);
		const bool bGate = Control == int32(ERiderControl::GateAnticipate) || Control == int32(ERiderControl::GateLaunch);
		if (!bGate && Control != int32(ERiderControl::GateToBase)) continue;
		const FPortAnim* Best = nullptr;
		double BestE = 1e30;
		const FVector BoardO = C.Num() >= 40 ? FVector(FCString::Atod(*C[19]), FCString::Atod(*C[20]), FCString::Atod(*C[21])) : FVector::ZeroVector;
		for (const int32 T : { Frame, Frame - 1 })
		{
			const FPortAnim* A = Port.Find((int64(Racer) << 32) | uint32(T));
			if (!A) continue;
			const double E = (A->Ch0 == Ch0 && A->Control == Control && (!bGate || FMath::Abs(A->Gate - Gate) < 1e-5) ? 0.0 : 1e6)
				+ (A->bPose && C.Num() >= 40 ? FVector::Dist(FVector(A->Board.Row(3)), BoardO) : 0.0);
			if (E < BestE) { BestE = E; Best = A; }
		}
		if (!TestNotNull(*FString::Printf(TEXT("racer %d frame %d re-run"), Racer, Frame), Best)) continue;
		Samples.FindOrAdd(Racer)++;
		const bool bId = Best->Ch0 == Ch0 && Best->Ch1 == Ch1 && Best->bMirror == bMirror && FMath::Abs(FMath::Abs(Best->Yaw) - FMath::Abs(Yaw)) < 1e-4;
		const double Want = Gate * Best->Last;
		const bool bPose = !bGate || (FMath::Abs(Best->Gate - Gate) < 1e-5 && Best->NumInst == 1 && Best->NumSubs == 1 && Best->Weight == 1.0
			&& FMath::Abs(Best->Frame - Want) < 1e-3);
		// 0x114730, 0x117180
		bool bMats = true;
		double V[30];
		for (int32 K = 0; K < 30 && C.Num() >= 40; ++K) { V[K] = FCString::Atod(*C[10 + K]); }
		const bool bTornPose = C.Num() >= 40 && V[23] - V[11] < 30.0;
		if (bTornPose) { Torn.FindOrAdd(Racer)++; }
		if (C.Num() >= 40 && !bTornPose)
		{
			double BestM = 1e30, DRot = 0.0, DPos = 0.0, DRoot = 0.0, DRootPos = 0.0, DHips = 0.0;
			for (const int32 T : { Frame, Frame - 1 })
			{
				const FPortAnim* A = Port.Find((int64(Racer) << 32) | uint32(T));
				if (!A || !A->bPose) continue;
				double R[5] = { 0, 0, 0, 0, 0 };
				for (int32 Rw = 0; Rw < 4; ++Rw)
				{
					for (int32 J = 0; J < 3; ++J)
					{
						const double Db = FMath::Abs(A->Board.M[Rw][J] - V[Rw * 3 + J]);
						const double Dr = FMath::Abs(A->Root.M[Rw][J] - V[12 + Rw * 3 + J]);
						if (Rw < 3) { R[0] = FMath::Max(R[0], Db); R[2] = FMath::Max(R[2], Dr); } else { R[1] = FMath::Max(R[1], Db); R[3] = FMath::Max(R[3], Dr); }
						if (Rw < 2) { R[4] = FMath::Max(R[4], FMath::Abs(A->Hips.M[Rw][J] - V[24 + Rw * 3 + J])); }
					}
				}
				const double M = R[0] * 100.0 + R[1] + R[2] * 100.0 + R[3] + R[4] * 100.0;
				if (M < BestM) { BestM = M; DRot = R[0]; DPos = R[1]; DRoot = R[2]; DRootPos = R[3]; DHips = R[4]; }
			}
			bMats = BestM < 1e29 && DRot < 2e-3 && DPos < 0.05 && DRoot < 2e-3 && DRootPos < 0.05 && DHips < 2e-3;
			TMap<int32, int32>& Out = bGate ? MatOut : BaseOut;
			if (!bMats && Out.FindRef(Racer) < 2)
			{
				AddInfo(FString::Printf(TEXT("racer %d frame %d control %d gate %.3f: rendered pose off: board rows %.4f origin %.3f, root rows %.4f hips origin %.3f, hips rows %.4f"),
					Racer, Frame, Control, Gate, DRot, DPos, DRoot, DRootPos, DHips));
			}
			if (!bMats) Out.FindOrAdd(Racer)++;
		}
		if (!bId) IdOut.FindOrAdd(Racer)++;
		if (!bPose) PoseOut.FindOrAdd(Racer)++;
		if (Best->Ch2 != Ch2) Ch2Out.FindOrAdd(Racer)++;
		if ((!bId || !bPose) && !Reported.Contains(Racer))
		{
			Reported.Add(Racer);
			AddInfo(FString::Printf(TEXT("racer %d frame %d first mismatch: original control %d ch0 0x%X gate %.5f (frame %.3f) mirror %d yaw %.3f; port control %d ch0 0x%X gate %.5f, %d instance(s), %d sub(s), frame %.3f of %.0f, weight %.3f, mirror %d yaw %.3f"),
				Racer, Frame, Control, Ch0, Gate, Want, int32(bMirror), Yaw, Best->Control, Best->Ch0, Best->Gate, Best->NumInst, Best->NumSubs, Best->Frame, Best->Last, Best->Weight, int32(Best->bMirror), Best->Yaw));
		}
	}
	for (const TPair<int32, int32>& P : Samples)
	{
		const int32 Allowed = FMath::Max(1, P.Value / 50);   // torn samples (as SSX.Replay.LiveTrace)
		AddInfo(FString::Printf(TEXT("racer %d: %d gate samples, id / stance outliers %d, gate pose outliers %d, rendered pose outliers in the gate %d, in Gate2Base %d (%d torn samples skipped), channel-2 idle differences %d"),
			P.Key, P.Value, IdOut.FindRef(P.Key), PoseOut.FindRef(P.Key), MatOut.FindRef(P.Key), BaseOut.FindRef(P.Key), Torn.FindRef(P.Key), Ch2Out.FindRef(P.Key)));
		TestTrue(FString::Printf(TEXT("racer %d: channel-0 / channel-1 id and stance outliers <= %d"), P.Key, Allowed), IdOut.FindRef(P.Key) <= Allowed);
		TestTrue(FString::Printf(TEXT("racer %d: gate pose (0x1FC frame = +0x238 (n - 1)) outliers <= %d"), P.Key, Allowed), PoseOut.FindRef(P.Key) <= Allowed);
		// TODO(anim): Gate2Base hips origin off by up to ~1.6 (reported, not asserted)
		TestTrue(FString::Printf(TEXT("racer %d: rendered pose in the gate (board, root, hips) outliers <= %d"), P.Key, Allowed), MatOut.FindRef(P.Key) <= Allowed);
	}
	TestTrue(TEXT("racer 4 (the human) sampled"), Samples.Contains(4));
	return !HasAnyErrors();
}

#if WITH_EDITORONLY_DATA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRenderReplayBoardClearanceTest, "SSX.Render.ReplayBoardClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXRenderReplayBoardClearanceTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	const FString RefPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("replay_rep0001.board.csv"));
	FString ReplayPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ORIGINAL_REPLAY"));
	if (ReplayPath.IsEmpty())
	{
		ReplayPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MemoryCard"), TEXT("BASLUS-20326-REP0001"), TEXT("BASLUS-20326-REP0001"));
	}
	TArray<FString> Lines;
	TArray<uint8> In;
	if (!FFileHelper::LoadFileToStringArray(Lines, *RefPath) || !FFileHelper::LoadFileToArray(In, *ReplayPath) || !Validate(In))
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: SimData/replay_rep0001.board.csv (tools/make_board_clearance_reference.py) or the REP0001 replay file missing"));
		return true;
	}
	struct FRow { int32 Frame, Surface; FVector P; double Roll, Low; };
	TArray<FRow> Rows;
	int32 LastFrame = 0;
	for (int32 I = 1; I < Lines.Num(); ++I)
	{
		TArray<FString> C;
		Lines[I].ParseIntoArray(C, TEXT(","));
		if (C.Num() < 8) continue;
		const FRow Row{ FCString::Atoi(*C[0]), FCString::Atoi(*C[1]), FVector(FCString::Atod(*C[2]), FCString::Atod(*C[3]), FCString::Atod(*C[4])),
			FCString::Atod(*C[5]), FCString::Atod(*C[6]) };
		if (Row.Frame > 2000) continue;
		Rows.Add(Row);
		LastFrame = FMath::Max(LastFrame, Row.Frame);
	}
	FSetup S;
	if (!TestTrue(TEXT("setup read"), ReadSetup(In, S))) return false;
	TUniquePtr<FFileRace> Race = MakeUnique<FFileRace>();
	FString Err;
	if (!Race->Build(S, Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: race / sim data not available (%s)"), *Err));
		return true;
	}
	if (!TestTrue(TEXT("replay read into the race"), ReadReplay(In, Race->Replay))) return false;
	const int32 Racer = 4;
	if (!TestTrue(TEXT("racer 4 has a pose"), Race->Sims.IsValidIndex(Racer) && Race->Poses.IsValidIndex(Racer) && Race->Poses[Racer].IsValid())) return false;
	const int32 Class = FMath::Clamp(Race->Sims[Racer]->Profile.BoardClass, 0, 2);
	bool bGoofyMesh = false;   // 0x201138
	UStaticMesh* Mesh = USSXCharacterAppearance::LoadDrawnBoardMesh(TEXT("brodi"), Class, Race->Sims[Racer]->Profile.Stance != 0, bGoofyMesh);
	const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
	if (!MD)
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: board meshes not imported"));
		return true;
	}
	TArray<FVector> Verts;
	{
		FStaticMeshConstAttributes Attr(*MD);
		TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		for (const FVertexID Id : MD->Vertices().GetElementIDs()) { Verts.Add(FVector(Pos[Id])); }
	}
	struct FPort { FVector P = FVector::ZeroVector; double Low = 0.0, Roll = 0.0; bool bValid = false; };
	TMap<int32, FPort> Port;
	Race->Replay.BeginRebuild();
	for (int32 F = 0; F < LastFrame + 600 && Race->Replay.IsRebuilding() && int32(Race->Replay.GetRecordFrame()) <= LastFrame; ++F)
	{
		Race->Frame();
		const FRiderSim& Sim = *Race->Sims[Racer];
		FPort Pt;
		Pt.P = Sim.State.Position;
		SSXAnim::FRiderPalette Pal;
		Race->Poses[Racer]->ComputePalette(Sim, Pal);
		FMatrix MeshToGame;
		const FVector N = Sim.State.GroundNormal.GetSafeNormal();
		if (Pal.bValid && Race->Poses[Racer]->GetLibrary() && !N.IsNearlyZero()
			&& USSXCharacterAppearance::BoardMeshToGame(*Race->Poses[Racer]->GetLibrary(), Class, Pal.Board, Sim.Profile.SizeScale, MeshToGame, Sim.Profile.Stance != 0,
				bGoofyMesh))
		{
			const FVector Hit = Sim.State.Position - Sim.State.GroundHeight * N;
			Pt.Low = 1e9;
			for (const FVector& V : Verts) { Pt.Low = FMath::Min(Pt.Low, FVector::DotProduct(MeshToGame.TransformPosition(V) - Hit, N)); }
			const FVector Up(Pal.Board.M[1][0], Pal.Board.M[1][1], Pal.Board.M[1][2]);
			const FVector Across(Pal.Board.M[2][0], Pal.Board.M[2][1], Pal.Board.M[2][2]);
			Pt.Roll = FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(N, Across), FVector::DotProduct(N, Up))));
			Pt.bValid = true;
		}
		Port.Add(int32(Race->Replay.GetRecordFrame()), Pt);
	}
	TArray<double> Diff, CarveOrig, CarvePort, StraightPort;
	int32 Diverged = 0, Out = 0;
	double MaxRoll = 0.0;
	for (const FRow& Row : Rows)
	{
		const FPort* Best = nullptr;
		int32 BestT = 0;
		for (const int32 T : { Row.Frame, Row.Frame - 1 })
		{
			const FPort* Pt = Port.Find(T);
			if (Pt && Pt->bValid && (!Best || FVector::Dist(Pt->P, Row.P) < FVector::Dist(Best->P, Row.P))) { Best = Pt; BestT = T; }
		}
		if (!Best || FVector::Dist(Best->P, Row.P) > 1.0)
		{
			++Diverged;
			continue;
		}
		for (const int32 T : { BestT - 1, BestT + 1 })
		{
			const FPort* Pt = Port.Find(T);
			if (Pt && Pt->bValid && FMath::Abs(Pt->Roll - Row.Roll) < FMath::Abs(Best->Roll - Row.Roll) - 0.05) { Best = Pt; }
		}
		const double D = Best->Low - Row.Low;
		Diff.Add(FMath::Abs(D));
		if (FMath::Abs(D) > 3.0)
		{
			++Out;
			AddInfo(FString::Printf(TEXT("frame %d: lowest board vertex %.2f vs original %.2f cm (|roll| %.1f vs %.1f deg)"), Row.Frame, Best->Low, Row.Low, Best->Roll, Row.Roll));
		}
		if (Row.Frame >= 722 && Row.Frame <= 830 && Row.Roll > 40.0)
		{
			CarveOrig.Add(Row.Low);
			CarvePort.Add(Best->Low);
			MaxRoll = FMath::Max(MaxRoll, Row.Roll);
		}
		if (Row.Roll < 3.0 && Row.Surface == 1)
		{
			StraightPort.Add(Best->Low);
		}
	}
	auto Median = [](TArray<double> A) { if (A.Num() == 0) { return 0.0; } A.Sort(); return A[A.Num() / 2]; };
	auto P90 = [](TArray<double> A) { if (A.Num() == 0) { return 0.0; } A.Sort(); return A[(A.Num() - 1) * 9 / 10]; };
	AddInfo(FString::Printf(TEXT("racer 4: %d ground samples compared (%d diverged / not re-run), |lowest vertex port - original| p50 %.3f p90 %.3f cm, %d over 3 cm; carve f722-830 (|roll| > 40, max %.1f deg) %d samples: original p50 %.2f, port p50 %.2f cm; straight (|roll| < 3) port p50 %.2f cm"),
		Diff.Num(), Diverged, Median(Diff), P90(Diff), Out, MaxRoll, CarveOrig.Num(), Median(CarveOrig), Median(CarvePort), Median(StraightPort)));
	TestTrue(TEXT("at least 200 synced ground samples"), Diff.Num() >= 200);
	TestTrue(FString::Printf(TEXT("lowest board vertex vs the original: |difference| p50 %.3f < 0.25 cm and p90 %.3f < 3 cm"), Median(Diff), P90(Diff)), Median(Diff) < 0.25 && P90(Diff) < 3.0);
	TestTrue(FString::Printf(TEXT("carve f722-830: port median %.2f within 0.5 cm of the original %.2f"), Median(CarvePort), Median(CarveOrig)), FMath::Abs(Median(CarvePort) - Median(CarveOrig)) < 0.5);
	TestTrue(TEXT("the first hard carve (f722-830) is sampled"), CarveOrig.Num() >= 20);
	TestTrue(FString::Printf(TEXT("in the hard carve the inside edge is under the snow in both (original %.2f, port %.2f cm)"), Median(CarveOrig), Median(CarvePort)),
		Median(CarveOrig) < -12.0 && Median(CarvePort) < -12.0);
	TestTrue(FString::Printf(TEXT("riding straight the Fr board clears the snow (port p50 %.2f cm)"), Median(StraightPort)), StraightPort.Num() >= 20 && Median(StraightPort) > 0.0);
	return !HasAnyErrors();
}
#endif

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRailLiveOneStepTest, "SSX.Rail.LiveOneStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXRailLiveOneStepTest::RunTest(const FString& Parameters)
{
	using namespace SSXReplayFile;
	const FString RefPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("replay_rep0001.onestep.bin"));
	FString ReplayPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ORIGINAL_REPLAY"));
	if (ReplayPath.IsEmpty())
	{
		ReplayPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MemoryCard"), TEXT("BASLUS-20326-REP0001"), TEXT("BASLUS-20326-REP0001"));
	}
	TArray<uint8> Ref, In;
	if (!FFileHelper::LoadFileToArray(Ref, *RefPath) || Ref.Num() < 8 || FMemory::Memcmp(Ref.GetData(), "RLV1", 4) != 0
		|| !FFileHelper::LoadFileToArray(In, *ReplayPath) || !Validate(In))
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: SimData/replay_rep0001.onestep.bin (tools/make_onestep_live_reference.py) or the REP0001 replay file missing"));
		return true;
	}
	constexpr int32 RecSize = 0xB50, SetSize = 0x270, EntrySize = 16 + RecSize + SetSize;
	int32 Count = 0;
	FMemory::Memcpy(&Count, Ref.GetData() + 4, 4);
	if (!TestEqual(TEXT("reference size"), Ref.Num(), 8 + Count * EntrySize)) return false;
	struct FEntry { uint32 Run, Frame, Racer, Flags; const uint8* Rec; const uint8* Set; };
	TArray<FEntry> Entries;
	for (int32 K = 0; K < Count; ++K)
	{
		const uint8* E = Ref.GetData() + 8 + K * EntrySize;
		FEntry& X = Entries.AddDefaulted_GetRef();
		FMemory::Memcpy(&X.Run, E, 16);
		X.Rec = E + 16;
		X.Set = E + 16 + RecSize;
	}
	FSetup S;
	if (!TestTrue(TEXT("setup read"), ReadSetup(In, S))) return false;

	struct FField { const TCHAR* Name; int32 Off; int32 N; };
	static const FField Fields[] = { { TEXT("p"), 0x140, 3 }, { TEXT("v"), 0x150, 3 }, { TEXT("q"), 0x170, 4 }, { TEXT("rail point"), 0x2D0, 3 },
		{ TEXT("rail up"), 0x2A0, 3 }, { TEXT("steer"), 0x214, 1 }, { TEXT("box filter"), 0x284, 1 } };
	auto PortFloats = [](const FRiderState& St, int32 Off, float* Out)
	{
		const FVector* V = nullptr;
		switch (Off)
		{
		case 0x140: V = &St.Position; break;
		case 0x150: V = &St.Velocity; break;
		case 0x2D0: V = &St.ContactPoint; break;
		case 0x2A0: V = &St.GroundNormal; break;
		case 0x170: Out[0] = float(St.Orientation.W); Out[1] = float(St.Orientation.X); Out[2] = float(St.Orientation.Y); Out[3] = float(St.Orientation.Z); return;
		case 0x214: Out[0] = float(St.Steer.Cur); return;
		case 0x284: Out[0] = float(St.RailBox.Cur); return;
		default: return;
		}
		Out[0] = float(V->X); Out[1] = float(V->Y); Out[2] = float(V->Z);
	};
	constexpr int32 NumFields = UE_ARRAY_COUNT(Fields);
	int32 Exact[NumFields] = {}, Total = 0, StateOk = 0, AllExact = 0;
	double MaxErr[NumFields] = {};
	TMap<FString, int32> KindTotal, KindExact;
	int32 Reported = 0, Transitions = 0, NoBoard = 0, ControlLag = 0, NoSet = 0;
	int32 LineSamples = 0, LineEqual = 0;
	TArray<FString> DumpRows;
	const bool bDumpRows = !FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_RAIL_DUMP")).IsEmpty();
	for (uint32 Run = 0; Run < 2; ++Run)
	{
		TMap<int64, const FEntry*> At;
		uint32 Last = 0;
		for (const FEntry& E : Entries)
		{
			if (E.Run != Run) continue;
			At.Add((int64(E.Racer) << 32) | E.Frame, &E);
			Last = FMath::Max(Last, E.Frame);
		}
		if (At.IsEmpty()) continue;
		TUniquePtr<FFileRace> Race = MakeUnique<FFileRace>();
		FString Err;
		if (!Race->Build(S, Err))
		{
			SSXTest::Skip(*this, FString::Printf(TEXT("SKIPPED: race / sim data not available (%s)"), *Err));
			return true;
		}
		if (!TestTrue(TEXT("replay read into the race"), ReadReplay(In, Race->Replay))) return false;
		for (const TPair<int64, const FEntry*>& P : At)
		{
			if (!(P.Value->Flags & 16u)) { Race->ForceSet.Add(P.Key, P.Value->Set); }
			if (!(P.Value->Flags & 1u)) continue;
			Race->Force.Add(P.Key, P.Value->Rec);
			if (P.Value->Flags & 8u) { Race->ForceControlAt.Add(P.Key); }
			const FEntry* const* Next = At.Find(P.Key + 1);
			if (Next && ((*Next)->Flags & 6u) == 6u) { Race->ForceBoard.Add(P.Key, (*Next)->Rec); }
			else if (!(P.Value->Flags & 2u)) { Race->ForceBoard.Add(P.Key, P.Value->Rec); }

		}
		TArray<int32> LastState;
		LastState.Init(-1, Race->Sims.Num());
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ONESTEP_NOBOARD")).IsEmpty()) { for (TPair<int64, const uint8*>& B : Race->ForceBoard) { B.Value = nullptr; } }
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_ONESTEP_NOSET")).IsEmpty()) { Race->ForceSet.Reset(); }
		Race->OnFrameEnd = [&]()
		{
			const uint32 F = Race->Replay.GetRecordFrame();
			for (int32 I = 0; I < Race->Sims.Num(); ++I)
			{
				const int32 Sx = int32(Race->Sims[I]->State.Motion) * 100 + int32(Race->Sims[I]->State.Control);
				if (Sx != LastState[I] && At.Contains((int64(I) << 32) | F) && Transitions < 60)
				{
					++Transitions;
					AddInfo(FString::Printf(TEXT("run %u racer %d f%u: port motion / control %d / %d (forced before: %s)"), Run, I, F, Sx / 100, Sx % 100,
						Race->Force.Contains((int64(I) << 32) | (F - 1)) ? TEXT("yes") : TEXT("no")));
				}
				LastState[I] = Sx;
				if (bDumpRows && At.Contains((int64(I) << 32) | F))
				{
					const FRacerRecord& Rc = Race->Session.GetRacer(I);
					DumpRows.Add(FString::Printf(TEXT("D,%u,%d,%d,%d,%d"), F, I, Rc.RiderContacts, Rc.RiderImpulses, Race->Force.Contains((int64(I) << 32) | (F - 1)) ? 1 : 0));
				}
				const FEntry* const* Cur = At.Find((int64(I) << 32) | F);
				const FEntry* const* Prev = At.Find((int64(I) << 32) | (F - 1));
				if (Cur && ((*Cur)->Flags & 4u))
				{
					float ToGo;
					FMemory::Memcpy(&ToGo, (*Cur)->Rec + 0x374, 4);
					++LineSamples;
					LineEqual += FMath::Abs(Race->Session.GetRacer(I).Line.DistToGo - double(ToGo)) < 1.0;
				}
				if (!Cur || !Prev || !((*Cur)->Flags & 4u) || !((*Prev)->Flags & 1u)) continue;
				if (!Race->ForceBoard.Contains((int64(I) << 32) | (F - 1))) { ++NoBoard; continue; }
				if ((*Cur)->Flags & 8u)
				{
					bool bSets = true;
					for (int32 J = 0; J < Race->Sims.Num(); ++J)
					{
						if (At.Contains((int64(J) << 32) | (F - 1)) && !Race->ForceSet.Contains((int64(J) << 32) | (F - 1))) { bSets = false; }
					}
					if (!bSets) { ++NoSet; continue; }
				}
				const uint8* Rec = (*Cur)->Rec;
				int32 M0, M1, C1, G1;
				FMemory::Memcpy(&M0, (*Prev)->Rec + 0x424, 4); FMemory::Memcpy(&M1, Rec + 0x424, 4); FMemory::Memcpy(&C1, Rec + 0x428, 4); FMemory::Memcpy(&G1, Rec + 0x1DC, 4);
				const FString Kind = M0 != 3 && M1 == 3 ? TEXT("catch") : M0 == 3 && M1 == 3 ? TEXT("ride") : M0 == 3 ? TEXT("exit")
					: ((*Cur)->Flags & 8u) ? TEXT("contact") : TEXT("approach");
				const FRiderState& St = Race->Sims[I]->State;
				++Total;
				KindTotal.FindOrAdd(Kind)++;
				bool bState = int32(St.Motion) == M1 && int32(St.Control) == C1 && St.GrindType == G1;
				if (!bState && int32(St.Motion) == M1 && St.GrindType == G1)
				{
					const FEntry* const* Nx = At.Find((int64(I) << 32) | (F + 1));
					int32 C2 = -1;
					if (Nx) { FMemory::Memcpy(&C2, (*Nx)->Rec + 0x428, 4); }
					if (C2 == int32(St.Control)) { bState = true; ++ControlLag; }
				}
				StateOk += bState;
				bool bAll = bState;
				FString Bad, Row = FString::Printf(TEXT("%u,%u,%d,%s,%d,%d,%d,%d"), Run, F, I, *Kind, M1, C1, int32(St.Motion), int32(St.Control));
				for (int32 K = 0; K < NumFields; ++K)
				{
					float Pv[4] = {}, Lv[4] = {};
					PortFloats(St, Fields[K].Off, Pv);
					FMemory::Memcpy(Lv, Rec + Fields[K].Off, 4 * Fields[K].N);
					const bool bOk = FMemory::Memcmp(Pv, Lv, 4 * Fields[K].N) == 0;
					double E = 0.0;
					for (int32 J = 0; J < Fields[K].N; ++J) { E = FMath::Max(E, double(FMath::Abs(Pv[J] - Lv[J]))); Row += FString::Printf(TEXT(",%.9g,%.9g"), Lv[J], Pv[J]); }
					Exact[K] += bOk;
					MaxErr[K] = FMath::Max(MaxErr[K], E);
					if (!bOk) { bAll = false; Bad += FString::Printf(TEXT(" %s %.3g"), Fields[K].Name, E); }
				}
				AllExact += bAll;
				if (bAll) { KindExact.FindOrAdd(Kind)++; }
				if (bDumpRows)
				{
					FRiderSphereSet Bs;
					const bool bSet = SSXRider::BuildPostAISphereSet(Race->Sims[I]->Profile, St, SSXRider::MaskAll, Bs);
					const FRacerRecord& Rr = Race->Session.GetRacer(I);
					Row += FString::Printf(TEXT(",lag=%d,contacts=%d,impulses=%d"), ((*Cur)->Flags & 2u) ? 1 : 0, Rr.RiderContacts, Rr.RiderImpulses);
					{
						const FEntry* const* Nx = At.Find((int64(I) << 32) | (F + 1));
						const uint8* PoseRec = !((*Cur)->Flags & 2u) ? Rec : (Nx && ((*Nx)->Flags & 6u) == 6u ? (*Nx)->Rec : nullptr);
						float Bo[3] = { 0, 0, 0 };
						if (PoseRec) { FMemory::Memcpy(Bo, PoseRec + 0x600 + 0x4A90 - 0x4600, 12); }
						Row += FString::Printf(TEXT(",BO=%d:%.9g:%.9g:%.9g/%.9g:%.9g:%.9g"), PoseRec ? 1 : 0, Bo[0], Bo[1], Bo[2], float(St.BoardOrigin.X), float(St.BoardOrigin.Y), float(St.BoardOrigin.Z));
						auto Lr = [PoseRec](int32 Off, int32 K) { float V = 0; if (PoseRec) { FMemory::Memcpy(&V, PoseRec + 0x600 + Off - 0x4600 + 4 * K, 4); } return V; };
						Row += FString::Printf(TEXT(",R3=%.9g:%.9g:%.9g/%.9g:%.9g:%.9g,B0=%.6g:%.6g:%.6g/%.6g:%.6g:%.6g,R0=%.6g:%.6g:%.6g/%.6g:%.6g:%.6g"),
							Lr(0x4AD0, 0), Lr(0x4AD0, 1), Lr(0x4AD0, 2), St.RenderRow3.X, St.RenderRow3.Y, St.RenderRow3.Z,
							Lr(0x4A60, 0), Lr(0x4A60, 1), Lr(0x4A60, 2), St.BoardRow0.X, St.BoardRow0.Y, St.BoardRow0.Z,
							Lr(0x4AA0, 0), Lr(0x4AA0, 1), Lr(0x4AA0, 2), St.RenderRow0.X, St.RenderRow0.Y, St.RenderRow0.Z);
					}
					for (int32 K = -1; K < 11; ++K)
					{
						const int32 Off = K < 0 ? 0x480 : 0x4A0 + 0x20 * K;
						float Lv[3];
						FMemory::Memcpy(Lv, Rec + Off, 12);
						const FVector Pc = !bSet ? FVector::ZeroVector : K < 0 ? Bs.BoundCentre : Bs.Centres[K];
						Row += FString::Printf(TEXT(",S%d=%.9g:%.9g:%.9g/%.9g:%.9g:%.9g"), K, Lv[0], Lv[1], Lv[2], float(Pc.X), float(Pc.Y), float(Pc.Z));
					}
					Row += FString::Printf(TEXT(",PR=%.9g:%.9g:%.9g,AY=%.5f:%d:%d:%d"), St.PoseRoot.X, St.PoseRoot.Y, St.PoseRoot.Z, St.AnimYaw, St.bAnimMirror ? 1 : 0, St.bRailAnimPose ? 1 : 0, Race->Poses.IsValidIndex(I) && Race->Poses[I] ? int32(1000.0 * Race->Poses[I]->GetController().Yaw) : -99999);
					Row += Race->DumpAnim(I);
					DumpRows.Add(Row);
				}
				if (!bAll && (Reported < 40 || Kind == TEXT("contact")))
				{
					++Reported;
					AddInfo(FString::Printf(TEXT("run %u racer %d f%u (%s): motion / control / grind %d / %d / %d vs original %d / %d / %d;%s"), Run, I, F, *Kind,
						int32(St.Motion), int32(St.Control), St.GrindType, M1, C1, G1, *Bad));
				}
			}
		};
		Race->Replay.BeginRebuild();
		for (int32 F = 0; Race->Replay.IsRebuilding() && Race->Replay.GetRecordFrame() <= Last; ++F) { Race->Frame(); }
		AddInfo(FString::Printf(TEXT("run %u: %d forced rail samples read before their control update"), Run, Race->ForcedPreControl));
	}
	if (bDumpRows) { FFileHelper::SaveStringArrayToFile(DumpRows, *FPlatformMisc::GetEnvironmentVariable(TEXT("SSX_RAIL_DUMP"))); }
	for (int32 K = 0; K < NumFields; ++K)
	{
		AddInfo(FString::Printf(TEXT("%s: %d / %d bit-exact, max error %.4g"), Fields[K].Name, Exact[K], Total, MaxErr[K]));
	}
	for (const TPair<FString, int32>& P : KindTotal)
	{
		AddInfo(FString::Printf(TEXT("%s: %d / %d one-step frames bit-exact in every field"), *P.Key, KindExact.FindRef(P.Key), P.Value));
	}
	AddInfo(FString::Printf(TEXT("line path distance to go (+0x374) within 1 cm of the trace: %d / %d samples"), LineEqual, LineSamples));
	AddInfo(FString::Printf(TEXT("one-step frames: %d, motion / control / grind type equal %d, every field bit-exact %d (%d clean pairs without the board of the update, %d contact pairs without every set, %d samples read before their control update)"), Total, StateOk, AllExact, NoBoard, NoSet, ControlLag));
	TestTrue(TEXT("rail frames compared"), KindTotal.FindRef(TEXT("ride")) > 0);
	for (const TCHAR* K : { TEXT("catch"), TEXT("ride"), TEXT("exit"), TEXT("contact") })
	{
		TestEqual(*FString::Printf(TEXT("%s updates bit-exact"), K), KindExact.FindRef(K), KindTotal.FindRef(K));
	}
	return !HasAnyErrors();
}
