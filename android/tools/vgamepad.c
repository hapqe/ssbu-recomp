/* vgamepad: a virtual gamepad through /dev/uinput (root), to test controller support on a device with none attached.
 * By default it says it is a USB Xbox 360 controller (045e:028e), so Android's stock key layout maps it as a gamepad and
 * SDL opens it like real hardware. -p: it is the device Procon2Droid makes for a Switch 2 Pro Controller over Bluetooth
 * instead (bus 0x0005, 057e:2069, "Nintendo Switch Pro Controller", sticks 0..65535; read off the Marble with getevent -lp,
 * 2026-10-23; without its rumble, see main), so the input takes the same way through Android as the owner's controller.
 * Commands on stdin, one per line:
 *   b <a|b|x|y|l|r|zl|zr|minus|plus|home|ls|rs> <ms>   press a button for <ms> milliseconds
 *   s <lx> <ly> <ms>                                    left stick at -1..1 for <ms>, then centred
 *   d <up|down|left|right> <ms>                         d-pad
 *   w <ms>                                              wait
 *   p <n> <min ms> <max ms> <hold ms> <lx|hat|btn|mix>  latency probes: n changes at random gaps of min..max ms, each held
 *                                                       for <hold ms>; lx = left stick fully left / right by turns, hat =
 *                                                       d-pad left / right by turns, btn = L, mix = the three in turn. Every
 *                                                       write prints "inj: <CLOCK_MONOTONIC ns> <what> <on|off>".
 * Build: aarch64-linux-android30-clang -O2 -o vgamepad vgamepad.c; run as root: su -c ./vgamepad [-p] < script */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fd, procon;
static int amin = -32768, amax = 32767;                 /* stick range */
static void emit(int type, int code, int val) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = type; e.code = code; e.value = val;
    if (write(fd, &e, sizeof e) != sizeof e) perror("write");
}
static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }
static int stick(float v) { return (int)((amin + amax) / 2.0f + v * (amax - amin) / 2.0f); }
static long long mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000000000LL + t.tv_nsec; }
static void absinfo(int code, int lo, int hi) {
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code; a.absinfo.minimum = lo; a.absinfo.maximum = hi;
    if (!procon && (code == ABS_X || code == ABS_Y || code == ABS_RX || code == ABS_RY)) a.absinfo.flat = 128;
    a.absinfo.value = code == ABS_X || code == ABS_Y || code == ABS_RX || code == ABS_RY ? (lo + hi) / 2 : 0;
    ioctl(fd, UI_ABS_SETUP, &a);
}
static int button(const char *n) {
    static const struct { const char *n; int code; } k[] = {
        {"a", BTN_EAST}, {"b", BTN_SOUTH}, {"x", BTN_NORTH}, {"y", BTN_WEST},     /* Switch labels: A is the east button */
        {"l", BTN_TL}, {"r", BTN_TR}, {"zl", BTN_TL2}, {"zr", BTN_TR2}, {"minus", BTN_SELECT}, {"plus", BTN_START},
        {"home", BTN_MODE}, {"ls", BTN_THUMBL}, {"rs", BTN_THUMBR}};
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) if (!strcmp(n, k[i].n)) return k[i].code;
    return -1;
}
static void probe_set(int kind, int dir, int on) {
    long long t;
    if (kind == 0) { emit(EV_ABS, ABS_X, stick(on ? (dir ? 1.0f : -1.0f) : 0.0f)); t = mono(); syn(); }
    else if (kind == 1) { emit(EV_ABS, ABS_HAT0X, on ? (dir ? 1 : -1) : 0); t = mono(); syn(); }
    else { emit(EV_KEY, BTN_TL, on); t = mono(); syn(); }
    printf("inj: %lld %s %s\n", t, kind == 0 ? (dir ? "lx+" : "lx-") : kind == 1 ? (dir ? "hat+" : "hat-") : "btn", on ? "on" : "off");
    fflush(stdout);
}
int main(int argc, char **argv) {
    procon = argc > 1 && !strcmp(argv[1], "-p");
    if (procon) { amin = 0; amax = 65535; }
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    int keys[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    int axes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
    for (unsigned i = 0; i < sizeof axes / sizeof axes[0]; i++) ioctl(fd, UI_SET_ABSBIT, axes[i]);
    absinfo(ABS_X, amin, amax); absinfo(ABS_Y, amin, amax); absinfo(ABS_RX, amin, amax); absinfo(ABS_RY, amin, amax);
    absinfo(ABS_Z, 0, 255); absinfo(ABS_RZ, 0, 255); absinfo(ABS_HAT0X, -1, 1); absinfo(ABS_HAT0Y, -1, 1);
    /* NO force feedback, although Procon2Droid's device has FF_RUMBLE: a uinput device that offers it must answer every
     * effect upload (UI_BEGIN_FF_UPLOAD) itself, and this one does not, so the game's first rumble left system_server's
     * EventHub::vibrate in its ioctl: system_server and the game stopped answering (2026-10-23, the Marble) */
    struct uinput_setup s;
    memset(&s, 0, sizeof s);
    if (procon) { s.id.bustype = BUS_BLUETOOTH; s.id.vendor = 0x057e; s.id.product = 0x2069; s.id.version = 1; strcpy(s.name, "Nintendo Switch Pro Controller"); }
    else { s.id.bustype = BUS_USB; s.id.vendor = 0x045e; s.id.product = 0x028e; s.id.version = 0x110; strcpy(s.name, "Microsoft X-Box 360 pad"); }
    if (ioctl(fd, UI_DEV_SETUP, &s) || ioctl(fd, UI_DEV_CREATE)) { perror("uinput setup"); return 1; }
    printf("vgamepad: created (%s)\n", procon ? "as Procon2Droid's Switch Pro Controller" : "as an Xbox 360 pad"); fflush(stdout);
    srand((unsigned)mono());
    char line[128], a[16];
    while (fgets(line, sizeof line, stdin)) {
        float x, y; int ms, code, n, lo, hi, hold;
        if (sscanf(line, "b %15s %d", a, &ms) == 2 && (code = button(a)) >= 0) {
            if (code == BTN_TL2 || code == BTN_TR2) { emit(EV_ABS, code == BTN_TL2 ? ABS_Z : ABS_RZ, 255); }
            emit(EV_KEY, code, 1); syn(); usleep(ms * 1000);
            if (code == BTN_TL2 || code == BTN_TR2) { emit(EV_ABS, code == BTN_TL2 ? ABS_Z : ABS_RZ, 0); }
            emit(EV_KEY, code, 0); syn();
        } else if (sscanf(line, "s %f %f %d", &x, &y, &ms) == 3) {
            emit(EV_ABS, ABS_X, stick(x)); emit(EV_ABS, ABS_Y, stick(-y)); syn(); usleep(ms * 1000);
            emit(EV_ABS, ABS_X, stick(0)); emit(EV_ABS, ABS_Y, stick(0)); syn();
        } else if (sscanf(line, "d %15s %d", a, &ms) == 2) {
            int ax = !strcmp(a, "left") || !strcmp(a, "right") ? ABS_HAT0X : ABS_HAT0Y, v = !strcmp(a, "left") || !strcmp(a, "up") ? -1 : 1;
            emit(EV_ABS, ax, v); syn(); usleep(ms * 1000); emit(EV_ABS, ax, 0); syn();
        } else if (sscanf(line, "p %d %d %d %d %15s", &n, &lo, &hi, &hold, a) == 5) {
            int kind = !strcmp(a, "lx") ? 0 : !strcmp(a, "hat") ? 1 : !strcmp(a, "btn") ? 2 : 3;
            for (int i = 0; i < n; i++) {
                int k = kind == 3 ? i % 3 : kind, gap = lo + (hi > lo ? rand() % (hi - lo + 1) : 0);
                usleep(gap * 1000);
                probe_set(k, (i / (kind == 3 ? 3 : 1)) & 1, 1);
                usleep(hold * 1000);
                probe_set(k, (i / (kind == 3 ? 3 : 1)) & 1, 0);
            }
        } else if (sscanf(line, "w %d", &ms) == 1) usleep(ms * 1000);
        else continue;
        printf("vgamepad: %s", line); fflush(stdout);
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
