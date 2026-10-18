#pragma once

#include "CoreMinimal.h"

// All values game space (Z up, cm, rad).
struct FSSXCameraDef
{
	FString Name;
	int32 PosAttachRider = 0;
	int32 PosRiderSlot = 0;         // FUN_00173c50
	int32 OrbitLagEnable = 0;
	double OrbitLagDiv = 0.0;
	int32 PosSmoothEnable = 0;
	int32 PosSmoothParam = 0;
	int32 UnkView40Enable = 0;
	double UnkView44Value = 0.0;    // TODO: consumer
	int32 AnchorUseBone = 0;
	int32 BoneIndex = 0;
	int32 StageArea = 0;
	FVector Base = FVector::ZeroVector;
	int32 OrbitEnable = 0;
	double OrbitDist = 0.0;
	double OrbitDistBoost = 0.0;
	int32 YawFromHeading = 0;
	int32 YawFromBone = 0;
	double YawOffset = 0.0;
	int32 YawGoofyFlip = 0;
	int32 ElevFromVelocity = 0;
	int32 ElevFromBone = 0;
	double ElevBase = 0.0;
	int32 ElevClampEnable = 0;
	double ElevMin = 0.0;
	double ElevMax = 0.0;
	double ElevDivDescending = 1.0;
	double ElevDivAscending = 1.0;
	int32 TgtAttachRider = 0;
	int32 TgtRiderSlot = 0;
	FVector TgtOff = FVector::ZeroVector;
	double TgtYaw = 0.0;
	double TgtPitch = 0.0;
	double TgtRoll = 0.0;
	double Fov = 1.6;
	double AngleSmoothDiv = 1.0;
	double RollDiv = 1.0;
	int32 RollFollowSlope = 0;
	int32 TgtUseBone = 0;
	int32 TgtBoneIndex = 0;
	int32 AutoFov = 0;
	double AutoFovRef = 0.0;
	int32 TgtReverse = 0;
	double NearClip = 100.0;
	int32 StaticCut = 0;
};

// FUN_00171610, FUN_00171500, FUN_00171e28, 0x177078
struct FSSXCameraRegion
{
	FString Name;
	FVector Centre = FVector::ZeroVector;
	int32 StageArea = 0;                    // FUN_001777D0
	FString ScriptOnEnter;
	FString ScriptOnExit;
	FString ScriptWhileInside;
	double Radius = 0.0;
	int32 RiderSlot = 0;
	int32 OnExit = 0, OnEnter = 0, WhileInside = 0;
	int32 ActiveInRace = 0, ActiveInReplay = 0;
	int32 OnlyFinished = 0;
	int32 NeedsLap = 0, NeedsCond = 0;
	int32 Capsule = 0;
	double CapsuleLen = 0.0, CapsuleYaw = 0.0, CapsulePitch = 0.0;
	int32 NeedBigAir = 0;
	double MinAirDist = 0.0;
	int32 NeedWipeout = 0;
};

struct FSSXCameraScriptEvent
{
	int32 Op = 0;
	FString Name;
	int32 A = 0;
	int32 B = 0;
	double C = 0.0;
	double T0 = 0.0;
	double T1 = 0.0;
	int32 Arg = 0;
};

// FUN_00176850
struct FSSXSceneActor
{
	FString List;
	bool bEnabled = false;
	int32 Role = -1;   // 0 player 1, 10 player 2, 11 / 12 the racer before / after player 1, 13 the next unnamed racer, 14..25 character role - 14
	bool bPlace = false;   // FUN_0011CAE8: placed at Pos / Yaw at the start and every frame
	int32 StageArea = 0;   // 1 / 2: Pos / Yaw relative to the start / finish stage area; 0 = world
	                        // FUN_00172100, FUN_001777D0
	FVector Pos = FVector::ZeroVector;
	double Yaw = 0.0;
};

// FUN_0016e518
class SSXTRICKYREBUILD_API FSSXCameraDefSet
{
public:
	bool LoadFiles(const TArray<FString>& Files, const FString& Dir = FString());
	static FString CourseFileForTrack(const FString& Track);
	bool LoadForTrack(const FString& Track, const FString& Dir = FString());
	static FString DefaultDir();

	const FSSXCameraDef* Find(const FString& Name) const { return Cameras.Find(Name); }
	// 0x1786D0, 0x178A28
	FSSXCameraDef* FindMutable(const FString& Name) { return Cameras.Find(Name); }
	// FUN_0016e408, FUN_0016e468
	const TArray<TArray<FSSXCameraRegion>>& GetRegionFiles() const { return RegionFiles; }
	const TArray<FSSXCameraScriptEvent>* FindScript(const FString& Name) const { return Scripts.Find(Name); }
	bool IsLoaded() const { return Cameras.Num() > 0; }
	const TArray<int32>* FindAnimation(const FString& Name) const { return Animations.Find(Name); }
	const TArray<FSSXSceneActor>* FindScene(const FString& Name) const { return Scenes.Find(Name); }

	bool AddJson(const FString& Text);

	// FUN_001777D0
	// area 1 / 2 = Mdl_StageArea_Start_0 / _Finish_0 world matrix (row vectors, translation in row 3): pos = (x, y, z, 1) M,
	// yaw = heading of the matrix X row; false (nothing written) for area 0 / unknown / no instance
	bool StageAreaTransform(int32 Area, FVector& InOutPos, double& OutYaw) const;
	bool LoadStageAreas(const FString& Track, const FString& Dir = FString());
	void SetStageArea(int32 Area, const FMatrix& M) { StageAreas.Add(Area, M); }

private:
	TMap<int32, FMatrix> StageAreas;
	TArray<TArray<FSSXCameraRegion>> RegionFiles;
	TMap<FString, FSSXCameraDef> Cameras;
	TMap<FString, TArray<FSSXCameraScriptEvent>> Scripts;
	TMap<FString, TArray<int32>> Animations;
	TMap<FString, TArray<FSSXSceneActor>> Scenes;
};
