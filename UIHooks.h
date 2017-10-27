#pragma once

#include "Windows.h"

class UIHooks
{
public:
	bool	initialize();
	void	destroy();

	void	show_secondary_waterfall(bool show);
	void	show_waterfall_controls(bool show);

	// TMainForm "HDSDR ..."
	HWND	hwndMainFrame = nullptr;
	// Main waterfall. TPanel ""
	HWND	hwndUpper = nullptr;
	// Main waterfall frequency scale.
	HWND	hwndUpperScale = nullptr;
	// Command box + secondary waterfall. TPanel ""
	HWND	hwndLower = nullptr;
	// Command box. TPanel ""
	HWND	hwndLowerLeft = nullptr;
	// Secondary waterfall with primary and secondary waterfall controls. TPanel "Pnl_Bottom_Left"
	HWND	hwndLowerRight = nullptr;
	// The bar above the secondary waterfall for controlling the main waterfall.
	HWND	hwndPrimaryWaterfallControls = nullptr;
	// The bar below the secondary waterfall for controlling the secondary waterfall.
	HWND	hwndSecondaryWaterfallControls = nullptr;
	// Secondary waterfall.
	HWND	hwndSecondaryWaterfall = nullptr;
	// Freqency scale of the secondary waterfall.
	HWND	hwndSecondaryWaterfallScale = nullptr;

	// Process ID of HDSDR.
	DWORD	process_id;
	// Main thread of HDSDR running the UI thread.
	DWORD	thread_id;

	// Hooks into the UI thread.
	HHOOK	hookBefore = nullptr;
	HHOOK	hookAfter  = nullptr;
	HHOOK	hookIdle   = nullptr;

	HWND	hwndMyPanel = nullptr;
	HWND	hwndButton1 = nullptr;
	HWND	hwndButton2 = nullptr;

	bool	secondary_waterfall_show = true;
	bool	waterfall_controls_shown = true;

private:
	static BOOL    CALLBACK EnumTopLevelWindowsProc(HWND hwnd, LPARAM lparam);
	static BOOL    CALLBACK EnumPanelsWindowsProc(HWND hwnd, LPARAM lparam);
	static LRESULT CALLBACK HookProcBefore(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK HookProcAfter(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK HookProcIdle(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK MyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool					create_my_panel();
	void					update_layout();
	// After discovering the lower right window subwindows, sort them by their Y coordinate.
	void					sort_lower_right_windows();
	void					unhook_callbacks();

	// If the layout is invalid, it should be updated at idle.
	bool	m_layout_invalid = false;
};
