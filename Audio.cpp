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

	findPeaberry(pEnumerator, true, &transmitDevice, &transmitAudioClient, wavex, ERROR_TRANSMIT);
	if (!transmitDevice) goto Exit;
	setPeaberryVolume(transmitDevice, ERROR_TRANSMIT);
	if (!error.empty()) goto Exit;
	initializeAudioClient(transmitDevice, wfx, &transmitAudioClient, &transmitCallbackMode, &transmitBufferFrames, ERROR_TRANSMIT);
	if (!error.empty()) goto Exit;
	//qDebug() << "transmitCallbackMode=" << transmitCallbackMode << "transmitBufferFrames=" << transmitBufferFrames;

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
	if (transmitCallbackMode) {
		hr = transmitAudioClient->SetEventHandle(transmitEvent);
		EXIT_ON_HR_ERROR(ERROR_TRANSMIT + "IAudioClient::SetEventHandle");
	}

	hr = receiveAudioClient->GetService(
		IID_IAudioCaptureClient,
		(void**)&receiveCaptureClient);
	EXIT_ON_HR_ERROR(ERROR_RECEIVE + "IAudioClient::GetService(IAudioCaptureClient)");

	hr = transmitAudioClient->GetService(
		IID_IAudioRenderClient,
		(void**)&transmitRenderClient);
	EXIT_ON_HR_ERROR(ERROR_TRANSMIT + "IAudioClient::GetService(IAudioRenderClient)");

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

#if 0
void Audio::run()
{
	double	aLocalCarrierAmp[NUM_CARRIER];
	double	aLocalCarrierPhaseInc[NUM_CARRIER];
	double	aLocalCarrierPhase[NUM_CARRIER];
	unsigned LocalSampleRate;

	DWORD lTicks = GetTickCount();
	DWORD lTicksNew = 0;
	DWORD lTicksPPM = lTicks;
	unsigned long generatorCount = 0;

	{
		for (int k = 0; k < NUM_CARRIER; ++k)
			aLocalCarrierPhase[k] = 0.0;

		LocalSampleRate = gExtSampleRate;
	}

	while (!m_exit_thread)
	{
		SleepEx(1, FALSE);
		if (gbExitThread)
			break;

		// new parametrization?
		if (giParameterUsed != giParameterSetNo)
		{
			++giParameterUsed;
			setAmpVal();
			for (int k = 0; k < NUM_CARRIER; ++k)
			{
				aLocalCarrierAmp[k] = gaCarrierAmp[k];
				aLocalCarrierPhaseInc[k] = gaCarrierPhaseInc[k];
			}
			LocalSampleRate = gExtSampleRate;
		}

		lTicksNew = GetTickCount();
		unsigned long elapsedMs = (unsigned long)(lTicksNew - lTicks);
		generatorCount += (LocalSampleRate * elapsedMs + 500L) / 1000L;

		while (generatorCount >= EXT_BLOCKLEN && !gbExitThread)
		{
			static int round = 0;
			bool mute = false;
			static bool muted = false;
			if (++round > 100) {
				if (round < 200)
					mute = true;
				// generatorCount = 0;
				else
					round = 0;
			}

			float samplesFlt[EXT_BLOCKLEN * 2];
			memset(&samplesFlt[0], 0, EXT_BLOCKLEN * 2 * sizeof(float));

			for (int k = 0; k < NUM_CARRIER; ++k)
			{
				if (aLocalCarrierAmp[k] <= 0.0)
					continue;

				const double ampFactor = aLocalCarrierAmp[k];
				const double phaseInc = aLocalCarrierPhaseInc[k];
				double phase = aLocalCarrierPhase[k];

				unsigned i, j = 0;
				for (i = j = 0; i < EXT_BLOCKLEN; ++i)
				{
					samplesFlt[j++] += (float)(ampFactor * cos(phase));
					samplesFlt[j++] += (float)(ampFactor * sin(phase));

					phase += phaseInc;
					if (phase > 3.1415926535897932384626433832795)	phase -= 2.0 * 3.1415926535897932384626433832795;
					else if (phase < -3.1415926535897932384626433832795)	phase += 2.0 * 3.1415926535897932384626433832795;
				}

				aLocalCarrierPhase[k] = phase;
			}

			static const float noise_amp = 0.05f;
			for (unsigned i = 0; i < EXT_BLOCKLEN; ++i) {
				samplesFlt[2 * i] += noise_amp * rand() / RAND_MAX;
				samplesFlt[2 * i + 1] += noise_amp * rand() / RAND_MAX;
			}

			if (muted) {
				if (mute)
					memset(&samplesFlt[0], 0, EXT_BLOCKLEN * 2 * sizeof(float));
				else {
					// Unmute
					for (unsigned i = 0; i < EXT_BLOCKLEN; ++i) {
						float factor = 0.5f - 0.5f * cos(3.1415926535897932384626433832795*(i + 0.5) / EXT_BLOCKLEN);
						samplesFlt[2 * i] *= factor;
						samplesFlt[2 * i + 1] *= factor;
					}
					muted = false;
				}
			}
			else if (mute) {
				// Mute
				muted = true;
				for (unsigned i = 0; i < EXT_BLOCKLEN; ++i) {
					float factor = 0.5f + 0.5f * cos(3.1415926535897932384626433832795*(i + 0.5) / EXT_BLOCKLEN);
					samplesFlt[2 * i] *= factor;
					samplesFlt[2 * i + 1] *= factor;
				}
			}

			generatorCount -= EXT_BLOCKLEN;

			if (pfnCallback && !gbExitThread)
			{
				if (gHwType == exthwUSBfloat32)
					pfnCallback(EXT_BLOCKLEN, 0, 0.0F, &samplesFlt[0]);
				else
				{
					int   samplePCM[EXT_BLOCKLEN * 2];
					if (gHwType == exthwFullPCM32)
					{
						for (int k = 0; k < 2 * EXT_BLOCKLEN; ++k)
							samplePCM[k] = (int)(samplesFlt[k] * 2147483648.0F);
					}
					else if (gHwType == exthwUSBdata32)
					{
						for (int k = 0; k < 2 * EXT_BLOCKLEN; ++k)
							samplePCM[k] = (int)(samplesFlt[k] * 8388608.0F);
					}
					else if (gHwType == exthwUSBdata24)
					{
						// error!
						for (int k = 0; k < 2 * EXT_BLOCKLEN; ++k)
							samplePCM[k] = (int)(samplesFlt[k] * 8388608.0F);
					}
					else if (gHwType == exthwUSBdataS8)
					{
						signed char * samplesS8 = (signed char *)((void*)(&samplePCM[0]));
						for (int k = 0; k < 2 * EXT_BLOCKLEN; ++k)
						{
							int pcm = (int)(samplesFlt[k] * 128.0F);
							samplesS8[k] = (pcm < -128) ? -128 : ((pcm > 127) ? 127 : pcm);
						}
					}
					else if (gHwType == exthwUSBdataU8)
					{
						unsigned char * samplesU8 = (unsigned char *)((void*)(&samplePCM[0]));
						for (int k = 0; k < 2 * EXT_BLOCKLEN; ++k)
						{
							int pcm = (int)(samplesFlt[k] * 128.0F) + 128;
							samplesU8[k] = (pcm < 0) ? 0 : ((pcm > 255) ? 255 : pcm);
						}
					}
					pfnCallback(EXT_BLOCKLEN, 0, 0.0F, &samplePCM[0]);
				}
			}
		}

		lTicks = lTicksNew;
	}
	gbExitThread = false;
	gbThreadRunning = false;
	return 0;
}
#else
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

	m_receive_buffer.assign(EXT_BLOCKLEN * 2, 0.f);
	m_receive_buffer_cnt = 0;

	// Prime buffers before calling start
	// Necessary for some drivers to know callback timing
	doTransmit(this->transmitBufferFrames);

	hr = this->receiveAudioClient->Start();
	if (FAILED(hr)) fatal_exit("receiveAudioClient->Start");
	hr = this->transmitAudioClient->Start();
	if (FAILED(hr)) fatal_exit("transmitAudioClient->Start");

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
		if (!this->transmitCallbackMode) {
			hr = this->transmitAudioClient->GetCurrentPadding(&padding);
			if (SUCCEEDED(hr))
				doTransmit(this->transmitBufferFrames - padding);
		}
		doReceive();
	}

	hr = this->receiveAudioClient->Stop();
	if (FAILED(hr)) fatal_exit("receiveAudioClient->Stop");
	hr = this->transmitAudioClient->Stop();
	if (FAILED(hr)) fatal_exit("transmitAudioClient->Stop");

	timeEndPeriod(dwMilliseconds);
	if (mmtc) AvRevertMmThreadCharacteristics(mmtc);
}
#endif

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
		while (numFramesToRead > 0 && m_receive_buffer_cnt < EXT_BLOCKLEN) {
			m_receive_buffer[m_receive_buffer_cnt * 2] = float(*buf++) / 32768.f;
			m_receive_buffer[m_receive_buffer_cnt * 2 + 1] = float(*buf++) / 32768.f;
			++m_receive_buffer_cnt;
			--numFramesToRead;
		}
		if (m_receive_buffer_cnt == EXT_BLOCKLEN) {
			pfnCallback(EXT_BLOCKLEN, 0, 0.0f, m_receive_buffer.data());
			m_receive_buffer_cnt = 0;
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
