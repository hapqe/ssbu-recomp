/* The Win32 API of windows.h (Android port) on POSIX / bionic. See windows.h for the conventions.
 *
 * Scope: what the runtime calls, with the Windows semantics the runtime relies on. Where Android has no equivalent
 * (suspending another thread, reading its registers, minidumps, the registry, GDI) the call fails the way Windows reports
 * a failure, and the callers already handle that (they are diagnostics). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define COMPAT_NO_FILE_WRAPPERS
#include "windows.h"
#include "psapi.h"
#include "tlhelp32.h"
#include "wincrypt.h"
#include "shlwapi.h"
#include "winsock_compat.h"
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <link.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <linux/futex.h>
#include <android/log.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* ================================================================ errors */
static __thread DWORD t_last_error;
DWORD GetLastError(void) { return t_last_error; }
void SetLastError(DWORD e) { t_last_error = e; }
static DWORD err_from_errno(int e) {
    switch (e) {
    case 0: return 0;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES: case EPERM: case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case EBADF: return ERROR_INVALID_HANDLE;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    case ETIMEDOUT: return ERROR_TIMEOUT;
    default: return 0x1000 + (DWORD)e;
    }
}
static void set_errno_error(void) { t_last_error = err_from_errno(errno); }

/* ================================================================ futex helpers */
static int futex_wait(volatile int32_t *addr, int32_t val, const struct timespec *rel) {
    return (int)syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val, rel, NULL, 0);
}
static void futex_wake(volatile int32_t *addr, int n) { syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0); }
static int64_t mono_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }
static struct timespec ns_to_ts(int64_t ns) { if (ns < 0) ns = 0; struct timespec t = {(time_t)(ns / 1000000000LL), (long)(ns % 1000000000LL)}; return t; }

/* ================================================================ memory */
#define PAGE 4096ull
typedef struct Alloc { uintptr_t base, size; DWORD prot0; uint8_t *page; struct Alloc *next; } Alloc;   /* page[i]: 0 reserved, else prot index */
static Alloc *g_allocs;
static pthread_mutex_t g_alloc_mu = PTHREAD_MUTEX_INITIALIZER;
static const DWORD k_prots[] = {0, PAGE_NOACCESS, PAGE_READONLY, PAGE_READWRITE, PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE};
static uint8_t prot_index(DWORD p) {
    p &= 0xff;
    for (uint8_t i = 1; i < sizeof k_prots / sizeof k_prots[0]; i++) if (k_prots[i] == p) return i;
    if (p == PAGE_WRITECOPY) return 3;
    if (p == PAGE_EXECUTE_WRITECOPY) return 6;
    return 3;
}
static int posix_prot(DWORD p) {
    switch (p & 0xff) {
    case PAGE_NOACCESS: return PROT_NONE;
    case PAGE_READONLY: return PROT_READ;
    case PAGE_READWRITE: case PAGE_WRITECOPY: return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE: return PROT_EXEC;
    case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY: return PROT_READ | PROT_WRITE | PROT_EXEC;
    }
    return PROT_READ | PROT_WRITE;
}
static Alloc *alloc_find(uintptr_t a) {
    for (Alloc *x = g_allocs; x; x = x->next) if (a >= x->base && a < x->base + x->size) return x;
    return NULL;
}
LPVOID VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD prot) {
    if (!size) { t_last_error = ERROR_INVALID_PARAMETER; return NULL; }
    uintptr_t a = (uintptr_t)addr & ~(PAGE - 1);
    uintptr_t end = ((uintptr_t)addr + size + PAGE - 1) & ~(PAGE - 1);
    pthread_mutex_lock(&g_alloc_mu);
    Alloc *x = a ? alloc_find(a) : NULL;
    if (x && (type & MEM_COMMIT)) {                         /* commit inside an existing reservation */
        if (end > x->base + x->size) { pthread_mutex_unlock(&g_alloc_mu); t_last_error = ERROR_INVALID_PARAMETER; return NULL; }
        if (mprotect((void *)a, end - a, posix_prot(prot))) { set_errno_error(); pthread_mutex_unlock(&g_alloc_mu); return NULL; }
        memset(x->page + (a - x->base) / PAGE, prot_index(prot), (end - a) / PAGE);
        pthread_mutex_unlock(&g_alloc_mu);
        return (void *)a;
    }
    if (x) { pthread_mutex_unlock(&g_alloc_mu); t_last_error = ERROR_INVALID_ADDRESS; return NULL; }
    /* a new allocation: Windows reserves at 64 KB granularity */
    if (!a) end = (size + 0xffff) & ~(uintptr_t)0xffff, a = 0;
    size_t len = a ? end - a : end;
    int pp = (type & MEM_COMMIT) ? posix_prot(prot) : PROT_NONE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (a ? MAP_FIXED_NOREPLACE : 0);
    void *m = mmap((void *)a, len, pp, flags, -1, 0);
    if (m == MAP_FAILED || (a && (uintptr_t)m != a)) {
        if (m != MAP_FAILED) munmap(m, len);                /* an old kernel took the address as a hint */
        set_errno_error();
        pthread_mutex_unlock(&g_alloc_mu);
        return NULL;
    }
    x = calloc(1, sizeof *x);
    x->base = (uintptr_t)m; x->size = len; x->prot0 = prot;
    x->page = malloc(len / PAGE);
    memset(x->page, (type & MEM_COMMIT) ? prot_index(prot) : 0, len / PAGE);
    x->next = g_allocs; g_allocs = x;
    pthread_mutex_unlock(&g_alloc_mu);
    return m;
}
BOOL VirtualFree(LPVOID addr, SIZE_T size, DWORD type) {
    uintptr_t a = (uintptr_t)addr;
    pthread_mutex_lock(&g_alloc_mu);
    Alloc **pp = &g_allocs, *x;
    while ((x = *pp) && !(a >= x->base && a < x->base + x->size)) pp = &x->next;
    if (!x) { pthread_mutex_unlock(&g_alloc_mu); t_last_error = ERROR_INVALID_PARAMETER; return FALSE; }
    if (type & MEM_RELEASE) {
        *pp = x->next;
        munmap((void *)x->base, x->size);
        free(x->page); free(x);
    } else if (type & MEM_DECOMMIT) {
        uintptr_t s = a & ~(PAGE - 1), e = size ? ((a + size + PAGE - 1) & ~(PAGE - 1)) : x->base + x->size;
        if (e > x->base + x->size) e = x->base + x->size;
        mmap((void *)s, e - s, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);   /* drops the pages */
        memset(x->page + (s - x->base) / PAGE, 0, (e - s) / PAGE);
    }
    pthread_mutex_unlock(&g_alloc_mu);
    return TRUE;
}
/* a mapping outside our allocations: /proc/self/maps */
static int maps_query(uintptr_t a, uintptr_t *lo, uintptr_t *hi, int *prot, uintptr_t *next_lo) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    *next_lo = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long s, e; char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3) continue;
        if (a >= s && a < e) {
            *lo = s; *hi = e; *prot = (perm[0] == 'r' ? PROT_READ : 0) | (perm[1] == 'w' ? PROT_WRITE : 0) | (perm[2] == 'x' ? PROT_EXEC : 0);
            found = 1; break;
        }
        if (s > a) { *next_lo = s; break; }
    }
    fclose(f);
    return found;
}
static DWORD win_prot(int p) {
    if (p & PROT_EXEC) return (p & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : (p & PROT_READ) ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    if (p & PROT_WRITE) return PAGE_READWRITE;
    if (p & PROT_READ) return PAGE_READONLY;
    return PAGE_NOACCESS;
}
SIZE_T VirtualQuery(LPCVOID addr, PMEMORY_BASIC_INFORMATION mbi, SIZE_T len) {
    if (len < sizeof *mbi) return 0;
    memset(mbi, 0, sizeof *mbi);
    uintptr_t a = (uintptr_t)addr & ~(PAGE - 1);
    pthread_mutex_lock(&g_alloc_mu);
    Alloc *x = alloc_find(a);
    if (x) {
        size_t i = (a - x->base) / PAGE, n = x->size / PAGE, j = i;
        uint8_t s = x->page[i];
        while (j < n && x->page[j] == s) j++;
        mbi->BaseAddress = (void *)a; mbi->AllocationBase = (void *)x->base; mbi->AllocationProtect = x->prot0;
        mbi->RegionSize = (j - i) * PAGE;
        mbi->State = s ? MEM_COMMIT : MEM_RESERVE;
        mbi->Protect = s ? k_prots[s] : 0;
        mbi->Type = MEM_PRIVATE;
        pthread_mutex_unlock(&g_alloc_mu);
        return sizeof *mbi;
    }
    pthread_mutex_unlock(&g_alloc_mu);
    uintptr_t lo, hi, next; int prot;
    if (maps_query(a, &lo, &hi, &prot, &next)) {
        mbi->BaseAddress = (void *)a; mbi->AllocationBase = (void *)lo; mbi->RegionSize = hi - a;
        mbi->State = MEM_COMMIT; mbi->Protect = mbi->AllocationProtect = win_prot(prot); mbi->Type = MEM_PRIVATE;
        return sizeof *mbi;
    }
    mbi->BaseAddress = (void *)a; mbi->RegionSize = next > a ? next - a : PAGE; mbi->State = MEM_FREE; mbi->Protect = PAGE_NOACCESS;
    return sizeof *mbi;
}
BOOL VirtualProtect(LPVOID addr, SIZE_T size, DWORD prot, PDWORD old) {
    uintptr_t a = (uintptr_t)addr & ~(PAGE - 1), e = ((uintptr_t)addr + size + PAGE - 1) & ~(PAGE - 1);
    pthread_mutex_lock(&g_alloc_mu);
    Alloc *x = alloc_find(a);
    if (old) *old = x ? k_prots[x->page[(a - x->base) / PAGE]] : PAGE_READWRITE;
    int r = mprotect((void *)a, e - a, posix_prot(prot));
    if (!r && x) {
        uintptr_t ee = e > x->base + x->size ? x->base + x->size : e;
        memset(x->page + (a - x->base) / PAGE, prot_index(prot), (ee - a) / PAGE);
    }
    pthread_mutex_unlock(&g_alloc_mu);
    if (r) { set_errno_error(); return FALSE; }
    return TRUE;
}
UINT GetWriteWatch(DWORD flags, PVOID base, SIZE_T size, PVOID *addrs, ULONG_PTR *count, ULONG *gran) { (void)flags; (void)base; (void)size; (void)addrs; if (count) *count = 0; if (gran) *gran = 4096; return 1; }
UINT ResetWriteWatch(LPVOID base, SIZE_T size) { (void)base; (void)size; return 1; }
BOOL FlushInstructionCache(HANDLE p, LPCVOID base, SIZE_T n) { (void)p; if (base) __builtin___clear_cache((char *)base, (char *)base + n); return TRUE; }
void *compat_aligned_malloc(size_t size, size_t align) {
    void *p = NULL;
    if (align < sizeof(void *)) align = sizeof(void *);
    return posix_memalign(&p, align, size ? size : 1) ? NULL : p;
}
void *compat_aligned_realloc(void *p, size_t size, size_t align) {
    void *q = compat_aligned_malloc(size, align);
    if (q && p) { size_t old = malloc_usable_size(p); memcpy(q, p, old < size ? old : size); free(p); }
    return q;
}
HANDLE GetProcessHeap(void) { return (HANDLE)1; }
LPVOID HeapAlloc(HANDLE h, DWORD f, SIZE_T n) { (void)h; return (f & HEAP_ZERO_MEMORY) ? calloc(1, n) : malloc(n); }
BOOL HeapFree(HANDLE h, DWORD f, LPVOID p) { (void)h; (void)f; free(p); return TRUE; }
void GetSystemInfo(LPSYSTEM_INFO si) {
    memset(si, 0, sizeof *si);
    si->wProcessorArchitecture = 12;                       /* PROCESSOR_ARCHITECTURE_ARM64 */
    si->dwPageSize = 4096;
    si->dwAllocationGranularity = 65536;
    si->lpMinimumApplicationAddress = (void *)0x10000;
    si->lpMaximumApplicationAddress = (void *)0x7fffffffffull;
    long n = sysconf(_SC_NPROCESSORS_CONF);
    si->dwNumberOfProcessors = n > 0 ? (DWORD)n : 1;
    si->dwActiveProcessorMask = si->dwNumberOfProcessors >= 64 ? ~(DWORD_PTR)0 : (((DWORD_PTR)1 << si->dwNumberOfProcessors) - 1);
}
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX m) {
    long pages = sysconf(_SC_PHYS_PAGES), avail = sysconf(_SC_AVPHYS_PAGES);
    m->ullTotalPhys = (uint64_t)pages * 4096; m->ullAvailPhys = (uint64_t)avail * 4096;
    m->ullTotalPageFile = m->ullTotalPhys; m->ullAvailPageFile = m->ullAvailPhys;
    m->ullTotalVirtual = 1ull << 39; m->ullAvailVirtual = 1ull << 38; m->ullAvailExtendedVirtual = 0;
    m->dwMemoryLoad = pages ? (DWORD)(100 - avail * 100 / pages) : 0;
    return TRUE;
}

/* ================================================================ handle objects */
enum { H_THREAD = 1, H_EVENT, H_MUTEX, H_SEM, H_TIMER, H_FILE, H_FIND, H_SNAP, H_MAPPING, H_PROCESS };
#define OBJ_MAGIC 0x4f424a57u
typedef struct Obj {
    uint32_t magic;
    int type;
    volatile int refs;
    /* waitable state, under g_wmu */
    int signaled, manual;
    /* thread */
    pthread_t th; volatile pid_t tid; LPTHREAD_START_ROUTINE fn; void *arg; DWORD exit_code; int started, suspend; size_t stack;
    /* mutex */
    pid_t owner; int count;
    /* semaphore */
    LONG sem, sem_max;
    /* timer */
    int64_t due_ns; LONG period;
    /* file */
    int fd; char *path;
    /* find */
    DIR *dir; char pattern[MAX_PATH]; char dirpath[MAX_PATH * 2];
    /* snapshot */
    DWORD *tids; int ntids, tpos; struct { uintptr_t base; size_t size; char name[MAX_PATH]; } *mods; int nmods, mpos;
    /* mapping */
    size_t map_size;
} Obj;
static pthread_mutex_t g_wmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wcv;
static pthread_once_t g_wcv_once = PTHREAD_ONCE_INIT;
static void wcv_init(void) { pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a, CLOCK_MONOTONIC); pthread_cond_init(&g_wcv, &a); }
static Obj *obj_new(int type) {
    pthread_once(&g_wcv_once, wcv_init);
    Obj *o = calloc(1, sizeof *o);
    o->magic = OBJ_MAGIC; o->type = type; o->refs = 1; o->fd = -1;
    return o;
}
#define PSEUDO_PROCESS ((HANDLE)(intptr_t)-1)
#define PSEUDO_THREAD ((HANDLE)(intptr_t)-2)
static __thread Obj *t_self;                                 /* the thread object of the running thread */
static Obj *self_obj(void) {
    if (!t_self) { Obj *o = obj_new(H_THREAD); o->th = pthread_self(); o->tid = gettid(); o->started = 1; o->refs = 1 << 20; t_self = o; }
    return t_self;
}
static Obj *obj_of(HANDLE h) {
    if (h == PSEUDO_THREAD) return self_obj();
    if (!h || h == INVALID_HANDLE_VALUE) return NULL;
    Obj *o = (Obj *)h;
    return o->magic == OBJ_MAGIC ? o : NULL;
}
static void obj_release(Obj *o) {
    if (__atomic_sub_fetch(&o->refs, 1, __ATOMIC_ACQ_REL) > 0) return;
    if (o->type == H_THREAD && !o->signaled) return;          /* the running thread keeps it */
    o->magic = 0;
    free(o->path); free(o->tids); free(o->mods);
    free(o);
}
BOOL CloseHandle(HANDLE h) {
    if (h == PSEUDO_THREAD || h == PSEUDO_PROCESS) return TRUE;
    Obj *o = obj_of(h);
    if (!o) { t_last_error = ERROR_INVALID_HANDLE; return FALSE; }
    if (o->type == H_FILE && o->fd >= 0) { close(o->fd); o->fd = -1; }
    if (o->type == H_FIND && o->dir) { closedir(o->dir); o->dir = NULL; }
    if (o->type == H_THREAD) pthread_mutex_lock(&g_wmu);
    int keep = o->type == H_THREAD && !o->signaled;
    if (o->type == H_THREAD) pthread_mutex_unlock(&g_wmu);
    if (keep) { __atomic_sub_fetch(&o->refs, 1, __ATOMIC_ACQ_REL); return TRUE; }
    obj_release(o);
    return TRUE;
}
BOOL DuplicateHandle(HANDLE sp, HANDLE h, HANDLE tp, LPHANDLE out, DWORD access, BOOL inherit, DWORD opts) {
    (void)sp; (void)tp; (void)access; (void)inherit; (void)opts;
    if (h == PSEUDO_THREAD) { Obj *o = self_obj(); __atomic_add_fetch(&o->refs, 1, __ATOMIC_ACQ_REL); *out = o; return TRUE; }
    Obj *o = obj_of(h);
    if (!o) { *out = h; return TRUE; }
    __atomic_add_fetch(&o->refs, 1, __ATOMIC_ACQ_REL);
    *out = o;
    return TRUE;
}

/* ---------------------------------------------------------------- waiting */
static int obj_ready(Obj *o, int64_t now) {
    switch (o->type) {
    case H_EVENT: case H_THREAD: case H_PROCESS: return o->signaled;
    case H_MUTEX: return o->count == 0 || o->owner == gettid();
    case H_SEM: return o->sem > 0;
    case H_TIMER: return o->signaled || (o->due_ns && now >= o->due_ns);
    }
    return 1;
}
static void obj_consume(Obj *o, int64_t now) {
    switch (o->type) {
    case H_EVENT: if (!o->manual) o->signaled = 0; break;
    case H_MUTEX: o->owner = gettid(); o->count++; break;
    case H_SEM: o->sem--; break;
    case H_TIMER:
        if (o->due_ns && now >= o->due_ns) {
            if (o->period > 0) { while (o->due_ns <= now) o->due_ns += (int64_t)o->period * 1000000LL; }
            else o->due_ns = 0;
            o->signaled = o->manual;
        } else if (!o->manual) o->signaled = 0;
        break;
    }
}
DWORD WaitForMultipleObjectsEx(DWORD n, const HANDLE *hs, BOOL all, DWORD ms, BOOL alertable) {
    (void)alertable;
    if (!n || n > MAXIMUM_WAIT_OBJECTS) { t_last_error = ERROR_INVALID_PARAMETER; return WAIT_FAILED; }
    Obj *os[MAXIMUM_WAIT_OBJECTS];
    for (DWORD i = 0; i < n; i++) {
        os[i] = obj_of(hs[i]);
        if (hs[i] == PSEUDO_PROCESS) { if (ms == INFINITE) for (;;) pause(); usleep((useconds_t)ms * 1000); return WAIT_TIMEOUT; }
        if (!os[i]) { t_last_error = ERROR_INVALID_HANDLE; return WAIT_FAILED; }
    }
    int64_t deadline = ms == INFINITE ? INT64_MAX : mono_ns() + (int64_t)ms * 1000000LL;
    pthread_mutex_lock(&g_wmu);
    for (;;) {
        int64_t now = mono_ns(), wake = deadline;
        if (all) {
            DWORD k = 0;
            for (; k < n && obj_ready(os[k], now); k++) {}
            if (k == n) { for (k = 0; k < n; k++) obj_consume(os[k], now); pthread_mutex_unlock(&g_wmu); return WAIT_OBJECT_0; }
        } else {
            for (DWORD k = 0; k < n; k++)
                if (obj_ready(os[k], now)) { obj_consume(os[k], now); pthread_mutex_unlock(&g_wmu); return WAIT_OBJECT_0 + k; }
        }
        if (now >= deadline) { pthread_mutex_unlock(&g_wmu); return WAIT_TIMEOUT; }
        for (DWORD k = 0; k < n; k++) if (os[k]->type == H_TIMER && os[k]->due_ns && os[k]->due_ns < wake) wake = os[k]->due_ns;
        if (wake == INT64_MAX) pthread_cond_wait(&g_wcv, &g_wmu);
        else { struct timespec ts = ns_to_ts(wake); pthread_cond_timedwait(&g_wcv, &g_wmu, &ts); }
    }
}
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms) { return WaitForMultipleObjectsEx(n, hs, all, ms, FALSE); }
DWORD WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL alertable) {
    Obj *o = obj_of(h);
    if (o && o->type == H_TIMER && !o->manual) {             /* the common pacing case: sleep to the due time without the lock */
        pthread_mutex_lock(&g_wmu);
        int64_t due = o->due_ns;
        pthread_mutex_unlock(&g_wmu);
        if (due && ms == INFINITE) {
            struct timespec ts = ns_to_ts(due);
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {}
        }
    }
    return WaitForMultipleObjectsEx(1, &h, FALSE, ms, alertable);
}
DWORD WaitForSingleObject(HANDLE h, DWORD ms) { return WaitForSingleObjectEx(h, ms, FALSE); }
static void signal_change(void) { pthread_cond_broadcast(&g_wcv); }

/* ---------------------------------------------------------------- events, mutexes, semaphores, timers */
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name) {
    (void)sa; (void)name;
    Obj *o = obj_new(H_EVENT);
    o->manual = manual; o->signaled = initial;
    return o;
}
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name) { (void)name; return CreateEventA(sa, manual, initial, NULL); }
HANDLE CreateEventExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access) { (void)access; return CreateEventA(sa, (flags & CREATE_EVENT_MANUAL_RESET) != 0, (flags & CREATE_EVENT_INITIAL_SET) != 0, name); }
static BOOL set_signaled(HANDLE h, int v, int pulse) {
    Obj *o = obj_of(h);
    if (!o) { t_last_error = ERROR_INVALID_HANDLE; return FALSE; }
    pthread_mutex_lock(&g_wmu);
    o->signaled = v;
    if (v) signal_change();
    pthread_mutex_unlock(&g_wmu);
    if (pulse) { sched_yield(); pthread_mutex_lock(&g_wmu); o->signaled = 0; pthread_mutex_unlock(&g_wmu); }
    return TRUE;
}
BOOL SetEvent(HANDLE h) { return set_signaled(h, 1, 0); }
BOOL ResetEvent(HANDLE h) { return set_signaled(h, 0, 0); }
BOOL PulseEvent(HANDLE h) { return set_signaled(h, 1, 1); }
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name) {
    (void)sa; (void)name;
    Obj *o = obj_new(H_MUTEX);
    if (owner) { o->owner = gettid(); o->count = 1; }
    return o;
}
BOOL ReleaseMutex(HANDLE h) {
    Obj *o = obj_of(h);
    if (!o) return FALSE;
    pthread_mutex_lock(&g_wmu);
    BOOL ok = o->count > 0 && o->owner == gettid();
    if (ok && --o->count == 0) { o->owner = 0; signal_change(); }
    pthread_mutex_unlock(&g_wmu);
    return ok;
}
HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCSTR name) {
    (void)sa; (void)name;
    Obj *o = obj_new(H_SEM);
    o->sem = initial; o->sem_max = maximum;
    return o;
}
BOOL ReleaseSemaphore(HANDLE h, LONG n, LPLONG prev) {
    Obj *o = obj_of(h);
    if (!o) return FALSE;
    pthread_mutex_lock(&g_wmu);
    if (prev) *prev = o->sem;
    BOOL ok = o->sem + n <= o->sem_max;
    if (ok) { o->sem += n; signal_change(); }
    pthread_mutex_unlock(&g_wmu);
    return ok;
}
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access) {
    (void)sa; (void)name; (void)access;
    Obj *o = obj_new(H_TIMER);
    o->manual = (flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) != 0;
    return o;
}
HANDLE CreateWaitableTimerA(LPSECURITY_ATTRIBUTES sa, BOOL manual, LPCSTR name) { (void)name; return CreateWaitableTimerExW(sa, NULL, manual ? CREATE_WAITABLE_TIMER_MANUAL_RESET : 0, 0); }
static int64_t filetime_now_100ns(void);
BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, LONG period, PTIMERAPCROUTINE apc, LPVOID arg, BOOL resume) {
    (void)apc; (void)arg; (void)resume;
    Obj *o = obj_of(h);
    if (!o || o->type != H_TIMER) { t_last_error = ERROR_INVALID_HANDLE; return FALSE; }
    int64_t d = due->QuadPart, now = mono_ns();
    int64_t at = d < 0 ? now + (-d) * 100 : now + (d - filetime_now_100ns()) * 100;   /* negative: relative, 100 ns units */
    pthread_mutex_lock(&g_wmu);
    o->due_ns = at > now ? at : now; o->period = period; o->signaled = 0;
    signal_change();
    pthread_mutex_unlock(&g_wmu);
    return TRUE;
}
BOOL SetWaitableTimerEx(HANDLE h, const LARGE_INTEGER *due, LONG period, PTIMERAPCROUTINE apc, LPVOID arg, void *reason, ULONG delay) { (void)reason; (void)delay; return SetWaitableTimer(h, due, period, apc, arg, FALSE); }
BOOL CancelWaitableTimer(HANDLE h) { Obj *o = obj_of(h); if (!o) return FALSE; pthread_mutex_lock(&g_wmu); o->due_ns = 0; pthread_mutex_unlock(&g_wmu); return TRUE; }

/* ---------------------------------------------------------------- threads */
static void altstack_setup(void);
static void *thread_trampoline(void *p) {
    Obj *o = p;
    t_self = o;
    o->tid = gettid();
    altstack_setup();
    pthread_mutex_lock(&g_wmu);
    signal_change();                                          /* CreateThread waits for the tid */
    while (o->suspend > 0) pthread_cond_wait(&g_wcv, &g_wmu);
    o->started = 1;
    pthread_mutex_unlock(&g_wmu);
    DWORD code = o->fn(o->arg);
    pthread_mutex_lock(&g_wmu);
    o->exit_code = code; o->signaled = 1;
    signal_change();
    pthread_mutex_unlock(&g_wmu);
    obj_release(o);
    return NULL;
}
HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, LPDWORD tid) {
    (void)sa;
    Obj *o = obj_new(H_THREAD);
    o->fn = fn; o->arg = arg; o->refs = 2;                    /* the handle and the running thread */
    o->suspend = (flags & CREATE_SUSPENDED) ? 1 : 0;
    o->exit_code = STILL_ACTIVE;
    pthread_attr_t at;
    pthread_attr_init(&at);
    size_t sz = stack ? stack : (size_t)8 << 20;               /* Windows' default 1 MB is too small for lifted code; 8 MB like Linux */
    if (sz < (size_t)1 << 20) sz = (size_t)1 << 20;
    o->stack = sz;
    pthread_attr_setstacksize(&at, sz);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    int r = pthread_create(&o->th, &at, thread_trampoline, o);
    pthread_attr_destroy(&at);
    if (r) { free(o); errno = r; set_errno_error(); return NULL; }
    pthread_mutex_lock(&g_wmu);
    while (!o->tid) pthread_cond_wait(&g_wcv, &g_wmu);
    pthread_mutex_unlock(&g_wmu);
    if (tid) *tid = (DWORD)o->tid;
    return o;
}
DWORD ResumeThread(HANDLE h) {
    Obj *o = obj_of(h);
    if (!o || o->type != H_THREAD) return (DWORD)-1;
    pthread_mutex_lock(&g_wmu);
    DWORD prev = (DWORD)o->suspend;
    if (o->suspend > 0 && --o->suspend == 0) signal_change();
    pthread_mutex_unlock(&g_wmu);
    return prev;
}
DWORD SuspendThread(HANDLE h) { (void)h; t_last_error = ERROR_NOT_SUPPORTED; return (DWORD)-1; }    /* not possible on Android */
BOOL TerminateThread(HANDLE h, DWORD code) { (void)h; (void)code; t_last_error = ERROR_NOT_SUPPORTED; return FALSE; }
BOOL GetExitCodeThread(HANDLE h, LPDWORD code) { Obj *o = obj_of(h); if (!o) return FALSE; pthread_mutex_lock(&g_wmu); *code = o->signaled ? o->exit_code : STILL_ACTIVE; pthread_mutex_unlock(&g_wmu); return TRUE; }
void ExitThread(DWORD code) {
    Obj *o = self_obj();
    pthread_mutex_lock(&g_wmu);
    o->exit_code = code; o->signaled = 1;
    signal_change();
    pthread_mutex_unlock(&g_wmu);
    pthread_exit(NULL);
}
HANDLE GetCurrentThread(void) { return PSEUDO_THREAD; }
HANDLE GetCurrentProcess(void) { return PSEUDO_PROCESS; }
DWORD GetCurrentThreadId(void) { return (DWORD)gettid(); }
DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }
DWORD GetThreadId(HANDLE h) { Obj *o = obj_of(h); return o && o->type == H_THREAD ? (DWORD)o->tid : 0; }
DWORD GetProcessId(HANDLE h) { (void)h; return (DWORD)getpid(); }
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD tid) {
    (void)access; (void)inherit;
    if (tid == (DWORD)gettid()) { Obj *o = self_obj(); __atomic_add_fetch(&o->refs, 1, __ATOMIC_ACQ_REL); return o; }
    Obj *o = obj_new(H_THREAD); o->tid = (pid_t)tid; o->started = 1; o->refs = 1;
    return o;                                               /* priority / affinity work through the tid; waiting on it does not */
}
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid) { (void)access; (void)inherit; (void)pid; t_last_error = ERROR_ACCESS_DENIED; return NULL; }
static pid_t tid_of(HANDLE h) { Obj *o = obj_of(h); return o && o->type == H_THREAD ? o->tid : 0; }
BOOL SetThreadPriority(HANDLE h, int prio) {
    pid_t t = tid_of(h);
    if (!t) return FALSE;
    int nice = prio >= 15 ? -10 : prio >= 2 ? -6 : prio == 1 ? -3 : prio == 0 ? 0 : prio == -1 ? 3 : prio >= -2 ? 6 : 10;   /* Android may refuse < 0: ignored */
    setpriority(PRIO_PROCESS, t, nice);
    return TRUE;
}
int GetThreadPriority(HANDLE h) { (void)h; return THREAD_PRIORITY_NORMAL; }
BOOL SetThreadPriorityBoost(HANDLE h, BOOL d) { (void)h; (void)d; return TRUE; }
DWORD_PTR SetThreadAffinityMask(HANDLE h, DWORD_PTR mask) {
    pid_t t = tid_of(h);
    if (!t) return 0;
    cpu_set_t s; CPU_ZERO(&s);
    for (int i = 0; i < 64 && i < CPU_SETSIZE; i++) if (mask >> i & 1) CPU_SET(i, &s);
    cpu_set_t old; CPU_ZERO(&old);
    sched_getaffinity(t, sizeof old, &old);
    DWORD_PTR prev = 0;
    for (int i = 0; i < 64; i++) if (CPU_ISSET(i, &old)) prev |= (DWORD_PTR)1 << i;
    if (sched_setaffinity(t, sizeof s, &s)) return prev ? prev : 1;    /* refused (isolated cores): report success as Windows would */
    return prev ? prev : 1;
}
BOOL GetProcessAffinityMask(HANDLE h, PDWORD_PTR proc, PDWORD_PTR sys) {
    (void)h;
    SYSTEM_INFO si; GetSystemInfo(&si);
    if (proc) *proc = si.dwActiveProcessorMask;
    if (sys) *sys = si.dwActiveProcessorMask;
    return TRUE;
}
BOOL SetPriorityClass(HANDLE h, DWORD c) { (void)h; (void)c; return TRUE; }
DWORD GetPriorityClass(HANDLE h) { (void)h; return NORMAL_PRIORITY_CLASS; }
HRESULT SetThreadDescription(HANDLE h, PCWSTR name) {
    char n[16]; int i = 0;
    for (; name && name[i] && i < 15; i++) n[i] = name[i] < 128 ? (char)name[i] : '?';
    n[i] = 0;
    Obj *o = obj_of(h);
    if (o && o->type == H_THREAD && o->th) pthread_setname_np(o->th, n);
    return S_OK;
}
DWORD GetCurrentProcessorNumber(void) { int c = sched_getcpu(); return c < 0 ? 0 : (DWORD)c; }
BOOL SwitchToThread(void) { sched_yield(); return TRUE; }
void Sleep(DWORD ms) {
    if (ms == 0) { sched_yield(); return; }
    if (ms == INFINITE) for (;;) pause();
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}
DWORD SleepEx(DWORD ms, BOOL alertable) { (void)alertable; Sleep(ms); return 0; }
void ExitProcess(UINT code) { fflush(stdout); fflush(stderr); _exit((int)code); }
BOOL TerminateProcess(HANDLE h, UINT code) { if (h == PSEUDO_PROCESS) ExitProcess(code); return FALSE; }
BOOL GetExitCodeProcess(HANDLE h, LPDWORD code) { (void)h; *code = STILL_ACTIVE; return TRUE; }

/* ---------------------------------------------------------------- critical sections */
static void cs_lazy(LPCRITICAL_SECTION cs) {
    if (__atomic_load_n(&cs->init, __ATOMIC_ACQUIRE) == 2) return;
    static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&once);
    if (cs->init != 2) {
        pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&cs->m, &a);
        pthread_mutexattr_destroy(&a);
        __atomic_store_n(&cs->init, 2, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&once);
}
void InitializeCriticalSection(LPCRITICAL_SECTION cs) { cs->init = 0; cs_lazy(cs); }
BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin) { InitializeCriticalSection(cs); cs->spin = spin; return TRUE; }
BOOL InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags) { (void)flags; return InitializeCriticalSectionAndSpinCount(cs, spin); }
void DeleteCriticalSection(LPCRITICAL_SECTION cs) { if (cs->init == 2) pthread_mutex_destroy(&cs->m); cs->init = 0; }
static void cs_owned(LPCRITICAL_SECTION cs) { if (cs->RecursionCount++ == 0) cs->OwningThread = (HANDLE)(uintptr_t)gettid(); }
void EnterCriticalSection(LPCRITICAL_SECTION cs) { cs_lazy(cs); pthread_mutex_lock(&cs->m); cs_owned(cs); }
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs) { cs_lazy(cs); if (pthread_mutex_trylock(&cs->m)) return FALSE; cs_owned(cs); return TRUE; }
void LeaveCriticalSection(LPCRITICAL_SECTION cs) { if (--cs->RecursionCount == 0) cs->OwningThread = NULL; pthread_mutex_unlock(&cs->m); }

/* ---------------------------------------------------------------- SRW locks (futex) */
void InitializeSRWLock(PSRWLOCK l) { l->state = 0; l->waiters = 0; }
BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK l) { int32_t z = 0; return __atomic_compare_exchange_n(&l->state, &z, -1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED); }
BOOLEAN TryAcquireSRWLockShared(PSRWLOCK l) {
    int32_t s = __atomic_load_n(&l->state, __ATOMIC_RELAXED);
    while (s >= 0 && !__atomic_load_n(&l->waiters, __ATOMIC_RELAXED))
        if (__atomic_compare_exchange_n(&l->state, &s, s + 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 1;
    return 0;
}
void AcquireSRWLockExclusive(PSRWLOCK l) {
    for (int spin = 0; spin < 64; spin++) { if (TryAcquireSRWLockExclusive(l)) return; YieldProcessor(); }
    __atomic_add_fetch(&l->waiters, 1, __ATOMIC_ACQ_REL);
    for (;;) {
        int32_t s = __atomic_load_n(&l->state, __ATOMIC_ACQUIRE);
        if (s == 0) { int32_t z = 0; if (__atomic_compare_exchange_n(&l->state, &z, -1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) break; continue; }
        futex_wait(&l->state, s, NULL);
    }
    __atomic_sub_fetch(&l->waiters, 1, __ATOMIC_ACQ_REL);
}
void AcquireSRWLockShared(PSRWLOCK l) {
    for (int spin = 0; spin < 64; spin++) {
        int32_t s = __atomic_load_n(&l->state, __ATOMIC_RELAXED);
        if (s >= 0 && __atomic_compare_exchange_n(&l->state, &s, s + 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
        YieldProcessor();
    }
    for (;;) {
        int32_t s = __atomic_load_n(&l->state, __ATOMIC_ACQUIRE);
        if (s >= 0) { if (__atomic_compare_exchange_n(&l->state, &s, s + 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return; continue; }
        futex_wait(&l->state, s, NULL);
    }
}
void ReleaseSRWLockExclusive(PSRWLOCK l) { __atomic_store_n(&l->state, 0, __ATOMIC_RELEASE); futex_wake(&l->state, INT_MAX); }
void ReleaseSRWLockShared(PSRWLOCK l) { if (__atomic_sub_fetch(&l->state, 1, __ATOMIC_RELEASE) == 0) futex_wake(&l->state, INT_MAX); }

/* ---------------------------------------------------------------- condition variables (futex sequence) */
void InitializeConditionVariable(PCONDITION_VARIABLE cv) { cv->seq = 0; cv->waiters = 0; }
static BOOL cv_wait(PCONDITION_VARIABLE cv, DWORD ms, void (*unlock)(void *), void (*lock)(void *), void *l) {
    int32_t s = __atomic_load_n(&cv->seq, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&cv->waiters, 1, __ATOMIC_ACQ_REL);
    unlock(l);
    int r = 0;
    if (ms == INFINITE) futex_wait(&cv->seq, s, NULL);
    else { struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L}; r = futex_wait(&cv->seq, s, &ts); }
    int e = errno;
    __atomic_sub_fetch(&cv->waiters, 1, __ATOMIC_ACQ_REL);
    lock(l);
    if (r == -1 && e == ETIMEDOUT) { t_last_error = ERROR_TIMEOUT; return FALSE; }
    return TRUE;
}
static __thread LONG t_cs_depth;
static void cs_unlock(void *p) { LPCRITICAL_SECTION cs = p; t_cs_depth = cs->RecursionCount; cs->RecursionCount = 0; cs->OwningThread = NULL; pthread_mutex_unlock(&cs->m); }
static void cs_lock(void *p) { LPCRITICAL_SECTION cs = p; LONG d = t_cs_depth; pthread_mutex_lock(&cs->m); cs->RecursionCount = d; cs->OwningThread = (HANDLE)(uintptr_t)gettid(); }
static void srwx_unlock(void *p) { ReleaseSRWLockExclusive(p); }
static void srwx_lock(void *p) { AcquireSRWLockExclusive(p); }
static void srws_unlock(void *p) { ReleaseSRWLockShared(p); }
static void srws_lock(void *p) { AcquireSRWLockShared(p); }
BOOL SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms) { return cv_wait(cv, ms, cs_unlock, cs_lock, cs); }
BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE cv, PSRWLOCK l, DWORD ms, ULONG flags) {
    return (flags & CONDITION_VARIABLE_LOCKMODE_SHARED) ? cv_wait(cv, ms, srws_unlock, srws_lock, l) : cv_wait(cv, ms, srwx_unlock, srwx_lock, l);
}
void WakeConditionVariable(PCONDITION_VARIABLE cv) { __atomic_add_fetch(&cv->seq, 1, __ATOMIC_RELEASE); if (__atomic_load_n(&cv->waiters, __ATOMIC_ACQUIRE)) futex_wake(&cv->seq, 1); }
void WakeAllConditionVariable(PCONDITION_VARIABLE cv) { __atomic_add_fetch(&cv->seq, 1, __ATOMIC_RELEASE); if (__atomic_load_n(&cv->waiters, __ATOMIC_ACQUIRE)) futex_wake(&cv->seq, INT_MAX); }
/* WaitOnAddress: the futex is on the aligned 32-bit word that holds the value */
BOOL WaitOnAddress(volatile void *addr, PVOID cmp, SIZE_T size, DWORD ms) {
    volatile int32_t *w = (volatile int32_t *)((uintptr_t)addr & ~(uintptr_t)3);
    int32_t cur = *w;
    if (memcmp((const void *)addr, cmp, size)) return TRUE;
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    int r = futex_wait(w, cur, ms == INFINITE ? NULL : &ts);
    if (r == -1 && errno == ETIMEDOUT) { t_last_error = ERROR_TIMEOUT; return FALSE; }
    return TRUE;
}
void WakeByAddressSingle(PVOID addr) { futex_wake((volatile int32_t *)((uintptr_t)addr & ~(uintptr_t)3), INT_MAX); }
void WakeByAddressAll(PVOID addr) { futex_wake((volatile int32_t *)((uintptr_t)addr & ~(uintptr_t)3), INT_MAX); }
BOOL InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN fn, PVOID param, LPVOID *ctx) {
    for (;;) {
        int32_t s = __atomic_load_n(&once->state, __ATOMIC_ACQUIRE);
        if (s == 2) { if (ctx) *ctx = once->ctx; return TRUE; }
        if (s == 0) {
            int32_t z = 0;
            if (__atomic_compare_exchange_n(&once->state, &z, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                void *c = NULL;
                BOOL ok = fn(once, param, &c);
                once->ctx = c;
                __atomic_store_n(&once->state, ok ? 2 : 0, __ATOMIC_RELEASE);
                futex_wake(&once->state, INT_MAX);
                if (ctx) *ctx = c;
                return ok;
            }
            continue;
        }
        futex_wait(&once->state, 1, NULL);
    }
}

BOOL InitOnceBeginInitialize(LPINIT_ONCE once, DWORD flags, PBOOL pending, LPVOID *ctx) {
    for (;;) {
        int32_t s = __atomic_load_n(&once->state, __ATOMIC_ACQUIRE);
        if (s == 2) { *pending = FALSE; if (ctx) *ctx = once->ctx; return TRUE; }
        if (flags & INIT_ONCE_CHECK_ONLY) { *pending = TRUE; return FALSE; }
        if (s == 0) { int32_t z = 0; if (__atomic_compare_exchange_n(&once->state, &z, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) { *pending = TRUE; return TRUE; } continue; }
        futex_wait(&once->state, 1, NULL);
    }
}
BOOL InitOnceComplete(LPINIT_ONCE once, DWORD flags, LPVOID ctx) {
    once->ctx = ctx;
    __atomic_store_n(&once->state, (flags & INIT_ONCE_INIT_FAILED) ? 0 : 2, __ATOMIC_RELEASE);
    futex_wake(&once->state, INT_MAX);
    return TRUE;
}

/* ---------------------------------------------------------------- TLS, fibers, FLS */
DWORD TlsAlloc(void) { pthread_key_t k; return pthread_key_create(&k, NULL) ? TLS_OUT_OF_INDEXES : (DWORD)k; }
BOOL TlsFree(DWORD i) { return pthread_key_delete((pthread_key_t)i) == 0; }
LPVOID TlsGetValue(DWORD i) { t_last_error = 0; return pthread_getspecific((pthread_key_t)i); }
BOOL TlsSetValue(DWORD i, LPVOID v) { return pthread_setspecific((pthread_key_t)i, v) == 0; }

#define FLS_SLOTS 128
typedef struct Fiber {
    void *sp;                         /* saved stack pointer (compat_fiber_switch); MUST stay the first field */
    void *param;
    LPFIBER_START_ROUTINE fn;
    void *stack; size_t stack_size;
    int is_thread;
    void *fls[FLS_SLOTS];
} Fiber;
static __thread Fiber *t_fiber;      /* the running fiber; NULL until the thread converts or reads FLS */
static __thread Fiber *t_thread_fiber_implicit;
static PFLS_CALLBACK_FUNCTION g_fls_cb[FLS_SLOTS];
static volatile int32_t g_fls_used[FLS_SLOTS];
void compat_fiber_switch(void **save_sp, void *new_sp);      /* win32_compat_fiber.S */
void compat_fiber_entry(void);                                /* win32_compat_fiber.S: x19 = Fiber * */
static Fiber *cur_fiber(void) {
    if (t_fiber) return t_fiber;
    if (!t_thread_fiber_implicit) { t_thread_fiber_implicit = calloc(1, sizeof(Fiber)); t_thread_fiber_implicit->is_thread = 1; }
    return t_thread_fiber_implicit;
}
LPVOID ConvertThreadToFiber(LPVOID param) {
    Fiber *f = cur_fiber();
    f->param = param;
    t_fiber = f;
    return f;
}
LPVOID ConvertThreadToFiberEx(LPVOID param, DWORD flags) { (void)flags; return ConvertThreadToFiber(param); }
BOOL ConvertFiberToThread(void) { t_fiber = NULL; return TRUE; }
BOOL IsThreadAFiber(void) { return t_fiber != NULL; }
__attribute__((noreturn, used)) void compat_fiber_start(Fiber *f) {
    t_fiber = f;
    f->fn(f->param);
    fprintf(stderr, "win32_compat: a fiber routine returned (the thread ends, as on Windows)\n");
    ExitThread(0);
}
LPVOID CreateFiberEx(SIZE_T commit, SIZE_T reserve, DWORD flags, LPFIBER_START_ROUTINE fn, LPVOID param) {
    (void)flags;
    size_t sz = reserve ? reserve : commit ? commit : (size_t)1 << 20;
    if (sz < 64 * 1024) sz = 64 * 1024;
    sz = (sz + PAGE - 1) & ~(PAGE - 1);
    void *stk = mmap(NULL, sz + PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (stk == MAP_FAILED) { set_errno_error(); return NULL; }
    mprotect(stk, PAGE, PROT_NONE);                         /* guard page */
    Fiber *f = calloc(1, sizeof *f);
    f->fn = fn; f->param = param; f->stack = stk; f->stack_size = sz + PAGE;
    /* initial frame for compat_fiber_switch: 20 saved registers (x19..x30, d8..d15), x30 = entry, x19 = fiber */
    uint64_t *top = (uint64_t *)((uint8_t *)stk + sz + PAGE);
    uint64_t *frame = top - 22;                               /* 160 bytes of registers + 16 spare, 16-aligned */
    memset(frame, 0, 22 * 8);
    frame[0] = (uint64_t)(uintptr_t)f;                        /* x19 */
    frame[11] = (uint64_t)(uintptr_t)compat_fiber_entry;      /* x30 */
    f->sp = frame;
    return f;
}
LPVOID CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE fn, LPVOID param) { return CreateFiberEx(stack, stack, 0, fn, param); }
void DeleteFiber(LPVOID p) {
    Fiber *f = p;
    if (!f) return;
    if (f == t_fiber) ExitThread(0);
    if (f->stack) munmap(f->stack, f->stack_size);
    if (!f->is_thread) free(f);
}
void SwitchToFiber(LPVOID p) {
    Fiber *to = p, *from = cur_fiber();
    if (to == from) return;
    t_fiber = to;
    compat_fiber_switch(&from->sp, to->sp);
    t_fiber = from;                                           /* back here: we are `from` again (TLS may be another thread's) */
}
PVOID GetCurrentFiber(void) { return t_fiber; }
PVOID GetFiberData(void) { return t_fiber ? t_fiber->param : NULL; }
DWORD FlsAlloc(PFLS_CALLBACK_FUNCTION cb) {
    for (int i = 0; i < FLS_SLOTS; i++) {
        int32_t z = 0;
        if (__atomic_compare_exchange_n(&g_fls_used[i], &z, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) { g_fls_cb[i] = cb; return (DWORD)i; }
    }
    return FLS_OUT_OF_INDEXES;
}
BOOL FlsFree(DWORD i) { if (i >= FLS_SLOTS) return FALSE; g_fls_used[i] = 0; return TRUE; }
PVOID FlsGetValue(DWORD i) { return i < FLS_SLOTS ? cur_fiber()->fls[i] : NULL; }
BOOL FlsSetValue(DWORD i, PVOID v) { if (i >= FLS_SLOTS) return FALSE; cur_fiber()->fls[i] = v; return TRUE; }

/* ================================================================ time */
#define EPOCH_DIFF_100NS 116444736000000000LL      /* 1601-01-01 .. 1970-01-01 */
static int64_t filetime_now_100ns(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return (int64_t)t.tv_sec * 10000000LL + t.tv_nsec / 100 + EPOCH_DIFF_100NS; }
BOOL QueryPerformanceCounter(LARGE_INTEGER *c) { c->QuadPart = mono_ns(); return TRUE; }
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000000LL; return TRUE; }
ULONGLONG GetTickCount64(void) { return (ULONGLONG)(mono_ns() / 1000000LL); }
DWORD GetTickCount(void) { return (DWORD)GetTickCount64(); }
DWORD timeGetTime(void) { return (DWORD)GetTickCount64(); }
MMRESULT timeBeginPeriod(UINT p) { (void)p; return 0; }
MMRESULT timeEndPeriod(UINT p) { (void)p; return 0; }
static void ft_set(LPFILETIME ft, int64_t v) { ft->dwLowDateTime = (DWORD)v; ft->dwHighDateTime = (DWORD)((uint64_t)v >> 32); }
static int64_t ft_get(const FILETIME *ft) { return (int64_t)(((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime); }
void GetSystemTimeAsFileTime(LPFILETIME ft) { ft_set(ft, filetime_now_100ns()); }
void GetSystemTimePreciseAsFileTime(LPFILETIME ft) { ft_set(ft, filetime_now_100ns()); }
static void tm_to_st(const struct tm *tm, int ms, LPSYSTEMTIME st) {
    st->wYear = (WORD)(tm->tm_year + 1900); st->wMonth = (WORD)(tm->tm_mon + 1); st->wDayOfWeek = (WORD)tm->tm_wday; st->wDay = (WORD)tm->tm_mday;
    st->wHour = (WORD)tm->tm_hour; st->wMinute = (WORD)tm->tm_min; st->wSecond = (WORD)tm->tm_sec; st->wMilliseconds = (WORD)ms;
}
void GetLocalTime(LPSYSTEMTIME st) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); struct tm tm; localtime_r(&t.tv_sec, &tm); tm_to_st(&tm, (int)(t.tv_nsec / 1000000), st); }
void GetSystemTime(LPSYSTEMTIME st) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); struct tm tm; gmtime_r(&t.tv_sec, &tm); tm_to_st(&tm, (int)(t.tv_nsec / 1000000), st); }
BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st) {
    int64_t v = ft_get(ft) - EPOCH_DIFF_100NS;
    time_t s = (time_t)(v / 10000000LL); int ms = (int)((v % 10000000LL) / 10000);
    if (ms < 0) { ms += 1000; s--; }
    struct tm tm; gmtime_r(&s, &tm); tm_to_st(&tm, ms, st);
    return TRUE;
}
BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft) {
    struct tm tm = {0};
    tm.tm_year = st->wYear - 1900; tm.tm_mon = st->wMonth - 1; tm.tm_mday = st->wDay; tm.tm_hour = st->wHour; tm.tm_min = st->wMinute; tm.tm_sec = st->wSecond;
    ft_set(ft, (int64_t)timegm(&tm) * 10000000LL + st->wMilliseconds * 10000LL + EPOCH_DIFF_100NS);
    return TRUE;
}
BOOL FileTimeToLocalFileTime(const FILETIME *ft, LPFILETIME out) {
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    ft_set(out, ft_get(ft) + (int64_t)tm.tm_gmtoff * 10000000LL);
    return TRUE;
}
static void ascii_to_w(WCHAR *d, const char *s, size_t n) { size_t i = 0; for (; s && s[i] && i + 1 < n; i++) d[i] = (WCHAR)(unsigned char)s[i]; d[i] = 0; }
DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION tz) {
    memset(tz, 0, sizeof *tz);
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    tz->Bias = (LONG)(-tm.tm_gmtoff / 60);
    tzset();
    ascii_to_w(tz->StandardName, tzname[0], 32); ascii_to_w(tz->DaylightName, tzname[1], 32);
    return TIME_ZONE_ID_UNKNOWN;
}
DWORD GetDynamicTimeZoneInformation(PDYNAMIC_TIME_ZONE_INFORMATION tz) {
    memset(tz, 0, sizeof *tz);
    TIME_ZONE_INFORMATION t; DWORD r = GetTimeZoneInformation(&t);
    tz->Bias = t.Bias; memcpy(tz->StandardName, t.StandardName, sizeof t.StandardName); memcpy(tz->DaylightName, t.DaylightName, sizeof t.DaylightName);
    const char *name = getenv("SSBU_TZ_NAME");                /* the launcher passes the IANA name (TimeZone.getDefault) */
    ascii_to_w(tz->TimeZoneKeyName, name ? name : "UTC", 128);
    return r;
}

/* ================================================================ paths and files */
const char *compat_path(const char *in, char *dst, size_t n) {
    if (!in) return NULL;
    size_t i = 0;
    for (; in[i] && i + 1 < n; i++) dst[i] = in[i] == '\\' ? '/' : in[i];
    dst[i] = 0;
    return dst;
}
#define PATHBUF char _pb[MAX_PATH * 4]
#define P(x) compat_path((x), _pb, sizeof _pb)
FILE *compat_fopen(const char *path, const char *mode) { PATHBUF; return fopen(P(path), mode); }
FILE *compat_freopen(const char *path, const char *mode, FILE *f) { PATHBUF; return freopen(path ? P(path) : NULL, mode, f); }
int compat_remove(const char *path) { PATHBUF; return remove(P(path)); }
int compat_rename(const char *a, const char *b) { char x[MAX_PATH * 4], y[MAX_PATH * 4]; return rename(compat_path(a, x, sizeof x), compat_path(b, y, sizeof y)); }
int compat_stat(const char *path, struct stat *st) { PATHBUF; return stat(P(path), st); }
int compat_access(const char *p, int m) { PATHBUF; return access(P(p), m == 6 ? R_OK | W_OK : m == 2 ? W_OK : m == 4 ? R_OK : F_OK); }
int compat_mkdir(const char *p) { PATHBUF; return mkdir(P(p), 0777); }
int compat_rmdir(const char *p) { PATHBUF; return rmdir(P(p)); }
int compat_chdir(const char *p) { PATHBUF; return chdir(P(p)); }
int compat_unlink(const char *p) { PATHBUF; return unlink(P(p)); }
static void to_backslash(char *s) { for (; *s; s++) if (*s == '/') *s = '\\'; }
char *compat_fullpath(char *out, const char *p, size_t n) {
    PATHBUF;
    char tmp[PATH_MAX];
    const char *q = P(p);
    if (!realpath(q, tmp)) {                                  /* Windows makes a full path of names that do not exist yet */
        if (q[0] == '/') snprintf(tmp, sizeof tmp, "%s", q);
        else { char cwd[PATH_MAX]; if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0; snprintf(tmp, sizeof tmp, "%s/%s", cwd, q); }
    }
    if (!out) { out = malloc(strlen(tmp) + 1); n = strlen(tmp) + 1; }
    snprintf(out, n, "%s", tmp);
    to_backslash(out);
    return out;
}
FILE *compat_wfopen(const WCHAR *p, const WCHAR *m) {
    char a[MAX_PATH * 4], b[16];
    WideCharToMultiByte(CP_UTF8, 0, p, -1, a, sizeof a, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, m, -1, b, sizeof b, NULL, NULL);
    return compat_fopen(a, b);
}
static int w2u(LPCWSTR w, char *out, size_t n) { return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL); }

HANDLE compat_handle_from_fd(int fd) { Obj *o = obj_new(H_FILE); o->fd = dup(fd); return o; }
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    (void)share; (void)sa; (void)tmpl;
    PATHBUF;
    const char *q = P(path);
    int fl = (access & (GENERIC_WRITE | GENERIC_ALL)) ? ((access & (GENERIC_READ | GENERIC_ALL)) ? O_RDWR : O_WRONLY) : O_RDONLY;
    struct stat st;
    int exists = stat(q, &st) == 0;
    switch (disp) {
    case CREATE_NEW: fl |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: fl |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: fl |= O_CREAT; break;
    case TRUNCATE_EXISTING: fl |= O_TRUNC; break;
    }
    if (exists && S_ISDIR(st.st_mode)) {                      /* FILE_FLAG_BACKUP_SEMANTICS opens folders */
        if (!(flags & FILE_FLAG_BACKUP_SEMANTICS)) { t_last_error = ERROR_ACCESS_DENIED; return INVALID_HANDLE_VALUE; }
        fl = O_RDONLY | O_DIRECTORY;
    }
    int fd = open(q, fl | O_CLOEXEC, 0666);
    if (fd < 0) { set_errno_error(); return INVALID_HANDLE_VALUE; }
    Obj *o = obj_new(H_FILE);
    o->fd = fd; o->path = strdup(q);
    t_last_error = (exists && (disp == CREATE_ALWAYS || disp == OPEN_ALWAYS)) ? ERROR_ALREADY_EXISTS : 0;
    if ((flags & FILE_FLAG_DELETE_ON_CLOSE)) unlink(q);
    return o;
}
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    char p[MAX_PATH * 4]; w2u(path, p, sizeof p);
    return CreateFileA(p, access, share, sa, disp, flags, tmpl);
}
static int fd_of(HANDLE h) {
    if (h == (HANDLE)(intptr_t)-11 || h == (HANDLE)(intptr_t)-12) return h == (HANDLE)(intptr_t)-11 ? 1 : 2;
    Obj *o = obj_of(h);
    return o && o->type == H_FILE ? o->fd : -1;
}
BOOL ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov) {
    int fd = fd_of(h);
    if (fd < 0) { t_last_error = ERROR_INVALID_HANDLE; return FALSE; }
    ssize_t r;
    if (ov) r = pread(fd, buf, n, (off_t)(((uint64_t)ov->OffsetHigh << 32) | ov->Offset));
    else { size_t done = 0; r = 0; while (done < n) { r = read(fd, (char *)buf + done, n - done); if (r <= 0) break; done += (size_t)r; } if (r >= 0) r = (ssize_t)done; }
    if (r < 0) { set_errno_error(); if (got) *got = 0; return FALSE; }
    if (got) *got = (DWORD)r;
    return TRUE;
}
BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD put, LPOVERLAPPED ov) {
    int fd = fd_of(h);
    if (fd < 0) { t_last_error = ERROR_INVALID_HANDLE; return FALSE; }
    ssize_t r;
    if (ov) r = pwrite(fd, buf, n, (off_t)(((uint64_t)ov->OffsetHigh << 32) | ov->Offset));
    else { size_t done = 0; r = 0; while (done < n) { r = write(fd, (const char *)buf + done, n - done); if (r <= 0) break; done += (size_t)r; } if (r >= 0) r = (ssize_t)done; }
    if (r < 0) { set_errno_error(); if (put) *put = 0; return FALSE; }
    if (put) *put = (DWORD)r;
    return TRUE;
}
BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER d, PLARGE_INTEGER pos, DWORD how) {
    int fd = fd_of(h);
    off_t r = lseek(fd, (off_t)d.QuadPart, how == FILE_BEGIN ? SEEK_SET : how == FILE_CURRENT ? SEEK_CUR : SEEK_END);
    if (r < 0) { set_errno_error(); return FALSE; }
    if (pos) pos->QuadPart = r;
    return TRUE;
}
DWORD SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD how) {
    LARGE_INTEGER d, p; d.QuadPart = hi ? (((int64_t)*hi << 32) | (uint32_t)lo) : lo;
    if (!SetFilePointerEx(h, d, &p, how)) return INVALID_SET_FILE_POINTER;
    if (hi) *hi = p.HighPart;
    return p.LowPart;
}
BOOL GetFileSizeEx(HANDLE h, PLARGE_INTEGER size) { struct stat st; if (fstat(fd_of(h), &st)) { set_errno_error(); return FALSE; } size->QuadPart = st.st_size; return TRUE; }
DWORD GetFileSize(HANDLE h, LPDWORD hi) { LARGE_INTEGER s; if (!GetFileSizeEx(h, &s)) return INVALID_FILE_SIZE; if (hi) *hi = (DWORD)s.HighPart; return s.LowPart; }
BOOL SetEndOfFile(HANDLE h) { int fd = fd_of(h); off_t p = lseek(fd, 0, SEEK_CUR); return p >= 0 && ftruncate(fd, p) == 0; }
BOOL FlushFileBuffers(HANDLE h) { return fsync(fd_of(h)) == 0; }
static void st_times(const struct stat *st, LPFILETIME c, LPFILETIME a, LPFILETIME w) {
    if (c) ft_set(c, (int64_t)st->st_ctim.tv_sec * 10000000LL + st->st_ctim.tv_nsec / 100 + EPOCH_DIFF_100NS);
    if (a) ft_set(a, (int64_t)st->st_atim.tv_sec * 10000000LL + st->st_atim.tv_nsec / 100 + EPOCH_DIFF_100NS);
    if (w) ft_set(w, (int64_t)st->st_mtim.tv_sec * 10000000LL + st->st_mtim.tv_nsec / 100 + EPOCH_DIFF_100NS);
}
BOOL GetFileTime(HANDLE h, LPFILETIME c, LPFILETIME a, LPFILETIME w) { struct stat st; if (fstat(fd_of(h), &st)) return FALSE; st_times(&st, c, a, w); return TRUE; }
static DWORD st_attr(const struct stat *st) { return S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE; }
BOOL GetFileInformationByHandle(HANDLE h, LPBY_HANDLE_FILE_INFORMATION info) {
    struct stat st; if (fstat(fd_of(h), &st)) return FALSE;
    memset(info, 0, sizeof *info);
    info->dwFileAttributes = st_attr(&st);
    st_times(&st, &info->ftCreationTime, &info->ftLastAccessTime, &info->ftLastWriteTime);
    info->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32); info->nFileSizeLow = (DWORD)st.st_size;
    info->nNumberOfLinks = (DWORD)st.st_nlink; info->dwVolumeSerialNumber = (DWORD)st.st_dev;
    info->nFileIndexHigh = (DWORD)((uint64_t)st.st_ino >> 32); info->nFileIndexLow = (DWORD)st.st_ino;
    return TRUE;
}
DWORD GetFileType(HANDLE h) { struct stat st; if (fstat(fd_of(h), &st)) return FILE_TYPE_UNKNOWN; return S_ISCHR(st.st_mode) ? FILE_TYPE_CHAR : FILE_TYPE_DISK; }
DWORD GetFinalPathNameByHandleA(HANDLE h, LPSTR out, DWORD n, DWORD flags) {
    (void)flags;
    Obj *o = obj_of(h);
    if (!o || !o->path) return 0;
    char tmp[PATH_MAX];
    const char *p = realpath(o->path, tmp) ? tmp : o->path;
    snprintf(out, n, "%s", p); to_backslash(out);
    return (DWORD)strlen(out);
}
HANDLE GetStdHandle(DWORD which) { return (HANDLE)(intptr_t)(int32_t)which; }
DWORD GetFileAttributesA(LPCSTR path) { PATHBUF; struct stat st; if (stat(P(path), &st)) { set_errno_error(); return INVALID_FILE_ATTRIBUTES; } return st_attr(&st); }
DWORD GetFileAttributesW(LPCWSTR path) { char p[MAX_PATH * 4]; w2u(path, p, sizeof p); return GetFileAttributesA(p); }
BOOL SetFileAttributesA(LPCSTR path, DWORD attr) { (void)attr; return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID out) {
    (void)level;
    PATHBUF; struct stat st;
    if (stat(P(path), &st)) { set_errno_error(); return FALSE; }
    WIN32_FILE_ATTRIBUTE_DATA *d = out;
    d->dwFileAttributes = st_attr(&st);
    st_times(&st, &d->ftCreationTime, &d->ftLastAccessTime, &d->ftLastWriteTime);
    d->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32); d->nFileSizeLow = (DWORD)st.st_size;
    return TRUE;
}
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID out) { char p[MAX_PATH * 4]; w2u(path, p, sizeof p); return GetFileAttributesExA(p, level, out); }
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa) { (void)sa; PATHBUF; if (mkdir(P(path), 0777)) { set_errno_error(); return FALSE; } return TRUE; }
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES sa) { char p[MAX_PATH * 4]; w2u(path, p, sizeof p); return CreateDirectoryA(p, sa); }
BOOL RemoveDirectoryA(LPCSTR path) { PATHBUF; if (rmdir(P(path))) { set_errno_error(); return FALSE; } return TRUE; }
BOOL DeleteFileA(LPCSTR path) { PATHBUF; if (unlink(P(path))) { set_errno_error(); return FALSE; } return TRUE; }
BOOL DeleteFileW(LPCWSTR path) { char p[MAX_PATH * 4]; w2u(path, p, sizeof p); return DeleteFileA(p); }
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags) {
    char a[MAX_PATH * 4], b[MAX_PATH * 4];
    compat_path(from, a, sizeof a); compat_path(to, b, sizeof b);
    struct stat st;
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && stat(b, &st) == 0) { t_last_error = ERROR_ALREADY_EXISTS; return FALSE; }
    if (rename(a, b)) { set_errno_error(); return FALSE; }
    return TRUE;
}
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) { char a[MAX_PATH * 4], b[MAX_PATH * 4]; w2u(from, a, sizeof a); w2u(to, b, sizeof b); return MoveFileExA(a, b, flags); }
BOOL MoveFileA(LPCSTR from, LPCSTR to) { return MoveFileExA(from, to, 0); }
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists) {
    char a[MAX_PATH * 4], b[MAX_PATH * 4];
    compat_path(from, a, sizeof a); compat_path(to, b, sizeof b);
    int in = open(a, O_RDONLY | O_CLOEXEC);
    if (in < 0) { set_errno_error(); return FALSE; }
    int out = open(b, O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0666);
    if (out < 0) { set_errno_error(); close(in); return FALSE; }
    char buf[1 << 16]; ssize_t r; BOOL ok = TRUE;
    while ((r = read(in, buf, sizeof buf)) > 0) if (write(out, buf, (size_t)r) != r) { ok = FALSE; break; }
    if (r < 0) ok = FALSE;
    close(in); close(out);
    return ok;
}
BOOL ReplaceFileA(LPCSTR repl, LPCSTR with, LPCSTR backup, DWORD flags, LPVOID a, LPVOID b) { (void)flags; (void)a; (void)b; if (backup) CopyFileA(repl, backup, FALSE); return MoveFileExA(with, repl, MOVEFILE_REPLACE_EXISTING); }

/* FindFirstFile: "<folder>\<pattern>"; case-insensitive like NTFS */
static BOOL find_next(Obj *o, LPWIN32_FIND_DATAA fd) {
    struct dirent *e;
    while (o->dir && (e = readdir(o->dir))) {
        if (fnmatch(o->pattern, e->d_name, FNM_CASEFOLD | FNM_PERIOD * 0) != 0) {
            if (!(strcmp(o->pattern, "*.*") == 0)) continue;     /* "*.*" matches every name on Windows */
        }
        memset(fd, 0, sizeof *fd);
        snprintf(fd->cFileName, sizeof fd->cFileName, "%s", e->d_name);
        char full[MAX_PATH * 3];
        snprintf(full, sizeof full, "%s/%s", o->dirpath, e->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            fd->dwFileAttributes = st_attr(&st);
            st_times(&st, &fd->ftCreationTime, &fd->ftLastAccessTime, &fd->ftLastWriteTime);
            fd->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32); fd->nFileSizeLow = (DWORD)st.st_size;
        } else fd->dwFileAttributes = e->d_type == DT_DIR ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
        return TRUE;
    }
    t_last_error = ERROR_NO_MORE_FILES;
    return FALSE;
}
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA fd) {
    PATHBUF;
    const char *q = P(pattern);
    const char *slash = strrchr(q, '/');
    Obj *o = obj_new(H_FIND);
    if (slash) { snprintf(o->dirpath, sizeof o->dirpath, "%.*s", (int)(slash - q), q); snprintf(o->pattern, sizeof o->pattern, "%s", slash + 1); }
    else { snprintf(o->dirpath, sizeof o->dirpath, "."); snprintf(o->pattern, sizeof o->pattern, "%s", q); }
    if (!o->dirpath[0]) snprintf(o->dirpath, sizeof o->dirpath, "/");
    o->dir = opendir(o->dirpath);
    if (!o->dir || !find_next(o, fd)) {
        if (o->dir) closedir(o->dir);
        t_last_error = o->dir ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
        free(o);
        return INVALID_HANDLE_VALUE;
    }
    return o;
}
BOOL FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd) { Obj *o = obj_of(h); return o && o->type == H_FIND ? find_next(o, fd) : FALSE; }
static void fd_a2w(const WIN32_FIND_DATAA *a, LPWIN32_FIND_DATAW w) {
    memset(w, 0, sizeof *w);
    w->dwFileAttributes = a->dwFileAttributes; w->ftCreationTime = a->ftCreationTime; w->ftLastAccessTime = a->ftLastAccessTime; w->ftLastWriteTime = a->ftLastWriteTime;
    w->nFileSizeHigh = a->nFileSizeHigh; w->nFileSizeLow = a->nFileSizeLow;
    MultiByteToWideChar(CP_UTF8, 0, a->cFileName, -1, w->cFileName, MAX_PATH);
}
HANDLE FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW fd) {
    char p[MAX_PATH * 4]; w2u(pattern, p, sizeof p);
    WIN32_FIND_DATAA a; HANDLE h = FindFirstFileA(p, &a);
    if (h != INVALID_HANDLE_VALUE) fd_a2w(&a, fd);
    return h;
}
BOOL FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW fd) { WIN32_FIND_DATAA a; if (!FindNextFileA(h, &a)) return FALSE; fd_a2w(&a, fd); return TRUE; }
BOOL FindClose(HANDLE h) { return CloseHandle(h); }
DWORD GetFullPathNameA(LPCSTR path, DWORD n, LPSTR out, LPSTR *file_part) {
    char tmp[MAX_PATH * 4];
    compat_fullpath(tmp, path, sizeof tmp);
    DWORD len = (DWORD)strlen(tmp);
    if (len + 1 > n) return len + 1;
    memcpy(out, tmp, len + 1);
    if (file_part) { char *s = strrchr(out, '\\'); *file_part = s ? s + 1 : out; }
    return len;
}
DWORD GetCurrentDirectoryA(DWORD n, LPSTR out) { char tmp[PATH_MAX]; if (!getcwd(tmp, sizeof tmp)) return 0; to_backslash(tmp); DWORD len = (DWORD)strlen(tmp); if (len + 1 > n) return len + 1; memcpy(out, tmp, len + 1); return len; }
BOOL SetCurrentDirectoryA(LPCSTR path) { return compat_chdir(path) == 0; }
DWORD GetTempPathA(DWORD n, LPSTR out) {
    const char *t = getenv("TMPDIR");
    if (!t || !t[0]) t = "/data/local/tmp";
    int k = snprintf(out, n, "%s\\", t);
    to_backslash(out);
    return (DWORD)k;
}
DWORD GetShortPathNameW(LPCWSTR in, LPWSTR out, DWORD n) { (void)in; (void)out; (void)n; return 0; }      /* no 8.3 names */
DWORD GetShortPathNameA(LPCSTR in, LPSTR out, DWORD n) { (void)in; (void)out; (void)n; return 0; }
UINT GetTempFileNameA(LPCSTR dir, LPCSTR prefix, UINT unique, LPSTR out) {
    static volatile int32_t ctr;
    UINT u = unique ? unique : (UINT)(getpid() * 1000 + __atomic_add_fetch(&ctr, 1, __ATOMIC_RELAXED)) & 0xffff;
    snprintf(out, MAX_PATH, "%s\\%.3s%04X.tmp", dir, prefix ? prefix : "", u);
    if (!unique) { HANDLE h = CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    return u;
}
BOOL GetDiskFreeSpaceExA(LPCSTR path, PULARGE_INTEGER avail, PULARGE_INTEGER total, PULARGE_INTEGER free_) {
    PATHBUF; struct statvfs s;
    if (statvfs(P(path ? path : "."), &s)) return FALSE;
    if (avail) avail->QuadPart = (ULONGLONG)s.f_bavail * s.f_frsize;
    if (total) total->QuadPart = (ULONGLONG)s.f_blocks * s.f_frsize;
    if (free_) free_->QuadPart = (ULONGLONG)s.f_bfree * s.f_frsize;
    return TRUE;
}
HANDLE CreateFileMappingA(HANDLE f, LPSECURITY_ATTRIBUTES sa, DWORD prot, DWORD hi, DWORD lo, LPCSTR name) {
    (void)sa; (void)prot; (void)name;
    Obj *src = obj_of(f), *o = obj_new(H_MAPPING);
    o->fd = src && src->type == H_FILE ? dup(src->fd) : -1;
    o->map_size = ((size_t)hi << 32) | lo;
    return o;
}
LPVOID MapViewOfFileEx(HANDLE m, DWORD access, DWORD hi, DWORD lo, SIZE_T n, LPVOID addr) {
    Obj *o = obj_of(m);
    if (!o || o->type != H_MAPPING) return NULL;
    size_t len = n ? n : o->map_size;
    if (!len && o->fd >= 0) { struct stat st; fstat(o->fd, &st); len = (size_t)st.st_size - (((size_t)hi << 32) | lo); }
    int prot = (access & FILE_MAP_WRITE) ? PROT_READ | PROT_WRITE : PROT_READ;
    int flags = (access & FILE_MAP_COPY) ? MAP_PRIVATE : (o->fd >= 0 ? MAP_SHARED : MAP_SHARED | MAP_ANONYMOUS);
    if (addr) flags |= MAP_FIXED_NOREPLACE;
    void *p = mmap(addr, len, prot, flags, o->fd, (off_t)(((uint64_t)hi << 32) | lo));
    return p == MAP_FAILED ? NULL : p;
}
LPVOID MapViewOfFile(HANDLE m, DWORD access, DWORD hi, DWORD lo, SIZE_T n) { return MapViewOfFileEx(m, access, hi, lo, n, NULL); }
BOOL UnmapViewOfFile(LPCVOID p) {
    uintptr_t lo, hi, next; int prot;
    if (!maps_query((uintptr_t)p, &lo, &hi, &prot, &next)) return FALSE;
    return munmap((void *)p, hi - (uintptr_t)p) == 0;
}
/* shlwapi */
BOOL PathFileExistsA(LPCSTR p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }
BOOL PathIsDirectoryA(LPCSTR p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
BOOL PathRemoveFileSpecA(LPSTR p) { char *s = strrchr(p, '\\'), *t = strrchr(p, '/'); if (t > s) s = t; if (!s) return FALSE; *s = 0; return TRUE; }
LPSTR PathFindFileNameA(LPCSTR p) { const char *s = strrchr(p, '\\'), *t = strrchr(p, '/'); if (t > s) s = t; return (LPSTR)(s ? s + 1 : p); }
LPSTR PathFindExtensionA(LPCSTR p) { const char *f = PathFindFileNameA(p), *d = strrchr(f, '.'); return (LPSTR)(d ? d : f + strlen(f)); }
BOOL PathAppendA(LPSTR p, LPCSTR more) { size_t n = strlen(p); if (n && p[n - 1] != '\\') strcat(p, "\\"); strcat(p, more); return TRUE; }
LPSTR StrStrIA(LPCSTR a, LPCSTR b) { return strcasestr(a, b); }

/* ================================================================ modules
 * HMODULE is the load address, as on Windows. LoadLibraryA takes Windows names ("ssbu_game_eh_o1.dll"): the file of that
 * name in SSBU_LIBDIR (the app's private copy of the game libraries: Android does not run code from shared storage) is
 * used when there is one, else the name as given. */
static int exe_path(char *out, size_t n);
typedef struct { uintptr_t base; void *dl; char path[MAX_PATH * 2]; } Mod;
static Mod g_mods[256];
static int g_nmods;
static pthread_mutex_t g_mod_mu = PTHREAD_MUTEX_INITIALIZER;
typedef struct { const char *path; uintptr_t base; } FindMod;
static int find_mod_cb(struct dl_phdr_info *info, size_t sz, void *p) {
    (void)sz;
    FindMod *f = p;
    if (!info->dlpi_name || !info->dlpi_name[0]) return 0;
    const char *a = strrchr(info->dlpi_name, '/'), *b = strrchr(f->path, '/');
    if (!strcmp(info->dlpi_name, f->path) || (a && b && !strcmp(a, b))) {
        uintptr_t lo = UINTPTR_MAX;
        for (int i = 0; i < info->dlpi_phnum; i++) if (info->dlpi_phdr[i].p_type == PT_LOAD && info->dlpi_addr + info->dlpi_phdr[i].p_vaddr < lo) lo = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
        f->base = lo;
        return 1;
    }
    return 0;
}
static Mod *mod_by_base(uintptr_t b) { for (int i = 0; i < g_nmods; i++) if (g_mods[i].base == b) return &g_mods[i]; return NULL; }
static uintptr_t self_base(void) { Dl_info di; return dladdr((void *)self_base, &di) ? (uintptr_t)di.dli_fbase : 0; }
static void *self_dl(void) {
    static void *h;
    if (!h) { Dl_info di; if (dladdr((void *)self_base, &di)) h = dlopen(di.dli_fname, RTLD_NOW | RTLD_NOLOAD); if (!h) h = RTLD_DEFAULT; }
    return h;
}
HMODULE LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags) {
    (void)f; (void)flags;
    if (!name) return NULL;
    const char *base = name;
    for (const char *s = name; *s; s++) if (*s == '\\' || *s == '/') base = s + 1;
    char path[MAX_PATH * 2];
    const char *libdir = getenv("SSBU_LIBDIR");
    struct stat st;
    path[0] = 0;
    if (libdir && libdir[0]) {
        /* the game libraries keep their place below the exe's folder (nro_bin_eh_o2\*.dll, fighters_bin\*.dll): the
         * launcher copies that tree into SSBU_LIBDIR */
        char rel[MAX_PATH * 2], exe[MAX_PATH * 2];
        compat_path(name, rel, sizeof rel);
        exe_path(exe, sizeof exe);
        compat_path(exe, exe, sizeof exe);
        char *slash = strrchr(exe, '/');
        if (slash) slash[1] = 0;
        const char *r = rel;
        if (slash && !strncmp(rel, exe, strlen(exe))) r = rel + strlen(exe);
        while (r[0] == '.' && r[1] == '/') r += 2;
        if (r[0] != '/') { snprintf(path, sizeof path, "%s/%s", libdir, r); if (stat(path, &st)) path[0] = 0; }
        if (!path[0]) { snprintf(path, sizeof path, "%s/%s", libdir, base); if (stat(path, &st)) path[0] = 0; }
    }
    if (!path[0]) compat_path(name, path, sizeof path);
    static const char *const sys[][2] = {          /* Windows DLLs and their Android libraries (in the APK) */
        {"SDL3.dll", "libSDL3.so"}, {"vulkan-1.dll", "libvulkan.so"}, {"ssbu_glslang.dll", "libssbu_glslang.so"},
        {"opus.dll", "libopus.so"}, {"libopus-0.dll", "libopus.so"},
    };
    for (unsigned i = 0; i < sizeof sys / sizeof sys[0]; i++) if (!strcasecmp(base, sys[i][0])) snprintf(path, sizeof path, "%s", sys[i][1]);
    static const char *const absent[] = {"gdi32.dll", "user32.dll", "kernel32.dll", "ntdll.dll", "dbghelp.dll", "opengl32.dll", "dwmapi.dll", "winmm.dll", "shcore.dll"};
    for (unsigned i = 0; i < sizeof absent / sizeof absent[0]; i++) if (!strcasecmp(base, absent[i])) { t_last_error = ERROR_FILE_NOT_FOUND; return NULL; }
    void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        __android_log_print(ANDROID_LOG_WARN, "OpenSmash", "LoadLibrary(%s): %s", name, dlerror());
        t_last_error = ERROR_FILE_NOT_FOUND;
        return NULL;
    }
    FindMod fm = {path, 0};
    dl_iterate_phdr(find_mod_cb, &fm);
    if (!fm.base) { void *sym = dlsym(h, "_init"); Dl_info di; if (sym && dladdr(sym, &di)) fm.base = (uintptr_t)di.dli_fbase; }
    if (!fm.base) fm.base = (uintptr_t)h;
    pthread_mutex_lock(&g_mod_mu);
    Mod *m = mod_by_base(fm.base);
    if (!m && g_nmods < (int)(sizeof g_mods / sizeof g_mods[0])) { m = &g_mods[g_nmods++]; m->base = fm.base; m->dl = h; snprintf(m->path, sizeof m->path, "%s", path); }
    pthread_mutex_unlock(&g_mod_mu);
    return (HMODULE)fm.base;
}
HMODULE LoadLibraryA(LPCSTR name) { return LoadLibraryExA(name, NULL, 0); }
HMODULE LoadLibraryW(LPCWSTR name) { char p[MAX_PATH * 4]; w2u(name, p, sizeof p); return LoadLibraryA(p); }
HMODULE LoadLibraryExW(LPCWSTR name, HANDLE f, DWORD flags) { char p[MAX_PATH * 4]; w2u(name, p, sizeof p); return LoadLibraryExA(p, f, flags); }
BOOL FreeLibrary(HMODULE m) {
    pthread_mutex_lock(&g_mod_mu);
    Mod *x = mod_by_base((uintptr_t)m);
    void *dl = x ? x->dl : NULL;
    pthread_mutex_unlock(&g_mod_mu);
    if (dl) dlclose(dl);
    return TRUE;
}
FARPROC GetProcAddress(HMODULE m, LPCSTR name) {
    if ((uintptr_t)name < 0x10000) return NULL;               /* by ordinal: no */
    void *dl = NULL;
    if (!m || (uintptr_t)m == self_base()) dl = self_dl();
    else { pthread_mutex_lock(&g_mod_mu); Mod *x = mod_by_base((uintptr_t)m); dl = x ? x->dl : NULL; pthread_mutex_unlock(&g_mod_mu); }
    if (!dl) return NULL;
    return (FARPROC)dlsym(dl, name);
}
HMODULE GetModuleHandleA(LPCSTR name) {
    if (!name) return (HMODULE)self_base();
    const char *base = name;
    for (const char *s = name; *s; s++) if (*s == '\\' || *s == '/') base = s + 1;
    pthread_mutex_lock(&g_mod_mu);
    for (int i = 0; i < g_nmods; i++) {
        const char *b = strrchr(g_mods[i].path, '/');
        if (!strcasecmp(b ? b + 1 : g_mods[i].path, base)) { HMODULE r = (HMODULE)g_mods[i].base; pthread_mutex_unlock(&g_mod_mu); return r; }
    }
    pthread_mutex_unlock(&g_mod_mu);
    return NULL;                                             /* system DLLs (ntdll, kernel32, ...) do not exist here */
}
HMODULE GetModuleHandleW(LPCWSTR name) { if (!name) return GetModuleHandleA(NULL); char p[MAX_PATH * 4]; w2u(name, p, sizeof p); return GetModuleHandleA(p); }
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *out) {
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        Dl_info di;
        if (!dladdr(name, &di) || !di.dli_fbase) { *out = NULL; return FALSE; }
        *out = (HMODULE)di.dli_fbase;
        return TRUE;
    }
    *out = GetModuleHandleA(name);
    return *out != NULL;
}
BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *out) {
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) return GetModuleHandleExA(flags, (LPCSTR)name, out);
    *out = GetModuleHandleW(name); return *out != NULL;
}
/* the "exe": SSBU_EXE (set by the Android launcher to <game folder>\work\ssbu_native.exe), with '\' separators */
static int exe_path(char *out, size_t n) {
    const char *e = getenv("SSBU_EXE");
    if (e && e[0]) snprintf(out, n, "%s", e);
    else { char cwd[PATH_MAX]; if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0; snprintf(out, n, "%s/ssbu_native.exe", cwd); }
    to_backslash(out);
    return (int)strlen(out);
}
DWORD GetModuleFileNameA(HMODULE m, LPSTR out, DWORD n) {
    char tmp[MAX_PATH * 4];
    if (!m || (uintptr_t)m == self_base()) exe_path(tmp, sizeof tmp);
    else {
        pthread_mutex_lock(&g_mod_mu);
        Mod *x = mod_by_base((uintptr_t)m);
        if (x) snprintf(tmp, sizeof tmp, "%s", x->path);
        pthread_mutex_unlock(&g_mod_mu);
        if (!x) { Dl_info di; if (dladdr((void *)m, &di) && di.dli_fname) snprintf(tmp, sizeof tmp, "%s", di.dli_fname); else { t_last_error = ERROR_INVALID_HANDLE; return 0; } }
        to_backslash(tmp);
    }
    DWORD len = (DWORD)strlen(tmp);
    if (!n) return 0;
    if (len >= n) { memcpy(out, tmp, n - 1); out[n - 1] = 0; t_last_error = ERROR_INSUFFICIENT_BUFFER; return n; }
    memcpy(out, tmp, len + 1);
    return len;
}
DWORD GetModuleFileNameW(HMODULE m, LPWSTR out, DWORD n) { char tmp[MAX_PATH * 4]; DWORD k = GetModuleFileNameA(m, tmp, sizeof tmp); if (!k) return 0; return (DWORD)MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, (int)n) - 1; }
BOOL SetDllDirectoryA(LPCSTR p) { (void)p; return TRUE; }
BOOL SetDefaultDllDirectories(DWORD f) { (void)f; return TRUE; }
void *AddDllDirectory(PCWSTR p) { (void)p; return (void *)1; }
BOOL QueryFullProcessImageNameA(HANDLE h, DWORD flags, LPSTR out, PDWORD n) { (void)h; (void)flags; *n = GetModuleFileNameA(NULL, out, *n); return *n != 0; }
/* psapi / toolhelp */
typedef struct { HMODULE *mods; DWORD max, n; } EnumMods;
static int enum_mods_cb(struct dl_phdr_info *info, size_t sz, void *p) {
    (void)sz;
    EnumMods *e = p;
    uintptr_t lo = UINTPTR_MAX;
    for (int i = 0; i < info->dlpi_phnum; i++) if (info->dlpi_phdr[i].p_type == PT_LOAD && info->dlpi_addr + info->dlpi_phdr[i].p_vaddr < lo) lo = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
    if (lo == UINTPTR_MAX) return 0;
    if (e->n < e->max) e->mods[e->n] = (HMODULE)lo;
    e->n++;
    return 0;
}
BOOL K32EnumProcessModules(HANDLE p, HMODULE *mods, DWORD cb, LPDWORD need) {
    (void)p;
    EnumMods e = {mods, cb / (DWORD)sizeof(HMODULE), 0};
    dl_iterate_phdr(enum_mods_cb, &e);
    if (need) *need = e.n * (DWORD)sizeof(HMODULE);
    return TRUE;
}
typedef struct { uintptr_t base; size_t size; char name[MAX_PATH]; int found; } ModInfo;
static int mod_info_cb(struct dl_phdr_info *info, size_t sz, void *p) {
    (void)sz;
    ModInfo *m = p;
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) if (info->dlpi_phdr[i].p_type == PT_LOAD) {
        uintptr_t a = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr, b = a + info->dlpi_phdr[i].p_memsz;
        if (a < lo) lo = a;
        if (b > hi) hi = b;
    }
    if (lo != m->base) return 0;
    m->size = hi - lo; m->found = 1;
    snprintf(m->name, sizeof m->name, "%s", info->dlpi_name ? info->dlpi_name : "");
    return 1;
}
BOOL K32GetModuleInformation(HANDLE p, HMODULE h, LPMODULEINFO mi, DWORD cb) {
    (void)p; (void)cb;
    ModInfo m = {(uintptr_t)h, 0, "", 0};
    dl_iterate_phdr(mod_info_cb, &m);
    if (!m.found) return FALSE;
    mi->lpBaseOfDll = (void *)m.base; mi->SizeOfImage = (DWORD)m.size; mi->EntryPoint = NULL;
    return TRUE;
}
DWORD K32GetModuleBaseNameA(HANDLE p, HMODULE h, LPSTR out, DWORD n) {
    (void)p;
    char tmp[MAX_PATH * 4];
    if (!GetModuleFileNameA(h, tmp, sizeof tmp)) return 0;
    const char *b = strrchr(tmp, '\\');
    snprintf(out, n, "%s", b ? b + 1 : tmp);
    return (DWORD)strlen(out);
}
BOOL K32GetProcessMemoryInfo(HANDLE p, PPROCESS_MEMORY_COUNTERS c, DWORD cb) {
    (void)p;
    memset(c, 0, cb);
    c->cb = cb;
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return FALSE;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned long kb;
        if (sscanf(line, "VmRSS: %lu", &kb) == 1) c->WorkingSetSize = (SIZE_T)kb * 1024;
        else if (sscanf(line, "VmHWM: %lu", &kb) == 1) c->PeakWorkingSetSize = (SIZE_T)kb * 1024;
        else if (sscanf(line, "VmData: %lu", &kb) == 1) { c->PagefileUsage = (SIZE_T)kb * 1024; if (cb >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) ((PROCESS_MEMORY_COUNTERS_EX *)c)->PrivateUsage = (SIZE_T)kb * 1024; }
    }
    fclose(f);
    c->PeakPagefileUsage = c->PagefileUsage;
    return TRUE;
}
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid) {
    (void)pid;
    Obj *o = obj_new(H_SNAP);
    if (flags & TH32CS_SNAPTHREAD) {
        DIR *d = opendir("/proc/self/task");
        struct dirent *e;
        int cap = 64;
        o->tids = malloc(cap * sizeof *o->tids);
        while (d && (e = readdir(d))) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
            if (o->ntids == cap) { cap *= 2; o->tids = realloc(o->tids, cap * sizeof *o->tids); }
            o->tids[o->ntids++] = (DWORD)atoi(e->d_name);
        }
        if (d) closedir(d);
    }
    if (flags & (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32)) {
        HMODULE mods[512]; DWORD need = 0;
        K32EnumProcessModules(NULL, mods, sizeof mods, &need);
        int n = (int)(need / sizeof(HMODULE)); if (n > 512) n = 512;
        o->mods = calloc((size_t)n + 1, sizeof *o->mods);
        for (int i = 0; i < n; i++) {
            ModInfo m = {(uintptr_t)mods[i], 0, "", 0};
            dl_iterate_phdr(mod_info_cb, &m);
            if (!m.found) continue;
            o->mods[o->nmods].base = m.base; o->mods[o->nmods].size = m.size;
            snprintf(o->mods[o->nmods].name, MAX_PATH, "%s", m.name);
            o->nmods++;
        }
    }
    return o;
}
BOOL Thread32First(HANDLE s, LPTHREADENTRY32 te) { Obj *o = obj_of(s); if (!o) return FALSE; o->tpos = 0; return Thread32Next(s, te); }
BOOL Thread32Next(HANDLE s, LPTHREADENTRY32 te) {
    Obj *o = obj_of(s);
    if (!o || o->tpos >= o->ntids) { t_last_error = ERROR_NO_MORE_FILES; return FALSE; }
    te->th32ThreadID = o->tids[o->tpos++]; te->th32OwnerProcessID = (DWORD)getpid(); te->cntUsage = 0; te->tpBasePri = 8; te->tpDeltaPri = 0; te->dwFlags = 0;
    return TRUE;
}
BOOL Module32First(HANDLE s, LPMODULEENTRY32 me) { Obj *o = obj_of(s); if (!o) return FALSE; o->mpos = 0; return Module32Next(s, me); }
BOOL Module32Next(HANDLE s, LPMODULEENTRY32 me) {
    Obj *o = obj_of(s);
    if (!o || o->mpos >= o->nmods) { t_last_error = ERROR_NO_MORE_FILES; return FALSE; }
    int i = o->mpos++;
    me->modBaseAddr = (BYTE *)o->mods[i].base; me->modBaseSize = (DWORD)o->mods[i].size; me->hModule = (HMODULE)o->mods[i].base;
    me->th32ProcessID = (DWORD)getpid();
    snprintf(me->szExePath, sizeof me->szExePath, "%s", o->mods[i].name);
    const char *b = strrchr(o->mods[i].name, '/');
    snprintf(me->szModule, sizeof me->szModule, "%s", b ? b + 1 : o->mods[i].name);
    return TRUE;
}

/* ================================================================ environment, strings */
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR out, DWORD n) {
    const char *v = getenv(name);
    if (!v) { t_last_error = 203; return 0; }                /* ERROR_ENVVAR_NOT_FOUND */
    DWORD len = (DWORD)strlen(v);
    if (len + 1 > n) return len + 1;
    memcpy(out, v, len + 1);
    return len;
}
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) { return (value ? setenv(name, value, 1) : unsetenv(name)) == 0; }
int compat_putenv(const char *s) {
    const char *eq = strchr(s, '=');
    if (!eq) return -1;
    char name[256]; snprintf(name, sizeof name, "%.*s", (int)(eq - s), s);
    return eq[1] ? setenv(name, eq + 1, 1) : unsetenv(name);
}
int compat_putenv_s(const char *n, const char *v) { return v && v[0] ? setenv(n, v, 1) : unsetenv(n); }
int compat_dupenv_s(char **out, size_t *len, const char *name) { const char *v = getenv(name); *out = v ? strdup(v) : NULL; if (len) *len = v ? strlen(v) + 1 : 0; return 0; }
char *compat_strlwr(char *s) { for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p); return s; }
char *compat_strupr(char *s) { for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p); return s; }
int compat_strncpy_s(char *d, size_t n, const char *s, size_t c) {
    if (!d || !n) return EINVAL;
    size_t k = strlen(s);
    if (c != (size_t)-1 && c < k) k = c;
    if (k >= n) k = n - 1;
    memcpy(d, s, k); d[k] = 0;
    return 0;
}
int compat_strcat_s(char *d, size_t n, const char *s) { size_t a = strlen(d); return compat_strncpy_s(d + a, n > a ? n - a : 0, s, (size_t)-1); }
char *compat_lstrcpyn(char *d, const char *s, int n) { if (n <= 0) return d; compat_strncpy_s(d, (size_t)n, s, (size_t)-1); return d; }
size_t compat_wcslen(const WCHAR *s) { size_t n = 0; while (s[n]) n++; return n; }
int compat_wcscmp(const WCHAR *a, const WCHAR *b) { while (*a && *a == *b) a++, b++; return (int)*a - (int)*b; }
int compat_wcsncmp(const WCHAR *a, const WCHAR *b, size_t n) { for (; n; n--, a++, b++) { if (*a != *b) return (int)*a - (int)*b; if (!*a) return 0; } return 0; }
static WCHAR wlow(WCHAR c) { return c >= 'A' && c <= 'Z' ? (WCHAR)(c + 32) : c; }
int compat_wcsicmp(const WCHAR *a, const WCHAR *b) { while (*a && wlow(*a) == wlow(*b)) a++, b++; return (int)wlow(*a) - (int)wlow(*b); }
WCHAR *compat_wcscpy(WCHAR *d, const WCHAR *s) { WCHAR *r = d; while ((*d++ = *s++)) {} return r; }
WCHAR *compat_wcsncpy(WCHAR *d, const WCHAR *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
WCHAR *compat_wcscat(WCHAR *d, const WCHAR *s) { compat_wcscpy(d + compat_wcslen(d), s); return d; }
WCHAR *compat_wcschr(const WCHAR *s, WCHAR c) { for (;; s++) { if (*s == c) return (WCHAR *)s; if (!*s) return NULL; } }
WCHAR *compat_wcsrchr(const WCHAR *s, WCHAR c) { const WCHAR *r = NULL; for (;; s++) { if (*s == c) r = s; if (!*s) return (WCHAR *)r; } }
WCHAR *compat_wcsstr(const WCHAR *s, const WCHAR *t) { size_t n = compat_wcslen(t); if (!n) return (WCHAR *)s; for (; *s; s++) if (!compat_wcsncmp(s, t, n)) return (WCHAR *)s; return NULL; }
WCHAR *compat_wcsdup(const WCHAR *s) { size_t n = (compat_wcslen(s) + 1) * sizeof(WCHAR); WCHAR *d = malloc(n); if (d) memcpy(d, s, n); return d; }
long compat_wcstol(const WCHAR *s, WCHAR **end, int base) {
    char tmp[64]; size_t i = 0;
    for (; s[i] && s[i] < 128 && i < sizeof tmp - 1; i++) tmp[i] = (char)s[i];
    tmp[i] = 0;
    char *e; long v = strtol(tmp, &e, base);
    if (end) *end = (WCHAR *)(s + (e - tmp));
    return v;
}
/* _snwprintf: the format is converted to UTF-8, %s / %ls take WCHAR strings (Windows' wide printf), %hs / %S narrow ones */
int compat_snwprintf(WCHAR *out, size_t n, const WCHAR *fmt, ...) {
    char f8[1024], o8[4096]; size_t k = 0;
    for (size_t i = 0; fmt[i] && k < sizeof f8 - 4; i++) {
        if (fmt[i] != '%') { k += (size_t)WideCharToMultiByte(CP_UTF8, 0, &fmt[i], 1, f8 + k, (int)(sizeof f8 - k), NULL, NULL); continue; }
        f8[k++] = '%';
        while (fmt[i + 1] && strchr("-+ #0123456789.lhIzjt*", (int)fmt[i + 1]) && k < sizeof f8 - 8) {
            if (fmt[i + 1] == 'l' && fmt[i + 2] == 's') { i++; continue; }
            if (fmt[i + 1] == 'h' && fmt[i + 2] == 's') { i++; f8[k++] = '\x01'; continue; }   /* narrow marker */
            if (fmt[i + 1] == 'I' && fmt[i + 2] == '6' && fmt[i + 3] == '4') { i += 3; f8[k++] = 'l'; f8[k++] = 'l'; continue; }
            f8[k++] = (char)fmt[++i];
        }
        if (fmt[i + 1]) { WCHAR c = fmt[++i]; f8[k++] = c == 'S' ? '\x01' : (char)c; if (c == 'S') f8[k++] = 's'; }
    }
    f8[k] = 0;
    /* format piece by piece so that %s can convert its argument */
    va_list ap; va_start(ap, fmt);
    size_t o = 0; const char *p = f8;
    while (*p && o < sizeof o8 - 1) {
        if (*p != '%') { o8[o++] = *p++; continue; }
        const char *s = p++;
        while (*p && !strchr("diouxXeEfgGcspn%", *p)) p++;
        if (!*p) break;
        char spec[64]; int narrow = 0; size_t sl = 0;
        for (const char *q = s; q <= p && sl < sizeof spec - 1; q++) { if (*q == '\x01') { narrow = 1; continue; } spec[sl++] = *q; }
        spec[sl] = 0;
        char c = *p++;
        int r = 0;
        if (c == 's') {
            const void *arg = va_arg(ap, const void *);
            char tmp[2048];
            if (!arg) snprintf(tmp, sizeof tmp, "(null)");
            else if (narrow) snprintf(tmp, sizeof tmp, "%s", (const char *)arg);
            else WideCharToMultiByte(CP_UTF8, 0, (const WCHAR *)arg, -1, tmp, sizeof tmp, NULL, NULL);
            r = snprintf(o8 + o, sizeof o8 - o, spec, tmp);
        } else if (c == 'c') r = snprintf(o8 + o, sizeof o8 - o, "%c", (char)va_arg(ap, int));
        else if (c == '%') r = snprintf(o8 + o, sizeof o8 - o, "%%");
        else if (strchr("eEfgG", c)) r = snprintf(o8 + o, sizeof o8 - o, spec, va_arg(ap, double));
        else if (strstr(spec, "ll") || strchr(spec, 'z') || strchr(spec, 'j') || c == 'p') r = snprintf(o8 + o, sizeof o8 - o, spec, va_arg(ap, long long));
        else r = snprintf(o8 + o, sizeof o8 - o, spec, va_arg(ap, int));
        if (r > 0) o += (size_t)r < sizeof o8 - o ? (size_t)r : sizeof o8 - o - 1;
    }
    va_end(ap);
    o8[o] = 0;
    int w = MultiByteToWideChar(CP_UTF8, 0, o8, -1, out, (int)n);
    if (w <= 0) { if (n) out[n - 1] = 0; return -1; }
    return w - 1;
}
int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR s, int n, LPWSTR out, int m) {
    (void)cp; (void)flags;
    const unsigned char *p = (const unsigned char *)s;
    size_t len = n < 0 ? strlen(s) + 1 : (size_t)n;
    int k = 0;
    for (size_t i = 0; i < len;) {
        uint32_t c = p[i];
        int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + (size_t)extra >= len + (extra ? 0 : 1)) { c = 0xFFFD; i++; }
        else {
            if (extra) c &= 0x3f >> extra;
            for (int j = 1; j <= extra; j++) c = (c << 6) | (p[i + j] & 0x3f);
            i += (size_t)extra + 1;
        }
        if (c >= 0x10000) {
            if (m) { if (k + 2 > m) { t_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; } c -= 0x10000; out[k] = (WCHAR)(0xD800 + (c >> 10)); out[k + 1] = (WCHAR)(0xDC00 + (c & 0x3ff)); }
            k += 2;
        } else {
            if (m) { if (k + 1 > m) { t_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; } out[k] = (WCHAR)c; }
            k++;
        }
    }
    return k;
}
int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR s, int n, LPSTR out, int m, LPCSTR def, BOOL *used) {
    (void)cp; (void)flags; (void)def;
    if (used) *used = FALSE;
    size_t len = n < 0 ? compat_wcslen(s) + 1 : (size_t)n;
    int k = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++; }
        char b[4]; int nb;
        if (c < 0x80) { b[0] = (char)c; nb = 1; }
        else if (c < 0x800) { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 0x3f)); nb = 2; }
        else if (c < 0x10000) { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 0x3f)); b[2] = (char)(0x80 | (c & 0x3f)); nb = 3; }
        else { b[0] = (char)(0xF0 | (c >> 18)); b[1] = (char)(0x80 | ((c >> 12) & 0x3f)); b[2] = (char)(0x80 | ((c >> 6) & 0x3f)); b[3] = (char)(0x80 | (c & 0x3f)); nb = 4; }
        if (m) { if (k + nb > m) { t_last_error = ERROR_INSUFFICIENT_BUFFER; return 0; } memcpy(out + k, b, (size_t)nb); }
        k += nb;
    }
    return k;
}
BOOL GetUserNameW(LPWSTR out, LPDWORD n) { const char *u = getenv("SSBU_USER_NAME"); if (!u || !u[0]) u = "Player"; int k = MultiByteToWideChar(CP_UTF8, 0, u, -1, out, (int)*n); if (k <= 0) return FALSE; *n = (DWORD)k; return TRUE; }
BOOL GetUserNameA(LPSTR out, LPDWORD n) { const char *u = getenv("SSBU_USER_NAME"); if (!u || !u[0]) u = "Player"; snprintf(out, *n, "%s", u); *n = (DWORD)strlen(out) + 1; return TRUE; }
BOOL GetComputerNameA(LPSTR out, LPDWORD n) { snprintf(out, *n, "android"); *n = (DWORD)strlen(out); return TRUE; }
int GetUserDefaultGeoName(LPWSTR out, int n) { const char *r = getenv("SSBU_REGION"); if (!r || !r[0]) r = "US"; return MultiByteToWideChar(CP_UTF8, 0, r, -1, out, n); }
int GetUserDefaultLocaleName(LPWSTR out, int n) { const char *r = getenv("SSBU_LOCALE"); if (!r || !r[0]) r = "en-US"; return MultiByteToWideChar(CP_UTF8, 0, r, -1, out, n); }
LCID GetUserDefaultLCID(void) { return 0x409; }
WORD GetUserDefaultUILanguage(void) { return 0x409; }
void OutputDebugStringA(LPCSTR s) { __android_log_write(ANDROID_LOG_DEBUG, "OpenSmash", s); }
void OutputDebugStringW(LPCWSTR s) { char t[2048]; WideCharToMultiByte(CP_UTF8, 0, s, -1, t, sizeof t, NULL, NULL); OutputDebugStringA(t); }
BOOL Beep(DWORD f, DWORD d) { (void)f; (void)d; return TRUE; }
UINT SetErrorMode(UINT m) { (void)m; return 0; }
int __argc;
char **__argv;
LPSTR GetCommandLineA(void) { static char c[] = "ssbu_native.exe"; return c; }
DWORD FormatMessageA(DWORD flags, LPCVOID src, DWORD id, DWORD lang, LPSTR out, DWORD n, va_list *args) {
    (void)src; (void)lang; (void)args;
    const char *m = id >= 0x1000 ? strerror((int)(id - 0x1000)) : id == ERROR_FILE_NOT_FOUND ? "The system cannot find the file specified." : id == ERROR_ACCESS_DENIED ? "Access is denied." : "Error";
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) { *(char **)out = strdup(m); return (DWORD)strlen(m); }
    snprintf(out, n, "%s", m);
    return (DWORD)strlen(out);
}
HLOCAL LocalFree(HLOCAL p) { free(p); return NULL; }
BOOL SetConsoleOutputCP(UINT cp) { (void)cp; return TRUE; }
BOOL SetConsoleCP(UINT cp) { (void)cp; return TRUE; }
BOOL SetConsoleTitleA(LPCSTR t) { (void)t; return TRUE; }
BOOL AllocConsole(void) { return FALSE; }
BOOL AttachConsole(DWORD pid) { (void)pid; return FALSE; }
HWND GetConsoleWindow(void) { return NULL; }
BOOLEAN SystemFunction036(PVOID buf, ULONG n) {
    uint8_t *p = buf;
    while (n) { ssize_t r = getrandom(p, n, 0); if (r <= 0) { if (errno == EINTR) continue; return FALSE; } p += r; n -= (ULONG)r; }
    return TRUE;
}
BOOL CryptAcquireContextA(HCRYPTPROV *p, LPCSTR c, LPCSTR prov, DWORD type, DWORD flags) { (void)c; (void)prov; (void)type; (void)flags; *p = 1; return TRUE; }
BOOL CryptAcquireContextW(HCRYPTPROV *p, LPCWSTR c, LPCWSTR prov, DWORD type, DWORD flags) { (void)c; (void)prov; (void)type; (void)flags; *p = 1; return TRUE; }
BOOL CryptGenRandom(HCRYPTPROV p, DWORD n, BYTE *buf) { (void)p; return SystemFunction036(buf, n); }
BOOL CryptReleaseContext(HCRYPTPROV p, DWORD flags) { (void)p; (void)flags; return TRUE; }
BOOL SetProcessInformation(HANDLE h, PROCESS_INFORMATION_CLASS c, LPVOID info, DWORD n) { (void)h; (void)c; (void)info; (void)n; return TRUE; }
BOOL GetSystemCpuSetInformation(PSYSTEM_CPU_SET_INFORMATION info, ULONG n, PULONG ret, HANDLE proc, ULONG flags) {
    (void)proc; (void)flags;
    long ncpu = sysconf(_SC_NPROCESSORS_CONF);
    if (ncpu < 1) ncpu = 1;
    ULONG need = (ULONG)ncpu * (ULONG)sizeof(SYSTEM_CPU_SET_INFORMATION);
    if (ret) *ret = need;
    if (!info || n < need) { t_last_error = ERROR_INSUFFICIENT_BUFFER; return FALSE; }
    /* efficiency class from the maximum frequency of each core (big.LITTLE): higher = faster, as on Windows */
    long fmax[64] = {0}, lo = LONG_MAX;
    for (int i = 0; i < ncpu && i < 64; i++) {
        char path[128]; snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        FILE *f = fopen(path, "r"); if (f) { if (fscanf(f, "%ld", &fmax[i]) != 1) fmax[i] = 0; fclose(f); }
        if (fmax[i] && fmax[i] < lo) lo = fmax[i];
    }
    for (int i = 0; i < ncpu; i++) {
        SYSTEM_CPU_SET_INFORMATION *s = &info[i];
        memset(s, 0, sizeof *s);
        s->Size = sizeof *s; s->Type = CpuSetInformation;
        s->CpuSet.Id = 0x100 + (DWORD)i; s->CpuSet.LogicalProcessorIndex = (BYTE)i; s->CpuSet.CoreIndex = (BYTE)i;
        s->CpuSet.EfficiencyClass = (BYTE)(i < 64 && fmax[i] > lo ? (fmax[i] > lo * 3 / 2 ? 2 : 1) : 0);
    }
    return TRUE;
}
BOOL SetThreadSelectedCpuSets(HANDLE h, const ULONG *ids, ULONG n) {
    DWORD_PTR m = 0;
    for (ULONG i = 0; i < n; i++) if (ids[i] >= 0x100 && ids[i] < 0x140) m |= (DWORD_PTR)1 << (ids[i] - 0x100);
    if (!n) { SYSTEM_INFO si; GetSystemInfo(&si); m = si.dwActiveProcessorMask; }
    SetThreadAffinityMask(h, m);
    return TRUE;
}
static void tick_ft(LPFILETIME ft, int64_t ns) { ft_set(ft, ns / 100); }
BOOL GetThreadTimes(HANDLE h, LPFILETIME c, LPFILETIME e, LPFILETIME k, LPFILETIME u) {
    pid_t t = tid_of(h);
    memset(c, 0, sizeof *c); memset(e, 0, sizeof *e);
    char path[64]; snprintf(path, sizeof path, "/proc/self/task/%d/stat", (int)t);
    FILE *f = fopen(path, "r");
    unsigned long ut = 0, st = 0;
    if (f) {
        char buf[1024]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
        char *p = strrchr(buf, ')');
        if (p) sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st);
    }
    long hz = sysconf(_SC_CLK_TCK); if (hz <= 0) hz = 100;
    tick_ft(k, (int64_t)st * 1000000000LL / hz); tick_ft(u, (int64_t)ut * 1000000000LL / hz);
    return f != NULL;
}
BOOL GetSystemTimes(LPFILETIME idle, LPFILETIME kernel, LPFILETIME user) {
    /* /proc/stat "cpu" line, in clock ticks; Windows counts idle time inside kernel time */
    unsigned long long u = 0, n = 0, s = 0, i = 0, w = 0, q = 0, sq = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (f) { if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &i, &w, &q, &sq) < 4) u = n = s = i = 0; fclose(f); }
    long hz = sysconf(_SC_CLK_TCK); if (hz <= 0) hz = 100;
    int64_t k = 10000000LL / hz;
    if (idle) ft_set(idle, (int64_t)(i + w) * k);
    if (kernel) ft_set(kernel, (int64_t)(s + q + sq + i + w) * k);
    if (user) ft_set(user, (int64_t)(u + n) * k);
    return f != NULL;
}
BOOL GetProcessTimes(HANDLE h, LPFILETIME c, LPFILETIME e, LPFILETIME k, LPFILETIME u) {
    (void)h;
    struct rusage r; getrusage(RUSAGE_SELF, &r);
    memset(c, 0, sizeof *c); memset(e, 0, sizeof *e);
    tick_ft(k, (int64_t)r.ru_stime.tv_sec * 1000000000LL + r.ru_stime.tv_usec * 1000LL);
    tick_ft(u, (int64_t)r.ru_utime.tv_sec * 1000000000LL + r.ru_utime.tv_usec * 1000LL);
    return TRUE;
}
BOOL QueryThreadCycleTime(HANDLE h, PULONG64 cycles) {
    if (h == PSEUDO_THREAD) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); *cycles = (ULONG64)t.tv_sec * 1000000000ull + (ULONG64)t.tv_nsec; return TRUE; }
    FILETIME c, e, k, u; if (!GetThreadTimes(h, &c, &e, &k, &u)) return FALSE;
    *cycles = (ULONG64)(ft_get(&k) + ft_get(&u)) * 100;
    return TRUE;
}
BOOL ReadProcessMemory(HANDLE h, LPCVOID addr, LPVOID buf, SIZE_T n, SIZE_T *got) {
    (void)h;
    /* a read that must not fault: process_vm_readv on ourselves */
    struct iovec l = {buf, n}, r = {(void *)addr, n};
    ssize_t k = syscall(__NR_process_vm_readv, getpid(), &l, 1, &r, 1, 0);
    if (got) *got = k > 0 ? (SIZE_T)k : 0;
    return k == (ssize_t)n;
}
BOOL WriteProcessMemory(HANDLE h, LPVOID addr, LPCVOID buf, SIZE_T n, SIZE_T *put) {
    (void)h;
    struct iovec l = {(void *)buf, n}, r = {addr, n};
    ssize_t k = syscall(__NR_process_vm_writev, getpid(), &l, 1, &r, 1, 0);
    if (put) *put = k > 0 ? (SIZE_T)k : 0;
    return k == (ssize_t)n;
}

/* ================================================================ exceptions: signals */
static LPTOP_LEVEL_EXCEPTION_FILTER g_filter;
static PVECTORED_EXCEPTION_HANDLER g_veh[8];
static int g_nveh;
static __thread int t_in_handler;
static void altstack_setup(void) {
    stack_t ss = {0};
    ss.ss_size = 256 * 1024;
    ss.ss_sp = mmap(NULL, ss.ss_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ss.ss_sp != MAP_FAILED) sigaltstack(&ss, NULL);
}
static struct sigaction g_old_sa[32];
static void ctx_from_uc(PCONTEXT c, const ucontext_t *uc) {
    memset(c, 0, sizeof *c);
    const struct sigcontext *m = &uc->uc_mcontext;
    for (int i = 0; i < 31; i++) c->X[i] = m->regs[i];
    c->Sp = m->sp; c->Pc = m->pc; c->Pstate = m->pstate;
    c->Rip = m->pc; c->Rsp = m->sp; c->Rbp = m->regs[29];
    c->Rax = m->regs[0]; c->Rcx = m->regs[1]; c->Rdx = m->regs[2]; c->Rbx = m->regs[3];
    c->Rsi = m->regs[4]; c->Rdi = m->regs[5]; c->R8 = m->regs[8]; c->R9 = m->regs[9];
    c->R10 = m->regs[10]; c->R11 = m->regs[11]; c->R12 = m->regs[19]; c->R13 = m->regs[20]; c->R14 = m->regs[21]; c->R15 = m->regs[30];
}
static void crash_handler(int sig, siginfo_t *si, void *ucv) {
    ucontext_t *uc = ucv;
    if (!t_in_handler) {
        t_in_handler = 1;
        EXCEPTION_RECORD er = {0}; CONTEXT cx; EXCEPTION_POINTERS ep = {&er, &cx};
        ctx_from_uc(&cx, uc);
        er.ExceptionAddress = (void *)(uintptr_t)cx.Pc;
        switch (sig) {
        case SIGSEGV: case SIGBUS: er.ExceptionCode = EXCEPTION_ACCESS_VIOLATION; er.NumberParameters = 2; er.ExceptionInformation[0] = 0; er.ExceptionInformation[1] = (ULONG_PTR)si->si_addr; break;
        case SIGILL: er.ExceptionCode = EXCEPTION_ILLEGAL_INSTRUCTION; break;
        case SIGFPE: er.ExceptionCode = EXCEPTION_INT_DIVIDE_BY_ZERO; break;
        case SIGTRAP: er.ExceptionCode = EXCEPTION_BREAKPOINT; break;
        default: er.ExceptionCode = 0xE0000000u | (DWORD)sig; break;
        }
        for (int i = 0; i < g_nveh; i++) if (g_veh[i] && g_veh[i](&ep) == EXCEPTION_CONTINUE_EXECUTION) { t_in_handler = 0; return; }
        __android_log_print(ANDROID_LOG_FATAL, "OpenSmash", "signal %d at pc %p (fault address %p)", sig, (void *)(uintptr_t)cx.Pc, si->si_addr);
        if (g_filter) {
            LONG r = g_filter(&ep);
            if (r == EXCEPTION_CONTINUE_EXECUTION) { t_in_handler = 0; return; }
        }
        fflush(stdout); fflush(stderr);
    }
    /* hand the signal on (debuggerd writes the tombstone), then die */
    sigaction(sig, &g_old_sa[sig], NULL);
    raise(sig);
}
static void install_signals(void) {
    static int done;
    if (done) return;
    done = 1;
    altstack_setup();
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP};
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, &g_old_sa[sigs[i]]);
}
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f) { LPTOP_LEVEL_EXCEPTION_FILTER o = g_filter; g_filter = f; install_signals(); return o; }
PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h) {
    (void)first;
    install_signals();
    if (g_nveh < 8) { g_veh[g_nveh++] = h; return (PVOID)h; }
    return NULL;
}
ULONG RemoveVectoredExceptionHandler(PVOID h) { for (int i = 0; i < g_nveh; i++) if ((PVOID)g_veh[i] == h) g_veh[i] = NULL; return 1; }
PVOID AddVectoredContinueHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h) { (void)first; (void)h; return (PVOID)1; }
void RaiseException(DWORD code, DWORD flags, DWORD n, const ULONG_PTR *args) {
    (void)flags;
    EXCEPTION_RECORD er = {0}; CONTEXT cx; EXCEPTION_POINTERS ep = {&er, &cx};
    RtlCaptureContext(&cx);
    er.ExceptionCode = code; er.ExceptionAddress = __builtin_return_address(0);
    er.NumberParameters = n > EXCEPTION_MAXIMUM_PARAMETERS ? EXCEPTION_MAXIMUM_PARAMETERS : n;
    for (DWORD i = 0; i < er.NumberParameters; i++) er.ExceptionInformation[i] = args[i];
    for (int i = 0; i < g_nveh; i++) if (g_veh[i]) g_veh[i](&ep);
    if (g_filter) g_filter(&ep);
    fflush(stdout); fflush(stderr);
    _exit(0xff);
}
BOOL GetThreadContext(HANDLE h, PCONTEXT ctx) {
    if (h == PSEUDO_THREAD || tid_of(h) == gettid()) { RtlCaptureContext(ctx); return TRUE; }
    t_last_error = ERROR_NOT_SUPPORTED;
    return FALSE;
}
BOOL SetThreadContext(HANDLE h, const CONTEXT *ctx) { (void)h; (void)ctx; t_last_error = ERROR_NOT_SUPPORTED; return FALSE; }
void RtlCaptureContext(PCONTEXT c) {
    memset(c, 0, sizeof *c);
    c->Pc = c->Rip = (DWORD64)(uintptr_t)__builtin_return_address(0);
    c->Sp = c->Rsp = (DWORD64)(uintptr_t)__builtin_frame_address(0);
    c->X[29] = c->Rbp = (DWORD64)(uintptr_t)__builtin_frame_address(0);
}
/* no Windows unwind tables: the stack walks that use them stop at once */
PRUNTIME_FUNCTION RtlLookupFunctionEntry(DWORD64 pc, PDWORD64 base, PVOID table) { (void)pc; (void)table; if (base) *base = 0; return NULL; }
PVOID RtlVirtualUnwind(DWORD type, DWORD64 base, DWORD64 pc, PRUNTIME_FUNCTION f, PCONTEXT ctx, PVOID *hd, PDWORD64 est, PVOID ptrs) { (void)type; (void)base; (void)pc; (void)f; (void)hd; (void)est; (void)ptrs; ctx->Rip = 0; return NULL; }
WORD RtlCaptureStackBackTrace(DWORD skip, DWORD n, PVOID *out, PDWORD hash) {
    /* frame-pointer walk (the build keeps frame pointers: -fno-omit-frame-pointer) */
    uintptr_t *fp = __builtin_frame_address(0);
    WORD k = 0;
    for (DWORD i = 0; fp && k < n && i < 256; i++) {
        uintptr_t next = fp[0], ret = fp[1];
        if (!ret) break;
        if (i >= skip) out[k++] = (PVOID)ret;
        if (next <= (uintptr_t)fp) break;
        fp = (uintptr_t *)next;
    }
    if (hash) *hash = 0;
    return k;
}

BOOL CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
    (void)app; (void)cmd; (void)pa; (void)ta; (void)inherit; (void)flags; (void)env; (void)dir; (void)si;
    memset(pi, 0, sizeof *pi);
    t_last_error = ERROR_NOT_SUPPORTED;
    return FALSE;
}
DWORD SetThreadExecutionState(DWORD f) { (void)f; return ES_CONTINUOUS; }
BOOL IsBadReadPtr(const void *p, UINT_PTR n) { char tmp[256]; for (UINT_PTR o = 0; o < n; o += sizeof tmp) { SIZE_T k = n - o < sizeof tmp ? n - o : sizeof tmp; if (!ReadProcessMemory(NULL, (const char *)p + o, tmp, k, NULL)) return TRUE; } return FALSE; }
BOOL IsBadWritePtr(void *p, UINT_PTR n) { MEMORY_BASIC_INFORMATION m; if (!VirtualQuery(p, &m, sizeof m)) return TRUE; return m.State != MEM_COMMIT || !(m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (uintptr_t)p + n > (uintptr_t)m.BaseAddress + m.RegionSize; }

/* ================================================================ user interface stand-ins */
static volatile uint8_t g_keys[256];
void compat_set_key(int vk, int down) { if (vk > 0 && vk < 256) g_keys[vk] = (uint8_t)(down ? 1 : 0); }
void keybd_event(BYTE vk, BYTE scan, DWORD flags, ULONG_PTR extra) { (void)scan; (void)extra; compat_set_key(vk, !(flags & KEYEVENTF_KEYUP)); }
SHORT GetAsyncKeyState(int vk) {
    if (vk <= 0 || vk >= 256) return 0;
    int d = g_keys[vk];
    if (vk == VK_SHIFT) d = g_keys[VK_LSHIFT] || g_keys[VK_RSHIFT] || g_keys[VK_SHIFT];
    if (vk == VK_CONTROL) d = g_keys[VK_LCONTROL] || g_keys[VK_RCONTROL] || g_keys[VK_CONTROL];
    if (vk == VK_MENU) d = g_keys[VK_LMENU] || g_keys[VK_RMENU] || g_keys[VK_MENU];
    return d ? (SHORT)0x8000 : 0;
}
SHORT GetKeyState(int vk) { return GetAsyncKeyState(vk); }
static volatile int g_focus = 1;
void compat_set_focus(int f) { g_focus = f; }
HWND GetForegroundWindow(void) { return g_focus ? (HWND)1 : NULL; }
BOOL SetForegroundWindow(HWND w) { (void)w; return TRUE; }
DWORD GetWindowThreadProcessId(HWND w, LPDWORD pid) { (void)w; if (pid) *pid = (DWORD)getpid(); return (DWORD)gettid(); }
BOOL SetWindowTextA(HWND w, LPCSTR t) { (void)w; (void)t; return TRUE; }
BOOL PostMessageA(HWND w, UINT m, WPARAM a, LPARAM b) { (void)w; (void)m; (void)a; (void)b; return TRUE; }
BOOL ShowWindow(HWND w, int cmd) { (void)w; (void)cmd; return TRUE; }
BOOL IsWindow(HWND w) { return w != NULL; }
BOOL IsIconic(HWND w) { (void)w; return FALSE; }
BOOL GetClientRect(HWND w, LPRECT r) { (void)w; r->left = r->top = 0; r->right = 1280; r->bottom = 720; return TRUE; }
BOOL GetWindowRect(HWND w, LPRECT r) { return GetClientRect(w, r); }
int ShowCursor(BOOL s) { (void)s; return 0; }
BOOL EnumWindows(WNDENUMPROC fn, LPARAM arg) { if (fn) fn((HWND)1, arg); return TRUE; }
BOOL IsWindowVisible(HWND w) { return w != NULL && g_focus; }
HWND GetWindow(HWND w, UINT cmd) { (void)w; (void)cmd; return NULL; }
BOOL GetUserPreferredUILanguages(DWORD flags, PULONG n, PWSTR buf, PULONG size) { (void)flags; (void)n; (void)buf; (void)size; return FALSE; }
int MessageBoxA(HWND w, LPCSTR text, LPCSTR title, UINT type) {
    (void)w;
    __android_log_print(ANDROID_LOG_ERROR, "OpenSmash", "%s: %s", title ? title : "", text ? text : "");
    printf("\n[%s]\n%s\n", title ? title : "", text ? text : "");
    fflush(stdout);
    void compat_message_box(const char *title, const char *text);       /* android_main.c: shown in the activity */
    compat_message_box(title, text);
    return (type & 0xf) == MB_YESNO ? IDNO : IDOK;
}
int MessageBoxW(HWND w, LPCWSTR text, LPCWSTR title, UINT type) {
    char a[4096], b[512];
    WideCharToMultiByte(CP_UTF8, 0, text ? text : L"", -1, a, sizeof a, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, title ? title : L"", -1, b, sizeof b, NULL, NULL);
    return MessageBoxA(w, a, b, type);
}
HINSTANCE ShellExecuteA(HWND w, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show) { (void)w; (void)op; (void)params; (void)dir; (void)show; __android_log_print(ANDROID_LOG_INFO, "OpenSmash", "open: %s", file ? file : ""); return (HINSTANCE)2; }
HINSTANCE ShellExecuteW(HWND w, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show) { (void)w; (void)op; (void)file; (void)params; (void)dir; (void)show; return (HINSTANCE)2; }
/* registry */
LSTATUS RegOpenKeyExA(HKEY k, LPCSTR sub, DWORD opt, DWORD sam, HKEY *out) { (void)k; (void)sub; (void)opt; (void)sam; *out = NULL; return ERROR_FILE_NOT_FOUND; }
LSTATUS RegOpenKeyExW(HKEY k, LPCWSTR sub, DWORD opt, DWORD sam, HKEY *out) { (void)k; (void)sub; (void)opt; (void)sam; *out = NULL; return ERROR_FILE_NOT_FOUND; }
LSTATUS RegQueryValueExA(HKEY k, LPCSTR name, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD n) { (void)k; (void)name; (void)res; (void)type; (void)data; (void)n; return ERROR_FILE_NOT_FOUND; }
LSTATUS RegQueryValueExW(HKEY k, LPCWSTR name, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD n) { (void)k; (void)name; (void)res; (void)type; (void)data; (void)n; return ERROR_FILE_NOT_FOUND; }
LSTATUS RegGetValueA(HKEY k, LPCSTR sub, LPCSTR name, DWORD flags, LPDWORD type, PVOID data, LPDWORD n) { (void)k; (void)sub; (void)name; (void)flags; (void)type; (void)data; (void)n; return ERROR_FILE_NOT_FOUND; }
LSTATUS RegGetValueW(HKEY k, LPCWSTR sub, LPCWSTR name, DWORD flags, LPDWORD type, PVOID data, LPDWORD n) { (void)k; (void)sub; (void)name; (void)flags; (void)type; (void)data; (void)n; return ERROR_FILE_NOT_FOUND; }
LSTATUS RegEnumKeyExW(HKEY k, DWORD i, LPWSTR name, LPDWORD n, LPDWORD res, LPWSTR cls, LPDWORD cn, PFILETIME t) { (void)k; (void)i; (void)name; (void)n; (void)res; (void)cls; (void)cn; (void)t; return ERROR_NO_MORE_FILES; }
LSTATUS RegCloseKey(HKEY k) { (void)k; return 0; }
LSTATUS SHGetValueA(HKEY k, LPCSTR sub, LPCSTR name, LPDWORD type, LPVOID data, LPDWORD n) { (void)k; (void)sub; (void)name; (void)type; (void)data; (void)n; return ERROR_FILE_NOT_FOUND; }
LSTATUS SHGetValueW(HKEY k, LPCWSTR sub, LPCWSTR name, LPDWORD type, LPVOID data, LPDWORD n) { (void)k; (void)sub; (void)name; (void)type; (void)data; (void)n; return ERROR_FILE_NOT_FOUND; }
HRESULT CoInitializeEx(LPVOID r, DWORD f) { (void)r; (void)f; return S_OK; }
HRESULT CoInitialize(LPVOID r) { (void)r; return S_OK; }
void CoUninitialize(void) {}
void CoTaskMemFree(LPVOID p) { free(p); }

/* ================================================================ winsock */
int WSAStartup(WORD v, LPWSADATA d) { if (d) { memset(d, 0, sizeof *d); d->wVersion = d->wHighVersion = v; } signal(SIGPIPE, SIG_IGN); return 0; }
int WSACleanup(void) { return 0; }
int WSAGetLastError(void) { return errno; }
void WSASetLastError(int e) { errno = e; }
int WSAIoctl(SOCKET s, DWORD code, LPVOID in, DWORD inn, LPVOID out, DWORD outn, LPDWORD got, LPOVERLAPPED ov, void *cb) {
    (void)s; (void)in; (void)inn; (void)out; (void)outn; (void)ov; (void)cb;
    if (got) *got = 0;
    if (code == SIO_UDP_CONNRESET) return 0;                  /* Linux does not report ICMP resets on unconnected UDP sockets */
    errno = EOPNOTSUPP;
    return -1;
}
int ioctlsocket(SOCKET s, long cmd, u_long *arg) {
    if ((unsigned long)cmd == 0x8004667Eul || cmd == FIONBIO) { int fl = fcntl(s, F_GETFL); return fcntl(s, F_SETFL, *arg ? fl | O_NONBLOCK : fl & ~O_NONBLOCK); }
    if ((unsigned long)cmd == 0x4004667Ful || cmd == FIONREAD) { int n = 0; int r = ioctl(s, FIONREAD, &n); *arg = (u_long)n; return r; }
    errno = EINVAL;
    return -1;
}
#undef setsockopt
#undef getsockopt
int compat_setsockopt(int s, int level, int name, const void *v, socklen_t n) {
    if (level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO) && n == sizeof(DWORD)) {
        DWORD ms = *(const DWORD *)v;
        struct timeval tv = {(time_t)(ms / 1000), (suseconds_t)(ms % 1000) * 1000};
        return setsockopt(s, level, name, &tv, sizeof tv);
    }
    if (level == SOL_SOCKET && name == SO_NOSIGPIPE) return 0;
    return setsockopt(s, level, name, v, n);
}
int compat_getsockopt(int s, int level, int name, void *v, socklen_t *n) {
    if (level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO) && *n == sizeof(DWORD)) {
        struct timeval tv; socklen_t k = sizeof tv;
        int r = getsockopt(s, level, name, &tv, &k);
        *(DWORD *)v = (DWORD)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
        return r;
    }
    return getsockopt(s, level, name, v, n);
}

/* ================================================================ for r1_state.c (rollback): writable segments of a loaded ELF
 * module, page-rounded, given its load address (the HMODULE). Returns the bytes added. */
typedef struct { uintptr_t base; void (*add)(uint8_t *, uint8_t *); size_t bytes; } WSeg;
static int wseg_cb(struct dl_phdr_info *info, size_t sz, void *p) {
    (void)sz;
    WSeg *w = p;
    uintptr_t lo = UINTPTR_MAX;
    for (int i = 0; i < info->dlpi_phnum; i++) if (info->dlpi_phdr[i].p_type == PT_LOAD && info->dlpi_addr + info->dlpi_phdr[i].p_vaddr < lo) lo = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
    if (lo != w->base) return 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_W)) continue;
        uintptr_t a = (info->dlpi_addr + ph->p_vaddr) & ~(PAGE - 1), b = (info->dlpi_addr + ph->p_vaddr + ph->p_memsz + PAGE - 1) & ~(PAGE - 1);
        w->add((uint8_t *)a, (uint8_t *)b);
        w->bytes += b - a;
    }
    return 1;
}
size_t android_writable_segments(uint8_t *base, void (*add)(uint8_t *, uint8_t *)) {
    WSeg w = {(uintptr_t)base, add, 0};
    dl_iterate_phdr(wseg_cb, &w);
    return w.bytes;
}
