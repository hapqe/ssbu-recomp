/* windows.h for the Android port: the part of the Win32 API the runtime uses, on POSIX / bionic.
 *
 * The runtime was written for Windows. Rather than fork 50 source files, the Android build compiles them unchanged
 * where it can, against this header (implementations in win32_compat.c), and puts an #ifdef __ANDROID__ only where a
 * Windows-only subsystem (windows, GDI, Media Foundation, the debugger API) has no direct equivalent.
 *
 * Conventions kept from Windows so that the sources need no edits:
 *   - the data model is LLP64 where the API says so: LONG / DWORD / ULONG are 32 bits (the build passes -fshort-wchar,
 *     so wchar_t / WCHAR are 16 bits as on Windows, and L"..." literals match WCHAR);
 *   - paths may use '\' and are turned into '/' by every file function here (and by the fopen / remove / rename
 *     wrappers below); GetModuleFileNameA returns the "exe" path with '\' separators, so code that cuts the file name
 *     off at the last '\' keeps working;
 *   - HANDLEs are pointers to objects of win32_compat.c (threads, events, waitable timers, mutexes, files, searches).
 */
#ifndef ANDROID_COMPAT_WINDOWS_H
#define ANDROID_COMPAT_WINDOWS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- keywords */
#define WINAPI
#define APIENTRY
#define CALLBACK
#define NTAPI
#define WINAPIV
#define __stdcall
#define __cdecl
#define __fastcall
#define __forceinline inline __attribute__((always_inline))
#define __declspec(x) __declspec_##x
#define __declspec_thread __thread
#define __declspec_dllexport __attribute__((visibility("default")))
#define __declspec_dllimport
#define __declspec_noinline __attribute__((noinline))
#define __declspec_noreturn __attribute__((noreturn))
#define __declspec_selectany __attribute__((weak))
#define __declspec_align(n) __attribute__((aligned(n)))
#define _In_
#define _In_opt_
#define _Out_
#define _Out_opt_
#define _Inout_
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#define UNREFERENCED_PARAMETER(p) ((void)(p))

/* ---------------------------------------------------------------- types (LLP64) */
typedef int BOOL, *PBOOL, *LPBOOL;
typedef uint8_t BOOLEAN;
typedef uint8_t BYTE, UCHAR, *PBYTE, *LPBYTE;
typedef char CHAR, CCHAR;
typedef int16_t SHORT;
typedef uint16_t WORD, USHORT, *PWORD;
typedef int32_t INT, LONG, *PLONG, *LPLONG, HRESULT, NTSTATUS, INT32;
typedef uint32_t UINT, DWORD, ULONG, *PDWORD, *LPDWORD, *PULONG, UINT32, DWORD32;
typedef int64_t LONGLONG, LONG64, INT64, *PLONG64;
typedef uint64_t ULONGLONG, ULONG64, DWORD64, UINT64, *PULONG64, *PDWORD64;
typedef intptr_t INT_PTR, LONG_PTR, SSIZE_T;
typedef uintptr_t UINT_PTR, ULONG_PTR, DWORD_PTR, *PDWORD_PTR, *PULONG_PTR;
typedef size_t SIZE_T, *PSIZE_T;
typedef float FLOAT;
typedef wchar_t WCHAR, *PWSTR, *LPWSTR;
typedef const wchar_t *PCWSTR, *LPCWSTR;
typedef char *PSTR, *LPSTR;
typedef const char *PCSTR, *LPCSTR;
typedef void *PVOID, *LPVOID;
typedef const void *LPCVOID, *PCVOID;
typedef void *HANDLE, **PHANDLE, **LPHANDLE;
typedef void *HMODULE, *HINSTANCE, *HWND, *HDC, *HFONT, *HBRUSH, *HBITMAP, *HGDIOBJ, *HPEN, *HMONITOR, *HICON, *HCURSOR,
    *HMENU, *HKEY, *HGLRC, *HRGN, *HGLOBAL, *HLOCAL, *FARPROC_PTR;
typedef intptr_t (*FARPROC)(void);
typedef LONG_PTR LRESULT, LPARAM;
typedef UINT_PTR WPARAM;
typedef DWORD COLORREF;
typedef WORD ATOM;
typedef DWORD LCID;
typedef UINT MMRESULT;
typedef LONG KPRIORITY;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w) ((BYTE)((DWORD_PTR)(w) >> 8))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_NOTIMPL ((HRESULT)0x80004001)
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#if !defined(NOMINMAX) && !defined(__cplusplus)
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#endif

typedef union _LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; }; struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER { struct { DWORD LowPart; DWORD HighPart; }; struct { DWORD LowPart; DWORD HighPart; } u; ULONGLONG QuadPart; } ULARGE_INTEGER, *PULARGE_INTEGER;
typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;
typedef struct _RECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef struct _POINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct _SIZE { LONG cx, cy; } SIZE, *PSIZE, *LPSIZE;
typedef struct _LUID { DWORD LowPart; LONG HighPart; } LUID, *PLUID;
typedef struct _GUID { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } GUID, IID, CLSID;
typedef struct _SECURITY_ATTRIBUTES { DWORD nLength; LPVOID lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef struct _OVERLAPPED { ULONG_PTR Internal, InternalHigh; union { struct { DWORD Offset, OffsetHigh; }; PVOID Pointer; }; HANDLE hEvent; } OVERLAPPED, *LPOVERLAPPED;

/* ---------------------------------------------------------------- errors */
#define ERROR_SUCCESS 0
#define NO_ERROR 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_NO_MORE_FILES 18
#define ERROR_HANDLE_EOF 38
#define ERROR_NOT_SUPPORTED 50
#define ERROR_FILE_EXISTS 80
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_ALREADY_EXISTS 183
#define ERROR_DEVICE_NOT_CONNECTED 1167
#define ERROR_INVALID_ADDRESS 487
#define ERROR_TIMEOUT 1460
DWORD GetLastError(void);
void SetLastError(DWORD e);

/* ---------------------------------------------------------------- memory */
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
#define MEM_FREE 0x10000
#define MEM_PRIVATE 0x20000
#define MEM_MAPPED 0x40000
#define MEM_RESET 0x80000
#define MEM_TOP_DOWN 0x100000
#define MEM_WRITE_WATCH 0x200000
#define MEM_LARGE_PAGES 0x20000000
#define MEM_IMAGE 0x1000000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define PAGE_NOCACHE 0x200
typedef struct _MEMORY_BASIC_INFORMATION { PVOID BaseAddress, AllocationBase; DWORD AllocationProtect; WORD PartitionId; SIZE_T RegionSize; DWORD State, Protect, Type; } MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;
LPVOID VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD prot);
BOOL VirtualFree(LPVOID addr, SIZE_T size, DWORD type);
BOOL VirtualProtect(LPVOID addr, SIZE_T size, DWORD prot, PDWORD old);
SIZE_T VirtualQuery(LPCVOID addr, PMEMORY_BASIC_INFORMATION mbi, SIZE_T len);
UINT GetWriteWatch(DWORD flags, PVOID base, SIZE_T size, PVOID *addrs, ULONG_PTR *count, ULONG *granularity);
UINT ResetWriteWatch(LPVOID base, SIZE_T size);
BOOL FlushInstructionCache(HANDLE proc, LPCVOID base, SIZE_T size);
#define _aligned_malloc(size, align) compat_aligned_malloc(size, align)
#define _aligned_free(p) free(p)
#define _aligned_realloc(p, size, align) compat_aligned_realloc(p, size, align)
#define _aligned_msize(p, align, off) malloc_usable_size(p)
#define _msize(p) malloc_usable_size(p)
#define _alloca(n) __builtin_alloca(n)
void *compat_aligned_malloc(size_t size, size_t align);
void *compat_aligned_realloc(void *p, size_t size, size_t align);
HANDLE GetProcessHeap(void);
LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T n);
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID p);
#define HEAP_ZERO_MEMORY 8
#define ZeroMemory(p, n) memset((p), 0, (n))
#define SecureZeroMemory(p, n) memset((p), 0, (n))
#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define MoveMemory(d, s, n) memmove((d), (s), (n))
#define FillMemory(d, n, v) memset((d), (v), (n))
typedef struct _SYSTEM_INFO { union { DWORD dwOemId; struct { WORD wProcessorArchitecture, wReserved; }; }; DWORD dwPageSize; LPVOID lpMinimumApplicationAddress, lpMaximumApplicationAddress; DWORD_PTR dwActiveProcessorMask; DWORD dwNumberOfProcessors, dwProcessorType, dwAllocationGranularity; WORD wProcessorLevel, wProcessorRevision; } SYSTEM_INFO, *LPSYSTEM_INFO;
void GetSystemInfo(LPSYSTEM_INFO si);
#define GetNativeSystemInfo GetSystemInfo
typedef uint64_t DWORDLONG;
typedef struct _MEMORYSTATUSEX2 { DWORD dwLength, dwMemoryLoad; DWORDLONG ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile, ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual; } MEMORYSTATUSEX, *LPMEMORYSTATUSEX;
BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX m);

/* ---------------------------------------------------------------- handles, threads, synchronisation */
#define WAIT_OBJECT_0 0
#define WAIT_ABANDONED 0x80
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFFu
#define STILL_ACTIVE 259
#define CREATE_SUSPENDED 0x4
#define CREATE_NO_WINDOW 0x08000000
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x10000
#define THREAD_PRIORITY_IDLE -15
#define THREAD_PRIORITY_LOWEST -2
#define THREAD_PRIORITY_BELOW_NORMAL -1
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define THREAD_ALL_ACCESS 0x1FFFFF
#define THREAD_SUSPEND_RESUME 0x2
#define THREAD_GET_CONTEXT 0x8
#define THREAD_QUERY_INFORMATION 0x40
#define THREAD_QUERY_LIMITED_INFORMATION 0x800
#define SYNCHRONIZE 0x100000
#define PROCESS_ALL_ACCESS 0x1FFFFF
#define PROCESS_VM_READ 0x10
#define PROCESS_DUP_HANDLE 0x40
#define PROCESS_QUERY_INFORMATION 0x400
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define DUPLICATE_SAME_ACCESS 2
#define HIGH_PRIORITY_CLASS 0x80
#define ABOVE_NORMAL_PRIORITY_CLASS 0x8000
#define NORMAL_PRIORITY_CLASS 0x20
#define REALTIME_PRIORITY_CLASS 0x100
#define EVENT_ALL_ACCESS 0x1F0003
#define TIMER_ALL_ACCESS 0x1F0003
#define CREATE_WAITABLE_TIMER_MANUAL_RESET 1
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#define MAXIMUM_WAIT_OBJECTS 64
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
typedef LPTHREAD_START_ROUTINE PTHREAD_START_ROUTINE;
BOOL CloseHandle(HANDLE h);
BOOL DuplicateHandle(HANDLE sp, HANDLE h, HANDLE tp, LPHANDLE out, DWORD access, BOOL inherit, DWORD opts);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, LPDWORD tid);
DWORD ResumeThread(HANDLE h);
DWORD SuspendThread(HANDLE h);
BOOL TerminateThread(HANDLE h, DWORD code);
BOOL GetExitCodeThread(HANDLE h, LPDWORD code);
__attribute__((noreturn)) void ExitThread(DWORD code);
HANDLE GetCurrentThread(void);
HANDLE GetCurrentProcess(void);
DWORD GetCurrentThreadId(void);
DWORD GetCurrentProcessId(void);
DWORD GetThreadId(HANDLE h);
DWORD GetProcessId(HANDLE h);
HANDLE OpenThread(DWORD access, BOOL inherit, DWORD tid);
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);
BOOL SetThreadPriority(HANDLE h, int prio);
int GetThreadPriority(HANDLE h);
BOOL SetThreadPriorityBoost(HANDLE h, BOOL disable);
DWORD_PTR SetThreadAffinityMask(HANDLE h, DWORD_PTR mask);
BOOL GetProcessAffinityMask(HANDLE h, PDWORD_PTR proc, PDWORD_PTR sys);
BOOL SetPriorityClass(HANDLE h, DWORD c);
DWORD GetPriorityClass(HANDLE h);
HRESULT SetThreadDescription(HANDLE h, PCWSTR name);
DWORD GetCurrentProcessorNumber(void);
BOOL SwitchToThread(void);
void Sleep(DWORD ms);
DWORD SleepEx(DWORD ms, BOOL alertable);
#define YieldProcessor() __asm__ volatile("yield")
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define _ReadWriteBarrier() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#define _mm_pause() YieldProcessor()
__attribute__((noreturn)) void ExitProcess(UINT code);
BOOL TerminateProcess(HANDLE h, UINT code);
BOOL GetExitCodeProcess(HANDLE h, LPDWORD code);

HANDLE CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name);
#define CreateEvent CreateEventA
#define CREATE_EVENT_MANUAL_RESET 1
#define CREATE_EVENT_INITIAL_SET 2
HANDLE CreateEventExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access);
BOOL SetEvent(HANDLE h);
BOOL ResetEvent(HANDLE h);
BOOL PulseEvent(HANDLE h);
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name);
BOOL ReleaseMutex(HANDLE h);
HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG initial, LONG maximum, LPCSTR name);
BOOL ReleaseSemaphore(HANDLE h, LONG n, LPLONG prev);
HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access);
HANDLE CreateWaitableTimerA(LPSECURITY_ATTRIBUTES sa, BOOL manual, LPCSTR name);
#define CreateWaitableTimerW(sa, m, n) CreateWaitableTimerA(sa, m, NULL)
typedef void (*PTIMERAPCROUTINE)(LPVOID, DWORD, DWORD);
BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, LONG period, PTIMERAPCROUTINE apc, LPVOID arg, BOOL resume);
BOOL SetWaitableTimerEx(HANDLE h, const LARGE_INTEGER *due, LONG period, PTIMERAPCROUTINE apc, LPVOID arg, void *reason, ULONG delay);
BOOL CancelWaitableTimer(HANDLE h);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
DWORD WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL alertable);
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL all, DWORD ms);
DWORD WaitForMultipleObjectsEx(DWORD n, const HANDLE *hs, BOOL all, DWORD ms, BOOL alertable);

typedef struct _CRITICAL_SECTION { pthread_mutex_t m; volatile int init; DWORD spin; HANDLE OwningThread; LONG LockCount, RecursionCount; } CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;
void InitializeCriticalSection(LPCRITICAL_SECTION cs);
BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin);
BOOL InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags);
void DeleteCriticalSection(LPCRITICAL_SECTION cs);
void EnterCriticalSection(LPCRITICAL_SECTION cs);
BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs);
void LeaveCriticalSection(LPCRITICAL_SECTION cs);
/* SRWLOCK: one word. 0 = free, -1 = held exclusive, n > 0 = n shared owners. Futex based (win32_compat.c). */
typedef struct _SRWLOCK { volatile int32_t state; volatile int32_t waiters; } SRWLOCK, *PSRWLOCK;
#define SRWLOCK_INIT {0, 0}
#define RTL_SRWLOCK_INIT SRWLOCK_INIT
void InitializeSRWLock(PSRWLOCK l);
void AcquireSRWLockExclusive(PSRWLOCK l);
void ReleaseSRWLockExclusive(PSRWLOCK l);
void AcquireSRWLockShared(PSRWLOCK l);
void ReleaseSRWLockShared(PSRWLOCK l);
BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK l);
BOOLEAN TryAcquireSRWLockShared(PSRWLOCK l);
/* CONDITION_VARIABLE: a sequence number waited on with futex(2). */
typedef struct _CONDITION_VARIABLE { volatile int32_t seq; volatile int32_t waiters; } CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT {0, 0}
#define CONDITION_VARIABLE_LOCKMODE_SHARED 1
void InitializeConditionVariable(PCONDITION_VARIABLE cv);
BOOL SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms);
BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE cv, PSRWLOCK l, DWORD ms, ULONG flags);
void WakeConditionVariable(PCONDITION_VARIABLE cv);
void WakeAllConditionVariable(PCONDITION_VARIABLE cv);
BOOL WaitOnAddress(volatile void *addr, PVOID cmp, SIZE_T size, DWORD ms);
void WakeByAddressSingle(PVOID addr);
void WakeByAddressAll(PVOID addr);
typedef struct _INIT_ONCE { volatile int32_t state; void *ctx; } INIT_ONCE, *PINIT_ONCE, *LPINIT_ONCE;
#define INIT_ONCE_STATIC_INIT {0, 0}
typedef BOOL (*PINIT_ONCE_FN)(PINIT_ONCE, PVOID, PVOID *);
BOOL InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN fn, PVOID param, LPVOID *ctx);
#define INIT_ONCE_CHECK_ONLY 1
#define INIT_ONCE_ASYNC 2
#define INIT_ONCE_INIT_FAILED 4
BOOL InitOnceBeginInitialize(LPINIT_ONCE once, DWORD flags, PBOOL pending, LPVOID *ctx);
BOOL InitOnceComplete(LPINIT_ONCE once, DWORD flags, LPVOID ctx);

/* thread-local and fiber-local storage */
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFu
#define FLS_OUT_OF_INDEXES 0xFFFFFFFFu
DWORD TlsAlloc(void);
BOOL TlsFree(DWORD i);
LPVOID TlsGetValue(DWORD i);
BOOL TlsSetValue(DWORD i, LPVOID v);
typedef void (*PFLS_CALLBACK_FUNCTION)(PVOID);
DWORD FlsAlloc(PFLS_CALLBACK_FUNCTION cb);
BOOL FlsFree(DWORD i);
PVOID FlsGetValue(DWORD i);
BOOL FlsSetValue(DWORD i, PVOID v);
/* fibers: our own context switch (aarch64, win32_compat_fiber.S) */
typedef void (*LPFIBER_START_ROUTINE)(LPVOID);
typedef LPFIBER_START_ROUTINE PFIBER_START_ROUTINE;
LPVOID ConvertThreadToFiber(LPVOID param);
LPVOID ConvertThreadToFiberEx(LPVOID param, DWORD flags);
BOOL ConvertFiberToThread(void);
LPVOID CreateFiber(SIZE_T stack, LPFIBER_START_ROUTINE fn, LPVOID param);
LPVOID CreateFiberEx(SIZE_T commit, SIZE_T reserve, DWORD flags, LPFIBER_START_ROUTINE fn, LPVOID param);
void DeleteFiber(LPVOID fiber);
void SwitchToFiber(LPVOID fiber);
PVOID GetCurrentFiber(void);
PVOID GetFiberData(void);
BOOL IsThreadAFiber(void);

/* interlocked: 32-bit LONG as on Windows */
#define InterlockedIncrement(p) __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p) __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedIncrement64 InterlockedIncrement
#define InterlockedDecrement64 InterlockedDecrement
#define InterlockedIncrement16 InterlockedIncrement
#define InterlockedDecrement16 InterlockedDecrement
#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchange64 InterlockedExchange
#define InterlockedExchange8 InterlockedExchange
#define InterlockedExchange16 InterlockedExchange
#define InterlockedExchangePointer(p, v) ((PVOID)__atomic_exchange_n((PVOID volatile *)(p), (PVOID)(v), __ATOMIC_SEQ_CST))
#define InterlockedExchangeAdd(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd64 InterlockedExchangeAdd
#define InterlockedAdd(p, v) __atomic_add_fetch((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedAdd64 InterlockedAdd
#define InterlockedOr(p, v) __atomic_fetch_or((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedOr64 InterlockedOr
#define InterlockedOr8 InterlockedOr
#define InterlockedAnd(p, v) __atomic_fetch_and((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedAnd64 InterlockedAnd
#define InterlockedAnd8 InterlockedAnd
#define InterlockedXor(p, v) __atomic_fetch_xor((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedXor64 InterlockedXor
#define InterlockedCompareExchange(p, x, c) compat_cas((p), (x), (c))
#define InterlockedCompareExchange64 InterlockedCompareExchange
#define InterlockedCompareExchange16 InterlockedCompareExchange
#define InterlockedCompareExchangePointer(p, x, c) ((PVOID)compat_cas((PVOID volatile *)(p), (PVOID)(x), (PVOID)(c)))
#define compat_cas(p, x, c) ({ __typeof__(*(p)) _cmp = (c); __atomic_compare_exchange_n((p), &_cmp, (x), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); _cmp; })
#define _InterlockedIncrement InterlockedIncrement
#define _InterlockedDecrement InterlockedDecrement
#define _InterlockedExchange InterlockedExchange
#define _InterlockedCompareExchange InterlockedCompareExchange
#define _InterlockedExchangeAdd InterlockedExchangeAdd
#define ReadAcquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define WriteRelease(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

/* ---------------------------------------------------------------- time */
BOOL QueryPerformanceCounter(LARGE_INTEGER *c);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f);
DWORD GetTickCount(void);
ULONGLONG GetTickCount64(void);
void GetSystemTimeAsFileTime(LPFILETIME ft);
void GetSystemTimePreciseAsFileTime(LPFILETIME ft);
void GetLocalTime(LPSYSTEMTIME st);
void GetSystemTime(LPSYSTEMTIME st);
BOOL FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME st);
BOOL SystemTimeToFileTime(const SYSTEMTIME *st, LPFILETIME ft);
BOOL FileTimeToLocalFileTime(const FILETIME *ft, LPFILETIME out);
DWORD timeGetTime(void);
MMRESULT timeBeginPeriod(UINT p);
MMRESULT timeEndPeriod(UINT p);
#define TIMERR_NOERROR 0
typedef struct _TIME_ZONE_INFORMATION { LONG Bias; WCHAR StandardName[32]; SYSTEMTIME StandardDate; LONG StandardBias; WCHAR DaylightName[32]; SYSTEMTIME DaylightDate; LONG DaylightBias; } TIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;
typedef struct _TIME_DYNAMIC_ZONE_INFORMATION { LONG Bias; WCHAR StandardName[32]; SYSTEMTIME StandardDate; LONG StandardBias; WCHAR DaylightName[32]; SYSTEMTIME DaylightDate; LONG DaylightBias; WCHAR TimeZoneKeyName[128]; BOOLEAN DynamicDaylightTimeDisabled; } DYNAMIC_TIME_ZONE_INFORMATION, *PDYNAMIC_TIME_ZONE_INFORMATION;
#define TIME_ZONE_ID_INVALID 0xFFFFFFFFu
#define TIME_ZONE_ID_UNKNOWN 0
DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION tz);
DWORD GetDynamicTimeZoneInformation(PDYNAMIC_TIME_ZONE_INFORMATION tz);
#define _time64(t) ((int64_t)time((time_t *)(t)))
#define _gmtime64_s(tm, t) (gmtime_r((const time_t *)(t), (tm)) ? 0 : 1)
#define _localtime64_s(tm, t) (localtime_r((const time_t *)(t), (tm)) ? 0 : 1)
#define _mkgmtime64(tm) timegm(tm)
#define _mkgmtime(tm) timegm(tm)
typedef int64_t __time64_t;

/* ---------------------------------------------------------------- files */
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define GENERIC_EXECUTE 0x20000000u
#define GENERIC_ALL 0x10000000u
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x1
#define FILE_ATTRIBUTE_HIDDEN 0x2
#define FILE_ATTRIBUTE_SYSTEM 0x4
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_TEMPORARY 0x100
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_DISK 1
#define FILE_TYPE_CHAR 2
#define FILE_NAME_NORMALIZED 0
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_COPY_ALLOWED 2
#define MOVEFILE_WRITE_THROUGH 8
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
typedef struct _WIN32_FIND_DATAA { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1; CHAR cFileName[MAX_PATH]; CHAR cAlternateFileName[14]; } WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef struct _WIN32_FIND_DATAW { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1; WCHAR cFileName[MAX_PATH]; WCHAR cAlternateFileName[14]; } WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;
typedef struct _WIN32_FILE_ATTRIBUTE_DATA { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard, GetFileExMaxInfoLevel } GET_FILEEX_INFO_LEVELS;
typedef struct _BY_HANDLE_FILE_INFORMATION { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD dwVolumeSerialNumber, nFileSizeHigh, nFileSizeLow, nNumberOfLinks, nFileIndexHigh, nFileIndexLow; } BY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl);
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl);
BOOL ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov);
BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD put, LPOVERLAPPED ov);
BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER dist, PLARGE_INTEGER pos, DWORD how);
DWORD SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD how);
BOOL GetFileSizeEx(HANDLE h, PLARGE_INTEGER size);
DWORD GetFileSize(HANDLE h, LPDWORD hi);
BOOL SetEndOfFile(HANDLE h);
BOOL FlushFileBuffers(HANDLE h);
BOOL GetFileTime(HANDLE h, LPFILETIME c, LPFILETIME a, LPFILETIME w);
BOOL GetFileInformationByHandle(HANDLE h, LPBY_HANDLE_FILE_INFORMATION info);
DWORD GetFileType(HANDLE h);
DWORD GetFinalPathNameByHandleA(HANDLE h, LPSTR out, DWORD n, DWORD flags);
HANDLE GetStdHandle(DWORD which);
DWORD GetFileAttributesA(LPCSTR path);
DWORD GetFileAttributesW(LPCWSTR path);
BOOL SetFileAttributesA(LPCSTR path, DWORD attr);
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID out);
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID out);
BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES sa);
BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES sa);
BOOL RemoveDirectoryA(LPCSTR path);
BOOL DeleteFileA(LPCSTR path);
BOOL DeleteFileW(LPCWSTR path);
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags);
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags);
BOOL MoveFileA(LPCSTR from, LPCSTR to);
BOOL CopyFileA(LPCSTR from, LPCSTR to, BOOL fail_if_exists);
BOOL ReplaceFileA(LPCSTR repl, LPCSTR with, LPCSTR backup, DWORD flags, LPVOID a, LPVOID b);
HANDLE FindFirstFileA(LPCSTR pattern, LPWIN32_FIND_DATAA fd);
HANDLE FindFirstFileW(LPCWSTR pattern, LPWIN32_FIND_DATAW fd);
BOOL FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd);
BOOL FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW fd);
BOOL FindClose(HANDLE h);
DWORD GetFullPathNameA(LPCSTR path, DWORD n, LPSTR out, LPSTR *file_part);
DWORD GetCurrentDirectoryA(DWORD n, LPSTR out);
BOOL SetCurrentDirectoryA(LPCSTR path);
DWORD GetTempPathA(DWORD n, LPSTR out);
DWORD GetShortPathNameW(LPCWSTR in, LPWSTR out, DWORD n);
DWORD GetShortPathNameA(LPCSTR in, LPSTR out, DWORD n);
UINT GetTempFileNameA(LPCSTR dir, LPCSTR prefix, UINT unique, LPSTR out);
BOOL GetDiskFreeSpaceExA(LPCSTR path, PULARGE_INTEGER avail, PULARGE_INTEGER total, PULARGE_INTEGER free_);
HANDLE CreateFileMappingA(HANDLE f, LPSECURITY_ATTRIBUTES sa, DWORD prot, DWORD hi, DWORD lo, LPCSTR name);
LPVOID MapViewOfFile(HANDLE m, DWORD access, DWORD hi, DWORD lo, SIZE_T n);
LPVOID MapViewOfFileEx(HANDLE m, DWORD access, DWORD hi, DWORD lo, SIZE_T n, LPVOID addr);
BOOL UnmapViewOfFile(LPCVOID p);
#define FILE_MAP_READ 4
#define FILE_MAP_WRITE 2
#define FILE_MAP_COPY 1
#define FILE_MAP_ALL_ACCESS 0xF001F
#define SEC_IMAGE 0x1000000
#define _get_osfhandle(fd) ((intptr_t)compat_handle_from_fd(fd))
#define _fileno fileno
#define _fseeki64 fseeko
#define _ftelli64 ftello
#define _isatty isatty
#define _open open
#define _close close
#define _read read
#define _write write
#define _lseeki64 lseek
#define _unlink compat_unlink
#define _access(p, m) compat_access((p), (m))
#define _mkdir(p) compat_mkdir(p)
#define _rmdir(p) compat_rmdir(p)
#define _chdir(p) compat_chdir(p)
#define _getcwd getcwd
#define _fullpath(out, p, n) compat_fullpath((out), (p), (n))
#define _stat64 stat
#define _stat compat_stat_fn
#define _fstat64 fstat
#define _S_IFDIR S_IFDIR
#define _S_IFREG S_IFREG
#define _O_BINARY 0
#define O_BINARY 0
HANDLE compat_handle_from_fd(int fd);
int compat_access(const char *p, int m);
int compat_mkdir(const char *p);
int compat_rmdir(const char *p);
int compat_chdir(const char *p);
int compat_unlink(const char *p);
char *compat_fullpath(char *out, const char *p, size_t n);
/* path helper: '\' -> '/', in place on a copy (dst of MAX_PATH * 4 bytes) */
const char *compat_path(const char *in, char *dst, size_t n);
FILE *compat_fopen(const char *path, const char *mode);
FILE *compat_freopen(const char *path, const char *mode, FILE *f);
int compat_remove(const char *path);
int compat_rename(const char *a, const char *b);
int compat_stat(const char *path, struct stat *st);
#ifndef COMPAT_NO_FILE_WRAPPERS
#define fopen(p, m) compat_fopen((p), (m))
#define freopen(p, m, f) compat_freopen((p), (m), (f))
#define remove(p) compat_remove(p)
#define rename(a, b) compat_rename((a), (b))
#define fread(p, s, n, f) compat_fread((p), (s), (n), (f))     /* the write watch: the pages it fills get their right first */
#endif
size_t compat_fread(void *p, size_t size, size_t n, FILE *f);
void compat_ww_prepare(void *p, size_t n);
#define _wfopen(p, m) compat_wfopen((p), (m))
FILE *compat_wfopen(const WCHAR *p, const WCHAR *m);

/* ---------------------------------------------------------------- modules */
#define GET_MODULE_HANDLE_EX_FLAG_PIN 1
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x100
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x1000
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x8
#define DONT_RESOLVE_DLL_REFERENCES 0x1
HMODULE LoadLibraryA(LPCSTR name);
HMODULE LoadLibraryW(LPCWSTR name);
HMODULE LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags);
HMODULE LoadLibraryExW(LPCWSTR name, HANDLE f, DWORD flags);
BOOL FreeLibrary(HMODULE m);
FARPROC GetProcAddress(HMODULE m, LPCSTR name);
HMODULE GetModuleHandleA(LPCSTR name);
HMODULE GetModuleHandleW(LPCWSTR name);
BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *out);
BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *out);
DWORD GetModuleFileNameA(HMODULE m, LPSTR out, DWORD n);
DWORD GetModuleFileNameW(HMODULE m, LPWSTR out, DWORD n);
BOOL SetDllDirectoryA(LPCSTR p);
BOOL SetDefaultDllDirectories(DWORD f);
void *AddDllDirectory(PCWSTR p);
BOOL QueryFullProcessImageNameA(HANDLE h, DWORD flags, LPSTR out, PDWORD n);
/* GetProcAddress returns a function pointer typed FARPROC; the sources cast it, which works with any pointer type. */

/* ---------------------------------------------------------------- environment, strings, misc */
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR out, DWORD n);
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value);
#define _putenv(s) compat_putenv(s)
#define _putenv_s(n, v) compat_putenv_s((n), (v))
#define _dupenv_s(out, len, name) compat_dupenv_s((out), (len), (name))
int compat_putenv(const char *s);
int compat_putenv_s(const char *n, const char *v);
int compat_dupenv_s(char **out, size_t *len, const char *name);
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _strdup strdup
#define _wcsdup compat_wcsdup
#define _strlwr compat_strlwr
#define _strupr compat_strupr
#define _atoi64 atoll
#define _strtoui64 strtoull
#define _strtoi64 strtoll
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define sprintf_s(b, n, ...) snprintf((b), (n), __VA_ARGS__)
#define vsprintf_s(b, n, f, a) vsnprintf((b), (n), (f), (a))
#define _TRUNCATE ((size_t)-1)
#define strncpy_s(d, n, s, c) compat_strncpy_s((d), (n), (s), (c))
#define strcpy_s(d, n, s) compat_strncpy_s((d), (n), (s), _TRUNCATE)
#define strcat_s(d, n, s) compat_strcat_s((d), (n), (s))
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define _byteswap_ushort(x) __builtin_bswap16(x)
#define _byteswap_ulong(x) __builtin_bswap32(x)
#define _byteswap_uint64(x) __builtin_bswap64(x)
#define _rotl(x, n) ((uint32_t)(((uint32_t)(x) << ((n) & 31)) | ((uint32_t)(x) >> ((32 - ((n) & 31)) & 31))))
#define _rotr(x, n) ((uint32_t)(((uint32_t)(x) >> ((n) & 31)) | ((uint32_t)(x) << ((32 - ((n) & 31)) & 31))))
#define _rotl64(x, n) ((uint64_t)(((uint64_t)(x) << ((n) & 63)) | ((uint64_t)(x) >> ((64 - ((n) & 63)) & 63))))
#define __debugbreak() __builtin_trap()
#define DebugBreak() __builtin_trap()
#define IsDebuggerPresent() 0
char *compat_strlwr(char *s);
char *compat_strupr(char *s);
int compat_strncpy_s(char *d, size_t n, const char *s, size_t c);
int compat_strcat_s(char *d, size_t n, const char *s);
/* wide strings: 16-bit (-fshort-wchar), so bionic's 32-bit wcs* functions must not be used */
size_t compat_wcslen(const WCHAR *s);
int compat_wcscmp(const WCHAR *a, const WCHAR *b);
int compat_wcsncmp(const WCHAR *a, const WCHAR *b, size_t n);
int compat_wcsicmp(const WCHAR *a, const WCHAR *b);
WCHAR *compat_wcscpy(WCHAR *d, const WCHAR *s);
WCHAR *compat_wcsncpy(WCHAR *d, const WCHAR *s, size_t n);
WCHAR *compat_wcscat(WCHAR *d, const WCHAR *s);
WCHAR *compat_wcschr(const WCHAR *s, WCHAR c);
WCHAR *compat_wcsrchr(const WCHAR *s, WCHAR c);
WCHAR *compat_wcsstr(const WCHAR *s, const WCHAR *t);
WCHAR *compat_wcsdup(const WCHAR *s);
int compat_snwprintf(WCHAR *out, size_t n, const WCHAR *fmt, ...);
long compat_wcstol(const WCHAR *s, WCHAR **end, int base);
#define wcslen compat_wcslen
#define wcscmp compat_wcscmp
#define wcsncmp compat_wcsncmp
#define _wcsicmp compat_wcsicmp
#define wcscpy compat_wcscpy
#define wcsncpy compat_wcsncpy
#define wcscat compat_wcscat
#define wcschr compat_wcschr
#define wcsrchr compat_wcsrchr
#define wcsstr compat_wcsstr
#define wcstol compat_wcstol
#define _snwprintf compat_snwprintf
#define swprintf compat_snwprintf
#define _swprintf compat_snwprintf
#define lstrlenW(s) ((int)compat_wcslen(s))
#define lstrcmpiW compat_wcsicmp
#define lstrcmpW compat_wcscmp
#define lstrcpyW compat_wcscpy
#define lstrlenA(s) ((int)strlen(s))
#define lstrcmpiA strcasecmp
#define lstrcpynA(d, s, n) compat_lstrcpyn((d), (s), (n))
char *compat_lstrcpyn(char *d, const char *s, int n);
#define CP_ACP 0
#define CP_UTF8 65001
#define WC_ERR_INVALID_CHARS 0x80
#define MB_ERR_INVALID_CHARS 0x8
int MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR s, int n, LPWSTR out, int m);
int WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR s, int n, LPSTR out, int m, LPCSTR def, BOOL *used);
BOOL GetUserNameW(LPWSTR out, LPDWORD n);
BOOL GetUserNameA(LPSTR out, LPDWORD n);
BOOL GetComputerNameA(LPSTR out, LPDWORD n);
int GetUserDefaultGeoName(LPWSTR out, int n);
int GetUserDefaultLocaleName(LPWSTR out, int n);
LCID GetUserDefaultLCID(void);
WORD GetUserDefaultUILanguage(void);
#define LOCALE_NAME_MAX_LENGTH 85
void OutputDebugStringA(LPCSTR s);
void OutputDebugStringW(LPCWSTR s);
BOOL Beep(DWORD f, DWORD d);
UINT SetErrorMode(UINT m);
#define SEM_FAILCRITICALERRORS 1
#define SEM_NOGPFAULTERRORBOX 2
#define SEM_NOOPENFILEERRORBOX 0x8000
extern int __argc;
extern char **__argv;
LPSTR GetCommandLineA(void);
DWORD FormatMessageA(DWORD flags, LPCVOID src, DWORD id, DWORD lang, LPSTR out, DWORD n, va_list *args);
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x200
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x100
HLOCAL LocalFree(HLOCAL p);
BOOL SetConsoleOutputCP(UINT cp);
BOOL SetConsoleCP(UINT cp);
BOOL SetConsoleTitleA(LPCSTR t);
BOOL AllocConsole(void);
BOOL AttachConsole(DWORD pid);
#define ATTACH_PARENT_PROCESS ((DWORD)-1)
HWND GetConsoleWindow(void);
BOOLEAN SystemFunction036(PVOID buf, ULONG n);          /* RtlGenRandom */
#define RtlGenRandom SystemFunction036

/* power / process information: accepted and ignored */
typedef enum _PROCESS_INFORMATION_CLASS { ProcessMemoryPriority, ProcessMemoryExhaustionInfo, ProcessAppMemoryInfo, ProcessInPrivateInfo, ProcessPowerThrottling } PROCESS_INFORMATION_CLASS;
typedef struct _PROCESS_POWER_THROTTLING_STATE { ULONG Version, ControlMask, StateMask; } PROCESS_POWER_THROTTLING_STATE;
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 1
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 4
BOOL SetProcessInformation(HANDLE h, PROCESS_INFORMATION_CLASS c, LPVOID info, DWORD n);
typedef enum _CPU_SET_INFORMATION_TYPE { CpuSetInformation } CPU_SET_INFORMATION_TYPE;
typedef struct _SYSTEM_CPU_SET_INFORMATION { DWORD Size; CPU_SET_INFORMATION_TYPE Type; union { struct { DWORD Id; WORD Group; BYTE LogicalProcessorIndex, CoreIndex, LastLevelCacheIndex, NumaNodeIndex, EfficiencyClass; union { BYTE AllFlags; struct { BYTE Parked : 1, Allocated : 1, AllocatedToTargetProcess : 1, RealTime : 1, ReservedFlags : 4; }; }; union { DWORD Reserved; BYTE SchedulingClass; }; DWORD64 AllocationTag; } CpuSet; }; } SYSTEM_CPU_SET_INFORMATION, *PSYSTEM_CPU_SET_INFORMATION;
BOOL GetSystemCpuSetInformation(PSYSTEM_CPU_SET_INFORMATION info, ULONG n, PULONG ret, HANDLE proc, ULONG flags);
BOOL SetThreadSelectedCpuSets(HANDLE h, const ULONG *ids, ULONG n);
typedef enum _LOGICAL_PROCESSOR_RELATIONSHIP { RelationProcessorCore, RelationNumaNode, RelationCache, RelationProcessorPackage, RelationGroup, RelationAll = 0xffff } LOGICAL_PROCESSOR_RELATIONSHIP;
BOOL GetThreadTimes(HANDLE h, LPFILETIME c, LPFILETIME e, LPFILETIME k, LPFILETIME u);
BOOL GetSystemTimes(LPFILETIME idle, LPFILETIME kernel, LPFILETIME user);
BOOL GetProcessTimes(HANDLE h, LPFILETIME c, LPFILETIME e, LPFILETIME k, LPFILETIME u);
BOOL QueryThreadCycleTime(HANDLE h, PULONG64 cycles);
BOOL ReadProcessMemory(HANDLE h, LPCVOID addr, LPVOID buf, SIZE_T n, SIZE_T *got);
BOOL WriteProcessMemory(HANDLE h, LPVOID addr, LPCVOID buf, SIZE_T n, SIZE_T *put);

/* ---------------------------------------------------------------- structured exceptions (crash reports only)
 * SetUnhandledExceptionFilter / AddVectoredExceptionHandler are served by signal handlers (SIGSEGV, SIGBUS, SIGILL,
 * SIGFPE, SIGTRAP): the filter gets an EXCEPTION_POINTERS whose CONTEXT holds the aarch64 registers under the x64 names
 * the reports print (Rip = pc, Rsp = sp, Rax.. = x0..). RaiseException raises a SIGABRT-like report. */
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001Du
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094u
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDu
#define EXCEPTION_BREAKPOINT 0x80000003u
#define EXCEPTION_SINGLE_STEP 0x80000004u
#define EXCEPTION_IN_PAGE_ERROR 0xC0000006u
#define EXCEPTION_DATATYPE_MISALIGNMENT 0x80000002u
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096u
#define EXCEPTION_GUARD_PAGE 0x80000001u
#define STATUS_ACCESS_VIOLATION EXCEPTION_ACCESS_VIOLATION
#define STATUS_GUARD_PAGE_VIOLATION EXCEPTION_GUARD_PAGE
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_NONCONTINUABLE 1
typedef struct _M128A { ULONGLONG Low; LONGLONG High; } M128A;
typedef struct _CONTEXT {
    DWORD64 P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    DWORD ContextFlags, MxCsr;
    WORD SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    DWORD EFlags;
    DWORD64 Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    DWORD64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8, R9, R10, R11, R12, R13, R14, R15, Rip;
    M128A Xmm[16];
    DWORD64 X[31], Sp, Pc, Pstate;     /* the real aarch64 registers */
} CONTEXT, *PCONTEXT;
#define CONTEXT_FULL 0x10000b
#define CONTEXT_ALL 0x10001f
#define CONTEXT_CONTROL 0x100001
#define CONTEXT_INTEGER 0x100002
#define CONTEXT_DEBUG_REGISTERS 0x100010
typedef struct _EXCEPTION_RECORD { DWORD ExceptionCode, ExceptionFlags; struct _EXCEPTION_RECORD *ExceptionRecord; PVOID ExceptionAddress; DWORD NumberParameters; ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS]; } EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct _EXCEPTION_POINTERS { PEXCEPTION_RECORD ExceptionRecord; PCONTEXT ContextRecord; } EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;
typedef LONG (*PTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS *);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
typedef LONG (*PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS *);
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f);
PVOID AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h);
ULONG RemoveVectoredExceptionHandler(PVOID h);
PVOID AddVectoredContinueHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER h);
__attribute__((noreturn)) void RaiseException(DWORD code, DWORD flags, DWORD n, const ULONG_PTR *args);
BOOL GetThreadContext(HANDLE h, PCONTEXT ctx);
BOOL SetThreadContext(HANDLE h, const CONTEXT *ctx);
void RtlCaptureContext(PCONTEXT ctx);
typedef struct _RUNTIME_FUNCTION { DWORD BeginAddress, EndAddress, UnwindData; } RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;
#define UNW_FLAG_NHANDLER 0
PRUNTIME_FUNCTION RtlLookupFunctionEntry(DWORD64 pc, PDWORD64 base, PVOID table);
PVOID RtlVirtualUnwind(DWORD type, DWORD64 base, DWORD64 pc, PRUNTIME_FUNCTION f, PCONTEXT ctx, PVOID *hd, PDWORD64 est, PVOID ptrs);
WORD RtlCaptureStackBackTrace(DWORD skip, DWORD n, PVOID *out, PDWORD hash);
#define CaptureStackBackTrace RtlCaptureStackBackTrace

#include "winnt_pe.h"

/* processes: CreateProcess is not available (the minidump helper and the self-restart are Windows only) */
typedef struct _STARTUPINFOA { DWORD cb; LPSTR lpReserved, lpDesktop, lpTitle; DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; WORD wShowWindow, cbReserved2; LPBYTE lpReserved2; HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOA, *LPSTARTUPINFOA;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;
#define STARTF_USESTDHANDLES 0x100
#define STARTF_USESHOWWINDOW 0x1
#define DETACHED_PROCESS 0x8
#define CREATE_NEW_CONSOLE 0x10
BOOL CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi);
#define _environ environ
extern char **environ;
/* power management: the activity keeps the screen on (FLAG_KEEP_SCREEN_ON) */
#define ES_SYSTEM_REQUIRED 0x1
#define ES_DISPLAY_REQUIRED 0x2
#define ES_CONTINUOUS 0x80000000u
DWORD SetThreadExecutionState(DWORD f);
BOOL IsBadReadPtr(const void *p, UINT_PTR n);
BOOL IsBadWritePtr(void *p, UINT_PTR n);

/* ---------------------------------------------------------------- user interface: just enough to compile
 * The Android build has no Win32 windows. MessageBoxA / W print to the log (and the launcher shows the log); the code
 * that creates windows is under #ifndef __ANDROID__ in the sources. */
#define MB_OK 0
#define MB_OKCANCEL 1
#define MB_YESNO 4
#define MB_ICONERROR 0x10
#define MB_ICONHAND 0x10
#define MB_ICONQUESTION 0x20
#define MB_ICONWARNING 0x30
#define MB_ICONEXCLAMATION 0x30
#define MB_ICONINFORMATION 0x40
#define MB_TOPMOST 0x40000
#define MB_SETFOREGROUND 0x10000
#define MB_SYSTEMMODAL 0x1000
#define MB_TASKMODAL 0x2000
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
int MessageBoxA(HWND w, LPCSTR text, LPCSTR title, UINT type);
int MessageBoxW(HWND w, LPCWSTR text, LPCWSTR title, UINT type);
SHORT GetAsyncKeyState(int vk);
SHORT GetKeyState(int vk);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND w);
DWORD GetWindowThreadProcessId(HWND w, LPDWORD pid);
BOOL SetWindowTextA(HWND w, LPCSTR t);
BOOL PostMessageA(HWND w, UINT m, WPARAM a, LPARAM b);
BOOL ShowWindow(HWND w, int cmd);
BOOL IsWindow(HWND w);
BOOL IsIconic(HWND w);
BOOL GetClientRect(HWND w, LPRECT r);
BOOL GetWindowRect(HWND w, LPRECT r);
int ShowCursor(BOOL show);
typedef BOOL (*WNDENUMPROC)(HWND, LPARAM);
BOOL EnumWindows(WNDENUMPROC fn, LPARAM arg);
BOOL IsWindowVisible(HWND w);
HWND GetWindow(HWND w, UINT cmd);
#define GW_OWNER 4
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNOACTIVATE 4
#define MUI_LANGUAGE_NAME 0x8
BOOL GetUserPreferredUILanguages(DWORD flags, PULONG n, PWSTR buf, PULONG size);
/* the activity reports focus (SDL window events): GetForegroundWindow is NULL while the app is in the background */
void compat_set_focus(int focused);
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_SHOWNORMAL 1
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define WM_APP 0x8000
#define WM_USER 0x0400
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
/* the host keyboard state the Android activity reports (win32_compat.c; SDL key events are mapped to VK codes) */
void compat_set_key(int vk, int down);
#define KEYEVENTF_KEYUP 0x2
void keybd_event(BYTE vk, BYTE scan, DWORD flags, ULONG_PTR extra);

/* GDI / WGL: the OpenGL renderer of nvn_gl.c is not available on Android; these fail, so it cannot start */
typedef struct tagPIXELFORMATDESCRIPTOR { WORD nSize, nVersion; DWORD dwFlags; BYTE iPixelType, cColorBits, cRedBits, cRedShift, cGreenBits, cGreenShift, cBlueBits, cBlueShift, cAlphaBits, cAlphaShift, cAccumBits, cAccumRedBits, cAccumGreenBits, cAccumBlueBits, cAccumAlphaBits, cDepthBits, cStencilBits, cAuxBuffers, iLayerType, bReserved; DWORD dwLayerMask, dwVisibleMask, dwDamageMask; } PIXELFORMATDESCRIPTOR, *PPIXELFORMATDESCRIPTOR;
#define PFD_TYPE_RGBA 0
#define PFD_MAIN_PLANE 0
#define PFD_DOUBLEBUFFER 0x1
#define PFD_DRAW_TO_WINDOW 0x4
#define PFD_SUPPORT_OPENGL 0x20
int ChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *p);
BOOL SetPixelFormat(HDC dc, int f, const PIXELFORMATDESCRIPTOR *p);
BOOL SwapBuffers(HDC dc);
HDC GetDC(HWND w);
int ReleaseDC(HWND w, HDC dc);
HGLRC wglCreateContext(HDC dc);
BOOL wglMakeCurrent(HDC dc, HGLRC rc);
BOOL wglDeleteContext(HGLRC rc);
HWND CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p);
BOOL DestroyWindow(HWND w);
int GetWindowTextA(HWND w, LPSTR out, int n);
#define WS_POPUP 0x80000000u
#define WS_VISIBLE 0x10000000u
#define WS_OVERLAPPEDWINDOW 0x00CF0000u
#define WRITE_WATCH_FLAG_RESET 1
#define LF_FACESIZE 32

/* bitmap file structures (screenshots are written as BMP files) */
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER { WORD bfType; DWORD bfSize; WORD bfReserved1, bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER, *PBITMAPFILEHEADER;
#pragma pack(pop)
typedef struct tagBITMAPINFOHEADER { DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount; DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant; } BITMAPINFOHEADER, *PBITMAPINFOHEADER;
typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct tagBITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO, *PBITMAPINFO;
#define BI_RGB 0
#define BI_BITFIELDS 3
#define DIB_RGB_COLORS 0

/* registry: no registry; every query fails */
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)0x80000002)
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001)
#define KEY_READ 0x20019
#define KEY_QUERY_VALUE 1
#define KEY_WOW64_64KEY 0x100
#define REG_SZ 1
#define REG_DWORD 4
#define REG_BINARY 3
#define RRF_RT_REG_SZ 2
#define RRF_RT_REG_DWORD 0x10
#define RRF_RT_REG_BINARY 8
#define RRF_RT_ANY 0xffff
typedef LONG LSTATUS;
LSTATUS RegOpenKeyExA(HKEY k, LPCSTR sub, DWORD opt, DWORD sam, HKEY *out);
LSTATUS RegOpenKeyExW(HKEY k, LPCWSTR sub, DWORD opt, DWORD sam, HKEY *out);
LSTATUS RegQueryValueExA(HKEY k, LPCSTR name, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD n);
LSTATUS RegQueryValueExW(HKEY k, LPCWSTR name, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD n);
LSTATUS RegGetValueA(HKEY k, LPCSTR sub, LPCSTR name, DWORD flags, LPDWORD type, PVOID data, LPDWORD n);
LSTATUS RegGetValueW(HKEY k, LPCWSTR sub, LPCWSTR name, DWORD flags, LPDWORD type, PVOID data, LPDWORD n);
LSTATUS RegEnumKeyExW(HKEY k, DWORD i, LPWSTR name, LPDWORD n, LPDWORD res, LPWSTR cls, LPDWORD cn, PFILETIME t);
LSTATUS RegCloseKey(HKEY k);

/* COM: CoInitializeEx and friends succeed and do nothing */
#define COINIT_MULTITHREADED 0
#define COINIT_APARTMENTTHREADED 2
#define COINIT_DISABLE_OLE1DDE 4
HRESULT CoInitializeEx(LPVOID r, DWORD f);
HRESULT CoInitialize(LPVOID r);
void CoUninitialize(void);
void CoTaskMemFree(LPVOID p);

#ifdef __cplusplus
}
#endif

/* ---------------------------------------------------------------- upstream plat_ helpers (runtime/plat/windows.h) the shared
 * runtime calls off Windows; the Android versions are in win32_compat.c */
int plat_backtrace(uintptr_t *pcs, int max);                        /* return addresses of this thread, innermost first */
void plat_print_build_identity(const char *exe, size_t dir_len);    /* boot.c: the log's "build:" lines */
int plat_current_stack(uintptr_t *lo, uintptr_t *hi);                /* the stack this thread runs on now (a fiber's or its own) */
#endif
