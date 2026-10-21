#!/system/bin/sh
# Android starts the app's processes through this script (a debuggable APK with wrap.sh in its native library folder): a
# fresh exec instead of a fork of zygote, whose heap reservations cover the guest's fixed addresses on a 39-bit address
# space. libnoaslr.so runs the app without layout randomisation (the system's mappings then stay at the top) and
# libguestreserve.so (android/runtime/guest_reserve.c) reserves the guest's range first thing.
D="$(dirname "$0")"
export LD_PRELOAD="$D/libguestreserve.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$D/libnoaslr.so" "$@"
