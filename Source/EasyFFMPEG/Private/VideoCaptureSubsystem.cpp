// Fill out your copyright notice in the Description page of Project Settings.


#include "VideoCaptureSubsystem.h"

#include "Slate/SceneViewport.h"
#include "Engine/GameEngine.h"
#include "HAL/FileManager.h"

#include "RHIStaticStates.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ScreenRendering.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "RenderTargetPool.h"
#include "RenderCommandFence.h"

#include "Kismet/GameplayStatics.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libavutil/error.h"
}

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "IAssetViewport.h"
#endif

DECLARE_LOG_CATEGORY_CLASS(LogVideoCaptureSubsystem, Log, All);

void UVideoCaptureSubsystem::Deinitialize()
{
	StopCapture();

	Super::Deinitialize();
}

void UVideoCaptureSubsystem::StartCapture(const FString& InVideoFilename, const FCaptureConfigs& InConfigs)
{
	CapturedFrameNumber = 0;
	CaptureConfigs = InConfigs;

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC == nullptr) {
		UE_LOG(LogVideoCaptureSubsystem, Warning, TEXT("Can not found player controller."));
		return;
	}

	if (IsInitialized()) {
		UE_LOG(LogVideoCaptureSubsystem, Warning, TEXT("Please uninitialize capture component before call InitCapture()."));
		return;
	}

	PC->GetViewportSize(ViewportSize.X, ViewportSize.Y);

	if (!FindViewportWindow()) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Cant find the viewport window."));
		return;
	}

	if (!InitReadbackTexture())
	{
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Failed to create the readback texture."));
		return;
	}

	VideoFilename = InVideoFilename;

	if (!CreateVideoFileWriter()) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Cant create the video file '%s'."), *VideoFilename);
		StopCapture();
		return;
	}

	DestroyVideoFileWriter();

	int32 result = avformat_alloc_output_context2(&FormatCtx, nullptr, nullptr, TCHAR_TO_UTF8(*VideoFilename));
	if (result < 0) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Can not allocate format context."));
		StopCapture();
		return;
	}

	Codec = avcodec_find_encoder(FormatCtx->oformat->video_codec);
	if (Codec == nullptr) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Codec not found."));
		StopCapture();
		return;
	}

	Stream = avformat_new_stream(FormatCtx, Codec);
	if (Stream == nullptr) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Can not allocate a new stream."));
		StopCapture();
		return;
	}

	CodecCtx = avcodec_alloc_context3(Codec);
	if (Codec == nullptr) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("InitCapture() failed: Cloud not allocate video codec context."));
		StopCapture();
		return;
	}

	Stream->codecpar->codec_id = FormatCtx->oformat->video_codec;
	Stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	Stream->codecpar->width = ViewportSize.X;
	Stream->codecpar->height = ViewportSize.Y;
	Stream->codecpar->format = AV_PIX_FMT_YUV420P;
	Stream->codecpar->bit_rate = CaptureConfigs.BitRate * 1000;

	avcodec_parameters_to_context(CodecCtx, Stream->codecpar);

	CodecCtx->time_base = { CaptureConfigs.FrameRate.Y, CaptureConfigs.FrameRate.X };
	CodecCtx->framerate = { CaptureConfigs.FrameRate.X, CaptureConfigs.FrameRate.Y };
	CodecCtx->gop_size = CaptureConfigs.GopSize;
	CodecCtx->max_b_frames = CaptureConfigs.MaxBFrames;

	if (Stream->codecpar->codec_id == AV_CODEC_ID_H264) {
		av_opt_set(CodecCtx, "preset", "slow", 0);
	}

	result = avcodec_open2(CodecCtx, Codec, nullptr);
	if (result < 0) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Could not open codec."));
		StopCapture();
		return;
	}

	avcodec_parameters_from_context(Stream->codecpar, CodecCtx);

	av_dump_format(FormatCtx, 0, TCHAR_TO_UTF8(*VideoFilename), 1);

	result = avio_open(&FormatCtx->pb, TCHAR_TO_UTF8(*VideoFilename), AVIO_FLAG_WRITE);
	if (result < 0) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Cant open the file '%s'."), *VideoFilename);
		StopCapture();
		return;
	}

	result = avformat_write_header(FormatCtx, nullptr);
	if (result < 0) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Error ocurred when write header into file."));
		StopCapture();
		return;
	}

	Packet = av_packet_alloc();
	if (Packet == nullptr) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("InitCapture() failed: Cloud not allocate packet."));
		StopCapture();
		return;
	}

	Frame = av_frame_alloc();
	if (Frame == nullptr) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Cloud not allocate video frame."));
		StopCapture();
		return;
	}

	Frame->format = CodecCtx->pix_fmt;
	Frame->width = CodecCtx->width;
	Frame->height = CodecCtx->height;

	result = av_frame_get_buffer(Frame, 0);
	if (result < 0) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Cloud not allocate the video frame data."));
		StopCapture();
		return;
	}

	int64 timeBase = CaptureConfigs.FrameRate.Y * 1000000000;
	CaptureFrameInterval = std::chrono::nanoseconds(timeBase / CaptureConfigs.FrameRate.X);
	PreFrameCaptureTime = std::chrono::steady_clock::now();

	AvailableEvent = nullptr;

	if (FSlateApplication::IsInitialized())
	{
		BackBufferHandle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &UVideoCaptureSubsystem::OnBackBufferReady_RenderThread);
	}

	CaptureState = EMovieCaptureState::Initialized;
}

bool UVideoCaptureSubsystem::IsInitialized()
{
	return CaptureState >= EMovieCaptureState::Initialized;
}

void UVideoCaptureSubsystem::StopCapture()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(BackBufferHandle);
	}

	BlockUntilAvailable();

	ViewportWindow = nullptr;
	ReadbackTexture.SafeRelease();
	ReadbackTexture = nullptr;

	DestroyVideoFileWriter();
	ReleaseContext();

	CaptureState = EMovieCaptureState::NotInit;
}

bool UVideoCaptureSubsystem::InitReadbackTexture()
{
	ReadbackTexture.SafeRelease();

	UVideoCaptureSubsystem* This = this;

	ENQUEUE_RENDER_COMMAND(CreateCaptureFrameTexture)(
		[This](FRHICommandListImmediate& RHICmdList)
		{
			FRHIResourceCreateInfo CreateInfo;

			This->ReadbackTexture = RHICreateTexture2D(
				This->ViewportSize.X,
				This->ViewportSize.Y,
				EPixelFormat::PF_B8G8R8A8,
				1,
				1,
				TexCreate_CPUReadback,
				CreateInfo
			);
		});

	FRenderCommandFence createTextureFence;
	createTextureFence.BeginFence(true);
	createTextureFence.Wait();

	return ReadbackTexture != nullptr;
}

bool UVideoCaptureSubsystem::FindViewportWindow()
{
	TSharedPtr<FSceneViewport> sceneViewport;

#if WITH_EDITOR
	if (GIsEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE)
			{
				FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
				if (SlatePlayInEditorSession)
				{
					if (SlatePlayInEditorSession->DestinationSlateViewport.IsValid())
					{
						TSharedPtr<IAssetViewport> DestinationLevelViewport = SlatePlayInEditorSession->DestinationSlateViewport.Pin();
						sceneViewport = DestinationLevelViewport->GetSharedActiveViewport();
					}
					else if (SlatePlayInEditorSession->SlatePlayInEditorWindowViewport.IsValid())
					{
						sceneViewport = SlatePlayInEditorSession->SlatePlayInEditorWindowViewport;
					}
				}
			}
		}
	}
	else
#endif
	{
		UGameEngine* gameEngine = Cast<UGameEngine>(GEngine);
		if (gameEngine == nullptr || !gameEngine->SceneViewport.IsValid()) {
			return false;
		}

		sceneViewport = gameEngine->SceneViewport;
	}

	TSharedPtr<SWindow> cachedWindow = FSlateApplication::Get().FindWidgetWindow(sceneViewport->GetViewportWidget().Pin().ToSharedRef());
	ViewportWindow = cachedWindow.Get();

	return ViewportWindow != nullptr;
}

bool UVideoCaptureSubsystem::CreateVideoFileWriter()
{
	if (IFileManager::Get().FileExists(*VideoFilename)) {
		IFileManager::Get().Delete(*VideoFilename);
	}

	Writer = IFileManager::Get().CreateFileWriter(*VideoFilename, EFileWrite::FILEWRITE_Append);
	if (Writer == nullptr) {
		return false;
	}

	return true;
}

void UVideoCaptureSubsystem::DestroyVideoFileWriter()
{
	if (Writer == nullptr) {
		return;
	}

	Writer->Flush();
	Writer->Close();

	delete Writer;
	Writer = nullptr;
}

void UVideoCaptureSubsystem::ReleaseContext()
{
	if (CaptureState != EMovieCaptureState::NotInit) {
		EncodeVideoFrame(CodecCtx, nullptr, Packet);

		av_write_trailer(FormatCtx);
	}

	if (CodecCtx != nullptr) {
		avcodec_free_context(&CodecCtx);
		CodecCtx = nullptr;
	}

	if (FormatCtx != nullptr) {
		if (FormatCtx->pb != nullptr) {
			avio_close(FormatCtx->pb);
		}
		avformat_free_context(FormatCtx);
		FormatCtx = nullptr;
	}

	if (Frame != nullptr) {
		av_frame_free(&Frame);
		Frame = nullptr;
	}

	if (Packet != nullptr) {
		av_packet_free(&Packet);
		Packet = nullptr;
	}
}

void UVideoCaptureSubsystem::WriteFrameToFile(const TArray<FColor>& ColorBuffer, int32 CurrentFrame)
{
	AVFrame* rgbFrame = av_frame_alloc();
	if (rgbFrame == nullptr) {
		return;
	}

	avpicture_fill((AVPicture*)rgbFrame, (uint8_t*)ColorBuffer.GetData(), AV_PIX_FMT_BGRA, CodecCtx->width, CodecCtx->height);

	SwsContext* scaleCtx = sws_getContext(CodecCtx->width, CodecCtx->height, AV_PIX_FMT_BGRA,
		CodecCtx->width, CodecCtx->height, CodecCtx->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (scaleCtx == nullptr) {
		av_frame_free(&rgbFrame);
		return;
	}

	int32 result = sws_scale(scaleCtx, rgbFrame->data, rgbFrame->linesize, 0, CodecCtx->height, Frame->data, Frame->linesize);

	av_frame_free(&rgbFrame);
	sws_freeContext(scaleCtx);

	Frame->pts = CurrentFrame;

	if (result != CodecCtx->height) {
		return;
	}

	EncodeVideoFrame(CodecCtx, Frame, Packet);
}

void UVideoCaptureSubsystem::EncodeVideoFrame(AVCodecContext* InCodecCtx, AVFrame* InFrame, AVPacket* InPacket)
{
	int32 result = avcodec_send_frame(InCodecCtx, InFrame);
	if (result < 0) {
		UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Error sending a frame for encoding."));
		return;
	}

	while (result >= 0)
	{

		InPacket->data = nullptr;
		InPacket->size = 0;

		result = avcodec_receive_packet(InCodecCtx, InPacket);
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
			return;
		}
		else if (result < 0) {
			UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Error during encoding."));
			return;
		}

		AVRational tempRational;
		tempRational.num = CaptureConfigs.FrameRate.Y;
		tempRational.den = CaptureConfigs.FrameRate.X;

		av_packet_rescale_ts(InPacket, tempRational, Stream->time_base);

		InPacket->stream_index = Stream->index;

		result = av_interleaved_write_frame(FormatCtx, InPacket);
		if (result < 0) {
			UE_LOG(LogVideoCaptureSubsystem, Error, TEXT("Error during interleaved write frame."));
			return;
		}

		av_packet_unref(InPacket);
	}
}

void UVideoCaptureSubsystem::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
	if (ViewportWindow != &SlateWindow)
	{
		return;
	}

	std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
	std::chrono::milliseconds passedTime = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - PreFrameCaptureTime);
	if (passedTime < CaptureFrameInterval)
	{
		return;
	}

	PreFrameCaptureTime += CaptureFrameInterval;

	BlockUntilAvailable();
	InitAvailableEvent();

	ResolveRenderTarget(BackBuffer);
}

void UVideoCaptureSubsystem::InitAvailableEvent()
{
	check(!AvailableEvent);
	AvailableEvent = FPlatformProcess::GetSynchEventFromPool();
}

void UVideoCaptureSubsystem::BlockUntilAvailable()
{
	if (AvailableEvent)
	{
		AvailableEvent->Wait(~0);

		FPlatformProcess::ReturnSynchEventToPool(AvailableEvent);
		AvailableEvent = nullptr;
	}
}

void UVideoCaptureSubsystem::ResolveRenderTarget(const FTexture2DRHIRef& SourceBackBuffer)
{
	static const FName RendererModuleName("Renderer");
	// @todo: JIRA UE-41879 and UE-43829 - added defensive guards against memory trampling on this render command to try and ascertain why it occasionally crashes
	uint32 MemoryGuard1 = 0xaffec7ed;

	// Load the renderer module on the main thread, as the module manager is not thread-safe, and copy the ptr into the render command, along with 'this' (which is protected by BlockUntilAvailable in ~FViewportSurfaceReader())
	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);

	uint32 MemoryGuard2 = 0xaffec7ed;
	IRendererModule* RendererModuleDebug = RendererModule;

	{
		FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();

		const FIntPoint TargetSize(ReadbackTexture->GetSizeX(), ReadbackTexture->GetSizeY());

		FPooledRenderTargetDesc OutputDesc = FPooledRenderTargetDesc::Create2DDesc(
			TargetSize,
			ReadbackTexture->GetFormat(),
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_RenderTargetable,
			false);

		TRefCountPtr<IPooledRenderTarget> ResampleTexturePooledRenderTarget;
		GRenderTargetPool.FindFreeElement(RHICmdList, OutputDesc, ResampleTexturePooledRenderTarget, TEXT("ResampleTexture"));
		check(ResampleTexturePooledRenderTarget);

		const FSceneRenderTargetItem& DestRenderTarget = ResampleTexturePooledRenderTarget->GetRenderTargetItem();

		FRHIRenderPassInfo RPInfo(DestRenderTarget.TargetableTexture, ERenderTargetActions::Load_Store, ReadbackTexture);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("FrameGrabberResolveRenderTarget"));
		{
			RHICmdList.SetViewport(0, 0, 0.0f, TargetSize.X, TargetSize.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;

			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceBackBuffer);


			float U = float(0) / float(SourceBackBuffer->GetSizeX());
			float V = float(0) / float(SourceBackBuffer->GetSizeY());
			float SizeU = float(ViewportSize.X) / float(SourceBackBuffer->GetSizeX()) - U;
			float SizeV = float(ViewportSize.Y) / float(SourceBackBuffer->GetSizeY()) - V;

			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,									// Dest X, Y
				TargetSize.X,							// Dest Width
				TargetSize.Y,							// Dest Height
				U, V,									// Source U, V
				SizeU, SizeV,							// Source USize, VSize
				ViewportSize,							// Target buffer size
				FIntPoint(1, 1),						// Source texture size
				VertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();


		void* ColorDataBuffer = nullptr;

		int32 Width = 0, Height = 0;
		RHICmdList.MapStagingSurface(ReadbackTexture, ColorDataBuffer, Width, Height);

		TArray<FColor> ColorData;
		ColorData.AddUninitialized(Width * Height);
		FMemory::Memcpy(ColorData.GetData(), ColorDataBuffer, Width * Height * sizeof(FColor));

		WriteFrameToFile(ColorData, CapturedFrameNumber++);

		RHICmdList.UnmapStagingSurface(ReadbackTexture);
		AvailableEvent->Trigger();
	};
}
