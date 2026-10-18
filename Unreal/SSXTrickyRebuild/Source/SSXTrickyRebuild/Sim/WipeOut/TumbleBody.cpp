#include "Sim/WipeOut/TumbleBody.h"
#include "Sim/SimCoords.h"

#include "Sim/Ground/WallCollision.h"
#include "Sim/RiderState.h"
#include "Sim/RiderSim.h"
#include "Sim/Ground/PropCollision.h"
#include "Sim/PatchWorld.h"
#include "Sim/EEFloat.h"

namespace SSXWipeOut
{
namespace
{
	using F3 = FVector3f;
	FORCEINLINE F3 ToF(const FVector& V) { return F3(float(V.X), float(V.Y), float(V.Z)); }
	FORCEINLINE FVector ToD(const F3& V) { return FVector(V.X, V.Y, V.Z); }

	// 0x153AD8, 0x10C2F0, 0x10E190
	FORCEINLINE F3 MulM(const FMatrix& M, const F3& V)
	{
		using SSXEE::FpuAdd;
		auto Row = [&](int32 C) { return FpuAdd(FpuAdd(float(M.M[0][C]) * V.X, float(M.M[1][C]) * V.Y), float(M.M[2][C]) * V.Z); };
		return F3(Row(0), Row(1), Row(2));
	}

	// 0x236530, 0x10D6F8
	FVector MassPropertiesF(const FVector Centres[SSXRider::NumShapeSpheres], FMatrix& OutInvI, float& OutMass)
	{
		using SSXEE::FpuAdd;
		using SSXEE::FpuSub;
		float M = 0.0f;
		F3 C = F3::ZeroVector;
		for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I)
		{
			const float Mi = float(SSXRider::SphereMass[I]);
			M = FpuAdd(M, Mi);
			C += ToF(Centres[I]) * Mi;
		}
		C *= 1.0f / M;
		float In[9] = {};
		for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I)
		{
			const F3 R = ToF(Centres[I]) - C;
			const float Mi = float(SSXRider::SphereMass[I]);
			const float Yz = Mi * (R.Y * R.Z), Xz = Mi * (R.X * R.Z), Xy = Mi * (R.X * R.Y);
			In[0] = FpuAdd(In[0], Mi * FpuAdd(R.Y * R.Y, R.Z * R.Z));
			In[4] = FpuAdd(In[4], Mi * FpuAdd(R.Z * R.Z, R.X * R.X));
			In[8] = FpuAdd(In[8], Mi * FpuAdd(R.X * R.X, R.Y * R.Y));
			In[1] = FpuSub(In[1], Xy); In[2] = FpuSub(In[2], Xz); In[5] = FpuSub(In[5], Yz);
			In[3] = FpuSub(In[3], Xy); In[6] = FpuSub(In[6], Xz); In[7] = FpuSub(In[7], Yz);
		}
		float A[9];
		for (int32 K = 0; K < 9; ++K) { A[K] = In[K] * InertiaScale; }
		const float C00 = FpuSub(A[4] * A[8], A[5] * A[7]);
		const float C10 = FpuSub(A[7] * A[2], A[8] * A[1]);
		const float C20 = FpuSub(A[1] * A[5], A[2] * A[4]);
		const float InvDet = 1.0f / FpuAdd(FpuAdd(A[0] * C00, A[3] * C10), A[6] * C20);
		float Inv[9];
		Inv[0] = C00 * InvDet;
		Inv[1] = C10 * InvDet;
		Inv[2] = C20 * InvDet;
		Inv[3] = FpuSub(A[6] * A[5], A[8] * A[3]) * InvDet;
		Inv[4] = FpuSub(A[0] * A[8], A[2] * A[6]) * InvDet;
		Inv[5] = FpuSub(A[3] * A[2], A[5] * A[0]) * InvDet;
		Inv[6] = FpuSub(A[3] * A[7], A[4] * A[6]) * InvDet;
		Inv[7] = FpuSub(A[6] * A[1], A[7] * A[0]) * InvDet;
		Inv[8] = FpuSub(A[0] * A[4], A[1] * A[3]) * InvDet;
		OutInvI = FMatrix::Identity;
		for (int32 R = 0; R < 3; ++R)
		{
			for (int32 Col = 0; Col < 3; ++Col)
			{
				OutInvI.M[R][Col] = double(Inv[R * 3 + Col] * InvInertiaScale);
			}
		}
		OutMass = M;
		return ToD(C);
	}
}

FVector MassProperties(const FVector Centres[SSXRider::NumShapeSpheres], FMatrix& OutInvI)
{
	float M;
	return MassPropertiesF(Centres, OutInvI, M);
}

void ContactCoefficients(double NormalZ, double Speed, double& OutE, double& OutMu)
{
	if (float(Speed) < ContactMinSpeed)
	{
		OutE = OutMu = 0.0;
		return;
	}
	const float Az = FMath::Abs(float(NormalZ));
	OutE = double(SSXEE::FpuSub(ContactRestitutionBase, Az * ContactRestitutionK));
	OutMu = double(Az * ContactFrictionK);
}

bool FTumbleBody::Init(const FRiderProfile& Profile, bool bGoofy, const FVector& RiderPos, const FQuat& Orientation, const FVector& Velocity,
                       const FVector* PosedCentres)
{
	if (PosedCentres)
	{
		FMemory::Memcpy(BodyCentres, PosedCentres, sizeof(BodyCentres));
		bValid = true;
	}
	else
	{
		bValid = SSXRider::BodyCentres(Profile, bGoofy, BodyCentres);
	}
	if (!bValid)
	{
		return false;
	}
	FVector World[SSXRider::NumShapeSpheres];
	const FQuat Qn = Orientation.GetNormalized();
	for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I)
	{
		World[I] = RiderPos + Qn.RotateVector(BodyCentres[I]);
	}
	InitFromSpheres(World, Qn, Velocity);
	CentreBody = Qn.Inverse().RotateVector(X - RiderPos);
	return true;
}

void FTumbleBody::InitFromSpheres(const FVector Spheres[SSXRider::NumShapeSpheres], const FQuat& Orientation, const FVector& Velocity)
{
	bValid = true;
	// 0x10D6F8
	const FVector C = MassFromSpheres(*this, Spheres);
	X = RoundF(C);
	Q = RoundF(Orientation);
	P = ToD(ToF(Velocity) * (1.0f / float(InvMass)));
	V = RoundF(Velocity);
	W = FVector::ZeroVector;          // 0x153808
	L = FVector::ZeroVector;
}

void FTumbleBody::RefreshNoCap()
{
	V = ToD(ToF(P) * float(InvMass));
	W = ToD(MulM(InvI, ToF(L)));
}

void FTumbleBody::SpinFromL()
{
	W = ToD(MulM(InvI, ToF(L)));
}

void FTumbleBody::Refresh()
{
	RefreshNoCap();
	const float Spin = ToF(W).Size();
	if (Spin > MaxSpinRate)
	{
		const float K = MaxSpinRate / Spin;
		W = ToD(ToF(W) * K);
		L = ToD(ToF(L) * K);
	}
}

void FTumbleBody::Integrate(const FVector& F, const FVector& Tau, double Dt)
{
	const float H = float(Dt);
	X = ToD(ToF(X) + ToF(V) * H);
	using SSXEE::FpuAdd;
	using SSXEE::FpuSub;
	const float Wx = float(W.X), Wy = float(W.Y), Wz = float(W.Z);
	const float Qw = float(Q.W), Qx = float(Q.X), Qy = float(Q.Y), Qz = float(Q.Z);
	float Nw = FpuAdd(Qw, FpuSub(FpuSub(-Wx * Qx, Wy * Qy), Wz * Qz) * 0.5f * H);
	float Nx = FpuAdd(Qx, FpuSub(FpuAdd(Wx * Qw, Wy * Qz), Wz * Qy) * 0.5f * H);
	float Ny = FpuAdd(Qy, FpuSub(FpuAdd(Wy * Qw, Wz * Qx), Wx * Qz) * 0.5f * H);
	float Nz = FpuAdd(Qz, FpuSub(FpuAdd(Wz * Qw, Wx * Qy), Wy * Qx) * 0.5f * H);
	const float Len = float(FMath::Sqrt(double(FpuAdd(FpuAdd(FpuAdd(Nx * Nx, Ny * Ny), Nz * Nz), Nw * Nw))));
	if (Len == 0.0f)
	{
		Nw = 1.0f;
	}
	else
	{
		const float Il = 1.0f / Len;
		Nw *= Il; Nx *= Il; Ny *= Il; Nz *= Il;
	}
	Q = FQuat(Nx, Ny, Nz, Nw);
	P = ToD(ToF(P) + ToF(F) * H);
	L = ToD(ToF(L) + ToF(Tau) * H);
}

void FTumbleBody::ApplyImpulse(const FVector& J, const FVector& R)
{
	P = ToD(ToF(P) + ToF(J));
	L = ToD(ToF(L) + F3::CrossProduct(ToF(R), ToF(J)));
	RefreshNoCap();
}

double FTumbleBody::EffectiveInvMass(const FVector& R, const FVector& D) const
{
	const F3 Rf = ToF(R), Df = ToF(D);
	const F3 Iv = MulM(InvI, F3::CrossProduct(Rf, Df));
	return double(float(InvMass) + F3::DotProduct(Df, F3::CrossProduct(Iv, Rf)));
}

void FTumbleBody::ContactImpulse(const FVector& R, const FVector& N, double Restitution, double Friction)
{
	const F3 Rf = ToF(R), Nf = ToF(N);
	auto Vc = [&]() { return ToF(V) + F3::CrossProduct(ToF(W), Rf); };
	float Vn = F3::DotProduct(Vc(), Nf);
	if (float(Friction) != 0.0f)
	{
		const F3 Vt = Vc() - Nf * Vn;
		const float SpeedT = Vt.Size();
		if (SpeedT > 0.0f)
		{
			const F3 T = Vt * (1.0f / SpeedT);
			const float K = float(EffectiveInvMass(R, ToD(T)));
			ApplyImpulse(ToD(T * ((-float(Friction) * SpeedT) / K)), R);
		}
		Vn = F3::DotProduct(Vc(), Nf);
		if (0.0f < Vn)
		{
			return;
		}
	}
	const float K = float(EffectiveInvMass(R, N));
	ApplyImpulse(ToD(Nf * ((-(float(Restitution) + 1.0f) * Vn) / K)), R);
}

FVector MassFromSpheres(FTumbleBody& Body, const FVector Spheres[SSXRider::NumShapeSpheres])
{
	float M;
	const FVector C = MassPropertiesF(Spheres, Body.InvI, M);
	Body.InvMass = double(1.0f / M);
	Body.Refresh();   // 0x153AD8
	return C;
}

namespace
{
	// 0x110830, 0x236448
	void PushAll(FTumbleBody& Body, FVector Spheres[SSXRider::NumShapeSpheres], FRiderSphereSet& Shape, double T, const FVector& M)
	{
		using namespace SSXEE;
		const FVector3f Dv = Scale(float(T), F(M));   // 0x102F50
		Body.X = D(Add(F(Body.X), Dv));
		Shape.BoundCentre = D(Add(F(Shape.BoundCentre), Dv));
		for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I)
		{
			Spheres[I] = D(Add(F(Spheres[I]), Dv));
			Shape.Centres[I] = Spheres[I];
		}
	}

	// 0x10E190
	bool Closing(const FTumbleBody& Body, const FVector& Point, const FVector& M, FVector& OutR)
	{
		using namespace SSXEE;
		const FVector3f R = Sub(F(Point), F(Body.X));
		OutR = D(R);
		return Dot(Add(F(Body.V), Cross(F(Body.W), R)), F(M)) < 0.0f;
	}

	double SpeedOf(const FVector& V)
	{
		return double(SSXEE::Length(SSXEE::F(V)));
	}

	FVector FoldOff(const FVector& U, const FVector& M)
	{
		using namespace SSXEE;
		const FVector3f Uf = F(U), Mf = F(M);
		const FVector3f T = Sub(Uf, Scale(Dot(Uf, Mf), Mf));
		const float L2 = Dot(T, T);
		return L2 > 0.0f ? D(Scale(Rsqrt(L2), T)) : FVector::ZeroVector;
	}

	// 0x257810, 0x236830
	void MakeShape(const FRiderProfile& Profile, const FVector Spheres[SSXRider::NumShapeSpheres], const FVector& BoundCentre, FRiderSphereSet& Out)
	{
		Out = FRiderSphereSet();
		Out.Count = SSXRider::NumShapeSpheres;
		for (int32 I = 0; I < SSXRider::NumShapeSpheres; ++I)
		{
			Out.Centres[I] = Spheres[I];
			Out.Radii[I] = SSXRider::SphereRadius[I];
		}
		Out.BoundCentre = BoundCentre;
		Out.BoundRadius = SSXRider::BoundRadiusPerScale * Profile.SizeScale;
		Out.Mask = SSXRider::MaskTumble;
		Out.bFineQuery = true;
		const FBox Box = FPatchWorld::SphereSetBox(Out);
		Out.bHasQueryBox = true;
		Out.QueryBoxMin = Box.Min;
		Out.QueryBoxMax = Box.Max;
	}

	void ContactDamp(FTumbleBody& Body)
	{
		using namespace SSXEE;
		const FVector3f P = Scale(ContactDamping, F(Body.P));
		Body.P = D(P);
		Body.V = D(Scale(float(Body.InvMass), P));
		const FVector3f L = Scale(ContactDamping, F(Body.L));
		Body.L = D(L);
		const FMatrix& M = Body.InvI;
		auto Row = [&](int32 C) { return FpuAdd(FpuAdd(float(M.M[0][C]) * L.X, float(M.M[1][C]) * L.Y), float(M.M[2][C]) * L.Z); };
		Body.W = FVector(Row(0), Row(1), Row(2));
	}
}

FTumbleContact TumbleContactPass(FRiderSim& Sim, FTumbleBody& Body, FVector Spheres[SSXRider::NumShapeSpheres], const FVector& GroundN)
{
	FTumbleContact Out;
	FRiderState& St = Sim.State;
	const IRiderWorld* World = Sim.GetWorld();
	if (!World)
	{
		return Out;
	}
	// 0x257810
	const FVector Bound = 0.5 * (Spheres[8] + Spheres[9]);
	FRiderSphereSet Shape;
	FVector M1 = FVector::ZeroVector;
	// 0x25AFF8
	MakeShape(Sim.Profile, Spheres, Bound, Shape);
	FRiderWorldHit Hit;
	if (World->QueryWalls(Shape, /*bFine=*/true, nullptr, Hit) && Hit.T > 0.0)
	{
		Out.bHit = true;
		const FVector Mn = RoundF(Hit.Normal);
		PushAll(Body, Spheres, Shape, Hit.T, Mn);
		FVector R;
		Out.T1 = Hit.T; Out.N1 = Mn;
		Out.bClose1 = Closing(Body, Hit.Point, Mn, R);
		if (Out.bClose1)
		{
			SSXGround::FeedStuckA(St, Mn);
			// 0x11A798, 0x136E70, 0x135258, 0x12E008, 0x1532C0, 0x150A80
			++St.TumbleImpactCount;
			St.TumbleImpactPoint = Hit.Point;
			St.TumbleImpactVelocity = Body.V;
			St.TumbleImpactNormal = Mn;
			St.AddPadImpact(Body.V, Mn);
			St.AddFxImpact(Hit.Point, Body.V, Mn);
			double E, Mu;
			ContactCoefficients(Mn.Z, SpeedOf(Body.V), E, Mu);
			Body.ContactImpulse(R, Mn, E, Mu);
			ContactDamp(Body);
			Out.bImpulse = true;
			++Out.Impulses;
			Out.Hit = Hit;
			Out.Hit.Normal = Mn;
		}
		M1 = Mn;
		FRiderWorldHit Hit2;
		if (World->QueryWalls(Shape, /*bFine=*/true, &M1, Hit2) && Hit2.T > 0.0)
		{
			FVector M2 = RoundF(Hit2.Normal);
			if (SSXEE::Dot(SSXEE::F(M2), SSXEE::F(M1)) < 0.0f)
			{
				M2 = FoldOff(M2, M1);
			}
			PushAll(Body, Spheres, Shape, Hit2.T, M2);
			FVector R2;
			Out.T2 = Hit2.T; Out.N2 = M2;
			Out.bClose2 = Closing(Body, Hit2.Point, M2, R2);
			if (Out.bClose2)
			{
				SSXGround::FeedStuckA(St, M2);
				double E, Mu;
				ContactCoefficients(M2.Z, SpeedOf(Body.V), E, Mu);
				Body.ContactImpulse(R2, M2, E, Mu);
				ContactDamp(Body);
				Out.bImpulse = true;
				++Out.Impulses;
				Out.Hit = Hit2;
				Out.Hit.Normal = M2;
			}
		}
	}
	// 0x25B878
	{
		SSXGround::FPropContactEvent Ev;
		if (World->QueryProps(Shape, Ev.Hit, Ev.Prop) && Ev.Hit.T > 0.0)
		{
			const FVector Mn = FoldOff(Ev.Hit.Normal, GroundN);
			const bool bSolid = (Ev.Prop.Flags & 0x80u) != 0;
			if (bSolid)
			{
				PushAll(Body, Spheres, Shape, Ev.Hit.T, Mn);
			}
			FVector R;
			Out.T3 = Ev.Hit.T; Out.N3 = Mn;
			Out.bClose3 = Closing(Body, Ev.Hit.Point, Mn, R);
			if (Out.bClose3)
			{
				// 0x13BD40
				Ev.Contact.Point = Ev.Hit.Point;
				Ev.Contact.Normal = Mn;
				Ev.Contact.Direction = SSXSim::RsqrtNormal(Body.V, St.BasisForward);
				Ev.Contact.Speed = SpeedOf(Body.V);
				Ev.TouchEffect = Ev.Prop.TouchEffect;
				Ev.bHitSound = true;
				if (Sim.OnPropContact)
				{
					Sim.OnPropContact(Sim, Ev);
				}
				if (bSolid)
				{
					SSXGround::FeedStuckA(St, Mn);
					Body.ContactImpulse(R, Mn, InstanceRestitution, 0.0);
					++Out.Impulses;
				}
				// 0x216C50
				++St.PropHitCount;
				St.PropHitInstance = Ev.Hit.InstanceIndex;
				St.PropHitSpeed = SpeedOf(Body.V);
			}
		}
	}
	// 0x10ED68, 0x10EBC8
	if (Sim.OnTumbleNeighbours)
	{
		Out.Impulses += Sim.OnTumbleNeighbours(Sim, Shape, Body);
	}
	return Out;
}

bool TumbleBookkeeping(double& PhaseTime, double& AirTime, bool bClosingContact, bool bGroundCheckHit, double Dt)
{
	const float H = float(Dt);
	if (bClosingContact)
	{
		PhaseTime = double(float(PhaseTime) + H);
		AirTime = 0.0;
		return true;
	}
	const float Air = float(AirTime);
	bool bGrounded = false;
	if (Air > AirGrace || !bGroundCheckHit)
	{
		PhaseTime = 0.0;
	}
	else
	{
		PhaseTime = double(float(PhaseTime) + H);
		bGrounded = true;
	}
	AirTime = double(Air + H);
	return bGrounded;
}

int32 SettleDecision(double PhaseTime, bool bGroundCheckHit, int32 Surface, double Nz, double FwdN)
{
	if (!bGroundCheckHit || !(float(PhaseTime) > SettlePhaseTime) || Surface == 6 || Surface == 10 || float(Nz) <= SettleSteepNz)
	{
		return 0;
	}
	if (float(FwdN) > SettleFaceDot) { return 0x2DC; }
	if (float(FwdN) < -SettleFaceDot) { return 0x2DD; }
	return 1;
}
}
