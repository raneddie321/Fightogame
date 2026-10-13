// FIGHTO - menus and full-screen overlays: main menu, pause, settings, controls,
// journal & map, shop, upgrades, death/arrest, mission results, credits, ending.
#include "../game/game.h"
#include "../game/script.h"
#include "draw2d.h"
#include "../audio/audio.h"

void game_continue_from_menu();

static const vec4 kRed(0.86f, 0.08f, 0.06f, 1);
static const vec4 kWhite(1, 1, 1, 1);
static const vec4 kDim(1, 1, 1, 0.6f);
static const vec4 kGold(1.0f, 0.78f, 0.25f, 1);

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
static int g_hoverPrev = -1;

static bool hit(float x, float y, float w, float h) {
    vec2 p = g.ctl.pointer;
    return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
}

// Vertical list of buttons. Returns the index activated this frame, or -1.
static int button_list(float x, float y, float w, float h, float gap, const char** labels, const bool* enabled, int n, float size) {
    float s = ui_scale();
    int activated = -1;
    if (g.ctl.menuDown) { g.menuIndex = (g.menuIndex + 1) % n; audio_play(SND_UI_HOVER, 0.6f); }
    if (g.ctl.menuUp) { g.menuIndex = (g.menuIndex + n - 1) % n; audio_play(SND_UI_HOVER, 0.6f); }
    g.menuIndex = clamp_t(g.menuIndex, 0, n - 1);
    for (int i = 0; i < n; i++) {
        float by = y + i * (h + gap);
        bool over = hit(x, by, w, h);
        if (over && (g.input && (g.input->mouseDX != 0 || g.input->mouseDY != 0 || g.ctl.pointerClick))) {
            if (g.menuIndex != i) audio_play(SND_UI_HOVER, 0.5f);
            g.menuIndex = i;
        }
        bool sel = g.menuIndex == i;
        bool en = !enabled || enabled[i];
        if (sel) {
            ui_rect_grad(x, by, w, h, vec4(0.75f, 0.06f, 0.05f, 0.85f), vec4(0.35f, 0.02f, 0.02f, 0.85f));
            ui_rect(x, by, 5 * s, h, kWhite);
        } else {
            ui_rect(x, by, w, h, vec4(0, 0, 0, 0.45f));
        }
        ui_text(FONT_BOLD, x + 24 * s, by + (h - size) * 0.5f, size, labels[i], en ? kWhite : vec4(1, 1, 1, 0.3f));
        if (en && ((over && g.ctl.pointerClick) || (sel && g.ctl.menuAccept))) activated = i;
    }
    if (activated >= 0) audio_play(SND_UI_CLICK, 0.8f);
    g_hoverPrev = g.menuIndex;
    return activated;
}

static void panel(float x, float y, float w, float h) {
    float s = ui_scale();
    ui_rect(x, y, w, h, vec4(0.02f, 0.02f, 0.025f, 0.88f));
    ui_rect(x, y, w, 4 * s, kRed);
    ui_rect_outline(x, y, w, h, 1, vec4(1, 1, 1, 0.08f));
}

static void title_logo(float cx, float y, float size, float t) {
    float s = ui_scale();
    const char* title = "FIGHTO";
    // Chromatic glitch copies
    float jitter = (fmodf(t, 3.7f) < 0.12f) ? 6 * s : 0;
    ui_text(FONT_TITLE, cx - 3 * s - jitter, y, size, title, vec4(0.0f, 0.8f, 1.0f, 0.35f), ALIGN_CENTER);
    ui_text(FONT_TITLE, cx + 3 * s + jitter, y, size, title, vec4(1.0f, 0.0f, 0.1f, 0.45f), ALIGN_CENTER);
    ui_text(FONT_TITLE, cx + 4 * s, y + 5 * s, size, title, vec4(0, 0, 0, 0.8f), ALIGN_CENTER);
    ui_text(FONT_TITLE, cx, y, size, title, vec4(0.92f, 0.08f, 0.05f, 1), ALIGN_CENTER);
    // Blood drips running from the letters
    float w = ui_text_width(FONT_TITLE, size, title);
    for (int i = 0; i < 14; i++) {
        float hx = hash_f01(i * 7 + 1);
        float x = cx - w * 0.5f + w * (0.05f + 0.9f * hx);
        float speed = 0.04f + 0.08f * hash_f01(i * 13 + 5);
        float len = fmodf(t * speed + hash_f01(i * 3 + 2), 1.0f) * size * 0.9f;
        float dy = y + size * 0.86f;
        float dw = (2.5f + 4 * hash_f01(i * 11)) * s;
        ui_rect_grad(x, dy, dw, len, vec4(0.7f, 0.02f, 0.02f, 0.95f), vec4(0.45f, 0.0f, 0.0f, 0.9f));
        ui_circle(vec2(x + dw * 0.5f, dy + len), dw * 0.9f, vec4(0.55f, 0.0f, 0.0f, 0.95f), 10);
    }
}

static void footer_hint(const char* text) {
    float s = ui_scale();
    ui_text_shadow(FONT_UI, (float)ui_width() * 0.5f, (float)ui_height() - 50 * s, 20 * s, text, kDim, ALIGN_CENTER);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
enum SettingRow { SR_QUALITY, SR_SCALE, SR_FULLSCREEN, SR_VSYNC, SR_BRIGHT, SR_FOV, SR_GRAIN, SR_SENS, SR_INVERT, SR_MASTER, SR_MUSIC, SR_SFX, SR_GORE, SR_SUBS, SR_DIFF, SR_TOUCH, SR_BACK, SR_COUNT };

static void setting_text(int r, char* label, char* value) {
    Settings& st = g.settings;
    static const char* q[4] = {"Low", "Medium", "High", "Ultra"};
    static const char* gore[3] = {"Off", "Normal", "Extreme"};
    static const char* diff[3] = {"Easy", "Normal", "Hard"};
    value[0] = 0;
    switch (r) {
    case SR_QUALITY: str_copy(label, 48, "Graphics Quality"); str_copy(value, 48, q[st.quality]); break;
    case SR_SCALE: str_copy(label, 48, "Render Scale"); fmt_str(value, 48, "%d%%", (int)(st.renderScale * 100 + 0.5f)); break;
    case SR_FULLSCREEN: str_copy(label, 48, "Fullscreen"); str_copy(value, 48, st.fullscreen ? "On" : "Off"); break;
    case SR_VSYNC: str_copy(label, 48, "V-Sync"); str_copy(value, 48, st.vsync ? "On" : "Off"); break;
    case SR_BRIGHT: str_copy(label, 48, "Brightness"); fmt_str(value, 48, "%d%%", (int)(st.brightness * 100 + 0.5f)); break;
    case SR_FOV: str_copy(label, 48, "Field of View"); fmt_str(value, 48, "%d", (int)st.fov); break;
    case SR_GRAIN: str_copy(label, 48, "Film Grain"); str_copy(value, 48, st.filmGrain ? "On" : "Off"); break;
    case SR_SENS: str_copy(label, 48, "Look Sensitivity"); fmt_str(value, 48, "%.1f", st.mouseSens); break;
    case SR_INVERT: str_copy(label, 48, "Invert Look Y"); str_copy(value, 48, st.invertY ? "On" : "Off"); break;
    case SR_MASTER: str_copy(label, 48, "Master Volume"); fmt_str(value, 48, "%d%%", (int)(st.masterVol * 100 + 0.5f)); break;
    case SR_MUSIC: str_copy(label, 48, "Music Volume"); fmt_str(value, 48, "%d%%", (int)(st.musicVol * 100 + 0.5f)); break;
    case SR_SFX: str_copy(label, 48, "Effects Volume"); fmt_str(value, 48, "%d%%", (int)(st.sfxVol * 100 + 0.5f)); break;
    case SR_GORE: str_copy(label, 48, "Blood & Gore"); str_copy(value, 48, gore[st.gore]); break;
    case SR_SUBS: str_copy(label, 48, "Subtitles"); str_copy(value, 48, st.subtitles ? "On" : "Off"); break;
    case SR_DIFF: str_copy(label, 48, "Difficulty"); str_copy(value, 48, diff[st.difficulty]); break;
    case SR_TOUCH: str_copy(label, 48, "Touch Controls Size"); fmt_str(value, 48, "%d%%", (int)(st.touchScale * 100 + 0.5f)); break;
    default: str_copy(label, 48, "Back"); break;
    }
}

static void setting_change(int r, int dir) {
    Settings& st = g.settings;
    switch (r) {
    case SR_QUALITY: st.quality = clamp_t(st.quality + dir, 0, 3); break;
    case SR_SCALE: st.renderScale = clampf(st.renderScale + dir * 0.05f, 0.5f, 1.0f); break;
    case SR_FULLSCREEN: st.fullscreen = !st.fullscreen; break;
    case SR_VSYNC: st.vsync = !st.vsync; break;
    case SR_BRIGHT: st.brightness = clampf(st.brightness + dir * 0.05f, 0.5f, 1.6f); break;
    case SR_FOV: st.fov = clampf(st.fov + dir * 2.0f, 50.0f, 90.0f); break;
    case SR_GRAIN: st.filmGrain = !st.filmGrain; break;
    case SR_SENS: st.mouseSens = clampf(st.mouseSens + dir * 0.1f, 0.1f, 4.0f); break;
    case SR_INVERT: st.invertY = !st.invertY; break;
    case SR_MASTER: st.masterVol = clampf(st.masterVol + dir * 0.05f, 0, 1); break;
    case SR_MUSIC: st.musicVol = clampf(st.musicVol + dir * 0.05f, 0, 1); break;
    case SR_SFX: st.sfxVol = clampf(st.sfxVol + dir * 0.05f, 0, 1); break;
    case SR_GORE: st.gore = (st.gore + dir + 3) % 3; break;
    case SR_SUBS: st.subtitles = !st.subtitles; break;
    case SR_DIFF: st.difficulty = clamp_t(st.difficulty + dir, 0, 2); break;
    case SR_TOUCH: st.touchScale = clampf(st.touchScale + dir * 0.1f, 0.6f, 1.6f); break;
    default: return;
    }
    settings_apply(st);
    audio_play(SND_UI_CLICK, 0.6f);
}

static void screen_settings() {
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    float pw = 900 * s, ph = H - 160 * s;
    float px = W * 0.5f - pw * 0.5f, py = 80 * s;
    panel(px, py, pw, ph);
    ui_text_shadow(FONT_TITLE, px + 40 * s, py + 26 * s, 54 * s, "SETTINGS", kWhite);
    float rowH = fminf(44 * s, (ph - 160 * s) / SR_COUNT);
    float y0 = py + 110 * s;
    if (g.ctl.menuDown) { g.menuIndex = (g.menuIndex + 1) % SR_COUNT; audio_play(SND_UI_HOVER, 0.5f); }
    if (g.ctl.menuUp) { g.menuIndex = (g.menuIndex + SR_COUNT - 1) % SR_COUNT; audio_play(SND_UI_HOVER, 0.5f); }
    for (int r = 0; r < SR_COUNT; r++) {
        float y = y0 + r * rowH;
        bool over = hit(px + 20 * s, y, pw - 40 * s, rowH - 4 * s);
        if (over && g.input && (g.input->mouseDX != 0 || g.input->mouseDY != 0 || g.ctl.pointerClick)) g.menuIndex = r;
        bool sel = g.menuIndex == r;
        if (sel) ui_rect(px + 20 * s, y, pw - 40 * s, rowH - 4 * s, vec4(0.75f, 0.06f, 0.05f, 0.5f));
        char label[48], value[48];
        setting_text(r, label, value);
        float ts = rowH * 0.55f;
        ui_text(FONT_UI, px + 40 * s, y + (rowH - ts) * 0.4f, ts, label, kWhite);
        if (value[0]) {
            char v2[64];
            fmt_str(v2, sizeof(v2), "<  %s  >", value);
            ui_text(FONT_BOLD, px + pw - 40 * s, y + (rowH - ts) * 0.4f, ts, v2, sel ? kGold : kWhite, ALIGN_RIGHT);
            if (over && g.ctl.pointerClick) setting_change(r, g.ctl.pointer.x > px + pw - 140 * s ? 1 : -1);
        }
        if (sel && g.ctl.menuLeft) setting_change(r, -1);
        if (sel && g.ctl.menuRight) setting_change(r, 1);
        if (r == SR_BACK && ((over && g.ctl.pointerClick) || (sel && g.ctl.menuAccept))) {
            settings_save(g.settings);
            game_set_screen(g.settingsReturn);
            return;
        }
    }
    if (g.ctl.menuBack) {
        settings_save(g.settings);
        audio_play(SND_UI_BACK, 0.7f);
        game_set_screen(g.settingsReturn);
    }
    footer_hint("Up/Down select  |  Left/Right change  |  Esc back");
}

static void screen_controls() {
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    float pw = 1100 * s, ph = H - 140 * s;
    float px = W * 0.5f - pw * 0.5f, py = 70 * s;
    panel(px, py, pw, ph);
    ui_text_shadow(FONT_TITLE, px + 40 * s, py + 22 * s, 50 * s, "CONTROLS", kWhite);
    int rows = ACT_COUNT + 2;
    float rowH = fminf(36 * s, (ph - 200 * s) / rows);
    float y0 = py + 100 * s;
    if (g.rebindAction >= 0) {
        int k = g.input ? g.input->lastKeyPressed : 0;
        if (k == KEY_ESCAPE) { g.rebindAction = -1; }
        else if (k != KEY_NONE) {
            g.settings.keys[g.rebindAction][g.rebindSlot] = k;
            g.rebindAction = -1;
            audio_play(SND_UI_CLICK, 0.8f);
            settings_save(g.settings);
        }
    } else {
        if (g.ctl.menuDown) g.menuIndex = (g.menuIndex + 1) % rows;
        if (g.ctl.menuUp) g.menuIndex = (g.menuIndex + rows - 1) % rows;
    }
    for (int r = 0; r < rows; r++) {
        float y = y0 + r * rowH;
        bool over = hit(px + 20 * s, y, pw - 40 * s, rowH - 2);
        if (over && g.rebindAction < 0 && g.input && (g.input->mouseDX != 0 || g.input->mouseDY != 0)) g.menuIndex = r;
        bool sel = g.menuIndex == r;
        if (sel) ui_rect(px + 20 * s, y, pw - 40 * s, rowH - 2, vec4(0.75f, 0.06f, 0.05f, 0.45f));
        float ts = rowH * 0.6f;
        if (r < ACT_COUNT) {
            ui_text(FONT_UI, px + 40 * s, y + rowH * 0.15f, ts, action_name(r), kWhite);
            for (int slot = 0; slot < 2; slot++) {
                float bx = px + pw - 420 * s + slot * 200 * s;
                bool waiting = g.rebindAction == r && g.rebindSlot == slot;
                const char* kn = g.settings.keys[r][slot] ? key_name(g.settings.keys[r][slot]) : "-";
                ui_rect(bx, y + 2, 180 * s, rowH - 6, waiting ? vec4(1, 0.8f, 0.2f, 0.4f) : vec4(1, 1, 1, 0.08f));
                ui_text(FONT_BOLD, bx + 90 * s, y + rowH * 0.15f, ts, waiting ? "press a key..." : kn, kWhite, ALIGN_CENTER);
                if (hit(bx, y, 180 * s, rowH) && g.ctl.pointerClick && g.rebindAction < 0) { g.rebindAction = r; g.rebindSlot = slot; }
            }
            if (sel && g.ctl.menuAccept && g.rebindAction < 0 && !g.ctl.pointerClick) { g.rebindAction = r; g.rebindSlot = 0; }
        } else if (r == ACT_COUNT) {
            ui_text(FONT_BOLD, px + 40 * s, y + rowH * 0.15f, ts, "Reset to defaults", kGold);
            if ((over && g.ctl.pointerClick) || (sel && g.ctl.menuAccept)) {
                Settings d;
                settings_defaults(&d);
                memcpy(g.settings.keys, d.keys, sizeof(d.keys));
                settings_save(g.settings);
                audio_play(SND_UI_CLICK, 0.8f);
            }
        } else {
            ui_text(FONT_BOLD, px + 40 * s, y + rowH * 0.15f, ts, "Back", kWhite);
            if ((over && g.ctl.pointerClick) || (sel && g.ctl.menuAccept)) { game_set_screen(g.settingsReturn); return; }
        }
    }
    if (g.rebindAction < 0 && g.ctl.menuBack) { audio_play(SND_UI_BACK, 0.7f); game_set_screen(g.settingsReturn); }
    ui_text_wrapped(FONT_UI, px + pw * 0.5f, py + ph - 70 * s, 18 * s, pw - 80 * s,
                    "Gamepad: Left stick move | Right stick look | A jump | B dodge | X attack | RB kick | LT aim/block | RT fire | Y interact | LB lock-on | Start pause. Touch: on-screen joystick and buttons.",
                    kDim, ALIGN_CENTER);
}

// ---------------------------------------------------------------------------
// Journal
// ---------------------------------------------------------------------------
struct EvidenceText { char title[64]; char body[700]; };
static EvidenceText g_evidence[MAX_EVIDENCE];
static bool g_evidenceLoaded;

static void load_evidence() {
    g_evidenceLoaded = true;
    Array<u8> d = {};
    if (!plat_read_asset("dialogue/evidence.txt", &d)) return;
    const char* p = (const char*)d.data;
    int cur = -1;
    char line[512];
    while (*p) {
        int n = 0;
        while (*p && *p != '\n') { if (n < 511) line[n++] = *p; p++; }
        if (*p) p++;
        line[n] = 0;
        if (line[0] == '#') continue;
        if (line[0] == '@') {
            char* sp = strchr(line, ' ');
            cur = str_to_int(line + 1);
            if (cur >= 0 && cur < MAX_EVIDENCE) str_copy(g_evidence[cur].title, 64, sp ? sp + 1 : "Evidence");
            continue;
        }
        if (cur >= 0 && cur < MAX_EVIDENCE && n > 0) {
            EvidenceText& e = g_evidence[cur];
            int len = (int)strlen(e.body);
            if (len > 0 && len < 690) { e.body[len++] = ' '; e.body[len] = 0; }
            str_copy(e.body + len, 700 - len, line);
        }
    }
    d.free_mem();
}

static void screen_journal() {
    if (!g_evidenceLoaded) load_evidence();
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    float pw = fminf(W - 80 * s, 1500 * s), ph = H - 120 * s;
    float px = W * 0.5f - pw * 0.5f, py = 60 * s;
    panel(px, py, pw, ph);
    static const char* tabs[4] = {"MISSIONS", "MAP", "EVIDENCE", "STATS"};
    if (g.ctl.menuRight || g.ctl.pressed[ACT_INTERACT]) g.journalTab = (g.journalTab + 1) % 4;
    if (g.ctl.menuLeft || g.ctl.pressed[ACT_LOCKON]) g.journalTab = (g.journalTab + 3) % 4;
    for (int t = 0; t < 4; t++) {
        float tx = px + 40 * s + t * 230 * s;
        bool sel = g.journalTab == t;
        if (hit(tx, py + 20 * s, 210 * s, 50 * s) && g.ctl.pointerClick) { g.journalTab = t; audio_play(SND_UI_CLICK, 0.6f); }
        ui_rect(tx, py + 20 * s, 210 * s, 50 * s, sel ? vec4(0.75f, 0.06f, 0.05f, 0.85f) : vec4(1, 1, 1, 0.06f));
        ui_text(FONT_BOLD, tx + 105 * s, py + 30 * s, 28 * s, tabs[t], kWhite, ALIGN_CENTER);
    }
    float cx = px + 40 * s, cy = py + 100 * s, cw = pw - 80 * s, ch = ph - 160 * s;
    char buf[256];
    if (g.journalTab == 0) {
        if (mission_active()) {
            ui_text_shadow(FONT_BOLD, cx, cy, 26 * s, "CURRENT", kGold);
            ui_text_shadow(FONT_BOLD, cx, cy + 34 * s, 30 * s, mission_title(), kWhite);
            ui_text_wrapped(FONT_UI, cx, cy + 72 * s, 24 * s, cw * 0.45f, mission_objective(), kDim);
            cy += 140 * s;
        }
        ui_text_shadow(FONT_BOLD, cx, cy, 26 * s, "STORY", kGold);
        float y = cy + 40 * s;
        for (int i = 0; i < mission_count(); i++) {
            if (y > py + ph - 60 * s) break;
            bool done = (g.pd.missionsDone >> (i & 31)) & 1;  // ids are assigned in load order
            bool avail = mission_available(i);
            const char* st = avail ? "AVAILABLE" : "";
            vec4 col = avail ? kWhite : vec4(1, 1, 1, 0.35f);
            FIGHTO_UNUSED(done);
            fmt_str(buf, sizeof(buf), "%s", mission_name(i));
            ui_text(FONT_UI, cx + 20 * s, y, 24 * s, buf, col);
            if (st[0]) ui_text(FONT_BOLD, cx + cw * 0.45f, y, 20 * s, st, kGold, ALIGN_RIGHT);
            y += 32 * s;
        }
        // Mission descriptions for available ones (right column)
        float ry = cy + 40 * s;
        ui_text_shadow(FONT_BOLD, cx + cw * 0.52f, cy, 26 * s, "BRIEFINGS", kGold);
        for (int i = 0; i < mission_count(); i++) {
            if (!mission_available(i)) continue;
            if (ry > py + ph - 120 * s) break;
            ui_text_shadow(FONT_BOLD, cx + cw * 0.52f, ry, 24 * s, mission_name(i), kWhite);
            ry += 30 * s;
            ry += ui_text_wrapped(FONT_UI, cx + cw * 0.52f, ry, 20 * s, cw * 0.46f, mission_desc(i), kDim) + 14 * s;
        }
    } else if (g.journalTab == 1) {
        if (g.mapZoom <= 0) { g.mapZoom = 1.0f; g.mapCenter = vec2(g.actors[PLAYER].pos.x, g.actors[PLAYER].pos.z); }
        if (g.ctl.wheel != 0) g.mapZoom = clampf(g.mapZoom * (g.ctl.wheel > 0 ? 1.2f : 0.83f), 0.6f, 4.0f);
        if (g.ctl.pointerDown && g.input) { g.mapCenter.x -= g.input->mouseDX / (ch / (CITY_HALF * 2.3f) * g.mapZoom); g.mapCenter.y -= g.input->mouseDY / (ch / (CITY_HALF * 2.3f) * g.mapZoom); }
        ui_clip(cx, cy, cw, ch);
        ui_rect(cx, cy, cw, ch, vec4(0.04f, 0.04f, 0.05f, 1));
        map_render(cx, cy, cw, ch, true);
        ui_clip(0, 0, 0, 0);
        const char* legend[] = {"Downtown", "The Blocks", "Ironworks", "West Hills", "Midtown"};
        vec4 lc[] = {vec4(0.16f, 0.2f, 0.3f, 1), vec4(0.28f, 0.17f, 0.15f, 1), vec4(0.22f, 0.22f, 0.18f, 1), vec4(0.14f, 0.24f, 0.15f, 1), vec4(0.2f, 0.2f, 0.24f, 1)};
        for (int i = 0; i < 5; i++) {
            ui_rect(cx + 20 * s, cy + 20 * s + i * 30 * s, 22 * s, 22 * s, lc[i]);
            ui_text_shadow(FONT_UI, cx + 52 * s, cy + 20 * s + i * 30 * s, 20 * s, legend[i], kWhite);
        }
        ui_circle(vec2(cx + 31 * s, cy + 186 * s), 7 * s, kGold, 12);
        ui_text_shadow(FONT_UI, cx + 52 * s, cy + 175 * s, 20 * s, "Mission", kWhite);
        ui_text_shadow(FONT_UI, cx + cw - 20 * s, cy + ch - 34 * s, 18 * s, "Wheel: zoom  |  Drag: pan", kDim, ALIGN_RIGHT);
    } else if (g.journalTab == 2) {
        int count = 0;
        float y = cy;
        for (int i = 0; i < MAX_EVIDENCE; i++) {
            if (!g.pd.evidence[i]) continue;
            count++;
            if (y > py + ph - 100 * s) break;
            ui_text_shadow(FONT_BOLD, cx, y, 24 * s, g_evidence[i].title[0] ? g_evidence[i].title : "Evidence", kGold);
            y += 30 * s;
            y += ui_text_wrapped(FONT_UI, cx, y, 20 * s, cw, g_evidence[i].body, kDim) + 16 * s;
        }
        fmt_str(buf, sizeof(buf), "Evidence collected: %d / %d", count, g_world.evidenceSpots.count < MAX_EVIDENCE ? g_world.evidenceSpots.count : MAX_EVIDENCE);
        ui_text_shadow(FONT_BOLD, cx + cw, py + 30 * s + 70 * s, 22 * s, buf, kWhite, ALIGN_RIGHT);
        if (count == 0) ui_text_shadow(FONT_UI, cx, cy, 24 * s, "No evidence yet. Look for glowing documents in alleys and key locations.", kDim);
    } else {
        const PlayerData& pd = g.pd;
        int acc = pd.shotsFired > 0 ? pd.shotsHit * 100 / pd.shotsFired : 0;
        int caches = 0;
        for (int i = 0; i < 32; i++) caches += pd.cachesFound[i] ? 1 : 0;
        const char* lines[12];
        char tmp[12][96];
        fmt_str(tmp[0], 96, "Play time: %d min", (int)(pd.playTime / 60));
        fmt_str(tmp[1], 96, "Kills: %d   Knockouts: %d", pd.kills, pd.knockouts);
        fmt_str(tmp[2], 96, "Shots fired: %d   Accuracy: %d%%", pd.shotsFired, acc);
        fmt_str(tmp[3], 96, "Fight Pit wins: %d", pd.pitWins);
        fmt_str(tmp[4], 96, "Distance driven: %.1f km", pd.distanceDriven / 1000.0f);
        fmt_str(tmp[5], 96, "Times arrested: %d   Deaths: %d", pd.timesArrested, pd.deaths);
        fmt_str(tmp[6], 96, "Hidden caches: %d / %d", caches, g_world.cacheSpots.count);
        fmt_str(tmp[7], 96, "Upgrades - Health %d  Stamina %d  Melee %d  Armour %d  Handling %d", pd.upgrades[0], pd.upgrades[1], pd.upgrades[2], pd.upgrades[3], pd.upgrades[4]);
        for (int i = 0; i < 8; i++) lines[i] = tmp[i];
        for (int i = 0; i < 8; i++) ui_text_shadow(FONT_UI, cx, cy + i * 36 * s, 24 * s, lines[i], kWhite);
        ui_text_shadow(FONT_BOLD, cx + cw * 0.55f, cy, 26 * s, "REPUTATION", kGold);
        int facs[5] = {FAC_CIVILIAN, FAC_POLICE, FAC_KINGS, FAC_VARGA, FAC_AEGIS};
        for (int i = 0; i < 5; i++) {
            float y = cy + 46 * s + i * 54 * s;
            ui_text(FONT_UI, cx + cw * 0.55f, y, 22 * s, faction_name(facs[i]), kWhite);
            float bx = cx + cw * 0.55f, bw = cw * 0.4f;
            ui_rect(bx, y + 28 * s, bw, 8 * s, vec4(1, 1, 1, 0.1f));
            float v = pd.rep[facs[i]] / 100.0f;
            float mid = bx + bw * 0.5f;
            if (v >= 0) ui_rect(mid, y + 28 * s, bw * 0.5f * v, 8 * s, vec4(0.4f, 0.9f, 0.4f, 1));
            else ui_rect(mid + bw * 0.5f * v, y + 28 * s, -bw * 0.5f * v, 8 * s, kRed);
            ui_rect(mid - 1, y + 24 * s, 2, 16 * s, kWhite);
        }
    }
    footer_hint("Left/Right or click: tabs  |  Tab / Esc: close");
    if (g.ctl.menuBack || g.ctl.pressed[ACT_JOURNAL] || g.ctl.pressed[ACT_MAP]) { g.mapZoom = 0; game_set_screen(SCR_PLAYING); }
}

// ---------------------------------------------------------------------------
// Shop & upgrades
// ---------------------------------------------------------------------------
struct ShopItem { const char* name; int price; int weapon; int kind; };   // kind 0 weapon, 1 ammo, 2 armour, 3 medkit
static const ShopItem g_shop[] = {
    {"Kestrel P9 pistol", 300, W_PISTOL, 0}, {"Breacher 12 shotgun", 900, W_SHOTGUN, 0}, {"Hornet SMG", 1400, W_SMG, 0},
    {"Gravedigger .50 revolver", 1800, W_MAGNUM, 0}, {"Ravager AR rifle", 2500, W_RIFLE, 0},
    {"Pistol ammo (45)", 40, W_PISTOL, 1}, {"Shotgun shells (12)", 60, W_SHOTGUN, 1}, {"SMG ammo (90)", 80, W_SMG, 1},
    {"Revolver rounds (12)", 90, W_MAGNUM, 1}, {"Rifle ammo (60)", 120, W_RIFLE, 1},
    {"Body armour", 250, 0, 2}, {"Medkit", 80, 0, 3},
};

static void screen_shop() {
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    float pw = 900 * s, ph = H - 160 * s;
    float px = W * 0.5f - pw * 0.5f, py = 80 * s;
    panel(px, py, pw, ph);
    ui_text_shadow(FONT_TITLE, px + 40 * s, py + 24 * s, 50 * s, "ACE PAWN & ARMS", kWhite);
    char buf[128];
    fmt_str(buf, sizeof(buf), "$%d", g.pd.money);
    ui_text_shadow(FONT_BOLD, px + pw - 40 * s, py + 36 * s, 34 * s, buf, vec4(0.55f, 1, 0.55f, 1), ALIGN_RIGHT);
    int n = ARRAY_COUNT(g_shop) + 1;
    const char* labels[16];
    char text[16][96];
    bool en[16];
    for (int i = 0; i < ARRAY_COUNT(g_shop); i++) {
        const ShopItem& it = g_shop[i];
        bool owned = it.kind == 0 && g.pd.hasWeapon[it.weapon];
        bool needGun = it.kind == 1 && !g.pd.hasWeapon[it.weapon];
        if (it.kind == 0 && it.weapon == W_RIFLE && !(g.pd.missionsDone & (1u << 4))) { fmt_str(text[i], 96, "%s  (after 'A Simple Job')", it.name); en[i] = false; }
        else if (owned) { fmt_str(text[i], 96, "%s  - OWNED", it.name); en[i] = false; }
        else if (needGun) { fmt_str(text[i], 96, "%s  (need the weapon)", it.name); en[i] = false; }
        else { fmt_str(text[i], 96, "%s   $%d", it.name, it.price); en[i] = g.pd.money >= it.price; }
        labels[i] = text[i];
    }
    labels[n - 1] = "Leave";
    en[n - 1] = true;
    float bh = fminf(48 * s, (ph - 160 * s) / n - 6 * s);
    int a = button_list(px + 40 * s, py + 110 * s, pw - 80 * s, bh, 6 * s, labels, en, n, bh * 0.5f);
    if (a == n - 1 || g.ctl.menuBack) { game_set_screen(SCR_PLAYING); return; }
    if (a >= 0) {
        const ShopItem& it = g_shop[a];
        g.pd.money -= it.price;
        if (it.kind == 0) player_give_weapon(it.weapon, g_weapons[it.weapon].clipSize * 2);
        else if (it.kind == 1) { int amt = it.weapon == W_PISTOL ? 45 : it.weapon == W_SHOTGUN ? 12 : it.weapon == W_SMG ? 90 : it.weapon == W_MAGNUM ? 12 : 60; g.pd.reserve[it.weapon] = min_t(g.pd.reserve[it.weapon] + amt, g_weapons[it.weapon].maxReserve); }
        else if (it.kind == 2) g.actors[PLAYER].armor = 100;
        else player_heal(60);
        audio_play(SND_MONEY, 0.7f);
    }
}

static const char* g_upNames[UP_COUNT] = {"Health", "Stamina", "Melee Damage", "Armour Plating", "Weapon & Car Handling"};
static int upgrade_price(int up, int level) { static const int base[UP_COUNT] = {500, 400, 600, 700, 500}; return base[up] * (level + 1) * (level + 1); }

static void screen_upgrades() {
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    float pw = 900 * s, ph = 620 * s;
    float px = W * 0.5f - pw * 0.5f, py = H * 0.5f - ph * 0.5f;
    panel(px, py, pw, ph);
    ui_text_shadow(FONT_TITLE, px + 40 * s, py + 24 * s, 50 * s, "UPGRADES", kWhite);
    char buf[64];
    fmt_str(buf, sizeof(buf), "$%d", g.pd.money);
    ui_text_shadow(FONT_BOLD, px + pw - 40 * s, py + 36 * s, 34 * s, buf, vec4(0.55f, 1, 0.55f, 1), ALIGN_RIGHT);
    const char* labels[UP_COUNT + 1];
    char text[UP_COUNT][96];
    bool en[UP_COUNT + 1];
    for (int i = 0; i < UP_COUNT; i++) {
        int lv = g.pd.upgrades[i];
        if (lv >= 3) { fmt_str(text[i], 96, "%s  [MAX]", g_upNames[i]); en[i] = false; }
        else { fmt_str(text[i], 96, "%s  Lv %d -> %d   $%d", g_upNames[i], lv, lv + 1, upgrade_price(i, lv)); en[i] = g.pd.money >= upgrade_price(i, lv); }
        labels[i] = text[i];
    }
    labels[UP_COUNT] = "Leave";
    en[UP_COUNT] = true;
    int a = button_list(px + 40 * s, py + 120 * s, pw - 80 * s, 64 * s, 10 * s, labels, en, UP_COUNT + 1, 28 * s);
    if (a == UP_COUNT || g.ctl.menuBack) { game_set_screen(SCR_PLAYING); return; }
    if (a >= 0) {
        g.pd.money -= upgrade_price(a, g.pd.upgrades[a]);
        g.pd.upgrades[a]++;
        if (a == UP_ARMOR) g.actors[PLAYER].armor = fminf(100.0f, g.actors[PLAYER].armor + 35.0f);
        g.actors[PLAYER].maxHealth = player_max_health();
        g.actors[PLAYER].health = g.actors[PLAYER].maxHealth;
        audio_play(SND_MONEY, 0.7f);
        notify("Upgrade purchased", kGold);
    }
}

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------
static void big_banner(const char* title, vec4 col, const char* sub) {
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    ui_rect(0, H * 0.3f, W, H * 0.4f, vec4(0, 0, 0, 0.65f));
    ui_text_shadow(FONT_TITLE, W * 0.5f, H * 0.36f, 110 * s, title, col, ALIGN_CENTER);
    if (sub) ui_text_wrapped(FONT_UI, W * 0.5f, H * 0.36f + 130 * s, 28 * s, W * 0.6f, sub, kWhite, ALIGN_CENTER);
}

static const char* g_credits[] = {
    "FIGHTO", "",
    "An original action game", "",
    "DESIGN, CODE, WORLD, CHARACTERS, AUDIO & STORY", "Created for FightO", "",
    "TECHNOLOGY", "Custom engine-free C++ framework", "OpenGL 3.3 / OpenGL ES 3.0 renderer", "Win32, X11 and Android platform layers written for FIGHTO",
    "Procedural textures, meshes, animation and audio synthesis", "",
    "THIRD-PARTY", "stb_image / stb_image_write by Sean Barrett (public domain)", "Inter typeface by Rasmus Andersson (SIL Open Font License 1.1)", "",
    "CAST", "Damian Cross", "Marcus Reyes", "Eli Cross", "Nikolai Varga", "Cmdr. Ava Kincaid", "Director Elias Thorne", "Dr. Imani Okafor", "\"Brick\" Halloran", "Tommy Kowalski", "The Overseer", "",
    "No game engine was used to make this game.", "", "Thank you for playing.",
};

static void screen_credits(bool ending) {
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    ui_rect(0, 0, W, H, vec4(0, 0, 0, 0.82f));
    static float t0 = 0;
    if (g.menuAnim == 0) t0 = g.realTime;
    g.menuAnim = 1;
    float t = g.realTime - t0;
    float y = H - t * 60 * s;
    if (ending) {
        const char* epi = "The mothership hangs over Blackridge like a second moon. Somewhere inside it, Eli is still alive - the Shard in Damian's chest hums with his voice. "
                          "A.E.G.I.S. lit the beacon that called them here. Thorne knew. Kincaid is choosing sides. The war for Blackridge has only started.";
        ui_text_wrapped(FONT_UI, W * 0.5f, y, 30 * s, W * 0.6f, epi, kWhite, ALIGN_CENTER);
        y += 260 * s;
    }
    for (int i = 0; i < ARRAY_COUNT(g_credits); i++) {
        bool head = i == 0;
        bool section = g_credits[i][0] && (strchr(g_credits[i], 'A') && g_credits[i][1] >= 'A' && g_credits[i][1] <= 'Z' && strlen(g_credits[i]) > 4 && g_credits[i][2] >= 'A' && g_credits[i][2] <= 'Z');
        ui_text_shadow(head ? FONT_TITLE : (section ? FONT_BOLD : FONT_UI), W * 0.5f, y, head ? 100 * s : 30 * s, g_credits[i], head ? kRed : (section ? kGold : kWhite), ALIGN_CENTER);
        y += head ? 140 * s : 44 * s;
    }
    if (y < -50 || g.ctl.menuBack || g.ctl.menuAccept || g.ctl.pointerClick) {
        g.menuAnim = 0;
        game_set_screen(ending ? SCR_PLAYING : SCR_MAIN_MENU);
    }
}

// ---------------------------------------------------------------------------
// Main dispatcher
// ---------------------------------------------------------------------------
void menu_update_render() {
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    switch (g.screen) {
    case SCR_MAIN_MENU: {
        ui_rect_grad(0, 0, W * 0.55f, H, vec4(0, 0, 0, 0.85f), vec4(0, 0, 0, 0.55f));
        title_logo(W * 0.27f, H * 0.12f, 170 * s, g.realTime);
        const char* labels[6] = {"CONTINUE", "NEW GAME", "SETTINGS", "CONTROLS", "CREDITS", "QUIT"};
        bool en[6] = {g.hasSave, true, true, true, true, !plat_is_mobile()};
        int n = plat_is_mobile() ? 5 : 6;
        int a = button_list(W * 0.27f - 220 * s, H * 0.46f, 440 * s, 58 * s, 12 * s, labels, en, n, 30 * s);
        if (a == 0) game_continue_from_menu();
        else if (a == 1) { if (g.hasSave) game_set_screen(SCR_LOAD_CONFIRM); else game_new_game(); }
        else if (a == 2) { g.settingsReturn = SCR_MAIN_MENU; game_set_screen(SCR_SETTINGS); }
        else if (a == 3) { g.settingsReturn = SCR_MAIN_MENU; game_set_screen(SCR_CONTROLS); }
        else if (a == 4) { g.menuAnim = 0; game_set_screen(SCR_CREDITS); }
        else if (a == 5) g.quitRequested = true;
        ui_text_shadow(FONT_UI, 30 * s, H - 46 * s, 18 * s, "v1.0  |  Engine-free build  |  Mature: strong violence and blood", kDim);
        if (g.saveStatusTime > 0) ui_text_shadow(FONT_BOLD, W * 0.27f, H * 0.4f, 22 * s, g.saveStatus, kGold, ALIGN_CENTER);
    } break;
    case SCR_LOAD_CONFIRM: {
        panel(W * 0.5f - 420 * s, H * 0.5f - 160 * s, 840 * s, 320 * s);
        ui_text_wrapped(FONT_BOLD, W * 0.5f, H * 0.5f - 120 * s, 30 * s, 760 * s, "Start a new game? Your existing save stays on disk until you save over it.", kWhite, ALIGN_CENTER);
        const char* labels[2] = {"START NEW GAME", "CANCEL"};
        int a = button_list(W * 0.5f - 250 * s, H * 0.5f, 500 * s, 54 * s, 10 * s, labels, 0, 2, 28 * s);
        if (a == 0) game_new_game();
        else if (a == 1 || g.ctl.menuBack) game_set_screen(SCR_MAIN_MENU);
    } break;
    case SCR_PAUSE: {
        ui_rect(0, 0, W, H, vec4(0, 0, 0, 0.6f));
        title_logo(W * 0.5f, H * 0.08f, 110 * s, g.realTime);
        ui_text_shadow(FONT_BOLD, W * 0.5f, H * 0.08f + 140 * s, 28 * s, "PAUSED", kDim, ALIGN_CENTER);
        const char* labels[7] = {"RESUME", "JOURNAL & MAP", "SAVE GAME", "LOAD GAME", "SETTINGS", "CONTROLS", "QUIT TO MAIN MENU"};
        int a = button_list(W * 0.5f - 230 * s, H * 0.32f, 460 * s, 54 * s, 10 * s, labels, 0, 7, 28 * s);
        if (a == 0 || (g.ctl.menuBack && a < 0)) game_set_screen(SCR_PLAYING);
        else if (a == 1) { g.journalTab = 0; game_set_screen(SCR_JOURNAL); }
        else if (a == 2) {
            char st[96];
            save_game(1, st, sizeof(st));
            str_copy(g.saveStatus, sizeof(g.saveStatus), st);
            g.saveStatusTime = 3.0f;
        } else if (a == 3) {
            if (save_exists(1) || save_exists(0)) game_continue_from_menu();
            else { str_copy(g.saveStatus, sizeof(g.saveStatus), "No saved game found"); g.saveStatusTime = 3.0f; }
        } else if (a == 4) { g.settingsReturn = SCR_PAUSE; game_set_screen(SCR_SETTINGS); }
        else if (a == 5) { g.settingsReturn = SCR_PAUSE; game_set_screen(SCR_CONTROLS); }
        else if (a == 6) { mission_abort(); g.hasSave = save_exists(0) || save_exists(1); game_set_screen(SCR_MAIN_MENU); }
        if (g.saveStatusTime > 0) ui_text_shadow(FONT_BOLD, W * 0.5f, H * 0.88f, 26 * s, g.saveStatus, kGold, ALIGN_CENTER);
    } break;
    case SCR_SETTINGS:
        if (g.settingsReturn != SCR_MAIN_MENU) ui_rect(0, 0, W, H, vec4(0, 0, 0, 0.5f));
        screen_settings();
        break;
    case SCR_CONTROLS:
        if (g.settingsReturn != SCR_MAIN_MENU) ui_rect(0, 0, W, H, vec4(0, 0, 0, 0.5f));
        screen_controls();
        break;
    case SCR_JOURNAL: screen_journal(); break;
    case SCR_SHOP: screen_shop(); break;
    case SCR_UPGRADES: screen_upgrades(); break;
    case SCR_CREDITS: screen_credits(false); break;
    case SCR_ENDING: screen_credits(true); break;
    case SCR_DEAD: {
        big_banner("FLATLINED", kRed, "Damian bled out on the streets of Blackridge. St. Agnes patches him up - for $100.");
        const char* labels[1] = {"CONTINUE"};
        if (button_list(W * 0.5f - 200 * s, H * 0.72f, 400 * s, 54 * s, 0, labels, 0, 1, 28 * s) == 0) {
            game_respawn_player(false);
            g.pd.money = max_t(0, g.pd.money - 100);
            game_set_screen(SCR_PLAYING);
        }
    } break;
    case SCR_ARRESTED: {
        big_banner("ARRESTED", vec4(0.35f, 0.55f, 1.0f, 1), "Blackridge PD booked Damian. A night in the cells, a fine and half your ammunition confiscated.");
        const char* labels[1] = {"CONTINUE"};
        if (button_list(W * 0.5f - 200 * s, H * 0.72f, 400 * s, 54 * s, 0, labels, 0, 1, 28 * s) == 0) game_set_screen(SCR_PLAYING);
    } break;
    case SCR_MISSION_COMPLETE: {
        char sub[160];
        fmt_str(sub, sizeof(sub), "%s\nReward: $%d", mission_last_title(), mission_reward_last());
        big_banner("MISSION COMPLETE", kGold, sub);
        if (g.saveStatusTime > 0) ui_text_shadow(FONT_BOLD, W * 0.5f, H * 0.66f, 22 * s, g.saveStatus, kDim, ALIGN_CENTER);
        const char* labels[1] = {"CONTINUE"};
        if (button_list(W * 0.5f - 200 * s, H * 0.72f, 400 * s, 54 * s, 0, labels, 0, 1, 28 * s) == 0) {
            if (flag_get("game_complete") == 1) { flag_set("game_complete", 2); g.menuAnim = 0; game_set_screen(SCR_ENDING); }
            else game_set_screen(SCR_PLAYING);
        }
    } break;
    case SCR_MISSION_FAILED: {
        big_banner("MISSION FAILED", kRed, mission_fail_reason());
        const char* labels[2] = {"RETRY FROM CHECKPOINT", "ABANDON MISSION"};
        int a = button_list(W * 0.5f - 260 * s, H * 0.72f, 520 * s, 54 * s, 10 * s, labels, 0, 2, 28 * s);
        if (a == 0) mission_retry();
        else if (a == 1) {
            mission_abort();
            if (g.actors[PLAYER].state == AS_DEAD) game_respawn_player(false);
            game_set_screen(SCR_PLAYING);
        }
    } break;
    default:
        break;
    }
}
