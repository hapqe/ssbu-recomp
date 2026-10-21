/* libguestreserve.so: keeps the guest's fixed addresses free. The lifted code uses the console's addresses as they are
 * (main at 0x7100000000, the modules from 0x7200000000, imports 0x73.., host-function placeholders 0x74.., the SDK at
 * 0x7500000000: runtime/hostrt.h, sdklift.h, nnro.c), and on a 39-bit address space Android puts its own mappings in
 * the same range: an app process forked from zygote inherits scudo's heap reservations there, so the runtime's
 * VirtualAlloc at those addresses fails ("cannot map guest image").
 *
 * So the app is started through wrap.sh (debuggable APK: a fresh exec instead of a zygote fork), which preloads this
 * library: its constructor runs before the C library's heap and ART spread out, and reserves the whole range PROT_NONE.
 * android/compat's VirtualAlloc maps over the reservation (guest_reserve_lo / guest_reserve_hi tell it where it is) and
 * VirtualFree gives the pages back to it. */
#include <android/log.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif

#define LO 0x7100000000ULL
#define HI 0x7600000000ULL

uintptr_t guest_reserve_lo, guest_reserve_hi;       /* 0, 0 when the range could not be reserved */

__attribute__((constructor(101))) static void guest_reserve(void) {
    void *m = mmap((void *)LO, HI - LO, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (m == (void *)LO) {
        prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, m, HI - LO, "opensmash guest");
        guest_reserve_lo = LO;
        guest_reserve_hi = HI;
    } else {
        if (m != MAP_FAILED) munmap(m, HI - LO);    /* an old kernel took the address as a hint */
        __android_log_print(ANDROID_LOG_WARN, "OpenSmash", "guest range 0x%llx..0x%llx is not free: the game cannot start",
                            (unsigned long long)LO, (unsigned long long)HI);
    }
}
