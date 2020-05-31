#include <enet/enet.h>

#include "Config.h"
#include "NetworkClient.h"
#include "LC_ExtIO_Types.h"

#include <assert.h>

NetworkClient g_network_client;

bool NetworkClient::init()
{
    if (enet_initialize() != 0) {
        ::MessageBoxA(nullptr, "An error occured while initializing ENet.", "ExtIO_Omnia error", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void NetworkClient::destroy()
{
    enet_deinitialize();
}

void NetworkClient::start()
{
    if (m_hThread != nullptr)
        return;

    m_exit_thread = false;
    ::InitializeCriticalSection(&m_mutex);
    m_hThread = ::CreateThread(nullptr, 0, &NetworkClient::thread_function, (void*)this, CREATE_SUSPENDED, nullptr);
    ::SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);
    ::ResumeThread(m_hThread);
}

void NetworkClient::stop()
{
    if (m_hThread == nullptr)
        return;
    m_exit_thread = true;
    WaitForMultipleObjects(1, &m_hThread, TRUE, INFINITE);
    CloseHandle(m_hThread);
    ::DeleteCriticalSection(&m_mutex);
    // Destroy packets, which were not sent to the server.
    for (ENetPacket *packet : m_queue)
        enet_packet_destroy(packet);
    m_queue.clear();
    m_hThread = nullptr;
}

// Set local oscillator frequency in Hz.
bool NetworkClient::set_freq(int64_t frequency)
{
    return true;
}

// Set the CW TX frequency in Hz.
bool NetworkClient::set_cw_tx_freq(int64_t frequency)
{
    return true;
}

// Set the CW keyer speed in Words per Minute.
// Limited to <5, 45>
bool NetworkClient::set_cw_keyer_speed(int wpm)
{
    return true;
}

bool NetworkClient::set_cw_keyer_mode(KeyerMode mode)
{
    return true;
}

// Delay of the dit sent after dit played, to avoid hot switching of the AMP relay, in microseconds. Maximum time is 15ms.
// Relay hang after the last dit, in microseconds. Maximum time is 10 seconds.
bool NetworkClient::set_amp_control(bool enabled, int delay, int hang)
{
    return true;
}

bool NetworkClient::setIQBalanceAndPower(double phase_balance_deg, double amplitude_balance, double power)
{
    return true;
}

DWORD WINAPI NetworkClient::thread_function(LPVOID lpParam)
{
    NetworkClient *pthis = (NetworkClient*)lpParam;
    pthis->run();
    return 0;
}

extern pfnExtIOCallback pfnCallback;

void NetworkClient::run()
{
    ENetHost *client = enet_host_create(nullptr, 1, 2, 57600 / 8, 14400 / 8);
    if (client == nullptr) {
        fprintf(stderr, "An error occured while trying to create an ENet server host\n");
        exit(EXIT_FAILURE);
    }

    ENetAddress address;
    enet_address_set_host(&address, g_config.network_server_name.c_str());
    address.port = g_config.network_server_port;

    // Connect and user service
    ENetPeer *peer = enet_host_connect(client, &address, 2, 0);
    if (peer == NULL) {
        fprintf(stderr, "No available peers for initializing an ENet connection");
        exit(EXIT_FAILURE);
    }

    while (! m_exit_thread) {
        // Pump packets prepared by the UI thread to enet.
        ::EnterCriticalSection(&m_mutex);
        for (ENetPacket* packet : m_queue)
            enet_peer_send(peer, 0, packet);
        m_queue.clear();
        ::LeaveCriticalSection(&m_mutex);

        // Block maximum 50 miliseconds.
        ENetEvent event;
        int eventStatus = enet_host_service(client, &event, 50);
        // If we had some event that interested us
        if (eventStatus > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                printf("(Client) We got a new connection from %x\n", event.peer->address.host);
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                if (event.channelID == 0) {
                    // Audio channel packet. Pass it to HDSDR audio callback.
                    assert(event.packet->dataLength == EXT_BLOCKLEN * 2 * 4);
                    if (event.packet->dataLength == EXT_BLOCKLEN * 2 * 4)
                        pfnCallback(EXT_BLOCKLEN, 0, 0.0f, event.packet->data);
                }
                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
//                printf("(Client) %s disconnected.\n", event.peer->data);
                // Reset client's information
//                event.peer->data = NULL;
                enet_host_connect(client, &address, 2, 0);
                if (peer == NULL) {
                    fprintf(stderr, "No available peers for initializing an ENet connection");
                    exit(EXIT_FAILURE);
                }
                break;
            }
        }
    }
}

/*
void NetworkClient::send_packet()
{
    ENetPacket* packet = enet_packet_create(message, strlen(message) + 1, ENET_PACKET_FLAG_RELIABLE);
}
*/

void NetworkClient::send_packet(ENetPacket* packet)
{
    ::EnterCriticalSection(&m_mutex);
    m_queue.emplace_back(packet);
    ::LeaveCriticalSection(&m_mutex);
}
