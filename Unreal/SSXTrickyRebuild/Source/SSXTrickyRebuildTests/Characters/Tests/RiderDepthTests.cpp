// 0x1DC3E0
#include "Characters/SSXRiderDepth.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "TestSkip.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SSXWorldPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace
{
	FString SquashWs(const FString& S)
	{
		FString O;
		for (TCHAR C : S) { if (!FChar::IsWhitespace(C)) { O.AppendChar(C); } }
		return O;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRiderDepthPullTest, "SSX.Render.RiderDepthPull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSSXRiderDepthPullTest::RunTest(const FString& Parameters)
{
	using namespace SSXRiderDepth;
	double Wp = 370.1602, Near = 15.0, Far = 49548.51953, Measured = 71075.3, MeasMin = 71073.0, MeasMax = 71076.9;
	struct FPx { double Zb, Zs; };
	TArray<FPx> Px;
	TArray<FString> Lines;
	const FString Csv = FPaths::Combine(FPaths::ProjectDir(), TEXT("SimData"), TEXT("pipe.rider_depth.csv"));
	if (FFileHelper::LoadFileToStringArray(Lines, *Csv) && Lines.Num() > 3)
	{
		TArray<FString> F;
		Lines[1].ParseIntoArray(F, TEXT(","));
		if (F.Num() >= 6)
		{
			Wp = FCString::Atod(*F[0]); Near = FCString::Atod(*F[1]); Far = FCString::Atod(*F[2]);
			Measured = FCString::Atod(*F[3]); MeasMin = FCString::Atod(*F[4]); MeasMax = FCString::Atod(*F[5]);
		}
		for (int32 I = 3; I < Lines.Num(); ++I)
		{
			Lines[I].ParseIntoArray(F, TEXT(","));
			if (F.Num() == 4) { Px.Add({ FCString::Atod(*F[2]), FCString::Atod(*F[3]) }); }
		}
	}
	else
	{
		SSXTest::Skip(*this, TEXT("SimData/pipe.rider_depth.csv missing (tools/make_rider_depth_reference.py): pixel check skipped"));
	}
	const double Off = GsZOffset(Wp, Near, Far);
	AddInfo(FString::Printf(TEXT("w_p %.2f cm, n %.0f, f %.1f: vfunc_23 offset %.1f Z units, measured %.1f (%.1f..%.1f), d %.6e"),
		Wp, Near, Far, Off, Measured, MeasMin, MeasMax, PullD(Wp, Near, Far)));
	TestTrue(TEXT("vfunc_23 offset = the GS dump's rider Z offset (within 15 Z units)"), FMath::Abs(Off - Measured) < 15.0);
	TestTrue(TEXT("measured offset constant over the board vertices"), MeasMax - MeasMin < 10.0);
	const double K = -ZScale * 2.0 * Far * Near / (Far - Near);
	TestTrue(TEXT("offset = K d"), FMath::Abs(Off - K * PullD(Wp, Near, Far)) < 1e-3);

	const double D = PullD(Wp, Near, Far);
	TestTrue(TEXT("d = 1/(w_p - 35.0229) - 1/w_p"), FMath::Abs(D - (1.0 / (Wp - PullCm) - 1.0 / Wp)) < 1e-15);
	const FVector Fwd = FVector(1.0, 0.5, -0.3).GetSafeNormal();
	const FVector RootRel = Fwd * Wp + FVector(0.0, 0.0, 12.0) - Fwd * (12.0 * Fwd.Z);
	for (const FVector Rel : { FVector(250.0, 140.0, -60.0), FVector(330.0, 160.0, -80.0), FVector(420.0, 200.0, -150.0) })
	{
		const FVector O = Wpo(Rel, RootRel, Fwd, PullCm, Near, Far);
		const FVector V = Rel + O;
		const double W = FVector::DotProduct(Rel, Fwd), W2 = FVector::DotProduct(V, Fwd);
		TestTrue(TEXT("1/w' = 1/w + d"), FMath::Abs(1.0 / W2 - (1.0 / W + D)) < 1e-12);
		TestTrue(TEXT("screen direction kept"), (V.GetSafeNormal() - Rel.GetSafeNormal()).Size() < 1e-9);
		TestTrue(TEXT("s = VertexScale"), FMath::Abs(W2 / W - VertexScale(W, D)) < 1e-12);
	}
	// toward the camera by ~35 cm at the root
	TestTrue(TEXT("root pulled 35.0229 cm along the view axis"),
		FMath::Abs(FVector::DotProduct(Wpo(RootRel, RootRel, Fwd, PullCm, Near, Far), Fwd) + PullCm) < 1e-9);
	TestEqual(TEXT("pull 0 -> no offset"), PullD(Wp, Near, Far, 0.0), 0.0);
	TestEqual(TEXT("pulled root nearer than n -> 0"), PullD(PullCm + Near - 0.01, Near, Far), 0.0);
	TestTrue(TEXT("pulled root at n -> on"), PullD(PullCm + Near + 0.01, Near, Far) > 0.0);
	TestEqual(TEXT("root beyond f -> 0"), PullD(Far + 1.0, Near, Far), 0.0);
	TestEqual(TEXT("GsZOffset follows the same rule"), GsZOffset(Far + 1.0, Near, Far), 0.0);
	TestTrue(TEXT("orthographic view -> 0"), Wpo(FVector(300, 0, 0), RootRel, Fwd, PullCm, Near, Far, true).IsZero());

	// 0x318080
	{
		const FVector RootNear = Fwd * 60.0;
		const double Dn = PullD(60.0, Near, Far);
		TestTrue(TEXT("passing racer: pull on"), Dn > 0.0);
		const FVector Behind = Fwd * -50.0 + FVector(0.0, 0.0, 40.0) - Fwd * (40.0 * Fwd.Z);
		const double Wb = FVector::DotProduct(Behind, Fwd);
		TestTrue(TEXT("old scale mirrors the vertex in front of the camera"), Wb / (1.0 + Dn * Wb) > 0.0);
		TestTrue(TEXT("vertex behind the eye: not moved"), Wpo(Behind, RootNear, Fwd, PullCm, Near, Far).IsZero());
		TestTrue(TEXT("vertex behind the eye: its triangles dropped"), VertexDropped(Behind, Fwd, PullCm, Near, Far));
		const FVector InFront = Fwd * (Near - 1.0);
		TestTrue(TEXT("vertex nearer than n: not moved, dropped"), Wpo(InFront, RootNear, Fwd, PullCm, Near, Far).IsZero()
			&& VertexDropped(InFront, Fwd, PullCm, Near, Far));
		TestTrue(TEXT("vertex beyond f: dropped"), VertexDropped(Fwd * (Far + 1.0), Fwd, PullCm, Near, Far));
		const FVector Ok = Fwd * (Near + 1.0);
		TestFalse(TEXT("vertex at n + 1: drawn"), VertexDropped(Ok, Fwd, PullCm, Near, Far));
		const FVector Moved = Ok + Wpo(Ok, RootNear, Fwd, PullCm, Near, Far);
		const double Wm = FVector::DotProduct(Moved, Fwd);
		TestTrue(TEXT("every pulled vertex stays in front of the eye, nearer than before"), Wm > 0.0 && Wm <= Near + 1.0);
		TestFalse(TEXT("front end (pull 0): nothing dropped"), VertexDropped(Behind, Fwd, 0.0, Near, Far));
		TestFalse(TEXT("orthographic (shadow capture): nothing dropped"), VertexDropped(Behind, Fwd, PullCm, Near, Far, true));
		for (double W = Near; W < 400.0; W += 0.5)
		{
			const FVector V = Fwd * W;
			if (FVector::DotProduct(V + Wpo(V, RootNear, Fwd, PullCm, Near, Far), Fwd) <= 0.0)
			{
				AddError(FString::Printf(TEXT("vertex at w %.1f pulled behind the eye"), W));
				break;
			}
		}
	}

	{
		const FVector2D G = GuardNdc();
		TestTrue(TEXT("guard band, one full viewport: 1.75007 x 2.28579"), FMath::IsNearlyEqual(G.X, 1.750069, 1e-5)
			&& FMath::IsNearlyEqual(G.Y, 2.285787, 1e-5));
		TestTrue(TEXT("guard band, 16:9 letterbox: y 3.04772"), FMath::IsNearlyEqual(GuardNdc(0.125, 0.75).Y, 3.047716, 1e-5));
		TestTrue(TEXT("guard band, an upper half viewport (oy -112): y 3.57157"), FMath::IsNearlyEqual(GuardNdc(0.0, 0.5).Y, 800.0325 / 224.0, 1e-4));
		const FVector R(0.0, 1.0, 0.0), U(0.0, 0.0, 1.0), Fw(1.0, 0.0, 0.0);
		struct FMode { double XScale, YScale, Top, Height; };
		const double Cot = 1.0 / FMath::Tan(0.825);
		int32 Bad = 0, Out = 0, In = 0;
		for (const FMode& Md : { FMode{ 1.0, 1.0, 0.0, 1.0 }, FMode{ 0.75, 0.75, 0.125, 0.75 }, FMode{ 0.75, 1.0, 0.0, 1.0 } })
		{
			const double Vw = 512.0, Vh = 448.0 * Md.Height, Oy = 448.0 * Md.Top + 0.5 * Vh - 224.0;
			const double M00 = Vw / GuardSpanX * Cot * Md.XScale;
			const double M11 = Vh / (GuardSpanY - 2.0 * FMath::Abs(Oy)) * (448.0 * 4.0 / 3.0 / 512.0) * (Vw / Vh) * Cot * Md.YScale;
			const double CotX = Cot * Md.XScale, CotY = CotX * (4.0 / 3.0 * Md.YScale / (Md.XScale * Md.Height));
			const FVector2D Gm = GuardNdc(Md.Top, Md.Height);
			for (double Tx = -3.0; Tx <= 3.0; Tx += 0.0625)
			{
				for (double Ty = -3.0; Ty <= 3.0; Ty += 0.0625)
				{
					const double W = 200.0;
					const FVector V = Fw * W + R * (Tx * W) + U * (Ty * W);
					const bool Orig = !(FMath::Abs(M00 * Tx) <= 1.0 && FMath::Abs(M11 * Ty) <= 1.0);
					const bool Port = VertexDropped(V, R, U, Fw, CotX, CotY, Gm, PullCm, Near, Far);
					Bad += Orig != Port && FMath::Abs(FMath::Abs(M00 * Tx) - 1.0) > 1e-6 && FMath::Abs(FMath::Abs(M11 * Ty) - 1.0) > 1e-6;
					Out += Orig;
					In += !Orig;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("guard sweep: %d drawn, %d dropped"), In, Out));
		TestEqual(TEXT("port drop == original clip / w test over the sweep (Off, 16:9, Animorphic)"), Bad, 0);
		TestTrue(TEXT("sweep covers both sides"), In > 0 && Out > 0);
		const double CotX = Cot, CotY = Cot * 4.0 / 3.0, W = 100.0;
		TestFalse(TEXT("vertex at image x 1.7: drawn"), VertexDropped(Fw * W + R * (1.7 * W / CotX), R, U, Fw, CotX, CotY, G, PullCm, Near, Far));
		TestTrue(TEXT("vertex at image x 1.8: dropped"), VertexDropped(Fw * W + R * (1.8 * W / CotX), R, U, Fw, CotX, CotY, G, PullCm, Near, Far));
		TestFalse(TEXT("vertex at image y -2.2: drawn"), VertexDropped(Fw * W - U * (2.2 * W / CotY), R, U, Fw, CotX, CotY, G, PullCm, Near, Far));
		TestTrue(TEXT("vertex at image y -2.4: dropped"), VertexDropped(Fw * W - U * (2.4 * W / CotY), R, U, Fw, CotX, CotY, G, PullCm, Near, Far));
		TestFalse(TEXT("front end (pull 0): no guard drop"), VertexDropped(Fw * W + R * (5.0 * W / CotX), R, U, Fw, CotX, CotY, G, 0.0, Near, Far));
		TestFalse(TEXT("orthographic: no guard drop"), VertexDropped(Fw * W + R * (5.0 * W / CotX), R, U, Fw, CotX, CotY, G, PullCm, Near, Far, true));
		TestTrue(TEXT("depth clip still applies"), VertexDropped(Fw * -50.0, R, U, Fw, CotX, CotY, G, PullCm, Near, Far));
	}

	if (Px.Num())
	{
		int32 With = 0, Without = 0, Drawn = 0;
		for (const FPx& P : Px)
		{
			const double Unbiased = P.Zb - Measured;
			Drawn += P.Zb >= P.Zs;
			With += Unbiased + Off >= P.Zs;
			Without += Unbiased >= P.Zs;
		}
		AddInfo(FString::Printf(TEXT("board pixels %d: drawn in the dump %d, with the port's offset %d, without %d"), Px.Num(), Drawn, With, Without));
		TestEqual(TEXT("board pixels in the capture"), Px.Num(), 1729);
		TestEqual(TEXT("every board pixel passes with the offset"), With, Px.Num());
		TestEqual(TEXT("same as the dump"), With, Drawn);
		TestEqual(TEXT("without the offset (the port's old picture)"), Without, 978);
	}

	const FString Hlsl = SquashWs(WpoHlsl());
	for (const TCHAR* Path : { TEXT("/Game/World/M_SSXRiderLit.M_SSXRiderLit"), TEXT("/Game/World/M_SSXRiderLit_Masked.M_SSXRiderLit_Masked"),
			TEXT("/Game/World/M_SSXRiderLitChrome.M_SSXRiderLitChrome"), TEXT("/Game/World/M_SSXRiderLitChrome_Masked.M_SSXRiderLitChrome_Masked") })
	{
		UMaterial* M = LoadObject<UMaterial>(nullptr, Path);
		if (!M)
		{
			AddError(FString::Printf(TEXT("%s missing (Scripts/make_world_materials.py)"), Path));
			continue;
		}
		const FString N = M->GetName();
		const UMaterialExpressionCustom* Node = nullptr;
		const UMaterialExpressionShadowReplace* Switch = nullptr;
		const UMaterialExpressionScalarParameter* PullParam = nullptr;
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			if (const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E.Get()); C && C->Description == TEXT("RiderDepthPull")) { Node = C; }
			if (const UMaterialExpressionShadowReplace* S = Cast<UMaterialExpressionShadowReplace>(E.Get())) { Switch = S; }
			if (const UMaterialExpressionScalarParameter* S = Cast<UMaterialExpressionScalarParameter>(E.Get()); S && S->ParameterName == TEXT("RiderDepthPull")) { PullParam = S; }
		}
		TestTrue(N + TEXT(": RiderDepthPull node = SSXRiderDepth::WpoHlsl"), Node && SquashWs(Node->Code) == Hlsl);
		const UMaterialExpressionCustom* Clip = nullptr;
		const UMaterialExpressionCustom* Keep = nullptr;
		for (const TObjectPtr<UMaterialExpression>& E : M->GetExpressions())
		{
			if (const UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E.Get()))
			{
				if (C->Description == TEXT("RiderDepthClip")) { Clip = C; }
				if (C->Description == TEXT("RiderDepthKeep")) { Keep = C; }
			}
		}
		TestTrue(N + TEXT(": RiderDepthClip node = SSXRiderDepth::ClipHlsl"), Clip && SquashWs(Clip->Code) == SquashWs(ClipHlsl()));
		TestTrue(N + TEXT(": RiderDepthKeep node = SSXRiderDepth::KeepHlsl"), Keep && SquashWs(Keep->Code) == SquashWs(KeepHlsl()));
		const UMaterialExpressionVectorParameter* GuardParam = nullptr;
		if (Clip)
		{
			for (const FCustomInput& In : Clip->Inputs)
			{
				if (In.InputName == TEXT("Guard")) { GuardParam = Cast<UMaterialExpressionVectorParameter>(In.Input.Expression); }
			}
		}
		const FVector2D G0 = GuardNdc();
		TestTrue(N + TEXT(": RiderDepthClip Guard = RiderGuard (default the full-viewport band)"), GuardParam
			&& GuardParam->ParameterName == TEXT("RiderGuard") && FMath::IsNearlyEqual(GuardParam->DefaultValue.R, float(G0.X), 1e-5f)
			&& FMath::IsNearlyEqual(GuardParam->DefaultValue.G, float(G0.Y), 1e-5f));
		TestTrue(N + TEXT(": the flag reaches the pixel through a vertex interpolator"), Keep && Keep->Inputs.Num() == 1
			&& Cast<UMaterialExpressionVertexInterpolator>(Keep->Inputs[0].Input.Expression)
			&& Cast<UMaterialExpressionVertexInterpolator>(Keep->Inputs[0].Input.Expression)->Input.Expression == Clip);
		TestTrue(N + TEXT(": masked (the dropped triangles are discarded)"), M->GetBlendMode() == BLEND_Masked);
		const FExpressionInput& Mask = M->GetEditorOnlyData()->OpacityMask;
		TestTrue(N + TEXT(": opacity mask = keep (x texture alpha for _Masked)"), Mask.IsConnected()
			&& (Mask.Expression == Keep || N.EndsWith(TEXT("_Masked"))));
		const FExpressionInput& Wpo = M->GetEditorOnlyData()->WorldPositionOffset;
		TestTrue(N + TEXT(": WPO from the shadow-pass switch"), Switch && Wpo.IsConnected() && Wpo.Expression == Switch);
		TestTrue(N + TEXT(": switch default = the pull, shadow input = constant"), Switch && Switch->Default.Expression == Node
			&& Switch->Shadow.Expression && Switch->Shadow.Expression != Node);
		TestTrue(N + TEXT(": RiderDepthPull defaults to 0 (front end, other users)"), PullParam && PullParam->DefaultValue == 0.0f);
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(M, GetTransientPackage());
		USSXWorldPresentation::ApplyRiderDepth(*Mid, PullCm, FVector(1.0, 2.0, 3.0), 15.0, GuardNdc(0.125, 0.75));
		FLinearColor GuardV;
		TestTrue(N + TEXT(": RiderGuard set (16:9 letterbox)"), Mid->GetVectorParameterValue(FMaterialParameterInfo(TEXT("RiderGuard")), GuardV)
			&& FMath::IsNearlyEqual(GuardV.G, float(GuardNdc(0.125, 0.75).Y), 1e-5f));
		float Pull = -1.0f, NearV = -1.0f;
		FLinearColor Root;
		TestTrue(N + TEXT(": instance parameters set"), Mid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RiderDepthPull")), Pull)
			&& Mid->GetScalarParameterValue(FMaterialParameterInfo(TEXT("RiderDepthNear")), NearV) && Mid->GetVectorParameterValue(FMaterialParameterInfo(TEXT("RiderRoot")), Root)
			&& FMath::IsNearlyEqual(Pull, float(PullCm)) && NearV == 15.0f && Root == FLinearColor(1, 2, 3, 1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSSXRiderMaterialsCompileTest, "SSX.Render.RiderMaterialsCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSSXRiderMaterialsCompileTest::RunTest(const FString& Parameters)
{
	auto Compile = [](UMaterial* M, EShaderPlatform Sp, TArray<FString>& Errors)
	{
		FMaterialResource* Res = M->AllocateResource();
		Res->SetMaterial(M, nullptr, Sp, EMaterialQualityLevel::High);
		const bool bOk = Res->CacheShaders(EMaterialShaderPrecompileMode::Synchronous);
		Res->FinishCompilation();
		Errors = Res->GetCompileErrors();
		return bOk && Errors.Num() == 0;
	};
	UMaterial* First = nullptr;
	for (const TCHAR* Path : { TEXT("/Game/World/M_SSXRiderLit.M_SSXRiderLit"), TEXT("/Game/World/M_SSXRiderLit_Masked.M_SSXRiderLit_Masked"),
			TEXT("/Game/World/M_SSXRiderLitChrome.M_SSXRiderLitChrome"), TEXT("/Game/World/M_SSXRiderLitChrome_Masked.M_SSXRiderLitChrome_Masked") })
	{
		UMaterial* M = LoadObject<UMaterial>(nullptr, Path);
		if (!M)
		{
			AddError(FString::Printf(TEXT("%s missing (Scripts/make_world_materials.py)"), Path));
			continue;
		}
		First = First ? First : M;
		for (const EShaderPlatform Sp : { SP_PCD3D_SM5, SP_PCD3D_SM6 })
		{
			TArray<FString> Errors;
			const bool bOk = Compile(M, Sp, Errors);
			const FString Tag = M->GetName() + (Sp == SP_PCD3D_SM5 ? TEXT(" SM5") : TEXT(" SM6"));
			for (const FString& E : Errors) { AddError(Tag + TEXT(": ") + E); }
			TestTrue(Tag + TEXT(": compiled"), bOk);
		}
	}
	if (First)
	{
		UMaterial* Bad = DuplicateObject<UMaterial>(First, GetTransientPackage());
		for (const TObjectPtr<UMaterialExpression>& E : Bad->GetExpressions())
		{
			if (UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(E.Get()); C && C->Description == TEXT("RiderDepthClip"))
			{
				C->Code = TEXT("return undefined_symbol_xyz;");
			}
		}
		TArray<FString> Errors;
		TestFalse(TEXT("a broken clip node fails to compile (the check works)"), Compile(Bad, SP_PCD3D_SM6, Errors));
		AddInfo(FString::Printf(TEXT("broken copy: %d errors"), Errors.Num()));
	}
	return true;
}

#endif
