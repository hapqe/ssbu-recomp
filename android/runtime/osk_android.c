/* osk.c for Android: the game's software keyboard (nn::swkbd, nnapplet.c) is answered by Android's own text input.
 * osk_run asks the activity (android_main.c, JNI) to show a dialog with an EditText and waits for it; the Windows build
 * draws its own controller keyboard into the Vulkan frame instead (osk.c), which is not needed on a phone. */
#include <windows.h>
#include <stdio.h>
#include "runtime.h"

int android_text_input(const char *header, const char *guide, const char *initial, int max_len, int min_len, int password, char *out, int out_size);   /* android_main.c */
int android_message(const char *text, int question);                                                                                              /* android_main.c */

int osk_key(UINT msg, WPARAM w) { (void)msg; (void)w; return 0; }
int osk_active(void) { return 0; }
int osk_take(uint32_t *dst, LONG *seen) { (void)dst; (void)seen; return 0; }
void osk_size(int *w, int *h) { *w = 0; *h = 0; }
void osk_test_start(void) {}
static void w2u(const wchar_t *w, char *out, int n) { if (!w) { out[0] = 0; return; } WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL); }
/* the text check's message over the keyboard: 1 = yes / OK, 0 = no */
int osk_message(const wchar_t *msg, int question) {
    char m[4096];
    w2u(msg, m, sizeof m);
    return android_message(m, question);
}
/* 1 = confirmed (out holds the text), 0 = cancelled */
int osk_run(const wchar_t *header, const wchar_t *guide, const wchar_t *initial, int max_len, int min_len, int password, wchar_t *out) {
    char h[512], g[1024], i[2048], o[2048];
    w2u(header, h, sizeof h); w2u(guide, g, sizeof g); w2u(initial, i, sizeof i);
    int ok = android_text_input(h, g, i, max_len, min_len, password, o, sizeof o);
    if (ok != 1) return 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, o, -1, out, max_len + 1);
    if (n <= 0) out[0] = 0;
    out[max_len] = 0;
    return 1;
}
