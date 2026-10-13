// FIGHTO - pickups: weapons, ammo, health, armour, money, evidence and caches.
#include "game.h"
#include "../audio/audio.h"

static Mesh g_pickupMesh[8];

void pickups_init_meshes() {
    for (int t = 0; t < 8; t++) {
        MeshBuilder mb;
        memset(&mb, 0, sizeof(mb));
        mb.reset();
        switch (t) {
        case PK_HEALTH:
            mb.set_mat(MAT_PAINT, 0, 1.0f); mb.set_color(vec3(0.9f, 0.9f, 0.9f));
            mb.box(vec3(-0.2f, 0, -0.12f), vec3(0.2f, 0.25f, 0.12f));
            mb.set_mat(MAT_PAINT, MF_EMISSIVE, 1.0f, 6.0f); mb.set_color(vec3(1.0f, 0.1f, 0.1f));
            mb.box(vec3(-0.12f, 0.08f, 0.121f), vec3(0.12f, 0.17f, 0.13f));
            mb.box(vec3(-0.045f, 0.02f, 0.121f), vec3(0.045f, 0.23f, 0.13f));
            break;
        case PK_ARMOR:
            mb.set_mat(MAT_METAL_PANEL, MF_METAL, 1.0f); mb.set_color(vec3(0.2f, 0.25f, 0.35f));
            mb.box(vec3(-0.22f, 0, -0.06f), vec3(0.22f, 0.4f, 0.06f));
            break;
        case PK_MONEY:
            mb.set_mat(MAT_PAINT, 0, 1.0f); mb.set_color(vec3(0.3f, 0.6f, 0.3f));
            for (int k = 0; k < 3; k++) mb.box(vec3(-0.12f, k * 0.04f, -0.06f), vec3(0.12f, k * 0.04f + 0.035f, 0.06f));
            break;
        case PK_EVIDENCE:
            mb.set_mat(MAT_PAINT, 0, 1.0f); mb.set_color(vec3(0.85f, 0.8f, 0.65f));
            mb.box(vec3(-0.15f, 0, -0.11f), vec3(0.15f, 0.03f, 0.11f));
            mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 3.0f); mb.set_color(vec3(0.3f, 0.8f, 1.0f));
            mb.box(vec3(-0.05f, 0.03f, -0.05f), vec3(0.05f, 0.035f, 0.05f));
            break;
        case PK_CACHE:
            mb.set_mat(MAT_METAL_PANEL, MF_METAL, 1.0f); mb.set_color(vec3(0.25f, 0.28f, 0.2f));
            mb.box(vec3(-0.35f, 0, -0.22f), vec3(0.35f, 0.35f, 0.22f));
            mb.set_mat(MAT_PAINT, MF_EMISSIVE, 1.0f, 5.0f); mb.set_color(vec3(1.0f, 0.75f, 0.2f));
            mb.box(vec3(-0.36f, 0.15f, -0.05f), vec3(-0.35f, 0.2f, 0.05f));
            break;
        case PK_ITEM:
            mb.set_mat(MAT_ALIEN, MF_EMISSIVE | MF_PULSE, 0.4f, 8.0f); mb.set_color(vec3(0.4f, 0.9f, 1.0f));
            mb.ellipsoid(vec3(0, 0.2f, 0), vec3(0.08f, 0.2f, 0.08f), 8, 6);
            break;
        default:
            mb.set_mat(MAT_METAL_PANEL, MF_METAL, 1.0f); mb.set_color(vec3(0.3f, 0.3f, 0.25f));
            mb.box(vec3(-0.15f, 0, -0.1f), vec3(0.15f, 0.15f, 0.1f));
            break;
        }
        g_pickupMesh[t] = mesh_create(mb);
        mb.free_mem();
    }
}

int pickup_spawn(int type, vec3 pos, int weapon, int amount, const char* tag) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup& p = g.pickups[i];
        if (p.active) continue;
        memset(&p, 0, sizeof(p));
        p.active = true;
        p.type = (u8)type;
        p.weapon = (u8)weapon;
        p.amount = amount;
        p.pos = pos;
        p.pos.y = world_ground_height(pos + vec3(0, 0.5f, 0), 0.1f, 1.0f) + 0.05f;
        p.life = (type == PK_WEAPON || type == PK_AMMO || type == PK_MONEY) && !(tag && tag[0]) ? 60.0f : -1.0f;
        if (tag) str_copy(p.tag, sizeof(p.tag), tag);
        return i;
    }
    return -1;
}

static void collect(Pickup& p) {
    char buf[96];
    switch (p.type) {
    case PK_WEAPON:
        if (!g.pd.hasWeapon[p.weapon]) player_give_weapon(p.weapon, p.amount);
        else {
            g.pd.reserve[p.weapon] = min_t(g.pd.reserve[p.weapon] + p.amount, g_weapons[p.weapon].maxReserve);
            fmt_str(buf, sizeof(buf), "+%d %s ammo", p.amount, g_weapons[p.weapon].name);
            notify(buf);
        }
        break;
    case PK_AMMO:
        g.pd.reserve[p.weapon] = min_t(g.pd.reserve[p.weapon] + p.amount, g_weapons[p.weapon].maxReserve);
        fmt_str(buf, sizeof(buf), "+%d ammo", p.amount);
        notify(buf);
        break;
    case PK_HEALTH:
        player_heal((float)p.amount);
        notify("Health restored", vec4(0.5f, 1, 0.5f, 1));
        break;
    case PK_ARMOR:
        g.actors[PLAYER].armor = fminf(100.0f, g.actors[PLAYER].armor + p.amount);
        notify("Body armour equipped", vec4(0.6f, 0.8f, 1, 1));
        break;
    case PK_MONEY:
        add_money(p.amount);
        break;
    case PK_EVIDENCE:
        if (p.evidenceId >= 0 && p.evidenceId < MAX_EVIDENCE) {
            g.pd.evidence[p.evidenceId] = true;
            notify("Evidence collected - check the Journal (Tab)", vec4(0.4f, 0.85f, 1, 1));
        }
        break;
    case PK_CACHE: {
        int amount = p.amount > 0 ? p.amount : 250;
        add_money(amount);
        if (p.evidenceId >= 0 && p.evidenceId < 32) g.pd.cachesFound[p.evidenceId] = true;
        int w = g.rng.chance(0.5f) ? W_PISTOL : W_SMG;
        if (g.pd.hasWeapon[w]) g.pd.reserve[w] = min_t(g.pd.reserve[w] + 60, g_weapons[w].maxReserve);
        else player_give_weapon(w, 60);
        g.actors[PLAYER].armor = fminf(100.0f, g.actors[PLAYER].armor + 25.0f);
        notify("Hidden cache found!", vec4(1.0f, 0.85f, 0.3f, 1));
    } break;
    case PK_ITEM:
        break;
    }
    audio_play(p.type == PK_MONEY || p.type == PK_CACHE ? SND_MONEY : SND_PICKUP, 0.8f);
    p.active = false;
}

void pickups_update(float dt) {
    Actor& pl = g.actors[PLAYER];
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup& p = g.pickups[i];
        if (!p.active) continue;
        p.spin += dt * 1.6f;
        if (p.life > 0) {
            p.life -= dt;
            if (p.life <= 0) { p.active = false; continue; }
        }
        if (pl.state == AS_DEAD || pl.state == AS_VEHICLE) continue;
        if (p.type == PK_ITEM) continue; // mission items are taken through interaction
        if (length(pl.pos + vec3(0, 0.5f, 0) - p.pos) < 1.3f) collect(p);
    }
}

void pickups_render() {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup& p = g.pickups[i];
        if (!p.active) continue;
        if (length(p.pos - g.cam.pos) > 80.0f) continue;
        DrawItem di;
        memset(&di, 0, sizeof(di));
        int t = p.type == PK_WEAPON || p.type == PK_AMMO ? (int)PK_AMMO : (int)p.type;
        di.mesh = &g_pickupMesh[t];
        di.model = mat4_trs(p.pos + vec3(0, 0.25f + sinf(p.spin * 2.0f) * 0.06f, 0), quat_axis_angle(vec3(0, 1, 0), p.spin), vec3(1, 1, 1));
        di.tint = vec4(1, 1, 1, 1);
        renderer_submit(di);
        vec3 c = p.type == PK_HEALTH ? vec3(1, 0.2f, 0.2f) : p.type == PK_EVIDENCE || p.type == PK_ITEM ? vec3(0.3f, 0.8f, 1.0f) : p.type == PK_MONEY || p.type == PK_CACHE ? vec3(0.4f, 1.0f, 0.4f) : vec3(1.0f, 0.8f, 0.3f);
        Light l = {p.pos + vec3(0, 0.6f, 0), 3.0f, c * 1.5f, vec3(0, -1, 0), 0};
        renderer_add_light(l);
        BeamDraw b;
        b.a = p.pos;
        b.b = p.pos + vec3(0, 2.0f, 0);
        b.r0 = 0.25f;
        b.r1 = 0.05f;
        b.color = c;
        b.intensity = 0.5f;
        b.alpha = 0.6f;
        renderer_beam(b);
    }
}
