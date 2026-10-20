#pragma once
#include "windows.h"
HINSTANCE ShellExecuteA(HWND w, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show);
HINSTANCE ShellExecuteW(HWND w, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show);
