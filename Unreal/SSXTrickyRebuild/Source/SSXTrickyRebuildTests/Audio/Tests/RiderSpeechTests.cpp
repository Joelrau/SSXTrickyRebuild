// 0x234698 / 0x234C38 / 0x235090 / 0x235398
#include "Audio/SSXAnnouncer.h"
#include "Audio/SSXAudioConfig.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXGameAudio.h"
#include "Audio/SSXRiderSpeech.h"
#include "Audio/SSXSpeechSystem.h"
#include "Camera/CameraDefs.h"
#include "Engine/GameInstance.h"
#include "Harness/RiderSetup.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/Paths.h"
#include "Race/GameRng.h"
#include "Race/IntroDialogue.h"
#include "Race/RaceSession.h"
#include "Race/RaceTrackData.h"
#include "Sim/PatchWorld.h"
#include "Sim/Rider/RiderProfiles.h"
#include "Sim/RiderSim.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags RiderSpeechFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
	namespace RS = SSXRiderSpeech;

	uint32 CharId(int32 E) { return SSXSpeech::RequestId(SSXSpeech::DbChar, E); }

	struct FSeqRand
	{
		TArray<uint32> V;
		int32 I = 0;
		uint32 operator()() { return V.IsValidIndex(I) ? V[I++] : 0u; }
	};

	struct FRig
	{
		RS::FHelpers H;
		bool bFree = true;
		TArray<int32> Zones;
		TArray<TPair<uint32, int32>> Sent;
		TArray<TArray<uint32>> Words;
		TArray<int32> Stops;
		int32 Kind = 0;
		FRig()
		{
			H.IsChannel1Free = [this]() { return bFree; };
			H.StopIfTag = [this](int32 T) { Stops.Add(T); };
			H.Zone = [this](const FVector& P) { const int32 I = int32(P.X); return Zones.IsValidIndex(I) ? Zones[I] : 0; };
			H.Relation = [this](int32, int32) { return Kind; };
			H.Post = [this](const SSXSpeech::FRequest& R, const FVector&, int32 Tag, int32)
			{
				Sent.Add({ R[0], Tag });
				TArray<uint32> W;
				for (int32 K = 1; K < 3; ++K) W.Add(R[K]);
				Words.Add(W);
			};
		}
	};

	RS::FRacer Racer(int32 Index, int32 Character, bool bHuman = false)
	{
		RS::FRacer R;
		R.Index = Index;
		R.Character = Character;
		R.Voice = Character;
		R.bHuman = bHuman;
		R.GamePos = FVector(Index, 0, 0);
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioRiderSpeechTest, "SSX.Audio.RiderSpeech", RiderSpeechFlags)
bool FSSXAudioRiderSpeechTest::RunTest(const FString&)
{
	TestEqual(TEXT("speaker masks"), RS::SpeakerMask(0) * 1 + RS::SpeakerMask(4) * 1000 + RS::SpeakerMask(6) * 100000, 0x40u + 2u * 1000u + 0x400u * 100000u);
	TestEqual(TEXT("MMM (12) has no mask"), RS::SpeakerMask(12), 0u);
	{
		FRig G;
		FSeqRand R{ { 2, 1 } };
		G.H.Wipeout(Racer(1, 4), [&R]() { return R(); });
		G.H.Wipeout(Racer(1, 4), [&R]() { return R(); });
		TestTrue(TEXT("0x6024 then 0x6023 with tag 0xD"), G.Sent.Num() == 2 && G.Sent[0].Key == CharId(0x6024) && G.Sent[1].Key == CharId(0x6023)
			&& G.Sent[0].Value == RS::TagRiderLine);
		TestEqual(TEXT("words {speaker, 0x40}"), G.Words[0], TArray<uint32>{ 2, 0x40 });
		TestTrue(TEXT("tag stops: own voice, then 0xC"), G.Stops.Num() >= 2 && G.Stops[0] == 4 && G.Stops[1] == RS::TagVictimLine);
		G.Sent.Reset();
		G.bFree = false;
		G.H.Wipeout(Racer(1, 4), [&R]() { return R(); });
		G.bFree = true;
		G.Zones = { 0, -1 };
		G.H.Wipeout(Racer(1, 4), [&R]() { return R(); });
		RS::FRacer Done = Racer(1, 4);
		Done.bFinished = true;
		G.Zones.Reset();
		G.H.Wipeout(Done, [&R]() { return R(); });
		TestEqual(TEXT("channel busy / out of range / finished: nothing"), G.Sent.Num(), 0);
	}
	{
		FRig G;
		FSeqRand R{ { 0 } };
		G.H.Bump(Racer(0, 6), true, [&R]() { return R(); });
		G.H.Bump(Racer(0, 6), false, [&R]() { return R(); });
		TestTrue(TEXT("hard bump 0x6022, soft bump the wipe-out line"), G.Sent.Num() == 2 && G.Sent[0].Key == CharId(0x6022) && G.Sent[1].Key == CharId(0x6024));
	}
	{
		FRig G;
		RS::FRacer A = Racer(0, 0), B = Racer(1, 1);
		G.Kind = 2;
		TestEqual(TEXT("kind 2: 1"), G.H.RelationCheck(A, B), 1);
		G.Kind = 0;
		TestEqual(TEXT("kind 0, level <= 60, no grudge: 0"), G.H.RelationCheck(A, B), 0);
		A.Level[1] = 61;
		TestEqual(TEXT("kind 0, level > 60: 1"), G.H.RelationCheck(A, B), 1);
		A.Level[1] = 0; A.bGrudge = true; A.GrudgeRival = 1;
		TestEqual(TEXT("grudge on B: 1"), G.H.RelationCheck(A, B), 1);
		G.Kind = -1;
		TestEqual(TEXT("negative kind: 0"), G.H.RelationCheck(A, B), 0);
	}
	{
		FRig G;
		FSeqRand R{ { 0, 0, 1, 0, 0 } };
		auto Rand = [&R]() { return R(); };
		G.H.Hit(Racer(0, 10), Racer(1, 4, true), true, true, Rand);
		TestTrue(TEXT("knock-down: the victim's wipe-out line"), G.Sent.Num() == 1 && G.Sent[0].Key == CharId(0x6024) && G.Words[0][0] == 2);
		G.Sent.Reset(); G.Words.Reset();
		G.H.Hit(Racer(0, 10), Racer(1, 4, true), true, false, Rand);
		TestTrue(TEXT("attacker speaks: 0x6036 {Brodi, 0x40 | 2}"), G.Sent.Num() == 1 && G.Sent[0].Key == CharId(0x6036) && G.Words[0] == TArray<uint32>({ 0x80, 0x42 })
			&& G.Sent[0].Value == 10);
		G.Sent.Reset(); G.Words.Reset();
		G.H.Hit(Racer(0, 10), Racer(1, 4, true), false, false, Rand);
		TestTrue(TEXT("victim speaks: 0x6036 {Moby, 0x48}, tag 0xC"), G.Sent.Num() == 1 && G.Words[0] == TArray<uint32>({ 2, 0x48 }) && G.Sent[0].Value == RS::TagVictimLine);
		G.Sent.Reset(); G.Words.Reset();
		G.H.Hit(Racer(0, 10), Racer(1, 8), true, false, Rand);
		TestTrue(TEXT("no human: wipe-out line"), G.Sent.Num() == 1 && (G.Sent[0].Key == CharId(0x6024) || G.Sent[0].Key == CharId(0x6023)) && G.Words[0][0] == 0x80);
	}
	{
		FRig G;
		G.H.Focus = 4;
		RS::FRacer P = Racer(4, 4, true), Q = Racer(1, 6);
		P.Velocity = FVector(2000, 0, 0);
		Q.Velocity = FVector(1000, 0, 0);
		FSeqRand R{ { 0 } };
		G.H.Overtake(P, Q, 599, 0, [&R]() { return R(); });
		TestEqual(TEXT("quiet for 600 frames after GO"), G.Sent.Num(), 0);
		G.H.Overtake(P, Q, 600, 0, [&R]() { return R(); });
		TestTrue(TEXT("the human passer speaks: 0x601A {Moby, JP}"), G.Sent.Num() == 1 && G.Sent[0].Key == CharId(0x601A) && G.Words[0] == TArray<uint32>({ 2, 0x400 }));
		G.Sent.Reset(); G.Words.Reset();
		Q.Velocity = FVector(1500, 0, 0);   // 500 cm/s: below 670.56
		G.H.Overtake(P, Q, 900, 0, [&R]() { return R(); });
		TestEqual(TEXT("too slow a pass: nothing"), G.Sent.Num(), 0);
		Q.Velocity = FVector(1000, 0, 0);
		Q.Level[4] = 70;
		FSeqRand R2{ { 1 } };
		G.H.Overtake(P, Q, 900, 0, [&R2]() { return R2(); });
		TestTrue(TEXT("both qualify, rand 1: passer"), G.Sent.Num() == 1 && G.Sent[0].Key == CharId(0x601A));
	}
	{
		FSSXSpeechSystem Sys;
		FSSXAnnouncer A;
		A.Reset(2, 0, 3);
		FSeqRand R{ { 0, 0, 0, 0, 0, 0 } };
		A.TrickAnnounce(Sys, 0x0100, 0, 5000, false, false, [&R]() { return R(); });
		TestTrue(TEXT("0x201C then 0x200F (not posted: no database) -> snd+0x6E04 = 0"), A.Posted.Num() >= 2 && (A.Posted[0] & 0xFFFF) == 0x201C
			&& (A.Posted[1] & 0xFFFF) == 0x200F && A.TrickGate == 0);
		A.Posted.Reset();
		A.TrickAnnounce(Sys, 0x0100, 0, 5000, false, false, [&R]() { return R(); });
		TestEqual(TEXT("gate closed until the next segment"), A.Posted.Num(), 0);
		A.SegmentStart();
		A.TrickAnnounce(Sys, 0x0100, 0, 5000, true, false, [&R]() { return R(); });
		TestTrue(TEXT("a repeated trick: 0x201A"), A.Posted.Num() == 1 && (A.Posted[0] & 0xFFFF) == 0x201A);
		// 0x231FF8 / 0x2310C8
		TestTrue(TEXT("field words"), FSSXAnnouncer::FieldWord(2, 2) == 0x15 && FSSXAnnouncer::FieldWord(0, 0) == 0xA && FSSXAnnouncer::FieldWord(1, 1) == 0
			&& FSSXAnnouncer::FieldWord(3, 1) == 4 && FSSXAnnouncer::FieldWord(1, 0) == 8);
		A.Reset(4, 0, 0);
		A.PlayerCharacter = 4;
		const TArray<uint32> Stg = A.StagingNarration(Sys, 0, [](int32, int32) { return 0; });
		TestEqual(TEXT("staging 0x8006 {venue 0x10 | 0x400, 0x2000, Moby 0x800, English 1}"), Stg,
			TArray<uint32>{ SSXSpeech::RequestId(SSXSpeech::DbNarr, 0x8006), 0x410, 0x2000, 0x800, 1 });
		A.PlayerVoice = 6;   // the player bit follows the voice byte (+0x64), not the character
		TestEqual(TEXT("staging player bit from the voice byte (voice 6, not the character 4)"), A.StagingNarration(Sys, 0, [](int32, int32) { return 0; })[3], FSSXAnnouncer::NarrCharacterBit(6));
		A.PlayerVoice = -1;
		const TArray<uint32> Rel = A.StagingNarration(Sys, 2, [](int32, int32 B) { return B == 6 ? 1 : 0; });
		TestTrue(TEXT("op 0x2A: riders with relation 1 (JP 0x20)"), Rel.Num() == 5 && Rel[2] == 0x20);
		A.Reset(4, 0, 1);
		A.PlayerCharacter = 4;
		TestEqual(TEXT("staging narration only in the first WC heat"), A.StagingNarration(Sys, 0, [](int32, int32) { return 0; }).Num(), 0);
	}
	// 0x234F68
	{
		FRig G;
		G.H.Focus = 0;
		G.H.Taunt(Racer(3, 7), Racer(0, 3, true));
		TestTrue(TEXT("taunt: CHAR 0x600F {4, 1}, tag = the speaker's voice"), G.Sent.Num() == 1 && G.Sent[0].Key == CharId(0x600F)
			&& G.Sent[0].Value == 7 && G.Words[0] == TArray<uint32>{ 4, 1 });
		G.Sent.Reset();
		G.Words.Reset();
		G.bFree = false;
		G.H.Taunt(Racer(3, 7), Racer(0, 3, true));
		G.bFree = true;
		G.H.Taunt(Racer(3, 7), Racer(4, 4));
		G.Zones = { 0, 0, 0, 0, -1 };
		G.H.Taunt(Racer(3, 7), Racer(4, 4, true));
		G.Zones = { 0, 0, 0, 0, 1 };
		G.H.Taunt(Racer(3, 7), Racer(4, 4, true));
		G.Zones.Reset();
		RS::FRacer Done = Racer(0, 3, true);
		Done.bFinished = true;
		G.H.Taunt(Racer(3, 7), Done);
		TestEqual(TEXT("taunt: busy / no human / zones / finished: nothing"), G.Sent.Num(), 0);
		RS::FRacer Mmm = Racer(3, 0);
		Mmm.Voice = 12;
		G.H.Taunt(Mmm, Racer(0, 3, true));
		TestTrue(TEXT("taunt: Mix Master Mike posts with speaker mask 0"), G.Sent.Num() == 1 && G.Words[0] == TArray<uint32>{ 0, 1 });
	}
	// 0x235298
	{
		FRig G;
		G.H.Focus = 0;
		G.H.WinTaunt(Racer(0, 3, true), 100.0);
		G.H.WinTaunt(Racer(0, 3, true), 0.5);
		G.H.WinTaunt(Racer(0, 3, true), 0.65);
		G.H.WinTaunt(Racer(2, 7), 100.0);
		TestTrue(TEXT("win taunt: 0x6028, 0x6027, 0x6028 {1}"), G.Sent.Num() == 3 && G.Sent[0].Key == CharId(0x6028)
			&& G.Sent[1].Key == CharId(0x6027) && G.Sent[2].Key == CharId(0x6028) && G.Words[0][0] == 1u);
	}
	return true;
}

namespace
{
	bool SessionOneSlots(SSXIntroDialogue::FSlot Out[SSXIntroDialogue::NumSlots])
	{
		const SSXIntroDialogue::FData& D = SSXIntroDialogue::FData::Get();
		if (!D.bLoaded)
		{
			return false;
		}
		SSXIntroDialogue::FContext C;
		const int32 Ids[15] = { 429, 120, 236, 429, 429, 429, 429, 429, 429, 429, 337, 330, 331, 332, 335 };
		const int32 Types[15] = { 0, 9, 21, 57, 57, 57, 57, 57, 57, 57, 55, 45, 53, 56, 51 };
		const int32 Vars[15] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 2 };
		FSeqRand Q;
		for (int32 K = 0; K < 15; ++K)
		{
			C.Ids[K] = Ids[K];
			C.Types[K] = Types[K];
			if (Ids[K] != SSXIntroDialogue::DefaultId) Q.V.Add(uint32(Vars[K]));
		}
		SSXIntroDialogue::FillLines(C, D, Out, [&Q]() { return Q(); });
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioRaceSimTest, "SSX.Audio.RaceSim", RiderSpeechFlags)
bool FSSXAudioRaceSimTest::RunTest(const FString&)
{
	FRaceTrackData Data;
	FPatchWorld World;
	FString Err;
	if (!Data.Load(FRaceTrackData::DefaultPath(TEXT("gari")), &Err) || !World.LoadTrack(FPatchWorld::DefaultTrackPath(TEXT("gari")), &Err))
	{
		AddError(FString::Printf(TEXT("gari race data / patches: %s"), *Err));
		return false;
	}
	SSXIntroDialogue::FSlot Slots15[SSXIntroDialogue::NumSlots];
	FSSXCameraDefSet Defs;
	if (!SessionOneSlots(Slots15) || !Defs.LoadForTrack(TEXT("gari")) || !Defs.FindScene(TEXT("FOE_1")))
	{
		AddError(TEXT("intro_dialogue.json / camera scenes missing (tools/export_intro_dialogue.py, tools/export_cameras.py)"));
		return false;
	}
	USSXAudioSubsystem* Audio = NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	SSXGameAudio::OnRaceSetup(*Audio, TEXT("gari"), 0, int32(ERaceEventMode::CircuitRace));
	if (!Audio->GetSpeech().IsLoaded())
	{
		AddError(TEXT("race speech not converted (tools/convert_speech.py)"));
		return false;
	}
	Audio->GetAnnouncer().HeatWord = 0;

	const int32 Riders[6] = { 10, 0, 8, 11, 4, 6 };
	TArray<TUniquePtr<FRiderSim>> Sims;
	TArray<USSXBoardSoundComponent*> Boards;
	FRaceSession Session;
	FRaceConfig Cfg;
	Cfg.Mode = ERaceEventMode::CircuitRace;
	TestTrue(TEXT("session"), Session.Init(Data, Cfg));
	for (int32 I = 0; I < 6; ++I)
	{
		TUniquePtr<FRiderSim> S = MakeUnique<FRiderSim>();
		SSXRiderSetup::RegisterStates(*S, true);
		S->SetWorld(&World);
		S->Profile.Character = Riders[I];
		Session.AddRacer(S.Get(), I == 4);
		USSXBoardSoundComponent* B = NewObject<USSXBoardSoundComponent>();
		B->bIsAI = I != 4;
		Boards.Add(B);
		Sims.Add(MoveTemp(S));
	}
	SSXIntroDialogue::FObject Obj;
	FSeqRand Any;
	SSXIntroDialogue::CopyToObject(Slots15, Obj, [&Any]() { return Any(); });
	for (int32 K = 0; K < 3; ++K)
	{
		Session.IntroSlots[K].Type = Slots15[K].Type;
		for (int32 J = 0; J < SSXIntroDialogue::NumLines; ++J)
		{
			const SSXIntroDialogue::FObjLine& L = Obj.Lines[K][J];
			Session.IntroSlots[K].Lines.Add({ L.Speaker, L.LineId, Slots15[K].Lines[J].Aux, L.Speech });
		}
	}
	Session.IntroLineKey = [](int32 LineId) { const SSXIntroDialogue::FLineClip* C = SSXIntroDialogue::FData::Get().Clip(LineId); return C ? C->Key() : -1; };
	SSXIntroDialogue::FScenePlayer Scene;
	int32 DirectorLeft = 0;
	Scene.OnEvent = [&](int32 Racer, int32 Value) { Audio->OnAnimEvent(Value, Racer, Sims[Racer]->State.Position, Racer == 4); };
	Session.IsIntroBusy = [&]() { return DirectorLeft > 0; };
	Session.IsSkipPressed = [&]() { return Session.GetIntroStep() == 0x11; };
	Session.OnDirectorReset = [&]() { DirectorLeft = 0; Scene.Stop(); };
	Session.OnIntroAction = [&](const FString& Name, int32 Arg, const FString& Text)
	{
		if (Name == TEXT("slot_start")) Obj.Slot = Arg;
		else if (Name == TEXT("intro_speech_begin")) Audio->IntroSpeechBegin();
		else if (Name == TEXT("intro_speech_end")) Audio->IntroSpeechEnd();
		else if (Name == TEXT("anim_speech")) Audio->IntroSpeechPost(Arg);
		else if (Name == TEXT("director_play") && Text == TEXT("STG_COM_1")) DirectorLeft = 300;
		else if (Name == TEXT("director_play") && Text == TEXT("GAT_FO_1"))
		{
			DirectorLeft = 600;
			TArray<SSXIntroDialogue::FSceneRacer> R;
			for (int32 I = 0; I < 6; ++I) R.Add({ I == 4 ? 0 : -1, Riders[I] });
			Scene.Start(*Defs.FindScene(TEXT("FOE_1")), Defs, R, Obj, SSXIntroDialogue::FData::Get());
		}
	};
	Session.Start();
	int32 Beeps = 0, Landings = 0, Crashes = 0, Frame = 0;
	bool bForcedWipe = false;
	TArray<FString> Released;
	for (Frame = 0; Frame < 2400 + 1800; ++Frame)
	{
		const ERacePhase Prev = Session.GetPhase();
		Session.Tick();
		SSXGameAudio::RaceFrame(*Audio, Session, 4, Prev);
		for (int32 I = 0; I < 6; ++I)
		{
			Sims[I]->Step(FRiderInputWord());
		}
		if (DirectorLeft > 0) --DirectorLeft;
		Scene.Step(Obj, SSXIntroDialogue::FData::Get());
		for (const FRaceCue& C : Session.GetCues())
		{
			SSXGameAudio::HandleSpeechCue(*Audio, Session, C);
		}
		Session.ClearCues();
		if (Session.GetPhase() == ERacePhase::Race && !bForcedWipe && Session.GetClock() > 10.0)
		{
			bForcedWipe = true;
			FRiderImpact Hit;
			Hit.Point = Sims[0]->State.Position;
			Hit.Direction = FVector(1, 0, 0);
			Hit.Normal = FVector(-1, 0, 0);
			Hit.Speed = 1500.0;
			Sims[0]->StartWipeOut(Hit, TEXT("audio race sim"));
		}
		for (int32 I = 0; I < 6; ++I)
		{
			Boards[I]->OneShotLog.Reset();
			Boards[I]->UpdateFromRider(Sims[I]->State, 1.f / 60.f);
			for (const FString& Id : Boards[I]->OneShotLog)
			{
				const int32 Snd = FCString::Atoi(*FPaths::GetCleanFilename(Id));
				Landings += Id.StartsWith(TEXT("sfx/zboard/")) && (Snd % 8 == 1 || Snd % 8 == 5) ? 1 : 0;
				Crashes += Id == SSXBoardAudio::ZbxsfxId(48) || Id == SSXBoardAudio::ZbxsfxId(49) || Id == SSXBoardAudio::ZbxsfxId(50)
					|| Id == SSXBoardAudio::ZbxsfxId(64) ? 1 : 0;
			}
		}
	}
	for (const SSXRaceAudio::FCommand& C : Audio->GetRaceCommandLog()) Beeps += C.Kind == SSXRaceAudio::FCommand::Beep ? 1 : 0;
	for (const FString& L : Audio->AnimEventLog) { if (L.StartsWith(TEXT("release:"))) Released.Add(L); }
	TestTrue(TEXT("the pre-race intro reached the race"), Session.GetIntroStep() == SSXIntro::Done && bForcedWipe);
	TestEqual(TEXT("banter: Moby's line at Moby, then JP's at JP (GAT_FO_1)"), Released.Num() >= 2 ? TArray<FString>{ Released[0], Released[1] } : Released,
		TArray<FString>{ TEXT("release:4:1025"), TEXT("release:5:1026") });
	int32 AnimSamples = 0;
	for (const FString& K : Audio->SpeechLog) AnimSamples += K.StartsWith(TEXT("anim/")) ? 1 : 0;
	TestTrue(TEXT("the banter samples were played"), AnimSamples >= 2);
	TestEqual(TEXT("countdown: four beeps"), Beeps, 4);
	const TArray<uint32>& Mc = Audio->GetAnnouncer().Posted;
	TestTrue(TEXT("GO lines 0x204A and 0x2020 (WC heat 0)"), Mc.Contains(SSXSpeech::RequestId(SSXSpeech::DbMC, 0x204A)) && Mc.Contains(SSXSpeech::RequestId(SSXSpeech::DbMC, 0x2020)));
	TestTrue(TEXT("landings of the riders"), Landings > 0);
	TestTrue(TEXT("the wipe-out crash sound"), Crashes > 0);
	const TArray<uint32>& Rider = Audio->GetRiderSpeech().Posted;
	TestTrue(TEXT("the wipe-out remark (CHAR 0x6023 / 0x6024)"), Rider.Contains(CharId(0x6023)) || Rider.Contains(CharId(0x6024)));
	{
		USSXBoardSoundComponent* B = NewObject<USSXBoardSoundComponent>();
		FRiderState S;
		S.Motion = ERiderMotion::Ground;
		S.SurfaceId = 0;
		B->UpdateFromRider(S, 1.f / 60.f);
		const ESSXSnowSurface Before = B->CurrentSurface;
		S.Motion = ERiderMotion::RailSlide;
		S.SurfaceId = 13;
		++S.Frame;
		B->UpdateFromRider(S, 1.f / 60.f);
		TestTrue(TEXT("grind: snow -> the rail's METAL programs (no dedicated rail one-shot)"), Before == ESSXSnowSurface::Powder && B->CurrentSurface == ESSXSnowSurface::Metal
			&& B->OneShotLog.Num() == 0);
	}
	AddInfo(FString::Printf(TEXT("race sim: %d frames, intro step %d, released %d banter lines, %d beeps, %d landings, %d crash sounds, %d rider remarks, %d MC posts"),
		Frame, Session.GetIntroStep(), Released.Num(), Beeps, Landings, Crashes, Rider.Num(), Mc.Num()));
	return true;
}

// 0x234698 / 0x234730
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioRiderSpeechVoicesTest, "SSX.Audio.RiderSpeechVoices", RiderSpeechFlags)
bool FSSXAudioRiderSpeechVoicesTest::RunTest(const FString&)
{
	uint32 All = 0;
	for (int32 V = 0; V < 12; ++V)
	{
		const uint32 M = RS::SpeakerMask(V);
		TestTrue(FString::Printf(TEXT("voice %d: one new bit"), V), FMath::IsPowerOfTwo(M) && !(All & M));
		All |= M;
	}
	TestEqual(TEXT("12 bits"), All, 0xFFFu);
	TestEqual(TEXT("voice 12 / -1: no mask"), RS::SpeakerMask(12) | RS::SpeakerMask(-1), 0u);

	auto Voiced = [](int32 Index, int32 Character, int32 Voice, bool bHuman)
	{
		RS::FRacer R = Racer(Index, Character, bHuman);
		R.Voice = Voice;
		return R;
	};
	for (int32 V = 0; V <= 12; ++V)
	{
		const uint32 M = RS::SpeakerMask(V);
		const int32 Ch = V == 12 ? 3 : V;
		const int32 O = (V + 1) % 12;
		FRig G;
		TArray<int32> RelChars;
		G.H.Relation = [&RelChars](int32 A, int32 B) { RelChars.Add(A); RelChars.Add(B); return 0; };
		FSeqRand R{ { 0, 0, 1 } };
		auto Rand = [&R]() { return R(); };
		G.H.Wipeout(Voiced(0, Ch, V, true), Rand);
		G.H.Bump(Voiced(0, Ch, V, true), true, Rand);
		G.H.Hit(Voiced(0, Ch, V, true), Voiced(1, O, O, false), true, false, Rand);
		G.H.Hit(Voiced(1, O, O, false), Voiced(0, Ch, V, true), true, false, Rand);
		G.H.Focus = 0;
		RS::FRacer P = Voiced(0, Ch, V, true), Q = Voiced(1, O, O, false);
		P.Velocity = FVector(2000, 0, 0);
		Q.Velocity = FVector(1000, 0, 0);
		G.H.Overtake(P, Q, 1000, 0, Rand);
		const bool bOk = G.Sent.Num() == 5
			&& G.Words[0][0] == M && G.Words[1][0] == M && G.Words[2][0] == M && G.Words[3][0] == M
			&& G.Words[4] == TArray<uint32>({ M, RS::SpeakerMask(O) })
			&& G.Sent[2].Value == V && G.Sent[4].Value == V;
		TestTrue(FString::Printf(TEXT("voice %d: wipe-out / bump / hit (attacker, victim) / overtake speak with mask 0x%x, voice tag %d"), V, M, V), bOk);
		TestTrue(FString::Printf(TEXT("voice %d: own-voice tag stops"), V), G.Stops.Contains(V) && !G.Stops.Contains(Ch == V ? -1 : Ch));
		TestTrue(FString::Printf(TEXT("voice %d: relation by character %d"), V, Ch), RelChars.Contains(Ch) && (V == Ch || !RelChars.Contains(V)));
	}

	const SSXRider::FRiderTable& T = SSXRider::FRiderTable::Shared();
	if (T.IsLoaded())
	{
		for (int32 C = 0; C < 12; ++C)
		{
			FRiderProfile P;
			SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(T, C, SSXRider::FPlayerOptions()), P);
			FRiderProfile A;
			SSXRider::ApplyProfile(SSXRider::BuildComputerProfile(T, C, false, 0, 0), A);
			TestTrue(FString::Printf(TEXT("rider %d: voice byte = character (player and computer)"), C), P.GetVoice() == C && A.GetVoice() == C && P.Character == C);
		}
		SSXRider::FPlayerOptions O;
		O.bMixMasterMike = true;
		FRiderProfile M;
		SSXRider::ApplyProfile(SSXRider::BuildPlayerProfile(T, 7, O), M);
		TestTrue(TEXT("Mix Master Mike: voice 12, character kept"), M.GetVoice() == 12 && M.Character == 7);
	}
	else
	{
		SSXTest::Skip(*this, TEXT("rider table missing: profile voice checks skipped"));
	}
	FRiderProfile Unset;
	Unset.Character = 5;
	TestEqual(TEXT("no profile loaded: voice falls back to +0x38"), Unset.GetVoice(), 5);

	SSXSpeech::FEventDatabase Db;
	TArray<SSXSpeech::FSampleGroup> Groups;
	FString Err;
	if (!SSXSpeech::LoadSection(FPaths::Combine(SSXAudio::ConvertedDir(), TEXT("speech"), TEXT("char")), TEXT("char"), Db, Groups, &Err))
	{
		SSXTest::Skip(*this, FString::Printf(TEXT("CHAR speech data missing (%s): sample checks skipped"), *Err));
		return true;
	}
	SSXSpeech::FEngine E;
	E.Register(Db, Groups);
	struct FEv { int32 Id; uint32 W2; bool bTarget; };
	const FEv Evs[] = { { 0x6022, 0, false }, { 0x6023, 0x40, false }, { 0x6024, 0x40, false }, { 0x6036, 0x42, false }, { 0x6036, 0x48, false },
		{ 0x6036, 0x50, false }, { 0x6036, 0x41, false }, { 0x6036, 0x44, false }, { 0x6036, 0x60, false },
		{ 0x6019, 0, true }, { 0x601A, 0, true }, { 0x601B, 0, true }, { 0x601C, 0, true } };
	int32 Lines = 0, Wrong = 0;
	for (int32 V = 0; V <= 12; ++V)
	{
		const uint32 M = RS::SpeakerMask(V);
		const int32 O = (V + 5) % 12;
		for (const FEv& Ev : Evs)
		{
			for (int32 K = 0; K < 6; ++K)
			{
				const uint32 W2 = Ev.bTarget ? RS::SpeakerMask(O) : Ev.W2;
				SSXSpeech::FLine L;
				const bool bRes = E.Resolve(SSXSpeech::MakeRequest({ SSXSpeech::RequestId(SSXSpeech::DbChar, Ev.Id), M, W2 }), L);
				if (V == 12)
				{
					Wrong += bRes ? 1 : 0;
					continue;
				}
				if (!bRes) continue;
				++Lines;
				for (const SSXSpeech::FResolvedSample& S : L.Samples)
				{
					const SSXSpeech::FSampleGroup& G = E.GetGroups()[S.Group];
					const bool bSpeaker = (1u << G.Tag(S.Sample, 0)) == M;
					const bool bTargetOk = !(Ev.bTarget && G.NumTags >= 2) || (1u << G.Tag(S.Sample, 1)) == W2;
					Wrong += (bSpeaker && bTargetOk) ? 0 : 1;
				}
			}
		}
	}
	TestTrue(TEXT("CHAR remarks resolved"), Lines > 0);
	TestEqual(TEXT("samples of another speaker / target, or a line for voice 12"), Wrong, 0);
	return true;
}

#endif
