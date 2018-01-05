#pragma once

#include <wrl.h>
#include <wrl/implements.h>
#include <windows.storage.streams.h>
#include <robuffer.h>
#include <vector>

namespace NativeBuffer
{
	class NativeBuffer :
		public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::RuntimeClassType::WinRtClassicComMix>,
		ABI::Windows::Storage::Streams::IBuffer,
		Windows::Storage::Streams::IBufferByteAccess>
	{
	public:
		virtual ~NativeBuffer()
		{
			if (m_free)
			{
				m_free(m_opaque);
			}
		}

		STDMETHODIMP RuntimeClassInitialize(byte *buffer, UINT totalSize)
		{
			m_length = totalSize;
			m_buffer = buffer;
			m_free = NULL;
			m_opaque = NULL;

			return S_OK;
		}

		STDMETHODIMP RuntimeClassInitialize(byte *buffer, UINT totalSize, void(*free)(void *opaque), void *opaque)
		{
			m_length = totalSize;
			m_buffer = buffer;
			m_free = free;
			m_opaque = opaque;

			return S_OK;
		}

		STDMETHODIMP Buffer(byte **value)
		{
			*value = m_buffer;

			return S_OK;
		}

		STDMETHODIMP get_Capacity(UINT32 *value)
		{
			*value = m_length;

			return S_OK;
		}

		STDMETHODIMP get_Length(UINT32 *value)
		{
			*value = m_length;

			return S_OK;
		}

		STDMETHODIMP put_Length(UINT32 value)
		{
			return E_FAIL;
		}


	private:
		UINT32 m_length;
		byte *m_buffer;
		void(*m_free)(void *opaque);
		void *m_opaque;
	};
}