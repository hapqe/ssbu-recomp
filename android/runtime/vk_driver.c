/* The Vulkan driver of the Android build: the system's (libvulkan.so), or a custom one the launcher installed into the
 * app's private storage (a Mesa Turnip build for Adreno, from an AdrenoTools driver package: meta.json + the .so).
 *
 * Many phones ship an Adreno driver below what the Vulkan back end needs (Vulkan 1.3, dynamic rendering, synchronization2,
 * VK_EXT_vertex_input_dynamic_state; android/runtime/devcheck.c). libadrenotools (bylaws, BSD-2) loads a private copy of
 * the system's Vulkan loader in a linker namespace of its own whose HAL is the custom driver: the handle exports the
 * whole Vulkan API like libvulkan.so, so nvn_vk.c and devcheck.c only take their symbols from another handle. Its hook
 * libraries (libmain_hook.so, libhook_impl.so, ...) are in the APK and must lie in the app's native library folder
 * (android:extractNativeLibs="true"), which is the folder of this library.
 *
 * Used by libmain.so (nvn_vk.c, driver from SSBU_VK_DRIVER) and libdevcheck.so (the launcher passes the driver).
 * Without a usable custom driver: the system's, and the reason in `note`. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <adrenotools/driver.h>

static char g_note[512];

const char *android_vulkan_note(void) { return g_note; }

/* driver: full path of the custom driver's .so in private storage, NULL or "" = the system's */
void *android_vulkan_load(const char *driver) {
    g_note[0] = 0;
    if (driver && driver[0]) {
        struct stat st;
        char dir[1024], hooks[1024];
        const char *slash = strrchr(driver, '/');
        Dl_info di;
        if (stat(driver, &st)) snprintf(g_note, sizeof g_note, "custom driver %s not found: the system's driver is used", driver);
        else if (!slash || slash - driver >= (int)sizeof dir - 1) snprintf(g_note, sizeof g_note, "custom driver path %s is not absolute: the system's driver is used", driver);
        else if (!dladdr((void *)android_vulkan_load, &di) || !di.dli_fname || !strrchr(di.dli_fname, '/'))
            snprintf(g_note, sizeof g_note, "the app's native library folder is unknown: the system's driver is used");
        else {
            snprintf(dir, sizeof dir, "%.*s", (int)(slash - driver + 1), driver);              /* with the '/' adrenotools expects */
            snprintf(hooks, sizeof hooks, "%.*s", (int)(strrchr(di.dli_fname, '/') - di.dli_fname + 1), di.dli_fname);
            void *h = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM, NULL, hooks, dir, slash + 1, NULL, NULL);
            if (h && dlsym(h, "vkGetInstanceProcAddr")) {
                snprintf(g_note, sizeof g_note, "custom driver %s", slash + 1);
                return h;
            }
            snprintf(g_note, sizeof g_note, "custom driver %s could not be loaded (adrenotools, hooks in %s): the system's driver is used", slash + 1, hooks);
        }
    }
    void *h = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (h && !g_note[0]) snprintf(g_note, sizeof g_note, "system driver");
    return h;
}

/* nvn_vk.c: the driver the launcher chose (SSBU_VK_DRIVER), printed into the game log */
void *android_vulkan_open(void) {
    void *h = android_vulkan_load(getenv("SSBU_VK_DRIVER"));
    printf("nvn_vk: Vulkan driver: %s\n", g_note);
    fflush(stdout);
    return h;
}

void *android_vulkan_sym(void *lib, const char *name) { return lib ? dlsym(lib, name) : NULL; }
