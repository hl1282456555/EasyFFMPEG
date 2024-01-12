// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "VideoCaptureStructures.h"
#include "Widgets/SWindow.h"
#include <chrono>
#include "AudioDevice.h"
#include "VideoCaptureSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class EASYFFMPEG_API UVideoCaptureSubsystem : public UGameInstanceSubsystem, public ISubmixBufferListener
{
	GENERATED_BODY()
public:

	/** Implement this for deinitialization of instances of the system */
	virtual void Deinitialize() override;

	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock);

	UFUNCTION(BlueprintCallable, Category = "Video Capture")
	void StartCapture(const FString& InVideoFilename, const FCaptureConfigs& InConfigs);

	UFUNCTION(BlueprintPure, Category = "Video Capture")
	bool IsInitialized();

	UFUNCTION(BlueprintCallable, Category = "Video Capture")
	void StopCapture();

protected:

	bool InitReadbackTexture();

	bool FindViewportWindow();

	bool CreateVideoFileWriter();

	void DestroyVideoFileWriter();

	void ReleaseContext();

	void WriteFrameToFile(const TArray<FColor>& ColorBuffer, int32 CurrentFrame);

	void EncodeVideoFrame(struct AVCodecContext* InCodecCtx, struct AVFrame* InFrame, struct AVPacket* InPacket);

	void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);

	void InitAvailableEvent();

	void BlockUntilAvailable();

	void ResolveRenderTarget(const FTexture2DRHIRef& SourceBackBuffer);

	struct AVFrame* AllocAudioFrame(enum AVSampleFormat Format, uint64 ChannelLayout, int32 SampleRate, int32 SamplesCount);

public:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Video Capture")
	FString	VideoFilename;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Video Capture")
	FCaptureConfigs	CaptureConfigs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Video Capture")
	int32 CapturedFrameNumber = 0;

	EMovieCaptureState CaptureState;

	FIntPoint ViewportSize;

	/** Texture used to store the resolved render target */
	FTexture2DRHIRef ReadbackTexture;

private:

	struct AVFormatContext* FormatCtx;
	struct AVCodec* Codec;
	struct AVCodecContext* CodecCtx;
	struct AVFrame* Frame;
	struct AVPacket* Packet;
	struct AVStream* Stream;

	struct AVStream* AudioStream;
	struct AVCodec* AudioCodec;
	struct AVCodecContext* AudioCodecCtx;
	struct AVFrame* AudioFrame;
	struct AVFrame* AudioTempFrame;
	struct SwrContext* AudioSwrCtx = nullptr;

	FArchive* Writer;

	std::chrono::steady_clock::time_point PreFrameCaptureTime;
	std::chrono::nanoseconds CaptureFrameInterval;

	void* ViewportWindow;
	FDelegateHandle BackBufferHandle;

	FEvent* AvailableEvent;
	TArray<uint8> AudioSubmixBuffer;
	int32 AudioSampleCount;
};
