// 0x10D6F8, 0x236530, 0x153AD8, 0x153C50, 0x154338, 0x153EE8, 0x153808, 0x10DAD8, 0x10E190, 0x10DCA8
#pragma once

#include "CoreMinimal.h"
#include "Sim/Rider/RiderShape.h"
#include "Sim/RiderWorld.h"

struct FRiderState;
struct FRiderProfile;
class FRiderSim;

namespace SSXWipeOut
{
	constexpr double BodyMass = 46.0;                 // 0x3A3F68
	constexpr float TumbleGravity = 1900.8359f;
	constexpr float TumbleLinearDrag = 0.5f;
	constexpr float TumbleAngularDrag = 0.5f;
	constexpr float HomingAccel = 600.0f;
	constexpr float HomingWindow = 0.15f;
	constexpr float InertiaScale = 1.2f;
	constexpr float InvInertiaScale = 0.8f;
	constexpr float MaxSpinRate = 23.561945f;
	constexpr float ContactMinSpeed = 100.0f;         // cm/s
	constexpr float ContactDamping = 0.95f;
	constexpr float ContactRestitutionBase = 0.8f;
	constexpr float ContactRestitutionK = 0.7f;
	constexpr float ContactFrictionK = 0.15f;
	constexpr float EntryRestitution = 0.15f;
	constexpr float InstanceRestitution = 0.5f;
	constexpr double NeighbourRestitution = 0.5;      // 0x10ED68
	constexpr float AirGrace = 0.1f;
	constexpr float SettlePhaseTime = 0.25f;
	constexpr float SettleSteepNz = 0.34906584f;
	constexpr float SettleFaceDot = 0.75f;

	FORCEINLINE double RoundF(double V) { return double(float(V)); }
	FORCEINLINE FVector RoundF(const FVector& V) { return FVector(float(V.X), float(V.Y), float(V.Z)); }
	FORCEINLINE FQuat RoundF(const FQuat& Q) { return FQuat(float(Q.X), float(Q.Y), float(Q.Z), float(Q.W)); }

	SSXTRICKYREBUILD_API FVector MassProperties(const FVector Centres[SSXRider::NumShapeSpheres], FMatrix& OutInvI);

	SSXTRICKYREBUILD_API void ContactCoefficients(double NormalZ, double Speed, double& OutE, double& OutMu);

	struct SSXTRICKYREBUILD_API FTumbleBody
	{
		bool bValid = false;
		double InvMass = 1.0 / BodyMass;
		FVector X = FVector::ZeroVector;
		FQuat Q = FQuat::Identity;
		FVector P = FVector::ZeroVector;
		FVector L = FVector::ZeroVector;
		FVector V = FVector::ZeroVector;
		FVector W = FVector::ZeroVector;
		FMatrix InvI = FMatrix::Identity;
		FVector CentreBody = FVector::ZeroVector;
		FVector BodyCentres[SSXRider::NumShapeSpheres];

		bool Init(const FRiderProfile& Profile, bool bGoofy, const FVector& RiderPos, const FQuat& Orientation, const FVector& Velocity,
		          const FVector* PosedCentres = nullptr);
		void InitFromSpheres(const FVector Spheres[SSXRider::NumShapeSpheres], const FQuat& Orientation, const FVector& Velocity);
		FVector RiderPosition() const { return X - Q.RotateVector(CentreBody); }
		void Integrate(const FVector& F, const FVector& Tau, double Dt);
		void Refresh();
		void RefreshNoCap();
		// 0x10C2F0
		void SpinFromL();
		void ApplyImpulse(const FVector& J, const FVector& R);
		double EffectiveInvMass(const FVector& R, const FVector& D) const;
		void ContactImpulse(const FVector& R, const FVector& N, double Restitution, double Friction);
		FVector PointVelocity(const FVector& R) const { return V + FVector::CrossProduct(W, R); }
		FVector Side() const { return Q.RotateVector(FVector(1, 0, 0)); }
		FVector Forward() const { return Q.RotateVector(FVector(0, 1, 0)); }
		FVector Up() const { return Q.RotateVector(FVector(0, 0, 1)); }
	};

	struct FTumbleContact
	{
		bool bHit = false;
		bool bImpulse = false;
		int32 Impulses = 0;
		FRiderWorldHit Hit;
		double T1 = 0.0, T2 = 0.0, T3 = 0.0;
		FVector N1 = FVector::ZeroVector, N2 = FVector::ZeroVector, N3 = FVector::ZeroVector;
		bool bClose1 = false, bClose2 = false, bClose3 = false;
	};

	SSXTRICKYREBUILD_API FVector MassFromSpheres(FTumbleBody& Body, const FVector Spheres[SSXRider::NumShapeSpheres]);

	SSXTRICKYREBUILD_API FTumbleContact TumbleContactPass(FRiderSim& Sim, FTumbleBody& Body, FVector Spheres[SSXRider::NumShapeSpheres], const FVector& GroundN);

	SSXTRICKYREBUILD_API bool TumbleBookkeeping(double& PhaseTime, double& AirTime, bool bClosingContact, bool bGroundCheckHit, double Dt);

	SSXTRICKYREBUILD_API int32 SettleDecision(double PhaseTime, bool bGroundCheckHit, int32 Surface, double Nz, double FwdN);

	inline double GetupSpeedLimit(double PhaseTime) { return FMath::Min(double(float(PhaseTime) * 694.4444f + 833.3333f), 2222.2222); }
}
