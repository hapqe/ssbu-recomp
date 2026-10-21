/* libdevcheck.so: what the launcher shows about this device's Vulkan driver, with the needs of the Vulkan back end
 * (runtime/nvn_vk_draw.c vkd_device_info): Vulkan 1.3 with dynamicRendering + synchronization2 (required),
 * VK_EXT_vertex_input_dynamic_state (required by the back end today), BC texture compression (decoded on the CPU when
 * the GPU has none: phones have ASTC instead), and the optional extensions the back end uses when they are there.
 * The driver checked is the one the game will use: the system's or the custom one installed by the launcher (vk_driver.c).
 * Built also as a command-line tool (-DDEVCHECK_MAIN, args: [driver .so]) for adb shell; the adrenotools hook libraries
 * must then lie beside it. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifndef DEVCHECK_MAIN
#include <jni.h>
#endif

static char g_out[16384];
static size_t g_n;
static void out(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(g_out + g_n, sizeof g_out - g_n, fmt, ap);
    va_end(ap);
    if (k > 0) g_n += (size_t)k < sizeof g_out - g_n ? (size_t)k : sizeof g_out - g_n - 1;
}
void *android_vulkan_load(const char *driver);           /* vk_driver.c */
const char *android_vulkan_note(void);
static int has(const VkExtensionProperties *e, uint32_t n, const char *name) { for (uint32_t i = 0; i < n; i++) if (!strcmp(e[i].extensionName, name)) return 1; return 0; }

const char *devcheck_report(const char *driver) {
    g_n = 0; g_out[0] = 0;
    void *lib = android_vulkan_load(driver);
    out("Vulkan driver: %s\n", android_vulkan_note());
    if (!lib) { out("✗ no Vulkan loader (libvulkan.so)\n"); return g_out; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "opensmash-devcheck", 1, "none", 1, VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL};
    VkInstance inst;
    if (!create || create(&ici, NULL, &inst) != VK_SUCCESS) { out("✗ vkCreateInstance failed (no Vulkan 1.3 driver?)\n"); return g_out; }
#define I(name) PFN_##name name = (PFN_##name)gipa(inst, #name)
    I(vkEnumeratePhysicalDevices); I(vkGetPhysicalDeviceProperties); I(vkGetPhysicalDeviceFeatures2); I(vkEnumerateDeviceExtensionProperties);
    I(vkGetPhysicalDeviceMemoryProperties); I(vkDestroyInstance);
    VkPhysicalDevice gpus[8];
    uint32_t ngpu = 8;
    vkEnumeratePhysicalDevices(inst, &ngpu, gpus);
    if (!ngpu) { out("✗ no Vulkan device\n"); vkDestroyInstance(inst, NULL); return g_out; }
    VkPhysicalDevice g = gpus[0];
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(g, &p);
    out("GPU: %s\nVulkan %u.%u.%u, driver 0x%x\n", p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g, &mp);
    unsigned long long heap = 0;
    for (uint32_t i = 0; i < mp.memoryHeapCount; i++) if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT && mp.memoryHeaps[i].size > heap) heap = mp.memoryHeaps[i].size;
    out("GPU memory heap: %llu MB\n", heap >> 20);
    static VkExtensionProperties e[512];
    uint32_t ne = 512;
    vkEnumerateDeviceExtensionProperties(g, NULL, &ne, e);
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vids = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT};
    VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, has(e, ne, "VK_EXT_vertex_input_dynamic_state") ? &vids : NULL};
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13};
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12};
    vkGetPhysicalDeviceFeatures2(g, &f2);
    int v13 = VK_API_VERSION_MINOR(p.apiVersion) >= 3 || VK_API_VERSION_MAJOR(p.apiVersion) > 1;
    int ok = 1;
#define REQ(c, what) do { int c_ = (c); out("%s %s\n", c_ ? "✓" : "✗", what); ok &= c_; } while (0)
#define OPT(c, what) out("%s %s\n", (c) ? "✓" : "–", what)
    REQ(v13, "Vulkan 1.3");
    REQ(f13.dynamicRendering && f13.synchronization2, "dynamic rendering + synchronization2");
    REQ(has(e, ne, "VK_EXT_vertex_input_dynamic_state") && vids.vertexInputDynamicState, "VK_EXT_vertex_input_dynamic_state");
    out("%s BC texture compression%s\n", f2.features.textureCompressionBC ? "✓" : "–", f2.features.textureCompressionBC ? "" : " (no: the game's BC textures are decoded on the CPU, slower loading)");
    OPT(f2.features.textureCompressionASTC_LDR, "ASTC texture compression");
    OPT(has(e, ne, "VK_EXT_extended_dynamic_state3"), "VK_EXT_extended_dynamic_state3");
    OPT(has(e, ne, "VK_KHR_push_descriptor"), "VK_KHR_push_descriptor");
    OPT(has(e, ne, "VK_EXT_custom_border_color"), "VK_EXT_custom_border_color");
    OPT(has(e, ne, "VK_EXT_index_type_uint8"), "VK_EXT_index_type_uint8");
    OPT(has(e, ne, "VK_EXT_provoking_vertex"), "VK_EXT_provoking_vertex");
    OPT(has(e, ne, "VK_EXT_graphics_pipeline_library"), "VK_EXT_graphics_pipeline_library");
    OPT(f2.features.logicOp, "logic operations");
    OPT(f2.features.depthClamp, "depth clamp");
    OPT(f12.samplerFilterMinmax, "min/max sampler reduction");
    out("%s\n", ok ? "=> this GPU has what the Vulkan back end needs"
                  : driver && driver[0] ? "=> this driver lacks something the Vulkan back end needs today (✗): the game will stop with a message naming it"
                  : "=> this driver lacks something the Vulkan back end needs today (✗): install a custom driver (Turnip for Adreno) above");
    vkDestroyInstance(inst, NULL);
    return g_out;
}

#ifdef DEVCHECK_MAIN
int main(int argc, char **argv) { fputs(devcheck_report(argc > 1 ? argv[1] : NULL), stdout); return 0; }
#else
JNIEXPORT jstring JNICALL Java_me_hapke_opensmash_DeviceCheck_nativeReport(JNIEnv *env, jclass cls, jstring driver) {
    (void)cls;
    const char *d = driver ? (*env)->GetStringUTFChars(env, driver, NULL) : NULL;
    jstring r = (*env)->NewStringUTF(env, devcheck_report(d));
    if (d) (*env)->ReleaseStringUTFChars(env, driver, d);
    return r;
}
#endif
