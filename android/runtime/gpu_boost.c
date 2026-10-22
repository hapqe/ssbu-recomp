/* Adreno GPU clock: a KGSL context of our own with a power constraint (the highest power level) and a one-dword CP_NOP
 * submitted with the PWR_CONSTRAINT flag; each such submission holds the GPU at its top level for the driver's idle
 * interval. Needs no root: /dev/kgsl-3d0 is the device every app's GPU driver opens.
 *
 * Why (the Marble, Snapdragon 7+ Gen 2, 2026-10-21): in fights the GPU governor (msm-adreno-tz) sat at 220-285 of
 * 580 MHz with the GPU 65-72 % busy: every frame took two refreshes (28-36 fps) and the CPU side ran slower with it (the
 * GPU's level also votes the memory bus: the audio thread used 290 ms of CPU per second against 210 at 580 MHz, the
 * game's thread 16-19 ms per frame against 9-10). An ADPF hint session did not move the GPU clock.
 *
 * SSBU_GPU_BOOST: 0 off, 1 (default) after a frame that missed its refresh, for SSBU_GPU_BOOST_HOLD_MS (default 20000)
 * after the last such frame (menus that keep up at a low clock are left to the governor), 2 always. Not an Adreno with
 * KGSL: off. The hold was 3 s: in a fight the clock then fell back every few seconds, the next frames came late, and the
 * boost began again (Big Battlefield, 2026-10-22: 55.8 fps with 3 s, 58.1 with the boost always on). */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* msm_kgsl.h (UAPI) */
#define KGSL_IOC_TYPE 0x09
struct kgsl_drawctxt_create { unsigned int flags, drawctxt_id; };
struct kgsl_device_getproperty { unsigned int type; void *value; size_t sizebytes; };
struct kgsl_device_constraint { unsigned int type, context_id; void *data; size_t size; };
struct kgsl_device_constraint_pwrlevel { unsigned int level; };
struct kgsl_gpumem_alloc_id { unsigned int id, flags; size_t size, mmapsize; unsigned long gpuaddr; unsigned long pad[2]; };
struct kgsl_command_object { uint64_t offset, gpuaddr, size; unsigned int flags, id; };
struct kgsl_gpu_command { uint64_t flags, cmdlist; unsigned int cmdsize, numcmds; uint64_t objlist; unsigned int objsize, numobjs;
                          uint64_t synclist; unsigned int syncsize, numsyncs, context_id, timestamp; };
#define IOCTL_KGSL_DRAWCTXT_CREATE _IOWR(KGSL_IOC_TYPE, 0x13, struct kgsl_drawctxt_create)
#define IOCTL_KGSL_SETPROPERTY _IOW(KGSL_IOC_TYPE, 0x32, struct kgsl_device_getproperty)
#define IOCTL_KGSL_GPUMEM_ALLOC_ID _IOWR(KGSL_IOC_TYPE, 0x34, struct kgsl_gpumem_alloc_id)
#define IOCTL_KGSL_GPU_COMMAND _IOWR(KGSL_IOC_TYPE, 0x4A, struct kgsl_gpu_command)
#define KGSL_PROP_PWR_CONSTRAINT 0x12
#define KGSL_CONSTRAINT_PWRLEVEL 1
#define KGSL_CONSTRAINT_PWR_MAX 1
#define KGSL_CONTEXT_NO_GMEM_ALLOC 0x2
#define KGSL_CONTEXT_PREAMBLE 0x10
#define KGSL_CONTEXT_PWR_CONSTRAINT 0x800
#define KGSL_CMDLIST_IB 0x1

static int g_mode = -1, g_fd = -1;
static unsigned g_ctx, g_ib_id;
static unsigned long g_ib_addr;
static int64_t g_until;
static long long g_frames, g_boosted, g_failed;

static unsigned odd_parity(unsigned v) { v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; return (~0x6996u >> (v & 0xf)) & 1; }
static unsigned pkt7(unsigned op, unsigned cnt) { return 0x70000000u | cnt | odd_parity(cnt) << 15 | (op & 0x7f) << 16 | odd_parity(op) << 23; }
static int64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec; }

static int boost_init(void) {
    const char *e = getenv("SSBU_GPU_BOOST");
    int mode = e ? atoi(e) : 1;
    if (mode <= 0) { printf("gpu_boost: off (SSBU_GPU_BOOST=0)\n"); return 0; }
    const char *why = NULL;
    struct kgsl_drawctxt_create cc = {KGSL_CONTEXT_NO_GMEM_ALLOC | KGSL_CONTEXT_PREAMBLE | KGSL_CONTEXT_PWR_CONSTRAINT, 0};
    struct kgsl_device_constraint_pwrlevel lv = {KGSL_CONSTRAINT_PWR_MAX};
    struct kgsl_device_constraint con = {KGSL_CONSTRAINT_PWRLEVEL, 0, &lv, sizeof lv};
    struct kgsl_device_getproperty sp = {KGSL_PROP_PWR_CONSTRAINT, &con, sizeof con};
    struct kgsl_gpumem_alloc_id al = {0, 0, 4096};
    uint32_t *ib = MAP_FAILED;
    if ((g_fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC)) < 0) why = "no /dev/kgsl-3d0 (not an Adreno GPU)";
    else if (ioctl(g_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &cc)) why = "context";
    else if (con.context_id = g_ctx = cc.drawctxt_id, ioctl(g_fd, IOCTL_KGSL_SETPROPERTY, &sp)) why = "power constraint";
    else if (ioctl(g_fd, IOCTL_KGSL_GPUMEM_ALLOC_ID, &al)) why = "command memory";
    else if ((ib = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, (off_t)al.id << 12)) == MAP_FAILED) why = "command memory mapping";
    if (why) {
        printf("gpu_boost: unavailable: %s%s%s\n", why, g_fd >= 0 ? ": " : "", g_fd >= 0 ? strerror(errno) : "");
        if (g_fd >= 0) close(g_fd);                    /* (the context and memory go with the descriptor) */
        g_fd = -1;
        return 0;
    }
    ib[0] = pkt7(0x10, 0);                             /* CP_NOP */
    munmap(ib, 4096);
    g_ib_id = al.id; g_ib_addr = al.gpuaddr;
    printf("gpu_boost: KGSL power constraint ready (context %u): the GPU is held at its highest level %s\n", g_ctx,
           mode >= 2 ? "always (SSBU_GPU_BOOST=2)" : "for a while after a frame that missed its refresh (SSBU_GPU_BOOST_HOLD_MS; SSBU_GPU_BOOST=2: always, 0: off)");
    return mode;
}

/* game thread, once per presented frame; late = the frame missed the refresh it was due at */
void android_gpu_boost_frame(int late) {
    if (g_mode < 0) g_mode = boost_init();
    if (!g_mode) return;
    int64_t now = now_ns();
    static int64_t hold = -1;
    if (hold < 0) { const char *e = getenv("SSBU_GPU_BOOST_HOLD_MS"); hold = (e && atoi(e) > 0 ? atoi(e) : 20000) * 1000000ll; }
    if (late) g_until = now + hold;
    g_frames++;
    if (g_mode < 2 && now >= g_until) return;
    struct kgsl_command_object co = {0, g_ib_addr, 4, KGSL_CMDLIST_IB, g_ib_id};
    struct kgsl_gpu_command gc = {KGSL_CONTEXT_PWR_CONSTRAINT, (uint64_t)(uintptr_t)&co, sizeof co, 1, 0, 0, 0, 0, 0, 0, g_ctx, 0};
    if (ioctl(g_fd, IOCTL_KGSL_GPU_COMMAND, &gc) && g_failed++ < 3) printf("gpu_boost: submission failed: %s\n", strerror(errno));
    g_boosted++;
    if (g_frames % 1800 == 0) printf("gpu_boost: %lld of %lld frames boosted so far\n", g_boosted, g_frames);
}
