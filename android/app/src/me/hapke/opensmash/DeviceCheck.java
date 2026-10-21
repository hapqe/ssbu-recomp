package me.hapke.opensmash;

import android.os.Build;

/** What the Vulkan driver the game will use (the system's, or the custom one: driver) offers, against the needs of the
 *  runtime's Vulkan back end (android/runtime/devcheck.c). */
final class DeviceCheck {
    private DeviceCheck() { }
    private static native String nativeReport(String driver);

    static String report(String driver) {
        StringBuilder sb = new StringBuilder();
        sb.append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" (").append(Build.DEVICE).append("), Android ")
          .append(Build.VERSION.RELEASE).append(", ").append(Build.SUPPORTED_ABIS[0]).append('\n');
        long mem = 0;
        try {
            for (String l : new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get("/proc/meminfo"))).split("\n"))
                if (l.startsWith("MemTotal:")) mem = Long.parseLong(l.replaceAll("[^0-9]", "")) >> 10;
        } catch (Exception ignored) { }
        sb.append("RAM: ").append(mem).append(" MB").append(mem > 0 && mem < 7000 ? "  (8 GB or more is recommended)" : "").append('\n');
        if (!"arm64-v8a".equals(Build.SUPPORTED_ABIS[0])) sb.append("✗ this port needs a 64-bit ARM device (arm64-v8a)\n");
        try {
            System.loadLibrary("devcheck");
            sb.append(nativeReport(driver));
        } catch (Throwable t) {
            sb.append("Vulkan check not available: ").append(t.getMessage()).append('\n');
        }
        return sb.toString();
    }
}
