#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/SSXFeDraw.h"
#include "UI/SSXFrontEnd.h"
#include "UI/SSXFeTrickBook.h"
#include "UI/SSXRaceOverlays.h"
#include "SSXFeWidget.generated.h"

class UMediaPlayer;
class UMediaTexture;
class UAudioComponent;
class USoundWaveProcedural;
class USSXFrontEndSubsystem;

// Paints the front end / race overlays in the original 640x480 space, 4:3-centred, stepped at 60 Hz.
UCLASS()
class SSXTRICKYREBUILD_API USSXFeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	enum class EMode : uint8 { FrontEnd, Race };
	void InitFrontEnd(USSXFrontEndSubsystem* InSub, bool bColdBoot);
	void InitRace(USSXFrontEndSubsystem* InSub);

	FSSXFrontEnd& GetFrontEnd() { return FE; }
	FSSXOverlayManager& GetOverlays() { return Overlays; }
	const FSSXFeDrawList& GetDrawList() const { return DrawList; }
	EMode GetMode() const { return Mode; }

	void PressPad(int32 Pad, uint32 Bit);
	void SkipMovie();
	static uint32 PadBitForKey(const FKey& K);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnAnalogValueChanged(const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void Step();
	void Redraw();
	void TickMovie(float Dt);
	void StartMovie(const FString& Name);
	void StopMovie();
	bool StartMovieAudio(const FString& WavPath);

	EMode Mode = EMode::FrontEnd;
	FSSXFrontEnd FE;
	FSSXOverlayManager Overlays;
	FSSXFeDrawList DrawList;
	TSharedPtr<SSSXFeCanvas> Canvas;
	UPROPERTY(Transient) TObjectPtr<USSXFrontEndSubsystem> Sub;
	float Accum = 0.f;
	uint32 Held[2] = { 0, 0 };
	/** Front end: the pressed buttons / keys per pad (several keys may map to one pad bit), the left stick (PS2 axes),
	 * last frame's word and the pad's FE key emitter. */
	TSet<FKey> HeldKeys[2];
	FVector2f Stick[2] = { FVector2f::ZeroVector, FVector2f::ZeroVector };
	uint32 PrevWord[2] = { 0, 0 };
	SSXFe::FFeKeyEmitter Emitter[2];
	void StepPads();

	UPROPERTY(Transient) TObjectPtr<UMediaPlayer> MediaPlayer;
	UPROPERTY(Transient) TObjectPtr<UMediaTexture> MediaTexture;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> MovieAudio;
	UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> MovieWave;
	FString PlayingMovie;
	float MovieClock = 0.f;
	bool bMovieStarted = false;
	bool bMovieVideoEnded = false;
	double MovieAudioSeconds = 0.0;
	double MovieAudioClock = 0.0;

	void StepBookVideo();
	SSXFeTrickBook::FVideo BookVideo;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> BookVideoTex;
};
