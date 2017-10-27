// #define WIN32_LEAN_AND_MEAN             // Selten verwendete Teile der Windows-Header nicht einbinden.
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "ExtIO_Omnia.h"
#include "Audio.h"
#include "cat.h"
#include "Config.h"
#include "UIHooks.h"

#pragma warning(disable : 4996)

#define snprintf	_snprintf

static bool SDR_supports_settings = false;  // assume not supported
static bool SDR_settings_valid = false;		// assume settings are for some other ExtIO

static char SDR_progname[32+1] = "\0";
static int  SDR_ver_major = -1;
static int  SDR_ver_minor = -1;

volatile int64_t	glLOfreq = 0L;
bool	gbInitHW = false;
int		giAttIdx = 0;
int		giDefaultAttIdx = 4;	// 0 dB
int		giMgcIdx = 0;
int		giDefaultMgcIdx = 0;	// 0 dB
int		giAgcIdx = 0;
int		giDefaultAgcIdx = 1;	// Auto
int		giThrIdx = 0;
int		giDefaultThrIdx = 2;	// Threshold: 20 dB
int		giWhatIdx = 0;

pfnExtIOCallback pfnCallback = nullptr;
volatile int	 giParameterSetNo = 0;

Audio	g_Audio;
Cat		g_Cat;
UIHooks g_UIHooks;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

extern "C"
bool __declspec(dllexport) __stdcall InitHW(char *name, char *model, int& type)
{
	type = exthwUSBfloat32;
	strcpy(name,  HWNAME);
	strcpy(model, HWMODEL);

	if ( !gbInitHW )
	{
		// do initialization

		glLOfreq = 6075000L;	// just a default value
		// .......... init here the hardware controlled by the DLL
		// ......... init here the DLL graphical interface, if any

		giAttIdx = giDefaultAttIdx;
		giMgcIdx = giDefaultMgcIdx;
		giAgcIdx = giDefaultAgcIdx;
		giThrIdx = giDefaultThrIdx;

		g_Audio.init();
		if (! g_Audio.get_error().empty()) {
			::MessageBoxA(nullptr, g_Audio.get_error().c_str(), "ExtIO_Omnia", MB_OK | MB_ICONERROR);
		} else {
			g_Cat.init();
			if (! g_Cat.get_error().empty())
				::MessageBoxA(nullptr, g_Cat.get_error().c_str(), "ExtIO_Omnia", MB_OK | MB_ICONERROR);
		}

		gbInitHW = true;
	}
	return gbInitHW;
}

//---------------------------------------------------------------------------
extern "C"
bool EXTIO_API OpenHW(void)
{
	// .... display here the DLL panel ,if any....
	// .....if no graphical interface, delete the following statement
	//::SetWindowPos(F->handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

	if (pfnCallback) {
		pfnCallback(-1, extHw_Changed_ATT, 0.0F, 0);
		// Let HDSDR swap the left / right IQ channels for the Omnia.
		pfnCallback(-1, extHw_RX_SwapIQ_ON, 0.0F, 0);
		pfnCallback(-1, extHw_TX_SwapIQ_ON, 0.0F, 0);
	}

	// in the above statement, F->handle is the window handle of the panel displayed 
	// by the DLL, if such a panel exists
	return gbInitHW;
}

//---------------------------------------------------------------------------
extern "C"
int  EXTIO_API StartHW(long LOfreq)
{
	int64_t ret = StartHW64( (int64_t)LOfreq );
	return (int)ret;
}

//---------------------------------------------------------------------------
extern "C"
int64_t EXTIO_API StartHW64(int64_t LOfreq)
{
	if (!gbInitHW)
		return 0;

	g_UIHooks.initialize();

	g_Audio.stop();
	SetHWLO64(LOfreq);
	g_Audio.start();
	// number of complex elements returned each
	// invocation of the callback routine
	return EXT_BLOCKLEN;
}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API StopHW(void)
{
	g_Audio.stop();
	return;  // nothing to do with this specific HW
}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API CloseHW(void)
{
	// ..... here you can shutdown your graphical interface, if any............
	g_UIHooks.destroy();
	if (gbInitHW)
	{
		/* close port */
	}
	gbInitHW = false;
}

//---------------------------------------------------------------------------
extern "C"
int  EXTIO_API SetHWLO(long LOfreq)
{
	int64_t ret = SetHWLO64( (int64_t)LOfreq );
	return (ret & 0xFFFFFFFF);
}

extern "C"
int64_t EXTIO_API SetHWLO64(int64_t LOfreq)
{
	// take frequency
	glLOfreq = LOfreq;
	g_Cat.set_freq(LOfreq);
	return 0;
}

//---------------------------------------------------------------------------
extern "C"
int  EXTIO_API GetStatus(void)
{
	return 0;  // status not supported by this specific HW,
}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API SetCallback( pfnExtIOCallback funcptr )
{
	pfnCallback = funcptr;
	return;
}

//---------------------------------------------------------------------------
extern "C"
long EXTIO_API GetHWLO(void)
{
	return (long)( glLOfreq & 0xFFFFFFFF );
}

extern "C"
int64_t EXTIO_API GetHWLO64(void)
{
	return glLOfreq;
}

//---------------------------------------------------------------------------
extern "C"
long EXTIO_API GetHWSR(void)
{
	// This DLL controls just an oscillator, not a digitizer
	return SAMPLE_RATE;
}

//---------------------------------------------------------------------------

// extern "C" long EXTIO_API GetTune(void);
// extern "C" void EXTIO_API GetFilters(int& loCut, int& hiCut, int& pitch);
// extern "C" char EXTIO_API GetMode(void);
// extern "C" void EXTIO_API ModeChanged(char mode);
// extern "C" void EXTIO_API IFLimitsChanged(long low, long high);
// extern "C" void EXTIO_API TuneChanged(long freq);

// extern "C" void    EXTIO_API TuneChanged64(int64_t freq);
// extern "C" int64_t EXTIO_API GetTune64(void);
// extern "C" void    EXTIO_API IFLimitsChanged64(int64_t low, int64_t high);

//---------------------------------------------------------------------------

// extern "C" void EXTIO_API RawDataReady(long samprate, int *Ldata, int *Rdata, int numsamples)

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API VersionInfo(const char * progname, int ver_major, int ver_minor)
{
  SDR_progname[0] = 0;
  SDR_ver_major = -1;
  SDR_ver_minor = -1;

  if ( progname )
  {
    strncpy( SDR_progname, progname, sizeof(SDR_progname) -1 );
    SDR_ver_major = ver_major;
    SDR_ver_minor = ver_minor;

	// possibility to check program's capabilities
	// depending on SDR program name and version,
	// f.e. if specific extHWstatusT enums are supported
  }
}

//---------------------------------------------------------------------------

// following "Attenuator"s visible on "RF" button

extern "C"
int EXTIO_API GetAttenuators( int atten_idx, float * attenuation )
{
	// fill in attenuation
	// use positive attenuation levels if signal is amplified (LNA)
	// use negative attenuation levels if signal is attenuated
	// sort by attenuation: use idx 0 for highest attenuation / most damping
	// this functions is called with incrementing idx
	//    - until this functions return != 0 for no more attenuator setting

	switch ( atten_idx )
	{
	case 0:		*attenuation = -30.0F;	return 0;
	case 1:		*attenuation = -20.0F;	return 0;
	case 2:		*attenuation = -10.0F;	return 0;
	case 3:		*attenuation = -6.0F;	return 0;
	case 4:		*attenuation =  0.0F;	return 0;
	case 5:		*attenuation =  9.0F;	return 0;
	default:	return 1;
	}
	return 1;
}

extern "C"
int EXTIO_API GetActualAttIdx(void)
{
	return giAttIdx;	// returns -1 on error
}

extern "C"
int EXTIO_API SetAttenuator( int atten_idx )
{
	int iPrevAttIdx = giAttIdx;

	switch ( atten_idx )
	{
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		giAttIdx = atten_idx;
		if ( iPrevAttIdx != giAttIdx )
			++giParameterSetNo;
		return 0;
	default:
		return 1;	// ERROR
	}
	return 1;	// ERROR
}


//---------------------------------------------------------------------------

// optional function to get AGC Mode: AGC_OFF (always agc_index = 0), AGC_SLOW, AGC_MEDIUM, AGC_FAST, ...
// this functions is called with incrementing idx
//    - until this functions returns != 0, which means that all agc modes are already delivered

extern "C"
int EXTIO_API ExtIoGetAGCs(int agc_idx, char * text)	// text limited to max 16 char
{
	switch (agc_idx)
	{
	case 0:		strcpy(text, "MGC");	return 0;
	case 1:		strcpy(text, "AGC");	return 0;
	case 2:		strcpy(text, "Thr");	return 0;
	//case 3:		strcpy(text, "?");		return 0;
	default:	return 1;
	}
	return 1;
}


extern "C"
int EXTIO_API ExtIoGetActualAGCidx(void)
{
	return giAgcIdx;	// returns -1 on error
}

extern "C"
int EXTIO_API ExtIoSetAGC(int agc_idx)
{
	// returns != 0 on error
	int iPrevAgcIdx = giAgcIdx;

	switch (agc_idx)
	{
	case 0:
	case 1:
	case 2:
	//case 3:
		giAgcIdx = agc_idx;
		if (iPrevAgcIdx != giAgcIdx)
		{
			++giParameterSetNo;
			if (pfnCallback )
				EXTIO_STATUS_CHANGE(pfnCallback, extHw_Changed_RF_IF);
		}
		return 0;
	default:
		return 1;	// ERROR
	}
	return 1;	// ERROR
}

// optional: HDSDR >= 2.62
extern "C"
int EXTIO_API ExtIoShowMGC(int agc_idx)		// return 1, to continue showing MGC slider on AGC
// return 0, is default for not showing MGC slider
{
	switch (agc_idx)
	{
	case 0:	return 1;	// MGC
	case 1:	return 1;	// AGC
	case 2:	return 1;	// Thr
	//case 3:	return 1;	// ?
	default:
		return 0;	// ERROR
	}
	return 0;	// ERROR
}

//---------------------------------------------------------------------------

// following "MGC"s visible on "IF" button

extern "C"
int EXTIO_API ExtIoGetMGCs(int mgc_idx, float * gain)
{
	// fill in gain
	// sort by ascending gain: use idx 0 for lowest gain
	// this functions is called with incrementing idx
	//    - until this functions returns != 0, which means that all gains are already delivered

	switch (giAgcIdx)
	{
	case 0:	// MGC
		switch (mgc_idx)
		{
		case 0:		*gain = 0.0F;	return 0;
		case 1:		*gain = 3.0F;	return 0;
		case 2:		*gain = 6.0F;	return 0;
		default:	return 1;
		}
		break;
	case 1:	// AGC
		//return 1;
		switch (mgc_idx)	// set threshold!
		{
		case 0:		*gain = 0.0F;	return 0;
		case 1:		*gain = 10.0F;	return 0;
		case 2:		*gain = 20.0F;	return 0;
		case 3:		*gain = 30.0F;	return 0;
		case 4:		*gain = 40.0F;	return 0;
		default:	return 1;
		}
		break;
	case 2:	// Thr
		switch (mgc_idx)
		{
		case 0:		*gain = 10.0F;	return 0;
		case 1:		*gain = 20.0F;	return 0;
		case 2:		*gain = 30.0F;	return 0;
		case 3:		*gain = 40.0F;	return 0;
		case 4:		*gain = 50.0F;	return 0;
		default:	return 1;
		}
		break;
	case 3:	// ?
		switch (mgc_idx)
		{
		case 0:		*gain = 50.0F;	return 0;
		case 1:		*gain = 60.0F;	return 0;
		default:	return 1;
		}
		break;
	}
	return 1;
}

extern "C"
int EXTIO_API ExtIoGetActualMgcIdx(void)
{
	switch (giAgcIdx)
	{
	case 0:	// MGC
		return giMgcIdx;	// returns -1 on error
	case 1:	// AGC
		//return -1;
		return giThrIdx;
	case 2:	// Thr
		return giThrIdx;
	case 3:
		return giWhatIdx;
	}
	return -1;
}

extern "C"
int EXTIO_API ExtIoSetMGC(int mgc_idx)
{
	int iPrevMgcIdx = giMgcIdx;
	int iPrevThrIdx = giThrIdx;

	switch (giAgcIdx)
	{
	case 0:	// MGC
		switch (mgc_idx)
		{
		case 0:
		case 1:
		case 2:
			giMgcIdx = mgc_idx;
			if (iPrevMgcIdx != giMgcIdx)
				++giParameterSetNo;
			return 0;
		default:
			return 1;	// ERROR
		}
		break;

	case 1:	// AGC
		//break;

	case 2:	// Thr
		switch (mgc_idx)
		{
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
			giThrIdx = mgc_idx;
			if (iPrevMgcIdx != giThrIdx)
				++giParameterSetNo;
			return 0;
		default:
			return 1;	// ERROR
		}
		break;
	case 3:	// ?
		switch (mgc_idx)
		{
		case 0:
		case 1:
			giWhatIdx = mgc_idx;
			return 0;
		default:
			return 1;	// ERROR
		}
		break;
	}
	return 1;	// ERROR
}

//---------------------------------------------------------------------------

// Only a single 96ksamples per second sampling rate is supported.
extern "C"
int EXTIO_API ExtIoGetSrates(int srate_idx, double *samplerate)
{
	switch (srate_idx) {
	case 0:	 *samplerate = 96000.0;	return 0;
	default:  return 1;	// ERROR
	}
}

extern "C" int  EXTIO_API ExtIoGetActualSrateIdx(void) { return 0; }
extern "C" int  EXTIO_API ExtIoSetSrate(int srate_idx) { return (srate_idx == 0) ? 0 : 1; }
extern "C" long EXTIO_API ExtIoGetBandwidth(int srate_idx) { return (srate_idx == 0) ? 80000L : -1L; }

// HDSDR >= 2.51
// optional functions to receive and set all special receiver settings (for save/restore in application)
//   allows application and profile specific settings.
//   easy to handle without problems with newer Windows versions saving a .ini file below programs as non-admin-user
extern "C"
int  EXTIO_API ExtIoGetSetting( int idx, char * description, char * value )
{
	return -1;	// ERROR
}
extern "C"
void EXTIO_API ExtIoSetSetting(int idx, const char * value)
{
}
