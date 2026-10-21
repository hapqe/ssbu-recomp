/* Entry point of the Android build (libmain.so, started by SDL3's SDLActivity through SDL_main).
 *
 * The launcher (GameActivity.java) passes, as SDL_main arguments:
 *     argv[1] game folder     the folder the user copied from the PC build (holds work\, binaries\, romfs\ or the RomFS
 *                             named by SSBU_ROMFS, state.json, ...), on shared storage
 *     argv[2] library folder  the app's private copy of work\'s game libraries (*.dll = Android ELF objects built by
 *                             android/tools/build_game_android.py): Android runs no code from shared storage
 *     argv[3..] KEY=VALUE     environment for the runtime (SSBU_* switches, SSBU_LOCALE, SSBU_TZ_NAME, ...)
 * Then: the environment the Windows runtime expects (SSBU_EXE = <game>\work\ssbu_native.exe, the current folder = work),
 * stdout / stderr to <game>/work/ssbu_android_log.txt and to logcat, the game library bound (gamedll.c), and the runtime's
 * main (boot.c, renamed ssbu_main) on a thread with a large stack. This thread stays SDL's event thread: the surface,
 * focus, keys and the back gesture come from SDL events. */
#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int ssbu_main(int argc, char **argv);                 /* boot.c main (-Dmain=ssbu_main) */
const char *game_dll_bind(void);                      /* gamedll.c */
void display_close_request(void);                     /* display.c */

static SDL_Window *g_window;
static ANativeWindow *volatile g_native;
static SRWLOCK g_win_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_win_cv = CONDITION_VARIABLE_INIT;
static char g_game[1024], g_work[1100];

/* ---------------------------------------------------------------- log: a pipe read into logcat and the log file */
static int g_log_fd = -1;
static DWORD WINAPI log_thread(LPVOID arg) {
    int fd = (int)(intptr_t)arg;
    char buf[4096], line[4096];
    size_t n = 0;
    ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0) {
        if (g_log_fd >= 0) write(g_log_fd, buf, (size_t)r);
        for (ssize_t i = 0; i < r; i++) {
            if (buf[i] == '\n' || n == sizeof line - 1) { line[n] = 0; __android_log_write(ANDROID_LOG_INFO, "OpenSmash", line); n = 0; }
            if (buf[i] != '\n') line[n++] = buf[i];
        }
    }
    return 0;
}
static void log_start(void) {
    char path[1200];
    snprintf(path, sizeof path, "%s/ssbu_android_log.txt", g_work);
    g_log_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    int p[2];
    if (pipe(p)) return;
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[1]);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    CloseHandle(CreateThread(NULL, 0, log_thread, (LPVOID)(intptr_t)p[0], 0, NULL));
}

/* ---------------------------------------------------------------- JNI: dialogs of the activity */
static jclass activity_class(JNIEnv *env) {
    jobject act = (jobject)SDL_GetAndroidActivity();
    if (!act) return NULL;
    jclass c = (*env)->GetObjectClass(env, act);
    (*env)->DeleteLocalRef(env, act);
    return c;
}
static jstring jstr(JNIEnv *env, const char *s) { return (*env)->NewStringUTF(env, s ? s : ""); }
void compat_message_box(const char *title, const char *text) {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jclass c = activity_class(env);
    jmethodID m = c ? (*env)->GetStaticMethodID(env, c, "showMessage", "(Ljava/lang/String;Ljava/lang/String;)V") : NULL;
    if (m) {
        jstring a = jstr(env, title), b = jstr(env, text);
        (*env)->CallStaticVoidMethod(env, c, m, a, b);
        (*env)->DeleteLocalRef(env, a); (*env)->DeleteLocalRef(env, b);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (c) (*env)->DeleteLocalRef(env, c);
}
/* blocks until the user answers; 1 = OK with the text in out, 0 = cancelled */
int android_text_input(const char *header, const char *guide, const char *initial, int max_len, int min_len, int password, char *out, int out_size) {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    int ok = 0;
    if (!env) return 0;
    jclass c = activity_class(env);
    jmethodID m = c ? (*env)->GetStaticMethodID(env, c, "textInput", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZ)Ljava/lang/String;") : NULL;
    if (m) {
        jstring a = jstr(env, header), b = jstr(env, guide), i = jstr(env, initial);
        jstring r = (jstring)(*env)->CallStaticObjectMethod(env, c, m, a, b, i, (jint)max_len, (jint)min_len, (jboolean)(password != 0));
        if (r) {
            const char *u = (*env)->GetStringUTFChars(env, r, NULL);
            snprintf(out, (size_t)out_size, "%s", u ? u : "");
            if (u) (*env)->ReleaseStringUTFChars(env, r, u);
            (*env)->DeleteLocalRef(env, r);
            ok = 1;
        }
        (*env)->DeleteLocalRef(env, a); (*env)->DeleteLocalRef(env, b); (*env)->DeleteLocalRef(env, i);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (c) (*env)->DeleteLocalRef(env, c);
    return ok;
}
/* 1 = yes / OK, 0 = no */
int android_message(const char *text, int question) {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    int r = 1;
    if (!env) return 1;
    jclass c = activity_class(env);
    jmethodID m = c ? (*env)->GetStaticMethodID(env, c, "askMessage", "(Ljava/lang/String;Z)Z") : NULL;
    if (m) { jstring a = jstr(env, text); r = (*env)->CallStaticBooleanMethod(env, c, m, a, (jboolean)(question != 0)) ? 1 : 0; (*env)->DeleteLocalRef(env, a); }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (c) (*env)->DeleteLocalRef(env, c);
    return r;
}

/* ---------------------------------------------------------------- the surface */
void *android_native_window(void) {
    AcquireSRWLockExclusive(&g_win_lock);
    while (!g_native) SleepConditionVariableSRW(&g_win_cv, &g_win_lock, INFINITE, 0);
    void *w = g_native;
    ReleaseSRWLockExclusive(&g_win_lock);
    return w;
}
static void native_window_update(void) {
    ANativeWindow *w = g_window ? (ANativeWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL) : NULL;
    AcquireSRWLockExclusive(&g_win_lock);
    if (w && w != g_native) {
        if (g_native) printf("android: the activity's surface changed (%p -> %p)\n", (void *)g_native, (void *)w);
        g_native = w;
    }
    ReleaseSRWLockExclusive(&g_win_lock);
    WakeAllConditionVariable(&g_win_cv);
}

/* ---------------------------------------------------------------- keys: SDL keycodes -> Windows virtual keys */
static int vk_of(SDL_Keycode k, SDL_Scancode sc) {
    if (k >= 'a' && k <= 'z') return (int)(k - 'a' + 'A');
    if (k >= '0' && k <= '9') return (int)k;
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return VK_F1 + (sc - SDL_SCANCODE_F1);
    switch (k) {
    case SDLK_RETURN: return VK_RETURN; case SDLK_ESCAPE: return VK_ESCAPE; case SDLK_BACKSPACE: return VK_BACK; case SDLK_TAB: return VK_TAB;
    case SDLK_SPACE: return VK_SPACE; case SDLK_LEFT: return VK_LEFT; case SDLK_RIGHT: return VK_RIGHT; case SDLK_UP: return VK_UP; case SDLK_DOWN: return VK_DOWN;
    case SDLK_LSHIFT: return VK_LSHIFT; case SDLK_RSHIFT: return VK_RSHIFT; case SDLK_LCTRL: return VK_LCONTROL; case SDLK_RCTRL: return VK_RCONTROL;
    case SDLK_LALT: return VK_LMENU; case SDLK_RALT: return VK_RMENU; case SDLK_HOME: return VK_HOME; case SDLK_END: return VK_END; case SDLK_DELETE: return VK_DELETE;
    case SDLK_PAGEUP: return VK_PRIOR; case SDLK_PAGEDOWN: return VK_NEXT; case SDLK_INSERT: return VK_INSERT;
    case SDLK_COMMA: return VK_OEM_COMMA; case SDLK_PERIOD: return VK_OEM_PERIOD; case SDLK_MINUS: return VK_OEM_MINUS; case SDLK_EQUALS: return VK_OEM_PLUS;
    case SDLK_SEMICOLON: return VK_OEM_1; case SDLK_SLASH: return VK_OEM_2; case SDLK_GRAVE: return VK_OEM_3; case SDLK_LEFTBRACKET: return VK_OEM_4;
    case SDLK_BACKSLASH: return VK_OEM_5; case SDLK_RIGHTBRACKET: return VK_OEM_6; case SDLK_APOSTROPHE: return VK_OEM_7;
    case SDLK_KP_0: return VK_NUMPAD0; case SDLK_KP_1: return VK_NUMPAD1; case SDLK_KP_2: return VK_NUMPAD2; case SDLK_KP_3: return VK_NUMPAD3; case SDLK_KP_4: return VK_NUMPAD4;
    case SDLK_KP_5: return VK_NUMPAD5; case SDLK_KP_6: return VK_NUMPAD6; case SDLK_KP_7: return VK_NUMPAD7; case SDLK_KP_8: return VK_NUMPAD8; case SDLK_KP_9: return VK_NUMPAD9;
    case SDLK_KP_ENTER: return VK_RETURN;
    }
    return 0;
}
/* SDL events are taken by a watch (it runs when an event is queued), because nnpad_sdl.c flushes SDL's queue at every
 * poll on the game's thread: the keyboard, the focus and the surface must not depend on who reads the queue first. */
static bool SDLCALL event_watch(void *user, SDL_Event *e) {
    (void)user;
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
        if (e->key.key == SDLK_AC_BACK) {                            /* the back gesture: the game decides (exit request) */
            if (e->type == SDL_EVENT_KEY_UP) { printf("android: back: exit request\n"); display_close_request(); }
            break;
        }
        int vk = vk_of(e->key.key, e->key.scancode);
        if (vk) compat_set_key(vk, e->type == SDL_EVENT_KEY_DOWN);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED: compat_set_focus(1); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: compat_set_focus(0); break;
    case SDL_EVENT_DID_ENTER_FOREGROUND: compat_set_focus(1); native_window_update(); break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND: compat_set_focus(0); break;
    case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: case SDL_EVENT_WINDOW_SHOWN: native_window_update(); break;
    case SDL_EVENT_QUIT: case SDL_EVENT_TERMINATING: printf("android: the activity ends\n"); display_close_request(); break;
    default: break;
    }
    return true;
}

/* ---------------------------------------------------------------- the runtime's thread */
static int g_argc;
static char *g_argv[8];
static DWORD WINAPI runtime_thread(LPVOID arg) {
    (void)arg;
    int code = ssbu_main(g_argc, g_argv);
    printf("android: the runtime returned %d\n", code);
    fflush(stdout);
    ExitProcess((UINT)code);
}
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static int vpad_attach(void);
int SDL_main(int argc, char *argv[]) {
    if (argc < 3) { __android_log_print(ANDROID_LOG_FATAL, "OpenSmash", "SDL_main: game folder and library folder missing"); return 1; }
    snprintf(g_game, sizeof g_game, "%s", argv[1]);
    snprintf(g_work, sizeof g_work, "%s/work", g_game);
    setenv("SSBU_LIBDIR", argv[2], 1);
    for (int i = 3; i < argc; i++) { const char *eq = strchr(argv[i], '='); if (eq) { char k[128]; snprintf(k, sizeof k, "%.*s", (int)(eq - argv[i]), argv[i]); setenv(k, eq + 1, 1); } }
    char exe[1200];
    snprintf(exe, sizeof exe, "%s/ssbu_native.exe", g_work);
    setenv("SSBU_EXE", exe, 1);
    if (chdir(g_work)) { __android_log_print(ANDROID_LOG_FATAL, "OpenSmash", "no folder %s", g_work); compat_message_box("OpenSmash", "The game folder has no work folder: copy the whole build folder of the PC (see the README)."); SDL_Delay(8000); return 1; }
    log_start();
    printf("OpenSmash for Android: game folder %s, libraries %s\n", g_game, argv[2]);
    {   /* the RomFS: <game>/romfs unless SSBU_ROMFS names another folder (state.json names the PC's folder) */
        char romfs[1200];
        snprintf(romfs, sizeof romfs, "%s/romfs", g_game);
        if (!getenv("SSBU_ROMFS") && exists(romfs)) setenv("SSBU_ROMFS", romfs, 1);
    }

    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");                 /* the game's threads run on; this thread only watches events */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) { printf("android: SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    if (getenv("SSBU_TOUCH_PAD")) vpad_attach();                       /* the on-screen pad is player 1 (see vpad_attach) */
    SDL_AddEventWatch(event_watch, NULL);
    g_window = SDL_CreateWindow("OpenSmash", 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) { printf("android: SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }
    native_window_update();

    const char *err = game_dll_bind();
    if (err) {
        printf("android: %s\n", err);
        compat_message_box("OpenSmash: the game library is missing", err);
        for (;;) { SDL_Event e; if (SDL_WaitEvent(&e) && (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_TERMINATING)) break; }
        return 2;
    }
    /* what scripts/run.py --play sets on Windows */
    static char elf[1300];
    {   char version[32] = "13.0.4", path[1200];
        snprintf(path, sizeof path, "%s/state.json", g_game);
        FILE *f = fopen(path, "rb");
        if (f) {
            static char text[1 << 16];
            size_t n = fread(text, 1, sizeof text - 1, f);
            text[n] = 0;
            fclose(f);
            const char *v = strstr(text, "\"game_version\"");
            if (v && (v = strchr(v + 14, '"'))) { int k = 0; for (v++; *v && *v != '"' && k < 31; v++) version[k++] = *v; version[k] = 0; }
        }
        const char *e = getenv("SSBU_MAIN_ELF");
        if (e && e[0]) snprintf(elf, sizeof elf, "%s", e);
        else snprintf(elf, sizeof elf, "%s/binaries/%s/main.elf", g_game, version);
        snprintf(path, sizeof path, "%s/saves", g_game);
        mkdir(path, 0777);
        if (!getenv("SSBU_USERDATA")) setenv("SSBU_USERDATA", path, 1);
        snprintf(path, sizeof path, "%s/sysdata/fonts", g_game);
        if (!getenv("SSBU_FONTS") && exists(path)) setenv("SSBU_FONTS", path, 1);
        snprintf(path, sizeof path, "%s/sysdata/zoneinfo", g_game);
        if (!getenv("SSBU_TZDIR") && exists(path)) setenv("SSBU_TZDIR", path, 1);
        snprintf(path, sizeof path, "%s/cache", g_game);
        mkdir(path, 0777);
        printf("android: game version %s, saves in %s/saves\n", version, g_game);
    }
    g_argc = 2;
    g_argv[0] = exe;
    g_argv[1] = elf;
    {   const char *sec = getenv("SSBU_SECONDS");                    /* tests: the run ends after that many seconds, with its reports (boot.c argv[2]) */
        if (sec && sec[0]) { g_argv[2] = (char *)sec; g_argc = 3; } }
    __argc = g_argc;                                                   /* as the Windows C runtime sets them (sdklift_rt.c finds sdk.elf beside main.elf) */
    __argv = g_argv;
    CloseHandle(CreateThread(NULL, (SIZE_T)64 << 20, runtime_thread, NULL, 0, NULL));
    for (;;) {                                                         /* keep SDL's Android glue serviced */
        SDL_PumpEvents();
        SDL_Delay(8);
    }
    return 0;
}

/* ---------------------------------------------------------------- on-screen controls: a virtual SDL gamepad
 * TouchControls.java -> GameActivity.nativeTouchPad (UI thread). With the overlay on (SSBU_TOUCH_PAD, GameActivity) the pad
 * is attached at the start, before the runtime looks for pads, so it is player 1 and the keyboard takes the next slot (it
 * was attached on the first touch, after the keyboard had become player 1: the game's player-1 screens ignored it). With
 * the overlay hidden there is no virtual pad at all. Bits = SDL_GamepadButton; 16 / 17 = ZL / ZR. */
static SDL_Joystick *g_vpad;
static int vpad_attach(void) {
    if (!g_vpad) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.vendor_id = 0x057e; desc.product_id = 0x2009;      /* reported as a Pro Controller: its face labels are the Switch ones (A east, B south) */
        desc.name = "OpenSmash touch controls";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        if (!id) { printf("android: the virtual pad cannot be attached: %s\n", SDL_GetError()); return 0; }
        g_vpad = SDL_OpenJoystick(id);
        if (!g_vpad) { printf("android: the virtual pad cannot be opened: %s\n", SDL_GetError()); return 0; }
        printf("android: on-screen controls attached as a virtual gamepad\n");
    }
    return g_vpad != NULL;
}
JNIEXPORT void JNICALL Java_me_hapke_opensmash_GameActivity_nativeTouchPad(JNIEnv *env, jclass cls, jint buttons, jfloat lx, jfloat ly, jfloat rx, jfloat ry) {
    (void)env; (void)cls;
    if (!vpad_attach()) return;
    for (int b = 0; b < 15; b++) SDL_SetJoystickVirtualButton(g_vpad, b, (buttons >> b & 1) != 0);
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_LEFTX, (Sint16)(lx * 32767));
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_LEFTY, (Sint16)(ly * 32767));
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_RIGHTX, (Sint16)(rx * 32767));
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_RIGHTY, (Sint16)(ry * 32767));
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, (buttons >> 16 & 1) ? 32767 : 0);
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, (buttons >> 17 & 1) ? 32767 : 0);
}
