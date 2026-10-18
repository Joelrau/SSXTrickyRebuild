#include "Particles/SSXLensFX.h"

#include "Camera/RaceCamera.h"
#include "Sim/SimCoords.h"
#include "World/SSXWorldLighting.h"

namespace SSXLensFX
{
	const double (&GlareRays())[NumGlareRays][2]
	{
		static const double Rays[NumGlareRays][2] = {
			{ 0.0, 0.2857 }, { 0.2618, 0.4762 }, { 0.4538, 0.7619 }, { 0.5411, 0.5714 }, { 0.6981, 0.2857 },
			{ 0.9599, 0.4286 }, { 1.1694, 0.5238 }, { 1.3439, 0.8095 }, { 1.4137, 0.7143 }, { 1.5184, 0.381 },
			{ 1.7453, 0.8095 }, { 1.8151, 0.6667 }, { 1.9373, 0.8571 }, { 1.9897, 0.2381 }, { 2.2689, 0.4286 },
			{ 2.4784, 0.7143 }, { 2.5831, 0.8095 }, { 2.6529, 0.5714 }, { 2.81, 0.2857 }, { 3.0718, 0.1905 },
			{ 3.3685, 0.3333 }, { 3.6128, 0.7143 }, { 3.7176, 0.619 }, { 3.8572, 0.7143 }, { 3.9619, 0.2381 },
			{ 4.2411, 0.4286 }, { 4.4506, 0.3333 }, { 4.6949, 0.7143 }, { 4.7997, 0.4286 }, { 5.0091, 0.6667 },
			{ 5.1313, 0.4762 }, { 5.3233, 0.7619 }, { 5.4105, 0.5238 }, { 5.5851, 0.1905 }, { 5.8818, 0.7619 },
			{ 5.969, 0.5238 }, { 6.1436, 0.619 } };
		return Rays;
	}

	bool HaloClass(uint32 Flags, FHaloClass& Out)
	{
		switch (Flags & 0x70)
		{
		case 0x10: Out = { 100.73963, 300.0 }; return true;
		case 0x20: Out = { 200.07841, 500.0 }; return true;
		case 0x40: Out = { 500.0, 800.0 }; return true;
		default: return false;
		}
	}

	FParams FParams::From(const FSSXTrackPresentation& T)
	{
		FParams P;
		P.bSunOn = T.bFlareOn;
		P.GlareRGB = T.GlareRGB;
		P.SunRGB = T.SunRGB;
		P.GlareAlpha = T.GlareAlpha;
		P.SunRadius = T.SunRadius;
		P.SunAlpha = T.SunAlpha;
		P.AzimuthDeg = T.SunAzimuthDeg;
		P.ElevationDeg = T.SunElevationDeg;
		P.SunDist = T.SunDistance;
		P.RingBase = T.HaloRingBase;
		P.StreakBase = T.HaloStreakBase;
		P.HaloAlpha = T.HaloAlpha;
		P.HaloRange = T.HaloRange;
		// 0x1C72E8
		P.Far = SSXCamera::ProjectionFar(float(T.DrawDist1P), float(T.DrawDist2P), 1, 4, 0);
		return P;
	}

	bool FCamera::Project(const FVector& P, FVector2D& OutNdc, double& OutDepth) const
	{
		const FVector V = P - Pos;
		OutDepth = FVector::DotProduct(V, Fwd);
		if (OutDepth <= 0.0)
		{
			return false;
		}
		OutNdc = FVector2D(FVector::DotProduct(V, Right) * CotH / OutDepth, -FVector::DotProduct(V, Up) * CotV / OutDepth);
		return true;
	}

	bool FCamera::InFrustum(const FVector& P) const
	{
		FVector2D N;
		double Z;
		return Project(P, N, Z) && Z > Near && Z < Far && FMath::Abs(N.X) <= 1.0 && FMath::Abs(N.Y) <= 1.0;
	}

	FVector FCamera::Unproject(const FVector2D& Ndc, double D) const
	{
		return Pos + Fwd * D + Right * (Ndc.X * D / CotH) - Up * (Ndc.Y * D / CotV);
	}

	void InsertHalo(TArray<FHaloPick>& Slots, int32 Light, double Depth)
	{
		if (Slots.Num() < MaxHalos)
		{
			Slots.Add({ Light, Depth });
			return;
		}
		double Max = 0.0;
		int32 Idx = INDEX_NONE;
		for (int32 I = 0; I < Slots.Num(); ++I)
		{
			if (Slots[I].Depth > Max) { Max = Slots[I].Depth; Idx = I; }
		}
		if (Max <= Depth || Idx == INDEX_NONE)
		{
			return;
		}
		Slots[Idx] = { Light, Depth };
	}

	void CollectHalos(const TArray<int32>& Sequence, const TArray<FSSXLight>& Lights, const FCamera& Cam, double Range,
		TArray<FHaloPick>& Out)
	{
		Out.Reset();
		for (const int32 Li : Sequence)
		{
			if (!Lights.IsValidIndex(Li) || (Lights[Li].Flags & 0x70) == 0)
			{
				continue;
			}
			const FVector P = SSXSim::GameToUnreal(Lights[Li].Position);
			if (!Cam.InFrustum(P))
			{
				continue;
			}
			const double Depth = FVector::DotProduct(P - Cam.Pos, Cam.Fwd);
			if (Depth < Range)
			{
				InsertHalo(Out, Li, Depth);
			}
		}
	}

	double CentreWeight(const FVector2D& Ndc)
	{
		const double K = (1.41422 - FMath::Sqrt(Ndc.X * Ndc.X + Ndc.Y * Ndc.Y)) * 0.70710355;
		const double K2 = K * K;
		return K2 * K2;
	}

	FVector HaloSizes(double Radius, const FVector2D& Ndc, double RingBase, double StreakBase)
	{
		const double K4 = CentreWeight(Ndc);
		return FVector(Radius, Radius * (RingBase + K4 * 1.0095813), Radius * (StreakBase + K4 * 1.2522545));
	}

	double StreakAngle(double NdcX) { return -(NdcX * UE_DOUBLE_PI * 0.5); }

	double DistanceFade(double Depth, double Range)
	{
		const double H = Range * 0.5;
		return H < Depth ? (H - (Depth - H)) / H : 1.0;
	}

	FVector HaloColour(const FVector& Rgb, double Intensity)
	{
		const FVector C = Rgb * Intensity;
		const double N = C.Size();
		return N == 0.0 ? C : C / N;
	}

	FVector SunDirectionGame(double AzimuthDeg, double ElevationDeg)
	{
		const double A = FMath::DegreesToRadians(AzimuthDeg), E = FMath::DegreesToRadians(ElevationDeg);
		return FVector(FMath::Cos(A) * FMath::Cos(E), FMath::Sin(A) * FMath::Cos(E), FMath::Sin(E));
	}

	FVector2D SunDistanceAndSize(double Dist, double Radius, double Far)
	{
		return Far < Dist ? FVector2D(Far - 1.0, Radius * ((Far - 1.0) / Dist)) : FVector2D(Dist, Radius);
	}

	double Visibility(int32 Counted, int32 Inside, int32 Total)
	{
		if (Total <= 0)
		{
			return 0.0;
		}
		if (Inside == Total)
		{
			return double(Counted) / double(Total);
		}
		const int32 Half = Total >> 1;
		return FMath::Max(double(Counted - Half) / double(Half), 0.0);
	}

	FIntPoint BlockOrigin(const FVector2D& Px, int32 W, int32 H)
	{
		const int32 X = FMath::Clamp(int32(Px.X - W * 0.5), 0, int32(Ps2ViewW) - W);
		const int32 Y = FMath::Clamp(int32(Px.Y - H * 0.5), 0, int32(Ps2ViewH) - H);
		return FIntPoint(X, Y);
	}

	namespace
	{
		double Angle(double Dx, double Dy)
		{
			double A;
			if (Dx > 0.0) { A = FMath::Atan(Dy / Dx); }
			else if (Dx < 0.0) { A = FMath::Atan(Dy / Dx) + UE_DOUBLE_PI; }
			else { A = Dy > 0.0 ? UE_DOUBLE_HALF_PI : -UE_DOUBLE_HALF_PI; }
			return A < 0.0 ? A + UE_DOUBLE_TWO_PI : A;
		}
	}

	void GlareFan(const FVector2D& S, TArray<FFanPoint>& Out)
	{
		Out.Reset();
		static const double Corners[4][2] = { { 1.0, 1.0 }, { -1.0, 1.0 }, { -1.0, -1.0 }, { 1.0, -1.0 } };
		double CornerAngle[4];
		for (int32 K = 0; K < 4; ++K) { CornerAngle[K] = Angle(Corners[K][0] - S.X, Corners[K][1] - S.Y); }
		const auto& Rays = GlareRays();
		int32 Ray = 0, Corner = 0;
		while (Ray < NumGlareRays)
		{
			double A, W;
			if (Corner < 4 && CornerAngle[Corner] < Rays[Ray][0])
			{
				A = CornerAngle[Corner++];
				W = Rays[FMath::Max(Ray - 1, 0)][1];
			}
			else
			{
				A = Rays[Ray][0];
				W = Rays[Ray][1];
				++Ray;
			}
			const double Dx = FMath::Cos(A), Dy = FMath::Sin(A);
			double Tx = 10.0, Ty = 10.0;
			if (Dx > 0.0) { Tx = (1.0 - S.X) / Dx; } else if (Dx < 0.0) { Tx = (-1.0 - S.X) / Dx; }
			if (Dy > 0.0) { Ty = (1.0 - S.Y) / Dy; } else if (Dy < 0.0) { Ty = (-1.0 - S.Y) / Dy; }
			FVector2D E;
			if (Tx < Ty) { E = FVector2D(Dx > 0.0 ? 1.0 : -1.0, S.Y + Tx * Dy); }
			else { E = FVector2D(S.X + Ty * Dx, Dy > 0.0 ? 1.0 : -1.0); }
			const double D2 = FVector2D::DistSquared(E, S);
			Out.Add({ E, W, (3.0 - FMath::Min(D2, 3.0)) / 3.0 });
		}
		if (Out.Num())
		{
			const FFanPoint First = Out[0];
			Out.Add(First);
		}
	}

	FVector4f QuadrantUV(int32 Q)
	{
		const float U0 = (Q == 1) ? 128.f : 0.f, V0 = (Q == 2) ? 128.f : 0.f;
		return FVector4f(U0 / 256.f, V0 / 256.f, (U0 + 127.f) / 256.f, (V0 + 127.f) / 256.f);
	}

	void BuildFrame(const FParams& P, const TArray<FSSXLight>& Lights, const TArray<int32>& HaloSequence, const FCamera& Cam,
		FLensFrame& Out)
	{
		Out.Reset();
		// TODO: the callback's element order is read from the slot order;
		CollectHalos(HaloSequence, Lights, Cam, P.HaloRange, Out.Halos);
		for (const FHaloPick& H : Out.Halos)
		{
			const FSSXLight& L = Lights[H.Light];
			FHaloClass Cls;
			if (!HaloClass(L.Flags, Cls))
			{
				continue;
			}
			const FVector Pos = SSXSim::GameToUnreal(L.Position);
			FVector2D Ndc;
			double Depth;
			if (!Cam.Project(Pos, Ndc, Depth))
			{
				continue;
			}
			const FVector Size = HaloSizes(Cls.Radius, Ndc, P.RingBase, P.StreakBase);
			const FVector C = HaloColour(L.Colour, L.Intensity);
			const double Fade = DistanceFade(H.Depth, P.HaloRange);
			const FOcclusion Occ{ Pos, Cls.PullIn, HaloBlockW, HaloBlockH };
			auto Col = [&](double Scale) { return FColor(GsByte(C.X), GsByte(C.Y), GsByte(C.Z), GsByte(Fade * Scale * P.Intensity)); };
			Out.Sprites.Add({ Pos, Size.X, QuadrantUV(0), Col(P.HaloAlpha.X), Occ });
			Out.Sprites.Add({ Pos, Size.Y, QuadrantUV(1), Col(P.HaloAlpha.Y), Occ });
			const double A = StreakAngle(Ndc.X);
			const FVector Ax = (Cam.Right * FMath::Cos(A) + Cam.Up * FMath::Sin(A)) * Size.Z;
			const FVector Bx = (Cam.Right * FMath::Sin(A) - Cam.Up * FMath::Cos(A)) * Size.Z;
			FQuad& Q = Out.Streaks.AddDefaulted_GetRef();
			Q.Corner[0] = Pos - Ax - Bx;
			Q.Corner[1] = Pos + Ax - Bx;
			Q.Corner[2] = Pos - Ax + Bx;
			Q.Corner[3] = Pos + Ax + Bx;
			Q.UV = QuadrantUV(2);
			Q.Colour = Col(P.HaloAlpha.Z);
			Q.Occ = Occ;
		}
		if (!P.bSunOn)
		{
			return;
		}
		const FVector2D DR = SunDistanceAndSize(P.SunDist, P.SunRadius, Cam.Far);
		const FVector S = Cam.Pos + SSXSim::GameToUnreal(SunDirectionGame(P.AzimuthDeg, P.ElevationDeg)) * DR.X;
		FVector2D SunNdc;
		double SunDepth;
		if (!Cam.InFrustum(S) || !Cam.Project(S, SunNdc, SunDepth))
		{
			return;
		}
		Out.bSun = true;
		const FOcclusion Occ{ S, 0.0, SunBlockW, SunBlockH };
		auto Rgb = [](const FIntVector& B, double A) { return FColor(GsByte(B.X / 255.0), GsByte(B.Y / 255.0), GsByte(B.Z / 255.0), GsByte(A)); };
		Out.Sprites.Add({ S, DR.Y, QuadrantUV(1), Rgb(P.SunRGB, P.SunAlpha * P.Intensity), Occ });
		TArray<FFanPoint> Pts;
		GlareFan(SunNdc, Pts);
		const FVector Centre = Cam.Unproject(SunNdc, FanPlaneDepth);
		const double K = P.GlareAlpha * P.Intensity;
		for (int32 I = 0; I + 1 < Pts.Num(); ++I)
		{
			FTri& T = Out.Fan.AddDefaulted_GetRef();
			T.P[0] = Centre;
			T.P[1] = Cam.Unproject(Pts[I].Ndc, FanPlaneDepth);
			T.P[2] = Cam.Unproject(Pts[I + 1].Ndc, FanPlaneDepth);
			const double W = K * Pts[I].Weight;
			T.C[0] = Rgb(P.GlareRGB, W);
			T.C[1] = Rgb(P.GlareRGB, W * Pts[I].Falloff);
			T.C[2] = Rgb(P.GlareRGB, W * Pts[I + 1].Falloff);
			T.Occ = Occ;
		}
	}
}
