/* <intrin.h> for the Android port: the MSVC intrinsics the runtime uses are defined in windows.h (android_port.h includes it
 * first); this file only lets `#include <intrin.h>` (nnunwind.c) find something that is not clang's MSVC-only wrapper. */
#pragma once
#include "windows.h"
