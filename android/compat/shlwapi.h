#pragma once
#include "windows.h"
BOOL PathFileExistsA(LPCSTR p);
BOOL PathIsDirectoryA(LPCSTR p);
BOOL PathRemoveFileSpecA(LPSTR p);
LPSTR PathFindFileNameA(LPCSTR p);
LPSTR PathFindExtensionA(LPCSTR p);
BOOL PathAppendA(LPSTR p, LPCSTR more);
LPSTR StrStrIA(LPCSTR a, LPCSTR b);
LSTATUS SHGetValueA(HKEY k, LPCSTR sub, LPCSTR name, LPDWORD type, LPVOID data, LPDWORD n);
LSTATUS SHGetValueW(HKEY k, LPCWSTR sub, LPCWSTR name, LPDWORD type, LPVOID data, LPDWORD n);
