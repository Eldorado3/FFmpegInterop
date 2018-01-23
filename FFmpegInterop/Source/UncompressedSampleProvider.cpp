//*****************************************************************************
//
//	Copyright 2016 Microsoft Corporation
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
#include "UncompressedSampleProvider.h"

using namespace FFmpegInterop;

UncompressedSampleProvider::UncompressedSampleProvider(FFmpegReader^ reader, AVFormatContext* avFormatCtx, AVCodecContext* avCodecCtx)
	: MediaSampleProvider(reader, avFormatCtx, avCodecCtx)
	, m_pAvFrame(nullptr)
{
}

HRESULT UncompressedSampleProvider::ProcessDecodedFrame(DataWriter^ dataWriter)
{
	return S_OK;
}

// Return S_FALSE for an incomplete frame
HRESULT UncompressedSampleProvider::GetFrameFromFFmpegDecoder(AVPacket* avPacket)
{

	DebugMessage(L"Decoding packet \n");

	HRESULT hr = S_OK;
	int decodeFrame = 0;

	int ret = AVERROR(EAGAIN);

	if (avPacket != nullptr)
	{
		int sendPacketResult = avcodec_send_packet(m_pAvCodecCtx, avPacket);
		if (sendPacketResult == AVERROR(EAGAIN))
		{
			// The decoder should have been drained and always ready to access input
			_ASSERT(FALSE);
			DebugMessage(L"Decoder didn't return EAGAIN when feeding packet\n");
			hr = E_UNEXPECTED;
		}
		else if (sendPacketResult < 0)
		{
			// We failed to send the packet
			hr = E_FAIL;
			DebugMessage(L"Decoder failed on the sample\n");
		}
	}

	/*
	if (SUCCEEDED(hr))
	{
		//binxie: drain the decoder 
		AVFrame *pFrame = av_frame_alloc();

		bool GotFrame = false;

		do {
			ret = avcodec_receive_frame(m_pAvCodecCtx, pFrame);

			if (ret == AVERROR_EOF) {
				avcodec_flush_buffers(m_pAvCodecCtx);
				av_frame_unref(pFrame);
				av_frame_free(&pFrame);
				DebugMessage(L"reaching EOF \n");
			}

			if (ret >= 0) {
				m_pAvFrame = pFrame;
				GotFrame = true;

				DebugMessage(L"Drain 1 frame\n");
			}

			//what if there is some other error??

			if (ret < 0 && ret != AVERROR(EAGAIN)) {

				hr = E_FAIL;
				av_frame_unref(pFrame);
				av_frame_free(&pFrame);
				DebugMessage(L"Failed to get a frame from the decoder\n");

				return hr;
			}

		} while (ret != AVERROR(EAGAIN));

		if (ret == AVERROR(EAGAIN) && !GotFrame) {

			// The decoder doesn't have enough data to produce a frame,
			// return S_FALSE to indicate a partial frame
			hr = S_FALSE;
			av_frame_unref(pFrame);
			av_frame_free(&pFrame);

			DebugMessage(L"Don't have enough info to decode\n");
		}
		else {

			DebugMessage(L"Got frame\n");
		}
	
	} */

	
	if (SUCCEEDED(hr))
	{
		AVFrame *pFrame = av_frame_alloc();
		// Try to get a frame from the decoder.
		decodeFrame = avcodec_receive_frame(m_pAvCodecCtx, pFrame);

		// The decoder is empty, send a packet to it.
		if (decodeFrame == AVERROR(EAGAIN))
		{
			// The decoder doesn't have enough data to produce a frame,
			// return S_FALSE to indicate a partial frame
			hr = S_FALSE;
			av_frame_unref(pFrame);
			av_frame_free(&pFrame);
		}
		else if (decodeFrame < 0)
		{
			hr = E_FAIL;
			av_frame_unref(pFrame);
			av_frame_free(&pFrame);
			DebugMessage(L"Failed to get a frame from the decoder\n");
		}
		else
		{
			m_pAvFrame = pFrame;
		}
	}

	

	return hr;
}

HRESULT UncompressedSampleProvider::DecodeAVPacket(DataWriter^ dataWriter, AVPacket* avPacket, int64_t& framePts, int64_t& frameDuration)
{
	HRESULT hr = S_OK;
	bool fGotFrame  = false;
	AVPacket *pPacket = avPacket;

	while (SUCCEEDED(hr))
	{
		hr = GetFrameFromFFmpegDecoder(pPacket);
		pPacket = nullptr;
		if (SUCCEEDED(hr))
		{
			if (hr == S_FALSE)
			{
				// If the decoder didn't give an initial frame we still need
				// to feed it more frames. Keep S_FALSE as the result
				if (fGotFrame)
				{
					hr = S_OK;
				}
				break;
			}
			// Update the timestamp if the packet has one
			else if (m_pAvFrame->pts != AV_NOPTS_VALUE)
			{
				framePts = m_pAvFrame->pts;
				frameDuration = m_pAvFrame->pkt_duration;
			}
			fGotFrame = true;

			hr = ProcessDecodedFrame(dataWriter);
		}
	}

	return hr;
}
