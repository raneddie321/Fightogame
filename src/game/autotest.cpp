// FIGHTO - automated gameplay tests. Run with `fighto --autotest [outdir]`
// (outdir is used through FIGHTO_USERDIR). Every step drives the real game
// systems through injected input or direct calls and records PASS/FAIL.
#include "autotest.h"
#include "game.h"
#include "../ui/draw2d.h"
#include "../audio/audio.h"

enum { RUNNING = 0, PASS = 1, FAIL = -1 };

struct Step {
    const char* name;
    int (*fn)(int frame);
    int timeout;
};

static GameLaunchOptions g_opt;
static int g_step;
static int g_frame;
static bool g_done;
static char g_msg[256];
static char g_report[16384];
static int g_reportLen;
static int g_pass, g_fail;
static char g_pendingShot[64];
static double g_startTime;
static int g_target = -1;
static int g_vehicle = -1;
static vec3 g_startPos;
static float g_startYaw;
static int g_missionIdx;
static int g_missionFrames;
static double g_missionT0;
static int g_counter;
static const char* g_only;
static double g_stepStart;

static void report(const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    char line[512];
    fmt_strv(line, sizeof(line), f, ap);
    va_end(ap);
    log_info("[AUTOTEST] %s", line);
    g_reportLen += fmt_str(g_report + g_reportLen, (int)sizeof(g_report) - g_reportLen, "%s\n", line);
}

static void shot(const char* name) { str_copy(g_pendingShot, sizeof(g_pendingShot), name); }

static Actor& P() { return g.actors[PLAYER]; }

static void face_point(vec3 t) {
    vec3 d = t - g.cam.pos;
    g.cam.yaw = atan2f(d.x, d.z);
    P().yaw = g.cam.yaw;
}

static int spawn_enemy(int kind, const char* preset, float dist, int weapon) {
    vec3 f = dir_from_yaw(P().yaw);
    vec3 pos = P().pos + f * dist;
    pos.y = world_ground_height(pos + vec3(0, 2, 0), 0.3f, 3.0f);
    int id = actor_spawn_preset(kind, preset, pos, P().yaw + PI_F, 777);
    if (id >= 0) {
        Actor& a = g.actors[id];
        str_copy(a.tag, sizeof(a.tag), "autotest");
        a.persistent = true;
        if (weapon >= 0) { a.weapon = weapon; a.clipAmmo = g_weapons[weapon].clipSize; }
    }
    return id;
}

static void clear_test_actors() {
    for (int i = 1; i < MAX_ACTORS; i++)
        if (g.actors[i].active && str_eq(g.actors[i].tag, "autotest")) actor_despawn(i);
}

// A clear, open spot on the sidewalk for the action tests
static void goto_test_arena() {
    vec3 p = landmark_pos(LM_PLAZA) + vec3(0, 0, 22);
    p.y = world_ground_height(p + vec3(0, 2, 0), 0.3f, 3.0f);
    player_teleport(p, PI_F);
    g.cam.pitch = 0.18f;
}

// ---------------------------------------------------------------------------
// Steps
// ---------------------------------------------------------------------------
static int st_boot(int f) {
    if (f == 15) shot("01_main_menu");
    if (f < 25) return RUNNING;
    return g.screen == SCR_MAIN_MENU ? PASS : FAIL;
}

static int st_new_game(int f) {
    if (f == 0) game_new_game();
    if (f == 20 && cutscene_active()) shot("02_intro_cutscene");
    if (f < 30) return RUNNING;
    fmt_str(g_msg, sizeof(g_msg), "screen=%d mission=%d", g.screen, mission_current_id());
    return (P().active && g.screen == SCR_PLAYING && mission_active()) ? PASS : FAIL;
}

static int st_skip_cutscene(int f) {
    if (f == 0) g.ctl.pressed[ACT_PAUSE] = true;
    if (f < 10) return RUNNING;
    if (g.screen == SCR_PAUSE) game_set_screen(SCR_PLAYING);
    dialogue_clear();
    return !cutscene_active() ? PASS : FAIL;
}

static int st_movement(int f) {
    if (f == 0) {
        mission_abort();
        goto_test_arena();
        P().invulnerable = true;
        g_startPos = P().pos;
    }
    if (f < 90) {
        g.ctl.move = vec2(0, 1);
        g.ctl.down[ACT_SPRINT] = true;
        if (f == 60) shot("03_running");
        return RUNNING;
    }
    float d = dist_xz(P().pos, g_startPos);
    fmt_str(g_msg, sizeof(g_msg), "moved %.1f m, speed %.1f", d, P().speed);
    return d > 8.0f ? PASS : FAIL;
}

static int st_jump(int f) {
    if (f == 0) { g_startPos = P().pos; g.ctl.pressed[ACT_JUMP] = true; g.ctl.down[ACT_JUMP] = true; }
    if (f > 0 && (P().pos.y > g_startPos.y + 0.3f || !P().grounded)) {
        fmt_str(g_msg, sizeof(g_msg), "airborne at frame %d, y +%.2f", f, P().pos.y - g_startPos.y);
        return PASS;
    }
    return f > 25 ? FAIL : RUNNING;
}

static int st_camera(int f) {
    if (f == 0) g_startYaw = g.cam.yaw;
    if (f < 20) { g.ctl.look.x = 0.05f; return RUNNING; }
    float d = fabsf(wrap_angle(g.cam.yaw - g_startYaw));
    fmt_str(g_msg, sizeof(g_msg), "yaw changed by %.2f rad", d);
    return d > 0.5f ? PASS : FAIL;
}

static int st_melee(int f) {
    if (f == 0) {
        goto_test_arena();
        g_target = spawn_enemy(AK_THUG, "kings", 2.2f, W_FISTS);
        if (g_target < 0) return FAIL;
        ai_make_hostile(g_target, PLAYER);
        g_counter = 0;
    }
    Actor& t = g.actors[g_target];
    if (!t.active) return FAIL;
    face_point(t.pos + vec3(0, 1, 0));
    if (f == 1) g.ctl.pressed[ACT_LOCKON] = true;
    if (f % 9 == 0) { g.ctl.pressed[ACT_ATTACK] = true; g.ctl.down[ACT_ATTACK] = true; }
    if (f % 45 == 20) { g.ctl.pressed[ACT_KICK] = true; g.ctl.down[ACT_KICK] = true; }
    if (t.health < t.maxHealth * 0.6f && g_counter == 0) { shot("04_melee_blood"); g_counter = 1; }
    if (!actor_alive(t)) {
        if (g_counter < 2) { g_counter = 2; g_frame = 0; return RUNNING; }
        return RUNNING;
    }
    return RUNNING;
}

static int st_melee_wrap(int f) {
    // Run the fight, then wait 70 frames to capture the ragdoll and blood pool.
    static int deadAt = -1;
    if (f == 0) deadAt = -1;
    if (deadAt < 0) {
        st_melee(f);
        if (g_target >= 0 && !actor_alive(g.actors[g_target])) deadAt = f;
        return RUNNING;
    }
    if (f - deadAt == 70) {
        Actor& t = g.actors[g_target];
        g.cam.cinematic = true;
        g.cam.cinePos = t.pos + vec3(2.5f, 2.2f, 2.5f);
        g.cam.cineTarget = t.pos;
        g.cam.cineFov = 55;
        g.cam.pos = g.cam.cinePos;
        g.cam.target = g.cam.cineTarget;
        shot("05_ragdoll_blood_pool");
    }
    if (f - deadAt < 75) return RUNNING;
    camera_release();
    int parts = fx_particle_count(), decals = fx_decal_count();
    fmt_str(g_msg, sizeof(g_msg), "enemy down after %d frames; blood particles %d, decals %d, ragdoll %s", deadAt, parts, decals, g.actors[g_target].ragdoll ? "yes" : "no");
    return decals > 0 ? PASS : FAIL;
}

static int st_damage(int f) {
    if (f == 0) {
        P().invulnerable = false;
        float before = P().health;
        combat_damage(PLAYER, -1, 25.0f, actor_chest(P()), vec3(0, 0, 1), DMG_BULLET, B_CHEST);
        fmt_str(g_msg, sizeof(g_msg), "health %.0f -> %.0f, screen blood %.2f", before, P().health, g.bloodScreen);
        P().invulnerable = true;
        if (P().health < before && g.bloodScreen > 0) return RUNNING;
        return FAIL;
    }
    if (f == 3) shot("06_damage_screen_blood");
    if (f < 5) return RUNNING;
    P().health = P().maxHealth;
    return PASS;
}

static int st_firearms(int f) {
    if (f == 0) {
        clear_test_actors();
        goto_test_arena();
        player_give_weapon(W_PISTOL, 60);
        player_select_weapon(W_PISTOL);
        g_target = spawn_enemy(AK_GUNMAN, "kings", 9.0f, W_FISTS);
        if (g_target < 0) return FAIL;
        g.actors[g_target].aiMode = AI_HOLD;
        g.actors[g_target].hostileToPlayer = true;
        g_counter = g.pd.shotsFired;
    }
    Actor& t = g.actors[g_target];
    // Aim at the chest
    vec3 aim = actor_chest(t);
    vec3 d = aim - g.cam.pos;
    g.cam.yaw = atan2f(d.x, d.z);
    g.cam.pitch = -atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
    g.ctl.down[ACT_AIM] = true;
    if (f > 15 && f % 8 == 0) { g.ctl.pressed[ACT_ATTACK] = true; g.ctl.down[ACT_ATTACK] = true; }
    if (f == 40) shot("07_gunfight_aim");
    if (!actor_alive(t)) {
        fmt_str(g_msg, sizeof(g_msg), "target down; shots %d, hits %d, clip %d", g.pd.shotsFired - g_counter, g.pd.shotsHit, P().clipAmmo);
        return (g.pd.shotsFired > g_counter) ? PASS : FAIL;
    }
    return RUNNING;
}

static int st_reload(int f) {
    if (f == 0) {
        P().clipAmmo = 3;
        g.ctl.pressed[ACT_RELOAD] = true;
    }
    if (f < 60) return RUNNING;
    fmt_str(g_msg, sizeof(g_msg), "clip after reload %d / %d, reserve %d", P().clipAmmo, g_weapons[P().weapon].clipSize, g.pd.reserve[P().weapon]);
    return P().clipAmmo == g_weapons[P().weapon].clipSize ? PASS : FAIL;
}

static int st_shotgun(int f) {
    if (f == 0) {
        clear_test_actors();
        player_give_weapon(W_SHOTGUN, 30);
        player_select_weapon(W_SHOTGUN);
        g_target = spawn_enemy(AK_THUG, "varga_thug", 5.0f, W_FISTS);
        if (g_target < 0) return FAIL;
        g.actors[g_target].aiMode = AI_HOLD;
        g_counter = g.pd.kills;
    }
    Actor& t = g.actors[g_target];
    vec3 aim = actor_head(t);
    vec3 d = aim - g.cam.pos;
    g.cam.yaw = atan2f(d.x, d.z);
    g.cam.pitch = -atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
    g.ctl.down[ACT_AIM] = true;
    if (f > 15 && f % 25 == 0) { g.ctl.pressed[ACT_ATTACK] = true; g.ctl.down[ACT_ATTACK] = true; }
    if (!actor_alive(t)) {
        static int after = 0;
        if (after == 0) after = f;
        if (f - after == 20) shot("08_shotgun_gore");
        if (f - after < 25) return RUNNING;
        after = 0;
        fmt_str(g_msg, sizeof(g_msg), "kills %d -> %d, decals %d", g_counter, g.pd.kills, fx_decal_count());
        return g.pd.kills > g_counter ? PASS : FAIL;
    }
    return RUNNING;
}

static int st_vehicle_enter(int f) {
    if (f == 0) {
        clear_test_actors();
        player_select_weapon(W_FISTS);
        // Right-hand lane of the north-south avenue west of Liberty Plaza, heading +Z
        vec3 lane(road_x(4) - 3.6f, 0.0f, block_center(4, 3).z - 30.0f);
        player_teleport(lane + vec3(3.0f, 0, 0), 0);
        g_vehicle = vehicle_spawn(VT_MUSCLE, lane, 0, vec3(0.6f, 0.05f, 0.05f), VAI_PARKED);
        if (g_vehicle < 0) return FAIL;
        str_copy(g.vehicles[g_vehicle].tag, 16, "autotest");
    }
    if (f == 5 || f == 10) { g.ctl.pressed[ACT_INTERACT] = true; g.ctl.down[ACT_INTERACT] = true; }
    if (P().state == AS_VEHICLE) { fmt_str(g_msg, sizeof(g_msg), "entered vehicle at frame %d", f); return PASS; }
    return RUNNING;
}

static int st_drive(int f) {
    if (f == 0) g_startPos = g.vehicles[g_vehicle].pos;
    if (f < 150) {
        g.ctl.move = vec2(0, 1);
        if (f == 120) shot("09_driving_chase_cam");
        return RUNNING;
    }
    float d = dist_xz(g.vehicles[g_vehicle].pos, g_startPos);
    fmt_str(g_msg, sizeof(g_msg), "drove %.1f m, top speed %.1f km/h", d, length(g.vehicles[g_vehicle].vel) * 3.6f);
    return d > 15.0f ? PASS : FAIL;
}

static int st_vehicle_exit(int f) {
    if (f == 0) { g.vehicles[g_vehicle].vel = vec3(0, 0, 0); }
    if (f == 30) { g.ctl.pressed[ACT_INTERACT] = true; g.ctl.down[ACT_INTERACT] = true; }
    if (f > 30 && P().state != AS_VEHICLE) return PASS;
    return RUNNING;
}

static int st_police(int f) {
    if (f == 0) {
        wanted_crime(CRIME_COP_ASSAULT, P().pos, true);
        fmt_str(g_msg, sizeof(g_msg), "wanted level %d", g.wanted.level);
        if (g.wanted.level < 1) return FAIL;
    }
    int cars = 0;
    for (int i = 0; i < MAX_VEHICLES; i++) if (g.vehicles[i].active && g.vehicles[i].type == VT_POLICE && g.vehicles[i].ai == VAI_CHASE) cars++;
    if (cars > 0) {
        static int seenAt = 0;
        if (seenAt == 0) seenAt = f;
        if (f - seenAt == 90) {
            // Look at the nearest police car
            for (int i = 0; i < MAX_VEHICLES; i++) if (g.vehicles[i].active && g.vehicles[i].type == VT_POLICE) { face_point(g.vehicles[i].pos); break; }
            shot("10_police_response");
        }
        if (f - seenAt < 95) return RUNNING;
        seenAt = 0;
        fmt_str(g_msg, sizeof(g_msg), "wanted %d, police units dispatched: %d", g.wanted.level, cars);
        wanted_clear();
        return PASS;
    }
    return RUNNING;
}

static bool mission_bit(int i) {
    FIGHTO_UNUSED(i);
    return false;
}

static int st_missions(int f) {
    FIGHTO_UNUSED(mission_bit);
    if (f == 0) {
        const char* first = getenv("FIGHTO_AUTOTEST_MISSION");
        g_missionIdx = first ? clamp_t(str_to_int(first), 0, 31) : 0;
        g_missionFrames = 0;
        mission_abort();
        // Optional: treat earlier missions as completed so a later mission can be tested directly
        const char* unlock = getenv("FIGHTO_AUTOTEST_UNLOCK");
        if (unlock && unlock[0] == '1') {
            for (int k = 0; k < g_missionIdx; k++) if (!mission_is_side(k)) g.pd.missionsDone |= 1u << (mission_id(k) & 31);
            if (g_missionIdx >= 5) game_set_phase(1);
            if (g_missionIdx >= 6) flag_set("aegis_access", 1);
        }
        clear_test_actors();
        P().invulnerable = true;
        if (P().state == AS_VEHICLE) player_exit_vehicle();
        report("  missions to run: %d", mission_count());
    }
    if (g.screen == SCR_MISSION_COMPLETE) {
        if (flag_get("game_complete") == 1) flag_set("game_complete", 2);
        game_set_screen(SCR_PLAYING);
    }
    if (g.screen == SCR_MISSION_FAILED) {
        report("  mission %d '%s' FAILED: %s", g_missionIdx, mission_name(g_missionIdx), mission_fail_reason());
        g_fail++;
        mission_abort();
        game_set_screen(SCR_PLAYING);
        g_missionIdx++;
        g_missionFrames = 0;
    }
    if (g.screen != SCR_PLAYING) game_set_screen(SCR_PLAYING);
    if (g_missionIdx >= mission_count()) {
        fmt_str(g_msg, sizeof(g_msg), "phase %d, missions bitmask 0x%x", g.pd.phase, g.pd.missionsDone);
        return PASS;
    }
    if (!mission_active() && g_missionFrames == 0) {
        if (!mission_available(g_missionIdx)) {
            report("  mission %d '%s' not available (skipped)", g_missionIdx, mission_name(g_missionIdx));
            g_missionIdx++;
            return RUNNING;
        }
        vec3 sp = mission_start_pos(g_missionIdx);
        if (P().state == AS_VEHICLE) player_exit_vehicle();
        player_teleport(sp + vec3(1.0f, 0, 1.0f), 0);
        mission_start(g_missionIdx);
        g_missionFrames = 1;
        g_missionT0 = plat_time_seconds();
        return RUNNING;
    }
    g_missionFrames++;
    char nm[64];
    if (g_missionFrames == 90) {
        fmt_str(nm, sizeof(nm), "m%02d_%s", g_missionIdx + 1, "a");
        shot(nm);
    }
    if (g_missionFrames % 12 == 0) mission_debug_complete_step();
    if (!mission_active() && g_missionFrames > 1) {
        report("  mission %d '%s' completed in %d frames (%.1f s)", g_missionIdx, mission_name(g_missionIdx), g_missionFrames, plat_time_seconds() - g_missionT0);
        g_pass++;
        g_missionIdx++;
        g_missionFrames = 0;
        return RUNNING;
    }
    if (g_missionFrames > 5000) {
        report("  mission %d '%s' TIMED OUT (objective: %s)", g_missionIdx, mission_name(g_missionIdx), mission_objective());
        g_fail++;
        mission_abort();
        g_missionIdx++;
        g_missionFrames = 0;
    }
    return RUNNING;
}

static int st_showcase(int f) {
    // Dedicated beauty shots of key locations
    struct Cam { vec3 pos, target; const char* name; };
    vec3 aegis(AEGIS_ORIGIN_X, AEGIS_ORIGIN_Y, AEGIS_ORIGIN_Z);
    vec3 nest(DRAIN_ORIGIN_X, DRAIN_ORIGIN_Y, DRAIN_ORIGIN_Z);
    Cam cams[] = {
        {landmark_pos(LM_DINER) + vec3(-12, 2.0f, -14), landmark_pos(LM_DINER) + vec3(0, 3, 5), "11_lucky_star_diner"},
        {landmark_pos(LM_PLAZA) + vec3(40, 18, 60), landmark_pos(LM_PLAZA) + vec3(0, 60, -40), "12_downtown_skyline"},
        {aegis + vec3(10, 3.0f, 26), aegis + vec3(21, 3.5f, 35), "13_aegis_holo_globe"},
        {aegis + vec3(-20, 2.5f, -62), aegis + vec3(-22, 1.5f, -80), "14_aegis_archive"},
        {aegis + vec3(26, 3.0f, -52), aegis + vec3(26, 5.0f, -72), "15_project_lantern_reactor"},
        {nest + vec3(-10, 3.0f, 36), nest + vec3(0, 7.0f, 66), "16_alien_nest"},
        {landmark_pos(LM_FIGHT_PIT) + vec3(-8, 3.5f, 14), landmark_pos(LM_FIGHT_PIT) + vec3(0, 1, 22), "17_fight_pit"},
        {landmark_pos(LM_WAREHOUSE) + vec3(-20, 3.0f, 12), landmark_pos(LM_WAREHOUSE) + vec3(18, 1.5f, 28), "18_halberd_warehouse"},
        {landmark_pos(LM_HOME) + vec3(6, 2.5f, -10), landmark_pos(LM_HOME) + vec3(0, 2, 6), "19_burned_home"},
    };
    int n = ARRAY_COUNT(cams);
    int idx = f / 12;
    if (idx >= n) { camera_release(); return PASS; }
    Cam& c = cams[idx];
    player_teleport(c.target + vec3(0.5f, 0, 0.5f), 0);
    g.cam.cinematic = true;
    g.cam.cinePos = g.cam.pos = c.pos;
    g.cam.cineTarget = g.cam.target = c.target;
    g.cam.cineFov = g.cam.fov = 60;
    if (f % 12 == 10) shot(c.name);
    return RUNNING;
}

static int st_invasion_shot(int f) {
    if (f == 0) game_set_phase(3);
    vec3 c = landmark_pos(LM_PLAZA);
    g.cam.cinematic = true;
    g.cam.cinePos = g.cam.pos = c + vec3(-30, 4.0f, 70);
    g.cam.cineTarget = g.cam.target = c + vec3(10, 140, -60);
    g.cam.cineFov = g.cam.fov = 70;
    if (f == 20) shot("20_invasion_sky");
    if (f < 25) return RUNNING;
    camera_release();
    return PASS;
}

static int st_save_load(int f) {
    if (f > 0) return PASS;
    char st[128];
    int before = g.pd.money = 12345;
    flag_set("autotest_flag", 77);
    if (!save_game(2, st, sizeof(st))) { fmt_str(g_msg, sizeof(g_msg), "save failed: %s", st); return FAIL; }
    if (!plat_user_file_exists("save_slot2.dat")) { str_copy(g_msg, sizeof(g_msg), "save file missing"); return FAIL; }
    // Second save creates the backup
    save_game(2, st, sizeof(st));
    g.pd.money = 1;
    flag_set("autotest_flag", 0);
    if (!load_game(2, st, sizeof(st)) || g.pd.money != before || flag_get("autotest_flag") != 77) { fmt_str(g_msg, sizeof(g_msg), "round trip mismatch (%s) money=%d", st, g.pd.money); return FAIL; }
    // Corrupt the main file: the backup must be used
    const char junk[] = "FGTO this is not a valid save";
    plat_write_user_file("save_slot2.dat", junk, sizeof(junk));
    g.pd.money = 5;
    bool ok = load_game(2, st, sizeof(st));
    if (!ok || g.pd.money != before || !strstr(st, "backup")) { fmt_str(g_msg, sizeof(g_msg), "backup recovery failed: %s", st); return FAIL; }
    // Corrupt both: loading must fail cleanly
    plat_write_user_file("save_slot2.bak", junk, sizeof(junk));
    ok = load_game(2, st, sizeof(st));
    if (ok) { str_copy(g_msg, sizeof(g_msg), "corrupt save was accepted"); return FAIL; }
    fmt_str(g_msg, sizeof(g_msg), "round trip ok; backup recovery ok; corrupt rejected with '%s'", st);
    return PASS;
}

static int st_settings(int f) {
    if (f > 0) return PASS;
    Settings saved = g.settings;
    g.settings.gore = 1;
    g.settings.fov = 70;
    g.settings.keys[ACT_JUMP][1] = KEY_J;
    if (!settings_save(g.settings)) return FAIL;
    Settings loaded;
    settings_load(&loaded);
    bool ok = loaded.gore == 1 && (int)loaded.fov == 70 && loaded.keys[ACT_JUMP][1] == KEY_J;
    g.settings = saved;
    settings_save(g.settings);
    settings_apply(g.settings);
    str_copy(g_msg, sizeof(g_msg), ok ? "settings persisted and reloaded" : "settings mismatch");
    return ok ? PASS : FAIL;
}

static int st_menus(int f) {
    static const int screens[] = {SCR_PAUSE, SCR_SETTINGS, SCR_CONTROLS, SCR_JOURNAL, SCR_JOURNAL, SCR_JOURNAL, SCR_JOURNAL, SCR_SHOP, SCR_UPGRADES, SCR_DEAD, SCR_MISSION_FAILED, SCR_CREDITS, SCR_MAIN_MENU};
    static const char* names[] = {"21_pause", "22_settings", "23_controls", "24_journal_missions", "25_journal_map", "26_journal_evidence", "27_journal_stats", "28_shop", "29_upgrades", "30_flatlined", "31_mission_failed", "32_credits", "33_main_menu_final"};
    int n = ARRAY_COUNT(screens);
    int idx = f / 8;
    if (idx >= n) return PASS;
    if (f % 8 == 0) {
        g.settingsReturn = SCR_PAUSE;
        g.screen = screens[idx];
        if (screens[idx] == SCR_JOURNAL) g.journalTab = idx - 3;
        g.menuAnim = 0;
        g.pd.evidence[0] = g.pd.evidence[1] = true;
    }
    if (f % 8 == 6) shot(names[idx]);
    // Prevent menus from reacting to stale input
    memset(&g.ctl, 0, sizeof(g.ctl));
    return RUNNING;
}

static int st_audio(int f) {
    if (f > 0) return PASS;
    int ok = 0;
    for (int i = 0; i < SND_COUNT; i++) if (audio_debug_render_peak(i) > 0.01f) ok++;
    fmt_str(g_msg, sizeof(g_msg), "%d / %d synthesised sounds have signal", ok, SND_COUNT);
    float buf[1024];
    game_audio_mix(buf, 512);
    return ok == SND_COUNT ? PASS : FAIL;
}

// Measures real frame times at representative locations (software GL in CI is slow; GPUs are far faster)
static double g_lastFrameT;
static double g_probeAcc;
static int g_probeN;
static int st_perf(int f) {
    struct Spot { int lm; vec3 off; float yaw; const char* name; };
    static const Spot spots[] = {
        {LM_PLAZA, vec3(0, 0, 10), PI_F, "Liberty Plaza (downtown)"},
        {LM_BUS_STOP, vec3(0, 0, 0), PI_F * 0.5f, "Harlan St. (The Blocks)"},
        {LM_FIGHT_PIT, vec3(0, 0, 12), 0, "Fight Pit interior"},
        {LM_AEGIS_BRIEFING, vec3(0, 0, 0), PI_F * 0.5f, "A.E.G.I.S. briefing room"},
        {LM_DRAIN_NEST, vec3(0, 0, -10), 0, "Alien nest"},
    };
    const int per = 24;
    int idx = f / per, k = f % per;
    double now = plat_time_seconds();
    if (idx >= ARRAY_COUNT(spots)) { camera_release(); return PASS; }
    if (k == 0) {
        const Spot& sp = spots[idx];
        vec3 p = landmark_pos(sp.lm) + sp.off;
        if (P().state == AS_VEHICLE) player_exit_vehicle();
        player_teleport(p, sp.yaw);
        g_probeAcc = 0;
        g_probeN = 0;
    } else if (k >= 8) {
        g_probeAcc += now - g_lastFrameT;
        g_probeN++;
    }
    g_lastFrameT = now;
    if (k == per - 2) {
        static const char* shots[] = {"perf_plaza", "perf_blocks", "perf_pit", "perf_briefing", "perf_nest"};
        shot(shots[idx]);
    }
    if (k == per - 1) report("  perf: %s: %.1f ms/frame (%d draws, %d tris)", spots[idx].name, g_probeAcc * 1000.0 / fmax(1, g_probeN), renderer_draw_calls(), renderer_triangles());
    return RUNNING;
}

static const Step g_steps[] = {
    {"Boot to main menu", st_boot, 60},
    {"New game starts mission 1", st_new_game, 80},
    {"Skip intro cutscene", st_skip_cutscene, 30},
    {"Movement & sprint", st_movement, 120},
    {"Jump", st_jump, 40},
    {"Camera look", st_camera, 40},
    {"Melee combat, blood & ragdoll", st_melee_wrap, 900},
    {"Player damage & screen blood", st_damage, 20},
    {"Firearms: aim & shoot", st_firearms, 600},
    {"Reload", st_reload, 90},
    {"Shotgun headshot gore", st_shotgun, 600},
    {"Enter vehicle", st_vehicle_enter, 90},
    {"Drive vehicle", st_drive, 200},
    {"Exit vehicle", st_vehicle_exit, 90},
    {"Wanted level & police dispatch", st_police, 900},
    {"Story & side missions (scripted run)", st_missions, 120000},
    {"Location showcase screenshots", st_showcase, 200},
    {"Invasion sky", st_invasion_shot, 40},
    {"Save / load / corruption handling", st_save_load, 10},
    {"Settings persistence", st_settings, 10},
    {"Menus & overlays", st_menus, 140},
    {"Audio synthesis", st_audio, 10},
    {"Performance probe", st_perf, 200},
};

void autotest_begin(const GameLaunchOptions* opt) {
    g_opt = *opt;
    g_step = 0;
    g_frame = 0;
    g_done = false;
    g_reportLen = 0;
    g_pass = g_fail = 0;
    g_startTime = plat_time_seconds();
    g_only = getenv("FIGHTO_AUTOTEST_ONLY");
    report("FIGHTO automated gameplay test");
    report("Platform: %s", plat_name());
}


static bool step_selected(int i) {
    if (!g_only || !g_only[0]) return true;
    if (i <= 2) return true;  // boot + new game are always needed
    // Case-sensitive substring match on the step name
    return strstr(g_steps[i].name, g_only) != 0;
}

void autotest_frame(float dt) {
    FIGHTO_UNUSED(dt);
    if (g_done) return;
    while (g_step < ARRAY_COUNT(g_steps) && g_frame == 0 && !step_selected(g_step)) g_step++;
    if (g_step >= ARRAY_COUNT(g_steps)) { plat_write_user_file("autotest_report.txt", g_report, (size_t)g_reportLen); g_done = true; return; }
    const Step& s = g_steps[g_step];
    if (g_frame == 0) { g_msg[0] = 0; g_stepStart = plat_time_seconds(); }
    int r = s.fn(g_frame);
    g_frame++;
    if (r == RUNNING && g_frame > s.timeout) {
        r = FAIL;
        if (!g_msg[0]) str_copy(g_msg, sizeof(g_msg), "timeout");
    }
    if (r != RUNNING) {
        if (r == PASS) g_pass++; else g_fail++;
        report("[%s] %s%s%s  (%d frames, %.1f s)", r == PASS ? "PASS" : "FAIL", s.name, g_msg[0] ? " - " : "", g_msg, g_frame, plat_time_seconds() - g_stepStart);
        g_step++;
        g_frame = 0;
        if (g_step >= ARRAY_COUNT(g_steps)) {
            double secs = plat_time_seconds() - g_startTime;
            report("Summary: %d passed, %d failed, %.1f s wall time, %d frames", g_pass, g_fail, secs, g.frame);
            report("Renderer last frame: %d draw calls, %d triangles", renderer_draw_calls(), renderer_triangles());
            plat_write_user_file("autotest_report.txt", g_report, (size_t)g_reportLen);
            g_done = true;
        }
    }
}

void autotest_overlay() {
    float s = ui_scale();
    if (g_done) return;
    char buf[128];
    fmt_str(buf, sizeof(buf), "AUTOTEST %d/%d: %s", g_step + 1, ARRAY_COUNT(g_steps), g_steps[g_step].name);
    ui_text_shadow(FONT_UI, 10 * s, (float)ui_height() - 30 * s, 18 * s, buf, vec4(0.6f, 1.0f, 0.6f, 0.8f));
}

void autotest_after_frame() {
    if (g_pendingShot[0]) {
        char name[96];
        fmt_str(name, sizeof(name), "%s.png", g_pendingShot);
        if (!renderer_screenshot(name)) report("  (screenshot %s failed)", name);
        g_pendingShot[0] = 0;
    }
}

bool autotest_finished() { return g_done; }
