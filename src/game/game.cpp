// FIGHTO - main game loop: boot, screens, simulation order, atmosphere,
// interactions, respawning and save/load glue.
#include "game.h"
#include "setpieces.h"
#include "autotest.h"
#include "../ui/draw2d.h"
#include "../audio/audio.h"

Game g;

// ---------------------------------------------------------------------------
// Small helpers used across modules
// ---------------------------------------------------------------------------
const char* faction_name(int f) {
    static const char* names[FAC_COUNT] = {"Damian", "Civilians", "Blackridge PD", "Ninth Street Kings", "Varga Syndicate", "A.E.G.I.S.", "Unknown Operatives", "Aliens", "Fight Pit"};
    return (f >= 0 && f < FAC_COUNT) ? names[f] : "?";
}

float rep_get(int f) { return (float)g.pd.rep[f]; }
void rep_add(int f, int delta) { g.pd.rep[f] = clamp_t(g.pd.rep[f] + delta, -100, 100); }

void add_money(int amount) {
    if (amount == 0) return;
    g.pd.money = max_t(0, g.pd.money + amount);
    char buf[64];
    fmt_str(buf, sizeof(buf), amount > 0 ? "+$%d" : "-$%d", amount > 0 ? amount : -amount);
    notify(buf, amount > 0 ? vec4(0.5f, 1.0f, 0.5f, 1) : vec4(1.0f, 0.5f, 0.4f, 1));
    if (amount > 0) audio_play(SND_MONEY, 0.5f);
}

void notify(const char* text, vec4 color) {
    if (g.noteCount >= ARRAY_COUNT(g.notes)) {
        for (int i = 1; i < g.noteCount; i++) g.notes[i - 1] = g.notes[i];
        g.noteCount--;
    }
    Notification& n = g.notes[g.noteCount++];
    str_copy(n.text, sizeof(n.text), text);
    n.time = 4.0f;
    n.color = color;
}

void interact_clear() {
    g.interactCount = 0;
    g.interactBest = -1;
}

void interact_add(vec3 pos, float radius, int kind, int index, const char* text) {
    if (g.interactCount >= MAX_INTERACTABLES) return;
    Interactable& it = g.interactables[g.interactCount++];
    it.pos = pos;
    it.radius = radius;
    it.kind = kind;
    it.index = index;
    str_copy(it.text, sizeof(it.text), text);
}

void game_set_screen(int s) {
    if (s == g.screen) return;
    g.prevScreen = g.screen;
    g.screen = s;
    g.menuIndex = 0;
    g.menuScroll = 0;
    g.rebindAction = -1;
    audio_pause(false);
}

void game_set_phase(int phase) {
    int old = g.pd.phase;
    g.pd.phase = clamp_t(phase, 0, 3);
    g.power = g.pd.phase != 2;
    world_set_phase_visuals(g.pd.phase);
    if (g.pd.phase >= 3) setpieces_invasion(true);
    if (g.pd.phase != old) {
        static const char* names[4] = {"", "A.E.G.I.S. has found you", "BLACKOUT: Blackridge loses power", "INVASION: the sky is burning"};
        if (g.pd.phase > 0) notify(names[g.pd.phase], vec4(1.0f, 0.6f, 0.3f, 1));
    }
}

void game_cleanup_world_entities(bool keepMission) {
    for (int i = 1; i < MAX_ACTORS; i++) {
        if (!g.actors[i].active) continue;
        if (keepMission && g.actors[i].tag[0]) continue;
        actor_despawn(i);
    }
    for (int i = 0; i < MAX_VEHICLES; i++) {
        if (!g.vehicles[i].active || g.vehicles[i].driver == PLAYER) continue;
        if (keepMission && g.vehicles[i].tag[0]) continue;
        g.vehicles[i].active = false;
    }
    for (int i = 0; i < MAX_PROJECTILES; i++) g.projectiles[i].active = false;
}

// ---------------------------------------------------------------------------
// Save / load wrappers
// ---------------------------------------------------------------------------
static void slot_name(int slot, char* out, int cap, bool backup) {
    fmt_str(out, cap, backup ? "save_slot%d.bak" : "save_slot%d.dat", slot);
}

bool save_exists(int slot) {
    char n[64];
    slot_name(slot, n, sizeof(n), false);
    return plat_user_file_exists(n);
}

bool save_game(int slot, char* status, int cap) {
    if (g.inMission && slot != 0) {
        fmt_str(status, cap, "Cannot save during a mission");
        return false;
    }
    if (g.actors[PLAYER].state == AS_DEAD) {
        fmt_str(status, cap, "Cannot save now");
        return false;
    }
    Array<u8> data = {};
    save_serialize(&data);
    char name[64], bak[64];
    slot_name(slot, name, sizeof(name), false);
    slot_name(slot, bak, sizeof(bak), true);
    if (plat_user_file_exists(name)) plat_copy_user_file(name, bak);
    bool ok = plat_write_user_file(name, data.data, (size_t)data.count);
    data.free_mem();
    if (ok) {
        fmt_str(status, cap, "Game saved");
        g.hasSave = true;
    } else {
        fmt_str(status, cap, "SAVE FAILED - check disk space / permissions");
    }
    return ok;
}

bool load_game(int slot, char* status, int cap) {
    char name[64], bak[64], err[96];
    slot_name(slot, name, sizeof(name), false);
    slot_name(slot, bak, sizeof(bak), true);
    Array<u8> data = {};
    bool ok = false;
    bool usedBackup = false;
    if (plat_read_user_file(name, &data)) ok = save_deserialize(data.data, data.count, err, sizeof(err));
    else fmt_str(err, sizeof(err), "No save found");
    if (!ok) {
        data.clear();
        if (plat_read_user_file(bak, &data)) {
            char err2[96];
            ok = save_deserialize(data.data, data.count, err2, sizeof(err2));
            usedBackup = ok;
        }
    }
    data.free_mem();
    if (!ok) { fmt_str(status, cap, "Load failed: %s", err); return false; }
    if (usedBackup) fmt_str(status, cap, "Main save was damaged (%s) - loaded backup", err);
    else fmt_str(status, cap, "Game loaded");
    return true;
}

// ---------------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------------
static void reset_world_state() {
    mission_abort();
    game_cleanup_world_entities(false);
    for (int i = 0; i < MAX_PICKUPS; i++) g.pickups[i].active = false;
    fx_clear();
    wanted_clear();
    dialogue_clear();
    if (cutscene_active()) cutscene_skip();
    camera_release();
    g.lockTarget = -1;
    g.bloodScreen = 0;
    g.damageFlash = 0;
    g.fade = 1.0f;
    g.fadeTarget = 0.0f;
}

static void spawn_collectibles() {
    // Evidence documents and hidden caches placed around the city
    for (int i = 0; i < g_world.evidenceSpots.count && i < MAX_EVIDENCE; i++) {
        if (g.pd.evidence[i]) continue;
        int pk = pickup_spawn(PK_EVIDENCE, g_world.evidenceSpots[i], 0, 0, "");
        if (pk >= 0) g.pickups[pk].evidenceId = i;
    }
    for (int i = 0; i < g_world.cacheSpots.count && i < 32; i++) {
        if (g.pd.cachesFound[i]) continue;
        int pk = pickup_spawn(PK_CACHE, g_world.cacheSpots[i], 0, 200 + (i % 4) * 100, "");
        if (pk >= 0) g.pickups[pk].evidenceId = i;
    }
    // Health and armour around landmarks
    pickup_spawn(PK_HEALTH, landmark_pos(LM_SAFEHOUSE) + vec3(-2, 0, 0), 0, 60, "");
    pickup_spawn(PK_HEALTH, landmark_pos(LM_HOSPITAL) + vec3(3, 0, 0), 0, 100, "");
    pickup_spawn(PK_ARMOR, landmark_pos(LM_POLICE) + vec3(8, 0, -2), 0, 50, "");
}

void game_new_game() {
    reset_world_state();
    player_reset(true);
    g.pd.phase = 0;
    g.power = true;
    world_set_power(true);
    setpieces_invasion(false);
    g.rainTarget = 0.85f;
    spawn_collectibles();
    g.newGame = true;
    game_set_screen(SCR_PLAYING);
    // The story begins immediately with the first mission
    for (int i = 0; i < mission_count(); i++)
        if (mission_available(i) && length(mission_start_pos(i) - g.actors[PLAYER].pos) < 30.0f) { mission_start(i); break; }
}

static void game_continue() {
    reset_world_state();
    player_reset(false);
    char status[128];
    int slot = save_exists(1) ? 1 : 0;
    // Prefer the most recent manual save if it exists
    if (save_exists(0) && save_exists(1)) slot = 1;
    if (!load_game(slot, status, sizeof(status)) && !load_game(slot == 1 ? 0 : 1, status, sizeof(status))) {
        str_copy(g.saveStatus, sizeof(g.saveStatus), status);
        g.saveStatusTime = 5.0f;
        game_new_game();
        return;
    }
    str_copy(g.saveStatus, sizeof(g.saveStatus), status);
    g.saveStatusTime = 4.0f;
    Actor& p = g.actors[PLAYER];
    player_teleport(p.pos, p.yaw);
    game_set_phase(g.pd.phase);
    world_set_power(g.power);
    spawn_collectibles();
    game_set_screen(SCR_PLAYING);
}

void game_respawn_player(bool arrested) {
    Actor& p = g.actors[PLAYER];
    if (p.state == AS_VEHICLE) player_exit_vehicle();
    if (p.ragdoll) ragdoll_free(p);
    if (arrested) {
        g.pd.timesArrested++;
        int fine = g.pd.money / 10;
        g.pd.money -= fine;
        for (int w = 1; w < W_COUNT; w++) g.pd.reserve[w] /= 2;
        if (g.inMission) { mission_on_player_death(); }
        else game_set_screen(SCR_ARRESTED);
        player_teleport(landmark_pos(LM_POLICE) + vec3(0, 0, -6), PI_F);
    } else {
        player_teleport(landmark_pos(LM_HOSPITAL) + vec3(0, 0, 6), 0);
    }
    p.state = AS_LOCOMOTION;
    p.health = p.maxHealth;
    p.clip = -1;
    p.vel = vec3(0, 0, 0);
    p.blood = 0;
    p.bleed = 0;
    p.knockedOut = 0;
    wanted_clear();
    g.bloodScreen = 0;
    g.damageFlash = 0;
    g.lockTarget = -1;
}

// ---------------------------------------------------------------------------
// World interactions
// ---------------------------------------------------------------------------
void interact_execute(const Interactable& it) {
    if (player_interact(it)) return;
    switch (it.kind) {
    case IK_TALK:
    case IK_MISSION:
        mission_interact(it.index);
        break;
    case IK_START_MISSION:
        mission_start(it.index);
        break;
    case IK_SHOP:
        game_set_screen(SCR_SHOP);
        break;
    case IK_ARMORY:
        game_set_screen(SCR_UPGRADES);
        break;
    case IK_SAVE: {
        char st[96];
        player_heal(1000);
        bool ok = save_game(1, st, sizeof(st));
        str_copy(g.saveStatus, sizeof(g.saveStatus), ok ? "Rested & saved at the safehouse" : st);
        g.saveStatusTime = 3.0f;
        g.fade = 1.0f;
        g.fadeTarget = 0.0f;
    } break;
    case IK_ELEVATOR_DOWN:
        audio_play(SND_ELEVATOR, 0.8f);
        g.fade = 1.0f;
        g.fadeTarget = 0.0f;
        player_teleport(landmark_pos(LM_AEGIS_LOBBY) + vec3(0, 0, -3), PI_F);
        break;
    case IK_ELEVATOR_UP:
        audio_play(SND_ELEVATOR, 0.8f);
        g.fade = 1.0f;
        g.fadeTarget = 0.0f;
        player_teleport(landmark_pos(LM_NEXUS) + vec3(0, 0, -3), PI_F);
        break;
    case IK_DRAIN_ENTER:
        g.fade = 1.0f;
        g.fadeTarget = 0.0f;
        player_teleport(vec3(DRAIN_ORIGIN_X, DRAIN_ORIGIN_Y, DRAIN_ORIGIN_Z - 86), 0);
        break;
    case IK_DRAIN_EXIT:
        g.fade = 1.0f;
        g.fadeTarget = 0.0f;
        player_teleport(landmark_pos(LM_DRAIN_HATCH) + vec3(0, 0, -1.5f), PI_F);
        break;
    default:
        break;
    }
}

static void gather_world_interactables() {
    Actor& p = g.actors[PLAYER];
    if (p.state == AS_VEHICLE || p.state == AS_DEAD) return;
    vec3 pp = p.pos;
    auto near = [&](vec3 q, float r) { return length(q - pp) < r + 6.0f; };
    vec3 shop = landmark_pos(LM_PAWN);
    if (near(shop, 3)) interact_add(shop, 2.5f, IK_SHOP, 0, "Shop: Ace Pawn & Arms");
    vec3 bed = landmark_pos(LM_SAFEHOUSE);
    if (near(bed, 2) && (g.pd.safehouseUnlocked || (g.pd.missionsDone & 1u))) interact_add(bed, 2.2f, IK_SAVE, 0, "Rest & save game");
    if (flag_get("aegis_access")) {
        vec3 el = landmark_pos(LM_NEXUS);
        if (near(el, 2)) interact_add(el, 2.5f, IK_ELEVATOR_DOWN, 0, "Elevator to A.E.G.I.S.");
        vec3 up = landmark_pos(LM_AEGIS_LOBBY);
        if (near(up, 2)) interact_add(up, 2.5f, IK_ELEVATOR_UP, 0, "Elevator to the surface");
        vec3 arm = landmark_pos(LM_AEGIS_RANGE) + vec3(-30, 0, 4);
        if (near(arm, 2)) interact_add(arm, 2.5f, IK_ARMORY, 0, "A.E.G.I.S. Armory upgrades");
    }
    vec3 safeGym = landmark_pos(LM_SAFEHOUSE) + vec3(-3.0f, 0, -1.0f);
    if (near(safeGym, 2) && (g.pd.missionsDone & 1u)) interact_add(safeGym, 1.8f, IK_ARMORY, 1, "Train (upgrades)");
    if (flag_get("drain_access")) {
        vec3 hatch = landmark_pos(LM_DRAIN_HATCH);
        if (near(hatch, 2)) interact_add(hatch, 2.5f, IK_DRAIN_ENTER, 0, "Climb down into the storm drain");
        vec3 ladder(DRAIN_ORIGIN_X, DRAIN_ORIGIN_Y, DRAIN_ORIGIN_Z - 89);
        if (near(ladder, 2)) interact_add(ladder, 2.5f, IK_DRAIN_EXIT, 0, "Climb out");
    }
    // Vehicles
    int bestV = -1;
    float bestD = 3.2f;
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (!v.active || v.destroyed || v.burnTime > 0) continue;
        float d = length(flat(v.pos - pp)) - g_vehicleDefs[v.type].halfExtents.x;
        if (d < bestD && fabsf(v.pos.y - pp.y) < 2.0f) { bestD = d; bestV = i; }
    }
    if (bestV >= 0) {
        Vehicle& v = g.vehicles[bestV];
        char t[80];
        fmt_str(t, sizeof(t), v.driver > 0 ? "Carjack %s" : "Enter %s", g_vehicleDefs[v.type].name);
        interact_add(v.pos + vec3(0, 1.0f, 0), 3.5f + g_vehicleDefs[v.type].halfExtents.x, IK_VEHICLE, bestV, t);
    }
    // Stealth takedowns on unaware enemies from behind
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!actor_alive(a) || a.knockedOut > 0 || a.state != AS_LOCOMOTION || a.kind == AK_CIVILIAN || a.kind == AK_DRONE) continue;
        if (a.app.body == BODY_BRUTE || a.app.body == BODY_OVERSEER || a.faction == FAC_AEGIS) continue;
        if (a.aiMode == AI_COMBAT || a.aiMode == AI_FOLLOW || a.aiMode == AI_SPECTATE) continue;
        vec3 d = pp - a.pos;
        if (length(flat(d)) > 1.7f || fabsf(d.y) > 1.0f) continue;
        if (dot(normalize(flat(d)), dir_from_yaw(a.yaw)) > -0.3f) continue;
        interact_add(a.pos + vec3(0, 1.0f, 0), 1.9f, IK_TAKEDOWN, i, "Stealth takedown");
        break;
    }
}

static void choose_interactable() {
    Actor& p = g.actors[PLAYER];
    g.interactBest = -1;
    float best = 1e9f;
    for (int i = 0; i < g.interactCount; i++) {
        const Interactable& it = g.interactables[i];
        float d = length(it.pos - (p.pos + vec3(0, 1.0f, 0)));
        if (d > it.radius) continue;
        float pri = it.kind == IK_TALK || it.kind == IK_MISSION ? 0.0f : it.kind == IK_TAKEDOWN ? 0.5f : it.kind == IK_START_MISSION ? 1.0f : 2.0f;
        float score = d + pri * 2.0f;
        if (score < best) { best = score; g.interactBest = i; }
    }
}

// ---------------------------------------------------------------------------
// Atmosphere
// ---------------------------------------------------------------------------
static void update_atmosphere(float dt) {
    Atmosphere& a = g.atm;
    Actor& p = g.actors[PLAYER];
    bool underground = g.cam.pos.y < -20.0f;
    float indoorAmb = 0;
    bool indoors = world_is_indoors(g.cam.pos, &indoorAmb) || underground;
    // Weather drifts slowly
    g.lightningTimer -= dt;
    if (!g.inMission && g.frame % 600 == 0 && g.rng.chance(0.3f)) g.rainTarget = g.rng.range(0.25f, 1.0f);
    g.rain = approach(g.rain, g.rainTarget, dt * 0.05f);
    if (g.rain > 0.75f && g.lightningTimer <= 0 && !underground) {
        g.lightningTimer = g.rng.range(8.0f, 25.0f);
        g.lightning = 1.0f;
        audio_play(SND_THUNDER, g.rng.range(0.4f, 0.8f), g.rng.range(0.85f, 1.1f));
    }
    g.lightning = fmaxf(0.0f, g.lightning - dt * 3.5f);
    float flash = g.lightning > 0 ? g.lightning * (0.5f + 0.5f * sinf(g.realTime * 60.0f)) : 0.0f;
    float inv = g.pd.phase >= 3 ? 1.0f : 0.0f;
    g.invasion = approach(g.invasion, inv, dt * 0.2f);
    a.sunDir = normalize(vec3(-0.35f, 0.55f, 0.4f));
    a.sunColor = lerp(vec3(0.16f, 0.2f, 0.32f), vec3(0.25f, 0.12f, 0.1f), g.invasion);
    a.skyTop = lerp(vec3(0.004f, 0.006f, 0.014f), vec3(0.03f, 0.008f, 0.012f), g.invasion);
    a.skyHorizon = lerp(vec3(0.07f, 0.055f, 0.06f), vec3(0.22f, 0.06f, 0.04f), g.invasion);
    a.ambientUp = lerp(vec3(0.03f, 0.035f, 0.05f), vec3(0.06f, 0.03f, 0.03f), g.invasion);
    a.ambientDown = vec3(0.02f, 0.016f, 0.014f);
    a.fogColor = lerp(vec3(0.05f, 0.045f, 0.05f), vec3(0.14f, 0.05f, 0.04f), g.invasion);
    a.fogDensity = 0.0055f + g.rain * 0.003f + (g.pd.phase == 2 ? 0.003f : 0.0f);
    a.fogHeightFalloff = 0.035f;
    a.fogBase = 0.0f;
    a.fogMax = 0.94f;
    a.rain = indoors ? 0.0f : g.rain;
    a.flash = flash;
    a.invasion = g.invasion;
    a.windowsOn = g.power ? 1.0f : 0.06f;
    a.emissiveScale = 1.0f;
    a.exposure = 1.65f;
    a.cloudCover = 0.45f + g.rain * 0.5f;
    a.wind = 0.3f + g.rain * 0.5f;
    a.alienPulse = g.invasion;
    if (underground) {
        a.ambientUp = vec3(0.05f, 0.055f, 0.065f) + vec3(indoorAmb, indoorAmb, indoorAmb) * 0.1f;
        a.ambientDown = vec3(0.025f, 0.025f, 0.03f);
        a.fogBase = g.cam.pos.y - 5.0f;
        a.fogDensity = 0.012f;
        a.fogColor = vec3(0.02f, 0.025f, 0.03f);
        a.sunColor = vec3(0, 0, 0);
        a.flash = 0;
    } else if (indoors) {
        a.ambientUp = a.ambientUp + vec3(indoorAmb, indoorAmb, indoorAmb) * 0.12f;
    }
    // District banner
    int d = district_at(p.pos);
    if (d != g.district) {
        g.district = d;
        g.districtBanner = 3.5f;
    }
    g.districtBanner = fmaxf(0.0f, g.districtBanner - dt);
    // Audio ambience & listener
    audio_set_listener(g.cam.pos, camera_forward());
    audio_set_ambience(underground ? 0.0f : g.rain, underground ? 0.0f : 1.0f, indoors ? 1.0f : 0.0f, g.invasion);
}

static void update_music() {
    int mood = MUSIC_CALM;
    float intensity = 0.4f;
    int fighting = 0;
    bool aliens = false;
    for (int i = 1; i < MAX_ACTORS; i++) {
        const Actor& a = g.actors[i];
        if (!actor_alive(a) || a.aiMode != AI_COMBAT || a.target != PLAYER) continue;
        if (length(a.pos - g.actors[PLAYER].pos) < 40.0f) { fighting++; if (a.faction == FAC_ALIEN) aliens = true; }
    }
    if (fighting > 0) { mood = aliens ? MUSIC_ALIEN : MUSIC_COMBAT; intensity = fminf(1.0f, 0.5f + fighting * 0.15f); }
    else if (g.wanted.level >= 2) { mood = MUSIC_CHASE; intensity = 0.7f; }
    else if (g.pd.phase >= 3) { mood = MUSIC_ALIEN; intensity = 0.5f; }
    else if (g.inMission) { mood = MUSIC_TENSION; intensity = 0.5f; }
    if (g.screen == SCR_MAIN_MENU || g.screen == SCR_CREDITS) { mood = MUSIC_MENU; intensity = 0.3f; }
    audio_set_music(mood, intensity);
    // Engines: player vehicle in slot 0, nearest others in 1..3
    int slots = 0;
    Actor& p = g.actors[PLAYER];
    if (p.state == AS_VEHICLE && p.vehicle >= 0) {
        Vehicle& v = g.vehicles[p.vehicle];
        audio_engine(0, v.pos, v.engineRev, fabsf(v.throttle), !v.destroyed);
        slots = 1;
    } else {
        audio_engine(0, vec3(0, 0, 0), 0, 0, false);
    }
    for (int i = 0; i < MAX_VEHICLES && slots < 4; i++) {
        Vehicle& v = g.vehicles[i];
        if (!v.active || v.driver == PLAYER || v.driver < 0 || v.destroyed) continue;
        if (length(v.pos - g.cam.pos) > 45.0f) continue;
        audio_engine(slots++, v.pos, v.engineRev, fabsf(v.throttle), true);
    }
    for (; slots < 4; slots++) audio_engine(slots, vec3(0, 0, 0), 0, 0, false);
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------
static void simulate(float dt) {
    Actor& p = g.actors[PLAYER];
    g.time += dt;
    g.pd.playTime += dt;
    interact_clear();
    gather_world_interactables();
    missions_update(dt);
    if (g.screen != SCR_PLAYING) return;
    choose_interactable();
    actors_update(dt);
    vehicles_update(dt);
    traffic_update(dt);
    ai_population_update(dt);
    wanted_update(dt);
    projectiles_update(dt);
    pickups_update(dt);
    combat_update(dt);
    setpieces_update(dt);
    fx_update(dt, world_particle_ground);
    dialogue_update(dt);
    cutscene_update(dt);
    // Player death
    if (p.state == AS_DEAD) {
        g.deathTimer += dt;
        if (g.deathTimer > 3.0f) {
            g.deathTimer = 0;
            g.pd.deaths++;
            if (g.inMission) mission_on_player_death();
            else game_set_screen(SCR_DEAD);
        }
    } else {
        g.deathTimer = 0;
    }
    for (int i = 0; i < g.noteCount;) {
        g.notes[i].time -= dt;
        if (g.notes[i].time <= 0) {
            for (int k = i + 1; k < g.noteCount; k++) g.notes[k - 1] = g.notes[k];
            g.noteCount--;
        } else i++;
    }
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------
bool game_init(const GameLaunchOptions* opt, int width, int height) {
    memset(&g, 0, sizeof(g));
    g.opt = *opt;
    g.width = width;
    g.height = height;
    g.timeScale = 1.0f;
    g.rng.seed(0xF16470u);
    g.lockTarget = -1;
    g.interactBest = -1;
    g.rebindAction = -1;
    g.power = true;
    g.rain = g.rainTarget = 0.85f;
    g.fade = 1.0f;
    g.fadeTarget = 0.0f;
    g.cam.fov = 62.0f;
    g.cam.pitch = 0.2f;
    if (!gl_load()) { log_error("OpenGL entry points missing"); return false; }
    settings_load(&g.settings);
    if (opt->autotest) {
        settings_defaults(&g.settings);
        g.settings.quality = GQ_MEDIUM;
    }
    renderer_init(g.settings.quality);
    if (!ui_init()) { log_error("UI init failed"); return false; }
    audio_init();
    settings_apply(g.settings);
    if (!anim_load_clips()) return false;
    world_generate(1337);
    vehicles_init_meshes();
    weapons_init_meshes();
    pickups_init_meshes();
    setpieces_init();
    missions_init();
    dialogue_load_all();
    cutscenes_load();
    player_reset(true);
    g.hasSave = save_exists(0) || save_exists(1);
    g.screen = SCR_MAIN_MENU;
    if (opt->autotest) autotest_begin(opt);
    log_info("FIGHTO ready (%s)", plat_name());
    return true;
}

bool game_frame(float dt, InputState* input, int width, int height) {
    if (g.opt.autotest) dt = 1.0f / 30.0f;
    g.width = width;
    g.height = height;
    g.input = input;
    g.realTime += dt;
    g.frame++;
    g.fps = lerpf(g.fps, dt > 0 ? 1.0f / dt : 60.0f, 0.05f);
    g.touch = input->touchMode;
    bool playing = g.screen == SCR_PLAYING;
    controls_update(&g.ctl, input, g.settings, playing, dt);
    if (g.opt.autotest) autotest_frame(dt);
    bool capture = playing && !g.touch && !g.cutscene && !g.opt.autotest;
    plat_capture_mouse(capture);

    // Screen-level hotkeys while playing
    if (playing && !g.cutscene) {
        if (g.ctl.pressed[ACT_PAUSE]) game_set_screen(SCR_PAUSE);
        else if (g.ctl.pressed[ACT_JOURNAL] || g.ctl.pressed[ACT_MAP]) { g.journalTab = g.ctl.pressed[ACT_MAP] ? 1 : 0; game_set_screen(SCR_JOURNAL); }
        else if (g.ctl.pressed[ACT_QUICKSAVE]) {
            char st[96];
            save_game(1, st, sizeof(st));
            str_copy(g.saveStatus, sizeof(g.saveStatus), st);
            g.saveStatusTime = 3.0f;
        }
        if (dialogue_active() && !mission_choice_active() && (input->pressed[KEY_ENTER] || (g.touch && g.ctl.pointerClick && g.ctl.pointer.y < g.height * 0.5f))) dialogue_skip_line();
    }
    playing = g.screen == SCR_PLAYING;
    // Time scale: hit-stop / slow motion
    float simDt = dt;
    if (g.slowMo > 0) {
        g.slowMo -= dt;
        simDt *= 0.25f;
    }
    if (playing) {
        simulate(simDt);
        camera_update(dt);
    } else if (g.screen == SCR_MAIN_MENU || g.screen == SCR_CREDITS || (g.screen == SCR_SETTINGS && g.settingsReturn == SCR_MAIN_MENU) || (g.screen == SCR_CONTROLS && g.settingsReturn == SCR_MAIN_MENU)) {
        // Attract mode: curated slow dolly shots along open streets (never through buildings),
        // cross-faded through black
        g.time += dt;
        const float shotLen = 14.0f;
        int shotIdx = (int)(g.realTime / shotLen) % 4;
        float u = fmodf(g.realTime, shotLen) / shotLen;
        vec3 c = landmark_pos(LM_PLAZA);
        vec3 from, to, look;
        switch (shotIdx) {
        case 0:  // Down the avenue towards downtown, low over the wet road
            from = vec3(road_x(4), 6.0f, block_center(4, 6).z);
            to = vec3(road_x(4), 7.5f, block_center(4, 4).z);
            look = vec3(0, -0.12f, -1);
            break;
        case 1:  // Harlan Street in The Blocks, neon at street level
            from = vec3(block_center(0, 5).x, 3.2f, road_x(6) - 2.0f);
            to = vec3(block_center(2, 5).x, 3.6f, road_x(6) - 2.0f);
            look = vec3(1, -0.08f, 0.15f);
            break;
        case 2:  // Skyline from beyond the city edge
            from = c + vec3(-560.0f, 90.0f, 330.0f);
            to = c + vec3(-520.0f, 110.0f, 200.0f);
            look = normalize(c + vec3(0, 80.0f, 0) - lerp(from, to, u));
            break;
        default:  // Ironworks rail road, sodium lights and steam
            from = vec3(block_center(7, 7).x, 5.0f, road_x(7));
            to = vec3(block_center(5, 7).x, 5.5f, road_x(7));
            look = vec3(-1, -0.1f, 0.2f);
            break;
        }
        float e = u * u * (3.0f - 2.0f * u);
        g.cam.pos = lerp(from, to, e);
        g.cam.target = g.cam.pos + normalize(look) * 10.0f;
        g.cam.fov = shotIdx == 2 ? 48.0f : 58.0f;
        float edge = fminf(u, 1.0f - u) * shotLen;
        g.fade = fmaxf(g.fade, 1.0f - saturate(edge / 0.8f));
        fx_update(dt, world_particle_ground);
        setpieces_update(dt);
    }
    update_atmosphere(dt);
    update_music();
    g.fade = approach(g.fade, g.fadeTarget, dt * 1.5f);
    g.whiteFlash = fmaxf(0.0f, g.whiteFlash - dt * 1.5f);
    g.shardGlow = fmaxf(0.0f, g.shardGlow - dt);
    g.letterbox = approach(g.letterbox, g.cutscene ? 1.0f : 0.0f, dt * 2.0f);
    g.saveStatusTime = fmaxf(0.0f, g.saveStatusTime - dt);

    // Render the world
    CameraState cs = camera_state();
    renderer_begin_frame(width, height, cs, g.atm, g.time, dt);
    actors_render();
    vehicles_render();
    pickups_render();
    projectiles_render();
    setpieces_render();
    missions_render_markers();
    Actor& p = g.actors[PLAYER];
    PostState post;
    memset(&post, 0, sizeof(post));
    float hp = p.maxHealth > 0 ? p.health / p.maxHealth : 1.0f;
    bool inGame = g.screen != SCR_MAIN_MENU && g.screen != SCR_CREDITS;
    post.damage = inGame ? g.damageFlash : 0;
    post.lowHealth = inGame && p.state != AS_DEAD ? saturate((0.35f - hp) / 0.35f) : 0;
    if (inGame && p.state == AS_DEAD) post.lowHealth = 1.0f;
    post.bloodScreen = inGame ? g.bloodScreen : 0;
    post.bloodSeed = g.bloodSeed;
    post.letterbox = g.letterbox;
    post.fade = g.fade;
    post.whiteFlash = g.whiteFlash;
    post.alienTint = saturate(g.shardGlow * 0.5f);
    post.saturation = g.pd.phase == 2 ? 0.75f : 0.92f;
    renderer_end_frame(post);
    // UI
    ui_begin(width, height);
    if (g.screen == SCR_PLAYING) hud_render();
    if (g.screen == SCR_PLAYING || g.screen == SCR_PAUSE) dialogue_render();
    menu_update_render();
    if (g.opt.autotest) autotest_overlay();
    ui_end();
    if (g.opt.autotest) autotest_after_frame();
    if (g.quitRequested) return false;
    if (g.opt.autotest && autotest_finished()) return false;
    return true;
}

void game_shutdown() {
    settings_save(g.settings);
    audio_shutdown();
    ui_shutdown();
    character_models_clear();
    world_destroy();
    renderer_shutdown();
}

void game_on_pause() {
    if (g.screen == SCR_PLAYING) game_set_screen(SCR_PAUSE);
    audio_pause(true);
    settings_save(g.settings);
}

void game_on_resume() { audio_pause(false); }

void game_continue_from_menu() { game_continue(); }
