// FIGHTO - melee combat, damage, hit reactions, gore effects and verlet ragdolls.
#include "game.h"
#include "../audio/audio.h"

static const vec3 kBlood(0.5f, 0.02f, 0.018f);
static const vec3 kBloodDark(0.3f, 0.012f, 0.01f);
static const vec3 kIchor(0.3f, 0.95f, 0.55f);

// ---------------------------------------------------------------------------
// Blood
// ---------------------------------------------------------------------------
static float gore_scale() {
    int g0 = fx_gore_level();
    return g0 == 0 ? 0.0f : g0 == 1 ? 0.6f : 1.6f;
}

void combat_blood_burst(vec3 pos, vec3 dir, float amount, bool alien) {
    // Heavy hits are already capped upstream; this guards every caller (huge amounts would
    // flood the particle pool with screen-sized mist)
    amount = clampf(amount, 0.0f, 4.0f);
    float gs = gore_scale();
    if (gs <= 0.0f) return;
    vec3 col = alien ? kIchor : kBlood;
    int drops = (int)((6.0f + amount * 16.0f) * gs);
    drops = min_t(drops, 160);
    vec3 d = length_sq(dir) > 1e-6f ? normalize(dir) : vec3(0, 1, 0);
    for (int i = 0; i < drops; i++) {
        Particle* p = fx_spawn(PT_BLOOD);
        if (!p) break;
        p->pos = pos + vec3(g.rng.range(-0.05f, 0.05f), g.rng.range(-0.05f, 0.05f), g.rng.range(-0.05f, 0.05f));
        vec3 spread(g.rng.range(-1, 1), g.rng.range(-0.4f, 1.2f), g.rng.range(-1, 1));
        p->vel = (d * g.rng.range(1.5f, 6.0f) + spread * g.rng.range(0.5f, 2.2f)) * (0.8f + amount * 0.15f);
        p->gravity = 9.8f;
        p->drag = 0.4f;
        bool chunk = gs > 1.0f && g.rng.chance(0.08f * amount);
        p->size = chunk ? g.rng.range(0.04f, 0.08f) : g.rng.range(0.012f, 0.04f);
        p->stretch = g.rng.chance(0.5f) ? 0.035f : 0.0f;
        p->life = p->maxLife = g.rng.range(0.8f, 2.0f);
        p->color = lerp(col, alien ? col * 0.6f : kBloodDark, g.rng.f01());
        p->spawnDecal = alien ? 2 : 1;
        p->emissive = alien ? 2.0f : 0.0f;
    }
    // Fine mist
    int mists = (int)((2 + amount * 3) * fminf(gs, 1.2f));
    for (int i = 0; i < mists; i++) {
        Particle* p = fx_spawn(PT_SMOKE);
        if (!p) break;
        p->pos = pos;
        p->vel = d * g.rng.range(0.5f, 2.0f) + vec3(g.rng.range(-0.4f, 0.4f), g.rng.range(-0.2f, 0.4f), g.rng.range(-0.4f, 0.4f));
        p->drag = 3.0f;
        p->size = g.rng.range(0.1f, 0.25f) * (0.7f + amount * 0.25f);
        p->sizeGrow = 0.6f;
        p->life = p->maxLife = g.rng.range(0.35f, 0.8f);
        p->color = alien ? col : vec3(0.35f, 0.02f, 0.02f);
        p->alpha = 0.55f;
    }
    audio_play_at(SND_SPLAT, pos, clampf(0.25f + amount * 0.25f, 0.2f, 1.0f), g.rng.range(0.85f, 1.2f));
}

void combat_blood_pool(vec3 pos, bool alien, float size) {
    if (gore_scale() <= 0.0f) return;
    float gy = world_ground_height(pos + vec3(0, 0.5f, 0), 0.1f, 0.6f);
    Decal* d = fx_decal(alien ? DT_ICHOR : DT_BLOOD_POOL, vec3(pos.x, gy + 0.015f, pos.z), vec3(0, 1, 0), size * (fx_gore_level() == 2 ? 1.25f : 1.0f), alien ? kIchor : vec3(0.4f, 0.014f, 0.012f), g.rng.f01());
    if (d) {
        d->grow = 0.12f;
        d->growTarget = 0.95f;
        d->growRate = 0.12f;
    }
}

static void wall_splatter(vec3 pos, vec3 dir, float amount, bool alien) {
    if (gore_scale() <= 0.0f) return;
    RayHit h;
    if (world_raycast(pos, normalize(dir), 3.5f, &h, false)) {
        float size = g.rng.range(0.35f, 0.75f) * (0.7f + amount * 0.35f) * (fx_gore_level() == 2 ? 1.3f : 1.0f);
        fx_decal(alien ? DT_ICHOR : DT_BLOOD_SPLAT, h.pos, h.normal, size, alien ? kIchor : kBlood, g.rng.f01());
        // Runs dripping down vertical walls
        if (fabsf(h.normal.y) < 0.3f && fx_gore_level() == 2) {
            Decal* run = fx_decal(DT_SMEAR, h.pos - vec3(0, size * 0.8f, 0), h.normal, size * 0.9f, kBloodDark, g.rng.f01());
            if (run) {
                run->tangent = normalize(cross(h.normal, vec3(0, 1, 0)));
                run->sizeX = 0.12f;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Ragdoll (verlet particles)
// ---------------------------------------------------------------------------
enum RP { RP_PELVIS, RP_CHEST, RP_HEAD, RP_LSH, RP_LEL, RP_LWR, RP_RSH, RP_REL, RP_RWR, RP_LHIP, RP_LKNEE, RP_LANK, RP_RHIP, RP_RKNEE, RP_RANK, RP_NECK, RP_COUNT };
static const int g_rpBone[RP_COUNT] = {B_PELVIS, B_CHEST, B_HEAD, B_L_UPPERARM, B_L_FOREARM, B_L_HAND, B_R_UPPERARM, B_R_FOREARM, B_R_HAND, B_L_THIGH, B_L_SHIN, B_L_FOOT, B_R_THIGH, B_R_SHIN, B_R_FOOT, B_NECK};
static const int g_rpLinks[][2] = {
    {RP_PELVIS, RP_CHEST}, {RP_CHEST, RP_NECK}, {RP_NECK, RP_HEAD}, {RP_CHEST, RP_LSH}, {RP_CHEST, RP_RSH}, {RP_LSH, RP_RSH},
    {RP_LSH, RP_LEL}, {RP_LEL, RP_LWR}, {RP_RSH, RP_REL}, {RP_REL, RP_RWR}, {RP_PELVIS, RP_LHIP}, {RP_PELVIS, RP_RHIP},
    {RP_LHIP, RP_RHIP}, {RP_LHIP, RP_LKNEE}, {RP_LKNEE, RP_LANK}, {RP_RHIP, RP_RKNEE}, {RP_RKNEE, RP_RANK},
    {RP_LHIP, RP_CHEST}, {RP_RHIP, RP_CHEST}, {RP_LSH, RP_PELVIS}, {RP_RSH, RP_PELVIS}, {RP_LSH, RP_NECK}, {RP_RSH, RP_NECK},
    {RP_HEAD, RP_CHEST}, {RP_LSH, RP_HEAD}, {RP_RSH, RP_HEAD}, {RP_LHIP, RP_LANK}, {RP_RHIP, RP_RANK},
};
#define RP_LINKS ((int)(sizeof(g_rpLinks) / sizeof(g_rpLinks[0])))

struct Ragdoll {
    vec3 p[RP_COUNT], prev[RP_COUNT];
    float rest[RP_LINKS];
    float yaw;
    float time;
    float still;
    bool sleeping;
};

static Ragdoll g_ragdollPool[24];
static bool g_ragdollUsed[24];

void ragdoll_free(Actor& a) {
    if (!a.ragdoll) return;
    int i = (int)(a.ragdoll - g_ragdollPool);
    if (i >= 0 && i < 24) g_ragdollUsed[i] = false;
    a.ragdoll = 0;
}

static vec3 rp_head_point(const Actor& a) { return joint_world(a.world, a.globals, B_HEAD, vec3(0, 0.12f * a.model->skel.scale, 0)); }

void ragdoll_start(Actor& a, vec3 impulse) {
    if (!a.model || a.ragdoll) return;
    int slot = -1;
    for (int i = 0; i < 24; i++) if (!g_ragdollUsed[i]) { slot = i; break; }
    if (slot < 0) {
        // Steal the oldest sleeping ragdoll
        for (int i = 0; i < MAX_ACTORS; i++) {
            Actor& o = g.actors[i];
            if (o.ragdoll && o.ragdoll->sleeping) { slot = (int)(o.ragdoll - g_ragdollPool); o.ragdoll = 0; break; }
        }
        if (slot < 0) return;
    }
    g_ragdollUsed[slot] = true;
    Ragdoll* r = &g_ragdollPool[slot];
    memset(r, 0, sizeof(*r));
    a.world = mat4_trs(a.pos, quat_axis_angle(vec3(0, 1, 0), a.yaw), vec3(1, 1, 1));
    for (int i = 0; i < RP_COUNT; i++) {
        r->p[i] = i == RP_HEAD ? rp_head_point(a) : joint_world(a.world, a.globals, g_rpBone[i]);
    }
    float dt = 1.0f / 60.0f;
    vec3 base = a.vel * dt;
    for (int i = 0; i < RP_COUNT; i++) {
        float w = (i == RP_HEAD || i == RP_CHEST || i == RP_NECK) ? 1.3f : (i == RP_PELVIS ? 0.9f : 0.7f);
        r->prev[i] = r->p[i] - base - impulse * (w * dt) - vec3(g.rng.range(-0.01f, 0.01f), 0, g.rng.range(-0.01f, 0.01f));
    }
    for (int k = 0; k < RP_LINKS; k++) r->rest[k] = length(r->p[g_rpLinks[k][0]] - r->p[g_rpLinks[k][1]]);
    r->yaw = a.yaw;
    a.ragdoll = r;
}

static mat4 basis_mat(vec3 x, vec3 y, vec3 z, vec3 o) {
    mat4 m = mat4_identity();
    m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
    m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
    m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    m.m[12] = o.x; m.m[13] = o.y; m.m[14] = o.z;
    return m;
}

static mat4 frame_from(vec3 origin, vec3 up, vec3 left) {
    vec3 y = normalize(up);
    vec3 x = normalize(left - y * dot(left, y));
    if (length_sq(x) < 1e-6f) x = vec3(1, 0, 0);
    vec3 z = cross(x, y);
    return basis_mat(x, y, z, origin);
}

static mat4 limb_mat(const Skeleton& s, int bone, int child, vec3 jointW, vec3 childW, float yaw) {
    vec3 restDir = normalize(s.restPos[child] - s.restPos[bone]);
    quat ry = quat_axis_angle(vec3(0, 1, 0), yaw);
    vec3 restW = quat_rotate(ry, restDir);
    vec3 cur = childW - jointW;
    if (length_sq(cur) < 1e-8f) cur = restW;
    quat q = quat_from_to(restW, normalize(cur)) * ry;
    return mat4_trs(jointW, q, vec3(1, 1, 1));
}

void ragdoll_update(Actor& a, float dt) {
    Ragdoll* r = a.ragdoll;
    if (!r) return;
    if (!r->sleeping) {
        r->time += dt;
        float h = fminf(dt, 1.0f / 30.0f);
        float motion = 0;
        for (int i = 0; i < RP_COUNT; i++) {
            vec3 v = (r->p[i] - r->prev[i]) * 0.985f;
            r->prev[i] = r->p[i];
            r->p[i] += v + vec3(0, -9.8f, 0) * (h * h);
            motion += length_sq(v);
        }
        for (int it = 0; it < 8; it++) {
            for (int k = 0; k < RP_LINKS; k++) {
                vec3& p0 = r->p[g_rpLinks[k][0]];
                vec3& p1 = r->p[g_rpLinks[k][1]];
                vec3 d = p1 - p0;
                float len = length(d);
                if (len < 1e-6f) continue;
                float diff = (len - r->rest[k]) / len * 0.5f;
                p0 += d * diff;
                p1 -= d * diff;
            }
            for (int i = 0; i < RP_COUNT; i++) {
                float rad = (i == RP_HEAD) ? 0.11f : (i == RP_CHEST || i == RP_PELVIS) ? 0.12f : 0.05f;
                float gy = world_ground_height(r->p[i] + vec3(0, 0.4f, 0), 0.02f, 0.45f);
                if (r->p[i].y < gy + rad) {
                    r->p[i].y = gy + rad;
                    // Friction
                    r->prev[i].x = lerpf(r->prev[i].x, r->p[i].x, 0.35f);
                    r->prev[i].z = lerpf(r->prev[i].z, r->p[i].z, 0.35f);
                }
            }
        }
        // Keep parts out of walls (push back along the motion)
        for (int i = 0; i < RP_COUNT; i += 3) {
            vec3 d = r->p[i] - r->prev[i];
            float len = length(d);
            if (len < 1e-4f) continue;
            RayHit hit;
            if (world_raycast(r->prev[i], d / len, len + 0.08f, &hit, false) && hit.box >= 0) {
                r->p[i] = hit.pos + hit.normal * 0.08f;
            }
        }
        if (motion < 0.00002f * RP_COUNT && r->time > 1.0f) r->still += dt;
        else r->still = 0;
        if (r->still > 0.8f || r->time > 8.0f) r->sleeping = true;
    }
    // Rebuild bone transforms (world space)
    const Skeleton& s = a.model->skel;
    vec3 left = r->p[RP_LHIP] - r->p[RP_RHIP];
    vec3 shoulders = r->p[RP_LSH] - r->p[RP_RSH];
    mat4 G[B_COUNT];
    G[B_PELVIS] = frame_from(r->p[RP_PELVIS], r->p[RP_CHEST] - r->p[RP_PELVIS], left);
    float spineT = (s.restPos[B_SPINE].y - s.restPos[B_PELVIS].y) / fmaxf(s.restPos[B_CHEST].y - s.restPos[B_PELVIS].y, 0.01f);
    G[B_SPINE] = frame_from(lerp(r->p[RP_PELVIS], r->p[RP_CHEST], spineT), r->p[RP_CHEST] - r->p[RP_PELVIS], lerp(left, shoulders, 0.5f));
    G[B_CHEST] = frame_from(r->p[RP_CHEST], r->p[RP_NECK] - r->p[RP_CHEST], shoulders);
    G[B_NECK] = frame_from(r->p[RP_NECK], r->p[RP_HEAD] - r->p[RP_NECK], shoulders);
    G[B_HEAD] = frame_from(r->p[RP_NECK] + normalize(r->p[RP_HEAD] - r->p[RP_NECK]) * length(s.restPos[B_HEAD] - s.restPos[B_NECK]), r->p[RP_HEAD] - r->p[RP_NECK], shoulders);
    G[B_L_UPPERARM] = limb_mat(s, B_L_UPPERARM, B_L_FOREARM, r->p[RP_LSH], r->p[RP_LEL], r->yaw);
    G[B_L_FOREARM] = limb_mat(s, B_L_FOREARM, B_L_HAND, r->p[RP_LEL], r->p[RP_LWR], r->yaw);
    G[B_L_HAND] = G[B_L_FOREARM];
    G[B_L_HAND].m[12] = r->p[RP_LWR].x; G[B_L_HAND].m[13] = r->p[RP_LWR].y; G[B_L_HAND].m[14] = r->p[RP_LWR].z;
    G[B_R_UPPERARM] = limb_mat(s, B_R_UPPERARM, B_R_FOREARM, r->p[RP_RSH], r->p[RP_REL], r->yaw);
    G[B_R_FOREARM] = limb_mat(s, B_R_FOREARM, B_R_HAND, r->p[RP_REL], r->p[RP_RWR], r->yaw);
    G[B_R_HAND] = G[B_R_FOREARM];
    G[B_R_HAND].m[12] = r->p[RP_RWR].x; G[B_R_HAND].m[13] = r->p[RP_RWR].y; G[B_R_HAND].m[14] = r->p[RP_RWR].z;
    G[B_L_THIGH] = limb_mat(s, B_L_THIGH, B_L_SHIN, r->p[RP_LHIP], r->p[RP_LKNEE], r->yaw);
    G[B_L_SHIN] = limb_mat(s, B_L_SHIN, B_L_FOOT, r->p[RP_LKNEE], r->p[RP_LANK], r->yaw);
    G[B_L_FOOT] = G[B_L_SHIN];
    G[B_L_FOOT].m[12] = r->p[RP_LANK].x; G[B_L_FOOT].m[13] = r->p[RP_LANK].y; G[B_L_FOOT].m[14] = r->p[RP_LANK].z;
    G[B_R_THIGH] = limb_mat(s, B_R_THIGH, B_R_SHIN, r->p[RP_RHIP], r->p[RP_RKNEE], r->yaw);
    G[B_R_SHIN] = limb_mat(s, B_R_SHIN, B_R_FOOT, r->p[RP_RKNEE], r->p[RP_RANK], r->yaw);
    G[B_R_FOOT] = G[B_R_SHIN];
    G[B_R_FOOT].m[12] = r->p[RP_RANK].x; G[B_R_FOOT].m[13] = r->p[RP_RANK].y; G[B_R_FOOT].m[14] = r->p[RP_RANK].z;
    for (int b = 0; b < B_COUNT; b++) {
        a.globals[b] = G[b];
        a.skin[b] = G[b] * mat4_translate(-s.restPos[b]);
    }
    a.pos = vec3(r->p[RP_PELVIS].x, world_ground_height(r->p[RP_PELVIS] + vec3(0, 0.3f, 0), 0.1f, 0.5f), r->p[RP_PELVIS].z);
}

// ---------------------------------------------------------------------------
// Melee
// ---------------------------------------------------------------------------
static float melee_base_damage(const Actor& a) {
    if (a.kind == AK_PLAYER) return 15.0f + g.pd.upgrades[UP_MELEE] * 5.0f;
    switch (a.kind) {
    case AK_FIGHTER: return a.app.build > 1.25f ? 15.0f : 11.0f;
    case AK_STALKER: return 18.0f;
    case AK_BRUTE: return 34.0f;
    case AK_TENDRIL: return 15.0f;
    case AK_OVERSEER: return 40.0f;
    case AK_CIVILIAN: return 5.0f;
    default: return 9.0f;
    }
}

void combat_start_attack(Actor& a, int clip, bool heavy) {
    a.state = AS_ATTACK;
    a.stateTime = 0;
    a.attackClip = clip;
    a.attackHeavy = heavy;
    a.hitCount = 0;
    a.comboQueued = false;
    float mult = 1.0f;
    if (clip == CLIP_UPPERCUT) mult = 1.9f;
    else if (clip == CLIP_KICK) mult = 1.8f;
    else if (clip == CLIP_HAYMAKER) mult = 2.6f;
    else if (clip == CLIP_HOOK) mult = 1.25f;
    else if (clip == CLIP_ELBOW) mult = 1.3f;
    else if (clip == CLIP_STOMP) mult = 1.4f;
    else if (clip == CLIP_LEAP) mult = 1.3f;
    a.attackDamage = melee_base_damage(a) * mult;
    a.stamina -= heavy ? 14.0f : 7.0f;
    float speed = 1.0f;
    if (a.kind != AK_PLAYER) speed = 0.85f + a.skill * 0.25f;
    actor_set_clip(a, clip, 0.08f, speed);
    audio_play_at(SND_WHOOSH, a.pos + vec3(0, 1.3f, 0), heavy ? 0.8f : 0.5f, heavy ? 0.8f : 1.1f);
}

static float attack_reach(const Actor& a) {
    float r = 1.35f;
    if (a.attackClip == CLIP_KICK || a.attackClip == CLIP_HAYMAKER) r = 1.75f;
    if (a.app.body == BODY_STALKER) r = 2.1f;
    if (a.app.body == BODY_BRUTE) r = 2.6f;
    if (a.app.body == BODY_OVERSEER) r = 2.8f;
    if (a.attackClip == CLIP_LEAP) r = 2.4f;
    return r * (a.model ? a.model->skel.scale : 1.0f);
}

static void npc_state_timers(int idx, Actor& a, float dt) {
    if (idx == PLAYER || a.state == AS_DEAD) return;
    switch (a.state) {
    case AS_ATTACK: {
        const AnimClip* c = anim_clip(a.attackClip);
        if (!c || a.clipTime >= c->length / fmaxf(a.clipSpeed, 0.1f) * a.clipSpeed) a.state = AS_LOCOMOTION;
    } break;
    case AS_HIT:
        if (a.stateTime > 0.38f) a.state = AS_LOCOMOTION;
        break;
    case AS_STAGGER:
        if (a.stateTime > 0.85f) a.state = AS_LOCOMOTION;
        break;
    case AS_KNOCKDOWN:
        if (a.stateTime > 1.8f + (1.0f - a.skill)) {
            a.state = AS_GETUP;
            a.stateTime = 0;
            actor_set_clip(a, CLIP_GETUP);
        }
        break;
    case AS_GETUP:
        if (a.stateTime > 1.1f) { a.state = AS_LOCOMOTION; a.iframes = 0.4f; }
        break;
    case AS_BLOCK:
        if (a.stateTime > 0.9f) { a.state = AS_LOCOMOTION; a.clip = -1; }
        break;
    case AS_DODGE:
        if (a.stateTime > 0.45f) a.state = AS_LOCOMOTION;
        break;
    default:
        break;
    }
    FIGHTO_UNUSED(dt);
}

void combat_update_attack(int idx, Actor& a, float dt) {
    npc_state_timers(idx, a, dt);
    if (a.state != AS_ATTACK) return;
    const AnimClip* c = anim_clip(a.attackClip);
    if (!c || c->hitStart < 0) return;
    if (a.clipTime < c->hitStart || a.clipTime > c->hitEnd) return;
    float reach = attack_reach(a);
    vec3 fwd = dir_from_yaw(a.yaw);
    for (int j = 0; j < MAX_ACTORS; j++) {
        if (j == idx) continue;
        Actor& t = g.actors[j];
        if (!actor_alive(t) || t.state == AS_VEHICLE || t.kind == AK_DRONE || t.knockedOut > 0) continue;
        if (t.state == AS_TAKEN_DOWN) continue;
        bool already = false;
        for (int k = 0; k < a.hitCount; k++) if (a.hitIds[k] == j) already = true;
        if (already) continue;
        // Friendly fire rules: NPCs only hit their targets / enemies; the player hits anyone in front
        if (idx != PLAYER && j != a.target && !actor_is_enemy(a, t)) continue;
        vec3 d = t.pos - a.pos;
        if (fabsf(d.y) > 1.6f) continue;
        float dist = length(flat(d));
        if (dist > reach + t.radius) continue;
        vec3 dn = dist > 0.01f ? flat(d) / dist : fwd;
        if (dot(dn, fwd) < 0.3f) continue;
        if (a.hitCount < 6) a.hitIds[a.hitCount++] = j;
        int bone = (a.attackClip == CLIP_KICK || a.attackClip == CLIP_SWEEP) ? B_SPINE : (a.attackClip == CLIP_CROSS ? B_CHEST : B_HEAD);
        vec3 hitPos = bone == B_HEAD ? actor_head(t) : actor_chest(t);
        // Blocking & parrying
        vec3 tf = dir_from_yaw(t.yaw);
        bool facing = dot(tf, -dn) > 0.3f;
        if (t.state == AS_BLOCK && facing && a.app.body != BODY_BRUTE) {
            if (g.time - t.blockStart < 0.2f) {
                // Perfect parry: attacker staggers
                a.state = AS_STAGGER;
                a.stateTime = 0;
                actor_set_clip(a, CLIP_STAGGER);
                audio_play_at(SND_BLOCK, hitPos, 1.0f, 1.3f);
                if (j == PLAYER || idx == PLAYER) { g.slowMo = 0.25f; camera_shake(0.25f); }
                if (j == PLAYER) notify("PARRY!", vec4(1.0f, 0.85f, 0.3f, 1));
                return;
            }
            t.stamina -= a.attackHeavy ? 28.0f : 14.0f;
            audio_play_at(SND_BLOCK, hitPos, 0.8f, 1.0f);
            t.knockVel = dn * 1.5f;
            if (t.stamina <= 0) {
                t.stamina = 0;
                t.state = AS_STAGGER;
                t.stateTime = 0;
                actor_set_clip(t, CLIP_STAGGER);
                if (j == PLAYER) notify("Guard broken", vec4(1, 0.4f, 0.3f, 1));
            } else {
                combat_damage(j, idx, a.attackDamage * 0.12f, hitPos, dn, DMG_MELEE, bone);
            }
            continue;
        }
        if (t.iframes > 0) continue;
        float dmg = a.attackDamage;
        if (idx != PLAYER && j == PLAYER) dmg *= g.settings.difficulty == 0 ? 0.55f : g.settings.difficulty == 2 ? 1.25f : 0.85f;
        // Stealthy hits on unaware targets hurt more
        if (idx == PLAYER && t.aiMode != AI_COMBAT && t.alert < 0.5f) dmg *= 1.5f;
        combat_damage(j, idx, dmg, hitPos, normalize(dn + vec3(0, a.attackClip == CLIP_UPPERCUT ? 0.8f : 0.15f, 0)), a.faction == FAC_ALIEN && a.app.body != BODY_HUMAN ? DMG_CLAW : DMG_MELEE, bone);
        bool heavy = a.attackHeavy;
        audio_play_at(heavy ? SND_PUNCH_HEAVY : (a.attackClip == CLIP_KICK ? SND_KICK : SND_PUNCH), hitPos, 1.0f, g.rng.range(0.9f, 1.1f));
        if (heavy && g.rng.chance(0.4f)) audio_play_at(SND_BONE_CRACK, hitPos, 0.7f);
        if (idx == PLAYER || j == PLAYER) {
            camera_shake(heavy ? 0.4f : 0.18f);
            g.slowMo = fmaxf(g.slowMo, heavy ? 0.09f : 0.04f);
        }
    }
}

// ---------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------
static void drop_weapon(Actor& v) {
    if (v.weapon <= W_FISTS || v.weapon >= W_CLAWS) return;
    if (v.weapon == W_ARC && v.faction == FAC_AEGIS && g.pd.phase < 1) return;
    pickup_spawn(PK_WEAPON, v.pos + vec3(g.rng.range(-0.5f, 0.5f), 0.1f, g.rng.range(-0.5f, 0.5f)), v.weapon, g_weapons[v.weapon].clipSize * 2, "");
    v.weapon = W_FISTS;
}

static void kill_actor(int victim, int attacker, vec3 dir, float dmg, int kind, int bone) {
    Actor& v = g.actors[victim];
    v.state = AS_DEAD;
    v.health = 0;
    v.deathTime = g.time;
    v.clip = -1;
    v.aimBlend = 0;
    if (g.lockTarget == victim) g.lockTarget = -1;
    bool alien = v.faction == FAC_ALIEN;
    if (v.kind == AK_DRONE) {
        explosion(v.pos, 3.0f, 25.0f, attacker, true);
        v.active = false;
        return;
    }
    // Ragdoll with an impulse from the killing blow
    float force = kind == DMG_EXPLOSION ? 9.0f : kind == DMG_VEHICLE ? fminf(dmg * 0.12f, 14.0f) : kind == DMG_HEADSHOT ? 4.0f : kind == DMG_MELEE ? 4.5f : 2.5f;
    vec3 impulse = dir * force + vec3(0, kind == DMG_EXPLOSION || kind == DMG_VEHICLE ? force * 0.5f : 0.6f, 0);
    if (v.app.body == BODY_HUMAN || v.app.body == BODY_STALKER) ragdoll_start(v, impulse);
    if (!v.ragdoll) actor_set_clip(v, dot(dir, dir_from_yaw(v.yaw)) > 0 ? CLIP_DEATH_FORWARD : CLIP_DEATH_BACK, 0.05f);
    drop_weapon(v);
    v.bleed = alien ? 4.0f : 8.0f;
    if (alien) audio_play_at(SND_ALIEN_SCREECH, v.pos, 0.9f, 0.7f);
    else if (g.rng.chance(0.6f)) audio_play_at(SND_SCREAM, v.pos, 0.7f, g.rng.range(0.85f, 1.15f));
    audio_play_at(SND_BODYFALL, v.pos, 0.8f);
    if (attacker == PLAYER) {
        g.pd.kills++;
        g.killMarker = 0.4f;
        if (v.faction == FAC_CIVILIAN && !v.hostileToPlayer) { wanted_crime(CRIME_MURDER, v.pos, false); rep_add(FAC_CIVILIAN, -3); }
        if (v.faction == FAC_POLICE) { wanted_crime(CRIME_COP_MURDER, v.pos, true); rep_add(FAC_POLICE, -10); }
        if (v.faction == FAC_KINGS) rep_add(FAC_KINGS, -4);
        if (v.faction == FAC_VARGA) rep_add(FAC_VARGA, -4);
        if (v.faction == FAC_ALIEN) rep_add(FAC_AEGIS, 1);
        if (kind == DMG_HEADSHOT) notify("HEADSHOT", vec4(1.0f, 0.3f, 0.2f, 1));
    }
    // Gory finishing touches
    if (fx_gore_level() > 0) {
        if (kind == DMG_HEADSHOT || dmg > 70.0f) {
            combat_blood_burst(actor_head(v), dir, 3.5f, alien);
            wall_splatter(actor_head(v), dir, 3.0f, alien);
            v.blood = 1.0f;
        }
        if (attacker == PLAYER && length(g.cam.pos - v.pos) < 3.0f && (kind == DMG_MELEE || kind == DMG_HEADSHOT || kind == DMG_BULLET) && fx_gore_level() == 2) {
            g.bloodScreen = fminf(1.0f, g.bloodScreen + 0.5f);
            g.bloodSeed = g.rng.range(0, 100);
        }
    }
    ai_alert_area(v.pos, 25.0f, attacker, 1);
    FIGHTO_UNUSED(bone);
}

void combat_damage(int victim, int attacker, float dmg, vec3 hitPos, vec3 dir, int kind, int bone) {
    if (victim < 0 || victim >= MAX_ACTORS) return;
    Actor& v = g.actors[victim];
    if (!v.active) return;
    bool alien = v.faction == FAC_ALIEN;
    vec3 d = length_sq(dir) > 1e-6f ? normalize(dir) : vec3(0, 0, 1);
    // Corpses still react to gunfire
    if (v.state == AS_DEAD) {
        if (kind != DMG_FALL) {
            combat_blood_burst(hitPos, d, kind == DMG_HEADSHOT ? 2.0f : 0.8f, alien);
            if (v.ragdoll) {
                v.ragdoll->sleeping = false;
                v.ragdoll->still = 0;
                v.ragdoll->time = fminf(v.ragdoll->time, 6.0f);
                for (int i = 0; i < RP_COUNT; i++) v.ragdoll->prev[i] -= d * 0.03f;
            }
        }
        return;
    }
    if (victim == PLAYER && (g.cutscene || v.invulnerable)) return;
    if (v.invulnerable) dmg = 0;
    if (kind == DMG_MELEE && v.iframes > 0) return;
    if (v.armor > 0 && kind != DMG_FALL) {
        float absorbed = fminf(v.armor, dmg * 0.6f);
        v.armor -= absorbed;
        dmg -= absorbed;
    }
    if (v.app.body == BODY_BRUTE && kind == DMG_MELEE && attacker == PLAYER) dmg *= 0.5f;
    v.health -= dmg;
    v.lastDamageTime = g.time;
    v.lastAttacker = attacker;
    v.hitFlash = 1.0f;
    float amount = kind == DMG_HEADSHOT ? 2.5f : kind == DMG_MELEE ? dmg / 22.0f : kind == DMG_FALL ? 0.3f : dmg / 16.0f;
    if (kind == DMG_ENERGY && !alien) amount *= 0.5f;
    amount = fminf(amount, 4.0f);
    if (kind != DMG_FALL && kind != DMG_FIRE) {
        combat_blood_burst(hitPos, d, amount, alien);
        if (kind == DMG_BULLET || kind == DMG_HEADSHOT || kind == DMG_CLAW) {
            // Exit wound and wall splatter behind the victim
            combat_blood_burst(hitPos + d * 0.25f, d, amount * 0.8f, alien);
            wall_splatter(hitPos, d, amount, alien);
        }
        if (kind == DMG_MELEE && fx_gore_level() > 0) {
            // Spit / nose blood towards the ground
            float gy = world_ground_height(v.pos + vec3(0, 0.3f, 0), 0.1f, 0.6f);
            if (g.rng.chance(0.5f)) fx_decal(DT_BLOOD_SPLAT, vec3(v.pos.x + d.x * 0.6f, gy + 0.012f, v.pos.z + d.z * 0.6f), vec3(0, 1, 0), g.rng.range(0.15f, 0.35f), kBlood, g.rng.f01());
        }
        v.blood = fminf(1.0f, v.blood + amount * 0.09f);
        v.bleed = fminf(12.0f, v.bleed + dmg * 0.05f);
    }
    if (victim == PLAYER) {
        g.damageFlash = fminf(1.0f, g.damageFlash + dmg / 40.0f);
        if (kind != DMG_FALL) {
            g.bloodScreen = fminf(1.0f, g.bloodScreen + dmg / 70.0f);
            if (g.bloodScreen > 0.3f) g.bloodSeed = g.rng.range(0, 100);
        }
        camera_shake(fminf(0.6f, dmg / 40.0f));
        audio_play(SND_GRUNT, 0.6f, g.rng.range(0.9f, 1.1f));
    } else {
        // Becoming hostile when attacked by the player
        if (attacker == PLAYER) {
            if (v.faction == FAC_POLICE) wanted_crime(CRIME_COP_ASSAULT, v.pos, true);
            else if (v.faction == FAC_CIVILIAN && !v.hostileToPlayer) wanted_crime(CRIME_ASSAULT, v.pos, false);
        }
        if (attacker >= 0 && v.kind != AK_CIVILIAN && v.aiMode != AI_FLEE) ai_make_hostile(victim, attacker);
        else if (attacker >= 0 && v.kind == AK_CIVILIAN) { v.aiMode = g.rng.chance(0.3f) ? AI_COWER : AI_FLEE; v.target = attacker; v.aiTimer = 8.0f; }
    }
    if (v.health <= 0.0f) {
        if (v.nonLethal) {
            v.health = 0;
            v.knockedOut = 1.0f;
            v.state = AS_DEAD;
            v.deathTime = g.time;
            v.poolSpawned = true;
            actor_set_clip(v, CLIP_KNOCKDOWN, 0.05f);
            v.knockVel = d * 3.0f;
            audio_play_at(SND_BODYFALL, v.pos);
            audio_play(SND_CROWD_CHEER, 0.8f);
            if (attacker == PLAYER) g.pd.knockouts++;
            return;
        }
        kill_actor(victim, attacker, d, dmg, kind, bone);
        return;
    }
    // Hit reactions
    bool big = v.app.body == BODY_BRUTE || v.app.body == BODY_OVERSEER;
    if (kind == DMG_FALL) return;
    if (kind == DMG_EXPLOSION || kind == DMG_VEHICLE) {
        if (!big) {
            v.state = AS_KNOCKDOWN;
            v.stateTime = 0;
            v.knockVel = d * 6.0f;
            v.vel = d * 6.0f + vec3(0, 3.0f, 0);
            actor_set_clip(v, CLIP_KNOCKDOWN, 0.05f);
        }
        return;
    }
    if (v.state == AS_KNOCKDOWN || v.state == AS_GETUP || v.state == AS_TAKEN_DOWN) return;
    if (big && dmg < 60.0f) return;
    if (kind == DMG_MELEE || kind == DMG_CLAW) {
        bool heavy = dmg > 24.0f;
        if (heavy && (g.rng.chance(0.35f) || v.health < v.maxHealth * 0.3f)) {
            v.state = AS_KNOCKDOWN;
            v.knockVel = d * 4.5f;
            actor_set_clip(v, CLIP_KNOCKDOWN, 0.05f);
            audio_play_at(SND_BODYFALL, v.pos, 0.7f);
        } else if (heavy) {
            v.state = AS_STAGGER;
            v.knockVel = d * 3.0f;
            actor_set_clip(v, CLIP_STAGGER, 0.05f);
        } else {
            v.state = AS_HIT;
            v.knockVel = d * 1.8f;
            actor_set_clip(v, bone == B_HEAD ? CLIP_HIT_HEAD : CLIP_HIT_FRONT, 0.04f);
        }
        v.stateTime = 0;
        if (victim != PLAYER) v.vel = v.vel + d * 2.0f;
    } else {
        // Bullet flinch without fully interrupting
        if (v.state == AS_LOCOMOTION && g.rng.chance(0.5f)) {
            actor_set_clip(v, bone == B_HEAD ? CLIP_HIT_HEAD : CLIP_HIT_FRONT, 0.04f, 1.4f);
        }
        v.vel = v.vel + d * 0.8f;
    }
}

void combat_update(float dt) {
    // Fade screen blood & damage flashes
    g.damageFlash = fmaxf(0.0f, g.damageFlash - dt * 1.5f);
    g.bloodScreen = fmaxf(0.0f, g.bloodScreen - dt * 0.12f);
    g.hitMarker = fmaxf(0.0f, g.hitMarker - dt);
    g.killMarker = fmaxf(0.0f, g.killMarker - dt);
    // Knocked out fighters slowly come round after the fight
    for (int i = 1; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!a.active || a.knockedOut <= 0) continue;
        a.knockVel = a.knockVel * (1.0f - fminf(1.0f, dt * 3.0f));
        if (!a.ragdoll && a.clip < 0) actor_set_clip(a, CLIP_DEAD_BACK);
    }
}
