// 0x14FA70, 0x14FE68, 0x14FFC8, 0x150248, 0x1506D8, 0x150740, 0x14FDB8, 0x14FD80, 0x236758, 0x123FC8, 0x114730, 0x126748, 0x11CAE8, 0x1267A0, 0x14FE18, 0x150428
#pragma once

#include "CoreMinimal.h"

namespace SSXRiderHit
{
	struct SSXTRICKYREBUILD_API FRiderPairs
	{
		static constexpr int32 None = -1;

		void Reset();
		// 0x11B348
		int32 AddProxy();
		void SetBox(int32 Proxy, const FVector3f& Min, const FVector3f& Max);
		void Move(int32 Proxy, const FVector3f& D);
		bool IsPaired(int32 A, int32 B) const;
		// 0x150848
		void SortLists();
		int32 NumProxies() const { return Proxies.Num(); }

		template <typename FVisit>
		void Walk(int32 Proxy, FVisit&& Visit)
		{
			for (int32 P = Proxies.IsValidIndex(Proxy) ? Proxies[Proxy].Head : None; P != None;)
			{
				Visit(Pairs[P].B == Proxy ? Pairs[P].A : Pairs[P].B);   // 0x110838
				P = Pairs[P].Next[Pairs[P].B == Proxy ? 1 : 0];
			}
		}

	private:
		struct FEnd
		{
			int32 Proxy = None;
			int32 Axis = 0;
			uint8 bMax = 0;
			int32 Next = None;
			int32 Prev = None;
		};
		struct FProxy
		{
			float Min[3] = { 0, 0, 0 };
			float Max[3] = { 0, 0, 0 };
			int32 End[2][3] = { { None, None, None }, { None, None, None } };
			int32 Head = None, Tail = None;
		};
		struct FPair
		{
			int32 A = None, B = None;
			int32 Next[2] = { None, None };
			int32 Prev[2] = { None, None };
			int32 FreeNext = None;
		};

		float Value(int32 E) const;
		void Append(int32 Axis, int32 E);       // 0x14F780
		void SwapForward(int32 Axis, int32 E);  // 0x14F7E0
		void UpdateAxis(int32 Proxy, int32 Axis);
		void Update(int32 Proxy);
		void Toggle(int32 Axis, int32 PA, int32 PB);
		void Link(int32 Proxy, int32 P);
		void Unlink(int32 Proxy, int32 P);
		int32& BitsOf(int32 PA, int32 PB);

		TArray<FProxy> Proxies;
		TArray<FEnd> Ends;
		TArray<FPair> Pairs;
		TArray<int32> Bits;
		int32 AxisHead[3] = { None, None, None };
		int32 AxisTail[3] = { None, None, None };
		int32 FreeHead = None;
	};
}
