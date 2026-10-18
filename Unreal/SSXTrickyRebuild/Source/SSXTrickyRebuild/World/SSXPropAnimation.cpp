#include "World/SSXPropAnimation.h"
#include "Sim/SimCoords.h"
#include "Race/GameRng.h"

#include "Components/StaticMeshComponent.h"
#include "Effects/SSXTrackEffects.h"
#include "Effects/TrackEffects.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Particles/SSXSpriteRenderer.h"
#include "SSXTrickyRebuild.h"
#include "SSXShowOffDraw.h"
#include "Sim/Air/AirStates.h"
#include "Sim/Ground/WallCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/RiderSim.h"
#include "Sim/WipeOut/TumbleBody.h"
#include "UI/SSXFrontEndData.h"
#include "World/SSXWorldPresentation.h"

using namespace SSXPropNodes;

namespace
{
	bool IsAnimType(int32 T) { return T >= SSXEffects::TypeAnimObject && T <= SSXEffects::TypeAnimTexFlip; }

	FMatrix GameToUnrealMatrix(const FMatrix& G)
	{
		static const double S[4] = { 1.0, -1.0, 1.0, 1.0 };
		FMatrix R;
		for (int32 I = 0; I < 4; ++I)
		{
			for (int32 J = 0; J < 4; ++J) { R.M[I][J] = S[I] * S[J] * G.M[I][J]; }
		}
		return R;
	}

	FQuat4d RotationOf(const FMatrix& M)
	{
		FMatrix R = M;
		R.RemoveScaling();
		R.SetOrigin(FVector::ZeroVector);
		return FQuat4d(R);
	}

	int32 ObjectOfActor(const AActor* A)
	{
		for (const FName& Tag : A->Tags)
		{
			const FString S = Tag.ToString();
			if (S.StartsWith(TEXT("SSXObj"), ESearchCase::CaseSensitive) && S.Len() > 6 && S.Mid(6).IsNumeric())
			{
				return FCString::Atoi(*S.Mid(6));
			}
		}
#if WITH_EDITOR
		const FString L = A->GetActorLabel();
		const int32 P = L.Find(TEXT("_obj"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (P != INDEX_NONE && L.Mid(P + 4).IsNumeric())
		{
			return FCString::Atoi(*L.Mid(P + 4));
		}
#endif
		return 0;
	}
}

USSXPropAnimation* USSXPropAnimation::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<USSXPropAnimation>() : nullptr;
}

void USSXPropAnimation::BeginTrack(const FString& Track, const SSXEffects::FEffectData* Data, SSXEffects::FEffectWorld* InFx,
                                   const TSharedPtr<FPatchWorld>& Sim)
{
	for (const TPair<int32, FNode>& P : Nodes)
	{
		ResetPose(P.Key);
	}
	PendingFences.Reset();
	Nodes.Reset();
	Moving.Reset();
	RiderProxies.Reset();
	LastAnimWorld.Reset();
	PendingDeltas.Reset();
	FxData = Data;
	Fx = InFx;
	SimWorld = Sim;
	FrameNo = 0;
	RollersKnocked = 0;
	MaxRollerTravel = 0.0;
	CrowdSteps = 0;
	bCrowdBuilt = false;
	StaticCrowdFrames.Reset();
	TrackStem = Track.ToLower();
	CrackFrames.Reset();
	MovieInsts.Reset();
	MovieStream = SSXPropMovie::FStream();
	MovieFile = SSXPropMovie::FSssFile();
	bMovieFileTried = false;
	bMovieDirty = false;
	MovieUploaded = INDEX_NONE;
	MovieTexture = nullptr;
	FString Err;
	if (!AnimData.Load(FPropAnimData::DefaultPath(Track), &Err))
	{
		UE_LOG(LogSSX, Warning, TEXT("[props] %s: %s; keyframed props and crowds stay static"), *Track, *Err);
	}
}

void USSXPropAnimation::RestartTrack()
{
	// 0x115FE8
	for (const TPair<int32, FNode>& P : Nodes)
	{
		if (P.Value.Type == SSXEffects::TypeRoller)
		{
			if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin()) { W->SetInstanceMovable(P.Key, false); }
		}
		if (P.Value.Type == SSXEffects::TypeMeshAnim)
		{
			if (USSXTrackEffects* Te = USSXTrackEffects::Get(this)) { Te->MarkDrawDirty(P.Key); }
		}
		if (IsAnimType(P.Value.Type))
		{
			EndAnimCollision(P.Key);
		}
	}
	TSet<int32> Posed;
	for (const TPair<int32, FNode>& P : Nodes) { Posed.Add(P.Key); }
	for (const TPair<int32, TArray<FMatrix>>& P : LastAnimWorld) { Posed.Add(P.Key); }
	for (const TPair<int32, TArray<FMatrix>>& P : PendingDeltas) { Posed.Add(P.Key); }
	for (int32 Inst : Posed)
	{
		ResetPose(Inst);
	}
	PendingFences.Reset();
	Nodes.Reset();
	Moving.Reset();
	RiderProxies.Reset();
	LastAnimWorld.Reset();
	PendingDeltas.Reset();
	CrackFrames.Reset();
	MovieInsts.Reset();
	MovieStream.Refs = 0;
	MovieStream.bOpen = false;
}

void USSXPropAnimation::SaveReplay(FReplayKeyframe& Out) const
{
	Out.bValid = true;
	Out.Nodes = Nodes;
	Out.LastAnimWorld = LastAnimWorld;
	Out.Controllers = Controllers.SaveState();
	Out.Moving = Moving;
	Out.RiderProxies = RiderProxies;
	Out.CrackFrames = CrackFrames;
	Out.MovieInsts = MovieInsts;
	Out.PendingFences = PendingFences;
	Out.FrameNo = FrameNo;
}

void USSXPropAnimation::LoadReplay(const FReplayKeyframe& In, TConstArrayView<int32> WasAnimated)
{
	if (!In.bValid)
	{
		return;
	}
	const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
	USSXTrackEffects* Te = USSXTrackEffects::Get(this);
	// 0x18F6C0
	TSet<int32> Posed;
	for (const TPair<int32, FNode>& P : Nodes)
	{
		Posed.Add(P.Key);
		if (P.Value.Type == SSXEffects::TypeRoller && W) { W->SetInstanceMovable(P.Key, false); }
		if (P.Value.Type == SSXEffects::TypeMeshAnim && Te) { Te->MarkDrawDirty(P.Key); }
		if (IsAnimType(P.Value.Type) && W) { W->SetInstanceAnimController(P.Key, false); }
	}
	for (const TPair<int32, TArray<FMatrix>>& P : LastAnimWorld) { Posed.Add(P.Key); }
	for (const TPair<int32, TArray<FMatrix>>& P : PendingDeltas) { Posed.Add(P.Key); }
	TSet<int32> Animated(WasAnimated);
	for (const TPair<int32, TArray<FMatrix>>& P : LastAnimWorld) { Animated.Add(P.Key); }
	Posed.Append(Animated);
	for (int32 Inst : Posed)
	{
		if (Animated.Contains(Inst) && !In.LastAnimWorld.Contains(Inst))
		{
			TArray<FMatrix> Static;
			AnimData.ObjectWorldMatrices(Inst, InstMatrix(Inst), 0.0, false, Static);
			UpdateAnimCollision(Inst, Static);
		}
		ResetPose(Inst);
	}
	// 0x148CC8 / 0x200098 / 0x1FFA10
	MovieStream.Refs = 0;
	MovieStream.bOpen = false;

	// 0x18F800 / 0x18F8A8
	Nodes = In.Nodes;
	LastAnimWorld = In.LastAnimWorld;
	Controllers.LoadState(In.Controllers);
	Moving = In.Moving;
	RiderProxies = In.RiderProxies;
	CrackFrames = In.CrackFrames;
	PendingFences = In.PendingFences;
	FrameNo = In.FrameNo;
	MovieInsts = In.MovieInsts;
	for (int32 K = 0; K < MovieInsts.Num(); ++K)
	{
		MovieStream.Acquire();   // 0x148AD0 / 0x148B28 / 0x1FFF30
	}
	for (TPair<int32, FNode>& P : Nodes)
	{
		FNode& N = P.Value;
		PendingReset.Remove(P.Key);
		if (IsAnimType(N.Type))
		{
			if (W) { W->SetInstanceAnimController(P.Key, true); }
			if (const TArray<FMatrix>* World = LastAnimWorld.Find(P.Key))
			{
				TArray<FMatrix> Static;
				AnimData.ObjectWorldMatrices(P.Key, InstMatrix(P.Key), 0.0, false, Static);
				TArray<FMatrix>& D = PendingDeltas.FindOrAdd(P.Key);
				D.SetNum(World->Num());
				for (int32 K = 0; K < World->Num() && K < Static.Num(); ++K) { D[K] = Static[K].Inverse() * (*World)[K]; }
				UpdateAnimCollision(P.Key, *World);
			}
			continue;
		}
		if (N.Type == SSXEffects::TypeRoller && W) { W->SetInstanceMovable(P.Key, true); }
		if (N.Type == SSXEffects::TypeMeshAnim && Te) { Te->MarkDrawDirty(P.Key); }
		if (N.Type == SSXEffects::TypeRoller || N.Type == SSXEffects::TypeMeshAnim)
		{
			TickNode(N);
		}
	}
}

void USSXPropAnimation::BindSim(FRiderSim& Sim)
{
	Controllers.Bind(Sim);
	Sim.OnMovingObjects = [this](FRiderSim& S, const FRiderSphereSet& Shape, SSXAnim::FRiderAnimChannel* Anim) { MovingObjects(S, Shape, Anim); };
	Sim.OnTumbleNeighbours = [this](FRiderSim& S, const FRiderSphereSet& Shape, SSXWipeOut::FTumbleBody& Body) { return TumbleNeighbours(S, Shape, Body); };
}

FMatrix USSXPropAnimation::InstMatrix(int32 Inst) const
{
	return FxData && FxData->InstMatrix.IsValidIndex(Inst) ? FxData->InstMatrix[Inst] : FMatrix::Identity;
}

void USSXPropAnimation::OnNodeCreated(int32 Inst, int32 Type, int32 Record, const FContactRecord* Contact, bool bByTouch)
{
	if (!FxData || !FxData->Records.IsValidIndex(Record))
	{
		return;
	}
	if (Type == SSXEffects::TypeMovie)
	{
		// 0x148A70 / 0x148B28 / 0x1FFF30
		if (!MovieInsts.Contains(Inst))
		{
			MovieInsts.Add(Inst);
			MovieStream.Acquire();
			if (!bMovieFileTried)
			{
				bMovieFileTried = true;
				FString Err;
				if (!MovieFile.Load(SSXPropMovie::DefaultPath(TrackStem), &Err))
				{
					UE_LOG(LogSSX, Warning, TEXT("[props] %s movie: %s; the screens show a blank texture"), *TrackStem, *Err);
				}
			}
		}
		return;
	}
	const SSXEffects::FRecord& R = FxData->Records[Record];
	const uint8* Bytes = FxData->RecordBytes(R);
	FNode N;
	N.Type = Type;
	N.Inst = Inst;
	N.Record = Record;
	if (IsAnimType(Type))
	{
		FAnimObjectRecord Rec;
		if (!FAnimObjectRecord::Parse(Bytes, R.Size, Rec))
		{
			return;
		}
		const FPropModelInfo* M = AnimData.ModelOfInstance(Inst);
		// 0x198810 / 0x23D4F8 / 0x23D560
		N.Anim.InitGame(Rec, M ? M->EndFrame : 0.0, bByTouch);
		if (Fx) { Fx->ApplyNodeFlags(Inst, SSXEffects::FlDisplay, SSXEffects::FlNodeDrawn); }
		if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin()) { W->SetInstanceAnimController(Inst, true); }
		Nodes.Add(Inst, N);
		TickNode(Nodes[Inst]);
		return;
	}
	if (Type == SSXEffects::TypeCracked)
	{
		// 0x1457A0
		if (const TArray<int32>* Fb = AnimData.FirstFlipbookOfInstance(Inst))
		{
			SSXProps::FTexFlip Bind;
			Bind.FrameCount = Fb->Num();
			Bind.SetFrame(-1);
			CrackFrames.Add(Inst, Bind.Frame);
		}
	}
	switch (Type)
	{
	case SSXEffects::TypeCrowdBox:
	{
		int32 Rows = 0, Cols = 0;
		if (R.Size >= 24)
		{
			Rows = FxData->I(R, 2);
			Cols = FxData->I(R, 3);
		}
		N.Crowd.Init(Rows, Cols, [this]() { return RandU32(); });
		break;
	}
	case SSXEffects::TypeRoller:
	{
		FRollerRecord Rec;
		const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
		const int32 K = W ? W->FindInstance(Inst) : INDEX_NONE;
		if (!FRollerRecord::Parse(Bytes, R.Size, Rec) || K == INDEX_NONE)
		{
			return;
		}
		const FSimInstance& SI = W->GetInstance(K);
		for (int32 J = 0; J < SI.NumNodes && N.TreeNode == INDEX_NONE; ++J)
		{
			if (W->GetInstanceNode(SI.FirstNode + J).Tree != INDEX_NONE) { N.TreeNode = SI.FirstNode + J; }
		}
		if (N.TreeNode == INDEX_NONE)
		{
			UE_LOG(LogSSX, Warning, TEXT("[props] Roller on instance %d without a sphere tree: not simulated"), Inst);
			return;
		}
		const FSimInstanceNode& Node = W->GetInstanceNode(N.TreeNode);
		const FSimSphereTree& Tree = W->GetSphereTree(Node.Tree);
		FMat3 InvIBody;
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J) { InvIBody.M[I][J] = Tree.InvInertia[3 * I + J]; }
		}
		N.X0 = Node.NodeToGame.TransformPosition(Tree.Com);
		N.Q0 = RotationOf(Node.NodeToGame);
		FRigidBody Body;
		Body.Init(Rec.Mass, InvIBody, Tree.Com, N.X0, N.Q0);
		N.Roller.Init(Rec, Body, Contact);
		if (Fx) { Fx->ApplyNodeFlags(Inst, 0x22, 0x45); }
		W->SetInstanceMovable(Inst, true);
		N.Proxy = Moving.Add(Inst, FMovingObjects::EKind::Roller, SI.Bounds);
		++RollersKnocked;
		break;
	}
	case SSXEffects::TypeMeshAnim:
	{
		FMeshAnimRecord Rec;
		if (!FMeshAnimRecord::Parse(Bytes, R.Size, Rec))
		{
			return;
		}
		TArray<FMatrix> World;
		if (const TArray<FMatrix>* Last = LastAnimWorld.Find(Inst))
		{
			World = *Last;
		}
		else
		{
			AnimData.ObjectWorldMatrices(Inst, InstMatrix(Inst), 0.0, false, World);
		}
		TArray<TPair<FVector, FQuat4d>> Start;
		for (const FMatrix& M : World)
		{
			Start.Add({ M.GetOrigin(), RotationOf(M) });
			N.PieceStart.Add(FTransform(FQuat(RotationOf(M)), M.GetOrigin()));
		}
		N.Mesh.Init(Rec, Start, InstMatrix(Inst), Contact, [this]() { return Rand01(); }, [this]() { return int32(RandU32() & 1); });
		if (Fx) { Fx->ApplyNodeFlags(Inst, SSXEffects::FlDisplay, SSXEffects::FlNodeDrawn); }
		break;
	}
	case SSXEffects::TypeFlag:
	{
		// 0x144938
		const FStripSource* S = AnimData.StripOf(Inst);
		if (R.Size >= 24)
		{
			N.Flag.Init(FxData->I(R, 1), FxData->F(R, 2), FxData->F(R, 3), FxData->F(R, 4), S ? S->Scale.X : 1.0, [this]() { return Rand01(); });
		}
		if (Fx) { Fx->ApplyNodeFlags(Inst, SSXEffects::FlDisplay, SSXEffects::FlNodeDrawn); }
		break;
	}
	case SSXEffects::TypeFence:
	{
		// 0x143550
		InitFence(N, Record);
		double Amount = 0.0;
		if (Contact)
		{
			const FVector Axis = AnimData.StripOf(Inst) ? SSXSim::RsqrtNormal(InstMatrix(Inst).GetScaledAxis(EAxis::X)) : Contact->N;
			Amount = FVector::DotProduct(Axis, Contact->VHat) * FMath::Abs(Contact->A) * 0.001;
		}
		N.Fence.Hit(Amount);
		if (Fx) { Fx->ApplyNodeFlags(Inst, SSXEffects::FlDisplay, SSXEffects::FlNodeDrawn); }
		Nodes.Add(Inst, MoveTemp(N));
		FenceNeighbour(Inst, 1, Nodes[Inst].Fence.NeighbourAmount());
		FenceNeighbour(Inst, 0, Nodes[Inst].Fence.NeighbourAmount());
		return;
	}
	default:
		return;
	}
	Nodes.Add(Inst, MoveTemp(N));
}

void USSXPropAnimation::OnNodeDestroyed(int32 Inst, int32 Type)
{
	if (Type == SSXEffects::TypeCracked)
	{
		CrackFrames.Remove(Inst);
	}
	if (Type == SSXEffects::TypeMovie)
	{
		// 0x148CC8 / 0x200098
		if (MovieInsts.Remove(Inst) > 0) { MovieStream.Release(); }
		return;
	}
	FNode* N = Nodes.Find(Inst);
	if (!N || N->Type != Type)
	{
		return;
	}
	if (N->Type == SSXEffects::TypeRoller)
	{
		Moving.Remove(N->Proxy);
		if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin()) { W->SetInstanceMovable(Inst, false); }
		ResetPose(Inst);
	}
	else if (IsAnimType(N->Type))
	{
		ResetPose(Inst);
		EndAnimCollision(Inst);
	}
	else if (N->Type == SSXEffects::TypeMeshAnim)
	{
		ResetPose(Inst);
		if (USSXTrackEffects* Te = USSXTrackEffects::Get(this)) { Te->MarkDrawDirty(Inst); }
	}
	Nodes.Remove(Inst);
}

void USSXPropAnimation::OnNodeRestart(int32 Inst)
{
	if (FNode* N = Nodes.Find(Inst); N && N->Type == SSXEffects::TypeRoller)
	{
		N->Roller.Wake();
		if (Fx) { Fx->SetRollerAsleep(Inst, false); }
	}
}

void USSXPropAnimation::OnRollerStop(int32 Inst)
{
	if (FNode* N = Nodes.Find(Inst); N && N->Type == SSXEffects::TypeRoller)
	{
		N->Roller.bAsleep = true;
		if (Fx) { Fx->SetRollerAsleep(Inst, true); }
	}
}

void USSXPropAnimation::OnNodeMessage(int32 Inst, int32 Type, int32 Msg, double Value)
{
	if (FNode* N = Nodes.Find(Inst); N && IsAnimType(N->Type) && IsAnimType(Type))
	{
		N->Anim.Message(Msg, Value);
	}
}

void USSXPropAnimation::InitFence(FNode& N, int32 Record)
{
	// 0x143FF8
	N.Fence = FFenceStrip();
	if (FxData && FxData->Records.IsValidIndex(Record))
	{
		const SSXEffects::FRecord& R = FxData->Records[Record];
		const FStripSource* S = AnimData.StripOf(N.Inst);
		N.Fence.Mode = R.Size >= 16 ? FxData->I(R, 1) : 0;
		N.Fence.Scale = R.Size >= 16 ? FxData->F(R, 2) / (FMath::Max(S ? S->Scale.X : 1.0, 1.0) * StripModelScaleK) : 0.0;
	}
}

void USSXPropAnimation::OnFenceTouch(int32 Inst, const FContactRecord& Contact)
{
	// 0x144428
	if (FNode* N = Nodes.Find(Inst); N && N->Type == SSXEffects::TypeFence)
	{
		N->Fence.Touch(FVector::DotProduct(Contact.N, Contact.VHat) * Contact.A * 0.001);
	}
}

void USSXPropAnimation::FenceNeighbour(int32 FromInst, int32 Side, double Amount)
{
	// 0x143928
	const FStripSource* S = AnimData.StripOf(FromInst);
	const FNode* From = Nodes.Find(FromInst);
	if (!S || !From || From->Type != SSXEffects::TypeFence)
	{
		return;
	}
	const int32 Nb = Side == 1 ? ((S->InstFlags & 0x400) ? INDEX_NONE : S->NeighbourC8) : ((S->InstFlags & 0x200) ? INDEX_NONE : S->NeighbourC4);
	if (Nb == INDEX_NONE || FMath::Abs(Amount) < FenceNeighbourMin)
	{
		return;
	}
	const SSXEffects::FController* Ctl = Fx ? Fx->FindController(Nb) : nullptr;
	FNode* Other = Nodes.Find(Nb);
	const bool bHasNode = Fx ? Ctl != nullptr : Other != nullptr;
	if (!bHasNode)
	{
		// 0x143740
		if (!PendingFences.ContainsByPredicate([Nb](const FFenceCreate& C) { return C.Inst == Nb; }))
		{
			PendingFences.Add({ Nb, From->Record, Amount });
		}
		return;
	}
	if ((Fx && Ctl->Type != SSXEffects::TypeFence) || !Other || Other->Type != SSXEffects::TypeFence)
	{
		return;
	}
	// 0x143B08
	Other->Fence.Hit(Amount);
	int32& T = Side == 0 ? Other->Fence.Timer44 : Other->Fence.Timer40;
	if (T == 0) { T = 4; }
}

void USSXPropAnimation::FlushFenceCreations()
{
	TArray<FFenceCreate> Todo = MoveTemp(PendingFences);
	PendingFences.Reset();
	for (const FFenceCreate& C : Todo)
	{
		if (Nodes.Contains(C.Inst) || (Fx && !Fx->CreateCodeNode(C.Inst, SSXEffects::TypeFence, C.Record)))
		{
			continue;
		}
		FNode N;
		N.Type = SSXEffects::TypeFence;
		N.Inst = C.Inst;
		N.Record = C.Record;
		InitFence(N, C.Record);
		N.Fence.Hit(C.Amount);
		N.Fence.Timer40 = 4;
		N.Fence.Timer44 = 4;
		if (Fx) { Fx->ApplyNodeFlags(C.Inst, SSXEffects::FlDisplay, SSXEffects::FlNodeDrawn); }
		Nodes.Add(C.Inst, MoveTemp(N));
	}
}

const FFlagStrip* USSXPropAnimation::FindFlag(int32 Inst) const
{
	const FNode* N = Nodes.Find(Inst);
	return N && N->Type == SSXEffects::TypeFlag ? &N->Flag : nullptr;
}

const FFenceStrip* USSXPropAnimation::FindFence(int32 Inst) const
{
	const FNode* N = Nodes.Find(Inst);
	return N && N->Type == SSXEffects::TypeFence ? &N->Fence : nullptr;
}

bool USSXPropAnimation::GetStripGame(int32 Inst, FVector OutPos[StripVerts], FVector2D OutUV[StripVerts], FColor& OutColour) const
{
	const FNode* N = Nodes.Find(Inst);
	const FStripSource* S = AnimData.StripOf(Inst);
	if (!N || !S || (N->Type != SSXEffects::TypeFlag && N->Type != SSXEffects::TypeFence))
	{
		return false;
	}
	double Off[StripCols];
	const bool bFlag = N->Type == SSXEffects::TypeFlag;
	if (bFlag) { N->Flag.Offsets(Off); } else { N->Fence.Offsets(Off); }
	FVector Local[StripVerts];
	StripLocal(*S, Off, bFlag || N->Fence.Mode == 1, Local, OutUV);
	const FMatrix M = StripDrawMatrix(*S, InstMatrix(Inst));
	for (int32 V = 0; V < StripVerts; ++V) { OutPos[V] = M.TransformPosition(Local[V]); }
	// 0x144EC8 / 0x143FF8 / 0x143CD0
	OutColour = StripColour(*S, bFlag ? FVector(1, 0, 0) : S->Nrm[0]);
	return true;
}

void USSXPropAnimation::ApplyStrips()
{
	StripsDrawn = 0;
	for (auto It = StripHidden.CreateIterator(); It; ++It)
	{
		const FNode* N = Nodes.Find(*It);
		if (N && (N->Type == SSXEffects::TypeFlag || N->Type == SSXEffects::TypeFence))
		{
			continue;
		}
		if (const TArray<FActorRef>* List = Actors.Find(*It))
		{
			for (const FActorRef& R : *List)
			{
				TArray<UStaticMeshComponent*> Comps;
				if (R.Actor.IsValid()) { R.Actor->GetComponents(Comps); }
				for (UStaticMeshComponent* C : Comps) { C->SetVisibility(true); }
			}
		}
		It.RemoveCurrent();
	}
	TMap<UMaterialInterface*, int32> BatchOf;
	TArray<FSSXSpriteBatch> Batches;
	for (const TPair<int32, FNode>& P : Nodes)
	{
		if (P.Value.Type != SSXEffects::TypeFlag && P.Value.Type != SSXEffects::TypeFence)
		{
			continue;
		}
		FVector Pos[StripVerts];
		FVector2D UV[StripVerts];
		FColor Colour;
		const TArray<FActorRef>* List = Actors.Find(P.Key);
		if (!List || !GetStripGame(P.Key, Pos, UV, Colour))
		{
			continue;
		}
		// 0x1E6DF8 / 0x1E6E28
		TArray<UMaterialInterface*, TInlineAllocator<4>> Mats;
		bool bShown = false;
		for (const FActorRef& R : *List)
		{
			AActor* A = R.Actor.Get();
			if (!A)
			{
				continue;
			}
			bShown |= !A->IsHidden();
			TArray<UStaticMeshComponent*> Comps;
			A->GetComponents(Comps);
			for (UStaticMeshComponent* C : Comps)
			{
				if (Mats.Num() == 0)
				{
					for (int32 I = 0; I < C->GetNumMaterials(); ++I)
					{
						if (UMaterialInterface* Mi = C->GetMaterial(I)) { Mats.AddUnique(Mi); }
					}
				}
				C->SetVisibility(false);
			}
		}
		StripHidden.Add(P.Key);
		if (!bShown || Mats.Num() == 0)
		{
			continue;
		}
		TArray<FSSXStripVertex> Strip;
		Strip.SetNum(StripVerts);
		for (int32 V = 0; V < StripVerts; ++V)
		{
			Strip[V].Pos = FVector3f(SSXSim::GameToUnreal(Pos[V]));
			Strip[V].UV = FVector2f(UV[V]);
			Strip[V].Color = Colour;
		}
		for (UMaterialInterface* Mat : Mats)
		{
			int32* B = BatchOf.Find(Mat);
			if (!B)
			{
				B = &BatchOf.Add(Mat, Batches.Num());
				Batches.AddDefaulted_GetRef().Material = Mat;
				if (!StripMaterials.Contains(Mat))
				{
					StripMaterials.Add(Mat);
					bStripSoftDepth |= USSXWorldPresentation::IsSoftBlendMaterial(Mat);
					if (StripRenderer)
					{
						TArray<UMaterialInterface*> Bases;
						for (UMaterialInterface* M : StripMaterials) { Bases.Add(M); }
						StripRenderer->SetBaseMaterials(Bases);
						StripRenderer->SetRenderCustomDepth(bStripSoftDepth);
					}
				}
			}
			Batches[*B].Strips.Add(Strip);
		}
		++StripsDrawn;
	}
	if (!StripRenderer && Batches.Num() > 0)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		AActor* Host = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		if (Host)
		{
			StripRenderer = NewObject<USSXSpriteRenderComponent>(Host, TEXT("SSXFlagFenceStrips"));
			Host->SetRootComponent(StripRenderer);
			StripRenderer->RegisterComponent();
			TArray<UMaterialInterface*> Bases;
			for (UMaterialInterface* M : StripMaterials) { Bases.Add(M); }
			StripRenderer->SetBaseMaterials(Bases);
			StripRenderer->SetRenderCustomDepth(bStripSoftDepth);
		}
	}
	if (StripRenderer)
	{
		StripRenderer->SetBatches(MoveTemp(Batches));
	}
}

void USSXPropAnimation::StepFrame()
{
	Controllers.Tick();
	++FrameNo;
	if (MovieStream.Step(MovieFile.NumFrames()))   // 0x2001F8
	{
		bMovieDirty = true;
	}
	TArray<int32> Done;
	for (TPair<int32, FNode>& P : Nodes)
	{
		FNode& N = P.Value;
		if (IsAnimType(N.Type))
		{
			if (N.Anim.Tick()) { Done.Add(P.Key); }
			if (N.Anim.bEvaluated) { TickNode(N); }
		}
		else if (N.Type == SSXEffects::TypeCrowdBox)
		{
			TArray<TPair<double, int32>, TInlineAllocator<8>> Humans;
			const FVector O = InstMatrix(N.Inst).GetOrigin();
			for (const TPair<FVector, int32>& H : CrowdHumans) { Humans.Add({ FVector::Dist(H.Key, O), H.Value }); }
			const bool bStep = FrameNo % FCrowdBox::Tick == 0;
			N.Crowd.Update(FrameNo, bCrowdPreRace, Humans, [this]() { return RandU32(); });
			CrowdSteps += bStep && N.Crowd.State != 0;
		}
		else if (N.Type == SSXEffects::TypeRoller)
		{
			if (N.Roller.bAsleep)
			{
				continue;
			}
			const bool bRest = N.Roller.Tick([this, &N](const FRigidBody&, FBodyHit& H) { return BodyHit(N, true, H); },
			                                 [this, &N](const FRigidBody&, FBodyHit& H) { return BodyHit(N, false, H); });
			TickNode(N);
			if (bRest) { Done.Add(P.Key); }
		}
		else if (N.Type == SSXEffects::TypeMeshAnim)
		{
			const FMeshAnim::EResult Res = N.Mesh.Tick();
			TickNode(N);
			if (Res == FMeshAnim::EResult::Done) { Done.Add(P.Key); }
		}
		else if (N.Type == SSXEffects::TypeFlag)
		{
			if (N.Flag.Tick()) { Done.Add(P.Key); }
		}
		else if (N.Type == SSXEffects::TypeFence)
		{
			const int32 Inst = P.Key;
			if (N.Fence.Tick([this, Inst](int32 Side, double Amount) { FenceNeighbour(Inst, Side, Amount); })) { Done.Add(Inst); }
		}
	}
	FlushFenceCreations();
	for (int32 Inst : Done)
	{
		FNode* N = Nodes.Find(Inst);
		if (!N || N->bDone)
		{
			continue;
		}
		N->bDone = N->Type != SSXEffects::TypeRoller;
		if (Fx)
		{
			Fx->NodeDone(Inst);
		}
		else if (N->Type == SSXEffects::TypeRoller)
		{
			N->Roller.bAsleep = true;
		}
	}
}

void USSXPropAnimation::TickNode(FNode& N)
{
	const int32 Inst = N.Inst;
	if (IsAnimType(N.Type))
	{
		TArray<FMatrix> Static, World;
		AnimData.ObjectWorldMatrices(Inst, InstMatrix(Inst), 0.0, false, Static);
		if (N.Anim.ComboPose())
		{
			AnimData.ComboWorldMatrices(Inst, InstMatrix(Inst), N.Anim.ComboT, N.Anim.SnapshotT, World);   // 0x19A478
		}
		else
		{
			AnimData.ObjectWorldMatrices(Inst, InstMatrix(Inst), N.Anim.T, true, World);
		}
		TArray<FMatrix>& D = PendingDeltas.FindOrAdd(Inst);
		D.SetNum(World.Num());
		for (int32 K = 0; K < World.Num(); ++K) { D[K] = Static[K].Inverse() * World[K]; }
		UpdateAnimCollision(Inst, World);
		LastAnimWorld.Add(Inst, MoveTemp(World));
	}
	else if (N.Type == SSXEffects::TypeRoller)
	{
		const FMatrix Delta = RollerDelta(N);
		MaxRollerTravel = FMath::Max(MaxRollerTravel, FVector::Dist(N.Roller.Body.X, N.X0));
		const FPropModelInfo* M = AnimData.ModelOfInstance(Inst);
		TArray<FMatrix>& D = PendingDeltas.FindOrAdd(Inst);
		D.Init(Delta, M ? FMath::Max(M->NumObjects, 1) : 1);
		if (const TSharedPtr<FPatchWorld> W = SimWorld.Pin(); W && N.Proxy != INDEX_NONE)
		{
			const int32 K = W->FindInstance(Inst);
			if (K != INDEX_NONE) { Moving.Update(N.Proxy, W->GetInstance(K).Bounds.TransformBy(Delta)); }
		}
	}
	else if (N.Type == SSXEffects::TypeMeshAnim)
	{
		TArray<FMatrix>& D = PendingDeltas.FindOrAdd(Inst);
		D.SetNum(N.Mesh.Pieces.Num());
		for (int32 K = 0; K < N.Mesh.Pieces.Num(); ++K)
		{
			const FTransform& S = N.PieceStart[K];
			const FQuat Dq = FQuat(N.Mesh.Pieces[K].Q) * S.GetRotation().Inverse();
			D[K] = FTranslationMatrix(-S.GetTranslation()) * FQuatRotationMatrix(Dq) * FTranslationMatrix(N.Mesh.Pieces[K].Pos);
		}
	}
}

FMatrix USSXPropAnimation::RollerDelta(const FNode& N) const
{
	const FQuat Dq = FQuat(N.Roller.Body.Q) * FQuat(N.Q0).Inverse();
	return FTranslationMatrix(-N.X0) * FQuatRotationMatrix(Dq) * FTranslationMatrix(N.Roller.Body.X);
}

bool USSXPropAnimation::BodyHit(const FNode& N, bool bTerrain, FBodyHit& Out) const
{
	const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
	if (!W || N.TreeNode == INDEX_NONE)
	{
		return false;
	}
	// 0x25AFF8 / 0x25B878
	const FSimInstanceNode& Node = W->GetInstanceNode(N.TreeNode);
	const FSimSphereTree& Tree = W->GetSphereTree(Node.Tree);
	const FMatrix ToWorld = Node.NodeToGame * RollerDelta(N);
	FRiderWorldHit Hit;
	if (!W->QueryTree(Tree, ToWorld, !bTerrain, Hit) || !(Hit.T > 0.0))
	{
		return false;
	}
	Out.Depth = Hit.T;
	Out.N = Hit.Normal;
	Out.C = Hit.Point;
	return true;
}

void USSXPropAnimation::UpdateAnimCollision(int32 Inst, TConstArrayView<FMatrix> World)
{
	const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
	const FPropModelInfo* M = AnimData.ModelOfInstance(Inst);
	if (!W || !M || W->FindInstance(Inst) == INDEX_NONE)
	{
		return;
	}
	int32 J = 0;
	for (int32 K = 0; K < M->NumObjects && K < World.Num(); ++K)
	{
		if (AnimData.Objects[M->FirstObject + K].Meshes > 0)
		{
			W->SetInstanceNodeMatrix(Inst, J++, World[K]);
		}
	}
}

void USSXPropAnimation::EndAnimCollision(int32 Inst)
{
	const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
	if (!W || W->FindInstance(Inst) == INDEX_NONE)
	{
		return;
	}
	TArray<FMatrix> Static;
	AnimData.ObjectWorldMatrices(Inst, InstMatrix(Inst), 0.0, false, Static);
	UpdateAnimCollision(Inst, Static);
	W->SetInstanceAnimController(Inst, false);
}

void USSXPropAnimation::MovingObjects(FRiderSim& Sim, const FRiderSphereSet& Shape, SSXAnim::FRiderAnimChannel* Anim)
{
	const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
	if (!W || Nodes.Num() == 0)
	{
		return;
	}
	const FBox RiderBox(Shape.BoundCentre - FVector(Shape.BoundRadius), Shape.BoundCentre + FVector(Shape.BoundRadius));
	int32* Proxy = RiderProxies.Find(&Sim);
	if (!Proxy || !Moving.Get(*Proxy).bUsed)
	{
		const int32 Id = Moving.Add(INDEX_NONE, FMovingObjects::EKind::Rider, RiderBox);
		if (Id == INDEX_NONE)
		{
			return;
		}
		Proxy = &RiderProxies.Add(&Sim, Id);
	}
	Moving.Update(*Proxy, RiderBox);
	TArray<int32> Pairs;
	Moving.Pairs(*Proxy, Pairs);
	for (int32 Id : Pairs)
	{
		const FMovingObjects::FProxy& Px = Moving.Get(Id);
		FNode* N = Px.Kind == FMovingObjects::EKind::Roller ? Nodes.Find(Px.Owner) : nullptr;
		if (!N || N->Type != SSXEffects::TypeRoller || N->TreeNode == INDEX_NONE)
		{
			continue;
		}
		const FSimInstanceNode& Node = W->GetInstanceNode(N->TreeNode);
		const FSimSphereTree& Tree = W->GetSphereTree(Node.Tree);
		const FMatrix ToWorld = Node.NodeToGame * RollerDelta(*N);
		const FMatrix ToNode = ToWorld.Inverse();
		FVector Push, Contact;
		if (!Tree.QuerySphere(ToNode.TransformPosition(Shape.BoundCentre), Shape.BoundRadius, Push, Contact))
		{
			continue;
		}
		bool bHit = false;
		for (int32 S = 0; S < Shape.Count && !bHit; ++S)
		{
			bHit = Tree.QuerySphere(ToNode.TransformPosition(Shape.Centres[S]), Shape.Radii[S], Push, Contact);
		}
		if (!bHit)
		{
			continue;
		}
		const FVector Nw = SSXSim::RsqrtNormal(ToWorld.TransformVector(Push));
		const FVector Cw = ToWorld.TransformPosition(Contact);
		// 0x126540
		const double Nv = FVector::DotProduct(Nw, Sim.State.Velocity);
		if (Nv > 0.0)
		{
			FRiderImpact Info;
			Info.Point = Cw;
			Info.Direction = Sim.State.Velocity.GetSafeNormal(1e-9, Sim.State.BasisForward);
			Info.Normal = Nw;
			Info.Speed = FVector::DotProduct(Nw, Sim.State.Velocity - N->Roller.Body.PointVelocity(Cw));
			SSXGround::WallImpact(Sim, Anim, Nv, Info, Nw, SSXAir::GetRenderedOrientation(Sim.State).RotateVector(FVector::UpVector));
		}
		const double RiderMass = SSXGround::RiderCollisionMass(Sim.Profile, Sim.State.BoostStrength);
		if (RiderHitsBody(N->Roller.Body, Sim.State.Velocity, RiderMass, Cw, Nw))
		{
			// 0x13F448
			if (N->Roller.bAsleep)
			{
				N->Roller.Wake();
				if (USSXTrackEffects* Te = USSXTrackEffects::Get(this)) { Te->TouchInstance(Sim, N->Inst); }
				if (Fx) { Fx->SetRollerAsleep(N->Inst, false); }
				// 0x216C50
				++Sim.State.PropHitCount;
				Sim.State.PropHitInstance = N->Inst;
				Sim.State.PropHitSpeed = Sim.State.Velocity.Size();
			}
		}
	}
}

int32 USSXPropAnimation::TumbleNeighbours(FRiderSim& Sim, const FRiderSphereSet& Shape, SSXWipeOut::FTumbleBody& Body)
{
	// 0x10ED68 / 0x13F448
	const TSharedPtr<FPatchWorld> W = SimWorld.Pin();
	if (!W || Nodes.Num() == 0)
	{
		return 0;
	}
	const FBox RiderBox(Shape.BoundCentre - FVector(Shape.BoundRadius), Shape.BoundCentre + FVector(Shape.BoundRadius));
	int32* Proxy = RiderProxies.Find(&Sim);
	if (!Proxy || !Moving.Get(*Proxy).bUsed)
	{
		const int32 Id = Moving.Add(INDEX_NONE, FMovingObjects::EKind::Rider, RiderBox);
		if (Id == INDEX_NONE)
		{
			return 0;
		}
		Proxy = &RiderProxies.Add(&Sim, Id);
	}
	Moving.Update(*Proxy, RiderBox);
	TArray<int32> Pairs;
	Moving.Pairs(*Proxy, Pairs);
	int32 Count = 0;
	for (int32 Id : Pairs)
	{
		const FMovingObjects::FProxy& Px = Moving.Get(Id);
		FNode* N = Px.Kind == FMovingObjects::EKind::Roller ? Nodes.Find(Px.Owner) : nullptr;
		if (!N || N->Type != SSXEffects::TypeRoller || N->TreeNode == INDEX_NONE)
		{
			continue;
		}
		const FSimInstanceNode& Node = W->GetInstanceNode(N->TreeNode);
		const FSimSphereTree& Tree = W->GetSphereTree(Node.Tree);
		const FMatrix ToWorld = Node.NodeToGame * RollerDelta(*N);
		const FMatrix ToNode = ToWorld.Inverse();
		FVector Push, Contact;
		if (!Tree.QuerySphere(ToNode.TransformPosition(Shape.BoundCentre), Shape.BoundRadius, Push, Contact))
		{
			continue;
		}
		bool bHit = false;
		for (int32 S = 0; S < Shape.Count && !bHit; ++S)
		{
			if ((Shape.Mask & (1 << S)) != 0)
			{
				bHit = Tree.QuerySphere(ToNode.TransformPosition(Shape.Centres[S]), Shape.Radii[S], Push, Contact);
			}
		}
		if (!bHit)
		{
			continue;
		}
		const FVector U = SSXSim::RsqrtNormal(ToWorld.TransformVector(Push));
		const FVector Cw = ToWorld.TransformPosition(Contact);
		const FVector Ra = Cw - Body.X;
		const double Vrel = FVector::DotProduct(Body.PointVelocity(Ra) - N->Roller.Body.PointVelocity(Cw), U);
		if (Vrel >= 0.0)
		{
			continue;
		}
		const double Ksum = Body.EffectiveInvMass(Ra, U) + N->Roller.Body.K(Cw - N->Roller.Body.X, U);
		const FVector J = U * (-(1.0 + SSXWipeOut::NeighbourRestitution) * Vrel / Ksum);
		Body.ApplyImpulse(J, Ra);
		N->Roller.Body.ApplyImpulse(Cw - N->Roller.Body.X, -J);
		++Count;
		if (N->Roller.bAsleep)
		{
			N->Roller.Wake();
			if (USSXTrackEffects* Te = USSXTrackEffects::Get(this)) { Te->TouchInstance(Sim, N->Inst); }
			if (Fx) { Fx->SetRollerAsleep(N->Inst, false); }
		}
	}
	return Count;
}

void USSXPropAnimation::BuildActors()
{
	bActorsBuilt = true;
	Actors.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const int32 Inst = SSXShowOff::InstanceOfActor(*It);
		if (Inst != INDEX_NONE)
		{
			Actors.FindOrAdd(Inst).Add(FActorRef{ *It, ObjectOfActor(*It), It->GetActorTransform() });
		}
	}
}

void USSXPropAnimation::ResetPose(int32 Inst)
{
	PendingDeltas.Remove(Inst);
	LastAnimWorld.Remove(Inst);
	PendingReset.Add(Inst);
}

void USSXPropAnimation::SetPoseDeltas(int32 Inst, TConstArrayView<FMatrix> GameDeltas)
{
	TArray<FActorRef>* List = Actors.Find(Inst);
	if (!List)
	{
		return;
	}
	for (FActorRef& R : *List)
	{
		AActor* A = R.Actor.Get();
		if (!A)
		{
			continue;
		}
		if (USceneComponent* Root = A->GetRootComponent(); Root && Root->Mobility != EComponentMobility::Movable)
		{
			Root->SetMobility(EComponentMobility::Movable);
		}
		if (A->GetAttachParentActor())
		{
			A->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		if (GameDeltas.Num() == 0)
		{
			A->SetActorTransform(R.Initial);
			continue;
		}
		const FMatrix& D = GameDeltas.IsValidIndex(R.Obj) ? GameDeltas[R.Obj] : GameDeltas[0];
		A->SetActorTransform(FTransform(R.Initial.ToMatrixWithScale() * GameToUnrealMatrix(D)));
	}
}

void USSXPropAnimation::ApplyRender()
{
	if (!GetWorld())
	{
		return;
	}
	if (!bActorsBuilt && (Controllers.Num() || Nodes.Num() || PendingReset.Num() || AnimData.IsLoaded() || MovieInsts.Num()))
	{
		BuildActors();
	}
	if (AnimData.IsLoaded())
	{
		ApplyCrowd();
		ApplyFlipbooks();
	}
	ApplyMovies();
	ApplyTextureOverrides();
	if (bActorsBuilt)
	{
		ApplyStrips();
	}
	for (int32 Inst : PendingReset)
	{
		if (!PendingDeltas.Contains(Inst)) { SetPoseDeltas(Inst, {}); }
	}
	PendingReset.Reset();
	for (const TPair<int32, TArray<FMatrix>>& P : PendingDeltas)
	{
		SetPoseDeltas(P.Key, P.Value);
	}
	for (auto It = PendingDeltas.CreateIterator(); It; ++It)
	{
		if (!Nodes.Contains(It.Key())) { It.RemoveCurrent(); }
	}
	for (const TPair<int32, FNode>& P : Nodes)
	{
		if (P.Value.Type != SSXEffects::TypeMeshAnim)
		{
			continue;
		}
		if (const TArray<FActorRef>* List = Actors.Find(P.Key))
		{
			for (const FActorRef& R : *List)
			{
				if (AActor* A = R.Actor.Get()) { A->SetActorHiddenInGame(!P.Value.Mesh.bActive); }
			}
		}
	}
	for (int32 Index = 0; Index < Controllers.Num(); ++Index)
	{
		const SSXProps::FPropController& C = Controllers.Get(Index);
		if (!C.HasScroll())
		{
			continue;
		}
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>>* MIDs = Materials.Find(C.Instance);
		if (!MIDs)
		{
			MIDs = &Materials.Add(C.Instance);
			if (const TArray<FActorRef>* List = Actors.Find(C.Instance))
			{
				for (const FActorRef& R : *List)
				{
					if (!R.Actor.IsValid())
					{
						continue;
					}
					TArray<UStaticMeshComponent*> Comps;
					R.Actor->GetComponents(Comps);
					for (UStaticMeshComponent* Comp : Comps)
					{
						for (int32 Slot = 0; Slot < Comp->GetNumMaterials(); ++Slot)
						{
							if (UMaterialInstanceDynamic* MID = Comp->CreateAndSetMaterialInstanceDynamic(Slot))
							{
								MIDs->Add(MID);
							}
						}
					}
				}
			}
		}
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& MID : *MIDs)
		{
			if (MID.IsValid())
			{
				MID->SetScalarParameterValue(ParamU, float(C.Scroll.Offset.X));
				MID->SetScalarParameterValue(ParamV, float(C.Scroll.Offset.Y));
			}
		}
	}
}

UTexture2D* USSXPropAnimation::CrowdAtlas()
{
	if (Atlas)
	{
		return Atlas;
	}
	const FString Dir = FPaths::Combine(SSXFrontEnd::TextureDir(), TEXT("CROWD"));
	constexpr int32 Cell = 128;
	FImage Out;
	Out.Init(Cell * 4, Cell * 4, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	FMemory::Memzero(Out.RawData.GetData(), Out.RawData.Num());
	int32 Loaded = 0;
	for (int32 K = 0; K < 16; ++K)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *FPaths::Combine(Dir, FString::Printf(TEXT("%03d_*.png"), K)), true, false);
		FImage Img;
		if (!Found.Num() || !FImageUtils::LoadImage(*FPaths::Combine(Dir, Found[0]), Img))
		{
			continue;
		}
		FImage Bgra;
		Img.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const int32 W = FMath::Min(Bgra.SizeX, Cell), H = FMath::Min(Bgra.SizeY, Cell);
		for (int32 Y = 0; Y < H; ++Y)
		{
			FMemory::Memcpy(&Out.RawData[((((K / 4) * Cell + Y) * Cell * 4) + (K % 4) * Cell) * 4], &Bgra.RawData[(Y * Bgra.SizeX) * 4], W * 4);
		}
		++Loaded;
	}
	if (Loaded == 0)
	{
		UE_LOG(LogSSX, Warning, TEXT("[props] crowd.ssh images not found in %s (tools/sshtex.py): crowds keep their texture"), *Dir);
		return nullptr;
	}
	Atlas = FImageUtils::CreateTexture2DFromImage(Out);
	if (Atlas)
	{
		Atlas->SRGB = true;
		Atlas->AddressX = TA_Clamp;
		Atlas->AddressY = TA_Clamp;
		Atlas->Filter = TF_Bilinear;
		Atlas->UpdateResource();
	}
	return Atlas;
}

void USSXPropAnimation::ApplyCrowd()
{
	if (!bCrowdBuilt)
	{
		bCrowdBuilt = true;
		CrowdMids.Reset();
		for (int32 M = 0; M < AnimData.Models.Num(); ++M)
		{
			if (!AnimData.Models[M].IsCrowd())
			{
				continue;
			}
			TArray<int32>& F = StaticCrowdFrames.Add(M);
			for (int32 K = 0; K < FCrowdBox::Slots; ++K) { F.Add(int32(RandU32() & 15)); }
		}
		if (!CrowdMaterial) { CrowdMaterial = LoadObject<UMaterialInterface>(nullptr, CrowdMaterialPath); }
		UTexture2D* Tex = CrowdMaterial ? CrowdAtlas() : nullptr;
		if (!Tex)
		{
			if (!CrowdMaterial) { UE_LOG(LogSSX, Warning, TEXT("[props] %s missing (Scripts/make_world_materials.py)"), CrowdMaterialPath); }
			return;
		}
		for (const TPair<int32, TArray<FActorRef>>& P : Actors)
		{
			const FPropModelInfo* Model = AnimData.ModelOfInstance(P.Key);
			if (!Model || !Model->IsCrowd())
			{
				continue;
			}
			TArray<TWeakObjectPtr<UMaterialInstanceDynamic>>& Mids = CrowdMids.Add(P.Key);
			for (const FActorRef& R : P.Value)
			{
				TArray<UStaticMeshComponent*> Comps;
				if (R.Actor.IsValid()) { R.Actor->GetComponents(Comps); }
				for (UStaticMeshComponent* Comp : Comps)
				{
					UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(CrowdMaterial, this);
					Mid->SetTextureParameterValue(TEXT("BaseTex"), Tex);
					for (int32 Slot = 0; Slot < Comp->GetNumMaterials(); ++Slot) { Comp->SetMaterial(Slot, Mid); }
					Mids.Add(Mid);
				}
			}
		}
		for (const TPair<int32, TArray<TWeakObjectPtr<UMaterialInstanceDynamic>>>& P : CrowdMids)
		{
			const TArray<int32>* F = StaticCrowdFrames.Find(AnimData.InstanceModel[P.Key]);
			for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Mid : P.Value)
			{
				for (int32 V = 0; V < 4 && F; ++V)
				{
					if (Mid.IsValid())
					{
						Mid->SetVectorParameterValue(*FString::Printf(TEXT("CrowdFrames%d"), V),
							FLinearColor(float((*F)[4 * V]), float((*F)[4 * V + 1]), float((*F)[4 * V + 2]), float((*F)[4 * V + 3])));
					}
				}
			}
		}
	}
	for (const TPair<int32, FNode>& P : Nodes)
	{
		if (P.Value.Type != SSXEffects::TypeCrowdBox)
		{
			continue;
		}
		const TArray<TWeakObjectPtr<UMaterialInstanceDynamic>>* Mids = CrowdMids.Find(P.Key);
		if (!Mids)
		{
			continue;
		}
		const int32* F = P.Value.Crowd.Frames;
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Mid : *Mids)
		{
			for (int32 V = 0; V < 4 && Mid.IsValid(); ++V)
			{
				Mid->SetVectorParameterValue(*FString::Printf(TEXT("CrowdFrames%d"), V),
					FLinearColor(float(F[4 * V]), float(F[4 * V + 1]), float(F[4 * V + 2]), float(F[4 * V + 3])));
			}
		}
	}
}

const FAnimObject* USSXPropAnimation::FindAnim(int32 Inst) const
{
	const FNode* N = Nodes.Find(Inst);
	return N && IsAnimType(N->Type) ? &N->Anim : nullptr;
}

int32 USSXPropAnimation::NumNodes(int32 Type) const
{
	int32 C = 0;
	for (const TPair<int32, FNode>& P : Nodes)
	{
		C += (Type < 0 || P.Value.Type == Type || (Type == SSXEffects::TypeAnimObject && IsAnimType(P.Value.Type)));
	}
	return C;
}
