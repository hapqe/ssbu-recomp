/* dbghelp.h for the Android port: no minidumps (MiniDumpWriteDump is never found by GetProcAddress). */
#pragma once
#include "windows.h"
typedef enum _MINIDUMP_TYPE { MiniDumpNormal = 0, MiniDumpWithDataSegs = 1, MiniDumpWithFullMemory = 2, MiniDumpWithHandleData = 4, MiniDumpWithThreadInfo = 0x1000, MiniDumpWithIndirectlyReferencedMemory = 0x40, MiniDumpWithProcessThreadData = 0x100, MiniDumpWithPrivateReadWriteMemory = 0x200, MiniDumpWithUnloadedModules = 0x20, MiniDumpWithFullMemoryInfo = 0x800 } MINIDUMP_TYPE;
typedef struct _MINIDUMP_EXCEPTION_INFORMATION { DWORD ThreadId; PEXCEPTION_POINTERS ExceptionPointers; BOOL ClientPointers; } MINIDUMP_EXCEPTION_INFORMATION, *PMINIDUMP_EXCEPTION_INFORMATION;
typedef void *PMINIDUMP_USER_STREAM_INFORMATION, *PMINIDUMP_CALLBACK_INFORMATION;
