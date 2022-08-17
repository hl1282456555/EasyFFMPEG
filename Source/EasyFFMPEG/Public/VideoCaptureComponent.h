// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FrameGrabber.h"
#include "Components/SceneComponent.h"
#include "VideoCaptureStructures.h"
#include "VideoCaptureComponent.generated.h"

UCLASS( meta=(BlueprintSpawnableComponent) )
class EASYFFMPEG_API UVideoCaptureComponent : public USceneComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UVideoCaptureComponent();

	UFUNCTION(BlueprintCallable, Category = "Video Capture")
	void StartCapture(const FString& InVideoFilename);

	UFUNCTION(BlueprintPure, Category = "Video Capture")
	bool IsInitialized();

	UFUNCTION(BlueprintCallable, Category = "Video Capture")
	bool CaptureThisFrame(int32 CurrentFrame);

	UFUNCTION(BlueprintCallable, Category = "Video Capture")
	void StopCapture();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void BeginDestroy() override;

	bool CreateVideoFileWriter();

	void DestroyVideoFileWriter();

	bool InitFrameGrabber(const FIntPoint& ViewportSize);

	void ReleaseFrameGrabber();

	void ReleaseContext();

	void WriteFrameToFile(const TArray<FColor>& ColorBuffer, int32 CurrentFrame);

	void EncodeVideoFrame(struct AVCodecContext* InCodecCtx, struct AVFrame* InFrame, struct AVPacket* InPacket);

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Video Capture")
	FString	VideoFilename;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Video Capture")
	FCaptureConfigs	CaptureConfigs;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Video Capture")
	int32 CapturedFrameNumber = 0;

	EMovieCaptureState CaptureState;

private:

	struct AVFormatContext* FormatCtx;
	struct AVCodec* Codec;
	struct AVCodecContext* CodecCtx;
	struct AVFrame* Frame;
	struct AVPacket* Packet;
	struct AVStream* Stream;

	TSharedPtr<FFrameGrabber>	FrameGrabber;
	FArchive* Writer;

	int32 ShouldCutFrameCount;

	FTimespan PassedTime;
	FTimespan FrameTimeForCapture;
};
