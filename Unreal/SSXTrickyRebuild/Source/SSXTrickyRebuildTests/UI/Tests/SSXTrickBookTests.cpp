// FUN_00157C18 / FUN_001579E0 / 0x165400 / 0x27E3C0
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Progression/SSXProgression.h"
#include "Sim/Air/RiderMessages.h"
#include "Sim/Air/TrickScore.h"
#include "UI/SSXFeProfile.h"
#include "UI/SSXFeTrickBook.h"

namespace
{
	constexpr EAutomationTestFlags TbTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	SSXFeTrickBook::FTable MakeTable(int32 Char)
	{
		SSXFeTrickBook::FTable T;
		T.Entries.SetNum(12 * SSXFeTrickBook::TricksPerRider);
		for (int32 K = 0; K < SSXFeTrickBook::TricksPerRider; ++K)
		{
			T.Entries[Char * SSXFeTrickBook::TricksPerRider + K].W0 = 0x1000 + K;
			T.Entries[Char * SSXFeTrickBook::TricksPerRider + K].W1 = 0x100;
		}
		return T;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrickBookRaceTest, "SSX.TrickBook.Race", TbTestFlags)
bool FSSXTrickBookRaceTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeTrickBook;
	// FUN_00157C18
	{
		FEntry E;
		E.W0 = 0x00120300;
		E.W1 = 0x02000100;
		TestTrue(TEXT("same record"), Matches(E, 0x00120300, 0x02000100));
		TestTrue(TEXT("prefix Switch / Late, BS, To Fakie, To Late, bail bits ignored"),
			Matches(E, 0x00120300, 0x02000100 | (1u << 16) | (4u << 16) | (2u << 27) | (2u << 20) | (1u << 30) | 0x5u | 0xF0u | 0xF000u));
		TestFalse(TEXT("other flip name"), Matches(E, 0x00120300, 0x02000200));
		TestFalse(TEXT("other flip count"), Matches(E, 0x00120300, 0x03000100));
		TestFalse(TEXT("word 0 differs (grab / spin / named flip)"), Matches(E, 0x00120301, 0x02000100));
	}
	// FUN_001579E0
	const int32 Mac = 3;
	const FTable T = MakeTable(Mac);
	{
		FSSXCareer C;
		C.BookChar = Mac;
		uint32 Ev = 0;
		TestEqual(TEXT("chapter 2 entry while chapter 1 is open: nothing"), LandBookTrick(T, C, Ev, 0x1000 + 5, 0x100), 0u);
		TestEqual(TEXT("chapter 1 entry 2"), LandBookTrick(T, C, Ev, 0x1000 + 2, 0x100 | (1u << 16)), 1u << 2);
		TestTrue(TEXT("set in career +0x1C and slot+0x10"), C.BookDone[1] == (1u << 2) && C.BookDone[0] == 0 && Ev == (1u << 2));
		TestEqual(TEXT("again: already done"), LandBookTrick(T, C, Ev, 0x1000 + 2, 0x100), 0u);
		TestEqual(TEXT("flip mismatch: nothing"), LandBookTrick(T, C, Ev, 0x1000 + 3, 0x200), 0u);
		C.BookDone[0] = 0x2;
		TestEqual(TEXT("done in +0x18: not set again"), LandBookTrick(T, C, Ev, 0x1000 + 1, 0x100), 0u);
	}
	{
		FTable Same = MakeTable(Mac);
		Same.Entries[Mac * TricksPerRider + 5].W0 = 0x1000 + 4;
		FSSXCareer C;
		C.BookChar = Mac;
		C.BookDone[0] = 0xF;
		uint32 Ev = 0;
		TestEqual(TEXT("entries 4 and 5 on one landing"), LandBookTrick(Same, C, Ev, 0x1000 + 4, 0x100), 0x30u);
		TestTrue(TEXT("career and event bits"), C.BookDone[1] == 0x30u && Ev == 0x30u && C.ChapterDone(0) && !C.ChapterDone(1));
	}
	// 0x165400
	if (FTable::Get().IsLoaded())
	{
		FSSXProfile P = FSSXProfile::MakeDefault();
		P.Slot[0].Char = Mac;
		P.Career[Mac].BookDone[0] = P.Career[Mac].BookDone[1] = 0;
		const FEntry* E0 = FTable::Get().Find(Mac, 0);
		const FEntry* E5 = FTable::Get().Find(Mac, 5);
		TestEqual(TEXT("trickdef chapter 2 entry locked"), P.LandBookTrick(0, E5->W0, E5->W1) & (1u << 5), 0u);
		TestTrue(TEXT("trickdef entry 0 learned"), (P.LandBookTrick(0, E0->W0, E0->W1) & 1u) != 0);
		TestTrue(TEXT("slot+0x10 and +0x1C"), (P.Slot[0].EventTricks & 1u) && (P.Career[Mac].BookDone[1] & 1u));
		P.DropEventTricks(0);
		TestTrue(TEXT("0x165400: the event's tricks leave the career"), P.Slot[0].EventTricks == 0 && (P.Career[Mac].BookDone[1] & 1u) == 0);
		// 0x164FC0
		P.LandBookTrick(0, E0->W0, E0->W1);
		P.ShowoffEndTricks(0, SSXProg::SERace, -1);
		P.ShowoffEndTricks(0, SSXProg::WCShowoff, 1);
		TestTrue(TEXT("race / showoff score 1: kept"), (P.Slot[0].EventTricks & 1u) && (P.Career[Mac].BookDone[1] & 1u));
		P.ShowoffEndTricks(0, SSXProg::SEShowoff, 0);
		TestTrue(TEXT("showoff score 0: dropped"), P.Slot[0].EventTricks == 0 && (P.Career[Mac].BookDone[1] & 1u) == 0);
		P.LandBookTrick(0, E0->W0, E0->W1);
		P.ShowoffEndTricks(0, SSXProg::WCShowoff, -1);
		TestTrue(TEXT("showoff not finished: dropped"), P.Slot[0].EventTricks == 0 && (P.Career[Mac].BookDone[1] & 1u) == 0);
		P.Slot[0].Char = -1;
		TestEqual(TEXT("no rider in the slot: nothing"), P.LandBookTrick(0, E0->W0, E0->W1), 0u);
	}
	else
	{
		SSXTest::Skip(*this, TEXT("extracted/DATA/TUTORIAL/TRICKDEF.DAT missing; disc entry checks skipped"));
	}
	// 0x16C420
	{
		using namespace SSXProg;
		FSSXSaveData S = FSSXSaveData::NewSave();
		FSSXProgressSession Sess;
		S.Career[Mac].BookDone[1] = 0x1F;
		const TArray<FSSXUnlock> U = AwardEvent(S, Sess, Mac, SERace, 0, 2, 0, 0, 0, 0x1Cu);
		const int32 I5 = U.IndexOfByKey(FSSXUnlock{ 0, UOutfit, 1 });
		const int32 I9 = U.IndexOfByKey(FSSXUnlock{ 0, UBookEntries, 3 });
		TestTrue(TEXT("chapter 1 -> outfit 1, 3 new entries -> type 9 (3) after it"), I5 != INDEX_NONE && I9 != INDEX_NONE && I5 < I9);
		const TArray<FSSXUnlock> None = AwardEvent(S, Sess, Mac, SERace, 0, 2, 0, 0, 0, 0u);
		TestEqual(TEXT("no new entries: no type 9"), None.IndexOfByPredicate([](const FSSXUnlock& X) { return X.Type == UBookEntries; }), INDEX_NONE);
		FSSXEventResult R;
		R.Mode = SEShowoff;
		R.Venue = 1;
		FSSXEventHuman H;
		H.Char = Mac;
		H.Score = 5000;
		H.EventTricks = 0x3;
		R.Humans.Add(H);
		EndEvent(S, Sess, R, false);
		TestTrue(TEXT("EndEvent: type 9 (2)"), Sess.Unlocks.Contains(FSSXUnlock{ 0, UBookEntries, 2 }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrickBookScoreTest, "SSX.TrickBook.Score", TbTestFlags)
bool FSSXTrickBookScoreTest::RunTest(const FString& Parameters)
{
	using namespace SSXAir;
	// 0x1569B0 / 0x1579E0
	{
		FTrickScore T;
		TArray<uint32> Seen;
		bool bLearn = true;
		T.BookCheck = [&Seen, &bLearn](uint32 W0, uint32 W1) { Seen.Add(W0); Seen.Add(W1); return bLearn; };
		T.SpinDeg = 360.0;
		T.Land(false, 0, false, false, false);
		TestEqual(TEXT("landing: checked once with the landed record"), Seen.Num(), 2);
		TestTrue(TEXT("checked record = the announced one"), Seen.Num() == 2 && Seen[0] == T.Announced.W0 && Seen[1] == T.Announced.W1);
		const FRiderMessage* M = T.Messages.Find(SSXMsg::Camera);
		TestTrue(TEXT("camera icon 0x11, life 1.2"), M && FMath::IsNearlyEqual(M->Life, SSXMsg::LifeCamera));
		FTrickScore U;
		U.BookCheck = [](uint32, uint32) { return false; };
		U.SpinDeg = 360.0;
		U.Land(false, 0, false, false, false);
		TestNull(TEXT("no bit set: no icon"), U.Messages.Find(SSXMsg::Camera));
		FTrickScore V;
		int32 Calls = 0;
		V.BookCheck = [&Calls](uint32, uint32) { ++Calls; return true; };
		V.Land(false, 0, false, false, false);
		TestEqual(TEXT("invalid record: no check"), Calls, 0);
		// 0x156618
		V.SegmentType = 1;
		V.RailFrames = 30;
		V.StartSegment(false, 0, false);
		TestEqual(TEXT("rail exit: checked"), Calls, 1);
		TestNotNull(TEXT("rail exit: icon"), V.Messages.Find(SSXMsg::Camera));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXTrickBookVideoTest, "SSX.TrickBook.Video", TbTestFlags)
bool FSSXTrickBookVideoTest::RunTest(const FString& Parameters)
{
	using namespace SSXFeTrickBook;
	TestEqual(TEXT("0x3AF808 path, Eddie k 0"), VideoEntry(0, 0), FString(TEXT("data\\video\\tb\\Eddi_000.sss")));
	TestEqual(TEXT("JP short Jp (0x331EB0)"), VideoEntry(6, 29), FString(TEXT("data\\video\\tb\\Jp_029.sss")));
	TestEqual(TEXT("Psymon: Psym_ (the archive also has Psym000..)"), VideoEntry(8, 7), FString(TEXT("data\\video\\tb\\Psym_007.sss")));
	// C0FB directory: {u24 offset, u24 size, name\0}, big-endian
	{
		const char* Name = "data\\video\\tb\\Mac_001.sss";
		TArray<uint8> A = { 0xC0, 0xFB, 0, 0, 0, 1 };
		const int32 NameLen = FCStringAnsi::Strlen(Name) + 1;
		const int32 Off = 6 + 6 + NameLen;
		A.Append({ uint8(Off >> 16), uint8(Off >> 8), uint8(Off), 0, 0, 3 });
		A.Append(reinterpret_cast<const uint8*>(Name), NameLen);
		A.Append({ 7, 8, 9 });
		TArray<uint8> Out;
		TestTrue(TEXT("C0FB entry found (case-insensitive)"), FindBigEntry(A, TEXT("DATA\\VIDEO\\TB\\MAC_001.SSS"), Out) && Out == TArray<uint8>({ 7, 8, 9 }));
		TestFalse(TEXT("missing entry"), FindBigEntry(A, TEXT("data\\video\\tb\\Mac_002.sss"), Out));
	}
	TArray<uint8> Bytes;
	if (!ReadVideo(3, 0, Bytes))
	{
		SSXTest::Skip(*this, TEXT("extracted/DATA/VIDEO/TB/TB_MAC.BIG missing; trick video checks skipped"));
		return true;
	}
	SSXPropMovie::FSssFile File;
	TestTrue(TEXT("Mac_000.sss parses"), File.LoadFromMemory(MoveTemp(Bytes)));
	TestTrue(TEXT("128 x 128 frames"), File.NumFrames() > 0 && File.FrameWidth(0) == 128 && File.FrameHeight(0) == 128);
	for (int32 C = 0; C < 12; ++C)
	{
		TArray<uint8> B;
		TestTrue(FString::Printf(TEXT("rider %d: video 29 in its archive"), C), ReadVideo(C, 29, B));
	}
	FVideo V;
	int32 New = 0;
	for (int32 F = 0; F < 8; ++F) { New += V.Step(3, 0) ? 1 : 0; }
	TestEqual(TEXT("8 steps: 2 chunks"), New, 2);
	TestTrue(TEXT("frame 1 shown, 128 x 128 pixels"), V.HasFrame() && V.Stream.Shown == 1 && V.Pixels.Num() == 128 * 128);
	for (int32 F = 0; F < 4 * File.NumFrames(); ++F) { V.Step(3, 0); }
	TestEqual(TEXT("loops back to frame 1"), V.Stream.Shown, 1);
	V.Step(3, -1);
	TestFalse(TEXT("released: no frame"), V.HasFrame());
	V.Step(3, 1);
	TestFalse(TEXT("new stream: blank until the 4th step"), V.HasFrame());
	return true;
}

#endif
