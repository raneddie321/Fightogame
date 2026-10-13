// FIGHTO - vehicles: procedural car models, driving physics, collisions,
// damage, traffic AI with signalised intersections and police pursuit.
#include "game.h"
#include "../audio/audio.h"

const VehicleDef g_vehicleDefs[VT_COUNT] = {
    // name            half extents (w, h, l)       vmax  accel brake grip steer mass  hp    price
    {"Vesta Sedan",     vec3(0.92f, 1.45f, 2.3f),    42.0f, 9.0f, 18.0f, 6.0f, 1.0f, 1.0f, 900, 0},
    {"Ronin Muscle",    vec3(0.95f, 1.3f, 2.45f),    52.0f, 12.5f, 17.0f, 5.0f, 1.0f, 1.1f, 950, 0},
    {"Atlas SUV",       vec3(1.0f, 1.8f, 2.4f),      38.0f, 8.0f, 16.0f, 5.5f, 0.85f, 1.5f, 1300, 0},
    {"Vanta GT",        vec3(0.95f, 1.2f, 2.25f),    62.0f, 15.0f, 20.0f, 7.0f, 1.1f, 0.9f, 800, 0},
    {"Hauler Van",      vec3(1.0f, 2.2f, 2.7f),      32.0f, 6.5f, 14.0f, 5.0f, 0.75f, 1.8f, 1400, 0},
    {"BRPD Interceptor",vec3(0.93f, 1.5f, 2.35f),    50.0f, 12.0f, 19.0f, 6.5f, 1.05f, 1.2f, 1400, 0},
    {"A.E.G.I.S. Vanguard",vec3(1.02f, 1.85f, 2.5f), 48.0f, 11.0f, 19.0f, 6.0f, 0.95f, 1.8f, 2500, 0},
    {"Metro Cab",       vec3(0.92f, 1.6f, 2.3f),     40.0f, 9.0f, 18.0f, 6.0f, 1.0f, 1.0f, 900, 0},
};

static Mesh g_bodyMesh[VT_COUNT];
static Mesh g_wheelMesh;
static Mesh g_lightBarMesh;

// ---------------------------------------------------------------------------
// Procedural car meshes
// ---------------------------------------------------------------------------
struct Station { float z, bottom, belt, top, w, wt; };

static void loft_car(MeshBuilder& mb, const Station* st, int n, bool tintable, vec3 paint) {
    for (int i = 0; i < n - 1; i++) {
        const Station& a = st[i];
        const Station& b = st[i + 1];
        bool cabin = (a.top - a.belt > 0.2f) && (b.top - b.belt > 0.2f);
        bool windscreen = !cabin && (fabsf((a.top - a.belt) - (b.top - b.belt)) > 0.2f);
        for (int side = -1; side <= 1; side += 2) {
            float s = (float)side;
            // Lower body
            vec3 p0(s * a.w, a.bottom, a.z), p1(s * b.w, b.bottom, b.z), p2(s * b.w, b.belt, b.z), p3(s * a.w, a.belt, a.z);
            if (tintable) mb.set_tintable(); else mb.set_color(paint);
            mb.set_mat(MAT_CARPAINT, MF_METAL, 1.0f);
            if (side > 0) mb.quad_auto(p0, p1, p2, p3); else mb.quad_auto(p1, p0, p3, p2);
            // Upper (glass in the cabin)
            vec3 q0(s * a.w, a.belt, a.z), q1(s * b.w, b.belt, b.z), q2(s * b.wt, b.top, b.z), q3(s * a.wt, a.top, a.z);
            if (cabin) { mb.set_mat(MAT_PAINT, MF_GLASS, 0.1f); mb.set_color(vec3(0.08f, 0.09f, 0.1f)); }
            if (side > 0) mb.quad_auto(q0, q1, q2, q3); else mb.quad_auto(q1, q0, q3, q2);
        }
        // Top surface (roof / hood / trunk / windscreen)
        vec3 t0(-a.wt, a.top, a.z), t1(a.wt, a.top, a.z), t2(b.wt, b.top, b.z), t3(-b.wt, b.top, b.z);
        if (windscreen) { mb.set_mat(MAT_PAINT, MF_GLASS, 0.1f); mb.set_color(vec3(0.08f, 0.09f, 0.1f)); }
        else { mb.set_mat(MAT_CARPAINT, MF_METAL, 1.0f); if (tintable) mb.set_tintable(); else mb.set_color(paint); }
        mb.quad_auto(t1, t0, t3, t2);
        // Underside
        mb.set_mat(MAT_RUBBER, 0, 1.0f);
        mb.set_color(vec3(0.05f, 0.05f, 0.05f));
        mb.quad_auto(vec3(-a.w, a.bottom, a.z), vec3(a.w, a.bottom, a.z), vec3(b.w, b.bottom, b.z), vec3(-b.w, b.bottom, b.z));
    }
    // End caps
    for (int e = 0; e < 2; e++) {
        const Station& s = e == 0 ? st[0] : st[n - 1];
        mb.set_mat(MAT_CARPAINT, MF_METAL, 1.0f);
        if (tintable) mb.set_tintable(); else mb.set_color(paint);
        vec3 a(-s.w, s.bottom, s.z), b(s.w, s.bottom, s.z), c(s.wt, s.top, s.z), d(-s.wt, s.top, s.z);
        vec3 m1(s.w, s.belt, s.z), m0(-s.w, s.belt, s.z);
        if (e == 0) { mb.quad_auto(b, a, m0, m1); mb.quad_auto(m1, m0, d, c); }
        else { mb.quad_auto(a, b, m1, m0); mb.quad_auto(m0, m1, c, d); }
    }
}

static void car_details(MeshBuilder& mb, const Station* st, int n, int type) {
    float zf = st[n - 1].z, zr = st[0].z;
    float w = st[n - 1].w;
    // Bumpers & grille
    mb.set_mat(MAT_RUBBER, 0, 1.0f);
    mb.set_color(vec3(0.07f, 0.07f, 0.075f));
    mb.box(vec3(-w - 0.02f, 0.3f, zf - 0.08f), vec3(w + 0.02f, 0.5f, zf + 0.08f));
    mb.box(vec3(-st[0].w - 0.02f, 0.3f, zr - 0.08f), vec3(st[0].w + 0.02f, 0.5f, zr + 0.08f));
    mb.box(vec3(-w * 0.55f, 0.5f, zf - 0.02f), vec3(w * 0.55f, 0.66f, zf + 0.03f));
    // Wheel arches (dark)
    float wz[2] = {zf - 0.75f, zr + 0.75f};
    for (int k = 0; k < 2; k++)
        for (int s = -1; s <= 1; s += 2) {
            mb.box(vec3(s > 0 ? w - 0.02f : -w - 0.03f, 0.3f, wz[k] - 0.42f), vec3(s > 0 ? w + 0.03f : -w + 0.02f, 0.72f, wz[k] + 0.42f));
        }
    // Head lights & tail lights
    float hy = st[n - 1].belt - 0.12f;
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 0.2f, 12.0f);
    mb.set_color(vec3(1.0f, 0.95f, 0.85f));
    mb.box(vec3(w - 0.38f, hy - 0.06f, zf - 0.02f), vec3(w - 0.08f, hy + 0.06f, zf + 0.04f));
    mb.box(vec3(-w + 0.08f, hy - 0.06f, zf - 0.02f), vec3(-w + 0.38f, hy + 0.06f, zf + 0.04f));
    float ty = st[0].belt - 0.12f;
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 0.2f, 6.0f);
    mb.set_color(vec3(1.0f, 0.05f, 0.03f));
    mb.box(vec3(st[0].w - 0.4f, ty - 0.06f, zr - 0.04f), vec3(st[0].w - 0.05f, ty + 0.06f, zr + 0.02f));
    mb.box(vec3(-st[0].w + 0.05f, ty - 0.06f, zr - 0.04f), vec3(-st[0].w + 0.4f, ty + 0.06f, zr + 0.02f));
    // Mirrors
    mb.set_mat(MAT_RUBBER, 0, 1.0f);
    mb.set_color(vec3(0.06f, 0.06f, 0.06f));
    float mz = 0.85f;
    for (int s = -1; s <= 1; s += 2) mb.box(vec3(s * (w + 0.0f) - 0.06f, 1.0f, mz - 0.06f), vec3(s * (w + 0.16f) + 0.06f * (s < 0), 1.12f, mz + 0.06f));
    // License plates
    mb.set_mat(MAT_PAINT, 0, 1.0f);
    mb.set_color(vec3(0.85f, 0.85f, 0.8f));
    mb.box(vec3(-0.25f, 0.36f, zr - 0.11f), vec3(0.25f, 0.48f, zr - 0.09f));
    if (type == VT_MUSCLE || type == VT_SPORTS) {
        mb.set_mat(MAT_CARPAINT, MF_METAL, 1.0f);
        mb.set_tintable();
        mb.box(vec3(-st[0].w + 0.1f, st[1].top + 0.18f, zr + 0.05f), vec3(st[0].w - 0.1f, st[1].top + 0.24f, zr + 0.35f));
        mb.box(vec3(-0.5f, st[1].top, zr + 0.15f), vec3(-0.45f, st[1].top + 0.2f, zr + 0.25f));
        mb.box(vec3(0.45f, st[1].top, zr + 0.15f), vec3(0.5f, st[1].top + 0.2f, zr + 0.25f));
    }
    if (type == VT_TAXI) {
        mb.set_mat(MAT_PAINT, MF_EMISSIVE, 1.0f, 8.0f);
        mb.set_color(vec3(1.0f, 0.85f, 0.4f));
        mb.box(vec3(-0.3f, 1.46f, -0.15f), vec3(0.3f, 1.62f, 0.15f));
    }
    if (type == VT_AEGIS) {
        mb.set_mat(MAT_PAINT, MF_EMISSIVE, 1.0f, 6.0f);
        mb.set_color(vec3(0.95f, 0.7f, 0.15f));
        for (int s = -1; s <= 1; s += 2) mb.box(vec3(s * (w + 0.005f) - 0.01f, 0.8f, zr + 0.3f), vec3(s * (w + 0.005f) + 0.01f, 0.85f, zf - 0.3f));
    }
}

static void build_vehicle_meshes() {
    for (int t = 0; t < VT_COUNT; t++) {
        MeshBuilder mb;
        memset(&mb, 0, sizeof(mb));
        mb.reset();
        Station st[10];
        int n = 0;
        float L = g_vehicleDefs[t].halfExtents.z, W = g_vehicleDefs[t].halfExtents.x;
        float roofY = g_vehicleDefs[t].halfExtents.y;
        float belt = (t == VT_SUV || t == VT_AEGIS) ? 1.08f : (t == VT_VAN ? 1.1f : (t == VT_SPORTS ? 0.82f : 0.95f));
        float bot = t == VT_SPORTS ? 0.25f : 0.33f;
        float wt = W - 0.18f;
        if (t == VT_VAN) {
            st[n++] = {-L, bot, belt, belt + 0.05f, W, W - 0.05f};
            st[n++] = {-L + 0.05f, bot, belt, roofY, W, W - 0.05f};
            st[n++] = {L - 1.1f, bot, belt, roofY, W, W - 0.05f};
            st[n++] = {L - 0.5f, bot, belt, belt + 0.25f, W, W - 0.08f};
            st[n++] = {L, bot, belt - 0.1f, belt, W, W - 0.08f};
        } else {
            float trunkY = belt + (t == VT_SUV || t == VT_AEGIS ? 0.05f : 0.02f);
            bool hatch = t == VT_SUV || t == VT_AEGIS;
            st[n++] = {-L, bot, belt - 0.2f, belt - 0.15f, W - 0.05f, W - 0.1f};
            st[n++] = {-L + 0.3f, bot, belt, trunkY, W, W - 0.06f};
            if (hatch) {
                st[n++] = {-L + 0.45f, bot, belt, roofY - 0.05f, W, wt};
            } else {
                st[n++] = {-L + (t == VT_MUSCLE ? 1.0f : 0.85f), bot, belt, trunkY + 0.02f, W, W - 0.06f};
                st[n++] = {-L + (t == VT_SPORTS ? 1.3f : 1.35f), bot, belt, roofY, W, wt};
            }
            st[n++] = {t == VT_SPORTS ? 0.1f : 0.25f, bot, belt, roofY + 0.02f, W, wt};
            st[n++] = {t == VT_MUSCLE ? 0.75f : 0.85f, bot, belt, belt + 0.03f, W, W - 0.06f};
            st[n++] = {L - 0.35f, bot, belt - 0.06f, belt - 0.04f, W, W - 0.08f};
            st[n++] = {L, bot + 0.05f, belt - 0.25f, belt - 0.2f, W - 0.05f, W - 0.12f};
        }
        bool tint = t != VT_POLICE && t != VT_TAXI && t != VT_AEGIS;
        vec3 paint = t == VT_TAXI ? vec3(0.95f, 0.75f, 0.1f) : t == VT_AEGIS ? vec3(0.08f, 0.08f, 0.09f) : vec3(0.08f, 0.08f, 0.1f);
        loft_car(mb, st, n, tint, paint);
        if (t == VT_POLICE) {
            // Two-tone: white doors panel
            mb.set_mat(MAT_CARPAINT, MF_METAL, 1.0f);
            mb.set_color(vec3(0.9f, 0.9f, 0.92f));
            for (int s = -1; s <= 1; s += 2) mb.box(vec3(s * W - 0.012f, 0.45f, -0.9f), vec3(s * W + 0.012f, belt - 0.05f, 0.9f));
        }
        car_details(mb, st, n, t);
        g_bodyMesh[t] = mesh_create(mb);
        mb.free_mem();
    }
    // Wheel (axis along X)
    MeshBuilder mb;
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    mb.set_mat(MAT_RUBBER, 0, 1.0f);
    mb.set_color(vec3(0.6f, 0.6f, 0.6f));
    int s0 = mb.vcount();
    mb.cylinder(vec3(0, -0.13f, 0), 0.36f, 0.26f, 14, false);
    mb.set_mat(MAT_STEEL, MF_METAL, 0.6f);
    mb.set_color(vec3(0.7f, 0.7f, 0.72f));
    mb.cylinder(vec3(0, -0.135f, 0), 0.22f, 0.27f, 10, true);
    mb.transform(s0, mat4_from_quat(quat_axis_angle(vec3(0, 0, 1), PI_F * 0.5f)));
    g_wheelMesh = mesh_create(mb);
    mb.free_mem();
    // Police light bar
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    mb.set_mat(MAT_PAINT, MF_EMISSIVE, 1.0f, 3.0f);
    mb.set_color(vec3(1.0f, 0.1f, 0.1f));
    mb.box(vec3(-0.6f, 0, -0.12f), vec3(-0.02f, 0.12f, 0.12f));
    mb.set_color(vec3(0.15f, 0.3f, 1.0f));
    mb.box(vec3(0.02f, 0, -0.12f), vec3(0.6f, 0.12f, 0.12f));
    g_lightBarMesh = mesh_create(mb);
    mb.free_mem();
}

void vehicles_init_meshes() { build_vehicle_meshes(); }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
vec3 vehicle_forward(const Vehicle& v) { return vec3(sinf(v.yaw), 0, cosf(v.yaw)); }
static vec3 vehicle_left(const Vehicle& v) { return vec3(cosf(v.yaw), 0, -sinf(v.yaw)); }

vec3 vehicle_seat_pos(const Vehicle& v, int seat) {
    vec3 l = vehicle_left(v);
    vec3 f = vehicle_forward(v);
    float side = seat == 0 ? 0.38f : -0.38f;
    return v.pos + l * side - f * 0.15f + vec3(0, 0.05f, 0);
}

vec3 vehicle_exit_pos(const Vehicle& v, bool* ok) {
    vec3 l = vehicle_left(v);
    float w = g_vehicleDefs[v.type].halfExtents.x;
    vec3 cands[4] = {v.pos + l * (w + 0.7f), v.pos - l * (w + 0.7f), v.pos + vehicle_forward(v) * (g_vehicleDefs[v.type].halfExtents.z + 0.8f),
                     v.pos - vehicle_forward(v) * (g_vehicleDefs[v.type].halfExtents.z + 0.8f)};
    for (int i = 0; i < 4; i++) {
        vec3 p = cands[i];
        p.y = world_ground_height(p + vec3(0, 1.0f, 0), 0.3f, 1.2f);
        if (!world_overlap_cylinder(p, 0.35f, 1.7f)) {
            if (ok) *ok = true;
            return p;
        }
    }
    if (ok) *ok = false;
    return cands[0];
}

int vehicle_find_tag(const char* tag) {
    for (int i = 0; i < MAX_VEHICLES; i++)
        if (g.vehicles[i].active && str_eq(g.vehicles[i].tag, tag)) return i;
    return -1;
}

static const vec3 g_carColors[] = {
    vec3(0.6f, 0.05f, 0.05f), vec3(0.08f, 0.1f, 0.25f), vec3(0.75f, 0.75f, 0.78f), vec3(0.1f, 0.1f, 0.1f), vec3(0.25f, 0.28f, 0.3f),
    vec3(0.05f, 0.25f, 0.15f), vec3(0.55f, 0.45f, 0.3f), vec3(0.85f, 0.85f, 0.85f), vec3(0.35f, 0.05f, 0.1f), vec3(0.1f, 0.3f, 0.45f),
};

int vehicle_spawn(int type, vec3 pos, float yaw, vec3 color, int ai) {
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (v.active) continue;
        memset(&v, 0, sizeof(v));
        v.active = true;
        v.type = (u8)type;
        v.pos = pos;
        v.yaw = yaw;
        v.color = color.x < 0 ? g_carColors[g.rng.irange(0, ARRAY_COUNT(g_carColors) - 1)] : color;
        v.ai = (u8)ai;
        v.driver = -1;
        v.health = g_vehicleDefs[type].health;
        v.handbrake = ai == VAI_PARKED;
        v.fromK = v.fromL = v.toK = v.toL = -1;
        return i;
    }
    return -1;
}

void vehicle_despawn(int vi) {
    Vehicle& v = g.vehicles[vi];
    if (!v.active) return;
    if (v.driver > 0 && g.actors[v.driver].active && g.actors[v.driver].vehicle == vi) actor_despawn(v.driver);
    if (v.driver == PLAYER) return;
    v.active = false;
}

void vehicle_damage(int vi, float dmg, vec3 pos) {
    Vehicle& v = g.vehicles[vi];
    if (!v.active || v.destroyed) return;
    v.health -= dmg;
    v.damage = fminf(1.0f, v.damage + dmg / g_vehicleDefs[v.type].health);
    if (v.health <= 0 && v.burnTime <= 0) {
        v.burnTime = 0.01f;
        if (v.driver > 0) {
            Actor& d = g.actors[v.driver];
            bool ok;
            d.pos = vehicle_exit_pos(v, &ok);
            d.state = AS_LOCOMOTION;
            d.vehicle = -1;
            d.aiMode = AI_FLEE;
            d.aiTimer = 10;
            d.lastKnown = v.pos;
            v.driver = -1;
        }
    }
    FIGHTO_UNUSED(pos);
}

// Three circles along the car body used for collisions.
static void car_circles(const Vehicle& v, vec3* c, float* r) {
    const VehicleDef& d = g_vehicleDefs[v.type];
    vec3 f = vehicle_forward(v);
    float off = d.halfExtents.z - d.halfExtents.x;
    c[0] = v.pos + f * off;
    c[1] = v.pos;
    c[2] = v.pos - f * off;
    *r = d.halfExtents.x;
}

static void collide_static(int vi, Vehicle& v, float dt) {
    vec3 c[3];
    float r;
    car_circles(v, c, &r);
    vec3 total(0, 0, 0);
    vec3 normalSum(0, 0, 0);
    int contacts = 0;
    float torque = 0;
    for (int k = 0; k < 3; k++) {
        vec3 p = c[k];
        p.y = v.pos.y;
        vec3 before = p;
        // Reuse the character solver: zero motion, just resolve penetration
        float gy;
        vec3 q = p;
        world_move_cylinder(&q, vec3(0, 0, 0), r, 1.2f, 0.45f, &gy);
        vec3 push = flat(q - before);
        if (length_sq(push) > 1e-6f) {
            total += push;
            normalSum += normalize(push);
            contacts++;
            float lever = (k == 0) ? 1.0f : (k == 2 ? -1.0f : 0.0f);
            vec3 lf = vehicle_left(v);
            torque += lever * dot(push, lf);
        }
    }
    if (contacts == 0) return;
    v.pos += total * (1.0f / contacts);
    vec3 n = normalize(normalSum);
    float into = dot(v.vel, n);
    if (into < 0) {
        float impact = -into;
        v.vel = v.vel - n * (into * 1.35f);
        v.vel = v.vel * 0.85f;
        if (impact > 4.0f) {
            vehicle_damage(vi, impact * impact * 0.45f, v.pos + n * -1.0f);
            if (g.time - v.lastCrashSound > 0.3f) {
                audio_play_at(SND_CRASH, v.pos, clampf(impact / 20.0f, 0.3f, 1.0f), g.rng.range(0.85f, 1.1f));
                v.lastCrashSound = g.time;
                for (int s = 0; s < 10; s++) {
                    Particle* p = fx_spawn(PT_SPARK);
                    if (!p) break;
                    p->pos = v.pos - n * g_vehicleDefs[v.type].halfExtents.x + vec3(0, 0.6f, 0);
                    p->vel = (n + vec3(g.rng.range(-1, 1), g.rng.range(0, 1), g.rng.range(-1, 1))) * g.rng.range(2, 7);
                    p->gravity = 9.8f;
                    p->size = 0.02f;
                    p->stretch = 0.03f;
                    p->life = p->maxLife = 0.4f;
                    p->color = vec3(1.0f, 0.7f, 0.3f);
                    p->emissive = 8;
                }
            }
            if (v.driver == PLAYER) camera_shake(clampf(impact / 25.0f, 0.1f, 0.8f));
        }
    }
    v.angVel += torque * 0.8f;
    FIGHTO_UNUSED(dt);
}

static void collide_vehicles(int vi, Vehicle& v) {
    vec3 ca[3];
    float ra;
    car_circles(v, ca, &ra);
    for (int j = vi + 1; j < MAX_VEHICLES; j++) {
        Vehicle& o = g.vehicles[j];
        if (!o.active || length_sq(o.pos - v.pos) > 64.0f) continue;
        vec3 cb[3];
        float rb;
        car_circles(o, cb, &rb);
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) {
                vec3 d = flat(ca[a] - cb[b]);
                float dist = length(d);
                float rr = ra + rb;
                if (dist >= rr || dist < 1e-4f) continue;
                vec3 n = d / dist;
                float pen = rr - dist;
                float ma = g_vehicleDefs[v.type].mass, mb = g_vehicleDefs[o.type].mass;
                float wa = mb / (ma + mb), wb = ma / (ma + mb);
                v.pos += n * (pen * wa);
                o.pos -= n * (pen * wb);
                float rel = dot(v.vel - o.vel, n);
                if (rel < 0) {
                    float jimp = -rel * 1.3f;
                    v.vel += n * (jimp * wa);
                    o.vel -= n * (jimp * wb);
                    if (-rel > 4.0f) {
                        vehicle_damage(vi, rel * rel * 0.3f, v.pos);
                        vehicle_damage(j, rel * rel * 0.3f, o.pos);
                        if (g.time - v.lastCrashSound > 0.3f) {
                            audio_play_at(SND_CRASH, (v.pos + o.pos) * 0.5f, clampf(-rel / 18.0f, 0.3f, 1.0f));
                            v.lastCrashSound = g.time;
                        }
                        if (v.driver == PLAYER && o.ai == VAI_TRAFFIC && o.driver > 0) {
                            o.hornTime = 1.5f;
                            if (o.type == VT_POLICE) wanted_crime(CRIME_COP_ASSAULT, o.pos, true);
                        }
                    }
                }
                ca[a] = ca[a] + n * (pen * wa);
            }
    }
}

static void hit_pedestrians(int vi, Vehicle& v) {
    float spd = length(v.vel);
    if (spd < 2.5f) return;
    vec3 c[3];
    float r;
    car_circles(v, c, &r);
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!a.active || a.state == AS_VEHICLE || a.kind == AK_DRONE) continue;
        if (fabsf(a.pos.y - v.pos.y) > 1.5f) continue;
        for (int k = 0; k < 3; k++) {
            vec3 d = flat(a.pos - c[k]);
            float dist = length(d);
            if (dist > r + a.radius) continue;
            vec3 n = dist > 0.01f ? d / dist : vehicle_left(v);
            if (dot(v.vel, n) <= 0.5f && a.state == AS_DEAD) continue;
            vec3 launch = normalize(v.vel + n * 2.0f);
            float dmg = spd * spd * 0.55f;
            bool alive = actor_alive(a);
            if (alive && a.iframes <= 0) {
                combat_damage(i, v.driver, dmg, a.pos + vec3(0, 1.0f, 0), launch, DMG_VEHICLE, B_PELVIS);
                a.vel = v.vel * 0.9f + vec3(0, spd * 0.25f, 0);
                a.iframes = 0.5f;
                v.blood = fminf(1.0f, v.blood + 0.35f);
                audio_play_at(SND_BODYFALL, a.pos, 1.0f, 0.8f);
                if (fx_gore_level() > 0) {
                    Decal* sm = fx_decal(DT_SMEAR, vec3(a.pos.x, world_particle_ground(a.pos) + 0.015f, a.pos.z) + flat(v.vel) * 0.15f, vec3(0, 1, 0), g.rng.range(1.0f, 2.2f), vec3(0.36f, 0.012f, 0.01f), g.rng.f01());
                    if (sm) { sm->tangent = normalize(flat(v.vel)); sm->sizeX = 0.25f; }
                }
                if (v.driver == PLAYER && a.faction != FAC_ALIEN) wanted_crime(CRIME_VEHICLE_HIT, a.pos, false);
            } else if (!alive) {
                a.pos += n * (r + a.radius - dist);
            }
            v.vel = v.vel * (alive ? 0.93f : 0.98f);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------
static void drive_physics(int vi, Vehicle& v, float dt) {
    const VehicleDef& d = g_vehicleDefs[v.type];
    vec3 f = vehicle_forward(v);
    vec3 l = vehicle_left(v);
    float vLong = dot(v.vel, f);
    float vLat = dot(v.vel, l);
    float speed = fabsf(vLong);
    bool dead = v.destroyed || v.burnTime > 0;
    float throttle = dead ? 0.0f : v.throttle;
    float maxSpeed = d.maxSpeed * (v.driver == PLAYER ? 1.0f : 0.8f);
    float accel = d.accel * (1.0f + (v.driver == PLAYER ? g.pd.upgrades[UP_HANDLING] * 0.08f : 0.0f));
    if (throttle > 0.01f) {
        if (vLong < -0.5f) vLong += d.brake * throttle * dt;
        else vLong += accel * throttle * (1.0f - saturate(vLong / maxSpeed) * 0.85f) * dt;
    } else if (throttle < -0.01f) {
        if (vLong > 0.5f) vLong -= d.brake * (-throttle) * dt;
        else if (vLong > -11.0f) vLong -= accel * 0.6f * (-throttle) * dt;
    } else {
        vLong -= vLong * 0.4f * dt + signf(vLong) * fminf(fabsf(vLong), 1.0f * dt);
    }
    if (v.brakeInput > 0) vLong = approach(vLong, 0.0f, d.brake * v.brakeInput * dt);
    if (v.handbrake) vLong = approach(vLong, 0.0f, (v.ai == VAI_PARKED ? 30.0f : 7.0f) * dt);
    vLong = clampf(vLong, -12.0f, maxSpeed);
    // Steering (bicycle model) with speed sensitive lock
    float steerAngle = v.steer * lerpf(0.62f, 0.16f, saturate(speed / 40.0f)) * d.steerRate;
    float wheelbase = d.halfExtents.z * 1.25f;
    float targetAng = vLong * tanf(steerAngle) / wheelbase;
    float grip = d.grip * (v.handbrake ? 0.25f : 1.0f);
    if (v.handbrake && speed > 6.0f) targetAng *= 1.5f;
    v.angVel = lerpf(v.angVel, targetAng, saturate(grip * 1.2f * dt * 3.0f));
    // Lateral grip
    vLat *= expf(-grip * dt * (v.handbrake ? 0.4f : 1.0f));
    v.yaw = wrap_angle(v.yaw + v.angVel * dt);
    f = vehicle_forward(v);
    l = vehicle_left(v);
    v.vel = f * vLong + l * vLat;
    // Skid marks / smoke when sliding
    if (fabsf(vLat) > 4.5f && speed > 6.0f && v.driver >= 0) {
        if (g.rng.chance(dt * 30.0f)) {
            Decal* tm = fx_decal(DT_TYRE, vec3(v.pos.x, v.pos.y + 0.012f, v.pos.z) - f * d.halfExtents.z * 0.7f + l * d.halfExtents.x * 0.8f, vec3(0, 1, 0), 0.5f, vec3(0, 0, 0), 0);
            if (tm) { tm->tangent = normalize(flat(v.vel)); tm->sizeX = 0.25f; }
            Particle* p = fx_spawn(PT_SMOKE);
            if (p) {
                p->pos = v.pos - f * d.halfExtents.z * 0.8f + vec3(0, 0.3f, 0);
                p->vel = vec3(g.rng.range(-0.5f, 0.5f), 0.6f, g.rng.range(-0.5f, 0.5f));
                p->size = 0.4f; p->sizeGrow = 1.2f; p->life = p->maxLife = 1.4f; p->color = vec3(0.6f, 0.6f, 0.62f); p->alpha = 0.45f;
            }
        }
        if (v.driver == PLAYER && g.rng.chance(dt * 4.0f)) audio_play_at(SND_SKID, v.pos, 0.5f, g.rng.range(0.9f, 1.1f));
    }
    // Integrate
    v.pos += v.vel * dt;
    float gy = world_ground_height(v.pos + vec3(0, 0.6f, 0), 0.8f, 0.7f);
    v.pos.y = lerpf(v.pos.y, gy, saturate(dt * 12.0f));
    collide_static(vi, v, dt);
    // Visual suspension
    float accelLong = (vLong - dot(v.vel, f)) / fmaxf(dt, 1e-4f);
    v.bodyPitch = lerpf(v.bodyPitch, clampf(-throttle * 0.03f + (v.brakeInput > 0 ? 0.04f : 0.0f), -0.06f, 0.06f), saturate(dt * 4.0f));
    v.bodyRoll = lerpf(v.bodyRoll, clampf(v.angVel * vLong * 0.004f, -0.08f, 0.08f), saturate(dt * 4.0f));
    v.wheelRot += vLong * dt / 0.36f;
    v.engineRev = lerpf(v.engineRev, saturate(speed / maxSpeed * 0.8f + fabsf(throttle) * 0.25f), saturate(dt * 4.0f));
    FIGHTO_UNUSED(accelLong);
}

static void player_drive(Vehicle& v) {
    const Controls& c = g.ctl;
    v.throttle = clampf(c.move.y, -1.0f, 1.0f);
    float targetSteer = -c.move.x;
    v.steer = approach(v.steer, targetSteer, g.dt * (fabsf(targetSteer) > 0.1f ? 3.5f : 5.0f));
    v.handbrake = c.down[ACT_JUMP];
    v.brakeInput = 0;
    if (c.pressed[ACT_HORN]) {
        audio_play_at(SND_HORN, v.pos, 1.0f, v.type == VT_VAN ? 0.8f : 1.0f);
        ai_alert_area(v.pos, 15.0f, PLAYER, 0);
    }
    if (v.type == VT_POLICE && c.pressed[ACT_KICK]) v.siren = !v.siren;
    g.pd.distanceDriven += length(v.vel) * g.dt;
}

// ---------------------------------------------------------------------------
// Traffic AI
// ---------------------------------------------------------------------------
static vec3 inter_pos(int k, int l) { return vec3(road_x(k), 0, road_x(l)); }

static bool light_green(int k, int l, bool alongZ) {
    float t = fmodf(g.time + (k * 7 + l * 3) * 1.7f, 24.0f);
    bool nsGreen = t < 10.5f;
    bool ewGreen = t >= 12.0f && t < 22.5f;
    return alongZ ? nsGreen : ewGreen;
}

static void pick_next(Vehicle& v) {
    int dk = v.toK - v.fromK, dl = v.toL - v.fromL;
    int opts[3][2];
    int n = 0;
    int dirs[3][2] = {{dk, dl}, {-dl, dk}, {dl, -dk}};   // straight, left, right
    for (int i = 0; i < 3; i++) {
        int nk = v.toK + dirs[i][0], nl = v.toL + dirs[i][1];
        if (nk < 0 || nl < 0 || nk > CITY_BLOCKS || nl > CITY_BLOCKS) continue;
        int weight = i == 0 ? 3 : 1;
        for (int w = 0; w < weight && n < 3; w++) { opts[n][0] = nk; opts[n][1] = nl; n++; }
    }
    if (n == 0) { opts[0][0] = v.fromK; opts[0][1] = v.fromL; n = 1; }
    int c = g.rng.irange(0, n - 1);
    v.fromK = v.toK;
    v.fromL = v.toL;
    v.toK = opts[c][0];
    v.toL = opts[c][1];
}

static void traffic_drive(int vi, Vehicle& v, float dt) {
    if (v.toK < 0) {
        // Snap to the nearest road segment
        vec3 rp = world_nearest_road_point(v.pos);
        int k = clamp_t((int)roundf((rp.x + CITY_HALF) / BLOCK_PITCH), 0, CITY_BLOCKS);
        int l = clamp_t((int)roundf((rp.z + CITY_HALF) / BLOCK_PITCH), 0, CITY_BLOCKS);
        v.fromK = k; v.fromL = l;
        vec3 f = vehicle_forward(v);
        if (fabsf(f.x) > fabsf(f.z)) { v.toK = clamp_t(k + (f.x > 0 ? 1 : -1), 0, CITY_BLOCKS); v.toL = l; }
        else { v.toK = k; v.toL = clamp_t(l + (f.z > 0 ? 1 : -1), 0, CITY_BLOCKS); }
        if (v.toK == v.fromK && v.toL == v.fromL) { v.toK = clamp_t(k + 1, 0, CITY_BLOCKS); if (v.toK == k) v.toK = k - 1; }
    }
    vec3 a = inter_pos(v.fromK, v.fromL), b = inter_pos(v.toK, v.toL);
    vec3 dir = normalize(b - a);
    vec3 right(-dir.z, 0, dir.x); // right-hand traffic: lane to the right of travel direction
    vec3 laneA = a + right * 3.6f, laneB = b + right * 3.6f;
    // Progress along lane
    float segLen = length(laneB - laneA);
    float s = dot(v.pos - laneA, dir);
    float distToEnd = segLen - s;
    if (distToEnd < 7.0f) {
        pick_next(v);
        return;
    }
    vec3 look = laneA + dir * fminf(s + 9.0f, segLen);
    vec3 to = look - v.pos;
    float targetYaw = atan2f(to.x, to.z);
    float err = wrap_angle(targetYaw - v.yaw);
    v.steer = clampf(err * 2.2f, -1.0f, 1.0f);
    float cruise = 12.5f;
    float desired = cruise;
    // Traffic light
    bool alongZ = fabsf(dir.z) > 0.5f;
    if (distToEnd < 22.0f && distToEnd > 9.5f && !light_green(v.toK, v.toL, alongZ)) desired = fminf(desired, (distToEnd - 11.0f) * 1.2f);
    // Obstacles ahead (cars, people, the player)
    vec3 f = vehicle_forward(v);
    for (int j = 0; j < MAX_VEHICLES; j++) {
        if (j == vi || !g.vehicles[j].active) continue;
        vec3 d = g.vehicles[j].pos - v.pos;
        float along = dot(d, f);
        if (along < 0 || along > 16.0f) continue;
        if (fabsf(dot(d, vec3(f.z, 0, -f.x))) > 2.2f) continue;
        desired = fminf(desired, (along - 6.0f) * 1.1f);
    }
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor& p = g.actors[i];
        if (!p.active || p.state == AS_VEHICLE || p.state == AS_DEAD) continue;
        vec3 d = p.pos - v.pos;
        float along = dot(d, f);
        if (along < 0 || along > 12.0f) continue;
        if (fabsf(dot(d, vec3(f.z, 0, -f.x))) > 1.6f) continue;
        desired = fminf(desired, (along - 4.0f) * 1.0f);
        if (i == PLAYER && along < 8.0f && v.hornTime <= 0) { v.hornTime = 3.0f; audio_play_at(SND_HORN, v.pos, 0.7f); }
    }
    float spd = dot(v.vel, f);
    if (desired <= 0.3f) { v.throttle = 0; v.brakeInput = 1.0f; }
    else { v.brakeInput = 0; v.throttle = clampf((desired - spd) * 0.4f, -1.0f, 1.0f); }
    v.handbrake = false;
    // Stuck recovery
    if (spd < 0.5f && desired > 3.0f) v.stuckTime += dt;
    else v.stuckTime = 0;
    if (v.stuckTime > 6.0f) { v.toK = -1; v.stuckTime = 0; }
}

static void chase_drive(int vi, Vehicle& v, float dt) {
    Actor& p = g.actors[PLAYER];
    vec3 target = p.state == AS_VEHICLE && p.vehicle >= 0 ? g.vehicles[p.vehicle].pos + g.vehicles[p.vehicle].vel * 0.6f : p.pos;
    vec3 to = target - v.pos;
    float dist = length(flat(to));
    // Avoid obstacles with three feelers
    vec3 f = vehicle_forward(v);
    float targetYaw = atan2f(to.x, to.z);
    RayHit h;
    if (world_raycast(v.pos + vec3(0, 0.7f, 0), f, 9.0f, &h, false)) {
        vec3 lft = vehicle_left(v);
        bool lBlocked = world_raycast(v.pos + vec3(0, 0.7f, 0), normalize(f + lft), 8.0f, 0, false);
        bool rBlocked = world_raycast(v.pos + vec3(0, 0.7f, 0), normalize(f - lft), 8.0f, 0, false);
        targetYaw = v.yaw + (lBlocked && !rBlocked ? -1.0f : 1.0f);
    }
    float err = wrap_angle(targetYaw - v.yaw);
    v.steer = clampf(err * 2.0f, -1.0f, 1.0f);
    float spd = dot(v.vel, f);
    bool onFoot = p.state != AS_VEHICLE;
    float desired = onFoot ? (dist > 18.0f ? 22.0f : 0.0f) : 34.0f;
    if (fabsf(err) > 1.2f && spd > 10.0f) desired = 10.0f;
    v.throttle = clampf((desired - spd) * 0.3f, -1.0f, 1.0f);
    v.handbrake = fabsf(err) > 1.0f && spd > 12.0f;
    if (spd < 0.8f && desired > 3.0f) v.stuckTime += dt; else v.stuckTime = 0;
    if (v.stuckTime > 2.5f) { v.throttle = -1.0f; if (v.stuckTime > 4.0f) v.stuckTime = 0; }
    // Officers bail out close to a player on foot
    if (onFoot && dist < 20.0f && fabsf(spd) < 2.0f && v.driver > 0) {
        Actor& d = g.actors[v.driver];
        bool ok;
        d.pos = vehicle_exit_pos(v, &ok);
        if (ok) {
            d.state = AS_LOCOMOTION;
            d.vehicle = -1;
            d.aiMode = AI_COMBAT;
            d.target = PLAYER;
            d.lastKnown = p.pos;
            d.lastSeen = g.time;
            v.driver = -1;
            v.ai = VAI_PARKED;
            v.handbrake = true;
        }
    }
}

void traffic_update(float dt) {
    g.trafficTimer -= dt;
    if (g.trafficTimer > 0) return;
    g.trafficTimer = 0.5f;
    Actor& p = g.actors[PLAYER];
    vec3 pp = p.pos;
    if (pp.y < -20.0f) return;
    int traffic = 0, parked = 0;
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (!v.active) continue;
        float d = length(v.pos - pp);
        if (!v.persistent && !v.tag[0] && v.driver != PLAYER && (d > 260.0f || (v.destroyed && d > 120.0f))) { vehicle_despawn(i); continue; }
        if (v.ai == VAI_TRAFFIC) traffic++;
        if (v.ai == VAI_PARKED) parked++;
    }
    int maxTraffic = plat_is_mobile() ? 8 : 16;
    if (g.pd.phase == 2) maxTraffic /= 2;
    if (g.pd.phase >= 3) maxTraffic /= 4;
    if (traffic < maxTraffic) {
        for (int tries = 0; tries < 6; tries++) {
            int k = g.rng.irange(0, CITY_BLOCKS), l = g.rng.irange(0, CITY_BLOCKS);
            bool alongZ = g.rng.chance(0.5f);
            int k2 = alongZ ? k : k + (g.rng.chance(0.5f) ? 1 : -1);
            int l2 = alongZ ? l + (g.rng.chance(0.5f) ? 1 : -1) : l;
            if (k2 < 0 || l2 < 0 || k2 > CITY_BLOCKS || l2 > CITY_BLOCKS) continue;
            vec3 a = inter_pos(k, l), b = inter_pos(k2, l2);
            vec3 dir = normalize(b - a);
            vec3 right = vec3(-dir.z, 0, dir.x);
            vec3 sp = a + dir * 30.0f + right * 3.6f;
            float d = length(sp - pp);
            if (d < 70.0f || d > 200.0f) continue;
            int type = g.rng.chance(0.12f) ? VT_TAXI : g.rng.chance(0.1f) ? VT_VAN : g.rng.chance(0.2f) ? VT_SUV : g.rng.chance(0.1f) ? VT_MUSCLE : g.rng.chance(0.05f) ? VT_SPORTS : VT_SEDAN;
            int vi = vehicle_spawn(type, sp, atan2f(dir.x, dir.z), vec3(-1, 0, 0), VAI_TRAFFIC);
            if (vi < 0) break;
            Vehicle& v = g.vehicles[vi];
            v.fromK = k; v.fromL = l; v.toK = k2; v.toL = l2;
            v.vel = dir * 10.0f;
            int dr = actor_spawn_preset(AK_CIVILIAN, "civilian", v.pos, v.yaw, g.rng.next());
            if (dr >= 0) {
                g.actors[dr].state = AS_VEHICLE;
                g.actors[dr].vehicle = vi;
                g.actors[dr].aiMode = AI_DRIVE;
                v.driver = dr;
            }
            break;
        }
    }
    int maxParked = plat_is_mobile() ? 8 : 14;
    if (parked < maxParked && g_world.parkingSpots.count > 0) {
        for (int tries = 0; tries < 6; tries++) {
            vec4 s = g_world.parkingSpots[g.rng.irange(0, g_world.parkingSpots.count - 1)];
            vec3 sp(s.x, s.y, s.z);
            float d = length(sp - pp);
            if (d < 50.0f || d > 160.0f) continue;
            bool taken = false;
            for (int i = 0; i < MAX_VEHICLES; i++) if (g.vehicles[i].active && length(g.vehicles[i].pos - sp) < 4.0f) taken = true;
            if (taken) continue;
            int type = g.rng.chance(0.2f) ? VT_SUV : g.rng.chance(0.12f) ? VT_MUSCLE : g.rng.chance(0.06f) ? VT_SPORTS : VT_SEDAN;
            int vi = vehicle_spawn(type, sp, s.w + (g.rng.chance(0.5f) ? PI_F : 0.0f), vec3(-1, 0, 0), VAI_PARKED);
            if (vi >= 0) g.vehicles[vi].locked = g.rng.chance(0.25f);
            break;
        }
    }
}

void vehicles_update(float dt) {
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (!v.active) continue;
        v.hornTime = fmaxf(0.0f, v.hornTime - dt);
        if (v.driver == PLAYER) player_drive(v);
        else if (v.driver > 0 && !v.destroyed && v.burnTime <= 0) {
            if (v.ai == VAI_TRAFFIC) traffic_drive(i, v, dt);
            else if (v.ai == VAI_CHASE) chase_drive(i, v, dt);
        } else if (v.driver < 0 && v.ai != VAI_SCRIPT) {
            v.throttle = 0;
            v.steer = 0;
            v.handbrake = true;
        }
        // Skip simulation for far parked cars
        bool sim = v.driver >= 0 || length_sq(v.vel) > 0.01f || length(v.pos - g.actors[PLAYER].pos) < 60.0f;
        if (sim) {
            drive_physics(i, v, dt);
            collide_vehicles(i, v);
            hit_pedestrians(i, v);
        }
        if (v.siren) v.sirenPhase += dt * 6.0f;
        // Fire & explosion
        if (v.burnTime > 0 && !v.destroyed) {
            v.burnTime += dt;
            if (g.rng.chance(dt * 25.0f)) {
                Particle* p = fx_spawn(PT_FIRE);
                if (p) {
                    p->pos = v.pos + vehicle_forward(v) * (g_vehicleDefs[v.type].halfExtents.z * 0.6f) + vec3(g.rng.range(-0.5f, 0.5f), 1.0f, g.rng.range(-0.5f, 0.5f));
                    p->vel = vec3(0, 2.5f, 0);
                    p->size = 0.5f; p->sizeGrow = 0.4f; p->life = p->maxLife = 0.6f; p->color = vec3(1, 0.5f, 0.2f);
                }
                Particle* s = fx_spawn(PT_SMOKE);
                if (s) {
                    s->pos = v.pos + vec3(0, 1.5f, 0);
                    s->vel = vec3(0, 2.0f, 0);
                    s->size = 0.6f; s->sizeGrow = 1.2f; s->life = s->maxLife = 2.5f; s->color = vec3(0.08f, 0.08f, 0.08f); s->alpha = 0.7f;
                }
            }
            if (v.burnTime > 5.0f) {
                v.destroyed = true;
                v.destroyedTime = g.time;
                explosion(v.pos + vec3(0, 1.0f, 0), 7.0f, 140.0f, -1, false);
                v.vel = v.vel + vec3(0, 0, 0);
            }
        }
        if (v.blood > 0) v.blood = fmaxf(0.0f, v.blood - dt * 0.01f);
    }
}

void vehicles_render() {
    vec3 cam = g.cam.pos;
    int lightsBudget = plat_is_mobile() ? 3 : 10;
    for (int i = 0; i < MAX_VEHICLES; i++) {
        Vehicle& v = g.vehicles[i];
        if (!v.active) continue;
        float d = length(v.pos - cam);
        if (d > renderer_settings().drawDistance) continue;
        if (!frustum_sphere(renderer_frustum(), v.pos + vec3(0, 1, 0), 4.0f)) continue;
        const VehicleDef& def = g_vehicleDefs[v.type];
        quat q = quat_axis_angle(vec3(0, 1, 0), v.yaw) * quat_axis_angle(vec3(1, 0, 0), v.bodyPitch) * quat_axis_angle(vec3(0, 0, 1), v.bodyRoll);
        mat4 body = mat4_trs(v.pos, q, vec3(1, 1, 1));
        DrawItem di;
        memset(&di, 0, sizeof(di));
        di.mesh = &g_bodyMesh[v.type];
        di.model = body;
        vec3 col = v.destroyed ? vec3(0.05f, 0.045f, 0.04f) : v.color * (1.0f - v.damage * 0.35f);
        di.tint = vec4(col.x, col.y, col.z, 1);
        di.castShadow = d < 70.0f;
        di.blood = v.blood;
        di.bloodColor = vec3(0.45f, 0.02f, 0.02f);
        renderer_submit(di);
        // Wheels
        float wz = def.halfExtents.z - 0.75f;
        for (int k = 0; k < 4; k++) {
            float x = (k & 1) ? def.halfExtents.x - 0.12f : -def.halfExtents.x + 0.12f;
            float z = (k & 2) ? wz : -wz;
            quat wq = quat_axis_angle(vec3(0, 1, 0), v.yaw + ((k & 2) ? v.steer * 0.45f : 0.0f)) * quat_axis_angle(vec3(1, 0, 0), v.wheelRot);
            vec3 wp = transform_point(mat4_trs(v.pos, quat_axis_angle(vec3(0, 1, 0), v.yaw), vec3(1, 1, 1)), vec3(x, 0.36f, z));
            DrawItem w = di;
            w.mesh = &g_wheelMesh;
            w.model = mat4_trs(wp, wq, vec3(1, 1, 1));
            w.tint = vec4(1, 1, 1, 1);
            w.blood = 0;
            w.castShadow = d < 30.0f;
            if (d < 120.0f) renderer_submit(w);
        }
        if (v.type == VT_POLICE) {
            DrawItem lb = di;
            lb.mesh = &g_lightBarMesh;
            lb.model = body * mat4_translate(vec3(0, def.halfExtents.y + 0.02f, -0.2f));
            lb.tint = vec4(1, 1, 1, 1);
            renderer_submit(lb);
            if (v.siren && d < 150.0f) {
                bool red = fmodf(v.sirenPhase, 2.0f) < 1.0f;
                vec3 lp = transform_point(body, vec3(red ? -0.3f : 0.3f, def.halfExtents.y + 0.3f, -0.2f));
                Light l = {lp, 16.0f, red ? vec3(1.0f, 0.05f, 0.05f) * 6.0f : vec3(0.1f, 0.25f, 1.0f) * 7.0f, vec3(0, -1, 0), 0};
                renderer_add_light(l);
            }
        }
        // Head lights (spot) and tail lights for nearby moving or occupied cars
        if (!v.destroyed && v.driver >= 0 && lightsBudget > 0 && d < 140.0f) {
            lightsBudget--;
            vec3 f = vehicle_forward(v);
            vec3 hp = v.pos + f * (def.halfExtents.z + 0.3f) + vec3(0, 0.75f, 0);
            Light hl = {hp, 34.0f, vec3(1.0f, 0.95f, 0.85f) * 14.0f, normalize(f + vec3(0, -0.12f, 0)), 0.82f};
            renderer_add_light(hl);
            vec3 tp = v.pos - f * (def.halfExtents.z + 0.4f) + vec3(0, 0.75f, 0);
            float brake = v.brakeInput > 0 || v.throttle < -0.1f ? 3.0f : 1.0f;
            Light tl = {tp, 5.0f, vec3(1.0f, 0.05f, 0.03f) * brake, vec3(0, -1, 0), 0};
            renderer_add_light(tl);
            BeamDraw b;
            b.a = hp;
            b.b = hp + normalize(f + vec3(0, -0.1f, 0)) * 14.0f;
            b.r0 = 0.3f;
            b.r1 = 3.0f;
            b.color = vec3(1.0f, 0.95f, 0.85f);
            b.intensity = 0.06f;
            b.alpha = 1.0f;
            renderer_beam(b);
        }
    }
}
