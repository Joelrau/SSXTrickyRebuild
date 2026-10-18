// 0x2153F8 / 0x1033C0 / 0x1055A8 / 0x104E68
#include "Audio/SSXAudioDiag.h"
#include "Audio/SSXAudioSubsystem.h"
#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Audio/SSXUISounds.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Sim/Ground/GroundControls.h"
#include "Sim/RiderSim.h"
#include "Sound/SoundAttenuation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AuditFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	USSXAudioSubsystem* MakeAuditAudio()
	{
		return NewObject<USSXAudioSubsystem>(NewObject<UGameInstance>());
	}

	FString Zbx(int32 S) { return SSXUISound::RaceId(S); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioAuditTableIdsTest, "SSX.Audio.Audit.TableIds", AuditFlags)

bool FSSXAudioAuditTableIdsTest::RunTest(const FString& Parameters)
{
	USSXAudioSubsystem* A = MakeAuditAudio();
	if (!A->GetManifest().IsLoaded())
	{
		SSXTest::Skip(*this, TEXT("SKIPPED: converted/audio/manifest.json missing (tools/convert_audio.py)"));
		return true;
	}
	TArray<FString> Ids = USSXBoardSoundComponent::AllSoundIds();
	for (const int32 S : { 48, 49, 50, 64, 51, 72, 73, 74, 77, 78, 90, 91, 95, 107, 108, 109, 0x72, 0x73, 119, 120, 121, 122, 123, 32 })
	{
		Ids.AddUnique(Zbx(S));
	}
	for (const TPair<FName, FString>& P : A->UISounds) { Ids.AddUnique(P.Value); }
	for (int32 K = 0; K < 3; ++K) { Ids.AddUnique(FString::Printf(TEXT("sfx/tricky/%02d"), K)); }
	Ids.AddUnique(TEXT("sfx/zboard/80"));
	Ids.AddUnique(TEXT("sfx/zboard/81"));
	for (int32 K = 0; K < 3; ++K) { Ids.AddUnique(FString::Printf(TEXT("sfx/Crowd/%02d"), K)); }
	struct FOp8 { const TCHAR* Bank; int32 Id; };
	const FOp8 Op8[] = {
		{ TEXT("garibaldi1"), 82 }, { TEXT("alaska1"), 82 }, { TEXT("iceberg"), 82 }, { TEXT("elysium1"), 82 },
		{ TEXT("mesabanca1"), 82 }, { TEXT("mesabanca1"), 83 }, { TEXT("snowdream1"), 82 }, { TEXT("snowdream1"), 83 },
		{ TEXT("pipedream1"), 82 }, { TEXT("pipedream1"), 64 }, { TEXT("pipedream1"), 65 }, { TEXT("megaplex1"), 64 },
		{ TEXT("megaplex1"), 65 }, { TEXT("merqurycity1"), 43 }, { TEXT("merqurycity1"), 64 }, { TEXT("merqurycity1"), 122 } };
	for (const FOp8& T : Op8)
	{
		Ids.AddUnique(FString::Printf(TEXT("sfx/%s/%02d"), T.Bank, T.Id));
	}
	// TODO: the Merqury 0x7A variant may map 43 elsewhere;
	const TSet<FString> NotInBank = { TEXT("sfx/mesabanca1/83"), TEXT("sfx/merqurycity1/43"), TEXT("sfx/pipedream1/65"),
		TEXT("sfx/zboard/69"), TEXT("sfx/zboard/70") };
	int32 Resolved = 0;
	for (const FString& Id : Ids)
	{
		const bool bOk = A->LoadSound(Id) != nullptr;
		if (NotInBank.Contains(Id))
		{
			TestFalse(*FString::Printf(TEXT("%s is not in the original bank"), *Id), bOk);
			TestTrue(*FString::Printf(TEXT("%s: reason not-in-manifest"), *Id), A->MissReason(Id) == SSXAudioDiag::EResult::NotInManifest);
			continue;
		}
		if (!bOk)
		{
			AddError(FString::Printf(TEXT("%s does not resolve (%s)"), *Id, SSXAudioDiag::ResultName(A->MissReason(Id))));
		}
		Resolved += bOk ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d / %d table ids resolve"), Resolved, Ids.Num() - NotInBank.Num()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioAuditSpatialTest, "SSX.Audio.Audit.Spatial", AuditFlags)

bool FSSXAudioAuditSpatialTest::RunTest(const FString& Parameters)
{
	USSXAudioSubsystem* A = MakeAuditAudio();
	// 0x20AAB8
	USoundAttenuation* F = A->GameFalloff(SSXAudioFalloff::TrackSoundRadiusM);
	TestTrue(TEXT("falloff attenuates"), F && F->Attenuation.bAttenuate);
	if (F)
	{
		TestEqual(TEXT("falloff distance 30 m"), F->Attenuation.FalloffDistance, 3000.f);
		TestEqual(TEXT("inner radius 0.5 m"), F->Attenuation.AttenuationShapeExtents.X, 50.0);
		const FRichCurve* C = F->Attenuation.CustomAttenuationCurve.GetRichCurveConst();
		TestEqual(TEXT("curve(0) = 1"), C->Eval(0.f), 1.f, 1e-4f);
		TestEqual(TEXT("curve(0.5) = 0.25"), C->Eval(0.5f), 0.25f, 1e-3f);
		TestEqual(TEXT("curve(1) = 0"), C->Eval(1.f), 0.f, 1e-4f);
		TestTrue(TEXT("custom distance model"), F->Attenuation.DistanceAlgorithm == EAttenuationDistanceModel::Custom);
	}
	TestTrue(TEXT("op 8 radius = FUN_00226DD8(30, -1)"), SSXAudioFalloff::TrackSoundRadiusM == 30.0);
	TestTrue(TEXT("same radius -> same attenuation"), A->GameFalloff(30.0) == F);

	SSXAudioDiag::Reset();
	const FVector P(100.f, 0.f, 0.f);
	A->PlayZbxsfxAt(90, P);
	A->bAnimEventSounds = true;
	A->OnAnimEvent(0xD0, 0, P, true);
	A->PlayGameSound(TEXT("tracksound"), TEXT("sfx/garibaldi1/82"), 1.f, &P, SSXAudioFalloff::TrackSoundRadiusM);
	A->PlayZbxsfx(119);
	A->PlayRaceUISound(SSXUISound::ERace::Navigate);
	struct FExpect { const TCHAR* System; bool b3D; bool bUI; };
	const FExpect Expect[] = { { TEXT("zbxsfx3d"), true, false }, { TEXT("animevent"), true, false }, { TEXT("tracksound"), true, false },
		{ TEXT("zbxsfx"), false, false }, { TEXT("raceui"), false, true } };
	for (const FExpect& E : Expect)
	{
		const SSXAudioDiag::FRecord* R = SSXAudioDiag::Records().FindByPredicate([&](const SSXAudioDiag::FRecord& X) { return X.System == E.System; });
		if (!TestNotNull(*FString::Printf(TEXT("%s noted"), E.System), R))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 3D"), E.System), R->b3D, E.b3D);
		TestEqual(*FString::Printf(TEXT("%s UI"), E.System), R->bUI, E.bUI);
		if (E.b3D)
		{
			TestEqual(*FString::Printf(TEXT("%s radius"), E.System), R->RadiusM, 30.0);
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioAuditPauseTest, "SSX.Audio.Audit.PauseClass", AuditFlags)

bool FSSXAudioAuditPauseTest::RunTest(const FString& Parameters)
{
	UAudioComponent* C = NewObject<UAudioComponent>();
	C->bIsUISound = true;
	USSXAudioSubsystem::MakeGameSound(C);
	TestFalse(TEXT("a game sound is not a UI sound (paused with the world)"), C->bIsUISound);
	SSXAudioDiag::Reset();
	USSXAudioSubsystem* A = MakeAuditAudio();
	A->PlayFeSound(1);
	const SSXAudioDiag::FRecord* R = SSXAudioDiag::Records().FindByPredicate([](const SSXAudioDiag::FRecord& X) { return X.System == TEXT("fe"); });
	TestTrue(TEXT("FE sounds stay UI sounds"), !R || R->bUI);
	A->ExecuteTrickyCommands([]() { return 0u; });
	A->GetTricky().Out.Add({ SSXTrickyAudio::ECmd::TrickyBank, 0, 0.0 });
	A->ExecuteTrickyCommands([]() { return 0u; });
	R = SSXAudioDiag::Records().FindByPredicate([](const SSXAudioDiag::FRecord& X) { return X.System == TEXT("tricky"); });
	if (TestNotNull(TEXT("tricky.bnk #0 requested"), R))
	{
		TestFalse(TEXT("Tricky cue is a game sound"), R->bUI);
		TestEqual(TEXT("Tricky cue id"), R->Id, FString(TEXT("sfx/tricky/00")));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioAuditBoostTest, "SSX.Audio.Audit.Boost", AuditFlags)

bool FSSXAudioAuditBoostTest::RunTest(const FString& Parameters)
{
	FRiderSim Sim;
	TSharedRef<FGroundContext> Ctx = RegisterGroundStates(Sim, FGroundConfig());
	FRiderState& S = Sim.State;
	S.BoostMeter = 1.0;
	SSXGround::BoostHandler(Sim, true, true);
	TestTrue(TEXT("boosting in Cruise"), S.BoostStrength > 0.0);
	const int32 Starts = S.BoostStartCount;
	SSXGround::FCruiseControl Cruise(Ctx);
	Cruise.Exit(Sim, ERiderControl::NaturalAir);
	TestEqual(TEXT("Cruise exit (take-off) releases the boost (0x1033C0 Boost(0, 0))"), S.BoostStrength, 0.0);

	// 0x104E68
	FRiderInputWord In;
	In.bBoostHeld = true;
	Ctx->Absorb = 1.0;
	SSXGround::FLandNormalControl Land(Ctx);
	Land.Update(Sim, In);
	TestEqual(TEXT("LandNormal: no boost start"), S.BoostStartCount, Starts);
	TestEqual(TEXT("LandNormal: still released"), S.BoostStrength, 0.0);

	// 0x1055A8
	SSXGround::BoostHandler(Sim, true, false);
	TestTrue(TEXT("boost restarted"), S.BoostStrength > 0.0);
	SSXGround::FPrewindControl Prewind(Ctx);
	Prewind.Exit(Sim, ERiderControl::Jump);
	TestEqual(TEXT("Prewind exit releases the boost"), S.BoostStrength, 0.0);
	return !HasAnyErrors();
}

#endif
