/* psapi.h for the Android port: module list and memory counters (win32_compat.c; modules from dl_iterate_phdr). */
#pragma once
#include "windows.h"
typedef struct _MODULEINFO { LPVOID lpBaseOfDll; DWORD SizeOfImage; LPVOID EntryPoint; } MODULEINFO, *LPMODULEINFO;
typedef struct _PROCESS_MEMORY_COUNTERS { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage; } PROCESS_MEMORY_COUNTERS, *PPROCESS_MEMORY_COUNTERS;
typedef struct _PROCESS_MEMORY_COUNTERS_EX { DWORD cb, PageFaultCount; SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage, PrivateUsage; } PROCESS_MEMORY_COUNTERS_EX;
BOOL K32EnumProcessModules(HANDLE p, HMODULE *mods, DWORD cb, LPDWORD need);
BOOL K32GetModuleInformation(HANDLE p, HMODULE m, LPMODULEINFO mi, DWORD cb);
BOOL K32GetProcessMemoryInfo(HANDLE p, PPROCESS_MEMORY_COUNTERS c, DWORD cb);
DWORD K32GetModuleBaseNameA(HANDLE p, HMODULE m, LPSTR out, DWORD n);
#define EnumProcessModules K32EnumProcessModules
#define GetModuleInformation K32GetModuleInformation
#define GetProcessMemoryInfo K32GetProcessMemoryInfo
#define GetModuleBaseNameA K32GetModuleBaseNameA
