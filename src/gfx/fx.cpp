// FIGHTO - particle and decal systems (blood, sparks, smoke, fire, impacts).
#include "renderer_internal.h"

#define MAX_PARTICLES 6000
#define MAX_DECALS 1400

static Particle g_particles[MAX_PARTICLES];
static int g_particleCount;
static Decal g_decals[MAX_DECALS];
static int g_decalNext;
static int g_goreLevel = 2;
static Rng g_fxRng = {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

void fx_set_gore_level(int level) { g_goreLevel = clamp_t(level, 0, 2); }
int fx_gore_level() { return g_goreLevel; }
int fx_particle_count() { return g_particleCount; }
int fx_decal_count() {
    int n = 0;
    for (int i = 0; i < MAX_DECALS; i++) n += g_decals[i].active ? 1 : 0;
    return n;
}

void fx_clear() {
    g_particleCount = 0;
    memset(g_decals, 0, sizeof(g_decals));
    g_decalNext = 0;
}

Particle* fx_spawn(ParticleType type) {
    if ((type == PT_BLOOD) && g_goreLevel == 0) return 0;
    Particle* p;
    if (g_particleCount < MAX_PARTICLES) {
        p = &g_particles[g_particleCount++];
    } else {
        // Recycle the particle closest to death
        p = &g_particles[g_fxRng.next() % MAX_PARTICLES];
    }
    memset(p, 0, sizeof(*p));
    p->type = (u8)type;
    p->alpha = 1.0f;
    p->size = 0.1f;
    p->life = p->maxLife = 1.0f;
    p->color = vec3(1, 1, 1);
    p->seed = g_fxRng.f01();
    p->rot = g_fxRng.range(0, TAU_F);
    return p;
}

Decal* fx_decal(DecalType type, vec3 pos, vec3 normal, float size, vec3 color, float seed) {
    if ((type == DT_BLOOD_SPLAT || type == DT_BLOOD_POOL || type == DT_SMEAR) && g_goreLevel == 0) return 0;
    Decal* d = &g_decals[g_decalNext];
    g_decalNext = (g_decalNext + 1) % MAX_DECALS;
    memset(d, 0, sizeof(*d));
    d->active = true;
    d->type = (u8)type;
    d->pos = pos;
    d->normal = normalize(normal);
    vec3 ref = fabsf(d->normal.y) > 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 t = normalize(cross(ref, d->normal));
    // Random rotation around the normal
    float a = seed * TAU_F;
    vec3 b = cross(d->normal, t);
    d->tangent = normalize(t * cosf(a) + b * sinf(a));
    d->size = size;
    d->sizeX = 1.0f;
    d->color = color;
    d->alpha = 1.0f;
    d->seed = seed * 10.0f;
    d->grow = 1.0f;
    d->growTarget = 1.0f;
    return d;
}

void fx_update(float dt, float groundHeightFn(vec3 p)) {
    for (int i = 0; i < g_particleCount;) {
        Particle& p = g_particles[i];
        p.life -= dt;
        if (p.life <= 0) {
            g_particles[i] = g_particles[--g_particleCount];
            continue;
        }
        p.vel.y -= p.gravity * dt;
        float dragF = 1.0f / (1.0f + p.drag * dt);
        p.vel = p.vel * dragF;
        vec3 np = p.pos + p.vel * dt;
        p.size += p.sizeGrow * dt;
        if (p.gravity > 0.5f && groundHeightFn) {
            float gh = groundHeightFn(np);
            if (np.y < gh + 0.01f && p.pos.y >= gh - 0.3f) {
                if (p.spawnDecal && g_goreLevel > 0) {
                    float s = p.size * g_fxRng.range(2.5f, 5.0f) * (g_goreLevel == 2 ? 1.3f : 1.0f);
                    fx_decal(p.spawnDecal == 2 ? DT_ICHOR : DT_BLOOD_SPLAT, vec3(np.x, gh + 0.01f, np.z), vec3(0, 1, 0), s, p.color, g_fxRng.f01());
                    g_particles[i] = g_particles[--g_particleCount];
                    continue;
                }
                np.y = gh + 0.01f;
                p.vel.y = -p.vel.y * 0.3f;
                p.vel.x *= 0.5f;
                p.vel.z *= 0.5f;
            }
        }
        p.pos = np;
        i++;
    }
    for (int i = 0; i < MAX_DECALS; i++) {
        Decal& d = g_decals[i];
        if (!d.active) continue;
        d.age += dt;
        if (d.growRate > 0 && d.grow < d.growTarget) d.grow = fminf(d.growTarget, d.grow + d.growRate * dt * (1.0f / (1.0f + d.grow)));
    }
}

static void push_vertex(Array<FxVertex>* verts, vec3 p, vec2 uv, vec3 color, float alpha, float p0, float p1, float p2, float p3) {
    FxVertex v;
    v.pos = p;
    v.uv = uv;
    v.col[0] = (u8)(saturate(color.x) * 255.0f);
    v.col[1] = (u8)(saturate(color.y) * 255.0f);
    v.col[2] = (u8)(saturate(color.z) * 255.0f);
    v.col[3] = (u8)(saturate(alpha) * 255.0f);
    v.params[0] = p0; v.params[1] = p1; v.params[2] = p2; v.params[3] = p3;
    verts->push(v);
}

void fx_push_quad(Array<FxVertex>* verts, Array<u32>* inds, vec3 a, vec3 b, vec3 c, vec3 d, vec3 color, float alpha, float p0, float p1, float p2, float p3) {
    u32 base = (u32)verts->count;
    push_vertex(verts, a, vec2(-1, -1), color, alpha, p0, p1, p2, p3);
    push_vertex(verts, b, vec2(1, -1), color, alpha, p0, p1, p2, p3);
    push_vertex(verts, c, vec2(1, 1), color, alpha, p0, p1, p2, p3);
    push_vertex(verts, d, vec2(-1, 1), color, alpha, p0, p1, p2, p3);
    inds->push(base); inds->push(base + 1); inds->push(base + 2);
    inds->push(base); inds->push(base + 2); inds->push(base + 3);
}

static int g_sortIdx[MAX_PARTICLES];
static float g_sortKey[MAX_PARTICLES];

static void sort_particles(int n) {
    // Simple insertion sort on mostly-coherent data, back to front
    for (int i = 1; i < n; i++) {
        int idx = g_sortIdx[i];
        float k = g_sortKey[i];
        int j = i - 1;
        while (j >= 0 && g_sortKey[j] < k) {
            g_sortIdx[j + 1] = g_sortIdx[j];
            g_sortKey[j + 1] = g_sortKey[j];
            j--;
        }
        g_sortIdx[j + 1] = idx;
        g_sortKey[j + 1] = k;
    }
}

void fx_build_particles(Array<FxVertex>* verts, Array<u32>* inds, vec3 camPos, vec3 right, vec3 up, const Frustum& fr) {
    int n = 0;
    for (int i = 0; i < g_particleCount; i++) {
        const Particle& p = g_particles[i];
        if (!frustum_sphere(fr, p.pos, p.size * 3.0f + length(p.vel) * p.stretch)) continue;
        g_sortIdx[n] = i;
        g_sortKey[n] = length_sq(p.pos - camPos);
        n++;
    }
    if (n < 1500) sort_particles(n);
    verts->reserve(verts->count + n * 4);
    inds->reserve(inds->count + n * 6);
    for (int k = 0; k < n; k++) {
        const Particle& p = g_particles[g_sortIdx[k]];
        float t = 1.0f - p.life / p.maxLife;
        float alpha = p.alpha;
        if (p.type == PT_SMOKE) alpha *= smoothstepf(0.0f, 0.15f, t) * (1.0f - t);
        else if (p.type == PT_FIRE || p.type == PT_SPARK || p.type == PT_MUZZLE || p.type == PT_RING) alpha *= 1.0f - t;
        else if (p.type == PT_BLOOD) alpha *= 1.0f - smoothstepf(0.7f, 1.0f, t);
        else alpha *= 1.0f - t * t;
        float s = p.size;
        if (p.stretch > 0.0f && length_sq(p.vel) > 0.01f) {
            // Velocity aligned streak
            vec3 v = p.vel * p.stretch;
            vec3 a = p.pos - v, b = p.pos;
            vec3 dir = normalize(b - a);
            vec3 toCam = normalize(camPos - p.pos);
            vec3 side = normalize(cross(dir, toCam)) * s;
            vec3 ext = dir * s;
            fx_push_quad(verts, inds, a - side - ext, a + side - ext, b + side + ext, b - side + ext, p.color, alpha,
                         p.type == PT_BLOOD ? (float)PT_STREAK : (float)p.type, p.type == PT_SPARK ? p.emissive : (p.type == PT_BLOOD ? 0.0f : p.emissive), p.seed * 10.0f, 0);
        } else {
            float c = cosf(p.rot), sn = sinf(p.rot);
            vec3 rx = (right * c + up * sn) * s;
            vec3 ry = (up * c - right * sn) * s;
            fx_push_quad(verts, inds, p.pos - rx - ry, p.pos + rx - ry, p.pos + rx + ry, p.pos - rx + ry, p.color, alpha, (float)p.type, p.emissive, p.seed * 10.0f, 0);
        }
    }
}

void fx_build_decals(Array<FxVertex>* verts, Array<u32>* inds, vec3 camPos, float maxDist, const Frustum& fr) {
    for (int i = 0; i < MAX_DECALS; i++) {
        const Decal& d = g_decals[i];
        if (!d.active) continue;
        float s = d.size * (d.type == DT_BLOOD_POOL ? 1.0f : 1.0f);
        if (length_sq(d.pos - camPos) > maxDist * maxDist) continue;
        if (!frustum_sphere(fr, d.pos, s * 1.5f)) continue;
        vec3 t = d.tangent * s * d.sizeX;
        vec3 b = cross(d.normal, d.tangent) * s;
        vec3 p = d.pos + d.normal * 0.012f;
        float alpha = d.alpha * (d.type == DT_BULLET_HOLE ? 1.0f : 0.95f);
        fx_push_quad(verts, inds, p - t - b, p + t - b, p + t + b, p - t + b, d.color, alpha, (float)d.type, 0, d.seed, d.grow);
    }
}
