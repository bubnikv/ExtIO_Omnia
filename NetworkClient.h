#ifndef NetworkClient_H
#define NetworkClient_H

#include <string>
#include <vector>
#include <cstdint>
#include "Config.h"

struct _ENetPacket;

class NetworkClient {
public:
	NetworkClient() {}
	~NetworkClient() {}
	bool init();
	void destroy();

	void start();
	void stop();

	const std::string get_error() const { return error; }
	std::string error;

	// Set local oscillator frequency in Hz.
	bool set_freq(int64_t frequency);
	// Set the CW TX frequency in Hz.
	bool set_cw_tx_freq(int64_t frequency);
	// Set the CW keyer speed in Words per Minute.
	// Limited to <5, 45>
	bool set_cw_keyer_speed(int wpm);
	bool set_cw_keyer_mode(KeyerMode mode);
	// Delay of the dit sent after dit played, to avoid hot switching of the AMP relay, in microseconds. Maximum time is 15ms.
	// Relay hang after the last dit, in microseconds. Maximum time is 10 seconds.
	bool set_amp_control(bool enabled, int delay, int hang);

	bool setIQBalanceAndPower(double phase_balance_deg, double amplitude_balance, double power);

private:
	void					run();
	static DWORD WINAPI		thread_function(LPVOID lpParam);
	void					send_packet(char* buf, int buflen);
	void					send_packet(_ENetPacket *packet);

	// Current audio thread handle.
	HANDLE					 m_hThread		= nullptr;
	CRITICAL_SECTION		 m_mutex;
	std::vector<_ENetPacket*> m_queue;
	bool					 m_connected	= false;
	// Flag to indicate the audio thread to stop.
	volatile bool			 m_exit_thread	= false;
};

extern NetworkClient g_network_client;

#endif // NetworkClient_H
