#include "Windows.h"
#include "CommCtrl.h"
#include "assert.h"
#include "stdlib.h"
#include "stdio.h"

#include <algorithm>

#include "cat.h"
#include "Config.h"
#include "UIHooks.h"
#include "resource.h"

#define ID_UIHOOKS_FIRST	(1001010)
#define ID_CWKEYER_TRACKBAR (ID_UIHOOKS_FIRST + 0)
#define ID_CWKEYER_TEXT		(ID_UIHOOKS_FIRST + 1)
#define ID_CWKEYER_MODE		(ID_UIHOOKS_FIRST + 2)
#define ID_AMP_ENABLE       (ID_UIHOOKS_FIRST + 3)
#define ID_BUTTON_RIT		(ID_UIHOOKS_FIRST + 4)
#define ID_BUTTON_XIT		(ID_UIHOOKS_FIRST + 5)

static wchar_t *keyer_mode_names[] = { L"Straight Key", L"Iambic A", L"Iambic B" };

static UIHooks *g_uihooks = nullptr;
extern HINSTANCE g_hInstance;

bool UIHooks::initialize()
{
	this->process_id = GetCurrentProcessId();
	// Find the main HDSDR window, fill this->hwndMainFrame.
	::EnumWindows(&UIHooks::EnumTopLevelWindowsProc, (LPARAM)this);
	if (this->hwndMainFrame == nullptr)
		return false;
	// Find the important child windows of the HDSDR main frame.
	::EnumChildWindows(this->hwndMainFrame, &UIHooks::EnumPanelsWindowsProc, (LPARAM)this);
	if (this->hwndMainFrame == nullptr || this->hwndUpper == nullptr || this->hwndUpperScale == nullptr ||
		this->hwndLower == nullptr || this->hwndLowerLeft == nullptr || this->hwndLowerRight == nullptr ||
		this->hwndPrimaryWaterfallControls == nullptr || this->hwndSecondaryWaterfallControls == nullptr || 
		this->hwndSecondaryWaterfall == nullptr || this->hwndSecondaryWaterfallScale == nullptr ||
		this->hwndLowerLeftRecordingButtons == nullptr || this->hwndLowerLeftRadioControlButtons == nullptr) {
		::MessageBoxA(nullptr, "Failed to discover HDSDR controls", "ExtIO Omnia", 0);
		return false;
	}
	this->sort_lower_right_windows();

#if 0
	int x = 135 + 266 + 20;
	int y = 157;
	int w = 56;
	int h = 21;
	HINSTANCE hInstance = (HINSTANCE)GetWindowLong(this->hwndLowerLeft, GWL_HINSTANCE);
	this->hwndButtonRit = CreateWindowExA(0, "BUTTON", "RIT", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
		x, y, w, h, this->hwndLowerLeft, (HMENU)(ID_BUTTON_RIT), hInstance, nullptr);
	y += h + 6;
	this->hwndButtonXit = CreateWindowExA(0, "BUTTON", "XIT", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
		x, y, w, h, this->hwndLowerLeft, (HMENU)(ID_BUTTON_XIT), hInstance, nullptr);
#endif

	this->hookBefore = ::SetWindowsHookEx(WH_CALLWNDPROC,    &UIHooks::HookProcBefore, nullptr, this->thread_id);
	this->hookAfter  = ::SetWindowsHookEx(WH_CALLWNDPROCRET, &UIHooks::HookProcAfter,  nullptr, this->thread_id);
	this->hookIdle   = ::SetWindowsHookEx(WH_FOREGROUNDIDLE, &UIHooks::HookProcIdle,   nullptr, this->thread_id);
	g_uihooks = this;
	g_uihooks->m_layout_invalid = true;
	return true;
}

void UIHooks::destroy()
{
	this->unhook_callbacks();
	g_uihooks = nullptr;
	DestroyWindow(this->hwndMyPanel);
	DestroyWindow(this->hwndMyRitXitPanel);
	this->hwndMyPanel = nullptr;
	this->hwndButton1 = nullptr;
	this->hwndButton2 = nullptr;
}

void UIHooks::show_my_panel(bool show)
{
	this->my_panel_shown = show;
	::ShowWindow(this->hwndMyPanel, show ? SW_SHOW : SW_HIDE);
	m_layout_invalid = true;
}

void UIHooks::show_secondary_waterfall(bool show)
{
	this->secondary_waterfall_show = show;
	::ShowWindow(this->hwndLowerRight, show ? SW_SHOW : SW_HIDE);
	m_layout_invalid = true;
}

void UIHooks::show_waterfall_controls(bool show)
{
	this->waterfall_controls_shown = show;
//	::ShowWindow(this->hwndPrimaryWaterfallControls, show ? SW_SHOW : SW_HIDE);
	::ShowWindow(this->hwndSecondaryWaterfallControls, show ? SW_SHOW : SW_HIDE);
	m_layout_invalid = true;
}

void UIHooks::show_recording_panel(bool show)
{
	::ShowWindow(this->hwndLowerLeftRecordingButtons, show ? SW_SHOW : SW_HIDE);
}

void UIHooks::show_radio_control(bool show)
{
	::ShowWindow(this->hwndLowerLeftRadioControlButtons, show ? SW_SHOW : SW_HIDE);
}

void UIHooks::set_keyer_speed(unsigned int speed)
{
	wchar_t buf[64];
	wsprintf(buf, L"%d WPM", speed);
	::SetWindowText(this->hwndKeyerSpeedText, buf);
	::SendMessage(this->hwndKeyerSpeedTrackBar, TBM_SETPOS, (WPARAM)TRUE, speed);
	g_config.keyer_wpm = speed;
	g_Cat.set_cw_keyer_speed(speed);
}

BOOL CALLBACK UIHooks::EnumTopLevelWindowsProc(HWND hwnd, LPARAM lparam)
{
	UIHooks *pThis		= (UIHooks*)lparam;
	DWORD	 process_id = 0;
	DWORD	 thread_id  = ::GetWindowThreadProcessId(hwnd, &process_id);
	if (process_id == pThis->process_id) {
		char window_text[2048];
		char class_name[2048];
		GetWindowTextA(hwnd, window_text, 2048);
		GetClassNameA(hwnd, class_name, 2048);
		if (strcmp(class_name, "TMainForm") == 0 && strncmp(window_text, "HDSDR", 5) == 0) {
			pThis->hwndMainFrame = hwnd;
			pThis->thread_id = thread_id;
			// Stop the further window enumeration.
			return false;
		}
	}
	// Continue enumerating the other windows.
	return true;
}

BOOL CALLBACK UIHooks::EnumPanelsWindowsProc(HWND hwnd, LPARAM lparam)
{
	UIHooks *pThis = (UIHooks*)lparam;
	HWND parent = GetParent(hwnd);
	char class_name[2048];
	GetClassNameA(hwnd, class_name, 2048);
	HWND *hwndsLowerRight[] = { &pThis->hwndPrimaryWaterfallControls, &pThis->hwndSecondaryWaterfall, &pThis->hwndSecondaryWaterfallControls };
	if (strcmp(class_name, "TPanel") == 0) {
		char window_text[2048];
		GetWindowTextA(hwnd, window_text, 2048);
		if (strcmp(window_text, "Pnl_Bottom_Left") == 0) {
			// Erase the "Pnl_Bottom_Left" as it could show up on the main window when the lower right window is made shorter.
			::SetWindowTextA(hwnd, "");
			pThis->hwndLowerRight = hwnd;
			pThis->hwndLower = parent;
		} else if (pThis->hwndLower == parent) {
			pThis->hwndLowerLeft = hwnd;
		} else if (pThis->hwndLowerLeft == parent) {
			RECT rect;
			::GetWindowRect(hwnd, &rect);
			int w = rect.right - rect.left;
			int h = rect.bottom - rect.top;
			::ScreenToClient(::GetParent(hwnd), (LPPOINT)&rect);
			if (rect.left == 135 && rect.top == 157 && w == 266 && h == 23 && pThis->hwndLowerLeftRecordingButtons == 0)
				pThis->hwndLowerLeftRecordingButtons = hwnd;
			else if (rect.left == 132 && rect.top == 235 && w == 309 && h == 78 && pThis->hwndLowerLeftRadioControlButtons == 0)
				pThis->hwndLowerLeftRadioControlButtons = hwnd;
		} else if (pThis->hwndLowerRight == parent) {
			for (int i = 0; i < 3; ++ i)
				if (*hwndsLowerRight[i] == nullptr) {
					*hwndsLowerRight[i] = hwnd;
					break;
				}
		}
	} else if (strcmp(class_name, "AdBHScale") == 0) {
		HWND parent2 = GetParent(parent);
		if (parent2 == pThis->hwndMainFrame) {
			pThis->hwndUpperScale = hwnd;
			pThis->hwndUpper = parent;
		} else if (parent2 == pThis->hwndLowerRight) {
			int i = 0;
			for (; i < 3 && *hwndsLowerRight[i] != parent; ++i);
			if (i == 0 || i == 2)
				std::swap(*hwndsLowerRight[i], pThis->hwndSecondaryWaterfall);
			pThis->hwndSecondaryWaterfall = parent;
			pThis->hwndSecondaryWaterfallScale = hwnd;
		}
	}
 // Continue enumerating the other windows.
	return true;
}

LRESULT CALLBACK UIHooks::HookProcBefore(int nCode, WPARAM wParam, LPARAM lParam)
{
	LPCWPSTRUCT cwp = (LPCWPSTRUCT)lParam;
	if (cwp->hwnd != nullptr && cwp->hwnd == g_uihooks->hwndMyPanel) {
		printf("Hu");
	}
	if (cwp->message == WM_SIZE) {
		if (cwp->hwnd == g_uihooks->hwndMainFrame ||
			cwp->hwnd == g_uihooks->hwndLower ||
			cwp->hwnd == g_uihooks->hwndLowerLeft ||
			cwp->hwnd == g_uihooks->hwndLowerRight ||
			cwp->hwnd == g_uihooks->hwndUpper)
			// Update layout when idle.
			g_uihooks->m_layout_invalid = true;
	} else if (cwp->message == WM_SIZING) {
	} else if (cwp->message == WM_COMMAND) {
		if ((HWND)cwp->lParam == g_uihooks->hwndButtonRit)
			g_uihooks->splitMode = (g_uihooks->splitMode == SPLIT_RIT) ? SPLIT_OFF : SPLIT_RIT;
		else if ((HWND)cwp->lParam == g_uihooks->hwndButtonXit)
			g_uihooks->splitMode = (g_uihooks->splitMode == SPLIT_XIT) ? SPLIT_OFF : SPLIT_XIT;
		::InvalidateRect(g_uihooks->hwndButtonRit, nullptr, false);
		::InvalidateRect(g_uihooks->hwndButtonXit, nullptr, false);
		g_uihooks->show_recording_panel(g_uihooks->splitMode == SPLIT_OFF);
		::ShowWindow(g_uihooks->hwndMyRitXitPanel, (g_uihooks->splitMode == SPLIT_OFF) ? SW_HIDE : SW_SHOW);
	} else if (cwp->message == WM_DRAWITEM && g_uihooks != nullptr) {
		if (cwp->wParam == ID_BUTTON_RIT || cwp->wParam == ID_BUTTON_XIT) {
			DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)cwp->lParam;
			HWND hwnd = dis->hwndItem;
			HDC  hdc = dis->hDC;
			SelectObject(hdc, GetStockObject(SYSTEM_FIXED_FONT));
			bool enabled = g_uihooks->splitMode == ((cwp->wParam == ID_BUTTON_RIT) ? SPLIT_RIT : SPLIT_XIT);
			COLORREF clrBg = enabled ? RGB(0x18, 0x0a0, 0x0f4) : RGB(0, 0x2c, 0x6b);
			SetTextColor(hdc, enabled ? RGB(0x0ff, 0x0ff, 0x0ff) : RGB(0x80, 0x80, 0x80));
			HBRUSH hBrush = CreateSolidBrush(clrBg);
			FillRect(hdc, &dis->rcItem, hBrush);
			DeleteObject(hBrush);
			SetBkColor(hdc, clrBg);
			DrawTextA(hdc, (cwp->wParam == ID_BUTTON_RIT) ? "RIT" : "XIT", -1, &dis->rcItem, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
			return TRUE;
		}
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK UIHooks::HookProcAfter(int nCode, WPARAM wParam, LPARAM lParam)
{
	LPCWPSTRUCT cwp = (LPCWPSTRUCT)lParam;
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK UIHooks::HookProcIdle(int nCode, WPARAM wParam, LPARAM lParam)
{
	LPCWPSTRUCT cwp = (LPCWPSTRUCT)lParam;
	if (g_uihooks != nullptr && g_uihooks->m_layout_invalid) {
		g_uihooks->m_layout_invalid = false;
		g_uihooks->update_layout();
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK UIHooks::MyPanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (g_uihooks == nullptr || g_uihooks->hwndMyPanel == nullptr || hwnd != g_uihooks->hwndMyPanel)
		return DefWindowProc(hwnd, msg, wParam, lParam);
	switch (msg)
	{
	case WM_CLOSE:
		g_uihooks->hwndMyPanel = nullptr;
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		break;
	case WM_ERASEBKGND:
	{
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect((HDC)wParam, &rc, (HBRUSH)::GetStockObject(BLACK_BRUSH));
		break;
	}
	case WM_COMMAND:
		if ((HWND)lParam == g_uihooks->hwndButton1)
			::MessageBoxA(g_uihooks->hwndMainFrame, "Button1 pressed", "Test", 0);
		else if ((HWND)lParam == g_uihooks->hwndButton2) {
//			::MessageBoxA(g_uihooks->hwndMainFrame, "Button2 pressed", "Test", 0);
			// Toggle visibility of the secondary waterfall controls.
			g_uihooks->show_waterfall_controls(!g_uihooks->waterfall_controls_shown);
		} else if ((HWND)lParam == g_uihooks->hwndKeyerMode) {
			if (HIWORD(wParam) == CBN_SELCHANGE) {
				DWORD mode = SendMessage(g_uihooks->hwndKeyerMode, CB_GETCURSEL, 0, 0);
				if (mode < 0)
					mode = 0;
				else if (mode >= 3)
					mode = 2;
				g_config.keyer_mode = (KeyerMode)mode;
				g_Cat.set_cw_keyer_mode((KeyerMode)mode);
			}
		} else if ((HWND)lParam == g_uihooks->hwndAmpButton) {
			g_config.amp_enabled = !g_config.amp_enabled; //  SendMessage(g_uihooks->hwndAmpButton, BM_GETCHECK, 0, 0) != 0;
			g_Cat.set_amp_control(g_config.amp_enabled, g_config.tx_delay, g_config.tx_hang);
		}
		break;
	case WM_HSCROLL:
		if (g_uihooks->hwndKeyerSpeedTrackBar == (HWND)lParam) {
			DWORD pos;
			switch (LOWORD(wParam)) {
			case TB_THUMBPOSITION:
			case TB_THUMBTRACK:
				pos = HIWORD(wParam);
				break;
			case TB_ENDTRACK:
				pos = SendMessage(g_uihooks->hwndKeyerSpeedTrackBar, TBM_GETPOS, 0, 0);
				if (pos > 45)
					SendMessage(g_uihooks->hwndKeyerSpeedTrackBar, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)45);
				else if (pos < 5)
					SendMessage(g_uihooks->hwndKeyerSpeedTrackBar, TBM_SETPOS, (WPARAM)TRUE, 5);
				break;
			default:
				pos = SendMessage(g_uihooks->hwndKeyerSpeedTrackBar, TBM_GETPOS, 0, 0);
				break;
			}
			g_uihooks->set_keyer_speed(pos);
		}
		break;
	case WM_CTLCOLORSTATIC:
	{
		if (g_uihooks->hwndKeyerSpeedTrackBar == (HWND)lParam || g_uihooks->hwndKeyerSpeedText == (HWND)lParam ||
			g_uihooks->hwndKeyerMode == (HWND)lParam) {
			HDC hdcStatic = (HDC)wParam;
			SetTextColor(hdcStatic, RGB(255, 255, 255));
			SetBkColor(hdcStatic, RGB(0, 0, 0));
			return (INT_PTR)GetStockObject(BLACK_BRUSH);
		}
		break;
	}
	case WM_DRAWITEM:
	{
		if (wParam == ID_AMP_ENABLE) {
			DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lParam;
			HWND hwnd = dis->hwndItem;
			HDC  hdc  = dis->hDC;
			SelectObject(hdc, GetStockObject(SYSTEM_FIXED_FONT));
			COLORREF clrBg = g_config.amp_enabled ? RGB(0x18, 0x0a0, 0x0f4) : RGB(0, 0x2c, 0x6b);
			SetTextColor(hdc, g_config.amp_enabled ? RGB(0x0ff, 0x0ff, 0x0ff) : RGB(0x80, 0x80, 0x80));
			HBRUSH hBrush = CreateSolidBrush(clrBg);
			FillRect(hdc, &dis->rcItem, hBrush);
			DeleteObject(hBrush);
			SetBkColor(hdc, clrBg);
			char buf[32];
			DrawTextA(hdc, "AMP", -1, &dis->rcItem, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
			return TRUE;
		}
	}
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

const wchar_t g_szClassName[] = L"ExtIO_Omnia Panel";

bool UIHooks::create_my_panel()
{
	// 1) Register the Window Class.
	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = UIHooks::MyPanelWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = (HINSTANCE)GetWindowLong(this->hwndLower, GWL_HINSTANCE);
	wc.hIcon = 0;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = g_szClassName;
	wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

	if (!RegisterClassEx(&wc)) {
		MessageBoxA(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	// 2) Create the panel.
	this->hwndMyPanel = ::CreateWindowEx(0, g_szClassName, L"Omnia",
		WS_CHILD,
		0, 0, 100, 100, this->hwndLower, nullptr, wc.hInstance, nullptr);
	if (this->hwndMyPanel == nullptr) {
		MessageBoxA(nullptr, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

#if 0
	if (! this->create_ritxit_panel())
		return false;
#endif

	// 3) Populate the panel with some buttons.
	this->hwndButton1 = ::CreateWindow(
		L"BUTTON",  // Predefined class; Unicode assumed 
		L"MsgBox",  // Button text 
		/* WS_VISIBLE | */ WS_CHILD,  // Styles 
		10,         // x position 
		10,         // y position 
		100,        // Button width
		20,         // Button height
		this->hwndMyPanel, // Parent window
		nullptr,    // No menu.
		wc.hInstance,
		nullptr);   // Pointer not needed.
	this->hwndButton2 = ::CreateWindow(
		L"BUTTON",  // Predefined class; Unicode assumed 
		L"Hide",    // Button text 
		/* WS_VISIBLE | */ WS_CHILD,  // Styles 
		10,         // x position 
		35,         // y position 
		100,        // Button width
		20,         // Button height
		this->hwndMyPanel, // Parent window
		nullptr,    // No menu.
		wc.hInstance,
		nullptr);   // Pointer not needed.
	int x = 10;
	int w = 200;
	this->hwndKeyerSpeedTrackBar = CreateWindowEx(0, TRACKBAR_CLASS, L"Trackbar",
		WS_CHILD | WS_VISIBLE /* | TBS_AUTOTICKS | TBS_ENABLESELRANGE */,
		x, 10, w, 40, this->hwndMyPanel, (HMENU)ID_CWKEYER_TRACKBAR, wc.hInstance, nullptr);
	x += w + 10;
	w = 100;
	this->hwndKeyerSpeedText = CreateWindow(L"STATIC", L"teststring", WS_CHILD | WS_VISIBLE,
		x, 10, w, 40, this->hwndMyPanel, (HMENU)(ID_CWKEYER_TEXT), wc.hInstance, nullptr);
	x += w + 10;
	w = 80;
	this->hwndKeyerMode = CreateWindow(WC_COMBOBOX, L"",
		CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
		x, 10, w, 40, this->hwndMyPanel, (HMENU)(ID_CWKEYER_MODE), wc.hInstance, nullptr);
	x += w + 10;
	w = 50;
	this->hwndAmpButton = CreateWindowExA(0, "BUTTON", "AMP", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
		x, 10, w, 24, this->hwndMyPanel, (HMENU)(ID_AMP_ENABLE), wc.hInstance, nullptr);
	SendMessage(this->hwndAmpButton, BM_SETCHECK, g_config.amp_enabled ? BST_CHECKED : BST_UNCHECKED, 0);

	SendMessage(this->hwndKeyerSpeedTrackBar, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(5, 45));
	SendMessage(this->hwndKeyerSpeedTrackBar, TBM_SETPAGESIZE, 0, (LPARAM)5);
	this->set_keyer_speed(g_config.keyer_wpm);

	for (int i = 0; i < 3; ++ i)
		::SendMessage(this->hwndKeyerMode, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)keyer_mode_names[i]);
	::SendMessage(this->hwndKeyerMode, CB_SETCURSEL, (WPARAM)g_config.keyer_mode, (LPARAM)0);

	this->update_layout();
	::ShowWindow(this->hwndMyPanel, SW_SHOW);
	::UpdateWindow(this->hwndMyPanel);
	return true;
}

void UIHooks::update_layout()
{
	if (this->hwndMyPanel == nullptr)
		create_my_panel();
	if (::IsWindowVisible(this->hwndLower) && ::IsWindowVisible(this->hwndLowerLeft)) {
		// Shrink the bottom right waterfall to leave space for my own buttons.
		RECT clientRect;
		::GetClientRect(this->hwndLower, &clientRect);
		RECT rectLeft;
		::GetWindowRect(this->hwndLowerLeft, &rectLeft);
		RECT rectRight;
		::GetWindowRect(this->hwndLowerLeft, &rectRight);
		::ScreenToClient(this->hwndLower, (LPPOINT)&rectLeft.left);
		::ScreenToClient(this->hwndLower, (LPPOINT)&rectLeft.right);
		::ScreenToClient(this->hwndLower, (LPPOINT)&rectRight.left);
		::ScreenToClient(this->hwndLower, (LPPOINT)&rectRight.right);
		DWORD width  = clientRect.right - rectLeft.right - 1;
		DWORD height = clientRect.bottom - rectLeft.top;
		height -= (this->my_panel_shown ? 50 : 1);
//		::MoveWindow(this->hwndLowerRight, rectLeft.right, waterfall_controls_shown ? rectLeft.top : (rectLeft.top - 100), width, waterfall_controls_shown ? height : (height + 200), TRUE);
		if (this->waterfall_controls_shown)
			::MoveWindow(this->hwndLowerRight, rectLeft.right, rectLeft.top, width, height, TRUE);
		else {
			RECT r;
			::GetWindowRect(this->hwndSecondaryWaterfallControls, &r);
			::ScreenToClient(this->hwndLower, (LPPOINT)&r.left);
			::ScreenToClient(this->hwndLower, (LPPOINT)&r.right);
			//			::MoveWindow(this->hwndLowerRight, rectLeft.right, rectLeft.top - (r.bottom - r.top), width, height, TRUE);
//			::MoveWindow(this->hwndLowerRight, rectLeft.right, rectLeft.top, width, height + (r.bottom - r.top), TRUE);
			::MoveWindow(this->hwndLowerRight, rectLeft.right, rectLeft.top, width, height + (r.bottom - r.top) - 5, TRUE);
		}
//		::MoveWindow(this->hwndSecondaryWaterfall, rectLeft.right, waterfall_controls_shown ? rectLeft.top : (rectLeft.top - 100), width, waterfall_controls_shown ? height : (height + 200), TRUE);
		::SetWindowPos(this->hwndMyPanel, my_panel_shown ? HWND_TOP : 0, rectLeft.right, rectLeft.top + height, width, height - 1, my_panel_shown ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
		g_uihooks->m_layout_invalid = false;
	}
}

LRESULT CALLBACK UIHooks::MyRitXitPanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (g_uihooks == nullptr || g_uihooks->hwndMyRitXitPanel == nullptr || hwnd != g_uihooks->hwndMyRitXitPanel)
		return DefWindowProc(hwnd, msg, wParam, lParam);
	switch (msg)
	{
	case WM_CLOSE:
		g_uihooks->hwndMyRitXitPanel = nullptr;
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);

		SelectObject(hdc, GetStockObject(SYSTEM_FIXED_FONT));
		bool enabled = true;
		COLORREF clrBg = enabled ? RGB(0x18, 0x0a0, 0x0f4) : RGB(0, 0x2c, 0x6b);
		SetTextColor(hdc, enabled ? RGB(0x0ff, 0x0ff, 0x0ff) : RGB(0x80, 0x80, 0x80));
		HBRUSH hBrush = CreateSolidBrush(clrBg);
		FillRect(hdc, &rc, hBrush);
		DeleteObject(hBrush);
		SetBkColor(hdc, clrBg);
		DrawTextA(hdc, "HUHUHU", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

		HDC		hdcMem = CreateCompatibleDC(hdc);
		HGDIOBJ oldBitmap = SelectObject(hdcMem, g_uihooks->hbmpDigts);
		BITMAP  bitmap;
		GetObject(g_uihooks->hbmpDigts, sizeof(bitmap), &bitmap);
		BitBlt(hdc, 50, 0, bitmap.bmWidth, bitmap.bmHeight, hdcMem, 0, 0, SRCCOPY);
		SelectObject(hdcMem, oldBitmap);
		DeleteDC(hdcMem);

		EndPaint(hwnd, &ps);
		return 0L;
	}
	case WM_ERASEBKGND:
		return TRUE;
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

const wchar_t g_szRitXitClassName[] = L"ExtIO_Omnia RitXitPanel";

bool UIHooks::create_ritxit_panel()
{
	// 1) Register the Window Class.
	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = UIHooks::MyRitXitPanelWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = (HINSTANCE)GetWindowLong(this->hwndLower, GWL_HINSTANCE);
	wc.hIcon = 0;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = g_szRitXitClassName;
	wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

	if (!RegisterClassEx(&wc)) {
		MessageBoxA(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	this->hbmpDigts = (HBITMAP)::LoadImage(g_hInstance, MAKEINTRESOURCE(IDB_FREQ_DIGITS), IMAGE_BITMAP, 0, 0, 0);
	if (this->hbmpDigts == nullptr) {
		MessageBoxA(nullptr, "Cannot load digits bitmap", "Error!", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	// 2) Create the panel.
	RECT rect;
	::GetWindowRect(this->hwndLowerLeftRecordingButtons, &rect);
	::ScreenToClient(this->hwndLowerLeft, (LPPOINT)&rect);
	::ScreenToClient(this->hwndLowerLeft, (LPPOINT)&rect.right);
	this->hwndMyRitXitPanel = ::CreateWindowEx(0, g_szRitXitClassName, L"Omnia",
		WS_CHILD,
		rect.left, rect.top, rect.right - rect.left, 60, this->hwndLowerLeft, nullptr, wc.hInstance, nullptr);
	if (this->hwndMyPanel == nullptr) {
		MessageBoxA(nullptr, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

//	::ShowWindow(this->hwndMyRitXitPanel, SW_SHOW);
//	::UpdateWindow(this->hwndMyRitXitPanel);
	return true;
}

void UIHooks::sort_lower_right_windows()
{
	assert(this->hwndLowerRight == ::GetParent(this->hwndPrimaryWaterfallControls));
	assert(this->hwndLowerRight == ::GetParent(this->hwndSecondaryWaterfall));
	assert(this->hwndLowerRight == ::GetParent(this->hwndSecondaryWaterfallControls));
	assert(this->hwndSecondaryWaterfall == ::GetParent(this->hwndSecondaryWaterfallScale));
	RECT rect1, rect2;
	::GetWindowRect(this->hwndPrimaryWaterfallControls, &rect1);
	::GetWindowRect(this->hwndSecondaryWaterfallControls, &rect2);
	if (rect1.top > rect2.top)
		std::swap(this->hwndPrimaryWaterfallControls, this->hwndSecondaryWaterfallControls);
}

void UIHooks::unhook_callbacks()
{
	// Deregister the callbacks.
	if (g_uihooks->hookBefore != nullptr)
		UnhookWindowsHookEx(g_uihooks->hookBefore);
	if (g_uihooks->hookAfter != nullptr)
		UnhookWindowsHookEx(g_uihooks->hookAfter);
	if (g_uihooks->hookIdle != nullptr)
		UnhookWindowsHookEx(g_uihooks->hookIdle);
	g_uihooks->hookBefore = nullptr;
	g_uihooks->hookAfter = nullptr;
	g_uihooks->hookIdle  = nullptr;
}
