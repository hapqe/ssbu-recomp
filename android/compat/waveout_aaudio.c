/* waveOut (mmsystem.h) on AAudio, for nnaudio.c. One stream per waveOutOpen, 16-bit PCM. The data callback takes the
 * queued WAVEHDRs in order; a header that has been played to its end is marked WHDR_DONE and the CALLBACK_EVENT is set,
 * as waveOut does. waveOutGetPosition counts the frames the callback has handed to the device. */
#include "windows.h"
#include "mmsystem.h"
#include <aaudio/AAudio.h>
#include <android/log.h>

typedef struct HWAVEOUT__ {
    AAudioStream *stream;
    pthread_mutex_t mu;
    WAVEHDR *head, *tail;             /* queue (lpNext) */
    DWORD offset;                     /* bytes of head already played */
    HANDLE event;
    int channels, paused;
    uint64_t frames;                  /* played since open / reset */
    float vol;
} Wave;

static void done(Wave *w, WAVEHDR *h) { h->dwFlags = (h->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE; }
static aaudio_data_callback_result_t data_cb(AAudioStream *s, void *user, void *audio, int32_t nframes) {
    (void)s;
    Wave *w = user;
    int16_t *out = audio;
    size_t want = (size_t)nframes * (size_t)w->channels * 2, put = 0;
    int signal = 0;
    pthread_mutex_lock(&w->mu);
    if (!w->paused) {
        while (put < want && w->head) {
            WAVEHDR *h = w->head;
            size_t left = h->dwBufferLength - w->offset, k = left < want - put ? left : want - put;
            memcpy((uint8_t *)out + put, h->lpData + w->offset, k);
            put += k; w->offset += (DWORD)k;
            if (w->offset >= h->dwBufferLength) { w->head = h->lpNext; if (!w->head) w->tail = NULL; w->offset = 0; done(w, h); signal = 1; }
        }
        w->frames += put / ((size_t)w->channels * 2);
    }
    float vol = w->vol;
    pthread_mutex_unlock(&w->mu);
    if (put < want) memset((uint8_t *)out + put, 0, want - put);
    if (vol < 0.999f) for (size_t i = 0; i < put / 2; i++) out[i] = (int16_t)(out[i] * vol);
    if (signal && w->event) SetEvent(w->event);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void error_cb(AAudioStream *s, void *user, aaudio_result_t e) {
    (void)s; (void)user;
    __android_log_print(ANDROID_LOG_WARN, "OpenSmash", "AAudio stream error %d (%s)", (int)e, AAudio_convertResultToText(e));
}
MMRESULT waveOutOpen(LPHWAVEOUT out, UINT dev, LPCWAVEFORMATEX f, DWORD_PTR cb, DWORD_PTR inst, DWORD flags) {
    (void)dev; (void)inst;
    if (f->wFormatTag != WAVE_FORMAT_PCM || f->wBitsPerSample != 16) return WAVERR_BADFORMAT;
    AAudioStreamBuilder *b;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return MMSYSERR_NODRIVER;
    Wave *w = calloc(1, sizeof *w);
    pthread_mutex_init(&w->mu, NULL);
    w->channels = f->nChannels; w->vol = 1.0f; w->paused = 0;
    w->event = (flags & 0x70000) == CALLBACK_EVENT ? (HANDLE)cb : NULL;
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, f->nChannels);
    AAudioStreamBuilder_setSampleRate(b, (int32_t)f->nSamplesPerSec);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_SONIFICATION);
    AAudioStreamBuilder_setDataCallback(b, data_cb, w);
    AAudioStreamBuilder_setErrorCallback(b, error_cb, w);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &w->stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { __android_log_print(ANDROID_LOG_WARN, "OpenSmash", "AAudio: openStream failed: %s", AAudio_convertResultToText(r)); free(w); return MMSYSERR_NODRIVER; }
    if (AAudioStream_getSampleRate(w->stream) != (int32_t)f->nSamplesPerSec)
        __android_log_print(ANDROID_LOG_INFO, "OpenSmash", "AAudio: %d Hz asked, the stream runs at %d Hz (resampled by AAudio)", (int)f->nSamplesPerSec, (int)AAudioStream_getSampleRate(w->stream));
    AAudioStream_requestStart(w->stream);
    *out = w;
    return MMSYSERR_NOERROR;
}
MMRESULT waveOutClose(HWAVEOUT w) {
    if (!w) return MMSYSERR_INVALHANDLE;
    AAudioStream_requestStop(w->stream);
    AAudioStream_close(w->stream);
    pthread_mutex_destroy(&w->mu);
    free(w);
    return MMSYSERR_NOERROR;
}
MMRESULT waveOutPrepareHeader(HWAVEOUT w, LPWAVEHDR h, UINT n) { (void)w; (void)n; h->dwFlags = (h->dwFlags | WHDR_PREPARED) & ~WHDR_DONE; return MMSYSERR_NOERROR; }
MMRESULT waveOutUnprepareHeader(HWAVEOUT w, LPWAVEHDR h, UINT n) { (void)w; (void)n; h->dwFlags &= ~WHDR_PREPARED; return MMSYSERR_NOERROR; }
MMRESULT waveOutWrite(HWAVEOUT w, LPWAVEHDR h, UINT n) {
    (void)n;
    if (!w) return MMSYSERR_INVALHANDLE;
    if (!(h->dwFlags & WHDR_PREPARED)) return WAVERR_UNPREPARED;
    pthread_mutex_lock(&w->mu);
    h->dwFlags = (h->dwFlags | WHDR_INQUEUE) & ~WHDR_DONE;
    h->lpNext = NULL;
    if (w->tail) w->tail->lpNext = h; else w->head = h;
    w->tail = h;
    pthread_mutex_unlock(&w->mu);
    return MMSYSERR_NOERROR;
}
MMRESULT waveOutPause(HWAVEOUT w) { if (!w) return MMSYSERR_INVALHANDLE; pthread_mutex_lock(&w->mu); w->paused = 1; pthread_mutex_unlock(&w->mu); return MMSYSERR_NOERROR; }
MMRESULT waveOutRestart(HWAVEOUT w) { if (!w) return MMSYSERR_INVALHANDLE; pthread_mutex_lock(&w->mu); w->paused = 0; pthread_mutex_unlock(&w->mu); return MMSYSERR_NOERROR; }
MMRESULT waveOutReset(HWAVEOUT w) {
    if (!w) return MMSYSERR_INVALHANDLE;
    pthread_mutex_lock(&w->mu);
    for (WAVEHDR *h = w->head; h;) { WAVEHDR *n = h->lpNext; done(w, h); h = n; }
    w->head = w->tail = NULL; w->offset = 0; w->frames = 0;
    pthread_mutex_unlock(&w->mu);
    if (w->event) SetEvent(w->event);
    return MMSYSERR_NOERROR;
}
MMRESULT waveOutGetPosition(HWAVEOUT w, LPMMTIME t, UINT n) {
    (void)n;
    if (!w) return MMSYSERR_INVALHANDLE;
    pthread_mutex_lock(&w->mu);
    uint64_t f = w->frames;
    pthread_mutex_unlock(&w->mu);
    if (t->wType == TIME_BYTES) t->u.cb = (DWORD)(f * (uint64_t)w->channels * 2);
    else { t->wType = TIME_SAMPLES; t->u.sample = (DWORD)f; }
    return MMSYSERR_NOERROR;
}
MMRESULT waveOutSetVolume(HWAVEOUT w, DWORD v) { if (!w) return MMSYSERR_INVALHANDLE; w->vol = (float)(v & 0xffff) / 65535.0f; return MMSYSERR_NOERROR; }
MMRESULT waveOutGetVolume(HWAVEOUT w, LPDWORD v) { if (!w) return MMSYSERR_INVALHANDLE; DWORD l = (DWORD)(w->vol * 65535.0f); *v = l | l << 16; return MMSYSERR_NOERROR; }
HRESULT CoCreateInstance(const GUID *clsid, void *outer, DWORD ctx, const GUID *iid, void **out) { (void)clsid; (void)outer; (void)ctx; (void)iid; *out = NULL; return REGDB_E_CLASSNOTREG; }
