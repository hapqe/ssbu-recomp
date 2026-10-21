/* vgamepad: a virtual USB gamepad through /dev/uinput (root), to test controller support on a device with none attached.
 * It says it is an Xbox 360 controller (045e:028e), so Android's stock key layout maps it as a gamepad and SDL opens it
 * like real hardware. Commands on stdin, one per line:
 *   b <a|b|x|y|l|r|zl|zr|minus|plus|home|ls|rs> <ms>   press a button for <ms> milliseconds
 *   s <lx> <ly> <ms>                                    left stick at -1..1 for <ms>, then centred
 *   d <up|down|left|right> <ms>                         d-pad
 *   w <ms>                                              wait
 * Build: aarch64-linux-android30-clang -O2 -o vgamepad vgamepad.c; run as root: su -c ./vgamepad < script */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fd;
static void emit(int type, int code, int val) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = type; e.code = code; e.value = val;
    if (write(fd, &e, sizeof e) != sizeof e) perror("write");
}
static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }
static void absinfo(int code, int lo, int hi) {
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code; a.absinfo.minimum = lo; a.absinfo.maximum = hi;
    if (code == ABS_X || code == ABS_Y || code == ABS_RX || code == ABS_RY) a.absinfo.flat = 128;
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
int main(void) {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    int keys[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    int axes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
    for (unsigned i = 0; i < sizeof axes / sizeof axes[0]; i++) ioctl(fd, UI_SET_ABSBIT, axes[i]);
    absinfo(ABS_X, -32768, 32767); absinfo(ABS_Y, -32768, 32767); absinfo(ABS_RX, -32768, 32767); absinfo(ABS_RY, -32768, 32767);
    absinfo(ABS_Z, 0, 255); absinfo(ABS_RZ, 0, 255); absinfo(ABS_HAT0X, -1, 1); absinfo(ABS_HAT0Y, -1, 1);
    struct uinput_setup s;
    memset(&s, 0, sizeof s);
    s.id.bustype = BUS_USB; s.id.vendor = 0x045e; s.id.product = 0x028e; s.id.version = 0x110;
    strcpy(s.name, "Microsoft X-Box 360 pad");
    if (ioctl(fd, UI_DEV_SETUP, &s) || ioctl(fd, UI_DEV_CREATE)) { perror("uinput setup"); return 1; }
    printf("vgamepad: created\n"); fflush(stdout);
    char line[128], a[16];
    while (fgets(line, sizeof line, stdin)) {
        float x, y; int ms, code;
        if (sscanf(line, "b %15s %d", a, &ms) == 2 && (code = button(a)) >= 0) {
            if (code == BTN_TL2 || code == BTN_TR2) { emit(EV_ABS, code == BTN_TL2 ? ABS_Z : ABS_RZ, 255); }
            emit(EV_KEY, code, 1); syn(); usleep(ms * 1000);
            if (code == BTN_TL2 || code == BTN_TR2) { emit(EV_ABS, code == BTN_TL2 ? ABS_Z : ABS_RZ, 0); }
            emit(EV_KEY, code, 0); syn();
        } else if (sscanf(line, "s %f %f %d", &x, &y, &ms) == 3) {
            emit(EV_ABS, ABS_X, (int)(x * 32767)); emit(EV_ABS, ABS_Y, (int)(y * 32767)); syn(); usleep(ms * 1000);
            emit(EV_ABS, ABS_X, 0); emit(EV_ABS, ABS_Y, 0); syn();
        } else if (sscanf(line, "d %15s %d", a, &ms) == 2) {
            int ax = !strcmp(a, "left") || !strcmp(a, "right") ? ABS_HAT0X : ABS_HAT0Y, v = !strcmp(a, "left") || !strcmp(a, "up") ? -1 : 1;
            emit(EV_ABS, ax, v); syn(); usleep(ms * 1000); emit(EV_ABS, ax, 0); syn();
        } else if (sscanf(line, "w %d", &ms) == 1) usleep(ms * 1000);
        else continue;
        printf("vgamepad: %s", line); fflush(stdout);
    }
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
