// FIGHTO - input mapping (keyboard/mouse, gamepad, touch) and settings persistence.
#include "game.h"
#include "../audio/audio.h"
#include "../ui/draw2d.h"

static const char* g_actionNames[ACT_COUNT] = {
    "Move Forward", "Move Back", "Move Left", "Move Right", "Sprint", "Jump / Handbrake", "Crouch / Stealth", "Interact / Enter Vehicle",
    "Attack / Fire", "Aim / Block", "Reload", "Lock-on Target", "Dodge", "Kick / Heavy", "Journal & Map", "Pause",
    "Quick Save", "Next Weapon", "Previous Weapon", "Horn", "Walk", "Map", "Dialogue Choice 1", "Dialogue Choice 2",
};
const char* action_name(int a) { return (a >= 0 && a < ACT_COUNT) ? g_actionNames[a] : "?"; }

void settings_defaults(Settings* s) {
    memset(s, 0, sizeof(*s));
    s->quality = plat_is_mobile() ? GQ_LOW : GQ_HIGH;
    s->renderScale = plat_is_mobile() ? 0.75f : 1.0f;
    s->fullscreen = false;
    s->vsync = true;
    s->brightness = 1.0f;
    s->fov = 62.0f;
    s->mouseSens = 1.0f;
    s->invertY = false;
    s->masterVol = 0.9f;
    s->musicVol = 0.6f;
    s->sfxVol = 0.9f;
    s->voiceVol = 0.8f;
    s->gore = 2;   // Extreme by default
    s->subtitles = true;
    s->difficulty = 1;
    s->filmGrain = true;
    s->touchScale = 1.0f;
    int def[ACT_COUNT][2] = {
        {KEY_W, KEY_UP}, {KEY_S, KEY_DOWN}, {KEY_A, KEY_LEFT}, {KEY_D, KEY_RIGHT}, {KEY_SHIFT, 0}, {KEY_SPACE, 0},
        {KEY_C, 0}, {KEY_E, 0}, {KEY_MOUSE_LEFT, 0}, {KEY_MOUSE_RIGHT, 0}, {KEY_R, 0}, {KEY_Q, KEY_MOUSE_MIDDLE},
        {KEY_CTRL, 0}, {KEY_F, 0}, {KEY_TAB, KEY_J}, {KEY_ESCAPE, KEY_P}, {KEY_F5, 0}, {KEY_X, KEY_MOUSE_X2},
        {KEY_Z, KEY_MOUSE_X1}, {KEY_H, 0}, {KEY_ALT, 0}, {KEY_M, 0}, {KEY_1, 0}, {KEY_2, 0},
    };
    memcpy(s->keys, def, sizeof(def));
}

bool settings_save(const Settings& s) {
    char buf[4096];
    int n = 0;
    n += fmt_str(buf + n, sizeof(buf) - n, "# FIGHTO settings\nquality=%d\nrenderScale=%.2f\nfullscreen=%d\nvsync=%d\nbrightness=%.2f\nfov=%.1f\n",
                 s.quality, s.renderScale, s.fullscreen ? 1 : 0, s.vsync ? 1 : 0, s.brightness, s.fov);
    n += fmt_str(buf + n, sizeof(buf) - n, "mouseSens=%.2f\ninvertY=%d\nmasterVol=%.2f\nmusicVol=%.2f\nsfxVol=%.2f\nvoiceVol=%.2f\ngore=%d\nsubtitles=%d\ndifficulty=%d\nfilmGrain=%d\ntouchScale=%.2f\n",
                 s.mouseSens, s.invertY ? 1 : 0, s.masterVol, s.musicVol, s.sfxVol, s.voiceVol, s.gore, s.subtitles ? 1 : 0, s.difficulty, s.filmGrain ? 1 : 0, s.touchScale);
    for (int a = 0; a < ACT_COUNT; a++) n += fmt_str(buf + n, sizeof(buf) - n, "bind%d=%d,%d\n", a, s.keys[a][0], s.keys[a][1]);
    return plat_write_user_file("settings.cfg", buf, (size_t)min_t(n, (int)sizeof(buf) - 1));
}

bool settings_load(Settings* s) {
    settings_defaults(s);
    Array<u8> data = {};
    if (!plat_read_user_file("settings.cfg", &data)) return false;
    const char* p = (const char*)data.data;
    char line[256];
    while (*p) {
        int n = 0;
        while (*p && *p != '\n') { if (n < 255) line[n++] = *p; p++; }
        if (*p) p++;
        line[n] = 0;
        char* eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        const char* k = line;
        const char* v = eq + 1;
        if (str_eq(k, "quality")) s->quality = clamp_t(str_to_int(v), 0, 3);
        else if (str_eq(k, "renderScale")) s->renderScale = clampf(str_to_float(v), 0.5f, 1.0f);
        else if (str_eq(k, "fullscreen")) s->fullscreen = str_to_int(v) != 0;
        else if (str_eq(k, "vsync")) s->vsync = str_to_int(v) != 0;
        else if (str_eq(k, "brightness")) s->brightness = clampf(str_to_float(v), 0.5f, 1.6f);
        else if (str_eq(k, "fov")) s->fov = clampf(str_to_float(v), 50.0f, 90.0f);
        else if (str_eq(k, "mouseSens")) s->mouseSens = clampf(str_to_float(v), 0.1f, 4.0f);
        else if (str_eq(k, "invertY")) s->invertY = str_to_int(v) != 0;
        else if (str_eq(k, "masterVol")) s->masterVol = clampf(str_to_float(v), 0, 1);
        else if (str_eq(k, "musicVol")) s->musicVol = clampf(str_to_float(v), 0, 1);
        else if (str_eq(k, "sfxVol")) s->sfxVol = clampf(str_to_float(v), 0, 1);
        else if (str_eq(k, "voiceVol")) s->voiceVol = clampf(str_to_float(v), 0, 1);
        else if (str_eq(k, "gore")) s->gore = clamp_t(str_to_int(v), 0, 2);
        else if (str_eq(k, "subtitles")) s->subtitles = str_to_int(v) != 0;
        else if (str_eq(k, "difficulty")) s->difficulty = clamp_t(str_to_int(v), 0, 2);
        else if (str_eq(k, "filmGrain")) s->filmGrain = str_to_int(v) != 0;
        else if (str_eq(k, "touchScale")) s->touchScale = clampf(str_to_float(v), 0.6f, 1.6f);
        else if (str_starts(k, "bind")) {
            int a = str_to_int(k + 4);
            const char* comma = strchr(v, ',');
            if (a >= 0 && a < ACT_COUNT && comma) {
                s->keys[a][0] = clamp_t(str_to_int(v), 0, KEY_COUNT - 1);
                s->keys[a][1] = clamp_t(str_to_int(comma + 1), 0, KEY_COUNT - 1);
            }
        }
    }
    data.free_mem();
    return true;
}

void settings_apply(const Settings& s) {
    RenderSettings rs = renderer_settings();
    rs.quality = s.quality;
    rs.renderScale = plat_is_mobile() ? fminf(s.renderScale, 0.85f) : s.renderScale;
    rs.shadows = s.quality >= GQ_MEDIUM || plat_is_mobile();
    rs.bloom = true;
    rs.fxaa = s.quality >= GQ_MEDIUM && !plat_is_mobile();
    rs.drawDistance = plat_is_mobile() ? 380.0f : (s.quality == GQ_LOW ? 450.0f : s.quality == GQ_MEDIUM ? 650.0f : 900.0f);
    rs.rainDensity = plat_is_mobile() ? 1500 : (s.quality == GQ_LOW ? 2000 : s.quality == GQ_ULTRA ? 9000 : 6000);
    rs.brightness = s.brightness;
    rs.fov = s.fov;
    rs.filmGrain = s.filmGrain;
    rs.chromatic = s.quality >= GQ_HIGH && !plat_is_mobile();
    rs.vsync = s.vsync;
    renderer_apply_settings(rs);
    fx_set_gore_level(s.gore);
    plat_set_fullscreen(s.fullscreen);
    audio_set_volumes(s.masterVol, s.musicVol, s.sfxVol, s.voiceVol);
}

// ---------------------------------------------------------------------------
// Touch controls
// ---------------------------------------------------------------------------
enum TouchRole { TR_NONE, TR_JOY, TR_LOOK, TR_BUTTON };
static int g_touchRole[MAX_TOUCHES];
static int g_touchButton[MAX_TOUCHES];
static vec2 g_touchLast[MAX_TOUCHES];
static vec2 g_joyCenter;
static vec2 g_joyVec;
static bool g_joyActive;

void touch_layout(TouchLayout* l, int w, int h, bool vehicle, bool armed, bool menu) {
    float s = (float)h / 720.0f * g.settings.touchScale;
    memset(l, 0, sizeof(*l));
    l->joyCenter = g_joyActive ? g_joyCenter : vec2(150 * s, h - 150 * s);
    l->joyKnob = l->joyCenter + g_joyVec * (70 * s);
    l->joyRadius = 80 * s;
    l->joyActive = g_joyActive;
    if (menu) return;
    float r = 46 * s;
    float bx = (float)w - 100 * s, by = (float)h - 110 * s;
    auto add = [&](float x, float y, float rad, int action, const char* label) {
        if (l->count >= 14) return;
        TouchButton& b = l->buttons[l->count++];
        b.c = vec2(x, y);
        b.r = rad;
        b.action = action;
        b.label = label;
    };
    if (vehicle) {
        add(bx, by, r * 1.15f, ACT_FORWARD, "GAS");
        add(bx - 120 * s, by + 10 * s, r, ACT_BACK, "BRAKE");
        add(bx - 50 * s, by - 115 * s, r * 0.8f, ACT_JUMP, "DRIFT");
        add(bx - 170 * s, by - 90 * s, r * 0.75f, ACT_INTERACT, "EXIT");
        add(bx + 10 * s, by - 210 * s, r * 0.7f, ACT_HORN, "HORN");
    } else {
        add(bx, by, r * 1.15f, ACT_ATTACK, armed ? "FIRE" : "HIT");
        add(bx - 115 * s, by + 20 * s, r * 0.9f, ACT_AIM, armed ? "AIM" : "BLOCK");
        add(bx - 30 * s, by - 110 * s, r * 0.85f, ACT_JUMP, "JUMP");
        add(bx - 135 * s, by - 85 * s, r * 0.8f, ACT_DODGE, "DODGE");
        add(bx - 230 * s, by - 5 * s, r * 0.75f, ACT_KICK, "KICK");
        add(bx + 15 * s, by - 215 * s, r * 0.75f, ACT_INTERACT, "USE");
        add(bx - 95 * s, by - 190 * s, r * 0.65f, ACT_LOCKON, "LOCK");
        add(bx - 185 * s, by - 175 * s, r * 0.6f, ACT_CROUCH, "CROUCH");
        if (armed) add(bx - 230 * s, by - 100 * s, r * 0.6f, ACT_RELOAD, "RELOAD");
        add(bx - 260 * s, by - 250 * s, r * 0.55f, ACT_WEAPON_NEXT, "WEAPON");
    }
    add(w - 60 * s, 60 * s, r * 0.6f, ACT_PAUSE, "II");
    add(w - 150 * s, 60 * s, r * 0.6f, ACT_JOURNAL, "MAP");
}

static void touch_controls(Controls* c, InputState* in, bool playing) {
    TouchLayout lay;
    Actor& p = g.actors[PLAYER];
    touch_layout(&lay, g.width, g.height, p.state == AS_VEHICLE, p.weapon != W_FISTS, !playing);
    float s = (float)g.height / 720.0f;
    bool joyThisFrame = false;
    for (int i = 0; i < MAX_TOUCHES; i++) {
        TouchPoint& t = in->touches[i];
        if (t.began) {
            g_touchRole[i] = TR_NONE;
            vec2 pt(t.x, t.y);
            c->pointer = pt;
            c->pointerClick = true;
            if (playing) {
                for (int b = 0; b < lay.count; b++) {
                    if (length(pt - lay.buttons[b].c) < lay.buttons[b].r * 1.15f) {
                        g_touchRole[i] = TR_BUTTON;
                        g_touchButton[i] = lay.buttons[b].action;
                        break;
                    }
                }
                if (g_touchRole[i] == TR_NONE) {
                    if (t.x < g.width * 0.4f && t.y > g.height * 0.35f) {
                        g_touchRole[i] = TR_JOY;
                        g_joyCenter = pt;
                        g_joyActive = true;
                    } else {
                        g_touchRole[i] = TR_LOOK;
                    }
                }
            }
            g_touchLast[i] = pt;
        }
        if (!t.active && !t.ended) continue;
        vec2 pt(t.x, t.y);
        if (t.active) c->pointerDown = true;
        switch (g_touchRole[i]) {
        case TR_JOY: {
            vec2 d = (pt - g_joyCenter) * (1.0f / (70 * s));
            if (length(d) > 1.0f) d = normalize(d);
            g_joyVec = d;
            joyThisFrame = t.active;
            c->move = vec2(d.x, -d.y);
            if (length(d) > 0.95f && p.state != AS_VEHICLE) c->down[ACT_SPRINT] = true;
        } break;
        case TR_LOOK: {
            vec2 d = pt - g_touchLast[i];
            c->look.x += d.x * 0.006f * g.settings.mouseSens;
            c->look.y += d.y * 0.004f * g.settings.mouseSens;
        } break;
        case TR_BUTTON: {
            int a = g_touchButton[i];
            if (t.active) c->down[a] = true;
            if (t.began) c->pressed[a] = true;
            if (t.ended) c->released[a] = true;
            if (p.state == AS_VEHICLE && a == ACT_FORWARD && t.active) c->move.y = 1.0f;
            if (p.state == AS_VEHICLE && a == ACT_BACK && t.active) c->move.y = -1.0f;
        } break;
        }
        g_touchLast[i] = pt;
        if (t.ended) g_touchRole[i] = TR_NONE;
    }
    if (!joyThisFrame) {
        g_joyActive = false;
        g_joyVec = vec2(0, 0);
    }
    // Vehicle steering from the joystick
    if (p.state == AS_VEHICLE && g_joyActive) c->move.x = g_joyVec.x;
}

void controls_update(Controls* c, InputState* in, const Settings& s, bool playing, float dt) {
    bool prevDown[ACT_COUNT];
    memcpy(prevDown, c->down, sizeof(prevDown));
    float prevHeld[ACT_COUNT];
    memcpy(prevHeld, c->held, sizeof(prevHeld));
    memset(c, 0, sizeof(*c));
    c->pointer = vec2(in->mouseX, in->mouseY);
    c->pointerClick = in->pressed[KEY_MOUSE_LEFT];
    c->pointerDown = in->down[KEY_MOUSE_LEFT];
    c->wheel = in->wheel;
    // Keyboard & mouse bindings
    for (int a = 0; a < ACT_COUNT; a++) {
        for (int k = 0; k < 2; k++) {
            int key = s.keys[a][k];
            if (key <= 0 || key >= KEY_COUNT) continue;
            if (in->down[key]) c->down[a] = true;
        }
    }
    c->move.x = (c->down[ACT_RIGHT] ? 1.0f : 0.0f) - (c->down[ACT_LEFT] ? 1.0f : 0.0f);
    c->move.y = (c->down[ACT_FORWARD] ? 1.0f : 0.0f) - (c->down[ACT_BACK] ? 1.0f : 0.0f);
    float sens = 0.0024f * s.mouseSens;
    if (in->mouseCaptured) {
        c->look.x = in->mouseDX * sens;
        c->look.y = in->mouseDY * sens * (s.invertY ? -1.0f : 1.0f);
    }
    for (int k = KEY_1; k <= KEY_8; k++)
        if (in->pressed[k]) c->weaponSlot = k - KEY_0;
    // Gamepad
    if (in->padConnected) {
        if (fabsf(in->padLX) > 0.0f || fabsf(in->padLY) > 0.0f) c->move = vec2(in->padLX, in->padLY);
        c->look.x += in->padRX * 2.6f * dt * s.mouseSens;
        c->look.y += -in->padRY * 1.8f * dt * s.mouseSens * (s.invertY ? -1.0f : 1.0f);
        if (in->padDown[PAD_A]) c->down[ACT_JUMP] = true;
        if (in->padDown[PAD_B]) c->down[ACT_DODGE] = true;
        if (in->padDown[PAD_X]) c->down[ACT_ATTACK] = true;
        if (in->padDown[PAD_Y]) c->down[ACT_INTERACT] = true;
        if (in->padDown[PAD_RB]) c->down[ACT_KICK] = true;
        if (in->padDown[PAD_LB]) c->down[ACT_LOCKON] = true;
        if (in->padLT > 0.4f) c->down[ACT_AIM] = true;
        if (in->padRT > 0.4f) c->down[ACT_ATTACK] = true;
        if (in->padDown[PAD_LSTICK]) c->down[ACT_SPRINT] = true;
        if (in->padDown[PAD_RSTICK]) c->down[ACT_CROUCH] = true;
        if (in->padDown[PAD_START]) c->down[ACT_PAUSE] = true;
        if (in->padDown[PAD_BACK]) c->down[ACT_JOURNAL] = true;
        if (in->padDown[PAD_DUP]) c->down[ACT_RELOAD] = true;
        if (in->padDown[PAD_DRIGHT]) c->down[ACT_WEAPON_NEXT] = true;
        if (in->padDown[PAD_DLEFT]) c->down[ACT_WEAPON_PREV] = true;
        c->menuUp |= in->padPressed[PAD_DUP];
        c->menuDown |= in->padPressed[PAD_DDOWN];
        c->menuLeft |= in->padPressed[PAD_DLEFT];
        c->menuRight |= in->padPressed[PAD_DRIGHT];
        c->menuAccept |= in->padPressed[PAD_A];
        c->menuBack |= in->padPressed[PAD_B] || in->padPressed[PAD_START];
    }
    // Touch
    if (in->touchMode) {
        g.touch = true;
        touch_controls(c, in, playing);
    }
    for (int a = 0; a < ACT_COUNT; a++) {
        if (c->down[a] && !prevDown[a]) c->pressed[a] = true;
        if (!c->down[a] && prevDown[a]) c->released[a] = true;
        c->held[a] = c->down[a] ? prevHeld[a] + dt : 0.0f;
    }
    c->menuUp |= in->pressed[KEY_UP] || in->pressed[KEY_W];
    c->menuDown |= in->pressed[KEY_DOWN] || in->pressed[KEY_S];
    c->menuLeft |= in->pressed[KEY_LEFT] || in->pressed[KEY_A];
    c->menuRight |= in->pressed[KEY_RIGHT] || in->pressed[KEY_D];
    c->menuAccept |= in->pressed[KEY_ENTER] || in->pressed[KEY_SPACE];
    c->menuBack |= in->pressed[KEY_ESCAPE] || in->pressed[KEY_BACKSPACE];
    for (int k = 0; k < KEY_COUNT; k++)
        if (in->pressed[k]) { c->anyKey = true; break; }
    if (in->padConnected)
        for (int b = 0; b < PAD_COUNT; b++)
            if (in->padPressed[b]) c->anyKey = true;
    if (c->pointerClick) c->anyKey = true;
}
