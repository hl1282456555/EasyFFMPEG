// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VideoCaptureStructures.generated.h"

UENUM(BlueprintType)
enum class EMovieCaptureState : uint8
{
	NotInit = 0,
	Initialized,
	Capturing,
};

USTRUCT(BlueprintType)
struct FCaptureConfigs
{
	GENERATED_USTRUCT_BODY()
public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Capture Configs")
		int32	BitRate = 5000;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Capture Configs")
		FIntPoint	FrameRate = FIntPoint(30, 1);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Capture Configs")
		int32	GopSize = 10;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Capture Configs")
		int32	MaxBFrames = 1;
};
