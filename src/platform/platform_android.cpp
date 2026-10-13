// FIGHTO - Android backend. No game engine and no NDK glue library: a small Java
// activity (android/java) hosts a GLSurfaceView (OpenGL ES 3) and an AudioTrack and
// calls into this library through JNI. System libraries (GLESv3, EGL, libandroid,
// liblog) are resolved at runtime with dlopen/dlsym.
#include "platform.h"
#include "../core/vmath.h"
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Dynamically resolved system APIs
// ---------------------------------------------------------------------------
struct AAssetManager;
struct AAsset;
typedef AAssetManager* (*PFN_AAssetManager_fromJava)(JNIEnv*, jobject);
typedef AAsset* (*PFN_AAssetManager_open)(AAssetManager*, const char*, int);
typedef long long (*PFN_AAsset_getLength64)(AAsset*);
typedef int (*PFN_AAsset_read)(AAsset*, void*, size_t);
typedef void (*PFN_AAsset_close)(AAsset*);
typedef int (*PFN_log_write)(int prio, const char* tag, const char* text);
typedef void* (*PFN_eglGetProcAddress)(const char*);

static void* g_glesLib;
static PFN_eglGetProcAddress p_eglGetProcAddress;
static PFN_AAssetManager_fromJava p_AAssetManager_fromJava;
static PFN_AAssetManager_open p_AAssetManager_open;
static PFN_AAsset_getLength64 p_AAsset_getLength64;
static PFN_AAsset_read p_AAsset_read;
static PFN_AAsset_close p_AAsset_close;
static PFN_log_write p_log_write;

static AAssetManager* g_assets;
static jobject g_assetsRef;
static char g_filesDir[512];
static int g_logFd = -1;

static bool g_inited;
static bool g_quit;
static int g_w = 1280, g_h = 720;
static double g_lastFrame;
static InputState g_input;

// ---------------------------------------------------------------------------
// Platform services
// ---------------------------------------------------------------------------
void plat_log(const char* msg, bool error) {
    if (p_log_write) p_log_write(error ? 6 /*ANDROID_LOG_ERROR*/ : 4 /*ANDROID_LOG_INFO*/, "FIGHTO", msg);
    if (g_logFd >= 0) {
        if (error) write(g_logFd, "ERROR: ", 7);
        write(g_logFd, msg, strlen(msg));
        write(g_logFd, "\n", 1);
    }
}

double plat_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void* plat_gl_proc(const char* name) {
    void* p = g_glesLib ? dlsym(g_glesLib, name) : 0;
    if (!p && p_eglGetProcAddress) p = p_eglGetProcAddress(name);
    return p;
}

bool plat_read_asset(const char* path, Array<u8>* out) {
    if (!g_assets || !p_AAssetManager_open) return false;
    AAsset* a = p_AAssetManager_open(g_assets, path, 2 /*AASSET_MODE_BUFFER*/);
    if (!a) return false;
    long long len = p_AAsset_getLength64(a);
    if (len < 0 || len > (64ll << 20)) { p_AAsset_close(a); return false; }
    out->clear();
    out->resize((int)len + 1);
    int got = 0;
    while (got < (int)len) {
        int n = p_AAsset_read(a, out->data + got, (size_t)((int)len - got));
        if (n <= 0) break;
        got += n;
    }
    p_AAsset_close(a);
    if (got != (int)len) { out->free_mem(); return false; }
    out->data[len] = 0;  // keep text assets NUL-terminated
    out->count = (int)len;
    return true;
}

static void user_path(char* out, int cap, const char* name) { fmt_str(out, cap, "%s/%s", g_filesDir, name); }
bool plat_read_user_file(const char* name, Array<u8>* out);

bool plat_write_user_file(const char* name, const void* data, size_t size) {
    char path[640], tmp[660];
    user_path(path, sizeof(path), name);
    fmt_str(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    const u8* p = (const u8*)data;
    size_t left = size;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n <= 0) { close(fd); unlink(tmp); return false; }
        p += n;
        left -= (size_t)n;
    }
    bool ok = fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); log_error("Write failed for %s", path); return false; }
    // Verify by reading back: a save is only reported as successful if it really landed
    Array<u8> check = {};
    bool same = plat_read_user_file(name, &check) && check.count == (int)size && (size == 0 || memcmp(check.data, data, size) == 0);
    check.free_mem();
    if (!same) log_error("Verification failed for %s", path);
    return same;
}

bool plat_read_user_file(const char* name, Array<u8>* out) {
    char path[640];
    user_path(path, sizeof(path), name);
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) return false;
    out->clear();
    u8 buf[16384];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) { close(fd); out->free_mem(); return false; }
        if (n == 0) break;
        int at = out->count;
        out->resize(at + (int)n);
        memcpy(out->data + at, buf, (size_t)n);
    }
    close(fd);
    out->push(0);
    out->count--;
    return true;
}

bool plat_user_file_exists(const char* name) {
    char path[640];
    user_path(path, sizeof(path), name);
    return access(path, F_OK) == 0;
}

bool plat_copy_user_file(const char* from, const char* to) {
    Array<u8> d = {};
    if (!plat_read_user_file(from, &d)) return false;
    bool ok = plat_write_user_file(to, d.data, (size_t)d.count);
    d.free_mem();
    return ok;
}

void plat_capture_mouse(bool capture) { FIGHTO_UNUSED(capture); }
void plat_set_fullscreen(bool fs) { FIGHTO_UNUSED(fs); }
void plat_set_vsync(bool on) { FIGHTO_UNUSED(on); }
void plat_request_quit() { g_quit = true; }
bool plat_is_mobile() { return true; }
const char* plat_name() { return "Android"; }

// ---------------------------------------------------------------------------
// Input mapping (keyboards and gamepads attached to Android devices)
// ---------------------------------------------------------------------------
static int map_android_key(int kc) {
    if (kc >= 29 && kc <= 54) return KEY_A + (kc - 29);      // KEYCODE_A..Z
    if (kc >= 7 && kc <= 16) return KEY_0 + (kc - 7);        // KEYCODE_0..9
    switch (kc) {
    case 62: return KEY_SPACE;
    case 66: return KEY_ENTER;
    case 111: return KEY_ESCAPE;
    case 61: return KEY_TAB;
    case 67: return KEY_BACKSPACE;
    case 59: case 60: return KEY_SHIFT;
    case 113: case 114: return KEY_CTRL;
    case 57: case 58: return KEY_ALT;
    case 19: return KEY_UP;
    case 20: return KEY_DOWN;
    case 21: return KEY_LEFT;
    case 22: return KEY_RIGHT;
    default: break;
    }
    if (kc >= 131 && kc <= 142) return KEY_F1 + (kc - 131);  // KEYCODE_F1..F12
    return KEY_NONE;
}

static int map_android_pad(int kc) {
    switch (kc) {
    case 96: return PAD_A;       // BUTTON_A
    case 97: return PAD_B;       // BUTTON_B
    case 99: return PAD_X;       // BUTTON_X
    case 100: return PAD_Y;      // BUTTON_Y
    case 102: return PAD_LB;     // BUTTON_L1
    case 103: return PAD_RB;     // BUTTON_R1
    case 109: return PAD_BACK;   // BUTTON_SELECT
    case 108: return PAD_START;  // BUTTON_START
    case 106: return PAD_LSTICK; // BUTTON_THUMBL
    case 107: return PAD_RSTICK; // BUTTON_THUMBR
    default: return -1;
    }
}

static TouchPoint* find_touch(int id) {
    for (int i = 0; i < MAX_TOUCHES; i++)
        if (g_input.touches[i].active && g_input.touches[i].id == id) return &g_input.touches[i];
    return 0;
}

// ---------------------------------------------------------------------------
// C++ runtime support normally supplied by the NDK's libc++ / crt objects
// ---------------------------------------------------------------------------
extern "C" {
void* __dso_handle = &__dso_handle;
int __cxa_atexit(void (*fn)(void*), void* arg, void* dso) { FIGHTO_UNUSED(fn); FIGHTO_UNUSED(arg); FIGHTO_UNUSED(dso); return 0; }
// Function-local statics: the game only initialises them on the GL thread, but keep
// the guard protocol correct with an atomic flag anyway.
int __cxa_guard_acquire(long long* g) {
    while (__atomic_exchange_n((char*)g + 1, 1, __ATOMIC_ACQUIRE)) {}
    if (*(char*)g) { __atomic_store_n((char*)g + 1, 0, __ATOMIC_RELEASE); return 0; }
    return 1;
}
void __cxa_guard_release(long long* g) { *(char*)g = 1; __atomic_store_n((char*)g + 1, 0, __ATOMIC_RELEASE); }
void __cxa_guard_abort(long long* g) { __atomic_store_n((char*)g + 1, 0, __ATOMIC_RELEASE); }
void __cxa_pure_virtual() { abort(); }
}

// ---------------------------------------------------------------------------
// JNI entry points (class com.fighto.game.FightoLib)
// ---------------------------------------------------------------------------
static void load_system_libs() {
    void* andr = dlopen("libandroid.so", RTLD_NOW);
    if (andr) {
        p_AAssetManager_fromJava = (PFN_AAssetManager_fromJava)dlsym(andr, "AAssetManager_fromJava");
        p_AAssetManager_open = (PFN_AAssetManager_open)dlsym(andr, "AAssetManager_open");
        p_AAsset_getLength64 = (PFN_AAsset_getLength64)dlsym(andr, "AAsset_getLength64");
        p_AAsset_read = (PFN_AAsset_read)dlsym(andr, "AAsset_read");
        p_AAsset_close = (PFN_AAsset_close)dlsym(andr, "AAsset_close");
    }
    void* lg = dlopen("liblog.so", RTLD_NOW);
    if (lg) p_log_write = (PFN_log_write)dlsym(lg, "__android_log_write");
    g_glesLib = dlopen("libGLESv3.so", RTLD_NOW);
    if (!g_glesLib) g_glesLib = dlopen("libGLESv2.so", RTLD_NOW);
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    if (egl) p_eglGetProcAddress = (PFN_eglGetProcAddress)dlsym(egl, "eglGetProcAddress");
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_fighto_game_FightoLib_nativeInit(JNIEnv* env, jclass, jobject assetManager, jstring filesDir, jint w, jint h) {
    if (g_inited) { g_w = w; g_h = h; return JNI_TRUE; }
    load_system_libs();
    const char* fd = env->GetStringUTFChars(filesDir, 0);
    str_copy(g_filesDir, sizeof(g_filesDir), fd);
    env->ReleaseStringUTFChars(filesDir, fd);
    char logPath[640];
    user_path(logPath, sizeof(logPath), "fighto.log");
    g_logFd = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    g_assetsRef = env->NewGlobalRef(assetManager);
    if (p_AAssetManager_fromJava) g_assets = p_AAssetManager_fromJava(env, g_assetsRef);
    log_info("FIGHTO Android: files %s, assets %s, GLES %s", g_filesDir, g_assets ? "ok" : "MISSING", g_glesLib ? "ok" : "MISSING");
    if (!g_assets || !g_glesLib) return JNI_FALSE;
    g_w = w;
    g_h = h;
    memset(&g_input, 0, sizeof(g_input));
    g_input.touchMode = true;
    GameLaunchOptions opt;
    memset(&opt, 0, sizeof(opt));
    opt.width = w;
    opt.height = h;
    if (!game_init(&opt, w, h)) { log_error("game_init failed"); return JNI_FALSE; }
    g_inited = true;
    g_lastFrame = plat_time_seconds();
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL Java_com_fighto_game_FightoLib_nativeResize(JNIEnv*, jclass, jint w, jint h) {
    g_w = w;
    g_h = h;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_fighto_game_FightoLib_nativeFrame(JNIEnv*, jclass) {
    if (!g_inited) return JNI_TRUE;
    double now = plat_time_seconds();
    float dt = (float)(now - g_lastFrame);
    g_lastFrame = now;
    dt = clampf(dt, 0.0f, 0.1f);
    bool keep = game_frame(dt, &g_input, g_w, g_h) && !g_quit;
    // Clear per-frame edges; drop finished touches
    memset(g_input.pressed, 0, sizeof(g_input.pressed));
    memset(g_input.released, 0, sizeof(g_input.released));
    memset(g_input.padPressed, 0, sizeof(g_input.padPressed));
    g_input.lastKeyPressed = KEY_NONE;
    g_input.wheel = 0;
    g_input.mouseDX = g_input.mouseDY = 0;
    for (int i = 0; i < MAX_TOUCHES; i++) {
        TouchPoint& t = g_input.touches[i];
        t.began = false;
        if (t.ended) { t.active = false; t.ended = false; }
    }
    return keep ? JNI_TRUE : JNI_FALSE;
}

// action: 0 down, 1 move, 2 up, 3 cancel
extern "C" JNIEXPORT void JNICALL Java_com_fighto_game_FightoLib_nativeTouch(JNIEnv*, jclass, jint action, jint id, jfloat x, jfloat y) {
    g_input.touchMode = true;
    TouchPoint* t = find_touch(id);
    if (action == 0) {
        if (!t) {
            for (int i = 0; i < MAX_TOUCHES; i++)
                if (!g_input.touches[i].active) { t = &g_input.touches[i]; break; }
        }
        if (!t) return;
        memset(t, 0, sizeof(*t));
        t->id = id;
        t->active = true;
        t->began = true;
        t->x = t->startX = x;
        t->y = t->startY = y;
        g_input.mouseX = x;
        g_input.mouseY = y;
    } else if (t) {
        t->x = x;
        t->y = y;
        if (action >= 2) t->ended = true;
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_fighto_game_FightoLib_nativeKey(JNIEnv*, jclass, jint keyCode, jboolean down) {
    int pb = map_android_pad(keyCode);
    if (pb >= 0) {
        if (down && !g_input.padDown[pb]) g_input.padPressed[pb] = true;
        g_input.padDown[pb] = down;
        g_input.padConnected = true;
        return;
    }
    if (keyCode >= 19 && keyCode <= 22 && g_input.padConnected) {
        int d = keyCode == 19 ? PAD_DUP : keyCode == 20 ? PAD_DDOWN : keyCode == 21 ? PAD_DLEFT : PAD_DRIGHT;
        if (down && !g_input.padDown[d]) g_input.padPressed[d] = true;
        g_input.padDown[d] = down;
    }
    int k = keyCode == 4 /*BACK*/ ? KEY_ESCAPE : map_android_key(keyCode);
    if (k == KEY_NONE) return;
    if (down && !g_input.down[k]) { g_input.pressed[k] = true; g_input.lastKeyPressed = k; }
    if (!down && g_input.down[k]) g_input.released[k] = true;
    g_input.down[k] = down;
    if (k != KEY_ESCAPE) g_input.touchMode = false;
}

extern "C" JNIEXPORT void JNICALL Java_com_fighto_game_FightoLib_nativeAxes(JNIEnv*, jclass, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt) {
    g_input.padConnected = true;
    g_input.padLX = lx;
    g_input.padLY = -ly;  // Android: down is positive
    g_input.padRX = rx;
    g_input.padRY = -ry;
    g_input.padLT = lt;
    g_input.padRT = rt;
    if (fabsf(lx) > 0.3f || fabsf(ly) > 0.3f || fabsf(rx) > 0.3f || fabsf(ry) > 0.3f) g_input.touchMode = false;
}

extern "C" JNIEXPORT void JNICALL Java_com_fighto_game_FightoLib_nativePause(JNIEnv*, jclass) {
    if (g_inited) game_on_pause();
    // Release everything that might be held
    memset(g_input.down, 0, sizeof(g_input.down));
    memset(g_input.padDown, 0, sizeof(g_input.padDown));
    for (int i = 0; i < MAX_TOUCHES; i++) g_input.touches[i].active = false;
}

extern "C" JNIEXPORT void JNICALL Java_com_fighto_game_FightoLib_nativeResume(JNIEnv*, jclass) {
    g_lastFrame = plat_time_seconds();
    if (g_inited) game_on_resume();
}

// Called from the Java audio thread: renders interleaved 16-bit stereo PCM.
extern "C" JNIEXPORT void JNICALL Java_com_fighto_game_FightoLib_nativeAudio(JNIEnv* env, jclass, jshortArray out, jint frames) {
    static float mix[4096 * 2];
    static short pcm[4096 * 2];
    if (frames > 4096) frames = 4096;
    if (g_inited) game_audio_mix(mix, frames);
    else memset(mix, 0, sizeof(float) * (size_t)frames * 2);
    for (int i = 0; i < frames * 2; i++) {
        float v = clampf(mix[i], -1.0f, 1.0f);
        pcm[i] = (short)(v * 32767.0f);
    }
    env->SetShortArrayRegion(out, 0, frames * 2, pcm);
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_fighto_game_FightoLib_nativeWantsQuit(JNIEnv*, jclass) { return g_quit ? JNI_TRUE : JNI_FALSE; }
