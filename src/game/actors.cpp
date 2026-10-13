// FIGHTO - actors: spawning, movement physics, animation blending and rendering.
#include "game.h"
#include "../audio/audio.h"

struct KindDef {
    float health;
    int weapon;
    int faction;
    float radius;
    float accuracy;
    float aggression;
    float skill;
    const char* preset;
};

static const KindDef g_kinds[AK_COUNT] = {
    {100, W_FISTS, FAC_PLAYER, 0.35f, 1.0f, 1.0f, 1.0f, "damian"},
    {40, W_FISTS, FAC_CIVILIAN, 0.33f, 0.2f, 0.1f, 0.1f, "civilian"},
    {75, W_FISTS, FAC_KINGS, 0.35f, 0.3f, 0.7f, 0.45f, "kings"},
    {70, W_PISTOL, FAC_KINGS, 0.35f, 0.45f, 0.6f, 0.4f, "kings"},
    {130, W_FISTS, FAC_FIGHTER, 0.36f, 0.3f, 0.8f, 0.6f, "fighter"},
    {90, W_PISTOL, FAC_POLICE, 0.35f, 0.55f, 0.6f, 0.5f, "police"},
    {150, W_ARC, FAC_AEGIS, 0.36f, 0.75f, 0.7f, 0.7f, "aegis"},
    {130, W_RIFLE, FAC_OPERATIVES, 0.36f, 0.65f, 0.75f, 0.7f, "operative"},
    {170, W_CLAWS, FAC_ALIEN, 0.42f, 0.5f, 0.95f, 0.7f, "stalker"},
    {650, W_CLAWS, FAC_ALIEN, 0.75f, 0.4f, 0.9f, 0.6f, "brute"},
    {280, W_CLAWS, FAC_ALIEN, 0.45f, 0.4f, 0.85f, 0.5f, "tendril"},
    {2400, W_LANCE, FAC_ALIEN, 0.6f, 0.7f, 0.9f, 0.8f, "overseer"},
    {90, W_DRONE_GUN, FAC_ALIEN, 0.6f, 0.55f, 0.8f, 0.6f, "stalker"},
    {100, W_FISTS, FAC_CIVILIAN, 0.35f, 0.5f, 0.3f, 0.5f, "civilian"},
};

static Mesh g_droneMesh;
static bool g_droneMeshBuilt;

static void build_drone_mesh() {
    MeshBuilder mb;
    memset(&mb, 0, sizeof(mb));
    mb.reset();
    // Manta-ray shaped alien drone: swept wings, central hull, glowing eyes
    mb.set_mat(MAT_ALIEN, MF_METAL, 0.6f);
    mb.set_color(vec3(0.35f, 0.33f, 0.4f));
    mb.ellipsoid(vec3(0, 0, 0), vec3(0.35f, 0.18f, 0.7f), 12, 8);
    for (int s = -1; s <= 1; s += 2) {
        vec3 a(0.2f * s, 0, 0.35f), b(1.4f * s, -0.05f, -0.35f), c(0.9f * s, 0.0f, -0.55f), d(0.25f * s, 0.05f, -0.5f);
        if (s > 0) { mb.quad_auto(a, b, c, d); mb.quad_auto(d, c, b, a); }
        else { mb.quad_auto(d, c, b, a); mb.quad_auto(a, b, c, d); }
    }
    mb.set_mat(MAT_PAINT, MF_EMISSIVE | MF_PULSE, 1.0f, 18.0f);
    mb.set_color(vec3(0.3f, 0.8f, 1.0f));
    mb.ellipsoid(vec3(0.12f, -0.02f, 0.62f), vec3(0.06f, 0.04f, 0.05f), 6, 4);
    mb.ellipsoid(vec3(-0.12f, -0.02f, 0.62f), vec3(0.06f, 0.04f, 0.05f), 6, 4);
    mb.ellipsoid(vec3(0, -0.15f, 0.0f), vec3(0.12f, 0.05f, 0.25f), 8, 4);
    g_droneMesh = mesh_create(mb);
    mb.free_mem();
    g_droneMeshBuilt = true;
}

int actor_spawn(int kind, vec3 pos, float yaw, const Appearance& app, int faction) {
    int slot = -1;
    if (kind == AK_PLAYER) slot = PLAYER;
    else
        for (int i = 1; i < MAX_ACTORS; i++)
            if (!g.actors[i].active) { slot = i; break; }
    if (slot < 0) return -1;
    Actor& a = g.actors[slot];
    if (a.ragdoll) ragdoll_free(a);
    memset(&a, 0, sizeof(a));
    const KindDef& k = g_kinds[kind];
    a.active = true;
    a.kind = (u8)kind;
    a.faction = (u8)(faction >= 0 ? faction : k.faction);
    a.app = app;
    a.model = kind == AK_DRONE ? 0 : character_model_get(app);
    a.pos = pos;
    a.home = pos;
    a.yaw = a.desiredYaw = yaw;
    a.radius = k.radius;
    a.height = app.height;
    float diff = g.settings.difficulty == 0 ? 0.75f : g.settings.difficulty == 2 ? 1.3f : 1.0f;
    a.maxHealth = a.health = kind == AK_PLAYER ? 100.0f : k.health * (a.faction == FAC_CIVILIAN ? 1.0f : diff);
    a.maxStamina = a.stamina = 100.0f;
    a.weapon = k.weapon;
    a.clipAmmo = g_weapons[a.weapon].clipSize;
    a.accuracy = k.accuracy * (g.settings.difficulty == 0 ? 0.7f : g.settings.difficulty == 2 ? 1.2f : 1.0f);
    a.aggression = k.aggression;
    a.skill = k.skill;
    a.clip = -1;
    a.fromClip = -1;
    a.clipSpeed = 1.0f;
    a.target = -1;
    a.vehicle = -1;
    a.navNode = -1;
    a.navPrev = -1;
    a.lastAttacker = -1;
    a.lastDamageTime = -100;
    a.locoPhase = g.rng.f01();
    a.state = AS_LOCOMOTION;
    a.aiMode = kind == AK_CIVILIAN ? AI_WANDER : AI_STAND;
    a.grounded = true;
    pose_identity(&a.pose);
    if (kind == AK_DRONE && !g_droneMeshBuilt) build_drone_mesh();
    return slot;
}

int actor_spawn_preset(int kind, const char* preset, vec3 pos, float yaw, u32 seed) {
    Appearance app = appearance_preset(preset ? preset : g_kinds[kind].preset, seed);
    return actor_spawn(kind, pos, yaw, app, -1);
}

void actor_despawn(int idx) {
    if (idx <= 0 || idx >= MAX_ACTORS) return;
    Actor& a = g.actors[idx];
    if (a.ragdoll) ragdoll_free(a);
    if (a.vehicle >= 0 && a.vehicle < MAX_VEHICLES && g.vehicles[a.vehicle].driver == idx) g.vehicles[a.vehicle].driver = -1;
    a.active = false;
    if (g.lockTarget == idx) g.lockTarget = -1;
}

void actor_set_clip(Actor& a, int clip, float blend, float speed) {
    if (a.clip >= 0) {
        a.fromClip = a.clip;
        a.fromTime = a.clipTime;
    } else {
        a.fromClip = -1;
    }
    a.clip = clip;
    a.clipTime = 0;
    a.clipSpeed = speed;
    a.blend = blend > 0 ? 0.0f : 1.0f;
    FIGHTO_UNUSED(blend);
}

bool actor_alive(const Actor& a) { return a.active && a.state != AS_DEAD && a.health > 0; }

bool is_hostile_faction(int f) {
    switch (f) {
    case FAC_ALIEN: case FAC_OPERATIVES: return true;
    case FAC_KINGS: return g.pd.rep[FAC_KINGS] < -20;
    case FAC_VARGA: return g.pd.rep[FAC_VARGA] < -20;
    case FAC_POLICE: return g.wanted.level > 0;
    default: return false;
    }
}

bool actor_is_enemy(const Actor& a, const Actor& b) {
    if (&a == &b) return false;
    if (a.faction == FAC_ALIEN && b.faction != FAC_ALIEN) return true;
    if (b.faction == FAC_ALIEN && a.faction != FAC_ALIEN) return true;
    if (a.kind == AK_PLAYER) return b.hostileToPlayer || is_hostile_faction(b.faction);
    if (b.kind == AK_PLAYER) return a.hostileToPlayer || is_hostile_faction(a.faction);
    if ((a.faction == FAC_AEGIS && b.faction == FAC_OPERATIVES) || (b.faction == FAC_AEGIS && a.faction == FAC_OPERATIVES)) return false;
    if ((a.faction == FAC_KINGS && b.faction == FAC_VARGA) || (a.faction == FAC_VARGA && b.faction == FAC_KINGS)) return a.hostileToPlayer && b.hostileToPlayer ? false : (a.aiMode == AI_COMBAT && b.aiMode == AI_COMBAT);
    return false;
}

int actor_find_tag(const char* tag) {
    for (int i = 0; i < MAX_ACTORS; i++)
        if (g.actors[i].active && str_eq(g.actors[i].tag, tag)) return i;
    return -1;
}

int actor_count_tag(const char* tag, bool aliveOnly) {
    int n = 0;
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor& a = g.actors[i];
        if (!a.active || !str_eq(a.tag, tag)) continue;
        if (aliveOnly && (!actor_alive(a) || a.knockedOut > 0)) continue;
        n++;
    }
    return n;
}

float actor_dist(int a, int b) { return length(g.actors[a].pos - g.actors[b].pos); }

void actor_face(Actor& a, vec3 p, float rate, float dt) {
    vec3 d = p - a.pos;
    if (d.x * d.x + d.z * d.z < 1e-4f) return;
    a.desiredYaw = atan2f(d.x, d.z);
    a.yaw = angle_approach(a.yaw, a.desiredYaw, rate * dt);
}

vec3 actor_head(const Actor& a) {
    if (!a.model) return a.pos + vec3(0, 0.3f, 0);
    if (a.ragdoll) return transform_point(a.globals[B_HEAD], vec3(0, 0.1f, 0));
    return joint_world(a.world, a.globals, B_HEAD, vec3(0, 0.1f * a.model->skel.scale, 0));
}
vec3 actor_chest(const Actor& a) {
    if (!a.model) return a.pos;
    if (a.ragdoll) return transform_point(a.globals[B_CHEST], vec3(0, 0, 0));
    return joint_world(a.world, a.globals, B_CHEST, vec3(0, 0.05f, 0));
}
vec3 actor_hand(const Actor& a, bool right) {
    if (!a.model) return a.pos;
    int b = right ? B_R_HAND : B_L_HAND;
    if (a.ragdoll) return transform_point(a.globals[b], vec3(0, -0.07f, 0));
    return joint_world(a.world, a.globals, b, vec3(0, -0.07f * a.model->skel.scale, 0));
}

// Separation between characters and pushing out of vehicles.
static void resolve_actor_overlap(int idx, Actor& a) {
    for (int j = 0; j < MAX_ACTORS; j++) {
        if (j == idx) continue;
        Actor& b = g.actors[j];
        if (!b.active || b.state == AS_DEAD || b.state == AS_VEHICLE || b.kind == AK_DRONE) continue;
        if (fabsf(b.pos.y - a.pos.y) > 1.5f) continue;
        float dx = a.pos.x - b.pos.x, dz = a.pos.z - b.pos.z;
        float r = a.radius + b.radius;
        float d2 = dx * dx + dz * dz;
        if (d2 >= r * r || d2 < 1e-8f) continue;
        float d = sqrtf(d2);
        float push = (r - d) * 0.5f;
        a.pos.x += dx / d * push;
        a.pos.z += dz / d * push;
    }
    for (int v = 0; v < MAX_VEHICLES; v++) {
        const Vehicle& ve = g.vehicles[v];
        if (!ve.active || a.vehicle == v) continue;
        const VehicleDef& vd = g_vehicleDefs[ve.type];
        vec3 rel = a.pos - ve.pos;
        if (fabsf(rel.y) > 2.0f) continue;
        float c = cosf(ve.yaw), s = sinf(ve.yaw);
        // local: x right(-x world?), use consistent rotation: forward = (sin, 0, cos)
        float lx = rel.x * c - rel.z * s;
        float lz = rel.x * s + rel.z * c;
        float hx = vd.halfExtents.x + a.radius, hz = vd.halfExtents.z + a.radius;
        if (fabsf(lx) < hx && fabsf(lz) < hz) {
            float px = hx - fabsf(lx), pz = hz - fabsf(lz);
            if (px < pz) lx = signf(lx) * hx;
            else lz = signf(lz) * hz;
            vec3 nr(lx * c + lz * s, 0, -lx * s + lz * c);
            a.pos.x = ve.pos.x + nr.x;
            a.pos.z = ve.pos.z + nr.z;
        }
    }
}

bool actor_move(Actor& a, vec3 wish, float dt, float accel) {
    if (a.kind == AK_DRONE) {
        a.vel = lerp(a.vel, wish, saturate(accel * dt * 0.5f));
        vec3 np = a.pos + a.vel * dt;
        if (world_overlap_cylinder(np - vec3(0, 0.3f, 0), a.radius, 0.6f)) np = a.pos;
        a.pos = np;
        return false;
    }
    float k = saturate(accel * dt);
    a.vel.x = lerpf(a.vel.x, wish.x, k);
    a.vel.z = lerpf(a.vel.z, wish.z, k);
    a.vel.y -= 18.0f * dt;
    if (a.vel.y < -40.0f) a.vel.y = -40.0f;
    vec3 delta = a.vel * dt;
    float gy;
    float prevVy = a.vel.y;
    bool wasGrounded = a.grounded;
    a.grounded = world_move_cylinder(&a.pos, delta, a.radius, fmaxf(a.height * 0.9f, 1.2f), 0.45f, &gy);
    if (a.grounded) {
        if (!wasGrounded && prevVy < -13.0f) {
            float dmg = (-prevVy - 13.0f) * 6.0f;
            combat_damage((int)(&a - g.actors), -1, dmg, a.pos, vec3(0, -1, 0), DMG_FALL, B_PELVIS);
        }
        a.vel.y = 0;
    }
    // Keep inside the world
    a.pos.x = clampf(a.pos.x, -WORLD_LIMIT - 200, WORLD_LIMIT + 200);
    a.pos.z = clampf(a.pos.z, -WORLD_LIMIT - 200, WORLD_LIMIT + 200);
    if (a.pos.y < -200.0f) a.pos = a.home;
    return a.grounded;
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------
static bool in_stance(const Actor& a) {
    if (a.weapon != W_FISTS) return false;
    if (a.app.body != BODY_HUMAN) return false;
    if (a.kind == AK_PLAYER) return g.time - a.lastDamageTime < 4.0f || a.aiTimer2 > 0 || g.lockTarget >= 0;
    return a.aiMode == AI_COMBAT && a.target >= 0 && a.kind != AK_CIVILIAN;
}

static bool upper_body_clip(int clip) {
    return clip == CLIP_AIM_PISTOL || clip == CLIP_AIM_RIFLE || clip == CLIP_RELOAD || clip == CLIP_TALK || clip == CLIP_POINT;
}

void actor_update_pose(Actor& a, float dt) {
    if (!a.model) return;
    if (a.ragdoll) {
        ragdoll_update(a, dt);
        return;
    }
    // Base layer
    Pose base;
    if (a.state == AS_VEHICLE) {
        pose_sample_clip(&base, anim_clip(CLIP_DRIVE), g.time);
    } else {
        float sp = a.speed;
        int body = a.app.body;
        if (sp > 0.25f || a.crouch > 0.05f || !a.grounded) {
            float strideLen = (body == BODY_HUMAN ? 1.45f : 1.9f) * a.model->skel.scale * (sp > 4.0f ? 1.5f : 1.0f);
            a.locoPhase += dt * sp / fmaxf(strideLen, 0.5f) * 0.5f;
            if (a.locoPhase > 1.0f) a.locoPhase -= 1.0f;
            pose_locomotion(&base, a.locoPhase, sp, a.crouch, 0.0f, body);
            if (sp < 0.6f && a.crouch < 0.05f) {
                Pose idle;
                pose_sample_clip(&idle, anim_clip(in_stance(a) ? CLIP_FIGHT_IDLE : CLIP_IDLE), g.time + a.home.x);
                pose_blend(&base, idle, base, saturate((sp - 0.25f) / 0.35f));
            }
            if (!a.grounded && a.vel.y < -3.0f && a.state == AS_LOCOMOTION) {
                Pose fall;
                pose_sample_clip(&fall, anim_clip(CLIP_FALL), g.time);
                pose_blend(&base, base, fall, saturate((-a.vel.y - 3.0f) / 4.0f));
            }
        } else {
            if (a.app.body == BODY_HUMAN) pose_sample_clip(&base, anim_clip(in_stance(a) ? CLIP_FIGHT_IDLE : CLIP_IDLE), g.time + a.home.x);
            else pose_locomotion(&base, 0.0f, 0.0f, 0.0f, 0.0f, a.app.body);
        }
        // Stance overlay while moving in a fight
        if (in_stance(a) && sp > 0.25f) {
            Pose st;
            pose_sample_clip(&st, anim_clip(CLIP_FIGHT_IDLE), g.time);
            pose_blend_upper(&base, base, st, 0.85f);
        }
    }
    // Action layer
    Pose out = base;
    if (a.clip >= 0) {
        const AnimClip* c = anim_clip(a.clip);
        if (c) {
            a.clipTime += dt * a.clipSpeed;
            Pose act;
            pose_sample_clip(&act, c, a.clipTime);
            a.blend = fminf(1.0f, a.blend + dt / 0.1f);
            float w = a.blend;
            if (!c->loop && a.clipTime > c->length) {
                // Blend out over 0.15 s after the clip ends (except holding poses)
                float o = (a.clipTime - c->length) / 0.15f;
                bool hold = a.clip == CLIP_DEATH_BACK || a.clip == CLIP_DEATH_FORWARD || a.clip == CLIP_KNOCKDOWN;
                if (!hold) w *= 1.0f - saturate(o);
                if (!hold && o >= 1.0f) a.clip = -1;
            }
            if (a.fromClip >= 0 && a.blend < 1.0f) {
                Pose prev;
                pose_sample_clip(&prev, anim_clip(a.fromClip), a.fromTime);
                Pose mixed;
                pose_blend(&mixed, prev, act, a.blend);
                act = mixed;
                w = 1.0f;
            }
            if (upper_body_clip(a.clip)) pose_blend_upper(&out, base, act, w);
            else pose_blend(&out, base, act, w);
        } else {
            a.clip = -1;
        }
    }
    // Aim overlay for firearms
    if (a.aimBlend > 0.01f && a.clip < 0 && a.state != AS_VEHICLE) {
        Pose aim;
        bool rifle = g_weapons[a.weapon].twoHanded;
        pose_sample_clip(&aim, anim_clip(rifle ? CLIP_AIM_RIFLE : CLIP_AIM_PISTOL), 0);
        pose_blend_upper(&out, out, aim, a.aimBlend);
    }
    a.pose = out;
    a.world = mat4_trs(a.pos, quat_axis_angle(vec3(0, 1, 0), a.yaw), vec3(1, 1, 1));
    pose_compute(a.model->skel, a.pose, a.globals, a.skin);
}

void actors_update(float dt) {
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!a.active) continue;
        a.stateTime += dt;
        a.hitFlash = fmaxf(0.0f, a.hitFlash - dt * 4.0f);
        a.muzzleFlash = fmaxf(0.0f, a.muzzleFlash - dt);
        a.iframes = fmaxf(0.0f, a.iframes - dt);
        a.fireCooldown = fmaxf(0.0f, a.fireCooldown - dt);
        a.attackCooldown = fmaxf(0.0f, a.attackCooldown - dt);
        a.shout = fmaxf(0.0f, a.shout - dt);
        if (i == PLAYER) player_update(dt);
        else if (a.state != AS_DEAD) ai_update(i, a, dt);
        else {
            // Dead bodies: settle, bleed out, eventually despawn when far away
            if (!a.ragdoll) {
                a.vel.x *= 0.9f;
                a.vel.z *= 0.9f;
                actor_move(a, vec3(0, 0, 0), dt, 2.0f);
            }
            a.speed = 0;
            if (!a.poolSpawned && g.time - a.deathTime > 0.8f) {
                a.poolSpawned = true;
                vec3 p = a.ragdoll ? transform_point(a.globals[B_CHEST], vec3(0, 0, 0)) : a.pos;
                combat_blood_pool(vec3(p.x, a.pos.y, p.z), a.faction == FAC_ALIEN, a.app.body == BODY_BRUTE ? 2.4f : 1.5f);
            }
            if (!a.persistent && g.time - a.deathTime > 90.0f && length(a.pos - g.actors[PLAYER].pos) > 60.0f) actor_despawn(i);
        }
        if (a.active && a.state != AS_VEHICLE && a.state != AS_DEAD && a.kind != AK_DRONE) resolve_actor_overlap(i, a);
        if (a.active) {
            combat_update_attack(i, a, dt);
            // Bleeding wounds leave drips
            if (a.bleed > 0 && a.state != AS_VEHICLE && fx_gore_level() > 0) {
                a.bleed -= dt;
                if (g.rng.chance(dt * 6.0f)) {
                    Particle* p = fx_spawn(PT_BLOOD);
                    if (p) {
                        p->pos = a.ragdoll ? actor_chest(a) : a.pos + vec3(g.rng.range(-0.2f, 0.2f), a.height * 0.5f, g.rng.range(-0.2f, 0.2f));
                        p->vel = vec3(g.rng.range(-0.3f, 0.3f), -0.5f, g.rng.range(-0.3f, 0.3f));
                        p->gravity = 9.8f;
                        p->size = g.rng.range(0.012f, 0.025f);
                        p->life = p->maxLife = 2.0f;
                        p->color = a.faction == FAC_ALIEN ? vec3(0.3f, 0.95f, 0.5f) : vec3(0.45f, 0.02f, 0.02f);
                        p->spawnDecal = a.faction == FAC_ALIEN ? 2 : 1;
                        p->stretch = 0.03f;
                    }
                }
            }
            float dist = length(a.pos - g.cam.pos);
            // Animation LOD: far actors update less often
            if (dist < 70.0f || (g.frame + i) % 3 == 0 || a.ragdoll) actor_update_pose(a, dist < 70.0f ? dt : dt * 3.0f);
            else a.world = mat4_trs(a.pos, quat_axis_angle(vec3(0, 1, 0), a.yaw), vec3(1, 1, 1));
        }
    }
}

void actors_render() {
    vec3 cam = g.cam.pos;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor& a = g.actors[i];
        if (!a.active) continue;
        if (a.state == AS_VEHICLE && i != PLAYER && length(a.pos - cam) > 30.0f) continue;
        float d = length(a.pos - cam);
        if (d > 160.0f) continue;
        if (i == PLAYER && g.cam.dist < 0.6f) continue;
        DrawItem di;
        memset(&di, 0, sizeof(di));
        di.tint = vec4(1, 1, 1, 1);
        di.blood = a.blood;
        di.bloodColor = a.faction == FAC_ALIEN ? vec3(0.25f, 0.95f, 0.5f) : vec3(0.5f, 0.02f, 0.02f);
        di.hitFlash = a.hitFlash;
        di.castShadow = d < 60.0f;
        if (a.kind == AK_DRONE) {
            di.mesh = &g_droneMesh;
            float bank = clampf(-a.vel.x * 0.05f, -0.4f, 0.4f);
            di.model = mat4_trs(a.pos + vec3(0, sinf(g.time * 2.0f + i) * 0.1f, 0), quat_axis_angle(vec3(0, 1, 0), a.yaw) * quat_axis_angle(vec3(0, 0, 1), bank), vec3(1.4f, 1.4f, 1.4f));
            renderer_submit(di);
            if (actor_alive(a)) {
                Light l = {a.pos - vec3(0, 0.4f, 0), 8.0f, vec3(0.3f, 0.75f, 1.0f) * 2.0f, vec3(0, -1, 0), 0};
                renderer_add_light(l);
            }
            continue;
        }
        if (!a.model) continue;
        di.mesh = &a.model->mesh;
        di.model = a.ragdoll ? mat4_identity() : a.world;
        di.bones = a.skin;
        di.boneCount = B_COUNT;
        renderer_submit(di);
        if (a.state != AS_VEHICLE || i == PLAYER) weapon_render(a);
        // Alien glow lights
        if (a.faction == FAC_ALIEN && actor_alive(a) && d < 50.0f) {
            Light l = {actor_head(a), a.app.body == BODY_TENDRIL ? 7.0f : 4.0f, a.app.accent * (a.app.body == BODY_TENDRIL ? 3.0f : 0.8f), vec3(0, -1, 0), 0};
            if (a.app.body == BODY_TENDRIL) l.pos = actor_chest(a);
            renderer_add_light(l);
        }
    }
}
