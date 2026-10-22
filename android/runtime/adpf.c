/* CPU clock for the frame: an ADPF performance hint session (APerformanceHint, Android 13+, no root) over the game's
 * thread and the render thread, told every frame how long the game thread worked on it (from its release at the tick to
 * its present call) against a target below the 16.7 ms of a frame. The governor then raises the clock of the cores those
 * threads run on when a frame comes close to its budget, instead of following the load average.
 *
 * Why (the Marble, a fight, 2026-10-22): the game thread needs ~16 ms of its 16.7 ms per frame and every frame that ends
 * after the tick waits for the next one (a 33 ms frame); the big cores meanwhile went up and down between 1.2 and 2.5 GHz.
 *
 * SSBU_ADPF=0 off; SSBU_ADPF_TARGET_US (default 14000) the target work duration. Missing API (Android 12 and older): off. */
#include <dlfcn.h>
#include <pthread.h>
#include <sys/resource.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
static APerformanceHintManager *(*p_get_manager)(void);
static APerformanceHintSession *(*p_create)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
static int (*p_report)(APerformanceHintSession *, int64_t);
static int (*p_set_threads)(APerformanceHintSession *, const pid_t *, size_t);      /* Android 14 */

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int32_t g_tids[8];
static int g_ntids, g_dirty, g_state = -1;      /* state: -1 not set up, 0 off, 1 on */
static APerformanceHintSession *g_session;
static int64_t g_target, g_begin;
static long long g_reports, g_over;

static int64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec; }

static int adpf_setup(void) {
    const char *e = getenv("SSBU_ADPF");
    if (e && !atoi(e)) { printf("adpf: off (SSBU_ADPF=0)\n"); return 0; }
    void *lib = dlopen("libandroid.so", RTLD_NOW);
    if (lib) {
        p_get_manager = (APerformanceHintManager *(*)(void))dlsym(lib, "APerformanceHint_getManager");
        p_create = (APerformanceHintSession *(*)(APerformanceHintManager *, const int32_t *, size_t, int64_t))dlsym(lib, "APerformanceHint_createSession");
        p_report = (int (*)(APerformanceHintSession *, int64_t))dlsym(lib, "APerformanceHint_reportActualWorkDuration");
        p_set_threads = (int (*)(APerformanceHintSession *, const pid_t *, size_t))dlsym(lib, "APerformanceHint_setThreads");
    }
    if (!p_get_manager || !p_create || !p_report) { printf("adpf: no performance hint API on this system: off\n"); return 0; }
    const char *t = getenv("SSBU_ADPF_TARGET_US");
    g_target = (t && atoi(t) > 1000 ? atoi(t) : 14000) * 1000ll;
    return 1;
}

/* any thread that works on every frame, once. It also gets nice -10 (SSBU_FRAME_NICE; 0 = leave it): the game thread
 * and the render thread are on every frame's critical path, and at the -4 / -3 they had they shared the three big cores
 * with the game's task workers and audio threads as equals ("main: not on a CPU" was the largest cause of slow frames). */
void android_adpf_register_thread(void) {
    {   static int nice = 1;
        if (nice == 1) { const char *e = getenv("SSBU_FRAME_NICE"); nice = e ? atoi(e) : -10; if (nice > 0) nice = 0; }
        if (nice < 0 && setpriority(PRIO_PROCESS, gettid(), nice)) printf("adpf: nice %d refused for thread %d\n", nice, gettid());
    }
    pthread_mutex_lock(&g_mu);
    int32_t tid = (int32_t)gettid();
    int known = 0;
    for (int i = 0; i < g_ntids; i++) known |= g_tids[i] == tid;
    if (!known && g_ntids < 8) { g_tids[g_ntids++] = tid; g_dirty = 1; }
    pthread_mutex_unlock(&g_mu);
}

/* game thread: released from its wait for the tick (the frame's work begins) */
void android_adpf_frame_begin(void) { g_begin = now_ns(); }

/* game thread: its present call (the frame's work ends) */
void android_adpf_frame_end(void) {
    if (g_state < 0) { g_state = adpf_setup(); android_adpf_register_thread(); }
    if (!g_state || !g_begin) return;
    int64_t work = now_ns() - g_begin;
    g_begin = 0;
    pthread_mutex_lock(&g_mu);
    if (!g_session) {
        APerformanceHintManager *m = p_get_manager();
        g_session = m ? p_create(m, g_tids, (size_t)g_ntids, g_target) : NULL;
        if (!g_session) { g_state = 0; pthread_mutex_unlock(&g_mu); printf("adpf: the system gave no session: off\n"); return; }
        printf("adpf: performance hint session over %d thread(s), target %.1f ms of work per frame\n", g_ntids, g_target / 1e6);
        g_dirty = 0;
    } else if (g_dirty && p_set_threads) {
        int r = p_set_threads(g_session, (const pid_t *)g_tids, (size_t)g_ntids);
        printf("adpf: session threads now %d (%s)\n", g_ntids, r ? "refused" : "ok");
        g_dirty = 0;
    }
    pthread_mutex_unlock(&g_mu);
    p_report(g_session, work > 0 ? work : 1);
    g_reports++;
    if (work > g_target) g_over++;
    if (g_reports % 3600 == 0) printf("adpf: %lld frames reported, %lld of them over the target\n", g_reports, g_over);
}
