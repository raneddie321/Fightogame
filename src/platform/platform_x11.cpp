// FIGHTO - Linux backend: X11 window, GLX OpenGL 3.3 core context, ALSA audio.
// Used for development and automated testing (Xvfb + Mesa).
#include "platform.h"
#include "platform_desktop.h"
#include "../core/vmath.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glx.h>

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
typedef void (*glXSwapIntervalEXTProc)(Display*, GLXDrawable, int);

static Display* g_dpy;
static Window g_win;
static GLXContext g_ctx;
static Atom g_wmDelete;
static bool g_quit;
static bool g_captured;
static bool g_fullscreen;
static int g_w = 1280, g_h = 720;
static Cursor g_blankCursor;
static volatile bool g_audioRun;
static pthread_t g_audioThread;

double plat_time_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void* plat_gl_proc(const char* name) { return (void*)glXGetProcAddressARB((const GLubyte*)name); }

void plat_capture_mouse(bool capture) {
    if (!g_dpy || capture == g_captured) return;
    g_captured = capture;
    if (capture) {
        XDefineCursor(g_dpy, g_win, g_blankCursor);
        XGrabPointer(g_dpy, g_win, True, PointerMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, g_win, None, CurrentTime);
        XWarpPointer(g_dpy, None, g_win, 0, 0, 0, 0, g_w / 2, g_h / 2);
    } else {
        XUndefineCursor(g_dpy, g_win);
        XUngrabPointer(g_dpy, CurrentTime);
    }
}

void plat_set_fullscreen(bool fs) {
    if (!g_dpy || fs == g_fullscreen) return;
    g_fullscreen = fs;
    Atom wmState = XInternAtom(g_dpy, "_NET_WM_STATE", False);
    Atom fsAtom = XInternAtom(g_dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XEvent e;
    memset(&e, 0, sizeof(e));
    e.type = ClientMessage;
    e.xclient.window = g_win;
    e.xclient.message_type = wmState;
    e.xclient.format = 32;
    e.xclient.data.l[0] = fs ? 1 : 0;
    e.xclient.data.l[1] = (long)fsAtom;
    XSendEvent(g_dpy, DefaultRootWindow(g_dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
}

void plat_set_vsync(bool on) {
    glXSwapIntervalEXTProc swap = (glXSwapIntervalEXTProc)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
    if (swap && g_dpy) swap(g_dpy, g_win, on ? 1 : 0);
}

void plat_request_quit() { g_quit = true; }
const char* plat_name() { return "Linux"; }

static int map_keysym(KeySym ks) {
    if (ks >= XK_a && ks <= XK_z) return 'A' + (int)(ks - XK_a);
    if (ks >= XK_A && ks <= XK_Z) return 'A' + (int)(ks - XK_A);
    if (ks >= XK_0 && ks <= XK_9) return '0' + (int)(ks - XK_0);
    switch (ks) {
    case XK_space: return KEY_SPACE;
    case XK_Escape: return KEY_ESCAPE;
    case XK_Return: case XK_KP_Enter: return KEY_ENTER;
    case XK_Tab: return KEY_TAB;
    case XK_BackSpace: return KEY_BACKSPACE;
    case XK_Shift_L: case XK_Shift_R: return KEY_SHIFT;
    case XK_Control_L: case XK_Control_R: return KEY_CTRL;
    case XK_Alt_L: case XK_Alt_R: return KEY_ALT;
    case XK_Up: return KEY_UP;
    case XK_Down: return KEY_DOWN;
    case XK_Left: return KEY_LEFT;
    case XK_Right: return KEY_RIGHT;
    case XK_F1: return KEY_F1; case XK_F2: return KEY_F2; case XK_F3: return KEY_F3; case XK_F4: return KEY_F4;
    case XK_F5: return KEY_F5; case XK_F6: return KEY_F6; case XK_F7: return KEY_F7; case XK_F8: return KEY_F8;
    case XK_F9: return KEY_F9; case XK_F10: return KEY_F10; case XK_F11: return KEY_F11; case XK_F12: return KEY_F12;
    default: return KEY_NONE;
    }
}

// ---------------------------------------------------------------------------
// ALSA audio (loaded dynamically so the game still runs without libasound)
// ---------------------------------------------------------------------------
typedef int (*snd_pcm_open_t)(void**, const char*, int, int);
typedef int (*snd_pcm_set_params_t)(void*, int, int, unsigned, unsigned, int, unsigned);
typedef long (*snd_pcm_writei_t)(void*, const void*, unsigned long);
typedef int (*snd_pcm_recover_t)(void*, int, int);
typedef int (*snd_pcm_close_t)(void*);

static void* g_pcm;
static snd_pcm_writei_t p_writei;
static snd_pcm_recover_t p_recover;
static snd_pcm_close_t p_close;

static void* audio_thread_main(void*) {
    const int frames = 512;
    float fbuf[frames * 2];
    i16 sbuf[frames * 2];
    while (g_audioRun) {
        game_audio_mix(fbuf, frames);
        for (int i = 0; i < frames * 2; i++) {
            float v = clampf(fbuf[i], -1.0f, 1.0f);
            sbuf[i] = (i16)(v * 32767.0f);
        }
        long r = p_writei(g_pcm, sbuf, frames);
        if (r < 0) p_recover(g_pcm, (int)r, 1);
    }
    return 0;
}

static void audio_start() {
    if (getenv("FIGHTO_NO_AUDIO")) return;
    void* lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!lib) { log_info("ALSA not available; audio disabled"); return; }
    snd_pcm_open_t p_open = (snd_pcm_open_t)dlsym(lib, "snd_pcm_open");
    snd_pcm_set_params_t p_set = (snd_pcm_set_params_t)dlsym(lib, "snd_pcm_set_params");
    p_writei = (snd_pcm_writei_t)dlsym(lib, "snd_pcm_writei");
    p_recover = (snd_pcm_recover_t)dlsym(lib, "snd_pcm_recover");
    p_close = (snd_pcm_close_t)dlsym(lib, "snd_pcm_close");
    if (!p_open || !p_set || !p_writei || !p_recover || !p_close) return;
    if (p_open(&g_pcm, "default", 0 /*PLAYBACK*/, 0) < 0) { log_info("No ALSA output device; audio disabled"); g_pcm = 0; return; }
    // SND_PCM_FORMAT_S16_LE = 2, SND_PCM_ACCESS_RW_INTERLEAVED = 3
    if (p_set(g_pcm, 2, 3, 2, AUDIO_SAMPLE_RATE, 1, 60000) < 0) { p_close(g_pcm); g_pcm = 0; log_info("ALSA configuration failed; audio disabled"); return; }
    g_audioRun = true;
    pthread_create(&g_audioThread, 0, audio_thread_main, 0);
    log_info("Audio: ALSA 44100 Hz stereo");
}

static void audio_stop() {
    if (!g_audioRun) return;
    g_audioRun = false;
    pthread_join(g_audioThread, 0);
    p_close(g_pcm);
}

int main(int argc, char** argv) {
    GameLaunchOptions opt;
    bool windowed = false;
    desktop_parse_args(argc, argv, &opt, &windowed);
    char exe[512] = {0};
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) exe[n] = 0;
    desktop_init_paths(exe);
    g_w = opt.width;
    g_h = opt.height;

    g_dpy = XOpenDisplay(0);
    if (!g_dpy) { log_error("Cannot open X display"); return 1; }
    int fbAttribs[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT,
                       GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24,
                       GLX_STENCIL_SIZE, 8, GLX_DOUBLEBUFFER, True, None};
    int fbCount = 0;
    GLXFBConfig* fbs = glXChooseFBConfig(g_dpy, DefaultScreen(g_dpy), fbAttribs, &fbCount);
    if (!fbs || fbCount == 0) { log_error("No suitable GLX framebuffer config"); return 1; }
    GLXFBConfig fb = fbs[0];
    XFree(fbs);
    XVisualInfo* vi = glXGetVisualFromFBConfig(g_dpy, fb);
    Window root = RootWindow(g_dpy, vi->screen);
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    swa.colormap = XCreateColormap(g_dpy, root, vi->visual, AllocNone);
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    g_win = XCreateWindow(g_dpy, root, 0, 0, (unsigned)g_w, (unsigned)g_h, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
    XStoreName(g_dpy, g_win, "FIGHTO");
    g_wmDelete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, g_win, &g_wmDelete, 1);
    XMapWindow(g_dpy, g_win);
    XFree(vi);

    glXCreateContextAttribsARBProc createCtx = (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    // --gles: request an OpenGL ES 3.0 context instead, to exercise the exact shader and
    // renderer paths used on Android (GLX_EXT_create_context_es2_profile)
    bool gles = false;
    for (int i = 1; i < argc; i++) if (str_eq(argv[i], "--gles")) gles = true;
    int ctxAttribs[] = {0x2091 /*MAJOR*/, 3, 0x2092 /*MINOR*/, gles ? 0 : 3, 0x9126 /*PROFILE_MASK*/, gles ? 0x4 /*ES2_PROFILE*/ : 0x1 /*CORE*/, None};
    if (createCtx) g_ctx = createCtx(g_dpy, fb, 0, True, ctxAttribs);
    if (!g_ctx) { log_error(gles ? "Failed to create an OpenGL ES 3.0 context" : "Failed to create an OpenGL 3.3 core context"); return 1; }
    glXMakeCurrent(g_dpy, g_win, g_ctx);

    // Invisible cursor for mouse look
    char data[8] = {0};
    Pixmap bm = XCreateBitmapFromData(g_dpy, g_win, data, 8, 8);
    XColor black;
    memset(&black, 0, sizeof(black));
    g_blankCursor = XCreatePixmapCursor(g_dpy, bm, bm, &black, &black, 0, 0);
    XFreePixmap(g_dpy, bm);

    if (!windowed && !opt.autotest) plat_set_fullscreen(false);
    plat_set_vsync(!opt.autotest);

    if (!game_init(&opt, g_w, g_h)) { log_error("game_init failed"); return 1; }
    if (!opt.autotest) audio_start();

    static InputState input;
    memset(&input, 0, sizeof(input));
    // --touch: the left mouse button acts as a finger, for testing the mobile touch controls
    bool touchEmu = false;
    for (int i = 1; i < argc; i++) if (str_eq(argv[i], "--touch")) touchEmu = true;
    double last = plat_time_seconds();
    bool running = true;
    while (running && !g_quit) {
        memset(input.pressed, 0, sizeof(input.pressed));
        memset(input.released, 0, sizeof(input.released));
        input.mouseDX = input.mouseDY = 0;
        input.wheel = 0;
        input.lastKeyPressed = KEY_NONE;
        while (XPending(g_dpy)) {
            XEvent e;
            XNextEvent(g_dpy, &e);
            switch (e.type) {
            case KeyPress: case KeyRelease: {
                // Filter auto-repeat release/press pairs
                if (e.type == KeyRelease && XEventsQueued(g_dpy, QueuedAfterReading)) {
                    XEvent nx;
                    XPeekEvent(g_dpy, &nx);
                    if (nx.type == KeyPress && nx.xkey.time == e.xkey.time && nx.xkey.keycode == e.xkey.keycode) {
                        XNextEvent(g_dpy, &nx);
                        break;
                    }
                }
                int k = map_keysym(XLookupKeysym(&e.xkey, 0));
                if (k != KEY_NONE) {
                    bool d = e.type == KeyPress;
                    if (d && !input.down[k]) { input.pressed[k] = true; input.lastKeyPressed = k; }
                    if (!d && input.down[k]) input.released[k] = true;
                    input.down[k] = d;
                }
            } break;
            case ButtonPress: case ButtonRelease: {
                bool d = e.type == ButtonPress;
                int b = (int)e.xbutton.button;
                if (b == 4 && d) { input.wheel++; break; }
                if (b == 5 && d) { input.wheel--; break; }
                int k = b == 1 ? KEY_MOUSE_LEFT : b == 3 ? KEY_MOUSE_RIGHT : b == 2 ? KEY_MOUSE_MIDDLE : b == 8 ? KEY_MOUSE_X1 : b == 9 ? KEY_MOUSE_X2 : KEY_NONE;
                if (k != KEY_NONE) {
                    if (d && !input.down[k]) { input.pressed[k] = true; input.lastKeyPressed = k; }
                    if (!d && input.down[k]) input.released[k] = true;
                    input.down[k] = d;
                }
            } break;
            case MotionNotify: {
                float mx = (float)e.xmotion.x, my = (float)e.xmotion.y;
                if (g_captured) {
                    float dx = mx - g_w / 2, dy = my - g_h / 2;
                    if (dx != 0 || dy != 0) { input.mouseDX += dx; input.mouseDY += dy; }
                } else {
                    input.mouseDX += mx - input.mouseX;
                    input.mouseDY += my - input.mouseY;
                }
                input.mouseX = mx;
                input.mouseY = my;
            } break;
            case ConfigureNotify:
                g_w = e.xconfigure.width;
                g_h = e.xconfigure.height;
                break;
            case FocusOut:
                memset(input.down, 0, sizeof(input.down));
                break;
            case ClientMessage:
                if ((Atom)e.xclient.data.l[0] == g_wmDelete) running = false;
                break;
            }
        }
        if (g_captured) {
            XWarpPointer(g_dpy, None, g_win, 0, 0, 0, 0, g_w / 2, g_h / 2);
            input.mouseX = (float)(g_w / 2);
            input.mouseY = (float)(g_h / 2);
        }
        input.mouseCaptured = g_captured;
        if (touchEmu) {
            TouchPoint& t = input.touches[0];
            if (t.ended) { t.active = false; t.ended = false; }
            t.began = false;
            if (input.pressed[KEY_MOUSE_LEFT]) {
                memset(&t, 0, sizeof(t));
                t.active = t.began = true;
                t.startX = input.mouseX;
                t.startY = input.mouseY;
            }
            if (t.active) { t.x = input.mouseX; t.y = input.mouseY; }
            if (input.released[KEY_MOUSE_LEFT] && t.active) t.ended = true;
            input.touchMode = true;
        }
        double now = plat_time_seconds();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (!game_frame(dt, &input, g_w, g_h)) running = false;
        glXSwapBuffers(g_dpy, g_win);
    }
    audio_stop();
    game_shutdown();
    plat_capture_mouse(false);
    glXMakeCurrent(g_dpy, None, 0);
    glXDestroyContext(g_dpy, g_ctx);
    XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
    return 0;
}
