#!/bin/bash
# Builds the native libraries of the APK (arm64-v8a) from the runtime sources, with the NDK alone:
#   libmain.so          the runtime (runtime/*.c through android/compat) + android/runtime/*.c; SDL_main is its entry
#   libssbu_glslang.so  GLSL -> SPIR-V (runtime/vk_glslang_shim.c + glslang, static)
# plus libSDL3.so, libopus.so and libadrenotools.so with its hook libraries from android/tools/build_deps.sh. Nothing of the
# game is in any of them.
# Output: android/build/lib/arm64-v8a/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="${NDK:-$SDK/ndk/29.0.14206865}"
case "$(uname -s)" in Darwin) HOST=darwin-x86_64 ;; Linux) HOST=linux-x86_64 ;; *) HOST=windows-x86_64 ;; esac
TC="$NDK/toolchains/llvm/prebuilt/$HOST/bin"
API=30
CC="$TC/clang --target=aarch64-linux-android$API"
CXX="$TC/clang++ --target=aarch64-linux-android$API"
B="$ROOT/android/build"
STAGE="$B/stage"
DEPS="$B/deps"
SRCDEPS="${SRCDEPS:-$ROOT/../ssbu-recomp-deps}"
OBJ="$B/obj"
LIB="$B/lib/arm64-v8a"
JOBS="${JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || /usr/sbin/sysctl -n hw.ncpu 2>/dev/null || nproc) | head -1)}"
mkdir -p "$OBJ" "$LIB"

[ -f "$DEPS/sdl3/install/lib/libSDL3.so" ] && [ -f "$DEPS/adrenotools/install/lib/libadrenotools.so" ] || { echo "run android/tools/build_deps.sh first"; exit 1; }
python3 "$ROOT/android/tools/stage.py" "$STAGE"

VKH="$SRCDEPS/Vulkan-Headers-1.4.329/include"
[ -d "$VKH" ] || VKH="$NDK/toolchains/llvm/prebuilt/$HOST/sysroot/usr/include"
# the runtime's own flags (tools/lifter/build.py HOST_FLAGS), the Android compatibility layer, and frame pointers for the
# crash reports' stack walks
CFLAGS=(-c -O1 -g -w -D_GNU_SOURCE -fno-strict-aliasing -fshort-wchar -fPIC -fno-omit-frame-pointer -ffunction-sections
        -include "$ROOT/android/compat/android_port.h" -I "$ROOT/android/compat" -I "$ROOT/android/third_party" -I "$STAGE"
        -I "$DEPS/sdl3/install/include" -I "$VKH" -I "$DEPS/adrenotools/install/include"
        '-Dfn_table=(*game_fn_table_p)' '-Dfn_table_count=(*game_fn_table_count_p)'
        '-Ddyn_syms=(*game_dyn_syms_p)' '-Ddyn_sym_count=(*game_dyn_sym_count_p)')

# tools/lifter/build.py HOST_SOURCES, without osk.c (the Android version is in android/runtime) + gamedll.c, menu_stub.c
HOST_SOURCES="$(python3 - "$STAGE" <<'EOF'
import os, sys
sys.path.insert(0, sys.argv[1])
os.environ.setdefault('IMGUI_ROOT', os.path.join(sys.argv[1], 'no-imgui'))
import build
src = [n for n in build.HOST_SOURCES if n not in ('osk.c', 'net_relay.c')] + ['gamedll.c', 'menu_stub.c', 'net_off.c']      # online play: off, as in the Linux build (net_off.c)
print(' '.join(src))
EOF
)"
COMPAT_SOURCES="win32_compat.c compat_hash.c gl_stub.c waveout_aaudio.c"
ANDROID_SOURCES="android_main.c osk_android.c vk_driver.c gpu_boost.c adpf.c"

compile() {    # src obj [extra flags]
  local src="$1" obj="$2"; shift 2
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$ROOT/android/compat/windows.h" -nt "$obj" ]; then
    $CC "${CFLAGS[@]}" "$@" "$src" -o "$obj" 2> "$obj.log" || { echo "FAILED $src"; cat "$obj.log" | grep -m 20 error; return 1; }
  fi
}
export -f compile 2>/dev/null || true
objs=()
pids=()
n=0
for f in $HOST_SOURCES; do
  extra=()
  [ "$f" = "boot.c" ] && extra=(-Dmain=ssbu_main)
  o="$OBJ/${f%.c}.o"
  # nvn_vk.c includes the other nvn_vk*.c files: rebuild it when any of them changes
  if [ "$f" = "nvn_vk.c" ]; then for g in "$STAGE"/nvn_vk_*.c; do [ "$g" -nt "$o" ] && rm -f "$o"; done; fi
  for h in "$STAGE"/*.h "$STAGE"/*.inc; do [ "$h" -nt "$o" ] && { rm -f "$o"; break; }; done
  compile "$STAGE/$f" "$o" "${extra[@]+"${extra[@]}"}" &
  pids+=($!); objs+=("$o"); n=$((n+1))
  if [ $((n % JOBS)) -eq 0 ]; then for p in "${pids[@]}"; do wait $p || exit 1; done; pids=(); fi
done
for f in $COMPAT_SOURCES; do o="$OBJ/compat_${f%.c}.o"; compile "$ROOT/android/compat/$f" "$o" & pids+=($!); objs+=("$o"); done
for f in $ANDROID_SOURCES; do o="$OBJ/android_${f%.c}.o"; compile "$ROOT/android/runtime/$f" "$o" & pids+=($!); objs+=("$o"); done
for p in "${pids[@]}"; do wait $p || exit 1; done
$CC -c "$ROOT/android/compat/win32_compat_fiber.S" -o "$OBJ/compat_fiber.o"
objs+=("$OBJ/compat_fiber.o")

echo "== link libmain.so (${#objs[@]} objects)"
$CC -shared -o "$LIB/libmain.so" "${objs[@]}" -L"$DEPS/sdl3/install/lib" -lSDL3 -L"$DEPS/adrenotools/install/lib" -ladrenotools -llog -landroid -laaudio -lmediandk -lm -ldl \
    -Wl,--gc-sections -Wl,--no-dependent-libraries -Wl,-z,max-page-size=16384 -Wl,--no-undefined -Wl,-soname,libmain.so

echo "== link libssbu_glslang.so"
GL="$DEPS/glslang/install"
compile "$STAGE/vk_glslang_shim.c" "$OBJ/vk_glslang_shim.o" -I "$GL/include"
$CXX -shared -o "$LIB/libssbu_glslang.so" "$OBJ/vk_glslang_shim.o" -L"$LIB" -lmain \
    -L"$GL/lib" -lglslang -lglslang-default-resource-limits -lSPIRV -lMachineIndependent -lGenericCodeGen -lOSDependent \
    -static-libstdc++ -llog -lm -ldl -Wl,-z,max-page-size=16384 -Wl,-soname,libssbu_glslang.so

cp "$DEPS/sdl3/install/lib/libSDL3.so" "$LIB/"
cp "$DEPS/opus/install/lib/libopus.so" "$LIB/"
cp "$DEPS/adrenotools/install/lib/"*.so "$LIB/"            # libadrenotools.so, libhook_impl.so, lib*_hook.so
"$TC/llvm-strip" --strip-debug "$LIB"/*.so
ls -la "$LIB"
