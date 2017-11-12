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

#ifndef AUDIO_H
#define AUDIO_H

#include <array>
#include <string>
#include <vector>

#include <windows.h>
#include <AudioClient.h>
#include <mmdeviceapi.h>

#include "Config.h"

extern const IID IID_IAudioClient;
extern const IID IID_IAudioRenderClient;

class Audio
{
	friend class AudioWorkerThread;
public:
	explicit Audio() : m_hThread(nullptr), m_exit_thread(false) {}
	~Audio() { destroy(); }

	void init();
	void destroy();

	const std::string get_error() const { return error;  }
	void start();
	void stop();

private:
	virtual int mutePaddingFrames();
	virtual double transmitPaddingSecs();

	void findPeaberry(IMMDeviceEnumerator *pEnumerator,
		bool isTransmit,
		IMMDevice **device,
		IAudioClient **audioClient,
		WAVEFORMATEXTENSIBLE *wavex,
		const std::string &errstr);
	void setPeaberryVolume(IMMDevice *device,
		const std::string &errstr);
	void initializeAudioClient(IMMDevice *device,
		WAVEFORMATEX* wfx,
		IAudioClient **audioClient,
		bool *callbackMode,
		UINT32 *bufferSize,
		const std::string &errstr);

private:
	void					run();
	static DWORD WINAPI		thread_function(LPVOID lpParam);
	void					doReceive();
	void					doTransmit(int frames);

	std::string			     error;

	IMMDevice				*receiveDevice = nullptr;
	IAudioClient			*receiveAudioClient   = nullptr;
	IAudioCaptureClient		*receiveCaptureClient = nullptr;
	HANDLE					 receiveEvent		  = nullptr;
	UINT32					 receiveBufferFrames  = 0;
	bool					 receiveCallbackMode  = false;

	IMMDevice				*transmitDevice		  = nullptr;
	IAudioClient			*transmitAudioClient  = nullptr;
	IAudioRenderClient		*transmitRenderClient = nullptr;
	HANDLE					 transmitEvent		  = nullptr;
	UINT32					 transmitBufferFrames = 0;
	bool					 transmitCallbackMode = false;

	// Current audio thread handle.
	HANDLE					 m_hThread;
	// Flag to indicate the audio thread to stop.
	volatile bool			 m_exit_thread;

	std::vector<float>		 m_receive_buffer;
	std::vector<float>		 m_receive_buffer_prev;
	size_t					 m_receive_buffer_cnt;

	bool					 m_muted			  = false;
	size_t					 m_mute_zeros		  = 0;
	size_t					 m_unmute_cntr        = 0;
	std::vector<float>		 m_unmute_envelope;
};

#endif // AUDIO_H
