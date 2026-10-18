#include "Camera/CameraDefs.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
	{
		double V = Default;
		return O->TryGetNumberField(Key, V) ? V : Default;
	}

	int32 Int(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32 Default)
	{
		double V = Default;
		return O->TryGetNumberField(Key, V) ? int32(V) : Default;
	}

	FSSXCameraDef ParseDef(const FString& Name, const TSharedPtr<FJsonObject>& O)
	{
		FSSXCameraDef D;
		D.Name = Name;
		D.PosAttachRider = Int(O, TEXT("pos_attach_rider"), D.PosAttachRider);
		D.PosRiderSlot = Int(O, TEXT("pos_rider_slot"), D.PosRiderSlot);
		D.OrbitLagEnable = Int(O, TEXT("orbit_lag_enable"), D.OrbitLagEnable);
		D.OrbitLagDiv = Num(O, TEXT("orbit_lag_div"), D.OrbitLagDiv);
		D.PosSmoothEnable = Int(O, TEXT("pos_smooth_enable"), D.PosSmoothEnable);
		D.PosSmoothParam = Int(O, TEXT("pos_smooth_param"), D.PosSmoothParam);
		D.UnkView40Enable = Int(O, TEXT("unk_view40_enable"), D.UnkView40Enable);
		D.UnkView44Value = Num(O, TEXT("unk_view44_value"), D.UnkView44Value);
		D.AnchorUseBone = Int(O, TEXT("anchor_use_bone"), D.AnchorUseBone);
		D.BoneIndex = Int(O, TEXT("bone_index"), D.BoneIndex);
		D.StageArea = Int(O, TEXT("stage_area"), D.StageArea);
		D.Base = FVector(Num(O, TEXT("base_x"), 0.0), Num(O, TEXT("base_y"), 0.0), Num(O, TEXT("base_z"), 0.0));
		D.OrbitEnable = Int(O, TEXT("orbit_enable"), D.OrbitEnable);
		D.OrbitDist = Num(O, TEXT("orbit_dist"), D.OrbitDist);
		D.OrbitDistBoost = Num(O, TEXT("orbit_dist_boost"), D.OrbitDistBoost);
		D.YawFromHeading = Int(O, TEXT("yaw_from_heading"), D.YawFromHeading);
		D.YawFromBone = Int(O, TEXT("yaw_from_bone"), D.YawFromBone);
		D.YawOffset = Num(O, TEXT("yaw_offset"), D.YawOffset);
		D.YawGoofyFlip = Int(O, TEXT("yaw_goofy_flip"), D.YawGoofyFlip);
		D.ElevFromVelocity = Int(O, TEXT("elev_from_velocity"), D.ElevFromVelocity);
		D.ElevFromBone = Int(O, TEXT("elev_from_bone"), D.ElevFromBone);
		D.ElevBase = Num(O, TEXT("elev_base"), D.ElevBase);
		D.ElevClampEnable = Int(O, TEXT("elev_clamp_enable"), D.ElevClampEnable);
		D.ElevMin = Num(O, TEXT("elev_min"), D.ElevMin);
		D.ElevMax = Num(O, TEXT("elev_max"), D.ElevMax);
		D.ElevDivDescending = Num(O, TEXT("elev_div_descending"), D.ElevDivDescending);
		D.ElevDivAscending = Num(O, TEXT("elev_div_ascending"), D.ElevDivAscending);
		D.TgtAttachRider = Int(O, TEXT("tgt_attach_rider"), D.TgtAttachRider);
		D.TgtRiderSlot = Int(O, TEXT("tgt_rider_slot"), D.TgtRiderSlot);
		D.TgtOff = FVector(Num(O, TEXT("tgt_off_x"), 0.0), Num(O, TEXT("tgt_off_y"), 0.0), Num(O, TEXT("tgt_off_z"), 0.0));
		D.TgtYaw = Num(O, TEXT("tgt_yaw"), D.TgtYaw);
		D.TgtPitch = Num(O, TEXT("tgt_pitch"), D.TgtPitch);
		D.TgtRoll = Num(O, TEXT("tgt_roll"), D.TgtRoll);
		D.Fov = Num(O, TEXT("fov"), D.Fov);
		D.AngleSmoothDiv = Num(O, TEXT("angle_smooth_div"), D.AngleSmoothDiv);
		D.RollDiv = Num(O, TEXT("roll_div"), D.RollDiv);
		D.RollFollowSlope = Int(O, TEXT("roll_follow_slope"), D.RollFollowSlope);
		D.TgtUseBone = Int(O, TEXT("tgt_use_bone"), D.TgtUseBone);
		D.TgtBoneIndex = Int(O, TEXT("tgt_bone_index"), D.TgtBoneIndex);
		D.AutoFov = Int(O, TEXT("auto_fov"), D.AutoFov);
		D.AutoFovRef = Num(O, TEXT("auto_fov_ref"), D.AutoFovRef);
		D.TgtReverse = Int(O, TEXT("tgt_reverse"), D.TgtReverse);
		D.NearClip = Num(O, TEXT("near_clip"), D.NearClip);
		D.StaticCut = Int(O, TEXT("static_cut"), D.StaticCut);
		return D;
	}
}

FString FSSXCameraDefSet::DefaultDir()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("cameras"));
}

bool FSSXCameraDefSet::AddJson(const FString& Text)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Cams = nullptr;
	if (Root->TryGetObjectField(TEXT("cameras"), Cams))
	{
		for (const auto& It : (*Cams)->Values)
		{
			const FString Key(It.Key);
			if (!Cameras.Contains(Key) && It.Value->Type == EJson::Object)
			{
				Cameras.Add(Key, ParseDef(Key, It.Value->AsObject()));
			}
		}
	}
	const TSharedPtr<FJsonObject>* Scr = nullptr;
	if (Root->TryGetObjectField(TEXT("scripts"), Scr))
	{
		for (const auto& It : (*Scr)->Values)
		{
			const FString Key(It.Key);
			if (Scripts.Contains(Key) || It.Value->Type != EJson::Array)
			{
				continue;
			}
			TArray<FSSXCameraScriptEvent>& Events = Scripts.Add(Key);
			for (const TSharedPtr<FJsonValue>& V : It.Value->AsArray())
			{
				const TSharedPtr<FJsonObject> E = V->AsObject();
				FSSXCameraScriptEvent Ev;
				Ev.Op = Int(E, TEXT("opcode"), 0);
				E->TryGetStringField(TEXT("name"), Ev.Name);
				Ev.A = Int(E, TEXT("a"), 0);
				Ev.B = Int(E, TEXT("b"), 0);
				Ev.C = Num(E, TEXT("c"), 0.0);
				Ev.T0 = Num(E, TEXT("t0"), 0.0);
				Ev.T1 = Num(E, TEXT("t1"), 0.0);
				Ev.Arg = Int(E, TEXT("arg"), 0);
				Events.Add(Ev);
			}
		}
	}
	TArray<FSSXCameraRegion>& FileRegions = RegionFiles.AddDefaulted_GetRef();
	const TSharedPtr<FJsonObject>* Regs = nullptr;
	if (Root->TryGetObjectField(TEXT("regions"), Regs))
	{
		for (const auto& It : (*Regs)->Values)
		{
			if (It.Value->Type != EJson::Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			FSSXCameraRegion R;
			R.Name = It.Key;
			R.Centre = FVector(Num(O, TEXT("centre_x"), 0.0), Num(O, TEXT("centre_y"), 0.0), Num(O, TEXT("centre_z"), 0.0));
			R.StageArea = Int(O, TEXT("stage_area"), 0);
			O->TryGetStringField(TEXT("script_on_enter"), R.ScriptOnEnter);
			O->TryGetStringField(TEXT("script_on_exit"), R.ScriptOnExit);
			O->TryGetStringField(TEXT("script_while_inside"), R.ScriptWhileInside);
			R.Radius = Num(O, TEXT("radius"), 0.0);
			R.RiderSlot = Int(O, TEXT("rider_slot"), 0);
			R.OnExit = Int(O, TEXT("on_exit"), 0);
			R.OnEnter = Int(O, TEXT("on_enter"), 0);
			R.WhileInside = Int(O, TEXT("while_inside"), 0);
			R.ActiveInRace = Int(O, TEXT("active_in_race"), 0);
			R.ActiveInReplay = Int(O, TEXT("active_in_replay"), 0);
			R.OnlyFinished = Int(O, TEXT("only_unfinished"), 0);
			R.NeedsLap = Int(O, TEXT("needs_lap"), 0);
			R.NeedsCond = Int(O, TEXT("needs_cond"), 0);
			R.Capsule = Int(O, TEXT("capsule"), 0);
			R.CapsuleLen = Num(O, TEXT("capsule_len"), 0.0);
			R.CapsuleYaw = Num(O, TEXT("capsule_yaw"), 0.0);
			R.CapsulePitch = Num(O, TEXT("capsule_pitch"), 0.0);
			R.NeedBigAir = Int(O, TEXT("need_big_air"), 0);
			R.MinAirDist = Num(O, TEXT("min_air_dist"), 0.0);
			R.NeedWipeout = Int(O, TEXT("need_wipeout"), 0);
			FileRegions.Add(R);
		}
	}
	const TSharedPtr<FJsonObject>* Anims = nullptr;
	if (Root->TryGetObjectField(TEXT("animations"), Anims))
	{
		for (const auto& It : (*Anims)->Values)
		{
			const FString Key(It.Key);
			if (Animations.Contains(Key) || It.Value->Type != EJson::Array)
			{
				continue;
			}
			TArray<int32>& Codes = Animations.Add(Key);
			for (const TSharedPtr<FJsonValue>& V : It.Value->AsArray())
			{
				Codes.Add(int32(V->AsNumber()));
			}
		}
	}
	const TSharedPtr<FJsonObject>* ScenesObj = nullptr;
	if (Root->TryGetObjectField(TEXT("scenes"), ScenesObj))
	{
		for (const auto& It : (*ScenesObj)->Values)
		{
			const FString Key(It.Key);
			if (Scenes.Contains(Key) || It.Value->Type != EJson::Array)
			{
				continue;
			}
			TArray<FSSXSceneActor>& Actors = Scenes.Add(Key);
			for (const TSharedPtr<FJsonValue>& V : It.Value->AsArray())
			{
				const TSharedPtr<FJsonObject> E = V->AsObject();
				FSSXSceneActor A;
				E->TryGetStringField(TEXT("name"), A.List);
				A.bEnabled = Int(E, TEXT("enabled"), 0) != 0;
				A.Role = Int(E, TEXT("role"), -1);
				A.bPlace = Int(E, TEXT("place"), 0) != 0;
				A.StageArea = Int(E, TEXT("path"), 0);
				const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
				if (E->TryGetArrayField(TEXT("pos"), P) && P->Num() == 3)
				{
					A.Pos = FVector((*P)[0]->AsNumber(), (*P)[1]->AsNumber(), (*P)[2]->AsNumber());
				}
				A.Yaw = Num(E, TEXT("yaw"), 0.0);
				Actors.Add(A);
			}
		}
	}
	return true;
}

bool FSSXCameraDefSet::LoadFiles(const TArray<FString>& Files, const FString& Dir)
{
	const FString Base = Dir.IsEmpty() ? DefaultDir() : Dir;
	bool bAll = true;
	for (const FString& F : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Base, F.ToLower() + TEXT(".json"))) || !AddJson(Text))
		{
			bAll = false;
		}
	}
	return bAll;
}

FString FSSXCameraDefSet::CourseFileForTrack(const FString& Track)
{
	// FUN_001783e8
	static const TMap<FString, FString> Map = {
		{ TEXT("gari"), TEXT("garibald") }, { TEXT("snow"), TEXT("snowdrea") }, { TEXT("elysium"), TEXT("elysium") },
		{ TEXT("mesa"), TEXT("mesablan") }, { TEXT("merquer"), TEXT("merqury") }, { TEXT("aloha"), TEXT("aloha") },
		{ TEXT("pipe"), TEXT("pipedrea") }, { TEXT("untrack"), TEXT("untracke") }, { TEXT("megaple"), TEXT("tokyo") },
		{ TEXT("trick"), TEXT("trick") }, { TEXT("alaska"), TEXT("alaska") },
	};
	const FString* F = Map.Find(Track.ToLower());
	return F ? *F : FString();
}

bool FSSXCameraDefSet::LoadForTrack(const FString& Track, const FString& Dir)
{
	TArray<FString> Files;
	const FString Course = CourseFileForTrack(Track);
	if (!Course.IsEmpty())
	{
		Files.Add(Course);
	}
	Files.Add(TEXT("commonob"));
	Files.Add(TEXT("scripts"));
	LoadStageAreas(Track, Dir);
	return LoadFiles(Files, Dir);
}

bool FSSXCameraDefSet::LoadStageAreas(const FString& Track, const FString& Dir)
{
	StageAreas.Reset();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir.IsEmpty() ? DefaultDir() : Dir, TEXT("stage_areas.json"))))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	const TSharedPtr<FJsonObject>* T = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->TryGetObjectField(Track.ToLower(), T))
	{
		return false;
	}
	for (const auto& It : (*T)->Values)
	{
		const TSharedPtr<FJsonObject>* A = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* M = nullptr;
		if (!It.Value->TryGetObject(A) || !(*A)->TryGetArrayField(TEXT("matrix"), M) || M->Num() != 16)
		{
			continue;
		}
		FMatrix Mat;
		for (int32 K = 0; K < 16; ++K)
		{
			Mat.M[K / 4][K % 4] = (*M)[K]->AsNumber();
		}
		StageAreas.Add(FCString::Atoi(*It.Key), Mat);
	}
	return StageAreas.Num() > 0;
}

bool FSSXCameraDefSet::StageAreaTransform(int32 Area, FVector& InOutPos, double& OutYaw) const
{
	// FUN_001777D0
	const FMatrix* M = (Area == 1 || Area == 2) ? StageAreas.Find(Area) : nullptr;
	if (!M)
	{
		return false;
	}
	const FVector P = InOutPos;
	InOutPos = FVector(
		P.X * M->M[0][0] + P.Y * M->M[1][0] + P.Z * M->M[2][0] + M->M[3][0],
		P.X * M->M[0][1] + P.Y * M->M[1][1] + P.Z * M->M[2][1] + M->M[3][1],
		P.X * M->M[0][2] + P.Y * M->M[1][2] + P.Z * M->M[2][2] + M->M[3][2]);
	const float X = float(M->M[0][0]), Y = float(M->M[0][1]);
	if (X == 0.0f)
	{
		OutYaw = Y == 0.0f ? 0.0 : (Y < 0.0f ? -UE_DOUBLE_HALF_PI : UE_DOUBLE_HALF_PI);
	}
	else
	{
		OutYaw = FMath::Atan(double(Y) / double(X));
		if (X < 0.0f)
		{
			OutYaw += Y > 0.0f ? UE_DOUBLE_PI : -UE_DOUBLE_PI;
		}
	}
	return true;
}
