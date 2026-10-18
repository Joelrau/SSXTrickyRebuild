#include "Characters/SSXRiderDepth.h"

namespace SSXRiderDepth
{
	double PullD(double Wp, double Near, double Far, double Pull)
	{
		if (Pull <= 0.0 || Wp - Pull < Near || Wp > Far)
		{
			return 0.0;
		}
		return 1.0 / (Wp - Pull) - 1.0 / Wp;
	}

	double VertexScale(double W, double D)
	{
		return 1.0 / (1.0 + D * W);
	}

	double GsZOffset(double Wp, double Near, double Far, double Pull)
	{
		if (Pull <= 0.0 || Wp - Pull < Near || Wp > Far)
		{
			return 0.0;
		}
		// 0x1DC3E0
		const double A = (Far + Near) / (Far - Near);
		const double B = 2.0 * Far * Near / (Far - Near);
		auto Zw = [A, B](double W) { return A - B / W; };
		return (Zw(Wp - Pull) - Zw(Wp)) * ZScale;
	}

	bool VertexClipped(double W, double Near, double Far)
	{
		return !(W >= Near && W <= Far);
	}

	bool VertexDropped(const FVector& Rel, const FVector& Fwd, double Pull, double Near, double Far, bool bOrtho)
	{
		return Pull > 0.0 && !bOrtho && VertexClipped(FVector::DotProduct(Rel, Fwd), Near, Far);
	}

	FVector2D GuardNdc(double ViewTop, double ViewHeight)
	{
		const double Vh = FrameH * ViewHeight;
		const double Oy = FrameH * ViewTop + 0.5 * Vh - 0.5 * FrameH;
		return FVector2D(GuardSpanX / FrameW, (GuardSpanY - 2.0 * FMath::Abs(Oy)) / Vh);
	}

	bool VertexOutsideGuard(const FVector& Rel, const FVector& Right, const FVector& Up, const FVector& Fwd, double CotX, double CotY,
		const FVector2D& Guard)
	{
		const double W = FVector::DotProduct(Rel, Fwd);
		const double X = CotX * FVector::DotProduct(Rel, Right) / W;
		const double Y = CotY * FVector::DotProduct(Rel, Up) / W;
		return !(FMath::Abs(X) <= Guard.X && FMath::Abs(Y) <= Guard.Y);
	}

	bool VertexDropped(const FVector& Rel, const FVector& Right, const FVector& Up, const FVector& Fwd, double CotX, double CotY,
		const FVector2D& Guard, double Pull, double Near, double Far, bool bOrtho)
	{
		return VertexDropped(Rel, Fwd, Pull, Near, Far, bOrtho)
			|| (Pull > 0.0 && !bOrtho && VertexOutsideGuard(Rel, Right, Up, Fwd, CotX, CotY, Guard));
	}

	FVector Wpo(const FVector& Rel, const FVector& RootRel, const FVector& Fwd, double Pull, double Near, double Far, bool bOrtho)
	{
		const double Wp = FVector::DotProduct(RootRel, Fwd);
		const double W = FVector::DotProduct(Rel, Fwd);
		if (Pull <= 0.0 || bOrtho || Wp - Pull < Near || Wp > Far || VertexClipped(W, Near, Far))
		{
			return FVector::ZeroVector;
		}
		const double D = 1.0 / (Wp - Pull) - 1.0 / Wp;
		const double S = 1.0 / (1.0 + D * W);
		return Rel * (S - 1.0);
	}

	const TCHAR* WpoHlsl()
	{
		return TEXT("float wp = dot(RootRel, F);\n")
			TEXT("float w = dot(Rel, F);\n")
			TEXT("if (Pull <= 0.0 || View.ViewToClip[3][3] >= 1.0 || wp - Pull < Near || wp > Far || !(w >= Near && w <= Far)) return float3(0.0, 0.0, 0.0);\n")
			TEXT("float d = 1.0 / (wp - Pull) - 1.0 / wp;\n")
			TEXT("float s = 1.0 / (1.0 + d * w);\n")
			TEXT("return Rel * (s - 1.0);\n");
	}

	const TCHAR* ClipHlsl()
	{
		return TEXT("float w = dot(Rel, F);\n")
			TEXT("if (Pull <= 0.0 || View.ViewToClip[3][3] >= 1.0) return 0.0;\n")
			TEXT("if (!(w >= Near && w <= Far)) return 1.0;\n")
			TEXT("float2 ndc = float2(View.ViewToClip[0][0] * dot(Rel, R), View.ViewToClip[1][1] * dot(Rel, U)) / w;\n")
			TEXT("return (abs(ndc.x) <= Guard.x && abs(ndc.y) <= Guard.y) ? 0.0 : 1.0;\n");
	}

	const TCHAR* KeepHlsl()
	{
		return TEXT("return Clip > 0.0 ? 0.0 : 1.0;\n");
	}
}
