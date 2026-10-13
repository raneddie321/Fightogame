// FIGHTO - in-game HUD: vitals, minimap, wanted level, weapon, objectives,
// prompts, crosshair, lock-on, notifications and touch controls.
#include "../game/game.h"
#include "draw2d.h"

static const vec4 kRed(0.86f, 0.08f, 0.06f, 1);
static const vec4 kWhite(1, 1, 1, 1);
static const vec4 kDim(1, 1, 1, 0.65f);
static const vec4 kGold(1.0f, 0.78f, 0.25f, 1);

static void bar(float x, float y, float w, float h, float v, vec4 col, vec4 back) {
    ui_rect(x - 2, y - 2, w + 4, h + 4, vec4(0, 0, 0, 0.55f));
    ui_rect(x, y, w, h, back);
    ui_rect_grad(x, y, w * saturate(v), h, col, with_alpha(col, 0.75f) * 0.8f);
}

static void star(vec2 c, float r, vec4 col) {
    vec2 pts[10];
    for (int i = 0; i < 10; i++) {
        float a = -PI_F * 0.5f + i * PI_F / 5.0f;
        float rr = (i & 1) ? r * 0.45f : r;
        pts[i] = vec2(c.x + cosf(a) * rr, c.y + sinf(a) * rr);
    }
    for (int i = 0; i < 10; i++) ui_triangle(c, pts[i], pts[(i + 1) % 10], col);
}

// ---------------------------------------------------------------------------
// Map / minimap
// ---------------------------------------------------------------------------
static vec4 district_color(int d) {
    switch (d) {
    case D_DOWNTOWN: return vec4(0.16f, 0.2f, 0.3f, 1);
    case D_BLOCKS: return vec4(0.28f, 0.17f, 0.15f, 1);
    case D_IRONWORKS: return vec4(0.22f, 0.22f, 0.18f, 1);
    case D_WESTHILLS: return vec4(0.14f, 0.24f, 0.15f, 1);
    case D_MIDTOWN: return vec4(0.2f, 0.2f, 0.24f, 1);
    default: return vec4(0.12f, 0.12f, 0.12f, 1);
    }
}

struct MapXf {
    vec2 center;      // screen centre
    vec3 focus;       // world focus
    float scale;      // pixels per metre
    float rot;        // rotation (minimap rotates with camera)
    bool clipCircle;
    float radius;
};

static vec2 map_pt(const MapXf& m, vec3 w) {
    float dx = w.x - m.focus.x, dz = w.z - m.focus.z;
    float c = cosf(m.rot), s = sinf(m.rot);
    // Screen: +x right, +y down. World: forward (yaw 0) is +Z which should be "up".
    float rx = dx * c - dz * s;
    float rz = dx * s + dz * c;
    return vec2(m.center.x - rx * m.scale, m.center.y - rz * m.scale);
}

static bool map_visible(const MapXf& m, vec2 p, float pad) {
    if (!m.clipCircle) return true;
    return length(p - m.center) < m.radius - pad;
}

static void map_quad_world(const MapXf& m, float x0, float z0, float x1, float z1, vec4 col) {
    ui_quad(map_pt(m, vec3(x0, 0, z0)), map_pt(m, vec3(x1, 0, z0)), map_pt(m, vec3(x1, 0, z1)), map_pt(m, vec3(x0, 0, z1)), col);
}

static void blip(const MapXf& m, vec3 w, float r, vec4 col, bool clampEdge) {
    vec2 p = map_pt(m, w);
    if (m.clipCircle) {
        vec2 d = p - m.center;
        float l = length(d);
        if (l > m.radius - r - 2) {
            if (!clampEdge) return;
            p = m.center + d * ((m.radius - r - 2) / l);
        }
    }
    ui_circle(p, r + 1.5f, vec4(0, 0, 0, 0.7f), 12);
    ui_circle(p, r, col, 12);
}

void map_render(float x, float y, float w, float h, bool full) {
    float s = ui_scale();
    Actor& p = g.actors[PLAYER];
    vec3 focus = p.pos;
    if (p.state == AS_VEHICLE && p.vehicle >= 0) focus = g.vehicles[p.vehicle].pos;
    MapXf m;
    m.center = vec2(x + w * 0.5f, y + h * 0.5f);
    m.clipCircle = !full;
    m.radius = w * 0.5f;
    if (full) {
        m.focus = vec3(g.mapCenter.x, 0, g.mapCenter.y);
        m.scale = fminf(w, h) / (CITY_HALF * 2.3f) * g.mapZoom;
        m.rot = PI_F;   // north (-Z) up
    } else {
        m.focus = focus;
        m.scale = (w * 0.5f) / 110.0f;
        m.rot = g.cam.yaw;   // rotate with the camera (camera forward points up)
        ui_circle(m.center, m.radius + 4 * s, vec4(0, 0, 0, 0.6f), 48);
        ui_circle(m.center, m.radius, vec4(0.06f, 0.06f, 0.07f, 0.92f), 48);
    }
    bool underground = focus.y < -20.0f;
    if (!underground) {
        // Blocks coloured by district, roads implied by the gaps
        for (int j = 0; j < CITY_BLOCKS; j++)
            for (int i = 0; i < CITY_BLOCKS; i++) {
                float x0 = block_min(i), x1 = block_max(i), z0 = block_min(j), z1 = block_max(j);
                if (!full) {
                    vec3 c((x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f);
                    if (dist_xz(c, focus) > 230.0f) continue;
                }
                vec4 col = district_color(block_district(i, j));
                // Clip blocks to the circle by subdividing into a 3x3 grid
                int sub = full ? 1 : 3;
                for (int a = 0; a < sub; a++)
                    for (int b = 0; b < sub; b++) {
                        float sx0 = lerpf(x0, x1, (float)a / sub), sx1 = lerpf(x0, x1, (float)(a + 1) / sub);
                        float sz0 = lerpf(z0, z1, (float)b / sub), sz1 = lerpf(z0, z1, (float)(b + 1) / sub);
                        vec2 cc = map_pt(m, vec3((sx0 + sx1) * 0.5f, 0, (sz0 + sz1) * 0.5f));
                        if (!map_visible(m, cc, 4 * s)) continue;
                        map_quad_world(m, sx0, sz0, sx1, sz1, col);
                    }
            }
        // Landmarks
        for (int i = 0; i < LM_COUNT; i++) {
            const Landmark& l = g_world.landmarks[i];
            if (!l.showOnMap || l.pos.y < -20.0f) continue;
            vec2 pt = map_pt(m, l.pos);
            if (!map_visible(m, pt, 4)) continue;
            ui_rect(pt.x - 3 * s, pt.y - 3 * s, 6 * s, 6 * s, vec4(0.85f, 0.85f, 0.9f, 0.8f));
            if (full) ui_text_shadow(FONT_UI, pt.x + 7 * s, pt.y - 9 * s, 16 * s, l.name, vec4(0.9f, 0.9f, 0.95f, 0.85f));
        }
    } else {
        ui_text(FONT_BOLD, m.center.x, m.center.y - 12 * s, 20 * s, "UNDERGROUND", vec4(0.6f, 0.7f, 0.8f, 0.6f), ALIGN_CENTER);
    }
    // Mission markers
    if (!mission_active()) {
        for (int i = 0; i < mission_count(); i++) {
            if (!mission_available(i)) continue;
            vec3 sp = mission_start_pos(i);
            blip(m, sp, 6 * s, kGold, !full);
        }
    }
    vec3 mk;
    if (mission_marker(&mk)) blip(m, mk, 7 * s, vec4(1.0f, 0.85f, 0.2f, 1), true);
    // Actors of interest
    for (int i = 1; i < MAX_ACTORS; i++) {
        const Actor& a = g.actors[i];
        if (!actor_alive(a) || a.knockedOut > 0) continue;
        bool hostile = a.aiMode == AI_COMBAT && a.target == PLAYER;
        bool police = a.faction == FAC_POLICE && g.wanted.level > 0;
        bool ally = a.aiMode == AI_FOLLOW;
        bool tagged = a.tag[0] != 0;
        if (!hostile && !police && !ally && !tagged) continue;
        vec4 col = hostile ? kRed : police ? (fmodf(g.realTime, 0.6f) < 0.3f ? vec4(0.2f, 0.4f, 1, 1) : kRed) : ally ? vec4(0.3f, 0.7f, 1.0f, 1) : vec4(0.9f, 0.9f, 0.9f, 1);
        blip(m, a.pos, 3.5f * s, col, false);
    }
    for (int i = 0; i < MAX_VEHICLES; i++) {
        const Vehicle& v = g.vehicles[i];
        if (!v.active || !v.siren) continue;
        blip(m, v.pos, 4.5f * s, fmodf(g.realTime, 0.5f) < 0.25f ? vec4(0.2f, 0.4f, 1, 1) : kRed, false);
    }
    // Player arrow
    vec2 pp = map_pt(m, focus);
    float yaw = (p.state == AS_VEHICLE && p.vehicle >= 0) ? g.vehicles[p.vehicle].yaw : p.yaw;
    float a = full ? (PI_F - yaw) : (g.cam.yaw - yaw);
    float r = 9 * s;
    vec2 tip(pp.x + sinf(a) * r * 1.3f, pp.y - cosf(a) * r * 1.3f);
    vec2 l(pp.x + sinf(a + 2.4f) * r, pp.y - cosf(a + 2.4f) * r);
    vec2 rr(pp.x + sinf(a - 2.4f) * r, pp.y - cosf(a - 2.4f) * r);
    ui_triangle(tip, l, pp, vec4(1, 1, 1, 1));
    ui_triangle(tip, pp, rr, vec4(0.85f, 0.85f, 0.85f, 1));
    if (!full) {
        ui_ring(m.center, m.radius, m.radius + 3 * s, vec4(0.85f, 0.1f, 0.08f, 0.9f), 48);
        // North indicator
        vec2 n = map_pt(m, focus + vec3(0, 0, -1000));
        vec2 dir = normalize(n - m.center);
        vec2 np = m.center + dir * (m.radius + 12 * s);
        ui_text_shadow(FONT_BOLD, np.x, np.y - 10 * s, 18 * s, "N", kWhite, ALIGN_CENTER);
    }
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------
static void draw_touch_controls() {
    TouchLayout lay;
    Actor& p = g.actors[PLAYER];
    touch_layout(&lay, ui_width(), ui_height(), p.state == AS_VEHICLE, p.weapon != W_FISTS, false);
    ui_circle(lay.joyCenter, lay.joyRadius, vec4(1, 1, 1, 0.08f), 32);
    ui_ring(lay.joyCenter, lay.joyRadius - 2, lay.joyRadius, vec4(1, 1, 1, 0.3f), 32);
    ui_circle(lay.joyKnob, lay.joyRadius * 0.42f, vec4(1, 1, 1, lay.joyActive ? 0.45f : 0.25f), 24);
    for (int i = 0; i < lay.count; i++) {
        const TouchButton& b = lay.buttons[i];
        bool down = g.ctl.down[b.action];
        ui_circle(b.c, b.r, down ? vec4(0.85f, 0.1f, 0.08f, 0.55f) : vec4(0, 0, 0, 0.35f), 28);
        ui_ring(b.c, b.r - 2, b.r, vec4(1, 1, 1, 0.5f), 28);
        float ts = b.r * 0.42f;
        ui_text(FONT_BOLD, b.c.x, b.c.y - ts * 0.55f, ts, b.label, vec4(1, 1, 1, 0.9f), ALIGN_CENTER);
    }
}

static void draw_enemy_bars() {
    float s = ui_scale();
    for (int i = 1; i < MAX_ACTORS; i++) {
        const Actor& a = g.actors[i];
        if (!actor_alive(a) || a.knockedOut > 0) continue;
        bool show = i == g.lockTarget || (a.aiMode == AI_COMBAT && a.target == PLAYER && g.time - a.lastDamageTime < 4.0f) || (a.nonLethal && a.aiMode == AI_COMBAT);
        if (!show) continue;
        if (length(a.pos - g.cam.pos) > 30.0f) continue;
        vec2 sp;
        if (!renderer_project(a.pos + vec3(0, a.height + 0.35f, 0), &sp)) continue;
        float w = 70 * s, h = 6 * s;
        bar(sp.x - w * 0.5f, sp.y, w, h, a.health / a.maxHealth, kRed, vec4(0.2f, 0.02f, 0.02f, 0.8f));
        if (a.name[0]) ui_text_shadow(FONT_UI, sp.x, sp.y - 24 * s, 18 * s, a.name, kDim, ALIGN_CENTER);
        if (i == g.lockTarget) {
            vec2 c;
            if (renderer_project(actor_chest(a), &c)) {
                float r = 22 * s;
                vec4 col = vec4(1.0f, 0.2f, 0.15f, 0.9f);
                float t = 3 * s;
                ui_rect(c.x - r, c.y - r, r * 0.5f, t, col); ui_rect(c.x - r, c.y - r, t, r * 0.5f, col);
                ui_rect(c.x + r * 0.5f, c.y - r, r * 0.5f, t, col); ui_rect(c.x + r - t, c.y - r, t, r * 0.5f, col);
                ui_rect(c.x - r, c.y + r - t, r * 0.5f, t, col); ui_rect(c.x - r, c.y + r * 0.5f, t, r * 0.5f, col);
                ui_rect(c.x + r * 0.5f, c.y + r - t, r * 0.5f, t, col); ui_rect(c.x + r - t, c.y + r * 0.5f, t, r * 0.5f, col);
            }
        }
    }
}

void hud_render() {
    if (g.cutscene) return;
    float s = ui_scale();
    float W = (float)ui_width(), H = (float)ui_height();
    Actor& p = g.actors[PLAYER];
    bool touch = g.touch;
    char buf[160];

    draw_enemy_bars();

    // Vitals (bottom left, above the touch joystick on mobile)
    float vx = 40 * s, vy = H - (touch ? 300 : 70) * s;
    if (!touch) {
        float mapR = 115 * s;
        map_render(vx, H - 40 * s - mapR * 2 - 60 * s, mapR * 2, mapR * 2, false);
        const char* dn = district_name(district_at(p.pos));
        ui_text_shadow(FONT_BOLD, vx + mapR, H - 40 * s - 52 * s, 20 * s, dn, kDim, ALIGN_CENTER);
        vx = vx + mapR * 2 + 30 * s;
    } else {
        float mapR = 80 * s;
        map_render(30 * s, 30 * s, mapR * 2, mapR * 2, false);
    }
    float bw = 280 * s;
    bar(vx, vy, bw, 14 * s, p.health / p.maxHealth, p.health / p.maxHealth < 0.3f && fmodf(g.realTime, 0.6f) < 0.3f ? vec4(1, 0.3f, 0.25f, 1) : kRed, vec4(0.15f, 0.03f, 0.03f, 0.8f));
    if (p.armor > 0) bar(vx, vy - 14 * s, bw * (p.armor / 100.0f), 6 * s, 1.0f, vec4(0.35f, 0.6f, 1.0f, 1), vec4(0, 0, 0, 0));
    bar(vx, vy + 22 * s, bw * 0.75f, 7 * s, p.stamina / p.maxStamina, vec4(0.95f, 0.8f, 0.3f, 1), vec4(0.15f, 0.12f, 0.03f, 0.8f));
    fmt_str(buf, sizeof(buf), "%d", (int)ceilf(p.health));
    ui_text_shadow(FONT_BOLD, vx + bw + 12 * s, vy - 8 * s, 26 * s, buf, kWhite);

    // Money & wanted level (top right)
    fmt_str(buf, sizeof(buf), "$%d", g.pd.money);
    ui_text_shadow(FONT_BOLD, W - 40 * s, 30 * s, 40 * s, buf, vec4(0.55f, 1.0f, 0.55f, 1), ALIGN_RIGHT);
    for (int i = 0; i < 5; i++) {
        vec2 c(W - 60 * s - i * 40 * s, 100 * s);
        bool lit = i < g.wanted.level;
        bool blink = lit && !g.wanted.seen && fmodf(g.realTime, 0.8f) < 0.4f;
        star(c, 15 * s, vec4(0, 0, 0, 0.6f));
        star(c, 13 * s, lit && !blink ? kWhite : vec4(0.3f, 0.3f, 0.3f, 0.6f));
    }
    // Notifications
    for (int i = 0; i < g.noteCount; i++) {
        const Notification& n = g.notes[i];
        float a = saturate(n.time * 2.0f) * saturate((4.0f - n.time) * 4.0f + 0.2f);
        ui_text_shadow(FONT_BOLD, W - 40 * s, 140 * s + i * 34 * s, 24 * s, n.text, with_alpha(n.color, a), ALIGN_RIGHT);
    }
    if (g.saveStatusTime > 0) ui_text_shadow(FONT_BOLD, W - 40 * s, H - 230 * s, 24 * s, g.saveStatus, vec4(0.8f, 0.9f, 1.0f, saturate(g.saveStatusTime)), ALIGN_RIGHT);

    // Weapon & ammo (bottom right)
    if (p.state != AS_VEHICLE) {
        const WeaponDef& wd = g_weapons[p.weapon];
        float wy = H - (touch ? 420 : 110) * s;
        ui_text_shadow(FONT_BOLD, W - 40 * s, wy, 30 * s, wd.name, kWhite, ALIGN_RIGHT);
        if (wd.clipSize > 0) {
            fmt_str(buf, sizeof(buf), "%d / %d", p.clipAmmo, g.pd.reserve[p.weapon]);
            vec4 col = p.clipAmmo == 0 ? kRed : (p.clipAmmo < wd.clipSize / 4 ? vec4(1, 0.7f, 0.3f, 1) : kWhite);
            ui_text_shadow(FONT_TITLE, W - 40 * s, wy + 34 * s, 46 * s, buf, col, ALIGN_RIGHT);
            if (p.reloadTimer > 0) ui_text_shadow(FONT_BOLD, W - 40 * s, wy - 32 * s, 22 * s, "RELOADING", kGold, ALIGN_RIGHT);
        } else {
            ui_text_shadow(FONT_UI, W - 40 * s, wy + 36 * s, 22 * s, g.lockTarget >= 0 ? "LOCKED ON" : "Q: lock-on  |  RMB: block", kDim, ALIGN_RIGHT);
        }
    } else if (p.vehicle >= 0) {
        Vehicle& v = g.vehicles[p.vehicle];
        float kmh = length(v.vel) * 3.6f;
        fmt_str(buf, sizeof(buf), "%d", (int)kmh);
        float wy = H - (touch ? 430 : 130) * s;
        ui_text_shadow(FONT_TITLE, W - 110 * s, wy, 70 * s, buf, kWhite, ALIGN_RIGHT);
        ui_text_shadow(FONT_BOLD, W - 40 * s, wy + 38 * s, 24 * s, "KM/H", kDim, ALIGN_RIGHT);
        ui_text_shadow(FONT_UI, W - 40 * s, wy + 74 * s, 20 * s, g_vehicleDefs[v.type].name, kDim, ALIGN_RIGHT);
        bar(W - 240 * s, wy + 104 * s, 200 * s, 6 * s, v.health / g_vehicleDefs[v.type].health, vec4(0.9f, 0.6f, 0.2f, 1), vec4(0.1f, 0.06f, 0.02f, 0.8f));
    }

    // Objective (top centre)
    if (mission_active()) {
        ui_text_shadow(FONT_BOLD, W * 0.5f, 22 * s, 22 * s, mission_title(), kGold, ALIGN_CENTER);
        ui_text_wrapped(FONT_UI, W * 0.5f, 52 * s, 28 * s, W * 0.5f, mission_objective(), kWhite, ALIGN_CENTER);
        float t = mission_timer();
        if (t >= 0) {
            fmt_str(buf, sizeof(buf), "%d:%02d", (int)t / 60, (int)t % 60);
            ui_text_shadow(FONT_TITLE, W * 0.5f, 100 * s, 44 * s, buf, t < 10 ? kRed : kWhite, ALIGN_CENTER);
        }
        float hp = mission_hold_progress();
        if (hp > 0) bar(W * 0.5f - 160 * s, H * 0.62f, 320 * s, 12 * s, hp, kGold, vec4(0.1f, 0.08f, 0.02f, 0.8f));
    }
    // District banner
    if (g.districtBanner > 0) {
        float a = saturate(g.districtBanner) * saturate((3.5f - g.districtBanner) * 3.0f);
        ui_text_shadow(FONT_TITLE, W * 0.5f, H * 0.2f, 64 * s, district_name(g.district), vec4(1, 1, 1, a * 0.9f), ALIGN_CENTER);
        ui_rect(W * 0.5f - 160 * s, H * 0.2f + 76 * s, 320 * s, 3 * s, vec4(0.85f, 0.08f, 0.06f, a));
    }

    // Crosshair
    bool armed = p.weapon != W_FISTS && p.state == AS_LOCOMOTION;
    if (armed && (g.ctl.down[ACT_AIM] || p.aimBlend > 0.5f)) {
        float cx = W * 0.5f, cy = H * 0.5f;
        float gap = (6 + (g.ctl.down[ACT_AIM] ? 0 : 10) + g.cam.recoilPitch * 300) * s;
        float len = 9 * s, t = 2 * s;
        vec4 col = g.hitMarker > 0 ? kRed : vec4(1, 1, 1, 0.9f);
        ui_rect(cx - gap - len, cy - t * 0.5f, len, t, col);
        ui_rect(cx + gap, cy - t * 0.5f, len, t, col);
        ui_rect(cx - t * 0.5f, cy - gap - len, t, len, col);
        ui_rect(cx - t * 0.5f, cy + gap, t, len, col);
        ui_circle(vec2(cx, cy), 1.6f * s, col, 8);
        if (g.killMarker > 0) {
            float r = 14 * s;
            ui_line(vec2(cx - r, cy - r), vec2(cx - r * 0.4f, cy - r * 0.4f), 3 * s, kRed);
            ui_line(vec2(cx + r, cy - r), vec2(cx + r * 0.4f, cy - r * 0.4f), 3 * s, kRed);
            ui_line(vec2(cx - r, cy + r), vec2(cx - r * 0.4f, cy + r * 0.4f), 3 * s, kRed);
            ui_line(vec2(cx + r, cy + r), vec2(cx + r * 0.4f, cy + r * 0.4f), 3 * s, kRed);
        }
    }

    // Interaction prompt
    if (g.interactBest >= 0 && p.state != AS_DEAD) {
        const Interactable& it = g.interactables[g.interactBest];
        const char* key = touch ? "USE" : key_name(g.settings.keys[ACT_INTERACT][0]);
        fmt_str(buf, sizeof(buf), "[%s]  %s", key, it.text);
        float tw = ui_text_width(FONT_BOLD, 28 * s, buf);
        ui_rect(W * 0.5f - tw * 0.5f - 18 * s, H * 0.7f - 10 * s, tw + 36 * s, 50 * s, vec4(0, 0, 0, 0.55f));
        ui_text(FONT_BOLD, W * 0.5f, H * 0.7f, 28 * s, buf, kWhite, ALIGN_CENTER);
    } else if (p.state == AS_VEHICLE) {
        fmt_str(buf, sizeof(buf), "[%s] Exit   [%s] Handbrake   [%s] Horn", key_name(g.settings.keys[ACT_INTERACT][0]), key_name(g.settings.keys[ACT_JUMP][0]), key_name(g.settings.keys[ACT_HORN][0]));
        if (!touch) ui_text_shadow(FONT_UI, W * 0.5f, H - 50 * s, 20 * s, buf, vec4(1, 1, 1, 0.5f), ALIGN_CENTER);
    }

    // Choice prompt
    if (mission_choice_active()) {
        float bwid = 820 * s, bh = 220 * s;
        float bx = W * 0.5f - bwid * 0.5f, by = H * 0.5f - bh * 0.5f;
        ui_rect(bx, by, bwid, bh, vec4(0, 0, 0, 0.75f));
        ui_rect(bx, by, bwid, 4 * s, kRed);
        ui_text_wrapped(FONT_BOLD, W * 0.5f, by + 24 * s, 28 * s, bwid - 60 * s, mission_choice_prompt(), kGold, ALIGN_CENTER);
        for (int i = 0; i < 2; i++) {
            float oy = by + 100 * s + i * 54 * s;
            bool hover = g.ctl.pointer.x > bx && g.ctl.pointer.x < bx + bwid && g.ctl.pointer.y > oy - 6 * s && g.ctl.pointer.y < oy + 44 * s;
            if (hover) ui_rect(bx + 20 * s, oy - 6 * s, bwid - 40 * s, 48 * s, vec4(0.85f, 0.08f, 0.06f, 0.35f));
            fmt_str(buf, sizeof(buf), "[%d]  %s", i + 1, mission_choice_option(i));
            ui_text(FONT_UI, bx + 40 * s, oy, 28 * s, buf, kWhite);
            if (hover && g.ctl.pointerClick) mission_choose(i + 1);
        }
    }
    // Low health heartbeat hint
    if (p.health < p.maxHealth * 0.25f && p.state != AS_DEAD) ui_text_shadow(FONT_BOLD, W * 0.5f, H * 0.82f, 24 * s, "BADLY HURT - get out of the fight", vec4(1, 0.3f, 0.25f, 0.6f + 0.4f * sinf(g.realTime * 6.0f)), ALIGN_CENTER);
    if (touch) draw_touch_controls();
    else if (g.newGame && g.time < 25.0f && !g.cutscene) {
        ui_text_shadow(FONT_UI, 40 * s, 40 * s, 20 * s, "WASD move  |  Mouse look  |  Shift sprint  |  Space jump  |  LMB attack  |  RMB block/aim  |  F kick  |  E interact  |  Tab journal", vec4(1, 1, 1, 0.55f));
    }
}
