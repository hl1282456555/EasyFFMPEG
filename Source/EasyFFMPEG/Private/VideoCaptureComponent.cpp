// Fill out your copyright notice in the Description page of Project Settings.


#include "VideoCaptureComponent.h"

#include "EasyFFMPEG.h"
#include "Engine/GameEngine.h"
#include "HAL/FileManager.h"

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

// Sets default values for this component's properties
UVideoCaptureComponent::UVideoCaptureComponent()
	: CaptureState(EMovieCaptureState::NotInit)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	// ...
}


void UVideoCaptureComponent::StartCapture(const FString& InVideoFilename)
{
	ShouldCutFrameCount = 0;

	if (IsInitialized()) {
		UE_LOG(LogFFmpeg, Warning, TEXT("Please uninitialize capture component before call InitCapture()."));
		return;
	}

	if (!InitFrameGrabber()) {
		UE_LOG(LogFFmpeg, Error, TEXT("Init frame grabber failed."));
		return;
	}

	VideoFilename = InVideoFilename;

	if (!CreateVideoFileWriter()) {
		UE_LOG(LogFFmpeg, Error, TEXT("Cant create the video file '%s'."), *VideoFilename);
		StopCapture();
		return;
	}

	DestroyVideoFileWriter();

	int32 result = avformat_alloc_output_context2(&FormatCtx, nullptr, nullptr, TCHAR_TO_UTF8(*VideoFilename));
	if (result < 0) {
		UE_LOG(LogFFmpeg, Error, TEXT("Can not allocate format context."));
		StopCapture();
		return;
	}

	Codec = avcodec_find_encoder(FormatCtx->oformat->video_codec);
	if (Codec == nullptr) {
		UE_LOG(LogFFmpeg, Error, TEXT("Codec not found."));
		StopCapture();
		return;
	}

	Stream = avformat_new_stream(FormatCtx, Codec);
	if (Stream == nullptr) {
		UE_LOG(LogFFmpeg, Error, TEXT("Can not allocate a new stream."));
		StopCapture();
		return;
	}

	CodecCtx = Stream->codec;
	if (Codec == nullptr) {
		UE_LOG(LogFFmpeg, Error, TEXT("InitCapture() failed: Cloud not allocate video codec context."));
		StopCapture();
		return;
	}

	Packet = av_packet_alloc();
	if (Packet == nullptr) {
		UE_LOG(LogFFmpeg, Error, TEXT("InitCapture() failed: Cloud not allocate packet."));
		StopCapture();
		return;
	}

	CodecCtx->bit_rate = CaptureConfigs.BitRate;
	CodecCtx->width = CaptureConfigs.Width;
	CodecCtx->height = CaptureConfigs.Height;
	CodecCtx->time_base = {CaptureConfigs.FrameRate.Y, CaptureConfigs.FrameRate.X};
	CodecCtx->framerate = {CaptureConfigs.FrameRate.X, CaptureConfigs.FrameRate.Y};
	CodecCtx->gop_size = CaptureConfigs.GopSize;
	CodecCtx->max_b_frames = CaptureConfigs.MaxBFrames;
	
	/*int32 pixLoss;*/
	CodecCtx->pix_fmt = AV_PIX_FMT_YUV444P/*avcodec_find_best_pix_fmt_of_list(Codec->pix_fmts, AV_PIX_FMT_RGBA, true, &pixLoss)*/;

	if (Codec->id == AV_CODEC_ID_H264) {
		av_opt_set(CodecCtx->priv_data, "preset", "slow", 0);
	}

	if (FormatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
		CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	result = avcodec_open2(CodecCtx, Codec, nullptr);
	if (result < 0) {
		UE_LOG(LogFFmpeg, Error, TEXT("Could not open codec."));
		StopCapture();
		return;
	}

	result = avcodec_parameters_from_context(Stream->codecpar, CodecCtx);
	if (result < 0) {
		UE_LOG(LogFFmpeg, Error, TEXT("Can not fill codec params."));
		StopCapture();
		return;
	}

	Stream->time_base = {CaptureConfigs.FrameRate.Y, CaptureConfigs.FrameRate.X};

	av_dump_format(FormatCtx, 0, TCHAR_TO_UTF8(*VideoFilename), 1);

	result = avio_open(&FormatCtx->pb, TCHAR_TO_UTF8(*VideoFilename), AVIO_FLAG_WRITE);
	if (result < 0) {
		UE_LOG(LogFFmpeg, Error, TEXT("Cant open the file '%s'."), *VideoFilename);
		StopCapture();
		return;
	}

	result = avformat_write_header(FormatCtx, nullptr);
	if (result < 0) {
		UE_LOG(LogFFmpeg, Error, TEXT("Error ocurred when write header into file."));
		StopCapture();
		return;
	}

	Frame = av_frame_alloc();
	if (Frame == nullptr) {
		UE_LOG(LogFFmpeg, Error, TEXT("Cloud not allocate video frame."));
		StopCapture();
		return;
	}

	Frame->format = CodecCtx->pix_fmt;
	Frame->width = CodecCtx->width;
	Frame->height = CodecCtx->height;

	result = av_frame_get_buffer(Frame, 0);
	if (result < 0) {
		UE_LOG(LogFFmpeg, Error, TEXT("Cloud not allocate the video frame data."));
		StopCapture();
		return;
	}

	CaptureState = EMovieCaptureState::Initialized;
}

bool UVideoCaptureComponent::IsInitialized()
{
	return CaptureState >= EMovieCaptureState::Initialized;
}

bool UVideoCaptureComponent::CaptureThisFrame(int32 CurrentFrame)
{
	if (CaptureState == EMovieCaptureState::NotInit || !FrameGrabber.IsValid()) {
		return false;
	}

	FrameGrabber->CaptureThisFrame(FFramePayloadPtr());
	TArray<FCapturedFrameData> frames = FrameGrabber->GetCapturedFrames();
	if (!frames.IsValidIndex(0)) {
		if (CurrentFrame == 0) {
			ShouldCutFrameCount++;
		}
		return false;
	}

	if (ShouldCutFrameCount > 1) {
		ShouldCutFrameCount--;
		return true;
	}

	FCapturedFrameData& lastFrame = frames.Last();

	//TArray<uint8> rgbColor;
	//for (int32 index = 0; index < lastFrame.ColorBuffer.Num(); index++)
	//{
	//	rgbColor.Add(lastFrame.ColorBuffer[index].R);
	//	rgbColor.Add(lastFrame.ColorBuffer[index].G);
	//	rgbColor.Add(lastFrame.ColorBuffer[index].B);
	//	rgbColor.Add(lastFrame.ColorBuffer[index].A);
	//}

	WriteFrameToFile(lastFrame.ColorBuffer, CurrentFrame);

	return true;
}

void UVideoCaptureComponent::StopCapture()
{
	ReleaseFrameGrabber();
	DestroyVideoFileWriter();
	ReleaseContext();

	CaptureState = EMovieCaptureState::NotInit;
}

// Called when the game starts
void UVideoCaptureComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}

void UVideoCaptureComponent::BeginDestroy()
{
	Super::BeginDestroy();
	StopCapture();
}

bool UVideoCaptureComponent::CreateVideoFileWriter()
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

void UVideoCaptureComponent::DestroyVideoFileWriter()
{
	if (Writer == nullptr) {
		return;
	}

	Writer->Flush();
	Writer->Close();

	delete Writer;
	Writer = nullptr;
}

bool UVideoCaptureComponent::InitFrameGrabber()
{
	if (FrameGrabber.IsValid()) {
		return true;
	}

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


	FrameGrabber = MakeShareable(new FFrameGrabber(sceneViewport.ToSharedRef(), FIntPoint(CaptureConfigs.Width, CaptureConfigs.Height)));
	FrameGrabber->StartCapturingFrames();

	return true;
}

void UVideoCaptureComponent::ReleaseFrameGrabber()
{
	if (FrameGrabber.IsValid()){
		FrameGrabber->StopCapturingFrames();
		FrameGrabber->Shutdown();
		FrameGrabber.Reset();
	}
}


void UVideoCaptureComponent::ReleaseContext()
{
	if (CaptureState != EMovieCaptureState::NotInit) {
		EncodeVideoFrame(CodecCtx, nullptr, Packet);

		av_write_trailer(FormatCtx);
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

void UVideoCaptureComponent::WriteFrameToFile(const TArray<FColor>& ColorBuffer, int32 CurrentFrame)
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

void UVideoCaptureComponent::EncodeVideoFrame(struct AVCodecContext* InCodecCtx, struct AVFrame* InFrame, struct AVPacket* InPacket)
{
	int32 result = avcodec_send_frame(InCodecCtx, InFrame);
	if (result < 0) {
		UE_LOG(LogFFmpeg, Error, TEXT("Error sending a frame for encoding."));
		return;
	}

	while(result >= 0)
	{

		InPacket->data = nullptr;
		InPacket->size = 0;

		result = avcodec_receive_packet(InCodecCtx, InPacket);
		if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
			return;
		}
		else if (result < 0) {
			UE_LOG(LogFFmpeg, Error, TEXT("Error during encoding."));
			return;
		}

		AVRational tempRational;
		tempRational.num = CaptureConfigs.FrameRate.Y;
		tempRational.den = CaptureConfigs.FrameRate.X;

		av_packet_rescale_ts(InPacket, tempRational, Stream->time_base);

		InPacket->stream_index = Stream->index;

		result = av_interleaved_write_frame(FormatCtx, InPacket);
		if (result < 0) {
			UE_LOG(LogFFmpeg, Error, TEXT("Error during interleaved write frame."));
			return;
		}

		av_packet_unref(InPacket);
	}
}

// Called every frame
void UVideoCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}
