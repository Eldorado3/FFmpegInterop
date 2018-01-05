//*****************************************************************************
//
//	Copyright 2015 Microsoft Corporation
//
//	Licensed under the Apache License, Version 2.0 (the "License");
//	you may not use this file except in compliance with the License.
//	You may obtain a copy of the License at
//
//	http ://www.apache.org/licenses/LICENSE-2.0
//
//	Unless required by applicable law or agreed to in writing, software
//	distributed under the License is distributed on an "AS IS" BASIS,
//	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//	See the License for the specific language governing permissions and
//	limitations under the License.
//
//*****************************************************************************

#include "pch.h"
#include "UncompressedVideoSampleProvider.h"
#include "NativeBufferFactory.h"
#include <mfapi.h>

extern "C"
{
#include <libavutil/imgutils.h>
}


using namespace FFmpegInterop;
using namespace NativeBuffer;
using namespace Windows::Media::MediaProperties;

UncompressedVideoSampleProvider::UncompressedVideoSampleProvider(
	FFmpegReader^ reader,
	AVFormatContext* avFormatCtx,
	AVCodecContext* avCodecCtx)
	: UncompressedSampleProvider(reader, avFormatCtx, avCodecCtx)
{
	switch (m_pAvCodecCtx->pix_fmt)
	{
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
		m_OutputPixelFormat = AV_PIX_FMT_YUV420P;
		OutputMediaSubtype = MediaEncodingSubtypes::Iyuv;
		break;
	case AV_PIX_FMT_YUVA420P:
		m_OutputPixelFormat = AV_PIX_FMT_BGRA;
		OutputMediaSubtype = MediaEncodingSubtypes::Argb32;
		break;
	default:
		m_OutputPixelFormat = AV_PIX_FMT_NV12;
		OutputMediaSubtype = MediaEncodingSubtypes::Nv12;
		break;
	}
}

HRESULT UncompressedVideoSampleProvider::AllocateResources()
{
	HRESULT hr = S_OK;
	hr = UncompressedSampleProvider::AllocateResources();
	if (SUCCEEDED(hr))
	{
		if (m_pAvCodecCtx->pix_fmt != AV_PIX_FMT_YUV420P && m_pAvCodecCtx->pix_fmt != AV_PIX_FMT_YUVJ420P)
		{
			// Setup software scaler to convert any unsupported decoder pixel format to NV12 that is supported in Windows & Windows Phone MediaElement
			m_pSwsCtx = sws_getContext(
				m_pAvCodecCtx->width,
				m_pAvCodecCtx->height,
				m_pAvCodecCtx->pix_fmt,
				m_pAvCodecCtx->width,
				m_pAvCodecCtx->height,
				m_OutputPixelFormat,
				SWS_BICUBIC,
				NULL,
				NULL,
				NULL);

			if (m_pSwsCtx == nullptr)
			{
				hr = E_OUTOFMEMORY;
			}

			if (SUCCEEDED(hr))
			{
				if (av_image_alloc(m_rgVideoBufferData, m_rgVideoBufferLineSize, m_pAvCodecCtx->width, m_pAvCodecCtx->height, m_OutputPixelFormat, 1) < 0)
				{
					hr = E_FAIL;
				}
			}
		}
		else
		{
			if (m_pAvCodecCtx->codec->capabilities & AV_CODEC_CAP_DR1)
			{
				m_pAvCodecCtx->get_buffer2 = &get_buffer2;
				m_pAvCodecCtx->opaque = (void*)this;
			}
		}
	}

	return hr;
}

UncompressedVideoSampleProvider::~UncompressedVideoSampleProvider()
{
	if (m_pAvFrame)
	{
		av_frame_free(&m_pAvFrame);
	}

	if (m_rgVideoBufferData)
	{
		av_freep(m_rgVideoBufferData);
	}

	if (m_pBufferPool)
	{
		av_buffer_pool_uninit(&m_pBufferPool);
	}
}

HRESULT UncompressedVideoSampleProvider::DecodeAVPacket(DataWriter^ dataWriter, AVPacket* avPacket, int64_t& framePts, int64_t& frameDuration)
{
	HRESULT hr = S_OK;
	hr = UncompressedSampleProvider::DecodeAVPacket(dataWriter, avPacket, framePts, frameDuration);

	// Don't set a timestamp on S_FALSE
	if (hr == S_OK)
	{
		// Try to get the best effort timestamp for the frame.
		framePts = av_frame_get_best_effort_timestamp(m_pAvFrame);
		m_interlaced_frame = m_pAvFrame->interlaced_frame == 1;
		m_top_field_first = m_pAvFrame->top_field_first == 1;
	}

	return hr;
}

MediaStreamSample^ UncompressedVideoSampleProvider::GetNextSample()
{
	MediaStreamSample^ sample = MediaSampleProvider::GetNextSample();

	if (sample != nullptr)
	{
		if (m_interlaced_frame)
		{
			sample->ExtendedProperties->Insert(MFSampleExtension_Interlaced, TRUE);
			sample->ExtendedProperties->Insert(MFSampleExtension_BottomFieldFirst, m_top_field_first ? safe_cast<Platform::Object^>(FALSE) : TRUE);
			sample->ExtendedProperties->Insert(MFSampleExtension_RepeatFirstField, safe_cast<Platform::Object^>(FALSE));
		}
		else
		{
			sample->ExtendedProperties->Insert(MFSampleExtension_Interlaced, safe_cast<Platform::Object^>(FALSE));
		}
	}

	return sample;
}

HRESULT UncompressedVideoSampleProvider::WriteAVPacketToStream(DataWriter^ dataWriter, AVPacket* avPacket)
{
	if (m_OutputPixelFormat == AV_PIX_FMT_YUV420P)
	{
		// ffmpeg does not allocate contiguous buffers for YUV, so we need to manually copy all three planes
		auto YBuffer = Platform::ArrayReference<uint8_t>(m_pAvFrame->data[0], m_pAvFrame->linesize[0] * m_pAvCodecCtx->height);
		auto UBuffer = Platform::ArrayReference<uint8_t>(m_pAvFrame->data[1], m_pAvFrame->linesize[1] * m_pAvCodecCtx->height / 2);
		auto VBuffer = Platform::ArrayReference<uint8_t>(m_pAvFrame->data[2], m_pAvFrame->linesize[2] * m_pAvCodecCtx->height / 2);
		dataWriter->WriteBytes(YBuffer);
		dataWriter->WriteBytes(UBuffer);
		dataWriter->WriteBytes(VBuffer);
	}
	else
	{
		// Convert decoded video pixel format to NV12 using FFmpeg software scaler
		if (sws_scale(m_pSwsCtx, (const uint8_t **)(m_pAvFrame->data), m_pAvFrame->linesize, 0, m_pAvCodecCtx->height, m_rgVideoBufferData, m_rgVideoBufferLineSize) < 0)
		{
			return E_FAIL;
		}

		// we allocate a contiguous buffer for sws_scale, so we do not have to copy YUV planes separately
		auto size = m_OutputPixelFormat == AVPixelFormat::AV_PIX_FMT_BGRA
			? m_rgVideoBufferLineSize[0] * m_pAvCodecCtx->height
			: (m_rgVideoBufferLineSize[0] * m_pAvCodecCtx->height) + (m_rgVideoBufferLineSize[1] * m_pAvCodecCtx->height / 2);
		auto buffer = Platform::ArrayReference<uint8_t>(m_rgVideoBufferData[0], size);
		dataWriter->WriteBytes(buffer);
	}

	av_frame_unref(m_pAvFrame);
	av_frame_free(&m_pAvFrame);

	return S_OK;
}

static void free_buffer(void *lpVoid)
{
	auto buffer = (AVBufferRef *)lpVoid;
	av_buffer_unref(&buffer);
}

int UncompressedVideoSampleProvider::get_buffer2(AVCodecContext *avCodecContext, AVFrame *frame, int flags)
{
	auto provider = reinterpret_cast<UncompressedVideoSampleProvider^>(avCodecContext->opaque);

	auto width = frame->width;
	auto height = frame->height;
	avcodec_align_dimensions(avCodecContext, &width, &height);

	frame->linesize[0] = width;
	frame->linesize[1] = width / 2;
	frame->linesize[2] = width / 2;
	frame->linesize[3] = 0;

	auto YBufferSize = frame->linesize[0] * height;
	auto UBufferSize = frame->linesize[1] * height / 2;
	auto VBufferSize = frame->linesize[2] * height / 2;
	auto totalSize = YBufferSize + UBufferSize + VBufferSize;

	if (!provider->m_pBufferPool)
	{
		provider->m_pBufferPool = av_buffer_pool_init(totalSize, NULL);
		if (!provider->m_pBufferPool)
		{
			return ERROR;
		}
	}

	auto buffer = av_buffer_pool_get(provider->m_pBufferPool);
	if (!buffer)
	{
		return ERROR;
	}

	if (buffer->size != totalSize)
	{
		av_buffer_unref(&buffer);
		return ERROR;
	}

	frame->buf[0] = buffer;
	frame->data[0] = buffer->data;
	frame->data[1] = buffer->data + YBufferSize;
	frame->data[2] = buffer->data + UBufferSize;
	frame->data[3] = NULL;
	frame->extended_data = frame->data;

	auto bufferRef = av_buffer_ref(buffer);
	auto ibuffer = NativeBufferFactory::CreateNativeBuffer(bufferRef->data, totalSize, &free_buffer, bufferRef);

	return 0;
}
