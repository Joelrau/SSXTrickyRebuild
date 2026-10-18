#include "Audio/SSXWaveKeepAlive.h"

#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "Sound/SoundWave.h"
#include "UObject/GCObject.h"

namespace SSXWaveKeepAlive
{
	namespace
	{
		struct FHeld
		{
			TObjectPtr<USoundWave> Wave;
			double Since = 0.0;
		};

		class FHolder final : public FGCObject
		{
		public:
			TArray<FHeld> Held;
			FTSTicker::FDelegateHandle Ticker;

			virtual void AddReferencedObjects(FReferenceCollector& Collector) override
			{
				for (FHeld& H : Held) { Collector.AddReferencedObject(H.Wave); }
			}
			virtual FString GetReferencerName() const override { return TEXT("SSXWaveKeepAlive"); }
		};

		FHolder& Holder()
		{
			static FHolder* H = new FHolder();
			return *H;
		}
	}

	void Track(USoundWave* Wave)
	{
		if (!Wave || !Wave->bProcedural || !IsValid(Wave))
		{
			return;
		}
		FHolder& H = Holder();
		const double Now = FPlatformTime::Seconds();
		if (FHeld* E = H.Held.FindByPredicate([Wave](const FHeld& X) { return X.Wave == Wave; }))
		{
			E->Since = Now;
		}
		else
		{
			H.Held.Add({ Wave, Now });
		}
		if (!H.Ticker.IsValid())
		{
			H.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				Prune(FPlatformTime::Seconds());
				return true;
			}), 0.25f);
		}
	}

	void Retire(UAudioComponent* Component)
	{
		if (!Component)
		{
			return;
		}
		Track(Cast<USoundWave>(Component->Sound));
		Component->Stop();
	}

	void Prune(double NowSeconds)
	{
		Holder().Held.RemoveAll([NowSeconds](const FHeld& E)
		{
			return !E.Wave || (NowSeconds - E.Since >= MinHoldSeconds && !E.Wave->IsGeneratingAudio());
		});
	}

	bool IsHeld(const USoundWave* Wave)
	{
		return Wave && Holder().Held.ContainsByPredicate([Wave](const FHeld& X) { return X.Wave == Wave; });
	}

}
