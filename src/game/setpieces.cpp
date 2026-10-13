// FIGHTO - set pieces implementation.
#include "game.h"
#include "setpieces.h"
#include "../audio/audio.h"

static Mesh g_globe, g_globeGrid, g_manta, g_mother;
static bool g_invasion;
static bool g_motherOn;
static float g_ventTimer;

struct Ship {
    float angle, radius, height, speed;
    vec3 pos;
    float beamPhase;
};
static Ship g_ships[7];

static void build_meshes() {
    MeshBuilder mb;
    // Holographic globe: dark core, glowing continents and a lat/long grid
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 0.2f, 3.0f);
    mb.set_color(vec3(0.15f, 0.45f, 0.9f));
    mb.ellipsoid(vec3(0, 0, 0), vec3(2.35f, 2.35f, 2.35f), 32, 20);
    Rng r;
    r.seed(99);
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 0.2f, 9.0f);
    for (int k = 0; k < 26; k++) {
        float lat = r.range(-1.1f, 1.1f), lon = r.range(0, TAU_F);
        vec3 d(cosf(lat) * sinf(lon), sinf(lat), cosf(lat) * cosf(lon));
        mb.set_color(r.chance(0.5f) ? vec3(0.55f, 0.95f, 0.75f) : vec3(0.45f, 0.85f, 0.6f));
        float s = r.range(0.25f, 0.7f);
        mb.ellipsoid(d * 2.36f, vec3(s, s * 0.6f, s), 8, 5);
    }
    g_globe = mesh_create(mb);
    mb.free_mem();
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 0.2f, 12.0f);
    mb.set_color(vec3(0.5f, 0.85f, 1.0f));
    for (int i = 0; i < 12; i++) {
        float lon = TAU_F * i / 12.0f;
        for (int k = 0; k < 16; k++) {
            float a0 = -PI_F * 0.5f + PI_F * k / 16.0f, a1 = -PI_F * 0.5f + PI_F * (k + 1) / 16.0f;
            vec3 p0(cosf(a0) * sinf(lon), sinf(a0), cosf(a0) * cosf(lon)), p1(cosf(a1) * sinf(lon), sinf(a1), cosf(a1) * cosf(lon));
            mb.tube(p0 * 2.42f, p1 * 2.42f, 0.012f, 0.012f, 3, false, false);
        }
    }
    for (int j = 1; j < 8; j++) {
        float lat = -PI_F * 0.5f + PI_F * j / 8.0f;
        for (int k = 0; k < 24; k++) {
            float l0 = TAU_F * k / 24.0f, l1 = TAU_F * (k + 1) / 24.0f;
            vec3 p0(cosf(lat) * sinf(l0), sinf(lat), cosf(lat) * cosf(l0)), p1(cosf(lat) * sinf(l1), sinf(lat), cosf(lat) * cosf(l1));
            mb.tube(p0 * 2.42f, p1 * 2.42f, 0.012f, 0.012f, 3, false, false);
        }
    }
    g_globeGrid = mesh_create(mb);
    mb.free_mem();
    // Manta-ray warship (~60 m wingspan)
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    mb.set_mat(MAT_ALIEN, MF_METAL, 0.7f);
    mb.set_color(vec3(0.3f, 0.28f, 0.32f));
    mb.ellipsoid(vec3(0, 0, 0), vec3(7, 3.5f, 22), 16, 10);
    for (int s = -1; s <= 1; s += 2) {
        float x = (float)s;
        vec3 a(4 * x, 0.5f, 14), b(34 * x, -2.0f, -10), c(26 * x, 0.5f, -16), d(6 * x, 1.0f, -18);
        if (s > 0) { mb.quad_auto(a, b, c, d); mb.quad_auto(d, c, b, a); }
        else { mb.quad_auto(d, c, b, a); mb.quad_auto(a, b, c, d); }
        mb.tube(vec3(30 * x, -1.5f, -12), vec3(38 * x, -1.0f, -30), 1.2f, 0.1f, 6);
        mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 25.0f);
        mb.set_color(vec3(0.3f, 0.75f, 1.0f));
        mb.ellipsoid(vec3(12 * x, -1.6f, -2), vec3(1.6f, 0.6f, 1.6f), 8, 5);
        mb.ellipsoid(vec3(22 * x, -1.8f, -8), vec3(1.2f, 0.5f, 1.2f), 8, 5);
        mb.set_mat(MAT_ALIEN, MF_METAL, 0.7f);
        mb.set_color(vec3(0.3f, 0.28f, 0.32f));
    }
    mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 30.0f);
    mb.set_color(vec3(0.4f, 0.85f, 1.0f));
    mb.ellipsoid(vec3(0, -3.3f, 6), vec3(2.2f, 0.8f, 2.2f), 10, 6);
    mb.ellipsoid(vec3(0, -2.8f, -8), vec3(1.6f, 0.6f, 1.6f), 10, 6);
    g_manta = mesh_create(mb);
    mb.free_mem();
    // Mothership: vast dark disc with a burning ring
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    mb.set_mat(MAT_ALIEN, MF_METAL, 0.8f);
    mb.set_color(vec3(0.22f, 0.2f, 0.24f));
    mb.ellipsoid(vec3(0, 0, 0), vec3(320, 40, 320), 48, 16);
    mb.ellipsoid(vec3(0, -30, 0), vec3(120, 30, 120), 32, 12);
    mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 60.0f);
    mb.set_color(vec3(1.0f, 0.35f, 0.15f));
    for (int k = 0; k < 36; k++) {
        float a = TAU_F * k / 36.0f;
        mb.ellipsoid(vec3(sinf(a) * 250, -22, cosf(a) * 250), vec3(9, 4, 9), 8, 4);
    }
    mb.set_color(vec3(0.35f, 0.8f, 1.0f));
    mb.ellipsoid(vec3(0, -58, 0), vec3(30, 6, 30), 24, 8);
    g_mother = mesh_create(mb);
    mb.free_mem();
}

void setpieces_init() {
    build_meshes();
    for (int i = 0; i < ARRAY_COUNT(g_ships); i++) {
        g_ships[i].angle = TAU_F * i / ARRAY_COUNT(g_ships);
        g_ships[i].radius = 140.0f + i * 35.0f;
        g_ships[i].height = 110.0f + (i % 3) * 30.0f;
        g_ships[i].speed = 0.025f + (i % 2) * 0.012f;
        g_ships[i].beamPhase = i * 1.3f;
    }
}

void setpieces_invasion(bool on) { g_invasion = on; g_motherOn = on; }
void setpieces_mothership(bool on) { g_motherOn = on; }

void setpieces_update(float dt) {
    for (int i = 0; i < ARRAY_COUNT(g_ships); i++) {
        Ship& s = g_ships[i];
        s.angle += s.speed * dt * (i % 2 ? 1.0f : -1.0f);
        s.beamPhase += dt;
        vec3 c = landmark_pos(LM_PLAZA);
        s.pos = c + vec3(sinf(s.angle) * s.radius, s.height, cosf(s.angle) * s.radius);
    }
    // Ambient particles near the camera: steam vents and burning barrels
    g_ventTimer -= dt;
    if (g_ventTimer <= 0) {
        g_ventTimer = 0.08f;
        vec3 cam = g.cam.pos;
        for (int i = 0; i < g_world.steamVents.count; i++) {
            vec3 v = g_world.steamVents[i];
            if (length(v - cam) > 70.0f) continue;
            Particle* p = fx_spawn(PT_SMOKE);
            if (!p) break;
            p->pos = v + vec3(g.rng.range(-0.2f, 0.2f), 0.1f, g.rng.range(-0.2f, 0.2f));
            p->vel = vec3(g.rng.range(-0.2f, 0.2f), g.rng.range(1.0f, 2.0f), g.rng.range(-0.2f, 0.2f));
            p->size = 0.3f;
            p->sizeGrow = 0.9f;
            p->life = p->maxLife = g.rng.range(2.0f, 3.5f);
            p->color = vec3(0.75f, 0.75f, 0.78f);
            p->alpha = 0.35f;
            p->drag = 0.4f;
        }
        for (int i = 0; i < g_world.firePoints.count; i++) {
            vec3 v = g_world.firePoints[i];
            if (length(v - cam) > 60.0f) continue;
            for (int k = 0; k < 2; k++) {
                Particle* p = fx_spawn(PT_FIRE);
                if (!p) break;
                p->pos = v + vec3(g.rng.range(-0.2f, 0.2f), 0, g.rng.range(-0.2f, 0.2f));
                p->vel = vec3(g.rng.range(-0.2f, 0.2f), g.rng.range(1.0f, 2.0f), g.rng.range(-0.2f, 0.2f));
                p->size = 0.25f;
                p->sizeGrow = -0.1f;
                p->life = p->maxLife = g.rng.range(0.4f, 0.8f);
                p->color = vec3(1.0f, 0.5f, 0.15f);
            }
        }
        // Invasion fires burning in the streets
        if (g_invasion) {
            for (int k = 0; k < 3; k++) {
                vec3 f = landmark_pos(LM_PLAZA) + vec3(sinf(k * 2.1f) * 40.0f, 0.2f, cosf(k * 2.1f) * 40.0f);
                if (length(f - cam) > 120.0f) continue;
                Particle* p = fx_spawn(PT_FIRE);
                if (!p) break;
                p->pos = f + vec3(g.rng.range(-1, 1), 0, g.rng.range(-1, 1));
                p->vel = vec3(0, g.rng.range(2, 4), 0);
                p->size = 0.8f; p->sizeGrow = 0.5f; p->life = p->maxLife = 1.0f; p->color = vec3(1, 0.5f, 0.2f);
            }
        }
    }
}

void setpieces_render() {
    DrawItem di;
    memset(&di, 0, sizeof(di));
    di.tint = vec4(1, 1, 1, 1);
    // Holo globe in the briefing room
    vec3 gc(AEGIS_ORIGIN_X + 21.0f, AEGIS_ORIGIN_Y + 3.9f, AEGIS_ORIGIN_Z + 35.0f);
    if (length(gc - g.cam.pos) < 60.0f) {
        di.mesh = &g_globe;
        di.model = mat4_trs(gc, quat_axis_angle(vec3(0, 1, 0), g.time * 0.25f) * quat_axis_angle(vec3(1, 0, 0), 0.4f), vec3(1, 1, 1));
        renderer_submit(di);
        di.mesh = &g_globeGrid;
        renderer_submit(di);
        BeamDraw b = {gc - vec3(0, 2.7f, 0), gc + vec3(0, 2.4f, 0), 4.0f, 2.6f, vec3(0.3f, 0.7f, 1.0f), 0.6f, 0.8f};
        renderer_beam(b);
    }
    if (!g_invasion && !g_motherOn) return;
    for (int i = 0; i < ARRAY_COUNT(g_ships) && g_invasion; i++) {
        Ship& s = g_ships[i];
        float heading = s.angle + (i % 2 ? PI_F * 0.5f : -PI_F * 0.5f);
        di.mesh = &g_manta;
        di.model = mat4_trs(s.pos, quat_axis_angle(vec3(0, 1, 0), heading) * quat_axis_angle(vec3(0, 0, 1), 0.08f * sinf(g.time + i)), vec3(1, 1, 1));
        di.castShadow = false;
        renderer_submit(di);
        // Sweeping search beams
        for (int k = 0; k < 2; k++) {
            float ph = s.beamPhase * 0.6f + k * 2.0f;
            vec3 ground = s.pos + vec3(sinf(ph) * 30.0f, -s.height, cosf(ph * 0.8f) * 30.0f);
            ground.y = 0.2f;
            BeamDraw b = {s.pos + vec3(0, -3.0f, 0), ground, 1.5f, 9.0f, vec3(0.45f, 0.75f, 1.0f), 0.55f, 1.0f};
            renderer_beam(b);
            if (length(ground - g.cam.pos) < 200.0f) {
                Light l = {ground + vec3(0, 4.0f, 0), 22.0f, vec3(0.4f, 0.7f, 1.0f) * 12.0f, vec3(0, -1, 0), 0};
                renderer_add_light(l);
            }
        }
    }
    if (g_motherOn) {
        di.mesh = &g_mother;
        di.model = mat4_trs(landmark_pos(LM_PLAZA) + vec3(0, 520.0f, 0), quat_axis_angle(vec3(0, 1, 0), g.time * 0.01f), vec3(1, 1, 1));
        di.castShadow = false;
        renderer_submit(di);
    }
}
