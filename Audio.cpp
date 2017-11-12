// Peaberry CW - Transceiver for Peaberry SDR
// Copyright (C) 2015 David Turnbull AE9RB
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "audio.h"
#include "avrt.h"
#include "endpointvolume.h"
#define _USE_MATH_DEFINES
#include "math.h"

#include "LC_ExtIO_Types.h"

// HDSDR callback function.
extern pfnExtIOCallback	pfnCallback;

// Extra Windows constants
#define pcw_KSDATAFORMAT_SUBTYPE_PCM (GUID) {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}}
#define pcw_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT (GUID) {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}}
PROPERTYKEY PKEY_Device_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
PROPERTYKEY PKEY_DeviceInterface_FriendlyName = { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 2 };
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);
const IID IID_IAudioEndpointVolume = __uuidof(IAudioEndpointVolume);

// Error handling Microsoft-style.
#define SAFE_RELEASE(punk)  if ((punk) != NULL) { (punk)->Release(); (punk) = NULL; }
#define EXIT_ON_HR_ERROR(msg) if (FAILED(hr)) { error = msg; goto Exit; }
#define EXIT_ERROR(msg) {error = msg; goto Exit;}

// Long message about exclusive mode used in multiple places
static const char DOES_NOT_ALLOW_EXCLUSIVE[] =
"device does not allow exclusive mode. This needs "
"to be turned on in Control Panel > Sound > Properties > Advanced.";

static void fatal_exit(const std::string &msg)
{
	::OutputDebugStringA(msg.c_str());
	::OutputDebugStringA("\n");
	__debugbreak();
}

 #define HAS_TX_AUDIO

void Audio::init()
{
	HRESULT hr;
	IMMDeviceEnumerator *pEnumerator = NULL;
	WAVEFORMATEXTENSIBLE *wavex = NULL;
	WAVEFORMATEX *wfx = NULL;
	LPWSTR transmitID = NULL;

	// Friendly error prefixes
	static const std::string ERROR_RECEIVE("Peaberry Radio recording ");
	static const std::string ERROR_TRANSMIT("Peaberry Radio playback ");

	// Peabery Audio events never fire anywhere I tested
	receiveCallbackMode = false;
	transmitCallbackMode = false;

	// The main Qt thread already has this initialized
	//hr = CoInitialize(NULL);
	//EXIT_ON_HR_ERROR("CoInitialize");

	// Operating systems without WASAPI fail here
	hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, NULL,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);
	EXIT_ON_HR_ERROR("Incompatible Operating System.");

	wavex = (WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));
	wfx = (WAVEFORMATEX*)wavex;
	if (wavex == NULL) EXIT_ERROR("CoTaskMemAlloc(WAVEFORMATEX)");

	findPeaberry(pEnumerator, false, &receiveDevice, &receiveAudioClient, wavex, ERROR_RECEIVE);
	if (!receiveDevice) goto Exit;
	setPeaberryVolume(receiveDevice, ERROR_RECEIVE);
	if (!error.empty()) goto Exit;
	initializeAudioClient(receiveDevice, wfx, &receiveAudioClient, &receiveCallbackMode, &receiveBufferFrames, ERROR_RECEIVE);
	if (!error.empty()) goto Exit;
	//qDebug() << "receiveCallbackMode=" << receiveCallbackMode << "receiveBufferFrames=" << receiveBufferFrames;

#ifdef HAS_TX_AUDIO
	findPeaberry(pEnumerator, true, &transmitDevice, &transmitAudioClient, wavex, ERROR_TRANSMIT);
	if (!transmitDevice) goto Exit;
	setPeaberryVolume(transmitDevice, ERROR_TRANSMIT);
	if (!error.empty()) goto Exit;
	initializeAudioClient(transmitDevice, wfx, &transmitAudioClient, &transmitCallbackMode, &transmitBufferFrames, ERROR_TRANSMIT);
	if (!error.empty()) goto Exit;
	//qDebug() << "transmitCallbackMode=" << transmitCallbackMode << "transmitBufferFrames=" << transmitBufferFrames;
#endif

	receiveEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	transmitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (receiveEvent == NULL || transmitEvent == NULL) {
		error = "CreateEvent";
		goto Exit;
	}

	if (receiveCallbackMode) {
		hr = receiveAudioClient->SetEventHandle(receiveEvent);
		EXIT_ON_HR_ERROR(ERROR_RECEIVE + "IAudioClient::SetEventHandle");
	}
#ifdef HAS_TX_AUDIO
	if (transmitCallbackMode) {
		hr = transmitAudioClient->SetEventHandle(transmitEvent);
		EXIT_ON_HR_ERROR(ERROR_TRANSMIT + "IAudioClient::SetEventHandle");
	}
#endif

	hr = receiveAudioClient->GetService(
		IID_IAudioCaptureClient,
		(void**)&receiveCaptureClient);
	EXIT_ON_HR_ERROR(ERROR_RECEIVE + "IAudioClient::GetService(IAudioCaptureClient)");

#ifdef HAS_TX_AUDIO
	hr = transmitAudioClient->GetService(
		IID_IAudioRenderClient,
		(void**)&transmitRenderClient);
	EXIT_ON_HR_ERROR(ERROR_TRANSMIT + "IAudioClient::GetService(IAudioRenderClient)");
#endif

Exit:
	CoTaskMemFree(transmitID);
	CoTaskMemFree(wavex);
	SAFE_RELEASE(pEnumerator);
}

void Audio::destroy()
{
	if (transmitEvent) CloseHandle(transmitEvent);
	SAFE_RELEASE(transmitDevice);
	SAFE_RELEASE(transmitAudioClient);
	SAFE_RELEASE(transmitRenderClient);

	if (receiveEvent) CloseHandle(receiveEvent);
	SAFE_RELEASE(receiveDevice);
	SAFE_RELEASE(receiveAudioClient);
	SAFE_RELEASE(receiveCaptureClient);
}

int Audio::mutePaddingFrames()
{
	// Extra 3ms from WASAPI in each receive and transmit buffers
	return int(transmitBufferFrames + receiveBufferFrames + 0.006 * SAMPLE_RATE);
}

double Audio::transmitPaddingSecs()
{
	// Extra 3ms is WASPI internal buffer
	return (double)transmitBufferFrames / SAMPLE_RATE + 0.003;
}

void Audio::findPeaberry(IMMDeviceEnumerator *pEnumerator,
	bool isTransmit,
	IMMDevice **device,
	IAudioClient **audioClient,
	WAVEFORMATEXTENSIBLE *wavex,
	const std::string &errstr)
{
	HRESULT hr;
	IMMDeviceCollection *pCollection = NULL;
	IPropertyStore *pProps = NULL;
	IMMDevice *pDevice = NULL;
	std::string errname;
	EDataFlow dataflow = isTransmit ? eRender : eCapture;
	PROPVARIANT varName;
	PropVariantInit(&varName);

	hr = pEnumerator->EnumAudioEndpoints(
		dataflow, DEVICE_STATE_ACTIVE,
		&pCollection);
	EXIT_ON_HR_ERROR("IMMDeviceEnumerator::EnumAudioEndpoints");

	UINT count;
	hr = pCollection->GetCount(&count);
	EXIT_ON_HR_ERROR("IMMDeviceCollection::GetCount");

	for (UINT i = 0; i < count; i++)
	{
		hr = pCollection->Item(i, &pDevice);
		EXIT_ON_HR_ERROR("IMMDeviceCollection::Item");

		hr = pDevice->OpenPropertyStore(
			STGM_READ, &pProps);
		EXIT_ON_HR_ERROR("IMMDevice::OpenPropertyStore");

		PropVariantClear(&varName);
		hr = pProps->GetValue(PKEY_DeviceInterface_FriendlyName, &varName);
		EXIT_ON_HR_ERROR("IPropertyStore::GetValue");

		std::wstring devName(varName.pwszVal);

		SAFE_RELEASE(pProps);
		if (devName.find(L"Peaberry Radio") != std::wstring::npos) {
			*device = pDevice;
			pDevice = NULL;
			break;
		}
		SAFE_RELEASE(pDevice);
	}

	if (!(*device)) EXIT_ERROR(errstr + "device not found.");

	hr = (*device)->Activate(
		IID_IAudioClient, CLSCTX_ALL,
		NULL, (void**)audioClient);
	EXIT_ON_HR_ERROR(errstr + "IMMDevice::Activate");

	wavex->Format.nSamplesPerSec = SAMPLE_RATE;
	wavex->Format.nChannels = 2;
	wavex->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
	wavex->Format.wBitsPerSample = 16;
	wavex->Samples.wValidBitsPerSample = 16;
	wavex->Format.nBlockAlign = (wavex->Format.wBitsPerSample >> 3) * wavex->Format.nChannels;
	wavex->Format.nAvgBytesPerSec = wavex->Format.nBlockAlign * wavex->Format.nSamplesPerSec;
	wavex->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wavex->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
//	wavex->SubFormat = pcw_KSDATAFORMAT_SUBTYPE_PCM;
	wavex->SubFormat = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
	hr = (*audioClient)->IsFormatSupported(
		AUDCLNT_SHAREMODE_EXCLUSIVE,
		(WAVEFORMATEX*)wavex, NULL);
	if (FAILED(hr)) {
		wavex->Format.cbSize = 0;
		wavex->Format.wFormatTag = WAVE_FORMAT_PCM;
		hr = (*audioClient)->IsFormatSupported(
			AUDCLNT_SHAREMODE_EXCLUSIVE,
			(WAVEFORMATEX*)wavex, NULL);
	}

	if (hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED) {
		SAFE_RELEASE(*device);
		EXIT_ERROR(errstr + DOES_NOT_ALLOW_EXCLUSIVE);
	}
	if (FAILED(hr)) {
		SAFE_RELEASE(*device);
		EXIT_ERROR(errstr + "device does not support expected format.");
	}

Exit:
	PropVariantClear(&varName);
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pProps);
	SAFE_RELEASE(pCollection);
}

// A common problem is that users fiddle with volume controls. Especially
// after Windows decides that your new radio should transmit system sounds.
void Audio::setPeaberryVolume(IMMDevice *device, const std::string &errstr)
{
	HRESULT hr;
	IAudioEndpointVolume *pEndptVol = NULL;

	hr = device->Activate(IID_IAudioEndpointVolume,
		CLSCTX_ALL, NULL, (void**)&pEndptVol);
	EXIT_ON_HR_ERROR(errstr + "IMMDevice::Activate(IAudioEndpointVolume)");

	hr = pEndptVol->SetMute(FALSE, NULL);
	EXIT_ON_HR_ERROR(errstr + "IAudioEndpointVolume::SetMute");

	hr = pEndptVol->SetMasterVolumeLevelScalar(1.0, NULL);
	EXIT_ON_HR_ERROR(errstr + "IAudioEndpointVolume::SetMasterVolumeLevelScalar");

	hr = pEndptVol->SetChannelVolumeLevelScalar(0, 1.0, NULL);
	EXIT_ON_HR_ERROR(errstr + "IAudioEndpointVolume::SetChannelVolumeLevelScalar(0)");

	hr = pEndptVol->SetChannelVolumeLevelScalar(1, 1.0, NULL);
	EXIT_ON_HR_ERROR(errstr + "IAudioEndpointVolume::SetChannelVolumeLevelScalar(1)");

Exit:
	SAFE_RELEASE(pEndptVol);
}

void Audio::initializeAudioClient(IMMDevice *device, WAVEFORMATEX *wfx, IAudioClient **audioClient, bool *callbackMode, UINT32 *bufferSize, const std::string &errstr)
{
	HRESULT hr;
	REFERENCE_TIME hnsBufferDuration;
	bool bufferRetried = false;
	DWORD streamFlags = 0;
	if (*callbackMode == true) streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

	// I tried to check PKEY_AudioEndpoint_Supports_EventDriven_Mode
	// but the vendor may not have that correctly set in the ini file.
	// Maybe there's a better way than checking for E_INVALIDARG.

	hr = (*audioClient)->GetDevicePeriod(NULL, &hnsBufferDuration);
	EXIT_ON_HR_ERROR(errstr + "IAudioClient::GetDevicePeriod");

	while (true) {
		hr = (*audioClient)->Initialize(
			AUDCLNT_SHAREMODE_EXCLUSIVE,
			streamFlags,
			hnsBufferDuration,
			hnsBufferDuration,
			wfx,
			NULL);
		if (hr == E_INVALIDARG) {
			if (streamFlags == 0) EXIT_ERROR(errstr + "IAudioClient::Initialize == E_INVALIDARG");
			streamFlags = 0;
			*callbackMode = false;
		}
		else if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
			if (bufferRetried) EXIT_ERROR(errstr + "IAudioClient::Initialize == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED");
			bufferRetried = true;
			hr = (*audioClient)->GetBufferSize(bufferSize);
			EXIT_ON_HR_ERROR(errstr + "audioClient->GetBufferSize");
			hnsBufferDuration = REFERENCE_TIME(((10000.0 * 1000 / wfx->nSamplesPerSec * (*bufferSize)) + 0.5));
		}
		else if (SUCCEEDED(hr)) {
			hr = (*audioClient)->GetBufferSize(bufferSize);
			EXIT_ON_HR_ERROR(errstr + "IAudioClient::GetBufferSize");
			break;
		}
		else EXIT_ERROR(errstr + "IAudioClient::Initialize");
		// reset for a retry
		SAFE_RELEASE(*audioClient);
		hr = device->Activate(
			IID_IAudioClient, CLSCTX_ALL,
			NULL, (void**)audioClient);
		EXIT_ON_HR_ERROR(errstr + "IMMDevice::Activate");
	}

Exit:
	return;
}

DWORD WINAPI Audio::thread_function(LPVOID lpParam)
{
	Audio *pthis = (Audio*)lpParam;
	pthis->run();
	return 0;
}

void Audio::start()
{
	if (m_hThread != nullptr)
		return;

	m_exit_thread = false;
	m_muted = false;
	m_mute_zeros = 0;
	m_unmute_cntr = 0;
	m_hThread = ::CreateThread(nullptr, 0, &Audio::thread_function, (void*)this, CREATE_SUSPENDED, nullptr);
	::SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);
	::ResumeThread(m_hThread);
}

void Audio::stop()
{
	if (m_hThread == nullptr)
		return;
	m_exit_thread = true;
	WaitForMultipleObjects(1, &m_hThread, TRUE, INFINITE);
	CloseHandle(m_hThread);
	m_hThread = nullptr;
}

void Audio::run()
{
	HRESULT hr;
	UINT32 padding;
	HANDLE events[] = { this->transmitEvent };
	DWORD taskIndex = 0;
	HANDLE mmtc = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
	UINT dwMilliseconds = 5;
	if (!this->transmitCallbackMode)
		dwMilliseconds = 1;
	timeBeginPeriod(dwMilliseconds);

	m_receive_buffer.assign((EXT_BLOCKLEN + MUTE_ENVELOPE_LEN + ZEROS_TO_MUTE) * 2, 0.f);
	m_receive_buffer_prev.assign(m_receive_buffer_prev.size(), 0.f);
	m_receive_buffer_cnt = 0;

	// 2 milliseconds
	m_unmute_envelope.assign(MUTE_ENVELOPE_LEN, 0.f);
	for (size_t i = 0; i < m_unmute_envelope.size(); ++ i)
		m_unmute_envelope[i] = 0.5f + 0.5f * cos((float(i) + 0.5f) * M_PI / float(m_unmute_envelope.size()));

	// Prime buffers before calling start
	// Necessary for some drivers to know callback timing
	doTransmit(this->transmitBufferFrames);

	hr = this->receiveAudioClient->Start();
	if (FAILED(hr)) fatal_exit("receiveAudioClient->Start");
#ifdef HAS_TX_AUDIO
	hr = this->transmitAudioClient->Start();
	if (FAILED(hr)) fatal_exit("transmitAudioClient->Start");
#endif

	while (! m_exit_thread) {
		switch (WaitForMultipleObjects(1, events, false, dwMilliseconds)) {
		case WAIT_OBJECT_0 + 0:
			doTransmit(this->transmitBufferFrames);
			break;
		case WAIT_TIMEOUT:
			break;
		default:
			fatal_exit("WaitForMultipleObjects");
			break;
		}
#ifdef HAS_TX_AUDIO
		if (!this->transmitCallbackMode) {
			hr = this->transmitAudioClient->GetCurrentPadding(&padding);
			if (SUCCEEDED(hr))
				doTransmit(this->transmitBufferFrames - padding);
		}
#endif
		doReceive();
	}

	hr = this->receiveAudioClient->Stop();
	if (FAILED(hr)) fatal_exit("receiveAudioClient->Stop");
#ifdef HAS_TX_AUDIO
	hr = this->transmitAudioClient->Stop();
	if (FAILED(hr)) fatal_exit("transmitAudioClient->Stop");
#endif

	timeEndPeriod(dwMilliseconds);
	if (mmtc) AvRevertMmThreadCharacteristics(mmtc);
}

static void hdsdr_mute(bool mute)
{
	pfnCallback(-1, mute ? extHw_Audio_MUTE_ON : extHw_Audio_MUTE_OFF, 0, nullptr);
}

void Audio::doReceive()
{
	HRESULT hr;
	INT16 *buf;
	UINT32 numFramesToRead;
	DWORD dwFlags;
	hr = this->receiveCaptureClient->GetBuffer((BYTE**)&buf, &numFramesToRead, &dwFlags, NULL, NULL);
	if (!numFramesToRead || FAILED(hr)) return;
	int samples = numFramesToRead * 2;
	// Process the receive buffer.
	while (numFramesToRead > 0) {
		while (numFramesToRead > 0 && m_receive_buffer_cnt < EXT_BLOCKLEN + MUTE_ENVELOPE_LEN + ZEROS_TO_MUTE) {
			m_receive_buffer[m_receive_buffer_cnt * 2] = float(*buf++) / 32768.f;
			m_receive_buffer[m_receive_buffer_cnt * 2 + 1] = float(*buf++) / 32768.f;
			++m_receive_buffer_cnt;
			--numFramesToRead;
		}
		if (m_receive_buffer_cnt == EXT_BLOCKLEN + MUTE_ENVELOPE_LEN + ZEROS_TO_MUTE) {
			for (size_t i = 2 * (MUTE_ENVELOPE_LEN + ZEROS_TO_MUTE); i < 2 * (EXT_BLOCKLEN + MUTE_ENVELOPE_LEN + ZEROS_TO_MUTE); i += 2) {
				if (m_muted) {
					if (m_receive_buffer[i] != 0 || m_receive_buffer[i+1] != 0) {
						// Unmute receiver slowly by an unmute envelope.
						m_muted = false;
						hdsdr_mute(false);
						m_mute_zeros = 0;
						m_unmute_cntr = m_unmute_envelope.size();
					}
				} else {
					// not muted
					if (m_receive_buffer[i] == 0 && m_receive_buffer[i+1] == 0) {
						if (++m_mute_zeros == ZEROS_TO_MUTE) {
							// Mute the receiver.
							m_muted = true;
							hdsdr_mute(true);
							// Taper the received audio to zero.
							int j = std::max<int>(0, int(i / 2) - int(m_mute_zeros));
							int start = std::max<int>(0, j - int(m_unmute_envelope.size()) + 1);
							for (int k = int(m_unmute_envelope.size()); j >= start; --j) {
								float a = m_unmute_envelope[-- k];
								m_receive_buffer[j*2] *= a;
								m_receive_buffer[j*2+1] *= a;
							}
						}
					} else
						m_mute_zeros = 0;
				}
				if (m_unmute_cntr > 0) {
					float a = m_unmute_envelope[--m_unmute_cntr];
					m_receive_buffer[i  ] *= a;
					m_receive_buffer[i+1] *= a;
					if (m_unmute_cntr == 0)
						m_muted = false;
				}
			}
			pfnCallback(EXT_BLOCKLEN, 0, 0.0f, m_receive_buffer.data());
			memcpy((char*)m_receive_buffer.data(), (char*)m_receive_buffer.data() + 8 * EXT_BLOCKLEN, 8 * (MUTE_ENVELOPE_LEN + ZEROS_TO_MUTE));
//			m_receive_buffer.swap(m_receive_buffer_prev);
			m_receive_buffer_cnt = MUTE_ENVELOPE_LEN + ZEROS_TO_MUTE;
		}
	}
	this->receiveCaptureClient->ReleaseBuffer(numFramesToRead);
	doReceive();
}

void Audio::doTransmit(int frames)
{
	if (!frames) return;
	HRESULT hr;
	INT16 *buf;
	hr = this->transmitRenderClient->GetBuffer(frames, (BYTE**)&buf);
	if (FAILED(hr)) return;
	// Process buf here.
	hr = this->transmitRenderClient->ReleaseBuffer(frames, 0);
}
