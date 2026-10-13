// FIGHTO - scripted cinematic sequences (camera shots, subtitles, effects).
#include "game.h"
#include "script.h"
#include "../audio/audio.h"
#include "setpieces.h"

struct Shot {
    float duration;
    float fov;
    vec3 from, to, lookFrom, lookTo;
    char speaker[16];
    char line[240];
    char fx[16];
};
struct Cutscene {
    char id[40];
    int first, count;
};

static Array<Shot> g_shots;
static Array<Cutscene> g_scenes;
static int g_active = -1;
static int g_shot;
static float g_t;
static bool g_lineShown;

static const char* g_lmNames[LM_COUNT] = {
    "home", "diner", "laundromat", "pawn", "safehouse", "bus_stop", "police", "hospital", "plaza", "meridian",
    "fight_pit", "chop_shop", "warehouse", "substation", "nexus", "drain_hatch", "mansion", "car_lot",
    "aegis_lobby", "aegis_briefing", "aegis_lab", "aegis_range", "aegis_archive", "aegis_reactor", "nest",
};

// Position tokens: "@landmark dx dy dz" or "P dx dy dz" (player relative, resolved at play time).
// Stored with a tag in .x of `from` when relative to the player: we resolve landmarks at load time.
static int parse_cpos(const ScriptLine& l, int i, vec3* out, bool* playerRel) {
    const char* a = script_str(l, i);
    *playerRel = false;
    if (a[0] == '@') {
        vec3 b(0, 0, 0);
        for (int k = 0; k < LM_COUNT; k++) if (str_eq(a + 1, g_lmNames[k])) b = landmark_pos(k);
        *out = b + vec3(script_float(l, i + 1), script_float(l, i + 2), script_float(l, i + 3));
        return 4;
    }
    if (str_eq(a, "P")) {
        *playerRel = true;
        *out = vec3(script_float(l, i + 1), script_float(l, i + 2), script_float(l, i + 3));
        return 4;
    }
    *out = vec3(script_float(l, i), script_float(l, i + 1), script_float(l, i + 2));
    return 3;
}

struct ShotRel { bool from, to, lf, lt; };
static Array<ShotRel> g_rel;

bool cutscenes_load() {
    g_shots.clear();
    g_scenes.clear();
    g_rel.clear();
    Array<u8> data = {};
    if (!plat_read_asset("missions/cutscenes.cut", &data)) { log_error("cutscenes.cut missing"); return false; }
    const char* cur = (const char*)data.data;
    ScriptLine l;
    int ln = 0;
    while (script_next_line(&cur, &l, &ln)) {
        const char* op = l.argv[0];
        if (op[0] == '@') {
            Cutscene c;
            memset(&c, 0, sizeof(c));
            str_copy(c.id, sizeof(c.id), op + 1);
            c.first = g_shots.count;
            g_scenes.push(c);
        } else if (str_eq(op, "shot") && g_scenes.count > 0) {
            Shot s;
            memset(&s, 0, sizeof(s));
            ShotRel r;
            int i = 1;
            s.duration = script_float(l, i++);
            s.fov = script_float(l, i++);
            i += parse_cpos(l, i, &s.from, &r.from);
            i += parse_cpos(l, i, &s.to, &r.to);
            i += parse_cpos(l, i, &s.lookFrom, &r.lf);
            i += parse_cpos(l, i, &s.lookTo, &r.lt);
            g_shots.push(s);
            g_rel.push(r);
            g_scenes.last().count++;
        } else if (str_eq(op, "line") && g_shots.count > 0) {
            str_copy(g_shots.last().speaker, sizeof(g_shots.last().speaker), script_str(l, 1));
            str_copy(g_shots.last().line, sizeof(g_shots.last().line), script_str(l, 2));
        } else if (str_eq(op, "fx") && g_shots.count > 0) {
            str_copy(g_shots.last().fx, sizeof(g_shots.last().fx), script_str(l, 1));
        }
    }
    data.free_mem();
    log_info("Loaded %d cutscenes (%d shots)", g_scenes.count, g_shots.count);
    return true;
}

static vec3 resolve(vec3 p, bool rel) {
    if (!rel) return p;
    const Actor& a = g.actors[PLAYER];
    // Player relative offsets rotate with the player's facing
    vec3 f = dir_from_yaw(a.yaw), r(-cosf(a.yaw), 0, sinf(a.yaw));
    return a.pos + r * p.x + vec3(0, p.y, 0) + f * p.z;
}

static void start_shot() {
    const Cutscene& c = g_scenes[g_active];
    if (g_shot >= c.count) return;
    const Shot& s = g_shots[c.first + g_shot];
    g_lineShown = false;
    if (str_eq(s.fx, "flash")) g.whiteFlash = 1.0f;
    else if (str_eq(s.fx, "shake")) camera_shake(0.8f);
    else if (str_eq(s.fx, "shard")) { g.shardGlow = 3.0f; audio_play(SND_SHARD_PULSE, 1.0f); }
    else if (str_eq(s.fx, "explosion")) { audio_play(SND_EXPLOSION, 1.0f); camera_shake(1.0f); }
    else if (str_eq(s.fx, "screech")) audio_play(SND_ALIEN_SCREECH, 1.0f);
    else if (str_eq(s.fx, "fade")) { g.fade = 1.0f; g.fadeTarget = 0.0f; }
    else if (str_eq(s.fx, "lantern")) {
        setpieces_lantern_fire();
        flag_set("lantern_fired", 1);
        g.whiteFlash = 1.0f;
        camera_shake(1.2f);
        audio_play(SND_EXPLOSION, 1.0f);
        audio_play(SND_SHARD_PULSE, 1.0f);
    }
    // Hard cut
    const ShotRel& r = g_rel[c.first + g_shot];
    g.cam.pos = resolve(s.from, r.from);
    g.cam.target = resolve(s.lookFrom, r.lf);
    g.cam.fov = s.fov;
}

void cutscene_start(const char* id) {
    for (int i = 0; i < g_scenes.count; i++) {
        if (!str_eq(g_scenes[i].id, id)) continue;
        g_active = i;
        g_shot = 0;
        g_t = 0;
        g.cutscene = true;
        start_shot();
        return;
    }
    log_error("Cutscene '%s' not found", id);
}

bool cutscene_active() { return g_active >= 0; }

void cutscene_skip() {
    if (g_active < 0) return;
    g_active = -1;
    g.cutscene = false;
    camera_release();
    dialogue_clear();
}

void cutscene_update(float dt) {
    if (g_active < 0) return;
    const Cutscene& c = g_scenes[g_active];
    if (g.ctl.pressed[ACT_PAUSE] || g.ctl.pressed[ACT_JUMP] || g.ctl.menuAccept || (g.touch && g.ctl.pointerClick)) {
        cutscene_skip();
        return;
    }
    if (g_shot >= c.count) { cutscene_skip(); return; }
    const Shot& s = g_shots[c.first + g_shot];
    const ShotRel& r = g_rel[c.first + g_shot];
    g_t += dt;
    float u = saturate(g_t / fmaxf(s.duration, 0.01f));
    float e = u * u * (3 - 2 * u);
    vec3 pos = lerp(resolve(s.from, r.from), resolve(s.to, r.to), e);
    vec3 look = lerp(resolve(s.lookFrom, r.lf), resolve(s.lookTo, r.lt), e);
    g.cam.cinematic = true;
    g.cam.cinePos = pos;
    g.cam.cineTarget = look;
    g.cam.cineFov = s.fov;
    g.cam.pos = pos;
    g.cam.target = look;
    g.cam.fov = s.fov;
    if (!g_lineShown && s.line[0]) {
        g_lineShown = true;
        dialogue_say(s.speaker, s.line, s.duration);
    }
    if (g_t >= s.duration) {
        g_shot++;
        g_t = 0;
        if (g_shot >= c.count) cutscene_skip();
        else start_shot();
    }
}
