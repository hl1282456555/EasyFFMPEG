// Copyright Epic Games, Inc. All Rights Reserved.

#include "EasyFFMPEG.h"

#include "Interfaces/IPluginManager.h"

extern "C" {
#include "libavformat/avformat.h"
}

DEFINE_LOG_CATEGORY(LogFFmpeg);

#define LOCTEXT_NAMESPACE "FEasyFFMPEGModule"

void FEasyFFMPEGModule::StartupModule()
{
	bInitialized = false;
	// This code will execute after your module is loaded into memory(nullptr) the exact timing is specified in the .uplugin file per-module
	InitLibraryHandles();

	// Init log level and bind callback
	av_log_set_level(AV_LOG_WARNING);
	av_log_set_callback(FFmpegCallback);

	UE_LOG(LogFFmpeg, Log, TEXT("FFmpeg AVCodec version: %d.%d.%d"), LIBAVFORMAT_VERSION_MAJOR, LIBAVFORMAT_VERSION_MINOR, LIBAVFORMAT_VERSION_MICRO);
	UE_LOG(LogFFmpeg, Log, TEXT("FFmpeg license: %s"), UTF8_TO_TCHAR(avformat_license()));
}

void FEasyFFMPEGModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	if (!bInitialized) {
		return;
	}

	UnloadHandledLibraries();
}


void FEasyFFMPEGModule::FFmpegCallback(void*, int Level, const char* Format, va_list ArgList)
{
	char buffer[2048]{ 0 };

#if PLATFORM_WINDOWS
	vsprintf_s(buffer, 2048, Format, ArgList);
#else 
	vsnprintf(buffer, 2048, Format, ArgList);
#endif

	FString logStr = FString::Printf(TEXT("FFMPEG - %s"), UTF8_TO_TCHAR(buffer));

	switch (Level)
	{
	case AV_LOG_TRACE:
		UE_LOG(LogFFmpeg, Log, TEXT("%s"), *logStr);
		break;
	case AV_LOG_DEBUG:
		UE_LOG(LogFFmpeg, Log, TEXT("%s"), *logStr);
		break;
	case AV_LOG_VERBOSE:
		UE_LOG(LogFFmpeg, Log, TEXT("%s"), *logStr);
		break;
	case AV_LOG_INFO:
		UE_LOG(LogFFmpeg, Log, TEXT("%s"), *logStr);
		break;
	case AV_LOG_WARNING:
		UE_LOG(LogFFmpeg, Warning, TEXT("%s"), *logStr);
		break;
	case AV_LOG_ERROR:
		UE_LOG(LogFFmpeg, Error, TEXT("%s"), *logStr);
		break;
	case AV_LOG_FATAL:
		UE_LOG(LogFFmpeg, Fatal, TEXT("%s"), *logStr);
		break;
	default:
		UE_LOG(LogFFmpeg, Log, TEXT("%s"), *logStr);
		break;
	}
}

void FEasyFFMPEGModule::InitLibraryHandles()
{
	if (bInitialized) {
		return;
	}

	FString zlibName;

#if PLATFORM_WINDOWS
	LibMP3LameHandle = LoadDependencyLibrary(TEXT("libmp3lame.dll"));
	LibX264Handle = LoadDependencyLibrary(TEXT("libx264-163.dll"));
	AVUtilHandle = LoadDependencyLibrary(TEXT("avutil-56.dll"));
	PostProcHandle = LoadDependencyLibrary(TEXT("postproc-55.dll"));
	SWResampleHandle = LoadDependencyLibrary(TEXT("swresample-3.dll"));
	SWScaleHandle = LoadDependencyLibrary(TEXT("swscale-5.dll"));
	AVCodecHandle = LoadDependencyLibrary(TEXT("avcodec-58.dll"));
	AVResampleHandle = LoadDependencyLibrary(TEXT("avresample-4.dll"));
	AVFormatHandle = LoadDependencyLibrary(TEXT("avformat-58.dll"));
	AVFilterHandle = LoadDependencyLibrary(TEXT("avfilter-7.dll"));
	AVDeviceHandle = LoadDependencyLibrary(TEXT("avdevice-58.dll"));

	if (AVCodecHandle == nullptr || AVDeviceHandle == nullptr || AVFilterHandle == nullptr ||
		AVFormatHandle == nullptr || AVResampleHandle == nullptr ||
		AVUtilHandle == nullptr || LibMP3LameHandle == nullptr || LibX264Handle == nullptr ||
		PostProcHandle == nullptr || SWResampleHandle == nullptr || SWScaleHandle == nullptr) {
		
		UE_LOG(LogFFmpeg, Error, TEXT("Load dependecy dll failed."));
		return;
	}
#endif

	bInitialized = true;
}

void FEasyFFMPEGModule::UnloadHandledLibraries()
{
	bInitialized = false;

#if PLATFORM_WINDOWS
	if (AVDeviceHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(AVDeviceHandle);
		AVDeviceHandle = nullptr;
	}

	if (AVFilterHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(AVFilterHandle);
		AVFilterHandle = nullptr;
	}

	if (AVFormatHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(AVFormatHandle);
		AVFormatHandle = nullptr;
	}

	if (AVResampleHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(AVResampleHandle);
		AVResampleHandle = nullptr;
	}

	if (AVCodecHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(AVCodecHandle);
		AVCodecHandle = nullptr;
	}

	if (SWScaleHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(SWScaleHandle);
		SWScaleHandle = nullptr;
	}

	if (SWResampleHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(SWResampleHandle);
		SWResampleHandle = nullptr;
	}

	if (PostProcHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(PostProcHandle);
		PostProcHandle = nullptr;
	}

	if (AVUtilHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(AVUtilHandle);
		AVUtilHandle = nullptr;
	}

	if (LibX264Handle != nullptr) {
		FPlatformProcess::FreeDllHandle(LibX264Handle);
		LibX264Handle = nullptr;
	}

	if (LibMP3LameHandle != nullptr) {
		FPlatformProcess::FreeDllHandle(LibMP3LameHandle);
		LibMP3LameHandle = nullptr;
	}
#endif
}

void* FEasyFFMPEGModule::LoadDependencyLibrary(const FString& DLLName)
{
	FString baseDir = IPluginManager::Get().FindPlugin("EasyFFMPEG")->GetBaseDir();
	
	bool bIsDebug = false;

#if UE_BUILD_DEBUG
	bIsDebug = true;
#else
	bIsDebug = false;
#endif

	FString dllDir = FPaths::Combine(*baseDir, TEXT("ThirdParty/ffmpeg/bin/"), bIsDebug ? TEXT("x64_Debug") : TEXT("x64_Release"), TEXT("Windows"));

	FString dllFilename = FPaths::Combine(*dllDir, *DLLName);

	UE_LOG(LogFFmpeg, Log, TEXT("Loading dependency dll ----> %s"), *dllFilename);

	return FPlatformProcess::GetDllHandle(*dllFilename);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FEasyFFMPEGModule, EasyFFMPEG)