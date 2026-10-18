#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/Air/TrickName.h"
#include "Sim/RiderSim.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXHudData.h"
#include "UI/SSXHudOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AnimTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> LoadRef()
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FSSXHudData::DefaultDir(), TEXT("hud_anim_ref.json"))))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
		}
		return Root;
	}

	struct FAnimAssets
	{
		FSSXHudData Data;
		FSSXHudSheet Sheet;
		FSSXFeAssets Assets;
		bool Make()
		{
			if (!Data.Load() || !Sheet.Load(FPaths::Combine(FSSXHudData::DefaultDir(), TEXT("hud_sprites.json")), false)) return false;
			Assets.HudSprites = &Sheet.Sprites;
			for (const TPair<FName, FString>& P : Sheet.PagePaths) Assets.Pages.Add(P.Key, UTexture2D::CreateTransient(1, 1));
			Assets.Title = FSSXBitmapFont::Load(FPaths::Combine(FSSXHudData::DefaultDir(), TEXT("../frontend/fonts/TITLE.json")),
				UTexture2D::CreateTransient(1, 1));
			if (!Assets.Title.IsValid()) return false;
			Assets.Title->ScaleX = SSXFe::TitleScaleX;
			Assets.Title->ScaleY = SSXFe::TitleScaleY;
			return true;
		}
	};

	FSSXHudInput OneRider(int32 Mode = 4)
	{
		FSSXHudInput In;
		In.Mode = Mode;
		In.RacerCount = 6;
		In.Players.AddDefaulted();
		return In;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXHudAnimTest, "SSX.HUD.Anim", AnimTestFlags)
bool FSSXHudAnimTest::RunTest(const FString& Parameters)
{
	FAnimAssets A;
	if (!A.Make())
	{
		AddError(TEXT("HUD data / sprites / TITLE font missing (tools/export_hud_data.py, tools/sfn.py)"));
		return false;
	}
	const TSharedPtr<FJsonObject> Ref = LoadRef();
	if (!Ref.IsValid()) SSXTest::Skip(*this, TEXT("no converted/hud/hud_anim_ref.json (tools/export_hud_anim_ref.py): model / RAM series skipped"));

	{
		FSSXHudOverlay H;
		H.Init(&A.Data, nullptr);
		H.Activate(4, 0, 6, 0, nullptr, 0.0);
		FSSXHudInput In = OneRider();
		TArray<FVector3f> Orb;
		if (Ref.IsValid())
		{
			for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("orb")))
			{
				const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
				Orb.Add(FVector3f((float)R[0]->AsNumber(), (float)R[1]->AsNumber(), (float)R[2]->AsNumber()));
			}
		}
		else
		{
			Orb = { { 20.f, 1.f, 0.f } };
		}
		double T = 20.0;
		float MaxDw = 0.f, MaxDb = 0.f, MinW = 1.f, MaxB = 0.f;
		int32 Frames = 0, WorstFrame = -1;
		const TArray<int32> Spot = { 0, 41, 42, 500 };
		const TArray<FVector2f> SpotWant = { { 1.f, 0.f }, { 0.22142834f, 39.047622f }, { 0.22499975f, 40.f }, { 0.37435131f, 24.179619f } };
		while (T > 0.0)
		{
			In.Players[0].TrickyTimer = Ref.IsValid() && Orb.IsValidIndex(Frames) ? (double)Orb[Frames].X : T;
			H.Update(In);
			const FSSXHudOverlay::FPlayerState& S = H.GetPlayer(0);
			if (Orb.IsValidIndex(Frames) && Ref.IsValid())
			{
				if (FMath::Abs(S.OrbW - Orb[Frames].Y) > MaxDw) { MaxDw = FMath::Abs(S.OrbW - Orb[Frames].Y); WorstFrame = Frames; }
				MaxDb = FMath::Max(MaxDb, FMath::Abs(S.Bounce - Orb[Frames].Z));
			}
			const int32 Si = Spot.Find(Frames);
			if (Si != INDEX_NONE)
			{
				TestTrue(FString::Printf(TEXT("orb frame %d: w %.6f (model %.6f), bounce %.5f (model %.5f)"), Frames, S.OrbW, SpotWant[Si].X,
					S.Bounce, SpotWant[Si].Y), FMath::IsNearlyEqual(S.OrbW, SpotWant[Si].X, 1e-4f) && FMath::IsNearlyEqual(S.Bounce, SpotWant[Si].Y, 1e-3f));
			}
			MinW = FMath::Min(MinW, S.OrbW);
			MaxB = FMath::Max(MaxB, S.Bounce);
			T = FRiderSim::TrickyTimerStep(T, false);
			++Frames;
		}
		AddInfo(FString::Printf(TEXT("orb: %d frames, max |dw| %.2e (frame %d), max |dbounce| %.2e vs the model; min w %.3f, peak bounce %.3f"),
			Frames, MaxDw, WorstFrame, MaxDb, MinW, MaxB));
		TestEqual(TEXT("1200 frames (20 s)"), Frames, 1200);
		if (Ref.IsValid()) TestTrue(TEXT("orb w / bounce match the model over the whole period"), MaxDw < 1e-3f && MaxDb < 1e-3f);
		TestTrue(TEXT("the coin face goes negative (mirrored), never |phase|"), MinW < -0.99f);
		TestTrue(TEXT("rises to 40 px"), FMath::IsNearlyEqual(MaxB, 40.f, 1e-3f));
		In.Players[0].TrickyTimer = 0.0;
		H.Update(In);
		TestTrue(TEXT("idle: phase reset to 1, bounce 0"), H.GetPlayer(0).Phase == 1.f && H.GetPlayer(0).Bounce == 0.f);
	}
	TestTrue(TEXT("fold: 1 at 1, 0 at 0, -1 at -1, 0 at -2, 1 at -3"), FSSXHudOverlay::OrbFold(1.f) == 1.f && FSSXHudOverlay::OrbFold(0.f) == 0.f
		&& FSSXHudOverlay::OrbFold(-1.f) == -1.f && FSSXHudOverlay::OrbFold(-2.f) == 0.f && FSSXHudOverlay::OrbFold(-3.f) == 1.f
		&& FMath::IsNearlyEqual(FSSXHudOverlay::OrbFold(-1.5f), -0.5f) && FMath::IsNearlyEqual(FSSXHudOverlay::OrbFold(-2.5f), 0.5f));

	{
		TArray<FSSXHudBoostSeg> T = A.Data.Boost;
		for (int32 K = 1; K < T.Num(); ++K) { T[K].Sprite = 0; T[K].Dy = 0.f; }
		T[4].Sprite = 0x14; T[4].Dy = 216.f;
		FSSXHudData::BuildBoostTable(T);
		bool bSame = T.Num() == 14;
		for (int32 K = 0; bSame && K < 14; ++K) bSame = T[K].Sprite == A.Data.Boost[K].Sprite && T[K].Dy == A.Data.Boost[K].Dy;
		TestTrue(TEXT("segment table build rule = RAM table (sprites 0x14 x4, 0x13 x5, 0x12 x5; dy 288..34)"), bSame);
		if (Ref.IsValid())
		{
			for (const TSharedPtr<FJsonValue>& V : Ref->GetArrayField(TEXT("ram")))
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				const double Meter = O->GetNumberField(TEXT("meter"));
				FSSXHudOverlay H;
				H.Init(&A.Data, nullptr);
				H.Activate(4, 0, 6, 0, nullptr, 0.0);
				FSSXHudInput In = OneRider();
				In.Players[0].BoostMeter = Meter;
				for (int32 I = 0; I < 400; ++I) H.Update(In);
				const TArray<TSharedPtr<FJsonValue>>& Sc = O->GetArrayField(TEXT("scale"));
				const TArray<TSharedPtr<FJsonValue>>& Lt = O->GetArrayField(TEXT("lit"));
				float MaxD = 0.f;
				bool bLit = true;
				for (int32 K = 0; K < 14; ++K)
				{
					MaxD = FMath::Max(MaxD, FMath::Abs(H.GetPlayer(0).SegScale[K] - (float)Sc[K]->AsNumber()));
					bLit &= H.GetPlayer(0).SegLit[K] == (Lt[K]->AsNumber() != 0.0);
				}
				TestTrue(FString::Printf(TEXT("RAM %s: S[0x1F]=%.4f -> 14 scales (max |d| %.2e) and lit flags"), *O->GetStringField(TEXT("session")),
					H.GetPlayer(0).BoostShown, MaxD), MaxD < 1e-5f && bLit);
			}
		}
		FSSXHudOverlay H;
		H.Init(&A.Data, nullptr);
		H.Activate(4, 0, 6, 0, nullptr, 0.0);
		FSSXHudInput In = OneRider();
		In.Players[0].BoostMeter = 0.38;
		In.Players[0].TrickValue = 0.1 / 0.6786545;
		for (int32 I = 0; I < 400; ++I) H.Update(In);
		const float Want[14] = { 1.f, 1.f, 1.f, 1.f, 1.f, 0.456f, 0.776f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };
		float MaxD = 0.f;
		for (int32 K = 0; K < 14; ++K) MaxD = FMath::Max(MaxD, FMath::Abs(H.GetPlayer(0).SegScale[K] - Want[K]));
		TestTrue(FString::Printf(TEXT("pending preview: grey segment 6 grows to 0.776 (max |d| %.2e)"), MaxD), MaxD < 1e-3f && !H.GetPlayer(0).SegLit[6]);
		In.Players[0].Messages = { { SSXHudMsg::Points2, 100, 2.5f, 0.f, 100 }, { SSXHudMsg::Points3, 100, 2.5f, 0.3125f, 100 } };
		H.Update(In);
		TestTrue(TEXT("pulse = 1 + pop(1/8, 1.4) = 1.6125 from the last entry"), FMath::IsNearlyEqual(H.GetPlayer(0).Pulse, 1.6125f, 1e-4f));
	}

	{
		TestTrue(TEXT("tier pop: 0 / 0.6 / 0.3 / 0 at e = 0, 18, 36, 37 (sx 0.75)"), FSSXHudOverlay::TierPop(0.f, 0.75f) == 0.f
			&& FMath::IsNearlyEqual(FSSXHudOverlay::TierPop(18.f, 0.75f), 0.6f) && FMath::IsNearlyEqual(FSSXHudOverlay::TierPop(36.f, 0.75f), 0.3f)
			&& FSSXHudOverlay::TierPop(37.f, 0.75f) == 0.f);
		FSSXHudOverlay H;
		H.Init(&A.Data, nullptr);
		H.Activate(4, 0, 6, 0, nullptr, 0.0);
		FSSXHudInput In = OneRider();
		In.Players[0].TrickValue = 500.0 / (10000.0 * 0.6786545);
		In.Players[0].PointsMultiplier = 2.0;
		TArray<float> Pops;
		for (int32 I = 0; I < 40; ++I) { H.Update(In); Pops.Add(H.GetPlayer(0).PointsPop); }
		TestEqual(TEXT("S[2] = unmultiplied 500"), H.GetPlayer(0).PointsPending, 500.f);
		TestEqual(TEXT("S[1] rolls to the multiplied 1000 (tick only)"), H.GetPlayer(0).PointsShown, 1000.f);
		TestTrue(FString::Printf(TEXT("pop on first appearance: %.3f / %.3f / %.3f / %.3f at frames 0, 18, 36, 37"), Pops[0], Pops[18], Pops[36], Pops[37]),
			Pops[0] == 0.f && FMath::IsNearlyEqual(Pops[18], 0.6f, 1e-5f) && FMath::IsNearlyEqual(Pops[36], 0.3f, 1e-5f) && Pops[37] == 0.f);
		TestEqual(TEXT("10 ticks over 0 -> 1000 (every new 100)"), H.ConsumeTicks(), 10);

		FSSXFeDrawList D(&A.Assets);
		H.Draw(D, In);
		const float S = 0.75f;
		const FSSXSprite* Mid = A.Sheet.Sprites.Find(0x3A);
		const FSSXFeDrawCmd* Plate = D.Cmds.FindByPredicate([&](const FSSXFeDrawCmd& C)
			{ return Mid && C.Kind == FSSXFeDrawCmd::EKind::Quad && FMath::IsNearlyEqual(C.Pos.Y, 80.f - 7.f * S, 1e-3f) && FMath::IsNearlyEqual(C.Size.Y, 38.f * S, 1e-3f); });
		TestTrue(TEXT("plate: top 80 - 7*s = 74.75, height (24 + 14)*s = 28.5"), Plate != nullptr);
		if (Plate)
		{
			TestTrue(TEXT("plate tinted by the tier colour (tier 0)"), Plate->Color != SSXFe::White);
		}
		const FSSXBitmapGlyph* G5 = A.Assets.Title->Find('5');
		const FSSXFeDrawCmd* Dig = D.Cmds.FindByPredicate([&](const FSSXFeDrawCmd& C)
			{ return C.Kind == FSSXFeDrawCmd::EKind::Glyphs && C.Layer == 2 && G5 && FMath::IsNearlyEqual(C.Pos.Y, 80.f + G5->Offset.Y * S * 1.3f, 1e-3f); });
		TestTrue(TEXT("digits: top at 80, white, layer 2 (plate on 0)"), Dig && Dig->Color == SSXFe::White && Plate && Plate->Layer == 0);
		const FSSXFeDrawCmd* Shadow = Dig ? D.Cmds.FindByPredicate([&](const FSSXFeDrawCmd& C)
			{ return C.Kind == FSSXFeDrawCmd::EKind::Glyphs && C.Layer == 1 && C.Pos == Dig->Pos + FVector2f(2.f, 2.f); }) : nullptr;
		TestTrue(TEXT("black 2-px drop shadow"), Shadow && Shadow->Color.R == 0.f && Shadow->Color.A == Dig->Color.A);
		TestTrue(TEXT("the number drawn is 500, not the multiplied roll"), D.Texts.Contains(TEXT("500")) && !D.Texts.Contains(TEXT("1000")));
	}

	{
		FSSXHudOverlay H;
		H.Init(&A.Data, nullptr);
		H.Activate(4, 0, 6, 0, nullptr, 0.0);
		FSSXHudInput In = OneRider();
		In.RaceTimeSec = 106.98;
		In.Players[0].Speed = 1000.0;
		H.Update(In);
		FSSXFeDrawList D(&A.Assets);
		H.Draw(D, In);
		const FSSXBitmapGlyph* G1 = A.Assets.Title->Find('1');
		const float Sc = 0.9f;
		const float WantY = 460.f - 24.f * Sc + (G1 ? G1->Offset.Y : 0.f) * Sc * SSXFe::TitleScaleY;
		const float WantX = 26.f + (G1 ? G1->Offset.X : 0.f) * Sc * SSXFe::TitleScaleX;
		const FSSXFeDrawCmd* T1 = D.Cmds.FindByPredicate([&](const FSSXFeDrawCmd& C)
			{ return C.Kind == FSSXFeDrawCmd::EKind::Glyphs && C.Layer == 2 && FMath::IsNearlyEqual(C.Pos.X, WantX, 1e-3f); });
		TestTrue(FString::Printf(TEXT("race time '1:46.98' first glyph top %.2f (want %.2f = 460 - 21.6 + yoff*s*1.3)"), T1 ? T1->Pos.Y : -1.f, WantY),
			T1 && FMath::IsNearlyEqual(T1->Pos.Y, WantY, 1e-3f));
		const float UnitTop = 430.f - 24.f * 0.7f;
		bool bUnitAbove = true;
		for (const FSSXFeDrawCmd& C : D.Cmds)
		{
			if (C.Kind == FSSXFeDrawCmd::EKind::Glyphs && C.Pos.X >= 90.f && C.Pos.Y > 400.f && C.Pos.Y < 440.f && C.Pos.Y + 1e-3f < UnitTop) bUnitAbove = false;
		}
		TestTrue(TEXT("speed unit sits on y = 430 (not drawn from its top at 430, where it crowded the race time)"), bUnitAbove);
		const float Ink = D.TextInkWidth(SSXFe::EFont::Title, TEXT("1:46.98"), Sc);
		const float Adv = D.TextWidth(SSXFe::EFont::Title, TEXT("1:46.98"), Sc);
		AddInfo(FString::Printf(TEXT("'1:46.98' at 0.9: ink width %.2f vs summed advances %.2f"), Ink, Adv));
		TestTrue(TEXT("ink width differs from the advance sum"), !FMath::IsNearlyEqual(Ink, Adv, 0.01f));
		bool bChain = false;
		for (const FSSXFeDrawCmd& C : D.Cmds)
		{
			for (int32 Id = 0x0D; Id <= 0x11; ++Id)
			{
				const FSSXSprite* Sp = A.Sheet.Sprites.Find(Id);
				bChain |= Sp && C.Kind == FSSXFeDrawCmd::EKind::Quad && C.Size == FVector2f(Sp->W, Sp->H) && C.Pos.Y < 140.f && C.Pos.X > 540.f;
			}
		}
		TestFalse(TEXT("no chain-piece bar"), bChain);
	}

	// FUN_0019e888
	{
		int32 G = 0, S = 0, B = 0;
		TestTrue(TEXT("track 4: 275000 / 175000 / 125000"), FSSXHudOverlay::MedalThresholds(3, 4, G, S, B) && G == 275000 && S == 175000 && B == 125000);
		TestTrue(TEXT("track 6 mode 3: 800000 / 500000 / 250000"), FSSXHudOverlay::MedalThresholds(3, 6, G, S, B) && G == 800000);
		for (const FIntPoint MT : { FIntPoint(5, 6), FIntPoint(3, 7), FIntPoint(3, 9), FIntPoint(3, 10) })
		{
			TestFalse(TEXT("not written by FUN_0019e888"), FSSXHudOverlay::MedalThresholds(MT.X, MT.Y, G, S, B));
			TestTrue(TEXT("keeps the 0xDEADC0DE heap fill"), G == (int32)0xDEADC0DEu && S == G && B == G);
		}
		{
			FSSXHudOverlay F;
			F.Init(&A.Data, nullptr);
			F.Activate(5, 6, 1, 0, nullptr, 0.0);
			FSSXHudInput Fi = OneRider(5);
			F.Update(Fi);
			TestEqual(TEXT("fill threshold (signed): score 0 reaches bronze"), F.GetPlayer(0).MedalTier, 1);
		}
		FSSXHudOverlay H;
		H.Init(&A.Data, nullptr);
		H.Activate(3, 0, 1, 0, nullptr, 0.0);
		FSSXHudInput In = OneRider(3);
		In.Players[0].Score = 60000.0;
		H.Update(In);
		TestEqual(TEXT("big jump: bronze first"), H.GetPlayer(0).MedalTier, 1);
		for (int32 I = 0; I < 125; ++I) H.Update(In);
		TestEqual(TEXT("silver after the 2-s pop"), H.GetPlayer(0).MedalTier, 2);
	}

	// 0x1A1FB4
	{
		FSSXHudOverlay H;
		H.Init(&A.Data, nullptr);
		H.Activate(3, 0, 1, 0, nullptr, 0.0);
		FSSXHudInput In = OneRider(3);
		int32 Beeps = 0;
		for (double C = 12.5; C > 0.0; C -= 1.0 / 60.0) { In.Clock = C; H.Update(In); Beeps += H.ConsumeShowoffBeeps(); }
		TestEqual(TEXT("10 beeps (clock 11 -> 10 ... 2 -> 1)"), Beeps, 10);
		In.Clock = 30.0; H.Update(In); In.Clock = 10.5; H.Update(In);
		TestEqual(TEXT("no beep on a jump of more than one second"), H.ConsumeShowoffBeeps(), 0);
		In.Clock = 11.2; H.Update(In); In.Players[0].FinishResult = 9000; In.Clock = 10.9; H.Update(In);
		TestEqual(TEXT("no beep once finished"), H.ConsumeShowoffBeeps(), 0);
		FSSXHudOverlay R;
		R.Init(&A.Data, nullptr);
		R.Activate(4, 0, 1, 0, nullptr, 0.0);
		FSSXHudInput Ri = OneRider(4);
		Ri.Clock = 5.1; R.Update(Ri); Ri.Clock = 4.9; R.Update(Ri);
		TestEqual(TEXT("race mode: no beep"), R.ConsumeShowoffBeeps(), 0);
	}

	{
		FSSXHudOverlay H;
		H.Init(&A.Data, nullptr);
		H.Activate(3, 6, 1, 0, nullptr, 0.0);
		FSSXHudInput In = OneRider(3);
		In.RacerCount = 1;
		In.Players[0].bSwitch = true;
		H.Update(In);
		FSSXFeDrawList D(&A.Assets);
		H.Draw(D, In);
		const FSSXSprite* Sp = A.Sheet.Sprites.Find(0x18);
		const FSSXFeDrawCmd* Badge = Sp ? D.Cmds.FindByPredicate([&](const FSSXFeDrawCmd& C)
			{ return C.Kind == FSSXFeDrawCmd::EKind::Quad && C.Size == FVector2f(Sp->W, Sp->H); }) : nullptr;
		TestTrue(TEXT("showoff riding switch: badge 0x18 centred at (570, 450)"), Badge
			&& FMath::IsNearlyEqual(Badge->Pos.X + Badge->Size.X * 0.5f, 570.f, 1.f) && FMath::IsNearlyEqual(Badge->Pos.Y + Badge->Size.Y * 0.5f, 450.f, 1.f));
	}
	return true;
}

#endif
