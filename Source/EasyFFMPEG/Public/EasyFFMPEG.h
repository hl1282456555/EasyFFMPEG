// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogFFmpeg, Log, All);

class FEasyFFMPEGModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void FFmpegCallback(void*, int Level, const char* Format, va_list ArgList);

protected:
	void InitLibraryHandles();

	void UnloadHandledLibraries();

	void* LoadDependencyLibrary(const FString& DLLName);

public:

	bool bInitialized;

	void* AVCodecHandle;
	void* AVDeviceHandle;
	void* AVFilterHandle;
	void* AVFormatHandle;
	void* AVResampleHandle;
	void* AVUtilHandle;
	void* LibMP3LameHandle;
	void* LibX264Handle;
	void* PostProcHandle;
	void* SWResampleHandle;
	void* SWScaleHandle;
};
