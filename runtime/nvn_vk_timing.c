/* 2026-10-23: TRUE TIMES ON SCREEN (VK_GOOGLE_display_timing). Included by nvn_vk_draw.c.
 *
 * display.c's display-timing lines are a model: "the vblank after the present shows the frame", on the vblank clock it
 * measured at the start. On the Marble that clock said 119.7 Hz while the panel ran at 60 Hz most of the time (the
 * system raises it to 120 Hz only while the screen is touched), so those lines could not tell an even picture from an
 * uneven one. Here every present gets an id and the display reports when the frame was really shown
 * (vkGetPastPresentationTimingGOOGLE, actualPresentTime): a frame's time on screen = the next frame's actual present
 * time - its own, counted in 8.33 ms steps (a 120 Hz refresh; at 60 Hz only the even counts occur). An even 60 fps is
 * every frame at 16.7 ms. A present the display never reported was replaced before it was shown (0).
 * One "nvn_vk_timing:" line per 1800 frames and one in the report at the exit. Desired present times: EVN below.
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
/* 2026-10-23, EVEN CADENCE ON A FAST PANEL (EVN). On the Marble at 120 Hz the game's poll -> on screen was 24.5 ms in a
 * fight against 45 ms at 60 Hz (the compositor's latch and scan-out are half as long), but a frame presented when it is
 * ready is shown at whichever 120 Hz refresh comes after it: 21-45 % of the frames were on screen for 8 or 25 ms instead of
 * 17 (render + composition took 11-13 ms for most frames and 21-30 ms for a tenth of them). Here every game frame gets a
 * desired present time (VK_GOOGLE_display_timing): its poll + D, put on the panel's refresh grid (the reported true times
 * are refreshes), never closer than one game frame to the one before. D moves in whole refreshes: up when more than 3 %
 * of 120 frames were shown after their time, down when for 600 frames (and 1,800 after a raise) at most one was late and
 * 98 % were ready a whole refresh before they were taken (presentMargin). So the picture is even and the delay is the
 * smallest the frame times allow. Only when the refresh is under 12 ms (a 60 Hz panel shows every frame for one refresh
 * anyway). NOT VERIFIED YET: the one test run (SSBU_FRAME_RATE=120, 2026-10-23) lost its log to a crash, so it is off unless
 * SSBU_VK_EVEN=1 (with SSBU_FRAME_RATE=120 on a 120 Hz panel). */
static struct {
    int want, on;
    PFN_vkGetRefreshCycleDurationGOOGLE rcd;
    LONG64 R, P, D, anchor, last_desired, frames, late, early, n, since_raise, raises, lowers;
    LONG64 desired[256];
} EVN = {.want = -1};
static void evn_refresh(void) {
    VkRefreshCycleDurationGOOGLE rc;
    if (!EVN.rcd || EVN.rcd(V.device, V.swapchain, &rc) != VK_SUCCESS || !rc.refreshDuration) return;
    LONG64 R = (LONG64)rc.refreshDuration, k = (LONG64)(16666667.0 / (double)R + 0.5);
    int on = EVN.want && R < 12000000 && k >= 2 && fabs((double)(k * R) - 16666667.0) < 300000.0;
    LONG64 dn = EVN.R ? (EVN.D + EVN.R / 2) / EVN.R : 4;                /* the delay in refreshes (first guess: two game frames of 120 Hz) */
    int same = EVN.R && R > EVN.R * 99 / 100 && R < EVN.R * 101 / 100;      /* the reported duration wobbles by a microsecond */
    if (!same || on != EVN.on) {
        printf("nvn_vk_timing: refresh %.3f ms: even cadence %s (SSBU_VK_EVEN)\n", (double)R / 1e6, on ? "on: every game frame is given a desired present time on the refresh grid" : "off");
        EVN.n = EVN.late = EVN.early = 0;
    }
    EVN.R = R; EVN.P = k * R; EVN.on = on; EVN.D = dn * R;
}
/* a frame the display reported (dtm_poll) */
static void evn_seen(const VkPastPresentationTimingGOOGLE *t) {
    EVN.anchor = (LONG64)t->actualPresentTime;
    LONG64 d = EVN.desired[t->presentID & 255];
    EVN.desired[t->presentID & 255] = 0;
    if (!EVN.on || !d) return;
    int late = (LONG64)t->actualPresentTime > d + EVN.R / 2;
    EVN.n++; EVN.since_raise++;
    if (late) EVN.late++;
    else if ((LONG64)t->presentMargin > EVN.R + 1000000 && t->presentMargin < 1000000000ull) EVN.early++;
    if (EVN.n % 120 == 0 && EVN.late * 100 > 3 * EVN.n && EVN.D < 8 * EVN.R) {
        EVN.D += EVN.R; EVN.raises++; EVN.since_raise = 0;
        if (EVN.raises + EVN.lowers <= 30) printf("nvn_vk_timing: even cadence: %lld of %lld frames late: the delay from the poll goes up to %.1f ms\n", (long long)EVN.late, (long long)EVN.n, (double)EVN.D / 1e6);
        EVN.n = EVN.late = EVN.early = 0;
    } else if (EVN.n >= 600) {
        if (EVN.late <= 1 && EVN.early * 100 >= 98 * EVN.n && EVN.since_raise >= 1800 && EVN.D > EVN.R) {
            EVN.D -= EVN.R; EVN.lowers++;
            if (EVN.raises + EVN.lowers <= 30) printf("nvn_vk_timing: even cadence: frames ready early: the delay from the poll goes down to %.1f ms\n", (double)EVN.D / 1e6);
        }
        EVN.n = EVN.late = EVN.early = 0;
    }
}
/* the desired present time of the frame presented now (0 = as soon as it is ready) */
static LONG64 evn_desired(uint32_t id, LONG64 poll, LONG64 now) {
    if (EVN.want < 0) {
        const char *e = getenv("SSBU_VK_EVEN");
        EVN.want = e && atoi(e) != 0;                                /* opt-in: measured once (2026-10-23), see the comment above */
        EVN.rcd = (PFN_vkGetRefreshCycleDurationGOOGLE)vkGetDeviceProcAddr(V.device, "vkGetRefreshCycleDurationGOOGLE");
    }
    if (EVN.frames++ % 120 == 0) evn_refresh();
    EVN.desired[id & 255] = 0;
    if (!EVN.on || !EVN.anchor) return 0;
    LONG64 t = (poll ? poll : now) + EVN.D, R = EVN.R;
    LONG64 n = (t - EVN.anchor + R - 1) / R;                         /* up onto the refresh grid (true present times) */
    t = EVN.anchor + n * R;
    if (EVN.last_desired && t < EVN.last_desired + EVN.P - R / 2) t = EVN.last_desired + EVN.P;
    if (t < now) return 0;
    EVN.last_desired = t;
    EVN.desired[id & 255] = t;
    return t - R / 4;                                                /* a little before the refresh: the compositor takes it for that one */
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
        {   /* display.c: queue drain. With a desired present time a frame is held on purpose: there the queue shows as frames
             * shown a whole game frame after their time */
            void display_queue_report(LONG64 margin_ns);
            LONG64 d = EVN.desired[t->presentID & 255];
            if (d) display_queue_report((LONG64)t->actualPresentTime - d >= EVN.P * 94 / 100 ? 1000000000 : 0);
            else if (t->presentMargin < 1000000000ull) display_queue_report((LONG64)t->presentMargin);
        }
        evn_seen(t);
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
        DTL[DTM.id & 255].id = DTM.id; DTL[DTM.id & 255].poll = display_l1_cur_poll(); DTL[DTM.id & 255].qp = q.QuadPart; DTL[DTM.id & 255].fight = g_in_fight;
        ptm->desiredPresentTime = (uint64_t)evn_desired(DTM.id, DTL[DTM.id & 255].poll, q.QuadPart); }
    memset(pti, 0, sizeof *pti);
    pti->sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE; pti->swapchainCount = 1; pti->pTimes = ptm; pti->pNext = pr->pNext;
    pr->pNext = pti;
    if (DTM.n >= 1800) { dtm_line(stdout, "last 1800 frames", DTM.hist, DTM.n); dtl_line(stdout); memset(DTM.hist, 0, sizeof DTM.hist); DTM.n = 0; }
}
static void dtm_report(FILE *out) {
    if (!DTM.on) return;
    if (EVN.R) fprintf(out, "nvn_vk_timing: even cadence %s: refresh %.3f ms, delay from the poll %.1f ms (raised %lld, lowered %lld times)\n", EVN.on ? "on" : "off", (double)EVN.R / 1e6, (double)EVN.D / 1e6, (long long)EVN.raises, (long long)EVN.lowers);
    dtm_line(out, "all frames", DTM.hist_all, DTM.n_all);
    dtl_line(out);
}
