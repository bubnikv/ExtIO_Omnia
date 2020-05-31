// OmniaNetServer.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "OmniaNetServer.h"

#include <enet/enet.h>

#include "../LC_ExtIO_Types.h"
#include "../Audio.h"
#include "../Cat.h"
#include <stdio.h>
#include <assert.h>
#include <string>

int enet_server_main();

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_OMNIANETSERVER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_OMNIANETSERVER));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OMNIANETSERVER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_OMNIANETSERVER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                enet_server_main();
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static constexpr size_t max_clients = 32;
static constexpr size_t max_channels = 2;

struct Client
{
    std::string name;
};

static Client       g_clients[max_clients];

static Audio        g_audio;
static Cat          g_cat;
static ENetAddress  g_address;
static ENetHost*    g_server = nullptr;

extern "C" {
    int receive_callback(int cnt, int status, float IQoffs, void* IQdata)
    {
        // 1) Push audio data to the clients.
        assert(cnt == -1 || cnt == 0 || cnt == EXT_BLOCKLEN);
        if (cnt == EXT_BLOCKLEN) {
            // Send a big 
            enet_host_broadcast(g_server, 0, enet_packet_create(IQdata, cnt * 2 * 4, 0));
        }
        // 2) Pump the UDP packets.
        for (;;) {
            ENetEvent event;
            int eventStatus = enet_host_service(g_server, &event, 0);
            if (eventStatus <= 0)
                break;
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                event.peer->data = &g_clients[event.peer->connectID];
                {
                    char buf[2048];
                    if (! enet_address_get_host(&event.peer->address, buf, 2048)) {
                        auto ip = event.peer->address.host;
                        sprintf(buf, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
                    }
                    static_cast<Client*>(event.peer->data)->name = std::string(buf) + ":" + std::to_string(event.peer->address.port);
                }
                printf("(Server) We got a new connection from %x\n", event.peer->address.host);
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                printf("(Server) Message from client : %s\n", event.packet->data);
                // Lets broadcast this message to all
                // enet_host_broadcast(server, 0, event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                printf("%s disconnected.\n", static_cast<const Client*>(event.peer->data)->name.c_str());
                // Reset client's information
                event.peer->data = nullptr;
                break;
            }
        }

        return 0;
    }
}

// HDSDR callback function.
extern pfnExtIOCallback	pfnCallback = receive_callback;

int enet_server_main()
{
    g_audio.init();
    if (! g_audio.get_error().empty()) {
        ::MessageBoxA(nullptr, g_audio.get_error().c_str(), "OmniaNetServer error", MB_OK | MB_ICONERROR);
        return 0;
    }
    g_Cat.init();
    if (! g_Cat.get_error().empty()) {
        ::MessageBoxA(nullptr, g_Cat.get_error().c_str(), "OmniaNetServer error", MB_OK | MB_ICONERROR);
        return 0;
    }

    // a. Initialize enet
    if (enet_initialize() != 0) {
        fprintf(stderr, "An error occured while initializing ENet.\n");
        return EXIT_FAILURE;
    }

//    atexit(enet_deinitialize);

    // b. Create a host using enet_host_create
    g_address.host = ENET_HOST_ANY;
    g_address.port = 1234;

    g_server = enet_host_create(&g_address, max_clients, max_channels, 0, 0);

    if (g_server == NULL) {
        fprintf(stderr, "An error occured while trying to create an ENet server host\n");
        exit(EXIT_FAILURE);
    }

    g_audio.start();
    return 0;
}
