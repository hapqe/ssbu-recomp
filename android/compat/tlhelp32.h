/* tlhelp32.h for the Android port: thread list from /proc/self/task, module list from dl_iterate_phdr (win32_compat.c). */
#pragma once
#include "windows.h"
#define TH32CS_SNAPTHREAD 0x4
#define TH32CS_SNAPMODULE 0x8
#define TH32CS_SNAPMODULE32 0x10
#define TH32CS_SNAPPROCESS 0x2
#define MAX_MODULE_NAME32 255
typedef struct tagTHREADENTRY32 { DWORD dwSize, cntUsage, th32ThreadID, th32OwnerProcessID; LONG tpBasePri, tpDeltaPri; DWORD dwFlags; } THREADENTRY32, *LPTHREADENTRY32;
typedef struct tagMODULEENTRY32 { DWORD dwSize, th32ModuleID, th32ProcessID, GlblcntUsage, ProccntUsage; BYTE *modBaseAddr; DWORD modBaseSize; HMODULE hModule; char szModule[MAX_MODULE_NAME32 + 1]; char szExePath[MAX_PATH]; } MODULEENTRY32, *LPMODULEENTRY32;
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid);
BOOL Thread32First(HANDLE s, LPTHREADENTRY32 te);
BOOL Thread32Next(HANDLE s, LPTHREADENTRY32 te);
BOOL Module32First(HANDLE s, LPMODULEENTRY32 me);
BOOL Module32Next(HANDLE s, LPMODULEENTRY32 me);
