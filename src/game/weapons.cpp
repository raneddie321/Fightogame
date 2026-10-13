// FIGHTO - firearms, energy and alien weapons: hitscan, projectiles, impacts,
// explosions, muzzle flashes and weapon meshes.
#include "game.h"
#include "../audio/audio.h"

const WeaponDef g_weapons[W_COUNT] = {
    // name                 clip reserve dmg   interval reload spread aimSp  recoil pel range auto  head  price ammo alien  proj   2h    tracer
    {"Fists",                0,   0,     12,   0.0f,   0.0f,  0.0f,  0.0f,  0.0f,  0,  2,    false, 1.0f, 0,    0,   false, false, false, vec3(1, 1, 1)},
    {"Kestrel P9",           15,  150,   26,   0.17f,  1.3f,  0.035f,0.008f,0.03f, 1,  120,  false, 3.0f, 300,  40,  false, false, false, vec3(1.0f, 0.85f, 0.5f)},
    {"Breacher 12",          6,   48,    15,   0.75f,  2.4f,  0.11f, 0.07f, 0.12f, 9,  45,   false, 1.6f, 900,  60,  false, false, true,  vec3(1.0f, 0.8f, 0.4f)},
    {"Hornet SMG",           30,  300,   17,   0.075f, 1.8f,  0.06f, 0.025f,0.02f, 1,  80,   true,  2.2f, 1400, 80,  false, false, true,  vec3(1.0f, 0.85f, 0.5f)},
    {"Ravager AR",           30,  240,   32,   0.11f,  2.1f,  0.045f,0.01f, 0.035f,1,  200,  true,  2.6f, 2500, 120, false, false, true,  vec3(1.0f, 0.8f, 0.45f)},
    {"Gravedigger .50",      6,   48,    85,   0.55f,  2.2f,  0.04f, 0.004f,0.15f, 1,  150,  false, 3.0f, 1800, 90,  false, false, false, vec3(1.0f, 0.75f, 0.4f)},
    {"A.E.G.I.S. Arc Carbine",40, 400,   30,   0.12f,  1.6f,  0.03f, 0.01f, 0.02f, 1,  120,  true,  2.0f, 0,    0,   false, false, true,  vec3(0.3f, 0.85f, 1.0f)},
    {"Void Lance",           8,   64,    160,  0.9f,   2.5f,  0.01f, 0.002f,0.1f,  1,  200,  false, 1.5f, 0,    0,   true,  false, true,  vec3(0.8f, 0.4f, 1.0f)},
    {"Shard Thrower",        18,  180,   40,   0.25f,  1.9f,  0.03f, 0.01f, 0.04f, 1,  90,   false, 1.5f, 0,    0,   true,  true,  true,  vec3(0.3f, 1.0f, 0.6f)},
    {"Claws",                0,   0,     22,   0.0f,   0.0f,  0.0f,  0.0f,  0.0f,  0,  2,    false, 1.0f, 0,    0,   true,  false, false, vec3(1, 1, 1)},
    {"Plasma Emitter",       60,  9999,  12,   0.28f,  1.0f,  0.05f, 0.03f, 0.0f,  1,  80,   true,  1.0f, 0,    0,   true,  true,  false, vec3(0.3f, 0.8f, 1.0f)},
};

static Mesh g_weaponMeshes[W_COUNT];

struct TempLight { Light l; float life, maxLife; };
struct TempBeam { BeamDraw b; float life, maxLife; };
static TempLight g_tempLights[48];
static TempBeam g_tempBeams[64];

void temp_light(vec3 pos, vec3 color, float radius, float life) {
    for (int i = 0; i < ARRAY_COUNT(g_tempLights); i++) {
        if (g_tempLights[i].life > 0) continue;
        g_tempLights[i].l.pos = pos;
        g_tempLights[i].l.color = color;
        g_tempLights[i].l.radius = radius;
        g_tempLights[i].l.spotDir = vec3(0, -1, 0);
        g_tempLights[i].l.spotCos = 0;
        g_tempLights[i].life = g_tempLights[i].maxLife = life;
        return;
    }
}

void temp_beam(vec3 a, vec3 b, float r, vec3 color, float intensity, float life) {
    for (int i = 0; i < ARRAY_COUNT(g_tempBeams); i++) {
        if (g_tempBeams[i].life > 0) continue;
        BeamDraw& bd = g_tempBeams[i].b;
        bd.a = a;
        bd.b = b;
        bd.r0 = r;
        bd.r1 = r;
        bd.color = color;
        bd.intensity = intensity;
        bd.alpha = 1;
        g_tempBeams[i].life = g_tempBeams[i].maxLife = life;
        return;
    }
}

// ---------------------------------------------------------------------------
// Weapon meshes (built in gun space: +Z barrel, +Y up, grip at origin)
// ---------------------------------------------------------------------------
static void gbox(MeshBuilder& mb, vec3 mn, vec3 mx) { mb.box(mn, mx, true, true); }

void weapons_init_meshes() {
    for (int w = 0; w < W_COUNT; w++) {
        MeshBuilder mb;
        memset(&mb, 0, sizeof(mb));
        mb.reset();
        vec3 dark(0.08f, 0.08f, 0.09f), steel(0.35f, 0.36f, 0.38f);
        switch (w) {
        case W_PISTOL:
            mb.set_mat(MAT_STEEL, MF_METAL, 1.4f); mb.set_color(dark);
            gbox(mb, vec3(-0.015f, 0.02f, -0.03f), vec3(0.015f, 0.055f, 0.17f));
            mb.set_mat(MAT_RUBBER, 0, 1.0f); mb.set_color(vec3(0.12f, 0.12f, 0.12f));
            gbox(mb, vec3(-0.014f, -0.08f, -0.03f), vec3(0.014f, 0.025f, 0.02f));
            break;
        case W_MAGNUM:
            mb.set_mat(MAT_STEEL, MF_METAL, 0.8f); mb.set_color(steel);
            gbox(mb, vec3(-0.012f, 0.03f, 0.02f), vec3(0.012f, 0.055f, 0.26f));
            mb.cylinder(vec3(0, 0.0f, 0.0f), 0.025f, 0.055f, 8, true);
            mb.set_mat(MAT_WOOD, 0, 1.0f); mb.set_color(vec3(0.4f, 0.25f, 0.15f));
            gbox(mb, vec3(-0.015f, -0.09f, -0.04f), vec3(0.015f, 0.02f, 0.01f));
            break;
        case W_SHOTGUN:
            mb.set_mat(MAT_STEEL, MF_METAL, 1.2f); mb.set_color(dark);
            mb.tube(vec3(0, 0.05f, 0.0f), vec3(0, 0.05f, 0.62f), 0.018f, 0.018f, 8);
            mb.tube(vec3(0, 0.025f, 0.05f), vec3(0, 0.025f, 0.5f), 0.016f, 0.016f, 8);
            gbox(mb, vec3(-0.022f, 0.01f, -0.12f), vec3(0.022f, 0.07f, 0.08f));
            mb.set_mat(MAT_WOOD, 0, 1.0f); mb.set_color(vec3(0.35f, 0.22f, 0.12f));
            gbox(mb, vec3(-0.02f, -0.03f, -0.42f), vec3(0.02f, 0.05f, -0.12f));
            gbox(mb, vec3(-0.022f, 0.0f, 0.25f), vec3(0.022f, 0.035f, 0.4f));
            break;
        case W_SMG:
            mb.set_mat(MAT_STEEL, MF_METAL, 1.4f); mb.set_color(dark);
            gbox(mb, vec3(-0.02f, 0.01f, -0.1f), vec3(0.02f, 0.07f, 0.22f));
            mb.tube(vec3(0, 0.04f, 0.22f), vec3(0, 0.04f, 0.32f), 0.012f, 0.012f, 6);
            gbox(mb, vec3(-0.012f, -0.15f, 0.04f), vec3(0.012f, 0.01f, 0.07f));
            gbox(mb, vec3(-0.014f, -0.08f, -0.06f), vec3(0.014f, 0.01f, -0.02f));
            break;
        case W_RIFLE:
            mb.set_mat(MAT_STEEL, MF_METAL, 1.3f); mb.set_color(vec3(0.12f, 0.13f, 0.12f));
            gbox(mb, vec3(-0.022f, 0.0f, -0.08f), vec3(0.022f, 0.08f, 0.3f));
            mb.tube(vec3(0, 0.045f, 0.3f), vec3(0, 0.045f, 0.62f), 0.012f, 0.012f, 6);
            gbox(mb, vec3(-0.015f, -0.16f, 0.06f), vec3(0.015f, 0.0f, 0.12f));
            gbox(mb, vec3(-0.018f, -0.02f, -0.36f), vec3(0.018f, 0.06f, -0.08f));
            gbox(mb, vec3(-0.01f, 0.08f, 0.0f), vec3(0.01f, 0.12f, 0.18f));
            break;
        case W_ARC:
            mb.set_mat(MAT_METAL_PANEL, MF_METAL, 1.0f); mb.set_color(vec3(0.15f, 0.15f, 0.17f));
            gbox(mb, vec3(-0.03f, -0.01f, -0.25f), vec3(0.03f, 0.09f, 0.35f));
            gbox(mb, vec3(-0.015f, -0.14f, 0.0f), vec3(0.015f, 0.0f, 0.06f));
            mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 14.0f); mb.set_color(vec3(0.3f, 0.85f, 1.0f));
            for (int k = 0; k < 4; k++) mb.cylinder(vec3(0, 0.04f, 0.1f + k * 0.06f), 0.038f, 0.012f, 10, true);
            mb.set_color(vec3(0.95f, 0.7f, 0.15f));
            gbox(mb, vec3(0.031f, 0.03f, -0.2f), vec3(0.034f, 0.05f, 0.2f));
            break;
        case W_LANCE:
            mb.set_mat(MAT_ALIEN, 0, 0.6f); mb.set_color(vec3(0.5f, 0.45f, 0.55f));
            mb.ellipsoid(vec3(0, 0.04f, 0.1f), vec3(0.045f, 0.06f, 0.35f), 10, 6);
            mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 18.0f); mb.set_color(vec3(0.75f, 0.35f, 1.0f));
            mb.ellipsoid(vec3(0, 0.06f, 0.42f), vec3(0.03f, 0.03f, 0.06f), 8, 5);
            mb.tube(vec3(0, 0.1f, -0.1f), vec3(0, 0.08f, 0.35f), 0.008f, 0.008f, 5);
            break;
        case W_SHARD:
            mb.set_mat(MAT_ALIEN, 0, 0.6f); mb.set_color(vec3(0.35f, 0.4f, 0.35f));
            mb.ellipsoid(vec3(0, 0.03f, 0.08f), vec3(0.05f, 0.05f, 0.25f), 10, 6);
            mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 16.0f); mb.set_color(vec3(0.3f, 1.0f, 0.6f));
            for (int k = 0; k < 3; k++) mb.tube(vec3(0, 0.03f, 0.2f), vec3(sinf(k * 2.1f) * 0.04f, 0.03f + cosf(k * 2.1f) * 0.04f, 0.42f), 0.008f, 0.002f, 4);
            break;
        default:
            break;
        }
        if (mb.inds.count > 0) g_weaponMeshes[w] = mesh_create(mb);
        mb.free_mem();
    }
}

static mat4 weapon_matrix(const Actor& a) {
    // Gun space -> hand space: gun +X -> hand +X, gun +Y -> hand +Z, gun +Z -> hand -Y
    mat4 g2h = mat4_identity();
    g2h.m[0] = 1; g2h.m[1] = 0; g2h.m[2] = 0;
    g2h.m[4] = 0; g2h.m[5] = 0; g2h.m[6] = 1;
    g2h.m[8] = 0; g2h.m[9] = -1; g2h.m[10] = 0;
    float sc = a.model ? a.model->skel.scale : 1.0f;
    g2h.m[12] = 0; g2h.m[13] = -0.075f * sc; g2h.m[14] = 0.0f;
    mat4 hand = a.ragdoll ? a.globals[B_R_HAND] : a.world * a.globals[B_R_HAND];
    return hand * g2h;
}

void weapon_render(const Actor& a) {
    if (!a.model || a.weapon <= W_FISTS || a.weapon >= W_CLAWS) return;
    const Mesh& m = g_weaponMeshes[a.weapon];
    if (!m.indexCount) return;
    DrawItem di;
    memset(&di, 0, sizeof(di));
    di.mesh = &m;
    di.model = weapon_matrix(a);
    di.tint = vec4(1, 1, 1, 1);
    di.castShadow = true;
    renderer_submit(di);
    if (a.muzzleFlash > 0) {
        vec3 muzzle = transform_point(di.model, vec3(0, 0.05f, g_weapons[a.weapon].twoHanded ? 0.65f : 0.22f));
        Light l = {muzzle, 9.0f, g_weapons[a.weapon].tracer * 4.0f, vec3(0, -1, 0), 0};
        renderer_add_light(l);
    }
}

// ---------------------------------------------------------------------------
// Ray vs characters
// ---------------------------------------------------------------------------
struct HitSphere { int bone; vec3 off; float r; };
static const HitSphere g_hitSpheres[] = {
    {B_HEAD, vec3(0, 0.1f, 0.01f), 0.12f}, {B_NECK, vec3(0, 0.02f, 0), 0.07f}, {B_CHEST, vec3(0, 0.06f, 0.01f), 0.2f},
    {B_SPINE, vec3(0, 0.05f, 0), 0.18f}, {B_PELVIS, vec3(0, -0.04f, 0), 0.18f},
    {B_L_UPPERARM, vec3(0, -0.13f, 0), 0.075f}, {B_R_UPPERARM, vec3(0, -0.13f, 0), 0.075f},
    {B_L_FOREARM, vec3(0, -0.12f, 0), 0.06f}, {B_R_FOREARM, vec3(0, -0.12f, 0), 0.06f},
    {B_L_THIGH, vec3(0, -0.2f, 0), 0.1f}, {B_R_THIGH, vec3(0, -0.2f, 0), 0.1f},
    {B_L_SHIN, vec3(0, -0.2f, 0), 0.075f}, {B_R_SHIN, vec3(0, -0.2f, 0), 0.075f},
};

int ray_actors(vec3 o, vec3 d, float maxT, int ignore, float* tOut, int* boneOut) {
    int best = -1;
    float bestT = maxT;
    int bestBone = B_CHEST;
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor& a = g.actors[i];
        if (!a.active || i == ignore) continue;
        if (a.state == AS_VEHICLE && i != PLAYER) continue;
        if (a.state == AS_VEHICLE) continue;
        vec3 c = a.ragdoll ? actor_chest(a) : a.pos + vec3(0, a.height * 0.5f, 0);
        float broad = a.kind == AK_DRONE ? 1.0f : a.height * 0.75f + 0.4f;
        float t;
        if (!ray_sphere(o, d, c, broad, bestT, &t)) continue;
        if (a.kind == AK_DRONE || !a.model) {
            if (ray_sphere(o, d, a.pos, 0.9f, bestT, &t)) { bestT = t; best = i; bestBone = B_CHEST; }
            continue;
        }
        float sc = a.model->skel.scale * (a.app.body == BODY_BRUTE ? 1.6f : 1.0f);
        for (int k = 0; k < ARRAY_COUNT(g_hitSpheres); k++) {
            const HitSphere& hs = g_hitSpheres[k];
            vec3 p = a.ragdoll ? transform_point(a.globals[hs.bone], hs.off * sc) : joint_world(a.world, a.globals, hs.bone, hs.off * sc);
            if (ray_sphere(o, d, p, hs.r * sc, bestT, &t)) { bestT = t; best = i; bestBone = hs.bone; }
        }
    }
    if (tOut) *tOut = bestT;
    if (boneOut) *boneOut = bestBone;
    return best;
}

// ---------------------------------------------------------------------------
// Impacts
// ---------------------------------------------------------------------------
static void impact_world(vec3 pos, vec3 n, int surface, vec3 dir) {
    if (surface == SURF_METAL) {
        for (int k = 0; k < 8; k++) {
            Particle* p = fx_spawn(PT_SPARK);
            if (!p) break;
            p->pos = pos + n * 0.02f;
            p->vel = normalize(reflect_v(dir, n) + vec3(g.rng.range(-0.5f, 0.5f), g.rng.range(-0.2f, 0.6f), g.rng.range(-0.5f, 0.5f))) * g.rng.range(3, 9);
            p->gravity = 9.8f;
            p->size = 0.015f;
            p->stretch = 0.02f;
            p->life = p->maxLife = g.rng.range(0.15f, 0.4f);
            p->color = vec3(1.0f, 0.75f, 0.35f);
            p->emissive = 8.0f;
        }
        if (g.rng.chance(0.3f)) audio_play_at(SND_RICOCHET, pos, 0.5f, g.rng.range(0.8f, 1.3f));
        else audio_play_at(SND_IMPACT_METAL, pos, 0.5f);
        temp_light(pos + n * 0.2f, vec3(1.0f, 0.7f, 0.3f) * 0.8f, 3.0f, 0.06f);
    } else {
        for (int k = 0; k < 4; k++) {
            Particle* p = fx_spawn(PT_SMOKE);
            if (!p) break;
            p->pos = pos + n * 0.05f;
            p->vel = (n + vec3(g.rng.range(-0.4f, 0.4f), g.rng.range(0, 0.5f), g.rng.range(-0.4f, 0.4f))) * g.rng.range(0.5f, 2.0f);
            p->drag = 3.0f;
            p->size = 0.06f;
            p->sizeGrow = 0.5f;
            p->life = p->maxLife = g.rng.range(0.4f, 0.9f);
            p->color = surface == SURF_WOOD ? vec3(0.5f, 0.4f, 0.3f) : vec3(0.55f, 0.55f, 0.52f);
            p->alpha = 0.6f;
        }
        for (int k = 0; k < 5; k++) {
            Particle* p = fx_spawn(PT_BLOOD);
            if (!p) break;
            p->pos = pos + n * 0.03f;
            p->vel = (n + vec3(g.rng.range(-0.6f, 0.6f), g.rng.range(0, 0.8f), g.rng.range(-0.6f, 0.6f))) * g.rng.range(1.5f, 4.0f);
            p->gravity = 9.8f;
            p->size = 0.012f;
            p->life = p->maxLife = 0.6f;
            p->color = vec3(0.4f, 0.38f, 0.35f);
        }
        audio_play_at(SND_IMPACT_CONCRETE, pos, 0.4f, g.rng.range(0.9f, 1.2f));
    }
    if (surface != SURF_ALIEN) fx_decal(DT_BULLET_HOLE, pos, n, g.rng.range(0.05f, 0.08f), vec3(0.05f, 0.05f, 0.05f), g.rng.f01());
}

void explosion(vec3 pos, float radius, float damage, int owner, bool alien) {
    vec3 fire = alien ? vec3(0.4f, 1.0f, 0.7f) : vec3(1.0f, 0.55f, 0.2f);
    for (int k = 0; k < 30; k++) {
        Particle* p = fx_spawn(PT_FIRE);
        if (!p) break;
        p->pos = pos + vec3(g.rng.range(-0.5f, 0.5f), g.rng.range(0, 0.6f), g.rng.range(-0.5f, 0.5f));
        p->vel = normalize(vec3(g.rng.range(-1, 1), g.rng.range(0.2f, 1.5f), g.rng.range(-1, 1))) * g.rng.range(2, 9);
        p->drag = 2.5f;
        p->size = g.rng.range(0.4f, 1.0f) * radius * 0.3f;
        p->sizeGrow = 1.5f;
        p->life = p->maxLife = g.rng.range(0.3f, 0.8f);
        p->color = fire;
    }
    for (int k = 0; k < 18; k++) {
        Particle* p = fx_spawn(PT_SMOKE);
        if (!p) break;
        p->pos = pos + vec3(0, 0.5f, 0);
        p->vel = normalize(vec3(g.rng.range(-1, 1), g.rng.range(0.4f, 1.5f), g.rng.range(-1, 1))) * g.rng.range(1, 5);
        p->drag = 1.5f;
        p->size = radius * 0.3f;
        p->sizeGrow = radius * 0.5f;
        p->life = p->maxLife = g.rng.range(1.5f, 4.0f);
        p->color = vec3(0.12f, 0.11f, 0.1f);
        p->alpha = 0.8f;
    }
    for (int k = 0; k < 25; k++) {
        Particle* p = fx_spawn(PT_SPARK);
        if (!p) break;
        p->pos = pos;
        p->vel = normalize(vec3(g.rng.range(-1, 1), g.rng.range(0, 1.5f), g.rng.range(-1, 1))) * g.rng.range(6, 18);
        p->gravity = 9.8f;
        p->size = 0.025f;
        p->stretch = 0.03f;
        p->life = p->maxLife = g.rng.range(0.4f, 1.2f);
        p->color = fire;
        p->emissive = 10.0f;
    }
    Particle* ring = fx_spawn(PT_RING);
    if (ring) { ring->pos = pos + vec3(0, 0.3f, 0); ring->size = 0.5f; ring->sizeGrow = radius * 6.0f; ring->life = ring->maxLife = 0.3f; ring->color = fire; }
    fx_decal(DT_SCORCH, vec3(pos.x, world_particle_ground(pos) + 0.02f, pos.z), vec3(0, 1, 0), radius * 0.7f, vec3(0, 0, 0), g.rng.f01());
    temp_light(pos + vec3(0, 1.0f, 0), fire * 25.0f, radius * 5.0f, 0.4f);
    audio_play_at(SND_EXPLOSION, pos, 1.0f, g.rng.range(0.85f, 1.1f));
    float camD = length(g.cam.pos - pos);
    camera_shake(clampf(1.4f - camD / 40.0f, 0.0f, 1.0f));
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!a.active || a.state == AS_VEHICLE) continue;
        float d = length(a.pos + vec3(0, 0.9f, 0) - pos);
        if (d > radius) continue;
        float f = 1.0f - d / radius;
        vec3 dir = normalize(a.pos - pos + vec3(0, 0.5f, 0));
        combat_damage(i, owner, damage * f, a.pos + vec3(0, 1.0f, 0), dir, DMG_EXPLOSION, B_CHEST);
    }
    for (int v = 0; v < MAX_VEHICLES; v++) {
        Vehicle& ve = g.vehicles[v];
        if (!ve.active) continue;
        float d = length(ve.pos - pos);
        if (d < radius * 1.5f) vehicle_damage(v, damage * (1.0f - d / (radius * 1.5f)) * 1.5f, pos);
    }
    ai_alert_area(pos, 80.0f, owner, 2);
}

// ---------------------------------------------------------------------------
// Firing
// ---------------------------------------------------------------------------
static vec3 perturb(vec3 d, float spread) {
    if (spread <= 0) return d;
    vec3 ref = fabsf(d.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 s = normalize(cross(d, ref));
    vec3 t = cross(s, d);
    float a = g.rng.range(0, TAU_F);
    float r = spread * sqrtf(g.rng.f01());
    return normalize(d + s * (cosf(a) * r) + t * (sinf(a) * r));
}

static int g_weaponSound[W_COUNT] = {SND_WHOOSH, SND_PISTOL, SND_SHOTGUN, SND_SMG, SND_RIFLE, SND_MAGNUM, SND_ARC, SND_LANCE, SND_SHARD, SND_WHOOSH, SND_DRONE_SHOT};

static void spawn_projectile(int shooter, int w, vec3 o, vec3 d, float dmg) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile& p = g.projectiles[i];
        if (p.active) continue;
        p.active = true;
        p.weapon = (u8)w;
        p.owner = shooter;
        p.pos = o;
        p.vel = d * (w == W_SHARD ? 70.0f : 38.0f);
        p.life = 3.0f;
        p.damage = dmg;
        p.color = g_weapons[w].tracer;
        return;
    }
}

void weapons_fire(int shooter, vec3 origin, vec3 dir, bool aimed) {
    Actor& s = g.actors[shooter];
    int w = s.weapon;
    const WeaponDef& wd = g_weapons[w];
    float spread = aimed ? wd.aimSpread : wd.spread;
    if (shooter != PLAYER) spread = spread * 1.5f + (1.0f - s.accuracy) * 0.08f;
    else spread *= 1.0f - g.pd.upgrades[UP_HANDLING] * 0.15f;
    if (shooter == PLAYER && s.speed > 3.0f) spread += 0.02f;
    s.muzzleFlash = 0.06f;
    audio_play_at(g_weaponSound[w], origin, 1.0f, g.rng.range(0.95f, 1.05f));
    // Muzzle flash particle
    Particle* mf = fx_spawn(PT_MUZZLE);
    if (mf) {
        mf->pos = origin + dir * (wd.twoHanded ? 0.55f : 0.2f);
        mf->size = w == W_SHOTGUN || w == W_MAGNUM ? 0.22f : 0.14f;
        mf->life = mf->maxLife = 0.05f;
        mf->color = wd.tracer;
    }
    if (!wd.alien && w != W_ARC && fx_particle_count() < 5000) {
        Particle* sh = fx_spawn(PT_BLOOD);
        if (sh) {
            vec3 right(-cosf(s.yaw), 0, sinf(s.yaw));
            sh->pos = origin;
            sh->vel = right * -2.5f + vec3(0, 2.0f, 0);
            sh->gravity = 9.8f;
            sh->size = 0.012f;
            sh->life = sh->maxLife = 1.0f;
            sh->color = vec3(0.85f, 0.65f, 0.25f);
        }
    }
    // Wanted level / alerts
    if (shooter == PLAYER) {
        ai_alert_area(origin, 60.0f, PLAYER, 2);
        if (!g.inMission || g.pd.phase == 0) wanted_crime(CRIME_SHOOTING, origin, false);
    } else {
        ai_alert_area(origin, 40.0f, shooter, 2);
    }
    if (wd.projectile) {
        spawn_projectile(shooter, w, origin + dir * 0.4f, perturb(dir, spread), wd.damage);
        return;
    }
    for (int pel = 0; pel < wd.pellets; pel++) {
        vec3 d = perturb(dir, spread);
        float maxT = wd.range;
        RayHit wh;
        bool hitWorld = world_raycast(origin, d, maxT, &wh, false);
        float worldT = hitWorld ? wh.t : maxT;
        float at;
        int bone;
        int victim = ray_actors(origin, d, worldT, shooter, &at, &bone);
        vec3 end;
        if (victim >= 0) {
            end = origin + d * at;
            float falloff = clampf(1.2f - at / wd.range, 0.35f, 1.0f);
            bool head = bone == B_HEAD || bone == B_NECK;
            float dmg = wd.damage * falloff * (head ? wd.headMult : 1.0f);
            if (shooter != PLAYER && victim == PLAYER) dmg *= g.settings.difficulty == 0 ? 0.45f : g.settings.difficulty == 2 ? 1.0f : 0.7f;
            combat_damage(victim, shooter, dmg, end, d, head ? DMG_HEADSHOT : (w == W_ARC ? DMG_ENERGY : DMG_BULLET), bone);
            if (shooter == PLAYER) { g.hitMarker = 0.15f; g.pd.shotsHit++; }
            // Arc chains to nearby hostiles
            if (w == W_ARC) {
                vec3 from = end;
                int chained = 0;
                for (int i = 1; i < MAX_ACTORS && chained < 2; i++) {
                    if (i == victim || i == shooter) continue;
                    Actor& t = g.actors[i];
                    if (!actor_alive(t) || !actor_is_enemy(s, t)) continue;
                    if (length(t.pos - from) > 6.0f) continue;
                    vec3 tp = actor_chest(t);
                    temp_beam(from, tp, 0.08f, vec3(0.4f, 0.9f, 1.0f), 3.0f, 0.12f);
                    combat_damage(i, shooter, wd.damage * 0.6f, tp, normalize(tp - from), DMG_ENERGY, B_CHEST);
                    from = tp;
                    chained++;
                }
            }
        } else {
            end = origin + d * worldT;
            if (hitWorld) impact_world(wh.pos, wh.normal, wh.surface, d);
        }
        // Tracers / beams
        if (w == W_LANCE) {
            temp_beam(origin, end, 0.12f, vec3(0.8f, 0.4f, 1.0f), 4.0f, 0.25f);
            explosion(end, 3.2f, wd.damage * 0.6f, shooter, true);
        } else if (w == W_ARC) {
            temp_beam(origin, end, 0.05f, vec3(0.35f, 0.85f, 1.0f), 3.0f, 0.07f);
            temp_light(end, vec3(0.3f, 0.8f, 1.0f) * 3.0f, 4.0f, 0.08f);
        } else if (g.rng.chance(pel == 0 ? 0.8f : 0.25f)) {
            Particle* t = fx_spawn(PT_STREAK);
            if (t) {
                t->pos = end;
                t->vel = d * 400.0f;
                t->stretch = fminf(length(end - origin), 30.0f) / 400.0f;
                t->size = 0.012f;
                t->life = t->maxLife = 0.04f;
                t->color = wd.tracer;
                t->emissive = 6.0f;
            }
        }
    }
}

bool weapons_try_reload(int idx) {
    Actor& a = g.actors[idx];
    const WeaponDef& wd = g_weapons[a.weapon];
    if (wd.clipSize <= 0 || a.reloadTimer > 0 || a.clipAmmo >= wd.clipSize) return false;
    if (idx == PLAYER && g.pd.reserve[a.weapon] <= 0) {
        notify("Out of ammo", vec4(1, 0.5f, 0.3f, 1));
        return false;
    }
    a.reloadTimer = wd.reloadTime;
    actor_set_clip(a, CLIP_RELOAD, 0.1f, 1.2f / wd.reloadTime);
    audio_play_at(SND_RELOAD, a.pos, 0.7f);
    if (idx != PLAYER) {
        a.clipAmmo = wd.clipSize;  // NPCs have unlimited reserve
    }
    return true;
}

void projectiles_update(float dt) {
    for (int i = 0; i < ARRAY_COUNT(g_tempLights); i++) g_tempLights[i].life -= dt;
    for (int i = 0; i < ARRAY_COUNT(g_tempBeams); i++) g_tempBeams[i].life -= dt;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile& p = g.projectiles[i];
        if (!p.active) continue;
        p.life -= dt;
        if (p.life <= 0) { p.active = false; continue; }
        vec3 d = p.vel * dt;
        float len = length(d);
        vec3 dir = d / fmaxf(len, 1e-5f);
        RayHit h;
        bool hw = world_raycast(p.pos, dir, len, &h, false);
        float t;
        int bone;
        int victim = ray_actors(p.pos, dir, hw ? h.t : len, p.owner, &t, &bone);
        if (victim >= 0) {
            vec3 hp = p.pos + dir * t;
            combat_damage(victim, p.owner, p.damage, hp, dir, DMG_ENERGY, bone);
            if (p.weapon == W_SHARD) explosion(hp, 2.5f, p.damage * 0.8f, p.owner, true);
            p.active = false;
            continue;
        }
        if (hw) {
            if (p.weapon == W_SHARD) explosion(h.pos + h.normal * 0.2f, 2.5f, p.damage * 0.8f, p.owner, true);
            else impact_world(h.pos, h.normal, SURF_METAL, dir);
            p.active = false;
            continue;
        }
        p.pos += d;
    }
}

void projectiles_render() {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile& p = g.projectiles[i];
        if (!p.active) continue;
        BeamDraw b;
        b.a = p.pos;
        b.b = p.pos - normalize(p.vel) * 1.2f;
        b.r0 = 0.07f;
        b.r1 = 0.02f;
        b.color = p.color;
        b.intensity = 5.0f;
        b.alpha = 1.0f;
        renderer_beam(b);
        Light l = {p.pos, 5.0f, p.color * 3.0f, vec3(0, -1, 0), 0};
        renderer_add_light(l);
    }
    for (int i = 0; i < ARRAY_COUNT(g_tempLights); i++) {
        TempLight& t = g_tempLights[i];
        if (t.life <= 0) continue;
        Light l = t.l;
        l.color = l.color * (t.life / t.maxLife);
        renderer_add_light(l);
    }
    for (int i = 0; i < ARRAY_COUNT(g_tempBeams); i++) {
        TempBeam& t = g_tempBeams[i];
        if (t.life <= 0) continue;
        BeamDraw b = t.b;
        b.alpha = t.life / t.maxLife;
        renderer_beam(b);
    }
}
