/* 2026-10-23: TRUE TIMES ON SCREEN (VK_GOOGLE_display_timing). Included by nvn_vk_draw.c.
 *
 * display.c's display-timing lines are a model: "the vblank after the present shows the frame", on the vblank clock it
 * measured at the start. On the Marble that clock said 119.7 Hz while the panel ran at 60 Hz most of the time (the
 * system raises it to 120 Hz only while the screen is touched), so those lines could not tell an even picture from an
 * uneven one. Here every present gets an id and the display reports when the frame was really shown
 * (vkGetPastPresentationTimingGOOGLE, actualPresentTime): a frame's time on screen = the next frame's actual present
 * time - its own, counted in 8.33 ms steps (a 120 Hz refresh; at 60 Hz only the even counts occur). An even 60 fps is
 * every frame at 16.7 ms. A present the display never reported was replaced before it was shown (0).
 * One "nvn_vk_timing:" line per 1800 frames and one in the report at the exit. No desired present times are set.
 * SSBU_VK_DTIMING=0 off, 1 on (default on Android, off elsewhere). */
#define DTM_STEP 8333333.0                           /* ns: a 120 Hz refresh, the unit of the counts */
static struct {
    int want, on, fail;
    PFN_vkGetPastPresentationTimingGOOGLE past;
    VkSwapchainKHR sc;
    uint32_t id, last_seen;                          /* last presentID given, last one the display reported */
    uint64_t prev_actual;
    LONG64 hist[7], n, hist_all[7], n_all, missed;   /* [0] not shown, [1..5] 8 / 17 / 25 / 33 / 42 ms, [6] longer */
} DTM;
/* 2026-10-23, input latency: player 1's poll of every presented frame (display_l1_cur_poll) against the time the display
 * showed it = the part of input-to-photon that is ours after the game read the pad. Fight frames: percentiles in the report.
 * SSBU_L1IN=1 (test): also one "l1in-frame:" line per frame (CLOCK_MONOTONIC ns), joined with the injector's times offline. */
LONG64 display_l1_cur_poll(void);
#define DTL_BINS 1000                                /* 0.1 ms bins up to 100 ms */
typedef struct { LONG64 poll, qp; uint32_t id; int fight; } DtlFrame;
static DtlFrame DTL[256];
static struct { LONG64 hist[2][DTL_BINS + 1], n[2]; double sum[2], sum2[2]; int log; } DTLS = {.log = -1};
static void dtl_add(int k, double ms) {
    int b = (int)(ms * 10.0);
    if (ms < 0) return;
    DTLS.hist[k][b > DTL_BINS ? DTL_BINS : b]++; DTLS.n[k]++; DTLS.sum[k] += ms; DTLS.sum2[k] += ms * ms;
}
static double dtl_q(int k, double q) {
    LONG64 want = (LONG64)(q * (double)DTLS.n[k]), acc = 0;
    for (int b = 0; b <= DTL_BINS; b++) { acc += DTLS.hist[k][b]; if (acc > want) return (b + 0.5) / 10.0; }
    return 0;
}
static void dtl_seen(const VkPastPresentationTimingGOOGLE *t) {
    if (DTLS.log < 0) DTLS.log = getenv("SSBU_L1IN") && atoi(getenv("SSBU_L1IN"));
    const DtlFrame *f = &DTL[t->presentID & 255];
    if (f->id != t->presentID) return;
    if (DTLS.log) printf("l1in-frame: id %u poll %lld present %lld shown %llu earliest %llu margin %llu fight %d\n", (unsigned)f->id, (long long)f->poll, (long long)f->qp,
                        (unsigned long long)t->actualPresentTime, (unsigned long long)t->earliestPresentTime, (unsigned long long)t->presentMargin, f->fight);
    if (!f->fight || (LONG64)t->actualPresentTime <= f->qp) return;
    if (f->poll) dtl_add(0, (double)((LONG64)t->actualPresentTime - f->poll) / 1e6);
    dtl_add(1, (double)((LONG64)t->actualPresentTime - f->qp) / 1e6);
}
static void dtl_line(FILE *out) {
    static const char *const nm[2] = {"player 1's poll -> on screen (true time)", "vkQueuePresentKHR -> on screen (true time)"};
    for (int k = 0; k < 2; k++) {
        if (!DTLS.n[k]) continue;
        double m = DTLS.sum[k] / (double)DTLS.n[k], v = DTLS.sum2[k] / (double)DTLS.n[k] - m * m;
        fprintf(out, "nvn_vk_timing: %-44s fight frames %6lld: mean %6.2f ms, sd %5.2f, 5 %% %6.2f, median %6.2f, 95 %% %6.2f, 99 %% %6.2f\n", nm[k], (long long)DTLS.n[k], m, v > 0 ? sqrt(v) : 0.0,
                dtl_q(k, 0.05), dtl_q(k, 0.5), dtl_q(k, 0.95), dtl_q(k, 0.99));
    }
}
static int dtm_want(void) {
    if (!DTM.want) {
        const char *e = getenv("SSBU_VK_DTIMING");
#ifdef SSBU_ANDROID
        DTM.want = 1 + (e ? atoi(e) != 0 : 1);
#else
        DTM.want = 1 + (e && atoi(e) != 0);
#endif
    }
    return DTM.want - 1;
}
/* after the device is made (D.has_dtm: the extension was enabled) */
static void dtm_init(void) {
    if (!D.has_dtm) { if (dtm_want()) printf("nvn_vk_timing: the device has no VK_GOOGLE_display_timing: no true times on screen\n"); return; }
    DTM.past = (PFN_vkGetPastPresentationTimingGOOGLE)vkGetDeviceProcAddr(V.device, "vkGetPastPresentationTimingGOOGLE");
    DTM.on = DTM.past != NULL;
    printf("nvn_vk_timing: VK_GOOGLE_display_timing %s (SSBU_VK_DTIMING)\n", DTM.on ? "on: the frames' true times on screen are reported" : "has no functions: off");
}
static void dtm_line(FILE *out, const char *what, const LONG64 *h, LONG64 n) {
    fprintf(out, "nvn_vk_timing: %s: %lld frames on screen for 0 / 8 / 17 / 25 / 33 / 42 / more ms (true times from the display): %lld / %lld / %lld / %lld / %lld / %lld / %lld = %.2f %% NOT for 17 ms\n",
            what, (long long)n, (long long)h[0], (long long)h[1], (long long)h[2], (long long)h[3], (long long)h[4], (long long)h[5], (long long)h[6], n ? 100.0 * (double)(n - h[2]) / (double)n : 0.0);
}
static void dtm_count(int b, LONG64 m) { DTM.hist[b] += m; DTM.hist_all[b] += m; DTM.n += m; DTM.n_all += m; }
/* what the display reported back for earlier presents */
static void dtm_poll(void) {
    VkPastPresentationTimingGOOGLE pt[32];
    if (DTM.sc != V.swapchain) { DTM.sc = V.swapchain; DTM.prev_actual = 0; DTM.last_seen = 0; }      /* made again: its presents' reports went with the old one */
    uint32_t n = 32;
    VkResult r = DTM.past(V.device, V.swapchain, &n, pt);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) { if (DTM.fail++ < 3) printf("nvn_vk_timing: vkGetPastPresentationTimingGOOGLE: %s\n", vk_result_name(r)); return; }
    for (uint32_t i = 0; i < n; i++) {
        VkPastPresentationTimingGOOGLE *t = &pt[i];
        if (!t->actualPresentTime || t->presentID <= DTM.last_seen) continue;
        if (DTM.last_seen && t->presentID - DTM.last_seen > 1 && t->presentID - DTM.last_seen < 64) {      /* never reported: replaced before a refresh showed it */
            LONG64 m = (LONG64)(t->presentID - DTM.last_seen - 1);
            dtm_count(0, m); DTM.missed += m;
        }
        DTM.last_seen = t->presentID;
        dtl_seen(t);
        { void display_queue_report(LONG64 margin_ns); if (t->presentMargin < 1000000000ull) display_queue_report((LONG64)t->presentMargin); }      /* display.c: queue drain */
        if (DTM.prev_actual && t->actualPresentTime > DTM.prev_actual) {
            double steps = (double)(t->actualPresentTime - DTM.prev_actual) / DTM_STEP;
            if (steps < 12.0) { int b = (int)(steps + 0.5); dtm_count(b < 1 ? 1 : b > 6 ? 6 : b, 1); }      /* a gap of 100 ms or more (a load) is not counted */
        }
        DTM.prev_actual = t->actualPresentTime;
    }
}
/* render thread, before vkQueuePresentKHR of a game frame: an id for the present (chained into pr) */
static void dtm_present(VkPresentInfoKHR *pr, VkPresentTimesInfoGOOGLE *pti, VkPresentTimeGOOGLE *ptm) {
    if (!DTM.on || !V.swapchain) return;
    dtm_poll();
    if (!++DTM.id) ++DTM.id;
    ptm->presentID = DTM.id; ptm->desiredPresentTime = 0;
    {   LARGE_INTEGER q; QueryPerformanceCounter(&q);
        DTL[DTM.id & 255].id = DTM.id; DTL[DTM.id & 255].poll = display_l1_cur_poll(); DTL[DTM.id & 255].qp = q.QuadPart; DTL[DTM.id & 255].fight = g_in_fight; }
    memset(pti, 0, sizeof *pti);
    pti->sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE; pti->swapchainCount = 1; pti->pTimes = ptm; pti->pNext = pr->pNext;
    pr->pNext = pti;
    if (DTM.n >= 1800) { dtm_line(stdout, "last 1800 frames", DTM.hist, DTM.n); dtl_line(stdout); memset(DTM.hist, 0, sizeof DTM.hist); DTM.n = 0; }
}
static void dtm_report(FILE *out) {
    if (!DTM.on) return;
    dtm_line(out, "all frames", DTM.hist_all, DTM.n_all);
    dtl_line(out);
}
