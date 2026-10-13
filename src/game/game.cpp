// Temporary renderer validation harness (replaced by the full game loop).
#include "../platform/platform.h"
#include "../gfx/renderer.h"
#include "../ui/draw2d.h"
#include "world.h"

static float g_time;
static int g_frame;
static GameLaunchOptions g_opt;

bool game_init(const GameLaunchOptions* opt, int w, int h) {
    g_opt = *opt;
    if (!gl_load()) return false;
    renderer_init(GQ_HIGH);
    if (!ui_init()) return false;
    world_generate(1337);
    return true;
}

bool game_frame(float dt, InputState* in, int w, int h) {
    g_time += 1.0f / 30.0f;
    g_frame++;
    CameraState cam;
    float a = g_time * 0.1f;
    vec3 target = landmark_pos(LM_HOME);
    cam.pos = target + vec3(sinf(a) * 25.0f, 6.0f, cosf(a) * 25.0f - 10.0f);
    cam.target = target + vec3(0, 3, 0);
    if (g_frame > 3) { cam.pos = vec3(60, 40, 250); cam.target = vec3(0, 20, 0); }
    if (g_frame > 6) { vec3 p = landmark_pos(LM_DINER); cam.pos = p + vec3(-14, 3.0f, -16); cam.target = p + vec3(0, 3, 4); }
    if (g_frame > 9) { vec3 p = landmark_pos(LM_FIGHT_PIT); cam.pos = p + vec3(-6, 6.0f, 18); cam.target = p + vec3(0, 1, 22); }
    if (g_frame > 12) { vec3 p = landmark_pos(LM_AEGIS_BRIEFING); cam.pos = p + vec3(-4, 3.5f, -8); cam.target = p + vec3(10, 1.5f, 1); }
    cam.up = vec3(0, 1, 0);
    cam.fov = 60;
    cam.zNear = 0.15f;
    cam.zFar = 900;
    Atmosphere atm;
    memset(&atm, 0, sizeof(atm));
    atm.sunDir = normalize(vec3(-0.35f, 0.55f, 0.4f));
    atm.sunColor = vec3(0.16f, 0.2f, 0.32f);
    atm.skyTop = vec3(0.004f, 0.006f, 0.014f);
    atm.skyHorizon = vec3(0.05f, 0.045f, 0.05f);
    atm.ambientUp = vec3(0.025f, 0.03f, 0.045f);
    atm.ambientDown = vec3(0.02f, 0.016f, 0.014f);
    atm.fogColor = vec3(0.045f, 0.04f, 0.045f);
    atm.fogDensity = 0.006f;
    atm.fogHeightFalloff = 0.04f;
    atm.fogBase = 0;
    atm.fogMax = 0.95f;
    atm.rain = 0.8f;
    atm.windowsOn = 1;
    atm.emissiveScale = 1;
    atm.exposure = 1.6f;
    atm.cloudCover = 0.6f;
    atm.wind = 0.3f;
    renderer_begin_frame(w, h, cam, atm, g_time, 1.0f / 30.0f);
    PostState post;
    memset(&post, 0, sizeof(post));
    renderer_end_frame(post);
    ui_begin(w, h);
    ui_text_shadow(FONT_TITLE, w * 0.5f, 40, 90, "FIGHTO", rgba(0.85f, 0.05f, 0.05f), ALIGN_CENTER);
    char buf[128];
    fmt_str(buf, sizeof(buf), "draws %d  tris %d", renderer_draw_calls(), renderer_triangles());
    ui_text(FONT_UI, 20, h - 40, 24, buf, rgba(1, 1, 1));
    ui_end();
    if (g_opt.autotest && (g_frame % 3) == 0) {
        char name[64];
        fmt_str(name, sizeof(name), "shot_%02d.png", g_frame / 3);
        renderer_screenshot(name);
    }
    return !(g_opt.autotest && g_frame >= 15);
}
void game_shutdown() {}
void game_on_pause() {}
void game_on_resume() {}
void game_audio_mix(float* out, int frames) { memset(out, 0, sizeof(float) * frames * 2); }
