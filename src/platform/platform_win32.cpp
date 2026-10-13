// FIGHTO - Windows backend: Win32 window, WGL OpenGL 3.3 core context,
// raw mouse input, XInput gamepads and waveOut audio. No external libraries.
#include "platform.h"
#include "platform_desktop.h"
#include "../core/vmath.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

typedef HGLRC(WINAPI* wglCreateContextAttribsARBProc)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* wglSwapIntervalEXTProc)(int);

static HWND g_hwnd;
static HDC g_hdc;
static HGLRC g_glrc;
static HMODULE g_opengl32;
static bool g_quit;
static bool g_captured;
static bool g_fullscreen;
static bool g_active = true;
static int g_w = 1280, g_h = 720;
static WINDOWPLACEMENT g_prevPlacement = {sizeof(WINDOWPLACEMENT)};
static InputState g_input;
static LARGE_INTEGER g_freq;

double plat_time_seconds() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)g_freq.QuadPart;
}

void* plat_gl_proc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == 0 || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1) {
        p = (void*)GetProcAddress(g_opengl32, name);
    }
    return p;
}

static void clip_cursor_to_window() {
    RECT r;
    GetClientRect(g_hwnd, &r);
    POINT tl = {r.left, r.top}, br = {r.right, r.bottom};
    ClientToScreen(g_hwnd, &tl);
    ClientToScreen(g_hwnd, &br);
    RECT clip = {tl.x, tl.y, br.x, br.y};
    ClipCursor(&clip);
}

void plat_capture_mouse(bool capture) {
    if (capture == g_captured) return;
    g_captured = capture;
    if (capture) {
        while (ShowCursor(FALSE) >= 0) {}
        clip_cursor_to_window();
    } else {
        while (ShowCursor(TRUE) < 0) {}
        ClipCursor(0);
    }
}

void plat_set_fullscreen(bool fs) {
    if (!g_hwnd || fs == g_fullscreen) return;
    g_fullscreen = fs;
    DWORD style = (DWORD)GetWindowLongA(g_hwnd, GWL_STYLE);
    if (fs) {
        MONITORINFO mi = {sizeof(mi)};
        if (GetWindowPlacement(g_hwnd, &g_prevPlacement) && GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongA(g_hwnd, GWL_STYLE, (LONG)(style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, (LONG)((style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(g_hwnd, &g_prevPlacement);
        SetWindowPos(g_hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    if (g_captured) clip_cursor_to_window();
}

void plat_set_vsync(bool on) {
    wglSwapIntervalEXTProc swap = (wglSwapIntervalEXTProc)(void*)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap) swap(on ? 1 : 0);
}

void plat_request_quit() { g_quit = true; }
const char* plat_name() { return "Windows"; }

static int map_vk(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return (int)vk;
    if (vk >= '0' && vk <= '9') return (int)vk;
    switch (vk) {
    case VK_SPACE: return KEY_SPACE;
    case VK_ESCAPE: return KEY_ESCAPE;
    case VK_RETURN: return KEY_ENTER;
    case VK_TAB: return KEY_TAB;
    case VK_BACK: return KEY_BACKSPACE;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return KEY_SHIFT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return KEY_CTRL;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return KEY_ALT;
    case VK_UP: return KEY_UP;
    case VK_DOWN: return KEY_DOWN;
    case VK_LEFT: return KEY_LEFT;
    case VK_RIGHT: return KEY_RIGHT;
    default:
        if (vk >= VK_F1 && vk <= VK_F12) return KEY_F1 + (int)(vk - VK_F1);
        return KEY_NONE;
    }
}

static void set_key(int k, bool d) {
    if (k == KEY_NONE) return;
    if (d && !g_input.down[k]) { g_input.pressed[k] = true; g_input.lastKeyPressed = k; }
    if (!d && g_input.down[k]) g_input.released[k] = true;
    g_input.down[k] = d;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: g_quit = true; return 0;
    case WM_SIZE:
        g_w = LOWORD(lp);
        g_h = HIWORD(lp);
        if (g_captured) clip_cursor_to_window();
        return 0;
    case WM_ACTIVATEAPP:
        g_active = wp != 0;
        if (!g_active) {
            memset(g_input.down, 0, sizeof(g_input.down));
            if (g_captured) { ClipCursor(0); }
        } else if (g_captured) {
            clip_cursor_to_window();
        }
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (!(lp & (1 << 30))) set_key(map_vk(wp), true);
        if (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) g_quit = true;
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        set_key(map_vk(wp), false);
        return 0;
    case WM_SYSCHAR: return 0; // suppress the alt beep
    case WM_LBUTTONDOWN: set_key(KEY_MOUSE_LEFT, true); SetCapture(hwnd); return 0;
    case WM_LBUTTONUP: set_key(KEY_MOUSE_LEFT, false); ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: set_key(KEY_MOUSE_RIGHT, true); return 0;
    case WM_RBUTTONUP: set_key(KEY_MOUSE_RIGHT, false); return 0;
    case WM_MBUTTONDOWN: set_key(KEY_MOUSE_MIDDLE, true); return 0;
    case WM_MBUTTONUP: set_key(KEY_MOUSE_MIDDLE, false); return 0;
    case WM_XBUTTONDOWN: set_key(HIWORD(wp) == 1 ? KEY_MOUSE_X1 : KEY_MOUSE_X2, true); return TRUE;
    case WM_XBUTTONUP: set_key(HIWORD(wp) == 1 ? KEY_MOUSE_X1 : KEY_MOUSE_X2, false); return TRUE;
    case WM_MOUSEWHEEL: g_input.wheel += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1; return 0;
    case WM_MOUSEMOVE: {
        float mx = (float)(short)LOWORD(lp), my = (float)(short)HIWORD(lp);
        g_input.mouseX = mx;
        g_input.mouseY = my;
    } return 0;
    case WM_INPUT: {
        RAWINPUT raw;
        UINT size = sizeof(raw);
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
            if (raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                g_input.mouseDX += (float)raw.data.mouse.lLastX;
                g_input.mouseDY += (float)raw.data.mouse.lLastY;
            }
        }
    } break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// XInput (loaded dynamically)
// ---------------------------------------------------------------------------
struct XGamepad { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
struct XState { DWORD dwPacketNumber; XGamepad Gamepad; };
typedef DWORD(WINAPI* XInputGetStateProc)(DWORD, XState*);
static XInputGetStateProc p_XInputGetState;

static float stick(SHORT v) {
    float f = v / 32767.0f;
    if (f < -1) f = -1;
    if (fabsf(f) < 0.24f) return 0;
    return (f - signf(f) * 0.24f) / 0.76f;
}

static void poll_gamepad() {
    bool prev[PAD_COUNT];
    memcpy(prev, g_input.padDown, sizeof(prev));
    memset(g_input.padDown, 0, sizeof(g_input.padDown));
    g_input.padConnected = false;
    if (!p_XInputGetState) return;
    XState st;
    memset(&st, 0, sizeof(st));
    if (p_XInputGetState(0, &st) != 0) return;
    g_input.padConnected = true;
    const XGamepad& g = st.Gamepad;
    g_input.padLX = stick(g.sThumbLX);
    g_input.padLY = stick(g.sThumbLY);
    g_input.padRX = stick(g.sThumbRX);
    g_input.padRY = stick(g.sThumbRY);
    g_input.padLT = g.bLeftTrigger / 255.0f;
    g_input.padRT = g.bRightTrigger / 255.0f;
    const WORD masks[PAD_COUNT] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080, 0x0001, 0x0002, 0x0004, 0x0008};
    for (int i = 0; i < PAD_COUNT; i++) {
        g_input.padDown[i] = (g.wButtons & masks[i]) != 0;
        g_input.padPressed[i] = g_input.padDown[i] && !prev[i];
    }
}

// ---------------------------------------------------------------------------
// waveOut audio
// ---------------------------------------------------------------------------
#define AUDIO_BUFFERS 4
#define AUDIO_FRAMES 1024
static HWAVEOUT g_waveOut;
static WAVEHDR g_hdrs[AUDIO_BUFFERS];
static i16 g_audioData[AUDIO_BUFFERS][AUDIO_FRAMES * 2];
static HANDLE g_audioEvent;
static HANDLE g_audioThread;
static volatile bool g_audioRun;

static void fill_buffer(int i) {
    static float fbuf[AUDIO_FRAMES * 2];
    game_audio_mix(fbuf, AUDIO_FRAMES);
    for (int s = 0; s < AUDIO_FRAMES * 2; s++) {
        float v = clampf(fbuf[s], -1.0f, 1.0f);
        g_audioData[i][s] = (i16)(v * 32767.0f);
    }
    g_hdrs[i].dwFlags &= ~WHDR_DONE;
    waveOutWrite(g_waveOut, &g_hdrs[i], sizeof(WAVEHDR));
}

static DWORD WINAPI audio_thread_main(LPVOID) {
    for (int i = 0; i < AUDIO_BUFFERS; i++) fill_buffer(i);
    while (g_audioRun) {
        WaitForSingleObject(g_audioEvent, 50);
        for (int i = 0; i < AUDIO_BUFFERS; i++) {
            if (g_hdrs[i].dwFlags & WHDR_DONE) fill_buffer(i);
        }
    }
    return 0;
}

static void audio_start() {
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = AUDIO_SAMPLE_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = AUDIO_SAMPLE_RATE * 4;
    g_audioEvent = CreateEventA(0, FALSE, FALSE, 0);
    if (waveOutOpen(&g_waveOut, WAVE_MAPPER, &fmt, (DWORD_PTR)g_audioEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        log_info("No audio output device; audio disabled");
        g_waveOut = 0;
        return;
    }
    for (int i = 0; i < AUDIO_BUFFERS; i++) {
        memset(&g_hdrs[i], 0, sizeof(WAVEHDR));
        g_hdrs[i].lpData = (LPSTR)g_audioData[i];
        g_hdrs[i].dwBufferLength = AUDIO_FRAMES * 4;
        waveOutPrepareHeader(g_waveOut, &g_hdrs[i], sizeof(WAVEHDR));
    }
    g_audioRun = true;
    g_audioThread = CreateThread(0, 0, audio_thread_main, 0, 0, 0);
    log_info("Audio: waveOut 44100 Hz stereo");
}

static void audio_stop() {
    if (!g_waveOut) return;
    g_audioRun = false;
    WaitForSingleObject(g_audioThread, 2000);
    waveOutReset(g_waveOut);
    for (int i = 0; i < AUDIO_BUFFERS; i++) waveOutUnprepareHeader(g_waveOut, &g_hdrs[i], sizeof(WAVEHDR));
    waveOutClose(g_waveOut);
}

static bool create_gl_context() {
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_hdc, &pfd);
    if (!pf || !SetPixelFormat(g_hdc, pf, &pfd)) { log_error("SetPixelFormat failed"); return false; }
    HGLRC legacy = wglCreateContext(g_hdc);
    if (!legacy) { log_error("wglCreateContext failed"); return false; }
    wglMakeCurrent(g_hdc, legacy);
    wglCreateContextAttribsARBProc createAttribs = (wglCreateContextAttribsARBProc)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!createAttribs) { log_error("wglCreateContextAttribsARB unavailable (OpenGL 3.3 required)"); return false; }
    int attribs[] = {0x2091, 3, 0x2092, 3, 0x9126, 0x1, 0};
    g_glrc = createAttribs(g_hdc, 0, attribs);
    wglMakeCurrent(0, 0);
    wglDeleteContext(legacy);
    if (!g_glrc) { log_error("Could not create an OpenGL 3.3 core context. Please update your graphics driver."); return false; }
    wglMakeCurrent(g_hdc, g_glrc);
    return true;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    QueryPerformanceFrequency(&g_freq);
    SetProcessDPIAware();
    timeBeginPeriod(1);
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(0, exe, MAX_PATH);
    GameLaunchOptions opt;
    bool windowed = false;
    desktop_parse_args(__argc, __argv, &opt, &windowed);
    desktop_init_paths(exe);
    g_opengl32 = LoadLibraryA("opengl32.dll");
    HMODULE xi = LoadLibraryA("xinput1_4.dll");
    if (!xi) xi = LoadLibraryA("xinput9_1_0.dll");
    if (xi) p_XInputGetState = (XInputGetStateProc)(void*)GetProcAddress(xi, "XInputGetState");

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    wc.lpszClassName = "FIGHTOWindow";
    RegisterClassA(&wc);
    g_w = opt.width;
    g_h = opt.height;
    RECT r = {0, 0, g_w, g_h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "FIGHTO", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    if (!g_hwnd) { MessageBoxA(0, "Could not create the game window.", "FIGHTO", MB_ICONERROR); return 1; }
    g_hdc = GetDC(g_hwnd);
    if (!create_gl_context()) {
        MessageBoxA(g_hwnd, "FIGHTO requires OpenGL 3.3. Please update your graphics driver.", "FIGHTO", MB_ICONERROR);
        return 1;
    }
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = 0;
    rid.hwndTarget = g_hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    plat_set_vsync(!opt.autotest);
    if (!game_init(&opt, g_w, g_h)) {
        MessageBoxA(g_hwnd, "FIGHTO failed to initialise. See fighto.log in %APPDATA%\\FIGHTO.", "FIGHTO", MB_ICONERROR);
        return 1;
    }
    if (!opt.autotest) audio_start();

    double last = plat_time_seconds();
    while (!g_quit) {
        memset(g_input.pressed, 0, sizeof(g_input.pressed));
        memset(g_input.released, 0, sizeof(g_input.released));
        g_input.mouseDX = g_input.mouseDY = 0;
        g_input.wheel = 0;
        g_input.lastKeyPressed = KEY_NONE;
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_quit = true;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        poll_gamepad();
        if (g_captured && g_active) {
            POINT c = {g_w / 2, g_h / 2};
            ClientToScreen(g_hwnd, &c);
            SetCursorPos(c.x, c.y);
        }
        g_input.mouseCaptured = g_captured;
        double now = plat_time_seconds();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (!game_frame(dt, &g_input, g_w, g_h)) g_quit = true;
        SwapBuffers(g_hdc);
        if (!g_active && !opt.autotest) Sleep(10);
    }
    audio_stop();
    game_shutdown();
    plat_capture_mouse(false);
    wglMakeCurrent(0, 0);
    wglDeleteContext(g_glrc);
    ReleaseDC(g_hwnd, g_hdc);
    DestroyWindow(g_hwnd);
    timeEndPeriod(1);
    return 0;
}
