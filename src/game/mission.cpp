// FIGHTO - data-driven mission system. Missions are text scripts in
// assets/missions/*.mis interpreted by this runtime (see docs/MISSION_SCRIPTING.md).
#include "game.h"
#include "script.h"
#include "../audio/audio.h"

#define MAX_MISSIONS 32
#define MAX_CMDS 220

struct MissionDef {
    int id;
    char key[24];
    char title[64];
    char desc[220];
    char startLm[24];
    float startDx, startDz;
    int requires;
    int requiresPhase;
    int reward;
    int phaseAfter;
    bool repeatable;
    bool side;
    bool hidden;
    ScriptLine* cmds;
    int cmdCount;
};

struct MissionRun {
    int def;
    int pc;
    int checkpointPc;
    vec3 cpPos;
    float cpYaw;
    int cpVehicleType;
    bool cpInVehicle;
    float stepTime;
    bool stepInit;
    float timer;
    bool timerActive;
    char protect[4][16];
    int protectCount;
    char objective[160];
    vec3 marker;
    bool hasMarker;
    bool choiceActive;
    char choicePrompt[128];
    char choiceOpt[2][80];
    char choiceLabel[2][32];
    float holdProgress;
    bool fastForward;
    int interactId;
    bool interacted;
    int rewardOverride;
    int lastReward;
    char lastTitle[64];
    char failReason[96];
    int choiceCount;
};

static MissionDef g_defs[MAX_MISSIONS];
static int g_defCount;
static MissionRun R;
static int g_cur = -1;

int mission_count() { return g_defCount; }
const char* mission_name(int i) { return g_defs[i].title; }
const char* mission_desc(int i) { return g_defs[i].desc; }
bool mission_active() { return g_cur >= 0; }
int mission_current_id() { return g_cur >= 0 ? g_defs[g_cur].id : -1; }
const char* mission_title() { return g_cur >= 0 ? g_defs[g_cur].title : ""; }
const char* mission_objective() { return g_cur >= 0 ? R.objective : ""; }
bool mission_choice_active() { return g_cur >= 0 && R.choiceActive; }
float mission_timer() { return (g_cur >= 0 && R.timerActive) ? R.timer : -1.0f; }
const char* mission_fail_reason() { return R.failReason; }
int mission_fail_reason_index() { return 0; }
int mission_reward_last() { return R.lastReward; }
const char* mission_last_title() { return R.lastTitle; }
const char* mission_choice_prompt() { return R.choicePrompt; }
const char* mission_choice_option(int i) { return R.choiceOpt[i]; }
float mission_hold_progress() { return R.holdProgress; }

bool mission_marker(vec3* pos) {
    if (g_cur < 0 || !R.hasMarker) return false;
    *pos = R.marker;
    return true;
}

static bool mission_done(int i) { return (g.pd.missionsDone >> (g_defs[i].id & 31)) & 1u; }

bool mission_available(int i) {
    const MissionDef& d = g_defs[i];
    if (d.hidden) return false;
    if (!d.repeatable && mission_done(i)) return false;
    if (d.requires > 0) {
        bool found = false;
        for (int k = 0; k < g_defCount; k++)
            if (g_defs[k].id == d.requires) found = mission_done(k);
        if (!found) return false;
    }
    if (g.pd.phase < d.requiresPhase) return false;
    return true;
}

static int landmark_by_name(const char* n) {
    static const char* names[LM_COUNT] = {
        "home", "diner", "laundromat", "pawn", "safehouse", "bus_stop", "police", "hospital", "plaza", "meridian",
        "fight_pit", "chop_shop", "warehouse", "substation", "nexus", "drain_hatch", "mansion", "car_lot",
        "aegis_lobby", "aegis_briefing", "aegis_lab", "aegis_range", "aegis_archive", "aegis_reactor", "nest",
    };
    for (int i = 0; i < LM_COUNT; i++) if (str_eq(n, names[i])) return i;
    return -1;
}

vec3 mission_start_pos(int i) {
    int lm = landmark_by_name(g_defs[i].startLm);
    vec3 p = lm >= 0 ? landmark_pos(lm) : vec3(0, 0, 0);
    return p + vec3(g_defs[i].startDx, 0, g_defs[i].startDz);
}

// Parses a position at argv[i]: "@landmark dx dz", "P dx dz" (player relative) or "x y z". Returns tokens consumed.
static int parse_pos(const ScriptLine& l, int i, vec3* out) {
    const char* a = script_str(l, i);
    if (a[0] == '@') {
        int lm = landmark_by_name(a + 1);
        vec3 b = lm >= 0 ? landmark_pos(lm) : vec3(0, 0, 0);
        if (lm < 0) log_error("Mission script line %d: unknown landmark %s", l.lineNo, a);
        *out = b + vec3(script_float(l, i + 1), 0, script_float(l, i + 2));
        if (out->y > -10.0f) out->y = world_ground_height(*out + vec3(0, 2.0f, 0), 0.2f, 3.0f);
        else out->y = world_ground_height(*out + vec3(0, 1.5f, 0), 0.2f, 2.5f);
        return 3;
    }
    if (str_eq(a, "P")) {
        *out = g.actors[PLAYER].pos + vec3(script_float(l, i + 1), 0, script_float(l, i + 2));
        return 3;
    }
    *out = vec3(script_float(l, i), script_float(l, i + 1), script_float(l, i + 2));
    return 3;
}

static int kind_by_name(const char* n) {
    static const char* names[AK_COUNT] = {"player", "civilian", "thug", "gunman", "fighter", "police", "aegis", "operative", "stalker", "brute", "tendril", "overseer", "drone", "story"};
    for (int i = 0; i < AK_COUNT; i++) if (str_eq(n, names[i])) return i;
    return AK_CIVILIAN;
}
static int weapon_by_name(const char* n) {
    static const char* names[W_COUNT] = {"fists", "pistol", "shotgun", "smg", "rifle", "magnum", "arc", "lance", "shard", "claws", "drone"};
    for (int i = 0; i < W_COUNT; i++) if (str_eq(n, names[i])) return i;
    return -1;
}
static int faction_by_name(const char* n) {
    static const char* names[FAC_COUNT] = {"player", "civilian", "police", "kings", "varga", "aegis", "operatives", "alien", "fighter"};
    for (int i = 0; i < FAC_COUNT; i++) if (str_eq(n, names[i])) return i;
    return -1;
}
static int vtype_by_name(const char* n) {
    static const char* names[VT_COUNT] = {"sedan", "muscle", "suv", "sports", "van", "police", "aegis", "taxi"};
    for (int i = 0; i < VT_COUNT; i++) if (str_eq(n, names[i])) return i;
    return VT_SEDAN;
}

static void apply_mode(Actor& a, const char* mode) {
    if (!mode || !mode[0]) return;
    if (str_eq(mode, "hold")) a.aiMode = AI_HOLD;
    else if (str_eq(mode, "stand")) a.aiMode = AI_STAND, a.aiTimer = 1e9f;
    else if (str_eq(mode, "guard")) a.aiMode = AI_GUARD;
    else if (str_eq(mode, "patrol")) { a.aiMode = AI_PATROL; a.goal = a.pos; }
    else if (str_eq(mode, "wander")) a.aiMode = AI_WANDER;
    else if (str_eq(mode, "follow")) a.aiMode = AI_FOLLOW;
    else if (str_eq(mode, "talk")) a.aiMode = AI_TALK;
    else if (str_eq(mode, "spectate")) { a.aiMode = AI_SPECTATE; a.goal = landmark_pos(LM_FIGHT_PIT) + vec3(0, 0, 25.0f); }
    else if (str_eq(mode, "hostile") || str_eq(mode, "combat")) { a.hostileToPlayer = true; ai_make_hostile((int)(&a - g.actors), PLAYER); }
    else if (str_eq(mode, "cower")) { a.aiMode = AI_COWER; a.aiTimer = 1e9f; }
}

static void each_tag(const char* tag, void (*fn)(int, Actor&, const ScriptLine&), const ScriptLine& l) {
    for (int i = 1; i < MAX_ACTORS; i++)
        if (g.actors[i].active && str_eq(g.actors[i].tag, tag)) fn(i, g.actors[i], l);
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
static bool load_mission_file(const char* file) {
    char path[128];
    fmt_str(path, sizeof(path), "missions/%s", file);
    Array<u8> data = {};
    if (!plat_read_asset(path, &data)) { log_error("Missing mission file %s", path); return false; }
    if (g_defCount >= MAX_MISSIONS) { data.free_mem(); return false; }
    MissionDef& d = g_defs[g_defCount];
    memset(&d, 0, sizeof(d));
    d.cmds = (ScriptLine*)calloc(MAX_CMDS, sizeof(ScriptLine));
    const char* cur = (const char*)data.data;
    ScriptLine l;
    int lineNo = 0;
    while (script_next_line(&cur, &l, &lineNo)) {
        const char* op = l.argv[0];
        if (str_eq(op, "mission")) d.id = script_int(l, 1);
        else if (str_eq(op, "key")) str_copy(d.key, sizeof(d.key), script_str(l, 1));
        else if (str_eq(op, "title")) str_copy(d.title, sizeof(d.title), script_str(l, 1));
        else if (str_eq(op, "desc")) str_copy(d.desc, sizeof(d.desc), script_str(l, 1));
        else if (str_eq(op, "start")) { str_copy(d.startLm, sizeof(d.startLm), script_str(l, 1)); d.startDx = script_float(l, 2); d.startDz = script_float(l, 3); }
        else if (str_eq(op, "requires")) d.requires = script_int(l, 1);
        else if (str_eq(op, "requires_phase")) d.requiresPhase = script_int(l, 1);
        else if (str_eq(op, "reward")) d.reward = script_int(l, 1);
        else if (str_eq(op, "phase_after")) d.phaseAfter = script_int(l, 1);
        else if (str_eq(op, "repeatable")) d.repeatable = true;
        else if (str_eq(op, "side")) d.side = true;
        else if (str_eq(op, "hidden")) d.hidden = true;
        else {
            if (d.cmdCount < MAX_CMDS) {
                // Re-point argv into the copied buffer
                ScriptLine& c = d.cmds[d.cmdCount++];
                memcpy(&c, &l, sizeof(l));
                for (int a = 0; a < l.argc; a++) c.argv[a] = c.buf + (l.argv[a] - l.buf);
            } else {
                log_error("Mission %s: too many commands", file);
            }
        }
    }
    data.free_mem();
    g_defCount++;
    return true;
}

void missions_init() {
    for (int i = 0; i < g_defCount; i++) free(g_defs[i].cmds);
    g_defCount = 0;
    g_cur = -1;
    memset(&R, 0, sizeof(R));
    Array<u8> list = {};
    if (!plat_read_asset("missions/missions.txt", &list)) { log_error("missions.txt missing"); return; }
    const char* cur = (const char*)list.data;
    ScriptLine l;
    int ln = 0;
    while (script_next_line(&cur, &l, &ln)) load_mission_file(l.argv[0]);
    list.free_mem();
    log_info("Loaded %d missions", g_defCount);
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
static int find_label(const MissionDef& d, const char* label) {
    for (int i = 0; i < d.cmdCount; i++)
        if (str_eq(d.cmds[i].argv[0], "label") && str_eq(script_str(d.cmds[i], 1), label)) return i;
    log_error("Mission %s: label %s not found", d.key, label);
    return d.cmdCount;
}

static void set_objective(const char* t) {
    if (!t || !t[0]) return;
    if (!str_eq(R.objective, t)) {
        str_copy(R.objective, sizeof(R.objective), t);
        if (!R.fastForward) audio_play(SND_NOTIFY, 0.5f);
    }
}

static void cleanup_tags(bool despawn) {
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!a.active || !a.tag[0]) continue;
        if (despawn) actor_despawn(i);
        else { a.tag[0] = 0; a.persistent = false; a.invulnerable = false; if (a.aiMode == AI_HOLD || a.aiMode == AI_FOLLOW) a.aiMode = a.kind == AK_CIVILIAN ? AI_WANDER : AI_STAND; }
    }
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (!v.active || !v.tag[0]) continue;
        if (despawn && v.driver != PLAYER) vehicle_despawn(i);
        else { v.tag[0] = 0; v.persistent = false; }
    }
    for (int i = 0; i < MAX_PICKUPS; i++)
        if (g.pickups[i].active && g.pickups[i].tag[0] && g.pickups[i].type == PK_ITEM) g.pickups[i].active = false;
}

void mission_start(int i) {
    if (i < 0 || i >= g_defCount) return;
    g_cur = i;
    memset(&R, 0, sizeof(R));
    R.def = i;
    R.pc = 0;
    R.checkpointPc = 0;
    R.cpPos = g.actors[PLAYER].pos;
    R.cpYaw = g.actors[PLAYER].yaw;
    R.rewardOverride = -1;
    g.inMission = true;
    str_copy(R.objective, sizeof(R.objective), g_defs[i].title);
    audio_play(SND_MISSION_START, 0.9f);
    char buf[128];
    fmt_str(buf, sizeof(buf), "%s", g_defs[i].title);
    notify(buf, vec4(1.0f, 0.8f, 0.3f, 1));
    log_info("Mission started: %s", g_defs[i].title);
}

static void mission_complete() {
    MissionDef& d = g_defs[g_cur];
    int reward = R.rewardOverride >= 0 ? R.rewardOverride : d.reward;
    add_money(reward);
    if (!d.repeatable) g.pd.missionsDone |= 1u << (d.id & 31);
    if (d.phaseAfter > g.pd.phase) game_set_phase(d.phaseAfter);
    R.lastReward = reward;
    str_copy(R.lastTitle, sizeof(R.lastTitle), d.title);
    cleanup_tags(false);
    g_cur = -1;
    g.inMission = false;
    g.timeScale = 1.0f;
    dialogue_clear();
    audio_play(SND_MISSION_COMPLETE, 1.0f);
    log_info("Mission complete: %s (+$%d)", d.title, reward);
    // Autosave after every story mission (never shows success unless the write succeeded)
    char status[96];
    if (save_game(0, status, sizeof(status))) str_copy(g.saveStatus, sizeof(g.saveStatus), "Autosaved");
    else str_copy(g.saveStatus, sizeof(g.saveStatus), status);
    g.saveStatusTime = 3.0f;
    game_set_screen(SCR_MISSION_COMPLETE);
}

static void mission_fail(const char* reason) {
    if (g_cur < 0) return;
    str_copy(R.failReason, sizeof(R.failReason), reason);
    str_copy(R.lastTitle, sizeof(R.lastTitle), g_defs[g_cur].title);
    audio_play(SND_MISSION_FAIL, 1.0f);
    log_info("Mission failed: %s (%s)", g_defs[g_cur].title, reason);
    game_set_screen(SCR_MISSION_FAILED);
}

void mission_on_player_death() { if (g_cur >= 0) mission_fail("Damian died"); }

void mission_abort() {
    if (g_cur < 0) return;
    cleanup_tags(true);
    g_cur = -1;
    g.inMission = false;
    dialogue_clear();
    if (cutscene_active()) cutscene_skip();
    camera_release();
}

// Executes one command. Returns true if the command completed (advance pc).
static bool exec(const ScriptLine& l, float dt);

static void fast_forward_to(int target) {
    R.fastForward = true;
    R.pc = 0;
    int guard = 0;
    while (R.pc < target && guard++ < 1000) {
        const ScriptLine& l = g_defs[g_cur].cmds[R.pc];
        const char* op = l.argv[0];
        // Blocking commands are skipped; their side effects are reconstructed
        if (str_eq(op, "kill") || str_eq(op, "defeat")) {
            const char* tag = script_str(l, 1);
            for (int i = 1; i < MAX_ACTORS; i++) if (g.actors[i].active && str_eq(g.actors[i].tag, tag)) actor_despawn(i);
            R.pc++;
            continue;
        }
        if (str_eq(op, "jump") || str_eq(op, "if_flag") || str_eq(op, "choice")) {
            R.stepInit = false;
            exec(l, 0);
            R.pc++;
            continue;
        }
        static const char* skip[] = {"goto", "drive_to", "enter_vehicle", "exit_vehicle", "talk", "interact", "hold", "dialogue", "cutscene",
                                     "wait", "survive", "lose_wanted", "pickup", "fade_out", "fade_in", "say", "kill_count", "end", "goto_actor", "fail"};
        bool sk = false;
        for (int k = 0; k < ARRAY_COUNT(skip); k++) if (str_eq(op, skip[k])) sk = true;
        if (sk) { R.pc++; continue; }
        R.stepInit = false;
        exec(l, 0);
        R.pc++;
    }
    R.fastForward = false;
}

void mission_retry() {
    if (g_cur < 0) return;
    int def = g_cur;
    int cp = R.checkpointPc;
    vec3 cpPos = R.cpPos;
    float cpYaw = R.cpYaw;
    bool inVeh = R.cpInVehicle;
    int vtype = R.cpVehicleType;
    cleanup_tags(true);
    dialogue_clear();
    if (cutscene_active()) cutscene_skip();
    camera_release();
    wanted_clear();
    game_respawn_player(false);
    Actor& p = g.actors[PLAYER];
    p.health = p.maxHealth;
    player_teleport(cpPos, cpYaw);
    g_cur = def;
    memset(&R, 0, sizeof(R));
    R.def = def;
    R.checkpointPc = cp;
    R.cpPos = cpPos;
    R.cpYaw = cpYaw;
    R.cpInVehicle = inVeh;
    R.cpVehicleType = vtype;
    R.rewardOverride = -1;
    g.inMission = true;
    fast_forward_to(cp);
    if (inVeh) {
        int vi = vehicle_spawn(vtype, cpPos, cpYaw, vec3(-1, 0, 0), VAI_NONE);
        if (vi >= 0) player_enter_vehicle(vi);
    }
    game_set_screen(SCR_PLAYING);
}

static bool tag_alive(const char* tag) { return actor_count_tag(tag, true) > 0; }

static void k_hostile(int i, Actor& a, const ScriptLine& l) { a.hostileToPlayer = true; a.nonLethal = a.nonLethal; ai_make_hostile(i, PLAYER); FIGHTO_UNUSED(l); }
static void k_friendly(int i, Actor& a, const ScriptLine& l) { a.hostileToPlayer = false; a.aiMode = AI_FOLLOW; a.faction = FAC_AEGIS; FIGHTO_UNUSED(i); FIGHTO_UNUSED(l); }
static void k_nonlethal(int i, Actor& a, const ScriptLine& l) { a.nonLethal = true; FIGHTO_UNUSED(i); FIGHTO_UNUSED(l); }
static void k_invuln(int i, Actor& a, const ScriptLine& l) { a.invulnerable = script_int(l, 2, 1) != 0; FIGHTO_UNUSED(i); }
static void k_mode(int i, Actor& a, const ScriptLine& l) { apply_mode(a, script_str(l, 2)); FIGHTO_UNUSED(i); }
static void k_despawn(int i, Actor& a, const ScriptLine& l) { actor_despawn(i); FIGHTO_UNUSED(a); FIGHTO_UNUSED(l); }
static void k_face_player(int i, Actor& a, const ScriptLine& l) { actor_face(a, g.actors[PLAYER].pos, 100.0f, 1.0f); FIGHTO_UNUSED(i); FIGHTO_UNUSED(l); }

static int spawn_from(const ScriptLine& l, int kindIdx, int at, const char* tag, vec3 pos, float yaw) {
    int kind = kind_by_name(script_str(l, kindIdx));
    const char* preset = script_str(l, kindIdx + 1);
    int id = actor_spawn_preset(kind, str_eq(preset, "-") ? 0 : preset, pos, yaw * DEG2RAD, g.rng.next());
    if (id < 0) return -1;
    Actor& a = g.actors[id];
    str_copy(a.tag, sizeof(a.tag), tag);
    a.persistent = true;
    a.home = pos;
    int f = faction_by_name(script_str(l, at));
    if (f >= 0) a.faction = (u8)f;
    int w = weapon_by_name(script_str(l, at + 1));
    if (w >= 0) { a.weapon = w; a.clipAmmo = g_weapons[w].clipSize; }
    apply_mode(a, script_str(l, at + 2));
    if (a.kind == AK_DRONE) a.pos.y += 6.0f;
    return id;
}

static bool exec(const ScriptLine& l, float dt) {
    const char* op = l.argv[0];
    MissionDef& d = g_defs[g_cur];
    Actor& p = g.actors[PLAYER];
    bool first = false;
    if (!R.stepInit) { R.stepInit = true; first = true; R.stepTime = 0; R.holdProgress = 0; R.interacted = false; }
    R.stepTime += dt;

    if (str_eq(op, "objective")) { set_objective(script_str(l, 1)); return true; }
    if (str_eq(op, "label")) return true;
    if (str_eq(op, "jump")) { R.pc = find_label(d, script_str(l, 1)) - 1; return true; }
    if (str_eq(op, "flag")) { flag_set(script_str(l, 1), script_int(l, 2)); return true; }
    if (str_eq(op, "if_flag")) {
        if (flag_get(script_str(l, 1)) == script_int(l, 2)) R.pc = find_label(d, script_str(l, 3)) - 1;
        return true;
    }
    if (str_eq(op, "checkpoint")) {
        if (!R.fastForward) {
            R.checkpointPc = R.pc + 1;
            R.cpPos = p.state == AS_VEHICLE && p.vehicle >= 0 ? g.vehicles[p.vehicle].pos : p.pos;
            R.cpYaw = p.yaw;
            R.cpInVehicle = p.state == AS_VEHICLE;
            R.cpVehicleType = p.vehicle >= 0 ? g.vehicles[p.vehicle].type : VT_SEDAN;
            notify("Checkpoint", vec4(0.7f, 0.9f, 1.0f, 1));
            audio_play(SND_CHECKPOINT, 0.5f);
        }
        return true;
    }
    if (str_eq(op, "spawn")) {
        vec3 pos;
        int n = parse_pos(l, 4, &pos);
        spawn_from(l, 2, 4 + n + 1, script_str(l, 1), pos, script_float(l, 4 + n));
        return true;
    }
    if (str_eq(op, "group")) {
        int count = script_int(l, 4);
        vec3 pos;
        int n = parse_pos(l, 5, &pos);
        float rad = script_float(l, 5 + n);
        for (int k = 0; k < count; k++) {
            float a = TAU_F * k / fmaxf((float)count, 1.0f) + g.rng.range(-0.3f, 0.3f);
            vec3 sp = pos + vec3(sinf(a), 0, cosf(a)) * (rad * g.rng.range(0.4f, 1.0f));
            if (sp.y > -10.0f) sp.y = world_ground_height(sp + vec3(0, 2.0f, 0), 0.2f, 3.0f);
            else sp.y = world_ground_height(sp + vec3(0, 1.5f, 0), 0.2f, 2.5f);
            vec3 to = p.pos - sp;
            spawn_from(l, 2, 5 + n + 1, script_str(l, 1), sp, atan2f(to.x, to.z) / DEG2RAD);
        }
        return true;
    }
    if (str_eq(op, "hostile")) { each_tag(script_str(l, 1), k_hostile, l); return true; }
    if (str_eq(op, "friendly")) { each_tag(script_str(l, 1), k_friendly, l); return true; }
    if (str_eq(op, "nonlethal")) { each_tag(script_str(l, 1), k_nonlethal, l); return true; }
    if (str_eq(op, "invulnerable")) { each_tag(script_str(l, 1), k_invuln, l); return true; }
    if (str_eq(op, "mode")) { each_tag(script_str(l, 1), k_mode, l); return true; }
    if (str_eq(op, "despawn")) { each_tag(script_str(l, 1), k_despawn, l); int v = vehicle_find_tag(script_str(l, 1)); if (v >= 0 && g.vehicles[v].driver != PLAYER) vehicle_despawn(v); return true; }
    if (str_eq(op, "face_player")) { each_tag(script_str(l, 1), k_face_player, l); return true; }
    if (str_eq(op, "goto_ai")) {
        vec3 pos;
        int n = parse_pos(l, 2, &pos);
        float speed = script_float(l, 2 + n, 3.0f);
        for (int i = 1; i < MAX_ACTORS; i++) {
            Actor& a = g.actors[i];
            if (a.active && str_eq(a.tag, script_str(l, 1))) { a.aiMode = AI_GOTO; a.goal = pos; a.scriptTimer = speed; }
        }
        return true;
    }
    if (str_eq(op, "protect")) {
        if (R.protectCount < 4) str_copy(R.protect[R.protectCount++], 16, script_str(l, 1));
        return true;
    }
    if (str_eq(op, "unprotect")) {
        for (int k = 0; k < R.protectCount; k++) if (str_eq(R.protect[k], script_str(l, 1))) { R.protect[k][0] = 0; }
        return true;
    }
    if (str_eq(op, "vehicle")) {
        vec3 pos;
        int n = parse_pos(l, 3, &pos);
        float yaw = script_float(l, 3 + n) * DEG2RAD;
        vec3 col = l.argc > 3 + n + 3 ? vec3(script_float(l, 4 + n), script_float(l, 5 + n), script_float(l, 6 + n)) : vec3(-1, 0, 0);
        int vi = vehicle_spawn(vtype_by_name(script_str(l, 2)), pos, yaw, col, VAI_PARKED);
        if (vi >= 0) { str_copy(g.vehicles[vi].tag, 16, script_str(l, 1)); g.vehicles[vi].persistent = true; }
        return true;
    }
    if (str_eq(op, "lock_vehicle")) { int v = vehicle_find_tag(script_str(l, 1)); if (v >= 0) g.vehicles[v].locked = script_int(l, 2) != 0; return true; }
    if (str_eq(op, "give_weapon")) { int w = weapon_by_name(script_str(l, 1)); if (w > 0) player_give_weapon(w, script_int(l, 2, 30)); return true; }
    if (str_eq(op, "give_money")) { add_money(script_int(l, 1)); return true; }
    if (str_eq(op, "reward")) { R.rewardOverride = script_int(l, 1); return true; }
    if (str_eq(op, "heal")) { p.health = p.maxHealth; return true; }
    if (str_eq(op, "armor")) { p.armor = fminf(100.0f, p.armor + script_float(l, 1)); return true; }
    if (str_eq(op, "rep")) { int f = faction_by_name(script_str(l, 1)); if (f >= 0) rep_add(f, script_int(l, 2)); return true; }
    if (str_eq(op, "wanted")) { wanted_set(script_int(l, 1)); return true; }
    if (str_eq(op, "clear_wanted")) { wanted_clear(); return true; }
    if (str_eq(op, "phase")) { game_set_phase(script_int(l, 1)); return true; }
    if (str_eq(op, "power")) { g.power = script_int(l, 1) != 0; world_set_power(g.power); return true; }
    if (str_eq(op, "rain")) { g.rainTarget = script_float(l, 1); return true; }
    if (str_eq(op, "music")) {
        const char* m = script_str(l, 1);
        int mood = str_eq(m, "calm") ? MUSIC_CALM : str_eq(m, "tension") ? MUSIC_TENSION : str_eq(m, "combat") ? MUSIC_COMBAT : str_eq(m, "alien") ? MUSIC_ALIEN : str_eq(m, "chase") ? MUSIC_CHASE : MUSIC_NONE;
        audio_set_music(mood, 1.0f);
        return true;
    }
    if (str_eq(op, "notify")) { if (!R.fastForward) notify(script_str(l, 1), vec4(1, 0.9f, 0.6f, 1)); return true; }
    if (str_eq(op, "teleport")) {
        vec3 pos;
        int n = parse_pos(l, 1, &pos);
        if (p.state == AS_VEHICLE) player_exit_vehicle();
        player_teleport(pos, script_float(l, 1 + n) * DEG2RAD);
        return true;
    }
    if (str_eq(op, "evidence")) {
        vec3 pos;
        parse_pos(l, 2, &pos);
        int id = script_int(l, 1);
        if (id >= 0 && id < MAX_EVIDENCE && !g.pd.evidence[id]) {
            int pk = pickup_spawn(PK_EVIDENCE, pos, 0, 0, "");
            if (pk >= 0) g.pickups[pk].evidenceId = id;
        }
        return true;
    }
    if (str_eq(op, "fx_explosion")) { vec3 pos; int n = parse_pos(l, 1, &pos); if (!R.fastForward) explosion(pos + vec3(0, 0.5f, 0), script_float(l, 1 + n, 5.0f), script_float(l, 2 + n, 0.0f), -1, script_int(l, 3 + n) != 0); return true; }
    if (str_eq(op, "fx_flash")) { if (!R.fastForward) g.whiteFlash = 1.0f; return true; }
    if (str_eq(op, "fx_shake")) { if (!R.fastForward) camera_shake(script_float(l, 1, 0.5f)); return true; }
    if (str_eq(op, "shard_glow")) { if (!R.fastForward) { g.shardGlow = script_float(l, 1, 3.0f); audio_play(SND_SHARD_PULSE, 1.0f); } return true; }
    if (str_eq(op, "sound")) {
        const char* s = script_str(l, 1);
        if (!R.fastForward) {
            if (str_eq(s, "screech")) audio_play(SND_ALIEN_SCREECH, 1.0f);
            else if (str_eq(s, "explosion")) audio_play(SND_EXPLOSION, 1.0f);
            else if (str_eq(s, "cheer")) audio_play(SND_CROWD_CHEER, 1.0f);
            else if (str_eq(s, "elevator")) audio_play(SND_ELEVATOR, 1.0f);
            else if (str_eq(s, "thunder")) audio_play(SND_THUNDER, 1.0f);
        }
        return true;
    }
    if (str_eq(op, "unlock_safehouse")) { g.pd.safehouseUnlocked = 1; return true; }
    if (str_eq(op, "pit_win")) { g.pd.pitWins++; g.pd.fightsWon++; return true; }
    if (str_eq(op, "say")) {
        if (!R.fastForward) dialogue_say(script_str(l, 1), script_str(l, 2), 0);
        return true;
    }
    if (str_eq(op, "timer")) { R.timer = script_float(l, 1); R.timerActive = true; return true; }
    if (str_eq(op, "timer_stop")) { R.timerActive = false; return true; }
    if (str_eq(op, "fail")) { mission_fail(script_str(l, 1, "Mission failed")); return false; }
    if (str_eq(op, "end")) { mission_complete(); return false; }

    // ---- Blocking commands ----
    if (str_eq(op, "wait")) return R.stepTime >= script_float(l, 1);
    if (str_eq(op, "dialogue")) {
        if (first) dialogue_start(script_str(l, 1));
        return !dialogue_active();
    }
    if (str_eq(op, "cutscene")) {
        if (first) cutscene_start(script_str(l, 1));
        return !cutscene_active();
    }
    if (str_eq(op, "fade_out")) { g.fadeTarget = 1.0f; return R.stepTime > 0.9f; }
    if (str_eq(op, "fade_in")) { g.fadeTarget = 0.0f; return R.stepTime > 0.6f; }
    if (str_eq(op, "goto") || str_eq(op, "drive_to")) {
        vec3 pos;
        int n = parse_pos(l, 1, &pos);
        float r = script_float(l, 1 + n, 4.0f);
        set_objective(script_str(l, 2 + n));
        R.marker = pos;
        R.hasMarker = true;
        bool needCar = str_eq(op, "drive_to");
        vec3 pp = p.state == AS_VEHICLE && p.vehicle >= 0 ? g.vehicles[p.vehicle].pos : p.pos;
        if (needCar && p.state != AS_VEHICLE) return false;
        if (dist_xz(pp, pos) < r && fabsf(pp.y - pos.y) < 6.0f) { R.hasMarker = false; return true; }
        return false;
    }
    if (str_eq(op, "goto_actor")) {
        int a = actor_find_tag(script_str(l, 1));
        if (a < 0) return true;
        set_objective(script_str(l, 3));
        R.marker = g.actors[a].pos;
        R.hasMarker = true;
        if (length(p.pos - g.actors[a].pos) < script_float(l, 2, 3.0f)) { R.hasMarker = false; return true; }
        return false;
    }
    if (str_eq(op, "enter_vehicle")) {
        int v = vehicle_find_tag(script_str(l, 1));
        if (v < 0) return true;
        set_objective(script_str(l, 2));
        R.marker = g.vehicles[v].pos;
        R.hasMarker = true;
        if (p.state == AS_VEHICLE && p.vehicle == v) { R.hasMarker = false; return true; }
        if (!g.vehicles[v].active || g.vehicles[v].destroyed) { mission_fail("The vehicle was destroyed"); return false; }
        return false;
    }
    if (str_eq(op, "exit_vehicle")) { set_objective(script_str(l, 1)); return p.state != AS_VEHICLE; }
    if (str_eq(op, "talk")) {
        int a = actor_find_tag(script_str(l, 1));
        if (a < 0) return true;
        Actor& t = g.actors[a];
        set_objective(script_str(l, 3, "Talk"));
        R.marker = t.pos;
        R.hasMarker = true;
        char prompt[80];
        fmt_str(prompt, sizeof(prompt), "%s", script_str(l, 2, "Talk"));
        interact_add(t.pos + vec3(0, 1.0f, 0), 2.6f, IK_TALK, 0, prompt);
        if (R.interacted) {
            R.hasMarker = false;
            actor_face(t, p.pos, 100.0f, 1.0f);
            return true;
        }
        return false;
    }
    if (str_eq(op, "interact")) {
        vec3 pos;
        int n = parse_pos(l, 1, &pos);
        float r = script_float(l, 1 + n, 2.0f);
        set_objective(script_str(l, 3 + n, script_str(l, 2 + n)));
        R.marker = pos;
        R.hasMarker = true;
        interact_add(pos + vec3(0, 1.0f, 0), r, IK_MISSION, 0, script_str(l, 2 + n, "Use"));
        if (R.interacted) { R.hasMarker = false; return true; }
        return false;
    }
    if (str_eq(op, "hold")) {
        vec3 pos;
        int n = parse_pos(l, 1, &pos);
        float r = script_float(l, 1 + n, 2.0f);
        float secs = script_float(l, 2 + n, 5.0f);
        set_objective(script_str(l, 3 + n, "Hold position"));
        R.marker = pos;
        R.hasMarker = true;
        bool inside = dist_xz(p.pos, pos) < r && fabsf(p.pos.y - pos.y) < 3.0f && p.state != AS_VEHICLE;
        if (inside) {
            interact_add(pos + vec3(0, 1.0f, 0), r, IK_MISSION, 1, "Hold [E]");
            if (g.ctl.down[ACT_INTERACT]) R.holdProgress += dt / secs;
        } else {
            R.holdProgress = fmaxf(0.0f, R.holdProgress - dt * 0.05f);
        }
        if (R.holdProgress >= 1.0f) { R.hasMarker = false; R.holdProgress = 0; return true; }
        return false;
    }
    if (str_eq(op, "pickup")) {
        vec3 pos;
        int n = parse_pos(l, 2, &pos);
        if (first) pickup_spawn(PK_ITEM, pos, 0, 0, script_str(l, 1));
        set_objective(script_str(l, 2 + n, "Take the item"));
        R.marker = pos;
        R.hasMarker = true;
        interact_add(pos + vec3(0, 0.8f, 0), 2.0f, IK_MISSION, 2, "Take [E]");
        if (R.interacted) {
            for (int i = 0; i < MAX_PICKUPS; i++) if (g.pickups[i].active && str_eq(g.pickups[i].tag, script_str(l, 1))) g.pickups[i].active = false;
            audio_play(SND_PICKUP, 0.9f);
            R.hasMarker = false;
            return true;
        }
        return false;
    }
    if (str_eq(op, "kill") || str_eq(op, "defeat")) {
        const char* tag = script_str(l, 1);
        set_objective(script_str(l, 2));
        // Marker on the nearest remaining target
        float best = 1e9f;
        R.hasMarker = false;
        for (int i = 1; i < MAX_ACTORS; i++) {
            Actor& a = g.actors[i];
            if (!a.active || !str_eq(a.tag, tag) || !actor_alive(a) || a.knockedOut > 0) continue;
            float dd = length(a.pos - p.pos);
            if (dd < best) { best = dd; R.marker = a.pos + vec3(0, a.height + 0.5f, 0); R.hasMarker = best > 12.0f; }
        }
        return !tag_alive(tag);
    }
    if (str_eq(op, "kill_count")) {
        const char* tag = script_str(l, 1);
        int need = script_int(l, 2);
        set_objective(script_str(l, 3));
        int dead = 0;
        for (int i = 1; i < MAX_ACTORS; i++) {
            Actor& a = g.actors[i];
            if (a.active && str_eq(a.tag, tag) && (!actor_alive(a) || a.knockedOut > 0)) dead++;
        }
        return dead >= need;
    }
    if (str_eq(op, "survive")) {
        set_objective(script_str(l, 2));
        R.timer = script_float(l, 1) - R.stepTime;
        R.timerActive = false;
        return R.stepTime >= script_float(l, 1);
    }
    if (str_eq(op, "lose_wanted")) { set_objective(script_str(l, 1, "Lose the police")); return g.wanted.level == 0; }
    if (str_eq(op, "choice")) {
        // choice "prompt" "opt1" label1 "opt2" label2
        char fname[48];
        fmt_str(fname, sizeof(fname), "ch_%s_%d", d.key, R.pc);
        if (R.fastForward) {
            int v = flag_get(fname);
            R.pc = find_label(d, script_str(l, v == 2 ? 5 : 3)) - 1;
            return true;
        }
        if (!R.choiceActive) {
            R.choiceActive = true;
            str_copy(R.choicePrompt, sizeof(R.choicePrompt), script_str(l, 1));
            str_copy(R.choiceOpt[0], sizeof(R.choiceOpt[0]), script_str(l, 2));
            str_copy(R.choiceOpt[1], sizeof(R.choiceOpt[1]), script_str(l, 4));
            str_copy(R.choiceLabel[0], sizeof(R.choiceLabel[0]), script_str(l, 3));
            str_copy(R.choiceLabel[1], sizeof(R.choiceLabel[1]), script_str(l, 5));
        }
        int pick = 0;
        if (g.ctl.pressed[ACT_CHOICE1]) pick = 1;
        if (g.ctl.pressed[ACT_CHOICE2]) pick = 2;
        if (R.choiceCount > 0) { pick = R.choiceCount; R.choiceCount = 0; }
        if (pick) {
            R.choiceActive = false;
            flag_set(fname, pick);
            audio_play(SND_UI_CLICK, 0.8f);
            R.pc = find_label(d, R.choiceLabel[pick - 1]) - 1;
            return true;
        }
        return false;
    }
    log_error("Mission %s line %d: unknown command '%s'", d.key, l.lineNo, op);
    return true;
}

void mission_choose(int option) { if (g_cur >= 0 && R.choiceActive) R.choiceCount = option; }

void mission_interact(int which) {
    FIGHTO_UNUSED(which);
    if (g_cur >= 0) R.interacted = true;
}

void mission_debug_complete_step() {
    if (g_cur < 0) return;
    const ScriptLine& l = g_defs[g_cur].cmds[R.pc];
    const char* op = l.argv[0];
    if (str_eq(op, "kill") || str_eq(op, "defeat") || str_eq(op, "kill_count")) {
        for (int i = 1; i < MAX_ACTORS; i++) {
            Actor& a = g.actors[i];
            if (a.active && str_eq(a.tag, script_str(l, 1)) && actor_alive(a)) combat_damage(i, PLAYER, 99999.0f, actor_chest(a), dir_from_yaw(g.actors[PLAYER].yaw), DMG_MELEE, B_CHEST);
        }
    } else if (str_eq(op, "goto") || str_eq(op, "drive_to") || str_eq(op, "hold") || str_eq(op, "interact") || str_eq(op, "goto_actor")) {
        if (str_eq(op, "drive_to") && g.actors[PLAYER].state != AS_VEHICLE) {
            int vi = vehicle_spawn(VT_SEDAN, g.actors[PLAYER].pos + vec3(3, 0, 0), 0, vec3(-1, 0, 0), VAI_NONE);
            if (vi >= 0) player_enter_vehicle(vi);
        }
        vec3 pos;
        if (str_eq(op, "goto_actor")) { int a = actor_find_tag(script_str(l, 1)); if (a >= 0) pos = g.actors[a].pos; else pos = g.actors[PLAYER].pos; }
        else parse_pos(l, 1, &pos);
        if (g.actors[PLAYER].state == AS_VEHICLE && g.actors[PLAYER].vehicle >= 0) { g.vehicles[g.actors[PLAYER].vehicle].pos = pos + vec3(1.0f, 0, 0); g.vehicles[g.actors[PLAYER].vehicle].vel = vec3(0, 0, 0); }
        else player_teleport(pos + vec3(0.5f, 0, 0.5f), g.actors[PLAYER].yaw);
        R.interacted = true;
        R.holdProgress = 1.0f;
    } else if (str_eq(op, "talk") || str_eq(op, "pickup")) {
        R.interacted = true;
    } else if (str_eq(op, "enter_vehicle")) {
        int v = vehicle_find_tag(script_str(l, 1));
        if (v >= 0) { if (g.actors[PLAYER].state == AS_VEHICLE) player_exit_vehicle(); player_enter_vehicle(v); }
    } else if (str_eq(op, "exit_vehicle")) {
        player_exit_vehicle();
    } else if (str_eq(op, "lose_wanted")) {
        wanted_clear();
    } else if (str_eq(op, "dialogue")) {
        dialogue_clear();
    } else if (str_eq(op, "cutscene")) {
        cutscene_skip();
    } else if (str_eq(op, "choice")) {
        R.choiceCount = 1;
    } else if (str_eq(op, "wait") || str_eq(op, "survive")) {
        R.stepTime = 1e6f;
    }
}

void missions_update(float dt) {
    // Start markers for available missions
    if (g_cur < 0) {
        for (int i = 0; i < g_defCount; i++) {
            if (!mission_available(i)) continue;
            vec3 sp = mission_start_pos(i);
            if (length(sp - g.actors[PLAYER].pos) < 40.0f) {
                char t[80];
                fmt_str(t, sizeof(t), "%s: %s", g_defs[i].side ? "Side job" : "Mission", g_defs[i].title);
                interact_add(sp + vec3(0, 1.0f, 0), 2.6f, IK_START_MISSION, i, t);
            }
        }
        return;
    }
    if (g.screen != SCR_PLAYING) return;
    MissionDef& d = g_defs[g_cur];
    // Fail conditions
    for (int k = 0; k < R.protectCount; k++) {
        if (!R.protect[k][0]) continue;
        int a = actor_find_tag(R.protect[k]);
        if (a < 0 || !actor_alive(g.actors[a])) {
            char buf[96];
            fmt_str(buf, sizeof(buf), "%s was killed", a >= 0 && g.actors[a].name[0] ? g.actors[a].name : "An ally");
            mission_fail(buf);
            return;
        }
    }
    if (R.timerActive) {
        R.timer -= dt;
        if (R.timer <= 0) { mission_fail("Time ran out"); return; }
    }
    // Execute commands until one blocks
    int guard = 0;
    while (g_cur >= 0 && R.pc < d.cmdCount && guard++ < 64) {
        int pcBefore = R.pc;
        bool done = exec(d.cmds[R.pc], guard == 1 ? dt : 0.0f);
        if (g_cur < 0 || g.screen != SCR_PLAYING) return;
        if (!done) break;
        R.pc++;
        R.stepInit = false;
        R.interacted = false;
        FIGHTO_UNUSED(pcBefore);
    }
    if (g_cur >= 0 && R.pc >= d.cmdCount) mission_complete();
}

void missions_render_markers() {
    // Mission start beacons
    if (g_cur < 0) {
        for (int i = 0; i < g_defCount; i++) {
            if (!mission_available(i)) continue;
            vec3 sp = mission_start_pos(i);
            if (length(sp - g.cam.pos) > 250.0f) continue;
            BeamDraw b;
            b.a = sp;
            b.b = sp + vec3(0, 30.0f, 0);
            b.r0 = 0.9f;
            b.r1 = 0.4f;
            b.color = g_defs[i].side ? vec3(0.3f, 0.9f, 0.5f) : vec3(1.0f, 0.75f, 0.2f);
            b.intensity = 1.2f;
            b.alpha = 0.8f;
            renderer_beam(b);
            Light l = {sp + vec3(0, 1.0f, 0), 6.0f, b.color * 3.0f, vec3(0, -1, 0), 0};
            renderer_add_light(l);
        }
        return;
    }
    if (R.hasMarker) {
        BeamDraw b;
        b.a = R.marker;
        b.b = R.marker + vec3(0, 25.0f, 0);
        b.r0 = 0.7f;
        b.r1 = 0.3f;
        b.color = vec3(1.0f, 0.8f, 0.25f);
        b.intensity = 1.0f;
        b.alpha = 0.8f;
        renderer_beam(b);
    }
}
