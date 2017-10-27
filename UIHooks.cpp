#include "Windows.h"
#include "assert.h"
#include "stdlib.h"
#include "stdio.h"

#include <algorithm>

#include "UIHooks.h"

static UIHooks *g_uihooks = nullptr;

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
		this->hwndSecondaryWaterfall == nullptr || this->hwndSecondaryWaterfallScale == nullptr) {
		::MessageBoxA(nullptr, "Failed to discover HDSDR controls", "ExtIO Omnia", 0);
		return false;
	}
	this->sort_lower_right_windows();

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
	DestroyWindow(this->hwndMyPanel);
	this->hwndMyPanel = nullptr;
	this->hwndButton1 = nullptr;
	this->hwndButton2 = nullptr;
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
	if (g_uihooks->m_layout_invalid) {
		g_uihooks->m_layout_invalid = false;
		g_uihooks->update_layout();
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK UIHooks::MyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (hwnd != g_uihooks->hwndMyPanel)
		return DefWindowProc(hwnd, msg, wParam, lParam);
	switch (msg)
	{
	case WM_CLOSE:
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
		}
		break;
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
	wc.lpfnWndProc = UIHooks::MyWndProc;
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

	// 3) Populate the panel with some buttons.
	this->hwndButton1 = ::CreateWindow(
		L"BUTTON",  // Predefined class; Unicode assumed 
		L"MsgBox",  // Button text 
		WS_VISIBLE | WS_CHILD,  // Styles 
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
		WS_VISIBLE | WS_CHILD,  // Styles 
		10,         // x position 
		35,         // y position 
		100,        // Button width
		20,         // Button height
		this->hwndMyPanel, // Parent window
		nullptr,    // No menu.
		wc.hInstance,
		nullptr);   // Pointer not needed.

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
		DWORD height = (clientRect.bottom - rectLeft.top) / 2;
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
		::SetWindowPos(this->hwndMyPanel, HWND_TOP, rectLeft.right, rectLeft.top + height, width, height - 1, SWP_SHOWWINDOW);
		g_uihooks->m_layout_invalid = false;
	}
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