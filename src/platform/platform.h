// FIGHTO - platform abstraction. Each backend (Win32, X11, Android) implements
// these functions and drives the game through the game_* entry points.
#pragma once
#include "../core/base.h"

enum Key {
    KEY_NONE = 0,
    KEY_A = 'A', KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_0 = '0', KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_SPACE = 128, KEY_ESCAPE, KEY_ENTER, KEY_TAB, KEY_BACKSPACE, KEY_SHIFT, KEY_CTRL, KEY_ALT,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_MOUSE_LEFT, KEY_MOUSE_RIGHT, KEY_MOUSE_MIDDLE, KEY_MOUSE_X1, KEY_MOUSE_X2,
    KEY_COUNT
};
const char* key_name(int key);

enum PadButton {
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_BACK, PAD_START, PAD_LSTICK, PAD_RSTICK,
    PAD_DUP, PAD_DDOWN, PAD_DLEFT, PAD_DRIGHT, PAD_COUNT
};

struct TouchPoint {
    int id;
    float x, y;        // pixels
    float startX, startY;
    bool active;
    bool began;        // started this frame
    bool ended;        // ended this frame
};

#define MAX_TOUCHES 10

struct InputState {
    bool down[KEY_COUNT];
    bool pressed[KEY_COUNT];
    bool released[KEY_COUNT];
    float mouseX, mouseY;     // pixels, top-left origin
    float mouseDX, mouseDY;   // raw motion this frame
    int wheel;
    bool mouseCaptured;
    // Gamepad
    bool padConnected;
    float padLX, padLY, padRX, padRY, padLT, padRT;
    bool padDown[PAD_COUNT];
    bool padPressed[PAD_COUNT];
    // Touch
    TouchPoint touches[MAX_TOUCHES];
    bool touchMode;           // true on touch devices
    int lastKeyPressed;       // for key rebinding (KEY_NONE if none)
};

// --- services implemented by each platform backend ---
void plat_log(const char* msg, bool error);
double plat_time_seconds();
void* plat_gl_proc(const char* name);
// Read-only game data (assets/ folder or APK assets)
bool plat_read_asset(const char* path, Array<u8>* out);
// Writable user data (saves, settings). Writes are atomic (temp file + rename).
bool plat_write_user_file(const char* name, const void* data, size_t size);
bool plat_read_user_file(const char* name, Array<u8>* out);
bool plat_user_file_exists(const char* name);
bool plat_copy_user_file(const char* from, const char* to);
void plat_capture_mouse(bool capture);
void plat_set_fullscreen(bool fs);
void plat_set_vsync(bool on);
void plat_request_quit();
bool plat_is_mobile();
const char* plat_name();

// --- entry points implemented by the game ---
struct GameLaunchOptions {
    bool autotest;           // run the automated gameplay test and exit
    bool unitScreenshot;     // write screenshots during autotest
    int width, height;
    char autotestOut[256];   // directory for autotest results
};
bool game_init(const GameLaunchOptions* opt, int width, int height);
// Called once per frame. Returns false when the game wants to exit.
bool game_frame(float dt, InputState* input, int width, int height);
void game_shutdown();
void game_on_pause();   // app sent to background (Android) / window minimised
void game_on_resume();
// Audio thread callback: fill interleaved stereo float samples at 44100 Hz.
void game_audio_mix(float* out, int frames);

#define AUDIO_SAMPLE_RATE 44100
