#include <windows.h>
#include <commctrl.h>
#include <vector>

#include "cat.h"
#include "Config.h"
#include "resource.h"

static HWND		 g_hwndConfigDlg = nullptr;
static HINSTANCE g_hInstance	 = nullptr;

static std::vector<std::pair<std::string, int>> g_pages = { 
	{ "TX Power",		IDD_PAGE_POWER		}, 
	{ "TX IQ Balance",	IDD_PAGE_IQ_BALANCE }
};

class ConfigDlg
{
public:
	static void update_tx_waveform()
	{
		g_Cat.setIQBalanceAndPower(g_config.tx_iq_balance_phase_correction, g_config.tx_iq_balance_amplitude_correction, g_config.tx_power);
	}

	static void init_power_tab(HWND hwnd)
	{
		HWND hslider = GetDlgItem(hwnd, IDC_SLIDER_OUTPUT_POWER);
		SendMessage(hslider, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(1, 1000));
		SendMessage(hslider, TBM_SETPAGESIZE, 0, (LPARAM)50);
		SendMessage(hslider, TBM_SETPOS, (WPARAM)TRUE, int(floor(g_config.tx_power * 1000. + 0.5)));
		update_power_text(hwnd);
	}

	static void update_power_text(HWND hwnd)
	{
		HWND htext = GetDlgItem(hwnd, IDC_TEXT_OUTPUT_POWER);
		char text[1024];
		sprintf(text, "Output power: %3.1lf%%", g_config.tx_power * 100.);
		SetWindowTextA(htext, text);
	}

	// Handle WM_HSCROLL messages of IDC_SLIDER_OUTPUT_POWER. hwnd is the handle of the slider control.
	static void handle_power_slider(HWND hwnd)
	{
		DWORD pos;
		pos = SendMessage(hwnd, TBM_GETPOS, 0, 0);
		if (pos > 1000)
			SendMessage(hwnd, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)1000);
		else if (pos < 1)
			SendMessage(hwnd, TBM_SETPOS, (WPARAM)TRUE, 1);
		g_config.tx_power = double(pos) / 1000.;
		update_power_text(GetParent(hwnd));
		update_tx_waveform();
	}

	static void init_iq_balance_tab(HWND hwnd)
	{
		HWND hslider = GetDlgItem(hwnd, IDC_SLIDER_AMPLITUDE_BALANCE_ROUGH);
		// Scale the rough amplitude adjustment by 10,000.
		int  pos_rough = int(floor(g_config.tx_iq_balance_amplitude_correction * 10000. + 0.5));
		SendMessage(hslider, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(8000, 12000));
		SendMessage(hslider, TBM_SETPAGESIZE, 0, (LPARAM)5);
		SendMessage(hslider, TBM_SETPOS, (WPARAM)TRUE, pos_rough);
		hslider = GetDlgItem(hwnd, IDC_SLIDER_AMPLITUDE_BALANCE_FINE);
		// Scale the fine amplitude adjustment by 1,000,000.
		int pos_fine = int(floor(10. * (g_config.tx_iq_balance_amplitude_correction * 10000. - double(pos_rough)) + 0.5)) + 1000;
		SendMessage(hslider, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(0, 2000));
		SendMessage(hslider, TBM_SETPAGESIZE, 0, (LPARAM)50);
		SendMessage(hslider, TBM_SETPOS, (WPARAM)TRUE, pos_fine);
		hslider = GetDlgItem(hwnd, IDC_SLIDER_PHASE_BALANCE_ROUGH);
		// Scale the rough phase adjustment by 1,000.
		pos_rough = int(floor(g_config.tx_iq_balance_phase_correction * 1000. + 0.5)) + 15000;
		SendMessage(hslider, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(0, 30000));
		SendMessage(hslider, TBM_SETPAGESIZE, 0, (LPARAM)50);
		SendMessage(hslider, TBM_SETPOS, (WPARAM)TRUE, pos_rough);
		hslider = GetDlgItem(hwnd, IDC_SLIDER_PHASE_BALANCE_FINE);
		// Scale the fine phase adjustment by 1,000,000.
		pos_fine = int(floor(10. * (g_config.tx_iq_balance_phase_correction * 1000. - double(pos_rough) + 15000.) + 0.5)) + 1000;
		SendMessage(hslider, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(0, 2000));
		SendMessage(hslider, TBM_SETPAGESIZE, 0, (LPARAM)50);
		SendMessage(hslider, TBM_SETPOS, (WPARAM)TRUE, pos_fine);
		update_iq_balance_text(hwnd);
	}

	static void update_iq_balance_text(HWND hwnd)
	{
		HWND htext = GetDlgItem(hwnd, IDC_TEXT_AMPLITUDE_BALANCE);
		char text[1024];
		sprintf(text, "Amplitude balance: %3.2lf%%", g_config.tx_iq_balance_amplitude_correction * 100.);
		SetWindowTextA(htext, text);
		htext = GetDlgItem(hwnd, IDC_TEXT_PHASE_BALANCE);
		sprintf(text, "Phase balance: %2.3lf%%", g_config.tx_iq_balance_phase_correction);
		SetWindowTextA(htext, text);
	}

	static void handle_iq_amplitude_balance_slider_rough(HWND hwnd)
	{
		// Reset the fine adjustment to center.
		SendMessage(GetDlgItem(GetParent(hwnd), IDC_SLIDER_AMPLITUDE_BALANCE_FINE), TBM_SETPOS, (WPARAM)TRUE, 1000);
		// Read and limit the rough adjustment value.
		DWORD pos = SendMessage(hwnd, TBM_GETPOS, 0, 0);
		if (pos > 12000) {
			pos = 12000;
			SendMessage(hwnd, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)pos);
		} else if (pos < 8000) {
			pos = 8000;
			SendMessage(hwnd, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)pos);
		}
		g_config.tx_iq_balance_amplitude_correction = double(pos) / 10000.;
		update_iq_balance_text(GetParent(hwnd));
		update_tx_waveform();
	}

	static void handle_iq_amplitude_balance_slider_fine(HWND hwnd)
	{
		// Read the rough slider value.
		DWORD pos_rough = SendMessage(GetDlgItem(GetParent(hwnd), IDC_SLIDER_AMPLITUDE_BALANCE_ROUGH), TBM_GETPOS, 0, 0);
		// Read and limit the fine adjustment value.
		DWORD pos_fine = SendMessage(hwnd, TBM_GETPOS, 0, 0);
		if (pos_fine > 2000) {
			pos_fine = 2000;
			SendMessage(hwnd, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)2000);
		}
		g_config.tx_iq_balance_amplitude_correction = double(pos_rough) / 10000. + (double(pos_fine) - 1000.) / 100000.;
		update_iq_balance_text(GetParent(hwnd));
		update_tx_waveform();
	}

	static void handle_iq_phase_balance_slider_rough(HWND hwnd)
	{
		// Reset the fine adjustment to center.
		SendMessage(GetDlgItem(GetParent(hwnd), IDC_SLIDER_PHASE_BALANCE_FINE), TBM_SETPOS, (WPARAM)TRUE, 1000);
		// Read and limit the rough adjustment value.
		DWORD pos = SendMessage(hwnd, TBM_GETPOS, 0, 0);
		if (pos > 30000) {
			pos = 30000;
			SendMessage(hwnd, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)pos);
		}
		g_config.tx_iq_balance_phase_correction = (double(pos) - 15000.) / 1000.;
		update_iq_balance_text(GetParent(hwnd));
		update_tx_waveform();
	}

	static void handle_iq_phase_balance_slider_fine(HWND hwnd)
	{
		// Read the rough slider value.
		DWORD pos_rough = SendMessage(GetDlgItem(GetParent(hwnd), IDC_SLIDER_PHASE_BALANCE_ROUGH), TBM_GETPOS, 0, 0);
		// Read and limit the fine adjustment value.
		DWORD pos_fine = SendMessage(hwnd, TBM_GETPOS, 0, 0);
		if (pos_fine > 2000) {
			pos_fine = 2000;
			SendMessage(hwnd, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)2000);
		}
		g_config.tx_iq_balance_phase_correction = (double(pos_rough) - 15000.) / 1000. + (double(pos_fine) - 1000.) / 1000.;
		update_iq_balance_text(GetParent(hwnd));
		update_tx_waveform();
	}

	static BOOL CALLBACK tab_dialog_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_INITDIALOG:
		{
			const int page_id = (int)lParam;
			switch (page_id) {
			case IDD_PAGE_POWER:
				init_power_tab(hwnd);
				break;
			case IDD_PAGE_IQ_BALANCE:
				init_iq_balance_tab(hwnd);
				break;
			default:
				break;
			}
			return TRUE;
		}
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSTATIC:
			return (INT_PTR)GetStockObject(WHITE_BRUSH);
		case WM_HSCROLL:
			switch (GetDlgCtrlID((HWND)lParam)) {
			case IDC_SLIDER_OUTPUT_POWER:
				handle_power_slider((HWND)lParam);
				break;
			case IDC_SLIDER_AMPLITUDE_BALANCE_ROUGH:
				handle_iq_amplitude_balance_slider_rough((HWND)lParam);
				break;
			case IDC_SLIDER_AMPLITUDE_BALANCE_FINE:
				handle_iq_amplitude_balance_slider_fine((HWND)lParam);
				break;
			case IDC_SLIDER_PHASE_BALANCE_ROUGH:
				handle_iq_phase_balance_slider_rough((HWND)lParam);
				break;
			case IDC_SLIDER_PHASE_BALANCE_FINE:
				handle_iq_phase_balance_slider_fine((HWND)lParam);
				break;
			}
			break;
		}
		return FALSE;
	}

	static void set_page(HWND hwnd, int page)
	{
		for (int i = 0; i < int(g_pages.size()); ++i) {
			HWND hwnd_page = GetDlgItem(hwnd, g_pages[i].second);
			if (i == page) {
				if (hwnd_page == nullptr) {
					hwnd_page = CreateDialogParam(g_hInstance, MAKEINTRESOURCE(g_pages[i].second), hwnd, &ConfigDlg::tab_dialog_proc, (LPARAM)g_pages[i].second);
					SetWindowLong(hwnd_page, GWL_ID, g_pages[i].second);
				}
				SetWindowPos(hwnd_page, 0, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
				SetFocus(GetWindow(hwnd_page, GW_CHILD));
			}
			else if (hwnd_page) {
				SetWindowPos(hwnd_page, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
			}
		}
	}

	static void setup_tab_control(HWND hwnd)
	{
		HWND hctl = GetDlgItem(hwnd, IDC_TAB);
		for (size_t i = 0; i < g_pages.size(); ++i) {
			TCITEMA item;
			item.mask = TCIF_TEXT;
			item.pszText = (LPSTR)g_pages[i].first.data();
			SendMessage(hctl, TCM_INSERTITEMA, i, (LPARAM)(&item));
		}
	}

	static void handle_tab_notify(HWND hwnd, LPNMHDR pnmhdr)
	{
		HWND hctl = GetDlgItem(hwnd, IDC_TAB);

		if (pnmhdr->code == TCN_SELCHANGE)
		{
			int p = TabCtrl_GetCurSel(hctl);

			set_page(hwnd, p);
		}
	}

	static BOOL CALLBACK dialog_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_INITDIALOG:
			setup_tab_control(hwnd);
			set_page(hwnd, 0);
			//    main_Create

			return TRUE;

		case WM_NOTIFY:

			if (wParam == IDC_TAB)
				handle_tab_notify(hwnd, (LPNMHDR)lParam);
			else
				return FALSE;

			return TRUE;

		case WM_CLOSE:
			EndDialog(hwnd, 0);
			return TRUE;
		}
		return FALSE;
	}
};

void open_config_dialog(HINSTANCE hInst)
{
	g_hInstance = hInst;
	if (g_hwndConfigDlg == nullptr)
		g_hwndConfigDlg = CreateDialogParam(g_hInstance, MAKEINTRESOURCE(IDD_CONFIG), nullptr, &ConfigDlg::dialog_proc, 0);
	ShowWindow(g_hwndConfigDlg, SW_SHOW);
}

void close_config_dialog()
{
	ShowWindow(g_hwndConfigDlg, SW_HIDE);
}

void toggle_config_dialog(HINSTANCE hInst)
{
	if (g_hwndConfigDlg && IsWindowVisible(g_hwndConfigDlg))
		close_config_dialog();
	else
		open_config_dialog(hInst);
}

void destroy_config_dialog()
{
	DestroyWindow(g_hwndConfigDlg);
}