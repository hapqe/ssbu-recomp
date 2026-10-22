#!/bin/bash
# Builds OpenSmash.apk (arm64-v8a) with the Android SDK tools alone (no Gradle):
#   1. android/tools/build_deps.sh    SDL3, glslang, libopus for Android (once)
#   2. android/tools/build_native.sh  libmain.so (the runtime), libssbu_glslang.so
#   3. libdevcheck.so, the Java side (launcher, game activity, SDL3's Java glue), resources, signing
# Needs: Android SDK (build-tools, platform 36), NDK 29, JDK 17, python3, and the sources of SDL3 3.4.18, glslang 16.6.0,
# opus 1.5.2, Vulkan-Headers 1.4.329 and libadrenotools (commit 8fae8ce, android/tools/build_deps.sh) in DEPS (default: ../ssbu-recomp-deps beside the repository).
# Output: android/build/OpenSmash.apk. Nothing of the game is in it.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cd .. && pwd)"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
BT="$SDK/build-tools/36.0.0"
PLATFORM="$SDK/platforms/android-36/android.jar"
NDK="${NDK:-$SDK/ndk/29.0.14206865}"
DEPS="${DEPS:-$ROOT/../ssbu-recomp-deps}"
export SRCDEPS="$DEPS"
if [ -z "${JAVA_HOME:-}" ]; then for j in /opt/homebrew/opt/openjdk@17 /usr/lib/jvm/java-17-openjdk-amd64; do [ -d "$j" ] && export JAVA_HOME="$j" && break; done; fi
JAVA_BIN="${JAVA_HOME:+$JAVA_HOME/bin/}"
KS="${KEYSTORE:-$HOME/.android/debug.keystore}"
OUT=build/apk
case "$(uname -s)" in Darwin) HOST=darwin-x86_64 ;; Linux) HOST=linux-x86_64 ;; *) HOST=windows-x86_64 ;; esac

tools/build_deps.sh "$DEPS"
tools/build_native.sh
echo "== libguestreserve.so, libnoaslr.so, wrap.sh"
# the guest's fixed address range, reserved before anything else maps there (runtime/guest_reserve.c); wrap.sh preloads it
"$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang" --target=aarch64-linux-android30 -shared -fPIC -O2 runtime/guest_reserve.c \
    -o build/lib/arm64-v8a/libguestreserve.so -llog -Wl,-z,max-page-size=16384 -Wl,-soname,libguestreserve.so
"$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang" --target=aarch64-linux-android30 -fPIE -pie -O2 runtime/noaslr.c \
    -o build/lib/arm64-v8a/libnoaslr.so -Wl,-z,max-page-size=16384
echo "== libdevcheck.so"
VKH="$DEPS/Vulkan-Headers-1.4.329/include"
AT=build/deps/adrenotools/install
"$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang" --target=aarch64-linux-android30 -shared -fPIC -O2 -I "$VKH" -I "$AT/include" \
    runtime/devcheck.c runtime/vk_driver.c -o build/lib/arm64-v8a/libdevcheck.so -L"$AT/lib" -ladrenotools -ldl -Wl,-z,max-page-size=16384 -Wl,-soname,libdevcheck.so

rm -rf "$OUT"
mkdir -p "$OUT/classes" "$OUT/dex" "$OUT/gen" "$OUT/src"
cp -R app/src/. "$OUT/src/"
mkdir -p "$OUT/src/org/libsdl/app"
cp "$DEPS/SDL3-3.4.18/android-project/app/src/main/java/org/libsdl/app/"*.java "$OUT/src/org/libsdl/app/"
[ -f app/res/mipmap-mdpi/ic_launcher.png ] || python3 tools/make_icon.py app/res

echo "== aapt2"
"$BT/aapt2" compile --dir app/res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM" --manifest app/AndroidManifest.xml \
    -R "$OUT/res.zip" --auto-add-overlay --java "$OUT/gen"

echo "== javac"
find "$OUT/src" "$OUT/gen" -name '*.java' > "$OUT/sources.txt"
"${JAVA_BIN}javac" -nowarn --release 11 -encoding UTF-8 -cp "$PLATFORM" -d "$OUT/classes" @"$OUT/sources.txt" 2>&1 | grep -v '^Note:' || true
[ -f "$OUT/classes/me/hapke/opensmash/GameActivity.class" ] || { echo "javac failed"; exit 1; }

echo "== d8"
"$BT/d8" --release --min-api 30 --lib "$PLATFORM" --output "$OUT/dex" $(find "$OUT/classes" -name '*.class')
(cd "$OUT/dex" && zip -q ../base.apk classes*.dex)
mkdir -p "$OUT/lib/arm64-v8a"
cp build/lib/arm64-v8a/*.so "$OUT/lib/arm64-v8a/"
install -m 755 app/wrap.sh "$OUT/lib/arm64-v8a/wrap.sh"     # debuggable APK: the app starts through it (runtime/guest_reserve.c)
(cd "$OUT" && zip -q -r base.apk lib)

echo "== align + sign"
"$BT/zipalign" -f -P 16 4 "$OUT/base.apk" "$OUT/aligned.apk"
if [ ! -f "$KS" ]; then
  mkdir -p "$(dirname "$KS")"
  "${JAVA_BIN}keytool" -genkeypair -keystore "$KS" -storepass android -keypass android -alias androiddebugkey -dname "CN=Android Debug,O=Android,C=US" \
      -keyalg RSA -keysize 2048 -validity 10000 >/dev/null
fi
# no v4 signature (.idsig): with one, adb installs incrementally and leaves wrap.sh out of the installed library folder,
# so the game process is a zygote fork and the guest range cannot be reserved ("cannot map guest image")
"$BT/apksigner" sign --v4-signing-enabled false --ks "$KS" --ks-key-alias "${KEY_ALIAS:-androiddebugkey}" --ks-pass "pass:${KS_PASS:-android}" --key-pass "pass:${KEY_PASS:-android}" \
    --out build/OpenSmash.apk "$OUT/aligned.apk"
ls -la build/OpenSmash.apk
echo "== done: android/build/OpenSmash.apk"
