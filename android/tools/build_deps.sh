#!/bin/bash
# Builds the third-party libraries of the Android port for arm64-v8a with the NDK and CMake:
#   SDL3 (libSDL3.so + its Java sources), glslang (static, for libssbu_glslang.so), libopus (libopus.so).
# Usage: android/tools/build_deps.sh <folder with SDL3-3.4.18, glslang-16.6.0, opus-1.5.2>
# Output: android/build/deps/{sdl3,glslang,opus}/install
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$(cd "${1:-$ROOT/../deps}" && pwd)"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="${NDK:-$SDK/ndk/29.0.14206865}"
API=30
OUT="$ROOT/android/build/deps"
CMAKE="${CMAKE:-$(command -v cmake || echo "$SDK/cmake/3.22.1/bin/cmake")}"
GEN=(-G Ninja)
common=(-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API
        -DCMAKE_BUILD_TYPE=Release -DANDROID_STL=c++_static)
mkdir -p "$OUT"

if [ ! -f "$OUT/sdl3/install/lib/libSDL3.so" ]; then
  echo "== SDL3"
  "$CMAKE" "${GEN[@]}" -S "$SRC/SDL3-3.4.18" -B "$OUT/sdl3/build" "${common[@]}" -DCMAKE_INSTALL_PREFIX="$OUT/sdl3/install" \
      -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF > "$OUT/sdl3.log"
  "$CMAKE" --build "$OUT/sdl3/build" >> "$OUT/sdl3.log"
  "$CMAKE" --install "$OUT/sdl3/build" >> "$OUT/sdl3.log"
fi

if [ ! -f "$OUT/glslang/install/lib/libglslang.a" ]; then
  echo "== glslang"
  "$CMAKE" "${GEN[@]}" -S "$SRC/glslang-16.6.0" -B "$OUT/glslang/build" "${common[@]}" -DCMAKE_INSTALL_PREFIX="$OUT/glslang/install" \
      -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DGLSLANG_ENABLE_INSTALL=ON -DENABLE_GLSLANG_BINARIES=OFF -DBUILD_SHARED_LIBS=OFF \
      -DENABLE_HLSL=OFF -DENABLE_SPVREMAPPER=OFF > "$OUT/glslang.log"
  "$CMAKE" --build "$OUT/glslang/build" >> "$OUT/glslang.log"
  "$CMAKE" --install "$OUT/glslang/build" >> "$OUT/glslang.log"
fi

if [ ! -f "$OUT/opus/install/lib/libopus.so" ]; then
  echo "== opus"
  "$CMAKE" "${GEN[@]}" -S "$SRC/opus-1.5.2" -B "$OUT/opus/build" "${common[@]}" -DCMAKE_INSTALL_PREFIX="$OUT/opus/install" \
      -DBUILD_SHARED_LIBS=ON -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF > "$OUT/opus.log"
  "$CMAKE" --build "$OUT/opus/build" >> "$OUT/opus.log"
  "$CMAKE" --install "$OUT/opus/build" >> "$OUT/opus.log"
fi
echo "== deps ready in $OUT"
