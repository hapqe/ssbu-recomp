/* mmsystem.h for the Android port: waveOut on AAudio (waveout_aaudio.c), the part nnaudio.c uses. */
#ifndef ANDROID_COMPAT_MMSYSTEM_H
#define ANDROID_COMPAT_MMSYSTEM_H
#include "windows.h"
typedef struct HWAVEOUT__ *HWAVEOUT, **LPHWAVEOUT;
typedef struct tWAVEFORMATEX { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; } WAVEFORMATEX, *LPWAVEFORMATEX;
typedef const WAVEFORMATEX *LPCWAVEFORMATEX;
typedef struct wavehdr_tag { LPSTR lpData; DWORD dwBufferLength, dwBytesRecorded; DWORD_PTR dwUser; DWORD dwFlags, dwLoops; struct wavehdr_tag *lpNext; DWORD_PTR reserved; } WAVEHDR, *PWAVEHDR, *LPWAVEHDR;
typedef struct mmtime_tag { UINT wType; union { DWORD ms, sample, cb, ticks; struct { BYTE hour, min, sec, frame, fps, dummy, pad[2]; } smpte; struct { DWORD songptrpos; } midi; } u; } MMTIME, *LPMMTIME;
#define WAVE_FORMAT_PCM 1
#define WAVE_MAPPER ((UINT)-1)
#define CALLBACK_NULL 0
#define CALLBACK_EVENT 0x50000
#define CALLBACK_FUNCTION 0x30000
#define MMSYSERR_NOERROR 0
#define MMSYSERR_ERROR 1
#define MMSYSERR_BADDEVICEID 2
#define MMSYSERR_INVALHANDLE 5
#define MMSYSERR_NODRIVER 6
#define MMSYSERR_INVALPARAM 11
#define WAVERR_BADFORMAT 32
#define WAVERR_UNPREPARED 34
#define WHDR_DONE 0x1
#define WHDR_PREPARED 0x2
#define WHDR_INQUEUE 0x10
#define TIME_MS 0x1
#define TIME_SAMPLES 0x2
#define TIME_BYTES 0x4
MMRESULT waveOutOpen(LPHWAVEOUT out, UINT dev, LPCWAVEFORMATEX f, DWORD_PTR cb, DWORD_PTR inst, DWORD flags);
MMRESULT waveOutClose(HWAVEOUT w);
MMRESULT waveOutPrepareHeader(HWAVEOUT w, LPWAVEHDR h, UINT n);
MMRESULT waveOutUnprepareHeader(HWAVEOUT w, LPWAVEHDR h, UINT n);
MMRESULT waveOutWrite(HWAVEOUT w, LPWAVEHDR h, UINT n);
MMRESULT waveOutPause(HWAVEOUT w);
MMRESULT waveOutRestart(HWAVEOUT w);
MMRESULT waveOutReset(HWAVEOUT w);
MMRESULT waveOutGetPosition(HWAVEOUT w, LPMMTIME t, UINT n);
MMRESULT waveOutSetVolume(HWAVEOUT w, DWORD v);
MMRESULT waveOutGetVolume(HWAVEOUT w, LPDWORD v);
/* COM bits of nnaudio.c's device watcher (CoCreateInstance fails: no IMMDeviceEnumerator on Android) */
#define STDMETHODCALLTYPE
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154)
#define CLSCTX_ALL 0x17
typedef struct _tagpropertykey { GUID fmtid; DWORD pid; } PROPERTYKEY;
HRESULT CoCreateInstance(const GUID *clsid, void *outer, DWORD ctx, const GUID *iid, void **out);
#endif
