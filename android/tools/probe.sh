#!/bin/bash
# Compile runtime sources (staged) for Android arm64 and count errors (porting aid). Args: file names; none = all.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NDK=${NDK:-$HOME/Library/Android/sdk/ndk/29.0.14206865}
CC=$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang
DEPS=${DEPS:-$ROOT/../ssbu-recomp-deps}
STAGE=$ROOT/android/build/stage
python3 "$ROOT/android/tools/stage.py" "$STAGE" > /dev/null
mkdir -p /tmp/osprobe
[ $# -eq 0 ] && set -- $(cat "$ROOT/android/tools/host_sources.txt")
for f in "$@"; do
  b=${f%.*}
  $CC --target=aarch64-linux-android30 -c -O0 -w -fno-strict-aliasing -fshort-wchar -ferror-limit=0 \
    -include "$ROOT/android/compat/android_port.h" -I "$ROOT/android/compat" -I "$ROOT/android/third_party" -I "$STAGE" \
    -I $DEPS/SDL3-3.4.18/include -I $DEPS/Vulkan-Headers-1.4.329/include -I $DEPS/glslang-16.6.0 \
    "$STAGE/$f" -o /tmp/osprobe/$b.o 2> /tmp/osprobe/$b.err
  echo "$f: $? $(grep -c 'error:' /tmp/osprobe/$b.err)"
done
