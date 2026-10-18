#include "Audio/SSXBoardAudio.h"
#include "Audio/SSXBoardSoundComponent.h"
#include "Sim/Ground/GroundControls.h"
#include "Sim/RiderSim.h"
#include "Audio/SSXSnowSound.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioBoardTest, "SSX.Audio.Board",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAudioBoardTest::RunTest(const FString& Parameters)
{
	namespace BA = SSXBoardAudio;
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"));
	FString Text;
	TSharedPtr<FJsonObject> Ref;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("board_audio_reference.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Ref) || !Ref.IsValid())
	{
		AddError(TEXT("SimData/board_audio_reference.json missing (run tools/make_board_audio_reference.py)"));
		return false;
	}
	FSSXSnowPrograms P;
	if (!TestTrue(TEXT("SNOW.INF loads"), P.LoadDefault()))
	{
		return false;
	}
	auto Report = [this](const FString& Msg) { AddInfo(Msg); UE_LOG(LogTemp, Display, TEXT("[boardaudio] %s"), *Msg); };

	{
		const TSharedPtr<FJsonObject> Progs = Ref->GetObjectField(TEXT("programs"));
		int32 Same = 0, Total = 0;
		for (int32 S = 0; S < int32(ESSXSnowSurface::Count); ++S)
		{
			for (int32 M = 0; M < int32(ESSXSnowMode::Count); ++M)
			{
				const FString Key = FString::Printf(TEXT("%s/%s"), FSSXSnowPrograms::SurfaceName(ESSXSnowSurface(S)), FSSXSnowPrograms::ModeName(ESSXSnowMode(M)));
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				++Total;
				if (!Progs->TryGetArrayField(Key, Arr)) { AddError(Key + TEXT(" missing")); continue; }
				TArray<int32> Model;
				for (const TSharedPtr<FJsonValue>& V : *Arr) { Model.Add(int32(V->AsNumber())); }
				const bool bSame = Model == P.Program(ESSXSnowSurface(S), ESSXSnowMode(M));
				Same += bSame ? 1 : 0;
				if (!bSame) { AddError(Key + TEXT(": bytecode differs from the model")); }
			}
		}
		Report(FString::Printf(TEXT("SNOW.INF programs == model: %d / %d"), Same, Total));
	}

	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Dir, TEXT("board_audio_replay.csv"))) || Lines.Num() < 2)
		{
			AddError(TEXT("SimData/board_audio_replay.csv missing"));
		}
		else
		{
			int64 Rows = 0, InOk = 0, IdxOk = 0, VoiceOk = 0, Bad = 0;
			int64 Nonzero[4] = {};
			TArray<FString> F;
			for (int32 I = 1; I < Lines.Num(); ++I)
			{
				Lines[I].ParseIntoArray(F, TEXT(","));
				if (F.Num() < 29) { continue; }
				auto Fl = [&F](int32 K) { return FCString::Atof(*F[K]); };
				auto In_ = [&F](int32 K) { return FCString::Atoi(*F[K]); };
				const int32 Motion = In_(4), Surface = In_(5), Rail = In_(6);
				const FSSXSnowInputs In = BA::Inputs(FVector3f(Fl(7), Fl(8), Fl(9)), FVector3f(Fl(10), Fl(11), Fl(12)), Fl(13), Fl(14));
				const bool bIn = In.Slip == In_(15) && In.Dig == In_(16) && In.Lean == In_(17) && In.Board == In_(18) && In.Bend == In_(19);
				const ESSXSnowSurface Idx = BA::SnowIndex(Surface, Motion, Rail);
				const bool bIdx = int32(Idx) == In_(20);
				bool bVoices = true;
				const BA::EVoice Roles[4] = { BA::EVoice::Speed, BA::EVoice::Glide, BA::EVoice::Carve, BA::EVoice::AIGlide };
				for (int32 V = 0; V < 4; ++V)
				{
					const FSSXSnowResult R = BA::Callback(Roles[V], P, Motion, ESSXSnowSurface(In_(20)), FSSXSnowInputs{ In_(15), In_(16), In_(17), In_(18), In_(19) });
					bVoices &= R.Volume == In_(21 + 2 * V) && R.Bend == In_(22 + 2 * V);
					Nonzero[V] += R.Volume != 0 ? 1 : 0;
				}
				++Rows;
				InOk += bIn; IdxOk += bIdx; VoiceOk += bVoices;
				if (!(bIn && bIdx && bVoices) && ++Bad <= 5)
				{
					AddError(FString::Printf(TEXT("replay row %d: %s"), I, *Lines[I]));
				}
			}
			Report(FString::Printf(TEXT("replay (live rides, 6 riders): %lld rows; inputs == model %lld, SNOW index %lld, 4 voice callbacks (vol+bend) %lld; non-zero volume rows speed %lld glide %lld carve %lld AI %lld"),
				Rows, InOk, IdxOk, VoiceOk, Nonzero[0], Nonzero[1], Nonzero[2], Nonzero[3]));
			TestTrue(TEXT("replay rows"), Rows > 90000);
			TestEqual(TEXT("replay inputs"), InOk, Rows);
			TestEqual(TEXT("replay SNOW index"), IdxOk, Rows);
			TestEqual(TEXT("replay voices"), VoiceOk, Rows);
		}
	}

	{
		int32 N = 0, RoleOk = 0, RamVol = 0, ModelVol = 0;
		for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("ram_voices")))
		{
			const TSharedPtr<FJsonObject> O = JV->AsObject();
			const FString Role = O->GetStringField(TEXT("role"));
			const bool bPlayer = O->GetIntegerField(TEXT("player")) != 0;
			const BA::EVoice V = Role == TEXT("speed") ? BA::EVoice::Speed : Role == TEXT("glide") ? BA::EVoice::Glide
				: Role == TEXT("carve") ? BA::EVoice::Carve : BA::EVoice::AIGlide;
			const bool bRoleForOwner = bPlayer ? V != BA::EVoice::AIGlide : V == BA::EVoice::AIGlide;
			const double R = bPlayer ? BA::PlayerVoiceRadius : BA::AIVoiceRadius;
			RoleOk += (bRoleForOwner && O->GetNumberField(TEXT("r88")) == R && O->GetNumberField(TEXT("r8c")) == R) ? 1 : 0;
			auto Vec = [&O](const TCHAR* K) { const TArray<TSharedPtr<FJsonValue>>& A = O->GetArrayField(K);
				return FVector3f(float(A[0]->AsNumber()), float(A[1]->AsNumber()), float(A[2]->AsNumber())); };
			const FSSXSnowInputs In = BA::Inputs(Vec(TEXT("cf")), Vec(TEXT("vel")), float(O->GetNumberField(TEXT("drag"))), float(O->GetNumberField(TEXT("steer"))));
			const int32 Motion = O->GetIntegerField(TEXT("motion"));
			const ESSXSnowSurface Idx = BA::SnowIndex(O->GetIntegerField(TEXT("surface")), Motion, O->GetIntegerField(TEXT("rail_surface")));
			const int32 Vol = BA::Callback(V, P, Motion, Idx, In).Volume;
			RamVol += Vol == O->GetIntegerField(TEXT("ram_vol")) ? 1 : 0;
			ModelVol += Vol == O->GetIntegerField(TEXT("model_vol")) ? 1 : 0;
			++N;
		}
		Report(FString::Printf(TEXT("RAM voices: %d; roles + radii (player 40 m speed/glide/carve, AI 50 m AI glide) %d; C++ volume == RAM record +0x64 on %d, == model on %d"),
			N, RoleOk, RamVol, ModelVol));
		TestEqual(TEXT("16 RAM voices"), N, 16);
		TestEqual(TEXT("roles and radii"), RoleOk, N);
		TestEqual(TEXT("volume == RAM on the 12 the model matches"), RamVol, 12);
		TestEqual(TEXT("volume == model"), ModelVol, N);
	}

	{
		int32 Total = 0, Ok = 0;
		for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("curves")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
			++Total; Ok += BA::Curve(int32(A[0]->AsNumber()), int32(A[1]->AsNumber())) == int32(A[2]->AsNumber()) ? 1 : 0;
		}
		for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("falloff")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
			++Total; Ok += BA::Falloff(int32(A[0]->AsNumber()), A[1]->AsNumber(), A[2]->AsNumber()) == int32(A[3]->AsNumber()) ? 1 : 0;
		}
		for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("doppler")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
			++Total; Ok += FMath::IsNearlyEqual(BA::Doppler(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()), A[3]->AsNumber(), 1e-12) ? 1 : 0;
		}
		for (const TSharedPtr<FJsonValue>& JV : Ref->GetArrayField(TEXT("big_air")))
		{
			const TArray<TSharedPtr<FJsonValue>>& A = JV->AsArray();
			int32 Byte = 0;
			double Wind = 0.0;
			const bool bActive = BA::BigAir(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber() != 0.0, Byte, Wind);
			const int32 Exp = int32(A[4]->AsNumber());
			++Total; Ok += (Exp < 0 ? !bActive && Byte == 127 : bActive && Byte == Exp && FMath::IsNearlyEqual(Wind, A[5]->AsNumber(), 1e-12)) ? 1 : 0;
		}
		Report(FString::Printf(TEXT("curves / falloff / Doppler / big-air samples == model: %d / %d"), Ok, Total));
		TestEqual(TEXT("model samples"), Ok, Total);

		TestEqual(TEXT("crash: rand 0 -> #48"), BA::CrashSound(0, 1), 48);
		TestEqual(TEXT("crash formula edge: rand 0x7FFF -> 51"), BA::CrashSound(0x7FFF, 1), 51);
		TestEqual(TEXT("crash: rand 0x7FFE -> #50"), BA::CrashSound(0x7FFE, 1), 50);
		TestEqual(TEXT("crash on surface 3 / 4 -> #64"), BA::CrashSound(1234, 4), 64);
		TestEqual(TEXT("bump #90 / #91"), BA::BumpSound(0) * 1000 + BA::BumpSound(0x7FFE), 90 * 1000 + 91);
		TestEqual(TEXT("boost tiers"), BA::BoostSound(0.7) * 10000 + BA::BoostSound(0.5) * 100 + BA::BoostSound(0.2) - 1200000, 120 * 10000 + 121 * 100 + 122 - 1200000);
		TestEqual(TEXT("voices: player glide on METAL = zboard #36, carve #35, AI glide #38, speed #0"),
			BA::VoiceSound(BA::EVoice::Glide, ESSXSnowSurface::Metal) * 1000000 + BA::VoiceSound(BA::EVoice::Carve, ESSXSnowSurface::Metal) * 1000
			+ BA::VoiceSound(BA::EVoice::AIGlide, ESSXSnowSurface::Metal) + BA::VoiceSound(BA::EVoice::Speed, ESSXSnowSurface::Metal), 36035038);
		TestEqual(TEXT("rail: index from the last caught rail"), int32(BA::SnowIndex(1, BA::MotionRail, 13)), int32(ESSXSnowSurface::Metal));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXAudioBoardTriggersTest, "SSX.Audio.BoardTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXAudioBoardTriggersTest::RunTest(const FString& Parameters)
{
	namespace BA = SSXBoardAudio;
	TestEqual(TEXT("burst tiers #72/#73/#74"), BA::BoostBurstSound(0.9) * 10000 + BA::BoostBurstSound(0.5) * 100 + BA::BoostBurstSound(0.1), 72 * 10000 + 73 * 100 + 74);
	TestTrue(TEXT("crowd levels: <=0 neutral, <0.25 0, <0.65 1, else 2"), BA::CrowdLevel(0.0) == -1 && BA::CrowdLevel(0.2) == 0
		&& BA::CrowdLevel(0.25) == 1 && BA::CrowdLevel(0.649) == 1 && BA::CrowdLevel(0.65) == 2);
	TestTrue(TEXT("prop hit volume clamp(curve2, 0, 127)"), BA::PropHitVolume(1.0e6) >= 0 && BA::PropHitVolume(1.0e6) <= 127);
	{
		// 0x11D038
		FRiderSim Sim;
		Sim.State.BoostMeter = 0.0;
		SSXGround::BoostHandler(Sim, true, true);
		TestEqual(TEXT("press with an empty meter"), Sim.State.BoostEmptyPressCount, 1);
		SSXGround::BoostHandler(Sim, true, false);
		TestEqual(TEXT("held (no edge) on empty: no #108"), Sim.State.BoostEmptyPressCount, 1);
		Sim.State.BoostMeter = 0.5;
		SSXGround::BoostHandler(Sim, true, true);
		TestTrue(TEXT("start by a press"), Sim.State.BoostStartCount == 1 && Sim.State.bBoostStartPressed);
		SSXGround::BoostHandler(Sim, true, false);
		TestEqual(TEXT("continuing boost is not a new start"), Sim.State.BoostStartCount, 1);
		SSXGround::BoostHandler(Sim, false, false);
		SSXGround::BoostHandler(Sim, true, false);
		TestTrue(TEXT("held start without the edge: no burst"), Sim.State.BoostStartCount == 2 && !Sim.State.bBoostStartPressed);
	}
	USSXBoardSoundComponent* C = NewObject<USSXBoardSoundComponent>();
	FRiderState S;
	S.Frame = 0;
	C->UpdateFromRider(S, 1.f / 60.f);
	auto Step = [&]() { ++S.Frame; C->OneShotLog.Reset(); C->UpdateFromRider(S, 1.f / 60.f); };
	S.BoostMeter = 0.9; S.BoostStrength = 1.0; S.BoostStartCount = 1; S.bBoostStartPressed = true;
	Step();
	TestEqual(TEXT("press start: burst #72 + held #120"), C->OneShotLog, TArray<FString>({ BA::ZbxsfxId(72), BA::ZbxsfxId(120) }));
	S.BoostStrength = 0.0; S.BoostMeter = 0.0; S.BoostEmptyPressCount = 1;
	Step();
	TestEqual(TEXT("empty-meter press: #108"), C->OneShotLog, TArray<FString>({ BA::ZbxsfxId(108) }));
	S.PropHitCount = 1; S.PropHitInstance = 5; S.PropHitSpeed = 1500.0;
	Step();
	TestEqual(TEXT("prop without a sound list: silent"), C->OneShotLog.Num(), 0);
	USSXBoardSoundComponent::PropSoundRing.Reset();
	USSXBoardSoundComponent::PropSoundLookup = [](int32 I, FString& Id) { Id = I == 5 || I == 6 ? TEXT("sfx/test/07") : FString(); return I == 5 || I == 6; };
	S.PropHitCount = 2; S.Frame += 60;
	Step();
	TestEqual(TEXT("prop hit: the instance's hit sound"), C->OneShotLog, TArray<FString>({ TEXT("sfx/test/07") }));
	S.PropHitCount = 3;
	Step();
	TestEqual(TEXT("ring hold 50 blocks the same instance"), C->OneShotLog.Num(), 0);
	S.PropHitCount = 4; S.PropHitInstance = 6;
	Step();
	TestEqual(TEXT("the ring is keyed by the instance: another prop plays"), C->OneShotLog.Num(), 1);
	// 0x2165C8
	TestTrue(TEXT("op 8 on the prop-hit instance: held for 50 ms"), USSXBoardSoundComponent::IsInstanceSoundHeld(6, S.Frame + 2)
		&& !USSXBoardSoundComponent::IsInstanceSoundHeld(6, S.Frame + 3) && !USSXBoardSoundComponent::IsInstanceSoundHeld(7, S.Frame));
	// 0x219FC0
	TestTrue(TEXT("op 16: 1 / 5 / 9.9 / 10 s -> 0x71 / 0x72 / 0x72 / 0x73"), BA::TimeBonusSound(1.0) == 0x71 && BA::TimeBonusSound(5.0) == 0x72
		&& BA::TimeBonusSound(9.9) == 0x72 && BA::TimeBonusSound(10.0) == 0x73);
	USSXBoardSoundComponent::PropSoundLookup = nullptr;
	USSXBoardSoundComponent::PropSoundRing.Reset();
	S.CrowdCueCount = 1; S.CrowdCueX = 0.5; S.bCrowdCueBail = false; Step();
	S.CrowdCueCount = 2; S.CrowdCueX = 0.9; S.bCrowdCueBail = true; Step();
	S.CrowdCueCount = 3; S.CrowdCueX = 0.0; S.bCrowdCueBail = false; Step();
	TestEqual(TEXT("crowd: cheer1, groan2, neutral"), C->CrowdLog, TArray<FString>({ TEXT("cheer1"), TEXT("groan2"), TEXT("neutral") }));
	return !HasAnyErrors();
}

#endif
