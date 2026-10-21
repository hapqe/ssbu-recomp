/* libnoaslr.so (an executable, named like a library so the APK installs it beside wrap.sh): runs its arguments with the
 * address-space layout randomisation of this process off, as setarch -R does. wrap.sh starts the app through it, so the
 * system's mappings grow down from the top of the 39-bit address space and the guest's fixed range below them stays free
 * for libguestreserve.so (guest_reserve.c); with a random mmap base the C library itself can land in that range. */
#include <stdio.h>
#include <sys/personality.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: libnoaslr.so <program> [args]\n"); return 2; }
    int cur = personality(0xffffffff);
    if (cur != -1) personality((unsigned long)cur | ADDR_NO_RANDOMIZE);
    execv(argv[1], argv + 1);
    perror(argv[1]);
    return 127;
}
